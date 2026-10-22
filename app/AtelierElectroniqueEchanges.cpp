// Prométhée : échanges de la conception électronique dans l'atelier (lot L10) : netlist en entrée, session d'un
// routeur externe, carte relue des fichiers de fabrication. Licence GPL-3.0-only.
#include <map>

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "FenetreSimulation.hpp"
#include "promethee/echanges_electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

QStringList AtelierElectronique::importerNetlist(const QString& chemin, const QString& csv) {
  if (!conceptionCourante()) return {};
  NetlistLue n;
  try {
    n = lireNetlist(prom::lireFichier(prom::cheminFichier(chemin.toStdString())));
  } catch (const std::exception& e) {
    afficherMessage(QStringLiteral("Netlist illisible : %1").arg(q(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    return {};
  }
  if (!n.erreurs.empty()) {
    afficherMessage(QStringLiteral("Netlist refusée : %1").arg(q(n.erreurs.front()).toHtmlEscaped()), NiveauMessage::Erreur);
    return {};
  }
  BilanNetlist b;
  modifierConception([&](ConceptionElec& c) { b = mettreAJourDepuisNetlist(c, n); }, QStringLiteral("Mise à jour depuis la netlist"));
  if (!csv.isEmpty()) prom::ecrireFichierAtomique(prom::cheminFichier(csv.toStdString()), rapportNetlistCsv(b));
  QStringList lignes;
  for (const auto& l : b.lignes) lignes << q(l);
  afficherMessage(QStringLiteral("<b>Netlist</b> : %1 composant(s) ajouté(s), %2 valeur(s), %3 empreinte(s), %4 liaison(s) changée(s)%5.")
                      .arg(b.ajoutes)
                      .arg(b.valeurs)
                      .arg(b.empreintes)
                      .arg(b.liaisons)
                      .arg(b.nonChangees ? QStringLiteral(" ; <b>%1</b> écart(s) laissé(s) sur un schéma câblé").arg(b.nonChangees) : QString()),
                  b.nonChangees ? NiveauMessage::Avertissement : NiveauMessage::Succes);
  return lignes;
}

int AtelierElectronique::importerSessionSpecctra(const QString& chemin) {
  if (!conceptionCourante()) return 0;
  SessionSpecctra s;
  try {
    s = lireSpecctraSes(prom::lireFichier(prom::cheminFichier(chemin.toStdString())));
  } catch (const std::exception& e) {
    afficherMessage(QStringLiteral("Session illisible : %1").arg(q(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    return 0;
  }
  if (!s.erreurs.empty()) {
    afficherMessage(QStringLiteral("Session refusée : %1").arg(q(s.erreurs.front()).toHtmlEscaped()), NiveauMessage::Erreur);
    return 0;
  }
  int n = 0;
  modifierConception([&](ConceptionElec& c) { n = appliquerSessionSpecctra(c, s); }, QStringLiteral("Routage du routeur externe"));
  afficherMessage(QStringLiteral("<b>Session Specctra</b> : %1 piste(s) et %2 via(s) posées (la DRC suit).").arg(n).arg(s.vias.size()), NiveauMessage::Succes);
  return n;
}

bool AtelierElectronique::ajouterCarteDepuisGerber(const QString& dossier) {
  // Couches de cuivre d'après leur fonction X2 (« Copper,L1,Top »), sinon d'après le nom KiCad (F_Cu, B_Cu) ;
  // perçages métallisés (PTH) ; contour (Profile, Edge_Cuts)
  std::map<Couche, std::string> cuivres;
  std::string pth, contour;
  QDirIterator it(dossier, {QStringLiteral("*.gbr"), QStringLiteral("*.gtl"), QStringLiteral("*.gbl"), QStringLiteral("*.gko"), QStringLiteral("*.gm1"), QStringLiteral("*.drl"),
                            QStringLiteral("*.xln")},
                  QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString f = it.next();
    std::string t;
    try {
      t = prom::lireFichier(prom::cheminFichier(f.toStdString()));
    } catch (const std::exception&) {
      continue;
    }
    const QString nom = QFileInfo(f).fileName();
    if (nom.endsWith(QLatin1String(".drl")) || nom.endsWith(QLatin1String(".xln"))) {
      if (t.find("NonPlated") == std::string::npos && !nom.contains(QLatin1String("NPTH"))) pth += t;
      continue;
    }
    if (t.find("FileFunction,Profile") != std::string::npos || nom.contains(QLatin1String("Edge_Cuts")) || nom.endsWith(QLatin1String(".gko")) || nom.endsWith(QLatin1String(".gm1")))
      contour = t;
    else if (t.find("FileFunction,Copper,L1,") != std::string::npos || nom.contains(QLatin1String("F_Cu")) || nom.endsWith(QLatin1String(".gtl")))
      cuivres[Couche::CuivreDessus] = t;
    else if ((t.find("FileFunction,Copper,") != std::string::npos && t.find(",Bot") != std::string::npos) || nom.contains(QLatin1String("B_Cu")) || nom.endsWith(QLatin1String(".gbl")))
      cuivres[Couche::CuivreDessous] = t;
  }
  if (cuivres.empty()) {
    afficherMessage(QStringLiteral("Aucune couche de cuivre trouvée dans « %1 ».").arg(dossier.toHtmlEscaped()), NiveauMessage::Erreur);
    return false;
  }
  const CarteRelue r = relireCarte(cuivres, pth);
  ConceptionElec c = conceptionDepuisCarteRelue(r, QFileInfo(dossier).fileName().toStdString(), contour, pth);
  if (cuivres.size() == 1) c.circuit.empilage.couchesCuivre = cuivres.count(Couche::CuivreDessus) ? 2 : 1;
  m_doc->modifier([&c](prom::Projet& p) { p.electronique.push_back(c); });
  ouvrir(static_cast<int>(m_doc->projet().electronique.size()) - 1, Circuit);
  afficherMessage(QStringLiteral("<b>Carte relue</b> : %1 pastille(s), %2 piste(s), %3 via(s), %4 réseau(x) tirés du cuivre ; un schéma de liaison la complète.")
                      .arg(r.pastilles.size())
                      .arg(r.pistes.size())
                      .arg(r.vias.size())
                      .arg(r.reseaux),
                  NiveauMessage::Succes);
  return true;
}

// ---------- Simulation (lot L11) ----------
FenetreSimulation* AtelierElectronique::simulation() const { return m_simulation; }

void AtelierElectronique::ouvrirSimulation() {
  if (!conceptionCourante()) return;
  if (!m_simulation) m_simulation = new FenetreSimulation(*this, this);
  else m_simulation->refaire();
  m_simulation->show();
  m_simulation->raise();
  m_simulation->activateWindow();
}

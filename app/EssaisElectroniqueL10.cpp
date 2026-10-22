// Prométhée : essais d'interface du lot L10 de l'audit (« Échanges ») : une netlist appliquée à la conception ouverte
// en un seul pas d'annulation, avec son rapport CSV ; la session d'un routeur externe posée en un pas ; un dossier
// Gerber relu en carte modifiable, ajoutée au projet, aux mêmes réseaux.
// Appelés à la fin de « Promethee --essais-electronique », ou seuls avec PROMETHEE_ESSAIS_LOTS=L10.
// Licence GPL-3.0-only.
#include <functional>
#include <set>
#include <string>

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "promethee/echanges_electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
const ConceptionElec* conception(Document* doc, std::size_t i = 0) { return doc->projet().electronique.size() <= i ? nullptr : &doc->projet().electronique[i]; }
AtelierElectronique* ouvrir(FenetrePrincipale& f, ConceptionElec c, int mode) {
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = c.nom;
  p.electronique.push_back(std::move(c));
  f.document()->creer(p);
  f.montrerElectronique(0, mode);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) return nullptr;
  for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur, AtelierElectronique::Panneau::Verifications,
                       AtelierElectronique::Panneau::Recherche, AtelierElectronique::Panneau::Apprendre, AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts,
                       AtelierElectronique::Panneau::Champs, AtelierElectronique::Panneau::APlacer})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  attendre(150);
  a->recalculer();
  return a;
}
QString dossierEssai(const QString& nom) {
  const QString d = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-essais-l10-") + nom);
  QDir(d).removeRecursively();
  QDir().mkpath(d);
  return d;
}
std::set<std::set<std::string>> partition(const NetlistLue& n) {
  std::set<std::set<std::string>> r;
  for (const auto& [nom, noeuds] : n.reseaux) {
    std::set<std::string> s;
    for (const auto& [ref, b] : noeuds) s.insert(ref + "." + b);
    if (s.size() >= 2) r.insert(s);
  }
  return r;
}
}  // namespace

void essaisElectroniqueL10(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  const ConceptionElec origine = exempleElectronique();

  // ---------- 10.7 · Netlist appliquée en un pas, rapport CSV ----------
  {
    const NetlistLue lue = lireNetlist(netlistKicad(origine, connectivite(origine)));
    AtelierElectronique* a = ouvrir(f, conceptionDepuisNetlist(lue, "Depuis la netlist"), AtelierElectronique::Circuit);
    if (!a) {
      verifier(false, "L10 : la conception d'essai s'ouvre", {});
      return;
    }
    verifier(reel::activer(&f), "L10 : la fenêtre est active", {});
    // R1 à 22k, R99 ajoutée entre deux réseaux, écrite au format de Prométhée
    NetlistLue n2 = lue;
    for (auto& x : n2.composants)
      if (x.reference == "R1") x.valeur = "22k";
    n2.composants.push_back({"R99", "1k", "R_0603"});
    auto it = n2.reseaux.begin();
    const std::string r1 = it->first, r2 = std::next(it)->first;
    n2.reseaux[r1].push_back({"R99", "1"});
    n2.reseaux[r2].push_back({"R99", "2"});
    std::string texte = "# Netlist d'essai\nComposants\n";
    for (const auto& x : n2.composants) texte += "  " + x.reference + "\t" + x.valeur + "\t" + x.empreinte + "\n";
    texte += "\nRéseaux\n";
    for (const auto& [nom, noeuds] : n2.reseaux) {
      texte += "  " + nom + " :";
      for (const auto& [ref, b] : noeuds) texte += " " + ref + "." + b;
      texte += "\n";
    }
    const QString d = dossierEssai(QStringLiteral("netlist"));
    const QString fichier = QDir(d).filePath(QStringLiteral("essai.net")), csv = QDir(d).filePath(QStringLiteral("essai-changements.csv"));
    prom::ecrireFichierAtomique(prom::cheminFichier(fichier.toStdString()), texte);
    const QStringList lignes = a->importerNetlist(fichier, csv);
    attendre(200);
    const ConceptionElec* c = conception(doc);
    bool r99 = false;
    for (const auto& e : c->circuit.empreintes) r99 |= c->composant(e.composant) && c->composant(e.composant)->reference == "R99";
    const bool applique = r99 && c->parReference("R1") && c->parReference("R1")->valeur == "22k" && partition(netlistDe(*c)) == partition(n2);
    doc->annuler();
    attendre(200);
    c = conception(doc);
    const bool defait = !c->parReference("R99") && c->parReference("R1")->valeur != "22k";
    verifier(applique && defait && QFile::exists(csv) && lignes.size() >= 2, "10.7 : une netlist met la carte à jour (composant ajouté, valeur, liaisons) en un pas, rapport CSV écrit",
             std::to_string(lignes.size()) + " ligne(s) au rapport");
  }

  // ---------- 10.11 · Session d'un routeur externe posée en un pas ----------
  {
    ConceptionElec sans = origine;
    sans.circuit.pistes.clear();
    sans.circuit.vias.clear();
    AtelierElectronique* a = ouvrir(f, sans, AtelierElectronique::Circuit);
    if (!a) return;
    const QString fichier = QDir(dossierEssai(QStringLiteral("specctra"))).filePath(QStringLiteral("carte.ses"));
    prom::ecrireFichierAtomique(prom::cheminFichier(fichier.toStdString()), specctraSes(origine));
    const int n = a->importerSessionSpecctra(fichier);
    attendre(200);
    const ConceptionElec* c = conception(doc);
    const Connectivite k = connectivite(*c);
    const bool route = etatRoutage(*c, k, remplirZones(*c, k)).chevelu.empty();
    doc->annuler();
    attendre(150);
    verifier(n == static_cast<int>(origine.circuit.pistes.size()) && route && conception(doc)->circuit.pistes.empty(),
             "10.11 : la session SES d'un routeur externe route la carte en un pas, défait d'un seul Ctrl+Z", std::to_string(n) + " piste(s)");
  }

  // ---------- 10.13 · Dossier Gerber relu en carte modifiable ----------
  {
    AtelierElectronique* a = ouvrir(f, origine, AtelierElectronique::Circuit);
    if (!a) return;
    const QString d = dossierEssai(QStringLiteral("gerber"));
    a->exporterFabrication(d);
    const bool ajoutee = a->ajouterCarteDepuisGerber(d);
    attendre(300);
    const ConceptionElec* c = conception(doc, 1);
    const bool memes = c && partition(netlistDe(*c)) == partition(netlistDe(origine));
    verifier(ajoutee && doc->projet().electronique.size() == 2 && memes && c && c->circuit.pistes.size() == origine.circuit.pistes.size(),
             "10.13 : un dossier Gerber relu devient une carte modifiable du projet, aux mêmes réseaux que la conception d'origine",
             c ? std::to_string(c->circuit.empreintes.size()) + " empreinte(s), " + std::to_string(c->circuit.pistes.size()) + " piste(s)" : std::string("non ajoutée"));
  }
}

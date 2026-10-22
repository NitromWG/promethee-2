// Prométhée : essais d'interface du lot L8 de l'audit (« Fabrication et cartes complexes ») : règle changée dans le
// gestionnaire de contraintes depuis le schéma, retrouvée au circuit, et l'inverse, chaque fois en un pas
// d'annulation ; routage complet de l'exemple au NE555 par le ruban ; panneau 3 × 2 écrit et relu ; segment retiré à
// la main d'un Gerber écrit, signalé au contrôle avant l'envoi ; devis de dix cartes ; visionneuse HTML.
// Appelés à la fin de « Promethee --essais-electronique », ou seuls avec PROMETHEE_ESSAIS_LOTS=L8.
// Licence GPL-3.0-only.
#include <chrono>
#include <cmath>
#include <functional>
#include <string>

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QLocale>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetreDevis.hpp"
#include "FenetrePrincipale.hpp"
#include "GestionnaireContraintes.hpp"
#include "promethee/fabrication_avancee.hpp"
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
const ConceptionElec* conception(Document* doc) { return doc->projet().electronique.empty() ? nullptr : &doc->projet().electronique[0]; }
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
  const QString d = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-essais-l8-") + nom);
  QDir(d).removeRecursively();
  QDir().mkpath(d);
  return d;
}
double valeurCellule(QTableWidget* t, int l, int c) {
  const QTableWidgetItem* it = t ? t->item(l, c) : nullptr;
  return it ? QLocale().toDouble(it->text()) : -1;
}
}  // namespace

void essaisElectroniqueL8(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();

  // ---------- Gestionnaire de contraintes : schéma → circuit, puis circuit → schéma, un pas chaque fois ----------
  {
    AtelierElectronique* a = ouvrir(f, exempleElectronique(), AtelierElectronique::Schema);
    if (!a) {
      verifier(false, "L8 : l'exemple s'ouvre", {});
      return;
    }
    verifier(reel::activer(&f), "L8 : la fenêtre est active", {});
    a->executerAction("regles.contraintes");
    attendre(250);
    GestionnaireContraintes* g = a->contraintes();
    const bool ouvert = g && g->isVisible() && g->origine() == QStringLiteral("schéma");
    // Un premier pas sans rapport (une classe ajoutée), puis la règle
    const std::size_t classes0 = conception(doc)->circuit.regles.classes.size();
    if (g) g->ajouterClasse();
    const Nm largeur0 = conception(doc)->circuit.regles.classes.empty() ? 0 : conception(doc)->circuit.regles.classes.back().largeur;
    const int ligne = static_cast<int>(classes0) + 1;
    const bool change = g && g->changer(ligne, GestionnaireContraintes::Largeur, 0.6);
    attendre(150);
    const bool auModele = conception(doc)->circuit.regles.classes.size() == classes0 + 1 && conception(doc)->circuit.regles.classes.back().largeur == nm(0.6);
    // Au circuit, la même fenêtre la montre
    f.montrerElectronique(0, AtelierElectronique::Circuit);
    attendre(300);
    a->executerAction("regles.contraintes");
    attendre(200);
    const double vueCircuit = valeurCellule(g ? g->regles() : nullptr, ligne, GestionnaireContraintes::Largeur);
    const bool origineCircuit = g && g->origine() == QStringLiteral("circuit");
    doc->annuler();
    attendre(200);
    const bool unPas = conception(doc)->circuit.regles.classes.size() == classes0 + 1 && conception(doc)->circuit.regles.classes.back().largeur == largeur0;
    char t[200];
    std::snprintf(t, sizeof t, "largeur %.3f au circuit ; après annulation %.3f mm, %zu classe(s)", vueCircuit, mm(conception(doc)->circuit.regles.classes.back().largeur),
                  conception(doc)->circuit.regles.classes.size());
    verifier(ouvert && change && auModele && std::abs(vueCircuit - 0.6) < 1e-9 && origineCircuit && unPas,
             "règle changée dans le gestionnaire de contraintes au schéma : le circuit la voit, et une seule annulation la défait", t);

    // L'inverse : saisie dans la cellule au circuit, relue au schéma
    const Nm iso0 = conception(doc)->circuit.regles.isolement;
    if (g) {
      if (QTableWidgetItem* it = g->regles()->item(0, GestionnaireContraintes::Isolement)) it->setText(QLocale().toString(0.8, 'f', 3));
    }
    attendre(200);
    const bool saisie = conception(doc)->circuit.regles.isolement == nm(0.8);
    f.montrerElectronique(0, AtelierElectronique::Schema);
    attendre(300);
    a->executerAction("regles.contraintes");
    attendre(200);
    const double vueSchema = valeurCellule(g ? g->regles() : nullptr, 0, GestionnaireContraintes::Isolement);
    doc->annuler();
    attendre(200);
    const bool unPas2 = conception(doc)->circuit.regles.isolement == iso0 && conception(doc)->circuit.regles.classes.size() == classes0 + 1;
    // Valeur sous le minimum : refusée, le modèle ne bouge pas
    const bool refuse = g && !g->changer(0, GestionnaireContraintes::Isolement, 0.01) && conception(doc)->circuit.regles.isolement == iso0;
    std::snprintf(t, sizeof t, "isolement %.3f au schéma ; après annulation %.3f mm", vueSchema, mm(conception(doc)->circuit.regles.isolement));
    verifier(saisie && std::abs(vueSchema - 0.8) < 1e-9 && unPas2 && refuse, "règle saisie au circuit : relue au schéma, défaite en un pas ; une valeur hors bornes est refusée", t);
    if (g) g->close();
  }

  // ---------- 8.9 · Routage complet par le ruban : l'exemple au NE555, sans violation, en moins de 10 s ----------
  {
    ConceptionElec c0 = exempleElectronique();
    c0.circuit.pistes.clear();
    c0.circuit.vias.clear();
    AtelierElectronique* a = ouvrir(f, c0, AtelierElectronique::Circuit);
    if (!a) return;
    const auto t0 = std::chrono::steady_clock::now();
    a->executerAction("circuit.routerComplet");
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    attendre(300);
    const ConceptionElec* c = conception(doc);
    const Connectivite k = connectivite(*c);
    const Remplissages z = remplirZones(*c, k);
    const auto chevelu = etatRoutage(*c, k, z).chevelu.size();
    int err = 0;
    for (const auto& v : verifierDRCReglee(*c, k, z)) err += v.gravite == Gravite::Erreur ? 1 : 0;
    const std::size_t posees = c->circuit.pistes.size();
    doc->annuler();
    attendre(200);
    const bool unPas = conception(doc)->circuit.pistes.empty();
    char t[160];
    std::snprintf(t, sizeof t, "%.2f s, %zu liaison(s) restante(s), %d erreur(s) DRC, %zu piste(s) posée(s)", s, chevelu, err, posees);
    verifier(chevelu == 0 && err == 0 && s < 10 && unPas && posees > 0, "8.9 : « Router tout » route l'exemple au NE555 en entier, sans violation, en moins de 10 s, en un pas", t);
  }

  // ---------- 8.8 · Panneau 3 × 2, écrit puis relu ; 8.19 · fichier retouché signalé avant l'envoi ----------
  {
    AtelierElectronique* a = ouvrir(f, exempleElectronique(), AtelierElectronique::Circuit);
    if (!a) return;
    const QString d = dossierEssai(QStringLiteral("panneau"));
    QStringList defauts;
    const QStringList ecrits = a->ecrirePanneau(d, OptionsPanneau{}, &defauts);
    QString cuivre;
    for (const auto& x : ecrits)
      if (x.endsWith(QStringLiteral("-F_Cu.gbr"))) cuivre = x;
    std::size_t pastillesPanneau = 0, pastillesCarte = 0;
    if (!cuivre.isEmpty())
      for (const auto& o : lireGerber(prom::lireFichier(prom::cheminFichier(cuivre.toStdString()))).objets) pastillesPanneau += o.genre != ObjetGerber::Genre::Trait && !o.reference.empty() ? 1 : 0;
    {
      const ConceptionElec* c = conception(doc);
      const Connectivite k = connectivite(*c);
      const ModeleFabrication m = modeleFabrication(*c, k, remplirZones(*c, k));
      for (const auto& o : lireGerber(gerber(m, Couche::CuivreDessus, "essai")).objets) pastillesCarte += o.genre != ObjetGerber::Genre::Trait && !o.reference.empty() ? 1 : 0;
    }
    char t[200];
    std::snprintf(t, sizeof t, "%lld fichier(s), %zu pastille(s) relue(s) pour %zu par carte, %lld défaut(s)", static_cast<long long>(ecrits.size()), pastillesPanneau, pastillesCarte,
                  static_cast<long long>(defauts.size()));
    verifier(defauts.isEmpty() && ecrits.size() >= 6 && pastillesCarte > 0 && pastillesPanneau == 6 * pastillesCarte, "8.8 : panneau 3 × 2 en languettes : contrôle sans défaut, Gerber écrits et relus", t);

    // Dossier de fabrication écrit, contrôlé ; un segment de piste retiré à la main du cuivre du dessus
    const QString dossier = dossierEssai(QStringLiteral("fabrication"));
    a->exporterFabrication(dossier);
    const QStringList avant = a->controlerFichiers(dossier);
    QString fichier;
    for (const QString& x : QDir(QDir(dossier).filePath(QStringLiteral("Gerber"))).entryList({QStringLiteral("*-F_Cu.gbr")})) fichier = QDir(QDir(dossier).filePath(QStringLiteral("Gerber"))).filePath(x);
    bool retire = false;
    if (!fichier.isEmpty()) {
      const std::string texte = prom::lireFichier(prom::cheminFichier(fichier.toStdString()));
      std::string sortie, reseau, precedente;
      bool region = false;
      std::size_t debut = 0;
      while (debut < texte.size()) {
        const std::size_t fin = texte.find('\n', debut);
        const std::string ligne = texte.substr(debut, fin == std::string::npos ? std::string::npos : fin - debut);
        debut = fin == std::string::npos ? texte.size() : fin + 1;
        if (ligne.rfind("%TO.N,", 0) == 0) reseau = ligne.substr(6, ligne.find('*') - 6);
        if (ligne.rfind("%TD", 0) == 0 && (ligne == "%TD*%" || ligne.rfind("%TD.N", 0) == 0)) reseau.clear();
        if (ligne.rfind("G36", 0) == 0) region = true;
        if (ligne.rfind("G37", 0) == 0) region = false;
        const bool d01 = ligne.size() > 4 && ligne.compare(ligne.size() - 4, 4, "D01*") == 0;
        const bool apresD02 = precedente.size() > 4 && precedente.compare(precedente.size() - 4, 4, "D02*") == 0;
        precedente = ligne;
        if (!retire && !region && d01 && apresD02 && !reseau.empty() && reseau != "GND") {
          retire = true;
          continue;
        }
        sortie += ligne + "\n";
      }
      prom::ecrireFichierAtomique(prom::cheminFichier(fichier.toStdString()), sortie);
    }
    const QStringList apres = a->controlerFichiers(dossier);
    bool coupure = false;
    for (const auto& x : apres) coupure |= x.contains(QStringLiteral("coupure entre"));
    std::snprintf(t, sizeof t, "%lld écart(s) avant, %lld après : %s", static_cast<long long>(avant.size()), static_cast<long long>(apres.size()),
                  apres.isEmpty() ? "" : apres.front().toStdString().c_str());
    verifier(avant.isEmpty() && retire && coupure, "8.19 : un segment retiré à la main d'un Gerber écrit est signalé avant l'envoi, avec ses deux pastilles", t);

    // ---------- 8.18 · Devis de dix cartes, côte à côte ----------
    a->executerAction("fabrication.devis");
    attendre(250);
    FenetreDevis* dv = a->fenetreDevis();
    const bool devisOk = dv && dv->isVisible() && dv->quantite()->value() == 10 && dv->table()->columnCount() == 2 && dv->offres().size() == 2 && dv->offres()[0].fabricant == "JLCPCB" &&
                         dv->offres()[1].fabricant == "PCBWay" && dv->offres()[0].prix > 0 && !dv->offres()[0].date.empty();
    std::snprintf(t, sizeof t, "%s %.2f / %s %.2f", dv && dv->offres().size() > 1 ? dv->offres()[0].fabricant.c_str() : "?", dv && !dv->offres().empty() ? dv->offres()[0].prix : 0.0,
                  dv && dv->offres().size() > 1 ? dv->offres()[1].fabricant.c_str() : "?", dv && dv->offres().size() > 1 ? dv->offres()[1].prix : 0.0);
    verifier(devisOk, "8.18 : le devis de dix cartes compare JLCPCB et PCBWay côte à côte, sans réseau", t);
    if (dv) dv->close();

    // ---------- 8.31 · Visionneuse HTML écrite ----------
    const QString page = QDir(dossierEssai(QStringLiteral("visionneuse"))).filePath(QStringLiteral("visionneuse.html"));
    const bool ecrite = a->exporterVisionneuse(page);
    const std::string h = ecrite ? prom::lireFichier(prom::cheminFichier(page.toStdString())) : std::string();
    verifier(ecrite && h.find("data-ref=\"U1\"") != std::string::npos && h.find("<script src") == std::string::npos,
             "8.31 : la visionneuse HTML s'écrit en une page autonome, repères cliquables", {});
  }
}

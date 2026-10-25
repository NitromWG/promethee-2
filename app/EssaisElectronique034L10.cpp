// Prométhée : essais intégrés de la 0.34, lot L10 — imports des autres logiciels par l'interface : projet EAGLE ouvert
// relié (10.5), carte KiCad de quatre couches ajoutée à la carte ouverte puis retirée d'un Ctrl+Z (10.12), bibliothèque
// EAGLE de 50 composants convertie en bibliothèque d'équipe avec son rapport (10.18), composant LCSC rangé avec son
// modèle 3D (10.6), nomenclature en entrée appliquée puis annulée (10.6). Licence GPL-3.0-only.
#include <algorithm>
#include <functional>
#include <set>
#include <string>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "EssaisReponses034.hpp"
#include "Document.hpp"
#include "FenetreChoixComposant.hpp"
#include "FenetrePrincipale.hpp"
#include "ImportsTiers034.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/dessins_carte.hpp"
#include "promethee/echanges_electronique.hpp"
#include "promethee/exports3d.hpp"
#include "promethee/idx.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/reprise_existant.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;
using namespace essais034;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
QString donnees() {
  QString dossier = qEnvironmentVariable("PROMETHEE_DONNEES");
  for (const QString& essai : {QCoreApplication::applicationDirPath() + QStringLiteral("/../../tests/donnees"), QStringLiteral("tests/donnees")})
    if (dossier.isEmpty() && QFileInfo::exists(essai + QStringLiteral("/eagle/ampli.sch"))) dossier = essai;
  return dossier;
}
AtelierElectronique* ouvrir(FenetrePrincipale& f, ConceptionElec c) {
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = c.nom;
  p.electronique.push_back(std::move(c));
  f.document()->creer(p);
  f.montrerElectronique(0, AtelierElectronique::Circuit);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) return nullptr;
  for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur, AtelierElectronique::Panneau::Verifications,
                       AtelierElectronique::Panneau::Recherche, AtelierElectronique::Panneau::Apprendre, AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts,
                       AtelierElectronique::Panneau::Champs, AtelierElectronique::Panneau::APlacer, AtelierElectronique::Panneau::Reseaux})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  attendre(150);
  return a;
}
bool ecrire(const QString& chemin, const std::string& texte) {
  try {
    prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), texte);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}
const char* const CARTE_4_COUCHES = R"((kicad_pcb (version 20221018) (generator pcbnew)
  (general (thickness 1.6))
  (layers (0 "F.Cu" signal) (1 "In1.Cu" signal) (2 "In2.Cu" signal) (31 "B.Cu" signal) (37 "F.SilkS" user) (44 "Edge.Cuts" user))
  (net 0 "") (net 1 "GND") (net 2 "SIG")
  (gr_rect (start 0 0) (end 30 20) (layer "Edge.Cuts") (width 0.1))
  (footprint "Resistor_SMD:R_0603_1608Metric" (layer "F.Cu") (at 10 10)
    (property "Reference" "R1") (property "Value" "10k")
    (pad "1" smd roundrect (at -0.8 0) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (net 1 "GND"))
    (pad "2" smd roundrect (at 0.8 0) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (net 2 "SIG")))
  (footprint "Resistor_SMD:R_0603_1608Metric" (layer "F.Cu") (at 20 10)
    (property "Reference" "R2") (property "Value" "10k")
    (pad "1" smd roundrect (at -0.8 0) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (net 2 "SIG"))
    (pad "2" smd roundrect (at 0.8 0) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (net 1 "GND")))
  (segment (start 10.8 10) (end 12 10) (width 0.25) (layer "F.Cu") (net 2))
  (via (at 12 10) (size 0.6) (drill 0.3) (layers "F.Cu" "B.Cu") (net 2))
  (segment (start 12 10) (end 18 10) (width 0.25) (layer "In1.Cu") (net 2))
  (via (at 18 10) (size 0.6) (drill 0.3) (layers "F.Cu" "B.Cu") (net 2))
  (segment (start 18 10) (end 19.2 10) (width 0.25) (layer "F.Cu") (net 2))
  (segment (start 5 15) (end 25 15) (width 0.5) (layer "In2.Cu") (net 1))
)
)";
}  // namespace

void essaisElectronique034L10(FenetrePrincipale& f, const Verifier& verifier) {
  Document* doc = f.document();
  const QString dossier = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("promethee-essais-034L10"));
  QDir(dossier).removeRecursively();
  QDir().mkpath(dossier);
  const QString d = donnees();
  if (d.isEmpty()) {
    std::printf("(données d'essai introuvables : imports du lot L10 non essayés)\n");
    return;
  }

  // ---------- 10.5 · Projet EAGLE ouvert : schéma et carte reliés ----------
  {
    const bool ok = f.importerConceptionTierce(d + QStringLiteral("/eagle/ampli.brd"), 0, false);
    attendre(600);
    bool relies = false;
    std::string detail;
    if (ok && !doc->projet().electronique.empty()) {
      const ConceptionElec& c = doc->projet().electronique.back();
      const Connectivite k = connectivite(c);
      const ComposantElec* ic = c.parReference("IC1");
      const ComposantElec* r3 = c.parReference("R3");
      int lies = 0;
      for (const auto& em : c.circuit.empreintes)
        if (const ComposantElec* x = c.composant(em.composant); x && !x->carteSeule && !x->symbole.empty()) ++lies;
      relies = c.feuilles.size() == 2 && lies == 5 && ic && r3 && k.nom(ic->uuid, "1") == "OUT" && k.nom(r3->uuid, "1") == "OUT";
      detail = std::to_string(c.feuilles.size()) + " feuille(s), " + std::to_string(lies) + " empreinte(s) reliée(s)";
    }
    AtelierElectronique* a = f.atelierElectronique();
    verifier(ok && relies && a && a->mode() == AtelierElectronique::Circuit, "10.5 : projet EAGLE ouvert d'un geste, schéma et carte reliés (OUT sur IC1.1 et R3.1, sur deux feuilles)", detail);
  }

  // ---------- 10.14 · Schéma KiCad 5 et circuit LTspice ouverts : réseaux de leurs propres netlists ----------
  {
    auto partition = [](const NetlistLue& n) {
      std::set<std::set<std::string>> p;
      for (const auto& [nom, noeuds] : n.reseaux) {
        std::set<std::string> e;
        for (const auto& [r, b] : noeuds) e.insert(r + "." + b);
        if (e.size() > 1) p.insert(e);
      }
      return p;
    };
    std::string detail;
    bool kicad5 = false, ltspice = false;
    if (f.importerConceptionTierce(d + QStringLiteral("/kicad5/filtre.sch"), 0, false)) {
      attendre(400);
      const ConceptionElec& c = doc->projet().electronique.back();
      const NetlistLue attendue = lireNetlist(prom::lireFichier(prom::cheminFichier((d + QStringLiteral("/kicad5/filtre.net")).toStdString())));
      kicad5 = c.feuilles.size() == 2 && c.composants.size() == 5 && partition(netlistDe(c)) == partition(attendue);
      detail += "KiCad 5 : " + std::to_string(c.composants.size()) + " composant(s), " + std::to_string(c.feuilles.size()) + " feuille(s) ; ";
    }
    if (f.importerConceptionTierce(d + QStringLiteral("/ltspice/rc.asc"), 0, false)) {
      attendre(400);
      const ConceptionElec& c = doc->projet().electronique.back();
      const NetlistLue attendue = lireNetlist(prom::lireFichier(prom::cheminFichier((d + QStringLiteral("/ltspice/rc.net")).toStdString())));
      ltspice = c.composants.size() == 4 && partition(netlistDe(c)) == partition(attendue) && c.extensions.count("directivesSpice") == 1;
      detail += "LTspice : " + std::to_string(c.composants.size()) + " composant(s)";
    }
    verifier(kicad5 && ltspice, "10.14 : schéma KiCad 5 (cache et feuille fille) et circuit LTspice ouverts, réseaux identiques aux netlists que ces logiciels écrivent", detail);
  }

  // ---------- 10.18 · Formats anciens ouverts d'un geste : carte Sprint-Layout, projet gEDA (schéma et carte) ----------
  {
    std::string detail;
    bool sprint = false, geda = false;
    if (f.importerConceptionTierce(d + QStringLiteral("/sprint/12F629_SM.lay6"), 0, false)) {
      attendre(400);
      const ConceptionElec& c = doc->projet().electronique.back();
      int plans = 0;
      for (const auto& z : c.circuit.zones) plans += z.reseau == "GND_PLANE";
      sprint = c.circuit.empreintes.size() == 14 && plans == 2;
      detail += "Sprint-Layout : " + std::to_string(c.circuit.empreintes.size()) + " empreinte(s), " + std::to_string(plans) + " plan(s) ; ";
    }
    if (f.importerConceptionTierce(d + QStringLiteral("/geda/goodfet50.sch"), 0, false)) {
      attendre(400);
      const ConceptionElec& c = doc->projet().electronique.back();
      geda = !c.schemaSeul && c.circuit.empreintes.size() == 16 && c.composants.size() >= 16;
      detail += "gEDA : " + std::to_string(c.composants.size()) + " composant(s), " + std::to_string(c.circuit.empreintes.size()) + " empreinte(s) reliée(s)";
    }
    verifier(sprint && geda, "10.18 : carte Sprint-Layout (plans de masse en zones) et projet gEDA (schéma gschem et sa carte reliés) ouverts d'un geste", detail);
  }

  // ---------- 10.12 · Carte KiCad de quatre couches ajoutée à la carte ouverte, un seul Ctrl+Z la retire ----------
  {
    AtelierElectronique* a = ouvrir(f, carteDeMesure(20, false));
    const QString pcb = QDir(dossier).filePath(QStringLiteral("ajout.kicad_pcb"));
    bool ok = a && ecrire(pcb, CARTE_4_COUCHES);
    const std::size_t n0 = doc->projet().electronique.empty() ? 0 : doc->projet().electronique[0].composants.size();
    // Le dialogue propose les couches sans retouche
    bool sansRetouche = false;
    if (ok) {
      std::string erreur;
      const auto imp = importerConceptionTierce("ajout.kicad_pcb", CARTE_4_COUCHES, {}, &erreur);
      if (imp) {
        DialogueImportConception dlg(*imp, &doc->projet().electronique[0]);
        const AssociationCouches as = dlg.association();
        sansRetouche = as.cuivresNecessaires == 4 && std::all_of(as.lignes.begin(), as.lignes.end(), [](const LigneAssociation& l) { return l.cible && *l.cible == l.lue; });
      }
    }
    ok = ok && f.importerConceptionTierce(pcb, 2, false);
    attendre(500);
    std::set<std::string> refs;
    bool unique = true;
    std::size_t n1 = 0;
    int cuivres = 0;
    if (!doc->projet().electronique.empty()) {
      const ConceptionElec& c = doc->projet().electronique[0];
      for (const auto& x : c.composants) unique = refs.insert(x.reference).second && unique;
      n1 = c.composants.size();
      cuivres = c.circuit.empilage.couchesCuivre;
    }
    doc->annuler();
    attendre(300);
    const ConceptionElec& apres = doc->projet().electronique[0];
    const bool retire = apres.composants.size() == n0 && apres.circuit.empilage.couchesCuivre == 2;
    verifier(ok && sansRetouche && unique && n1 == n0 + 2 && cuivres == 4 && retire,
             "10.12 : carte KiCad de quatre couches ajoutée à une carte de 20 composants : couches associées sans retouche, aucun repère en double, un seul Ctrl+Z la retire",
             std::to_string(n0) + " → " + std::to_string(n1) + " composants, " + std::to_string(cuivres) + " cuivres");
  }

  // ---------- 10.18 · Bibliothèque EAGLE de 50 composants → bibliothèque d'équipe, un rapport par objet ----------
  {
    AtelierElectronique* a = f.atelierElectronique();
    std::string lbr = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<eagle version=\"9.6.2\"><drawing><library name=\"maison\"><packages>"
                      "<package name=\"R0603\"><smd name=\"1\" x=\"-0.8\" y=\"0\" dx=\"0.9\" dy=\"0.95\" layer=\"1\"/><smd name=\"2\" x=\"0.8\" y=\"0\" dx=\"0.9\" dy=\"0.95\" layer=\"1\"/></package>"
                      "</packages><symbols><symbol name=\"R\"><pin name=\"1\" x=\"-5.08\" y=\"0\" length=\"short\" direction=\"pas\"/><pin name=\"2\" x=\"5.08\" y=\"0\" length=\"short\" direction=\"pas\" rot=\"R180\"/></symbol></symbols><devicesets>";
    for (int i = 0; i < 50; ++i)
      lbr += "<deviceset name=\"R" + std::to_string(i) + "K\" prefix=\"R\"><gates><gate name=\"G$1\" symbol=\"R\" x=\"0\" y=\"0\"/></gates><devices><device name=\"\" package=\"R0603\"><connects>"
             "<connect gate=\"G$1\" pin=\"1\" pad=\"1\"/><connect gate=\"G$1\" pin=\"2\" pad=\"2\"/></connects><technologies><technology name=\"\"/></technologies></device></devices></deviceset>";
    lbr += "</devicesets></library></drawing></eagle>\n";
    const QString fichier = QDir(dossier).filePath(QStringLiteral("maison.lbr")), equipe = QDir(dossier).filePath(QStringLiteral("equipe.json"));
    int articles = -1, lignes = 0, relus = 0;
    if (a && ecrire(fichier, lbr)) {
      DialogueImportBibliotheques* dlg = a->importBibliotheques();
      articles = dlg->importer({fichier}, equipe, false);
      lignes = dlg->table()->rowCount();
      try {
        relus = static_cast<int>(lireContenuBibliotheque(prom::lireFichier(prom::cheminFichier(equipe.toStdString()))).articles.size());
      } catch (const std::exception&) {
      }
      dlg->enregistrerRapport(QDir(dossier).filePath(QStringLiteral("rapport.csv")));
      dlg->hide();
    }
    verifier(articles == 50 && relus == 50 && lignes == 101 && QFileInfo(QDir(dossier).filePath(QStringLiteral("rapport.csv"))).size() > 1000,
             "10.18 : bibliothèque EAGLE de 50 composants convertie en bibliothèque d'équipe : 50 articles, une ligne de rapport par objet",
             std::to_string(articles) + " article(s), " + std::to_string(lignes) + " ligne(s)");
  }

  // ---------- 10.6 · Composant LCSC rangé avec son code et son modèle 3D ; nomenclature en entrée ----------
  {
    AtelierElectronique* a = f.atelierElectronique();
    std::string id;
    if (a) {
      a->ouvrirChoixComposant();
      attendre(300);
      if (FenetreChoixComposant* ch = a->choixComposant()) {
        id = ch->importerArchive(d + QStringLiteral("/easyeda/C20526.json"));
        ch->hide();
      }
    }
    const auto perso = AtelierElectronique::articlesPerso();
    const auto it = std::find_if(perso.begin(), perso.end(), [](const Article& x) { return x.id == "lcsc:C20526"; });
    const bool modele = it != perso.end() && !it->modele3D.empty() && QFile::exists(QFileInfo(AtelierElectronique::cheminArticlesPerso()).dir().filePath(QString::fromStdString(it->modele3D)));
    verifier(id == "lcsc:C20526" && it != perso.end() && it->lcsc == "C20526" && modele, "10.6 : composant LCSC importé d'EasyEDA avec son code, son empreinte et son modèle 3D", id);

    // Nomenclature : lue, appliquée en une commande, retirée d'un Ctrl+Z
    ConceptionElec c0 = nouvelleConception("Nomenclature");
    for (const char* ref : {"R1", "R2", "R3", "C1"}) {
      ComposantElec x;
      x.uuid = nouvelUuid();
      x.reference = ref;
      x.valeur = "?";
      c0.composants.push_back(x);
    }
    a = ouvrir(f, c0);
    const QString csv = QDir(dossier).filePath(QStringLiteral("bom.csv"));
    int changes = 0;
    bool lu = false, annule = false;
    if (a && ecrire(csv, "Designator;Value;MPN;LCSC Part\nR1-R3;10k;RC0603FR-0710KL;C25804\nC1;100n;;C14663\n")) {
      DialogueNomenclatureEntree* dlg = a->nomenclatureEntree();
      lu = dlg->lire(csv) && dlg->table()->rowCount() == 11;
      changes = dlg->appliquer();
      dlg->hide();
      attendre(200);
      const ConceptionElec& c = doc->projet().electronique[0];
      const bool ecrit = c.parReference("R2") && c.parReference("R2")->lcsc == "C25804" && c.parReference("C1")->valeur == "100n";
      doc->annuler();
      attendre(200);
      annule = ecrit && doc->projet().electronique[0].parReference("R2")->valeur == "?";
    }
    verifier(lu && changes == 11 && annule, "10.6 : nomenclature CSV en entrée : la liste des changements d'abord, une commande, un Ctrl+Z", std::to_string(changes) + " changement(s)");
  }
  // ---------- 10.10 · Carte équipée exportée en GLB depuis le ruban, relue par le lecteur de contrôle ----------
  {
    AtelierElectronique* a = ouvrir(f, exempleElectronique());
    const QString glb = QDir(dossier).filePath(QStringLiteral("carte.glb"));
    prom::ResumeGlb g;
    if (a) {
      repondreDans({saisie({}), fichier(glb)});
      a->executerAction("carte.exporter3D");
      attendre(1500);
      try {
        g = prom::lireGlb(prom::lireFichier(prom::cheminFichier(glb.toStdString())));
      } catch (const std::exception& e) {
        g.erreur = e.what();
      }
    }
    verifier(g.ok && g.noeuds > 3 && g.materiaux >= 3, "10.10 : carte équipée exportée en GLB depuis le ruban, fichier valide pour un visualiseur tiers",
             g.ok ? std::to_string(g.noeuds) + " nœud(s), " + std::to_string(g.triangles) + " triangle(s)" : g.erreur);
  }

  // ---------- 10.24 · Proposition IDX de la mécanique acceptée, carte mise à jour, un Ctrl+Z ----------
  {
    AtelierElectronique* a = f.atelierElectronique();
    bool ok = false;
    std::string detail;
    if (a && !doc->projet().electronique.empty()) {
      const ConceptionElec& c = doc->projet().electronique[0];
      const ComposantElec* j = nullptr;
      for (const auto& em : c.circuit.empreintes)
        if (const ComposantElec* x = c.composant(em.composant); x && (!j || x->reference.rfind("J", 0) == 0)) j = x;
      std::string prop = idxLigneDeBase(c);
      const std::string ref = j ? j->reference : std::string();
      const auto nom = prop.find("<foundation:ObjectName>" + ref + "</foundation:ObjectName></pdm:InstanceName>");
      if (j && nom != std::string::npos) {
        const auto tx = prop.find("<pdm:tx><property:Value>", nom) + 24;
        const auto fin = prop.find('<', tx);
        prop.replace(tx, fin - tx, std::to_string(std::stod(prop.substr(tx, fin - tx)) + 2));
        prop.replace(prop.find("EDMDProcessInstructionSendInformation"), 37, "EDMDProcessInstructionSendChanges");
        const QString fichierIdx = QDir(dossier).filePath(QStringLiteral("proposition.idx"));
        Pt avant;
        for (const auto& em : c.circuit.empreintes)
          if (em.composant == j->uuid) avant = em.position;
        const std::string uuid = j->uuid;
        if (ecrire(fichierIdx, prop) && a->recevoirIdx(fichierIdx, false)) {
          attendre(300);
          const ConceptionElec& c2 = doc->projet().electronique[0];
          Pt apres;
          for (const auto& em : c2.circuit.empreintes)
            if (em.composant == uuid) apres = em.position;
          const bool deplace = std::abs(mm(apres.x - avant.x) - 2) < 1e-3;
          const bool inscrit = historiqueIdx(c2).size() == 1;
          const bool reponse = QFileInfo::exists(QDir(dossier).filePath(QStringLiteral("proposition-reponse.idx")));
          doc->annuler();
          attendre(200);
          Pt annule;
          for (const auto& em : doc->projet().electronique[0].circuit.empreintes)
            if (em.composant == uuid) annule = em.position;
          ok = deplace && inscrit && reponse && annule == avant;
          detail = ref + (deplace ? " déplacé de 2 mm" : " non déplacé") + (inscrit ? ", échange inscrit" : "") + (reponse ? ", réponse écrite" : "");
        }
      }
    }
    verifier(ok, "10.24 : proposition IDX de la mécanique (connecteur déplacé de 2 mm) acceptée, carte mise à jour, échange inscrit, un Ctrl+Z la retire", detail);
  }
  // ---------- 10.20 · Netlist d'un schéma LTspice : carte mise à jour, rapport CSV en colonnes justes ----------
  {
    const std::string v1 = "* filtre.asc\nV1 IN 0 SINE(0 1 1k)\nR1 IN OUT 10k\nC1 OUT 0 100n\n.tran 5m\n.end\n";
    const std::string v2 = "* filtre.asc\nV1 IN 0 SINE(0 1 1k)\nR1 IN OUT 22k\nC1 OUT 0 100n\nC2 OUT 0 4.7µ\n.tran 5m\n.end\n";
    AtelierElectronique* a = ouvrir(f, conceptionDepuisNetlist(lireNetlist(v1), "Filtre LTspice"));
    const QString net = QDir(dossier).filePath(QStringLiteral("filtre.net")), csv = QDir(dossier).filePath(QStringLiteral("filtre-changements.csv"));
    bool ok = false;
    std::string detail;
    if (a && ecrire(net, v2)) {
      a->importerNetlist(net, csv);
      attendre(300);
      const ConceptionElec& c = doc->projet().electronique[0];
      std::vector<std::vector<std::string>> t;
      try {
        t = lireTableau(prom::lireFichier(prom::cheminFichier(csv.toStdString())));
      } catch (const std::exception&) {
      }
      const bool colonnes = !t.empty() && std::all_of(t.begin(), t.end(), [](const auto& l) { return l.size() == 5; });
      bool decimale = false;
      for (const auto& l : t) decimale = decimale || (l.size() == 5 && l[0] == "C2" && l[4] == "0,0000047");
      ok = c.parReference("R1") && c.parReference("R1")->valeur == "22k" && c.parReference("C2") && colonnes && decimale;
      detail = std::to_string(t.size()) + " ligne(s) au rapport";
    }
    verifier(ok, "10.20 : netlist d'un schéma LTspice appliquée à la carte, rapport des changements en colonnes et décimales justes", detail);
  }
  // ---------- 10.21 · Logo SVG de 20 mm posé en sérigraphie : 20,00 mm dans le Gerber relu ----------
  {
    AtelierElectronique* a = ouvrir(f, exempleElectronique());
    const QString logo = QDir(dossier).filePath(QStringLiteral("logo.svg"));
    double largeur = 0;
    int regions = 0;
    if (a && ecrire(logo, R"(<svg xmlns="http://www.w3.org/2000/svg" width="40mm" height="20mm" viewBox="0 0 40 20">
  <path d="M 10 2 A 8 8 0 1 1 9.99 2 Z M 10 6 A 4 4 0 1 0 10.01 6 Z" fill="#000"/><rect x="22" y="4" width="16" height="12" fill="#000"/></svg>)")) {
      OptionsDessinCarte o;
      o.couche = Couche::SerigraphieDessus;
      o.largeurVoulue = 20;
      o.centre = ptMm(30, 20);
      a->importerDessinCarte(logo, false, &o);
      attendre(200);
      const ConceptionElec& c = doc->projet().electronique[0];
      const Connectivite k = connectivite(c);
      const std::string g = gerber(modeleFabrication(c, k, remplirZones(c, k)), Couche::SerigraphieDessus, "0.34");
      // Lecture indépendante des régions (G36…G37), format 4.6 en millimètres
      double x0 = 1e300, x1 = -1e300;
      bool region = false;
      std::size_t i = 0;
      while (i < g.size()) {
        const auto fin = g.find('*', i);
        if (fin == std::string::npos) break;
        std::string cmd = g.substr(i, fin - i);
        i = fin + 1;
        while (!cmd.empty() && (cmd.front() == '\n' || cmd.front() == '\r' || cmd.front() == '%')) cmd.erase(cmd.begin());
        if (cmd == "G36") region = true, ++regions;
        else if (cmd == "G37") region = false;
        else if (region && !cmd.empty() && cmd[0] == 'X') {
          const double x = std::stoll(cmd.substr(1)) / 1e6;
          x0 = std::min(x0, x), x1 = std::max(x1, x);
        }
      }
      largeur = x1 - x0;
      if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DEBUG")) std::printf("(dessins : %zu ; gerber : %zu octets, G36 à %zu)\n", c.circuit.dessins.size(), g.size(), g.find("G36"));
    }
    verifier(regions == 2 && std::abs(largeur - 20) < 0.005, "10.21 : logo SVG de 20 mm posé en sérigraphie : 20,00 mm dans le Gerber relu", std::to_string(largeur) + " mm, " + std::to_string(regions) + " région(s)");
  }

  // ---------- 10.19 · Dossier de projets converti en série, sur son fil, sans s'arrêter au premier échec ----------
  {
    const QString source = QDir(dossier).filePath(QStringLiteral("serie")), sortie = QDir(dossier).filePath(QStringLiteral("serie-convertis"));
    auto copier = [&](const QString& de, const QString& vers) {
      QDir().mkpath(QFileInfo(QDir(source).filePath(vers)).path());
      QFile::copy(d + QLatin1Char('/') + de, QDir(source).filePath(vers));
    };
    copier(QStringLiteral("eagle/ampli.sch"), QStringLiteral("eagle/ampli.sch"));
    copier(QStringLiteral("eagle/ampli.brd"), QStringLiteral("eagle/ampli.brd"));
    copier(QStringLiteral("kicad5/filtre.sch"), QStringLiteral("kicad5/filtre.sch"));
    copier(QStringLiteral("kicad5/filtre-cache.lib"), QStringLiteral("kicad5/filtre-cache.lib"));
    copier(QStringLiteral("kicad5/etage.sch"), QStringLiteral("kicad5/etage.sch"));
    copier(QStringLiteral("geda/goodfet50.sch"), QStringLiteral("geda/goodfet50.sch"));
    copier(QStringLiteral("geda/goodfet50.pcb"), QStringLiteral("geda/goodfet50.pcb"));
    copier(QStringLiteral("sprint/12F629_SM.lay6"), QStringLiteral("sprint/12F629_SM.lay6"));
    {
      QFile casse(QDir(source).filePath(QStringLiteral("casse/casse.kicad_pcb")));
      QDir().mkpath(QFileInfo(casse).path());
      if (casse.open(QIODevice::WriteOnly)) casse.write("ceci n'est pas une carte");
    }
    AtelierElectronique* a = f.atelierElectronique();
    if (!a) {
      f.montrerElectronique(0, AtelierElectronique::Circuit);
      attendre(300);
      a = f.atelierElectronique();
    }
    bool ok = false;
    std::string detail;
    if (a) {
      DialogueConversionSerie* dlg = a->conversionSerie();
      const int n = dlg->choisirDossier(source);
      dlg->reglerDestination(sortie);
      const QString equipe = QDir(dossier).filePath(QStringLiteral("equipe-serie.json"));
      dlg->reglerBibliotheque(equipe);
      const bool lance = dlg->lancer(true);
      const auto& b = dlg->bilan();
      const int convertis = static_cast<int>(QDir(sortie).entryList({QStringLiteral("*.promethee")}, QDir::Files).size());
      const int rapports = static_cast<int>(QDir(sortie).entryList({QStringLiteral("*.rapport.md")}, QDir::Files).size());
      std::string erreur;
      ContenuBibliotheque relue;
      try {
        relue = lireContenuBibliotheque(prom::lireFichier(prom::cheminFichier(equipe.toStdString())), &erreur);
      } catch (const std::exception& e) {
        erreur = e.what();
      }
      bool tableJuste = dlg->table()->rowCount() == n;
      for (int i = 0; i < dlg->table()->rowCount(); ++i) tableJuste = tableJuste && dlg->table()->item(i, 3)->text() != QStringLiteral("à convertir");
      // Le guide de l'ancien outil s'ouvre sur le logiciel le plus fréquent de la série
      dlg->montrerGuide();
      attendre(200);
      const auto* guide = dlg->findChild<QDialog*>(QStringLiteral("guideAncienOutil"));
      const auto* page = guide ? guide->findChild<QTextBrowser*>(QStringLiteral("pageGuide")) : nullptr;
      const bool guideOk = page && page->toPlainText().contains(QStringLiteral("Prométhée pour les habitués"));
      if (guide) const_cast<QDialog*>(guide)->close();
      ok = lance && n == 5 && static_cast<int>(b.projets.size()) == 5 && b.reussis == 4 && b.echecs == 1 && convertis == 4 && rapports == 5 && QFileInfo::exists(QDir(sortie).filePath(QStringLiteral("bilan-conversion.md"))) &&
           erreur.empty() && relue.articles.size() == static_cast<std::size_t>(b.articlesCrees) && b.articlesCrees >= 1 && tableJuste && guideOk;
      detail = std::to_string(n) + " projet(s), " + std::to_string(b.reussis) + " converti(s), " + std::to_string(b.echecs) + " échec(s), " + std::to_string(convertis) + " fichier(s) .promethee, " +
               std::to_string(rapports) + " rapport(s), " + std::to_string(b.articlesCrees) + " article(s) d'équipe (" + std::to_string(relue.articles.size()) + " relu(s))" + (guideOk ? "" : ", guide absent") +
               (tableJuste ? "" : ", tableau incomplet") + (erreur.empty() ? "" : ", " + erreur);
      dlg->close();
    }
    verifier(ok, "10.19 : dossier de projets EAGLE, KiCad 5, gEDA et Sprint-Layout converti en série : un rapport par projet, l'échec n'arrête pas la série, articles rangés dans la bibliothèque d'équipe, guide ouvert", detail);
  }
}

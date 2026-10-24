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
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "EssaisReponses034.hpp"
#include "Document.hpp"
#include "FenetreChoixComposant.hpp"
#include "FenetrePrincipale.hpp"
#include "ImportsTiers034.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/exports3d.hpp"
#include "promethee/idx.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"

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
}

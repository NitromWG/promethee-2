// Prométhée : essais du lot L19 de la 0.34 (santé de la bibliothèque, liste d'achat, faisceaux, PLM).
// Licence GPL-3.0-only.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/articles.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/liste_achat.hpp"
#include "promethee/modele.hpp"
#include "promethee/sante_bibliotheque.hpp"

using namespace prom;
using namespace prom::ecad;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {
fs::path dossierEssai(const std::string& nom) {
  const fs::path d = fs::temp_directory_path() / ("promethee-l19-" + nom);
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
ComposantElec* parRef(ConceptionElec& c, const std::string& r) {
  for (auto& k : c.composants)
    if (k.reference == r) return &k;
  return nullptr;
}
// Bibliothèque de 197 résistances de valeurs toutes différentes, puis trois doublons semés : même article, référence
// fabricant écrite sans son tiret
std::vector<Article> bibliothequeSemee() {
  std::vector<Article> b;
  for (int i = 0; i < 197; ++i) {
    Article a;
    a.id = "r-" + std::to_string(i);
    a.nom = "Résistance " + std::to_string(1000 + 7 * i) + " Ω 0603";
    a.symbole = "R";
    a.empreinte = "R_0603";
    a.valeur = std::to_string(1000 + 7 * i);
    a.fabricant = "Yageo";
    a.mpn = "RC0603FR-07" + std::to_string(1000 + 7 * i) + "RL";
    b.push_back(a);
  }
  for (const int k : {3, 50, 120}) {
    Article d = b[static_cast<std::size_t>(k)];
    d.id = "doublon-" + std::to_string(k);
    d.mpn.erase(d.mpn.find('-'), 1);
    b.push_back(d);
  }
  return b;
}
}  // namespace

TEST_CASE("19.4 · Santé : trois doublons semés repérés malgré le tiret, fusion qui repointe deux projets", "[L19][sante]") {
  const std::vector<Article> bib = bibliothequeSemee();
  REQUIRE(bib.size() == 200);
  CHECK(referenceNormalisee("rc0603fr-07 10k.l") == "RC0603FR0710KL");
  int appels = 0, dernier = 0;
  const SanteBibliotheque s = santeBibliotheque(bib, nullptr, [&](int fait, int total) {
    ++appels;
    dernier = fait;
    CHECK(total == 200);
  });
  CHECK(s.articles == 200);
  CHECK_FALSE(s.interrompu);
  CHECK(appels > 1);
  CHECK(dernier == 200);
  REQUIRE(s.doublons.size() == 3);
  std::vector<std::string> trouves;
  for (const auto& d : s.doublons) {
    REQUIRE(d.articles.size() == 2);
    CHECK(d.raison.find("référence fabricant") != std::string::npos);
    trouves.push_back(d.articles[0] < d.articles[1] ? d.articles[1] : d.articles[0]);
  }
  std::sort(trouves.begin(), trouves.end());
  CHECK(trouves == std::vector<std::string>{"r-120", "r-3", "r-50"});  // chaque paire : le doublon et l'original
  // Modèles 3D manquants relevés, pas de modèle de simulation exigé pour une résistance
  CHECK(std::count_if(s.problemes.begin(), s.problemes.end(), [](const ProblemeArticle& p) { return p.genre == "modèle 3D"; }) == 200);
  CHECK(std::none_of(s.problemes.begin(), s.problemes.end(), [](const ProblemeArticle& p) { return p.genre == "modèle de simulation"; }));
  // Interrompu tout de suite
  std::atomic<bool> arret{true};
  CHECK(santeBibliotheque(bib, &arret).interrompu);
  // Doublon sans référence fabricant : mêmes paramètres et mêmes pastilles
  std::vector<Article> sansRef = {bib[0], bib[0]};
  sansRef[0].mpn.clear();
  sansRef[1].mpn.clear();
  sansRef[1].id = "copie";
  sansRef[1].valeur = "1k";  // 1000 Ω écrit autrement
  const SanteBibliotheque s2 = santeBibliotheque(sansRef);
  REQUIRE(s2.doublons.size() == 1);
  CHECK(s2.doublons[0].raison == "mêmes paramètres et mêmes pastilles");
  // Comparaison côte à côte
  const auto lignes = comparerArticles(bib[3], bib[197]);
  const auto ligne = [&](const std::string& champ) { return *std::find_if(lignes.begin(), lignes.end(), [&](const LigneComparaison& l) { return l.champ == champ; }); };
  CHECK(ligne("Référence fabricant").differe);
  CHECK_FALSE(ligne("Référence normalisée").differe);
  CHECK_FALSE(ligne("Pastilles").differe);

  // Fusion : bibliothèque d'équipe en fichier, deux projets citent le doublon, un troisième non
  const fs::path d = dossierEssai("fusion");
  ContenuBibliotheque contenu;
  contenu.nom = "Équipe";
  contenu.articles = bib;
  const std::string fichier = utf8(d / "equipe.prombib");
  ecrireFichierAtomique(d / "equipe.prombib", texteContenuBibliotheque(contenu));
  const ContenuBibliotheque relu = lireFichierBibliotheque(fichier);
  CHECK(relu.articles.size() == 200);
  CHECK(santeBibliotheque(relu).doublons.size() == 3);
  std::vector<std::string> projets;
  for (int i = 0; i < 3; ++i) {
    Projet p = projetVide();
    p.nom = "Carte " + std::to_string(i);
    p.electronique.push_back(exempleElectronique());
    ConceptionElec& c = p.electronique[0];
    if (i < 2) {
      ComposantElec* r1 = parRef(c, "R1");
      REQUIRE(r1);
      if (i == 0) {
        r1->article = "doublon-3";  // cité par son article
        c.articles["doublon-3"] = bib[197];
      } else {
        const std::string sym = installerArticle(c, bib[197]);  // posé depuis l'article : symbole « art:doublon-3 »
        REQUIRE(sym == "art:doublon-3");
        for (auto& f : c.feuilles)
          for (auto& sp : f.symboles)
            if (sp.composant == r1->uuid) sp.symbole = sym;
        r1->symbole = sym;
        remplirDepuisArticle(c, *r1);
        CHECK(r1->article == "doublon-3");
      }
    }
    projets.push_back(utf8(d / ("carte" + std::to_string(i) + ".prom")));
    ecrireProjet(p, projets.back());
  }
  const std::string avantTroisieme = lireFichier(cheminFichier(projets[2]));
  // Supprimer le doublon est refusé tant qu'un projet le cite
  const OperationBibliotheque refus = supprimerArticle(fichier, "doublon-3", projets, "Evan");
  CHECK_FALSE(refus.fait);
  CHECK(refus.erreur.find("cite encore") != std::string::npos);
  const OperationBibliotheque f = fusionnerArticles(fichier, "r-3", "doublon-3", projets, "Evan");
  REQUIRE(f.fait);
  CHECK(f.projets.size() == 2);
  CHECK(f.composants == 2);
  for (int i = 0; i < 2; ++i) {
    Projet p = lireProjet(projets[static_cast<std::size_t>(i)]);
    ComposantElec* r1 = parRef(p.electronique[0], "R1");
    REQUIRE(r1);
    CHECK(r1->article == "r-3");
    CHECK(r1->mpn == bib[3].mpn);
    CHECK_FALSE(p.electronique[0].articles.count("doublon-3"));
    if (i == 1) {
      CHECK(r1->symbole == "art:r-3");
      for (const auto& fe : p.electronique[0].feuilles)
        for (const auto& sp : fe.symboles)
          if (sp.composant == r1->uuid) CHECK(sp.symbole == "art:r-3");
    }
  }
  CHECK(lireFichier(cheminFichier(projets[2])) == avantTroisieme);  // le projet qui ne citait pas le doublon : intact
  const ContenuBibliotheque apres = lireFichierBibliotheque(fichier);
  CHECK(apres.articles.size() == 199);
  CHECK(santeBibliotheque(apres).doublons.size() == 2);
  const std::string journal = lireFichier(d / "journal-bibliotheque.csv");
  CHECK(journal.find(";Evan;fusion;r-3;doublon-3 rejoint r-3;2;2") != std::string::npos);
  // Annulée : bibliothèque et projets reviennent, le journal garde la trace
  REQUIRE(annulerOperation(f));
  CHECK(lireFichierBibliotheque(fichier).articles.size() == 200);
  CHECK(parRef(lireProjet(projets[0]).electronique[0], "R1")->article == "doublon-3");
  CHECK(lireFichier(d / "journal-bibliotheque.csv").find(";annulation;") != std::string::npos);
  // Refaite
  const OperationBibliotheque f2 = refaireOperation(f);
  REQUIRE(f2.fait);
  CHECK(f2.composants == 2);
  CHECK(parRef(lireProjet(projets[0]).electronique[0], "R1")->article == "r-3");
  // Correction d'un champ, annulable ; la version de l'article monte
  const OperationBibliotheque k = corrigerArticle(fichier, "r-50", "modele3D", "modeles/R_0603.step", "Evan");
  REQUIRE(k.fait);
  const auto corrige = lireFichierBibliotheque(fichier);
  const auto it = std::find_if(corrige.articles.begin(), corrige.articles.end(), [](const Article& a) { return a.id == "r-50"; });
  REQUIRE(it != corrige.articles.end());
  CHECK(it->modele3D == "modeles/R_0603.step");
  CHECK(it->version == 2);
  CHECK(corrigerArticle(fichier, "r-50", "parametre:tolerance", "1 %").fait);
  CHECK_FALSE(corrigerArticle(fichier, "r-50", "inconnu", "x").fait);
  REQUIRE(annulerOperation(k));
  // Suppression du doublon libre (aucun projet ne le cite) : annulable aussi
  const OperationBibliotheque sup = supprimerArticle(fichier, "doublon-50", projets, "Evan");
  REQUIRE(sup.fait);
  CHECK(lireFichierBibliotheque(fichier).articles.size() == 198);
  REQUIRE(annulerOperation(sup));
  CHECK(lireFichierBibliotheque(fichier).articles.size() == 199);
  // Le contenu de la bibliothèque d'équipe autre que ses articles reste tel quel
  CHECK(lireFichierBibliotheque(fichier).nom == "Équipe");
}

TEST_CASE("19.4 · Demandes d'articles : dépôt, champs imposés, suivi, notification au demandeur", "[L19][demandes]") {
  const fs::path d = dossierEssai("demandes");
  const std::string equipe = utf8(d);
  REQUIRE(imposerChamps(equipe, {"boitier"}));
  CHECK(champsImposes(equipe) == std::vector<std::string>{"boitier"});
  DemandeArticle a;
  a.demandeur = "Evan";
  a.reference = "TPS62130RGTR";
  a.fabricant = "Texas Instruments";
  std::string erreur;
  CHECK_FALSE(deposerDemande(equipe, a, &erreur));  // champ imposé manquant
  CHECK(erreur.find("boitier") != std::string::npos);
  a.champs["boitier"] = "VQFN-16";
  ecrireFichierAtomique(d / "fiche.pdf", "%PDF-1.4 fiche");
  a.fiche = utf8(d / "fiche.pdf");
  REQUIRE(deposerDemande(equipe, a, &erreur));
  CHECK_FALSE(a.uuid.empty());
  CHECK(a.fiche.rfind("fiches/", 0) == 0);
  CHECK(fs::exists(d / "demandes" / cheminFichier(a.fiche)));
  DemandeArticle b;
  b.demandeur = "Lucie";
  b.reference = "STM32G031K8T6";
  b.champs["boitier"] = "LQFP-32";
  REQUIRE(deposerDemande(equipe, b));
  auto liste = demandesDeposees(equipe);
  REQUIRE(liste.size() == 2);
  CHECK(liste[0].etat == EtatDemande::Deposee);
  // Le bibliothécaire la prend, la publie ; la demande de Lucie est refusée
  REQUIRE(suivreDemande(equipe, a.uuid, EtatDemande::Prise, "Marc"));
  REQUIRE(suivreDemande(equipe, a.uuid, EtatDemande::Publiee, "Marc", "ti-tps62130"));
  REQUIRE(suivreDemande(equipe, b.uuid, EtatDemande::Refusee, "Marc", {}, "déjà au catalogue sous STM32G031K8T6TR"));
  liste = demandesDeposees(equipe);
  const auto pa = std::find_if(liste.begin(), liste.end(), [&](const DemandeArticle& x) { return x.uuid == a.uuid; });
  const auto pb = std::find_if(liste.begin(), liste.end(), [&](const DemandeArticle& x) { return x.uuid == b.uuid; });
  REQUIRE(pa != liste.end());
  REQUIRE(pb != liste.end());
  CHECK(pa->etat == EtatDemande::Publiee);
  CHECK(pa->bibliothecaire == "Marc");
  CHECK(pb->etat == EtatDemande::Refusee);
  CHECK(pb->raison.find("STM32G031K8T6TR") != std::string::npos);
  // Notification : seulement quand l'article publié apparaît dans la bibliothèque
  std::vector<Article> bib;
  CHECK(demandesServies(equipe, "Evan", bib).empty());
  Article publie;
  publie.id = "ti-tps62130";
  publie.mpn = "TPS62130RGTR";
  bib.push_back(publie);
  const auto servies = demandesServies(equipe, "Evan", bib);
  REQUIRE(servies.size() == 1);
  CHECK(servies[0].uuid == a.uuid);
  CHECK(demandesServies(equipe, "Lucie", bib).empty());
}

TEST_CASE("19.7 · Liste d'achat : la bobine de 5 000 l'emporte sur 1 020 pièces en bande coupée", "[L19][achat]") {
  LigneAchat l;
  l.cle = "RC0603FR-0710KL";
  l.parCarte = 4;
  l.pertes = 0.02;
  SourceAchat s;
  s.fournisseur = "Mouser";
  s.reference = "603-RC0603FR-0710KL";
  Conditionnement bobine;
  bobine.genre = "bobine";
  bobine.pieces = 5000;
  bobine.prix = {{1, 0.002}};
  Conditionnement bande;
  bande.genre = "bande coupée";
  bande.prix = {{1, 0.01}};
  s.conditionnements = {bande, bobine};
  l.sources = {s};
  CHECK(besoinLigne(l, 250) == 1020);
  const auto cb = coutConditionnement(bande, 1020);
  const auto cr = coutConditionnement(bobine, 1020);
  REQUIRE(cb);
  REQUIRE(cr);
  CHECK(cb->quantite == 1020);
  CHECK(cb->cout == Approx(10.20));
  CHECK(cr->quantite == 5000);
  CHECK(cr->lots == 1);
  CHECK(cr->cout == Approx(10.0));
  OptionsAchat o;
  o.cartes = 250;
  const ListeAchat liste = calculerListeAchat({l}, o);
  REQUIRE(liste.lignes.size() == 1);
  CHECK(liste.lignes[0].conditionnement == "bobine");
  CHECK(liste.lignes[0].quantite == 5000);
  CHECK(liste.lignes[0].besoin == 1020);
  CHECK(liste.lignes[0].cout == Approx(10.0));
  CHECK(liste.total == Approx(10.0));
  CHECK(csvListeAchat(liste).find("RC0603FR-0710KL;Mouser;bobine;1020;5000;1;") != std::string::npos);
  // Minimum, multiple, conditionnements entiers, paliers de prix
  Conditionnement k;
  k.minimum = 100;
  k.multiple = 50;
  k.prix = {{1, 0.05}, {150, 0.03}};
  const auto ck = coutConditionnement(k, 120);
  REQUIRE(ck);
  CHECK(ck->quantite == 150);
  CHECK(ck->prixUnitaire == Approx(0.03));
  CHECK(coutConditionnement(bobine, 6000)->quantite == 10000);
  CHECK(coutConditionnement(bobine, 6000)->lots == 2);
  // Stock insuffisant : la bande coupée sans stock suffisant n'est pas retenue
  SourceAchat petite = s;
  petite.fournisseur = "Farnell";
  petite.stock = 500;
  petite.conditionnements = {bande};
  LigneAchat l2 = l;
  l2.sources = {petite, s};
  l2.sources[1].conditionnements = {bande};
  l2.sources[1].stock = 100000;
  const ListeAchat avecStock = calculerListeAchat({l2}, o);
  REQUIRE(avecStock.lignes.size() == 1);
  CHECK(avecStock.lignes[0].fournisseur == "Mouser");
}

TEST_CASE("19.7 · Port, franco et douane : regrouper chez un fournisseur bat la ligne par ligne", "[L19][achat]") {
  auto ligne = [](const std::string& cle, double fr, double us) {
    LigneAchat l;
    l.cle = cle;
    SourceAchat a;
    a.fournisseur = "Distrib-FR";
    a.pays = "FR";
    a.conditionnements = {Conditionnement{"bande coupée", 1, 1, 1, {{1, fr}}}};
    SourceAchat b;
    b.fournisseur = "Distrib-US";
    b.pays = "US";
    b.conditionnements = {Conditionnement{"bande coupée", 1, 1, 1, {{1, us}}}};
    l.sources = {a, b};
    return l;
  };
  const std::string donnees = R"({"livraison":"FR","pays":{"FR":{"port":6,"franco":50},"US":{"port":20,"douane":0.04}}})";
  const auto d = lireFraisAchat(donnees);
  REQUIRE(d);
  CHECK(d->pays.at("US").douane == Approx(0.04));
  OptionsAchat o;
  o.cartes = 1;
  o.donnees = *d;
  const std::vector<LigneAchat> lignes = {ligne("A", 10, 9), ligne("B", 5, 6)};
  const ListeAchat exacte = calculerListeAchat(lignes, o);
  CHECK(exacte.methode == "exacte (ensembles de fournisseurs)");
  CHECK(exacte.fournisseurs == std::vector<std::string>{"Distrib-FR"});
  CHECK(exacte.total == Approx(21.0));  // 15 € + 6 € de port
  o.fournisseursMax = 1;  // trop de fournisseurs : ligne par ligne, le port compté après
  const ListeAchat simple = calculerListeAchat(lignes, o);
  CHECK(simple.methode == "ligne par ligne");
  CHECK(simple.total == Approx(9 + 5 + 6 + 20 + 9 * 0.04));
  // Franco atteint : port offert
  o.fournisseursMax = 12;
  const ListeAchat gros = calculerListeAchat({ligne("A", 60, 70)}, o);
  CHECK(gros.port == Approx(0.0));
  CHECK(gros.total == Approx(60.0));
  // Panier au gabarit du distributeur
  LigneAchat lm = ligne("C", 1, 2);
  lm.sources[0].fournisseur = "Mouser";
  lm.sources[0].reference = "603-C";
  const ListeAchat lp = calculerListeAchat({lm}, o);
  const std::string panier = panierDistributeur(lp, gabaritsPanier()[0]);
  CHECK(panier.rfind("Mouser Part Number,Manufacturer Part Number,Quantity,Customer Part Number\n", 0) == 0);
  CHECK(panier.find("603-C,C,1,") != std::string::npos);
  const auto g = lireGabaritPanier(texteGabaritPanier(gabaritsPanier()[1]));
  REQUIRE(g);
  CHECK(g->distributeur == "Digi-Key");
  CHECK(g->colonnes == gabaritsPanier()[1].colonnes);
}

TEST_CASE("19.7 · Tableau de bord de la nomenclature : note sur 10, seules les lignes touchées recalculées", "[L19][tableau]") {
  ConceptionElec c = exempleElectronique();
  const TableauBordNomenclature t1 = tableauBordNomenclature(c);
  REQUIRE_FALSE(t1.lignes.empty());
  CHECK(t1.recalculees == static_cast<int>(t1.lignes.size()));
  CHECK(t1.note >= 0);
  CHECK(t1.note <= 10);
  const TableauBordNomenclature t0 = tableauBordNomenclature(c, &t1);
  CHECK(t0.recalculees == 0);
  CHECK(t0.note == Approx(t1.note));
  // Une ligne touchée : seule elle (et la ligne qu'elle quitte) se recalcule
  ComposantElec* d1 = parRef(c, "D1");
  REQUIRE(d1);
  d1->mpn = "LTST-C150GKT";
  d1->parametres["rohs"] = "oui";
  d1->parametres["reach"] = "oui";
  const TableauBordNomenclature t2 = tableauBordNomenclature(c, &t1);
  CHECK(t2.recalculees >= 1);
  CHECK(t2.recalculees <= 2);
  CHECK(t2.recalculees < static_cast<int>(t2.lignes.size()));
  const auto ld = std::find_if(t2.lignes.begin(), t2.lignes.end(), [](const NoteLigneNomenclature& l) { return l.cle == "LTST-C150GKT"; });
  REQUIRE(ld != t2.lignes.end());
  CHECK(std::none_of(ld->remarques.begin(), ld->remarques.end(), [](const std::string& r) { return r.find("RoHS") != std::string::npos; }));
  d1->parametres["rohs"] = "non";
  const TableauBordNomenclature t3 = tableauBordNomenclature(c, &t2);
  const auto ld3 = std::find_if(t3.lignes.begin(), t3.lignes.end(), [](const NoteLigneNomenclature& l) { return l.cle == "LTST-C150GKT"; });
  REQUIRE(ld3 != t3.lignes.end());
  CHECK(ld3->risque < ld->risque);
  CHECK(std::any_of(ld3->remarques.begin(), ld3->remarques.end(), [](const std::string& r) { return r == "RoHS non conforme"; }));
  const std::string page = htmlTableauBord(t3, "Carte d'essai");
  CHECK(page.find("<svg") != std::string::npos);
  CHECK(page.find(" / 10</p>") != std::string::npos);
  CHECK(page.find("<!doctype html>") == 0);
}

TEST_CASE("19.7 · Lignes d'achat de deux projets réunis, offres des articles en sources", "[L19][achat]") {
  ConceptionElec a = exempleElectronique(), b = exempleElectronique();
  Article art;
  art.id = "yageo-10k";
  art.mpn = "RC0603FR-0710KL";
  OffreArticle reel;
  reel.distributeur = "LCSC";
  reel.reference = "C25804";
  reel.conditionnement = 5000;
  reel.prix = {{1, 0.0015}};
  OffreArticle detail;
  detail.distributeur = "Mouser";
  detail.reference = "603-RC0603FR-0710KL";
  detail.prix = {{1, 0.01}};
  art.offres = {reel, detail};
  for (ConceptionElec* c : {&a, &b}) {
    c->articles[art.id] = art;
    ComposantElec* r1 = parRef(*c, "R1");
    REQUIRE(r1);
    r1->article = art.id;
    r1->mpn = art.mpn;
  }
  FraisAchat d;
  d.fournisseurs["LCSC"] = "CN";
  const auto lignes = lignesAchat({{&a, 100}, {&b, 150}}, 0.02, &d);
  const auto l = std::find_if(lignes.begin(), lignes.end(), [](const LigneAchat& x) { return x.cle == "RC0603FR-0710KL"; });
  REQUIRE(l != lignes.end());
  CHECK(besoinLigne(*l, 1) == 255);  // 250 cartes, une résistance, 2 %
  REQUIRE(l->sources.size() == 2);
  CHECK(l->sources[0].pays == "CN");
  CHECK(l->sources[0].conditionnements[0].genre == "bobine");
  OptionsAchat o;
  const ListeAchat liste = calculerListeAchat({*l}, o);
  REQUIRE(liste.lignes.size() == 1);
  CHECK(liste.lignes[0].fournisseur == "Mouser");  // 255 × 0,01 = 2,55 € contre 5 000 × 0,0015 = 7,50 €
  CHECK(liste.lignes[0].cout == Approx(2.55));
}

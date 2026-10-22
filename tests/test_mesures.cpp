// Prométhée : mesures de performance (matrice QA-03). Licence GPL-3.0-only.
// Cas cachés (balise [.]) : ils ne tournent qu'à la demande, en version optimisée :
//   promethee_tests "[mesures]"
// Chaque étape affiche son temps ; la mémoire de pointe du processus est lue à la fin (Linux).
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include <TopLoc_Location.hxx>

#include "promethee/assemblage.hpp"
#include "promethee/construction.hpp"
#include "promethee/electronique.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/piece.hpp"

using namespace prom;
namespace fs = std::filesystem;

namespace {
class Chrono {
 public:
  double secondes() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_debut).count(); }
  void noter(const char* etape) {
    std::printf("  %-58s %9.3f s\n", etape, secondes());
    std::fflush(stdout);
    m_debut = std::chrono::steady_clock::now();
  }

 private:
  std::chrono::steady_clock::time_point m_debut = std::chrono::steady_clock::now();
};

// Mémoire de pointe du processus (Mio), 0 si inconnue
double memoirePointe() {
  std::ifstream f("/proc/self/status");
  std::string ligne;
  while (std::getline(f, ligne))
    if (ligne.rfind("VmHWM:", 0) == 0) return std::stod(ligne.substr(6)) / 1024.0;
  return 0;
}

// Carte de n résistances en grille, reliées deux à deux, avec une zone de masse au dos
ecad::ConceptionElec grandeCarte(int n, int colonnes, double pas) {
  using namespace ecad;
  ConceptionElec c = nouvelleConception("Mesure " + std::to_string(n));
  std::vector<std::string> syms;
  for (int i = 0; i < n; ++i) syms.push_back(poserSymbole(c, 0, "R", ptMm(25.4 + 12.7 * (i % colonnes), 25.4 + 12.7 * (i / colonnes))));
  annoter(c, false);
  for (int i = 0; i + 1 < n; i += 2) {
    Pt a{}, b{};
    for (const auto& s : c.feuilles[0].symboles) {
      if (s.uuid == syms[static_cast<std::size_t>(i)])
        for (const auto& bp : brochesPlacees(s, *c.symbole(s.symbole))) if (bp.broche->numero == "2") a = bp.p;
      if (s.uuid == syms[static_cast<std::size_t>(i + 1)])
        for (const auto& bp : brochesPlacees(s, *c.symbole(s.symbole))) if (bp.broche->numero == "1") b = bp.p;
    }
    c.feuilles[0].fils.push_back({nouvelUuid(), a, {b.x, a.y}});
    c.feuilles[0].fils.push_back({nouvelUuid(), {b.x, a.y}, b});
  }
  mettreAJourCircuit(c);
  const int lignes = (n + colonnes - 1) / colonnes;
  c.circuit.contour.largeur = nm(16 + pas * colonnes);
  c.circuit.contour.hauteur = nm(16 + 10.0 * lignes);
  for (std::size_t i = 0; i < c.circuit.empreintes.size(); ++i)
    c.circuit.empreintes[i].position = ptMm(8 + pas * static_cast<double>(i % static_cast<std::size_t>(colonnes)), 8 + 10.0 * static_cast<double>(i / static_cast<std::size_t>(colonnes)));
  // Zone sur toute la carte, sur la face des composants, du réseau le plus long : remplie autour de toutes les
  // autres pastilles et pistes, contrôlée ensuite objet par objet (DRC-24)
  std::string reseauZone;
  {
    const Connectivite k = connectivite(c);
    std::size_t plus = 0;
    std::map<std::string, std::size_t> parReseau;
    for (const auto& pp : pastilles(c, k))
      if (!pp.reseau.empty()) ++parReseau[pp.reseau];
    for (const auto& [nom, nb] : parReseau)
      if (nb > plus) { plus = nb; reseauZone = nom; }
  }
  Zone z;
  z.uuid = nouvelUuid();
  z.reseau = reseauZone;
  z.couche = Couche::CuivreDessus;
  z.contour = {ptMm(0, 0), Pt{c.circuit.contour.largeur, 0}, Pt{c.circuit.contour.largeur, c.circuit.contour.hauteur}, Pt{0, c.circuit.contour.hauteur}};
  c.circuit.zones.push_back(z);
  return c;
}

void mesurerCarte(int n, int colonnes) {
  using namespace ecad;
  std::printf("\nCarte de %d composants (%d réseaux à router), zone de cuivre sur toute la carte\n", n, n / 2);
  Chrono t;
  ConceptionElec c = grandeCarte(n, colonnes, 12);
  t.noter("schéma, annotation, mise à jour du circuit");
  const BilanRoutage b = routerSimple(c);
  t.noter("routage automatique simple");
  const Connectivite k = connectivite(c);
  t.noter("connectivité");
  const Remplissages r = remplirZones(c, k);
  t.noter("remplissage des zones");
  const auto v = verifierDRC(c, k, r);
  t.noter("vérification DRC (contrôle du cuivre des zones compris)");
  const fs::path d = fs::temp_directory_path() / ("promethee_mesure_" + std::to_string(n));
  fs::remove_all(d);
  ecrireDossierFabrication(c, d.string(), "mesure");
  t.noter("dossier de fabrication (Gerber, perçages, CSV)");
  const auto defauts = controlerDossierFabrication(c, d.string());
  t.noter("relecture et contrôle du dossier");
  std::printf("  liaisons routées %d, vias %d, restantes %d ; %zu violation(s) ; contrôle : %zu défaut(s)\n", b.liaisons, b.vias, b.restantes, v.size(), defauts.size());
  CHECK(defauts.empty());
  fs::remove_all(d);
}

// Pièce : plaque 40 × 30 × 5 à quatre trous
Piece plaque(int& extrusion, std::vector<int>& lignes) {
  Piece p;
  p.densite = 2.7;
  p.densiteConfirmee = true;
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  lignes = e.esquisse.ajouterRectangle(0, 0, 40, 30);
  const int ie = p.ajouter(e);
  FonctionPiece x;
  x.type = TypeFonction::Extrusion;
  x.esquisseId = ie;
  x.longueur = 5;
  extrusion = p.ajouter(x);
  FonctionPiece pts;
  pts.type = TypeFonction::Esquisse;
  pts.face = std::to_string(extrusion) + ":sommet";
  for (const auto& [px, py] : {std::pair{5.0, 5.0}, {35.0, 5.0}, {5.0, 25.0}, {35.0, 25.0}}) pts.esquisse.ajouterPoint(px, py);
  const int ip = p.ajouter(pts);
  FonctionPiece tr;
  tr.type = TypeFonction::Percage;
  tr.esquisseId = ip;
  tr.typePercage = "simple";
  tr.diametre = 3.2;
  tr.traversant = true;
  p.ajouter(tr);
  return p;
}

void mesurerAssemblage(int n) {
  std::printf("\nAssemblage de %d plaques empilées, entièrement contraintes (%d contraintes)\n", n, 3 * (n - 1));
  Chrono t;
  int ix = 0;
  std::vector<int> lignes;
  const Piece p = plaque(ix, lignes);
  const std::vector<ResultatPiece> pieces = {construirePiece(p)};
  t.noter("construction de la pièce (esquisse, extrusion, perçages)");
  Assemblage a;
  for (int i = 0; i < n; ++i) a.composants.push_back({0, 0.3 * i, 0.2 * i, 7.0 * i, 0, 0, 0, i == 0});
  // Chaque plaque posée sur la précédente, flancs avant et gauche alignés : aucun degré de liberté
  const std::string dessus = std::to_string(ix) + ":sommet", dessous = std::to_string(ix) + ":base";
  const std::string avant = std::to_string(ix) + ":cote:" + std::to_string(lignes[0]), gauche = std::to_string(ix) + ":cote:" + std::to_string(lignes[3]);
  for (int i = 0; i + 1 < n; ++i)
    for (const auto& [ra, rb, aligne] : {std::tuple{dessus, dessous, false}, {avant, avant, true}, {gauche, gauche, true}}) {
      ContrainteAssemblage c;
      c.type = "coincidence";
      c.a = i;
      c.b = i + 1;
      c.refA = ra;
      c.refB = rb;
      c.inverse = aligne;
      a.contraintes.push_back(c);
    }
  const EtatAssemblage e = resoudreAssemblage(a, pieces);
  t.noter("résolution des contraintes");
  const AnalyseAssemblage an = analyserAssemblage(a, pieces);
  t.noter("analyse des degrés de liberté");
  std::vector<TopoDS_Shape> formes;
  for (const auto& c : a.composants) formes.push_back(pieces[0].forme.Moved(TopLoc_Location(placementComposant(c))));
  t.noter("placement des formes");
  int contacts = 0, interferences = 0;
  for (std::size_t i = 0; i + 1 < formes.size(); ++i) {
    const AnalyseContact k = analyserContact(formes[i], formes[i + 1]);
    contacts += k.etat == EtatContact::Contact ? 1 : 0;
    interferences += k.etat == EtatContact::Interference ? 1 : 0;
    if (k.etat != EtatContact::Contact)
      std::printf("    plaques %zu et %zu : %s, distance %.6f mm, volume commun %.6f mm³ %s ; z %.6f / %.6f\n", i, i + 1, libelleContact(k.etat).c_str(), k.distance, k.volumeCommun,
                  k.detail.c_str(), a.composants[i].z, a.composants[i + 1].z);
  }
  t.noter("contacts et interférences des voisins (analyse exacte)");
  const std::vector<PaireContact> toutes = contactsEntre(formes);
  t.noter("toutes les paires (boîtes d'abord, puis analyse exacte des voisines)");
  const BilanJeux jeux = plusPetitsJeux(formes, 30);
  t.noter("trente plus petits jeux");
  CHECK(static_cast<int>(toutes.size()) == n - 1);
  CHECK(jeux.jeux.size() == 30);
  CHECK(jeux.contacts == n - 1);  // les contacts sont comptés à part, pas listés comme jeux nuls (AS-29)
  const ProprietesAssemblage pr = proprietesAssemblage({a}, 0, {p}, pieces);
  t.noter("propriétés physiques de l'assemblage");
  std::printf("  résolu %s, %d degré(s) de liberté, %d contact(s), %d interférence(s), masse %.1f g\n", e.resolu ? "oui" : "non", an.degresLiberte, contacts, interferences, pr.masse);
  CHECK(e.resolu);
  CHECK(an.degresLiberte == 0);
  CHECK(contacts == n - 1);
  CHECK(interferences == 0);
}
}  // namespace

TEST_CASE("QA-03 : mesures de performance, cartes et assemblages", "[.][mesures][QA-03]") {
  Chrono total;
  mesurerCarte(120, 12);
  mesurerCarte(400, 20);
  mesurerAssemblage(50);
  mesurerAssemblage(200);
  std::printf("\nTemps total %.1f s ; mémoire de pointe %.0f Mio\n", total.secondes(), memoirePointe());
}

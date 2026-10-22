// Prométhée : corpus de cartes d'essai pour les lecteurs indépendants du dossier de fabrication (audit Q4, tâche 0.2
// du lot L0). Écrit le dossier de chaque carte dans <dossier>/<nom>/, le contrôle par le noyau, et y dépose
// « attendu.txt » : les faits de la conception que les lecteurs indépendants doivent retrouver dans les fichiers
// (couches de cuivre, composants placés, trous non métallisés, dimensions), sous forme d'arguments de
// tests/verifier_fabrication.py. Les fentes rejoindront le corpus avec le lot L4 qui les fabrique.
// Utilisation : corpus_fabrication <dossier> <données d'essai> ; code 0 si chaque dossier passe le contrôle du noyau.
// Licence GPL-3.0-only.
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/kicad_electronique.hpp"

using namespace prom::ecad;
namespace fs = std::filesystem;

namespace {

void placer(ConceptionElec& c, const std::string& reference, double x, double y, double rotation = 0) {
  for (auto& e : c.circuit.empreintes)
    if (const ComposantElec* k = c.composant(e.composant); k && k->reference == reference) {
      e.position = ptMm(x, y);
      e.rotation = rotation;
    }
}

// Simple face, cuivre dessous, tout en traversant : les perçages sont tous non métallisés (F9)
ConceptionElec simpleFace() {
  ConceptionElec c = exempleElectronique();
  c.nom = "Corpus simple face";
  for (auto& x : c.composants) {
    if (x.reference.empty()) continue;
    switch (x.reference.front()) {
      case 'R': x.empreinte = "R_Axial_P10.16"; break;
      case 'C': x.empreinte = "CP_Radial_D5.0_P2.00"; break;
      case 'D': x.empreinte = "LED_D5.0"; break;
      case 'U': x.empreinte = "DIP-8"; break;
      default: break;
    }
  }
  c.circuit.empilage.couchesCuivre = 1;
  c.circuit.pistes.clear();
  c.circuit.vias.clear();
  c.circuit.contour.largeur = nm(80);
  c.circuit.contour.hauteur = nm(50);
  mettreAJourCircuit(c);
  placer(c, "J1", 6, 25);
  placer(c, "U1", 40, 25);
  placer(c, "R1", 20, 10);
  placer(c, "R2", 20, 40);
  placer(c, "R3", 58, 10);
  placer(c, "C1", 62, 26);
  placer(c, "C2", 32, 40);
  placer(c, "C3", 50, 40);
  placer(c, "D1", 66, 40);
  placer(c, "H1", 3.8, 3.8);
  placer(c, "H2", 76.2, 3.8);
  placer(c, "H3", 3.8, 46.2);
  placer(c, "H4", 76.2, 46.2);
  for (auto& z : c.circuit.zones) z.contour = {ptMm(0.4, 0.4), ptMm(79.6, 0.4), ptMm(79.6, 49.6), ptMm(0.4, 49.6)};
  routerSimple(c);
  return c;
}

// Quatre couches : plans de masse et d'alimentation intérieurs, vias de part en part
ConceptionElec quatreCouches() {
  ConceptionElec c = exempleElectronique();
  c.nom = "Corpus quatre couches";
  c.circuit.empilage.couchesCuivre = 4;
  Zone gnd, vcc;
  gnd.uuid = nouvelUuid();
  gnd.reseau = "GND";
  gnd.couche = Couche::CuivreInt1;
  gnd.contour = {ptMm(0.4, 0.4), ptMm(49.6, 0.4), ptMm(49.6, 31.6), ptMm(0.4, 31.6)};
  vcc = gnd;
  vcc.uuid = nouvelUuid();
  vcc.reseau = "+5V";
  vcc.couche = Couche::CuivreInt2;
  c.circuit.zones.push_back(gnd);
  c.circuit.zones.push_back(vcc);
  c.circuit.vias.push_back({nouvelUuid(), "GND", ptMm(10, 28), nm(0.8), nm(0.4)});
  c.circuit.vias.push_back({nouvelUuid(), "+5V", ptMm(40, 28), nm(0.8), nm(0.4)});
  return c;
}

// La carte d'exemple retournée : chaque composant dessous, la carte vue en miroir (x → L − x), pistes et zones
// passées sur l'autre face ; même circuit, fabriqué par l'autre côté
ConceptionElec retournee(ConceptionElec c) {
  c.nom = "Corpus composants dessous";
  const Boite b = boite(c.circuit.contour.polygone());
  const Nm somme = b.x0 + b.x1;
  auto miroir = [somme](Pt p) { return Pt{somme - p.x, p.y}; };
  for (auto& e : c.circuit.empreintes) {
    e.position = miroir(e.position);
    e.rotation = -e.rotation;
    e.dessous = !e.dessous;
  }
  for (auto& p : c.circuit.pistes) {
    p.a = miroir(p.a);
    p.b = miroir(p.b);
    p.couche = opposee(p.couche);
  }
  for (auto& v : c.circuit.vias) v.p = miroir(v.p);
  for (auto& z : c.circuit.zones) {
    for (auto& q : z.contour) q = miroir(q);
    z.contour = orienter(z.contour);
    z.couche = opposee(z.couche);
  }
  for (auto& t : c.circuit.textes) {
    t.p = miroir(t.p);
    t.rotation = -t.rotation;
    t.couche = opposee(t.couche);
  }
  for (auto& q : c.circuit.contour.libre) q = miroir(q);
  if (!c.circuit.contour.libre.empty()) c.circuit.contour.libre = orienter(c.circuit.contour.libre);
  for (auto& d : c.circuit.contour.decoupes) {
    for (auto& q : d) q = miroir(q);
    d = orienter(d);
  }
  return c;
}

// Zones sur les deux faces et découpe interne : le cuivre contourne la fenêtre fraisée
ConceptionElec zonesDecoupe() {
  ConceptionElec c = exempleElectronique();
  c.nom = "Corpus zones et decoupe";
  c.circuit.contour.largeur = nm(72);
  for (auto& z : c.circuit.zones) z.contour = {ptMm(0.4, 0.4), ptMm(71.6, 0.4), ptMm(71.6, 31.6), ptMm(0.4, 31.6)};
  c.circuit.contour.decoupes.push_back(orienter({ptMm(55, 10), ptMm(66, 10), ptMm(66, 22), ptMm(55, 22)}));
  Zone dessus;
  dessus.uuid = nouvelUuid();
  dessus.reseau = "GND";
  dessus.couche = Couche::CuivreDessus;
  dessus.contour = {ptMm(51, 0.4), ptMm(71.6, 0.4), ptMm(71.6, 31.6), ptMm(51, 31.6)};
  c.circuit.zones.push_back(dessus);
  c.circuit.vias.push_back({nouvelUuid(), "GND", ptMm(69, 3), nm(0.8), nm(0.4)});
  c.circuit.vias.push_back({nouvelUuid(), "GND", ptMm(69, 29), nm(0.8), nm(0.4)});
  return c;
}

// Faits attendus de la conception, tirés du modèle (pas des fichiers écrits)
std::string attendu(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  const int nc = static_cast<int>(couchesCuivre(c.circuit.empilage.couchesCuivre).size());
  int composants = 0, npth = 0;
  for (const auto& e : c.circuit.empreintes) composants += dansFichierPlacement(c, e) ? 1 : 0;
  for (const auto& p : pastilles(c, k))
    if (p.pastille.percage > 0 && (p.pastille.type == TypePastille::NonMetallisee || nc == 1)) ++npth;
  if (nc == 1) npth += static_cast<int>(c.circuit.vias.size());
  const Boite b = boite(c.circuit.contour.polygone());
  char t[160];
  std::snprintf(t, sizeof t, "--couches %d --composants %d --trous-npth %d --carte %.3f %.3f\n", nc, composants, npth, mm(b.largeur()), mm(b.hauteur()));
  return t;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Utilisation : corpus_fabrication <dossier> <données d'essai>\n";
    return 64;
  }
  const fs::path racine = prom::cheminFichier(argv[1]), donnees = prom::cheminFichier(argv[2]);
  std::vector<std::pair<std::string, ConceptionElec>> corpus;
  try {
    corpus.emplace_back("exemple", exempleElectronique());
    corpus.emplace_back("simple-face", simpleFace());
    corpus.emplace_back("quatre-couches", quatreCouches());
    corpus.emplace_back("dessous", retournee(exempleElectronique()));
    corpus.emplace_back("zones-decoupe", zonesDecoupe());
    for (const char* nom : {"StickHub", "ecc83-pp"}) {
      const fs::path f = donnees / "kicad" / (std::string(nom) + ".kicad_pcb");
      if (!fs::exists(f)) {
        std::cerr << "carte KiCad d'essai introuvable : " << f.string() << '\n';
        return 2;
      }
      corpus.emplace_back(std::string("kicad-") + nom, conceptionDepuisKicad(prom::lireFichier(f), nom));
    }
  } catch (const std::exception& e) {
    std::cerr << "corpus : " << e.what() << '\n';
    return 2;
  }
  int echecs = 0;
  for (const auto& [nom, c] : corpus) {
    const fs::path d = racine / nom;
    fs::remove_all(d);
    fs::create_directories(d);
    try {
      ecrireDossierFabrication(c, d.string(), "corpus");
      const auto defauts = controlerDossierFabrication(c, d.string());
      prom::ecrireFichierAtomique(d / "attendu.txt", attendu(c));
      std::cout << nom << " : " << (defauts.empty() ? "écrit et contrôlé" : "contrôle du noyau en défaut") << " ; attendu " << attendu(c);
      for (const auto& x : defauts) std::cout << "  - " << x << '\n';
      echecs += defauts.empty() ? 0 : 1;
    } catch (const std::exception& e) {
      std::cout << nom << " : " << e.what() << '\n';
      ++echecs;
    }
  }
  return echecs == 0 ? 0 : 1;
}

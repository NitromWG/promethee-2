// Prométhée : exemple électronique livré avec le logiciel — un clignotant à NE555 monté en astable. Schéma câblé
// (fils, jonctions, étiquettes, alimentations, découplage), composants réels (fabricant et référence), circuit
// imprimé 2 couches placé, routé, avec plan de masse au dos et trous de fixation. Licence GPL-3.0-only.
#include "promethee/electronique.hpp"

namespace prom::ecad {

ConceptionElec exempleElectronique() {
  ConceptionElec c = nouvelleConception("Clignotant 555");
  c.cartouche = {"Clignotant à NE555", "Prométhée", "Exemple", "A", "2026-10-09", "Exemple livré avec Prométhée"};
  c.feuilles[0].nom = "Clignotant";
  auto poser = [&](const char* id, double x, double y, int rotation = 0, bool miroir = false) {
    const std::string u = poserSymbole(c, 0, id, ptMm(x, y), rotation);
    c.feuilles[0].symboles.back().miroir = miroir;
    return u;
  };
  auto broche = [&](const std::string& symbole, const char* numero) -> Pt {
    for (const auto& s : c.feuilles[0].symboles)
      if (s.uuid == symbole)
        for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
          if (b.broche->numero == numero) return b.p;
    return {};
  };
  auto fil = [&](Pt a, Pt b) { c.feuilles[0].fils.push_back({nouvelUuid(), a, b}); };
  auto P = [](double x, double y) { return ptMm(x, y); };
  auto composant = [&](const std::string& symbole, const char* ref, const char* valeur, const char* fabricant, const char* mpn, const char* empreinte = nullptr) {
    for (const auto& s : c.feuilles[0].symboles)
      if (s.uuid == symbole)
        if (ComposantElec* k = c.composant(s.composant)) {
          k->reference = ref;
          k->valeur = valeur;
          k->fabricant = fabricant;
          k->mpn = mpn;
          if (empreinte) k->empreinte = empreinte;
        }
  };
  // ----- Alimentation : connecteur J1 -----
  const std::string j1 = poser("Conn_01x02", 76.2, 86.36, 0, true);
  fil(broche(j1, "1"), P(86.36, 85.09));
  poser("+5V", 86.36, 85.09);
  fil(broche(j1, "2"), P(91.44, 87.63));
  poser("GND", 91.44, 87.63);
  // ----- Temporisateur U1 -----
  const std::string u1 = poser("NE555", 142.24, 86.36);
  fil(broche(u1, "8"), P(142.24, 71.12));
  poser("+5V", 142.24, 71.12);
  fil(broche(u1, "1"), P(142.24, 101.6));
  poser("GND", 142.24, 101.6);
  // Remise à zéro (4) tenue à l'alimentation
  fil(broche(u1, "4"), P(127, 81.28));
  fil(P(127, 81.28), P(127, 71.12));
  fil(P(127, 71.12), P(142.24, 71.12));
  // Déclenchement (2) et seuil (6) reliés, vers le condensateur de temporisation
  fil(broche(u1, "2"), P(124.46, 83.82));
  fil(P(124.46, 83.82), P(124.46, 88.9));
  fil(broche(u1, "6"), P(124.46, 88.9));
  fil(P(124.46, 88.9), P(116.84, 88.9));
  // Tension de contrôle (5) découplée
  const std::string c2 = poser("C", 121.92, 99.06);
  fil(broche(u1, "5"), P(121.92, 91.44));
  fil(P(121.92, 91.44), broche(c2, "1"));
  fil(broche(c2, "2"), P(121.92, 105.41));
  poser("GND", 121.92, 105.41);
  // Réseau de temporisation R1, R2, C1
  const std::string r1 = poser("R", 116.84, 63.5);
  const std::string r2 = poser("R", 116.84, 78.74);
  const std::string c1 = poser("C_Pol", 116.84, 96.52, 0, true);  // repères à gauche, loin de C2
  fil(broche(r1, "1"), P(116.84, 55.88));
  poser("+5V", 116.84, 55.88);
  fil(broche(r1, "2"), broche(r2, "1"));
  fil(broche(r2, "2"), broche(c1, "1"));
  fil(broche(c1, "2"), P(116.84, 104.14));
  poser("GND", 116.84, 104.14);
  // Décharge (7) : étiquette entre le point R1-R2 et la broche
  fil(P(116.84, 71.12), P(106.68, 71.12));
  c.feuilles[0].etiquettes.push_back({nouvelUuid(), "DECH", P(106.68, 71.12), 0, TypeEtiquette::Locale});
  fil(broche(u1, "7"), P(157.48, 88.9));
  c.feuilles[0].etiquettes.push_back({nouvelUuid(), "DECH", P(157.48, 88.9), 0, TypeEtiquette::Locale});
  // Sortie (3) : résistance R3 et LED D1
  const std::string r3 = poser("R", 165.1, 91.44);
  const std::string d1 = poser("LED", 165.1, 102.87, 3);
  fil(broche(u1, "3"), P(165.1, 83.82));
  fil(P(165.1, 83.82), broche(r3, "1"));
  fil(broche(r3, "2"), broche(d1, "2"));
  fil(broche(d1, "1"), P(165.1, 110.49));
  poser("GND", 165.1, 110.49);
  // Découplage de l'alimentation au plus près du circuit
  const std::string c3 = poser("C", 182.88, 76.2);
  fil(broche(c3, "1"), P(182.88, 68.58));
  poser("+5V", 182.88, 68.58);
  fil(broche(c3, "2"), P(182.88, 83.82));
  poser("GND", 182.88, 83.82);
  // Trous de fixation
  std::string trous[4];
  for (int i = 0; i < 4; ++i) trous[i] = poser("Trou_fixation", 78.74 + 12.7 * i, 124.46);
  c.feuilles[0].textes.push_back({nouvelUuid(), "Clignotant à NE555 monté en astable : f = 1,44 / ((R1 + 2 R2) C1), environ 1,5 Hz", P(135.89, 45.72), 2'200'000});
  c.feuilles[0].textes.push_back({nouvelUuid(), "Alimentation 5 V", P(83.82, 76.2), 1'800'000});
  // Composants réels
  composant(j1, "J1", "Alim 5 V", "Würth Elektronik", "61300211121");
  composant(u1, "U1", "NE555", "Texas Instruments", "NE555DR");
  composant(r1, "R1", "1k", "Yageo", "RC0603FR-071KL");
  composant(r2, "R2", "47k", "Yageo", "RC0603FR-0747KL");
  composant(r3, "R3", "330", "Yageo", "RC0603FR-07330RL");
  composant(c1, "C1", "10µF", "Nichicon", "UVR1C100MDD", "CP_Radial_D5.0_P2.00");
  composant(c2, "C2", "10nF", "Murata", "GRM188R71H103KA01D");
  composant(c3, "C3", "100nF", "Murata", "GRM188R71H104KA93D");
  composant(d1, "D1", "Rouge", "Kingbright", "APT2012EC");
  for (int i = 0; i < 4; ++i) composant(trous[i], ("H" + std::to_string(i + 1)).c_str(), "M3", "", "");
  // ----- Circuit imprimé -----
  Circuit& k = c.circuit;
  k.contour.largeur = nm(50);
  k.contour.hauteur = nm(32);
  k.contour.rayon = nm(1.5);
  mettreAJourCircuit(c);
  struct Place {
    const char* ref;
    double x, y, rotation;
  };
  const Place places[] = {{"J1", 5, 16, 0},       {"U1", 25, 16, 0},       {"C3", 31, 13, 0},       {"R1", 34.5, 12.5, 270}, {"R2", 34.5, 17.5, 270},
                          {"C1", 38, 24, 0},      {"C2", 31, 21.5, 270},   {"R3", 19.5, 16.635, 180}, {"D1", 14, 16.635, 0}, {"H1", 3.8, 3.8, 0},
                          {"H2", 46.2, 3.8, 0},   {"H3", 3.8, 28.2, 0},    {"H4", 46.2, 28.2, 0}};
  for (auto& e : k.empreintes)
    if (const ComposantElec* x = c.composant(e.composant))
      for (const auto& p : places)
        if (x->reference == p.ref) {
          e.position = ptMm(p.x, p.y);
          e.rotation = p.rotation;
        }
  // Plan de masse sur toute la face arrière
  Zone z;
  z.uuid = nouvelUuid();
  z.reseau = "GND";
  z.couche = Couche::CuivreDessous;
  z.contour = {ptMm(0.4, 0.4), ptMm(49.6, 0.4), ptMm(49.6, 31.6), ptMm(0.4, 31.6)};
  k.zones.push_back(z);
  routerSimple(c);
  // Repères de sérigraphie
  k.textes.push_back({nouvelUuid(), "CLIGNOTANT 555 rev A", Couche::SerigraphieDessus, ptMm(25, 29.2), 1'000'000, 0});
  return c;
}

}  // namespace prom::ecad

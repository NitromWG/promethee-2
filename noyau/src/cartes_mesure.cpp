// Prométhée : cartes de mesure (lot L1, tâche 1.1, AUT-12, Q5). Une carte de la taille voulue, toujours la même
// pour une taille donnée, sur laquelle relever les temps de l'atelier à chaque construction : blocs de dix
// composants (un NE555 monté en astable, six résistances, trois condensateurs) reliés par des étiquettes au bout de
// fils courts, dix blocs par feuille, un connecteur d'alimentation, les empreintes en grille et, si demandé, un plan
// de masse sur toute la face des composants, qui contourne chaque pastille : c'est le cuivre rempli qui coûte (A7).
// La carte n'est pas routée : le chevelu reste à faire.
// Licence GPL-3.0-only.
#include <cmath>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

namespace {
// Vers le corps du symbole, selon BrochePlacee::direction ; le fil part dans l'autre sens
Pt versCorps(int direction) {
  switch (direction) {
    case 0: return {1, 0};
    case 1: return {0, 1};
    case 2: return {-1, 0};
    default: return {0, -1};
  }
}
}  // namespace

ConceptionElec carteDeMesure(int composants, bool zone) {
  const int blocs = std::max(1, (composants + 9) / 10);
  ConceptionElec c = nouvelleConception("Carte de mesure " + std::to_string(blocs * 10));
  c.cartouche.titre = "Carte de mesure, " + std::to_string(blocs * 10) + " composants";
  c.cartouche.commentaire = "Générée pour relever les temps de l'atelier (AUT-12)";
  c.feuilles[0].nom = "Alimentation et blocs 1 à 10";
  c.feuilles[0].format = "A3";
  // Une étiquette au bout d'un fil de 2,54 mm tiré de la broche, vers l'extérieur du symbole
  auto relier = [&c](std::size_t feuille, const std::string& symbole, const char* numero, const std::string& reseau, bool globale) {
    Feuille& f = c.feuilles[feuille];
    for (const auto& s : f.symboles)
      if (s.uuid == symbole)
        for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
          if (b.broche->numero == numero) {
            const Pt d = versCorps(b.direction);
            const Pt bout{b.p.x - d.x * 2'540'000, b.p.y - d.y * 2'540'000};
            f.fils.push_back({nouvelUuid(), b.p, bout});
            Etiquette e;
            e.uuid = nouvelUuid();
            e.texte = reseau;
            e.p = bout;
            e.type = globale ? TypeEtiquette::Globale : TypeEtiquette::Locale;
            f.etiquettes.push_back(e);
            return;
          }
  };
  // Alimentation : un connecteur, source de VCC et de GND (EL-14)
  const std::string j = poserSymbole(c, 0, "Conn_01x02", ptMm(20.32, 20.32));
  relier(0, j, "1", "VCC", true);
  relier(0, j, "2", "GND", true);
  for (int b = 0; b < blocs; ++b) {
    const std::size_t feuille = static_cast<std::size_t>(b / 10);
    while (c.feuilles.size() <= feuille) {
      Feuille f;
      f.uuid = nouvelUuid();
      f.nom = "Blocs " + std::to_string(c.feuilles.size() * 10 + 1) + " à " + std::to_string(c.feuilles.size() * 10 + 10);
      f.format = "A3";
      c.feuilles.push_back(f);
    }
    // Cinq blocs par rangée, deux rangées par feuille, chacun dans un carré de 76,2 × 76,2 mm
    const double x0 = 50.8 + 76.2 * (b % 5), y0 = 50.8 + 101.6 * ((b % 10) / 5);
    const std::string n = "B" + std::to_string(b + 1) + "_";
    const std::string u = poserSymbole(c, feuille, "NE555", ptMm(x0 + 25.4, y0 + 30.48));
    relier(feuille, u, "8", "VCC", true);
    relier(feuille, u, "1", "GND", true);
    relier(feuille, u, "4", "VCC", true);
    relier(feuille, u, "2", n + "TR", false);
    relier(feuille, u, "6", n + "TR", false);
    relier(feuille, u, "7", n + "DIS", false);
    relier(feuille, u, "3", n + "Q", false);
    relier(feuille, u, "5", n + "CV", false);
    // Résistances : 1 et 2 du réseau RC, 3 et 4 de la sortie, 5 et 6 en pont
    const char* r[][2] = {{"VCC", "DIS"}, {"DIS", "TR"}, {"Q", "LED"}, {"LED", "GND"}, {"VCC", "PU"}, {"PU", "GND"}};
    for (int k = 0; k < 6; ++k) {
      const std::string s = poserSymbole(c, feuille, "R", ptMm(x0 + 50.8 + 10.16 * (k % 2), y0 + 10.16 + 20.32 * (k / 2)));
      for (int p = 0; p < 2; ++p) {
        const std::string res = r[k][p];
        const bool global = res == "VCC" || res == "GND";
        relier(feuille, s, p == 0 ? "1" : "2", global ? res : n + res, global);
      }
    }
    // Condensateurs : celui du réseau RC, celui de CV, le découplage
    const char* cc[][2] = {{"TR", "GND"}, {"CV", "GND"}, {"VCC", "GND"}};
    for (int k = 0; k < 3; ++k) {
      const std::string s = poserSymbole(c, feuille, "C", ptMm(x0 + 5.08 + 10.16 * k, y0 + 60.96));
      for (int p = 0; p < 2; ++p) {
        const std::string res = cc[k][p];
        const bool global = res == "VCC" || res == "GND";
        relier(feuille, s, p == 0 ? "1" : "2", global ? res : n + res, global);
      }
    }
  }
  annoter(c, false);
  mettreAJourCircuit(c);
  // Empreintes en grille de 6,35 mm, le connecteur dans le premier rang ; la carte autour, à 5 mm des bords
  const int colonnes = std::max(4, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(c.circuit.empreintes.size())))));
  for (std::size_t i = 0; i < c.circuit.empreintes.size(); ++i)
    c.circuit.empreintes[i].position = ptMm(8 + 6.35 * static_cast<double>(i % static_cast<std::size_t>(colonnes)), 8 + 6.35 * static_cast<double>(i / static_cast<std::size_t>(colonnes)));
  const int rangs = static_cast<int>((c.circuit.empreintes.size() + static_cast<std::size_t>(colonnes) - 1) / static_cast<std::size_t>(colonnes));
  c.circuit.contour.origine = {0, 0};
  c.circuit.contour.largeur = nm(16 + 6.35 * (colonnes - 1));
  c.circuit.contour.hauteur = nm(16 + 6.35 * (rangs - 1));
  if (zone) {
    Zone z;
    z.uuid = nouvelUuid();
    z.reseau = "GND";
    z.couche = Couche::CuivreDessus;
    z.contour = {Pt{0, 0}, Pt{c.circuit.contour.largeur, 0}, Pt{c.circuit.contour.largeur, c.circuit.contour.hauteur}, Pt{0, c.circuit.contour.hauteur}};
    c.circuit.zones.push_back(z);
  }
  return c;
}

}  // namespace prom::ecad

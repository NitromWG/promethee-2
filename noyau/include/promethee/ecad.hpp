// Prométhée : socle de la CAO électronique. Coordonnées entières en nanomètres (exactes : aucune dérive des
// flottants dans les intersections, décalages et isolements), identifiants stables, géométrie, moteur de polygones
// (opérations booléennes et décalages sur entiers, adossés à Clipper2) et index spatial. Aucune dépendance à
// l'interface. Repère des éditeurs : x vers la droite, y vers le bas (comme à l'écran) ; les sorties de
// fabrication retournent l'axe y. Licence GPL-3.0-only.
#pragma once

#include <cmath>
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace prom::ecad {

using Nm = std::int64_t;
inline constexpr Nm NM_PAR_MM = 1'000'000;
inline Nm nm(double millimetres) { return static_cast<Nm>(std::llround(millimetres * 1e6)); }
inline double mm(Nm v) { return static_cast<double>(v) * 1e-6; }

struct Pt {
  Nm x = 0, y = 0;
  friend bool operator==(const Pt&, const Pt&) = default;
  friend auto operator<=>(const Pt&, const Pt&) = default;
  Pt operator+(Pt o) const { return {x + o.x, y + o.y}; }
  Pt operator-(Pt o) const { return {x - o.x, y - o.y}; }
};
inline Pt ptMm(double x, double y) { return {nm(x), nm(y)}; }

// Boîte englobante alignée sur les axes ; vide tant que x1 < x0.
struct Boite {
  Nm x0 = 0, y0 = 0, x1 = -1, y1 = -1;
  bool vide() const { return x1 < x0 || y1 < y0; }
  void etendre(Pt p);
  void etendre(const Boite& b);
  Boite gonflee(Nm d) const;
  bool touche(const Boite& b) const;
  bool contient(Pt p) const;
  bool contient(const Boite& b) const;
  Pt centre() const { return {(x0 + x1) / 2, (y0 + y1) / 2}; }
  Nm largeur() const { return x1 - x0; }
  Nm hauteur() const { return y1 - y0; }
  friend bool operator==(const Boite&, const Boite&) = default;
};

// Identifiant stable (UUID version 4) : la référence d'un composant (R1, R27…) n'est jamais son identité.
std::string nouvelUuid();
// Identifiant à la forme d'un UUID, toujours le même pour une même graine (objets anciens sans identifiant, lus
// d'un fichier ou d'un import : la même lecture donne les mêmes identifiants)
std::string uuidDerive(std::string_view graine);

// Rotations dans le sens direct vu à l'écran (y vers le bas) : un quart de tour envoie +x vers le haut.
Pt tournerQuarts(Pt p, int quarts);
Pt tournerDegres(Pt p, double degres);

double distance(Pt a, Pt b);
double distancePointSegment(Pt p, Pt a, Pt b);
double distanceSegments(Pt a1, Pt a2, Pt b1, Pt b2);
Pt projeteSurSegment(Pt p, Pt a, Pt b);
bool surSegment(Pt p, Pt a, Pt b);  // p sur [a, b], à 1 nm près
bool segmentsSeCoupent(Pt a1, Pt a2, Pt b1, Pt b2);

// Moteur de polygones. Un ensemble de polygones suit la règle « non nul » : contours extérieurs dans un sens,
// trous dans l'autre (c'est ce que rendent toutes les opérations).
using Polygone = std::vector<Pt>;
using Polygones = std::vector<Polygone>;
Polygones unir(const Polygones& a);
Polygones soustraire(const Polygones& a, const Polygones& b);
Polygones intersecter(const Polygones& a, const Polygones& b);
Polygones gonfler(const Polygones& a, Nm d);  // décalage à angles arrondis ; négatif : rétrécit
Polygones trianguler(const Polygones& a);     // triangles (sorties de fabrication sans trous)
double aireMm2(const Polygones& p);           // trous déduits
double aireMm2(const Polygone& p);            // valeur absolue
bool dedans(const Polygone& p, Pt q);         // bord compris
bool dedans(const Polygones& p, Pt q);        // trous exclus
Boite boite(const Polygone& p);
Boite boite(const Polygones& p);
Polygone orienter(Polygone p);                // sens positif (contour extérieur)
// Approximation des arcs. Extérieure : le polygone contient l'arc vrai (cuivre, perçages : les isolements
// restent sûrs). Intérieure : il est contenu dans l'arc vrai (contour de carte : la distance au bord n'est
// jamais surestimée). Dans les deux cas les côtés droits tombent exactement à leur cote.
enum class Approche { Exterieure, Interieure };
Polygone cercle(Pt c, Nm r);
Polygone rectangle(Pt c, Nm l, Nm h, double degres = 0);
Polygone rectangleArrondi(Pt c, Nm l, Nm h, Nm r, double degres = 0, Approche approche = Approche::Exterieure);
Polygone oblong(Pt a, Pt b, Nm largeur);      // segment épaissi (piste, pastille ovale)
double distancePolygones(const Polygone& a, const Polygone& b);         // 0 s'ils se touchent ou se chevauchent
double distancePolygoneSegment(const Polygone& p, Pt a, Pt b);          // 0 si le segment touche le polygone
struct Ilot {
  Polygone exterieur;
  Polygones trous;
};
std::vector<Ilot> ilots(const Polygones& p);  // morceaux d'un seul tenant, avec leurs trous

// Index spatial par grille de hachage : on ne teste jamais chaque objet contre tous les autres. La recherche rend
// des candidats ; le test exact de géométrie vient ensuite.
class IndexSpatial {
 public:
  explicit IndexSpatial(Nm cellule = 2 * NM_PAR_MM) : m_cellule(cellule) {}
  void inserer(int id, const Boite& b);
  std::vector<int> chercher(const Boite& b) const;
  void vider() { m_cases.clear(); m_grands.clear(); }

 private:
  Nm m_cellule;
  std::unordered_map<std::int64_t, std::vector<int>> m_cases;
  std::vector<int> m_grands;  // objets trop étendus pour la grille : toujours candidats
  static std::int64_t cle(std::int64_t i, std::int64_t j) { return (i << 32) ^ (j & 0xffffffff); }
};

}  // namespace prom::ecad

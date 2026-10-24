// Prométhée : modèle des contraintes du circuit imprimé — noms des genres, lecture et écriture des valeurs
// « min/pref/max » avec leur part interne (« 0,15/0,2 »). La résolution sur les objets est dans regles_carte.cpp.
// Licence GPL-3.0-only.
#include "promethee/contraintes.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace prom::ecad {

namespace {
struct NomGenre {
  GenreContrainte g;
  const char* code;
  const char* nom;
};
constexpr NomGenre GENRES[] = {
    {GenreContrainte::Isolement, "isolement", "Isolement"},
    {GenreContrainte::Largeur, "largeur", "Largeur de piste"},
    {GenreContrainte::ViaDiametre, "viaDiametre", "Diamètre de via"},
    {GenreContrainte::ViaPercage, "viaPercage", "Perçage de via"},
    {GenreContrainte::EcartPaire, "ecartPaire", "Écart de paire"},
    {GenreContrainte::NonCouple, "nonCouple", "Longueur non couplée"},
    {GenreContrainte::Longueur, "longueur", "Longueur"},
    {GenreContrainte::Retard, "retard", "Retard"},
    {GenreContrainte::NombreVias, "nombreVias", "Nombre de vias"},
    {GenreContrainte::CouchesPermises, "couchesPermises", "Couches permises"},
    {GenreContrainte::Interdiction, "interdiction", "Interdiction"},
    {GenreContrainte::Bord, "bord", "Distance au bord"},
    {GenreContrainte::CuivreTrou, "cuivreTrou", "Cuivre au perçage"},
    {GenreContrainte::ParalleleMax, "paralleleMax", "Longueur parallèle"},
    {GenreContrainte::Courant, "courant", "Courant"},
    {GenreContrainte::EcartComposants, "ecartComposants", "Écart entre composants"},
    {GenreContrainte::Orientations, "orientations", "Orientations permises"},
    {GenreContrainte::MargeVernis, "margeVernis", "Marge de vernis"},
    {GenreContrainte::MargePate, "margePate", "Marge de pâte"},
    {GenreContrainte::PontVernis, "pontVernis", "Pont de vernis"},
    {GenreContrainte::HauteurMax, "hauteurMax", "Hauteur maximale"},
    {GenreContrainte::Proximite, "proximite", "Proximité"},
    {GenreContrainte::ViasParPastille, "sortiePastille", "Sortie des pastilles CMS"},
    {GenreContrainte::PateNonMontes, "pateNonMontes", "Pâte sous les non-montés"},
    {GenreContrainte::IsolementCouches, "isolementCouches", "Isolement entre couches"},
    {GenreContrainte::Ecart3D, "ecart3D", "Écart 3D entre corps"},
};

// « 0,15 », « 0.15 mm », « 150 µm », « 6 mil », « 25 ps » → nanomètres (ou picosecondes pour un retard, gardées telles)
bool lireLongueur(std::string t, Nm& v) {
  t.erase(std::remove_if(t.begin(), t.end(), [](unsigned char ch) { return std::isspace(ch); }), t.end());
  std::replace(t.begin(), t.end(), ',', '.');
  if (t.empty()) return false;
  char* fin = nullptr;
  const double x = std::strtod(t.c_str(), &fin);
  if (fin == t.c_str() || !std::isfinite(x)) return false;
  const std::string unite(fin);
  double facteur = 1e6;  // mm par défaut
  if (unite.empty() || unite == "mm") facteur = 1e6;
  else if (unite == "um" || unite == "µm") facteur = 1e3;
  else if (unite == "mil") facteur = 25'400;
  else if (unite == "in") facteur = 25.4e6;
  else if (unite == "nm") facteur = 1;
  else if (unite == "ps" || unite == "A" || unite == "V" || unite == "x") facteur = 1;  // grandeurs sans longueur : gardées
  else return false;
  v = static_cast<Nm>(std::llround(x * facteur));
  return true;
}
std::string mmCourt(Nm v) {
  char t[32];
  std::snprintf(t, sizeof t, "%.4g", static_cast<double>(v) * 1e-6);
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
}  // namespace

const char* codeGenreContrainte(GenreContrainte g) {
  for (const auto& x : GENRES)
    if (x.g == g) return x.code;
  return "isolement";
}
const char* nomGenreContrainte(GenreContrainte g) {
  for (const auto& x : GENRES)
    if (x.g == g) return x.nom;
  return "?";
}
bool genreContrainteDepuisCode(const std::string& code, GenreContrainte& g) {
  for (const auto& x : GENRES)
    if (code == x.code) {
      g = x.g;
      return true;
    }
  return false;
}

bool lireValeurContrainte(const std::string& texte, ValeurContrainte& v) {
  // « min » ou « externe/interne » ; un « < » ou « max » devant écrit un maximum : « <3 », « max 3 »
  std::string t = texte;
  bool maximum = false;
  if (!t.empty() && t.front() == '<') {
    maximum = true;
    t.erase(0, 1);
  } else if (t.rfind("max", 0) == 0) {
    maximum = true;
    t.erase(0, 3);
  }
  const auto barre = t.find('/');
  Nm a = 0, b = 0;
  if (!lireLongueur(barre == std::string::npos ? t : t.substr(0, barre), a)) return false;
  ValeurContrainte r;
  (maximum ? r.max : r.min) = a;
  if (barre != std::string::npos) {
    if (!lireLongueur(t.substr(barre + 1), b)) return false;
    r.interneDistincte = true;
    (maximum ? r.maxInterne : r.minInterne) = b;
  }
  v = r;
  return true;
}

std::string texteValeurContrainte(const ValeurContrainte& v) {
  auto un = [](Nm mn, Nm pf, Nm mx) {
    std::string s;
    if (mn) s += mmCourt(mn);
    if (pf && pf != mn) s += (s.empty() ? "" : " ") + std::string("préf. ") + mmCourt(pf);
    if (mx) s += (s.empty() ? "" : " ") + std::string("max ") + mmCourt(mx);
    return s.empty() ? std::string("—") : s;
  };
  std::string s = un(v.min, v.pref, v.max);
  if (v.interneDistincte) s += " / " + un(v.minInterne, v.prefInterne, v.maxInterne);
  return s;
}

}  // namespace prom::ecad

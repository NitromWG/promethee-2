// Prométhée : appareils électriques personnels. Le symbole se dessine d'après les bornes de l'appareil, côté par
// côté : cadre de la CEI 60617 (élément d'appareil) avec son marquage, bornes au pas de 5,08 mm sur la grille de
// 1,27 mm, repère et valeur placés là où aucune borne ne sort. Licence GPL-3.0-only.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "electrique_interne.hpp"
#include "promethee/electrique.hpp"

namespace prom::elec {

using ecad::Broche;
using ecad::DefSymbole;
using ecad::nm;
using ecad::ptMm;
using ecad::Trace;
using ecad::TypeBroche;

namespace {

constexpr double PAS = 5.08;     // entre deux bornes d'un même côté
constexpr double BROCHE = 2.54;  // longueur du trait de borne
constexpr double GRILLE = 1.27;
constexpr std::size_t BORNES_PAR_COTE = 40;
constexpr std::size_t LONGUEUR_BORNE = 8;
const std::string PREFIXE_ID = "perso:";
const std::string FAMILLE = "Mes appareils";

double grilleSup(double v) { return std::ceil(v / GRILLE - 1e-9) * GRILLE; }

std::size_t longueurUtf8(const std::string& s) {
  std::size_t n = 0;
  for (unsigned char ch : s) n += (ch & 0xC0) != 0x80 ? 1 : 0;
  return n;
}

std::string rogne(const std::string& s) {
  const auto debut = s.find_first_not_of(" \t\r\n");
  if (debut == std::string::npos) return {};
  return s.substr(debut, s.find_last_not_of(" \t\r\n") - debut + 1);
}

// Lettre de repère : lettres ASCII seules, en capitales
std::string lettres(const std::string& s) {
  std::string r;
  for (unsigned char ch : s)
    if (std::isalpha(ch) && ch < 0x80) r += static_cast<char>(std::toupper(ch));
  return r;
}

// Identifiant lisible : accents du français retirés, espaces en tirets
std::string sansAccents(const std::string& s) {
  static const std::pair<const char*, char> table[] = {
      {"à", 'a'}, {"â", 'a'}, {"ä", 'a'}, {"ç", 'c'}, {"é", 'e'}, {"è", 'e'}, {"ê", 'e'}, {"ë", 'e'}, {"î", 'i'}, {"ï", 'i'}, {"ô", 'o'}, {"ö", 'o'},
      {"ù", 'u'}, {"û", 'u'}, {"ü", 'u'}, {"ÿ", 'y'}, {"À", 'A'}, {"Â", 'A'}, {"Ç", 'C'}, {"É", 'E'}, {"È", 'E'}, {"Ê", 'E'}, {"Î", 'I'}, {"Ô", 'O'},
      {"Ù", 'U'}, {"Û", 'U'}, {"œ", 'o'}, {"Œ", 'O'}, {"æ", 'a'}, {"Æ", 'A'}};
  std::string r;
  for (std::size_t i = 0; i < s.size();) {
    bool remplace = false;
    for (const auto& [accent, lettre] : table) {
      const std::size_t n = std::char_traits<char>::length(accent);
      if (s.compare(i, n, accent) == 0) {
        r += lettre;
        i += n;
        remplace = true;
        break;
      }
    }
    if (!remplace) r += s[i++];
  }
  return r;
}

std::string slug(const std::string& nom) {
  std::string r;
  for (unsigned char ch : sansAccents(rogne(nom))) {
    if ((std::isalnum(ch) && ch < 0x80) || ch == '_' || ch == '.') r += static_cast<char>(ch);
    else if ((ch == ' ' || ch == '-' || ch == '/') && !r.empty() && r.back() != '-') r += '-';
  }
  while (!r.empty() && r.back() == '-') r.pop_back();
  return r.empty() ? std::string("appareil") : r;
}

const char* codeMontage(MontageAppareil m) {
  switch (m) {
    case MontageAppareil::Rail: return "rail";
    case MontageAppareil::Plaque: return "plaque";
    case MontageAppareil::Porte: return "porte";
    case MontageAppareil::HorsArmoire: return "hors-armoire";
  }
  return "rail";
}
MontageAppareil montageDepuis(const std::string& c) {
  for (MontageAppareil m : {MontageAppareil::Rail, MontageAppareil::Plaque, MontageAppareil::Porte, MontageAppareil::HorsArmoire})
    if (c == codeMontage(m)) return m;
  return MontageAppareil::Rail;
}

double nombreDe(const DefSymbole& d, const char* cle, double defaut) {
  const auto it = d.proprietes.find(cle);
  return it == d.proprietes.end() || it->second.empty() ? defaut : detail::lireNombre(it->second);
}

Broche borne(const std::string& numero, double x, double y, int direction) {
  Broche b;
  b.numero = numero;
  b.nom = "~";
  b.type = TypeBroche::Passive;
  b.extremite = ptMm(x, y);
  b.direction = direction;
  b.longueur = nm(BROCHE);
  b.nomVisible = false;
  b.numeroVisible = true;
  return b;
}

// Lettres du repère d'un appareil (« K12 » → « K »), sans le tiret de tête
std::string lettresRepere(const std::string& reference) {
  std::string r;
  for (char ch : reference) {
    if (ch == '-' && r.empty()) continue;
    if (std::isalpha(static_cast<unsigned char>(ch))) r += ch;
    else break;
  }
  return r;
}

int numeroApres(const std::string& reference, const std::string& prefixe) {
  if (reference.size() <= prefixe.size() || reference.compare(0, prefixe.size(), prefixe) != 0) return -1;
  int n = 0;
  for (std::size_t i = prefixe.size(); i < reference.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(reference[i]))) return -1;
    n = n * 10 + (reference[i] - '0');
    if (n > 1'000'000) return -1;
  }
  return n;
}

}  // namespace

const char* nomMontage(MontageAppareil m) {
  switch (m) {
    case MontageAppareil::Rail: return "Sur rail DIN";
    case MontageAppareil::Plaque: return "Sur la plaque de montage";
    case MontageAppareil::Porte: return "Sur la porte (façade)";
    case MontageAppareil::HorsArmoire: return "Hors armoire (sur la machine)";
  }
  return "Sur rail DIN";
}

bool estAppareilPersonnel(const std::string& symbole) { return symbole.rfind(PREFIXE_ID, 0) == 0; }

std::vector<std::string> lireBornes(const std::string& texte) {
  std::vector<std::string> r;
  std::string mot;
  auto finir = [&] {
    if (!mot.empty()) r.push_back(mot);
    mot.clear();
  };
  for (char ch : texte) {
    if (ch == ' ' || ch == '\t' || ch == ',' || ch == ';' || ch == '\r' || ch == '\n') finir();
    else mot += ch;
  }
  finir();
  return r;
}

std::string texteBornes(const std::vector<std::string>& bornes) {
  std::string r;
  for (const auto& b : bornes) r += (r.empty() ? "" : " ") + b;
  return r;
}

std::vector<std::string> defautsAppareil(const SpecAppareil& a) {
  std::vector<std::string> r;
  if (rogne(a.nom).empty()) r.push_back("Donne un nom à l'appareil (« Relais de sécurité », « Régulateur de température »…).");
  const std::string l = lettres(a.prefixe);
  if (l.empty() || l.size() > 3 || l.size() != rogne(a.prefixe).size())
    r.push_back("La lettre de repère se compose de 1 à 3 lettres sans accent (A, K, KF…).");
  std::set<std::string> vues;
  std::size_t total = 0;
  for (const auto* cote : {&a.haut, &a.bas, &a.gauche, &a.droite}) {
    total += cote->size();
    if (cote->size() > BORNES_PAR_COTE)
      r.push_back("Plus de " + std::to_string(BORNES_PAR_COTE) + " bornes sur un même côté : répartis-les sur les autres côtés.");
    for (const auto& b : *cote) {
      if (!vues.insert(b).second) r.push_back("La borne « " + b + " » figure deux fois : chaque borne a son propre numéro.");
      if (longueurUtf8(b) > LONGUEUR_BORNE) r.push_back("La borne « " + b + " » est trop longue : " + std::to_string(LONGUEUR_BORNE) + " caractères au plus.");
    }
  }
  if (total == 0) r.push_back("Indique au moins une borne (A1, 13, L, +24V…), sur le côté où elle sort.");
  if (a.largeur < 0 || a.hauteur < 0 || a.profondeur < 0) r.push_back("Les dimensions ne peuvent pas être négatives.");
  return r;
}

DefSymbole symboleAppareil(const SpecAppareil& a) {
  DefSymbole d;
  d.nom = rogne(a.nom).empty() ? std::string("Appareil") : rogne(a.nom);
  d.id = a.id.empty() ? PREFIXE_ID + slug(d.nom) : a.id;
  d.categorie = FAMILLE;
  const std::string l = lettres(a.prefixe);
  d.prefixe = l.empty() ? std::string("A") : l.substr(0, 3);
  d.description = rogne(a.description);
  d.fabricant = rogne(a.fabricant);
  d.mpn = rogne(a.reference);
  const std::string marquage = rogne(a.marquage);
  d.motsCles = "appareil personnel " + d.nom + " " + marquage + " " + d.fabricant + " " + d.mpn;
  // Cadre : assez large pour les bornes du haut et du bas et pour le marquage, assez haut pour celles des côtés
  const double nH = static_cast<double>(std::max(a.haut.size(), a.bas.size()));
  const double nV = static_cast<double>(std::max(a.gauche.size(), a.droite.size()));
  const double texte = 0.62 * 1.8 * static_cast<double>(longueurUtf8(marquage));
  const double w = grilleSup(std::max({5.08, nH > 0 ? (nH - 1) * PAS / 2 + 2.54 : 0.0, texte / 2 + 1.27}));
  const double h = grilleSup(std::max({3.81, nV > 0 ? (nV - 1) * PAS / 2 + 2.54 : 0.0}));
  auto rangee = [](std::size_t n, std::size_t k) { return (static_cast<double>(k) - (static_cast<double>(n) - 1) / 2) * PAS; };
  for (std::size_t k = 0; k < a.haut.size(); ++k) d.broches.push_back(borne(a.haut[k], rangee(a.haut.size(), k), -h - BROCHE, 1));
  for (std::size_t k = 0; k < a.bas.size(); ++k) d.broches.push_back(borne(a.bas[k], rangee(a.bas.size(), k), h + BROCHE, 3));
  for (std::size_t k = 0; k < a.gauche.size(); ++k) d.broches.push_back(borne(a.gauche[k], -w - BROCHE, rangee(a.gauche.size(), k), 0));
  for (std::size_t k = 0; k < a.droite.size(); ++k) d.broches.push_back(borne(a.droite[k], w + BROCHE, rangee(a.droite.size(), k), 2));
  Trace cadre;
  cadre.forme = Trace::Forme::Rectangle;
  cadre.points = {ptMm(-w, -h), ptMm(w, h)};
  d.traces.push_back(cadre);
  if (!marquage.empty()) {
    Trace t;
    t.forme = Trace::Forme::Texte;
    t.points = {ptMm(0, 0)};
    t.texte = marquage;
    t.taille = nm(1.8);
    d.traces.push_back(t);
  }
  // Repère et valeur là où aucune borne ne sort : à droite, sinon à gauche, sinon au-dessus du coin droit
  if (a.droite.empty()) {
    d.posReference = ptMm(w + 1.27, -1.6);
    d.posValeur = ptMm(w + 1.27, 1.6);
  } else if (a.gauche.empty()) {
    d.posReference = ptMm(-w - 1.27, -1.6);
    d.posValeur = ptMm(-w - 1.27, 1.6);
  } else {
    d.posReference = ptMm(w + 1.27, -h - 3.4);
    d.posValeur = ptMm(w + 1.27, -h - 1.0);
  }
  if (!marquage.empty()) d.proprietes["marquage"] = marquage;
  d.proprietes["montage"] = codeMontage(a.montage);
  if (a.largeur > 0) d.proprietes["largeur"] = detail::nombreFr(a.largeur);
  if (a.hauteur > 0) d.proprietes["hauteur"] = detail::nombreFr(a.hauteur);
  if (a.profondeur > 0) d.proprietes["profondeur"] = detail::nombreFr(a.profondeur);
  if (a.pertes >= 0) d.proprietes["pertes"] = detail::nombreFr(a.pertes);
  if (a.prix > 0) d.proprietes["prix"] = detail::nombreFr(std::round(a.prix * 100) / 100);
  return d;
}

SpecAppareil specAppareil(const DefSymbole& d) {
  SpecAppareil a;
  a.id = d.id;
  a.nom = d.nom;
  a.prefixe = d.prefixe;
  a.description = d.description;
  a.fabricant = d.fabricant;
  a.reference = d.mpn;
  if (const auto it = d.proprietes.find("marquage"); it != d.proprietes.end()) a.marquage = it->second;
  if (const auto it = d.proprietes.find("montage"); it != d.proprietes.end()) a.montage = montageDepuis(it->second);
  a.largeur = nombreDe(d, "largeur", 0);
  a.hauteur = nombreDe(d, "hauteur", 0);
  a.profondeur = nombreDe(d, "profondeur", 0);
  a.pertes = nombreDe(d, "pertes", -1);
  a.prix = nombreDe(d, "prix", 0);
  // Bornes : chaque côté dans l'ordre de lecture (de gauche à droite, de haut en bas)
  std::vector<const Broche*> v;
  for (const auto& b : d.broches) v.push_back(&b);
  std::stable_sort(v.begin(), v.end(), [](const Broche* x, const Broche* y) {
    const bool hx = x->direction == 1 || x->direction == 3;
    return hx ? x->extremite.x < y->extremite.x : x->extremite.y < y->extremite.y;
  });
  for (const Broche* b : v) {
    switch (b->direction) {
      case 1: a.haut.push_back(b->numero); break;
      case 3: a.bas.push_back(b->numero); break;
      case 0: a.gauche.push_back(b->numero); break;
      default: a.droite.push_back(b->numero); break;
    }
  }
  return a;
}

std::string identifiantAppareil(const ProjetElectrique& p, const std::string& nom, const std::vector<std::string>& autresPris) {
  const std::string base = PREFIXE_ID + slug(nom);
  auto pris = [&](const std::string& id) {
    return p.schema.symboles.count(id) > 0 || std::find(autresPris.begin(), autresPris.end(), id) != autresPris.end();
  };
  std::string id = base;
  for (int n = 2; pris(id); ++n) id = base + "-" + std::to_string(n);
  return id;
}

std::string enregistrerAppareil(ProjetElectrique& p, const SpecAppareil& a) {
  SpecAppareil b = a;
  if (b.id.empty()) b.id = identifiantAppareil(p, b.nom);
  DefSymbole d = symboleAppareil(b);
  const auto ancien = p.schema.symboles.find(d.id);
  if (ancien != p.schema.symboles.end()) {
    const DefSymbole avant = ancien->second;
    d.version = avant.version + 1;
    // Les appareils posés suivent : désignation, fabricant et référence encore ceux de l'ancienne définition, lettre
    // de repère changée (numéro suivant libre pour la nouvelle lettre)
    int suivant = 0;
    for (const auto& k : p.schema.composants) suivant = std::max(suivant, numeroApres(k.reference, d.prefixe));
    for (auto& k : p.schema.composants) {
      if (k.symbole != d.id) continue;
      if (k.description == avant.nom) k.description = d.nom;
      if (k.valeur == avant.nom) k.valeur = d.nom;
      if (k.fabricant == avant.fabricant) k.fabricant = d.fabricant;
      if (k.mpn == avant.mpn) k.mpn = d.mpn;
      if (d.prefixe != avant.prefixe && lettresRepere(k.reference) == avant.prefixe) k.reference = d.prefixe + std::to_string(++suivant);
    }
  }
  p.schema.symboles[d.id] = d;
  return d.id;
}

std::vector<const DefSymbole*> appareilsPersonnels(const ProjetElectrique& p) {
  std::vector<const DefSymbole*> v;
  for (const auto& [id, d] : p.schema.symboles)
    if (estAppareilPersonnel(id)) v.push_back(&d);
  std::sort(v.begin(), v.end(), [](const DefSymbole* x, const DefSymbole* y) { return x->nom != y->nom ? x->nom < y->nom : x->id < y->id; });
  return v;
}

}  // namespace prom::elec

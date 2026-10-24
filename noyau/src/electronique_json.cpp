// Prométhée : enregistrement de la conception électronique dans le projet (JSON). Coordonnées en nanomètres
// entiers : la relecture redonne exactement la même géométrie. Lecture tolérante. Licence GPL-3.0-only.
#include "promethee/electronique.hpp"
#include "promethee/valeurs.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/articles.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "promethee/migrations.hpp"
#include "promethee/robustesse.hpp"

namespace prom::ecad {

using J = nlohmann::ordered_json;

namespace {
J pt(Pt p) { return J::array({p.x, p.y}); }
// Coordonnées bornées à ±10 m (EL-34) : au-delà, un fichier abîmé ferait déborder les calculs en entiers
constexpr double BORNE_COORDONNEE = 1e10;
// Compte rendu d'une relecture : ce qui a été ignoré ou ramené dans les bornes, pour l'afficher à l'ouverture
struct CompteLecture {
  std::map<std::string, int> ignores;
  int bornes = 0;
};
thread_local CompteLecture* g_compte = nullptr;
Nm coordonnee(double v) {
  if (!std::isfinite(v)) { if (g_compte) ++g_compte->bornes; return 0; }
  if (std::abs(v) > BORNE_COORDONNEE) {
    if (g_compte) ++g_compte->bornes;
    v = std::copysign(BORNE_COORDONNEE, v);
  }
  return static_cast<Nm>(std::llround(v));
}
Pt lirePt(const J& j) {
  if (!j.is_array() || j.size() < 2 || !j[0].is_number() || !j[1].is_number()) return {};
  return {coordonnee(j[0].get<double>()), coordonnee(j[1].get<double>())};
}
// Champ d'un objet, ou null : jamais d'exception sur une valeur qui n'est pas un objet (EL-07)
const J& champ(const J& o, const char* k) {
  static const J rien;
  if (!o.is_object()) return rien;
  const auto it = o.find(k);
  return it == o.end() ? rien : *it;
}
// Élément d'une liste qui n'est pas un objet : ignoré et compté
bool objet(const J& y, const char* quoi) {
  if (y.is_object()) return true;
  if (g_compte) ++g_compte->ignores[quoi];
  return false;
}
std::string txt(const J& o, const char* k, const std::string& defaut = {}) { return o.is_object() && o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : defaut; }
double num(const J& o, const char* k, double defaut = 0) {
  if (!(o.is_object() && o.contains(k) && o[k].is_number())) return defaut;
  const double v = o[k].get<double>();
  return std::isfinite(v) ? v : defaut;
}
Nm entier(const J& o, const char* k, Nm defaut = 0) { return o.is_object() && o.contains(k) && o[k].is_number() ? coordonnee(o[k].get<double>()) : defaut; }
int ent(const J& o, const char* k, int defaut = 0) {
  if (!(o.is_object() && o.contains(k) && o[k].is_number())) return defaut;
  const double v = o[k].get<double>();
  return std::isfinite(v) && std::abs(v) < 2e9 ? static_cast<int>(std::lround(v)) : defaut;
}
bool vrai(const J& o, const char* k, bool defaut = false) { return o.is_object() && o.contains(k) && o[k].is_boolean() ? o[k].get<bool>() : defaut; }
const J& tableau(const J& o, const char* k) {
  static const J vide = J::array();
  return o.is_object() && o.contains(k) && o[k].is_array() ? o[k] : vide;
}
J points(const Polygone& p) {
  J a = J::array();
  for (const Pt& q : p) a.push_back(pt(q));
  return a;
}
Polygone lirePoints(const J& a);
J volumesVersJson(const std::vector<VolumeLibre>& v) {
  J r = J::array();
  for (const auto& x : v) r.push_back({{"contour", points(x.contour)}, {"z0", x.z0}, {"z1", x.z1}, {"couleur", x.couleur}});
  return r;
}
std::vector<VolumeLibre> volumesDepuisJson(const J& a) {
  std::vector<VolumeLibre> r;
  if (!a.is_array()) return r;
  for (const auto& y : a) {
    if (!y.is_object()) continue;
    VolumeLibre v;
    v.contour = lirePoints(champ(y, "contour"));
    v.z0 = std::max<Nm>(entier(y, "z0"), 0);
    v.z1 = std::max<Nm>(entier(y, "z1"), v.z0);
    v.couleur = txt(y, "couleur", v.couleur);
    if (v.contour.size() >= 3 && v.z1 > v.z0) r.push_back(v);
  }
  return r;
}
Polygone lirePoints(const J& a) {
  Polygone p;
  if (a.is_array())
    for (const auto& q : a) p.push_back(lirePt(q));
  return p;
}
Couche couche(const J& o, const char* k, Couche defaut) {
  const auto c = coucheDepuisCode(txt(o, k));
  return c ? *c : defaut;
}
// Clés inconnues (ECH-11, A13) : à la lecture, ce qu'un objet ne sait pas lire est gardé sous son identité ; à
// l'écriture, ces clés reviennent après les siennes, sans jamais remplacer une clé connue
using Connues = std::initializer_list<const char*>;
void garderInconnues(ConceptionElec& c, const std::string& cle, const J& o, Connues connues) {
  if (!o.is_object()) return;
  J reste = J::object();
  for (auto it = o.begin(); it != o.end(); ++it)
    if (std::none_of(connues.begin(), connues.end(), [&](const char* k) { return it.key() == k; })) reste[it.key()] = it.value();
  if (!reste.empty()) c.clesInconnues[cle] = std::move(reste);
}
// Clés connues de la 0.33 et clés ajoutées par la 0.34
template <std::size_t N>
void garderInconnues(ConceptionElec& c, const std::string& cle, const J& o, Connues connues, const char* const (&enPlus)[N]) {
  if (!o.is_object()) return;
  J reste = J::object();
  for (auto it = o.begin(); it != o.end(); ++it)
    if (std::none_of(connues.begin(), connues.end(), [&](const char* k) { return it.key() == k; }) && std::none_of(std::begin(enPlus), std::end(enPlus), [&](const char* k) { return it.key() == k; }))
      reste[it.key()] = it.value();
  if (!reste.empty()) c.clesInconnues[cle] = std::move(reste);
}
void rendreInconnues(const ConceptionElec& c, const std::string& cle, J& o) {
  const auto it = c.clesInconnues.find(cle);
  if (it == c.clesInconnues.end() || !o.is_object()) return;
  for (auto k = it->second.begin(); k != it->second.end(); ++k)
    if (!o.contains(k.key())) o[k.key()] = k.value();
}
// Liste écrite dans un ordre stable : par identifiant, quel que soit l'ordre des modifications
template <class T, class Cle>
std::vector<const T*> trie(const std::vector<T>& v, Cle cle) {
  std::vector<const T*> r;
  r.reserve(v.size());
  for (const auto& x : v) r.push_back(&x);
  std::stable_sort(r.begin(), r.end(), [&](const T* a, const T* b) { return cle(*a) < cle(*b); });
  return r;
}
template <class T, class Cle>
void trierEnPlace(std::vector<T>& v, Cle cle) {
  std::stable_sort(v.begin(), v.end(), [&](const T& a, const T& b) { return cle(a) < cle(b); });
}
}  // namespace

J symboleVersJson(const DefSymbole& d) {
  J o = {{"id", d.id}, {"nom", d.nom}, {"description", d.description}, {"categorie", d.categorie}, {"prefixe", d.prefixe}, {"empreinte", d.empreinte}, {"version", d.version},
         {"posRef", pt(d.posReference)}, {"posVal", pt(d.posValeur)}};
  for (const auto& [k, v] : {std::pair{"motsCles", &d.motsCles}, std::pair{"fabricant", &d.fabricant}, std::pair{"mpn", &d.mpn}, std::pair{"fiche", &d.fiche}})
    if (!v->empty()) o[k] = *v;
  if (d.alimentation) o["alimentation"] = true;
  if (d.drapeau) o["drapeau"] = true;
  if (d.mecanique) o["mecanique"] = true;
  if (d.traversant) o["traversant"] = true;
  if (d.liaisonReseaux) o["liaisonReseaux"] = true;
  if (d.genre != GenreComposant::Standard) o["genre"] = codeGenreComposant(d.genre);
  if (d.unites > 1) o["unites"] = d.unites;
  if (!d.nomsUnites.empty()) o["nomsUnites"] = d.nomsUnites;
  if (!d.proprietes.empty()) {
    J pr = J::object();
    for (const auto& [k, v] : d.proprietes) pr[k] = v;
    o["proprietes"] = pr;
  }
  if (!d.parent.empty()) o["parent"] = d.parent;
  if (!d.style.empty()) o["style"] = d.style;
  if (d.vues > 1) o["vues"] = d.vues;
  if (!d.brochesReliees.empty()) o["brochesReliees"] = d.brochesReliees;
  J b = J::array();
  for (const auto& x : d.broches) {
    J y = {{"numero", x.numero}, {"nom", x.nom}, {"type", codeTypeBroche(x.type)}, {"p", pt(x.extremite)}, {"dir", x.direction}, {"l", x.longueur}, {"nomVisible", x.nomVisible}, {"numeroVisible", x.numeroVisible}};
    if (x.unite) y["unite"] = x.unite;
    if (!x.style.empty()) y["style"] = x.style;
    if (!x.section.empty()) y["section"] = x.section;
    if (!x.groupe.empty()) y["groupe"] = x.groupe;
    if (x.rang) y["rang"] = x.rang;
    if (!x.alternatives.empty()) {
      J a = J::array();
      for (const auto& f : x.alternatives) a.push_back({{"nom", f.nom}, {"type", codeTypeBroche(f.type)}});
      y["alternatives"] = a;
    }
    if (x.cachee) y["cachee"] = true;
    if (x.vue) y["vue"] = x.vue;
    b.push_back(y);
  }
  o["broches"] = b;
  J t = J::array();
  for (const auto& x : d.traces) {
    J y = {{"forme", static_cast<int>(x.forme)}, {"points", points(x.points)}};
    if (static_cast<int>(x.forme) > static_cast<int>(Trace::Forme::Texte)) {
      // La 0.33 dessine la polyligne du contour ; la vraie forme suit
      y["forme034"] = static_cast<int>(x.forme);
      y["points034"] = y["points"];
      y["forme"] = static_cast<int>(Trace::Forme::Polyligne);
      y["points"] = points(contourTrace(x));
    }
    if (x.rayonY) y["rayonY"] = x.rayonY;
    if (x.arrondi) y["arrondi"] = x.arrondi;
    if (x.epaisseur) y["epaisseur"] = x.epaisseur;
    if (!x.style.empty()) y["style"] = x.style;
    if (!x.couleur.empty()) y["couleur"] = x.couleur;
    if (x.fleches) y["fleches"] = x.fleches;
    if (x.vue) y["vue"] = x.vue;
    if (x.rayon) y["rayon"] = x.rayon;
    if (x.debut != 0 || x.fin != 0) { y["debut"] = x.debut; y["fin"] = x.fin; }
    if (x.rempli) y["rempli"] = true;
    if (x.ferme) y["ferme"] = true;
    if (x.fond) y["fond"] = true;
    if (!x.texte.empty()) { y["texte"] = x.texte; y["taille"] = x.taille; }
    if (x.unite) y["unite"] = x.unite;
    t.push_back(y);
  }
  o["traces"] = t;
  return o;
}

DefSymbole symboleDepuisJson(const J& o) {
  DefSymbole d;
  d.id = txt(o, "id");
  d.nom = txt(o, "nom", d.id);
  d.description = txt(o, "description");
  d.categorie = txt(o, "categorie");
  d.prefixe = txt(o, "prefixe", "U");
  d.empreinte = txt(o, "empreinte");
  d.version = ent(o, "version", 1);
  d.motsCles = txt(o, "motsCles");
  d.fabricant = txt(o, "fabricant");
  d.mpn = txt(o, "mpn");
  d.fiche = txt(o, "fiche");
  d.alimentation = vrai(o, "alimentation");
  d.drapeau = vrai(o, "drapeau");
  d.mecanique = vrai(o, "mecanique");
  d.traversant = vrai(o, "traversant");
  d.liaisonReseaux = vrai(o, "liaisonReseaux");
  d.genre = genreComposantDepuisCode(txt(o, "genre"));
  for (const auto& n : tableau(o, "nomsUnites"))
    if (n.is_string()) d.nomsUnites.push_back(n.get<std::string>());
  if (o.contains("posRef")) d.posReference = lirePt(o["posRef"]);
  if (o.contains("posVal")) d.posValeur = lirePt(o["posVal"]);
  d.unites = std::clamp(ent(o, "unites", 1), 1, 64);
  d.parent = txt(o, "parent");
  d.style = txt(o, "style");
  d.vues = std::clamp(ent(o, "vues", 1), 1, 2);
  for (const auto& g : tableau(o, "brochesReliees")) {
    std::vector<std::string> groupe;
    if (g.is_array())
      for (const auto& n : g)
        if (n.is_string()) groupe.push_back(n.get<std::string>());
    if (groupe.size() >= 2) d.brochesReliees.push_back(groupe);
  }
  if (const auto it = o.find("proprietes"); it != o.end() && it->is_object())
    for (const auto& [k, v] : it->items())
      if (v.is_string()) d.proprietes[k] = v.get<std::string>();
  for (const auto& x : tableau(o, "broches")) {
    Broche b;
    b.numero = txt(x, "numero");
    b.nom = txt(x, "nom");
    b.type = typeBrocheDepuis(txt(x, "type", "passive"));
    b.extremite = lirePt(champ(x, "p"));
    b.direction = std::clamp(ent(x, "dir"), 0, 3);
    b.longueur = entier(x, "l", 2'540'000);
    b.nomVisible = vrai(x, "nomVisible", true);
    b.numeroVisible = vrai(x, "numeroVisible", true);
    b.unite = std::clamp(ent(x, "unite", 0), 0, d.unites);
    b.style = txt(x, "style");
    b.section = txt(x, "section");
    b.groupe = txt(x, "groupe");
    b.rang = ent(x, "rang");
    for (const auto& f : tableau(x, "alternatives"))
      if (f.is_object() && !txt(f, "nom").empty()) b.alternatives.push_back({txt(f, "nom"), typeBrocheDepuis(txt(f, "type", "bidirectionnelle"))});
    b.cachee = vrai(x, "cachee");
    b.vue = std::clamp(ent(x, "vue"), 0, 2);
    d.broches.push_back(b);
  }
  for (const auto& x : tableau(o, "traces")) {
    Trace t;
    t.forme = static_cast<Trace::Forme>(std::clamp(ent(x, "forme"), 0, 4));
    t.points = lirePoints(champ(x, "points"));
    if (x.contains("forme034")) {
      t.forme = static_cast<Trace::Forme>(std::clamp(ent(x, "forme034"), 0, static_cast<int>(Trace::Forme::CadreTexte)));
      t.points = lirePoints(champ(x, "points034"));
    }
    t.rayonY = std::max<Nm>(entier(x, "rayonY"), 0);
    t.arrondi = std::max<Nm>(entier(x, "arrondi"), 0);
    t.epaisseur = std::max<Nm>(entier(x, "epaisseur"), 0);
    t.style = txt(x, "style");
    t.couleur = txt(x, "couleur");
    t.fleches = std::clamp(ent(x, "fleches"), 0, 3);
    t.vue = std::clamp(ent(x, "vue"), 0, 2);
    t.rayon = entier(x, "rayon");
    t.debut = num(x, "debut");
    t.fin = num(x, "fin");
    t.rempli = vrai(x, "rempli");
    t.ferme = vrai(x, "ferme");
    t.fond = vrai(x, "fond");
    t.texte = txt(x, "texte");
    t.taille = entier(x, "taille", 1'270'000);
    t.unite = std::clamp(ent(x, "unite", 0), 0, d.unites);
    d.traces.push_back(t);
  }
  return d;
}

J contraintesVersJson(const ContraintesCarte& a);
ContraintesCarte contraintesDepuisJson(const J& o);
namespace {
J reglesVersJson(const Regles& r, const ConceptionElec& ce) {
  J o = {{"isolement", r.isolement}, {"largeur", r.largeur}, {"largeurMin", r.largeurMin}, {"viaDiametre", r.viaDiametre}, {"viaPercage", r.viaPercage},
         {"anneauMin", r.anneauMin}, {"percageMin", r.percageMin}, {"bord", r.bord}, {"ecartTrous", r.ecartTrous}, {"cuivreTrou", r.cuivreTrou}, {"rayonsMin", r.rayonsMin}, {"profil", r.profil}};
  J cl = J::array();
  for (const auto& c : r.classes) {
    J x = {{"nom", c.nom}, {"isolement", c.isolement}, {"largeur", c.largeur}, {"viaDiametre", c.viaDiametre}, {"viaPercage", c.viaPercage}};
    if (c.courant > 0) {
      x["courant"] = c.courant;
      x["echauffement"] = c.echauffement;
    }
    if (c.tension > 0) x["tension"] = c.tension;
    if (!c.couleur.empty()) x["couleur"] = c.couleur;
    rendreInconnues(ce, "classe:" + c.nom, x);
    cl.push_back(x);
  }
  o["classes"] = cl;
  // Affectations dans l'ordre saisi : l'objet JSON garde l'ordre de ses clés, qui est celui des essais des motifs
  // (R5) ; la 0.28, qui les relisait dans l'ordre alphabétique, relit le même fichier sans perte
  J af = J::object();
  for (const auto& [reseau, classe] : r.classeReseau) af[reseau] = classe;
  o["affectations"] = af;
  if (r.capacitesPerso) {
    o["capacitesPerso"] = {{"isolement", r.capacitesPerso->isolement}, {"largeurMin", r.capacitesPerso->largeurMin}, {"percageMin", r.capacitesPerso->percageMin},
                           {"anneauMin", r.capacitesPerso->anneauMin}, {"bord", r.capacitesPerso->bord}};
    rendreInconnues(ce, "circuit/regles/capacitesPerso", o["capacitesPerso"]);
  }
  if (!r.avancees.vide()) o["avancees"] = contraintesVersJson(r.avancees);
  rendreInconnues(ce, "circuit/regles", o);
  return o;
}

Regles reglesDepuisJson(const J& o, ConceptionElec& ce) {
  garderInconnues(ce, "circuit/regles", o,
                  {"isolement", "largeur", "largeurMin", "viaDiametre", "viaPercage", "anneauMin", "percageMin", "bord", "ecartTrous", "cuivreTrou", "rayonsMin", "profil", "classes",
                   "affectations", "capacitesPerso", "avancees"});
  Regles r;
  r.isolement = std::max<Nm>(entier(o, "isolement", r.isolement), 10'000);
  r.largeur = std::max<Nm>(entier(o, "largeur", r.largeur), 10'000);
  r.largeurMin = std::max<Nm>(entier(o, "largeurMin", r.largeurMin), 10'000);
  r.viaDiametre = std::max<Nm>(entier(o, "viaDiametre", r.viaDiametre), 50'000);
  r.viaPercage = std::max<Nm>(entier(o, "viaPercage", r.viaPercage), 50'000);
  r.anneauMin = std::max<Nm>(entier(o, "anneauMin", r.anneauMin), 0);
  r.percageMin = std::max<Nm>(entier(o, "percageMin", r.percageMin), 50'000);
  r.bord = std::max<Nm>(entier(o, "bord", r.bord), 0);
  r.ecartTrous = std::max<Nm>(entier(o, "ecartTrous", r.ecartTrous), 0);
  r.cuivreTrou = std::max<Nm>(entier(o, "cuivreTrou", r.cuivreTrou), 0);
  r.rayonsMin = std::clamp(ent(o, "rayonsMin", r.rayonsMin), 0, 4);
  r.profil = txt(o, "profil", r.profil);
  for (const auto& x : tableau(o, "classes")) {
    if (!objet(x, "classe de réseaux")) continue;
    ClasseReseau c;
    c.nom = txt(x, "nom");
    c.isolement = entier(x, "isolement", c.isolement);
    c.largeur = entier(x, "largeur", c.largeur);
    c.viaDiametre = entier(x, "viaDiametre", c.viaDiametre);
    c.viaPercage = entier(x, "viaPercage", c.viaPercage);
    if (x.contains("courant") && x["courant"].is_number() && x["courant"].get<double>() > 0) c.courant = std::min(x["courant"].get<double>(), 500.0);
    if (x.contains("echauffement") && x["echauffement"].is_number() && x["echauffement"].get<double>() > 0) c.echauffement = std::min(x["echauffement"].get<double>(), 200.0);
    c.tension = std::clamp(num(x, "tension"), 0.0, 100'000.0);
    c.couleur = txt(x, "couleur");
    if (c.nom.empty()) continue;
    garderInconnues(ce, "classe:" + c.nom, x, {"nom", "isolement", "largeur", "viaDiametre", "viaPercage", "courant", "echauffement", "tension", "couleur"});
    r.classes.push_back(c);
  }
  // Relues dans l'ordre du fichier (R5) : un projet de la 0.28 les avait écrites dans l'ordre alphabétique, celui où
  // elle les essayait ; leur sens ne change donc pas
  if (o.is_object() && o.contains("affectations") && o["affectations"].is_object())
    for (auto it = o["affectations"].begin(); it != o["affectations"].end(); ++it)
      if (it.value().is_string()) r.classeReseau[it.key()] = it.value().get<std::string>();
  if (const J& cp = champ(o, "capacitesPerso"); cp.is_object()) {
    garderInconnues(ce, "circuit/regles/capacitesPerso", cp, {"isolement", "largeurMin", "percageMin", "anneauMin", "bord"});
    CapacitesFabricant x;
    x.isolement = std::max<Nm>(entier(cp, "isolement"), 0);
    x.largeurMin = std::max<Nm>(entier(cp, "largeurMin"), 0);
    x.percageMin = std::max<Nm>(entier(cp, "percageMin"), 0);
    x.anneauMin = std::max<Nm>(entier(cp, "anneauMin"), 0);
    x.bord = std::max<Nm>(entier(cp, "bord"), 0);
    r.capacitesPerso = x;
  }
  if (const J& av = champ(o, "avancees"); av.is_object()) r.avancees = contraintesDepuisJson(av);
  return r;
}
}  // namespace

namespace {
J couchesVersJson(Couches c) {
  J a = J::array();
  for (int i = 0; i < NB_COUCHES; ++i)
    if (c & bit(static_cast<Couche>(i))) a.push_back(codeCouche(static_cast<Couche>(i)));
  return a;
}
Couches couchesDepuisJson(const J& a) {
  Couches c = 0;
  if (a.is_array())
    for (const auto& x : a)
      if (x.is_string())
        if (const auto k = coucheDepuisCode(x.get<std::string>())) c |= bit(*k);
  return c;
}
}  // namespace

// ---------- 0.34 : champs nouveaux des objets du circuit et du schéma ----------
// Chaque champ n'est écrit que s'il n'a pas sa valeur par défaut : un fichier sans eux reste celui de la 0.33, et la
// 0.33 garde ceux qu'elle ne connaît pas (clés inconnues). Une forme nouvelle s'écrit aussi sous la forme la plus
// proche qu'une version plus ancienne sache dessiner (« forme034 » porte la vraie).
namespace {
const char* codeVerrou(Verrou v) { return v == Verrou::Fige ? "fige" : v == Verrou::Protege ? "protege" : ""; }
Verrou lireVerrou(const J& o) {
  const std::string v = txt(o, "verrou");
  return v == "fige" ? Verrou::Fige : v == "protege" ? Verrou::Protege : Verrou::Aucun;
}
const char* codeConnexion(ConnexionZone c) { return c == ConnexionZone::Thermique ? "thermique" : c == ConnexionZone::Pleine ? "pleine" : c == ConnexionZone::Aucune ? "aucune" : ""; }
ConnexionZone lireConnexion(const std::string& v) { return v == "thermique" ? ConnexionZone::Thermique : v == "pleine" ? ConnexionZone::Pleine : v == "aucune" ? ConnexionZone::Aucune : ConnexionZone::Heritee; }
J lamageJson(const Lamage& l) {
  J y = {{"d", l.diametre}, {"p", l.profondeur}};
  if (l.angle != 0) y["angle"] = l.angle;
  return y;
}
std::optional<Lamage> lireLamage(const J& o) {
  if (!o.is_object()) return std::nullopt;
  Lamage l;
  l.diametre = std::max<Nm>(entier(o, "d"), 0);
  l.profondeur = std::max<Nm>(entier(o, "p"), 0);
  l.angle = std::clamp(num(o, "angle"), 0.0, 180.0);
  if (l.diametre <= 0) return std::nullopt;
  return l;
}
constexpr const char* CLES_PASTILLE_034[] = {"forme034", "chanfrein", "coins", "trapeze", "libre", "fenteL", "decPercage", "tolPercage", "trous", "margeVernis", "margePate",
                                       "motifPate", "vernisLibre", "pateLibre", "gabarit", "connexion", "lamageDessus", "lamageDessous"};
void ecrirePastille034(const Pastille& p, J& y) {
  if (static_cast<int>(p.forme) > static_cast<int>(FormePastille::RectArrondi)) {
    // La 0.33 dessine un rectangle à la place ; la vraie forme suit
    y["forme"] = static_cast<int>(FormePastille::Rect);
    y["forme034"] = static_cast<int>(p.forme);
  }
  if (p.forme == FormePastille::Chanfreine) {
    y["chanfrein"] = p.chanfrein;
    if (p.coinsChanfrein != 0xF) y["coins"] = p.coinsChanfrein;
  }
  if (p.forme == FormePastille::Trapeze) y["trapeze"] = p.trapeze;
  if (p.forme == FormePastille::Libre && !p.contourLibre.empty()) y["libre"] = points(p.contourLibre);
  if (p.longueurPercage > 0) y["fenteL"] = p.longueurPercage;
  if (p.decalagePercage != Pt{}) y["decPercage"] = pt(p.decalagePercage);
  if (p.tolerancePercage > 0) y["tolPercage"] = p.tolerancePercage;
  if (!p.trousEnPlus.empty()) {
    J a = J::array();
    for (const auto& t : p.trousEnPlus) {
      J z = {{"p", pt(t.decalage)}, {"d", t.diametre}};
      if (t.longueur > 0) z["l"] = t.longueur;
      if (t.rotation != 0) z["rot"] = t.rotation;
      a.push_back(z);
    }
    y["trous"] = a;
  }
  if (p.margeVernis) y["margeVernis"] = *p.margeVernis;
  if (p.margePate) y["margePate"] = *p.margePate;
  if (p.motifPate) y["motifPate"] = {{"lignes", p.motifPate->lignes}, {"colonnes", p.motifPate->colonnes}, {"ecart", p.motifPate->ecart}, {"rayon", p.motifPate->rayon}, {"couverture", p.motifPate->couverture}};
  if (!p.vernisLibre.empty()) y["vernisLibre"] = points(p.vernisLibre);
  if (!p.pateLibre.empty()) y["pateLibre"] = points(p.pateLibre);
  if (!p.gabarit.empty()) y["gabarit"] = p.gabarit;
  if (p.connexion != ConnexionZone::Heritee) y["connexion"] = codeConnexion(p.connexion);
  if (p.lamageDessus) y["lamageDessus"] = lamageJson(*p.lamageDessus);
  if (p.lamageDessous) y["lamageDessous"] = lamageJson(*p.lamageDessous);
}
void lirePastille034(const J& y, Pastille& p) {
  if (y.contains("forme034")) p.forme = static_cast<FormePastille>(std::clamp(ent(y, "forme034"), 0, static_cast<int>(FormePastille::Libre)));
  p.chanfrein = std::clamp(num(y, "chanfrein", 0.2), 0.0, 0.5);
  p.coinsChanfrein = std::clamp(ent(y, "coins", 0xF), 0, 0xF);
  p.trapeze = entier(y, "trapeze");
  p.contourLibre = lirePoints(champ(y, "libre"));
  p.longueurPercage = std::max<Nm>(entier(y, "fenteL"), 0);
  p.decalagePercage = lirePt(champ(y, "decPercage"));
  p.tolerancePercage = std::max<Nm>(entier(y, "tolPercage"), 0);
  for (const auto& z : tableau(y, "trous")) {
    if (!z.is_object()) continue;
    TrouPastille t;
    t.decalage = lirePt(champ(z, "p"));
    t.diametre = std::max<Nm>(entier(z, "d"), 0);
    t.longueur = std::max<Nm>(entier(z, "l"), 0);
    t.rotation = num(z, "rot");
    if (t.diametre > 0) p.trousEnPlus.push_back(t);
  }
  if (y.contains("margeVernis")) p.margeVernis = std::clamp<Nm>(entier(y, "margeVernis"), -1'000'000, 1'000'000);
  if (y.contains("margePate")) p.margePate = std::clamp<Nm>(entier(y, "margePate"), -1'000'000, 1'000'000);
  if (const J& m = champ(y, "motifPate"); m.is_object()) {
    MotifPate x;
    x.lignes = std::clamp(ent(m, "lignes", 2), 1, 20);
    x.colonnes = std::clamp(ent(m, "colonnes", 2), 1, 20);
    x.ecart = std::max<Nm>(entier(m, "ecart", x.ecart), 0);
    x.rayon = std::max<Nm>(entier(m, "rayon"), 0);
    x.couverture = std::clamp(num(m, "couverture", 0.6), 0.05, 1.0);
    p.motifPate = x;
  }
  p.vernisLibre = lirePoints(champ(y, "vernisLibre"));
  p.pateLibre = lirePoints(champ(y, "pateLibre"));
  p.gabarit = txt(y, "gabarit");
  p.connexion = lireConnexion(txt(y, "connexion"));
  p.lamageDessus = lireLamage(champ(y, "lamageDessus"));
  p.lamageDessous = lireLamage(champ(y, "lamageDessous"));
}
J gabaritJson(const GabaritPastille& g) {
  J y = {{"nom", g.nom}, {"forme", static_cast<int>(g.forme)}, {"type", static_cast<int>(g.type)}, {"l", g.largeur}, {"h", g.hauteur}};
  if (g.percage) y["percage"] = g.percage;
  if (g.longueurPercage) y["fenteL"] = g.longueurPercage;
  if (g.forme == FormePastille::RectArrondi) y["arrondi"] = g.arrondi;
  if (g.forme == FormePastille::Chanfreine) y["chanfrein"] = g.chanfrein;
  if (g.connexion != ConnexionZone::Heritee) y["connexion"] = codeConnexion(g.connexion);
  y["margeVernis"] = g.margeVernis;
  if (g.margePate) y["margePate"] = g.margePate;
  if (g.tauxPate != 1.0) y["tauxPate"] = g.tauxPate;
  return y;
}
GabaritPastille lireGabarit(const J& y) {
  GabaritPastille g;
  g.nom = txt(y, "nom");
  g.forme = static_cast<FormePastille>(std::clamp(ent(y, "forme", 3), 0, static_cast<int>(FormePastille::Libre)));
  g.type = static_cast<TypePastille>(std::clamp(ent(y, "type"), 0, 2));
  g.largeur = std::max<Nm>(entier(y, "l"), 1'000);
  g.hauteur = std::max<Nm>(entier(y, "h"), 1'000);
  g.percage = std::max<Nm>(entier(y, "percage"), 0);
  g.longueurPercage = std::max<Nm>(entier(y, "fenteL"), 0);
  g.arrondi = std::clamp(num(y, "arrondi", 0.25), 0.0, 0.5);
  g.chanfrein = std::clamp(num(y, "chanfrein", 0.2), 0.0, 0.5);
  g.connexion = lireConnexion(txt(y, "connexion"));
  g.margeVernis = entier(y, "margeVernis", g.margeVernis);
  g.margePate = entier(y, "margePate");
  g.tauxPate = std::clamp(num(y, "tauxPate", 1.0), 0.05, 1.0);
  return g;
}
constexpr const char* CLES_VIA_034[] = {"de", "vers", "typeVia", "laser", "remplissage", "dInterne", "dDessous", "retirerCouronnes", "contrePercage", "verrou", "groupe"};
void ecrireVia034(const Via& v, J& y) {
  if (v.type != TypeVia::Traversante) {
    static const char* types[] = {"traversante", "borgne", "enterree", "micro"};
    y["typeVia"] = types[static_cast<int>(v.type)];
    y["de"] = codeCouche(v.de);
    y["vers"] = codeCouche(v.vers);
  }
  if (v.laser) y["laser"] = true;
  if (!v.remplissage.empty()) y["remplissage"] = v.remplissage;
  if (v.diametreInterne) y["dInterne"] = v.diametreInterne;
  if (v.diametreDessous) y["dDessous"] = v.diametreDessous;
  if (v.retirerCouronnes) y["retirerCouronnes"] = true;
  if (v.contrePercage)
    y["contrePercage"] = {{"dessous", v.contrePercage->dessous}, {"jusqua", codeCouche(v.contrePercage->jusqua)}, {"d", v.contrePercage->diametre}, {"talon", v.contrePercage->talonMax}};
  if (v.verrou != Verrou::Aucun) y["verrou"] = codeVerrou(v.verrou);
  if (!v.groupe.empty()) y["groupe"] = v.groupe;
}
void lireVia034(const J& y, Via& v) {
  const std::string t = txt(y, "typeVia");
  v.type = t == "borgne" ? TypeVia::Borgne : t == "enterree" ? TypeVia::Enterree : t == "micro" ? TypeVia::Micro : TypeVia::Traversante;
  v.de = couche(y, "de", Couche::CuivreDessus);
  v.vers = couche(y, "vers", Couche::CuivreDessous);
  if (!estCuivre(v.de)) v.de = Couche::CuivreDessus;
  if (!estCuivre(v.vers)) v.vers = Couche::CuivreDessous;
  v.laser = vrai(y, "laser");
  v.remplissage = txt(y, "remplissage");
  v.diametreInterne = std::max<Nm>(entier(y, "dInterne"), 0);
  v.diametreDessous = std::max<Nm>(entier(y, "dDessous"), 0);
  v.retirerCouronnes = vrai(y, "retirerCouronnes");
  if (const J& c = champ(y, "contrePercage"); c.is_object()) {
    ContrePercage x;
    x.dessous = vrai(c, "dessous", true);
    x.jusqua = couche(c, "jusqua", Couche::CuivreInt1);
    x.diametre = std::max<Nm>(entier(c, "d"), 0);
    x.talonMax = std::max<Nm>(entier(c, "talon", x.talonMax), 0);
    v.contrePercage = x;
  }
  v.verrou = lireVerrou(y);
  v.groupe = txt(y, "groupe");
}
constexpr const char* CLES_PISTE_034[] = {"milieu", "largeurFin", "boutsCarres", "verrou", "motif"};
void ecrirePiste034(const Piste& p, J& y) {
  if (p.milieu) y["milieu"] = pt(*p.milieu);
  if (p.largeurFin > 0) y["largeurFin"] = p.largeurFin;
  if (p.boutsCarres) y["boutsCarres"] = true;
  if (p.verrou != Verrou::Aucun) y["verrou"] = codeVerrou(p.verrou);
  if (!p.motif.empty()) y["motif"] = p.motif;
}
void lirePiste034(const J& y, Piste& p) {
  if (y.contains("milieu")) p.milieu = lirePt(champ(y, "milieu"));
  p.largeurFin = std::max<Nm>(entier(y, "largeurFin"), 0);
  p.boutsCarres = vrai(y, "boutsCarres");
  p.verrou = lireVerrou(y);
  p.motif = txt(y, "motif");
}
J polygonesJson(const Polygones& v) {
  J a = J::array();
  for (const auto& p : v) a.push_back(points(p));
  return a;
}
Polygones lirePolygones(const J& a) {
  Polygones r;
  if (a.is_array())
    for (const auto& x : a)
      if (Polygone p = lirePoints(x); p.size() >= 3) r.push_back(std::move(p));
  return r;
}
constexpr const char* CLES_ZONE_034[] = {"nom", "hachure", "thermiquesTraversants", "aireIlotMin", "relierIlots", "verrou", "etat", "fige", "signature", "partition", "garde", "pasCouture"};
void ecrireZone034(const Zone& z, J& y) {
  if (!z.nom.empty()) y["nom"] = z.nom;
  if (z.remplissage == RemplissageZone::Hachure) y["hachure"] = {{"largeur", z.hachureLargeur}, {"ecart", z.hachureEcart}, {"angle", z.hachureAngle}};
  if (z.thermiquesTraversantsSeuls) y["thermiquesTraversants"] = true;
  if (z.aireIlotMin > 0) y["aireIlotMin"] = z.aireIlotMin;
  if (z.relierIlotsParVias) y["relierIlots"] = true;
  if (z.verrou != Verrou::Aucun) y["verrou"] = codeVerrou(z.verrou);
  if (z.etat != EtatZone::Normale) y["etat"] = z.etat == EtatZone::Figee ? "figee" : "miseDeCote";
  if (!z.remplissageFige.empty()) y["fige"] = polygonesJson(z.remplissageFige);
  if (!z.signature.empty()) y["signature"] = z.signature;
  if (!z.partition.empty()) y["partition"] = z.partition;
  if (!z.garde.empty()) y["garde"] = z.garde;
  if (z.pasCouture > 0) y["pasCouture"] = z.pasCouture;
}
void lireZone034(const J& y, Zone& z) {
  z.nom = txt(y, "nom");
  if (const J& h = champ(y, "hachure"); h.is_object()) {
    z.remplissage = RemplissageZone::Hachure;
    z.hachureLargeur = std::max<Nm>(entier(h, "largeur", z.hachureLargeur), 50'000);
    z.hachureEcart = std::max<Nm>(entier(h, "ecart", z.hachureEcart), 50'000);
    z.hachureAngle = num(h, "angle", 45);
  }
  z.thermiquesTraversantsSeuls = vrai(y, "thermiquesTraversants");
  z.aireIlotMin = std::max(0.0, num(y, "aireIlotMin"));
  z.relierIlotsParVias = vrai(y, "relierIlots");
  z.verrou = lireVerrou(y);
  const std::string e = txt(y, "etat");
  z.etat = e == "figee" ? EtatZone::Figee : e == "miseDeCote" ? EtatZone::MiseDeCote : EtatZone::Normale;
  z.remplissageFige = lirePolygones(champ(y, "fige"));
  z.signature = txt(y, "signature");
  z.partition = txt(y, "partition");
  z.garde = txt(y, "garde");
  z.pasCouture = std::max<Nm>(entier(y, "pasCouture"), 0);
}
J valeurContrainteJson(const ValeurContrainte& v) {
  J y = J::object();
  if (v.min) y["min"] = v.min;
  if (v.pref) y["pref"] = v.pref;
  if (v.max) y["max"] = v.max;
  if (v.interneDistincte) {
    y["interne"] = J::object();
    if (v.minInterne) y["interne"]["min"] = v.minInterne;
    if (v.prefInterne) y["interne"]["pref"] = v.prefInterne;
    if (v.maxInterne) y["interne"]["max"] = v.maxInterne;
  }
  return y;
}
ValeurContrainte lireValeurContrainteJson(const J& y) {
  ValeurContrainte v;
  v.min = entier(y, "min");
  v.pref = entier(y, "pref");
  v.max = entier(y, "max");
  if (const J& i = champ(y, "interne"); i.is_object()) {
    v.interneDistincte = true;
    v.minInterne = entier(i, "min");
    v.prefInterne = entier(i, "pref");
    v.maxInterne = entier(i, "max");
  }
  return v;
}


constexpr const char* CLES_FEUILLE_034[] = {"directives", "blocs", "formes", "tableaux", "images", "zonesRegles", "groupes", "page", "largeurLibre", "hauteurLibre", "variables",
                                            "modeleFeuille", "calquesMasques", "fichier", "revisionFichier"};
J traceJson(const Trace& t) {
  J y = {{"forme", static_cast<int>(t.forme)}, {"points", points(t.points)}};
  if (t.rayon) y["rayon"] = t.rayon;
  if (t.debut != 0 || t.fin != 0) y["debut"] = t.debut, y["fin"] = t.fin;
  if (t.rempli) y["rempli"] = true;
  if (t.ferme) y["ferme"] = true;
  if (t.fond) y["fond"] = true;
  if (!t.texte.empty()) y["texte"] = t.texte, y["taille"] = t.taille;
  if (t.rayonY) y["rayonY"] = t.rayonY;
  if (t.arrondi) y["arrondi"] = t.arrondi;
  if (t.epaisseur) y["epaisseur"] = t.epaisseur;
  if (!t.style.empty()) y["style"] = t.style;
  if (!t.couleur.empty()) y["couleur"] = t.couleur;
  if (t.fleches) y["fleches"] = t.fleches;
  return y;
}
Trace lireTrace(const J& x) {
  Trace t;
  t.forme = static_cast<Trace::Forme>(std::clamp(ent(x, "forme"), 0, static_cast<int>(Trace::Forme::CadreTexte)));
  t.points = lirePoints(champ(x, "points"));
  t.rayon = entier(x, "rayon");
  t.debut = num(x, "debut");
  t.fin = num(x, "fin");
  t.rempli = vrai(x, "rempli");
  t.ferme = vrai(x, "ferme");
  t.fond = vrai(x, "fond");
  t.texte = txt(x, "texte");
  t.taille = entier(x, "taille", 1'270'000);
  t.rayonY = std::max<Nm>(entier(x, "rayonY"), 0);
  t.arrondi = std::max<Nm>(entier(x, "arrondi"), 0);
  t.epaisseur = std::max<Nm>(entier(x, "epaisseur"), 0);
  t.style = txt(x, "style");
  t.couleur = txt(x, "couleur");
  t.fleches = std::clamp(ent(x, "fleches"), 0, 3);
  return t;
}
J proprietesJson(const std::map<std::string, std::string>& m) {
  J y = J::object();
  for (const auto& [k, v] : m) y[k] = v;
  return y;
}
std::map<std::string, std::string> lireProprietes(const J& o) {
  std::map<std::string, std::string> m;
  if (o.is_object())
    for (auto it = o.begin(); it != o.end(); ++it)
      if (it.value().is_string()) m[it.key()] = it.value().get<std::string>();
  return m;
}
void ecrireFeuille034(const Feuille& f, J& y) {
  if (!f.blocs.empty()) {
    J l = J::array();
    for (const auto& b : f.blocs) {
      J z = {{"uuid", b.uuid}, {"nom", b.nom}, {"feuille", b.feuille}, {"p", pt(b.position)}, {"largeur", b.largeur}, {"hauteur", b.hauteur}};
      J ports = J::array();
      for (const auto& p : b.ports) {
        J q = {{"uuid", p.uuid}, {"nom", p.nom}, {"p", pt(p.p)}, {"cote", p.cote}};
        if (p.sens != SensSignal::Aucun) q["sens"] = codeSensSignal(p.sens);
        ports.push_back(q);
      }
      z["ports"] = ports;
      if (!b.parametres.empty()) z["parametres"] = proprietesJson(b.parametres);
      if (!b.fichier.empty()) z["fichier"] = b.fichier;
      if (!b.couleur.empty()) z["couleur"] = b.couleur;
      l.push_back(z);
    }
    y["blocs"] = l;
  }
  if (!f.formes.empty()) {
    J l = J::array();
    for (const auto& x : f.formes) {
      J z = {{"uuid", x.uuid}, {"trace", traceJson(x.trace)}};
      if (x.calque) z["calque"] = x.calque;
      if (x.ordre) z["ordre"] = x.ordre;
      l.push_back(z);
    }
    y["formes"] = l;
  }
  if (!f.tableaux.empty()) {
    J l = J::array();
    for (const auto& t : f.tableaux)
      l.push_back({{"uuid", t.uuid}, {"p", pt(t.p)}, {"cellules", t.cellules}, {"largeurs", t.largeurs}, {"hauteurLigne", t.hauteurLigne}, {"taille", t.taille}, {"bordure", t.bordure}});
    y["tableaux"] = l;
  }
  if (!f.images.empty()) {
    J l = J::array();
    for (const auto& im : f.images) {
      J z = {{"uuid", im.uuid}, {"p", pt(im.p)}, {"echelle", im.echelle}};
      if (!im.fichier.empty()) z["fichier"] = im.fichier;
      if (!im.donnees.empty()) z["donnees"] = im.donnees;
      l.push_back(z);
    }
    y["images"] = l;
  }
  if (!f.zonesRegles.empty()) {
    J l = J::array();
    for (const auto& z : f.zonesRegles) {
      J q = {{"uuid", z.uuid}, {"nom", z.nom}, {"contour", points(z.contour)}, {"proprietes", proprietesJson(z.proprietes)}};
      if (z.masque) q["masque"] = true;
      l.push_back(q);
    }
    y["zonesRegles"] = l;
  }
  if (!f.groupes.empty()) {
    J l = J::array();
    for (const auto& g : f.groupes) {
      J q = {{"uuid", g.uuid}, {"nom", g.nom}, {"membres", g.membres}};
      if (g.verrou) q["verrou"] = true;
      l.push_back(q);
    }
    y["groupes"] = l;
  }
  if (!f.page.empty()) y["page"] = f.page;
  if (f.largeurLibre > 0 && f.hauteurLibre > 0) y["largeurLibre"] = f.largeurLibre, y["hauteurLibre"] = f.hauteurLibre;
  if (!f.variables.empty()) y["variables"] = proprietesJson(f.variables);
  if (!f.modeleFeuille.empty()) y["modeleFeuille"] = f.modeleFeuille;
  if (!f.calquesMasques.empty()) y["calquesMasques"] = f.calquesMasques;
  if (!f.fichier.empty()) y["fichier"] = f.fichier;
  if (!f.revisionFichier.empty()) y["revisionFichier"] = f.revisionFichier;
}
void lireFeuille034(const J& x, Feuille& f) {
  for (const auto& z : tableau(x, "blocs")) {
    if (!z.is_object()) continue;
    BlocFeuille b;
    b.uuid = txt(z, "uuid", nouvelUuid());
    b.nom = txt(z, "nom");
    b.feuille = txt(z, "feuille");
    b.position = lirePt(champ(z, "p"));
    b.largeur = std::max<Nm>(entier(z, "largeur", b.largeur), 2'540'000);
    b.hauteur = std::max<Nm>(entier(z, "hauteur", b.hauteur), 2'540'000);
    for (const auto& q : tableau(z, "ports")) {
      if (!q.is_object()) continue;
      PortBloc p;
      p.uuid = txt(q, "uuid", nouvelUuid());
      p.nom = txt(q, "nom");
      p.p = lirePt(champ(q, "p"));
      p.cote = std::clamp(ent(q, "cote"), 0, 3);
      p.sens = sensSignalDepuis(txt(q, "sens"));
      if (!p.nom.empty()) b.ports.push_back(p);
    }
    b.parametres = lireProprietes(champ(z, "parametres"));
    b.fichier = txt(z, "fichier");
    b.couleur = txt(z, "couleur");
    f.blocs.push_back(b);
  }
  for (const auto& z : tableau(x, "formes")) {
    if (!z.is_object()) continue;
    FormeSchema fs;
    fs.uuid = txt(z, "uuid", nouvelUuid());
    fs.trace = lireTrace(champ(z, "trace"));
    fs.calque = std::clamp(ent(z, "calque"), 0, 7);
    fs.ordre = ent(z, "ordre");
    f.formes.push_back(fs);
  }
  for (const auto& z : tableau(x, "tableaux")) {
    if (!z.is_object()) continue;
    TableauSchema t;
    t.uuid = txt(z, "uuid", nouvelUuid());
    t.p = lirePt(champ(z, "p"));
    for (const auto& ligne : tableau(z, "cellules")) {
      std::vector<std::string> l;
      if (ligne.is_array())
        for (const auto& cel : ligne) l.push_back(cel.is_string() ? cel.get<std::string>() : std::string());
      t.cellules.push_back(l);
    }
    for (const auto& w : tableau(z, "largeurs"))
      if (w.is_number()) t.largeurs.push_back(coordonnee(w.get<double>()));
    t.hauteurLigne = std::max<Nm>(entier(z, "hauteurLigne", t.hauteurLigne), 500'000);
    t.taille = std::max<Nm>(entier(z, "taille", t.taille), 300'000);
    t.bordure = vrai(z, "bordure", true);
    f.tableaux.push_back(t);
  }
  for (const auto& z : tableau(x, "images")) {
    if (!z.is_object()) continue;
    ImageSchema im;
    im.uuid = txt(z, "uuid", nouvelUuid());
    im.p = lirePt(champ(z, "p"));
    im.echelle = std::clamp(num(z, "echelle", 1), 0.01, 100.0);
    im.fichier = txt(z, "fichier");
    im.donnees = txt(z, "donnees");
    f.images.push_back(im);
  }
  for (const auto& z : tableau(x, "zonesRegles")) {
    if (!z.is_object()) continue;
    ZoneRegleSchema r;
    r.uuid = txt(z, "uuid", nouvelUuid());
    r.nom = txt(z, "nom");
    r.contour = lirePoints(champ(z, "contour"));
    r.proprietes = lireProprietes(champ(z, "proprietes"));
    r.masque = vrai(z, "masque");
    if (r.contour.size() >= 3) f.zonesRegles.push_back(r);
  }
  for (const auto& z : tableau(x, "groupes")) {
    if (!z.is_object()) continue;
    GroupeSchema g;
    g.uuid = txt(z, "uuid", nouvelUuid());
    g.nom = txt(z, "nom");
    for (const auto& m : tableau(z, "membres"))
      if (m.is_string()) g.membres.push_back(m.get<std::string>());
    g.verrou = vrai(z, "verrou");
    f.groupes.push_back(g);
  }
  f.page = txt(x, "page");
  f.largeurLibre = std::max<Nm>(entier(x, "largeurLibre"), 0);
  f.hauteurLibre = std::max<Nm>(entier(x, "hauteurLibre"), 0);
  f.variables = lireProprietes(champ(x, "variables"));
  f.modeleFeuille = txt(x, "modeleFeuille");
  for (const auto& m : tableau(x, "calquesMasques"))
    if (m.is_number_integer()) f.calquesMasques.push_back(std::clamp(m.get<int>(), 0, 7));
  f.fichier = txt(x, "fichier");
  f.revisionFichier = txt(x, "revisionFichier");
}
J styleFilJson(const StyleFil& s) {
  J y = J::object();
  if (!s.couleur.empty()) y["couleur"] = s.couleur;
  if (!s.style.empty()) y["style"] = s.style;
  if (s.epaisseur) y["epaisseur"] = s.epaisseur;
  return y;
}
StyleFil lireStyleFil(const J& y) { return {txt(y, "couleur"), txt(y, "style"), std::max<Nm>(entier(y, "epaisseur"), 0)}; }
J reglagesSchemaJson(const ReglagesSchema& r) {
  J y = {{"fil", styleFilJson(r.fil)}, {"bus", styleFilJson(r.bus)}};
  if (!r.parClasse.empty()) {
    J pc = J::object();
    for (const auto& [k, v] : r.parClasse) pc[k] = styleFilJson(v);
    y["parClasse"] = pc;
  }
  if (r.diametreJonction) y["diametreJonction"] = r.diametreJonction;
  if (!r.couleurJonction.empty()) y["couleurJonction"] = r.couleurJonction;
  if (r.tailleTexte) y["tailleTexte"] = r.tailleTexte;
  if (r.tailleEtiquette) y["tailleEtiquette"] = r.tailleEtiquette;
  if (r.tailleChamp) y["tailleChamp"] = r.tailleChamp;
  y["formatRenvoi"] = r.formatRenvoi;
  y["formatPage"] = r.formatPage;
  y["porteeNoms"] = r.porteeNoms;
  y["zonesCadre"] = r.zonesCadre;
  return y;
}
ReglagesSchema lireReglagesSchema(const J& y) {
  ReglagesSchema r;
  r.fil = lireStyleFil(champ(y, "fil"));
  r.bus = lireStyleFil(champ(y, "bus"));
  if (const J& pc = champ(y, "parClasse"); pc.is_object())
    for (auto it = pc.begin(); it != pc.end(); ++it) r.parClasse[it.key()] = lireStyleFil(it.value());
  r.diametreJonction = std::max<Nm>(entier(y, "diametreJonction"), 0);
  r.couleurJonction = txt(y, "couleurJonction");
  r.tailleTexte = std::max<Nm>(entier(y, "tailleTexte"), 0);
  r.tailleEtiquette = std::max<Nm>(entier(y, "tailleEtiquette"), 0);
  r.tailleChamp = std::max<Nm>(entier(y, "tailleChamp"), 0);
  r.formatRenvoi = txt(y, "formatRenvoi", r.formatRenvoi);
  r.formatPage = txt(y, "formatPage", r.formatPage);
  r.porteeNoms = txt(y, "porteeNoms", r.porteeNoms);
  r.zonesCadre = vrai(y, "zonesCadre", true);
  return r;
}

constexpr const char* CLES_CIRCUIT_034[] = {"zonesRegles", "regions", "motifs", "groupes", "cotes", "gabarits"};
constexpr const char* CLES_AFFICHAGE_034[] = {"origine", "yHaut", "origineFab", "visibles", "couleursReseaux", "jeux", "nomsCouches", "images", "contoursIsolement"};
void ecrireAffichage034(const AffichageCircuit& a, J& y) {
  if (a.origineTravail) y["origine"] = pt(*a.origineTravail);
  if (a.yVersLeHaut) y["yHaut"] = true;
  if (a.origineFabrication) y["origineFab"] = true;
  if (a.couchesVisibles != ~Couches{0}) y["visibles"] = couchesVersJson(a.couchesVisibles);
  if (!a.couleursReseaux.empty()) y["couleursReseaux"] = a.couleursReseaux;
  if (!a.jeux.empty()) {
    J l = J::array();
    for (const auto& j : a.jeux) {
      J z = {{"nom", j.nom}, {"couches", couchesVersJson(j.couches)}};
      if (!j.opacites.empty()) z["opacites"] = j.opacites;
      l.push_back(z);
    }
    y["jeux"] = l;
  }
  if (!a.nomsCouches.empty()) y["nomsCouches"] = a.nomsCouches;
  if (!a.images.empty()) {
    J l = J::array();
    for (const auto& im : a.images)
      l.push_back({{"fichier", im.fichier}, {"pixels", {im.px1, im.py1, im.px2, im.py2}}, {"carte1", pt(im.carte1)}, {"carte2", pt(im.carte2)}, {"opacite", im.opacite}, {"visible", im.visible}});
    y["images"] = l;
  }
  if (a.contoursIsolement) y["contoursIsolement"] = true;
}
void lireAffichage034(const J& y, AffichageCircuit& a) {
  if (y.contains("origine")) a.origineTravail = lirePt(champ(y, "origine"));
  a.yVersLeHaut = vrai(y, "yHaut");
  a.origineFabrication = vrai(y, "origineFab");
  if (y.contains("visibles")) a.couchesVisibles = couchesDepuisJson(champ(y, "visibles"));
  if (const J& cr = champ(y, "couleursReseaux"); cr.is_object())
    for (auto it = cr.begin(); it != cr.end(); ++it)
      if (it.value().is_string()) a.couleursReseaux[it.key()] = it.value().get<std::string>();
  for (const auto& z : tableau(y, "jeux")) {
    if (!z.is_object()) continue;
    JeuVisibilite j;
    j.nom = txt(z, "nom");
    j.couches = couchesDepuisJson(champ(z, "couches"));
    if (const J& op = champ(z, "opacites"); op.is_object())
      for (auto it = op.begin(); it != op.end(); ++it)
        if (it.value().is_number()) j.opacites[it.key()] = std::clamp(it.value().get<double>(), 0.0, 1.0);
    a.jeux.push_back(j);
  }
  if (const J& nc = champ(y, "nomsCouches"); nc.is_object())
    for (auto it = nc.begin(); it != nc.end(); ++it)
      if (it.value().is_string()) a.nomsCouches[it.key()] = it.value().get<std::string>();
  for (const auto& z : tableau(y, "images")) {
    if (!z.is_object()) continue;
    ImageReference im;
    im.fichier = txt(z, "fichier");
    if (const J& px = champ(z, "pixels"); px.is_array() && px.size() == 4)
      for (std::size_t i = 0; i < 4; ++i)
        if (px[i].is_number()) (i == 0 ? im.px1 : i == 1 ? im.py1 : i == 2 ? im.px2 : im.py2) = px[i].get<double>();
    im.carte1 = lirePt(champ(z, "carte1"));
    im.carte2 = lirePt(champ(z, "carte2"));
    im.opacite = std::clamp(num(z, "opacite", 0.5), 0.0, 1.0);
    im.visible = vrai(z, "visible", true);
    a.images.push_back(im);
  }
  a.contoursIsolement = vrai(y, "contoursIsolement");
}
void ecrireEmpilage034(const Empilage& e, J& y) {
  if (!e.gabarit.empty()) y["gabarit"] = e.gabarit;
  if (e.microVias || e.viasBorgnes || e.viasEnterrees) {
    J v = {{"micro", e.microVias}, {"borgnes", e.viasBorgnes}, {"enterrees", e.viasEnterrees}, {"microDiametre", e.microViaDiametre}, {"microPercage", e.microViaPercage},
           {"priorite", e.prioriteVias}};
    y["vias"] = v;
  }
  if (e.pile.empty()) return;
  static const char* genres[] = {"cuivre", "dielectrique", "ame"};
  static const char* usages[] = {"signal", "plan", "mixte", "cavalier"};
  J l = J::array();
  for (const auto& x : e.pile) {
    J z = {{"genre", genres[static_cast<int>(x.genre)]}, {"epaisseur", x.epaisseur}};
    if (!x.nom.empty()) z["nom"] = x.nom;
    if (!x.materiau.empty()) z["materiau"] = x.materiau;
    if (x.genre == GenreCoucheEmpilage::Cuivre) z["usage"] = usages[static_cast<int>(x.usage)];
    else {
      z["er"] = x.permittivite;
      z["tanD"] = x.pertes;
      if (x.preimpregne) z["preimpregne"] = true;
    }
    if (x.conductivite > 0) z["conductivite"] = x.conductivite;
    l.push_back(z);
  }
  y["pile"] = l;
}
void lireEmpilage034(const J& y, Empilage& e) {
  e.gabarit = txt(y, "gabarit");
  if (const J& v = champ(y, "vias"); v.is_object()) {
    e.microVias = vrai(v, "micro");
    e.viasBorgnes = vrai(v, "borgnes");
    e.viasEnterrees = vrai(v, "enterrees");
    e.microViaDiametre = std::max<Nm>(entier(v, "microDiametre", e.microViaDiametre), 50'000);
    e.microViaPercage = std::clamp<Nm>(entier(v, "microPercage", e.microViaPercage), 25'000, e.microViaDiametre);
    std::vector<std::string> pr;
    for (const auto& x : tableau(v, "priorite"))
      if (x.is_string()) pr.push_back(x.get<std::string>());
    if (!pr.empty()) e.prioriteVias = pr;
  }
  for (const auto& z : tableau(y, "pile")) {
    if (!z.is_object()) continue;
    CoucheEmpilage x;
    const std::string g = txt(z, "genre");
    x.genre = g == "cuivre" ? GenreCoucheEmpilage::Cuivre : g == "ame" ? GenreCoucheEmpilage::AmeMetallique : GenreCoucheEmpilage::Dielectrique;
    x.epaisseur = std::clamp(num(z, "epaisseur"), 0.0, 10.0);
    x.nom = txt(z, "nom");
    x.materiau = txt(z, "materiau", x.genre == GenreCoucheEmpilage::Cuivre ? "Cuivre" : "FR-4");
    const std::string u = txt(z, "usage");
    x.usage = u == "plan" ? UsageCuivre::Plan : u == "mixte" ? UsageCuivre::Mixte : u == "cavalier" ? UsageCuivre::Cavalier : UsageCuivre::Signal;
    x.permittivite = std::clamp(num(z, "er", 4.4), 1.0, 100.0);
    x.pertes = std::clamp(num(z, "tanD", 0.02), 0.0, 1.0);
    x.preimpregne = vrai(z, "preimpregne");
    x.conductivite = std::max(0.0, num(z, "conductivite"));
    e.pile.push_back(x);
  }
}
void ecrireFabrication034(const ReglagesFabrication& f, J& y) {
  if (f.margePate) y["margePate"] = f.margePate;
  if (f.pontVernisMin) y["pontVernisMin"] = f.pontVernisMin;
  if (f.procede != "refusion") y["procede"] = f.procede;
  if (f.densite != 'N') y["densite"] = std::string(1, f.densite);
  if (!f.formatNumeroSerie.empty()) y["formatNumeroSerie"] = f.formatNumeroSerie;
  if (f.fentesG85) y["fentesG85"] = true;
}
void ecrireCircuit034(const Circuit& k, J& ci) {
  if (!k.zonesRegles.empty()) {
    J l = J::array();
    for (const auto& z : k.zonesRegles) {
      J y = {{"uuid", z.uuid}, {"contour", points(z.contour)}};
      if (!z.nom.empty()) y["nom"] = z.nom;
      if (z.couches) y["couches"] = couchesVersJson(z.couches);
      J in = J::array();
      for (const auto& [b, n] : {std::pair{z.interditPistes, "pistes"}, std::pair{z.interditVias, "vias"}, std::pair{z.interditPastilles, "pastilles"}, std::pair{z.interditZones, "zones"},
                                 std::pair{z.interditEmpreintes, "empreintes"}})
        if (b) in.push_back(n);
      if (!in.empty()) y["interdits"] = in;
      if (!z.jeu.empty()) y["jeu"] = z.jeu;
      if (z.hauteurMax) y["hauteurMax"] = z.hauteurMax;
      if (!z.placement.empty()) y["placement"] = z.placement;
      if (z.depuisBoitier) y["depuisBoitier"] = true;
      if (z.verrou != Verrou::Aucun) y["verrou"] = codeVerrou(z.verrou);
      l.push_back(y);
    }
    ci["zonesRegles"] = l;
  }
  if (!k.regions.empty()) {
    J l = J::array();
    for (const auto& r : k.regions) {
      J y = {{"uuid", r.uuid}, {"reseau", r.reseau}, {"couche", codeCouche(r.couche)}, {"forme", points(r.forme)}};
      if (!r.genre.empty()) y["genre"] = r.genre;
      if (!r.objet.empty()) y["objet"] = r.objet;
      if (r.verrou != Verrou::Aucun) y["verrou"] = codeVerrou(r.verrou);
      l.push_back(y);
    }
    ci["regions"] = l;
  }
  if (!k.motifs.empty()) {
    static const char* genres[] = {"accordeon", "trombone", "dentsDeScie"};
    J l = J::array();
    for (const auto& m : k.motifs)
      l.push_back({{"uuid", m.uuid}, {"reseau", m.reseau}, {"genre", genres[static_cast<int>(m.genre)]}, {"couche", codeCouche(m.couche)}, {"a", pt(m.a)}, {"b", pt(m.b)},
                   {"largeur", m.largeur}, {"amplitude", m.amplitude}, {"pas", m.pas}, {"rayon", m.rayon}, {"cible", m.longueurCible}, {"deuxCotes", m.deuxCotes}});
    ci["motifs"] = l;
  }
  if (!k.groupes.empty()) {
    J l = J::array();
    for (const auto& g : k.groupes) {
      J y = {{"uuid", g.uuid}, {"nom", g.nom}, {"membres", g.membres}};
      if (!g.parent.empty()) y["parent"] = g.parent;
      if (g.verrou) y["verrou"] = true;
      l.push_back(y);
    }
    ci["groupes"] = l;
  }
  if (!k.cotes.empty()) {
    static const char* genres[] = {"alignee", "horizontale", "verticale", "angulaire", "rayon", "diametre", "ordonnee", "centre"};
    J l = J::array();
    for (const auto& x : k.cotes) {
      J y = {{"uuid", x.uuid}, {"genre", genres[static_cast<int>(x.genre)]}, {"couche", codeCouche(x.couche)}, {"points", points(x.points)}, {"decalage", x.decalage},
             {"hauteur", x.hauteurTexte}, {"epaisseur", x.epaisseur}, {"precision", x.precision}, {"unite", x.unite}};
      for (const auto& [cle, v] : {std::pair{"prefixe", &x.prefixe}, std::pair{"suffixe", &x.suffixe}, std::pair{"texte", &x.texte}, std::pair{"base", &x.base}})
        if (!v->empty()) y[cle] = *v;
      l.push_back(y);
    }
    ci["cotes"] = l;
  }
  if (!k.gabarits.empty()) {
    J l = J::array();
    for (const auto& [nom, g] : k.gabarits) l.push_back(gabaritJson(g));
    ci["gabarits"] = l;
  }
}
void lireCircuit034(const J& k, Circuit& ci) {
  for (const auto& y : tableau(k, "zonesRegles")) {
    if (!y.is_object()) continue;
    ZoneRegle z;
    z.uuid = txt(y, "uuid", nouvelUuid());
    z.nom = txt(y, "nom");
    z.contour = lirePoints(champ(y, "contour"));
    z.couches = couchesDepuisJson(champ(y, "couches"));
    for (const auto& n : tableau(y, "interdits")) {
      if (!n.is_string()) continue;
      const std::string x = n.get<std::string>();
      if (x == "pistes") z.interditPistes = true;
      else if (x == "vias") z.interditVias = true;
      else if (x == "pastilles") z.interditPastilles = true;
      else if (x == "zones") z.interditZones = true;
      else if (x == "empreintes") z.interditEmpreintes = true;
    }
    z.jeu = txt(y, "jeu");
    z.hauteurMax = std::max<Nm>(entier(y, "hauteurMax"), 0);
    z.placement = txt(y, "placement");
    z.depuisBoitier = vrai(y, "depuisBoitier");
    z.verrou = lireVerrou(y);
    if (z.contour.size() >= 3) ci.zonesRegles.push_back(z);
  }
  for (const auto& y : tableau(k, "regions")) {
    if (!y.is_object()) continue;
    RegionCuivre r;
    r.uuid = txt(y, "uuid", nouvelUuid());
    r.reseau = txt(y, "reseau");
    r.couche = couche(y, "couche", Couche::CuivreDessus);
    r.forme = lirePoints(champ(y, "forme"));
    r.genre = txt(y, "genre");
    r.objet = txt(y, "objet");
    r.verrou = lireVerrou(y);
    if (r.forme.size() >= 3 && estCuivre(r.couche)) ci.regions.push_back(r);
  }
  for (const auto& y : tableau(k, "motifs")) {
    if (!y.is_object()) continue;
    MotifAccord m;
    m.uuid = txt(y, "uuid", nouvelUuid());
    m.reseau = txt(y, "reseau");
    const std::string g = txt(y, "genre");
    m.genre = g == "trombone" ? GenreMotifAccord::Trombone : g == "dentsDeScie" ? GenreMotifAccord::DentsDeScie : GenreMotifAccord::Accordeon;
    m.couche = couche(y, "couche", Couche::CuivreDessus);
    m.a = lirePt(champ(y, "a"));
    m.b = lirePt(champ(y, "b"));
    m.largeur = std::max<Nm>(entier(y, "largeur", m.largeur), 10'000);
    m.amplitude = std::max<Nm>(entier(y, "amplitude", m.amplitude), 0);
    m.pas = std::max<Nm>(entier(y, "pas"), 0);
    m.rayon = std::max<Nm>(entier(y, "rayon"), 0);
    m.longueurCible = std::max<Nm>(entier(y, "cible"), 0);
    m.deuxCotes = vrai(y, "deuxCotes", true);
    ci.motifs.push_back(m);
  }
  for (const auto& y : tableau(k, "groupes")) {
    if (!y.is_object()) continue;
    GroupeCircuit g;
    g.uuid = txt(y, "uuid", nouvelUuid());
    g.nom = txt(y, "nom");
    g.parent = txt(y, "parent");
    for (const auto& m : tableau(y, "membres"))
      if (m.is_string()) g.membres.push_back(m.get<std::string>());
    g.verrou = vrai(y, "verrou");
    ci.groupes.push_back(g);
  }
  for (const auto& y : tableau(k, "cotes")) {
    if (!y.is_object()) continue;
    Cote x;
    x.uuid = txt(y, "uuid", nouvelUuid());
    const std::string g = txt(y, "genre");
    static const std::pair<const char*, GenreCote> genres[] = {{"alignee", GenreCote::Alignee},   {"horizontale", GenreCote::Horizontale}, {"verticale", GenreCote::Verticale},
                                                               {"angulaire", GenreCote::Angulaire}, {"rayon", GenreCote::Rayon},             {"diametre", GenreCote::Diametre},
                                                               {"ordonnee", GenreCote::Ordonnee},   {"centre", GenreCote::Centre}};
    for (const auto& [code, genre] : genres)
      if (g == code) x.genre = genre;
    x.couche = couche(y, "couche", Couche::Dessins);
    x.points = lirePoints(champ(y, "points"));
    x.decalage = entier(y, "decalage", x.decalage);
    x.hauteurTexte = std::max<Nm>(entier(y, "hauteur", x.hauteurTexte), 100'000);
    x.epaisseur = std::max<Nm>(entier(y, "epaisseur", x.epaisseur), 10'000);
    x.precision = std::clamp(ent(y, "precision", 2), 0, 6);
    x.unite = txt(y, "unite", "mm");
    x.prefixe = txt(y, "prefixe");
    x.suffixe = txt(y, "suffixe");
    x.texte = txt(y, "texte");
    x.base = txt(y, "base");
    if (!x.points.empty()) ci.cotes.push_back(x);
  }
  for (const auto& y : tableau(k, "gabarits"))
    if (y.is_object())
      if (GabaritPastille g = lireGabarit(y); !g.nom.empty()) ci.gabarits[g.nom] = g;
}
}  // namespace

J contraintesVersJson(const ContraintesCarte& a) {
  J o = J::object();
  if (!a.contraintes.empty()) {
    J l = J::array();
    for (const auto& c : a.contraintes) {
      J y = {{"nom", c.nom}, {"genre", codeGenreContrainte(c.genre)}, {"valeur", valeurContrainteJson(c.valeur)}};
      if (!c.portee.empty()) y["portee"] = c.portee;
      if (!c.porteeB.empty()) y["porteeB"] = c.porteeB;
      if (c.couches) y["couches"] = couchesVersJson(c.couches);
      if (!c.texte.empty()) y["texte"] = c.texte;
      if (c.priorite) y["priorite"] = c.priorite;
      if (!c.jeu.empty()) y["jeu"] = c.jeu;
      if (!c.justification.empty()) y["justification"] = c.justification;
      if (!c.source.empty()) y["source"] = c.source;
      if (!c.active) y["active"] = false;
      l.push_back(y);
    }
    o["contraintes"] = l;
  }
  if (!a.jeux.empty()) {
    J l = J::array();
    for (const auto& j : a.jeux) {
      J y = {{"nom", j.nom}};
      if (!j.parent.empty()) y["parent"] = j.parent;
      if (!j.description.empty()) y["description"] = j.description;
      l.push_back(y);
    }
    o["jeux"] = l;
  }
  if (!a.classesComposants.empty()) {
    J l = J::array();
    for (const auto& c : a.classesComposants) {
      J y = {{"nom", c.nom}};
      if (!c.membres.empty()) y["membres"] = c.membres;
      if (!c.regle.empty()) y["regle"] = c.regle;
      l.push_back(y);
    }
    o["classesComposants"] = l;
  }
  if (!a.paires.empty()) {
    J l = J::array();
    for (const auto& p : a.paires) {
      J y = {{"nom", p.nom}, {"positif", p.positif}, {"negatif", p.negatif}};
      if (!p.classe.empty()) y["classe"] = p.classe;
      for (const auto& [k, v] : {std::pair{"largeur", p.largeur}, std::pair{"ecart", p.ecart}, std::pair{"ecartMin", p.ecartMin}, std::pair{"ecartMax", p.ecartMax}, std::pair{"nonCoupleMax", p.nonCoupleMax}})
        if (v) y[k] = v;
      if (p.impedance > 0) y["impedance"] = p.impedance;
      l.push_back(y);
    }
    o["paires"] = l;
  }
  if (!a.accords.empty()) {
    J l = J::array();
    for (const auto& g : a.accords) {
      J y = {{"nom", g.nom}, {"reseaux", g.reseaux}, {"tolerance", g.tolerance}};
      if (g.cible) y["cible"] = g.cible;
      if (g.retard) y["retard"] = true;
      l.push_back(y);
    }
    o["accords"] = l;
  }
  if (!a.signaux.empty()) {
    static const char* topo[] = {"libre", "chaine", "flyby", "etoile", "t"};
    J l = J::array();
    for (const auto& s : a.signaux) {
      J y = {{"nom", s.nom}, {"reseaux", s.reseaux}};
      if (!s.ordre.empty()) y["ordre"] = s.ordre;
      if (s.topologie != TopologieSignal::Libre) y["topologie"] = topo[static_cast<int>(s.topologie)];
      if (!s.troncons.empty()) {
        J t = J::array();
        for (const auto& x : s.troncons) {
          J z = {{"de", x.de}, {"vers", x.vers}};
          if (x.longueurMax) z["max"] = x.longueurMax;
          if (x.longueurMin) z["min"] = x.longueurMin;
          t.push_back(z);
        }
        y["troncons"] = t;
      }
      l.push_back(y);
    }
    o["signaux"] = l;
  }
  if (!a.impedances.empty()) {
    J l = J::array();
    for (const auto& p : a.impedances) {
      J y = {{"nom", p.nom}, {"impedance", p.impedance}};
      if (p.differentiel) y["differentiel"] = true;
      J pc = J::object();
      for (const auto& [code, le] : p.parCouche) pc[code] = J::array({le.first, le.second});
      y["parCouche"] = pc;
      l.push_back(y);
    }
    o["impedances"] = l;
  }
  if (a.tableIsolement != "IPC-2221") o["tableIsolement"] = a.tableIsolement;
  return o;
}

ContraintesCarte contraintesDepuisJson(const J& o) {
  ContraintesCarte a;
  for (const auto& y : tableau(o, "contraintes")) {
    if (!y.is_object()) continue;
    Contrainte c;
    c.nom = txt(y, "nom");
    if (!genreContrainteDepuisCode(txt(y, "genre"), c.genre)) continue;
    c.valeur = lireValeurContrainteJson(champ(y, "valeur"));
    c.portee = txt(y, "portee");
    c.porteeB = txt(y, "porteeB");
    c.couches = couchesDepuisJson(champ(y, "couches"));
    c.texte = txt(y, "texte");
    c.priorite = ent(y, "priorite");
    c.jeu = txt(y, "jeu");
    c.justification = txt(y, "justification");
    c.source = txt(y, "source");
    c.active = vrai(y, "active", true);
    a.contraintes.push_back(c);
  }
  for (const auto& y : tableau(o, "jeux"))
    if (y.is_object() && !txt(y, "nom").empty()) a.jeux.push_back({txt(y, "nom"), txt(y, "parent"), txt(y, "description")});
  for (const auto& y : tableau(o, "classesComposants")) {
    if (!y.is_object() || txt(y, "nom").empty()) continue;
    ClasseComposants c;
    c.nom = txt(y, "nom");
    for (const auto& m : tableau(y, "membres"))
      if (m.is_string()) c.membres.push_back(m.get<std::string>());
    c.regle = txt(y, "regle");
    a.classesComposants.push_back(c);
  }
  for (const auto& y : tableau(o, "paires")) {
    if (!y.is_object()) continue;
    PaireDifferentielle p;
    p.nom = txt(y, "nom");
    p.positif = txt(y, "positif");
    p.negatif = txt(y, "negatif");
    p.classe = txt(y, "classe");
    p.largeur = entier(y, "largeur");
    p.ecart = entier(y, "ecart");
    p.ecartMin = entier(y, "ecartMin");
    p.ecartMax = entier(y, "ecartMax");
    p.nonCoupleMax = entier(y, "nonCoupleMax");
    p.impedance = num(y, "impedance");
    if (!p.positif.empty() && !p.negatif.empty()) a.paires.push_back(p);
  }
  for (const auto& y : tableau(o, "accords")) {
    if (!y.is_object()) continue;
    GroupeAccord g;
    g.nom = txt(y, "nom");
    for (const auto& r : tableau(y, "reseaux"))
      if (r.is_string()) g.reseaux.push_back(r.get<std::string>());
    g.cible = entier(y, "cible");
    g.tolerance = entier(y, "tolerance", g.tolerance);
    g.retard = vrai(y, "retard");
    a.accords.push_back(g);
  }
  for (const auto& y : tableau(o, "signaux")) {
    if (!y.is_object()) continue;
    Signal s;
    s.nom = txt(y, "nom");
    for (const auto& r : tableau(y, "reseaux"))
      if (r.is_string()) s.reseaux.push_back(r.get<std::string>());
    for (const auto& r : tableau(y, "ordre"))
      if (r.is_string()) s.ordre.push_back(r.get<std::string>());
    const std::string t = txt(y, "topologie");
    s.topologie = t == "chaine" ? TopologieSignal::Chaine : t == "flyby" ? TopologieSignal::FlyBy : t == "etoile" ? TopologieSignal::Etoile : t == "t" ? TopologieSignal::T : TopologieSignal::Libre;
    for (const auto& z : tableau(y, "troncons"))
      if (z.is_object()) s.troncons.push_back({txt(z, "de"), txt(z, "vers"), entier(z, "max"), entier(z, "min")});
    a.signaux.push_back(s);
  }
  for (const auto& y : tableau(o, "impedances")) {
    if (!y.is_object()) continue;
    ProfilImpedance p;
    p.nom = txt(y, "nom");
    p.impedance = num(y, "impedance", 50);
    p.differentiel = vrai(y, "differentiel");
    if (const J& pc = champ(y, "parCouche"); pc.is_object())
      for (auto it = pc.begin(); it != pc.end(); ++it)
        if (it.value().is_array() && it.value().size() == 2 && it.value()[0].is_number() && it.value()[1].is_number())
          p.parCouche[it.key()] = {coordonnee(it.value()[0].get<double>()), coordonnee(it.value()[1].get<double>())};
    a.impedances.push_back(p);
  }
  a.tableIsolement = txt(o, "tableIsolement", a.tableIsolement);
  return a;
}

J versJson(const ConceptionElec& c) {
  // « formatElectronique » est gardé pour la 0.29, qui sans lui prendrait le fichier pour un fichier de la 0.28 et
  // retournerait les textes de la carte (1.22)
  J o = {{"uuid", c.uuid}, {"nom", c.nom}, {"format", FORMAT_ELECTRONIQUE}, {"formatElectronique", FORMAT_ELECTRONIQUE}};
  if (c.pilotageBoitier) o["pilotageBoitier"] = true;
  if (c.schemaSeul) o["schemaSeul"] = true;
  if (c.verrouillee) o["verrouillee"] = true;
  if (c.synchroSurConfirmation) o["synchroSurConfirmation"] = true;
  if (!c.variables.empty()) o["variables"] = c.variables;
  if (!c.publications.empty()) {
    J pu = J::array();
    for (const auto& x : c.publications) {
      J y = {{"id", x.id}, {"revision", x.revision}, {"date", x.date}, {"auteur", x.auteur}, {"empreinte", x.empreinte}, {"dossier", x.dossier}};
      if (!x.derogation.empty()) y["derogation"] = x.derogation;
      if (!x.violations.empty()) y["violations"] = x.violations;
      if (!x.fichiers.empty()) {
        J f = J::object();
        for (const auto& [nom, h] : x.fichiers) f[nom] = h;
        y["fichiers"] = f;
      }
      rendreInconnues(c, "publication:" + x.id, y);
      pu.push_back(y);
    }
    o["publications"] = pu;
  }
  o["cartouche"] = {{"titre", c.cartouche.titre}, {"societe", c.cartouche.societe}, {"auteur", c.cartouche.auteur}, {"revision", c.cartouche.revision}, {"date", c.cartouche.date}, {"commentaire", c.cartouche.commentaire}};
  if (!c.cartouche.champs.empty()) o["cartouche"]["champs"] = c.cartouche.champs;
  if (!c.cartouche.historique.empty()) {
    J h = J::array();
    for (const auto& r : c.cartouche.historique) h.push_back({{"revision", r.revision}, {"date", r.date}, {"auteur", r.auteur}, {"description", r.description}});
    o["cartouche"]["historique"] = h;
  }
  rendreInconnues(c, "cartouche", o["cartouche"]);
  J comps = J::array();
  for (const ComposantElec* px : trie(c.composants, [](const ComposantElec& x) { return x.uuid; })) {
    const ComposantElec& x = *px;
    J y = {{"uuid", x.uuid}, {"ref", x.reference}, {"valeur", x.valeur}, {"symbole", x.symbole}, {"empreinte", x.empreinte}};
    for (const auto& [k, v] : {std::pair{"fabricant", &x.fabricant}, std::pair{"mpn", &x.mpn}, std::pair{"fournisseur", &x.fournisseur}, std::pair{"description", &x.description}, std::pair{"fiche", &x.fiche}})
      if (!v->empty()) y[k] = *v;
    if (x.nePasMonter) y["nePasMonter"] = true;
    if (x.horsCarte) y["horsCarte"] = true;
    // La valeur lue, en nombre et en unité de base (3.2) : un autre outil la lit sans refaire la grammaire
    if (const ValeurComprise v = lireValeurComposant(x.valeur, familleDe(c, x)); v.comprise()) y["valeurLue"] = {{"nombre", v.nombre}, {"unite", uniteDeFamille(v.famille)}};
    if (!x.formule.empty()) y["formule"] = x.formule;
    if (!x.parametres.empty()) y["parametres"] = x.parametres;
    if (x.hauteur > 0) y["hauteur"] = x.hauteur;
    if (x.provenanceHauteur != ProvenanceHauteur::Saisie) y["provenanceHauteur"] = codeProvenance(x.provenanceHauteur);
    if (!x.sourceHauteur.empty()) y["sourceHauteur"] = x.sourceHauteur;
    if (x.genre != GenreComposant::Standard) y["genre"] = codeGenreComposant(x.genre);
    if (x.horsSimulation) y["horsSimulation"] = true;
    if (!x.brochageVerifie.empty()) y["brochageVerifie"] = x.brochageVerifie;
    if (x.horsNomenclature) y["horsNomenclature"] = true;
    if (x.horsPlacement) y["horsPlacement"] = true;
    if (!x.champs.empty()) y["champs"] = x.champs;
    if (!x.article.empty()) {
      y["article"] = x.article;
      if (x.versionArticle != 0) y["versionArticle"] = x.versionArticle;
    }
    if (!x.lcsc.empty()) y["lcsc"] = x.lcsc;
    if (!x.modele3D.empty()) y["modele3D"] = x.modele3D;
    if (!x.modeleComposant.empty()) y["modeleComposant"] = x.modeleComposant;
    if (x.carteSeule) y["carteSeule"] = true;
    if (!x.variantes.empty()) {
      J va = J::object();
      for (const auto& [nom, sv] : x.variantes) {
        J z = J::object();
        if (sv.montage != MontageVariante::Herite) z["montage"] = sv.montage == MontageVariante::Monte ? "monte" : "nonMonte";
        if (!sv.valeur.empty()) z["valeur"] = sv.valeur;
        if (!sv.article.empty()) z["article"] = sv.article;
        if (!sv.empreinte.empty()) z["empreinte"] = sv.empreinte;
        va[nom] = z;
      }
      y["variantes"] = va;
    }
    rendreInconnues(c, "composant:" + x.uuid, y);
    comps.push_back(y);
  }
  o["composants"] = comps;
  const auto parUuid = [](const auto& x) { return x.uuid; };
  const auto parPoint = [](const auto& x) { return std::pair{x.p.x, x.p.y}; };
  J feuilles = J::array();
  for (const auto& f : c.feuilles) {
    J y = {{"uuid", f.uuid}, {"nom", f.nom}, {"format", f.format}};
    if (f.repetitions > 1) y["repetitions"] = f.repetitions;
    if (!f.modele.empty()) y["modele"] = f.modele, y["instance"] = f.instance;
    J s = J::array();
    for (const SymbolePlace* px : trie(f.symboles, parUuid)) {
      const SymbolePlace& x = *px;
      J z = {{"uuid", x.uuid}, {"symbole", x.symbole}, {"p", pt(x.position)}, {"rot", x.rotation}};
      if (!x.composant.empty()) z["composant"] = x.composant;
      if (!x.nomAlim.empty()) z["alim"] = x.nomAlim;
      if (x.miroir) z["miroir"] = true;
      if (x.unite != 1) z["unite"] = x.unite;
      if (!x.numeros.empty()) z["numeros"] = x.numeros;
      for (const auto& [cle, ch] : {std::pair{"champRef", &x.champReference}, std::pair{"champVal", &x.champValeur}})
        if (*ch) {
          J q = {{"d", pt((*ch)->decalage)}};
          if ((*ch)->bloque) q["bloque"] = true;
          z[cle] = q;
        }
      if (x.vue != 1) z["vue"] = x.vue;
      if (!x.fonctions.empty()) z["fonctions"] = x.fonctions;
      if (x.nomsMasques) z["nomsMasques"] = true;
      rendreInconnues(c, "symbolePlace:" + x.uuid, z);
      s.push_back(z);
    }
    y["symboles"] = s;
    J w = J::array();
    for (const Fil* px : trie(f.fils, parUuid)) {
      J z = {{"uuid", px->uuid}, {"a", pt(px->a)}, {"b", pt(px->b)}};
      if (px->rayon > 0) z["rayon"] = px->rayon;
      rendreInconnues(c, "fil:" + px->uuid, z);
      w.push_back(z);
    }
    y["fils"] = w;
    J jn = J::array(), nc = J::array();
    // [x, y, uuid] : une version qui ne connaît pas l'identifiant lit encore le point (S26)
    auto avecId = [](Pt p, const std::string& uuid) { return uuid.empty() ? pt(p) : J::array({p.x, p.y, uuid}); };
    for (const Jonction* x : trie(f.jonctions, parPoint)) jn.push_back(avecId(x->p, x->uuid));
    for (const NonConnexion* x : trie(f.nonConnexions, parPoint)) nc.push_back(avecId(x->p, x->uuid));
    y["jonctions"] = jn;
    y["nonConnexions"] = nc;
    J e = J::array(), di = J::array();
    for (const Etiquette* px : trie(f.etiquettes, parUuid)) {
      // Un connecteur hors feuille relie comme une globale : une version qui ne le connaît pas le lit ainsi. Une
      // étiquette hiérarchique se lit en locale dans la 0.33 ; une directive, qui ne relie rien, va dans sa propre
      // liste, que la 0.33 garde sans la lire
      J z = {{"uuid", px->uuid}, {"texte", px->texte}, {"p", pt(px->p)}, {"rot", px->rotation},
             {"globale", px->type == TypeEtiquette::Globale || px->type == TypeEtiquette::HorsFeuille}};
      if (px->type == TypeEtiquette::HorsFeuille) z["horsFeuille"] = true;
      if (px->type == TypeEtiquette::Hierarchique) z["hierarchique"] = true;
      if (px->sens != SensSignal::Aucun) z["sens"] = codeSensSignal(px->sens);
      if (!px->proprietes.empty()) z["proprietes"] = px->proprietes;
      rendreInconnues(c, "etiquette:" + px->uuid, z);
      (px->type == TypeEtiquette::Directive ? di : e).push_back(z);
    }
    y["etiquettes"] = e;
    if (!di.empty()) y["directives"] = di;
    ecrireFeuille034(f, y);
    J t = J::array();
    for (const TexteSchema* px : trie(f.textes, parUuid)) {
      J z = {{"uuid", px->uuid}, {"texte", px->texte}, {"p", pt(px->p)}, {"taille", px->taille}};
      if (px->rotation) z["rot"] = px->rotation;
      rendreInconnues(c, "texteSchema:" + px->uuid, z);
      t.push_back(z);
    }
    y["textes"] = t;
    rendreInconnues(c, "feuille:" + f.uuid, y);
    feuilles.push_back(y);
  }
  o["feuilles"] = feuilles;
  J syms = J::array();
  for (const auto& [id, d] : c.symboles) {
    J y = symboleVersJson(d);
    rendreInconnues(c, "defSymbole:" + id, y);
    for (std::size_t i = 0; i < y["broches"].size(); ++i) rendreInconnues(c, "defSymbole:" + id + "/broche:" + std::to_string(i), y["broches"][i]);
    for (std::size_t i = 0; i < y["traces"].size(); ++i) rendreInconnues(c, "defSymbole:" + id + "/trace:" + std::to_string(i), y["traces"][i]);
    syms.push_back(y);
  }
  o["symboles"] = syms;
  if (!c.nomsReseaux.empty()) o["nomsReseaux"] = c.nomsReseaux;
  if (!c.erc.vide()) {
    J e = J::object();
    if (!c.erc.gravites.empty()) {
      J g = J::object();
      for (const auto& [code, gr] : c.erc.gravites) g[code] = gr == GraviteReglee::Erreur ? "erreur" : gr == GraviteReglee::Avertissement ? "avertissement" : "ignorer";
      e["gravites"] = g;
    }
    if (!c.erc.matrice.empty()) e["matrice"] = c.erc.matrice;
    if (!c.erc.exclusions.empty()) {
      J x = J::array();
      for (const auto& ex : c.erc.exclusions) {
        J un = {{"code", ex.code}, {"objets", ex.objets}, {"raison", ex.raison}, {"auteur", ex.auteur}, {"date", ex.date}};
        if (!ex.signature.empty()) un["signature"] = ex.signature;
        x.push_back(un);
      }
      e["exclusions"] = x;
    }
    rendreInconnues(c, "erc", e);
    o["erc"] = e;
  }
  if (!c.empreintes.empty()) {
    J em = J::array();
    for (const auto& [id, d] : c.empreintes) {
      J y = empreinteVersJson(d);
      rendreInconnues(c, "defEmpreinte:" + id, y);
      rendreInconnues(c, "defEmpreinte:" + id + "/corps", y["corps"]);
      for (std::size_t i = 0; i < y["pastilles"].size(); ++i) rendreInconnues(c, "defEmpreinte:" + id + "/pastille:" + std::to_string(i), y["pastilles"][i]);
      for (std::size_t i = 0; i < y["traits"].size(); ++i) rendreInconnues(c, "defEmpreinte:" + id + "/trait:" + std::to_string(i), y["traits"][i]);
      em.push_back(y);
    }
    o["bibliothequeEmpreintes"] = em;
  }
  if (!c.articles.empty()) {
    J ar = J::array();
    for (const auto& [id, a] : c.articles) ar.push_back(articleVersJson(a));
    o["articles"] = ar;
  }
  if (!c.modeles3D.empty()) {
    J mo = J::array();
    for (const auto& [id, m] : c.modeles3D) {
      J y = {{"id", id}, {"nom", m.nom}, {"boite", {m.x0, m.y0, m.z0, m.x1, m.y1, m.z1}}, {"brep", m.brep}};
      if (m.quarts) y["quarts"] = m.quarts;
      if (m.dx != 0 || m.dy != 0 || m.dz != 0) y["reglage"] = {m.dx, m.dy, m.dz};
      mo.push_back(y);
    }
    o["modeles3D"] = mo;
  }
  if (!c.facades.empty()) {
    J fa = J::array();
    for (const auto& f : c.facades) {
      J y = {{"uuid", f.uuid}, {"nom", f.nom}, {"ecartMin", f.ecartMin}, {"marge", f.marge}, {"elements", J::array()}};
      static const char* genres[] = {"decoupeRonde", "decoupeRect", "legende", "echelle"};
      for (const auto& e : f.elements) {
        J z = {{"uuid", e.uuid}, {"genre", genres[static_cast<int>(e.genre)]}, {"dx", e.dx}, {"dy", e.dy}};
        if (!e.composant.empty()) z["composant"] = e.composant;
        switch (e.genre) {
          case ElementFacade::Genre::DecoupeRonde: z["diametre"] = e.diametre; break;
          case ElementFacade::Genre::DecoupeRect: z["largeur"] = e.largeur, z["hauteur"] = e.hauteur; break;
          case ElementFacade::Genre::Legende: z["texte"] = e.texte, z["taille"] = e.taille; break;
          case ElementFacade::Genre::Echelle:
            z["graduations"] = e.graduations, z["rayon"] = e.rayon, z["angleDebut"] = e.angleDebut, z["angleFin"] = e.angleFin, z["longueurTrait"] = e.longueurTrait;
            break;
        }
        y["elements"].push_back(z);
      }
      fa.push_back(y);
    }
    o["facades"] = fa;
  }
  if (!c.piecesMontees.empty()) {
    J pm = J::array();
    for (const auto& x : c.piecesMontees) {
      J y = {{"uuid", x.uuid}, {"reference", x.reference}, {"nom", x.nom}, {"place", {x.x, x.y, x.z}}};
      if (!x.fabricant.empty()) y["fabricant"] = x.fabricant;
      if (!x.mpn.empty()) y["mpn"] = x.mpn;
      if (!x.fournisseur.empty()) y["fournisseur"] = x.fournisseur;
      if (!x.modele.empty()) y["modele"] = x.modele;
      if (!x.composant.empty()) y["composant"] = x.composant;
      if (x.quantite != 1) y["quantite"] = x.quantite;
      if (x.rotation != 0) y["rotation"] = x.rotation;
      if (x.dessous) y["dessous"] = true;
      pm.push_back(y);
    }
    o["piecesMontees"] = pm;
  }
  if (c.boitierImporte) {
    const BoitierImporte& b = *c.boitierImporte;
    J y = {{"modele", b.modele}, {"place", {b.x, b.y, b.z}}, {"rotation", b.rotation}, {"jeu", b.jeu}};
    if (!b.nom.empty()) y["nom"] = b.nom;
    if (!b.fabricant.empty()) y["fabricant"] = b.fabricant;
    if (!b.mpn.empty()) y["mpn"] = b.mpn;
    o["boitierImporte"] = y;
  }
  if (!c.pointsDeVue.empty()) {
    J pv = J::array();
    for (const auto& v : c.pointsDeVue)
      pv.push_back({{"nom", v.nom}, {"cible", {v.cible[0], v.cible[1], v.cible[2]}}, {"azimut", v.azimut}, {"elevation", v.elevation}, {"distance", v.distance},
                    {"orthographique", v.orthographique}, {"champ", v.champ}});
    o["pointsDeVue"] = pv;
  }
  if (!c.esquisseContour.is_null()) o["esquisseContour"] = c.esquisseContour;
  const Circuit& k = c.circuit;
  J ci = {{"contour", {{"origine", pt(k.contour.origine)}, {"largeur", k.contour.largeur}, {"hauteur", k.contour.hauteur}, {"rayon", k.contour.rayon}, {"libre", points(k.contour.libre)}}},
          {"empilage", {{"couches", k.empilage.couchesCuivre}, {"epaisseur", k.empilage.epaisseur}, {"cuivre", k.empilage.cuivre}, {"materiau", k.empilage.materiau}, {"finition", k.empilage.finition},
                        {"vernis", k.empilage.vernis}, {"serigraphie", k.empilage.serigraphie}}},
          {"regles", reglesVersJson(k.regles, c)},
          {"serigraphie", {{"reperes", k.serigraphie.reperes}, {"valeurs", k.serigraphie.valeurs}, {"hauteur", k.serigraphie.hauteur}}},
          {"fabrication", {{"margeVernis", k.fabrication.margeVernis}, {"viasCouvertes", k.fabrication.viasCouvertes}, {"tauxPate", k.fabrication.tauxPateGrandes},
                           {"seuilPate", k.fabrication.seuilGrandePastille}}}};
  if (k.affichage.unite != "mm" || k.affichage.grille != 0 || k.affichage.opaciteInactives != 0.45 || !(k.affichage == AffichageCircuit{k.affichage.unite, k.affichage.grille, k.affichage.opaciteInactives})) {
    ci["affichage"] = {{"unite", k.affichage.unite}, {"grille", k.affichage.grille}, {"opacite", k.affichage.opaciteInactives}};
    ecrireAffichage034(k.affichage, ci["affichage"]);
  }
  ecrireEmpilage034(k.empilage, ci["empilage"]);
  ecrireFabrication034(k.fabrication, ci["fabrication"]);
  if (!k.contour.traitements.empty()) {
    static const char* genres[] = {"biseau", "bordMetallise", "demiTrous"};
    J t = J::array();
    for (const auto& x : k.contour.traitements) {
      J y = {{"genre", genres[static_cast<int>(x.genre)]}, {"de", x.premiere}, {"a", x.derniere}};
      if (x.genre == ContourCarte::Traitement::Genre::Biseau) y["angle"] = x.angle, y["profondeur"] = x.profondeur;
      t.push_back(y);
    }
    ci["contour"]["traitements"] = t;
  }
  if (!k.contour.decoupes.empty()) {
    J d = J::array();
    for (const auto& x : k.contour.decoupes) d.push_back(points(x));
    ci["contour"]["decoupes"] = d;
  }
  for (const char* sous : {"contour", "empilage", "serigraphie", "fabrication", "affichage"})
    if (ci.contains(sous)) rendreInconnues(c, std::string("circuit/") + sous, ci[sous]);
  J em = J::array();
  for (const EmpreintePlacee* px : trie(k.empreintes, [](const EmpreintePlacee& x) { return x.composant; })) {
    J y = {{"composant", px->composant}, {"empreinte", px->empreinte}, {"p", pt(px->position)}, {"rot", px->rotation}};
    if (px->dessous) y["dessous"] = true;
    if (px->verrou) y["verrou"] = true;
    if (px->repere) y["repere"] = pt(*px->repere);
    if (!px->copieLocale.empty()) y["copieLocale"] = px->copieLocale;
    if (!px->couleur.empty()) y["couleur"] = px->couleur;
    rendreInconnues(c, "empreintePlacee:" + px->composant, y);
    em.push_back(y);
  }
  ci["empreintes"] = em;
  J pi = J::array();
  for (const Piste* px : trie(k.pistes, parUuid)) {
    J y = {{"uuid", px->uuid}, {"reseau", px->reseau}, {"couche", codeCouche(px->couche)}, {"a", pt(px->a)}, {"b", pt(px->b)}, {"largeur", px->largeur}};
    ecrirePiste034(*px, y);
    rendreInconnues(c, "piste:" + px->uuid, y);
    pi.push_back(y);
  }
  ci["pistes"] = pi;
  J vi = J::array();
  for (const Via* px : trie(k.vias, parUuid)) {
    J y = {{"uuid", px->uuid}, {"reseau", px->reseau}, {"p", pt(px->p)}, {"diametre", px->diametre}, {"percage", px->percage}};
    ecrireVia034(*px, y);
    rendreInconnues(c, "via:" + px->uuid, y);
    vi.push_back(y);
  }
  ci["vias"] = vi;
  J zo = J::array();
  for (const auto& x : k.zones) {  // dans leur ordre : il départage deux zones de même priorité
    J y = {{"uuid", x.uuid}, {"reseau", x.reseau}, {"couche", codeCouche(x.couche)}, {"contour", points(x.contour)}, {"priorite", x.priorite}, {"isolement", x.isolement},
           {"epaisseurMin", x.epaisseurMin}, {"thermique", x.thermique}, {"largeurRayon", x.largeurRayon}, {"ecartThermique", x.ecartThermique}};
    ecrireZone034(x, y);
    rendreInconnues(c, "zone:" + x.uuid, y);
    zo.push_back(y);
  }
  ci["zones"] = zo;
  J te = J::array();
  for (const TexteCircuit* px : trie(k.textes, parUuid)) {
    J y = {{"uuid", px->uuid}, {"texte", px->texte}, {"couche", codeCouche(px->couche)}, {"p", pt(px->p)}, {"hauteur", px->hauteur}, {"rot", px->rotation}};
    if (px->miroir) y["miroir"] = *px->miroir;
    if (!px->police.empty()) y["police"] = px->police;
    if (px->reserve) y["reserve"] = true;
    rendreInconnues(c, "texteCarte:" + px->uuid, y);
    te.push_back(y);
  }
  ci["textes"] = te;
  if (!k.volumes.empty()) ci["volumes"] = volumesVersJson(k.volumes);
  if (!k.dessins.empty()) {
    J ds = J::array();
    static const char* formes[] = {"trait", "arc", "cercle", "polygone", "rectangle", "bezier"};
    for (const auto& d : k.dessins) {
      J y = {{"uuid", d.uuid}, {"forme", formes[static_cast<int>(d.forme)]}, {"couche", codeCouche(d.couche)}, {"points", points(d.points)}, {"epaisseur", d.epaisseur}};
      if (d.forme == DessinCircuit::Forme::Rectangle || d.forme == DessinCircuit::Forme::Bezier) {
        // La 0.33 lit un polygone (rectangle) ou un trait (courbe) aux mêmes sommets découpés
        y["forme034"] = y["forme"];
        y["points034"] = y["points"];
        y["forme"] = d.forme == DessinCircuit::Forme::Rectangle ? "polygone" : "trait";
        y["points"] = points(contourDessin(d));
      }
      if (d.plein) y["plein"] = true;
      if (d.hachure) y["hachure"] = true;
      if (d.rayon) y["rayon"] = d.rayon;
      ds.push_back(y);
    }
    ci["dessins"] = ds;
  }
  ecrireCircuit034(k, ci);
  rendreInconnues(c, "circuit", ci);
  o["circuit"] = ci;
  // Lots L12 et L13 : variantes de montage, notes attachées aux objets, jeux de sorties
  if (!c.variantesMontage.empty()) {
    J va = J::array();
    for (const auto& v : c.variantesMontage) {
      J y = {{"nom", v.nom}};
      if (!v.description.empty()) y["description"] = v.description;
      va.push_back(y);
    }
    o["variantesMontage"] = va;
  }
  if (!c.varianteActive.empty()) o["varianteActive"] = c.varianteActive;
  if (!c.notes.empty()) {
    static const char* genres[] = {"commentaire", "tache", "journal"};
    J no = J::array();
    for (const auto& n : c.notes) {
      J y = {{"uuid", n.uuid}, {"genre", genres[static_cast<int>(n.genre)]}, {"texte", n.texte}};
      if (!n.objet.empty()) y["objet"] = n.objet;
      if (!n.parent.empty()) y["parent"] = n.parent;
      if (!n.auteur.empty()) y["auteur"] = n.auteur;
      if (!n.date.empty()) y["date"] = n.date;
      if (!n.responsable.empty()) y["responsable"] = n.responsable;
      if (!n.revision.empty()) y["revision"] = n.revision;
      if (n.priorite) y["priorite"] = n.priorite;
      if (n.close) y["close"] = true;
      no.push_back(y);
    }
    o["notes"] = no;
  }
  if (!c.jeuxSorties.empty()) {
    J js = J::array();
    for (const auto& j : c.jeuxSorties) {
      J y = {{"nom", j.nom}, {"dossier", j.dossier}, {"sorties", j.sorties}};
      if (!j.variante.empty()) y["variante"] = j.variante;
      if (j.zip) y["zip"] = true;
      if (!j.controler) y["controler"] = false;
      js.push_back(y);
    }
    o["jeuxSorties"] = js;
  }
  if (!(c.reglagesSchema == ReglagesSchema{})) o["reglagesSchema"] = reglagesSchemaJson(c.reglagesSchema);
  if (!c.aliasBus.empty()) o["aliasBus"] = c.aliasBus;
  if (!c.extensions.empty()) {
    J ex = J::object();
    for (const auto& [cle, v] : c.extensions) ex[cle] = v;
    o["extensions"] = ex;
  }
  rendreInconnues(c, "conception", o);
  return o;
}

J empreinteVersJson(const DefEmpreinte& d) {
  J o = {{"id", d.id}, {"description", d.description}, {"famille", d.famille}, {"version", d.version}};
  J pa = J::array();
  for (const auto& p : d.pastilles) {
    J y = {{"numero", p.numero}, {"type", static_cast<int>(p.type)}, {"forme", static_cast<int>(p.forme)}, {"p", pt(p.position)}, {"l", p.largeur}, {"h", p.hauteur}};
    if (p.rotation != 0) y["rot"] = p.rotation;
    if (p.percage) y["percage"] = p.percage;
    if (p.forme == FormePastille::RectArrondi) y["arrondi"] = p.arrondi;
    if (p.couches) y["couches"] = couchesVersJson(p.couches);
    if (!p.fabrication.empty()) y["fab"] = p.fabrication;
    ecrirePastille034(p, y);
    pa.push_back(y);
  }
  o["pastilles"] = pa;
  J tr = J::array();
  for (const auto& t : d.traits) {
    J y = {{"couche", codeCouche(t.couche)}, {"a", pt(t.a)}, {"b", pt(t.b)}, {"ep", t.epaisseur}};
    if (t.rayon) y["rayon"] = t.rayon;
    tr.push_back(y);
  }
  o["traits"] = tr;
  o["encombrement"] = J::array({d.encombrement.x0, d.encombrement.y0, d.encombrement.x1, d.encombrement.y1});
  o["corps"] = {{"l", d.corps.largeur}, {"p", d.corps.profondeur}, {"h", d.corps.hauteur}, {"rond", d.corps.rond}, {"couleur", d.corps.couleur}, {"dx", d.corps.decalage.x}, {"dy", d.corps.decalage.y}};
  if (!d.volumes.empty()) o["volumes"] = volumesVersJson(d.volumes);
  return o;
}

DefEmpreinte empreinteDepuisJson(const J& o) {
  DefEmpreinte d;
  d.id = txt(o, "id");
  d.description = txt(o, "description");
  d.famille = txt(o, "famille");
  d.version = ent(o, "version", 1);
  for (const auto& y : tableau(o, "pastilles")) {
    if (!objet(y, "pastille d'empreinte")) continue;
    Pastille p;
    p.numero = txt(y, "numero");
    p.type = static_cast<TypePastille>(std::clamp(ent(y, "type"), 0, 2));
    p.forme = static_cast<FormePastille>(std::clamp(ent(y, "forme", 3), 0, 3));
    p.position = lirePt(champ(y, "p"));
    p.largeur = std::max<Nm>(entier(y, "l"), 1'000);
    p.hauteur = std::max<Nm>(entier(y, "h"), 1'000);
    p.rotation = num(y, "rot");
    p.percage = std::max<Nm>(entier(y, "percage"), 0);
    p.arrondi = std::clamp(num(y, "arrondi", 0.25), 0.0, 0.5);
    p.couches = couchesDepuisJson(champ(y, "couches"));
    p.fabrication = txt(y, "fab");
    lirePastille034(y, p);
    d.pastilles.push_back(p);
  }
  for (const auto& y : tableau(o, "traits")) {
    if (!objet(y, "trait d'empreinte")) continue;
    TraitEmpreinte t;
    t.couche = couche(y, "couche", Couche::SerigraphieDessus);
    t.a = lirePt(champ(y, "a"));
    t.b = lirePt(champ(y, "b"));
    t.epaisseur = std::max<Nm>(entier(y, "ep", t.epaisseur), 1'000);
    t.rayon = std::max<Nm>(entier(y, "rayon"), 0);
    d.traits.push_back(t);
  }
  const J& e = champ(o, "encombrement");
  if (e.is_array() && e.size() == 4 && e[0].is_number() && e[1].is_number() && e[2].is_number() && e[3].is_number())
    d.encombrement = {coordonnee(e[0].get<double>()), coordonnee(e[1].get<double>()), coordonnee(e[2].get<double>()), coordonnee(e[3].get<double>())};
  const J& k = champ(o, "corps");
  d.corps.largeur = std::max<Nm>(entier(k, "l"), 0);
  d.corps.profondeur = std::max<Nm>(entier(k, "p"), 0);
  d.corps.hauteur = std::max<Nm>(entier(k, "h"), 0);
  d.corps.rond = vrai(k, "rond");
  d.corps.couleur = txt(k, "couleur", d.corps.couleur);
  // Centre du corps (3.4, T5) : absent avant la 0.32, il prend le centre de l'encombrement
  if (k.is_object() && k.contains("dx") && k.contains("dy")) d.corps.decalage = {entier(k, "dx"), entier(k, "dy")};
  else d.corps.decalage = decalageParDefaut(d);
  d.volumes = volumesDepuisJson(champ(o, "volumes"));
  return d;
}

namespace {
ConceptionElec lireConception(const J& o, std::vector<std::string>* alertes);
}  // namespace

// Une conception écrite par une version précédente monte d'abord, en mémoire, au format courant, étape par étape
// (1.22, ECH-11) ; ce que la montée a changé rejoint les alertes de lecture
ConceptionElec conceptionDepuisJson(const J& o, std::vector<std::string>* alertes) {
  if (!o.is_object() || versionConception(o) == versionFichierCourante()) return lireConception(o, alertes);
  J monte = o;
  std::vector<std::string> notes;
  monterConception(monte, &notes);
  ConceptionElec c = lireConception(monte, alertes);
  // Champs libres connus (« Tolérance », « Puissance »…) repris en paramètres typés, texte gardé tel quel (3.3)
  if (const int n = migrerChampsEnParametres(c); n > 0)
    notes.push_back(std::to_string(n) + (n > 1 ? " champs libres repris en paramètres typés (tolérance, puissance…), textes gardés." : " champ libre repris en paramètre typé, texte gardé."));
  if (alertes)
    for (const auto& n : notes) alertes->push_back("Carte « " + c.nom + " » : " + n);
  return c;
}

namespace {
ConceptionElec lireConception(const J& o, std::vector<std::string>* alertes) {
  // Lecture objet par objet (EL-07) : un objet illisible est ignoré et compté, le reste de la conception est
  // gardé ; une coordonnée hors des bornes est ramenée à ±10 m (EL-34). Le compte rendu va dans « alertes ».
  CompteLecture compte;
  CompteLecture* const precedent = std::exchange(g_compte, &compte);
  struct Restaurer {
    CompteLecture* p;
    ~Restaurer() { g_compte = p; }
  } restaurer{precedent};
  ConceptionElec c;
  c.uuid = txt(o, "uuid");
  if (c.uuid.empty()) c.uuid = nouvelUuid();
  c.nom = txt(o, "nom", c.nom);
  garderInconnues(c, "conception", o,
                  {"uuid", "nom", "format", "formatElectronique", "pilotageBoitier", "schemaSeul", "verrouillee", "publications", "cartouche", "composants", "feuilles", "symboles", "nomsReseaux",
                   "erc", "bibliothequeEmpreintes", "circuit", "synchroSurConfirmation", "variables", "articles", "modeles3D", "facades", "piecesMontees", "boitierImporte", "pointsDeVue", "esquisseContour",
                   "variantesMontage", "varianteActive", "notes", "jeuxSorties", "reglagesSchema", "aliasBus", "extensions"});
  if (const J& rs = champ(o, "reglagesSchema"); rs.is_object()) c.reglagesSchema = lireReglagesSchema(rs);
  if (const J& ab = champ(o, "aliasBus"); ab.is_object())
    for (auto it = ab.begin(); it != ab.end(); ++it)
      if (it.value().is_array()) {
        std::vector<std::string> membres;
        for (const auto& m : it.value())
          if (m.is_string()) membres.push_back(m.get<std::string>());
        c.aliasBus[it.key()] = membres;
      }
  if (const J& ex = champ(o, "extensions"); ex.is_object())
    for (auto it = ex.begin(); it != ex.end(); ++it) c.extensions[it.key()] = it.value();
  for (const auto& x : tableau(o, "articles"))
    if (x.is_object())
      if (Article a = articleDepuisJson(x); !a.id.empty()) c.articles[a.id] = a;
  for (const auto& x : tableau(o, "modeles3D")) {
    if (!x.is_object() || txt(x, "id").empty()) continue;
    Modele3D m;
    m.nom = txt(x, "nom");
    m.brep = txt(x, "brep");
    if (const J& b = champ(x, "boite"); b.is_array() && b.size() == 6) {
      double v[6];
      for (int i = 0; i < 6; ++i) v[i] = b[static_cast<std::size_t>(i)].is_number() ? b[static_cast<std::size_t>(i)].get<double>() : 0;
      m.x0 = v[0], m.y0 = v[1], m.z0 = v[2], m.x1 = v[3], m.y1 = v[4], m.z1 = v[5];
    }
    m.quarts = ent(x, "quarts", 0);
    if (const J& r = champ(x, "reglage"); r.is_array() && r.size() == 3 && r[0].is_number() && r[1].is_number() && r[2].is_number())
      m.dx = r[0].get<double>(), m.dy = r[1].get<double>(), m.dz = r[2].get<double>();
    c.modeles3D[txt(x, "id")] = m;
  }
  auto trois = [](const J& t, double v[3]) {
    if (t.is_array() && t.size() == 3)
      for (std::size_t i = 0; i < 3; ++i) v[i] = t[i].is_number() ? t[i].get<double>() : 0;
  };
  for (const auto& x : tableau(o, "piecesMontees")) {
    if (!x.is_object()) continue;
    PieceMontee m;
    m.uuid = txt(x, "uuid");
    if (m.uuid.empty()) m.uuid = nouvelUuid();
    m.reference = txt(x, "reference");
    m.nom = txt(x, "nom");
    m.fabricant = txt(x, "fabricant");
    m.mpn = txt(x, "mpn");
    m.fournisseur = txt(x, "fournisseur");
    m.modele = txt(x, "modele");
    m.composant = txt(x, "composant");
    m.quantite = std::max(1, ent(x, "quantite", 1));
    double v[3] = {0, 0, 0};
    trois(champ(x, "place"), v);
    m.x = v[0], m.y = v[1], m.z = v[2];
    m.rotation = num(x, "rotation", 0);
    m.dessous = vrai(x, "dessous");
    c.piecesMontees.push_back(m);
  }
  if (const J& b = champ(o, "boitierImporte"); b.is_object()) {
    BoitierImporte bi;
    bi.modele = txt(b, "modele");
    bi.nom = txt(b, "nom");
    bi.fabricant = txt(b, "fabricant");
    bi.mpn = txt(b, "mpn");
    double v[3] = {0, 0, 0};
    trois(champ(b, "place"), v);
    bi.x = v[0], bi.y = v[1], bi.z = v[2];
    bi.rotation = num(b, "rotation", 0);
    bi.jeu = num(b, "jeu", 0.5);
    c.boitierImporte = bi;
  }
  for (const auto& x : tableau(o, "pointsDeVue")) {
    if (!x.is_object() || txt(x, "nom").empty()) continue;
    PointDeVue v;
    v.nom = txt(x, "nom");
    trois(champ(x, "cible"), v.cible);
    v.azimut = num(x, "azimut", 45);
    v.elevation = num(x, "elevation", 30);
    v.distance = num(x, "distance", 100);
    v.orthographique = vrai(x, "orthographique");
    v.champ = num(x, "champ", 30);
    c.pointsDeVue.push_back(v);
  }
  for (const auto& x : tableau(o, "facades")) {
    if (!x.is_object()) continue;
    Facade f;
    f.uuid = txt(x, "uuid");
    if (f.uuid.empty()) f.uuid = nouvelUuid();
    f.nom = txt(x, "nom", f.nom);
    f.ecartMin = num(x, "ecartMin", 2);
    f.marge = num(x, "marge", 1.5);
    for (const auto& z : tableau(x, "elements")) {
      if (!z.is_object()) continue;
      ElementFacade e;
      e.uuid = txt(z, "uuid");
      if (e.uuid.empty()) e.uuid = nouvelUuid();
      const std::string g = txt(z, "genre");
      e.genre = g == "decoupeRect" ? ElementFacade::Genre::DecoupeRect : g == "legende" ? ElementFacade::Genre::Legende : g == "echelle" ? ElementFacade::Genre::Echelle : ElementFacade::Genre::DecoupeRonde;
      e.composant = txt(z, "composant");
      e.dx = num(z, "dx");
      e.dy = num(z, "dy");
      e.diametre = num(z, "diametre", 7);
      e.largeur = num(z, "largeur", 10);
      e.hauteur = num(z, "hauteur", 5);
      e.texte = txt(z, "texte");
      e.taille = num(z, "taille", 3);
      e.graduations = ent(z, "graduations", 11);
      e.rayon = num(z, "rayon", 10);
      e.angleDebut = num(z, "angleDebut", 225);
      e.angleFin = num(z, "angleFin", -45);
      e.longueurTrait = num(z, "longueurTrait", 2);
      f.elements.push_back(e);
    }
    c.facades.push_back(f);
  }
  if (o.is_object() && o.contains("esquisseContour") && o["esquisseContour"].is_object()) c.esquisseContour = o["esquisseContour"];
  for (const auto& x : tableau(o, "variantesMontage")) {
    if (!x.is_object() || txt(x, "nom").empty()) continue;
    if (std::any_of(c.variantesMontage.begin(), c.variantesMontage.end(), [&](const VarianteMontage& v) { return v.nom == txt(x, "nom"); })) continue;
    c.variantesMontage.push_back({txt(x, "nom"), txt(x, "description")});
  }
  c.varianteActive = txt(o, "varianteActive");
  for (const auto& x : tableau(o, "notes")) {
    if (!x.is_object()) continue;
    NoteObjet n;
    n.uuid = txt(x, "uuid");
    if (n.uuid.empty()) n.uuid = nouvelUuid();
    const std::string g = txt(x, "genre");
    n.genre = g == "tache" ? NoteObjet::Genre::Tache : g == "journal" ? NoteObjet::Genre::Journal : NoteObjet::Genre::Commentaire;
    n.objet = txt(x, "objet");
    n.parent = txt(x, "parent");
    n.auteur = txt(x, "auteur");
    n.date = txt(x, "date");
    n.texte = txt(x, "texte");
    n.responsable = txt(x, "responsable");
    n.revision = txt(x, "revision");
    n.priorite = std::clamp(ent(x, "priorite"), 0, 2);
    n.close = vrai(x, "close");
    c.notes.push_back(n);
  }
  for (const auto& x : tableau(o, "jeuxSorties")) {
    if (!x.is_object() || txt(x, "nom").empty()) continue;
    JeuSorties j;
    j.nom = txt(x, "nom");
    j.dossier = txt(x, "dossier", j.dossier);
    j.variante = txt(x, "variante");
    for (const auto& y : tableau(x, "sorties"))
      if (y.is_string()) j.sorties.push_back(y.get<std::string>());
    j.zip = vrai(x, "zip");
    j.controler = vrai(x, "controler", true);
    c.jeuxSorties.push_back(j);
  }
  // Format de la conception (ECH-10) : « format », « formatElectronique » dans les fichiers de la 0.29, rien jusqu'à
  // la 0.28. Plus récent que ce logiciel : lecture tolérante, et l'alerte dit ce qu'enregistrer peut perdre.
  const int format = o.is_object() && o.contains("format") && o["format"].is_number() ? ent(o, "format", 1) : ent(o, "formatElectronique", 1);
  if (format > FORMAT_ELECTRONIQUE && alertes)
    alertes->push_back("Carte « " + c.nom + " » : écrite par une version plus récente de Prométhée (format " + std::to_string(format) + ", ce logiciel lit jusqu'au " +
                       std::to_string(FORMAT_ELECTRONIQUE) + ") ; ce qu'il ne connaît pas est gardé tel quel, mais l'enregistrer peut perdre des données.");
  c.pilotageBoitier = vrai(o, "pilotageBoitier");
  c.schemaSeul = vrai(o, "schemaSeul");
  c.verrouillee = vrai(o, "verrouillee");
  c.synchroSurConfirmation = vrai(o, "synchroSurConfirmation");
  if (const J& v = champ(o, "variables"); v.is_object())
    for (auto it = v.begin(); it != v.end(); ++it)
      if (it.value().is_string() && !it.key().empty()) c.variables[it.key()] = it.value().get<std::string>();
  for (const auto& x : tableau(o, "publications")) {
    if (!objet(x, "publication")) continue;
    Publication p;
    p.id = txt(x, "id");
    p.revision = txt(x, "revision");
    p.date = txt(x, "date");
    p.auteur = txt(x, "auteur");
    p.empreinte = txt(x, "empreinte");
    p.derogation = txt(x, "derogation");
    p.dossier = txt(x, "dossier");
    for (const auto& v : tableau(x, "violations"))
      if (v.is_string()) p.violations.push_back(v.get<std::string>());
    if (x.contains("fichiers") && x["fichiers"].is_object())
      for (auto it = x["fichiers"].begin(); it != x["fichiers"].end(); ++it)
        if (it.value().is_string()) p.fichiers.push_back({it.key(), it.value().get<std::string>()});
    if (p.id.empty()) continue;
    garderInconnues(c, "publication:" + p.id, x, {"id", "revision", "date", "auteur", "empreinte", "dossier", "derogation", "violations", "fichiers"});
    c.publications.push_back(p);
  }
  if (o.is_object() && o.contains("cartouche")) {
    const J& t = o["cartouche"];
    c.cartouche = {txt(t, "titre"), txt(t, "societe"), txt(t, "auteur"), txt(t, "revision", "A"), txt(t, "date"), txt(t, "commentaire"), {}, {}};
    if (const J& ch = champ(t, "champs"); ch.is_object())
      for (auto it = ch.begin(); it != ch.end(); ++it)
        if (it.value().is_string()) c.cartouche.champs[it.key()] = it.value().get<std::string>();
    for (const auto& r : tableau(t, "historique"))
      if (r.is_object()) c.cartouche.historique.push_back({txt(r, "revision"), txt(r, "date"), txt(r, "auteur"), txt(r, "description")});
    garderInconnues(c, "cartouche", t, {"titre", "societe", "auteur", "revision", "date", "commentaire", "champs", "historique"});
  }
  for (const auto& x : tableau(o, "symboles")) {
    if (!objet(x, "symbole")) continue;
    DefSymbole d = symboleDepuisJson(x);
    if (d.id.empty()) continue;
    garderInconnues(c, "defSymbole:" + d.id, x,
                    {"id", "nom", "description", "categorie", "prefixe", "empreinte", "version", "posRef", "posVal", "motsCles", "fabricant", "mpn", "fiche", "alimentation", "drapeau",
                     "mecanique", "traversant", "liaisonReseaux", "genre", "unites", "nomsUnites", "proprietes", "broches", "traces", "parent", "style", "vues", "brochesReliees"});
    const J& br = tableau(x, "broches");
    for (std::size_t i = 0; i < br.size(); ++i)
      garderInconnues(c, "defSymbole:" + d.id + "/broche:" + std::to_string(i), br[i],
                      {"numero", "nom", "type", "p", "dir", "l", "nomVisible", "numeroVisible", "unite", "style", "section", "groupe", "rang", "alternatives", "cachee", "vue"});
    const J& tr = tableau(x, "traces");
    for (std::size_t i = 0; i < tr.size(); ++i)
      garderInconnues(c, "defSymbole:" + d.id + "/trace:" + std::to_string(i), tr[i],
                      {"forme", "points", "rayon", "debut", "fin", "rempli", "ferme", "fond", "texte", "taille", "unite", "forme034", "points034", "rayonY", "arrondi", "epaisseur", "style",
                       "couleur", "fleches", "vue"});
    c.symboles[d.id] = d;
  }
  if (const J& e = champ(o, "erc"); e.is_object()) {
    garderInconnues(c, "erc", e, {"gravites", "matrice", "exclusions"});
    if (const J& g = champ(e, "gravites"); g.is_object())
      for (auto it = g.begin(); it != g.end(); ++it)
        if (it.value().is_string()) {
          const std::string v = it.value().get<std::string>();
          c.erc.gravites[it.key()] = v == "ignorer" ? GraviteReglee::Ignorer : v == "avertissement" ? GraviteReglee::Avertissement : GraviteReglee::Erreur;
        }
    if (const J& m = champ(e, "matrice"); m.is_object())
      for (auto it = m.begin(); it != m.end(); ++it)
        if (it.value().is_string()) c.erc.matrice[it.key()] = it.value().get<std::string>();
    for (const auto& x : tableau(e, "exclusions")) {
      if (!objet(x, "exclusion ERC")) continue;
      ExclusionErc ex;
      ex.code = txt(x, "code");
      for (const auto& id : tableau(x, "objets"))
        if (id.is_string()) ex.objets.push_back(id.get<std::string>());
      ex.raison = txt(x, "raison");
      ex.auteur = txt(x, "auteur");
      ex.date = txt(x, "date");
      ex.signature = txt(x, "signature");
      if (!ex.code.empty()) c.erc.exclusions.push_back(ex);
    }
  }
  if (const J& nr = champ(o, "nomsReseaux"); nr.is_object())
    for (auto it = nr.begin(); it != nr.end(); ++it)
      if (it.value().is_string()) c.nomsReseaux[it.key()] = it.value().get<std::string>();
  // Empreintes figées dans le projet (EL-35)
  for (const auto& x : tableau(o, "bibliothequeEmpreintes")) {
    if (!objet(x, "empreinte de la bibliothèque du projet")) continue;
    DefEmpreinte d = empreinteDepuisJson(x);
    if (d.id.empty() || (d.pastilles.empty() && d.traits.empty())) {  // un logo n'a que de la sérigraphie
      ++compte.ignores["empreinte de la bibliothèque du projet"];
      continue;
    }
    garderInconnues(c, "defEmpreinte:" + d.id, x, {"id", "description", "famille", "version", "pastilles", "traits", "encombrement", "corps", "volumes"});
    garderInconnues(c, "defEmpreinte:" + d.id + "/corps", champ(x, "corps"), {"l", "p", "h", "rond", "couleur", "dx", "dy"});
    // Pastilles et traits gardés par leur rang : la lecture n'en saute aucun qui soit un objet, le rang reste le même
    std::size_t rang = 0;
    for (const auto& y : tableau(x, "pastilles"))
      if (y.is_object())
        garderInconnues(c, "defEmpreinte:" + d.id + "/pastille:" + std::to_string(rang++), y, {"numero", "type", "forme", "p", "l", "h", "rot", "percage", "arrondi", "couches", "fab"}, CLES_PASTILLE_034);
    rang = 0;
    for (const auto& y : tableau(x, "traits"))
      if (y.is_object()) garderInconnues(c, "defEmpreinte:" + d.id + "/trait:" + std::to_string(rang++), y, {"couche", "a", "b", "ep", "rayon"});
    c.empreintes[d.id] = d;
  }
  for (const auto& x : tableau(o, "composants")) {
    if (!objet(x, "composant")) continue;
    ComposantElec k;
    k.uuid = txt(x, "uuid");
    if (k.uuid.empty()) continue;
    k.reference = txt(x, "ref");
    k.valeur = txt(x, "valeur");
    k.symbole = txt(x, "symbole");
    k.empreinte = txt(x, "empreinte");
    k.fabricant = txt(x, "fabricant");
    k.mpn = txt(x, "mpn");
    k.fournisseur = txt(x, "fournisseur");
    k.description = txt(x, "description");
    k.fiche = txt(x, "fiche");
    k.nePasMonter = vrai(x, "nePasMonter");
    k.horsCarte = vrai(x, "horsCarte");
    if (x.contains("hauteur") && x["hauteur"].is_number() && x["hauteur"].get<double>() > 0) k.hauteur = std::min(x["hauteur"].get<double>(), 200.0);
    // « hauteurVerifiee » de la 0.31 est reprise par la migration (une hauteur tapée devient « saisie »)
    if (x.contains("provenanceHauteur")) {
      k.provenanceHauteur = provenanceDepuisCode(txt(x, "provenanceHauteur"));
      if (k.provenanceHauteur == ProvenanceHauteur::Generique) k.provenanceHauteur = ProvenanceHauteur::Saisie;
    }
    k.sourceHauteur = txt(x, "sourceHauteur");
    k.genre = genreComposantDepuisCode(txt(x, "genre"));
    k.horsSimulation = vrai(x, "horsSimulation");
    k.formule = txt(x, "formule");
    if (const J& pa = champ(x, "parametres"); pa.is_object())
      for (auto it = pa.begin(); it != pa.end(); ++it)
        if (it.value().is_string()) k.parametres[it.key()] = it.value().get<std::string>();
    k.brochageVerifie = txt(x, "brochageVerifie");
    k.horsNomenclature = vrai(x, "horsNomenclature");
    k.horsPlacement = vrai(x, "horsPlacement");
    if (x.contains("champs") && x["champs"].is_object())
      for (auto it = x["champs"].begin(); it != x["champs"].end(); ++it)
        if (it.value().is_string()) k.champs[it.key()] = it.value().get<std::string>();
    k.article = txt(x, "article");
    k.versionArticle = ent(x, "versionArticle");
    k.lcsc = txt(x, "lcsc");
    k.modele3D = txt(x, "modele3D");
    k.modeleComposant = txt(x, "modeleComposant");
    k.carteSeule = vrai(x, "carteSeule");
    if (const J& va = champ(x, "variantes"); va.is_object())
      for (auto it = va.begin(); it != va.end(); ++it) {
        if (!it.value().is_object() || it.key().empty()) continue;
        SurchargeVariante sv;
        const std::string m = txt(it.value(), "montage");
        sv.montage = m == "monte" ? MontageVariante::Monte : m == "nonMonte" ? MontageVariante::NonMonte : MontageVariante::Herite;
        sv.valeur = txt(it.value(), "valeur");
        sv.article = txt(it.value(), "article");
        sv.empreinte = txt(it.value(), "empreinte");
        k.variantes[it.key()] = sv;
      }
    garderInconnues(c, "composant:" + k.uuid, x,
                    {"uuid", "ref", "valeur", "symbole", "empreinte", "fabricant", "mpn", "fournisseur", "description", "fiche", "nePasMonter", "horsCarte", "hauteur", "provenanceHauteur",
                     "sourceHauteur", "genre", "horsSimulation", "formule", "parametres", "valeurLue", "brochageVerifie", "horsNomenclature", "horsPlacement", "champs", "article",
                     "versionArticle", "lcsc", "modele3D", "modeleComposant", "variantes", "carteSeule"});
    c.composants.push_back(k);
  }
  for (const auto& x : tableau(o, "feuilles")) {
    if (!objet(x, "feuille")) continue;
    Feuille f;
    f.uuid = txt(x, "uuid");
    if (f.uuid.empty()) f.uuid = nouvelUuid();
    f.nom = txt(x, "nom", f.nom);
    f.format = txt(x, "format", "A4");
    f.repetitions = ent(x, "repetitions", 0);
    f.modele = txt(x, "modele");
    f.instance = ent(x, "instance", 0);
    garderInconnues(c, "feuille:" + f.uuid, x, {"uuid", "nom", "format", "symboles", "fils", "jonctions", "nonConnexions", "etiquettes", "textes", "repetitions", "modele", "instance"},
                    CLES_FEUILLE_034);
    for (const auto& y : tableau(x, "symboles")) {
      if (!objet(y, "symbole placé")) continue;
      SymbolePlace s;
      s.uuid = txt(y, "uuid");
      s.symbole = txt(y, "symbole");
      s.composant = txt(y, "composant");
      s.nomAlim = txt(y, "alim");
      s.position = lirePt(champ(y, "p"));
      s.rotation = ((ent(y, "rot") % 4) + 4) % 4;
      s.miroir = vrai(y, "miroir");
      s.unite = std::clamp(ent(y, "unite", 1), 1, 64);
      if (const J& n = champ(y, "numeros"); n.is_object())
        for (const auto& [de, vers] : n.items())
          if (vers.is_string() && !de.empty()) s.numeros[de] = vers.get<std::string>();
      for (const auto& [cle, ch] : {std::pair{"champRef", &s.champReference}, std::pair{"champVal", &s.champValeur}})
        if (const J& q = champ(y, cle); q.is_object()) *ch = ChampPlace{lirePt(champ(q, "d")), vrai(q, "bloque")};
      s.vue = std::clamp(ent(y, "vue", 1), 1, 2);
      if (const J& fo = champ(y, "fonctions"); fo.is_object())
        for (const auto& [de, vers] : fo.items())
          if (vers.is_string()) s.fonctions[de] = vers.get<std::string>();
      s.nomsMasques = vrai(y, "nomsMasques");
      if (s.uuid.empty() || s.symbole.empty()) continue;
      garderInconnues(c, "symbolePlace:" + s.uuid, y, {"uuid", "symbole", "composant", "alim", "p", "rot", "miroir", "unite", "numeros", "champRef", "champVal", "vue", "fonctions", "nomsMasques"});
      f.symboles.push_back(s);
    }
    for (const auto& y : tableau(x, "fils"))
      if (objet(y, "fil")) {
        f.fils.push_back({txt(y, "uuid", nouvelUuid()), lirePt(champ(y, "a")), lirePt(champ(y, "b"))});
        if (y.contains("rayon") && y["rayon"].is_number_integer()) f.fils.back().rayon = std::max<Nm>(0, y["rayon"].get<Nm>());
        garderInconnues(c, "fil:" + f.fils.back().uuid, y, {"uuid", "a", "b", "rayon"});
      }
    // Identifiant lu, sinon tiré de la feuille et de la position : relire le même fichier donne les mêmes (S26)
    auto idLu = [&](const J& y, const char* genre, std::size_t rang) {
      if (y.is_array() && y.size() >= 3 && y[2].is_string() && !y[2].get<std::string>().empty()) return y[2].get<std::string>();
      const Pt p = lirePt(y);
      return uuidDerive(f.uuid + ':' + genre + ':' + std::to_string(p.x) + ':' + std::to_string(p.y) + ':' + std::to_string(rang));
    };
    {
      std::size_t rang = 0;
      for (const auto& y : tableau(x, "jonctions")) f.jonctions.push_back({lirePt(y), idLu(y, "jonction", rang++)});
      rang = 0;
      for (const auto& y : tableau(x, "nonConnexions")) f.nonConnexions.push_back({lirePt(y), idLu(y, "nc", rang++)});
    }
    for (const char* liste : {"etiquettes", "directives"})
      for (const auto& y : tableau(x, liste))
        if (objet(y, "étiquette")) {
          const bool directive = std::string(liste) == "directives";
          const TypeEtiquette type = directive ? TypeEtiquette::Directive
                                     : vrai(y, "hierarchique") ? TypeEtiquette::Hierarchique
                                     : vrai(y, "horsFeuille") ? TypeEtiquette::HorsFeuille
                                     : vrai(y, "globale") ? TypeEtiquette::Globale
                                                          : TypeEtiquette::Locale;
          Etiquette et{txt(y, "uuid", nouvelUuid()), txt(y, "texte"), lirePt(champ(y, "p")), ent(y, "rot"), type, sensSignalDepuis(txt(y, "sens")), {}};
          if (const J& pr = champ(y, "proprietes"); pr.is_object())
            for (auto it = pr.begin(); it != pr.end(); ++it)
              if (it.value().is_string()) et.proprietes[it.key()] = it.value().get<std::string>();
          f.etiquettes.push_back(et);
          garderInconnues(c, "etiquette:" + f.etiquettes.back().uuid, y, {"uuid", "texte", "p", "rot", "globale", "horsFeuille", "sens", "hierarchique", "proprietes"});
        }
    lireFeuille034(x, f);
    for (const auto& y : tableau(x, "textes"))
      if (objet(y, "texte de schéma")) {
        f.textes.push_back({txt(y, "uuid", nouvelUuid()), txt(y, "texte"), lirePt(champ(y, "p")), entier(y, "taille", 1'800'000), ((static_cast<int>(num(y, "rot")) % 4) + 4) % 4});
        garderInconnues(c, "texteSchema:" + f.textes.back().uuid, y, {"uuid", "texte", "p", "taille", "rot"});
      }
    // Ordre stable (ECH-10) : celui de l'écriture
    trierEnPlace(f.symboles, [](const SymbolePlace& a) { return a.uuid; });
    trierEnPlace(f.fils, [](const Fil& a) { return a.uuid; });
    trierEnPlace(f.etiquettes, [](const Etiquette& a) { return a.uuid; });
    trierEnPlace(f.textes, [](const TexteSchema& a) { return a.uuid; });
    trierEnPlace(f.jonctions, [](const Jonction& a) { return std::pair{a.p.x, a.p.y}; });
    trierEnPlace(f.nonConnexions, [](const NonConnexion& a) { return std::pair{a.p.x, a.p.y}; });
    c.feuilles.push_back(f);
  }
  if (c.feuilles.empty()) {
    Feuille f;
    f.uuid = nouvelUuid();
    c.feuilles.push_back(f);
  }
  trierEnPlace(c.composants, [](const ComposantElec& a) { return a.uuid; });
  if (o.is_object() && o.contains("circuit") && o["circuit"].is_object()) {
    const J& k = o["circuit"];
    Circuit& ci = c.circuit;
    garderInconnues(c, "circuit", k, {"contour", "affichage", "empilage", "regles", "serigraphie", "fabrication", "empreintes", "pistes", "vias", "zones", "textes", "volumes", "dessins"},
                    CLES_CIRCUIT_034);
    garderInconnues(c, "circuit/contour", champ(k, "contour"), {"origine", "largeur", "hauteur", "rayon", "libre", "decoupes", "traitements"});
    garderInconnues(c, "circuit/affichage", champ(k, "affichage"), {"unite", "grille", "opacite"}, CLES_AFFICHAGE_034);
    garderInconnues(c, "circuit/empilage", champ(k, "empilage"), {"couches", "epaisseur", "cuivre", "materiau", "finition", "vernis", "serigraphie", "pile", "gabarit", "vias"});
    garderInconnues(c, "circuit/serigraphie", champ(k, "serigraphie"), {"reperes", "valeurs", "hauteur"});
    garderInconnues(c, "circuit/fabrication", champ(k, "fabrication"), {"margeVernis", "viasCouvertes", "tauxPate", "seuilPate", "margePate", "pontVernisMin", "procede", "densite", "formatNumeroSerie", "fentesG85"});
    if (k.contains("contour")) {
      const J& t = k["contour"];
      ci.contour.origine = lirePt(champ(t, "origine"));
      ci.contour.largeur = std::max<Nm>(entier(t, "largeur", ci.contour.largeur), NM_PAR_MM);
      ci.contour.hauteur = std::max<Nm>(entier(t, "hauteur", ci.contour.hauteur), NM_PAR_MM);
      ci.contour.rayon = std::max<Nm>(entier(t, "rayon", ci.contour.rayon), 0);
      ci.contour.libre = lirePoints(champ(t, "libre"));
      ci.contour.decoupes.clear();
      if (t.contains("decoupes") && t["decoupes"].is_array())
        for (const J& x : t["decoupes"])
          if (Polygone d = lirePoints(x); d.size() >= 3) ci.contour.decoupes.push_back(std::move(d));
      for (const auto& x : tableau(t, "traitements")) {
        if (!x.is_object()) continue;
        ContourCarte::Traitement tr;
        const std::string g = txt(x, "genre");
        tr.genre = g == "bordMetallise" ? ContourCarte::Traitement::Genre::BordMetallise : g == "demiTrous" ? ContourCarte::Traitement::Genre::DemiTrous : ContourCarte::Traitement::Genre::Biseau;
        tr.premiere = std::max(0, ent(x, "de"));
        tr.derniere = std::max(tr.premiere, ent(x, "a"));
        tr.angle = num(x, "angle", 30);
        tr.profondeur = std::max<Nm>(entier(x, "profondeur"), 0);
        ci.contour.traitements.push_back(tr);
      }
    }
    if (k.contains("affichage") && k["affichage"].is_object()) {
      const J& t = k["affichage"];
      const std::string u = t.contains("unite") && t["unite"].is_string() ? t["unite"].get<std::string>() : std::string("mm");
      ci.affichage.unite = u == "mil" || u == "in" ? u : "mm";
      ci.affichage.grille = std::clamp<Nm>(entier(t, "grille", 0), 0, 10 * NM_PAR_MM);
      ci.affichage.opaciteInactives = t.contains("opacite") && t["opacite"].is_number() ? std::clamp(t["opacite"].get<double>(), 0.05, 1.0) : 0.45;
      lireAffichage034(t, ci.affichage);
    }
    if (k.contains("empilage")) {
      const J& t = k["empilage"];
      const int n = ent(t, "couches", 2);
      // 2 à 32 cuivres ; un nombre impair au-delà de 1 est permis (8.20 : empilage impair ou asymétrique)
      ci.empilage.couchesCuivre = std::clamp(n, 1, MAX_CUIVRES);
      lireEmpilage034(t, ci.empilage);
      ci.empilage.epaisseur = std::clamp(num(t, "epaisseur", 1.6), 0.2, 6.0);
      ci.empilage.cuivre = std::clamp(num(t, "cuivre", 0.035), 0.009, 0.21);
      ci.empilage.materiau = txt(t, "materiau", ci.empilage.materiau);
      ci.empilage.finition = txt(t, "finition", ci.empilage.finition);
      ci.empilage.vernis = txt(t, "vernis", ci.empilage.vernis);
      ci.empilage.serigraphie = txt(t, "serigraphie", ci.empilage.serigraphie);
    }
    if (k.contains("regles")) ci.regles = reglesDepuisJson(k["regles"], c);
    if (const J& sg = champ(k, "serigraphie"); sg.is_object()) {
      ci.serigraphie.reperes = vrai(sg, "reperes", true);
      ci.serigraphie.valeurs = vrai(sg, "valeurs", false);
      ci.serigraphie.hauteur = std::clamp<Nm>(entier(sg, "hauteur", ci.serigraphie.hauteur), 500'000, 5'000'000);
    }
    if (const J& fa = champ(k, "fabrication"); fa.is_object()) {
      ci.fabrication.margeVernis = std::clamp<Nm>(entier(fa, "margeVernis", ci.fabrication.margeVernis), 0, 500'000);
      ci.fabrication.viasCouvertes = vrai(fa, "viasCouvertes", true);
      ci.fabrication.tauxPateGrandes = std::clamp(num(fa, "tauxPate", ci.fabrication.tauxPateGrandes), 0.3, 1.0);
      ci.fabrication.seuilGrandePastille = std::clamp(num(fa, "seuilPate", ci.fabrication.seuilGrandePastille), 0.5, 100.0);
      ci.fabrication.margePate = std::clamp<Nm>(entier(fa, "margePate"), -500'000, 500'000);
      ci.fabrication.pontVernisMin = std::clamp<Nm>(entier(fa, "pontVernisMin"), 0, 1'000'000);
      ci.fabrication.procede = txt(fa, "procede", ci.fabrication.procede);
      if (const std::string d = txt(fa, "densite"); d == "M" || d == "N" || d == "L") ci.fabrication.densite = d[0];
      ci.fabrication.formatNumeroSerie = txt(fa, "formatNumeroSerie");
      ci.fabrication.fentesG85 = vrai(fa, "fentesG85");
    }
    for (const auto& y : tableau(k, "empreintes")) {
      if (!objet(y, "empreinte placée")) continue;
      EmpreintePlacee e;
      e.composant = txt(y, "composant");
      e.empreinte = txt(y, "empreinte");
      e.position = lirePt(champ(y, "p"));
      e.rotation = num(y, "rot");
      e.dessous = vrai(y, "dessous");
      e.verrou = vrai(y, "verrou");
      if (y.contains("repere")) e.repere = lirePt(champ(y, "repere"));
      e.copieLocale = txt(y, "copieLocale");
      e.couleur = txt(y, "couleur");
      if (e.composant.empty()) continue;
      garderInconnues(c, "empreintePlacee:" + e.composant, y, {"composant", "empreinte", "p", "rot", "dessous", "verrou", "repere", "copieLocale", "couleur"});
      ci.empreintes.push_back(e);
    }
    for (const auto& y : tableau(k, "pistes")) {
      if (!objet(y, "piste")) continue;
      Piste p;
      p.uuid = txt(y, "uuid", nouvelUuid());
      p.reseau = txt(y, "reseau");
      p.couche = couche(y, "couche", Couche::CuivreDessus);
      p.a = lirePt(champ(y, "a"));
      p.b = lirePt(champ(y, "b"));
      p.largeur = std::max<Nm>(entier(y, "largeur", p.largeur), 10'000);
      lirePiste034(y, p);
      garderInconnues(c, "piste:" + p.uuid, y, {"uuid", "reseau", "couche", "a", "b", "largeur"}, CLES_PISTE_034);
      ci.pistes.push_back(p);
    }
    for (const auto& y : tableau(k, "vias")) {
      if (!objet(y, "via")) continue;
      Via v;
      v.uuid = txt(y, "uuid", nouvelUuid());
      v.reseau = txt(y, "reseau");
      v.p = lirePt(champ(y, "p"));
      v.diametre = std::max<Nm>(entier(y, "diametre", v.diametre), 100'000);
      v.percage = std::clamp<Nm>(entier(y, "percage", v.percage), 50'000, v.diametre);
      lireVia034(y, v);
      garderInconnues(c, "via:" + v.uuid, y, {"uuid", "reseau", "p", "diametre", "percage"}, CLES_VIA_034);
      ci.vias.push_back(v);
    }
    for (const auto& y : tableau(k, "zones")) {
      if (!objet(y, "zone")) continue;
      Zone z;
      z.uuid = txt(y, "uuid", nouvelUuid());
      z.reseau = txt(y, "reseau");
      z.couche = couche(y, "couche", Couche::CuivreDessous);
      z.contour = lirePoints(champ(y, "contour"));
      z.priorite = ent(y, "priorite");
      z.isolement = entier(y, "isolement");
      z.epaisseurMin = entier(y, "epaisseurMin", z.epaisseurMin);
      z.thermique = vrai(y, "thermique", true);
      z.largeurRayon = entier(y, "largeurRayon", z.largeurRayon);
      z.ecartThermique = entier(y, "ecartThermique", z.ecartThermique);
      lireZone034(y, z);
      garderInconnues(c, "zone:" + z.uuid, y, {"uuid", "reseau", "couche", "contour", "priorite", "isolement", "epaisseurMin", "thermique", "largeurRayon", "ecartThermique"}, CLES_ZONE_034);
      ci.zones.push_back(z);
    }
    for (const auto& y : tableau(k, "textes"))
      if (objet(y, "texte de carte")) {
        ci.textes.push_back({txt(y, "uuid", nouvelUuid()), txt(y, "texte"), couche(y, "couche", Couche::SerigraphieDessus), lirePt(champ(y, "p")), entier(y, "hauteur", 1'000'000), num(y, "rot")});
        if (y.contains("miroir") && y["miroir"].is_boolean()) ci.textes.back().miroir = y["miroir"].get<bool>();
        ci.textes.back().police = txt(y, "police");
        ci.textes.back().reserve = vrai(y, "reserve");
        garderInconnues(c, "texteCarte:" + ci.textes.back().uuid, y, {"uuid", "texte", "couche", "p", "hauteur", "rot", "miroir", "police", "reserve"});
      }
    ci.volumes = volumesDepuisJson(champ(k, "volumes"));
    for (const auto& y : tableau(k, "dessins")) {
      if (!y.is_object()) continue;
      DessinCircuit d;
      d.uuid = txt(y, "uuid", nouvelUuid());
      const std::string f = y.contains("forme034") ? txt(y, "forme034") : txt(y, "forme");
      d.forme = f == "arc" ? DessinCircuit::Forme::Arc : f == "cercle" ? DessinCircuit::Forme::Cercle : f == "polygone" ? DessinCircuit::Forme::Polygone : f == "rectangle" ? DessinCircuit::Forme::Rectangle
              : f == "bezier" ? DessinCircuit::Forme::Bezier : DessinCircuit::Forme::Trait;
      d.couche = couche(y, "couche", Couche::SerigraphieDessus);
      d.points = lirePoints(champ(y, y.contains("points034") ? "points034" : "points"));
      d.epaisseur = entier(y, "epaisseur", 150'000);
      d.plein = vrai(y, "plein");
      d.hachure = vrai(y, "hachure");
      d.rayon = std::max<Nm>(entier(y, "rayon"), 0);
      ci.dessins.push_back(d);
    }
    lireCircuit034(k, ci);
    trierEnPlace(ci.empreintes, [](const EmpreintePlacee& a) { return a.composant; });
    trierEnPlace(ci.pistes, [](const Piste& a) { return a.uuid; });
    trierEnPlace(ci.vias, [](const Via& a) { return a.uuid; });
    trierEnPlace(ci.textes, [](const TexteCircuit& a) { return a.uuid; });
  }
  // Identifiants en double (9.7) : renouvelés, et dits
  const std::vector<std::string> doublons = reparerDoublons(c);
  if (alertes) {
    const std::string ctx = "Carte « " + c.nom + " » : ";
    for (const auto& n : doublons) alertes->push_back(ctx + n + ".");
    if (!compte.ignores.empty()) {
      std::string liste;
      for (const auto& [quoi, n] : compte.ignores) liste += (liste.empty() ? "" : ", ") + quoi + " × " + std::to_string(n);
      alertes->push_back(ctx + "objets illisibles ignorés, le reste est gardé (" + liste + ").");
    }
    if (compte.bornes) alertes->push_back(ctx + std::to_string(compte.bornes) + " coordonnée(s) aberrante(s) ramenée(s) à ±10 m.");
  }
  return c;
}
}  // namespace

}  // namespace prom::ecad

namespace prom::ecad {
// Styles du schéma seuls, pour les jeux de réglages (7.25)
nlohmann::ordered_json reglagesSchemaVersJson(const ReglagesSchema& r) { return reglagesSchemaJson(r); }
ReglagesSchema reglagesSchemaDepuisJson(const nlohmann::ordered_json& j) { return lireReglagesSchema(j); }
}  // namespace prom::ecad

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
  J b = J::array();
  for (const auto& x : d.broches) {
    J y = {{"numero", x.numero}, {"nom", x.nom}, {"type", codeTypeBroche(x.type)}, {"p", pt(x.extremite)}, {"dir", x.direction}, {"l", x.longueur}, {"nomVisible", x.nomVisible}, {"numeroVisible", x.numeroVisible}};
    if (x.unite) y["unite"] = x.unite;
    b.push_back(y);
  }
  o["broches"] = b;
  J t = J::array();
  for (const auto& x : d.traces) {
    J y = {{"forme", static_cast<int>(x.forme)}, {"points", points(x.points)}};
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
    d.broches.push_back(b);
  }
  for (const auto& x : tableau(o, "traces")) {
    Trace t;
    t.forme = static_cast<Trace::Forme>(std::clamp(ent(x, "forme"), 0, 4));
    t.points = lirePoints(champ(x, "points"));
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
  rendreInconnues(ce, "circuit/regles", o);
  return o;
}

Regles reglesDepuisJson(const J& o, ConceptionElec& ce) {
  garderInconnues(ce, "circuit/regles", o,
                  {"isolement", "largeur", "largeurMin", "viaDiametre", "viaPercage", "anneauMin", "percageMin", "bord", "ecartTrous", "cuivreTrou", "rayonsMin", "profil", "classes",
                   "affectations", "capacitesPerso"});
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
    if (c.nom.empty()) continue;
    garderInconnues(ce, "classe:" + c.nom, x, {"nom", "isolement", "largeur", "viaDiametre", "viaPercage", "courant", "echauffement"});
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
      y["versionArticle"] = x.versionArticle;
    }
    if (!x.lcsc.empty()) y["lcsc"] = x.lcsc;
    rendreInconnues(c, "composant:" + x.uuid, y);
    comps.push_back(y);
  }
  o["composants"] = comps;
  const auto parUuid = [](const auto& x) { return x.uuid; };
  const auto parPoint = [](const auto& x) { return std::pair{x.p.x, x.p.y}; };
  J feuilles = J::array();
  for (const auto& f : c.feuilles) {
    J y = {{"uuid", f.uuid}, {"nom", f.nom}, {"format", f.format}};
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
    J e = J::array();
    for (const Etiquette* px : trie(f.etiquettes, parUuid)) {
      // Un connecteur hors feuille relie comme une globale : une version qui ne le connaît pas le lit ainsi
      J z = {{"uuid", px->uuid}, {"texte", px->texte}, {"p", pt(px->p)}, {"rot", px->rotation}, {"globale", px->type != TypeEtiquette::Locale}};
      if (px->type == TypeEtiquette::HorsFeuille) z["horsFeuille"] = true;
      if (px->sens != SensSignal::Aucun) z["sens"] = codeSensSignal(px->sens);
      rendreInconnues(c, "etiquette:" + px->uuid, z);
      e.push_back(z);
    }
    y["etiquettes"] = e;
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
      for (const auto& ex : c.erc.exclusions) x.push_back({{"code", ex.code}, {"objets", ex.objets}, {"raison", ex.raison}, {"auteur", ex.auteur}, {"date", ex.date}});
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
  const Circuit& k = c.circuit;
  J ci = {{"contour", {{"origine", pt(k.contour.origine)}, {"largeur", k.contour.largeur}, {"hauteur", k.contour.hauteur}, {"rayon", k.contour.rayon}, {"libre", points(k.contour.libre)}}},
          {"empilage", {{"couches", k.empilage.couchesCuivre}, {"epaisseur", k.empilage.epaisseur}, {"cuivre", k.empilage.cuivre}, {"materiau", k.empilage.materiau}, {"finition", k.empilage.finition},
                        {"vernis", k.empilage.vernis}, {"serigraphie", k.empilage.serigraphie}}},
          {"regles", reglesVersJson(k.regles, c)},
          {"serigraphie", {{"reperes", k.serigraphie.reperes}, {"valeurs", k.serigraphie.valeurs}, {"hauteur", k.serigraphie.hauteur}}},
          {"fabrication", {{"margeVernis", k.fabrication.margeVernis}, {"viasCouvertes", k.fabrication.viasCouvertes}, {"tauxPate", k.fabrication.tauxPateGrandes},
                           {"seuilPate", k.fabrication.seuilGrandePastille}}}};
  if (k.affichage.unite != "mm" || k.affichage.grille != 0 || k.affichage.opaciteInactives != 0.45)
    ci["affichage"] = {{"unite", k.affichage.unite}, {"grille", k.affichage.grille}, {"opacite", k.affichage.opaciteInactives}};
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
    rendreInconnues(c, "empreintePlacee:" + px->composant, y);
    em.push_back(y);
  }
  ci["empreintes"] = em;
  J pi = J::array();
  for (const Piste* px : trie(k.pistes, parUuid)) {
    J y = {{"uuid", px->uuid}, {"reseau", px->reseau}, {"couche", codeCouche(px->couche)}, {"a", pt(px->a)}, {"b", pt(px->b)}, {"largeur", px->largeur}};
    rendreInconnues(c, "piste:" + px->uuid, y);
    pi.push_back(y);
  }
  ci["pistes"] = pi;
  J vi = J::array();
  for (const Via* px : trie(k.vias, parUuid)) {
    J y = {{"uuid", px->uuid}, {"reseau", px->reseau}, {"p", pt(px->p)}, {"diametre", px->diametre}, {"percage", px->percage}};
    rendreInconnues(c, "via:" + px->uuid, y);
    vi.push_back(y);
  }
  ci["vias"] = vi;
  J zo = J::array();
  for (const auto& x : k.zones) {  // dans leur ordre : il départage deux zones de même priorité
    J y = {{"uuid", x.uuid}, {"reseau", x.reseau}, {"couche", codeCouche(x.couche)}, {"contour", points(x.contour)}, {"priorite", x.priorite}, {"isolement", x.isolement},
           {"epaisseurMin", x.epaisseurMin}, {"thermique", x.thermique}, {"largeurRayon", x.largeurRayon}, {"ecartThermique", x.ecartThermique}};
    rendreInconnues(c, "zone:" + x.uuid, y);
    zo.push_back(y);
  }
  ci["zones"] = zo;
  J te = J::array();
  for (const TexteCircuit* px : trie(k.textes, parUuid)) {
    J y = {{"uuid", px->uuid}, {"texte", px->texte}, {"couche", codeCouche(px->couche)}, {"p", pt(px->p)}, {"hauteur", px->hauteur}, {"rot", px->rotation}};
    rendreInconnues(c, "texteCarte:" + px->uuid, y);
    te.push_back(y);
  }
  ci["textes"] = te;
  if (!k.volumes.empty()) ci["volumes"] = volumesVersJson(k.volumes);
  rendreInconnues(c, "circuit", ci);
  o["circuit"] = ci;
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
                   "erc", "bibliothequeEmpreintes", "circuit", "synchroSurConfirmation", "variables", "articles"});
  for (const auto& x : tableau(o, "articles"))
    if (x.is_object())
      if (Article a = articleDepuisJson(x); !a.id.empty()) c.articles[a.id] = a;
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
    c.cartouche = {txt(t, "titre"), txt(t, "societe"), txt(t, "auteur"), txt(t, "revision", "A"), txt(t, "date"), txt(t, "commentaire")};
    garderInconnues(c, "cartouche", t, {"titre", "societe", "auteur", "revision", "date", "commentaire"});
  }
  for (const auto& x : tableau(o, "symboles")) {
    if (!objet(x, "symbole")) continue;
    DefSymbole d = symboleDepuisJson(x);
    if (d.id.empty()) continue;
    garderInconnues(c, "defSymbole:" + d.id, x,
                    {"id", "nom", "description", "categorie", "prefixe", "empreinte", "version", "posRef", "posVal", "motsCles", "fabricant", "mpn", "fiche", "alimentation", "drapeau",
                     "mecanique", "traversant", "liaisonReseaux", "genre", "unites", "nomsUnites", "proprietes", "broches", "traces"});
    const J& br = tableau(x, "broches");
    for (std::size_t i = 0; i < br.size(); ++i)
      garderInconnues(c, "defSymbole:" + d.id + "/broche:" + std::to_string(i), br[i], {"numero", "nom", "type", "p", "dir", "l", "nomVisible", "numeroVisible", "unite"});
    const J& tr = tableau(x, "traces");
    for (std::size_t i = 0; i < tr.size(); ++i)
      garderInconnues(c, "defSymbole:" + d.id + "/trace:" + std::to_string(i), tr[i], {"forme", "points", "rayon", "debut", "fin", "rempli", "ferme", "fond", "texte", "taille", "unite"});
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
      if (y.is_object()) garderInconnues(c, "defEmpreinte:" + d.id + "/pastille:" + std::to_string(rang++), y, {"numero", "type", "forme", "p", "l", "h", "rot", "percage", "arrondi", "couches", "fab"});
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
    garderInconnues(c, "composant:" + k.uuid, x,
                    {"uuid", "ref", "valeur", "symbole", "empreinte", "fabricant", "mpn", "fournisseur", "description", "fiche", "nePasMonter", "horsCarte", "hauteur", "provenanceHauteur",
                     "sourceHauteur", "genre", "horsSimulation", "formule", "parametres", "valeurLue", "brochageVerifie", "horsNomenclature", "horsPlacement", "champs", "article",
                     "versionArticle", "lcsc"});
    c.composants.push_back(k);
  }
  for (const auto& x : tableau(o, "feuilles")) {
    if (!objet(x, "feuille")) continue;
    Feuille f;
    f.uuid = txt(x, "uuid");
    if (f.uuid.empty()) f.uuid = nouvelUuid();
    f.nom = txt(x, "nom", f.nom);
    f.format = txt(x, "format", "A4");
    garderInconnues(c, "feuille:" + f.uuid, x, {"uuid", "nom", "format", "symboles", "fils", "jonctions", "nonConnexions", "etiquettes", "textes"});
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
      if (s.uuid.empty() || s.symbole.empty()) continue;
      garderInconnues(c, "symbolePlace:" + s.uuid, y, {"uuid", "symbole", "composant", "alim", "p", "rot", "miroir", "unite", "numeros", "champRef", "champVal"});
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
    for (const auto& y : tableau(x, "etiquettes"))
      if (objet(y, "étiquette")) {
        const TypeEtiquette type = vrai(y, "horsFeuille") ? TypeEtiquette::HorsFeuille : vrai(y, "globale") ? TypeEtiquette::Globale : TypeEtiquette::Locale;
        f.etiquettes.push_back({txt(y, "uuid", nouvelUuid()), txt(y, "texte"), lirePt(champ(y, "p")), ent(y, "rot"), type, sensSignalDepuis(txt(y, "sens"))});
        garderInconnues(c, "etiquette:" + f.etiquettes.back().uuid, y, {"uuid", "texte", "p", "rot", "globale", "horsFeuille", "sens"});
      }
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
    garderInconnues(c, "circuit", k, {"contour", "affichage", "empilage", "regles", "serigraphie", "fabrication", "empreintes", "pistes", "vias", "zones", "textes", "volumes"});
    garderInconnues(c, "circuit/contour", champ(k, "contour"), {"origine", "largeur", "hauteur", "rayon", "libre", "decoupes"});
    garderInconnues(c, "circuit/affichage", champ(k, "affichage"), {"unite", "grille", "opacite"});
    garderInconnues(c, "circuit/empilage", champ(k, "empilage"), {"couches", "epaisseur", "cuivre", "materiau", "finition", "vernis", "serigraphie"});
    garderInconnues(c, "circuit/serigraphie", champ(k, "serigraphie"), {"reperes", "valeurs", "hauteur"});
    garderInconnues(c, "circuit/fabrication", champ(k, "fabrication"), {"margeVernis", "viasCouvertes", "tauxPate", "seuilPate"});
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
    }
    if (k.contains("affichage") && k["affichage"].is_object()) {
      const J& t = k["affichage"];
      const std::string u = t.contains("unite") && t["unite"].is_string() ? t["unite"].get<std::string>() : std::string("mm");
      ci.affichage.unite = u == "mil" || u == "in" ? u : "mm";
      ci.affichage.grille = std::clamp<Nm>(entier(t, "grille", 0), 0, 10 * NM_PAR_MM);
      ci.affichage.opaciteInactives = t.contains("opacite") && t["opacite"].is_number() ? std::clamp(t["opacite"].get<double>(), 0.05, 1.0) : 0.45;
    }
    if (k.contains("empilage")) {
      const J& t = k["empilage"];
      const int n = ent(t, "couches", 2);
      ci.empilage.couchesCuivre = n >= 4 ? 4 : n == 1 ? 1 : 2;
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
      if (e.composant.empty()) continue;
      garderInconnues(c, "empreintePlacee:" + e.composant, y, {"composant", "empreinte", "p", "rot", "dessous", "verrou"});
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
      garderInconnues(c, "piste:" + p.uuid, y, {"uuid", "reseau", "couche", "a", "b", "largeur"});
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
      garderInconnues(c, "via:" + v.uuid, y, {"uuid", "reseau", "p", "diametre", "percage"});
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
      garderInconnues(c, "zone:" + z.uuid, y, {"uuid", "reseau", "couche", "contour", "priorite", "isolement", "epaisseurMin", "thermique", "largeurRayon", "ecartThermique"});
      ci.zones.push_back(z);
    }
    for (const auto& y : tableau(k, "textes"))
      if (objet(y, "texte de carte")) {
        ci.textes.push_back({txt(y, "uuid", nouvelUuid()), txt(y, "texte"), couche(y, "couche", Couche::SerigraphieDessus), lirePt(champ(y, "p")), entier(y, "hauteur", 1'000'000), num(y, "rot")});
        garderInconnues(c, "texteCarte:" + ci.textes.back().uuid, y, {"uuid", "texte", "couche", "p", "hauteur", "rot"});
      }
    ci.volumes = volumesDepuisJson(champ(k, "volumes"));
    trierEnPlace(ci.empreintes, [](const EmpreintePlacee& a) { return a.composant; });
    trierEnPlace(ci.pistes, [](const Piste& a) { return a.uuid; });
    trierEnPlace(ci.vias, [](const Via& a) { return a.uuid; });
    trierEnPlace(ci.textes, [](const TexteCircuit& a) { return a.uuid; });
  }
  if (alertes) {
    const std::string ctx = "Carte « " + c.nom + " » : ";
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

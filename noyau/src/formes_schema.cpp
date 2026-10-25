// Prométhée : formes de la feuille (7.19) et jeux de styles (7.25). Voir schema_avance.hpp.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>

#include "promethee/dxf_dessin.hpp"
#include "promethee/lecture_svg.hpp"
#include "promethee/schema_avance.hpp"

namespace prom::ecad {

namespace {
Nm mm(double v) { return static_cast<Nm>(std::llround(v * 1e6)); }
Feuille& feuilleDe(ConceptionElec& c, std::size_t i) {
  if (i >= c.feuilles.size()) throw std::out_of_range("feuille inconnue");
  return c.feuilles[i];
}
int prochainOrdre(const Feuille& f) {
  int o = 0;
  for (const auto& x : f.formes) o = std::max(o, x.ordre + 1);
  return o;
}
}  // namespace

BilanImportFormes importerDxfFeuille(ConceptionElec& c, std::size_t feuille, const std::string& texteDxf, const std::set<std::string>& calques, Pt ou, int calqueNotes,
                                     double echelle) {
  exigerModifiable(c);
  Feuille& f = feuilleDe(c, feuille);
  BilanImportFormes b;
  const dxf::DessinDxf d = dxf::lireDessinDxf(texteDxf);
  b.alertes = d.alertes;
  auto garde = [&](const std::string& calque) {
    if (!calques.empty()) return calques.count(calque) > 0;
    const auto* k = d.calque(calque);
    return !k || k->visible;
  };
  // Encombrement des traits retenus (mm, Y vers le haut)
  double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
  auto etendre = [&](double x, double y) {
    x0 = std::min(x0, x);
    y0 = std::min(y0, y);
    x1 = std::max(x1, x);
    y1 = std::max(y1, y);
  };
  for (const auto& t : d.traits) {
    if (!garde(t.calque)) continue;
    if (t.genre == dxf::TraitDxf::Genre::Cercle || t.genre == dxf::TraitDxf::Genre::Arc) {
      etendre(t.centre.x - t.rayon, t.centre.y - t.rayon);
      etendre(t.centre.x + t.rayon, t.centre.y + t.rayon);
    } else {
      etendre(t.a.x, t.a.y);
      etendre(t.b.x, t.b.y);
    }
  }
  if (x0 > x1) {
    b.alertes.push_back("aucun trait sur les calques choisis");
    return b;
  }
  // Coin bas-gauche du dessin en « ou » ; Y du schéma vers le bas
  auto vers = [&](double x, double y) { return Pt{ou.x + mm((x - x0) * echelle), ou.y - mm((y - y0) * echelle)}; };
  int ordre = prochainOrdre(f);
  for (const auto& t : d.traits) {
    if (!garde(t.calque)) continue;
    FormeSchema s;
    s.uuid = nouvelUuid();
    s.calque = std::clamp(calqueNotes, 0, 7);
    s.ordre = ordre++;
    if (const auto* k = d.calque(t.calque)) s.trace.couleur = dxf::couleurAci(k->couleur);
    switch (t.genre) {
      case dxf::TraitDxf::Genre::Ligne:
        s.trace.forme = Trace::Forme::Polyligne;
        s.trace.points = {vers(t.a.x, t.a.y), vers(t.b.x, t.b.y)};
        break;
      case dxf::TraitDxf::Genre::Arc: {
        s.trace.forme = Trace::Forme::Arc;
        s.trace.points = {vers(t.centre.x, t.centre.y)};
        s.trace.rayon = mm(t.rayon * echelle);
        // Angles vus à l'écran : Y retourné mais le dessin garde son aspect, les angles restent ceux du DXF
        s.trace.debut = std::atan2(t.a.y - t.centre.y, t.a.x - t.centre.x) * 180 / 3.14159265358979323846;
        s.trace.fin = std::atan2(t.b.y - t.centre.y, t.b.x - t.centre.x) * 180 / 3.14159265358979323846;
        break;
      }
      case dxf::TraitDxf::Genre::Cercle:
        s.trace.forme = Trace::Forme::Cercle;
        s.trace.points = {vers(t.centre.x, t.centre.y)};
        s.trace.rayon = mm(t.rayon * echelle);
        break;
      case dxf::TraitDxf::Genre::Point: continue;
    }
    b.uuids.push_back(s.uuid);
    f.formes.push_back(std::move(s));
    ++b.formes;
  }
  return b;
}

BilanImportFormes importerSvgFeuille(ConceptionElec& c, std::size_t feuille, const std::string& texteSvg, Pt ou, int calqueNotes, double echelle) {
  exigerModifiable(c);
  Feuille& f = feuilleDe(c, feuille);
  BilanImportFormes b;
  const svg::DessinSvg d = svg::lireSvg(texteSvg);
  b.alertes = d.alertes;
  auto vers = [&](svg::Pt2 p) { return Pt{ou.x + mm(p.x * echelle), ou.y + mm(p.y * echelle)}; };
  int ordre = prochainOrdre(f);
  for (const auto& x : d.formes) {
    FormeSchema s;
    s.uuid = nouvelUuid();
    s.calque = std::clamp(calqueNotes, 0, 7);
    s.ordre = ordre++;
    s.trace.couleur = x.trait;
    s.trace.epaisseur = mm(x.epaisseur * echelle);
    s.trace.rempli = !x.fond.empty();
    s.trace.ferme = x.ferme;
    for (const auto& p : x.points) s.trace.points.push_back(vers(p));
    switch (x.genre) {
      case svg::FormeSvg::Genre::Polyligne: s.trace.forme = Trace::Forme::Polyligne; break;
      case svg::FormeSvg::Genre::Bezier: s.trace.forme = Trace::Forme::Bezier; break;
      case svg::FormeSvg::Genre::Cercle:
        s.trace.forme = Trace::Forme::Cercle;
        s.trace.rayon = mm(x.rx * echelle);
        break;
      case svg::FormeSvg::Genre::Ellipse:
        s.trace.forme = Trace::Forme::Ellipse;
        s.trace.rayon = mm(x.rx * echelle);
        s.trace.rayonY = mm(x.ry * echelle);
        break;
      case svg::FormeSvg::Genre::Rectangle:
        s.trace.forme = Trace::Forme::Rectangle;
        s.trace.arrondi = mm(x.rx * echelle);
        break;
    }
    b.uuids.push_back(s.uuid);
    f.formes.push_back(std::move(s));
    ++b.formes;
  }
  return b;
}

std::vector<const FormeSchema*> formesRangees(const Feuille& f, bool pourPdf) {
  (void)pourPdf;  // les calques masqués le sont à l'écran comme au PDF
  std::vector<const FormeSchema*> r;
  for (const auto& x : f.formes)
    if (std::find(f.calquesMasques.begin(), f.calquesMasques.end(), x.calque) == f.calquesMasques.end()) r.push_back(&x);
  std::stable_sort(r.begin(), r.end(), [](const FormeSchema* a, const FormeSchema* b) { return a->calque != b->calque ? a->calque < b->calque : a->ordre < b->ordre; });
  return r;
}

bool changerOrdre(Feuille& f, const std::string& forme, Superposition s) {
  auto it = std::find_if(f.formes.begin(), f.formes.end(), [&](const FormeSchema& x) { return x.uuid == forme; });
  if (it == f.formes.end()) return false;
  // Rangs du même calque, dans l'ordre
  std::vector<FormeSchema*> meme;
  for (auto& x : f.formes)
    if (x.calque == it->calque) meme.push_back(&x);
  std::stable_sort(meme.begin(), meme.end(), [](const FormeSchema* a, const FormeSchema* b) { return a->ordre < b->ordre; });
  const auto pos = static_cast<std::size_t>(std::find(meme.begin(), meme.end(), &*it) - meme.begin());
  FormeSchema* cible = meme[pos];
  meme.erase(meme.begin() + static_cast<std::ptrdiff_t>(pos));
  std::size_t nouvelle = pos;
  switch (s) {
    case Superposition::PremierPlan: nouvelle = meme.size(); break;
    case Superposition::ArrierePlan: nouvelle = 0; break;
    case Superposition::Monter: nouvelle = std::min(pos + 1, meme.size()); break;
    case Superposition::Descendre: nouvelle = pos == 0 ? 0 : pos - 1; break;
  }
  meme.insert(meme.begin() + static_cast<std::ptrdiff_t>(nouvelle), cible);
  for (std::size_t i = 0; i < meme.size(); ++i) meme[i]->ordre = static_cast<int>(i);
  return true;
}

double mesurer(const std::vector<Pt>& points) {
  double l = 0;
  for (std::size_t i = 1; i < points.size(); ++i)
    l += std::hypot(static_cast<double>(points[i].x - points[i - 1].x), static_cast<double>(points[i].y - points[i - 1].y));
  return l / 1e6;
}

// ---------- Styles et jeux (7.25) ----------
namespace {
using J = nlohmann::ordered_json;
J familleJson(const DefautsFamille& f) {
  J j = J::object();
  if (f.tailleTexte) j["tailleTexte"] = f.tailleTexte;
  if (f.epaisseurTexte) j["epaisseurTexte"] = f.epaisseurTexte;
  if (f.epaisseurTrait) j["epaisseurTrait"] = f.epaisseurTrait;
  return j;
}
Nm entierDe(const J& j, const char* cle) { return j.is_object() && j.contains(cle) && j[cle].is_number() ? std::max<Nm>(j[cle].get<Nm>(), 0) : 0; }
DefautsFamille familleDepuis(const J& j) { return {entierDe(j, "tailleTexte"), entierDe(j, "epaisseurTexte"), entierDe(j, "epaisseurTrait")}; }
J defautsJson(const DefautsCarte& d) {
  J j = J::object();
  const std::pair<const char*, const DefautsFamille*> familles[] = {
      {"cuivre", &d.cuivre}, {"serigraphie", &d.serigraphie}, {"fabrication", &d.fabrication}, {"encombrement", &d.encombrement}, {"documentation", &d.documentation}};
  for (const auto& [nom, f] : familles)
    if (!(*f == DefautsFamille{})) j[nom] = familleJson(*f);
  if (d.largeurPiste) j["largeurPiste"] = d.largeurPiste;
  if (d.diametreVia) j["diametreVia"] = d.diametreVia;
  if (d.percageVia) j["percageVia"] = d.percageVia;
  return j;
}
DefautsCarte defautsDepuis(const J& j) {
  DefautsCarte d;
  if (!j.is_object()) return d;
  auto f = [&](const char* nom) { return j.contains(nom) ? familleDepuis(j[nom]) : DefautsFamille{}; };
  d.cuivre = f("cuivre");
  d.serigraphie = f("serigraphie");
  d.fabrication = f("fabrication");
  d.encombrement = f("encombrement");
  d.documentation = f("documentation");
  d.largeurPiste = entierDe(j, "largeurPiste");
  d.diametreVia = entierDe(j, "diametreVia");
  d.percageVia = entierDe(j, "percageVia");
  return d;
}
J classeJson(const ClasseReseau& k) {
  J j = {{"nom", k.nom}, {"isolement", k.isolement}, {"largeur", k.largeur}, {"viaDiametre", k.viaDiametre}, {"viaPercage", k.viaPercage}};
  if (!k.couleur.empty()) j["couleur"] = k.couleur;
  if (k.courant > 0) j["courant"] = k.courant;
  if (k.tension > 0) j["tension"] = k.tension;
  return j;
}
ClasseReseau classeDepuis(const J& j) {
  ClasseReseau k;
  k.nom = j.value("nom", std::string());
  k.isolement = j.value("isolement", k.isolement);
  k.largeur = j.value("largeur", k.largeur);
  k.viaDiametre = j.value("viaDiametre", k.viaDiametre);
  k.viaPercage = j.value("viaPercage", k.viaPercage);
  k.couleur = j.value("couleur", std::string());
  k.courant = j.value("courant", 0.0);
  k.tension = j.value("tension", 0.0);
  return k;
}
}  // namespace

DefautsCarte defautsCarte(const ConceptionElec& c) {
  const auto it = c.extensions.find("defautsCarte");
  return it == c.extensions.end() ? DefautsCarte{} : defautsDepuis(it->second);
}

void reglerDefautsCarte(ConceptionElec& c, const DefautsCarte& d) {
  if (d == DefautsCarte{}) c.extensions.erase("defautsCarte");
  else c.extensions["defautsCarte"] = defautsJson(d);
}

const DefautsFamille& familleDefauts(const DefautsCarte& d, Couche couche) {
  switch (couche) {
    case Couche::SerigraphieDessus:
    case Couche::SerigraphieDessous: return d.serigraphie;
    case Couche::FabricationDessus:
    case Couche::FabricationDessous: return d.fabrication;
    case Couche::EncombrementDessus:
    case Couche::EncombrementDessous: return d.encombrement;
    default: break;
  }
  if (estCuivre(couche)) return d.cuivre;
  return d.documentation;
}

TexteCircuit texteParDefaut(const ConceptionElec& c, Couche couche, const std::string& texte, Pt p) {
  TexteCircuit t;
  t.uuid = nouvelUuid();
  t.texte = texte;
  t.couche = couche;
  t.p = p;
  const DefautsCarte defauts = defautsCarte(c);  // gardé en vie : familleDefauts rend une référence dedans
  const DefautsFamille& f = familleDefauts(defauts, couche);
  if (f.tailleTexte > 0) t.hauteur = f.tailleTexte;
  else if (couche == Couche::SerigraphieDessus || couche == Couche::SerigraphieDessous) t.hauteur = c.circuit.serigraphie.hauteur;
  return t;
}

DessinCircuit dessinParDefaut(const ConceptionElec& c, Couche couche) {
  DessinCircuit d;
  d.uuid = nouvelUuid();
  d.couche = couche;
  const DefautsCarte defauts = defautsCarte(c);
  const DefautsFamille& f = familleDefauts(defauts, couche);
  if (f.epaisseurTrait > 0) d.epaisseur = f.epaisseurTrait;
  return d;
}

StyleFil styleDuFil(const ConceptionElec& c, const std::string& reseau, bool bus) {
  const ReglagesSchema& r = c.reglagesSchema;
  StyleFil s = bus ? r.bus : r.fil;
  if (const ClasseReseau* k = reseau.empty() ? nullptr : classeDe(c.circuit.regles, reseau)) {
    if (const auto it = r.parClasse.find(k->nom); it != r.parClasse.end()) {
      if (!it->second.couleur.empty()) s.couleur = it->second.couleur;
      if (!it->second.style.empty()) s.style = it->second.style;
      if (it->second.epaisseur > 0) s.epaisseur = it->second.epaisseur;
    }
    if (s.couleur.empty()) s.couleur = k->couleur;
  }
  return s;
}

std::vector<JeuStyles> jeuxStylesIntegres() {
  std::vector<JeuStyles> r;
  {
    JeuStyles j;
    j.nom = "standard";
    j.description = "Réglages de la 0.33 : fils fins, sérigraphie de 1 mm";
    r.push_back(j);
  }
  {
    // Enseignement : tout plus gros et lisible au vidéoprojecteur, alimentation en couleur
    JeuStyles j;
    j.nom = "école";
    j.description = "Lisible au tableau : textes plus grands, fils épais, puissance en rouge et masse en bleu";
    j.schema.fil = {"", "", 300'000};
    j.schema.bus = {"#1565C0", "", 600'000};
    j.schema.parClasse["Puissance"] = {"#E53935", "", 500'000};
    j.schema.parClasse["Masse"] = {"#1E88E5", "", 400'000};
    j.schema.diametreJonction = 1'200'000;
    j.schema.tailleTexte = 1'778'000;
    j.schema.tailleEtiquette = 1'778'000;
    j.schema.tailleChamp = 1'524'000;
    j.carte.serigraphie = {1'200'000, 200'000, 200'000};
    j.carte.fabrication = {1'500'000, 150'000, 150'000};
    j.carte.documentation = {2'000'000, 200'000, 200'000};
    j.carte.largeurPiste = 400'000;
    j.carte.diametreVia = 1'000'000;
    j.carte.percageVia = 500'000;
    ClasseReseau puissance;
    puissance.nom = "Puissance";
    puissance.largeur = 800'000;
    puissance.isolement = 300'000;
    puissance.viaDiametre = 1'200'000;
    puissance.viaPercage = 600'000;
    puissance.couleur = "#E53935";
    ClasseReseau masse;
    masse.nom = "Masse";
    masse.largeur = 600'000;
    masse.couleur = "#1E88E5";
    j.classes = {puissance, masse};
    r.push_back(j);
  }
  {
    JeuStyles j;
    j.nom = "compact";
    j.description = "Cartes denses : sérigraphie de 0,8 mm, traits de 0,12 mm";
    j.carte.serigraphie = {800'000, 120'000, 120'000};
    j.carte.fabrication = {800'000, 100'000, 100'000};
    j.carte.largeurPiste = 150'000;
    j.carte.diametreVia = 500'000;
    j.carte.percageVia = 250'000;
    j.schema.tailleTexte = 1'016'000;
    j.schema.tailleChamp = 1'016'000;
    r.push_back(j);
  }
  return r;
}

JeuStyles jeuDeLaConception(const ConceptionElec& c, const std::string& nom) {
  JeuStyles j;
  j.nom = nom;
  j.schema = c.reglagesSchema;
  j.carte = defautsCarte(c);
  for (const auto& k : c.circuit.regles.classes)
    if (!k.couleur.empty() || c.reglagesSchema.parClasse.count(k.nom)) j.classes.push_back(k);
  return j;
}

int appliquerJeuStyles(ConceptionElec& c, const JeuStyles& j) {
  exigerModifiable(c);
  c.reglagesSchema = j.schema;
  reglerDefautsCarte(c, j.carte);
  if (j.carte.serigraphie.tailleTexte > 0) c.circuit.serigraphie.hauteur = j.carte.serigraphie.tailleTexte;
  int creees = 0;
  for (const auto& k : j.classes) {
    auto it = std::find_if(c.circuit.regles.classes.begin(), c.circuit.regles.classes.end(), [&](const ClasseReseau& x) { return x.nom == k.nom; });
    if (it == c.circuit.regles.classes.end()) {
      c.circuit.regles.classes.push_back(k);
      ++creees;
    } else if (!k.couleur.empty()) {
      it->couleur = k.couleur;
    }
  }
  return creees;
}

std::string texteJeuStyles(const JeuStyles& j) {
  J o;
  o["format"] = "promethee-styles";
  o["version"] = 1;
  o["nom"] = j.nom;
  if (!j.description.empty()) o["description"] = j.description;
  o["schema"] = reglagesSchemaVersJson(j.schema);
  o["carte"] = defautsJson(j.carte);
  J k = J::array();
  for (const auto& x : j.classes) k.push_back(classeJson(x));
  o["classes"] = k;
  return o.dump(1);
}

std::optional<JeuStyles> lireJeuStyles(const std::string& texte, std::string* erreur) {
  try {
    const J o = J::parse(texte);
    if (o.value("format", std::string()) != "promethee-styles") throw std::runtime_error("ce n'est pas un jeu de styles Prométhée");
    JeuStyles j;
    j.nom = o.value("nom", std::string());
    j.description = o.value("description", std::string());
    if (o.contains("schema")) j.schema = reglagesSchemaDepuisJson(o["schema"]);
    if (o.contains("carte")) j.carte = defautsDepuis(o["carte"]);
    if (o.contains("classes") && o["classes"].is_array())
      for (const auto& x : o["classes"]) j.classes.push_back(classeDepuis(x));
    return j;
  } catch (const std::exception& x) {
    if (erreur) *erreur = x.what();
    return std::nullopt;
  }
}

}  // namespace prom::ecad

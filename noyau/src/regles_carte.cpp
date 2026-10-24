// Prométhée : résolution des contraintes du circuit, tables d'isolement sourcées, classes de composants, règles en
// fichier. Licence GPL-3.0-only.
#include "promethee/regles_carte.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace prom::ecad {

namespace {
std::string mmTexte(Nm v) {
  char t[32];
  std::snprintf(t, sizeof t, "%.3g mm", static_cast<double>(v) * 1e-6);
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
std::string prefixeDe(const std::string& ref) {
  std::string p;
  for (char ch : ref) {
    if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '?') break;
    p += ch;
  }
  return p;
}
const ClasseReseau* classeReseau(const ConceptionElec& c, const std::string& reseau) { return classeDe(c.circuit.regles, reseau); }
}  // namespace

std::vector<std::string> regionsEn(const ConceptionElec& c, Pt p, Couches couches) {
  std::vector<std::string> r;
  for (const auto& z : c.circuit.zonesRegles)
    if ((z.couches == 0 || couches == 0 || (z.couches & couches)) && z.contour.size() >= 3 && dedans(z.contour, p)) r.push_back(z.nom.empty() ? z.uuid : z.nom);
  return r;
}

Proprietes proprietesReseau(const ConceptionElec& c, const std::string& reseau) {
  return [&c, reseau](const std::string& nom) -> std::optional<ValeurExpression> {
    if (nom == "reseau" || nom == "net") return ValeurExpression{reseau};
    if (nom == "classe" || nom == "netclass") {
      const ClasseReseau* k = classeReseau(c, reseau);
      return ValeurExpression{k ? k->nom : std::string("Défaut")};
    }
    if (nom == "genre") return ValeurExpression{std::string("reseau")};
    if (nom == "tension") {
      const ClasseReseau* k = classeReseau(c, reseau);
      return ValeurExpression{k ? k->tension : 0.0};
    }
    return std::nullopt;
  };
}

Proprietes proprietesPiste(const ConceptionElec& c, const Piste& p) {
  return [&c, &p](const std::string& nom) -> std::optional<ValeurExpression> {
    if (nom == "genre" || nom == "type") return ValeurExpression{std::string("piste")};
    if (nom == "couche" || nom == "layer") return ValeurExpression{std::string(codeCouche(p.couche))};
    if (nom == "interne") return ValeurExpression{estCuivreInterne(p.couche) ? 1.0 : 0.0};
    if (nom == "largeur" || nom == "width") return ValeurExpression{static_cast<double>(p.largeur)};
    if (nom == "longueur" || nom == "length") return ValeurExpression{longueurPiste(p)};
    if (nom == "region") {
      const auto r = regionsEn(c, {(p.a.x + p.b.x) / 2, (p.a.y + p.b.y) / 2}, bit(p.couche));
      return r.empty() ? std::nullopt : std::optional<ValeurExpression>{ValeurExpression{r.front()}};
    }
    return proprietesReseau(c, p.reseau)(nom);
  };
}

Proprietes proprietesVia(const ConceptionElec& c, const Via& v) {
  return [&c, &v](const std::string& nom) -> std::optional<ValeurExpression> {
    if (nom == "genre" || nom == "type") return ValeurExpression{std::string("via")};
    if (nom == "diametre") return ValeurExpression{static_cast<double>(v.diametre)};
    if (nom == "percage" || nom == "drill") return ValeurExpression{static_cast<double>(v.percage)};
    if (nom == "couche") return ValeurExpression{std::string(codeCouche(v.de))};
    if (nom == "typevia") {
      static const char* t[] = {"traversante", "borgne", "enterree", "micro"};
      return ValeurExpression{std::string(t[static_cast<int>(v.type)])};
    }
    if (nom == "region") {
      const auto r = regionsEn(c, v.p);
      return r.empty() ? std::nullopt : std::optional<ValeurExpression>{ValeurExpression{r.front()}};
    }
    return proprietesReseau(c, v.reseau)(nom);
  };
}

Proprietes proprietesComposant(const ConceptionElec& c, const ComposantElec& x) {
  return [&c, &x](const std::string& nom) -> std::optional<ValeurExpression> {
    if (nom == "genre" || nom == "type") return ValeurExpression{std::string("empreinte")};
    if (nom == "reference" || nom == "ref") return ValeurExpression{x.reference};
    if (nom == "prefixe") return ValeurExpression{prefixeDe(x.reference)};
    if (nom == "valeur" || nom == "value") return ValeurExpression{x.valeur};
    if (nom == "empreinte" || nom == "footprint") return ValeurExpression{x.empreinte};
    if (nom == "boitier") {
      // « 0402 », « SOT-23 », « QFN »… : la taille des passifs d'abord, sinon la famille du boîtier
      for (const char* t : {"01005", "0201", "0402", "0603", "0805", "1206", "1210", "2512"})
        if (x.empreinte.find(t) != std::string::npos) return ValeurExpression{std::string(t)};
      return ValeurExpression{familleBoitier(x.empreinte)};
    }
    if (nom == "fabricant") return ValeurExpression{x.fabricant};
    if (nom == "mpn") return ValeurExpression{x.mpn};
    if (nom == "article") return ValeurExpression{x.article};
    if (nom == "monte") return ValeurExpression{x.nePasMonter ? 0.0 : 1.0};
    if (nom == "classes" || nom == "classecomposant") {
      std::string s;
      for (const auto& k : classesDuComposant(c, x)) s += (s.empty() ? "" : ",") + k;
      return ValeurExpression{s};
    }
    for (const auto& e : c.circuit.empreintes)
      if (e.composant == x.uuid) {
        if (nom == "face" || nom == "side") return ValeurExpression{std::string(e.dessous ? "dessous" : "dessus")};
        if (nom == "rotation") return ValeurExpression{e.rotation};
        if (nom == "region") {
          const auto r = regionsEn(c, e.position);
          return r.empty() ? std::nullopt : std::optional<ValeurExpression>{ValeurExpression{r.front()}};
        }
      }
    if (const auto it = x.champs.find(nom); it != x.champs.end()) return ValeurExpression{it->second};
    if (const auto it = x.parametres.find(nom); it != x.parametres.end()) return ValeurExpression{it->second};
    return std::nullopt;
  };
}

std::vector<std::string> classesDuComposant(const ConceptionElec& c, const ComposantElec& x) {
  std::vector<std::string> r;
  for (const auto& k : c.circuit.regles.avancees.classesComposants) {
    bool dedans = std::find(k.membres.begin(), k.membres.end(), x.reference) != k.membres.end();
    if (!dedans && !k.regle.empty()) {
      const Expression e = Expression::compiler(k.regle);
      // La règle ne lit pas « classes » (sinon la classe dépendrait d'elle-même)
      dedans = e.valide() && e.evaluer([&](const std::string& nom) -> std::optional<ValeurExpression> {
        if (nom == "classes" || nom == "classecomposant") return std::nullopt;
        return proprietesComposant(c, x)(nom);
      });
    }
    if (dedans) r.push_back(k.nom);
  }
  return r;
}

std::vector<const ComposantElec*> membresClasse(const ConceptionElec& c, const ClasseComposants& k) {
  std::vector<const ComposantElec*> r;
  for (const auto& x : c.composants) {
    const auto cl = classesDuComposant(c, x);
    if (std::find(cl.begin(), cl.end(), k.nom) != cl.end()) r.push_back(&x);
  }
  return r;
}

namespace {
// Plus stricte : un minimum plus grand, sinon un maximum plus petit
bool plusStricteQue(const ValeurContrainte& a, const ValeurContrainte& b, bool interne) {
  if (a.minPour(interne) != b.minPour(interne)) return a.minPour(interne) > b.minPour(interne);
  const Nm ma = a.maxPour(interne), mb = b.maxPour(interne);
  if (ma && mb) return ma < mb;
  return ma != 0 && mb == 0;
}
bool portee(const std::string& texte, const Proprietes& p) {
  if (texte.empty()) return true;
  const Expression e = Expression::compiler(texte);
  return e.valide() && e.evaluer(p);
}
}  // namespace

Resolution resoudre(const ConceptionElec& c, GenreContrainte genre, const Proprietes& a, const Proprietes* b, bool interne) {
  struct Candidat {
    std::string nom;
    ValeurContrainte v;
    int priorite;
    std::string justification;
  };
  std::vector<Candidat> cand;
  const Regles& R = c.circuit.regles;
  // Règle générale et classes de réseaux : contraintes implicites, priorité 0
  auto implicite = [&](const char* nom, Nm valeur, const std::string& justif) {
    ValeurContrainte v;
    v.min = valeur;
    cand.push_back({nom, v, 0, justif});
  };
  switch (genre) {
    case GenreContrainte::Isolement: implicite("règle générale", R.isolement, "isolement de la carte"); break;
    case GenreContrainte::Largeur: implicite("règle générale", R.largeurMin, "largeur minimale de la carte"); break;
    case GenreContrainte::ViaDiametre: implicite("règle générale", R.viaDiametre, "via de la carte"); break;
    case GenreContrainte::ViaPercage: implicite("règle générale", R.percageMin, "perçage minimal de la carte"); break;
    case GenreContrainte::Bord: implicite("règle générale", R.bord, "distance au bord de la carte"); break;
    case GenreContrainte::CuivreTrou: implicite("règle générale", R.cuivreTrou, "distance d'un cuivre à un perçage"); break;
    default: break;
  }
  auto classeDe = [](const Proprietes& p) -> std::string {
    const auto v = p("classe");
    if (!v) return {};
    const std::string* s = std::get_if<std::string>(&*v);
    return s ? *s : std::string();
  };
  for (const Proprietes* p : {&a, b}) {
    if (!p) continue;
    const std::string nomClasse = classeDe(*p);
    for (const auto& k : R.classes) {
      if (k.nom != nomClasse) continue;
      const std::size_t avant = cand.size();
      if (genre == GenreContrainte::Isolement && k.isolement > 0)
        implicite("classe", k.isolement, "classe « " + k.nom + " »");
      if (genre == GenreContrainte::Largeur && k.largeur > 0) {
        ValeurContrainte v;
        v.pref = k.largeur;
        cand.push_back({"classe", v, 0, "classe « " + k.nom + " »"});
      }
      if (genre == GenreContrainte::ViaDiametre && k.viaDiametre > 0) implicite("classe", k.viaDiametre, "classe « " + k.nom + " »");
      if (genre == GenreContrainte::Isolement && k.tension > 0) {
        // Tension de service : la table de la carte
        const bool cei = R.avancees.tableIsolement == "CEI 60664-1";
        const double d = cei ? ligneFuiteCEI60664(k.tension / std::sqrt(2.0), 2, 3, true) : isolementIPC2221(k.tension, interne ? "B1" : "B2");
        implicite("classe", nm(d), "classe « " + k.nom + " » à " + std::to_string(static_cast<int>(k.tension)) + " V, " + (cei ? sourceCEI60664() : sourceIPC2221()));
      }
      for (std::size_t i = avant; i < cand.size(); ++i) cand[i].nom = "classe " + k.nom;
    }
  }
  for (const auto& k : R.avancees.contraintes) {
    if (!k.active || k.genre != genre) continue;
    bool prise = false;
    if (b && !k.porteeB.empty()) prise = (portee(k.portee, a) && portee(k.porteeB, *b)) || (portee(k.portee, *b) && portee(k.porteeB, a));
    else if (b) prise = portee(k.portee, a) || portee(k.portee, *b);
    else prise = portee(k.portee, a);
    if (prise) cand.push_back({k.nom.empty() ? std::string(nomGenreContrainte(k.genre)) : k.nom, k.valeur, k.priorite, k.justification.empty() ? k.source : k.justification});
  }
  Resolution r;
  if (cand.empty()) return r;
  const Candidat* meilleur = &cand.front();
  for (const auto& x : cand)
    if (x.priorite > meilleur->priorite || (x.priorite == meilleur->priorite && plusStricteQue(x.v, meilleur->v, interne))) meilleur = &x;
  r.trouvee = true;
  r.valeur = meilleur->v;
  r.regle = meilleur->nom;
  const Candidat& generale = cand.front();
  r.justification = mmTexte(meilleur->v.minPour(interne) ? meilleur->v.minPour(interne) : meilleur->v.prefPour(interne)) + ", " + meilleur->nom;
  if (meilleur != &generale && generale.nom == "règle générale")
    r.justification += meilleur->priorite > 0 ? ", priorité " + std::to_string(meilleur->priorite) + " sur la règle générale" : ", plus stricte que la règle générale (" + mmTexte(generale.v.minPour(interne)) + ")";
  if (!meilleur->justification.empty()) r.justification += " — " + meilleur->justification;
  for (const auto& x : cand)
    if (&x != meilleur) r.ecartees.push_back(x.nom + " : " + mmTexte(x.v.minPour(interne)));
  return r;
}

Resolution isolementRequis(const ConceptionElec& c, const Proprietes& a, const Proprietes& b, bool interne) { return resoudre(c, GenreContrainte::Isolement, a, &b, interne); }

double isolementIPC2221(double tension, const std::string& categorie) {
  // IPC-2221B, table 6-1 (mm) ; tension continue ou crête
  static const double seuils[] = {15, 30, 50, 100, 150, 170, 250, 300, 500};
  static const std::map<std::string, std::vector<double>> table = {
      {"B1", {0.05, 0.05, 0.1, 0.1, 0.2, 0.2, 0.2, 0.2, 0.25}},   {"B2", {0.1, 0.1, 0.6, 0.6, 0.6, 1.25, 1.25, 1.25, 2.5}},
      {"B3", {0.1, 0.1, 0.6, 1.5, 3.2, 3.2, 6.4, 12.5, 12.5}},   {"B4", {0.05, 0.05, 0.13, 0.13, 0.4, 0.4, 0.4, 0.4, 0.8}},
      {"A5", {0.13, 0.13, 0.13, 0.13, 0.4, 0.4, 0.4, 0.4, 0.8}}, {"A6", {0.13, 0.25, 0.4, 0.5, 0.8, 0.8, 0.8, 0.8, 1.5}},
      {"A7", {0.13, 0.13, 0.13, 0.13, 0.4, 0.4, 0.4, 0.8, 0.8}}};
  static const std::map<std::string, double> parVolt = {{"B1", 0.0025}, {"B2", 0.005}, {"B3", 0.025}, {"B4", 0.00305}, {"A5", 0.00305}, {"A6", 0.00305}, {"A7", 0.00305}};
  const auto it = table.find(categorie);
  if (it == table.end()) return 0;
  tension = std::abs(tension);
  for (std::size_t i = 0; i < 9; ++i)
    if (tension <= seuils[i]) return it->second[i];
  // Au-delà de 500 V : la valeur par volt s'ajoute à celle de 500 V (note de la table 6-1)
  return it->second[8] + (tension - 500) * parVolt.at(categorie);
}

double ligneFuiteCEI60664(double u, int pollution, int groupe, bool circuitImprime) {
  // CEI 60664-1:2020, table F.4 (mm) : tension efficace ; colonnes : circuit imprimé PD1, PD2 ; PD1 ; PD2 I, II, III ;
  // PD3 I, II, III. Entre deux lignes, la valeur de la ligne supérieure (prudente).
  struct Ligne {
    double u, c1, c2, p1, p2a, p2b, p2c, p3a, p3b, p3c;
  };
  static const Ligne t[] = {{10, 0.025, 0.04, 0.08, 0.4, 0.4, 0.4, 1.0, 1.0, 1.0},       {12.5, 0.025, 0.04, 0.09, 0.42, 0.42, 0.42, 1.05, 1.05, 1.05},
                            {16, 0.025, 0.04, 0.1, 0.45, 0.45, 0.45, 1.1, 1.1, 1.1},       {20, 0.025, 0.04, 0.11, 0.48, 0.48, 0.48, 1.2, 1.2, 1.2},
                            {25, 0.025, 0.04, 0.125, 0.5, 0.5, 0.5, 1.25, 1.25, 1.25},    {32, 0.025, 0.04, 0.14, 0.53, 0.53, 0.53, 1.3, 1.3, 1.3},
                            {40, 0.025, 0.04, 0.16, 0.56, 0.8, 1.1, 1.4, 1.6, 1.8},       {50, 0.025, 0.04, 0.18, 0.6, 0.85, 1.2, 1.5, 1.7, 1.9},
                            {63, 0.04, 0.063, 0.2, 0.63, 0.9, 1.25, 1.6, 1.8, 2.0},       {80, 0.063, 0.1, 0.22, 0.67, 0.95, 1.3, 1.7, 1.9, 2.1},
                            {100, 0.1, 0.16, 0.25, 0.71, 1.0, 1.4, 1.8, 2.0, 2.2},        {125, 0.16, 0.25, 0.28, 0.75, 1.05, 1.5, 1.9, 2.1, 2.4},
                            {160, 0.25, 0.4, 0.32, 0.8, 1.1, 1.6, 2.0, 2.2, 2.5},         {200, 0.4, 0.63, 0.42, 1.0, 1.4, 2.0, 2.5, 2.8, 3.2},
                            {250, 0.56, 1.0, 0.56, 1.25, 1.8, 2.5, 3.2, 3.6, 4.0},        {320, 0.75, 1.6, 0.75, 1.6, 2.2, 3.2, 4.0, 4.5, 5.0},
                            {400, 1.0, 2.0, 1.0, 2.0, 2.8, 4.0, 5.0, 5.6, 6.3},           {500, 1.3, 2.5, 1.3, 2.5, 3.6, 5.0, 6.3, 7.1, 8.0},
                            {630, 1.8, 3.2, 1.8, 3.2, 4.5, 6.3, 8.0, 9.0, 10.0},          {800, 2.4, 4.0, 2.4, 4.0, 5.6, 8.0, 10.0, 11.0, 12.5},
                            {1000, 3.2, 5.0, 3.2, 5.0, 7.1, 10.0, 12.5, 14.0, 16.0}};
  const Ligne* l = &t[std::size(t) - 1];
  for (const auto& x : t)
    if (std::abs(u) <= x.u) {
      l = &x;
      break;
    }
  if (circuitImprime && pollution <= 2) return pollution <= 1 ? l->c1 : l->c2;
  if (pollution <= 1) return l->p1;
  if (pollution == 2) return groupe <= 1 ? l->p2a : groupe == 2 ? l->p2b : l->p2c;
  return groupe <= 1 ? l->p3a : groupe == 2 ? l->p3b : l->p3c;
}

std::string sourceIPC2221() { return "IPC-2221B table 6-1"; }
std::string sourceCEI60664() { return "CEI 60664-1 table F.4"; }

std::string exporterRegles(const Regles& r) {
  nlohmann::ordered_json o;
  o["format"] = "promethee-regles";
  o["version"] = 1;
  nlohmann::ordered_json cl = nlohmann::ordered_json::array();
  for (const auto& k : r.classes) {
    nlohmann::ordered_json x = {{"nom", k.nom}, {"isolement", k.isolement}, {"largeur", k.largeur}, {"viaDiametre", k.viaDiametre}, {"viaPercage", k.viaPercage}};
    if (k.courant > 0) x["courant"] = k.courant, x["echauffement"] = k.echauffement;
    if (k.tension > 0) x["tension"] = k.tension;
    if (!k.couleur.empty()) x["couleur"] = k.couleur;
    cl.push_back(x);
  }
  o["classes"] = cl;
  nlohmann::ordered_json af = nlohmann::ordered_json::object();
  for (const auto& [reseau, classe] : r.classeReseau) af[reseau] = classe;
  o["affectations"] = af;
  o["avancees"] = contraintesVersJson(r.avancees);
  return o.dump(2) + "\n";
}

bool importerRegles(Regles& r, const std::string& texte, bool remplacer, std::string* erreur) {
  nlohmann::ordered_json o;
  try {
    o = nlohmann::ordered_json::parse(texte);
  } catch (const std::exception& e) {
    if (erreur) *erreur = std::string("fichier de règles illisible : ") + e.what();
    return false;
  }
  if (!o.is_object() || o.value("format", "") != "promethee-regles") {
    if (erreur) *erreur = "ce n'est pas un fichier de règles de Prométhée";
    return false;
  }
  std::vector<ClasseReseau> classes;
  for (const auto& x : o.value("classes", nlohmann::ordered_json::array())) {
    if (!x.is_object() || !x.contains("nom")) continue;
    ClasseReseau k;
    k.nom = x.value("nom", "");
    k.isolement = x.value("isolement", k.isolement);
    k.largeur = x.value("largeur", k.largeur);
    k.viaDiametre = x.value("viaDiametre", k.viaDiametre);
    k.viaPercage = x.value("viaPercage", k.viaPercage);
    k.courant = x.value("courant", 0.0);
    k.echauffement = x.value("echauffement", 10.0);
    k.tension = x.value("tension", 0.0);
    k.couleur = x.value("couleur", "");
    classes.push_back(k);
  }
  const ContraintesCarte av = contraintesDepuisJson(o.value("avancees", nlohmann::ordered_json::object()));
  if (remplacer) {
    r.classes = classes;
    r.classeReseau.clear();
    r.avancees = av;
  } else {
    for (const auto& k : classes) {
      auto it = std::find_if(r.classes.begin(), r.classes.end(), [&](const ClasseReseau& x) { return x.nom == k.nom; });
      if (it != r.classes.end()) *it = k;
      else r.classes.push_back(k);
    }
    for (const auto& k : av.contraintes) {
      auto it = std::find_if(r.avancees.contraintes.begin(), r.avancees.contraintes.end(), [&](const Contrainte& x) { return !x.nom.empty() && x.nom == k.nom; });
      if (it != r.avancees.contraintes.end()) *it = k;
      else r.avancees.contraintes.push_back(k);
    }
    for (const auto& k : av.classesComposants) {
      auto it = std::find_if(r.avancees.classesComposants.begin(), r.avancees.classesComposants.end(), [&](const ClasseComposants& x) { return x.nom == k.nom; });
      if (it != r.avancees.classesComposants.end()) *it = k;
      else r.avancees.classesComposants.push_back(k);
    }
    for (const auto& k : av.paires) r.avancees.paires.push_back(k);
    for (const auto& k : av.accords) r.avancees.accords.push_back(k);
    for (const auto& k : av.signaux) r.avancees.signaux.push_back(k);
    for (const auto& k : av.jeux) r.avancees.jeux.push_back(k);
    for (const auto& k : av.impedances) r.avancees.impedances.push_back(k);
    if (av.tableIsolement != "IPC-2221") r.avancees.tableIsolement = av.tableIsolement;
  }
  if (const auto it = o.find("affectations"); it != o.end() && it->is_object())
    for (auto a = it->begin(); a != it->end(); ++a)
      if (a.value().is_string()) r.classeReseau[a.key()] = a.value().get<std::string>();
  return true;
}

std::vector<EcartProfil> ecartsAuProfil(const Regles& r) {
  std::vector<EcartProfil> e;
  const auto profil = profilDeReference(r);
  if (!profil) return e;
  const Regles& p = profil->regles;
  auto sous = [&](const std::string& ou, Nm v, Nm min, const char* quoi) {
    if (v > 0 && v < min) e.push_back({ou, std::string(quoi) + " de " + mmTexte(v) + " sous le minimum du profil « " + profil->nom + " » (" + mmTexte(min) + ")"});
  };
  sous("règles générales", r.isolement, p.isolement, "isolement");
  sous("règles générales", r.largeurMin, p.largeurMin, "largeur");
  sous("règles générales", r.percageMin, p.percageMin, "perçage");
  for (const auto& k : r.classes) {
    sous("classe " + k.nom, k.isolement, p.isolement, "isolement");
    sous("classe " + k.nom, k.largeur, p.largeurMin, "largeur");
    sous("classe " + k.nom, k.viaPercage, p.percageMin, "perçage de via");
    if (k.viaDiametre > 0 && k.viaPercage > 0 && (k.viaDiametre - k.viaPercage) / 2 < p.anneauMin)
      e.push_back({"classe " + k.nom, "couronne de via de " + mmTexte((k.viaDiametre - k.viaPercage) / 2) + " sous le minimum du profil (" + mmTexte(p.anneauMin) + ")"});
  }
  for (const auto& k : r.avancees.contraintes) {
    const std::string ou = "contrainte « " + (k.nom.empty() ? std::string(nomGenreContrainte(k.genre)) : k.nom) + " »";
    switch (k.genre) {
      case GenreContrainte::Isolement: sous(ou, k.valeur.min, p.isolement, "isolement"); sous(ou, k.valeur.minInterne, p.isolement, "isolement interne"); break;
      case GenreContrainte::Largeur: sous(ou, k.valeur.min, p.largeurMin, "largeur"); sous(ou, k.valeur.minInterne, p.largeurMin, "largeur interne"); break;
      case GenreContrainte::ViaPercage: sous(ou, k.valeur.min, p.percageMin, "perçage"); break;
      default: break;
    }
  }
  return e;
}

}  // namespace prom::ecad

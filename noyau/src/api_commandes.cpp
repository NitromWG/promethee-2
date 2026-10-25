// Prométhée : interface de commandes en JSON-RPC 2.0 (lot L13). Licence GPL-3.0-only.
#include "promethee/api_commandes.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <stdexcept>

#include "promethee/circuit_edition.hpp"
#include "promethee/modele.hpp"
#include "promethee/revue_conception.hpp"
#include "promethee/simulation.hpp"
#include "promethee/simulation_projet.hpp"
#include "promethee/variantes.hpp"
#include "promethee/version.hpp"

namespace prom {

namespace {
using J = nlohmann::ordered_json;
using namespace ecad;

struct ErreurApi : std::runtime_error {
  int code;
  ErreurApi(int c, const std::string& m) : std::runtime_error(m), code(c) {}
};

double nombre(const J& p, const char* k, std::optional<double> defaut = std::nullopt) {
  if (p.is_object() && p.contains(k) && p[k].is_number()) return p[k].get<double>();
  if (defaut) return *defaut;
  throw ErreurApi(-32602, std::string("paramètre « ") + k + " » attendu (nombre)");
}
std::string texte(const J& p, const char* k, std::optional<std::string> defaut = std::nullopt) {
  if (p.is_object() && p.contains(k) && p[k].is_string()) return p[k].get<std::string>();
  if (defaut) return *defaut;
  throw ErreurApi(-32602, std::string("paramètre « ") + k + " » attendu (texte)");
}
Pt point(const J& p, const char* k) {
  if (p.is_object() && p.contains(k) && p[k].is_array() && p[k].size() == 2 && p[k][0].is_number() && p[k][1].is_number())
    return ptMm(p[k][0].get<double>(), p[k][1].get<double>());
  throw ErreurApi(-32602, std::string("paramètre « ") + k + " » attendu ([x, y] en mm)");
}
ConceptionElec& conception(Projet& p, const J& params) {
  if (p.electronique.empty()) throw ErreurApi(-32001, "le projet n'a pas de conception électronique");
  if (params.is_object() && params.contains("conception")) {
    const J& c = params["conception"];
    if (c.is_number_integer()) {
      const auto i = c.get<long long>();
      if (i >= 0 && static_cast<std::size_t>(i) < p.electronique.size()) return p.electronique[static_cast<std::size_t>(i)];
    } else if (c.is_string())
      for (auto& x : p.electronique)
        if (x.uuid == c.get<std::string>() || x.nom == c.get<std::string>()) return x;
    throw ErreurApi(-32602, "conception inconnue");
  }
  return p.electronique.front();
}
J violations(const std::vector<Violation>& v) {
  J r = J::array();
  for (const auto& x : v)
    r.push_back({{"gravite", x.gravite == Gravite::Erreur ? "erreur" : "avertissement"}, {"code", x.code}, {"message", x.message}, {"x", static_cast<double>(x.position.x) * 1e-6},
                 {"y", static_cast<double>(x.position.y) * 1e-6}});
  return r;
}

// Analyse décrite en JSON : {type: "ac"|"tran"|"op"|"dc", debut, fin, points, pas, duree, source, de, a, increment}
AnalyseSpice analyseDe(const J& a) {
  AnalyseSpice r;
  const J x = a.is_object() && a.contains("analyse") && a["analyse"].is_object() ? a["analyse"] : J::object();
  const std::string t = texte(x, "type", std::string("ac"));
  if (t == "ac" || t == "alternatif") {
    r.type = AnalyseSpice::Type::Alternatif;
    r.fDebut = nombre(x, "debut", 10.0);
    r.fFin = nombre(x, "fin", 1e6);
    r.pointsParDecade = static_cast<int>(nombre(x, "points", 50.0));
  } else if (t == "tran" || t == "transitoire") {
    r.type = AnalyseSpice::Type::Transitoire;
    r.pas = nombre(x, "pas", 1e-5);
    r.duree = nombre(x, "duree", 1e-2);
  } else if (t == "dc" || t == "balayage") {
    r.type = AnalyseSpice::Type::Continu;
    r.source = texte(x, "source", std::string("V1"));
    r.dcDebut = nombre(x, "de", 0.0);
    r.dcFin = nombre(x, "a", 5.0);
    r.dcPas = nombre(x, "increment", 0.1);
  } else if (t == "op" || t == "repos") {
    r.type = AnalyseSpice::Type::PointDeRepos;
  } else
    throw ErreurApi(-32602, "analyse inconnue : " + t);
  return r;
}
// Le calcul de ngspice sur la conception (sans la modifier)
std::pair<ResultatSimulation, NetlistSpice> calculer(const ConceptionElec& c, const AnalyseSpice& an) {
  if (!ngspiceDisponible()) throw ErreurApi(-32010, "ngspice n'est pas installé sur ce poste");
  OptionsSimulation o;
  NetlistSpice n = netlistSpice(c, an, o);
  if (!n.valide()) throw ErreurApi(-32011, "netlist refusée : " + (n.erreurs.empty() ? std::string("?") : n.erreurs.front()));
  ResultatSimulation r = simuler(n);
  if (!r.reussi()) {
    // Remèdes de convergence essayés dans l'ordre (11.9)
    EssaiRemedes e = simulerAvecRemedes(c, an, o);
    if (!e.resultat.reussi()) throw ErreurApi(-32012, "calcul de ngspice en échec : " + analyserEchec(r, n).cause);
    return {e.resultat, e.netlist};
  }
  return {r, n};
}

using Methode = std::function<J(Projet&, const J&, bool& modifie, std::string& pas)>;
const std::map<std::string, std::pair<std::string, Methode>>& table() {
  static const std::map<std::string, std::pair<std::string, Methode>> t = {
      {"version", {"version du logiciel", [](Projet&, const J&, bool&, std::string&) { return J(ligneVersion()); }}},
      {"methodes", {"liste des méthodes", [](Projet&, const J&, bool&, std::string&) {
                      J r = J::object();
                      for (const auto& [n, d] : methodesApi()) r[n] = d;
                      return r;
                    }}},
      {"projet.resume", {"nom du projet et de ses conceptions électroniques", [](Projet& p, const J&, bool&, std::string&) {
                           J c = J::array();
                           for (const auto& x : p.electronique)
                             c.push_back({{"uuid", x.uuid}, {"nom", x.nom}, {"composants", x.composants.size()}, {"feuilles", x.feuilles.size()}, {"pistes", x.circuit.pistes.size()},
                                          {"vias", x.circuit.vias.size()}, {"variantes", x.variantesMontage.size()}});
                           return J{{"nom", p.nom}, {"conceptions", c}};
                         }}},
      {"conception.composants", {"composants : repère, valeur, empreinte, UUID", [](Projet& p, const J& a, bool&, std::string&) {
                                   J r = J::array();
                                   for (const auto& k : conception(p, a).composants) r.push_back({{"uuid", k.uuid}, {"ref", k.reference}, {"valeur", k.valeur}, {"empreinte", k.empreinte}});
                                   return r;
                                 }}},
      {"conception.reseaux", {"réseaux et leurs broches (repère.numéro)", [](Projet& p, const J& a, bool&, std::string&) {
                                const ConceptionElec& c = conception(p, a);
                                const Connectivite k = connectivite(c);
                                J r = J::object();
                                for (const auto& n : k.reseaux) {
                                  J b = J::array();
                                  for (const auto& x : n.broches)
                                    if (const ComposantElec* comp = c.composant(x.composant)) b.push_back(comp->reference + "." + x.broche);
                                  r[n.nom] = b;
                                }
                                return r;
                              }}},
      {"composant.valeur", {"change la valeur d'un composant : {ref, valeur}", [](Projet& p, const J& a, bool& m, std::string& pas) {
                              ConceptionElec& c = conception(p, a);
                              const std::string ref = texte(a, "ref"), v = texte(a, "valeur");
                              for (auto& k : c.composants)
                                if (k.reference == ref) {
                                  if (c.verrouillee) throw ErreurApi(-32003, "conception publiée : nouvelle révision nécessaire");
                                  k.valeur = v;
                                  m = true;
                                  pas = "Valeur de " + ref + " : " + v;
                                  return J(true);
                                }
                              throw ErreurApi(-32602, "repère inconnu : " + ref);
                            }}},
      {"symbole.poser", {"pose un symbole de la bibliothèque : {symbole, x, y, feuille?, ref?, valeur?}", [](Projet& p, const J& a, bool& m, std::string& pas) {
                           ConceptionElec& c = conception(p, a);
                           if (c.verrouillee) throw ErreurApi(-32003, "conception publiée : nouvelle révision nécessaire");
                           const std::size_t f = static_cast<std::size_t>(nombre(a, "feuille", 0.0));
                           if (f >= c.feuilles.size()) throw ErreurApi(-32602, "feuille inconnue");
                           const std::string s = poserSymbole(c, f, texte(a, "symbole"), ptMm(nombre(a, "x"), nombre(a, "y")), static_cast<int>(nombre(a, "rotation", 0.0)));
                           if (s.empty()) throw ErreurApi(-32602, "symbole inconnu : " + texte(a, "symbole"));
                           std::string ref;
                           for (const auto& sp : c.feuilles[f].symboles)
                             if (sp.uuid == s)
                               if (ComposantElec* k = c.composant(sp.composant)) {
                                 if (a.contains("ref")) k->reference = texte(a, "ref");
                                 if (a.contains("valeur")) k->valeur = texte(a, "valeur");
                                 ref = k->reference;
                               }
                           m = true;
                           pas = "Symbole " + ref + " posé";
                           return J{{"symbole", s}, {"ref", ref}};
                         }}},
      {"via.poser", {"pose une via : {x, y, reseau?, diametre?, percage?} (mm)", [](Projet& p, const J& a, bool& m, std::string& pas) {
                       ConceptionElec& c = conception(p, a);
                       if (c.verrouillee) throw ErreurApi(-32003, "conception publiée : nouvelle révision nécessaire");
                       Via v;
                       v.uuid = nouvelUuid();
                       v.p = ptMm(nombre(a, "x"), nombre(a, "y"));
                       v.reseau = texte(a, "reseau", std::string());
                       v.diametre = static_cast<Nm>(std::llround(nombre(a, "diametre", 0.8) * 1e6));
                       v.percage = static_cast<Nm>(std::llround(nombre(a, "percage", 0.4) * 1e6));
                       if (v.percage <= 0 || v.diametre <= v.percage) throw ErreurApi(-32602, "diamètre et perçage incohérents");
                       c.circuit.vias.push_back(v);
                       m = true;
                       pas = "Via posée";
                       return J(v.uuid);
                     }}},
      {"piste.poser", {"pose un segment de piste : {a:[x,y], b:[x,y], largeur?, couche?, reseau?}", [](Projet& p, const J& a, bool& m, std::string& pas) {
                         ConceptionElec& c = conception(p, a);
                         if (c.verrouillee) throw ErreurApi(-32003, "conception publiée : nouvelle révision nécessaire");
                         Piste t;
                         t.uuid = nouvelUuid();
                         t.a = point(a, "a");
                         t.b = point(a, "b");
                         t.largeur = static_cast<Nm>(std::llround(nombre(a, "largeur", 0.25) * 1e6));
                         const auto couche = coucheDepuisCode(texte(a, "couche", std::string("F.Cu")));
                         if (!couche || !estCuivre(*couche)) throw ErreurApi(-32602, "couche de cuivre inconnue");
                         t.couche = *couche;
                         t.reseau = texte(a, "reseau", std::string());
                         c.circuit.pistes.push_back(t);
                         m = true;
                         pas = "Piste posée";
                         return J(t.uuid);
                       }}},
      {"erc", {"vérification électrique du schéma", [](Projet& p, const J& a, bool&, std::string&) {
                 const ConceptionElec& c = conception(p, a);
                 return violations(verifierERC(c, connectivite(c), NiveauErc::Complet));
               }}},
      {"drc", {"vérification des règles du circuit", [](Projet& p, const J& a, bool&, std::string&) {
                 const ConceptionElec& c = conception(p, a);
                 const Connectivite k = connectivite(c);
                 return violations(verifierDRCReglee(c, k, remplirZones(c, k)));
               }}},
      {"revue", {"revue automatique (LED sans résistance, découplage, I2C…)", [](Projet& p, const J& a, bool&, std::string&) {
                   J r = J::array();
                   for (const auto& x : revueConception(conception(p, a)))
                     r.push_back({{"regle", x.regle}, {"gravite", x.gravite}, {"objet", x.objet}, {"message", x.message}, {"correction", x.correction}});
                   return r;
                 }}},
      // ---------- 0.34 (13.8, 13.9, 13.17) : simulation, format de la carte ----------
      {"simulation.mesurer", {"mesure par ngspice : {analyse:{type, debut, fin…}, mesure, reseau, parametre?} → {valeur, unite}", [](Projet& p, const J& a, bool&, std::string&) {
                                const ConceptionElec& c = conception(p, a);
                                const auto g = mesureDepuisNom(texte(a, "mesure", std::string("coupure")));
                                if (!g) throw ErreurApi(-32602, "mesure inconnue : " + texte(a, "mesure", std::string()));
                                const auto [r, n] = calculer(c, analyseDe(a));
                                const ResultatMesure m = mesurer({*g, texte(a, "reseau"), nombre(a, "parametre", 0.0)}, r, n);
                                if (!m.valide) throw ErreurApi(-32013, "mesure impossible sur « " + texte(a, "reseau") + " »");
                                return J{{"valeur", m.valeur}, {"unite", m.unite}, {"nom", m.nom}};
                              }}},
      {"simulation.courbe", {"courbe calculée par ngspice : {analyse, expression} → {x, y}", [](Projet& p, const J& a, bool&, std::string&) {
                               const ConceptionElec& c = conception(p, a);
                               const auto [r, n] = calculer(c, analyseDe(a));
                               const CourbeCalculee k = evaluerCourbe(texte(a, "expression"), r, n);
                               if (!k.valide()) throw ErreurApi(-32013, "expression impossible : " + k.erreur);
                               return J{{"x", k.x}, {"y", k.complexe ? k.modules() : k.reels()}, {"echelle", r.echelle}};
                             }}},
      {"ngspice.commande", {"commande de lecture de ngspice sur le dernier calcul : {commande} (« shell » et l'écriture sont refusées)", [](Projet&, const J& a, bool&, std::string&) {
                              std::string refus;
                              const auto lignes = commandeConsole(texte(a, "commande"), &refus);
                              if (!refus.empty()) throw ErreurApi(-32014, refus);
                              return J(lignes);
                            }}},
      {"carte.contour", {"contour rectangulaire de la carte : {largeur, hauteur, rayon?} (mm)", [](Projet& p, const J& a, bool& m, std::string& pas) {
                           ConceptionElec& c = conception(p, a);
                           if (c.verrouillee) throw ErreurApi(-32003, "conception publiée : nouvelle révision nécessaire");
                           const double L = nombre(a, "largeur"), H = nombre(a, "hauteur"), R = nombre(a, "rayon", 0.0);
                           if (L <= 0 || H <= 0 || R < 0 || 2 * R > std::min(L, H)) throw ErreurApi(-32602, "dimensions du contour incohérentes");
                           c.circuit.contour.libre.clear();
                           c.circuit.contour.largeur = static_cast<Nm>(std::llround(L * 1e6));
                           c.circuit.contour.hauteur = static_cast<Nm>(std::llround(H * 1e6));
                           c.circuit.contour.rayon = static_cast<Nm>(std::llround(R * 1e6));
                           m = true;
                           pas = "Contour de la carte";
                           return J(true);
                         }}},
      {"carte.trou", {"trou de fixation non métallisé : {x, y, diametre} (mm, depuis le coin haut gauche du contour)", [](Projet& p, const J& a, bool& m, std::string& pas) {
                        ConceptionElec& c = conception(p, a);
                        if (c.verrouillee) throw ErreurApi(-32003, "conception publiée : nouvelle révision nécessaire");
                        const double d = nombre(a, "diametre");
                        if (d <= 0 || d > 20) throw ErreurApi(-32602, "diamètre attendu (mm)");
                        char id[48];
                        std::snprintf(id, sizeof id, "MountingHole_%g", d);
                        const Pt q{c.circuit.contour.origine.x + static_cast<Nm>(std::llround(nombre(a, "x") * 1e6)), c.circuit.contour.origine.y + static_cast<Nm>(std::llround(nombre(a, "y") * 1e6))};
                        const std::string u = poserEmpreinteSeule(c, id, q, 0, false, "H");
                        if (u.empty()) throw ErreurApi(-32602, std::string("trou impossible : ") + id);
                        m = true;
                        pas = "Trou de fixation";
                        const ComposantElec* k = c.composant(u);
                        return J(k ? k->reference : u);
                      }}},
      {"carte.texte", {"texte sur la carte : {texte, x, y, hauteur?, couche?} (mm ; sérigraphie du dessus par défaut)", [](Projet& p, const J& a, bool& m, std::string& pas) {
                         ConceptionElec& c = conception(p, a);
                         if (c.verrouillee) throw ErreurApi(-32003, "conception publiée : nouvelle révision nécessaire");
                         TexteCircuit t;
                         t.uuid = nouvelUuid();
                         t.texte = texte(a, "texte");
                         t.p = {c.circuit.contour.origine.x + static_cast<Nm>(std::llround(nombre(a, "x") * 1e6)), c.circuit.contour.origine.y + static_cast<Nm>(std::llround(nombre(a, "y") * 1e6))};
                         t.hauteur = static_cast<Nm>(std::llround(nombre(a, "hauteur", 1.0) * 1e6));
                         const auto couche = coucheDepuisCode(texte(a, "couche", std::string("F.SilkS")));
                         if (!couche) throw ErreurApi(-32602, "couche inconnue");
                         t.couche = *couche;
                         c.circuit.textes.push_back(t);
                         m = true;
                         pas = "Texte « " + t.texte + " »";
                         return J(t.uuid);
                       }}},
      {"carte.regles", {"règles de la carte : {isolement?, largeur?, via?, percage?, bord?} (mm)", [](Projet& p, const J& a, bool& m, std::string& pas) {
                          ConceptionElec& c = conception(p, a);
                          if (c.verrouillee) throw ErreurApi(-32003, "conception publiée : nouvelle révision nécessaire");
                          Regles& r = c.circuit.regles;
                          auto mm = [&](const char* k, Nm& v) {
                            if (a.contains(k)) v = static_cast<Nm>(std::llround(nombre(a, k) * 1e6));
                          };
                          mm("isolement", r.isolement);
                          mm("largeur", r.largeur);
                          mm("via", r.viaDiametre);
                          mm("percage", r.viaPercage);
                          mm("bord", r.bord);
                          m = true;
                          pas = "Règles de la carte";
                          return J(true);
                        }}},
      {"cartouche", {"cartouche : {titre?, societe?, auteur?, revision?, date?, commentaire?}", [](Projet& p, const J& a, bool& m, std::string& pas) {
                       ConceptionElec& c = conception(p, a);
                       for (const auto& [k, champ] : std::initializer_list<std::pair<const char*, std::string*>>{{"titre", &c.cartouche.titre}, {"societe", &c.cartouche.societe}, {"auteur", &c.cartouche.auteur},
                                                                                                          {"revision", &c.cartouche.revision}, {"date", &c.cartouche.date}, {"commentaire", &c.cartouche.commentaire}})
                         if (a.contains(k)) *champ = texte(a, k);
                       m = true;
                       pas = "Cartouche";
                       return J(true);
                     }}},
      {"cout", {"coût de la nomenclature : {cartes?, variante?}", [](Projet& p, const J& a, bool&, std::string&) {
                  const CoutNomenclature n = coutNomenclature(conception(p, a), static_cast<int>(nombre(a, "cartes", 1.0)), texte(a, "variante", std::string()));
                  return J{{"total", n.total}, {"parCarte", n.parCarte}, {"devise", n.devise}, {"lignesSansPrix", n.lignesSansPrix}};
                }}},
  };
  return t;
}
}  // namespace

const std::vector<std::pair<std::string, std::string>>& methodesApi() {
  static const std::vector<std::pair<std::string, std::string>> m = [] {
    std::vector<std::pair<std::string, std::string>> r;
    for (const auto& [n, x] : table()) r.emplace_back(n, x.first);
    r.emplace_back("lot", "requêtes appliquées ensemble, en un seul pas d'annulation : params = [requêtes]");
    return r;
  }();
  return m;
}

ReponseApi traiterRequete(Projet& p, const J& q) {
  ReponseApi r;
  const J id = q.is_object() && q.contains("id") ? q["id"] : J();
  r.reponse = {{"jsonrpc", "2.0"}, {"id", id}};
  try {
    if (!q.is_object() || !q.contains("method") || !q["method"].is_string()) throw ErreurApi(-32600, "requête invalide");
    const std::string methode = q["method"].get<std::string>();
    const J params = q.contains("params") ? q["params"] : J::object();
    if (methode == "lot") {
      if (!params.is_array()) throw ErreurApi(-32602, "« lot » attend un tableau de requêtes");
      // Tout ou rien : le lot travaille sur une copie, rendue au projet seulement si chaque requête réussit
      Projet copie = p;
      J resultats = J::array();
      int changements = 0;
      for (const auto& sous : params) {
        ReponseApi s = traiterRequete(copie, sous);
        if (s.reponse.contains("error")) {
          r.reponse["error"] = {{"code", s.reponse["error"]["code"]}, {"message", "lot abandonné : " + s.reponse["error"]["message"].get<std::string>()}, {"data", resultats.size()}};
          return r;
        }
        changements += s.modifie;
        resultats.push_back(s.reponse["result"]);
      }
      if (changements) {
        p = std::move(copie);
        r.modifie = true;
        r.nomPas = "API : " + std::to_string(changements) + " modification" + (changements > 1 ? "s" : "");
      }
      r.reponse["result"] = resultats;
      return r;
    }
    const auto it = table().find(methode);
    if (it == table().end()) throw ErreurApi(-32601, "méthode inconnue : " + methode);
    r.reponse["result"] = it->second.second(p, params, r.modifie, r.nomPas);
    if (r.modifie && r.nomPas.empty()) r.nomPas = "API : " + methode;
  } catch (const ErreurApi& e) {
    r.modifie = false;
    r.reponse["error"] = {{"code", e.code}, {"message", e.what()}};
  } catch (const std::exception& e) {
    r.modifie = false;
    r.reponse["error"] = {{"code", -32603}, {"message", e.what()}};
  }
  return r;
}

ReponseApi traiterTexte(Projet& p, const std::string& t) {
  const J q = J::parse(t, nullptr, false);
  if (q.is_discarded()) {
    ReponseApi r;
    r.reponse = {{"jsonrpc", "2.0"}, {"id", nullptr}, {"error", {{"code", -32700}, {"message", "JSON illisible"}}}};
    return r;
  }
  return traiterRequete(p, q);
}

}  // namespace prom

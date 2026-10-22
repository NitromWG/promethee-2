// Le portage C++ doit reproduire le prototype web à l'identique, cas par cas.
// Les cas de référence (tests/donnees/reference_js.json) sont produits par le noyau JavaScript du jalon 0.
// Licence GPL-3.0-only.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <sstream>

#include "promethee/derive.hpp"
#include "promethee/modele.hpp"
#include "promethee/placement.hpp"
#include "promethee/verifs.hpp"

using namespace prom;

namespace {

const Json& reference() {
  static const Json j = [] {
    std::ifstream f(std::string(PROMETHEE_DONNEES) + "/reference_js.json");
    std::stringstream s;
    s << f.rdbuf();
    return Json::parse(s.str());
  }();
  return j;
}

void comparer(const Json& a, const Json& b, const std::string& chemin, bool ignorerIds, std::vector<std::string>& ecarts) {
  if (a.is_number() && b.is_number()) {
    if (std::abs(a.get<double>() - b.get<double>()) > 1e-9) ecarts.push_back(chemin + " : " + a.dump() + " au lieu de " + b.dump());
    return;
  }
  if (a.type() != b.type()) { ecarts.push_back(chemin + " : " + a.dump() + " au lieu de " + b.dump()); return; }
  if (a.is_object()) {
    for (auto it = a.begin(); it != a.end(); ++it) {
      if (ignorerIds && it.key() == "id") continue;
      if (it.key() == "version") continue;  // format 2 côté natif, 1 côté prototype : le contenu reste identique
      if (!b.contains(it.key())) { ecarts.push_back(chemin + "." + it.key() + " en trop"); continue; }
      comparer(it.value(), b.at(it.key()), chemin + "." + it.key(), ignorerIds, ecarts);
    }
    for (auto it = b.begin(); it != b.end(); ++it)
      if (!a.contains(it.key()) && !(ignorerIds && it.key() == "id")) ecarts.push_back(chemin + "." + it.key() + " manquant");
    return;
  }
  if (a.is_array()) {
    if (a.size() != b.size()) { ecarts.push_back(chemin + " : " + std::to_string(a.size()) + " éléments au lieu de " + std::to_string(b.size())); return; }
    for (size_t i = 0; i < a.size(); ++i) comparer(a[i], b[i], chemin + "[" + std::to_string(i) + "]", ignorerIds, ecarts);
    return;
  }
  if (a != b) ecarts.push_back(chemin + " : " + a.dump() + " au lieu de " + b.dump());
}

std::string lettreJson(Bord b) { return std::string(1, lettre(b)); }

Json deriveEnJson(const Derive& d) {
  Json j = {{"cx", d.cx}, {"cy", d.cy}, {"Li", d.Li}, {"Wi", d.Wi}, {"ri", d.ri}, {"Lo", d.Lo}, {"Wo", d.Wo}, {"ro", d.ro},
            {"zf", d.zf}, {"zpb", d.zpb}, {"zpt", d.zpt}, {"Rb", d.Rb}, {"rp", d.rp}, {"rTete", d.rTete}, {"sb", d.sb}, {"sa", d.sa},
            {"H", d.H}, {"zt", d.zt}, {"ztop", d.ztop}, {"zLevre", d.zLevre}, {"longVis", d.longVis}};
  j["lo"] = {{"hx", d.lo.hx}, {"hy", d.lo.hy}, {"r", d.lo.r}};
  j["li"] = {{"hx", d.li.hx}, {"hy", d.li.hy}, {"r", d.li.r}};
  j["trous"] = Json::array();
  for (const auto& t : d.trous) j["trous"].push_back({{"id", t.id}, {"x", t.x}, {"y", t.y}});
  j["comps"] = Json::array();
  for (const auto& g : d.comps) {
    Json c = {{"id", g.id}, {"x", g.x}, {"y", g.y}, {"hx", g.hx}, {"hy", g.hy}, {"rot", g.rot}, {"top", g.top}};
    c["face"] = g.bord ? Json(g.face) : Json(nullptr);
    if (g.decoupe) {
      const auto& o = *g.decoupe;
      c["decoupe"] = {{"mur", lettreJson(o.mur)}, {"u", o.u}, {"zc", o.zc}, {"w", o.w}, {"h", o.h}, {"r", o.r}};
    } else {
      c["decoupe"] = nullptr;
    }
    if (g.trouCouvercle) c["trouCouvercle"] = {{"x", g.trouCouvercle->x}, {"y", g.trouCouvercle->y}, {"d", g.trouCouvercle->d}};
    else c["trouCouvercle"] = nullptr;
    j["comps"].push_back(c);
  }
  Json dec = Json::object();
  for (const auto& [mur, liste] : decoupesValides(d)) {
    Json ids = Json::array();
    for (const auto& o : liste) ids.push_back(o.id);
    dec[lettreJson(mur)] = ids;
  }
  j["decoupesValides"] = dec;
  j["piliersValides"] = Json::array();
  for (const auto& q : piliersValides(d)) j["piliersValides"].push_back(q.id);
  j["percagesValides"] = Json::array();
  for (const auto& q : percagesValides(d)) j["percagesValides"].push_back(q.id);
  return j;
}

Json problemesEnJson(const std::vector<Probleme>& pb) {
  Json j = Json::array();
  for (const auto& q : pb)
    j.push_back({{"sev", nomSeverite(q.sev)}, {"code", q.code}, {"msg", q.msg}, {"ids", q.ids}, {"fix", q.fix ? Json(q.fix->libelle) : Json(nullptr)}});
  return j;
}

std::string rapport(const std::vector<std::string>& ecarts) {
  std::string s;
  for (size_t i = 0; i < ecarts.size() && i < 12; ++i) s += "\n  " + ecarts[i];
  return s;
}

}  // namespace

TEST_CASE("Les cas de référence du prototype sont présents") {
  REQUIRE(reference().at("cas").size() >= 9);
}

TEST_CASE("Un projet du prototype est relu à l'identique") {
  for (const auto& cas : reference().at("cas")) {
    std::vector<std::string> e;
    comparer(versJson(normaliser(cas.at("projet"))), cas.at("projet"), cas.at("nom").get<std::string>(), false, e);
    INFO(cas.at("nom").get<std::string>() << rapport(e));
    CHECK(e.empty());
  }
}

TEST_CASE("La lecture tolérante corrige les valeurs absurdes comme le prototype") {
  for (const auto& cas : reference().at("cas")) {
    std::vector<std::string> e;
    comparer(versJson(normaliser(cas.at("source"))), cas.at("projet"), cas.at("nom").get<std::string>(), true, e);
    INFO(cas.at("nom").get<std::string>() << rapport(e));
    CHECK(e.empty());
  }
}

TEST_CASE("Les grandeurs dérivées sont identiques au prototype") {
  for (const auto& cas : reference().at("cas")) {
    std::vector<std::string> e;
    comparer(deriveEnJson(deriver(normaliser(cas.at("projet")))), cas.at("derive"), cas.at("nom").get<std::string>(), false, e);
    INFO(cas.at("nom").get<std::string>() << rapport(e));
    CHECK(e.empty());
  }
}

TEST_CASE("Les vérifications donnent les mêmes problèmes, mot pour mot") {
  for (const auto& cas : reference().at("cas")) {
    const Projet p = normaliser(cas.at("projet"));
    std::vector<std::string> e;
    comparer(problemesEnJson(verifier(p, deriver(p))), cas.at("problemes"), cas.at("nom").get<std::string>(), false, e);
    INFO(cas.at("nom").get<std::string>() << rapport(e));
    CHECK(e.empty());
  }
}

TEST_CASE("Les corrections automatiques suivent la même séquence") {
  for (const auto& cas : reference().at("cas")) {
    Projet q = normaliser(cas.at("projet"));
    const auto faites = corrigerTout(q);
    const auto& attendu = cas.at("corrige");
    std::vector<std::string> e;
    comparer(Json(faites), attendu.at("corrections"), "corrections", false, e);
    comparer(versJson(q), attendu.at("projet"), "projet", false, e);
    const Derive d = deriver(q);
    comparer(Json(d.H), attendu.at("H"), "H", false, e);
    Json codes = Json::array();
    for (const auto& pb : verifier(q, d)) codes.push_back(pb.code);
    comparer(codes, attendu.at("problemes"), "problemes", false, e);
    INFO(cas.at("nom").get<std::string>() << rapport(e));
    CHECK(e.empty());
  }
}

TEST_CASE("L'ajout automatique place les éléments aux mêmes endroits que le prototype") {
  const Json* attendu = nullptr;
  for (const auto& cas : reference().at("cas")) if (cas.at("nom") == "encombre") attendu = &cas.at("projet");
  REQUIRE(attendu != nullptr);
  Projet p = projetVide();
  for (const char* type : {"usbc", "led", "led", "bouton", "module", "condo", "jst", "capteur", "ic", "jack"}) CHECK_FALSE(ajouterComposant(p, type).empty());
  CHECK_FALSE(ajouterTrou(p).empty());
  Json obtenu = versJson(p), ref = *attendu;
  std::vector<std::string> e;
  comparer(obtenu, ref, "encombre", true, e);
  INFO(rapport(e));
  CHECK(e.empty());
}

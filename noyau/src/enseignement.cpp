// Prométhée : enseignement (lot L16 de la 0.34 ; 16.10 à 16.14). Voir enseignement.hpp. Licence GPL-3.0-only.
#include "promethee/enseignement.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <numeric>
#include <random>
#include <regex>
#include <sstream>

#include "promethee/archive.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/formules.hpp"
#include "promethee/hachage.hpp"
#include "promethee/valeurs.hpp"

namespace prom::ecad {

using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;

namespace {
std::string minuscules(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string sansEspaces(const std::string& s) {
  std::string r;
  for (char c : s)
    if (!std::isspace(static_cast<unsigned char>(c))) r += c;
  return r;
}
std::vector<std::string> chaines(const Json& j, const char* cle) {
  std::vector<std::string> r;
  if (j.contains(cle) && j[cle].is_array())
    for (const auto& x : j[cle])
      if (x.is_string()) r.push_back(x.get<std::string>());
  return r;
}
std::string echapperXmlTexte(const std::string& s) {
  std::string r;
  for (char c : s) {
    if (c == '&') r += "&amp;";
    else if (c == '<') r += "&lt;";
    else if (c == '>') r += "&gt;";
    else if (c == '"') r += "&quot;";
    else r += c;
  }
  return r;
}
std::string nombre(double v) {
  char b[48];
  std::snprintf(b, sizeof b, "%.6g", v);
  return b;
}
// Montage d'exemple : symboles posés en ligne, chaque broche nommée par une étiquette globale au bout d'un fil
struct Montage {
  ConceptionElec c;
  double x = 25.4, y = 50.8;
  explicit Montage(const std::string& nom) : c(nouvelleConception(nom)) {}
  std::string poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, y));
    x += 40.64;
    if (x > 250) x = 25.4, y += 50.8;
    std::string uuid;
    for (const auto& p : std::vector<SymbolePlace>(c.feuilles[0].symboles))
      if (p.uuid == s) {
        ComposantElec* r = c.composant(p.composant);
        if (!r) continue;
        r->reference = ref;
        if (!valeur.empty()) r->valeur = valeur;
        uuid = r->uuid;
        const DefSymbole* d = c.symbole(p.symbole);
        if (!d) continue;
        for (const auto& b : brochesPlacees(p, *d))
          for (const auto& [num, reseau] : liaisons)
            if (b.broche->numero == num) {
              const Pt dir = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
              const Pt bout{b.p.x - dir.x * 2'540'000, b.p.y - dir.y * 2'540'000};
              c.feuilles[0].fils.push_back({nouvelUuid(), b.p, bout});
              Etiquette e;
              e.uuid = nouvelUuid();
              e.texte = reseau;
              e.p = bout;
              e.type = TypeEtiquette::Globale;
              c.feuilles[0].etiquettes.push_back(e);
            }
      }
    return uuid;
  }
};
// Composants posés sur une feuille, sans les alimentations, drapeaux et pièces sans broche
std::vector<const ComposantElec*> composantsElectriques(const ConceptionElec& c) {
  std::set<std::string> poses;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (!s.composant.empty()) poses.insert(s.composant);
  std::vector<const ComposantElec*> v;
  for (const auto& x : c.composants) {
    if (!poses.count(x.uuid) || x.genre == GenreComposant::Graphique || x.genre == GenreComposant::Mecanique) continue;
    const DefSymbole* d = c.symbole(x.symbole);
    if (!d || d->broches.empty() || d->alimentation || d->drapeau) continue;
    v.push_back(&x);
  }
  return v;
}
// Valeur comprise d'un composant (nombre dans l'unité de base), ou le texte tel quel
std::string valeurNormalisee(const ConceptionElec& c, const ComposantElec& x) {
  const FamilleValeur f = familleDe(c, x);
  const ValeurComprise v = lireValeurComposant(x.valeur, f);
  if (v.comprise()) return nombre(v.nombre);
  return minuscules(sansEspaces(x.valeur));
}
bool reseauAlimentation(const std::string& nom) {
  std::string m;
  for (char ch : nom) m += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return m == "GND" || m == "0" || m == "VCC" || m == "VDD" || m == "VSS" || m == "VEE" || (!m.empty() && (m[0] == '+' || m[0] == '-'));
}
}  // namespace

// ======================================================================================================================
// Salle de TP (16.10)
// ======================================================================================================================
std::optional<ReglagesEtablissement> lireReglagesEtablissement(const std::string& json, std::string* erreur) {
  auto refuser = [&](const std::string& e) -> std::optional<ReglagesEtablissement> {
    if (erreur) *erreur = e;
    return std::nullopt;
  };
  Json j;
  try {
    j = Json::parse(json);
  } catch (const std::exception& e) {
    return refuser(std::string("fichier de l'établissement illisible : ") + e.what());
  }
  if (!j.is_object()) return refuser("fichier de l'établissement : un objet JSON est attendu");
  ReglagesEtablissement r;
  r.nom = j.value("nom", std::string());
  r.dossierBibliotheques = j.value("dossierBibliotheques", std::string());
  r.profilFabricant = j.value("profilFabricant", std::string());
  r.interfaceSimplifiee = j.value("interfaceSimplifiee", false);
  r.interfaceVerrouillee = j.value("interfaceVerrouillee", false);
  r.actionsMasquees = chaines(j, "actionsMasquees");
  r.dossierClasse = j.value("dossierClasse", std::string());
  if (!r.profilFabricant.empty()) {
    bool connu = false;
    std::string liste;
    for (const auto& p : profilsFabricants()) {
      connu |= p.nom == r.profilFabricant;
      liste += (liste.empty() ? "" : ", ") + p.nom;
    }
    if (!connu) return refuser("profil de fabricant inconnu : « " + r.profilFabricant + " » (connus : " + liste + ")");
  }
  return r;
}

std::string ecrireReglagesEtablissement(const ReglagesEtablissement& r) {
  Json j = {{"format", "promethee-etablissement"},
            {"version", 1},
            {"nom", r.nom},
            {"dossierBibliotheques", r.dossierBibliotheques},
            {"profilFabricant", r.profilFabricant},
            {"interfaceSimplifiee", r.interfaceSimplifiee},
            {"interfaceVerrouillee", r.interfaceVerrouillee},
            {"actionsMasquees", r.actionsMasquees},
            {"dossierClasse", r.dossierClasse}};
  return j.dump(2) + "\n";
}

std::string resoudreDossier(const std::string& dossierDuFichier, const std::string& chemin) {
  if (chemin.empty()) return {};
  const fs::path p = cheminFichier(chemin);
  return utf8((p.is_absolute() ? p : cheminFichier(dossierDuFichier) / p).lexically_normal());
}

ConceptionElec exempleDiviseur() {
  Montage m("Diviseur de tension");
  m.poser("Pile", "BT1", "5V", {{"1", "+5V"}, {"2", "GND"}});
  m.poser("R", "R2", "10k", {{"1", "+5V"}, {"2", "MID"}});
  m.poser("R", "R1", "10k", {{"1", "MID"}, {"2", "GND"}});
  return m.c;
}

ConceptionElec exempleFiltreRC() {
  Montage m("Filtre RC passe-bas");
  m.poser("Source_tension", "VG1", "DC 0 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
  m.poser("R", "R1", "1k59", {{"1", "ENTREE"}, {"2", "SORTIE"}});
  m.poser("C", "C1", "100n", {{"1", "SORTIE"}, {"2", "GND"}});
  return m.c;
}

const std::vector<ExempleCours>& recueilExemples() {
  static const std::vector<ExempleCours> v = [] {
    std::vector<ExempleCours> r;
    r.push_back({"bases-diviseur", "Diviseur de tension", "bases", "Deux résistances égales partagent la tension de la pile : 2,5 V au milieu. Change R1 et regarde la tension suivre V × R1 / (R1 + R2).",
                 exempleDiviseur});
    r.push_back({"bases-led", "LED et sa résistance", "bases", "La résistance limite le courant de la LED : (5 V − 2 V) / 330 Ω ≈ 9 mA. Sans elle, la LED claque.", [] {
                   Montage m("LED et sa résistance");
                   m.poser("Pile", "BT1", "5V", {{"1", "+5V"}, {"2", "GND"}});
                   m.poser("R", "R1", "330", {{"1", "+5V"}, {"2", "A"}});
                   m.poser("LED", "D1", "rouge", {{"2", "A"}, {"1", "GND"}});
                   return m.c;
                 }});
    r.push_back({"filtres-rc", "Filtre RC passe-bas", "filtres", "Au-dessus de 1 / (2π R C) ≈ 1 kHz, le condensateur court-circuite le signal : le gain baisse de 20 dB par décade.",
                 exempleFiltreRC});
    r.push_back({"filtres-cr", "Filtre CR passe-haut", "filtres", "Le condensateur en série bloque le continu ; au-dessus de 1 kHz, le signal passe.", [] {
                   Montage m("Filtre CR passe-haut");
                   m.poser("Source_tension", "VG1", "DC 0 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
                   m.poser("C", "C1", "100n", {{"1", "ENTREE"}, {"2", "SORTIE"}});
                   m.poser("R", "R1", "1k59", {{"1", "SORTIE"}, {"2", "GND"}});
                   return m.c;
                 }});
    r.push_back({"amplificateurs-emetteur", "Amplificateur à émetteur commun", "amplificateurs",
                 "Le transistor amplifie le petit signal de la base ; le pont de base fixe le point de repos, la résistance d'émetteur le stabilise.", [] {
                   Montage m("Émetteur commun");
                   m.poser("Pile", "BT1", "12V", {{"1", "VCC"}, {"2", "GND"}});
                   m.poser("R", "R1", "47k", {{"1", "VCC"}, {"2", "B"}});
                   m.poser("R", "R2", "10k", {{"1", "B"}, {"2", "GND"}});
                   m.poser("R", "R3", "4k7", {{"1", "VCC"}, {"2", "C"}});
                   m.poser("R", "R4", "1k", {{"1", "E"}, {"2", "GND"}});
                   m.poser("Q_NPN", "Q1", "2N2222", {{"1", "E"}, {"2", "B"}, {"3", "C"}});
                   return m.c;
                 }});
    r.push_back({"555-clignotant", "Clignotant à NE555", "555", "Le condensateur se charge par R1 + R2 et se décharge par R2 : la LED clignote à environ 1,44 / ((R1 + 2 R2) C).",
                 exempleElectronique});
    r.push_back({"logique-additionneur", "Additionneur complet", "logique", "Deux OU exclusifs donnent la somme, deux ET et un OU la retenue : la table de vérité de A + B + retenue.", [] {
                   Montage m("Additionneur complet");
                   m.poser("Source_tension", "VA", "PULSE(0 5 0 1u 1u 0.5m 1m)", {{"1", "A"}, {"2", "GND"}});
                   m.poser("Source_tension", "VB", "PULSE(0 5 0 1u 1u 1m 2m)", {{"1", "B"}, {"2", "GND"}});
                   m.poser("Source_tension", "VC", "PULSE(0 5 0 1u 1u 2m 4m)", {{"1", "CIN"}, {"2", "GND"}});
                   m.poser("Porte_OU_X", "U1", "", {{"1", "A"}, {"2", "B"}, {"3", "T1"}});
                   m.poser("Porte_OU_X", "U2", "", {{"1", "T1"}, {"2", "CIN"}, {"3", "S"}});
                   m.poser("Porte_ET", "U3", "", {{"1", "A"}, {"2", "B"}, {"3", "T2"}});
                   m.poser("Porte_ET", "U4", "", {{"1", "T1"}, {"2", "CIN"}, {"3", "T3"}});
                   m.poser("Porte_OU", "U5", "", {{"1", "T2"}, {"2", "T3"}, {"3", "COUT"}});
                   return m.c;
                 }});
    r.push_back({"alimentations-zener", "Régulateur à diode Zener", "alimentations", "La Zener tient sa tension de claquage : la sortie reste près de 5,1 V tant que le courant de la résistance suffit.", [] {
                   Montage m("Régulateur Zener");
                   m.poser("Pile", "BT1", "12V", {{"1", "VIN"}, {"2", "GND"}});
                   m.poser("R", "R1", "470", {{"1", "VIN"}, {"2", "VOUT"}});
                   m.poser("D_Zener", "D1", "5V1", {{"1", "VOUT"}, {"2", "GND"}});
                   m.poser("R", "R2", "1k", {{"1", "VOUT"}, {"2", "GND"}});
                   return m.c;
                 }});
    return r;
  }();
  return v;
}

const ExempleCours* exempleCours(const std::string& id) {
  for (const auto& e : recueilExemples())
    if (e.id == id) return &e;
  return nullptr;
}

// ======================================================================================================================
// Consignes (16.11)
// ======================================================================================================================
const char* codeCondition(GenreCondition g) {
  switch (g) {
    case GenreCondition::ComposantPose: return "composant";
    case GenreCondition::BrochesReliees: return "liaison";
    case GenreCondition::MesureDansPlage: return "mesure";
    case GenreCondition::Reponse: return "reponse";
    default: return "aucune";
  }
}
GenreCondition conditionDepuis(const std::string& code) {
  if (code == "composant") return GenreCondition::ComposantPose;
  if (code == "liaison") return GenreCondition::BrochesReliees;
  if (code == "mesure") return GenreCondition::MesureDansPlage;
  if (code == "reponse") return GenreCondition::Reponse;
  return GenreCondition::Aucune;
}
const std::vector<std::pair<std::string, std::string>>& conditionsProposees() {
  static const std::vector<std::pair<std::string, std::string>> v{{"aucune", "Aucune (étape à lire)"},
                                                                  {"composant", "Composant posé"},
                                                                  {"liaison", "Broches reliées"},
                                                                  {"mesure", "Mesure dans une plage"},
                                                                  {"reponse", "Réponse à une question"}};
  return v;
}

namespace {
Json conditionEnJson(const ConditionEtape& c) {
  Json j = {{"genre", codeCondition(c.genre)}};
  switch (c.genre) {
    case GenreCondition::ComposantPose: j["symbole"] = c.symbole, j["valeur"] = c.valeur, j["nombre"] = c.nombre; break;
    case GenreCondition::BrochesReliees: j["a"] = c.a, j["b"] = c.b; break;
    case GenreCondition::MesureDansPlage: j["reseau"] = c.a, j["grandeur"] = c.grandeur, j["min"] = c.min, j["max"] = c.max; break;
    case GenreCondition::Reponse: j["reponse"] = c.reponse; break;
    default: break;
  }
  return j;
}
ConditionEtape conditionDepuisJson(const Json& j) {
  ConditionEtape c;
  if (!j.is_object()) return c;
  c.genre = conditionDepuis(j.value("genre", std::string("aucune")));
  c.symbole = j.value("symbole", std::string());
  c.valeur = j.value("valeur", std::string());
  c.nombre = j.value("nombre", 1);
  c.a = j.value("a", j.value("reseau", std::string()));
  c.b = j.value("b", std::string());
  c.grandeur = j.value("grandeur", std::string("tension"));
  c.min = j.value("min", 0.0);
  c.max = j.value("max", 0.0);
  c.reponse = j.value("reponse", std::string());
  return c;
}
Json consigneEnJson(const Consigne& c) {
  Json etapes = Json::array();
  for (const auto& e : c.etapes) {
    Json x = {{"titre", e.titre}, {"texte", e.texte}};
    if (!e.media.empty()) x["media"] = e.media;
    if (!e.question.empty()) x["question"] = e.question;
    if (e.instant >= 0) x["instant"] = e.instant;
    x["condition"] = conditionEnJson(e.condition);
    etapes.push_back(x);
  }
  return {{"id", c.id}, {"titre", c.titre}, {"description", c.description}, {"etapes", etapes}};
}
Consigne consigneDepuisJson(const Json& j) {
  Consigne c;
  c.id = j.value("id", std::string());
  c.titre = j.value("titre", std::string());
  c.description = j.value("description", std::string());
  if (j.contains("etapes") && j["etapes"].is_array())
    for (const auto& x : j["etapes"]) {
      EtapeConsigne e;
      e.titre = x.value("titre", std::string());
      e.texte = x.value("texte", std::string());
      e.media = x.value("media", std::string());
      e.question = x.value("question", std::string());
      e.instant = x.value("instant", -1.0);
      if (x.contains("condition")) e.condition = conditionDepuisJson(x["condition"]);
      c.etapes.push_back(e);
    }
  return c;
}
// « R1.2 » : composant de repère R1, broche 2 ; sinon un nom de réseau
std::optional<std::pair<std::string, std::string>> brocheDesignee(const ConceptionElec& c, const std::string& texte) {
  const auto point = texte.rfind('.');
  if (point == std::string::npos || point == 0 || point + 1 >= texte.size()) return std::nullopt;
  const ComposantElec* x = c.parReference(texte.substr(0, point));
  if (!x) return std::nullopt;
  return std::pair{x->uuid, texte.substr(point + 1)};
}
int reseauDesigne(const ConceptionElec& c, const Connectivite& k, const std::string& texte) {
  if (const auto b = brocheDesignee(c, texte)) return k.reseau(b->first, b->second);
  return k.indice(texte);
}
std::optional<double> nombreDe(const std::string& texte) {
  // Nombre en notation d'ingénieur : « 2,5 », « 10k », « 4k7 », « 100 nF », « 2.2u », « 1e3 »
  std::string t;
  for (char ch : texte) t += ch == ',' ? '.' : ch;
  static const std::regex motif(R"(^\s*([-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)\s*(p|n|u|\xC2\xB5|m|k|K|M|G|meg|MEG)?([0-9]*))");
  std::smatch m;
  if (!std::regex_search(t, m, motif)) return std::nullopt;
  double v = std::stod(m[1]);
  const std::string prefixe = m[2];
  static const std::map<std::string, double> facteurs{{"p", 1e-12}, {"n", 1e-9}, {"u", 1e-6}, {"\xC2\xB5", 1e-6}, {"m", 1e-3}, {"k", 1e3}, {"K", 1e3}, {"M", 1e6}, {"meg", 1e6}, {"MEG", 1e6}, {"G", 1e9}};
  if (!prefixe.empty()) {
    const double f = facteurs.at(prefixe);
    // « 4k7 » : les chiffres après le préfixe sont la partie décimale
    if (m[3].length() > 0 && m[1].str().find('.') == std::string::npos) v = std::stod(m[1].str() + "." + m[3].str());
    v *= f;
  }
  return v;
}
}  // namespace

std::vector<Consigne> consignesDe(const ConceptionElec& c) {
  std::vector<Consigne> r;
  const auto it = c.extensions.find("consignes");
  if (it == c.extensions.end() || !it->second.is_array()) return r;
  for (const auto& j : it->second) r.push_back(consigneDepuisJson(j));
  return r;
}
void ecrireConsignes(ConceptionElec& c, const std::vector<Consigne>& v) {
  Json a = Json::array();
  for (const auto& x : v) a.push_back(consigneEnJson(x));
  c.extensions["consignes"] = a;
}
std::vector<SerieExercices> seriesDe(const ConceptionElec& c) {
  std::vector<SerieExercices> r;
  const auto it = c.extensions.find("series");
  if (it == c.extensions.end() || !it->second.is_array()) return r;
  for (const auto& j : it->second) {
    SerieExercices s;
    s.titre = j.value("titre", std::string());
    s.circuits = chaines(j, "circuits");
    s.consignes = chaines(j, "consignes");
    if (j.contains("problemes") && j["problemes"].is_array())
      for (const auto& p : j["problemes"]) s.problemes.emplace_back(p.value("enonce", std::string()), p.value("corrige", std::string()));
    r.push_back(s);
  }
  return r;
}
void ecrireSeries(ConceptionElec& c, const std::vector<SerieExercices>& v) {
  Json a = Json::array();
  for (const auto& s : v) {
    Json p = Json::array();
    for (const auto& [e, k] : s.problemes) p.push_back({{"enonce", e}, {"corrige", k}});
    a.push_back({{"titre", s.titre}, {"circuits", s.circuits}, {"consignes", s.consignes}, {"problemes", p}});
  }
  c.extensions["series"] = a;
}
std::string consigneEnTexte(const Consigne& c) {
  Json j = consigneEnJson(c);
  j["format"] = "promethee-consigne";
  j["version"] = 1;
  return j.dump(2) + "\n";
}
std::optional<Consigne> consigneDepuisTexte(const std::string& texte, std::string* erreur) {
  try {
    const Json j = Json::parse(texte);
    if (!j.is_object() || j.value("format", std::string()) != "promethee-consigne") {
      if (erreur) *erreur = "ce fichier n'est pas une consigne de Prométhée";
      return std::nullopt;
    }
    return consigneDepuisJson(j);
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return std::nullopt;
  }
}

EtatEtape verifierEtape(const EtapeConsigne& e, const ConceptionElec& eleve, const std::string& reponse) {
  EtatEtape r;
  const ConditionEtape& k = e.condition;
  switch (k.genre) {
    case GenreCondition::Aucune:
      r.faite = true;
      r.raison = "étape à lire";
      return r;
    case GenreCondition::ComposantPose: {
      int n = 0;
      for (const ComposantElec* x : composantsElectriques(eleve)) {
        const bool genre = k.symbole.empty() || x->symbole == k.symbole || (x->reference.rfind(k.symbole, 0) == 0 && x->reference.size() > k.symbole.size() && std::isdigit(static_cast<unsigned char>(x->reference[k.symbole.size()])));
        if (!genre) continue;
        if (!k.valeur.empty()) {
          // Valeur voulue et valeur posée lues dans la famille du composant (« 10k » d'une résistance : 10 000 Ω)
          const FamilleValeur f = familleDe(eleve, *x);
          const ValeurComprise voulue = lireValeurComposant(k.valeur, f), posee = lireValeurComposant(x->valeur, f);
          if (voulue.comprise() && posee.comprise()) {
            if (std::abs(posee.nombre - voulue.nombre) > 0.01 * std::abs(voulue.nombre)) continue;
          } else if (minuscules(sansEspaces(x->valeur)) != minuscules(sansEspaces(k.valeur)))
            continue;
        }
        ++n;
      }
      r.faite = n >= std::max(1, k.nombre);
      r.raison = std::to_string(n) + " composant(s) " + (k.symbole.empty() ? std::string() : k.symbole + " ") + (k.valeur.empty() ? std::string() : "de " + k.valeur + " ") + "posé(s), " +
                 std::to_string(std::max(1, k.nombre)) + " attendu(s)";
      return r;
    }
    case GenreCondition::BrochesReliees: {
      const Connectivite c = connectivite(eleve);
      const int a = reseauDesigne(eleve, c, k.a), b = reseauDesigne(eleve, c, k.b);
      r.faite = a >= 0 && a == b;
      r.raison = r.faite ? k.a + " et " + k.b + " sont reliées" : a < 0 ? k.a + " n'est reliée à rien" : b < 0 ? k.b + " n'est reliée à rien" : k.a + " et " + k.b + " ne sont pas reliées";
      return r;
    }
    case GenreCondition::MesureDansPlage: {
      if (!ngspiceDisponible()) {
        r.raison = "ngspice absent : mesure impossible";
        return r;
      }
      AnalyseSpice an;
      an.type = AnalyseSpice::Type::PointDeRepos;
      const NetlistSpice n = netlistSpice(eleve, an);
      if (!n.valide()) {
        r.raison = "le montage ne se simule pas : " + n.erreurs.front();
        return r;
      }
      const ResultatSimulation s = simuler(n);
      const std::string vec = vecteurTension(n, k.a);
      const auto* v = vec.empty() ? nullptr : s.reel(vec);
      if (!s.reussi() || !v || v->empty()) {
        r.raison = "pas de mesure sur « " + k.a + " »" + (s.erreur.empty() ? std::string() : " : " + s.erreur);
        return r;
      }
      const double x = v->back();
      r.faite = x >= k.min && x <= k.max;
      char t[160];
      std::snprintf(t, sizeof t, "%s = %.3g V (attendu entre %.3g et %.3g V)", k.a.c_str(), x, k.min, k.max);
      r.raison = t;
      return r;
    }
    case GenreCondition::Reponse: {
      const auto attendu = nombreDe(k.reponse), donne = nombreDe(reponse);
      if (attendu && donne) r.faite = std::abs(*donne - *attendu) <= 0.02 * std::abs(*attendu) + 1e-12;
      else r.faite = !reponse.empty() && minuscules(sansEspaces(reponse)) == minuscules(sansEspaces(k.reponse));
      r.raison = reponse.empty() ? "réponse attendue" : r.faite ? "réponse juste" : "réponse à revoir";
      return r;
    }
  }
  return r;
}

ParcoursApprendre parcoursDepuisConsigne(const Consigne& c) {
  ParcoursApprendre p;
  p.id = "consigne:" + c.id;
  p.nom = c.titre;
  p.description = c.description;
  for (const auto& e : c.etapes) {
    EtapeApprendre x;
    x.titre = e.titre;
    x.pourquoi = e.texte;
    x.comment = e.question;
    if (e.condition.genre != GenreCondition::Aucune && e.condition.genre != GenreCondition::Reponse) x.faite = [e](const ConceptionElec& eleve) { return verifierEtape(e, eleve).faite; };
    p.etapes.push_back(x);
  }
  return p;
}

// ======================================================================================================================
// Couche de l'enseignant (16.12)
// ======================================================================================================================
namespace {
const char* codePanne(GenrePanne g) { return g == GenrePanne::Coupure ? "coupure" : g == GenrePanne::CourtCircuit ? "court-circuit" : "valeur"; }
GenrePanne panneDepuis(const std::string& s) { return s == "court-circuit" ? GenrePanne::CourtCircuit : s == "valeur" ? GenrePanne::ValeurFausse : GenrePanne::Coupure; }
// Chiffrement léger des pannes : la couche est lisible, les pannes ne s'y lisent pas d'un coup d'œil (le sceau,
// lui, rend visible toute retouche)
std::string flotChiffre(const std::string& clair) {
  std::string r(clair.size(), '\0');
  std::string bloc;
  for (std::size_t i = 0; i < clair.size(); ++i) {
    if (i % 32 == 0) {
      const std::string h = sha256Hex("promethee-pannes:" + std::to_string(i / 32));
      bloc.clear();
      for (std::size_t k = 0; k + 1 < h.size(); k += 2) bloc += static_cast<char>(std::stoi(h.substr(k, 2), nullptr, 16));
    }
    r[i] = static_cast<char>(clair[i] ^ bloc[i % 32]);
  }
  return r;
}
std::string versHex(const std::string& s) {
  static const char* c = "0123456789abcdef";
  std::string r;
  for (unsigned char x : s) r += c[x >> 4], r += c[x & 15];
  return r;
}
std::string depuisHex(const std::string& s) {
  std::string r;
  for (std::size_t i = 0; i + 1 < s.size(); i += 2) r += static_cast<char>(std::stoi(s.substr(i, 2), nullptr, 16));
  return r;
}
Json pannesEnJson(const std::vector<PanneCachee>& v) {
  Json a = Json::array();
  for (const auto& p : v) a.push_back({{"genre", codePanne(p.genre)}, {"composant", p.composant}, {"broche", p.broche}, {"a", p.a}, {"b", p.b}, {"valeur", p.valeur}, {"titre", p.titre}});
  return a;
}
std::vector<PanneCachee> pannesDepuisJson(const Json& a) {
  std::vector<PanneCachee> r;
  if (!a.is_array()) return r;
  for (const auto& j : a) {
    PanneCachee p;
    p.genre = panneDepuis(j.value("genre", std::string("coupure")));
    p.composant = j.value("composant", std::string());
    p.broche = j.value("broche", std::string());
    p.a = j.value("a", std::string());
    p.b = j.value("b", std::string());
    p.valeur = j.value("valeur", std::string());
    p.titre = j.value("titre", std::string());
    r.push_back(p);
  }
  return r;
}
Json coucheEnJson(const CoucheEnseignant& e, bool avecSceau) {
  Json restr = {{"actionsMasquees", e.restrictions.actionsMasquees},
                {"instrumentsMasques", e.restrictions.instrumentsMasques},
                {"analysesMasquees", e.restrictions.analysesMasquees},
                {"interfaceSimplifiee", e.restrictions.interfaceSimplifiee},
                {"verrouillee", e.restrictions.verrouillee},
                {"profilSimulation", e.restrictions.profilSimulation}};
  Json consignes = Json::array();
  for (const auto& c : e.consignes) consignes.push_back(consigneEnJson(c));
  Json j = {{"verrous", e.verrous}, {"restrictions", restr}, {"pannes", versHex(flotChiffre(pannesEnJson(e.pannes).dump()))}, {"consignes", consignes}};
  if (avecSceau) j["sceau"] = e.sceau;
  return j;
}
// Contenu des parties verrouillées, dans un ordre fixe : composant, ses poses, les réseaux de ses broches
std::string contenuVerrouille(const ConceptionElec& c, const CoucheEnseignant& e) {
  std::ostringstream o;
  const Connectivite k = connectivite(c);
  std::vector<std::string> v = e.verrous;
  std::sort(v.begin(), v.end());
  for (const auto& u : v) {
    const ComposantElec* x = c.composant(u);
    if (!x) {
      o << "absent:" << u << "\n";
      continue;
    }
    o << u << "|" << x->reference << "|" << x->symbole << "|" << x->valeur << "|" << x->empreinte << "\n";
    for (const auto& [n, val] : std::map<std::string, std::string>(x->champs.begin(), x->champs.end())) o << "  " << n << "=" << val << "\n";
    for (const auto& f : c.feuilles)
      for (const auto& s : f.symboles)
        if (s.composant == u) o << "  pose " << f.uuid << " " << s.position.x << " " << s.position.y << " " << s.rotation << " " << s.miroir << "\n";
    std::vector<std::pair<std::string, std::string>> broches;
    for (const auto& [cle, r] : k.parBroche)
      if (cle.first == u) broches.emplace_back(cle.second, k.reseaux[static_cast<std::size_t>(r)].nom);
    std::sort(broches.begin(), broches.end());
    for (const auto& [b, r] : broches) o << "  broche " << b << " " << r << "\n";
  }
  return o.str();
}
}  // namespace

std::optional<CoucheEnseignant> coucheEnseignant(const ConceptionElec& c) {
  const auto it = c.extensions.find("enseignant");
  if (it == c.extensions.end() || !it->second.is_object()) return std::nullopt;
  const Json& j = it->second;
  CoucheEnseignant e;
  e.verrous = chaines(j, "verrous");
  if (j.contains("restrictions") && j["restrictions"].is_object()) {
    const Json& r = j["restrictions"];
    e.restrictions.actionsMasquees = chaines(r, "actionsMasquees");
    e.restrictions.instrumentsMasques = chaines(r, "instrumentsMasques");
    e.restrictions.analysesMasquees = chaines(r, "analysesMasquees");
    e.restrictions.interfaceSimplifiee = r.value("interfaceSimplifiee", false);
    e.restrictions.verrouillee = r.value("verrouillee", false);
    if (r.contains("profilSimulation") && r["profilSimulation"].is_object())
      for (const auto& [cle, v] : r["profilSimulation"].items())
        if (v.is_string()) e.restrictions.profilSimulation[cle] = v.get<std::string>();
  }
  if (j.contains("pannes") && j["pannes"].is_string()) {
    try {
      e.pannes = pannesDepuisJson(Json::parse(flotChiffre(depuisHex(j["pannes"].get<std::string>()))));
    } catch (...) {
    }
  }
  if (j.contains("consignes") && j["consignes"].is_array())
    for (const auto& x : j["consignes"]) e.consignes.push_back(consigneDepuisJson(x));
  e.sceau = j.value("sceau", std::string());
  return e;
}

void ecrireCoucheEnseignant(ConceptionElec& c, const CoucheEnseignant& e) { c.extensions["enseignant"] = coucheEnJson(e, true); }

std::string calculerSceau(const ConceptionElec& c, const CoucheEnseignant& e, const std::string& secret) {
  return hmacSha256Hex(secret, coucheEnJson(e, false).dump() + "\n" + contenuVerrouille(c, e));
}

void sceller(ConceptionElec& c, const std::string& secret) {
  CoucheEnseignant e = coucheEnseignant(c).value_or(CoucheEnseignant{});
  e.sceau = calculerSceau(c, e, secret);
  ecrireCoucheEnseignant(c, e);
}

EtatSceau verifierSceau(const ConceptionElec& c, const std::string& secret) {
  const auto e = coucheEnseignant(c);
  if (!e || e->sceau.empty()) return EtatSceau::Absent;
  return calculerSceau(c, *e, secret) == e->sceau ? EtatSceau::Intact : EtatSceau::Retouche;
}

std::string refusEnseignant(const ConceptionElec& avant, const ConceptionElec& apres) {
  const auto e = coucheEnseignant(avant);
  if (!e) return {};
  const auto ia = avant.extensions.find("enseignant"), ib = apres.extensions.find("enseignant");
  if (ib == apres.extensions.end() || ia->second != ib->second) return "La couche de l'enseignant ne se modifie pas depuis le projet de l'élève.";
  if (e->verrous.empty()) return {};
  if (contenuVerrouille(avant, *e) == contenuVerrouille(apres, *e)) return {};
  for (const auto& u : e->verrous) {
    const ComposantElec* x = avant.composant(u);
    CoucheEnseignant seul = *e;
    seul.verrous = {u};
    if (contenuVerrouille(avant, seul) != contenuVerrouille(apres, seul)) return (x ? x->reference : u) + " est verrouillé par l'enseignant : ni sa valeur, ni sa place, ni ses liaisons ne changent.";
  }
  return "Une partie verrouillée par l'enseignant a changé.";
}

bool actionPermise(const ConceptionElec& c, const std::string& action) {
  const auto e = coucheEnseignant(c);
  if (!e) return true;
  const auto& m = e->restrictions.actionsMasquees;
  return std::find(m.begin(), m.end(), action) == m.end();
}

std::vector<PanneCachee> pannesCachees(const ConceptionElec& c) {
  const auto e = coucheEnseignant(c);
  return e ? e->pannes : std::vector<PanneCachee>{};
}

void appliquerPannes(NetlistSpice& n, const ConceptionElec& c, const std::vector<PanneCachee>& pannes) {
  if (pannes.empty()) return;
  std::vector<std::string> lignes;
  {
    std::istringstream in(n.texte);
    std::string l;
    while (std::getline(in, l)) lignes.push_back(l);
  }
  // Élément SPICE d'un repère : celui que la netlist a noté pour lui (le premier, s'il y en a plusieurs)
  auto element = [&](const std::string& ref) -> std::string {
    for (const auto& [el, r] : n.instances)
      if (r == ref) return el;
    return "r" + minuscules(ref);
  };
  auto ligneDe = [&](const std::string& el) -> int {
    for (std::size_t i = 0; i < lignes.size(); ++i) {
      std::istringstream ls(lignes[i]);
      std::string t;
      if (ls >> t && minuscules(t) == el) return static_cast<int>(i);
    }
    return -1;
  };
  auto mots = [](const std::string& l) {
    std::vector<std::string> v;
    std::istringstream ls(l);
    std::string t;
    while (ls >> t) v.push_back(t);
    return v;
  };
  std::vector<std::string> ajouts;
  int k = 0;
  for (const auto& p : pannes) {
    ++k;
    if (p.genre == GenrePanne::CourtCircuit) {
      const auto a = n.noeuds.find(p.a), b = n.noeuds.find(p.b);
      if (a != n.noeuds.end() && b != n.noeuds.end()) ajouts.push_back("Rpanne_cc" + std::to_string(k) + " " + a->second + " " + b->second + " 1m");
      continue;
    }
    const std::string el = element(p.composant);
    const int i = ligneDe(el);
    if (i < 0) continue;
    std::vector<std::string> m = mots(lignes[static_cast<std::size_t>(i)]);
    if (m.size() < 3) continue;
    if (p.genre == GenrePanne::Coupure) {
      // La broche ouverte quitte son nœud : une résistance de 1 TΩ la retient au circuit
      std::size_t rang = 1;
      if (!p.broche.empty())
        if (const int b = std::atoi(p.broche.c_str()); b >= 1 && static_cast<std::size_t>(b) < m.size() - 1) rang = static_cast<std::size_t>(b);
      const std::string libre = "panne_" + el + "_" + std::to_string(rang);
      ajouts.push_back("Rpanne_" + el + "_" + std::to_string(rang) + " " + libre + " " + m[rang] + " 1e12");
      m[rang] = libre;
    } else {
      // Valeur fausse : la valeur des résistances, condensateurs et inductances
      const char g = static_cast<char>(std::tolower(static_cast<unsigned char>(el[0])));
      if ((g == 'r' || g == 'c' || g == 'l') && m.size() >= 4) {
        const ComposantElec* x = c.parReference(p.composant);
        const ValeurComprise v = lireValeurComposant(p.valeur, x ? familleDe(c, *x) : FamilleValeur::Aucune);
        m[3] = v.comprise() ? nombre(v.nombre) : p.valeur;
      }
    }
    std::string l;
    for (std::size_t j = 0; j < m.size(); ++j) l += (j ? " " : "") + m[j];
    lignes[static_cast<std::size_t>(i)] = l;
  }
  std::ostringstream o;
  for (const auto& l : lignes) {
    if (minuscules(sansEspaces(l)) == ".end")
      for (const auto& a : ajouts) o << a << "\n";
    o << l << "\n";
  }
  n.texte = o.str();
}

// ======================================================================================================================
// Évaluation (16.13) : isomorphisme de graphes étiquetés, VF2
// ======================================================================================================================
namespace {
struct Graphe {
  std::vector<std::string> etiquettes;                    // sommets : composants puis réseaux
  std::vector<std::string> noms;                          // repère du composant, nom du réseau
  std::vector<std::map<int, std::multiset<std::string>>> voisins;  // voisin → étiquettes des arêtes
  int composants = 0;
};
bool symetrique(const std::string& symbole) {
  return symbole == "R" || symbole == "C" || symbole == "L" || symbole == "Fusible" || symbole == "Interrupteur" || symbole == "Bouton" || symbole == "Buzzer" || symbole == "Ferrite";
}
Graphe graphe(const ConceptionElec& c) {
  Graphe g;
  const Connectivite k = connectivite(c);
  const auto comps = composantsElectriques(c);
  std::map<int, int> sommetReseau;
  for (const ComposantElec* x : comps) {
    g.etiquettes.push_back("c:" + x->symbole + ":" + valeurNormalisee(c, *x));
    g.noms.push_back(x->reference);
    g.voisins.emplace_back();
  }
  g.composants = static_cast<int>(comps.size());
  for (std::size_t i = 0; i < comps.size(); ++i) {
    const ComposantElec* x = comps[i];
    const DefSymbole* d = c.symbole(x->symbole);
    std::set<std::string> vues;
    for (const auto& b : d->broches) {
      if (!vues.insert(b.numero).second) continue;
      const int r = k.reseau(x->uuid, b.numero);
      if (r < 0) continue;
      auto it = sommetReseau.find(r);
      if (it == sommetReseau.end()) {
        const std::string& nom = k.reseaux[static_cast<std::size_t>(r)].nom;
        g.etiquettes.push_back(reseauAlimentation(nom) ? "n:" + nom : std::string("n:"));
        g.noms.push_back(nom);
        g.voisins.emplace_back();
        it = sommetReseau.emplace(r, static_cast<int>(g.etiquettes.size()) - 1).first;
      }
      const std::string e = symetrique(x->symbole) ? std::string("p") : b.numero;
      g.voisins[i][it->second].insert(e);
      g.voisins[static_cast<std::size_t>(it->second)][static_cast<int>(i)].insert(e);
    }
  }
  return g;
}
class Vf2 {
 public:
  Vf2(const Graphe& a, const Graphe& b) : m_a(a), m_b(b), m_ab(a.etiquettes.size(), -1), m_ba(b.etiquettes.size(), -1) {
    // Ordre : en largeur depuis le sommet le plus rare, pour que chaque sommet ait des voisins déjà placés
    const int n = static_cast<int>(a.etiquettes.size());
    std::map<std::string, int> frequence;
    for (const auto& e : a.etiquettes) ++frequence[e];
    std::vector<bool> vu(static_cast<std::size_t>(n), false);
    while (static_cast<int>(m_ordre.size()) < n) {
      int depart = -1;
      for (int i = 0; i < n; ++i)
        if (!vu[static_cast<std::size_t>(i)] && (depart < 0 || frequence[a.etiquettes[static_cast<std::size_t>(i)]] < frequence[a.etiquettes[static_cast<std::size_t>(depart)]] ||
                                                 (frequence[a.etiquettes[static_cast<std::size_t>(i)]] == frequence[a.etiquettes[static_cast<std::size_t>(depart)]] &&
                                                  a.voisins[static_cast<std::size_t>(i)].size() > a.voisins[static_cast<std::size_t>(depart)].size())))
          depart = i;
      std::vector<int> file{depart};
      vu[static_cast<std::size_t>(depart)] = true;
      for (std::size_t t = 0; t < file.size(); ++t) {
        m_ordre.push_back(file[t]);
        for (const auto& [v, e] : a.voisins[static_cast<std::size_t>(file[t])])
          if (!vu[static_cast<std::size_t>(v)]) vu[static_cast<std::size_t>(v)] = true, file.push_back(v);
      }
    }
  }
  bool chercher() {
    if (m_a.etiquettes.size() != m_b.etiquettes.size()) return false;
    return etendre(0);
  }
  const std::vector<int>& correspondance() const { return m_ab; }

 private:
  bool compatibles(int u, int v) const {
    if (m_a.etiquettes[static_cast<std::size_t>(u)] != m_b.etiquettes[static_cast<std::size_t>(v)]) return false;
    const auto& nu = m_a.voisins[static_cast<std::size_t>(u)];
    const auto& nv = m_b.voisins[static_cast<std::size_t>(v)];
    if (nu.size() != nv.size()) return false;
    // Voisins déjà placés : mêmes arêtes, mêmes étiquettes, dans les deux sens
    int places = 0;
    for (const auto& [w, e] : nu) {
      const int w2 = m_ab[static_cast<std::size_t>(w)];
      if (w2 < 0) continue;
      ++places;
      const auto it = nv.find(w2);
      if (it == nv.end() || it->second != e) return false;
    }
    int placesB = 0;
    for (const auto& [w, e] : nv) placesB += m_ba[static_cast<std::size_t>(w)] >= 0;
    if (places != placesB) return false;
    // Anticipation : les voisins encore libres, comptés par étiquette, se correspondent
    std::map<std::string, int> libres;
    for (const auto& [w, e] : nu)
      if (m_ab[static_cast<std::size_t>(w)] < 0) ++libres[m_a.etiquettes[static_cast<std::size_t>(w)]];
    for (const auto& [w, e] : nv)
      if (m_ba[static_cast<std::size_t>(w)] < 0) --libres[m_b.etiquettes[static_cast<std::size_t>(w)]];
    for (const auto& [l, n] : libres)
      if (n != 0) return false;
    return true;
  }
  bool etendre(std::size_t profondeur) {
    if (profondeur == m_ordre.size()) return true;
    if (++m_pas > 2'000'000) return false;  // garde-fou
    const int u = m_ordre[profondeur];
    // Candidats : voisins des images des voisins placés, sinon tous les sommets libres
    std::vector<int> candidats;
    for (const auto& [w, e] : m_a.voisins[static_cast<std::size_t>(u)])
      if (const int w2 = m_ab[static_cast<std::size_t>(w)]; w2 >= 0) {
        for (const auto& [v, e2] : m_b.voisins[static_cast<std::size_t>(w2)])
          if (m_ba[static_cast<std::size_t>(v)] < 0) candidats.push_back(v);
        break;
      }
    if (candidats.empty())
      for (int v = 0; v < static_cast<int>(m_b.etiquettes.size()); ++v)
        if (m_ba[static_cast<std::size_t>(v)] < 0) candidats.push_back(v);
    for (int v : candidats) {
      if (m_ba[static_cast<std::size_t>(v)] >= 0 || !compatibles(u, v)) continue;
      m_ab[static_cast<std::size_t>(u)] = v;
      m_ba[static_cast<std::size_t>(v)] = u;
      if (etendre(profondeur + 1)) return true;
      m_ab[static_cast<std::size_t>(u)] = -1;
      m_ba[static_cast<std::size_t>(v)] = -1;
    }
    return false;
  }
  const Graphe& m_a;
  const Graphe& m_b;
  std::vector<int> m_ab, m_ba, m_ordre;
  long m_pas = 0;
};
std::string nomGenre(const std::string& symbole) {
  static const std::map<std::string, std::string> n{{"R", "résistance"}, {"C", "condensateur"}, {"C_Pol", "condensateur polarisé"}, {"L", "inductance"}, {"LED", "LED"}, {"D", "diode"},
                                                   {"Q_NPN", "transistor NPN"}, {"Q_PNP", "transistor PNP"}, {"Pile", "pile"}, {"Interrupteur", "interrupteur"}};
  const auto it = n.find(symbole);
  return it == n.end() ? symbole : it->second;
}
}  // namespace

ResultatEvaluation evaluerMontage(const ConceptionElec& eleve, const ConceptionElec& reference) {
  ResultatEvaluation r;
  const Graphe a = graphe(eleve), b = graphe(reference);
  Vf2 vf2(a, b);
  if (vf2.chercher()) {
    r.juste = true;
    for (int i = 0; i < a.composants; ++i) r.correspondance[a.noms[static_cast<std::size_t>(i)]] = b.noms[static_cast<std::size_t>(vf2.correspondance()[static_cast<std::size_t>(i)])];
    return r;
  }
  // Écarts : composants de trop ou manquants (même symbole et même valeur), puis liaisons
  std::map<std::string, std::vector<std::string>> ea, eb;
  for (int i = 0; i < a.composants; ++i) ea[a.etiquettes[static_cast<std::size_t>(i)]].push_back(a.noms[static_cast<std::size_t>(i)]);
  for (int i = 0; i < b.composants; ++i) eb[b.etiquettes[static_cast<std::size_t>(i)]].push_back(b.noms[static_cast<std::size_t>(i)]);
  auto decrire = [](const std::string& etiquette) {
    const auto p = etiquette.find(':', 2);
    const std::string symbole = etiquette.substr(2, p - 2), valeur = p == std::string::npos ? std::string() : etiquette.substr(p + 1);
    return std::pair{nomGenre(symbole), valeur};
  };
  std::set<std::string> toutes;
  for (const auto& [e, v] : ea) toutes.insert(e);
  for (const auto& [e, v] : eb) toutes.insert(e);
  bool memesComposants = true;
  for (const auto& e : toutes) {
    const int na = static_cast<int>(ea[e].size()), nb = static_cast<int>(eb[e].size());
    if (na == nb) continue;
    memesComposants = false;
    const auto [genre, valeur] = decrire(e);
    if (na > nb) {
      std::string refs;
      for (std::size_t i = static_cast<std::size_t>(nb); i < ea[e].size(); ++i) refs += (refs.empty() ? "" : ", ") + ea[e][i];
      r.ecarts.push_back(std::to_string(na - nb) + " " + genre + (na - nb > 1 ? "s" : "") + " de trop (" + refs + (valeur.empty() ? "" : ", " + valeur) + ")");
    } else
      r.ecarts.push_back("il manque " + std::to_string(nb - na) + " " + genre + (nb - na > 1 ? "s" : "") + (valeur.empty() ? "" : " de " + valeur));
  }
  if (memesComposants) r.ecarts.push_back("les composants sont les bons, mais leurs liaisons diffèrent de celles du montage de référence");
  return r;
}

bool formulesEquivalentes(const std::string& eleve, const std::string& reference, int points, double tolerance) {
  std::set<std::string> noms;
  for (const auto& n : nomsDeFormule(eleve)) noms.insert(n);
  for (const auto& n : nomsDeFormule(reference)) noms.insert(n);
  std::mt19937 alea(20261021);
  std::uniform_real_distribution<double> tirage(0.5, 2.0);
  for (int k = 0; k < std::max(1, points); ++k) {
    std::map<std::string, double> v;
    for (const auto& n : noms) v[n] = tirage(alea);
    const ContexteFormule ctx = [&](const std::string& n) -> std::optional<Quantite> {
      const auto it = v.find(n);
      if (it == v.end()) return std::nullopt;
      return Quantite::nombre(it->second);
    };
    const ResultatFormule a = evaluerFormule(eleve, ctx), b = evaluerFormule(reference, ctx);
    if (!a.ok || !b.ok || !a.q.scalaire() || !b.q.scalaire()) return false;
    const std::complex<double> x = a.q.v.front(), y = b.q.v.front();
    if (std::abs(x - y) > tolerance * std::max(1.0, std::abs(y))) return false;
  }
  return true;
}

bool nombreJuste(const std::string& eleve, double attendu, double tolerance) {
  const auto x = nombreDe(eleve);
  return x && std::abs(*x - attendu) <= tolerance * std::abs(attendu) + 1e-15;
}

// ======================================================================================================================
// Classe et diffusion (16.14)
// ======================================================================================================================
namespace {
fs::path dossierDevoir(const std::string& classe, const std::string& id) { return cheminFichier(classe) / "devoirs" / cheminFichier(id); }
Json devoirEnJson(const Devoir& d) {
  return {{"format", "promethee-devoir"}, {"id", d.id}, {"titre", d.titre}, {"consigne", d.consigne}, {"echeance", d.echeance}, {"interdits", d.interdits}, {"fichierProjet", d.fichierProjet}};
}
std::string nomSur(const std::string& s) {
  std::string r;
  for (char c : s) r += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || (static_cast<unsigned char>(c) >= 0x80)) ? c : '_';
  return r.empty() ? std::string("sans_nom") : r;
}
}  // namespace

bool publierDevoir(const std::string& dossierClasse, const Devoir& d, const std::string& projet, std::string* erreur) {
  try {
    const fs::path p = dossierDevoir(dossierClasse, nomSur(d.id));
    fs::create_directories(p / "rendus");
    Devoir x = d;
    if (x.fichierProjet.empty()) x.fichierProjet = "depart.promethee";
    ecrireFichierAtomique(p / cheminFichier(x.fichierProjet), projet);
    ecrireFichierAtomique(p / "devoir.json", devoirEnJson(x).dump(2) + "\n");
    return true;
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return false;
  }
}

std::vector<Devoir> devoirsPublies(const std::string& dossierClasse) {
  std::vector<Devoir> r;
  std::error_code ec;
  const fs::path racine = cheminFichier(dossierClasse) / "devoirs";
  if (!fs::is_directory(racine, ec)) return r;
  for (const auto& e : fs::directory_iterator(racine, ec)) {
    try {
      const Json j = Json::parse(lireFichier(e.path() / "devoir.json"));
      Devoir d;
      d.id = j.value("id", std::string());
      d.titre = j.value("titre", std::string());
      d.consigne = j.value("consigne", std::string());
      d.echeance = j.value("echeance", std::string());
      d.interdits = chaines(j, "interdits");
      d.fichierProjet = j.value("fichierProjet", std::string());
      r.push_back(d);
    } catch (...) {
    }
  }
  std::sort(r.begin(), r.end(), [](const Devoir& a, const Devoir& b) { return a.id < b.id; });
  return r;
}

bool deposerRendu(const std::string& dossierClasse, const std::string& devoir, const std::string& eleve, const std::string& projet, const std::string& quand, std::string* erreur) {
  try {
    const fs::path p = dossierDevoir(dossierClasse, nomSur(devoir));
    if (!fs::exists(p / "devoir.json")) throw std::runtime_error("devoir « " + devoir + " » introuvable dans le dossier de la classe");
    const Json d = Json::parse(lireFichier(p / "devoir.json"));
    const std::string echeance = d.value("echeance", std::string());
    fs::create_directories(p / "rendus");
    ecrireFichierAtomique(p / "rendus" / cheminFichier(nomSur(eleve) + ".promethee"), projet);
    Json r = {{"eleve", eleve}, {"depose", quand}, {"enRetard", !echeance.empty() && quand > echeance}, {"note", -1}, {"remarques", ""}};
    ecrireFichierAtomique(p / "rendus" / cheminFichier(nomSur(eleve) + ".json"), r.dump(2) + "\n");
    return true;
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return false;
  }
}

std::vector<Rendu> releverRendus(const std::string& dossierClasse, const std::string& devoir) {
  std::vector<Rendu> r;
  std::error_code ec;
  const fs::path p = dossierDevoir(dossierClasse, nomSur(devoir)) / "rendus";
  if (!fs::is_directory(p, ec)) return r;
  for (const auto& e : fs::directory_iterator(p, ec)) {
    if (e.path().extension() != ".json") continue;
    try {
      const Json j = Json::parse(lireFichier(e.path()));
      Rendu x;
      x.eleve = j.value("eleve", std::string());
      x.fichier = utf8(fs::path(e.path()).replace_extension(".promethee"));
      x.depose = j.value("depose", std::string());
      x.enRetard = j.value("enRetard", false);
      x.note = j.value("note", -1.0);
      x.remarques = j.value("remarques", std::string());
      r.push_back(x);
    } catch (...) {
    }
  }
  std::sort(r.begin(), r.end(), [](const Rendu& a, const Rendu& b) { return a.eleve < b.eleve; });
  return r;
}

bool noter(const std::string& dossierClasse, const std::string& devoir, const std::string& eleve, double note, const std::string& remarques) {
  try {
    const fs::path f = dossierDevoir(dossierClasse, nomSur(devoir)) / "rendus" / cheminFichier(nomSur(eleve) + ".json");
    Json j = Json::parse(lireFichier(f));
    j["note"] = note;
    j["remarques"] = remarques;
    ecrireFichierAtomique(f, j.dump(2) + "\n");
    return true;
  } catch (...) {
    return false;
  }
}

std::string notesCsv(const std::vector<Rendu>& rendus, const std::string& devoir) {
  // Carnet de notes de l'ENT : point-virgule, virgule décimale, une ligne par élève
  std::ostringstream o;
  o << "Élève;Devoir;Note;Remarques;Déposé;En retard\n";
  auto champ = [](const std::string& s) {
    if (s.find_first_of(";\"\n") == std::string::npos) return s;
    std::string r = "\"";
    for (char c : s) r += c == '"' ? std::string("\"\"") : std::string(1, c);
    return r + "\"";
  };
  for (const auto& r : rendus) {
    std::string note;
    if (r.note >= 0) {
      note = nombre(r.note);
      std::replace(note.begin(), note.end(), '.', ',');
    }
    o << champ(r.eleve) << ";" << champ(devoir) << ";" << note << ";" << champ(r.remarques) << ";" << r.depose << ";" << (r.enRetard ? "oui" : "non") << "\n";
  }
  return o.str();
}

std::string paquetCommonCartridge(const Devoir& d, const std::string& projet) {
  const std::string id = "promethee_" + nomSur(d.id);
  std::ostringstream m;
  m << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    << "<manifest identifier=\"" << id << "\" xmlns=\"http://www.imsglobal.org/xsd/imsccv1p1/imscp_v1p1\" xmlns:lomimscc=\"http://ltsc.ieee.org/xsd/imsccv1p1/LOM/manifest\">\n"
    << "  <metadata><schema>IMS Common Cartridge</schema><schemaversion>1.1.0</schemaversion>\n"
    << "    <lomimscc:lom><lomimscc:general><lomimscc:title><lomimscc:string>" << echapperXmlTexte(d.titre) << "</lomimscc:string></lomimscc:title></lomimscc:general></lomimscc:lom>\n"
    << "  </metadata>\n  <organizations>\n    <organization identifier=\"org\" structure=\"rooted-hierarchy\">\n      <item identifier=\"racine\">\n"
    << "        <item identifier=\"devoir\" identifierref=\"res_devoir\"><title>" << echapperXmlTexte(d.titre) << "</title></item>\n"
    << "      </item>\n    </organization>\n  </organizations>\n  <resources>\n"
    << "    <resource identifier=\"res_devoir\" type=\"webcontent\" href=\"devoir/consigne.html\">\n      <file href=\"devoir/consigne.html\"/>\n      <file href=\"devoir/depart.promethee\"/>\n"
    << "    </resource>\n  </resources>\n</manifest>\n";
  std::ostringstream h;
  h << "<!doctype html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\"><title>" << echapperXmlTexte(d.titre) << "</title></head><body>\n<h1>" << echapperXmlTexte(d.titre) << "</h1>\n<p>"
    << echapperXmlTexte(d.consigne) << "</p>\n";
  if (!d.echeance.empty()) h << "<p>À rendre avant le " << echapperXmlTexte(d.echeance) << ".</p>\n";
  if (!d.interdits.empty()) {
    h << "<p>Pendant ce devoir, Prométhée masque :</p><ul>\n";
    for (const auto& x : d.interdits) h << "<li>" << echapperXmlTexte(x) << "</li>\n";
    h << "</ul>\n";
  }
  h << "<p>Ouvre <a href=\"depart.promethee\">le projet de départ</a> dans Prométhée.</p>\n</body></html>\n";
  return zipStocke({{"imsmanifest.xml", m.str()}, {"devoir/consigne.html", h.str()}, {"devoir/depart.promethee", projet}});
}

std::vector<ElementLineaire> circuitLineaire(const ConceptionElec& c, std::string* erreur) {
  std::vector<ElementLineaire> r;
  const Connectivite k = connectivite(c);
  std::string refus;
  auto noeud = [&](const std::string& composant, const std::string& broche) -> std::string {
    const int i = k.reseau(composant, broche);
    if (i < 0) return "nc_" + composant.substr(0, 8) + "_" + broche;
    std::string n = k.reseaux[static_cast<std::size_t>(i)].nom;
    std::string m;
    for (char ch : n) m += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return m == "GND" || m == "0" || m == "MASSE" || m == "VSS" ? std::string("0") : n;
  };
  for (const ComposantElec* x : composantsElectriques(c)) {
    ElementLineaire e;
    e.nom = x->reference;
    const FamilleValeur f = familleDe(c, *x);
    const ValeurComprise v = lireValeurComposant(x->valeur, f);
    const std::string& s = x->symbole;
    if (s == "R" || s == "C" || s == "C_Pol" || s == "L") {
      e.genre = s == "R" ? 'R' : s == "L" ? 'L' : 'C';
      e.a = noeud(x->uuid, "1");
      e.b = noeud(x->uuid, "2");
      e.valeur = v.comprise() ? v.nombre : 0;
      if (e.valeur <= 0) {
        refus += (refus.empty() ? "" : ", ") + x->reference + " (valeur « " + x->valeur + " »)";
        continue;
      }
    } else if (s == "Source_tension" || s == "Pile") {
      e.genre = 'V';
      e.a = noeud(x->uuid, "1");
      e.b = noeud(x->uuid, "2");
      const std::string m = minuscules(x->valeur);
      const auto ac = m.find("ac");
      e.ac = ac == std::string::npos ? 0 : nombreDe(x->valeur.substr(ac + 2)).value_or(1);
      if (s == "Source_tension" && ac == std::string::npos) e.ac = 1;
      e.valeur = 0;
    } else {
      refus += (refus.empty() ? "" : ", ") + x->reference + " (" + s + ")";
      continue;
    }
    r.push_back(e);
  }
  if (erreur) *erreur = refus.empty() ? std::string() : "éléments non simulés dans la page : " + refus;
  return r;
}

namespace {
// Feuille du schéma en SVG (mm) : traits des symboles, broches, fils, jonctions, étiquettes, repères et valeurs
std::string svgSchemaFeuille(const ConceptionElec& c, std::size_t feuille) {
  std::ostringstream o;
  if (feuille >= c.feuilles.size()) return {};
  const Feuille& f = c.feuilles[feuille];
  double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
  auto etendre = [&](Pt p) {
    x0 = std::min(x0, p.x * 1e-6), y0 = std::min(y0, p.y * 1e-6), x1 = std::max(x1, p.x * 1e-6), y1 = std::max(y1, p.y * 1e-6);
  };
  std::ostringstream corps;
  auto P = [](Pt p) { return nombre(p.x * 1e-6) + "," + nombre(p.y * 1e-6); };
  auto ligne = [&](Pt a, Pt b, const char* couleur, double ep) {
    corps << "<line x1=\"" << nombre(a.x * 1e-6) << "\" y1=\"" << nombre(a.y * 1e-6) << "\" x2=\"" << nombre(b.x * 1e-6) << "\" y2=\"" << nombre(b.y * 1e-6) << "\" stroke=\"" << couleur
          << "\" stroke-width=\"" << ep << "\"/>\n";
    etendre(a), etendre(b);
  };
  auto texte = [&](Pt p, const std::string& t, double taille, const char* couleur) {
    corps << "<text x=\"" << nombre(p.x * 1e-6) << "\" y=\"" << nombre(p.y * 1e-6) << "\" font-size=\"" << taille << "\" fill=\"" << couleur << "\">" << echapperXmlTexte(t) << "</text>\n";
    etendre(p);
  };
  for (const auto& s : f.symboles) {
    const DefSymbole* d = c.symbole(s.symbole);
    if (!d) continue;
    const ComposantElec* x = s.composant.empty() ? nullptr : c.composant(s.composant);
    corps << "<g class=\"symbole\"" << (x ? " data-ref=\"" + echapperXmlTexte(x->reference) + "\"" : std::string()) << ">\n";
    for (const auto& t : d->traces) {
      if (t.unite != 0 && t.unite != s.unite) continue;
      if (t.forme == Trace::Forme::Texte) {
        if (!t.points.empty()) texte(transformer(s, t.points[0]), t.texte, std::max(1.0, t.taille * 1e-6), "#8B0000");
        continue;
      }
      std::vector<Pt> pts;
      if (t.forme == Trace::Forme::Rectangle && t.points.size() >= 2)
        pts = {t.points[0], {t.points[1].x, t.points[0].y}, t.points[1], {t.points[0].x, t.points[1].y}, t.points[0]};
      else if (t.forme == Trace::Forme::Polyligne)
        pts = t.points;
      else
        pts = contourTrace(t);
      if (pts.size() < 2) continue;
      corps << "<polyline points=\"";
      for (const auto& p : pts) {
        const Pt q = transformer(s, p);
        corps << P(q) << " ";
        etendre(q);
      }
      if ((t.ferme || t.forme == Trace::Forme::Cercle || t.forme == Trace::Forme::Ellipse) && t.forme != Trace::Forme::Rectangle) corps << P(transformer(s, pts.front()));
      corps << "\" fill=\"" << (t.rempli ? "#8B0000" : t.fond ? "#FFFFC0" : "none") << "\" stroke=\"#8B0000\" stroke-width=\"0.254\"/>\n";
    }
    for (const auto& b : d->broches) {
      if (b.unite != 0 && b.unite != s.unite) continue;
      const Pt dir = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
      ligne(transformer(s, b.extremite), transformer(s, {b.extremite.x + dir.x * b.longueur, b.extremite.y + dir.y * b.longueur}), "#8B0000", 0.254);
    }
    if (x) {
      texte(transformer(s, d->posReference), x->reference, 1.5, "#006060");
      if (!x->valeur.empty()) texte(transformer(s, d->posValeur), x->valeur, 1.5, "#006060");
    } else if (d->alimentation)
      texte(transformer(s, d->posValeur), s.nomAlim.empty() ? d->nom : s.nomAlim, 1.5, "#8B0000");
    corps << "</g>\n";
  }
  for (const auto& w : f.fils) ligne(w.a, w.b, "#006400", 0.254);
  for (const auto& j : f.jonctions) {
    corps << "<circle cx=\"" << nombre(j.p.x * 1e-6) << "\" cy=\"" << nombre(j.p.y * 1e-6) << "\" r=\"0.5\" fill=\"#006400\"/>\n";
    etendre(j.p);
  }
  for (const auto& e : f.etiquettes) texte(e.p, e.texte, 1.6, "#00008B");
  if (x0 > x1) x0 = y0 = 0, x1 = y1 = 10;
  const double m = 5;
  o << "<svg id=\"schema\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << nombre(x0 - m) << " " << nombre(y0 - m) << " " << nombre(x1 - x0 + 2 * m) << " " << nombre(y1 - y0 + 2 * m)
    << "\" font-family=\"sans-serif\">\n<rect x=\"" << nombre(x0 - m) << "\" y=\"" << nombre(y0 - m) << "\" width=\"" << nombre(x1 - x0 + 2 * m) << "\" height=\"" << nombre(y1 - y0 + 2 * m)
    << "\" fill=\"#FFFFF8\"/>\n"
    << corps.str() << "</svg>\n";
  return o.str();
}

// Moteur de la page : analyse nodale modifiée en complexes, élimination de Gauss à pivot partiel
const char* kMoteurJs = R"JS(
"use strict";
function cplx(re, im) { return [re, im || 0]; }
function cAdd(a, b) { return [a[0] + b[0], a[1] + b[1]]; }
function cSub(a, b) { return [a[0] - b[0], a[1] - b[1]]; }
function cMul(a, b) { return [a[0] * b[0] - a[1] * b[1], a[0] * b[1] + a[1] * b[0]]; }
function cDiv(a, b) { const d = b[0] * b[0] + b[1] * b[1]; return [(a[0] * b[0] + a[1] * b[1]) / d, (a[1] * b[0] - a[0] * b[1]) / d]; }
function cAbs(a) { return Math.hypot(a[0], a[1]); }
function resoudre(A, b) {
  const n = b.length;
  for (let k = 0; k < n; k++) {
    let p = k, m = cAbs(A[k][k]);
    for (let i = k + 1; i < n; i++) { const v = cAbs(A[i][k]); if (v > m) { m = v; p = i; } }
    if (m < 1e-300) throw new Error("circuit singulier (nœud flottant ?)");
    [A[k], A[p]] = [A[p], A[k]]; [b[k], b[p]] = [b[p], b[k]];
    for (let i = k + 1; i < n; i++) {
      const f = cDiv(A[i][k], A[k][k]);
      if (f[0] === 0 && f[1] === 0) continue;
      for (let j = k; j < n; j++) A[i][j] = cSub(A[i][j], cMul(f, A[k][j]));
      b[i] = cSub(b[i], cMul(f, b[k]));
    }
  }
  const x = new Array(n);
  for (let i = n - 1; i >= 0; i--) {
    let s = b[i];
    for (let j = i + 1; j < n; j++) s = cSub(s, cMul(A[i][j], x[j]));
    x[i] = cDiv(s, A[i][i]);
  }
  return x;
}
function simulerCa(elements, sortie, fDebut, fFin, parDecade) {
  const noeuds = new Map();
  for (const e of elements) for (const n of [e.a, e.b]) if (n !== "0" && !noeuds.has(n)) noeuds.set(n, noeuds.size);
  const sources = elements.filter(e => e.genre === "V");
  const N = noeuds.size, M = sources.length, T = N + M;
  const res = { frequences: [], gainDb: [], phase: [] };
  const decades = Math.log10(fFin / fDebut), pas = Math.max(1, Math.round(decades * parDecade));
  for (let k = 0; k <= pas; k++) {
    const f = fDebut * Math.pow(10, decades * k / pas), w = 2 * Math.PI * f;
    const A = Array.from({ length: T }, () => Array.from({ length: T }, () => cplx(0, 0)));
    const b = Array.from({ length: T }, () => cplx(0, 0));
    const ajouter = (i, j, y) => { if (i >= 0 && j >= 0) A[i][j] = cAdd(A[i][j], y); };
    const indice = n => (n === "0" ? -1 : noeuds.get(n));
    let s = 0;
    for (const e of elements) {
      const i = indice(e.a), j = indice(e.b);
      if (e.genre === "V") {
        const r = N + s++;
        if (i >= 0) { A[i][r] = cAdd(A[i][r], cplx(1)); A[r][i] = cAdd(A[r][i], cplx(1)); }
        if (j >= 0) { A[j][r] = cSub(A[j][r], cplx(1)); A[r][j] = cSub(A[r][j], cplx(1)); }
        b[r] = cplx(e.ac, 0);
        continue;
      }
      const y = e.genre === "R" ? cplx(1 / e.valeur, 0) : e.genre === "C" ? cplx(0, w * e.valeur) : cDiv(cplx(1), cplx(0, Math.max(w, 1e-9) * e.valeur));
      ajouter(i, i, y); ajouter(j, j, y); ajouter(i, j, cSub(cplx(0), y)); ajouter(j, i, cSub(cplx(0), y));
    }
    const x = resoudre(A, b);
    const vs = indice(sortie) >= 0 ? x[indice(sortie)] : cplx(0);
    const amplitude = sources.reduce((m, e) => Math.max(m, Math.abs(e.ac)), 0) || 1;
    res.frequences.push(f);
    res.gainDb.push(20 * Math.log10(Math.max(cAbs(vs) / amplitude, 1e-30)));
    res.phase.push(Math.atan2(vs[1], vs[0]) * 180 / Math.PI);
  }
  return res;
}
function frequenceCoupure(f, g) {
  // Point à mi-puissance : 10 log10(2) ≈ 3,01 dB sous le gain maximal
  const ref = Math.max(...g), seuil = ref - 10 * Math.log10(2);
  for (let i = 1; i < g.length; i++) {
    if (g[i - 1] >= seuil && g[i] < seuil) {
      const t = (seuil - g[i - 1]) / (g[i] - g[i - 1]);
      return Math.pow(10, Math.log10(f[i - 1]) + t * (Math.log10(f[i]) - Math.log10(f[i - 1])));
    }
  }
  return NaN;
}
function ecrireFrequence(f) {
  if (!isFinite(f)) return "aucune";
  const u = f >= 1e6 ? [1e6, "MHz"] : f >= 1e3 ? [1e3, "kHz"] : [1, "Hz"];
  return (f / u[0]).toFixed(2).replace(".", ",") + " " + u[1];
}
if (typeof module !== "undefined") module.exports = { simulerCa, frequenceCoupure, ecrireFrequence, resoudre };
)JS";

const char* kPageJs = R"JS(
"use strict";
(function () {
  const donnees = JSON.parse(document.getElementById("circuit").textContent);
  const sortie = document.getElementById("sortieChoisie");
  for (const n of donnees.noeuds) { const o = document.createElement("option"); o.textContent = n; sortie.appendChild(o); }
  sortie.value = donnees.sortie;
  const courbe = document.getElementById("courbe");
  function tracer(r) {
    const W = 640, H = 300, g0 = -60, g1 = 10;
    const lx = f => 50 + (Math.log10(f) - Math.log10(r.frequences[0])) / (Math.log10(r.frequences[r.frequences.length - 1]) - Math.log10(r.frequences[0])) * (W - 70);
    const ly = g => 20 + (g1 - Math.max(g0, Math.min(g1, g))) / (g1 - g0) * (H - 50);
    let s = "";
    for (let g = g0; g <= g1; g += 10) s += `<line x1="50" x2="${W - 20}" y1="${ly(g)}" y2="${ly(g)}" stroke="#ddd"/><text x="8" y="${ly(g) + 4}" font-size="11">${g} dB</text>`;
    for (let d = Math.ceil(Math.log10(r.frequences[0])); d <= Math.log10(r.frequences[r.frequences.length - 1]); d++)
      s += `<line y1="20" y2="${H - 30}" x1="${lx(Math.pow(10, d))}" x2="${lx(Math.pow(10, d))}" stroke="#ddd"/><text y="${H - 12}" x="${lx(Math.pow(10, d)) - 14}" font-size="11">${ecrireFrequence(Math.pow(10, d))}</text>`;
    s += `<polyline fill="none" stroke="#1565C0" stroke-width="2" points="${r.frequences.map((f, i) => lx(f) + "," + ly(r.gainDb[i])).join(" ")}"/>`;
    courbe.innerHTML = s;
  }
  function calculer() {
    const r = simulerCa(donnees.elements, sortie.value, donnees.fDebut, donnees.fFin, 50);
    tracer(r);
    const fc = frequenceCoupure(r.frequences, r.gainDb);
    document.body.dataset.coupure = String(fc);
    document.getElementById("resultat").textContent = "Coupure à −3 dB : " + ecrireFrequence(fc);
  }
  sortie.addEventListener("change", calculer);
  // Schéma navigable : molette pour le zoom, glisser pour se déplacer
  const svg = document.getElementById("schema");
  if (svg) {
    let vb = svg.getAttribute("viewBox").split(" ").map(Number), prise = null;
    const appliquer = () => svg.setAttribute("viewBox", vb.join(" "));
    svg.addEventListener("wheel", e => { e.preventDefault(); const k = e.deltaY > 0 ? 1.15 : 1 / 1.15; const cx = vb[0] + vb[2] / 2, cy = vb[1] + vb[3] / 2; vb[2] *= k; vb[3] *= k; vb[0] = cx - vb[2] / 2; vb[1] = cy - vb[3] / 2; appliquer(); });
    svg.addEventListener("mousedown", e => { prise = [e.clientX, e.clientY, vb[0], vb[1]]; });
    window.addEventListener("mouseup", () => { prise = null; });
    window.addEventListener("mousemove", e => { if (!prise) return; const k = vb[2] / svg.clientWidth; vb[0] = prise[2] - (e.clientX - prise[0]) * k; vb[1] = prise[3] - (e.clientY - prise[1]) * k; appliquer(); });
  }
  try { calculer(); } catch (e) { document.getElementById("resultat").textContent = "Simulation impossible : " + e.message; }
})();
)JS";
}  // namespace

std::string pageHtmlCircuit(const ConceptionElec& c, const std::string& entree, const std::string& sortie) {
  std::string erreur;
  const std::vector<ElementLineaire> el = circuitLineaire(c, &erreur);
  Json elements = Json::array();
  std::set<std::string> noeuds;
  for (const auto& e : el) {
    elements.push_back({{"genre", std::string(1, e.genre)}, {"nom", e.nom}, {"a", e.a}, {"b", e.b}, {"valeur", e.valeur}, {"ac", e.ac}});
    for (const auto& n : {e.a, e.b})
      if (n != "0") noeuds.insert(n);
  }
  // Sortie : celle donnée, sinon « SORTIE » ou « OUT », sinon le dernier nœud qui n'est pas celui d'une source
  std::string s = sortie;
  if (s.empty())
    for (const char* n : {"SORTIE", "OUT", "VOUT", "S"})
      if (noeuds.count(n)) {
        s = n;
        break;
      }
  if (s.empty() && !noeuds.empty()) s = *noeuds.rbegin();
  Json donnees = {{"titre", c.nom}, {"entree", entree}, {"sortie", s}, {"noeuds", Json(std::vector<std::string>(noeuds.begin(), noeuds.end()))}, {"fDebut", 10.0}, {"fFin", 1e6}, {"elements", elements}};
  std::string json = donnees.dump();
  // Un « </ » dans le JSON fermerait la balise script
  for (std::size_t i = json.find("</"); i != std::string::npos; i = json.find("</", i + 2)) json.replace(i, 2, "<\\/");
  std::ostringstream o;
  o << "<!doctype html>\n<html lang=\"fr\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>" << echapperXmlTexte(c.nom)
    << " — Prométhée</title>\n<style>\nbody{font-family:sans-serif;margin:16px;color:#222;background:#fafafa}\n#schema{width:100%;max-width:900px;height:360px;border:1px solid #ccc;background:#fffff8;cursor:grab}\n"
       "#courbe{width:100%;max-width:660px;height:300px;border:1px solid #ccc;background:#fff}\n#resultat{font-size:1.3em;font-weight:bold;margin:12px 0}\n</style>\n</head>\n<body>\n<h1>"
    << echapperXmlTexte(c.nom) << "</h1>\n<p>Page autonome écrite par Prométhée : elle simule le circuit dans le navigateur, sans réseau.</p>\n";
  if (!erreur.empty()) o << "<p><em>" << echapperXmlTexte(erreur) << "</em></p>\n";
  o << svgSchemaFeuille(c, 0) << "<p><label>Sortie observée : <select id=\"sortieChoisie\"></select></label></p>\n<div id=\"resultat\">Calcul…</div>\n"
    << "<svg id=\"courbe\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 640 300\"></svg>\n"
    << "<script id=\"circuit\" type=\"application/json\">" << json << "</script>\n<script id=\"moteur\">" << kMoteurJs << "</script>\n<script id=\"page\">" << kPageJs << "</script>\n</body>\n</html>\n";
  return o.str();
}

}  // namespace prom::ecad

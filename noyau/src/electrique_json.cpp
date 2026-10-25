// Prométhée : enregistrement d'un projet électrique (JSON). Le schéma garde le format de la conception
// électronique (coordonnées en nanomètres entiers) ; s'y ajoutent les réglages, les folios, les conducteurs, les
// câbles, les armoires, les faisceaux, le logement et l'installation calculée. Lecture tolérante. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "electrique_interne.hpp"
#include "promethee/electrique.hpp"
#include "promethee/electrique_grafcet.hpp"

namespace prom::elec {

using J = nlohmann::ordered_json;

namespace {

const J& cle(const J& o, const char* k) {
  static const J rien;
  if (!o.is_object()) return rien;
  const auto it = o.find(k);
  return it == o.end() ? rien : *it;
}
std::string txt(const J& o, const char* k, const std::string& defaut = {}) {
  const J& v = cle(o, k);
  return v.is_string() ? v.get<std::string>() : defaut;
}
double num(const J& o, const char* k, double defaut = 0) {
  const J& v = cle(o, k);
  if (!v.is_number()) return defaut;
  const double x = v.get<double>();
  return std::isfinite(x) && std::abs(x) < 1e12 ? x : defaut;
}
int ent(const J& o, const char* k, int defaut = 0) {
  const double x = num(o, k, defaut);
  return std::abs(x) < 2e9 ? static_cast<int>(std::lround(x)) : defaut;
}
bool vrai(const J& o, const char* k, bool defaut = false) {
  const J& v = cle(o, k);
  return v.is_boolean() ? v.get<bool>() : defaut;
}
const J& liste(const J& o, const char* k) {
  static const J vide = J::array();
  const J& v = cle(o, k);
  return v.is_array() ? v : vide;
}
J pt(ecad::Pt p) { return J::array({p.x, p.y}); }
ecad::Nm coordonnee(double v) {
  if (!std::isfinite(v)) return 0;
  return static_cast<ecad::Nm>(std::llround(std::clamp(v, -1e10, 1e10)));
}
ecad::Pt lirePt(const J& j) {
  if (!j.is_array() || j.size() < 2 || !j[0].is_number() || !j[1].is_number()) return {};
  return {coordonnee(j[0].get<double>()), coordonnee(j[1].get<double>())};
}

const char* codeRegime(RegimeNeutre r) {
  switch (r) {
    case RegimeNeutre::TT: return "TT";
    case RegimeNeutre::TNS: return "TN-S";
    case RegimeNeutre::TNC: return "TN-C";
    case RegimeNeutre::IT: return "IT";
  }
  return "TT";
}
RegimeNeutre regimeDepuis(const std::string& s) {
  if (s == "TN-S") return RegimeNeutre::TNS;
  if (s == "TN-C") return RegimeNeutre::TNC;
  if (s == "IT") return RegimeNeutre::IT;
  return RegimeNeutre::TT;
}

const char* codePiece(Piece p) {
  switch (p) {
    case Piece::Sejour: return "sejour";
    case Piece::Chambre: return "chambre";
    case Piece::Cuisine: return "cuisine";
    case Piece::SalleDeBain: return "salleDeBain";
    case Piece::WC: return "wc";
    case Piece::Entree: return "entree";
    case Piece::Couloir: return "couloir";
    case Piece::Garage: return "garage";
    case Piece::Buanderie: return "buanderie";
    case Piece::Bureau: return "bureau";
    case Piece::Exterieur: return "exterieur";
    case Piece::Autre: break;
  }
  return "autre";
}
Piece pieceDepuis(const std::string& s) {
  for (Piece p : {Piece::Sejour, Piece::Chambre, Piece::Cuisine, Piece::SalleDeBain, Piece::WC, Piece::Entree, Piece::Couloir, Piece::Garage, Piece::Buanderie, Piece::Bureau,
                  Piece::Exterieur})
    if (s == codePiece(p)) return p;
  return Piece::Autre;
}

J logementVersJson(const Logement& l) {
  J j;
  j["nom"] = l.nom;
  j["surface"] = l.surface;
  j["triphase"] = l.triphase;
  j["abonnementKVA"] = l.abonnementKVA;
  J pieces = J::array();
  for (const auto& p : l.pieces)
    pieces.push_back({{"type", codePiece(p.type)},
                      {"nom", p.nom},
                      {"surface", p.surface},
                      {"prises", p.prises},
                      {"pointsLumineux", p.pointsLumineux},
                      {"voletRoulant", p.voletRoulant},
                      {"chauffage", p.chauffage}});
  j["pieces"] = pieces;
  j["equipements"] = {{"plaqueCuisson", l.plaqueCuisson}, {"four", l.four},           {"laveVaisselle", l.laveVaisselle}, {"laveLinge", l.laveLinge},
                      {"secheLinge", l.secheLinge},       {"congelateur", l.congelateur}, {"chauffeEau", l.chauffeEau},      {"vmc", l.vmc},
                      {"borneRecharge", l.borneRecharge}, {"climatisation", l.climatisation}};
  j["puissanceChauffeEau"] = l.puissanceChauffeEau;
  j["puissanceBorne"] = l.puissanceBorne;
  j["puissanceClim"] = l.puissanceClim;
  return j;
}

Logement logementDepuisJson(const J& j) {
  Logement l;
  l.nom = txt(j, "nom", l.nom);
  l.surface = std::max(1.0, num(j, "surface", l.surface));
  l.triphase = vrai(j, "triphase");
  l.abonnementKVA = num(j, "abonnementKVA", l.abonnementKVA);
  for (const auto& x : liste(j, "pieces")) {
    if (!x.is_object()) continue;
    PieceLogement p;
    p.type = pieceDepuis(txt(x, "type"));
    p.nom = txt(x, "nom");
    p.surface = std::max(0.0, num(x, "surface", p.surface));
    p.prises = std::max(0, ent(x, "prises"));
    p.pointsLumineux = std::max(0, ent(x, "pointsLumineux", 1));
    p.voletRoulant = vrai(x, "voletRoulant");
    p.chauffage = std::max(0.0, num(x, "chauffage"));
    l.pieces.push_back(p);
  }
  const J& e = cle(j, "equipements");
  l.plaqueCuisson = vrai(e, "plaqueCuisson", l.plaqueCuisson);
  l.four = vrai(e, "four", l.four);
  l.laveVaisselle = vrai(e, "laveVaisselle", l.laveVaisselle);
  l.laveLinge = vrai(e, "laveLinge", l.laveLinge);
  l.secheLinge = vrai(e, "secheLinge", l.secheLinge);
  l.congelateur = vrai(e, "congelateur", l.congelateur);
  l.chauffeEau = vrai(e, "chauffeEau", l.chauffeEau);
  l.vmc = vrai(e, "vmc", l.vmc);
  l.borneRecharge = vrai(e, "borneRecharge", l.borneRecharge);
  l.climatisation = vrai(e, "climatisation", l.climatisation);
  l.puissanceChauffeEau = num(j, "puissanceChauffeEau", l.puissanceChauffeEau);
  l.puissanceBorne = num(j, "puissanceBorne", l.puissanceBorne);
  l.puissanceClim = num(j, "puissanceClim", l.puissanceClim);
  return l;
}

// ---------- Distribution ----------
template <typename E>
struct Codes {
  std::vector<std::pair<E, const char*>> v;
  const char* code(E e) const {
    for (const auto& [k, c] : v)
      if (k == e) return c;
    return v.front().second;
  }
  E depuis(const std::string& s, E defaut) const {
    for (const auto& [k, c] : v)
      if (s == c) return k;
    return defaut;
  }
};
const Codes<TypeSourceBT> CODES_SOURCE{{{TypeSourceBT::BranchementLimite, "branchementLimite"},
                                        {TypeSourceBT::ReseauPublic, "reseauPublic"},
                                        {TypeSourceBT::Transformateur, "transformateur"},
                                        {TypeSourceBT::Groupe, "groupe"}}};
const Codes<TypeCharge> CODES_CHARGE{{{TypeCharge::Tableau, "tableau"},
                                      {TypeCharge::Eclairage, "eclairage"},
                                      {TypeCharge::Prises, "prises"},
                                      {TypeCharge::Chauffage, "chauffage"},
                                      {TypeCharge::Moteur, "moteur"},
                                      {TypeCharge::IRVE, "irve"},
                                      {TypeCharge::Condensateurs, "condensateurs"},
                                      {TypeCharge::Autre, "autre"}}};
const Codes<Polarite> CODES_POLARITE{{{Polarite::TriphaseNeutre, "3PN"}, {Polarite::Triphase, "3P"}, {Polarite::Monophase, "1PN"}}};
const Codes<TypeProtection> CODES_PROTECTION{{{TypeProtection::Auto, "auto"},
                                              {TypeProtection::Modulaire, "modulaire"},
                                              {TypeProtection::Reglable, "reglable"},
                                              {TypeProtection::Fusible, "fusible"},
                                              {TypeProtection::Moteur, "moteur"}}};
const Codes<MethodePose> CODES_POSE{{{MethodePose::A1, "A1"},
                                     {MethodePose::A2, "A2"},
                                     {MethodePose::B1, "B1"},
                                     {MethodePose::B2, "B2"},
                                     {MethodePose::C, "C"},
                                     {MethodePose::E, "E"},
                                     {MethodePose::D1, "D1"}}};

J reseauVersJson(const ReseauBT& r) {
  const SourceBT& s = r.source;
  J j;
  j["nom"] = r.nom;
  j["source"] = {{"type", CODES_SOURCE.code(s.type)}, {"puissance", s.puissance}, {"ucc", s.ucc},         {"pertesCuivre", s.pertesCuivre},
                 {"sccAmont", s.sccAmont},             {"icc", s.icc},             {"triphase", s.triphase}, {"xdSub", s.xdSub},
                 {"xdTrans", s.xdTrans},               {"x0", s.x0},               {"tension", s.tension},   {"regime", codeRegime(s.regime)},
                 {"resistanceTerre", s.resistanceTerre}, {"controleurIsolement", s.controleurIsolement}, {"domestique", s.domestique}};
  j["cosPhiCible"] = r.cosPhiCible;
  j["selectivite"] = r.selectivite;
  j["chuteEclairage"] = r.chuteEclairage;
  j["chuteAutres"] = r.chuteAutres;
  J departs = J::array();
  for (const auto& d : r.departs)
    departs.push_back({{"uuid", d.uuid},
                       {"parent", d.parent},
                       {"repere", d.repere},
                       {"nom", d.nom},
                       {"charge", CODES_CHARGE.code(d.charge)},
                       {"polarite", CODES_POLARITE.code(d.polarite)},
                       {"phase", d.phase},
                       {"puissance", d.puissance},
                       {"cosPhi", d.cosPhi},
                       {"rendement", d.rendement},
                       {"ku", d.ku},
                       {"ks", d.ks},
                       {"courantImpose", d.courantImpose},
                       {"tauxH3", d.tauxH3},
                       {"demarrage", d.demarrage},
                       {"longueur", d.longueur},
                       {"ame", d.ame == Ame::Cuivre ? "cuivre" : "aluminium"},
                       {"isolant", d.isolant == Isolant::PVC ? "PVC" : "PR"},
                       {"pose", CODES_POSE.code(d.pose)},
                       {"temperature", d.temperature},
                       {"circuitsJointifs", d.circuitsJointifs},
                       {"unipolaire", d.unipolaire},
                       {"parallele", d.parallele},
                       {"sectionImposee", d.sectionImposee},
                       {"sectionMini", d.sectionMini},
                       {"neutreReduit", d.neutreReduit},
                       {"protection", CODES_PROTECTION.code(d.protection)},
                       {"courbe", d.courbe},
                       {"calibreImpose", d.calibreImpose},
                       {"reglageIr", d.reglageIr},
                       {"isd", d.isd},
                       {"retarde", d.retarde},
                       {"pdcImpose", d.pdcImpose},
                       {"differentiel", d.differentiel},
                       {"typeDifferentiel", d.typeDifferentiel},
                       {"differentielSelectif", d.differentielSelectif}});
  j["departs"] = departs;
  return j;
}

ReseauBT reseauDepuisJson(const J& j) {
  ReseauBT r;
  r.nom = txt(j, "nom", r.nom);
  const J& x = cle(j, "source");
  SourceBT& s = r.source;
  s.type = CODES_SOURCE.depuis(txt(x, "type"), s.type);
  s.puissance = std::clamp(num(x, "puissance", s.puissance), 0.1, 1e5);
  s.ucc = std::clamp(num(x, "ucc", s.ucc), 0.5, 30.0);
  s.pertesCuivre = std::clamp(num(x, "pertesCuivre", s.pertesCuivre), 0.0, 1e6);
  s.sccAmont = std::clamp(num(x, "sccAmont", s.sccAmont), 0.0, 1e5);
  s.icc = std::clamp(num(x, "icc", s.icc), 0.1, 200.0);
  s.triphase = vrai(x, "triphase", s.triphase);
  s.xdSub = std::clamp(num(x, "xdSub", s.xdSub), 1.0, 100.0);
  s.xdTrans = std::clamp(num(x, "xdTrans", s.xdTrans), 1.0, 200.0);
  s.x0 = std::clamp(num(x, "x0", s.x0), 0.0, 100.0);
  s.tension = std::clamp(num(x, "tension", s.tension), 100.0, 1000.0);
  s.regime = regimeDepuis(txt(x, "regime", codeRegime(s.regime)));
  s.resistanceTerre = std::clamp(num(x, "resistanceTerre", s.resistanceTerre), 0.0, 1e4);
  s.controleurIsolement = vrai(x, "controleurIsolement", s.controleurIsolement);
  s.domestique = vrai(x, "domestique", s.domestique);
  r.cosPhiCible = std::clamp(num(j, "cosPhiCible", r.cosPhiCible), 0.5, 1.0);
  r.selectivite = vrai(j, "selectivite", r.selectivite);
  r.chuteEclairage = std::clamp(num(j, "chuteEclairage", 0), 0.0, 50.0);
  r.chuteAutres = std::clamp(num(j, "chuteAutres", 0), 0.0, 50.0);
  for (const auto& y : liste(j, "departs")) {
    if (!y.is_object()) continue;
    DepartBT d;
    d.uuid = txt(y, "uuid");
    if (d.uuid.empty()) d.uuid = ecad::nouvelUuid();
    d.parent = txt(y, "parent");
    d.repere = txt(y, "repere", d.repere);
    d.nom = txt(y, "nom", d.nom);
    d.charge = CODES_CHARGE.depuis(txt(y, "charge"), d.charge);
    d.polarite = CODES_POLARITE.depuis(txt(y, "polarite"), d.polarite);
    d.phase = std::clamp(ent(y, "phase"), 0, 3);
    d.puissance = std::clamp(num(y, "puissance", d.puissance), 0.0, 1e6);
    d.cosPhi = std::clamp(num(y, "cosPhi", d.cosPhi), 0.0, 1.0);
    d.rendement = std::clamp(num(y, "rendement", d.rendement), 0.1, 1.0);
    d.ku = std::clamp(num(y, "ku", d.ku), 0.0, 1.0);
    d.ks = std::clamp(num(y, "ks", d.ks), 0.0, 1.0);
    d.courantImpose = std::clamp(num(y, "courantImpose", 0), 0.0, 1e5);
    d.tauxH3 = std::clamp(num(y, "tauxH3", 0), 0.0, 100.0);
    d.demarrage = std::clamp(num(y, "demarrage", 0), 0.0, 20.0);
    d.longueur = std::clamp(num(y, "longueur", d.longueur), 0.0, 1e5);
    d.ame = txt(y, "ame") == "aluminium" ? Ame::Aluminium : Ame::Cuivre;
    d.isolant = txt(y, "isolant") == "PVC" ? Isolant::PVC : Isolant::PR;
    d.pose = CODES_POSE.depuis(txt(y, "pose"), d.pose);
    d.temperature = std::clamp(num(y, "temperature", d.temperature), -40.0, 80.0);
    d.circuitsJointifs = std::clamp(ent(y, "circuitsJointifs", 1), 1, 100);
    d.unipolaire = vrai(y, "unipolaire");
    d.parallele = std::clamp(ent(y, "parallele", 0), 0, 12);
    d.sectionImposee = std::clamp(num(y, "sectionImposee", 0), 0.0, 1000.0);
    d.sectionMini = std::clamp(num(y, "sectionMini", 0), 0.0, 1000.0);
    d.neutreReduit = vrai(y, "neutreReduit");
    d.protection = CODES_PROTECTION.depuis(txt(y, "protection"), d.protection);
    d.courbe = txt(y, "courbe", d.courbe);
    d.calibreImpose = std::clamp(num(y, "calibreImpose", 0), 0.0, 1e4);
    d.reglageIr = std::clamp(num(y, "reglageIr", 0), 0.0, 1e4);
    d.isd = std::clamp(num(y, "isd", d.isd), 0.0, 20.0);
    d.retarde = vrai(y, "retarde");
    d.pdcImpose = std::clamp(num(y, "pdcImpose", 0), 0.0, 300.0);
    d.differentiel = std::clamp(num(y, "differentiel", 0), 0.0, 1e5);
    d.typeDifferentiel = txt(y, "typeDifferentiel", d.typeDifferentiel);
    d.differentielSelectif = vrai(y, "differentielSelectif");
    r.departs.push_back(d);
  }
  return r;
}

// ---------- Photovoltaïque ----------
const Codes<BatimentPV> CODES_BATIMENT_PV{{{BatimentPV::Habitation, "habitation"}, {BatimentPV::Tertiaire, "tertiaire"}, {BatimentPV::CentraleSol, "centraleSol"}}};

J pvVersJson(const InstallationPV& i) {
  const ModulePV& m = i.module;
  const OnduleurPV& o = i.onduleur;
  J j;
  j["nom"] = i.nom;
  j["module"] = {{"nom", m.nom},       {"puissance", m.puissance},   {"voc", m.voc},           {"vmp", m.vmp},
                 {"isc", m.isc},       {"imp", m.imp},               {"coefVoc", m.coefVoc},   {"coefIsc", m.coefIsc},
                 {"tensionMax", m.tensionMax}, {"courantInverse", m.courantInverse}, {"largeur", m.largeur}, {"hauteur", m.hauteur}};
  j["onduleur"] = {{"nom", o.nom},
                   {"puissanceAC", o.puissanceAC},
                   {"puissanceDCMax", o.puissanceDCMax},
                   {"tensionDCMax", o.tensionDCMax},
                   {"mpptMin", o.mpptMin},
                   {"mpptMax", o.mpptMax},
                   {"courantMppt", o.courantMppt},
                   {"iscMppt", o.iscMppt},
                   {"mppt", o.mppt},
                   {"entreesParMppt", o.entreesParMppt},
                   {"triphase", o.triphase},
                   {"courantACMax", o.courantACMax}};
  j["onduleurs"] = i.onduleurs;
  j["nombreModules"] = i.nombreModules;
  j["modulesParChaine"] = i.modulesParChaine;
  j["tMin"] = i.tMin;
  j["tMax"] = i.tMax;
  j["temperatureCables"] = i.temperatureCables;
  j["longueurDC"] = i.longueurDC;
  j["longueurAC"] = i.longueurAC;
  j["sectionDC"] = i.sectionDC;
  j["sectionAC"] = i.sectionAC;
  j["ng"] = i.ng;
  j["batiment"] = CODES_BATIMENT_PV.code(i.batiment);
  j["paratonnerre"] = i.paratonnerre;
  j["venteTotale"] = i.venteTotale;
  j["productible"] = i.productible;
  j["orientation"] = i.orientation;
  j["inclinaison"] = i.inclinaison;
  return j;
}

InstallationPV pvDepuisJson(const J& j) {
  InstallationPV i;
  i.nom = txt(j, "nom", i.nom);
  const J& a = cle(j, "module");
  ModulePV& m = i.module;
  m.nom = txt(a, "nom", m.nom);
  m.puissance = std::clamp(num(a, "puissance", m.puissance), 1.0, 2000.0);
  m.voc = std::clamp(num(a, "voc", m.voc), 1.0, 1500.0);
  m.vmp = std::clamp(num(a, "vmp", m.vmp), 1.0, 1500.0);
  m.isc = std::clamp(num(a, "isc", m.isc), 0.1, 100.0);
  m.imp = std::clamp(num(a, "imp", m.imp), 0.1, 100.0);
  m.coefVoc = std::clamp(num(a, "coefVoc", m.coefVoc), -2.0, 0.0);
  m.coefIsc = std::clamp(num(a, "coefIsc", m.coefIsc), -1.0, 1.0);
  m.tensionMax = std::clamp(num(a, "tensionMax", m.tensionMax), 50.0, 1500.0);
  m.courantInverse = std::clamp(num(a, "courantInverse", m.courantInverse), 0.1, 200.0);
  m.largeur = std::clamp(num(a, "largeur", m.largeur), 100.0, 5000.0);
  m.hauteur = std::clamp(num(a, "hauteur", m.hauteur), 100.0, 5000.0);
  const J& b = cle(j, "onduleur");
  OnduleurPV& o = i.onduleur;
  o.nom = txt(b, "nom", o.nom);
  o.puissanceAC = std::clamp(num(b, "puissanceAC", o.puissanceAC), 100.0, 5e6);
  o.puissanceDCMax = std::clamp(num(b, "puissanceDCMax", o.puissanceDCMax), 100.0, 1e7);
  o.tensionDCMax = std::clamp(num(b, "tensionDCMax", o.tensionDCMax), 50.0, 1500.0);
  o.mpptMin = std::clamp(num(b, "mpptMin", o.mpptMin), 0.0, 1500.0);
  o.mpptMax = std::clamp(num(b, "mpptMax", o.mpptMax), 0.0, 1500.0);
  o.courantMppt = std::clamp(num(b, "courantMppt", o.courantMppt), 0.1, 1000.0);
  o.iscMppt = std::clamp(num(b, "iscMppt", o.iscMppt), 0.1, 1000.0);
  o.mppt = std::clamp(ent(b, "mppt", o.mppt), 1, 32);
  o.entreesParMppt = std::clamp(ent(b, "entreesParMppt", o.entreesParMppt), 1, 16);
  o.triphase = vrai(b, "triphase", o.triphase);
  o.courantACMax = std::clamp(num(b, "courantACMax", 0), 0.0, 1e4);
  i.onduleurs = std::clamp(ent(j, "onduleurs", i.onduleurs), 1, 100);
  i.nombreModules = std::clamp(ent(j, "nombreModules", i.nombreModules), 0, 100000);
  i.modulesParChaine = std::clamp(ent(j, "modulesParChaine", 0), 0, 100);
  i.tMin = std::clamp(num(j, "tMin", i.tMin), -50.0, 40.0);
  i.tMax = std::clamp(num(j, "tMax", i.tMax), 20.0, 100.0);
  i.temperatureCables = std::clamp(num(j, "temperatureCables", i.temperatureCables), -20.0, 110.0);
  i.longueurDC = std::clamp(num(j, "longueurDC", i.longueurDC), 0.0, 1e4);
  i.longueurAC = std::clamp(num(j, "longueurAC", i.longueurAC), 0.0, 1e4);
  i.sectionDC = std::clamp(num(j, "sectionDC", 0), 0.0, 1000.0);
  i.sectionAC = std::clamp(num(j, "sectionAC", 0), 0.0, 1000.0);
  i.ng = std::clamp(num(j, "ng", i.ng), 0.01, 50.0);
  i.batiment = CODES_BATIMENT_PV.depuis(txt(j, "batiment"), i.batiment);
  i.paratonnerre = vrai(j, "paratonnerre");
  i.venteTotale = vrai(j, "venteTotale");
  i.productible = std::clamp(num(j, "productible", i.productible), 0.0, 3000.0);
  i.orientation = std::clamp(num(j, "orientation", i.orientation), -180.0, 180.0);
  i.inclinaison = std::clamp(num(j, "inclinaison", i.inclinaison), 0.0, 90.0);
  return i;
}

}  // namespace

J versJson(const ProjetElectrique& p) {
  J j;
  j["format"] = "promethee-electrique";
  j["version"] = 1;
  j["uuid"] = p.uuid;
  j["nom"] = p.nom;
  const ReglagesElectriques& r = p.reglages;
  j["reglages"] = {{"norme", r.norme == NormeRepere::Cei81346 ? "cei81346" : "classique"},
                   {"tiret", r.tiret},
                   {"fils", r.fils == NumerotationFils::ParFolio ? "parFolio" : "sequentielle"},
                   {"format", r.format},
                   {"colonnes", r.colonnes},
                   {"lignes", r.lignes},
                   {"tension", r.tension},
                   {"tensionSimple", r.tensionSimple},
                   {"frequence", r.frequence},
                   {"regime", codeRegime(r.regime)},
                   {"tensionCommande", r.tensionCommande},
                   {"commandeContinue", r.commandeContinue},
                   {"societe", r.societe},
                   {"auteur", r.auteur},
                   {"client", r.client},
                   {"installation", r.installation},
                   {"emplacement", r.emplacement}};
  J folios = J::object();
  for (const auto& [id, f] : p.folios) {
    folios[id] = {{"type", codeTypeFolio(f.type)}, {"commentaire", f.commentaire}};
    if (!f.origine.empty()) folios[id]["origine"] = f.origine;
    if (!f.grafcet.empty()) folios[id]["grafcet"] = f.grafcet;
    if (!f.faisceau.empty()) folios[id]["faisceau"] = f.faisceau;
  }
  j["folios"] = folios;
  J conducteurs = J::object();
  for (const auto& [fil, c] : p.conducteurs) conducteurs[fil] = {{"section", c.section}, {"couleur", c.couleur}, {"type", c.type}};
  j["conducteurs"] = conducteurs;
  J cables = J::array();
  for (const auto& c : p.cables) {
    J a = J::array();
    for (const auto& x : c.affectation) a.push_back({{"repere", x.repere}, {"fil", x.fil}});
    cables.push_back({{"uuid", c.uuid},
                      {"reference", c.reference},
                      {"designation", c.designation},
                      {"conducteurs", c.conducteurs},
                      {"section", c.section},
                      {"longueur", c.longueur},
                      {"blinde", c.blinde},
                      {"de", c.de},
                      {"vers", c.vers},
                      {"affectation", a}});
  }
  j["cables"] = cables;
  J armoires = J::array();
  for (const auto& a : p.armoires) {
    J rails = J::array(), goulottes = J::array(), implantations = J::array();
    for (const auto& x : a.rails) rails.push_back({{"y", x.y}, {"x0", x.x0}, {"x1", x.x1}});
    for (const auto& g : a.goulottes) goulottes.push_back(J::array({g.b.x0, g.b.y0, g.b.x1, g.b.y1}));
    for (const auto& x : a.implantations) implantations.push_back({{"composant", x.composant}, {"p", pt(x.p)}, {"rail", x.rail}, {"porte", x.porte}});
    armoires.push_back({{"uuid", a.uuid},
                        {"nom", a.nom},
                        {"largeur", a.largeur},
                        {"hauteur", a.hauteur},
                        {"profondeur", a.profondeur},
                        {"marge", a.marge},
                        {"tableauModulaire", a.tableauModulaire},
                        {"modulesParRangee", a.modulesParRangee},
                        {"pose", a.pose},
                        {"temperatureExterieure", a.temperatureExterieure},
                        {"temperatureAdmise", a.temperatureAdmise},
                        {"charge", a.charge},
                        {"rails", rails},
                        {"goulottes", goulottes},
                        {"implantations", implantations}});
  }
  j["armoires"] = armoires;
  J faisceaux = J::array();
  for (const auto& f : p.faisceaux) {
    J noeuds = J::array(), branches = J::array();
    for (const auto& n : f.noeuds) noeuds.push_back({{"uuid", n.uuid}, {"nom", n.nom}, {"p", pt(n.p)}, {"composant", n.composant}});
    for (const auto& b : f.branches) branches.push_back({{"a", b.a}, {"b", b.b}, {"longueur", b.longueur}, {"protection", b.protection}});
    faisceaux.push_back({{"uuid", f.uuid}, {"nom", f.nom}, {"surlongueur", f.surlongueur}, {"noeuds", noeuds}, {"branches", branches}});
  }
  j["faisceaux"] = faisceaux;
  if (p.logement) j["logement"] = logementVersJson(*p.logement);
  if (p.distribution) j["distribution"] = reseauVersJson(*p.distribution);
  if (p.photovoltaique) j["photovoltaique"] = pvVersJson(*p.photovoltaique);
  if (!p.prix.empty()) {
    J prix = J::object();
    for (const auto& [k, v] : p.prix) prix[k] = v;
    j["prix"] = prix;
  }
  j["tauxHoraire"] = p.tauxHoraire;
  if (!p.grafcets.empty()) {
    J grafcets = J::array();
    for (const auto& g : p.grafcets) {
      J etapes = J::array(), transitions = J::array();
      for (const auto& e : g.etapes) {
        J actions = J::array();
        for (const auto& a : e.actions) actions.push_back(texteAction(a));
        J x = {{"numero", e.numero}, {"actions", actions}};
        if (e.initiale) x["initiale"] = true;
        if (!e.commentaire.empty()) x["commentaire"] = e.commentaire;
        etapes.push_back(x);
      }
      for (const auto& t : g.transitions) {
        J x = {{"amont", t.amont}, {"aval", t.aval}, {"receptivite", t.receptivite}};
        if (!t.commentaire.empty()) x["commentaire"] = t.commentaire;
        transitions.push_back(x);
      }
      grafcets.push_back({{"uuid", g.uuid}, {"nom", g.nom}, {"etapes", etapes}, {"transitions", transitions}});
    }
    j["grafcets"] = grafcets;
  }
  if (!p.revisions.empty()) {
    J revisions = J::array();
    for (const auto& r : p.revisions) revisions.push_back({{"indice", r.indice}, {"date", r.date}, {"auteur", r.auteur}, {"description", r.description}});
    j["revisions"] = revisions;
  }
  j["schema"] = ecad::versJson(p.schema);
  return j;
}

ProjetElectrique projetElectriqueDepuisJson(const J& j, std::vector<std::string>* alertes) {
  ProjetElectrique p;
  p.uuid = txt(j, "uuid");
  if (p.uuid.empty()) p.uuid = ecad::nouvelUuid();
  p.nom = txt(j, "nom", p.nom);
  if (const int v = ent(j, "version", 1); v > 1 && alertes)
    alertes->push_back("Projet électrique enregistré par une version plus récente de Prométhée (format " + std::to_string(v) + ") : ce qui est inconnu est ignoré.");
  const J& r = cle(j, "reglages");
  ReglagesElectriques& g = p.reglages;
  g.norme = txt(r, "norme") == "cei81346" ? NormeRepere::Cei81346 : NormeRepere::Classique;
  g.tiret = vrai(r, "tiret", g.tiret);
  g.fils = txt(r, "fils") == "parFolio" ? NumerotationFils::ParFolio : NumerotationFils::Sequentielle;
  g.format = txt(r, "format", g.format);
  if (g.format != "A4" && g.format != "A3" && g.format != "A2") g.format = "A3";
  g.colonnes = std::clamp(ent(r, "colonnes", g.colonnes), 1, 40);
  g.lignes = std::clamp(ent(r, "lignes", g.lignes), 1, 26);
  g.tension = num(r, "tension", g.tension);
  g.tensionSimple = num(r, "tensionSimple", g.tensionSimple);
  g.frequence = num(r, "frequence", g.frequence);
  g.regime = regimeDepuis(txt(r, "regime", "TT"));
  g.tensionCommande = num(r, "tensionCommande", g.tensionCommande);
  g.commandeContinue = vrai(r, "commandeContinue", g.commandeContinue);
  g.societe = txt(r, "societe");
  g.auteur = txt(r, "auteur");
  g.client = txt(r, "client");
  g.installation = txt(r, "installation");
  g.emplacement = txt(r, "emplacement");
  const J& schema = cle(j, "schema");
  if (schema.is_object()) p.schema = ecad::conceptionDepuisJson(schema, alertes);
  else {
    p.schema = ecad::nouvelleConception(p.nom);
    if (alertes) alertes->push_back("Projet électrique sans schéma : un folio vide a été créé.");
  }
  if (const J& f = cle(j, "folios"); f.is_object())
    for (auto it = f.begin(); it != f.end(); ++it)
      p.folios[it.key()] = {typeFolioDepuis(txt(it.value(), "type")), txt(it.value(), "commentaire"), txt(it.value(), "origine"), txt(it.value(), "grafcet"), txt(it.value(), "faisceau")};
  // Un folio sans fiche (projet ancien, fichier retouché) en reçoit une ; une fiche sans folio est retirée
  for (const auto& f : p.schema.feuilles)
    if (!p.folios.count(f.uuid)) p.folios[f.uuid] = {};
  for (auto it = p.folios.begin(); it != p.folios.end();) {
    bool existe = false;
    for (const auto& f : p.schema.feuilles) existe = existe || f.uuid == it->first;
    it = existe ? std::next(it) : p.folios.erase(it);
  }
  if (const J& c = cle(j, "conducteurs"); c.is_object())
    for (auto it = c.begin(); it != c.end(); ++it) {
      ProprietesConducteur x;
      x.section = std::max(0.0, num(it.value(), "section"));
      x.couleur = txt(it.value(), "couleur");
      x.type = txt(it.value(), "type");
      p.conducteurs[it.key()] = x;
    }
  for (const auto& x : liste(j, "cables")) {
    if (!x.is_object()) continue;
    Cable c;
    c.uuid = txt(x, "uuid");
    if (c.uuid.empty()) c.uuid = ecad::nouvelUuid();
    c.reference = txt(x, "reference");
    c.designation = txt(x, "designation", c.designation);
    c.conducteurs = std::clamp(ent(x, "conducteurs", c.conducteurs), 1, 61);
    c.section = std::max(0.0, num(x, "section", c.section));
    c.longueur = std::max(0.0, num(x, "longueur"));
    c.blinde = vrai(x, "blinde");
    c.de = txt(x, "de");
    c.vers = txt(x, "vers");
    for (const auto& a : liste(x, "affectation"))
      if (a.is_object()) c.affectation.push_back({txt(a, "repere"), txt(a, "fil")});
    p.cables.push_back(c);
  }
  for (const auto& x : liste(j, "armoires")) {
    if (!x.is_object()) continue;
    Armoire a;
    a.uuid = txt(x, "uuid");
    if (a.uuid.empty()) a.uuid = ecad::nouvelUuid();
    a.nom = txt(x, "nom", a.nom);
    a.largeur = std::clamp(num(x, "largeur", a.largeur), 50.0, 5000.0);
    a.hauteur = std::clamp(num(x, "hauteur", a.hauteur), 50.0, 5000.0);
    a.profondeur = std::clamp(num(x, "profondeur", a.profondeur), 20.0, 2000.0);
    a.marge = std::clamp(num(x, "marge", a.marge), 0.0, 500.0);
    a.tableauModulaire = vrai(x, "tableauModulaire");
    a.modulesParRangee = std::clamp(ent(x, "modulesParRangee", a.modulesParRangee), 4, 48);
    a.pose = std::clamp(ent(x, "pose", a.pose), 0, 6);
    a.temperatureExterieure = std::clamp(num(x, "temperatureExterieure", a.temperatureExterieure), -40.0, 70.0);
    a.temperatureAdmise = std::clamp(num(x, "temperatureAdmise", a.temperatureAdmise), 0.0, 90.0);
    a.charge = std::clamp(num(x, "charge", a.charge), 0.0, 1.0);
    for (const auto& y : liste(x, "rails"))
      if (y.is_object()) a.rails.push_back({coordonnee(num(y, "y")), coordonnee(num(y, "x0")), coordonnee(num(y, "x1"))});
    for (const auto& y : liste(x, "goulottes"))
      if (y.is_array() && y.size() == 4 && y[0].is_number() && y[1].is_number() && y[2].is_number() && y[3].is_number())
        a.goulottes.push_back({{coordonnee(y[0].get<double>()), coordonnee(y[1].get<double>()), coordonnee(y[2].get<double>()), coordonnee(y[3].get<double>())}});
    for (const auto& y : liste(x, "implantations"))
      if (y.is_object()) {
        Implantation im;
        im.composant = txt(y, "composant");
        im.p = lirePt(cle(y, "p"));
        im.rail = ent(y, "rail", -1);
        im.porte = vrai(y, "porte");
        if (!im.composant.empty()) a.implantations.push_back(im);
      }
    p.armoires.push_back(a);
  }
  for (const auto& x : liste(j, "faisceaux")) {
    if (!x.is_object()) continue;
    Faisceau f;
    f.uuid = txt(x, "uuid");
    if (f.uuid.empty()) f.uuid = ecad::nouvelUuid();
    f.nom = txt(x, "nom", f.nom);
    f.surlongueur = std::max(0.0, num(x, "surlongueur", f.surlongueur));
    for (const auto& y : liste(x, "noeuds"))
      if (y.is_object()) f.noeuds.push_back({txt(y, "uuid"), txt(y, "nom"), lirePt(cle(y, "p")), txt(y, "composant")});
    for (const auto& y : liste(x, "branches"))
      if (y.is_object()) f.branches.push_back({txt(y, "a"), txt(y, "b"), std::max(0.0, num(y, "longueur")), txt(y, "protection")});
    p.faisceaux.push_back(f);
  }
  if (const J& l = cle(j, "logement"); l.is_object()) p.logement = logementDepuisJson(l);
  if (const J& d = cle(j, "distribution"); d.is_object()) p.distribution = reseauDepuisJson(d);
  if (const J& v = cle(j, "photovoltaique"); v.is_object()) p.photovoltaique = pvDepuisJson(v);
  if (const J& x = cle(j, "prix"); x.is_object())
    for (const auto& [k, v] : x.items())
      if (v.is_number() && std::isfinite(v.get<double>()) && v.get<double>() >= 0) p.prix[k] = v.get<double>();
  p.tauxHoraire = std::clamp(num(j, "tauxHoraire", p.tauxHoraire), 0.0, 10000.0);
  for (const auto& x : liste(j, "revisions"))
    if (x.is_object()) p.revisions.push_back({txt(x, "indice"), txt(x, "date"), txt(x, "auteur"), txt(x, "description")});
  for (const auto& x : liste(j, "grafcets")) {
    if (!x.is_object()) continue;
    Grafcet g;
    g.uuid = txt(x, "uuid");
    if (g.uuid.empty()) g.uuid = ecad::nouvelUuid();
    g.nom = txt(x, "nom", g.nom);
    auto numeros = [](const J& v) {
      std::vector<int> r;
      if (v.is_array())
        for (const auto& n : v)
          if (n.is_number_integer()) r.push_back(n.get<int>());
      return r;
    };
    for (const auto& e : liste(x, "etapes")) {
      if (!e.is_object()) continue;
      EtapeGrafcet et;
      et.numero = ent(e, "numero");
      et.initiale = vrai(e, "initiale");
      et.commentaire = txt(e, "commentaire");
      for (const auto& a : liste(e, "actions")) {
        ActionGrafcet ac;
        std::string erreur;
        if (a.is_string() && lireAction(a.get<std::string>(), ac, &erreur)) et.actions.push_back(ac);
        else if (alertes && a.is_string()) alertes->push_back("Grafcet " + g.nom + ", étape " + std::to_string(et.numero) + " : action « " + a.get<std::string>() + " » ignorée (" + erreur + ").");
      }
      g.etapes.push_back(et);
    }
    for (const auto& t : liste(x, "transitions"))
      if (t.is_object()) g.transitions.push_back({numeros(cle(t, "amont")), numeros(cle(t, "aval")), txt(t, "receptivite", "1"), txt(t, "commentaire")});
    p.grafcets.push_back(g);
  }
  return p;
}

}  // namespace prom::elec

// Prométhée : pièces paramétriques. Licence GPL-3.0-only.
#include "promethee/piece.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/depot.hpp"
#include "promethee/lecture.hpp"
#include "promethee/unites.hpp"

#include <map>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>

namespace prom {

namespace {
struct NomType { TypeFonction t; const char* cle; const char* nom; };
const NomType TYPES[] = {{TypeFonction::Esquisse, "esquisse", "Esquisse"},     {TypeFonction::Extrusion, "extrusion", "Extrusion"},
                         {TypeFonction::Revolution, "revolution", "Révolution"}, {TypeFonction::Conge, "conge", "Congé"},
                         {TypeFonction::Chanfrein, "chanfrein", "Chanfrein"},   {TypeFonction::Coque, "coque", "Coque"},
                         {TypeFonction::Plan, "plan", "Plan"},                  {TypeFonction::Axe, "axe", "Axe"},
                         {TypeFonction::Point, "point", "Point"},               {TypeFonction::Repere, "repere", "Repère"},
                         {TypeFonction::Percage, "percage", "Perçage"},         {TypeFonction::Depouille, "depouille", "Dépouille"},
                         {TypeFonction::Repetition, "repetition", "Répétition"}, {TypeFonction::Symetrie, "symetrie", "Symétrie"},
                         {TypeFonction::Balayage, "balayage", "Balayage"},      {TypeFonction::Lissage, "lissage", "Lissage"},
                         {TypeFonction::Esquisse3D, "esquisse3d", "Esquisse 3D"}, {TypeFonction::Import, "import", "Import"},
                         {TypeFonction::Tole, "tole", "Tôle de base"},          {TypeFonction::BordTombe, "bordTombe", "Bord tombé"},
                         {TypeFonction::Nervure, "nervure", "Nervure"},          {TypeFonction::Filetage, "filetage", "Filetage"}};
const char* cleType(TypeFonction t) { for (const auto& x : TYPES) if (x.t == t) return x.cle; return "esquisse"; }
}  // namespace

const char* nomTypeFonction(TypeFonction t) {
  if (t == TypeFonction::Inconnu) return "Fonction inconnue";
  for (const auto& x : TYPES) if (x.t == t) return x.nom;
  return "?";
}

FonctionPiece* Piece::fonction(int id) { for (auto& f : fonctions) if (f.id == id) return &f; return nullptr; }
const FonctionPiece* Piece::fonction(int id) const { for (const auto& f : fonctions) if (f.id == id) return &f; return nullptr; }

int Piece::ajouter(FonctionPiece f) {
  f.id = prochainId++;
  if (f.nom.empty()) {
    // Nom par défaut : le plus grand numéro déjà pris pour ce préfixe, plus un (jamais un nom en double après une
    // suppression). Les enlèvements ont leur propre série.
    const bool enlevement = f.type == TypeFonction::Extrusion && f.enlever;
    const std::string prefixe = std::string(enlevement ? "Enlèvement" : nomTypeFonction(f.type)) + " ";
    int n = 0;
    for (const auto& g : fonctions)
      if (g.nom.rfind(prefixe, 0) == 0) {
        const std::string reste = g.nom.substr(prefixe.size());
        if (!reste.empty() && reste.size() < 9 && std::all_of(reste.begin(), reste.end(), [](char c) { return c >= '0' && c <= '9'; }))
          n = std::max(n, std::stoi(reste));
      }
    f.nom = prefixe + std::to_string(n + 1);
  }
  fonctions.push_back(f);
  return f.id;
}

std::vector<int> parentsDe(const FonctionPiece& f) {
  std::set<int> r;
  auto lire = [&](std::string ref) {
    if (ref.rfind("S:", 0) == 0) ref = ref.substr(2);
    std::size_t debut = 0;
    while (true) {
      const std::size_t fin = ref.find('|', debut);
      std::string morceau = ref.substr(debut, fin == std::string::npos ? std::string::npos : fin - debut);
      if (morceau.rfind("F:", 0) == 0 || morceau.rfind("E:", 0) == 0) morceau = morceau.substr(2);
      if (!morceau.empty() && std::isdigit(static_cast<unsigned char>(morceau[0]))) r.insert(std::atoi(morceau.c_str()));
      if (fin == std::string::npos) break;
      debut = fin + 1;
    }
  };
  if (f.esquisseId) r.insert(f.esquisseId);
  if (f.cheminId) r.insert(f.cheminId);
  for (int s : f.sources) r.insert(s);
  for (const auto& x : f.supports) lire(x);
  for (const auto& x : f.references) lire(x);
  for (const auto& [x, e] : f.epaisseursFaces) lire(x);
  for (const std::string* x : {&f.face, &f.refFin, &f.refFin2, &f.planNeutre, &f.direction1, &f.direction2, &f.faceChanfrein, &f.axeBase}) lire(*x);
  r.erase(f.id);
  r.erase(0);
  return {r.begin(), r.end()};
}

std::string deplacementInterdit(const Piece& p, int id, int sens) {
  const auto& fs = p.fonctions;
  for (std::size_t i = 0; i < fs.size(); ++i) {
    if (fs[i].id != id) continue;
    const long j = static_cast<long>(i) + sens;
    if (j < 0 || j >= static_cast<long>(fs.size())) return "La fonction est déjà " + std::string(sens < 0 ? "en tête" : "en fin") + " de l'arbre.";
    const FonctionPiece& autre = fs[static_cast<std::size_t>(j)];
    const FonctionPiece& avant = sens < 0 ? autre : fs[i];   // celle qui passera après l'autre
    const FonctionPiece& apres = sens < 0 ? fs[i] : autre;
    const auto parents = parentsDe(apres);
    if (std::find(parents.begin(), parents.end(), avant.id) != parents.end())
      return "« " + apres.nom + " » utilise « " + avant.nom + " » : elle doit rester après elle dans l'arbre.";
    return {};
  }
  return "Fonction introuvable.";
}

bool deplacerBloc(Piece& p, int id, int sens, std::string* raison) {
  auto refus = [raison](const std::string& m) { if (raison) *raison = m; return false; };
  auto& fs = p.fonctions;
  // Esquisse rangée sous la fonction qui l'utilise, comme dans l'arbre : la première extrusion ou révolution qui la cite
  std::map<int, int> consommateur;
  for (const auto& f : fs)
    if ((f.type == TypeFonction::Extrusion || f.type == TypeFonction::Revolution || f.type == TypeFonction::Tole || f.type == TypeFonction::Nervure) && f.esquisseId && !consommateur.count(f.esquisseId)) consommateur[f.esquisseId] = f.id;
  // Blocs dans l'ordre de l'arbre : une fonction et les esquisses rangées sous elle
  struct Bloc { int tete; std::vector<FonctionPiece> membres; };
  std::vector<Bloc> blocs;
  std::map<int, std::vector<FonctionPiece>> absorbees;
  for (const auto& f : fs)
    if (f.type == TypeFonction::Esquisse && consommateur.count(f.id)) absorbees[consommateur[f.id]].push_back(f);
  for (const auto& f : fs) {
    if (f.type == TypeFonction::Esquisse && consommateur.count(f.id)) continue;
    Bloc b{f.id, absorbees[f.id]};
    b.membres.push_back(f);
    blocs.push_back(std::move(b));
  }
  const int cible = consommateur.count(id) ? consommateur[id] : id;
  std::size_t k = blocs.size();
  for (std::size_t i = 0; i < blocs.size(); ++i) if (blocs[i].tete == cible) k = i;
  if (k == blocs.size()) return refus("Fonction introuvable.");
  const long j = static_cast<long>(k) + sens;
  if (sens == 0 || j < 0 || j >= static_cast<long>(blocs.size())) return refus("La fonction est déjà " + std::string(sens < 0 ? "en tête" : "en fin") + " de l'arbre.");
  std::swap(blocs[k], blocs[static_cast<std::size_t>(j)]);
  std::vector<FonctionPiece> ordre;
  for (const auto& b : blocs) ordre.insert(ordre.end(), b.membres.begin(), b.membres.end());
  // Aucune fonction ne doit venir avant une fonction dont elle dépend (FO-04, AP-06)
  std::map<int, std::size_t> rang;
  for (std::size_t i = 0; i < ordre.size(); ++i) rang[ordre[i].id] = i;
  for (std::size_t i = 0; i < ordre.size(); ++i)
    for (int parent : parentsDe(ordre[i]))
      if (const auto it = rang.find(parent); it != rang.end() && it->second > i) {
        const FonctionPiece* pa = nullptr;
        for (const auto& f : ordre) if (f.id == parent) pa = &f;
        return refus("« " + ordre[i].nom + " » utilise « " + (pa ? pa->nom : std::string("?")) + " » : elle doit rester après elle dans l'arbre.");
      }
  fs = std::move(ordre);
  return true;
}

std::string defautNomVariable(const std::string& nom) {
  if (nom.empty()) return "Le nom est vide.";
  const auto lettre = [](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
  if (!lettre(static_cast<unsigned char>(nom[0]))) return "« " + nom + " » : un nom commence par une lettre sans accent ou « _ ».";
  for (unsigned char c : nom)
    if (!lettre(c) && !(c >= '0' && c <= '9')) return "« " + nom + " » : lettres sans accent, chiffres et « _ » seulement (une cote ne pourrait pas la citer).";
  static const std::set<std::string> unites = {"mm", "cm", "m", "in", "ft", "deg", "rad"};
  if (unites.count(nom)) return "« " + nom + " » est une unité : choisis un autre nom.";
  return {};
}

std::string Piece::propriete(const std::string& nom) const {
  auto pli = [](const std::string& t) {
    std::string r = translitterer(t);
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
  };
  const std::string cherche = pli(nom);
  for (const auto& [n, v] : proprietes) if (pli(n) == cherche) return v;
  return {};
}

Json pieceVersJson(const Piece& p) {
  Json j = {{"id", p.id}, {"nom", p.nom}, {"prochainId", p.prochainId}, {"fonctions", Json::array()}};
  if (p.uniteLongueur != "mm") j["uniteLongueur"] = p.uniteLongueur;
  if (p.uniteAngle != "deg") j["uniteAngle"] = p.uniteAngle;
  if (p.materiau != "PLA" || p.densiteConfirmee) { j["materiau"] = p.materiau; j["densite"] = p.densite; }
  if (p.densiteConfirmee) j["materiauConfirme"] = true;
  if (p.couleur != "#C9CFD6") j["couleur"] = p.couleur;
  if (!p.couleursFaces.empty()) { Json c = Json::object(); for (const auto& [n, v] : p.couleursFaces) c[n] = v; j["couleursFaces"] = c; }
  if (!p.reference.empty()) j["reference"] = p.reference;
  if (!p.fournisseur.empty()) j["fournisseur"] = p.fournisseur;
  if (!p.proprietes.empty()) { Json l = Json::array(); for (const auto& [n, v] : p.proprietes) l.push_back({n, v}); j["proprietes"] = l; }
  if (p.repere > 0) j["repere"] = p.repere;
  if (p.repereSymetrique > 0) j["repereSymetrique"] = p.repereSymetrique;
  if (!p.variables.empty()) { Json l = Json::array(); for (const auto& [n, e] : p.variables) l.push_back({n, e}); j["variables"] = l; }
  if (p.transparence > 0) j["transparence"] = p.transparence;
  if (!p.plans.empty()) {
    Json l = Json::array();
    for (const auto& pl : p.plans) {
      Json o = {{"nom", pl.nom}, {"format", pl.format}, {"echelle", pl.echelle}, {"face", pl.face}, {"dessus", pl.dessus}, {"gauche", pl.gauche}, {"iso", pl.iso}, {"cachees", pl.cachees}};
      if (pl.portrait) o["portrait"] = true;
      if (pl.troisiemeDiedre) o["troisiemeDiedre"] = true;
      if (pl.vueFace != "avant") o["vueFace"] = pl.vueFace;
      if (pl.rotationFace != 0) o["rotationFace"] = pl.rotationFace;
      if (pl.vueVerticale != "dessus") o["vueVerticale"] = pl.vueVerticale;
      if (pl.vueLaterale != "gauche") o["vueLaterale"] = pl.vueLaterale;
      if (!pl.axes) o["axes"] = false;
      if (pl.tangentes) o["tangentes"] = true;
      if (!pl.coupe.empty()) { o["coupe"] = pl.coupe; o["decalageCoupe"] = pl.decalageCoupe; if (pl.demiCoupe) o["demiCoupe"] = true; }
      if (pl.detail) {
        o["detail"] = {{"vue", pl.vueDetail}, {"x", pl.detailX}, {"y", pl.detailY}, {"rayon", pl.detailRayon}, {"echelle", pl.detailEchelle}};
      }
      if (pl.interrompue) o["interruption"] = {pl.interruptionDebut, pl.interruptionFin};
      if (pl.cotes) o["cotes"] = true;
      if (pl.cotesModele) o["cotesModele"] = true;
      if (!pl.notes.empty()) o["notes"] = pl.notes;
      if (!pl.toleranceGenerale.empty()) o["toleranceGenerale"] = pl.toleranceGenerale;
      LecteurJson::reporter(o, pl.autres);
      l.push_back(o);
    }
    j["plans"] = l;
  }
  for (const auto& f : p.fonctions) {
    if (f.type == TypeFonction::Inconnu) {
      // Fonction d'une version plus récente : réécrite telle qu'elle a été lue
      Json o = f.autres.is_object() ? f.autres : Json::object();
      o["id"] = f.id;
      j["fonctions"].push_back(o);
      continue;
    }
    Json o = {{"id", f.id}, {"type", cleType(f.type)}, {"nom", f.nom}};
    if (f.desactivee) o["desactivee"] = true;
    if (f.visibilite) o["visibilite"] = f.visibilite;
    switch (f.type) {
      case TypeFonction::Esquisse:
        o["plan"] = f.plan == PlanBase::XY ? "XY" : f.plan == PlanBase::XZ ? "XZ" : "YZ";
        if (f.decalage != 0) o["decalage"] = f.decalage;
        if (!f.face.empty()) o["face"] = f.face;
        if (f.axeX[0] != 0 || f.axeX[1] != 0 || f.axeX[2] != 0) o["axeX"] = {f.axeX[0], f.axeX[1], f.axeX[2]};
        o["esquisse"] = esquisseVersJson(f.esquisse);
        break;
      case TypeFonction::Extrusion:
        o["esquisseId"] = f.esquisseId; o["longueur"] = f.longueur; o["enlever"] = f.enlever;
        if (f.symetrique) o["symetrique"] = true;
        if (f.inverse) o["inverse"] = true;
        if (f.traversant) o["traversant"] = true;
        if (f.fin != "borgne") o["fin"] = f.fin;
        if (!f.refFin.empty()) o["refFin"] = f.refFin;
        if (f.decalageFin != 0) o["decalageFin"] = f.decalageFin;
        if (!f.fin2.empty()) { o["fin2"] = f.fin2; o["longueur2"] = f.longueur2; }
        if (!f.refFin2.empty()) o["refFin2"] = f.refFin2;
        if (f.decalageFin2 != 0) o["decalageFin2"] = f.decalageFin2;
        if (f.depouille != 0) o["depouille"] = f.depouille;
        if (f.mince) { o["mince"] = true; o["epaisseurMince"] = f.epaisseurMince; o["sensMince"] = f.sensMince; }
        if (f.mince && f.minceArrondi) o["minceArrondi"] = true;
        if (!f.regions.empty()) o["regions"] = f.regions;
        if (!f.pointsRegions.empty()) {
          Json pts = Json::array();
          for (const auto& q : f.pointsRegions) pts.push_back({q[0], q[1]});
          o["pointsRegions"] = pts;
        }
        break;
      case TypeFonction::Revolution:
        o["esquisseId"] = f.esquisseId; o["axe"] = f.axe; o["angle"] = f.angle; o["enlever"] = f.enlever;
        if (!f.axeBase.empty()) o["axeBase"] = f.axeBase;
        if (f.inverse) o["inverse"] = true;
        if (f.milieu) o["milieu"] = true;
        if (f.angle2 != 0) o["angle2"] = f.angle2;
        if (f.mince) { o["mince"] = true; o["epaisseurMince"] = f.epaisseurMince; o["sensMince"] = f.sensMince; }
        if (f.mince && f.minceArrondi) o["minceArrondi"] = true;
        if (!f.regions.empty()) o["regions"] = f.regions;
        if (!f.pointsRegions.empty()) {
          Json pts = Json::array();
          for (const auto& q : f.pointsRegions) pts.push_back({q[0], q[1]});
          o["pointsRegions"] = pts;
        }
        break;
      case TypeFonction::Conge: o["references"] = f.references; o["rayon"] = f.rayon; break;
      case TypeFonction::Chanfrein:
        o["references"] = f.references; o["distance"] = f.distance;
        if (f.typeChanfrein != "egal") { o["typeChanfrein"] = f.typeChanfrein; o["distance2"] = f.distance2; o["angleChanfrein"] = f.angleChanfrein; }
        if (f.typeChanfrein != "egal" && !f.faceChanfrein.empty()) o["faceChanfrein"] = f.faceChanfrein;
        if (f.inverse) o["inverse"] = true;
        break;
      case TypeFonction::Coque:
        o["references"] = f.references; o["epaisseur"] = f.epaisseur;
        if (!f.epaisseursFaces.empty()) {
          Json l = Json::array();
          for (const auto& [n, e] : f.epaisseursFaces) l.push_back({n, e});
          o["epaisseursFaces"] = l;
        }
        break;
      case TypeFonction::Plan: case TypeFonction::Axe: case TypeFonction::Point: case TypeFonction::Repere:
        o["mode"] = f.mode; o["supports"] = f.supports;
        if (f.distance != 1) o["distance"] = f.distance;
        if (f.angle != 360) o["angle"] = f.angle;
        if (f.x != 0 || f.y != 0 || f.z != 0) o["xyz"] = {f.x, f.y, f.z};
        if (f.inverse) o["inverse"] = true;
        break;
      case TypeFonction::Percage:
        o["esquisseId"] = f.esquisseId; o["typePercage"] = f.typePercage; o["norme"] = f.norme; o["taille"] = f.taille;
        o["ajustement"] = f.ajustement; o["diametre"] = f.diametre; o["profondeur"] = f.profondeur; o["longueurFiletee"] = f.longueurFiletee;
        o["traversant"] = f.traversant; o["pointeForet"] = f.pointeForet;
        if (f.inverse) o["inverse"] = true;
        break;
      case TypeFonction::Balayage:
      case TypeFonction::Lissage:
        o["esquisseId"] = f.esquisseId; o["cheminId"] = f.cheminId; o["sources"] = f.sources; o["enlever"] = f.enlever;
        if (f.tube) { o["tube"] = true; o["diametre"] = f.diametre; o["epaisseur"] = f.epaisseur; }
        break;
      case TypeFonction::Import:
        o["donnees"] = f.donnees; o["source"] = f.source;
        break;
      case TypeFonction::Tole:
        o["esquisseId"] = f.esquisseId; o["epaisseur"] = f.epaisseur; o["rayon"] = f.rayon;
        if (f.facteurK > 0) o["facteurK"] = f.facteurK;
        if (f.degagement != "rectangulaire") o["degagement"] = f.degagement;
        if (f.inverse) o["inverse"] = true;
        if (!f.regions.empty()) o["regions"] = f.regions;
        break;
      case TypeFonction::Nervure:
        o["esquisseId"] = f.esquisseId; o["epaisseur"] = f.epaisseur; o["mode"] = f.mode.empty() ? std::string("parallele") : f.mode; o["sensMince"] = f.sensMince;
        if (f.inverse) o["inverse"] = true;
        break;
      case TypeFonction::Filetage:
        o["references"] = f.references; o["norme"] = f.norme; o["taille"] = f.taille; o["longueurFiletee"] = f.longueurFiletee;
        if (f.pasFiletage > 0) o["pasFiletage"] = f.pasFiletage;
        if (f.inverse) o["inverse"] = true;
        break;
      case TypeFonction::BordTombe:
        o["references"] = f.references; o["longueur"] = f.longueur; o["angle"] = f.angle; o["rayon"] = f.rayon;
        o["mesure"] = f.mesure; o["positionPli"] = f.positionPli;
        if (f.inverse) o["inverse"] = true;
        if (f.retraitDebut != 0) o["retraitDebut"] = f.retraitDebut;
        if (f.retraitFin != 0) o["retraitFin"] = f.retraitFin;
        break;
      case TypeFonction::Esquisse3D: {
        Json pts = Json::array();
        for (const auto& q : f.points3d) pts.push_back({q[0], q[1], q[2]});
        o["points3d"] = pts; o["lisse"] = f.lisse; o["fermee"] = f.fermee;
        break;
      }
      case TypeFonction::Depouille:
        o["planNeutre"] = f.planNeutre; o["references"] = f.references; o["angle"] = f.angle;
        if (f.inverse) o["inverse"] = true;
        break;
      case TypeFonction::Repetition: case TypeFonction::Symetrie: {
        o["sources"] = f.sources;
        o["typeRepetition"] = f.typeRepetition;
        o["direction1"] = f.direction1; o["nombre1"] = f.nombre1; o["pas1"] = f.pas1;
        if (!f.direction2.empty()) { o["direction2"] = f.direction2; o["nombre2"] = f.nombre2; o["pas2"] = f.pas2; }
        if (f.inverse1) o["inverse1"] = true;
        if (f.inverse2) o["inverse2"] = true;
        if (f.esquisseId) o["esquisseId"] = f.esquisseId;
        if (f.angle != 360) o["angle"] = f.angle;
        if (!f.supports.empty()) o["supports"] = f.supports;
        if (!f.tableau.empty()) {
          Json l = Json::array();
          for (const auto& t : f.tableau) l.push_back({t[0], t[1], t[2]});
          o["tableau"] = l;
        }
        break;
      }
      case TypeFonction::Inconnu: break;
    }
    LecteurJson::reporter(o, f.autres);
    j["fonctions"].push_back(o);
  }
  LecteurJson::reporter(j, p.autres);
  return j;
}

Piece pieceDepuisJson(const Json& j, std::vector<std::string>* alertes) {
  Piece p;
  if (!j.is_object()) {
    if (alertes && !j.is_null()) alertes->push_back("Pièce illisible (ce n'est pas un objet JSON) : ignorée");
    return p;
  }
  const LecteurJson L(alertes, "Pièce « " + (j.contains("nom") && j["nom"].is_string() ? j["nom"].get<std::string>() : std::string("?")) + " »");
  p.id = L.texte(j, "id", std::string());
  p.nom = L.texte(j, "nom", std::string("Pièce"));
  p.uniteLongueur = L.texte(j, "uniteLongueur", std::string("mm"));
  p.uniteAngle = L.texte(j, "uniteAngle", std::string("deg"));
  p.materiau = L.texte(j, "materiau", std::string("PLA"));
  // Densité : un nombre plausible (0,001 à 100 g/cm³), sinon le défaut, marqué provisoire.
  const double d = L.nombre(j, "densite", -1);
  const bool densiteLisible = d >= 0.001 && d <= 100;
  p.densite = densiteLisible ? d : 1.24;
  p.densiteConfirmee = densiteLisible && (L.booleen(j, "materiauConfirme", false) || p.materiau != "PLA");
  p.couleur = L.texte(j, "couleur", std::string("#C9CFD6"));
  if (j.contains("couleursFaces") && j["couleursFaces"].is_object())
    for (const auto& [n, v] : j["couleursFaces"].items()) {
      if (v.is_string() && v.get<std::string>().size() == 7 && v.get<std::string>()[0] == '#') p.couleursFaces[n] = v.get<std::string>();
      else L.alerter("couleur de face illisible écartée (" + n + ")");
    }
  p.reference = L.texte(j, "reference", std::string());
  p.fournisseur = L.texte(j, "fournisseur", std::string());
  if (j.contains("proprietes") && j["proprietes"].is_array())
    for (const auto& v : j["proprietes"]) {
      if (v.is_array() && v.size() == 2 && v[0].is_string() && v[1].is_string() && !v[0].get<std::string>().empty()) p.proprietes.push_back({v[0].get<std::string>(), v[1].get<std::string>()});
      else L.alerter("une propriété illisible est écartée");
    }
  p.repere = std::max(0, L.entier(j, "repere", 0));
  p.repereSymetrique = std::max(0, L.entier(j, "repereSymetrique", 0));
  if (j.contains("variables") && j["variables"].is_array())
    for (const auto& v : j["variables"]) {
      if (v.is_array() && v.size() == 2 && v[0].is_string() && v[1].is_string()) p.variables.push_back({v[0].get<std::string>(), v[1].get<std::string>()});
      else L.alerter("une variable illisible est écartée");
    }
  // Transparence : une fraction ; hors de [0 ; 1] elle n'a pas de sens et est ramenée, avec une alerte
  p.transparence = L.nombre(j, "transparence", 0.0);
  if (p.transparence < 0 || p.transparence > 1) {
    L.alerter("transparence hors de [0 ; 1] ramenée");
    p.transparence = std::clamp(p.transparence, 0.0, 1.0);
  }
  if (j.contains("plans") && j["plans"].is_array())
    for (const auto& o : j["plans"]) {
      if (!o.is_object()) { L.alerter("une mise en plan illisible est écartée"); continue; }
      const LecteurJson P = L.sous("mise en plan");
      PlanPiece pl;
      pl.nom = P.texte(o, "nom", std::string("Mise en plan"));
      pl.format = P.texte(o, "format", std::string("A4"));
      if (pl.format != "A0" && pl.format != "A1" && pl.format != "A2" && pl.format != "A3" && pl.format != "A4") { P.alerter("format de feuille inconnu : A4"); pl.format = "A4"; }
      pl.portrait = P.booleen(o, "portrait", false);
      pl.troisiemeDiedre = P.booleen(o, "troisiemeDiedre", false);
      pl.vueFace = P.texte(o, "vueFace", std::string("avant"));
      if (pl.vueFace != "avant" && pl.vueFace != "arriere" && pl.vueFace != "dessus" && pl.vueFace != "dessous" && pl.vueFace != "gauche" && pl.vueFace != "droite") {
        P.alerter("vue de face inconnue : vue de l'avant");
        pl.vueFace = "avant";
      }
      pl.rotationFace = ((P.entier(o, "rotationFace", 0) % 360) + 360) % 360 / 90 * 90;
      pl.vueVerticale = P.texte(o, "vueVerticale", std::string("dessus")) == "dessous" ? "dessous" : "dessus";
      pl.vueLaterale = P.texte(o, "vueLaterale", std::string("gauche")) == "droite" ? "droite" : "gauche";
      pl.echelle = P.nombre(o, "echelle", 0.0);
      if (pl.echelle < 0) { P.alerter("échelle négative remplacée par l'échelle automatique"); pl.echelle = 0; }
      if (pl.echelle > 0) {
        // Série ISO 5455 : une échelle hors série est ramenée à la plus proche, avec une alerte (MP-09)
        static const double serie[] = {50, 20, 10, 5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01, 0.005, 0.002, 0.001};
        double proche = 1, ecart = 1e300;
        for (double v : serie)
          if (const double d = std::abs(std::log(v / pl.echelle)); d < ecart) { ecart = d; proche = v; }
        if (std::abs(proche - pl.echelle) > 1e-12 * proche) { P.alerter("échelle hors de la série normalisée ramenée à la plus proche"); pl.echelle = proche; }
      }
      pl.face = P.booleen(o, "face", true); pl.dessus = P.booleen(o, "dessus", true); pl.gauche = P.booleen(o, "gauche", true);
      pl.iso = P.booleen(o, "iso", true); pl.cachees = P.booleen(o, "cachees", true);
      pl.axes = P.booleen(o, "axes", true); pl.tangentes = P.booleen(o, "tangentes", false);
      pl.coupe = P.texte(o, "coupe", std::string());
      if (!pl.coupe.empty() && pl.coupe != "face" && pl.coupe != "dessus" && pl.coupe != "gauche") { P.alerter("vue en coupe inconnue : sans coupe"); pl.coupe.clear(); }
      pl.decalageCoupe = P.nombre(o, "decalageCoupe", 0.0);
      pl.demiCoupe = P.booleen(o, "demiCoupe", false);
      if (o.contains("detail") && o["detail"].is_object()) {
        const Json& d = o["detail"];
        pl.detail = true;
        pl.vueDetail = P.texte(d, "vue", std::string("face"));
        pl.detailX = P.nombre(d, "x", 0.25);
        pl.detailY = P.nombre(d, "y", 0.25);
        pl.detailRayon = P.nombre(d, "rayon", 0.15);
        pl.detailEchelle = P.nombre(d, "echelle", 2.0);
      }
      if (o.contains("interruption") && o["interruption"].is_array() && o["interruption"].size() == 2 && o["interruption"][0].is_number() && o["interruption"][1].is_number()) {
        pl.interrompue = true;
        pl.interruptionDebut = o["interruption"][0].get<double>();
        pl.interruptionFin = o["interruption"][1].get<double>();
      }
      pl.cotes = P.booleen(o, "cotes", false);
      pl.cotesModele = P.booleen(o, "cotesModele", false);
      pl.notes = P.textes(o, "notes");
      pl.toleranceGenerale = P.texte(o, "toleranceGenerale", std::string());
      pl.autres = LecteurJson::inconnus(o, {"nom", "format", "portrait", "troisiemeDiedre", "vueFace", "rotationFace", "vueVerticale", "vueLaterale", "echelle", "face", "dessus", "gauche", "iso", "cachees", "axes", "tangentes", "coupe", "decalageCoupe", "demiCoupe", "detail",
                                            "interruption", "cotes", "cotesModele", "notes", "toleranceGenerale"});
      p.plans.push_back(pl);
    }
  int maxId = 0;
  if (j.contains("fonctions") && j["fonctions"].is_array())
    for (const auto& o : j["fonctions"]) {
      if (!o.is_object()) { L.alerter("une fonction illisible est écartée"); continue; }
      FonctionPiece f;
      f.id = L.entier(o, "id", 0);
      const LecteurJson F = L.sous("fonction " + std::to_string(f.id));
      if (f.id <= 0) { F.alerter("identifiant manquant : fonction écartée"); continue; }
      if (p.fonction(f.id)) { F.alerter("identifiant en double : fonction écartée"); continue; }
      const std::string t = F.texte(o, "type", std::string());
      bool connu = false;
      for (const auto& x : TYPES) if (t == x.cle) { f.type = x.t; connu = true; }
      f.nom = F.texte(o, "nom", std::string());
      if (!connu) {
        // Type d'une version plus récente : gardé tel quel, non calculé
        F.alerter("type « " + t + " » inconnu de cette version : fonction gardée sans être calculée");
        f.type = TypeFonction::Inconnu;
        f.autres = o;
        p.fonctions.push_back(f);
        maxId = std::max(maxId, f.id);
        continue;
      }
      f.desactivee = F.booleen(o, "desactivee", false);
      f.visibilite = std::clamp(F.entier(o, "visibilite", 0), 0, 2);
      const std::string plan = F.texte(o, "plan", std::string("XY"));
      f.plan = plan == "XZ" ? PlanBase::XZ : plan == "YZ" ? PlanBase::YZ : PlanBase::XY;
      f.decalage = F.nombre(o, "decalage", 0.0);
      f.face = F.texte(o, "face", std::string());
      if (o.contains("axeX")) {
        double x = 0, y = 0, z = 0;
        if (F.triplet(o["axeX"], x, y, z)) f.axeX = {x, y, z};
      }
      if (o.contains("esquisse")) {
        try {
          f.esquisse = esquisseDepuisJson(o["esquisse"], alertes);
        } catch (const std::exception& e) {
          F.alerter(std::string("esquisse illisible (") + e.what() + ") : vide");
        }
      }
      f.esquisseId = F.entier(o, "esquisseId", 0);
      f.enlever = F.booleen(o, "enlever", false);
      f.longueur = F.nombre(o, "longueur", 10.0);
      f.symetrique = F.booleen(o, "symetrique", false);
      f.inverse = F.booleen(o, "inverse", false);
      f.traversant = F.booleen(o, "traversant", false);
      f.axe = F.entier(o, "axe", 0);
      f.angle = F.nombre(o, "angle", 360.0);
      f.references = F.textes(o, "references");
      f.fin = F.texte(o, "fin", std::string("borgne"));
      f.refFin = F.texte(o, "refFin", std::string());
      f.decalageFin = F.nombre(o, "decalageFin", 0.0);
      f.fin2 = F.texte(o, "fin2", std::string());
      f.longueur2 = F.nombre(o, "longueur2", 10.0);
      f.refFin2 = F.texte(o, "refFin2", std::string());
      f.decalageFin2 = F.nombre(o, "decalageFin2", 0.0);
      f.depouille = F.nombre(o, "depouille", 0.0);
      f.mince = F.booleen(o, "mince", false);
      f.epaisseurMince = F.nombre(o, "epaisseurMince", 1.0);
      f.sensMince = F.texte(o, "sensMince", std::string("interieur"));
      f.minceArrondi = F.booleen(o, "minceArrondi", false);
      f.regions = F.textes(o, "regions");
      if (o.contains("pointsRegions") && o["pointsRegions"].is_array())
        for (const auto& q : o["pointsRegions"]) {
          if (q.is_array() && q.size() == 2 && q[0].is_number() && q[1].is_number() && std::isfinite(q[0].get<double>()) && std::isfinite(q[1].get<double>()))
            f.pointsRegions.push_back({q[0].get<double>(), q[1].get<double>()});
          else F.alerter("un point de région illisible est écarté");
        }
      if (f.pointsRegions.size() != f.regions.size()) f.pointsRegions.clear();  // incohérent : la clé seule fait foi
      f.axeBase = F.texte(o, "axeBase", std::string());
      f.milieu = F.booleen(o, "milieu", false);
      f.angle2 = F.nombre(o, "angle2", 0.0);
      f.typeChanfrein = F.texte(o, "typeChanfrein", std::string("egal"));
      f.distance2 = F.nombre(o, "distance2", 1.0);
      f.angleChanfrein = F.nombre(o, "angleChanfrein", 45.0);
      f.faceChanfrein = F.texte(o, "faceChanfrein", std::string());
      if (o.contains("epaisseursFaces") && o["epaisseursFaces"].is_array())
        for (const auto& x : o["epaisseursFaces"]) {
          if (x.is_array() && x.size() == 2 && x[0].is_string() && x[1].is_number() && std::isfinite(x[1].get<double>()))
            f.epaisseursFaces.push_back({x[0].get<std::string>(), x[1].get<double>()});
          else F.alerter("une épaisseur particulière de coque illisible est écartée");
        }
      f.mode = F.texte(o, "mode", std::string());
      f.supports = F.textes(o, "supports");
      if (o.contains("xyz")) {
        double x = 0, y = 0, z = 0;
        if (F.triplet(o["xyz"], x, y, z)) { f.x = x; f.y = y; f.z = z; }
      }
      f.typePercage = F.texte(o, "typePercage", std::string("simple"));
      f.norme = F.texte(o, "norme", std::string("ISO"));
      f.taille = F.texte(o, "taille", std::string("M5"));
      f.ajustement = F.texte(o, "ajustement", std::string("normal"));
      f.diametre = F.nombre(o, "diametre", 5.0);
      f.profondeur = F.nombre(o, "profondeur", 10.0);
      f.longueurFiletee = F.nombre(o, "longueurFiletee", 8.0);
      f.pointeForet = F.booleen(o, "pointeForet", true);
      f.planNeutre = F.texte(o, "planNeutre", std::string());
      f.cheminId = F.entier(o, "cheminId", 0);
      if (o.contains("points3d") && o["points3d"].is_array())
        for (const auto& q : o["points3d"]) {
          double x = 0, y = 0, z = 0;
          if (F.triplet(q, x, y, z)) f.points3d.push_back({x, y, z});
        }
      f.lisse = F.booleen(o, "lisse", false);
      f.donnees = deposerGeometrie(F.texte(o, "donnees", std::string()));  // gros texte BRep : au dépôt (EX-06)
      f.source = F.texte(o, "source", std::string());
      f.tube = F.booleen(o, "tube", false);
      f.fermee = F.booleen(o, "fermee", false);
      f.typeRepetition = F.texte(o, "typeRepetition", std::string("lineaire"));
      f.sources = F.entiers(o, "sources");
      f.direction1 = F.texte(o, "direction1", std::string());
      f.direction2 = F.texte(o, "direction2", std::string());
      // Nombres d'exemplaires : bornés pour la mémoire (10 000 au plus, comme dans l'interface), avec une alerte
      f.nombre1 = F.entier(o, "nombre1", 3);
      f.nombre2 = F.entier(o, "nombre2", 1);
      if (f.nombre1 < 1 || f.nombre1 > 10000 || f.nombre2 < 1 || f.nombre2 > 10000) {
        F.alerter("nombre d'exemplaires hors de [1 ; 10 000] ramené");
        f.nombre1 = std::clamp(f.nombre1, 1, 10000);
        f.nombre2 = std::clamp(f.nombre2, 1, 10000);
      }
      f.pas1 = F.nombre(o, "pas1", 10.0);
      f.pas2 = F.nombre(o, "pas2", 10.0);
      f.inverse1 = F.booleen(o, "inverse1", false);
      f.inverse2 = F.booleen(o, "inverse2", false);
      if (o.contains("tableau") && o["tableau"].is_array())
        for (const auto& q : o["tableau"]) {
          double x = 0, y = 0, z = 0;
          if (F.triplet(q, x, y, z)) f.tableau.push_back({x, y, z});
        }
      f.rayon = F.nombre(o, "rayon", 1.0);
      f.distance = F.nombre(o, "distance", 1.0);
      f.epaisseur = F.nombre(o, "epaisseur", 1.5);
      f.facteurK = F.nombre(o, "facteurK", 0.0);
      f.degagement = F.texte(o, "degagement", std::string("rectangulaire"));
      f.mesure = F.texte(o, "mesure", std::string("exterieure"));
      f.positionPli = F.texte(o, "positionPli", std::string("matiere-interieure"));
      f.retraitDebut = F.nombre(o, "retraitDebut", 0.0);
      f.retraitFin = F.nombre(o, "retraitFin", 0.0);
      f.pasFiletage = F.nombre(o, "pasFiletage", 0.0);
      f.autres = LecteurJson::inconnus(o, {"id", "type", "nom", "desactivee", "visibilite", "plan", "decalage", "face", "axeX", "esquisse", "esquisseId", "enlever",
                                           "longueur", "symetrique", "inverse", "traversant", "axe", "angle", "references", "fin", "refFin", "decalageFin",
                                           "fin2", "longueur2", "refFin2", "decalageFin2", "depouille", "mince", "epaisseurMince", "sensMince", "minceArrondi", "regions", "pointsRegions", "faceChanfrein",
                                           "axeBase", "milieu", "angle2", "typeChanfrein", "distance2", "angleChanfrein", "epaisseursFaces", "mode",
                                           "supports", "xyz", "typePercage", "norme", "taille", "ajustement", "diametre", "profondeur", "longueurFiletee",
                                           "pointeForet", "planNeutre", "cheminId", "points3d", "lisse", "donnees", "source", "tube", "fermee",
                                           "typeRepetition", "sources", "direction1", "direction2", "nombre1", "nombre2", "pas1", "pas2", "inverse1",
                                           "inverse2", "tableau", "rayon", "distance", "epaisseur", "facteurK", "degagement", "mesure", "positionPli",
                                           "retraitDebut", "retraitFin", "pasFiletage"});
      p.fonctions.push_back(f);
      maxId = std::max(maxId, f.id);
    }
  p.prochainId = std::max(L.entier(j, "prochainId", 1), maxId + 1);
  p.autres = LecteurJson::inconnus(j, {"id", "nom", "uniteLongueur", "uniteAngle", "materiau", "densite", "materiauConfirme", "couleur", "couleursFaces", "reference", "proprietes", "repere", "repereSymetrique",
                                       "fournisseur", "variables", "transparence", "plans", "fonctions", "prochainId"});
  return p;
}

}  // namespace prom

namespace prom {
Unites unitesDe(const Piece& p) {
  Unites u;
  u.longueur = uniteLongueurDepuis(p.uniteLongueur);
  u.angle = uniteAngleDepuis(p.uniteAngle);
  return u;
}

std::map<std::string, Grandeur> evaluerGrandeurs(const Piece& p, std::string* erreur) {
  // Chaque variable garde sa dimension : « n = 4 » est un nombre, « a = 30° » un angle, « L = 100 mm » une
  // longueur ; un nombre seul ne devient une longueur du document qu'à l'usage, dans une cote (ES-15)
  std::map<std::string, Grandeur> vars;
  const Unites u = unitesDe(p);
  for (const auto& [nom, expr] : p.variables) {
    Grandeur g;
    std::string err;
    if (!lireGrandeur(expr, u, g, &err, &vars)) { if (erreur && erreur->empty()) *erreur = nom + " : " + err; continue; }
    vars[nom] = g;
  }
  return vars;
}

std::map<std::string, double> evaluerVariables(const Piece& p, std::string* erreur) {
  std::map<std::string, double> r;
  for (const auto& [nom, g] : evaluerGrandeurs(p, erreur)) r[nom] = g.valeur;
  return r;
}

bool appliquerVariables(Piece& p, std::string* erreur) {
  const auto vars = evaluerGrandeurs(p, erreur);
  const Unites u = unitesDe(p);  // unité d'angle comprise (ES-16)
  bool change = false;
  for (auto& f : p.fonctions) {
    if (f.type != TypeFonction::Esquisse) continue;
    bool ici = false;
    for (auto& c : f.esquisse.contraintes) {
      if (c.expression.empty()) continue;
      double v = 0;
      std::string err;
      if (!lireValeur(c.expression, c.type == TypeContrainte::Angle, u, v, &err, &vars)) { if (erreur && erreur->empty()) *erreur = err; continue; }
      const double nv = valeurReecrite(c, v);
      if (std::abs(nv - c.valeur) > 1e-12) { c.valeur = nv; ici = true; }
    }
    if (ici) {
      change = true;
      // Une variable qui rend l'esquisse impossible est signalée (ES-17)
      const EtatEsquisse etat = resoudre(f.esquisse);
      if (!etat.resolue && erreur && erreur->empty()) *erreur = "« " + f.nom + " » : " + etat.message;
    }
  }
  return change;
}
}  // namespace prom

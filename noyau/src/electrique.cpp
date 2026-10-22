// Prométhée : CAO électrique — folios, pose des appareils et de leurs contacts, repérage, renvois croisés,
// numérotation des fils, propriétés des conducteurs. Licence GPL-3.0-only.
#include "promethee/electrique.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "electrique_interne.hpp"

namespace prom::elec {

using ecad::Boite;
using ecad::ComposantElec;
using ecad::ConceptionElec;
using ecad::DefSymbole;
using ecad::Feuille;
using ecad::NM_PAR_MM;
using ecad::nouvelUuid;
using ecad::SymbolePlace;
using detail::majuscules;

// ---------- Folios ----------
const char* nomTypeFolio(TypeFolio t) {
  switch (t) {
    case TypeFolio::Garde: return "Page de garde";
    case TypeFolio::Puissance: return "Puissance";
    case TypeFolio::Commande: return "Commande";
    case TypeFolio::Automate: return "Automate";
    case TypeFolio::Distribution: return "Distribution";
    case TypeFolio::Unifilaire: return "Schéma unifilaire";
    case TypeFolio::Borniers: return "Borniers";
    case TypeFolio::Implantation: return "Implantation";
    case TypeFolio::Nomenclature: return "Nomenclature";
    case TypeFolio::Grafcet: return "Grafcet";
    case TypeFolio::Libre: break;
  }
  return "Folio";
}
const char* codeTypeFolio(TypeFolio t) {
  switch (t) {
    case TypeFolio::Garde: return "garde";
    case TypeFolio::Puissance: return "puissance";
    case TypeFolio::Commande: return "commande";
    case TypeFolio::Automate: return "automate";
    case TypeFolio::Distribution: return "distribution";
    case TypeFolio::Unifilaire: return "unifilaire";
    case TypeFolio::Borniers: return "borniers";
    case TypeFolio::Implantation: return "implantation";
    case TypeFolio::Nomenclature: return "nomenclature";
    case TypeFolio::Grafcet: return "grafcet";
    case TypeFolio::Libre: break;
  }
  return "libre";
}
bool folioDessineDOffice(TypeFolio t) {
  return t == TypeFolio::Garde || t == TypeFolio::Borniers || t == TypeFolio::Implantation || t == TypeFolio::Nomenclature || t == TypeFolio::Grafcet;
}
TypeFolio typeFolioDepuis(const std::string& code) {
  for (TypeFolio t : {TypeFolio::Garde, TypeFolio::Puissance, TypeFolio::Commande, TypeFolio::Automate, TypeFolio::Distribution, TypeFolio::Unifilaire, TypeFolio::Borniers,
                      TypeFolio::Implantation, TypeFolio::Nomenclature, TypeFolio::Grafcet})
    if (code == codeTypeFolio(t)) return t;
  return TypeFolio::Libre;
}

CadreFolio cadreFolio(const std::string& format, int colonnes, int lignes) {
  CadreFolio c;
  c.feuille = ecad::cadreFeuille(format);
  c.colonnes = std::clamp(colonnes, 1, 40);
  c.lignes = std::clamp(lignes, 1, 26);
  const Nm m = 10 * NM_PAR_MM, bande = 5 * NM_PAR_MM, hc = 25 * NM_PAR_MM;
  c.cartouche = {c.feuille.x0 + m, c.feuille.y1 - m - hc, c.feuille.x1 - m, c.feuille.y1 - m};
  c.dessin = {c.feuille.x0 + m + bande, c.feuille.y0 + m + bande, c.feuille.x1 - m - bande, c.cartouche.y0 - bande};
  c.largeurColonne = c.dessin.largeur() / c.colonnes;
  c.hauteurLigne = c.dessin.hauteur() / c.lignes;
  return c;
}

std::string PositionFolio::texte() const { return std::to_string(folio) + "." + std::to_string(colonne); }
std::string PositionFolio::texteComplet() const { return texte() + std::string(1, static_cast<char>('A' + std::clamp(ligne, 0, 25))); }

PositionFolio position(const ProjetElectrique& p, std::size_t feuille, Pt q) {
  PositionFolio r;
  r.folio = static_cast<int>(feuille) + 1;
  const std::string format = feuille < p.schema.feuilles.size() ? p.schema.feuilles[feuille].format : p.reglages.format;
  const CadreFolio c = cadreFolio(format, p.reglages.colonnes, p.reglages.lignes);
  if (c.largeurColonne > 0) r.colonne = std::clamp(static_cast<int>((q.x - c.dessin.x0) / c.largeurColonne) + 1, 1, c.colonnes);
  if (c.hauteurLigne > 0) r.ligne = std::clamp(static_cast<int>((q.y - c.dessin.y0) / c.hauteurLigne), 0, c.lignes - 1);
  return r;
}

const InfosFolio& ProjetElectrique::folio(const std::string& feuille) const {
  static const InfosFolio defaut;
  const auto it = folios.find(feuille);
  return it == folios.end() ? defaut : it->second;
}

// ---------- Révisions ----------
std::string indiceSuivant(const std::string& indice) {
  if (indice.empty()) return "A";
  std::string r = indice;
  // Comme un compteur en lettres : A … Z, puis AA, AB… ; un indice qui n'est pas en lettres reçoit un suffixe
  for (std::size_t i = r.size(); i-- > 0;) {
    const char c = r[i];
    if (c >= 'A' && c < 'Z') {
      r[i] = static_cast<char>(c + 1);
      return r;
    }
    if (c == 'Z') {
      r[i] = 'A';
      if (i == 0) return "A" + r;
      continue;
    }
    return indice + "A";
  }
  return r;
}

std::string revisionCourante(const ProjetElectrique& p) {
  if (!p.revisions.empty()) return p.revisions.back().indice;
  return p.schema.cartouche.revision;
}

const RevisionProjet& nouvelleRevision(ProjetElectrique& p, const std::string& date, const std::string& description) {
  RevisionProjet r;
  r.indice = p.revisions.empty() ? (p.schema.cartouche.revision.empty() ? std::string("A") : p.schema.cartouche.revision) : indiceSuivant(p.revisions.back().indice);
  r.date = date;
  r.auteur = !p.reglages.auteur.empty() ? p.reglages.auteur : p.schema.cartouche.auteur;
  r.description = description;
  p.revisions.push_back(r);
  p.schema.cartouche.revision = r.indice;
  p.schema.cartouche.date = date;
  return p.revisions.back();
}

const std::vector<std::pair<std::string, std::string>>& couleursConducteurs() {
  static const std::vector<std::pair<std::string, std::string>> v = {
      {"BK", "noir"},  {"BN", "brun"},   {"GY", "gris"},       {"LBU", "bleu clair"}, {"GNYE", "vert-jaune"}, {"RD", "rouge"}, {"DBU", "bleu foncé"},
      {"BU", "bleu"},  {"WH", "blanc"},  {"OG", "orange"},     {"YE", "jaune"},       {"GN", "vert"},         {"VT", "violet"}, {"PK", "rose"},
      {"TQ", "turquoise"}, {"WHBU", "blanc-bleu"}};
  return v;
}

std::vector<std::string> reperesConducteurs(int nombre, bool protection, bool numerotes) {
  std::vector<std::string> r;
  nombre = std::clamp(nombre, 1, 61);
  if (numerotes) {
    for (int i = 1; i <= nombre - (protection ? 1 : 0); ++i) r.push_back(std::to_string(i));
    if (protection) r.push_back("GNYE");
    return r;
  }
  // HD 308 : 3G : BN BU GNYE ; 4G : BN BK GY GNYE ; 5G : BN BK GY BU GNYE ; sans protection : BN BU, BN BK GY…
  static const std::map<int, std::vector<std::string>> avecPE = {{2, {"BN", "GNYE"}},
                                                                 {3, {"BN", "BU", "GNYE"}},
                                                                 {4, {"BN", "BK", "GY", "GNYE"}},
                                                                 {5, {"BN", "BK", "GY", "BU", "GNYE"}}};
  static const std::map<int, std::vector<std::string>> sansPE = {{1, {"BK"}}, {2, {"BN", "BU"}}, {3, {"BN", "BK", "GY"}}, {4, {"BU", "BN", "BK", "GY"}}, {5, {"BU", "BN", "BK", "GY", "BK"}}};
  const auto& table = protection ? avecPE : sansPE;
  if (const auto it = table.find(nombre); it != table.end()) return it->second;
  return reperesConducteurs(nombre, protection, true);
}

const char* nomPiece(Piece p) {
  switch (p) {
    case Piece::Sejour: return "Séjour";
    case Piece::Chambre: return "Chambre";
    case Piece::Cuisine: return "Cuisine";
    case Piece::SalleDeBain: return "Salle d'eau";
    case Piece::WC: return "WC";
    case Piece::Entree: return "Entrée";
    case Piece::Couloir: return "Couloir";
    case Piece::Garage: return "Garage";
    case Piece::Buanderie: return "Buanderie";
    case Piece::Bureau: return "Bureau";
    case Piece::Exterieur: return "Extérieur";
    case Piece::Autre: break;
  }
  return "Pièce";
}

ProjetElectrique nouveauProjetElectrique(const std::string& nom, bool foliosDeDepart) {
  ProjetElectrique p;
  p.uuid = nouvelUuid();
  p.nom = nom.empty() ? std::string("Projet électrique") : nom;
  p.schema = ecad::nouvelleConception(p.nom);
  p.schema.feuilles.clear();
  p.schema.cartouche.titre = p.nom;
  if (foliosDeDepart) {
    ajouterFolio(p, TypeFolio::Garde, "Page de garde");
    ajouterFolio(p, TypeFolio::Puissance, "Puissance");
    ajouterFolio(p, TypeFolio::Commande, "Commande");
  } else {
    ajouterFolio(p, TypeFolio::Libre, "Folio 1");
  }
  return p;
}

std::size_t ajouterFolio(ProjetElectrique& p, TypeFolio type, const std::string& titre, std::size_t position) {
  Feuille f;
  f.uuid = nouvelUuid();
  f.nom = titre.empty() ? std::string(nomTypeFolio(type)) : titre;
  f.format = p.reglages.format.empty() ? std::string("A3") : p.reglages.format;
  const std::size_t i = std::min(position, p.schema.feuilles.size());
  p.schema.feuilles.insert(p.schema.feuilles.begin() + static_cast<std::ptrdiff_t>(i), f);
  p.folios[f.uuid] = {type, {}, {}, {}};
  return i;
}

// ---------- Appareils ----------
namespace {

// Numéro qui suit la lettre dans un repère (« KM12 » → 12 pour « KM »), −1 sinon
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

int prochainNumero(const ConceptionElec& c, const std::string& prefixe, const std::string& sauf = {}) {
  int m = 0;
  for (const auto& k : c.composants)
    if (k.uuid != sauf) m = std::max(m, numeroApres(k.reference, prefixe));
  return m + 1;
}

const DefSymbole* definition(ProjetElectrique& p, const std::string& id) {
  if (const DefSymbole* d = p.schema.symbole(id)) return d;
  const DefSymbole* d = symboleElectrique(id);
  if (d) p.schema.symboles[id] = *d;  // version verrouillée dans le projet
  return d ? &p.schema.symboles[id] : nullptr;
}

void appliquerCatalogue(ComposantElec& k, const EntreeCatalogue& e) {
  k.champs["catalogue"] = e.id;
  k.description = e.designation;
  k.valeur = e.designation;
  if (e.fabricant != "Générique") k.fabricant = e.fabricant;
  if (!e.reference.empty()) k.mpn = e.reference;
  auto nombre = [](double v) { return detail::nombreFr(v); };
  if (e.calibre > 0) k.champs["calibre"] = nombre(e.calibre);
  if (!e.courbe.empty()) k.champs["courbe"] = e.courbe;
  if (e.sensibilite > 0) k.champs["sensibilite"] = nombre(e.sensibilite);
  if (!e.typeDifferentiel.empty()) k.champs["typeDifferentiel"] = e.typeDifferentiel;
  if (e.tensionBobine > 0) k.champs["tension"] = nombre(e.tensionBobine) + (e.bobineContinue ? " V continu" : " V");
  if (e.puissance > 0) k.champs["puissance"] = nombre(e.puissance);
}

}  // namespace

std::string repereAffiche(const ProjetElectrique& p, const ComposantElec& c) { return (p.reglages.tiret ? "-" : "") + c.reference; }

std::string caracteristiques(const ComposantElec& c) {
  auto champ = [&](const char* n) { return detail::champ(c, n); };
  // Texte composé par un calcul (chaîne de modules, onduleur…) : montré tel quel
  if (const std::string a = champ("affichage"); !a.empty()) return a;
  std::string r;
  auto ajouter = [&](const std::string& t) {
    if (!t.empty()) r += (r.empty() ? "" : " ") + t;
  };
  if (const std::string cal = champ("calibre"); !cal.empty()) ajouter(cal + " A" + (champ("courbe").empty() ? std::string() : " " + champ("courbe")));
  if (const std::string sens = champ("sensibilite"); !sens.empty())
    ajouter(sens + " mA" + (champ("typeDifferentiel").empty() || champ("typeDifferentiel") == "S" ? std::string() : " type " + champ("typeDifferentiel")));
  if (const std::string kw = champ("puissance"); !kw.empty() && c.symbole.find("bobine") == std::string::npos) ajouter(kw + " kW");
  if (const std::string u = champ("tension"); !u.empty() && r.empty()) ajouter(u);
  if (const std::string sec = champ("section"); !sec.empty()) ajouter(sec + " mm²");
  return r;
}

std::string poserAppareil(ProjetElectrique& p, std::size_t feuille, const std::string& symbole, Pt pos, int rotation, const std::string& catalogue) {
  if (feuille >= p.schema.feuilles.size()) return {};
  const DefSymbole* d = definition(p, symbole);
  if (!d) return {};
  SymbolePlace s;
  s.uuid = nouvelUuid();
  s.symbole = symbole;
  s.position = pos;
  s.rotation = ((rotation % 4) + 4) % 4;
  // Symbole graphique sans lettre de repère (départ d'unifilaire, contact posé sans son appareil) : pas d'appareil
  const bool graphique = d->prefixe.empty() && roleSymbole(symbole) != RoleSymbole::Borne;
  if (d->alimentation) {
    s.nomAlim = d->nom;
  } else if (!d->drapeau && !graphique) {
    ComposantElec k;
    k.uuid = nouvelUuid();
    k.symbole = symbole;
    // Symbole de la bibliothèque : lettre de la norme choisie ; appareil personnel ou importé : la sienne
    const std::string prefixe = symboleElectrique(symbole) ? prefixeRepere(symbole, p.reglages.norme) : d->prefixe.empty() ? std::string("A") : d->prefixe;
    k.reference = prefixe + std::to_string(prochainNumero(p.schema, prefixe));
    k.description = d->nom;
    k.valeur = d->nom;
    k.fabricant = d->fabricant;
    k.mpn = d->mpn;
    k.horsCarte = true;  // pas de circuit imprimé : rien n'est attendu côté empreinte
    if (const EntreeCatalogue* e = entreeCatalogue(catalogue)) appliquerCatalogue(k, *e);
    else if (const auto proposees = entreesPourSymbole(symbole); proposees.size() == 1) appliquerCatalogue(k, *proposees.front());
    p.schema.composants.push_back(k);
    s.composant = k.uuid;
  }
  p.schema.feuilles[feuille].symboles.push_back(s);
  return s.uuid;
}

std::map<std::string, std::string> numerosSuivants(const ProjetElectrique& p, const std::string& composant, RoleSymbole role, const std::string& symbole) {
  std::map<std::string, std::string> r;
  std::set<std::string> pris;
  for (const auto& f : p.schema.feuilles)
    for (const auto& s : f.symboles) {
      if (s.composant != composant) continue;
      const DefSymbole* d = p.schema.symbole(s.symbole);
      if (!d) d = symboleElectrique(s.symbole);
      if (!d) continue;
      for (const auto& b : d->broches) pris.insert(ecad::numeroBroche(s, b.numero));
    }
  const DefSymbole* d = p.schema.symbole(symbole);
  if (!d) d = symboleElectrique(symbole);
  if (!d) return r;
  switch (role) {
    case RoleSymbole::ContactNO:
    case RoleSymbole::ContactNF:
    case RoleSymbole::ContactInverseur:
    case RoleSymbole::ContactNOTempo:
    case RoleSymbole::ContactNFTempo: {
      // Rang des contacts auxiliaires (CEI 60947-5-1) : un rang par contact, le premier libre de 1 à 8
      std::set<int> rangs;
      for (const auto& n : pris)
        if (n.size() == 2 && std::isdigit(static_cast<unsigned char>(n[0])) && std::isdigit(static_cast<unsigned char>(n[1])) && n[1] >= '1' && n[1] <= '8') rangs.insert(n[0] - '0');
      int rang = 1;
      while (rangs.count(rang) && rang < 8) ++rang;
      for (const auto& b : d->broches) r[b.numero] = std::to_string(rang) + b.numero.substr(b.numero.size() - 1);
      break;
    }
    case RoleSymbole::Borne: {
      int m = 0;
      for (const auto& n : pris) {
        int v = 0;
        bool chiffres = !n.empty();
        for (char ch : n) {
          if (!std::isdigit(static_cast<unsigned char>(ch))) { chiffres = false; break; }
          v = v * 10 + (ch - '0');
        }
        if (chiffres) m = std::max(m, v);
      }
      for (const auto& b : d->broches) r[b.numero] = std::to_string(m + 1);
      break;
    }
    case RoleSymbole::PolesPuissance: {
      // Second jeu de pôles (rare) : décalé au-delà des bornes prises
      bool libre = true;
      for (const auto& b : d->broches) libre = libre && !pris.count(b.numero);
      if (!libre) {
        int m = 0;
        for (const auto& n : pris)
          if (n.size() == 1 && std::isdigit(static_cast<unsigned char>(n[0]))) m = std::max(m, n[0] - '0');
        int k = 0;
        for (const auto& b : d->broches) r[b.numero] = std::to_string(m + 1 + k++);
      }
      break;
    }
    default: break;
  }
  return r;
}

std::string poserElement(ProjetElectrique& p, std::size_t feuille, const std::string& symbole, Pt pos, int rotation, const std::string& composant) {
  if (feuille >= p.schema.feuilles.size() || !p.schema.composant(composant)) return {};
  const DefSymbole* d = definition(p, symbole);
  if (!d) return {};
  SymbolePlace s;
  s.uuid = nouvelUuid();
  s.symbole = symbole;
  s.composant = composant;
  s.position = pos;
  s.rotation = ((rotation % 4) + 4) % 4;
  s.numeros = numerosSuivants(p, composant, roleSymbole(symbole), symbole);
  p.schema.feuilles[feuille].symboles.push_back(s);
  return s.uuid;
}

std::string poserBorne(ProjetElectrique& p, std::size_t feuille, Pt pos, int rotation, const std::string& bornier, bool protection) {
  if (feuille >= p.schema.feuilles.size()) return {};
  const std::string symbole = protection ? "iec:borne-pe" : "iec:borne";
  std::string id = bornier;
  if (id.empty() || !p.schema.composant(id)) {
    if (!definition(p, "iec:borne")) return {};
    ComposantElec k;
    k.uuid = nouvelUuid();
    k.symbole = "iec:borne";
    const std::string prefixe = prefixeRepere("iec:borne", p.reglages.norme);
    k.reference = prefixe + std::to_string(prochainNumero(p.schema, prefixe));
    k.description = "Bornier";
    k.valeur = "Bornier";
    k.horsCarte = true;
    k.champs["catalogue"] = "x-2,5";
    p.schema.composants.push_back(k);
    id = k.uuid;
  }
  return poserElement(p, feuille, symbole, pos, rotation, id);
}

std::string poserPotentiel(ProjetElectrique& p, std::size_t feuille, const std::string& nom, Pt pos, int rotation) {
  if (feuille >= p.schema.feuilles.size() || nom.empty()) return {};
  ecad::Etiquette e;
  e.uuid = nouvelUuid();
  e.texte = nom;
  e.p = pos;
  e.rotation = ((rotation % 4) + 4) % 4;
  e.type = ecad::TypeEtiquette::Globale;
  p.schema.feuilles[feuille].etiquettes.push_back(e);
  return e.uuid;
}

bool choisirReference(ProjetElectrique& p, const std::string& composant, const std::string& catalogue) {
  ComposantElec* k = p.schema.composant(composant);
  if (!k) return false;
  if (catalogue.empty()) {
    k->champs.erase("catalogue");
    return true;
  }
  const EntreeCatalogue* e = entreeCatalogue(catalogue);
  if (!e) return false;
  for (const char* c : {"calibre", "courbe", "sensibilite", "typeDifferentiel", "tension", "puissance"}) k->champs.erase(c);
  appliquerCatalogue(*k, *e);
  return true;
}

bool rattacher(ProjetElectrique& p, const std::string& symbole, const std::string& composant) {
  if (!p.schema.composant(composant)) return false;
  for (auto& f : p.schema.feuilles)
    for (auto& s : f.symboles) {
      if (s.uuid != symbole) continue;
      if (s.composant == composant) return true;
      const std::string ancien = s.composant;
      s.composant.clear();
      s.numeros.clear();
      s.numeros = numerosSuivants(p, composant, roleSymbole(s.symbole), s.symbole);
      s.composant = composant;
      // Ancien appareil devenu sans pose (contact posé seul) : il disparaît
      bool encore = ancien.empty();
      for (const auto& g : p.schema.feuilles)
        for (const auto& x : g.symboles) encore = encore || x.composant == ancien;
      if (!encore) supprimerAppareil(p, ancien);
      return true;
    }
  return false;
}

void supprimerAppareil(ProjetElectrique& p, const std::string& composant) {
  if (composant.empty()) return;
  for (auto& f : p.schema.feuilles)
    f.symboles.erase(std::remove_if(f.symboles.begin(), f.symboles.end(), [&](const SymbolePlace& s) { return s.composant == composant; }), f.symboles.end());
  p.schema.composants.erase(std::remove_if(p.schema.composants.begin(), p.schema.composants.end(), [&](const ComposantElec& c) { return c.uuid == composant; }),
                            p.schema.composants.end());
  for (auto it = p.schema.nomsReseaux.begin(); it != p.schema.nomsReseaux.end();)
    it = it->first.rfind(composant + ":", 0) == 0 ? p.schema.nomsReseaux.erase(it) : std::next(it);
  for (auto& a : p.armoires)
    a.implantations.erase(std::remove_if(a.implantations.begin(), a.implantations.end(), [&](const Implantation& im) { return im.composant == composant; }), a.implantations.end());
  for (auto& f : p.faisceaux)
    for (auto& n : f.noeuds)
      if (n.composant == composant) n.composant.clear();
}

bool supprimerFolio(ProjetElectrique& p, std::size_t folio) {
  if (folio >= p.schema.feuilles.size() || p.schema.feuilles.size() < 2) return false;
  // Appareils posés sur ce folio : ceux qui n'ont plus aucune autre pose partent avec lui (un appareil ajouté
  // directement à l'armoire, sans pose, reste)
  std::set<std::string> ici;
  for (const auto& s : p.schema.feuilles[folio].symboles)
    if (!s.composant.empty()) ici.insert(s.composant);
  p.folios.erase(p.schema.feuilles[folio].uuid);
  p.schema.feuilles.erase(p.schema.feuilles.begin() + static_cast<std::ptrdiff_t>(folio));
  for (const auto& f : p.schema.feuilles)
    for (const auto& s : f.symboles) ici.erase(s.composant);
  for (const auto& id : ici) supprimerAppareil(p, id);
  return true;
}

namespace detail {

std::size_t retirerFoliosCalcules(ProjetElectrique& p, const std::string& origine) {
  std::vector<std::string> anciens;
  std::size_t position = p.schema.feuilles.size();
  for (std::size_t f = 0; f < p.schema.feuilles.size(); ++f)
    if (p.folio(p.schema.feuilles[f].uuid).origine == origine) {
      anciens.push_back(p.schema.feuilles[f].uuid);
      position = std::min(position, f);
    }
  if (!anciens.empty() && anciens.size() == p.schema.feuilles.size()) ajouterFolio(p, TypeFolio::Libre);  // provisoire
  for (const auto& uuid : anciens)
    for (std::size_t f = 0; f < p.schema.feuilles.size(); ++f)
      if (p.schema.feuilles[f].uuid == uuid) {
        supprimerFolio(p, f);
        break;
      }
  position = std::min(position, p.schema.feuilles.size());
  // Folio vierge (projet né pour ce calcul, ou le provisoire) : les folios calculés prennent sa place
  if (p.schema.feuilles.size() == 1) {
    const auto& f0 = p.schema.feuilles.front();
    if (f0.symboles.empty() && f0.fils.empty() && f0.textes.empty() && f0.etiquettes.empty() && p.folio(f0.uuid).type != TypeFolio::Garde) {
      p.folios.erase(f0.uuid);
      p.schema.feuilles.clear();
      position = 0;
    }
  }
  return position;
}

}  // namespace detail

std::vector<std::string> appareilsPour(const ProjetElectrique& p, RoleSymbole role) {
  std::vector<const ComposantElec*> v;
  static const std::set<std::string> sansContact = {"iec:moteur-3", "iec:moteur-3-yd", "iec:moteur-1", "iec:moteur-cc", "iec:voyant", "iec:lampe", "iec:sonore", "iec:gyrophare",
                                                    "iec:resistance-chauffante", "iec:ventilateur", "iec:prise", "iec:transfo-1", "iec:transfo-3", "iec:alim-dc", "iec:batterie",
                                                    "iec:compteur", "iec:amperemetre", "iec:voltmetre", "iec:fusible", "iec:parafoudre", "iec:u-depart"};
  for (const auto& c : p.schema.composants) {
    const std::string& s = c.symbole;
    bool ok = false;
    switch (role) {
      case RoleSymbole::Borne: ok = roleSymbole(s) == RoleSymbole::Borne; break;
      case RoleSymbole::PolesPuissance: ok = s == "iec:bobine-km"; break;
      case RoleSymbole::ContactThermiqueNF:
      case RoleSymbole::ContactThermiqueNO: ok = s == "iec:relais-thermique-3p"; break;
      case RoleSymbole::ContactNO:
      case RoleSymbole::ContactNF:
      case RoleSymbole::ContactInverseur:
      case RoleSymbole::ContactNOTempo:
      case RoleSymbole::ContactNFTempo: ok = roleSymbole(s) == RoleSymbole::Principal && !sansContact.count(s); break;
      default: break;
    }
    if (ok) v.push_back(&c);
  }
  // Bobines d'abord pour les contacts, puis ordre des repères
  std::stable_sort(v.begin(), v.end(), [](const ComposantElec* a, const ComposantElec* b) {
    const bool ba = a->symbole.find("bobine") != std::string::npos, bb = b->symbole.find("bobine") != std::string::npos;
    if (ba != bb) return ba;
    return ecad::ordreNaturel(a->reference, b->reference);
  });
  std::vector<std::string> r;
  for (const auto* c : v) r.push_back(c->uuid);
  return r;
}

// ---------- Repérage ----------
namespace {
struct Lieu {
  std::size_t feuille = 0;
  int colonne = 0;
  Nm y = 0, x = 0;
  bool place = false;
  friend bool operator<(const Lieu& a, const Lieu& b) { return std::tie(a.place, a.feuille, a.colonne, a.y, a.x) < std::tie(b.place, b.feuille, b.colonne, b.y, b.x); }
};
std::map<std::string, Lieu> premiersLieux(const ProjetElectrique& p) {
  std::map<std::string, Lieu> lieux;
  for (std::size_t f = 0; f < p.schema.feuilles.size(); ++f)
    for (const auto& s : p.schema.feuilles[f].symboles) {
      if (s.composant.empty()) continue;
      const PositionFolio pos = position(p, f, s.position);
      Lieu l{f, pos.colonne, s.position.y, s.position.x, true};
      const auto it = lieux.find(s.composant);
      // « place » vrai pour tous : on compare donc d'abord le folio
      if (it == lieux.end() || std::tie(l.feuille, l.colonne, l.y, l.x) < std::tie(it->second.feuille, it->second.colonne, it->second.y, it->second.x)) lieux[s.composant] = l;
    }
  return lieux;
}
}  // namespace

int annoterElectrique(ProjetElectrique& p, bool tout) {
  const auto lieux = premiersLieux(p);
  std::vector<ComposantElec*> ordre;
  for (auto& c : p.schema.composants) ordre.push_back(&c);
  std::stable_sort(ordre.begin(), ordre.end(), [&](const ComposantElec* a, const ComposantElec* b) {
    const auto ia = lieux.find(a->uuid), ib = lieux.find(b->uuid);
    const Lieu la = ia == lieux.end() ? Lieu{static_cast<std::size_t>(-1), 0, 0, 0, false} : ia->second;
    const Lieu lb = ib == lieux.end() ? Lieu{static_cast<std::size_t>(-1), 0, 0, 0, false} : ib->second;
    if (la.place != lb.place) return la.place;
    return std::tie(la.feuille, la.colonne, la.y, la.x) < std::tie(lb.feuille, lb.colonne, lb.y, lb.x);
  });
  std::map<std::string, std::set<int>> pris;  // lettre → numéros gardés
  std::vector<std::pair<ComposantElec*, std::string>> aNumeroter;
  for (ComposantElec* c : ordre) {
    const std::string prefixe = detail::prefixeComposant(p, *c, tout);
    const int n = numeroApres(c->reference, prefixe);
    const bool garde = !tout && n > 0 && c->reference.find('?') == std::string::npos && !pris[prefixe].count(n);
    if (garde) pris[prefixe].insert(n);
    else aNumeroter.push_back({c, prefixe});
  }
  int changes = 0;
  std::map<std::string, int> suivant;
  for (auto& [c, prefixe] : aNumeroter) {
    int& n = suivant[prefixe];
    if (n == 0) n = 1;
    while (pris[prefixe].count(n)) ++n;
    const std::string ref = prefixe + std::to_string(n);
    pris[prefixe].insert(n);
    if (c->reference != ref) {
      c->reference = ref;
      ++changes;
    }
  }
  return changes;
}

Renvois renvois(const ProjetElectrique& p) {
  Renvois r;
  for (std::size_t f = 0; f < p.schema.feuilles.size(); ++f) {
    const Feuille& F = p.schema.feuilles[f];
    for (const auto& s : F.symboles) {
      ElementRenvoi e;
      e.symbole = s.uuid;
      e.definition = s.symbole;
      e.feuille = f;
      e.ou = position(p, f, s.position);
      e.role = roleSymbole(s.symbole);
      // Symbole principal de son appareil (bobine, corps), y compris pour un symbole importé
      if (!s.composant.empty())
        if (const ComposantElec* x = p.schema.composant(s.composant); x && x->symbole == s.symbole && e.role != RoleSymbole::Borne) e.role = RoleSymbole::Principal;
      if (const DefSymbole* d = detail::definitionDe(p, s.symbole)) {
        std::vector<std::string> numeros;
        for (const auto& bp : ecad::brochesPlacees(s, *d))
          if (std::find(numeros.begin(), numeros.end(), bp.numero) == numeros.end()) numeros.push_back(bp.numero);
        if (e.role == RoleSymbole::PolesPuissance) {
          std::string t;
          for (std::size_t i = 0; i + 1 < numeros.size(); i += 2) t += (t.empty() ? "" : " ") + numeros[i] + "-" + numeros[i + 1];
          e.bornes = t;
        } else {
          for (const auto& n : numeros) e.bornes += (e.bornes.empty() ? "" : "-") + n;
        }
      }
      r.parSymbole[s.uuid] = e;
      if (!s.composant.empty()) r.parComposant[s.composant].push_back(e);
    }
    for (const auto& e : F.etiquettes) {
      if (e.type != ecad::TypeEtiquette::Globale) continue;
      ElementRenvoi x;
      x.symbole = e.uuid;
      x.feuille = f;
      x.ou = position(p, f, e.p);
      x.role = RoleSymbole::Renvoi;
      r.parPotentiel[e.texte].push_back(x);
    }
  }
  for (auto& [c, v] : r.parComposant)
    std::stable_sort(v.begin(), v.end(), [](const ElementRenvoi& a, const ElementRenvoi& b) {
      if ((a.role == RoleSymbole::Principal) != (b.role == RoleSymbole::Principal)) return a.role == RoleSymbole::Principal;
      return std::tie(a.feuille, a.ou.colonne, a.ou.ligne) < std::tie(b.feuille, b.ou.colonne, b.ou.ligne);
    });
  return r;
}

// ---------- Fils ----------
int numeroterFils(ProjetElectrique& p, bool tout) {
  ecad::ConceptionElec& c = p.schema;
  auto cle = [](const ecad::NoeudBroche& b) { return b.composant + ":" + b.broche; };
  if (tout) {
    // Les potentiels nommés (étiquettes, terre) gardent leur nom : on ne retire que les numéros
    const ecad::Connectivite k0 = ecad::connectivite(c);
    for (const auto& r : k0.reseaux)
      if (!r.nomme)
        for (const auto& b : r.broches) c.nomsReseaux.erase(cle(b));
  }
  const ecad::Connectivite k = ecad::connectivite(c);
  // Premier point de chaque réseau dans l'ordre de lecture (folio, colonne, hauteur)
  std::map<int, std::tuple<std::size_t, int, Nm, Nm>> premier;
  auto noter = [&](int reseau, std::size_t f, Pt q) {
    const PositionFolio pos = position(p, f, q);
    const auto t = std::make_tuple(f, pos.colonne, q.y, q.x);
    const auto it = premier.find(reseau);
    if (it == premier.end() || t < it->second) premier[reseau] = t;
  };
  for (std::size_t f = 0; f < c.feuilles.size(); ++f) {
    for (const auto& w : c.feuilles[f].fils)
      if (const auto it = k.parFil.find(w.uuid); it != k.parFil.end()) {
        noter(it->second, f, w.a);
        noter(it->second, f, w.b);
      }
    for (const auto& s : c.feuilles[f].symboles) {
      const DefSymbole* d = c.symbole(s.symbole);
      if (!d || s.composant.empty()) continue;
      for (const auto& bp : ecad::brochesPlacees(s, *d))
        if (const auto it = k.parBroche.find({s.composant, bp.numero}); it != k.parBroche.end()) noter(it->second, f, bp.p);
    }
  }
  // Numéros déjà donnés (gardés quand on ne renumérote pas tout)
  std::set<std::string> existants;
  int maxSeq = 0;
  std::map<int, int> maxParFolio;
  for (const auto& [b, nom] : c.nomsReseaux) {
    existants.insert(nom);
    const auto point = nom.find('.');
    try {
      if (point == std::string::npos) maxSeq = std::max(maxSeq, std::stoi(nom));
      else maxParFolio[std::stoi(nom.substr(0, point))] = std::max(maxParFolio[std::stoi(nom.substr(0, point))], std::stoi(nom.substr(point + 1)));
    } catch (...) {
    }
  }
  std::vector<int> aNumeroter;
  for (std::size_t i = 0; i < k.reseaux.size(); ++i) {
    const auto& r = k.reseaux[i];
    if (r.nomme || r.broches.empty() || r.broches.size() < 2) continue;
    bool deja = false;
    for (const auto& b : r.broches) deja = deja || c.nomsReseaux.count(cle(b));
    if (deja) {
      // Potentiel déjà numéroté : les broches qui l'ont rejoint depuis prennent son numéro (il ne bouge plus)
      if (!detail::nomAutomatique(r.nom))
        for (const auto& b : r.broches) c.nomsReseaux.emplace(cle(b), r.nom);
      continue;
    }
    aNumeroter.push_back(static_cast<int>(i));
  }
  std::stable_sort(aNumeroter.begin(), aNumeroter.end(), [&](int a, int b) {
    const auto ia = premier.find(a), ib = premier.find(b);
    if (ia == premier.end() || ib == premier.end()) return ia != premier.end() && ib == premier.end();
    return ia->second < ib->second;
  });
  int compte = 0;
  for (int i : aNumeroter) {
    std::string numero;
    if (p.reglages.fils == NumerotationFils::ParFolio) {
      const int folio = premier.count(i) ? static_cast<int>(std::get<0>(premier[i])) + 1 : 0;
      do numero = std::to_string(folio) + "." + std::to_string(++maxParFolio[folio]);
      while (existants.count(numero));
    } else {
      do numero = std::to_string(++maxSeq);
      while (existants.count(numero));
    }
    existants.insert(numero);
    for (const auto& b : k.reseaux[static_cast<std::size_t>(i)].broches) c.nomsReseaux[cle(b)] = numero;
    ++compte;
  }
  return compte;
}

ClasseConducteur classeConducteur(const std::string& nom) {
  const std::string n = majuscules(nom);
  if (n == "PE" || n == "PEN" || n == "TERRE") return ClasseConducteur::Protection;
  if (n == "N") return ClasseConducteur::Neutre;
  if (n == "L" || n == "L1" || n == "L2" || n == "L3") return ClasseConducteur::Phase;
  if (n == "0V" || n == "0 V" || n == "M" || n == "-" || n == "0" || n == "-24V") return ClasseConducteur::ZeroVolt;
  if (n.find("24V") != std::string::npos || n == "L+" || n == "+" || n.find("+24") != std::string::npos || n.find("12V") != std::string::npos) return ClasseConducteur::CommandeContinue;
  return ClasseConducteur::Autre;
}

namespace {
// Le réseau touche-t-il un appareil de puissance (pôles, protections multipolaires, moteur, variateur) ? Les
// contacts auxiliaires (13-14, 95-96…), les bobines (A1-A2) et les entrées de commande d'un variateur n'en sont pas.
bool reseauDePuissance(const ProjetElectrique& p, const ecad::Reseau& r) {
  for (const auto& b : r.broches) {
    const ComposantElec* x = p.schema.composant(b.composant);
    if (!x) continue;
    const std::string& s = x->symbole;
    const std::string& n = b.broche;
    const bool auxiliaire = (n.size() == 2 && std::isdigit(static_cast<unsigned char>(n[0])) && std::isdigit(static_cast<unsigned char>(n[1]))) || n == "A1" || n == "A2" ||
                            n == "X1" || n == "X2" || n.rfind("DI", 0) == 0 || n == "COM";
    if (auxiliaire) continue;
    if (s == "iec:bobine-km") {
      // Un contacteur : ses pôles (1 à 8) sont de puissance
      if (n.size() == 1 && n[0] >= '1' && n[0] <= '8') return true;
      continue;
    }
    const bool puissance = s.find("-3p") != std::string::npos || s.find("-4p") != std::string::npos || s.find("moteur") != std::string::npos ||
                           s == "iec:variateur" || s == "iec:demarreur" || s == "iec:resistance-chauffante" || s == "iec:prise" || s == "iec:disj-1p" ||
                           s == "iec:disj-1pn" || s == "iec:disj-2p" || s == "iec:diff-2p" || s == "iec:disj-diff-2p" || s == "iec:agcp" || s == "iec:ventilateur" ||
                           s == "iec:u-disj" || s == "iec:u-diff" || s == "iec:u-agcp" || s == "iec:compteur";
    if (puissance) return true;
  }
  return false;
}
}  // namespace

ProprietesConducteur proprietesEffectives(const ProjetElectrique& p, const std::string& fil) {
  const ecad::Connectivite k = ecad::connectivite(p.schema);
  return detail::proprietes(p, k, fil);
}

// ---------- Outils communs ----------
namespace detail {

std::string nombreFr(double v, int decimales) {
  if (!std::isfinite(v)) return "0";
  char b[48];
  if (decimales >= 0) std::snprintf(b, sizeof b, "%.*f", decimales, v);
  else {
    std::snprintf(b, sizeof b, "%.6f", v);
    std::string t = b;
    if (t.find('.') != std::string::npos) {
      while (!t.empty() && t.back() == '0') t.pop_back();
      if (!t.empty() && t.back() == '.') t.pop_back();
    }
    if (t == "-0") t = "0";
    std::replace(t.begin(), t.end(), '.', ',');
    return t;
  }
  std::string t = b;
  std::replace(t.begin(), t.end(), '.', ',');
  return t;
}

double lireNombre(const std::string& s) {
  std::size_t i = 0;
  while (i < s.size() && !std::isdigit(static_cast<unsigned char>(s[i])) && !(s[i] == '-' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) ++i;
  if (i >= s.size()) return 0;
  std::string t;
  if (s[i] == '-') t += s[i++];
  bool virgule = false;
  for (; i < s.size(); ++i) {
    const char ch = s[i];
    if (std::isdigit(static_cast<unsigned char>(ch))) t += ch;
    else if ((ch == ',' || ch == '.') && !virgule && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1]))) {
      t += '.';
      virgule = true;
    } else break;
  }
  try {
    const double v = std::stod(t);
    return std::isfinite(v) ? v : 0;
  } catch (...) {
    return 0;
  }
}

std::string majuscules(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string champ(const ComposantElec& c, const std::string& nom) {
  const auto it = c.champs.find(nom);
  return it == c.champs.end() ? std::string() : it->second;
}

const DefSymbole* definitionDe(const ProjetElectrique& p, const std::string& id) {
  if (const auto it = p.schema.symboles.find(id); it != p.schema.symboles.end()) return &it->second;
  if (const DefSymbole* d = symboleElectrique(id)) return d;
  return ecad::symboleBibliotheque(id);
}

const EntreeCatalogue* catalogueDe(const ComposantElec& c) { return entreeCatalogue(champ(c, "catalogue")); }

bool estBornier(const ComposantElec& c) { return roleSymbole(c.symbole) == RoleSymbole::Borne; }

std::string prefixeComposant(const ProjetElectrique& p, const ComposantElec& c, bool normaliser) {
  if (normaliser && symboleElectrique(c.symbole)) return prefixeRepere(c.symbole, p.reglages.norme);
  // Lettres du repère actuel (« KM12 », « KM? », « -KT3 »)
  std::string lettres;
  for (char ch : c.reference) {
    if (ch == '-' && lettres.empty()) continue;
    if (std::isalpha(static_cast<unsigned char>(ch))) lettres += ch;
    else break;
  }
  if (!lettres.empty()) return lettres;
  if (symboleElectrique(c.symbole)) return prefixeRepere(c.symbole, p.reglages.norme);
  const DefSymbole* d = definitionDe(p, c.symbole);
  return d && !d->prefixe.empty() ? d->prefixe : std::string("A");
}

LieuxBroches lieuxBroches(const ProjetElectrique& p) {
  LieuxBroches r;
  std::map<std::pair<std::string, std::string>, std::tuple<std::size_t, int, Nm, Nm>> ordre;
  for (std::size_t f = 0; f < p.schema.feuilles.size(); ++f)
    for (const auto& s : p.schema.feuilles[f].symboles) {
      if (s.composant.empty()) continue;
      const DefSymbole* d = definitionDe(p, s.symbole);
      if (!d) continue;
      for (const auto& bp : ecad::brochesPlacees(s, *d)) {
        const auto cle = std::make_pair(s.composant, bp.numero);
        const auto t = std::make_tuple(f, position(p, f, bp.p).colonne, bp.p.y, bp.p.x);
        const auto it = ordre.find(cle);
        if (it == ordre.end() || t < it->second) {
          ordre[cle] = t;
          r[cle] = {f, bp.p, s.uuid};
        }
      }
    }
  return r;
}

std::string repereBroche(const ProjetElectrique& p, const std::string& composant, const std::string& broche) {
  const ComposantElec* c = p.schema.composant(composant);
  return (c ? repereAffiche(p, *c) : std::string("?")) + ":" + broche;
}

bool nomAutomatique(const std::string& nom) { return nom.rfind("N-", 0) == 0 || nom.empty(); }

ProprietesConducteur proprietes(const ProjetElectrique& p, const ecad::Connectivite& k, const std::string& fil) {
  ProprietesConducteur r;
  if (const auto it = p.conducteurs.find(fil); it != p.conducteurs.end()) r = it->second;
  ClasseConducteur classe = classeConducteur(fil);
  if (classe == ClasseConducteur::Autre) {
    const int i = k.indice(fil);
    const bool puissance = i >= 0 && reseauDePuissance(p, k.reseaux[static_cast<std::size_t>(i)]);
    classe = puissance ? ClasseConducteur::Phase : p.reglages.commandeContinue ? ClasseConducteur::CommandeContinue : ClasseConducteur::Commande;
  }
  const std::string n = majuscules(fil);
  double section = 0;
  std::string couleur, type;
  switch (classe) {
    case ClasseConducteur::Phase:
      section = 2.5;
      couleur = n == "L1" ? "BN" : n == "L2" ? "BK" : n == "L3" ? "GY" : "BK";
      type = "H07V-K";
      break;
    case ClasseConducteur::Neutre:
      section = 2.5;
      couleur = "LBU";
      type = "H07V-K";
      break;
    case ClasseConducteur::Protection:
      section = 2.5;
      couleur = "GNYE";
      type = "H07V-K";
      break;
    case ClasseConducteur::Commande:
      section = 1.0;
      couleur = "RD";
      type = "H05V-K";
      break;
    case ClasseConducteur::CommandeContinue:
    case ClasseConducteur::ZeroVolt:
      section = 0.75;
      couleur = "DBU";
      type = "H05V-K";
      break;
    case ClasseConducteur::Autre: break;
  }
  if (r.section <= 0) r.section = section;
  if (r.couleur.empty()) r.couleur = couleur;
  if (r.type.empty()) r.type = type;
  return r;
}

double calibreDe(const ComposantElec& c) {
  const double v = lireNombre(champ(c, "calibre"));
  if (v > 0) return v;
  const EntreeCatalogue* e = catalogueDe(c);
  return e ? e->calibre : 0;
}

double puissanceDe(const ComposantElec& c) {
  const std::string t = champ(c, "puissance");
  if (!t.empty()) return lireNombre(t);
  const EntreeCatalogue* e = catalogueDe(c);
  return e ? e->puissance : 0;
}

std::string documentCsv(const std::vector<std::string>& entete, const std::vector<std::vector<std::string>>& lignes) {
  auto ligne = [](const std::vector<std::string>& v) {
    std::string r;
    for (std::size_t i = 0; i < v.size(); ++i) {
      if (i) r += ';';
      const std::string& x = v[i];
      if (x.find_first_of(";\"\r\n") != std::string::npos) {
        r += '"';
        for (char ch : x) {
          if (ch == '"') r += '"';
          r += ch;
        }
        r += '"';
      } else r += x;
    }
    return r + "\r\n";
  };
  std::string r = "\xEF\xBB\xBF" + ligne(entete);
  for (const auto& l : lignes) r += ligne(l);
  return r;
}

}  // namespace detail

}  // namespace prom::elec

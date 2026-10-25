// Prométhée : reprise de l'existant (lot L10 de la 0.34 ; 10.19). Voir reprise_existant.hpp. Licence GPL-3.0-only.
#include "promethee/reprise_existant.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

#include "promethee/echanges_electronique.hpp"
#include "promethee/hachage.hpp"
#include "promethee/schema.hpp"
#include "xml_interne.hpp"

namespace prom::ecad {

namespace {
using xml_interne::NoeudXml;

std::string minuscules(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
// Clé de comparaison d'un nom de champ : minuscules ASCII, lettres et chiffres seuls
std::string cleChamp(const std::string& s) {
  std::string r;
  for (const char c : s)
    if (std::isalnum(static_cast<unsigned char>(c))) r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}
std::string sansEspacesAutour(const std::string& s) {
  const auto a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
// Identifiant d'article tiré d'un texte : lettres, chiffres, « - _ . + », le reste en « _ »
std::string identifiantDe(const std::string& s) {
  std::string r;
  for (const char c : s) {
    const auto u = static_cast<unsigned char>(c);
    r += std::isalnum(u) || c == '-' || c == '_' || c == '.' || c == '+' ? c : '_';
  }
  return r.empty() ? std::string("article") : r;
}
std::string echapper(const std::string& s) {
  std::string r;
  r.reserve(s.size());
  for (const char c : s) switch (c) {
      case '&': r += "&amp;"; break;
      case '<': r += "&lt;"; break;
      case '>': r += "&gt;"; break;
      case '"': r += "&quot;"; break;
      default: r += c;
    }
  return r;
}
std::string nombre(double v) {
  if (std::abs(v) < 5e-7) v = 0;
  char t[48];
  std::snprintf(t, sizeof t, "%.4f", v);
  std::string s = t;
  if (s.find('.') != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  return s == "-0" ? "0" : s;
}
double reel(const std::string& s, double defaut = 0) {
  const std::string t = sansEspacesAutour(s);
  if (t.empty()) return defaut;
  char* fin = nullptr;
  const double v = std::strtod(t.c_str(), &fin);
  return fin == t.c_str() ? defaut : v;
}
int entier(const std::string& s, int defaut = 0) {
  const double v = reel(s, std::numeric_limits<double>::quiet_NaN());
  return std::isnan(v) ? defaut : static_cast<int>(std::lround(v));
}
Nm nmDe(double v) { return static_cast<Nm>(std::llround(v * 1e6)); }
// Nom local d'un élément ou d'un attribut (sans préfixe d'espace de noms)
std::string local(const std::string& n) {
  const auto p = n.find(':');
  return p == std::string::npos ? n : n.substr(p + 1);
}
const NoeudXml* enfantLocal(const NoeudXml& n, const std::string& nom) {
  for (const auto& e : n.enfants)
    if (local(e.nom) == nom) return &e;
  return nullptr;
}
std::vector<const NoeudXml*> tousLocaux(const NoeudXml& n, const std::string& nom) {
  std::vector<const NoeudXml*> v;
  for (const auto& e : n.enfants)
    if (local(e.nom) == nom) v.push_back(&e);
  return v;
}
std::string texteDe(const NoeudXml& n, const std::string& nom) {
  const NoeudXml* e = enfantLocal(n, nom);
  return e ? sansEspacesAutour(e->texte) : std::string();
}
std::string attrLocal(const NoeudXml& n, const std::string& nom) {
  for (const auto& [k, v] : n.attributs)
    if (local(k) == nom) return v;
  return {};
}
const NoeudXml* chercher(const NoeudXml& n, const std::string& nom) {
  if (local(n.nom) == nom) return &n;
  for (const auto& e : n.enfants)
    if (const NoeudXml* t = chercher(e, nom)) return t;
  return nullptr;
}

// Fabricants comparables : mêmes lettres, l'un préfixe de l'autre (« Yageo » et « YAGEO Corporation »), ou noms courts
// connus (« TI » et « Texas Instruments »)
std::string fabricantNormalise(const std::string& f) {
  const std::string c = cleChamp(f);
  static const std::map<std::string, std::string> alias = {
      {"ti", "texasinstruments"}, {"adi", "analogdevices"}, {"st", "stmicroelectronics"}, {"stm", "stmicroelectronics"}, {"on", "onsemi"},
      {"onsemiconductor", "onsemi"}, {"nxpsemiconductors", "nxp"}, {"infineontechnologies", "infineon"}, {"microchiptechnology", "microchip"},
      {"renesaselectronics", "renesas"}, {"vishayintertechnology", "vishay"}, {"murataelectronics", "murata"}, {"tdkcorporation", "tdk"},
      {"kemetcorporation", "kemet"}, {"yageocorporation", "yageo"}, {"panasonicelectroniccomponents", "panasonic"}, {"samsungelectromechanics", "samsung"},
      {"wurthelektronik", "wurth"}, {"wurthelectronics", "wurth"}, {"we", "wurth"}, {"maximintegrated", "maxim"}, {"lineartechnology", "linear"}};
  const auto it = alias.find(c);
  return it == alias.end() ? c : it->second;
}
bool fabricantsCompatibles(const std::string& a, const std::string& b) {
  const std::string x = fabricantNormalise(a), y = fabricantNormalise(b);
  if (x.empty() || y.empty()) return true;
  return x.rfind(y, 0) == 0 || y.rfind(x, 0) == 0;
}

// Champs que les autres logiciels écrivent pour la référence, le fabricant, le fournisseur et le code LCSC
const std::vector<std::string>& clesMpn() {
  static const std::vector<std::string> v = {"mpn", "manufacturerpartnumber", "manufacturerpartnumber1", "mfrpartnumber", "mfrpn", "mfrno", "mfrnumber",
                                             "mfgpartnumber", "mfgpn", "manfpartnumber", "manufacturerpn", "manufacturerpartno", "mpn1", "referencefabricant",
                                             "reffabricant", "manufacturerreference"};
  return v;
}
const std::vector<std::string>& clesFabricant() {
  static const std::vector<std::string> v = {"manufacturer", "manufacturer1", "manufacturername", "mfr", "mfg", "mf", "manf", "fabricant", "hersteller", "manufacturername1"};
  return v;
}
const std::vector<std::string>& clesFournisseur() {
  static const std::vector<std::string> v = {"supplier", "supplier1", "distributor", "distributor1", "fournisseur", "vendor"};
  return v;
}
const std::vector<std::string>& clesLcsc() {
  static const std::vector<std::string> v = {"lcsc", "lcscpart", "lcscpartnumber", "lcscpn", "jlcpcbpart", "jlcpcbpartnumber"};
  return v;
}
std::string champParCles(const ComposantElec& k, const std::vector<std::string>& cles) {
  for (const std::string& c : cles)
    for (const auto& [nom, v] : k.champs)
      if (cleChamp(nom) == c) {
        const std::string t = sansEspacesAutour(v);
        if (!t.empty() && t != "~" && t != "-" && minuscules(t) != "n/a" && minuscules(t) != "na") return t;
      }
  return {};
}
bool champIdentite(const std::string& nom) {
  const std::string c = cleChamp(nom);
  for (const auto* l : {&clesMpn(), &clesFabricant(), &clesFournisseur(), &clesLcsc()})
    if (std::find(l->begin(), l->end(), c) != l->end()) return true;
  static const std::set<std::string> autres = {"footprint", "datasheet", "description", "value", "reference", "comment", "designator", "sheetname", "sheetfile",
                                               "manufacturerpartnumber2", "manufacturer2", "supplierpartnumber", "supplierpartnumber1", "componentlink1url",
                                               "componentlink1description", "simdevice", "simtype", "simpins", "simparams", "spiceprimitive", "spicemodel"};
  return autres.count(c) > 0;
}

// Article de « b » qui porte la référence (en propre ou en seconde source), du même fabricant quand les deux le disent
const Article* articleDeReference(const ContenuBibliotheque& b, const std::string& mpn, const std::string& fabricant) {
  const std::string m = mpnNormalise(mpn);
  for (const Article& a : b.articles)
    if (mpnNormalise(a.mpn) == m && fabricantsCompatibles(a.fabricant, fabricant)) return &a;
  for (const Article& a : b.articles)
    for (const SecondeSource& s : a.secondes)
      if (mpnNormalise(s.mpn) == m && fabricantsCompatibles(s.fabricant, fabricant)) return &a;
  return nullptr;
}
// Pose « objet » sous son identifiant dans « liste » : déjà là à l'identique, gardé ; un autre objet du même
// identifiant, renommé « ~2 », « ~3 »… ; rend l'identifiant retenu
template <class T> std::string ranger(std::vector<T>& liste, T objet) {
  const std::string base = objet.id;
  for (int n = 1;; ++n) {
    objet.id = n == 1 ? base : base + "~" + std::to_string(n);
    const auto it = std::find_if(liste.begin(), liste.end(), [&](const T& x) { return x.id == objet.id; });
    if (it == liste.end()) {
      liste.push_back(objet);
      return objet.id;
    }
    if (*it == objet) return objet.id;
  }
}
std::string dossierDe(const std::string& chemin) {
  const auto p = chemin.find_last_of('/');
  return p == std::string::npos ? std::string() : chemin.substr(0, p);
}
std::string nomDe(const std::string& chemin) {
  const auto p = chemin.find_last_of('/');
  return p == std::string::npos ? chemin : chemin.substr(p + 1);
}
std::string extensionDe(const std::string& n) {
  const auto p = n.find_last_of('.');
  return p == std::string::npos ? std::string() : minuscules(n.substr(p + 1));
}
std::string baseDe(const std::string& n) {
  const std::string f = nomDe(n);
  const auto p = f.find_last_of('.');
  return p == std::string::npos ? f : f.substr(0, p);
}
}  // namespace

// ---------- Rattachement des articles ----------
std::string mpnNormalise(const std::string& mpn) {
  std::string r;
  for (const char c : mpn)
    if (!std::isspace(static_cast<unsigned char>(c))) r += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return r;
}

int completerReferencesFabricant(ConceptionElec& c) {
  int n = 0;
  for (ComposantElec& k : c.composants) {
    bool change = false;
    auto completer = [&](std::string& champ, const std::vector<std::string>& cles) {
      if (!sansEspacesAutour(champ).empty()) return;
      if (std::string v = champParCles(k, cles); !v.empty()) {
        champ = std::move(v);
        change = true;
      }
    };
    completer(k.mpn, clesMpn());
    completer(k.fabricant, clesFabricant());
    completer(k.fournisseur, clesFournisseur());
    completer(k.lcsc, clesLcsc());
    if (change) ++n;
  }
  return n;
}

RattachementArticles rattacherArticles(ConceptionElec& c, ContenuBibliotheque& b, const std::string& origine) {
  RattachementArticles r;
  completerReferencesFabricant(c);
  std::set<std::string> vus;
  for (ComposantElec& k : c.composants) {
    if (!k.reference.empty() && k.reference[0] == '#') continue;  // alimentation, drapeau
    LigneRattachement l{k.reference, sansEspacesAutour(k.mpn), sansEspacesAutour(k.fabricant), {}, {}, {}};
    if (l.mpn.empty()) {
      l.etat = "sans référence";
      l.detail = "aucune référence de fabricant dans le fichier";
      ++r.sansReference;
      r.lignes.push_back(l);
      continue;
    }
    const Article* trouve = articleDeReference(b, l.mpn, l.fabricant);
    if (trouve) {
      l.etat = "rattaché";
      l.detail = mpnNormalise(trouve->mpn) == mpnNormalise(l.mpn) ? "article de la bibliothèque" : "seconde source de « " + trouve->mpn + " »";
      ++r.rattaches;
    } else {
      // Article créé d'après le composant, avec son symbole et son empreinte
      Article a;
      a.nom = l.mpn;
      a.mpn = l.mpn;
      a.fabricant = l.fabricant;
      a.description = k.description;
      a.categorie = origine.empty() ? std::string("Articles repris") : origine;
      a.valeur = k.valeur;
      a.fiche = k.fiche;
      a.lcsc = k.lcsc;
      a.hauteur = k.hauteur;
      a.parametres = k.parametres;
      for (const auto& [nom, v] : k.champs)
        if (!champIdentite(nom) && !v.empty() && v.size() <= 80 && !a.parametres.count(nom)) a.parametres[nom] = v;
      if (!k.symbole.empty()) {
        const DefSymbole* d = c.symbole(k.symbole);
        a.symbole = d && !symboleBibliotheque(k.symbole) ? ranger(b.symboles, *d) : k.symbole;
      }
      if (!k.empreinte.empty()) {
        const DefEmpreinte* e = c.empreinte(k.empreinte);
        a.empreinte = e && !empreinteEnCache(k.empreinte) ? ranger(b.empreintes, *e) : k.empreinte;
      }
      // Identifiant : la référence, sinon fabricant et référence, sinon numérotée
      std::string id = identifiantDe(l.mpn);
      auto pris = [&](const std::string& x) { return std::any_of(b.articles.begin(), b.articles.end(), [&](const Article& y) { return y.id == x; }); };
      if (pris(id) && !l.fabricant.empty()) id = identifiantDe(l.fabricant + "_" + l.mpn);
      for (int n = 2; pris(id); ++n) id = identifiantDe(l.mpn) + "~" + std::to_string(n);
      a.id = id;
      b.articles.push_back(a);
      trouve = &b.articles.back();
      l.etat = "créé";
      l.detail = "article ajouté à la bibliothèque « " + (b.nom.empty() ? std::string("d’équipe") : b.nom) + " »";
      ++r.crees;
    }
    l.article = trouve->id;
    k.article = trouve->id;
    k.versionArticle = trouve->version;
    if (k.fabricant.empty()) k.fabricant = trouve->fabricant;
    if (k.lcsc.empty()) k.lcsc = trouve->lcsc;
    if (k.fiche.empty()) k.fiche = trouve->fiche;
    c.articles[trouve->id] = *trouve;
    if (vus.insert(trouve->id).second) r.articlesUtilises.push_back(trouve->id);
    r.lignes.push_back(l);
  }
  return r;
}

// ---------- IPC/DAC-2552 ----------
namespace {
// Paramètres de l'article portés par des éléments propres de la fiche
const std::set<std::string>& parametresPhysiques() {
  static const std::set<std::string> s = {"longueur", "largeur", "masse", "temperature_min", "temperature_max", "msl", "rohs", "conditionnement", "quantite_conditionnement"};
  return s;
}
std::string montageDe(const DefEmpreinte& e) {
  const bool tht = std::any_of(e.pastilles.begin(), e.pastilles.end(), [](const Pastille& p) { return p.type == TypePastille::Traversante; });
  const bool cms = std::any_of(e.pastilles.begin(), e.pastilles.end(), [](const Pastille& p) { return p.type == TypePastille::CMS; });
  return tht && cms ? "mixed" : tht ? "THT" : "SMT";
}
// Composant de la fiche : l'élément « Component » tel qu'écrit, pour la somme de contrôle
std::string brutComposant(const std::string& xml) {
  const auto a = xml.find("<Component");
  const auto b = xml.find("</Component>", a == std::string::npos ? 0 : a);
  if (a == std::string::npos || b == std::string::npos) return {};
  return xml.substr(a, b + 12 - a);
}
}  // namespace

std::string xmlIpc2552(const Article& a, const DefEmpreinte* e, const std::string& date) {
  std::string c;
  auto ligne = [&c](int niveau, const std::string& t) { c += std::string(static_cast<std::size_t>(niveau) * 2, ' ') + t + "\n"; };
  auto element = [&](int niveau, const std::string& nom, const std::string& v) {
    if (!v.empty()) ligne(niveau, "<" + nom + ">" + echapper(v) + "</" + nom + ">");
  };
  auto param = [&a](const std::string& k) -> std::string {
    const auto it = a.parametres.find(k);
    return it == a.parametres.end() ? std::string() : it->second;
  };
  c += "  <Component id=\"" + echapper(a.id) + "\" version=\"" + std::to_string(a.version) + "\">\n";
  ligne(2, "<Identification>");
  element(3, "Manufacturer_name", a.fabricant);
  element(3, "Manufacturer_part_number", a.mpn);
  element(3, "Enterprise_part_number", a.numeroInterne);
  element(3, "Name", a.nom);
  element(3, "Description", a.description);
  element(3, "Category", a.categorie);
  element(3, "Value", a.valeur);
  element(3, "Lifecycle_status", a.etatCycle);
  element(3, "Datasheet", a.fiche);
  if (a.generique) ligne(3, "<Generic_part requirements=\"" + echapper(a.exigences) + "\"/>");
  ligne(2, "</Identification>");
  // Composition physique : boîtier, cotes, masse, modèle 3D
  ligne(2, "<Physical_composition>");
  if (!a.empreinte.empty() || e) {
    std::string p = "<Package name=\"" + echapper(a.empreinte.empty() && e ? e->id : a.empreinte) + "\"";
    if (e) p += " mounting=\"" + montageDe(*e) + "\" terminals=\"" + std::to_string(e->pastilles.size()) + "\"";
    ligne(3, p + "/>");
  }
  std::string longueur = param("longueur"), largeur = param("largeur");
  if (longueur.empty() && e && e->corps.largeur > 0) longueur = nombre(mm(e->corps.largeur));
  if (largeur.empty() && e && e->corps.profondeur > 0) largeur = nombre(mm(e->corps.profondeur));
  const double hauteur = a.hauteur > 0 ? a.hauteur : e && e->corps.hauteur > 0 ? mm(e->corps.hauteur) : 0;
  if (!longueur.empty() || !largeur.empty() || hauteur > 0) {
    ligne(3, "<Size_and_tolerance>");
    if (!longueur.empty()) ligne(4, "<Body_length unit=\"mm\" nominal=\"" + echapper(longueur) + "\"/>");
    if (!largeur.empty()) ligne(4, "<Body_width unit=\"mm\" nominal=\"" + echapper(largeur) + "\"/>");
    if (hauteur > 0) ligne(4, "<Seated_height unit=\"mm\" nominal=\"" + nombre(hauteur) + "\"/>");
    ligne(3, "</Size_and_tolerance>");
  }
  if (const std::string m = param("masse"); !m.empty()) ligne(3, "<Weight unit=\"g\">" + echapper(m) + "</Weight>");
  element(3, "Model_3d", a.modele3D);
  ligne(2, "</Physical_composition>");
  // Propriétés électriques : les paramètres qui n'ont pas d'élément propre
  bool ouvert = false;
  for (const auto& [k, v] : a.parametres) {
    if (parametresPhysiques().count(k)) continue;
    if (!ouvert) ligne(2, "<Electrical_properties>");
    ouvert = true;
    ligne(3, "<Property name=\"" + echapper(k) + "\" value=\"" + echapper(v) + "\"/>");
  }
  if (ouvert) ligne(2, "</Electrical_properties>");
  // Environnement et conditionnement
  const std::string tmin = param("temperature_min"), tmax = param("temperature_max"), msl = param("msl"), rohs = param("rohs");
  if (!tmin.empty() || !tmax.empty() || !msl.empty() || !rohs.empty()) {
    ligne(2, "<Environment_info>");
    if (!tmin.empty() || !tmax.empty())
      ligne(3, "<Operating_temperature unit=\"C\"" + (tmin.empty() ? std::string() : " min=\"" + echapper(tmin) + "\"") + (tmax.empty() ? std::string() : " max=\"" + echapper(tmax) + "\"") + "/>");
    element(3, "Moisture_sensitivity_level", msl);
    element(3, "RoHS", rohs);
    ligne(2, "</Environment_info>");
  }
  if (const std::string cond = param("conditionnement"), qte = param("quantite_conditionnement"); !cond.empty() || !qte.empty()) {
    ligne(2, "<Packing>");
    element(3, "Packaging", cond);
    element(3, "Quantity", qte);
    ligne(2, "</Packing>");
  }
  // Fournisseurs (le code LCSC compris) et secondes sources
  if (!a.lcsc.empty() || !a.offres.empty()) {
    ligne(2, "<Supplier_info>");
    if (!a.lcsc.empty()) ligne(3, "<Supplier name=\"LCSC\" part_number=\"" + echapper(a.lcsc) + "\" role=\"catalogue\"/>");
    for (const OffreArticle& o : a.offres) {
      std::string s = "<Supplier name=\"" + echapper(o.distributeur) + "\" part_number=\"" + echapper(o.reference) + "\" currency=\"" + echapper(o.devise) + "\" minimum_pack=\"" +
                      std::to_string(o.conditionnement) + "\"" + (o.stock >= 0 ? " stock=\"" + std::to_string(o.stock) + "\"" : std::string());
      if (o.prix.empty()) {
        ligne(3, s + "/>");
        continue;
      }
      ligne(3, s + ">");
      for (const auto& [q, p] : o.prix) ligne(4, "<Price_break quantity=\"" + std::to_string(q) + "\" unit_price=\"" + nombre(p) + "\"/>");
      ligne(3, "</Supplier>");
    }
    ligne(2, "</Supplier_info>");
  }
  for (const SecondeSource& s : a.secondes)
    ligne(2, "<Alternate_source manufacturer=\"" + echapper(s.fabricant) + "\" part_number=\"" + echapper(s.mpn) + "\" lifecycle=\"" + echapper(s.cycleVie) + "\" rank=\"" +
                 std::to_string(s.rang) + "\"/>");
  // Ce que l'IPC/DAC-2552 ne décrit pas et que Prométhée garde : symbole, brochage, héritage
  if (!a.symbole.empty() || !a.brochage.empty() || !a.base.empty() || !a.nonHerites.empty()) {
    ligne(2, "<Extension system=\"Prométhée\">");
    element(3, "Symbol", a.symbole);
    element(3, "Base", a.base);
    for (const auto& [b, p] : a.brochage) ligne(3, "<Pin_map pin=\"" + echapper(b) + "\" pads=\"" + echapper(p) + "\"/>");
    for (const std::string& n : a.nonHerites) ligne(3, "<Not_inherited field=\"" + echapper(n) + "\"/>");
    ligne(2, "</Extension>");
  }
  c += "  </Component>";
  std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<IPC-2552 xmlns=\"http://webstds.ipc.org/2552\" revision=\"2022\">\n";
  x += "  <MBD_file_header>\n    <File_type>Component</File_type>\n    <Originating_system>Prométhée</Originating_system>\n";
  if (!date.empty()) x += "    <Date>" + echapper(date) + "</Date>\n";
  x += "  </MBD_file_header>\n";
  x += c + "\n";
  // Somme du composant tel qu'il paraît dans le fichier, de « <Component » à « </Component> »
  x += "  <Checksum algorithm=\"SHA-256\">" + sha256Hex(std::string_view(c).substr(2)) + "</Checksum>\n</IPC-2552>\n";
  return x;
}

std::optional<FicheIpc2552> articleDepuisIpc2552(const std::string& xml, std::string* erreur) {
  NoeudXml doc;
  try {
    doc = xml_interne::lireXml(xml);
  } catch (const std::exception& e) {
    if (erreur) *erreur = std::string("XML illisible : ") + e.what();
    return std::nullopt;
  }
  const NoeudXml* comp = chercher(doc, "Component");
  if (!comp || !chercher(doc, "IPC-2552")) {
    if (erreur) *erreur = "pas une fiche IPC/DAC-2552 (élément « Component » absent)";
    return std::nullopt;
  }
  FicheIpc2552 f;
  Article& a = f.article;
  a.id = attrLocal(*comp, "id");
  a.version = std::max(1, entier(attrLocal(*comp, "version"), 1));
  if (const NoeudXml* id = enfantLocal(*comp, "Identification")) {
    a.fabricant = texteDe(*id, "Manufacturer_name");
    a.mpn = texteDe(*id, "Manufacturer_part_number");
    a.numeroInterne = texteDe(*id, "Enterprise_part_number");
    a.nom = texteDe(*id, "Name");
    a.description = texteDe(*id, "Description");
    a.categorie = texteDe(*id, "Category");
    a.valeur = texteDe(*id, "Value");
    a.etatCycle = texteDe(*id, "Lifecycle_status");
    a.fiche = texteDe(*id, "Datasheet");
    if (const NoeudXml* g = enfantLocal(*id, "Generic_part")) {
      a.generique = true;
      a.exigences = attrLocal(*g, "requirements");
    }
  }
  if (a.mpn.empty() && a.nom.empty()) {
    if (erreur) *erreur = "fiche sans référence de fabricant ni nom";
    return std::nullopt;
  }
  if (a.nom.empty()) a.nom = a.mpn;
  if (a.id.empty()) a.id = identifiantDe(a.mpn.empty() ? a.nom : a.mpn);
  if (const NoeudXml* ph = enfantLocal(*comp, "Physical_composition")) {
    if (const NoeudXml* p = enfantLocal(*ph, "Package")) a.empreinte = attrLocal(*p, "name");
    if (const NoeudXml* s = enfantLocal(*ph, "Size_and_tolerance")) {
      if (const NoeudXml* x = enfantLocal(*s, "Body_length")) a.parametres["longueur"] = attrLocal(*x, "nominal");
      if (const NoeudXml* x = enfantLocal(*s, "Body_width")) a.parametres["largeur"] = attrLocal(*x, "nominal");
      if (const NoeudXml* x = enfantLocal(*s, "Seated_height")) a.hauteur = reel(attrLocal(*x, "nominal"));
    }
    if (const NoeudXml* w = enfantLocal(*ph, "Weight")) {
      double v = reel(w->texte);
      const std::string u = minuscules(attrLocal(*w, "unit"));
      if (u == "mg") v /= 1000;
      else if (u == "kg") v *= 1000;
      a.parametres["masse"] = u == "g" || u.empty() ? sansEspacesAutour(w->texte) : nombre(v);
    }
    a.modele3D = texteDe(*ph, "Model_3d");
  }
  if (const NoeudXml* el = enfantLocal(*comp, "Electrical_properties"))
    for (const NoeudXml* p : tousLocaux(*el, "Property"))
      if (const std::string k = attrLocal(*p, "name"); !k.empty()) a.parametres[k] = attrLocal(*p, "value");
  if (const NoeudXml* en = enfantLocal(*comp, "Environment_info")) {
    if (const NoeudXml* t = enfantLocal(*en, "Operating_temperature")) {
      if (const std::string v = attrLocal(*t, "min"); !v.empty()) a.parametres["temperature_min"] = v;
      if (const std::string v = attrLocal(*t, "max"); !v.empty()) a.parametres["temperature_max"] = v;
    }
    if (const std::string v = texteDe(*en, "Moisture_sensitivity_level"); !v.empty()) a.parametres["msl"] = v;
    if (const std::string v = texteDe(*en, "RoHS"); !v.empty()) a.parametres["rohs"] = v;
  }
  if (const NoeudXml* pk = enfantLocal(*comp, "Packing")) {
    if (const std::string v = texteDe(*pk, "Packaging"); !v.empty()) a.parametres["conditionnement"] = v;
    if (const std::string v = texteDe(*pk, "Quantity"); !v.empty()) a.parametres["quantite_conditionnement"] = v;
  }
  if (const NoeudXml* si = enfantLocal(*comp, "Supplier_info"))
    for (const NoeudXml* s : tousLocaux(*si, "Supplier")) {
      if (attrLocal(*s, "role") == "catalogue" && minuscules(attrLocal(*s, "name")) == "lcsc") {
        a.lcsc = attrLocal(*s, "part_number");
        continue;
      }
      OffreArticle o;
      o.distributeur = attrLocal(*s, "name");
      o.reference = attrLocal(*s, "part_number");
      if (const std::string d = attrLocal(*s, "currency"); !d.empty()) o.devise = d;
      o.conditionnement = std::max(1, entier(attrLocal(*s, "minimum_pack"), 1));
      o.stock = entier(attrLocal(*s, "stock"), -1);
      for (const NoeudXml* p : tousLocaux(*s, "Price_break")) o.prix.push_back({entier(attrLocal(*p, "quantity"), 1), reel(attrLocal(*p, "unit_price"))});
      a.offres.push_back(o);
    }
  for (const NoeudXml* s : tousLocaux(*comp, "Alternate_source")) {
    SecondeSource x;
    x.fabricant = attrLocal(*s, "manufacturer");
    x.mpn = attrLocal(*s, "part_number");
    if (const std::string v = attrLocal(*s, "lifecycle"); !v.empty()) x.cycleVie = v;
    x.rang = std::max(2, entier(attrLocal(*s, "rank"), 2));
    a.secondes.push_back(x);
  }
  if (const NoeudXml* ex = enfantLocal(*comp, "Extension")) {
    a.symbole = texteDe(*ex, "Symbol");
    a.base = texteDe(*ex, "Base");
    for (const NoeudXml* p : tousLocaux(*ex, "Pin_map")) a.brochage[attrLocal(*p, "pin")] = attrLocal(*p, "pads");
    for (const NoeudXml* n : tousLocaux(*ex, "Not_inherited")) a.nonHerites.insert(attrLocal(*n, "field"));
  }
  // Somme de contrôle : celle du composant tel qu'il est écrit dans le fichier
  if (const NoeudXml* s = chercher(doc, "Checksum")) {
    const std::string attendue = minuscules(sansEspacesAutour(s->texte));
    if (minuscules(attrLocal(*s, "algorithm")) != "sha-256") f.alertes.push_back("somme de contrôle « " + attrLocal(*s, "algorithm") + " » non vérifiée");
    else if (sha256Hex(brutComposant(xml)) != attendue) {
      f.sommeJuste = false;
      f.alertes.push_back("somme de contrôle fausse : la fiche a été modifiée après son écriture");
    }
  }
  return f;
}

int enrichirArticles(std::vector<Article>& articles, const std::vector<Article>& fiches) {
  int n = 0;
  for (Article& a : articles) {
    if (a.mpn.empty()) continue;
    const auto f = std::find_if(fiches.begin(), fiches.end(), [&](const Article& x) { return mpnNormalise(x.mpn) == mpnNormalise(a.mpn) && fabricantsCompatibles(x.fabricant, a.fabricant); });
    if (f == fiches.end()) continue;
    const Article avant = a;
    for (auto [champ, source] : {std::pair{&a.description, &f->description}, {&a.valeur, &f->valeur}, {&a.fabricant, &f->fabricant}, {&a.fiche, &f->fiche}, {&a.lcsc, &f->lcsc},
                                 {&a.modele3D, &f->modele3D}, {&a.numeroInterne, &f->numeroInterne}, {&a.etatCycle, &f->etatCycle}, {&a.empreinte, &f->empreinte}})
      if (champ->empty()) *champ = *source;
    if (a.hauteur <= 0) a.hauteur = f->hauteur;
    for (const auto& [k, v] : f->parametres) a.parametres.try_emplace(k, v);
    for (const OffreArticle& o : f->offres)
      if (std::none_of(a.offres.begin(), a.offres.end(), [&](const OffreArticle& x) { return x.distributeur == o.distributeur && x.reference == o.reference; })) a.offres.push_back(o);
    for (const SecondeSource& s : f->secondes)
      if (std::none_of(a.secondes.begin(), a.secondes.end(), [&](const SecondeSource& x) { return mpnNormalise(x.mpn) == mpnNormalise(s.mpn); })) a.secondes.push_back(s);
    if (!(a == avant)) ++n;
  }
  return n;
}

// ---------- JEP30 ----------
namespace {
std::string typeJedec(const std::string& nom) {
  const std::string n = minuscules(nom);
  auto a = [&n](const char* x) { return n.find(x) != std::string::npos; };
  if (a("bga")) return "BGA";
  if (a("qfn") || a("vqfn") || a("wqfn")) return "QFN";
  if (a("dfn") || a("son")) return "DFN";
  if (a("qfp")) return "QFP";
  if (a("tssop") || a("msop") || a("ssop")) return "SSOP";
  if (a("soic") || a("so-") || a("so8") || a("sop")) return "SO";
  if (a("sot")) return "SOT";
  if (a("dip") || a("dil")) return "DIP";
  if (a("to-") || a("to2") || a("dpak")) return "TO";
  if (a("sma") || a("smb") || a("smc") || a("sod")) return "DO";
  if (a("header") || a("pinheader") || a("conn")) return "CONNECTOR";
  for (const char* puce : {"0201", "0402", "0603", "0805", "1206", "1210", "2010", "2512"})
    if (a(puce)) return "CHIP";
  return "OTHER";
}
const char* formeJep30(FormePastille f) {
  switch (f) {
    case FormePastille::Rond: return "rond";
    case FormePastille::Ovale: return "oblong";
    default: return "rectangulaire";
  }
}
std::string formeXml(const std::string& f) { return f == "rond" ? "Round" : f == "oblong" ? "Oblong" : "Rectangular"; }
std::string formeDepuisXml(const std::string& f) {
  const std::string m = minuscules(f);
  return m == "round" || m == "circular" || m == "rond" ? "rond" : m == "oblong" || m == "oval" || m == "obround" ? "oblong" : "rectangulaire";
}
}  // namespace

BoitierJep30 boitierDepuisEmpreinte(const DefEmpreinte& e, const Article* a) {
  BoitierJep30 b;
  b.nom = e.id.rfind("jep30:", 0) == 0 ? e.id.substr(6) : e.id;
  b.type = typeJedec(e.id);
  b.montage = montageDe(e) == "SMT" ? "CMS" : "traversant";
  if (a) {
    b.fabricant = a->fabricant;
    b.mpn = a->mpn;
  }
  const Boite corps = empriseCorps(e);
  b.longueur = e.corps.largeur > 0 ? mm(e.corps.largeur) : corps.vide() ? 0 : mm(corps.x1 - corps.x0);
  b.largeur = e.corps.profondeur > 0 ? mm(e.corps.profondeur) : corps.vide() ? 0 : mm(corps.y1 - corps.y0);
  b.hauteur = mm(e.corps.hauteur);
  double pas = 0;
  for (std::size_t i = 0; i < e.pastilles.size(); ++i)
    for (std::size_t j = i + 1; j < e.pastilles.size(); ++j) {
      const double d = distance(e.pastilles[i].position, e.pastilles[j].position) / 1e6;
      if (d > 1e-6 && (pas == 0 || d < pas)) pas = d;
    }
  b.pas = pas;
  for (const Pastille& p : e.pastilles) {
    TerminalJep30 t;
    t.numero = p.numero;
    t.x = mm(p.position.x);
    t.y = -mm(p.position.y);
    t.largeur = mm(p.largeur);
    t.longueur = mm(p.hauteur);
    t.percage = mm(p.percage);
    t.rotation = p.rotation;
    t.forme = formeJep30(p.forme);
    b.terminaux.push_back(t);
  }
  return b;
}

DefEmpreinte empreinteDepuisBoitier(const BoitierJep30& b) {
  DefEmpreinte e;
  e.id = "jep30:" + identifiantDe(b.nom);
  e.description = "Boîtier " + b.nom + (b.type.empty() ? std::string() : " (" + b.type + ")") + " repris d’un fichier JEP30";
  e.famille = b.type;
  Boite emprise;
  for (const TerminalJep30& t : b.terminaux) {
    Pastille p;
    p.numero = t.numero;
    p.position = {nmDe(t.x), -nmDe(t.y)};
    p.largeur = nmDe(t.largeur);
    p.hauteur = nmDe(t.longueur);
    p.rotation = t.rotation;
    p.percage = nmDe(t.percage);
    p.type = t.percage > 0 ? TypePastille::Traversante : TypePastille::CMS;
    p.forme = t.forme == "rond" ? FormePastille::Rond : t.forme == "oblong" ? FormePastille::Ovale : p.type == TypePastille::CMS ? FormePastille::RectArrondi : FormePastille::Rect;
    const bool quart = std::abs(std::fmod(std::abs(t.rotation), 180.0) - 90) < 1e-6;
    const Nm dx = (quart ? p.hauteur : p.largeur) / 2, dy = (quart ? p.largeur : p.hauteur) / 2;
    emprise.etendre(Pt{p.position.x - dx, p.position.y - dy});
    emprise.etendre(Pt{p.position.x + dx, p.position.y + dy});
    e.pastilles.push_back(p);
  }
  const Nm cl = nmDe(b.longueur) / 2, cw = nmDe(b.largeur) / 2;
  if (cl > 0 && cw > 0) {
    emprise.etendre(Pt{-cl, -cw});
    emprise.etendre(Pt{cl, cw});
    e.corps.largeur = 2 * cl;
    e.corps.profondeur = 2 * cw;
    const Pt c[4] = {{-cl, -cw}, {cl, -cw}, {cl, cw}, {-cl, cw}};
    for (int i = 0; i < 4; ++i) e.traits.push_back({Couche::FabricationDessus, c[i], c[(i + 1) % 4], 100'000, 0});
  }
  e.corps.hauteur = nmDe(b.hauteur);
  e.encombrement = emprise.vide() ? Boite{} : emprise.gonflee(250'000);
  return e;
}

std::string xmlJep30(const BoitierJep30& b) {
  const std::string p = "JEP30-D10:";
  std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  x += "<" + p + "PartModel xmlns:JEP30-D10=\"http://www.jedec.org/JEP30/D10\" ComplianceToPartModelSchemaVersion=\"D10\" PartModelContentRevision=\"1\" PartModelStatus=\"Released\" DocumentID=\"" +
       echapper("jep30:" + b.nom) + "\">\n";
  auto el = [&](int niveau, const std::string& nom, const std::string& v, const std::string& unite = {}) {
    if (v.empty()) return;
    x += std::string(static_cast<std::size_t>(niveau) * 2, ' ') + "<" + p + nom + (unite.empty() ? std::string() : " unit=\"" + unite + "\"") + ">" + echapper(v) + "</" + p + nom + ">\n";
  };
  if (!b.fabricant.empty() || !b.mpn.empty()) {
    x += "  <" + p + "BusinessInfo>\n";
    if (!b.fabricant.empty()) {
      x += "    <" + p + "Manufacturer-Array>\n      <" + p + "Manufacturer>\n";
      el(4, "ManufacturerName", b.fabricant);
      x += "      </" + p + "Manufacturer>\n    </" + p + "Manufacturer-Array>\n";
    }
    if (!b.mpn.empty()) {
      x += "    <" + p + "ManufacturerPartNumber-Array>\n";
      el(3, "ManufacturerPartNumber", b.mpn);
      x += "    </" + p + "ManufacturerPartNumber-Array>\n";
    }
    x += "  </" + p + "BusinessInfo>\n";
  }
  x += "  <" + p + "PackageSection>\n    <" + p + "Package-Array>\n      <" + p + "Package>\n";
  el(4, "PackageName", b.nom);
  el(4, "PackageType", b.type);
  el(4, "MountingType", b.montage == "traversant" ? "ThroughHole" : "SurfaceMount");
  el(4, "PhysicalModelType", "Nominal");
  if (b.longueur > 0) el(4, "BodyLength", nombre(b.longueur), "mm");
  if (b.largeur > 0) el(4, "BodyWidth", nombre(b.largeur), "mm");
  if (b.hauteur > 0) el(4, "SeatedHeight", nombre(b.hauteur), "mm");
  if (b.pas > 0) el(4, "TerminalPitch", nombre(b.pas), "mm");
  el(4, "TerminalCount", std::to_string(b.terminaux.size()));
  if (!b.terminaux.empty()) {
    x += "        <" + p + "Terminal-Array Basis=\"LandPattern\" unit=\"mm\">\n";
    for (const TerminalJep30& t : b.terminaux)
      x += "          <" + p + "Terminal TerminalNumber=\"" + echapper(t.numero) + "\" PositionX=\"" + nombre(t.x) + "\" PositionY=\"" + nombre(t.y) + "\" Width=\"" + nombre(t.largeur) + "\" Length=\"" +
           nombre(t.longueur) + "\" Rotation=\"" + nombre(t.rotation) + "\" Shape=\"" + formeXml(t.forme) + "\"" + (t.percage > 0 ? " DrillDiameter=\"" + nombre(t.percage) + "\"" : std::string()) + "/>\n";
    x += "        </" + p + "Terminal-Array>\n";
  }
  x += "      </" + p + "Package>\n    </" + p + "Package-Array>\n  </" + p + "PackageSection>\n</" + p + "PartModel>\n";
  return x;
}

std::optional<BoitierJep30> lireJep30(const std::string& xml, std::string* erreur) {
  NoeudXml doc;
  try {
    doc = xml_interne::lireXml(xml);
  } catch (const std::exception& e) {
    if (erreur) *erreur = std::string("XML illisible : ") + e.what();
    return std::nullopt;
  }
  const NoeudXml* modele = chercher(doc, "PartModel");
  const NoeudXml* paquet = modele ? chercher(*modele, "Package") : nullptr;
  if (!paquet) {
    if (erreur) *erreur = "pas un modèle de pièce JEP30 (élément « Package » absent)";
    return std::nullopt;
  }
  BoitierJep30 b;
  // Cotes : en mm, ou converties depuis l'unité dite (« mil », « in », « um »)
  auto cote = [](const NoeudXml& n, const std::string& v) {
    double x = reel(v);
    const std::string u = minuscules(attrLocal(n, "unit"));
    if (u == "mil" || u == "mils") x *= 0.0254;
    else if (u == "in" || u == "inch") x *= 25.4;
    else if (u == "um" || u == "µm") x /= 1000;
    return x;
  };
  auto coteDe = [&](const char* nom) {
    const NoeudXml* n = enfantLocal(*paquet, nom);
    return n ? cote(*n, n->texte) : 0.0;
  };
  b.nom = texteDe(*paquet, "PackageName");
  if (b.nom.empty()) b.nom = attrLocal(*modele, "DocumentID");
  b.type = texteDe(*paquet, "PackageType");
  b.montage = minuscules(texteDe(*paquet, "MountingType")).find("through") != std::string::npos ? "traversant" : "CMS";
  b.longueur = coteDe("BodyLength");
  b.largeur = coteDe("BodyWidth");
  b.hauteur = coteDe("SeatedHeight");
  if (b.hauteur == 0) b.hauteur = coteDe("MaximumHeight");
  b.pas = coteDe("TerminalPitch");
  if (const NoeudXml* bi = chercher(*modele, "BusinessInfo")) {
    if (const NoeudXml* m = chercher(*bi, "ManufacturerName")) b.fabricant = sansEspacesAutour(m->texte);
    if (const NoeudXml* m = chercher(*bi, "ManufacturerPartNumber")) b.mpn = sansEspacesAutour(m->texte);
  }
  if (const NoeudXml* ta = enfantLocal(*paquet, "Terminal-Array")) {
    for (const NoeudXml* t : tousLocaux(*ta, "Terminal")) {
      TerminalJep30 x;
      x.numero = attrLocal(*t, "TerminalNumber");
      x.x = cote(*ta, attrLocal(*t, "PositionX"));
      x.y = cote(*ta, attrLocal(*t, "PositionY"));
      x.largeur = cote(*ta, attrLocal(*t, "Width"));
      x.longueur = cote(*ta, attrLocal(*t, "Length"));
      x.percage = cote(*ta, attrLocal(*t, "DrillDiameter"));
      x.rotation = reel(attrLocal(*t, "Rotation"));
      x.forme = formeDepuisXml(attrLocal(*t, "Shape"));
      b.terminaux.push_back(x);
    }
  }
  if (b.nom.empty()) {
    if (erreur) *erreur = "boîtier sans nom";
    return std::nullopt;
  }
  return b;
}

// ---------- Conception de référence ----------
std::optional<ImportReference> importerConceptionReference(const std::string& nomFichier, const std::string& contenu, const LecteurVoisin& lire, ContenuBibliotheque& bibliotheque,
                                                           const std::vector<std::string>& fichesXml, std::string* erreur) {
  auto imp = importerConceptionTierce(nomFichier, contenu, lire, erreur);
  if (!imp) return std::nullopt;
  ImportReference r;
  r.import = std::move(*imp);
  std::vector<Article> fiches;
  for (const std::string& x : fichesXml) {
    std::string e1, e2;
    if (auto f = articleDepuisIpc2552(x, &e1)) {
      ++r.fichesLues;
      for (const auto& a : f->alertes) r.alertes.push_back((f->article.mpn.empty() ? f->article.nom : f->article.mpn) + " : " + a);
      fiches.push_back(f->article);
    } else if (auto b = lireJep30(x, &e2)) {
      ranger(bibliotheque.empreintes, empreinteDepuisBoitier(*b));
      ++r.boitiers;
    } else
      r.alertes.push_back("fiche jointe illisible : " + e1);
  }
  r.articles = rattacherArticles(r.import.conception, bibliotheque, "Repris de " + r.import.logiciel);
  r.articlesEnrichis = enrichirArticles(bibliotheque.articles, fiches);
  // Fiches sans composant dans la conception : articles du fabricant à part entière
  for (Article f : fiches)
    if (!articleDeReference(bibliotheque, f.mpn, f.fabricant)) {
      if (f.categorie.empty()) f.categorie = "Repris de " + r.import.logiciel;
      ranger(bibliotheque.articles, f);
    }
  for (auto& [id, a] : r.import.conception.articles)
    for (const Article& b : bibliotheque.articles)
      if (b.id == id) a = b;
  return r;
}

// ---------- Conversion d'un dossier en série ----------
namespace {
// Feuilles filles qu'appelle un schéma : « F1 "x.sch" » (KiCad 5), « (property "Sheetfile" "x.kicad_sch" » (KiCad 6 et
// suivants), « source=x.sch » (gschem, Lepton)
std::vector<std::string> feuillesAppelees(const std::string& t) {
  std::vector<std::string> r;
  auto guillemets = [&](std::size_t p) {
    const auto a = t.find('"', p), b = a == std::string::npos ? a : t.find('"', a + 1);
    if (b != std::string::npos) r.push_back(t.substr(a + 1, b - a - 1));
  };
  for (std::size_t p = t.find("\nF1 \""); p != std::string::npos; p = t.find("\nF1 \"", p + 1)) guillemets(p + 3);
  for (const char* cle : {"(property \"Sheetfile\"", "(property \"Sheet file\""})
    for (std::size_t p = t.find(cle); p != std::string::npos; p = t.find(cle, p + 1)) guillemets(p + std::strlen(cle));
  for (std::size_t p = t.find("\nsource="); p != std::string::npos; p = t.find("\nsource=", p + 1)) {
    const auto f = t.find_first_of("\r\n", p + 8);
    r.push_back(sansEspacesAutour(t.substr(p + 8, f == std::string::npos ? std::string::npos : f - p - 8)));
  }
  for (auto& x : r) std::replace(x.begin(), x.end(), '\\', '/');
  return r;
}
}  // namespace

std::vector<ProjetAConvertir> projetsDuDossier(const std::vector<std::string>& fichiersBruts, const LecteurDossier& lire) {
  std::vector<std::string> fichiers;
  for (std::string f : fichiersBruts) {
    std::replace(f.begin(), f.end(), '\\', '/');
    fichiers.push_back(f);
  }
  std::sort(fichiers.begin(), fichiers.end());
  // Ordre des fichiers principaux : un projet complet d'abord, puis le schéma (qui relie la carte voisine), la carte
  static const std::vector<std::string> ordre = {"kicad_pro", "prjpcb", "kicad_sch", "sch", "kicad_pcb", "brd", "schdoc", "csschdoc", "cmschdoc", "pcbdoc", "cspcbdoc", "cmpcbdoc",
                                                 "swpcbdoc", "pcb", "asc", "lay6", "lay", "cpa", "csa", "vsdx"};
  std::set<std::string> dossiersKicad, dossiersAltium;
  for (const auto& f : fichiers) {
    const std::string e = extensionDe(f);
    if (e == "kicad_pro") dossiersKicad.insert(dossierDe(f));
    if (e == "prjpcb") dossiersAltium.insert(dossierDe(f));
  }
  auto sousAltium = [&](const std::string& d) {
    for (const auto& a : dossiersAltium)
      if (d == a || (a.empty() ? true : d.rfind(a + "/", 0) == 0)) return true;
    return false;
  };
  std::map<std::pair<std::string, std::string>, std::vector<std::string>> groupes;  // (dossier, base en minuscules) → fichiers
  std::vector<std::pair<std::string, std::string>> ordreGroupes;
  for (const auto& f : fichiers) {
    const std::string e = extensionDe(f), d = dossierDe(f);
    if (std::find(ordre.begin(), ordre.end(), e) == ordre.end()) continue;
    // Dossiers de sauvegarde et d'aperçu des logiciels : pas des projets
    const std::string dm = "/" + minuscules(d) + "/";
    if (dm.find("/history/") != std::string::npos || dm.find("-backups/") != std::string::npos || dm.find("/__previews/") != std::string::npos) continue;
    if (e != "prjpcb" && e.find("doc") != std::string::npos && sousAltium(d)) continue;  // documents d'un projet Altium
    const std::pair<std::string, std::string> cle{d, minuscules(baseDe(f))};
    if (!groupes.count(cle)) ordreGroupes.push_back(cle);
    groupes[cle].push_back(f);
  }
  // Feuilles filles appelées par un schéma du même dossier
  std::set<std::string> appelees;
  if (lire)
    for (const auto& f : fichiers)
      if (const std::string e = extensionDe(f); e == "sch" || e == "kicad_sch")
        if (const auto t = lire(f))
          for (const auto& x : feuillesAppelees(*t)) {
            const std::string d = dossierDe(f);
            appelees.insert(minuscules(d.empty() ? x : d + "/" + x));
          }
  std::vector<ProjetAConvertir> r;
  for (const auto& cle : ordreGroupes) {
    const auto& g = groupes[cle];
    if (std::all_of(g.begin(), g.end(), [&](const std::string& f) { return appelees.count(minuscules(f)) > 0; })) continue;
    const bool projetKicad = std::any_of(g.begin(), g.end(), [](const std::string& f) { return extensionDe(f) == "kicad_pro"; });
    // Feuilles filles d'un projet KiCad : un .kicad_sch seul, dans un dossier qui a son .kicad_pro
    if (!projetKicad && dossiersKicad.count(cle.first) && std::all_of(g.begin(), g.end(), [](const std::string& f) { return extensionDe(f) == "kicad_sch"; })) continue;
    std::string principal;
    for (const auto& e : ordre)
      for (const auto& f : g)
        if (principal.empty() && extensionDe(f) == e) principal = f;
    if (!principal.empty()) r.push_back({baseDe(principal), principal});
  }
  // Projets de même nom dans des dossiers différents : nommés aussi par leur dossier (« E1/ampli »)
  std::map<std::string, int> noms;
  for (const auto& p : r) ++noms[p.nom];
  for (auto& p : r)
    if (noms[p.nom] > 1) {
      const std::string d = dossierDe(p.fichier);
      p.nom = (d.empty() ? std::string(".") : nomDe(d)) + "/" + p.nom;
    }
  return r;
}

std::string RapportProjet::texte() const {
  std::string t = "# Conversion de « " + projet + " »\n\n";
  t += "- Fichier : " + fichier + "\n";
  if (!logiciel.empty()) t += "- Logiciel d’origine : " + logiciel + "\n";
  t += std::string("- Résultat : ") + (reussi ? "converti" : "échec — " + erreur) + "\n";
  if (!fichiersLus.empty()) {
    t += "- Fichiers lus :";
    for (const auto& f : fichiersLus) t += " " + f;
    t += "\n";
  }
  if (reussi) {
    t += "- Contenu : " + std::to_string(composants) + " composant(s), " + std::to_string(reseaux) + " réseau(x), " + std::to_string(empreintes) + " empreinte(s) posée(s), " +
         std::to_string(feuilles) + " feuille(s)\n\n";
    t += "## Articles\n\n" + std::to_string(articles.rattaches) + " rattaché(s) à la bibliothèque, " + std::to_string(articles.crees) + " créé(s), " +
         std::to_string(articles.sansReference) + " composant(s) sans référence de fabricant.\n\n";
    if (!articles.lignes.empty()) {
      t += "| Repère | Référence fabricant | Fabricant | Article | État |\n|---|---|---|---|---|\n";
      for (const auto& l : articles.lignes) t += "| " + l.reference + " | " + l.mpn + " | " + l.fabricant + " | " + l.article + " | " + l.etat + " |\n";
      t += "\n";
    }
  }
  if (!alertes.empty()) {
    t += "## À vérifier\n\n";
    for (const auto& a : alertes) t += "- " + a + "\n";
  }
  return t;
}

std::string BilanSerie::texte() const {
  std::string t = "# Conversion en série\n\n";
  t += std::to_string(projets.size()) + " projet(s) : " + std::to_string(reussis) + " converti(s), " + std::to_string(echecs) + " en échec" + (interrompu ? ", série interrompue" : "") +
       ". " + std::to_string(articlesCrees) + " article(s) ajouté(s) à la bibliothèque d’équipe, dont " + std::to_string(articlesCommuns) + " commun(s) à plusieurs projets.\n\n";
  t += "| Projet | Logiciel | Résultat | Composants | Rattachés | Créés |\n|---|---|---|---|---|---|\n";
  for (const auto& p : projets)
    t += "| " + p.projet + " | " + p.logiciel + " | " + (p.reussi ? std::string("converti") : "échec : " + p.erreur) + " | " + std::to_string(p.composants) + " | " +
         std::to_string(p.articles.rattaches) + " | " + std::to_string(p.articles.crees) + " |\n";
  return t;
}

BilanSerie convertirEnSerie(const std::vector<ProjetAConvertir>& projets, ContenuBibliotheque& equipe, const LecteurDossier& lire,
                            const std::function<void(const RapportProjet&, const ConceptionElec&)>& ecrire, const std::function<void(std::size_t, std::size_t)>& progression,
                            const std::atomic<bool>* arret) {
  BilanSerie b;
  const std::size_t avant = equipe.articles.size();
  std::map<std::string, std::set<std::size_t>> usage;  // article → projets qui l'emploient
  for (std::size_t i = 0; i < projets.size(); ++i) {
    if (arret && arret->load()) {
      b.interrompu = true;
      break;
    }
    const ProjetAConvertir& p = projets[i];
    RapportProjet r;
    r.projet = p.nom;
    r.fichier = p.fichier;
    // Ce que le projet ajoute à la bibliothèque est retiré s'il échoue
    const std::size_t na = equipe.articles.size(), ns = equipe.symboles.size(), ne = equipe.empreintes.size();
    try {
      const std::optional<std::string> contenu = lire ? lire(p.fichier) : std::nullopt;
      if (!contenu) throw std::runtime_error("fichier illisible ou absent");
      const std::string dossier = dossierDe(p.fichier);
      std::string erreur;
      auto imp = importerConceptionTierce(nomDe(p.fichier), *contenu, [&](const std::string& n) { return lire(dossier.empty() ? n : dossier + "/" + n); }, &erreur);
      if (!imp) throw std::runtime_error(erreur.empty() ? std::string("import impossible") : erreur);
      r.logiciel = imp->logiciel;
      r.fichiersLus = imp->fichiers;
      r.alertes = imp->alertes;
      r.articles = rattacherArticles(imp->conception, equipe, "Repris de " + imp->logiciel);
      const ConceptionElec& c = imp->conception;
      r.composants = static_cast<int>(c.composants.size());
      r.reseaux = static_cast<int>(netlistDe(c).reseaux.size());
      r.empreintes = static_cast<int>(c.circuit.empreintes.size());
      r.feuilles = static_cast<int>(c.feuilles.size());
      r.reussi = true;
      if (ecrire) ecrire(r, c);
      for (const auto& id : r.articles.articlesUtilises) usage[id].insert(i);
      ++b.reussis;
    } catch (const std::exception& e) {
      equipe.articles.resize(na);
      equipe.symboles.resize(ns);
      equipe.empreintes.resize(ne);
      r.reussi = false;
      r.erreur = e.what();
      r.articles = {};
      ++b.echecs;
    }
    b.projets.push_back(std::move(r));
    if (progression) progression(i + 1, projets.size());
  }
  b.articlesCrees = static_cast<int>(equipe.articles.size() - avant);
  b.articlesCommuns = static_cast<int>(std::count_if(usage.begin(), usage.end(), [](const auto& u) { return u.second.size() >= 2; }));
  return b;
}

// ---------- Guide de l'ancien outil ----------
namespace {
const std::map<std::string, std::vector<TermeGuide>>& guides() {
  static const std::map<std::string, std::vector<TermeGuide>> g = {
      {"KiCad",
       {{"Eeschema, éditeur de schéma", "Atelier électronique, onglet Schéma", "un seul modèle : le schéma et le circuit sont toujours d’accord"},
        {"Pcbnew, éditeur de PCB", "Atelier électronique, onglet Circuit", "même fichier de projet que la mécanique et l’électrique"},
        {"Ajouter un symbole (A)", "Choisir un composant", "A, comme dans KiCad ; la fenêtre cherche aussi les articles réels"},
        {"Footprint, empreinte", "Empreinte", "« Affecter les empreintes » les met en tableau, par règles d’équivalence"},
        {"Champs MPN, Manufacturer", "Référence fabricant, fabricant, article rattaché", "E ouvre l’inspecteur du composant choisi"},
        {"Mettre à jour le PCB depuis le schéma (F8)", "Synchroniser", "F8 ; le panneau des écarts dit tout ce qui change"},
        {"Annoter le schéma", "Annoter les nouveaux composants, Tout renuméroter", "ruban Schéma"},
        {"ERC, DRC", "Vérifier", "F7 lance les deux ; F4 va au problème suivant"},
        {"Router une piste (X)", "Router", "X ; W et Maj+W changent la largeur, / inverse le coude, Retour arrière retire le dernier"},
        {"Via (V)", "Via", "V ; Page préc. et Page suiv. changent de cuivre pendant le routage"},
        {"Retourner l’empreinte (F)", "Changer de face", "F"},
        {"Zone remplie (B pour remplir)", "Zone de cuivre", "remplie d’elle-même à chaque changement"},
        {"Edge.Cuts", "Contour de la carte", "couche Contour, ou contour piloté par une esquisse"},
        {"Feuille hiérarchique", "Feuille fille", "« Feuille mère » remonte d’un niveau"},
        {"Classes de réseaux, Board Setup", "Gestionnaire de contraintes", "règles et classes communes au schéma et au circuit"},
        {"Tracer, fichiers Gerber", "Publier pour la fabrication", "dossier complet et révision verrouillée ; « Dossier de fabrication (brouillon) » sans publier"},
        {"Glisser (G), déplacer (M)", "Glisser en gardant les fils, déplacer en les décrochant", "G et M au schéma"}}},
      {"EAGLE",
       {{"Fenêtres Schematic et Board", "Onglets Schéma et Circuit de l’atelier", "rien à annoter à la main : schéma et carte restent liés"},
        {"Device, Deviceset", "Article : symbole, empreinte et brochage", "fenêtre de choix des composants (A)"},
        {"Package", "Empreinte", "bibliothèque d’empreintes, générateur IPC-7351"},
        {"Gate", "Unité du symbole (U1A, U1B…)", "« Échanger les deux portes choisies »"},
        {"Signal", "Réseau", "même mot au circuit et au schéma"},
        {"Airwire", "Chevelu (liaison non routée)", "compté par la vérification (DRC-20)"},
        {"Polygon puis RATSNEST", "Zone de cuivre", "remplie d’elle-même, pas de commande à relancer"},
        {"ULP", "Script Python sur les commandes annulables", "un script s’annule d’un Ctrl+Z"},
        {"CAM Processor", "Publier pour la fabrication", "Gerber, perçage, placement, nomenclature d’un geste"},
        {"ADD, NET, LABEL", "Choisir un composant, Fil, Étiquette", "A, W, L"},
        {"ROUTE, VIA, MIRROR", "Router, Via, Changer de face", "X, V, F"},
        {"ROTATE, MOVE, COPY", "Tourner, déplacer, dupliquer", "R, glisser ou G, Ctrl+D"},
        {"Attribut MPN", "Référence fabricant, rattachée à l’article", "les articles communs sont rangés une fois dans la bibliothèque d’équipe"},
        {"Couches 1 Top, 16 Bottom, 20 Dimension", "Cuivre dessus, cuivre dessous, contour", "tableau d’association des couches à l’import"},
        {"Couches 21 tPlace, 29 tStop, 31 tCream", "Sérigraphie, vernis épargne, pâte du dessus", "déduites des pastilles"},
        {"Règles DRC (.dru)", "Gestionnaire de contraintes", "règles par classe et par zone"}}},
      {"Altium",
       {{"Projet PCB (.PrjPcb)", "Projet Prométhée", "un seul fichier pour la carte, le boîtier et l’armoire"},
        {"SchDoc, PcbDoc", "Feuilles du schéma, circuit", "onglets Schéma et Circuit"},
        {"Bibliothèque intégrée, base de données", "Bibliothèque d’articles, personnelle ou d’équipe", "« Bibliothèque d’articles en tableau »"},
        {"Designator, Comment", "Repère, valeur", "F2 modifie le texte sur place"},
        {"Paramètres (Manufacturer Part Number)", "Référence fabricant et champs", "rattachés à l’article par la référence"},
        {"Polygon Pour", "Zone de cuivre", "remplie d’elle-même"},
        {"Room", "Zone de règles", "règles propres à une région de la carte"},
        {"Design Rules", "Gestionnaire de contraintes", "règles et classes, communes au schéma et au circuit"},
        {"Net Class, Differential Pair", "Classe de réseaux, paire différentielle", "gabarits d’interface (USB, HDMI, DDR…)"},
        {"Routage interactif (Ctrl+W)", "Router", "X ; Tab avant de poser règle l’outil"},
        {"Place Via", "Via", "V"},
        {"Retourner sur l’autre face (L en glissant)", "Changer de face", "F"},
        {"Update PCB Document", "Synchroniser", "F8"},
        {"Compile, ERC", "Vérifier", "F7"},
        {"Variants", "Variantes de montage", "outils de conception, variante active en un pas"},
        {"OutJob", "Jeux de sorties, publication", "« Publier pour la fabrication »"},
        {"Espace pour tourner", "Tourner", "R, Maj+R dans l’autre sens"}}},
      {"PADS",
       {{"PADS Logic", "Onglet Schéma", ""},
        {"PADS Layout", "Onglet Circuit", ""},
        {"Part Type, Decal", "Article, empreinte", ""},
        {"ECO", "Synchroniser", "F8"},
        {"Copper Pour, Plane", "Zone de cuivre, plan", "les plans négatifs deviennent des zones remplies"},
        {"Router", "Router", "X"},
        {"Commandes sans mode (W, V…)", "Raccourcis de l’atelier", "F1 en donne la liste"}}},
      {"OrCAD",
       {{"Capture", "Onglet Schéma", ""},
        {"Allegro, PCB Editor", "Onglet Circuit", ""},
        {"Part, footprint (.psm, .dra)", "Symbole, empreinte", ""},
        {"Créer la netlist", "Rien à faire : un seul modèle", "« Netlist pour un autre logiciel » pour l’exporter"},
        {"Constraint Manager", "Gestionnaire de contraintes", ""},
        {"Shape", "Zone de cuivre", ""},
        {"Padstack", "Pile de pastilles (gabarit IPC-7351)", "« Piles de pastilles du projet »"},
        {"Annotate", "Annoter les nouveaux composants", ""},
        {"DRC", "Vérifier", "F7"}}},
      {"EasyEDA",
       {{"Schematic, PCB", "Onglets Schéma et Circuit", ""},
        {"LCSC Part #", "Code LCSC du composant", "repris dans la nomenclature"},
        {"Copper Area", "Zone de cuivre", ""},
        {"Update PCB", "Synchroniser", "F8"},
        {"Wire (Alt+W)", "Fil", "W"}}},
      {"gEDA",
       {{"gschem, lepton-schematic", "Onglet Schéma", ""},
        {"pcb", "Onglet Circuit", ""},
        {"gsch2pcb, gnetlist", "Synchronisation permanente", "F8 montre les écarts"},
        {"Attributs refdes, value, footprint", "Repère, valeur, empreinte", "E ouvre l’inspecteur"},
        {"net= (alimentations cachées)", "Broches d’alimentation communes, réseaux nommés", ""},
        {"slot, slotdef", "Unités du symbole", ""},
        {"source= (sous-schéma)", "Feuille fille", ""},
        {"Element", "Empreinte posée", ""},
        {"Polygon (clearpoly)", "Zone de cuivre", "dégagements tirés des règles"}}},
      {"Sprint-Layout",
       {{"Onglet de platine", "Carte (une conception par platine)", ""},
        {"Macro", "Empreinte de la bibliothèque", ""},
        {"Plan de masse", "Zone de cuivre GND_PLANE", "relié par le cuivre"},
        {"Connexions (chevelu)", "Chevelu, réseaux tirés du cuivre", "les réseaux portent le nom de la première pastille"},
        {"Couches C1, C2, I1, I2", "Cuivre dessus, dessous, internes 1 et 2", ""},
        {"Autorouteur", "Router tout", "recherche de chemin pour chaque liaison"},
        {"Pas de schéma", "Schéma à dessiner", "la carte s’y relie par les repères"}}},
      {"Protel Autotrax",
       {{"Component", "Empreinte posée", ""},
        {"Track", "Piste", ""},
        {"Pad, Via", "Pastille, via", ""},
        {"NETDEF", "Réseaux de la carte", ""},
        {"Couche Keepout", "Contour de la carte", "quand elle entoure tout"},
        {"Fill", "Région de cuivre", ""}}},
      {"P-CAD",
       {{"Pattern", "Empreinte", ""},
        {"Symbol, Component", "Symbole, article", ""},
        {"Copper Pour", "Zone de cuivre", ""},
        {"Plane", "Plan (cuivre interne)", ""},
        {"Keepout", "Zone de règles interdite", ""},
        {"ECO", "Synchroniser", "F8"}}},
      {"CADSTAR",
       {{"Archives .csa, .cpa", "Schéma, circuit", ""},
        {"Part, Symbol, Component", "Article, symbole, empreinte", ""},
        {"Template", "Zone de cuivre", ""},
        {"Route Codes", "Classes de réseaux (largeurs)", "Gestionnaire de contraintes"},
        {"Spacing Codes", "Règles de dégagement", "Gestionnaire de contraintes"}}},
      {"LTspice",
       {{"Schéma .asc", "Feuille du schéma", "les directives SPICE sont gardées pour la simulation"},
        {"Symbole .asy", "Symbole", ""},
        {"Directives .tran, .ac", "Analyses de la simulation (ngspice)", "« Simuler le schéma »"},
        {"Sonde", "Sonde de la simulation", ""}}}};
  return g;
}
const std::vector<TermeGuide>& communs() {
  static const std::vector<TermeGuide> v = {{"Annuler", "Annuler, tout compris (import, script, synchronisation)", "Ctrl+Z"},
                                            {"Raccourcis", "Liste des raccourcis de l’atelier et de l’outil", "F1"},
                                            {"Bibliothèque partagée", "Bibliothèque d’équipe (fichier)", "les articles communs y sont rangés une fois"},
                                            {"Vérification", "Vérifier (ERC et DRC)", "F7, puis F4 pour le problème suivant"}};
  return v;
}
std::string nomGuide(const std::string& logiciel) {
  const std::string l = minuscules(logiciel);
  auto a = [&l](const char* x) { return l.find(x) != std::string::npos; };
  if (a("kicad")) return "KiCad";
  if (a("eagle") || a("fusion")) return "EAGLE";
  if (a("altium") || a("circuitstudio") || a("circuitmaker") || a("solidworks")) return "Altium";
  if (a("pads")) return "PADS";
  if (a("orcad") || a("allegro")) return "OrCAD";
  if (a("easyeda") || a("lcsc")) return "EasyEDA";
  if (a("geda") || a("lepton")) return "gEDA";
  if (a("sprint")) return "Sprint-Layout";
  if (a("autotrax") || a("easytrax") || a("protel")) return "Protel Autotrax";
  if (a("p-cad") || a("pcad")) return "P-CAD";
  if (a("cadstar")) return "CADSTAR";
  if (a("ltspice")) return "LTspice";
  return {};
}
}  // namespace

const std::vector<TermeGuide>& guideAncienOutil(const std::string& logiciel) {
  const std::string n = nomGuide(logiciel);
  const auto it = guides().find(n);
  return it == guides().end() ? communs() : it->second;
}

std::vector<std::string> logicielsDuGuide() {
  std::vector<std::string> v;
  for (const auto& [n, t] : guides()) v.push_back(n);
  return v;
}

std::string guideHtml(const std::string& logiciel) {
  const std::string n = nomGuide(logiciel);
  const std::string titre = n.empty() ? std::string("Prométhée pour les habitués d’un autre logiciel") : "Prométhée pour les habitués de " + n;
  std::string h = "<!DOCTYPE html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\"><title>" + echapper(titre) +
                  "</title>\n<style>body{font-family:sans-serif;margin:24px;color:#1d1f24}table{border-collapse:collapse;width:100%}th,td{border:1px solid #c8ccd4;padding:6px "
                  "8px;text-align:left;vertical-align:top}th{background:#eef0f4}td:first-child{white-space:nowrap}</style></head><body>\n";
  h += "<h1>" + echapper(titre) + "</h1>\n<table><tr><th>Dans l’ancien outil</th><th>Dans Prométhée</th><th>Geste, touche</th></tr>\n";
  auto lignes = [&h](const std::vector<TermeGuide>& v) {
    for (const auto& t : v) h += "<tr><td>" + echapper(t.ancien) + "</td><td>" + echapper(t.promethee) + "</td><td>" + echapper(t.geste) + "</td></tr>\n";
  };
  if (!n.empty()) lignes(guideAncienOutil(n));
  lignes(communs());
  h += "</table>\n</body></html>\n";
  return h;
}

}  // namespace prom::ecad

// Prométhée : import des documents Altium Designer (10.15, 10.18). Voir import_altium.hpp. Licence GPL-3.0-only.
#include "promethee/import_altium.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <numbers>
#include <set>
#include <sstream>

#include "cao_interne.hpp"
#include "promethee/archive.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/import_commun.hpp"

namespace prom::ecad {

namespace {
using Proprietes = std::map<std::string, std::string>;

// ---------- Textes et nombres ----------
std::string majuscules(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
std::string minuscules(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string latin1(const std::string& s) {
  std::string r;
  for (const char ch : s) {
    const auto u = static_cast<unsigned char>(ch);
    if (u < 0x80) r += ch;
    else r += static_cast<char>(0xC0 | (u >> 6)), r += static_cast<char>(0x80 | (u & 0x3F));
  }
  return r;
}
std::string rogner(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  return s.substr(i);
}
// « |CLÉ=VALEUR|… » : clés en majuscules ; valeurs en Latin-1, en UTF-8 sous une clé « %UTF8%… »
Proprietes proprietesTexte(const std::string& s) {
  Proprietes r;
  std::size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '|') ++i;
    const std::size_t fin = s.find('|', i);
    const std::string morceau = s.substr(i, fin == std::string::npos ? std::string::npos : fin - i);
    i = fin == std::string::npos ? s.size() : fin;
    const std::size_t egal = morceau.find('=');
    if (egal == std::string::npos) continue;
    const std::string cle = majuscules(rogner(morceau.substr(0, egal)));
    const std::string valeur = morceau.substr(egal + 1);
    if (cle.rfind("%UTF8%", 0) == 0) r[cle.substr(6)] = rogner(valeur);
    else r.emplace(cle, rogner(latin1(valeur)));
  }
  return r;
}
std::string texte(const Proprietes& p, const std::string& cle, const std::string& defaut = {}) {
  const auto it = p.find(cle);
  return it == p.end() ? defaut : it->second;
}
long entier(const Proprietes& p, const std::string& cle, long defaut = 0) {
  const auto it = p.find(cle);
  if (it == p.end() || it->second.empty()) return defaut;
  return std::strtol(it->second.c_str(), nullptr, 10);
}
double reel(const Proprietes& p, const std::string& cle, double defaut = 0) {
  const auto it = p.find(cle);
  if (it == p.end() || it->second.empty()) return defaut;
  std::istringstream in(it->second);
  in.imbue(std::locale::classic());
  double v = defaut;
  in >> v;
  return v;
}
bool booleen(const Proprietes& p, const std::string& cle, bool defaut = false) {
  const auto it = p.find(cle);
  if (it == p.end()) return defaut;
  return it->second == "T" || it->second == "TRUE";
}
// Schéma : la valeur entière est en dizaines de mils, la fraction (« _FRAC ») en dix-millièmes de mil
double milsSch(const Proprietes& p, const std::string& cle) { return 10.0 * static_cast<double>(entier(p, cle)) + static_cast<double>(entier(p, cle + "_FRAC")) / 1e4; }
double milsSch1(const Proprietes& p, const std::string& cle) { return 100.0 * static_cast<double>(entier(p, cle)) + static_cast<double>(entier(p, cle + "_FRAC1")) / 1e4; }
// Carte : « 123.4mil »
double milsCarte(const Proprietes& p, const std::string& cle) {
  std::string v = texte(p, cle);
  if (v.size() < 3 || minuscules(v.substr(v.size() - 3)) != "mil") return 0;
  v.resize(v.size() - 3);
  std::istringstream in(v);
  in.imbue(std::locale::classic());
  double x = 0;
  in >> x;
  return x;
}
Nm nmDeMils(double mils) { return static_cast<Nm>(std::llround(mils * 25'400.0)); }

// ---------- Lecteur binaire des flux ----------
struct Flux {
  const std::string& d;
  std::size_t p = 0, finSous = 0;
  std::size_t restants() const { return p < d.size() ? d.size() - p : 0; }
  void besoin(std::size_t n) const {
    if (p + n > d.size()) throw std::runtime_error("enregistrement Altium tronqué");
  }
  std::uint8_t u8() {
    besoin(1);
    return static_cast<std::uint8_t>(d[p++]);
  }
  std::uint16_t u16() {
    besoin(2);
    const std::uint16_t v = static_cast<std::uint16_t>(static_cast<unsigned char>(d[p]) | static_cast<unsigned char>(d[p + 1]) << 8);
    p += 2;
    return v;
  }
  std::uint32_t u32() {
    besoin(4);
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = v << 8 | static_cast<unsigned char>(d[p + static_cast<std::size_t>(i)]);
    p += 4;
    return v;
  }
  std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
  std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
  double f64() {
    besoin(8);
    double v = 0;
    std::memcpy(&v, d.data() + p, 8);
    p += 8;
    return v;
  }
  void sauter(std::size_t n) {
    besoin(n);
    p += n;
  }
  std::string pascal() {
    const std::size_t n = u8();
    besoin(n);
    std::string s = d.substr(p, n);
    p += n;
    return s;
  }
  std::size_t sous() {
    const std::size_t n = u32();
    finSous = p + n;
    if (finSous > d.size()) throw std::runtime_error("sous-enregistrement Altium hors du flux");
    return n;
  }
  std::size_t resteSous() const { return finSous > p ? finSous - p : 0; }
  void finirSous() { p = finSous; }
  // Propriétés : longueur sur 4 octets (octet haut non nul : données binaires, rendues dans « binaire »)
  Proprietes proprietes(std::string* binaire = nullptr) {
    std::uint32_t n = u32();
    const bool estBinaire = (n & 0xFF000000u) != 0;
    n &= 0x00FFFFFFu;
    besoin(n);
    std::string s = d.substr(p, n);
    p += n;
    if (estBinaire) {
      if (binaire) *binaire = s;
      return {};
    }
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return proprietesTexte(s);
  }
};
// Coordonnées de carte : dixièmes de micro-pouce (2,54 nm), y vers le haut
Nm unite(std::int32_t v) { return static_cast<Nm>(std::llround(static_cast<double>(v) * 2.54)); }

const std::string* flux(const std::vector<std::pair<std::string, std::string>>& f, const std::string& chemin) {
  const std::string c = minuscules(chemin);
  for (const auto& [k, v] : f)
    if (minuscules(k) == c) return &v;
  return nullptr;
}

// ======================================================================================================================
// Schéma
// ======================================================================================================================
struct BrocheSch {
  Pt connexion, corps;  // feuille, y vers le bas
  std::string nom, numero;
  TypeBroche type = TypeBroche::Passive;
  bool cachee = false;
  int partie = 0;
};
struct ComposantSch {
  std::string libref, description, ref, valeur, empreinte, uid;
  Pt position;
  int orientation = 0, partie = 1, parties = 1, mode = 0;
  bool miroir = false;
  std::map<std::string, std::string> parametres;
  std::vector<BrocheSch> broches;
  std::vector<Trace> traces;  // feuille
};
struct PortSch {
  std::string nom;
  Pt a, b;  // les deux bouts
  SensSignal sens = SensSignal::Aucun;
};
struct BlocSch {
  BlocFeuille bloc;
  std::string fichier;
};
struct FeuilleSch {
  std::string fichier;
  Feuille feuille;
  std::vector<ComposantSch> composants;
  std::vector<PortSch> ports;
  std::vector<std::pair<std::string, Pt>> etiquettes;  // étiquettes de réseau
  std::vector<std::pair<std::string, Pt>> alimentations;
  std::vector<BlocSch> blocs;
};
TypeBroche typeAltium(long e) {
  switch (e) {
    case 0: return TypeBroche::Entree;
    case 1: return TypeBroche::Bidirectionnelle;
    case 2: return TypeBroche::Sortie;
    case 3: return TypeBroche::CollecteurOuvert;
    case 4: return TypeBroche::Passive;
    case 5: return TypeBroche::TroisEtats;
    case 6: return TypeBroche::CollecteurOuvert;  // émetteur ouvert
    case 7: return TypeBroche::AlimEntree;
    default: return TypeBroche::NonSpecifiee;
  }
}
// Enregistrements d'un schéma, l'en-tête à part : SchDoc binaire (flux « FileHeader ») ou ASCII (une ligne chacun)
std::vector<Proprietes> enregistrementsSchema(const std::string& octets) {
  std::vector<Proprietes> r;
  if (estCfb(octets)) {
    const auto f = lireCfb(octets);
    const std::string* entete = flux(f, "FileHeader");
    if (!entete) throw std::runtime_error("schéma Altium sans flux « FileHeader »");
    Flux x{*entete};
    if (x.restants() > 0) x.proprietes();  // en-tête
    while (x.restants() >= 4) {
      std::string binaire;
      Proprietes p = x.proprietes(&binaire);
      r.push_back(std::move(p));
    }
  } else {
    std::istringstream in(octets);
    bool entete = true;
    for (std::string l; std::getline(in, l);) {
      if (!l.empty() && l.back() == '\r') l.pop_back();
      if (l.empty()) continue;
      if (entete) {
        entete = false;
        if (l.find("HEADER=") != std::string::npos) continue;
      }
      r.push_back(proprietesTexte(l));
    }
  }
  return r;
}

FeuilleSch lireFeuilleSch(const std::string& octets, const std::string& fichier, RapportImportAltium& rapport) {
  FeuilleSch fs;
  fs.fichier = fichier;
  const std::vector<Proprietes> recs = enregistrementsSchema(octets);
  // Hauteur de la page : le plus haut des points, la feuille d'ici ayant y vers le bas
  double hauteur = 0;
  for (const auto& p : recs) {
    for (const auto& [k, v] : p)
      if (k == "LOCATION.Y" || k == "CORNER.Y" || (k.size() >= 2 && k[0] == 'Y' && std::isdigit(static_cast<unsigned char>(k[1])) && k.find('_') == std::string::npos))
        hauteur = std::max(hauteur, 10.0 * static_cast<double>(std::strtol(v.c_str(), nullptr, 10)));
    if (entier(p, "RECORD", -1) == 31 && booleen(p, "USECUSTOMSHEET")) hauteur = std::max(hauteur, milsSch(p, "CUSTOMY"));
  }
  const double H = std::ceil((hauteur + 200) / 100) * 100;
  auto P = [&](double x, double y) { return Pt{nmDeMils(x), nmDeMils(H - y)}; };
  auto lieu = [&](const Proprietes& p) { return P(milsSch(p, "LOCATION.X"), milsSch(p, "LOCATION.Y")); };
  Feuille& f = fs.feuille;
  f.uuid = nouvelUuid();
  f.format = "A3";
  std::map<long, std::size_t> composantDe;  // indice d'enregistrement → composant
  std::map<long, std::size_t> blocDe;
  auto proprietaire = [&](long i) {
    // Remonte la chaîne des propriétaires jusqu'au composant (liste d'implémentations, etc.)
    for (int garde = 0; garde < 8 && i >= 0 && static_cast<std::size_t>(i) < recs.size(); ++garde) {
      if (composantDe.count(i)) return static_cast<long>(composantDe[i]);
      i = entier(recs[static_cast<std::size_t>(i)], "OWNERINDEX", -1);
    }
    return -1L;
  };
  auto partieVisible = [&](const Proprietes& p, const ComposantSch& c) {
    const long partie = entier(p, "OWNERPARTID", -1);
    return (partie <= 0 || partie == c.partie) && entier(p, "OWNERPARTDISPLAYMODE", 0) == c.mode;
  };
  for (std::size_t i = 0; i < recs.size(); ++i) {
    const Proprietes& p = recs[i];
    const long genre = entier(p, "RECORD", -1);
    const long proprio = entier(p, "OWNERINDEX", -1);
    switch (genre) {
      case 1: {
        ComposantSch c;
        c.libref = texte(p, "LIBREFERENCE");
        c.description = texte(p, "COMPONENTDESCRIPTION");
        c.uid = texte(p, "UNIQUEID");
        c.position = lieu(p);
        c.orientation = static_cast<int>(entier(p, "ORIENTATION") & 3);
        c.miroir = booleen(p, "ISMIRRORED");
        c.partie = static_cast<int>(std::max(1L, entier(p, "CURRENTPARTID", 1)));
        c.parties = static_cast<int>(std::max(1L, entier(p, "PARTCOUNT", 2) - 1));
        c.mode = static_cast<int>(entier(p, "DISPLAYMODE", 0));
        composantDe[static_cast<long>(i)] = fs.composants.size();
        fs.composants.push_back(c);
        break;
      }
      case 2: {
        const long k = proprietaire(proprio);
        if (k < 0) break;
        ComposantSch& c = fs.composants[static_cast<std::size_t>(k)];
        if (!partieVisible(p, c)) break;
        const long conglomerat = entier(p, "PINCONGLOMERATE");
        const double x = milsSch(p, "LOCATION.X"), y = milsSch(p, "LOCATION.Y"), l = milsSch(p, "PINLENGTH");
        double cx = x, cy = y;
        switch (conglomerat & 3) {
          case 0: cx += l; break;
          case 1: cy += l; break;
          case 2: cx -= l; break;
          default: cy -= l; break;
        }
        BrocheSch b;
        b.connexion = P(cx, cy);
        b.corps = P(x, y);
        b.nom = texte(p, "NAME");
        b.numero = texte(p, "DESIGNATOR");
        b.type = typeAltium(entier(p, "ELECTRICAL", 4));
        b.cachee = (conglomerat & 4) != 0;
        b.partie = static_cast<int>(std::max(0L, entier(p, "OWNERPARTID", 0)));
        c.broches.push_back(b);
        break;
      }
      case 34: {
        const long k = proprietaire(proprio);
        if (k >= 0) fs.composants[static_cast<std::size_t>(k)].ref = texte(p, "TEXT");
        break;
      }
      case 41: {
        const long k = proprietaire(proprio);
        if (k >= 0) fs.composants[static_cast<std::size_t>(k)].parametres[majuscules(texte(p, "NAME"))] = texte(p, "TEXT");
        break;
      }
      case 45: {
        const long k = proprietaire(proprio);
        if (k >= 0 && majuscules(texte(p, "MODELTYPE")) == "PCBLIB" && (booleen(p, "ISCURRENT") || fs.composants[static_cast<std::size_t>(k)].empreinte.empty()))
          fs.composants[static_cast<std::size_t>(k)].empreinte = texte(p, "MODELNAME");
        break;
      }
      case 6:
      case 7:
      case 13:
      case 14: {
        const long k = proprio >= 0 ? proprietaire(proprio) : -1;
        if (k < 0) break;  // dessins de la feuille : non repris
        ComposantSch& c = fs.composants[static_cast<std::size_t>(k)];
        if (!partieVisible(p, c)) break;
        Trace t;
        if (genre == 14) {
          t.forme = Trace::Forme::Rectangle;
          t.points = {lieu(p), P(milsSch(p, "CORNER.X"), milsSch(p, "CORNER.Y"))};
          t.fond = booleen(p, "ISSOLID");
        } else if (genre == 13) {
          t.points = {lieu(p), P(milsSch(p, "CORNER.X"), milsSch(p, "CORNER.Y"))};
        } else {
          const long n = entier(p, "LOCATIONCOUNT");
          for (long j = 1; j <= n; ++j) t.points.push_back(P(milsSch(p, "X" + std::to_string(j)), milsSch(p, "Y" + std::to_string(j))));
          t.ferme = genre == 7;
          t.fond = genre == 7 && booleen(p, "ISSOLID");
        }
        if (!t.points.empty()) c.traces.push_back(t);
        break;
      }
      case 27: {
        const long n = entier(p, "LOCATIONCOUNT");
        Pt avant;
        for (long j = 1; j <= n; ++j) {
          const Pt q = P(milsSch(p, "X" + std::to_string(j)), milsSch(p, "Y" + std::to_string(j)));
          if (j > 1 && !(q == avant)) f.fils.push_back({nouvelUuid(), avant, q});
          avant = q;
        }
        break;
      }
      case 25: fs.etiquettes.push_back({texte(p, "TEXT"), lieu(p)}); break;
      case 17: fs.alimentations.push_back({texte(p, "TEXT"), lieu(p)}); break;
      case 29: f.jonctions.push_back({lieu(p), nouvelUuid()}); break;
      case 22: f.nonConnexions.push_back({lieu(p), nouvelUuid()}); break;
      case 18: {
        PortSch s;
        s.nom = texte(p, "NAME");
        const double x = milsSch(p, "LOCATION.X"), y = milsSch(p, "LOCATION.Y"), l = milsSch(p, "WIDTH");
        const long style = entier(p, "STYLE");
        s.a = P(x, y);
        s.b = style >= 4 ? P(x, y - l) : P(x + l, y);
        const long io = entier(p, "IOTYPE");
        s.sens = io == 1 ? SensSignal::Sortie : io == 2 ? SensSignal::Entree : io == 3 ? SensSignal::Bidirectionnel : SensSignal::Aucun;
        fs.ports.push_back(s);
        break;
      }
      case 15: {
        BlocSch b;
        b.bloc.uuid = nouvelUuid();
        b.bloc.position = lieu(p);
        b.bloc.largeur = nmDeMils(milsSch(p, "XSIZE"));
        b.bloc.hauteur = nmDeMils(milsSch(p, "YSIZE"));
        blocDe[static_cast<long>(i)] = fs.blocs.size();
        fs.blocs.push_back(b);
        break;
      }
      case 16: {
        const auto it = blocDe.find(proprio);
        if (it == blocDe.end()) break;
        BlocFeuille& b = fs.blocs[it->second].bloc;
        PortBloc port;
        port.uuid = nouvelUuid();
        port.nom = texte(p, "NAME");
        const Nm d = nmDeMils(milsSch1(p, "DISTANCEFROMTOP"));
        const long cote = entier(p, "SIDE");
        port.cote = cote == 0 ? 0 : cote == 1 ? 1 : cote == 2 ? 2 : 3;
        port.p = cote == 0 ? Pt{b.position.x, b.position.y + d} : cote == 1 ? Pt{b.position.x + b.largeur, b.position.y + d} : cote == 2 ? Pt{b.position.x + d, b.position.y} : Pt{b.position.x + d, b.position.y + b.hauteur};
        const long io = entier(p, "IOTYPE");
        port.sens = io == 1 ? SensSignal::Sortie : io == 2 ? SensSignal::Entree : io == 3 ? SensSignal::Bidirectionnel : SensSignal::Aucun;
        b.ports.push_back(port);
        break;
      }
      case 32: {
        const auto it = blocDe.find(proprio);
        if (it != blocDe.end()) fs.blocs[it->second].bloc.nom = texte(p, "TEXT");
        break;
      }
      case 33: {
        const auto it = blocDe.find(proprio);
        if (it != blocDe.end()) fs.blocs[it->second].fichier = texte(p, "TEXT");
        break;
      }
      case 4: {
        if (proprio >= 0) break;
        TexteSchema t;
        t.uuid = nouvelUuid();
        t.texte = texte(p, "TEXT");
        t.p = lieu(p);
        if (!t.texte.empty()) f.textes.push_back(t);
        break;
      }
      case 26: rapport.alertes.push_back(fichier + " : bus dessiné non repris (ses étiquettes de membres relient les réseaux)"); break;
      default: break;
    }
  }
  // Commentaire : la valeur ; « =Value » renvoie au paramètre nommé
  for (auto& c : fs.composants) {
    std::string v = c.parametres.count("COMMENT") ? c.parametres["COMMENT"] : std::string();
    if (!v.empty() && v[0] == '=') {
      const std::string cle = majuscules(v.substr(1));
      v = c.parametres.count(cle) ? c.parametres[cle] : std::string();
    }
    if (v.empty() && c.parametres.count("VALUE")) v = c.parametres["VALUE"];
    c.valeur = v;
  }
  return fs;
}

// Repère du symbole : position, quarts de tour (sens direct), miroir avant la rotation (SymbolePlace)
Pt versSymbole(const ComposantSch& c, Pt feuille) {
  Pt q = tournerQuarts(feuille - c.position, -c.orientation);
  if (c.miroir) q.x = -q.x;
  return q;
}

DefSymbole symboleAlimentation() {
  DefSymbole d;
  d.id = "altium:alimentation";
  d.nom = "Port d’alimentation";
  d.categorie = "Importés d’Altium";
  d.prefixe = "#PWR";
  d.alimentation = true;
  Broche b;
  b.numero = "1";
  b.nom = "ALIM";
  b.type = TypeBroche::AlimEntree;
  b.direction = 3;
  b.longueur = 0;
  b.nomVisible = b.numeroVisible = false;
  d.broches = {b};
  Trace t;
  t.points = {ptMm(0, 0), ptMm(0, -1.27)};
  Trace barre;
  barre.points = {ptMm(-1.27, -1.27), ptMm(1.27, -1.27)};
  d.traces = {t, barre};
  return d;
}

// Assemblage des feuilles d'un projet : hiérarchie par les noms de fichiers des feuilles filles, portée des noms,
// composants réunis par repère, symboles dédoublonnés
void assemblerSchemas(ConceptionElec& c, std::vector<FeuilleSch>& feuilles, int portee, RapportImportAltium& r) {
  // Hiérarchie
  std::map<std::string, std::size_t> parFichier;
  for (std::size_t i = 0; i < feuilles.size(); ++i) {
    std::string n = minuscules(feuilles[i].fichier);
    if (const auto p = n.find_last_of("/\\"); p != std::string::npos) n = n.substr(p + 1);
    parFichier[n] = i;
  }
  // Fille d'un bloc : le premier fichier qu'il nomme (« a.SchDoc;b.SchDoc » : pages d'un même bloc, la première
  // retenue) ; parcours depuis les feuilles qu'aucun bloc ne vise, chaque feuille rattachée une seule fois, les renvois
  // croisés (une page qui vise sa mère ou une feuille déjà placée) laissés vides
  auto cible = [&](const BlocSch& b) -> std::optional<std::size_t> {
    std::string n = minuscules(b.fichier);
    if (const auto p = n.find(';'); p != std::string::npos) n = n.substr(0, p);
    if (const auto p = n.find_last_of("/\\"); p != std::string::npos) n = n.substr(p + 1);
    if (const auto it = parFichier.find(rogner(n)); it != parFichier.end()) return it->second;
    return std::nullopt;
  };
  std::set<std::size_t> visees;
  for (std::size_t i = 0; i < feuilles.size(); ++i)
    for (const auto& b : feuilles[i].blocs)
      if (const auto t = cible(b); t && *t != i) visees.insert(*t);
  std::vector<std::size_t> pile;
  for (std::size_t i = 0; i < feuilles.size(); ++i)
    if (!visees.count(i)) pile.push_back(i);
  if (pile.empty() && !feuilles.empty()) pile.push_back(0);
  std::set<std::size_t> placees(pile.begin(), pile.end()), filles;
  bool hierarchie = false;
  for (std::size_t k = 0; k < pile.size(); ++k) {
    const std::size_t i = pile[k];
    for (auto& b : feuilles[i].blocs) {
      const auto t = cible(b);
      if (!t) {
        if (!b.fichier.empty()) r.alertes.push_back("Feuille fille « " + b.fichier + " » absente du projet : son bloc reste vide");
        continue;
      }
      if (placees.count(*t)) {
        r.alertes.push_back(feuilles[i].fichier + " : renvoi vers « " + b.fichier + " », feuille déjà placée dans la hiérarchie, bloc laissé vide");
        continue;
      }
      if (b.fichier.find(';') != std::string::npos) r.alertes.push_back("Bloc à plusieurs pages « " + b.fichier + " » : la première devient sa feuille fille");
      b.bloc.feuille = feuilles[*t].feuille.uuid;
      placees.insert(*t);
      filles.insert(*t);
      pile.push_back(*t);
      hierarchie = true;
    }
  }
  bool ports = false;
  for (const auto& fs : feuilles) ports = ports || !fs.ports.empty();
  // Portée : 0 automatique (hiérarchique s'il y a des feuilles filles, à plat s'il y a des ports, globale sinon),
  // 1 à plat, 2 et 3 hiérarchique, 4 globale
  int mode = portee;
  if (mode == 0) mode = hierarchie ? 2 : ports ? 1 : 4;
  const TypeEtiquette typeEtiquette = mode == 4 ? TypeEtiquette::Globale : TypeEtiquette::Locale;
  c.symboles["altium:alimentation"] = symboleAlimentation();
  // Composants réunis par repère, toutes feuilles et parties confondues
  struct Reuni {
    ComposantElec composant;
    DefSymbole symbole;
    std::set<std::string> numeros;
  };
  std::map<std::string, Reuni> reunis;
  std::vector<std::string> ordre;
  for (std::size_t i = 0; i < feuilles.size(); ++i) {
    FeuilleSch& fs = feuilles[i];
    Feuille& f = fs.feuille;
    std::string nom = fs.fichier;
    if (const auto p = nom.find_last_of("/\\"); p != std::string::npos) nom = nom.substr(p + 1);
    if (const auto p = nom.find_last_of('.'); p != std::string::npos) nom = nom.substr(0, p);
    f.nom = nom.empty() ? "Feuille " + std::to_string(i + 1) : nom;
    for (const auto& [texteEtiquette, p] : fs.etiquettes) {
      Etiquette e;
      e.uuid = nouvelUuid();
      e.texte = texteEtiquette;
      e.p = p;
      e.type = typeEtiquette;
      f.etiquettes.push_back(e);
    }
    for (const auto& [nomAlim, p] : fs.alimentations) {
      SymbolePlace sp;
      sp.uuid = nouvelUuid();
      sp.symbole = "altium:alimentation";
      sp.nomAlim = nomAlim;
      sp.position = p;
      f.symboles.push_back(sp);
    }
    // Ports : une étiquette au bout relié à un fil (aux deux s'ils le sont tous deux), hiérarchique dans une feuille
    // fille, globale ailleurs
    std::set<Pt> bouts;
    for (const auto& w : f.fils) bouts.insert(w.a), bouts.insert(w.b);
    const TypeEtiquette typePort = mode == 2 && filles.count(i) ? TypeEtiquette::Hierarchique : TypeEtiquette::Globale;
    for (const auto& s : fs.ports) {
      std::vector<Pt> ou;
      if (bouts.count(s.a)) ou.push_back(s.a);
      if (bouts.count(s.b) && !(s.b == s.a)) ou.push_back(s.b);
      if (ou.empty()) ou.push_back(s.a);
      for (const Pt& q : ou) {
        Etiquette e;
        e.uuid = nouvelUuid();
        e.texte = s.nom;
        e.p = q;
        e.type = typePort;
        e.sens = s.sens;
        f.etiquettes.push_back(e);
      }
    }
    for (auto& b : fs.blocs) f.blocs.push_back(b.bloc);
    for (const ComposantSch& x : fs.composants) {
      if (x.ref.empty()) {
        r.alertes.push_back(f.nom + " : composant « " + x.libref + " » sans désignateur, ignoré");
        continue;
      }
      auto [it, neuf] = reunis.try_emplace(x.ref);
      Reuni& u = it->second;
      if (neuf) {
        ordre.push_back(x.ref);
        u.composant.uuid = nouvelUuid();
        u.composant.reference = x.ref;
        u.composant.valeur = x.valeur;
        u.composant.empreinte = x.empreinte.empty() ? std::string() : "altium:" + x.empreinte;
        u.composant.description = x.description;
        for (const auto& [k, v] : x.parametres)
          if (k != "COMMENT" && !v.empty() && v[0] != '=') u.composant.champs[k] = v;
        u.symbole.nom = x.libref.empty() ? x.ref : x.libref;
        u.symbole.description = x.description;
        u.symbole.categorie = "Importés d’Altium";
        std::string prefixe;
        for (const char ch : x.ref)
          if (std::isalpha(static_cast<unsigned char>(ch))) prefixe += ch;
          else break;
        u.symbole.prefixe = prefixe.empty() ? "U" : prefixe;
        u.symbole.unites = x.parties;
      }
      u.symbole.unites = std::max(u.symbole.unites, x.parties);
      if (u.composant.empreinte.empty() && !x.empreinte.empty()) u.composant.empreinte = "altium:" + x.empreinte;
      for (const BrocheSch& b : x.broches) {
        const int unite = u.symbole.unites > 1 ? b.partie : 0;
        if (!u.numeros.insert(b.numero + "#" + std::to_string(unite)).second) continue;
        Broche nb;
        nb.numero = b.numero;
        nb.nom = b.nom;
        nb.type = b.type;
        nb.cachee = b.cachee;
        nb.unite = unite;
        nb.extremite = versSymbole(x, b.connexion);
        const Pt corps = versSymbole(x, b.corps);
        const Pt v = corps - nb.extremite;
        nb.longueur = std::max(std::abs(v.x), std::abs(v.y));
        nb.direction = std::abs(v.x) >= std::abs(v.y) ? (v.x >= 0 ? 0 : 2) : (v.y >= 0 ? 1 : 3);
        u.symbole.broches.push_back(nb);
      }
      for (Trace t : x.traces) {
        for (Pt& q : t.points) q = versSymbole(x, q);
        t.unite = u.symbole.unites > 1 ? x.partie : 0;
        if (std::find(u.symbole.traces.begin(), u.symbole.traces.end(), t) == u.symbole.traces.end()) u.symbole.traces.push_back(t);
      }
      SymbolePlace sp;
      sp.uuid = nouvelUuid();
      sp.composant = u.composant.uuid;
      sp.position = x.position;
      sp.rotation = x.orientation;
      sp.miroir = x.miroir;
      sp.unite = u.symbole.unites > 1 ? x.partie : 1;
      sp.symbole = "#" + x.ref;  // provisoire : remplacé par le symbole dédoublonné
      f.symboles.push_back(sp);
    }
  }
  // Symboles dédoublonnés : un même dessin et les mêmes broches partagent une définition, nommée d'après la référence
  // de bibliothèque d'Altium
  std::vector<DefSymbole> uniques;
  std::map<std::string, std::string> idDe;  // repère → identifiant retenu
  std::map<std::string, int> variantes;
  for (const std::string& ref : ordre) {
    Reuni& u = reunis[ref];
    DefSymbole d = u.symbole;
    std::string trouve;
    for (const DefSymbole& e : uniques) {
      DefSymbole a = e;
      a.id.clear();
      if (a == d) trouve = e.id;
    }
    if (trouve.empty()) {
      const int k = ++variantes[d.nom];
      d.id = "altium:" + d.nom + (k > 1 ? "~" + std::to_string(k) : "");
      trouve = d.id;
      uniques.push_back(d);
    }
    idDe[ref] = trouve;
    u.composant.symbole = trouve;
    c.composants.push_back(u.composant);
  }
  for (DefSymbole& d : uniques) c.symboles[d.id] = d;
  for (auto& fs : feuilles)
    for (auto& sp : fs.feuille.symboles)
      if (!sp.symbole.empty() && sp.symbole[0] == '#') sp.symbole = idDe[sp.symbole.substr(1)];
  // Feuilles : les racines d'abord, dans l'ordre du projet
  for (std::size_t i = 0; i < feuilles.size(); ++i)
    if (!filles.count(i)) c.feuilles.push_back(feuilles[i].feuille);
  for (std::size_t i = 0; i < feuilles.size(); ++i)
    if (filles.count(i)) c.feuilles.push_back(feuilles[i].feuille);
  r.feuilles = static_cast<int>(c.feuilles.size());
  r.composants = static_cast<int>(c.composants.size());
}

// ======================================================================================================================
// Carte
// ======================================================================================================================
struct PadA {
  std::string nom;
  int composant = -1, reseau = -1, couche = 1, forme = 1, formeAlt = 0, trou = 0, arrondi = 0;
  Pt p;
  Nm l = 0, h = 0, percage = 0, fente = 0;
  double angle = 0;
  bool metallise = true;
};
struct ComposantA {
  std::string ref, motif, commentaire, uid;
  Pt p;
  double angle = 0;
  bool dessous = false;
};
struct TraitA {
  int composant = -1, reseau = -1, couche = 1;
  Pt a, b;
  std::optional<Pt> milieu;
  Nm largeur = 0;
  bool contourZone = false;
};
struct CarteA {
  std::vector<std::string> reseaux;
  std::vector<ComposantA> composants;
  std::vector<PadA> pads;
  std::vector<TraitA> traits;
  std::vector<Via> vias;
  std::vector<std::pair<int, Via>> viasReseau;  // indice de réseau, via
  std::vector<Zone> zones;
  std::vector<std::pair<int, std::size_t>> zonesReseau;
  Polygone contour;
  Polygones decoupes;
};
constexpr int SANS = 0xFFFF;
int indice(std::uint16_t v) { return v == SANS ? -1 : static_cast<int>(v); }
int coucheDeNom(const std::string& n) {
  const std::string u = majuscules(n);
  if (u == "TOP") return 1;
  if (u == "BOTTOM") return 32;
  if (u.rfind("MID", 0) == 0) return 1 + std::atoi(u.c_str() + 3);
  if (u.rfind("PLANE", 0) == 0) return 38 + std::atoi(u.c_str() + 5);
  if (u == "TOPOVERLAY") return 33;
  if (u == "BOTTOMOVERLAY") return 34;
  if (u == "MULTILAYER") return 74;
  if (u == "KEEPOUT") return 56;
  return 0;
}
bool estCuivreAltium(int c) { return (c >= 1 && c <= 32) || (c >= 39 && c <= 54); }
Pt pointCarte(Flux& x) {
  const Nm a = unite(x.i32());
  const Nm b = unite(x.i32());
  return {a, -b};
}
// Contour d'un polygone de propriétés (VX0, VY0, KIND0, CX0, CY0, R0, SA0, EA0…), arcs découpés
Polygone sommets(const Proprietes& p) {
  Polygone r;
  for (int i = 0;; ++i) {
    const std::string s = std::to_string(i);
    if (!p.count("VX" + s) || !p.count("VY" + s)) break;
    const Pt v{nmDeMils(milsCarte(p, "VX" + s)), -nmDeMils(milsCarte(p, "VY" + s))};
    if (entier(p, "KIND" + s) != 0) {
      const Pt c{nmDeMils(milsCarte(p, "CX" + s)), -nmDeMils(milsCarte(p, "CY" + s))};
      const double rayon = static_cast<double>(nmDeMils(milsCarte(p, "R" + s)));
      double sa = reel(p, "SA" + s), ea = reel(p, "EA" + s);
      while (ea <= sa) ea += 360;
      auto point = [&](double a) {
        const double t = a * std::numbers::pi / 180;
        return Pt{c.x + static_cast<Nm>(std::llround(std::cos(t) * rayon)), c.y - static_cast<Nm>(std::llround(std::sin(t) * rayon))};
      };
      const int n = std::max(2, static_cast<int>(std::ceil((ea - sa) / 10)));
      std::vector<Pt> arc;
      for (int k = 0; k <= n; ++k) arc.push_back(point(sa + (ea - sa) * k / n));
      // L'arc part du bout le plus proche du sommet
      if (distance(arc.back(), v) < distance(arc.front(), v)) std::reverse(arc.begin(), arc.end());
      for (const Pt& q : arc)
        if (r.empty() || !(r.back() == q)) r.push_back(q);
    } else if (r.empty() || !(r.back() == v))
      r.push_back(v);
  }
  if (r.size() > 1 && r.front() == r.back()) r.pop_back();
  return r;
}
// Enregistrements binaires d'une carte ou d'une empreinte de bibliothèque
PadA lirePad(Flux& x) {
  PadA p;
  if (x.u8() != 2) throw std::runtime_error("pastille Altium : genre d'enregistrement inattendu");
  x.sous();
  p.nom = latin1(x.pascal());
  x.finirSous();
  for (int k = 0; k < 3; ++k) {
    x.sous();
    x.finirSous();
  }
  const std::size_t n5 = x.sous();
  if (n5 < 110) throw std::runtime_error("pastille Altium : sous-enregistrement trop court");
  p.couche = x.u8();
  x.u8();
  x.u8();
  p.reseau = indice(x.u16());
  x.sauter(2);
  p.composant = indice(x.u16());
  x.sauter(4);
  p.p = pointCarte(x);
  p.l = unite(x.i32());
  p.h = unite(x.i32());
  x.sauter(16);  // tailles du milieu et du dessous
  p.percage = unite(x.i32());
  p.forme = x.u8();
  x.u8();
  x.u8();
  p.angle = x.f64();
  p.metallise = x.u8() != 0;
  x.finirSous();
  const std::size_t n6 = x.sous();
  if (n6 >= 596) {
    x.sauter(4 * 29 * 2 + 29 + 1);  // tailles et formes des couches internes, octet inconnu
    p.trou = x.u8();
    p.fente = unite(x.i32());
    x.f64();  // angle de la fente
    x.sauter(4 * 32 * 2 + 1);
    // Formes de remplacement et rayons des coins, couche 0 (dessus) en tête
    p.formeAlt = x.u8();
    x.sauter(31);
    p.arrondi = x.u8();
  }
  x.finirSous();
  return p;
}
TraitA lirePiste(Flux& x) {
  TraitA t;
  x.u8();
  x.sous();
  t.couche = x.u8();
  const std::uint8_t drapeaux = x.u8();
  t.contourZone = (drapeaux & 0x02) != 0;
  x.u8();
  t.reseau = indice(x.u16());
  x.u16();
  t.composant = indice(x.u16());
  x.sauter(4);
  t.a = pointCarte(x);
  t.b = pointCarte(x);
  t.largeur = unite(x.i32());
  x.finirSous();
  return t;
}
TraitA lireArc(Flux& x) {
  TraitA t;
  x.u8();
  x.sous();
  t.couche = x.u8();
  const std::uint8_t drapeaux = x.u8();
  t.contourZone = (drapeaux & 0x02) != 0;
  x.u8();
  t.reseau = indice(x.u16());
  x.u16();
  t.composant = indice(x.u16());
  x.sauter(4);
  const Pt c = pointCarte(x);
  const double rayon = static_cast<double>(unite(x.i32()));
  double sa = x.f64(), ea = x.f64();
  t.largeur = unite(x.i32());
  x.finirSous();
  while (ea <= sa) ea += 360;
  auto point = [&](double a) {
    const double r = a * std::numbers::pi / 180;
    return Pt{c.x + static_cast<Nm>(std::llround(std::cos(r) * rayon)), c.y - static_cast<Nm>(std::llround(std::sin(r) * rayon))};
  };
  t.a = point(sa);
  t.b = point(ea);
  t.milieu = point((sa + ea) / 2);
  return t;
}
Via lireVia(Flux& x, int& reseau) {
  Via v;
  x.u8();
  x.sous();
  x.u8();
  x.u8();
  x.u8();
  reseau = indice(x.u16());
  x.u16();
  x.u16();
  x.sauter(4);
  v.p = pointCarte(x);
  v.diametre = unite(x.i32());
  v.percage = unite(x.i32());
  const int de = x.u8(), vers = x.u8();
  x.finirSous();
  auto couche = [](int a) { return a == 1 ? Couche::CuivreDessus : a == 32 ? Couche::CuivreDessous : Couche::CuivreDessus; };
  v.de = couche(de);
  v.vers = vers == 32 || vers == 0 ? Couche::CuivreDessous : couche(vers);
  if (!(de == 1 && (vers == 32 || vers == 0))) v.type = TypeVia::Borgne;
  return v;
}
// Texte : le commentaire et le désignateur d'un composant
struct TexteA {
  int composant = -1;
  bool commentaire = false, designateur = false;
  std::string texte;
};
TexteA lireTexte(Flux& x) {
  TexteA t;
  x.u8();
  const std::size_t n1 = x.sous();
  x.u8();
  x.sauter(6);
  t.composant = indice(x.u16());
  if (n1 >= 123) {
    x.sauter(4);
    pointCarte(x);
    x.i32();
    x.u16();
    x.f64();
    x.u8();
    x.i32();
    t.commentaire = x.u8() != 0;
    t.designateur = x.u8() != 0;
  }
  x.finirSous();
  x.sous();
  if (x.resteSous() > 0) t.texte = latin1(x.pascal());
  x.finirSous();
  return t;
}
// Région (cuivre, découpe de la carte) et surface pleine
struct RegionA {
  int couche = 0, reseau = -1, polygone = -1, composant = -1;
  bool decoupe = false, cuivre = false;
  Polygone contour;
};
RegionA lireRegion(Flux& x, bool etendue) {
  RegionA g;
  x.u8();
  x.sous();
  g.couche = x.u8();
  x.u8();
  x.u8();
  g.reseau = indice(x.u16());
  g.polygone = indice(x.u16());
  g.composant = indice(x.u16());
  x.sauter(5);
  const std::uint16_t trous = x.u16();
  x.sauter(2);
  const Proprietes p = x.proprietes();
  const long genre = entier(p, "KIND");
  g.decoupe = booleen(p, "ISBOARDCUTOUT");
  g.cuivre = genre == 0 && !g.decoupe;
  std::uint32_t n = x.u32();
  if (etendue) ++n;
  for (std::uint32_t i = 0; i < n; ++i) {
    if (etendue) {
      x.u8();
      const Pt q = pointCarte(x);
      pointCarte(x);
      x.i32();
      x.f64();
      x.f64();
      if (g.contour.empty() || !(g.contour.back() == q)) g.contour.push_back(q);
    } else {
      const double vx = x.f64(), vy = x.f64();
      g.contour.push_back({static_cast<Nm>(std::llround(vx * 2.54)), -static_cast<Nm>(std::llround(vy * 2.54))});
    }
  }
  for (std::uint16_t k = 0; k < trous; ++k) {
    const std::uint32_t m = x.u32();
    x.sauter(16 * static_cast<std::size_t>(m));
  }
  x.finirSous();
  if (g.contour.size() > 1 && g.contour.front() == g.contour.back()) g.contour.pop_back();
  return g;
}
RegionA lireSurface(Flux& x) {
  RegionA g;
  x.u8();
  x.sous();
  g.couche = x.u8();
  x.u8();
  x.u8();
  g.reseau = indice(x.u16());
  x.sauter(2);
  g.composant = indice(x.u16());
  x.sauter(4);
  const Pt a = pointCarte(x);
  const Pt b = pointCarte(x);
  const double angle = x.f64();
  x.finirSous();
  g.cuivre = true;
  const Pt c{(a.x + b.x) / 2, (a.y + b.y) / 2};
  const Nm l = std::abs(b.x - a.x) / 2, h = std::abs(b.y - a.y) / 2;
  for (const Pt& q : {Pt{-l, -h}, Pt{l, -h}, Pt{l, h}, Pt{-l, h}}) g.contour.push_back(tournerDegres(q, angle) + c);
  return g;
}
void sauterSimple(Flux& x) {
  x.u8();
  x.sous();
  x.finirSous();
}

CarteA lireCarteA(const std::vector<std::pair<std::string, std::string>>& f, RapportImportAltium& r) {
  CarteA c;
  if (const std::string* b = flux(f, "Board6/Data")) {
    Flux x{*b};
    if (x.restants() >= 4) c.contour = sommets(x.proprietes());
  }
  if (const std::string* n = flux(f, "Nets6/Data")) {
    Flux x{*n};
    while (x.restants() >= 4) c.reseaux.push_back(texte(x.proprietes(), "NAME"));
  }
  if (const std::string* n = flux(f, "Components6/Data")) {
    Flux x{*n};
    while (x.restants() >= 4) {
      const Proprietes p = x.proprietes();
      ComposantA k;
      k.ref = texte(p, "SOURCEDESIGNATOR");
      k.motif = texte(p, "PATTERN");
      k.uid = texte(p, "SOURCEUNIQUEID");
      k.p = {nmDeMils(milsCarte(p, "X")), -nmDeMils(milsCarte(p, "Y"))};
      k.angle = reel(p, "ROTATION");
      k.dessous = majuscules(texte(p, "LAYER")) == "BOTTOM";
      c.composants.push_back(k);
    }
  }
  auto parcourir = [&](const char* nom, const std::function<void(Flux&)>& lire) {
    const std::string* d = flux(f, std::string(nom) + "/Data");
    if (!d) return;
    Flux x{*d};
    try {
      while (x.restants() > 4) lire(x);
    } catch (const std::exception& e) {
      r.alertes.push_back(std::string(nom) + " : lecture arrêtée (" + e.what() + ")");
    }
  };
  parcourir("Pads6", [&](Flux& x) { c.pads.push_back(lirePad(x)); });
  parcourir("Tracks6", [&](Flux& x) { c.traits.push_back(lirePiste(x)); });
  parcourir("Arcs6", [&](Flux& x) { c.traits.push_back(lireArc(x)); });
  parcourir("Vias6", [&](Flux& x) {
    int n = -1;
    Via v = lireVia(x, n);
    c.viasReseau.push_back({n, v});
  });
  parcourir("Texts6", [&](Flux& x) {
    const TexteA t = lireTexte(x);
    if (t.commentaire && t.composant >= 0 && static_cast<std::size_t>(t.composant) < c.composants.size() && !t.texte.empty() && t.texte[0] != '.')
      c.composants[static_cast<std::size_t>(t.composant)].commentaire = t.texte;
  });
  if (const std::string* d = flux(f, "Polygons6/Data")) {
    Flux x{*d};
    while (x.restants() >= 4) {
      const Proprietes p = x.proprietes();
      const int couche = coucheDeNom(texte(p, "LAYER"));
      if (!estCuivreAltium(couche) || majuscules(texte(p, "POLYGONTYPE", "Polygon")) == "SPLITPLANE") continue;
      Zone z;
      z.contour = sommets(p);
      if (z.contour.size() < 3) continue;
      c.zonesReseau.push_back({static_cast<int>(entier(p, "NET", SANS)), c.zones.size()});
      z.couche = couche == 1 ? Couche::CuivreDessus : couche == 32 ? Couche::CuivreDessous : Couche::CuivreInt1;
      z.priorite = static_cast<int>(entier(p, "POURINDEX"));
      c.zones.push_back(z);
      // Couche interne retenue plus bas, avec les autres
      c.zones.back().nom = std::to_string(couche);
    }
  }
  // Surfaces pleines et régions de cuivre de la carte : des zones prioritaires ; découpes de la carte ; ce qui appartient
  // à une empreinte (prolongement de pastille) ou à une zone (ses morceaux) n'est pas repris
  int surfaces = 0;
  auto garder = [&](const RegionA& g) {
    if (g.decoupe && g.contour.size() >= 3) {
      c.decoupes.push_back(g.contour);
      return;
    }
    if (!g.cuivre || g.composant >= 0 || g.polygone >= 0 || !estCuivreAltium(g.couche) || g.contour.size() < 3) return;
    Zone z;
    z.contour = g.contour;
    z.priorite = 100;
    z.nom = std::to_string(g.couche);
    c.zonesReseau.push_back({g.reseau, c.zones.size()});
    c.zones.push_back(z);
    ++surfaces;
  };
  parcourir("Fills6", [&](Flux& x) { garder(lireSurface(x)); });
  parcourir("Regions6", [&](Flux& x) { garder(lireRegion(x, false)); });
  parcourir("ShapeBasedRegions6", [&](Flux& x) { garder(lireRegion(x, true)); });
  if (surfaces) r.alertes.push_back(std::to_string(surfaces) + " surface(s) et région(s) de cuivre reprises en zones prioritaires (remplies à nouveau ici)");
  return c;
}

// Couches de cuivre utilisées : dessus, internes (dans l'ordre des numéros d'Altium), dessous
struct Couches {
  std::map<int, Couche> table;
  int cuivres = 2;
  Couche de(int a, Couche defaut = Couche::CuivreDessus) const {
    const auto it = table.find(a);
    return it == table.end() ? defaut : it->second;
  }
};
Couches couchesDe(const CarteA& c) {
  std::set<int> internes;
  for (const auto& t : c.traits)
    if (estCuivreAltium(t.couche) && t.couche != 1 && t.couche != 32) internes.insert(t.couche);
  for (const auto& z : c.zones)
    if (const int a = std::atoi(z.nom.c_str()); estCuivreAltium(a) && a != 1 && a != 32) internes.insert(a);
  Couches r;
  r.table[1] = Couche::CuivreDessus;
  r.table[32] = Couche::CuivreDessous;
  int k = 0;
  for (const int a : internes) r.table[a] = cuivreInterne(std::min(++k, MAX_CUIVRES - 2));
  r.cuivres = 2 + k;
  return r;
}

// Pastille dans le repère de l'empreinte posée à « e »
Pastille pastilleLocale(const PadA& a, const EmpreintePlacee& e) {
  Pastille p;
  p.numero = a.nom;
  p.position = versEmpreinte(e, a.p);
  p.largeur = a.l;
  p.hauteur = a.h;
  p.rotation = e.dessous ? e.rotation - a.angle : a.angle - e.rotation;
  p.rotation = std::fmod(std::fmod(p.rotation, 360) + 360, 360);
  if (std::abs(p.rotation - 360) < 1e-6) p.rotation = 0;
  p.percage = a.percage;
  p.type = a.percage > 0 ? (a.metallise ? TypePastille::Traversante : TypePastille::NonMetallisee) : TypePastille::CMS;
  if (a.formeAlt == 9 && a.forme == 2) {
    p.forme = FormePastille::RectArrondi;
    p.arrondi = std::clamp(a.arrondi / 200.0, 0.0, 0.5);
  } else if (a.forme == 1)
    p.forme = a.l == a.h ? FormePastille::Rond : FormePastille::Ovale;
  else if (a.forme == 3) {
    p.forme = FormePastille::Chanfreine;
    p.chanfrein = 0.2929;
    p.coinsChanfrein = 0xF;
  } else
    p.forme = FormePastille::Rect;
  if (a.trou == 2 && a.fente > a.percage) p.longueurPercage = a.fente;
  // Face : une CMS du dessus d'une empreinte du dessous est sur sa face propre
  if (p.type == TypePastille::CMS) {
    const bool faceComposant = (a.couche == 1 && !e.dessous) || (a.couche == 32 && e.dessous);
    if (!faceComposant) p.couches = bit(Couche::CuivreDessous) | bit(Couche::VernisDessous) | bit(Couche::PateDessous);
  }
  return p;
}
Couche coucheLocale(int a, bool dessous) {
  const bool dessus = a == 33 || a == 1 || a == 35 || a == 37;
  const bool basse = a == 34 || a == 32 || a == 36 || a == 38;
  if (a == 33 || a == 34) return (dessus != dessous) ? Couche::SerigraphieDessus : Couche::SerigraphieDessous;
  if (dessus || basse) return (dessus != dessous) ? Couche::FabricationDessus : Couche::FabricationDessous;
  return Couche::FabricationDessus;
}
DefEmpreinte cadrer(DefEmpreinte d) {
  Boite b;
  for (const auto& p : d.pastilles) b.etendre(boite(formePastille(p)));
  for (const auto& t : d.traits) b.etendre(t.a), b.etendre(t.b);
  if (b.vide()) b = Boite{-NM_PAR_MM, -NM_PAR_MM, NM_PAR_MM, NM_PAR_MM};
  d.encombrement = b.gonflee(250'000);
  d.corps.largeur = d.encombrement.largeur();
  d.corps.profondeur = d.encombrement.hauteur();
  d.corps.hauteur = NM_PAR_MM;
  d.corps.decalage = d.encombrement.centre();
  return d;
}

ConceptionElec conceptionDeCarte(CarteA a, const std::string& nom, RapportImportAltium& r) {
  const Couches couches = couchesDe(a);
  // Pastilles libres : sans réseau ni trou (logo, motif dessiné en pastilles), un dessin de cuivre ; les autres
  // (points de test, couture), une pièce de la carte chacune, « PL1 », « PL2 »…
  int libres = 0;
  std::vector<DessinCircuit> cuivreLibre;
  std::vector<PadA> gardees;
  for (PadA& p : a.pads) {
    const bool libre = p.composant < 0 || static_cast<std::size_t>(p.composant) >= a.composants.size();
    if (libre && p.reseau < 0 && p.percage == 0) {
      DessinCircuit d;
      d.forme = DessinCircuit::Forme::Polygone;
      d.points = formePastille(pastilleLocale(p, EmpreintePlacee{}));
      d.plein = true;
      d.epaisseur = 0;
      for (const Couche k : p.couche == 74 ? std::vector<Couche>{Couche::CuivreDessus, Couche::CuivreDessous} : std::vector<Couche>{p.couche == 32 ? Couche::CuivreDessous : Couche::CuivreDessus}) {
        d.couche = k;
        cuivreLibre.push_back(d);
      }
      continue;
    }
    gardees.push_back(p);
    if (libre) {
      PadA& g = gardees.back();
      ComposantA k;
      k.ref = "PL" + std::to_string(++libres);
      k.motif = "pastille-libre";
      k.p = g.p;
      g.composant = static_cast<int>(a.composants.size());
      if (g.nom.empty()) g.nom = "1";
      a.composants.push_back(k);
    }
  }
  a.pads = std::move(gardees);
  if (libres) r.alertes.push_back(std::to_string(libres) + " pastille(s) libre(s) reprise(s) en pièces de la carte (PL1…)");
  if (!cuivreLibre.empty()) r.alertes.push_back(std::to_string(cuivreLibre.size()) + " pastille(s) sans réseau ni composant (motif dessiné) reprise(s) en dessins de cuivre");
  cao_interne::CarteLue l;
  l.cuivres = couches.cuivres;
  auto nomReseau = [&](int i) { return i >= 0 && static_cast<std::size_t>(i) < a.reseaux.size() ? a.reseaux[static_cast<std::size_t>(i)] : std::string(); };
  // Empreintes : une par composant, dans son repère, puis dédoublonnées par motif
  std::vector<DefEmpreinte> parComposant(a.composants.size());
  std::vector<EmpreintePlacee> poses(a.composants.size());
  for (std::size_t i = 0; i < a.composants.size(); ++i) {
    poses[i].position = a.composants[i].p;
    poses[i].rotation = a.composants[i].angle;
    poses[i].dessous = a.composants[i].dessous;
  }
  for (const PadA& p : a.pads) {
    const std::size_t k = static_cast<std::size_t>(p.composant);
    parComposant[k].pastilles.push_back(pastilleLocale(p, poses[k]));
    const std::string n = nomReseau(p.reseau);
    if (!n.empty() && !p.nom.empty()) l.netlist.reseaux[n].push_back({a.composants[k].ref, p.nom});
  }
  for (const TraitA& t : a.traits) {
    if (t.composant >= 0 && static_cast<std::size_t>(t.composant) < a.composants.size()) {
      if (estCuivreAltium(t.couche)) continue;  // cuivre propre à l'empreinte : non repris
      if (t.couche != 33 && t.couche != 34) continue;
      const std::size_t k = static_cast<std::size_t>(t.composant);
      TraitEmpreinte x;
      x.couche = coucheLocale(t.couche, poses[k].dessous);
      x.a = versEmpreinte(poses[k], t.a);
      x.b = versEmpreinte(poses[k], t.b);
      x.epaisseur = t.largeur;
      parComposant[k].traits.push_back(x);
      continue;
    }
    if (!estCuivreAltium(t.couche) || t.contourZone) continue;
    Piste p;
    p.reseau = nomReseau(t.reseau);
    p.couche = couches.de(t.couche);
    p.a = t.a;
    p.b = t.b;
    p.milieu = t.milieu;
    p.largeur = std::max<Nm>(t.largeur, 50'000);
    l.pistes.push_back(p);
  }
  for (const auto& [n, v] : a.viasReseau) {
    Via x = v;
    x.reseau = nomReseau(n);
    l.vias.push_back(x);
  }
  // Dédoublonnage : un même motif aux mêmes pastilles partage la définition « altium:<motif> »
  std::map<std::string, std::vector<std::string>> variantes;  // motif → identifiants
  for (std::size_t i = 0; i < a.composants.size(); ++i) {
    const ComposantA& k = a.composants[i];
    DefEmpreinte d = parComposant[i];
    std::sort(d.pastilles.begin(), d.pastilles.end(), [](const Pastille& x, const Pastille& y) { return std::tie(x.numero, x.position) < std::tie(y.numero, y.position); });
    std::string id;
    for (const std::string& v : variantes[k.motif]) {
      DefEmpreinte e = l.empreintes[v];
      e.id.clear();
      DefEmpreinte g = d;
      g.id.clear();
      // Tolérance d'arrondi des angles quelconques : positions à 2 nm près
      bool pareil = e.pastilles.size() == g.pastilles.size();
      for (std::size_t j = 0; pareil && j < g.pastilles.size(); ++j) {
        const Pastille& x = e.pastilles[j];
        const Pastille& y = g.pastilles[j];
        pareil = x.numero == y.numero && std::abs(x.position.x - y.position.x) <= 20 && std::abs(x.position.y - y.position.y) <= 20 && x.largeur == y.largeur && x.hauteur == y.hauteur &&
                 x.forme == y.forme && x.percage == y.percage && std::abs(std::remainder(x.rotation - y.rotation, 180.0)) < 0.01;
      }
      if (pareil) id = v;
    }
    if (id.empty()) {
      id = k.motif.empty() ? "sans-motif-" + std::to_string(i) : k.motif;
      if (!variantes[k.motif].empty()) id += "~" + std::to_string(variantes[k.motif].size() + 1);
      d.description = k.motif;
      l.empreintes[id] = cadrer(d);
      variantes[k.motif].push_back(id);
    }
    cao_interne::CarteLue::Pose pose;
    pose.ref = k.ref;
    pose.decal = id;
    pose.p = k.p;
    pose.rotation = k.angle;
    pose.dessous = k.dessous;
    l.poses.push_back(pose);
    l.netlist.composants.push_back({k.ref, k.commentaire, id});
  }
  l.contour = a.contour;
  RapportLectureCao rl;
  ConceptionElec c = cao_interne::assemblerCarte(l, nom, "altium:", rl);
  for (const Polygone& q : a.decoupes) c.circuit.contour.decoupes.push_back(orienter(q));
  for (DessinCircuit& d : cuivreLibre) {
    d.uuid = nouvelUuid();
    c.circuit.dessins.push_back(d);
  }
  // Zones de cuivre
  for (std::size_t z = 0; z < a.zones.size(); ++z) {
    Zone x = a.zones[z];
    x.uuid = nouvelUuid();
    x.couche = couches.de(std::atoi(x.nom.c_str()), x.couche);
    x.nom.clear();
    for (const auto& [n, k] : a.zonesReseau)
      if (k == z) x.reseau = nomReseau(n);
    c.circuit.zones.push_back(x);
  }
  r.composants = rl.composants;
  r.reseaux = rl.reseaux;
  r.pistes = rl.pistes;
  r.vias = rl.vias;
  r.zones = static_cast<int>(a.zones.size());
  r.empreintes = static_cast<int>(l.empreintes.size());
  r.alertes.insert(r.alertes.end(), rl.alertes.begin(), rl.alertes.end());
  return c;
}

// PrjPcb : documents dans l'ordre, portée des noms
struct ProjetA {
  std::vector<std::string> documents;
  int portee = 0;
};
ProjetA lireProjet(const std::string& t) {
  ProjetA p;
  std::istringstream in(t);
  std::string section;
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (!l.empty() && l.front() == '[') section = l;
    const auto e = l.find('=');
    if (e == std::string::npos) continue;
    const std::string cle = majuscules(rogner(l.substr(0, e))), valeur = rogner(l.substr(e + 1));
    if (section.rfind("[Document", 0) == 0 && cle == "DOCUMENTPATH") p.documents.push_back(valeur);
    if (section == "[Design]" && cle == "HIERARCHYMODE") p.portee = std::atoi(valeur.c_str());
  }
  return p;
}
std::string extension(const std::string& n) {
  const auto p = n.find_last_of('.');
  return p == std::string::npos ? std::string() : minuscules(n.substr(p + 1));
}
bool estExtensionSchema(const std::string& e) { return e == "schdoc" || e == "csschdoc" || e == "cmschdoc"; }
bool estExtensionCarte(const std::string& e) { return e == "pcbdoc" || e == "cspcbdoc" || e == "cmpcbdoc" || e == "swpcbdoc"; }
}  // namespace

bool estSchemaAltium(const std::string& o) {
  if (estCfb(o)) {
    try {
      for (const auto& [k, v] : lireCfb(o))
        if (minuscules(k) == "fileheader") return v.find("Schematic") != std::string::npos;
    } catch (const std::exception&) {
    }
    return false;
  }
  return o.rfind("|HEADER=Protel for Windows - Schematic Capture", 0) == 0;
}
bool estCarteAltium(const std::string& o) {
  if (!estCfb(o)) return false;
  try {
    for (const auto& [k, v] : lireCfb(o))
      if (minuscules(k) == "board6/data") return true;
  } catch (const std::exception&) {
  }
  return false;
}
bool estBibliothequeAltium(const std::string& o) {
  if (!estCfb(o)) return false;
  try {
    for (const auto& [k, v] : lireCfb(o)) {
      const std::string m = minuscules(k);
      if (m == "fileheader" && (v.find("Schematic Library") != std::string::npos || v.find("SchLib") != std::string::npos)) return true;
      if (m == "library/data" || m == "library/header") return true;
    }
  } catch (const std::exception&) {
  }
  return false;
}

ConceptionElec conceptionDepuisAltium(const std::vector<std::pair<std::string, std::string>>& schemas, const std::optional<std::string>& carte, const std::string& nom,
                                      RapportImportAltium* rapport) {
  RapportImportAltium r;
  ConceptionElec c = nouvelleConception(nom);
  c.feuilles.clear();
  std::vector<FeuilleSch> feuilles;
  for (const auto& [n, o] : schemas) {
    try {
      feuilles.push_back(lireFeuilleSch(o, n, r));
      r.fichiers.push_back(n);
    } catch (const std::exception& e) {
      r.alertes.push_back(n + " illisible : " + e.what());
    }
  }
  assemblerSchemas(c, feuilles, 0, r);
  if (c.feuilles.empty()) {
    Feuille f;
    f.uuid = nouvelUuid();
    c.feuilles.push_back(f);
  }
  synchroniserReseaux(c);
  if (carte) {
    try {
      const CarteA a = lireCarteA(lireCfb(*carte), r);
      RapportImportAltium rc;
      ConceptionElec k = conceptionDeCarte(a, nom, rc);
      r.alertes.insert(r.alertes.end(), rc.alertes.begin(), rc.alertes.end());
      r.empreintes = rc.empreintes, r.pistes = rc.pistes, r.vias = rc.vias, r.zones = rc.zones;
      if (schemas.empty()) {
        c = std::move(k);
        r.composants = rc.composants;
      } else {
        const RapportLiaison l = relierCarteAuSchema(c, k);
        for (const auto& x : l.carteSeule) r.alertes.push_back(x + " : posé sur la carte sans composant au schéma");
        for (const auto& x : l.schemaSeul) r.alertes.push_back(x + " : au schéma, absent de la carte");
        for (const auto& x : l.ecarts) r.alertes.push_back(x);
      }
    } catch (const std::exception& e) {
      r.alertes.push_back(std::string("carte illisible : ") + e.what());
    }
  } else
    c.schemaSeul = true;
  r.reseaux = static_cast<int>(netlistDe(c).reseaux.size());
  if (rapport) *rapport = r;
  return c;
}

ConceptionElec conceptionDepuisProjetAltium(const std::string& prjpcb, const std::string& nom, const LecteurAltium& lire, RapportImportAltium* rapport) {
  const ProjetA p = lireProjet(prjpcb);
  std::vector<std::pair<std::string, std::string>> schemas;
  std::optional<std::string> carte;
  std::vector<std::string> manquants;
  for (const auto& d : p.documents) {
    const std::string e = extension(d);
    if (!estExtensionSchema(e) && !estExtensionCarte(e)) continue;
    std::string chemin = d;
    std::replace(chemin.begin(), chemin.end(), '\\', '/');
    const auto o = lire ? lire(chemin) : std::nullopt;
    if (!o) {
      manquants.push_back(d);
      continue;
    }
    if (estExtensionSchema(e)) schemas.push_back({d, *o});
    else if (!carte) carte = *o;
  }
  RapportImportAltium r;
  ConceptionElec c = conceptionDepuisAltium(schemas, carte, nom, &r);
  for (const auto& m : manquants) r.alertes.push_back("Document « " + m + " » du projet introuvable");
  if (carte) r.fichiers.push_back("carte");
  if (rapport) *rapport = r;
  return c;
}

// ======================================================================================================================
// Bibliothèques
// ======================================================================================================================
BibliothequeAltium lireBibliothequeAltium(const std::string& octets, const std::string& bibliotheque) {
  BibliothequeAltium b;
  std::vector<std::pair<std::string, std::string>> f;
  try {
    f = lireCfb(octets);
  } catch (const std::exception& e) {
    b.alertes.push_back(std::string("bibliothèque illisible : ") + e.what());
    return b;
  }
  // Dossiers portant un flux « Data » : un symbole ou une empreinte chacun
  std::map<std::string, std::map<std::string, const std::string*>> dossiers;
  for (const auto& [k, v] : f) {
    const auto p = k.find('/');
    if (p == std::string::npos) continue;
    dossiers[k.substr(0, p)][k.substr(p + 1)] = &v;
  }
  const bool schema = flux(f, "FileHeader") && flux(f, "FileHeader")->find("Schematic") != std::string::npos;
  for (const auto& [dossier, contenu] : dossiers) {
    const auto it = contenu.find("Data");
    if (it == contenu.end() || dossier == "Library" || dossier == "FileVersionInfo") continue;
    try {
      Flux x{*it->second};
      if (schema) {
        DefSymbole d;
        d.categorie = "Importés d’Altium";
        bool premier = true;
        while (x.restants() >= 4) {
          std::string binaire;
          Proprietes p = x.proprietes(&binaire);
          if (!binaire.empty()) {
            // Broche binaire
            Flux y{binaire};
            if (y.i32() != 2) continue;
            y.u8();
            p["OWNERPARTID"] = std::to_string(y.i16());
            y.u8();
            y.sauter(4);
            y.pascal();
            y.u8();
            p["ELECTRICAL"] = std::to_string(y.u8());
            p["PINCONGLOMERATE"] = std::to_string(y.u8());
            p["PINLENGTH"] = std::to_string(y.i16());
            p["LOCATION.X"] = std::to_string(y.i16());
            p["LOCATION.Y"] = std::to_string(y.i16());
            y.i32();
            p["NAME"] = latin1(y.pascal());
            p["DESIGNATOR"] = latin1(y.pascal());
            p["RECORD"] = "2";
          }
          const long genre = entier(p, "RECORD", -1);
          if (premier) {
            premier = false;
            if (genre != 1) throw std::runtime_error("le symbole ne commence pas par son composant");
            d.nom = texte(p, "LIBREFERENCE", dossier);
            d.description = texte(p, "COMPONENTDESCRIPTION");
            d.unites = static_cast<int>(std::max(1L, entier(p, "PARTCOUNT", 2) - 1));
            continue;
          }
          auto P = [&](double xm, double ym) { return Pt{nmDeMils(xm), -nmDeMils(ym)}; };
          if (genre == 2) {
            if (entier(p, "OWNERPARTDISPLAYMODE", 0) != 0) continue;
            const long cg = entier(p, "PINCONGLOMERATE");
            const double px = milsSch(p, "LOCATION.X"), py = milsSch(p, "LOCATION.Y"), l = milsSch(p, "PINLENGTH");
            double cx = px, cy = py;
            switch (cg & 3) {
              case 0: cx += l; break;
              case 1: cy += l; break;
              case 2: cx -= l; break;
              default: cy -= l; break;
            }
            Broche br;
            br.numero = texte(p, "DESIGNATOR");
            br.nom = texte(p, "NAME");
            br.type = typeAltium(entier(p, "ELECTRICAL", 4));
            br.cachee = (cg & 4) != 0;
            br.extremite = P(cx, cy);
            br.longueur = nmDeMils(l);
            const int o = static_cast<int>(cg & 3);
            br.direction = o == 0 ? 2 : o == 1 ? 1 : o == 2 ? 0 : 3;  // de l'extrémité vers le corps
            br.unite = d.unites > 1 ? static_cast<int>(std::max(0L, entier(p, "OWNERPARTID", 0))) : 0;
            d.broches.push_back(br);
          } else if (genre == 34) {
            std::string pre;
            for (const char ch : texte(p, "TEXT"))
              if (std::isalpha(static_cast<unsigned char>(ch))) pre += ch;
              else break;
            if (!pre.empty()) d.prefixe = pre;
          } else if (genre == 45 && majuscules(texte(p, "MODELTYPE")) == "PCBLIB" && (booleen(p, "ISCURRENT") || d.empreinte.empty())) {
            d.empreinte = "altium:" + bibliotheque + ":" + texte(p, "MODELNAME");
          } else if (genre == 14 || genre == 13 || genre == 6 || genre == 7) {
            if (entier(p, "OWNERPARTDISPLAYMODE", 0) != 0) continue;
            Trace t;
            if (genre == 14 || genre == 13) {
              t.forme = genre == 14 ? Trace::Forme::Rectangle : Trace::Forme::Polyligne;
              t.points = {P(milsSch(p, "LOCATION.X"), milsSch(p, "LOCATION.Y")), P(milsSch(p, "CORNER.X"), milsSch(p, "CORNER.Y"))};
              t.fond = genre == 14 && booleen(p, "ISSOLID");
            } else {
              for (long j = 1; j <= entier(p, "LOCATIONCOUNT"); ++j) t.points.push_back(P(milsSch(p, "X" + std::to_string(j)), milsSch(p, "Y" + std::to_string(j))));
              t.ferme = genre == 7;
            }
            t.unite = d.unites > 1 ? static_cast<int>(std::max(0L, entier(p, "OWNERPARTID", 0))) : 0;
            if (!t.points.empty()) d.traces.push_back(t);
          }
        }
        d.id = "altium:" + bibliotheque + ":" + d.nom;
        b.symboles.push_back(d);
      } else {
        DefEmpreinte d;
        x.sous();
        const std::string nom = latin1(x.pascal());
        x.finirSous();
        while (x.restants() >= 4) {
          const std::uint8_t genre = static_cast<std::uint8_t>((*it->second)[x.p]);
          if (genre == 2) {
            const PadA a = lirePad(x);
            d.pastilles.push_back(pastilleLocale(a, EmpreintePlacee{}));
          } else if (genre == 4 || genre == 1) {
            const TraitA t = genre == 4 ? lirePiste(x) : lireArc(x);
            if (t.couche == 33 || t.couche == 34) {
              TraitEmpreinte e;
              e.couche = coucheLocale(t.couche, false);
              e.a = t.a;
              e.b = t.b;
              e.epaisseur = t.largeur;
              d.traits.push_back(e);
            }
          } else if (genre == 3) {
            // Via de l'empreinte : une pastille traversante, numérotée d'après la pastille qu'elle touche
            int n = 0;
            const Via v = lireVia(x, n);
            Pastille q;
            q.type = TypePastille::Traversante;
            q.forme = FormePastille::Rond;
            q.position = v.p;
            q.largeur = q.hauteur = v.diametre;
            q.percage = v.percage;
            d.pastilles.push_back(q);
          } else if (genre == 6) {
            // Surface de cuivre de l'empreinte : une pastille CMS sur sa face, numérotée de même
            x.u8();
            x.sous();
            const int couche = x.u8();
            x.sauter(12);
            const Pt a1 = pointCarte(x);
            const Pt a2 = pointCarte(x);
            const double angle = x.f64();
            x.finirSous();
            if (couche == 1 || couche == 32) {
              Pastille q;
              q.forme = FormePastille::Rect;
              q.position = {(a1.x + a2.x) / 2, (a1.y + a2.y) / 2};
              q.largeur = std::abs(a2.x - a1.x);
              q.hauteur = std::abs(a2.y - a1.y);
              q.rotation = angle;
              if (couche == 32) q.couches = bit(Couche::CuivreDessous);
              else q.couches = bit(Couche::CuivreDessus);
              d.pastilles.push_back(q);
            }
          } else if (genre == 5) {
            x.u8();
            x.sous();
            x.finirSous();
            x.sous();
            x.finirSous();
          } else if (genre == 11 || genre == 12) {
            sauterSimple(x);
          } else {
            b.alertes.push_back(nom + " : enregistrement " + std::to_string(genre) + " inconnu, lecture arrêtée");
            break;
          }
        }
        // Pastilles sans numéro (vias, surfaces) : celui de la pastille numérotée qu'elles touchent, jusqu'à stabilité
        for (bool change = true; change;) {
          change = false;
          for (auto& q : d.pastilles) {
            if (!q.numero.empty()) continue;
            const Polygone fq = formePastille(q);
            for (const auto& o : d.pastilles)
              if (!o.numero.empty() && distancePolygones(fq, formePastille(o)) == 0) {
                q.numero = o.numero;
                change = true;
                break;
              }
          }
        }
        d.id = "altium:" + bibliotheque + ":" + (nom.empty() ? dossier : nom);
        d.description = nom.empty() ? dossier : nom;
        d.famille = "Importées d’Altium";
        b.empreintes.push_back(cadrer(d));
      }
    } catch (const std::exception& e) {
      b.alertes.push_back(dossier + " : " + e.what());
    }
  }
  return b;
}

// ======================================================================================================================
// Écrivains d'essai
// ======================================================================================================================
namespace {
std::string ligneProprietes(const EnregistrementAltium& e) {
  std::string s;
  for (const auto& [k, v] : e) s += "|" + k + "=" + v;
  return s;
}
void ecU32(std::string& s, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) s += static_cast<char>((v >> (8 * i)) & 0xFF);
}
void ecU16(std::string& s, std::uint16_t v) {
  s += static_cast<char>(v & 0xFF);
  s += static_cast<char>(v >> 8);
}
void ecI32(std::string& s, std::int32_t v) { ecU32(s, static_cast<std::uint32_t>(v)); }
void ecF64(std::string& s, double v) {
  char b[8];
  std::memcpy(b, &v, 8);
  s.append(b, 8);
}
std::int32_t enUnites(double mm) { return static_cast<std::int32_t>(std::llround(mm * 1e6 / 2.54)); }
std::string blocProprietes(const EnregistrementAltium& e) {
  std::string t = ligneProprietes(e);
  t += '\0';
  std::string s;
  ecU32(s, static_cast<std::uint32_t>(t.size()));
  return s + t;
}
std::string mils(double mm) {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o << mm / 0.0254 << "mil";
  return o.str();
}
}  // namespace

std::string schDocAltium(const std::vector<EnregistrementAltium>& enregistrements, bool ascii) {
  const EnregistrementAltium entete = {{"HEADER", ascii ? "Protel for Windows - Schematic Capture Ascii File Version 5.0" : "Protel for Windows - Schematic Capture Binary File Version 5.0"}, {"WEIGHT", std::to_string(enregistrements.size())}};
  if (ascii) {
    std::string s = ligneProprietes(entete) + "\n";
    for (const auto& e : enregistrements) s += ligneProprietes(e) + "\n";
    return s;
  }
  std::string d = blocProprietes(entete);
  for (const auto& e : enregistrements) d += blocProprietes(e);
  return ecrireCfb({{"FileHeader", d}});
}

std::string pcbDocAltium(const CarteAltium& k) {
  // Contour
  EnregistrementAltium b = {{"FILENAME", "essai.PcbDoc"}, {"KIND", "Protel_Advanced_PCB"}, {"VERSION", "3.00"}};
  for (std::size_t i = 0; i < k.contour.size(); ++i) {
    const std::string s = std::to_string(i);
    b.push_back({"KIND" + s, "0"});
    b.push_back({"VX" + s, mils(k.contour[i].first)});
    b.push_back({"VY" + s, mils(k.contour[i].second)});
  }
  std::string nets, comps, pads, tracks, vias;
  for (const auto& n : k.reseaux) nets += blocProprietes({{"NAME", n}});
  for (const auto& c : k.composants)
    comps += blocProprietes({{"LAYER", c.dessous ? "BOTTOM" : "TOP"}, {"X", mils(c.x)}, {"Y", mils(c.y)}, {"ROTATION", std::to_string(c.angle)}, {"PATTERN", c.motif}, {"SOURCEDESIGNATOR", c.designateur}});
  auto idx = [](int i) { return static_cast<std::uint16_t>(i < 0 ? 0xFFFF : i); };
  for (const auto& p : k.pastilles) {
    pads += '\x02';
    std::string s1;
    s1 += static_cast<char>(p.nom.size());
    s1 += p.nom;
    ecU32(pads, static_cast<std::uint32_t>(s1.size()));
    pads += s1;
    for (int j = 0; j < 3; ++j) ecU32(pads, 0);
    std::string s5;
    s5 += static_cast<char>(p.couche);
    s5 += '\x04';
    s5 += '\0';
    ecU16(s5, idx(p.reseau));
    ecU16(s5, 0xFFFF);
    ecU16(s5, idx(p.composant));
    ecU32(s5, 0);
    ecI32(s5, enUnites(p.x));
    ecI32(s5, enUnites(p.y));
    for (int j = 0; j < 3; ++j) ecI32(s5, enUnites(p.largeur)), ecI32(s5, enUnites(p.hauteur));
    ecI32(s5, enUnites(p.percage));
    s5 += static_cast<char>(p.forme);
    s5 += static_cast<char>(p.forme);
    s5 += static_cast<char>(p.forme);
    ecF64(s5, p.angle);
    s5 += static_cast<char>(p.percage > 0 ? 1 : 0);
    s5.resize(110, '\0');
    ecU32(pads, static_cast<std::uint32_t>(s5.size()));
    pads += s5;
    ecU32(pads, 0);
  }
  for (const auto& t : k.pistes) {
    tracks += '\x04';
    std::string s;
    s += static_cast<char>(t.couche);
    s += '\x04';
    s += '\0';
    ecU16(s, idx(t.reseau));
    ecU16(s, 0xFFFF);
    ecU16(s, 0xFFFF);
    ecU32(s, 0);
    ecI32(s, enUnites(t.x1));
    ecI32(s, enUnites(t.y1));
    ecI32(s, enUnites(t.x2));
    ecI32(s, enUnites(t.y2));
    ecI32(s, enUnites(t.largeur));
    ecU16(s, 0);
    s += '\0';
    ecU32(tracks, static_cast<std::uint32_t>(s.size()));
    tracks += s;
  }
  for (const auto& v : k.vias) {
    vias += '\x03';
    std::string s;
    s += '\0';
    s += '\x04';
    s += '\0';
    ecU16(s, idx(v.reseau));
    ecU16(s, 0xFFFF);
    ecU16(s, 0xFFFF);
    ecU32(s, 0);
    ecI32(s, enUnites(v.x));
    ecI32(s, enUnites(v.y));
    ecI32(s, enUnites(v.diametre));
    ecI32(s, enUnites(v.percage));
    s += '\x01';
    s += '\x20';
    ecU32(vias, static_cast<std::uint32_t>(s.size()));
    vias += s;
  }
  return ecrireCfb({{"FileHeader", blocProprietes({{"HEADER", "PCB 6.0 Binary File"}})},
                    {"Board6/Data", blocProprietes(b)},
                    {"Nets6/Data", nets},
                    {"Components6/Data", comps},
                    {"Pads6/Data", pads},
                    {"Tracks6/Data", tracks},
                    {"Vias6/Data", vias}});
}

}  // namespace prom::ecad

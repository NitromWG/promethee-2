// Prométhée : netlists des autres logiciels (10.20). Voir netlists_tierces.hpp. Licence GPL-3.0-only.
#include "promethee/netlists_tierces.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <functional>
#include <map>
#include <set>
#include <sstream>

#include "kicad_interne.hpp"

namespace prom::ecad {

namespace {
std::string garni(const std::string& s) {
  const auto a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
std::string minuscules(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}
std::vector<std::string> mots(const std::string& s) {
  std::vector<std::string> r;
  std::istringstream in(s);
  for (std::string m; in >> m;) r.push_back(m);
  return r;
}
std::vector<std::string> lignes(const std::string& t) {
  std::vector<std::string> r;
  std::istringstream in(t);
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    r.push_back(l);
  }
  return r;
}
std::string sansGuillemets(std::string s) {
  s = garni(s);
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) return s.substr(1, s.size() - 2);
  return s;
}
// « R1.2 » ou « R1-2 » : repère et broche, au dernier séparateur
bool repereBroche(const std::string& t, char sep, std::string& ref, std::string& broche) {
  const auto p = t.rfind(sep);
  if (p == std::string::npos || p == 0 || p + 1 >= t.size()) return false;
  ref = t.substr(0, p);
  broche = t.substr(p + 1);
  return true;
}
std::string date() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char b[32];
  std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tm);
  return b;
}
ComposantNetlist* composant(NetlistLue& n, const std::string& ref) {
  for (auto& x : n.composants)
    if (x.reference == ref) return &x;
  n.composants.push_back({ref, {}, {}});
  return &n.composants.back();
}
// Identifiant EDIF : lettres, chiffres et soulignés, commençant par une lettre ; sinon « &… » et le nom d'origine
std::string idEdif(const std::string& nom, std::set<std::string>& pris) {
  std::string id;
  for (unsigned char ch : nom) id += std::isalnum(ch) ? static_cast<char>(ch) : '_';
  if (id.empty() || !std::isalpha(static_cast<unsigned char>(id[0]))) id = "&" + id;
  std::string base = id;
  for (int k = 2; pris.count(id); ++k) id = base + "_" + std::to_string(k);
  pris.insert(id);
  return id;
}
std::string nomEdif(const std::string& nom, std::set<std::string>& pris) {
  const std::string id = idEdif(nom, pris);
  return id == nom ? id : "(rename " + id + " \"" + nom + "\")";
}

// ---------- Lecteurs ----------
NetlistLue lireOrcad(const std::string& t) {
  NetlistLue n;
  ComposantNetlist* courant = nullptr;
  for (const auto& l0 : lignes(t)) {
    const std::string l = garni(l0);
    if (l.empty() || l[0] != '(' || l.rfind("( {", 0) == 0 || l.rfind("({", 0) == 0) continue;
    auto m = mots(l.substr(1));
    if (!m.empty() && m.back() == ")") m.pop_back();
    if (m.size() >= 4 && (m[0][0] == '/' || std::isxdigit(static_cast<unsigned char>(m[0][0])) || m[0][0] == '$')) {
      // ( id empreinte repère valeur
      courant = composant(n, m[2]);
      courant->empreinte = m[1] == "$noname" ? std::string() : m[1];
      courant->valeur = m[3];
    } else if (courant && m.size() >= 2) {
      const std::string reseau = m[1] == "?" ? std::string() : m[1];
      if (!reseau.empty()) n.reseaux[reseau].push_back({courant->reference, m[0]});
    }
  }
  return n;
}
NetlistLue lirePads(const std::string& t) {
  NetlistLue n;
  int partie = 0;
  std::string reseau;
  for (const auto& l0 : lignes(t)) {
    const std::string l = garni(l0);
    if (l.empty()) continue;
    if (l.rfind("*PART*", 0) == 0) {
      partie = 1;
      continue;
    }
    if (l.rfind("*NET*", 0) == 0) {
      partie = 2;
      continue;
    }
    if (l.rfind("*END*", 0) == 0) break;
    if (l.rfind("*SIGNAL*", 0) == 0) {
      reseau = garni(l.substr(8));
      partie = 2;
      continue;
    }
    if (l[0] == '*') continue;
    if (partie == 1) {
      const auto m = mots(l);
      if (m.empty()) continue;
      ComposantNetlist* x = composant(n, m[0]);
      if (m.size() > 1) {
        // « R1 10k@R_0603 » : type de pièce (la valeur) et empreinte (decal) ; « R1 R_0603 » : l'empreinte seule
        const auto at = m[1].find('@');
        x->empreinte = at == std::string::npos ? m[1] : m[1].substr(at + 1);
        if (at != std::string::npos) x->valeur = m[1].substr(0, at);
      }
    } else if (partie == 2 && !reseau.empty())
      for (const auto& b : mots(l)) {
        std::string r, p;
        if (repereBroche(b, '.', r, p)) n.reseaux[reseau].push_back({r, p});
      }
  }
  return n;
}
NetlistLue lireAllegro(const std::string& t) {
  NetlistLue n;
  int partie = 0;
  std::string reseau, cumul;
  auto finirReseau = [&](const std::string& corps) {
    for (std::string b : mots(corps)) {
      if (!b.empty() && b.back() == ',') b.pop_back();
      std::string r, p;
      if (repereBroche(b, '.', r, p)) n.reseaux[reseau].push_back({r, p});
    }
  };
  for (const auto& l0 : lignes(t)) {
    const std::string l = garni(l0);
    if (l.empty()) continue;
    if (l.rfind("$PACKAGES", 0) == 0) {
      partie = 1;
      continue;
    }
    if (l.rfind("$NETS", 0) == 0) {
      partie = 2;
      continue;
    }
    if (l.rfind("$END", 0) == 0) break;
    if (partie == 1) {
      // « empreinte ! empreinte ! valeur ; R1 R2 »
      const auto pv = l.find(';');
      if (pv == std::string::npos) continue;
      std::vector<std::string> champs;
      std::string tete = l.substr(0, pv), c;
      std::istringstream in(tete);
      while (std::getline(in, c, '!')) champs.push_back(garni(sansGuillemets(c)));
      for (const auto& ref : mots(l.substr(pv + 1))) {
        ComposantNetlist* x = composant(n, ref);
        if (!champs.empty()) x->empreinte = champs[0];
        if (champs.size() > 2) x->valeur = champs[2];
      }
    } else if (partie == 2) {
      const auto pv = l.find(';');
      if (pv != std::string::npos) {
        reseau = sansGuillemets(l.substr(0, pv));
        finirReseau(l.substr(pv + 1));
      } else if (!reseau.empty())
        finirReseau(l);
    }
  }
  return n;
}
NetlistLue lireCadstar(const std::string& t) {
  NetlistLue n;
  std::string reseau;
  for (const auto& l0 : lignes(t)) {
    const std::string l = garni(l0);
    if (l.empty()) continue;
    const auto m = mots(l);
    if (m[0] == ".ADD_COM" && m.size() >= 2) {
      ComposantNetlist* x = composant(n, m[1]);
      if (m.size() >= 3) x->empreinte = sansGuillemets(l.substr(l.find(m[2])));
    } else if (m[0] == ".ADD_TER" && m.size() >= 3) {
      reseau = sansGuillemets(l.substr(l.find(m[2])));
      std::string r, p;
      if (repereBroche(m[1], '.', r, p)) n.reseaux[reseau].push_back({r, p});
    } else if ((m[0] == ".TER" || m[0][0] != '.') && !reseau.empty()) {
      for (std::size_t i = m[0] == ".TER" ? 1 : 0; i < m.size(); ++i) {
        std::string r, p;
        if (repereBroche(m[i], '.', r, p)) n.reseaux[reseau].push_back({r, p});
      }
    } else if (m[0] == ".END")
      break;
  }
  return n;
}
std::string nomExpr(const kicad_interne::Expr& e) {
  // Nom d'un objet EDIF : atome, ou « (rename id "nom") », ou « (name id (display "nom")) »
  if (!e.liste) return e.atome;
  if (e.nom() == "rename" && e.elements.size() >= 3) return e.elements[2].liste ? nomExpr(e.elements[2]) : e.elements[2].atome;
  if (e.nom() == "name" && e.elements.size() >= 2) return e.elements[1].atome;
  return e.elements.size() > 1 ? nomExpr(e.elements[1]) : std::string();
}
std::string idExpr(const kicad_interne::Expr& e) {
  if (!e.liste) return e.atome;
  if ((e.nom() == "rename" || e.nom() == "name") && e.elements.size() >= 2) return e.elements[1].atome;
  return {};
}
void parcourirEdif(const kicad_interne::Expr& e, NetlistLue& n, std::map<std::string, std::string>& instances, std::vector<const kicad_interne::Expr*>& reseaux) {
  if (!e.liste) return;
  if (e.nom() == "instance" && e.elements.size() >= 2) {
    const std::string id = idExpr(e.elements[1]), nom = nomExpr(e.elements[1]);
    instances[id] = nom;
    ComposantNetlist* x = composant(n, nom);
    for (const auto& s : e.elements) {
      if (s.nom() == "viewRef")
        for (const auto& c : s.elements)
          if (c.nom() == "cellRef" && c.elements.size() >= 2) x->empreinte = c.elements[1].liste ? idExpr(c.elements[1]) : c.elements[1].atome;
      if (s.nom() == "property" && s.elements.size() >= 3) {
        const std::string cle = minuscules(nomExpr(s.elements[1]));
        const auto& v = s.elements[2];
        const std::string val = v.liste && v.elements.size() >= 2 ? v.elements[1].atome : v.atome;
        if (cle == "value" || cle == "valeur") x->valeur = val;
        if (cle == "footprint" || cle == "package" || cle == "pcb_footprint") x->empreinte = val;
      }
    }
    return;
  }
  if (e.nom() == "net") {
    reseaux.push_back(&e);
    return;
  }
  for (const auto& s : e.elements) parcourirEdif(s, n, instances, reseaux);
}
NetlistLue lireEdif(const std::string& t) {
  NetlistLue n;
  try {
    const kicad_interne::Expr racine = kicad_interne::Lecteur(t).lire();
    std::map<std::string, std::string> instances;
    std::vector<const kicad_interne::Expr*> reseaux;
    parcourirEdif(racine, n, instances, reseaux);
    // Noms des ports : les « rename » des interfaces des cellules
    std::map<std::string, std::string> ports, cellules;
    std::function<void(const kicad_interne::Expr&)> portsDe = [&](const kicad_interne::Expr& e) {
      if (!e.liste) return;
      if (e.nom() == "port" && e.elements.size() >= 2) ports[idExpr(e.elements[1])] = nomExpr(e.elements[1]);
      if (e.nom() == "cell" && e.elements.size() >= 2) cellules[idExpr(e.elements[1])] = nomExpr(e.elements[1]);
      for (const auto& s : e.elements) portsDe(s);
    };
    portsDe(racine);
    // Empreintes : la cellule désignée par son identifiant, nommée par son « rename »
    for (auto& x : n.composants)
      if (const auto it = cellules.find(x.empreinte); it != cellules.end()) x.empreinte = it->second;
    for (const auto* r : reseaux) {
      if (r->elements.size() < 2) continue;
      const std::string nom = nomExpr(r->elements[1]);
      for (const auto& s : r->elements)
        if (s.nom() == "joined")
          for (const auto& pr : s.elements) {
            if (pr.nom() != "portRef" || pr.elements.size() < 2) continue;
            std::string port = idExpr(pr.elements[1]), inst;
            if (ports.count(port)) port = ports[port];
            else if (pr.elements[1].liste) port = nomExpr(pr.elements[1]);
            if (!port.empty() && port[0] == '&') port = port.substr(1);
            for (const auto& x : pr.elements)
              if (x.nom() == "instanceRef" && x.elements.size() >= 2) inst = x.elements[1].atome;
            if (inst.empty()) continue;  // port de la cellule elle-même
            n.reseaux[nom].push_back({instances.count(inst) ? instances[inst] : inst, port});
          }
    }
  } catch (const std::exception& e) {
    n.erreurs.push_back(std::string("EDIF illisible : ") + e.what());
  }
  return n;
}
// SPICE et LTspice : nombre de nœuds par lettre d'élément
int noeudsSpice(char lettre) {
  switch (std::toupper(static_cast<unsigned char>(lettre))) {
    case 'R': case 'C': case 'L': case 'D': case 'V': case 'I': case 'B': case 'F': case 'H': case 'W': return 2;
    case 'Q': case 'J': case 'Z': return 3;
    case 'M': case 'E': case 'G': case 'S': case 'T': case 'O': return 4;
    default: return 0;
  }
}
NetlistLue lireSpice(const std::string& t) {
  NetlistLue n;
  std::vector<std::string> cartes;
  for (const auto& l0 : lignes(t)) {
    const std::string l = garni(l0);
    if (l.empty()) continue;
    if (l[0] == '+' && !cartes.empty()) {
      cartes.back() += " " + l.substr(1);
      continue;
    }
    cartes.push_back(l);
  }
  bool premiere = true;
  for (const auto& c : cartes) {
    if (premiere && c[0] == '*') {
      premiere = false;
      continue;
    }
    premiere = false;
    if (c[0] == '*' || c[0] == '.' || c[0] == ';') continue;
    auto m = mots(c);
    if (m.size() < 3) continue;
    const std::string ref = m[0];
    int nb = noeudsSpice(ref[0]);
    std::string valeur;
    if (std::toupper(static_cast<unsigned char>(ref[0])) == 'X') {
      // Sous-circuit : les nœuds, puis le nom du sous-circuit, puis ses paramètres
      std::size_t fin = m.size();
      for (std::size_t i = 1; i < m.size(); ++i)
        if (m[i].find('=') != std::string::npos || minuscules(m[i]) == "params:") {
          fin = i;
          break;
        }
      nb = static_cast<int>(fin) - 2;
      valeur = m[fin - 1];
    } else if (nb == 0)
      continue;
    if (static_cast<int>(m.size()) < nb + 1) continue;
    if (valeur.empty())
      for (std::size_t i = static_cast<std::size_t>(nb) + 1; i < m.size(); ++i) valeur += (valeur.empty() ? "" : " ") + m[i];
    ComposantNetlist* x = composant(n, ref);
    x->valeur = valeur;
    for (int k = 1; k <= nb; ++k) {
      std::string noeud = m[static_cast<std::size_t>(k)];
      if (noeud == "0" || minuscules(noeud) == "gnd") noeud = "GND";
      n.reseaux[noeud].push_back({ref, std::to_string(k)});
    }
  }
  return n;
}
NetlistLue lireGeda(const std::string& t) {
  NetlistLue n;
  std::string cumul;
  std::vector<std::string> enregistrements;
  for (const auto& l0 : lignes(t)) {
    std::string l = l0;
    if (!garni(l).empty() && garni(l).back() == '\\') {
      l = garni(l);
      l.pop_back();
      cumul += l + " ";
      continue;
    }
    cumul += l;
    if (!garni(cumul).empty()) enregistrements.push_back(cumul);
    cumul.clear();
  }
  for (const auto& e : enregistrements) {
    const auto m = mots(e);
    if (m.size() < 2 || m[0][0] == '#') continue;
    for (std::size_t i = 1; i < m.size(); ++i) {
      std::string r, p;
      if (repereBroche(m[i], '-', r, p)) {
        n.reseaux[m[0]].push_back({r, p});
        composant(n, r);
      }
    }
  }
  return n;
}
NetlistLue lireTinyCad(const std::string& t) {
  NetlistLue n;
  ComposantNetlist* courant = nullptr;
  for (const auto& l0 : lignes(t)) {
    const std::string l = garni(l0);
    if (l.rfind("COMPONENT", 0) == 0) {
      courant = composant(n, sansGuillemets(l.substr(9)));
    } else if (l.rfind("OPTION", 0) == 0 && courant) {
      const auto eg = l.find('=');
      if (eg == std::string::npos) continue;
      const std::string cle = minuscules(sansGuillemets(l.substr(6, eg - 6))), val = sansGuillemets(l.substr(eg + 1));
      if (cle == "package") courant->empreinte = val;
      if (cle == "value" || cle == "valeur") courant->valeur = val;
      if (cle == "name" && courant->valeur.empty()) courant->valeur = val;
    } else if (l.rfind("NET", 0) == 0) {
      const auto eg = l.find('=');
      if (eg == std::string::npos) continue;
      const std::string reseau = sansGuillemets(l.substr(3, eg - 3));
      std::string reste = l.substr(eg + 1);
      for (std::size_t a; (a = reste.find('(')) != std::string::npos;) {
        const auto b = reste.find(')', a);
        if (b == std::string::npos) break;
        const std::string paire = reste.substr(a + 1, b - a - 1);
        const auto v = paire.find(',');
        if (v != std::string::npos) n.reseaux[reseau].push_back({garni(paire.substr(0, v)), garni(paire.substr(v + 1))});
        reste = reste.substr(b + 1);
      }
    }
  }
  return n;
}
}  // namespace

const std::vector<InfoFormatNetlist>& formatsNetlist() {
  static const std::vector<InfoFormatNetlist> f{
      {FormatNetlist::OrcadPcb2, "OrCAD PCB II", "net"}, {FormatNetlist::Pads, "PADS", "asc"},      {FormatNetlist::Allegro, "Allegro (Telesis)", "net"},
      {FormatNetlist::Cadstar, "CADSTAR", "frp"},         {FormatNetlist::Edif, "EDIF 2 0 0", "edf"}, {FormatNetlist::Spice, "SPICE (LTspice)", "net"},
      {FormatNetlist::Geda, "gEDA (gschem)", "net"},      {FormatNetlist::TinyCad, "TinyCAD", "net"},
  };
  return f;
}

std::optional<FormatNetlist> reconnaitreNetlist(const std::string& t) {
  const std::string debut = t.substr(0, 4096);
  if (debut.find("*PADS-PCB*") != std::string::npos || debut.find("*PADS2000*") != std::string::npos || (debut.find("*PART*") != std::string::npos && debut.find("*NET*") != std::string::npos))
    return FormatNetlist::Pads;
  if (debut.find("$PACKAGES") != std::string::npos || debut.find("$NETS") != std::string::npos) return FormatNetlist::Allegro;
  if (debut.find(".ADD_COM") != std::string::npos || debut.find(".TYP FULL") != std::string::npos || debut.find(".HEA") != std::string::npos) return FormatNetlist::Cadstar;
  {
    const auto p = debut.find_first_not_of(" \t\r\n");
    if (p != std::string::npos && minuscules(debut.substr(p, 6)) == "(edif ") return FormatNetlist::Edif;
  }
  if (debut.find("COMPONENT '") != std::string::npos || debut.find("NET  '") != std::string::npos || debut.find("NET '") != std::string::npos) return FormatNetlist::TinyCad;
  if (debut.find("( {") != std::string::npos || debut.find("({") != std::string::npos) return FormatNetlist::OrcadPcb2;
  // SPICE : un titre puis des cartes à lettre d'élément, ou des directives
  const auto ls = lignes(debut);
  int cartes = 0, geda = 0;
  for (const auto& l : ls) {
    const std::string g = garni(l);
    if (g.empty()) continue;
    if (g[0] == '.' && (minuscules(g).rfind(".end", 0) == 0 || minuscules(g).rfind(".tran", 0) == 0 || minuscules(g).rfind(".ac", 0) == 0 || minuscules(g).rfind(".op", 0) == 0 || minuscules(g).rfind(".backanno", 0) == 0)) cartes += 3;
    const auto m = mots(g);
    if (m.size() >= 3 && noeudsSpice(m[0][0]) > 0 && std::isdigit(static_cast<unsigned char>(m[0].back()))) ++cartes;
    if (m.size() >= 2) {
      bool tous = true;
      for (std::size_t i = 1; i < m.size(); ++i) {
        std::string r, p;
        tous = tous && (repereBroche(m[i], '-', r, p) || m[i] == "\\");
      }
      geda += tous;
    }
  }
  if (cartes >= 3) return FormatNetlist::Spice;
  if (geda >= 1 && cartes == 0) return FormatNetlist::Geda;
  return std::nullopt;
}

NetlistLue lireNetlistTierce(const std::string& texte, FormatNetlist f) {
  switch (f) {
    case FormatNetlist::OrcadPcb2: return lireOrcad(texte);
    case FormatNetlist::Pads: return lirePads(texte);
    case FormatNetlist::Allegro: return lireAllegro(texte);
    case FormatNetlist::Cadstar: return lireCadstar(texte);
    case FormatNetlist::Edif: return lireEdif(texte);
    case FormatNetlist::Spice: return lireSpice(texte);
    case FormatNetlist::Geda: return lireGeda(texte);
    case FormatNetlist::TinyCad: return lireTinyCad(texte);
  }
  return {};
}

std::string ecrireNetlist(const NetlistLue& n, FormatNetlist f, const std::string& titre) {
  std::ostringstream o;
  // Broches de chaque composant, et réseau de chaque broche
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> parComposant;
  for (const auto& [reseau, noeuds] : n.reseaux)
    for (const auto& [ref, broche] : noeuds) parComposant[ref].push_back({broche, reseau});
  auto empreinte = [](const ComposantNetlist& x) { return x.empreinte.empty() ? std::string("$noname") : x.empreinte; };
  switch (f) {
    case FormatNetlist::OrcadPcb2:
      o << "( { OrCAD PCB II Netlist Format, " << titre << ", " << date() << " }\n";
      for (const auto& x : n.composants) {
        o << " ( /" << x.reference << " " << empreinte(x) << " " << x.reference << " " << (x.valeur.empty() ? "~" : x.valeur) << "\n";
        for (const auto& [b, r] : parComposant[x.reference]) o << "  ( " << b << " " << r << " )\n";
        o << " )\n";
      }
      o << ")\n*\n";
      break;
    case FormatNetlist::Pads:
      o << "*PADS-PCB*\n*PART*\n";
      for (const auto& x : n.composants) o << x.reference << " " << (x.empreinte.empty() ? "UNKNOWN" : x.empreinte) << "\n";
      o << "\n*NET*\n";
      for (const auto& [reseau, noeuds] : n.reseaux) {
        o << "*SIGNAL* " << reseau << "\n";
        int k = 0;
        for (const auto& [ref, broche] : noeuds) o << ref << "." << broche << (++k % 8 == 0 ? "\n" : " ");
        if (k % 8) o << "\n";
      }
      o << "*END*\n";
      break;
    case FormatNetlist::Allegro: {
      o << "(Source: " << titre << ")\n(Date: " << date() << ")\n\n$PACKAGES\n";
      std::map<std::pair<std::string, std::string>, std::vector<std::string>> paquets;
      for (const auto& x : n.composants) paquets[{x.empreinte.empty() ? "UNKNOWN" : x.empreinte, x.valeur}].push_back(x.reference);
      for (const auto& [cle, refs] : paquets) {
        o << "'" << cle.first << "' ! '" << cle.first << "' ! " << (cle.second.empty() ? "~" : cle.second) << ";";
        for (const auto& r : refs) o << " " << r;
        o << "\n";
      }
      o << "$NETS\n";
      for (const auto& [reseau, noeuds] : n.reseaux) {
        o << "'" << reseau << "';";
        int k = 0;
        for (const auto& [ref, broche] : noeuds) o << (k && k % 10 == 0 ? ",\n " : " ") << ref << "." << broche, ++k;
        o << "\n";
      }
      o << "$END\n";
      break;
    }
    case FormatNetlist::Cadstar:
      o << ".HEA\n.TIM " << date() << "\n.APP \"" << titre << "\"\n.TYP FULL\n\n";
      for (const auto& x : n.composants) o << ".ADD_COM " << x.reference << " \"" << (x.empreinte.empty() ? x.valeur : x.empreinte) << "\"\n";
      o << "\n";
      for (const auto& [reseau, noeuds] : n.reseaux) {
        bool premier = true;
        for (const auto& [ref, broche] : noeuds) {
          if (premier) o << ".ADD_TER " << ref << "." << broche << " \"" << reseau << "\"\n";
          else o << ".TER     " << ref << "." << broche << "\n";
          premier = false;
        }
      }
      o << "\n.END\n";
      break;
    case FormatNetlist::Edif: {
      std::set<std::string> idsCellules, idsInstances, idsReseaux;
      std::map<std::string, std::set<std::string>> brochesParCellule;
      std::map<std::string, std::string> celluleDe;
      for (const auto& x : n.composants) {
        const std::string cell = x.empreinte.empty() ? "GENERIQUE" : x.empreinte;
        celluleDe[x.reference] = cell;
        brochesParCellule[cell];  // une cellule même sans broche reliée
        for (const auto& [b, r] : parComposant[x.reference]) brochesParCellule[cell].insert(b);
      }
      std::map<std::string, std::string> idCellule, idInstance;
      for (const auto& [cell, b] : brochesParCellule) idCellule[cell] = idEdif(cell, idsCellules);
      o << "(edif " << "circuit\n  (edifVersion 2 0 0)\n  (edifLevel 0)\n  (keywordMap (keywordLevel 0))\n  (status (written (timeStamp 2026 1 1 0 0 0) (program \"Promethee\")))\n";
      o << "  (library parts\n    (edifLevel 0)\n    (technology (numberDefinition))\n";
      for (const auto& [cell, broches] : brochesParCellule) {
        o << "    (cell " << (idCellule[cell] == cell ? cell : "(rename " + idCellule[cell] + " \"" + cell + "\")") << " (cellType GENERIC)\n      (view netlist (viewType NETLIST)\n        (interface";
        std::set<std::string> ids;
        for (const auto& b : broches) o << "\n          (port " << nomEdif(b, ids) << " (direction INOUT))";
        o << ")))\n";
      }
      o << "  )\n  (library circuit_lib\n    (edifLevel 0)\n    (technology (numberDefinition))\n    (cell circuit (cellType GENERIC)\n      (view netlist (viewType NETLIST)\n        (interface)\n        (contents\n";
      for (const auto& x : n.composants) {
        idInstance[x.reference] = idEdif(x.reference, idsInstances);
        o << "          (instance " << (idInstance[x.reference] == x.reference ? x.reference : "(rename " + idInstance[x.reference] + " \"" + x.reference + "\")") << " (viewRef netlist (cellRef "
          << idCellule[celluleDe[x.reference]] << " (libraryRef parts)))";
        if (!x.valeur.empty()) o << " (property Value (string \"" << x.valeur << "\"))";
        o << ")\n";
      }
      for (const auto& [reseau, noeuds] : n.reseaux) {
        o << "          (net " << nomEdif(reseau, idsReseaux) << " (joined";
        for (const auto& [ref, broche] : noeuds) {
          std::set<std::string> ids;
          const std::string pid = idEdif(broche, ids);
          o << " (portRef " << pid << " (instanceRef " << (idInstance.count(ref) ? idInstance[ref] : ref) << "))";
        }
        o << "))\n";
      }
      o << "        ))))\n  (design circuit (cellRef circuit (libraryRef circuit_lib))))\n";
      break;
    }
    case FormatNetlist::Spice: {
      o << "* " << titre << " — netlist écrite par Prométhée\n";
      for (const auto& x : n.composants) {
        auto broches = parComposant[x.reference];
        std::sort(broches.begin(), broches.end(), [](const auto& a, const auto& b) {
          const bool na = !a.first.empty() && std::all_of(a.first.begin(), a.first.end(), [](unsigned char ch) { return std::isdigit(ch); });
          const bool nb2 = !b.first.empty() && std::all_of(b.first.begin(), b.first.end(), [](unsigned char ch) { return std::isdigit(ch); });
          return na && nb2 ? std::stoi(a.first) < std::stoi(b.first) : a.first < b.first;
        });
        o << x.reference;
        for (const auto& [b, r] : broches) o << " " << (r == "GND" ? std::string("0") : r);
        o << " " << (x.valeur.empty() ? "?" : x.valeur) << "\n";
      }
      o << ".end\n";
      break;
    }
    case FormatNetlist::Geda:
      for (const auto& [reseau, noeuds] : n.reseaux) {
        o << reseau;
        for (const auto& [ref, broche] : noeuds) o << " " << ref << "-" << broche;
        o << "\n";
      }
      break;
    case FormatNetlist::TinyCad:
      o << "====================\nTinyCAD Netlist (" << titre << ", " << date() << ")\n====================\n\n";
      for (const auto& x : n.composants) {
        o << "COMPONENT '" << x.reference << "'\n";
        if (!x.valeur.empty()) o << "OPTION 'Value' = '" << x.valeur << "'\n";
        if (!x.empreinte.empty()) o << "OPTION 'Package' = '" << x.empreinte << "'\n";
      }
      o << "\n";
      for (const auto& [reseau, noeuds] : n.reseaux) {
        o << "NET  '" << reseau << "'  =  ";
        bool premier = true;
        for (const auto& [ref, broche] : noeuds) o << (premier ? "" : ",") << "(" << ref << "," << broche << ")", premier = false;
        o << "\n";
      }
      break;
  }
  return o.str();
}

std::string netlistGabarit(const NetlistLue& n, const std::string& gabarit, const std::string& titre) {
  const auto ls = lignes(gabarit);
  std::size_t premiere = ls.size(), derniere = 0;
  for (std::size_t i = 0; i < ls.size(); ++i)
    if (ls[i].rfind("@", 0) == 0) {
      premiere = std::min(premiere, i);
      derniere = i;
    }
  auto remplacer = [&](std::string s, const std::map<std::string, std::string>& v) {
    std::map<std::string, std::string> tout = v;
    tout["date"] = date();
    tout["titre"] = titre;
    std::string r;
    for (std::size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '{') {
        const auto f = s.find('}', i);
        if (f != std::string::npos) {
          const auto it = tout.find(s.substr(i + 1, f - i - 1));
          if (it != tout.end()) {
            r += it->second;
            i = f;
            continue;
          }
        }
      }
      r += s[i];
    }
    return r;
  };
  std::string o;
  for (std::size_t i = 0; i < premiere && i < ls.size(); ++i) o += remplacer(ls[i], {}) + "\n";
  std::string modeleComposant, modeleReseau, modeleBroche;
  for (std::size_t i = premiere; i <= derniere && i < ls.size(); ++i) {
    const std::string& l = ls[i];
    if (l.rfind("@composant", 0) == 0) modeleComposant = l.size() > 11 ? l.substr(11) : std::string();
    else if (l.rfind("@reseau", 0) == 0) modeleReseau = l.size() > 8 ? l.substr(8) : std::string();
    else if (l.rfind("@broche", 0) == 0) modeleBroche = l.size() > 8 ? l.substr(8) : std::string();
  }
  if (premiere < ls.size()) {
    if (!modeleComposant.empty())
      for (const auto& x : n.composants) o += remplacer(modeleComposant, {{"ref", x.reference}, {"valeur", x.valeur}, {"empreinte", x.empreinte}}) + "\n";
    if (!modeleReseau.empty() || !modeleBroche.empty())
      for (const auto& [reseau, noeuds] : n.reseaux) {
        if (!modeleReseau.empty()) o += remplacer(modeleReseau, {{"nom", reseau}, {"nombre", std::to_string(noeuds.size())}}) + "\n";
        if (!modeleBroche.empty())
          for (const auto& [ref, broche] : noeuds) o += remplacer(modeleBroche, {{"ref", ref}, {"broche", broche}, {"nom", reseau}}) + "\n";
      }
  }
  for (std::size_t i = premiere < ls.size() ? derniere + 1 : ls.size(); i < ls.size(); ++i) o += remplacer(ls[i], {}) + "\n";
  return o;
}

}  // namespace prom::ecad

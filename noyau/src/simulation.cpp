// Prométhée : simulation du schéma (lot L11) : netlist SPICE, contrôles, mesures, boîte à calculs. Le moteur ngspice
// est dans moteur_ngspice.cpp. Licence GPL-3.0-only.
#include "promethee/simulation.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <regex>
#include <set>
#include <sstream>

#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
std::string nb(double v) {
  char t[48];
  std::snprintf(t, sizeof t, "%.9g", v);
  return t;
}
std::string minuscules(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}
bool estMasse(const std::string& n) {
  const std::string m = minuscules(n);
  return m == "gnd" || m == "0" || m == "agnd" || m == "dgnd" || m == "pgnd" || m == "masse" || m == "gnda" || m == "vss" || m == "0v";
}
// Tension d'un réseau d'alimentation d'après son nom : « +5V » → 5, « +3V3 » → 3,3, « -12V » → −12, « VCC » → 5
std::optional<double> tensionDeNom(const std::string& nom) {
  static const std::regex motif(R"(^([+-]?)(\d+)V(\d*)$)", std::regex::icase);
  std::smatch m;
  if (std::regex_match(nom, m, motif)) {
    double v = std::stod(m[2].str());
    if (!m[3].str().empty()) v += std::stod("0." + m[3].str());
    return m[1].str() == "-" ? -v : v;
  }
  const std::string n = minuscules(nom);
  if (n == "vcc" || n == "vdd" || n == "v+") return 5.0;
  return std::nullopt;
}
// Nombre lu dans un texte (« 9V », « 3,3 V », « AMS1117-3.3 » → 3,3)
std::optional<double> nombreDans(const std::string& t, bool dernier = false) {
  static const std::regex motif(R"((\d+(?:[.,]\d+)?))");
  std::optional<double> r;
  for (std::sregex_iterator it(t.begin(), t.end(), motif), fin; it != fin; ++it) {
    std::string x = (*it)[1].str();
    std::replace(x.begin(), x.end(), ',', '.');
    r = std::stod(x);
    if (!dernier) break;
  }
  return r;
}

const char* MODELE_555 = R"(.subckt NE555 gnd trig out reset ctrl thres disch vcc
* Temporisateur 555 comportemental (Prométhée) : diviseur 5k/5k/5k, comparateurs, bascule, sortie, décharge
R1 vcc ctrl 5k
R2 ctrl lo 5k
R3 lo gnd 5k
Bs s gnd V = (V(lo) - V(trig)) > 0 ? 1 : 0
Br r gnd V = (V(thres) - V(ctrl)) > 0 ? 1 : 0
Bq qn gnd V = (V(reset, gnd) < 0.7) ? 0 : ((V(s) > 0.5) ? 1 : ((V(r) > 0.5) ? 0 : ((V(q) > 0.5) ? 1 : 0)))
Rq qn q 1k
Cq q gnd 10p
Bqb qb gnd V = 1 - V(q)
Bo outi gnd V = V(q) > 0.5 ? V(vcc, gnd) - 1.7 : 0.1
Ro outi out 10
Sd disch gnd qb gnd swdecharge
.model swdecharge sw(vt=0.5 vh=0.1 ron=10 roff=1e9)
.ends NE555
)";
const char* MODELE_AOP = R"(.subckt AOP inp inm vcc vee out
* Amplificateur opérationnel comportemental (Prométhée) : gain 1e5, pôle à 10 Hz, sortie bornée aux rails par une
* tangente hyperbolique (pente jamais nulle : le point de repos se trouve sans s'accrocher à un rail)
Rin inp inm 10meg
Bg n1 0 V = 1e5 * V(inp, inm)
Rp n1 n2 1k
Cp n2 0 15.9u
Bo o1 0 V = (V(vcc)+V(vee))/2 + ((V(vcc)-V(vee))/2 - 0.1) * tanh((V(n2) - (V(vcc)+V(vee))/2) / ((V(vcc)-V(vee))/2 - 0.1))
Ro o1 out 50
.ends AOP
)";
const char* MODELE_REGULATEUR = R"(.subckt REGULATEUR vi vo gnd params: vout=5 chute=1.5
* Régulateur linéaire comportemental (Prométhée) : sortie bornée par l'entrée moins la chute
Rq vi gnd 100k
Bo o1 gnd V = max(min(V(vi, gnd) - chute, vout), 0)
Ro o1 vo 0.1
.ends REGULATEUR
)";
}  // namespace

// Compteur binaire 4 bits (14.2) : quatre bascules T de XSPICE en cascade, horloge et remise à zéro par des ponts
// analogique-numérique, sorties par des ponts numérique-analogique en logique 5 V
const char* const MODELE_COMPTEUR4 = R"(.subckt COMPTEUR4 clk raz q0 q1 q2 q3 vcc gnd
a_in [clk raz] [d_clk d_raz] pont_entree_c4
a_un d_un un_c4
a0 d_un d_clk null d_raz d_q0 d_nq0 bascule_t_c4
a1 d_un d_nq0 null d_raz d_q1 d_nq1 bascule_t_c4
a2 d_un d_nq1 null d_raz d_q2 d_nq2 bascule_t_c4
a3 d_un d_nq2 null d_raz d_q3 d_nq3 bascule_t_c4
a_out [d_q0 d_q1 d_q2 d_q3] [q0 q1 q2 q3] pont_sortie_c4
r_vcc vcc gnd 1meg
.model pont_entree_c4 adc_bridge(in_low=1.5 in_high=3.5)
.model pont_sortie_c4 dac_bridge(out_low=0 out_high=5 out_undef=2.5 t_rise=1n t_fall=1n)
.model bascule_t_c4 d_tff(clk_delay=10n set_delay=10n reset_delay=10n)
.model un_c4 d_pullup
.ends COMPTEUR4
)";
namespace {
// Décodeur BCD vers sept segments (14.2) : sources comportementales, somme de produits d'entrées seuillées en douceur
// (aucun saut de dérivée), sorties par 220 Ω
std::string decodeur7() {
  static const char* segments[7] = {"0,2,3,5,6,7,8,9", "0,1,2,3,4,7,8,9", "0,1,3,4,5,6,7,8,9", "0,2,3,5,6,8,9", "0,2,6,8", "0,4,5,6,8,9", "2,3,4,5,6,8,9"};
  static const char* noms[7] = {"a", "b", "c", "d", "e", "f", "g"};
  std::string t = ".subckt DECODEUR7 ea eb ec ed sa sb sc sd se sf sg vcc gnd\n";
  const char* entrees[4] = {"ea", "eb", "ec", "ed"};
  for (int i = 0; i < 4; ++i)
    t += std::string("b_x") + std::to_string(i) + " x" + std::to_string(i) + " gnd v = (1 + tanh(40 * (v(" + entrees[i] + ", gnd) - v(vcc, gnd) / 2))) / 2\n";
  for (int s = 0; s < 7; ++s) {
    std::string e;
    std::string liste = segments[s];
    std::size_t p = 0;
    while (p <= liste.size()) {
      const std::size_t q = liste.find(',', p);
      const int k = std::stoi(liste.substr(p, q == std::string::npos ? std::string::npos : q - p));
      std::string terme;
      for (int i = 0; i < 4; ++i) terme += std::string(terme.empty() ? "" : " * ") + (((k >> i) & 1) ? "v(x" + std::to_string(i) + ", gnd)" : "(1 - v(x" + std::to_string(i) + ", gnd))");
      e += (e.empty() ? "" : " + ") + terme;
      if (q == std::string::npos) break;
      p = q + 1;
    }
    t += std::string("b_") + noms[s] + " i" + noms[s] + " gnd v = v(vcc, gnd) * (" + e + ")\n";
    t += std::string("r_") + noms[s] + " i" + noms[s] + " s" + noms[s] + " 220\n";
  }
  for (int i = 0; i < 4; ++i) t += std::string("r_e") + std::to_string(i) + " " + entrees[i] + " gnd 1meg\n";
  t += ".ends DECODEUR7\n";
  return t;
}
const std::string MODELE_DECODEUR7 = decodeur7();
}  // namespace

const std::map<std::string, std::string>& modelesFournis() {
  static const std::map<std::string, std::string> m = {
      {"NE555", MODELE_555},
      {"AOP", MODELE_AOP},
      {"REGULATEUR", MODELE_REGULATEUR},
      {"DLED", ".model DLED D(IS=1e-22 N=1.5 RS=2 BV=5 IBV=10u CJO=10p)\n"},
      {"COMPTEUR4", MODELE_COMPTEUR4},
      {"DECODEUR7", MODELE_DECODEUR7},
      {"SWCOMMANDE", ".model SWCOMMANDE sw(vt=0.5 vh=0.2 ron=0.01 roff=1e9)\n"},
      // Broche d'un microcontrôleur co-simulé (15.4) : sortie de 25 Ω, tirage de 35 kΩ
      {"SWBROCHE", ".model SWBROCHE sw(vt=0.5 vh=0.2 ron=25 roff=1e10)\n"},
      {"SWTIRAGE", ".model SWTIRAGE sw(vt=0.5 vh=0.2 ron=35k roff=1e10)\n"},
      {"MOTEURCC", ".subckt MOTEURCC p n w params: r=2 l=1m ke=0.01 kt=0.01 j=1e-5 b=1e-6 tl=0\nr_a p a {r}\nl_a a c {l}\nv_i c d 0\ne_fcem d n w 0 {ke}\n"
                   "f_couple 0 w v_i {kt}\nc_j w 0 {j}\nr_b w 0 {1/b}\ni_tl w 0 dc {tl}\n.ends MOTEURCC\n"},
      {"D1N4148", ".model D1N4148 D(IS=2.52n RS=0.568 N=1.752 CJO=4p M=0.4 TT=20n BV=100 IBV=100u)\n"},
      {"DZENER", ".model DZENER D(IS=1e-14 RS=5 BV=5.1 IBV=5m)\n"},
      {"DSCHOTTKY", ".model DSCHOTTKY D(IS=1e-5 RS=0.05 N=1.05 CJO=100p BV=40)\n"},
      {"QNPN", ".model QNPN NPN(IS=1e-14 BF=200 VAF=100 CJE=8p CJC=3p TF=0.4n TR=50n)\n"},
      {"QPNP", ".model QPNP PNP(IS=1e-14 BF=150 VAF=100 CJE=8p CJC=3p TF=0.4n TR=50n)\n"},
  };
  return m;
}

// ---------- Modèles d'autres simulateurs (11.5) ----------
namespace {
// Nombre SPICE avec son suffixe (« 1MEG », « 4.7k », « 10u », « 2.5 »)
double nombreSpice(const std::string& t) {
  const std::string m = minuscules(t);
  char* fin = nullptr;
  double v = std::strtod(m.c_str(), &fin);
  const std::string s = fin ? std::string(fin) : std::string();
  if (s.rfind("meg", 0) == 0) v *= 1e6;
  else if (s.rfind("mil", 0) == 0) v *= 25.4e-6;
  else if (!s.empty()) switch (s[0]) {
      case 't': v *= 1e12; break;
      case 'g': v *= 1e9; break;
      case 'k': v *= 1e3; break;
      case 'm': v *= 1e-3; break;
      case 'u': v *= 1e-6; break;
      case 'n': v *= 1e-9; break;
      case 'p': v *= 1e-12; break;
      case 'f': v *= 1e-15; break;
      default: break;
    }
  return v;
}
bool lettreIdent(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; }
// Appel « fonction( » au début d'un mot, en partant de « depuis » ; rend la position du nom
std::size_t appel(const std::string& l, const char* fonction, std::size_t depuis = 0) {
  const std::string m = minuscules(l), f = fonction;
  for (std::size_t p = m.find(f, depuis); p != std::string::npos; p = m.find(f, p + 1)) {
    if (p > 0 && lettreIdent(m[p - 1])) continue;
    std::size_t q = p + f.size();
    while (q < m.size() && m[q] == ' ') ++q;
    if (q < m.size() && m[q] == '(') return p;
  }
  return std::string::npos;
}
// Arguments d'un appel dont la parenthèse ouvrante est en « ouvre » : rend la position de la fermante
std::size_t arguments(const std::string& l, std::size_t ouvre, std::vector<std::string>& args) {
  int profondeur = 0;
  std::string courant;
  for (std::size_t i = ouvre; i < l.size(); ++i) {
    const char ch = l[i];
    if (ch == '(') {
      if (profondeur++ > 0) courant += ch;
    } else if (ch == ')') {
      if (--profondeur == 0) {
        args.push_back(courant);
        return i;
      }
      courant += ch;
    } else if (ch == ',' && profondeur == 1) {
      args.push_back(courant);
      courant.clear();
    } else
      courant += ch;
  }
  return std::string::npos;
}
// Remplace chaque appel de « fonction » à « n » arguments par ce que rend « ecrire » ; rend le nombre de changements
int remplacerAppels(std::string& l, const char* fonction, std::size_t n, const std::function<std::string(const std::vector<std::string>&)>& ecrire) {
  int changes = 0;
  for (std::size_t p = appel(l, fonction); p != std::string::npos; p = appel(l, fonction, p + 1)) {
    const std::size_t ouvre = l.find('(', p);
    std::vector<std::string> args;
    const std::size_t ferme = arguments(l, ouvre, args);
    if (ferme == std::string::npos || args.size() != n) continue;
    for (auto& a : args) {
      const auto d = a.find_first_not_of(" \t"), f = a.find_last_not_of(" \t");
      a = d == std::string::npos ? std::string() : a.substr(d, f - d + 1);
    }
    const std::string r = ecrire(args);
    l.replace(p, ferme + 1 - p, r);
    ++changes;
  }
  return changes;
}
// Paramètres « NOM=valeur » d'une carte .model (espaces ou virgules entre eux)
std::map<std::string, std::string> parametresModele(const std::string& l) {
  std::map<std::string, std::string> r;
  static const std::regex p(R"(([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^\s,()]+))");
  for (std::sregex_iterator m(l.begin(), l.end(), p), fin; m != fin; ++m) r[minuscules((*m)[1].str())] = (*m)[2].str();
  return r;
}
}  // namespace

AdaptationModele adapterModele(const std::string& texte, const std::string& compatibilite) {
  AdaptationModele r;
  (void)compatibilite;
  std::istringstream in(texte);
  int numero = 0;
  static const std::vector<std::string> primitivesLt = {"dflop", "srflop", "and", "or", "xor", "buf", "inv", "schmitt", "schmtbuf", "schmtinv", "diffschmitt", "ota",
                                                        "varistor", "phidet", "modulator", "samplehold", "counter"};
  for (std::string l; std::getline(in, l);) {
    ++numero;
    if (!l.empty() && l.back() == '\r') l.pop_back();
    const std::string m = minuscules(l);
    const std::string ligne = "ligne " + std::to_string(numero);
    std::size_t i = 0;
    while (i < m.size() && std::isspace(static_cast<unsigned char>(m[i]))) ++i;
    const std::string debut = m.substr(i);
    if (!debut.empty() && debut[0] == '*') {
      if (debut.find("$cdnencstart") != std::string::npos || debut.find("encrypt") != std::string::npos) r.refus.push_back(ligne + " : modèle chiffré, illisible hors de son simulateur");
      r.texte += l + "\n";
      continue;
    }
    if (debut.find("$cdnenc") != std::string::npos || (debut.rfind(".protect", 0) == 0 && debut.find("encrypt") != std::string::npos)) {
      r.refus.push_back(ligne + " : modèle chiffré, illisible hors de son simulateur");
      continue;
    }
    for (const char* x : {".stimulus", ".stimlib", ".distribution", ".machine"})
      if (debut.rfind(x, 0) == 0) r.refus.push_back(ligne + " : « " + std::string(x) + " » n’existe pas dans ngspice");
    if (!debut.empty() && debut[0] == 'a') {
      std::istringstream mots(debut);
      std::vector<std::string> v;
      for (std::string w; mots >> w;) v.push_back(w);
      for (const auto& w : v)
        if (std::find(primitivesLt.begin(), primitivesLt.end(), w) != primitivesLt.end()) {
          r.refus.push_back(ligne + " : primitive « " + w + " » de LTspice, sans équivalent dans ngspice");
          break;
        }
    }
    std::string x = l;
    // IF(c, a, b) → ((c) ? (a) : (b)) ; LIMIT(v, a, b) → min(max(v, a), b)
    const int si = remplacerAppels(x, "if", 3, [](const std::vector<std::string>& a) { return "((" + a[0] + ") ? (" + a[1] + ") : (" + a[2] + "))"; });
    const int borne = remplacerAppels(x, "limit", 3, [](const std::vector<std::string>& a) { return "min(max(" + a[0] + ", " + a[1] + "), " + a[2] + ")"; });
    if (si) r.notes.push_back(ligne + " : IF() écrit en (condition ? oui : non)");
    if (borne) r.notes.push_back(ligne + " : LIMIT() écrit en min(max())");
    // TC=a,b sur une résistance, un condensateur, une inductance
    static const std::regex tc(R"(\bTC\s*=\s*([-+0-9.eE]+[a-zA-Z]*)\s*,\s*([-+0-9.eE]+[a-zA-Z]*))", std::regex::icase);
    if (std::regex_search(x, tc)) {
      x = std::regex_replace(x, tc, "tc1=$1 tc2=$2");
      r.notes.push_back(ligne + " : TC=a,b écrit en tc1, tc2");
    }
    // Interrupteurs de PSpice
    static const std::regex vsw(R"(^(\s*\.model\s+\S+\s+)(vswitch|iswitch)\b(.*)$)", std::regex::icase);
    std::smatch ms;
    if (std::regex_match(x, ms, vsw)) {
      const auto p = parametresModele(ms[3].str());
      auto lire = [&](const char* cle, double defaut) { return p.count(cle) ? nombreSpice(p.at(cle)) : defaut; };
      const bool tension = minuscules(ms[2].str()) == "vswitch";
      const double on = lire(tension ? "von" : "ion", tension ? 1 : 1e-3), off = lire(tension ? "voff" : "ioff", 0);
      x = ms[1].str() + (tension ? "sw(" : "csw(") + (tension ? "vt=" : "it=") + nb((on + off) / 2) + (tension ? " vh=" : " ih=") + nb(std::abs(on - off) / 2) + " ron=" +
          nb(lire("ron", 1)) + " roff=" + nb(lire("roff", 1e6)) + ")";
      r.notes.push_back(ligne + " : interrupteur " + ms[2].str() + " de PSpice écrit en " + (tension ? "sw" : "csw") + " (bascule à hystérésis au lieu de la transition douce)");
    }
    r.texte += x + "\n";
  }
  return r;
}

std::string nomModele(const std::string& texte) {
  static const std::regex sc(R"(^\s*\.subckt\s+(\S+))", std::regex::icase | std::regex::multiline);
  static const std::regex md(R"(^\s*\.model\s+(\S+))", std::regex::icase | std::regex::multiline);
  std::smatch m;
  if (std::regex_search(texte, m, sc)) return m[1].str();
  if (std::regex_search(texte, m, md)) return m[1].str();
  return {};
}

int noeudsSousCircuit(const std::string& texte, const std::string& nom) {
  std::istringstream in(texte);
  std::vector<std::string> lignes;
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    lignes.push_back(l);
  }
  for (std::size_t i = 0; i < lignes.size(); ++i) {
    std::istringstream mots(lignes[i]);
    std::string a, b;
    mots >> a >> b;
    if (minuscules(a) != ".subckt" || minuscules(b) != minuscules(nom)) continue;
    int n = 0;
    auto compter = [&](std::istringstream& s) {
      for (std::string w; s >> w;) {
        if (minuscules(w) == "params:" || w.find('=') != std::string::npos) return false;
        ++n;
      }
      return true;
    };
    bool suite = compter(mots);
    for (std::size_t j = i + 1; suite && j < lignes.size(); ++j) {
      std::size_t p = lignes[j].find_first_not_of(" \t");
      if (p == std::string::npos || lignes[j][p] != '+') break;
      std::istringstream s(lignes[j].substr(p + 1));
      suite = compter(s);
    }
    return n;
  }
  return -1;
}

std::string AnalyseSpice::carte() const {
  switch (type) {
    case Type::PointDeRepos: return ".op";
    case Type::Continu: return ".dc " + (source.empty() ? std::string("V1") : source) + " " + nb(dcDebut) + " " + nb(dcFin) + " " + nb(dcPas);
    case Type::Alternatif: return ".ac dec " + std::to_string(std::max(1, pointsParDecade)) + " " + nb(fDebut) + " " + nb(fFin);
    case Type::Transitoire: return ".tran " + nb(pas) + " " + nb(duree) + " " + nb(debut);
  }
  return {};
}

namespace {
// Texte de la valeur pour la netlist : un paramètre entre accolades est gardé tel quel
std::string valeurSpice(const std::string& texte, double nombre) {
  const auto a = texte.find('{');
  if (a != std::string::npos && texte.find('}', a) != std::string::npos) return texte.substr(a, texte.rfind('}') - a + 1);
  return nb(nombre);
}
bool commencePar(const std::string& s, const char* p) {
  std::size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  const std::size_t n = std::strlen(p);
  if (s.size() - i < n) return false;
  for (std::size_t k = 0; k < n; ++k)
    if (std::tolower(static_cast<unsigned char>(s[i + k])) != std::tolower(static_cast<unsigned char>(p[k]))) return false;
  return true;
}
// Directives SPICE posées sur les feuilles : textes dont chaque ligne non vide commence par « . », « + » ou « * »
std::vector<std::string> directivesDesFeuilles(const ConceptionElec& c) {
  std::vector<std::string> r;
  for (const auto& f : c.feuilles)
    for (const auto& t : f.textes) {
      std::istringstream in(t.texte);
      std::vector<std::string> lignes;
      bool directive = true, une = false;
      for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        std::size_t i = 0;
        while (i < l.size() && std::isspace(static_cast<unsigned char>(l[i]))) ++i;
        if (i == l.size()) continue;
        if (l[i] != '.' && l[i] != '+' && l[i] != '*' && !une) {
          directive = false;
          break;
        }
        // Dans un bloc .control, toute ligne est une commande
        lignes.push_back(l.substr(i));
        if (l[i] == '.') une = commencePar(l.substr(i), ".control") ? true : commencePar(l.substr(i), ".endc") ? false : une;
      }
      if (directive && !lignes.empty())
        for (auto& l : lignes) r.push_back(l);
    }
  return r;
}
// Paramètres dans l'ordre de leurs dépendances (tri topologique) ; une définition circulaire est refusée
std::vector<ParametreGlobal> parametresOrdonnes(const std::vector<ParametreGlobal>& p, std::string& erreur) {
  std::map<std::string, std::size_t> indice;
  for (std::size_t i = 0; i < p.size(); ++i) indice[minuscules(p[i].nom)] = i;
  std::vector<std::set<std::size_t>> dep(p.size());
  static const std::regex mot(R"([A-Za-z_][A-Za-z0-9_]*)");
  for (std::size_t i = 0; i < p.size(); ++i)
    for (std::sregex_iterator m(p[i].expression.begin(), p[i].expression.end(), mot), fin; m != fin; ++m)
      if (const auto it = indice.find(minuscules(m->str())); it != indice.end() && it->second != i) dep[i].insert(it->second);
  std::vector<int> etat(p.size(), 0);
  std::vector<ParametreGlobal> r;
  std::function<bool(std::size_t)> visiter = [&](std::size_t i) {
    if (etat[i] == 2) return true;
    if (etat[i] == 1) return false;
    etat[i] = 1;
    for (std::size_t d : dep[i])
      if (!visiter(d)) {
        if (erreur.empty()) erreur = p[i].nom + " et " + p[d].nom + " se définissent l’un par l’autre : corrige l’une des formules";
        return false;
      }
    etat[i] = 2;
    r.push_back(p[i]);
    return true;
  };
  for (std::size_t i = 0; i < p.size(); ++i)
    if (!visiter(i)) return {};
  return r;
}
}  // namespace

NetlistSpice netlistSpice(const ConceptionElec& c, const AnalyseSpice& a) { return netlistSpice(c, a, OptionsSimulation{}); }

NetlistSpice netlistSpice(const ConceptionElec& c, const AnalyseSpice& a, const OptionsSimulation& o) {
  NetlistSpice n;
  const Connectivite k = connectivite(c);
  // Partie simulée seule : composants retenus, réseaux coupés
  const bool partie = !o.composants.empty();
  auto dansPartie = [&](const ComposantElec& x) { return !partie || o.composants.count(x.uuid) > 0; };
  if (partie)
    for (const auto& r : k.reseaux) {
      bool dedans = false, dehors = false;
      for (const auto& b : r.broches) {
        const ComposantElec* x = c.composant(b.composant);
        if (!x || x->reference.empty() || x->reference[0] == '#') continue;
        (o.composants.count(x->uuid) ? dedans : dehors) = true;
      }
      if (dedans && dehors && !estMasse(r.nom)) n.coupees.push_back(r.nom);
    }
  // Nœuds : la masse en 0, les autres d'après le nom du réseau
  std::set<std::string> pris{"0"};
  auto noeud = [&](const std::string& reseau) -> std::string {
    if (reseau.empty()) return {};
    if (const auto it = n.noeuds.find(reseau); it != n.noeuds.end()) return it->second;
    if (estMasse(reseau)) return n.noeuds[reseau] = "0";
    std::string x = "n_";
    for (unsigned char ch : reseau) x += std::isalnum(ch) ? static_cast<char>(std::tolower(ch)) : '_';
    std::string y = x;
    for (int i = 2; pris.count(y); ++i) y = x + "_" + std::to_string(i);
    pris.insert(y);
    return n.noeuds[reseau] = y;
  };
  // Une broche sans liaison reçoit un nœud propre (elle ne touche rien)
  int libres = 0;
  auto noeudBroche = [&](const ComposantElec& x, const std::string& broche) {
    if (const auto it = o.noeudsBroches.find({x.uuid, broche}); it != o.noeudsBroches.end()) return it->second;
    const std::string r = k.nom(x.uuid, broche);
    return r.empty() ? "nc_" + std::to_string(++libres) : noeud(r);
  };
  std::ostringstream el;
  std::set<std::string> modeles;
  std::set<std::string> bibliotheques;                 // textes « Sim.Bibliotheque », une fois chacun
  std::map<std::string, std::string> modelesArticles;  // nom → texte adapté, une fois chacun
  std::set<std::string> compatibilites;
  std::set<std::string> sourcesPositives;  // nœuds tenus par une source posée
  // Liaisons en continu, pour le contrôle des nœuds flottants
  std::vector<std::pair<std::string, std::string>> liaisonsContinu;
  auto valeurDe = [&](const ComposantElec& x, double defaut) {
    const ValeurComprise v = lireValeurComposant(x.valeur, familleDe(c, x));
    if (v.comprise() && v.nombre > 0) return v.nombre;
    if (const auto d = nombreDans(x.valeur)) return *d;
    return defaut;
  };
  // Source continue : en rampe au début du transitoire si demandé (remède de convergence)
  const bool rampe = o.rampeSources && a.type == AnalyseSpice::Type::Transitoire;
  const double dureeRampe = std::max(a.duree * 0.01, 10 * a.pas);
  auto continu = [&](double v) { return rampe ? "PWL(0 0 " + nb(dureeRampe) + " " + nb(v) + ")" : "DC " + nb(v); };
  std::set<std::string> poses;
  for (const auto& x : c.composants) {
    if (x.reference.empty() || x.reference[0] == '#') continue;
    if (!dansPartie(x)) continue;
    const std::string ref = x.reference;
    const auto avantComposant = static_cast<std::size_t>(el.tellp());
    // Éléments écrits pour ce composant : instance SPICE → repère
    auto noterInstances = [&]() {
      const std::string t = el.str().substr(avantComposant);
      std::istringstream in(t);
      for (std::string l; std::getline(in, l);)
        if (!l.empty() && std::isalpha(static_cast<unsigned char>(l[0]))) n.instances[minuscules(l.substr(0, l.find(' ')))] = ref;
    };
    if (const auto it = x.champs.find("Sim.Exclure"); it != x.champs.end() && (it->second == "1" || minuscules(it->second) == "oui")) {
      n.avertissements.push_back(ref + " écarté de la simulation (champ Sim.Exclure)");
      continue;
    }
    // Co-simulation (15.4, 15.7) : microcontrôleur émulé ou composant décrit en HDL. Chaque broche reliée est tenue
    // par des sources externes que le participant règle à chaque pas : niveau et validation de la sortie, tirage
    // (microcontrôleur) ; sortie de 50 Ω ou entrée à haute impédance (HDL)
    if (const auto ic = o.cosimulation.find(ref); o.continu && ic != o.cosimulation.end()) {
      if (const DefSymbole* d = c.symbole(x.symbole)) {
        const bool micro = ic->second == "micro";
        std::string vcc;
        for (const auto& b : d->broches)
          if ((b.type == TypeBroche::AlimEntree || b.type == TypeBroche::AlimSortie) && (minuscules(b.nom) == "vcc" || minuscules(b.nom) == "vdd") && !k.nom(x.uuid, b.numero).empty())
            vcc = noeudBroche(x, b.numero);
        for (const auto& b : d->broches) {
          if (b.type == TypeBroche::AlimEntree || b.type == TypeBroche::AlimSortie || k.nom(x.uuid, b.numero).empty()) continue;
          const std::string nd = noeudBroche(x, b.numero), base = ref + "_" + b.numero, interne = "cs_" + minuscules(base);
          if (micro) {
            el << "V" << base << "d " << interne << "d 0 dc 0 external\n";
            el << "S" << base << " " << interne << "d " << nd << " " << interne << "c 0 SWBROCHE\n";
            el << "V" << base << "c " << interne << "c 0 dc 0 external\n";
            if (!vcc.empty()) {
              el << "S" << base << "p " << vcc << " " << nd << " " << interne << "p 0 SWTIRAGE\n";
              el << "V" << base << "p " << interne << "p 0 dc 0 external\n";
            }
            modeles.insert("SWBROCHE");
            modeles.insert("SWTIRAGE");
          } else if (b.type == TypeBroche::Sortie || b.type == TypeBroche::TroisEtats || b.type == TypeBroche::Bidirectionnelle) {
            el << "V" << base << "d " << interne << "d 0 dc 0 external\n";
            el << "R" << base << " " << interne << "d " << nd << " 50\n";
          } else
            el << "R" << base << "e " << nd << " 0 1e9\n";
          liaisonsContinu.push_back({nd, "0"});
        }
        noterInstances();
        poses.insert(ref);
        continue;
      }
    }
    // Élément écrit à la main : « XU1 {1} {2} MONMODELE », les numéros de broches remplacés par leurs nœuds
    if (const auto it = x.champs.find("Sim.Modele"); it != x.champs.end() && !it->second.empty()) {
      std::string t = it->second;
      static const std::regex marque(R"(\{([^}]+)\})");
      std::string sortie;
      std::size_t debut = 0;
      std::vector<std::string> noeudsElement;
      for (std::sregex_iterator m(t.begin(), t.end(), marque), fin; m != fin; ++m) {
        sortie += t.substr(debut, static_cast<std::size_t>(m->position()) - debut);
        const std::string cle = (*m)[1].str();
        if (cle == "ref") sortie += ref;
        else {
          const std::string nd = noeudBroche(x, cle);
          noeudsElement.push_back(nd);
          sortie += nd;
        }
        debut = static_cast<std::size_t>(m->position() + m->length());
      }
      sortie += t.substr(debut);
      el << sortie << "\n";
      for (std::size_t i = 1; i < noeudsElement.size(); ++i) liaisonsContinu.push_back({noeudsElement[0], noeudsElement[i]});
      if (const auto b = x.champs.find("Sim.Bibliotheque"); b != x.champs.end() && bibliotheques.insert(b->second).second) el << b->second << "\n";
      noterInstances();
      poses.insert(ref);
      continue;
    }
    // Modèle de l'article (11.5) : sous-circuit ou modèle de semi-conducteur, broches dans l'ordre de ses nœuds
    if (const auto ia = c.articles.find(x.article); ia != c.articles.end() && !ia->second.modeleSpice.vide()) {
      const ModeleSpice& m = ia->second.modeleSpice;
      const DefSymbole* d = c.symbole(x.symbole);
      const std::string nom = m.nom.empty() ? nomModele(m.texte) : m.nom;
      if (!modelesArticles.count(nom)) {
        const AdaptationModele ad = adapterModele(m.texte, m.compatibilite);
        for (const auto& r : ad.refus) n.erreurs.push_back(ref + " : modèle « " + nom + " », " + r);
        for (const auto& t : ad.notes) n.avertissements.push_back(ref + " : modèle « " + nom + " », " + t);
        modelesArticles[nom] = ad.texte;
      }
      if (!m.compatibilite.empty()) compatibilites.insert(minuscules(m.compatibilite));
      const std::string tm = minuscules(m.texte);
      const bool sousCircuit = tm.find(".subckt") != std::string::npos;
      // Genre du modèle : la lettre de l'élément
      char lettre = 'X';
      std::vector<std::string> nomsDefaut;
      if (!sousCircuit) {
        static const std::regex type(R"(\.model\s+\S+\s+([a-z]+))", std::regex::icase);
        std::smatch mt;
        const std::string g = std::regex_search(tm, mt, type) ? mt[1].str() : std::string();
        if (g == "d") lettre = 'D', nomsDefaut = {"A", "K"};
        else if (g == "npn" || g == "pnp") lettre = 'Q', nomsDefaut = {"C", "B", "E"};
        else if (g == "nmos" || g == "pmos" || g == "vdmos") lettre = 'M', nomsDefaut = {"D", "G", "S"};
        else if (g == "njf" || g == "pjf") lettre = 'J', nomsDefaut = {"D", "G", "S"};
      }
      // Module Verilog-A compilé (11.12) : élément N, bibliothèque OSDI chargée avant le circuit
      if (minuscules(m.genre) == "verilog-a") {
        lettre = 'N';
        if (!m.fichier.empty() && std::find(n.chargements.begin(), n.chargements.end(), m.fichier) == n.chargements.end()) n.chargements.push_back(m.fichier);
      }
      std::vector<std::string> instances = m.broches;
      if (instances.empty()) {
        // Broches par leur nom (A, K ; C, B, E ; D, G, S), sinon toutes dans l'ordre de leurs numéros
        std::string l;
        if (d && !nomsDefaut.empty())
          for (const auto& nb0 : nomsDefaut)
            for (const auto& b : d->broches)
              if (minuscules(b.nom) == minuscules(nb0)) l += (l.empty() ? "" : " ") + b.numero;
        if (l.empty() && d) {
          std::vector<std::string> nums;
          for (const auto& b : d->broches) nums.push_back(b.numero);
          std::sort(nums.begin(), nums.end(), [](const std::string& p, const std::string& q) { return std::atoi(p.c_str()) != std::atoi(q.c_str()) ? std::atoi(p.c_str()) < std::atoi(q.c_str()) : p < q; });
          nums.erase(std::unique(nums.begin(), nums.end()), nums.end());
          for (const auto& b : nums) l += (l.empty() ? "" : " ") + b;
        }
        instances.push_back(l);
      }
      const int attendus = sousCircuit ? noeudsSousCircuit(m.texte, nom) : -1;
      // Broches propres à chaque instance : une instance dont aucune n'est reliée (unité non posée) est laissée
      std::map<std::string, int> partage;
      std::vector<std::vector<std::string>> listes;
      for (const auto& inst : instances) {
        std::istringstream in(inst);
        std::vector<std::string> v;
        for (std::string b; in >> b;) v.push_back(b);
        for (const auto& b : std::set<std::string>(v.begin(), v.end())) ++partage[b];
        listes.push_back(v);
      }
      int posees = 0;
      for (std::size_t i = 0; i < listes.size(); ++i) {
        const auto& v = listes[i];
        if (listes.size() > 1) {
          bool reliee = false;
          for (const auto& b : v)
            if (partage[b] == 1 && !k.nom(x.uuid, b).empty()) reliee = true;
          if (!reliee) {
            n.avertissements.push_back(ref + " : unité " + std::to_string(i + 1) + " non reliée, laissée hors du calcul");
            continue;
          }
        }
        if (attendus >= 0 && static_cast<int>(v.size()) != attendus)
          n.erreurs.push_back(ref + " : le sous-circuit « " + nom + " » attend " + std::to_string(attendus) + " nœud(s), l’article en relie " + std::to_string(v.size()));
        std::string ligne(1, lettre);
        ligne += ref + (i ? "_" + std::to_string(i + 1) : std::string());
        std::vector<std::string> nds;
        for (const auto& b : v) nds.push_back(noeudBroche(x, b));
        if (lettre == 'M' && nds.size() == 3) nds.push_back(nds[2]);  // substrat relié à la source
        for (const auto& nd : nds) ligne += " " + nd;
        el << ligne << " " << nom << "\n";
        for (std::size_t j = 1; j < nds.size(); ++j) liaisonsContinu.push_back({nds[0], nds[j]});
        ++posees;
      }
      if (posees) poses.insert(ref);
      noterInstances();
      continue;
    }
    const std::string s = x.symbole;
    auto N = [&](const char* broche) { return noeudBroche(x, broche); };
    auto deux = [&](const char* p1, const char* p2) {
      liaisonsContinu.push_back({N(p1), N(p2)});
      return N(p1) + " " + N(p2);
    };
    if (s == "R") el << "R" << ref << " " << deux("1", "2") << " " << valeurSpice(x.valeur, valeurDe(x, 1000)) << "\n";
    else if (s == "C" || s == "C_Pol") {
      el << "C" << ref << " " << N("1") << " " << N("2") << " " << valeurSpice(x.valeur, valeurDe(x, 1e-6));
      if (const auto ic = x.champs.find("Sim.IC"); ic != x.champs.end() && !ic->second.empty()) el << " IC=" << ic->second;
      el << "\n";
    } else if (s == "L") {
      el << "L" << ref << " " << deux("1", "2") << " " << valeurSpice(x.valeur, valeurDe(x, 1e-6));
      if (const auto ic = x.champs.find("Sim.IC"); ic != x.champs.end() && !ic->second.empty()) el << " IC=" << ic->second;
      el << "\n";
    } else if (s == "Fusible" || s == "Liaison_reseaux") el << "R" << ref << " " << deux("1", "2") << " 1m\n";
    else if (s == "Buzzer") el << "R" << ref << " " << deux("1", "2") << " 50\n";
    else if (s == "Moteur") {
      // Moteur à courant continu (14.11) : induit, force contre-électromotrice, inertie et frottement ; sa vitesse
      // (rad/s) est la tension du nœud « w_<repère> »
      auto champ = [&](const char* cle, double defaut) {
        const auto it = x.champs.find(cle);
        return it == x.champs.end() ? defaut : nombreSpice(it->second);
      };
      el << "X" << ref << " " << deux("1", "2") << " w_" << minuscules(ref) << " MOTEURCC params: r=" << nb(champ("Sim.R", 2)) << " l=" << nb(champ("Sim.L", 1e-3))
         << " ke=" << nb(champ("Sim.Ke", 0.01)) << " kt=" << nb(champ("Sim.Kt", 0.01)) << " j=" << nb(champ("Sim.J", 1e-5)) << " b=" << nb(std::max(1e-12, champ("Sim.B", 1e-6)))
         << " tl=" << nb(champ("Sim.Couple", 0)) << "\n";
      modeles.insert("MOTEURCC");
    }
    else if (s == "Bouton" || s == "Interrupteur") {
      const bool ferme = x.champs.count("Sim.Ferme") && x.champs.at("Sim.Ferme") == "1";
      if (o.continu) {
        // Simulation continue (14.2) : interrupteur commandé par une source externe, réglée pendant le calcul
        const std::string commande = "c_" + minuscules(ref);
        el << "S" << ref << " " << deux("1", "2") << " " << commande << " 0 SWCOMMANDE\n";
        el << "V" << ref << "_c " << commande << " 0 dc " << (ferme ? "1" : "0") << " external\n";
        modeles.insert("SWCOMMANDE");
      } else
        el << "R" << ref << " " << deux("1", "2") << " " << (ferme ? "1m" : "1e9") << "\n";
    } else if (s == "Compteur_4bits") {
      el << "X" << ref;
      for (const char* p : {"1", "2", "3", "4", "5", "6", "7", "8"}) el << " " << N(p);
      el << " COMPTEUR4\n";
      for (const char* p : {"1", "2", "3", "4", "5", "6", "7"}) liaisonsContinu.push_back({N("8"), N(p)});
      modeles.insert("COMPTEUR4");
    } else if (s == "Decodeur_7seg") {
      el << "X" << ref;
      for (const char* p : {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13"}) el << " " << N(p);
      el << " DECODEUR7\n";
      for (const char* p : {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"}) liaisonsContinu.push_back({N("13"), N(p)});
      modeles.insert("DECODEUR7");
    } else if (s == "Afficheur_7seg") {
      const char* segments = "abcdefg";
      int i = 0;
      for (const char* p : {"1", "2", "3", "4", "5", "6", "7"}) {
        el << "D" << ref << "_" << segments[i++] << " " << N(p) << " " << N("8") << " DLED\n";
        liaisonsContinu.push_back({N(p), N("8")});
      }
      modeles.insert("DLED");
    } else if (s == "Potentiometre") {
      const double r = valeurDe(x, 10'000);
      double pos = 0.5;
      if (const auto it = x.champs.find("Sim.Position"); it != x.champs.end())
        if (const auto v = nombreDans(it->second)) pos = std::clamp(*v > 1 ? *v / 100 : *v, 0.001, 0.999);
      el << "R" << ref << "a " << deux("1", "2") << " " << nb(r * pos) << "\n";
      el << "R" << ref << "b " << deux("2", "3") << " " << nb(r * (1 - pos)) << "\n";
    } else if (s == "D" || s == "LED" || s == "D_Zener" || s == "D_Schottky") {
      const char* m = s == "LED" ? "DLED" : s == "D_Zener" ? "DZENER" : s == "D_Schottky" ? "DSCHOTTKY" : "D1N4148";
      el << "D" << ref << " " << N("2") << " " << N("1") << " " << m << "\n";  // anode, cathode (broche 1 : cathode)
      liaisonsContinu.push_back({N("1"), N("2")});
      modeles.insert(m);
    } else if (s == "Q_NPN" || s == "Q_PNP") {
      const char* m = s == "Q_NPN" ? "QNPN" : "QPNP";
      el << "Q" << ref << " " << N("3") << " " << N("1") << " " << N("2") << " " << m << "\n";  // C B E (SOT-23 : 1 B, 2 E, 3 C)
      liaisonsContinu.push_back({N("1"), N("2")});
      liaisonsContinu.push_back({N("3"), N("2")});
      modeles.insert(m);
    } else if (s == "NE555") {
      el << "X" << ref << " " << N("1") << " " << N("2") << " " << N("3") << " " << N("4") << " " << N("5") << " " << N("6") << " " << N("7") << " " << N("8") << " NE555\n";
      for (const char* p : {"2", "3", "4", "5", "6", "7", "8"}) liaisonsContinu.push_back({N("1"), N(p)});
      modeles.insert("NE555");
    } else if (s == "AOP") {
      el << "X" << ref << " " << N("1") << " " << N("3") << " " << N("5") << " " << N("2") << " " << N("4") << " AOP\n";
      for (const char* p : {"1", "3", "4", "5"}) liaisonsContinu.push_back({N("2"), N(p)});
      modeles.insert("AOP");
    } else if (s == "LM358_unites") {
      // Une pose par unité ; l'alimentation (8 V+, 4 V−) commune
      const std::string vcc = N("8"), vee = N("4");
      el << "X" << ref << "A " << N("3") << " " << N("2") << " " << vcc << " " << vee << " " << N("1") << " AOP\n";
      el << "X" << ref << "B " << N("5") << " " << N("6") << " " << vcc << " " << vee << " " << N("7") << " AOP\n";
      for (const char* p : {"1", "2", "3", "5", "6", "7", "8"}) liaisonsContinu.push_back({vee, N(p)});
      modeles.insert("AOP");
    } else if (s == "Regulateur" || s == "LM7805") {
      const bool l7805 = s == "LM7805";
      const std::string vi = N(l7805 ? "1" : "3"), vo = N(l7805 ? "3" : "2"), gnd = N(l7805 ? "2" : "1");
      const double vout = l7805 ? 5.0 : nombreDans(x.valeur, true).value_or(3.3);
      el << "X" << ref << " " << vi << " " << vo << " " << gnd << " REGULATEUR params: vout=" << nb(vout) << " chute=" << (l7805 ? "2" : "1.1") << "\n";
      liaisonsContinu.push_back({vi, gnd});
      liaisonsContinu.push_back({vo, gnd});
      modeles.insert("REGULATEUR");
    } else if (s == "Pile") {
      el << "V" << ref << " " << deux("1", "2") << " " << continu(nombreDans(x.valeur).value_or(9)) << "\n";
      sourcesPositives.insert(N("1"));
    } else if (s == "Source_tension") {
      // La valeur dit la source : « 5 », « DC 5 AC 1 », « SIN(0 1 1k) », « PULSE(…) », ou une phrase PWL en français
      std::string v = x.valeur.empty() ? std::string("0") : x.valeur;
      std::string err;
      const std::string vm = minuscules(v);
      // Source « externe » (14.4) : générateur réglé pendant une simulation continue
      if (vm == "externe" || vm == "external" || vm.rfind("generateur", 0) == 0 || vm.rfind("générateur", 0) == 0) {
        if (o.continu) el << "V" << ref << " " << deux("1", "2") << " dc 0 external\n";
        else {
          el << "V" << ref << " " << deux("1", "2") << " dc 0\n";
          n.avertissements.push_back(ref + " : source externe (générateur) à 0 V hors de la simulation continue");
        }
        sourcesPositives.insert(N("1"));
        noterInstances();
        poses.insert(ref);
        continue;
      }
      if (vm.find("pendant") != std::string::npos || vm.find("rampe") != std::string::npos || vm.find("puis") != std::string::npos) {
        if (const std::string p = sourcePwl(v, &err); !p.empty()) v = p;
        else n.erreurs.push_back(ref + " : source non comprise (" + err + ")");
      }
      if (!std::isalpha(static_cast<unsigned char>(v[0])) && v.find('(') == std::string::npos && v.find('{') == std::string::npos) v = continu(nombreDans(v).value_or(0));
      else if (rampe && commencePar(v, "dc ") && vm.find(" ac") == std::string::npos) v = continu(nombreDans(v).value_or(0));
      el << "V" << ref << " " << deux("1", "2") << " " << v << "\n";
      sourcesPositives.insert(N("1"));
    } else if (s == "Source_courant") {
      std::string v = x.valeur.empty() ? std::string("0") : x.valeur;
      if (!std::isalpha(static_cast<unsigned char>(v[0])) && v.find('(') == std::string::npos) {
        const ValeurComprise vc = lireValeurComposant(v, FamilleValeur::Aucune);
        v = "DC " + nb(vc.comprise() ? vc.nombre : nombreDans(v).value_or(0) * (v.find("mA") != std::string::npos ? 1e-3 : 1));
      }
      el << "I" << ref << " " << N("1") << " " << N("2") << " " << v << "\n";
    } else {
      // Mécanique, connecteurs, points de test : sans effet sur le calcul ; un circuit intégré sans modèle est signalé
      const bool silencieux = s.rfind("Conn_", 0) == 0 || s.rfind("Bornier_", 0) == 0 || s == "Trou_fixation" || s == "Trou_fixation_masse" || s == "Entretoise" || s == "Vis" ||
                              s == "Dissipateur" || s == "Point_test" || s == "Quartz" || s == "Instrument";
      if (!silencieux) n.avertissements.push_back(ref + " (" + s + ") écarté : pas de modèle de simulation ; un champ Sim.Modele le donne");
      continue;
    }
    noterInstances();
    poses.insert(ref);
  }
  // Réseaux coupés d'une partie : une source, une charge ou rien, comme réglé
  std::ostringstream coupures;
  int nc = 0;
  for (const auto& r : n.coupees) {
    const auto it = std::find_if(o.liaisons.begin(), o.liaisons.end(), [&](const LiaisonCoupee& l) { return l.reseau == r; });
    if (it == o.liaisons.end() || it->bouclage == BouclageLiaison::Rien) {
      if (!tensionDeNom(r)) n.avertissements.push_back("réseau coupé « " + r + " » laissé en l’air");
      continue;
    }
    const std::string nd = noeud(r);
    if (nd == "0") continue;
    ++nc;
    if (it->bouclage == BouclageLiaison::Source) {
      coupures << "V_coupe" << nc << " " << nd << " 0 DC " << nb(it->valeur) << (it->ac != 0 ? " AC " + nb(it->ac) : std::string()) << "\n";
      sourcesPositives.insert(nd);
    } else
      coupures << "R_coupe" << nc << " " << nd << " 0 " << nb(it->valeur > 0 ? it->valeur : 1e3) << "\n";
    liaisonsContinu.push_back({nd, "0"});
  }
  // Réseaux d'alimentation nommés sans source : une source continue implicite
  std::ostringstream implicites;
  for (const auto& r : k.reseaux) {
    if (r.broches.empty()) continue;
    const auto v = tensionDeNom(r.nom);
    if (!v) continue;
    const std::string nd = noeud(r.nom);
    if (nd == "0" || sourcesPositives.count(nd)) continue;
    bool utilise = false;
    for (const auto& b : r.broches) utilise |= c.composant(b.composant) && poses.count(c.composant(b.composant)->reference);
    if (!utilise) continue;
    implicites << "V_" << nd << " " << nd << " 0 " << continu(*v) << "\n";
    liaisonsContinu.push_back({nd, "0"});
    n.avertissements.push_back("réseau « " + r.nom + " » alimenté par une source implicite de " + nb(*v) + " V");
  }
  // Contrôles avant le calcul : masse, nœuds sans chemin vers elle en continu
  std::set<std::string> tous;
  for (const auto& [r, nd] : n.noeuds) tous.insert(nd);
  if (!tous.count("0")) n.erreurs.push_back("pas de masse : nomme « GND » le réseau de référence (symbole de masse)");
  std::map<std::string, std::string> parent;
  std::function<std::string(const std::string&)> racine = [&](const std::string& x) -> std::string {
    auto it = parent.find(x);
    if (it == parent.end() || it->second == x) return x;
    return it->second = racine(it->second);
  };
  for (const auto& nd : tous) parent[nd] = nd;
  for (const auto& [u, v] : liaisonsContinu) {
    if (!parent.count(u)) parent[u] = u;
    if (!parent.count(v)) parent[v] = v;
    parent[racine(u)] = racine(v);
  }
  const bool shunt = o.moteur.count("rshunt") > 0;
  if (tous.count("0") && !shunt)
    for (const auto& [reseau, nd] : n.noeuds)
      if (nd != "0" && racine(nd) != racine("0")) n.erreurs.push_back("nœud sans chemin vers la masse en continu : « " + reseau + " » (un condensateur seul ne suffit pas)");
  // Directives des feuilles (11.8) : recopiées telles quelles ; un bloc .control seulement s'il est permis, jamais
  // la commande shell ; une analyse écrite à la main remplace celle des réglages
  const std::vector<std::string> directives = directivesDesFeuilles(c);
  bool analyseEcrite = false, controle = false;
  for (const auto& l : directives) {
    std::istringstream mots(l);
    std::string carte;
    mots >> carte;
    carte = minuscules(carte);
    for (const char* x : {".tran", ".ac", ".dc", ".op", ".noise", ".tf", ".disto", ".pz", ".sens", ".sp"})
      if (carte == x) analyseEcrite = true;
    if (commencePar(l, ".control")) controle = true;
    if (controle && minuscules(l).find("shell") != std::string::npos) n.erreurs.push_back("directive refusée : la commande « shell » de ngspice n’est jamais permise");
  }
  if (controle && !o.autoriserControle) n.erreurs.push_back("bloc .control des directives refusé : permets-le pour ce projet sur ce poste (réglages de la simulation)");
  if (analyseEcrite) n.avertissements.push_back("analyse écrite dans les directives de la feuille : elle remplace celle des réglages");
  // Paramètres du projet, dans l'ordre de leurs dépendances
  std::string erreurParametres;
  const std::vector<ParametreGlobal> parametres = parametresOrdonnes(o.parametres, erreurParametres);
  if (!erreurParametres.empty()) n.erreurs.push_back("paramètres : " + erreurParametres);
  // Compatibilité : celle des réglages, sinon celle des modèles
  if (!o.compatibilite.empty()) n.compatibilite = minuscules(o.compatibilite);
  else if (compatibilites.size() == 1) n.compatibilite = *compatibilites.begin();
  else if (compatibilites.size() > 1) {
    std::string t;
    for (const char* x : {"lt", "ps", "hs", "a", "ll"})
      for (const auto& cm : compatibilites)
        if (cm.find(x) != std::string::npos && t.find(x) == std::string::npos) t += x;
    n.compatibilite = t;
  }
  // Texte
  std::ostringstream t;
  t << "* " << (c.nom.empty() ? std::string("Conception") : c.nom) << " — netlist écrite par Prométhée\n";
  for (const auto& p : parametres) t << ".param " << p.nom << "=" << (p.expression.find_first_of("+-*/()") != std::string::npos ? "{" + p.expression + "}" : p.expression) << "\n";
  t << el.str() << coupures.str() << implicites.str();
  for (const auto& l : o.elementsEnPlus) t << l << "\n";
  for (const auto& m : modeles) t << modelesFournis().at(m);
  for (const auto& [nom, texte] : modelesArticles) t << texte << (texte.empty() || texte.back() == '\n' ? "" : "\n");
  // Conditions initiales, point de départ, options du moteur
  auto tensions = [&](const char* carte, const std::map<std::string, double>& m) {
    std::string l;
    for (const auto& [reseau, v] : m)
      if (const std::string nd = n.noeuds.count(reseau) ? n.noeuds.at(reseau) : std::string(); !nd.empty() && nd != "0") l += " v(" + nd + ")=" + nb(v);
    if (!l.empty()) t << carte << l << "\n";
  };
  tensions(".ic", o.conditionsInitiales);
  tensions(".nodeset", o.pointDepart);
  if (!o.moteur.empty()) {
    t << ".options";
    for (const auto& [cle, v] : o.moteur) t << " " << cle << (v.empty() ? std::string() : "=" + v);
    t << "\n";
  }
  for (const auto& l : directives) t << l << "\n";
  if (!o.sauvegardes.empty()) {
    t << (o.continu ? ".save" : ".save all");
    for (const auto& v : o.sauvegardes) t << " " << v;
    t << "\n";
  }
  if (!analyseEcrite) {
    std::string carte = a.carte();
    if (a.type == AnalyseSpice::Type::Transitoire && (o.pasMax > 0 || o.sansPointRepos)) {
      if (o.pasMax > 0) carte += " " + nb(o.pasMax);
      if (o.sansPointRepos) carte += " uic";
    }
    t << carte << "\n";
  }
  t << ".end\n";
  n.texte = t.str();
  return n;
}

std::string sourceCommande(const std::string& reference) { return "v" + minuscules(reference) + "_c"; }
std::string sourceExterne(const std::string& reference) { return "v" + minuscules(reference); }
std::string sourceBrocheCosimulee(const std::string& reference, const std::string& numero, char genre) { return "v" + minuscules(reference) + "_" + minuscules(numero) + genre; }

std::string vecteurTension(const NetlistSpice& n, const std::string& reseau) {
  const auto it = n.noeuds.find(reseau);
  if (it == n.noeuds.end() || it->second == "0") return {};
  return "v(" + it->second + ")";
}

namespace {
// « v(n_sortie) » : ngspice range le vecteur sous le nom du nœud seul
bool memeVecteur(const std::string& k, const std::string& nom) {
  const std::string a = minuscules(k), b = minuscules(nom);
  if (a == b) return true;
  if (b.size() > 3 && b.rfind("v(", 0) == 0 && b.back() == ')') return a == b.substr(2, b.size() - 3);
  return false;
}
}  // namespace

const std::vector<double>* ResultatSimulation::reel(const std::string& nom) const {
  for (const auto& [k, v] : reels)
    if (memeVecteur(k, nom)) return &v;
  return nullptr;
}
const std::vector<std::complex<double>>* ResultatSimulation::complexe(const std::string& nom) const {
  for (const auto& [k, v] : complexes)
    if (memeVecteur(k, nom)) return &v;
  return nullptr;
}

// ---------- Mesures ----------
double frequenceMesuree(const std::vector<double>& t, const std::vector<double>& v) {
  if (t.size() < 4 || v.size() != t.size()) return 0;
  const std::size_t debut = t.size() / 2;
  const auto [mn, mx] = std::minmax_element(v.begin() + static_cast<long>(debut), v.end());
  const double milieu = (*mn + *mx) / 2;
  if (*mx - *mn < 1e-9) return 0;
  std::vector<double> fronts;
  for (std::size_t i = debut + 1; i < t.size(); ++i)
    if (v[i - 1] < milieu && v[i] >= milieu) fronts.push_back(t[i - 1] + (milieu - v[i - 1]) / (v[i] - v[i - 1]) * (t[i] - t[i - 1]));
  if (fronts.size() < 2) return 0;
  return static_cast<double>(fronts.size() - 1) / (fronts.back() - fronts.front());
}

double frequenceCoupure(const std::vector<double>& f, const std::vector<std::complex<double>>& h) {
  if (f.size() < 2 || h.size() != f.size()) return 0;
  const double ref = std::abs(h.front()) / std::sqrt(2.0);
  for (std::size_t i = 1; i < f.size(); ++i)
    if (std::abs(h[i - 1]) >= ref && std::abs(h[i]) < ref) {
      // Interpolation en décibels sur le logarithme de la fréquence
      const double a = 20 * std::log10(std::abs(h[i - 1])), b = 20 * std::log10(std::abs(h[i])), cible = 20 * std::log10(ref);
      const double la = std::log10(f[i - 1]), lb = std::log10(f[i]);
      return std::pow(10.0, la + (cible - a) / (b - a) * (lb - la));
    }
  return 0;
}

double valeurA(const std::vector<double>& x, const std::vector<double>& y, double x0) {
  if (x.empty() || y.size() != x.size()) return 0;
  if (x0 <= x.front()) return y.front();
  if (x0 >= x.back()) return y.back();
  const auto it = std::upper_bound(x.begin(), x.end(), x0);
  const std::size_t i = static_cast<std::size_t>(it - x.begin());
  return y[i - 1] + (x0 - x[i - 1]) / (x[i] - x[i - 1]) * (y[i] - y[i - 1]);
}

std::vector<double> gainDb(const std::vector<std::complex<double>>& h) {
  std::vector<double> r;
  r.reserve(h.size());
  for (const auto& x : h) r.push_back(20 * std::log10(std::max(std::abs(x), 1e-30)));
  return r;
}

// ---------- Boîte à calculs ----------
CombinaisonE combinaisonE(double cible, int serie) {
  CombinaisonE meilleur;
  if (cible <= 0) return meilleur;
  std::vector<double> valeurs;
  for (int decade = -1; decade <= 7; ++decade)
    for (double m : serieE(serie)) valeurs.push_back(m * std::pow(10.0, decade));
  std::sort(valeurs.begin(), valeurs.end());
  valeurs.erase(std::unique(valeurs.begin(), valeurs.end()), valeurs.end());
  auto essayer = [&](double a, double b, bool par) {
    const double v = par ? a * b / (a + b) : a + b;
    const double e = std::abs(v - cible) / cible;
    if (meilleur.valeur == 0 || e < meilleur.ecart - 1e-12) meilleur = {a, b, par, v, e};
  };
  auto voisins = [&](double cherche, const std::function<void(double)>& f) {
    const auto it = std::lower_bound(valeurs.begin(), valeurs.end(), cherche);
    if (it != valeurs.end()) f(*it);
    if (it != valeurs.begin()) f(*std::prev(it));
  };
  for (double a : valeurs) {
    if (a <= cible) voisins(cible - a, [&](double b) { essayer(a, b, false); });
    if (a > cible) voisins(1 / (1 / cible - 1 / a), [&](double b) { essayer(a, b, true); });
  }
  if (meilleur.a < meilleur.b) std::swap(meilleur.a, meilleur.b);
  return meilleur;
}

// ---------- Sources en français ----------
std::string sourcePwl(const std::string& description, std::string* erreur) {
  // Valeur avec unité : « 5 V », « 500 mV », « 1 ms », « 10 µs »
  static const std::regex tension(R"(([-+]?\d+(?:[.,]\d+)?)\s*(mV|V))");
  static const std::regex duree(R"((\d+(?:[.,]\d+)?)\s*(ns|µs|us|ms|s)\b)");
  auto lire = [](const std::smatch& m) {
    std::string x = m[1].str();
    std::replace(x.begin(), x.end(), ',', '.');
    return std::stod(x);
  };
  auto facteur = [](const std::string& u) { return u == "ns" ? 1e-9 : (u == "µs" || u == "us") ? 1e-6 : u == "ms" ? 1e-3 : u == "mV" ? 1e-3 : 1.0; };
  // Morceaux séparés par « puis » ou une virgule
  std::vector<std::string> morceaux;
  std::string reste = description;
  for (;;) {
    std::size_t p = reste.find(", puis");
    std::size_t l = 6;
    if (p == std::string::npos) {
      p = reste.find(" puis ");
      l = 6;
    }
    if (p == std::string::npos) {
      p = reste.find(',');
      l = 1;
    }
    if (p == std::string::npos) break;
    morceaux.push_back(reste.substr(0, p));
    reste = reste.substr(p + l);
  }
  morceaux.push_back(reste);
  double t = 0, v = 0;
  std::vector<std::pair<double, double>> points;
  bool premier = true;
  for (const auto& m0 : morceaux) {
    const std::string m = minuscules(m0);
    std::smatch mt, md;
    const bool aT = std::regex_search(m0, mt, tension), aD = std::regex_search(m0, md, duree);
    if (m.find("rampe") != std::string::npos && aT && aD) {
      if (premier) points.push_back({t, v});
      t += lire(md) * facteur(md[2].str());
      v = lire(mt) * facteur(mt[2].str());
      points.push_back({t, v});
    } else if (m.find("pendant") != std::string::npos && aT && aD) {
      const double nv = lire(mt) * facteur(mt[2].str());
      if (premier || nv != v) {
        if (!premier) t += 1e-9;
        v = nv;
        points.push_back({t, v});
      }
      t += lire(md) * facteur(md[2].str());
      points.push_back({t, v});
    } else if ((m.find("saut") != std::string::npos || premier) && aT) {
      if (!premier) t += 1e-9;
      v = lire(mt) * facteur(mt[2].str());
      points.push_back({t, v});
    } else {
      if (erreur) *erreur = "morceau non compris : « " + m0 + " »";
      return {};
    }
    premier = false;
  }
  std::string r = "PWL(";
  for (std::size_t i = 0; i < points.size(); ++i) r += (i ? " " : "") + nb(points[i].first) + " " + nb(points[i].second);
  return r + ")";
}

}  // namespace prom::ecad

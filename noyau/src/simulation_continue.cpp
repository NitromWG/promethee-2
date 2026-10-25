// Prométhée : générateurs, instruments, retour visuel, façades et instruments Python (lot L14 de la 0.34). Voir
// simulation_continue.hpp ; la session continue elle-même est dans moteur_ngspice.cpp. Licence GPL-3.0-only.
#include "promethee/simulation_continue.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <numbers>
#include <set>
#include <sstream>

#include "promethee/modele.hpp"
#include "promethee/scripts_python.hpp"
#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
constexpr double DEUX_PI = 2 * std::numbers::pi;
std::string minuscules(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

// ======================================================================================================================
// Générateurs
// ======================================================================================================================
const char* nomForme(Generateur::Forme f) {
  switch (f) {
    case Generateur::Forme::Continu: return "continu";
    case Generateur::Forme::Sinus: return "sinus";
    case Generateur::Forme::Carre: return "carré";
    case Generateur::Forme::Triangle: return "triangle";
    case Generateur::Forme::DentDeScie: return "dent de scie";
    case Generateur::Forme::Impulsions: return "impulsions";
    case Generateur::Forme::Arbitraire: return "arbitraire";
    case Generateur::Forme::Mots: return "mots";
  }
  return "";
}

double valeurForme(const Generateur& g, double phase, std::size_t periode) {
  const double x = phase - std::floor(phase);
  const double r = std::clamp(g.rapport, 1e-6, 1 - 1e-6);
  switch (g.forme) {
    case Generateur::Forme::Continu: return 1;
    case Generateur::Forme::Sinus: return std::sin(DEUX_PI * x);
    case Generateur::Forme::Carre: return x < r ? 1 : -1;
    case Generateur::Forme::Triangle: return x < r ? -1 + 2 * x / r : 1 - 2 * (x - r) / (1 - r);
    case Generateur::Forme::DentDeScie: return -1 + 2 * x;
    case Generateur::Forme::Impulsions: return x < r ? 1 : 0;
    case Generateur::Forme::Arbitraire: {
      if (g.trace.empty()) return 0;
      std::vector<std::pair<double, double>> p = g.trace;
      std::sort(p.begin(), p.end());
      if (x <= p.front().first) {
        // Entre le dernier point de la période précédente et le premier
        const auto& [x0, y0] = p.back();
        const auto& [x1, y1] = p.front();
        const double d = x1 + 1 - x0;
        return d <= 0 ? y1 : y0 + (y1 - y0) * (x + 1 - x0) / d;
      }
      for (std::size_t i = 1; i < p.size(); ++i)
        if (x <= p[i].first) {
          const double d = p[i].first - p[i - 1].first;
          return d <= 0 ? p[i].second : p[i - 1].second + (p[i].second - p[i - 1].second) * (x - p[i - 1].first) / d;
        }
      const auto& [x0, y0] = p.back();
      const auto& [x1, y1] = p.front();
      const double d = x1 + 1 - x0;
      return d <= 0 ? y0 : y0 + (y1 - y0) * (x - x0) / d;
    }
    case Generateur::Forme::Mots: {
      if (g.mots.empty()) return 0;
      const std::uint8_t m = g.mots[periode % g.mots.size()];
      return (m >> std::clamp(g.bit, 0, 7)) & 1;
    }
  }
  return 0;
}

SourceGeneree::SourceGeneree(Generateur g) { m_troncons.push_back({0, 0, std::move(g)}); }

double SourceGeneree::phase(double t) const {
  const Troncon* tr = &m_troncons.front();
  for (auto it = m_troncons.rbegin(); it != m_troncons.rend(); ++it)
    if (it->debut <= t) {
      tr = &*it;
      break;
    }
  const Generateur& g = tr->g;
  double ph = tr->phase0 + g.frequence * (t - tr->debut);
  if (g.modulation == Generateur::Modulation::Frequence && g.frequenceModulante > 0)
    ph += g.excursion / (DEUX_PI * g.frequenceModulante) * (std::cos(DEUX_PI * g.frequenceModulante * tr->debut) - std::cos(DEUX_PI * g.frequenceModulante * t));
  return ph;
}

void SourceGeneree::regler(const Generateur& g, double t) {
  const double ph = phase(t);
  // Les tronçons d'après t (temps refusé puis repris) sont remplacés
  while (m_troncons.size() > 1 && m_troncons.back().debut > t) m_troncons.pop_back();
  m_troncons.push_back({t, ph, g});
  if (m_troncons.size() > 16) m_troncons.erase(m_troncons.begin(), m_troncons.begin() + 8);
}

double SourceGeneree::valeur(double t) const {
  const Troncon* tr = &m_troncons.front();
  for (auto it = m_troncons.rbegin(); it != m_troncons.rend(); ++it)
    if (it->debut <= t) {
      tr = &*it;
      break;
    }
  const Generateur& g = tr->g;
  const double ph = phase(t);
  const double v = valeurForme(g, ph, static_cast<std::size_t>(std::max(0.0, std::floor(ph))));
  double a = g.amplitude;
  if (g.modulation == Generateur::Modulation::Amplitude) a *= 1 + g.profondeur * std::sin(DEUX_PI * g.frequenceModulante * t);
  if (g.forme == Generateur::Forme::Mots) return g.decalage + g.niveauHaut * v;
  return g.decalage + a * v;
}

NetlistSpice netlistContinue(const ConceptionElec& c, const OptionsContinue& oc, OptionsSimulation o, const std::vector<std::string>& sondes) {
  AnalyseSpice a;
  a.type = AnalyseSpice::Type::Transitoire;
  a.pas = oc.pas;
  a.duree = oc.duree;
  o.continu = true;
  o.pasMax = oc.pas;
  o.sauvegardes = sondes;
  return netlistSpice(c, a, o);
}

// ======================================================================================================================
// Mesures
// ======================================================================================================================
double valeurA(const Releve& s, double t) {
  if (s.empty()) return std::numeric_limits<double>::quiet_NaN();
  if (t <= s.front().t) return s.front().v;
  if (t >= s.back().t) return s.back().v;
  const auto it = std::lower_bound(s.begin(), s.end(), t, [](const Echantillon& e, double x) { return e.t < x; });
  const Echantillon& b = *it;
  const Echantillon& a = *(it - 1);
  const double d = b.t - a.t;
  return d <= 0 ? b.v : a.v + (b.v - a.v) * (t - a.t) / d;
}

MesureSignal mesurerSignal(const Releve& s, double debut, double fin) {
  MesureSignal m;
  if (s.size() < 2) return m;
  const double t0 = std::max(debut, s.front().t), t1 = std::min(fin, s.back().t);
  if (t1 <= t0) return m;
  double i1 = 0, i2 = 0;
  m.min = 1e300;
  m.max = -1e300;
  auto segment = [&](double ta, double va, double tb, double vb) {
    const double h = tb - ta;
    if (h <= 0) return;
    i1 += h * (va + vb) / 2;
    i2 += h * (va * va + va * vb + vb * vb) / 3;  // exact pour un segment droit
    m.min = std::min({m.min, va, vb});
    m.max = std::max({m.max, va, vb});
  };
  double ta = t0, va = valeurA(s, t0);
  for (const auto& e : s) {
    if (e.t <= t0) continue;
    if (e.t >= t1) break;
    segment(ta, va, e.t, e.v);
    ta = e.t;
    va = e.v;
  }
  segment(ta, va, t1, valeurA(s, t1));
  m.duree = t1 - t0;
  m.moyenne = i1 / m.duree;
  m.efficace = std::sqrt(std::max(0.0, i2 / m.duree));
  m.efficaceAlternatif = std::sqrt(std::max(0.0, i2 / m.duree - m.moyenne * m.moyenne));
  m.creteCrete = m.max - m.min;
  m.valide = true;
  return m;
}

namespace {
// Instants des fronts (montants ou descendants) à travers le seuil, avec une hystérésis de 10 % de l'excursion
std::vector<double> fronts(const Releve& s, double seuil, bool montants, double debut) {
  std::vector<double> r;
  if (s.size() < 2) return r;
  double mn = 1e300, mx = -1e300;
  for (const auto& e : s)
    if (e.t >= debut) mn = std::min(mn, e.v), mx = std::max(mx, e.v);
  if (mx <= mn) return r;
  if (std::isnan(seuil)) seuil = (mn + mx) / 2;
  const double h = 0.05 * (mx - mn);
  bool arme = false;
  for (std::size_t i = 1; i < s.size(); ++i) {
    if (s[i].t < debut) continue;
    const double a = montants ? s[i - 1].v : -s[i - 1].v, b = montants ? s[i].v : -s[i].v, sl = montants ? seuil : -seuil;
    if (b < sl - h || a < sl - h) arme = true;
    if (arme && a < sl && b >= sl) {
      const double d = b - a;
      r.push_back(d <= 0 ? s[i].t : s[i - 1].t + (s[i].t - s[i - 1].t) * (sl - a) / d);
      arme = false;
    }
  }
  return r;
}
}  // namespace

double frequenceSignal(const Releve& s, double seuil, int* nombre, double debut) {
  const std::vector<double> f = fronts(s, seuil, true, debut);
  if (nombre) *nombre = static_cast<int>(f.size());
  if (f.size() < 2 || f.back() <= f.front()) return 0;
  return static_cast<double>(f.size() - 1) / (f.back() - f.front());
}

int compterFronts(const Releve& s, double seuil, bool montants, double debut) { return static_cast<int>(fronts(s, seuil, montants, debut).size()); }

double puissanceMoyenne(const Releve& u, const Releve& i, double debut, double fin) {
  if (u.size() < 2 || i.empty()) return 0;
  Releve p;
  for (const auto& e : u)
    if (e.t >= debut && e.t <= fin) p.push_back({e.t, e.v * valeurA(i, e.t)});
  return mesurerSignal(p).moyenne;
}

double frequencemetre(const Releve& s) { return frequenceSignal(s); }

std::vector<InstrumentPose> instrumentsDuSchema(const ConceptionElec& c) {
  std::vector<InstrumentPose> v;
  const Connectivite k = connectivite(c);
  for (const auto& x : c.composants) {
    if (x.symbole != "Instrument") continue;
    InstrumentPose p;
    p.composant = x.uuid;
    p.reference = x.reference;
    p.genre = x.valeur;
    if (const auto it = x.champs.find("Instrument.Script"); it != x.champs.end()) p.script = it->second;
    p.plus = k.nom(x.uuid, "1");
    p.moins = k.nom(x.uuid, "2");
    v.push_back(p);
  }
  return v;
}

// ======================================================================================================================
// Oscilloscope
// ======================================================================================================================
TraceOscillo traceOscilloscope(const std::vector<Releve>& voies, const ReglagesOscillo& r) {
  TraceOscillo t;
  t.voies.resize(voies.size());
  if (voies.empty()) return t;
  const double fenetre = std::max(1e-12, r.secondesParDivision * std::max(1, r.divisions));
  const double avant = fenetre * std::clamp(r.preDeclenchement, 0.0, 1.0), apres = fenetre - avant;
  const Releve& d = voies[static_cast<std::size_t>(std::clamp(r.voieDeclenchement, 0, static_cast<int>(voies.size()) - 1))];
  double instant = std::numeric_limits<double>::quiet_NaN();
  if (d.size() >= 2) {
    const double debut = d.front().t, fin = d.back().t;
    for (std::size_t i = d.size() - 1; i >= 1; --i) {
      const double a = d[i - 1].v, b = d[i].v;
      const bool croise = r.frontMontant ? (a < r.niveau && b >= r.niveau) : (a > r.niveau && b <= r.niveau);
      if (croise) {
        const double tc = b == a ? d[i].t : d[i - 1].t + (d[i].t - d[i - 1].t) * (r.niveau - a) / (b - a);
        if (tc + apres <= fin && tc - avant >= debut) {
          instant = tc;
          break;
        }
      }
      if (d[i].t < debut + avant) break;
    }
    if (std::isnan(instant) && r.mode == ReglagesOscillo::Mode::Auto) instant = fin - apres;
    else t.declenche = !std::isnan(instant);
  }
  if (std::isnan(instant)) return t;
  t.instant = instant;
  for (std::size_t k = 0; k < voies.size(); ++k) {
    const VoieOscillo v = k < r.voies.size() ? r.voies[k] : VoieOscillo{};
    Releve x;
    for (const auto& e : voies[k])
      if (e.t >= instant - avant && e.t <= instant + apres) x.push_back({e.t - instant, e.v});
    if (v.couplage == CouplageVoie::Masse)
      for (auto& e : x) e.v = 0;
    else if (v.couplage == CouplageVoie::Alternatif && x.size() >= 2) {
      const double m = mesurerSignal(x).moyenne;
      for (auto& e : x) e.v -= m;
    }
    t.voies[k] = std::move(x);
  }
  return t;
}

// ======================================================================================================================
// Sept segments
// ======================================================================================================================
namespace {
// Bits a (0) à g (6)
constexpr std::uint8_t SEGMENTS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
}  // namespace
std::uint8_t segmentsDuChiffre(int chiffre) { return chiffre >= 0 && chiffre <= 9 ? SEGMENTS[chiffre] : 0; }
int chiffreDesSegments(std::uint8_t s) {
  for (int i = 0; i < 10; ++i)
    if (SEGMENTS[i] == (s & 0x7F)) return i;
  return -1;
}
int valeurBinaire(const std::vector<double>& tensions, double seuil) {
  int v = 0;
  for (std::size_t i = 0; i < tensions.size(); ++i)
    if (tensions[i] > seuil) v |= 1 << i;
  return v;
}

// ======================================================================================================================
// Retour visuel
// ======================================================================================================================
namespace {
// Élément à deux bornes de la netlist : son vecteur de courant (positif de n+ vers n− à travers lui) et ses nœuds
struct ElementDeux {
  std::string instance, vecteur, plus, moins, reference;
};
std::vector<ElementDeux> elementsDeux(const NetlistSpice& n) {
  std::vector<ElementDeux> v;
  std::istringstream in(n.texte);
  bool sousCircuit = false;
  for (std::string l; std::getline(in, l);) {
    if (l.empty()) continue;
    const std::string m = minuscules(l);
    if (m.rfind(".subckt", 0) == 0) sousCircuit = true;
    if (m.rfind(".ends", 0) == 0) {
      sousCircuit = false;
      continue;
    }
    if (sousCircuit || !std::isalpha(static_cast<unsigned char>(m[0]))) continue;
    std::istringstream mots(m);
    std::string inst, a, b;
    mots >> inst >> a >> b;
    const auto it = n.instances.find(inst);
    if (it == n.instances.end() || a.empty() || b.empty()) continue;
    std::string vec;
    switch (inst[0]) {
      case 'r': vec = "@" + inst + "[i]"; break;
      case 'c': vec = "@" + inst + "[i]"; break;
      case 'l': vec = "@" + inst + "[i]"; break;
      case 'd': vec = "@" + inst + "[id]"; break;
      case 's': vec = "@" + inst + "[i]"; break;
      case 'v': vec = inst + "#branch"; break;
      default: continue;
    }
    v.push_back({inst, vec, a, b, it->second});
  }
  return v;
}
double lire(const std::map<std::string, double>& valeurs, const std::string& nom) {
  if (const auto it = valeurs.find(nom); it != valeurs.end()) return it->second;
  const std::string m = minuscules(nom);
  for (const auto& [k, v] : valeurs)
    if (minuscules(k) == m) return v;
  // Une tension « v(n_x) » est rangée sous « n_x »
  if (m.size() > 3 && m.rfind("v(", 0) == 0) return lire(valeurs, m.substr(2, m.size() - 3));
  return std::numeric_limits<double>::quiet_NaN();
}
// Limite tirée d'un paramètre de l'article (« 20 mA », « 0,25 W »), sinon celle donnée
double limiteArticle(const ConceptionElec& c, const ComposantElec& k, std::initializer_list<const char*> cles, FamilleValeur f, double defaut) {
  const auto it = c.articles.find(k.article);
  const std::map<std::string, std::string>* p = it != c.articles.end() ? &it->second.parametres : nullptr;
  std::vector<std::pair<std::string, std::string>> champs;
  if (p)
    for (const auto& [a, b] : *p) champs.emplace_back(minuscules(a), b);
  for (const auto& [a, b] : k.champs) champs.emplace_back(minuscules(a), b);
  for (const char* cle : cles)
    for (const auto& [a, b] : champs)
      if (a == cle) {
        std::string t = b;
        for (auto& ch : t)
          if (ch == ',') ch = '.';
        char* fin = nullptr;
        double v = std::strtod(t.c_str(), &fin);
        std::string u = fin ? std::string(fin) : std::string();
        while (!u.empty() && u.front() == ' ') u.erase(u.begin());
        if (!u.empty()) switch (u[0]) {
            case 'm': v *= 1e-3; break;
            case 'u':
            case '\xc2': v *= 1e-6; break;  // « µ » en UTF-8
            case 'k': v *= 1e3; break;
            default: break;
          }
        if (v > 0) return v;
      }
  (void)f;
  return defaut;
}
}  // namespace

std::vector<std::string> sondesRetourVisuel(const ConceptionElec&, const NetlistSpice& n) {
  std::vector<std::string> v;
  for (const auto& e : elementsDeux(n)) v.push_back(e.vecteur);
  return v;
}

std::array<int, 3> couleurTension(double v, double vmax) {
  if (!(vmax > 0) || std::isnan(v)) return {128, 128, 128};
  const double x = std::clamp(v / vmax, -1.0, 1.0);
  if (x >= 0) return {static_cast<int>(128 + 127 * x), static_cast<int>(128 - 96 * x), static_cast<int>(128 - 96 * x)};
  return {static_cast<int>(128 + 96 * x), static_cast<int>(128 + 32 * x), static_cast<int>(128 - 127 * x)};
}

RetourVisuel retourVisuel(const ConceptionElec& c, const NetlistSpice& n, const std::map<std::string, double>& valeurs) {
  RetourVisuel r;
  const Connectivite k = connectivite(c);
  // Tensions des réseaux
  for (const auto& [reseau, noeud] : n.noeuds) r.tensions[reseau] = noeud == "0" ? 0 : lire(valeurs, noeud);
  // Courant entrant dans chaque réseau par chaque broche : un élément tire I de son nœud n+ et le rend à n−
  std::map<std::pair<std::string, std::string>, double> injection;  // (composant, broche) → A entrant dans le réseau
  std::map<std::string, std::string> reseauDuNoeud;
  for (const auto& [reseau, noeud] : n.noeuds) reseauDuNoeud.emplace(noeud, reseau);
  std::map<std::string, std::vector<std::string>> elementsDe;  // composant → éléments (courants connus)
  for (const auto& e : elementsDeux(n)) {
    const double i = lire(valeurs, e.vecteur);
    if (std::isnan(i)) continue;
    const ComposantElec* x = c.parReference(e.reference);
    if (!x) continue;
    const DefSymbole* d = c.symbole(x->symbole);
    if (!d) continue;
    // Broches du composant sur les nœuds n+ et n−
    std::string bp, bm;
    for (const auto& b : d->broches) {
      const std::string res = k.nom(x->uuid, b.numero);
      const auto it = n.noeuds.find(res);
      if (it == n.noeuds.end()) continue;
      if (it->second == e.plus && bp.empty()) bp = b.numero;
      else if (it->second == e.moins && bm.empty()) bm = b.numero;
    }
    if (!bp.empty()) injection[{x->uuid, bp}] -= i;
    if (!bm.empty()) injection[{x->uuid, bm}] += i;
    elementsDe[x->uuid].push_back(e.instance);
    // Courant de la broche 1 vers la broche 2
    if (bp == "1" || bm == "2") r.courants[x->uuid] += i;
    else if (bp == "2" || bm == "1") r.courants[x->uuid] -= i;
  }
  // Fils : par feuille et par réseau, arbre des fils (les étiquettes d'un même réseau se rejoignent en un nœud
  // virtuel) ; le courant de chaque fil est la somme des injections de la branche qu'il porte
  for (const auto& f : c.feuilles) {
    std::map<std::pair<Nm, Nm>, int> indicePoint;
    std::vector<std::pair<Nm, Nm>> points;
    auto point = [&](Pt p) {
      const auto cle = std::make_pair(p.x, p.y);
      const auto [it, neuf] = indicePoint.emplace(cle, static_cast<int>(points.size()));
      if (neuf) points.push_back(cle);
      return it->second;
    };
    struct Arete {
      int a, b;
      std::string fil;  // vide : liaison virtuelle (étiquette)
    };
    std::vector<Arete> aretes;
    std::map<int, double> injectionPoint;
    std::map<int, std::string> reseauPoint;
    for (const auto& w : f.fils) {
      const auto it = k.parFil.find(w.uuid);
      if (it == k.parFil.end()) continue;
      const int a = point(w.a), b = point(w.b);
      aretes.push_back({a, b, w.uuid});
      reseauPoint[a] = reseauPoint[b] = k.reseaux[static_cast<std::size_t>(it->second)].nom;
    }
    std::map<std::string, int> moyeu;
    for (const auto& e : f.etiquettes) {
      const auto it = k.parEtiquette.find(e.uuid);
      if (it == k.parEtiquette.end()) continue;
      const std::string res = k.reseaux[static_cast<std::size_t>(it->second)].nom;
      auto [m, neuf] = moyeu.emplace(res, 0);
      if (neuf) {
        m->second = static_cast<int>(points.size());
        points.push_back({std::numeric_limits<Nm>::min(), static_cast<Nm>(moyeu.size())});
      }
      aretes.push_back({point(e.p), m->second, {}});
    }
    for (const auto& s : f.symboles) {
      const DefSymbole* d = c.symbole(s.symbole);
      if (!d || s.composant.empty()) continue;
      for (const auto& b : brochesPlacees(s, *d)) {
        const auto it = injection.find({s.composant, b.broche->numero});
        if (it == injection.end()) continue;
        if (const auto ip = indicePoint.find({b.p.x, b.p.y}); ip != indicePoint.end()) injectionPoint[ip->second] += it->second;
      }
    }
    // Arbre couvrant par parcours en largeur, puis cumul des injections des feuilles vers la racine
    const int np = static_cast<int>(points.size());
    std::vector<std::vector<std::pair<int, int>>> voisins(static_cast<std::size_t>(np));
    for (int i = 0; i < static_cast<int>(aretes.size()); ++i) {
      voisins[static_cast<std::size_t>(aretes[static_cast<std::size_t>(i)].a)].push_back({aretes[static_cast<std::size_t>(i)].b, i});
      voisins[static_cast<std::size_t>(aretes[static_cast<std::size_t>(i)].b)].push_back({aretes[static_cast<std::size_t>(i)].a, i});
    }
    std::vector<int> parent(static_cast<std::size_t>(np), -2), areteParent(static_cast<std::size_t>(np), -1), ordre;
    for (int racine = 0; racine < np; ++racine) {
      if (parent[static_cast<std::size_t>(racine)] != -2) continue;
      parent[static_cast<std::size_t>(racine)] = -1;
      std::deque<int> file{racine};
      while (!file.empty()) {
        const int u = file.front();
        file.pop_front();
        ordre.push_back(u);
        for (const auto& [w, ar] : voisins[static_cast<std::size_t>(u)])
          if (parent[static_cast<std::size_t>(w)] == -2) {
            parent[static_cast<std::size_t>(w)] = u;
            areteParent[static_cast<std::size_t>(w)] = ar;
            file.push_back(w);
          }
      }
    }
    std::vector<double> cumul(static_cast<std::size_t>(np), 0);
    for (const auto& [p, i] : injectionPoint) cumul[static_cast<std::size_t>(p)] += i;
    std::map<std::string, std::pair<double, int>> parFil;  // fil → (courant, sens)
    for (auto it = ordre.rbegin(); it != ordre.rend(); ++it) {
      const int u = *it;
      const int p = parent[static_cast<std::size_t>(u)];
      if (p < 0) continue;
      const double flux = cumul[static_cast<std::size_t>(u)];  // de u vers son parent
      cumul[static_cast<std::size_t>(p)] += flux;
      const Arete& ar = aretes[static_cast<std::size_t>(areteParent[static_cast<std::size_t>(u)])];
      if (ar.fil.empty()) continue;
      // Sens : de a vers b si le flux va de u vers p et que u est a
      const bool deAversB = (ar.a == u) == (flux > 0);
      parFil[ar.fil] = {std::abs(flux), std::abs(flux) < 1e-12 ? 0 : (deAversB ? 1 : -1)};
    }
    for (const auto& w : f.fils) {
      const auto it = parFil.find(w.uuid);
      CourantFil cf;
      cf.fil = w.uuid;
      if (it != parFil.end()) cf.courant = it->second.first, cf.sens = it->second.second;
      if (const auto ip = k.parFil.find(w.uuid); ip != k.parFil.end()) {
        const std::string res = k.reseaux[static_cast<std::size_t>(ip->second)].nom;
        if (const auto t = r.tensions.find(res); t != r.tensions.end()) cf.tension = t->second;
      }
      r.fils.push_back(cf);
    }
  }
  // Limites des composants
  auto tensionBroche = [&](const ComposantElec& x, const char* b) {
    const std::string res = k.nom(x.uuid, b);
    const auto it = r.tensions.find(res);
    return it == r.tensions.end() ? std::numeric_limits<double>::quiet_NaN() : it->second;
  };
  for (const auto& x : c.composants) {
    const auto ic = r.courants.find(x.uuid);
    const double i = ic == r.courants.end() ? std::numeric_limits<double>::quiet_NaN() : ic->second;
    auto fumer = [&](const std::string& grandeur, double v, double lim, const std::string& unite) {
      Fumee f;
      f.composant = x.uuid;
      f.reference = x.reference;
      f.grandeur = grandeur;
      f.valeur = v;
      f.limite = lim;
      f.message = x.reference + " : " + grandeur + " de " + ecrireSI(v, unite) + " pour " + ecrireSI(lim, unite) + " au plus";
      r.fumees.push_back(f);
    };
    if (x.symbole == "LED" && !std::isnan(i)) {
      // Broche 2 : anode ; le courant direct va de la broche 2 vers la broche 1
      const double direct = -i;
      const double lim = limiteArticle(c, x, {"courant max", "if max", "imax", "if", "courant"}, FamilleValeur::Aucune, 20e-3);
      if (direct > lim) fumer("courant", direct, lim, "A");
    } else if (x.symbole == "R" && !std::isnan(i)) {
      const ValeurComprise v = lireValeurComposant(x.valeur, FamilleValeur::Resistance);
      const double rv = v.etat == EtatValeur::Comprise ? v.nombre : 1000;
      const double defaut = x.empreinte.find("0402") != std::string::npos ? 0.063 : x.empreinte.find("0603") != std::string::npos ? 0.1 : x.empreinte.find("0805") != std::string::npos ? 0.125 : 0.25;
      const double lim = v.puissance ? *v.puissance : limiteArticle(c, x, {"puissance", "p max", "pmax", "p"}, FamilleValeur::Aucune, defaut);
      const double p = i * i * rv;
      if (p > lim) fumer("puissance", p, lim, "W");
    } else if (x.symbole == "C_Pol") {
      const double u = tensionBroche(x, "1") - tensionBroche(x, "2");
      if (!std::isnan(u) && u < -0.5) fumer("tension inverse", -u, 0.5, "V");
    } else if (x.symbole == "Pile" && !std::isnan(i)) {
      const double lim = limiteArticle(c, x, {"courant max", "imax", "courant"}, FamilleValeur::Aucune, 1.0);
      if (std::abs(i) > lim) fumer("court-circuit", std::abs(i), lim, "A");
    }
  }
  return r;
}

RetourVisuel retourVisuel(const ConceptionElec& c, const NetlistSpice& n, const ResultatSimulation& res, std::size_t indice) {
  std::map<std::string, double> v;
  for (const auto& [nom, x] : res.reels) {
    if (x.empty()) continue;
    v[nom] = x[std::min(indice, x.size() - 1)];
  }
  return retourVisuel(c, n, v);
}

// ======================================================================================================================
// Façades
// ======================================================================================================================
namespace {
std::string attribut(const std::string& balise, const std::string& nom) {
  for (const char q : {'"', '\''}) {
    const std::string cle = " " + nom + "=" + q;
    const auto p = balise.find(cle);
    if (p == std::string::npos) continue;
    const auto f = balise.find(q, p + cle.size());
    if (f != std::string::npos) return balise.substr(p + cle.size(), f - p - cle.size());
  }
  return {};
}
double nombre(const std::string& t) {
  char* fin = nullptr;
  const double v = std::strtod(t.c_str(), &fin);
  return fin == t.c_str() ? 0 : v;
}
}  // namespace

const ElementFacadeInstrument* FacadeInstrument::element(const std::string& genre, const std::string& nom) const {
  for (const auto& e : elements)
    if (e.genre == genre && e.nom == nom) return &e;
  return nullptr;
}

std::optional<FacadeInstrument> lireFacade(const std::string& svg, std::string* erreur) {
  const auto racine = svg.find("<svg");
  if (racine == std::string::npos) {
    if (erreur) *erreur = "pas de balise <svg>";
    return std::nullopt;
  }
  FacadeInstrument f;
  f.svg = svg;
  const std::string tete = svg.substr(racine, svg.find('>', racine) - racine);
  f.largeur = nombre(attribut(tete, "width"));
  f.hauteur = nombre(attribut(tete, "height"));
  std::size_t p = racine;
  while ((p = svg.find('<', p + 1)) != std::string::npos) {
    const auto fin = svg.find('>', p);
    if (fin == std::string::npos) break;
    const std::string b = svg.substr(p, fin - p);
    const std::string id = attribut(b, "id");
    const auto deux = id.find(':');
    if (deux != std::string::npos) {
      ElementFacadeInstrument e;
      e.id = id;
      e.genre = id.substr(0, deux);
      e.nom = id.substr(deux + 1);
      if (e.genre == "bouton" || e.genre == "calibre" || e.genre == "menu" || e.genre == "affichage") {
        if (b.rfind("<circle", 0) == 0) {
          const double r = nombre(attribut(b, "r"));
          e.x = nombre(attribut(b, "cx")) - r;
          e.y = nombre(attribut(b, "cy")) - r;
          e.largeur = e.hauteur = 2 * r;
        } else {
          e.x = nombre(attribut(b, "x"));
          e.y = nombre(attribut(b, "y"));
          e.largeur = nombre(attribut(b, "width"));
          e.hauteur = nombre(attribut(b, "height"));
        }
        const std::string choix = attribut(b, "data-choix");
        for (std::size_t a = 0; !choix.empty() && a <= choix.size();) {
          const auto z = choix.find('|', a);
          e.choix.push_back(choix.substr(a, z == std::string::npos ? std::string::npos : z - a));
          if (z == std::string::npos) break;
          a = z + 1;
        }
        f.elements.push_back(e);
      }
    }
    p = fin;
  }
  if (f.elements.empty()) {
    if (erreur) *erreur = "aucun élément nommé (bouton:, calibre:, menu:, affichage:)";
    return std::nullopt;
  }
  return f;
}

const FacadeInstrument& facadeGenerique(const std::string& instrument) {
  static const std::map<std::string, FacadeInstrument> facades = [] {
    std::map<std::string, FacadeInstrument> m;
    auto faire = [&](const std::string& nom, const std::string& corps) {
      const std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"420\" height=\"240\" viewBox=\"0 0 420 240\">"
                              "<rect x=\"0\" y=\"0\" width=\"420\" height=\"240\" rx=\"14\" fill=\"#2B2F36\"/>" +
                              corps + "</svg>";
      auto f = lireFacade(svg);
      f->nom = nom;
      m[nom] = *f;
    };
    faire("oscilloscope",
          "<rect id=\"affichage:ecran\" x=\"16\" y=\"16\" width=\"260\" height=\"180\" fill=\"#0B3D2E\"/>"
          "<circle id=\"calibre:voltsParDivision\" cx=\"318\" cy=\"46\" r=\"18\" fill=\"#B8BCC4\" data-choix=\"0.01|0.02|0.05|0.1|0.2|0.5|1|2|5|10\"/>"
          "<circle id=\"calibre:secondesParDivision\" cx=\"378\" cy=\"46\" r=\"18\" fill=\"#B8BCC4\" data-choix=\"1e-6|2e-6|5e-6|1e-5|2e-5|5e-5|1e-4|2e-4|5e-4|1e-3|2e-3|5e-3|1e-2\"/>"
          "<rect id=\"menu:couplage\" x=\"300\" y=\"90\" width=\"100\" height=\"24\" fill=\"#4A505A\" data-choix=\"continu|alternatif|masse\"/>"
          "<rect id=\"menu:declenchement\" x=\"300\" y=\"124\" width=\"100\" height=\"24\" fill=\"#4A505A\" data-choix=\"auto|normal|unique\"/>"
          "<rect id=\"bouton:xy\" x=\"300\" y=\"160\" width=\"44\" height=\"24\" fill=\"#5B4BF5\"/>"
          "<rect id=\"bouton:marche\" x=\"356\" y=\"160\" width=\"44\" height=\"24\" fill=\"#1E885A\"/>");
    faire("multimetre",
          "<rect id=\"affichage:mesure\" x=\"40\" y=\"20\" width=\"340\" height=\"70\" fill=\"#C8D8C0\"/>"
          "<circle id=\"calibre:mode\" cx=\"210\" cy=\"160\" r=\"46\" fill=\"#B8BCC4\" data-choix=\"V continu|V alternatif|V efficace vrai|A continu|A alternatif|Ω|Hz\"/>"
          "<rect id=\"bouton:maintien\" x=\"40\" y=\"200\" width=\"70\" height=\"26\" fill=\"#5B4BF5\"/>");
    faire("generateur",
          "<rect id=\"affichage:frequence\" x=\"20\" y=\"20\" width=\"240\" height=\"50\" fill=\"#C8D8C0\"/>"
          "<rect id=\"menu:forme\" x=\"20\" y=\"90\" width=\"120\" height=\"26\" fill=\"#4A505A\" data-choix=\"sinus|carré|triangle|dent de scie|impulsions|arbitraire\"/>"
          "<circle id=\"calibre:frequence\" cx=\"320\" cy=\"60\" r=\"32\" fill=\"#B8BCC4\" data-choix=\"10|20|50|100|200|500|1000|2000|5000|10000|20000|50000|100000\"/>"
          "<circle id=\"calibre:amplitude\" cx=\"320\" cy=\"160\" r=\"28\" fill=\"#B8BCC4\" data-choix=\"0.1|0.2|0.5|1|2|5|10\"/>"
          "<rect id=\"menu:modulation\" x=\"20\" y=\"130\" width=\"120\" height=\"26\" fill=\"#4A505A\" data-choix=\"aucune|amplitude|fréquence\"/>"
          "<rect id=\"bouton:sortie\" x=\"20\" y=\"190\" width=\"80\" height=\"28\" fill=\"#1E885A\"/>");
    return m;
  }();
  static const FacadeInstrument vide;
  const auto it = facades.find(instrument);
  return it == facades.end() ? vide : it->second;
}

bool ReglagesInstrument::cliquer(const FacadeInstrument& f, double x, double y) {
  for (const auto& e : f.elements) {
    if (x < e.x || y < e.y || x > e.x + e.largeur || y > e.y + e.hauteur) continue;
    std::string& v = valeurs[e.nom];
    if (e.genre == "bouton") v = v == "1" ? "0" : "1";
    else if ((e.genre == "calibre" || e.genre == "menu") && !e.choix.empty()) {
      const auto it = std::find(e.choix.begin(), e.choix.end(), v);
      v = it == e.choix.end() || it + 1 == e.choix.end() ? e.choix.front() : *(it + 1);
    } else
      continue;
    return true;
  }
  return false;
}

// ======================================================================================================================
// Instruments écrits en Python
// ======================================================================================================================
ResultatInstrument executerInstrument(const std::string& code, const std::map<std::string, Releve>& sondes, std::size_t bloc, double delai) {
  ResultatInstrument r;
  std::map<std::string, std::size_t> lus;  // sonde → échantillons déjà rendus
  OptionsScript o;
  o.delai = delai;
  o.nom = "instrument";
  o.hote = [&](const std::string& methode, const nlohmann::ordered_json& p) -> std::optional<nlohmann::ordered_json> {
    if (methode == "instrument.sondes") {
      nlohmann::ordered_json v = nlohmann::ordered_json::array();
      for (const auto& [n, s] : sondes) v.push_back(n);
      return v;
    }
    if (methode == "instrument.echantillons") {
      const std::string n = p.value("sonde", std::string());
      auto it = sondes.find(n);
      if (it == sondes.end())
        for (auto x = sondes.begin(); x != sondes.end(); ++x)
          if (minuscules(x->first) == minuscules(n)) it = x;
      nlohmann::ordered_json v = nlohmann::ordered_json::array();
      if (it == sondes.end()) return v;
      std::size_t& k = lus[it->first];
      for (std::size_t i = 0; i < std::max<std::size_t>(1, bloc) && k < it->second.size(); ++i, ++k) v.push_back({it->second[k].t, it->second[k].v});
      return v;
    }
    if (methode == "instrument.afficher") {
      const std::string n = p.value("nom", std::string("mesure"));
      const double v = p.contains("valeur") && p["valeur"].is_number() ? p["valeur"].get<double>() : 0;
      r.valeurs[n] = v;
      r.journal.emplace_back(n, v);
      return nlohmann::ordered_json(true);
    }
    if (methode == "instrument.facade") {
      r.facade = p.value("svg", std::string());
      return nlohmann::ordered_json(true);
    }
    return std::nullopt;
  };
  Projet vide = projetVide();
  const ResultatScript s = executerScript(vide, code, o);
  r.reussi = s.reussi;
  r.erreurs = s.erreurs;
  return r;
}

}  // namespace prom::ecad

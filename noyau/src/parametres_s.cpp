// Prométhée : paramètres S, Touchstone et tracés de l'analyse (lot L14 de la 0.34 ; 14.10). Voir parametres_s.hpp.
// Licence GPL-3.0-only.
#include "promethee/parametres_s.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>

namespace prom::ecad {

namespace {
std::string minuscules(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string nb(double v) {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o.precision(12);
  o << v;
  return o.str();
}
std::string avantAnalyse(const std::string& texte, const std::string& ajout, const std::string& carte) {
  const auto fin = texte.rfind("\n.end");
  if (fin == std::string::npos) return texte;
  const auto debut = texte.rfind('\n', fin - 1);
  if (debut == std::string::npos) return texte;
  return texte.substr(0, debut + 1) + ajout + carte + texte.substr(fin);
}
using M2 = std::array<Complexe, 4>;  // 11, 12, 21, 22
M2 produit(const M2& a, const M2& b) { return {a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3], a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3]}; }
M2 inverse(const M2& a) {
  const Complexe d = a[0] * a[3] - a[1] * a[2];
  return {a[3] / d, -a[1] / d, -a[2] / d, a[0] / d};
}
}  // namespace

std::optional<ParametresS> parametresS(const ConceptionElec& c, const PortsAnalyseur& p, double fDebut, double fFin, int ppd, std::string* erreur) {
  auto refuser = [&](const std::string& e) -> std::optional<ParametresS> {
    if (erreur) *erreur = e;
    return std::nullopt;
  };
  if (!ngspiceDisponible()) return refuser("ngspice n'est pas installé");
  AnalyseSpice a;
  a.type = AnalyseSpice::Type::Alternatif;
  a.fDebut = fDebut;
  a.fFin = fFin;
  a.pointsParDecade = ppd;
  const NetlistSpice n = netlistSpice(c, a);
  if (!n.valide()) return refuser(n.erreurs.front());
  auto noeud = [&](const std::string& r) -> std::string {
    const auto it = n.noeuds.find(r);
    return it == n.noeuds.end() ? std::string() : it->second;
  };
  const std::string p1 = noeud(p.port1), p2 = p.port2.empty() ? std::string() : noeud(p.port2), m = p.masse.empty() ? std::string("0") : noeud(p.masse);
  if (p1.empty() || (!p.port2.empty() && p2.empty()) || m.empty()) return refuser("port introuvable dans la netlist");
  const bool deux = !p2.empty();
  ParametresS s;
  s.ports = deux ? 2 : 1;
  s.z0 = p.z0;
  // Attaque d'un port par 2 V derrière Z0 (onde incidente de 1 V), l'autre chargé par Z0
  auto attaquer = [&](const std::string& actif, const std::string& charge, std::vector<Complexe>& va, std::vector<Complexe>& vc) -> bool {
    std::string ajout = "V_ps n_ps " + m + " dc 0 ac 2\nR_ps n_ps " + actif + " " + nb(p.z0) + "\n";
    if (!charge.empty()) ajout += "R_pc " + charge + " " + m + " " + nb(p.z0) + "\n";
    const ResultatSimulation r = simuler(avantAnalyse(n.texte, ajout, a.carte()));
    if (!r.reussi()) {
      if (erreur) *erreur = r.erreur;
      return false;
    }
    auto tension = [&](const std::string& nd) -> std::vector<Complexe> {
      const auto* x = r.complexe("v(" + nd + ")");
      if (!x) x = r.complexe(nd);
      std::vector<Complexe> v = x ? *x : std::vector<Complexe>{};
      if (m != "0") {
        const auto* y = r.complexe("v(" + m + ")");
        if (!y) y = r.complexe(m);
        for (std::size_t i = 0; y && i < v.size() && i < y->size(); ++i) v[i] -= (*y)[i];
      }
      return v;
    };
    va = tension(actif);
    if (!charge.empty()) vc = tension(charge);
    if (s.frequences.empty())
      if (const auto* f = r.reel("frequency")) s.frequences = *f;
    return !va.empty();
  };
  std::vector<Complexe> v11, v21, v12, v22;
  if (!attaquer(p1, p2, v11, v21)) return std::nullopt;
  if (deux && !attaquer(p2, p1, v22, v12)) return std::nullopt;
  for (std::size_t i = 0; i < s.frequences.size() && i < v11.size(); ++i) {
    std::array<Complexe, 4> x{v11[i] - 1.0, 0, 0, 0};
    if (deux) {
      x[1] = i < v12.size() ? v12[i] : 0;
      x[2] = i < v21.size() ? v21[i] : 0;
      x[3] = i < v22.size() ? v22[i] - 1.0 : 0;
    }
    s.s.push_back(x);
  }
  return s;
}

std::array<Complexe, 4> versZ(const std::array<Complexe, 4>& s, double z0) {
  const M2 i{1, 0, 0, 1};
  const M2 plus{i[0] + s[0], s[1], s[2], i[3] + s[3]}, moins{i[0] - s[0], -s[1], -s[2], i[3] - s[3]};
  M2 z = produit(plus, inverse(moins));
  for (auto& x : z) x *= z0;
  return z;
}
std::array<Complexe, 4> versY(const std::array<Complexe, 4>& s, double z0) {
  const M2 i{1, 0, 0, 1};
  const M2 plus{i[0] + s[0], s[1], s[2], i[3] + s[3]}, moins{i[0] - s[0], -s[1], -s[2], i[3] - s[3]};
  M2 y = produit(moins, inverse(plus));
  for (auto& x : y) x /= z0;
  return y;
}
std::array<Complexe, 4> versH(const std::array<Complexe, 4>& s, double z0) {
  const M2 z = versZ(s, z0);
  const Complexe d = z[0] * z[3] - z[1] * z[2];
  return {d / z[3], z[1] / z[3], -z[2] / z[3], Complexe(1) / z[3]};
}
double enDecibels(Complexe x) { return 20 * std::log10(std::max(1e-300, std::abs(x))); }
double phaseDegres(Complexe x) { return std::arg(x) * 180 / std::numbers::pi; }

std::string ecrireTouchstone(const ParametresS& p, const std::string& format, const std::string& unite) {
  const std::string f = minuscules(format), u = minuscules(unite);
  const double echelle = u == "ghz" ? 1e9 : u == "mhz" ? 1e6 : u == "khz" ? 1e3 : 1;
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o.precision(10);
  o << "! Écrit par Prométhée : paramètres S, " << p.ports << " port(s), Z0 = " << p.z0 << " Ω\n";
  o << "# " << (u == "ghz" ? "GHZ" : u == "mhz" ? "MHZ" : u == "khz" ? "KHZ" : "HZ") << " S " << (f == "ma" ? "MA" : f == "ri" ? "RI" : "DB") << " R " << p.z0 << "\n";
  auto paire = [&](Complexe x) {
    if (f == "ri") return nb(x.real()) + " " + nb(x.imag());
    if (f == "ma") return nb(std::abs(x)) + " " + nb(phaseDegres(x));
    return nb(enDecibels(x)) + " " + nb(phaseDegres(x));
  };
  for (std::size_t i = 0; i < p.frequences.size() && i < p.s.size(); ++i) {
    o << p.frequences[i] / echelle << " " << paire(p.s[i][0]);
    // Ordre de Touchstone pour deux ports : S11 S21 S12 S22
    if (p.ports == 2) o << " " << paire(p.s[i][2]) << " " << paire(p.s[i][1]) << " " << paire(p.s[i][3]);
    o << "\n";
  }
  return o.str();
}

std::optional<ParametresS> lireTouchstone(const std::string& texte, int ports, std::string* erreur) {
  auto refuser = [&](const std::string& e) -> std::optional<ParametresS> {
    if (erreur) *erreur = e;
    return std::nullopt;
  };
  if (ports != 1 && ports != 2) return refuser("seuls les fichiers à un ou deux ports sont lus");
  ParametresS p;
  p.ports = ports;
  double echelle = 1e9;  // GHz par défaut
  std::string format = "ma";
  std::vector<double> nombres;
  std::istringstream in(texte);
  bool option = false;
  for (std::string l; std::getline(in, l);) {
    if (const auto c = l.find('!'); c != std::string::npos) l = l.substr(0, c);
    if (l.find_first_not_of(" \t\r") == std::string::npos) continue;
    if (l[l.find_first_not_of(" \t")] == '#') {
      option = true;
      std::istringstream mots(minuscules(l.substr(l.find('#') + 1)));
      for (std::string m; mots >> m;) {
        if (m == "hz") echelle = 1;
        else if (m == "khz") echelle = 1e3;
        else if (m == "mhz") echelle = 1e6;
        else if (m == "ghz") echelle = 1e9;
        else if (m == "ma" || m == "db" || m == "ri") format = m;
        else if (m == "r") mots >> p.z0;
        else if (m != "s") return refuser("paramètres « " + m + " » : seuls les paramètres S sont lus");
      }
      continue;
    }
    std::istringstream mots(l);
    mots.imbue(std::locale::classic());
    for (double x; mots >> x;) nombres.push_back(x);
  }
  if (!option) return refuser("ligne d'options « # » absente");
  const std::size_t parLigne = ports == 2 ? 9 : 3;
  if (nombres.size() < parLigne || nombres.size() % parLigne) return refuser("nombre de valeurs incohérent");
  auto complexe = [&](double a, double b) {
    if (format == "ri") return Complexe(a, b);
    const double module = format == "db" ? std::pow(10.0, a / 20) : a;
    return std::polar(module, b * std::numbers::pi / 180);
  };
  for (std::size_t i = 0; i < nombres.size(); i += parLigne) {
    p.frequences.push_back(nombres[i] * echelle);
    std::array<Complexe, 4> s{complexe(nombres[i + 1], nombres[i + 2]), 0, 0, 0};
    if (ports == 2) {
      s[2] = complexe(nombres[i + 3], nombres[i + 4]);
      s[1] = complexe(nombres[i + 5], nombres[i + 6]);
      s[3] = complexe(nombres[i + 7], nombres[i + 8]);
    }
    p.s.push_back(s);
  }
  return p;
}

std::vector<std::pair<double, double>> pointsSmith(const std::vector<Complexe>& g) {
  std::vector<std::pair<double, double>> v;
  for (const auto& x : g) v.emplace_back(x.real(), x.imag());
  return v;
}
std::vector<std::pair<double, double>> pointsBlack(const std::vector<Complexe>& h) {
  std::vector<std::pair<double, double>> v;
  const auto ph = phaseDeroulee(h);
  for (std::size_t i = 0; i < h.size(); ++i) v.emplace_back(ph[i], enDecibels(h[i]));
  return v;
}
std::vector<double> phaseDeroulee(const std::vector<Complexe>& h) {
  std::vector<double> v;
  double avant = 0;
  for (std::size_t i = 0; i < h.size(); ++i) {
    double p = phaseDegres(h[i]);
    if (i) {
      while (p - avant > 180) p -= 360;
      while (p - avant < -180) p += 360;
    }
    v.push_back(p);
    avant = p;
  }
  return v;
}
std::vector<double> tempsDeGroupe(const std::vector<double>& f, const std::vector<Complexe>& h) {
  std::vector<double> v(f.size(), 0);
  const auto ph = phaseDeroulee(h);
  for (std::size_t i = 0; i < f.size() && i < ph.size(); ++i) {
    const std::size_t a = i == 0 ? 0 : i - 1, b = std::min(f.size() - 1, i + 1);
    if (b == a || f[b] == f[a]) continue;
    v[i] = -(ph[b] - ph[a]) * std::numbers::pi / 180 / (2 * std::numbers::pi * (f[b] - f[a]));
  }
  return v;
}
Histogramme histogramme(const std::vector<double>& v, int classes) {
  Histogramme h;
  if (v.empty() || classes < 1) return h;
  h.min = *std::min_element(v.begin(), v.end());
  h.max = *std::max_element(v.begin(), v.end());
  h.classes.assign(static_cast<std::size_t>(classes), 0);
  const double l = h.max > h.min ? (h.max - h.min) / classes : 1;
  for (const double x : v) ++h.classes[static_cast<std::size_t>(std::clamp(static_cast<int>((x - h.min) / l), 0, classes - 1))];
  return h;
}

DiagrammeOeil diagrammeOeil(const Releve& s, double T, double decalage, double seuil) {
  DiagrammeOeil d;
  if (s.size() < 2 || T <= 0) return d;
  double mn = 1e300, mx = -1e300;
  for (const auto& e : s) mn = std::min(mn, e.v), mx = std::max(mx, e.v);
  if (std::isnan(seuil)) seuil = (mn + mx) / 2;
  const double t0 = s.front().t + decalage;
  const int n = static_cast<int>((s.back().t - t0) / T) - 1;
  for (int k = 0; k < n; ++k) {
    Releve r;
    for (int i = 0; i <= 100; ++i) {
      const double t = t0 + k * T + 2 * T * i / 100;
      r.push_back({2 * T * i / 100, valeurA(s, t)});
    }
    d.traces.push_back(std::move(r));
  }
  // Ouverture : à chaque position du bit (milieu de la fenêtre de deux bits), plus bas des « 1 » moins plus haut des « 0 »
  double meilleure = 0;
  int largeur = 0, plusLarge = 0;
  for (int i = 25; i <= 75; ++i) {
    double haut = 1e300, bas = -1e300;
    for (const auto& r : d.traces) {
      const double v = r[static_cast<std::size_t>(i)].v;
      if (v >= seuil) haut = std::min(haut, v);
      else bas = std::max(bas, v);
    }
    const double o = haut < 1e300 && bas > -1e300 ? haut - bas : 0;
    meilleure = std::max(meilleure, o);
    largeur = o > 0.1 * (mx - mn) ? largeur + 1 : 0;
    plusLarge = std::max(plusLarge, largeur);
  }
  d.ouvertureVerticale = meilleure;
  d.ouvertureHorizontale = plusLarge / 50.0;
  return d;
}

}  // namespace prom::ecad

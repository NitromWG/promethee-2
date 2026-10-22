// Prométhée : simulation par passes et assistants de calcul (lot L14). Licence GPL-3.0-only.
#include "promethee/simulation_avancee.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>
#include <random>
#include <set>
#include <sstream>

#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
std::string minuscules(std::string s) {
  for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}
// Nombre SPICE : « 1000 », « 1e-07 », « 4.7k », « 2.2meg », « 10u »
std::optional<double> nombreSpice(const std::string& t0) {
  const std::string t = minuscules(t0);
  std::size_t fin = 0;
  double v = 0;
  try {
    v = std::stod(t, &fin);
  } catch (const std::exception&) {
    return std::nullopt;
  }
  const std::string s = t.substr(fin);
  if (s.rfind("meg", 0) == 0) return v * 1e6;
  if (s.empty()) return v;
  switch (s[0]) {
    case 't': return v * 1e12;
    case 'g': return v * 1e9;
    case 'k': return v * 1e3;
    case 'm': return v * 1e-3;
    case 'u': return v * 1e-6;
    case 'n': return v * 1e-9;
    case 'p': return v * 1e-12;
    case 'f': return v * 1e-15;
    default: return v;
  }
}
std::vector<std::string> mots(const std::string& ligne) {
  std::vector<std::string> r;
  std::istringstream s(ligne);
  std::string m;
  while (s >> m) r.push_back(m);
  return r;
}
std::string nombre(double v) {
  char s[40];
  std::snprintf(s, sizeof s, "%.9g", v);
  return s;
}
// Lancement d'une passe : la netlist modifiée, simulée, mesurée
bool passe(const std::string& netlist, const NetlistSpice& n, const MesureSimulation& m, const std::atomic<bool>* arret, double& mesure, std::string* erreur) {
  const ResultatSimulation r = simuler(netlist, arret);
  if (!r.reussi()) {
    if (erreur) *erreur = r.interrompu ? "interrompu" : r.erreur;
    return false;
  }
  mesure = m(r, n);
  return std::isfinite(mesure) && mesure != 0;
}
void statistiques(ResultatPasses& r) {
  if (r.mesures.empty()) return;
  r.min = *std::min_element(r.mesures.begin(), r.mesures.end());
  r.max = *std::max_element(r.mesures.begin(), r.mesures.end());
  double s = 0;
  for (double x : r.mesures) s += x;
  r.moyenne = s / static_cast<double>(r.mesures.size());
  double v = 0;
  for (double x : r.mesures) v += (x - r.moyenne) * (x - r.moyenne);
  r.ecartType = r.mesures.size() > 1 ? std::sqrt(v / static_cast<double>(r.mesures.size() - 1)) : 0;
}
}  // namespace

MesureSimulation mesureCoupure(const std::string& reseau) {
  return [reseau](const ResultatSimulation& r, const NetlistSpice& n) {
    const auto* f = r.reel("frequency");
    const auto* h = r.complexe(vecteurTension(n, reseau));
    if (!f || !h || f->size() < 2 || h->size() != f->size()) return 0.0;
    std::size_t pic = 0;
    for (std::size_t i = 1; i < h->size(); ++i)
      if (std::abs((*h)[i]) > std::abs((*h)[pic])) pic = i;
    if (std::abs(h->front()) >= 0.5 * std::abs((*h)[pic])) return frequenceCoupure(*f, *h);
    // Passe-haut : la référence est le gain de la bande passante (le plus haut, l'amplificateur réel retombe au-delà),
    // la coupure se cherche en descendant depuis lui
    const double ref = std::abs((*h)[pic]) / std::sqrt(2.0);
    for (std::size_t i = pic; i > 0; --i)
      if (std::abs((*h)[i]) >= ref && std::abs((*h)[i - 1]) < ref) {
        const double a = 20 * std::log10(std::abs((*h)[i - 1])), b = 20 * std::log10(std::abs((*h)[i])), cible = 20 * std::log10(ref);
        const double la = std::log10((*f)[i - 1]), lb = std::log10((*f)[i]);
        return std::pow(10.0, la + (cible - a) / (b - a) * (lb - la));
      }
    return 0.0;
  };
}
MesureSimulation mesureFrequence(const std::string& reseau) {
  return [reseau](const ResultatSimulation& r, const NetlistSpice& n) {
    const auto* t = r.reel("time");
    const auto* v = r.reel(vecteurTension(n, reseau));
    return t && v ? frequenceMesuree(*t, *v) : 0.0;
  };
}
MesureSimulation mesureTension(const std::string& reseau) {
  return [reseau](const ResultatSimulation& r, const NetlistSpice& n) {
    const auto* v = r.reel(vecteurTension(n, reseau));
    return v && !v->empty() ? v->front() : 0.0;
  };
}

std::vector<Dispersion> dispersions(const ConceptionElec& c, const NetlistSpice& n, const std::map<std::string, double>& tolerances) {
  std::vector<Dispersion> r;
  std::istringstream s(n.texte);
  std::string ligne;
  while (std::getline(s, ligne)) {
    const auto m = mots(ligne);
    if (m.size() < 4) continue;
    const char e = static_cast<char>(std::toupper(static_cast<unsigned char>(m[0][0])));
    if (e != 'R' && e != 'C' && e != 'L') continue;
    const std::string ref = m[0].substr(1);
    const ComposantElec* k = c.parReference(ref);
    const auto v = nombreSpice(m[3]);
    if (!k || !v || *v <= 0) continue;
    Dispersion d;
    d.reference = ref;
    d.element = e;
    d.nominale = *v;
    d.tolerance = e == 'R' ? 0.05 : 0.10;
    if (const auto it = tolerances.find(ref); it != tolerances.end()) d.tolerance = it->second;
    else if (const auto p = k->parametres.find("tolerance"); p != k->parametres.end()) {
      std::string t = p->second;
      std::replace(t.begin(), t.end(), ',', '.');
      if (const double x = std::atof(t.c_str()); x > 0) d.tolerance = x / 100;
    } else if (const ValeurComprise vc = lireValeurComposant(k->valeur, familleDe(c, *k)); vc.tolerance)
      d.tolerance = *vc.tolerance / 100;
    r.push_back(d);
  }
  return r;
}

std::string netlistAvecValeurs(const std::string& netlist, const std::map<std::string, double>& valeurs) {
  std::istringstream s(netlist);
  std::string ligne, sortie;
  while (std::getline(s, ligne)) {
    const auto m = mots(ligne);
    if (m.size() >= 4) {
      const char e = static_cast<char>(std::toupper(static_cast<unsigned char>(m[0][0])));
      if (e == 'R' || e == 'C' || e == 'L')
        if (const auto it = valeurs.find(m[0].substr(1)); it != valeurs.end()) {
          std::string l = m[0] + " " + m[1] + " " + m[2] + " " + nombre(it->second);
          for (std::size_t i = 4; i < m.size(); ++i) l += " " + m[i];
          sortie += l + "\n";
          continue;
        }
    }
    sortie += ligne + "\n";
  }
  return sortie;
}

ResultatPasses monteCarlo(const ConceptionElec& c, const AnalyseSpice& a, const MesureSimulation& m, int passes, unsigned graine, const std::map<std::string, double>& tolerances,
                          const std::atomic<bool>* arret) {
  ResultatPasses r;
  const NetlistSpice n = netlistSpice(c, a);
  if (!n.valide()) {
    r.erreur = n.erreurs.front();
    return r;
  }
  if (!passe(n.texte, n, m, arret, r.nominale, &r.erreur)) return r;
  const auto d = dispersions(c, n, tolerances);
  std::mt19937 alea(graine);
  std::uniform_real_distribution<double> u(-1, 1);
  for (int i = 0; i < passes; ++i) {
    if (arret && arret->load()) {
      r.interrompu = true;
      break;
    }
    std::map<std::string, double> valeurs;
    for (const auto& x : d) valeurs[x.reference] = x.nominale * (1 + x.tolerance * u(alea));
    double mesure = 0;
    if (passe(netlistAvecValeurs(n.texte, valeurs), n, m, arret, mesure, nullptr)) {
      r.mesures.push_back(mesure);
      r.tirages.push_back(valeurs);
    } else
      ++r.echecs;
  }
  statistiques(r);
  return r;
}

ResultatPasses pireCas(const ConceptionElec& c, const AnalyseSpice& a, const MesureSimulation& m, const std::map<std::string, double>& tolerances, const std::atomic<bool>* arret) {
  ResultatPasses r;
  const NetlistSpice n = netlistSpice(c, a);
  if (!n.valide()) {
    r.erreur = n.erreurs.front();
    return r;
  }
  if (!passe(n.texte, n, m, arret, r.nominale, &r.erreur)) return r;
  const auto d = dispersions(c, n, tolerances);
  // Sensibilité de chaque composant : +1 % sur sa valeur
  std::map<std::string, double> signe;
  for (const auto& x : d) {
    double mesure = 0;
    if (!passe(netlistAvecValeurs(n.texte, {{x.reference, x.nominale * 1.01}}), n, m, arret, mesure, nullptr)) {
      ++r.echecs;
      continue;
    }
    const double s = (mesure - r.nominale) / r.nominale / 0.01;
    r.sensibilites[x.reference] = s;
    signe[x.reference] = s >= 0 ? 1 : -1;
  }
  // Les deux coins : chaque composant à la borne qui pousse la mesure vers le haut, puis vers le bas
  for (double sens : {1.0, -1.0}) {
    std::map<std::string, double> valeurs;
    for (const auto& x : d) valeurs[x.reference] = x.nominale * (1 + sens * signe[x.reference] * x.tolerance);
    double mesure = 0;
    if (passe(netlistAvecValeurs(n.texte, valeurs), n, m, arret, mesure, nullptr)) {
      r.mesures.push_back(mesure);
      r.tirages.push_back(valeurs);
    } else
      ++r.echecs;
  }
  r.mesures.push_back(r.nominale);
  statistiques(r);
  return r;
}

std::vector<std::pair<double, double>> balayer(const ConceptionElec& c, const AnalyseSpice& a, const std::string& reference, const std::vector<double>& valeurs,
                                               const MesureSimulation& m, const std::atomic<bool>* arret) {
  std::vector<std::pair<double, double>> r;
  const NetlistSpice n = netlistSpice(c, a);
  if (!n.valide()) return r;
  for (double v : valeurs) {
    if (arret && arret->load()) break;
    double mesure = 0;
    if (passe(netlistAvecValeurs(n.texte, {{reference, v}}), n, m, arret, mesure, nullptr)) r.emplace_back(v, mesure);
  }
  return r;
}

// ---------- Assistants ----------
namespace {
double normaliser(double v, int serie) { return plusProcheDansSerie(v, serie); }
std::string ecrireR(double v) { return ecrireCompact(v, FamilleValeur::Resistance); }
std::string ecrireC(double v) { return ecrireCompact(v, FamilleValeur::Condensateur); }
// Fréquence à −3 dB d'un second ordre (passe-bas ou passe-haut) de pulsation propre f0 et de facteur de qualité q
double coupureSecondOrdre(double f0, double q, bool passeHaut) {
  const double b = 2 - 1 / (q * q);
  const double u = passeHaut ? (-b + std::sqrt(b * b + 4)) / 2 : (b + std::sqrt(b * b + 4)) / 2;
  return f0 * std::sqrt(u);
}
}  // namespace

Proposition assistantLed(double vAlim, double vf, double courant, int serie) {
  Proposition p;
  p.titre = "LED et sa résistance";
  const double r = (vAlim - vf) / courant, rn = normaliser(r, serie), i = (vAlim - vf) / rn, puissance = i * i * rn;
  p.elements.push_back({"R1", "R", ecrireR(rn), r, {{"1", "VCC"}, {"2", "LED_A"}}});
  p.elements.push_back({"D1", "LED", "LED", 0, {{"2", "LED_A"}, {"1", "GND"}}});
  p.resultats = {{"R", rn}, {"I", i}, {"P", puissance}};
  if (puissance > 0.1) p.notes.push_back("La résistance dissipe " + nombre(std::round(puissance * 1000)) + " mW : boîtier 1206 ou plus.");
  if (vAlim <= vf) p.notes.push_back("Tension d'alimentation sous la tension directe de la LED : elle ne s'allume pas.");
  return p;
}

Proposition assistantDiviseur(double vin, double vout, double rTotal, int serie) {
  Proposition p;
  p.titre = "Diviseur de tension";
  const double r2 = rTotal * vout / vin, r1 = rTotal - r2;
  // La paire de la série la plus juste : R1 parcourt les valeurs voisines, R2 suit au plus près du rapport
  double r1n = normaliser(r1, serie), r2n = normaliser(r2, serie), meilleur = std::abs(vin * r2n / (r1n + r2n) - vout);
  for (const int sens : {1, -1}) {
    double a = normaliser(r1, serie);
    for (int k = 0; k < 6; ++k) {
      a = voisineDansSerie(a, serie, sens);
      const double b = normaliser(a * vout / (vin - vout), serie);
      const double e = std::abs(vin * b / (a + b) - vout);
      if (e < meilleur) {
        meilleur = e;
        r1n = a;
        r2n = b;
      }
    }
  }
  p.elements.push_back({"R1", "R", ecrireR(r1n), r1, {{"1", "ENTREE"}, {"2", "SORTIE"}}});
  p.elements.push_back({"R2", "R", ecrireR(r2n), r2, {{"1", "SORTIE"}, {"2", "GND"}}});
  p.resultats = {{"Vout", vin * r2n / (r1n + r2n)}, {"I", vin / (r1n + r2n)}};
  return p;
}

Proposition assistantAstable555(double f, double rapport, double c, int serie) {
  Proposition p;
  p.titre = "Astable à NE555";
  rapport = std::clamp(rapport, 0.51, 0.95);
  const double cn = normaliser(c, 12);
  const double tHaut = rapport / f, tBas = (1 - rapport) / f;
  const double r2 = tBas / (std::numbers::ln2 * cn), r1 = std::max(1e3, tHaut / (std::numbers::ln2 * cn) - r2);
  const double r1n = normaliser(r1, serie), r2n = normaliser(r2, serie);
  p.elements.push_back({"U1", "NE555", "NE555", 0, {{"8", "VCC"}, {"4", "VCC"}, {"1", "GND"}, {"7", "DIS"}, {"6", "THR"}, {"2", "THR"}, {"3", "SORTIE"}, {"5", "CV"}}});
  p.elements.push_back({"R1", "R", ecrireR(r1n), r1, {{"1", "VCC"}, {"2", "DIS"}}});
  p.elements.push_back({"R2", "R", ecrireR(r2n), r2, {{"1", "DIS"}, {"2", "THR"}}});
  p.elements.push_back({"C1", "C", ecrireC(cn), c, {{"1", "THR"}, {"2", "GND"}}});
  p.elements.push_back({"C2", "C", "10n", 10e-9, {{"1", "CV"}, {"2", "GND"}}});
  p.resultats = {{"f", 1.0 / (std::numbers::ln2 * (r1n + 2 * r2n) * cn)}, {"rapport", (r1n + r2n) / (r1n + 2 * r2n)}};
  if (r1 < 1e3 + 1) p.notes.push_back("R1 relevée à 1 kΩ pour ne pas court-circuiter la décharge : le rapport cyclique monte.");
  return p;
}

Proposition assistantPasseBasRC(double fc, double c, int serie) {
  Proposition p;
  p.titre = "Filtre passe-bas RC";
  const double cn = normaliser(c, 12), r = 1 / (2 * std::numbers::pi * fc * cn), rn = normaliser(r, serie);
  p.elements.push_back({"R1", "R", ecrireR(rn), r, {{"1", "ENTREE"}, {"2", "SORTIE"}}});
  p.elements.push_back({"C1", "C", ecrireC(cn), c, {{"1", "SORTIE"}, {"2", "GND"}}});
  p.resultats = {{"fc", 1 / (2 * std::numbers::pi * rn * cn)}};
  return p;
}

Proposition assistantSallenKey(double fc, bool passeHaut, double c, int serie) {
  Proposition p;
  p.titre = passeHaut ? "Passe-haut de Butterworth (Sallen-Key, ordre 2)" : "Passe-bas de Butterworth (Sallen-Key, ordre 2)";
  const auto aop = ElementPropose{"U1", "AOP", "AOP", 0, {{"1", "SK_P"}, {"3", "SORTIE"}, {"4", "SORTIE"}, {"5", "VCC"}, {"2", "VEE"}}};
  if (!passeHaut) {
    // Gain unité, R1 = R2 : Q = ½·√(C1/C2) ; C1 (contre-réaction) ≈ 2·C2 pour Q ≈ 0,707
    const double c2 = normaliser(c, 12), c1 = normaliser(2 * c2, 12), q = 0.5 * std::sqrt(c1 / c2);
    const double f0 = fc / (coupureSecondOrdre(1, q, false));
    const double r = 1 / (2 * std::numbers::pi * f0 * std::sqrt(c1 * c2)), rn = normaliser(r, serie);
    p.elements.push_back({"R1", "R", ecrireR(rn), r, {{"1", "ENTREE"}, {"2", "SK_N"}}});
    p.elements.push_back({"R2", "R", ecrireR(rn), r, {{"1", "SK_N"}, {"2", "SK_P"}}});
    p.elements.push_back({"C1", "C", ecrireC(c1), 2 * c2, {{"1", "SK_N"}, {"2", "SORTIE"}}});
    p.elements.push_back({"C2", "C", ecrireC(c2), c2, {{"1", "SK_P"}, {"2", "GND"}}});
    p.elements.push_back(aop);
    const double f0n = 1 / (2 * std::numbers::pi * rn * std::sqrt(c1 * c2));
    p.resultats = {{"fc", coupureSecondOrdre(f0n, q, false)}, {"f0", f0n}, {"Q", q}};
  } else {
    // Gain unité, C1 = C2 : Q = ½·√(R2/R1) ; R2 (vers la masse) = 2·R1 (contre-réaction) pour Q = 0,707
    const double cn = normaliser(c, 12);
    const double r1 = 1 / (2 * std::numbers::pi * fc * cn * std::sqrt(2.0));
    const double r1n = normaliser(r1, serie), r2n = normaliser(2 * r1n, serie), q = 0.5 * std::sqrt(r2n / r1n);
    p.elements.push_back({"C1", "C", ecrireC(cn), cn, {{"1", "ENTREE"}, {"2", "SK_N"}}});
    p.elements.push_back({"C2", "C", ecrireC(cn), cn, {{"1", "SK_N"}, {"2", "SK_P"}}});
    p.elements.push_back({"R1", "R", ecrireR(r1n), r1, {{"1", "SK_N"}, {"2", "SORTIE"}}});
    p.elements.push_back({"R2", "R", ecrireR(r2n), 2 * r1, {{"1", "SK_P"}, {"2", "GND"}}});
    p.elements.push_back(aop);
    const double f0n = 1 / (2 * std::numbers::pi * cn * std::sqrt(r1n * r2n));
    p.resultats = {{"fc", coupureSecondOrdre(f0n, q, true)}, {"f0", f0n}, {"Q", q}};
  }
  p.notes.push_back("Alimenter l'amplificateur par VCC et VEE (symétrique) ou décaler l'entrée à mi-tension.");
  return p;
}

std::vector<std::string> poserProposition(ConceptionElec& c, const Proposition& p, std::size_t feuille, Pt origine) {
  std::vector<std::string> r;
  if (feuille >= c.feuilles.size()) return r;
  Nm x = origine.x;
  std::set<std::string> pris;
  for (const auto& k : c.composants) pris.insert(k.reference);
  std::map<std::string, std::string> renomme;  // repère proposé → repère posé
  for (const auto& e : p.elements) {
    const std::string s = poserSymbole(c, feuille, e.symbole, {x, origine.y});
    x += 25'400'000;
    if (s.empty()) continue;
    for (auto& sp : c.feuilles[feuille].symboles) {
      if (sp.uuid != s) continue;
      ComposantElec* k = c.composant(sp.composant);
      if (!k) break;
      // Repère libre : le proposé, sinon le suivant de même préfixe
      std::string prefixe, ref = e.reference;
      for (char ch : ref)
        if (std::isalpha(static_cast<unsigned char>(ch))) prefixe += ch;
      for (int n = 1; pris.count(ref); ++n) ref = prefixe + std::to_string(n);
      pris.insert(ref);
      k->reference = ref;
      k->valeur = e.valeur;
      renomme[e.reference] = ref;
      r.push_back(ref);
      const DefSymbole* d = c.symbole(sp.symbole);
      if (!d) break;
      for (const auto& b : brochesPlacees(sp, *d))
        for (const auto& [num, reseau] : e.liaisons)
          if (b.broche->numero == num) {
            const Pt dir = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
            const Pt bout{b.p.x - dir.x * 2'540'000, b.p.y - dir.y * 2'540'000};
            c.feuilles[feuille].fils.push_back({nouvelUuid(), b.p, bout});
            Etiquette et;
            et.uuid = nouvelUuid();
            et.texte = reseau;
            et.p = bout;
            et.type = TypeEtiquette::Globale;
            c.feuilles[feuille].etiquettes.push_back(et);
          }
      break;
    }
  }
  return r;
}

}  // namespace prom::ecad

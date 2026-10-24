// Prométhée : moteur de lignes de transmission (8.21). Voir lignes_transmission.hpp. Les formules sont celles des
// publications citées, sous la forme où les écrit le calculateur de Qucs et de KiCad (mêmes coefficients), pour que
// les deux outils donnent les mêmes valeurs. Licence GPL-3.0-only.
#include "promethee/lignes_transmission.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

#include "promethee/circuit.hpp"
#include "promethee/contraintes.hpp"

namespace prom::ecad {

namespace {
constexpr double PI = std::numbers::pi;
constexpr double ZF0 = 376.730313668;  // impédance du vide (Ω)
constexpr double C0 = 299792458.0;     // m/s
constexpr double MU0 = 12.566370614e-7;
constexpr double DB_PAR_NP = 20.0 / std::numbers::ln10;

double profondeurPeau(double f, double sigma) { return f > 0 && sigma > 0 ? 1.0 / std::sqrt(PI * f * MU0 * sigma) : 0.0; }
// Intégrale elliptique complète de première espèce, par la moyenne arithmético-géométrique
double ellipK(double k) {
  k = std::clamp(k, 0.0, 1.0 - 1e-15);
  double a = 1.0, b = std::sqrt(1.0 - k * k);
  for (int i = 0; i < 40 && std::abs(a - b) > 1e-15 * a; ++i) {
    const double m = (a + b) / 2;
    b = std::sqrt(a * b);
    a = m;
  }
  return PI / (2 * a);
}

// ---------- Microruban (Hammerstad et Jensen 1980 ; dispersion : Kirschning et Jansen 1982) ----------
struct Microruban {
  double er, h, t, w, couvercle;
  double z0Statique = 0, erEff0 = 0, z0Homogene1 = 0;

  static double z0Homogene(double u) {
    const double f = 6.0 + (2.0 * PI - 6.0) * std::exp(-std::pow(30.666 / u, 0.7528));
    return ZF0 / (2.0 * PI) * std::log(f / u + std::sqrt(1.0 + 4.0 / (u * u)));
  }
  static double remplissage(double u, double er) {
    const double u2 = u * u, u3 = u2 * u, u4 = u3 * u;
    const double a = 1.0 + std::log((u4 + u2 / 2704) / (u4 + 0.432)) / 49.0 + std::log(1.0 + u3 / 5929.741) / 18.7;
    const double b = 0.564 * std::pow((er - 0.9) / (er + 3.0), 0.053);
    return std::pow(1.0 + 10.0 / u, -a * b);
  }
  static double qCouvercle(double h2h) { return std::tanh(1.043 + 0.121 * h2h - 1.164 / h2h); }
  static double qEpaisseur(double u, double th) { return (2.0 * std::log(2.0) / PI) * (th / std::sqrt(u)); }
  static double duEpaisseur(double u, double th, double er) {
    if (th <= 0) return 0;
    const double d = (th / PI) * std::log(1.0 + (4.0 * std::numbers::e) * std::pow(std::tanh(std::sqrt(6.517 * u)), 2.0) / th);
    return 0.5 * d * (1.0 + 1.0 / std::cosh(std::sqrt(er - 1.0)));
  }
  static double dispersionEr(double u, double er, double fn) {
    const double p1 = 0.27488 + u * (0.6315 + 0.525 / std::pow(1.0 + 0.0157 * fn, 20.0)) - 0.065683 * std::exp(-8.7513 * u);
    const double p2 = 0.33622 * (1.0 - std::exp(-0.03442 * er));
    const double p3 = 0.0363 * std::exp(-4.6 * u) * (1.0 - std::exp(-std::pow(fn / 38.7, 4.97)));
    const double p4 = 1.0 + 2.751 * (1.0 - std::exp(-std::pow(er / 15.916, 8.0)));
    return p1 * p2 * std::pow((p3 * p4 + 0.1844) * fn, 1.5763);
  }
  static double dispersionZ0(double u, double er, double e0, double ef, double fn) {
    const double r1 = 0.03891 * std::pow(er, 1.4), r2 = 0.267 * std::pow(u, 7.0), r3 = 4.766 * std::exp(-3.228 * std::pow(u, 0.641));
    const double r4 = 0.016 + std::pow(0.0514 * er, 4.524), r5 = std::pow(fn / 28.843, 12.0), r6 = 22.2 * std::pow(u, 1.92);
    const double r7 = 1.206 - 0.3144 * std::exp(-r1) * (1.0 - std::exp(-r2));
    const double r8 = 1.0 + 1.275 * (1.0 - std::exp(-0.004625 * r3 * std::pow(er, 1.674) * std::pow(fn / 18.365, 2.745)));
    double x = std::pow(er - 1.0, 6.0);
    const double r9 = 5.086 * r4 * (r5 / (0.3838 + 0.386 * r4)) * (std::exp(-r6) / (1.0 + 1.2992 * r5)) * (x / (1.0 + 10.0 * x));
    const double r10 = 0.00044 * std::pow(er, 2.136) + 0.0184;
    x = std::pow(fn / 19.47, 6.0);
    const double r11 = x / (1.0 + 0.0962 * x), r12 = 1.0 / (1.0 + 0.00245 * u * u);
    const double r13 = 0.9408 * std::pow(ef, r8) - 0.9603, r14 = (0.9408 - r9) * std::pow(e0, r8) - 0.9603;
    const double r15 = 0.707 * r10 * std::pow(fn / 12.3, 1.097), r16 = 1.0 + 0.0503 * er * er * r11 * (1.0 - std::exp(-std::pow(u / 15.0, 6.0)));
    const double r17 = r7 * (1.0 - 1.1241 * (r12 / r16) * std::exp(-0.026 * std::pow(fn, 1.15656) - r15));
    return std::pow(r13 / r14, r17);
  }
  void statique() {
    const double h2h = (couvercle > 0 ? couvercle : 1e20) / h, th = t / h;
    double u = w / h;
    z0Homogene1 = z0Homogene(u + duEpaisseur(u, th, 1.0));
    u += duEpaisseur(u, th, er);
    const double z0hr = z0Homogene(u);
    const double q = (remplissage(u, er) - qEpaisseur(u, th)) * qCouvercle(h2h);
    const double eefT = 0.5 * (er + 1.0) + 0.5 * q * (er - 1.0);
    erEff0 = eefT * std::pow(z0Homogene1 / z0hr, 2.0);
    z0Statique = z0hr / std::sqrt(eefT);
  }
  // Z0 et εeff à la fréquence f (Hz)
  std::pair<double, double> aFrequence(double f) const {
    const double u = w / h, fn = f * h / 1e6;  // GHz·mm
    const double ef = er - (er - erEff0) / (1.0 + dispersionEr(u, er, fn));
    return {z0Statique * dispersionZ0(u, er, erEff0, ef, fn), ef};
  }
};

// ---------- Ruban décentré (Wadell) : deux demi-lignes en parallèle ----------
double demiRuban(double er, double t, double w, double hauteur, double f, double sigma, double& ac) {
  const double hmt = hauteur - t;
  ac = std::sqrt(f / sigma / 17.2);
  double z;
  if (w / hmt >= 0.35) {
    const double zl = w + (2.0 * hauteur * std::log((2.0 * hauteur - t) / hmt) - t * std::log(hauteur * hauteur / hmt / hmt - 1.0)) / PI;
    z = ZF0 * hmt / std::sqrt(er) / 4.0 / zl;
    ac *= 2.02e-6 * er * z / hmt;
    ac *= 1.0 + 2.0 * w / hmt + (hauteur + t) / hmt / PI * std::log(2.0 * hauteur / t - 1.0);
  } else {
    const bool epais = t / w > 1.0;
    const double tdw = epais ? w / t : t / w;
    double de = 1.0 + tdw / PI * (1.0 + std::log(4.0 * PI / tdw)) + 0.236 * std::pow(tdw, 1.65);
    de *= epais ? t / 2.0 : w / 2.0;
    z = ZF0 / 2.0 / PI / std::sqrt(er) * std::log(4.0 * hauteur / PI / de);
    ac *= 0.01141 / z / de;
    ac *= de / hauteur + 0.5 + tdw / 2.0 / PI + 0.5 / PI * std::log(4.0 * PI / tdw) + 0.1947 * std::pow(tdw, 0.65) - 0.0767 * std::pow(tdw, 1.65);
  }
  return z;
}
double ruban(double er, double h, double a, double t, double w, double f, double sigma, double& pertesNp) {
  if (a <= 0) a = (h - t) / 2;
  double ac1 = 0, ac2 = 0;
  const double z = 2.0 / (1.0 / demiRuban(er, t, w, 2.0 * a + t, f, sigma, ac1) + 1.0 / demiRuban(er, t, w, 2.0 * (h - a) - t, f, sigma, ac2));
  pertesNp = ac1 + ac2;
  return z;
}

// ---------- Paire couplée en microruban (Kirschning et Jansen 1984) ----------
struct PaireMicroruban {
  double er, h, t, w, s, couvercle;
  double z0Pair0 = 0, z0Impair0 = 0, erPair0 = 0, erImpair0 = 0;
  double erPairF = 0, erImpairF = 0, zPairF = 0, zImpairF = 0;
  void calculer(double f) {
    const double u = w / h, g = s / h, th = t / h, h2h = (couvercle > 0 ? couvercle : 1e20) / h;
    double due = 0, duo = 0;
    if (th > 0) {
      const double du = (1.25 * th / PI) * (1.0 + std::log((2.0 + (4.0 * PI * u - 2.0) / (1.0 + std::exp(-100.0 * (u - 1.0 / (2.0 * PI))))) / th));
      const double dt = th / (g * er);
      due = du * (1.0 - 0.5 * std::exp(-0.69 * du / dt));
      duo = due + dt;
    }
    const double ute = u + due, uto = u + duo;
    // Ligne seule, sans épaisseur ni couvercle
    Microruban seule{er, h, 0.0, w, 1e12};
    seule.statique();
    const auto [z0sF, esF] = seule.aFrequence(f);
    const double es0 = seule.erEff0, z0s0 = seule.z0Statique;
    // Permittivités effectives statiques
    {
      const double v = ute * (20.0 + g * g) / (10.0 + g * g) + g * std::exp(-g), v3 = v * v * v, v4 = v3 * v;
      const double ae = 1.0 + std::log((v4 + v * v / 2704.0) / (v4 + 0.432)) / 49.0 + std::log(1.0 + v3 / 5929.741) / 18.7;
      const double be = 0.564 * std::pow((er - 0.9) / (er + 3.0), 0.053);
      const double qc = h2h <= 39 ? std::tanh(1.626 + 0.107 * h2h - 1.733 / std::sqrt(h2h)) : 1.0;
      const double q = (std::pow(1.0 + 10.0 / v, -ae * be) - Microruban::qEpaisseur(ute, th)) * qc;
      erPair0 = 0.5 * (er + 1.0) + 0.5 * (er - 1.0) * q;
    }
    {
      const double bo = 0.747 * er / (0.15 + er), co = bo - (bo - 0.207) * std::exp(-0.414 * uto), dO = 0.593 + 0.694 * std::exp(-0.562 * uto);
      const double qc = h2h <= 7 ? std::tanh(9.575 / (7.0 - h2h) - 2.965 + 1.68 * h2h - 0.311 * h2h * h2h) : 1.0;
      const double q = (std::exp(-co * std::pow(g, dO)) - Microruban::qEpaisseur(uto, th)) * qc;
      const double ao = 0.7287 * (es0 - 0.5 * (er + 1.0)) * (1.0 - std::exp(-0.179 * uto));
      erImpair0 = (0.5 * (er + 1.0) + ao - es0) * q + es0;
    }
    // Impédances statiques des deux modes
    const double q1 = 0.8695 * std::pow(ute, 0.194), q2 = 1.0 + 0.7519 * g + 0.189 * std::pow(g, 2.31);
    const double q3 = 0.1975 + std::pow(16.6 + std::pow(8.4 / g, 6.0), -0.387) + std::log(std::pow(g, 10.0) / (1.0 + std::pow(g / 3.4, 10.0))) / 241.0;
    const double q4 = 2.0 * q1 / (q2 * (std::exp(-g) * std::pow(ute, q3) + (2.0 - std::exp(-g)) * std::pow(ute, -q3)));
    z0Pair0 = z0s0 * std::sqrt(es0 / erPair0) / (1.0 - std::sqrt(es0) * q4 * z0s0 / ZF0);
    {
      const double A = -4.351 / std::pow(1.0 + h2h, 1.842), B = 6.639 / std::pow(1.0 + h2h, 1.861), C = -2.291 / std::pow(1.0 + h2h, 1.90);
      const double fe = 1.0 - std::atanh(A + (B + C * ute) * ute);
      const double x = std::pow(10.0, 0.103 * g - 0.159), y = std::pow(10.0, 0.0492 * g - 0.073);
      const double D = 0.747 / std::sin(0.5 * PI * x), E = 0.725 * std::sin(0.5 * PI * y), F = std::pow(10.0, 0.11 - 0.0947 * g);
      const double ge = 270.0 * (1.0 - std::tanh(D + E * std::sqrt(1.0 + h2h) - F / (1.0 + h2h)));
      z0Pair0 -= fe * ge / std::sqrt(erPair0);
    }
    const double q5 = 1.794 + 1.14 * std::log(1.0 + 0.638 / (g + 0.517 * std::pow(g, 2.43)));
    const double q6 = 0.2305 + std::log(std::pow(g, 10.0) / (1.0 + std::pow(g / 5.8, 10.0))) / 281.3 + std::log(1.0 + 0.598 * std::pow(g, 1.154)) / 5.1;
    const double q7 = (10.0 + 190.0 * g * g) / (1.0 + 82.3 * g * g * g), q8 = std::exp(-6.5 - 0.95 * std::log(g) - std::pow(g / 0.15, 5.0));
    const double q9 = std::log(q7) * (q8 + 1.0 / 16.5), q10 = (q2 * q4 - q5 * std::exp(std::log(uto) * q6 * std::pow(uto, -q9))) / q2;
    z0Impair0 = z0s0 * std::sqrt(es0 / erImpair0) / (1.0 - std::sqrt(es0) * q10 * z0s0 / ZF0);
    {
      const double J = std::tanh(std::pow(1.0 + h2h, 1.585) / 6.0), fo = std::pow(uto, J), G = 2.178 - 0.796 * g;
      const double K = g > 0.858 ? std::log10(20.492 * std::pow(g, 0.174)) : 1.30, L = g > 0.873 ? 2.51 * std::pow(g, -0.462) : 2.674;
      const double go = 270.0 * (1.0 - std::tanh(G + K * std::sqrt(1.0 + h2h) - L / (1.0 + h2h)));
      z0Impair0 -= fo * go / std::sqrt(erImpair0);
    }
    // Dispersion des permittivités
    const double fn = f * h / 1e6;
    const double p1 = 0.27488 + (0.6315 + 0.525 / std::pow(1.0 + 0.0157 * fn, 20.0)) * u - 0.065683 * std::exp(-8.7513 * u);
    const double p2 = 0.33622 * (1.0 - std::exp(-0.03442 * er)), p3 = 0.0363 * std::exp(-4.6 * u) * (1.0 - std::exp(-std::pow(fn / 38.7, 4.97)));
    const double p4 = 1.0 + 2.751 * (1.0 - std::exp(-std::pow(er / 15.916, 8.0))), p5 = 0.334 * std::exp(-3.3 * std::pow(er / 15.0, 3.0)) + 0.746;
    const double p6 = p5 * std::exp(-std::pow(fn / 18.0, 0.368)), p7 = 1.0 + 4.069 * p6 * std::pow(g, 0.479) * std::exp(-1.347 * std::pow(g, 0.595) - 0.17 * std::pow(g, 2.5));
    erPairF = er - (er - erPair0) / (1.0 + p1 * p2 * std::pow((p3 * p4 + 0.1844 * p7) * fn, 1.5763));
    const double p8 = 0.7168 * (1.0 + 1.076 / (1.0 + 0.0576 * (er - 1.0)));
    const double p9 = p8 - 0.7913 * (1.0 - std::exp(-std::pow(fn / 20.0, 1.424))) * std::atan(2.481 * std::pow(er / 8.0, 0.946));
    const double p10 = 0.242 * std::pow(er - 1.0, 0.55), p11 = 0.6366 * (std::exp(-0.3401 * fn) - 1.0) * std::atan(1.263 * std::pow(u / 3.0, 1.629));
    const double p12 = p9 + (1.0 - p9) / (1.0 + 1.183 * std::pow(u, 1.376)), p13 = 1.695 * p10 / (0.414 + 1.605 * p10);
    const double p14 = 0.8928 + 0.1072 * (1.0 - std::exp(-0.42 * std::pow(fn / 20.0, 3.215)));
    const double p15 = std::abs(1.0 - 0.8928 * (1.0 + p11) * p12 * std::exp(-p13 * std::pow(g, 1.092)) / p14);
    erImpairF = er - (er - erImpair0) / (1.0 + p1 * p2 * std::pow((p3 * p4 + 0.1844) * fn * p15, 1.5763));
    // Dispersion des impédances
    const double q11 = 0.893 * (1.0 - 0.3 / (1.0 + 0.7 * (er - 1.0)));
    const double q12 = 2.121 * (std::pow(fn / 20.0, 4.91) / (1.0 + q11 * std::pow(fn / 20.0, 4.91))) * std::exp(-2.87 * g) * std::pow(g, 0.902);
    const double q13 = 1.0 + 0.038 * std::pow(er / 8.0, 5.1), q14 = 1.0 + 1.203 * std::pow(er / 15.0, 4.0) / (1.0 + std::pow(er / 15.0, 4.0));
    const double q15 = 1.887 * std::exp(-1.5 * std::pow(g, 0.84)) * std::pow(g, q14) / (1.0 + 0.41 * std::pow(fn / 15.0, 3.0) * std::pow(u, 2.0 / q13) / (0.125 + std::pow(u, 1.626 / q13)));
    const double q16 = (1.0 + 9.0 / (1.0 + 0.403 * std::pow(er - 1.0, 2))) * q15;
    const double q17 = 0.394 * (1.0 - std::exp(-1.47 * std::pow(u / 7.0, 0.672))) * (1.0 - std::exp(-4.25 * std::pow(fn / 20.0, 1.87)));
    const double q18 = 0.61 * (1.0 - std::exp(-2.13 * std::pow(u / 8.0, 1.593))) / (1.0 + 6.544 * std::pow(g, 4.17));
    const double q19 = 0.21 * g * g * g * g / ((1.0 + 0.18 * std::pow(g, 4.9)) * (1.0 + 0.1 * u * u) * (1.0 + std::pow(fn / 24.0, 3.0)));
    const double q20 = (0.09 + 1.0 / (1.0 + 0.1 * std::pow(er - 1, 2.7))) * q19;
    const double q21 = std::abs(1.0 - 42.54 * std::pow(g, 0.133) * std::exp(-0.812 * g) * std::pow(u, 2.5) / (1.0 + 0.033 * std::pow(u, 2.5)));
    const double re = std::pow(fn / 28.843, 12), qe = 0.016 + std::pow(0.0514 * er * q21, 4.524), pe = 4.766 * std::exp(-3.228 * std::pow(u, 0.641));
    const double de = 5.086 * qe * (re / (0.3838 + 0.386 * qe)) * (std::exp(-22.2 * std::pow(u, 1.92)) / (1.0 + 1.2992 * re)) * (std::pow(er - 1.0, 6.0) / (1.0 + 10 * std::pow(er - 1.0, 6.0)));
    const double ce = 1.0 + 1.275 * (1.0 - std::exp(-0.004625 * pe * std::pow(er, 1.674) * std::pow(fn / 18.365, 2.745))) - q12 + q16 - q17 + q18 + q20;
    const double r1 = 0.03891 * std::pow(er, 1.4), r2 = 0.267 * std::pow(u, 7.0), r7 = 1.206 - 0.3144 * std::exp(-r1) * (1.0 - std::exp(-r2));
    const double r10 = 0.00044 * std::pow(er, 2.136) + 0.0184, x = std::pow(fn / 19.47, 6.0), r11 = x / (1.0 + 0.0962 * x);
    const double r12 = 1.0 / (1.0 + 0.00245 * u * u), r15 = 0.707 * r10 * std::pow(fn / 12.3, 1.097);
    const double r16 = 1.0 + 0.0503 * er * er * r11 * (1.0 - std::exp(-std::pow(u / 15.0, 6.0)));
    const double q0 = r7 * (1.0 - 1.1241 * (r12 / r16) * std::exp(-0.026 * std::pow(fn, 1.15656) - r15));
    zPairF = z0Pair0 * std::pow(0.9408 * std::pow(esF, ce) - 0.9603, q0) / std::pow((0.9408 - de) * std::pow(es0, ce) - 0.9603, q0);
    const double q29 = 15.16 / (1.0 + 0.196 * std::pow(er - 1.0, 2.0));
    double y = std::pow(er - 1.0, 3.0);
    const double q28 = 0.149 * y / (94.5 + 0.038 * y);
    y = std::pow(er - 1.0, 1.5);
    const double q27 = 0.4 * std::pow(g, 0.84) * (1.0 + 2.5 * y / (5.0 + y));
    y = std::pow((er - 1.0) / 13.0, 12.0);
    const double q26 = 30.0 - 22.2 * (y / (1.0 + 3.0 * y)) - q29;
    y = (er - 1.0) * (er - 1.0);
    const double q25 = (0.3 * fn * fn / (10.0 + fn * fn)) * (1.0 + 2.333 * y / (5.0 + y));
    const double q24 = 2.506 * q28 * std::pow(u, 0.894) * std::pow((1.0 + 1.3 * u) * fn / 99.25, 4.29) / (3.575 + std::pow(u, 0.894));
    const double q23 = 1.0 + 0.005 * fn * q27 / ((1.0 + 0.812 * std::pow(fn / 15.0, 1.9)) * (1.0 + 0.025 * u * u));
    const double q22 = 0.925 * std::pow(fn / q26, 1.536) / (1.0 + 0.3 * std::pow(fn / 30.0, 1.536));
    zImpairF = z0sF + (z0Impair0 * std::pow(erImpairF / erImpair0, q22) - z0sF * q23) / (1.0 + q24 + std::pow(0.46 * g, 2.2) * q25);
  }
};

// ---------- Paire couplée en ruban (Cohn), épaisseur reportée par le rapport de la ligne seule ----------
std::pair<double, double> paireRuban(double er, double h, double t, double w, double s) {
  const double kw = std::tanh(PI * w / (2 * h)), kws = std::tanh(PI * (w + s) / (2 * h));
  const double ke = kw * kws, ko = kw / kws;
  auto rapport = [](double k) { return ellipK(std::sqrt(1 - k * k)) / ellipK(k); };
  const double base = 30 * PI / std::sqrt(er);
  const double ze0 = base * rapport(ke), zo0 = base * rapport(ko);
  const double k = 1.0 / std::cosh(PI * w / (2 * h));
  const double zs0 = base * ellipK(k) / ellipK(std::sqrt(1 - k * k));
  double np = 0;
  const double zsT = ruban(er, h, 0, t, w, 1e9, 5.8e7, np);
  const double r = zs0 > 0 ? zsT / zs0 : 1.0;
  return {ze0 * r, zo0 * r};
}

double retardPsMm(double erEff) { return std::sqrt(std::max(erEff, 1.0)) * 1e9 / C0; }  // (s/m → ps/mm)
}  // namespace

double permittiviteA(const Dielectrique& d, double f) {
  if (!d.largeBande || f <= 0) return d.er;
  const double w1 = 2 * PI * d.f1, w2 = 2 * PI * d.f2, wr = 2 * PI * d.fRef, w = 2 * PI * f;
  const double im = [&](double x) { return (std::atan(x / w1) - std::atan(x / w2)) / std::numbers::ln10; }(wr);
  const double a = im > 0 ? d.tanD * d.er / im : 0;
  auto reel = [&](double x) { return 0.5 * std::log10((w2 * w2 + x * x) / (w1 * w1 + x * x)); };
  const double einf = d.er - a * reel(wr);
  return einf + a * reel(w);
}

double pertesA(const Dielectrique& d, double f) {
  if (!d.largeBande || f <= 0) return d.tanD;
  const double w1 = 2 * PI * d.f1, w2 = 2 * PI * d.f2, wr = 2 * PI * d.fRef, w = 2 * PI * f;
  auto im = [&](double x) { return (std::atan(x / w1) - std::atan(x / w2)) / std::numbers::ln10; };
  const double a = im(wr) > 0 ? d.tanD * d.er / im(wr) : 0;
  const double e2 = a * im(w), e1 = permittiviteA(d, f);
  return e1 > 0 ? e2 / e1 : 0;
}

double facteurRugosite(const Rugosite& r, double f, double sigma) {
  const double delta = profondeurPeau(f, sigma);
  if (delta <= 0) return 1;
  if (r.rayon > 0 && r.nombre > 0 && r.surfaceTuile > 0) {
    // Huray : chaque boule de rayon a perd comme une sphère dans le champ, rapporté à la tuile plane
    const double a = r.rayon;
    return 1.0 + 1.5 * (r.nombre * 4 * PI * a * a / r.surfaceTuile) / (1.0 + delta / a + delta * delta / (2 * a * a));
  }
  if (r.rq > 0) return 1.0 + (2.0 / PI) * std::atan(1.4 * std::pow(r.rq / delta, 2.0));  // Hammerstad
  return 1;
}

const char* nomGenreLigne(GenreLigne g) {
  switch (g) {
    case GenreLigne::Microruban: return "Microruban";
    case GenreLigne::Ruban: return "Ruban";
    case GenreLigne::MicrorubanCouple: return "Paire en microruban";
    case GenreLigne::RubanCouple: return "Paire en ruban";
  }
  return "?";
}

LigneTransmission::LigneTransmission(GenreLigne genre, Dielectrique d, Rugosite r, double sigma) : m_genre(genre), m_d(d), m_r(r), m_sigma(sigma) {}

ResultatLigne LigneTransmission::analyser(const GeometrieLigne& g, double f) const {
  ResultatLigne R;
  if (g.largeur <= 0 || g.h <= 0) return R;
  const double er = permittiviteA(m_d, f), tanD = pertesA(m_d, f);
  R.profondeurPeau = profondeurPeau(f, m_sigma);
  const double kRug = facteurRugosite(m_r, f, m_sigma);
  const double rs = R.profondeurPeau > 0 ? kRug / (m_sigma * R.profondeurPeau) : 0;
  auto pertesMicroruban = [&](double z0h, double w, double eeff0) {
    // Pertes du conducteur (Hammerstad) et du diélectrique, dB/m
    if (f > 0 && rs > 0) {
      const double K = std::exp(-1.2 * std::pow(z0h / ZF0, 0.7));
      const double qc = (PI * z0h * w * f) / (rs * C0 * K);
      R.pertesConducteur = (20.0 * PI / std::numbers::ln10) * f * std::sqrt(eeff0) / (C0 * qc);
    }
    R.pertesDielectrique = er > 1 ? (20.0 * PI / std::numbers::ln10) * (f / C0) * (er / std::sqrt(eeff0)) * ((eeff0 - 1.0) / (er - 1.0)) * tanD : 0;
  };
  switch (m_genre) {
    case GenreLigne::Microruban: {
      Microruban m{er, g.h, g.t, g.largeur, g.couvercle};
      m.statique();
      const auto [z, ef] = m.aFrequence(f);
      R.z0 = z;
      R.erEff = ef;
      pertesMicroruban(m.z0Homogene1, g.largeur, m.erEff0);
      break;
    }
    case GenreLigne::Ruban: {
      double np = 0;
      R.z0 = ruban(er, g.h, g.a, g.t, g.largeur, std::max(f, 1.0), m_sigma, np);
      R.erEff = er;
      R.pertesConducteur = DB_PAR_NP * np * kRug;
      R.pertesDielectrique = DB_PAR_NP * (PI / C0) * f * std::sqrt(er) * tanD;
      break;
    }
    case GenreLigne::MicrorubanCouple: {
      if (g.ecart <= 0) return R;
      PaireMicroruban p{er, g.h, g.t, g.largeur, g.ecart, g.couvercle};
      p.calculer(std::max(f, 1.0));
      R.zPair = p.zPairF;
      R.zImpair = p.zImpairF;
      R.zDiff = 2 * p.zImpairF;
      R.zCommun = p.zPairF / 2;
      R.z0 = p.zImpairF;
      R.erEffPair = p.erPairF;
      R.erEffImpair = p.erImpairF;
      R.erEff = p.erImpairF;
      pertesMicroruban(p.z0Impair0 * std::sqrt(p.erImpair0), g.largeur, p.erImpair0);
      break;
    }
    case GenreLigne::RubanCouple: {
      if (g.ecart <= 0) return R;
      const auto [ze, zo] = paireRuban(er, g.h, g.t, g.largeur, g.ecart);
      R.zPair = ze;
      R.zImpair = zo;
      R.zDiff = 2 * zo;
      R.zCommun = ze / 2;
      R.z0 = zo;
      R.erEff = R.erEffPair = R.erEffImpair = er;
      double np = 0;
      ruban(er, g.h, g.a, g.t, g.largeur, std::max(f, 1.0), m_sigma, np);
      R.pertesConducteur = DB_PAR_NP * np * kRug;
      R.pertesDielectrique = DB_PAR_NP * (PI / C0) * f * std::sqrt(er) * tanD;
      break;
    }
  }
  R.retard = retardPsMm(R.erEff);
  return R;
}

namespace {
bool couple(GenreLigne g) { return g == GenreLigne::MicrorubanCouple || g == GenreLigne::RubanCouple; }
}  // namespace

std::optional<double> LigneTransmission::largeurPour(double z, GeometrieLigne g, double f) const {
  // Z décroît avec la largeur : dichotomie sur une échelle logarithmique
  auto valeur = [&](double w) {
    g.largeur = w;
    const ResultatLigne r = analyser(g, f);
    return couple(m_genre) ? r.zDiff : r.z0;
  };
  double a = std::max(1e-6, g.h * 1e-3), b = g.h * 40;
  if (m_genre == GenreLigne::Ruban || m_genre == GenreLigne::RubanCouple) b = std::min(b, g.h * 8);
  double za = valeur(a), zb = valeur(b);
  if (!(std::isfinite(za) && std::isfinite(zb)) || z > za || z < zb) return std::nullopt;
  for (int i = 0; i < 80; ++i) {
    const double m = std::sqrt(a * b), zm = valeur(m);
    if (zm > z) a = m;
    else b = m;
  }
  return std::sqrt(a * b);
}

std::optional<double> LigneTransmission::ecartPour(double zdiff, GeometrieLigne g, double f) const {
  if (!couple(m_genre)) return std::nullopt;
  // Zdiff croît avec l'écart
  auto valeur = [&](double s) {
    g.ecart = s;
    return analyser(g, f).zDiff;
  };
  double a = std::max(1e-6, g.h * 0.01), b = g.h * 20;
  if (!(valeur(a) <= zdiff && valeur(b) >= zdiff)) return std::nullopt;
  for (int i = 0; i < 80; ++i) {
    const double m = std::sqrt(a * b);
    if (valeur(m) < zdiff) a = m;
    else b = m;
  }
  return std::sqrt(a * b);
}

// ---------- Sur la carte ----------
LigneSurCouche ligneSurCouche(const Empilage& e, Couche couche) {
  LigneSurCouche L;
  const auto pile = pileEmpilage(e);
  const int n = e.couchesCuivre, rang = rangCuivre(couche, n);
  std::vector<std::size_t> cuivres;
  for (std::size_t i = 0; i < pile.size(); ++i)
    if (pile[i].genre == GenreCoucheEmpilage::Cuivre) cuivres.push_back(i);
  L.geometrie.t = e.cuivre * 1e-3;
  L.dielectrique.er = 4.4;
  if (rang < 0 || static_cast<std::size_t>(rang) >= cuivres.size()) {
    L.geometrie.h = e.epaisseur * 1e-3;
    return L;
  }
  const std::size_t i = cuivres[static_cast<std::size_t>(rang)];
  L.geometrie.t = (pile[i].epaisseur > 0 ? pile[i].epaisseur : e.cuivre) * 1e-3;
  // Diélectriques jusqu'au cuivre voisin, au-dessus et au-dessous
  auto jusquAuCuivre = [&](int pas, double& epaisseur, double& erPondere, double& tanPondere) {
    epaisseur = erPondere = tanPondere = 0;
    for (int j = static_cast<int>(i) + pas; j >= 0 && j < static_cast<int>(pile.size()); j += pas) {
      const CoucheEmpilage& x = pile[static_cast<std::size_t>(j)];
      if (x.genre == GenreCoucheEmpilage::Cuivre || x.genre == GenreCoucheEmpilage::AmeMetallique) return true;
      epaisseur += x.epaisseur;
      erPondere += x.permittivite * x.epaisseur;
      tanPondere += x.pertes * x.epaisseur;
    }
    return false;
  };
  double hd = 0, erd = 0, tand = 0, hb = 0, erb = 0, tanb = 0;
  const bool planDessus = jusquAuCuivre(-1, hd, erd, tand), planDessous = jusquAuCuivre(+1, hb, erb, tanb);
  if (planDessus && planDessous && hd > 0 && hb > 0) {
    L.genre = GenreLigne::Ruban;
    L.geometrie.h = (hd + hb) * 1e-3 + L.geometrie.t;
    L.geometrie.a = hd * 1e-3;
    L.dielectrique.er = (erd + erb) / (hd + hb);
    L.dielectrique.tanD = (tand + tanb) / (hd + hb);
  } else {
    const bool dessous = planDessous && hb > 0;
    const double hh = dessous ? hb : hd;
    L.genre = GenreLigne::Microruban;
    L.geometrie.h = (hh > 0 ? hh : e.epaisseur) * 1e-3;
    if (hh > 0) {
      L.dielectrique.er = (dessous ? erb : erd) / hh;
      L.dielectrique.tanD = (dessous ? tanb : tand) / hh;
    }
  }
  return L;
}

void calculerProfil(const ConceptionElec& c, ProfilImpedance& p, Nm ecart, double frequence) {
  p.parCouche.clear();
  const Empilage& e = c.circuit.empilage;
  for (Couche k : couchesCuivre(e.couchesCuivre)) {
    LigneSurCouche L = ligneSurCouche(e, k);
    GenreLigne g = L.genre;
    if (p.differentiel) g = g == GenreLigne::Ruban ? GenreLigne::RubanCouple : GenreLigne::MicrorubanCouple;
    L.geometrie.ecart = mm(ecart) * 1e-3;
    if (p.differentiel && L.geometrie.ecart <= 0) L.geometrie.ecart = 0.15e-3;
    const LigneTransmission ligne(g, L.dielectrique);
    if (const auto w = ligne.largeurPour(p.impedance, L.geometrie, frequence))
      p.parCouche[codeCouche(k)] = {static_cast<Nm>(std::llround(*w * 1e9 / 1000.0)) * 1000, p.differentiel ? static_cast<Nm>(std::llround(L.geometrie.ecart * 1e9)) : Nm{0}};
  }
}

const ProfilImpedance* profilDuReseau(const ConceptionElec& c, const std::string& reseau) {
  const ClasseReseau* k = classeDe(c.circuit.regles, reseau);
  if (!k) return nullptr;
  for (const auto& p : c.circuit.regles.avancees.impedances)
    if (p.nom == k->nom) return &p;
  return nullptr;
}

Nm largeurSurCouche(const ConceptionElec& c, const std::string& reseau, Couche couche) {
  if (const ProfilImpedance* p = profilDuReseau(c, reseau))
    if (const auto it = p->parCouche.find(codeCouche(couche)); it != p->parCouche.end() && it->second.first > 0) return it->second.first;
  return largeurPiste(c.circuit.regles, reseau);
}

double retardCouche(const ConceptionElec& c, Couche couche, Nm largeur, double frequence) {
  const LigneSurCouche L = ligneSurCouche(c.circuit.empilage, couche);
  GeometrieLigne g = L.geometrie;
  g.largeur = std::max(mm(largeur), 0.01) * 1e-3;
  const ResultatLigne r = LigneTransmission(L.genre, L.dielectrique).analyser(g, frequence);
  return r.retard > 0 ? r.retard : retardPsMm(L.dielectrique.er);
}

double retardVia(const ConceptionElec& c, Couche de, Couche vers) {
  // Fût de la via entre les deux couches, dans la permittivité moyenne de ce qu'il traverse
  const auto pile = pileEmpilage(c.circuit.empilage);
  const int n = c.circuit.empilage.couchesCuivre;
  int ra = rangCuivre(de, n), rb = rangCuivre(vers, n);
  if (ra > rb) std::swap(ra, rb);
  double longueur = 0, erL = 0;
  int rang = -1;
  for (const auto& x : pile) {
    if (x.genre == GenreCoucheEmpilage::Cuivre) {
      ++rang;
      continue;
    }
    if (rang >= ra && rang < rb) {
      longueur += x.epaisseur;
      erL += x.permittivite * x.epaisseur;
    }
  }
  if (longueur <= 0) return 0;
  return longueur * retardPsMm(erL / longueur);
}

}  // namespace prom::ecad

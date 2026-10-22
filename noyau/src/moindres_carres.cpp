// Prométhée : moindres carrés creux. Licence GPL-3.0-only.
#include "promethee/moindres_carres.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace prom::numerique {

namespace {

double norme2(const std::vector<double>& v) {
  double s = 0;
  for (double a : v) s += a * a;
  return s;
}

using Taille = std::size_t;
Taille T(int i) { return static_cast<Taille>(i); }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Cholesky à profil

void CholeskyProfil::structure(const MatriceCreuse& J, int n) {
  n_ = n;
  // Graphe de JᵀJ
  std::vector<std::vector<int>> voisins(T(n));
  for (const auto& ligne : J)
    for (const auto& [a, va] : ligne) {
      (void)va;
      for (const auto& [b, vb] : ligne) {
        (void)vb;
        if (a != b) voisins[T(a)].push_back(b);
      }
    }
  for (auto& v : voisins) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
  }
  // Cuthill-McKee inverse, composante par composante, en partant d'un nœud de degré minimal
  std::vector<int> ordre;
  ordre.reserve(T(n));
  std::vector<char> vu(T(n), 0);
  std::vector<int> parDegre(T(n));
  for (int i = 0; i < n; ++i) parDegre[T(i)] = i;
  std::stable_sort(parDegre.begin(), parDegre.end(), [&](int a, int b) { return voisins[T(a)].size() < voisins[T(b)].size(); });
  for (int depart : parDegre) {
    if (vu[T(depart)]) continue;
    std::deque<int> file{depart};
    vu[T(depart)] = 1;
    while (!file.empty()) {
      const int u = file.front();
      file.pop_front();
      ordre.push_back(u);
      std::vector<int> suivants;
      for (int w : voisins[T(u)])
        if (!vu[T(w)]) { vu[T(w)] = 1; suivants.push_back(w); }
      std::stable_sort(suivants.begin(), suivants.end(), [&](int a, int b) { return voisins[T(a)].size() < voisins[T(b)].size(); });
      for (int w : suivants) file.push_back(w);
    }
  }
  std::reverse(ordre.begin(), ordre.end());
  perm_.assign(T(n), 0);
  inv_ = ordre;
  for (int k = 0; k < n; ++k) perm_[T(ordre[T(k)])] = k;
  // Profil
  debut_.assign(T(n), 0);
  for (int i = 0; i < n; ++i) debut_[T(i)] = i;
  for (int a = 0; a < n; ++a)
    for (int b : voisins[T(a)]) {
      const int pa = perm_[T(a)], pb = perm_[T(b)];
      if (pb < pa) debut_[T(pa)] = std::min(debut_[T(pa)], pb);
    }
  position_.assign(T(n) + 1, 0);
  for (int i = 0; i < n; ++i) position_[T(i) + 1] = position_[T(i)] + T(i - debut_[T(i)] + 1);
  normale_.assign(position_[T(n)], 0.0);
  valeurs_.assign(position_[T(n)], 0.0);
}

void CholeskyProfil::assemblerNormale(const MatriceCreuse& J, std::vector<double>& diag) {
  std::fill(normale_.begin(), normale_.end(), 0.0);
  for (const auto& ligne : J)
    for (const auto& [a, va] : ligne) {
      if (va == 0) continue;
      const int pa = perm_[T(a)];
      for (const auto& [b, vb] : ligne) {
        if (vb == 0) continue;
        const int pb = perm_[T(b)];
        if (pb <= pa) normale_[indice(pa, pb)] += va * vb;
      }
    }
  diag.assign(T(n_), 0.0);
  for (int a = 0; a < n_; ++a) diag[T(a)] = normale_[indice(perm_[T(a)], perm_[T(a)])];
}

bool CholeskyProfil::factoriser(const std::vector<double>& ajout) {
  valeurs_ = normale_;
  for (int a = 0; a < n_; ++a) valeurs_[indice(perm_[T(a)], perm_[T(a)])] += ajout[T(a)];
  for (int i = 0; i < n_; ++i) {
    const int di = debut_[T(i)];
    for (int j = di; j <= i; ++j) {
      double s = valeurs_[indice(i, j)];
      const int k0 = std::max(di, debut_[T(j)]);
      const double* li = &valeurs_[indice(i, k0)];
      const double* lj = &valeurs_[indice(j, k0)];
      for (int k = k0; k < j; ++k) s -= *li++ * *lj++;
      if (j < i) valeurs_[indice(i, j)] = s / valeurs_[indice(j, j)];
      else {
        if (!(s > 0) || !std::isfinite(s)) return false;
        valeurs_[indice(i, i)] = std::sqrt(s);
      }
    }
  }
  return true;
}

void CholeskyProfil::resoudre(const std::vector<double>& b, std::vector<double>& x) const {
  std::vector<double> y(T(n_));
  for (int a = 0; a < n_; ++a) y[T(perm_[T(a)])] = b[T(a)];
  for (int i = 0; i < n_; ++i) {
    double s = y[T(i)];
    for (int k = debut_[T(i)]; k < i; ++k) s -= valeurs_[indice(i, k)] * y[T(k)];
    y[T(i)] = s / valeurs_[indice(i, i)];
  }
  for (int i = n_ - 1; i >= 0; --i) {
    y[T(i)] /= valeurs_[indice(i, i)];
    const double yi = y[T(i)];
    for (int k = debut_[T(i)]; k < i; ++k) y[T(k)] -= valeurs_[indice(i, k)] * yi;
  }
  x.assign(T(n_), 0.0);
  for (int a = 0; a < n_; ++a) x[T(a)] = y[T(perm_[T(a)])];
}

// ---------------------------------------------------------------------------------------------------------------
// Levenberg-Marquardt

double levenbergMarquardt(ProblemeMoindresCarres& p, std::vector<double>& x, const OptionsLM& o) {
  std::vector<double> F;
  p.residus(x, F);
  double cout = norme2(F);
  const int n = p.dimension();
  if (n == 0) return cout;
  double lambda = 1e-3;
  bool secoue = false;
  MatriceCreuse J;
  CholeskyProfil chol;
  bool structurePosee = false;
  std::vector<double> diag, g(T(n)), ajout(T(n)), delta, xn, Fn;
  for (int it = 0; it < o.iterations && cout > o.coutCible; ++it) {
    p.jacobienne(x, J);
    if (!structurePosee) { chol.structure(J, n); structurePosee = true; }
    chol.assemblerNormale(J, diag);
    std::fill(g.begin(), g.end(), 0.0);
    for (Taille i = 0; i < J.size() && i < F.size(); ++i)
      for (const auto& [j, v] : J[i]) g[T(j)] -= v * F[i];
    bool progres = false;
    while (lambda < 1e14) {
      for (int j = 0; j < n; ++j) ajout[T(j)] = lambda * diag[T(j)] + lambda * 1e-6 + 1e-12;
      if (chol.factoriser(ajout)) {
        chol.resoudre(g, delta);
        xn = x;
        for (int j = 0; j < n; ++j) xn[T(j)] += delta[T(j)];
        p.residus(xn, Fn);
        const double cn = norme2(Fn);
        if (cn < cout) {
          const double gain = cout - cn;
          x.swap(xn);
          F.swap(Fn);
          cout = cn;
          lambda = std::max(lambda / 3, 1e-12);
          progres = true;
          if (gain < 1e-26) it = o.iterations;
          break;
        }
      }
      lambda *= 4;
    }
    if (!progres) {
      // Point selle (deux lignes exactement perpendiculaires qu'on veut parallèles…) : gradient nul, rien ne bouge ;
      // une seule petite secousse déterministe permet d'en sortir (ES-21)
      if (!o.secousse || secoue || cout < 1e-16) break;
      secoue = true;
      for (int j = 0; j < n; ++j) x[T(j)] += 1e-4 * std::max(1.0, std::abs(x[T(j)])) * std::sin(1.0 + 2.399963 * j);
      p.residus(x, F);
      cout = norme2(F);
      lambda = 1e-3;
    }
  }
  return cout;
}

// ---------------------------------------------------------------------------------------------------------------
// Analyse de rang

namespace {
double valeurEn(const LigneCreuse& l, int col) {
  const auto it = std::lower_bound(l.begin(), l.end(), col, [](const std::pair<int, double>& a, int c) { return a.first < c; });
  return it != l.end() && it->first == col ? it->second : 0.0;
}
}  // namespace

AnalyseRang analyserRang(MatriceCreuse J, int n, bool avecNoyau, double tol) {
  AnalyseRang r;
  const int m = static_cast<int>(J.size());
  // Lignes ramenées à une norme unité : une contrainte sur une petite ligne ne pèse pas plus qu'une autre, et le
  // rang ne dépend pas de l'échelle de l'esquisse (ES-23, ES-24)
  std::vector<std::vector<int>> occupants(T(n));
  for (int i = 0; i < m; ++i) {
    auto& l = J[T(i)];
    l.erase(std::remove_if(l.begin(), l.end(), [](const std::pair<int, double>& a) { return a.second == 0; }), l.end());
    double nrm = 0;
    for (const auto& [j, v] : l) nrm += v * v;
    nrm = std::sqrt(nrm);
    if (nrm > 1e-300)
      for (auto& [j, v] : l) v /= nrm;
    for (const auto& [j, v] : l) occupants[T(j)].push_back(i);
  }
  std::vector<char> pivotee(T(m), 0);
  std::vector<int> colPivot, lignePivot;
  std::vector<int> candidats;
  LigneCreuse fusion;
  for (int col = 0; col < n; ++col) {
    candidats.clear();
    double maxi = 0;
    auto& occ = occupants[T(col)];
    std::sort(occ.begin(), occ.end());
    occ.erase(std::unique(occ.begin(), occ.end()), occ.end());
    for (int i : occ) {
      if (pivotee[T(i)]) continue;
      const double v = valeurEn(J[T(i)], col);
      if (v == 0) continue;
      candidats.push_back(i);
      maxi = std::max(maxi, std::abs(v));
    }
    if (maxi < tol) continue;
    // Pivot seuil : parmi les lignes assez fortes, la plus ancienne (indice le plus petit)
    int piv = -1;
    for (int i : candidats)
      if (std::abs(valeurEn(J[T(i)], col)) >= 0.25 * maxi && (piv < 0 || i < piv)) piv = i;
    auto& lp = J[T(piv)];
    const double d = valeurEn(lp, col);
    for (auto& [j, v] : lp) v /= d;
    for (int i : candidats) {
      if (i == piv) continue;
      auto& li = J[T(i)];
      const double f = valeurEn(li, col);
      if (f == 0) continue;
      fusion.clear();
      auto a = li.begin();
      auto b = lp.begin();
      while (a != li.end() || b != lp.end()) {
        if (b == lp.end() || (a != li.end() && a->first < b->first)) { fusion.push_back(*a); ++a; }
        else if (a == li.end() || b->first < a->first) {
          const double v = -f * b->second;
          if (std::abs(v) > 1e-15) { fusion.emplace_back(b->first, v); occupants[T(b->first)].push_back(i); }
          ++b;
        } else {
          const double v = b->first == col ? 0.0 : a->second - f * b->second;
          if (std::abs(v) > 1e-15) fusion.emplace_back(a->first, v);
          ++a;
          ++b;
        }
      }
      li.swap(fusion);
    }
    pivotee[T(piv)] = 1;
    colPivot.push_back(col);
    lignePivot.push_back(piv);
  }
  r.rang = static_cast<int>(colPivot.size());
  for (int i = 0; i < m; ++i)
    if (!pivotee[T(i)]) r.lignesDependantes.push_back(i);
  r.parametreLibre.assign(T(n), false);
  std::vector<char> estPivot(T(n), 0);
  for (int c : colPivot) estPivot[T(c)] = 1;
  // Noyau : pour chaque colonne libre f, x_f = 1, les autres libres à 0, et les colonnes pivots par remontée
  std::vector<double> v(T(n));
  for (int f = 0; f < n; ++f) {
    if (estPivot[T(f)]) continue;
    r.parametreLibre[T(f)] = true;
    std::fill(v.begin(), v.end(), 0.0);
    v[T(f)] = 1;
    for (int k = static_cast<int>(colPivot.size()) - 1; k >= 0; --k) {
      double s = 0;
      for (const auto& [j, a] : J[T(lignePivot[T(k)])])
        if (j != colPivot[T(k)]) s += a * v[T(j)];
      v[T(colPivot[T(k)])] = -s;
    }
    for (int c : colPivot)
      if (std::abs(v[T(c)]) > 1e-7) r.parametreLibre[T(c)] = true;
    if (avecNoyau) r.noyau.push_back(v);
  }
  return r;
}

}  // namespace prom::numerique

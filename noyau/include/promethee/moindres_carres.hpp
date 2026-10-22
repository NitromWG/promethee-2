// Prométhée : moindres carrés non linéaires creux (Levenberg-Marquardt), Cholesky à profil après renumérotation de
// Cuthill-McKee inverse, et analyse de rang d'une jacobienne creuse (rang, lignes dépendantes, paramètres libres,
// base du noyau). Indépendant de l'esquisse : sert aussi aux autres solveurs. Licence GPL-3.0-only.
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace prom::numerique {

// Ligne creuse : (colonne, valeur), colonnes strictement croissantes. Une entrée structurelle peut valoir 0.
using LigneCreuse = std::vector<std::pair<int, double>>;
using MatriceCreuse = std::vector<LigneCreuse>;

// Problème F(x) = 0 au sens des moindres carrés ; la structure de la jacobienne ne change pas d'un appel à l'autre.
class ProblemeMoindresCarres {
 public:
  virtual ~ProblemeMoindresCarres() = default;
  [[nodiscard]] virtual int dimension() const = 0;
  virtual void residus(const std::vector<double>& x, std::vector<double>& F) = 0;
  virtual void jacobienne(const std::vector<double>& x, MatriceCreuse& J) = 0;
};

struct OptionsLM {
  int iterations = 200;
  double coutCible = 1e-22;  // arrêt quand ‖F‖² passe dessous
  bool secousse = true;      // une petite secousse déterministe pour sortir d'un point selle
};

// Renvoie ‖F‖² final ; x est modifié en place.
double levenbergMarquardt(ProblemeMoindresCarres& p, std::vector<double>& x, const OptionsLM& o = {});

// Matrice symétrique définie positive creuse, factorisée par Cholesky sur son profil après renumérotation de
// Cuthill-McKee inverse (largeur de bande faible pour les systèmes géométriques : coût quasi linéaire).
class CholeskyProfil {
 public:
  // Structure tirée des lignes de J : deux colonnes présentes sur une même ligne sont voisines dans JᵀJ.
  void structure(const MatriceCreuse& J, int n);
  // Assemble JᵀJ (structure déjà posée) ; diag reçoit sa diagonale.
  void assemblerNormale(const MatriceCreuse& J, std::vector<double>& diag);
  // Ajoute d[i] à la diagonale de la copie à factoriser ; renvoie false si la matrice n'est pas définie positive.
  bool factoriser(const std::vector<double>& ajoutDiagonale);
  // Résout L Lᵀ x = b (dans la numérotation d'origine).
  void resoudre(const std::vector<double>& b, std::vector<double>& x) const;
  [[nodiscard]] int taille() const { return n_; }
  [[nodiscard]] std::size_t occupation() const { return valeurs_.size(); }

 private:
  int n_ = 0;
  std::vector<int> perm_, inv_;       // perm_[ancien] = nouveau, inv_[nouveau] = ancien
  std::vector<int> debut_;            // première colonne du profil de chaque ligne (numérotation nouvelle)
  std::vector<std::size_t> position_; // décalage de la ligne dans valeurs_
  std::vector<double> normale_, valeurs_;
  [[nodiscard]] std::size_t indice(int i, int j) const { return position_[static_cast<std::size_t>(i)] + static_cast<std::size_t>(j - debut_[static_cast<std::size_t>(i)]); }
};

struct AnalyseRang {
  int rang = 0;
  std::vector<int> lignesDependantes;       // lignes sans pivot : redondantes ou en conflit
  std::vector<bool> parametreLibre;         // paramètre que bouge au moins une direction du noyau
  std::vector<std::vector<double>> noyau;   // base du noyau (non orthonormée), si demandée
};

// Élimination creuse à pivot seuil (parmi les pivots assez forts, la ligne la plus ancienne garde le pivot : ce
// sont les dernières contraintes posées qui sont désignées redondantes). Lignes ramenées à une norme unité.
AnalyseRang analyserRang(MatriceCreuse J, int n, bool avecNoyau = false, double tolerance = 1e-8);

}  // namespace prom::numerique

// Prométhée : classeur XLSX à plusieurs feuilles (lot L19 de la 0.34 ; 19.13). Texte ou nombre par cellule, en-tête
// en gras figé, largeurs de colonnes d'après le contenu ; archive OOXML minimale lue par Excel et LibreOffice Calc.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

namespace prom {

struct CelluleClasseur {
  std::string texte;
  double nombre = 0;
  bool estNombre = false;
  CelluleClasseur() = default;
  CelluleClasseur(std::string t) : texte(std::move(t)) {}  // NOLINT : conversion voulue
  CelluleClasseur(const char* t) : texte(t) {}             // NOLINT
  CelluleClasseur(double v) : nombre(v), estNombre(true) {}  // NOLINT
  CelluleClasseur(int v) : nombre(v), estNombre(true) {}     // NOLINT
};
struct FeuilleClasseur {
  std::string nom;  // 31 caractères au plus, sans « []:*?/\ »
  std::vector<std::string> entetes;
  std::vector<std::vector<CelluleClasseur>> lignes;
};
std::string xlsxClasseur(const std::vector<FeuilleClasseur>& feuilles);
// Nom de feuille rendu valide (caractères interdits remplacés, 31 caractères au plus)
std::string nomFeuilleValide(const std::string& nom);

}  // namespace prom

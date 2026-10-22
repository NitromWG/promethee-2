// Prométhée : construction de la tôlerie pendant le rejeu de l'arbre (interne à la bibliothèque de géométrie).
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec2d.hxx>

#include "promethee/construction.hpp"
#include "promethee/tolerie.hpp"

namespace prom::tolerie {

// Transformation rigide du plan (rotation, translation, miroir éventuel)
struct Affine2 {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  gp_Pnt2d operator()(const gp_Pnt2d& p) const { return {a * p.X() + b * p.Y() + e, c * p.X() + d * p.Y() + f}; }
  Affine2 operator*(const Affine2& o) const {
    return {a * o.a + b * o.c, a * o.b + b * o.d, c * o.a + d * o.c, c * o.b + d * o.d, a * o.e + b * o.f + e, c * o.e + d * o.f + f};
  }
};

// Paroi : plaque plane d'épaisseur constante, repère local (X, Y dans le plan, Z normal ; matière de z = 0 à z = e)
struct Paroi {
  int fonction = 0;
  gp_Ax3 repere;
  Affine2 versDeplie;  // (x, y) local → (X, Y) du déplié
  struct Cote {
    std::string cle;   // « cote:12 » (élément d'esquisse), « bout », « cote1 », « cote2 »
    gp_Pnt2d a, b;
    gp_Vec2d dehors;   // unitaire, vers l'extérieur de la paroi
  };
  std::vector<Cote> cotes;
  std::map<std::string, double> retraitsPli;  // côté déjà plié → recul du début du pli vers l'intérieur (≥ 0)
};

struct OutilTole {
  TopoDS_Shape ajout, retrait;  // matière du pli et du bord (à réunir), entaille et dégagements (à enlever)
  std::vector<FaceNommee> nomsAjout, nomsRetrait;
  std::string erreur;
  std::vector<std::string> avertissements;
};

class Constructeur {
 public:
  bool actif() const { return m_actif; }
  int base() const { return m_base; }
  // Tôle de base : faces des régions retenues (dans le plan de l'esquisse), arêtes d'origine (arête → élément)
  std::string poserBase(const FonctionPiece& f, const std::vector<TopoDS_Face>& regions, const std::vector<std::pair<TopoDS_Edge, int>>& origine, const gp_Ax3& ax);
  // Bord tombé : outils à appliquer au corps ; rien n'est retenu avant « valider »
  OutilTole bordTombe(const FonctionPiece& f, const TopoDS_Shape& corps);
  void valider();
  void abandonner();
  // Fonction appliquée au corps après la tôlerie, que le déplié ne reprend pas
  void ignorer(const FonctionPiece& f);
  DeplieTole deplie() const;

 private:
  void effacerAttente();
  bool m_actif = false;
  int m_base = 0;
  double m_e = 1, m_r = 1, m_k = 0.5;
  std::string m_degagement = "rectangulaire";
  gp_Ax3 m_repere;
  std::map<int, Paroi> m_parois;
  TopoDS_Shape m_plat;  // faces du déplié dans le plan z = 0 du repère
  std::vector<LignePli> m_plis;
  std::vector<std::string> m_alertes;
  // En attente de validation
  struct Attente {
    bool present = false;
    Paroi paroi;
    int parent = 0;
    std::string cle;
    double retrait = 0;
    TopoDS_Shape plat;
    LignePli pli;
  } m_attente;
};

}  // namespace prom::tolerie

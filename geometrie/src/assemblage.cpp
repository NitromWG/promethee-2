// Prométhée : résolution des contraintes d'assemblage. Licence GPL-3.0-only.
#include "promethee/assemblage.hpp"
#include "promethee/structure.hpp"
#include "promethee/moindres_carres.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <functional>
#include <numbers>
#include <stdexcept>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pln.hxx>

namespace prom {

namespace {
constexpr double DEG = 180 / std::numbers::pi;

gp_Trsf miroirDe(const ComposantAssemblage& c) {
  gp_Trsf m;
  if (c.miroir == 1) m.SetMirror(gp::XOY());
  else if (c.miroir == 2) m.SetMirror(gp::ZOX());
  else if (c.miroir == 3) m.SetMirror(gp::YOZ());
  return m;
}
// Partie rigide du placement (translation et rotation, sans la symétrie)
gp_Trsf rigideDe(double x, double y, double z, double rx, double ry, double rz) {
  gp_Trsf tx, ty, tz, t;
  tx.SetRotation(gp::OX(), rx / DEG);
  ty.SetRotation(gp::OY(), ry / DEG);
  tz.SetRotation(gp::OZ(), rz / DEG);
  t.SetTranslation(gp_Vec(x, y, z));
  return t * tz * ty * tx;
}
gp_Mat rotationDe(double rx, double ry, double rz) { return rigideDe(0, 0, 0, rx, ry, rz).VectorialPart(); }

double angleProche(double a, double proche) { return a + 360 * std::round((proche - a) / 360); }

// Angles d'Euler (degrés, R = Rz·Ry·Rx) d'une rotation, choisis au plus près des précédents : des deux écritures
// possibles, la plus proche, et chaque angle déroulé du nombre de tours voulu (continuité d'une résolution à l'autre).
std::array<double, 3> eulerDepuis(const gp_Mat& R, const std::array<double, 3>& prec) {
  const double r11 = R(1, 1), r21 = R(2, 1), r31 = R(3, 1), r32 = R(3, 2), r33 = R(3, 3), r12 = R(1, 2), r22 = R(2, 2);
  const double cb = std::hypot(r11, r21);
  std::array<std::array<double, 3>, 2> sol;
  if (cb > 1e-9) {
    const double b = std::atan2(-r31, cb), a = std::atan2(r21, r11), c = std::atan2(r32, r33);
    sol[0] = {c * DEG, b * DEG, a * DEG};
    sol[1] = {(c + std::numbers::pi) * DEG, (std::numbers::pi - b) * DEG, (a + std::numbers::pi) * DEG};
  } else {
    // Blocage de cardan (ry = ±90°) : seule une combinaison de rx et rz compte ; on garde rx précédent
    const double c = prec[0] / DEG;
    const bool haut = -r31 > 0;
    const double a = haut ? c - std::atan2(r12, r22) : std::atan2(-r12, r22) - c;
    sol[0] = sol[1] = {prec[0], haut ? 90.0 : -90.0, a * DEG};
  }
  std::array<double, 3> meilleur{};
  double ecart = 1e300;
  for (auto s : sol) {
    for (int k = 0; k < 3; ++k) s[static_cast<std::size_t>(k)] = angleProche(s[static_cast<std::size_t>(k)], prec[static_cast<std::size_t>(k)]);
    double e = 0;
    for (int k = 0; k < 3; ++k) e += std::pow(s[static_cast<std::size_t>(k)] - prec[static_cast<std::size_t>(k)], 2);
    if (e < ecart) { ecart = e; meilleur = s; }
  }
  return meilleur;
}

gp_Dir perpendiculaire(const gp_Dir& v) {
  gp_Vec a = gp_Vec(v).Crossed(gp_Vec(0, 0, 1));
  if (a.Magnitude() < 0.1) a = gp_Vec(v).Crossed(gp_Vec(1, 0, 0));
  return gp_Dir(a);
}

// Angle (rad, dans ]−π ; π]) dont la rotation D tourne autour de l'axe a (exprimé avant la rotation)
double angleAutourAxe(const gp_Mat& D, const gp_Dir& a) {
  const gp_Vec w(perpendiculaire(a));
  const gp_XYZ dw = D * w.XYZ();
  const gp_Vec v(dw);
  return std::atan2(w.Crossed(v).Dot(gp_Vec(a)), w.Dot(v));
}
double derouler(double replie, double estime) { return replie + 2 * std::numbers::pi * std::round((estime - replie) / (2 * std::numbers::pi)); }

// Géométrie d'une face de référence, dans le repère de sa pièce : plan (point, normale sortante), axe d'un cylindre
// ou d'un cône, centre d'une sphère.
// A2 : aussi une droite (arête droite, axe de base ou de référence), un cercle (arête circulaire : son axe, son
// centre) et un point (sommet, origine, point de référence)
enum class Genre { Aucun, Plan, Cylindre, Cone, Sphere, Droite, Cercle, Point };
struct Ref {
  Genre genre = Genre::Aucun;
  gp_Pnt p;
  gp_Dir d;
  double rayon = 0;
  bool plan() const { return genre == Genre::Plan; }
  // Droite portée : axe d'une face de révolution, arête droite, axe d'une arête circulaire
  bool axe() const { return genre == Genre::Cylindre || genre == Genre::Cone || genre == Genre::Droite || genre == Genre::Cercle; }
  bool point() const { return genre == Genre::Point; }
};
const char* nomGenre(Genre g) {
  switch (g) {
    case Genre::Plan: return "plan";
    case Genre::Cylindre: return "cylindre";
    case Genre::Cone: return "cone";
    case Genre::Sphere: return "sphere";
    case Genre::Droite: return "droite";
    case Genre::Cercle: return "cercle";
    case Genre::Point: return "point";
    case Genre::Aucun: break;
  }
  return "";
}

// Plans, axes et origine d'un repère : « XY » (normale +Z), « XZ » (normale +Y), « YZ » (normale +X), « X », « Y »,
// « Z », « O »
Ref referenceBase(const std::string& quoi) {
  Ref x;
  x.p = gp::Origin();
  if (quoi == "XY" || quoi == "XZ" || quoi == "YZ") {
    x.genre = Genre::Plan;
    x.d = quoi == "XY" ? gp::DZ() : quoi == "XZ" ? gp::DY() : gp::DX();
  } else if (quoi == "X" || quoi == "Y" || quoi == "Z") {
    x.genre = Genre::Droite;
    x.d = quoi == "X" ? gp::DX() : quoi == "Y" ? gp::DY() : gp::DZ();
  } else if (quoi == "O") {
    x.genre = Genre::Point;
    x.d = gp::DZ();
  }
  return x;
}

// Arête d'une pièce : droite (premier sommet, direction du premier au dernier) ou cercle (centre, axe, rayon)
Ref referenceArete(const TopoDS_Edge& e) {
  Ref x;
  BRepAdaptor_Curve c(e);
  if (c.GetType() == GeomAbs_Line) {
    const gp_Pnt a = c.Value(c.FirstParameter()), b = c.Value(c.LastParameter());
    if (a.Distance(b) < 1e-12) return x;
    x.genre = Genre::Droite;
    x.p = a;
    x.d = gp_Dir(gp_Vec(a, b));
  } else if (c.GetType() == GeomAbs_Circle) {
    x.genre = Genre::Cercle;
    x.p = c.Circle().Location();
    x.d = c.Circle().Axis().Direction();
    x.rayon = c.Circle().Radius();
  }
  return x;
}

std::string apres(const std::string& nom, const char* prefixe) {
  const std::string p = prefixe;
  return nom.rfind(p, 0) == 0 ? nom.substr(p.size()) : std::string();
}
int entierOu(const std::string& t, int defaut) {
  try {
    std::size_t lu = 0;
    const int v = std::stoi(t, &lu);
    return lu == t.size() ? v : defaut;
  } catch (...) {
    return defaut;
  }
}

Ref reference(const ResultatPiece& r, const std::string& nom) {
  Ref x;
  // Repère de la pièce (A2) : « base:XY », « base:X », « base:O »…
  if (const std::string b = apres(nom, "base:"); !b.empty()) return referenceBase(b);
  // Géométrie de référence de la pièce (A2) : « plan:12 », « axe:12 », « point:12 » (numéro de la fonction)
  if (const std::string id = apres(nom, "plan:"); !id.empty()) {
    for (const auto& [i, ax] : r.plansReference)
      if (i == entierOu(id, -1)) { x.genre = Genre::Plan; x.p = ax.Location(); x.d = ax.Direction(); return x; }
    return x;
  }
  if (const std::string id = apres(nom, "axe:"); !id.empty()) {
    for (const auto& [i, ax] : r.axesReference)
      if (i == entierOu(id, -1)) { x.genre = Genre::Droite; x.p = ax.Location(); x.d = ax.Direction(); return x; }
    return x;
  }
  if (const std::string id = apres(nom, "point:"); !id.empty()) {
    for (const auto& [i, q] : r.pointsReference)
      if (i == entierOu(id, -1)) { x.genre = Genre::Point; x.p = q; x.d = gp::DZ(); return x; }
    return x;
  }
  // Sommet (« S:A|B|C ») et arête (« A|B ») : même résolution stricte que les faces
  if (nom.rfind("S:", 0) == 0) {
    TopoDS_Vertex v;
    if (!r.resoudreSommet(nom, v).trouvee() || v.IsNull()) return x;
    x.genre = Genre::Point;
    x.p = BRep_Tool::Pnt(v);
    x.d = gp::DZ();
    return x;
  }
  if (nom.find('|') != std::string::npos) {
    std::vector<TopoDS_Edge> aretes;
    if (r.resoudreAretes(nom, aretes).trouvee() && aretes.size() == 1) return referenceArete(aretes.front());
    return x;
  }
  // Résolution stricte : une face disparue ou coupée en morceaux ne se remplace pas par une autre ; la contrainte
  // reste alors non satisfaite, et le dit.
  std::vector<TopoDS_Face> faces;
  if (!r.resoudreFaces(nom, faces, true).trouvee()) return x;
  for (const auto& f0 : faces) {
    // L'orientation qui compte est celle de la face dans le solide (normale sortante)
    TopoDS_Face f = f0;
    for (TopExp_Explorer ex(r.forme, TopAbs_FACE); ex.More(); ex.Next())
      if (ex.Current().IsSame(f0)) { f = TopoDS::Face(ex.Current()); break; }
    BRepAdaptor_Surface s(f);
    if (s.GetType() == GeomAbs_Plane) {
      x.genre = Genre::Plan;
      x.p = s.Plane().Location();
      // Normale géométrique Xdir ^ Ydir : opposée à l'axe principal quand le repère est indirect (face copiée
      // par une symétrie de pièce, AS-11), puis retournée selon l'orientation de la face dans le solide
      gp_Dir n = s.Plane().Axis().Direction();
      if (!s.Plane().Position().Direct()) n.Reverse();
      if (f.Orientation() == TopAbs_REVERSED) n.Reverse();
      x.d = n;
      return x;
    }
    if (s.GetType() == GeomAbs_Sphere) {
      x.genre = Genre::Sphere;
      x.p = s.Sphere().Location();
      x.d = gp_Dir(0, 0, 1);  // sans objet : une sphère n'a pas d'axe
      x.rayon = s.Sphere().Radius();
      return x;
    }
    if (s.GetType() == GeomAbs_Cylinder || s.GetType() == GeomAbs_Cone) {
      x.genre = s.GetType() == GeomAbs_Cylinder ? Genre::Cylindre : Genre::Cone;
      if (s.GetType() == GeomAbs_Cylinder) x.rayon = s.Cylinder().Radius();
      const gp_Ax1 a = s.GetType() == GeomAbs_Cylinder ? s.Cylinder().Axis() : s.Cone().Axis();
      x.p = a.Location();
      x.d = a.Direction();
      return x;
    }
  }
  return x;
}

// Face d'un composant : face de sa pièce, ou face d'un composant d'un sous-assemblage désignée par son chemin
// « @k/nom » (récursif : « @k/@j/nom »), exprimée dans le repère du composant (AS-16)
Ref referenceComposant(const ComposantAssemblage& c, const std::string& nom, const std::vector<ResultatPiece>& pieces, const std::vector<Assemblage>* sous, int profondeur = 0) {
  // Plans, axes et origine de l'assemblage (A2) : rendus dans le repère de l'assemblage, qui ne bouge pas
  if (referenceAssemblage(nom)) return referenceBase(nom.substr(std::string(PREFIXE_REFERENCE_ASSEMBLAGE).size()));
  // Repère propre d'un sous-assemblage (« base:XY » d'un composant qui en est un)
  if (c.assemblage >= 0 && nom.rfind("base:", 0) == 0) return referenceBase(nom.substr(5));
  if (c.assemblage < 0) {
    if (c.piece < 0 || c.piece >= static_cast<int>(pieces.size())) return {};
    return reference(pieces[static_cast<std::size_t>(c.piece)], nom);
  }
  if (!sous || profondeur > 8 || c.assemblage >= static_cast<int>(sous->size()) || nom.size() < 3 || nom[0] != '@') return {};
  const std::size_t barre = nom.find('/');
  if (barre == std::string::npos) return {};
  int k = -1;
  try {
    k = std::stoi(nom.substr(1, barre - 1));
  } catch (...) {
    return {};
  }
  const Assemblage& s = (*sous)[static_cast<std::size_t>(c.assemblage)];
  if (k < 0 || k >= static_cast<int>(s.composants.size()) || s.composants[static_cast<std::size_t>(k)].supprime) return {};
  const ComposantAssemblage& sc = s.composants[static_cast<std::size_t>(k)];
  Ref r = referenceComposant(sc, nom.substr(barre + 1), pieces, sous, profondeur + 1);
  if (r.genre == Genre::Aucun) return r;
  const gp_Trsf T = placementComposant(sc);
  r.p.Transform(T);
  r.d.Transform(T);
  return r;
}

std::string nombreLisible(double v) {
  if (std::abs(v) < 5e-4) v = 0;
  char b[32];
  std::snprintf(b, sizeof b, "%.3f", v);
  std::string s = b;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  std::replace(s.begin(), s.end(), '.', ',');
  return s == "-0" ? "0" : s;
}
std::string triplet(const gp_XYZ& v) { return "(" + nombreLisible(v.X()) + " ; " + nombreLisible(v.Y()) + " ; " + nombreLisible(v.Z()) + ")"; }

// ---------- Système des contraintes d'un assemblage ----------
// Inconnues : six par composant mobile, petits déplacements autour de l'état courant : translation (mm) et
// rotation autour des axes globaux passant par le centre du composant, mise à l'échelle de sa taille (mm aussi),
// pour que toutes les colonnes de la jacobienne aient la même unité.
class Systeme {
 public:
  struct Contrainte {
    Ref ra, rb;
    int c = 0;  // indice dans l'assemblage
    bool ga = false, gb = false;  // côté porté par l'assemblage lui-même (A2) : ne bouge pas
  };

  Systeme(const Assemblage& a, const std::vector<ResultatPiece>& pieces, const std::vector<Assemblage>* sous = nullptr) : m_a(a) {
    const int nc = static_cast<int>(a.composants.size());
    for (int ci = 0; ci < static_cast<int>(a.contraintes.size()); ++ci) {
      const auto& c = a.contraintes[static_cast<std::size_t>(ci)];
      if (c.suspendue) continue;
      auto ecarter = [&](const std::string& raison) { ecartees.push_back({ci, raison}); };
      if (c.a < 0 || c.b < 0 || c.a >= nc || c.b >= nc) { ecarter("un de ses composants n'existe plus"); continue; }
      // Type inconnu (fichier d'une version plus récente, faute de frappe) : jamais résolu comme une autre
      // contrainte (AS-28)
      if (!typeContrainteAssemblageConnu(c.type)) { ecarter("type « " + c.type + " » inconnu de cette version"); continue; }
      const auto& ca = a.composants[static_cast<std::size_t>(c.a)];
      const auto& cb = a.composants[static_cast<std::size_t>(c.b)];
      if (ca.supprime || cb.supprime) continue;  // composant suspendu : contrainte en attente
      const bool ga = referenceAssemblage(c.refA), gb = referenceAssemblage(c.refB);
      if (ga && gb) { ecarter("ses deux références sont celles de l'assemblage"); continue; }
      if (c.a == c.b && !ga && !gb) { ecarter("ses deux faces appartiennent au même composant"); continue; }
      if ((ca.assemblage < 0 && (ca.piece < 0 || ca.piece >= static_cast<int>(pieces.size()))) || (cb.assemblage < 0 && (cb.piece < 0 || cb.piece >= static_cast<int>(pieces.size())))) {
        ecarter("pièce manquante");
        continue;
      }
      // Un sous-assemblage est un corps rigide : ses faces se désignent par leur chemin « @k/face » (AS-16)
      Contrainte k{referenceComposant(ca, c.refA, pieces, sous), referenceComposant(cb, c.refB, pieces, sous), ci, ga, gb};
      if (k.ra.genre == Genre::Aucun || k.rb.genre == Genre::Aucun) {
        ecarter("référence « " + (k.ra.genre == Genre::Aucun ? c.refA : c.refB) + " » introuvable (supprimée, coupée en morceaux ou d'un type sans repère)");
        continue;
      }
      if (const std::string refus = facesAdmises(c.type, nomGenre(k.ra.genre), nomGenre(k.rb.genre)); !refus.empty()) { ecarter(refus); continue; }
      const bool liaison = c.type == "pivot" || c.type == "glissiere" || c.type == "vis";
      // Une liaison mémorise la rotation de ses deux composants : elle relie deux composants, pas l'assemblage
      if ((liaison || c.type == "engrenage" || c.type == "rotule") && (ga || gb)) { ecarter("une liaison relie deux composants ; vers l'assemblage, utilise une contrainte simple"); continue; }
      if ((liaison && c.parametres.size() < 7) || (c.type == "engrenage" && c.parametres.size() < 6)) { ecarter("liaison sans état de départ : recrée-la"); continue; }
      contraintes.push_back(k);
    }
    indice.assign(static_cast<std::size_t>(nc), -1);
    for (int i = 1; i < nc; ++i) {
      const auto& c = a.composants[static_cast<std::size_t>(i)];
      if (c.fixe || c.supprime) continue;
      indice[static_cast<std::size_t>(i)] = n;
      ordre.push_back(i);
      n += 6;
    }
    // Poses de départ, centres et tailles
    poses0.resize(static_cast<std::size_t>(nc));
    miroirs.resize(static_cast<std::size_t>(nc));
    centres.assign(static_cast<std::size_t>(nc), gp_Pnt());
    echelles.assign(static_cast<std::size_t>(nc), 1.0);
    for (int i = 0; i < nc; ++i) {
      const auto& c = a.composants[static_cast<std::size_t>(i)];
      poses0[static_cast<std::size_t>(i)] = rigideDe(c.x, c.y, c.z, c.rx, c.ry, c.rz);
      miroirs[static_cast<std::size_t>(i)] = miroirDe(c);
      if (c.assemblage < 0 && c.piece >= 0 && c.piece < static_cast<int>(pieces.size()) && !pieces[static_cast<std::size_t>(c.piece)].forme.IsNull()) {
        Bnd_Box b;
        BRepBndLib::Add(pieces[static_cast<std::size_t>(c.piece)].forme, b);
        if (!b.IsVoid()) {
          double x0, y0, z0, x1, y1, z1;
          b.Get(x0, y0, z0, x1, y1, z1);
          centres[static_cast<std::size_t>(i)] = gp_Pnt((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
          echelles[static_cast<std::size_t>(i)] = std::max(1.0, std::sqrt(b.SquareExtent()) / 2);
        }
      }
    }
  }

  int n = 0;
  std::vector<int> indice;  // composant → première inconnue, −1 s'il ne bouge pas
  std::vector<int> ordre;   // composants mobiles
  std::vector<Contrainte> contraintes;
  std::vector<std::pair<int, std::string>> ecartees;
  std::vector<gp_Trsf> poses0, miroirs;
  std::vector<gp_Pnt> centres;   // centre de chaque composant, dans le repère de sa pièce
  std::vector<double> echelles;  // demi-diagonale (mm) : échelle des colonnes de rotation

  gp_Trsf placement(int i, const std::vector<gp_Trsf>& P) const { return P[static_cast<std::size_t>(i)] * miroirs[static_cast<std::size_t>(i)]; }
  gp_Pnt centre(int i, const std::vector<gp_Trsf>& P) const { return centres[static_cast<std::size_t>(i)].Transformed(placement(i, P)); }

  // Petit déplacement d'un composant : translation u[0..2] (mm) puis rotation u[3..5] (× échelle) autour de son centre
  void avancer(std::vector<gp_Trsf>& P, int i, const double* u) const {
    const gp_Pnt C = centre(i, P);
    const double L = echelles[static_cast<std::size_t>(i)];
    const gp_Vec w(u[3] / L, u[4] / L, u[5] / L);
    gp_Trsf rot, tr;
    if (w.Magnitude() > 1e-300) rot.SetRotation(gp_Ax1(C, gp_Dir(w)), w.Magnitude());
    tr.SetTranslation(gp_Vec(u[0], u[1], u[2]));
    P[static_cast<std::size_t>(i)] = tr * rot * P[static_cast<std::size_t>(i)];
  }
  void avancer(std::vector<gp_Trsf>& P, const std::vector<double>& u) const {
    for (int i : ordre) avancer(P, i, u.data() + indice[static_cast<std::size_t>(i)]);
  }
  // Rotation finie d'un composant autour d'un axe passant par son centre
  void tourner(std::vector<gp_Trsf>& P, int i, const gp_Dir& axe, double angle) const {
    gp_Trsf rot;
    rot.SetRotation(gp_Ax1(centre(i, P), axe), angle);
    P[static_cast<std::size_t>(i)] = rot * P[static_cast<std::size_t>(i)];
  }

  // Composants dont dépend une contrainte : ses deux côtés mobiles, sauf celui que porte l'assemblage (A2)
  std::vector<int> composantsMobiles(const Contrainte& k, const ContrainteAssemblage& c) const {
    std::vector<int> r;
    if (!k.ga && indice[static_cast<std::size_t>(c.a)] >= 0) r.push_back(c.a);
    if (!k.gb && indice[static_cast<std::size_t>(c.b)] >= 0 && (r.empty() || r.front() != c.b)) r.push_back(c.b);
    return r;
  }

  // Directions et points de référence placés
  struct Places { gp_Pnt pa, pb; gp_Vec va, vb; gp_Dir da, db; };
  Places places(const Contrainte& k, const std::vector<gp_Trsf>& P) const {
    const ContrainteAssemblage& c = m_a.contraintes[static_cast<std::size_t>(k.c)];
    const gp_Trsf ta = k.ga ? gp_Trsf() : placement(c.a, P), tb = k.gb ? gp_Trsf() : placement(c.b, P);
    Places q;
    q.pa = k.ra.p.Transformed(ta);
    q.pb = k.rb.p.Transformed(tb);
    q.da = k.ra.d.Transformed(ta);
    q.db = k.rb.d.Transformed(tb);
    q.va = gp_Vec(q.da);
    q.vb = gp_Vec(q.db);
    return q;
  }
  // Rotation qu'un composant a faite depuis la création d'une liaison (angles d'Euler mémorisés à partir de prm)
  gp_Mat rotationDepuis(int i, const std::vector<gp_Trsf>& P, const double* euler0) const {
    const gp_Mat R = P[static_cast<std::size_t>(i)].VectorialPart();
    return R * rotationDe(euler0[0], euler0[1], euler0[2]).Transposed();
  }

  // Résidus d'une contrainte : nuls quand elle est satisfaite. « suivi » reçoit les angles (degrés) des liaisons.
  void residus(const Contrainte& k, const std::vector<gp_Trsf>& P, std::vector<double>& r, std::array<double, 2>* suivi = nullptr) const {
    const ContrainteAssemblage& c = m_a.contraintes[static_cast<std::size_t>(k.c)];
    const Places q = places(k, P);
    const gp_Vec& va = q.va;
    const gp_Vec& vb = q.vb;
    const std::string& t = c.type;
    // Directions parallèles, de même sens ou opposées (la plus proche) : résidu vectoriel sans point selle à angle droit
    auto paralleles = [&] {
      // Sens imposé (A1) : même sens ou opposé ; sinon le plus proche
      const double sens = c.alignement == 1 ? 1.0 : c.alignement == 2 ? -1.0 : (va.Dot(vb) >= 0 ? 1.0 : -1.0);
      const gp_Vec s = vb - va * sens;
      r.insert(r.end(), {s.X(), s.Y(), s.Z()});
    };
    auto axesConfondus = [&] {
      paralleles();
      const gp_Vec w(q.pa, q.pb);
      const gp_Vec perp = w - va * w.Dot(va);
      r.insert(r.end(), {perp.X(), perp.Y(), perp.Z()});
    };
    if (t == "coincidence" && k.ra.plan() && k.rb.plan()) {
      // Faces planes face à face (alignées si « inverse »), à « valeur » mm l'une de l'autre
      const gp_Vec s = c.inverse ? vb - va : vb + va;
      r.insert(r.end(), {s.X(), s.Y(), s.Z(), gp_Vec(q.pa, q.pb).Dot(va) - c.valeur});
    } else if (t == "coincidence") {
      // A2 : points, droites (arêtes, axes) et plans mêlés
      const bool ptA = k.ra.point(), ptB = k.rb.point();
      if (ptA && ptB) {
        // Deux points confondus
        r.insert(r.end(), {q.pb.X() - q.pa.X(), q.pb.Y() - q.pa.Y(), q.pb.Z() - q.pa.Z()});
      } else if (ptA || ptB) {
        const gp_Pnt pt = ptA ? q.pa : q.pb, po = ptA ? q.pb : q.pa;
        const gp_Vec vo = ptA ? vb : va;
        const gp_Vec w(po, pt);
        if ((ptA ? k.rb : k.ra).plan()) {
          r.push_back(w.Dot(vo) - c.valeur);  // point sur le plan, ou à « valeur » mm de lui (côté de la normale)
        } else {
          const gp_Vec perp = w - vo * w.Dot(vo);  // point sur la droite
          r.insert(r.end(), {perp.X(), perp.Y(), perp.Z()});
        }
      } else if (k.ra.axe() && k.rb.axe()) {
        axesConfondus();  // droites confondues
      } else {
        // Une droite dans un plan (ou parallèle à lui, à « valeur » mm)
        const bool planA = k.ra.plan();
        const gp_Vec n = planA ? va : vb, d = planA ? vb : va;
        const gp_Pnt pp = planA ? q.pa : q.pb, pd = planA ? q.pb : q.pa;
        r.push_back(d.Dot(n));
        r.push_back(gp_Vec(pp, pd).Dot(n) - c.valeur);
      }
    } else if (t == "parallele") {
      paralleles();
    } else if (t == "perpendiculaire") {
      r.push_back(va.Dot(vb));
    } else if (t == "angle" && c.alignement != 0 && c.parametres.size() >= 3) {
      // Angle orienté (A1) : signé autour de l'axe mémorisé dans le repère du composant a ; « 2 » le sens inverse
      const gp_Trsf ta = k.ga ? gp_Trsf() : placement(c.a, P);
      gp_Vec axe(c.parametres[0], c.parametres[1], c.parametres[2]);
      axe.Transform(ta);
      if (axe.Magnitude() < 1e-12) axe = gp_Vec(0, 0, 1);
      axe.Normalize();
      const double signe = std::atan2(va.Crossed(vb).Dot(axe), va.Dot(vb));
      const double cible = (c.alignement == 2 ? -1 : 1) * c.valeur / DEG;
      double e = signe - cible;
      while (e > std::numbers::pi) e -= 2 * std::numbers::pi;
      while (e <= -std::numbers::pi) e += 2 * std::numbers::pi;
      r.push_back(e);  // un seul degré de liberté retiré, comme l'angle non orienté
    } else if (t == "angle") {
      // Angle entre les deux directions, en radians (AS-24) : à 0° et 180°, résidu vectoriel (deux équations, rang
      // plein) ; ailleurs, atan2 dont la dérivée ne s'annule pas à la solution
      const double cible = std::acos(std::clamp(std::cos(c.valeur / DEG), -1.0, 1.0));
      if (cible < 1e-9) {
        const gp_Vec s = vb - va;
        r.insert(r.end(), {s.X(), s.Y(), s.Z()});
      } else if (std::numbers::pi - cible < 1e-9) {
        const gp_Vec s = vb + va;
        r.insert(r.end(), {s.X(), s.Y(), s.Z()});
      } else {
        r.push_back(std::atan2(va.Crossed(vb).Magnitude(), va.Dot(vb)) - cible);
      }
    } else if (t == "concentrique") {
      axesConfondus();
    } else if (t == "tangente") {
      // Face plane tangente à un cylindre (axe parallèle au plan, à un rayon du plan) ou à une sphère, du côté
      // extérieur de la face (normale sortante) ; « inverse » : de l'autre côté (AS-30)
      const bool planA = k.ra.plan();
      const gp_Pnt pp = planA ? q.pa : q.pb, pc = planA ? q.pb : q.pa;
      const gp_Vec nn = planA ? va : vb, axe = planA ? vb : va;
      const Ref& autre = planA ? k.rb : k.ra;
      if (autre.genre == Genre::Cylindre) r.push_back(axe.Dot(nn));
      r.push_back(gp_Vec(pp, pc).Dot(nn) - (c.inverse ? -autre.rayon : autre.rayon));
    } else if (t == "rotule") {
      r.insert(r.end(), {q.pb.X() - q.pa.X(), q.pb.Y() - q.pa.Y(), q.pb.Z() - q.pa.Z()});
    } else if (t == "engrenage") {
      // Chaque roue tourne autour de son axe ; angles mesurés autour d'une même direction (celle de A) :
      // zA·θA + zB·θB = 0, soit θA + rapport·θB = 0 avec rapport = zB / zA (négatif : couronne intérieure), AS-10
      const auto& prm = c.parametres;
      const gp_Mat DA = rotationDepuis(c.a, P, prm.data()), DB = rotationDepuis(c.b, P, prm.data() + 3);
      const double thA = derouler(angleAutourAxe(DA, gp_Dir(DA.Transposed() * q.da.XYZ())), c.angleSuivi[0] / DEG);
      const double thB = derouler(angleAutourAxe(DB, gp_Dir(DB.Transposed() * q.db.XYZ())), c.angleSuivi[1] / DEG);
      const double sens = va.Dot(vb) >= 0 ? 1.0 : -1.0;
      const double rapport = c.valeur == 0 ? 1.0 : c.valeur;
      r.push_back(thA + rapport * sens * thB);
      if (suivi) *suivi = {thA * DEG, thB * DEG};
    } else {
      // Pivot, glissière, vis : axes confondus, position axiale et rotation relative autour de l'axe
      const auto& prm = c.parametres;
      axesConfondus();
      const double axial = gp_Vec(q.pa, q.pb).Dot(va);
      const gp_Mat DA = rotationDepuis(c.a, P, prm.data()), DB = rotationDepuis(c.b, P, prm.data() + 3);
      const gp_Mat Q = DA.Transposed() * DB;  // rotation de B vue depuis A
      const double pas = t == "vis" ? (prm.size() >= 8 ? prm[7] : 1.0) : 0.0;
      // Tour déroulé près de la valeur imposée, sinon près de la dernière position (AS-06)
      double estime = c.angleSuivi[0] / DEG;
      if (c.pilotee && t != "glissiere") estime = c.valeur / DEG;
      else if (t == "vis" && std::abs(pas) > 1e-12) estime = (axial - prm[6]) / pas * 2 * std::numbers::pi;
      else if (t == "glissiere") estime = 0;
      const double theta = derouler(angleAutourAxe(Q, gp_Dir(DA.Transposed() * q.da.XYZ())), estime);
      if (t == "pivot") {
        r.push_back(axial - prm[6]);  // pas de translation le long de l'axe
        if (c.pilotee) r.push_back(theta - c.valeur / DEG);
      } else if (t == "glissiere") {
        r.push_back(theta);  // pas de rotation autour de l'axe
        if (c.pilotee) r.push_back(axial - prm[6] - c.valeur);
      } else {
        r.push_back(axial - prm[6] - pas * theta / (2 * std::numbers::pi));  // la course suit la rotation
        if (c.pilotee) r.push_back(theta - c.valeur / DEG);
      }
      if (suivi) *suivi = {theta * DEG, 0};
    }
  }

  std::vector<double> residus(const std::vector<gp_Trsf>& P, std::vector<int>* origine = nullptr) const {
    std::vector<double> r;
    if (origine) origine->clear();
    for (const auto& k : contraintes) {
      const std::size_t avant = r.size();
      residus(k, P, r);
      if (origine) origine->insert(origine->end(), r.size() - avant, k.c);
    }
    return r;
  }

  // Jacobienne creuse par différences centrées : seules les inconnues des deux composants d'une contrainte comptent
  numerique::MatriceCreuse jacobienne(const std::vector<gp_Trsf>& P0) const {
    constexpr double h = 1e-6;
    numerique::MatriceCreuse J;
    std::vector<gp_Trsf> P = P0;
    std::vector<double> rp, rm;
    for (const auto& k : contraintes) {
      const ContrainteAssemblage& c = m_a.contraintes[static_cast<std::size_t>(k.c)];
      rp.clear();
      residus(k, P, rp);
      const std::size_t base = J.size();
      J.resize(base + rp.size());
      std::vector<int> comps;
      for (int comp : composantsMobiles(k, c)) if (std::find(comps.begin(), comps.end(), comp) == comps.end()) comps.push_back(comp);
      std::sort(comps.begin(), comps.end(), [&](int x, int y) { return indice[static_cast<std::size_t>(x)] < indice[static_cast<std::size_t>(y)]; });
      for (int comp : comps)
        for (int d = 0; d < 6; ++d) {
          double u[6] = {0, 0, 0, 0, 0, 0};
          const gp_Trsf garde = P[static_cast<std::size_t>(comp)];
          u[d] = h;
          avancer(P, comp, u);
          rp.clear();
          residus(k, P, rp);
          P[static_cast<std::size_t>(comp)] = garde;
          u[d] = -h;
          avancer(P, comp, u);
          rm.clear();
          residus(k, P, rm);
          P[static_cast<std::size_t>(comp)] = garde;
          for (std::size_t i = 0; i < rp.size() && i < rm.size(); ++i) J[base + i].emplace_back(indice[static_cast<std::size_t>(comp)] + d, (rp[i] - rm[i]) / (2 * h));
        }
    }
    return J;
  }

  // Orientations de départ où le gradient d'une contrainte d'orientation est nul alors qu'elle n'est pas satisfaite
  // (faces perpendiculaires qu'on veut parallèles, normales de même sens qu'on veut opposées…) : le composant mobile
  // est tourné de la rotation qui la satisfait, avant les moindres carrés (AS-04).
  void prealigner(std::vector<gp_Trsf>& P) const {
    for (const auto& k : contraintes) {
      const ContrainteAssemblage& c = m_a.contraintes[static_cast<std::size_t>(k.c)];
      // Côté tourné : b s'il est mobile et n'est pas l'assemblage, sinon a
      const bool coteB = !k.gb && indice[static_cast<std::size_t>(c.b)] >= 0;
      const bool coteA = !coteB && !k.ga && indice[static_cast<std::size_t>(c.a)] >= 0;
      if (!coteA && !coteB) continue;
      const int m = coteB ? c.b : c.a;
      // Un point n'a pas de direction : rien à pré-aligner
      if (k.ra.point() || k.rb.point()) continue;
      const Places q = places(k, P);
      const gp_Vec vm = coteB ? q.vb : q.va, vo = coteB ? q.va : q.vb;
      const double cosinus = vm.Dot(vo), sinus = vm.Crossed(vo).Magnitude();
      const gp_Dir dm(vm);
      auto versAutre = [&](double angle) {  // tourner vm vers vo d'un angle donné
        const gp_Vec axe = vm.Crossed(vo);
        tourner(P, m, axe.Magnitude() > 1e-9 ? gp_Dir(axe) : perpendiculaire(dm), angle);
      };
      const std::string& t = c.type;
      if (t == "coincidence" && k.ra.plan() && k.rb.plan()) {
        const double voulu = c.inverse ? 1 : -1;  // cosinus attendu
        if (cosinus * voulu < -1 + 1e-6) tourner(P, m, perpendiculaire(dm), std::numbers::pi);
      } else if (t == "coincidence" && k.ra.axe() && k.rb.axe()) {
        if (std::abs(cosinus) < 1e-6) versAutre(std::numbers::pi / 2);  // droites confondues
      } else if (t == "coincidence") {
        if (std::abs(cosinus) > 1 - 1e-6) tourner(P, m, perpendiculaire(dm), std::numbers::pi / 2);  // droite dans un plan
      } else if (t == "parallele" || t == "concentrique" || t == "pivot" || t == "glissiere" || t == "vis") {
        if (std::abs(cosinus) < 1e-6) versAutre(std::numbers::pi / 2);
      } else if (t == "perpendiculaire" || (t == "tangente" && (k.ra.genre == Genre::Cylindre || k.rb.genre == Genre::Cylindre))) {
        if (std::abs(cosinus) > 1 - 1e-6) tourner(P, m, perpendiculaire(dm), std::numbers::pi / 2);
      } else if (t == "angle") {
        const double cible = std::acos(std::clamp(std::cos(c.valeur / DEG), -1.0, 1.0));
        if (cible < 1e-9 || std::numbers::pi - cible < 1e-9) {
          if (std::abs(cosinus) < 1e-6) versAutre(std::numbers::pi / 2);
        } else if (sinus < 1e-6) {
          const double actuel = cosinus > 0 ? 0 : std::numbers::pi;
          tourner(P, m, perpendiculaire(dm), actuel - cible);
        }
      }
    }
  }

 private:
  const Assemblage& m_a;
};

double norme2(const std::vector<double>& v) {
  double s = 0;
  for (double q : v) s += q * q;
  return s;
}

// Levenberg-Marquardt sur les poses : pas résolu par Cholesky à profil (numérotation de Cuthill-McKee inverse)
double moindresCarres(const Systeme& S, std::vector<gp_Trsf>& P, int iterations) {
  std::vector<double> r = S.residus(P);
  double f = norme2(r), lambda = 1e-3;
  numerique::CholeskyProfil chol;
  bool structure = false;
  std::vector<double> diag, g, ajout(static_cast<std::size_t>(S.n)), pas;
  for (int it = 0; it < iterations && f > 1e-24; ++it) {
    const numerique::MatriceCreuse J = S.jacobienne(P);
    if (!structure) { chol.structure(J, S.n); structure = true; }
    chol.assemblerNormale(J, diag);
    g.assign(static_cast<std::size_t>(S.n), 0.0);
    for (std::size_t i = 0; i < J.size() && i < r.size(); ++i)
      for (const auto& [j, v] : J[i]) g[static_cast<std::size_t>(j)] -= v * r[i];
    bool ameliore = false;
    for (int essai = 0; essai < 14 && !ameliore; ++essai) {
      for (int j = 0; j < S.n; ++j) ajout[static_cast<std::size_t>(j)] = lambda * (1 + diag[static_cast<std::size_t>(j)]) + 1e-14;
      if (!chol.factoriser(ajout)) { lambda *= 10; continue; }
      chol.resoudre(g, pas);
      std::vector<gp_Trsf> Pn = P;
      S.avancer(Pn, pas);
      std::vector<double> rn = S.residus(Pn);
      const double fn = norme2(rn);
      if (fn < f) {
        const double gain = f - fn;
        P = std::move(Pn);
        r = std::move(rn);
        f = fn;
        lambda = std::max(1e-12, lambda / 5);
        ameliore = true;
        if (gain < 1e-28) it = iterations;
      } else {
        lambda *= 8;
      }
    }
    if (!ameliore) break;
  }
  return f;
}

// Contraintes violées (un résidu au-delà de la tolérance)
std::vector<int> violees(const Systeme& S, const std::vector<gp_Trsf>& P) {
  std::vector<int> origine;
  const std::vector<double> r = S.residus(P, &origine);
  std::vector<int> v;
  for (std::size_t q = 0; q < r.size(); ++q)
    if (std::abs(r[q]) > 1e-5 && std::find(v.begin(), v.end(), origine[q]) == v.end()) v.push_back(origine[q]);
  return v;
}
}  // namespace

std::string facesAdmises(const std::string& type, const std::string& a, const std::string& b) {
  auto plan = [](const std::string& g) { return g == "plan"; };
  auto revolution = [](const std::string& g) { return g == "cylindre" || g == "cone"; };
  // Droite portée (A2) : axe d'une face de révolution, arête droite ou circulaire, axe de base ou de référence
  auto axe = [&](const std::string& g) { return revolution(g) || g == "droite" || g == "cercle"; };
  auto arete = [](const std::string& g) { return g == "droite" || g == "cercle" || g == "point"; };
  auto direction = [&](const std::string& g) { return plan(g) || axe(g); };
  if (type == "coincidence") {
    if (plan(a) && plan(b)) return {};
    // Arêtes, sommets, axes et points (A2) : point sur point, droite ou plan ; droite sur droite ou dans un plan
    const bool admis = (arete(a) || arete(b)) && (plan(a) || axe(a) || a == "point") && (plan(b) || axe(b) || b == "point");
    return admis ? std::string() : "« Coïncidente » relie deux plans, ou une arête, un sommet ou un axe à un plan, une droite ou un point";
  }
  if (type == "parallele" || type == "perpendiculaire" || type == "angle")
    return direction(a) && direction(b) ? std::string() : "cette contrainte relie des plans, des arêtes droites ou des axes (cylindre, cône), pas une sphère ni un point";
  if (type == "concentrique")
    return axe(a) && axe(b) ? std::string() : "« Concentrique » relie deux axes (faces de révolution, arêtes circulaires, axes)";
  if (type == "pivot" || type == "glissiere" || type == "vis" || type == "engrenage")
    return revolution(a) && revolution(b) ? std::string() : "cette contrainte relie deux faces de révolution d'axe défini (cylindre, cône)";
  if (type == "rotule") return a == "sphere" && b == "sphere" ? std::string() : "une rotule relie deux faces sphériques";
  if (type == "tangente")
    return (plan(a) && (b == "cylindre" || b == "sphere")) || (plan(b) && (a == "cylindre" || a == "sphere")) ? std::string()
                                                                                                             : "« Tangente » relie une face plane et un cylindre ou une sphère";
  return "type de contrainte inconnu";
}

std::string genreFace(const ResultatPiece& piece, const std::string& nom) { return nomGenre(reference(piece, nom).genre); }
std::string genreFaceComposant(const ComposantAssemblage& c, const std::string& nom, const std::vector<ResultatPiece>& pieces, const std::vector<Assemblage>* sous) {
  return nomGenre(referenceComposant(c, nom, pieces, sous).genre);
}

gp_Trsf placementComposant(const ComposantAssemblage& c) { return rigideDe(c.x, c.y, c.z, c.rx, c.ry, c.rz) * miroirDe(c); }

ComposantAssemblage composantDepuisPlacement(const gp_Trsf& t) {
  ComposantAssemblage c;
  gp_Trsf rigide = t;
  // Une transformation qui retourne l'espace (déterminant négatif) : T = R · S_YZ, d'où R = T · S_YZ
  if (t.IsNegative()) {
    c.miroir = 3;
    gp_Trsf s;
    s.SetMirror(gp::YOZ());
    rigide = t * s;
  }
  const gp_XYZ o = rigide.TranslationPart();
  c.x = o.X(); c.y = o.Y(); c.z = o.Z();
  const auto a = eulerDepuis(rigide.VectorialPart(), {0, 0, 0});
  c.rx = a[0]; c.ry = a[1]; c.rz = a[2];
  for (double* v : {&c.x, &c.y, &c.z, &c.rx, &c.ry, &c.rz}) if (std::abs(*v) < 1e-9) *v = 0;
  return c;
}

void pivoterAutourAxeGlobal(ComposantAssemblage& c, int axe, double degres) {
  gp_Trsf rot;
  rot.SetRotation(axe == 0 ? gp::OX() : axe == 1 ? gp::OY() : gp::OZ(), degres / DEG);
  const gp_Mat R = rot.VectorialPart() * rotationDe(c.rx, c.ry, c.rz);
  const auto a = eulerDepuis(R, {c.rx, c.ry, c.rz});
  c.rx = a[0]; c.ry = a[1]; c.rz = a[2];
}

void pivoterAutour(ComposantAssemblage& c, const gp_Ax1& axe, double degres) {
  gp_Trsf rot;
  rot.SetRotation(axe, degres / DEG);
  const gp_Trsf T = rot * rigideDe(c.x, c.y, c.z, c.rx, c.ry, c.rz);
  const gp_XYZ t = T.TranslationPart();
  const auto a = eulerDepuis(T.VectorialPart(), {c.rx, c.ry, c.rz});
  c.x = t.X(); c.y = t.Y(); c.z = t.Z();
  c.rx = a[0]; c.ry = a[1]; c.rz = a[2];
}

ComposantAssemblage composantSymetrique(const ComposantAssemblage& c, int plan) {
  ComposantAssemblage s = c;
  if (plan < 1 || plan > 3) return s;
  // S (symétrie globale) · t · R · M = t' · R' · M'  avec t' = S(t) ; si M est l'identité, M' = la symétrie locale de
  // même plan et R' = S·R·M' ; si M est déjà une symétrie, M' = identité et R' = S·R·M (deux symétries : une rotation)
  gp_Trsf S;
  S.SetMirror(plan == 1 ? gp::XOY() : plan == 2 ? gp::ZOX() : gp::YOZ());
  const gp_Mat Sm = S.VectorialPart();
  const gp_XYZ t = Sm * gp_XYZ(c.x, c.y, c.z);
  ComposantAssemblage sansRot = c;
  sansRot.x = sansRot.y = sansRot.z = 0;
  sansRot.rx = sansRot.ry = sansRot.rz = 0;
  const gp_Mat M = miroirDe(sansRot).VectorialPart();
  gp_Mat Rp;
  if (c.miroir == 0) {
    s.miroir = plan;
    ComposantAssemblage local = sansRot;
    local.miroir = plan;
    Rp = Sm * rotationDe(c.rx, c.ry, c.rz) * miroirDe(local).VectorialPart();
  } else {
    s.miroir = 0;
    Rp = Sm * rotationDe(c.rx, c.ry, c.rz) * M;
  }
  const auto a = eulerDepuis(Rp, {c.rx, c.ry, c.rz});
  s.x = t.X(); s.y = t.Y(); s.z = t.Z();
  s.rx = a[0]; s.ry = a[1]; s.rz = a[2];
  s.fixe = false;
  return s;
}

std::optional<std::vector<ComposantAssemblage>> deplacerDansContraintes(const Assemblage& a, int k, const ComposantAssemblage& cible, const std::vector<ResultatPiece>& pieces,
                                                                         const std::vector<Assemblage>* sous) {
  const int nc = static_cast<int>(a.composants.size());
  if (k < 0 || k >= nc) return std::nullopt;
  Assemblage A = a;
  A.composants[static_cast<std::size_t>(k)] = cible;
  auto actif = [](const ContrainteAssemblage& c) { return !c.suspendue; };
  auto touche = [k](const ContrainteAssemblage& c) { return c.a == k || c.b == k; };
  if (std::none_of(a.contraintes.begin(), a.contraintes.end(), [&](const auto& c) { return actif(c) && touche(c); })) return A.composants;
  auto tenir = [&](Assemblage x, const ComposantAssemblage& pose) -> std::optional<std::vector<ComposantAssemblage>> {
    x.composants[static_cast<std::size_t>(k)] = pose;
    x.composants[static_cast<std::size_t>(k)].fixe = true;
    if (!resoudreAssemblage(x, pieces, sous).resolu) return std::nullopt;
    x.composants[static_cast<std::size_t>(k)].fixe = pose.fixe;
    return x.composants;
  };
  // 1. Tenu à la cible : les composants liés suivent (mécanisme)
  if (auto r = tenir(A, cible)) return r;
  // 2. Ramené sur ses contraintes envers ce qui ne bouge pas : les autres composants figés, ses contraintes vers un
  //    composant mobile mises de côté (celui-là suivra), celles qui ne le concernent pas aussi
  Assemblage P = A;
  for (int i = 0; i < nc; ++i)
    if (i != k) P.composants[static_cast<std::size_t>(i)].fixe = true;
  for (auto& c : P.contraintes) {
    if (!touche(c)) { c.suspendue = true; continue; }
    const bool versAssemblage = referenceAssemblage(c.refA) || referenceAssemblage(c.refB);
    const int autre = c.a == k ? c.b : c.a;
    const bool autreImmobile = versAssemblage || autre == 0 || (autre >= 0 && autre < nc && a.composants[static_cast<std::size_t>(autre)].fixe);
    if (!autreImmobile) c.suspendue = true;
  }
  ComposantAssemblage projete = cible;
  if (resoudreAssemblage(P, pieces, sous).resolu) projete = P.composants[static_cast<std::size_t>(k)];
  projete.fixe = cible.fixe;
  // 3. Tenu là : les autres suivent
  if (auto r = tenir(A, projete)) return r;
  // 4. Tout se résout depuis là
  Assemblage D = a;
  D.composants[static_cast<std::size_t>(k)] = projete;
  if (resoudreAssemblage(D, pieces, sous).resolu) return D.composants;
  return std::nullopt;
}

std::vector<int> ordreAssemblages(const std::vector<Assemblage>& assemblages, int racine) {
  const int n = static_cast<int>(assemblages.size());
  std::vector<int> ordre;
  std::vector<char> etat(static_cast<std::size_t>(n), 0);  // 0 non vu, 1 en cours, 2 fait
  std::function<void(int, int)> visiter = [&](int i, int profondeur) {
    if (i < 0 || i >= n || etat[static_cast<std::size_t>(i)] != 0 || profondeur > PROFONDEUR_ASSEMBLAGE_MAX) return;
    etat[static_cast<std::size_t>(i)] = 1;
    for (const auto& c : assemblages[static_cast<std::size_t>(i)].composants)
      if (c.assemblage >= 0 && !c.supprime) visiter(c.assemblage, profondeur + 1);  // une boucle (état 1) est ignorée
    etat[static_cast<std::size_t>(i)] = 2;
    ordre.push_back(i);
  };
  if (racine >= 0) visiter(racine, 0);
  else for (int i = 0; i < n; ++i) visiter(i, 0);
  return ordre;
}

std::vector<MiseAJourAssemblage> mettreAJourAssemblages(std::vector<Assemblage>& assemblages, const std::vector<ResultatPiece>& pieces, int racine) {
  std::vector<MiseAJourAssemblage> r;
  for (int i : ordreAssemblages(assemblages, racine)) {
    // Copie : les contraintes d'un parent lisent les sous-assemblages déjà mis à jour, dans « assemblages »
    Assemblage a = assemblages[static_cast<std::size_t>(i)];
    MiseAJourAssemblage m;
    m.assemblage = i;
    m.etat = resoudreAssemblage(a, pieces, &assemblages);
    const Assemblage& avant = assemblages[static_cast<std::size_t>(i)];
    for (std::size_t k = 0; k < a.composants.size() && k < avant.composants.size(); ++k) {
      const auto& x = a.composants[k];
      const auto& y = avant.composants[k];
      if (std::abs(x.x - y.x) + std::abs(x.y - y.y) + std::abs(x.z - y.z) + std::abs(x.rx - y.rx) + std::abs(x.ry - y.ry) + std::abs(x.rz - y.rz) > 1e-7) m.deplace = true;
    }
    for (std::size_t k = 0; k < a.contraintes.size() && k < avant.contraintes.size(); ++k)
      if (std::abs(a.contraintes[k].angleSuivi[0] - avant.contraintes[k].angleSuivi[0]) + std::abs(a.contraintes[k].angleSuivi[1] - avant.contraintes[k].angleSuivi[1]) > 1e-7) m.deplace = true;
    if (m.deplace) assemblages[static_cast<std::size_t>(i)] = std::move(a);
    r.push_back(std::move(m));
  }
  return r;
}

bool facesCompatibles(const ResultatPiece& ancienne, const ResultatPiece& nouvelle, const std::string& nom, std::string* raison) {
  auto dire = [&](const std::string& m) { if (raison) *raison = m; return false; };
  const Ref a = reference(ancienne, nom), b = reference(nouvelle, nom);
  if (b.genre == Genre::Aucun) return dire("la face « " + nom + " » n'existe pas dans la nouvelle pièce");
  if (a.genre == Genre::Aucun) return dire("la face « " + nom + " » n'existait plus dans l'ancienne pièce");
  if (a.genre != b.genre) return dire(std::string("« ") + nom + " » est un " + nomGenre(a.genre) + " dans l'ancienne pièce et un " + nomGenre(b.genre) + " dans la nouvelle");
  if (std::abs(a.rayon - b.rayon) > 1e-6) return dire("« " + nom + " » n'a pas le même rayon (" + nombreLisible(a.rayon) + " mm → " + nombreLisible(b.rayon) + " mm)");
  if (a.genre != Genre::Sphere && std::abs(std::abs(gp_Vec(a.d).Dot(gp_Vec(b.d))) - 1) > 1e-6)
    return dire("« " + nom + " » n'a pas la même orientation dans la pièce");
  if (a.genre == Genre::Plan && gp_Vec(a.d).Dot(gp_Vec(b.d)) < 0) return dire("« " + nom + " » regarde dans l'autre sens");
  return true;
}

EtatAssemblage resoudreAssemblage(Assemblage& a, const std::vector<ResultatPiece>& pieces, const std::vector<Assemblage>* sous) {
  EtatAssemblage e;
  if (a.composants.empty() || a.contraintes.empty()) {
    e.resolu = true;
    return e;
  }
  const Systeme S(a, pieces, sous);
  e.problemes = S.ecartees;
  std::vector<gp_Trsf> P = S.poses0;
  double f = norme2(S.residus(P));
  if (S.n > 0) {
    S.prealigner(P);
    f = moindresCarres(S, P, 200);
    // Bloqué ailleurs qu'à la solution (autre point selle) : petite rotation déterministe des composants des
    // contraintes violées, puis nouvel essai ; on garde le meilleur
    for (int essai = 0; essai < 2 && f > 1e-20; ++essai) {
      std::vector<gp_Trsf> Q = P;
      for (int ci : violees(S, P)) {
        const auto& c = a.contraintes[static_cast<std::size_t>(ci)];
        for (int comp : {c.b, c.a})
          if (S.indice[static_cast<std::size_t>(comp)] >= 0) {
            S.tourner(Q, comp, gp_Dir(1, 2 + essai, 3), (3.0 + 7 * essai) / DEG);
            break;
          }
      }
      const double fq = moindresCarres(S, Q, 200);
      if (fq < f) { P = std::move(Q); f = fq; }
    }
  }
  const bool satisfait = f < 1e-10;
  if (satisfait) {
    // Solution : poses réécrites en position et angles d'Euler (au plus près des précédents), angles des liaisons suivis
    for (int i : S.ordre) {
      auto& c = a.composants[static_cast<std::size_t>(i)];
      const gp_XYZ t = P[static_cast<std::size_t>(i)].TranslationPart();
      const auto ang = eulerDepuis(P[static_cast<std::size_t>(i)].VectorialPart(), {c.rx, c.ry, c.rz});
      c.x = t.X(); c.y = t.Y(); c.z = t.Z();
      c.rx = ang[0]; c.ry = ang[1]; c.rz = ang[2];
    }
    for (const auto& k : S.contraintes) {
      auto& c = a.contraintes[static_cast<std::size_t>(k.c)];
      if (c.type != "pivot" && c.type != "glissiere" && c.type != "vis" && c.type != "engrenage") continue;
      std::array<double, 2> suivi{c.angleSuivi};
      std::vector<double> r;
      S.residus(k, P, r, &suivi);
      c.angleSuivi = suivi;
    }
  } else {
    // Insoluble : rien n'est écrit, chaque composant garde sa dernière position valide (AS-20)
    for (int ci : violees(S, P)) e.problemes.push_back({ci, "ne peut pas être satisfaite avec les autres"});
  }
  e.contraintesNonSatisfaites = static_cast<int>(e.problemes.size());
  e.resolu = satisfait && S.ecartees.empty();
  if (!e.resolu) {
    if (!S.ecartees.empty()) e.message = "Contrainte écartée : " + S.ecartees.front().second + ".";
    else if (S.n == 0) e.message = "Tous les composants sont fixes, et une contrainte n'est pas satisfaite.";
    else e.message = "Contraintes incompatibles : elles ne peuvent pas être toutes satisfaites. Rien n'a été déplacé.";
  }
  return e;
}

// Analyse des libertés : noyau de la jacobienne des contraintes à la position actuelle (élimination creuse). Pour
// chaque composant, les mouvements encore possibles (projection du noyau sur ses six inconnues) : leur nombre, les
// translations et les directions de rotation libres, et chacun décrit comme un torseur dans le repère de l'assemblage.
AnalyseAssemblage analyserAssemblage(const Assemblage& a, const std::vector<ResultatPiece>& pieces, const std::vector<Assemblage>* sous) {
  AnalyseAssemblage an;
  const int nc = static_cast<int>(a.composants.size());
  an.libres.assign(static_cast<std::size_t>(nc), {false, false, false, false, false, false});
  an.libertes.assign(static_cast<std::size_t>(nc), 0);
  an.mouvements.assign(static_cast<std::size_t>(nc), {});
  const Systeme S(a, pieces, sous);
  for (const auto& [ci, raison] : S.ecartees) an.nonSatisfaites.push_back(ci);
  // Contraintes violées, même quand tout est fixe (AS-25)
  for (int ci : violees(S, S.poses0))
    if (std::find(an.nonSatisfaites.begin(), an.nonSatisfaites.end(), ci) == an.nonSatisfaites.end()) an.nonSatisfaites.push_back(ci);
  if (S.n == 0) return an;
  const numerique::AnalyseRang rang = numerique::analyserRang(S.jacobienne(S.poses0), S.n, true, 1e-7);
  an.degresLiberte = S.n - rang.rang;
  // Base orthonormée du noyau
  auto produit = [](const std::vector<double>& x, const std::vector<double>& y) { double s = 0; for (std::size_t i = 0; i < x.size(); ++i) s += x[i] * y[i]; return s; };
  std::vector<std::vector<double>> base;
  for (auto v : rang.noyau) {
    for (int passe = 0; passe < 2; ++passe)
      for (const auto& q : base) { const double p = produit(v, q); for (std::size_t i = 0; i < v.size(); ++i) v[i] -= p * q[i]; }
    const double l = std::sqrt(produit(v, v));
    if (l < 1e-10) continue;
    for (double& c : v) c /= l;
    base.push_back(std::move(v));
  }
  auto orthonormer = [&](std::vector<std::vector<double>> l, double tol) {
    std::vector<std::vector<double>> r;
    for (auto v : l) {
      for (int passe = 0; passe < 2; ++passe)
        for (const auto& q : r) { const double p = produit(v, q); for (std::size_t i = 0; i < v.size(); ++i) v[i] -= p * q[i]; }
      const double n2 = std::sqrt(produit(v, v));
      if (n2 < tol) continue;
      for (double& c : v) c /= n2;
      r.push_back(std::move(v));
    }
    return r;
  };
  for (int ic : S.ordre) {
    const std::size_t b = static_cast<std::size_t>(S.indice[static_cast<std::size_t>(ic)]);
    std::vector<std::vector<double>> restreints;
    for (const auto& q : base) restreints.push_back({q[b], q[b + 1], q[b + 2], q[b + 3], q[b + 4], q[b + 5]});
    const auto mouvements = orthonormer(restreints, 1e-6);
    an.libertes[static_cast<std::size_t>(ic)] = static_cast<int>(mouvements.size());
    // Translations pures et directions de rotation
    for (int d = 0; d < 3; ++d) {
      double part = 0;
      for (const auto& m : mouvements) part += m[static_cast<std::size_t>(d)] * m[static_cast<std::size_t>(d)];
      an.libres[static_cast<std::size_t>(ic)][static_cast<std::size_t>(d)] = part > 0.81;
    }
    std::vector<std::vector<double>> rotations;
    for (const auto& m : mouvements) rotations.push_back({m[3], m[4], m[5]});
    const auto axes = orthonormer(rotations, 1e-6);
    for (int d = 0; d < 3; ++d) {
      double part = 0;
      for (const auto& w : axes) part += w[static_cast<std::size_t>(d)] * w[static_cast<std::size_t>(d)];
      an.libres[static_cast<std::size_t>(ic)][static_cast<std::size_t>(3 + d)] = part > 0.81;
    }
    // Description : élimination qui sépare les rotations (ω ≠ 0) des translations pures, puis torseur de chacune
    std::vector<std::vector<double>> lignes = mouvements;
    std::size_t rangRot = 0;
    for (int col = 3; col < 6 && rangRot < lignes.size(); ++col) {
      std::size_t piv = rangRot;
      for (std::size_t i = rangRot; i < lignes.size(); ++i)
        if (std::abs(lignes[i][static_cast<std::size_t>(col)]) > std::abs(lignes[piv][static_cast<std::size_t>(col)])) piv = i;
      if (std::abs(lignes[piv][static_cast<std::size_t>(col)]) < 1e-6) continue;
      std::swap(lignes[rangRot], lignes[piv]);
      for (std::size_t i = 0; i < lignes.size(); ++i) {
        if (i == rangRot) continue;
        const double fct = lignes[i][static_cast<std::size_t>(col)] / lignes[rangRot][static_cast<std::size_t>(col)];
        for (int j = 0; j < 6; ++j) lignes[i][static_cast<std::size_t>(j)] -= fct * lignes[rangRot][static_cast<std::size_t>(j)];
      }
      ++rangRot;
    }
    std::vector<gp_Vec> translations;
    for (std::size_t i = rangRot; i < lignes.size(); ++i) {
      gp_Vec v(lignes[i][0], lignes[i][1], lignes[i][2]);
      if (v.Magnitude() < 1e-9) continue;
      v.Normalize();
      translations.push_back(v);
      an.mouvements[static_cast<std::size_t>(ic)].push_back("translation selon " + triplet(v.XYZ()));
    }
    const gp_Pnt C = S.centre(ic, S.poses0);
    const double L = S.echelles[static_cast<std::size_t>(ic)];
    for (std::size_t i = 0; i < rangRot; ++i) {
      gp_Vec w(lignes[i][3] / L, lignes[i][4] / L, lignes[i][5] / L);
      gp_Vec v(lignes[i][0], lignes[i][1], lignes[i][2]);
      for (const auto& t : translations) v -= t * v.Dot(t);  // la part due à une translation libre n'est pas un pas de vis
      const double w2 = w.SquareMagnitude();
      if (w2 < 1e-30) continue;
      const gp_Pnt surAxe = C.Translated(w.Crossed(v) / w2);
      const double pasParTour = w.Dot(v) / w2 * 2 * std::numbers::pi;
      const gp_Dir axe(w);
      std::string m = "rotation autour de l'axe " + triplet(axe.XYZ()) + " passant par " + triplet(surAxe.XYZ());
      if (std::abs(pasParTour) > 1e-6) m = "vissage d'axe " + triplet(axe.XYZ()) + " passant par " + triplet(surAxe.XYZ()) + ", pas " + nombreLisible(pasParTour) + " mm/tour";
      an.mouvements[static_cast<std::size_t>(ic)].push_back(m);
    }
  }
  return an;
}
}  // namespace prom

namespace prom {
ContrainteAssemblage creerLiaison(const Assemblage& a, const std::vector<ResultatPiece>& pieces, const std::string& type, int ca, const std::string& refA, int cb,
                                  const std::string& refB, double valeur, const std::vector<Assemblage>* sous) {
  ContrainteAssemblage c;
  c.type = type;
  c.a = ca;
  c.b = cb;
  c.refA = refA;
  c.refB = refB;
  c.valeur = type == "engrenage" ? (valeur == 0 ? 1 : valeur) : 0;
  // Contrat (AS-31) : composants existants, distincts, qui sont des pièces ; faces trouvées dans ces pièces
  const int nc = static_cast<int>(a.composants.size());
  if (ca < 0 || cb < 0 || ca >= nc || cb >= nc) throw std::invalid_argument("Liaison impossible : composant inexistant.");
  if (referenceAssemblage(refA) || referenceAssemblage(refB)) throw std::invalid_argument("Liaison impossible : une liaison relie deux composants, pas l'assemblage.");
  if (ca == cb) throw std::invalid_argument("Liaison impossible : les deux faces appartiennent au même composant.");
  const auto& A = a.composants[static_cast<size_t>(ca)];
  const auto& B = a.composants[static_cast<size_t>(cb)];
  if ((A.assemblage < 0 && (A.piece < 0 || A.piece >= static_cast<int>(pieces.size()))) || (B.assemblage < 0 && (B.piece < 0 || B.piece >= static_cast<int>(pieces.size()))))
    throw std::invalid_argument("Liaison impossible : pièce manquante.");
  const Ref ra = referenceComposant(A, refA, pieces, sous), rb = referenceComposant(B, refB, pieces, sous);
  if (ra.genre == Genre::Aucun || rb.genre == Genre::Aucun) throw std::invalid_argument("Liaison impossible : une des faces est introuvable.");
  if (const std::string refus = facesAdmises(type, nomGenre(ra.genre), nomGenre(rb.genre)); !refus.empty())
    throw std::invalid_argument("Liaison impossible : " + refus + ".");
  c.parametres = {A.rx, A.ry, A.rz, B.rx, B.ry, B.rz};
  if (type == "pivot" || type == "glissiere" || type == "vis") {
    // Position axiale de départ, que la liaison garde (pivot) ou fait varier (glissière, vis)
    const gp_Pnt pa = ra.p.Transformed(placementComposant(A)), pb = rb.p.Transformed(placementComposant(B));
    c.parametres.push_back(gp_Vec(pa, pb).Dot(gp_Vec(ra.d.Transformed(placementComposant(A)))));
    if (type == "vis") c.parametres.push_back(valeur == 0 ? 1 : valeur);
  }
  return c;
}
std::optional<double> valeurMesuree(const Assemblage& a, const ContrainteAssemblage& c, const std::vector<ResultatPiece>& pieces, const std::vector<Assemblage>* sous) {
  const int nc = static_cast<int>(a.composants.size());
  if (c.a < 0 || c.b < 0 || c.a >= nc || c.b >= nc) return std::nullopt;
  const ComposantAssemblage& A = a.composants[static_cast<std::size_t>(c.a)];
  const ComposantAssemblage& B = a.composants[static_cast<std::size_t>(c.b)];
  const Ref ra = referenceComposant(A, c.refA, pieces, sous), rb = referenceComposant(B, c.refB, pieces, sous);
  if (ra.genre == Genre::Aucun || rb.genre == Genre::Aucun) return std::nullopt;
  const gp_Trsf ta = referenceAssemblage(c.refA) ? gp_Trsf() : placementComposant(A), tb = referenceAssemblage(c.refB) ? gp_Trsf() : placementComposant(B);
  const gp_Pnt pa = ra.p.Transformed(ta), pb = rb.p.Transformed(tb);
  const gp_Vec va(ra.d.Transformed(ta)), vb(rb.d.Transformed(tb));
  if (c.type == "angle") {
    if (ra.point() || rb.point()) return std::nullopt;
    return std::atan2(va.Crossed(vb).Magnitude(), va.Dot(vb)) * DEG;
  }
  if (c.type != "coincidence") return std::nullopt;
  if (ra.plan() && rb.plan()) return gp_Vec(pa, pb).Dot(va);
  // Point ou droite et plan : distance signée le long de la normale du plan, comme dans la résolution
  if (ra.plan() != rb.plan() && !(ra.axe() && rb.axe())) {
    const bool planA = ra.plan();
    const gp_Pnt pp = planA ? pa : pb, autre = planA ? pb : pa;
    const gp_Vec n = planA ? va : vb;
    return gp_Vec(pp, autre).Dot(n);
  }
  return std::nullopt;
}

bool orienterAngle(Assemblage& a, int k, const std::vector<ResultatPiece>& pieces, const std::vector<Assemblage>* sous) {
  if (k < 0 || k >= static_cast<int>(a.contraintes.size())) return false;
  ContrainteAssemblage& c = a.contraintes[static_cast<std::size_t>(k)];
  const int nc = static_cast<int>(a.composants.size());
  if (c.type != "angle" || c.a < 0 || c.b < 0 || c.a >= nc || c.b >= nc) return false;
  const ComposantAssemblage& A = a.composants[static_cast<std::size_t>(c.a)];
  const ComposantAssemblage& B = a.composants[static_cast<std::size_t>(c.b)];
  const Ref ra = referenceComposant(A, c.refA, pieces, sous), rb = referenceComposant(B, c.refB, pieces, sous);
  if (ra.genre == Genre::Aucun || rb.genre == Genre::Aucun) return false;
  // Un côté porté par l'assemblage (A2) ne bouge pas : son repère est celui de l'assemblage
  const gp_Trsf ta = referenceAssemblage(c.refA) ? gp_Trsf() : placementComposant(A), tb = referenceAssemblage(c.refB) ? gp_Trsf() : placementComposant(B);
  if (ra.point() || rb.point()) return false;
  const gp_Vec va(ra.d.Transformed(ta)), vb(rb.d.Transformed(tb));
  gp_Vec n = va.Crossed(vb);
  if (n.Magnitude() < 1e-9) return false;  // faces parallèles : pas d'axe pour orienter
  n.Normalize();
  // Axe gardé dans le repère du composant a : il tourne avec lui
  gp_Vec local = n;
  local.Transform(ta.Inverted());
  c.parametres = {local.X(), local.Y(), local.Z()};
  c.alignement = 1;
  c.valeur = std::atan2(va.Crossed(vb).Dot(n), va.Dot(vb)) * DEG;
  return true;
}

std::string libelleContact(EtatContact e) {
  switch (e) {
    case EtatContact::Separees: return "séparées";
    case EtatContact::JeuFaible: return "jeu faible";
    case EtatContact::Contact: return "contact";
    case EtatContact::Interference: return "interférence";
    case EtatContact::Indetermine: return "indéterminé";
  }
  return {};
}

namespace {
double volumeSolides(const TopoDS_Shape& s) {
  double v = 0;
  for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next()) {
    GProp_GProps g;
    BRepGProp::VolumeProperties(ex.Current(), g);
    v += std::abs(g.Mass());
  }
  return v;
}
}  // namespace

namespace {
// Un solide de « dedans » est-il entièrement à l'intérieur d'un solide de « dehors » ? (un de ses sommets y est)
bool unSolideDans(const TopoDS_Shape& dedans, const TopoDS_Shape& dehors) {
  for (TopExp_Explorer ea(dedans, TopAbs_SOLID); ea.More(); ea.Next()) {
    TopExp_Explorer ev(ea.Current(), TopAbs_VERTEX);
    if (!ev.More()) continue;
    const gp_Pnt q = BRep_Tool::Pnt(TopoDS::Vertex(ev.Current()));
    for (TopExp_Explorer eb(dehors, TopAbs_SOLID); eb.More(); eb.Next()) {
      BRepClass3d_SolidClassifier cl(eb.Current(), q, 1e-7);
      if (cl.State() == TopAbs_IN) return true;
    }
  }
  return false;
}
// Distance et volume commun de deux formes déjà validées
AnalyseContact contactValide(const TopoDS_Shape& a, const TopoDS_Shape& b, const TolerancesContact& t) {
  AnalyseContact r;
  try {
    BRepExtrema_DistShapeShape dist(a, b);
    if (!dist.IsDone() || dist.NbSolution() == 0) { r.detail = "distance minimale impossible à calculer"; return r; }
    r.distance = dist.Value();
    r.pointA = dist.PointOnShape1(1);
    r.pointB = dist.PointOnShape2(1);
    // Inclusion complète sans contact des surfaces : OpenCascade ne la voit que si l'englobante est un solide, pas
    // un composé (sous-assemblage, pièce à plusieurs corps) ; on classe donc un sommet de chaque solide de l'une dans
    // les solides de l'autre (AS-33)
    bool inclusion = dist.InnerSolution();
    if (!inclusion && r.distance > t.contact) inclusion = unSolideDans(a, b) || unSolideDans(b, a);
    if (r.distance > t.contact && !inclusion) {
      r.etat = r.distance < t.jeuFaible ? EtatContact::JeuFaible : EtatContact::Separees;
      return r;
    }
    BRepAlgoAPI_Common commun(a, b);
    if (!commun.IsDone() || commun.HasErrors()) { r.detail = "calcul du volume commun en échec"; return r; }
    r.volumeCommun = volumeSolides(commun.Shape());
    r.distance = 0;
    // Épaisseur moyenne du recouvrement (volume commun ÷ moitié de sa surface) : un recouvrement plus mince que la
    // tolérance de contact est un contact, même entre de grandes faces (AS-34)
    GProp_GProps surf;
    BRepGProp::SurfaceProperties(commun.Shape(), surf);
    const double epaisseur = surf.Mass() > 1e-12 ? 2 * r.volumeCommun / surf.Mass() : 0;
    r.etat = r.volumeCommun > t.volume && epaisseur > t.contact ? EtatContact::Interference : EtatContact::Contact;
  } catch (const Standard_Failure&) {
    r.etat = EtatContact::Indetermine;
    r.detail = "calcul géométrique en échec";
  }
  return r;
}

// Formes préparées une fois : boîte englobante et défaut de géométrie éventuel
struct FormesPreparees {
  std::vector<Bnd_Box> boites;
  std::vector<std::string> defauts;
  explicit FormesPreparees(const std::vector<TopoDS_Shape>& formes) {
    for (const auto& f : formes) {
      Bnd_Box b;
      if (!f.IsNull()) BRepBndLib::Add(f, b);
      boites.push_back(b);
      defauts.push_back(f.IsNull() ? std::string("pièce sans géométrie") : defautGeometrie(f));
    }
  }
  AnalyseContact analyser(const std::vector<TopoDS_Shape>& formes, std::size_t i, std::size_t j, const TolerancesContact& t) const {
    for (std::size_t k : {i, j})
      if (!defauts[k].empty()) {
        AnalyseContact r;
        r.detail = formes[k].IsNull() ? defauts[k] : "géométrie invalide (" + defauts[k] + ")";
        return r;
      }
    return contactValide(formes[i], formes[j], t);
  }
};
}  // namespace

AnalyseContact analyserContact(const TopoDS_Shape& a, const TopoDS_Shape& b, const TolerancesContact& t) {
  AnalyseContact r;
  if (a.IsNull() || b.IsNull()) { r.detail = "pièce sans géométrie"; return r; }
  for (const TopoDS_Shape* s : {&a, &b})
    if (const std::string d = defautGeometrie(*s); !d.empty()) { r.detail = "géométrie invalide (" + d + ")"; return r; }
  return contactValide(a, b, t);
}

std::vector<PaireContact> contactsEntre(const std::vector<TopoDS_Shape>& formes, const TolerancesContact& t) {
  const FormesPreparees p(formes);
  std::vector<PaireContact> r;
  for (std::size_t i = 0; i < formes.size(); ++i)
    for (std::size_t j = i + 1; j < formes.size(); ++j) {
      if (formes[i].IsNull() || formes[j].IsNull()) continue;
      Bnd_Box bi = p.boites[i];
      bi.Enlarge(t.jeuFaible + t.contact);
      if (bi.IsOut(p.boites[j])) continue;  // boîtes écartées de plus que le jeu faible : séparées
      const AnalyseContact c = p.analyser(formes, i, j, t);
      if (c.etat != EtatContact::Separees) r.push_back({static_cast<int>(i), static_cast<int>(j), c});
    }
  return r;
}

BilanJeux plusPetitsJeux(const std::vector<TopoDS_Shape>& formes, std::size_t nombre, const TolerancesContact& t) {
  const FormesPreparees p(formes);
  struct Candidat {
    double borne;
    std::size_t i, j;
  };
  std::vector<Candidat> candidats;
  for (std::size_t i = 0; i < formes.size(); ++i)
    for (std::size_t j = i + 1; j < formes.size(); ++j)
      if (!formes[i].IsNull() && !formes[j].IsNull()) candidats.push_back({p.boites[i].Distance(p.boites[j]), i, j});
  std::stable_sort(candidats.begin(), candidats.end(), [](const Candidat& x, const Candidat& y) { return x.borne < y.borne; });
  BilanJeux bilan;
  std::vector<PaireContact>& r = bilan.jeux;
  for (const Candidat& k : candidats) {
    // La distance exacte n'est jamais plus petite que celle des boîtes : dès le dernier jeu retenu atteint, c'est fini
    // (seulement pour les jeux ; les paires qui se touchent, à boîtes jointives, sont toutes vues avant)
    if (nombre > 0 && r.size() >= nombre && k.borne > t.contact && k.borne >= r[nombre - 1].c.distance) break;
    PaireContact x{static_cast<int>(k.i), static_cast<int>(k.j), p.analyser(formes, k.i, k.j, t)};
    switch (x.c.etat) {
      case EtatContact::Contact: ++bilan.contacts; continue;
      case EtatContact::Interference: ++bilan.interferences; continue;
      case EtatContact::Indetermine: ++bilan.indetermines; continue;
      case EtatContact::Separees:
      case EtatContact::JeuFaible: break;
    }
    r.insert(std::upper_bound(r.begin(), r.end(), x, [](const PaireContact& u, const PaireContact& v) { return u.c.distance < v.c.distance; }), x);
  }
  if (r.size() > nombre) r.resize(nombre);
  return bilan;
}

ProprietesAssemblage proprietesAssemblage(const std::vector<Assemblage>& assemblages, int indice, const std::vector<Piece>& definitions,
                                          const std::vector<ResultatPiece>& resultats) {
  ProprietesAssemblage r;
  double mx = 0, my = 0, mz = 0;
  std::vector<int> pile;  // assemblages en cours de développement (contre les boucles)
  std::function<void(int, const gp_Trsf&)> parcourir = [&](int a, const gp_Trsf& parent) {
    if (a < 0 || a >= static_cast<int>(assemblages.size())) return;
    if (std::find(pile.begin(), pile.end(), a) != pile.end()) {
      r.exclues.push_back("sous-assemblage « " + assemblages[static_cast<size_t>(a)].nom + " » inclus dans lui-même : ignoré");
      return;
    }
    pile.push_back(a);
    for (const auto& c : assemblages[static_cast<size_t>(a)].composants) {
      if (c.supprime) continue;
      const gp_Trsf t = parent * placementComposant(c);
      if (c.assemblage >= 0) {
        parcourir(c.assemblage, t);
        continue;
      }
      if (c.piece < 0 || c.piece >= static_cast<int>(definitions.size()) || c.piece >= static_cast<int>(resultats.size())) {
        r.exclues.push_back("composant sans pièce");
        continue;
      }
      const Piece& def = definitions[static_cast<size_t>(c.piece)];
      const ProprietesMasse pm = proprietesMasse(resultats[static_cast<size_t>(c.piece)].forme, def.densite);
      if (!pm.valide) {
        r.exclues.push_back("« " + def.nom + " » : " + libelleStatut(pm.statut) + (pm.defaut.empty() ? std::string() : " (" + pm.defaut + ")"));
        continue;
      }
      const gp_Pnt g = gp_Pnt(pm.cx, pm.cy, pm.cz).Transformed(t);
      r.masse += pm.masse;
      r.volume += pm.volume;
      mx += g.X() * pm.masse;
      my += g.Y() * pm.masse;
      mz += g.Z() * pm.masse;
      ++r.pieces;
      if (!def.densiteConfirmee) ++r.provisoires;
    }
    pile.pop_back();
  };
  parcourir(indice, gp_Trsf());
  if (r.masse > 0) r.centre = gp_Pnt(mx / r.masse, my / r.masse, mz / r.masse);
  return r;
}

}  // namespace prom

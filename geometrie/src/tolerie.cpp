// Prométhée : tôlerie — tôle de base, bords tombés, déplié et ses exports (M5). Licence GPL-3.0-only.
#include "tolerie_interne.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <sstream>
#include <tuple>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec2d.hxx>

namespace prom {

namespace {
constexpr double PI = std::numbers::pi;
constexpr double JEU_COIN = 0.2;  // mm : jeu laissé entre un bord tombé et le pli voisin

double volumeDe(const TopoDS_Shape& s) {
  if (s.IsNull()) return 0;
  GProp_GProps g;
  BRepGProp::VolumeProperties(s, g);
  return std::abs(g.Mass());
}
gp_Pnt centre(const TopoDS_Shape& f) {
  GProp_GProps g;
  BRepGProp::SurfaceProperties(f, g);
  return g.CentreOfMass();
}
std::string nombre(double v, int decimales = 2) {
  char t[64];
  std::snprintf(t, sizeof t, "%.*f", decimales, v);
  std::string s = t;
  if (s.find('.') != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  return s == "-0" ? "0" : s;
}
std::string virgule(std::string s) {
  for (char& c : s)
    if (c == '.') c = ',';
  return s;
}
}  // namespace

double facteurKDin6935(double rayon, double epaisseur) {
  if (epaisseur <= 0) return 0.5;
  const double rapport = std::max(rayon, 1e-6) / epaisseur;
  if (rapport > 5) return 0.5;
  const double k = std::clamp(0.65 + 0.5 * std::log10(rapport), 0.5, 1.0);
  return k / 2;
}
double facteurKTole(const FonctionPiece& tole) { return tole.facteurK > 0 ? std::min(tole.facteurK, 1.0) : facteurKDin6935(tole.rayon, tole.epaisseur); }
double longueurDeveloppee(double angleDegres, double rayon, double epaisseur, double facteurK) { return angleDegres * PI / 180 * (rayon + facteurK * epaisseur); }

namespace tolerie {
namespace {

gp_Pnt vers3D(const gp_Ax3& ax, double x, double y, double z = 0) {
  return ax.Location().Translated(gp_Vec(ax.XDirection()) * x + gp_Vec(ax.YDirection()) * y + gp_Vec(ax.Direction()) * z);
}
gp_Pnt2d versLocal(const gp_Ax3& ax, const gp_Pnt& p) {
  const gp_Vec w(ax.Location(), p);
  return {w.Dot(gp_Vec(ax.XDirection())), w.Dot(gp_Vec(ax.YDirection()))};
}
// Face plane d'un polygone (points 2D du déplié) dans le plan z = 0 du repère
TopoDS_Face polygone(const gp_Ax3& ax, const std::vector<gp_Pnt2d>& pts) {
  BRepBuilderAPI_MakePolygon mp;
  for (const auto& q : pts) mp.Add(vers3D(ax, q.X(), q.Y()));
  mp.Close();
  return BRepBuilderAPI_MakeFace(gp_Pln(ax), mp.Wire(), true).Face();
}
TopoDS_Face disque(const gp_Ax3& ax, const gp_Pnt2d& c, double r) {
  const gp_Circ cercle(gp_Ax2(vers3D(ax, c.X(), c.Y()), ax.Direction()), r);
  return BRepBuilderAPI_MakeFace(gp_Pln(ax), BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(cercle).Edge()).Wire(), true).Face();
}
TopoDS_Shape reunir(const TopoDS_Shape& a, const TopoDS_Shape& b) {
  if (a.IsNull()) return b;
  if (b.IsNull()) return a;
  BRepAlgoAPI_Fuse f(a, b);
  return f.IsDone() && !f.HasErrors() ? f.Shape() : a;
}
TopoDS_Shape enlever(const TopoDS_Shape& a, const TopoDS_Shape& b) {
  if (a.IsNull() || b.IsNull()) return a;
  BRepAlgoAPI_Cut c(a, b);
  return c.IsDone() && !c.HasErrors() ? c.Shape() : a;
}
TopoDS_Shape unifier(const TopoDS_Shape& s) {
  if (s.IsNull()) return s;
  try {
    ShapeUpgrade_UnifySameDomain u(s, true, true, true);
    u.Build();
    return u.Shape();
  } catch (const Standard_Failure&) {
    return s;
  }
}

}  // namespace

std::string Constructeur::poserBase(const FonctionPiece& f, const std::vector<TopoDS_Face>& regions, const std::vector<std::pair<TopoDS_Edge, int>>& origine, const gp_Ax3& ax) {
  if (m_actif) return "Une pièce n'a qu'une tôle de base : ajoute des bords tombés à celle-ci.";
  if (!(f.epaisseur > 0)) return "L'épaisseur de la tôle doit être positive.";
  if (!(f.rayon > 0)) return "Le rayon de pliage doit être positif.";
  m_e = f.epaisseur;
  m_r = f.rayon;
  m_k = facteurKTole(f);
  m_degagement = f.degagement;
  const gp_Dir n = f.inverse ? ax.Direction().Reversed() : ax.Direction();
  m_repere = gp_Ax3(ax.Location(), n, ax.XDirection());
  Paroi p;
  p.fonction = f.id;
  p.repere = m_repere;
  // Côtés droits du contour : de quel côté est la matière ?
  auto dedans = [&](const gp_Pnt2d& q) {
    const gp_Pnt q3 = vers3D(m_repere, q.X(), q.Y());
    for (const auto& r : regions) {
      BRepClass_FaceClassifier cl(r, q3, 1e-7);
      if (cl.State() == TopAbs_IN) return true;
    }
    return false;
  };
  for (const auto& [ed, el] : origine) {
    BRepAdaptor_Curve c(ed);
    if (c.GetType() != GeomAbs_Line) continue;
    const gp_Pnt2d a = versLocal(m_repere, c.Value(c.FirstParameter())), b = versLocal(m_repere, c.Value(c.LastParameter()));
    const gp_Vec2d ab(a, b);
    if (ab.Magnitude() < 1e-7) continue;
    const gp_Vec2d nrm = gp_Vec2d(-ab.Y(), ab.X()).Normalized();
    const gp_Pnt2d m((a.X() + b.X()) / 2, (a.Y() + b.Y()) / 2);
    const double eps = std::min(1e-3, 0.01 * ab.Magnitude());
    const bool g = dedans(m.Translated(nrm * eps)), d = dedans(m.Translated(nrm * -eps));
    if (g == d) continue;  // côté intérieur (entre deux régions) ou indécidable
    p.cotes.push_back({"cote:" + std::to_string(el), a, b, g ? -nrm : nrm});
  }
  for (const auto& r : regions) m_plat = reunir(m_plat, r);
  m_parois[f.id] = p;
  m_base = f.id;
  m_actif = true;
  return {};
}

OutilTole Constructeur::bordTombe(const FonctionPiece& f, const TopoDS_Shape& corps) {
  OutilTole o;
  effacerAttente();
  if (!m_actif) { o.erreur = "Pose d'abord une tôle de base : le bord tombé se plie sur une de ses arêtes."; return o; }
  if (f.references.empty()) { o.erreur = "Choisis l'arête (ou la face mince) le long de laquelle plier."; return o; }
  // Côté visé : nom de face « 3:cote:12 », « 5:bout »…, ou arête « A|B » dont une des faces en est un
  const Paroi* paroi = nullptr;
  const Paroi::Cote* cote = nullptr;
  {
    std::string ref = f.references.front();
    std::vector<std::string> noms;
    for (std::size_t debut = 0;;) {
      const std::size_t fin = ref.find('|', debut);
      noms.push_back(ref.substr(debut, fin == std::string::npos ? std::string::npos : fin - debut));
      if (fin == std::string::npos) break;
      debut = fin + 1;
    }
    for (std::string nom : noms) {
      if (nom.rfind("S:", 0) == 0) nom = nom.substr(2);
      if (const auto h = nom.find('#'); h != std::string::npos) nom = nom.substr(0, h);
      const auto deux = nom.find(':');
      if (deux == std::string::npos || deux == 0 || !std::isdigit(static_cast<unsigned char>(nom[0]))) continue;
      const int id = std::atoi(nom.c_str());
      const std::string cle = nom.substr(deux + 1);
      const auto it = m_parois.find(id);
      if (it == m_parois.end()) continue;
      for (const auto& c : it->second.cotes)
        if (c.cle == cle) {
          paroi = &it->second;
          cote = &c;
          break;
        }
      if (cote) break;
    }
    if (!cote) {
      o.erreur = "« " + ref + " » n'est pas un côté droit d'une paroi de la tôle : choisis la face mince (ou l'arête) le long de laquelle plier.";
      return o;
    }
  }
  if (paroi->retraitsPli.count(cote->cle)) { o.erreur = "Ce côté porte déjà un bord tombé."; return o; }
  const double t = m_e, r = f.rayon > 0 ? f.rayon : m_r, theta = f.angle;
  if (!(theta > 0) || theta > 180) { o.erreur = "L'angle de pliage doit être compris entre 0 et 180°."; return o; }
  if (!(f.longueur > 0)) { o.erreur = "La longueur du bord doit être positive."; return o; }
  const gp_Pnt2d A = cote->a, B = cote->b;
  const double longueurCote = A.Distance(B);
  const gp_Vec2d u = gp_Vec2d(A, B).Normalized(), d = cote->dehors;
  // Début du pli par rapport à l'arête (le long de « dehors ») : pli extérieur, matière extérieure ou intérieure
  const double e = f.positionPli == "pli-exterieur" ? 0.0 : f.positionPli == "matiere-exterieure" ? -r : -(r + t);
  // Plis voisins aux deux bouts de l'arête
  bool voisinA = false, voisinB = false;
  double reculA = 0, reculB = 0;
  for (const auto& c : paroi->cotes) {
    const auto it = paroi->retraitsPli.find(c.cle);
    if (it == paroi->retraitsPli.end() || &c == cote) continue;
    for (const gp_Pnt2d& q : {c.a, c.b}) {
      if (q.Distance(A) < 1e-6) { voisinA = true; reculA = std::max(reculA, it->second); }
      if (q.Distance(B) < 1e-6) { voisinB = true; reculB = std::max(reculB, it->second); }
    }
  }
  const double coinA = voisinA && (reculA > 0 || e < 0) ? std::max(reculA, -e) + JEU_COIN : 0;
  const double coinB = voisinB && (reculB > 0 || e < 0) ? std::max(reculB, -e) + JEU_COIN : 0;
  const double s0 = std::max(f.retraitDebut, coinA), s1 = longueurCote - std::max(f.retraitFin, coinB);
  if (s1 - s0 < 1e-3) { o.erreur = "Le bord tombé n'a plus de largeur : retraits ou plis voisins trop grands pour cette arête de " + nombre(longueurCote) + " mm."; return o; }
  const double ba = longueurDeveloppee(theta, r, t, m_k);
  const double demiAngle = theta <= 90 ? std::tan(theta * PI / 360) : 1.0;
  const double avance = f.mesure == "interieure" ? r * demiAngle : f.mesure == "tangente" ? 0.0 : (r + t) * demiAngle;
  const double lf = f.longueur - avance;
  if (lf < 1e-3) { o.erreur = "Bord trop court : sa longueur doit dépasser " + nombre(avance, 3) + " mm (rayon et épaisseur du pli compris)."; return o; }
  // Zones à enlever de la paroi : entaille sous le pli (matière intérieure ou extérieure), dégagements
  auto point = [&](double s, double x) { return gp_Pnt2d(A.X() + u.X() * s + d.X() * x, A.Y() + u.Y() * s + d.Y() * x); };
  std::vector<std::vector<gp_Pnt2d>> rectanglesRetrait;
  std::vector<std::pair<gp_Pnt2d, double>> disquesRetrait;
  if (e < 0) {
    const double bas = voisinA ? std::max(f.retraitDebut, reculA) : s0, haut = voisinB ? longueurCote - std::max(f.retraitFin, reculB) : s1;
    rectanglesRetrait.push_back({point(bas, e), point(haut, e), point(haut, 1), point(bas, 1)});
  }
  const double lDeg = t, pDeg = t / 2;  // largeur et dépassement des dégagements de pli
  if (m_degagement != "aucun")
    for (const bool debut : {true, false}) {
      const bool partiel = debut ? (f.retraitDebut > 1e-9 && !voisinA) : (f.retraitFin > 1e-9 && !voisinB);
      if (!partiel) continue;
      const double sa = debut ? s0 - lDeg : s1, sb = debut ? s0 : s1 + lDeg;
      if (m_degagement == "oblong") {
        const double xb = e - pDeg + lDeg / 2;
        rectanglesRetrait.push_back({point(sa, xb), point(sb, xb), point(sb, 1), point(sa, 1)});
        disquesRetrait.push_back({point((sa + sb) / 2, xb), lDeg / 2});
      } else
        rectanglesRetrait.push_back({point(sa, e - pDeg), point(sb, e - pDeg), point(sb, 1), point(sa, 1)});
    }
  // ----- Pièce pliée -----
  const gp_Ax3& W = paroi->repere;
  auto P3 = [&](const gp_Pnt2d& q, double z) { return vers3D(W, q.X(), q.Y(), z); };
  const gp_Vec u3 = gp_Vec(W.XDirection()) * u.X() + gp_Vec(W.YDirection()) * u.Y();
  const gp_Vec d3 = gp_Vec(W.XDirection()) * d.X() + gp_Vec(W.YDirection()) * d.Y();
  const gp_Vec N(W.Direction());
  const double sigma = f.inverse ? -1 : 1;
  const gp_Pnt2d sec0 = point(s0, e), sec1 = point(s1, e);
  const gp_Pnt axeP = P3(sec0, sigma > 0 ? t + r : -r);
  gp_Vec axeV = u3;
  {
    const gp_Vec v = N * (sigma > 0 ? -(t + r) : (t + r));
    if ((axeV ^ v).Dot(d3) < 0) axeV.Reverse();
  }
  const gp_Ax1 axe(axeP, gp_Dir(axeV));
  try {
    BRepBuilderAPI_MakePolygon mp(P3(sec0, 0), P3(sec1, 0), P3(sec1, t), P3(sec0, t), true);
    const TopoDS_Face section = BRepBuilderAPI_MakeFace(mp.Wire(), true).Face();
    const TopoDS_Shape pli = BRepPrimAPI_MakeRevol(section, axe, theta * PI / 180).Shape();
    const TopoDS_Shape plaque = BRepPrimAPI_MakePrism(section, d3 * lf).Shape();
    gp_Trsf rot;
    rot.SetRotation(axe, theta * PI / 180);
    const TopoDS_Shape plaqueR = BRepBuilderAPI_Transform(plaque, rot, true).Shape();
    BRepAlgoAPI_Fuse fu(pli, plaqueR);
    if (!fu.IsDone() || fu.HasErrors()) { o.erreur = "Le pli et le bord n'ont pas pu être réunis."; return o; }
    o.ajout = fu.Shape();
    // Repère de la nouvelle paroi : à la fin du pli, X le long du bord, Z du même côté que la paroi d'origine
    const gp_Pnt Oc = P3(sec0, 0).Transformed(rot);
    const gp_Dir Xc = gp_Dir(d3).Transformed(rot), Nc = gp_Dir(N).Transformed(rot);
    const gp_Ax3 Rc(Oc, Nc, Xc);
    const double sy = u3.Dot(gp_Vec(Rc.YDirection())) > 0 ? 1 : -1, largeur = s1 - s0;
    const std::string id = std::to_string(f.id) + ":";
    const double tol = 1e-5 * std::max(1.0, std::max(lf, largeur));
    for (TopExp_Explorer ex(o.ajout, TopAbs_FACE); ex.More(); ex.Next()) {
      const TopoDS_Face fc = TopoDS::Face(ex.Current());
      BRepAdaptor_Surface sf(fc);
      std::string nom;
      if (sf.GetType() == GeomAbs_Cylinder) nom = std::abs(sf.Cylinder().Radius() - r) < std::abs(sf.Cylinder().Radius() - r - t) ? "pli:int" : "pli:ext";
      else {
        const gp_Pnt c = centre(fc);
        const gp_Vec w(Oc, c);
        const double x = w.Dot(gp_Vec(Rc.XDirection())), y = w.Dot(gp_Vec(Rc.YDirection())), z = w.Dot(gp_Vec(Rc.Direction()));
        const double s = gp_Vec(P3(A, 0), c).Dot(u3);
        if (x > tol) {
          if (std::abs(x - lf) < tol) nom = "bout";
          else if (std::abs(z) < tol) nom = "dessous";
          else if (std::abs(z - t) < tol) nom = "dessus";
          else if (std::abs(y) < tol) nom = "cote1";
          else nom = std::abs(y - sy * largeur) < tol ? "cote2" : "bord";
        } else
          nom = std::abs(s - s0) < tol ? "pli:cote1" : std::abs(s - s1) < tol ? "pli:cote2" : "pli:debut";
      }
      o.nomsAjout.push_back({fc, id + nom});
    }
    // Entaille et dégagements (prismes traversant l'épaisseur)
    TopoDS_Shape retraitPlat;
    for (const auto& rect : rectanglesRetrait) {
      std::vector<gp_Pnt2d> q;
      for (const auto& p2 : rect) q.push_back(p2);
      const TopoDS_Face fl = polygone(gp_Ax3(P3({0, 0}, -1), W.Direction(), W.XDirection()), q);
      o.retrait = reunir(o.retrait, BRepPrimAPI_MakePrism(fl, N * (t + 2)).Shape());
    }
    for (const auto& [c, rr] : disquesRetrait) {
      const TopoDS_Face fl = disque(gp_Ax3(P3({0, 0}, -1), W.Direction(), W.XDirection()), c, rr);
      o.retrait = reunir(o.retrait, BRepPrimAPI_MakePrism(fl, N * (t + 2)).Shape());
    }
    if (!o.retrait.IsNull())
      for (TopExp_Explorer ex(o.retrait, TopAbs_FACE); ex.More(); ex.Next()) o.nomsRetrait.push_back({TopoDS::Face(ex.Current()), id + "entaille"});
    // Le bord ne doit pas traverser la matière restante
    if (!corps.IsNull()) {
      const TopoDS_Shape reste = o.retrait.IsNull() ? corps : enlever(corps, o.retrait);
      BRepAlgoAPI_Common com(o.ajout, reste);
      if (com.IsDone() && !com.HasErrors() && volumeDe(com.Shape()) > 1e-4 * std::max(1.0, volumeDe(o.ajout))) {
        o.erreur = "Le bord tombé rencontrerait la matière de la pièce : change son sens, sa position ou sa longueur.";
        return o;
      }
    }
    // ----- Nouvelle paroi et déplié -----
    Paroi enfant;
    enfant.fonction = f.id;
    enfant.repere = Rc;
    const gp_Pnt2d o2 = point(s0, e + ba);
    const Affine2 local{d.X(), sy * u.X(), d.Y(), sy * u.Y(), o2.X(), o2.Y()};
    enfant.versDeplie = paroi->versDeplie * local;
    enfant.cotes = {{"bout", {lf, 0}, {lf, sy * largeur}, gp_Vec2d(1, 0)},
                    {"cote1", {0, 0}, {lf, 0}, gp_Vec2d(0, -sy)},
                    {"cote2", {0, sy * largeur}, {lf, sy * largeur}, gp_Vec2d(0, sy)}};
    const Affine2& vp = paroi->versDeplie;
    auto plat = [&](const std::vector<gp_Pnt2d>& q) {
      std::vector<gp_Pnt2d> v;
      for (const auto& p2 : q) v.push_back(vp(p2));
      return polygone(m_repere, v);
    };
    TopoDS_Shape nouveau = m_plat;
    for (const auto& rect : rectanglesRetrait) nouveau = enlever(nouveau, plat(rect));
    for (const auto& [c, rr] : disquesRetrait) nouveau = enlever(nouveau, disque(m_repere, vp(c), rr));
    nouveau = reunir(nouveau, plat({point(s0, e), point(s1, e), point(s1, e + ba + lf), point(s0, e + ba + lf)}));
    LignePli lp;
    lp.fonction = f.id;
    auto pp = [&](double s, double x) { const gp_Pnt2d q = vp(point(s, x)); return vers3D(m_repere, q.X(), q.Y()); };
    lp.a = pp(s0, e + ba / 2);
    lp.b = pp(s1, e + ba / 2);
    lp.debutA = pp(s0, e);
    lp.debutB = pp(s1, e);
    lp.finA = pp(s0, e + ba);
    lp.finB = pp(s1, e + ba);
    lp.angle = theta;
    lp.rayon = r;
    lp.facteurK = m_k;
    lp.developpee = ba;
    lp.versLeHaut = sigma > 0;
    lp.note = std::string(sigma > 0 ? "HAUT " : "BAS ") + virgule(nombre(theta, 1)) + "° R" + virgule(nombre(r, 2));
    m_attente.present = true;
    m_attente.paroi = enfant;
    m_attente.parent = paroi->fonction;
    m_attente.cle = cote->cle;
    m_attente.retrait = -e;
    m_attente.plat = nouveau;
    m_attente.pli = lp;
  } catch (const Standard_Failure& x) {
    o.erreur = std::string("Construction du pli impossible : ") + (x.GetMessageString() ? x.GetMessageString() : "erreur de géométrie") + ".";
  }
  return o;
}

void Constructeur::valider() {
  if (!m_attente.present) return;
  m_parois[m_attente.paroi.fonction] = m_attente.paroi;
  m_parois[m_attente.parent].retraitsPli[m_attente.cle] = m_attente.retrait;
  m_plat = m_attente.plat;
  m_plis.push_back(m_attente.pli);
  effacerAttente();
}

void Constructeur::abandonner() { effacerAttente(); }

void Constructeur::effacerAttente() {
  m_attente.present = false;
  m_attente.paroi.cotes.clear();
  m_attente.paroi.retraitsPli.clear();
  m_attente.plat.Nullify();
  m_attente.cle.clear();
}

void Constructeur::ignorer(const FonctionPiece& f) {
  if (!m_actif) return;
  m_alertes.push_back("« " + f.nom + " » n'est pas reprise dans le déplié (fonction ajoutée à la tôle, hors tôlerie).");
}

DeplieTole Constructeur::deplie() const {
  DeplieTole d;
  if (!m_actif) return d;
  d.present = true;
  d.base = m_base;
  d.epaisseur = m_e;
  d.rayon = m_r;
  d.facteurK = m_k;
  d.repere = m_repere;
  d.contour = unifier(m_plat);
  d.plis = m_plis;
  d.alertes = m_alertes;
  double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
  for (TopExp_Explorer ex(d.contour, TopAbs_VERTEX); ex.More(); ex.Next()) {
    const gp_Pnt2d q = versLocal(m_repere, BRep_Tool::Pnt(TopoDS::Vertex(ex.Current())));
    x0 = std::min(x0, q.X());
    y0 = std::min(y0, q.Y());
    x1 = std::max(x1, q.X());
    y1 = std::max(y1, q.Y());
  }
  // Les arcs dépassent parfois leurs sommets : encombrement exact par la boîte des arêtes
  for (TopExp_Explorer ex(d.contour, TopAbs_EDGE); ex.More(); ex.Next()) {
    BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
    for (int k = 0; k <= 16; ++k) {
      const gp_Pnt2d q = versLocal(m_repere, c.Value(c.FirstParameter() + (c.LastParameter() - c.FirstParameter()) * k / 16));
      x0 = std::min(x0, q.X());
      y0 = std::min(y0, q.Y());
      x1 = std::max(x1, q.X());
      y1 = std::max(y1, q.Y());
    }
  }
  if (x0 <= x1) {
    d.xmin = x0;
    d.ymin = y0;
    d.xmax = x1;
    d.ymax = y1;
  }
  return d;
}

}  // namespace tolerie

TopoDS_Shape solideDeplie(const DeplieTole& d) {
  if (!d.present || d.contour.IsNull()) return {};
  try {
    return BRepPrimAPI_MakePrism(d.contour, gp_Vec(d.repere.Direction()) * d.epaisseur).Shape();
  } catch (const Standard_Failure&) {
    return {};
  }
}

namespace {
// Traits du contour du déplié dans son repère (x, y), coin bas-gauche à l'origine
struct TraitPlat {
  bool arc = false, cercle = false;
  double x1 = 0, y1 = 0, x2 = 0, y2 = 0;  // ligne
  double cx = 0, cy = 0, r = 0, a0 = 0, a1 = 0;  // arc : degrés, sens direct
};
std::vector<TraitPlat> traitsPlats(const DeplieTole& d) {
  std::vector<TraitPlat> v;
  const gp_Ax3& ax = d.repere;
  auto loc = [&](const gp_Pnt& p) { const gp_Vec w(ax.Location(), p); return gp_Pnt2d(w.Dot(gp_Vec(ax.XDirection())) - d.xmin, w.Dot(gp_Vec(ax.YDirection())) - d.ymin); };
  for (TopExp_Explorer ex(d.contour, TopAbs_EDGE); ex.More(); ex.Next()) {
    const TopoDS_Edge ed = TopoDS::Edge(ex.Current());
    BRepAdaptor_Curve c(ed);
    const double t0 = c.FirstParameter(), t1 = c.LastParameter();
    if (c.GetType() == GeomAbs_Line) {
      const gp_Pnt2d a = loc(c.Value(t0)), b = loc(c.Value(t1));
      v.push_back({false, false, a.X(), a.Y(), b.X(), b.Y()});
    } else if (c.GetType() == GeomAbs_Circle) {
      const gp_Circ ci = c.Circle();
      TraitPlat t;
      const gp_Pnt2d ce = loc(ci.Location());
      t.cx = ce.X();
      t.cy = ce.Y();
      t.r = ci.Radius();
      if (std::abs(t1 - t0 - 2 * PI) < 1e-9) {
        t.cercle = true;
        v.push_back(t);
        continue;
      }
      const gp_Pnt2d a = loc(c.Value(t0)), b = loc(c.Value(t1)), m = loc(c.Value((t0 + t1) / 2));
      auto ang = [&](const gp_Pnt2d& q) { return std::atan2(q.Y() - t.cy, q.X() - t.cx) * 180 / PI; };
      // Sens direct de a vers b s'il passe par le milieu, sinon de b vers a
      const double aa = ang(a), ab = ang(b), am = ang(m);
      auto balayage = [](double de, double vers) { double s = std::fmod(vers - de + 720, 360); return s; };
      t.arc = true;
      if (balayage(aa, am) < balayage(aa, ab)) { t.a0 = aa; t.a1 = ab; }
      else { t.a0 = ab; t.a1 = aa; }
      v.push_back(t);
    } else {
      GCPnts_QuasiUniformDeflection q(c, 0.005, t0, t1);
      if (!q.IsDone()) continue;
      for (int k = 1; k < q.NbPoints(); ++k) {
        const gp_Pnt2d a = loc(q.Value(k)), b = loc(q.Value(k + 1));
        v.push_back({false, false, a.X(), a.Y(), b.X(), b.Y()});
      }
    }
  }
  return v;
}
}  // namespace

std::string dxfDeplie(const DeplieTole& d, const std::string& titre) {
  std::ostringstream s;
  auto g = [&s](int code, const std::string& v) { s << code << "\r\n" << v << "\r\n"; };
  auto n6 = [](double v) { return nombre(v, 6); };
  g(0, "SECTION"); g(2, "HEADER"); g(9, "$ACADVER"); g(1, "AC1009"); g(9, "$INSUNITS"); g(70, "4");
  g(9, "$EXTMIN"); g(10, "0"); g(20, "0"); g(30, "0");
  g(9, "$EXTMAX"); g(10, n6(d.xmax - d.xmin)); g(20, n6(d.ymax - d.ymin)); g(30, "0");
  g(0, "ENDSEC");
  g(0, "SECTION"); g(2, "TABLES");
  g(0, "TABLE"); g(2, "LTYPE"); g(70, "2");
  g(0, "LTYPE"); g(2, "CONTINUOUS"); g(70, "0"); g(3, "Solid line"); g(72, "65"); g(73, "0"); g(40, "0");
  g(0, "LTYPE"); g(2, "DASHED"); g(70, "0"); g(3, "__ __ __"); g(72, "65"); g(73, "2"); g(40, "6"); g(49, "4"); g(49, "-2");
  g(0, "ENDTAB");
  g(0, "TABLE"); g(2, "LAYER"); g(70, "4");
  for (const auto& [nom, couleur, type] : {std::tuple{"CONTOUR", "7", "CONTINUOUS"}, std::tuple{"PLIS-HAUT", "1", "CONTINUOUS"}, std::tuple{"PLIS-BAS", "5", "DASHED"}, std::tuple{"NOTES", "3", "CONTINUOUS"}}) {
    g(0, "LAYER"); g(2, nom); g(70, "0"); g(62, couleur); g(6, type);
  }
  g(0, "ENDTAB"); g(0, "ENDSEC");
  g(0, "SECTION"); g(2, "ENTITIES");
  for (const auto& t : traitsPlats(d)) {
    if (t.cercle) { g(0, "CIRCLE"); g(8, "CONTOUR"); g(10, n6(t.cx)); g(20, n6(t.cy)); g(30, "0"); g(40, n6(t.r)); }
    else if (t.arc) { g(0, "ARC"); g(8, "CONTOUR"); g(10, n6(t.cx)); g(20, n6(t.cy)); g(30, "0"); g(40, n6(t.r)); g(50, n6(t.a0)); g(51, n6(t.a1)); }
    else { g(0, "LINE"); g(8, "CONTOUR"); g(10, n6(t.x1)); g(20, n6(t.y1)); g(30, "0"); g(11, n6(t.x2)); g(21, n6(t.y2)); g(31, "0"); }
  }
  const gp_Ax3& ax = d.repere;
  auto loc = [&](const gp_Pnt& p) { const gp_Vec w(ax.Location(), p); return gp_Pnt2d(w.Dot(gp_Vec(ax.XDirection())) - d.xmin, w.Dot(gp_Vec(ax.YDirection())) - d.ymin); };
  const double hauteurTexte = std::clamp(0.02 * std::max(d.xmax - d.xmin, d.ymax - d.ymin), 1.5, 5.0);
  int k = 0;
  for (const auto& p : d.plis) {
    ++k;
    const gp_Pnt2d a = loc(p.a), b = loc(p.b);
    const char* calque = p.versLeHaut ? "PLIS-HAUT" : "PLIS-BAS";
    g(0, "LINE"); g(8, calque); g(10, n6(a.X())); g(20, n6(a.Y())); g(30, "0"); g(11, n6(b.X())); g(21, n6(b.Y())); g(31, "0");
    double angle = std::atan2(b.Y() - a.Y(), b.X() - a.X()) * 180 / PI;
    if (angle > 90.0001) angle -= 180;
    if (angle < -89.9999) angle += 180;
    std::string note = "PLI " + std::to_string(k) + " " + p.note;
    std::string dxfNote;
    for (std::size_t i = 0; i < note.size(); ++i) {
      if (note.compare(i, 2, "°") == 0) { dxfNote += "%%d"; ++i; }
      else dxfNote += note[i];
    }
    const double mx = (a.X() + b.X()) / 2, my = (a.Y() + b.Y()) / 2, nx = -std::sin(angle * PI / 180), ny = std::cos(angle * PI / 180);
    g(0, "TEXT"); g(8, "NOTES"); g(10, n6(mx + nx * hauteurTexte * 0.4)); g(20, n6(my + ny * hauteurTexte * 0.4)); g(30, "0"); g(40, n6(hauteurTexte)); g(1, dxfNote);
    g(50, n6(angle)); g(72, "1"); g(11, n6(mx + nx * hauteurTexte * 0.4)); g(21, n6(my + ny * hauteurTexte * 0.4)); g(31, "0");
  }
  if (!titre.empty()) {
    std::string t;
    for (unsigned char c : titre) t += c < 128 ? static_cast<char>(c) : '?';
    g(0, "TEXT"); g(8, "NOTES"); g(10, "0"); g(20, n6(-3 * hauteurTexte)); g(30, "0"); g(40, n6(hauteurTexte));
    g(1, t + " - ep. " + nombre(d.epaisseur, 2) + " mm, K " + nombre(d.facteurK, 3));
  }
  g(0, "ENDSEC"); g(0, "EOF");
  return s.str();
}

std::string svgDeplie(const DeplieTole& d, const std::string& titre) {
  const double marge = 10, l = d.xmax - d.xmin + 2 * marge, h = d.ymax - d.ymin + 2 * marge + 8;
  std::ostringstream s;
  auto X = [&](double x) { return nombre(x + marge, 3); };
  auto Y = [&](double y) { return nombre(d.ymax - d.ymin - y + marge + 8, 3); };
  s << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << nombre(l, 3) << "mm\" height=\"" << nombre(h, 3) << "mm\" viewBox=\"0 0 " << nombre(l, 3) << " " << nombre(h, 3) << "\">\n";
  s << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
  if (!titre.empty()) s << "<text x=\"" << X(0) << "\" y=\"6\" font-family=\"sans-serif\" font-size=\"4\">" << titre << " — ép. " << virgule(nombre(d.epaisseur, 2)) << " mm</text>\n";
  s << "<g fill=\"none\" stroke=\"black\" stroke-width=\"0.25\">\n";
  for (const auto& t : traitsPlats(d)) {
    if (t.cercle) s << "<circle cx=\"" << X(t.cx) << "\" cy=\"" << Y(t.cy) << "\" r=\"" << nombre(t.r, 4) << "\"/>\n";
    else if (t.arc) {
      const double x0 = t.cx + t.r * std::cos(t.a0 * PI / 180), y0 = t.cy + t.r * std::sin(t.a0 * PI / 180);
      const double x1 = t.cx + t.r * std::cos(t.a1 * PI / 180), y1 = t.cy + t.r * std::sin(t.a1 * PI / 180);
      const double bal = std::fmod(t.a1 - t.a0 + 720, 360);
      s << "<path d=\"M " << X(x0) << " " << Y(y0) << " A " << nombre(t.r, 4) << " " << nombre(t.r, 4) << " 0 " << (bal > 180 ? 1 : 0) << " 0 " << X(x1) << " " << Y(y1) << "\"/>\n";
    } else
      s << "<line x1=\"" << X(t.x1) << "\" y1=\"" << Y(t.y1) << "\" x2=\"" << X(t.x2) << "\" y2=\"" << Y(t.y2) << "\"/>\n";
  }
  s << "</g>\n";
  const gp_Ax3& ax = d.repere;
  auto loc = [&](const gp_Pnt& p) { const gp_Vec w(ax.Location(), p); return gp_Pnt2d(w.Dot(gp_Vec(ax.XDirection())) - d.xmin, w.Dot(gp_Vec(ax.YDirection())) - d.ymin); };
  for (const auto& p : d.plis) {
    const gp_Pnt2d a = loc(p.a), b = loc(p.b);
    s << "<line x1=\"" << X(a.X()) << "\" y1=\"" << Y(a.Y()) << "\" x2=\"" << X(b.X()) << "\" y2=\"" << Y(b.Y()) << "\" stroke=\"" << (p.versLeHaut ? "#C2402E" : "#1E5FD6")
      << "\" stroke-width=\"0.2\"" << (p.versLeHaut ? "" : " stroke-dasharray=\"2 1\"") << "/>\n";
    s << "<text x=\"" << X((a.X() + b.X()) / 2) << "\" y=\"" << Y((a.Y() + b.Y()) / 2) << "\" font-family=\"sans-serif\" font-size=\"2.5\" fill=\"#2E7D32\" text-anchor=\"middle\">" << p.note << "</text>\n";
  }
  s << "</svg>\n";
  return s.str();
}

}  // namespace prom

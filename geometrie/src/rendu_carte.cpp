// Prométhée : images et vidéos de la carte (lot L6, tâche 6.18). Voir rendu_carte.hpp.
// Lancer de rayons sur le maillage d'OpenCascade, arbre de boîtes englobantes, ombres et reflets, plusieurs fils.
// Licence GPL-3.0-only.
#include "promethee/rendu_carte.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <set>
#include <thread>

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Trsf.hxx>

#include "promethee/carte3d.hpp"
#include "promethee/distances3d.hpp"

namespace prom {

namespace {
struct V3 {
  double x = 0, y = 0, z = 0;
  V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  V3 operator*(double k) const { return {x * k, y * k, z * k}; }
  double dot(const V3& o) const { return x * o.x + y * o.y + z * o.z; }
  V3 cross(const V3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
  double norme() const { return std::sqrt(dot(*this)); }
  V3 unite() const {
    const double n = norme();
    return n > 0 ? *this * (1 / n) : V3{0, 0, 1};
  }
};
struct Matiere {
  V3 couleur;
  double speculaire = 0.1, brillance = 16, reflet = 0;
};
struct Triangle {
  V3 a, b, c, n;
  int matiere = 0;
};
struct Noeud {
  V3 min, max;
  int gauche = -1, droite = -1, debut = 0, nombre = 0;  // feuille : nombre > 0
};

V3 couleurHex(const std::string& h) {
  unsigned r = 128, g = 128, b = 128;
  if (h.size() == 7 && h[0] == '#') std::sscanf(h.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
  return {r / 255.0, g / 255.0, b / 255.0};
}
// Matière tirée de la couleur : étain et cuivre brillants, corps sombres satinés, vernis mat
Matiere matiereDe(const std::string& h) {
  Matiere m;
  m.couleur = couleurHex(h);
  if (h == "#C8CCD4" || h == "#B0B4BA") {
    m.speculaire = 0.8, m.brillance = 60, m.reflet = 0.35;  // étain, quartz métallique
  } else if (h == "#3A9A66" || h == "#2E8A57") {
    m.speculaire = 0.35, m.brillance = 30, m.reflet = 0.08;  // cuivre sous le vernis
  } else if (m.couleur.x + m.couleur.y + m.couleur.z < 0.6) {
    m.speculaire = 0.3, m.brillance = 24, m.reflet = 0.04;  // corps noirs
  } else {
    m.speculaire = 0.15, m.brillance = 12;
  }
  return m;
}

class Scene {
 public:
  std::vector<Triangle> tris;
  std::vector<Matiere> matieres;
  std::vector<Noeud> noeuds;
  V3 min{1e300, 1e300, 1e300}, max{-1e300, -1e300, -1e300};

  void ajouter(const TopoDS_Shape& s, const Matiere& m, double fleche) {
    if (s.IsNull()) return;
    try {
      BRepMesh_IncrementalMesh maillage(s, fleche, false, 0.35, true);
    } catch (const Standard_Failure&) {
      return;
    }
    const int im = static_cast<int>(matieres.size());
    matieres.push_back(m);
    for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
      const TopoDS_Face f = TopoDS::Face(ex.Current());
      TopLoc_Location loc;
      const Handle(Poly_Triangulation) t = BRep_Tool::Triangulation(f, loc);
      if (t.IsNull()) continue;
      const gp_Trsf tr = loc.Transformation();
      const bool inverse = f.Orientation() == TopAbs_REVERSED;
      for (int i = 1; i <= t->NbTriangles(); ++i) {
        int n1, n2, n3;
        t->Triangle(i).Get(n1, n2, n3);
        if (inverse) std::swap(n2, n3);
        auto p = [&](int k) {
          gp_Pnt q = t->Node(k).Transformed(tr);
          return V3{q.X(), q.Y(), q.Z()};
        };
        Triangle x{p(n1), p(n2), p(n3), {}, im};
        x.n = (x.b - x.a).cross(x.c - x.a).unite();
        for (const V3& v : {x.a, x.b, x.c}) {
          min = {std::min(min.x, v.x), std::min(min.y, v.y), std::min(min.z, v.z)};
          max = {std::max(max.x, v.x), std::max(max.y, v.y), std::max(max.z, v.z)};
        }
        tris.push_back(x);
      }
    }
  }

  void construire() {
    noeuds.clear();
    if (tris.empty()) return;
    noeuds.reserve(tris.size() * 2);
    construire(0, static_cast<int>(tris.size()));
  }

  // Premier triangle touché par le rayon o + t d, t dans ]tmin, tmax[ ; −1 sans contact
  int toucher(const V3& o, const V3& d, double tmin, double& tmax, bool premierSuffit = false) const {
    if (noeuds.empty()) return -1;
    const V3 inv{1 / (std::abs(d.x) > 1e-15 ? d.x : 1e-15), 1 / (std::abs(d.y) > 1e-15 ? d.y : 1e-15), 1 / (std::abs(d.z) > 1e-15 ? d.z : 1e-15)};
    int pile[64];
    int n = 0, meilleur = -1;
    pile[n++] = 0;
    while (n > 0) {
      const Noeud& q = noeuds[static_cast<std::size_t>(pile[--n])];
      if (!boite(q, o, inv, tmin, tmax)) continue;
      if (q.nombre > 0) {
        for (int i = q.debut; i < q.debut + q.nombre; ++i) {
          double t;
          if (intersecter(tris[static_cast<std::size_t>(i)], o, d, t) && t > tmin && t < tmax) {
            tmax = t;
            meilleur = i;
            if (premierSuffit) return meilleur;
          }
        }
      } else if (n < 62) {
        pile[n++] = q.gauche;
        pile[n++] = q.droite;
      }
    }
    return meilleur;
  }

 private:
  int construire(int debut, int fin) {
    const int id = static_cast<int>(noeuds.size());
    noeuds.push_back({});
    Noeud q;
    q.min = {1e300, 1e300, 1e300};
    q.max = {-1e300, -1e300, -1e300};
    for (int i = debut; i < fin; ++i)
      for (const V3& v : {tris[static_cast<std::size_t>(i)].a, tris[static_cast<std::size_t>(i)].b, tris[static_cast<std::size_t>(i)].c}) {
        q.min = {std::min(q.min.x, v.x), std::min(q.min.y, v.y), std::min(q.min.z, v.z)};
        q.max = {std::max(q.max.x, v.x), std::max(q.max.y, v.y), std::max(q.max.z, v.z)};
      }
    if (fin - debut <= 4) {
      q.debut = debut;
      q.nombre = fin - debut;
      noeuds[static_cast<std::size_t>(id)] = q;
      return id;
    }
    const V3 e = q.max - q.min;
    const int axe = e.x > e.y && e.x > e.z ? 0 : e.y > e.z ? 1 : 2;
    auto centre = [axe](const Triangle& t) { return axe == 0 ? t.a.x + t.b.x + t.c.x : axe == 1 ? t.a.y + t.b.y + t.c.y : t.a.z + t.b.z + t.c.z; };
    const int milieu = (debut + fin) / 2;
    std::nth_element(tris.begin() + debut, tris.begin() + milieu, tris.begin() + fin, [&](const Triangle& a, const Triangle& b) { return centre(a) < centre(b); });
    q.gauche = construire(debut, milieu);
    q.droite = construire(milieu, fin);
    noeuds[static_cast<std::size_t>(id)] = q;
    return id;
  }
  static bool boite(const Noeud& q, const V3& o, const V3& inv, double tmin, double tmax) {
    double t0 = (q.min.x - o.x) * inv.x, t1 = (q.max.x - o.x) * inv.x;
    if (t0 > t1) std::swap(t0, t1);
    tmin = std::max(tmin, t0), tmax = std::min(tmax, t1);
    t0 = (q.min.y - o.y) * inv.y, t1 = (q.max.y - o.y) * inv.y;
    if (t0 > t1) std::swap(t0, t1);
    tmin = std::max(tmin, t0), tmax = std::min(tmax, t1);
    t0 = (q.min.z - o.z) * inv.z, t1 = (q.max.z - o.z) * inv.z;
    if (t0 > t1) std::swap(t0, t1);
    tmin = std::max(tmin, t0), tmax = std::min(tmax, t1);
    return tmin <= tmax;
  }
  // Möller–Trumbore
  static bool intersecter(const Triangle& x, const V3& o, const V3& d, double& t) {
    const V3 e1 = x.b - x.a, e2 = x.c - x.a, p = d.cross(e2);
    const double det = e1.dot(p);
    if (std::abs(det) < 1e-14) return false;
    const double inv = 1 / det;
    const V3 s = o - x.a;
    const double u = s.dot(p) * inv;
    if (u < 0 || u > 1) return false;
    const V3 q = s.cross(e1);
    const double v = d.dot(q) * inv;
    if (v < 0 || u + v > 1) return false;
    t = e2.dot(q) * inv;
    return true;
  }
};

TopoDS_Shape lever(const TopoDS_Shape& s, double dz) {
  if (s.IsNull() || std::abs(dz) < 1e-12) return s;
  gp_Trsf t;
  t.SetTranslation(gp_Vec(0, 0, dz));
  return BRepBuilderAPI_Transform(s, t, true).Shape();
}

struct Camera {
  V3 oeil, avant, droite, haut;
  bool ortho = false;
  double demiHauteur = 1;  // ortho : demi-hauteur de la vue (mm) ; perspective : tan(champ / 2)
};
Camera camera(const ecad::PointDeVue& v, double rapport) {
  (void)rapport;
  Camera c;
  const double a = v.azimut * std::numbers::pi / 180, e = v.elevation * std::numbers::pi / 180;
  const V3 cible{v.cible[0], v.cible[1], v.cible[2]};
  const V3 dir{std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e)};
  c.oeil = cible + dir * v.distance;
  c.avant = (cible - c.oeil).unite();
  V3 haut{0, 0, 1};
  if (std::abs(c.avant.dot(haut)) > 0.999) haut = {0, 1, 0};
  c.droite = c.avant.cross(haut).unite();
  c.haut = c.droite.cross(c.avant).unite();
  c.ortho = v.orthographique;
  const double champ = std::clamp(v.champ, 5.0, 120.0) * std::numbers::pi / 180;
  c.demiHauteur = c.ortho ? v.distance * std::tan(champ / 2) : std::tan(champ / 2);
  return c;
}
}  // namespace

bool ImageRendu::alphaPresent() const {
  bool transparent = false, opaque = false;
  for (std::size_t i = 3; i < rgba.size(); i += 4) {
    transparent |= rgba[i] == 0;
    opaque |= rgba[i] == 255;
    if (transparent && opaque) return true;
  }
  return false;
}

ecad::PointDeVue vueDeTroisQuarts(const ecad::ConceptionElec& c) {
  const ecad::Boite b = ecad::boite(c.circuit.contour.polygone());
  ecad::PointDeVue v;
  v.nom = "Trois quarts";
  v.cible[0] = ecad::mm(b.x0 + b.x1) / 2;
  v.cible[1] = -ecad::mm(b.y0 + b.y1) / 2;
  v.cible[2] = 0;
  v.azimut = -60;
  v.elevation = 40;
  v.champ = 30;
  v.distance = std::max(ecad::mm(b.largeur()), ecad::mm(b.hauteur())) * 2.4 + 10;
  return v;
}

ImageRendu rendreCarte(const ecad::ConceptionElec& c, const OptionsRendu& o, const std::atomic<bool>* arret, const std::function<void(double)>& progres) {
  const auto debut = std::chrono::steady_clock::now();
  ImageRendu img;
  img.largeur = std::max(1, o.largeur);
  img.hauteur = std::max(1, o.hauteur);
  img.rgba.assign(static_cast<std::size_t>(img.largeur) * static_cast<std::size_t>(img.hauteur) * 4, 0);
  // Scène : carte équipée, pièces montées, boîtier importé
  Scene sc;
  const Carte3D carte = modele3DCarte(c);
  for (const auto& [forme, couleur] : carte.couleurs) {
    if (arret && arret->load()) {
      img.interrompu = true;
      return img;
    }
    sc.ajouter(forme, matiereDe(couleur), o.precision);
  }
  for (const auto& m : c.piecesMontees) {
    const auto it = o.leveePieces.find(m.uuid);
    sc.ajouter(lever(formePieceMontee(c, m), it == o.leveePieces.end() ? 0 : it->second), matiereDe("#B0B4BA"), o.precision);
  }
  if (o.boitier && c.boitierImporte) {
    Matiere m = matiereDe("#D8DCE2");
    m.speculaire = 0.2;
    sc.ajouter(lever(formeBoitierImporte(c), o.leveeBoitier), m, o.precision * 2);
  }
  sc.construire();
  img.triangles = static_cast<int>(sc.tris.size());
  // Lumières : celles demandées, huit au plus ; sinon trois lumières de studio
  std::vector<LumiereRendu> lum(o.lumieres.begin(), o.lumieres.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(o.lumieres.size(), 8)));
  if (lum.empty()) {
    LumiereRendu cle, appoint, contre;
    cle.direction[0] = -0.5, cle.direction[1] = 0.6, cle.direction[2] = -1, cle.intensite = 0.75;
    appoint.direction[0] = 0.7, appoint.direction[1] = 0.3, appoint.direction[2] = -0.6, appoint.intensite = 0.35;
    contre.direction[0] = 0.2, contre.direction[1] = -0.9, contre.direction[2] = -0.4, contre.intensite = 0.25;
    lum = {cle, appoint, contre};
  }
  std::vector<V3> versLumiere;
  for (const auto& l : lum) versLumiere.push_back((V3{l.direction[0], l.direction[1], l.direction[2]} * -1).unite());
  const double eps = std::max(1e-4, (sc.max - sc.min).norme() * 1e-6);
  const V3 ciel{0.82, 0.85, 0.9};
  const Camera cam = camera(o.vue, static_cast<double>(img.largeur) / img.hauteur);
  const double rapport = static_cast<double>(img.largeur) / img.hauteur;
  // Couleur d'un rayon ; « touche » : le rayon primaire a rencontré la scène
  std::function<V3(const V3&, const V3&, int, bool&)> tracer = [&](const V3& o0, const V3& d, int profondeur, bool& touche) -> V3 {
    double t = 1e300;
    const int i = sc.toucher(o0, d, eps, t);
    if (i < 0) {
      touche = false;
      return ciel;
    }
    touche = true;
    const Triangle& tr = sc.tris[static_cast<std::size_t>(i)];
    const Matiere& m = sc.matieres[static_cast<std::size_t>(tr.matiere)];
    const V3 p = o0 + d * t;
    V3 n = tr.n;
    if (n.dot(d) > 0) n = n * -1;
    const V3 vue = d * -1;
    V3 col = m.couleur * 0.2;
    for (std::size_t k = 0; k < lum.size(); ++k) {
      const V3& L = versLumiere[k];
      const double diffus = n.dot(L);
      if (diffus <= 0) continue;
      if (o.ombres) {
        double tl = 1e300;
        if (sc.toucher(p + n * eps * 10, L, eps, tl, true) >= 0) continue;
      }
      const V3 h = (L + vue).unite();
      const double spec = m.speculaire * std::pow(std::max(0.0, n.dot(h)), m.brillance);
      const V3 lc{lum[k].couleur[0] * lum[k].intensite, lum[k].couleur[1] * lum[k].intensite, lum[k].couleur[2] * lum[k].intensite};
      col = col + V3{m.couleur.x * lc.x * diffus + spec * lc.x, m.couleur.y * lc.y * diffus + spec * lc.y, m.couleur.z * lc.z * diffus + spec * lc.z};
    }
    if (o.reflets && m.reflet > 0 && profondeur < 2) {
      const V3 r = (d - n * (2 * d.dot(n))).unite();
      bool t2 = false;
      const V3 rc = tracer(p + n * eps * 10, r, profondeur + 1, t2);
      col = col * (1 - m.reflet) + rc * m.reflet;
    }
    return col;
  };
  const int ech = std::clamp(o.echantillons, 1, 4);
  std::atomic<int> ligne{0}, faites{0};
  auto travail = [&] {
    for (int y = ligne++; y < img.hauteur; y = ligne++) {
      if (arret && arret->load()) return;
      for (int x = 0; x < img.largeur; ++x) {
        V3 somme;
        int touches = 0;
        for (int sy = 0; sy < ech; ++sy)
          for (int sx = 0; sx < ech; ++sx) {
            const double u = ((x + (sx + 0.5) / ech) / img.largeur * 2 - 1) * rapport, v = 1 - (y + (sy + 0.5) / ech) / img.hauteur * 2;
            V3 oo, d;
            if (cam.ortho) {
              oo = cam.oeil + cam.droite * (u * cam.demiHauteur) + cam.haut * (v * cam.demiHauteur);
              d = cam.avant;
            } else {
              oo = cam.oeil;
              d = (cam.avant + cam.droite * (u * cam.demiHauteur) + cam.haut * (v * cam.demiHauteur)).unite();
            }
            bool touche = false;
            const V3 col = tracer(oo, d, 0, touche);
            if (touche) {
              somme = somme + col;
              ++touches;
            }
          }
        const std::size_t k = (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.largeur) + static_cast<std::size_t>(x)) * 4;
        const int n = ech * ech;
        const double couverture = static_cast<double>(touches) / n;
        if (touches > 0) {
          const V3 moy = somme * (1.0 / touches);
          auto octet = [](double v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0, 1.0) * 255 + 0.5); };
          if (o.fondTransparent) {
            img.rgba[k] = octet(moy.x), img.rgba[k + 1] = octet(moy.y), img.rgba[k + 2] = octet(moy.z);
            img.rgba[k + 3] = octet(couverture);
          } else {
            const V3 f{((o.fond >> 16) & 0xFF) / 255.0, ((o.fond >> 8) & 0xFF) / 255.0, (o.fond & 0xFF) / 255.0};
            const V3 m = moy * couverture + f * (1 - couverture);
            img.rgba[k] = octet(m.x), img.rgba[k + 1] = octet(m.y), img.rgba[k + 2] = octet(m.z), img.rgba[k + 3] = 255;
          }
        } else if (!o.fondTransparent) {
          img.rgba[k] = static_cast<std::uint8_t>((o.fond >> 16) & 0xFF), img.rgba[k + 1] = static_cast<std::uint8_t>((o.fond >> 8) & 0xFF), img.rgba[k + 2] = static_cast<std::uint8_t>(o.fond & 0xFF);
          img.rgba[k + 3] = 255;
        }
      }
      const int f = ++faites;
      if (progres && (f % 16 == 0 || f == img.hauteur)) progres(static_cast<double>(f) / img.hauteur);
    }
  };
  const int nf = o.fils > 0 ? o.fils : std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
  std::vector<std::thread> fils;
  for (int i = 1; i < nf; ++i) fils.emplace_back(travail);
  travail();
  for (auto& f : fils) f.join();
  img.interrompu = arret && arret->load() && faites.load() < img.hauteur;
  img.secondes = std::chrono::duration<double>(std::chrono::steady_clock::now() - debut).count();
  return img;
}

double dureeAnimation(const std::vector<ImageCle>& cles) {
  double d = 0;
  for (const auto& c : cles) d = std::max(d, c.t);
  return d;
}

EtatAnimation etatAnimation(const std::vector<ImageCle>& cles0, double t) {
  EtatAnimation e;
  if (cles0.empty()) return e;
  std::vector<ImageCle> cles = cles0;
  std::stable_sort(cles.begin(), cles.end(), [](const ImageCle& a, const ImageCle& b) { return a.t < b.t; });
  if (t <= cles.front().t) return {cles.front().vue, cles.front().leveeBoitier, cles.front().leveePieces};
  if (t >= cles.back().t) return {cles.back().vue, cles.back().leveeBoitier, cles.back().leveePieces};
  std::size_t i = 0;
  while (i + 1 < cles.size() && cles[i + 1].t < t) ++i;
  const ImageCle& a = cles[i];
  const ImageCle& b = cles[i + 1];
  const double u0 = b.t > a.t ? (t - a.t) / (b.t - a.t) : 1, u = u0 * u0 * (3 - 2 * u0);  // départ et arrivée adoucis
  auto lin = [u](double x, double y) { return x + (y - x) * u; };
  auto angle = [u](double x, double y) {
    double d = std::fmod(y - x + 540.0, 360.0) - 180.0;  // le plus court chemin
    return x + d * u;
  };
  e.vue = a.vue;
  e.vue.nom.clear();
  for (int k = 0; k < 3; ++k) e.vue.cible[k] = lin(a.vue.cible[k], b.vue.cible[k]);
  e.vue.azimut = angle(a.vue.azimut, b.vue.azimut);
  e.vue.elevation = lin(a.vue.elevation, b.vue.elevation);
  e.vue.distance = lin(a.vue.distance, b.vue.distance);
  e.vue.champ = lin(a.vue.champ, b.vue.champ);
  e.vue.orthographique = u < 0.5 ? a.vue.orthographique : b.vue.orthographique;
  e.leveeBoitier = lin(a.leveeBoitier, b.leveeBoitier);
  std::set<std::string> ids;
  for (const auto& [k, v] : a.leveePieces) ids.insert(k);
  for (const auto& [k, v] : b.leveePieces) ids.insert(k);
  for (const auto& k : ids) {
    const double x = a.leveePieces.count(k) ? a.leveePieces.at(k) : 0, y = b.leveePieces.count(k) ? b.leveePieces.at(k) : 0;
    e.leveePieces[k] = lin(x, y);
  }
  return e;
}

}  // namespace prom

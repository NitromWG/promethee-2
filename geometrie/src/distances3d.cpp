// Prométhée : distances exactes de la carte équipée (lot L6, 6.7 et 6.12). Voir distances3d.hpp.
// Licence GPL-3.0-only.
#include "promethee/distances3d.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <gp_Trsf.hxx>

#include "promethee/carte3d.hpp"
#include "promethee/expressions.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/regles_carte.hpp"

namespace prom {

using namespace ecad;

namespace {
Bnd_Box boiteDe(const TopoDS_Shape& s) {
  Bnd_Box b;
  if (!s.IsNull()) BRepBndLib::Add(s, b);
  return b;
}
// Distance entre deux boîtes (0 si elles se recoupent)
double distanceBoites(const Bnd_Box& a, const Bnd_Box& b) {
  if (a.IsVoid() || b.IsVoid()) return 1e300;
  double ax0, ay0, az0, ax1, ay1, az1, bx0, by0, bz0, bx1, by1, bz1;
  a.Get(ax0, ay0, az0, ax1, ay1, az1);
  b.Get(bx0, by0, bz0, bx1, by1, bz1);
  const double dx = std::max({0.0, bx0 - ax1, ax0 - bx1}), dy = std::max({0.0, by0 - ay1, ay0 - by1}), dz = std::max({0.0, bz0 - az1, az0 - bz1});
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}
std::string texteMm(double v) {
  char t[32];
  std::snprintf(t, sizeof t, "%.2f mm", v);
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
// Modèle d'une pièce ou d'un boîtier : forme exacte lue de son texte BRep
TopoDS_Shape modele(const ConceptionElec& c, const std::string& id) {
  const auto it = c.modeles3D.find(id);
  if (it == c.modeles3D.end() || it->second.brep.empty()) return {};
  try {
    return formeDepuisBrep(it->second.brep);
  } catch (const Standard_Failure&) {
    return {};
  }
}
TopoDS_Shape transformer(const TopoDS_Shape& s, const gp_Trsf& t) { return s.IsNull() ? s : BRepBuilderAPI_Transform(s, t, true).Shape(); }
}  // namespace

TopoDS_Shape formePieceMontee(const ConceptionElec& c, const PieceMontee& m) {
  TopoDS_Shape s = modele(c, m.modele);
  if (s.IsNull()) return s;
  const Modele3D& md = c.modeles3D.at(m.modele);
  // Comme un corps de composant : boîte brute centrée et posée, tournée, placée sur la face ; retournée au dessous
  const double t = c.circuit.empilage.epaisseur, cu = std::max(0.035, c.circuit.empilage.cuivre);
  gp_Trsf centrer, tourner, placer, retourner;
  centrer.SetTranslation(gp_Vec(-(md.x0 + md.x1) / 2, -(md.y0 + md.y1) / 2, -md.z0));
  tourner.SetRotation(gp::OZ(), m.rotation * std::numbers::pi / 180);
  placer.SetTranslation(gp_Vec(m.x, -m.y, cu + m.z));
  s = transformer(transformer(transformer(s, centrer), tourner), placer);
  if (m.dessous) {
    retourner.SetMirror(gp_Ax2(gp_Pnt(0, 0, -t / 2), gp::DZ()));
    s = transformer(s, retourner);
  }
  return s;
}

TopoDS_Shape formeBoitierImporte(const ConceptionElec& c) {
  if (!c.boitierImporte) return {};
  const BoitierImporte& b = *c.boitierImporte;
  TopoDS_Shape s = modele(c, b.modele);
  gp_Trsf tourner, placer;
  tourner.SetRotation(gp::OZ(), b.rotation * std::numbers::pi / 180);
  placer.SetTranslation(gp_Vec(b.x, -b.y, b.z));
  return transformer(transformer(s, tourner), placer);
}

std::vector<CorpsCarte> corpsCarte(const ConceptionElec& c) {
  std::vector<CorpsCarte> r;
  for (const auto& e : c.circuit.empreintes) {
    const ComposantElec* x = c.composant(e.composant);
    if (!x || x->nePasMonter) continue;
    TopoDS_Shape s = corpsComposant(c, e);
    if (!s.IsNull()) r.push_back({CorpsCarte::Genre::Composant, x->uuid, x->reference, s});
  }
  for (const auto& m : c.piecesMontees)
    if (TopoDS_Shape s = formePieceMontee(c, m); !s.IsNull()) r.push_back({CorpsCarte::Genre::Piece, m.uuid, m.reference.empty() ? m.nom : m.reference, s});
  if (TopoDS_Shape s = formeBoitierImporte(c); !s.IsNull()) r.push_back({CorpsCarte::Genre::Boitier, "boitier", c.boitierImporte->nom.empty() ? std::string("boîtier importé") : c.boitierImporte->nom, s});
  return r;
}

double distanceExacte(const TopoDS_Shape& a, const TopoDS_Shape& b, gp_Pnt* pa, gp_Pnt* pb, double borne) {
  if (a.IsNull() || b.IsNull()) return -1;
  if (borne >= 0) {
    const double db = distanceBoites(boiteDe(a), boiteDe(b));
    if (db > borne) return db;
  }
  try {
    BRepExtrema_DistShapeShape d(a, b);
    if (!d.IsDone() || d.NbSolution() < 1) return -1;
    if (pa) *pa = d.PointOnShape1(1);
    if (pb) *pb = d.PointOnShape2(1);
    // Un corps dans la matière de l'autre : distance nulle
    return d.InnerSolution() ? 0.0 : d.Value();
  } catch (const Standard_Failure&) {
    return -1;
  }
}

MesureCorps mesurerCorps(const ConceptionElec& c, const std::string& idA, const std::string& idB) {
  MesureCorps r;
  const auto corps = corpsCarte(c);
  const auto a = std::find_if(corps.begin(), corps.end(), [&](const CorpsCarte& x) { return x.id == idA; });
  const auto b = std::find_if(corps.begin(), corps.end(), [&](const CorpsCarte& x) { return x.id == idB; });
  if (a == corps.end() || b == corps.end()) return r;
  ElementMesure ea, eb;
  ea.forme = a->forme;
  ea.nom = a->nom;
  eb.forme = b->forme;
  eb.nom = b->nom;
  r.mesure = mesurer({ea, eb});
  // Distance à la carte : le point le plus bas d'un corps du dessus au-dessus de la face (z = 0), le plus haut d'un
  // corps du dessous sous la face du dessous (z = −épaisseur)
  const double t = c.circuit.empilage.epaisseur;
  auto aLaCarte = [&](const TopoDS_Shape& s) {
    double x0, y0, z0, x1, y1, z1;
    const Bnd_Box bb = boiteDe(s);
    if (bb.IsVoid()) return 0.0;
    bb.Get(x0, y0, z0, x1, y1, z1);
    return z0 >= -t / 2 ? std::max(0.0, z0) : std::max(0.0, -t - z1);
  };
  r.distanceCarteA = aLaCarte(a->forme);
  r.distanceCarteB = aLaCarte(b->forme);
  return r;
}

std::vector<Violation> verifierDistances3D(const ConceptionElec& c) {
  std::vector<Violation> v;
  const auto& contraintes = c.circuit.regles.avancees.contraintes;
  const bool ecarts = std::any_of(contraintes.begin(), contraintes.end(), [](const Contrainte& k) { return k.active && k.genre == GenreContrainte::Ecart3D && k.valeur.min > 0; });
  if (!ecarts && !c.boitierImporte) return v;
  std::vector<CorpsCarte> corps = corpsCarte(c);
  std::vector<Bnd_Box> boites;
  for (const auto& x : corps) boites.push_back(boiteDe(x.forme));
  auto centre = [](const gp_Pnt& p, const gp_Pnt& q) { return ptMm((p.X() + q.X()) / 2, -(p.Y() + q.Y()) / 2); };
  // DRC-47 : corps qui heurte le boîtier importé, ou s'en approche sous son jeu
  if (c.boitierImporte) {
    const auto it = std::find_if(corps.begin(), corps.end(), [](const CorpsCarte& x) { return x.genre == CorpsCarte::Genre::Boitier; });
    if (it == corps.end()) {
      Violation x;
      x.gravite = Gravite::Avertissement;
      x.code = "DRC-47";
      x.message = "Boîtier importé sans modèle lisible : ses collisions ne sont pas contrôlées";
      x.conseil = "Importe à nouveau son fichier STEP.";
      v.push_back(x);
    } else {
      const double jeu = std::max(0.0, c.boitierImporte->jeu);
      const std::size_t ib = static_cast<std::size_t>(it - corps.begin());
      for (std::size_t i = 0; i < corps.size(); ++i) {
        if (i == ib) continue;
        gp_Pnt p, q;
        const double d = distanceExacte(corps[i].forme, corps[ib].forme, &p, &q, jeu + 1e-6);
        if (d < 0 || d >= jeu) continue;
        Violation x;
        x.gravite = Gravite::Erreur;
        x.code = "DRC-47";
        x.message = d <= 1e-6 ? corps[i].nom + " heurte le boîtier « " + corps[ib].nom + " »"
                              : corps[i].nom + " à " + texteMm(d) + " du boîtier « " + corps[ib].nom + " » (jeu de " + texteMm(jeu) + ")";
        x.conseil = "Déplace ou abaisse le composant, ou choisis un boîtier plus grand.";
        x.position = centre(p, q);
        x.objets = {corps[i].id};
        v.push_back(x);
      }
    }
  }
  // DRC-75 : écart 3D entre deux corps sous la contrainte qui les vise
  for (const auto& k : contraintes) {
    if (!k.active || k.genre != GenreContrainte::Ecart3D || k.valeur.min <= 0) continue;
    const double min = static_cast<double>(k.valeur.min) * 1e-6;
    const Expression ea = Expression::compiler(k.portee), eb = Expression::compiler(k.porteeB.empty() ? k.portee : k.porteeB);
    auto vise = [&](const Expression& e, const CorpsCarte& x) {
      if (x.genre == CorpsCarte::Genre::Boitier) return false;
      if (x.genre == CorpsCarte::Genre::Piece) return k.portee.empty();
      const ComposantElec* cx = c.composant(x.id);
      return cx && e.valide() && e.evaluer(proprietesComposant(c, *cx));
    };
    for (std::size_t i = 0; i < corps.size(); ++i)
      for (std::size_t j = i + 1; j < corps.size(); ++j) {
        if (!((vise(ea, corps[i]) && vise(eb, corps[j])) || (vise(eb, corps[i]) && vise(ea, corps[j])))) continue;
        if (distanceBoites(boites[i], boites[j]) >= min) continue;
        gp_Pnt p, q;
        const double d = distanceExacte(corps[i].forme, corps[j].forme, &p, &q);
        if (d < 0 || d >= min - 1e-9) continue;
        Violation x;
        x.gravite = Gravite::Erreur;
        x.code = "DRC-75";
        x.message = "Corps de " + corps[i].nom + " et " + corps[j].nom + " à " + texteMm(d) + " pour " + texteMm(min) + " au moins (« " + k.nom + " »)";
        x.conseil = "Écarte les deux composants : la mesure est faite sur leurs formes exactes.";
        x.position = centre(p, q);
        x.objets = {corps[i].id, corps[j].id};
        v.push_back(x);
      }
  }
  return v;
}

}  // namespace prom

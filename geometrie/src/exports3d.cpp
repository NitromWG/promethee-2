// Prométhée : exports 3D (10.10, 10.24). Voir exports3d.hpp. Licence GPL-3.0-only.
#include "promethee/exports3d.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <sstream>

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>

#include <nlohmann/json.hpp>

#include "promethee/archive.hpp"
#include "promethee/compression.hpp"
#include "promethee/fichiers.hpp"

namespace prom {

namespace {
// Maillage d'une pièce par couleur : chaque face garde ses sommets (arêtes vives), normales lissées dans la face
struct Lot {
  std::array<double, 3> couleur{0.8, 0.8, 0.8};
  std::vector<std::array<float, 3>> positions, normales;  // mm, repère de la carte (z au-dessus)
  std::vector<uint32_t> indices;
};
struct PieceMaillee {
  std::string nom;
  std::vector<Lot> lots;
};
std::string nomPropre(const std::string& n, const std::string& defaut) {
  std::string r;
  for (unsigned char ch : n) r += (std::isalnum(ch) || ch >= 0x80 || ch == '-' || ch == '.') ? static_cast<char>(ch) : '_';
  return r.empty() ? defaut : r;
}
std::string echapperXml(const std::string& s) {
  std::string r;
  for (char ch : s) {
    switch (ch) {
      case '&': r += "&amp;"; break;
      case '<': r += "&lt;"; break;
      case '>': r += "&gt;"; break;
      case '"': r += "&quot;"; break;
      default: r += ch;
    }
  }
  return r;
}
std::string nb(double v, int dec = 5) {
  char t[48];
  std::snprintf(t, sizeof t, "%.*f", dec, std::abs(v) < 1e-12 ? 0.0 : v);
  std::string s = t;
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
  }
  return s == "-0" ? "0" : s;
}
std::vector<PieceMaillee> mailler(const std::vector<PieceNommee>& pieces, double fleche) {
  std::vector<PieceMaillee> r;
  for (const auto& p : pieces) {
    if (p.forme.IsNull()) continue;
    BRepMesh_IncrementalMesh m(p.forme, fleche, false, 0.25, true);
    PieceMaillee pm;
    pm.nom = p.nom;
    std::map<std::array<int, 3>, std::size_t> parCouleur;
    auto lot = [&](const std::array<double, 3>& c) -> Lot& {
      const std::array<int, 3> cle{static_cast<int>(std::lround(c[0] * 255)), static_cast<int>(std::lround(c[1] * 255)), static_cast<int>(std::lround(c[2] * 255))};
      auto it = parCouleur.find(cle);
      if (it == parCouleur.end()) {
        it = parCouleur.emplace(cle, pm.lots.size()).first;
        pm.lots.push_back({});
        pm.lots.back().couleur = c;
      }
      return pm.lots[it->second];
    };
    for (TopExp_Explorer ex(p.forme, TopAbs_FACE); ex.More(); ex.Next()) {
      const TopoDS_Face& f = TopoDS::Face(ex.Current());
      TopLoc_Location loc;
      const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(f, loc);
      if (tri.IsNull()) continue;
      std::array<double, 3> couleur{p.r, p.g, p.b};
      for (const auto& [face, c] : p.couleursFaces)
        if (face.IsPartner(f)) {
          couleur = c;
          break;
        }
      Lot& l = lot(couleur);
      const gp_Trsf tr = loc.Transformation();
      const uint32_t base = static_cast<uint32_t>(l.positions.size());
      std::vector<std::array<double, 3>> n(static_cast<std::size_t>(tri->NbNodes()), {0, 0, 0});
      for (int i = 1; i <= tri->NbNodes(); ++i) {
        const gp_Pnt q = tri->Node(i).Transformed(tr);
        l.positions.push_back({static_cast<float>(q.X()), static_cast<float>(q.Y()), static_cast<float>(q.Z())});
      }
      const bool inverse = f.Orientation() == TopAbs_REVERSED;
      for (int i = 1; i <= tri->NbTriangles(); ++i) {
        int a = 0, b = 0, c = 0;
        tri->Triangle(i).Get(a, b, c);
        if (inverse) std::swap(b, c);
        if (a == b || b == c || a == c) continue;
        const auto& A = l.positions[base + static_cast<uint32_t>(a - 1)];
        const auto& B = l.positions[base + static_cast<uint32_t>(b - 1)];
        const auto& C = l.positions[base + static_cast<uint32_t>(c - 1)];
        const double ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2], vx = C[0] - A[0], vy = C[1] - A[1], vz = C[2] - A[2];
        const std::array<double, 3> nt{uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
        for (int k : {a, b, c})
          for (int d = 0; d < 3; ++d) n[static_cast<std::size_t>(k - 1)][static_cast<std::size_t>(d)] += nt[static_cast<std::size_t>(d)];
        l.indices.push_back(base + static_cast<uint32_t>(a - 1));
        l.indices.push_back(base + static_cast<uint32_t>(b - 1));
        l.indices.push_back(base + static_cast<uint32_t>(c - 1));
      }
      for (const auto& v : n) {
        const double len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        l.normales.push_back(len > 1e-20 ? std::array<float, 3>{static_cast<float>(v[0] / len), static_cast<float>(v[1] / len), static_cast<float>(v[2] / len)} : std::array<float, 3>{0, 0, 1});
      }
    }
    pm.lots.erase(std::remove_if(pm.lots.begin(), pm.lots.end(), [](const Lot& l) { return l.indices.empty(); }), pm.lots.end());
    if (!pm.lots.empty()) r.push_back(std::move(pm));
  }
  return r;
}
double lineaire(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
bool metal(const std::array<double, 3>& c) { return c[0] > 0.6 && std::abs(c[0] - c[1]) < 0.12 && std::abs(c[1] - c[2]) < 0.12; }
void ajouter(std::string& s, const void* d, std::size_t n) { s.append(static_cast<const char*>(d), n); }
void bourrer(std::string& s, char c) {
  while (s.size() % 4) s += c;
}

// ---------- GLB ----------
std::string glb(const std::vector<PieceMaillee>& pieces) {
  using json = nlohmann::json;
  json j;
  j["asset"] = {{"version", "2.0"}, {"generator", "Prométhée"}};
  j["scene"] = 0;
  json noeuds = json::array(), maillages = json::array(), accesseurs = json::array(), vues = json::array(), materiaux = json::array();
  std::map<std::array<int, 3>, int> parCouleur;
  std::string bin;
  auto vue = [&](const void* d, std::size_t n, int cible) {
    bourrer(bin, '\0');
    const std::size_t debut = bin.size();
    ajouter(bin, d, n);
    vues.push_back({{"buffer", 0}, {"byteOffset", debut}, {"byteLength", n}, {"target", cible}});
    return static_cast<int>(vues.size()) - 1;
  };
  for (const auto& p : pieces) {
    json prims = json::array();
    for (const auto& l : p.lots) {
      // glTF : y vers le haut, en mètres ; la carte : z au-dessus, en millimètres
      std::vector<float> pos, nor;
      std::array<float, 3> mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
      for (std::size_t i = 0; i < l.positions.size(); ++i) {
        const auto& q = l.positions[i];
        const std::array<float, 3> g{q[0] / 1000.f, q[2] / 1000.f, -q[1] / 1000.f};
        for (int d = 0; d < 3; ++d) {
          mn[static_cast<std::size_t>(d)] = std::min(mn[static_cast<std::size_t>(d)], g[static_cast<std::size_t>(d)]);
          mx[static_cast<std::size_t>(d)] = std::max(mx[static_cast<std::size_t>(d)], g[static_cast<std::size_t>(d)]);
        }
        pos.insert(pos.end(), g.begin(), g.end());
        const auto& nn = l.normales[i];
        nor.insert(nor.end(), {nn[0], nn[2], -nn[1]});
      }
      const int vp = vue(pos.data(), pos.size() * 4, 34962), vn = vue(nor.data(), nor.size() * 4, 34962), vi = vue(l.indices.data(), l.indices.size() * 4, 34963);
      accesseurs.push_back({{"bufferView", vp}, {"componentType", 5126}, {"count", l.positions.size()}, {"type", "VEC3"}, {"min", {mn[0], mn[1], mn[2]}}, {"max", {mx[0], mx[1], mx[2]}}});
      accesseurs.push_back({{"bufferView", vn}, {"componentType", 5126}, {"count", l.normales.size()}, {"type", "VEC3"}});
      accesseurs.push_back({{"bufferView", vi}, {"componentType", 5125}, {"count", l.indices.size()}, {"type", "SCALAR"}});
      const int a0 = static_cast<int>(accesseurs.size()) - 3;
      const std::array<int, 3> cle{static_cast<int>(std::lround(l.couleur[0] * 255)), static_cast<int>(std::lround(l.couleur[1] * 255)), static_cast<int>(std::lround(l.couleur[2] * 255))};
      auto it = parCouleur.find(cle);
      if (it == parCouleur.end()) {
        char nom[16];
        std::snprintf(nom, sizeof nom, "#%02X%02X%02X", cle[0], cle[1], cle[2]);
        materiaux.push_back({{"name", nom},
                             {"pbrMetallicRoughness",
                              {{"baseColorFactor", {lineaire(l.couleur[0]), lineaire(l.couleur[1]), lineaire(l.couleur[2]), 1.0}}, {"metallicFactor", metal(l.couleur) ? 0.8 : 0.0}, {"roughnessFactor", metal(l.couleur) ? 0.35 : 0.6}}}});
        it = parCouleur.emplace(cle, static_cast<int>(materiaux.size()) - 1).first;
      }
      prims.push_back({{"attributes", {{"POSITION", a0}, {"NORMAL", a0 + 1}}}, {"indices", a0 + 2}, {"material", it->second}, {"mode", 4}});
    }
    maillages.push_back({{"name", p.nom}, {"primitives", prims}});
    noeuds.push_back({{"name", p.nom}, {"mesh", static_cast<int>(maillages.size()) - 1}});
  }
  json scene = json::array();
  for (std::size_t i = 0; i < noeuds.size(); ++i) scene.push_back(i);
  j["scenes"] = json::array({{{"name", "Carte"}, {"nodes", scene}}});
  j["nodes"] = noeuds;
  j["meshes"] = maillages;
  j["materials"] = materiaux;
  j["accessors"] = accesseurs;
  j["bufferViews"] = vues;
  bourrer(bin, '\0');
  j["buffers"] = json::array({{{"byteLength", bin.size()}}});
  std::string texte = j.dump();
  bourrer(texte, ' ');
  std::string o;
  const uint32_t magie = 0x46546C67, version = 2, total = static_cast<uint32_t>(12 + 8 + texte.size() + 8 + bin.size());
  const uint32_t lj = static_cast<uint32_t>(texte.size()), tj = 0x4E4F534A, lb = static_cast<uint32_t>(bin.size()), tb = 0x004E4942;
  ajouter(o, &magie, 4);
  ajouter(o, &version, 4);
  ajouter(o, &total, 4);
  ajouter(o, &lj, 4);
  ajouter(o, &tj, 4);
  o += texte;
  ajouter(o, &lb, 4);
  ajouter(o, &tb, 4);
  o += bin;
  return o;
}

// ---------- VRML 2.0 ----------
std::string vrml(const std::vector<PieceMaillee>& pieces) {
  std::ostringstream o;
  o << "#VRML V2.0 utf8\n# Prométhée : carte équipée, en mètres, z au-dessus de la carte\n";
  std::map<std::string, int> pris;
  for (const auto& p : pieces) {
    std::string def = nomPropre(p.nom, "piece");
    if (std::isdigit(static_cast<unsigned char>(def[0]))) def = "_" + def;
    if (const int n = pris[def]++; n) def += "_" + std::to_string(n + 1);
    o << "DEF " << def << " Transform {\n  children [\n";
    for (const auto& l : p.lots) {
      o << "    Shape {\n      appearance Appearance { material Material { diffuseColor " << nb(l.couleur[0], 3) << ' ' << nb(l.couleur[1], 3) << ' ' << nb(l.couleur[2], 3)
        << (metal(l.couleur) ? " specularColor 0.6 0.6 0.6 shininess 0.4" : "") << " } }\n      geometry IndexedFaceSet {\n        creaseAngle 0.5\n        coord Coordinate { point [\n";
      for (const auto& q : l.positions) o << "          " << nb(q[0] / 1000.0, 7) << ' ' << nb(q[1] / 1000.0, 7) << ' ' << nb(q[2] / 1000.0, 7) << ",\n";
      o << "        ] }\n        coordIndex [\n";
      for (std::size_t i = 0; i + 2 < l.indices.size(); i += 3) o << "          " << l.indices[i] << ", " << l.indices[i + 1] << ", " << l.indices[i + 2] << ", -1,\n";
      o << "        ]\n      }\n    }\n";
    }
    o << "  ]\n}\n";
  }
  return o.str();
}

// ---------- STL binaire ----------
std::string stl(const std::vector<PieceMaillee>& pieces) {
  std::string o(80, ' ');
  const std::string entete = "Promethee : carte equipee (mm)";
  std::memcpy(o.data(), entete.data(), entete.size());
  uint32_t n = 0;
  for (const auto& p : pieces)
    for (const auto& l : p.lots) n += static_cast<uint32_t>(l.indices.size() / 3);
  ajouter(o, &n, 4);
  for (const auto& p : pieces)
    for (const auto& l : p.lots)
      for (std::size_t i = 0; i + 2 < l.indices.size(); i += 3) {
        const auto& A = l.positions[l.indices[i]];
        const auto& B = l.positions[l.indices[i + 1]];
        const auto& C = l.positions[l.indices[i + 2]];
        const float ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2], vx = C[0] - A[0], vy = C[1] - A[1], vz = C[2] - A[2];
        float nn[3] = {uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
        const float len = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
        if (len > 0)
          for (float& x : nn) x /= len;
        ajouter(o, nn, 12);
        ajouter(o, A.data(), 12);
        ajouter(o, B.data(), 12);
        ajouter(o, C.data(), 12);
        const uint16_t attr = 0;
        ajouter(o, &attr, 2);
      }
  return o;
}

// ---------- COLLADA 1.4.1 ----------
std::string collada(const std::vector<PieceMaillee>& pieces) {
  std::ostringstream o;
  o << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">\n"
    << "  <asset><contributor><authoring_tool>Prométhée</authoring_tool></contributor><created>2026-01-01T00:00:00</created><modified>2026-01-01T00:00:00</modified>"
    << "<unit name=\"millimeter\" meter=\"0.001\"/><up_axis>Z_UP</up_axis></asset>\n";
  std::map<std::array<int, 3>, int> parCouleur;
  std::vector<std::array<double, 3>> couleurs;
  for (const auto& p : pieces)
    for (const auto& l : p.lots) {
      const std::array<int, 3> cle{static_cast<int>(std::lround(l.couleur[0] * 255)), static_cast<int>(std::lround(l.couleur[1] * 255)), static_cast<int>(std::lround(l.couleur[2] * 255))};
      if (parCouleur.emplace(cle, static_cast<int>(couleurs.size())).second) couleurs.push_back(l.couleur);
    }
  o << "  <library_effects>\n";
  for (std::size_t i = 0; i < couleurs.size(); ++i)
    o << "    <effect id=\"effet" << i << "\"><profile_COMMON><technique sid=\"commun\"><lambert><diffuse><color>" << nb(couleurs[i][0], 3) << ' ' << nb(couleurs[i][1], 3) << ' '
      << nb(couleurs[i][2], 3) << " 1</color></diffuse></lambert></technique></profile_COMMON></effect>\n";
  o << "  </library_effects>\n  <library_materials>\n";
  for (std::size_t i = 0; i < couleurs.size(); ++i) o << "    <material id=\"matiere" << i << "\"><instance_effect url=\"#effet" << i << "\"/></material>\n";
  o << "  </library_materials>\n  <library_geometries>\n";
  auto cleDe = [&](const Lot& l) {
    return parCouleur.at({static_cast<int>(std::lround(l.couleur[0] * 255)), static_cast<int>(std::lround(l.couleur[1] * 255)), static_cast<int>(std::lround(l.couleur[2] * 255))});
  };
  for (std::size_t i = 0; i < pieces.size(); ++i) {
    for (std::size_t k = 0; k < pieces[i].lots.size(); ++k) {
      const Lot& l = pieces[i].lots[k];
      const std::string g = "g" + std::to_string(i) + "_" + std::to_string(k);
      o << "    <geometry id=\"" << g << "\" name=\"" << echapperXml(pieces[i].nom) << "\"><mesh>\n      <source id=\"" << g << "-p\"><float_array id=\"" << g << "-pa\" count=\""
        << l.positions.size() * 3 << "\">";
      for (const auto& q : l.positions) o << nb(q[0]) << ' ' << nb(q[1]) << ' ' << nb(q[2]) << ' ';
      o << "</float_array><technique_common><accessor source=\"#" << g << "-pa\" count=\"" << l.positions.size()
        << "\" stride=\"3\"><param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/><param name=\"Z\" type=\"float\"/></accessor></technique_common></source>\n";
      o << "      <source id=\"" << g << "-n\"><float_array id=\"" << g << "-na\" count=\"" << l.normales.size() * 3 << "\">";
      for (const auto& q : l.normales) o << nb(q[0], 4) << ' ' << nb(q[1], 4) << ' ' << nb(q[2], 4) << ' ';
      o << "</float_array><technique_common><accessor source=\"#" << g << "-na\" count=\"" << l.normales.size()
        << "\" stride=\"3\"><param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/><param name=\"Z\" type=\"float\"/></accessor></technique_common></source>\n";
      o << "      <vertices id=\"" << g << "-v\"><input semantic=\"POSITION\" source=\"#" << g << "-p\"/><input semantic=\"NORMAL\" source=\"#" << g << "-n\"/></vertices>\n";
      o << "      <triangles material=\"m" << cleDe(l) << "\" count=\"" << l.indices.size() / 3 << "\"><input semantic=\"VERTEX\" source=\"#" << g << "-v\" offset=\"0\"/><p>";
      for (uint32_t x : l.indices) o << x << ' ';
      o << "</p></triangles>\n    </mesh></geometry>\n";
    }
  }
  o << "  </library_geometries>\n  <library_visual_scenes><visual_scene id=\"scene\" name=\"Carte\">\n";
  for (std::size_t i = 0; i < pieces.size(); ++i) {
    o << "    <node id=\"n" << i << "\" name=\"" << echapperXml(pieces[i].nom) << "\">";
    for (std::size_t k = 0; k < pieces[i].lots.size(); ++k) {
      const int m = cleDe(pieces[i].lots[k]);
      o << "<instance_geometry url=\"#g" << i << "_" << k << "\"><bind_material><technique_common><instance_material symbol=\"m" << m << "\" target=\"#matiere" << m
        << "\"/></technique_common></bind_material></instance_geometry>";
    }
    o << "</node>\n";
  }
  o << "  </visual_scene></library_visual_scenes>\n  <scene><instance_visual_scene url=\"#scene\"/></scene>\n</COLLADA>\n";
  return o.str();
}

// ---------- AMF 1.1 ----------
std::string amf(const std::vector<PieceMaillee>& pieces) {
  std::ostringstream o;
  o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<amf unit=\"millimeter\" version=\"1.1\">\n  <metadata type=\"producer\">Prométhée</metadata>\n";
  std::map<std::array<int, 3>, int> parCouleur;
  for (const auto& p : pieces)
    for (const auto& l : p.lots) {
      const std::array<int, 3> cle{static_cast<int>(std::lround(l.couleur[0] * 255)), static_cast<int>(std::lround(l.couleur[1] * 255)), static_cast<int>(std::lround(l.couleur[2] * 255))};
      if (parCouleur.emplace(cle, static_cast<int>(parCouleur.size()) + 1).second)
        o << "  <material id=\"" << parCouleur.at(cle) << "\"><color><r>" << nb(l.couleur[0], 3) << "</r><g>" << nb(l.couleur[1], 3) << "</g><b>" << nb(l.couleur[2], 3) << "</b></color></material>\n";
    }
  for (std::size_t i = 0; i < pieces.size(); ++i) {
    o << "  <object id=\"" << i << "\">\n    <metadata type=\"name\">" << echapperXml(pieces[i].nom) << "</metadata>\n    <mesh>\n      <vertices>\n";
    for (const auto& l : pieces[i].lots)
      for (const auto& q : l.positions) o << "        <vertex><coordinates><x>" << nb(q[0]) << "</x><y>" << nb(q[1]) << "</y><z>" << nb(q[2]) << "</z></coordinates></vertex>\n";
    o << "      </vertices>\n";
    uint32_t base = 0;
    for (const auto& l : pieces[i].lots) {
      const std::array<int, 3> cle{static_cast<int>(std::lround(l.couleur[0] * 255)), static_cast<int>(std::lround(l.couleur[1] * 255)), static_cast<int>(std::lround(l.couleur[2] * 255))};
      o << "      <volume materialid=\"" << parCouleur.at(cle) << "\">\n";
      for (std::size_t k = 0; k + 2 < l.indices.size(); k += 3)
        o << "        <triangle><v1>" << base + l.indices[k] << "</v1><v2>" << base + l.indices[k + 1] << "</v2><v3>" << base + l.indices[k + 2] << "</v3></triangle>\n";
      o << "      </volume>\n";
      base += static_cast<uint32_t>(l.positions.size());
    }
    o << "    </mesh>\n  </object>\n";
  }
  o << "</amf>\n";
  return o.str();
}

// ---------- OpenSCAD ----------
std::string openscad(const std::vector<PieceMaillee>& pieces) {
  std::ostringstream o;
  o << "// Prométhée : carte équipée en polyèdres OpenSCAD (mm, z au-dessus de la carte)\n";
  for (const auto& p : pieces)
    for (const auto& l : p.lots) {
      o << "// " << p.nom << "\ncolor([" << nb(l.couleur[0], 3) << ", " << nb(l.couleur[1], 3) << ", " << nb(l.couleur[2], 3) << "]) polyhedron(points=[";
      for (std::size_t i = 0; i < l.positions.size(); ++i) o << (i ? "," : "") << '[' << nb(l.positions[i][0]) << ',' << nb(l.positions[i][1]) << ',' << nb(l.positions[i][2]) << ']';
      o << "], faces=[";
      // OpenSCAD veut les faces en sens horaire vues du dehors
      for (std::size_t k = 0; k + 2 < l.indices.size(); k += 3) o << (k ? "," : "") << '[' << l.indices[k] << ',' << l.indices[k + 2] << ',' << l.indices[k + 1] << ']';
      o << "]);\n";
    }
  return o.str();
}

// ---------- XAO (géométrie exacte, groupe des faces de pastilles) ----------
std::string xao(const std::vector<PieceNommee>& pieces) {
  BRep_Builder b;
  TopoDS_Compound ensemble;
  b.MakeCompound(ensemble);
  for (const auto& p : pieces)
    if (!p.forme.IsNull()) b.Add(ensemble, p.forme);
  std::ostringstream brep;
  BRepTools::Write(ensemble, brep);
  std::ostringstream o;
  o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<XAO version=\"1.0\" author=\"Prométhée\">\n  <geometry name=\"Carte\">\n    <shape format=\"BREP\"><![CDATA[" << brep.str() << "]]></shape>\n    <topology>\n";
  const std::pair<TopAbs_ShapeEnum, const char*> genres[] = {{TopAbs_VERTEX, "vertex"}, {TopAbs_EDGE, "edge"}, {TopAbs_FACE, "face"}, {TopAbs_SOLID, "solid"}};
  TopTools_IndexedMapOfShape faces;
  for (const auto& [genre, nom] : genres) {
    TopTools_IndexedMapOfShape m;
    TopExp::MapShapes(ensemble, genre, m);
    if (genre == TopAbs_FACE) faces = m;
    o << "      <" << nom << "s count=\"" << m.Extent() << "\">\n";
    for (int i = 1; i <= m.Extent(); ++i) o << "        <" << nom << " index=\"" << i - 1 << "\" name=\"\" reference=\"" << i << "\"/>\n";
    o << "      </" << nom << "s>\n";
  }
  o << "    </topology>\n  </geometry>\n";
  // Pastilles : faces de la carte à la couleur de la finition (métal clair)
  std::vector<int> pastilles;
  for (const auto& p : pieces)
    for (const auto& [face, c] : p.couleursFaces)
      if (metal(c))
        for (TopExp_Explorer ex(face, TopAbs_FACE); ex.More(); ex.Next())
          for (int i = 1; i <= faces.Extent(); ++i)
            if (faces(i).IsPartner(ex.Current())) {
              pastilles.push_back(i - 1);
              break;
            }
  std::sort(pastilles.begin(), pastilles.end());
  pastilles.erase(std::unique(pastilles.begin(), pastilles.end()), pastilles.end());
  o << "  <groups count=\"1\">\n    <group name=\"pastilles\" dimension=\"face\" count=\"" << pastilles.size() << "\">\n";
  for (int i : pastilles) o << "      <element index=\"" << i << "\"/>\n";
  o << "    </group>\n  </groups>\n  <fields count=\"0\"/>\n</XAO>\n";
  return o.str();
}

// ---------- STEP compressé (gzip) ----------
std::string gzip(const std::string& d) {
  std::string o("\x1F\x8B\x08\x00\x00\x00\x00\x00\x00\xFF", 10);
  o += deflerBrut(d);
  const uint32_t crc = crc32(d), n = static_cast<uint32_t>(d.size());
  ajouter(o, &crc, 4);
  ajouter(o, &n, 4);
  return o;
}
}  // namespace

const std::vector<InfoFormat3D>& formats3D() {
  static const std::vector<InfoFormat3D> f{
      {Format3D::GLB, "glb", "glTF binaire (GLB)", "navigateurs, Blender, visionneuses de Windows et de macOS, réalité augmentée"},
      {Format3D::VRML, "wrl", "VRML 2.0", "visionneuses VRML, KiCad, anciens outils de rendu"},
      {Format3D::STL, "stl", "STL binaire", "impression 3D, trancheurs ; une seule couleur"},
      {Format3D::COLLADA, "dae", "COLLADA", "SketchUp, Blender, moteurs de jeu"},
      {Format3D::AMF, "amf", "AMF", "impression 3D en couleurs"},
      {Format3D::OpenSCAD, "scad", "Script OpenSCAD", "polyèdres colorés à reprendre dans OpenSCAD"},
      {Format3D::XAO, "xao", "XAO (Salome)", "géométrie exacte et groupe des faces de pastilles, pour le maillage et la simulation"},
      {Format3D::STPZ, "stpz", "STEP compressé", "STEP AP214 dans un gzip, lu par KiCad et FreeCAD"},
  };
  return f;
}

std::string texte3D(const std::vector<PieceNommee>& pieces, Format3D f, double fleche) {
  switch (f) {
    case Format3D::XAO: return xao(pieces);
    case Format3D::STPZ: {
      // STEP écrit dans un fichier temporaire par OpenCascade, puis compressé
      const std::filesystem::path tmp = std::filesystem::temp_directory_path() / ("promethee-" + std::to_string(reinterpret_cast<std::uintptr_t>(&pieces)) + ".step");
      exporterStep(pieces, tmp.string());
      const std::string s = lireFichier(tmp);
      std::error_code ec;
      std::filesystem::remove(tmp, ec);
      return gzip(s);
    }
    default: break;
  }
  const auto m = mailler(pieces, fleche);
  switch (f) {
    case Format3D::GLB: return glb(m);
    case Format3D::VRML: return vrml(m);
    case Format3D::STL: return stl(m);
    case Format3D::COLLADA: return collada(m);
    case Format3D::AMF: return amf(m);
    case Format3D::OpenSCAD: return openscad(m);
    default: return {};
  }
}

void exporter3D(const std::vector<PieceNommee>& pieces, Format3D f, const std::string& chemin, double fleche) { ecrireFichierAtomique(cheminFichier(chemin), texte3D(pieces, f, fleche)); }

ResumeGlb lireGlb(const std::string& o) {
  ResumeGlb r;
  auto u32 = [&](std::size_t i) {
    uint32_t v = 0;
    if (i + 4 <= o.size()) std::memcpy(&v, o.data() + i, 4);
    return v;
  };
  if (o.size() < 20 || u32(0) != 0x46546C67) return r.erreur = "en-tête glTF absent", r;
  if (u32(4) != 2) return r.erreur = "version " + std::to_string(u32(4)), r;
  if (u32(8) != o.size()) return r.erreur = "longueur annoncée " + std::to_string(u32(8)) + " pour " + std::to_string(o.size()), r;
  const uint32_t lj = u32(12);
  if (u32(16) != 0x4E4F534A || 20 + lj > o.size() || lj % 4) return r.erreur = "morceau JSON invalide", r;
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(o.substr(20, lj));
  } catch (const std::exception& e) {
    return r.erreur = std::string("JSON : ") + e.what(), r;
  }
  const std::size_t pb = 20 + lj;
  if (pb + 8 > o.size() || u32(pb + 4) != 0x004E4942) return r.erreur = "morceau binaire absent", r;
  const uint32_t lb = u32(pb);
  if (pb + 8 + lb != o.size() || lb % 4) return r.erreur = "morceau binaire de longueur fausse", r;
  if (j.value("asset", nlohmann::json::object()).value("version", "") != "2.0") return r.erreur = "asset.version n'est pas 2.0", r;
  const auto& tampons = j.at("buffers");
  if (tampons.size() != 1 || tampons[0].value("byteLength", 0u) > lb) return r.erreur = "tampon plus long que le morceau binaire", r;
  const auto& vues = j.at("bufferViews");
  for (const auto& v : vues)
    if (v.value("byteOffset", 0u) + v.at("byteLength").get<std::size_t>() > lb) return r.erreur = "vue hors du tampon", r;
  const auto& acc = j.at("accessors");
  auto taille = [](const nlohmann::json& a) {
    const std::string t = a.at("type");
    const std::size_t comp = t == "SCALAR" ? 1 : t == "VEC2" ? 2 : t == "VEC3" ? 3 : 4;
    const int ct = a.at("componentType");
    return comp * (ct == 5126 || ct == 5125 ? 4 : ct == 5123 || ct == 5122 ? 2 : 1) * a.at("count").get<std::size_t>();
  };
  for (const auto& a : acc)
    if (taille(a) > vues.at(a.at("bufferView").get<std::size_t>()).at("byteLength").get<std::size_t>()) return r.erreur = "accesseur plus long que sa vue", r;
  bool premier = true;
  for (const auto& m : j.at("meshes")) {
    ++r.maillages;
    for (const auto& p : m.at("primitives")) {
      ++r.primitives;
      const auto& pos = acc.at(p.at("attributes").at("POSITION").get<std::size_t>());
      if (!pos.contains("min") || !pos.contains("max")) return r.erreur = "POSITION sans min ni max", r;
      r.sommets += pos.at("count").get<std::size_t>();
      r.triangles += acc.at(p.at("indices").get<std::size_t>()).at("count").get<std::size_t>() / 3;
      // Indices dans les bornes : relus dans le tampon
      const auto& ia = acc.at(p.at("indices").get<std::size_t>());
      const auto& iv = vues.at(ia.at("bufferView").get<std::size_t>());
      const std::size_t debut = pb + 8 + iv.value("byteOffset", 0u) + ia.value("byteOffset", 0u);
      for (std::size_t k = 0; k < ia.at("count").get<std::size_t>(); ++k)
        if (u32(debut + 4 * k) >= pos.at("count").get<uint32_t>()) return r.erreur = "indice hors des sommets", r;
      for (int d = 0; d < 3; ++d) {
        const double a = pos.at("min")[static_cast<std::size_t>(d)], b = pos.at("max")[static_cast<std::size_t>(d)];
        r.min[static_cast<std::size_t>(d)] = premier ? a : std::min(r.min[static_cast<std::size_t>(d)], a);
        r.max[static_cast<std::size_t>(d)] = premier ? b : std::max(r.max[static_cast<std::size_t>(d)], b);
      }
      premier = false;
    }
  }
  r.materiaux = static_cast<int>(j.value("materials", nlohmann::json::array()).size());
  for (const auto& n : j.at("nodes")) {
    ++r.noeuds;
    r.nomsNoeuds.push_back(n.value("name", ""));
  }
  r.ok = true;
  return r;
}

}  // namespace prom

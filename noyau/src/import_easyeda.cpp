// Prométhée : composants EasyEDA et LCSC (10.6). Voir import_easyeda.hpp. Licence GPL-3.0-only.
#include "promethee/import_easyeda.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <sstream>

#include <nlohmann/json.hpp>

#include "promethee/circuit_edition.hpp"

namespace prom::ecad {

namespace {
using json = nlohmann::json;
constexpr double UNITE_MM = 0.254;  // une unité d'EasyEDA : 10 mil

std::vector<std::string> champs(const std::string& s, char sep = '~') {
  std::vector<std::string> r;
  std::string x;
  for (char ch : s) {
    if (ch == sep) {
      r.push_back(x);
      x.clear();
    } else
      x += ch;
  }
  r.push_back(x);
  return r;
}
std::vector<std::string> blocs(const std::string& s) {
  std::vector<std::string> r;
  std::size_t a = 0;
  for (std::size_t p; (p = s.find("^^", a)) != std::string::npos; a = p + 2) r.push_back(s.substr(a, p - a));
  r.push_back(s.substr(a));
  return r;
}
double reel(const std::string& s, double d = 0) {
  if (s.empty()) return d;
  char* fin = nullptr;
  const double v = std::strtod(s.c_str(), &fin);
  return fin == s.c_str() ? d : v;
}
std::string at(const std::vector<std::string>& v, std::size_t i) { return i < v.size() ? v[i] : std::string(); }
std::string texte(const json& j, const char* cle) {
  if (!j.is_object() || !j.contains(cle)) return {};
  const json& v = j.at(cle);
  if (v.is_string()) return v.get<std::string>();
  if (v.is_number()) return v.dump();
  return {};
}
double nombre(const json& j, const char* cle, double d = 0) {
  if (!j.is_object() || !j.contains(cle)) return d;
  const json& v = j.at(cle);
  if (v.is_number()) return v.get<double>();
  if (v.is_string()) return reel(v.get<std::string>(), d);
  return d;
}
std::vector<double> nombres(const std::string& s) {
  std::vector<double> r;
  std::istringstream in(s);
  for (std::string x; in >> x;) {
    for (char& ch : x)
      if (ch == ',') ch = ' ';
    std::istringstream in2(x);
    for (double v; in2 >> v;) r.push_back(v);
  }
  return r;
}
// Points d'un chemin SVG simple (M, L, H, V, h, v, l ; Z ignoré), coordonnées absolues
std::vector<std::pair<double, double>> pointsChemin(const std::string& d) {
  std::vector<std::pair<double, double>> pts;
  std::istringstream in(d);
  char cmd = 'M';
  double x = 0, y = 0;
  std::string jeton;
  auto lire = [&](double& v) {
    if (!(in >> jeton)) return false;
    v = reel(jeton);
    return true;
  };
  while (in >> std::ws && !in.eof()) {
    const int c = in.peek();
    if (std::isalpha(c)) {
      cmd = static_cast<char>(in.get());
      if (cmd == 'Z' || cmd == 'z') continue;
    }
    double a = 0, b = 0;
    switch (cmd) {
      case 'M': case 'L':
        if (!lire(a) || !lire(b)) return pts;
        x = a; y = b;
        break;
      case 'l': case 'm':
        if (!lire(a) || !lire(b)) return pts;
        x += a; y += b;
        break;
      case 'H': if (!lire(a)) return pts; x = a; break;
      case 'h': if (!lire(a)) return pts; x += a; break;
      case 'V': if (!lire(a)) return pts; y = a; break;
      case 'v': if (!lire(a)) return pts; y += a; break;
      default:
        if (!(in >> jeton)) return pts;  // commande non lue (A, C…) : on avance
        continue;
    }
    pts.push_back({x, y});
  }
  return pts;
}

// ---------- Symbole ----------
TypeBroche typeBroche(const std::string& e) {
  if (e == "1") return TypeBroche::Entree;
  if (e == "2") return TypeBroche::Sortie;
  if (e == "3") return TypeBroche::Bidirectionnelle;
  if (e == "4") return TypeBroche::AlimEntree;
  return TypeBroche::Passive;
}
DefSymbole symbole(const json& data, const std::string& id, const std::string& nom, std::vector<std::string>& alertes) {
  DefSymbole d;
  d.id = id;
  d.nom = nom;
  d.categorie = "Importés de LCSC";
  const json& head = data.value("head", json::object());
  const json& cpara = head.value("c_para", json::object());
  std::string pre = texte(cpara, "pre");
  while (!pre.empty() && (pre.back() == '?' || pre.back() == ' ')) pre.pop_back();
  d.prefixe = pre.empty() ? "U" : pre;
  double x0 = nombre(head, "x"), y0 = nombre(head, "y");
  const json shapes = data.value("shape", json::array());
  // Origine calée sur la grille de 100 mil d'après la première broche, pour que les broches tombent sur la grille
  for (const auto& s : shapes) {
    if (!s.is_string() || s.get<std::string>().rfind("P~", 0) != 0) continue;
    const auto f = champs(blocs(s.get<std::string>()).front());
    const double px = reel(at(f, 4)), py = reel(at(f, 5));
    x0 = px - 10 * std::round((px - x0) / 10);
    y0 = py - 10 * std::round((py - y0) / 10);
    break;
  }
  auto p = [&](double x, double y) { return ptMm((x - x0) * UNITE_MM, (y - y0) * UNITE_MM); };
  for (const auto& s : shapes) {
    if (!s.is_string()) continue;
    const std::string t = s.get<std::string>();
    const auto f = champs(t);
    const std::string genre = at(f, 0);
    if (genre == "P") {
      const auto b = blocs(t);
      const auto reglage = champs(b[0]);
      Broche br;
      br.type = typeBroche(at(reglage, 2));
      br.numero = at(reglage, 3);
      br.numero.erase(std::remove(br.numero.begin(), br.numero.end(), ' '), br.numero.end());
      const double px = reel(at(reglage, 4)), py = reel(at(reglage, 5));
      br.extremite = p(px, py);
      // Direction et longueur d'après le trait de la broche, de l'extrémité vers le corps
      double lx = 0, ly = 0;
      if (b.size() > 2) {
        const auto pts = pointsChemin(champs(b[2]).front());
        if (pts.size() >= 2) {
          lx = pts.back().first - pts.front().first;
          ly = pts.back().second - pts.front().second;
        }
      }
      if (std::abs(lx) < 1e-9 && std::abs(ly) < 1e-9) {
        // Pas de trait lisible : l'angle de la broche, 0 vers la gauche du corps
        const int r = static_cast<int>(std::lround(reel(at(reglage, 6)) / 90)) & 3;
        static const double dx[4] = {-10, 0, 10, 0}, dy[4] = {0, -10, 0, 10};
        lx = -dx[r];
        ly = -dy[r];
      }
      br.direction = std::abs(lx) >= std::abs(ly) ? (lx > 0 ? 0 : 2) : (ly > 0 ? 1 : 3);
      br.longueur = nm(std::hypot(lx, ly) * UNITE_MM);
      if (b.size() > 3) {
        const auto nomB = champs(b[3]);
        br.nom = at(nomB, 4);
        br.nomVisible = at(nomB, 0) != "0";
      }
      if (b.size() > 4) br.numeroVisible = champs(b[4]).front() != "0";
      if (b.size() > 5 && champs(b[5]).front() == "1") br.style = "inversee";
      if (b.size() > 6 && champs(b[6]).front() == "1") br.style = br.style.empty() ? "horloge" : "horlogeInversee";
      if (br.numero.empty()) {
        alertes.push_back("Broche sans numéro ignorée");
        continue;
      }
      d.broches.push_back(br);
    } else if (genre == "R") {
      Trace tr;
      tr.forme = Trace::Forme::Rectangle;
      const double x = reel(at(f, 1)), y = reel(at(f, 2)), w = reel(at(f, 5)), h = reel(at(f, 6));
      tr.points = {p(x, y), p(x + w, y + h)};
      tr.epaisseur = nm(std::max(0.5, reel(at(f, 8), 1)) * UNITE_MM);
      tr.rempli = !at(f, 10).empty() && at(f, 10) != "none";
      d.traces.push_back(tr);
    } else if (genre == "PL" || genre == "PG") {
      Trace tr;
      tr.forme = Trace::Forme::Polyligne;
      const auto v = nombres(at(f, 1));
      for (std::size_t i = 0; i + 1 < v.size(); i += 2) tr.points.push_back(p(v[i], v[i + 1]));
      tr.epaisseur = nm(std::max(0.5, reel(at(f, 3), 1)) * UNITE_MM);
      tr.ferme = genre == "PG";
      tr.rempli = genre == "PG" && !at(f, 5).empty() && at(f, 5) != "none";
      if (tr.points.size() >= 2) d.traces.push_back(tr);
    } else if (genre == "C") {
      Trace tr;
      tr.forme = Trace::Forme::Cercle;
      tr.points = {p(reel(at(f, 1)), reel(at(f, 2)))};
      tr.rayon = nm(reel(at(f, 3)) * UNITE_MM);
      tr.epaisseur = nm(std::max(0.5, reel(at(f, 5), 1)) * UNITE_MM);
      d.traces.push_back(tr);
    } else if (genre == "E") {
      Trace tr;
      tr.forme = Trace::Forme::Ellipse;
      tr.points = {p(reel(at(f, 1)), reel(at(f, 2)))};
      tr.rayon = nm(reel(at(f, 3)) * UNITE_MM);
      tr.rayonY = nm(reel(at(f, 4)) * UNITE_MM);
      tr.epaisseur = nm(std::max(0.5, reel(at(f, 6), 1)) * UNITE_MM);
      d.traces.push_back(tr);
    } else if (genre == "PT" || genre == "A") {
      // Chemin : ses points d'appui, en polyligne
      Trace tr;
      tr.forme = Trace::Forme::Polyligne;
      for (const auto& [x, y] : pointsChemin(at(f, 1))) tr.points.push_back(p(x, y));
      tr.epaisseur = nm(std::max(0.5, reel(at(f, 4), 1)) * UNITE_MM);
      if (tr.points.size() >= 2) d.traces.push_back(tr);
    }
  }
  return d;
}

// ---------- Empreinte ----------
Couche coucheTrait(int l) {
  switch (l) {
    case 3: return Couche::SerigraphieDessus;
    case 4: return Couche::SerigraphieDessous;
    case 13: return Couche::FabricationDessus;
    case 14: return Couche::FabricationDessous;
    case 12: return Couche::Dessins;
    default: return Couche::Commentaires;
  }
}
struct Modele3DEasyEda {
  std::string uuid, titre;
  double cx = 0, cy = 0, z = 0, rz = 0;
};
DefEmpreinte empreinte(const json& data, const std::string& id, std::vector<std::string>& alertes, std::optional<Modele3DEasyEda>& m3d) {
  DefEmpreinte d;
  d.id = id;
  d.famille = "LCSC";
  const json& head = data.value("head", json::object());
  const double x0 = nombre(head, "x"), y0 = nombre(head, "y");
  d.description = "Empreinte EasyEDA « " + texte(head.value("c_para", json::object()), "package") + " »";
  auto p = [&](double x, double y) { return ptMm((x - x0) * UNITE_MM, (y - y0) * UNITE_MM); };
  auto lg = [](double v) { return nm(v * UNITE_MM); };
  Boite enc;
  bool cms = true;
  for (const auto& s : data.value("shape", json::array())) {
    if (!s.is_string()) continue;
    const std::string t = s.get<std::string>();
    const auto f = champs(t);
    const std::string genre = at(f, 0);
    if (genre == "PAD") {
      Pastille pa;
      const std::string forme = at(f, 1);
      pa.position = p(reel(at(f, 2)), reel(at(f, 3)));
      pa.largeur = lg(reel(at(f, 4)));
      pa.hauteur = lg(reel(at(f, 5)));
      const int couche = static_cast<int>(reel(at(f, 6), 1));
      pa.numero = at(f, 8);
      const double rayonTrou = reel(at(f, 9));
      pa.rotation = std::fmod(360 - reel(at(f, 11)), 360.0);  // EasyEDA tourne en sens horaire (y vers le bas)
      const double longTrou = reel(at(f, 13));
      const bool metal = at(f, 15) != "N";
      if (forme == "ELLIPSE") pa.forme = std::abs(pa.largeur - pa.hauteur) < 1000 ? FormePastille::Rond : FormePastille::Ovale;
      else if (forme == "OVAL") pa.forme = FormePastille::Ovale;
      else if (forme == "POLYGON") {
        pa.forme = FormePastille::Libre;
        const auto v = nombres(at(f, 10));
        // Contour en absolu, ramené au centre et redressé (repère de la pastille, avant rotation)
        for (std::size_t i = 0; i + 1 < v.size(); i += 2) {
          const Pt q = p(v[i], v[i + 1]) - pa.position;
          pa.contourLibre.push_back(tournerDegres(q, -pa.rotation));
        }
        Boite bq;
        for (const Pt& q : pa.contourLibre) bq.etendre(q);
        if (!bq.vide()) {
          pa.largeur = std::max(pa.largeur, bq.largeur());
          pa.hauteur = std::max(pa.hauteur, bq.hauteur());
        }
      } else
        pa.forme = FormePastille::Rect;
      if (couche == 11 || rayonTrou > 0) {
        cms = cms && rayonTrou <= 0;
        pa.type = metal ? TypePastille::Traversante : TypePastille::NonMetallisee;
        pa.percage = lg(2 * rayonTrou);
        if (longTrou > 2 * rayonTrou + 1e-6) {
          // Fente : sa longueur le long du grand côté de la pastille
          pa.longueurPercage = lg(longTrou);
          if (pa.hauteur > pa.largeur) pa.rotation = std::fmod(pa.rotation + 90, 360.0), std::swap(pa.largeur, pa.hauteur);
        }
      } else if (couche == 2)
        pa.couches = bit(Couche::CuivreDessous) | bit(Couche::VernisDessous) | bit(Couche::PateDessous);
      d.pastilles.push_back(pa);
      enc.etendre(boite(formePastille(pa)));
    } else if (genre == "HOLE") {
      Pastille pa;
      pa.type = TypePastille::NonMetallisee;
      pa.forme = FormePastille::Rond;
      pa.position = p(reel(at(f, 1)), reel(at(f, 2)));
      pa.percage = pa.largeur = pa.hauteur = lg(2 * reel(at(f, 3)));
      d.pastilles.push_back(pa);
      enc.etendre(boite(formePastille(pa)));
    } else if (genre == "TRACK") {
      const Couche k = coucheTrait(static_cast<int>(reel(at(f, 2))));
      if (k == Couche::Commentaires) continue;
      const auto v = nombres(at(f, 4));
      for (std::size_t i = 0; i + 3 < v.size(); i += 2) {
        TraitEmpreinte tr;
        tr.couche = k;
        tr.a = p(v[i], v[i + 1]);
        tr.b = p(v[i + 2], v[i + 3]);
        tr.epaisseur = std::max<Nm>(lg(reel(at(f, 1))), 50'000);
        d.traits.push_back(tr);
        enc.etendre(tr.a);
        enc.etendre(tr.b);
      }
    } else if (genre == "CIRCLE") {
      const Couche k = coucheTrait(static_cast<int>(reel(at(f, 5))));
      if (k == Couche::Commentaires) continue;
      TraitEmpreinte tr;
      tr.couche = k;
      tr.a = p(reel(at(f, 1)), reel(at(f, 2)));
      tr.rayon = lg(reel(at(f, 3)));
      tr.epaisseur = std::max<Nm>(lg(reel(at(f, 4))), 50'000);
      d.traits.push_back(tr);
    } else if (genre == "ARC") {
      const Couche k = coucheTrait(static_cast<int>(reel(at(f, 2))));
      if (k == Couche::Commentaires) continue;
      // Arc SVG « M x y A rx ry rot grand sens x2 y2 » : ses deux bouts reliés par des cordes sur le cercle
      std::istringstream in(at(f, 4));
      std::string c1, c2;
      double ax, ay, rx, ry, rot, grand, sens, bx, by;
      if (!(in >> c1 >> ax >> ay >> c2 >> rx >> ry >> rot >> grand >> sens >> bx >> by) || rx <= 0) {
        std::string brut = at(f, 4);
        for (char& ch : brut)
          if (ch == ',') ch = ' ';
        std::istringstream in2(brut);
        if (!(in2 >> c1 >> ax >> ay >> c2 >> rx >> ry >> rot >> grand >> sens >> bx >> by) || rx <= 0) continue;
      }
      const double dxm = (ax - bx) / 2, dym = (ay - by) / 2, dd = dxm * dxm + dym * dym;
      const double r = std::max(rx, std::sqrt(dd));
      const double h = std::sqrt(std::max(0.0, r * r - dd)) * ((grand != sens) ? 1 : -1);
      const double mx = (ax + bx) / 2, my = (ay + by) / 2, n = std::sqrt(dd);
      if (n < 1e-9) continue;
      const double cx = mx - h * dym / n, cy = my + h * dxm / n;
      double a0 = std::atan2(ay - cy, ax - cx), a1 = std::atan2(by - cy, bx - cx);
      if (sens > 0.5 && a1 < a0) a1 += 2 * std::numbers::pi;
      if (sens < 0.5 && a1 > a0) a1 -= 2 * std::numbers::pi;
      const int pas = std::max(4, static_cast<int>(std::abs(a1 - a0) / (std::numbers::pi / 16)));
      Pt prec = p(ax, ay);
      for (int i = 1; i <= pas; ++i) {
        const double t = a0 + (a1 - a0) * i / pas;
        const Pt q = p(cx + r * std::cos(t), cy + r * std::sin(t));
        TraitEmpreinte tr;
        tr.couche = k;
        tr.a = prec;
        tr.b = q;
        tr.epaisseur = std::max<Nm>(lg(reel(at(f, 1))), 50'000);
        d.traits.push_back(tr);
        prec = q;
      }
    } else if (genre == "SVGNODE") {
      try {
        const json n = json::parse(t.substr(8));
        const json& a = n.value("attrs", json::object());
        if (texte(a, "c_etype") == "outline3D") {
          Modele3DEasyEda m;
          m.uuid = texte(a, "uuid");
          m.titre = texte(a, "title");
          const auto o = nombres(texte(a, "c_origin"));
          if (o.size() >= 2) {
            m.cx = (o[0] - x0) * UNITE_MM;
            m.cy = (o[1] - y0) * UNITE_MM;
          }
          m.z = nombre(a, "z") * UNITE_MM;
          const auto r = nombres(texte(a, "c_rotation"));
          if (r.size() >= 3) m.rz = r[2];
          m3d = m;
        }
      } catch (const std::exception&) {
        alertes.push_back("Nœud 3D de l'empreinte illisible");
      }
    }
  }
  if (d.pastilles.empty()) alertes.push_back("Empreinte sans pastille");
  if (enc.vide()) enc = Boite{-NM_PAR_MM, -NM_PAR_MM, NM_PAR_MM, NM_PAR_MM};
  d.encombrement = enc.gonflee(250'000);
  d.corps.largeur = d.encombrement.largeur();
  d.corps.profondeur = d.encombrement.hauteur();
  d.corps.hauteur = NM_PAR_MM;
  d.corps.decalage = d.encombrement.centre();
  if (m3d && cms) m3d->z = -m3d->z;  // CMS : EasyEDA compte la hauteur du nœud vers le bas
  else if (m3d) m3d->z = 0;
  return d;
}

const json& resultat(const json& j) { return j.contains("result") && j.at("result").is_object() ? j.at("result") : j; }
json dataStr(const json& x) {
  if (!x.contains("dataStr")) return json::object();
  const json& d = x.at("dataStr");
  if (d.is_string()) return json::parse(d.get<std::string>());
  return d;
}
}  // namespace

std::string stlDepuisObj(const std::string& obj, double dx, double dy, double dz, double rz) {
  std::vector<std::array<double, 3>> v;
  std::string o = "solid modele\n";
  const double c = std::cos(rz * std::numbers::pi / 180), s = std::sin(rz * std::numbers::pi / 180);
  auto sommet = [&](const std::array<double, 3>& q) {
    // OBJ : y vers le haut ; repère de l'empreinte : y vers le bas. Rotation en sens direct vue de dessus.
    const double x = q[0] * c - q[1] * s, y = q[0] * s + q[1] * c;
    return std::array<double, 3>{x + dx, -y + dy, q[2] + dz};
  };
  char tampon[160];
  std::istringstream in(obj);
  for (std::string ligne; std::getline(in, ligne);) {
    if (ligne.size() > 2 && ligne[0] == 'v' && ligne[1] == ' ') {
      std::istringstream l(ligne.substr(2));
      std::array<double, 3> q{0, 0, 0};
      l >> q[0] >> q[1] >> q[2];
      v.push_back(sommet(q));
    } else if (ligne.size() > 2 && ligne[0] == 'f' && ligne[1] == ' ') {
      std::istringstream l(ligne.substr(2));
      std::vector<int> ids;
      for (std::string x; l >> x;) {
        const int i = std::atoi(x.c_str());
        ids.push_back(i < 0 ? static_cast<int>(v.size()) + i : i - 1);
      }
      for (std::size_t k = 1; k + 1 < ids.size(); ++k) {
        const int ia = ids[0], ib = ids[k], ic = ids[k + 1];
        if (ia < 0 || ib < 0 || ic < 0 || ia >= static_cast<int>(v.size()) || ib >= static_cast<int>(v.size()) || ic >= static_cast<int>(v.size())) continue;
        // Le retournement de y inverse le sens des faces : b et c échangés
        const auto& A = v[static_cast<std::size_t>(ia)];
        const auto& B = v[static_cast<std::size_t>(ic)];
        const auto& C = v[static_cast<std::size_t>(ib)];
        const double ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2], wx = C[0] - A[0], wy = C[1] - A[1], wz = C[2] - A[2];
        double nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
        const double n = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (n < 1e-12) continue;
        nx /= n, ny /= n, nz /= n;
        std::snprintf(tampon, sizeof tampon, "facet normal %g %g %g\n outer loop\n", nx, ny, nz);
        o += tampon;
        for (const auto* P : {&A, &B, &C}) {
          std::snprintf(tampon, sizeof tampon, "  vertex %.6f %.6f %.6f\n", (*P)[0], (*P)[1], (*P)[2]);
          o += tampon;
        }
        o += " endloop\nendfacet\n";
      }
    }
  }
  return o + "endsolid modele\n";
}

std::string uuidModeleEasyEda(const std::string& texteJson) {
  try {
    const json j = json::parse(texteJson);
    const json& r = resultat(j);
    if (!r.contains("packageDetail")) return {};
    std::vector<std::string> alertes;
    std::optional<Modele3DEasyEda> m;
    empreinte(dataStr(r.at("packageDetail")), "x", alertes, m);
    return m ? m->uuid : std::string();
  } catch (const std::exception&) {
    return {};
  }
}

ImportArchive importerComposantEasyEda(const std::string& texteJson, const std::string& modele, const std::string& nomModele) {
  ImportArchive r;
  json j;
  try {
    j = json::parse(texteJson);
  } catch (const std::exception& e) {
    r.alertes.push_back(std::string("JSON illisible : ") + e.what());
    return r;
  }
  const json& res = resultat(j);
  json sym, pkg;
  try {
    sym = dataStr(res);
    if (res.contains("packageDetail")) pkg = dataStr(res.at("packageDetail"));
  } catch (const std::exception& e) {
    r.alertes.push_back(std::string("Données du composant illisibles : ") + e.what());
    return r;
  }
  if (!sym.contains("shape")) {
    r.alertes.push_back("Ce fichier n'est pas une réponse de l'API des composants d'EasyEDA (pas de « dataStr »).");
    return r;
  }
  const json& cpara = sym.value("head", json::object()).value("c_para", json::object());
  std::string code = texte(cpara, "BOM_Supplier Part");
  for (const char* cle : {"lcsc", "szlcsc"})
    if (code.empty() && res.contains(cle) && res.at(cle).is_object()) code = texte(res.at(cle), "number");
  std::string nom = texte(cpara, "name");
  if (nom.empty()) nom = texte(res, "title");
  if (nom.empty()) nom = code.empty() ? "Composant" : code;
  const std::string ident = code.empty() ? nom : code;
  // Symbole
  DefSymbole d = symbole(sym, "lcsc:" + ident, nom, r.alertes);
  if (d.broches.empty()) {
    r.alertes.push_back("Symbole sans broche");
    return r;
  }
  // Empreinte et modèle 3D
  std::optional<Modele3DEasyEda> m3d;
  std::string paquet = texte(cpara, "package");
  if (!pkg.is_null() && pkg.contains("shape")) {
    if (paquet.empty()) paquet = texte(res.at("packageDetail"), "title");
    DefEmpreinte e = empreinte(pkg, "lcsc:" + (paquet.empty() ? ident : paquet), r.alertes, m3d);
    if (!e.pastilles.empty()) {
      d.empreinte = e.id;
      r.empreinte = e;
    }
  } else
    r.alertes.push_back("Pas d'empreinte dans la réponse (« packageDetail » absent)");
  r.symboles.push_back(d);
  Article& a = r.article;
  a.id = "lcsc:" + ident;
  a.nom = nom;
  a.valeur = nom;
  a.categorie = "Importés de LCSC";
  a.description = texte(res, "description");
  if (a.description.empty()) a.description = "Composant LCSC " + ident + (paquet.empty() ? std::string() : " (" + paquet + ")");
  a.symbole = d.id;
  a.empreinte = d.empreinte;
  a.lcsc = code;
  a.fabricant = texte(cpara, "BOM_Manufacturer");
  if (a.fabricant.empty()) a.fabricant = texte(cpara, "Manufacturer");
  a.mpn = texte(cpara, "BOM_Manufacturer Part");
  if (a.mpn.empty()) a.mpn = texte(cpara, "Manufacturer Part");
  if (!texte(cpara, "link").empty()) a.fiche = texte(cpara, "link");
  if (res.contains("lcsc") && res.at("lcsc").is_object() && !texte(res.at("lcsc"), "url").empty()) a.parametres["pageLcsc"] = texte(res.at("lcsc"), "url");
  // Modèle 3D joint : OBJ d'EasyEDA changé en STL posé selon le nœud 3D, STEP gardé tel quel
  if (!modele.empty()) {
    std::string ext = nomModele.substr(nomModele.find_last_of('.') == std::string::npos ? nomModele.size() : nomModele.find_last_of('.') + 1);
    for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const bool estObj = ext == "obj" || (ext.empty() && modele.find("\nv ") != std::string::npos);
    if (estObj) {
      const Modele3DEasyEda m = m3d.value_or(Modele3DEasyEda{});
      r.modele3D = stlDepuisObj(modele, m.cx, m.cy, m.z, std::fmod(360 - m.rz, 360.0));
      r.nomModele3D = ident + ".stl";
    } else {
      r.modele3D = modele;
      r.nomModele3D = nomModele.empty() ? ident + ".step" : nomModele;
    }
    a.modele3D = r.nomModele3D;
  } else if (m3d && !m3d->uuid.empty())
    r.alertes.push_back("Modèle 3D « " + m3d->titre + " » non joint : il se télécharge sous l'identifiant " + m3d->uuid + " (accès en ligne activé ou fichier enregistré à côté)");
  if (r.empreinte) r.controles = controlerArticle(a, &r.symboles.front(), &*r.empreinte);
  return r;
}

}  // namespace prom::ecad

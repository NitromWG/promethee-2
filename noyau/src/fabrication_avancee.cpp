// Prométhée : panneau, relecture des fichiers de fabrication, impédance, devis, routage complet, visionneuse HTML
// (lot L8). Licence GPL-3.0-only.
#include "promethee/fabrication_avancee.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <set>
#include <sstream>

#include "promethee/hauteurs.hpp"
#include "promethee/routeur.hpp"
#include "promethee/verification.hpp"

namespace prom::ecad {

namespace {
// ---------- Union-find ----------
struct Ensembles {
  std::vector<int> p;
  int ajouter() {
    p.push_back(static_cast<int>(p.size()));
    return p.back();
  }
  int racine(int x) {
    while (p[static_cast<std::size_t>(x)] != x) x = p[static_cast<std::size_t>(x)] = p[static_cast<std::size_t>(p[static_cast<std::size_t>(x)])];
    return x;
  }
  void unir(int a, int b) { p[static_cast<std::size_t>(racine(a))] = racine(b); }
};
Pt decale(Pt p, Pt d) { return {p.x + d.x, p.y + d.y}; }
}  // namespace

// ---------- Panneau ----------
PanneauFabrication panneauFabrication(const ConceptionElec& c, const OptionsPanneau& o, const std::string& version) {
  (void)version;
  PanneauFabrication r;
  const Connectivite k = connectivite(c);
  const Remplissages z = remplirZones(c, k);
  const ModeleFabrication base = modeleFabrication(c, k, z);
  for (const auto& v : verifierDRCReglee(c, k, z))
    if (v.gravite == Gravite::Erreur) r.defauts.push_back("carte : " + v.code + " — " + v.message);
  const Nm W = base.boite.largeur(), H = base.boite.hauteur(), ecart = nm(o.ecart), rail = nm(o.rail);
  const int C = std::max(1, o.colonnes), L = std::max(1, o.lignes);
  const Nm PW = 2 * rail + 2 * ecart + C * W + (C - 1) * ecart, PH = 2 * rail + 2 * ecart + L * H + (L - 1) * ecart;
  ModeleFabrication& m = r.modele;
  m.nom = base.nom + " (panneau " + std::to_string(C) + " × " + std::to_string(L) + ")";
  m.boite = {0, 0, PW, PH};
  m.contour = {{0, 0}, {PW, 0}, {PW, PH}, {0, PH}};
  auto origine = [&](int i, int j) { return Pt{rail + ecart + j * (W + ecart) - base.boite.x0, rail + ecart + i * (H + ecart) - base.boite.y0}; };
  // Cuivre, vernis, pâte, sérigraphie : chaque carte à sa place
  for (const auto& cf : base.couches) {
    CoucheFab n;
    n.couche = cf.couche;
    if (cf.couche != Couche::Contour)
      for (int i = 0; i < L; ++i)
        for (int j = 0; j < C; ++j) {
          const Pt d = origine(i, j);
          for (PrimitiveFab p : cf.primitives) {
            p.a = decale(p.a, d);
            p.b = decale(p.b, d);
            for (Pt& q : p.region) q = decale(q, d);
            n.primitives.push_back(std::move(p));
          }
        }
    m.couches.push_back(std::move(n));
  }
  for (int i = 0; i < L; ++i)
    for (int j = 0; j < C; ++j)
      for (TrouFab t : base.trous) {
        t.p = decale(t.p, origine(i, j));
        m.trous.push_back(t);
      }
  // Languettes : sur les bords haut et bas de chaque carte, aux mêmes abscisses d'une rangée à l'autre
  const int n = std::max(1, o.languettesParBord);
  const Nm lw = nm(o.largeurLanguette);
  auto languettes = [&](int j) {
    std::vector<std::pair<Nm, Nm>> v;
    const Nm x0 = rail + ecart + j * (W + ecart);
    for (int t = 0; t < n; ++t) {
      const Nm cx = x0 + W * (t + 1) / (n + 1);
      v.push_back({cx - lw / 2, cx + lw / 2});
    }
    return v;
  };
  CoucheFab* contour = nullptr;
  for (auto& cf : m.couches)
    if (cf.couche == Couche::Contour) contour = &cf;
  if (!contour) {
    m.couches.push_back(CoucheFab{Couche::Contour, {}});
    contour = &m.couches.back();
  }
  auto trait = [&](Pt a, Pt b) {
    if (a == b) return;
    PrimitiveFab p;
    p.type = PrimitiveFab::Type::Trait;
    p.a = a;
    p.b = b;
    p.l = 100'000;
    p.fonction = "Profile";
    contour->primitives.push_back(p);
  };
  // Un segment horizontal coupé là où passent les languettes
  auto horizontalCoupe = [&](Pt a, Pt b, const std::vector<std::pair<Nm, Nm>>& trous) {
    if (a.x > b.x) std::swap(a, b);
    Nm x = a.x;
    for (const auto& [t0, t1] : trous) {
      if (t1 <= x || t0 >= b.x) continue;
      if (t0 > x) trait({x, a.y}, {t0, a.y});
      x = std::max(x, t1);
    }
    if (x < b.x) trait({x, a.y}, b);
  };
  // Cadre extérieur, et bord intérieur du cadre (coupé en haut et en bas par les languettes de la première et de la
  // dernière rangée)
  trait({0, 0}, {PW, 0});
  trait({PW, 0}, {PW, PH});
  trait({PW, PH}, {0, PH});
  trait({0, PH}, {0, 0});
  std::vector<std::pair<Nm, Nm>> toutes;
  for (int j = 0; j < C; ++j)
    for (const auto& x : languettes(j)) toutes.push_back(x);
  std::sort(toutes.begin(), toutes.end());
  horizontalCoupe({rail, rail}, {PW - rail, rail}, toutes);
  horizontalCoupe({rail, PH - rail}, {PW - rail, PH - rail}, toutes);
  trait({PW - rail, rail}, {PW - rail, PH - rail});
  trait({rail, rail}, {rail, PH - rail});
  // Le contour de chaque carte, coupé aux languettes sur ses bords haut et bas
  for (int i = 0; i < L; ++i)
    for (int j = 0; j < C; ++j) {
      const Pt d = origine(i, j);
      const Nm haut = base.boite.y0 + d.y, bas = base.boite.y1 + d.y;
      const auto l = languettes(j);
      for (std::size_t q = 0; q < base.contour.size(); ++q) {
        const Pt a = decale(base.contour[q], d), b = decale(base.contour[(q + 1) % base.contour.size()], d);
        if (a.y == b.y && (std::abs(a.y - haut) <= 1 || std::abs(a.y - bas) <= 1)) horizontalCoupe(a, b, l);
        else trait(a, b);
      }
      // Perforations : une rangée par languette, tangente au bord de la carte, côté languette : le trou n'entame pas
      // la carte et le cuivre garde sa distance au bord
      const Nm rt = nm(o.diametreTrou) / 2;
      for (const Nm y : {haut - rt, bas + rt})
        for (const auto& [t0, t1] : l) {
          ++r.languettes;
          for (Nm x = t0 + nm(o.pasTrous) / 2; x <= t1 - nm(o.diametreTrou) / 2; x += nm(o.pasTrous)) {
            m.trous.push_back({{x, y}, nm(o.diametreTrou), false, "MechanicalDrill"});
            ++r.perforations;
          }
        }
    }
  // Contrôles du panneau : une perforation loin du cuivre (marge au bord des règles)
  const Nm bord = c.circuit.regles.bord;
  for (const auto& t : m.trous) {
    if (t.metallise || t.fonction != "MechanicalDrill" || t.diametre != nm(o.diametreTrou)) continue;
    for (const auto& cf : m.couches) {
      if (cf.couche != Couche::CuivreDessus && cf.couche != Couche::CuivreDessous) continue;
      for (const auto& p : cf.primitives) {
        double d = 0;
        if (p.type == PrimitiveFab::Type::Region) d = distancePolygoneSegment(p.region, t.p, t.p);
        else if (p.type == PrimitiveFab::Type::Trait) d = distancePointSegment(t.p, p.a, p.b) - static_cast<double>(p.l) / 2;
        else d = distance(t.p, p.a) - static_cast<double>(std::max(p.l, p.h)) / 2;
        if (d - static_cast<double>(t.diametre) / 2 < static_cast<double>(bord) - 1000) {
          char x[160];
          std::snprintf(x, sizeof x, "perforation en %.2f ; %.2f mm à %.2f mm du cuivre (minimum %.2f mm)", mm(t.p.x), mm(t.p.y), mm(static_cast<Nm>(d - static_cast<double>(t.diametre) / 2)), mm(bord));
          r.defauts.push_back(x);
          goto suivant;
        }
      }
    }
  suivant:;
  }
  return r;
}

// ---------- Relecture ----------
LectureGerber lireGerber(const std::string& texte) {
  LectureGerber r;
  struct Ouverture {
    char forme = 'C';
    Nm l = 0, h = 0;
  };
  std::map<int, Ouverture> ouvertures;
  int courante = -1;
  Nm x = 0, y = 0;
  bool region = false;
  Polygone pointsRegion;
  std::string ref, num, reseau;
  auto valeurMm = [](const std::string& s) { return static_cast<Nm>(std::llround(std::stod(s) * 1e6)); };
  auto forme = [&](Pt p) -> Polygone {
    const auto it = ouvertures.find(courante);
    if (it == ouvertures.end()) return cercle(p, 50'000);
    const Ouverture& o = it->second;
    if (o.forme == 'C') return cercle(p, std::max<Nm>(o.l / 2, 500));
    if (o.forme == 'R') return rectangle(p, o.l, o.h);
    if (o.l >= o.h) return oblong({p.x - (o.l - o.h) / 2, p.y}, {p.x + (o.l - o.h) / 2, p.y}, o.h);
    return oblong({p.x, p.y - (o.h - o.l) / 2}, {p.x, p.y + (o.h - o.l) / 2}, o.l);
  };
  // Un plan en négatif n'est pas relu comme du cuivre posé
  if (texte.find("%TF.FilePolarity,Negative") != std::string::npos) return r;
  std::istringstream in(texte);
  std::string ligne;
  while (std::getline(in, ligne)) {
    if (!ligne.empty() && ligne.back() == '\r') ligne.pop_back();
    if (ligne.empty()) continue;
    try {
      if (ligne.rfind("%ADD", 0) == 0) {
        // %ADD10C,0.800000*%
        std::size_t i = 4;
        int code = 0;
        while (i < ligne.size() && std::isdigit(static_cast<unsigned char>(ligne[i]))) code = code * 10 + (ligne[i++] - '0');
        Ouverture o;
        o.forme = ligne[i];
        const std::string reste = ligne.substr(i + 2, ligne.find('*') - i - 2);
        const auto xx = reste.find('X');
        o.l = valeurMm(reste.substr(0, xx));
        o.h = xx == std::string::npos ? o.l : valeurMm(reste.substr(xx + 1));
        ouvertures[code] = o;
        continue;
      }
      if (ligne.rfind("%TO.P,", 0) == 0) {
        const std::string v = ligne.substr(6, ligne.find('*') - 6);
        const auto v1 = v.find(',');
        ref = v.substr(0, v1);
        const auto v2 = v.find(',', v1 + 1);
        num = v1 == std::string::npos ? std::string() : v.substr(v1 + 1, v2 == std::string::npos ? std::string::npos : v2 - v1 - 1);
        continue;
      }
      if (ligne.rfind("%TO.N,", 0) == 0) {
        reseau = ligne.substr(6, ligne.find('*') - 6);
        continue;
      }
      if (ligne.rfind("%TD.P", 0) == 0) {
        ref.clear();
        num.clear();
        continue;
      }
      if (ligne.rfind("%TD.N", 0) == 0) {
        reseau.clear();
        continue;
      }
      if (ligne == "%TD*%") {
        ref.clear();
        num.clear();
        reseau.clear();
        continue;
      }
      if (ligne[0] == '%') continue;
      if (ligne.rfind("G36", 0) == 0) {
        region = true;
        pointsRegion.clear();
        continue;
      }
      if (ligne.rfind("G37", 0) == 0) {
        region = false;
        if (pointsRegion.size() >= 3) r.objets.push_back({ObjetGerber::Genre::Region, pointsRegion, ref, num, reseau});
        continue;
      }
      if (ligne[0] == 'D' && ligne.size() > 1 && std::isdigit(static_cast<unsigned char>(ligne[1]))) {
        const int code = std::stoi(ligne.substr(1));
        if (code >= 10) courante = code;
        continue;
      }
      if (ligne[0] == 'X' || ligne[0] == 'Y') {
        Nm nx = x, ny = y;
        std::size_t i = 0;
        auto entier = [&](std::size_t& j) {
          std::size_t f = j;
          if (f < ligne.size() && (ligne[f] == '-' || ligne[f] == '+')) ++f;
          while (f < ligne.size() && std::isdigit(static_cast<unsigned char>(ligne[f]))) ++f;
          const Nm v = std::stoll(ligne.substr(j, f - j));
          j = f;
          return v;
        };
        if (ligne[i] == 'X') {
          ++i;
          nx = entier(i);
        }
        if (i < ligne.size() && ligne[i] == 'Y') {
          ++i;
          ny = entier(i);
        }
        const std::string op = ligne.substr(i, 3);
        const Pt a{x, y}, b{nx, ny};
        if (op == "D01") {
          if (region) {
            if (pointsRegion.empty()) pointsRegion.push_back(a);
            pointsRegion.push_back(b);
          } else {
            const auto it = ouvertures.find(courante);
            const Nm l = it == ouvertures.end() ? 100'000 : it->second.l;
            r.objets.push_back({ObjetGerber::Genre::Trait, oblong(a, b, std::max<Nm>(l, 1000)), ref, num, reseau});
          }
        } else if (op == "D02") {
          if (region) {
            if (pointsRegion.size() >= 3) r.objets.push_back({ObjetGerber::Genre::Region, pointsRegion, ref, num, reseau});
            pointsRegion = {b};
          }
        } else if (op == "D03")
          r.objets.push_back({ObjetGerber::Genre::Flash, forme(b), ref, num, reseau});
        x = nx;
        y = ny;
      }
    } catch (const std::exception&) {
      r.erreurs.push_back("ligne illisible : " + ligne);
    }
  }
  return r;
}

std::vector<std::pair<Pt, Nm>> lirePercages(const std::string& texte) {
  std::vector<std::pair<Pt, Nm>> r;
  std::map<int, Nm> outils;
  int courant = 0;
  std::istringstream in(texte);
  std::string ligne;
  bool entete = true;
  while (std::getline(in, ligne)) {
    if (ligne.empty() || ligne[0] == ';') continue;
    if (ligne == "%") {
      entete = false;
      continue;
    }
    if (ligne[0] == 'T') {
      const auto c = ligne.find('C');
      const int t = std::stoi(ligne.substr(1, c == std::string::npos ? std::string::npos : c - 1));
      if (entete && c != std::string::npos) outils[t] = static_cast<Nm>(std::llround(std::stod(ligne.substr(c + 1)) * 1e6));
      else courant = t;
      continue;
    }
    if (ligne[0] == 'X' && !entete) {
      const auto yy = ligne.find('Y');
      const double x = std::stod(ligne.substr(1, yy - 1)), y = std::stod(ligne.substr(yy + 1));
      r.push_back({{std::llround(x * 1e6), std::llround(y * 1e6)}, outils[courant]});
    }
  }
  return r;
}

std::vector<std::vector<std::string>> groupesRelus(const std::vector<LectureGerber>& cuivres, const std::vector<std::pair<Pt, Nm>>& percages) {
  struct Ref {
    std::size_t couche, objet;
  };
  std::vector<Ref> refs;
  std::vector<Boite> boites;
  for (std::size_t l = 0; l < cuivres.size(); ++l)
    for (std::size_t i = 0; i < cuivres[l].objets.size(); ++i) {
      refs.push_back({l, i});
      boites.push_back(boite(cuivres[l].objets[i].forme));
    }
  Ensembles u;
  for (std::size_t i = 0; i < refs.size(); ++i) u.ajouter();
  IndexSpatial index(2 * NM_PAR_MM);
  for (std::size_t i = 0; i < refs.size(); ++i) index.inserer(static_cast<int>(i), boites[i]);
  for (std::size_t i = 0; i < refs.size(); ++i)
    for (int j : index.chercher(boites[i])) {
      if (static_cast<std::size_t>(j) <= i || refs[static_cast<std::size_t>(j)].couche != refs[i].couche) continue;
      if (!boites[i].touche(boites[static_cast<std::size_t>(j)])) continue;
      const auto& a = cuivres[refs[i].couche].objets[refs[i].objet].forme;
      const auto& b = cuivres[refs[static_cast<std::size_t>(j)].couche].objets[refs[static_cast<std::size_t>(j)].objet].forme;
      if (distancePolygones(a, b) <= 0) u.unir(static_cast<int>(i), j);
    }
  // Un perçage métallisé relie ce qu'il traverse sur chaque couche
  for (const auto& [p, d] : percages) {
    Boite b;
    b.etendre(p);
    int premier = -1;
    for (int j : index.chercher(b.gonflee(d / 2))) {
      const auto& f = cuivres[refs[static_cast<std::size_t>(j)].couche].objets[refs[static_cast<std::size_t>(j)].objet].forme;
      if (dedans(f, p)) {
        if (premier < 0) premier = j;
        else u.unir(premier, j);
      }
    }
  }
  std::map<int, std::set<std::string>> parRacine;
  for (std::size_t i = 0; i < refs.size(); ++i) {
    const ObjetGerber& o = cuivres[refs[i].couche].objets[refs[i].objet];
    if (!o.reference.empty() && !o.numero.empty()) parRacine[u.racine(static_cast<int>(i))].insert(o.reference + "." + o.numero);
  }
  std::vector<std::vector<std::string>> r;
  for (auto& [rac, s] : parRacine) r.emplace_back(s.begin(), s.end());
  std::sort(r.begin(), r.end());
  return r;
}

namespace {
std::pair<std::vector<LectureGerber>, std::vector<std::pair<Pt, Nm>>> fichiersEnMemoire(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
  std::vector<LectureGerber> cu;
  for (const auto& cf : m.couches)
    if (estCuivre(cf.couche)) cu.push_back(lireGerber(gerber(m, cf.couche, "verification")));
  return {cu, lirePercages(excellon(m, true, "verification"))};
}
}  // namespace

std::vector<std::vector<std::string>> groupesConception(const ConceptionElec& c) {
  const auto [cu, pe] = fichiersEnMemoire(c);
  return groupesRelus(cu, pe);
}

std::vector<std::string> comparerFabrication(const ConceptionElec& c, const std::map<Couche, std::string>& gerbers, const std::string& percagesMetallises) {
  std::vector<std::string> r;
  std::vector<LectureGerber> cu;
  for (const auto& [couche, texte] : gerbers)
    if (estCuivre(couche)) {
      cu.push_back(lireGerber(texte));
      for (const auto& e : cu.back().erreurs) r.push_back(std::string(nomCouche(couche)) + " : " + e);
    }
  // Référence : les mêmes groupes tirés du modèle de fabrication en mémoire (liaisons de réseaux voulues et cuivre
  // réellement posé compris) ; les fichiers relus doivent dire exactement la même chose
  const auto relus = groupesRelus(cu, lirePercages(percagesMetallises));
  const auto attendus = groupesConception(c);
  std::map<std::string, std::size_t> gR, gC;
  for (std::size_t i = 0; i < relus.size(); ++i)
    for (const auto& p : relus[i]) gR[p] = i;
  for (std::size_t i = 0; i < attendus.size(); ++i)
    for (const auto& p : attendus[i]) gC[p] = i;
  const Connectivite k = connectivite(c);
  std::map<std::string, std::string> reseauDe;  // « R1.2 » → réseau
  for (const auto& p : pastilles(c, k))
    if (!p.reference.empty() && !p.numero.empty()) reseauDe[p.reference + "." + p.numero] = p.reseau;
  auto nomReseau = [&](const std::string& p) {
    const auto it = reseauDe.find(p);
    return it == reseauDe.end() || it->second.empty() ? std::string("sans réseau") : "réseau « " + it->second + " »";
  };
  for (const auto& g : attendus) {
    std::map<std::size_t, std::string> morceaux;  // groupe relu → première pastille
    for (const auto& p : g) {
      const auto it = gR.find(p);
      if (it == gR.end()) {
        r.push_back("la pastille " + p + " (" + nomReseau(p) + ") manque dans les fichiers");
        continue;
      }
      morceaux.emplace(it->second, p);
    }
    if (morceaux.size() > 1) {
      auto a = morceaux.begin(), b = std::next(a);
      r.push_back("coupure entre " + a->second + " et " + b->second + " (" + nomReseau(a->second) + ") : reliées dans la conception, séparées dans les fichiers" +
                  (morceaux.size() > 2 ? " (" + std::to_string(morceaux.size()) + " morceaux)" : std::string()));
    }
  }
  for (const auto& g : relus) {
    std::map<std::size_t, std::string> morceaux;
    for (const auto& p : g)
      if (const auto it = gC.find(p); it != gC.end()) morceaux.emplace(it->second, p);
    if (morceaux.size() > 1) {
      auto a = morceaux.begin(), b = std::next(a);
      r.push_back("court-circuit entre " + a->second + " (" + nomReseau(a->second) + ") et " + b->second + " (" + nomReseau(b->second) + ") dans les fichiers");
    }
  }
  return r;
}

// ---------- Impédance ----------
Microruban microrubanDe(const Empilage& e) {
  Microruban m;
  m.h = e.couchesCuivre >= 4 ? 0.2 : e.epaisseur;
  m.t = e.cuivre;
  std::string mat = e.materiau;
  for (char& ch : mat) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  m.er = mat.find("ROGERS") != std::string::npos || mat.find("RO4") != std::string::npos ? 3.48 : mat.find("ALU") != std::string::npos ? 4.0 : 4.5;
  return m;
}

double impedanceMicroruban(double w, const Microruban& m) { return 87.0 / std::sqrt(m.er + 1.41) * std::log(5.98 * m.h / (0.8 * w + m.t)); }

double impedanceDifferentielle(double w, double s, const Microruban& m) { return 2 * impedanceMicroruban(w, m) * (1 - 0.48 * std::exp(-0.96 * s / m.h)); }

ProposPaire proposerPaire(double zdiff, const Empilage& e) {
  const Microruban m = microrubanDe(e);
  auto ecartDe = [](double w) { return std::max(0.15, w); };
  double a = 0.05, b = 5.0;
  for (int i = 0; i < 80; ++i) {
    const double w = (a + b) / 2;
    if (impedanceDifferentielle(w, ecartDe(w), m) > zdiff) a = w;
    else b = w;
  }
  ProposPaire p;
  p.largeur = std::round((a + b) / 2 * 1000) / 1000;
  p.ecart = std::round(ecartDe(p.largeur) * 1000) / 1000;
  p.z0 = impedanceMicroruban(p.largeur, m);
  p.zdiff = impedanceDifferentielle(p.largeur, p.ecart, m);
  return p;
}

// ---------- Devis ----------
std::vector<OffreFabrication> devis(const ConceptionElec& c, int quantite) {
  struct Grille {
    const char* fabricant;
    const char* date;
    double base[3];      // 1, 2, 4 couches
    double parCm2[3];
    double enig, epaisseur;
    double delai;
  };
  // Grilles datées des profils (prix publics relevés à la date indiquée, hors port et taxes)
  static const Grille grilles[] = {{"JLCPCB", "2026-10-01", {2.0, 2.0, 7.0}, {0.010, 0.012, 0.030}, 15.0, 5.0, 3},
                                   {"PCBWay", "2026-10-01", {5.0, 5.0, 28.0}, {0.012, 0.014, 0.035}, 18.0, 6.0, 4}};
  const Boite b = boite(c.circuit.contour.polygone());
  const double surface = mm(b.largeur()) * mm(b.hauteur()) / 100.0;  // cm² par carte
  const int couches = c.circuit.empilage.couchesCuivre;
  const int rang = couches <= 1 ? 0 : couches <= 2 ? 1 : 2;
  std::vector<OffreFabrication> r;
  for (const auto& g : grilles) {
    OffreFabrication o;
    o.fabricant = g.fabricant;
    o.date = g.date;
    o.quantite = quantite;
    o.prix = g.base[rang] + surface * quantite * g.parCm2[rang];
    char t[160];
    std::snprintf(t, sizeof t, "%d couche(s), %.1f cm² par carte, %d cartes", couches, surface, quantite);
    o.details.push_back(t);
    std::string fin = c.circuit.empilage.finition;
    for (char& ch : fin) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (fin.find("ENIG") != std::string::npos) {
      o.prix += g.enig;
      o.details.push_back("finition ENIG");
    }
    if (std::abs(c.circuit.empilage.epaisseur - 1.6) > 1e-6) {
      o.prix += g.epaisseur;
      o.details.push_back("épaisseur hors standard");
    }
    o.prix = std::round(o.prix * 100) / 100;
    o.delaiJours = g.delai;
    r.push_back(o);
  }
  return r;
}

// ---------- Routage complet ----------
BilanRoutage routerComplet(ConceptionElec& c) {
  exigerModifiable(c);
  std::size_t aFaire = 0;
  {
    const Connectivite k = connectivite(c);
    aFaire = etatRoutage(c, k, remplirZones(c, k)).chevelu.size();
  }
  const std::size_t viasAvant = c.circuit.vias.size();
  routerSimple(c);
  for (int passe = 0; passe < 3; ++passe) {
    const Connectivite k = connectivite(c);
    auto chevelu = etatRoutage(c, k, remplirZones(c, k)).chevelu;
    if (chevelu.empty()) break;
    std::sort(chevelu.begin(), chevelu.end(), [](const Liaison& a, const Liaison& b) { return distance(a.a, a.b) < distance(b.a, b.b); });
    int poses = 0;
    for (const auto& l : chevelu)
      if (routerLiaison(c, l).empty()) ++poses;
    if (poses == 0) break;
  }
  BilanRoutage b;
  b.nonRoutees = diagnostiquerChevelu(c);
  b.restantes = static_cast<int>(b.nonRoutees.size());
  b.vias = static_cast<int>(c.circuit.vias.size() - viasAvant);
  b.liaisons = static_cast<int>(aFaire > b.nonRoutees.size() ? aFaire - b.nonRoutees.size() : 0);
  return b;
}

// ---------- Visionneuse HTML ----------
std::string visionneuseHtml(const ConceptionElec& c) {
  auto x3 = [](double v) {
    char t[32];
    std::snprintf(t, sizeof t, "%.3f", v);
    return std::string(t);
  };
  auto echap = [](const std::string& s) {
    std::string r;
    for (char ch : s) r += ch == '<' ? "&lt;" : ch == '>' ? "&gt;" : ch == '&' ? "&amp;" : ch == '"' ? "&quot;" : ch == '\'' ? "&#39;" : std::string(1, ch);
    return r;
  };
  auto js = [](const std::string& s) {
    std::string r = "\"";
    for (char ch : s) r += ch == '"' || ch == '\\' ? std::string("\\") + ch : ch == '<' ? std::string("\\u003c") : std::string(1, ch);
    return r + "\"";
  };
  auto clic = [&](const std::string& r) { return " data-ref=\"" + echap(r) + "\" onclick=\"allumer(this.dataset.ref)\""; };
  std::ostringstream o;
  o << "<!doctype html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\"><title>" << echap(c.nom) << "</title>\n<style>"
       "body{font-family:sans-serif;margin:0;display:grid;grid-template-columns:1fr 1fr;grid-template-rows:1fr 1fr;height:100vh;color:#1d2330}"
       "section{position:relative;overflow:auto;border:1px solid #ccd}h3{position:absolute;margin:4px 8px;font-size:13px;color:#667}"
       "#circuit{background:#20242c}#vue3d{background:#e8ebf0}svg,canvas{width:100%;height:100%;display:block}"
       "table{border-collapse:collapse;width:100%;margin-top:28px}td,th{border-bottom:1px solid #ccd;padding:3px 5px;text-align:left}"
       "tr.allume{background:#ffe9a6}.pastille{fill:#c8ccd4}.allume .pastille{fill:#ffcc33}.ref{fill:#eee;font-size:1.2px;text-anchor:middle;cursor:pointer}"
       ".sym{fill:#fffbe8;stroke:#8a1c1c;stroke-width:0.15}.allume .sym{fill:#ffcc33}.fil{stroke:#2a7a2a;stroke-width:0.15}"
       ".refs{font-size:1.6px;fill:#333;text-anchor:middle;cursor:pointer}a{color:#246}"
       "</style></head><body>\n";
  // Schéma : fils et symboles de chaque feuille, côte à côte
  {
    o << "<section id=\"schema\"><h3>Schéma</h3>";
    for (const auto& f : c.feuilles) {
      Boite b;
      for (const auto& fil : f.fils) {
        b.etendre(fil.a);
        b.etendre(fil.b);
      }
      for (const auto& s : f.symboles)
        if (const DefSymbole* d = c.symbole(s.symbole)) b.etendre(boiteSymbole(s, *d));
      if (b.vide()) continue;
      b = b.gonflee(5 * NM_PAR_MM);
      o << "<svg class=\"feuille\" data-feuille=\"" << echap(f.nom) << "\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << x3(mm(b.x0)) << ' ' << x3(mm(b.y0)) << ' ' << x3(mm(b.largeur())) << ' ' << x3(mm(b.hauteur())) << "\">";
      for (const auto& fil : f.fils) o << "<line class=\"fil\" x1=\"" << x3(mm(fil.a.x)) << "\" y1=\"" << x3(mm(fil.a.y)) << "\" x2=\"" << x3(mm(fil.b.x)) << "\" y2=\"" << x3(mm(fil.b.y)) << "\"/>";
      for (const auto& s : f.symboles) {
        const DefSymbole* d = c.symbole(s.symbole);
        if (!d) continue;
        const Boite sb = boiteSymbole(s, *d);
        const ComposantElec* x = s.composant.empty() ? nullptr : c.composant(s.composant);
        const std::string ref = x ? x->reference : s.nomAlim;
        o << "<g" << (x ? clic(ref) : std::string()) << "><rect class=\"sym\" x=\"" << x3(mm(sb.x0)) << "\" y=\"" << x3(mm(sb.y0)) << "\" width=\"" << x3(mm(sb.largeur())) << "\" height=\"" << x3(mm(sb.hauteur()))
          << "\"/><text class=\"refs\" x=\"" << x3(mm(sb.centre().x)) << "\" y=\"" << x3(mm(sb.centre().y) + 0.6) << "\">" << echap(ref) << "</text></g>";
      }
      o << "</svg>";
    }
    o << "</section>\n";
  }
  // Circuit : contour, pastilles, repères
  const Polygone carte = c.circuit.contour.polygone();
  const Boite b = boite(carte);
  o << "<section id=\"circuit\"><h3>Circuit</h3><svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << x3(mm(b.x0) - 2) << ' ' << x3(mm(b.y0) - 2) << ' ' << x3(mm(b.largeur()) + 4) << ' ' << x3(mm(b.hauteur()) + 4)
    << "\">\n<polygon fill=\"#1f6b45\" stroke=\"#ccc\" stroke-width=\"0.1\" points=\"";
  for (const Pt p : carte) o << x3(mm(p.x)) << ',' << x3(mm(p.y)) << ' ';
  o << "\"/>\n";
  std::ostringstream corps;  // pour la 3D
  for (const auto& e : c.circuit.empreintes) {
    const ComposantElec* x = c.composant(e.composant);
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    if (!x || !d) continue;
    o << "<g" << clic(x->reference) << ">";
    for (const auto& p : d->pastilles) {
      o << "<polygon class=\"pastille\" points=\"";
      for (const Pt q : formePastille(pastilleSurCarte(e, p))) o << x3(mm(q.x)) << ',' << x3(mm(q.y)) << ' ';
      o << "\"/>";
    }
    const Pt rp = positionRepere(e, *d, c.circuit.serigraphie.hauteur);
    o << "<text class=\"ref\" x=\"" << x3(mm(rp.x)) << "\" y=\"" << x3(mm(rp.y)) << "\">" << echap(x->reference) << "</text></g>\n";
    if (const auto cp = corpsPose(c, e))
      corps << "{r:" << js(x->reference) << ",x:" << x3(mm(cp->centre.x)) << ",y:" << x3(mm(cp->centre.y)) << ",l:" << x3(cp->largeur) << ",p:" << x3(cp->profondeur) << ",a:" << x3(cp->rotation) << ",h:"
            << x3(std::max(0.2, cp->hauteur.mm)) << ",d:" << (cp->dessous ? 1 : 0) << "},";
  }
  o << "</svg></section>\n";
  // 3D : corps des composants sur la carte, tournée à la souris (sans bibliothèque ni réseau)
  o << "<section id=\"vue3d\"><h3>3D</h3><canvas id=\"toile\"></canvas></section>\n";
  // Nomenclature
  o << "<section id=\"nom\"><h3>Nomenclature — " << echap(c.nom) << "</h3><table><tr><th>Repères</th><th>Valeur</th><th>Empreinte</th><th>Qté</th></tr>\n";
  for (const auto& l : nomenclature(c)) {
    o << "<tr data-refs=\"";
    for (const auto& r : l.references) o << echap(r) << ' ';
    o << "\"><td>";
    for (const auto& r : l.references) o << "<a href=\"#\"" << clic(r) << ">" << echap(r) << "</a> ";
    o << "</td><td>" << echap(l.valeur) << "</td><td>" << echap(l.empreinte) << "</td><td>" << l.quantite << "</td></tr>\n";
  }
  o << "</table></section>\n<script>\nvar carte=[";
  for (const Pt p : carte) o << '[' << x3(mm(p.x)) << ',' << x3(mm(p.y)) << "],";
  o << "],epaisseur=" << x3(c.circuit.empilage.epaisseur) << ",corps=[" << corps.str() << "],choisi='';\n"
    << R"JS(function allumer(r){choisi=r;document.querySelectorAll('.allume').forEach(function(e){e.classList.remove('allume')});
document.querySelectorAll('[data-ref]').forEach(function(e){if(e.dataset.ref===r&&e.tagName!=='A')e.classList.add('allume')});
document.querySelectorAll('tr[data-refs]').forEach(function(e){if((' '+e.dataset.refs).indexOf(' '+r+' ')>=0)e.classList.add('allume')});dessiner();return false;}
var az=-0.6,el=0.9,toile=document.getElementById('toile'),cx=0,cy=0;carte.forEach(function(p){cx+=p[0]/carte.length;cy+=p[1]/carte.length});
function proj(x,y,z){x-=cx;y=cy-y;var X=x*Math.cos(az)-y*Math.sin(az),Y=x*Math.sin(az)+y*Math.cos(az);return[X,Y*Math.cos(el)+z*Math.sin(el),Y*Math.sin(el)-z*Math.cos(el)];}
function dessiner(){var w=toile.width=toile.clientWidth,h=toile.height=toile.clientHeight,g=toile.getContext('2d'),k=0;
carte.forEach(function(p){k=Math.max(k,Math.abs(p[0]-cx),Math.abs(p[1]-cy))});var s=0.42*Math.min(w,h)/Math.max(k,1),faces=[];
function face(pts,coul,ref){var q=pts.map(function(p){return proj(p[0],p[1],p[2])}),z=0;q.forEach(function(p){z+=p[2]/q.length});faces.push({q:q,z:z,c:coul,r:ref});}
face(carte.map(function(p){return[p[0],p[1],0]}),'#1f6b45','');face(carte.map(function(p){return[p[0],p[1],-epaisseur]}),'#174f33','');
corps.forEach(function(b){var c=Math.cos(b.a*Math.PI/180),sn=Math.sin(b.a*Math.PI/180),z0=b.d?-epaisseur:0,z1=b.d?-epaisseur-b.h:b.h;
var co=[[-1,-1],[1,-1],[1,1],[-1,1]].map(function(u){var x=u[0]*b.l/2,y=u[1]*b.p/2;return[b.x+x*c-y*sn,b.y+x*sn+y*c]});
var coul=b.r===choisi?'#ffcc33':'#3a3d44';face(co.map(function(p){return[p[0],p[1],z1]}),coul,b.r);
for(var i=0;i<4;i++){var a=co[i],e=co[(i+1)%4];face([[a[0],a[1],z0],[e[0],e[1],z0],[e[0],e[1],z1],[a[0],a[1],z1]],b.r===choisi?'#e0a800':'#2a2c31',b.r);}});
faces.sort(function(a,b){return b.z-a.z});g.clearRect(0,0,w,h);faces.forEach(function(f){g.beginPath();f.q.forEach(function(p,i){var X=w/2+p[0]*s,Y=h/2-p[1]*s;i?g.lineTo(X,Y):g.moveTo(X,Y)});g.closePath();g.fillStyle=f.c;g.fill();g.strokeStyle='rgba(0,0,0,.35)';g.stroke();});}
var glisse=null;toile.onmousedown=function(e){glisse=[e.clientX,e.clientY]};window.onmouseup=function(){glisse=null};
window.onmousemove=function(e){if(!glisse)return;az+=(e.clientX-glisse[0])*0.01;el=Math.max(0.05,Math.min(1.5,el+(e.clientY-glisse[1])*0.01));glisse=[e.clientX,e.clientY];dessiner();};
window.onresize=dessiner;dessiner();
</script>
</body></html>
)JS";
  return o.str();
}

}  // namespace prom::ecad

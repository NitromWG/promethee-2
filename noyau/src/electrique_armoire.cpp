// Prométhée : armoire électrique (plaque de fond équipée de rails DIN et de goulottes, ou coffret modulaire de
// logement) et faisceaux de câblage. Implantation automatique par famille d'appareils, bilan de remplissage et de
// réserve, plan de perçage de la plaque (DXF R12), longueurs de fils d'un faisceau. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "electrique_interne.hpp"
#include "promethee/electrique.hpp"

namespace prom::elec {

using ecad::Boite;
using ecad::ComposantElec;
using ecad::NM_PAR_MM;
using ecad::Nm;
using ecad::Pt;

namespace {

Nm mm(double v) { return static_cast<Nm>(std::llround(v * static_cast<double>(NM_PAR_MM))); }
double versMm(Nm v) { return static_cast<double>(v) / static_cast<double>(NM_PAR_MM); }

constexpr double LARGEUR_GOULOTTE = 40, PAS_RANGEE = 150, JEU = 2;

bool symboleBranchement(const std::string& s) { return s == "iec:agcp" || s == "iec:u-agcp"; }

int famille(const ComposantElec& c) {
  const std::string& s = c.symbole;
  if (s == "iec:inter-sect-3p" || s == "iec:inter-sect-4p" || s == "iec:sect-3p" || s == "iec:sect-fusible-3p") return 0;
  if (s.rfind("iec:disj", 0) == 0 || s == "iec:fusible" || s.rfind("iec:diff", 0) == 0 || s == "iec:parafoudre" || symboleBranchement(s) || s == "iec:u-disj" || s == "iec:u-diff")
    return 1;
  if (s == "iec:alim-dc" || s.rfind("iec:transfo", 0) == 0) return 2;
  if (s.rfind("iec:bobine", 0) == 0 || s == "iec:relais-thermique-3p" || s == "iec:telerupteur") return 3;
  if (s == "iec:automate-8e8s" || s == "iec:variateur" || s == "iec:demarreur" || s == "iec:compteur") return 4;
  if (detail::estBornier(c)) return 6;
  return 5;
}

}  // namespace

void equiperArmoire(Armoire& a) {
  a.rails.clear();
  a.goulottes.clear();
  if (a.tableauModulaire) {
    // Coffret : rangées de modules de 18 mm, au pas de 150 mm, centrées
    const double largeurRail = 18.0 * a.modulesParRangee;
    const double x0 = std::max(0.0, (a.largeur - largeurRail) / 2);
    for (int i = 0;; ++i) {
      const double y = 40 + PAS_RANGEE / 2 + PAS_RANGEE * i;
      if (y + 45 > a.hauteur - 20 && i > 0) break;
      a.rails.push_back({mm(y), mm(x0), mm(x0 + largeurRail)});
      if (y + 45 > a.hauteur - 20) break;
    }
    return;
  }
  // Plaque de fond : goulottes verticales sur les côtés, une horizontale en haut et sous chaque rail
  const double m = a.marge, L = a.largeur, H = a.hauteur, g = LARGEUR_GOULOTTE;
  a.goulottes.push_back({{mm(m), mm(m), mm(m + g), mm(H - m)}});
  a.goulottes.push_back({{mm(L - m - g), mm(m), mm(L - m), mm(H - m)}});
  a.goulottes.push_back({{mm(m + g), mm(m), mm(L - m - g), mm(m + g)}});
  for (int i = 0;; ++i) {
    const double axe = m + g + 15 + 45 + PAS_RANGEE * i;
    if (axe + 60 + g > H - m + 1e-9) break;
    a.rails.push_back({mm(axe), mm(m + g + 5), mm(L - m - g - 5)});
    a.goulottes.push_back({{mm(m + g), mm(axe + 60), mm(L - m - g), mm(axe + 60 + g)}});
  }
}

std::vector<std::string> appareilsArmoire(const ProjetElectrique& p, const Armoire& a) {
  std::vector<const ComposantElec*> v;
  for (const auto& c : p.schema.composants) {
    if (a.tableauModulaire && symboleBranchement(c.symbole)) continue;  // dans la gaine technique, à côté du compteur
    const Encombrement e = encombrement(p, c.uuid);
    if (e.horsArmoire || e.largeur <= 0) continue;
    if (c.symbole == "iec:connecteur") continue;
    v.push_back(&c);
  }
  const detail::LieuxBroches lieux = detail::lieuxBroches(p);
  if (a.tableauModulaire) {
    // Coffret : l'ordre du schéma unifilaire, différentiel puis ses disjoncteurs
    std::map<std::string, std::tuple<std::size_t, Nm, Nm>> ordre;
    for (std::size_t f = 0; f < p.schema.feuilles.size(); ++f)
      for (const auto& s : p.schema.feuilles[f].symboles) {
        if (s.composant.empty()) continue;
        const auto t = std::make_tuple(f, s.position.x, s.position.y);
        const auto it = ordre.find(s.composant);
        if (it == ordre.end() || t < it->second) ordre[s.composant] = t;
      }
    std::stable_sort(v.begin(), v.end(), [&](const ComposantElec* x, const ComposantElec* y) {
      const auto ix = ordre.find(x->uuid), iy = ordre.find(y->uuid);
      if (ix == ordre.end() || iy == ordre.end()) return ix != ordre.end() && iy == ordre.end();
      return ix->second < iy->second;
    });
  } else {
    std::stable_sort(v.begin(), v.end(), [](const ComposantElec* x, const ComposantElec* y) {
      const int fx = famille(*x), fy = famille(*y);
      if (fx != fy) return fx < fy;
      return ecad::ordreNaturel(x->reference, y->reference);
    });
    // Chaque relais thermique juste après le contacteur qui l'alimente
    const ecad::Connectivite k = ecad::connectivite(p.schema);
    for (std::size_t i = 0; i < v.size(); ++i) {
      if (v[i]->symbole != "iec:relais-thermique-3p") continue;
      const int r = k.reseau(v[i]->uuid, "1");
      if (r < 0) continue;
      std::string contacteur;
      for (const auto& b : k.reseaux[static_cast<std::size_t>(r)].broches)
        if (const ComposantElec* x = p.schema.composant(b.composant); x && x->symbole == "iec:bobine-km") contacteur = x->uuid;
      if (contacteur.empty()) continue;
      const ComposantElec* rt = v[i];
      v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));
      const auto it = std::find_if(v.begin(), v.end(), [&](const ComposantElec* x) { return x->uuid == contacteur; });
      v.insert(it == v.end() ? v.end() : it + 1, rt);
    }
  }
  (void)lieux;
  std::vector<std::string> r;
  for (const auto* c : v) r.push_back(c->uuid);
  return r;
}

int implanterArmoire(ProjetElectrique& p, std::size_t indice) {
  if (indice >= p.armoires.size()) return 0;
  Armoire& a = p.armoires[indice];
  if (a.rails.empty() || a.tableauModulaire) equiperArmoire(a);
  a.implantations.clear();
  const std::vector<std::string> appareils = appareilsArmoire(p, a);
  std::vector<std::string> plaque, porte, borniers;
  for (const auto& id : appareils) {
    const Encombrement e = encombrement(p, id);
    const ComposantElec* c = p.schema.composant(id);
    if (e.porte && !a.tableauModulaire) porte.push_back(id);
    else if (c && detail::estBornier(*c) && !a.tableauModulaire) borniers.push_back(id);
    else plaque.push_back(id);
  }
  int poses = 0;
  const double jeu = a.tableauModulaire ? 0 : JEU;
  std::size_t rail = 0;
  double x = a.rails.empty() ? 0 : versMm(a.rails.front().x0);
  auto poser = [&](const std::string& id, std::size_t& r, double& cx) {
    if (a.rails.empty()) return false;
    const Encombrement e = encombrement(p, id);
    while (r < a.rails.size() && cx + e.largeur > versMm(a.rails[r].x1) + 1e-6) {
      ++r;
      if (r < a.rails.size()) cx = versMm(a.rails[r].x0);
    }
    if (r >= a.rails.size()) return false;
    Implantation im;
    im.composant = id;
    im.p = {mm(cx), a.rails[r].y - mm(e.hauteur / 2)};
    im.rail = e.rail ? static_cast<int>(r) : -1;
    a.implantations.push_back(im);
    cx += e.largeur + jeu;
    ++poses;
    return true;
  };
  std::vector<std::string> restants;
  for (const auto& id : plaque)
    if (!poser(id, rail, x)) restants.push_back(id);
  // Borniers sur le dernier rail, s'il est libre ; sinon à la suite
  if (!borniers.empty() && !a.rails.empty()) {
    std::size_t dernier = a.rails.size() - 1;
    if (dernier > rail) {
      rail = dernier;
      x = versMm(a.rails[rail].x0);
    }
    for (const auto& id : borniers)
      if (!poser(id, rail, x)) restants.push_back(id);
  }
  // Ce qui ne tient pas : posé sous la plaque, signalé par le bilan
  double xr = a.marge;
  for (const auto& id : restants) {
    const Encombrement e = encombrement(p, id);
    a.implantations.push_back({id, {mm(xr), mm(a.hauteur + 20)}, -1, false});
    xr += e.largeur + 5;
  }
  // Porte : boutons, voyants, ventilation, en grille
  double px = a.marge + 60, py = a.marge + 80;
  for (const auto& id : porte) {
    const Encombrement e = encombrement(p, id);
    if (px + e.largeur > a.largeur - a.marge - 40) {
      px = a.marge + 60;
      py += 70;
    }
    a.implantations.push_back({id, {mm(px), mm(py)}, -1, true});
    px += std::max(45.0, e.largeur + 15);
    ++poses;
  }
  return poses;
}

int implanterManquants(ProjetElectrique& p, std::size_t indice) {
  if (indice >= p.armoires.size()) return 0;
  Armoire& a = p.armoires[indice];
  if (a.rails.empty()) equiperArmoire(a);
  std::set<std::string> implantes;
  for (const auto& im : a.implantations) implantes.insert(im.composant);
  // Intervalles occupés sur chaque rail, et boîtes de la porte
  const auto boites = boitesArmoire(p, indice);
  std::vector<std::vector<std::pair<double, double>>> occupes(a.rails.size());
  std::vector<Boite> porte;
  for (const auto& b : boites) {
    if (b.porte) {
      porte.push_back(b.b);
      continue;
    }
    for (std::size_t r = 0; r < a.rails.size(); ++r)
      if (b.b.y0 <= a.rails[r].y && a.rails[r].y <= b.b.y1) occupes[r].push_back({versMm(b.b.x0), versMm(b.b.x1)});
  }
  int ajoutes = 0;
  const double jeu = a.tableauModulaire ? 0 : JEU;
  for (const auto& id : appareilsArmoire(p, a)) {
    if (implantes.count(id)) continue;
    const Encombrement e = encombrement(p, id);
    if (e.porte && !a.tableauModulaire) {
      for (double y = a.marge + 80; y + e.hauteur < a.hauteur - a.marge; y += 70) {
        bool place = false;
        for (double x = a.marge + 60; x + e.largeur < a.largeur - a.marge - 40; x += 45) {
          const Boite b{mm(x), mm(y), mm(x + e.largeur), mm(y + e.hauteur)};
          const bool libre = std::none_of(porte.begin(), porte.end(), [&](const Boite& c) { return b.x0 < c.x1 && c.x0 < b.x1 && b.y0 < c.y1 && c.y0 < b.y1; });
          if (!libre) continue;
          a.implantations.push_back({id, {b.x0, b.y0}, -1, true});
          porte.push_back(b);
          place = true;
          break;
        }
        if (place) {
          ++ajoutes;
          break;
        }
      }
      continue;
    }
    bool place = false;
    for (std::size_t r = 0; r < a.rails.size() && !place; ++r) {
      auto& liste = occupes[r];
      std::sort(liste.begin(), liste.end());
      double x = versMm(a.rails[r].x0);
      for (const auto& [x0, x1] : liste) {
        if (x + e.largeur <= x0 - jeu + 1e-6) break;
        x = std::max(x, x1 + jeu);
      }
      if (x + e.largeur > versMm(a.rails[r].x1) + 1e-6) continue;
      a.implantations.push_back({id, {mm(x), a.rails[r].y - mm(e.hauteur / 2)}, e.rail ? static_cast<int>(r) : -1, false});
      liste.push_back({x, x + e.largeur});
      place = true;
      ++ajoutes;
    }
  }
  return ajoutes;
}

std::vector<BoiteAppareil> boitesArmoire(const ProjetElectrique& p, std::size_t indice) {
  std::vector<BoiteAppareil> v;
  if (indice >= p.armoires.size()) return v;
  for (const auto& im : p.armoires[indice].implantations) {
    const Encombrement e = encombrement(p, im.composant);
    BoiteAppareil b;
    b.composant = im.composant;
    b.b = {im.p.x, im.p.y, im.p.x + mm(e.largeur), im.p.y + mm(e.hauteur)};
    b.porte = im.porte;
    b.rail = im.rail;
    v.push_back(b);
  }
  return v;
}

BilanArmoire bilanArmoire(const ProjetElectrique& p, std::size_t indice) {
  BilanArmoire r;
  if (indice >= p.armoires.size()) return r;
  const Armoire& a = p.armoires[indice];
  const std::vector<BoiteAppareil> boites = boitesArmoire(p, indice);
  double longueur = 0, occupee = 0;
  for (const auto& x : a.rails) longueur += versMm(x.x1 - x.x0);
  const Boite plaque{0, 0, mm(a.largeur), mm(a.hauteur)};
  for (std::size_t i = 0; i < boites.size(); ++i) {
    const auto& b = boites[i];
    if (b.porte) continue;
    if (b.rail >= 0 || a.tableauModulaire) occupee += versMm(b.b.largeur());
    bool hors = b.b.x0 < plaque.x0 || b.b.y0 < plaque.y0 || b.b.x1 > plaque.x1 || b.b.y1 > plaque.y1;
    for (std::size_t j = 0; j < boites.size() && !hors; ++j) {
      if (j == i || boites[j].porte) continue;
      const Boite& c = boites[j].b;
      const bool chevauche = b.b.x0 < c.x1 - mm(0.1) && c.x0 < b.b.x1 - mm(0.1) && b.b.y0 < c.y1 - mm(0.1) && c.y0 < b.b.y1 - mm(0.1);
      hors = chevauche;
    }
    if (hors) ++r.horsPlaque;
  }
  r.tauxRemplissage = longueur > 0 ? std::min(1.0, occupee / longueur) : 0;
  r.reserve = 1.0 - r.tauxRemplissage;
  const std::vector<std::string> attendus = appareilsArmoire(p, a);
  std::set<std::string> implantes;
  for (const auto& im : a.implantations) implantes.insert(im.composant);
  int manquants = 0;
  for (const auto& id : attendus) manquants += implantes.count(id) ? 0 : 1;
  if (manquants > 0) r.remarques.push_back(std::to_string(manquants) + " appareil(s) de l'armoire ne sont pas encore implantés : lance « Implanter automatiquement ».");
  if (r.horsPlaque > 0) r.remarques.push_back(std::to_string(r.horsPlaque) + " appareil(s) débordent de la plaque ou se chevauchent : agrandis l'armoire ou ajoute un rail.");
  if (longueur > 0 && r.reserve < 0.2 - 1e-9)
    r.remarques.push_back("Réserve de " + detail::nombreFr(100 * r.reserve, 0) + " % : 20 % au moins sont conseillés pour les évolutions" +
                          std::string(a.tableauModulaire ? " (exigés par la NF C 15-100 dans un tableau de logement)." : "."));
  int machine = 0;
  for (const auto& c : p.schema.composants)
    if (encombrement(p, c.uuid).horsArmoire && !c.symbole.empty() && c.symbole != "iec:connecteur") ++machine;
  if (machine > 0) r.remarques.push_back(std::to_string(machine) + " appareil(s) hors armoire (moteurs, capteurs, récepteurs) : raccordés par les borniers et les câbles.");
  return r;
}

std::string dxfPlaque(const ProjetElectrique& p, std::size_t indice) {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  if (indice >= p.armoires.size()) return {};
  const Armoire& a = p.armoires[indice];
  const double H = a.hauteur;
  auto paire = [&](int code, const std::string& v) { o << code << "\n" << v << "\n"; };
  auto nombre = [](double v) {
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s.setf(std::ios::fixed);
    s.precision(3);
    s << v;
    return s.str();
  };
  auto ligne = [&](const char* calque, double x0, double y0, double x1, double y1) {
    paire(0, "LINE");
    paire(8, calque);
    paire(10, nombre(x0));
    paire(20, nombre(H - y0));
    paire(11, nombre(x1));
    paire(21, nombre(H - y1));
  };
  auto rectangle = [&](const char* calque, double x0, double y0, double x1, double y1) {
    ligne(calque, x0, y0, x1, y0);
    ligne(calque, x1, y0, x1, y1);
    ligne(calque, x1, y1, x0, y1);
    ligne(calque, x0, y1, x0, y0);
  };
  auto trou = [&](double x, double y, double d) {
    paire(0, "CIRCLE");
    paire(8, "PERCAGES");
    paire(10, nombre(x));
    paire(20, nombre(H - y));
    paire(40, nombre(d / 2));
  };
  auto texte = [&](double x, double y, const std::string& t, double h) {
    paire(0, "TEXT");
    paire(8, "REPERES");
    paire(10, nombre(x));
    paire(20, nombre(H - y));
    paire(40, nombre(h));
    paire(1, t);
  };
  // En-tête minimal (R12) et calques
  paire(0, "SECTION");
  paire(2, "HEADER");
  paire(9, "$ACADVER");
  paire(1, "AC1009");
  paire(9, "$INSUNITS");
  paire(70, "4");
  paire(0, "ENDSEC");
  paire(0, "SECTION");
  paire(2, "TABLES");
  paire(0, "TABLE");
  paire(2, "LAYER");
  paire(70, "6");
  for (const auto& [nom, couleur] : std::vector<std::pair<std::string, int>>{{"PLAQUE", 7}, {"RAILS", 5}, {"GOULOTTES", 3}, {"APPAREILS", 1}, {"PERCAGES", 6}, {"REPERES", 7}}) {
    paire(0, "LAYER");
    paire(2, nom);
    paire(70, "0");
    paire(62, std::to_string(couleur));
    paire(6, "CONTINUOUS");
  }
  paire(0, "ENDTAB");
  paire(0, "ENDSEC");
  paire(0, "SECTION");
  paire(2, "ENTITIES");
  rectangle("PLAQUE", 0, 0, a.largeur, a.hauteur);
  for (const auto& r : a.rails) {
    const double y = versMm(r.y), x0 = versMm(r.x0), x1 = versMm(r.x1);
    rectangle("RAILS", x0, y - 17.5, x1, y + 17.5);
    // Fixation du rail : vis M5 tous les 100 mm, à 25 mm des extrémités
    for (double x = x0 + 25; x <= x1 - 25 + 1e-6; x += 100) trou(x, y, 5.5);
    if (x1 - x0 > 50 && std::fmod(x1 - x0 - 50, 100) > 1e-6) trou(x1 - 25, y, 5.5);
  }
  for (const auto& g : a.goulottes) {
    const double x0 = versMm(g.b.x0), y0 = versMm(g.b.y0), x1 = versMm(g.b.x1), y1 = versMm(g.b.y1);
    rectangle("GOULOTTES", x0, y0, x1, y1);
    // Fixation de la goulotte : sur son axe, tous les 200 mm
    const bool horizontale = x1 - x0 >= y1 - y0;
    const double l = horizontale ? x1 - x0 : y1 - y0;
    for (double s = 50; s <= l - 50 + 1e-6; s += 200) trou(horizontale ? x0 + s : (x0 + x1) / 2, horizontale ? (y0 + y1) / 2 : y0 + s, 4.5);
  }
  for (const auto& b : boitesArmoire(p, indice)) {
    if (b.porte) continue;
    const double x0 = versMm(b.b.x0), y0 = versMm(b.b.y0), x1 = versMm(b.b.x1), y1 = versMm(b.b.y1);
    rectangle("APPAREILS", x0, y0, x1, y1);
    if (b.rail < 0) {
      // Appareil fixé sur la plaque : deux vis M4 au milieu des bords haut et bas
      trou((x0 + x1) / 2, y0 + 5, 4.5);
      trou((x0 + x1) / 2, y1 - 5, 4.5);
    }
    if (const ComposantElec* c = p.schema.composant(b.composant)) texte(x0 + 1, y0 + 5, repereAffiche(p, *c), 3.5);
  }
  paire(0, "ENDSEC");
  paire(0, "EOF");
  return o.str();
}

double longueurBranche(const Faisceau& f, const BrancheFaisceau& b) {
  if (b.longueur > 0) return b.longueur;
  const NoeudFaisceau* a = nullptr;
  const NoeudFaisceau* c = nullptr;
  for (const auto& n : f.noeuds) {
    if (n.uuid == b.a) a = &n;
    if (n.uuid == b.b) c = &n;
  }
  if (!a || !c) return 0;
  return std::hypot(versMm(a->p.x - c->p.x), versMm(a->p.y - c->p.y));
}

std::vector<FilFaisceau> filsFaisceau(const ProjetElectrique& p, std::size_t indice) {
  std::vector<FilFaisceau> v;
  if (indice >= p.faisceaux.size()) return v;
  const Faisceau& f = p.faisceaux[indice];
  std::map<std::string, std::size_t> noeudDe;  // appareil (connecteur) → nœud
  std::map<std::string, std::size_t> parUuid;
  for (std::size_t i = 0; i < f.noeuds.size(); ++i) {
    parUuid[f.noeuds[i].uuid] = i;
    if (!f.noeuds[i].composant.empty()) noeudDe[f.noeuds[i].composant] = i;
  }
  // Graphe des branches : plus court chemin entre deux nœuds (Dijkstra)
  std::vector<std::vector<std::pair<std::size_t, std::size_t>>> voisins(f.noeuds.size());  // (voisin, branche)
  for (std::size_t j = 0; j < f.branches.size(); ++j) {
    const auto ia = parUuid.find(f.branches[j].a), ib = parUuid.find(f.branches[j].b);
    if (ia == parUuid.end() || ib == parUuid.end()) continue;
    voisins[ia->second].push_back({ib->second, j});
    voisins[ib->second].push_back({ia->second, j});
  }
  auto chemin = [&](std::size_t de, std::size_t vers, std::vector<std::size_t>& branches) {
    std::vector<double> d(f.noeuds.size(), std::numeric_limits<double>::infinity());
    std::vector<std::pair<std::size_t, std::size_t>> avant(f.noeuds.size(), {static_cast<std::size_t>(-1), 0});
    using E = std::pair<double, std::size_t>;
    std::priority_queue<E, std::vector<E>, std::greater<E>> file;
    d[de] = 0;
    file.push({0, de});
    while (!file.empty()) {
      const auto [dist, n] = file.top();
      file.pop();
      if (dist > d[n]) continue;
      for (const auto& [m, j] : voisins[n]) {
        const double nd = dist + longueurBranche(f, f.branches[j]);
        if (nd < d[m]) {
          d[m] = nd;
          avant[m] = {n, j};
          file.push({nd, m});
        }
      }
    }
    if (!std::isfinite(d[vers])) return -1.0;
    for (std::size_t n = vers; n != de; n = avant[n].first) branches.insert(branches.begin(), avant[n].second);
    return d[vers];
  };
  const ecad::Connectivite k = ecad::connectivite(p.schema);
  for (const auto& r : k.reseaux) {
    std::vector<std::pair<std::size_t, std::string>> bouts;  // nœud, « -X1:3 »
    for (const auto& b : r.broches)
      if (const auto it = noeudDe.find(b.composant); it != noeudDe.end()) bouts.push_back({it->second, detail::repereBroche(p, b.composant, b.broche)});
    if (bouts.size() < 2) continue;
    std::sort(bouts.begin(), bouts.end());
    const ProprietesConducteur pr = detail::proprietes(p, k, r.nom);
    for (std::size_t i = 1; i < bouts.size(); ++i) {
      FilFaisceau w;
      w.fil = detail::nomAutomatique(r.nom) ? std::string() : r.nom;
      w.de = bouts[i - 1].second;
      w.vers = bouts[i].second;
      w.couleur = pr.couleur;
      w.section = pr.section;
      std::vector<std::size_t> branches;
      const double l = bouts[i - 1].first == bouts[i].first ? 0 : chemin(bouts[i - 1].first, bouts[i].first, branches);
      if (l < 0) {
        w.longueur = 0;  // nœuds non reliés par le faisceau
      } else {
        w.longueur = l + 2 * f.surlongueur;
        for (std::size_t j : branches) {
          const auto& b = f.branches[j];
          std::string na = b.a, nb = b.b;
          if (const auto ia = parUuid.find(b.a); ia != parUuid.end() && !f.noeuds[ia->second].nom.empty()) na = f.noeuds[ia->second].nom;
          if (const auto ib = parUuid.find(b.b); ib != parUuid.end() && !f.noeuds[ib->second].nom.empty()) nb = f.noeuds[ib->second].nom;
          w.branches.push_back(na + "–" + nb);
        }
      }
      v.push_back(w);
    }
  }
  return v;
}

}  // namespace prom::elec

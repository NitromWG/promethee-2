// Prométhée : sérigraphie préparée pour la fabrication (8.13). Voir serigraphie_preparee.hpp.
// Licence GPL-3.0-only.
#include "promethee/serigraphie_preparee.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <set>

#include "promethee/circuit_edition.hpp"
#include "promethee/empreintes_edition.hpp"
#include "promethee/police.hpp"

namespace prom::ecad {

namespace {
// Ouvertures de vernis d'une face, depuis le modèle de fabrication
Polygones ouvertures(const ModeleFabrication& m, Couche vernis) {
  Polygones r;
  for (const auto& cf : m.couches) {
    if (cf.couche != vernis) continue;
    for (const auto& p : cf.primitives) {
      switch (p.type) {
        case PrimitiveFab::Type::Disque: r.push_back(cercle(p.a, std::max<Nm>(p.l / 2, 1))); break;
        case PrimitiveFab::Type::Rect: r.push_back(rectangle(p.a, p.l, p.h, p.rotation)); break;
        case PrimitiveFab::Type::Oblong: r.push_back(rectangleArrondi(p.a, p.l, p.h, std::min(p.l, p.h) / 2, p.rotation)); break;
        case PrimitiveFab::Type::RectArrondi: r.push_back(rectangleArrondi(p.a, p.l, p.h, p.r, p.rotation)); break;
        case PrimitiveFab::Type::Region: r.push_back(p.region); break;
        case PrimitiveFab::Type::Trait:
        case PrimitiveFab::Type::Arc: r.push_back(oblong(p.a, p.b, std::max<Nm>(p.l, 1))); break;
      }
    }
  }
  return unir(r);
}
// Morceaux du segment [a, b] hors de « interdit » (et dans « permis » s'il n'est pas vide)
std::vector<std::pair<Pt, Pt>> horsDe(Pt a, Pt b, const Polygones& interdit, const Polygones& permis) {
  std::vector<double> t{0.0, 1.0};
  auto coupes = [&](const Polygones& ps) {
    for (const auto& p : ps)
      for (std::size_t i = 0; i < p.size(); ++i) {
        const Pt c = p[i], d = p[(i + 1) % p.size()];
        const double rx = static_cast<double>(b.x - a.x), ry = static_cast<double>(b.y - a.y), sx = static_cast<double>(d.x - c.x), sy = static_cast<double>(d.y - c.y);
        const double den = rx * sy - ry * sx;
        if (std::abs(den) < 1e-9) continue;
        const double qx = static_cast<double>(c.x - a.x), qy = static_cast<double>(c.y - a.y);
        const double u = (qx * sy - qy * sx) / den, v = (qx * ry - qy * rx) / den;
        if (u > 0 && u < 1 && v >= 0 && v <= 1) t.push_back(u);
      }
  };
  coupes(interdit);
  coupes(permis);
  std::sort(t.begin(), t.end());
  std::vector<std::pair<Pt, Pt>> r;
  auto point = [&](double u) { return Pt{a.x + static_cast<Nm>(std::llround(static_cast<double>(b.x - a.x) * u)), a.y + static_cast<Nm>(std::llround(static_cast<double>(b.y - a.y) * u))}; };
  for (std::size_t i = 1; i < t.size(); ++i) {
    if (t[i] - t[i - 1] < 1e-9) continue;
    const Pt m = point((t[i] + t[i - 1]) / 2);
    const bool garde = !dedans(interdit, m) && (permis.empty() || dedans(permis, m));
    if (!garde) continue;
    // Morceaux contigus réunis
    if (!r.empty() && r.back().second == point(t[i - 1])) r.back().second = point(t[i]);
    else r.push_back({point(t[i - 1]), point(t[i])});
  }
  return r;
}
std::vector<std::pair<Pt, Pt>> segmentsTrait(const TraitEmpreinte& t) {
  std::vector<std::pair<Pt, Pt>> s;
  if (t.rayon > 0) {
    for (int k = 0; k < 32; ++k) {
      const double a0 = 2 * std::numbers::pi * k / 32, a1 = 2 * std::numbers::pi * (k + 1) / 32, r = static_cast<double>(t.rayon);
      s.push_back({{t.a.x + static_cast<Nm>(std::lround(r * std::cos(a0))), t.a.y + static_cast<Nm>(std::lround(r * std::sin(a0)))},
                   {t.a.x + static_cast<Nm>(std::lround(r * std::cos(a1))), t.a.y + static_cast<Nm>(std::lround(r * std::sin(a1)))}});
    }
  } else
    s.push_back({t.a, t.b});
  return s;
}
bool estSerigraphie(Couche k) { return k == Couche::SerigraphieDessus || k == Couche::SerigraphieDessous; }

struct Plan {
  std::vector<ChangementSerigraphie> changements;
  std::map<std::string, std::vector<TraitEmpreinte>> traitsRognes;  // composant → traits de sa copie locale
  std::map<std::string, Pt> reperes;  // composant → nouvelle place du repère (repère de l'empreinte)
  std::map<std::string, Pt> textes;   // texte de la carte → nouvelle place
  std::set<std::string> dessinsHorsCarte, textesHorsCarte;
};

// Obstacles d'une face (pastilles et ouvertures de vernis, une à une) pour la recherche d'une place de repère
struct Obstacles {
  std::vector<Polygone> formes;
  std::vector<Boite> boites;
  void ajouter(const Polygone& p) {
    if (p.size() < 3) return;
    formes.push_back(p);
    boites.push_back(boite(p));
  }
  void ajouter(const Polygones& ps) {
    for (const auto& p : ps) ajouter(p);
  }
  // Le trait [a, b] d'épaisseur 2·demi passe-t-il à moins de « marge » d'un obstacle ?
  bool touche(Pt a, Pt b, Nm demi, Nm marge) const {
    Boite bs;
    bs.etendre(a);
    bs.etendre(b);
    const Boite g = bs.gonflee(demi + marge + 1);
    for (std::size_t i = 0; i < formes.size(); ++i)
      if (g.touche(boites[i]) && distancePolygoneSegment(formes[i], a, b) < static_cast<double>(demi + marge)) return true;
    return false;
  }
};

Plan planifier(const ConceptionElec& c, const OptionsSerigraphie034& o) {
  Plan plan;
  const Connectivite k = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
  // Ouvertures de vernis et cuivre des pastilles (une pastille couverte de vernis reste une pastille), élargis de la marge
  Polygones dessus0 = ouvertures(m, Couche::VernisDessus), dessous0 = ouvertures(m, Couche::VernisDessous);
  for (const auto& p : pastilles(c, k)) {
    if (p.couches & bit(Couche::CuivreDessus)) dessus0.push_back(p.forme);
    if (p.couches & bit(Couche::CuivreDessous)) dessous0.push_back(p.forme);
  }
  const Polygones interditDessus = gonfler(unir(dessus0), o.marge), interditDessous = gonfler(unir(dessous0), o.marge);
  const Polygone carte = c.circuit.contour.polygone();
  const Polygones dansCarte = o.supprimerHorsCarte ? soustraire({carte}, c.circuit.contour.decoupes) : Polygones{};
  std::map<std::pair<int, Nm>, Polygones> cache;  // (face, demi-épaisseur) → zone interdite au milieu du trait
  auto interdit = [&](bool dessous, Nm demi) -> const Polygones& {
    auto it = cache.find({dessous, demi});
    if (it == cache.end()) it = cache.emplace(std::pair{static_cast<int>(dessous), demi}, gonfler(dessous ? interditDessous : interditDessus, demi)).first;
    return it->second;
  };
  if (o.rogner)
    for (const auto& e : c.circuit.empreintes) {
      const DefEmpreinte* d = c.empreinte(e.empreinte);
      const ComposantElec* x = c.composant(e.composant);
      if (!d || !x) continue;
      std::vector<TraitEmpreinte> nouveaux;
      int rognes = 0, supprimes = 0;
      for (const auto& t : d->traits) {
        const Couche surCarte = coucheSurCarte(e, t.couche);
        if (!estSerigraphie(surCarte)) {
          nouveaux.push_back(t);
          continue;
        }
        const Polygones& zone = interdit(surCarte == Couche::SerigraphieDessous, t.epaisseur / 2);
        bool touche = false;
        std::vector<TraitEmpreinte> morceaux;
        for (const auto& [la, lb] : segmentsTrait(t)) {
          const Pt a = versCarte(e, la), b = versCarte(e, lb);
          const auto garde = horsDe(a, b, zone, dansCarte);
          double reste = 0;
          for (const auto& [p, q] : garde) reste += distance(p, q);
          if (distance(a, b) - reste > 1'000.0) touche = true;
          for (const auto& [p, q] : garde) {
            if (distance(p, q) < static_cast<double>(o.longueurMin)) continue;
            TraitEmpreinte n = t;
            n.rayon = 0;
            n.a = versEmpreinte(e, p);
            n.b = versEmpreinte(e, q);
            morceaux.push_back(n);
          }
        }
        if (!touche) {
          nouveaux.push_back(t);
          continue;
        }
        if (morceaux.empty()) ++supprimes;
        else ++rognes;
        nouveaux.insert(nouveaux.end(), morceaux.begin(), morceaux.end());
      }
      if (rognes || supprimes) {
        plan.traitsRognes[e.composant] = nouveaux;
        ChangementSerigraphie ch;
        ch.genre = rognes ? ChangementSerigraphie::Genre::Rogne : ChangementSerigraphie::Genre::Supprime;
        ch.composant = e.composant;
        ch.description = x->reference + " : " + (rognes ? std::to_string(rognes) + " trait(s) rogné(s)" : std::string()) + (rognes && supprimes ? ", " : "") +
                         (supprimes ? std::to_string(supprimes) + " supprimé(s)" : std::string()) + " autour des pastilles";
        plan.changements.push_back(ch);
      }
    }
  // Repères posés sur une pastille : une place libre cherchée en spirale autour du corps, d'abord hors des autres
  // encombrements et des repères déjà posés, sinon simplement hors des pastilles et dans la carte
  if (o.ecarterReperes) {
    Obstacles obs[2];
    for (int f = 0; f < 2; ++f) {
      for (const auto& p : pastilles(c, k))
        if (p.couches & bit(f ? Couche::CuivreDessous : Couche::CuivreDessus)) obs[f].ajouter(p.forme);
    }
    for (const auto& cf : m.couches)
      if (cf.couche == Couche::VernisDessus || cf.couche == Couche::VernisDessous) {
        ModeleFabrication une;
        une.couches.push_back(cf);
        obs[cf.couche == Couche::VernisDessous].ajouter(ouvertures(une, cf.couche));
      }
    std::map<std::string, std::vector<TraitSerigraphie>> parComposant;
    for (const auto& t : textesSerigraphie(c)) {
      const ComposantElec* x = t.composant.empty() ? nullptr : c.composant(t.composant);
      if (x && t.texte == x->reference) parComposant[t.composant].push_back(t);
    }
    // Boîtes des repères (gardés ou déplacés) et des encombrements, par face
    std::vector<Boite> boitesReperes[2], encombrements[2];
    std::map<std::string, Boite> repereDe;
    for (const auto& [comp, traits] : parComposant) {
      Boite b;
      for (const auto& t : traits) {
        b.etendre(t.a);
        b.etendre(t.b);
      }
      repereDe[comp] = b;
    }
    for (const auto& e : c.circuit.empreintes)
      if (const DefEmpreinte* d = c.empreinte(e.empreinte)) encombrements[e.dessous].push_back(boite(encombrementSurCarte(e, *d)));
    const Nm h = std::max<Nm>(c.circuit.serigraphie.hauteur, 500'000);
    auto dansLaCarte = [&](Pt p) { return dansCarte.empty() || dedans(dansCarte, p); };
    std::vector<std::pair<std::string, int>> aDeplacer;  // composant, face
    for (const auto& [comp, traits] : parComposant) {
      bool mal = false;
      for (const auto& t : traits) mal |= obs[t.couche == Couche::SerigraphieDessous].touche(t.a, t.b, t.epaisseur / 2, 0);
      if (mal) aDeplacer.push_back({comp, traits.front().couche == Couche::SerigraphieDessous});
      else boitesReperes[traits.front().couche == Couche::SerigraphieDessous].push_back(repereDe[comp]);
    }
    for (const auto& [comp, f] : aDeplacer) {
      const auto& traits = parComposant[comp];
      const EmpreintePlacee* e = nullptr;
      for (const auto& x : c.circuit.empreintes)
        if (x.composant == comp) e = &x;
      const DefEmpreinte* d = e ? c.empreinte(e->empreinte) : nullptr;
      const ComposantElec* x = c.composant(comp);
      if (!e || !d || !x) continue;
      const Pt actuel = positionRepere(*e, *d, c.circuit.serigraphie.hauteur);
      const Boite corps = boite(encombrementSurCarte(*e, *d)), texte = repereDe[comp];
      const Nm r0 = std::max(corps.largeur(), corps.hauteur()) / 2 + std::max(texte.largeur(), texte.hauteur()) / 2;
      std::optional<Pt> trouve;
      for (int exigence = 0; exigence < 2 && !trouve; ++exigence)
        for (int tour = 0; tour < 40 && !trouve; ++tour)
          for (int a = 0; a < 32 && !trouve; ++a) {
            const double ang = -std::numbers::pi / 2 + 2 * std::numbers::pi * a / 32;
            const double r = static_cast<double>(tour == 0 ? r0 / 2 : r0 + (tour - 1) * h / 3);
            const Pt centre{corps.centre().x + static_cast<Nm>(std::llround(std::cos(ang) * r)), corps.centre().y + static_cast<Nm>(std::llround(std::sin(ang) * r))};
            const Pt dp = centre - actuel;
            bool libre = true;
            for (const auto& t : traits) {
              const Pt ta = t.a + dp, tb = t.b + dp;
              if (!dansLaCarte(ta) || !dansLaCarte(tb) || obs[f].touche(ta, tb, t.epaisseur / 2, o.marge)) {
                libre = false;
                break;
              }
            }
            Boite bt = texte;
            bt.x0 += dp.x, bt.x1 += dp.x, bt.y0 += dp.y, bt.y1 += dp.y;
            if (libre && exigence == 0) {
              for (const auto& b : boitesReperes[f]) libre &= !b.gonflee(100'000).touche(bt);
              for (std::size_t i = 0; i < encombrements[f].size() && libre; ++i)
                if (!(encombrements[f][i] == corps)) libre &= !encombrements[f][i].touche(bt);
            }
            if (libre) {
              trouve = centre;
              boitesReperes[f].push_back(bt);
            }
          }
      if (trouve) {
        plan.reperes[comp] = versEmpreinte(*e, *trouve);
        plan.changements.push_back({ChangementSerigraphie::Genre::Deplace, comp, x->reference + " : repère écarté des pastilles"});
      } else
        plan.changements.push_back({ChangementSerigraphie::Genre::Deplace, comp, x->reference + " : repère sur une pastille, aucune place libre autour du corps"});
    }
    // Textes de la carte posés sur une pastille : la place libre la plus proche, en spirale autour de la leur
    for (const auto& t : c.circuit.textes) {
      if (!estSerigraphie(t.couche) || t.texte.empty() || (o.supprimerHorsCarte && !dedans(carte, t.p))) continue;
      const int f = t.couche == Couche::SerigraphieDessous;
      const Nm ht = std::max<Nm>(t.hauteur, 300'000), demi = epaisseurTexte(ht) / 2;
      const auto traits = traitsTexte(t.texte, t.p, ht, t.rotation, f == 1);
      bool mal = false;
      Boite bt0;
      for (const auto& s2 : traits) {
        mal |= obs[f].touche(s2.a, s2.b, demi, 0);
        bt0.etendre(s2.a);
        bt0.etendre(s2.b);
      }
      if (!mal) continue;
      std::optional<Pt> trouve;
      for (int exigence = 0; exigence < 2 && !trouve; ++exigence)
        for (int tour = 1; tour <= 60 && !trouve; ++tour)
          for (int a = 0; a < 32 && !trouve; ++a) {
            const double ang = -std::numbers::pi / 2 + 2 * std::numbers::pi * a / 32, r = static_cast<double>(tour * ht / 4);
            const Pt dp{static_cast<Nm>(std::llround(std::cos(ang) * r)), static_cast<Nm>(std::llround(std::sin(ang) * r))};
            bool libre = true;
            for (const auto& s2 : traits)
              if (!dansLaCarte(s2.a + dp) || !dansLaCarte(s2.b + dp) || obs[f].touche(s2.a + dp, s2.b + dp, demi, o.marge)) {
                libre = false;
                break;
              }
            Boite bt = bt0;
            bt.x0 += dp.x, bt.x1 += dp.x, bt.y0 += dp.y, bt.y1 += dp.y;
            if (libre && exigence == 0) {
              for (const auto& b : boitesReperes[f]) libre &= !b.gonflee(100'000).touche(bt);
              for (const auto& b : encombrements[f]) libre &= !b.touche(bt);
            }
            if (libre) {
              trouve = t.p + dp;
              boitesReperes[f].push_back(bt);
            }
          }
      const std::string court = t.texte.substr(0, t.texte.find('\n'));
      if (trouve) {
        plan.textes[t.uuid] = *trouve;
        plan.changements.push_back({ChangementSerigraphie::Genre::Deplace, {}, "texte « " + court + " » écarté des pastilles"});
      } else
        plan.changements.push_back({ChangementSerigraphie::Genre::Deplace, {}, "texte « " + court + " » sur une pastille, aucune place libre"});
    }
  }
  // Dessins et textes de la carte hors du contour
  if (o.supprimerHorsCarte) {
    for (const auto& d : c.circuit.dessins)
      if (estSerigraphie(d.couche)) {
        bool dehors = false;
        for (const Pt p : d.points) dehors |= !dedans(carte, p);
        if (dehors) {
          plan.dessinsHorsCarte.insert(d.uuid);
          plan.changements.push_back({ChangementSerigraphie::Genre::Supprime, {}, "dessin de sérigraphie hors de la carte supprimé"});
        }
      }
    for (const auto& t : c.circuit.textes)
      if (estSerigraphie(t.couche) && !dedans(carte, t.p)) {
        plan.textesHorsCarte.insert(t.uuid);
        plan.changements.push_back({ChangementSerigraphie::Genre::Supprime, {}, "texte « " + t.texte + " » hors de la carte supprimé"});
      }
  }
  return plan;
}
}  // namespace

std::vector<ChangementSerigraphie> preparerSerigraphie(const ConceptionElec& c, const OptionsSerigraphie034& o) { return planifier(c, o).changements; }

std::vector<ChangementSerigraphie> appliquerPreparationSerigraphie(ConceptionElec& c, const OptionsSerigraphie034& o) {
  exigerModifiable(c);
  const Plan plan = planifier(c, o);
  for (const auto& [composant, traits] : plan.traitsRognes) {
    const std::string copie = copierEmpreinteLocale(c, composant);
    if (const auto it = c.empreintes.find(copie); it != c.empreintes.end()) it->second.traits = traits;
  }
  for (auto& e : c.circuit.empreintes)
    if (const auto it = plan.reperes.find(e.composant); it != plan.reperes.end()) e.repere = it->second;
  for (auto& t : c.circuit.textes)
    if (const auto it = plan.textes.find(t.uuid); it != plan.textes.end()) t.p = it->second;
  std::erase_if(c.circuit.dessins, [&](const DessinCircuit& d) { return plan.dessinsHorsCarte.count(d.uuid) > 0; });
  std::erase_if(c.circuit.textes, [&](const TexteCircuit& t) { return plan.textesHorsCarte.count(t.uuid) > 0; });
  return plan.changements;
}

std::vector<std::string> serigraphieSurPastilles(const ConceptionElec& c, Nm marge) {
  std::vector<std::string> r;
  const Connectivite k = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
  Polygones dessus0 = ouvertures(m, Couche::VernisDessus), dessous0 = ouvertures(m, Couche::VernisDessous);
  for (const auto& p : pastilles(c, k)) {
    if (p.couches & bit(Couche::CuivreDessus)) dessus0.push_back(p.forme);
    if (p.couches & bit(Couche::CuivreDessous)) dessous0.push_back(p.forme);
  }
  const Polygones dessus = gonfler(unir(dessus0), marge), dessous = gonfler(unir(dessous0), marge);
  for (const auto& e : c.circuit.empreintes) {
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    const ComposantElec* x = c.composant(e.composant);
    if (!d || !x) continue;
    for (const auto& t : d->traits) {
      const Couche k2 = coucheSurCarte(e, t.couche);
      if (!estSerigraphie(k2)) continue;
      const Polygones zone = gonfler(k2 == Couche::SerigraphieDessous ? dessous : dessus, t.epaisseur / 2 - 1'000);
      for (const auto& [la, lb] : segmentsTrait(t)) {
        const Pt a = versCarte(e, la), b = versCarte(e, lb);
        if (horsDe(a, b, zone, {}).size() != 1 || dedans(zone, a)) {
          r.push_back(x->reference);
          break;
        }
      }
    }
  }
  std::sort(r.begin(), r.end(), ordreNaturel);
  r.erase(std::unique(r.begin(), r.end()), r.end());
  return r;
}

// ---------- Numéro de série ----------
FormatNumeroSerie formatNumeroSerie(const ConceptionElec& c) {
  FormatNumeroSerie f;
  if (const auto it = c.extensions.find("numeroSerie"); it != c.extensions.end()) {
    f.format = it->second.value("format", f.format);
    f.depart = it->second.value("depart", f.depart);
  }
  return f;
}

void reglerNumeroSerie(ConceptionElec& c, const FormatNumeroSerie& f) { c.extensions["numeroSerie"] = {{"format", f.format}, {"depart", f.depart}}; }

std::string numeroSerie(const FormatNumeroSerie& f, int copie) {
  const int n = f.depart + copie;
  std::string r;
  for (std::size_t i = 0; i < f.format.size(); ++i) {
    if (f.format.compare(i, 2, "{n") == 0) {
      const std::size_t fin = f.format.find('}', i);
      if (fin != std::string::npos) {
        int largeur = 0;
        if (const auto d = f.format.find(':', i); d != std::string::npos && d < fin) largeur = std::atoi(f.format.substr(d + 1, fin - d - 1).c_str());
        char t[32];
        std::snprintf(t, sizeof t, "%0*d", largeur, n);
        r += t;
        i = fin;
        continue;
      }
    }
    r += f.format[i];
  }
  return r;
}

ConceptionElec conceptionNumerotee(const ConceptionElec& c, int copie) {
  ConceptionElec r = c;
  const std::string n = numeroSerie(formatNumeroSerie(c), copie);
  for (auto& t : r.circuit.textes)
    for (std::size_t i = t.texte.find("${SERIE}"); i != std::string::npos; i = t.texte.find("${SERIE}", i + n.size())) t.texte.replace(i, 8, n);
  return r;
}

}  // namespace prom::ecad

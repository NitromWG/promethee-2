// Prométhée : contrôles de fabrication et d'assemblage décrits en données (8.17). Voir controles_fabrication.hpp.
// Licence GPL-3.0-only.
#include "promethee/controles_fabrication.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

namespace prom::ecad {

namespace {
using J = nlohmann::ordered_json;
double reglage(const ControleFabrication& k, const char* cle, double defaut) {
  const auto it = k.reglages.find(cle);
  return it == k.reglages.end() ? defaut : it->second;
}
std::string mmTexte(double v, int d = 2) {
  char t[32];
  std::snprintf(t, sizeof t, "%.*f", d, v);
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
const char* nomGravite(Gravite g) { return g == Gravite::Erreur ? "erreur" : "avertissement"; }
Gravite graviteDepuis(const std::string& s) { return s == "erreur" ? Gravite::Erreur : Gravite::Avertissement; }

// Forme d'une primitive de fabrication, dans son repère (celui de la carte)
Polygone formePrimitive(const PrimitiveFab& p) {
  switch (p.type) {
    case PrimitiveFab::Type::Disque: return cercle(p.a, std::max<Nm>(p.l / 2, 1));
    case PrimitiveFab::Type::Rect: return rectangle(p.a, p.l, p.h, p.rotation);
    case PrimitiveFab::Type::Oblong: return rectangleArrondi(p.a, p.l, p.h, std::min(p.l, p.h) / 2, p.rotation);
    case PrimitiveFab::Type::RectArrondi: return rectangleArrondi(p.a, p.l, p.h, p.r, p.rotation);
    case PrimitiveFab::Type::Region: return p.region;
    case PrimitiveFab::Type::Trait: return oblong(p.a, p.b, std::max<Nm>(p.l, 1));
    case PrimitiveFab::Type::Arc: return oblong(p.a, p.b, std::max<Nm>(p.l, 1));
  }
  return {};
}
// Arc découpé en traits (a, passant par m, jusqu'à b)
Polygones formeArc(const PrimitiveFab& p) {
  const double ax = static_cast<double>(p.a.x), ay = static_cast<double>(p.a.y), bx = static_cast<double>(p.b.x), by = static_cast<double>(p.b.y), mx = static_cast<double>(p.m.x),
               my = static_cast<double>(p.m.y);
  const double d = 2 * (ax * (my - by) + mx * (by - ay) + bx * (ay - my));
  if (std::abs(d) < 1e-6) return {oblong(p.a, p.b, p.l)};
  const double ux = ((ax * ax + ay * ay) * (my - by) + (mx * mx + my * my) * (by - ay) + (bx * bx + by * by) * (ay - my)) / d;
  const double uy = ((ax * ax + ay * ay) * (bx - mx) + (mx * mx + my * my) * (ax - bx) + (bx * bx + by * by) * (mx - ax)) / d;
  const double r = std::hypot(ax - ux, ay - uy);
  double a0 = std::atan2(ay - uy, ax - ux), am = std::atan2(my - uy, mx - ux), a1 = std::atan2(by - uy, bx - ux);
  auto norm = [](double x) {
    while (x < 0) x += 2 * std::numbers::pi;
    while (x >= 2 * std::numbers::pi) x -= 2 * std::numbers::pi;
    return x;
  };
  double s = norm(a1 - a0);
  if (norm(am - a0) > s) s -= 2 * std::numbers::pi;
  Polygones r2;
  Pt prec = p.a;
  for (int i = 1; i <= 24; ++i) {
    const double t = a0 + s * i / 24;
    const Pt q{static_cast<Nm>(std::llround(ux + r * std::cos(t))), static_cast<Nm>(std::llround(uy + r * std::sin(t)))};
    r2.push_back(oblong(prec, q, p.l));
    prec = q;
  }
  return r2;
}
std::map<Couche, std::map<std::string, Polygones>> cuivreParReseau(const ModeleFabrication& m) {
  std::map<Couche, std::map<std::string, Polygones>> r;
  for (const auto& cf : m.couches) {
    if (!estCuivre(cf.couche)) continue;
    for (const auto& p : cf.primitives) {
      auto& v = r[cf.couche][p.reseau];
      if (p.type == PrimitiveFab::Type::Arc)
        for (auto& x : formeArc(p)) v.push_back(std::move(x));
      else
        v.push_back(formePrimitive(p));
    }
  }
  return r;
}
Polygones formesCouche(const ModeleFabrication& m, Couche k) {
  Polygones r;
  for (const auto& cf : m.couches)
    if (cf.couche == k)
      for (const auto& p : cf.primitives) {
        if (p.type == PrimitiveFab::Type::Arc)
          for (auto& x : formeArc(p)) r.push_back(std::move(x));
        else
          r.push_back(formePrimitive(p));
      }
  return r;
}
Pt centreIlot(const Polygone& p) {
  const Boite b = boite(p);
  return b.centre();
}
// Vignette : le cuivre (gris), la zone en cause (rouge) dans une fenêtre de « demi » autour du point
std::string vignette(const Polygones& fond, const Polygones& marque, Pt c, Nm demi) {
  const Polygone fenetre = rectangle(c, 2 * demi, 2 * demi);
  const Polygones f = intersecter(fond, {fenetre}), mq = intersecter(marque, {fenetre});
  std::ostringstream s;
  const double x0 = static_cast<double>(c.x - demi), y0 = static_cast<double>(c.y - demi), k = 160.0 / static_cast<double>(2 * demi);
  s << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"160\" height=\"160\" viewBox=\"0 0 160 160\"><rect width=\"160\" height=\"160\" fill=\"#0b3d20\"/>";
  auto chemin = [&](const Polygones& ps, const char* couleur) {
    s << "<path fill-rule=\"nonzero\" fill=\"" << couleur << "\" d=\"";
    for (const auto& p : ps) {
      for (std::size_t i = 0; i < p.size(); ++i) {
        char t[64];
        std::snprintf(t, sizeof t, "%c%.1f %.1f ", i ? 'L' : 'M', (static_cast<double>(p[i].x) - x0) * k, (static_cast<double>(p[i].y) - y0) * k);
        s << t;
      }
      s << "Z ";
    }
    s << "\"/>";
  };
  chemin(f, "#c8a24a");
  chemin(mq, "#e53935");
  s << "</svg>";
  return s.str();
}
}  // namespace

std::vector<ControleFabrication> controlesFabricationParDefaut() {
  std::vector<ControleFabrication> v;
  v.push_back({"DFM-21", "Piège à acide", "gravure", "", Gravite::Avertissement, 5, true, {{"rayon", 0.1}, {"angle", 70}}});
  v.push_back({"DFM-22", "Copeau de vernis", "vernis", "", Gravite::Avertissement, 3, true, {{"largeur", 0.1}}});
  v.push_back({"DFM-23", "Écart entre boîtiers pour la machine", "assemblage", "", Gravite::Avertissement, 2, true, {{"ecart", 0.3}}});
  v.push_back({"DFM-24", "Écart de retouche autour d'un BGA ou d'un QFN", "assemblage", "", Gravite::Avertissement, 1, true, {{"ecart", 2.0}}});
  v.push_back({"DFM-25", "Soulèvement d'un petit passif", "assemblage", "", Gravite::Avertissement, 3, true, {{"rapport", 3.0}}});
  v.push_back({"DFM-26", "Pâte sur une via", "assemblage", "", Gravite::Avertissement, 3, true, {}});
  v.push_back({"DFM-27", "Ligne de nomenclature incomplète", "nomenclature", "", Gravite::Avertissement, 1, true, {}});
  v.push_back({"DFM-28", "Article sans profil de refusion", "assemblage", "", Gravite::Avertissement, 0.5, true, {}});
  v.push_back({"DFM-29", "Pièce sensible à l'humidité à étuver", "assemblage", "", Gravite::Avertissement, 0.5, true, {{"msl", 3}}});
  return v;
}

std::vector<ControleFabrication> lireControlesFabrication(const std::string& json, std::string* erreur) {
  std::vector<ControleFabrication> v;
  try {
    const J j = J::parse(json);
    for (const auto& x : j.at("controles")) {
      ControleFabrication k;
      k.code = x.at("code").get<std::string>();
      k.nom = x.value("nom", k.code);
      k.procede = x.value("procede", std::string());
      k.fabricant = x.value("fabricant", std::string());
      k.gravite = graviteDepuis(x.value("gravite", std::string("avertissement")));
      k.poids = x.value("poids", 1.0);
      k.actif = x.value("actif", true);
      if (x.contains("reglages"))
        for (auto it = x["reglages"].begin(); it != x["reglages"].end(); ++it) k.reglages[it.key()] = it.value().get<double>();
      v.push_back(k);
    }
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return {};
  }
  return v;
}

std::string texteControlesFabrication(const std::vector<ControleFabrication>& v) {
  J a = J::array();
  for (const auto& k : v) {
    J r = J::object();
    for (const auto& [c, x] : k.reglages) r[c] = x;
    a.push_back({{"code", k.code}, {"nom", k.nom}, {"procede", k.procede}, {"fabricant", k.fabricant}, {"gravite", nomGravite(k.gravite)}, {"poids", k.poids}, {"actif", k.actif}, {"reglages", r}});
  }
  J j;
  j["format"] = "promethee-controles-fabrication";
  j["controles"] = a;
  return j.dump(1);
}

BilanFabrication controlerFabrication(const ConceptionElec& c, const std::vector<ControleFabrication>& controles, const std::atomic<bool>* arret) {
  BilanFabrication b;
  const Connectivite k = connectivite(c);
  const Remplissages zones = remplirZones(c, k);
  const ModeleFabrication m = modeleFabrication(c, k, zones);
  auto arrete = [&] {
    if (arret && arret->load()) {
      b.interrompu = true;
      return true;
    }
    return false;
  };
  auto constat = [&](const ControleFabrication& kk, ConstatFabrication x) {
    x.code = kk.code;
    x.gravite = kk.gravite;
    b.constats.push_back(std::move(x));
    b.score -= kk.poids;
  };
  std::map<Couche, std::map<std::string, Polygones>> parReseau;
  bool cuivreCalcule = false;
  for (const auto& kk : controles) {
    if (!kk.actif || arrete()) continue;
    if (kk.code == "DFM-21") {
      // Fermeture morphologique du cuivre de chaque réseau : ce qu'elle ajoute est un coin trop aigu (le liquide de
      // gravure y stagne) ; l'aire comblée par un angle θ vaut r² (cot θ/2 − (π − θ)/2), comparée à celle de l'angle
      // limite du réglage
      if (!cuivreCalcule) {
        parReseau = cuivreParReseau(m);
        cuivreCalcule = true;
      }
      const double r = reglage(kk, "rayon", 0.1), theta = reglage(kk, "angle", 70) * std::numbers::pi / 180;
      const double seuil = r * r * (1 / std::tan(theta / 2) - (std::numbers::pi - theta) / 2);
      for (const auto& [couche, reseaux] : parReseau) {
        Polygones tout;
        for (const auto& [n, v] : reseaux) tout.insert(tout.end(), v.begin(), v.end());
        const Polygones cuivreTout = unir(tout);
        for (const auto& [n, v] : reseaux) {
          if (arrete()) break;
          const Polygones cuivre = unir(v);
          const Polygones ferme = gonfler(gonfler(cuivre, nm(r)), -nm(r));
          for (const auto& il : ilots(soustraire(ferme, cuivreTout))) {
            Polygones morceau{il.exterieur};
            morceau.insert(morceau.end(), il.trous.begin(), il.trous.end());
            if (aireMm2(morceau) < seuil) continue;
            ConstatFabrication x;
            x.position = centreIlot(il.exterieur);
            x.couche = couche;
            x.message = std::string("Piège à acide sur ") + nomCouche(couche) + (n.empty() ? "" : " (réseau " + n + ")") + " : coin de cuivre plus aigu que " +
                        mmTexte(reglage(kk, "angle", 70), 0) + "°";
            x.conseil = "Raccorder les pistes à 90° ou plus, ou arrondir le coin (goutte).";
            x.vignette = vignette(cuivreTout, morceau, x.position, 1'500'000);
            constat(kk, x);
          }
        }
      }
    } else if (kk.code == "DFM-22") {
      // Vernis restant entre deux ouvertures plus étroit que la largeur réglée : il se décolle
      const double w = reglage(kk, "largeur", 0.1);
      for (const Couche vc : {Couche::VernisDessus, Couche::VernisDessous}) {
        const Polygones ouvertures = unir(formesCouche(m, vc));
        if (ouvertures.empty()) continue;
        const Polygones vernis = soustraire({m.contour}, ouvertures);
        const Polygones ouvert = gonfler(gonfler(vernis, -nm(w / 2)), nm(w / 2));
        for (const auto& il : ilots(soustraire(vernis, ouvert))) {
          Polygones morceau{il.exterieur};
          if (aireMm2(morceau) < 0.0005) continue;
          // Bande fine entre deux ouvertures (et pas un simple arrondi d'angle) : longueur nettement plus grande que w
          const Boite bb = boite(il.exterieur);
          if (std::max(mm(bb.largeur()), mm(bb.hauteur())) < 2 * w) continue;
          ConstatFabrication x;
          x.position = centreIlot(il.exterieur);
          x.couche = vc;
          x.message = std::string("Copeau de vernis sur ") + nomCouche(vc) + " : bande de moins de " + mmTexte(w) + " mm entre deux ouvertures";
          x.conseil = "Réunir les deux ouvertures (pont de vernis supprimé) ou les écarter.";
          x.vignette = vignette(ouvertures, morceau, x.position, 1'500'000);
          constat(kk, x);
        }
      }
    } else if (kk.code == "DFM-23" || kk.code == "DFM-24") {
      const double ecart = reglage(kk, "ecart", kk.code == "DFM-23" ? 0.3 : 2.0);
      struct Corps {
        const EmpreintePlacee* e;
        Polygone p;
        std::string ref, famille;
      };
      std::vector<Corps> corps;
      for (const auto& e : c.circuit.empreintes) {
        const DefEmpreinte* d = c.empreinte(e.empreinte);
        const ComposantElec* x = c.composant(e.composant);
        if (!d || !x) continue;
        corps.push_back({&e, encombrementSurCarte(e, *d), x->reference, familleBoitier(e.copieLocale.empty() ? e.empreinte : e.copieLocale)});
      }
      for (std::size_t i = 0; i < corps.size(); ++i)
        for (std::size_t j = i + 1; j < corps.size(); ++j) {
          if (corps[i].e->dessous != corps[j].e->dessous) continue;
          if (kk.code == "DFM-24") {
            const bool fragileI = corps[i].famille == "BGA" || corps[i].famille == "QFN", fragileJ = corps[j].famille == "BGA" || corps[j].famille == "QFN";
            if (!fragileI && !fragileJ) continue;
          }
          const double d = distancePolygones(corps[i].p, corps[j].p) * 1e-6;
          if (d <= 0 || d >= ecart) continue;
          ConstatFabrication x;
          x.position = Pt{(corps[i].e->position.x + corps[j].e->position.x) / 2, (corps[i].e->position.y + corps[j].e->position.y) / 2};
          x.message = corps[i].ref + " et " + corps[j].ref + " à " + mmTexte(d) + " mm (" + (kk.code == "DFM-23" ? "machine" : "retouche") + " : " + mmTexte(ecart, 1) + " mm)";
          x.conseil = kk.code == "DFM-23" ? "Écarter les boîtiers pour la buse de la machine." : "Laisser la place de la buse d'air chaud autour du boîtier à retoucher.";
          x.objets = {corps[i].e->composant, corps[j].e->composant};
          constat(kk, x);
        }
    } else if (kk.code == "DFM-25") {
      // Petits passifs à deux pastilles (0402 et moins) : cuivre très différent d'une pastille à l'autre
      const double rapport = reglage(kk, "rapport", 3.0);
      const auto pp = pastilles(c, k);
      if (!cuivreCalcule) {
        parReseau = cuivreParReseau(m);
        cuivreCalcule = true;
      }
      std::map<std::string, std::vector<const PastillePlacee*>> parComposant;
      for (const auto& p : pp) parComposant[p.composant].push_back(&p);
      for (const auto& [comp, v] : parComposant) {
        if (v.size() != 2) continue;
        const EmpreintePlacee* e = nullptr;
        for (const auto& x : c.circuit.empreintes)
          if (x.composant == comp) e = &x;
        if (!e) continue;
        const std::string id = e->copieLocale.empty() ? e->empreinte : e->copieLocale;
        if (id.find("0402") == std::string::npos && id.find("0201") == std::string::npos && id.find("01005") == std::string::npos) continue;
        // Cuivre du réseau qui touche chaque pastille, dans un rayon de 1 mm (masse thermique)
        double aires[2] = {0, 0};
        for (int i = 0; i < 2; ++i) {
          const Couche couche = v[static_cast<std::size_t>(i)]->dessous ? Couche::CuivreDessous : Couche::CuivreDessus;
          const auto itc = parReseau.find(couche);
          if (itc == parReseau.end()) continue;
          const auto itr = itc->second.find(v[static_cast<std::size_t>(i)]->reseau);
          if (itr == itc->second.end()) continue;
          aires[i] = aireMm2(intersecter(unir(itr->second), {cercle(v[static_cast<std::size_t>(i)]->pastille.position, 1'000'000)}));
        }
        const double grand = std::max(aires[0], aires[1]), petit = std::max(1e-6, std::min(aires[0], aires[1]));
        if (grand / petit < rapport) continue;
        ConstatFabrication x;
        x.position = e->position;
        x.couche = e->dessous ? Couche::CuivreDessous : Couche::CuivreDessus;
        x.message = v[0]->reference + " : cuivre " + mmTexte(grand / petit, 1) + " fois plus grand d'un côté (soulèvement au refusion)";
        x.conseil = "Relier la pastille au plan par des rayons thermiques, ou équilibrer les deux côtés.";
        x.objets = {comp};
        constat(kk, x);
      }
    } else if (kk.code == "DFM-26") {
      for (const Couche pc : {Couche::PateDessus, Couche::PateDessous}) {
        const Polygones pate = unir(formesCouche(m, pc));
        if (pate.empty()) continue;
        for (const auto& v : c.circuit.vias) {
          if (intersecter(pate, {cercle(v.p, std::max<Nm>(v.percage / 2, 1))}).empty()) continue;
          ConstatFabrication x;
          x.position = v.p;
          x.couche = pc;
          x.message = std::string("Via sous une ouverture de pâte (") + nomCouche(pc) + ") : la soudure s'y écoule";
          x.conseil = "Boucher la via (IPC-4761) ou la sortir de la pastille.";
          x.objets = {v.uuid};
          constat(kk, x);
        }
      }
    } else if (kk.code == "DFM-27") {
      for (const auto& l : nomenclature(c)) {
        if (!l.mpn.empty() && !l.fabricant.empty()) continue;
        std::string refs;
        for (const auto& r : l.references) refs += (refs.empty() ? "" : ", ") + r;
        ConstatFabrication x;
        x.message = "Nomenclature : " + refs + " (" + l.valeur + ") sans " + (l.mpn.empty() ? std::string("référence fabricant") : std::string("fabricant"));
        x.conseil = "Choisir l'article réel (bibliothèque d'articles).";
        constat(kk, x);
      }
    } else if (kk.code == "DFM-28" || kk.code == "DFM-29") {
      for (const auto& x : c.composants) {
        if (!dansNomenclature(c, x) || x.nePasMonter) continue;
        auto champ = [&](const char* cle) {
          for (const auto& [n, v] : x.champs) {
            std::string a = n;
            for (auto& ch : a) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (a == cle) return v;
          }
          return std::string();
        };
        if (kk.code == "DFM-28") {
          const std::string pic = champ("refusion");
          if (pic.empty()) {
            ConstatFabrication y;
            y.message = x.reference + " : pas de température de refusion connue";
            y.objets = {x.uuid};
            constat(kk, y);
            continue;
          }
          const double t = std::atof(pic.c_str());
          if (t > 0 && (b.picRefusion == 0 || t < b.picRefusion)) {
            b.picRefusion = t;
            b.profilRefusion = "pic " + mmTexte(t, 0) + " °C, imposé par " + x.reference + (x.mpn.empty() ? "" : " (" + x.mpn + ")");
          }
        } else {
          const std::string msl = champ("msl");
          if (!msl.empty() && std::atof(msl.c_str()) >= reglage(kk, "msl", 3)) {
            b.aEtuver.push_back(x.reference);
            ConstatFabrication y;
            y.message = x.reference + " : MSL " + msl + ", à étuver avant refusion s'il a pris l'humidité (J-STD-033)";
            y.objets = {x.uuid};
            constat(kk, y);
          }
        }
      }
    }
  }
  std::sort(b.aEtuver.begin(), b.aEtuver.end(), ordreNaturel);
  b.score = std::max(0.0, b.score);
  return b;
}

std::string rapportFabricationHtml(const ConceptionElec& c, const BilanFabrication& b) {
  std::ostringstream s;
  auto echapper = [](const std::string& t) {
    std::string r;
    for (char ch : t) r += ch == '<' ? std::string("&lt;") : ch == '>' ? std::string("&gt;") : ch == '&' ? std::string("&amp;") : std::string(1, ch);
    return r;
  };
  s << "<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\"><title>Contrôles de fabrication — " << echapper(c.nom) << "</title>"
    << "<style>body{font-family:sans-serif;margin:2em}table{border-collapse:collapse}td,th{border:1px solid #ccc;padding:6px;vertical-align:top}.erreur{color:#c62828}.avertissement{color:#ef6c00}</style></head><body>";
  s << "<h1>Contrôles de fabrication — " << echapper(c.nom) << " révision " << echapper(c.cartouche.revision) << "</h1>";
  s << "<p>Score : " << static_cast<int>(std::lround(b.score)) << " / 100 · " << b.constats.size() << " constat(s)";
  if (!b.profilRefusion.empty()) s << " · profil de refusion : " << echapper(b.profilRefusion);
  if (!b.aEtuver.empty()) {
    s << " · à étuver :";
    for (const auto& r : b.aEtuver) s << ' ' << echapper(r);
  }
  s << "</p><table><tr><th>Code</th><th>Gravité</th><th>Constat</th><th>Vue</th></tr>";
  for (const auto& x : b.constats) {
    char pos[64];
    std::snprintf(pos, sizeof pos, " (%.2f ; %.2f mm)", mm(x.position.x), mm(x.position.y));
    s << "<tr><td>" << x.code << "</td><td class=\"" << nomGravite(x.gravite) << "\">" << nomGravite(x.gravite) << "</td><td>" << echapper(x.message) << pos << "<br><i>" << echapper(x.conseil)
      << "</i></td><td>" << x.vignette << "</td></tr>";
  }
  s << "</table></body></html>\n";
  return s.str();
}

}  // namespace prom::ecad

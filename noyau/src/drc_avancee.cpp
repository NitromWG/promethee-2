// Prométhée : contrôles de la DRC ajoutés par la 0.34 — zones de règles et de placement (5.4, DRC-44), règles du
// gestionnaire de contraintes (5.27 : couches permises, nombre de vias, interdictions, isolements des tensions de
// service), quinze contrôles nouveaux (5.28, DRC-60 à DRC-74), paires et accords (5.10, 5.22 : DRC-53, DRC-54),
// proximité posée au schéma (5.30, DRC-76), ligne de fuite (8.10, DRC-55), cinq familles de 8.25 (DRC-77 à DRC-81)
// et contre-perçage (8.23, DRC-82). Appelés par verifierDRC, sur les mêmes données constantes. Licence GPL-3.0-only.
#include "promethee/drc_avancee.hpp"
#include "promethee/drc_familles.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <set>

#include "promethee/circuit_edition.hpp"
#include "promethee/schema_avance.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/piles_pastilles.hpp"
#include "promethee/regles_carte.hpp"

namespace prom::ecad {

namespace {
std::string mmTexte(double v) {
  char t[32];
  std::snprintf(t, sizeof t, "%.3f", v * 1e-6);
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  return s + " mm";
}
Pt milieu(Pt a, Pt b) { return {(a.x + b.x) / 2, (a.y + b.y) / 2}; }

// Morceaux de cuivre : pistes, vias, régions et pastilles réunis par contact, couche par couche
struct Cuivre {
  enum class Genre { Pastille, Piste, Via, Region } genre;
  std::size_t indice;  // dans sa liste (pastilles, pistes, vias, régions)
  std::string reseau;
  Couches couches;
  Polygones forme;
  Boite boite;
};
struct Ensembles {
  std::vector<std::size_t> p;
  explicit Ensembles(std::size_t n) : p(n) {
    for (std::size_t i = 0; i < n; ++i) p[i] = i;
  }
  std::size_t racine(std::size_t x) {
    while (p[x] != x) x = p[x] = p[p[x]];
    return x;
  }
  void unir(std::size_t a, std::size_t b) {
    a = racine(a);
    b = racine(b);
    if (a != b) p[std::max(a, b)] = std::min(a, b);
  }
};
}  // namespace

void verifierDRCAvancee(const ConceptionElec& c, const Connectivite& k, const Remplissages& remplis, const std::vector<PastillePlacee>& pp, std::vector<Violation>& v) {
  auto ajouter = [&](Gravite g, const char* code, std::string m, std::string conseil, Pt p, std::vector<std::string> objets = {}) {
    v.push_back({g, code, std::move(m), std::move(conseil), {}, p, std::move(objets)});
  };
  const Circuit& ci = c.circuit;
  const int nc = ci.empilage.couchesCuivre;
  const Couches cuivres = tousLesCuivres(nc);

  // ---------- 5.4 · Zones de règles, d'interdiction et de placement (DRC-44) ----------
  for (const auto& z : ci.zonesRegles) {
    if (z.contour.size() < 3) continue;
    const Polygone zone = orienter(z.contour);
    const Boite bz = boite(zone);
    const Couches visees = z.couches ? z.couches : cuivres;
    const std::string nom = z.nom.empty() ? std::string("sans nom") : z.nom;
    auto touche = [&](const Polygones& forme) { return !forme.empty() && bz.touche(boite(forme)) && !intersecter(forme, {zone}).empty(); };
    if (z.interditPistes)
      for (const auto& p : ci.pistes)
        if ((bit(p.couche) & visees) && touche(formePiste(p)))
          ajouter(Gravite::Erreur, "DRC-44", "Piste de « " + p.reseau + " » dans la zone interdite aux pistes « " + nom + " »", "Contourne la zone, ou retire l'interdiction.", milieu(p.a, p.b), {p.uuid, z.uuid});
    if (z.interditVias)
      for (const auto& x : ci.vias)
        if ((couchesVia(x, nc) & visees) && touche({cercle(x.p, x.diametre / 2)}))
          ajouter(Gravite::Erreur, "DRC-44", "Via dans la zone interdite aux vias « " + nom + " »", "Déplace la via hors de la zone.", x.p, {x.uuid, z.uuid});
    if (z.interditPastilles)
      for (const auto& p : pp)
        if ((p.couches & visees) && touche({p.forme}))
          ajouter(Gravite::Erreur, "DRC-44", "Pastille " + p.reference + "." + p.numero + " dans la zone interdite aux pastilles « " + nom + " »", "Déplace le composant hors de la zone.", p.pastille.position,
                  {p.composant, z.uuid});
    if (z.interditZones)
      for (const auto& y : ci.zones)
        if ((bit(y.couche) & visees))
          if (const auto it = remplis.find(y.uuid); it != remplis.end() && touche(it->second))
            ajouter(Gravite::Erreur, "DRC-44", "Zone de cuivre « " + y.reseau + " » dans la zone interdite « " + nom + " »", "Rogne la zone de cuivre, ou retire l'interdiction.", zone.front(), {y.uuid, z.uuid});
    // Composants : interdits, hauteur maximale (5.24), zone de placement (5.30)
    for (const auto& e : ci.empreintes) {
      const DefEmpreinte* d = c.empreinte(e.empreinte);
      const ComposantElec* x = c.composant(e.composant);
      if (!d || !x) continue;
      const Polygone enc = encombrementSurCarte(e, *d);
      const bool dedansZone = touche({enc});
      if (z.interditEmpreintes && dedansZone)
        ajouter(Gravite::Erreur, "DRC-44", x->reference + " dans la zone interdite aux composants « " + nom + " »", "Déplace-le hors de la zone.", e.position, {e.composant, z.uuid});
      if (z.hauteurMax > 0 && dedansZone) {
        const double h = hauteurComposant(x, d).mm;
        if (nm(h) > z.hauteurMax)
          ajouter(Gravite::Erreur, "DRC-44", x->reference + " haut de " + mmTexte(nm(h) * 1.0) + " dans la zone « " + nom + " » limitée à " + mmTexte(static_cast<double>(z.hauteurMax)),
                  "Choisis un composant plus bas, ou place-le ailleurs.", e.position, {e.composant, z.uuid});
      }
    }
    if (!z.placement.empty()) {
      // Composants à ranger dans la zone : ceux de la feuille ou de la classe qu'elle désigne
      std::set<std::string> attendus;
      if (z.placement.rfind("feuille:", 0) == 0) {
        const std::string f = z.placement.substr(8);
        for (const auto& fe : c.feuilles)
          if (fe.uuid == f || fe.nom == f)
            for (const auto& s : fe.symboles)
              if (!s.composant.empty()) attendus.insert(s.composant);
      } else if (z.placement.rfind("classe:", 0) == 0) {
        const std::string nomClasse = z.placement.substr(7);
        for (const auto& kc : ci.regles.avancees.classesComposants)
          if (kc.nom == nomClasse)
            for (const ComposantElec* x : membresClasse(c, kc)) attendus.insert(x->uuid);
      }
      for (const auto& e : ci.empreintes) {
        if (!attendus.count(e.composant)) continue;
        const DefEmpreinte* d = c.empreinte(e.empreinte);
        const ComposantElec* x = c.composant(e.composant);
        if (!d || !x) continue;
        const Polygone enc = encombrementSurCarte(e, *d);
        if (!soustraire({enc}, {zone}).empty() && aireMm2(soustraire({enc}, {zone})) > 1e-4)
          ajouter(Gravite::Avertissement, "DRC-44", x->reference + " hors de sa zone de placement « " + nom + " »", "Range-le dans la zone (Placer dans la zone).", e.position, {e.composant, z.uuid});
      }
    }
  }

  // ---------- 5.27 · Règles du gestionnaire de contraintes ----------
  const ContraintesCarte& av = ci.regles.avancees;
  if (!av.contraintes.empty()) {
    // Couches permises (DRC-57) : la piste d'un réseau visé hors de ses couches
    for (const auto& p : ci.pistes) {
      const Proprietes pr = proprietesPiste(c, p);
      const Resolution r = resoudre(c, GenreContrainte::CouchesPermises, pr);
      (void)r;
      for (const auto& kk : av.contraintes) {
        if (!kk.active || kk.genre != GenreContrainte::CouchesPermises || kk.couches == 0) continue;
        const Expression e = Expression::compiler(kk.portee);
        if (!e.valide() || !e.evaluer(pr)) continue;
        if (!(kk.couches & bit(p.couche)))
          ajouter(Gravite::Erreur, "DRC-57", "Piste de « " + p.reseau + " » sur " + nomCouche(p.couche) + ", hors des couches permises par « " + kk.nom + " »",
                  "Reporte-la sur une couche permise (le routeur les suit d'office).", milieu(p.a, p.b), {p.uuid});
      }
    }
    // Nombre de vias par réseau (DRC-58)
    std::map<std::string, int> vias;
    for (const auto& x : ci.vias) ++vias[k.canonique(x.reseau)];
    for (const auto& kk : av.contraintes) {
      if (!kk.active || kk.genre != GenreContrainte::NombreVias || kk.valeur.max <= 0) continue;
      for (const auto& [reseau, n] : vias) {
        const Expression e = Expression::compiler(kk.portee);
        if (e.valide() && e.evaluer(proprietesReseau(c, reseau)) && n > kk.valeur.max)
          ajouter(Gravite::Erreur, "DRC-58", "Réseau « " + reseau + " » : " + std::to_string(n) + " vias pour " + std::to_string(kk.valeur.max) + " permises (« " + kk.nom + " »)",
                  "Route-le avec moins de changements de couche.", {});
      }
    }
    // Interdictions par genre d'objet sous condition (DRC-59)
    for (const auto& kk : av.contraintes) {
      if (!kk.active || kk.genre != GenreContrainte::Interdiction) continue;
      const Expression e = Expression::compiler(kk.portee);
      if (!e.valide()) continue;
      const std::string objets = kk.texte.empty() ? std::string("pistes,vias") : kk.texte;
      if (objets.find("pistes") != std::string::npos)
        for (const auto& p : ci.pistes)
          if (e.evaluer(proprietesPiste(c, p)))
            ajouter(Gravite::Erreur, "DRC-59", "Piste interdite par « " + kk.nom + " »", kk.justification.empty() ? std::string("Retire-la.") : kk.justification, milieu(p.a, p.b), {p.uuid});
      if (objets.find("vias") != std::string::npos)
        for (const auto& x : ci.vias)
          if (e.evaluer(proprietesVia(c, x)))
            ajouter(Gravite::Erreur, "DRC-59", "Via interdite par « " + kk.nom + " »", kk.justification.empty() ? std::string("Retire-la.") : kk.justification, x.p, {x.uuid});
    }
    // Largeur par groupe de couches (8.24, « 0,15/0,2 ») : sous le minimum résolu
    for (const auto& p : ci.pistes) {
      const Resolution r = resoudre(c, GenreContrainte::Largeur, proprietesPiste(c, p), nullptr, estCuivreInterne(p.couche));
      const Nm min = r.valeur.minPour(estCuivreInterne(p.couche));
      if (r.trouvee && min > ci.regles.largeurMin && p.largeur < min)
        ajouter(Gravite::Erreur, "DRC-02", "Piste de " + mmTexte(static_cast<double>(p.largeur)) + " sur « " + p.reseau + " » en " + (estCuivreInterne(p.couche) ? "couche interne" : "couche externe") +
                                                 " : " + r.justification,
                "Élargis la piste à la valeur de sa contrainte.", milieu(p.a, p.b), {p.uuid});
    }
  }
  // Isolements résolus (tensions de service, CEI 60664-1, contraintes d'isolement) au-delà de la règle des classes
  {
    bool utile = !av.contraintes.empty();
    for (const auto& kl : ci.regles.classes) utile |= kl.tension > 0;
    if (utile) {
      struct O {
        Polygones forme;
        Boite b;
        std::string reseau, id;
        Couches couches;
        Proprietes pr;
      };
      std::vector<O> objets;
      for (const auto& p : ci.pistes) objets.push_back({formePiste(p), {}, k.canonique(p.reseau), p.uuid, bit(p.couche), proprietesPiste(c, p)});
      for (const auto& x : ci.vias) objets.push_back({{cercle(x.p, x.diametre / 2)}, {}, k.canonique(x.reseau), x.uuid, couchesVia(x, nc), proprietesVia(c, x)});
      for (const auto& p : pp) objets.push_back({{p.forme}, {}, p.reseau, p.composant, p.couches & cuivres, proprietesReseau(c, p.reseau)});
      Nm portee = ci.regles.isolement;
      for (const auto& kl : ci.regles.classes) portee = std::max(portee, kl.tension > 0 ? nm(std::max(isolementIPC2221(kl.tension, "B2"), ligneFuiteCEI60664(kl.tension, 3, 3, false))) : kl.isolement);
      for (const auto& kk : av.contraintes)
        if (kk.genre == GenreContrainte::Isolement) portee = std::max({portee, kk.valeur.min, kk.valeur.minInterne});
      IndexSpatial index(2 * NM_PAR_MM);
      for (std::size_t i = 0; i < objets.size(); ++i) {
        objets[i].b = boite(objets[i].forme);
        index.inserer(static_cast<int>(i), objets[i].b);
      }
      std::set<std::pair<std::string, std::string>> vus;
      for (std::size_t i = 0; i < objets.size(); ++i)
        for (int jj : index.chercher(objets[i].b.gonflee(portee))) {
          const std::size_t j = static_cast<std::size_t>(jj);
          if (j <= i) continue;
          const O& a = objets[i];
          const O& b = objets[j];
          const Couches communes = a.couches & b.couches;
          if (!communes || a.reseau.empty() || b.reseau.empty() || a.reseau == b.reseau) continue;
          bool interne = true;
          for (int ic = 0; ic < NB_COUCHES; ++ic)
            if ((communes & bit(static_cast<Couche>(ic))) && !estCuivreInterne(static_cast<Couche>(ic))) interne = false;
          const Resolution r = isolementRequis(c, a.pr, b.pr, interne);
          const Nm base = isolementEntre(ci.regles, a.reseau, b.reseau);
          const Nm requis = r.valeur.minPour(interne);
          if (!r.trouvee || requis <= base) continue;
          if (!a.b.gonflee(requis).touche(b.b)) continue;
          double d = 1e18;
          for (const auto& pa : a.forme)
            for (const auto& pb : b.forme) d = std::min(d, distancePolygones(pa, pb));
          if (d >= static_cast<double>(base) && d + 0.5 < static_cast<double>(requis) && vus.insert({std::min(a.id, b.id), std::max(a.id, b.id)}).second)
            ajouter(Gravite::Erreur, "DRC-01", "Isolement de " + mmTexte(d) + " entre « " + a.reseau + " » et « " + b.reseau + " » : " + r.justification,
                    "Écarte les deux cuivres à la distance exigée par la tension de service ou la contrainte.", a.b.centre(), {a.id, b.id});
        }
    }
  }

  // ---------- 7.11 · Textes de la carte marqués ou aux variables sans valeur (DRC-83, DRC-84) ----------
  for (auto& x : verifierTextesRiches(c, true)) v.push_back(std::move(x));

  // ---------- 5.28 · Quinze contrôles nouveaux (DRC-60 à DRC-74) ----------
  // DRC-60 : via qui ne relie qu'une couche
  for (const auto& x : ci.vias) {
    int n = 0;
    for (const Couche cu : couchesCuivre(nc)) n += (couchesVia(x, nc) & bit(cu)) ? 1 : 0;
    // Une via borgne ou enterrée déclarée de la couche vers elle-même ne relie qu'une couche, même sur deux couches
    const bool memeCouche = x.type != TypeVia::Traversante && rangCuivre(x.de, nc) == rangCuivre(x.vers, nc);
    if (nc > 1 && (n < 2 || memeCouche)) ajouter(Gravite::Erreur, "DRC-60", "Via qui ne relie qu'une couche (" + std::string(codeCouche(x.de)) + ")", "Choisis ses couches de départ et d'arrivée, ou retire-la.", x.p, {x.uuid});
  }
  // DRC-61 : zones de réseaux différents, de même priorité, qui se chevauchent sur une couche
  for (std::size_t i = 0; i < ci.zones.size(); ++i)
    for (std::size_t j = i + 1; j < ci.zones.size(); ++j) {
      const Zone& a = ci.zones[i];
      const Zone& b = ci.zones[j];
      if (a.couche != b.couche || a.priorite != b.priorite || k.canonique(a.reseau) == k.canonique(b.reseau) || a.contour.size() < 3 || b.contour.size() < 3) continue;
      if (!a.partition.empty() && a.partition == b.partition) continue;  // plan partagé : voulu
      const Polygones commun = intersecter({orienter(a.contour)}, {orienter(b.contour)});
      if (!commun.empty() && aireMm2(commun) > 1e-3)
        ajouter(Gravite::Erreur, "DRC-61", "Zones « " + a.reseau + " » et « " + b.reseau + " » de même priorité qui se chevauchent sur " + nomCouche(a.couche),
                "Donne une priorité à l'une des deux : l'ordre de remplissage ne doit pas dépendre du fichier.", boite(commun).centre(), {a.uuid, b.uuid});
    }
  // Îlots du cuivre de chaque zone, calculés une fois pour DRC-62 et DRC-63
  std::map<std::string, std::vector<Ilot>> ilotsZones;
  for (const auto& z : ci.zones)
    if (const auto it = remplis.find(z.uuid); it != remplis.end()) ilotsZones[z.uuid] = ilots(it->second);
  // DRC-62 : plan coupé en plusieurs morceaux
  for (const auto& z : ci.zones) {
    const auto it = ilotsZones.find(z.uuid);
    if (it == ilotsZones.end() || z.reseau.empty()) continue;
    const auto& morceaux = it->second;
    if (morceaux.size() >= 2 && z.remplissage == RemplissageZone::Plein)
      ajouter(Gravite::Avertissement, "DRC-62", "Plan « " + z.reseau + " » coupé en " + std::to_string(morceaux.size()) + " morceaux sur " + nomCouche(z.couche),
              "Une piste ou une rangée de vias coupe le plan : le courant de retour fait le tour. Écarte-les ou relie les morceaux.", morceaux.front().exterieur.front(), {z.uuid});
  }
  // DRC-63 : cuivre mort, morceau d'un réseau sans aucune pastille
  {
    std::vector<Cuivre> cu;
    for (std::size_t i = 0; i < pp.size(); ++i)
      if (pp[i].couches & cuivres) cu.push_back({Cuivre::Genre::Pastille, i, pp[i].reseau, pp[i].couches & cuivres, {pp[i].forme}, boite(pp[i].forme)});
    for (std::size_t i = 0; i < ci.pistes.size(); ++i) {
      Polygones f = formePiste(ci.pistes[i]);
      cu.push_back({Cuivre::Genre::Piste, i, k.canonique(ci.pistes[i].reseau), bit(ci.pistes[i].couche), f, boite(f)});
    }
    for (std::size_t i = 0; i < ci.vias.size(); ++i) {
      Polygones f{cercle(ci.vias[i].p, ci.vias[i].diametre / 2)};
      cu.push_back({Cuivre::Genre::Via, i, k.canonique(ci.vias[i].reseau), couchesVia(ci.vias[i], nc) & cuivres, f, boite(f)});
    }
    for (std::size_t i = 0; i < ci.regions.size(); ++i)
      if (ci.regions[i].forme.size() >= 3) {
        Polygones f{orienter(ci.regions[i].forme)};
        cu.push_back({Cuivre::Genre::Region, i, k.canonique(ci.regions[i].reseau), bit(ci.regions[i].couche), f, boite(f)});
      }
    const std::size_t petits = cu.size();  // pastilles, pistes, vias et régions ; les îlots des zones viennent après
    // Points de contact de chaque petit cuivre : centre d'une pastille ou d'une via, bouts d'une piste, sommets d'une
    // région ; un îlot de zone le touche s'il contient l'un d'eux (le remplissage n'a gardé que le cuivre relié)
    std::vector<std::vector<Pt>> contacts(petits);
    for (std::size_t i = 0; i < petits; ++i) {
      switch (cu[i].genre) {
        case Cuivre::Genre::Pastille: contacts[i] = {pp[cu[i].indice].pastille.position}; break;
        case Cuivre::Genre::Via: contacts[i] = {ci.vias[cu[i].indice].p}; break;
        case Cuivre::Genre::Piste: contacts[i] = {ci.pistes[cu[i].indice].a, ci.pistes[cu[i].indice].b}; break;
        case Cuivre::Genre::Region: contacts[i] = ci.regions[cu[i].indice].forme; break;
      }
    }
    for (const auto& z : ci.zones)
      if (const auto it = ilotsZones.find(z.uuid); it != ilotsZones.end())
        for (const Ilot& il : it->second) {
          Polygones f{il.exterieur};
          f.insert(f.end(), il.trous.begin(), il.trous.end());
          cu.push_back({Cuivre::Genre::Region, static_cast<std::size_t>(-1), k.canonique(z.reseau), bit(z.couche), f, boite(il.exterieur)});
        }
    Ensembles u(cu.size());
    IndexSpatial index(2 * NM_PAR_MM);
    for (std::size_t i = 0; i < petits; ++i) index.inserer(static_cast<int>(i), cu[i].boite.gonflee(1'000));
    // Seul le cuivre d'un même réseau compte (deux réseaux qui se touchent sont un court-circuit, DRC-21). Le contact
    // se mesure en distances exactes (segments épaissis, disques, polygones), sans opération booléenne
    struct Geo {
      std::vector<std::pair<Pt, Pt>> segments;  // piste : ses segments (arc en cordes), demi-largeur
      double demi = 0;
      bool disque = false;  // via : centre, rayon
      Pt centre;
      const Polygone* poly = nullptr;  // pastille, région, segment effilé
    };
    std::vector<Geo> geo(petits);
    std::vector<Polygones> formesEffilees(petits);
    for (std::size_t i = 0; i < petits; ++i) {
      Geo& g = geo[i];
      switch (cu[i].genre) {
        case Cuivre::Genre::Pastille: g.poly = &pp[cu[i].indice].forme; break;
        case Cuivre::Genre::Via:
          g.disque = true;
          g.centre = ci.vias[cu[i].indice].p;
          g.demi = static_cast<double>(ci.vias[cu[i].indice].diametre) / 2;
          break;
        case Cuivre::Genre::Piste: {
          const Piste& t = ci.pistes[cu[i].indice];
          if (!t.milieu && (t.largeurFin > 0 || t.boutsCarres)) {
            formesEffilees[i] = cu[i].forme;
            if (!formesEffilees[i].empty()) g.poly = &formesEffilees[i].front();
          } else {
            g.segments = segmentsPiste(t);
            g.demi = static_cast<double>(t.largeur) / 2;
          }
          break;
        }
        case Cuivre::Genre::Region: g.poly = &ci.regions[cu[i].indice].forme; break;
      }
    }
    auto touchent = [&](const Geo& a, const Geo& b) {
      constexpr double jeu = 1'000;
      if (!a.segments.empty() && !b.segments.empty()) {
        for (const auto& [p, q] : a.segments)
          for (const auto& [r, t] : b.segments)
            if (distanceSegments(p, q, r, t) - a.demi - b.demi <= jeu) return true;
        return false;
      }
      if (a.disque && b.disque) return distance(a.centre, b.centre) - a.demi - b.demi <= jeu;
      if (a.disque || b.disque) {
        const Geo& d = a.disque ? a : b;
        const Geo& o = a.disque ? b : a;
        for (const auto& [p, q] : o.segments)
          if (distancePointSegment(d.centre, p, q) - d.demi - o.demi <= jeu) return true;
        return o.poly && (dedans(*o.poly, d.centre) || distancePolygoneSegment(*o.poly, d.centre, d.centre) - d.demi <= jeu);
      }
      if (a.poly && b.poly) return distancePolygones(*a.poly, *b.poly) <= jeu;
      const Geo& s2 = a.poly ? b : a;
      const Geo& o = a.poly ? a : b;
      if (!o.poly) return false;
      for (const auto& [p, q] : s2.segments)
        if (distancePolygoneSegment(*o.poly, p, q) - s2.demi <= jeu) return true;
      return false;
    };
    for (std::size_t i = 0; i < petits; ++i)
      for (int jj : index.chercher(cu[i].boite.gonflee(1'000))) {
        const std::size_t j = static_cast<std::size_t>(jj);
        if (j <= i || cu[i].reseau != cu[j].reseau || !(cu[i].couches & cu[j].couches) || !cu[i].boite.gonflee(1'000).touche(cu[j].boite)) continue;
        if (touchent(geo[i], geo[j])) u.unir(i, j);
      }
    for (std::size_t i = petits; i < cu.size(); ++i)
      for (int jj : index.chercher(cu[i].boite)) {
        const std::size_t j = static_cast<std::size_t>(jj);
        if (cu[i].reseau != cu[j].reseau || !(cu[i].couches & cu[j].couches)) continue;
        // Contour extérieur de l'îlot seul : une pastille à liaison thermique est dans un trou du plan, que ses rayons
        // relient ; le remplissage n'a gardé que des îlots reliés à leur réseau
        if (std::any_of(contacts[j].begin(), contacts[j].end(), [&](Pt q) { return cu[i].boite.contient(q) && dedans(cu[i].forme.front(), q); })) u.unir(i, j);
      }
    std::map<std::size_t, bool> aPastille;
    for (std::size_t i = 0; i < cu.size(); ++i) aPastille[u.racine(i)] = aPastille[u.racine(i)] || cu[i].genre == Cuivre::Genre::Pastille;
    std::set<std::size_t> signales;
    for (std::size_t i = 0; i < cu.size(); ++i) {
      const std::size_t r = u.racine(i);
      if (aPastille[r] || cu[i].reseau.empty() || cu[i].genre == Cuivre::Genre::Pastille || !signales.insert(r).second) continue;
      std::string id;
      if (cu[i].genre == Cuivre::Genre::Piste) id = ci.pistes[cu[i].indice].uuid;
      else if (cu[i].genre == Cuivre::Genre::Via) id = ci.vias[cu[i].indice].uuid;
      else if (cu[i].indice != static_cast<std::size_t>(-1)) id = ci.regions[cu[i].indice].uuid;
      ajouter(Gravite::Avertissement, "DRC-63", "Cuivre mort du réseau « " + cu[i].reseau + " » : relié à aucune pastille", "Supprime ce cuivre, ou relie-le à une pastille de son réseau.", cu[i].boite.centre(),
              id.empty() ? std::vector<std::string>{} : std::vector<std::string>{id});
    }
  }
  // DRC-64 : liaison thermique affamée, rayons plus étroits que la largeur minimale de la carte
  for (const auto& z : ci.zones)
    if (z.thermique && !z.reseau.empty() && z.largeurRayon < ci.regles.largeurMin)
      ajouter(Gravite::Avertissement, "DRC-64", "Zone « " + z.reseau + " » : rayons thermiques de " + mmTexte(static_cast<double>(z.largeurRayon)) + ", plus étroits que la piste la plus fine permise (" +
                                                      mmTexte(static_cast<double>(ci.regles.largeurMin)) + ")",
              "Élargis les rayons : trop fins, ils ne portent pas le courant et le fabricant peut les perdre.", z.contour.empty() ? Pt{} : z.contour.front(), {z.uuid});
  // DRC-65 : écart entre sérigraphies, repères de deux composants qui se recouvrent
  {
    std::map<std::string, Boite> parComposant;
    for (const auto& t : textesSerigraphie(c)) {
      if (t.composant.empty()) continue;
      Boite& b = parComposant[t.composant + (t.couche == Couche::SerigraphieDessous ? "#B" : "#F")];
      b.etendre(t.a);
      b.etendre(t.b);
    }
    std::vector<std::pair<std::string, Boite>> liste(parComposant.begin(), parComposant.end());
    for (std::size_t i = 0; i < liste.size(); ++i)
      for (std::size_t j = i + 1; j < liste.size(); ++j) {
        const std::string fi = liste[i].first.substr(liste[i].first.size() - 2), fj = liste[j].first.substr(liste[j].first.size() - 2);
        if (fi != fj) continue;
        const Boite& a = liste[i].second;
        const Boite& b = liste[j].second;
        if (a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1) {
          const std::string ca = liste[i].first.substr(0, liste[i].first.size() - 2), cb = liste[j].first.substr(0, liste[j].first.size() - 2);
          const ComposantElec* x = c.composant(ca);
          const ComposantElec* y = c.composant(cb);
          ajouter(Gravite::Avertissement, "DRC-65", "Repères de " + (x ? x->reference : ca) + " et " + (y ? y->reference : cb) + " qui se recouvrent en sérigraphie",
                  "Déplace l'un des deux repères (Replacer les repères).", a.centre(), {ca, cb});
        }
      }
  }
  // DRC-66 : texte en miroir sur la mauvaise face
  for (const auto& t : ci.textes) {
    if (!t.miroir) continue;
    const bool dessous = t.couche == Couche::SerigraphieDessous || t.couche == Couche::CuivreDessous || t.couche == Couche::VernisDessous || t.couche == Couche::FabricationDessous;
    if (*t.miroir != dessous)
      ajouter(Gravite::Avertissement, "DRC-66", "Texte « " + t.texte + " » " + (*t.miroir ? "en miroir sur la face du dessus" : "à l'endroit sur la face du dessous"),
              "Un texte du dessous se lit en miroir vu de dessus, un texte du dessus à l'endroit.", t.p, {t.uuid});
  }
  // DRC-67 : dessin ou texte sur la couche du contour (la fraise le suivrait)
  for (const auto& d : ci.dessins)
    if (d.couche == Couche::Contour)
      ajouter(Gravite::Erreur, "DRC-67", "Dessin sur la couche du contour de la carte : la fraise le suivrait", "Mets-le sur une couche de dessin, ou fais-en une découpe.", d.points.empty() ? Pt{} : d.points.front(),
              {d.uuid});
  for (const auto& t : ci.textes)
    if (t.couche == Couche::Contour)
      ajouter(Gravite::Erreur, "DRC-67", "Texte « " + t.texte + " » sur la couche du contour de la carte", "Mets-le en sérigraphie.", t.p, {t.uuid});
  // DRC-68 : segment nul ou plus court que le centième de sa largeur ; DRC-69 : réseau plus long que sa contrainte
  for (const auto& p : ci.pistes)
    if (!p.milieu && distance(p.a, p.b) * 100 < static_cast<double>(p.largeur))
      ajouter(Gravite::Avertissement, "DRC-68", "Segment de piste de " + mmTexte(distance(p.a, p.b)) + " sur « " + p.reseau + " » : un reste de tracé",
              "Lance « Nettoyer les pistes » : il le fondra dans son voisin.", p.a, {p.uuid});
  {
    std::map<std::string, double> longueurs;
    for (const auto& p : ci.pistes) longueurs[k.canonique(p.reseau)] += longueurPiste(p);
    for (const auto& kk : av.contraintes) {
      if (!kk.active || kk.genre != GenreContrainte::Longueur || (kk.valeur.max <= 0 && kk.valeur.min <= 0)) continue;
      const Expression e = Expression::compiler(kk.portee);
      for (const auto& [reseau, L] : longueurs)
        if (e.valide() && e.evaluer(proprietesReseau(c, reseau))) {
          if (kk.valeur.max > 0 && L > static_cast<double>(kk.valeur.max))
            ajouter(Gravite::Erreur, "DRC-69", "Réseau « " + reseau + " » long de " + mmTexte(L) + " pour " + mmTexte(static_cast<double>(kk.valeur.max)) + " au plus (« " + kk.nom + " »)",
                    "Raccourcis son tracé.", {});
          if (kk.valeur.min > 0 && L < static_cast<double>(kk.valeur.min))
            ajouter(Gravite::Erreur, "DRC-69", "Réseau « " + reseau + " » long de " + mmTexte(L) + " pour " + mmTexte(static_cast<double>(kk.valeur.min)) + " au moins (« " + kk.nom + " »)",
                    "Allonge-le par un motif d'accord.", {});
        }
    }
  }
  // DRC-70 : piste qui touche une via hors de son centre
  for (const auto& p : ci.pistes)
    for (const auto& x : ci.vias) {
      if (k.canonique(x.reseau) != k.canonique(p.reseau) || !(couchesVia(x, nc) & bit(p.couche))) continue;
      for (const Pt bout : {p.a, p.b}) {
        const double d = distance(bout, x.p);
        if (d > 1'000 && d < static_cast<double>(x.diametre) / 2 && distancePointSegment(x.p, p.a, p.b) > 1'000)
          ajouter(Gravite::Avertissement, "DRC-70", "Piste de « " + p.reseau + " » qui s'arrête sur la via à " + mmTexte(d) + " de son centre",
                  "Amène la piste au centre de la via : un raccord excentré se casse au perçage.", bout, {p.uuid, x.uuid});
      }
    }
  // DRC-71 à DRC-74 : pastilles et trous des empreintes posées revérifiés
  for (const auto& e : ci.empreintes) {
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    const ComposantElec* x = c.composant(e.composant);
    if (!d || !x) continue;
    for (std::size_t i = 0; i < d->pastilles.size(); ++i) {
      const Pastille& pa = d->pastilles[i];
      const Pastille pc = pastilleSurCarte(e, pa);
      if (pa.type == TypePastille::Traversante && pa.percage > 0 && std::min(pa.largeur, pa.hauteur) <= pa.percage && pa.forme != FormePastille::Libre)
        ajouter(Gravite::Erreur, "DRC-71", x->reference + "." + pa.numero + " : pastille de " + mmTexte(static_cast<double>(std::min(pa.largeur, pa.hauteur))) + " pour un perçage de " +
                                                 mmTexte(static_cast<double>(pa.percage)),
                "Agrandis la pastille : le foret ne laisserait aucun cuivre.", pc.position, {e.composant});
      if (pa.percage > 0 && pa.type != TypePastille::NonMetallisee) {
        const Polygone forme = formePastille(pc);
        const Pt trou = surPastille(pc, pc.decalagePercage);
        if (!dedans(forme, trou))
          ajouter(Gravite::Erreur, "DRC-72", x->reference + "." + pa.numero + " : le trou sort de sa pastille", "Ramène le décalage du trou dans la pastille.", trou, {e.composant});
      }
      if (pa.longueurPercage > 0 && pa.longueurPercage <= pa.percage)
        ajouter(Gravite::Avertissement, "DRC-74", x->reference + "." + pa.numero + " : fente plus courte que son diamètre (un trou rond)", "Donne à la fente une longueur supérieure à sa largeur.",
                pc.position, {e.composant});
      for (const auto& t : pa.trousEnPlus)
        if (!dedans(formePastille(pc), surPastille(pc, t.decalage)))
          ajouter(Gravite::Erreur, "DRC-74", x->reference + "." + pa.numero + " : un trou en plus sort de la pastille", "Ramène-le dans la pastille.", surPastille(pc, t.decalage), {e.composant});
      for (std::size_t j = i + 1; j < d->pastilles.size(); ++j) {
        const Pastille& pb = d->pastilles[j];
        if (pb.numero == pa.numero || pa.numero.empty() || pb.numero.empty()) continue;
        if (!(couchesPastille(pa, false, nc) & couchesPastille(pb, false, nc) & cuivres)) continue;
        const Polygones commun = intersecter({formePastille(pa)}, {formePastille(pb)});
        if (!commun.empty() && aireMm2(commun) > 1e-5)
          ajouter(Gravite::Erreur, "DRC-73", x->reference + " : pastilles " + pa.numero + " et " + pb.numero + " qui se recouvrent dans l'empreinte",
                  "Corrige l'empreinte : deux broches différentes seraient en court-circuit.", pc.position, {e.composant});
      }
    }
  }

  // ---------- 5.10, 5.22 · Paires différentielles et accords (DRC-53, DRC-54) ----------
  {
    std::map<std::string, double> longueurs;
    for (const auto& p : ci.pistes) longueurs[k.canonique(p.reseau)] += longueurPiste(p);
    for (const auto& paire : av.paires) {
      const std::string P = k.canonique(paire.positif), M = k.canonique(paire.negatif);
      double couple = 0;
      double total = 0;
      for (const auto& a : ci.pistes) {
        if (k.canonique(a.reseau) != P) continue;
        total += longueurPiste(a);
        for (const auto& b : ci.pistes) {
          if (k.canonique(b.reseau) != M || b.couche != a.couche) continue;
          const double ecart = distanceSegments(a.a, a.b, b.a, b.b) - static_cast<double>(a.largeur + b.largeur) / 2;
          // Écart mesuré là où les deux tronçons sont parallèles et en vis-à-vis
          const double da = distance(a.a, a.b), db = distance(b.a, b.b);
          if (da < 1 || db < 1) continue;
          const double ux = static_cast<double>(a.b.x - a.a.x) / da, uy = static_cast<double>(a.b.y - a.a.y) / da;
          const double vx = static_cast<double>(b.b.x - b.a.x) / db, vy = static_cast<double>(b.b.y - b.a.y) / db;
          if (std::abs(ux * vy - uy * vx) > 0.02) continue;
          const double portee = paire.ecartMax > 0 ? static_cast<double>(paire.ecartMax) * 1.5 : 3.0 * static_cast<double>(std::max<Nm>(paire.ecart, 100'000));
          if (ecart > portee) continue;
          couple += std::min(da, db);
          if ((paire.ecartMin > 0 && ecart + 0.5 < static_cast<double>(paire.ecartMin)) || (paire.ecartMax > 0 && ecart > static_cast<double>(paire.ecartMax) + 0.5))
            ajouter(Gravite::Erreur, "DRC-53", "Paire « " + paire.nom + " » : écart de " + mmTexte(ecart) + " hors de [" + mmTexte(static_cast<double>(paire.ecartMin)) + " ; " +
                                                     mmTexte(static_cast<double>(paire.ecartMax)) + "]",
                    "Retrace la paire à écart constant (Router la paire).", milieu(a.a, a.b), {a.uuid, b.uuid});
        }
      }
      if (paire.nonCoupleMax > 0 && total - couple > static_cast<double>(paire.nonCoupleMax) + 1)
        ajouter(Gravite::Avertissement, "DRC-53", "Paire « " + paire.nom + " » : " + mmTexte(total - couple) + " non couplés (" + mmTexte(static_cast<double>(paire.nonCoupleMax)) + " au plus)",
                "Rapproche les deux pistes sur toute leur longueur.", {});
    }
    for (const auto& g : av.accords) {
      if (g.reseaux.empty()) continue;
      // En retard (8.27), cible et tolérance en femtosecondes : la longueur de chaque couche à sa vitesse
      auto mesure = [&](const std::string& r) { return g.retard ? retardReseau(c, r) * 1000 : longueurs[k.canonique(r)]; };
      auto texte = [&](double v) {
        if (!g.retard) return mmTexte(v);
        char t[32];
        std::snprintf(t, sizeof t, "%.1f ps", v / 1000);
        std::string s2 = t;
        std::replace(s2.begin(), s2.end(), '.', ',');
        return s2;
      };
      double cible = static_cast<double>(g.cible);
      if (cible <= 0)
        for (const auto& r : g.reseaux) cible = std::max(cible, mesure(r));
      for (const auto& r : g.reseaux) {
        const double L = mesure(r);
        if (std::abs(L - cible) > static_cast<double>(g.tolerance))
          ajouter(Gravite::Erreur, "DRC-54", "Accord « " + g.nom + " » : « " + r + " » " + (g.retard ? "en retard de " : "long de ") + texte(L) + " pour " + texte(cible) + " ± " + texte(static_cast<double>(g.tolerance)),
                  "Ajoute un motif d'accord (Accorder la longueur).", {});
      }
    }
  }

  // ---------- 5.30 · Proximité posée au schéma (DRC-76) : « C12 à 2 mm au plus de la broche 7 de U3 » ----------
  for (const auto& kk : av.contraintes) {
    if (!kk.active || kk.genre != GenreContrainte::Proximite || kk.valeur.max <= 0) continue;
    // Portée A : « reference == 'C12' » ; portée B : « broche == 'U3.7' » ou « reference == 'U3' »
    auto pointDe = [&](const std::string& expr, std::string& objet) -> std::optional<Pt> {
      const Expression e = Expression::compiler(expr);
      if (!e.valide()) return std::nullopt;
      for (const auto& p : pp) {
        const std::string br = p.reference + "." + p.numero;
        const bool prise = e.evaluer([&](const std::string& nom) -> std::optional<ValeurExpression> {
          if (nom == "broche") return ValeurExpression{br};
          if (nom == "reference") return ValeurExpression{p.reference};
          if (nom == "reseau") return ValeurExpression{p.reseau};
          return std::nullopt;
        });
        if (prise && !expr.empty()) {
          objet = p.composant;
          return p.pastille.position;
        }
      }
      return std::nullopt;
    };
    std::string oa, ob;
    const auto pa = pointDe(kk.portee, oa), pb = pointDe(kk.porteeB, ob);
    if (!pa || !pb) continue;
    // La distance est celle de l'empreinte la plus proche de la broche : on prend la pastille de A la plus proche
    double d = 1e18;
    for (const auto& p : pp)
      if (p.composant == oa) d = std::min(d, distance(p.pastille.position, *pb));
    if (d > static_cast<double>(kk.valeur.max))
      ajouter(Gravite::Erreur, "DRC-76", "« " + kk.nom + " » : " + mmTexte(d) + " pour " + mmTexte(static_cast<double>(kk.valeur.max)) + " au plus", kk.justification.empty() ? std::string("Rapproche le composant.") : kk.justification,
              *pa, {oa, ob});
  }

  // ---------- 8.23 · Contre-perçage (DRC-82) ----------
  for (const auto& x : ci.vias) {
    if (!x.contrePercage) continue;
    const BilanContrePercage b = bilanContrePercage(x, ci.empilage);
    if (!b.possible)
      ajouter(Gravite::Erreur, "DRC-82", "Contre-perçage impossible : " + b.raison, "Change la couche gardée ou le talon toléré.", x.p, {x.uuid});
    for (const auto& id : pistesSurCouchesCoupees(c, x))
      ajouter(Gravite::Erreur, "DRC-82", "Piste reliée à une couche que le contre-perçage de la via coupe", "Change la couche de la piste, ou arrête le contre-perçage plus tôt.", x.p, {id, x.uuid});
  }

  // ---------- 8.10, 8.25 · Ligne de fuite (DRC-55), familles DRC-77 à DRC-81 ----------
  verifierFamilles034(c, k, remplis, pp, v);
}

}  // namespace prom::ecad

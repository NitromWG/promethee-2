// Prométhée : rapports de la CAO électrique — liste des appareils, nomenclature, plan des borniers, carnet de
// câblage, liste des entrées et sorties d'automate, liste des folios, et leurs exports CSV. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "electrique_interne.hpp"
#include "promethee/electrique.hpp"
#include "promethee/electrique_fabrication.hpp"

namespace prom::elec {

using ecad::ComposantElec;
using ecad::DefSymbole;
using ecad::ordreNaturel;

namespace {

// Bornes d'un bornier : numéro réel → poses (feuille, position, borne de protection ?)
struct PoseBorne {
  std::size_t feuille = 0;
  ecad::Pt p;
  bool protection = false;
};
std::map<std::string, std::vector<PoseBorne>> bornesDe(const ProjetElectrique& p, const std::string& bornier) {
  std::map<std::string, std::vector<PoseBorne>> r;
  for (std::size_t f = 0; f < p.schema.feuilles.size(); ++f)
    for (const auto& s : p.schema.feuilles[f].symboles) {
      if (s.composant != bornier) continue;
      const DefSymbole* d = detail::definitionDe(p, s.symbole);
      if (!d || d->broches.empty()) continue;
      const std::string n = ecad::numeroBroche(s, d->broches.front().numero);
      r[n].push_back({f, s.position, s.symbole == "iec:borne-pe" || d->id.find("terre") != std::string::npos || d->id.find("-pe") != std::string::npos});
    }
  return r;
}

std::vector<std::string> numerosTries(const std::map<std::string, std::vector<PoseBorne>>& m) {
  std::vector<std::string> v;
  for (const auto& [n, poses] : m) v.push_back(n);
  std::sort(v.begin(), v.end(), ordreNaturel);
  return v;
}

std::vector<const ComposantElec*> composantsTries(const ProjetElectrique& p) {
  std::vector<const ComposantElec*> v;
  for (const auto& c : p.schema.composants) v.push_back(&c);
  std::stable_sort(v.begin(), v.end(), [](const ComposantElec* a, const ComposantElec* b) { return ordreNaturel(a->reference, b->reference); });
  return v;
}

std::string designationDe(const ComposantElec& c) {
  if (!c.description.empty()) return c.description;
  if (const EntreeCatalogue* e = detail::catalogueDe(c)) return e->designation;
  return c.valeur;
}

std::string positionsDe(const ProjetElectrique& p, const std::vector<ElementRenvoi>& elements) {
  std::vector<std::tuple<std::size_t, int, std::string>> v;
  std::set<std::string> vus;
  for (const auto& e : elements) {
    const std::string t = e.ou.texte();
    if (vus.insert(t).second) v.push_back({e.feuille, e.ou.colonne, t});
  }
  std::sort(v.begin(), v.end());
  std::string r;
  for (const auto& x : v) r += (r.empty() ? "" : ", ") + std::get<2>(x);
  (void)p;
  return r;
}

const char* SUFFIXE_COTE = "~b";

}  // namespace

std::vector<LigneAppareil> listeAppareils(const ProjetElectrique& p) {
  const Renvois r = renvois(p);
  std::vector<LigneAppareil> v;
  for (const ComposantElec* c : composantsTries(p)) {
    LigneAppareil l;
    l.uuid = c->uuid;
    l.repere = repereAffiche(p, *c);
    l.designation = designationDe(*c);
    const EntreeCatalogue* e = detail::catalogueDe(*c);
    l.fabricant = !c->fabricant.empty() ? c->fabricant : e ? e->fabricant : std::string();
    l.reference = !c->mpn.empty() ? c->mpn : e ? e->reference : std::string();
    l.fonction = detail::champ(*c, "fonction");
    l.emplacement = detail::champ(*c, "emplacement");
    if (const auto it = r.parComposant.find(c->uuid); it != r.parComposant.end()) l.folios = positionsDe(p, it->second);
    if (detail::estBornier(*c)) l.quantite = static_cast<int>(bornesDe(p, c->uuid).size());
    v.push_back(l);
  }
  return v;
}

std::vector<LigneNomenclatureElec> nomenclatureElectrique(const ProjetElectrique& p) {
  std::vector<LigneNomenclatureElec> v;
  std::map<std::tuple<std::string, std::string, std::string>, std::size_t> index;
  // « indicatif » : prix public du catalogue ou de l'appareil personnel (0 : à saisir)
  auto ajouter = [&](const std::string& designation, const std::string& fabricant, const std::string& reference, const std::string& repere, int quantite,
                     double indicatif) {
    const auto cle = std::make_tuple(designation, fabricant, reference);
    auto it = index.find(cle);
    if (it == index.end()) {
      it = index.emplace(cle, v.size()).first;
      LigneNomenclatureElec l;
      l.designation = designation;
      l.fabricant = fabricant;
      l.reference = reference;
      l.cle = clePrix(designation, fabricant, reference);
      if (const auto x = p.prix.find(l.cle); x != p.prix.end()) {
        l.prixUnitaire = x->second;
        l.prixSaisi = true;
      } else if (indicatif > 0) {
        l.prixUnitaire = indicatif;
      }
      v.push_back(l);
    }
    LigneNomenclatureElec& l = v[it->second];
    l.quantite += quantite;
    if (std::find(l.reperes.begin(), l.reperes.end(), repere) == l.reperes.end()) l.reperes.push_back(repere);
  };
  for (const ComposantElec* c : composantsTries(p)) {
    if (detail::champ(*c, "nomenclature") == "non") continue;
    const std::string repere = repereAffiche(p, *c);
    if (detail::estBornier(*c)) {
      // Un bornier se commande borne par borne : bornes de passage et bornes de protection à part
      const EntreeCatalogue* normale = detail::catalogueDe(*c);
      if (!normale) normale = entreeCatalogue(c->symbole == "iec:connecteur" ? "x-connecteur" : "x-2,5");
      const std::string idPE = detail::champ(*c, "cataloguePE");
      const EntreeCatalogue* pe = entreeCatalogue(idPE.empty() ? std::string("x-pe-2,5") : idPE);
      int normales = 0, protections = 0;
      for (const auto& [n, poses] : bornesDe(p, c->uuid)) {
        bool estPE = false;
        for (const auto& x : poses) estPE = estPE || x.protection;
        ++(estPE ? protections : normales);
      }
      if (c->symbole == "iec:connecteur") {
        if (normales + protections > 0)
          ajouter(designationDe(*c) + " (" + std::to_string(normales + protections) + " contacts)", c->fabricant, c->mpn, repere, 1, normale ? normale->prix : 0);
        continue;
      }
      if (normales > 0 && normale) ajouter(normale->designation, normale->fabricant, normale->reference, repere, normales, normale->prix);
      if (protections > 0 && pe) ajouter(pe->designation, pe->fabricant, pe->reference, repere, protections, pe->prix);
      continue;
    }
    const EntreeCatalogue* e = detail::catalogueDe(*c);
    const std::string fabricant = !c->fabricant.empty() ? c->fabricant : e ? e->fabricant : std::string();
    const std::string reference = !c->mpn.empty() ? c->mpn : e ? e->reference : std::string();
    // Le prix indicatif ne vaut que pour l'appareil générique : une référence de fabricant saisie attend son tarif.
    // Un appareil personnel garde le prix de sa définition tant qu'il garde sa référence.
    double indicatif = e && reference == e->reference ? e->prix : 0;
    if (!e && estAppareilPersonnel(c->symbole))
      if (const ecad::DefSymbole* d = detail::definitionDe(p, c->symbole); d && reference == d->mpn) indicatif = specAppareil(*d).prix;
    ajouter(designationDe(*c), fabricant, reference, repere, 1, indicatif);
  }
  for (auto& l : v) std::sort(l.reperes.begin(), l.reperes.end(), ordreNaturel);
  std::stable_sort(v.begin(), v.end(), [](const LigneNomenclatureElec& a, const LigneNomenclatureElec& b) {
    if (a.reperes.empty() || b.reperes.empty()) return !a.reperes.empty() && b.reperes.empty();
    return ordreNaturel(a.reperes.front(), b.reperes.front());
  });
  return v;
}

std::string clePrix(const std::string& designation, const std::string& fabricant, const std::string& reference) {
  return reference.empty() ? "des:" + designation : "ref:" + fabricant + "|" + reference;
}

void fixerPrix(ProjetElectrique& p, const std::string& cle, double prix) {
  if (cle.empty()) return;
  if (prix < 0 || !std::isfinite(prix)) p.prix.erase(cle);
  else p.prix[cle] = std::round(prix * 100) / 100;
}

ChiffrageNomenclature chiffrage(const std::vector<LigneNomenclatureElec>& v) {
  ChiffrageNomenclature c;
  for (const auto& l : v) {
    ++c.lignes;
    if (l.prixSaisi) ++c.saisies;
    else if (l.prixUnitaire > 0) ++c.indicatives;
    else ++c.sansPrix;
    c.total += l.montant();
  }
  return c;
}

std::vector<LigneBorne> planBorniers(const ProjetElectrique& p) {
  std::vector<LigneBorne> lignes;
  std::set<std::string> borniers;
  for (const auto& c : p.schema.composants)
    if (detail::estBornier(c)) borniers.insert(c.uuid);
  if (borniers.empty()) return lignes;
  // Copie où les deux côtés de chaque borne sont séparés : on voit ce qui arrive en haut et ce qui part en bas
  ecad::ConceptionElec c = p.schema;
  for (auto& f : c.feuilles)
    for (auto& s : f.symboles) {
      if (!borniers.count(s.composant)) continue;
      const DefSymbole* d = detail::definitionDe(p, s.symbole);
      if (!d || d->broches.size() != 2 || d->broches[0].numero != d->broches[1].numero) continue;
      const std::string id = s.symbole + SUFFIXE_COTE;
      if (!c.symboles.count(id)) {
        DefSymbole x = *d;
        x.id = id;
        x.broches[1].numero += SUFFIXE_COTE;
        c.symboles[id] = x;
      }
      s.numeros[d->broches[1].numero + SUFFIXE_COTE] = ecad::numeroBroche(s, d->broches[0].numero) + SUFFIXE_COTE;
      s.symbole = id;
    }
  const ecad::Connectivite kc = ecad::connectivite(c);
  const ecad::Connectivite kr = ecad::connectivite(p.schema);
  auto sansSuffixe = [](std::string n) {
    if (n.size() > 2 && n.compare(n.size() - 2, 2, SUFFIXE_COTE) == 0) n.resize(n.size() - 2);
    return n;
  };
  struct Cote {
    std::vector<std::string> reperes;
    bool exterieur = false;
  };
  auto cote = [&](const std::string& bornier, const std::string& numero) {
    Cote r;
    const int i = kc.reseau(bornier, numero);
    if (i < 0) return r;
    const ecad::Reseau& res = kc.reseaux[static_cast<std::size_t>(i)];
    for (const auto& b : res.broches) {
      if (b.composant == bornier && sansSuffixe(b.broche) == sansSuffixe(numero)) continue;
      r.reperes.push_back(detail::repereBroche(p, b.composant, sansSuffixe(b.broche)));
      if (const ComposantElec* x = p.schema.composant(b.composant); x && !detail::estBornier(*x) && encombrement(p, x->uuid).horsArmoire) r.exterieur = true;
    }
    std::sort(r.reperes.begin(), r.reperes.end(), ordreNaturel);
    r.reperes.erase(std::unique(r.reperes.begin(), r.reperes.end()), r.reperes.end());
    if (r.reperes.empty() && res.nomme) r.reperes.push_back(res.nom);
    return r;
  };
  auto joindre = [](const std::vector<std::string>& v) {
    std::string t;
    for (const auto& x : v) t += (t.empty() ? "" : ", ") + x;
    return t;
  };
  for (const ComposantElec* x : composantsTries(p)) {
    if (!borniers.count(x->uuid)) continue;
    const std::string repere = repereAffiche(p, *x);
    const auto bornes = bornesDe(p, x->uuid);
    for (const std::string& n : numerosTries(bornes)) {
      LigneBorne l;
      l.bornier = repere;
      l.borne = n;
      const auto& poses = bornes.at(n);
      for (const auto& q : poses) l.protection = l.protection || q.protection;
      Cote haut = cote(x->uuid, n), bas = cote(x->uuid, n + SUFFIXE_COTE);
      // Côté armoire en haut, côté machine en bas ; un côté qui mène à un appareil hors armoire est l'extérieur
      if (haut.exterieur && !bas.exterieur) std::swap(haut, bas);
      l.interieur = joindre(haut.reperes);
      l.exterieur = joindre(bas.reperes);
      const int i = kr.reseau(x->uuid, n);
      if (i >= 0) {
        const std::string& nom = kr.reseaux[static_cast<std::size_t>(i)].nom;
        l.fil = detail::nomAutomatique(nom) ? std::string() : nom;
      }
      if (!l.fil.empty())
        for (const auto& w : p.cables) {
          if (!w.de.empty() && w.de != repere && !w.vers.empty() && w.vers != repere) continue;
          for (const auto& a : w.affectation)
            if (a.fil == l.fil) {
              l.cable = w.reference + (a.repere.empty() ? "" : ":" + a.repere);
              break;
            }
          if (!l.cable.empty()) break;
        }
      const PoseBorne& premiere = poses.front();
      l.folio = position(p, premiere.feuille, premiere.p).texte();
      lignes.push_back(l);
    }
  }
  return lignes;
}

std::vector<LigneLiaison> carnetCablage(const ProjetElectrique& p) {
  // Les liaisons suivent le dessin : sur chaque folio, les bornes reliées par un même tracé de fils sont câblées de
  // proche en proche ; d'un tracé à l'autre du même potentiel (renvoi, autre folio), une liaison les rejoint.
  std::vector<LigneLiaison> v;
  const ecad::Connectivite k = ecad::connectivite(p.schema);
  struct Point {
    std::string composant, broche;
    ecad::Pt p;
    std::size_t feuille = 0;
    std::tuple<std::size_t, int, ecad::Nm, ecad::Nm> ordre;
  };
  struct Groupe {
    int reseau = -1;
    std::vector<Point> points;  // dans l'ordre du câblage
  };
  std::vector<Groupe> groupes;
  for (std::size_t f = 0; f < p.schema.feuilles.size(); ++f) {
    const ecad::Feuille& F = p.schema.feuilles[f];
    const std::size_t n = F.fils.size();
    std::vector<std::size_t> parent(n);
    for (std::size_t i = 0; i < n; ++i) parent[i] = i;
    std::function<std::size_t(std::size_t)> racine = [&](std::size_t x) { return parent[x] == x ? x : parent[x] = racine(parent[x]); };
    auto unir = [&](std::size_t a, std::size_t b) { parent[racine(a)] = racine(b); };
    auto touche = [](ecad::Pt q, const ecad::Fil& w) { return q == w.a || q == w.b || ecad::surSegment(q, w.a, w.b); };
    std::map<ecad::Pt, std::vector<std::size_t>> parBout;
    for (std::size_t i = 0; i < n; ++i) {
      parBout[F.fils[i].a].push_back(i);
      parBout[F.fils[i].b].push_back(i);
    }
    for (const auto& [q, liste] : parBout) {
      for (std::size_t i = 1; i < liste.size(); ++i) unir(liste[0], liste[i]);
      for (std::size_t j = 0; j < n; ++j)
        if (touche(q, F.fils[j])) unir(liste[0], j);
    }
    // Broches de ce folio, rattachées au tracé qu'elles touchent (ou à leur point, broche contre broche)
    std::map<std::string, std::vector<Point>> parTrace;
    for (const auto& s : F.symboles) {
      if (s.composant.empty()) continue;
      const DefSymbole* d = detail::definitionDe(p, s.symbole);
      if (!d) continue;
      for (const auto& bp : ecad::brochesPlacees(s, *d)) {
        std::string cle;
        for (std::size_t j = 0; j < n && cle.empty(); ++j)
          if (touche(bp.p, F.fils[j])) cle = "f" + std::to_string(racine(j));
        if (cle.empty()) cle = "p" + std::to_string(bp.p.x) + ":" + std::to_string(bp.p.y);
        const PositionFolio pos = position(p, f, bp.p);
        parTrace[cle].push_back({s.composant, bp.numero, bp.p, f, std::make_tuple(f, pos.colonne, bp.p.y, bp.p.x)});
      }
    }
    for (auto& [cle, points] : parTrace) {
      // Une même broche (borne vue des deux côtés) ne compte qu'une fois dans un tracé
      std::vector<Point> uniques;
      for (const auto& x : points)
        if (std::none_of(uniques.begin(), uniques.end(), [&](const Point& y) { return y.composant == x.composant && y.broche == x.broche; })) uniques.push_back(x);
      if (uniques.empty()) continue;
      // De proche en proche, en partant de la première borne dans l'ordre de lecture
      std::sort(uniques.begin(), uniques.end(), [](const Point& a, const Point& b) { return a.ordre < b.ordre; });
      std::vector<Point> chaine{uniques.front()};
      std::vector<bool> pris(uniques.size(), false);
      pris[0] = true;
      for (std::size_t etape = 1; etape < uniques.size(); ++etape) {
        std::size_t meilleur = 0;
        long double distance = -1;
        for (std::size_t i = 0; i < uniques.size(); ++i) {
          if (pris[i]) continue;
          const long double d = std::llabs(uniques[i].p.x - chaine.back().p.x) + std::llabs(uniques[i].p.y - chaine.back().p.y);
          if (distance < 0 || d < distance) {
            distance = d;
            meilleur = i;
          }
        }
        pris[meilleur] = true;
        chaine.push_back(uniques[meilleur]);
      }
      Groupe g;
      g.reseau = k.reseau(chaine.front().composant, chaine.front().broche);
      g.points = std::move(chaine);
      groupes.push_back(std::move(g));
    }
  }
  // Par potentiel, dans l'ordre de lecture de leur première borne
  std::map<int, std::vector<std::size_t>> parReseau;
  for (std::size_t i = 0; i < groupes.size(); ++i)
    if (groupes[i].reseau >= 0) parReseau[groupes[i].reseau].push_back(i);
  std::vector<std::pair<std::tuple<std::size_t, int, ecad::Nm, ecad::Nm>, int>> ordreReseaux;
  for (auto& [r, liste] : parReseau) {
    std::sort(liste.begin(), liste.end(), [&](std::size_t a, std::size_t b) { return groupes[a].points.front().ordre < groupes[b].points.front().ordre; });
    ordreReseaux.push_back({groupes[liste.front()].points.front().ordre, r});
  }
  std::sort(ordreReseaux.begin(), ordreReseaux.end());
  auto repere = [&](const Point& x) { return detail::repereBroche(p, x.composant, x.broche); };
  const EstimateurLongueurs longueurs(p);
  for (const auto& [o, r] : ordreReseaux) {
    const ecad::Reseau& res = k.reseaux[static_cast<std::size_t>(r)];
    const ProprietesConducteur pr = detail::proprietes(p, k, res.nom);
    const std::string fil = detail::nomAutomatique(res.nom) ? std::string() : res.nom;
    const std::string section = pr.section > 0 ? detail::nombreFr(pr.section) : std::string();
    auto liaison = [&](const Point& a, const Point& b) {
      if (a.composant == b.composant && a.broche == b.broche) return;
      LigneLiaison x;
      x.fil = fil;
      x.de = repere(a);
      x.vers = repere(b);
      x.section = section;
      x.couleur = pr.couleur;
      x.folio = position(p, a.feuille, a.p).texte();
      x.longueur = longueurs.longueur(a.composant, b.composant);
      x.type = pr.type;
      x.mm2 = pr.section;
      x.composantDe = a.composant;
      x.composantVers = b.composant;
      v.push_back(std::move(x));
    };
    const auto& liste = parReseau.at(r);
    // Tracés reliés entre eux par une borne commune : déjà raccordés ; les autres sont rejoints de proche en proche
    std::vector<std::size_t> composante(liste.size());
    for (std::size_t i = 0; i < liste.size(); ++i) composante[i] = i;
    std::function<std::size_t(std::size_t)> chef = [&](std::size_t x) { return composante[x] == x ? x : composante[x] = chef(composante[x]); };
    for (std::size_t i = 0; i < liste.size(); ++i)
      for (std::size_t j = i + 1; j < liste.size(); ++j) {
        bool commun = false;
        for (const auto& a : groupes[liste[i]].points)
          for (const auto& b : groupes[liste[j]].points) commun = commun || (a.composant == b.composant && a.broche == b.broche);
        if (commun) composante[chef(i)] = chef(j);
      }
    const Point* dernier = nullptr;
    std::set<std::size_t> vues;
    for (std::size_t i = 0; i < liste.size(); ++i) {
      const Groupe& g = groupes[liste[i]];
      for (std::size_t j = 1; j < g.points.size(); ++j) liaison(g.points[j - 1], g.points[j]);
      const std::size_t c = chef(i);
      if (!vues.count(c)) {
        if (dernier) liaison(*dernier, g.points.front());
        vues.insert(c);
      }
      dernier = &g.points.back();
    }
  }
  return v;
}

std::vector<LigneEntreeSortie> listeEntreesSorties(const ProjetElectrique& p) {
  std::vector<LigneEntreeSortie> v;
  const ecad::Connectivite k = ecad::connectivite(p.schema);
  const detail::LieuxBroches lieux = detail::lieuxBroches(p);
  auto estES = [](const ecad::Broche& b, char& sens) {
    const std::string& n = b.numero;
    if (b.nom.size() > 2 && b.nom[0] == '%' && (b.nom[1] == 'I' || b.nom[1] == 'Q')) {
      sens = b.nom[1] == 'I' ? 'E' : 'S';
      return true;
    }
    if (n.size() >= 4 && (n[0] == 'I' || n[0] == 'Q') && std::isdigit(static_cast<unsigned char>(n[1])) && n.find('.') != std::string::npos) {
      sens = n[0] == 'I' ? 'E' : 'S';
      return true;
    }
    return false;
  };
  for (const ComposantElec* c : composantsTries(p)) {
    const DefSymbole* d = detail::definitionDe(p, c->symbole);
    if (!d) continue;
    const std::string automate = repereAffiche(p, *c);
    for (const auto& b : d->broches) {
      char sens = 0;
      if (!estES(b, sens)) continue;
      LigneEntreeSortie l;
      l.automate = automate;
      l.adresse = b.nom.size() > 1 && b.nom[0] == '%' ? b.nom : "%" + b.numero;
      l.sens = std::string(1, sens);
      const int i = k.reseau(c->uuid, b.numero);
      if (i >= 0) {
        std::vector<std::string> reperes;
        std::string designation;
        for (const auto& x : k.reseaux[static_cast<std::size_t>(i)].broches) {
          if (x.composant == c->uuid) continue;
          const ComposantElec* y = p.schema.composant(x.composant);
          if (!y || detail::estBornier(*y)) continue;
          reperes.push_back(repereAffiche(p, *y));
          if (designation.empty()) {
            designation = detail::champ(*y, "fonction");
            if (designation.empty()) designation = designationDe(*y);
          }
        }
        std::sort(reperes.begin(), reperes.end(), ordreNaturel);
        reperes.erase(std::unique(reperes.begin(), reperes.end()), reperes.end());
        for (const auto& r : reperes) l.repere += (l.repere.empty() ? "" : ", ") + r;
        l.designation = designation;
      }
      if (const auto it = lieux.find({c->uuid, b.numero}); it != lieux.end()) l.folio = position(p, it->second.feuille, it->second.p).texte();
      v.push_back(l);
    }
  }
  return v;
}

std::vector<LigneFolio> listeFolios(const ProjetElectrique& p) {
  std::vector<LigneFolio> v;
  const ecad::Connectivite k = ecad::connectivite(p.schema);
  for (std::size_t f = 0; f < p.schema.feuilles.size(); ++f) {
    const ecad::Feuille& F = p.schema.feuilles[f];
    LigneFolio l;
    l.numero = static_cast<int>(f) + 1;
    l.titre = F.nom;
    l.type = nomTypeFolio(p.folio(F.uuid).type);
    std::set<std::string> appareils;
    for (const auto& s : F.symboles)
      if (!s.composant.empty()) appareils.insert(s.composant);
    l.appareils = static_cast<int>(appareils.size());
    std::set<int> reseaux;
    for (const auto& w : F.fils)
      if (const auto it = k.parFil.find(w.uuid); it != k.parFil.end()) reseaux.insert(it->second);
    l.fils = static_cast<int>(reseaux.size());
    v.push_back(l);
  }
  return v;
}

// ---------- Exports CSV ----------
std::string csvAppareils(const ProjetElectrique& p) {
  std::vector<std::vector<std::string>> l;
  for (const auto& a : listeAppareils(p)) l.push_back({a.repere, a.designation, a.fabricant, a.reference, a.fonction, a.emplacement, a.folios, std::to_string(a.quantite)});
  return detail::documentCsv({"Repère", "Désignation", "Fabricant", "Référence", "Fonction", "Emplacement", "Folios", "Quantité"}, l);
}

std::string csvNomenclatureElectrique(const ProjetElectrique& p) {
  std::vector<std::vector<std::string>> l;
  for (const auto& n : nomenclatureElectrique(p)) {
    std::string reperes;
    for (const auto& r : n.reperes) reperes += (reperes.empty() ? "" : ", ") + r;
    const std::string pu = n.prixUnitaire > 0 || n.prixSaisi ? detail::nombreFr(n.prixUnitaire, 2) : std::string();
    const std::string montant = n.prixUnitaire > 0 || n.prixSaisi ? detail::nombreFr(n.montant(), 2) : std::string();
    l.push_back({std::to_string(n.quantite), reperes, n.designation, n.fabricant, n.reference, pu, montant, n.prixSaisi ? "saisi" : n.prixUnitaire > 0 ? "indicatif" : ""});
  }
  const ChiffrageNomenclature c = chiffrage(nomenclatureElectrique(p));
  l.push_back({"", "", "Total HT", "", "", "", detail::nombreFr(c.total, 2), c.sansPrix > 0 ? std::to_string(c.sansPrix) + " ligne(s) sans prix" : ""});
  return detail::documentCsv({"Quantité", "Repères", "Désignation", "Fabricant", "Référence", "Prix unitaire HT (€)", "Montant HT (€)", "Prix"}, l);
}

std::string csvBorniers(const ProjetElectrique& p) {
  std::vector<std::vector<std::string>> l;
  for (const auto& b : planBorniers(p)) l.push_back({b.bornier, b.borne, b.interieur, b.exterieur, b.fil, b.cable, b.folio, b.protection ? "PE" : ""});
  return detail::documentCsv({"Bornier", "Borne", "Côté armoire", "Côté extérieur", "Fil", "Câble", "Folio", "Protection"}, l);
}

std::string csvCables(const ProjetElectrique& p) {
  std::vector<std::vector<std::string>> l;
  std::vector<const Cable*> cables;
  for (const auto& c : p.cables) cables.push_back(&c);
  std::stable_sort(cables.begin(), cables.end(), [](const Cable* a, const Cable* b) { return ordreNaturel(a->reference, b->reference); });
  for (const Cable* c : cables) {
    const std::string type = c->designation + " " + std::to_string(c->conducteurs) + "G" + detail::nombreFr(c->section);
    const std::string longueur = c->longueur > 0 ? detail::nombreFr(c->longueur) : std::string();
    std::vector<ConducteurCable> conducteurs = c->affectation;
    if (conducteurs.empty())
      for (const auto& r : reperesConducteurs(c->conducteurs, true, false)) conducteurs.push_back({r, {}});
    for (const auto& x : conducteurs) l.push_back({c->reference, type, c->de, c->vers, longueur, x.repere, x.fil.empty() ? "réserve" : x.fil});
  }
  return detail::documentCsv({"Câble", "Type", "De", "Vers", "Longueur (m)", "Conducteur", "Fil"}, l);
}

std::string csvCarnetCablage(const ProjetElectrique& p) {
  std::vector<std::vector<std::string>> l;
  for (const auto& x : carnetCablage(p))
    l.push_back({x.fil, x.de, x.vers, x.section, x.couleur, x.longueur > 0 ? detail::nombreFr(x.longueur, 0) : std::string(), x.folio});
  return detail::documentCsv({"Fil", "De", "Vers", "Section (mm²)", "Couleur", "Longueur (mm)", "Folio"}, l);
}

std::string csvEntreesSorties(const ProjetElectrique& p) {
  std::vector<std::vector<std::string>> l;
  for (const auto& x : listeEntreesSorties(p)) l.push_back({x.automate, x.adresse, x.sens == "E" ? "Entrée" : "Sortie", x.repere, x.designation, x.folio});
  return detail::documentCsv({"Automate", "Adresse", "Sens", "Repère", "Désignation", "Folio"}, l);
}

std::string csvFolios(const ProjetElectrique& p) {
  std::vector<std::vector<std::string>> l;
  for (const auto& f : listeFolios(p)) l.push_back({std::to_string(f.numero), f.titre, f.type, std::to_string(f.appareils), std::to_string(f.fils)});
  return detail::documentCsv({"Folio", "Titre", "Type", "Appareils", "Potentiels"}, l);
}

std::string csvBilanPuissance(const ProjetElectrique& p, double cosPhi) {
  const BilanPuissance b = bilanPuissance(p, cosPhi);
  std::vector<std::vector<std::string>> l;
  for (const auto& x : b.lignes)
    l.push_back({x.repere, x.designation, detail::nombreFr(x.puissance), detail::nombreFr(x.ku), detail::nombreFr(x.ks), detail::nombreFr(x.absorbee(), 3)});
  l.push_back({"Total", "", detail::nombreFr(b.installee), "", "", detail::nombreFr(b.absorbee, 3)});
  l.push_back({"Courant", "", "", "", "", detail::nombreFr(b.courant, 1) + " A"});
  return detail::documentCsv({"Repère", "Désignation", "Puissance (kW)", "ku", "ks", "Absorbée (kW)"}, l);
}

void dimensionnerCable(Cable& c, int conducteurs, bool protection) {
  c.conducteurs = std::clamp(conducteurs, 1, 61);
  const std::vector<std::string> reperes = reperesConducteurs(c.conducteurs, protection, c.conducteurs > 5);
  std::vector<std::string> fils;
  std::string pe;
  for (const auto& a : c.affectation) {
    if (a.fil.empty()) continue;
    if (classeConducteur(a.fil) == ClasseConducteur::Protection) pe = a.fil;
    else fils.push_back(a.fil);
  }
  c.affectation.clear();
  std::size_t k = 0;
  for (const auto& r : reperes) {
    std::string fil;
    if (r == "GNYE") fil = pe;
    else if (k < fils.size()) fil = fils[k++];
    c.affectation.push_back({r, fil});
  }
}

int affecterCable(ProjetElectrique& p, std::size_t indice) {
  if (indice >= p.cables.size()) return 0;
  Cable& c = p.cables[indice];
  const ecad::Connectivite k = ecad::connectivite(p.schema);
  auto appareil = [&](const std::string& repere) -> const ComposantElec* {
    std::string r = repere;
    while (!r.empty() && r.front() == '-') r.erase(r.begin());
    return r.empty() ? nullptr : p.schema.parReference(r);
  };
  const ComposantElec* a = appareil(c.de);
  const ComposantElec* b = appareil(c.vers);
  if (!a || !b) return 0;
  std::vector<std::string> fils;
  std::string pe;
  for (const auto& r : k.reseaux) {
    bool chezA = false, chezB = false;
    for (const auto& x : r.broches) {
      chezA = chezA || x.composant == a->uuid;
      chezB = chezB || x.composant == b->uuid;
    }
    if (!chezA || !chezB) continue;
    if (classeConducteur(r.nom) == ClasseConducteur::Protection) pe = r.nom;
    else fils.push_back(r.nom);
  }
  std::sort(fils.begin(), fils.end(), ordreNaturel);
  const int besoin = static_cast<int>(fils.size()) + (pe.empty() ? 0 : 1);
  if (besoin == 0) return 0;
  if (c.conducteurs < besoin) c.conducteurs = besoin;
  c.affectation.clear();
  for (const auto& f : fils) c.affectation.push_back({{}, f});
  if (!pe.empty()) c.affectation.push_back({{}, pe});
  // Repères : couleurs normalisées jusqu'à 5 conducteurs, numéros au-delà ; le PE sur le vert-jaune
  const std::vector<std::string> reperes = reperesConducteurs(c.conducteurs, !pe.empty(), c.conducteurs > 5);
  std::vector<ConducteurCable> v;
  std::size_t i = 0;
  for (const auto& r : reperes) {
    if (r == "GNYE") v.push_back({r, pe});
    else v.push_back({r, i < fils.size() ? fils[i++] : std::string()});
  }
  c.affectation = v;
  return besoin;
}

}  // namespace prom::elec

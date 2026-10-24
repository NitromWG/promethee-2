// Prométhée : synchronisation continue du schéma et du circuit (lot L3, tâches 3.6, 3.7, 3.18, 3.19). Voir
// synchronisation.hpp.
// Licence GPL-3.0-only.
#include "promethee/synchronisation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

#include "promethee/commandes.hpp"
#include "promethee/formules.hpp"
#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
// Composants présents au schéma : un symbole posé, ou aucun symbole du tout (composant de la seule carte, comme un
// trou de fixation importé de KiCad sans schéma : le schéma ne l'a pas supprimé)
std::set<std::string> composantsPoses(const ConceptionElec& c) {
  std::set<std::string> r;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (!s.composant.empty()) r.insert(s.composant);
  // Composant de la carte seule (5.17) : posé sans symbole, la synchronisation le garde avec ses réseaux
  for (const auto& x : c.composants)
    if (x.symbole.empty() || x.carteSeule) r.insert(x.uuid);
  return r;
}

std::string pluriel(int n, const char* un, const char* plusieurs) { return std::to_string(n) + " " + (n > 1 ? plusieurs : un); }

std::string cuivreEnClair(int pistes, int vias) {
  std::string r;
  if (pistes) r = pluriel(pistes, "piste", "pistes");
  if (vias) r += (r.empty() ? "" : " et ") + pluriel(vias, "via", "vias");
  return r;
}

// Cuivre qui touche l'une des pastilles d'une empreinte placée (sans doublon)
CuivreTouchant cuivreSurEmpreinte(const ConceptionElec& c, const EmpreintePlacee& e) {
  CuivreTouchant r;
  const DefEmpreinte* d = c.empreinte(e.empreinte);
  if (!d) return r;
  std::set<std::size_t> pistes, vias;
  for (const auto& p : d->pastilles) {
    if (p.type == TypePastille::NonMetallisee) continue;
    const CuivreTouchant t = cuivreSurPastille(c, e, p);
    pistes.insert(t.pistes.begin(), t.pistes.end());
    vias.insert(t.vias.begin(), t.vias.end());
  }
  r.pistes.assign(pistes.begin(), pistes.end());
  r.vias.assign(vias.begin(), vias.end());
  return r;
}

const EmpreintePlacee* placee(const ConceptionElec& c, const std::string& composant) {
  for (const auto& e : c.circuit.empreintes)
    if (e.composant == composant) return &e;
  return nullptr;
}

// Pose d'une empreinte « à placer » : à droite de la carte, sous celles qui attendent déjà, par rangées de 60 mm
void poserAPlacer(ConceptionElec& c, const ComposantElec& k) {
  const DefEmpreinte* d = c.empreinte(k.empreinte);
  if (!d) return;
  const Boite carte = boite(c.circuit.contour.polygone());
  const Nm gauche = carte.x1 + 8 * NM_PAR_MM, largeurRangement = 60 * NM_PAR_MM, ecart = 2 * NM_PAR_MM;
  const Nm l = d->encombrement.vide() ? 5 * NM_PAR_MM : d->encombrement.largeur(), h = d->encombrement.vide() ? 5 * NM_PAR_MM : d->encombrement.hauteur();
  // Dernière rangée occupée par les empreintes en attente
  Nm y = carte.y0, x = gauche, hauteurLigne = 0;
  std::vector<Boite> attente;
  for (const auto& e : c.circuit.empreintes)
    if (aPlacer(c, e))
      if (const DefEmpreinte* de = c.empreinte(e.empreinte)) attente.push_back(boite(encombrementSurCarte(e, *de)));
  if (!attente.empty()) {
    Nm basDerniere = carte.y0;
    for (const auto& b : attente) basDerniere = std::max(basDerniere, b.y0);
    y = basDerniere;
    for (const auto& b : attente)
      if (b.y0 >= basDerniere - ecart) {
        x = std::max(x, b.x1 + ecart);
        hauteurLigne = std::max(hauteurLigne, b.hauteur());
      }
  }
  if (x + l > gauche + largeurRangement && x > gauche) {
    x = gauche;
    y += hauteurLigne + ecart;
  }
  EmpreintePlacee e;
  e.composant = k.uuid;
  e.empreinte = k.empreinte;
  const Pt centre = d->encombrement.vide() ? Pt{} : d->encombrement.centre();
  e.position = surGrille({x + l / 2 - centre.x, y + h / 2 - centre.y}, 50'000);
  c.circuit.empreintes.push_back(e);
}

// Groupe de cuivre relié à des pistes et vias de départ, sur un réseau donné : bouts communs, via sous un bout
void etendreGroupe(const ConceptionElec& c, const std::string& reseau, std::set<std::size_t>& pistes, std::set<std::size_t>& vias) {
  const auto& P = c.circuit.pistes;
  const auto& V = c.circuit.vias;
  auto touche = [](Pt a, Pt b, Nm tolerance) { return distance(a, b) <= static_cast<double>(tolerance); };
  for (bool encore = true; encore;) {
    encore = false;
    for (std::size_t i = 0; i < P.size(); ++i) {
      if (pistes.count(i) || P[i].reseau != reseau) continue;
      bool relie = false;
      for (std::size_t j : pistes)
        if (P[j].couche == P[i].couche)
          for (Pt a : {P[i].a, P[i].b})
            for (Pt b : {P[j].a, P[j].b}) relie |= touche(a, b, std::max(P[i].largeur, P[j].largeur) / 2);
      for (std::size_t v : vias)
        for (Pt a : {P[i].a, P[i].b}) relie |= touche(a, V[v].p, V[v].diametre / 2);
      if (relie) {
        pistes.insert(i);
        encore = true;
      }
    }
    for (std::size_t v = 0; v < V.size(); ++v) {
      if (vias.count(v) || V[v].reseau != reseau) continue;
      bool relie = false;
      for (std::size_t j : pistes)
        for (Pt a : {P[j].a, P[j].b}) relie |= touche(a, V[v].p, V[v].diametre / 2);
      if (relie) {
        vias.insert(v);
        encore = true;
      }
    }
  }
}

// Pastilles (composant, numéro) que touche un groupe de cuivre
std::set<std::pair<std::string, std::string>> pastillesDuGroupe(const ConceptionElec& c, const std::set<std::size_t>& pistes, const std::set<std::size_t>& vias) {
  std::set<std::pair<std::string, std::string>> r;
  for (const auto& e : c.circuit.empreintes) {
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    if (!d) continue;
    for (const auto& p : d->pastilles) {
      if (p.numero.empty() || p.type == TypePastille::NonMetallisee) continue;
      const CuivreTouchant t = cuivreSurPastille(c, e, p);
      const bool touche = std::any_of(t.pistes.begin(), t.pistes.end(), [&](std::size_t i) { return pistes.count(i) > 0; }) ||
                          std::any_of(t.vias.begin(), t.vias.end(), [&](std::size_t i) { return vias.count(i) > 0; });
      if (touche) r.insert({e.composant, p.numero});
    }
  }
  return r;
}

// Point de connexion d'une broche au schéma (première pose qui la porte) et sa feuille
bool pointDeBroche(const ConceptionElec& c, const std::string& composant, const std::string& numero, std::size_t& feuille, Pt& p) {
  for (std::size_t f = 0; f < c.feuilles.size(); ++f)
    for (const auto& s : c.feuilles[f].symboles) {
      if (s.composant != composant) continue;
      const DefSymbole* d = c.symbole(s.symbole);
      if (!d) continue;
      for (const auto& b : brochesPlacees(s, *d))
        if (b.numero == numero) {
          feuille = f;
          p = b.p;
          return true;
        }
    }
  return false;
}
std::string nomDeBroche(const ConceptionElec& c, const std::string& composant, const std::string& numero) {
  const ComposantElec* x = c.composant(composant);
  const DefSymbole* d = x ? c.symbole(x->symbole) : nullptr;
  if (!d) return {};
  for (const auto& b : d->broches)
    if (b.numero == numero && !b.nom.empty() && b.nom != "~" && b.nom.find_first_not_of("0123456789") != std::string::npos) return b.nom;
  return {};
}

// Étiquette posée sur une broche (une locale ; une globale si la liaison passe d'une feuille à l'autre)
void poserEtiquette(ConceptionElec& c, std::size_t feuille, Pt p, const std::string& nom, bool globale) {
  for (const auto& e : c.feuilles[feuille].etiquettes)
    if (e.p == p && e.texte == nom) return;
  Etiquette e;
  e.uuid = nouvelUuid();
  e.texte = nom;
  e.p = p;
  e.type = globale ? TypeEtiquette::Globale : TypeEtiquette::Locale;
  c.feuilles[feuille].etiquettes.push_back(e);
}
}  // namespace

const char* nomGenreEcart(GenreEcart g) {
  switch (g) {
    case GenreEcart::Nouveau: return "Composant nouveau";
    case GenreEcart::SupprimeLibre: return "Composant supprimé";
    case GenreEcart::SupprimeRoute: return "Composant supprimé, routé";
    case GenreEcart::EmpreinteRemplacee: return "Empreinte changée";
    case GenreEcart::PastillePerdue: return "Empreinte changée, pastille routée perdue";
    case GenreEcart::ReseauRenomme: return "Réseau renommé";
    case GenreEcart::BrocheRoutee: return "Broche passée à un autre réseau";
    case GenreEcart::HorsCarteLibre: return "Mis hors carte";
    case GenreEcart::HorsCarteRoute: return "Mis hors carte, routé";
    case GenreEcart::HorsCarteDecoche: return "Remis sur la carte";
    case GenreEcart::Verrouillee: return "Empreinte verrouillée changée";
    case GenreEcart::LiaisonCircuit: return "Liaison tracée au circuit";
  }
  return "Écart";
}

std::string EcartSynchro::cle() const { return std::to_string(static_cast<int>(genre)) + "|" + composant + "|" + broche + "|" + avant; }

CuivreTouchant cuivreSurPastille(const ConceptionElec& c, const EmpreintePlacee& e, const Pastille& p) {
  CuivreTouchant r;
  const Pastille q = pastilleSurCarte(e, p);
  const Polygone f = formePastille(q);
  const Boite bf = boite(f);
  const int n = c.circuit.empilage.couchesCuivre;
  const Couches couches = couchesPastille(p, e.dessous, n);
  for (std::size_t i = 0; i < c.circuit.pistes.size(); ++i) {
    const Piste& t = c.circuit.pistes[i];
    if (!(couches & bit(t.couche))) continue;
    Boite b;
    b.etendre(t.a);
    b.etendre(t.b);
    if (!b.gonflee(t.largeur).touche(bf)) continue;
    if (distancePolygoneSegment(f, t.a, t.b) <= static_cast<double>(t.largeur) / 2) r.pistes.push_back(i);
  }
  if (couches & tousLesCuivres(n))
    for (std::size_t i = 0; i < c.circuit.vias.size(); ++i) {
      const Via& v = c.circuit.vias[i];
      if (distancePolygoneSegment(f, v.p, v.p) <= static_cast<double>(v.diametre) / 2) r.vias.push_back(i);
    }
  return r;
}

int retirerComposantsSansSymbole(ConceptionElec& c, const std::set<std::string>& composants) {
  exigerModifiable(c);
  const std::set<std::string> poses = composantsPoses(c);
  int n = 0;
  for (const auto& id : composants) {
    if (poses.count(id)) continue;
    if (const EmpreintePlacee* e = placee(c, id); e && cuivreSurEmpreinte(c, *e).nombre() > 0) continue;
    const std::size_t avant = c.composants.size();
    std::erase_if(c.composants, [&](const ComposantElec& x) { return x.uuid == id; });
    n += static_cast<int>(avant - c.composants.size());
  }
  return n;
}

bool aPlacer(const ConceptionElec& c, const EmpreintePlacee& e) {
  const DefEmpreinte* d = c.empreinte(e.empreinte);
  const Pt p = versCarte(e, d ? pointDePlacement(*d) : Pt{});
  return !c.circuit.contour.contient(p);
}

std::vector<EcartSynchro> ecartsSynchronisation(const ConceptionElec& c) { return ecartsSynchronisation(c, connectivite(c)); }

std::vector<EcartSynchro> ecartsSynchronisation(const ConceptionElec& c, const Connectivite& k) {
  std::vector<EcartSynchro> r;
  if (c.verrouillee) return r;  // 17 : une conception publiée ne bouge pas
  const std::set<std::string> poses = composantsPoses(c);
  auto nombres = [&](const CuivreTouchant& t) { return cuivreEnClair(static_cast<int>(t.pistes.size()), static_cast<int>(t.vias.size())); };
  // 8 : réseaux renommés sans changer qui est relié à qui. Le cuivre porte un nom qui n'existe plus au schéma (ou un
  // ancien nom figé), et toutes les pastilles qu'il touche sont sur un même réseau d'aujourd'hui.
  std::map<std::string, std::string> renommes;
  {
    std::map<std::string, std::set<std::string>> attendusParNom;  // nom porté par le cuivre → réseaux des pastilles touchées
    for (const auto& e : c.circuit.empreintes) {
      const DefEmpreinte* d = c.empreinte(e.empreinte);
      if (!d || !poses.count(e.composant)) continue;
      for (const auto& p : d->pastilles) {
        if (p.numero.empty() || p.type == TypePastille::NonMetallisee) continue;
        const std::string attendu = k.nom(e.composant, p.numero);
        const CuivreTouchant t = cuivreSurPastille(c, e, p);
        for (std::size_t i : t.pistes) attendusParNom[c.circuit.pistes[i].reseau].insert(attendu);
        for (std::size_t i : t.vias) attendusParNom[c.circuit.vias[i].reseau].insert(attendu);
      }
    }
    auto noter = [&](const std::string& n) {
      if (n.empty() || renommes.count(n)) return;
      const std::string canon = k.canonique(n);
      if (canon != n) {
        renommes[n] = canon;  // ancien nom figé, alias du réseau actuel
        return;
      }
      if (k.indice(n) >= 0) return;  // le réseau existe toujours
      const auto it = attendusParNom.find(n);
      if (it != attendusParNom.end() && it->second.size() == 1 && !it->second.begin()->empty()) renommes[n] = *it->second.begin();
    };
    for (const auto& t : c.circuit.pistes) noter(t.reseau);
    for (const auto& v : c.circuit.vias) noter(v.reseau);
    for (const auto& z : c.circuit.zones) noter(z.reseau);
  }
  for (const auto& x : c.composants) {
    if (!poses.count(x.uuid)) continue;
    const EmpreintePlacee* e = placee(c, x.uuid);
    const DefEmpreinte* attendue = x.empreinte.empty() ? nullptr : c.empreinte(x.empreinte);
    EcartSynchro ec;
    ec.composant = x.uuid;
    ec.reference = x.reference;
    if (!e) {
      if (x.horsCarte || !attendue) continue;  // 15 : rien à créer, l'ERC en parle
      ec.genre = GenreEcart::Nouveau;
      ec.sansRisque = true;
      ec.apres = x.empreinte;
      ec.description = x.reference + " : empreinte " + x.empreinte + " à placer";
      r.push_back(ec);
      continue;
    }
    if (x.horsCarte) {
      const CuivreTouchant t = cuivreSurEmpreinte(c, *e);
      ec.cuivre = t.nombre();
      ec.genre = ec.cuivre ? GenreEcart::HorsCarteRoute : GenreEcart::HorsCarteLibre;
      ec.sansRisque = ec.cuivre == 0;
      ec.avant = e->empreinte;
      ec.description = x.reference + " mis hors carte" + (ec.cuivre ? " · " + nombres(t) + " touchent ses pastilles" : std::string(" : empreinte retirée"));
      r.push_back(ec);
      continue;
    }
    if (!attendue) continue;  // 15 : empreinte inconnue ou effacée, l'ancienne reste
    const DefEmpreinte* actuelle = c.empreinte(e->empreinte);
    // Une copie locale retouchée sur la carte (7.24) dérive de l'empreinte du schéma : pas d'écart
    if (e->empreinte != x.empreinte && e->copieLocale != x.empreinte) {
      ec.avant = e->empreinte;
      ec.apres = x.empreinte;
      std::vector<std::string> perdues;
      int pistesPerdues = 0;
      if (actuelle)
        for (const auto& p : actuelle->pastilles) {
          if (p.numero.empty() || p.type == TypePastille::NonMetallisee) continue;
          const CuivreTouchant t = cuivreSurPastille(c, *e, p);
          ec.cuivre += t.nombre();
          const bool pendant = std::any_of(attendue->pastilles.begin(), attendue->pastilles.end(), [&](const Pastille& q) { return q.numero == p.numero; });
          if (t.nombre() && !pendant && std::find(perdues.begin(), perdues.end(), p.numero) == perdues.end()) {
            perdues.push_back(p.numero);
            pistesPerdues += t.nombre();
          }
        }
      if (e->verrou) {
        ec.genre = GenreEcart::Verrouillee;
        ec.description = x.reference + " : empreinte verrouillée sur la carte, " + e->empreinte + " → " + x.empreinte + " au schéma";
      } else if (!perdues.empty()) {
        ec.genre = GenreEcart::PastillePerdue;
        ec.broche = perdues.front();
        std::string liste;
        for (const auto& p : perdues) liste += (liste.empty() ? "" : ", ") + p;
        ec.description = x.reference + " : " + (perdues.size() > 1 ? "les pastilles " + liste + " n'existent" : "la pastille " + liste + " n'existe") +
                         " pas dans la nouvelle empreinte · " + cuivreEnClair(pistesPerdues, 0);
      } else {
        ec.genre = GenreEcart::EmpreinteRemplacee;
        ec.sansRisque = true;
        ec.description = "Empreinte de " + x.reference + " : " + e->empreinte + " → " + x.empreinte;
      }
      r.push_back(ec);
      continue;
    }
    // 10 : broche passée à un autre réseau alors que du cuivre porte l'ancien
    if (actuelle)
      for (const auto& p : actuelle->pastilles) {
        if (p.numero.empty() || p.type == TypePastille::NonMetallisee) continue;
        const std::string attendu = k.nom(x.uuid, p.numero);
        if (attendu.empty()) continue;
        const CuivreTouchant t = cuivreSurPastille(c, *e, p);
        std::map<std::string, int> anciens;
        auto autre = [&](const std::string& n) {
          if (n.empty() || k.canonique(n) == attendu) return false;
          const auto it = renommes.find(n);
          return it == renommes.end() || it->second != attendu;  // un réseau renommé n'est pas un changement de liaison
        };
        for (std::size_t i : t.pistes)
          if (const auto& n = c.circuit.pistes[i].reseau; autre(n)) ++anciens[n];
        for (std::size_t i : t.vias)
          if (const auto& n = c.circuit.vias[i].reseau; autre(n)) ++anciens[n];
        if (anciens.empty()) continue;
        EcartSynchro b = ec;
        b.genre = GenreEcart::BrocheRoutee;
        b.broche = p.numero;
        b.avant = anciens.begin()->first;
        b.apres = attendu;
        b.cuivre = anciens.begin()->second;
        b.description = x.reference + "." + p.numero + " passe de " + b.avant + " à " + attendu + " · " + pluriel(b.cuivre, "objet de cuivre porte", "objets de cuivre portent") +
                        " l'ancien réseau";
        if (std::none_of(r.begin(), r.end(), [&](const EcartSynchro& y) { return y.cle() == b.cle(); })) r.push_back(b);
      }
  }
  // Empreintes dont le composant n'est plus au schéma
  for (const auto& e : c.circuit.empreintes) {
    const ComposantElec* x = c.composant(e.composant);
    if (x && poses.count(x->uuid)) continue;
    EcartSynchro ec;
    ec.composant = e.composant;
    ec.reference = x ? x->reference : std::string();
    ec.avant = e.empreinte;
    const CuivreTouchant t = cuivreSurEmpreinte(c, e);
    ec.cuivre = t.nombre();
    ec.genre = ec.cuivre ? GenreEcart::SupprimeRoute : GenreEcart::SupprimeLibre;
    ec.sansRisque = ec.cuivre == 0;
    const std::string qui = ec.reference.empty() ? "L'empreinte " + e.empreinte + " (composant supprimé)" : ec.reference + " supprimé du schéma";
    ec.description = qui + (ec.cuivre ? " · " + nombres(t) + " touchent ses pastilles" : std::string(" : empreinte retirée"));
    r.push_back(ec);
  }
  // 8 : réseaux renommés, mêmes liaisons : le cuivre porte encore l'ancien nom
  for (const auto& [ancien, nouveau] : renommes) {
    EcartSynchro ec;
    ec.genre = GenreEcart::ReseauRenomme;
    ec.sansRisque = true;
    ec.avant = ancien;
    ec.apres = nouveau;
    ec.description = "Réseau renommé : " + ancien + " → " + nouveau + " · cuivre renommé";
    r.push_back(ec);
  }
  // ROU-34 : piste sans réseau tracée entre des pastilles que le schéma ne relie pas
  {
    std::set<std::size_t> vus;
    for (std::size_t i = 0; i < c.circuit.pistes.size(); ++i) {
      if (vus.count(i) || !c.circuit.pistes[i].reseau.empty()) continue;
      std::set<std::size_t> pistes{i}, vias;
      etendreGroupe(c, "", pistes, vias);
      vus.insert(pistes.begin(), pistes.end());
      const auto pp = pastillesDuGroupe(c, pistes, vias);
      std::set<std::string> reseaux;
      bool seules = true;
      for (const auto& [comp, num] : pp) {
        const int ri = k.reseau(comp, num);
        reseaux.insert(k.nom(comp, num));
        if (ri >= 0 && k.reseaux[static_cast<std::size_t>(ri)].broches.size() > 1) seules = false;
      }
      if (pp.size() < 2 || reseaux.size() < 2 || !seules) continue;
      EcartSynchro ec;
      ec.genre = GenreEcart::LiaisonCircuit;
      ec.cuivre = static_cast<int>(pistes.size() + vias.size());
      std::string liste;
      for (const auto& [comp, num] : pp) {
        const ComposantElec* x = c.composant(comp);
        const std::string ref = x ? x->reference : std::string("?");
        ec.pastilles.push_back(ref + ":" + num);
        liste += (liste.empty() ? "" : ", ") + ref + "." + num;
        if (ec.composant.empty()) {
          ec.composant = comp;
          ec.reference = ref;
        }
      }
      // Nom proposé : celui d'une broche nommée, sinon le nom automatique de la première
      for (const auto& [comp, num] : pp)
        if (ec.apres.empty()) ec.apres = nomDeBroche(c, comp, num);
      if (ec.apres.empty()) ec.apres = "N-" + ec.reference + "-" + pp.begin()->second;
      ec.broche = std::to_string(i);  // première piste du groupe : identité de l'écart
      ec.description = "Liaison tracée au circuit entre " + liste + " · réseau « " + ec.apres + " » proposé au schéma";
      r.push_back(ec);
    }
  }
  return r;
}

bool remplacerEmpreinteEnPlace(ConceptionElec& c, const std::string& composant, const std::string& nouvelle) {
  auto it = std::find_if(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [&](const EmpreintePlacee& e) { return e.composant == composant; });
  if (it == c.circuit.empreintes.end()) return false;
  const DefEmpreinte* pn = c.empreinte(nouvelle);
  if (!pn) return false;
  const DefEmpreinte neuve = *pn;
  const DefEmpreinte* pa = c.empreinte(it->empreinte);
  const DefEmpreinte ancienne = pa ? *pa : DefEmpreinte{};
  const EmpreintePlacee avant = *it;
  const int n = c.circuit.empilage.couchesCuivre;
  // Bouts de piste et vias accrochés aux anciennes pastilles, par broche
  struct Attache {
    bool via = false;
    std::size_t i = 0;
    int bout = 0;  // 0 : a, 1 : b
    std::string numero;
  };
  std::vector<Attache> attaches;
  std::set<std::tuple<bool, std::size_t, int>> vus;
  for (const auto& p : ancienne.pastilles) {
    if (p.numero.empty() || p.type == TypePastille::NonMetallisee) continue;
    const Pastille q = pastilleSurCarte(avant, p);
    const Polygone f = formePastille(q);
    const Couches couches = couchesPastille(p, avant.dessous, n);
    for (std::size_t i = 0; i < c.circuit.pistes.size(); ++i) {
      const Piste& t = c.circuit.pistes[i];
      if (!(couches & bit(t.couche))) continue;
      for (int bout = 0; bout < 2; ++bout) {
        const Pt pt = bout ? t.b : t.a;
        if (distancePolygoneSegment(f, pt, pt) <= static_cast<double>(t.largeur) / 2 && vus.insert({false, i, bout}).second) attaches.push_back({false, i, bout, p.numero});
      }
    }
    if (couches & tousLesCuivres(n))
      for (std::size_t i = 0; i < c.circuit.vias.size(); ++i)
        if (distancePolygoneSegment(f, c.circuit.vias[i].p, c.circuit.vias[i].p) <= static_cast<double>(c.circuit.vias[i].diametre) / 2 && vus.insert({true, i, 0}).second)
          attaches.push_back({true, i, 0, p.numero});
  }
  // Même point de placement (centre des pastilles), même rotation, même face, même verrou
  const Pt point = versCarte(avant, pointDePlacement(ancienne));
  EmpreintePlacee& e = *it;
  e.empreinte = nouvelle;
  EmpreintePlacee z = e;
  z.position = {};
  e.position = point - versCarte(z, pointDePlacement(neuve));
  // Chaque attache rejoint la nouvelle pastille de sa broche
  for (const Attache& a : attaches) {
    Pt& pt = a.via ? c.circuit.vias[a.i].p : (a.bout ? c.circuit.pistes[a.i].b : c.circuit.pistes[a.i].a);
    const Nm tol = a.via ? c.circuit.vias[a.i].diametre / 2 : c.circuit.pistes[a.i].largeur / 2;
    bool dedans = false;
    double meilleure = std::numeric_limits<double>::max();
    Pt cible = pt;
    for (const auto& p : neuve.pastilles) {
      if (p.numero != a.numero) continue;
      const Pastille q = pastilleSurCarte(e, p);
      if (distancePolygoneSegment(formePastille(q), pt, pt) <= 0) {
        dedans = true;
        break;
      }
      const double d = distance(pt, q.position);
      if (d < meilleure) {
        meilleure = d;
        cible = q.position;
      }
    }
    (void)tol;
    if (!dedans && meilleure < std::numeric_limits<double>::max()) pt = cible;
  }
  figerEmpreintes(c);
  return true;
}

int appliquerEcarts(ConceptionElec& c, const std::vector<EcartSynchro>& ecarts) {
  exigerModifiable(c);
  int n = 0;
  bool renommage = false;
  auto retirer = [&](const std::string& composant) {
    const std::size_t avant = c.circuit.empreintes.size();
    std::erase_if(c.circuit.empreintes, [&](const EmpreintePlacee& e) { return e.composant == composant; });
    return c.circuit.empreintes.size() != avant;
  };
  for (const EcartSynchro& ec : ecarts) {
    switch (ec.genre) {
      case GenreEcart::Nouveau:
      case GenreEcart::HorsCarteDecoche:
        if (const ComposantElec* x = c.composant(ec.composant); x && !placee(c, x->uuid) && !x->horsCarte && c.empreinte(x->empreinte)) {
          poserAPlacer(c, *x);
          ++n;
        }
        break;
      case GenreEcart::SupprimeLibre:
      case GenreEcart::SupprimeRoute:
        if (retirer(ec.composant)) ++n;  // les pistes restent, en bouts libres
        // Le composant gardé pour nommer l'écart part avec son empreinte
        if (!composantsPoses(c).count(ec.composant)) std::erase_if(c.composants, [&](const ComposantElec& x) { return x.uuid == ec.composant; });
        break;
      case GenreEcart::HorsCarteLibre:
      case GenreEcart::HorsCarteRoute:
        if (retirer(ec.composant)) ++n;
        break;
      case GenreEcart::EmpreinteRemplacee:
      case GenreEcart::PastillePerdue:
      case GenreEcart::Verrouillee:
        if (remplacerEmpreinteEnPlace(c, ec.composant, ec.apres)) ++n;
        break;
      case GenreEcart::ReseauRenomme: {
        for (auto& t : c.circuit.pistes)
          if (t.reseau == ec.avant) t.reseau = ec.apres;
        for (auto& v : c.circuit.vias)
          if (v.reseau == ec.avant) v.reseau = ec.apres;
        for (auto& z : c.circuit.zones)
          if (z.reseau == ec.avant) z.reseau = ec.apres;
        renommage = true;
        ++n;
        break;
      }
      case GenreEcart::BrocheRoutee: {
        // Le cuivre qui ne touche que cette pastille prend le nouveau réseau ; le reste garde l'ancien
        const EmpreintePlacee* e = placee(c, ec.composant);
        const DefEmpreinte* d = e ? c.empreinte(e->empreinte) : nullptr;
        if (!d) break;
        std::set<std::size_t> pistes, vias;
        for (const auto& p : d->pastilles)
          if (p.numero == ec.broche) {
            const CuivreTouchant t = cuivreSurPastille(c, *e, p);
            for (std::size_t i : t.pistes)
              if (c.circuit.pistes[i].reseau == ec.avant) pistes.insert(i);
            for (std::size_t i : t.vias)
              if (c.circuit.vias[i].reseau == ec.avant) vias.insert(i);
          }
        etendreGroupe(c, ec.avant, pistes, vias);
        const auto pp = pastillesDuGroupe(c, pistes, vias);
        if (pp.size() == 1) {
          for (std::size_t i : pistes) c.circuit.pistes[i].reseau = ec.apres;
          for (std::size_t i : vias) c.circuit.vias[i].reseau = ec.apres;
          ++n;
        }
        break;
      }
      case GenreEcart::LiaisonCircuit: {
        // Réseau nouveau au schéma : une étiquette au nom proposé sur chaque broche reliée au circuit
        std::vector<std::tuple<std::size_t, Pt>> poses;
        std::set<std::size_t> feuilles;
        for (const auto& rp : ec.pastilles) {
          const std::size_t deux = rp.rfind(':');
          const std::string ref = rp.substr(0, deux), num = rp.substr(deux + 1);
          for (const auto& x : c.composants)
            if (x.reference == ref) {
              std::size_t f = 0;
              Pt p;
              if (pointDeBroche(c, x.uuid, num, f, p)) {
                poses.emplace_back(f, p);
                feuilles.insert(f);
              }
              break;
            }
        }
        if (poses.size() < 2) break;
        for (const auto& [f, p] : poses) poserEtiquette(c, f, p, ec.apres, feuilles.size() > 1);
        std::set<std::size_t> pistes, vias;
        if (const std::size_t i = static_cast<std::size_t>(std::stoul(ec.broche.empty() ? "0" : ec.broche)); i < c.circuit.pistes.size()) {
          pistes.insert(i);
          etendreGroupe(c, "", pistes, vias);
          for (std::size_t j : pistes) c.circuit.pistes[j].reseau = ec.apres;
          for (std::size_t j : vias) c.circuit.vias[j].reseau = ec.apres;
        }
        ++n;
        break;
      }
    }
  }
  if (n) {
    figerEmpreintes(c);
    if (renommage) synchroniserReseaux(c);
    // Empreintes figées que plus rien n'utilise : retirées du projet
    std::set<std::string> utilisees;
    for (const auto& e : c.circuit.empreintes) utilisees.insert(e.empreinte);
    for (const auto& x : c.composants) utilisees.insert(x.empreinte);
    std::erase_if(c.empreintes, [&](const auto& kv) { return !utilisees.count(kv.first); });
  }
  return n;
}

std::vector<EcartSynchro> synchroniser(ConceptionElec& c) {
  std::vector<EcartSynchro> e = ecartsSynchronisation(c);
  if (c.verrouillee || c.synchroSurConfirmation) return e;
  std::vector<EcartSynchro> surs, attente;
  for (auto& x : e) (x.sansRisque ? surs : attente).push_back(x);
  if (surs.empty()) return attente;
  appliquerEcarts(c, surs);
  return ecartsSynchronisation(c);
}

bool appliquerSuites(ConceptionElec& c, const ChangementConception& ch) {
  if (c.verrouillee) return false;
  bool change = false;
  // Valeurs calculées (3.14)
  if (std::any_of(c.composants.begin(), c.composants.end(), [](const ComposantElec& x) { return !x.formule.empty(); })) {
    std::vector<std::string> avant;
    for (const auto& x : c.composants) avant.push_back(x.valeur);
    recalculerFormules(c);
    for (std::size_t i = 0; i < c.composants.size() && !change; ++i) change = c.composants[i].valeur != avant[i];
  }
  // Écarts sans risque (3.6) : seulement quand la modification peut toucher le lien du schéma et du circuit
  static const std::set<std::string> proprietes{"empreinte", "horsCarte", "reference", "symbole"};
  bool lien = ch.entiere || ch.demandeConnectivite() || ch.circuit;
  for (const auto& p : ch.proprietes) lien |= proprietes.count(p) > 0;
  bool ajoutRetrait = false;
  for (const auto& [uuid, natures] : ch.objets) ajoutRetrait |= (natures & (ChangementConception::Ajout | ChangementConception::Retrait)) != 0;
  lien |= ajoutRetrait;
  // Cuivre ou empreintes seulement déplacés (glisser sur une grande carte, 9.2) : seul du cuivre écarté d'une empreinte
  // mise hors carte ou dont le composant a quitté le schéma peut rendre un écart sans risque ; sans de telles
  // empreintes, la comparaison complète est épargnée
  if (lien && !ch.entiere && !ch.demandeConnectivite() && !ajoutRetrait && !ch.zones && !ch.regles && ch.proprietes.empty()) {
    std::set<std::string> poses;
    for (const auto& f : c.feuilles)
      for (const auto& s : f.symboles) poses.insert(s.composant);
    bool enAttente = false;
    for (const auto& e : c.circuit.empreintes) {
      const ComposantElec* x = c.composant(e.composant);
      if (!x || x->horsCarte || !poses.count(x->uuid)) {
        enAttente = true;
        break;
      }
    }
    if (!enAttente) lien = false;
  }
  if (lien && !c.synchroSurConfirmation) {
    std::vector<EcartSynchro> surs;
    for (auto& x : ecartsSynchronisation(c))
      if (x.sansRisque) surs.push_back(x);
    if (!surs.empty() && appliquerEcarts(c, surs) > 0) change = true;
  }
  return change;
}

int renommerReseauDepuisCircuit(ConceptionElec& c, const std::string& ancien, const std::string& nouveau) {
  exigerModifiable(c);
  if (ancien.empty() || nouveau.empty() || ancien == nouveau) return 0;
  const Connectivite k = connectivite(c);
  const std::string canon = k.canonique(ancien);
  int n = 0;
  const int i = k.indice(canon);
  if (i >= 0) {
    const Reseau& r = k.reseaux[static_cast<std::size_t>(i)];
    if (r.nomme) n += renommerReseau(c, canon, nouveau);
    if (n == 0) {
      // Réseau sans nom au schéma : une étiquette sur sa première broche posée
      for (const auto& b : r.broches) {
        std::size_t f = 0;
        Pt p;
        if (pointDeBroche(c, b.composant, b.broche, f, p)) {
          poserEtiquette(c, f, p, nouveau, false);
          ++n;
          break;
        }
      }
    }
  }
  // Le cuivre prend le nouveau nom, ainsi que les affectations de classes qui visaient l'ancien
  auto suit = [&](std::string& reseau) {
    if (!reseau.empty() && (reseau == ancien || reseau == canon || k.canonique(reseau) == canon)) {
      reseau = nouveau;
      ++n;
    }
  };
  for (auto& t : c.circuit.pistes) suit(t.reseau);
  for (auto& v : c.circuit.vias) suit(v.reseau);
  for (auto& z : c.circuit.zones) suit(z.reseau);
  for (auto& [motif, classe] : c.circuit.regles.classeReseau)
    if (motif == ancien || motif == canon) motif = nouveau;
  return n;
}

// ---------- Appariement (3.19) ----------
std::vector<int> affectationCoutMinimal(const std::vector<std::vector<double>>& couts) {
  const std::size_t lignes = couts.size();
  if (lignes == 0) return {};
  std::size_t colonnes = 0;
  for (const auto& l : couts) colonnes = std::max(colonnes, l.size());
  const std::size_t n = std::max(lignes, colonnes);
  const double grand = 1e9;
  // Matrice carrée complétée par des coûts nuls (lignes ou colonnes fictives)
  std::vector<std::vector<double>> a(n + 1, std::vector<double>(n + 1, 0));
  for (std::size_t i = 0; i < n; ++i)
    for (std::size_t j = 0; j < n; ++j) a[i + 1][j + 1] = i < lignes && j < couts[i].size() ? std::min(couts[i][j], grand) : (i < lignes && j < colonnes ? grand : 0);
  // Kuhn-Munkres en O(n³), potentiels u et v
  std::vector<double> u(n + 1, 0), v(n + 1, 0);
  std::vector<std::size_t> p(n + 1, 0), voie(n + 1, 0);
  for (std::size_t i = 1; i <= n; ++i) {
    p[0] = i;
    std::size_t j0 = 0;
    std::vector<double> mini(n + 1, std::numeric_limits<double>::max());
    std::vector<bool> utilise(n + 1, false);
    do {
      utilise[j0] = true;
      const std::size_t i0 = p[j0];
      double delta = std::numeric_limits<double>::max();
      std::size_t j1 = 0;
      for (std::size_t j = 1; j <= n; ++j)
        if (!utilise[j]) {
          const double cur = a[i0][j] - u[i0] - v[j];
          if (cur < mini[j]) {
            mini[j] = cur;
            voie[j] = j0;
          }
          if (mini[j] < delta) {
            delta = mini[j];
            j1 = j;
          }
        }
      for (std::size_t j = 0; j <= n; ++j)
        if (utilise[j]) {
          u[p[j]] += delta;
          v[j] -= delta;
        } else
          mini[j] -= delta;
      j0 = j1;
    } while (p[j0] != 0);
    do {
      const std::size_t j1 = voie[j0];
      p[j0] = p[j1];
      j0 = j1;
    } while (j0);
  }
  std::vector<int> r(lignes, -1);
  for (std::size_t j = 1; j <= n; ++j)
    if (p[j] >= 1 && p[j] <= lignes && j <= colonnes && a[p[j]][j] < grand) r[p[j] - 1] = static_cast<int>(j - 1);
  return r;
}

namespace {
// Distance d'édition (Levenshtein), pour comparer repères, valeurs et empreintes des orphelins
double distanceTexte(const std::string& a, const std::string& b) {
  std::vector<std::size_t> d(b.size() + 1);
  std::iota(d.begin(), d.end(), 0);
  for (std::size_t i = 1; i <= a.size(); ++i) {
    std::size_t prec = d[0];
    d[0] = i;
    for (std::size_t j = 1; j <= b.size(); ++j) {
      const std::size_t t = d[j];
      d[j] = std::min({d[j] + 1, d[j - 1] + 1, prec + (a[i - 1] == b[j - 1] ? 0 : 1)});
      prec = t;
    }
  }
  const std::size_t m = std::max<std::size_t>({a.size(), b.size(), 1});
  return static_cast<double>(d[b.size()]) / static_cast<double>(m);
}
std::string prefixe(const std::string& r) {
  std::string p;
  for (char x : r) {
    if (!std::isalpha(static_cast<unsigned char>(x))) break;
    p += x;
  }
  return p;
}
}  // namespace

std::vector<EmpreinteSansLien> empreintesSansLien(const ConceptionElec& source) {
  std::vector<EmpreinteSansLien> r;
  for (const auto& e : source.circuit.empreintes) {
    const ComposantElec* x = source.composant(e.composant);
    r.push_back({x ? x->reference : std::string(), x ? x->valeur : std::string(), e.empreinte});
  }
  return r;
}

int reporterCarte(ConceptionElec& cible, const ConceptionElec& source, const std::vector<Appariement>& paires) {
  exigerModifiable(cible);
  cible.schemaSeul = false;
  cible.circuit.contour = source.circuit.contour;
  cible.circuit.empilage = source.circuit.empilage;
  cible.circuit.regles = source.circuit.regles;
  cible.circuit.fabrication = source.circuit.fabrication;
  cible.circuit.serigraphie = source.circuit.serigraphie;
  cible.circuit.pistes = source.circuit.pistes;
  cible.circuit.vias = source.circuit.vias;
  cible.circuit.zones = source.circuit.zones;
  cible.circuit.textes = source.circuit.textes;
  cible.circuit.volumes = source.circuit.volumes;
  cible.circuit.empreintes.clear();
  int n = 0;
  for (const Appariement& a : paires) {
    if (a.empreinte >= source.circuit.empreintes.size()) continue;
    ComposantElec* x = cible.composant(a.composant);
    if (!x) continue;
    EmpreintePlacee e = source.circuit.empreintes[a.empreinte];
    e.composant = x->uuid;
    if (const DefEmpreinte* d = source.empreinte(e.empreinte)) cible.empreintes[e.empreinte] = *d;
    x->empreinte = e.empreinte;  // l'empreinte de la carte : le schéma la prend, rien à remplacer
    cible.circuit.empreintes.push_back(e);
    ++n;
  }
  return n;
}

ResultatAppariement apparier(const ConceptionElec& c, const std::vector<EmpreinteSansLien>& carte, bool coutMinimal) {
  ResultatAppariement r;
  // Tous les composants qui vont sur la carte, symbole posé ou non (un trou de fixation importé n'en a pas)
  std::vector<const ComposantElec*> libres;
  for (const auto& x : c.composants)
    if (!x.horsCarte) libres.push_back(&x);
  std::vector<bool> pris(carte.size(), false);
  auto prendre = [&](std::size_t i, const ComposantElec* x, Appariement::Par par, double cout) {
    r.paires.push_back({i, x->uuid, par, cout});
    pris[i] = true;
    libres.erase(std::find(libres.begin(), libres.end(), x));
  };
  // 1. Par repère
  for (std::size_t i = 0; i < carte.size(); ++i) {
    if (carte[i].reference.empty()) continue;
    const auto it = std::find_if(libres.begin(), libres.end(), [&](const ComposantElec* x) { return x->reference == carte[i].reference; });
    if (it != libres.end()) prendre(i, *it, Appariement::Par::Repere, 0);
  }
  // 2. Par valeur et empreinte, quand un seul composant libre les porte
  for (std::size_t i = 0; i < carte.size(); ++i) {
    if (pris[i]) continue;
    std::vector<const ComposantElec*> v;
    for (const ComposantElec* x : libres)
      if (x->empreinte == carte[i].empreinte && cleValeur(lireValeurComposant(x->valeur, familleDe(c, *x)), x->valeur) ==
                                                    cleValeur(lireValeurComposant(carte[i].valeur, familleDe(c, *x)), carte[i].valeur))
        v.push_back(x);
    if (v.size() == 1) prendre(i, v.front(), Appariement::Par::ValeurEmpreinte, 0);
  }
  // 3. Les orphelins, par coût minimal
  std::vector<std::size_t> restes;
  for (std::size_t i = 0; i < carte.size(); ++i)
    if (!pris[i]) restes.push_back(i);
  if (coutMinimal && !restes.empty() && !libres.empty()) {
    std::vector<std::vector<double>> couts(restes.size(), std::vector<double>(libres.size()));
    for (std::size_t a = 0; a < restes.size(); ++a)
      for (std::size_t b = 0; b < libres.size(); ++b) {
        const EmpreinteSansLien& e = carte[restes[a]];
        const ComposantElec& x = *libres[b];
        double cout = distanceTexte(e.reference, x.reference) + distanceTexte(e.valeur, x.valeur) + 2 * distanceTexte(e.empreinte, x.empreinte);
        if (prefixe(e.reference) != prefixe(x.reference)) cout += 3;  // une résistance ne s'apparie pas à un condensateur
        couts[a][b] = cout;
      }
    const std::vector<int> choix = affectationCoutMinimal(couts);
    std::vector<std::tuple<std::size_t, const ComposantElec*, double>> faits;
    for (std::size_t a = 0; a < restes.size(); ++a)
      if (choix[a] >= 0 && couts[a][static_cast<std::size_t>(choix[a])] < 3) faits.emplace_back(a, libres[static_cast<std::size_t>(choix[a])], couts[a][static_cast<std::size_t>(choix[a])]);
    for (const auto& [a, x, cout] : faits) prendre(restes[a], x, Appariement::Par::CoutMinimal, cout);
  }
  for (std::size_t i = 0; i < carte.size(); ++i)
    if (!pris[i]) r.empreintesSeules.push_back(i);
  for (const ComposantElec* x : libres) r.composantsSeuls.push_back(x->uuid);
  return r;
}

}  // namespace prom::ecad

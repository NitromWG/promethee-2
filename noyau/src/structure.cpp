// Prométhée : opérations de structure sur les pièces et les assemblages d'un projet. Licence GPL-3.0-only.
#include "promethee/structure.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <map>

namespace prom {

namespace {

std::string nomAssemblage(const Projet& p, int a) {
  return rangAssemblageValide(p, a) ? "« " + p.assemblages[static_cast<std::size_t>(a)].nom + " »" : "n° " + std::to_string(a + 1);
}

// Retire les contraintes qui visent un composant de « retires » et renumérote les autres
int renumeroterContraintes(Assemblage& a, const std::vector<int>& nouveauRang) {
  std::vector<ContrainteAssemblage> gardees;
  gardees.reserve(a.contraintes.size());
  int retirees = 0;
  const int n = static_cast<int>(nouveauRang.size());
  for (auto c : a.contraintes) {
    const int na = c.a >= 0 && c.a < n ? nouveauRang[static_cast<std::size_t>(c.a)] : -1;
    const int nb = c.b >= 0 && c.b < n ? nouveauRang[static_cast<std::size_t>(c.b)] : -1;
    if (na < 0 || nb < 0) { ++retirees; continue; }
    c.a = na;
    c.b = nb;
    gardees.push_back(std::move(c));
  }
  a.contraintes = std::move(gardees);
  return retirees;
}

}  // namespace

bool referenceAssemblage(const std::string& ref) { return ref.rfind(PREFIXE_REFERENCE_ASSEMBLAGE, 0) == 0; }
bool contrainteVersAssemblage(const ContrainteAssemblage& c) { return referenceAssemblage(c.refA) != referenceAssemblage(c.refB); }

bool typeContrainteAssemblageConnu(const std::string& type) {
  static const std::array<const char*, 11> connus = {"coincidence", "concentrique", "parallele", "perpendiculaire", "angle", "tangente",
                                                     "pivot",       "glissiere",    "rotule",    "engrenage",       "vis"};
  return std::any_of(connus.begin(), connus.end(), [&](const char* t) { return type == t; });
}

bool rangPieceValide(const Projet& p, int piece) { return piece >= 0 && piece < static_cast<int>(p.pieces.size()); }
bool rangAssemblageValide(const Projet& p, int assemblage) { return assemblage >= 0 && assemblage < static_cast<int>(p.assemblages.size()); }

std::vector<int> assemblagesUtilisantPiece(const Projet& p, int piece) {
  std::vector<int> r;
  for (int a = 0; a < static_cast<int>(p.assemblages.size()); ++a)
    for (const auto& c : p.assemblages[static_cast<std::size_t>(a)].composants)
      if (c.assemblage < 0 && c.piece == piece) { r.push_back(a); break; }
  return r;
}

std::vector<int> assemblagesUtilisantAssemblage(const Projet& p, int assemblage) {
  std::vector<int> r;
  for (int a = 0; a < static_cast<int>(p.assemblages.size()); ++a)
    for (const auto& c : p.assemblages[static_cast<std::size_t>(a)].composants)
      if (c.assemblage == assemblage) { r.push_back(a); break; }
  return r;
}

bool contientAssemblage(const Projet& p, int dans, int cherche) {
  if (dans == cherche) return true;
  // Parcours en largeur avec ensemble des assemblages déjà vus : fini même en présence de boucles
  std::vector<char> vu(p.assemblages.size(), 0);
  std::vector<int> file;
  if (rangAssemblageValide(p, dans)) { file.push_back(dans); vu[static_cast<std::size_t>(dans)] = 1; }
  for (std::size_t i = 0; i < file.size(); ++i)
    for (const auto& c : p.assemblages[static_cast<std::size_t>(file[i])].composants) {
      if (c.assemblage < 0) continue;
      if (c.assemblage == cherche) return true;
      if (rangAssemblageValide(p, c.assemblage) && !vu[static_cast<std::size_t>(c.assemblage)]) {
        vu[static_cast<std::size_t>(c.assemblage)] = 1;
        file.push_back(c.assemblage);
      }
    }
  return false;
}

std::vector<std::pair<int, int>> quantitesPieces(const Projet& p, int assemblage) {
  std::map<int, int> q;
  std::vector<char> enCours(p.assemblages.size(), 0);
  std::function<void(int, int, int)> compter = [&](int a, int mult, int profondeur) {
    if (!rangAssemblageValide(p, a) || enCours[static_cast<std::size_t>(a)] || profondeur > PROFONDEUR_ASSEMBLAGE_MAX) return;
    enCours[static_cast<std::size_t>(a)] = 1;
    for (const auto& c : p.assemblages[static_cast<std::size_t>(a)].composants) {
      if (c.supprime) continue;
      if (c.assemblage >= 0) compter(c.assemblage, mult, profondeur + 1);
      else if (rangPieceValide(p, c.piece)) q[c.piece] += mult;
    }
    enCours[static_cast<std::size_t>(a)] = 0;
  };
  compter(assemblage, 1, 0);
  return {q.begin(), q.end()};
}

std::vector<LigneNomenclatureMeca> nomenclatureMecanique(const Projet& p, int assemblage) {
  std::map<std::pair<int, bool>, int> q;
  std::vector<char> enCours(p.assemblages.size(), 0);
  std::function<void(int, bool, int)> compter = [&](int a, bool miroir, int profondeur) {
    if (!rangAssemblageValide(p, a) || enCours[static_cast<std::size_t>(a)] || profondeur > PROFONDEUR_ASSEMBLAGE_MAX) return;
    enCours[static_cast<std::size_t>(a)] = 1;
    for (const auto& c : p.assemblages[static_cast<std::size_t>(a)].composants) {
      if (c.supprime) continue;
      const bool m = miroir != (c.miroir != 0);
      if (c.assemblage >= 0) compter(c.assemblage, m, profondeur + 1);
      else if (rangPieceValide(p, c.piece)) ++q[{c.piece, m}];
    }
    enCours[static_cast<std::size_t>(a)] = 0;
  };
  if (assemblage >= 0) compter(assemblage, false, 0);
  else {
    // Racines : assemblages qui ne servent de sous-assemblage à aucun autre
    std::vector<char> sous(p.assemblages.size(), 0);
    for (const auto& a : p.assemblages)
      for (const auto& c : a.composants)
        if (c.assemblage >= 0 && rangAssemblageValide(p, c.assemblage)) sous[static_cast<std::size_t>(c.assemblage)] = 1;
    for (int a = 0; a < static_cast<int>(p.assemblages.size()); ++a)
      if (!sous[static_cast<std::size_t>(a)]) compter(a, false, 0);
    // Pièces présentes dans aucun assemblage : une de chaque
    std::vector<char> utilisee(p.pieces.size(), 0);
    for (const auto& a : p.assemblages)
      for (const auto& c : a.composants)
        if (c.assemblage < 0 && rangPieceValide(p, c.piece)) utilisee[static_cast<std::size_t>(c.piece)] = 1;
    for (int i = 0; i < static_cast<int>(p.pieces.size()); ++i)
      if (!utilisee[static_cast<std::size_t>(i)]) q[{i, false}] += 1;
  }
  std::vector<LigneNomenclatureMeca> r;
  for (const auto& [cle, n] : q) r.push_back({cle.first, cle.second, n});
  return r;
}

std::vector<LigneNomenclatureNiveau> nomenclatureNiveaux(const Projet& p, int assemblage) {
  std::vector<LigneNomenclatureNiveau> r;
  std::vector<char> enCours(p.assemblages.size(), 0);
  std::function<void(int, bool, int, int, const std::string&)> developper = [&](int a, bool miroir, int multiple, int niveau, const std::string& prefixe) {
    if (!rangAssemblageValide(p, a) || enCours[static_cast<std::size_t>(a)] || niveau > PROFONDEUR_ASSEMBLAGE_MAX) return;
    enCours[static_cast<std::size_t>(a)] = 1;
    // Regroupement des composants identiques, dans l'ordre de première apparition
    struct Groupe { bool assemblage; int indice; bool symetrique; int quantite; };
    std::vector<Groupe> groupes;
    for (const auto& c : p.assemblages[static_cast<std::size_t>(a)].composants) {
      if (c.supprime) continue;
      const bool sousAssemblage = c.assemblage >= 0;
      const int indice = sousAssemblage ? c.assemblage : c.piece;
      if (sousAssemblage ? !rangAssemblageValide(p, indice) : !rangPieceValide(p, indice)) continue;
      const bool m = miroir != (c.miroir != 0);
      auto it = std::find_if(groupes.begin(), groupes.end(), [&](const Groupe& g) { return g.assemblage == sousAssemblage && g.indice == indice && g.symetrique == m; });
      if (it == groupes.end()) groupes.push_back({sousAssemblage, indice, m, 1});
      else ++it->quantite;
    }
    int rang = 0;
    for (const auto& g : groupes) {
      LigneNomenclatureNiveau l;
      l.niveau = niveau;
      l.numero = prefixe + std::to_string(++rang);
      l.assemblage = g.assemblage;
      l.indice = g.indice;
      l.symetrique = g.symetrique;
      l.quantite = g.quantite;
      l.quantiteTotale = g.quantite * multiple;
      r.push_back(l);
      if (g.assemblage) developper(g.indice, g.symetrique, l.quantiteTotale, niveau + 1, l.numero + ".");
    }
    enCours[static_cast<std::size_t>(a)] = 0;
  };
  developper(assemblage, false, 1, 0, "");
  return r;
}

int repereArticle(const Projet& p, bool assemblage, int indice, bool symetrique) {
  if (assemblage) return rangAssemblageValide(p, indice) ? p.assemblages[static_cast<std::size_t>(indice)].repere : 0;
  if (!rangPieceValide(p, indice)) return 0;
  const Json& j = p.pieces[static_cast<std::size_t>(indice)];
  const char* cle = symetrique ? "repereSymetrique" : "repere";
  return j.is_object() && j.contains(cle) && j[cle].is_number_integer() ? j[cle].get<int>() : 0;
}

int assurerReperes(Projet& p) {
  // Versions symétriques employées
  std::set<int> symetriques;
  for (int a = 0; a < static_cast<int>(p.assemblages.size()); ++a)
    for (const auto& l : nomenclatureNiveaux(p, a)) if (!l.assemblage && l.symetrique) symetriques.insert(l.indice);
  std::set<int> pris;
  int plusGrand = 0;
  auto lire = [](const Json& j, const char* cle) { return j.is_object() && j.contains(cle) && j[cle].is_number_integer() ? j[cle].get<int>() : 0; };
  for (const auto& j : p.pieces) for (const char* cle : {"repere", "repereSymetrique"}) plusGrand = std::max(plusGrand, lire(j, cle));
  for (const auto& a : p.assemblages) plusGrand = std::max(plusGrand, a.repere);
  int attribues = 0;
  // Garde le repère s'il est valide et libre, sinon en donne un nouveau
  auto tenir = [&](int actuel) -> int {
    if (actuel >= 1 && !pris.count(actuel)) { pris.insert(actuel); return actuel; }
    ++attribues;
    pris.insert(++plusGrand);
    return plusGrand;
  };
  for (int i = 0; i < static_cast<int>(p.pieces.size()); ++i) {
    Json& j = p.pieces[static_cast<std::size_t>(i)];
    if (!j.is_object()) continue;
    const int r = tenir(lire(j, "repere"));
    if (lire(j, "repere") != r) j["repere"] = r;
    if (symetriques.count(i)) {
      const int s = tenir(lire(j, "repereSymetrique"));
      if (lire(j, "repereSymetrique") != s) j["repereSymetrique"] = s;
    } else if (const int s = lire(j, "repereSymetrique"); s > 0) {
      // Version symétrique plus employée : son repère reste réservé, sauf s'il est pris (copie de pièce)
      if (pris.count(s)) j.erase("repereSymetrique");
      else pris.insert(s);
    }
  }
  for (auto& a : p.assemblages) a.repere = tenir(a.repere);
  return attribues;
}

BilanRetrait retirerComposants(Assemblage& a, const std::set<int>& ks) {
  BilanRetrait b;
  std::vector<int> rang(a.composants.size(), -1);
  std::vector<ComposantAssemblage> restes;
  restes.reserve(a.composants.size());
  for (int k = 0; k < static_cast<int>(a.composants.size()); ++k) {
    if (ks.count(k)) { ++b.composants; continue; }
    rang[static_cast<std::size_t>(k)] = static_cast<int>(restes.size());
    restes.push_back(a.composants[static_cast<std::size_t>(k)]);
  }
  if (!b.composants) return b;
  b.contraintes = renumeroterContraintes(a, rang);
  a.composants = std::move(restes);
  return b;
}

BilanRetrait retirerComposant(Assemblage& a, int k) { return retirerComposants(a, {k}); }

BilanRetrait supprimerPiece(Projet& p, int piece) {
  BilanRetrait total;
  if (!rangPieceValide(p, piece)) return total;
  for (auto& a : p.assemblages) {
    std::set<int> vises;
    for (int k = 0; k < static_cast<int>(a.composants.size()); ++k)
      if (a.composants[static_cast<std::size_t>(k)].assemblage < 0 && a.composants[static_cast<std::size_t>(k)].piece == piece) vises.insert(k);
    const BilanRetrait b = retirerComposants(a, vises);
    total.composants += b.composants;
    total.contraintes += b.contraintes;
    for (auto& c : a.composants)
      if (c.assemblage < 0 && c.piece > piece) --c.piece;
  }
  p.pieces.erase(p.pieces.begin() + piece);
  return total;
}

bool supprimerAssemblage(Projet& p, int assemblage, std::string* raison) {
  if (!rangAssemblageValide(p, assemblage)) {
    if (raison) *raison = "Assemblage inexistant.";
    return false;
  }
  std::vector<int> utilisateurs;
  for (int u : assemblagesUtilisantAssemblage(p, assemblage))
    if (u != assemblage) utilisateurs.push_back(u);
  if (!utilisateurs.empty()) {
    if (raison) {
      std::string noms;
      for (std::size_t i = 0; i < utilisateurs.size(); ++i) noms += (i ? ", " : "") + nomAssemblage(p, utilisateurs[i]);
      *raison = nomAssemblage(p, assemblage) + " est utilisé comme sous-assemblage dans " + noms + " : retire-le d'abord de " +
                (utilisateurs.size() > 1 ? "ces assemblages." : "cet assemblage.");
    }
    return false;
  }
  p.assemblages.erase(p.assemblages.begin() + assemblage);
  for (auto& a : p.assemblages)
    for (auto& c : a.composants)
      if (c.assemblage > assemblage) --c.assemblage;
  return true;
}

std::vector<int> contraintesCoupees(const Assemblage& a, const std::set<int>& selection) {
  std::vector<int> r;
  for (int i = 0; i < static_cast<int>(a.contraintes.size()); ++i) {
    const auto& c = a.contraintes[static_cast<std::size_t>(i)];
    // Une contrainte vers l'assemblage (A2) est coupée quand son composant part dans le sous-assemblage
    if (selection.count(c.a) != selection.count(c.b) || (contrainteVersAssemblage(c) && selection.count(referenceAssemblage(c.refA) ? c.b : c.a))) r.push_back(i);
  }
  return r;
}

int creerSousAssemblage(Projet& p, int parent, const std::set<int>& selection, const std::string& nom) {
  if (!rangAssemblageValide(p, parent)) return -1;
  // Copie de travail : aucune référence dans p.assemblages n'est tenue à travers l'ajout (AS-03)
  Assemblage pere = p.assemblages[static_cast<std::size_t>(parent)];
  std::set<int> choisis;
  for (int k : selection)
    if (k >= 0 && k < static_cast<int>(pere.composants.size())) choisis.insert(k);
  if (choisis.size() < 2) return -1;
  Assemblage sous;
  sous.nom = nom.empty() ? "Sous-assemblage " + std::to_string(p.assemblages.size() + 1) : nom;
  std::map<int, int> rangSous;
  bool fixe = false;
  for (int k : choisis) {
    const ComposantAssemblage& c = pere.composants[static_cast<std::size_t>(k)];
    if (k == 0 || c.fixe) fixe = true;
    rangSous[k] = static_cast<int>(sous.composants.size());
    sous.composants.push_back(c);
  }
  // Dans le sous-assemblage, le premier composant est la référence : il est fixe, les autres gardent leur état
  sous.composants.front().fixe = true;
  for (const auto& c : pere.contraintes)
    if (choisis.count(c.a) && choisis.count(c.b) && !contrainteVersAssemblage(c)) {
      ContrainteAssemblage x = c;
      x.a = rangSous[c.a];
      x.b = rangSous[c.b];
      sous.contraintes.push_back(std::move(x));
    }
  // Contraintes qui reliaient la sélection au reste : gardées, elles visent désormais la face par son chemin dans le
  // sous-assemblage (« @k/face »), qui devient un corps rigide (AS-16). Une liaison perd son état de départ, que
  // l'atelier recalcule (les rotations du sous-assemblage ne sont pas celles de la pièce).
  std::vector<ContrainteAssemblage> coupees;
  std::vector<int> rangReste(pere.composants.size(), -1);
  for (int k = 0, r = 0; k < static_cast<int>(pere.composants.size()); ++k)
    if (!choisis.count(k)) rangReste[static_cast<std::size_t>(k)] = r++;
  for (const auto& c : pere.contraintes) {
    ContrainteAssemblage x = c;
    if (contrainteVersAssemblage(c)) {
      // Vers l'assemblage (A2) : le composant part dans le sous-assemblage, la référence d'assemblage reste celle du
      // parent ; le côté réel vise sa face par son chemin, les deux côtés désignent le sous-assemblage
      const bool aReel = !referenceAssemblage(c.refA);
      const int reel = aReel ? c.a : c.b;
      if (!choisis.count(reel)) continue;
      std::string& ref = aReel ? x.refA : x.refB;
      ref = "@" + std::to_string(rangSous[reel]) + "/" + ref;
      x.a = x.b = -1;
      coupees.push_back(std::move(x));
      continue;
    }
    if (choisis.count(c.a) == choisis.count(c.b)) continue;
    if (choisis.count(c.a)) {
      x.refA = "@" + std::to_string(rangSous[c.a]) + "/" + c.refA;
      x.a = -1;  // le sous-assemblage, placé plus bas
      x.b = rangReste[static_cast<std::size_t>(c.b)];
    } else {
      x.refB = "@" + std::to_string(rangSous[c.b]) + "/" + c.refB;
      x.b = -1;
      x.a = rangReste[static_cast<std::size_t>(c.a)];
    }
    if (x.type == "pivot" || x.type == "glissiere" || x.type == "vis" || x.type == "engrenage") x.parametres.clear();
    coupees.push_back(std::move(x));
  }
  retirerComposants(pere, choisis);
  const int rang = static_cast<int>(p.assemblages.size());
  // Le sous-assemblage est placé à l'origine : ses composants gardent leur position dans l'espace
  ComposantAssemblage s;
  s.assemblage = rang;
  s.fixe = fixe || pere.composants.empty();
  int rangS = 0;
  if (fixe && !pere.composants.empty()) {
    // La référence fixe du parent (rang 0) était dans la sélection : le sous-assemblage prend sa place
    pere.composants.insert(pere.composants.begin(), s);
    for (auto& c : pere.contraintes) { ++c.a; ++c.b; }
    for (auto& c : coupees) { if (c.a >= 0) ++c.a; if (c.b >= 0) ++c.b; }
  } else {
    rangS = static_cast<int>(pere.composants.size());
    pere.composants.push_back(s);
  }
  for (auto& c : coupees) {
    if (c.a < 0) c.a = rangS;
    if (c.b < 0) c.b = rangS;
    pere.contraintes.push_back(std::move(c));
  }
  p.assemblages.push_back(std::move(sous));
  p.assemblages[static_cast<std::size_t>(parent)] = std::move(pere);
  return rang;
}

int assurerIdentifiantsPieces(Projet& p, std::vector<std::string>* alertes) {
  std::set<std::string> explicites, pris;
  for (const auto& x : p.pieces)
    if (x.is_object() && x.contains("id") && x["id"].is_string()) explicites.insert(x["id"].get<std::string>());
  int changees = 0, n = 0;
  for (auto& x : p.pieces) {
    ++n;
    if (!x.is_object()) continue;
    const bool lisible = x.contains("id") && x["id"].is_string() && !x["id"].get<std::string>().empty();
    if (!lisible || pris.count(x["id"].get<std::string>())) {
      if (lisible && alertes) alertes->push_back("Pièce n° " + std::to_string(n) + " : identifiant « " + x["id"].get<std::string>() + " » en double, remplacé.");
      int k = n;
      while (pris.count("piece" + std::to_string(k)) || explicites.count("piece" + std::to_string(k))) ++k;
      x["id"] = "piece" + std::to_string(k);
      ++changees;
    }
    pris.insert(x["id"].get<std::string>());
  }
  return changees;
}

void validerAssemblages(Projet& p, std::vector<std::string>* alertes) {
  auto alerter = [&](const std::string& m) { if (alertes) alertes->push_back(m); };
  const int na = static_cast<int>(p.assemblages.size());
  for (int ai = 0; ai < na; ++ai) {
    Assemblage& a = p.assemblages[static_cast<std::size_t>(ai)];
    const std::string ctx = "Assemblage « " + a.nom + " »";
    // Composants dont le sous-assemblage n'existe pas, ou qui le contiendraient lui-même
    std::set<int> invalides;
    for (int k = 0; k < static_cast<int>(a.composants.size()); ++k) {
      const auto& c = a.composants[static_cast<std::size_t>(k)];
      if (c.assemblage >= 0) {
        if (!rangAssemblageValide(p, c.assemblage)) {
          alerter(ctx + " : le composant n° " + std::to_string(k + 1) + " désigne un sous-assemblage inexistant ; retiré.");
          invalides.insert(k);
        } else if (contientAssemblage(p, c.assemblage, ai)) {
          alerter(ctx + " : le composant n° " + std::to_string(k + 1) + " le contiendrait lui-même (boucle de sous-assemblages) ; retiré.");
          invalides.insert(k);
          // retiré tout de suite, pour que la boucle ne soit pas signalée une seconde fois depuis l'autre côté
          a.composants[static_cast<std::size_t>(k)].assemblage = -2;
        }
      } else if (c.assemblage < -1) {
        invalides.insert(k);
      } else if (!rangPieceValide(p, c.piece)) {
        alerter(ctx + " : le composant n° " + std::to_string(k + 1) + " désigne une pièce inexistante (n° " + std::to_string(c.piece + 1) + ") ; retiré.");
        invalides.insert(k);
      }
    }
    if (!invalides.empty()) {
      const BilanRetrait b = retirerComposants(a, invalides);
      if (b.contraintes) alerter(ctx + " : " + std::to_string(b.contraintes) + " contrainte(s) liée(s) aux composants retirés écartée(s).");
    }
    // Contraintes dont un composant n'existe pas, ou qui relient un composant à lui-même
    const int nc = static_cast<int>(a.composants.size());
    std::vector<ContrainteAssemblage> gardees;
    for (std::size_t i = 0; i < a.contraintes.size(); ++i) {
      const auto& c = a.contraintes[i];
      // Une contrainte vers l'assemblage (A2) porte le même composant des deux côtés
      if (c.a < 0 || c.b < 0 || c.a >= nc || c.b >= nc || (c.a == c.b && !contrainteVersAssemblage(c))) {
        alerter(ctx + " : la contrainte n° " + std::to_string(i + 1) + " désigne un composant inexistant ; écartée.");
        continue;
      }
      if (!typeContrainteAssemblageConnu(c.type))
        alerter(ctx + " : la contrainte n° " + std::to_string(i + 1) + " est de type inconnu « " + c.type + " » ; gardée, mais elle n'est pas résolue.");
      gardees.push_back(c);
    }
    a.contraintes = std::move(gardees);
  }
}

}  // namespace prom

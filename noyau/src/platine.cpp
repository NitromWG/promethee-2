// Prométhée : platine d'essai et documents de câblage (lot L16 de la 0.34 ; 16.2 à 16.4, 16.6). Voir platine.hpp.
// Licence GPL-3.0-only.
#include "promethee/platine.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <set>
#include <sstream>

namespace prom::ecad {

namespace {
const std::vector<std::string>& rangees() {
  static const std::vector<std::string> r{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};
  return r;
}
bool estRail(const std::string& r) { return r == "+h" || r == "-h" || r == "+b" || r == "-b"; }
constexpr double hauteurPlatine = 48.26;  // mm, du bord haut au bord bas du dessin
std::string majuscules(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
bool reseauMasse(const std::string& n) {
  const std::string m = majuscules(n);
  return m == "GND" || m == "0" || m == "MASSE" || m == "VSS" || m.rfind("GND", 0) == 0;
}
bool reseauAlimentation(const std::string& n) {
  const std::string m = majuscules(n);
  return (!m.empty() && m[0] == '+') || m == "VCC" || m == "VDD" || m == "5V" || m == "3V3" || m == "12V" || m == "9V";
}
struct Ensembles {
  std::vector<int> p;
  int trouver(int x) { return p[static_cast<std::size_t>(x)] == x ? x : p[static_cast<std::size_t>(x)] = trouver(p[static_cast<std::size_t>(x)]); }
  void unir(int a, int b) {
    a = trouver(a), b = trouver(b);
    if (a != b) p[static_cast<std::size_t>(a)] = b;
  }
  int ajouter() {
    p.push_back(static_cast<int>(p.size()));
    return static_cast<int>(p.size()) - 1;
  }
};
// Composants posés sur une feuille (les drapeaux et symboles d'alimentation n'en sont pas)
std::vector<const ComposantElec*> composantsPoses(const ConceptionElec& c) {
  std::set<std::string> poses;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (!s.composant.empty()) poses.insert(s.composant);
  std::vector<const ComposantElec*> v;
  for (const auto& x : c.composants) {
    if (!poses.count(x.uuid) || x.genre == GenreComposant::Graphique || x.genre == GenreComposant::Mecanique) continue;
    const DefSymbole* d = c.symbole(x.symbole);
    if (!d || d->broches.empty() || d->alimentation || d->drapeau) continue;  // trou de fixation, logo, alimentation : rien à enfoncer
    v.push_back(&x);
  }
  return v;
}
}  // namespace

// ======================================================================================================================
// Trous
// ======================================================================================================================
std::string texteTrou(const Trou& t) { return estRail(t.rangee) ? t.rangee + std::to_string(t.colonne) : std::to_string(t.colonne) + t.rangee; }

std::optional<Trou> lireTrou(const std::string& s) {
  if (s.size() < 2) return std::nullopt;
  Trou t;
  if (s[0] == '+' || s[0] == '-') {
    t.rangee = s.substr(0, 2);
    t.colonne = std::atoi(s.c_str() + 2);
  } else {
    std::size_t i = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i == 0 || i >= s.size()) return std::nullopt;
    t.colonne = std::atoi(s.substr(0, i).c_str());
    t.rangee = s.substr(i);
  }
  if (t.colonne <= 0) return std::nullopt;
  return t;
}

bool trouValide(const ModelePlatine& m, const Trou& t) {
  if (t.colonne < 1 || t.colonne > m.colonnes) return false;
  return estRail(t.rangee) || std::find(rangees().begin(), rangees().end(), t.rangee) != rangees().end();
}

std::string bandeDuTrou(const Trou& t) {
  if (estRail(t.rangee)) return t.rangee;
  return std::to_string(t.colonne) + (t.rangee < "f" ? "-haut" : "-bas");
}

Pt positionTrou(const Trou& t) {
  const double x = t.colonne * 2.54;
  double y;
  if (t.rangee == "-h") y = 2.54;
  else if (t.rangee == "+h") y = 5.08;
  else if (t.rangee == "+b") y = 43.18;
  else if (t.rangee == "-b") y = 45.72;
  else {
    const int i = static_cast<int>(std::find(rangees().begin(), rangees().end(), t.rangee) - rangees().begin());
    // Rainure centrale de 7,62 mm (0,3 pouce) entre e et f : un boîtier DIP de 300 mils l'enjambe
    y = i < 5 ? 10.16 + i * 2.54 : 27.94 + (i - 5) * 2.54;
  }
  return ptMm(x, y);
}

std::optional<Trou> trouProche(const ModelePlatine& m, Pt p, Nm tolerance) {
  std::optional<Trou> meilleur;
  double d = static_cast<double>(tolerance);
  std::vector<std::string> tout = rangees();
  for (const char* r : {"+h", "-h", "+b", "-b"}) tout.push_back(r);
  for (int c = 1; c <= m.colonnes; ++c)
    for (const auto& r : tout) {
      const Trou t{c, r};
      const Pt q = positionTrou(t);
      const double e = std::hypot(static_cast<double>(q.x - p.x), static_cast<double>(q.y - p.y));
      if (e <= d) d = e, meilleur = t;
    }
  return meilleur;
}

// ======================================================================================================================
// Vue
// ======================================================================================================================
const PosePlatine* VuePlatine::pose(const std::string& composant) const {
  for (const auto& p : poses)
    if (p.composant == composant) return &p;
  return nullptr;
}
PosePlatine* VuePlatine::pose(const std::string& composant) {
  for (auto& p : poses)
    if (p.composant == composant) return &p;
  return nullptr;
}

VuePlatine platineDe(const ConceptionElec& c) {
  VuePlatine v;
  const auto it = c.extensions.find("platine");
  if (it == c.extensions.end() || !it->second.is_object()) return v;
  const auto& j = it->second;
  v.modele.nom = j.value("modele", std::string("830 points"));
  v.modele.colonnes = j.value("colonnes", 63);
  for (const auto& p : j.value("poses", nlohmann::ordered_json::array())) {
    PosePlatine x;
    x.composant = p.value("composant", std::string());
    if (p.contains("broches") && p["broches"].is_object())
      for (const auto& [n, t] : p["broches"].items())
        if (const auto tr = lireTrou(t.get<std::string>())) x.broches[n] = *tr;
    v.poses.push_back(x);
  }
  for (const auto& f : j.value("fils", nlohmann::ordered_json::array())) {
    FilPlatine x;
    x.uuid = f.value("uuid", std::string());
    if (const auto a = lireTrou(f.value("a", std::string()))) x.a = *a;
    if (const auto b = lireTrou(f.value("b", std::string()))) x.b = *b;
    x.couleur = f.value("couleur", std::string("#D32F2F"));
    x.section = f.value("section", 0.5);
    x.courbe = f.value("courbe", true);
    v.fils.push_back(x);
  }
  return v;
}

void ecrirePlatine(ConceptionElec& c, const VuePlatine& v) {
  nlohmann::ordered_json j = {{"modele", v.modele.nom}, {"colonnes", v.modele.colonnes}};
  nlohmann::ordered_json poses = nlohmann::ordered_json::array(), fils = nlohmann::ordered_json::array();
  for (const auto& p : v.poses) {
    nlohmann::ordered_json b = nlohmann::ordered_json::object();
    for (const auto& [n, t] : p.broches) b[n] = texteTrou(t);
    poses.push_back({{"composant", p.composant}, {"broches", b}});
  }
  for (const auto& f : v.fils) fils.push_back({{"uuid", f.uuid}, {"a", texteTrou(f.a)}, {"b", texteTrou(f.b)}, {"couleur", f.couleur}, {"section", f.section}, {"courbe", f.courbe}});
  j["poses"] = poses;
  j["fils"] = fils;
  c.extensions["platine"] = j;
}

// ======================================================================================================================
// Connexions
// ======================================================================================================================
int ConnectivitePlatine::noeudDuTrou(const Trou& t) const {
  const auto it = parBande.find(bandeDuTrou(t));
  return it == parBande.end() ? -1 : it->second;
}

ConnectivitePlatine connectivitePlatine(const VuePlatine& v) {
  ConnectivitePlatine r;
  Ensembles e;
  std::map<std::string, int> bande;
  auto indice = [&](const Trou& t) {
    const std::string b = bandeDuTrou(t);
    const auto it = bande.find(b);
    if (it != bande.end()) return it->second;
    return bande[b] = e.ajouter();
  };
  for (const auto& p : v.poses)
    for (const auto& [n, t] : p.broches)
      if (trouValide(v.modele, t)) indice(t);
  for (const auto& f : v.fils)
    if (trouValide(v.modele, f.a) && trouValide(v.modele, f.b)) e.unir(indice(f.a), indice(f.b));
  std::map<int, int> noeud;  // racine → nœud
  for (const auto& [b, i] : bande) {
    const int racine = e.trouver(i);
    if (!noeud.count(racine)) {
      noeud[racine] = static_cast<int>(r.noeuds.size());
      r.noeuds.emplace_back();
    }
    r.parBande[b] = noeud[racine];
  }
  for (const auto& p : v.poses)
    for (const auto& [n, t] : p.broches) {
      if (!trouValide(v.modele, t)) continue;
      const int k = r.parBande.at(bandeDuTrou(t));
      r.noeuds[static_cast<std::size_t>(k)].push_back({p.composant, n});
      r.parBroche[{p.composant, n}] = k;
    }
  return r;
}

std::vector<Trou> trousDuPotentiel(const VuePlatine& v, const Trou& t) {
  const ConnectivitePlatine k = connectivitePlatine(v);
  const int n = k.noeudDuTrou(t);
  std::vector<Trou> r;
  std::vector<std::string> tout = rangees();
  for (const char* x : {"+h", "-h", "+b", "-b"}) tout.push_back(x);
  for (int c = 1; c <= v.modele.colonnes; ++c)
    for (const auto& x : tout) {
      const Trou u{c, x};
      if (n >= 0 ? k.noeudDuTrou(u) == n : bandeDuTrou(u) == bandeDuTrou(t)) r.push_back(u);
    }
  return r;
}

std::vector<EcartPlatine> comparerPlatine(const ConceptionElec& c, const VuePlatine& v) {
  std::vector<EcartPlatine> r;
  const Connectivite k = connectivite(c);
  const ConnectivitePlatine kp = connectivitePlatine(v);
  // Composants absents, pattes hors de la platine
  for (const ComposantElec* x : composantsPoses(c)) {
    const PosePlatine* p = v.pose(x->uuid);
    if (!p) {
      EcartPlatine e;
      e.genre = "composant absent de la platine";
      e.composant = x->uuid;
      e.message = x->reference + " n'est pas sur la platine";
      r.push_back(e);
      continue;
    }
    for (const auto& [n, t] : p->broches)
      if (!trouValide(v.modele, t)) {
        EcartPlatine e;
        e.genre = "patte hors de la platine";
        e.composant = x->uuid;
        e.broche = n;
        e.trou = t;
        e.message = x->reference + "." + n + " hors de la platine (" + texteTrou(t) + ")";
        r.push_back(e);
      }
  }
  auto ref = [&](const std::string& u) {
    const ComposantElec* x = c.composant(u);
    return x ? x->reference : u;
  };
  // Réseau par réseau : ses broches posées doivent partager un nœud de la platine
  for (const auto& res : k.reseaux) {
    std::vector<NoeudBroche> posees;
    for (const auto& b : res.broches)
      if (kp.parBroche.count({b.composant, b.broche})) posees.push_back(b);
    if (posees.size() < 2) continue;
    std::map<int, int> compte;
    for (const auto& b : posees) ++compte[kp.parBroche.at({b.composant, b.broche})];
    // Le nœud qui réunit le plus de broches fait foi ; sans majorité (deux broches séparées), chacune est signalée
    int majorite = -1, meilleur = 0, exaequo = 0;
    for (const auto& [n, nb] : compte) {
      if (nb > meilleur) meilleur = nb, majorite = n, exaequo = 1;
      else if (nb == meilleur) ++exaequo;
    }
    if (exaequo > 1) majorite = -1;
    for (const auto& b : posees) {
      const int noeud = kp.parBroche.at({b.composant, b.broche});
      if (noeud == majorite) continue;
      const NoeudBroche* repere = nullptr;
      for (const auto& o : posees) {
        const int n2 = kp.parBroche.at({o.composant, o.broche});
        if (n2 == noeud) continue;
        repere = &o;
        if (n2 == majorite) break;
      }
      EcartPlatine e;
      e.genre = "liaison manquante";
      e.composant = b.composant;
      e.broche = b.broche;
      e.reseau = res.nom;
      if (repere) e.autreComposant = repere->composant, e.autreBroche = repere->broche;
      if (const PosePlatine* p = v.pose(b.composant))
        if (const auto it = p->broches.find(b.broche); it != p->broches.end()) e.trou = it->second;
      e.message = ref(b.composant) + "." + b.broche + " (" + texteTrou(e.trou) + ") n'est pas reliée à " + (repere ? ref(repere->composant) + "." + repere->broche : std::string("son réseau")) +
                  " (réseau " + res.nom + ")";
      r.push_back(e);
    }
  }
  // Nœud par nœud : des broches de réseaux différents réunies
  for (const auto& n : kp.noeuds) {
    std::map<int, NoeudBroche> parReseau;
    for (const auto& b : n) {
      const int rs = k.reseau(b.composant, b.broche);
      parReseau.emplace(rs, b);
    }
    if (parReseau.size() < 2) continue;
    auto it = parReseau.begin();
    const NoeudBroche premier = it->second;
    const std::string nomPremier = it->first >= 0 ? k.reseaux[static_cast<std::size_t>(it->first)].nom : std::string("aucun");
    for (++it; it != parReseau.end(); ++it) {
      EcartPlatine e;
      e.genre = "liaison en trop";
      e.composant = it->second.composant;
      e.broche = it->second.broche;
      e.autreComposant = premier.composant;
      e.autreBroche = premier.broche;
      e.reseau = it->first >= 0 ? k.reseaux[static_cast<std::size_t>(it->first)].nom : std::string();
      if (const PosePlatine* p = v.pose(e.composant))
        if (const auto i = p->broches.find(e.broche); i != p->broches.end()) e.trou = i->second;
      e.message = ref(e.composant) + "." + e.broche + " (réseau " + e.reseau + ") touche " + ref(premier.composant) + "." + premier.broche + " (réseau " + nomPremier + ") sur la platine";
      r.push_back(e);
    }
  }
  return r;
}

std::map<std::pair<std::string, std::string>, EtatPatte> etatsPattes(const ConceptionElec& c, const VuePlatine& v) {
  std::map<std::pair<std::string, std::string>, EtatPatte> r;
  const Connectivite k = connectivite(c);
  const ConnectivitePlatine kp = connectivitePlatine(v);
  std::set<std::pair<std::string, std::string>> fautives;
  for (const auto& e : comparerPlatine(c, v))
    if (!e.broche.empty()) fautives.insert({e.composant, e.broche});
  for (const auto& p : v.poses)
    for (const auto& [n, t] : p.broches) {
      const int rs = k.reseau(p.composant, n);
      int autres = 0;
      if (rs >= 0)
        for (const auto& b : k.reseaux[static_cast<std::size_t>(rs)].broches) autres += (b.composant != p.composant || b.broche != n) && kp.parBroche.count({b.composant, b.broche});
      if (!trouValide(v.modele, t) || fautives.count({p.composant, n})) r[{p.composant, n}] = EtatPatte::Rouge;
      else if (autres == 0) r[{p.composant, n}] = EtatPatte::Libre;
      else r[{p.composant, n}] = EtatPatte::Verte;
    }
  return r;
}

// ======================================================================================================================
// Montage posé de lui-même
// ======================================================================================================================
VuePlatine monterSurPlatine(const ConceptionElec& c, int etapes) {
  VuePlatine v;
  const Connectivite k = connectivite(c);
  std::vector<const ComposantElec*> comps = composantsPoses(c);
  auto broches = [&](const ComposantElec& x) {
    std::vector<std::string> b;
    if (const DefSymbole* d = c.symbole(x.symbole))
      for (const auto& p : d->broches)
        if (std::find(b.begin(), b.end(), p.numero) == b.end()) b.push_back(p.numero);
    std::sort(b.begin(), b.end(), [](const std::string& a, const std::string& z) { return std::atoi(a.c_str()) != std::atoi(z.c_str()) ? std::atoi(a.c_str()) < std::atoi(z.c_str()) : a < z; });
    return b;
  };
  auto estCi = [&](const ComposantElec& x, std::size_t n) { return n > 2 && n % 2 == 0 && x.symbole != "Pile" && x.symbole.rfind("Q_", 0) != 0 && x.symbole != "Potentiometre"; };
  // Les circuits intégrés d'abord (à cheval sur la rainure), puis les autres
  std::stable_sort(comps.begin(), comps.end(), [&](const ComposantElec* a, const ComposantElec* b) { return estCi(*a, broches(*a).size()) && !estCi(*b, broches(*b).size()); });
  // Réseau d'une broche : vide quand elle ne rejoint aucune autre broche (rien à câbler)
  auto reseauDe = [&](const std::string& composant, const std::string& n) {
    const int r = k.reseau(composant, n);
    return r >= 0 && k.reseaux[static_cast<std::size_t>(r)].broches.size() > 1 ? k.reseaux[static_cast<std::size_t>(r)].nom : std::string();
  };
  auto couleur = [](const std::string& reseau) { return reseauMasse(reseau) ? std::string("#212121") : reseauAlimentation(reseau) ? std::string("#D32F2F") : std::string("#1565C0"); };
  auto fil = [&](const Trou& a, const Trou& b, const std::string& reseau) {
    FilPlatine f;
    f.uuid = nouvelUuid();
    f.a = a;
    f.b = b;
    f.couleur = couleur(reseau);
    v.fils.push_back(f);
  };
  // Rails : la première masse sur la paire « -h » et « -b », la première alimentation sur « +h » et « +b » ; le rail
  // du bas se relie à celui du haut par un fil au premier usage
  std::map<std::string, char> paire;  // réseau → '-' ou '+'
  std::set<std::string> railsPontes;
  std::map<std::string, int> prochainRail{{"+h", 2}, {"-h", 2}, {"+b", 2}, {"-b", 2}};
  auto railDe = [&](const std::string& reseau, bool bas) -> std::string {
    if (reseau.empty()) return {};
    auto it = paire.find(reseau);
    if (it == paire.end()) {
      const bool masse = reseauMasse(reseau), alim = !masse && reseauAlimentation(reseau);
      char signe = 0;
      if (masse || alim) {
        signe = masse ? '-' : '+';
        for (const auto& [n, s] : paire)
          if (s == signe) signe = 0;  // la paire est prise par un autre réseau
      }
      it = paire.emplace(reseau, signe).first;
    }
    if (!it->second) return {};
    const std::string r = std::string(1, it->second) + (bas ? "b" : "h");
    if (bas && !railsPontes.count(r)) {
      railsPontes.insert(r);
      fil({1, std::string(1, it->second) + "h"}, {1, r}, reseau);
    }
    return r;
  };
  auto trouDuRail = [&](const std::string& r, int colonneVoulue) {
    int& n = prochainRail[r];
    n = std::max(n, std::clamp(colonneVoulue, 2, v.modele.colonnes));
    const Trou t{std::min(n, v.modele.colonnes), r};
    ++n;
    return t;
  };
  // Bande d'accueil de chaque réseau : ses trous libres. Chaque patte a sa demi-bande ; elle devient l'accueil de son
  // réseau, ou se relie à l'accueil (ou au rail) par un fil.
  using Bande = std::vector<Trou>;
  std::map<std::string, Bande> accueil;
  auto prendre = [](Bande& b) {
    const Trou t = b.front();
    b.erase(b.begin());
    return t;
  };
  auto rejoindre = [&](Bande propre, const std::string& reseau, bool bas) {
    if (reseau.empty() || propre.empty()) return;
    if (const std::string r = railDe(reseau, bas); !r.empty()) {
      fil(prendre(propre), trouDuRail(r, propre.front().colonne), reseau);
      return;
    }
    auto it = accueil.find(reseau);
    if (it == accueil.end()) {
      accueil.emplace(reseau, propre);
      return;
    }
    if (it->second.size() >= 2) {
      fil(prendre(propre), prendre(it->second), reseau);
      if (propre.size() > it->second.size()) it->second = propre;
    } else {
      fil(prendre(it->second), prendre(propre), reseau);
      it->second = propre;
    }
  };
  auto demiBande = [](int col, bool bas, const std::string& sauf) {
    Bande b;
    for (const char* r : bas ? std::vector<const char*>{"f", "g", "h", "i", "j"} : std::vector<const char*>{"e", "d", "c", "b", "a"})
      if (r != sauf) b.push_back({col, r});
    return b;
  };
  int colonne = 3;
  int curseur[2] = {0, 0};  // haut, bas : première colonne libre des composants discrets
  bool discrets = false;
  int poses = 0;
  for (const ComposantElec* x : comps) {
    if (etapes >= 0 && poses >= etapes) break;
    const std::vector<std::string> b = broches(*x);
    if (b.empty()) continue;
    PosePlatine p;
    p.composant = x->uuid;
    if (estCi(*x, b.size())) {
      // Boîtier DIP vu de dessus, encoche à gauche : broches 1 à n/2 sur la rangée f de gauche à droite, les
      // suivantes sur la rangée e de droite à gauche ; chaque broche garde les quatre autres trous de sa demi-bande
      const int n = static_cast<int>(b.size()), moitie = n / 2, c0 = colonne;
      for (int i = 0; i < n; ++i) {
        const bool bas = i < moitie;
        const int col = bas ? c0 + i : c0 + (n - 1 - i);
        const Trou t{col, bas ? "f" : "e"};
        p.broches[b[static_cast<std::size_t>(i)]] = t;
        rejoindre(demiBande(col, bas, t.rangee), reseauDe(x->uuid, b[static_cast<std::size_t>(i)]), bas);
      }
      colonne = c0 + moitie + 1;
    } else {
      // Composant discret : ses pattes sur des demi-bandes voisines (deux colonnes d'écart pour un composant axial),
      // dans la moitié la moins avancée de la platine, puis chaque bande reliée à son réseau
      if (!discrets) curseur[0] = curseur[1] = colonne, discrets = true;
      const bool axial = b.size() == 2 && (x->symbole == "R" || x->symbole == "C" || x->symbole == "L" || x->symbole == "D" || x->symbole == "D_Zener" || x->symbole == "Fusible");
      const int pas = axial ? 2 : 1;
      const int moitie = curseur[1] < curseur[0] ? 1 : 0;
      const bool bas = moitie == 1;
      const int c0 = curseur[moitie];
      const std::string rangee = bas ? "h" : "c";
      for (std::size_t i = 0; i < b.size(); ++i) {
        const int col = c0 + static_cast<int>(i) * pas;
        p.broches[b[i]] = {std::min(col, v.modele.colonnes), rangee};
        rejoindre(demiBande(std::min(col, v.modele.colonnes), bas, rangee), reseauDe(x->uuid, b[i]), bas);
      }
      curseur[moitie] = c0 + static_cast<int>(b.size() - 1) * pas + 2;
    }
    v.poses.push_back(p);
    ++poses;
  }
  return v;
}

// ======================================================================================================================
// Synchronisation
// ======================================================================================================================
namespace {
void etiqueterBroche(ConceptionElec& c, const std::string& composant, const std::string& broche, const std::string& reseau) {
  for (auto& f : c.feuilles)
    for (const auto& sp : std::vector<SymbolePlace>(f.symboles)) {
      if (sp.composant != composant) continue;
      const DefSymbole* d = c.symbole(sp.symbole);
      if (!d) continue;
      for (const auto& b : brochesPlacees(sp, *d)) {
        if ((b.numero.empty() ? b.broche->numero : b.numero) != broche) continue;
        const Pt dir = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
        const Pt bout{b.p.x - dir.x * 2'540'000, b.p.y - dir.y * 2'540'000};
        f.fils.push_back({nouvelUuid(), b.p, bout});
        Etiquette e;
        e.uuid = nouvelUuid();
        e.texte = reseau;
        e.p = bout;
        e.type = TypeEtiquette::Globale;
        f.etiquettes.push_back(e);
        return;
      }
    }
}
}  // namespace

std::string ajouterFilPlatine(ConceptionElec& c, FilPlatine f, std::string* erreur) {
  VuePlatine v = platineDe(c);
  auto refuser = [&](const std::string& e) {
    if (erreur) *erreur = e;
    return std::string();
  };
  if (!trouValide(v.modele, f.a) || !trouValide(v.modele, f.b)) return refuser("trou hors de la platine");
  const ConnectivitePlatine kp = connectivitePlatine(v);
  const int na = kp.noeudDuTrou(f.a), nb = kp.noeudDuTrou(f.b);
  if (f.uuid.empty()) f.uuid = nouvelUuid();
  v.fils.push_back(f);
  ecrirePlatine(c, v);
  if (na < 0 || nb < 0 || na == nb || kp.noeuds[static_cast<std::size_t>(na)].empty() || kp.noeuds[static_cast<std::size_t>(nb)].empty()) return {};
  // Une broche de chaque côté : le schéma les relie
  const NoeudBroche pa = kp.noeuds[static_cast<std::size_t>(na)].front(), pb = kp.noeuds[static_cast<std::size_t>(nb)].front();
  const Connectivite k = connectivite(c);
  const int ra = k.reseau(pa.composant, pa.broche), rb = k.reseau(pb.composant, pb.broche);
  if (ra >= 0 && ra == rb) return k.reseaux[static_cast<std::size_t>(ra)].nom;
  // Le nom d'un réseau nommé, sinon un nom neuf posé des deux côtés
  std::string nom;
  bool deuxCotes = false;
  if (rb >= 0 && k.reseaux[static_cast<std::size_t>(rb)].nomme) nom = k.reseaux[static_cast<std::size_t>(rb)].nom;
  else if (ra >= 0 && k.reseaux[static_cast<std::size_t>(ra)].nomme) {
    nom = k.reseaux[static_cast<std::size_t>(ra)].nom;
    etiqueterBroche(c, pb.composant, pb.broche, nom);
    return nom;
  } else {
    const ComposantElec* x = c.composant(pa.composant);
    nom = "P_" + (x ? x->reference : std::string("X")) + "_" + pa.broche;
    deuxCotes = true;
  }
  etiqueterBroche(c, pa.composant, pa.broche, nom);
  if (deuxCotes) etiqueterBroche(c, pb.composant, pb.broche, nom);
  return nom;
}

// ======================================================================================================================
// Documents
// ======================================================================================================================
bool deplacerSurPlatine(VuePlatine& v, const std::string& composant, int colonnes, int rangeesDecalage, std::string* erreur) {
  auto refuser = [&](const std::string& e) {
    if (erreur) *erreur = e;
    return false;
  };
  PosePlatine* p = v.pose(composant);
  if (!p) return refuser("composant absent de la platine");
  auto decaler = [&](const Trou& t) -> std::optional<Trou> {
    Trou u = t;
    u.colonne += colonnes;
    if (!estRail(t.rangee)) {
      const int i = static_cast<int>(std::find(rangees().begin(), rangees().end(), t.rangee) - rangees().begin()), j = i + rangeesDecalage;
      if (j < 0 || j > 9 || (i < 5) != (j < 5)) return std::nullopt;  // la rainure sépare les moitiés
      u.rangee = rangees()[static_cast<std::size_t>(j)];
    }
    if (!trouValide(v.modele, u)) return std::nullopt;
    return u;
  };
  // Demi-bandes que ses pattes sont seules à occuper : les bouts de fils qui s'y trouvent le suivent
  std::set<std::string> siennes;
  for (const auto& [n, t] : p->broches)
    if (!estRail(t.rangee)) siennes.insert(bandeDuTrou(t));
  for (const auto& autre : v.poses)
    if (autre.composant != composant)
      for (const auto& [n, t] : autre.broches) siennes.erase(bandeDuTrou(t));
  std::map<std::string, Trou> pattes;
  for (const auto& [n, t] : p->broches) {
    const auto u = decaler(t);
    if (!u) return refuser("la patte " + n + " sortirait de la platine ou franchirait la rainure");
    pattes[n] = *u;
  }
  std::vector<FilPlatine> fils = v.fils;
  for (auto& f : fils)
    for (Trou* bout : {&f.a, &f.b})
      if (!estRail(bout->rangee) && siennes.count(bandeDuTrou(*bout))) {
        const auto u = decaler(*bout);
        if (!u) return refuser("un fil relié au composant sortirait de la platine");
        *bout = *u;
      }
  // Trous pris après le déplacement : un trou ne reçoit qu'une patte ou un bout de fil
  std::set<Trou> occupes;
  for (const auto& autre : v.poses)
    if (autre.composant != composant)
      for (const auto& [n, t] : autre.broches) occupes.insert(t);
  for (const auto& f : fils) {
    if (occupes.count(f.a) || occupes.count(f.b)) return refuser("un fil arriverait dans un trou occupé");
    occupes.insert(f.a), occupes.insert(f.b);
  }
  for (const auto& [n, t] : pattes) {
    if (occupes.count(t)) return refuser("le trou " + texteTrou(t) + " est déjà occupé");
    occupes.insert(t);
  }
  // Une patte qui quitte une bande où restent d'autres pattes ou des fils garde sa liaison par un fil tiré de
  // l'ancienne bande à la nouvelle
  auto trouLibre = [&](const Trou& dansBande) -> std::optional<Trou> {
    std::vector<Trou> candidats;
    if (estRail(dansBande.rangee))
      for (int col = 1; col <= v.modele.colonnes; ++col) candidats.push_back({col, dansBande.rangee});
    else {
      const int i = static_cast<int>(std::find(rangees().begin(), rangees().end(), dansBande.rangee) - rangees().begin());
      for (int j = i < 5 ? 0 : 5; j < (i < 5 ? 5 : 10); ++j) candidats.push_back({dansBande.colonne, rangees()[static_cast<std::size_t>(j)]});
    }
    // Le plus proche du trou donné
    std::sort(candidats.begin(), candidats.end(), [&](const Trou& a, const Trou& b) { return std::abs(a.colonne - dansBande.colonne) < std::abs(b.colonne - dansBande.colonne); });
    for (const auto& t : candidats)
      if (!occupes.count(t)) return t;
    return std::nullopt;
  };
  std::set<std::pair<std::string, std::string>> relies;
  for (const auto& [n, t] : p->broches) {
    const std::string ancienne = bandeDuTrou(t), nouvelle = bandeDuTrou(pattes.at(n));
    if (ancienne == nouvelle || relies.count({ancienne, nouvelle})) continue;
    bool habitee = false;
    for (const auto& autre : v.poses)
      if (autre.composant != composant)
        for (const auto& [m, x] : autre.broches) habitee |= bandeDuTrou(x) == ancienne;
    for (const auto& f : fils) habitee |= bandeDuTrou(f.a) == ancienne || bandeDuTrou(f.b) == ancienne;
    if (!habitee) continue;
    const auto a = trouLibre(t), b = trouLibre(pattes.at(n));
    if (!a || !b) return refuser("plus de trou libre pour garder la liaison de la patte " + n);
    occupes.insert(*a), occupes.insert(*b);
    FilPlatine f;
    f.uuid = nouvelUuid();
    f.a = *a;
    f.b = *b;
    f.couleur = "#43A047";
    fils.push_back(f);
    relies.insert({ancienne, nouvelle});
  }
  p->broches = pattes;
  v.fils = fils;
  return true;
}

// ======================================================================================================================
// Impression, plaque à bandes et 3D
// ======================================================================================================================
ModeleTypon modeleTyponPlatine(const VuePlatine& v, const OptionsTypon& o, bool coteCuivre) {
  ModeleTypon m;
  m.miroir = coteCuivre != o.miroirTransfert;
  const double marge = o.marge, largeurDessin = (v.modele.colonnes + 2) * 2.54;
  const double ex = o.echelleX > 0 ? o.echelleX : 1, ey = o.echelleY > 0 ? o.echelleY : 1;
  m.echelleX = ex;
  m.echelleY = ey;
  auto place = [&](double x, double y) {
    double qx = x + marge;
    if (m.miroir) qx = largeurDessin + 2 * marge - qx;
    return Pt{nm(qx * ex), nm((y + marge) * ey)};
  };
  auto rectangle = [&](double x0, double y0, double x1, double y1) {
    Polygone r{place(x0, y0), place(x1, y0), place(x1, y1), place(x0, y1)};
    if (m.miroir) std::reverse(r.begin(), r.end());
    return r;
  };
  m.largeur = std::max(90.0, (largeurDessin + 2 * marge) * ex);
  m.hauteurCarte = (hauteurPlatine + 2 * marge) * ey;
  m.hauteur = m.hauteurCarte + 14;
  m.contour = rectangle(0, 0, largeurDessin, hauteurPlatine);
  // Bandes de cuivre : une par demi-colonne, une par rail
  auto y = [](const char* r) { return mm(positionTrou({1, r}).y); };
  for (int col = 1; col <= v.modele.colonnes; ++col) {
    const double x = col * 2.54;
    m.cuivre.push_back({rectangle(x - 0.9, y("a") - 1.1, x + 0.9, y("e") + 1.1)});
    m.cuivre.push_back({rectangle(x - 0.9, y("f") - 1.1, x + 0.9, y("j") + 1.1)});
  }
  for (const char* r : {"-h", "+h", "+b", "-b"}) m.cuivre.push_back({rectangle(2.54 - 1.1, y(r) - 0.9, v.modele.colonnes * 2.54 + 1.1, y(r) + 0.9)});
  std::vector<std::string> tout = rangees();
  for (const char* r : {"+h", "-h", "+b", "-b"}) tout.push_back(r);
  for (int col = 1; col <= v.modele.colonnes; ++col)
    for (const auto& r : tout) {
      const Pt p = positionTrou({col, r});
      m.trous.push_back({place(mm(p.x), mm(p.y)), nm(1.0)});
    }
  // Côté composants, les fils se voient ; côté cuivre, ils sont dessous
  if (!coteCuivre)
    for (const auto& f : v.fils) {
      const Pt a = positionTrou(f.a), b = positionTrou(f.b);
      m.traits.push_back({place(mm(a.x), mm(a.y)), place(mm(b.x), mm(b.y)), nm(0.6)});
    }
  return m;
}

std::vector<Trou> coupuresBandes(const VuePlatine& v) {
  // Plaque à bandes continues de a à j : la coupure entre e et f sépare les deux moitiés d'une colonne quand elles
  // portent deux potentiels ; deux moitiés vides ou de même nœud restent d'un seul tenant
  const ConnectivitePlatine k = connectivitePlatine(v);
  std::vector<Trou> r;
  for (int col = 1; col <= v.modele.colonnes; ++col) {
    const int h = k.noeudDuTrou({col, "e"}), b = k.noeudDuTrou({col, "f"});
    if (h >= 0 && b >= 0 && h != b) r.push_back({col, "e"});  // coupure sous le trou e, vers f
  }
  return r;
}

std::string hpglPlaque(const VuePlatine& v) {
  // Géométrie de la plaque à bandes : pas uniforme de 2,54 mm, rangées a à j de suite (sans rainure)
  auto X = [](double mmx) { return static_cast<long>(std::lround(mmx / 0.025)); };
  auto ligne = [&](std::ostringstream& o, double x0, double y0, double x1, double y1) { o << "PU" << X(x0) << "," << X(y0) << ";PD" << X(x1) << "," << X(y1) << ";"; };
  const double y0 = 2.54, yj = y0 + 9 * 2.54;
  std::ostringstream o;
  o << "IN;SP1;\n";
  // Sillons entre bandes (gravure d'une plaque nue en plaque à bandes)
  for (int col = 1; col < v.modele.colonnes; ++col) {
    const double x = col * 2.54 + 1.27;
    ligne(o, x, y0 - 1.27, x, yj + 1.27);
    o << "\n";
  }
  // Coupures des bandes entre e et f
  o << "SP2;\n";
  for (const auto& t : coupuresBandes(v)) {
    const double x = t.colonne * 2.54, y = y0 + 4.5 * 2.54;
    ligne(o, x - 1.0, y, x + 1.0, y);
    o << "\n";
  }
  // Perçages : trous occupés par une patte ou un fil, marqués d'une croix
  o << "SP3;\n";
  std::set<Trou> occupes;
  for (const auto& p : v.poses)
    for (const auto& [n, t] : p.broches) occupes.insert(t);
  for (const auto& f : v.fils) occupes.insert(f.a), occupes.insert(f.b);
  for (const auto& t : occupes) {
    if (estRail(t.rangee)) continue;
    const int i = static_cast<int>(std::find(rangees().begin(), rangees().end(), t.rangee) - rangees().begin());
    const double x = t.colonne * 2.54, y = y0 + i * 2.54;
    o << "PU" << X(x) << "," << X(y) << ";PD;PU;\n";
  }
  o << "SP0;PU0,0;\n";
  return o.str();
}

std::vector<Objet3DPlatine> scenePlatine3D(const ConceptionElec& c, const VuePlatine& v) {
  std::vector<Objet3DPlatine> r;
  const double largeur = (v.modele.colonnes + 2) * 2.54;
  r.push_back({"platine", "", "", {{0, 0, -8.5}, {largeur, hauteurPlatine, 0}}, 0, "#F4F1E8"});
  auto P = [](const Trou& t) {
    const Pt p = positionTrou(t);
    return std::array<double, 3>{mm(p.x), mm(p.y), 0};
  };
  for (const auto& p : v.poses) {
    const ComposantElec* x = c.composant(p.composant);
    if (!x || p.broches.empty()) continue;
    double cx = 0, cy = 0, minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const auto& [n, t] : p.broches) {
      const auto q = P(t);
      cx += q[0], cy += q[1];
      minX = std::min(minX, q[0]), maxX = std::max(maxX, q[0]), minY = std::min(minY, q[1]), maxY = std::max(maxY, q[1]);
    }
    cx /= static_cast<double>(p.broches.size()), cy /= static_cast<double>(p.broches.size());
    std::string genre = "corps", couleur = "#D7C49E";
    double dx = 3.2, dy = 1.3, h = 2.6, z0 = 2.0;
    const std::string& s = x->symbole;
    const bool ci = p.broches.size() > 2 && p.broches.size() % 2 == 0 && maxY - minY > 5;
    if (ci) dx = (maxX - minX) / 2 + 1.3, dy = (maxY - minY) / 2 - 1.0, h = 3.3, z0 = 0.6, couleur = "#263238";
    else if (s == "LED") genre = "led", dx = dy = 2.5, h = 5.5, z0 = 3.0, couleur = "#E53935";
    else if (s == "Interrupteur" || s == "Bouton" || s.rfind("SW", 0) == 0) genre = "interrupteur", dx = 3.0, dy = 1.8, h = 3.5, z0 = 0.5, couleur = "#455A64";
    else if (s == "C" || s == "C_Pol") dx = dy = 1.6, h = 5.0, z0 = 3.0, couleur = s == "C" ? "#F9A825" : "#1565C0";
    else if (s == "R") couleur = "#D7C49E";
    else if (s.rfind("Q_", 0) == 0) dx = 2.3, dy = 1.0, h = 4.5, z0 = 3.0, couleur = "#212121";
    else couleur = "#607D8B";
    r.push_back({genre, x->uuid, x->reference, {{cx - dx, cy - dy, z0}, {cx + dx, cy + dy, z0 + h}}, 0, couleur});
    // Pattes : du trou, droit vers le haut, puis vers le corps
    for (const auto& [n, t] : p.broches) {
      const auto q = P(t);
      const double zc = ci ? z0 + 0.5 : z0 + h / 2;
      r.push_back({"patte", x->uuid, x->reference, {q, {q[0], q[1], ci ? zc : 1.5}, {std::clamp(q[0], cx - dx, cx + dx), std::clamp(q[1], cy - dy, cy + dy), zc}}, 0.25, "#B0BEC5"});
    }
  }
  for (const auto& f : v.fils) {
    const auto a = P(f.a), b = P(f.b);
    const double l = std::hypot(b[0] - a[0], b[1] - a[1]), arche = std::min(12.0, 2.0 + l * 0.12);
    std::vector<std::array<double, 3>> axe{a, {a[0], a[1], 1.5}};
    if (f.courbe)
      for (int i = 1; i < 8; ++i) {
        const double s = i / 8.0;
        axe.push_back({a[0] + (b[0] - a[0]) * s, a[1] + (b[1] - a[1]) * s, 1.5 + arche * 4 * s * (1 - s)});
      }
    else
      axe.push_back({a[0], a[1], 1.5 + arche}), axe.push_back({b[0], b[1], 1.5 + arche});
    axe.push_back({b[0], b[1], 1.5});
    axe.push_back(b);
    r.push_back({"fil", "", "", axe, std::max(0.3, std::sqrt(f.section / 3.14159) * 0.9), f.couleur});
  }
  return r;
}

std::string svgCoupures(const VuePlatine& v) {
  // Côté cuivre de la plaque à bandes, en miroir : bandes, coupures (croix rouges) et trous percés
  std::ostringstream o;
  const double l = (v.modele.colonnes + 2) * 2.54, h = 12 * 2.54;
  auto X = [&](int col) { return l - col * 2.54; };
  o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << l << "mm\" height=\"" << h << "mm\" viewBox=\"0 0 " << l << " " << h << "\">\n";
  o << "<rect x=\"0\" y=\"0\" width=\"" << l << "\" height=\"" << h << "\" fill=\"#E8D9B5\"/>\n";
  for (int col = 1; col <= v.modele.colonnes; ++col)
    o << "<rect x=\"" << X(col) - 0.9 << "\" y=\"" << 2.54 - 1.1 << "\" width=\"1.8\" height=\"" << 9 * 2.54 + 2.2 << "\" fill=\"#C87533\"/>\n";
  for (const auto& t : coupuresBandes(v)) {
    const double x = X(t.colonne), y = 2.54 + 4.5 * 2.54;
    o << "<path d=\"M" << x - 1.1 << " " << y - 1.1 << " L" << x + 1.1 << " " << y + 1.1 << " M" << x - 1.1 << " " << y + 1.1 << " L" << x + 1.1 << " " << y - 1.1
      << "\" stroke=\"#D32F2F\" stroke-width=\"0.4\"/>\n";
  }
  o << "<text x=\"2\" y=\"" << h - 1.5 << "\" font-size=\"2\" font-family=\"sans-serif\">Côté cuivre, en miroir : " << coupuresBandes(v).size() << " coupure(s) entre e et f</text>\n</svg>\n";
  return o.str();
}

std::vector<LigneFil> listeFils(const VuePlatine& v) {
  std::vector<LigneFil> r;
  for (const auto& f : v.fils) {
    const Pt a = positionTrou(f.a), b = positionTrou(f.b);
    r.push_back({texteTrou(f.a), texteTrou(f.b), f.couleur, std::hypot(static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y)) / 1e6 + 10});
  }
  return r;
}

std::string planDeChargement(const ConceptionElec& c, const VuePlatine& v) {
  std::ostringstream o;
  o << "Plan de chargement (" << v.modele.nom << ")\n";
  for (const auto& p : v.poses) {
    const ComposantElec* x = c.composant(p.composant);
    o << (x ? x->reference + " " + x->valeur : p.composant) << " :";
    for (const auto& [n, t] : p.broches) o << " " << n << "→" << texteTrou(t);
    o << "\n";
  }
  o << "Fils :\n";
  for (const auto& l : listeFils(v)) {
    char lg[32];
    std::snprintf(lg, sizeof lg, "%.0f", l.longueur);
    o << "  " << l.de << " → " << l.a << " (" << l.couleur << ", " << lg << " mm)\n";
  }
  return o.str();
}

std::string svgPlatine(const ConceptionElec& c, const VuePlatine& v) {
  std::ostringstream o;
  const double l = (v.modele.colonnes + 2) * 2.54, h = hauteurPlatine;
  o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << l << "mm\" height=\"" << h << "mm\" viewBox=\"0 0 " << l << " " << h << "\">\n";
  o << "<rect x=\"0\" y=\"0\" width=\"" << l << "\" height=\"" << h << "\" fill=\"#F4F1E8\" stroke=\"#999\" stroke-width=\"0.2\"/>\n";
  std::vector<std::string> tout = rangees();
  for (const char* r : {"+h", "-h", "+b", "-b"}) tout.push_back(r);
  for (int col = 1; col <= v.modele.colonnes; ++col)
    for (const auto& r : tout) {
      const Pt p = positionTrou({col, r});
      o << "<rect x=\"" << p.x / 1e6 - 0.4 << "\" y=\"" << p.y / 1e6 - 0.4 << "\" width=\"0.8\" height=\"0.8\" fill=\"#555\"/>\n";
    }
  for (const auto& f : v.fils) {
    const Pt a = positionTrou(f.a), b = positionTrou(f.b);
    o << "<line x1=\"" << a.x / 1e6 << "\" y1=\"" << a.y / 1e6 << "\" x2=\"" << b.x / 1e6 << "\" y2=\"" << b.y / 1e6 << "\" stroke=\"" << f.couleur << "\" stroke-width=\"0.6\"/>\n";
  }
  for (const auto& p : v.poses) {
    const ComposantElec* x = c.composant(p.composant);
    std::vector<Pt> pts;
    for (const auto& [n, t] : p.broches) pts.push_back(positionTrou(t));
    for (std::size_t i = 1; i < pts.size(); ++i)
      o << "<line x1=\"" << pts[i - 1].x / 1e6 << "\" y1=\"" << pts[i - 1].y / 1e6 << "\" x2=\"" << pts[i].x / 1e6 << "\" y2=\"" << pts[i].y / 1e6
        << "\" stroke=\"#8D6E63\" stroke-width=\"0.8\"/>\n";
    if (!pts.empty() && x) o << "<text x=\"" << pts[0].x / 1e6 << "\" y=\"" << pts[0].y / 1e6 - 1 << "\" font-size=\"1.6\">" << x->reference << "</text>\n";
  }
  o << "</svg>\n";
  return o.str();
}

// ======================================================================================================================
// Guitare
// ======================================================================================================================
std::vector<std::vector<std::string>> contactsSelecteur5() { return {{"1"}, {"1", "2"}, {"2"}, {"2", "3"}, {"3"}}; }

std::vector<PositionGuitare> analyserGuitare(const ConceptionElec& c, const std::string& selecteur, const std::string& sortie) {
  std::vector<PositionGuitare> r;
  const Connectivite k = connectivite(c);
  const ComposantElec* sel = c.parReference(selecteur);
  const ComposantElec* jack = c.parReference(sortie);
  if (!sel || !jack) return r;
  const int n = static_cast<int>(k.reseaux.size());
  const auto contacts = contactsSelecteur5();
  for (std::size_t pos = 0; pos < contacts.size(); ++pos) {
    Ensembles e;
    for (int i = 0; i < n; ++i) e.ajouter();
    auto unir = [&](const std::string& ca, const std::string& ba, const std::string& cb, const std::string& bb) {
      const int a = k.reseau(ca, ba), b = k.reseau(cb, bb);
      if (a >= 0 && b >= 0) e.unir(a, b);
    };
    for (const auto& ct : contacts[pos]) unir(sel->uuid, "C", sel->uuid, ct);
    for (const auto& x : c.composants)
      if (x.symbole == "Potentiometre") unir(x.uuid, "3", x.uuid, "2");  // le signal passe de l'entrée au curseur
    const int a = k.reseau(jack->uuid, "1"), b = k.reseau(jack->uuid, "2");
    PositionGuitare p;
    p.position = static_cast<int>(pos) + 1;
    if (a < 0 || b < 0) {
      r.push_back(p);
      continue;
    }
    const int chaud = e.trouver(a), masse = e.trouver(b);
    // Les micros sont des arêtes entre deux ensembles : chemins de « chaud » à « masse »
    struct Arete {
      std::string ref, nom;
      int u, w;
    };
    std::vector<Arete> aretes;
    for (const auto& x : c.composants) {
      if (x.symbole != "Micro_guitare") continue;
      const int h = k.reseau(x.uuid, "1"), f = k.reseau(x.uuid, "2");
      if (h < 0 || f < 0) continue;
      aretes.push_back({x.reference, x.valeur.empty() ? x.reference : x.valeur, e.trouver(h), e.trouver(f)});
    }
    std::vector<std::vector<std::size_t>> chemins;
    std::vector<std::size_t> courant;
    std::set<int> vus{chaud};
    std::function<void(int)> chercher = [&](int s) {
      if (s == masse && !courant.empty()) {
        chemins.push_back(courant);
        return;
      }
      for (std::size_t i = 0; i < aretes.size(); ++i) {
        if (std::find(courant.begin(), courant.end(), i) != courant.end()) continue;
        const int t = aretes[i].u == s ? aretes[i].w : aretes[i].w == s ? aretes[i].u : -1;
        if (t < 0 || (t != masse && vus.count(t))) continue;
        vus.insert(t);
        courant.push_back(i);
        chercher(t);
        courant.pop_back();
        vus.erase(t);
      }
    };
    chercher(chaud);
    std::set<std::size_t> entendus;
    bool serie = false, horsPhase = false;
    for (const auto& ch : chemins) {
      serie |= ch.size() > 1;
      for (std::size_t i : ch) entendus.insert(i);
    }
    // Hors phase : un micro entendu dont le froid est du côté du chaud
    for (std::size_t i : entendus) horsPhase |= aretes[i].w == chaud;
    for (std::size_t i : entendus) p.micros.push_back(aretes[i].ref);
    p.couplage = entendus.empty() ? "aucun" : entendus.size() == 1 ? "seul" : serie ? "série" : "parallèle";
    p.horsPhase = horsPhase && entendus.size() > 1;
    std::vector<std::string> noms;
    for (std::size_t i : entendus) noms.push_back(aretes[i].nom);
    std::sort(noms.begin(), noms.end(), [](const std::string& x, const std::string& y) {
      static const std::vector<std::string> ordre{"manche", "milieu", "chevalet"};
      const auto ix = std::find(ordre.begin(), ordre.end(), x), iy = std::find(ordre.begin(), ordre.end(), y);
      return ix != iy ? ix < iy : x < y;
    });
    if (noms.empty()) p.texte = "aucun micro";
    else if (noms.size() == 1) p.texte = noms[0] + " seul";
    else {
      std::string t;
      for (std::size_t i = 0; i < noms.size(); ++i) t += (i == 0 ? "" : i + 1 == noms.size() ? " et " : ", ") + noms[i];
      p.texte = t + " en " + p.couplage + (p.horsPhase ? ", hors phase" : "");
    }
    r.push_back(p);
  }
  return r;
}

ConceptionElec exempleGuitareTroisMicros() {
  ConceptionElec c = nouvelleConception("Guitare à trois micros");
  auto symbole = [&](const std::string& id, const std::string& nom, const std::string& prefixe, const std::vector<std::pair<std::string, std::string>>& b) {
    SpecSymbole s;
    s.id = id;
    s.nom = nom;
    s.prefixe = prefixe;
    s.categorie = "Guitare";
    for (const auto& [n, x] : b) {
      SpecBroche p;
      p.numero = n;
      p.nom = x;
      p.type = TypeBroche::Passive;
      s.broches.push_back(p);
    }
    c.symboles[id] = genererSymbole(s);
  };
  symbole("Micro_guitare", "Micro de guitare", "MIC", {{"1", "chaud"}, {"2", "froid"}});
  symbole("Selecteur_5", "Sélecteur à cinq positions", "SEL", {{"C", "C"}, {"1", "1"}, {"2", "2"}, {"3", "3"}});
  symbole("Jack", "Jack de sortie", "J", {{"1", "pointe"}, {"2", "corps"}});
  double x = 25.4;
  auto poser = [&](const std::string& sym, const std::string& ref, const std::string& valeur, const std::map<std::string, std::string>& l) {
    const std::string s = poserSymbole(c, 0, sym, ptMm(x, 60));
    x += 35.56;
    for (const auto& sp : std::vector<SymbolePlace>(c.feuilles[0].symboles))
      if (sp.uuid == s) {
        if (ComposantElec* k = c.composant(sp.composant)) {
          k->reference = ref;
          k->valeur = valeur;
        }
        for (const auto& [n, reseau] : l) etiqueterBroche(c, sp.composant, n, reseau);
      }
  };
  poser("Micro_guitare", "MIC1", "chevalet", {{"1", "S1"}, {"2", "GND"}});
  poser("Micro_guitare", "MIC2", "milieu", {{"1", "S2"}, {"2", "GND"}});
  poser("Micro_guitare", "MIC3", "manche", {{"1", "S3"}, {"2", "GND"}});
  poser("Selecteur_5", "SEL1", "5 positions", {{"C", "COMMUN"}, {"1", "S1"}, {"2", "S2"}, {"3", "S3"}});
  poser("Potentiometre", "RV1", "250k", {{"3", "COMMUN"}, {"2", "SORTIE"}, {"1", "GND"}});
  poser("Jack", "J1", "6,35 mm", {{"1", "SORTIE"}, {"2", "GND"}});
  return c;
}

}  // namespace prom::ecad

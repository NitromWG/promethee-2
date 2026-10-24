// Prométhée : piles de pastilles et de vias (4.24, 8.22, 8.23) — gabarits IPC-7351, via choisie par le tracé,
// contre-perçage. Licence GPL-3.0-only.
#include "promethee/piles_pastilles.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace prom::ecad {

GabaritPastille gabaritDepuisPastille(const Pastille& p, Nm margeVernis) {
  GabaritPastille g;
  g.forme = p.forme;
  g.type = p.type;
  g.largeur = p.largeur;
  g.hauteur = p.hauteur;
  g.percage = p.type == TypePastille::CMS ? 0 : p.percage;
  g.longueurPercage = p.type == TypePastille::CMS ? 0 : p.longueurPercage;
  g.arrondi = p.arrondi;
  g.chanfrein = p.chanfrein;
  g.connexion = p.connexion;
  g.margeVernis = p.margeVernis.value_or(margeVernis);
  g.margePate = p.margePate.value_or(0);
  g.nom = nomIPCGabarit(g);
  return g;
}

const std::vector<GabaritPastille>& gabaritsIntegres() {
  static const std::vector<GabaritPastille> v = [] {
    std::vector<GabaritPastille> r;
    auto cms = [&](double l, double h) {
      GabaritPastille g;
      g.forme = FormePastille::RectArrondi;
      g.largeur = nm(l);
      g.hauteur = nm(h);
      g.nom = nomIPCGabarit(g);
      r.push_back(g);
    };
    // Passifs IPC-7351B densité N (pastilles des générateurs de 0.20) et pattes de SOIC, QFP, QFN courantes
    cms(0.6, 0.5);    // 0402
    cms(0.95, 1.0);   // 0603
    cms(1.2, 1.4);    // 0805
    cms(1.2, 1.75);   // 1206
    cms(1.55, 0.6);   // SOIC 1,27 mm
    cms(1.5, 0.3);    // QFP 0,5 mm
    cms(0.8, 0.28);   // QFN 0,5 mm
    auto tht = [&](double d, double t) {
      GabaritPastille g;
      g.forme = FormePastille::Rond;
      g.type = TypePastille::Traversante;
      g.largeur = g.hauteur = nm(d);
      g.percage = nm(t);
      g.connexion = ConnexionZone::Thermique;
      g.nom = nomIPCGabarit(g);
      r.push_back(g);
    };
    tht(1.6, 0.8);
    tht(1.7, 1.0);
    tht(2.0, 1.2);
    tht(2.4, 1.3);
    return r;
  }();
  return v;
}

int lierAuxGabarits(DefEmpreinte& d, std::map<std::string, GabaritPastille>& gabarits) {
  int n = 0;
  for (auto& p : d.pastilles) {
    // Pastille couchée (plus haute que large) d'une forme symétrique : décrite debout et tournée d'un quart de tour,
    // pour que les pastilles des quatre côtés d'un QFP tiennent d'une même pile (même géométrie sur la carte)
    const bool symetrique = p.forme == FormePastille::Rect || p.forme == FormePastille::RectArrondi || p.forme == FormePastille::Ovale || p.forme == FormePastille::Rond;
    if (symetrique && p.hauteur > p.largeur && !p.fente() && p.decalagePercage == Pt{} && p.trousEnPlus.empty() && p.contourLibre.empty() && p.vernisLibre.empty() &&
        p.pateLibre.empty() && !p.motifPate) {
      std::swap(p.largeur, p.hauteur);
      p.rotation = std::fmod(p.rotation + 90.0, 360.0);
    }
    GabaritPastille g = gabaritDepuisPastille(p);
    const auto it = gabarits.find(g.nom);
    if (it == gabarits.end()) gabarits.emplace(g.nom, g);
    p.gabarit = g.nom;
    ++n;
  }
  return n;
}

int appliquerGabaritPartout(ConceptionElec& c, const std::string& ancien, const GabaritPastille& g) {
  exigerModifiable(c);
  int n = 0;
  for (auto& [id, d] : c.empreintes) {
    bool change = false;
    for (auto& p : d.pastilles)
      if (p.gabarit == ancien) {
        appliquerGabarit(p, g);
        ++n;
        change = true;
      }
    if (change) ++d.version;
  }
  if (ancien != g.nom) c.circuit.gabarits.erase(ancien);
  c.circuit.gabarits[g.nom] = g;
  return n;
}

Via viaPour(const Empilage& e, const Regles& r, const std::string& reseau, Couche de, Couche vers) {
  Via v;
  v.reseau = reseau;
  v.diametre = viaDiametre(r, reseau);
  v.percage = viaPercage(r, reseau);
  const int n = e.couchesCuivre;
  int a = rangCuivre(de, n), b = rangCuivre(vers, n);
  if (a < 0 || b < 0 || n <= 2) return v;  // simple ou double face : traversante
  if (a > b) std::swap(a, b);
  const bool exterieur = a == 0 || b == n - 1;
  const bool adjacentes = b - a == 1;
  const auto cu = couchesCuivre(n);
  for (const auto& genre : e.prioriteVias) {
    if (genre == "micro" && e.microVias && adjacentes && exterieur) {
      v.type = TypeVia::Micro;
      v.laser = true;
      v.diametre = e.microViaDiametre;
      v.percage = e.microViaPercage;
      v.de = cu[static_cast<std::size_t>(a)];
      v.vers = cu[static_cast<std::size_t>(b)];
      return v;
    }
    if (genre == "borgne" && e.viasBorgnes && exterieur && !(a == 0 && b == n - 1)) {
      v.type = TypeVia::Borgne;
      v.de = cu[static_cast<std::size_t>(a)];
      v.vers = cu[static_cast<std::size_t>(b)];
      return v;
    }
    if (genre == "enterree" && e.viasEnterrees && !exterieur) {
      v.type = TypeVia::Enterree;
      v.de = cu[static_cast<std::size_t>(a)];
      v.vers = cu[static_cast<std::size_t>(b)];
      return v;
    }
    if (genre == "traversante") return v;
  }
  return v;
}

BilanContrePercage bilanContrePercage(const Via& v, const Empilage& e) {
  BilanContrePercage b;
  if (!v.contrePercage) {
    b.possible = false;
    b.raison = "pas de contre-perçage";
    return b;
  }
  const ContrePercage& cp = *v.contrePercage;
  const int n = e.couchesCuivre;
  const int garde = rangCuivre(cp.jusqua, n);
  if (garde < 0 || n <= 2) {
    b.possible = false;
    b.raison = "couche gardée absente de l'empilage";
    return b;
  }
  // Cotes des faces de chaque cuivre depuis le dessus de la carte, d'après l'empilage détaillé
  const auto pile = pileEmpilage(e);
  std::vector<std::pair<double, double>> cuivres;  // (haut, bas) de chaque cuivre
  double z = 0;
  for (const auto& x : pile) {
    if (x.genre == GenreCoucheEmpilage::Cuivre) cuivres.push_back({z, z + x.epaisseur});
    z += x.epaisseur;
  }
  const double total = z;
  if (static_cast<int>(cuivres.size()) != n) {
    b.possible = false;
    b.raison = "empilage incomplet";
    return b;
  }
  const double talonMax = mm(cp.talonMax);
  const auto cu = couchesCuivre(n);
  if (cp.dessous) {
    // Depuis le dessous jusqu'à « talonMax » sous la face basse de la couche gardée ; chaque couche plus basse doit
    // être entièrement coupée
    const double fond = cuivres[static_cast<std::size_t>(garde)].second + talonMax;
    b.profondeur = total - fond;
    b.talon = talonMax;
    for (int i = garde + 1; i < n; ++i) {
      if (cuivres[static_cast<std::size_t>(i)].first < fond - 1e-9) {
        b.possible = false;
        b.raison = std::string(codeCouche(cu[static_cast<std::size_t>(i)])) + " est à moins de " + std::to_string(talonMax).substr(0, 4) + " mm de la couche gardée : le talon la laisserait";
      }
      b.coupees.push_back(cu[static_cast<std::size_t>(i)]);
    }
  } else {
    const double fond = cuivres[static_cast<std::size_t>(garde)].first - talonMax;
    b.profondeur = fond;
    b.talon = talonMax;
    for (int i = 0; i < garde; ++i) {
      if (cuivres[static_cast<std::size_t>(i)].second > fond + 1e-9) {
        b.possible = false;
        b.raison = std::string(codeCouche(cu[static_cast<std::size_t>(i)])) + " est à moins du talon de la couche gardée";
      }
      b.coupees.push_back(cu[static_cast<std::size_t>(i)]);
    }
  }
  if (b.profondeur <= 0) {
    b.possible = false;
    b.raison = "profondeur nulle";
  }
  return b;
}

std::vector<std::string> pistesSurCouchesCoupees(const ConceptionElec& c, const Via& v) {
  std::vector<std::string> r;
  const BilanContrePercage b = bilanContrePercage(v, c.circuit.empilage);
  if (b.coupees.empty()) return r;
  const std::set<Couche> coupees(b.coupees.begin(), b.coupees.end());
  for (const auto& p : c.circuit.pistes)
    if (p.reseau == v.reseau && coupees.count(p.couche) &&
        (distance(p.a, v.p) <= static_cast<double>(v.diametre) / 2 + 1 || distance(p.b, v.p) <= static_cast<double>(v.diametre) / 2 + 1))
      r.push_back(p.uuid);
  return r;
}

}  // namespace prom::ecad

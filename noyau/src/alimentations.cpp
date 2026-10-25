// Prométhée : alimentations — arbre des rails, abaisseur et sa boucle, magnétiques (lot L14 de la 0.34 ; 14.14). Voir
// alimentations.hpp. Licence GPL-3.0-only.
#include "promethee/alimentations.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <set>
#include <sstream>

#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
constexpr double PI = std::numbers::pi;
using Cx = std::complex<double>;
std::string nb(double v) {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o.precision(8);
  o << v;
  return o.str();
}
// Tension écrite dans une valeur : le dernier nombre (« AMS1117-3.3 » : 3,3 ; « 12V » : 12)
double tension(const std::string& t, double defaut) {
  std::string x;
  for (const char c : t) x += c == ',' ? '.' : c;
  std::size_t fin = x.find_last_of("0123456789");
  if (fin == std::string::npos) return defaut;
  std::size_t debut = fin;
  while (debut > 0 && (std::isdigit(static_cast<unsigned char>(x[debut - 1])) || x[debut - 1] == '.')) --debut;
  return std::atof(x.substr(debut, fin - debut + 1).c_str());
}
double champNombre(const ComposantElec& k, const char* cle, double defaut) {
  const auto it = k.champs.find(cle);
  if (it == k.champs.end()) return defaut;
  std::string x;
  for (const char c : it->second) x += c == ',' ? '.' : c;
  char* fin = nullptr;
  const double v = std::strtod(x.c_str(), &fin);
  return fin == x.c_str() ? defaut : v;
}
// Valeur normalisée de la série E12 au-dessus (inductances, condensateurs)
double serieAuDessus(double v) {
  static const double e12[] = {1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2, 10.0};
  const double d = std::pow(10.0, std::floor(std::log10(v)));
  for (const double x : e12)
    if (x * d >= v * 0.999) return x * d;
  return 10 * d;
}
// Série E96 (1 %), pour le diviseur de mesure : la tension de sortie doit tomber juste
double serieE96(double v) {
  static const double e96[] = {1.00, 1.02, 1.05, 1.07, 1.10, 1.13, 1.15, 1.18, 1.21, 1.24, 1.27, 1.30, 1.33, 1.37, 1.40, 1.43, 1.47, 1.50, 1.54, 1.58,
                               1.62, 1.65, 1.69, 1.74, 1.78, 1.82, 1.87, 1.91, 1.96, 2.00, 2.05, 2.10, 2.15, 2.21, 2.26, 2.32, 2.37, 2.43, 2.49, 2.55,
                               2.61, 2.67, 2.74, 2.80, 2.87, 2.94, 3.01, 3.09, 3.16, 3.24, 3.32, 3.40, 3.48, 3.57, 3.65, 3.74, 3.83, 3.92, 4.02, 4.12,
                               4.22, 4.32, 4.42, 4.53, 4.64, 4.75, 4.87, 4.99, 5.11, 5.23, 5.36, 5.49, 5.62, 5.76, 5.90, 6.04, 6.19, 6.34, 6.49, 6.65,
                               6.81, 6.98, 7.15, 7.32, 7.50, 7.68, 7.87, 8.06, 8.25, 8.45, 8.66, 8.87, 9.09, 9.31, 9.53, 9.76, 10.0};
  const double d = std::pow(10.0, std::floor(std::log10(v)));
  double meilleur = d, ecart = 1e300;
  for (const double x : e96)
    if (std::abs(std::log(x * d / v)) < ecart) ecart = std::abs(std::log(x * d / v)), meilleur = x * d;
  return meilleur;
}
double serieProche(double v) {
  static const double e24[] = {1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0, 3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1, 10.0};
  const double d = std::pow(10.0, std::floor(std::log10(v)));
  double meilleur = d, ecart = 1e300;
  for (const double x : e24)
    if (std::abs(std::log(x * d / v)) < ecart) ecart = std::abs(std::log(x * d / v)), meilleur = x * d;
  return meilleur;
}
}  // namespace

// ======================================================================================================================
// Arbre des rails
// ======================================================================================================================
std::string ArbreRails::texte() const {
  std::ostringstream o;
  std::function<void(int, int)> ecrire = [&](int i, int profondeur) {
    const EtageRail& e = etages[static_cast<std::size_t>(i)];
    o << std::string(static_cast<std::size_t>(profondeur) * 2, ' ') << e.reference << " (" << e.genre << ") " << e.sortie << " : " << nb(e.vSortie) << " V, " << nb(e.iSortie) << " A";
    if (e.genre != "source") o << ", rendement " << nb(std::round(e.rendement * 1000) / 10) << " %, dissipe " << nb(std::round(e.dissipation * 1000) / 1000) << " W";
    o << "\n";
    for (const int k : e.enfants) ecrire(k, profondeur + 1);
  };
  for (const int r : racines) ecrire(r, 0);
  return o.str();
}

ArbreRails arbreRails(const ConceptionElec& c, const std::map<std::string, double>& charges) {
  ArbreRails a;
  const Connectivite k = connectivite(c);
  for (const auto& x : c.composants) {
    EtageRail e;
    e.composant = x.uuid;
    e.reference = x.reference;
    if (x.symbole == "Pile" || (x.symbole == "Source_tension" && !x.valeur.empty() && std::isdigit(static_cast<unsigned char>(x.valeur[0])))) {
      e.genre = "source";
      e.sortie = k.nom(x.uuid, "1");
      e.vSortie = e.vEntree = tension(x.valeur, 9);
    } else if (x.symbole == "Regulateur" || x.symbole == "LM7805") {
      const bool l7805 = x.symbole == "LM7805";
      e.genre = "linéaire";
      e.entree = k.nom(x.uuid, l7805 ? "1" : "3");
      e.sortie = k.nom(x.uuid, l7805 ? "3" : "2");
      e.vSortie = l7805 ? 5.0 : champNombre(x, "Sim.VSortie", tension(x.valeur, 3.3));
    } else if (const auto it = x.champs.find("Sim.Topologie"); it != x.champs.end()) {
      e.genre = "découpage";
      const DefSymbole* d = c.symbole(x.symbole);
      for (const auto& b : d ? d->broches : std::vector<Broche>{})
        if (b.nom == "VIN") e.entree = k.nom(x.uuid, b.numero);
      e.sortie = x.champs.count("Sim.Sortie") ? x.champs.at("Sim.Sortie") : std::string();
      e.vSortie = champNombre(x, "Sim.VSortie", 5);
      e.rendement = champNombre(x, "Sim.Rendement", 0.9);
    } else
      continue;
    if (e.sortie.empty()) continue;
    a.etages.push_back(e);
  }
  // Liens : un étage est alimenté par celui dont la sortie est son entrée
  std::vector<int> parent(a.etages.size(), -1);
  for (std::size_t i = 0; i < a.etages.size(); ++i)
    for (std::size_t j = 0; j < a.etages.size(); ++j)
      if (i != j && !a.etages[i].entree.empty() && a.etages[i].entree == a.etages[j].sortie) {
        parent[i] = static_cast<int>(j);
        a.etages[j].enfants.push_back(static_cast<int>(i));
      }
  for (std::size_t i = 0; i < a.etages.size(); ++i)
    if (parent[i] < 0) a.racines.push_back(static_cast<int>(i));
  // Tensions d'entrée du haut vers le bas, courants du bas vers le haut
  std::function<void(int)> tensions = [&](int i) {
    EtageRail& e = a.etages[static_cast<std::size_t>(i)];
    for (const int k2 : e.enfants) {
      a.etages[static_cast<std::size_t>(k2)].vEntree = e.vSortie;
      tensions(k2);
    }
  };
  for (const int r : a.racines) tensions(r);
  std::function<void(int)> courants = [&](int i) {
    EtageRail& e = a.etages[static_cast<std::size_t>(i)];
    const auto it = charges.find(e.sortie);
    e.charges = it == charges.end() ? 0 : it->second;
    e.iSortie = e.charges;
    for (const int k2 : e.enfants) {
      courants(k2);
      e.iSortie += a.etages[static_cast<std::size_t>(k2)].iEntree;
    }
    const ComposantElec* x = c.composant(e.composant);
    const double iq = x ? champNombre(*x, "Sim.Iq", 0) : 0;
    if (e.genre == "source") {
      e.iEntree = e.iSortie;
      e.rendement = 1;
    } else if (e.genre == "linéaire") {
      e.iEntree = e.iSortie + iq;
      e.rendement = e.vEntree > 0 && e.iEntree > 0 ? e.vSortie * e.iSortie / (e.vEntree * e.iEntree) : 0;
    } else {
      e.iEntree = e.vEntree > 0 ? e.vSortie * e.iSortie / (e.rendement * e.vEntree) + iq : 0;
    }
    e.dissipation = e.genre == "source" ? 0 : e.vEntree * e.iEntree - e.vSortie * e.iSortie;
    a.dissipationTotale += e.dissipation;
  };
  for (const int r : a.racines) {
    courants(r);
    const EtageRail& e = a.etages[static_cast<std::size_t>(r)];
    a.puissanceEntree += (e.genre == "source" ? e.vSortie : e.vEntree) * e.iEntree;
  }
  return a;
}

// ======================================================================================================================
// Abaisseur
// ======================================================================================================================
namespace {
Cx gainEtage(const ConceptionAbaisseur& c, const DemandeAbaisseur& d, double f) {
  const Cx s(0, 2 * PI * f);
  const Cx zl = c.dcr + s * c.inductance, zc = c.esr + 1.0 / (s * c.capacite);
  const Cx zs = (c.rCharge * zc) / (c.rCharge + zc);
  return d.vEntree / d.vRampe * zs / (zl + zs);
}
Cx gainCorrecteur(const ConceptionAbaisseur& c, double f) {
  const Cx s(0, 2 * PI * f);
  const Cx zf = (c.r2 + 1.0 / (s * c.c1)) * (1.0 / (s * c.c2)) / (c.r2 + 1.0 / (s * c.c1) + 1.0 / (s * c.c2));
  const Cx zi = c.r1 * (c.r3 + 1.0 / (s * c.c3)) / (c.r1 + c.r3 + 1.0 / (s * c.c3));
  return zf / zi;
}
}  // namespace

ConceptionAbaisseur concevoirAbaisseur(const DemandeAbaisseur& d) {
  ConceptionAbaisseur c;
  if (d.vSortie <= 0 || d.vEntree <= d.vSortie || d.courant <= 0 || d.frequence <= 0) {
    c.notes.push_back("demande impossible : il faut 0 < Vsortie < Ventrée et un courant positif");
    return c;
  }
  const double vd = 0.45;  // diode Schottky
  c.rapport = (d.vSortie + vd) / (d.vEntree + vd);
  const double di = std::max(0.05, d.ondulationCourant) * d.courant;
  c.inductance = serieAuDessus((d.vEntree - d.vSortie) * c.rapport / (d.frequence * di));
  const double diReel = (d.vEntree - d.vSortie) * c.rapport / (d.frequence * c.inductance);
  c.iCrete = d.courant + diReel / 2;
  c.iEfficace = std::sqrt(d.courant * d.courant + diReel * diReel / 12);
  const double cMin = diReel / (8 * d.frequence * std::max(1e-6, d.ondulationTension * d.vSortie));
  c.capacite = serieAuDessus(std::max(3 * cMin, 22e-6));
  c.esr = 0.005;  // céramique X7R
  c.dcr = 0.03;
  c.rCharge = d.vSortie / d.courant;
  c.f0 = 1 / (2 * PI * std::sqrt(c.inductance * c.capacite));
  c.frequenceCoupure = std::min(d.frequence / 10, 5 * c.f0 * 10);
  // Facteur K : remontée de phase voulue au croisement, type III (deux zéros, deux pôles)
  const Cx t0 = gainEtage(c, d, c.frequenceCoupure);
  const double phase = std::arg(t0) * 180 / PI;
  const double remontee = std::clamp(d.margeVoulue - 90 - phase, 1.0, 170.0);
  const double kf = std::pow(std::tan((remontee / 4 + 45) * PI / 180), 2);
  const double fz = c.frequenceCoupure / std::sqrt(kf), fp = c.frequenceCoupure * std::sqrt(kf);
  c.rBas = c.r1 * d.vReference / (d.vSortie - d.vReference);
  c.c3 = 1 / (2 * PI * fz * c.r1);
  c.r3 = 1 / (2 * PI * fp * c.c3);
  auto regler = [&](double r2) {
    c.r2 = r2;
    c.c1 = 1 / (2 * PI * fz * r2);
    c.c2 = c.c1 / (2 * PI * fp * r2 * c.c1 - 1);
    return std::abs(gainCorrecteur(c, c.frequenceCoupure) * t0);
  };
  double bas = 10, haut = 1e7;
  for (int i = 0; i < 200; ++i) {
    const double m = std::sqrt(bas * haut);
    (regler(m) < 1 ? bas : haut) = m;
  }
  regler(std::sqrt(bas * haut));
  // Valeurs normalisées (E24), puis marge recalculée sur les valeurs posées
  c.r2 = serieProche(c.r2);
  c.r3 = serieProche(c.r3);
  c.c1 = serieProche(c.c1);
  c.c2 = serieProche(c.c2);
  c.c3 = serieProche(c.c3);
  c.rBas = serieE96(c.rBas);
  std::vector<double> f;
  std::vector<Cx> l;
  for (int i = 0; i <= 600; ++i) {
    f.push_back(10 * std::pow(10.0, i / 100.0));
    l.push_back(gainCorrecteur(c, f.back()) * gainEtage(c, d, f.back()));
  }
  const MargesBoucle m = marges(f, l);
  c.margePrevue = m.margePhase;
  if (m.ok) c.frequenceCoupure = m.frequenceCoupure;
  // Rendement estimé : conduction (interrupteur de 50 mΩ), diode, commutation (20 ns), cuivre de l'inductance
  const double pSortie = d.vSortie * d.courant;
  c.pertesConduction = c.iEfficace * c.iEfficace * 0.05 * c.rapport;
  c.pertesDiode = vd * d.courant * (1 - c.rapport);
  c.pertesCommutation = 0.5 * d.vEntree * d.courant * 40e-9 * d.frequence;
  c.pertesInductance = c.iEfficace * c.iEfficace * c.dcr;
  c.rendement = pSortie / (pSortie + c.pertesConduction + c.pertesDiode + c.pertesCommutation + c.pertesInductance);
  const auto texte = [](double v, FamilleValeur fam) { return ecrireCompact(v, fam); };
  c.nomenclature = {{"U1", "contrôleur abaisseur MLI en mode tension, " + nb(d.frequence / 1e3) + " kHz, interrupteur intégré"},
                    {"L1", texte(c.inductance, FamilleValeur::Inductance) + "H, " + nb(std::ceil(c.iCrete * 13) / 10) + " A de saturation au moins"},
                    {"D1", "diode Schottky 40 V " + nb(std::ceil(d.courant * 1.5)) + " A (SS34)"},
                    {"CIN", "10 µF " + nb(std::ceil(d.vEntree * 2)) + " V X7R"},
                    {"COUT", texte(c.capacite, FamilleValeur::Condensateur) + "F " + nb(std::ceil(d.vSortie * 2)) + " V X7R"},
                    {"CBOOT", "100 nF 25 V"},
                    {"R1", texte(c.r1, FamilleValeur::Resistance) + " 1 %"},
                    {"RBAS", texte(c.rBas, FamilleValeur::Resistance) + " 1 %"},
                    {"R2", texte(c.r2, FamilleValeur::Resistance)},
                    {"R3", texte(c.r3, FamilleValeur::Resistance)},
                    {"C1", texte(c.c1, FamilleValeur::Condensateur) + "F C0G"},
                    {"C2", texte(c.c2, FamilleValeur::Condensateur) + "F C0G"},
                    {"C3", texte(c.c3, FamilleValeur::Condensateur) + "F C0G"}};
  c.notes.push_back("rapport cyclique " + nb(std::round(c.rapport * 1000) / 1000) + ", ondulation de courant " + nb(std::round(diReel * 1000) / 1000) + " A crête à crête");
  c.notes.push_back("résonance LC à " + nb(std::round(c.f0)) + " Hz, croisement à " + nb(std::round(c.frequenceCoupure)) + " Hz, facteur K " + nb(std::round(kf * 10) / 10));
  c.notes.push_back("marge de phase prévue " + nb(std::round(c.margePrevue * 10) / 10) + "°, rendement estimé " + nb(std::round(c.rendement * 1000) / 10) + " %");
  c.ok = m.ok;
  return c;
}

std::string netlistBoucle(const ConceptionAbaisseur& c, const DemandeAbaisseur& d) {
  std::ostringstream o;
  o << "* Boucle de l'abaisseur " << nb(d.vEntree) << " V vers " << nb(d.vSortie) << " V (modèle moyen), coupée à l'entrée du correcteur\n";
  o << "V_test t 0 dc 0 ac 1\n";
  o << "R1 t n1 " << nb(c.r1) << "\nR3 t n3 " << nb(c.r3) << "\nC3 n3 n1 " << nb(c.c3) << "\n";
  o << "R2 n1 n2 " << nb(c.r2) << "\nC1 n2 comp " << nb(c.c1) << "\nC2 n1 comp " << nb(c.c2) << "\n";
  o << "E_ampli comp 0 0 n1 1e6\n";
  o << "E_mli sw 0 comp 0 " << nb(d.vEntree / d.vRampe) << "\n";
  o << "R_dcr sw lx " << nb(c.dcr) << "\nL1 lx sortie " << nb(c.inductance) << "\n";
  o << "C_sortie sortie ce " << nb(c.capacite) << "\nR_esr ce 0 " << nb(c.esr) << "\nR_charge sortie 0 " << nb(c.rCharge) << "\n";
  o << ".ac dec 100 10 10meg\n.end\n";
  return o.str();
}

MargesBoucle marges(const std::vector<double>& f, const std::vector<Cx>& l) {
  MargesBoucle m;
  if (f.size() < 2 || l.size() != f.size()) {
    m.erreur = "courbe vide";
    return m;
  }
  // Phase déroulée (degrés)
  std::vector<double> ph(l.size());
  for (std::size_t i = 0; i < l.size(); ++i) {
    double p = std::arg(l[i]) * 180 / PI;
    if (i) {
      while (p - ph[i - 1] > 180) p -= 360;
      while (p - ph[i - 1] < -180) p += 360;
    }
    ph[i] = p;
  }
  for (std::size_t i = 1; i < l.size(); ++i) {
    const double a = std::abs(l[i - 1]), b = std::abs(l[i]);
    if (a >= 1 && b < 1 && !m.ok) {
      const double u = std::log(a) / (std::log(a) - std::log(b));
      m.frequenceCoupure = std::exp(std::log(f[i - 1]) + u * (std::log(f[i]) - std::log(f[i - 1])));
      double p = ph[i - 1] + u * (ph[i] - ph[i - 1]);
      // Marge : écart à −180° (modulo 360°)
      double marge = 180 + p;
      while (marge > 180) marge -= 360;
      while (marge <= -180) marge += 360;
      m.margePhase = marge;
      m.ok = true;
    }
    // Marge de gain : là où la phase passe −180°
    if (m.margeGain == 0 && (ph[i - 1] + 180) * (ph[i] + 180) <= 0 && ph[i - 1] != ph[i]) {
      const double u = (-180 - ph[i - 1]) / (ph[i] - ph[i - 1]);
      const double g = std::abs(l[i - 1]) * std::pow(std::abs(l[i]) / std::abs(l[i - 1]), u);
      m.margeGain = -20 * std::log10(g);
    }
  }
  if (!m.ok) m.erreur = "pas de croisement à 0 dB dans la plage";
  return m;
}

MargesBoucle simulerBoucle(const ConceptionAbaisseur& c, const DemandeAbaisseur& d) {
  MargesBoucle m;
  const ResultatSimulation r = simuler(netlistBoucle(c, d));
  if (!r.reussi()) {
    m.erreur = r.erreur;
    return m;
  }
  const auto* f = r.reel("frequency");
  const auto* s = r.complexe("v(sortie)");
  if (!s) s = r.complexe("sortie");
  const auto* t = r.complexe("v(t)");
  if (!t) t = r.complexe("t");
  if (!f || !s || !t) {
    m.erreur = "courbes de la boucle absentes";
    return m;
  }
  std::vector<Cx> l;
  for (std::size_t i = 0; i < f->size() && i < s->size() && i < t->size(); ++i) l.push_back(-(*s)[i] / (*t)[i]);
  return marges(*f, l);
}

namespace {
// Pose et relie par des étiquettes globales ; rend l'UUID du composant
std::string poser(ConceptionElec& c, std::size_t feuille, const std::string& symbole, Pt p, const std::string& ref, const std::string& valeur,
                  const std::vector<std::pair<std::string, std::string>>& liaisons) {
  const std::string s = poserSymbole(c, feuille, symbole, p);
  for (auto& sp : c.feuilles[feuille].symboles) {
    if (sp.uuid != s) continue;
    if (ComposantElec* k = c.composant(sp.composant)) {
      k->reference = ref;
      k->valeur = valeur;
    }
    for (const auto& b : brochesPlacees(sp, *c.symbole(sp.symbole))) {
      std::string reseau;
      for (const auto& [n, r] : liaisons)
        if (n == b.broche->numero) reseau = r;
      if (reseau.empty()) {
        c.feuilles[feuille].nonConnexions.push_back({b.p, nouvelUuid()});
        continue;
      }
      const Pt d = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
      const Pt bout{b.p.x - d.x * 2'540'000, b.p.y - d.y * 2'540'000};
      c.feuilles[feuille].fils.push_back({nouvelUuid(), b.p, bout});
      Etiquette e;
      e.uuid = nouvelUuid();
      e.texte = reseau;
      e.p = bout;
      e.type = TypeEtiquette::Globale;
      c.feuilles[feuille].etiquettes.push_back(e);
    }
    return sp.composant;
  }
  return {};
}
}  // namespace

std::vector<std::string> poserAbaisseur(ConceptionElec& c, const ConceptionAbaisseur& a, const DemandeAbaisseur& d, std::size_t feuille, Pt o) {
  std::vector<std::string> v;
  if (feuille >= c.feuilles.size() || !a.ok) return v;
  if (!c.symboles.count("galerie:ABAISSEUR_MLI")) {
    SpecSymbole s;
    s.id = "galerie:ABAISSEUR_MLI";
    s.nom = "Contrôleur abaisseur";
    s.prefixe = "U";
    s.empreinte = "SOIC-8";
    s.description = "Contrôleur abaisseur MLI en mode tension, interrupteur intégré (générique : choisir la référence)";
    const std::vector<std::tuple<const char*, const char*, TypeBroche>> b = {{"1", "VIN", TypeBroche::AlimEntree}, {"2", "EN", TypeBroche::Entree}, {"3", "BOOT", TypeBroche::Passive},
                                                                             {"4", "SW", TypeBroche::Sortie},    {"5", "FB", TypeBroche::Entree}, {"6", "COMP", TypeBroche::Sortie},
                                                                             {"7", "GND", TypeBroche::AlimEntree}};
    for (const auto& [n, nom, t] : b) s.broches.push_back({n, nom, t});
    c.symboles[s.id] = genererSymbole(s);
  }
  const std::string vin = "+" + nb(d.vEntree) + "V", vout = "+" + nb(d.vSortie) + "V";
  double x = 0;
  auto ici = [&]() {
    const Pt p{o.x + nm(x), o.y};
    x += 25.4;
    return p;
  };
  const auto txt = [](double val, FamilleValeur f) { return ecrireCompact(val, f); };
  const std::string u = poser(c, feuille, "galerie:ABAISSEUR_MLI", ici(), "U1", "abaisseur", {{"1", vin}, {"2", vin}, {"3", "BOOT"}, {"4", "SW"}, {"5", "FB"}, {"6", "COMP"}, {"7", "GND"}});
  if (ComposantElec* k = c.composant(u)) {
    k->champs["Sim.Topologie"] = "abaisseur";
    k->champs["Sim.Sortie"] = vout;
    k->champs["Sim.VSortie"] = nb(d.vSortie);
    k->champs["Sim.Rendement"] = nb(a.rendement);
    k->champs["Sim.Exclure"] = "1";
  }
  v.push_back(u);
  v.push_back(poser(c, feuille, "L", ici(), "L1", txt(a.inductance, FamilleValeur::Inductance), {{"1", "SW"}, {"2", vout}}));
  v.push_back(poser(c, feuille, "D_Schottky", ici(), "D1", "SS34", {{"1", "SW"}, {"2", "GND"}}));
  v.push_back(poser(c, feuille, "C", ici(), "CIN", "10u", {{"1", vin}, {"2", "GND"}}));
  v.push_back(poser(c, feuille, "C", ici(), "COUT", txt(a.capacite, FamilleValeur::Condensateur), {{"1", vout}, {"2", "GND"}}));
  v.push_back(poser(c, feuille, "C", ici(), "CBOOT", "100n", {{"1", "BOOT"}, {"2", "SW"}}));
  x = 0;
  o.y += nm(30);
  v.push_back(poser(c, feuille, "R", ici(), "R1", txt(a.r1, FamilleValeur::Resistance), {{"1", vout}, {"2", "FB"}}));
  v.push_back(poser(c, feuille, "R", ici(), "RBAS", txt(a.rBas, FamilleValeur::Resistance), {{"1", "FB"}, {"2", "GND"}}));
  v.push_back(poser(c, feuille, "R", ici(), "R3", txt(a.r3, FamilleValeur::Resistance), {{"1", vout}, {"2", "N3"}}));
  v.push_back(poser(c, feuille, "C", ici(), "C3", txt(a.c3, FamilleValeur::Condensateur), {{"1", "N3"}, {"2", "FB"}}));
  v.push_back(poser(c, feuille, "R", ici(), "R2", txt(a.r2, FamilleValeur::Resistance), {{"1", "FB"}, {"2", "N2"}}));
  v.push_back(poser(c, feuille, "C", ici(), "C1", txt(a.c1, FamilleValeur::Condensateur), {{"1", "N2"}, {"2", "COMP"}}));
  v.push_back(poser(c, feuille, "C", ici(), "C2", txt(a.c2, FamilleValeur::Condensateur), {{"1", "FB"}, {"2", "COMP"}}));
  v.erase(std::remove(v.begin(), v.end(), std::string()), v.end());
  return v;
}

// ======================================================================================================================
// Magnétiques
// ======================================================================================================================
const std::vector<Noyau>& noyaux() {
  // Ferrite N87 (approchés : à confirmer sur la fiche du fabricant) ; AL des noyaux à entrefer pour inductances
  static const std::vector<Noyau> v = {
      {"EFD 20/10/7", "N87", 31e-6, 47e-3, 1.46e-6, 100e-9, 0.39, 0.88, 1.46, 2.52, 22e-6, 38e-3},
      {"E 25/13/7", "N87", 52.5e-6, 57.5e-3, 3.02e-6, 160e-9, 0.39, 0.88, 1.46, 2.52, 87e-6, 52e-3},
      {"RM 8", "N87", 64e-6, 38e-3, 2.43e-6, 250e-9, 0.39, 0.88, 1.46, 2.52, 30e-6, 42e-3},
      {"ETD 29", "N87", 76e-6, 72e-3, 5.47e-6, 250e-9, 0.39, 0.88, 1.46, 2.52, 97e-6, 53e-3},
  };
  return v;
}

double pertesSteinmetz(const Noyau& n, double f, double b) { return n.k * std::pow(f, n.alpha) * std::pow(std::abs(b), n.beta) * n.ve; }
double profondeurPeau(double f, double rho) { return f > 0 ? std::sqrt(rho / (PI * f * 4e-7 * PI)) : 1e300; }

double facteurDowell(double h, double delta, int m) {
  const double x = h / delta;
  if (x < 1e-6) return 1;
  const double a = (std::sinh(2 * x) + std::sin(2 * x)) / (std::cosh(2 * x) - std::cos(2 * x));
  const double b = (std::sinh(x) - std::sin(x)) / (std::cosh(x) + std::cos(x));
  return x * (a + 2.0 * (m * m - 1) / 3.0 * b);
}

Bobinage calculerBobine(double l, double iCrete, double iEff, double ondulation, double f, const Noyau& n, double d) {
  Bobinage b;
  b.noyau = n.nom;
  b.spires = std::max(1, static_cast<int>(std::ceil(std::sqrt(l / n.al))));
  b.bCrete = l * iCrete / (b.spires * n.ae);
  b.sature = b.bCrete > n.bsat;
  const double bAlternatif = l * ondulation / 2 / (b.spires * n.ae);
  b.pertesFer = pertesSteinmetz(n, f, bAlternatif);
  const double aire = PI * d * d / 4;
  b.rdc = 1.72e-8 * b.spires * n.longueurSpire / aire;
  // Couches : la fenêtre (carrée) tient « largeur / d » tours par couche
  const int parCouche = std::max(1, static_cast<int>(std::sqrt(n.fenetre) / d));
  b.couches = std::max(1, (b.spires + parCouche - 1) / parCouche);
  // Fil rond ramené à une couche équivalente (Dowell) : épaisseur d·√(π/4), porosité 0,9
  const double h = d * std::sqrt(PI / 4) * std::sqrt(0.9);
  b.facteur = facteurDowell(h, profondeurPeau(f), b.couches);
  b.rac = b.rdc * b.facteur;
  b.pertesCuivre = iEff * iEff * b.rdc + std::pow(ondulation / std::sqrt(12.0), 2) * b.rac;
  return b;
}

std::map<std::string, std::string> transformateurPlanar(int primaire, int secondaire, double dext, double largeur, double ecart) {
  auto spirale = [&](int tours, const std::string& nom) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << "G04 Prométhée : transformateur planar, " << nom << ", " << tours << " tours*\n";
    o << "%FSLAX46Y46*%\n%MOMM*%\n%LPD*%\n%ADD10C," << nb(largeur) << "*%\nD10*\n";
    const double pasRayon = largeur + ecart;
    double r = dext / 2 - largeur / 2;
    const int segments = 64;
    bool premier = true;
    auto point = [&](double x, double y, bool trace) {
      o << "X" << static_cast<long long>(std::llround(x * 1e6)) << "Y" << static_cast<long long>(std::llround(y * 1e6)) << (trace ? "D01*" : "D02*") << "\n";
    };
    for (int t = 0; t < tours && r > largeur; ++t)
      for (int s = 0; s < segments; ++s) {
        const double a = 2 * PI * s / segments, rr = r - pasRayon * s / segments;
        point(rr * std::cos(a), rr * std::sin(a), !premier);
        premier = false;
        if (s == segments - 1) r -= pasRayon;
      }
    o << "M02*\n";
    return o.str();
  };
  return {{"F.Cu", spirale(primaire, "primaire")}, {"B.Cu", spirale(secondaire, "secondaire")}};
}

}  // namespace prom::ecad

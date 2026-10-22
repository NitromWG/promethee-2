// Prométhée : bibliothèque de symboles électriques (CEI 60617), dessinés pour Prométhée. Conventions : le courant
// descend (alimentation en haut), bornes au pas de 2,54 mm, pôles espacés de 5,08 mm ; numéros de bornes de la
// CEI 60947 (pôles 1-2, 3-4, 5-6 ; contacts auxiliaires : chiffre de rang puis 1-2 à ouverture, 3-4 à fermeture ;
// bobines A1-A2 ; relais thermique 95-96 et 97-98). Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "promethee/electrique.hpp"

namespace prom::elec {

using ecad::Broche;
using ecad::DefSymbole;
using ecad::nm;
using ecad::ptMm;
using ecad::Trace;
using ecad::TypeBroche;

namespace {

// Broche : extrémité (x, y), direction vers le corps (0 +x, 1 +y, 2 −x, 3 −y), longueur du trait
Broche B(const std::string& num, double x, double y, int dir, double l = 2.54, const std::string& nom = "~", bool numeroVisible = true) {
  Broche b;
  b.numero = num;
  b.nom = nom;
  b.type = TypeBroche::Passive;
  b.extremite = ptMm(x, y);
  b.direction = dir;
  b.longueur = nm(l);
  b.nomVisible = nom != "~";
  b.numeroVisible = numeroVisible;
  return b;
}
Trace L(std::initializer_list<std::pair<double, double>> pts, bool ferme = false, bool rempli = false) {
  Trace t;
  t.forme = Trace::Forme::Polyligne;
  for (const auto& [x, y] : pts) t.points.push_back(ptMm(x, y));
  t.ferme = ferme || rempli;
  t.rempli = rempli;
  return t;
}
Trace R(double x0, double y0, double x1, double y1, bool rempli = false) {
  Trace t;
  t.forme = Trace::Forme::Rectangle;
  t.points = {ptMm(x0, y0), ptMm(x1, y1)};
  t.rempli = rempli;
  return t;
}
Trace C(double cx, double cy, double r, bool rempli = false) {
  Trace t;
  t.forme = Trace::Forme::Cercle;
  t.points = {ptMm(cx, cy)};
  t.rayon = nm(r);
  t.rempli = rempli;
  return t;
}
Trace A(double cx, double cy, double r, double a0, double a1) {
  Trace t;
  t.forme = Trace::Forme::Arc;
  t.points = {ptMm(cx, cy)};
  t.rayon = nm(r);
  t.debut = a0;
  t.fin = a1;
  return t;
}
Trace T(double x, double y, const std::string& texte, double taille = 1.8) {
  Trace t;
  t.forme = Trace::Forme::Texte;
  t.points = {ptMm(x, y)};
  t.texte = texte;
  t.taille = nm(taille);
  return t;
}
// Trait mixte (liaison mécanique) : tirets de 1 mm, intervalles de 0,8 mm
void tirets(std::vector<Trace>& v, double x0, double y0, double x1, double y1) {
  const double l = std::hypot(x1 - x0, y1 - y0);
  if (l < 1e-9) return;
  const double ux = (x1 - x0) / l, uy = (y1 - y0) / l;
  for (double s = 0; s < l; s += 1.8) {
    const double e = std::min(l, s + 1.0);
    v.push_back(L({{x0 + ux * s, y0 + uy * s}, {x0 + ux * e, y0 + uy * e}}));
  }
}
DefSymbole S(const std::string& id, const std::string& nom, const std::string& famille, const std::string& prefixe, const std::string& description,
             const std::string& motsCles) {
  DefSymbole d;
  d.id = id;
  d.nom = nom;
  d.categorie = famille;
  d.prefixe = prefixe;
  d.description = description;
  d.motsCles = motsCles;
  d.posReference = ptMm(4.5, -1.6);
  d.posValeur = ptMm(4.5, 1.6);
  return d;
}

// Contact vertical centré en (x, 0), bornes en ±7,62 : « no » à fermeture, sinon à ouverture
void contact(DefSymbole& d, double x, bool no, const std::string& haut, const std::string& bas, int unite = 0) {
  std::vector<Trace>& t = d.traces;
  const std::size_t debut = t.size();
  t.push_back(L({{x, -5.08}, {x, -2.54}}));
  t.push_back(L({{x, 5.08}, {x, 2.54}}));
  if (no) {
    t.push_back(L({{x, 2.54}, {x - 2.3, -2.9}}));
  } else {
    t.push_back(L({{x, -2.54}, {x + 2.0, -2.54}}));
    t.push_back(L({{x, 2.54}, {x + 2.6, -3.3}}));
  }
  for (std::size_t i = debut; i < t.size(); ++i) t[i].unite = unite;
  Broche h = B(haut, x, -7.62, 1, 2.54), b = B(bas, x, 7.62, 3, 2.54);
  h.unite = b.unite = unite;
  d.broches.push_back(h);
  d.broches.push_back(b);
}
// Marques des fonctions de coupure sur le contact fixe (en haut) : disjoncteur ×, sectionneur barre, contacteur
// demi-cercle, interrupteur-sectionneur barre et demi-cercle
enum class Fonction { Aucune, Disjoncteur, Sectionneur, Contacteur, Interrupteur, InterSectionneur };
void marque(DefSymbole& d, double x, Fonction f) {
  auto& t = d.traces;
  switch (f) {
    case Fonction::Disjoncteur:
      t.push_back(L({{x - 0.9, -3.44}, {x + 0.9, -1.64}}));
      t.push_back(L({{x - 0.9, -1.64}, {x + 0.9, -3.44}}));
      break;
    case Fonction::Sectionneur: t.push_back(L({{x - 1.0, -2.54}, {x + 1.0, -2.54}})); break;
    case Fonction::Contacteur: t.push_back(A(x, -3.3, 0.76, -90, 90)); break;
    case Fonction::Interrupteur: t.push_back(C(x, -3.2, 0.6)); break;
    case Fonction::InterSectionneur:
      t.push_back(L({{x - 1.0, -2.54}, {x + 1.0, -2.54}}));
      t.push_back(C(x, -3.2, 0.6));
      break;
    case Fonction::Aucune: break;
  }
}
// Anneau du tore différentiel autour des conducteurs (entre xg et xd, hauteur y)
void anneau(std::vector<Trace>& t, double xg, double xd, double y, double r) {
  t.push_back(A(xg, y, r, 90, 270));
  t.push_back(A(xd, y, r, -90, 90));
  t.push_back(L({{xg, y - r}, {xd, y - r}}));
  t.push_back(L({{xg, y + r}, {xd, y + r}}));
}
// Appareil à n pôles de puissance (bornes 1-2, 3-4, 5-6, 7-8), liaison mécanique en trait mixte
void poles(DefSymbole& d, int n, Fonction f, bool neutreDernier = false) {
  const double pas = 5.08, x0 = -pas * (n - 1) / 2.0;
  for (int i = 0; i < n; ++i) {
    const double x = x0 + pas * i;
    std::string h = std::to_string(2 * i + 1), b = std::to_string(2 * i + 2);
    if (neutreDernier && i == n - 1) {
      h = "N1";
      b = "N2";
    }
    contact(d, x, true, h, b);
    marque(d, x, f);
  }
  if (n > 1) tirets(d.traces, x0 - 1.15, -0.2, x0 + pas * (n - 1) - 1.15, -0.2);
  d.posReference = ptMm(x0 + pas * (n - 1) + 3.8, -1.6);
  d.posValeur = ptMm(x0 + pas * (n - 1) + 3.8, 1.6);
}
// Organe de commande à gauche du contact (relié par un trait mixte) : poussoir, coup de poing, rotatif, levier
enum class Organe { Poussoir, CoupDePoing, Rotatif, Levier, Galet, Thermique, Flotteur, Pression, Temperature };
void organe(DefSymbole& d, double x, Organe o) {
  auto& t = d.traces;
  const double y = 0, xa = x - 6.0;
  tirets(t, x - 1.2, y, xa, y);
  switch (o) {
    case Organe::Poussoir: t.push_back(L({{xa, y - 1.27}, {xa, y + 1.27}})); t.push_back(L({{xa - 1.0, y - 1.27}, {xa, y - 1.27}})); t.push_back(L({{xa - 1.0, y + 1.27}, {xa, y + 1.27}})); break;
    case Organe::CoupDePoing: t.push_back(A(xa, y, 1.9, 90, 270)); t.push_back(L({{xa, y - 1.9}, {xa, y + 1.9}})); break;
    case Organe::Rotatif: t.push_back(L({{xa - 1.2, y - 1.27}, {xa, y - 1.27}, {xa, y + 1.27}, {xa + 1.2, y + 1.27}})); break;
    case Organe::Levier: t.push_back(L({{xa, y}, {xa - 1.5, y - 1.8}})); t.push_back(C(xa - 1.7, y - 2.0, 0.35, true)); break;
    case Organe::Galet: t.push_back(L({{xa, y - 1.2}, {xa - 1.6, y}, {xa, y + 1.2}}, true)); t.push_back(C(xa - 2.2, y, 0.6)); break;
    case Organe::Thermique: t.push_back(L({{xa, y - 1.27}, {xa - 1.0, y - 1.27}, {xa - 1.0, y}, {xa, y}, {xa, y + 1.27}, {xa - 1.0, y + 1.27}})); break;
    case Organe::Flotteur: t.push_back(L({{xa, y}, {xa - 2.0, y}})); t.push_back(C(xa - 2.8, y, 0.8)); break;
    case Organe::Pression: t.push_back(T(xa - 1.6, y, "p", 1.8)); break;
    case Organe::Temperature: t.push_back(T(xa - 1.6, y, "θ", 1.8)); break;
  }
}

std::vector<DefSymbole> construire() {
  std::vector<DefSymbole> v;
  const std::string PROT = "Protection et sectionnement", COMM = "Commande et relayage", PUIS = "Puissance et récepteurs", SIGN = "Signalisation",
                    CAPT = "Capteurs et dialogue", ALIM = "Alimentations et transformateurs", BORN = "Bornes et connecteurs", AUTO = "Automates et variateurs",
                    BAT = "Bâtiment", UNIF = "Unifilaire (tableau)", PROD = "Production et stockage", MES = "Mesure et instrumentation", HTA = "HTA et postes",
                    SEC = "Sécurité et courants faibles";

  // ----- Protection et sectionnement -----
  {
    DefSymbole d = S("iec:disj-1p", "Disjoncteur 1P", PROT, "Q", "Disjoncteur unipolaire (magnétothermique)", "disjoncteur protection unipolaire 1p");
    poles(d, 1, Fonction::Disjoncteur);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:disj-1pn", "Disjoncteur 1P+N", PROT, "Q", "Disjoncteur phase + neutre", "disjoncteur phase neutre divisionnaire");
    poles(d, 2, Fonction::Disjoncteur, true);
    d.traces.push_back(T(2.54 + 1.4, -5.6, "N", 1.4));
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:disj-2p", "Disjoncteur 2P", PROT, "Q", "Disjoncteur bipolaire", "disjoncteur bipolaire 2p");
    poles(d, 2, Fonction::Disjoncteur);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:disj-3p", "Disjoncteur 3P", PROT, "Q", "Disjoncteur tripolaire", "disjoncteur tripolaire 3p triphase");
    poles(d, 3, Fonction::Disjoncteur);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:disj-4p", "Disjoncteur 3P+N", PROT, "Q", "Disjoncteur tétrapolaire (3 phases + neutre)", "disjoncteur tetrapolaire 4p neutre");
    poles(d, 4, Fonction::Disjoncteur, true);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:disj-moteur-3p", "Disjoncteur moteur", PROT, "Q", "Disjoncteur moteur magnétothermique (protection contre les surcharges et les courts-circuits)",
                     "disjoncteur moteur magnetothermique gv2 protection surcharge");
    poles(d, 3, Fonction::Disjoncteur);
    // Déclencheurs thermique et magnétique, sur la liaison mécanique
    d.traces.push_back(R(-9.4, -1.4, -7.0, 1.0));
    d.traces.push_back(L({{-9.0, 0.6}, {-9.0, -0.4}, {-8.2, -0.4}, {-8.2, 0.6}, {-7.4, 0.6}, {-7.4, -0.4}}));
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:sect-3p", "Sectionneur 3P", PROT, "Q", "Sectionneur tripolaire (coupure à vide, visible)", "sectionneur isolement tripolaire");
    poles(d, 3, Fonction::Sectionneur);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:inter-sect-3p", "Interrupteur-sectionneur 3P", PROT, "Q", "Interrupteur-sectionneur tripolaire (coupure en charge)",
                     "interrupteur sectionneur general tete tripolaire");
    poles(d, 3, Fonction::InterSectionneur);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:inter-sect-4p", "Interrupteur-sectionneur 3P+N", PROT, "Q", "Interrupteur-sectionneur 3 phases + neutre", "interrupteur sectionneur general neutre 4p");
    poles(d, 4, Fonction::InterSectionneur, true);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:fusible", "Fusible", PROT, "F", "Fusible (cartouche)", "fusible cartouche protection");
    d.broches = {B("1", 0, -7.62, 1, 3.81), B("2", 0, 7.62, 3, 3.81)};
    d.traces = {R(-1.27, -3.81, 1.27, 3.81), L({{0, -3.81}, {0, 3.81}})};
    d.posReference = ptMm(3.0, -1.6);
    d.posValeur = ptMm(3.0, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:sect-fusible-3p", "Sectionneur porte-fusibles 3P", PROT, "Q", "Sectionneur à fusibles tripolaire", "sectionneur porte fusible tripolaire");
    for (int i = 0; i < 3; ++i) {
      const double x = -5.08 + 5.08 * i;
      d.broches.push_back(B(std::to_string(2 * i + 1), x, -10.16, 1, 2.54));
      d.broches.push_back(B(std::to_string(2 * i + 2), x, 10.16, 3, 2.54));
      d.traces.push_back(L({{x, -7.62}, {x, -5.08}}));
      d.traces.push_back(L({{x, -5.08}, {x - 2.0, -0.6}}));
      d.traces.push_back(L({{x - 1.0, -5.08}, {x + 1.0, -5.08}}));
      d.traces.push_back(R(x - 0.9, 0.6, x + 0.9, 5.4));
      d.traces.push_back(L({{x, 5.4}, {x, 7.62}}));
    }
    tirets(d.traces, -6.6, -2.8, 3.5, -2.8);
    d.posReference = ptMm(8.6, -1.6);
    d.posValeur = ptMm(8.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:relais-thermique-3p", "Relais thermique", PROT, "F", "Relais de protection thermique (surcharge) ; contacts 95-96 et 97-98 à poser à part",
                     "relais thermique lrd protection surcharge moteur");
    for (int i = 0; i < 3; ++i) {
      const double x = -5.08 + 5.08 * i;
      d.broches.push_back(B(std::to_string(2 * i + 1), x, -7.62, 1, 3.81));
      d.broches.push_back(B(std::to_string(2 * i + 2), x, 7.62, 3, 3.81));
      d.traces.push_back(L({{x, -3.81}, {x, -1.4}, {x + 1.2, -1.4}, {x + 1.2, 1.4}, {x, 1.4}, {x, 3.81}}));
    }
    d.traces.push_back(R(-7.0, -2.4, 7.4, 2.4));
    d.posReference = ptMm(9.4, -1.6);
    d.posValeur = ptMm(9.4, 1.6);
    v.push_back(d);
  }
  auto differentiel = [&](const std::string& id, const std::string& nom, int n, bool disj, const std::string& desc, const std::string& mots) {
    DefSymbole d = S(id, nom, PROT, "Q", desc, mots);
    poles(d, n, disj ? Fonction::Disjoncteur : Fonction::Interrupteur, true);
    const double w = 5.08 * (n - 1) / 2.0 + 1.6;
    anneau(d.traces, -5.08 * (n - 1) / 2.0, 5.08 * (n - 1) / 2.0, 3.9, 1.6);
    d.traces.push_back(L({{w, 3.9}, {w + 2.2, 3.9}, {w + 2.2, -0.2}}));
    d.traces.push_back(T(w + 3.6, 2.0, "IΔ", 1.4));
    d.posReference = ptMm(w + 5.6, -2.4);
    d.posValeur = ptMm(w + 5.6, 0.6);
    v.push_back(d);
  };
  differentiel("iec:diff-2p", "Interrupteur différentiel 2P", 2, false, "Interrupteur différentiel bipolaire (30 mA : protection des personnes)",
               "interrupteur differentiel 30ma id bipolaire");
  differentiel("iec:diff-4p", "Interrupteur différentiel 4P", 4, false, "Interrupteur différentiel tétrapolaire", "interrupteur differentiel tetrapolaire 4p");
  differentiel("iec:disj-diff-2p", "Disjoncteur différentiel 2P", 2, true, "Disjoncteur différentiel phase + neutre", "disjoncteur differentiel ddr");
  differentiel("iec:agcp", "Disjoncteur de branchement", 2, true, "Disjoncteur général de branchement (AGCP), différentiel 500 mA sélectif",
               "agcp disjoncteur branchement general abonne enedis");
  {
    DefSymbole d = S("iec:parafoudre", "Parafoudre", PROT, "F", "Parafoudre (protection contre les surtensions)", "parafoudre surtension foudre");
    d.broches = {B("1", 0, -7.62, 1, 3.3), B("PE", 0, 7.62, 3, 3.3)};
    d.traces = {R(-1.6, -4.32, 1.6, 4.32), L({{-1.0, 2.6}, {1.0, -2.6}}), L({{0.2, -2.6}, {1.0, -2.6}, {1.0, -1.8}})};
    d.posReference = ptMm(3.4, -1.6);
    d.posValeur = ptMm(3.4, 1.6);
    v.push_back(d);
  }

  // ----- Commande et relayage -----
  auto bobine = [&](const std::string& id, const std::string& nom, int genre, const std::string& prefixe, const std::string& desc, const std::string& mots) {
    DefSymbole d = S(id, nom, COMM, prefixe, desc, mots);
    d.broches = {B("A1", 0, -7.62, 1, 5.715), B("A2", 0, 7.62, 3, 5.715)};
    d.traces = {R(-3.81, -1.905, 3.81, 1.905)};
    if (genre == 1) {  // temporisée au travail
      d.traces.push_back(R(-6.35, -1.905, -3.81, 1.905));
      d.traces.push_back(L({{-6.35, -1.905}, {-3.81, 1.905}}));
      d.traces.push_back(L({{-6.35, 1.905}, {-3.81, -1.905}}));
    } else if (genre == 2) {  // temporisée au repos
      d.traces.push_back(R(-6.35, -1.905, -3.81, 1.905, true));
    } else if (genre == 3) {  // électrovanne
      d.traces.push_back(L({{3.81, 0}, {6.0, 0}}));
      d.traces.push_back(L({{6.0, -1.6}, {9.2, 1.6}, {9.2, -1.6}, {6.0, 1.6}}, true));
    }
    d.posReference = ptMm(genre == 3 ? 10.6 : 5.2, -1.6);
    d.posValeur = ptMm(genre == 3 ? 10.6 : 5.2, 1.6);
    v.push_back(d);
  };
  bobine("iec:bobine-km", "Bobine de contacteur", 0, "KM", "Bobine de contacteur (A1-A2) ; ses pôles et contacts se posent à part et renvoient ici",
         "bobine contacteur km lc1 commande puissance");
  bobine("iec:bobine-ka", "Bobine de relais", 0, "KA", "Bobine de relais auxiliaire (A1-A2)", "bobine relais auxiliaire ka commande");
  bobine("iec:bobine-tempo-travail", "Relais temporisé au travail", 1, "KA", "Bobine temporisée à l'enclenchement (retard au travail)",
         "temporisation tempo travail retard enclenchement minuterie");
  bobine("iec:bobine-tempo-repos", "Relais temporisé au repos", 2, "KA", "Bobine temporisée au déclenchement (retard au repos)", "temporisation tempo repos retard declenchement");
  {
    DefSymbole d = S("iec:contact-no", "Contact à fermeture", COMM, "", "Contact auxiliaire à fermeture (NO) d'un appareil : 13-14, 23-24…",
                     "contact fermeture no ouvert travail auxiliaire");
    contact(d, 0, true, "3", "4");
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:contact-nf", "Contact à ouverture", COMM, "", "Contact auxiliaire à ouverture (NF) d'un appareil : 11-12, 21-22…",
                     "contact ouverture nf ferme repos auxiliaire");
    contact(d, 0, false, "1", "2");
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:contact-inverseur", "Contact inverseur", COMM, "", "Contact inverseur d'un relais : commun 11, ouverture 12, fermeture 14",
                     "contact inverseur commun relais");
    d.broches = {B("1", 0, 7.62, 3, 2.54), B("2", -2.54, -7.62, 1, 2.54), B("4", 2.54, -7.62, 1, 2.54)};
    d.traces = {L({{0, 5.08}, {0, 2.54}}), L({{-2.54, -5.08}, {-2.54, -2.54}, {-0.6, -2.54}}), L({{2.54, -5.08}, {2.54, -1.0}}), L({{0, 2.54}, {-1.6, -3.0}})};
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:contact-no-tempo", "Contact temporisé à fermeture", COMM, "", "Contact à fermeture temporisé au travail (67-68…)",
                     "contact temporise fermeture retard travail");
    contact(d, 0, true, "7", "8");
    d.traces.push_back(A(-3.6, -0.6, 1.6, -60, 60));
    tirets(d.traces, -1.2, -0.6, -2.0, -0.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:contact-nf-tempo", "Contact temporisé à ouverture", COMM, "", "Contact à ouverture temporisé au travail (55-56…)",
                     "contact temporise ouverture retard");
    contact(d, 0, false, "5", "6");
    d.traces.push_back(A(-2.0, -0.6, 1.6, 120, 240));
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:poles-km-3p", "Pôles de contacteur 3P", COMM, "", "Pôles de puissance d'un contacteur (1-2, 3-4, 5-6), rattachés à sa bobine",
                     "poles puissance contacteur km tripolaire");
    poles(d, 3, Fonction::Contacteur);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:poles-km-4p", "Pôles de contacteur 4P", COMM, "", "Pôles de puissance d'un contacteur tétrapolaire", "poles contacteur tetrapolaire");
    poles(d, 4, Fonction::Contacteur);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:contact-thermique-nf", "Contact du relais thermique (95-96)", COMM, "", "Contact à ouverture du relais thermique, s'ouvre en surcharge",
                     "contact thermique 95 96 defaut surcharge");
    contact(d, 0, false, "95", "96");
    organe(d, 0, Organe::Thermique);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:contact-thermique-no", "Contact du relais thermique (97-98)", COMM, "", "Contact à fermeture du relais thermique : signalisation du défaut",
                     "contact thermique 97 98 signalisation defaut");
    contact(d, 0, true, "97", "98");
    organe(d, 0, Organe::Thermique);
    v.push_back(d);
  }

  // ----- Capteurs et dialogue (organes principaux : le symbole porte le repère) -----
  auto bouton = [&](const std::string& id, const std::string& nom, bool no, Organe o, const std::string& prefixe, const std::string& desc, const std::string& mots) {
    DefSymbole d = S(id, nom, CAPT, prefixe, desc, mots);
    contact(d, 0, no, no ? "13" : "11", no ? "14" : "12");
    organe(d, 0, o);
    v.push_back(d);
  };
  bouton("iec:bp-no", "Bouton poussoir marche", true, Organe::Poussoir, "S", "Bouton poussoir à fermeture (marche)", "bouton poussoir marche start fermeture");
  bouton("iec:bp-nf", "Bouton poussoir arrêt", false, Organe::Poussoir, "S", "Bouton poussoir à ouverture (arrêt)", "bouton poussoir arret stop ouverture");
  bouton("iec:arret-urgence", "Arrêt d'urgence", false, Organe::CoupDePoing, "S", "Arrêt d'urgence coup de poing à accrochage (contact à ouverture positive)",
         "arret urgence coup de poing au securite");
  bouton("iec:commutateur", "Commutateur 2 positions", true, Organe::Rotatif, "S", "Commutateur rotatif à deux positions", "commutateur selecteur rotatif auto manu");
  bouton("iec:interrupteur-levier", "Interrupteur à levier", true, Organe::Levier, "S", "Interrupteur à levier", "interrupteur levier marche arret");
  bouton("iec:fdc-no", "Fin de course (fermeture)", true, Organe::Galet, "S", "Interrupteur de position à fermeture", "fin de course position galet butee");
  bouton("iec:fdc-nf", "Fin de course (ouverture)", false, Organe::Galet, "S", "Interrupteur de position à ouverture", "fin de course position ouverture");
  bouton("iec:thermostat", "Thermostat", false, Organe::Temperature, "B", "Thermostat (s'ouvre au-dessus du seuil)", "thermostat temperature");
  bouton("iec:pressostat", "Pressostat", true, Organe::Pression, "B", "Pressostat (se ferme au-dessus du seuil)", "pressostat pression");
  bouton("iec:flotteur", "Interrupteur à flotteur", true, Organe::Flotteur, "B", "Détecteur de niveau à flotteur", "flotteur niveau poire");
  {
    DefSymbole d = S("iec:capteur-pnp", "Détecteur de proximité PNP", CAPT, "B", "Détecteur inductif 3 fils PNP : 1 (+, brun), 3 (0 V, bleu), 4 (sortie, noir)",
                     "capteur detecteur inductif proximite pnp 3 fils");
    d.broches = {B("1", -2.54, -7.62, 1, 3.81, "+"), B("3", -2.54, 7.62, 3, 3.81, "-"), B("4", 2.54, 7.62, 3, 3.81, "Q")};
    d.traces = {R(-5.08, -3.81, 5.08, 3.81), L({{-1.4, 0}, {0, -1.4}, {1.4, 0}, {0, 1.4}}, true), L({{-3.6, -2.0}, {-2.0, -2.0}}), T(2.8, -2.2, "+", 1.4)};
    d.posReference = ptMm(6.6, -1.6);
    d.posValeur = ptMm(6.6, 1.6);
    v.push_back(d);
  }

  // ----- Signalisation -----
  {
    DefSymbole d = S("iec:voyant", "Voyant", SIGN, "H", "Voyant lumineux de signalisation (X1-X2)", "voyant lampe signalisation temoin");
    d.broches = {B("X1", 0, -7.62, 1, 5.08), B("X2", 0, 7.62, 3, 5.08)};
    d.traces = {C(0, 0, 2.54), L({{-1.8, -1.8}, {1.8, 1.8}}), L({{-1.8, 1.8}, {1.8, -1.8}})};
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:sonore", "Avertisseur sonore", SIGN, "H", "Avertisseur sonore (sirène, klaxon)", "sirene klaxon buzzer avertisseur sonore");
    d.broches = {B("1", -1.27, -7.62, 1, 5.08), B("2", 1.27, -7.62, 1, 5.08)};
    d.traces = {A(0, 1.0, 3.2, 180, 360), L({{-3.2, 1.0}, {3.2, 1.0}})};
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:gyrophare", "Feu clignotant", SIGN, "H", "Feu de signalisation clignotant (gyrophare)", "gyrophare feu clignotant");
    d.broches = {B("X1", 0, -7.62, 1, 5.08), B("X2", 0, 7.62, 3, 5.08)};
    d.traces = {C(0, 0, 2.54), L({{-1.8, -1.8}, {1.8, 1.8}}), L({{-1.8, 1.8}, {1.8, -1.8}}), L({{3.0, -2.2}, {4.2, -3.2}}), L({{3.4, 0}, {4.8, 0}}), L({{3.0, 2.2}, {4.2, 3.2}})};
    d.posReference = ptMm(6.0, -1.6);
    d.posValeur = ptMm(6.0, 1.6);
    v.push_back(d);
  }

  // ----- Puissance et récepteurs -----
  {
    DefSymbole d = S("iec:moteur-3", "Moteur triphasé", PUIS, "M", "Moteur asynchrone triphasé (U1, V1, W1, PE)", "moteur triphase asynchrone 3 phases");
    d.broches = {B("U1", -5.08, -10.16, 1, 2.0), B("V1", 0, -10.16, 1, 3.6), B("W1", 5.08, -10.16, 1, 2.0), B("PE", 10.16, 0, 2, 4.06)};
    d.traces = {C(0, 0, 6.35), T(0, -1.3, "M", 3.0), T(0, 2.2, "3~", 2.0), L({{-5.08, -8.16}, {-3.3, -5.4}}), L({{5.08, -8.16}, {3.3, -5.4}})};
    d.posReference = ptMm(8.4, -5.0);
    d.posValeur = ptMm(8.4, 4.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:moteur-3-yd", "Moteur triphasé 6 bornes", PUIS, "M", "Moteur triphasé à six bornes (démarrage étoile-triangle)", "moteur etoile triangle 6 bornes");
    d.broches = {B("U1", -5.08, -10.16, 1, 2.0), B("V1", 0, -10.16, 1, 3.6), B("W1", 5.08, -10.16, 1, 2.0),
                 B("W2", -5.08, 10.16, 3, 2.0), B("U2", 0, 10.16, 3, 3.6), B("V2", 5.08, 10.16, 3, 2.0), B("PE", 10.16, 0, 2, 4.06)};
    d.traces = {C(0, 0, 6.35), T(0, -1.3, "M", 3.0), T(0, 2.2, "3~", 2.0), L({{-5.08, -8.16}, {-3.3, -5.4}}), L({{5.08, -8.16}, {3.3, -5.4}}),
                L({{-5.08, 8.16}, {-3.3, 5.4}}), L({{5.08, 8.16}, {3.3, 5.4}})};
    d.posReference = ptMm(8.4, -5.0);
    d.posValeur = ptMm(8.4, 4.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:moteur-1", "Moteur monophasé", PUIS, "M", "Moteur monophasé (U1, U2, PE)", "moteur monophase");
    d.broches = {B("U1", -2.54, -10.16, 1, 4.2), B("U2", 2.54, -10.16, 1, 4.2), B("PE", 10.16, 0, 2, 4.06)};
    d.traces = {C(0, 0, 6.35), T(0, -1.3, "M", 3.0), T(0, 2.2, "1~", 2.0)};
    d.posReference = ptMm(8.4, -5.0);
    d.posValeur = ptMm(8.4, 4.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:moteur-cc", "Moteur à courant continu", PUIS, "M", "Moteur à courant continu (A1, A2)", "moteur continu cc dc");
    d.broches = {B("A1", 0, -10.16, 1, 3.81), B("A2", 0, 10.16, 3, 3.81)};
    d.traces = {C(0, 0, 6.35), T(0, -1.3, "M", 3.0), L({{-1.4, 2.4}, {1.4, 2.4}}), L({{-1.4, 3.2}, {-0.6, 3.2}}), L({{-0.2, 3.2}, {0.2, 3.2}}), L({{0.6, 3.2}, {1.4, 3.2}})};
    d.posReference = ptMm(8.4, -5.0);
    d.posValeur = ptMm(8.4, 4.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:resistance-chauffante", "Résistance chauffante", PUIS, "E", "Élément chauffant (résistance)", "resistance chauffante chauffage four");
    d.broches = {B("1", 0, -7.62, 1, 3.81), B("2", 0, 7.62, 3, 3.81)};
    d.traces = {R(-1.524, -3.81, 1.524, 3.81)};
    for (double y : {-2.0, 0.0, 2.0}) d.traces.push_back(L({{-1.524, y}, {1.524, y}}));
    d.posReference = ptMm(3.6, -1.6);
    d.posValeur = ptMm(3.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:ventilateur", "Ventilateur", PUIS, "M", "Ventilateur (moteur et hélice)", "ventilateur ventilation extracteur");
    d.broches = {B("1", -2.54, -10.16, 1, 4.2), B("2", 2.54, -10.16, 1, 4.2), B("PE", 10.16, 0, 2, 4.06)};
    d.traces = {C(0, 0, 6.35), C(0, 0, 0.8, true), A(-1.6, -1.6, 2.2, 200, 340), A(1.6, 1.6, 2.2, 20, 160)};
    d.posReference = ptMm(8.4, -5.0);
    d.posValeur = ptMm(8.4, 4.6);
    v.push_back(d);
  }
  bobine("iec:electrovanne", "Électrovanne", 3, "Y", "Électrovanne (bobine A1-A2 et distributeur)", "electrovanne vanne distributeur pneumatique hydraulique");
  {
    DefSymbole d = S("iec:lampe", "Lampe d'éclairage", PUIS, "E", "Luminaire, point lumineux", "lampe eclairage luminaire point lumineux");
    d.broches = {B("1", 0, -7.62, 1, 5.08), B("2", 0, 7.62, 3, 5.08)};
    d.traces = {C(0, 0, 2.54), L({{-1.8, -1.8}, {1.8, 1.8}}), L({{-1.8, 1.8}, {1.8, -1.8}})};
    v.push_back(d);
  }

  // ----- Alimentations et transformateurs -----
  {
    DefSymbole d = S("iec:transfo-1", "Transformateur monophasé", ALIM, "T", "Transformateur monophasé (primaire 1-2, secondaire 3-4)", "transformateur monophase commande 230 24");
    d.broches = {B("1", -2.54, -12.7, 1, 4.6), B("2", 2.54, -12.7, 1, 4.6), B("3", -2.54, 12.7, 3, 4.6), B("4", 2.54, 12.7, 3, 4.6)};
    d.traces = {C(0, -3.2, 4.4), C(0, 3.2, 4.4)};
    d.posReference = ptMm(6.4, -1.6);
    d.posValeur = ptMm(6.4, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:transfo-3", "Transformateur triphasé", ALIM, "T", "Transformateur triphasé (1U 1V 1W / 2U 2V 2W 2N)", "transformateur triphase");
    d.broches = {B("1U", -5.08, -12.7, 1, 4.0), B("1V", 0, -12.7, 1, 4.0), B("1W", 5.08, -12.7, 1, 4.0),
                 B("2U", -5.08, 12.7, 3, 4.0), B("2V", 0, 12.7, 3, 4.0), B("2W", 5.08, 12.7, 3, 4.0), B("2N", 10.16, 12.7, 3, 6.0)};
    d.traces = {C(0, -3.6, 5.0), C(0, 3.6, 5.0), L({{10.16, 6.7}, {4.3, 4.8}})};
    d.posReference = ptMm(8.0, -1.6);
    d.posValeur = ptMm(8.0, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:alim-dc", "Alimentation à découpage", ALIM, "G", "Alimentation continue (L, N, PE → +, −), 24 V en général", "alimentation 24v dc continue decoupage");
    d.broches = {B("L", -2.54, -10.16, 1, 4.0), B("N", 2.54, -10.16, 1, 4.0), B("PE", 10.16, -2.54, 2, 3.81), B("+", -2.54, 10.16, 3, 4.0), B("-", 2.54, 10.16, 3, 4.0)};
    d.traces = {R(-6.35, -6.16, 6.35, 6.16), L({{-6.35, 6.16}, {6.35, -6.16}}), T(-2.8, -2.8, "~", 2.2), T(2.8, 2.6, "=", 2.2)};
    d.posReference = ptMm(8.4, 2.4);
    d.posValeur = ptMm(8.4, 5.4);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:batterie", "Batterie", ALIM, "G", "Batterie d'accumulateurs (+, −)", "batterie accumulateur");
    d.broches = {B("+", 0, -7.62, 1, 6.35), B("-", 0, 7.62, 3, 6.35)};
    d.traces = {L({{-2.54, -1.27}, {2.54, -1.27}}), L({{-1.27, 0}, {1.27, 0}}), L({{-2.54, 1.27}, {2.54, 1.27}}), T(3.2, -2.2, "+", 1.4)};
    d.posReference = ptMm(4.6, -1.6);
    d.posValeur = ptMm(4.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:terre", "Terre (PE)", ALIM, "", "Conducteur de protection, terre : nomme le potentiel PE", "terre protection pe masse liaison equipotentielle");
    d.alimentation = true;
    d.nom = "PE";
    d.broches = {B("1", 0, -2.54, 1, 2.54, "~", false)};
    d.traces = {L({{-2.54, 0}, {2.54, 0}}), L({{-1.6, 0.9}, {1.6, 0.9}}), L({{-0.7, 1.8}, {0.7, 1.8}})};
    d.posValeur = ptMm(0, 3.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:zero-volt", "0 V", ALIM, "", "Potentiel 0 V des circuits de commande", "0v zero volt masse commande");
    d.alimentation = true;
    d.nom = "0V";
    d.broches = {B("1", 0, -2.54, 1, 2.54, "~", false)};
    d.traces = {L({{-2.54, 0}, {2.54, 0}})};
    d.posValeur = ptMm(0, 1.8);
    v.push_back(d);
  }

  // ----- Bornes et connecteurs -----
  {
    DefSymbole d = S("iec:borne", "Borne de bornier", BORN, "X", "Borne de passage d'un bornier : son numéro est donné à la pose (-X1:3)", "borne bornier passage raccordement");
    d.broches = {B("1", 0, -3.81, 1, 2.92, "~", false), B("1", 0, 3.81, 3, 2.92, "~", false)};
    d.traces = {C(0, 0, 0.89)};
    d.posReference = ptMm(1.9, -0.4);
    d.posValeur = ptMm(1.9, 1.8);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:borne-pe", "Borne de protection", BORN, "X", "Borne de terre (vert-jaune) d'un bornier", "borne terre pe protection bornier");
    d.broches = {B("1", 0, -3.81, 1, 2.92, "~", false), B("1", 0, 3.81, 3, 2.92, "~", false)};
    d.traces = {C(0, 0, 0.89), L({{1.6, -0.4}, {3.4, -0.4}}), L({{1.9, 0.3}, {3.1, 0.3}}), L({{2.2, 1.0}, {2.8, 1.0}})};
    d.posReference = ptMm(4.0, -0.4);
    d.posValeur = ptMm(4.0, 1.8);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:connecteur", "Contact de connecteur", BORN, "X", "Contact d'un connecteur (fiche et socle) : son numéro est donné à la pose",
                     "connecteur fiche socle prise contact");
    d.broches = {B("1", 0, -5.08, 1, 3.81, "~", false), B("1", 0, 5.08, 3, 3.81, "~", false)};
    d.traces = {L({{-1.27, -1.9}, {0, -0.6}, {1.27, -1.9}}), L({{-1.27, 1.27}, {1.27, 1.27}}), L({{0, 0.4}, {0, 1.27}})};
    d.posReference = ptMm(2.4, -0.4);
    d.posValeur = ptMm(2.4, 1.8);
    v.push_back(d);
  }

  // ----- Automates et variateurs -----
  {
    DefSymbole d = S("iec:automate-8e8s", "Automate 8 entrées / 8 sorties", AUTO, "A", "Automate programmable : alimentation, 8 entrées (%I0.0…), 8 sorties (%Q0.0…), chacune posée à part",
                     "automate api plc entrees sorties programmable");
    d.unites = 3;
    // Unité 1 : alimentation
    auto ajout = [&](Broche b, int unite) {
      b.unite = unite;
      d.broches.push_back(b);
    };
    ajout(B("L+", -2.54, -7.62, 1, 3.81, "L+"), 1);
    ajout(B("M", 2.54, -7.62, 1, 3.81, "M"), 1);
    // Unité 2 : entrées, en haut ; unité 3 : sorties, en bas
    for (int i = 0; i < 8; ++i) {
      const double x = -17.78 + 5.08 * i;
      ajout(B("I0." + std::to_string(i), x, -7.62, 1, 3.81, "%I0." + std::to_string(i)), 2);
      ajout(B("Q0." + std::to_string(i), x, 7.62, 3, 3.81, "%Q0." + std::to_string(i)), 3);
    }
    ajout(B("1M", 22.86, -7.62, 1, 3.81, "1M"), 2);
    ajout(B("2L", 22.86, 7.62, 3, 3.81, "2L"), 3);
    for (auto [u, x0, x1, txt] : {std::tuple{1, -6.35, 6.35, "Alim."}, std::tuple{2, -21.0, 26.0, "Entrées"}, std::tuple{3, -21.0, 26.0, "Sorties"}}) {
      Trace r = R(x0, -3.81, x1, 3.81);
      r.unite = u;
      d.traces.push_back(r);
      Trace t = T((x0 + x1) / 2, 0, txt, 1.6);
      t.unite = u;
      d.traces.push_back(t);
    }
    d.posReference = ptMm(28.0, -1.6);
    d.posValeur = ptMm(28.0, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:variateur", "Variateur de vitesse", AUTO, "U", "Variateur de fréquence : réseau L1 L2 L3, moteur U V W, PE, commande DI1, DI2, COM",
                     "variateur frequence vitesse convertisseur atv");
    d.broches = {B("L1", -5.08, -12.7, 1, 4.0), B("L2", 0, -12.7, 1, 4.0), B("L3", 5.08, -12.7, 1, 4.0), B("U", -5.08, 12.7, 3, 4.0), B("V", 0, 12.7, 3, 4.0),
                 B("W", 5.08, 12.7, 3, 4.0), B("PE", 12.7, -5.08, 2, 4.0), B("DI1", -12.7, -2.54, 0, 4.0, "DI1"), B("DI2", -12.7, 0, 0, 4.0, "DI2"),
                 B("COM", -12.7, 2.54, 0, 4.0, "COM")};
    d.traces = {R(-8.7, -8.7, 8.7, 8.7), L({{-8.7, 8.7}, {8.7, -8.7}}), T(-4.0, -4.0, "~", 2.2), T(4.0, 3.8, "~", 2.2)};
    d.posReference = ptMm(10.6, 2.0);
    d.posValeur = ptMm(10.6, 5.0);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:demarreur", "Démarreur progressif", AUTO, "U", "Démarreur progressif (L1 L2 L3 → T1 T2 T3)", "demarreur progressif soft starter");
    d.broches = {B("L1", -5.08, -12.7, 1, 4.0), B("L2", 0, -12.7, 1, 4.0), B("L3", 5.08, -12.7, 1, 4.0), B("T1", -5.08, 12.7, 3, 4.0), B("T2", 0, 12.7, 3, 4.0),
                 B("T3", 5.08, 12.7, 3, 4.0)};
    d.traces = {R(-8.7, -8.7, 8.7, 8.7), L({{-4.0, 3.0}, {4.0, -3.0}}), L({{1.0, -3.0}, {4.0, -3.0}, {4.0, -0.6}})};
    d.posReference = ptMm(10.6, -1.6);
    d.posValeur = ptMm(10.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:compteur", "Compteur d'énergie", MES, "P", "Compteur d'énergie (kWh)", "compteur energie kwh sous comptage");
    d.broches = {B("1", -2.54, -10.16, 1, 4.0), B("3", 2.54, -10.16, 1, 4.0), B("2", -2.54, 10.16, 3, 4.0), B("4", 2.54, 10.16, 3, 4.0)};
    d.traces = {R(-6.16, -6.16, 6.16, 6.16), T(0, 0, "kWh", 1.8)};
    d.posReference = ptMm(8.0, -1.6);
    d.posValeur = ptMm(8.0, 1.6);
    v.push_back(d);
  }
  for (auto [id, nom, lettre] : {std::tuple{"iec:amperemetre", "Ampèremètre", "A"}, std::tuple{"iec:voltmetre", "Voltmètre", "V"}, std::tuple{"iec:wattmetre", "Wattmètre", "W"},
                                 std::tuple{"iec:frequencemetre", "Fréquencemètre", "Hz"}, std::tuple{"iec:compteur-horaire", "Compteur horaire", "h"}}) {
    DefSymbole d = S(id, nom, MES, "P", std::string(nom) + " (appareil de mesure)", std::string("mesure indicateur ") + nom);
    d.broches = {B("1", 0, -7.62, 1, 4.445), B("2", 0, 7.62, 3, 4.445)};
    d.traces = {C(0, 0, 3.175), T(0, 0, lettre, 2.6)};
    v.push_back(d);
  }

  // ----- Bâtiment (multifilaire) -----
  {
    DefSymbole d = S("iec:inter-simple", "Interrupteur simple allumage", BAT, "S", "Interrupteur d'éclairage, simple allumage", "interrupteur simple allumage eclairage");
    contact(d, 0, true, "1", "2");
    organe(d, 0, Organe::Levier);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:va-et-vient", "Va-et-vient", BAT, "S", "Interrupteur va-et-vient (commun L, navettes 1 et 2)", "va et vient navette interrupteur deux endroits");
    d.broches = {B("L", 0, 7.62, 3, 2.54), B("1", -2.54, -7.62, 1, 2.54), B("2", 2.54, -7.62, 1, 2.54)};
    d.traces = {L({{0, 5.08}, {0, 2.54}}), L({{-2.54, -5.08}, {-2.54, -2.54}}), L({{2.54, -5.08}, {2.54, -2.54}}), L({{0, 2.54}, {-2.3, -2.2}})};
    organe(d, 0, Organe::Levier);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:poussoir", "Bouton poussoir (éclairage)", BAT, "S", "Poussoir de commande de télérupteur ou de minuterie", "poussoir eclairage telerupteur minuterie");
    contact(d, 0, true, "1", "2");
    organe(d, 0, Organe::Poussoir);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:telerupteur", "Télérupteur", BAT, "KA", "Télérupteur : bobine A1-A2, contact 1-2", "telerupteur bistable eclairage plusieurs points");
    d.broches = {B("A1", -5.08, -7.62, 1, 5.715), B("A2", -5.08, 7.62, 3, 5.715)};
    d.traces = {R(-7.62, -1.905, -2.54, 1.905), L({{-6.6, -1.0}, {-5.08, 1.0}, {-3.6, -1.0}})};
    contact(d, 2.54, true, "1", "2");
    tirets(d.traces, -2.54, 0, 1.2, 0);
    d.posReference = ptMm(5.6, -1.6);
    d.posValeur = ptMm(5.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:prise", "Prise de courant 2P+T", BAT, "XD", "Socle de prise de courant 16 A avec terre (L, N, PE)", "prise courant socle 2p+t 16a");
    d.broches = {B("L", -2.54, -7.62, 1, 3.81), B("N", 2.54, -7.62, 1, 3.81), B("PE", 7.62, -7.62, 1, 5.08)};
    d.traces = {A(0, 1.27, 3.81, 180, 360), L({{-3.81, 1.27}, {3.81, 1.27}}), L({{-2.54, -3.81}, {-2.54, -1.0}}), L({{2.54, -3.81}, {2.54, -1.0}}),
                L({{7.62, -2.54}, {7.62, 1.27}, {3.81, 1.27}})};
    d.posReference = ptMm(9.4, -1.0);
    d.posValeur = ptMm(9.4, 2.0);
    v.push_back(d);
  }

  // ----- Unifilaire (tableau de logement, distribution) -----
  auto unifilaire = [&](const std::string& id, const std::string& nom, const std::string& prefixe, Fonction f, bool diff, const std::string& desc, const std::string& mots) {
    DefSymbole d = S(id, nom, UNIF, prefixe, desc, mots);
    d.broches = {B("1", 0, -7.62, 1, 2.54), B("2", 0, 7.62, 3, 2.54)};
    d.traces = {L({{0, -5.08}, {0, -2.54}}), L({{0, 5.08}, {0, 2.54}}), L({{0, 2.54}, {-2.3, -2.9}})};
    marque(d, 0, f);
    // Deux traits : phase et neutre
    d.traces.push_back(L({{-0.9, 4.6}, {0.9, 3.8}}));
    d.traces.push_back(L({{-0.9, 5.6}, {0.9, 4.8}}));
    if (diff) {
      anneau(d.traces, 0, 0, 3.6, 1.4);
      d.traces.push_back(T(3.2, 3.6, "IΔ", 1.2));
    }
    d.posReference = ptMm(3.4, -2.4);
    d.posValeur = ptMm(3.4, 0.4);
    v.push_back(d);
  };
  unifilaire("iec:u-disj", "Disjoncteur (unifilaire)", "Q", Fonction::Disjoncteur, false, "Disjoncteur divisionnaire en représentation unifilaire", "disjoncteur divisionnaire unifilaire");
  unifilaire("iec:u-diff", "Interrupteur différentiel (unifilaire)", "Q", Fonction::Contacteur, true, "Interrupteur différentiel 30 mA en représentation unifilaire",
             "interrupteur differentiel unifilaire 30ma");
  unifilaire("iec:u-agcp", "Disjoncteur de branchement (unifilaire)", "Q", Fonction::Disjoncteur, true, "Disjoncteur de branchement 500 mA en représentation unifilaire",
             "agcp branchement unifilaire");
  {
    DefSymbole d = S("iec:u-depart", "Départ de circuit", UNIF, "", "Départ vers un circuit du logement (repère, usage, section)", "depart circuit utilisation");
    d.broches = {B("1", 0, -5.08, 1, 3.81)};
    d.traces = {L({{-1.27, -1.27}, {0, 0}, {1.27, -1.27}}), L({{0, 0}, {0, -1.27}})};
    d.posReference = ptMm(-1.0, 2.6);
    d.posValeur = ptMm(-1.0, 5.0);
    v.push_back(d);
  }
  unifilaire("iec:u-dd", "Disjoncteur différentiel (unifilaire)", "Q", Fonction::Disjoncteur, true, "Disjoncteur différentiel en représentation unifilaire",
             "disjoncteur differentiel unifilaire");
  unifilaire("iec:u-inter", "Interrupteur-sectionneur (unifilaire)", "Q", Fonction::InterSectionneur, false,
             "Interrupteur-sectionneur (arrivée d'un tableau) en représentation unifilaire", "interrupteur sectionneur arrivee unifilaire");
  {
    DefSymbole d = S("iec:u-fusible", "Fusibles (unifilaire)", UNIF, "F", "Fusibles gG en représentation unifilaire (sectionneur porte-fusibles)", "fusible gg unifilaire");
    d.broches = {B("1", 0, -7.62, 1, 2.54), B("2", 0, 7.62, 3, 2.54)};
    d.traces = {L({{0, -5.08}, {0, 5.08}}), R(-1.1, -3.3, 1.1, 3.3)};
    d.posReference = ptMm(3.4, -2.4);
    d.posValeur = ptMm(3.4, 0.4);
    v.push_back(d);
  }
  // Sources de l'unifilaire : dessin seul (pas d'appareil à la nomenclature du tableau)
  {
    DefSymbole d = S("iec:u-transfo", "Transformateur (unifilaire)", UNIF, "", "Transformateur HTA/BT en représentation unifilaire, sortie basse tension en bas",
                     "transformateur poste hta bt unifilaire source");
    d.broches = {B("2", 0, 7.62, 3, 2.54)};
    d.traces = {L({{0, -7.62}, {0, -4.2}}), C(0, -1.6, 2.6), C(0, 1.6, 2.6), L({{0, 4.2}, {0, 5.08}})};
    d.posReference = ptMm(3.8, -1.6);
    d.posValeur = ptMm(3.8, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:u-groupe", "Groupe électrogène (unifilaire)", UNIF, "", "Groupe électrogène en représentation unifilaire", "groupe electrogene generateur secours source");
    d.broches = {B("2", 0, 7.62, 3, 2.54)};
    d.traces = {C(0, 0, 3.0), T(0, -0.6, "G", 2.2), T(0, 1.5, "3~", 1.2), L({{0, 3.0}, {0, 5.08}})};
    d.posReference = ptMm(4.0, -1.6);
    d.posValeur = ptMm(4.0, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:u-reseau", "Arrivée du réseau (unifilaire)", UNIF, "", "Arrivée du réseau public de distribution, en représentation unifilaire",
                     "reseau public arrivee livraison source enedis");
    d.broches = {B("2", 0, 7.62, 3, 2.54)};
    d.traces = {L({{-3.0, -0.9}, {3.0, -0.9}}), L({{-3.0, 0}, {3.0, 0}}), L({{0, 0}, {0, 5.08}}), T(0, -2.4, "~", 2.0)};
    d.posReference = ptMm(4.0, -1.6);
    d.posValeur = ptMm(4.0, 1.6);
    v.push_back(d);
  }
  // Récepteurs de l'unifilaire : dessin seul, raccordé par le haut
  auto recepteur = [&](const std::string& id, const std::string& nom, const std::string& desc, const std::string& mots, std::vector<Trace> corps) {
    DefSymbole d = S(id, nom, UNIF, "", desc, mots);
    d.broches = {B("1", 0, -5.08, 1, 2.54)};
    d.traces = std::move(corps);
    d.posReference = ptMm(3.4, -1.0);
    d.posValeur = ptMm(3.4, 1.6);
    v.push_back(d);
  };
  recepteur("iec:u-moteur", "Moteur (unifilaire)", "Moteur alimenté par un départ, en représentation unifilaire", "moteur unifilaire depart recepteur",
            {C(0, 0, 2.54), T(0, 0, "M", 2.2)});
  recepteur("iec:u-lampe", "Éclairage (unifilaire)", "Circuit d'éclairage en représentation unifilaire", "eclairage lampe luminaire unifilaire recepteur",
            {L({{0, -2.54}, {0, -2.0}}), C(0, 0, 2.0), L({{-1.41, -1.41}, {1.41, 1.41}}), L({{-1.41, 1.41}, {1.41, -1.41}})});
  recepteur("iec:u-prise", "Prises de courant (unifilaire)", "Circuit de prises de courant en représentation unifilaire", "prise courant socle unifilaire recepteur",
            {L({{0, -2.54}, {0, -1.0}}), A(0, 1.0, 2.0, 180, 360), L({{-2.0, 1.0}, {2.0, 1.0}})});
  recepteur("iec:u-chauffage", "Chauffage (unifilaire)", "Appareil de chauffage (résistances) en représentation unifilaire", "chauffage radiateur resistance unifilaire recepteur",
            {R(-2.0, -2.54, 2.0, 1.5), L({{-1.6, 1.1}, {-0.4, -2.1}}), L({{-0.6, 1.1}, {0.6, -2.1}}), L({{0.4, 1.1}, {1.6, -2.1}})});
  recepteur("iec:u-irve", "Borne de recharge (unifilaire)", "Borne de recharge de véhicule électrique (IRVE) en représentation unifilaire",
            "borne recharge irve vehicule electrique unifilaire recepteur", {R(-2.2, -2.54, 2.2, 2.0), T(0, -0.3, "VE", 1.6)});
  recepteur("iec:u-condensateur", "Condensateurs (unifilaire)", "Batterie de condensateurs (compensation de l'énergie réactive) en représentation unifilaire",
            "condensateur compensation reactive cos phi unifilaire recepteur",
            {L({{0, -2.54}, {0, -0.5}}), L({{-2.0, -0.5}, {2.0, -0.5}}), L({{-2.0, 0.5}, {2.0, 0.5}}), L({{0, 0.5}, {0, 1.8}})});
  recepteur("iec:u-tableau", "Tableau (unifilaire)", "Tableau divisionnaire alimenté par un départ, en représentation unifilaire", "tableau divisionnaire armoire unifilaire",
            {R(-2.8, -2.54, 2.8, 1.3), L({{-2.8, 1.3}, {2.8, 1.3}, {2.8, -2.54}}, true, true)});
  // Photovoltaïque : générateur (chaînes de modules), onduleur à une ou plusieurs entrées MPPT, compteur de production
  {
    DefSymbole d = S("iec:u-pv", "Générateur photovoltaïque (unifilaire)", UNIF, "G", "Chaîne (ou groupe de chaînes) de modules photovoltaïques, sortie continue en bas",
                     "photovoltaique panneau solaire module chaine string generateur pv");
    d.broches = {B("2", 0, 7.62, 3, 2.54)};
    d.traces = {R(-4.4, -1.8, 4.4, 5.08), L({{-1.47, -1.8}, {-1.47, 5.08}}), L({{1.47, -1.8}, {1.47, 5.08}}), L({{-4.4, 1.64}, {4.4, 1.64}}),
                L({{-6.4, -6.0}, {-4.2, -3.8}}), L({{-5.2, -3.8}, {-4.2, -3.8}, {-4.2, -4.8}}), L({{-3.6, -6.0}, {-1.4, -3.8}}), L({{-2.4, -3.8}, {-1.4, -3.8}, {-1.4, -4.8}})};
    d.posReference = ptMm(6.0, -1.0);
    d.posValeur = ptMm(6.0, 2.0);
    v.push_back(d);
  }
  for (int e = 1; e <= 4; ++e) {
    const std::string id = e == 1 ? "iec:u-onduleur" : "iec:u-onduleur-" + std::to_string(e);
    DefSymbole d = S(id, e == 1 ? "Onduleur photovoltaïque (unifilaire)" : "Onduleur photovoltaïque à " + std::to_string(e) + " entrées (unifilaire)", UNIF, "U",
                     "Onduleur : " + std::string(e == 1 ? "une entrée continue" : std::to_string(e) + " entrées continues (MPPT)") + " en haut, sortie alternative en bas",
                     "onduleur photovoltaique convertisseur mppt injection reseau");
    const double w = std::max(7.62, (e - 1) / 2.0 * 5.08 + 2.54);
    for (int i = 0; i < e; ++i) d.broches.push_back(B("DC" + std::to_string(i + 1), (i - (e - 1) / 2.0) * 5.08, -10.16, 1, 2.54, "~", e > 1));
    d.broches.push_back(B("AC", 0, 10.16, 3, 2.54, "~", false));
    d.traces = {R(-w, -7.62, w, 7.62), L({{-w, 7.62}, {w, -7.62}}), T(-w / 2, -3.0, "=", 2.4), T(w / 2, 3.4, "~", 2.4)};
    d.posReference = ptMm(w + 2.0, -1.6);
    d.posValeur = ptMm(w + 2.0, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:u-compteur", "Compteur et réseau (unifilaire)", UNIF, "", "Compteur du distributeur puis réseau public, raccordés par le haut",
                     "compteur production linky reseau public injection");
    d.broches = {B("1", 0, -5.08, 1, 2.54)};
    d.traces = {R(-3.2, -2.54, 3.2, 2.2), T(0, -0.2, "kWh", 1.4), L({{0, 2.2}, {0, 4.4}}), L({{-2.6, 4.4}, {2.6, 4.4}}), L({{-2.6, 5.3}, {2.6, 5.3}}), T(0, 6.9, "~", 1.8)};
    d.posReference = ptMm(4.4, -1.0);
    d.posValeur = ptMm(4.4, 1.6);
    v.push_back(d);
  }
  unifilaire("iec:u-contacteur", "Contacteur (unifilaire)", "KM", Fonction::Contacteur, false, "Contacteur (heures creuses, délestage, commande d'un circuit) en représentation unifilaire",
             "contacteur unifilaire heures creuses delestage commande");
  {
    DefSymbole d = S("iec:u-asi", "ASI (unifilaire)", UNIF, "U", "Alimentation sans interruption en représentation unifilaire : entrée en haut, sortie secourue en bas",
                     "asi onduleur ups secours batterie informatique unifilaire");
    d.broches = {B("1", 0, -6.35, 1, 2.54), B("2", 0, 6.35, 3, 2.54)};
    d.traces = {R(-3.81, -3.81, 3.81, 3.81), T(0, -1.2, "ASI", 1.4), L({{-1.6, 1.0}, {1.6, 1.0}}), L({{-0.8, 2.0}, {0.8, 2.0}})};
    d.posReference = ptMm(5.4, -1.6);
    d.posValeur = ptMm(5.4, 1.6);
    v.push_back(d);
  }

  // ----- HTA et postes (représentation unifilaire : un trait pour les trois phases) -----
  auto appareilHTA = [&](const std::string& id, const std::string& nom, const std::string& prefixe, Fonction f, bool fusible, const std::string& desc, const std::string& mots) {
    DefSymbole d = S(id, nom, HTA, prefixe, desc, mots);
    d.broches = {B("1", 0, -10.16, 1, 2.54), B("2", 0, 10.16, 3, 2.54)};
    d.traces = {L({{0, -7.62}, {0, -2.54}}), L({{0, 2.54}, {0, 7.62}}), L({{0, 2.54}, {-2.3, -2.9}})};
    marque(d, 0, f);
    if (fusible) d.traces.push_back(R(-1.0, 3.6, 1.0, 7.0));
    for (double y : {-6.6, -5.8, -5.0}) d.traces.push_back(L({{-0.9, y + 0.4}, {0.9, y - 0.4}}));  // trois phases
    d.posReference = ptMm(3.4, -2.4);
    d.posValeur = ptMm(3.4, 0.4);
    v.push_back(d);
  };
  appareilHTA("iec:hta-interrupteur", "Interrupteur HTA (cellule IM)", "Q", Fonction::InterSectionneur, false,
              "Interrupteur-sectionneur HTA : ouvre et ferme en charge une arrivée ou un départ du poste (cellule IM)", "hta interrupteur sectionneur cellule im poste livraison boucle 20 kv");
  appareilHTA("iec:hta-disjoncteur", "Disjoncteur HTA (cellule DM1)", "Q", Fonction::Disjoncteur, false, "Disjoncteur HTA : coupe les courts-circuits, commandé par un relais de protection",
              "hta disjoncteur cellule dm1 protection generale poste 20 kv");
  appareilHTA("iec:hta-sectionneur", "Sectionneur HTA", "Q", Fonction::Sectionneur, false, "Sectionneur HTA : isole un tronçon hors charge, coupure visible",
              "hta sectionneur isolement coupure visible poste");
  appareilHTA("iec:hta-combine", "Interrupteur-fusibles HTA (cellule QM)", "Q", Fonction::InterSectionneur, true,
              "Combiné interrupteur-fusibles HTA : protège un transformateur jusqu'à 1 250 kVA environ (cellule QM)", "hta interrupteur fusibles combine cellule qm protection transformateur");
  {
    DefSymbole d = S("iec:hta-terre", "Sectionneur de mise à la terre", HTA, "Q", "Sectionneur de mise à la terre : relie les conducteurs à la terre avant une intervention",
                     "hta sectionneur mise a la terre mat consignation securite");
    d.broches = {B("1", 0, -7.62, 1, 2.54)};
    d.traces = {L({{0, 2.54}, {-2.3, -2.9}}), L({{0, 2.54}, {0, 4.0}}), L({{-2.2, 4.0}, {2.2, 4.0}}), L({{-1.4, 4.8}, {1.4, 4.8}}), L({{-0.6, 5.6}, {0.6, 5.6}})};
    marque(d, 0, Fonction::Sectionneur);
    d.traces.push_back(L({{0, -5.08}, {0, -2.54}}));
    d.posReference = ptMm(3.4, -2.4);
    d.posValeur = ptMm(3.4, 0.4);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:tt", "Transformateur de tension", HTA, "T", "Transformateur de tension (TT) : abaisse la tension du réseau pour les relais et les compteurs (100 V)",
                     "transformateur tension tt mesure comptage hta relais");
    d.broches = {B("A", 0, -6.35, 1, 2.75), B("a", -1.27, 6.35, 3, 2.75), B("n", 1.27, 6.35, 3, 2.75)};
    d.traces = {C(0, -1.4, 2.2), C(0, 1.4, 2.2)};
    d.posReference = ptMm(3.6, -1.6);
    d.posValeur = ptMm(3.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:relais-protection", "Relais de protection (50/51)", HTA, "F",
                     "Relais de protection à maximum de courant (ANSI 50/51) : mesure par les TC et fait déclencher le disjoncteur", "relais protection maximum courant 50 51 sepam numerique hta tc declenchement");
    d.broches = {B("S1", -7.62, -1.27, 0, 2.54), B("S2", -7.62, 1.27, 0, 2.54), B("A1", -2.54, -6.35, 1, 2.54), B("A2", 2.54, -6.35, 1, 2.54),
                 B("11", -2.54, 6.35, 3, 2.54), B("14", 2.54, 6.35, 3, 2.54)};
    d.traces = {R(-5.08, -3.81, 5.08, 3.81), T(0, -0.9, "I>  I>>", 1.6), T(0, 1.8, "50/51", 1.2)};
    d.posReference = ptMm(6.6, -1.6);
    d.posValeur = ptMm(6.6, 1.6);
    v.push_back(d);
  }

  // ----- Production et stockage -----
  {
    DefSymbole d = S("iec:module-pv", "Module photovoltaïque", PROD, "G", "Module (ou chaîne) photovoltaïque : sorties + et − du courant continu",
                     "photovoltaique panneau solaire module chaine string generateur");
    d.broches = {B("+", -2.54, 6.35, 3, 2.54), B("-", 2.54, 6.35, 3, 2.54)};
    d.traces = {R(-3.81, -1.27, 3.81, 3.81), L({{-1.27, -1.27}, {-1.27, 3.81}}), L({{1.27, -1.27}, {1.27, 3.81}}), L({{-3.81, 1.27}, {3.81, 1.27}}),
                L({{-6.0, -5.0}, {-4.0, -3.0}}), L({{-5.0, -3.0}, {-4.0, -3.0}, {-4.0, -4.0}}), L({{-3.4, -5.0}, {-1.4, -3.0}}), L({{-2.4, -3.0}, {-1.4, -3.0}, {-1.4, -4.0}})};
    d.posReference = ptMm(5.4, -0.4);
    d.posValeur = ptMm(5.4, 2.4);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:onduleur-pv", "Onduleur photovoltaïque", PROD, "U", "Onduleur raccordé au réseau : continu (+, −) en haut, alternatif (L, N) en bas, terre à droite",
                     "onduleur photovoltaique injection reseau mppt convertisseur");
    d.broches = {B("+", -2.54, -7.62, 1, 2.54), B("-", 2.54, -7.62, 1, 2.54), B("L", -2.54, 7.62, 3, 2.54), B("N", 2.54, 7.62, 3, 2.54), B("PE", 7.62, 2.54, 2, 2.54)};
    d.traces = {R(-5.08, -5.08, 5.08, 5.08), L({{-5.08, 5.08}, {5.08, -5.08}}), T(-2.6, -2.4, "=", 2.2), T(2.6, 2.6, "~", 2.2)};
    d.posReference = ptMm(6.6, -2.4);
    d.posValeur = ptMm(6.6, 0.0);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:asi", "Alimentation sans interruption (ASI)", PROD, "U", "Onduleur de secours (ASI) : entrée L N en haut, sortie secourue L' N' en bas, batterie intégrée",
                     "asi ups onduleur secours batterie informatique serveur");
    d.broches = {B("L", -2.54, -7.62, 1, 2.54), B("N", 2.54, -7.62, 1, 2.54), B("L'", -2.54, 7.62, 3, 2.54), B("N'", 2.54, 7.62, 3, 2.54), B("PE", 8.89, 0, 2, 2.54)};
    d.traces = {R(-6.35, -5.08, 6.35, 5.08), T(0, -1.6, "ASI", 1.8), L({{-1.6, 1.6}, {1.6, 1.6}}), L({{-0.8, 2.6}, {0.8, 2.6}})};
    d.posReference = ptMm(7.6, -2.6);
    d.posValeur = ptMm(7.6, -0.4);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:inverseur-sources", "Inverseur de sources", PROD, "Q", "Inverseur de sources : passe la charge du réseau normal (N) à la source de secours (S)",
                     "inverseur source normal secours groupe electrogene permutation");
    d.broches = {B("N", -2.54, -7.62, 1, 2.54), B("S", 2.54, -7.62, 1, 2.54), B("C", 0, 7.62, 3, 2.54)};
    d.traces = {L({{-2.54, -5.08}, {-2.54, -2.54}}), L({{2.54, -5.08}, {2.54, -2.54}}), L({{0, 5.08}, {0, 2.54}}), L({{0, 2.54}, {-2.3, -2.3}}),
                L({{-3.4, -2.54}, {-1.7, -2.54}}), L({{1.7, -2.54}, {3.4, -2.54}}), T(-4.6, -4.0, "N", 1.2), T(4.6, -4.0, "S", 1.2)};
    d.posReference = ptMm(4.6, -0.2);
    d.posValeur = ptMm(4.6, 2.4);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:groupe-electrogene", "Groupe électrogène", PROD, "G", "Groupe électrogène triphasé : U, V, W, N en bas, terre à droite",
                     "groupe electrogene generateur secours alternateur diesel");
    d.broches = {B("U", -3.81, 7.62, 3, 2.54), B("V", -1.27, 7.62, 3, 2.54), B("W", 1.27, 7.62, 3, 2.54), B("N", 3.81, 7.62, 3, 2.54), B("PE", 7.62, 0, 2, 2.54)};
    d.traces = {C(0, 0, 5.08), T(0, -1.4, "G", 2.6), T(0, 1.8, "3~", 1.4)};
    for (double x : {-3.81, -1.27, 1.27, 3.81}) d.traces.push_back(L({{x, std::sqrt(5.08 * 5.08 - x * x)}, {x, 5.08}}));
    d.posReference = ptMm(6.6, -3.4);
    d.posValeur = ptMm(6.6, -1.2);
    v.push_back(d);
  }

  // ----- Mesure et instrumentation -----
  {
    DefSymbole d = S("iec:tc", "Transformateur de courant", MES, "T", "Transformateur de courant (TC) : le conducteur traverse le tore, le secondaire S1-S2 donne 1 ou 5 A",
                     "transformateur courant tc tore mesure comptage centrale relais 5a");
    d.broches = {B("P1", 0, -5.08, 1, 2.54), B("P2", 0, 5.08, 3, 2.54), B("S1", 5.08, -1.27, 2, 2.54), B("S2", 5.08, 1.27, 2, 2.54)};
    d.traces = {L({{0, -2.54}, {0, 2.54}}), C(0, 0, 1.6), L({{2.54, -1.27}, {1.3, -0.9}}), L({{2.54, 1.27}, {1.3, 0.9}})};
    d.posReference = ptMm(-2.6, -1.6);
    d.posValeur = ptMm(-2.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:centrale-mesure", "Centrale de mesure", MES, "P", "Centrale de mesure : tensions V1 V2 V3 VN, courants par trois TC, énergie, puissances, harmoniques",
                     "centrale mesure multimetre energie puissance harmoniques tc modbus comptage");
    d.broches = {B("V1", -7.62, -6.35, 1, 2.54), B("V2", -5.08, -6.35, 1, 2.54), B("V3", -2.54, -6.35, 1, 2.54), B("VN", 0, -6.35, 1, 2.54), B("A1", 5.08, -6.35, 1, 2.54),
                 B("A2", 7.62, -6.35, 1, 2.54), B("I1+", -7.62, 6.35, 3, 2.54), B("I1-", -5.08, 6.35, 3, 2.54), B("I2+", -2.54, 6.35, 3, 2.54), B("I2-", 0, 6.35, 3, 2.54),
                 B("I3+", 2.54, 6.35, 3, 2.54), B("I3-", 5.08, 6.35, 3, 2.54)};
    d.traces = {R(-10.16, -3.81, 10.16, 3.81), T(0, 0, "kWh · V · A · W", 1.5)};
    d.posReference = ptMm(11.6, -1.6);
    d.posValeur = ptMm(11.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:transmetteur", "Transmetteur 4-20 mA", MES, "B", "Transmetteur deux fils : la grandeur mesurée (pression, niveau, débit) devient un courant de 4 à 20 mA",
                     "transmetteur capteur 4-20 ma boucle courant pression niveau debit analogique");
    d.broches = {B("+", 0, -6.35, 1, 3.175), B("-", 0, 6.35, 3, 3.175)};
    d.traces = {C(0, 0, 3.175), T(0, -0.6, "4-20", 1.3), T(0, 1.3, "mA", 1.1)};
    d.posReference = ptMm(4.6, -1.6);
    d.posValeur = ptMm(4.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:pt100", "Sonde Pt100 (3 fils)", MES, "B", "Sonde de température à résistance de platine (100 Ω à 0 °C), câblée en trois fils pour compenser la ligne",
                     "sonde pt100 temperature rtd resistance platine 3 fils");
    d.broches = {B("1", 0, -6.35, 1, 3.35), B("2", 0, 6.35, 3, 3.35), B("3", -2.54, 6.35, 3, 1.9)};
    d.traces = {R(-1.0, -3.0, 1.0, 3.0), L({{-2.0, 2.6}, {2.0, -2.6}}), L({{-2.54, 4.45}, {-2.54, 3.6}, {0, 3.6}}), T(3.2, 0, "θ", 1.6)};
    d.posReference = ptMm(4.6, -1.6);
    d.posValeur = ptMm(4.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:thermocouple", "Thermocouple", MES, "B", "Thermocouple : deux métaux soudés donnent une petite tension selon la température (types K, J…)",
                     "thermocouple temperature type k j sonde");
    d.broches = {B("+", -2.54, -6.35, 1, 2.54), B("-", 2.54, -6.35, 1, 2.54)};
    d.traces = {L({{-2.54, -3.81}, {-2.54, 0}, {0, 2.54}}), L({{2.54, -3.81}, {2.54, 0}, {0, 2.54}}), C(0, 2.54, 0.5, true), T(3.6, 1.8, "θ", 1.6)};
    d.posReference = ptMm(4.6, -2.6);
    d.posValeur = ptMm(4.6, -0.4);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:codeur", "Codeur incrémental", MES, "B", "Codeur incrémental : alimentation + et 0 V, voies A, B (sens) et Z (top zéro)",
                     "codeur incremental position vitesse voies a b z encodeur");
    d.broches = {B("+", -2.54, -6.35, 1, 2.54), B("0V", 2.54, -6.35, 1, 2.54), B("A", -2.54, 6.35, 3, 2.54), B("B", 0, 6.35, 3, 2.54), B("Z", 2.54, 6.35, 3, 2.54)};
    d.traces = {R(-6.35, -3.81, 6.35, 3.81), T(0, 0, "codeur", 1.4)};
    d.posReference = ptMm(7.8, -1.6);
    d.posValeur = ptMm(7.8, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:controle-isolement", "Contrôleur permanent d'isolement", MES, "P",
                     "Contrôleur permanent d'isolement (CPI) d'un réseau IT : signale le premier défaut à la terre (contact 11-14)", "controleur permanent isolement cpi it defaut terre surveillance");
    d.broches = {B("L", -2.54, -6.35, 1, 2.54), B("PE", -2.54, 6.35, 3, 2.54), B("11", 0, 6.35, 3, 2.54), B("14", 2.54, 6.35, 3, 2.54)};
    d.traces = {R(-5.08, -3.81, 5.08, 3.81), T(0, -0.8, "R<", 1.8), T(0, 1.8, "CPI", 1.2)};
    d.posReference = ptMm(6.6, -1.6);
    d.posValeur = ptMm(6.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:relais-phases", "Relais de contrôle des phases", MES, "KA",
                     "Relais de contrôle de réseau triphasé : ordre et absence de phase, sous-tension ; contact 11-14 fermé quand tout va bien", "relais controle phases ordre absence sous tension triphase protection moteur");
    d.broches = {B("L1", -2.54, -6.35, 1, 2.54), B("L2", 0, -6.35, 1, 2.54), B("L3", 2.54, -6.35, 1, 2.54), B("11", -2.54, 6.35, 3, 2.54), B("14", 2.54, 6.35, 3, 2.54)};
    d.traces = {R(-5.08, -3.81, 5.08, 3.81), T(0, -0.8, "3~", 1.6), T(0, 1.6, "U<  ⟳", 1.1)};
    d.posReference = ptMm(6.6, -1.6);
    d.posValeur = ptMm(6.6, 1.6);
    v.push_back(d);
  }

  // ----- Sécurité et courants faibles -----
  {
    DefSymbole d = S("iec:baes", "Bloc autonome d'éclairage de sécurité (BAES)", SEC, "E",
                     "BAES : s'allume sur sa batterie quand le secteur manque (évacuation ou ambiance) ; L N secteur, + − télécommande", "baes eclairage securite evacuation ambiance erp secours");
    d.broches = {B("L", -1.27, -5.08, 1, 2.54), B("N", 1.27, -5.08, 1, 2.54), B("+", -1.27, 5.08, 3, 2.54), B("-", 1.27, 5.08, 3, 2.54)};
    d.traces = {R(-3.81, -2.54, 3.81, 2.54), C(0, 0, 1.4), L({{-1.0, -1.0}, {1.0, 1.0}}), L({{-1.0, 1.0}, {1.0, -1.0}}), C(2.8, -1.6, 0.35, true)};
    d.posReference = ptMm(5.2, -1.6);
    d.posValeur = ptMm(5.2, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:detecteur-incendie", "Détecteur automatique d'incendie", SEC, "B", "Détecteur de fumée ou de chaleur raccordé à la boucle de la centrale d'alarme (+, −)",
                     "detecteur incendie fumee chaleur ssi centrale alarme boucle");
    d.broches = {B("+", -1.27, -6.35, 1, 2.54), B("-", 1.27, -6.35, 1, 2.54)};
    d.traces = {R(-3.81, -3.81, 3.81, 3.81), T(0, -0.4, "DI", 1.8), A(-1.2, 2.2, 0.6, 0, 180), A(0, 2.2, 0.6, 180, 360), A(1.2, 2.2, 0.6, 0, 180)};
    d.posReference = ptMm(5.2, -1.6);
    d.posValeur = ptMm(5.2, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:declencheur-manuel", "Déclencheur manuel d'alarme", SEC, "S", "Déclencheur manuel (bris de glace) : donne l'alarme incendie à la main",
                     "declencheur manuel dm alarme incendie bris de glace boitier rouge");
    d.broches = {B("+", -1.27, -6.35, 1, 2.54), B("-", 1.27, -6.35, 1, 2.54)};
    d.traces = {R(-3.81, -3.81, 3.81, 3.81), R(-2.2, -2.2, 2.2, 2.2), T(0, 0, "DM", 1.4)};
    d.posReference = ptMm(5.2, -1.6);
    d.posValeur = ptMm(5.2, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:prise-rj45", "Prise informatique RJ45", SEC, "XD", "Prise de communication RJ45 (réseau informatique, téléphone) du grade choisi pour le logement",
                     "prise rj45 informatique reseau ethernet telephone grade communication");
    d.broches = {B("1", 0, -5.08, 1, 2.54)};
    d.traces = {R(-3.81, -2.54, 3.81, 2.54), T(0, 0, "RJ45", 1.4)};
    d.posReference = ptMm(5.2, -1.6);
    d.posValeur = ptMm(5.2, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:prise-tv", "Prise télévision", SEC, "XD", "Prise d'antenne (télévision, satellite) raccordée au câble coaxial", "prise tv television antenne coaxial satellite");
    d.broches = {B("1", 0, -5.08, 1, 2.54)};
    d.traces = {C(0, 0, 2.54), T(0, 0, "TV", 1.4)};
    d.posReference = ptMm(3.8, -1.6);
    d.posValeur = ptMm(3.8, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:detecteur-presence", "Détecteur de présence", SEC, "B", "Détecteur de présence ou de mouvement : allume l'éclairage (L, N, sortie L')",
                     "detecteur presence mouvement infrarouge eclairage automatique couloir");
    d.broches = {B("L", -2.54, -6.35, 1, 2.54), B("N", 0, -6.35, 1, 2.54), B("L'", 2.54, -6.35, 1, 2.54)};
    d.traces = {R(-3.81, -3.81, 3.81, 2.54), A(0, 2.54, 2.2, 180, 360), T(0, -1.6, "IR", 1.2)};
    d.posReference = ptMm(5.2, -1.6);
    d.posValeur = ptMm(5.2, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:contact-ouverture", "Contact d'ouverture (ILS)", SEC, "B", "Contact magnétique de porte ou de fenêtre (alarme intrusion) : s'ouvre quand l'aimant s'éloigne",
                     "contact ouverture ils porte fenetre alarme intrusion magnetique");
    contact(d, 0, false, "1", "2");
    d.traces.push_back(R(-6.4, -1.0, -3.4, 1.0));
    d.traces.push_back(T(-4.9, 0, "N S", 0.9));
    v.push_back(d);
  }

  // ----- Bâtiment (compléments) -----
  {
    DefSymbole d = S("iec:inter-double", "Interrupteur double allumage", BAT, "S", "Interrupteur double : deux circuits d'éclairage commandés d'un même boîtier (L, 1, 2)",
                     "interrupteur double allumage deux circuits eclairage");
    d.broches = {B("L", 0, 7.62, 3, 2.54), B("1", -2.54, -7.62, 1, 2.54), B("2", 2.54, -7.62, 1, 2.54)};
    d.traces = {L({{0, 5.08}, {0, 2.54}}), L({{-2.54, 2.54}, {2.54, 2.54}}), L({{-2.54, -5.08}, {-2.54, -2.54}}), L({{2.54, -5.08}, {2.54, -2.54}}),
                L({{-2.54, 2.54}, {-4.6, -2.2}}), L({{2.54, 2.54}, {0.5, -2.2}})};
    organe(d, -2.54, Organe::Levier);
    d.posReference = ptMm(5.0, -1.6);
    d.posValeur = ptMm(5.0, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:minuterie", "Minuterie", BAT, "KA", "Minuterie d'escalier : les poussoirs (borne 3) allument l'éclairage (L') pour une durée réglée",
                     "minuterie escalier poussoir eclairage temporise cage");
    d.broches = {B("L", -2.54, -6.35, 1, 2.54), B("N", 2.54, -6.35, 1, 2.54), B("3", -2.54, 6.35, 3, 2.54), B("L'", 2.54, 6.35, 3, 2.54)};
    d.traces = {R(-5.08, -3.81, 5.08, 3.81), C(-1.8, 0, 1.6), L({{-1.8, 0}, {-1.8, -1.1}}), L({{-1.8, 0}, {-0.9, 0}}), T(2.4, 0, "t", 1.6)};
    d.posReference = ptMm(6.6, -1.6);
    d.posValeur = ptMm(6.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:contacteur-hc", "Contacteur heures creuses", BAT, "KM",
                     "Contacteur jour/nuit : le signal du compteur (A1-A2) met le chauffe-eau en marche aux heures creuses (pôle 1-2)", "contacteur heures creuses jour nuit chauffe eau ballon tarif");
    d.broches = {B("A1", -2.54, -6.35, 1, 2.54), B("1", 2.54, -6.35, 1, 2.54), B("A2", -2.54, 6.35, 3, 2.54), B("2", 2.54, 6.35, 3, 2.54)};
    d.traces = {R(-5.08, -3.81, 5.08, 3.81), T(0, -0.8, "HC", 1.8), T(0, 1.8, "0 · auto · 1", 1.0)};
    d.posReference = ptMm(6.6, -1.6);
    d.posValeur = ptMm(6.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:moteur-volet", "Moteur de volet roulant", BAT, "M", "Moteur de volet roulant : montée (M), descente (D), neutre (N), terre", "volet roulant moteur store montee descente");
    d.broches = {B("M", -2.54, -6.35, 1, 2.54), B("D", 0, -6.35, 1, 2.54), B("N", 2.54, -6.35, 1, 2.54), B("PE", 6.35, 0, 2, 2.54)};
    d.traces = {C(0, 0, 3.81), T(0, -0.6, "M", 2.0), T(0, 2.0, "↕", 1.4)};
    for (double x : {-2.54, 2.54}) d.traces.push_back(L({{x, -3.81}, {x, -std::sqrt(3.81 * 3.81 - x * x)}}));
    d.posReference = ptMm(5.4, -3.0);
    d.posValeur = ptMm(5.4, 2.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:inter-volet", "Inter de volet roulant", BAT, "S", "Interrupteur de volet roulant : envoie la phase (L) sur la montée (M) ou la descente (D), jamais les deux",
                     "interrupteur volet roulant montee descente commande");
    d.broches = {B("L", 0, 7.62, 3, 2.54), B("M", -2.54, -7.62, 1, 2.54), B("D", 2.54, -7.62, 1, 2.54)};
    d.traces = {L({{0, 5.08}, {0, 2.54}}), L({{0, 2.54}, {-2.0, -2.2}}), L({{-2.54, -5.08}, {-2.54, -2.54}}), L({{2.54, -5.08}, {2.54, -2.54}}), T(-4.2, -4.2, "↑", 1.4),
                T(4.2, -4.2, "↓", 1.4)};
    d.posReference = ptMm(4.6, -0.4);
    d.posValeur = ptMm(4.6, 2.2);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:chauffe-eau", "Chauffe-eau électrique", BAT, "E", "Chauffe-eau à accumulation (résistance et thermostat), sur circuit spécialisé, souvent aux heures creuses",
                     "chauffe eau ballon cumulus resistance eau chaude sanitaire");
    d.broches = {B("L", -1.27, -6.35, 1, 2.54), B("N", 1.27, -6.35, 1, 2.54), B("PE", 6.35, 0, 2, 2.54)};
    d.traces = {R(-3.81, -3.81, 3.81, 3.81), L({{-2.6, 1.4}, {-1.6, -1.4}, {-0.6, 1.4}, {0.4, -1.4}, {1.4, 1.4}, {2.4, -1.4}})};
    d.posReference = ptMm(5.2, -3.0);
    d.posValeur = ptMm(5.2, 2.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:vmc", "Ventilation mécanique (VMC)", BAT, "M", "Groupe de VMC à deux vitesses : petite vitesse (1), grande vitesse (2, cuisine), neutre",
                     "vmc ventilation mecanique controlee extracteur deux vitesses");
    d.broches = {B("1", -2.54, -6.35, 1, 2.54), B("2", 0, -6.35, 1, 2.54), B("N", 2.54, -6.35, 1, 2.54)};
    d.traces = {C(0, 0, 3.81), T(0, 0.2, "VMC", 1.3)};
    for (double x : {-2.54, 2.54}) d.traces.push_back(L({{x, -3.81}, {x, -std::sqrt(3.81 * 3.81 - x * x)}}));
    d.posReference = ptMm(5.4, -1.6);
    d.posValeur = ptMm(5.4, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:sortie-cable", "Sortie de câble", BAT, "XD", "Sortie de câble : alimente un appareil raccordé à demeure (plaque, volet, radiateur)",
                     "sortie cable boite raccordement appareil fixe plaque cuisson radiateur");
    d.broches = {B("L", -1.27, -5.08, 1, 2.54), B("N", 1.27, -5.08, 1, 2.54), B("PE", 5.08, 0, 2, 2.54)};
    d.traces = {C(0, 0, 2.54), L({{-1.4, 0}, {1.4, 0}}), L({{0.6, -0.7}, {1.4, 0}, {0.6, 0.7}})};
    d.posReference = ptMm(3.8, -3.0);
    d.posValeur = ptMm(3.8, 2.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:prise-32a", "Prise 32 A (cuisson)", BAT, "XD", "Socle 32 A pour la cuisinière ou la plaque de cuisson, sur circuit spécialisé en 6 mm²",
                     "prise 32a cuisson cuisiniere plaque circuit specialise");
    d.broches = {B("L", -2.54, -7.62, 1, 3.81), B("N", 2.54, -7.62, 1, 3.81), B("PE", 7.62, -7.62, 1, 5.08)};
    d.traces = {A(0, 1.27, 3.81, 180, 360), L({{-3.81, 1.27}, {3.81, 1.27}}), L({{-2.54, -3.81}, {-2.54, -1.0}}), L({{2.54, -3.81}, {2.54, -1.0}}),
                L({{7.62, -2.54}, {7.62, 1.27}, {3.81, 1.27}}), T(0, 3.2, "32 A", 1.2)};
    d.posReference = ptMm(9.4, -1.0);
    d.posValeur = ptMm(9.4, 2.0);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:horloge", "Interrupteur horaire", COMM, "KA", "Interrupteur horaire (programmateur) : ferme son contact 1-2 aux heures programmées ; A1-A2 alimentation",
                     "interrupteur horaire programmateur horloge minuterie eclairage exterieur");
    d.broches = {B("A1", -2.54, -6.35, 1, 2.54), B("1", 2.54, -6.35, 1, 2.54), B("A2", -2.54, 6.35, 3, 2.54), B("2", 2.54, 6.35, 3, 2.54)};
    d.traces = {R(-5.08, -3.81, 5.08, 3.81), C(0, 0, 2.2), L({{0, 0}, {0, -1.6}}), L({{0, 0}, {1.2, 0.6}})};
    d.posReference = ptMm(6.6, -1.6);
    d.posValeur = ptMm(6.6, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:borne-irve", "Borne de recharge (IRVE)", PUIS, "XD", "Borne de recharge de véhicule électrique : L1 L2 L3 N PE (monophasée : L1, N, PE)",
                     "borne recharge irve vehicule electrique wallbox mode 3");
    d.broches = {B("L1", -5.08, -6.35, 1, 2.54), B("L2", -2.54, -6.35, 1, 2.54), B("L3", 0, -6.35, 1, 2.54), B("N", 2.54, -6.35, 1, 2.54), B("PE", 5.08, -6.35, 1, 2.54)};
    d.traces = {R(-6.35, -3.81, 6.35, 3.81), T(0, -0.6, "VE", 1.8), A(0, 1.6, 1.2, 180, 360)};
    d.posReference = ptMm(7.8, -1.6);
    d.posValeur = ptMm(7.8, 1.6);
    v.push_back(d);
  }
  {
    DefSymbole d = S("iec:condensateurs", "Batterie de condensateurs", PUIS, "C", "Batterie de condensateurs triphasée : relève le cos φ (compensation de l'énergie réactive)",
                     "condensateurs batterie compensation reactive cos phi kvar");
    d.broches = {B("L1", -5.08, -7.62, 1, 2.54), B("L2", 0, -7.62, 1, 2.54), B("L3", 5.08, -7.62, 1, 2.54)};
    for (double x : {-5.08, 0.0, 5.08}) {
      d.traces.push_back(L({{x, -5.08}, {x, -0.5}}));
      d.traces.push_back(L({{x - 1.4, -0.5}, {x + 1.4, -0.5}}));
      d.traces.push_back(L({{x - 1.4, 0.5}, {x + 1.4, 0.5}}));
      d.traces.push_back(L({{x, 0.5}, {x, 2.54}}));
    }
    d.traces.push_back(L({{-5.08, 2.54}, {5.08, 2.54}}));
    d.posReference = ptMm(7.0, -1.6);
    d.posValeur = ptMm(7.0, 1.6);
    v.push_back(d);
  }
  for (auto& d : v) d.version = 1;
  return v;
}

}  // namespace

const std::vector<DefSymbole>& bibliothequeElectrique() {
  static const std::vector<DefSymbole> v = construire();
  return v;
}

const DefSymbole* symboleElectrique(const std::string& id) {
  static const std::map<std::string, std::size_t> index = [] {
    std::map<std::string, std::size_t> m;
    const auto& v = bibliothequeElectrique();
    for (std::size_t i = 0; i < v.size(); ++i) m[v[i].id] = i;
    return m;
  }();
  const auto it = index.find(id);
  return it == index.end() ? nullptr : &bibliothequeElectrique()[it->second];
}

bool estSymboleElectrique(const std::string& id) { return id.rfind("iec:", 0) == 0; }

const std::vector<std::string>& famillesElectriques() {
  static const std::vector<std::string> f = {"Protection et sectionnement", "Commande et relayage",    "Capteurs et dialogue",   "Signalisation",
                                             "Puissance et récepteurs",     "Alimentations et transformateurs", "Production et stockage", "Mesure et instrumentation",
                                             "Bornes et connecteurs",       "Automates et variateurs", "Bâtiment",               "Sécurité et courants faibles",
                                             "HTA et postes",               "Unifilaire (tableau)"};
  return f;
}

RoleSymbole roleSymbole(const std::string& id) {
  static const std::map<std::string, RoleSymbole> roles = {
      {"iec:contact-no", RoleSymbole::ContactNO},
      {"iec:contact-nf", RoleSymbole::ContactNF},
      {"iec:contact-inverseur", RoleSymbole::ContactInverseur},
      {"iec:contact-no-tempo", RoleSymbole::ContactNOTempo},
      {"iec:contact-nf-tempo", RoleSymbole::ContactNFTempo},
      {"iec:poles-km-3p", RoleSymbole::PolesPuissance},
      {"iec:poles-km-4p", RoleSymbole::PolesPuissance},
      {"iec:contact-thermique-nf", RoleSymbole::ContactThermiqueNF},
      {"iec:contact-thermique-no", RoleSymbole::ContactThermiqueNO},
      {"iec:borne", RoleSymbole::Borne},
      {"iec:borne-pe", RoleSymbole::Borne},
      {"iec:connecteur", RoleSymbole::Borne},
      {"iec:terre", RoleSymbole::Potentiel},
      {"iec:zero-volt", RoleSymbole::Potentiel},
  };
  const auto it = roles.find(id);
  if (it != roles.end()) return it->second;
  // Un appareil personnel porte son repère et peut porter des contacts posés ailleurs, comme une bobine
  return estSymboleElectrique(id) || estAppareilPersonnel(id) ? RoleSymbole::Principal : RoleSymbole::Autre;
}

}  // namespace prom::elec

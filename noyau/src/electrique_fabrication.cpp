// Prométhée : fabrication de l'armoire — longueurs des fils par les goulottes, liste de coupe et embouts, filerie,
// pertes des appareils et bilan thermique (CEI/TR 60890), chiffrage du tableau. Licence GPL-3.0-only.
#include "promethee/electrique_fabrication.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "electrique_interne.hpp"

namespace prom::elec {

using ecad::ComposantElec;

namespace {

double versMm(ecad::Nm v) { return static_cast<double>(v) / static_cast<double>(ecad::NM_PAR_MM); }

// Pôles d'un appareil : catalogue, sinon ce que disent ses caractéristiques (« 4P », « 3P+N », « 1P+N »)
int polesDe(const ComposantElec& c, int defaut) {
  if (const EntreeCatalogue* e = detail::catalogueDe(c); e && e->poles > 0) return e->poles;
  const std::string t = detail::majuscules(detail::champ(c, "affichage") + " " + detail::champ(c, "fonction") + " " + c.valeur);
  if (t.find("3P+N") != std::string::npos || t.find("4P") != std::string::npos || t.find("TÉTRA") != std::string::npos) return 4;
  if (t.find("3P") != std::string::npos || t.find("TRI") != std::string::npos) return 3;
  if (t.find("1P+N") != std::string::npos || t.find("2P") != std::string::npos) return 2;
  if (t.find("1P") != std::string::npos) return 1;
  return defaut;
}

// Bornes d'un bornier : numéros distincts de ses poses
int bornesPosees(const ProjetElectrique& p, const std::string& composant) {
  std::set<std::string> numeros;
  for (const auto& f : p.schema.feuilles)
    for (const auto& s : f.symboles) {
      if (s.composant != composant) continue;
      const ecad::DefSymbole* d = detail::definitionDe(p, s.symbole);
      if (d && !d->broches.empty()) numeros.insert(ecad::numeroBroche(s, d->broches.front().numero));
    }
  return static_cast<int>(numeros.size());
}

std::string nomCouleur(const std::string& code) {
  const std::string c = detail::majuscules(code);
  for (const auto& [k, nom] : couleursConducteurs())
    if (k == c) return nom;
  return code;
}

}  // namespace

// ---------- Longueurs ----------
EstimateurLongueurs::EstimateurLongueurs(const ProjetElectrique& p) {
  m_goulottes.resize(p.armoires.size());
  for (std::size_t ai = 0; ai < p.armoires.size(); ++ai) {
    const Armoire& a = p.armoires[ai];
    Goulottes& g = m_goulottes[ai];
    g.hauteur = a.hauteur;
    g.gauche = a.largeur;
    g.droite = 0;
    for (const auto& x : a.goulottes) {
      const double x0 = versMm(x.b.x0), y0 = versMm(x.b.y0), x1 = versMm(x.b.x1), y1 = versMm(x.b.y1);
      if (x1 - x0 >= y1 - y0) g.horizontales.push_back((y0 + y1) / 2);
      else {
        g.verticales = true;
        g.gauche = std::min(g.gauche, (x0 + x1) / 2);
        g.droite = std::max(g.droite, (x0 + x1) / 2);
      }
    }
    if (!g.verticales) {
      g.gauche = 0;
      g.droite = a.largeur;
    }
    for (const auto& b : boitesArmoire(p, ai)) {
      // Posé sous la plaque faute de place : pas encore à sa place, longueur inconnue
      if (!b.porte && versMm(b.b.y0) > a.hauteur) continue;
      Lieu l;
      l.armoire = ai;
      l.x = versMm(b.b.x0 + b.b.x1) / 2;
      l.y = versMm(b.b.y0 + b.b.y1) / 2;
      l.porte = b.porte;
      m_lieux.emplace(b.composant, l);
    }
  }
}

double EstimateurLongueurs::surPlaque(const Goulottes& g, double xa, double ya, double xb, double yb) const {
  if (g.horizontales.empty()) return std::abs(xa - xb) + std::abs(ya - yb);
  auto proche = [&](double y) {
    double meilleur = g.horizontales.front();
    for (double h : g.horizontales)
      if (std::abs(h - y) < std::abs(meilleur - y)) meilleur = h;
    return meilleur;
  };
  const double da = proche(ya), db = proche(yb);
  if (std::abs(da - db) < 1) return std::abs(ya - da) + std::abs(xa - xb) + std::abs(yb - db);
  // D'une rangée à l'autre : par la goulotte verticale du côté le plus court
  double horizontal = std::abs(xa - xb);
  if (g.verticales) {
    const double parGauche = std::abs(xa - g.gauche) + std::abs(xb - g.gauche), parDroite = std::abs(xa - g.droite) + std::abs(xb - g.droite);
    horizontal = std::min(parGauche, parDroite);
  }
  return std::abs(ya - da) + horizontal + std::abs(da - db) + std::abs(yb - db);
}

double EstimateurLongueurs::longueur(const std::string& composantA, const std::string& composantB) const {
  const auto ia = m_lieux.find(composantA), ib = m_lieux.find(composantB);
  if (ia == m_lieux.end() || ib == m_lieux.end()) return 0;
  const Lieu& a = ia->second;
  const Lieu& b = ib->second;
  if (a.armoire != b.armoire) return 0;
  const Goulottes& g = m_goulottes[a.armoire];
  double d = 0;
  if (composantA == composantB) d = 50;  // pont entre deux bornes du même appareil
  else if (a.porte && b.porte) d = std::abs(a.x - b.x) + std::abs(a.y - b.y);
  else if (a.porte || b.porte) {
    const Lieu& porte = a.porte ? a : b;
    const Lieu& plaque = a.porte ? b : a;
    // Sur la porte jusqu'à la charnière (côté gauche, à mi-hauteur), la lyre, puis la goulotte verticale de gauche
    d = porte.x + std::abs(porte.y - g.hauteur / 2) + CHARNIERE + surPlaque(g, g.gauche, g.hauteur / 2, plaque.x, plaque.y);
  } else {
    d = surPlaque(g, a.x, a.y, b.x, b.y);
  }
  return std::ceil((d + 2 * RESERVE) / 50 - 1e-9) * 50;
}

// ---------- Liste de coupe ----------
std::string embout(double section, bool double_) {
  // Code couleur français (NF C 63-023) des embouts à collerette
  static const std::vector<std::pair<double, const char*>> codes = {{0.25, "violet"}, {0.34, "turquoise"}, {0.5, "blanc"}, {0.75, "bleu"}, {1, "rouge"},
                                                                     {1.5, "noir"},   {2.5, "gris"},       {4, "orange"},  {6, "vert"},    {10, "marron"},
                                                                     {16, "ivoire"},  {25, "noir"},        {35, "rouge"},  {50, "bleu"}};
  if (section <= 0) return {};
  const char* couleur = codes.back().second;
  double s = codes.back().first;
  for (const auto& [x, c] : codes)
    if (x >= section - 1e-9) {
      couleur = c;
      s = x;
      break;
    }
  return std::string(double_ ? "embout double 2 × " : "embout ") + detail::nombreFr(s) + " mm² " + couleur;
}

ListeDeCoupe listeDeCoupe(const ProjetElectrique& p) {
  ListeDeCoupe r;
  const std::vector<LigneLiaison> carnet = carnetCablage(p);
  // Bornes de bornier : deux points de serrage (un de chaque côté) ; les autres bornes d'appareil : un
  std::set<std::string> borniers;
  for (const auto& c : p.schema.composants)
    if (detail::estBornier(c)) borniers.insert(c.uuid);
  std::map<std::string, std::vector<std::size_t>> parBorne;
  for (std::size_t i = 0; i < carnet.size(); ++i) {
    parBorne[carnet[i].de].push_back(i);
    parBorne[carnet[i].vers].push_back(i);
  }
  std::map<std::pair<std::size_t, std::string>, bool> doubles;  // (ligne, borne) → embout double
  for (const auto& [borne, lignes] : parBorne) {
    if (borne.empty()) continue;
    const LigneLiaison& l0 = carnet[lignes.front()];
    const std::string& composant = l0.de == borne ? l0.composantDe : l0.composantVers;
    const std::size_t places = borniers.count(composant) ? 2 : 1, n = lignes.size();
    if (n <= places) continue;
    if (n > 2 * places) {
      r.remarques.push_back(std::to_string(n) + " fils sur " + borne + " : plus de deux par point de serrage. Prévois un répartiteur, une borne de plus ou un pont.");
      continue;
    }
    // (n − places) embouts doubles, chacun pour deux fils
    for (std::size_t k = 0; k < 2 * (n - places); ++k) doubles[{lignes[k], borne}] = true;
  }
  for (std::size_t i = 0; i < carnet.size(); ++i) {
    const LigneLiaison& l = carnet[i];
    if (l.longueur <= 0) {
      ++r.sansLongueur;
      continue;
    }
    LigneCoupe c;
    c.fil = l.fil;
    c.type = l.type.empty() ? "H07V-K" : l.type;
    c.couleur = l.couleur;
    c.section = l.mm2;
    c.longueur = l.longueur;
    c.de = l.de;
    c.vers = l.vers;
    c.folio = l.folio;
    const bool dDe = doubles.count({i, l.de}) > 0, dVers = doubles.count({i, l.vers}) > 0;
    c.emboutDe = embout(l.mm2, dDe);
    c.emboutVers = embout(l.mm2, dVers);
    r.lignes.push_back(c);
  }
  std::stable_sort(r.lignes.begin(), r.lignes.end(), [](const LigneCoupe& a, const LigneCoupe& b) {
    return std::make_tuple(a.section, a.couleur, -a.longueur) < std::make_tuple(b.section, b.couleur, -b.longueur);
  });
  std::map<std::tuple<double, std::string, std::string>, BobineFil> bobines;
  for (std::size_t i = 0; i < r.lignes.size(); ++i) {
    LigneCoupe& c = r.lignes[i];
    c.numero = static_cast<int>(i) + 1;
    BobineFil& b = bobines[{c.section, c.couleur, c.type}];
    b.section = c.section;
    b.couleur = c.couleur;
    b.type = c.type;
    b.metres += c.longueur / 1000;
    ++b.fils;
  }
  // Un embout double sert deux fils : compté une fois, sur le premier des deux
  std::set<std::pair<std::string, std::string>> doublesComptes;
  for (const auto& c : r.lignes)
    for (const auto& [borne, e] : {std::pair{c.de, c.emboutDe}, std::pair{c.vers, c.emboutVers}}) {
      if (e.empty()) continue;
      if (e.rfind("embout double", 0) == 0 && !doublesComptes.insert({borne, e}).second) continue;
      ++r.embouts[e];
    }
  for (auto& [cle, b] : bobines) {
    b.metres = std::ceil(b.metres * 1.1 * 10) / 10;
    r.bobines.push_back(b);
  }
  if (r.sansLongueur > 0)
    r.remarques.push_back(std::to_string(r.sansLongueur) +
                          " liaison(s) sans longueur : un bout est hors armoire (c'est un câble), pas encore implanté, ou dans une autre armoire.");
  return r;
}

std::string csvListeDeCoupe(const ProjetElectrique& p) {
  const ListeDeCoupe l = listeDeCoupe(p);
  std::vector<std::vector<std::string>> v;
  for (const auto& c : l.lignes)
    v.push_back({std::to_string(c.numero), c.fil, c.type, detail::nombreFr(c.section), nomCouleur(c.couleur), detail::nombreFr(c.longueur, 0), c.de, c.emboutDe, c.vers,
                 c.emboutVers, c.folio});
  for (const auto& b : l.bobines)
    v.push_back({"", "Filerie", b.type, detail::nombreFr(b.section), nomCouleur(b.couleur), detail::nombreFr(b.metres * 1000, 0), std::to_string(b.fils) + " fil(s)", "", "", "", ""});
  for (const auto& [e, n] : l.embouts) v.push_back({"", "Embouts", "", "", "", "", std::to_string(n) + " × " + e, "", "", "", ""});
  return detail::documentCsv({"N°", "Fil", "Type", "Section (mm²)", "Couleur", "Longueur (mm)", "De", "Embout", "Vers", "Embout", "Folio"}, v);
}

// ---------- Bilan thermique ----------
const std::vector<std::string>& posesArmoire() {
  static const std::vector<std::string> v = {"Isolée, libre de tous côtés",        "Isolée, contre un mur",
                                             "En bout de rangée, libre",           "En bout de rangée, contre un mur",
                                             "Au milieu d’une rangée, libre",      "Au milieu d’une rangée, contre un mur",
                                             "Au milieu, contre un mur, toit couvert"};
  return v;
}

double surfaceEchangeArmoire(double largeur, double hauteur, double profondeur, int pose) {
  const double l = largeur / 1000, h = hauteur / 1000, p = profondeur / 1000;
  switch (std::clamp(pose, 0, 6)) {
    case 0: return 1.8 * h * (l + p) + 1.4 * l * p;
    case 1: return 1.4 * l * (h + p) + 1.8 * p * h;
    case 2: return 1.4 * p * (h + l) + 1.8 * l * h;
    case 3: return 1.4 * h * (l + p) + 1.4 * l * p;
    case 4: return 1.8 * l * h + 1.4 * l * p + p * h;
    case 5: return 1.4 * l * (h + p) + p * h;
    default: return 1.4 * l * h + 0.7 * l * p + p * h;
  }
}

double pertesAppareil(const ProjetElectrique& p, const ComposantElec& c, double charge) {
  const std::string& s = c.symbole;
  // Appareil personnel : les pertes données à sa création (constantes en service), sinon inconnues
  if (estAppareilPersonnel(s)) {
    const ecad::DefSymbole* d = detail::definitionDe(p, s);
    return d ? specAppareil(*d).pertes : -1;
  }
  const double in = detail::calibreDe(c), k2 = charge * charge;
  const double kw = detail::puissanceDe(c);
  auto modulaire = [&](int poles) { return poles * (1.0 + 0.07 * in) * k2; };  // disjoncteur : par pôle au courant assigné
  auto coupure = [&](int poles) { return poles * (0.2 + 0.0005 * in * in) * k2; };
  if (s == "iec:disj-1p") return modulaire(1);
  if (s == "iec:disj-1pn" || s == "iec:disj-2p" || s == "iec:disj-diff-2p" || s == "iec:agcp" || s == "iec:u-agcp") return modulaire(2);
  if (s == "iec:disj-3p") return modulaire(3);
  if (s == "iec:disj-4p") return modulaire(3) + 0.5 * modulaire(1);  // neutre peu chargé
  if (s == "iec:u-disj" || s == "iec:u-dd") return modulaire(polesDe(c, 2));
  if (s == "iec:disj-moteur-3p") return 3 * (0.6 + 0.08 * in) * k2;
  if (s == "iec:relais-thermique-3p") return 3 * (0.8 + 0.06 * in) * k2;
  if (s == "iec:diff-2p" || s == "iec:diff-4p" || s == "iec:u-diff") return polesDe(c, s == "iec:diff-4p" ? 4 : 2) * (0.5 + 0.03 * in) * k2;
  if (s == "iec:inter-sect-3p" || s == "iec:sect-3p") return coupure(3);
  if (s == "iec:inter-sect-4p" || s == "iec:inverseur-sources") return coupure(4);
  if (s == "iec:u-inter") return coupure(polesDe(c, 2));
  if (s == "iec:fusible" || s == "iec:u-fusible") return polesDe(c, 1) * std::min(3.0, 0.6 + 0.12 * in) * k2;
  if (s == "iec:sect-fusible-3p") return 3 * std::min(3.0, 0.6 + 0.06 * in) * k2;
  if (s == "iec:bobine-km" || s == "iec:u-contacteur") return 3.0 + 3 * 0.0015 * in * in * k2;
  if (s == "iec:contacteur-hc") return 2.0 + 2 * 0.002 * in * in * k2;
  if (s == "iec:bobine-ka") return 0.8;
  if (s == "iec:bobine-tempo-travail" || s == "iec:bobine-tempo-repos" || s == "iec:horloge" || s == "iec:minuterie") return 1.2;
  if (s == "iec:telerupteur") return 0.3 * k2;
  if (s == "iec:alim-dc") {
    const double sortie = 24 * (in > 0 ? in : 2.5) * charge;
    return 2.0 + sortie * (1 / 0.89 - 1);
  }
  if (s == "iec:transfo-1" || s == "iec:transfo-3" || s == "iec:u-transfo") {
    const double va = kw > 0 ? kw * 1000 : 100;
    return va * (0.04 + 0.08 * k2);
  }
  if (s == "iec:variateur") return 12 + 25 * (kw > 0 ? kw : 1) * k2;
  if (s == "iec:demarreur") return 5 + 0.5 * (kw > 0 ? kw : 4);
  if (s == "iec:automate-8e8s") return 8;
  if (s == "iec:asi" || s == "iec:u-asi") return 20 + 0.08 * (kw > 0 ? kw * 1000 : 1500) * charge;  // en ligne : rendement voisin de 92 %
  if (s == "iec:onduleur-pv" || s == "iec:u-onduleur") {
    const double w = kw > 0 ? kw * 1000 : 3600;
    return 0.03 * w * charge;
  }
  if (s == "iec:condensateurs" || s == "iec:u-condensateur") return 50;
  if (s == "iec:compteur" || s == "iec:u-compteur" || s == "iec:voltmetre" || s == "iec:amperemetre" || s == "iec:frequencemetre" || s == "iec:wattmetre") return 1.0;
  if (s == "iec:compteur-horaire") return 0.5;
  if (s == "iec:centrale-mesure") return 5;
  if (s == "iec:controle-isolement") return 4;
  if (s == "iec:relais-phases") return 1.5;
  if (s == "iec:relais-protection") return 10;
  if (s == "iec:tc") return 0.5;
  if (s == "iec:tt") return 2;
  if (s == "iec:voyant") return 0.5;
  if (s == "iec:borne") return 0.1 * k2 * std::max(1, bornesPosees(p, c.uuid));
  if (s == "iec:prise") return 0.6 * k2;
  // Sans perte notable en service : commande manuelle, parafoudre, terre, ventilateur (il refroidit)
  if (s == "iec:bp-no" || s == "iec:bp-nf" || s == "iec:arret-urgence" || s == "iec:commutateur" || s == "iec:parafoudre" || s == "iec:borne-pe" || s == "iec:sonore" ||
      s == "iec:ventilateur" || s == "iec:resistance-chauffante" || s == "iec:interrupteur-levier" || s == "iec:thermostat")
    return 0;
  return -1;
}

BilanThermique bilanThermique(const ProjetElectrique& p, std::size_t indice) {
  BilanThermique r;
  if (indice >= p.armoires.size()) return r;
  const Armoire& a = p.armoires[indice];
  const double charge = std::clamp(a.charge, 0.0, 1.0);
  for (const auto& im : a.implantations) {
    const ComposantElec* c = p.schema.composant(im.composant);
    if (!c) continue;
    if (c->symbole == "iec:ventilateur") ++r.ventilateurs;
    const double w = pertesAppareil(p, *c, charge);
    if (w < 0) {
      ++r.sansDonnee;
      continue;
    }
    if (w <= 0) continue;
    PerteAppareil x;
    x.composant = c->uuid;
    x.repere = repereAffiche(p, *c);
    const EntreeCatalogue* e = detail::catalogueDe(*c);
    const std::string fonction = detail::champ(*c, "fonction");
    x.designation = e ? e->designation : !fonction.empty() ? fonction : c->description;
    x.puissance = w;
    r.dissipee += w;
    r.pertes.push_back(x);
  }
  std::stable_sort(r.pertes.begin(), r.pertes.end(), [](const PerteAppareil& x, const PerteAppareil& y) { return x.puissance > y.puissance; });
  constexpr double K = 5.5;  // W/(m²·K), tôle d'acier peinte
  r.surface = surfaceEchangeArmoire(a.largeur, a.hauteur, a.profondeur, a.pose);
  r.echauffement = r.surface > 0 ? r.dissipee / (K * r.surface) : 0;
  r.temperatureInterieure = a.temperatureExterieure + r.echauffement;
  const double ecart = a.temperatureAdmise - a.temperatureExterieure;
  const std::string n0 = detail::nombreFr(r.temperatureInterieure, 0);
  if (r.temperatureInterieure <= a.temperatureAdmise + 1e-9) {
    r.remarques.push_back("Les parois suffisent : " + n0 + " °C à l'intérieur pour " + detail::nombreFr(a.temperatureAdmise, 0) + " °C admis, sans ventilation.");
  } else if (ecart > 0) {
    r.ventilation = 3.1 * (r.dissipee - K * r.surface * ecart) / ecart;
    const double debit = std::ceil(r.ventilation / 10) * 10;
    r.remarques.push_back("Sans ventilation, " + n0 + " °C à l'intérieur : il faut un ventilateur filtrant d'au moins " + detail::nombreFr(debit, 0) +
                          " m³/h, entrée d'air en bas, sortie en haut sur la face opposée.");
    if (r.ventilateurs > 0) {
      const double pose = 55.0 * r.ventilateurs;
      r.remarques.push_back(std::to_string(r.ventilateurs) + " ventilateur(s) de 55 m³/h déjà prévus (" + detail::nombreFr(pose, 0) + " m³/h) : " +
                            (pose >= r.ventilation ? std::string("c'est suffisant.") : std::string("c'est trop peu.")));
    }
  } else {
    r.climatisation = r.dissipee + K * r.surface * (a.temperatureExterieure - a.temperatureAdmise);
    r.remarques.push_back("Il fait plus chaud dehors (" + detail::nombreFr(a.temperatureExterieure, 0) + " °C) que ce qu'admettent les appareils : seul un climatiseur d'armoire de " +
                          detail::nombreFr(std::ceil(r.climatisation / 50) * 50, 0) + " W de froid convient.");
  }
  if (r.sansDonnee > 0)
    r.remarques.push_back(std::to_string(r.sansDonnee) + " appareil(s) sans donnée de pertes : ajoute leur puissance dissipée (fiche du fabricant) avant de conclure.");
  return r;
}

// ---------- Chiffrage ----------
double prixFilParMetre(double section) {
  static const std::vector<std::pair<double, double>> prix = {{0.5, 0.18}, {0.75, 0.22}, {1, 0.26}, {1.5, 0.32}, {2.5, 0.50}, {4, 0.80},
                                                              {6, 1.15},   {10, 2.00},  {16, 3.10}, {25, 4.90}, {35, 6.80}, {50, 9.50}};
  for (const auto& [s, e] : prix)
    if (section <= s + 1e-9) return e;
  return prix.back().second * section / prix.back().first;
}

Chiffrage chiffrer(const ProjetElectrique& p) {
  Chiffrage r;
  const std::vector<LigneNomenclatureElec> nomenclature = nomenclatureElectrique(p);
  const ChiffrageNomenclature cn = chiffrage(nomenclature);
  r.materiel = cn.total;
  r.lignesSansPrix = cn.sansPrix;
  const ListeDeCoupe coupe = listeDeCoupe(p);
  for (const auto& b : coupe.bobines) r.filerie += b.metres * prixFilParMetre(b.section);
  for (const auto& [e, n] : coupe.embouts) r.filerie += n * (e.rfind("embout double", 0) == 0 ? 0.07 : 0.04);
  r.filerie = std::round(r.filerie * 100) / 100;
  // Appareils selon où ils vont : implantés dans une armoire, sinon d'après leur encombrement
  std::set<std::string> vus;
  int rail = 0, plaque = 0, porte = 0, bornes = 0;
  auto compter = [&](const std::string& id, bool surPorte, bool surRail) {
    if (!vus.insert(id).second) return;
    const ComposantElec* c = p.schema.composant(id);
    if (!c) return;
    if (detail::estBornier(*c)) {
      bornes += std::max(1, bornesPosees(p, id));
      return;
    }
    ++(surPorte ? porte : surRail ? rail : plaque);
  };
  double rails = 0, goulottes = 0;
  for (std::size_t ai = 0; ai < p.armoires.size(); ++ai) {
    const Armoire& a = p.armoires[ai];
    for (const auto& im : a.implantations) compter(im.composant, im.porte, im.rail >= 0 || a.tableauModulaire);
    for (const auto& x : a.rails) rails += versMm(x.x1 - x.x0) / 1000;
    for (const auto& g : a.goulottes) goulottes += std::max(versMm(g.b.x1 - g.b.x0), versMm(g.b.y1 - g.b.y0)) / 1000;
  }
  for (const auto& c : p.schema.composants) {
    if (vus.count(c.uuid)) continue;
    const Encombrement e = encombrement(p, c.uuid);
    if (e.horsArmoire || e.largeur <= 0 || c.symbole == "iec:connecteur") continue;
    compter(c.uuid, e.porte, e.rail);
  }
  int conducteurs = 0;
  for (const auto& c : p.cables) conducteurs += c.conducteurs;
  auto poste = [&](const std::string& nom, double quantite, double heuresUnite) {
    if (quantite <= 0) return;
    r.postes.push_back({nom, quantite, quantite * heuresUnite});
  };
  poste("Appareils sur rail", rail, 0.10);
  poste("Appareils sur plaque (perçage, fixation)", plaque, 0.30);
  poste("Appareils de porte (perçage Ø 22, montage)", porte, 0.25);
  poste("Bornes", bornes, 0.03);
  poste("Fils intérieurs (coupe, embouts, repères, raccordement)", static_cast<double>(coupe.lignes.size()), 0.10);
  poste("Rails et goulottes (m)", std::round((rails + goulottes) * 10) / 10, 0.15);
  poste("Conducteurs de câbles raccordés", conducteurs, 0.05);
  double heures = 0;
  for (const auto& x : r.postes) heures += x.heures;
  if (heures > 0) poste("Contrôle, essais et dossier (10 %)", 1, heures * 0.10);
  for (const auto& x : r.postes) r.heures += x.heures;
  r.heures = std::round(r.heures * 10) / 10;
  r.mainOeuvre = std::round(r.heures * p.tauxHoraire * 100) / 100;
  r.total = r.materiel + r.filerie + r.mainOeuvre;
  return r;
}

std::string csvChiffrage(const ProjetElectrique& p) {
  const Chiffrage c = chiffrer(p);
  std::vector<std::vector<std::string>> v;
  v.push_back({"Matériel (nomenclature)", "", "", detail::nombreFr(c.materiel, 2)});
  v.push_back({"Filerie et embouts", "", "", detail::nombreFr(c.filerie, 2)});
  for (const auto& x : c.postes) v.push_back({x.nom, detail::nombreFr(x.quantite), detail::nombreFr(x.heures, 2), ""});
  v.push_back({"Main d'œuvre", "", detail::nombreFr(c.heures, 1), detail::nombreFr(c.mainOeuvre, 2)});
  v.push_back({"Total HT", "", "", detail::nombreFr(c.total, 2)});
  if (c.lignesSansPrix > 0) v.push_back({std::to_string(c.lignesSansPrix) + " ligne(s) de nomenclature sans prix", "", "", ""});
  return detail::documentCsv({"Poste", "Quantité", "Heures", "Montant HT (€)"}, v);
}

}  // namespace prom::elec

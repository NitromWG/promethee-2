// Prométhée : conception guidée par modules et circuits types. Voir conception_guidee.hpp. Licence GPL-3.0-only.
#include "promethee/conception_guidee.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "promethee/fabrication_avancee.hpp"
#include "promethee/placement_auto.hpp"
#include "promethee/synchronisation.hpp"

namespace prom::ecad {

namespace {
double mmDe(Nm v) { return static_cast<double>(v) / 1e6; }
std::string majuscules(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}
Pastille pastilleCms(const std::string& numero, double x, double y, double l, double h) {
  Pastille p;
  p.numero = numero;
  p.type = TypePastille::CMS;
  p.forme = FormePastille::RectArrondi;
  p.position = ptMm(x, y);
  p.largeur = nm(l);
  p.hauteur = nm(h);
  return p;
}
void cadre(DefEmpreinte& d, double l, double h) {
  const double x0 = -l / 2, y0 = -h / 2, x1 = l / 2, y1 = h / 2;
  for (const auto& [a, b] : std::vector<std::pair<Pt, Pt>>{{ptMm(x0, y0), ptMm(x1, y0)}, {ptMm(x1, y0), ptMm(x1, y1)}, {ptMm(x1, y1), ptMm(x0, y1)}, {ptMm(x0, y1), ptMm(x0, y0)}}) {
    TraitEmpreinte t;
    t.couche = Couche::FabricationDessus;
    t.a = a, t.b = b;
    d.traits.push_back(t);
  }
}
}  // namespace

const std::vector<ModuleGalerie>& galerieModules() {
  static const std::vector<ModuleGalerie> v = [] {
    std::vector<ModuleGalerie> g;
    ModuleGalerie s3;
    s3.id = "esp32-s3-wroom-1";
    s3.nom = "ESP32-S3-WROOM-1";
    s3.fabricant = "Espressif";
    s3.mpn = "ESP32-S3-WROOM-1-N8";
    s3.description = "Module Wi-Fi et Bluetooth LE à ESP32-S3, USB natif (IO19 D-, IO20 D+), 3,3 V ; brochage de la fiche Espressif, dessin à vérifier avant fabrication";
    const char* noms[] = {"GND",  "3V3",  "EN",   "IO4",  "IO5",  "IO6",  "IO7",  "IO15", "IO16", "IO17", "IO18", "IO8",  "IO19", "IO20",
                          "IO3",  "IO46", "IO9",  "IO10", "IO11", "IO12", "IO13", "IO14", "IO21", "IO47", "IO48", "IO45", "IO0",  "IO35",
                          "IO36", "IO37", "IO38", "IO39", "IO40", "IO41", "IO42", "RXD0", "TXD0", "IO2",  "IO1",  "GND",  "GND"};
    for (int i = 0; i < 41; ++i) s3.broches.emplace_back(std::to_string(i + 1), noms[i]);
    s3.interfaces = {{"usb", {"13", "14"}}, {"i2c", {"21", "22"}}, {"serie", {"36", "37"}}, {"led", {"12"}}};
    s3.centrale = 4;
    g.push_back(s3);
    return g;
  }();
  return v;
}

const ModuleGalerie* moduleGalerie(const std::string& id) {
  for (const auto& m : galerieModules())
    if (m.id == id) return &m;
  return nullptr;
}

DefSymbole symboleModule(const ModuleGalerie& m) {
  SpecSymbole s;
  s.id = "galerie:" + m.id;
  s.nom = m.nom;
  s.prefixe = "U";
  s.description = m.description;
  s.empreinte = "galerie:" + m.id;
  s.fabricant = m.fabricant;
  s.mpn = m.mpn;
  std::set<std::string> vus;
  for (const auto& [n, nom] : m.broches) {
    SpecBroche b;
    b.numero = n;
    b.nom = nom;
    b.type = nom == "GND" ? TypeBroche::AlimEntree : nom == m.alimentation ? TypeBroche::AlimEntree : nom == m.activation ? TypeBroche::Entree : TypeBroche::Bidirectionnelle;
    s.broches.push_back(b);
  }
  return genererSymbole(s);
}

DefEmpreinte empreinteModule(const ModuleGalerie& m) {
  DefEmpreinte d;
  d.id = "galerie:" + m.id;
  d.description = m.description;
  d.famille = "module";
  const double L = m.largeur, H = m.hauteur, p = m.pas;
  const double xc = L / 2 - 0.25, yb = H / 2 - 0.25;  // centres des rangées : pastilles à cheval sur le bord du module
  std::size_t n = 0;
  auto suivante = [&]() -> const std::string& { return m.broches[n++].first; };
  // Côté gauche, de haut en bas ; bas, de gauche à droite ; droite, de bas en haut (sens des fiches Espressif)
  const double yBasGauche = H / 2 - 1.5, yHautGauche = yBasGauche - (m.gauche - 1) * p;
  for (int i = 0; i < m.gauche && n < m.broches.size(); ++i) d.pastilles.push_back(pastilleCms(suivante(), -xc, yHautGauche + i * p, 1.5, 0.9));
  const double xBas0 = -(m.bas - 1) * p / 2;
  for (int i = 0; i < m.bas && n < m.broches.size(); ++i) d.pastilles.push_back(pastilleCms(suivante(), xBas0 + i * p, yb, 0.9, 1.5));
  for (int i = 0; i < m.droite && n < m.broches.size(); ++i) d.pastilles.push_back(pastilleCms(suivante(), xc, yBasGauche - i * p, 1.5, 0.9));
  if (m.centrale > 0 && n < m.broches.size()) {
    Pastille c = pastilleCms(suivante(), -1.5, 2.46, m.centrale, m.centrale);
    c.forme = FormePastille::Rect;
    d.pastilles.push_back(c);
  }
  cadre(d, L, H);
  d.encombrement = {nm(-xc - 1.0), nm(-H / 2 - 0.25), nm(xc + 1.0), nm(yb + 1.0)};
  d.corps.largeur = nm(L), d.corps.profondeur = nm(H), d.corps.hauteur = nm(3.2);
  d.corps.couleur = "#B8BCC4";
  return d;
}

const std::vector<std::pair<std::string, std::string>>& interfacesGuidees() {
  static const std::vector<std::pair<std::string, std::string>> v = {
      {"usb", "Port USB micro-B : alimentation par le bus (régulateur 3,3 V et ses condensateurs) et données vers le module, lignes protégées contre les décharges"},
      {"i2c", "Connecteur I2C 4 points (3V3, SDA, SCL, GND) et tirages de 4,7 kΩ"},
      {"serie", "Connecteur série 3 points (TX, RX, GND)"},
      {"led", "Témoin à LED sur une broche libre, avec sa résistance"},
  };
  return v;
}

namespace {
// Symbole et empreinte d'un connecteur USB micro-B (face d'accouplement vers la gauche) et d'une protection ESD
DefSymbole symboleUsb() {
  SpecSymbole s;
  s.id = "galerie:USB_B_Micro";
  s.nom = "USB micro-B";
  s.prefixe = "J";
  s.empreinte = "galerie:USB_Micro-B";
  s.description = "Connecteur USB 2.0 micro-B, femelle, pour montage en surface";
  const std::vector<std::pair<std::string, std::string>> b = {{"1", "VBUS"}, {"2", "D-"}, {"3", "D+"}, {"4", "ID"}, {"5", "GND"}, {"6", "SHIELD"}};
  for (const auto& [n, nom] : b) {
    SpecBroche x;
    x.numero = n;
    x.nom = nom;
    x.type = nom == "VBUS" ? TypeBroche::AlimSortie : TypeBroche::Passive;
    s.broches.push_back(x);
  }
  return genererSymbole(s);
}
DefEmpreinte empreinteUsb() {
  DefEmpreinte d;
  d.id = "galerie:USB_Micro-B";
  d.description = "USB micro-B CMS, cinq contacts au pas de 0,65 mm, deux pattes de blindage ; à vérifier sur la fiche du connecteur retenu";
  d.famille = "connecteur";
  for (int i = 0; i < 5; ++i) d.pastilles.push_back(pastilleCms(std::to_string(i + 1), 1.6, -1.3 + 0.65 * i, 1.35, 0.4));
  d.pastilles.push_back(pastilleCms("6", -0.4, -3.6, 2.0, 1.8));
  Pastille b = pastilleCms("6", -0.4, 3.6, 2.0, 1.8);
  d.pastilles.push_back(b);
  cadre(d, 5.0, 7.5);
  d.encombrement = {nm(-2.5), nm(-4.7), nm(2.6), nm(4.7)};
  d.corps.largeur = nm(5.0), d.corps.profondeur = nm(7.5), d.corps.hauteur = nm(2.5);
  d.corps.couleur = "#C0C4CA";
  return d;
}
DefSymbole symboleEsd() {
  SpecSymbole s;
  s.id = "galerie:USBLC6-2SC6";
  s.nom = "USBLC6-2SC6";
  s.prefixe = "U";
  s.empreinte = "galerie:SOT-23-6";
  s.fabricant = "STMicroelectronics";
  s.mpn = "USBLC6-2SC6";
  s.description = "Protection ESD de deux lignes USB, SOT-23-6";
  const std::vector<std::pair<std::string, std::string>> b = {{"1", "IO1"}, {"2", "GND"}, {"3", "IO2"}, {"4", "IO2'"}, {"5", "VBUS"}, {"6", "IO1'"}};
  for (const auto& [n, nom] : b) {
    SpecBroche x;
    x.numero = n;
    x.nom = nom;
    x.type = TypeBroche::Passive;
    s.broches.push_back(x);
  }
  return genererSymbole(s);
}
DefEmpreinte empreinteSot236() {
  DefEmpreinte d;
  d.id = "galerie:SOT-23-6";
  d.description = "SOT-23-6, pas de 0,95 mm";
  d.famille = "SOT-23";
  for (int i = 0; i < 3; ++i) d.pastilles.push_back(pastilleCms(std::to_string(i + 1), -0.95 + 0.95 * i, 1.15, 0.6, 1.0));
  for (int i = 0; i < 3; ++i) d.pastilles.push_back(pastilleCms(std::to_string(4 + i), 0.95 - 0.95 * i, -1.15, 0.6, 1.0));
  cadre(d, 2.9, 1.6);
  d.encombrement = {nm(-1.7), nm(-1.9), nm(1.7), nm(1.9)};
  d.corps.largeur = nm(2.9), d.corps.profondeur = nm(1.6), d.corps.hauteur = nm(1.1);
  return d;
}
// Fil de 2,54 mm tiré de la broche vers l'extérieur du symbole, terminé par une étiquette globale
void etiqueter(ConceptionElec& c, const BrochePlacee& b, const std::string& reseau) {
  const Pt d = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
  const Pt bout{b.p.x - d.x * 2'540'000, b.p.y - d.y * 2'540'000};
  c.feuilles[0].fils.push_back({nouvelUuid(), b.p, bout});
  Etiquette e;
  e.uuid = nouvelUuid();
  e.texte = reseau;
  e.p = bout;
  e.type = TypeEtiquette::Globale;
  c.feuilles[0].etiquettes.push_back(e);
}
// Pose un symbole et relie ses broches par des étiquettes, comme on le ferait à la main ; une broche sans liaison
// reçoit une marque de non-connexion. Rend l'UUID du composant (vide pour un drapeau).
std::string poser(ConceptionElec& c, const std::string& symbole, Pt position, const std::string& ref, const std::string& valeur,
                  const std::multimap<std::string, std::string>& liaisons, const std::string& mpn = {}) {
  const std::string s = poserSymbole(c, 0, symbole, position);
  for (auto& p : c.feuilles[0].symboles) {
    if (p.uuid != s) continue;
    if (ComposantElec* k = c.composant(p.composant)) {
      k->reference = ref;
      k->valeur = valeur;
      if (!mpn.empty()) k->mpn = mpn;
    }
    for (const auto& b : brochesPlacees(p, *c.symbole(p.symbole))) {
      const auto it = liaisons.find(b.broche->numero);
      if (it != liaisons.end()) etiqueter(c, b, it->second);
      else c.feuilles[0].nonConnexions.push_back({b.p, nouvelUuid()});
    }
    return p.composant;
  }
  return {};
}
EmpreintePlacee* placee(ConceptionElec& c, const std::string& composant) {
  for (auto& e : c.circuit.empreintes)
    if (e.composant == composant) return &e;
  return nullptr;
}
}  // namespace

ResultatGuide concevoirParModules(const DemandeGuidee& dem) {
  ResultatGuide r;
  const ModuleGalerie* mod = moduleGalerie(dem.module);
  if (!mod) {
    r.refus.push_back("Module « " + dem.module + " » absent de la galerie.");
    return r;
  }
  if (dem.largeur < 30 || dem.hauteur < mod->hauteur + 3) {
    r.refus.push_back("Gabarit trop petit pour le module (" + std::to_string(static_cast<int>(mod->largeur)) + " × " + std::to_string(static_cast<int>(mod->hauteur)) + " mm).");
    return r;
  }
  ConceptionElec& c = r.conception;
  c = nouvelleConception(dem.nom);
  c.circuit.contour.libre.clear();
  c.circuit.contour.largeur = nm(dem.largeur);
  c.circuit.contour.hauteur = nm(dem.hauteur);
  c.circuit.contour.rayon = nm(1);
  c.circuit.regles.isolement = nm(0.15);
  c.circuit.regles.largeur = nm(0.25);
  c.circuit.regles.largeurMin = nm(0.15);
  // Galerie : symboles et empreintes figés dans le projet
  const DefSymbole sm = symboleModule(*mod), su = symboleUsb(), se = symboleEsd();
  c.symboles[sm.id] = sm;
  c.symboles[su.id] = su;
  c.symboles[se.id] = se;
  c.empreintes["galerie:" + mod->id] = empreinteModule(*mod);
  c.empreintes["galerie:USB_Micro-B"] = empreinteUsb();
  c.empreintes["galerie:SOT-23-6"] = empreinteSot236();
  // Module au milieu de la feuille, ses circuits types sur la rangée du haut, l'USB sur celle du bas
  std::multimap<std::string, std::string> lm;
  for (const auto& [n, nom] : mod->broches) {
    if (nom == mod->masse) lm.emplace(n, "GND");
    else if (nom == mod->alimentation) lm.emplace(n, "+3V3");
    else if (nom == mod->activation) lm.emplace(n, "EN");
  }
  const bool usb = std::find(dem.interfaces.begin(), dem.interfaces.end(), "usb") != dem.interfaces.end();
  for (const auto& i : dem.interfaces)
    if (i != "usb") r.journal.push_back("Interface « " + i + " » : pas encore posée par le guide, à ajouter à la main");
  if (usb)
    for (const auto& [i, b] : mod->interfaces)
      if (i == "usb" && b.size() == 2) lm.emplace(b[0], "USB_DM"), lm.emplace(b[1], "USB_DP");
  const std::string u1 = poser(c, sm.id, ptMm(150, 105), "U1", mod->nom, lm, mod->mpn);
  r.journal.push_back("Module " + mod->nom + " posé (U1), alimenté en 3,3 V");
  double x = 30;
  auto rangee = [&x](double y) {
    const Pt p = ptMm(x, y);
    x += 25;
    return p;
  };
  const std::string c3 = poser(c, "C", rangee(30), "C3", "100n", {{"1", "+3V3"}, {"2", "GND"}});
  const std::string c4 = poser(c, "C", rangee(30), "C4", "10u", {{"1", "+3V3"}, {"2", "GND"}});
  const std::string r1 = poser(c, "R", rangee(30), "R1", "10k", {{"1", "+3V3"}, {"2", "EN"}});
  const std::string c5 = poser(c, "C", rangee(30), "C5", "1u", {{"1", "EN"}, {"2", "GND"}});
  for (const std::string& k : {c3, c4, r1, c5})
    if (ComposantElec* y = c.composant(k)) y->empreinte = y->reference == "C4" ? "C_0805" : y->reference[0] == 'R' ? "R_0603" : "C_0603";
  r.journal.push_back("Circuits types du module : découplage de 100 nF et 10 µF sur 3V3, tirage de 10 kΩ et retard de 1 µF sur EN");
  std::string j1, u2, u3, c1, c2;
  x = 30;
  if (usb) {
    j1 = poser(c, su.id, rangee(180), "J1", "USB micro-B", {{"1", "VBUS"}, {"2", "USB_DM"}, {"3", "USB_DP"}, {"5", "GND"}, {"6", "GND"}});
    u3 = poser(c, se.id, rangee(180), "U3", "USBLC6-2SC6", {{"1", "USB_DM"}, {"6", "USB_DM"}, {"3", "USB_DP"}, {"4", "USB_DP"}, {"2", "GND"}, {"5", "VBUS"}}, "USBLC6-2SC6");
    u2 = poser(c, "Regulateur", rangee(180), "U2", "AMS1117-3.3", {{"3", "VBUS"}, {"2", "+3V3"}, {"1", "GND"}}, "AMS1117-3.3");
    c1 = poser(c, "C", rangee(180), "C1", "10u", {{"1", "VBUS"}, {"2", "GND"}});
    c2 = poser(c, "C", rangee(180), "C2", "10u", {{"1", "+3V3"}, {"2", "GND"}});
    for (const std::string& k : {c1, c2})
      if (ComposantElec* y = c.composant(k)) y->empreinte = "C_0805";
    r.journal.push_back("Port USB : J1, protection U3 sur D+ et D-, régulateur U2 de 3,3 V depuis VBUS, C1 et C2 de 10 µF");
  } else {
    // Sans USB, l'alimentation arrive de l'extérieur : drapeaux sur 3V3 et GND
    for (const char* reseau : {"+3V3", "GND"}) poser(c, "Drapeau_alim", rangee(180), {}, {}, {{"1", reseau}});
    r.journal.push_back("Sans port USB : 3V3 et GND déclarés alimentés de l'extérieur (drapeaux)");
  }
  // Carte : empreintes posées depuis le schéma, puis rangées
  synchroniser(c);
  const Pt o = c.circuit.contour.origine;
  auto mettre = [&](const std::string& composant, double x, double y, double rotation = 0) {
    if (EmpreintePlacee* e = placee(c, composant)) {
      e->position = {o.x + nm(x), o.y + nm(y)};
      e->rotation = rotation;
    }
  };
  const double W = dem.largeur, H = dem.hauteur;
  // Module contre le bord droit, antenne en haut contre le bord ; connecteur USB au bord gauche, ouverture dehors
  const Boite em = c.empreintes["galerie:" + mod->id].encombrement;
  const double mx = W - mmDe(em.x1) - 0.5, my = 0.5 - mmDe(em.y0);
  mettre(u1, mx, my);
  if (usb) {
    mettre(j1, 2.6, H / 2);
    mettre(u3, 9.0, H / 2);
    mettre(u2, 15.0, 6.0);
    mettre(c1, 9.0, 5.0, 90);
    mettre(c2, 21.0, 5.0, 90);
  }
  const double gx = mx - mod->largeur / 2 - 4.0;  // colonne des composants du module, à gauche de ses pastilles
  mettre(c3, gx, 5.0, 90);
  mettre(c4, gx - 2.5, 5.0, 90);
  mettre(r1, gx, my - 2.0, 90);
  mettre(c5, gx - 2.5, my - 2.0, 90);
  for (auto& e : c.circuit.empreintes) e.verrou = e.composant == u1 || e.composant == j1;
  // Bande d'antenne : ni piste, ni via, ni cuivre coulé, sur toutes les couches
  if (mod->antenne > 0) {
    ZoneRegle z;
    z.uuid = nouvelUuid();
    z.nom = "Antenne " + mod->nom;
    const double x0 = mx - mod->largeur / 2 - 0.5, y1 = my - mod->hauteur / 2 + mod->antenne;
    z.contour = {{o.x + nm(x0), o.y}, {o.x + nm(W), o.y}, {o.x + nm(W), o.y + nm(y1)}, {o.x + nm(x0), o.y + nm(y1)}};
    z.interditPistes = z.interditVias = z.interditZones = true;
    c.circuit.zonesRegles.push_back(z);
  }
  if (dem.placer) {
    OptionsPlacementAuto op;
    op.fixes = {u1, j1};
    op.iterations = 20'000;
    ConceptionElec essai = c;
    const ResultatPlacementAuto pa = placerAutomatiquement(essai, op);
    if (pa.collisions == 0 && collisionsPlacement(essai) == 0 && longueurChevelu(essai) < longueurChevelu(c)) {
      c = std::move(essai);
      r.journal.push_back("Placement automatique : chevelu de " + std::to_string(static_cast<int>(pa.chevelu)) + " mm");
    } else
      r.journal.push_back("Placement du gabarit gardé (le placement automatique ne faisait pas mieux)");
  }
  // Masse : plans de cuivre sur les deux faces (la bande d'antenne en est retirée au remplissage)
  for (const Couche couche : {Couche::CuivreDessus, Couche::CuivreDessous}) {
    Zone z;
    z.uuid = nouvelUuid();
    z.reseau = "GND";
    z.couche = couche;
    z.contour = {{o.x, o.y}, {o.x + nm(W), o.y}, {o.x + nm(W), o.y + nm(H)}, {o.x, o.y + nm(H)}};
    z.thermiquesTraversantsSeuls = true;  // carte refondue : les CMS se raccordent pleines
    c.circuit.zones.push_back(z);
  }
  if (dem.router) {
    const BilanRoutage b = routerComplet(c);
    r.journal.push_back("Routage : " + std::to_string(b.liaisons) + " liaison(s), " + std::to_string(b.vias) + " via(s)" + (b.complet() ? "" : ", " + std::to_string(b.restantes) + " restante(s)"));
  }
  // Vérifications
  const Connectivite k = connectivite(c);
  for (const auto& v : verifierERC(c, k, NiveauErc::Complet))
    if (v.gravite == Gravite::Erreur) ++r.erreursErc, r.violations.push_back("ERC " + v.code + " : " + v.message);
  for (const auto& v : verifierDRCReglee(c, k, remplirZones(c, k)))
    if (v.gravite == Gravite::Erreur) ++r.erreursDrc, r.violations.push_back("DRC " + v.code + " : " + v.message);
  r.journal.push_back("ERC : " + std::to_string(r.erreursErc) + " erreur(s) ; DRC : " + std::to_string(r.erreursDrc) + " erreur(s)");
  return r;
}

std::vector<PropositionCircuitType> circuitsTypes(const ConceptionElec& c) {
  std::vector<PropositionCircuitType> v;
  const Connectivite k = connectivite(c);
  std::set<std::string> reseaux;
  for (const auto& n : k.reseaux) reseaux.insert(majuscules(n.nom));
  // Découplage : chaque circuit intégré dont une broche d'alimentation n'a pas de condensateur sur son réseau
  for (const auto& x : c.composants) {
    const DefSymbole* d = c.symbole(x.symbole);
    if (!d || x.reference.empty() || x.reference[0] != 'U') continue;
    for (const auto& b : d->broches) {
      if (b.type != TypeBroche::AlimEntree) continue;
      const std::string n = k.nom(x.uuid, b.numero);
      if (n.empty() || majuscules(n) == "GND") continue;
      bool condensateur = false;
      for (const auto& r : k.reseaux)
        if (r.nom == n)
          for (const auto& bb : r.broches)
            if (const ComposantElec* y = c.composant(bb.composant); y && (y->symbole == "C" || y->symbole == "C_Pol")) condensateur = true;
      if (!condensateur) v.push_back({"découplage", x.reference, "Condensateur de 100 nF entre " + n + " et GND, au plus près de " + x.reference + " broche " + b.numero, {n, "GND"}});
    }
  }
  // Tirages I2C, protection USB
  for (const auto& [a, b] : std::vector<std::pair<std::string, std::string>>{{"SDA", "SCL"}})
    if (reseaux.count(a) && reseaux.count(b)) v.push_back({"tirage", a + "/" + b, "Tirages de 4,7 kΩ vers 3V3 sur " + a + " et " + b, {a, b}});
  for (const auto& [a, b] : std::vector<std::pair<std::string, std::string>>{{"D+", "D-"}, {"USB_DP", "USB_DM"}})
    if (reseaux.count(a) && reseaux.count(b)) {
      bool protege = false;
      for (const auto& x : c.composants) protege |= x.mpn.find("USBLC6") != std::string::npos || x.valeur.find("ESD") != std::string::npos;
      if (!protege) v.push_back({"esd", a + "/" + b, "Protection contre les décharges (USBLC6-2SC6) sur " + a + " et " + b, {a, b}});
    }
  return v;
}

std::vector<std::string> poserCircuitType(ConceptionElec& c, const PropositionCircuitType& p, std::string* erreur) {
  auto refuser = [&](const std::string& m) {
    if (erreur) *erreur = m;
    return std::vector<std::string>{};
  };
  if (c.feuilles.empty()) return refuser("La conception n'a pas de feuille.");
  // Sous ce qui est déjà posé sur la première feuille
  double y = 30;
  for (const auto& s : c.feuilles[0].symboles) y = std::max(y, mmDe(s.position.y) + 25);
  y = std::min(y, 190.0);
  double x = 30;
  auto suivant = [&] {
    const Pt q = ptMm(x, y);
    x += 25;
    return q;
  };
  // Réseau d'alimentation 3,3 V du projet, s'il existe
  std::string vcc = "+3V3";
  const Connectivite k = connectivite(c);
  for (const auto& n : k.reseaux)
    if (majuscules(n.nom) == "3V3" || majuscules(n.nom) == "+3V3" || majuscules(n.nom) == "+3.3V") vcc = n.nom;
  std::vector<std::string> poses;
  if (p.regle == "découplage" && !p.reseaux.empty()) {
    poses.push_back(poser(c, "C", suivant(), "C?", "100n", {{"1", p.reseaux[0]}, {"2", p.reseaux.size() > 1 ? p.reseaux[1] : std::string("GND")}}));
  } else if (p.regle == "tirage") {
    for (const auto& n : p.reseaux) poses.push_back(poser(c, "R", suivant(), "R?", "4k7", {{"1", vcc}, {"2", n}}));
  } else if (p.regle == "esd" && p.reseaux.size() == 2) {
    const DefSymbole se = symboleEsd();
    if (!c.symboles.count(se.id)) c.symboles[se.id] = se;
    if (!c.empreintes.count("galerie:SOT-23-6")) c.empreintes["galerie:SOT-23-6"] = empreinteSot236();
    std::string bus = "VBUS";
    for (const auto& n : k.reseaux)
      if (majuscules(n.nom) == "VBUS" || majuscules(n.nom) == "+5V") bus = n.nom;
    // Les réseaux sont nommés D+ puis D- (ou USB_DP puis USB_DM)
    const std::string dp = p.reseaux[0], dm = p.reseaux[1];
    poses.push_back(poser(c, se.id, suivant(), "U?", "USBLC6-2SC6", {{"1", dm}, {"6", dm}, {"3", dp}, {"4", dp}, {"2", "GND"}, {"5", bus}}, "USBLC6-2SC6"));
  } else
    return refuser("Circuit type « " + p.regle + " » inconnu.");
  poses.erase(std::remove(poses.begin(), poses.end(), std::string()), poses.end());
  if (poses.empty()) return refuser("Rien n'a pu être posé.");
  return poses;
}

}  // namespace prom::ecad

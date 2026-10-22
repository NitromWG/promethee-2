// Essais indépendants du noyau électronique de Prométhée v0.25 (audit). Chaque essai construit un cas, mesure, et
// imprime « DÉFAUT CONFIRMÉ » ou « conforme » avec les chiffres. Compilé contre la bibliothèque du noyau.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

#include "promethee/derive.hpp"
#include "promethee/electronique.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;
namespace fs = std::filesystem;

static int g_defauts = 0, g_conformes = 0;
static void rapport(const std::string& id, bool defaut, const std::string& detail) {
  std::printf("[%s] %s — %s\n", id.c_str(), defaut ? "DÉFAUT CONFIRMÉ" : "conforme", detail.c_str());
  std::fflush(stdout);
  (defaut ? g_defauts : g_conformes)++;
}
static std::string mmS(double nmv) {
  char t[32];
  std::snprintf(t, sizeof t, "%.3f mm", nmv * 1e-6);
  return t;
}

// ---------- Aides ----------
static const SymbolePlace* place(const ConceptionElec& c, const std::string& sym) {
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == sym) return &s;
  return nullptr;
}
static std::string compDe(const ConceptionElec& c, const std::string& sym) {
  const SymbolePlace* s = place(c, sym);
  return s ? s->composant : std::string();
}
static Pt broche(const ConceptionElec& c, const std::string& sym, const std::string& num) {
  const SymbolePlace* s = place(c, sym);
  if (!s) return {};
  for (const auto& b : brochesPlacees(*s, *c.symbole(s->symbole)))
    if (b.broche->numero == num) return b.p;
  std::printf("  !! broche %s introuvable\n", num.c_str());
  return {};
}
static void fil(ConceptionElec& c, Pt a, Pt b, int f = 0) { c.feuilles[static_cast<std::size_t>(f)].fils.push_back({nouvelUuid(), a, b}); }
static void relier(ConceptionElec& c, Pt a, Pt b, int f = 0) {
  if (a.x != b.x && a.y != b.y) {
    fil(c, a, {b.x, a.y}, f);
    fil(c, {b.x, a.y}, b, f);
  } else
    fil(c, a, b, f);
}
static void etiquette(ConceptionElec& c, const std::string& t, Pt p, bool globale, int f = 0) {
  c.feuilles[static_cast<std::size_t>(f)].etiquettes.push_back({nouvelUuid(), t, p, 0, globale ? TypeEtiquette::Globale : TypeEtiquette::Locale});
}
static int compter(const std::vector<Violation>& v, const std::string& code) {
  return static_cast<int>(std::count_if(v.begin(), v.end(), [&](const Violation& x) { return x.code == code; }));
}
static std::string codes(const std::vector<Violation>& v, Gravite g) {
  std::map<std::string, int> m;
  for (const auto& x : v)
    if (x.gravite == g) m[x.code]++;
  std::string r;
  for (const auto& [k, n] : m) r += (r.empty() ? "" : ", ") + k + "×" + std::to_string(n);
  return r.empty() ? "aucune" : r;
}
static EmpreintePlacee* fp(ConceptionElec& c, const std::string& sym) {
  const std::string id = compDe(c, sym);
  for (auto& e : c.circuit.empreintes)
    if (e.composant == id) return &e;
  return nullptr;
}
static const PastillePlacee* pad(const std::vector<PastillePlacee>& pp, const std::string& comp, const std::string& num) {
  for (const auto& p : pp)
    if (p.composant == comp && p.numero == num) return &p;
  return nullptr;
}
struct Analyse {
  Connectivite k;
  std::vector<Violation> erc, drc;
  Remplissages z;
  EtatRoutage e;
  std::vector<PastillePlacee> pp;
};
static Analyse analyser(const ConceptionElec& c) {
  Analyse a;
  a.k = connectivite(c);
  a.erc = verifierERC(c, a.k);
  a.pp = pastilles(c, a.k);
  a.z = remplirZones(c, a.k);
  a.e = etatRoutage(c, a.k, a.z);
  a.drc = verifierDRC(c, a.k, a.z);
  return a;
}
static void piste(ConceptionElec& c, const std::string& reseau, Couche L, Pt a, Pt b, double largeurMm = 0.25) {
  c.circuit.pistes.push_back({nouvelUuid(), reseau, L, a, b, nm(largeurMm)});
}
static Zone zonePleine(const ConceptionElec& c, const std::string& reseau, Couche L) {
  Zone z;
  z.uuid = nouvelUuid();
  z.reseau = reseau;
  z.couche = L;
  const Boite b = boite(c.circuit.contour.polygone());
  z.contour = {{b.x0, b.y0}, {b.x1, b.y0}, {b.x1, b.y1}, {b.x0, b.y1}};
  return z;
}
static double distanceAuCuivre(const Polygone& forme, const Polygones& cuivre) {
  if (cuivre.empty()) return 1e18;
  for (const Pt& q : forme)
    if (dedans(cuivre, q)) return 0;
  for (const auto& p : cuivre)
    for (const Pt& q : p)
      if (dedans(forme, q)) return 0;
  double d = 1e18;
  for (const auto& p : cuivre)
    for (std::size_t i = 0; i < p.size(); ++i) d = std::min(d, distancePolygoneSegment(forme, p[i], p[(i + 1) % p.size()]));
  return d;
}

// ---------- Schéma et vérification électrique ----------
static void essaisERC() {
  {  // CX01a : même nom en étiquette globale et locale sur le même réseau
    ConceptionElec c = nouvelleConception("T01a");
    const auto r1 = poserSymbole(c, 0, "R", ptMm(50.8, 50.8)), r2 = poserSymbole(c, 0, "R", ptMm(76.2, 50.8));
    annoter(c, false);
    const Pt a = broche(c, r1, "2"), b = broche(c, r2, "2");
    const Pt coin{a.x, a.y + nm(5.08)};
    fil(c, a, coin);
    fil(c, coin, {b.x, coin.y});
    fil(c, {b.x, coin.y}, b);
    etiquette(c, "SDA", {nm(63.5), coin.y}, true);
    etiquette(c, "SDA", {a.x, a.y + nm(2.54)}, false);
    const auto k = connectivite(c);
    const auto erc = verifierERC(c, k);
    std::string msg;
    for (const auto& v : erc)
      if (v.code == "ERC-23") msg = v.message;
    rapport("CX01a", compter(erc, "ERC-23") > 0, "étiquette globale « SDA » + locale « SDA » sur le même fil : " + std::to_string(compter(erc, "ERC-23")) + " ERC-23 — « " + msg + " » ; dérogeable : " + (derogeable("ERC-23") ? "oui" : "non"));
  }
  {  // CX01b : symbole d'alimentation GND + étiquette globale GND
    ConceptionElec c = nouvelleConception("T01b");
    const auto r1 = poserSymbole(c, 0, "R", ptMm(50.8, 50.8));
    annoter(c, false);
    const Pt a = broche(c, r1, "2");
    const Pt bout{a.x, a.y + nm(7.62)};
    fil(c, a, bout);
    poserSymbole(c, 0, "GND", bout);
    etiquette(c, "GND", {a.x, a.y + nm(2.54)}, true);
    const auto erc = verifierERC(c, connectivite(c));
    std::string msg;
    for (const auto& v : erc)
      if (v.code == "ERC-23") msg = v.message;
    rapport("CX01b", compter(erc, "ERC-23") > 0, "symbole GND + étiquette globale « GND » sur le même fil : « " + msg + " »");
  }
  {  // CX01c : deux feuilles : +5V par symbole d'alimentation sur la feuille 1, étiquette globale « +5V » sur la feuille 2
    ConceptionElec c = nouvelleConception("T01c");
    Feuille f2;
    f2.uuid = nouvelUuid();
    f2.nom = "Feuille 2";
    c.feuilles.push_back(f2);
    const auto r1 = poserSymbole(c, 0, "R", ptMm(50.8, 50.8));
    const auto r2 = poserSymbole(c, 1, "R", ptMm(50.8, 50.8));
    annoter(c, false);
    const Pt a = broche(c, r1, "1"), b = broche(c, r2, "1");
    fil(c, a, {a.x, a.y - nm(5.08)}, 0);
    poserSymbole(c, 0, "+5V", {a.x, a.y - nm(5.08)});
    fil(c, b, {b.x, b.y - nm(5.08)}, 1);
    etiquette(c, "+5V", {b.x, b.y - nm(5.08)}, true, 1);
    const auto k = connectivite(c);
    const auto erc = verifierERC(c, k);
    rapport("CX01c", compter(erc, "ERC-23") > 0, "alimentation +5V amenée sur la feuille 2 par une étiquette globale « +5V » (montage multi-feuilles courant) : même réseau = " +
                                                    std::string(k.reseau(compDe(c, r1), "1") == k.reseau(compDe(c, r2), "1") ? "oui" : "non") + ", ERC-23 = " + std::to_string(compter(erc, "ERC-23")) + " (non dérogeable)");
  }
  {  // CX02 : alias légitime (+5V et VCC_5V) bloquant à vie
    ConceptionElec c = nouvelleConception("T02");
    const auto r1 = poserSymbole(c, 0, "R", ptMm(50.8, 50.8));
    annoter(c, false);
    const Pt a = broche(c, r1, "1");
    const Pt bout{a.x, a.y - nm(7.62)};
    fil(c, a, bout);
    poserSymbole(c, 0, "+5V", bout);
    etiquette(c, "VCC_5V", {a.x, a.y - nm(2.54)}, false);
    const auto erc = verifierERC(c, connectivite(c));
    rapport("CX02", compter(erc, "ERC-23") > 0 && !derogeable("ERC-23"), "alimentation +5V nommée aussi « VCC_5V » par une étiquette : ERC-23 (" + std::to_string(compter(erc, "ERC-23")) + "), jamais dérogeable");
  }
  {  // CX04 : un condensateur de découplage suffit à déclarer « alimenté » un réseau que rien n'alimente
    ConceptionElec c = nouvelleConception("T04");
    const auto u = poserSymbole(c, 0, "ATtiny85", ptMm(101.6, 76.2));
    const auto cap = poserSymbole(c, 0, "C", ptMm(127, 63.5));
    annoter(c, false);
    const Pt vcc = broche(c, u, "8"), c1 = broche(c, cap, "1"), c2 = broche(c, cap, "2"), gnd = broche(c, u, "4");
    relier(c, vcc, c1);
    const Pt g1{c2.x, c2.y + nm(5.08)};
    fil(c, c2, g1);
    poserSymbole(c, 0, "GND", g1);
    const Pt g2{gnd.x, gnd.y + nm(5.08)};
    fil(c, gnd, g2);
    poserSymbole(c, 0, "GND", g2);
    const auto k = connectivite(c);
    const auto erc = verifierERC(c, k);
    const std::string nomVcc = k.nom(compDe(c, u), "8");
    bool erc22Vcc = false;
    for (const auto& v : erc)
      if (v.code == "ERC-22" && v.message.find(nomVcc) != std::string::npos) erc22Vcc = true;
    rapport("CX04", !erc22Vcc, "VCC de l'ATtiny85 relié seulement à un condensateur (aucune source) : ERC-22 sur « " + nomVcc + " » = " + (erc22Vcc ? "oui" : "non") + " ; erreurs ERC : " + codes(erc, Gravite::Erreur));
  }
  {  // CX05 : deux entrées reliées par étiquettes, aucun pilote
    for (const bool parEtiquette : {false, true}) {
      ConceptionElec c = nouvelleConception("T05");
      const auto u = poserSymbole(c, 0, "74HC00", ptMm(101.6, 76.2));
      annoter(c, false);
      const Pt a = broche(c, u, "1"), b = broche(c, u, "4");
      if (parEtiquette) {
        const Pt pa{a.x - nm(5.08), a.y}, pb{b.x - nm(5.08), b.y};
        fil(c, a, pa);
        fil(c, b, pb);
        etiquette(c, "HORLOGE", pa, false);
        etiquette(c, "HORLOGE", pb, false);
      } else {
        const Pt pa{a.x - nm(5.08), a.y};
        fil(c, a, pa);
        fil(c, pa, {pa.x, b.y});
        fil(c, {pa.x, b.y}, b);
      }
      const auto k = connectivite(c);
      const auto erc = verifierERC(c, k);
      const std::string nomA = k.nom(compDe(c, u), "1");
      int surA = 0;
      for (const auto& v : erc)
        if (v.code == "ERC-21" && v.message.find("« " + nomA + " »") != std::string::npos) ++surA;
      if (parEtiquette)
        rapport("CX05", surA == 0, "deux entrées 1A et 2A du 74HC00 reliées par l'étiquette « HORLOGE », sans aucun pilote : ERC-21 sur ce réseau = " + std::to_string(surA));
      else
        std::printf("  (témoin) mêmes entrées reliées par un fil : ERC-21 sur « %s » = %d\n", nomA.c_str(), surA);
    }
  }
  {  // CX06 : broche de type « non connectée » câblée
    ConceptionElec c = nouvelleConception("T06");
    SpecSymbole sp;
    sp.nom = "CI essai";
    sp.empreinte = "SOT-23";
    sp.broches = lireListeBroches("1 IN in\n2 OUT out\n3 NC nc");
    DefSymbole d = genererSymbole(sp);
    c.symboles[d.id] = d;
    const auto u = poserSymbole(c, 0, d.id, ptMm(101.6, 76.2));
    const auto r = poserSymbole(c, 0, "R", ptMm(127, 76.2));
    annoter(c, false);
    relier(c, broche(c, u, "3"), broche(c, r, "1"));
    const auto erc = verifierERC(c, connectivite(c));
    bool cite = false;
    for (const auto& v : erc)
      if (v.message.find("broche 3") != std::string::npos) cite = true;
    rapport("CX06", !cite, std::string("broche de type « non connectée » reliée à une résistance : ") + (cite ? "signalée" : "rien de signalé") + " ; ERC : " + codes(erc, Gravite::Erreur) + " / " + codes(erc, Gravite::Avertissement));
  }
  {  // CX07 : marque de non-connexion posée dans le vide
    ConceptionElec c = nouvelleConception("T07");
    poserSymbole(c, 0, "R", ptMm(50.8, 50.8));
    annoter(c, false);
    const auto avant = verifierERC(c, connectivite(c));
    c.feuilles[0].nonConnexions.push_back({ptMm(120, 120), nouvelUuid()});
    const auto apres = verifierERC(c, connectivite(c));
    rapport("CX07", avant.size() == apres.size(), "marque « non connecté » posée loin de toute broche : " + std::to_string(apres.size() - avant.size()) + " violation(s) ajoutée(s)");
  }
  {  // CX41 : ERC-31 sur une étiquette qui nomme simplement un réseau
    ConceptionElec c = nouvelleConception("T41");
    const auto r1 = poserSymbole(c, 0, "R", ptMm(50.8, 50.8)), r2 = poserSymbole(c, 0, "R", ptMm(76.2, 50.8));
    annoter(c, false);
    relier(c, broche(c, r1, "2"), broche(c, r2, "2"));
    etiquette(c, "MESURE", broche(c, r1, "2"), false);
    const auto erc = verifierERC(c, connectivite(c));
    rapport("CX41", compter(erc, "ERC-31") > 0, "étiquette « MESURE » posée une fois pour nommer un réseau déjà relié : ERC-31 « utilisée une seule fois » = " + std::to_string(compter(erc, "ERC-31")));
  }
  {  // CX42 : régulateur créé depuis une liste de broches : VOUT n'est pas reconnue comme sortie d'alimentation
    const auto b = lireListeBroches("1 VIN\n2 GND\n3 VOUT\n4 EN\n5 NRST I\n6 PA0 I/O\n7 PA1 / ADC1 io");
    std::string t;
    for (const auto& x : b) t += x.numero + ":" + x.nom + "=" + codeTypeBroche(x.type) + " ";
    const bool defaut = b.size() >= 3 && b[2].type != TypeBroche::AlimSortie;
    rapport("CX42", defaut, "liste « 1 VIN / 2 GND / 3 VOUT / … » lue comme : " + t);
  }
  {  // CX43 : « Annoter les nouveaux » renomme des repères choisis à la main
    ConceptionElec c = nouvelleConception("T43");
    const auto a = poserSymbole(c, 0, "Point_test", ptMm(50.8, 50.8)), b = poserSymbole(c, 0, "LED", ptMm(76.2, 50.8)), d = poserSymbole(c, 0, "LM358", ptMm(101.6, 50.8)),
               e = poserSymbole(c, 0, "R", ptMm(127, 50.8));
    c.composant(compDe(c, a))->reference = "TP_VCC";
    c.composant(compDe(c, b))->reference = "LED_PWR";
    c.composant(compDe(c, d))->reference = "U1A";
    (void)e;  // R? : le seul vraiment nouveau
    annoter(c, false);
    const std::string t = c.composant(compDe(c, a))->reference + ", " + c.composant(compDe(c, b))->reference + ", " + c.composant(compDe(c, d))->reference + ", " + c.composant(compDe(c, e))->reference;
    rapport("CX43", c.composant(compDe(c, a))->reference != "TP_VCC" || c.composant(compDe(c, d))->reference != "U1A",
            "repères TP_VCC, LED_PWR, U1A et un nouveau R? → après « Annoter les nouveaux » : " + t);
  }
}

// ---------- Réseaux renommés : pistes orphelines ----------
struct Carte {
  ConceptionElec c;
  std::string j, r1, r2, r3;
};
// J1 (2 broches) → R1 → R2 → retour à J1.2 ; option : J1.2 à la masse, R3 en parallèle sur R2 (avec ou sans empreinte)
static Carte carteDeuxResistances(bool masse = false, int r3 = 0, bool router = true) {
  Carte k;
  ConceptionElec& c = k.c;
  c = nouvelleConception("Deux resistances");
  k.j = poserSymbole(c, 0, "Conn_01x02", ptMm(25.4, 50.8));
  k.r1 = poserSymbole(c, 0, "R", ptMm(40.64, 49.53), 1);
  k.r2 = poserSymbole(c, 0, "R", ptMm(55.88, 49.53), 1);
  if (r3) k.r3 = poserSymbole(c, 0, "R", ptMm(55.88, 40.64), 1);
  annoter(c, false);
  fil(c, broche(c, k.j, "1"), broche(c, k.r1, "1"));
  fil(c, broche(c, k.r1, "2"), broche(c, k.r2, "1"));
  const Pt r22 = broche(c, k.r2, "2"), j2 = broche(c, k.j, "2");
  fil(c, r22, ptMm(59.69, 60.96));
  fil(c, ptMm(59.69, 60.96), ptMm(17.78, 60.96));
  fil(c, ptMm(17.78, 60.96), ptMm(17.78, 52.07));
  fil(c, ptMm(17.78, 52.07), j2);
  if (masse) poserSymbole(c, 0, "GND", ptMm(38.1, 60.96));
  if (r3) {
    fil(c, broche(c, k.r3, "1"), broche(c, k.r2, "1"));
    fil(c, broche(c, k.r3, "2"), broche(c, k.r2, "2"));
    if (r3 == 2) c.composant(compDe(c, k.r3))->empreinte.clear();
  }
  c.circuit.contour.largeur = nm(40);
  c.circuit.contour.hauteur = nm(25);
  mettreAJourCircuit(c);
  fp(c, k.j)->position = ptMm(6, 12.5);
  fp(c, k.r1)->position = ptMm(16, 8);
  fp(c, k.r2)->position = ptMm(28, 8);
  if (r3 == 1) fp(c, k.r3)->position = ptMm(28, 17);
  for (auto& x : c.composants) { x.fabricant = "Yageo"; x.mpn = "RC0603FR-07" + x.reference; }
  c.cartouche = {"Essai", "Audit", "Audit", "A", "2026-10-10", ""};
  if (router) routerSimple(c);
  return k;
}

static void essaisNomsDeReseaux() {
  {
    Carte k = carteDeuxResistances();
    ConceptionElec& c = k.c;
    const Analyse a0 = analyser(c);
    const std::string avant = a0.k.nom(compDe(c, k.r1), "2");
    c.composant(compDe(c, k.r1))->reference = "R5";  // l'utilisateur renumérote R1 en R5 dans le panneau
    const Analyse a1 = analyser(c);
    std::set<std::string> rp;
    for (const auto& p : c.circuit.pistes) rp.insert(p.reseau);
    std::string l;
    for (const auto& r : rp) l += r + " ";
    rapport("CX03a", compter(a1.drc, "DRC-21") > 0 || compter(a1.drc, "DRC-20") > 0,
            "carte routée, DRC avant : " + codes(a0.drc, Gravite::Erreur) + " ; R1 renommé R5 : le réseau « " + avant + " » devient « " + a1.k.nom(compDe(c, k.r1), "2") + " » ; DRC après : " +
                codes(a1.drc, Gravite::Erreur) + " ; réseaux portés par les pistes : " + l + "; erreurs critiques de validation : " + std::to_string(valider(c).critiques()));
  }
  {
    Carte k = carteDeuxResistances();
    ConceptionElec& c = k.c;
    const Analyse a0 = analyser(c);
    const auto cap = poserSymbole(c, 0, "C", ptMm(48.26, 33.02));
    annoter(c, false);
    fil(c, broche(c, cap, "2"), ptMm(48.26, 49.53));  // descend sur le fil R1-R2 (jonction en T)
    const auto bil = mettreAJourCircuit(c);
    fp(c, cap)->position = ptMm(22, 16);
    const Analyse a = analyser(c);
    // Correction de l'essai : la liaison de C1, qui vient d'être posé, reste légitimement à router (DRC-20) ;
    // le défaut est le renommage du réseau et les courts-circuits qu'il crée
    rapport("CX03b", compter(a.drc, "DRC-21") > 0 || a0.k.nom(compDe(c, k.r1), "2") != a.k.nom(compDe(c, k.r1), "2"),
            "C1 ajouté sur le réseau R1-R2 déjà routé puis F8 (" + std::to_string(bil.ajoutees) + " empreinte ajoutée) : réseau « " + a0.k.nom(compDe(c, k.r1), "2") + " » → « " +
                a.k.nom(compDe(c, k.r1), "2") + " » ; DRC : " + codes(a.drc, Gravite::Erreur) + " ; critiques : " + std::to_string(valider(c).critiques()));
  }
  {
    Carte k = carteDeuxResistances();
    ConceptionElec& c = k.c;
    etiquette(c, "MILIEU", ptMm(48.26, 49.53), false);
    const Analyse a = analyser(c);
    rapport("CX03c", compter(a.drc, "DRC-21") > 0 || compter(a.drc, "DRC-20") > 0,
            "étiquette « MILIEU » posée sur le réseau R1-R2 déjà routé : DRC " + codes(a.drc, Gravite::Erreur));
  }
  {
    Carte k = carteDeuxResistances();
    ConceptionElec& c = k.c;
    const std::string n = connectivite(c).nom(compDe(c, k.r1), "2");
    ClasseReseau cl;
    cl.nom = "Puissance";
    cl.largeur = nm(0.8);
    c.circuit.regles.classes.push_back(cl);
    c.circuit.regles.classeReseau[n] = "Puissance";
    c.composant(compDe(c, k.r1))->reference = "R9";
    const std::string n2 = connectivite(c).nom(compDe(c, k.r1), "2");
    rapport("CX03d", classeDe(c.circuit.regles, n2) == nullptr, "réseau « " + n + " » rangé dans la classe « Puissance » ; R1 renommé R9 : le réseau devient « " + n2 + " » et sa classe est " +
                                                                    (classeDe(c.circuit.regles, n2) ? "gardée" : "perdue en silence"));
  }
}

// ---------- Empreintes et DRC ----------
static void essaisEmpreintesDRC() {
  {  // CX08a : QFN-16 3x3 : écart entre la pastille centrale et les broches
    const auto d = genererEmpreinte("QFN-16-3x3-0.5");
    double mini = 1e18;
    if (d) {
      const Pastille* ep = nullptr;
      for (const auto& p : d->pastilles)
        if (p.numero == "17") ep = &p;
      for (const auto& p : d->pastilles)
        if (ep && &p != ep) mini = std::min(mini, distancePolygones(formePastille(*ep), formePastille(p)));
    }
    std::string autres;
    for (const char* id : {"QFN-20-4x4-0.5", "QFN-24-4x4-0.5", "QFN-32-5x5-0.5", "QFN-48-7x7-0.5"}) {
      const auto e = genererEmpreinte(id);
      double m = 1e18;
      const std::string nEP = std::to_string(e->pastilles.size());
      const Pastille* ep = nullptr;
      for (const auto& p : e->pastilles)
        if (p.numero == nEP) ep = &p;
      for (const auto& p : e->pastilles)
        if (ep && &p != ep) m = std::min(m, distancePolygones(formePastille(*ep), formePastille(p)));
      autres += std::string(id) + " " + mmS(m) + " ; ";
    }
    rapport("CX08a", mini < 150'000, "QFN-16-3x3-0.5 : pastille centrale à " + mmS(mini) + " des broches (autres QFN : " + autres + ")");
  }
  {  // CX08b : DRC ne compare jamais deux pastilles d'une même empreinte
    ConceptionElec c = nouvelleConception("T08b");
    SpecSymbole sp;
    sp.nom = "CI QFN16";
    sp.empreinte = "QFN-16-3x3-0.5";
    std::string l;
    for (int i = 1; i <= 17; ++i) l += std::to_string(i) + " P" + std::to_string(i) + " passive\n";
    sp.broches = lireListeBroches(l);
    DefSymbole d = genererSymbole(sp);
    c.symboles[d.id] = d;
    const auto u = poserSymbole(c, 0, d.id, ptMm(101.6, 76.2));
    const auto r = poserSymbole(c, 0, "R", ptMm(152.4, 76.2));
    annoter(c, false);
    relier(c, broche(c, u, "1"), broche(c, r, "1"));
    relier(c, broche(c, u, "17"), broche(c, r, "2"));
    c.circuit.contour.largeur = nm(30);
    c.circuit.contour.hauteur = nm(20);
    mettreAJourCircuit(c);
    // Depuis la correction du générateur (EL-05), le QFN-16 n'a plus cet écart : l'essai reprend l'ancienne
    // pastille centrale de 1,85 mm, telle qu'un projet antérieur l'a figée, pour vérifier que la DRC la voit
    for (auto& p : c.empreintes["QFN-16-3x3-0.5"].pastilles)
      if (p.numero == "17") p.largeur = p.hauteur = nm(1.85);
    fp(c, u)->position = ptMm(10, 10);
    fp(c, r)->position = ptMm(22, 10);
    const Analyse a = analyser(c);
    int intra = 0;
    for (const auto& v : a.drc)
      if ((v.code == "DRC-01" || v.code == "DRC-21") && v.message.find("U1.") != std::string::npos && v.message.find("touche pastille U1") != std::string::npos) ++intra;
    int intra2 = 0;
    for (const auto& v : a.drc)
      if (v.code == "DRC-01" && std::count(v.message.begin(), v.message.end(), 'U') >= 2 && v.message.find("U1.") != std::string::npos) ++intra2;
    rapport("CX08b", intra + intra2 == 0, "QFN-16 avec broche 1 et pastille centrale sur deux réseaux différents, écart 0,075 mm < isolement 0,2 mm : " + std::to_string(intra + intra2) + " violation (DRC : " + codes(a.drc, Gravite::Erreur) + ")");
  }
  {  // CX08c : TSSOP au profil « Gravure maison » (isolement 0,4 mm, écart réel 0,25 mm)
    ConceptionElec c = nouvelleConception("T08c");
    c.circuit.regles = profilsFabricants()[2].regles;
    SpecSymbole sp;
    sp.nom = "CI TSSOP";
    sp.empreinte = "TSSOP-20";
    std::string l;
    for (int i = 1; i <= 20; ++i) l += std::to_string(i) + " P" + std::to_string(i) + " passive\n";
    sp.broches = lireListeBroches(l);
    DefSymbole d = genererSymbole(sp);
    c.symboles[d.id] = d;
    const auto u = poserSymbole(c, 0, d.id, ptMm(101.6, 76.2));
    const auto r1 = poserSymbole(c, 0, "R", ptMm(152.4, 76.2)), r2 = poserSymbole(c, 0, "R", ptMm(177.8, 76.2));
    annoter(c, false);
    relier(c, broche(c, u, "1"), broche(c, r1, "1"));
    relier(c, broche(c, u, "2"), broche(c, r2, "1"));
    c.circuit.contour.largeur = nm(40);
    c.circuit.contour.hauteur = nm(30);
    mettreAJourCircuit(c);
    fp(c, u)->position = ptMm(10, 15);
    fp(c, r1)->position = ptMm(25, 10);
    fp(c, r2)->position = ptMm(25, 20);
    const Analyse a = analyser(c);
    int u1 = 0;
    for (const auto& v : a.drc)
      if (v.code == "DRC-01" && v.message.find("U1.1") != std::string::npos && v.message.find("U1.2") != std::string::npos) ++u1;
    rapport("CX08c", u1 == 0, "TSSOP-20 au profil « Gravure maison » (isolement 0,4 mm ; écart réel entre U1.1 et U1.2 : 0,25 mm) : " + std::to_string(u1) + " violation");
  }
  {  // CX09a / CX09b : trou non métallisé traversé par une piste (DRC, puis routeur automatique)
    for (const bool automatique : {false, true}) {
      Carte k = carteDeuxResistances(false, 0, false);
      ConceptionElec& c = k.c;
      const auto h = poserSymbole(c, 0, "Trou_fixation", ptMm(63.5, 76.2));
      annoter(c, false);
      mettreAJourCircuit(c);
      fp(c, k.r2)->position = ptMm(34, 8);
      fp(c, h)->position = ptMm(25, 8);
      const Pt trou = ptMm(25, 8);
      const auto kk = connectivite(c);
      const auto pp = pastilles(c, kk);
      if (automatique) routerSimple(c);
      else {
        // tout est routé proprement, sauf R1-R2 qui passe tout droit par le trou
        routerSimple(c);
        c.circuit.pistes.erase(std::remove_if(c.circuit.pistes.begin(), c.circuit.pistes.end(), [&](const Piste& p) { return p.reseau == kk.nom(compDe(c, k.r1), "2"); }), c.circuit.pistes.end());
        piste(c, kk.nom(compDe(c, k.r1), "2"), Couche::CuivreDessus, pad(pp, compDe(c, k.r1), "2")->pastille.position, pad(pp, compDe(c, k.r2), "1")->pastille.position);
      }
      const Analyse an = analyser(c);
      double dmin = 1e18;
      for (const auto& t : c.circuit.pistes) dmin = std::min(dmin, distancePointSegment(trou, t.a, t.b) - static_cast<double>(t.largeur) / 2);
      const bool coupe = dmin < 1.6e6;
      int erreurs = 0;
      for (const auto& v : an.drc) erreurs += v.gravite == Gravite::Erreur;
      rapport(automatique ? "CX09b" : "CX09a", coupe && erreurs == 0,
              std::string(automatique ? "routeur automatique" : "piste tracée à la main") + ", trou de fixation non métallisé Ø 3,2 mm entre R1 et R2 : le cuivre passe à " + mmS(dmin) +
                  " du centre du trou (rayon 1,600 mm) ; DRC : " + codes(an.drc, Gravite::Erreur) + " / " + codes(an.drc, Gravite::Avertissement) + " ; chevelu : " + std::to_string(an.e.chevelu.size()));
    }
  }
  {  // CX10 : via de couture posée dans un plan de masse
    Carte k = carteDeuxResistances(true, 0, false);
    ConceptionElec& c = k.c;
    c.circuit.zones.push_back(zonePleine(c, "GND", Couche::CuivreDessous));
    routerSimple(c);
    c.circuit.vias.push_back({nouvelUuid(), "", ptMm(20, 20), nm(0.8), nm(0.4)});
    const Analyse a = analyser(c);
    const double d = distanceAuCuivre(cercle(ptMm(20, 20), nm(0.4)), a.z.begin()->second);
    bool alerte = false;
    for (const auto& v : a.drc)
      if (v.message.find("via") != std::string::npos) alerte = true;
    rapport("CX10", d > 0 && !alerte, "via sans réseau (outil Via cliqué dans le vide) au milieu du plan GND rempli (" + std::to_string(static_cast<int>(aireMm2(a.z.begin()->second))) + " mm²) : le plan s'en écarte, cuivre à " +
                                         mmS(d) + " ; via flottante signalée : " + (alerte ? "oui" : "non") + " ; DRC : " + codes(a.drc, Gravite::Erreur) + " / " + codes(a.drc, Gravite::Avertissement));
  }
  {  // CX11 / CX18 : rayons thermiques absents, pastille déclarée reliée quand même
    ConceptionElec c = nouvelleConception("T11");
    const auto j = poserSymbole(c, 0, "Conn_01x02", ptMm(25.4, 50.8));
    const auto r = poserSymbole(c, 0, "R", ptMm(50.8, 50.8));
    annoter(c, false);
    c.composant(compDe(c, r))->empreinte = "R_Axial_P10.16";
    relier(c, broche(c, j, "1"), broche(c, r, "1"));
    const Pt g1 = broche(c, j, "2"), g2 = broche(c, r, "2");
    fil(c, g1, {g1.x + nm(5.08), g1.y});
    poserSymbole(c, 0, "GND", {g1.x + nm(5.08), g1.y});
    fil(c, g2, {g2.x, g2.y + nm(5.08)});
    poserSymbole(c, 0, "GND", {g2.x, g2.y + nm(5.08)});
    c.circuit.contour.largeur = nm(40);
    c.circuit.contour.hauteur = nm(25);
    mettreAJourCircuit(c);
    fp(c, j)->position = ptMm(6, 12.5);
    fp(c, r)->position = ptMm(24, 12.5);
    fp(c, r)->rotation = 0;
    routerSimple(c);
    // La masse passe uniquement par le plan : on retire les pistes et vias GND posées par le routeur
    c.circuit.pistes.erase(std::remove_if(c.circuit.pistes.begin(), c.circuit.pistes.end(), [](const Piste& p) { return p.reseau == "GND"; }), c.circuit.pistes.end());
    c.circuit.vias.erase(std::remove_if(c.circuit.vias.begin(), c.circuit.vias.end(), [](const Via& v) { return v.reseau == "GND"; }), c.circuit.vias.end());
    {
      const auto k0 = connectivite(c);
      std::printf("  (montage) pistes restantes : %zu, réseaux : J1.1=%s J1.2=%s R1.2=%s\n", c.circuit.pistes.size(), k0.nom(compDe(c, j), "1").c_str(), k0.nom(compDe(c, j), "2").c_str(), k0.nom(compDe(c, r), "2").c_str());
    }
    Zone z = zonePleine(c, "GND", Couche::CuivreDessous);
    for (const double ep : {0.25, 0.6}) {
      ConceptionElec cc = c;
      z.epaisseurMin = nm(ep);
      cc.circuit.zones = {z};
      const Analyse a = analyser(cc);
      const PastillePlacee* pj = pad(a.pp, compDe(cc, j), "2");
      const PastillePlacee* prr = pad(a.pp, compDe(cc, r), "2");
      // Distance au plan principal (le plus grand îlot de la zone, trous compris)
      Polygones principal;
      double aireMax = -1;
      for (const auto& il : ilots(a.z.begin()->second)) {
        const double ar = aireMm2(il.exterieur);
        if (ar > aireMax) {
          aireMax = ar;
          principal = {il.exterieur};
          principal.insert(principal.end(), il.trous.begin(), il.trous.end());
        }
      }
      const double dj = distanceAuCuivre(pj->forme, principal), dr = distanceAuCuivre(prr->forme, principal);
      bool gndComplet = true;
      for (const auto& l : a.e.chevelu)
        if (l.reseau == "GND") gndComplet = false;
      {
        const Polygones& f = a.z.begin()->second;
        const Pt cj = pj->pastille.position;
        std::string s;
        for (const Pt dlt : {Pt{nm(0.95), 0}, Pt{-nm(0.95), 0}, Pt{0, nm(0.95)}, Pt{0, -nm(0.95)}, Pt{nm(0.7), nm(0.7)}}) s += dedans(f, cj + dlt) ? "1" : "0";
        for (const auto& poly : f) {
          const Boite bb = boite(poly);
          if (bb.gonflee(nm(2)).contient(cj)) std::printf("      polygone de %zu sommets, aire signée %s, boîte (%.2f;%.2f)-(%.2f;%.2f)\n", poly.size(), aireMm2(Polygones{poly}) > 0 ? "+" : "?", mm(bb.x0), mm(bb.y0), mm(bb.x1), mm(bb.y1));
        }
        int dansPad = 0;
        for (const auto& poly : f) for (const Pt& q : poly) dansPad += dedans(pj->forme, q);
        int padDans = 0;
        for (const Pt& q : pj->forme) padDans += dedans(f, q);
        std::printf("      sommets du cuivre dans la pastille : %d ; sommets de la pastille dans le cuivre : %d ; centre J1.2 (%.2f;%.2f)\n", dansPad, padDans, mm(cj.x), mm(cj.y));
        std::printf("    ep=%.2f : %zu polygones, aire %.2f mm², cuivre juste hors de J1.2 (droite, gauche, bas, haut, diagonale) : %s ; pastille J1.2 forme=%d l=%lld h=%lld\n", ep, f.size(), aireMm2(f), s.c_str(),
                    static_cast<int>(pj->pastille.forme), static_cast<long long>(pj->pastille.largeur), static_cast<long long>(pj->pastille.hauteur));
      }
      rapport(ep < 0.5 ? "CX11-témoin" : "CX18", ep > 0.5 && dj > 0 && dr > 0 && gndComplet,
              "zone GND au dos, épaisseur minimale " + mmS(nm(ep)) + " (rayons de 0,5 mm) : distance pastille → plan J1.2 " + mmS(dj) + ", R1.2 " + mmS(dr) +
                  " ; réseau GND déclaré complet : " + (gndComplet ? "oui" : "non") + " ; DRC erreurs : " + codes(a.drc, Gravite::Erreur));
    }
    // Rayons coupés par quatre vias d'un autre réseau
    {
      ConceptionElec cc = c;
      z.epaisseurMin = nm(0.25);
      cc.circuit.zones = {z};
      const Analyse a0 = analyser(cc);
      const Pt pj = pad(a0.pp, compDe(cc, j), "2")->pastille.position;
      const std::string reseau5 = a0.k.nom(compDe(cc, j), "1");
      for (const Pt dlt : {Pt{nm(1.5), 0}, Pt{-nm(1.5), 0}, Pt{0, nm(1.5)}, Pt{0, -nm(1.5)}}) cc.circuit.vias.push_back({nouvelUuid(), reseau5, pj + dlt, nm(0.8), nm(0.4)});
      const Analyse a = analyser(cc);
      const PastillePlacee* p2 = pad(a.pp, compDe(cc, j), "2");
      // Le cuivre en contact avec la pastille : îlot des rayons ; est-il relié au reste du plan ?
      const auto ils = ilots(a.z.begin()->second);
      int ilotPastille = -1, i = 0;
      for (const auto& il : ils) {
        Polygones pl{il.exterieur};
        if (distanceAuCuivre(p2->forme, pl) <= 0) ilotPastille = i;
        ++i;
      }
      bool gndComplet = true;
      for (const auto& l : a.e.chevelu)
        if (l.reseau == "GND") gndComplet = false;
      std::string aires;
      for (const auto& il : ils) aires += std::to_string(aireMm2(il.exterieur)).substr(0, 6) + " mm² ; ";
      for (const auto& v : a.drc)
        if (v.gravite == Gravite::Erreur) std::printf("    DRC %s : %s\n", v.code.c_str(), v.message.c_str());
      rapport("CX11", ils.size() > 1 && gndComplet,
              "quatre vias +5V coupent les quatre rayons de J1.2 : " + std::to_string(ils.size()) + " îlots de cuivre GND, la pastille ne touche que l'îlot n°" + std::to_string(ilotPastille) +
                  " (îlots : " + aires + ") ; réseau GND déclaré complet : " + (gndComplet ? "oui" : "non") + " ; DRC : " + codes(a.drc, Gravite::Erreur) + " / " + codes(a.drc, Gravite::Avertissement));
    }
  }
  {  // CX12 : passage de 4 à 2 couches
    Carte kk = carteDeuxResistances(true);
    ConceptionElec& c = kk.c;
    c.circuit.empilage.couchesCuivre = 4;
    c.circuit.zones.push_back(zonePleine(c, "GND", Couche::CuivreInt2));
    piste(c, connectivite(c).nom(compDe(c, kk.r1), "2"), Couche::CuivreInt1, ptMm(10, 20), ptMm(30, 20));
    c.circuit.empilage.couchesCuivre = 2;
    const Analyse a = analyser(c);
    const ModeleFabrication m = modeleFabrication(c, a.k, a.z);
    std::size_t inner = 0;
    for (const auto& cf : m.couches)
      if (cf.couche == Couche::CuivreInt1 || cf.couche == Couche::CuivreInt2) inner += cf.primitives.size();
    // Correction de l'essai : le constat d'origine était toujours rendu comme défaut ; le défaut est l'absence
    // d'une alerte DRC « objet sur une couche absente de l'empilage » (DRC-17)
    rapport("CX12", compter(a.drc, "DRC-17") == 0, "4 → 2 couches : 1 piste In1 et 1 zone In2 restent dans le projet (" + std::to_string(c.circuit.pistes.size()) + " pistes au total), " + std::to_string(inner) +
                              " primitive(s) internes en fabrication ; alertes « couche absente » (DRC-17) : " + std::to_string(compter(a.drc, "DRC-17")) + " (" + codes(a.drc, Gravite::Erreur) + ")");
  }
  {  // CX13 : vias superposées
    for (int cas = 0; cas < 2; ++cas) {
      Carte kk = carteDeuxResistances();
      ConceptionElec& c = kk.c;
      const std::string n = connectivite(c).nom(compDe(c, kk.r1), "2");
      c.circuit.vias.push_back({nouvelUuid(), n, ptMm(24, 16), nm(0.8), nm(0.4)});
      c.circuit.vias.push_back({nouvelUuid(), n, ptMm(24, 16), nm(0.8), nm(0.4)});
      if (cas == 1) c.circuit.vias.push_back({nouvelUuid(), n, ptMm(24.05, 16), nm(0.8), nm(0.4)});
      const Analyse a = analyser(c);
      if (cas == 0) rapport("CX13", compter(a.drc, "DRC-14") == 0, "deux vias du même réseau exactement superposées (double perçage) : DRC-14 = " + std::to_string(compter(a.drc, "DRC-14")) + " ; DRC : " + codes(a.drc, Gravite::Erreur));
      else std::printf("  (témoin) avec une troisième via à 0,05 mm : DRC-14 = %d (la paire superposée reste ignorée)\n", compter(a.drc, "DRC-14"));
    }
  }
  {  // CX19 : pastilles de même numéro (languette du SOT-223, bouton 6 mm) : chevelu avant routage
    ConceptionElec c = nouvelleConception("T19");
    const auto u = poserSymbole(c, 0, "Regulateur", ptMm(101.6, 76.2));
    const auto cap = poserSymbole(c, 0, "C", ptMm(127, 88.9));
    const auto sw = poserSymbole(c, 0, "Bouton", ptMm(76.2, 101.6));
    annoter(c, false);
    relier(c, broche(c, u, "2"), broche(c, cap, "1"));
    relier(c, broche(c, sw, "2"), broche(c, cap, "2"));
    c.circuit.contour.largeur = nm(40);
    c.circuit.contour.hauteur = nm(30);
    mettreAJourCircuit(c);
    fp(c, u)->position = ptMm(12, 15);
    fp(c, cap)->position = ptMm(26, 22);
    fp(c, sw)->position = ptMm(28, 8);
    const Analyse a = analyser(c);
    int interne = 0;
    std::string l;
    for (const auto& li : a.e.chevelu) {
      const PastillePlacee *pa = nullptr, *pb = nullptr;
      for (const auto& p : a.pp) {
        if (p.pastille.position == li.a) pa = &p;
        if (p.pastille.position == li.b) pb = &p;
      }
      if (pa && pb && pa->composant == pb->composant) { ++interne; l += pa->reference + "." + pa->numero + "↔" + pb->reference + "." + pb->numero + " "; }
    }
    rapport("CX19", interne > 0, std::to_string(interne) + " liaison(s) du chevelu entre pastilles d'un même composant déjà reliées en interne : " + l + "; elles comptent comme DRC-20, erreur critique non dérogeable");
  }
  {  // CX40 : courts-circuits de la connectivité sans position
    Carte kk = carteDeuxResistances();
    ConceptionElec& c = kk.c;
    piste(c, connectivite(c).nom(compDe(c, kk.r1), "2"), Couche::CuivreDessus, ptMm(6, 11.23), ptMm(15.175, 8));  // pastille J1.1 reliée à R1.1 par une piste d'un autre réseau
    const Analyse a = analyser(c);
    int sansPosition = 0;
    for (const auto& v : a.drc)
      if (v.code == "DRC-21" && v.position.x == 0 && v.position.y == 0) ++sansPosition;
    rapport("CX40", sansPosition > 0, std::to_string(sansPosition) + " court(s)-circuit(s) DRC-21 rendus sans position (0 ; 0) : le clic dans la liste ne mène nulle part");
  }
}

// ---------- Validation et publication ----------
static std::string lire(const std::string& f) {
  std::ifstream i(f, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(i)), {});
}

static void essaisValidation() {
  {  // CX20a : un composant sans empreinte disparaît du circuit, la carte reste publiable
    Carte kk = carteDeuxResistances(true, 2);
    ConceptionElec& c = kk.c;
    const Validation v = valider(c);
    std::string l;
    for (const auto& e : v.etapes)
      for (const auto& d : e.details) l += d.code + "(" + (d.gravite == Gravite::Erreur ? "E" : "A") + ") ";
    rapport("CX20a", v.publiable(), "R3 en parallèle sur R2, sans empreinte : publiable = " + std::string(v.publiable() ? "oui" : "non") + " ; signalé : " + l + "; nomenclature : " +
                                        std::to_string(nomenclature(c).size()) + " ligne(s) (R3 compris), circuit : " + std::to_string(c.circuit.empreintes.size()) + " empreintes");
  }
  {  // CX20b : composant ajouté au schéma sans mise à jour du circuit
    Carte kk = carteDeuxResistances(true);
    ConceptionElec& c = kk.c;
    const auto r3 = poserSymbole(c, 0, "R", ptMm(55.88, 40.64), 1);
    annoter(c, false);
    fil(c, broche(c, r3, "1"), broche(c, kk.r2, "1"));
    fil(c, broche(c, r3, "2"), broche(c, kk.r2, "2"));
    c.composant(compDe(c, r3))->fabricant = "Yageo";
    c.composant(compDe(c, r3))->mpn = "RC0603FR-07R3";
    const Validation v = valider(c);
    std::string l;
    for (const auto& e : v.etapes)
      for (const auto& d : e.details) l += d.code + "(" + (d.gravite == Gravite::Erreur ? "E" : "A") + ") ";
    const std::string dossier = "sorties/pub_cx20b";
    fs::remove_all(dossier);
    fs::create_directories(dossier);
    std::string res;
    try {
      ConceptionElec copie = c;
      std::vector<std::string> fichiers;
      const Publication p = publier(copie, dossier, "audit", "2026-10-10", "", "audit", &fichiers);
      std::string bom, pl;
      for (const auto& f : fichiers) {
        if (f.find("nomenclature.csv") != std::string::npos) bom = lire(dossier + "/" + p.dossier + "/" + f);
        if (f.find("placement.csv") != std::string::npos) pl = lire(dossier + "/" + p.dossier + "/" + f);
      }
      res = "publication " + p.id + " écrite ; R3 dans la nomenclature : " + (bom.find("R3") != std::string::npos ? "oui" : "non") + ", dans le placement : " + (pl.find("R3") != std::string::npos ? "oui" : "non");
    } catch (const std::exception& e) {
      res = std::string("publication refusée : ") + e.what();
    }
    rapport("CX20b", v.publiable(), "R3 ajouté et câblé dans le schéma, circuit non mis à jour : publiable = " + std::string(v.publiable() ? "oui" : "non") + " ; signalé : " + l + " ; " + res);
  }
}

// ---------- Fabrication ----------
static void essaisFabrication() {
  const std::string dossier = "sorties/exemple";
  fs::remove_all(dossier);
  ConceptionElec c = exempleElectronique();
  const auto ecrits = ecrireDossierFabrication(c, dossier, "audit");
  {  // CX14 / CX15 : textes de la carte et repères absents de la sérigraphie
    const Analyse a = analyser(c);
    const ModeleFabrication m = modeleFabrication(c, a.k, a.z);
    std::size_t traits = 0, attendus = 0;
    for (const auto& cf : m.couches)
      if (cf.couche == Couche::SerigraphieDessus) traits = cf.primitives.size();
    for (const auto& e : c.circuit.empreintes)
      if (const auto* d = empreinteEnCache(e.empreinte))
        for (const auto& t : d->traits)
          if (t.couche == Couche::SerigraphieDessus) attendus += t.rayon > 0 ? 36 : 1;
    // y a-t-il une primitive de sérigraphie près du texte « CLIGNOTANT 555 rev A » (25 ; 29,2) ?
    int pres = 0;
    for (const auto& cf : m.couches)
      if (cf.couche == Couche::SerigraphieDessus)
        for (const auto& p : cf.primitives)
          if (std::abs(mm(p.a.x) - 25) < 8 && std::abs(mm(p.a.y) - 29.2) < 1.0) ++pres;
    rapport("CX14", !c.circuit.textes.empty() && pres == 0, "l'exemple contient le texte de carte « " + c.circuit.textes.front().texte + " » ; primitives de sérigraphie à son emplacement : " + std::to_string(pres));
    rapport("CX15", traits == attendus, "sérigraphie du dessus : " + std::to_string(traits) + " traits, exactement ceux des dessins d'empreintes (" + std::to_string(attendus) + ") : aucun repère R1, U1… ni valeur");
  }
  {  // CX16 : attributs X2 des régions et des pastilles
    std::string g;
    for (const auto& f : ecrits)
      if (f.find("F_Cu") != std::string::npos) g = lire(dossier + "/" + f);
    std::size_t regions = 0, pos = 0;
    while ((pos = g.find("G36*", pos)) != std::string::npos) { ++regions; ++pos; }
    const std::size_t premiereRegion = g.find("G36*"), premiereOuverture = g.find("%ADD");
    const bool regionsAvantAttributs = premiereRegion != std::string::npos && premiereOuverture != std::string::npos && g.rfind("%TA.AperFunction", premiereRegion) < premiereOuverture;
    const bool toP = g.find("%TO.P,") != std::string::npos, toC = g.find("%TO.C,") != std::string::npos;
    // Les régions sont écrites après « %TD*% » : aucun attribut d'ouverture ne s'y applique
    const std::size_t td = g.rfind("%TD*%", premiereRegion);
    const std::size_t ta = g.rfind("%TA.", premiereRegion);
    rapport("CX16", regions > 0 && (td != std::string::npos && (ta == std::string::npos || ta < td)) && !toP && !toC,
            std::to_string(regions) + " régions (G36) dans F.Cu (pastilles à coins arrondis et zones) écrites après la remise à zéro des attributs (%TD) : sans .AperFunction ; %TO.P : " +
                (toP ? "présent" : "absent") + ", %TO.C : " + (toC ? "présent" : "absent") + (regionsAvantAttributs ? "" : ""));
  }
  {  // CX17 : CSV de placement avec une valeur longue ou des guillemets
    ConceptionElec cc = c;
    for (auto& x : cc.composants)
      if (x.reference == "R1") x.valeur = std::string(260, 'x');
      else if (x.reference == "R2") x.valeur = "47k \"1%\"";
    const std::string csv = csvPlacement(cc);
    std::istringstream is(csv);
    std::string ligne;
    int lignes = 0, mauvaises = 0;
    while (std::getline(is, ligne)) {
      ++lignes;
      int champs = 1;
      bool q = false;
      for (char ch : ligne) {
        if (ch == '"') q = !q;
        else if (ch == ',' && !q) ++champs;
      }
      if (champs != 7) ++mauvaises;
    }
    rapport("CX17", mauvaises > 0, "placement avec une valeur de 260 caractères et une valeur contenant des guillemets : " + std::to_string(lignes) + " lignes, " + std::to_string(mauvaises) +
                                       " ligne(s) qui n'ont pas 7 champs ; en-tête : « " + csv.substr(0, csv.find('\n')) + " »");
  }
  {  // CX36 : pâte : pastille centrale à 100 %
    ConceptionElec cc = nouvelleConception("pate");
    SpecSymbole sp;
    sp.nom = "QFN";
    sp.empreinte = "QFN-32-5x5-0.5";
    std::string l;
    for (int i = 1; i <= 33; ++i) l += std::to_string(i) + " P" + std::to_string(i) + "\n";
    sp.broches = lireListeBroches(l);
    DefSymbole d = genererSymbole(sp);
    cc.symboles[d.id] = d;
    poserSymbole(cc, 0, d.id, ptMm(100, 100));
    annoter(cc, false);
    mettreAJourCircuit(cc);
    cc.circuit.empreintes[0].position = ptMm(25, 20);
    const Analyse a = analyser(cc);
    const ModeleFabrication m = modeleFabrication(cc, a.k, a.z);
    double airePate = 0, aireCuivre = 0;
    for (const auto& cf : m.couches)
      for (const auto& p : cf.primitives)
        if (p.numero == "33") {
          const double ar = p.type == PrimitiveFab::Type::Region ? aireMm2(p.region) : mm(p.l) * mm(p.h);
          if (cf.couche == Couche::PateDessus) airePate += ar;
          if (cf.couche == Couche::CuivreDessus) aireCuivre += ar;
        }
    rapport("CX36", airePate >= aireCuivre * 0.99, "QFN-32 : ouverture de pâte de la pastille centrale " + std::to_string(airePate) + " mm² pour " + std::to_string(aireCuivre) + " mm² de cuivre (100 %, sans fenêtres)");
  }
}

// ---------- Lien avec le boîtier ----------
static void essaisBoitier() {
  ConceptionElec c = nouvelleConception("Boîtier");
  const auto led = poserSymbole(c, 0, "LED", ptMm(50.8, 50.8));
  const auto j = poserSymbole(c, 0, "Conn_01x04", ptMm(76.2, 50.8));
  const auto sw = poserSymbole(c, 0, "Bouton", ptMm(101.6, 50.8));
  const auto u = poserSymbole(c, 0, "NE555", ptMm(127, 50.8));
  const auto h1 = poserSymbole(c, 0, "Trou_fixation", ptMm(152.4, 50.8));
  annoter(c, false);
  c.composant(compDe(c, led))->empreinte = "LED_0805";
  c.circuit.contour.largeur = nm(60);
  c.circuit.contour.hauteur = nm(40);
  mettreAJourCircuit(c);
  fp(c, led)->position = ptMm(30, 20);
  fp(c, j)->position = ptMm(2.0, 20);  // barrette au ras du bord gauche
  fp(c, sw)->position = ptMm(45, 20);
  fp(c, u)->position = ptMm(15, 20);
  fp(c, u)->rotation = 45;
  fp(c, h1)->position = ptMm(4, 4);
  prom::Projet p = prom::projetVide();
  p.electronique.push_back(c);
  prom::appliquerCarteElectronique(p, 0);
  const prom::Derive d = prom::deriver(p);
  const auto perc = prom::percagesValides(d);
  const auto dec = prom::decoupesValides(d);
  std::size_t ndec = 0;
  for (const auto& [b, v] : dec) ndec += v.size();
  std::string types;
  for (const auto& k : p.composants) types += k.ref + "=" + k.type + "(rot " + std::to_string(k.rot) + ") ";
  {  // CX47 : réglages du boîtier écrasés à chaque recalcul quand la carte pilote le boîtier
    prom::Projet q = p;
    std::string avant;
    if (!q.trous.empty()) { q.trous[0].fixation = "insert"; q.trous[0].vis = "M2.5"; avant = q.trous[0].fixation + "/" + *q.trous[0].vis; }
    if (!q.composants.empty()) q.composants[0].x += 5;
    const double x0 = q.composants.empty() ? 0 : q.composants[0].x;
    prom::appliquerCarteElectronique(q, 0);  // ce que fait Document::recalculer après chaque modification
    const std::string apres = q.trous.empty() ? "?" : q.trous[0].fixation + "/" + (q.trous[0].vis ? *q.trous[0].vis : std::string("vis du projet"));
    rapport("CX47", q.trous.empty() || apres != avant, "trous du boîtier : " + std::to_string(q.trous.size()) + " ; réglage « " + avant + " » choisi pour un trou → après la modification suivante : « " + apres +
                                                          " » ; composant déplacé de 5 mm dans la vue Carte → revenu en x = " + std::to_string(q.composants.empty() ? 0 : q.composants[0].x) + " (avant : " + std::to_string(x0) + ")");
  }
  rapport("CX33", perc.size() < 2 || ndec == 0,
          "carte avec LED 0805, bouton 6 mm, barrette au bord, NE555 à 45° → boîtier : " + std::to_string(perc.size()) + " perçage(s) de couvercle, " + std::to_string(ndec) + " découpe(s) de paroi ; types : " + types);
}

// ---------- Robustesse de la lecture ----------
static void essaisFichier() {
  {
    nlohmann::ordered_json j = versJson(exempleElectronique());
    j["circuit"]["pistes"][0]["a"] = {1e30, -1e30};
    j["circuit"]["pistes"][0]["largeur"] = -5;
    j["circuit"]["zones"][0]["contour"] = nlohmann::ordered_json::array({nlohmann::ordered_json::array({0, 0}), nlohmann::ordered_json::array({1, 1})});
    j["circuit"]["empreintes"][0]["empreinte"] = "INCONNUE_42";
    j["feuilles"][0]["symboles"][0]["rot"] = 7;
    try {
      ConceptionElec c = conceptionDepuisJson(j);
      const Analyse a = analyser(c);
      rapport("CX44", false, "fichier aux valeurs aberrantes (coordonnée 1e30, largeur négative, zone à 2 sommets, empreinte inconnue) : relu ; piste a = (" + std::to_string(c.circuit.pistes[0].a.x) + " ; " +
                                 std::to_string(c.circuit.pistes[0].a.y) + ") nm, largeur " + mmS(c.circuit.pistes[0].largeur) + " ; DRC : " + codes(a.drc, Gravite::Erreur));
    } catch (const std::exception& e) {
      rapport("CX44", true, std::string("exception à la relecture : ") + e.what());
    }
  }
}

static void essaisCatalogue() {
  std::string l;
  for (const char* id : {"LED_D3.0", "LED_D5.0", "R_Axial_P10.16", "D_DO-41"}) l += std::string(id) + (genererEmpreinte(id) ? " connue ; " : " INCONNUE ; ");
  bool dansCatalogue = false;
  for (const auto& i : catalogueEmpreintes()) dansCatalogue |= i.id == "LED_D5.0";
  rapport("CX48", !genererEmpreinte("LED_D5.0"), l + "LED_D5.0 dans le catalogue : " + (dansCatalogue ? "oui" : "non") + " (" + std::to_string(catalogueEmpreintes().size()) + " empreintes au catalogue)");
}

static void essaisProjet() {
  // CX46 : une seule valeur de mauvais type dans une piste fait disparaître toute la carte, sans avertissement
  prom::Projet p = prom::projetVide();
  p.electronique.push_back(exempleElectronique());
  prom::Json j = prom::versJson(p);
  j["electronique"][0]["circuit"]["pistes"][0] = 123;
  std::vector<std::string> av;
  const prom::Projet q = prom::normaliser(j, &av);
  std::string l;
  for (const auto& x : av) l += x + " | ";
  rapport("CX46", q.electronique.empty() && av.empty(), "projet dont une piste (sur 28) vaut 123 au lieu d'un objet : cartes relues = " + std::to_string(q.electronique.size()) + " (sur 1) ; avertissements à la lecture : " +
                                                            (av.empty() ? std::string("aucun") : l) + " ; un enregistrement ensuite efface la carte du fichier");
}

// ---------- Performances ----------
static void essaisPerformances(int nCI) {
  ConceptionElec c = nouvelleConception("Grande carte");
  c.circuit.contour.largeur = nm(220);
  c.circuit.contour.hauteur = nm(160);
  // Un CI SOIC-14 (74HC00) entouré de 4 résistances et 2 condensateurs de découplage ; chaque entrée tirée par une
  // résistance vers une étiquette de bus, les sorties vers une étiquette ; masse et +3V3 par symboles.
  int ix = 0;
  std::vector<std::string> cis;
  for (int n = 0; n < nCI; ++n) {
    const double X = 25.4 + 50.8 * (n % 12), Y = 25.4 + 50.8 * (n / 12);
    const auto u = poserSymbole(c, 0, "74HC00", ptMm(X, Y));
    cis.push_back(u);
    const Pt vcc = broche(c, u, "14"), gnd = broche(c, u, "7");
    fil(c, vcc, {vcc.x, vcc.y - nm(2.54)});
    poserSymbole(c, 0, "+3V3", {vcc.x, vcc.y - nm(2.54)});
    fil(c, gnd, {gnd.x, gnd.y + nm(2.54)});
    poserSymbole(c, 0, "GND", {gnd.x, gnd.y + nm(2.54)});
    for (const char* b : {"1", "2", "4", "5"}) {
      const Pt p = broche(c, u, b);
      const auto r = poserSymbole(c, 0, "R", {p.x - nm(10.16), p.y}, 1);
      const Pt r1 = broche(c, r, "1"), r2 = broche(c, r, "2");
      relier(c, p, r1);
      etiquette(c, "BUS" + std::to_string((ix++) % 64), r2, true);
    }
    for (const char* b : {"3", "6"}) {
      const Pt p = broche(c, u, b);
      fil(c, p, {p.x + nm(5.08), p.y});
      etiquette(c, "BUS" + std::to_string((ix++) % 64), {p.x + nm(5.08), p.y}, true);
    }
    for (int k = 0; k < 2; ++k) {
      const auto cap = poserSymbole(c, 0, "C", ptMm(X + 15.24 + 5.08 * k, Y - 10.16));
      const Pt a = broche(c, cap, "1"), b = broche(c, cap, "2");
      fil(c, a, {a.x, a.y - nm(2.54)});
      poserSymbole(c, 0, "+3V3", {a.x, a.y - nm(2.54)});
      fil(c, b, {b.x, b.y + nm(2.54)});
      poserSymbole(c, 0, "GND", {b.x, b.y + nm(2.54)});
    }
  }
  annoter(c, false);
  mettreAJourCircuit(c);
  // Placement en grille sur la carte
  std::size_t i = 0;
  for (auto& e : c.circuit.empreintes) {
    e.position = ptMm(6 + 7.0 * static_cast<double>(i % 30), 6 + 6.0 * static_cast<double>(i / 30));
    ++i;
  }
  c.circuit.zones.push_back(zonePleine(c, "GND", Couche::CuivreDessous));
  c.circuit.zones.push_back(zonePleine(c, "+3V3", Couche::CuivreDessus));
  int nPads = 0;
  for (const auto& e : c.circuit.empreintes)
    if (const auto* d = empreinteEnCache(e.empreinte)) nPads += static_cast<int>(d->pastilles.size());
  using H = std::chrono::steady_clock;
  auto ms = [](H::time_point a) { return std::chrono::duration<double, std::milli>(H::now() - a).count(); };
  auto t0 = H::now();
  const Connectivite k = connectivite(c);
  const double tK = ms(t0);
  t0 = H::now();
  const auto erc = verifierERC(c, k);
  const double tE = ms(t0);
  t0 = H::now();
  const auto pp = pastilles(c, k);
  const double tP = ms(t0);
  t0 = H::now();
  const Remplissages z = remplirZones(c, k);
  const double tZ = ms(t0);
  t0 = H::now();
  const EtatRoutage e = etatRoutage(c, k, z);
  const double tR = ms(t0);
  t0 = H::now();
  const auto drc = verifierDRC(c, k, z);
  const double tD = ms(t0);
  const double total = tK + tE + tP + tZ + tR + tD;
  t0 = H::now();
  const ModeleFabrication m = modeleFabrication(c, k, z);
  std::size_t taille = 0;
  for (const auto& cf : m.couches) taille += gerber(m, cf.couche, "audit").size();
  const double tF = ms(t0);
  std::size_t gerbCu = gerber(m, Couche::CuivreDessous, "audit").size();
  for (const auto& cf : m.couches) {
    const std::string g = gerber(m, cf.couche, "audit");
    std::size_t reg = 0, p = 0;
    while ((p = g.find("G36", p)) != std::string::npos) { ++reg; ++p; }
    std::printf("    %s : %.2f Mo, %zu régions\n", codeCouche(cf.couche), static_cast<double>(g.size()) / 1e6, reg);
  }
  {
    std::size_t vide = 0;
    for (const auto& zz : c.circuit.zones) if (z.at(zz.uuid).empty()) ++vide;
    std::printf("    zones vides : %zu / %zu\n", vide, c.circuit.zones.size());
  }
  if (std::getenv("ROUTER")) {
    ConceptionElec cr = c;
    auto tr = H::now();
    const BilanRoutage br = routerSimple(cr);
    std::printf("    routeur simple : %.1f s, %d liaisons posées, %d restantes, %d vias\n", ms(tr) / 1000, br.liaisons, br.restantes, br.vias);
    const Connectivite k2 = connectivite(cr);
    const Remplissages z2 = remplirZones(cr, k2);
    const auto d2 = verifierDRC(cr, k2, z2);
    std::printf("    DRC après routage : %s / %s\n", codes(d2, Gravite::Erreur).c_str(), codes(d2, Gravite::Avertissement).c_str());
  }
  char t[600];
  std::snprintf(t, sizeof t,
                "%zu composants, %d pastilles, %zu réseaux : connectivité %.0f ms, ERC %.0f ms, pastilles %.0f ms, zones %.0f ms, chevelu %.0f ms, DRC %.0f ms → %.1f s par modification "
                "(l'atelier recalcule tout après chaque geste) ; Gerber %.0f ms, %.1f Mo (B.Cu %.1f Mo)",
                c.composants.size(), nPads, k.reseaux.size(), tK, tE, tP, tZ, tR, tD, total / 1000, tF, static_cast<double>(taille) / 1e6, static_cast<double>(gerbCu) / 1e6);
  rapport("CX39-" + std::to_string(nCI), total > 500, t);
  if (const char* exp = std::getenv("EXPORT_JSON")) {
    std::ofstream o(exp);
    o << versJson(c).dump();
  }
  t0 = H::now();
  const std::string js = versJson(c).dump();
  const double tJ = ms(t0);
  std::snprintf(t, sizeof t, "projet JSON de %.1f Mo, sérialisé en %.0f ms ; l'historique d'annulation en garde jusqu'à 300 copies complètes (%.0f Mo), et chaque modification sérialise le projet au moins deux fois",
                static_cast<double>(js.size()) / 1e6, tJ, static_cast<double>(js.size()) * 300 / 1e6);
  rapport("CX45-" + std::to_string(nCI), js.size() * 300 > 200'000'000, t);
  (void)erc;
  (void)pp;
  (void)e;
  (void)drc;
}

int main(int argc, char** argv) {
  const std::string quoi = argc > 1 ? argv[1] : "tout";
  if (quoi == "tout" || quoi == "erc") essaisERC();
  if (quoi == "tout" || quoi == "reseaux") essaisNomsDeReseaux();
  if (quoi == "tout" || quoi == "drc") essaisEmpreintesDRC();
  if (quoi == "tout" || quoi == "validation") essaisValidation();
  if (quoi == "tout" || quoi == "fab") essaisFabrication();
  if (quoi == "tout" || quoi == "boitier") essaisBoitier();
  if (quoi == "tout" || quoi == "fichier") essaisFichier();
  if (quoi == "tout" || quoi == "projet") essaisProjet();
  if (quoi == "tout" || quoi == "catalogue") essaisCatalogue();
  if (quoi == "perf") for (int n : {10, 30, 60}) essaisPerformances(n);
  if (quoi == "perf1") essaisPerformances(argc > 2 ? std::atoi(argv[2]) : 10);
  std::printf("\n%d défaut(s) confirmé(s), %d conforme(s)\n", g_defauts, g_conformes);
  return 0;
}

// Prométhée : circuits types de la CAO électrique — schémas courants posés en un geste (démarrage direct, inverseur
// de sens de marche, alimentation 24 V de la commande, éclairage par télérupteur, va-et-vient, prises). Chacun se
// pose à droite de ce qui est déjà dessiné, sur un folio neuf s'il n'y a plus de place, relié au reste du projet par
// ses potentiels ; appareils repérés à la suite avec leur référence du catalogue, fils numérotés, sections réglées
// selon le calibre de leur protection. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "electrique_interne.hpp"
#include "promethee/electrique.hpp"

namespace prom::elec {

namespace {

using ecad::Pt;
Pt P(double x, double y) { return ecad::ptMm(x * 2.54, y * 2.54); }
Pt sous(Pt a, double y) { return {a.x, P(0, y).y}; }  // même abscisse, à la hauteur y (en pas)

// Pose les éléments d'un circuit type et retient ce qui a été posé (pour régler les conducteurs ensuite)
class Constructeur {
 public:
  explicit Constructeur(ProjetElectrique& p) : m_p(p) {}

  // Folio du type voulu où le circuit tient en largeur (à droite de ce qui y est dessiné), créé au besoin ; x0 :
  // abscisse de départ (en pas de 2,54 mm)
  std::size_t folio(TypeFolio type, const std::string& titre, double largeur, double& x0) {
    std::size_t dernier = static_cast<std::size_t>(-1);
    for (std::size_t f = 0; f < m_p.schema.feuilles.size(); ++f)
      if (m_p.folio(m_p.schema.feuilles[f].uuid).type == type) dernier = f;
    if (dernier != static_cast<std::size_t>(-1)) {
      const double libre = abscisseLibre(dernier);
      if (libre + largeur <= limite(dernier)) {
        x0 = libre;
        return dernier;
      }
    }
    // Pas de folio de ce type, ou plus de place : un folio neuf, juste après le dernier de ce type
    const std::size_t position = dernier == static_cast<std::size_t>(-1) ? m_p.schema.feuilles.size() : dernier + 1;
    const std::size_t f = ajouterFolio(m_p, type, titre, position);
    x0 = 10;
    return f;
  }

  std::string appareil(std::size_t f, const std::string& symbole, double x, double y, const std::string& fonction, int rotation = 0, std::string catalogue = "*") {
    if (catalogue == "*") {
      const auto entrees = entreesPourSymbole(symbole);
      catalogue = entrees.empty() ? std::string() : entrees.front()->id;
    }
    const std::string pose = poserAppareil(m_p, f, symbole, P(x, y), rotation, catalogue);
    if (ecad::ComposantElec* k = composant(pose)) {
      if (!fonction.empty()) k->champs["fonction"] = fonction;
      m_composants.insert(k->uuid);
    }
    return pose;
  }

  std::string element(std::size_t f, const std::string& symbole, double x, double y, const std::string& porteur, int rotation = 0) {
    return poserElement(m_p, f, symbole, P(x, y), rotation, porteur);
  }

  std::string borne(std::size_t f, double x, double y, const std::string& bornier, bool protection = false) {
    const std::string pose = poserBorne(m_p, f, P(x, y), 0, bornier, protection);
    if (ecad::ComposantElec* k = composant(pose)) m_composants.insert(k->uuid);
    return pose;
  }

  ecad::ComposantElec* composant(const std::string& pose) {
    for (auto& fe : m_p.schema.feuilles)
      for (const auto& s : fe.symboles)
        if (s.uuid == pose) return m_p.schema.composant(s.composant);
    return nullptr;
  }
  std::string idComposant(const std::string& pose) {
    const ecad::ComposantElec* k = composant(pose);
    return k ? k->uuid : std::string();
  }

  // Borne d'un symbole posé, par son numéro dans la définition (avant renumérotation : 3-4 pour un contact),
  // ou par son rang quand les deux bornes portent le même numéro (borne de bornier : 0 en haut, 1 en bas)
  Pt b(const std::string& pose, const std::string& numero, std::size_t rang = 0) {
    for (const auto& fe : m_p.schema.feuilles)
      for (const auto& s : fe.symboles) {
        if (s.uuid != pose) continue;
        const ecad::DefSymbole* d = m_p.schema.symbole(s.symbole);
        if (!d) return {};
        std::size_t n = 0;
        for (const auto& bp : ecad::brochesPlacees(s, *d))
          if (bp.broche->numero == numero && n++ == rang) return bp.p;
      }
    return {};
  }

  // Fil d'un point à l'autre, en équerre (vertical d'abord, ou horizontal d'abord)
  void fil(std::size_t f, Pt a, Pt c, bool verticalDabord = true) {
    if (a == c) return;
    if (a.x == c.x || a.y == c.y) {
      segment(f, a, c);
      return;
    }
    const Pt coin = verticalDabord ? Pt{a.x, c.y} : Pt{c.x, a.y};
    segment(f, a, coin);
    segment(f, coin, c);
  }
  void segment(std::size_t f, Pt a, Pt c) {
    if (a == c) return;
    ecad::Fil w;
    w.uuid = ecad::nouvelUuid();
    w.a = a;
    w.b = c;
    m_p.schema.feuilles[f].fils.push_back(w);
  }
  // Potentiel au bout d'un fil vertical : vers le haut (rotation 1) ou vers le bas (3)
  void potentiel(std::size_t f, const std::string& nom, Pt p, int rotation) { poserPotentiel(m_p, f, nom, p, rotation); }

  // Après la pose : fils numérotés (sans toucher aux numéros déjà donnés), puis section et type des conducteurs du
  // circuit selon le calibre de leur protection
  void finir(double sectionPuissance, const std::string& typePuissance) {
    numeroterFils(m_p, false);
    if (sectionPuissance <= 0) return;
    const ecad::Connectivite k = ecad::connectivite(m_p.schema);
    for (const auto& r : k.reseaux) {
      if (r.nomme || detail::nomAutomatique(r.nom)) continue;
      bool aNous = false;
      for (const auto& n : r.broches) aNous = aNous || m_composants.count(n.composant);
      if (!aNous) continue;
      const ClasseConducteur c = classeConducteur(r.nom);
      if (c != ClasseConducteur::Autre) continue;
      ProprietesConducteur& pr = m_p.conducteurs[r.nom];
      if (pr.section <= 0) pr.section = sectionPuissance;
      if (pr.type.empty()) pr.type = typePuissance;
    }
  }

 private:
  // Abscisse libre d'un folio (en pas) : à droite de tout ce qui y est dessiné, avec une marge
  double abscisseLibre(std::size_t f) const {
    const auto& fe = m_p.schema.feuilles[f];
    ecad::Nm x = 0;
    bool vide = true;
    for (const auto& s : fe.symboles)
      if (const ecad::DefSymbole* d = detail::definitionDe(m_p, s.symbole)) {
        x = std::max(x, ecad::boiteSymbole(s, *d).x1);
        vide = false;
      }
    for (const auto& w : fe.fils) {
      x = std::max({x, w.a.x, w.b.x});
      vide = false;
    }
    for (const auto& e : fe.etiquettes) {
      x = std::max(x, e.p.x + ((e.rotation & 1) ? 0 : static_cast<ecad::Nm>(e.texte.size() + 2) * 1'500'000));
      vide = false;
    }
    if (vide) return 10;
    return std::ceil(ecad::mm(x) / 2.54) + 8;
  }
  double limite(std::size_t f) const {
    const CadreFolio c = cadreFolio(m_p.schema.feuilles[f].format, m_p.reglages.colonnes, m_p.reglages.lignes);
    return std::floor(ecad::mm(c.dessin.x1) / 2.54) - 4;
  }

  ProjetElectrique& m_p;
  std::set<std::string> m_composants;
};

// ----- Démarrage direct : disjoncteur moteur, contacteur, bornier, moteur ; marche, arrêt, auto-maintien, voyant -----
int demarrageDirect(ProjetElectrique& p) {
  Constructeur c(p);
  double x = 0, xc = 0;
  const std::size_t fp = c.folio(TypeFolio::Puissance, "Puissance", 16, x);
  const std::size_t fc = c.folio(TypeFolio::Commande, "Commande", 18, xc);
  // Commande d'abord : la bobine porte le contacteur dont les pôles vont sur la puissance
  const std::string s0 = c.appareil(fc, "iec:bp-nf", xc, 18, "Arrêt du moteur");
  const std::string s1 = c.appareil(fc, "iec:bp-no", xc, 28, "Marche du moteur");
  const std::string km = c.appareil(fc, "iec:bobine-km", xc, 46, "Contacteur du moteur", 0, p.reglages.tensionCommande > 100 ? "km-9-230ac" : "km-9-24dc");
  const std::string idKm = c.idComposant(km);
  const std::string am = c.element(fc, "iec:contact-no", xc + 6, 28, idKm);   // auto-maintien
  const std::string sig = c.element(fc, "iec:contact-no", xc + 12, 28, idKm);  // signalisation
  const std::string h = c.appareil(fc, "iec:voyant", xc + 12, 46, "Moteur en marche", 0, p.reglages.tensionCommande > 100 ? "" : "v-vert");
  const bool continu = p.reglages.commandeContinue && p.reglages.tensionCommande < 100;
  const std::string plus = continu ? "24V" : "L1", moins = continu ? "0V" : "N";
  c.potentiel(fc, plus, P(xc, 9), 1);
  c.fil(fc, P(xc, 9), c.b(s0, "11"));
  c.fil(fc, c.b(s0, "12"), c.b(s1, "13"));
  c.fil(fc, c.b(am, "3"), P(xc + 6, 23));
  c.fil(fc, P(xc + 6, 23), P(xc, 23));  // en T sur la liaison arrêt → marche
  c.fil(fc, c.b(s1, "14"), c.b(km, "A1"));
  c.fil(fc, c.b(am, "4"), P(xc + 6, 34));
  c.fil(fc, P(xc + 6, 34), P(xc, 34));  // en T sur la liaison marche → bobine
  c.fil(fc, P(xc + 12, 11), P(xc, 11));  // en T sous le potentiel
  c.fil(fc, P(xc + 12, 11), c.b(sig, "3"));
  c.fil(fc, c.b(sig, "4"), c.b(h, "X1"));
  c.fil(fc, c.b(km, "A2"), P(xc, 58));
  c.potentiel(fc, moins, P(xc, 58), 3);
  c.fil(fc, c.b(h, "X2"), P(xc + 12, 54));
  c.fil(fc, P(xc + 12, 54), P(xc, 54));  // en T sur le retour
  // Puissance : L1 L2 L3 → disjoncteur moteur → pôles → bornier → moteur ; PE
  const std::string q = c.appareil(fp, "iec:disj-moteur-3p", x + 2, 22, "Protection du moteur", 0, "dm-6,3");
  const std::string poles = c.element(fp, "iec:poles-km-3p", x + 2, 36, idKm);
  const std::string phases[] = {"L1", "L2", "L3"};
  const std::string entrees[] = {"1", "3", "5"}, sorties[] = {"2", "4", "6"};
  for (int i = 0; i < 3; ++i) {
    const double xi = x + 2.0 * i;
    c.potentiel(fp, phases[i], P(xi, 9 + 2.0 * i), 1);
    c.fil(fp, P(xi, 9 + 2.0 * i), c.b(q, entrees[i]));
    c.fil(fp, c.b(q, sorties[i]), c.b(poles, entrees[i]));
  }
  const std::string x1 = c.borne(fp, x, 48, {});
  const std::string idX = c.idComposant(x1);
  const std::string bornes[] = {x1, c.borne(fp, x + 2, 48, idX), c.borne(fp, x + 4, 48, idX)};
  const std::string bpe = c.borne(fp, x + 10, 48, idX, true);
  const std::string m = c.appareil(fp, "iec:moteur-3", x + 2, 60, "Moteur", 0, "m3-2,2");
  const std::string enroulements[] = {"U1", "V1", "W1"};
  for (int i = 0; i < 3; ++i) {
    c.fil(fp, c.b(poles, sorties[i]), c.b(bornes[i], "1", 0));
    c.fil(fp, c.b(bornes[i], "1", 1), c.b(m, enroulements[i]));
  }
  c.potentiel(fp, "PE", P(x + 10, 15), 1);
  c.fil(fp, P(x + 10, 15), c.b(bpe, "1", 0));
  c.fil(fp, c.b(m, "PE"), c.b(bpe, "1", 1), false);
  c.finir(0, {});
  return static_cast<int>(fc);
}

// ----- Inverseur de sens de marche : deux contacteurs, croisement de deux phases, verrouillage électrique -----
int inverseurSens(ProjetElectrique& p) {
  Constructeur c(p);
  double x = 0, xc = 0;
  const std::size_t fp = c.folio(TypeFolio::Puissance, "Puissance", 22, x);
  const std::size_t fc = c.folio(TypeFolio::Commande, "Commande", 26, xc);
  const bool continu = p.reglages.commandeContinue && p.reglages.tensionCommande < 100;
  const std::string plus = continu ? "24V" : "L1", moins = continu ? "0V" : "N";
  const std::string cat = p.reglages.tensionCommande > 100 ? "km-9-230ac" : "km-9-24dc";
  // Commande : arrêt commun, puis deux colonnes (avant, arrière), chacune verrouillée par l'autre contacteur
  const std::string s0 = c.appareil(fc, "iec:bp-nf", xc, 16, "Arrêt du moteur");
  const std::string s1 = c.appareil(fc, "iec:bp-no", xc, 27, "Marche avant");
  const std::string s2 = c.appareil(fc, "iec:bp-no", xc + 12, 27, "Marche arrière");
  const std::string k1 = c.appareil(fc, "iec:bobine-km", xc, 50, "Marche avant", 0, cat);
  const std::string k2 = c.appareil(fc, "iec:bobine-km", xc + 12, 50, "Marche arrière", 0, cat);
  const std::string id1 = c.idComposant(k1), id2 = c.idComposant(k2);
  const std::string am1 = c.element(fc, "iec:contact-no", xc + 5, 27, id1);
  const std::string am2 = c.element(fc, "iec:contact-no", xc + 17, 27, id2);
  const std::string v2 = c.element(fc, "iec:contact-nf", xc, 38, id2);       // KM2 ouvert pour la marche avant
  const std::string v1 = c.element(fc, "iec:contact-nf", xc + 12, 38, id1);  // KM1 ouvert pour la marche arrière
  c.potentiel(fc, plus, P(xc, 8), 1);
  c.fil(fc, P(xc, 8), c.b(s0, "11"));
  c.fil(fc, c.b(s0, "12"), c.b(s1, "13"));
  c.fil(fc, P(xc, 21), P(xc + 12, 21));  // en T : l'arrêt commande les deux colonnes
  c.fil(fc, P(xc + 12, 21), c.b(s2, "13"));
  for (const auto& [s, am, v, k, xk] :
       {std::tuple{s1, am1, v2, k1, xc}, std::tuple{s2, am2, v1, k2, xc + 12}}) {
    c.fil(fc, c.b(am, "3"), P(xk + 5, 22.5));
    c.fil(fc, P(xk + 5, 22.5), P(xk, 22.5));  // auto-maintien en parallèle sur le bouton
    c.fil(fc, c.b(s, "14"), c.b(v, "1"));
    c.fil(fc, c.b(am, "4"), P(xk + 5, 32));
    c.fil(fc, P(xk + 5, 32), P(xk, 32));
    c.fil(fc, c.b(v, "2"), c.b(k, "A1"));
  }
  c.fil(fc, c.b(k1, "A2"), P(xc, 60));
  c.potentiel(fc, moins, P(xc, 60), 3);
  c.fil(fc, c.b(k2, "A2"), P(xc + 12, 57));
  c.fil(fc, P(xc + 12, 57), P(xc, 57));
  // Puissance : disjoncteur moteur, KM1 droit, KM2 croise L1 et L3 vers le moteur
  const std::string q = c.appareil(fp, "iec:disj-moteur-3p", x + 2, 20, "Protection du moteur", 0, "dm-6,3");
  const std::string p1 = c.element(fp, "iec:poles-km-3p", x + 2, 38, id1);
  const std::string p2 = c.element(fp, "iec:poles-km-3p", x + 10, 38, id2);
  const std::string phases[] = {"L1", "L2", "L3"};
  const std::string entrees[] = {"1", "3", "5"}, sorties[] = {"2", "4", "6"};
  for (int i = 0; i < 3; ++i) {
    const double xi = x + 2.0 * i;
    c.potentiel(fp, phases[i], P(xi, 8 + 2.0 * i), 1);
    c.fil(fp, P(xi, 8 + 2.0 * i), c.b(q, entrees[i]));
    c.fil(fp, c.b(q, sorties[i]), c.b(p1, entrees[i]));
    // Dérivation vers KM2, chaque phase à sa hauteur (les croisements sans point ne se relient pas)
    const double y = 26 + 2.0 * i;
    c.fil(fp, P(xi, y), P(x + 8 + 2.0 * i, y));
    c.fil(fp, P(x + 8 + 2.0 * i, y), c.b(p2, entrees[i]));
  }
  const std::string x1 = c.borne(fp, x, 54, {});
  const std::string idX = c.idComposant(x1);
  const std::string bornes[] = {x1, c.borne(fp, x + 2, 54, idX), c.borne(fp, x + 4, 54, idX)};
  const std::string bpe = c.borne(fp, x + 16, 54, idX, true);
  for (int i = 0; i < 3; ++i) c.fil(fp, c.b(p1, sorties[i]), c.b(bornes[i], "1", 0));
  // KM2 : 2 → W, 4 → V, 6 → U (en T sur les liaisons de KM1)
  const double xs2[] = {x + 8, x + 10, x + 12};
  const double vers[] = {x + 4, x + 2, x};
  const double hauteurs[] = {48, 46, 44};
  for (int i = 0; i < 3; ++i) {
    c.fil(fp, c.b(p2, sorties[i]), P(xs2[i], hauteurs[i]));
    c.fil(fp, P(xs2[i], hauteurs[i]), P(vers[i], hauteurs[i]));
  }
  const std::string m = c.appareil(fp, "iec:moteur-3", x + 2, 64, "Moteur à deux sens de marche", 0, "m3-2,2");
  const std::string enroulements[] = {"U1", "V1", "W1"};
  for (int i = 0; i < 3; ++i) c.fil(fp, c.b(bornes[i], "1", 1), c.b(m, enroulements[i]));
  c.potentiel(fp, "PE", P(x + 16, 14), 1);
  c.fil(fp, P(x + 16, 14), c.b(bpe, "1", 0));
  c.fil(fp, c.b(m, "PE"), c.b(bpe, "1", 1), false);
  c.finir(0, {});
  return static_cast<int>(fc);
}

// ----- Alimentation 24 V continu de la commande : disjoncteur 1P+N, alimentation à découpage -----
int alimentationCommande(ProjetElectrique& p) {
  Constructeur c(p);
  double x = 0;
  const std::size_t fp = c.folio(TypeFolio::Puissance, "Puissance", 12, x);
  const std::string q = c.appareil(fp, "iec:disj-1pn", x + 1, 20, "Protection de l'alimentation 24 V", 0, "dj-1pn-c2");
  const std::string g = c.appareil(fp, "iec:alim-dc", x + 1, 36, "Alimentation de la commande", 0, "g-24-2,5");
  c.potentiel(fp, "L1", P(x, 9), 1);
  c.fil(fp, P(x, 9), c.b(q, "1"));
  c.potentiel(fp, "N", P(x + 2, 11), 1);
  c.fil(fp, P(x + 2, 11), c.b(q, "N1"));
  c.fil(fp, c.b(q, "2"), c.b(g, "L"));
  c.fil(fp, c.b(q, "N2"), c.b(g, "N"));
  c.potentiel(fp, "PE", P(x + 5, 13), 1);
  c.fil(fp, P(x + 5, 13), c.b(g, "PE"));
  c.fil(fp, c.b(g, "+"), P(x, 48));
  c.potentiel(fp, "24V", P(x, 48), 3);
  c.fil(fp, c.b(g, "-"), P(x + 2, 52));
  c.potentiel(fp, "0V", P(x + 2, 52), 3);
  c.finir(0, {});
  return static_cast<int>(fp);
}

// Départ 1P+N d'un circuit du bâtiment : disjoncteur sous L1 et N, phase et neutre rendus en bas
struct Depart {
  std::string q;
  Pt phase, neutre;
};
Depart depart(Constructeur& c, std::size_t f, double x, const std::string& calibre, const std::string& fonction) {
  Depart d;
  d.q = c.appareil(f, "iec:disj-1pn", x + 1, 18, fonction, 0, "dj-1pn-c" + calibre);
  c.potentiel(f, "L1", P(x, 8), 1);
  c.fil(f, P(x, 8), c.b(d.q, "1"));
  c.potentiel(f, "N", P(x + 2, 10), 1);
  c.fil(f, P(x + 2, 10), c.b(d.q, "N1"));
  d.phase = c.b(d.q, "2");
  d.neutre = c.b(d.q, "N2");
  return d;
}

// ----- Éclairage commandé par télérupteur depuis deux boutons poussoirs -----
int telerupteur(ProjetElectrique& p) {
  Constructeur c(p);
  double x = 0;
  const std::size_t f = c.folio(TypeFolio::Distribution, "Circuits du bâtiment", 20, x);
  const Depart d = depart(c, f, x, "10", "Éclairage par télérupteur");
  const std::string s1 = c.appareil(f, "iec:poussoir", x + 6, 32, "Poussoir entrée");
  const std::string s2 = c.appareil(f, "iec:poussoir", x + 9, 32, "Poussoir couloir");
  const std::string kt = c.appareil(f, "iec:telerupteur", x + 8, 44, "Télérupteur de l'éclairage");
  const std::string e = c.appareil(f, "iec:lampe", x + 15, 54, "Point lumineux");
  // Phase : vers les poussoirs et le contact du télérupteur
  c.fil(f, d.phase, sous(d.phase, 25));
  c.fil(f, sous(d.phase, 25), P(x + 12, 25));
  c.fil(f, c.b(s1, "1"), P(x + 6, 25));
  c.fil(f, c.b(s2, "1"), P(x + 9, 25));
  c.fil(f, P(x + 12, 25), P(x + 12, 39));
  c.fil(f, P(x + 12, 39), c.b(kt, "1"), false);
  // Poussoirs en parallèle sur la bobine
  c.fil(f, c.b(s1, "2"), c.b(kt, "A1"));
  c.fil(f, c.b(s2, "2"), P(x + 9, 37));
  c.fil(f, P(x + 9, 37), P(x + 6, 37));
  // Contact → lampe ; neutre commun en bas
  c.fil(f, c.b(kt, "2"), sous(c.b(kt, "2"), 50));
  c.fil(f, sous(c.b(kt, "2"), 50), c.b(e, "1"), false);
  c.fil(f, c.b(e, "2"), P(x + 15, 62));
  c.fil(f, d.neutre, sous(d.neutre, 62));
  c.fil(f, sous(d.neutre, 62), P(x + 15, 62));
  c.fil(f, c.b(kt, "A2"), sous(c.b(kt, "A2"), 62));
  c.finir(1.5, "H07V-U");
  return static_cast<int>(f);
}

// ----- Éclairage en va-et-vient : deux interrupteurs reliés par leurs navettes -----
int vaEtVient(ProjetElectrique& p) {
  Constructeur c(p);
  double x = 0;
  const std::size_t f = c.folio(TypeFolio::Distribution, "Circuits du bâtiment", 14, x);
  const Depart d = depart(c, f, x, "10", "Éclairage en va-et-vient");
  const std::string sa = c.appareil(f, "iec:va-et-vient", x + 7, 32, "Va-et-vient entrée", 2);
  const std::string sb = c.appareil(f, "iec:va-et-vient", x + 7, 44, "Va-et-vient chambre");
  const std::string e = c.appareil(f, "iec:lampe", x + 7, 56, "Point lumineux");
  c.fil(f, d.phase, sous(d.phase, 26));
  c.fil(f, sous(d.phase, 26), P(x + 7, 26));
  c.fil(f, P(x + 7, 26), c.b(sa, "L"));
  // Navettes : 1 → 2 et 2 → 1 (le premier interrupteur est retourné)
  c.fil(f, c.b(sa, "2"), c.b(sb, "1"));
  c.fil(f, c.b(sa, "1"), c.b(sb, "2"));
  c.fil(f, c.b(sb, "L"), c.b(e, "1"));
  c.fil(f, c.b(e, "2"), P(x + 7, 63));
  c.fil(f, d.neutre, sous(d.neutre, 63));
  c.fil(f, sous(d.neutre, 63), P(x + 7, 63));
  c.finir(1.5, "H07V-U");
  return static_cast<int>(f);
}

// ----- Circuit de prises : disjoncteur 20 A, trois socles 2P+T en 2,5 mm² -----
int prises(ProjetElectrique& p) {
  Constructeur c(p);
  double x = 0;
  const std::size_t f = c.folio(TypeFolio::Distribution, "Circuits du bâtiment", 30, x);
  const Depart d = depart(c, f, x, "20", "Prises de courant");
  const double xp[] = {x + 8, x + 15, x + 22};
  std::vector<std::string> socles;
  for (int i = 0; i < 3; ++i) socles.push_back(c.appareil(f, "iec:prise", xp[i], 50, "Prise " + std::to_string(i + 1)));
  const double yL = 38, yN = 40, yPE = 42;
  c.fil(f, d.phase, sous(d.phase, yL));
  c.fil(f, sous(d.phase, yL), P(xp[2] - 1, yL));
  c.fil(f, d.neutre, sous(d.neutre, yN));
  c.fil(f, sous(d.neutre, yN), P(xp[2] + 1, yN));
  c.potentiel(f, "PE", P(x + 5, 12), 1);
  c.fil(f, P(x + 5, 12), P(x + 5, yPE));
  c.fil(f, P(x + 5, yPE), P(xp[2] + 3, yPE));
  for (int i = 0; i < 3; ++i) {
    c.fil(f, c.b(socles[static_cast<std::size_t>(i)], "L"), P(xp[i] - 1, yL));
    c.fil(f, c.b(socles[static_cast<std::size_t>(i)], "N"), P(xp[i] + 1, yN));
    c.fil(f, c.b(socles[static_cast<std::size_t>(i)], "PE"), P(xp[i] + 3, yPE));
  }
  c.finir(2.5, "H07V-U");
  return static_cast<int>(f);
}

}  // namespace

const std::vector<InfosCircuitType>& circuitsTypes() {
  static const std::vector<InfosCircuitType> v = {
      {CircuitType::DemarrageDirect, "Démarrage direct d'un moteur", "Disjoncteur moteur, contacteur, bornier, moteur ; arrêt, marche, auto-maintien, voyant de marche", false},
      {CircuitType::InverseurSens, "Inverseur de sens de marche", "Deux contacteurs croisant deux phases, verrouillés l'un par l'autre ; arrêt, marche avant, marche arrière", false},
      {CircuitType::AlimentationCommande, "Alimentation 24 V de la commande", "Disjoncteur 1P+N et alimentation à découpage : potentiels 24V et 0V pour la commande", false},
      {CircuitType::Telerupteur, "Éclairage par télérupteur", "Disjoncteur 10 A, deux boutons poussoirs, télérupteur, point lumineux", true},
      {CircuitType::VaEtVient, "Éclairage en va-et-vient", "Disjoncteur 10 A, deux interrupteurs va-et-vient reliés par leurs navettes, point lumineux", true},
      {CircuitType::Prises, "Circuit de prises 16 A", "Disjoncteur 20 A, trois socles 2P+T, conducteurs de 2,5 mm²", true},
  };
  return v;
}

int insererCircuitType(ProjetElectrique& p, CircuitType t) {
  switch (t) {
    case CircuitType::DemarrageDirect: return demarrageDirect(p);
    case CircuitType::InverseurSens: return inverseurSens(p);
    case CircuitType::AlimentationCommande: return alimentationCommande(p);
    case CircuitType::Telerupteur: return telerupteur(p);
    case CircuitType::VaEtVient: return vaEtVient(p);
    case CircuitType::Prises: return prises(p);
  }
  return -1;
}

}  // namespace prom::elec

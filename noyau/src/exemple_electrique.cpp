// Prométhée : exemple complet de CAO électrique — démarrage direct d'un moteur triphasé. Folio de puissance
// (sectionneur porte-fusibles, contacteur, relais thermique, bornier, moteur, alimentation 24 V), folio de
// commande (marche, arrêt, auto-maintien, voyants de marche et de défaut), fils numérotés, câble du moteur,
// armoire implantée. Licence GPL-3.0-only.
#include <string>
#include <vector>

#include "electrique_interne.hpp"
#include "promethee/electrique.hpp"

namespace prom::elec {

ProjetElectrique exempleDemarrageMoteur() {
  ProjetElectrique p = nouveauProjetElectrique("Démarrage direct d'un moteur", true);
  p.reglages.installation = "Pompe de relevage";
  p.reglages.client = "Exemple Prométhée";
  p.reglages.tensionCommande = 24;
  p.reglages.commandeContinue = true;
  // Grille en pas de 2,54 mm
  const auto P = [](double x, double y) { return ecad::ptMm(x * 2.54, y * 2.54); };
  auto fil = [&](std::size_t f, double x0, double y0, double x1, double y1) {
    ecad::Fil w;
    w.uuid = ecad::nouvelUuid();
    w.a = P(x0, y0);
    w.b = P(x1, y1);
    if (w.a != w.b) p.schema.feuilles[f].fils.push_back(w);
  };
  auto potentiel = [&](std::size_t f, const std::string& nom, double x, double y, int rotation) {
    ecad::Etiquette e;
    e.uuid = ecad::nouvelUuid();
    e.texte = nom;
    e.p = P(x, y);
    e.rotation = rotation;
    e.type = ecad::TypeEtiquette::Globale;
    p.schema.feuilles[f].etiquettes.push_back(e);
  };
  auto texte = [&](std::size_t f, double x, double y, const std::string& t, double taille) {
    ecad::TexteSchema s;
    s.uuid = ecad::nouvelUuid();
    s.texte = t;
    s.p = P(x, y);
    s.taille = ecad::nm(taille);
    p.schema.feuilles[f].textes.push_back(s);
  };
  auto composantDe = [&](std::size_t f, const std::string& symbole) -> ecad::ComposantElec* {
    for (const auto& s : p.schema.feuilles[f].symboles)
      if (s.uuid == symbole) return p.schema.composant(s.composant);
    return nullptr;
  };
  auto fonction = [&](std::size_t f, const std::string& symbole, const std::string& texteFonction) {
    if (ecad::ComposantElec* k = composantDe(f, symbole)) k->champs["fonction"] = texteFonction;
    return composantDe(f, symbole);
  };

  // ----- Folio 1 : page de garde (titre, renseignements et sommaire dessinés d'office) -----
  p.reglages.societe = "Prométhée";
  p.reglages.auteur = "Exemple livré";
  texte(0, 14, 92, "Moteur 2,2 kW 400 V : marche, arrêt, auto-maintien, protection thermique, voyants de marche et de défaut.", 3);

  // ----- Folio 2 : puissance -----
  const std::size_t F = 1;
  for (const auto& [nom, y] : std::vector<std::pair<std::string, double>>{{"L1", 12}, {"L2", 14}, {"L3", 16}, {"N", 18}, {"PE", 20}}) {
    fil(F, 8, y, 150, y);
    potentiel(F, nom, 8, y, 2);
    potentiel(F, nom, 150, y, 0);
  }
  // Sectionneur porte-fusibles, contacteur, relais thermique
  const std::string q1 = poserAppareil(p, F, "iec:sect-fusible-3p", P(24, 34), 0, "sf-3p-32");
  if (ecad::ComposantElec* k = fonction(F, q1, "Sectionnement et protection de la pompe")) k->champs["calibre"] = "10";  // fusibles aM 10 A
  fil(F, 22, 30, 22, 12);
  fil(F, 24, 30, 24, 14);
  fil(F, 26, 30, 26, 16);
  const std::string bobine = poserAppareil(p, 2, "iec:bobine-km", P(20, 82), 0, "km-9-24dc");
  ecad::ComposantElec* km = fonction(2, bobine, "Contacteur de la pompe");
  const std::string km1 = km ? km->uuid : std::string();
  poserElement(p, F, "iec:poles-km-3p", P(24, 46), 0, km1);
  for (double x : {22.0, 24.0, 26.0}) fil(F, x, 38, x, 43);
  const std::string f1 = poserAppareil(p, F, "iec:relais-thermique-3p", P(24, 58), 0, "rt-6");
  ecad::ComposantElec* frt = fonction(F, f1, "Protection thermique de la pompe");
  const std::string idF1 = frt ? frt->uuid : std::string();  // UUID : les pointeurs bougent quand on ajoute un appareil
  for (double x : {22.0, 24.0, 26.0}) fil(F, x, 49, x, 55);
  // Bornier de la machine et moteur
  const std::string x1a = poserBorne(p, F, P(22, 70), 0, {});
  const ecad::ComposantElec* bx1 = composantDe(F, x1a);
  const std::string idX1 = bx1 ? bx1->uuid : std::string();
  poserBorne(p, F, P(24, 70), 0, idX1);
  poserBorne(p, F, P(26, 70), 0, idX1);
  poserBorne(p, F, P(34, 70), 0, idX1, true);
  for (double x : {22.0, 24.0, 26.0}) fil(F, x, 61, x, 68.5);
  fil(F, 34, 20, 34, 68.5);
  const std::string m1 = poserAppareil(p, F, "iec:moteur-3", P(24, 82), 0, "m3-2,2");
  fonction(F, m1, "Pompe de relevage");
  if (ecad::ComposantElec* m = composantDe(F, m1)) m->champs["emplacement"] = "Machine";
  for (double x : {22.0, 24.0, 26.0}) fil(F, x, 71.5, x, 78);
  fil(F, 34, 71.5, 34, 82);
  fil(F, 34, 82, 28, 82);
  // Alimentation de la commande : disjoncteur 1P+N et alimentation 24 V continu
  const std::string q2 = poserAppareil(p, F, "iec:disj-1pn", P(60, 34), 0, "dj-1pn-c2");
  fonction(F, q2, "Protection de l'alimentation 24 V");
  fil(F, 59, 31, 59, 12);
  fil(F, 61, 31, 61, 18);
  const std::string g1 = poserAppareil(p, F, "iec:alim-dc", P(60, 48), 0, "g-24-2,5");
  fonction(F, g1, "Alimentation de la commande");
  fil(F, 59, 37, 59, 44);
  fil(F, 61, 37, 61, 44);
  fil(F, 64, 47, 64, 20);
  fil(F, 59, 52, 59, 58);
  potentiel(F, "24V", 59, 58, 3);
  fil(F, 61, 52, 61, 62);
  potentiel(F, "0V", 61, 62, 3);

  // ----- Folio 3 : commande -----
  const std::size_t C = 2;
  fil(C, 8, 10, 60, 10);
  potentiel(C, "24V", 8, 10, 2);
  fil(C, 8, 92, 60, 92);
  potentiel(C, "0V", 8, 92, 2);
  // Contact du relais thermique, arrêt, marche et auto-maintien, bobine
  poserElement(p, C, "iec:contact-thermique-nf", P(20, 20), 0, idF1);
  fil(C, 20, 10, 20, 17);
  const std::string s1 = poserAppareil(p, C, "iec:bp-nf", P(20, 32), 0, "bp-rouge");
  fonction(C, s1, "Arrêt de la pompe");
  fil(C, 20, 23, 20, 29);
  const std::string s2 = poserAppareil(p, C, "iec:bp-no", P(20, 46), 0, "bp-vert");
  fonction(C, s2, "Marche de la pompe");
  poserElement(p, C, "iec:contact-no", P(28, 46), 0, km1);
  fil(C, 20, 35, 20, 39);
  fil(C, 20, 39, 20, 43);
  fil(C, 20, 39, 28, 39);
  fil(C, 28, 39, 28, 43);
  fil(C, 20, 49, 20, 53);
  fil(C, 28, 49, 28, 53);
  fil(C, 28, 53, 20, 53);
  fil(C, 20, 53, 20, 79);
  fil(C, 20, 85, 20, 92);
  // Voyant de marche (contact 23-24 du contacteur) et voyant de défaut (contact 97-98 du relais thermique)
  poserElement(p, C, "iec:contact-no", P(44, 46), 0, km1);
  fil(C, 44, 10, 44, 43);
  const std::string h1 = poserAppareil(p, C, "iec:voyant", P(44, 82), 0, "v-vert");
  fonction(C, h1, "Pompe en marche");
  fil(C, 44, 49, 44, 79);
  fil(C, 44, 85, 44, 92);
  poserElement(p, C, "iec:contact-thermique-no", P(60, 46), 0, idF1);
  fil(C, 60, 10, 60, 43);
  const std::string h2 = poserAppareil(p, C, "iec:voyant", P(60, 82), 0, "v-orange");
  fonction(C, h2, "Défaut surcharge");
  fil(C, 60, 49, 60, 79);
  fil(C, 60, 85, 60, 92);
  texte(C, 14, 98, "Commande en 24 V continu : arrêt prioritaire, marche par impulsion, auto-maintien par le contact 13-14 de -KM1.", 2.5);

  annoterElectrique(p, false);
  numeroterFils(p, true);

  // ----- Câble du moteur -----
  {
    const ecad::Connectivite k = ecad::connectivite(p.schema);
    Cable w;
    w.uuid = ecad::nouvelUuid();
    w.reference = "W1";
    w.designation = "H07RN-F";
    w.conducteurs = 4;
    w.section = 2.5;
    w.longueur = 8;
    if (const ecad::ComposantElec* x1 = p.schema.composant(idX1)) w.de = repereAffiche(p, *x1);
    if (const ecad::ComposantElec* m = composantDe(F, m1)) w.vers = repereAffiche(p, *m);
    const std::vector<std::string> couleurs = reperesConducteurs(4, true, false);  // BN BK GY GNYE
    for (int i = 0; i < 3; ++i) w.affectation.push_back({couleurs[static_cast<std::size_t>(i)], k.nom(idX1, std::to_string(i + 1))});
    w.affectation.push_back({"GNYE", "PE"});
    p.cables.push_back(w);
  }

  // ----- Armoire -----
  Armoire a;
  a.uuid = ecad::nouvelUuid();
  a.nom = "Coffret de la pompe";
  a.largeur = 400;
  a.hauteur = 500;
  a.profondeur = 200;
  a.marge = 20;
  p.armoires.push_back(a);
  implanterArmoire(p, 0);
  // Folios dessinés d'office : plan des borniers, coffret à l'échelle, nomenclature
  ajouterFolio(p, TypeFolio::Borniers, "Plan des borniers");
  ajouterFolio(p, TypeFolio::Implantation, "Implantation du coffret");
  ajouterFolio(p, TypeFolio::Nomenclature, "Nomenclature");
  return p;
}

}  // namespace prom::elec

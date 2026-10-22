// Prométhée : fabrication de l'armoire. Longueur de chaque fil d'après l'implantation (par les goulottes), liste
// de coupe avec embouts, filerie à commander, pertes des appareils et bilan thermique de l'enveloppe (CEI/TR 60890),
// chiffrage du tableau (matériel, filerie, main d'œuvre). Aucune dépendance à l'interface. Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "promethee/electrique.hpp"

namespace prom::elec {

// ---------- Longueurs des fils ----------
// Un fil part de son appareil vers la goulotte horizontale la plus proche, la suit, passe par la goulotte verticale
// du côté le plus court quand les deux appareils ne sont pas sur la même rangée, et redescend au second ; un appareil
// de porte rejoint la plaque par la charnière (lyre). Chaque extrémité reçoit sa réserve de raccordement. La
// longueur est inconnue (0) quand un appareil est hors armoire, pas encore implanté, ou dans une autre armoire :
// c'est alors un câble, pas un fil.
class EstimateurLongueurs {
 public:
  explicit EstimateurLongueurs(const ProjetElectrique& p);
  double longueur(const std::string& composantA, const std::string& composantB) const;  // mm, aux 50 mm supérieurs
  static constexpr double RESERVE = 75;     // mm par extrémité : dénudage, boucle de reprise
  static constexpr double CHARNIERE = 500;  // mm de la porte à la plaque

 private:
  struct Lieu {
    std::size_t armoire = 0;
    double x = 0, y = 0;  // centre de l'appareil (mm, repère de la plaque ou de la porte)
    bool porte = false;
  };
  struct Goulottes {
    std::vector<double> horizontales;  // axes (mm)
    double gauche = 0, droite = 0, hauteur = 0;
    bool verticales = false;
  };
  double surPlaque(const Goulottes& g, double xa, double ya, double xb, double yb) const;
  std::map<std::string, Lieu> m_lieux;
  std::vector<Goulottes> m_goulottes;
};

// ---------- Liste de coupe ----------
// Embout de câblage d'une section, en code couleur français (NF C 63-023) : « 1,5 mm² noir » ; double : deux fils
// sertis dans le même embout
std::string embout(double section, bool double_ = false);
struct LigneCoupe {
  int numero = 0;                    // ordre de coupe
  std::string fil, type, couleur;    // type : « H07V-K »
  double section = 0;                // mm²
  double longueur = 0;               // mm
  std::string de, vers, emboutDe, emboutVers, folio;
};
struct BobineFil {
  std::string type, couleur;
  double section = 0;
  double metres = 0;  // à commander, 10 % de chute compris
  int fils = 0;
};
struct ListeDeCoupe {
  std::vector<LigneCoupe> lignes;           // par section, couleur, puis longueur décroissante
  std::vector<BobineFil> bobines;           // filerie à prévoir
  std::map<std::string, int> embouts;       // « embout 1,5 mm² noir » → quantité
  int sansLongueur = 0;                     // liaisons dont un bout est hors armoire ou pas implanté
  std::vector<std::string> remarques;       // trop de fils sur une borne…
};
ListeDeCoupe listeDeCoupe(const ProjetElectrique& p);
std::string csvListeDeCoupe(const ProjetElectrique& p);

// ---------- Bilan thermique (CEI/TR 60890) ----------
const std::vector<std::string>& posesArmoire();  // 0 isolée et libre … 6 au milieu d'une rangée, contre un mur, toit couvert
// Surface d'échange efficace d'une enveloppe (m², dimensions en mm) selon sa pose
double surfaceEchangeArmoire(double largeur, double hauteur, double profondeur, int pose);
// Puissance dissipée par un appareil (W) à une charge donnée (part du courant assigné, 0 à 1) : les pertes du
// courant suivent son carré, celles des bobines et de l'électronique restent constantes. Données typiques du marché.
double pertesAppareil(const ProjetElectrique& p, const ecad::ComposantElec& c, double charge);
struct PerteAppareil {
  std::string composant, repere, designation;
  double puissance = 0;  // W
};
struct BilanThermique {
  std::vector<PerteAppareil> pertes;  // décroissantes
  double dissipee = 0;                // W
  double surface = 0;                 // m²
  double echauffement = 0;            // K
  double temperatureInterieure = 0;   // °C (moyenne de l'air intérieur)
  double ventilation = 0;             // m³/h nécessaires (0 : inutile)
  double climatisation = 0;           // W de froid nécessaires (0 : inutile)
  int ventilateurs = 0;               // ventilateurs filtrants déjà posés dans l'armoire
  int sansDonnee = 0;                 // appareils dont on ne connaît pas les pertes
  std::vector<std::string> remarques;
};
BilanThermique bilanThermique(const ProjetElectrique& p, std::size_t armoire);

// ---------- Chiffrage ----------
// Matériel (nomenclature au prix saisi ou indicatif), filerie et embouts (liste de coupe), main d'œuvre en temps
// unitaires de tableautier : pose de chaque appareil selon où il va, bornes, fils coupés, dénudés, sertis, repérés et
// raccordés, rails et goulottes, contrôle et essais (10 %).
struct PosteChiffrage {
  std::string nom;
  double quantite = 0, heures = 0;
};
struct Chiffrage {
  double materiel = 0, filerie = 0, heures = 0, mainOeuvre = 0, total = 0;  // € HT, heures
  int lignesSansPrix = 0;
  std::vector<PosteChiffrage> postes;
};
Chiffrage chiffrer(const ProjetElectrique& p);
std::string csvChiffrage(const ProjetElectrique& p);
double prixFilParMetre(double section);  // € HT/m, fil souple H07V-K / H05V-K, indicatif

}  // namespace prom::elec

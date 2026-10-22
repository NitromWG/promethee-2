// Prométhée : intégrité du signal et de l'alimentation (lot L18 ; 18.2, 18.3, 18.5 ; SI-01 à SI-08, PI-01 à PI-05).
// Chaque tronçon de piste reçoit son impédance et le plan de référence vraiment présent sous lui (zone remplie de la
// couche voisine) ; un tronçon qui passe au-dessus d'une fente ou d'un vide est signalé. Diaphonie estimée entre
// tronçons parallèles de réseaux différents. Chute de tension en continu d'un réseau d'alimentation : pistes et vias
// en résistances, résolu par les nœuds. Inductance de montage de chaque condensateur de découplage. Les analyses
// tournent sur la conception seule, sans moteur extérieur. Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- Impédance et plans de référence (18.3) ----------
struct Troncon {
  std::string piste, reseau;
  Couche couche = Couche::CuivreDessus;
  double longueur = 0, largeur = 0;  // mm
  double z0 = 0;                      // Ω (0 : couche intérieure, non calculée)
  bool reference = false;             // plan présent sous tout le tronçon
  double partSansPlan = 0;            // part de la longueur sans plan sous elle
  std::string plan;                   // réseau du plan de référence
};
struct RapportImpedance {
  std::vector<Troncon> troncons;
  std::vector<std::string> alertes;  // « piste de CLK sur 4,2 mm au-dessus d'une fente du plan GND »
};
// « reseaux » vide : tous les réseaux routés
RapportImpedance analyserImpedance(const ConceptionElec& c, const std::vector<std::string>& reseaux = {});

// ---------- Diaphonie (18.2) ----------
struct Couplage {
  std::string agresseur, victime;
  Couche couche = Couche::CuivreDessus;
  double longueurCouplee = 0, ecart = 0;  // mm
  double coefficient = 0;                 // diaphonie proche estimée (0 à 1) : 1 / (1 + (s/h)²)
};
// Couples de tronçons parallèles de réseaux différents, sur la même couche, plus longs que « longueurMin » et dont le
// coefficient dépasse « seuil »
std::vector<Couplage> analyserDiaphonie(const ConceptionElec& c, double longueurMin = 5, double seuil = 0.05);

// ---------- Chute de tension en continu (18.5, PI) ----------
struct ChargeCourant {
  std::string point;  // « U1.8 » : repère et numéro de pastille
  double courant = 0; // A
};
struct ResultatChute {
  std::map<std::string, double> tensions;  // point → tension (V) au bout des pistes
  std::map<std::string, double> chutes;    // point → chute (V) depuis la source
  double chuteMax = 0, courantMax = 0;      // A dans le tronçon le plus chargé
  std::string tronconMax;
  double densiteMax = 0;                    // A/mm²
  std::vector<std::string> alertes;         // point non relié par du cuivre à la source…
};
// Réseau « reseau » alimenté à « tension » V par la pastille « source » (« J1.1 ») ; pistes et vias seulement (les
// zones ne sont pas comptées : la chute réelle est plus faible si un plan les double)
ResultatChute chuteTension(const ConceptionElec& c, const std::string& reseau, const std::string& source, double tension, const std::vector<ChargeCourant>& charges);

// ---------- Découplage (18.5) ----------
struct MontageDecouplage {
  std::string condensateur, reseau, masse;
  double longueur = 0;    // mm de piste entre ses pastilles et les vias ou pastilles qui le relient
  int vias = 0;
  double inductance = 0;  // nH estimés : 1 nH/mm de piste, 0,5 nH par via, 0,4 nH de boîtier
};
std::vector<MontageDecouplage> analyserDecouplage(const ConceptionElec& c);

}  // namespace prom::ecad

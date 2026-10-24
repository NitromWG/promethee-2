// Prométhée : familles de contrôles de la DRC ajoutées par le lot L8 de la 0.34 — ligne de fuite (8.10, DRC-55) et
// taux de cuivre (CUI-13) ; isolements quels que soient les réseaux (DRC-77), isolement entre couches à travers le
// diélectrique (DRC-78), courant d'une branche tronçon par tronçon (DRC-79), sortie des pastilles CMS (DRC-80),
// longueur parallèle bornée (DRC-81) (8.25 ; DRC-08, DRC-31 à DRC-34, DRC-37 du catalogue).
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// Appelé par verifierDRCAvancee
void verifierFamilles034(const ConceptionElec& c, const Connectivite& k, const Remplissages& remplis, const std::vector<PastillePlacee>& pp, std::vector<Violation>& v);
// Règles d'assemblage (8.14) : écart entre composants (DRC-88), orientations permises (DRC-89), pont de vernis (DRC-90)
void verifierAssemblage034(const ConceptionElec& c, const std::vector<PastillePlacee>& pp, std::vector<Violation>& v);

// ---------- Ligne de fuite (8.10) ----------
// Réglages de surface (gardés dans ConceptionElec::extensions["isolementSurface"]) : degré de pollution, groupe de
// matériau (CTI), colonnes « circuit imprimé » de la table F.4 ; une rainure plus étroite que X (CEI 60664-1 tableau
// 2 : 0,25 mm, 1,0 mm, 1,5 mm selon la pollution) ne rallonge pas la ligne de fuite
struct ReglagesSurface {
  int pollution = 2, groupe = 3;
  bool circuitImprime = true;
};
ReglagesSurface reglagesSurface(const ConceptionElec& c);
void reglerSurface(ConceptionElec& c, const ReglagesSurface& r);
double largeurRainureX(int pollution);  // mm
// Plus court chemin sur la surface entre deux cuivres, qui contourne les obstacles (découpes, mm)
double ligneDeFuite(const Polygone& a, const Polygone& b, const Polygones& obstacles);
// Ligne de fuite exigée entre deux réseaux, d'après la tension de service de leurs classes (V crête, ramenée en
// efficace) ; 0 si aucune tension n'est déclarée
double ligneFuiteExigee(const ConceptionElec& c, const std::string& a, const std::string& b);

// ---------- Taux de cuivre (CUI-13) ----------
struct TauxCuivre {
  Couche couche = Couche::CuivreDessus;
  double cuivre = 0, carte = 0, taux = 0;  // mm², mm², % de la surface de la carte
};
std::vector<TauxCuivre> tauxCuivre(const ConceptionElec& c, const Connectivite& k, const Remplissages& r);

// ---------- Courant d'une branche (8.25) ----------
// Contrainte « courant » : texte « Q1.2>J1.1 » (de, vers), valeur.min en milliampères ; échauffement de la classe du
// réseau (10 °C à défaut). Le trajet le plus court dans le graphe du cuivre (pistes, vias, pastilles) ; chaque
// tronçon y est comparé à la largeur que demande l'IPC-2221 sur sa couche
struct TronconCourant {
  std::string piste;  // UUID ; vide : via ou pastille
  Couche couche = Couche::CuivreDessus;
  Pt a, b;
  Nm largeur = 0, requise = 0;
};
std::vector<TronconCourant> trajetBranche(const ConceptionElec& c, const Connectivite& k, const std::string& de, const std::string& vers, double courantA, double echauffement);

}  // namespace prom::ecad

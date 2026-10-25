// Prométhée : contrôles de fabrication et d'assemblage décrits en données (lot L8 de la 0.34 ; 8.17 ; FAB-25, FAB-28,
// FAB-29, FAB-18). Chaque contrôle porte son procédé (« gravure », « vernis », « assemblage », « nomenclature »), son
// fabricant, sa gravité, son poids et ses réglages ; ils sont relus d'un fichier JSON et menés sur un fil (arrêt
// possible). Pièges à acide et copeaux de vernis par ouverture et fermeture morphologiques (Clipper2) ; écarts pour la
// machine et la retouche ; soulèvement des petits passifs ; pâte sur via ; nomenclature ligne par ligne. Rapport
// illustré (une vignette SVG par constat) ; profil de refusion le plus contraignant tiré des articles ; pièces à
// étuver selon la J-STD-033.
// Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <map>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct ControleFabrication {
  std::string code, nom, procede, fabricant;  // « DFM-01 », « Piège à acide », « gravure », « » (tous)
  Gravite gravite = Gravite::Avertissement;
  double poids = 1;
  bool actif = true;
  std::map<std::string, double> reglages;  // mm, degrés, rapports…
};
// Contrôles par défaut : DFM-21 piège à acide (angle < 70° ou fermeture de 0,15 mm), DFM-22 copeau de vernis
// (< 0,1 mm), DFM-23 écart entre boîtiers (machine, 0,3 mm), DFM-24 écart de retouche autour des BGA et QFN (2 mm),
// DFM-25 soulèvement des petits passifs (0402 et moins, cuivre déséquilibré), DFM-26 pâte sur via, DFM-27 ligne de
// nomenclature incomplète (référence, fabricant), DFM-28 article sans profil de refusion, DFM-29 pièce à étuver
std::vector<ControleFabrication> controlesFabricationParDefaut();
std::vector<ControleFabrication> lireControlesFabrication(const std::string& json, std::string* erreur = nullptr);
std::string texteControlesFabrication(const std::vector<ControleFabrication>& v);

struct ConstatFabrication {
  std::string code, message, conseil;
  Gravite gravite = Gravite::Avertissement;
  Pt position;
  Couche couche = Couche::CuivreDessus;
  std::vector<std::string> objets;
  std::string vignette;  // SVG de la région autour du constat
};
struct BilanFabrication {
  std::vector<ConstatFabrication> constats;
  double score = 100;  // 100 moins les poids des constats, borné à 0
  std::string profilRefusion;  // le plus contraignant des articles
  double picRefusion = 0;      // °C
  std::vector<std::string> aEtuver;  // repères des pièces MSL ≥ 3 (J-STD-033)
  bool interrompu = false;
};
BilanFabrication controlerFabrication(const ConceptionElec& c, const std::vector<ControleFabrication>& controles = controlesFabricationParDefaut(),
                                      const std::atomic<bool>* arret = nullptr);
// Sur la connectivité et les zones déjà calculées (fil du calcul de l'atelier, validation) : rien n'est refait
BilanFabrication controlerFabrication(const ConceptionElec& c, const Connectivite& k, const Remplissages& zones,
                                      const std::vector<ControleFabrication>& controles = controlesFabricationParDefaut(), const std::atomic<bool>* arret = nullptr);
// Validation (publication) avec les contrôles de fabrication déjà menés, sur le fil du calcul de l'atelier : rien de
// lourd n'est refait à chaque relecture de l'inspecteur
Validation valider(const ConceptionElec& c, const Connectivite& k, const EtatRoutage& etat, const std::vector<Violation>& erc, const std::vector<Violation>& drc,
                   const std::vector<ConstatFabrication>& fabrication);
// Rapport illustré pour le fabricant (HTML autonome, une vignette par constat)
std::string rapportFabricationHtml(const ConceptionElec& c, const BilanFabrication& b);

}  // namespace prom::ecad

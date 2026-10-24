// Prométhée : routage automatique par stratégies (lot L8 de la 0.34 ; 8.28 ; ROU-44, ROU-45, PLA-34). Le routeur sans
// grille pose la géométrie à l'isolement par le moteur de contournement (5.7) ; des stratégies enregistrées (passes,
// ordre des liaisons, couches et directions préférées, coût d'une via, portée, réseaux visés) se comparent par essais
// sur une copie, et la meilleure s'applique en une commande, sans toucher aux pistes déjà posées. Le routeur avance
// liaison par liaison (pause, reprise) et rend un rapport de préparation. Avant lui, l'optimiseur d'échange de broches
// cherche, par l'algorithme hongrois, groupe par groupe, les échanges permis (broches d'un même groupe) qui démêlent
// le chevelu ; ceux qu'on accepte sont reportés au schéma. Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct StrategieRoutage {
  std::string nom = "Standard";
  int passes = 2;                       // nouvelles tentatives des liaisons restantes
  bool courtesDabord = true;            // ordre des liaisons : les plus courtes d'abord, sinon les plus longues
  std::vector<std::string> couches;     // codes des couches permises, dans l'ordre de préférence (vide : toutes)
  std::map<std::string, char> directions;  // code de couche → « H » ou « V » : direction préférée
  double coutVia = 5;                   // mm de piste qu'une via vaut au score
  double portee = 0;                    // mm : longueur maximale d'une liaison tentée (0 : toutes)
  std::string reseaux;                  // motif des réseaux visés (« USB_* ») ; vide : tous
  bool vias = true;                     // une liaison sans couche commune peut passer par une via
  friend bool operator==(const StrategieRoutage&, const StrategieRoutage&) = default;
};
nlohmann::ordered_json strategieVersJson(const StrategieRoutage& s);
StrategieRoutage strategieDepuisJson(const nlohmann::ordered_json& o);
// Stratégies enregistrées dans le projet (extensions["strategiesRoutage"]), et deux proposées d'office
std::vector<StrategieRoutage> strategiesRoutage(const ConceptionElec& c);
void enregistrerStrategies(ConceptionElec& c, const std::vector<StrategieRoutage>& s);

// Routeur pas à pas : chaque appel d'« avancer » route au plus « n » liaisons ; « termine » quand toutes les passes
// sont faites. Il travaille sur sa propre copie de la conception.
class RouteurAuto {
 public:
  RouteurAuto(const ConceptionElec& c, StrategieRoutage s);
  int avancer(int n = 1);
  bool termine() const { return m_termine; }
  const ConceptionElec& resultat() const { return m_c; }
  int routees() const { return m_routees; }
  int vias() const { return m_vias; }
  int restantes() const;
  double longueur() const;  // mm de piste posée par le routeur

 private:
  void preparerPasse();
  bool router(const Liaison& l);
  ConceptionElec m_c;
  StrategieRoutage m_s;
  std::vector<PastillePlacee> m_pp;
  std::vector<Liaison> m_file;
  std::size_t m_indice = 0;
  int m_passe = 0, m_routees = 0, m_vias = 0, m_poseesPasse = 0;
  std::size_t m_pistesAvant = 0;
  bool m_termine = false;
};

struct EssaiRoutage {
  std::string strategie;
  int liaisons = 0, routees = 0, vias = 0;
  double longueur = 0, duree = 0, score = 0;  // mm, s
  ConceptionElec resultat;
};
EssaiRoutage essayerStrategie(const ConceptionElec& c, const StrategieRoutage& s);
// Essais comparés, du meilleur au moins bon (plus de liaisons routées, puis le moindre coût longueur + vias)
std::vector<EssaiRoutage> comparerStrategies(const ConceptionElec& c, const std::vector<StrategieRoutage>& s);
// Applique un essai en une commande : pistes et vias que le routeur a ajoutées ; celles déjà posées restent
int appliquerEssai(ConceptionElec& c, const EssaiRoutage& e);

// Rapport de préparation : liaisons à faire, réseaux, liaisons sans couche commune, pastilles hors de la carte…
std::vector<std::string> rapportPreparation(const ConceptionElec& c);

// ---------- Optimiseur d'échange de broches ----------
struct EchangePropose {
  std::string composant, symbolePlace, reference;
  std::string numeroA, numeroB;
  double gain = 0;  // mm de chevelu en moins
};
// Échanges permis (broches d'un même « groupe » d'une même unité) qui raccourcissent le chevelu, par l'affectation
// optimale (algorithme hongrois) de chaque groupe
std::vector<EchangePropose> optimiserEchanges(const ConceptionElec& c);
// Reporte au schéma les échanges acceptés (rétroannotation, 7.8) ; rend le nombre fait
int appliquerEchanges(ConceptionElec& c, const std::vector<EchangePropose>& e);
// Affectation de coût minimal (algorithme hongrois, matrice carrée) : colonne choisie pour chaque ligne
std::vector<int> affectationHongroise(const std::vector<std::vector<double>>& cout);

}  // namespace prom::ecad

// Prométhée : logique numérique (lot L15 ; 15.9, 15.11, 15.12 ; SIM-48 à SIM-51, STR-28). Les portes du schéma
// (bibliothèque « Logique » et 74HC00 posé porte par porte) forment un circuit logique ; un simulateur à événements
// en logique à quatre états (0, 1, X, Z) le fait tourner porte par porte, avec le retard de chaque porte, et tire
// les tables de vérité. Le même circuit s'écrit en Verilog structurel, prêt pour Yosys. Un fichier de brochage de
// FPGA donne un symbole découpé en une unité par banc. Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

enum class Niveau : std::uint8_t { Bas, Haut, X, Z };
char lettreNiveau(Niveau n);  // « 0 », « 1 », « X », « Z »

struct Porte {
  std::string nom;     // « U1A »
  std::string genre;   // « ET », « OU », « NON », « NON_ET », « NON_OU », « OU_X », « TAMPON », « D »
  std::vector<int> entrees;  // signaux ; bascule D : D puis horloge
  std::vector<int> sorties;  // bascule D : Q puis Q barre (−1 : non reliée)
  double retard = 10e-9;     // s (champ « Retard » du composant : « 7 ns »)
};
struct CircuitLogique {
  std::vector<std::string> signaux;  // noms des réseaux
  std::map<std::string, int> indice;
  std::vector<Porte> portes;
  std::vector<std::string> entrees;  // signaux que rien ne pilote
  std::vector<std::string> alertes;  // sortie pilotée deux fois…
  int signal(const std::string& nom) const {
    const auto it = indice.find(nom);
    return it == indice.end() ? -1 : it->second;
  }
};
CircuitLogique circuitLogique(const ConceptionElec& c);

class SimulateurLogique {
 public:
  explicit SimulateurLogique(CircuitLogique c);
  void forcer(const std::string& signal, Niveau v);  // entrée posée à l'instant courant
  // Fait avancer le temps jusqu'à « jusqua » (s) ; rend le nombre d'événements traités
  int avancer(double jusqua);
  int avancerJusquauRepos(double limite = 1e-3);  // jusqu'à ce qu'aucun événement ne reste (ou la limite)
  Niveau niveau(const std::string& signal) const;
  double maintenant() const { return m_t; }
  struct Changement {
    double t;
    int signal;
    Niveau v;
  };
  const std::vector<Changement>& trace() const { return m_trace; }
  const std::vector<int>& portesActives() const { return m_actives; }  // évaluées au dernier pas (surbrillance)
  const CircuitLogique& circuit() const { return m_c; }

 private:
  void evaluer(std::size_t porte);
  void planifier(double t, int signal, Niveau v);
  CircuitLogique m_c;
  std::vector<Niveau> m_niveaux;
  std::vector<std::vector<std::size_t>> m_lecteurs;  // signal → portes qui le lisent
  struct Evenement {
    double t;
    std::uint64_t ordre;
    int signal;
    Niveau v;
  };
  std::vector<Evenement> m_file;  // tas
  std::vector<Niveau> m_prevu;    // dernière valeur planifiée par signal
  std::vector<Niveau> m_horlogesAvant;
  std::uint64_t m_ordre = 0;
  double m_t = 0;
  std::vector<Changement> m_trace;
  std::vector<int> m_actives;
};

// Table de vérité : toutes les combinaisons des entrées (au plus 12), une ligne « 01 → 10 » par combinaison
std::vector<std::pair<std::string, std::string>> tableVerite(const CircuitLogique& c, const std::vector<std::string>& entrees, const std::vector<std::string>& sorties);

// Verilog structurel (15.12) : primitives (and, or, not, nand, nor, xor, buf) avec leur retard, bascules en
// « always @(posedge) » ; entrées : les réseaux nommés sans pilote, sorties : les réseaux nommés pilotés
std::string verilogStructurel(const CircuitLogique& c, const std::string& module);

// ---------- Brochage de FPGA (15.11) ----------
// CSV « broche, nom, banc[, type] » (séparateur virgule ou point-virgule, en-tête facultative), comme les
// fichiers de brochage des fabricants ramenés à ces colonnes. Une unité par banc, une unité pour les alimentations
// et les masses ; les paires différentielles (_P et _N) côte à côte. Rend un symbole « id ».
DefSymbole symboleDepuisBrochageFpga(const std::string& csv, const std::string& id, const std::string& nom, std::vector<std::string>* alertes = nullptr);

}  // namespace prom::ecad

// Prométhée : composants décrits par du code, machines à états et logique programmable (lot L15 de la 0.34 ; 15.7,
// 15.10, 15.12 ; EMB-13, EMB-20, EMB-22, EMB-26, EMB-27).
//
// Un composant décrit en Verilog ou en VHDL se pose au schéma (un symbole tiré de ses ports) et se co-simule avec le
// circuit : son module tourne dans le simulateur de logique du poste (Icarus Verilog ou Verilator pour le Verilog,
// GHDL pour le VHDL), lancé en processus à part avec un banc d'essai écrit par Prométhée qui lit les entrées et rend
// les sorties à chaque changement ; ses broches de sortie sont des sources de 50 Ω de la simulation continue.
//
// Machines à états : sur la base du GRAFCET de l'atelier électrique (une étape active à la fois), simulées au rythme
// de l'horloge, converties en table de transitions, en circuit à bascules D et portes dont les équations sont
// réduites par Quine et McCluskey, ou en VHDL simulé par GHDL. Schéma à contacts simulé en cycle d'automate :
// contacts, bobines, mises à 1 et à 0, temporisations et compteurs.
//
// Logique programmable : schéma logique exporté en VHDL structurel et simulé par GHDL (table de vérité comparée à
// celle du schéma), synthétisé par Yosys ; sommes de produits placées dans les macrocellules d'un GAL16V8 en mode
// simple, fichier JEDEC écrit d'après sa carte des fusibles. Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "promethee/electrique.hpp"
#include "promethee/electrique_grafcet.hpp"
#include "promethee/logique.hpp"
#include "promethee/processus.hpp"
#include "promethee/simulation.hpp"
#include "promethee/simulation_continue.hpp"

namespace prom::ecad {

// ---------- Composants décrits en HDL (15.7) ----------
struct PortHdl {
  std::string nom;
  bool sortie = false;
  int largeur = 1;  // bus « [3:0] » : 4
};
struct ModuleHdl {
  std::string langage = "verilog";  // « verilog », « vhdl »
  std::string nom, source;
  std::vector<PortHdl> ports;
  // Bits dans l'ordre des ports (« q[0] »… pour un bus), entrées puis sorties
  std::vector<std::string> bitsEntree() const;
  std::vector<std::string> bitsSortie() const;
};
std::optional<ModuleHdl> lireModuleHdl(const std::string& source, const std::string& langage, std::string* erreur = nullptr);
std::string langageHdl(const std::string& chemin);  // « .v », « .sv » : verilog ; « .vhd », « .vhdl » : vhdl
// Outil du poste pour ce langage : « iverilog », « verilator », « ghdl » ; vide s'il manque
std::string outilHdl(const std::string& langage);
// Symbole du composant : entrées à gauche, sorties à droite (un bus donne une broche par bit, « q0 »…), VCC et GND
DefSymbole symboleHdl(const ModuleHdl& m);
// Pose le composant décrit par « source » sur la feuille ; son code va dans le champ HDL.Code. Rend son UUID.
std::string poserComposantHdl(ConceptionElec& c, const ModuleHdl& m, std::size_t feuille, Pt position, const std::string& reference = {});
std::optional<ModuleHdl> moduleDuComposant(const ComposantElec& x);

// Simulateur de logique du poste piloté pas à pas
class SimulateurHdl {
 public:
  bool demarrer(const ModuleHdl& m, const std::string& dossier, std::string* erreur = nullptr);
  // Entrées (bits dans l'ordre de bitsEntree) posées, puis sorties relues (ordre de bitsSortie) ; nullopt si le
  // simulateur ne répond pas
  std::optional<std::vector<int>> evaluer(const std::vector<int>& entrees);
  void arreter();
  const std::string& outil() const { return m_outil; }
  const std::string& journal() const { return m_journal; }

 private:
  ProcessusInteractif m_p;
  ModuleHdl m_m;
  std::string m_outil, m_journal;
};

// Participant de la simulation continue : les broches du composant reliées aux bits du module par leur nom. Le
// module part de ses entrées à 0 : une entrée déjà haute au premier pas compte pour lui comme un front montant
// (là où un modèle XSPICE part du point de repos) ; une horloge qui démarre basse donne les mêmes fronts aux deux.
class CosimulationHdl : public ParticipantContinu {
 public:
  CosimulationHdl(const ConceptionElec& c, const std::string& reference, const NetlistSpice& n);
  bool demarrer(const std::string& dossier, std::string* erreur = nullptr);
  std::vector<std::string> sondes() const override;
  void pas(double t, const std::map<std::string, double>& valeurs) override;
  bool source(const std::string& nom, double t, double& valeur) override;
  void interrompre() override;
  int evaluations() const;
  std::map<std::string, int> sorties() const;  // bit → niveau

 private:
  struct Broche {
    std::string numero, bit, vecteur;
    bool sortie = false;
  };
  std::string m_reference, m_vecteurVcc;
  std::optional<ModuleHdl> m_m;
  SimulateurHdl m_sim;
  std::vector<Broche> m_broches;
  std::vector<int> m_entrees, m_sorties;
  double m_vcc = 5;
  int m_evaluations = 0;
  bool m_pret = false, m_interrompu = false;
  mutable std::mutex m_verrou;
};

// ---------- Réduction des équations (Quine et McCluskey) ----------
struct Implicant {
  std::uint32_t valeur = 0, masque = 0;  // masque : variables absentes du produit
  friend bool operator==(const Implicant&, const Implicant&) = default;
};
// Couverture minimale des « uns » (indifférents permis) : implicants premiers, essentiels d'abord
std::vector<Implicant> reduire(int variables, const std::vector<std::uint32_t>& uns, const std::vector<std::uint32_t>& indifferents = {});
// « a·/b + c » ; variable 0 = bit de poids faible ; « 0 » ou « 1 » pour une fonction constante
std::string sommeDeProduits(const std::vector<Implicant>& p, const std::vector<std::string>& noms, int variables);
bool evaluerProduits(const std::vector<Implicant>& p, std::uint32_t entree);

// ---------- Machines à états (15.10) ----------
struct EtatMachine {
  std::string nom;
  std::vector<std::string> sorties;  // à 1 dans cet état (machine de Moore)
};
struct TransitionMachine {
  int de = 0, vers = 0;
  std::string condition = "1";  // réceptivité du GRAFCET : « pieton », « /a · b », « 1 »
};
struct MachineEtats {
  std::string nom = "machine";
  std::vector<EtatMachine> etats;
  int initial = 0;
  std::vector<TransitionMachine> transitions;  // la première vraie l'emporte ; aucune : l'état reste
  std::vector<std::string> entrees, sorties;
};
// Étapes du GRAFCET (une seule active à la fois) → états ; réceptivités → conditions ; actions continues → sorties
std::optional<MachineEtats> machineDepuisGrafcet(const prom::elec::Grafcet& g, std::string* erreur = nullptr);
prom::elec::Grafcet grafcetDepuisMachine(const MachineEtats& m);
MachineEtats machineFeuxTricolores();  // rouge 3 périodes, vert jusqu'à l'appel du piéton (au moins 2), orange 1
bool conditionVraie(const std::string& condition, const std::map<std::string, bool>& entrees);
// Une ligne par période d'horloge : les sorties, puis l'état suivant pris sur le front
using SequenceSorties = std::vector<std::map<std::string, bool>>;
SequenceSorties simulerMachine(const MachineEtats& m, const std::vector<std::map<std::string, bool>>& entrees, int periodes);
struct LigneTransition {
  std::string etat, condition, suivant;
  std::vector<std::string> sorties;
};
std::vector<LigneTransition> tableTransitions(const MachineEtats& m);
// Codage binaire (l'état initial en 0) ; bascules D avec remise à zéro synchrone « raz »
struct EquationsMachine {
  int bits = 0;
  std::vector<std::string> variables;               // état q0… puis entrées
  std::map<std::string, std::vector<Implicant>> d;  // « d0 » → produits
  std::map<std::string, std::vector<Implicant>> sorties;
  std::map<std::string, std::string> textes;        // « d0 » → « /q1·q0 + … »
};
EquationsMachine equationsMachine(const MachineEtats& m);
// Circuit de portes à deux entrées et de bascules D posé sur une feuille, signaux nommés : horloge, raz, entrées,
// sorties, q0…
ConceptionElec circuitBascules(const MachineEtats& m, EquationsMachine* equations = nullptr);
// Le circuit simulé par le simulateur logique : raz la première période, puis les entrées, un front par période
SequenceSorties simulerCircuitBascules(const ConceptionElec& c, const MachineEtats& m, const std::vector<std::map<std::string, bool>>& entrees, int periodes);
std::string vhdlMachine(const MachineEtats& m);
std::string bancVhdlMachine(const MachineEtats& m, const std::vector<std::map<std::string, bool>>& entrees, int periodes);
// GHDL : analyse, élaboration et lancement (« entite ») dans « dossier » ; lignes écrites par le banc
struct ResultatGhdl {
  bool ok = false;
  std::string journal;
  std::vector<std::string> lignes;
};
ResultatGhdl simulerGhdl(const std::map<std::string, std::string>& fichiers, const std::string& entite, const std::string& dossier, double delai = 180);
SequenceSorties lireSequenceGhdl(const std::vector<std::string>& lignes, const std::vector<std::string>& sorties);

// ---------- Schéma à contacts ----------
struct ElementContacts {
  std::string genre = "contact";  // « contact », « contactInverse », « bobine », « miseA1 », « miseA0 », « tempo », « compteur »
  std::string variable;
  double duree = 0;   // tempo au travail (s)
  int consigne = 0;   // compteur : valeur atteinte
};
struct EchelonContacts {
  std::vector<std::vector<ElementContacts>> branches;  // branches en parallèle, contacts en série dans chacune
  ElementContacts sortie;
};
struct ProgrammeContacts {
  std::vector<EchelonContacts> echelons;
};
class AutomateContacts {
 public:
  explicit AutomateContacts(ProgrammeContacts p) : m_p(std::move(p)) {}
  void fixer(const std::string& entree, bool v) { m_v[entree] = v; }
  void cycle(double dt);  // lecture des entrées, échelons dans l'ordre, sorties
  bool valeur(const std::string& v) const;
  int compte(const std::string& compteur) const;

 private:
  ProgrammeContacts m_p;
  std::map<std::string, bool> m_v, m_avant;
  std::map<std::string, double> m_tempo;
  std::map<std::string, int> m_compte;
};

// ---------- Logique programmable (15.12) ----------
// VHDL structurel d'un circuit logique (portes en affectations concurrentes, bascules en processus)
std::string vhdlStructurel(const CircuitLogique& c, const std::string& entite);
// Banc d'essai qui parcourt toutes les combinaisons des entrées et écrit « entrées sorties » en binaire
std::string bancTableVerite(const std::string& entite, const std::vector<std::string>& entrees, const std::vector<std::string>& sorties);
// Table de vérité relue de GHDL, au format de tableVerite (« 01 » → « 10 »)
std::vector<std::pair<std::string, std::string>> tableVeriteGhdl(const CircuitLogique& c, const std::vector<std::string>& entrees, const std::vector<std::string>& sorties,
                                                                 const std::string& dossier, std::string* erreur = nullptr);
// Synthèse par Yosys du Verilog structurel : nombre de cellules, ou erreur
struct SyntheseYosys {
  bool ok = false;
  int cellules = 0;
  std::string journal;
};
SyntheseYosys synthetiserYosys(const std::string& verilog, const std::string& module, const std::string& dossier);
// GAL16V8 en mode simple : jusqu'à 8 sorties combinatoires (broches 12 à 19), 7 produits chacune, entrées sur les
// broches 2 à 9 et 11 ; « equations » : sortie → produits sur « entrees » (variable i : bit i)
struct PlacementGal {
  bool ok = false;
  std::map<std::string, int> brochesEntrees, brochesSorties;
  std::string jedec;
  std::vector<std::string> erreurs;
};
PlacementGal placerGal16V8(const std::vector<std::string>& entrees, const std::map<std::string, std::vector<Implicant>>& equations, const std::string& nom = "PROMETHEE");
// Relecture d'un fichier JEDEC : fusibles (1 : intact) et somme de contrôle vérifiée
struct FusiblesJedec {
  std::vector<bool> fusibles;
  bool sommeJuste = false;
  int nombre = 0;
};
std::optional<FusiblesJedec> lireJedec(const std::string& texte);
// Fonction réalisée par le GAL programmé (sortie de la broche pour une combinaison des broches d'entrée)
bool evaluerGal16V8(const FusiblesJedec& f, int brocheSortie, const std::map<int, bool>& entrees);

}  // namespace prom::ecad

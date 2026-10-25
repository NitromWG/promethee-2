// Prométhée : enseignement (lot L16 de la 0.34 ; 16.10 à 16.14 ; ENS-01 à ENS-13).
//
// Salle de TP : réglages de l'établissement (dossier des bibliothèques, profil de fabricant, interface simplifiée,
// actions masquées) lus d'un fichier posé à côté du paquet portable, recueil d'exemples prêts à simuler.
// Consignes et séries : étapes écrites par l'enseignant en données (texte, média, question, moment de la
// simulation, condition choisie dans une liste), chacune vérifiée seule sur le projet de l'élève.
// Couche de l'enseignant : scellée par un HMAC-SHA-256 (toute retouche se voit au relevé), parties verrouillées que
// les commandes refusent avec la raison, profil de restrictions, pannes cachées appliquées à la netlist seulement.
// Évaluation : montage de l'élève comparé à la référence par isomorphisme de graphes étiquetés (VF2), aux repères
// près ; formule comparée en des points tirés au hasard ; mode examen. Classe sans service en ligne : devoirs
// publiés dans un dossier partagé, rendus relevés, notés, notes en CSV, paquet IMS Common Cartridge ; circuit
// exporté en page HTML autonome qui le simule hors ligne (solveur nodal en JavaScript, sans réseau).
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"
#include "promethee/electronique_parcours.hpp"
#include "promethee/simulation.hpp"

namespace prom::ecad {

// ======================================================================================================================
// Salle de TP (16.10)
// ======================================================================================================================
struct ReglagesEtablissement {
  std::string nom;                          // « Lycée Jean-Moulin, section STI2D »
  std::string dossierBibliotheques;         // imposé ; relatif au fichier des réglages ou absolu
  std::string profilFabricant;              // nom d'un profil du catalogue (règles des cartes neuves)
  bool interfaceSimplifiee = false;
  bool interfaceVerrouillee = false;        // l'élève ne revient pas à l'interface complète
  std::vector<std::string> actionsMasquees;  // identifiants du registre des actions
  std::string dossierClasse;                // dossier partagé des devoirs et des rendus
  friend bool operator==(const ReglagesEtablissement&, const ReglagesEtablissement&) = default;
};
inline constexpr const char* kFichierEtablissement = "etablissement.json";
std::optional<ReglagesEtablissement> lireReglagesEtablissement(const std::string& json, std::string* erreur = nullptr);
std::string ecrireReglagesEtablissement(const ReglagesEtablissement& r);
// Dossier imposé, ramené à un chemin absolu à partir du dossier du fichier des réglages
std::string resoudreDossier(const std::string& dossierDuFichier, const std::string& chemin);

// Recueil d'exemples, ouverts depuis l'accueil en un clic, prêts à simuler, chacun avec une courte explication
struct ExempleCours {
  std::string id, titre, theme, explication;  // thèmes : bases, filtres, amplificateurs, 555, logique, alimentations
  std::function<ConceptionElec()> creer;
};
const std::vector<ExempleCours>& recueilExemples();
const ExempleCours* exempleCours(const std::string& id);
// Diviseur de tension 5 V → 2,5 V (R2 entre +5V et MID, R1 entre MID et GND, 10 kΩ chacune), filtre RC passe-bas
// de 1 kHz (R1 1,59 kΩ, C1 100 nF, source « Source_tension » alternative VG1 en entrée)
ConceptionElec exempleDiviseur();
ConceptionElec exempleFiltreRC();

// ======================================================================================================================
// Consignes et séries (16.11)
// ======================================================================================================================
enum class GenreCondition : std::uint8_t { Aucune, ComposantPose, BrochesReliees, MesureDansPlage, Reponse };
const char* codeCondition(GenreCondition g);  // « aucune », « composant », « liaison », « mesure », « reponse »
GenreCondition conditionDepuis(const std::string& code);
// Conditions proposées à l'enseignant, dans l'ordre de la liste : (code, libellé)
const std::vector<std::pair<std::string, std::string>>& conditionsProposees();
struct ConditionEtape {
  GenreCondition genre = GenreCondition::Aucune;
  std::string symbole;     // composant : symbole (« R », « LED ») ou préfixe de repère (« R ») ; vide : tout composant
  std::string valeur;      // composant : valeur voulue (« 330 », « 10k » ; comparée en grandeur), vide : toute
  int nombre = 1;          // composant : au moins
  std::string a, b;        // liaison : « R1.2 » et « D1.2 », ou un nom de réseau (« GND ») ; mesure : a = réseau
  std::string grandeur = "tension";  // mesure : « tension » (V au point de repos)
  double min = 0, max = 0;
  std::string reponse;     // réponse : attendue (sans casse ni espaces ; un nombre à 2 % près)
  friend bool operator==(const ConditionEtape&, const ConditionEtape&) = default;
};
struct EtapeConsigne {
  std::string titre, texte;
  std::string media;       // chemin relatif au sous-dossier « consignes » du projet (image, son, vidéo)
  std::string question;
  double instant = -1;     // moment de la simulation lié (s) ; −1 : aucun
  ConditionEtape condition;
  friend bool operator==(const EtapeConsigne&, const EtapeConsigne&) = default;
};
struct Consigne {
  std::string id, titre, description;
  std::vector<EtapeConsigne> etapes;
  friend bool operator==(const Consigne&, const Consigne&) = default;
};
// Série : circuits préparés (conceptions du projet, par nom), consignes, problèmes et exemples résolus
struct SerieExercices {
  std::string titre;
  std::vector<std::string> circuits, consignes;
  std::vector<std::pair<std::string, std::string>> problemes;  // énoncé, corrigé
  friend bool operator==(const SerieExercices&, const SerieExercices&) = default;
};
std::vector<Consigne> consignesDe(const ConceptionElec& c);  // c.extensions["consignes"]
void ecrireConsignes(ConceptionElec& c, const std::vector<Consigne>& v);
std::vector<SerieExercices> seriesDe(const ConceptionElec& c);  // c.extensions["series"]
void ecrireSeries(ConceptionElec& c, const std::vector<SerieExercices>& v);
std::string consigneEnTexte(const Consigne& c);  // fichier « .consigne.json » partagé à la classe
std::optional<Consigne> consigneDepuisTexte(const std::string& texte, std::string* erreur = nullptr);
struct EtatEtape {
  bool faite = false;
  std::string raison;  // ce qui manque, ou ce qui a été vu
};
// Vérification d'une étape sur le projet de l'élève ; la mesure passe par le point de repos de ngspice
EtatEtape verifierEtape(const EtapeConsigne& e, const ConceptionElec& eleve, const std::string& reponse = {});
// Parcours « Apprendre » tiré d'une consigne : ses étapes se vérifient seules
ParcoursApprendre parcoursDepuisConsigne(const Consigne& c);

// ======================================================================================================================
// Couche de l'enseignant (16.12)
// ======================================================================================================================
enum class GenrePanne : std::uint8_t { Coupure, CourtCircuit, ValeurFausse };
struct PanneCachee {
  GenrePanne genre = GenrePanne::Coupure;
  std::string composant;  // repère (coupure, valeur fausse)
  std::string broche;     // coupure : broche ouverte (vide : la première)
  std::string a, b;       // court-circuit : deux réseaux
  std::string valeur;     // valeur fausse
  std::string titre;      // « R2 coupée »
  friend bool operator==(const PanneCachee&, const PanneCachee&) = default;
};
struct RestrictionsEleve {
  std::vector<std::string> actionsMasquees, instrumentsMasques, analysesMasquees;
  bool interfaceSimplifiee = false, verrouillee = false;
  std::map<std::string, std::string> profilSimulation;  // options du moteur distribuées (reltol, method…)
  friend bool operator==(const RestrictionsEleve&, const RestrictionsEleve&) = default;
};
struct CoucheEnseignant {
  std::vector<std::string> verrous;  // UUID des composants verrouillés
  RestrictionsEleve restrictions;
  std::vector<PanneCachee> pannes;
  std::vector<Consigne> consignes;
  std::string sceau;                 // HMAC-SHA-256 en hexadécimal ; vide : non scellée
  friend bool operator==(const CoucheEnseignant&, const CoucheEnseignant&) = default;
};
std::optional<CoucheEnseignant> coucheEnseignant(const ConceptionElec& c);  // c.extensions["enseignant"]
void ecrireCoucheEnseignant(ConceptionElec& c, const CoucheEnseignant& e);
// Sceau : HMAC-SHA-256, de clé le secret de l'enseignant, sur la couche (sans son sceau) et le contenu des parties
// verrouillées (symbole, valeur et champs des composants, leurs poses et les fils qui les touchent)
std::string calculerSceau(const ConceptionElec& c, const CoucheEnseignant& e, const std::string& secret);
void sceller(ConceptionElec& c, const std::string& secret);
enum class EtatSceau : std::uint8_t { Absent, Intact, Retouche };
EtatSceau verifierSceau(const ConceptionElec& c, const std::string& secret);
// Modification refusée par la couche : raison (composant verrouillé touché, couche retouchée), vide si permise
std::string refusEnseignant(const ConceptionElec& avant, const ConceptionElec& apres);
bool actionPermise(const ConceptionElec& c, const std::string& action);
// Pannes cachées : rangées chiffrées (la couche ne les montre pas en clair), appliquées à la netlist générée
std::vector<PanneCachee> pannesCachees(const ConceptionElec& c);
void appliquerPannes(NetlistSpice& n, const ConceptionElec& c, const std::vector<PanneCachee>& pannes);

// ======================================================================================================================
// Évaluation (16.13)
// ======================================================================================================================
struct ResultatEvaluation {
  bool juste = false;
  std::vector<std::string> ecarts;                    // « une résistance (R4, 10k) de trop »
  std::map<std::string, std::string> correspondance;  // repère de l'élève → repère de la référence
};
// Graphe étiqueté biparti (composants, réseaux) : composant étiqueté par son symbole et sa valeur comprise, réseau
// par son nom quand il est d'alimentation (GND, +5V), arête par la broche (les broches des passifs symétriques se
// valent) ; isomorphisme par VF2
ResultatEvaluation evaluerMontage(const ConceptionElec& eleve, const ConceptionElec& reference);
// Formule de l'élève comparée à la référence en « points » tirés au hasard (variables entre 0,5 et 2)
bool formulesEquivalentes(const std::string& eleve, const std::string& reference, int points = 12, double tolerance = 1e-6);
// Nombre de l'élève comparé à l'attendu (tolérance relative)
bool nombreJuste(const std::string& eleve, double attendu, double tolerance = 0.02);
struct ControleEleve {
  std::string mode;  // « rapide » (à volonté, non compté), « complet » (compté), « entrainement », « examen »
  int controlesComptes = 0;
  std::vector<std::pair<std::string, std::string>> avertissementsJustifies;  // avertissement, justification
  std::vector<std::string> avertissementsValides;                           // validés par l'enseignant
};

// ======================================================================================================================
// Classe et diffusion (16.14)
// ======================================================================================================================
struct Devoir {
  std::string id, titre, consigne;      // consigne : texte, ou identifiant d'une consigne
  std::string echeance;                 // ISO 8601 (« 2026-11-14T18:00 »)
  std::vector<std::string> interdits;   // actions masquées pendant le devoir
  std::string fichierProjet;            // projet de départ, dans le dossier du devoir
  friend bool operator==(const Devoir&, const Devoir&) = default;
};
struct Rendu {
  std::string eleve, fichier, depose;   // dépôt ISO 8601
  bool enRetard = false;
  double note = -1;
  std::string remarques;
};
// Dossier de la classe : « devoirs/<id>/devoir.json », rendus « devoirs/<id>/rendus/<élève>.promethee »
bool publierDevoir(const std::string& dossierClasse, const Devoir& d, const std::string& projet, std::string* erreur = nullptr);
std::vector<Devoir> devoirsPublies(const std::string& dossierClasse);
bool deposerRendu(const std::string& dossierClasse, const std::string& devoir, const std::string& eleve, const std::string& projet, const std::string& quand, std::string* erreur = nullptr);
std::vector<Rendu> releverRendus(const std::string& dossierClasse, const std::string& devoir);
bool noter(const std::string& dossierClasse, const std::string& devoir, const std::string& eleve, double note, const std::string& remarques);
std::string notesCsv(const std::vector<Rendu>& rendus, const std::string& devoir);  // carnet de notes de l'ENT
std::string paquetCommonCartridge(const Devoir& d, const std::string& projet);      // zip IMS CC 1.1
// Page HTML autonome : schéma navigable (SVG), courbes, simulation en JavaScript par analyse nodale modifiée
// (résistances, condensateurs, inductances, sources) ; aucune ressource extérieure
std::string pageHtmlCircuit(const ConceptionElec& c, const std::string& entree = {}, const std::string& sortie = {});
// Circuit linéaire extrait pour la page (repris par les essais) : éléments « R », « C », « L », « V »
struct ElementLineaire {
  char genre = 'R';
  std::string nom, a, b;  // nœuds (« 0 » la masse)
  double valeur = 0, ac = 0;
};
std::vector<ElementLineaire> circuitLineaire(const ConceptionElec& c, std::string* erreur = nullptr);

}  // namespace prom::ecad

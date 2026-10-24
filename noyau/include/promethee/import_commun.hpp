// Prométhée : chaîne d'import commune à tous les formats de cartes (lot L10 de la 0.34 ; 10.12 ; ECH-18, ECH-27).
// Chaque importeur (KiCad, EAGLE, Altium, PADS…) rend une conception ; cette chaîne la finit de la même façon pour
// tous : un tableau associe chaque couche d'origine à une couche de Prométhée, proposé d'après le nom et la fonction
// de la couche et gardé par logiciel d'origine ; une carte et un schéma lus séparément se relient par les repères ; et
// l'import peut viser la carte ouverte : repères en double renumérotés, réseaux homonymes montrés avant d'être réunis,
// cuivres ajoutés à l'empilage s'il en manque, le tout en une seule modification (un seul Ctrl+Z). Licence
// GPL-3.0-only.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- Lecture d'après le fichier ----------
// Projet, schéma ou carte d'un autre logiciel, reconnu par son extension et son contenu ; les fichiers voisins (même
// nom de base : schéma et carte d'un projet KiCad ou EAGLE, .kicad_pro des règles) sont lus par « lire » (nom de
// fichier → contenu, rien s'il manque) et reliés
struct ImportTiers {
  ConceptionElec conception;
  std::string logiciel;              // « KiCad », « EAGLE »…
  std::vector<std::string> fichiers; // fichiers lus
  std::vector<std::string> alertes;
};
using LecteurVoisin = std::function<std::optional<std::string>(const std::string&)>;
std::optional<ImportTiers> importerConceptionTierce(const std::string& nomFichier, const std::string& contenu, const LecteurVoisin& lire, std::string* erreur = nullptr);
std::string filtreConceptionsTierces();  // « *.kicad_pcb *.kicad_sch *.kicad_pro *.sch *.brd … »

// ---------- Couches ----------
struct LigneAssociation {
  std::string origine;            // nom de la couche dans le fichier d'origine (« In1.Cu », « 2 Route2 », « Mid1 »)
  std::string fonction;           // « cuivre », « sérigraphie », « vernis », « pâte », « colle », « contour »…
  Couche lue = Couche::CuivreDessus;   // couche où l'importeur a posé ses objets
  std::optional<Couche> cible;    // couche retenue ; vide : couche ignorée (ses objets ne sont pas repris)
  int objets = 0;                 // objets de la carte importée sur cette couche
};
struct AssociationCouches {
  std::string logiciel;
  std::vector<LigneAssociation> lignes;
  int cuivresNecessaires = 2;     // cuivres que la carte de destination doit avoir
};
// Les noms d'origine sont ceux que l'importeur a notés dans l'extension « importCouches » de la conception (code de
// Prométhée → nom d'origine), sinon le code lui-même. La proposition garde la couche lue, ramène les cuivres internes
// dans l'ordre sur ceux de la destination, puis applique l'association gardée pour ce logiciel (nom d'origine → code).
AssociationCouches proposerAssociation(const std::string& logiciel, const ConceptionElec& importee, int cuivresDestination,
                                       const std::map<std::string, std::string>& gardee = {});
std::map<std::string, std::string> associationAGarder(const AssociationCouches& a);  // nom d'origine → code retenu
// Change les couches de tout ce que porte la carte (pistes, vias, zones, textes, dessins, cotes, régions, zones de
// règles) ; les objets d'une couche ignorée sont retirés. Rend le nombre d'objets changés ou retirés.
int appliquerAssociation(ConceptionElec& c, const AssociationCouches& a);
std::string fonctionCouche(Couche c);

// ---------- Carte et schéma lus séparément ----------
struct RapportLiaison {
  int relies = 0;                          // empreintes de la carte reliées à un composant du schéma
  std::vector<std::string> carteSeule;     // repères posés sur la carte sans composant au schéma
  std::vector<std::string> schemaSeul;     // composants du schéma absents de la carte
  std::vector<std::string> ecarts;         // pastilles dont le réseau de la carte n'est pas celui du schéma
};
// « schema » garde ses feuilles et ses composants ; la carte apporte son contour, son empilage, ses règles, ses
// empreintes posées (reliées par le repère) et son cuivre, dont les réseaux prennent les noms du schéma
RapportLiaison relierCarteAuSchema(ConceptionElec& schema, const ConceptionElec& carte);

// ---------- Import dans la carte ouverte ----------
struct OptionsFusion {
  bool decaler = true;               // posée à droite de la carte ouverte, sans chevauchement
  Pt decalage;                       // sinon ce décalage
  bool reunirHomonymes = true;       // réseaux de même nom réunis ; faux : ceux de la carte ajoutée sont suffixés
  bool feuilles = true;              // feuilles du schéma ajoutées à celles de la conception
};
struct RapportFusion {
  std::vector<std::pair<std::string, std::string>> renumerotes;  // ancien repère → nouveau
  std::vector<std::string> homonymes;                            // réseaux présents des deux côtés
  int composants = 0, pistes = 0, vias = 0, zones = 0, feuilles = 0;
  int cuivresAvant = 0, cuivresApres = 0;
  Pt decalage;
};
// Ce que la fusion ferait, sans rien changer : repères renumérotés et réseaux homonymes, à montrer avant
RapportFusion apercuFusion(const ConceptionElec& ouverte, const ConceptionElec& ajoutee, const OptionsFusion& o = {});
RapportFusion fusionnerConception(ConceptionElec& ouverte, ConceptionElec ajoutee, const OptionsFusion& o = {});

}  // namespace prom::ecad

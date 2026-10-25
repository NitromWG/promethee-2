// Prométhée : jeux de sorties (lot L13 ; 13.2, 13.7 ; FAB-10, FAB-11). Un jeu nomme ses sorties, son dossier (avec
// variables), sa variante de montage, son archive zip ; il se lance d'un coup, depuis l'interface ou la ligne de
// commande. Avant d'écrire, il contrôle les articles et compare les empreintes figées du projet à la bibliothèque :
// un échec arrête la publication, rapport des différences joint. Tout s'écrit d'abord dans un dossier temporaire,
// livré en une fois. Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// Codes des sorties et leur libellé : « fabrication » (dossier complet), « gerber », « percages », « placement »,
// « nomenclature », « ipc356 », « ipc2581 », « netlist », « boardview », « specctra », « dxf », « visionneuse »,
// « cout », « variantes », « projet »
const std::vector<std::pair<std::string, std::string>>& sortiesConnues();

// Contrôles du jeu (13.7) : empreintes figées différentes de la bibliothèque, articles génériques ou en fin de vie ;
// rend les refus, « rapport » reçoit le détail des différences d'empreintes, « alertes » ce qui n'arrête pas
std::vector<std::string> controlerAvantSorties(const ConceptionElec& c, std::string* rapport = nullptr, std::vector<std::string>* alertes = nullptr);

// Dossier du jeu, variables remplacées : {projet}, {carte}, {revision}, {variante}, {date} et celles de « variables »
// (ligne de commande -D CLE=VALEUR) ; un nom sans variable connue reste tel quel
std::string dossierDuJeu(const JeuSorties& j, const ConceptionElec& c, const std::string& projet, const std::map<std::string, std::string>& variables = {});

struct ResultatJeu {
  bool publie = false;
  std::string dossier;                 // dossier livré (ou archive .zip)
  std::vector<std::string> fichiers;   // chemins relatifs écrits
  std::vector<std::string> refus;      // contrôles qui ont arrêté le jeu
  std::vector<std::string> journal;    // ce qui s'est passé, dans l'ordre
  std::string rapport;                 // différences d'empreintes (écrit aussi à côté du dossier en cas de refus)
};
// « base » : dossier où les chemins relatifs du jeu prennent racine (celui du projet)
ResultatJeu executerJeu(const ConceptionElec& c, const JeuSorties& j, const std::string& base, const std::string& projet, const std::string& version,
                        const std::map<std::string, std::string>& variables = {});

// Jeu « Documentation » (13.6) : rendu PNG des deux faces, statistiques JSON, schéma en PDF, nomenclature ; il
// tourne sur une machine sans écran
JeuSorties jeuDocumentation();
// Jeu « Dossier JLCPCB » prêt à l'emploi : Gerber, perçages, placement et nomenclature aux noms attendus, en zip
JeuSorties jeuJlcpcb();

}  // namespace prom::ecad

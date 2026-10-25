// Prométhée : modèles de simulation (lot L11 de la 0.34 ; 11.11, 11.12 ; SIM-03, SIM-31, SIM-32, SIP-28).
// Fiches de modèles intégrés à champs bornés, comme les calculateurs, qui écrivent la ligne .model, l'élément ou le
// bloc XSPICE voulu ; ajustement des paramètres d'une diode et d'un MOSFET sur les points relevés dans la fiche
// technique (Levenberg-Marquardt de moindres_carres.hpp), modèles d'interrupteur et de transformateur ; rattachement
// du modèle à l'article. Fichiers IBIS lus (composant, broches, modèles, tables, rampes, formes d'onde) et changés en
// modèle comportemental de tampon pour ngspice, d'après la méthode du lecteur IBIS de KiCad (GPL) prise pour
// référence, sans en reprendre le code. Modules Verilog-A compilés par OpenVAF en OSDI, chargés par ngspice.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "promethee/electrique_calculateurs.hpp"
#include "promethee/schema.hpp"

namespace prom::ecad {

// ---------- Fiches de modèles intégrés (11.11) ----------
struct FicheModele {
  std::string id, nom, genre, description;  // genre : celui du ModeleSpice écrit (« diode », « mos », « sous-circuit »…)
  std::vector<prom::elec::ChampCalcul> champs;  // bornés ; un choix est un indice
  // Broches de l'élément, dans l'ordre de ses nœuds (« A K », « C B E », « D G S »)
  std::string broches;
};
const std::vector<FicheModele>& fichesModeles();
const FicheModele* ficheModele(const std::string& id);
// Texte du modèle (« nom » : celui du modèle ou du sous-circuit) ; une valeur hors bornes est refusée avec sa raison
std::optional<std::string> ecrireModele(const FicheModele& f, const std::string& nom, const std::vector<double>& valeurs, std::string* refus = nullptr);

// ---------- Modèles tirés de la fiche technique (11.11, SIM-32) ----------
struct PointFiche {
  double x = 0, y = 0;  // diode : courant direct (A), tension directe (V)
};
struct AjustementModele {
  bool reussi = false;
  std::string erreur;
  std::string nom, texte, genre, broches;  // texte : .model ou .subckt prêt pour la netlist
  std::map<std::string, double> parametres;
  std::vector<double> ecarts;  // écart relatif de chaque point
  double ecartMax = 0;
  std::string source;          // « fiche … : 3 points »
};
// Diode : IS, N, RS ajustés sur les points (courant, tension) relevés (au moins deux) ; CJO, TT, BV facultatifs
AjustementModele ajusterDiode(const std::vector<PointFiche>& points, const std::string& nom, double temperature = 27, double cjo = 0, double tt = 0, double bv = 0);
// MOSFET de puissance (VDMOS de ngspice) : VTO et KP ajustés sur le seuil (vth à idSeuil) et la résistance passante
// (rdsOn à vgsOn) ; capacités tirées des charges de grille (qg à vgsQg, qgd sous vdsQ)
AjustementModele ajusterVdmos(double vth, double idSeuil, double rdsOn, double vgsOn, double qg, double vgsQg, double qgd, double vdsQ, bool canalP, const std::string& nom);
AjustementModele modeleInterrupteur(double ron, double roff, double seuil, double hysteresis, const std::string& nom);
// Transformateur en inductances couplées : k = √(1 − Lfuite / Lmagnétisante), L2 = L1 × rapport²
AjustementModele modeleTransformateur(double lPrimaire, double rapport, double lFuite, const std::string& nom);
// Le modèle devient celui de l'article, avec sa source
void rattacherModele(Article& a, const AjustementModele& m);

// ---------- IBIS (11.12) ----------
struct TableIbis {
  std::vector<double> x;  // tension (V) ou temps (s)
  std::vector<double> typ, min, max;
  const std::vector<double>& coin(int c) const { return c == 1 ? min : c == 2 ? max : typ; }
};
struct FormeOndeIbis {
  double rFixture = 50, vFixture = 0, cFixture = 0, lFixture = 0;
  TableIbis table;
};
struct ModeleIbis {
  std::string nom, genre;  // genre : Output, I/O, Input, 3-state, Open_drain…
  bool inverse = false;    // Polarity Inverting
  double cComp[3] = {0, 0, 0};
  double tension[3] = {0, 0, 0};  // [Voltage Range] typ, min, max
  TableIbis pullup, pulldown, masse, alimentation;
  double rampeMontee[3] = {0, 0, 0}, rampeDescente[3] = {0, 0, 0};  // V/s
  double rLoad = 50;
  std::vector<FormeOndeIbis> montees, descentes;
  double vinl = 0.8, vinh = 2.0;
};
struct BrocheIbis {
  std::string numero, signal, modele;
  double r = 0, l = 0, c = 0;
};
struct FichierIbis {
  std::string version, composant, fabricant;
  double rPkg[3] = {0, 0, 0}, lPkg[3] = {0, 0, 0}, cPkg[3] = {0, 0, 0};
  std::vector<BrocheIbis> broches;
  std::vector<ModeleIbis> modeles;
  std::map<std::string, std::vector<std::string>> selecteurs;  // [Model Selector] nom → modèles
  std::vector<std::string> alertes;
  const ModeleIbis* modele(const std::string& nom) const;
};
std::optional<FichierIbis> lireIbis(const std::string& texte, std::string* erreur = nullptr);
struct OptionsTamponIbis {
  std::string modele;          // nom du [Model], ou numéro de la broche qui le désigne
  int coin = 0;                // 0 typique, 1 minimal, 2 maximal
  bool boitier = false;        // R, L, C du boîtier (ou de la broche) entre la puce et la broche
  bool differentiel = false;   // deux tampons opposés, sorties « outp » et « outn »
  // Stimulus, comme dans le lecteur IBIS de KiCad : instants des fronts, alternés, le premier montant
  std::vector<double> fronts{1e-9};
};
// Sous-circuit « nom » à nœud « out » (« outp outn » en différentiel) : sortie tirée par les tables I-V en sources B,
// pondérées dans le temps par les coefficients de tirage tirés de la première forme d'onde montante et descendante
// (de la rampe à défaut)
AjustementModele tamponIbis(const FichierIbis& f, const OptionsTamponIbis& o, const std::string& nom);

// ---------- Verilog-A (11.12, SIM-03) ----------
struct ModuleVerilogA {
  std::string nom;
  std::vector<std::string> ports;
  std::map<std::string, std::string> parametres;  // valeurs par défaut
};
std::optional<ModuleVerilogA> lireVerilogA(const std::string& source, std::string* erreur = nullptr);
// OpenVAF sur le poste (le programme « openvaf » dans le PATH) ; sans lui, rend la raison
bool openvafDisponible(std::string* raison = nullptr);
// Ligne de commande qui compile « source » en « osdi » (lancée par l'interface, à la demande)
std::vector<std::string> commandeOpenvaf(const std::string& source, const std::string& osdi);
// Modèle d'article d'un module compilé : élément N, modèle « <module>_va », bibliothèque OSDI à charger
ModeleSpice modeleVerilogA(const ModuleVerilogA& m, const std::string& source, const std::string& osdi);

}  // namespace prom::ecad

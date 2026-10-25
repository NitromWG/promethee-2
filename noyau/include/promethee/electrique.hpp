// Prométhée : CAO électrique (armoires, machines, bâtiment). Un projet électrique est un schéma en folios (même
// moteur que le schéma électronique : symboles, fils, étiquettes, connectivité) enrichi de ce qu'attend un
// électricien : symboles normalisés CEI 60617, appareils repérés par leur lettre (-Q1, -KM1, -S2…), contacts
// rattachés à leur bobine avec renvois croisés (folio.colonne), bornes de borniers, numéros de fils, conducteurs
// (section, couleur), câbles, rapports (nomenclature, borniers, câbles, carnet de câblage, entrées/sorties), et
// vérifications propres au métier. Aucune dépendance à l'interface. Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/electronique.hpp"

namespace prom::elec {

using ecad::Nm;
using ecad::Pt;

// ---------- Réglages du projet ----------
// Repérage des appareils : lettres classiques (Q, KM, KA, F, S, H, M, X, W…) ou codes à deux lettres de la
// CEI 81346-2:2019 (QA, QB, KF, SF, PF, MA, XD, WG…). Le tiret (« -KM1 ») est le préfixe d'aspect « produit ».
enum class NormeRepere : std::uint8_t { Classique, Cei81346 };
// Numéros de fils : un numéro par potentiel (équipotentielle), séquentiel dans l'ordre de lecture des folios, ou
// préfixé par le folio (« 3.12 »)
enum class NumerotationFils : std::uint8_t { Sequentielle, ParFolio };
enum class RegimeNeutre : std::uint8_t { TT, TNS, TNC, IT };
// Canalisations : méthode de référence de pose (CEI 60364-5-52, annexe B) et isolant
enum class MethodePose : std::uint8_t { A1, A2, B1, B2, C, E, D1 };
enum class Isolant : std::uint8_t { PVC, PR };  // PR : polyéthylène réticulé (XLPE)
struct ReglagesElectriques {
  NormeRepere norme = NormeRepere::Classique;
  bool tiret = true;                      // « -KM1 » plutôt que « KM1 » (aspect produit, CEI 81346)
  NumerotationFils fils = NumerotationFils::Sequentielle;
  std::string format = "A3";              // format des nouveaux folios
  int colonnes = 10, lignes = 6;          // grille des folios (repères des renvois : folio.colonne)
  double tension = 400, tensionSimple = 230, frequence = 50;  // réseau d'alimentation
  RegimeNeutre regime = RegimeNeutre::TT;
  double tensionCommande = 24;            // circuits de commande (V)
  bool commandeContinue = true;           // 24 V continu (sinon alternatif)
  std::string societe, auteur, client, installation, emplacement;  // cartouche
};

// ---------- Folios ----------
// Garde, Borniers, Implantation, Nomenclature et Grafcet sont dessinés d'office (sommaire, plan des borniers,
// armoire, liste du matériel, grafcet du projet), tenus à jour à chaque modification ; les autres reçoivent le schéma
// Cablage (0.34, 19.10) : schéma de câblage d'un faisceau, dessiné d'office depuis le faisceau qu'il nomme
enum class TypeFolio : std::uint8_t { Garde, Puissance, Commande, Automate, Distribution, Unifilaire, Borniers, Implantation, Nomenclature, Grafcet, Libre, Cablage };
bool folioDessineDOffice(TypeFolio t);
const char* nomTypeFolio(TypeFolio t);
const char* codeTypeFolio(TypeFolio t);
TypeFolio typeFolioDepuis(const std::string& code);
struct InfosFolio {
  TypeFolio type = TypeFolio::Libre;
  std::string commentaire;
  std::string origine;  // folio tiré d'un calcul (« distribution », « photovoltaique ») : remplacé quand on le régénère
  std::string grafcet;  // folio Grafcet : UUID du grafcet qu'il dessine
  std::string faisceau;  // folio de câblage : UUID du faisceau qu'il dessine (vide : le premier)
};
// Cadre d'un folio électrique (CEI 61082) : bandeaux de repères (colonnes numérotées en haut et en bas, lignes
// lettrées à gauche et à droite), zone de dessin, cartouche sur toute la largeur en bas
struct CadreFolio {
  ecad::Boite feuille, dessin, cartouche;
  int colonnes = 10, lignes = 6;
  Nm largeurColonne = 0, hauteurLigne = 0;
};
CadreFolio cadreFolio(const std::string& format, int colonnes, int lignes);
struct PositionFolio {
  int folio = 0;    // 1, 2… (ordre des folios)
  int colonne = 0;  // 1…colonnes
  int ligne = 0;    // 0 : A…
  std::string texte() const;           // « 3.4 »
  std::string texteComplet() const;    // « 3.4B »
};

// ---------- Conducteurs et câbles ----------
// Couleurs codées selon la CEI 60757 : BK noir, BN brun, RD rouge, OG orange, YE jaune, GN vert, BU bleu, VT violet,
// GY gris, WH blanc, PK rose, TQ turquoise, GNYE vert-jaune (protection), DBU bleu foncé (commande en continu),
// LBU bleu clair (neutre)
const std::vector<std::pair<std::string, std::string>>& couleursConducteurs();  // code → nom
struct ProprietesConducteur {
  double section = 0;        // mm² ; 0 : celle par défaut de sa classe
  std::string couleur;       // code CEI 60757 ; vide : par défaut
  std::string type;          // « H07V-K », « H05V-K »…
};
struct ConducteurCable {
  std::string repere;        // « 1 », « BN », « GNYE »
  std::string fil;           // numéro de fil ou potentiel qu'il porte (vide : réserve)
};
struct Cable {
  std::string uuid, reference;          // « W1 »
  std::string designation = "H07RN-F";  // type de câble
  int conducteurs = 3;
  double section = 1.5;
  double longueur = 0;                  // m (0 : à mesurer)
  bool blinde = false;
  std::string de, vers;                 // repères des appareils aux extrémités (« -X1 », « -M1 »)
  std::vector<ConducteurCable> affectation;
};
// Repères des conducteurs d'un câble : couleurs normalisées (HD 308) ou numéros, conducteur de protection compris
std::vector<std::string> reperesConducteurs(int nombre, bool protection, bool numerotes);

// ---------- Armoire ----------
// Implantation sur plaque de fond : rails DIN (35 mm), goulottes, appareils posés à leur taille réelle
struct RailDin {
  Nm y = 0, x0 = 0, x1 = 0;   // axe du rail, dans le repère de la plaque (mm → nm), y vers le bas
};
struct Goulotte {
  ecad::Boite b;
};
struct Implantation {
  std::string composant;      // UUID de l'appareil
  Pt p;                       // coin haut gauche, repère de la plaque
  int rail = -1;              // rail porteur (−1 : fixé sur la plaque)
  bool porte = false;         // sur la porte (boutons, voyants)
};
struct Armoire {
  std::string uuid, nom = "Armoire";
  double largeur = 600, hauteur = 800, profondeur = 250;  // enveloppe (mm)
  double marge = 30;          // bord de plaque sans appareil
  std::vector<RailDin> rails;
  std::vector<Goulotte> goulottes;
  std::vector<Implantation> implantations;
  bool tableauModulaire = false;  // coffret de logement : rangées de 13 ou 18 modules
  int modulesParRangee = 13;
  // Bilan thermique (CEI/TR 60890) : pose de l'enveloppe (0 isolée et libre … 6 au milieu d'une rangée, contre un
  // mur, toit couvert), températures extérieure maximale et intérieure admise, charge moyenne des appareils
  int pose = 1;
  double temperatureExterieure = 35, temperatureAdmise = 45, charge = 0.8;
};

// ---------- Faisceau ----------
struct NoeudFaisceau {
  std::string uuid, nom;      // nœud de dérivation ou extrémité
  Pt p;                       // sur la planche (mm → nm)
  std::string composant;      // connecteur posé ici (UUID), vide pour une dérivation
};
struct BrancheFaisceau {
  std::string a, b;           // nœuds
  double longueur = 0;        // mm ; 0 : longueur dessinée
  std::string protection;     // gaine, ruban…
};
struct Faisceau {
  std::string uuid, nom = "Faisceau";
  std::vector<NoeudFaisceau> noeuds;
  std::vector<BrancheFaisceau> branches;
  double surlongueur = 50;    // mm par extrémité (dénudage, reprise)
};

// ---------- Logement (NF C 15-100) ----------
enum class Piece : std::uint8_t { Sejour, Chambre, Cuisine, SalleDeBain, WC, Entree, Couloir, Garage, Buanderie, Bureau, Exterieur, Autre };
const char* nomPiece(Piece p);
struct PieceLogement {
  Piece type = Piece::Chambre;
  std::string nom;
  double surface = 12;   // m²
  int prises = 0;        // 0 : le minimum de la norme
  int pointsLumineux = 1;
  bool voletRoulant = false;
  double chauffage = 0;  // W de chauffage électrique (0 : aucun)
};
struct Logement {
  std::string nom = "Logement";
  double surface = 70;
  bool triphase = false;
  double abonnementKVA = 9;
  std::vector<PieceLogement> pieces;
  // Équipements à circuit spécialisé
  bool plaqueCuisson = true, four = true, laveVaisselle = true, laveLinge = true, secheLinge = false, congelateur = false,
       chauffeEau = true, vmc = true, borneRecharge = false, climatisation = false;
  double puissanceChauffeEau = 3000, puissanceBorne = 7400, puissanceClim = 2500;
};

// ---------- Installation : distribution basse tension ----------
// Les données de l'installation (source, tableaux, départs, canalisations, protections) ; le calcul (courants,
// sections, chutes de tension, courts-circuits, protection des personnes, sélectivité) est dans
// promethee/electrique_distribution.hpp. Une valeur à 0 est calculée ; une valeur donnée est imposée et vérifiée.
enum class TypeSourceBT : std::uint8_t {
  BranchementLimite,  // réseau public, branchement à puissance limitée (≤ 36 kVA) : Icc fourni par le distributeur
  ReseauPublic,       // réseau public, branchement > 36 kVA : réseau conventionnel de la NF C 14-100
  Transformateur,     // poste de livraison HTA/BT privé
  Groupe              // groupe électrogène
};
enum class Ame : std::uint8_t { Cuivre, Aluminium };
enum class TypeCharge : std::uint8_t { Tableau, Eclairage, Prises, Chauffage, Moteur, IRVE, Condensateurs, Autre };
enum class Polarite : std::uint8_t { TriphaseNeutre, Triphase, Monophase };
// Auto : modulaire jusqu'à 63 A, disjoncteur moteur pour un moteur jusqu'à 65 A, réglable au-delà
enum class TypeProtection : std::uint8_t { Auto, Modulaire, Reglable, Fusible, Moteur };
struct SourceBT {
  TypeSourceBT type = TypeSourceBT::Transformateur;
  double puissance = 630;       // kVA : transformateur, groupe, puissance souscrite ou de raccordement
  double ucc = 4;               // % : tension de court-circuit du transformateur
  double pertesCuivre = 0;      // W : pertes dues à la charge (0 : RT = 0,31 ZT, UTE C 15-105)
  double sccAmont = 500;        // MVA : puissance de court-circuit du réseau HTA
  double icc = 6;               // kA : courant de court-circuit au point de livraison (branchement ≤ 36 kVA)
  bool triphase = true;         // branchement ≤ 36 kVA : triphasé ou monophasé
  double xdSub = 12, xdTrans = 25, x0 = 6;  // % : réactances du groupe (subtransitoire, transitoire, homopolaire)
  double tension = 400;         // V entre phases (tension simple U0 = U/√3)
  RegimeNeutre regime = RegimeNeutre::TNS;
  double resistanceTerre = 30;  // Ω : prise de terre des masses (TT)
  bool controleurIsolement = true;  // IT : contrôleur permanent d'isolement posé
  bool domestique = false;      // installation domestique : pouvoir de coupure de 3 kA admis
};
struct DepartBT {
  std::string uuid;
  std::string parent;           // tableau amont (UUID) ; vide : alimenté par la source (tableau général)
  std::string repere = "Q1";    // protection du départ (Q…) ou tableau (TGBT, TD1…)
  std::string nom = "Départ";
  TypeCharge charge = TypeCharge::Autre;
  Polarite polarite = Polarite::TriphaseNeutre;
  int phase = 0;                // monophasé : phase raccordée (1, 2, 3), 0 : répartie
  // Charge
  double puissance = 5;         // kW (moteur : puissance utile ; condensateurs : kvar ; tableau : sans objet)
  double cosPhi = 0.85, rendement = 1, ku = 1;
  double ks = 0;                // tableau : simultanéité (0 : selon le nombre de départs)
  double courantImpose = 0;     // A : Ib imposé (0 : calculé)
  double tauxH3 = 0;            // % d'harmonique de rang 3 (dimensionne le neutre)
  double demarrage = 0;         // moteur : Id/In (0 : pas de vérification du démarrage)
  // Canalisation (du tableau amont au récepteur ou au tableau)
  double longueur = 20;         // m
  Ame ame = Ame::Cuivre;
  Isolant isolant = Isolant::PR;
  MethodePose pose = MethodePose::E;
  double temperature = 30;      // °C : air (pose enterrée : sol)
  int circuitsJointifs = 1;     // circuits groupés avec celui-ci (facteur de groupement)
  bool unipolaire = false;      // câbles unipolaires (en nappe) plutôt que multiconducteur
  int parallele = 0;            // câbles par phase (0 : selon le besoin)
  double sectionImposee = 0, sectionMini = 0;  // mm²
  bool neutreReduit = false;    // neutre à demi-section permis (au-delà de 16 mm², sans harmoniques)
  // Protection (dans le tableau amont)
  TypeProtection protection = TypeProtection::Auto;
  std::string courbe = "C";     // B, C, D (modulaire)
  double calibreImpose = 0;     // A
  double reglageIr = 0;         // A : réglage du déclencheur (réglable, moteur) ; 0 : au plus près de Ib
  double isd = 10;              // réglable : seuil court retard ou magnétique, en multiple de Ir
  bool retarde = false;         // réglable : court retard (sélectivité chronométrique)
  double pdcImpose = 0;         // kA
  double differentiel = 0;      // mA (0 : aucun)
  std::string typeDifferentiel = "A";  // AC, A, F, B
  bool differentielSelectif = false;   // type S (retardé)
};
struct ReseauBT {
  std::string nom = "Installation";
  SourceBT source;
  std::vector<DepartBT> departs;  // ordre d'affichage ; l'arbre se lit par les parents
  double cosPhiCible = 0.93;      // compensation de l'énergie réactive
  bool selectivite = true;        // protection d'un tableau d'au moins 1,6 fois le plus fort réglage de ses départs
  double chuteEclairage = 0, chuteAutres = 0;  // % maximum (0 : 3 et 5 % sur le réseau public, 6 et 8 % sur un poste privé)
};

// ---------- Photovoltaïque ----------
// Les données de l'installation photovoltaïque raccordée au réseau ; le calcul (calepinage, protections continues
// et alternatives, câbles, parafoudres, production) est dans promethee/electrique_pv.hpp
struct ModulePV {
  std::string nom = "Module 425 Wc";
  double puissance = 425;                                   // Wc
  double voc = 38.5, vmp = 32.2, isc = 13.9, imp = 13.2;   // conditions STC (V, A)
  double coefVoc = -0.27, coefIsc = 0.05;                   // %/°C
  double tensionMax = 1000;                                 // V : tension maximale du système
  double courantInverse = 25;                               // A : courant inverse admissible IRM
  double largeur = 1134, hauteur = 1722;                    // mm
};
struct OnduleurPV {
  std::string nom = "Onduleur monophasé 3,6 kVA";
  double puissanceAC = 3680;           // VA
  double puissanceDCMax = 5500;        // W
  double tensionDCMax = 600;           // V
  double mpptMin = 90, mpptMax = 520;  // V : plage de recherche du point de puissance maximale
  double courantMppt = 16;             // A : courant d'entrée maximal par MPPT
  double iscMppt = 20;                 // A : courant de court-circuit maximal par MPPT
  int mppt = 2, entreesParMppt = 1;
  bool triphase = false;
  double courantACMax = 0;             // A : courant de sortie maximal (notice) ; 0 : 1,1 fois le courant nominal
};
// Type de bâtiment, pour la longueur critique des parafoudres (guide UTE C 61-740-52)
enum class BatimentPV : std::uint8_t { Habitation, Tertiaire, CentraleSol };
struct InstallationPV {
  std::string nom = "Installation photovoltaïque";
  ModulePV module;
  OnduleurPV onduleur;
  int onduleurs = 1;
  int nombreModules = 10;              // modules posés
  int modulesParChaine = 0;            // 0 : calculé
  double tMin = -10, tMax = 70;        // °C : ambiante minimale du site, température maximale des cellules
  double temperatureCables = 70;       // °C : ambiante autour des câbles continus (sous les modules)
  double longueurDC = 20;              // m : de la chaîne la plus éloignée à l'onduleur (aller)
  double longueurAC = 15;              // m : de l'onduleur au tableau
  double sectionDC = 0, sectionAC = 0; // mm² (0 : calculée)
  double ng = 1.5;                     // densité de foudroiement (impacts/km²/an)
  BatimentPV batiment = BatimentPV::Habitation;
  bool paratonnerre = false;
  bool venteTotale = false;            // vente totale : disjoncteur de branchement et compteur propres ; sinon circuit dédié du tableau
  double productible = 1150;           // kWh par kWc et par an au site, plein sud incliné à 30°, pertes comprises
  double orientation = 0;              // ° : écart au sud (−90 est, +90 ouest)
  double inclinaison = 30;             // ° : 0 à plat, 90 en façade
};

// ---------- Grafcet (NF EN CEI 60848) ----------
// Étapes, transitions et actions ; les divergences et convergences en OU et en ET viennent du graphe lui-même (une
// transition a ses étapes amont et aval). Lecture, vérification, simulation, dessin : electrique_grafcet.hpp.
struct ActionGrafcet {
  enum class Type : std::uint8_t { Continue, Activation, Desactivation };
  Type type = Type::Continue;  // continue : vraie tant que l'étape est active ; mémorisée : affectée à l'activation ou à la désactivation
  std::string sortie;          // « KM1 », « Avance »
  bool valeur = true;          // action mémorisée : « := 1 » ou « := 0 »
  std::string condition;       // action conditionnelle (continue seulement) : « si dcy », « si 3s/X2 »
};
struct EtapeGrafcet {
  int numero = 0;
  bool initiale = false;
  std::vector<ActionGrafcet> actions;
  std::string commentaire;
};
struct TransitionGrafcet {
  std::vector<int> amont, aval;  // numéros d'étapes : plusieurs en amont, convergence en ET ; plusieurs en aval, divergence en ET
  std::string receptivite = "1";
  std::string commentaire;
};
struct Grafcet {
  std::string uuid, nom = "G1";
  std::vector<EtapeGrafcet> etapes;
  std::vector<TransitionGrafcet> transitions;
  const EtapeGrafcet* etape(int numero) const;
  EtapeGrafcet* etape(int numero);
  int numeroLibre() const;  // le plus petit numéro d'étape inutilisé
};

// ---------- Projet électrique ----------
// Révision du dossier : indice (A, B…), date, auteur, ce qui a changé ; la plus récente va au cartouche
struct RevisionProjet {
  std::string indice, date, auteur, description;
};
struct ProjetElectrique {
  std::string uuid, nom = "Projet électrique";
  ecad::ConceptionElec schema;                              // folios, appareils, symboles, fils
  ReglagesElectriques reglages;
  std::map<std::string, InfosFolio> folios;                 // UUID de feuille → infos
  std::map<std::string, ProprietesConducteur> conducteurs;  // numéro de fil ou potentiel → propriétés
  std::vector<Cable> cables;
  std::vector<Armoire> armoires;
  std::vector<Faisceau> faisceaux;
  std::optional<Logement> logement;                         // projet né de l'assistant logement
  std::optional<ReseauBT> distribution;                     // installation calculée (source, tableaux, départs)
  std::optional<InstallationPV> photovoltaique;             // installation photovoltaïque calculée
  std::map<std::string, double> prix;                       // prix unitaires saisis (€ HT), par ligne de nomenclature (sa clé)
  double tauxHoraire = 55;                                  // € HT de l'heure de main d'œuvre (chiffrage)
  std::vector<RevisionProjet> revisions;                    // de la première à la plus récente
  std::vector<Grafcet> grafcets;                            // grafcets du projet (onglet Grafcet, folios Grafcet)
  const InfosFolio& folio(const std::string& feuille) const;
};
// Nouvelle révision à l'indice suivant (la première reprend celui du cartouche, « A » par défaut) ; le cartouche
// prend son indice et sa date
const RevisionProjet& nouvelleRevision(ProjetElectrique& p, const std::string& date, const std::string& description);
std::string revisionCourante(const ProjetElectrique& p);
std::string indiceSuivant(const std::string& indice);  // A → B, Z → AA
ProjetElectrique nouveauProjetElectrique(const std::string& nom, bool foliosDeDepart = true);
// Ajoute un folio (rend son indice) ; « titre » vide : nom tiré du type
std::size_t ajouterFolio(ProjetElectrique& p, TypeFolio type, const std::string& titre = {}, std::size_t position = static_cast<std::size_t>(-1));
PositionFolio position(const ProjetElectrique& p, std::size_t feuille, Pt q);

// ---------- Bibliothèque CEI 60617 et catalogue ----------
// Rôle d'un symbole dans son appareil : le principal porte le repère (bobine d'un contacteur, corps d'un
// disjoncteur…) ; les contacts et pôles s'y rattachent et reçoivent leurs numéros de bornes
enum class RoleSymbole : std::uint8_t { Principal, ContactNO, ContactNF, ContactInverseur, ContactNOTempo, ContactNFTempo, PolesPuissance, ContactThermiqueNF,
                                        ContactThermiqueNO, Borne, Potentiel, Renvoi, Autre };
const std::vector<ecad::DefSymbole>& bibliothequeElectrique();
const ecad::DefSymbole* symboleElectrique(const std::string& id);
RoleSymbole roleSymbole(const std::string& id);
bool estSymboleElectrique(const std::string& id);
// Familles de la palette, dans l'ordre d'affichage
const std::vector<std::string>& famillesElectriques();

struct EntreeCatalogue {
  std::string id, famille, designation, fabricant = "Générique", reference;
  std::string symbole;            // symbole principal
  std::string prefixe;            // lettre de repère (classique) ; le code CEI 81346 se déduit
  int poles = 0;                  // pôles de puissance
  double calibre = 0;             // A
  std::string courbe;             // B, C, D
  double pouvoirCoupure = 0;      // kA
  double sensibilite = 0;         // mA (différentiel)
  std::string typeDifferentiel;   // AC, A, F, B
  double tensionBobine = 0;       // V
  bool bobineContinue = false;
  int contactsNO = 0, contactsNF = 0;  // contacts auxiliaires disponibles
  int inverseurs = 0;                  // contacts inverseurs (relais)
  double puissance = 0;           // kW (moteur, récepteur)
  double largeur = 0, hauteur = 0, profondeur = 0;  // mm (0 : selon les modules)
  int modules = 0;                // modules de 18 mm (appareils modulaires)
  bool rail = true;               // monté sur rail DIN (sinon plaque, porte ou hors armoire)
  bool porte = false;             // monté en façade (boutons, voyants)
  bool horsArmoire = false;       // moteur, capteur, récepteur sur la machine
  double prix = 0;                // € HT : prix public indicatif (à remplacer par ses tarifs dans la nomenclature)
};
const std::vector<EntreeCatalogue>& catalogueElectrique();
const EntreeCatalogue* entreeCatalogue(const std::string& id);
// Entrées du catalogue qui conviennent à un symbole (pour la proposer à la pose)
std::vector<const EntreeCatalogue*> entreesPourSymbole(const std::string& symbole);
// Lettre de repère d'un symbole selon la norme choisie
std::string prefixeRepere(const std::string& symbole, NormeRepere norme);
// Dimensions d'un appareil dans l'armoire (catalogue, sinon modules, sinon gabarit de son symbole), en mm
struct Encombrement {
  double largeur = 0, hauteur = 0, profondeur = 0;
  bool rail = true, porte = false, horsArmoire = false;
};
Encombrement encombrement(const ProjetElectrique& p, const std::string& composant);

// ---------- Appareils personnels ----------
// Appareil absent de la bibliothèque (relais de sécurité, régulateur, contrôleur, module d'automate, appareil de la
// maison) : un nom, sa lettre de repère, ses bornes côté par côté. Le symbole se dessine d'office : cadre de la
// CEI 60617 avec son marquage, bornes au pas de 5,08 mm, toutes sur la grille. Il se range dans le projet (l'atelier
// le garde aussi dans la bibliothèque personnelle) ; ses dimensions et son montage servent à l'implantation de
// l'armoire et au chiffrage, ses pertes au bilan thermique, son prix à la nomenclature. Il porte des contacts posés
// ailleurs, comme une bobine.
enum class MontageAppareil : std::uint8_t { Rail, Plaque, Porte, HorsArmoire };
struct SpecAppareil {
  std::string id;  // vide : tiré du nom (« perso:Relais-de-securite »)
  std::string nom, prefixe = "A", marquage, description, fabricant, reference;
  std::vector<std::string> haut, bas, gauche, droite;  // numéros des bornes, dans l'ordre de lecture
  double largeur = 0, hauteur = 0, profondeur = 0;     // mm ; 0 : gabarit d'office
  MontageAppareil montage = MontageAppareil::Rail;
  double pertes = -1;  // W dissipés en service ; < 0 : inconnus
  double prix = 0;     // € HT indicatif ; 0 : à saisir dans la nomenclature
};
const char* nomMontage(MontageAppareil m);  // « Sur rail DIN »…
bool estAppareilPersonnel(const std::string& symbole);
// « A1 A2, 13 ; 14 » → A1, A2, 13, 14 (espaces, virgules et points-virgules séparent)
std::vector<std::string> lireBornes(const std::string& texte);
std::string texteBornes(const std::vector<std::string>& bornes);
// Ce qui empêche de dessiner l'appareil (nom vide, aucune borne, borne en double, lettre de repère invalide)
std::vector<std::string> defautsAppareil(const SpecAppareil& a);
ecad::DefSymbole symboleAppareil(const SpecAppareil& a);
SpecAppareil specAppareil(const ecad::DefSymbole& d);  // pour le modifier
// Identifiant tiré du nom, libre dans le projet et parmi « autresPris » (bibliothèque personnelle) :
// « perso:Relais-de-securite », puis « …-2 »
std::string identifiantAppareil(const ProjetElectrique& p, const std::string& nom, const std::vector<std::string>& autresPris = {});
// Range la définition dans le projet et rend son identifiant. Même identifiant : la définition est remplacée (version
// suivante) et les appareils posés suivent ; leurs fils restent où ils étaient.
std::string enregistrerAppareil(ProjetElectrique& p, const SpecAppareil& a);
// Appareils personnels du projet, triés par nom
std::vector<const ecad::DefSymbole*> appareilsPersonnels(const ProjetElectrique& p);

// ---------- Pose ----------
// Pose le symbole principal d'un nouvel appareil, repéré tout de suite (« KM3 »), avec l'entrée du catalogue
// (caractéristiques recopiées dans ses champs) ; rend l'UUID du symbole posé
std::string poserAppareil(ProjetElectrique& p, std::size_t feuille, const std::string& symbole, Pt position, int rotation = 0, const std::string& catalogue = {});
// Pose un contact, des pôles ou une borne rattachés à un appareil existant (UUID du composant), avec les numéros de
// bornes suivants libres (13-14, 23-24… ; 21-22, 31-32… ; 1-2, 3-4, 5-6 ; 95-96)
std::string poserElement(ProjetElectrique& p, std::size_t feuille, const std::string& symbole, Pt position, int rotation, const std::string& composant);
// Pose une borne de bornier (« -X1 ») : bornier existant (UUID) ou nouveau si vide ; numéro de borne suivant
std::string poserBorne(ProjetElectrique& p, std::size_t feuille, Pt position, int rotation, const std::string& bornier, bool protection = false);
// Pose un potentiel (L1, L2, L3, N, PE, 24V, 0V…) ou un renvoi de folio : étiquette globale à rendu électrique
std::string poserPotentiel(ProjetElectrique& p, std::size_t feuille, const std::string& nom, Pt position, int rotation = 0);
// Référence du catalogue donnée à un appareil : désignation et caractéristiques recopiées (vide : aucune)
bool choisirReference(ProjetElectrique& p, const std::string& composant, const std::string& catalogue);
// Rattache un contact (pose « symbole ») à un autre appareil, avec les numéros de bornes suivants libres ; une
// borne change de bornier et prend le numéro suivant. Faux si l'élément ou l'appareil n'existe pas.
bool rattacher(ProjetElectrique& p, const std::string& symbole, const std::string& composant);
// Supprime un appareil : toutes ses poses, son implantation dans les armoires, ses nœuds de faisceau
void supprimerAppareil(ProjetElectrique& p, const std::string& composant);
// Supprime un folio et ce qu'il porte ; les appareils qui n'ont plus aucune pose disparaissent avec lui
bool supprimerFolio(ProjetElectrique& p, std::size_t folio);
// Numéros de bornes suivants pour un contact de ce rôle sur cet appareil
std::map<std::string, std::string> numerosSuivants(const ProjetElectrique& p, const std::string& composant, RoleSymbole role, const std::string& symbole);
// Appareils auxquels un élément de ce rôle peut se rattacher (bobines pour un contact, borniers pour une borne…)
std::vector<std::string> appareilsPour(const ProjetElectrique& p, RoleSymbole role);
std::string repereAffiche(const ProjetElectrique& p, const ecad::ComposantElec& c);  // « -KM1 » ou « KM1 »
// Caractéristiques courtes d'un appareil, pour le schéma : « 16 A C », « 40 A 30 mA type A », « 2,2 kW »,
// « 24 V continu » ; vide si rien n'est renseigné
std::string caracteristiques(const ecad::ComposantElec& c);

// ---------- Repérage, renvois, fils ----------
// Annotation électrique : chaque lettre numérotée dans l'ordre de lecture (folio, colonne, ligne) ; « tout » :
// renumérote aussi les appareils déjà repérés. Les bornes gardent leur bornier.
int annoterElectrique(ProjetElectrique& p, bool tout);
struct ElementRenvoi {
  std::string symbole;       // UUID du symbole posé
  std::string definition;    // identifiant de son symbole (« iec:contact-no »)
  std::size_t feuille = 0;
  PositionFolio ou;
  RoleSymbole role = RoleSymbole::Autre;
  std::string bornes;        // « 13-14 »
};
struct Renvois {
  std::map<std::string, std::vector<ElementRenvoi>> parComposant;   // UUID d'appareil → ses éléments posés
  std::map<std::string, ElementRenvoi> parSymbole;                   // UUID de symbole → sa position
  std::map<std::string, std::vector<ElementRenvoi>> parPotentiel;    // nom → étiquettes globales posées (renvois de folio)
};
Renvois renvois(const ProjetElectrique& p);
// Numérotation des fils : chaque potentiel sans nom reçoit un numéro, figé (il ne bouge plus quand on ajoute un
// fil) ; « tout » : renumérote dans l'ordre de lecture. Rend le nombre de potentiels numérotés.
int numeroterFils(ProjetElectrique& p, bool tout);
// Propriétés effectives d'un conducteur (réglages du potentiel, sinon par défaut selon sa classe)
enum class ClasseConducteur : std::uint8_t { Phase, Neutre, Protection, Commande, CommandeContinue, ZeroVolt, Autre };
ClasseConducteur classeConducteur(const std::string& nom);
ProprietesConducteur proprietesEffectives(const ProjetElectrique& p, const std::string& fil);

// ---------- Rapports ----------
struct LigneAppareil {
  std::string uuid, repere, designation, fabricant, reference, fonction, emplacement, folios;
  int quantite = 1;
};
std::vector<LigneAppareil> listeAppareils(const ProjetElectrique& p);           // un appareil par ligne
struct LigneNomenclatureElec {
  int quantite = 0;
  std::vector<std::string> reperes;
  std::string designation, fabricant, reference;
  std::string cle;            // clé du prix saisi (référence, sinon désignation)
  double prixUnitaire = 0;    // € HT : saisi dans le projet, sinon indicatif du catalogue (0 : inconnu)
  bool prixSaisi = false;
  double montant() const { return prixUnitaire * quantite; }
};
std::vector<LigneNomenclatureElec> nomenclatureElectrique(const ProjetElectrique& p);  // regroupée par référence
// Clé sous laquelle le projet garde le prix saisi d'une ligne : la référence du fabricant si elle existe, sinon la
// désignation (« ref:Fabricant|Référence », « des:Désignation »)
std::string clePrix(const std::string& designation, const std::string& fabricant, const std::string& reference);
// Saisit le prix unitaire HT d'une ligne (prix ≥ 0) ou le retire (prix < 0 : retour au prix indicatif du catalogue)
void fixerPrix(ProjetElectrique& p, const std::string& cle, double prix);
struct ChiffrageNomenclature {
  double total = 0;            // € HT, somme des montants connus
  int lignes = 0, saisies = 0, indicatives = 0, sansPrix = 0;
};
ChiffrageNomenclature chiffrage(const std::vector<LigneNomenclatureElec>& v);
struct LigneBorne {
  std::string bornier, borne, interieur, exterieur;  // repères de ce qui arrive de chaque côté
  std::string fil, cable, folio;
  bool protection = false;
};
std::vector<LigneBorne> planBorniers(const ProjetElectrique& p);
struct LigneLiaison {
  std::string fil, de, vers, section, couleur, folio;  // de « -Q1:2 » vers « -KM1:1 »
  double longueur = 0;  // mm, estimée d'après l'implantation des deux appareils dans l'armoire (0 : inconnue)
  std::string type;     // « H07V-K »
  double mm2 = 0;       // section en nombre
  std::string composantDe, composantVers;  // UUID des appareils
};
std::vector<LigneLiaison> carnetCablage(const ProjetElectrique& p);
struct LigneEntreeSortie {
  std::string automate, adresse, sens, repere, designation, folio;  // sens : « E » ou « S »
};
std::vector<LigneEntreeSortie> listeEntreesSorties(const ProjetElectrique& p);
struct LigneFolio {
  int numero = 0;
  std::string titre, type;
  int appareils = 0, fils = 0;
};
std::vector<LigneFolio> listeFolios(const ProjetElectrique& p);
// Exports tabulaires (CSV au point-virgule, UTF-8 avec BOM, lisibles par un tableur français)
std::string csvAppareils(const ProjetElectrique& p);
std::string csvNomenclatureElectrique(const ProjetElectrique& p);
std::string csvBorniers(const ProjetElectrique& p);
std::string csvCables(const ProjetElectrique& p);
std::string csvCarnetCablage(const ProjetElectrique& p);
std::string csvEntreesSorties(const ProjetElectrique& p);
std::string csvFolios(const ProjetElectrique& p);
std::string csvBilanPuissance(const ProjetElectrique& p, double cosPhi = 0.85);
// Affecte les conducteurs d'un câble aux potentiels qui relient ses deux extrémités (« de » et « vers », repères
// d'appareils ou de borniers) : le PE au vert-jaune, les autres dans l'ordre. Rend le nombre de conducteurs affectés.
int affecterCable(ProjetElectrique& p, std::size_t cable);
// Repères des conducteurs d'un câble de « n » conducteurs, réaffectés en gardant les potentiels déjà donnés
void dimensionnerCable(Cable& c, int conducteurs, bool protection);

// ---------- Vérifications ----------
// ERC du schéma (fils, broches libres, courts-circuits entre potentiels) et règles du métier : appareil sans
// repère ou en double, contact sans bobine, plus de contacts que l'appareil n'en a, bornes en double, borne hors
// bornier, potentiel ou renvoi posé une seule fois, conducteur de protection d'une autre couleur que vert-jaune,
// section trop faible pour la protection, câble dont un conducteur porte deux potentiels, adresse d'automate en
// double, folio sans titre…
std::vector<ecad::Violation> verifierElectrique(const ProjetElectrique& p);

// ---------- Calculs (CEI 60364-5-52, NF C 15-100) ----------
const char* nomMethodePose(MethodePose m);
// Courant admissible (A) d'un conducteur en cuivre, conditions de référence (30 °C) puis facteurs de correction
// de température et de groupement ; 0 si la section n'est pas dans les tableaux
double courantAdmissible(double section, MethodePose m, Isolant i, int conducteursCharges, double temperature = 30, int circuitsGroupes = 1);
double facteurTemperature(Isolant i, double temperature);
double facteurGroupement(int circuits);
const std::vector<double>& sectionsNormalisees();  // 1,5 … 240 mm²
struct ChuteTension {
  double volts = 0, pourcent = 0;
  bool admissible = true;  // ≤ 3 % éclairage, ≤ 5 % autres usages (réseau public BT)
};
// Chute de tension d'un circuit en cuivre : ΔU = b (ρ L / S cos φ + λ L sin φ) Ib, b = 2 en monophasé, 1 en
// triphasé (rapportée à la tension simple), ρ = 0,0225 Ω·mm²/m (annexe G de la NF C 15-100), λ = 0,08 mΩ/m
ChuteTension chuteTension(double courant, double longueur, double section, bool triphase, double cosPhi, double tension, bool eclairage);
struct ChoixSection {
  double section = 0, courantAdmissible = 0, calibre = 0;
  ChuteTension chute;
  std::string justification;
};
// Plus petite section normalisée qui convient : calibre normalisé In ≥ Ib, Iz ≥ In, chute de tension admissible
ChoixSection choisirSection(double courantEmploi, double longueur, bool triphase, double cosPhi, MethodePose m, Isolant i, double temperature, int circuitsGroupes,
                            bool eclairage, double tension);
const std::vector<double>& calibresNormalises();  // 2, 6, 10, 16, 20, 25, 32, 40, 50, 63…
struct LigneBilan {
  std::string repere, designation;
  double puissance = 0, ku = 1, ks = 1;  // kW, utilisation, simultanéité
  double absorbee() const { return puissance * ku * ks; }
};
struct BilanPuissance {
  std::vector<LigneBilan> lignes;
  double installee = 0, absorbee = 0, courant = 0;  // kW, kW, A (sous la tension du réseau)
};
BilanPuissance bilanPuissance(const ProjetElectrique& p, double cosPhi = 0.85);

// ---------- Logement (NF C 15-100, édition 2024 et guides) ----------
// Règles appliquées, avec leur portée (« exigence », « recommandation ») ; elles ne remplacent ni le texte de la
// norme ni la visite du Consuel
struct CircuitLogement {
  std::string nom, usage;      // « Prises séjour », « prises »
  double calibre = 16;         // A
  double section = 1.5;        // mm²
  int points = 0;              // prises ou points lumineux
  bool differentielTypeA = false;
  double puissance = 0;        // W
  int differentiel = 0;        // rang de l'interrupteur différentiel (0, 1…)
  bool specialise = false;
};
struct TableauLogement {
  std::vector<CircuitLogement> circuits;
  struct Differentiel {
    double calibre = 40;
    std::string type = "AC";   // AC, A, F
  };
  std::vector<Differentiel> differentiels;
  int modulesOccupes = 0, modulesTotal = 0, rangees = 0;
  std::vector<std::string> remarques;
};
TableauLogement tableauLogement(const Logement& l);
struct AvisNorme {
  bool exigence = true;  // sinon recommandation
  std::string regle, message;
};
std::vector<AvisNorme> verifierLogement(const Logement& l, const TableauLogement& t);
// Projet électrique complet du logement : page de garde (règles appliquées), unifilaire du tableau (disjoncteur
// de branchement, différentiels, départs repérés avec section et calibre), coffret modulaire implanté (réserve
// d'au moins 20 %), nomenclature
ProjetElectrique projetLogement(const Logement& l);
Logement logementType(double surface, int chambres);  // logement courant prêt à ajuster

// ---------- Armoire et faisceau ----------
// Implantation automatique des appareils de l'armoire sur ses rails (créés au besoin), par famille : protection,
// commande, automate, alimentation, borniers en bas ; rend le nombre d'appareils posés
int implanterArmoire(ProjetElectrique& p, std::size_t armoire);
// Implante seulement les appareils qui ne le sont pas encore, dans le premier emplacement libre des rails (ou de
// la porte), sans bouger ceux déjà placés ; rend le nombre d'appareils ajoutés
int implanterManquants(ProjetElectrique& p, std::size_t armoire);
// Rails, goulottes et rangées par défaut d'une armoire vide (plaque de fond ou coffret modulaire)
void equiperArmoire(Armoire& a);
// Appareils implantés, à leur taille (bornier : largeur de toutes ses bornes et de ses butées), en nm dans le
// repère de la plaque (ou de la porte)
struct BoiteAppareil {
  std::string composant;
  ecad::Boite b;
  bool porte = false;
  int rail = -1;
};
std::vector<BoiteAppareil> boitesArmoire(const ProjetElectrique& p, std::size_t armoire);
// Appareils qui vont dans l'armoire (ni sur la machine, ni simple dessin), dans l'ordre d'implantation
std::vector<std::string> appareilsArmoire(const ProjetElectrique& p, const Armoire& a);
struct BilanArmoire {
  double tauxRemplissage = 0;   // part de la longueur de rail occupée
  double reserve = 0;           // part libre (≥ 20 % conseillé)
  int horsPlaque = 0;           // appareils qui débordent ou se chevauchent
  std::vector<std::string> remarques;
};
BilanArmoire bilanArmoire(const ProjetElectrique& p, std::size_t armoire);
std::string dxfPlaque(const ProjetElectrique& p, std::size_t armoire);  // plan de perçage de la plaque (DXF R12)
struct FilFaisceau {
  std::string fil, de, vers, couleur;
  double section = 0, longueur = 0;  // mm
  std::vector<std::string> branches;
};
std::vector<FilFaisceau> filsFaisceau(const ProjetElectrique& p, std::size_t faisceau);
double longueurBranche(const Faisceau& f, const BrancheFaisceau& b);  // mm

// ---------- Exemples ----------
ProjetElectrique exempleDemarrageMoteur();  // démarrage direct d'un moteur triphasé, marche/arrêt, voyants

// ---------- Circuits types ----------
// Schémas courants posés en un geste, à droite de ce qui est déjà dessiné (sur un folio neuf s'il n'y a plus de
// place), reliés au reste du projet par leurs potentiels (L1, L2, L3, N, PE, 24V, 0V) ; appareils repérés à la suite
// avec leur référence du catalogue, fils numérotés, sections réglées selon le calibre de la protection
enum class CircuitType : std::uint8_t { DemarrageDirect, InverseurSens, AlimentationCommande, Telerupteur, VaEtVient, Prises };
struct InfosCircuitType {
  CircuitType type;
  std::string nom, description;
  bool batiment = false;  // circuit du bâtiment (folio de distribution) plutôt que d'armoire
};
const std::vector<InfosCircuitType>& circuitsTypes();
int insererCircuitType(ProjetElectrique& p, CircuitType t);  // indice du folio à montrer

// ---------- Import QElectroTech ----------
// Lit un projet QElectroTech (.qet) : folios, éléments (définitions embarquées converties en symboles), bornes,
// conducteurs (avec leurs numéros), textes ; les maîtres et esclaves (bobine et contacts) deviennent un seul
// appareil. « alertes » décrit ce qui n'a pas pu être repris. Lève std::runtime_error si le fichier est illisible.
ProjetElectrique importerQet(const std::string& contenu, std::vector<std::string>* alertes = nullptr);

// ---------- Fichier ----------
nlohmann::ordered_json versJson(const ProjetElectrique& p);
ProjetElectrique projetElectriqueDepuisJson(const nlohmann::ordered_json& j, std::vector<std::string>* alertes = nullptr);

}  // namespace prom::elec

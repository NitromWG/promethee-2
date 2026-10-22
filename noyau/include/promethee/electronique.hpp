// Prométhée : conception électronique. Un seul modèle pour le schéma et le circuit imprimé : les composants (UUID)
// sont partagés, la connectivité est calculée une fois (graphe : broche = nœud, connexion = arête, réseau = partie
// connexe) et sert à tout : vérification électrique, circuit imprimé, chevelu, DRC, nomenclature, fabrication.
// Moteurs : connectivité, ERC, annotation, mise à jour du circuit, zones de cuivre, état du routage, DRC, modèle de
// fabrication indépendant des formats, puis Gerber X2, Excellon, placement et nomenclature. Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/circuit.hpp"
#include "promethee/schema.hpp"

namespace prom::ecad {

// Publication pour la fabrication (« release ») : identifiant, révision, empreinte de la conception publiée et,
// si elle a été forcée malgré des erreurs, la raison donnée et les erreurs acceptées (journal d'audit).
struct Publication {
  std::string id;          // R001, R002…
  std::string revision;    // révision du cartouche
  std::string date, auteur;
  std::string empreinte;   // SHA-256 de la conception publiée (16 chiffres FNV-1a avant la version 0.25)
  std::string derogation;  // vide : publiée sans erreur
  std::string dossier;     // nom du dossier écrit
  std::vector<std::string> violations;  // erreurs acceptées par dérogation : « code — étape : message »
  std::vector<std::pair<std::string, std::string>> fichiers;  // fichier du dossier → SHA-256
  friend bool operator==(const Publication&, const Publication&) = default;
};

// Vérification électrique réglée par projet (2.11, E5, ERC-02 à ERC-08 du catalogue) : gravité propre d'un code
// (« ignorer » compris), cases changées de la matrice des conflits de broches, exclusions une par une (code,
// objets, raison, auteur, date), rappelées à la publication
enum class GraviteReglee : std::uint8_t { Erreur, Avertissement, Ignorer };
struct ExclusionErc {
  std::string code;
  std::vector<std::string> objets;  // UUID des objets visés ; vide : toutes les violations de ce code
  std::string raison, auteur, date;
  friend bool operator==(const ExclusionErc&, const ExclusionErc&) = default;
};
struct ReglagesErc {
  std::map<std::string, GraviteReglee> gravites;
  // « sortie/collecteurOuvert » (codes des types, dans l'ordre de la matrice) → « erreur », « avertissement », « permis »
  std::map<std::string, std::string> matrice;
  std::vector<ExclusionErc> exclusions;
  bool vide() const { return gravites.empty() && matrice.empty() && exclusions.empty(); }
  friend bool operator==(const ReglagesErc&, const ReglagesErc&) = default;
};
struct ConceptionElec {
  std::string uuid, nom = "Carte électronique";
  std::vector<ComposantElec> composants;
  std::vector<Feuille> feuilles;
  Cartouche cartouche;
  Circuit circuit;
  std::map<std::string, DefSymbole> symboles;  // définitions utilisées, verrouillées dans le projet
  // Empreintes utilisées, figées dans le projet à la mise à jour du circuit (EL-35) : une évolution du générateur
  // ne change pas une carte existante ; « actualiserEmpreintes » reprend la version de la bibliothèque.
  std::map<std::string, DefEmpreinte> empreintes;
  // Articles utilisés, figés dans le projet comme les symboles et les empreintes (4.2) : une mise à jour homonyme de
  // la bibliothèque ne les change pas sans qu'on l'accepte
  std::map<std::string, Article> articles;
  // Noms automatiques figés (EL-01) : « UUID du composant:numéro de broche » → nom donné à son réseau quand le
  // circuit a été mis à jour. Renommer R1 en R5, ajouter un composant ou poser une étiquette ne change plus le
  // réseau que portent les pistes : l'ancien nom reste un alias du réseau.
  std::map<std::string, std::string> nomsReseaux;
  bool pilotageBoitier = false;                // le boîtier mécanique du projet suit cette carte
  // Schéma seul (STR-13) : schéma de principe ou documentation, sans circuit imprimé ; l'atelier ne propose le
  // circuit, la 3D et la fabrication qu'à la demande (« Créer le circuit imprimé »)
  bool schemaSeul = false;
  bool verrouillee = false;                    // publiée : plus aucune modification avant une nouvelle révision
  std::vector<Publication> publications;
  ReglagesErc erc;
  // Variables de la conception (3.14, SAI-44) : nom → formule (« Vcc » → « 5 V »), lues par les valeurs calculées
  // et les textes qui affichent un calcul
  std::map<std::string, std::string> variables;
  // Synchronisation du circuit (3.6) : automatique pour les changements sans risque (par défaut), ou tout écart
  // passé par le panneau des écarts
  bool synchroSurConfirmation = false;
  // Clés que ce logiciel ne connaît pas, relues puis réécrites telles quelles, objet par objet (ECH-11, A13) : un
  // projet ouvert puis enregistré par une version plus ancienne ne perd plus ce qu'une plus récente y a mis. Clé : le
  // genre et l'identité de l'objet (« composant:<uuid> », « circuit/regles »…), valeur : ses clés inconnues
  std::map<std::string, nlohmann::ordered_json> clesInconnues;
  const ComposantElec* composant(const std::string& uuid) const;
  ComposantElec* composant(const std::string& uuid);
  const ComposantElec* parReference(const std::string& ref) const;
  const DefSymbole* symbole(const std::string& id) const;  // copie du projet, sinon bibliothèque
  const DefEmpreinte* empreinte(const std::string& id) const;  // copie figée du projet, sinon générateur
};
ConceptionElec nouvelleConception(const std::string& nom);
// Exemple complet : clignotant à NE555 (schéma câblé, circuit placé et routé avec plan de masse, nomenclature)
ConceptionElec exempleElectronique();
// Carte de mesure (lot L1, tâche 1.1, AUT-12) : « composants » composants, arrondis à la dizaine, en blocs de dix (un
// NE555 en astable, six résistances, trois condensateurs) reliés par des étiquettes, dix blocs par feuille ; empreintes
// en grille et, si « zone », un plan de masse GND sur toute la face des composants ; non routée. Les temps de
// l'atelier se relèvent dessus à chaque construction (« promethee carte-mesure », banc mesures_electronique).
ConceptionElec carteDeMesure(int composants, bool zone = true);

// Le verrou d'une révision publiée est tenu par le noyau, pas seulement par l'interface (F4) : toute fonction qui
// modifie une conception refuse une conception verrouillée en levant ConceptionVerrouillee (code VER-01). La
// ligne de commande et les scripts ne peuvent donc pas altérer une révision publiée.
class ConceptionVerrouillee : public std::runtime_error {
 public:
  explicit ConceptionVerrouillee(const ConceptionElec& c);
  static constexpr const char* CODE = "VER-01";
};
void exigerModifiable(const ConceptionElec& c);  // lève ConceptionVerrouillee
// Pose un symbole de la bibliothèque : composant créé (référence « R? »), définition recopiée dans le projet.
std::string poserSymbole(ConceptionElec& c, std::size_t feuille, const std::string& idSymbole, Pt position, int rotation = 0);
// Symbole à plusieurs unités : pose une autre unité du composant du symbole « uuidSymbole » (même repère, U1B…),
// à côté de lui. « unite » 0 : la première unité qui n'est posée nulle part. Rend l'uuid posé, vide si toutes les
// unités sont déjà posées ou si le symbole n'a qu'une unité.
std::string poserUnite(ConceptionElec& c, std::size_t feuille, const std::string& uuidSymbole, int unite = 0);
// Unités d'un composant déjà posées (toutes feuilles)
std::set<int> unitesPosees(const ConceptionElec& c, const std::string& composant);
// Le premier repère libre d'un préfixe : « C3 » si C1 et C2 sont pris (U1A compte pour U1) (2.2, 2.3)
std::string prochainRepere(const ConceptionElec& c, const std::string& prefixe);
// Repère donné à la pose (SAI-05, S15) : un composant dont le repère contient « ? » reçoit le premier repère libre de
// son préfixe, toutes feuilles comprises ; rend le repère du composant
std::string annoterALaPose(ConceptionElec& c, const std::string& composant);
// Unités enchaînées (SYM-04) : l'unité « unite » d'un composant déjà posé, posée en « position » ; vide si l'unité
// n'existe pas ou si elle est déjà posée
std::string poserUniteEn(ConceptionElec& c, std::size_t feuille, const std::string& composant, int unite, Pt position, int rotation = 0);
// La première unité d'un composant qui n'est posée nulle part ; 0 si toutes le sont
int uniteSuivante(const ConceptionElec& c, const std::string& composant);
// Insertion en série (2.3) : un symbole à deux broches posé sur un fil s'y insère, le morceau de fil entre ses deux
// broches disparaît et les deux moitiés finissent sur elles ; faux, sans rien changer, si les deux broches ne tombent
// pas sur un même fil, dans son axe
bool insererDansFil(ConceptionElec& c, std::size_t feuille, const std::string& symbole);
// Placement des champs autour du symbole (2.23, SAI-34) : repère et valeur à l'une des quatre places candidates
// (au-dessus, au-dessous, à droite, à gauche du corps), la moins recouverte par les autres objets de la feuille,
// calée sur la grille de 1,27 mm ; un champ bloqué garde sa place. Rend le nombre de champs déplacés.
int replacerChamps(ConceptionElec& c, std::size_t feuille, const std::string& symbole);
// Article mécanique déposé sur le circuit (trou de fixation, 2.2) : son symbole va sur la feuille « Mécanique »,
// créée au besoin, avec son repère ; son empreinte se pose à « position », tournée de « rotation » degrés. Rend
// l'uuid du composant, vide si l'article n'a pas d'empreinte.
std::string poserArticleCircuit(ConceptionElec& c, const std::string& idSymbole, Pt position, double rotation = 0);

// ---------- Presse-papiers du schéma (EL-22) ----------
// Ce qui est copié d'une feuille : symboles avec leurs composants (et leurs définitions), fils, étiquettes, textes,
// jonctions et non-connexions. Au collage, tout reçoit de nouveaux identifiants ; chaque composant copié devient
// un composant neuf, dont la référence repasse à « R? » pour l'annotation.
struct PressePapiersSchema {
  std::vector<SymbolePlace> symboles;
  std::vector<ComposantElec> composants;
  std::map<std::string, DefSymbole> definitions;
  std::map<std::string, DefEmpreinte> empreintes;  // copies figées du projet d'origine, utilisées par les composants copiés
  std::vector<Fil> fils;
  std::vector<Etiquette> etiquettes;
  std::vector<TexteSchema> textes;
  std::vector<Jonction> jonctions;
  std::vector<NonConnexion> nonConnexions;
  Pt reference;  // coin haut-gauche de ce qui est copié : le collage le place sous la souris
  [[nodiscard]] bool vide() const { return symboles.empty() && fils.empty() && etiquettes.empty() && textes.empty() && jonctions.empty() && nonConnexions.empty(); }
};
// Identifiants de sélection des jonctions et des non-connexions : leur UUID (S26) ; « j:x:y », « nc:x:y » pour
// un objet construit à la main sans identifiant
std::string idJonction(const Jonction& j);
std::string idNonConnexion(const NonConnexion& n);
PressePapiersSchema copierSchema(const ConceptionElec& c, std::size_t feuille, const std::set<std::string>& selection);
// Colle le presse-papiers sur la feuille, décalé pour que sa référence tombe en « ou » ; rend les identifiants des
// objets créés (pour les sélectionner). Les définitions du presse-papiers (symboles, empreintes figées) arrivent
// toujours telles qu'elles ont été copiées (S32, SAI-27) : une définition du projet de même identifiant mais
// différente garde sa place, la copie collée en prend un à elle (« id~2 »).
std::set<std::string> collerSchema(ConceptionElec& c, std::size_t feuille, const PressePapiersSchema& pp, Pt ou);

// Rotation d'un quart de tour (sens direct) ou miroir gauche-droite / haut-bas de la sélection, vus à l'écran
// (EL-24) : un symbole seul tourne sur lui-même, une sélection de plusieurs objets autour du centre de son
// encombrement. Les fils non sélectionnés qui aboutissaient à une broche déplacée la suivent, coudés à l'équerre
// au besoin : les connexions restent (EL-19).
enum class OperationSchema { Tourner, MiroirGaucheDroite, MiroirHautBas };
void transformerSelectionSchema(ConceptionElec& c, std::size_t feuille, const std::set<std::string>& selection, OperationSchema op);

// ---------- Gestes du schéma, côté noyau (lot L2) : la ligne de commande et les scripts les font aussi ----------
// Presse-papiers en JSON, pour le presse-papiers du système et le collage d'un projet à l'autre (2.13, SAI-27) :
// les définitions des symboles voyagent avec (S32)
nlohmann::ordered_json presseVersJson(const PressePapiersSchema& pp);
PressePapiersSchema presseDepuisJson(const nlohmann::ordered_json& j);
// Repères au collage (2.24) : gardés (s'ils sont libres), libres (« R? » puis repère donné), effacés (« R? »)
enum class ReperesCollage : std::uint8_t { Gardes, Libres, Effaces };
std::set<std::string> collerSchemaAvec(ConceptionElec& c, std::size_t feuille, const PressePapiersSchema& pp, Pt ou, ReperesCollage reperes);
// Coller en matrice (2.24, SAI-03) : « lignes » × « colonnes » copies au pas « pas », repères donnés d'abord en ligne
// (D1, D2… sur la première ligne) ou d'abord en colonne
std::set<std::string> collerEnMatrice(ConceptionElec& c, std::size_t feuille, const PressePapiersSchema& pp, Pt origine, int lignes, int colonnes, Pt pas, bool ligneDabord);

// Tracé d'un fil à l'équerre (2.7, 2.18, SAI-06, SAI-29) : recherche A* sur la grille de 1,27 mm, qui contourne le
// corps des symboles d'un pas de garde et coûte « coude » par coude et « croisement » par fil d'un autre réseau
// croisé ; rend les sommets du chemin, départ et arrivée compris, vide s'il n'y en a pas
struct ReglagesTraceFil {
  double coude = 2.0, croisement = 12.0;
  // Zone de recherche : la boîte des deux bouts élargie de dix pas, et de « marge » au-delà d'un pas (« Relier à… »
  // la porte à la moitié de la feuille)
  Nm marge = GRILLE_SCHEMA;
  int maxNoeuds = 250'000;  // au-delà, pas de chemin
};
std::vector<Pt> tracerFil(const ConceptionElec& c, std::size_t feuille, Pt depart, Pt arrivee, const ReglagesTraceFil& r = {});
// « Relier à… » : deux broches désignées « U1:PA9 » (repère:numéro ou repère:nom), reliées par un fil tracé ainsi,
// sur la feuille de la première ; rend le nombre de segments posés, −1 si une broche est introuvable ou si aucun
// chemin n'existe. relierPaires relie une liste de paires, dans un ordre qui pose d'abord les plus courtes.
int relierBroches(ConceptionElec& c, const std::string& de, const std::string& vers, const ReglagesTraceFil& r = {});
int relierPaires(ConceptionElec& c, const std::vector<std::pair<std::string, std::string>>& paires, std::vector<std::string>* echecs = nullptr, const ReglagesTraceFil& r = {});
// Fils : couper en un point (les deux morceaux restent reliés), trancher (deux morceaux disjoints, la jonction
// ôtée), fusionner les segments colinéaires bout à bout d'un même réseau, décaler les croisements à quatre branches
// en deux T décalés d'un pas (2.7, 2.19)
bool couperFil(ConceptionElec& c, std::size_t feuille, const std::string& fil, Pt p);
bool trancherFil(ConceptionElec& c, std::size_t feuille, const std::string& fil, Pt p);
int fusionnerFils(ConceptionElec& c, std::size_t feuille);
int decalerCroisements(ConceptionElec& c, std::size_t feuille, const std::set<std::string>* selection = nullptr);

// Renommer un réseau (2.20, SAI-32) : toutes ses étiquettes et alimentations nommées « ancien » prennent le nom
// « nouveau », sur une feuille (« feuille » ≥ 0) ou toutes (−1) ; rend le nombre d'objets renommés
int renommerReseau(ConceptionElec& c, const std::string& ancien, const std::string& nouveau, int feuille = -1);
// Terminer les broches libres (2.21, SAI-13) : sur les composants donnés, chaque broche libre reçoit une étiquette
// à son nom au bout d'un fil court, ou une marque de non-connexion ; rend le nombre de broches terminées
int terminerBrochesLibres(ConceptionElec& c, std::size_t feuille, const std::set<std::string>& composants, bool etiquettes = true);
// Aligner et répartir sur la grille (2.25, SAI-35) : les fils accrochés suivent, à l'équerre
enum class Alignement : std::uint8_t { Gauche, Droite, Haut, Bas, CentreHorizontal, CentreVertical, RepartirHorizontal, RepartirVertical };
int alignerSchema(ConceptionElec& c, std::size_t feuille, const std::set<std::string>& selection, Alignement a);
// Échanger deux symboles de même genre (2.25, SAI-36) : chacun prend la place et l'orientation de l'autre
bool echangerSymboles(ConceptionElec& c, std::size_t feuille, const std::string& a, const std::string& b);
// Déplacer des objets du schéma de « delta », fils accrochés retracés à l'équerre (fils élastiques, 2.8, S16) ou
// décrochés (M) ; marques de non-connexion et jonctions des broches déplacées suivent (S17, S31)
void deplacerSchema(ConceptionElec& c, std::size_t feuille, const std::set<std::string>& selection, Pt delta, bool elastique = true);

// ---------- Connectivité ----------
struct NoeudBroche {
  std::string composant, broche;
};
struct Reseau {
  std::string nom;
  bool nomme = false;              // par une étiquette ou une alimentation
  std::vector<NoeudBroche> broches;
  std::vector<std::string> noms;   // noms distincts posés sur ce réseau (alimentations, étiquettes globales, locales)
  std::vector<std::string> alimentations;  // noms distincts des symboles d'alimentation : deux, c'est un court-circuit
  std::vector<std::string> alias;          // anciens noms figés de ses broches (le cuivre peut encore les porter)
  // Source : sortie d'alimentation, drapeau d'alimentation, ou broche d'un connecteur (l'alimentation arrive de
  // l'extérieur). Une broche passive d'un autre composant (condensateur, résistance) n'alimente rien (EL-14).
  bool alimente = false;
};
struct Connectivite {
  std::vector<Reseau> reseaux;
  std::map<std::pair<std::string, std::string>, int> parBroche;  // (UUID composant, numéro) → réseau
  std::map<std::string, int> parFil, parEtiquette, parSymbole;    // UUID → réseau
  std::map<std::string, std::vector<Pt>> jonctions;               // par feuille : trois branches ou plus
  std::map<std::string, std::vector<Pt>> extremitesLibres;        // par feuille : bouts de fil qui ne touchent rien
  int reseau(const std::string& composant, const std::string& broche) const;
  std::string nom(const std::string& composant, const std::string& broche) const;
  int indice(const std::string& nomReseau) const;  // nom actuel ou alias
  std::map<std::string, int> parNom;               // nom actuel et alias → réseau
  // Nom actuel du réseau désigné par « nom » (un alias, un ancien nom figé) ; « nom » lui-même s'il est inconnu
  std::string canonique(const std::string& nom) const;
  // Broche commune à plusieurs unités posée sur deux réseaux déjà formés (2.33, ERC-42) : ses poses ne sont pas
  // réunies en silence ; les réseaux qu'elles touchent sont nommés ici
  struct ConflitCommun {
    std::string composant, broche;
    std::vector<std::string> reseaux;
  };
  std::vector<ConflitCommun> conflitsCommuns;
};
// Règles dont les affectations de classes visent les noms actuels des réseaux (les alias suivent)
Regles reglesResolues(const Regles& r, const Connectivite& k);
// Fige les noms automatiques de tous les réseaux, ramène pistes, vias, zones et classes sur les noms actuels ;
// rend le nombre d'objets renommés. Appelé par la mise à jour du circuit et le routage automatique. Deux
// affectations qui visent le même réseau après un renommage, vers deux classes différentes, gardent la plus
// contraignante ; chaque conflit est décrit dans « conflits » (R11), jamais tranché en silence.
int synchroniserReseaux(ConceptionElec& c, std::vector<std::string>* conflits = nullptr);
// Diagnostic des affectations de classes (DRC-35) : réseau cité qui n'existe pas, motif qui ne prend rien ou que
// masque un motif placé avant lui (R5), classe supprimée (R10), deux affectations vers des classes différentes
// pour un même réseau (R11). Un message par défaut.
std::vector<std::string> diagnosticAffectations(const Regles& r, const Connectivite& k);
Connectivite connectivite(const ConceptionElec& c);

// ---------- Vérifications ----------
enum class Gravite : std::uint8_t { Erreur, Avertissement };
// Quand l'ERC parle (S9) : immédiat (marque rouge tout de suite), dessin (petit cercle gris au bout d'une broche ou
// d'un fil libre, compteur, rien d'autre), complet (sur « Vérifier », au passage au circuit, avant une publication)
enum class NiveauErc : std::uint8_t { Immediat, Dessin, Complet };
struct Violation {
  Gravite gravite = Gravite::Erreur;
  std::string code, message, conseil;
  std::string feuille;  // ERC : UUID de la feuille
  Pt position;
  std::vector<std::string> objets;
  NiveauErc niveau = NiveauErc::Complet;
};
std::vector<Violation> verifierERC(const ConceptionElec& c, const Connectivite& k);
// ERC par niveau (2.11, S9) : seulement les règles des niveaux jusqu'à « jusqua » ; gravités réglées et exclusions
// de la conception appliquées (une violation exclue n'est pas rendue, une gravité « ignorer » non plus)
std::vector<Violation> verifierERC(const ConceptionElec& c, const Connectivite& k, NiveauErc jusqua);
// Matrice des conflits de broches (ERC-20, E1) : « . » permis, « A » avertissement, « E » erreur, selon la matrice
// par défaut et les cases réglées du projet
char conflitBroches(const ConceptionElec& c, TypeBroche a, TypeBroche b);
// L'exclusion de la conception qui couvre cette violation, s'il y en a une ; le niveau propre d'un code
const ExclusionErc* exclusionDe(const ConceptionElec& c, const Violation& v);
NiveauErc niveauErc(const std::string& code);
// Garde-fou du brochage (B17, ERC-09) : vrai si l'empreinte du composant n'est pas de la même famille de boîtier
// (même famille et même nombre de pastilles) que l'empreinte pour laquelle son symbole est écrit, et que la
// correspondance broche ↔ pastille n'a pas été vérifiée pour elle (ComposantElec::brochageVerifie).
bool brochageAVerifier(const ConceptionElec& c, const ComposantElec& x);
// Annotation : « nouveaux » ne touche qu'aux repères vides ou qui contiennent « ? » ; « tout » renumérote dans
// l'ordre de lecture des feuilles. La liste prévue se montre avant d'être appliquée.
struct ChangementReference {
  std::string composant, avant, apres;
};
std::vector<ChangementReference> annotationPrevue(const ConceptionElec& c, bool toutRenumeroter);
int annoter(ConceptionElec& c, bool toutRenumeroter);
struct LigneNomenclature {
  int quantite = 0;
  std::vector<std::string> references;
  std::string valeur, empreinte, fabricant, mpn, fournisseur, description, lcsc;
};
std::vector<LigneNomenclature> nomenclature(const ConceptionElec& c);
// Passage d'un composant à la nomenclature et au fichier de placement, selon son genre (3.17, SAI-45) et ses cases
// (hors nomenclature, hors placement, ne pas monter, hors carte) ; un trou de fixation et une alimentation n'y vont pas
GenreComposant genreEffectif(const ConceptionElec& c, const ComposantElec& x);
bool dansNomenclature(const ConceptionElec& c, const ComposantElec& x);
bool dansPlacement(const ConceptionElec& c, const ComposantElec& x);
// Exclusions pour toute une feuille d'un coup (STR-08) : la case est posée sur chacun de ses composants ; rend leur
// nombre. « quoi » : « nePasMonter », « horsNomenclature », « horsCarte », « horsPlacement », « horsSimulation »
int exclureFeuille(ConceptionElec& c, std::size_t feuille, const std::string& quoi, bool oui);
std::string netlistTexte(const ConceptionElec& c, const Connectivite& k);
bool ordreNaturel(const std::string& a, const std::string& b);  // R2 avant R10

// ---------- Circuit imprimé ----------
struct BilanMiseAJour {
  int ajoutees = 0, retirees = 0, changees = 0;
  int cuivreRenomme = 0;  // pistes, vias, zones et classes ramenées sur le nom actuel de leur réseau
  std::vector<std::string> conflitsClasses;  // affectations en conflit après un renommage (R11)
};
BilanMiseAJour mettreAJourCircuit(ConceptionElec& c);
struct PastillePlacee {
  std::string composant, reference, numero, reseau;
  Pastille pastille;  // dans le repère de la carte
  bool dessous = false;
  Couches couches = 0;
  Polygone forme;
  int empreinte = 0;  // indice dans circuit.empreintes
  // Pastille de cuivre posée sur une face qui n'en a pas (CMS sur le dessus d'une carte simple face, R2) : elle ne
  // touche aucun cuivre, mais compte au chevelu, pour que ses liaisons restent « non routées » (DRC-20)
  bool inaccessible = false;
};
std::vector<PastillePlacee> pastilles(const ConceptionElec& c, const Connectivite& k);
// Hauteur du corps d'un composant posé : hauteurComposant(), dans hauteurs.hpp (3.4)
// Emprise du corps dans le repère de l'empreinte : son corps, ou à défaut son encombrement rentré de 0,25 mm (point
// de test, empreinte écrite sans corps), pour qu'une hauteur saisie ait toujours un corps à dessiner (T2)
Boite empriseCorps(const DefEmpreinte& d);
const DefEmpreinte* empreinteEnCache(const std::string& id);  // générée une fois, gardée
// Fige dans la conception les empreintes utilisées qui ne le sont pas encore ; rend leur nombre.
int figerEmpreintes(ConceptionElec& c);
// Empreintes figées qui diffèrent de la version actuelle du générateur, et reprise de cette version
std::vector<std::string> empreintesPerimees(const ConceptionElec& c);
int actualiserEmpreintes(ConceptionElec& c, const std::vector<std::string>& ids);
using Remplissages = std::map<std::string, Polygones>;       // UUID de zone → cuivre
Remplissages remplirZones(const ConceptionElec& c, const Connectivite& k);
struct Liaison {
  std::string reseau;
  Pt a, b;
};
struct CourtCircuit {
  std::string a, b;  // réseaux
  Pt position;       // un point de contact entre les deux cuivres (EL-27)
};
struct EtatRoutage {
  std::vector<Liaison> chevelu;                              // liaisons à router
  std::vector<CourtCircuit> courtsCircuits;
  std::vector<Pt> boutsLibres;                               // extrémités de piste qui ne touchent rien
  int reseauxComplets = 0, reseauxTotal = 0;
};
EtatRoutage etatRoutage(const ConceptionElec& c, const Connectivite& k, const Remplissages& r);
std::vector<Violation> verifierDRC(const ConceptionElec& c, const Connectivite& k, const Remplissages& r);
// DRC réglée (2.33, ERC-11) : les gravités du projet (ReglagesErc::gravites, codes « DRC-xx ») et ses exclusions
// valent aussi pour la vérification des règles de dessin ; un contrôle ignoré n'est pas rendu
std::vector<Violation> verifierDRCReglee(const ConceptionElec& c, const Connectivite& k, const Remplissages& r);
// Réseau de la zone remplie qui couvre ce point (une via posée dans un plan de masse en prend le réseau, EL-12)
std::string reseauDeZoneSous(const ConceptionElec& c, const Connectivite& k, const Remplissages& r, Pt p);
// Routage automatique simple : les liaisons qui passent sans détour (45°, équerre, sortie droite des pastilles), sans
// jamais violer l'isolement ; les pastilles CMS d'un réseau qui a une zone sur l'autre face y descendent par une via.
// Routeur d'appoint, sans garantie de complétude : il ne cherche pas de détour et ne déplace rien. Chaque
// liaison qu'il laisse au chevelu est rendue avec la raison (obstacle rencontré, absence de couche commune…).
struct LiaisonRestante {
  std::string reseau;
  Pt a, b;
  std::string raison;
};
// Bilan compté sur la carte : liaisons du chevelu résolues, vias ajoutées, liaisons restantes (avec leur raison)
struct BilanRoutage {
  int liaisons = 0, vias = 0, restantes = 0;
  std::vector<LiaisonRestante> nonRoutees;
  bool complet() const { return restantes == 0; }
};
BilanRoutage routerSimple(ConceptionElec& c);
// Pourquoi chaque liaison du chevelu ne passe pas par un tracé simple (diagnostic, sans rien poser).
std::vector<LiaisonRestante> diagnostiquerChevelu(const ConceptionElec& c);

// ---------- Fabrication ----------
struct PrimitiveFab {
  enum class Type : std::uint8_t { Disque, Rect, Oblong, Region, Trait } type = Type::Disque;
  Pt a, b;            // centre ; trait : extrémités
  Nm l = 0, h = 0;    // disque : diamètre dans l ; trait : largeur dans l
  Polygone region;
  std::string fonction, reseau, reference, numero;
  std::string broche;  // nom de la broche (« SDA ») : troisième champ de %TO.P (F10)
};
struct CoucheFab {
  Couche couche;
  std::vector<PrimitiveFab> primitives;
};
struct TrouFab {
  Pt p;
  Nm diametre = 0;
  bool metallise = true;
  std::string fonction;  // ViaDrill, ComponentDrill, MechanicalDrill
};
struct ModeleFabrication {
  std::string nom;
  Polygone contour;
  Boite boite;
  std::vector<CoucheFab> couches;
  std::vector<TrouFab> trous;
};
ModeleFabrication modeleFabrication(const ConceptionElec& c, const Connectivite& k, const Remplissages& r);
// Textes de la sérigraphie (repères, valeurs, textes de carte) en traits de la police vectorielle commune :
// mêmes traits à l'écran, dans les Gerber et dans la vérification des textes posés sur une pastille (EL-06)
struct TraitSerigraphie {
  Couche couche = Couche::SerigraphieDessus;
  Pt a, b;
  Nm epaisseur = 150'000;
  std::string composant;  // UUID du composant (repère, valeur) ; vide pour un texte de carte
  std::string texte;
};
std::vector<TraitSerigraphie> textesSerigraphie(const ConceptionElec& c);

// Typon pour la gravure maison (insoleuse, transfert de toner) : une couche de cuivre à l'échelle 1, vue côté
// cuivre (la face dessous est donc en miroir), avec un miroir de plus pour le transfert de toner, en positif
// (cuivre noir) ou en négatif, des perçages réduits à un point de centrage pour guider le foret, le contour de la
// carte et une règle de 50 mm pour vérifier l'échelle de l'impression.
struct OptionsTypon {
  Couche couche = Couche::CuivreDessous;
  bool miroirTransfert = false;  // transfert de toner : le typon se pose face imprimée contre le cuivre
  bool negatif = false;
  double trouCentrage = 0.6;     // mm : diamètre du point laissé au centre des perçages (0 : trous réels)
  double marge = 5;              // mm autour de la carte
};
struct ModeleTypon {
  double largeur = 0, hauteur = 0;            // mm, page (carte et marges, au moins 90 mm de large), règle comprise
  double hauteurCarte = 0;                    // mm, partie carte de la page
  std::vector<Polygones> cuivre;              // régions (contour et trous), en nm dans le repère du typon (Y vers le bas)
  struct Trait {
    Pt a, b;
    Nm largeur;
  };
  std::vector<Trait> traits;                  // pistes (bouts ronds)
  std::vector<std::pair<Pt, Nm>> trous;       // centre, diamètre dessiné (blanc sur le cuivre)
  Polygone contour;
  Polygones decoupes;
  bool miroir = false;
};
ModeleTypon modeleTypon(const ConceptionElec& c, const Connectivite& k, const Remplissages& r, const OptionsTypon& o);
std::string svgTypon(const ModeleTypon& m, const OptionsTypon& o, const std::string& titre);
// Un îlot de cuivre et ses trous en un seul contour, les trous reliés par des coupures d'épaisseur nulle (EL-47)
Polygone regionAvecCoupures(const Polygone& exterieur, const Polygones& trous);
// Repère commun des fichiers de fabrication (Gerber, perçages, placement) : origine au coin bas gauche de la
// carte, Y vers le haut, toutes les coordonnées positives, comme l'attendent fabricants et assembleurs (EL-29)
inline Pt versRepereFabrication(const Boite& carte, Pt p) { return {p.x - carte.x0, carte.y1 - p.y}; }
inline Pt depuisRepereFabrication(const Boite& carte, Pt q) { return {q.x + carte.x0, carte.y1 - q.y}; }
std::string gerber(const ModeleFabrication& m, Couche couche, const std::string& version);
std::string excellon(const ModeleFabrication& m, bool metallises, const std::string& version);
// Placement : origine au coin bas gauche de la carte, Y vers le haut ; « anglais » : en-têtes Designator, Mid X,
// Mid Y, Rotation, Layer (top/bottom) attendus par les assembleurs. Mid X et Mid Y donnent le point de placement
// (centre des pastilles), pas l'origine de l'empreinte ; la rotation est en degrés, positive dans le sens
// anti-horaire vue de dessus, entre 0 et 360 ; un composant du dessous garde l'angle vu de dessus, sans miroir (F3).
std::string csvPlacement(const ConceptionElec& c, bool anglais = false);
// Règle commune au fichier de placement et à son contrôle (F14) : un composant posé, à monter, ni mécanique, ni
// hors carte, ni hors placement
bool dansFichierPlacement(const ConceptionElec& c, const EmpreintePlacee& e);
std::string csvNomenclature(const ConceptionElec& c);
// Liste de réseaux pour le test électrique des cartes nues (IPC-D-356A, millimètres au micromètre) : chaque
// pastille et chaque via avec son réseau, sa face d'accès, son perçage ; repère commun des fichiers de fabrication
std::string ipcD356(const ConceptionElec& c, const Connectivite& k);
// Fichier de travail Gerber (.gbrjob, JSON de Ucamco) : dimensions, nombre de couches, épaisseur, finition, règles
// et fonction de chaque fichier Gerber écrit
std::string gerberJob(const ConceptionElec& c, const ModeleFabrication& m, const std::vector<std::pair<std::string, Couche>>& fichiers, const std::string& version);
// Liste de réseaux au format KiCad (.net, « export » version E) : composants avec valeur et empreinte, réseaux et
// leurs broches (fonction, type)
std::string netlistKicad(const ConceptionElec& c, const Connectivite& k);
// Dossier de fabrication complet (Gerber, perçage, placement, nomenclature, LISEZMOI) ; std::runtime_error si
// l'écriture échoue. Rend les fichiers écrits.
std::vector<std::string> ecrireDossierFabrication(const ConceptionElec& c, const std::string& dossier, const std::string& version);

// ---------- Validation globale, santé de la conception, publication ----------
// Chaque étape a ses erreurs (bloquantes pour la publication), ses avertissements et un score de 0 à 100 tiré de
// mesures réelles (part des réseaux routés, des lignes de nomenclature complètes, des champs du cartouche…). Chaque
// violation de l'ERC et de la DRC est rangée dans une seule étape.
struct EtapeValidation {
  std::string nom;
  int erreurs = 0, avertissements = 0, score = 100;
  std::vector<Violation> details;
};
struct Validation {
  std::vector<EtapeValidation> etapes;
  int bloquants() const;
  int critiques() const;  // erreurs qu'aucune dérogation ne lève (voir derogeable)
  bool publiable() const { return bloquants() == 0; }
};
// Erreurs dérogeables : une raison écrite permet de publier malgré elles (isolement d'une règle maison, entrée
// non pilotée voulue…). Ne le sont jamais : courts-circuits, liaisons non routées, broches sans pastille,
// empreintes inconnues, contour invalide, cuivre hors carte, perçages qui se recoupent, règles sous les
// capacités du fabricant, intégrité du projet. Elles bloquent la publication sans exception.
bool derogeable(const std::string& code);
Validation valider(const ConceptionElec& c, const Connectivite& k, const EtatRoutage& etat, const std::vector<Violation>& erc, const std::vector<Violation>& drc);
Validation valider(const ConceptionElec& c);
std::string empreinteConception(const ConceptionElec& c);   // SHA-256 du modèle (hors publications et réglages d'affichage), 64 chiffres hexadécimaux
std::string revisionSuivante(const std::string& r);          // A → B, Z → AA, 3 → 4
// Publie depuis un instantané immuable de la conception : vérifie tout, écrit le dossier de fabrication à côté,
// le contrôle (controlerDossierFabrication), y ajoute l'instantané (conception.json), la fiche de publication
// (règles figées, validation, dérogation, empreintes) et SHA256SUMS.txt, puis le renomme en
// « dossierParent/<nom>_rev<R>_<id> » : le dossier final est complet ou n'existe pas, et n'en remplace jamais un
// autre. « complements » écrit des fichiers en plus (dessins PDF) dans le dossier en cours d'écriture, avant le
// calcul des empreintes ; il rend leurs chemins relatifs. Refuse (std::runtime_error) une conception verrouillée,
// une erreur critique (jamais dérogeable), ou des erreurs dérogeables sans raison écrite.
Publication publier(ConceptionElec& c, const std::string& dossierParent, const std::string& auteur, const std::string& date, const std::string& derogation,
                    const std::string& version, std::vector<std::string>* fichiers = nullptr,
                    const std::function<std::vector<std::string>(const std::string& dossierEnEcriture)>& complements = nullptr);
void nouvelleRevision(ConceptionElec& c);  // déverrouille et passe à la révision suivante
// Contrôle d'un dossier de fabrication écrit : présence et contenu de chaque fichier attendu (couches Gerber,
// en-têtes et unités, perçages et nombre de trous, placement, nomenclature), cohérence du contour avec la
// carte. Rend la liste des défauts (vide : dossier conforme).
std::vector<std::string> controlerDossierFabrication(const ConceptionElec& c, const std::string& dossier);
// Publication déjà écrite : chaque fichier existe et garde son SHA-256 d'origine. Rend les écarts.
std::vector<std::string> verifierPublication(const Publication& p, const std::string& dossierParent);

// ---------- Fichier ----------
// Format propre à la conception électronique (clé « format » de chaque conception, ECH-10) : absent ou 1 jusqu'à la
// 0.28, 2 depuis la 0.29. Une conception d'un format plus récent s'ouvre en lecture tolérante, avec une alerte.
inline constexpr int FORMAT_ELECTRONIQUE = 4;  // 2 : la 0.30 ; 3 : la 0.31 (lot L2) ; 4 : la 0.32 (lot L3)
// Écriture dans un ordre stable : clés dans un ordre fixe, clés inconnues ensuite ; listes dont l'ordre n'a pas de
// sens (composants, symboles posés, fils, étiquettes, textes, empreintes placées, pistes, vias, jonctions) triées par
// identifiant ; feuilles, zones (départage des priorités égales), classes, affectations et publications dans leur ordre
nlohmann::ordered_json versJson(const ConceptionElec& c);
// Lecture tolérante, objet par objet (ordre des clés gardé) : ce qui est ignoré ou corrigé est décrit dans « alertes »
ConceptionElec conceptionDepuisJson(const nlohmann::ordered_json& j, std::vector<std::string>* alertes = nullptr);
nlohmann::ordered_json empreinteVersJson(const DefEmpreinte& d);
DefEmpreinte empreinteDepuisJson(const nlohmann::ordered_json& j);
nlohmann::ordered_json symboleVersJson(const DefSymbole& d);
DefSymbole symboleDepuisJson(const nlohmann::ordered_json& j);

}  // namespace prom::ecad

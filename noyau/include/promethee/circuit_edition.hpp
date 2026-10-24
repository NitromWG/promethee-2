// Prométhée : gestes du circuit imprimé côté noyau (lot L5, 0.34). Chaque fonction modifie la conception d'un coup :
// l'interface l'appelle dans une commande (un pas d'annulation), la ligne de commande et les scripts de même.
// Contour et découpes (5.4, 5.31), collisions, chevelu et renumérotation selon la carte (5.6, 5.19), nettoyage et
// glissement des pistes (5.9, 5.23), paires, accords et gouttes (5.10, 5.22), zones (5.11, 5.25, 5.26), presse-papiers
// du circuit (5.16, 5.30), carte sans schéma (5.17), import du placement et repères (5.19), découplages et placement
// poussé (5.24), codes 2D et logos (5.15, 5.31), gabarits de carte (5.15), réglages repris (5.32).
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- 5.4, 5.31 · Contour et découpes ----------
// Boucle −1 : le contour (passé en polygone libre s'il était un rectangle) ; k ≥ 0 : la découpe k
bool deplacerSommetContour(ConceptionElec& c, int boucle, std::size_t sommet, Pt p);
bool insererSommetContour(ConceptionElec& c, int boucle, std::size_t apres, Pt p);
bool supprimerSommetContour(ConceptionElec& c, int boucle, std::size_t sommet);
std::vector<Pt> sommetsContour(const ConceptionElec& c, int boucle);
// Contour donné sommet par sommet (tableau ou saisie dynamique), coordonnées dans le repère de travail (5.31)
bool contourDepuisPoints(ConceptionElec& c, const std::vector<Pt>& pointsTravail);
// Repère de travail (5.31) : origine choisie et axe y vers le haut au choix
Pt versRepereTravail(const AffichageCircuit& a, Pt carte);
Pt depuisRepereTravail(const AffichageCircuit& a, Pt travail);
// Collisions d'encombrement d'un composant, en direct pendant le glisser (5.4) : les composants touchés, même face
std::vector<std::string> collisionsEncombrement(const ConceptionElec& c, const std::string& composant);

// ---------- 5.6, 5.19 · Chevelu et renumérotation ----------
double longueurChevelu(const EtatRoutage& e);  // nm
// Huit parcours : direction principale (gauche→droite, droite→gauche, haut→bas, bas→haut) puis secondaire
enum class Parcours : std::uint8_t { GDHB, GDBH, DGHB, DGBH, HBGD, HBDG, BHGD, BHDG };
struct OptionsRenumerotation {
  Parcours parcours = Parcours::GDHB;
  std::set<std::string> portee;   // composants (UUID) ; vide : tous
  std::set<std::string> exclus;   // repères gardés tels quels
  std::string prefixeDessous;     // préfixe ajouté aux composants du dessous (« B ») ; vide : aucun
  int debut = 1;
  bool dessusDabord = true;       // la face du dessus numérotée avant celle du dessous
  Nm rangee = 1'000'000;          // tolérance d'alignement d'une rangée
};
std::vector<ChangementReference> renumerotationPrevue(const ConceptionElec& c, const OptionsRenumerotation& o);
int renumeroter(ConceptionElec& c, const OptionsRenumerotation& o, std::vector<std::string>* journal = nullptr);

// ---------- 5.9, 5.23 · Pistes posées ----------
struct ChangementNettoyage {
  std::string genre;        // « nul », « double », « fusion », « via orpheline », « bout libre »
  std::string description;
  std::vector<std::string> objets;
};
std::vector<ChangementNettoyage> nettoyagePrevu(const ConceptionElec& c, const std::set<std::string>* selection = nullptr);
int nettoyer(ConceptionElec& c, const std::vector<ChangementNettoyage>& choisis);
// Glisser un segment posé (5.9) : il se déplace parallèlement à lui-même, ses voisins le suivent à 45°
bool glisserSegment(ConceptionElec& c, const std::string& piste, Pt delta);
// Via déplacée, les pistes qui y aboutissent la suivent (5.23)
bool deplacerVia(ConceptionElec& c, const std::string& via, Pt p);
// Couper une piste en un point (les deux morceaux restent reliés) ; trancher : un trou de « ecart » entre eux
bool couperPiste(ConceptionElec& c, const std::string& piste, Pt p);
bool trancherPiste(ConceptionElec& c, const std::string& piste, Pt p, Nm ecart);
// Dérouter (5.23) : « connexion:<piste> », « reseau:<nom> », « composant:<uuid> », « carte » ; les réseaux tenus à
// l'écart (gardes) et le cuivre verrouillé restent. Rend le nombre d'objets retirés.
int derouter(ConceptionElec& c, const std::string& portee, const std::set<std::string>& gardes = {});
// Pinceau de réseau (5.23) : les objets choisis prennent ce réseau
int appliquerReseau(ConceptionElec& c, const std::set<std::string>& objets, const std::string& reseau);
// Lissage (5.23) : coins droits coupés à 45° et détours retirés, à effort 1 à 3, sans violer l'isolement
int lisser(ConceptionElec& c, const std::set<std::string>& pistes, int effort = 2);
// Plusieurs liaisons routées en une commande (5.9, 5.21) ; les raisons de celles qui restent au chevelu
int routerPlusieurs(ConceptionElec& c, const std::vector<Liaison>& liaisons, std::vector<std::string>* raisons = nullptr);

// ---------- 5.10, 5.22, 8.27 · Paires, accords, gouttes ----------
struct BilanPaire {
  bool ok = false;
  std::string raison;
  Nm ecart = 0;
  std::vector<std::string> pistes;
};
// Route une paire à écart constant : le tracé central contourne les obstacles avec la largeur de la paire entière,
// puis se dédouble de part et d'autre
BilanPaire routerPaire(ConceptionElec& c, const PaireDifferentielle& paire, Couche couche);
double longueurReseau(const ConceptionElec& c, const std::string& reseau);  // nm
// Retard de propagation (8.27) : ligne à bande dans εr, microruban dans la permittivité effective de Hammerstad
double psParMm(const ConceptionElec& c, Couche couche, Nm largeur);
double retardReseau(const ConceptionElec& c, const std::string& reseau);  // ps
// Segments d'un motif d'accord (accordéon, trombone, dents de scie), à la place de son segment d'origine
std::vector<Piste> segmentsMotif(const MotifAccord& m);
double longueurMotif(const MotifAccord& m);
// Pose un motif sur le plus long segment droit d'un réseau pour atteindre « cible » ; rend son UUID, vide sinon
std::string accorderReseau(ConceptionElec& c, const std::string& reseau, Nm cible, GenreMotifAccord genre, Nm amplitude = 1'000'000);
int regenererMotifs(ConceptionElec& c);
// Accorde tout un groupe sur sa cible (8.27) ; les réseaux qui n'ont pas la place vont au bilan
int accorderGroupe(ConceptionElec& c, const GroupeAccord& g, std::vector<std::string>* nonTenus = nullptr);
struct OptionsGouttes {
  double pourcentage = 0.5;  // longueur de la goutte, en part de la largeur de la pastille
  double largeur = 0.8;      // largeur à la pastille, en part de sa plus petite dimension
  bool cms = true, traversantes = true, vias = true;
  Nm longueurMax = 1'000'000;
};
int poserGouttes(ConceptionElec& c, const OptionsGouttes& o = {});
int retirerGouttes(ConceptionElec& c);

// ---------- 5.11, 5.25, 5.26 · Zones ----------
std::string zoneSurToutLaCarte(ConceptionElec& c, const std::string& reseau, Couche couche);
// Plan de masse d'un réseau (le symbole de masse) sur les couches données (5.25)
std::vector<std::string> planDuReseau(ConceptionElec& c, const std::string& reseau, const std::vector<Couche>& couches);
// Couture de vias en grille dans le cuivre rempli d'une zone (5.11), groupe « couture:<zone> » ; refaite à l'appel
int coudreZone(ConceptionElec& c, const std::string& zone, Nm pas, Nm diametre = 600'000, Nm percage = 300'000);
// Îlots d'une zone non reliés : une via vers la zone de même réseau d'une autre couche (5.25)
int relierIlotsParVias(ConceptionElec& c, const std::string& zone);
int nommerZones(ConceptionElec& c);  // nom unique donné d'office : « GND · B.Cu », « GND · B.Cu (2) »
std::string dupliquerZone(ConceptionElec& c, const std::string& zone, Couche couche);
std::vector<std::string> couperZone(ConceptionElec& c, const std::string& zone, Pt a, Pt b);
std::string fusionnerZones(ConceptionElec& c, const std::vector<std::string>& zones);
bool soustraireZone(ConceptionElec& c, const std::string& zone, const std::string& autre);
std::string zoneDepuisPistes(ConceptionElec& c, const std::set<std::string>& pistes, Nm marge);
bool arrondirZone(ConceptionElec& c, const std::string& zone, Nm rayon, bool chanfrein);
// Remplissages vérifiés avant toute sortie (5.26) : la signature des données de la zone ; périmée, à refaire
std::string signatureZone(const ConceptionElec& c, const Zone& z);
std::vector<std::string> remplissagesPerimes(const ConceptionElec& c);
// Fige (remplissage gardé), met de côté ou rend à la normale les zones choisies
int etatZones(ConceptionElec& c, const std::set<std::string>& zones, EtatZone etat);

// ---------- 5.16, 5.30 · Presse-papiers du circuit ----------
struct PressePapiersCircuit {
  std::vector<EmpreintePlacee> empreintes;
  std::vector<ComposantElec> composants;
  std::map<std::string, DefEmpreinte> definitions;
  std::vector<Piste> pistes;
  std::vector<Via> vias;
  std::vector<Zone> zones;
  std::vector<TexteCircuit> textes;
  std::vector<DessinCircuit> dessins;
  std::map<std::string, std::string> reseaux;  // « composant:numéro » → réseau de la broche copiée
  Pt reference;
  bool vide() const { return empreintes.empty() && pistes.empty() && vias.empty() && zones.empty() && textes.empty() && dessins.empty(); }
};
PressePapiersCircuit copierCircuit(const ConceptionElec& c, const std::set<std::string>& selection);
// Collage spécial (5.30) : réseaux gardés ou retirés, cuivre rangé dans une classe ; rend les identifiants créés
struct OptionsCollageCircuit {
  bool garderReseaux = true;
  std::string classe;     // cuivre collé rangé dans cette classe (affectations des réseaux)
  bool composantsNeufs = true;  // les composants collés deviennent de la carte seule (repères « ? »)
};
std::set<std::string> collerCircuit(ConceptionElec& c, const PressePapiersCircuit& pp, Pt ou, const OptionsCollageCircuit& o = {});
nlohmann::ordered_json presseCircuitVersJson(const PressePapiersCircuit& pp);
PressePapiersCircuit presseCircuitDepuisJson(const nlohmann::ordered_json& j);

// ---------- 5.17 · Carte sans schéma, sélections logiques ----------
// Pose une empreinte sans schéma : son composant est de la carte seule, au premier repère libre du préfixe
std::string poserEmpreinteSeule(ConceptionElec& c, const std::string& idEmpreinte, Pt p, double rotation = 0, bool dessous = false, const std::string& prefixe = "");
// Piste tirée entre deux pastilles (5.17) : leur réseau est créé, ou celui de l'une réunit l'autre ; rend son nom
std::string reseauParPiste(ConceptionElec& c, const std::string& composantA, const std::string& numeroA, const std::string& composantB, const std::string& numeroB);
bool echangerEmpreintes(ConceptionElec& c, const std::string& a, const std::string& b);
// Serrer (5.17) : les composants choisis se rapprochent de leur centre commun, sans se recouvrir
int serrerEmpreintes(ConceptionElec& c, const std::vector<std::string>& composants, Nm ecart = 250'000);
// Sélections logiques : « reseau », « feuille », « classe », « valeur », « empreinte », « groupe », « composant »
std::set<std::string> selectionLogique(const ConceptionElec& c, const std::string& critere, const std::string& valeur);

// ---------- 5.19 · Changements en masse ----------
// Placement repris d'un fichier : CSV, texte à colonnes, ou classeur XLSX (contenu brut) ; en-têtes de KiCad,
// d'Altium, de PADS, d'Allegro reconnus ; le journal dit ce qui a été fait et ce qui ne l'a pas été
struct BilanImportPlacement {
  int places = 0, inconnus = 0;
  std::vector<std::string> journal;
};
BilanImportPlacement importerPlacement(ConceptionElec& c, const std::string& contenu, bool unitesPouces = false);
// Lignes d'un classeur XLSX (première feuille), cellules en texte
std::vector<std::vector<std::string>> lireXlsx(const std::string& octets);
std::vector<std::vector<std::string>> lireTableau(const std::string& texte);  // CSV, point-virgule, tabulations ou espaces
// Repères de sérigraphie placés d'office (5.19) : position 1 à 9 (pavé numérique autour du corps), 0 : place libre
// la plus proche cherchée en spirale
int placerReperes(ConceptionElec& c, const std::set<std::string>& composants, int position);
// Édition des textes en masse : hauteur, couche, épaisseur (repères compris)
int modifierTextes(ConceptionElec& c, const std::set<std::string>& textes, std::optional<Nm> hauteur, std::optional<Couche> couche);
// Table d'échange de couches (5.19) : pistes, zones, vias, textes et dessins passent de l'une à l'autre
int echangerCouches(ConceptionElec& c, const std::map<Couche, Couche>& table);

// ---------- 5.24 · Placement assisté ----------
// Condensateurs de découplage d'un circuit intégré : chacun au plus près de sa broche d'alimentation, le plus petit
// d'abord, sans recouvrement ; puis relié par le contournement. Rend le nombre placé.
int placerDecouplages(ConceptionElec& c, const std::string& circuitIntegre, Nm distanceMax = 2'000'000);
// Empreinte posée qui écarte ses voisines (placement poussé) ; faux si une voisine verrouillée bloque
bool pousserEmpreintes(ConceptionElec& c, const std::string& composant, Pt position);

// ---------- 5.15, 5.31 · Codes 2D, codes-barres et logos ----------
// QR code (ISO/IEC 18004, mode octet, correction M, versions 1 à 10) : modules, vrai = sombre
std::vector<std::vector<bool>> codeQR(const std::string& texte);
// Micro QR (M2 à M4, mode octet, correction L) : pour de petites données
std::vector<std::vector<bool>> codeMicroQR(const std::string& texte);
// Code 128 (jeu B) : largeurs des barres et espaces, en modules
std::vector<int> code128(const std::string& texte);
// Modules en polygones (un carré par module sombre, réunis), module de « pas », coin haut gauche en « origine »
Polygones modulesEnPolygones(const std::vector<std::vector<bool>>& modules, Pt origine, Nm pas);
// Barres d'un code-barres (largeurs alternées barre, espace… en modules), coin haut gauche en « origine »
Polygones barresEnPolygones(const std::vector<int>& largeurs, Pt origine, Nm module, Nm hauteur);
// Motif posé sur la carte (code, logo) : région de cuivre « logo » du réseau donné sur un cuivre, dessins pleins
// ailleurs (sérigraphie, vernis…) ; chaque îlot et ses trous font un seul contour. Rend les UUID créés.
std::vector<std::string> poserMotifGraphique(ConceptionElec& c, const Polygones& motif, Couche couche, const std::string& reseau = {});
// Logo tiré d'une image en niveaux de gris : seuil, puis carrés marchants (vectoriser) ou pixels réunis
Polygones logoDepuisImage(const std::vector<std::uint8_t>& gris, int largeur, int hauteur, int seuil, double mmParPixel, Pt origine, bool vectoriser = true);

// ---------- 5.15, 5.31 · Cotes ----------
// Géométrie d'une cote : traits (lignes d'attache, ligne de cote, flèches, arcs en segments), point et angle du texte
struct GeometrieCote {
  std::vector<std::pair<Pt, Pt>> traits;
  Pt texte;
  double angle = 0;  // degrés, sens direct à l'écran
  std::string valeur;
};
GeometrieCote geometrieCote(const Cote& c);
// Cote posée (rend son UUID) ; en ligne de base, chaque nouvelle cote part du premier point de la précédente
std::string poserCote(ConceptionElec& c, GenreCote genre, const std::vector<Pt>& points, Nm decalage = 2'000'000);

// ---------- 5.15 · Gabarits de carte ----------
struct GabaritCarte {
  std::string nom, description;
  Polygone contour;            // repère du gabarit, coin haut gauche en (0, 0)
  std::vector<Pt> trous;       // trous de fixation
  Nm diametreTrous = 3'200'000;
};
const std::vector<GabaritCarte>& gabaritsCarte();
void appliquerGabaritCarte(ConceptionElec& c, const GabaritCarte& g);

// ---------- 5.32 · Réglages repris d'une autre carte ----------
struct SectionsReglages {
  bool regles = false, classes = false, contraintes = false, empilage = false, couches = false, vues = false, fabrication = false, serigraphie = false;
};
void reprendreReglages(ConceptionElec& c, const ConceptionElec& source, const SectionsReglages& s);

}  // namespace prom::ecad

// Prométhée : schéma de câblage de faisceau, planche, longueurs et fabrication (lot L19 de la 0.34 ; 19.10 à 19.13 ;
// CAB-01 à CAB-13, MEC-09, MEC-18).
//
// Un faisceau relie des connecteurs (connecteurs de carte, dont les broches portent les réseaux de la carte, borniers,
// connecteurs volants) par des fils numérotés, seuls ou dans des câbles (blindage et drain, paires torsadées),
// avec épissures et cavaliers. Sa connectivité est un graphe de fils et de cavités, d'où sortent la liste des fils,
// la planche, la nomenclature et les programmes des machines. Câbles, contacts, connecteurs et protections viennent
// d'une bibliothèque en fichier de données (les huit désignations de l'atelier électrique d'abord, format versionné et
// migré). Le faisceau est gardé dans le schéma du projet électrique (extensions « cablage ») ; un folio « Câblage de
// faisceau » le dessine d'office.
//
// La planche part de la topologie (nœuds et tronçons) : le tronc, plus long chemin de l'arbre, à l'horizontale, les
// branches de part et d'autre à leur longueur ; elle s'imprime à l'échelle 1 (SVG en millimètres, PDF en pages
// assemblées sur repères). Chaque fil garde une longueur de référence et sa provenance (saisie, planche, topologie,
// 3D) ; un circuit continu se résout par analyse nodale sur ces longueurs. Le plan de fabrication, les contrôles, le
// classeur XLSX, les fichiers des machines (gabarits modifiables) et le devis en sortent.
// Licence GPL-3.0-only.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/electrique.hpp"

namespace prom::elec {

// ======================================================================================================================
// Bibliothèque (19.10)
// ======================================================================================================================
inline constexpr int kVersionBibliothequeCablage = 2;
struct ConducteurBib {
  std::string repere, couleur;  // « 1 », « BU » ; couleur CEI 60757, « BU/WH » (deux teintes), « +YE » : liseré
  int paire = 0;                // paire torsadée (1, 2…) ; 0 : aucune
  bool drain = false;           // fil de drain du blindage (nu)
  friend bool operator==(const ConducteurBib&, const ConducteurBib&) = default;
};
struct EntreeCable {
  std::string designation, description, norme;
  std::vector<ConducteurBib> conducteurs;  // vide : « conducteurs » numérotés selon HD 308 (couleurs normalisées)
  int nombreConducteurs = 3;               // quand la liste est vide
  double section = 1.5;                    // mm² d'un conducteur
  bool blinde = false, drain = false;      // drain : fil nu le long du blindage
  double diametre = 0;                     // mm, gaine extérieure (0 : d'après les conducteurs)
  double diametreConducteur = 0;           // mm, isolant compris (0 : d'après la section)
  double resistanceLineique = 0;           // mΩ/m d'un conducteur (0 : cuivre recuit d'après la section)
  double courantAdmissible = 0;            // A par conducteur (0 : d'après la section, à l'air libre)
  double rayonCourbure = 0;                // mm minimal (0 : 10 fois le diamètre)
  double tension = 0;                      // V assignés
  double prixMetre = 0;                    // € HT
  std::vector<std::string> pieces;         // pièces associées (presse-étoupe, manchon de reprise…)
  friend bool operator==(const EntreeCable&, const EntreeCable&) = default;
};
struct EntreeContact {
  std::string reference, fabricant, genre = "femelle";
  double sectionMin = 0, sectionMax = 0;  // mm² acceptés
  double denudage = 0, recul = 0;         // mm : longueur dénudée, recul de l'isolant
  double prix = 0;
  friend bool operator==(const EntreeContact&, const EntreeContact&) = default;
};
struct EntreeConnecteurCablage {
  std::string reference, fabricant, description;
  int cavites = 0;
  bool etanche = false;               // chaque fil a son joint, chaque cavité vide son bouchon
  std::vector<std::string> contacts;  // contacts permis (vide : tous)
  std::string joint, bouchon;         // références par défaut
  double prix = 0;
  friend bool operator==(const EntreeConnecteurCablage&, const EntreeConnecteurCablage&) = default;
};
struct EntreeProtection {
  std::string reference, genre = "gaine", description;  // genre : « gaine », « ruban », « manchon », « étiquette »
  double diametreMin = 0, diametreMax = 0;               // mm de faisceau couverts
  double epaisseur = 0;                                  // mm (coupe)
  double largeur = 0, recouvrement = 0.5;                // ruban : largeur (mm), part recouverte à chaque tour
  double prixMetre = 0, prix = 0;                        // € HT au mètre ; à la pièce (manchon, étiquette)
  friend bool operator==(const EntreeProtection&, const EntreeProtection&) = default;
};
struct BibliothequeCablage {
  int version = kVersionBibliothequeCablage;
  std::vector<EntreeCable> cables;
  std::vector<EntreeContact> contacts;
  std::vector<EntreeConnecteurCablage> connecteurs;
  std::vector<EntreeProtection> protections;
  const EntreeCable* cable(const std::string& designation) const;
  const EntreeContact* contact(const std::string& reference) const;
  const EntreeConnecteurCablage* connecteur(const std::string& reference) const;
  const EntreeProtection* protection(const std::string& reference) const;
  friend bool operator==(const BibliothequeCablage&, const BibliothequeCablage&) = default;
};
// Les huit désignations de l'atelier électrique (H07RN-F, H05VV-F, H05RR-F, U-1000 R2V, U-1000 AR2V, LiYY, LiYCY
// blindé, FR-N1X1G1), des câbles de données (LiYCY-TP à paires), des fils seuls, contacts, connecteurs, protections
const BibliothequeCablage& bibliothequeCablageIntegree();
// Lecture : version 1 (désignation, nombre de conducteurs, section, blindage) migrée vers la version 2 (conducteurs
// détaillés, paires, drain, électrique) ; « migrations » dit ce qui a changé
std::optional<BibliothequeCablage> lireBibliothequeCablage(const std::string& json, std::vector<std::string>* migrations = nullptr, std::string* erreur = nullptr);
std::string texteBibliothequeCablage(const BibliothequeCablage& b);
// Conducteurs effectifs d'une entrée (liste, sinon HD 308 : couleurs normalisées, vert-jaune compris)
std::vector<ConducteurBib> conducteursCable(const EntreeCable& c);
double diametreFil(double section);                       // mm, fil seul isolé (FLRY-B, H05V-K)
double diametreFilSeul(const BibliothequeCablage& b, double section);  // celui de la bibliothèque s'il est donné
double resistanceCuivre(double section);                  // mΩ/m à 20 °C
double courantAdmissibleFil(double section);              // A, fil seul à l'air libre, 30 °C
double resistanceLineique(const EntreeCable& c);          // mΩ/m
double courantAdmissibleCable(const EntreeCable& c);      // A
double diametreCable(const EntreeCable& c);               // mm
std::string couleurAffichage(const std::string& code);  // « #RRGGBB » de la première teinte d'un code CEI 60757
// Contact proposé pour une section : parmi ceux qui l'acceptent (et que le connecteur permet), la plage la plus
// étroite ; nul si aucun
const EntreeContact* contactPour(const BibliothequeCablage& b, double section, const EntreeConnecteurCablage* connecteur = nullptr);
// Protections du genre donné dont la plage couvre ce diamètre, la plus serrée d'abord
std::vector<const EntreeProtection*> protectionsPour(const BibliothequeCablage& b, double diametre, const std::string& genre);

// ======================================================================================================================
// Faisceau (19.10)
// ======================================================================================================================
enum class ProvenanceLongueur : std::uint8_t { Aucune, Saisie, Planche, Topologie, Trace3D };
const char* nomProvenance(ProvenanceLongueur p);  // « à mesurer », « saisie », « planche », « topologie », « 3D »
struct CaviteCablage {
  std::string numero;                 // « 1 », « A3 »
  std::string reseau;                 // connecteur de carte : réseau de la broche
  std::string contact;                // contact serti (référence)
  std::string joint, bouchon;         // connecteur étanche : joint du fil, bouchon d'une cavité vide
  double denudage = 0, recul = 0;     // mm (0 : ceux du contact)
  friend bool operator==(const CaviteCablage&, const CaviteCablage&) = default;
};
// Dessin d'un connecteur sur la planche (19.11) : projection en lignes cachées de son modèle 3D (celle de la mise en
// plan, de face ou de côté), en mm, y vers le haut, centrée sur son point d'attache ; tournée et répétée à volonté,
// numéros de cavités compris. Calculée par l'interface (module de géométrie) et gardée avec le connecteur.
struct DessinConnecteur {
  std::string vue = "face";  // « face », « côté »
  std::vector<std::vector<std::pair<double, double>>> vus, caches;
  std::vector<std::pair<std::string, std::pair<double, double>>> cavites;  // numéro → position
  double rotation = 0;       // degrés, sens direct
  int repetitions = 1;       // copies côte à côte (rangée de boîtiers identiques)
  double largeur = 0, hauteur = 0;
  friend bool operator==(const DessinConnecteur&, const DessinConnecteur&) = default;
};
struct ConnecteurCablage {
  std::string uuid, repere;           // « J3 », « X1 »
  std::string reference;              // entrée de la bibliothèque (vide : générique)
  bool carte = false;                 // connecteur d'une carte
  int conception = -1;                // conception électronique du projet
  std::string composant;              // UUID du composant (carte : le connecteur ; schéma électrique : le bornier)
  bool etanche = false;
  std::vector<CaviteCablage> cavites;
  ecad::Pt position;                  // sur le folio de câblage (nm)
  std::optional<DessinConnecteur> dessin;  // sur la planche, à la place de la boîte
  const CaviteCablage* cavite(const std::string& numero) const;
  CaviteCablage* cavite(const std::string& numero);
  friend bool operator==(const ConnecteurCablage&, const ConnecteurCablage&) = default;
};
struct FilCablage {
  std::string uuid, numero;
  double section = 0.5;               // mm²
  std::string couleur;                // « BU », « BU/WH », « BK/RD/WH+YE » (trois teintes et un liseré)
  std::string cable;                  // UUID du câble qui le porte (vide : fil seul)
  int conducteur = -1;                // rang dans le câble
  int paire = 0;                      // paire torsadée
  bool drain = false;
  std::string de, deCavite, vers, versCavite;  // repères des connecteurs et cavités (vide : en l'air)
  std::string surFil;                 // le fil s'arrête sur cet autre fil (numéro) : épissure posée d'elle-même
  std::string reseau;                 // réseau de la carte qu'il porte
  bool cavalier = false;              // relie deux cavités du même connecteur
  double longueur = 0;                // m : longueur de référence
  ProvenanceLongueur provenance = ProvenanceLongueur::Aucune;
  double longueurForcee = 0;          // m (0 : celle du chemin)
  std::vector<std::string> chemin;    // tronçons imposés (vide : le plus court sur la topologie)
  friend bool operator==(const FilCablage&, const FilCablage&) = default;
};
struct CableCablage {
  std::string uuid, reference;        // « W1 »
  std::string designation;            // entrée de la bibliothèque
  std::string de, vers;               // connecteurs
  double longueur = 0;                // m
  ProvenanceLongueur provenance = ProvenanceLongueur::Aucune;
  friend bool operator==(const CableCablage&, const CableCablage&) = default;
};
struct EpissureCablage {
  std::string uuid, repere;           // « SP1 »
  std::vector<std::string> fils;      // numéros
  std::string noeud;                  // nœud de la topologie où elle se trouve (vide : au milieu du premier tronçon)
  friend bool operator==(const EpissureCablage&, const EpissureCablage&) = default;
};
struct NoeudCablage {
  std::string uuid, nom;              // « N1 » ; extrémité : le repère du connecteur
  std::string connecteur;             // repère du connecteur posé ici (vide : dérivation)
  double x = 0, y = 0;                // mm sur la planche
  friend bool operator==(const NoeudCablage&, const NoeudCablage&) = default;
};
struct TronconCablage {
  std::string uuid, a, b;             // nœuds
  double longueur = 0;                // mm réels (saisis) ; 0 : longueur dessinée
  bool coupure = false;               // symbole de coupure : la longueur dessinée n'est pas la réelle
  friend bool operator==(const TronconCablage&, const TronconCablage&) = default;
};
struct ProtectionPosee {
  std::string troncon, reference;     // entrée de la bibliothèque
  double debut = 0, fin = 0;          // mm depuis le nœud « a » du tronçon (fin 0 : tout le tronçon)
  double decalage = 0;                // mm : décalage des protections superposées
  std::string texte;                  // étiquette : lignes séparées par « \n », elles suivent le tronçon
  friend bool operator==(const ProtectionPosee&, const ProtectionPosee&) = default;
};
struct Cablage {
  std::string uuid, nom = "Faisceau";
  std::vector<ConnecteurCablage> connecteurs;
  std::vector<CableCablage> cables;
  std::vector<FilCablage> fils;
  std::vector<EpissureCablage> epissures;
  std::vector<NoeudCablage> noeuds;
  std::vector<TronconCablage> troncons;
  std::vector<ProtectionPosee> protections;
  double foisonnement = 1.0;          // facteur sur le diamètre de l'empilement
  double surlongueur = 50;            // mm par extrémité (dénudage, reprise)
  ConnecteurCablage* connecteur(const std::string& repere);
  const ConnecteurCablage* connecteur(const std::string& repere) const;
  FilCablage* fil(const std::string& numero);
  const FilCablage* fil(const std::string& numero) const;
  friend bool operator==(const Cablage&, const Cablage&) = default;
};
nlohmann::ordered_json cablageVersJson(const Cablage& c);
Cablage cablageDepuisJson(const nlohmann::ordered_json& j);
// Faisceaux du projet (gardés dans p.schema.extensions["cablage"])
std::vector<Cablage> cablages(const ProjetElectrique& p);
void ecrireCablages(ProjetElectrique& p, const std::vector<Cablage>& v);

// Connecteur d'une carte : une cavité par broche du composant « repere », chacune au réseau de sa broche
std::optional<ConnecteurCablage> connecteurDeCarte(const ecad::ConceptionElec& carte, int conception, const std::string& repere);
// Bornier (« X1 ») ou appareil du schéma électrique : une cavité par borne ; sinon connecteur générique à « cavites »
ConnecteurCablage connecteurDuSchema(const ProjetElectrique& p, const std::string& repere, int cavites = 0);
// Pose un câble de la bibliothèque entre deux connecteurs du faisceau : un fil par conducteur (drain compris), aux
// cavités données dans l'ordre (vide : les premières libres) ; le drain va à la cavité suivante du connecteur
// « vers » s'il en reste une, son autre bout isolé (blindage repris d'un seul côté). Les fils prennent le réseau de
// la cavité de carte où ils arrivent, et un numéro. Rend la référence du câble (« W1 »), vide si l'entrée manque.
std::string poserCable(Cablage& h, const BibliothequeCablage& b, const std::string& designation, const std::string& de, const std::string& vers,
                       const std::vector<std::string>& cavitesDe = {}, const std::vector<std::string>& cavitesVers = {});
// Fil seul ; « vers » vide et « surFil » donné : il s'arrête sur cet autre fil. Rend son numéro.
std::string poserFil(Cablage& h, double section, const std::string& couleur, const std::string& de, const std::string& deCavite, const std::string& vers,
                     const std::string& versCavite, const std::string& surFil = {});
// Cavalier entre deux cavités du même connecteur, à sa longueur saisie (m)
std::string poserCavalier(Cablage& h, const std::string& connecteur, const std::string& a, const std::string& b, double section, double longueur);
// Épissures posées d'elles-mêmes : chaque fil qui s'arrête sur un autre en crée une (ou rejoint la sienne) ; rend le
// nombre d'épissures
int poserEpissures(Cablage& h);
// Chaque cavité occupée reçoit le contact qui accepte la section de son fil (et que le connecteur permet), dénudage
// et recul de l'isolant ; un connecteur étanche reçoit les joints, ses cavités vides leurs bouchons. Les cavités qui
// ont déjà un contact convenable le gardent. Rend le nombre de cavités servies.
int affecterContacts(Cablage& h, const BibliothequeCablage& b);
// Une entrée de la bibliothèque changée : les câbles posés de cette désignation reprennent sections et couleurs
// (l'appelant en fait une commande annulable) ; rend le nombre de fils mis à jour
int mettreAJourCables(Cablage& h, const BibliothequeCablage& b);
// Graphe de fils et de cavités : les équipotentielles (« J3:1 », « X1:1 », « SP1 »)
std::vector<std::vector<std::string>> equipotentielles(const Cablage& h);

struct LigneFil {
  std::string numero, de, vers, couleur, cable, reseau;  // de « J3:1 »
  double section = 0, longueur = 0;                      // mm², m
  std::string provenance, contactDe, contactVers;
  double denudageDe = 0, denudageVers = 0;               // mm
  int paire = 0;
  bool drain = false, cavalier = false;
};
std::vector<LigneFil> listeFils(const Cablage& h, const BibliothequeCablage& b);

// ======================================================================================================================
// Planche (19.11)
// ======================================================================================================================
// Rapport du diamètre du plus petit cercle contenant n cercles égaux à leur diamètre (empilement optimal connu) ;
// au-delà de la table, d'après la densité de l'empilement hexagonal
double rapportEmpilement(int n);
// Diamètre d'un faisceau de fils (mm) : empilement des cercles, foisonnement appliqué
double diametreFaisceau(const std::vector<double>& diametres, double foisonnement = 1.0);
// Topologie par défaut tirée de la connectivité : un nœud par connecteur ; deux connecteurs reliés directement
// partagent un tronçon (au-delà de deux, une dérivation commune) ; longueur des câbles, sinon 500 mm
void topologieParDefaut(Cablage& h, double longueurDefaut = 500);
// Chemin de tronçons d'un nœud à l'autre (le plus court, Dijkstra) ; vide si non reliés
std::vector<std::string> cheminTroncons(const Cablage& h, const std::string& noeudA, const std::string& noeudB);
double longueurTroncon(const Cablage& h, const TronconCablage& t);  // mm : saisie, sinon dessinée
// Longueurs des fils depuis les tronçons (chemin imposé ou plus court, surlongueurs aux extrémités), provenance
// « planche » ; une longueur forcée l'emporte ; rend le nombre de fils mis à jour
int longueursDepuisPlanche(Cablage& h);
// Fils de chaque tronçon et leur diamètre (mm)
struct OccupationTroncon {
  std::string troncon;
  std::vector<std::string> fils;
  double diametre = 0;
};
std::vector<OccupationTroncon> occupationTroncons(const Cablage& h, const BibliothequeCablage& b);
// Ruban posé en spirale : longueur de ruban (mm) pour couvrir « longueur » mm d'un faisceau de « diametre » mm
double longueurRuban(double diametre, double longueur, double largeur, double recouvrement);

struct PlancheDessinee {
  struct Noeud {
    std::string uuid, nom, connecteur;
    double x = 0, y = 0;
  };
  struct Troncon {
    std::string uuid;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // mm
    double longueur = 0, diametre = 0;      // réelle (cote), diamètre du faisceau
    bool tronc = false, coupure = false;
  };
  std::vector<Noeud> noeuds;
  std::vector<Troncon> troncons;
  double largeur = 0, hauteur = 0;  // mm, marges comprises
  std::vector<std::string> tronc;   // nœuds du tronc, dans l'ordre
};
// Tronc (plus long chemin de l'arbre) à l'horizontale ; branches de part et d'autre, à leur longueur. « garder » :
// les nœuds déjà placés à la main gardent leur place.
PlancheDessinee dessinerPlanche(Cablage& h, const BibliothequeCablage& b, bool garder = false);
// SVG à l'échelle donnée (1 : un millimètre de la planche mesure un millimètre à l'impression) : tronçons à leur
// diamètre, cotes de longueur, coupures, connecteurs et leurs tableaux de cavités, protections et étiquettes
std::string svgPlanche(const Cablage& h, const BibliothequeCablage& b, const PlancheDessinee& d, double echelle = 1.0);
// Pages assemblées sur repères : format (« A4 », « A3 »), recouvrement (mm) ; chaque page à l'échelle 1 porte des
// croix de repérage et son rang (« ligne 1, colonne 2 ») ; PDF d'autant de pages
struct PagesPlanche {
  int colonnes = 0, lignes = 0;
  std::string pdf;
};
PagesPlanche pagesPlanche(const Cablage& h, const BibliothequeCablage& b, const PlancheDessinee& d, const std::string& format = "A4", double recouvrement = 10);
// Coupe d'un tronçon : cercles des fils à leur couleur et à leur diamètre, enveloppe, protections et épaisseurs
std::string svgCoupe(const Cablage& h, const BibliothequeCablage& b, const std::string& troncon);
// La planche de l'atelier électrique (mode Faisceau) tirée du faisceau, même UUID : un nœud par nœud de la
// topologie à sa place, relié à l'appareil du schéma de même repère, une branche par tronçon à sa longueur, ses
// protections nommées
Faisceau planchePourAtelier(const Cablage& h, const ProjetElectrique& p);
// Les nœuds rangés à la souris sur la planche de l'atelier et les longueurs saisies de ses branches reviennent au
// faisceau ; rend le nombre de nœuds et de tronçons repris
int reprendrePlanche(Cablage& h, const Faisceau& f);

// ======================================================================================================================
// Topologie, 3D, circuits continus (19.12)
// ======================================================================================================================
// Feuille de topologie : espaces (armoire, pupitre, châssis, porte) et passages entre leurs points, à l'échelle (mm)
struct PointTopologie {
  std::string id, espace;
  double x = 0, y = 0;
  friend bool operator==(const PointTopologie&, const PointTopologie&) = default;
};
struct PassageTopologie {
  std::string a, b;
  double longueur = 0;  // mm ; 0 : distance des points
  bool ouvert = true;
  friend bool operator==(const PassageTopologie&, const PassageTopologie&) = default;
};
struct Topologie {
  std::vector<std::string> espaces;
  std::vector<PointTopologie> points;
  std::vector<PassageTopologie> passages;
  friend bool operator==(const Topologie&, const Topologie&) = default;
};
struct RouteTopologie {
  std::vector<std::string> points;
  double longueur = 0;  // mm
  bool trouvee = false;
};
// Fichier de données : {"espaces":[…],"points":[{"id","espace","x","y"}],"passages":[{"a","b","longueur","ouvert"}]}
std::optional<Topologie> lireTopologie(const std::string& json, std::string* erreur = nullptr);
std::string texteTopologie(const Topologie& t);
// Plus court chemin de Dijkstra, passages imposés (points par où passer, dans l'ordre) compris
RouteTopologie routerTopologie(const Topologie& t, const std::string& de, const std::string& vers, const std::vector<std::string>& imposes = {});
// La longueur de la route (plus les surlongueurs) remplace « à mesurer » dans la liste des câbles de l'atelier
// électrique et dans le câble du faisceau ; rend vrai si un câble a été mis à jour
bool longueurDepuisTopologie(ProjetElectrique& p, const std::string& cable, const RouteTopologie& r, double surlongueur = 0);
// Route 3D par points d'attache, raccordée à chaque coude par un arc de rayon « rayon » (mm) : longueur exacte de la
// courbe (droites et arcs) ; les coudes trop serrés pour le rayon sont signalés
struct Route3D {
  double longueur = 0;  // mm
  std::vector<std::string> alertes;
  std::vector<std::array<double, 3>> points;  // polyligne échantillonnée de la courbe (mm)
};
Route3D routerEn3D(const std::vector<std::array<double, 3>>& attaches, double rayon);
// Longueur venue de la 3D sur un câble du faisceau (m) : ses fils la prennent, provenance « 3D »
int longueurDepuis3D(Cablage& h, const std::string& cable, double longueurMm);
// Faisceau tracé en 3D dans l'assemblage : une branche par tronçon, entre deux nœuds nommés (repère du connecteur ou
// dérivation), par ses points d'attache. Mis à plat en planche : la topologie et les longueurs (courbes raccordées
// au rayon) sont gardées, les fils prennent leur longueur de ces tronçons, provenance « 3D ». Rend le nombre de
// tronçons ; les alertes de coudes trop serrés vont dans « alertes ».
struct Branche3D {
  std::string a, b;
  std::vector<std::array<double, 3>> points;  // mm, de a vers b, extrémités comprises
  double rayon = 0;
};
int mettreAPlat(Cablage& h, const std::vector<Branche3D>& branches, std::vector<std::string>* alertes = nullptr);
std::optional<std::vector<Branche3D>> lireBranches3D(const std::string& json, std::string* erreur = nullptr);  // {"branches":[{"a","b","rayon","points":[[x,y,z]…]}]}
// Circuit continu sur les longueurs des fils : source (V), charges à courant, résistance ou puissance constants
struct ChargeContinue {
  std::string nom;
  double courant = 0, resistance = 0, puissance = 0;  // un seul non nul
};
struct CircuitContinu {
  double tension = 12;              // V à la source
  std::vector<std::string> aller;   // fils de la source à la charge (numéros), en série
  std::vector<std::string> retour;  // fils du retour
  ChargeContinue charge;
  double fusible = 0;               // A, calibre du fusible de la source (0 : aucun)
  double chuteMax = 5;              // % admis
};
struct BilanContinu {
  double courant = 0, chute = 0, pourcent = 0, tensionCharge = 0;
  int iterations = 0;
  bool depasse = false;             // chute au-delà de chuteMax
  std::vector<std::string> alertes; // chute, section contre courant admissible, fusible contre section
};
BilanContinu resoudreContinu(const Cablage& h, const BibliothequeCablage& b, const CircuitContinu& c);
// Analyse nodale générale : nœuds numérotés de 1 à n (0 : masse), résistances, sources de courant, une source de
// tension idéale ; rend les tensions des nœuds (vide si singulier)
struct ElementNodal {
  int a = 0, b = 0;
  double resistance = 0;  // Ω (> 0) ; sinon source de courant de a vers b
  double courant = 0;
};
std::vector<double> analyseNodale(int noeuds, const std::vector<ElementNodal>& elements, int noeudSource, double tensionSource);

// ======================================================================================================================
// Fabrication (19.13)
// ======================================================================================================================
struct DefautFaisceau {
  std::string genre, objet, message;  // genre : « fil sans extrémité », « contact hors plage », « étanchéité »,
                                      // « protection hors plage », « chute de tension »
};
std::vector<DefautFaisceau> verifierFaisceau(const Cablage& h, const BibliothequeCablage& b, const std::vector<CircuitContinu>& circuits = {});
// Nomenclature du faisceau : fils et câbles « selon besoin » (longueur totale), contacts, joints, bouchons,
// connecteurs, protections (ruban compté en spirale), cavaliers ; bulle de repère par ligne
struct LigneNomenclatureCablage {
  int bulle = 0;
  std::string reference, designation, unite;  // unite : « m » ou « pce »
  double quantite = 0, prixUnitaire = 0;
  bool selonBesoin = false;
};
std::vector<LigneNomenclatureCablage> nomenclatureCablage(const Cablage& h, const BibliothequeCablage& b);
// Classeur XLSX : nomenclature, liste des fils, étiquettes, gaines (une feuille chacune)
std::string xlsxFaisceau(const Cablage& h, const BibliothequeCablage& b);
// Plan de fabrication (gabarit de plan de L8) : planche cotée, liste des fils et tables de connexion sur plusieurs
// pages, nomenclature avec bulles ; PDF
std::string pdfPlanFaisceau(const Cablage& h, const BibliothequeCablage& b, const std::string& titre, const std::string& revision = "A");
// Fichiers des machines et des testeurs, d'après un gabarit modifiable : en-tête, ligne par fil (ou par connexion),
// pied ; champs entre accolades : {numero} {section} {couleur} {longueur_mm} {denudage_de} {denudage_vers}
// {contact_de} {contact_vers} {de} {vers} {cavite_de} {cavite_vers} {connecteur_de} {connecteur_vers} {cable} {reseau}
struct GabaritMachine {
  std::string nom, genre;        // genre : « coupe », « sertissage », « étiquettes », « testeur »
  std::string extension = "csv";
  std::string entete, ligne, pied;
  friend bool operator==(const GabaritMachine&, const GabaritMachine&) = default;
};
const std::vector<GabaritMachine>& gabaritsMachines();  // Komax, Schleuniger, étiquettes, Cirris, DIT-MCO
std::optional<GabaritMachine> lireGabaritMachine(const std::string& json, std::string* erreur = nullptr);
std::string texteGabaritMachine(const GabaritMachine& g);
std::string fichierMachine(const Cablage& h, const BibliothequeCablage& b, const GabaritMachine& g);
// Devis du faisceau : matériel aux prix (bibliothèque, ou ceux de 19.5 donnés par référence), main-d'œuvre d'après les
// temps unitaires de l'équipe (s) : coupe par fil, sertissage par contact, pose de gaine par protection, contrôle
// par connexion, au taux horaire
struct TempsUnitaires {
  double coupe = 12, sertissage = 20, gaine = 45, controle = 6, etiquette = 15;  // s
  double tauxHoraire = 55;                                                      // € HT
};
std::optional<TempsUnitaires> lireTempsUnitaires(const std::string& json, std::string* erreur = nullptr);
struct DevisFaisceau {
  int fils = 0, sertissages = 0, protections = 0, connexions = 0, etiquettes = 0;
  double secondes = 0, mainOeuvre = 0, materiel = 0, total = 0;
  std::vector<std::pair<std::string, double>> detailTemps;  // opération → secondes
  std::vector<LigneNomenclatureCablage> lignes;
};
DevisFaisceau devisFaisceau(const Cablage& h, const BibliothequeCablage& b, const TempsUnitaires& t, const std::map<std::string, double>& prix = {});
std::string csvDevis(const DevisFaisceau& d);
std::string pdfDevis(const DevisFaisceau& d, const std::string& titre);

// Folio « Câblage de faisceau » dessiné d'office : boîtes des connecteurs avec leurs cavités, fils (numéro, section,
// couleur, réseau), câbles cerclés, blindage et drain, paires torsadées, épissures (positions en mm sur le folio)
struct DispositionCablage {
  struct Boite {
    std::string repere, titre;
    double x = 0, y = 0, largeur = 0, hauteur = 0;
    std::vector<std::pair<std::string, double>> cavites;  // numéro → ordonnée de la cavité
    bool droite = false;                                    // cavités sur le bord droit
  };
  struct Trait {
    std::string numero, texte, couleur;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int paire = 0;
    bool drain = false;
  };
  struct Cadre {
    std::string reference, texte;
    double x = 0, y0 = 0, y1 = 0;  // ellipse du câble autour de ses fils
    bool blinde = false;
  };
  std::vector<Boite> boites;
  std::vector<Trait> traits;
  std::vector<Cadre> cables;
  std::vector<std::pair<std::string, std::pair<double, double>>> epissures;
};
DispositionCablage disposerFolioCablage(const Cablage& h, const BibliothequeCablage& b, double largeur, double hauteur);

}  // namespace prom::elec

// Prométhée : circuit imprimé. Couches, empilage, empreintes générées par un moteur paramétrique (pastilles,
// sérigraphie, encombrement, corps 3D), empreintes placées (même UUID que le composant du schéma), pistes, vias,
// zones de cuivre, textes, contour de carte et règles de conception (globales, puis par classe de réseau).
// Licence GPL-3.0-only.
#pragma once

#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "promethee/contraintes.hpp"
#include "promethee/ecad.hpp"

namespace prom::ecad {

// Couches de la carte. Les quinze premières gardent leur rang depuis la 0.20 ; la 0.34 (8.20) ajoute les cuivres
// internes 3 à 30 (32 cuivres en tout, comme KiCad), puis les couches techniques : colle (8.14), vernis de
// protection (8.14), dessins et commentaires, et neuf couches personnelles (7.22).
enum class Couche : std::uint8_t {
  CuivreDessus, CuivreInt1, CuivreInt2, CuivreDessous, VernisDessus, VernisDessous, PateDessus, PateDessous,
  SerigraphieDessus, SerigraphieDessous, Contour, EncombrementDessus, EncombrementDessous, FabricationDessus, FabricationDessous,
  CuivreInt3, CuivreInt4, CuivreInt5, CuivreInt6, CuivreInt7, CuivreInt8, CuivreInt9, CuivreInt10, CuivreInt11, CuivreInt12,
  CuivreInt13, CuivreInt14, CuivreInt15, CuivreInt16, CuivreInt17, CuivreInt18, CuivreInt19, CuivreInt20, CuivreInt21,
  CuivreInt22, CuivreInt23, CuivreInt24, CuivreInt25, CuivreInt26, CuivreInt27, CuivreInt28, CuivreInt29, CuivreInt30,
  ColleDessus, ColleDessous, ProtectionDessus, ProtectionDessous, Dessins, Commentaires,
  Utilisateur1, Utilisateur2, Utilisateur3, Utilisateur4, Utilisateur5, Utilisateur6, Utilisateur7, Utilisateur8, Utilisateur9
};
inline constexpr int NB_COUCHES = 58;
inline constexpr int MAX_CUIVRES = 32;
using Couches = std::uint64_t;
inline constexpr Couches bit(Couche c) { return Couches{1} << static_cast<unsigned>(c); }
const char* nomCouche(Couche c);   // « Cuivre dessus »
const char* codeCouche(Couche c);  // « F.Cu » : noms compris par tous les fabricants
std::optional<Couche> coucheDepuisCode(const std::string& code);
bool estCuivre(Couche c);
bool estCuivreInterne(Couche c);
Couche opposee(Couche c);          // dessus ↔ dessous
// Cuivres de l'empilage, du dessus vers le dessous : 1 (simple face : dessous), puis 2 à 32 (internes 1, 2, 3…)
std::vector<Couche> couchesCuivre(int nombre);
Couches tousLesCuivres(int nombre);
Couche cuivreInterne(int k);       // k de 1 à 30
// Rang physique d'un cuivre dans un empilage de « nombre » cuivres (0 : dessus, nombre − 1 : dessous) ; −1 s'il n'y
// est pas
int rangCuivre(Couche c, int nombre);
// Couches personnelles et techniques qu'un dessin peut porter (7.22) ; leur nom affiché peut être changé par carte
bool estCoucheDessin(Couche c);

// Formes de pastille : les quatre de la 0.20, puis le rectangle chanfreiné, le trapèze et la forme libre (4.24)
enum class FormePastille : std::uint8_t { Rond, Rect, Ovale, RectArrondi, Chanfreine, Trapeze, Libre };
enum class TypePastille : std::uint8_t { CMS, Traversante, NonMetallisee };
// Raccord d'une pastille à la zone de son réseau (5.11, CUI-05) : celui de la zone, rayons, plein ou aucun
enum class ConnexionZone : std::uint8_t { Heritee, Thermique, Pleine, Aucune };
// Lamage ou fraisure d'un trou (8.12, CAR-35) : diamètre et profondeur, angle de la fraisure (0 : lamage droit)
struct Lamage {
  Nm diametre = 0, profondeur = 0;
  double angle = 0;
  friend bool operator==(const Lamage&, const Lamage&) = default;
};
// Trou supplémentaire d'une pastille (4.27) : dans le repère de la pastille, rond ou oblong
struct TrouPastille {
  Pt decalage;
  Nm diametre = 0, longueur = 0;
  double rotation = 0;
  friend bool operator==(const TrouPastille&, const TrouPastille&) = default;
};
// Motif de pâte d'une pastille (4.24, EMP-26) : grille de fenêtres, écart entre elles, taux de couverture, rayon
struct MotifPate {
  int lignes = 2, colonnes = 2;
  Nm ecart = 200'000, rayon = 0;
  double couverture = 0.6;
  friend bool operator==(const MotifPate&, const MotifPate&) = default;
};
struct Pastille {
  std::string numero;
  TypePastille type = TypePastille::CMS;
  FormePastille forme = FormePastille::RectArrondi;
  Pt position;
  Nm largeur = 0, hauteur = 0;
  double rotation = 0;   // degrés
  Nm percage = 0;        // diamètre du trou (0 : CMS)
  double arrondi = 0.25; // rectangle arrondi : rayon / plus petit côté
  // Couches explicites, dans le repère de l'empreinte posée dessus (X2) : 0 les déduit du type (cuivre, vernis et pâte
  // de la face pour une CMS) ; sinon elles sont respectées partout, Gerber, DRC et 3D compris. Un doigt de connecteur
  // n'a pas de pâte, une pastille CMS d'une empreinte du dessus peut être sur B.Cu (connecteur de tranche).
  Couches couches = 0;
  // Propriété de fabrication (3.20, EMP-18), écrite dans l'attribut .AperFunction des Gerber X2 : « » (ordinaire),
  // « bga », « fiducialLocal », « fiducialGlobal », « test », « dissipateur », « castellee », « insertion »,
  // « connecteurBord » (doigt de connecteur de tranche, 4.24)
  std::string fabrication;
  // Formes de 4.24 : chanfrein (part du plus petit côté) et coins chanfreinés (bits 1 haut gauche, 2 haut droit,
  // 4 bas droit, 8 bas gauche) ; trapèze : écart de largeur entre le haut et le bas ; forme libre : contour dans le
  // repère de la pastille (centre en 0, avant rotation)
  double chanfrein = 0.2;
  int coinsChanfrein = 0xF;
  Nm trapeze = 0;
  Polygone contourLibre;
  // Perçage complet (4.27) : trou oblong de « longueurPercage » le long de l'axe x de la pastille (fente), décalé du
  // centre, tolérance du diamètre, trous en plus
  Nm longueurPercage = 0;
  Pt decalagePercage;
  Nm tolerancePercage = 0;
  std::vector<TrouPastille> trousEnPlus;
  // Vernis et pâte propres à la pastille (4.24) : marges, taux et motif de pâte, formes libres (repère de la pastille)
  std::optional<Nm> margeVernis, margePate;
  std::optional<MotifPate> motifPate;
  Polygone vernisLibre, pateLibre;
  std::string gabarit;  // pile de pastilles nommée selon l'IPC-7351 dont elle tient sa forme (4.24)
  ConnexionZone connexion = ConnexionZone::Heritee;  // 5.11
  std::optional<Lamage> lamageDessus, lamageDessous;  // 8.12
  bool fente() const { return percage > 0 && longueurPercage > percage; }
  friend bool operator==(const Pastille&, const Pastille&) = default;
};
// Pile de pastilles nommée (4.24, EMP-25) : forme, dimensions, perçage, raccord aux zones, et la règle qui en
// déduit vernis et pâte ; une pastille qui la cite suit ses changements en une commande
struct GabaritPastille {
  std::string nom;  // nom IPC-7351 (« RX160Y60D0T », « C150H90 »…)
  FormePastille forme = FormePastille::RectArrondi;
  TypePastille type = TypePastille::CMS;
  Nm largeur = 0, hauteur = 0, percage = 0, longueurPercage = 0;
  double arrondi = 0.25, chanfrein = 0.2;
  ConnexionZone connexion = ConnexionZone::Heritee;
  Nm margeVernis = 50'000, margePate = 0;
  double tauxPate = 1.0;
  friend bool operator==(const GabaritPastille&, const GabaritPastille&) = default;
};
// Nom IPC-7351 d'une pile de pastilles d'après sa forme et ses cotes (centièmes de mm)
std::string nomIPCGabarit(const GabaritPastille& g);
// Applique le gabarit à une pastille : forme, dimensions et perçage ; position, numéro et rotation gardés
void appliquerGabarit(Pastille& p, const GabaritPastille& g);
struct TraitEmpreinte {
  Couche couche = Couche::SerigraphieDessus;
  Pt a, b;
  Nm epaisseur = 120'000;
  Nm rayon = 0;  // > 0 : cercle de centre a
  friend bool operator==(const TraitEmpreinte&, const TraitEmpreinte&) = default;
};
struct Corps3D {
  Nm largeur = 0, profondeur = 0, hauteur = 0;  // le long de x, de y, au-dessus de la carte
  bool rond = false;
  std::string couleur = "#2A2D34";
  // Centre du corps dans le repère de l'empreinte (3.4, T5) : le centre de l'encombrement, rempli d'office à l'import
  // et à la génération ; l'origine d'un traversant de KiCad est souvent à la broche 1
  Pt decalage;
  friend bool operator==(const Corps3D&, const Corps3D&) = default;
};
// Volume libre (3.20, EMP-11) : un contour extrudé entre deux hauteurs au-dessus de la carte, de la couleur voulue,
// dans une empreinte (repère de l'empreinte) ou sur la carte ; il entre dans la 3D et dans le boîtier comme un corps
struct VolumeLibre {
  Polygone contour;
  Nm z0 = 0, z1 = 0;
  std::string couleur = "#7A7F8A";
  friend bool operator==(const VolumeLibre&, const VolumeLibre&) = default;
};
const char* fonctionGerberPastille(const std::string& fabrication, bool cms);  // « BGAPad,CuDef », « FiducialPad,Local »…
const std::vector<std::string>& proprietesFabricationPastille();

struct DefEmpreinte {
  std::string id, description, famille;
  int version = 1;
  std::vector<Pastille> pastilles;
  std::vector<TraitEmpreinte> traits;
  Boite encombrement;  // zone réservée au composant (courtyard)
  Corps3D corps;
  std::vector<VolumeLibre> volumes;
  friend bool operator==(const DefEmpreinte&, const DefEmpreinte&) = default;
};
// Moteur paramétrique : « R_0603 », « SOIC-8 », « QFN-32-5x5-0.5 », « PinHeader_1x04_P2.54 »…
std::optional<DefEmpreinte> genererEmpreinte(const std::string& id);
struct InfoEmpreinte {
  std::string id, famille, description;
  int pastilles = 0;
};
const std::vector<InfoEmpreinte>& catalogueEmpreintes();

// Empreinte créée par l'utilisateur (fonction KiCad « créer une empreinte ») : un tableau de pastilles en rangées et
// colonnes, au pas donné, numérotées par rangée, par colonne (barrettes doubles) ou en tour (boîtiers DIL :
// rangée du bas de gauche à droite, puis rangée du haut de droite à gauche). Sérigraphie autour des pastilles,
// repère de la broche 1, encombrement à 0,25 mm, corps à la hauteur donnée. Identifiant « perso:<nom> ».
struct SpecEmpreinte {
  enum class Numerotation : std::uint8_t { ParRangee, ParColonne, Tour };
  std::string nom = "Connecteur", description;
  TypePastille type = TypePastille::Traversante;
  FormePastille forme = FormePastille::Rond;
  int rangees = 1, colonnes = 4;
  double pasX = 2.54, pasY = 2.54;                     // mm
  double largeur = 1.7, hauteur = 1.7, percage = 1.0;  // mm (perçage ignoré en CMS)
  Numerotation numerotation = Numerotation::ParRangee;
  bool premiereCarree = true;                          // broche 1 carrée (repère au montage)
  double hauteurCorps = 8.5;                           // mm au-dessus de la carte
};
DefEmpreinte genererEmpreinteSpec(const SpecEmpreinte& s);
// Raison pour laquelle une spécification ne peut pas donner d'empreinte (B15) : perçage plus large que la pastille
// (la largeur pour une pastille ronde), couronne sous « couronneMin », dimensions hors bornes ; vide si elle convient.
// Une valeur hors bornes est refusée avec sa raison, jamais ramenée en silence.
std::string verifierSpecEmpreinte(const SpecEmpreinte& s, Nm couronneMin = 0);
std::string identifiantEmpreintePerso(const std::string& nom);  // « perso:Barrette_2x5 »

// Couche de l'empilage détaillé (8.20, CAR-04, CAR-36) : cuivre typé, diélectrique (âme ou préimprégné) ou âme
// métallique sous un diélectrique thermique
enum class GenreCoucheEmpilage : std::uint8_t { Cuivre, Dielectrique, AmeMetallique };
enum class UsageCuivre : std::uint8_t { Signal, Plan, Mixte, Cavalier };
struct CoucheEmpilage {
  GenreCoucheEmpilage genre = GenreCoucheEmpilage::Dielectrique;
  std::string nom, materiau = "FR-4";
  double epaisseur = 0;          // mm
  double permittivite = 4.4;     // εr à 1 GHz
  double pertes = 0.02;          // tan δ
  double conductivite = 0;       // W/(m·K) d'une âme métallique ou d'un diélectrique thermique
  UsageCuivre usage = UsageCuivre::Signal;
  bool preimpregne = false;      // diélectrique : préimprégné (« prepreg ») plutôt qu'âme (« core »)
  friend bool operator==(const CoucheEmpilage&, const CoucheEmpilage&) = default;
};
struct Empilage {
  int couchesCuivre = 2;  // 1 (simple face, cuivre dessous), 2 à 32
  double epaisseur = 1.6;          // mm
  double cuivre = 0.035;           // mm (35 µm)
  std::string materiau = "FR-4", finition = "HASL sans plomb", vernis = "vert", serigraphie = "blanc";
  // Empilage détaillé, du dessus vers le dessous (8.20) ; vide : déduit (cuivres de même épaisseur, diélectriques
  // égaux qui font l'épaisseur totale)
  std::vector<CoucheEmpilage> pile;
  std::string gabarit;  // gabarit d'empilage d'où il vient (dossier d'équipe)
  // Vias permises par le procédé (8.22, HDI) et ordre dans lequel le routeur les choisit d'après les couches de départ
  // et d'arrivée : « micro », « borgne », « enterree », « traversante »
  bool microVias = false, viasBorgnes = false, viasEnterrees = false;
  Nm microViaDiametre = 250'000, microViaPercage = 100'000;
  std::vector<std::string> prioriteVias{"micro", "borgne", "enterree", "traversante"};
  friend bool operator==(const Empilage&, const Empilage&) = default;
};
// Empilage détaillé d'une carte : celui qu'elle porte, sinon celui que donnent ses réglages courts
std::vector<CoucheEmpilage> pileEmpilage(const Empilage& e);
double epaisseurPile(const std::vector<CoucheEmpilage>& pile);  // mm
// Classe de réseaux : largeur et isolement propres. Le dimensionnement électrique n'est jamais déduit du nom
// des réseaux : « courant » (A, 0 = non renseigné) et « echauffement » (élévation de température admise, °C)
// sont saisis par l'utilisateur ; la largeur recommandée en découle par IPC-2221 (voir largeurIPC2221), et la
// DRC vérifie chaque piste de la classe.
struct ClasseReseau {
  std::string nom;
  Nm isolement = 200'000, largeur = 250'000, viaDiametre = 800'000, viaPercage = 400'000;
  double courant = 0;        // A, courant continu (ou efficace) à transporter ; 0 : pas de vérification
  double echauffement = 10;  // °C au-dessus de l'ambiante
  // Tension de service (5.12, V crête) : l'isolement et la ligne de fuite se lisent dans la table de la carte
  // (IPC-2221 ou CEI 60664-1) quand elle est donnée
  double tension = 0;
  std::string couleur;       // couleur des réseaux de la classe dans les vues (« #E53935 »)
  friend bool operator==(const ClasseReseau&, const ClasseReseau&) = default;
};
// Affectations des réseaux aux classes, dans l'ordre où l'utilisateur les range (R5) : un nom exact, ou un motif
// (« +*V », « USB_D? »). Les motifs s'essaient dans cet ordre, le premier qui correspond l'emporte ; l'ordre est
// gardé dans le fichier. L'interface est celle d'un dictionnaire (find, count, [], erase), parcouru dans l'ordre.
class AffectationsClasses {
 public:
  using Paire = std::pair<std::string, std::string>;  // réseau ou motif → classe
  using iterator = std::vector<Paire>::iterator;
  using const_iterator = std::vector<Paire>::const_iterator;
  AffectationsClasses() = default;
  AffectationsClasses(std::initializer_list<Paire> l);
  iterator begin() { return m_liste.begin(); }
  iterator end() { return m_liste.end(); }
  const_iterator begin() const { return m_liste.begin(); }
  const_iterator end() const { return m_liste.end(); }
  std::size_t size() const { return m_liste.size(); }
  bool empty() const { return m_liste.empty(); }
  iterator find(const std::string& nom);
  const_iterator find(const std::string& nom) const;
  std::size_t count(const std::string& nom) const { return find(nom) == end() ? 0 : 1; }
  std::string& operator[](const std::string& nom);  // ajoute à la fin s'il manque
  std::size_t erase(const std::string& nom);
  iterator erase(const_iterator it) { return m_liste.erase(it); }
  void clear() { m_liste.clear(); }
  // Déplace l'affectation « nom » au rang « rang » (0 : la première essayée)
  void deplacer(const std::string& nom, std::size_t rang);
  bool operator==(const AffectationsClasses& o) const = default;

 private:
  std::vector<Paire> m_liste;
};

// Capacités d'un fabricant hors catalogue (R3) : chaque minimum est écrit par l'utilisateur et la carte y est
// comparée comme aux profils intégrés. Un profil inconnu sans capacités complètes bloque la publication.
struct CapacitesFabricant {
  Nm isolement = 0, largeurMin = 0, percageMin = 0, anneauMin = 0, bord = 0;
  bool complet() const { return isolement > 0 && largeurMin > 0 && percageMin > 0 && anneauMin > 0 && bord > 0; }
  bool operator==(const CapacitesFabricant& o) const = default;
};

struct Regles {
  Nm isolement = 200'000, largeur = 250'000, largeurMin = 150'000, viaDiametre = 800'000, viaPercage = 400'000;
  Nm anneauMin = 130'000, percageMin = 300'000, bord = 300'000;
  Nm ecartTrous = 250'000;  // distance minimale entre les bords de deux perçages
  Nm cuivreTrou = 250'000;  // distance minimale entre un cuivre et le bord d'un perçage non métallisé (EL-03)
  int rayonsMin = 2;        // rayons thermiques qui doivent relier une pastille à sa zone (EL-02)
  std::string profil = "Standard";
  std::optional<CapacitesFabricant> capacitesPerso;  // profil personnalisé : « profil » porte alors son nom
  std::vector<ClasseReseau> classes;
  AffectationsClasses classeReseau;  // réseau ou motif → classe, dans l'ordre saisi
  ContraintesCarte avancees;  // contraintes, jeux, classes de composants, paires, accords, signaux (5.12, 8.24)
  friend bool operator==(const Regles&, const Regles&) = default;
};
// Classe d'un réseau : son nom exact, sinon le premier motif qui lui correspond, dans l'ordre saisi (« +*V »,
// « USB_D? », EL-39, R5). Une affectation exacte vers une classe qui n'existe plus laisse essayer les motifs (R10).
const ClasseReseau* classeDe(const Regles& r, const std::string& reseau);
bool motifReseau(const std::string& motif);
bool correspond(const std::string& motif, const std::string& nom);
Nm isolementEntre(const Regles& r, const std::string& a, const std::string& b);
Nm largeurPiste(const Regles& r, const std::string& reseau);
Nm viaDiametre(const Regles& r, const std::string& reseau);
Nm viaPercage(const Regles& r, const std::string& reseau);
// La plus contraignante de deux classes : la plus grande isolation, puis la piste la plus large, puis la plus
// grande via (R11 : deux affectations qui visent le même réseau gardent celle-là)
const ClasseReseau* plusStricte(const ClasseReseau* a, const ClasseReseau* b);
struct ProfilFabricant {
  std::string nom, description;
  Regles regles;
};
const std::vector<ProfilFabricant>& profilsFabricants();
// Profil contre lequel la carte se compare : un profil du catalogue par son nom, sinon le profil personnalisé
// complet que portent les règles ; vide si aucun ne convient (R3 : erreur critique DFM-20).
std::optional<ProfilFabricant> profilDeReference(const Regles& r);

// Largeur de piste recommandée par IPC-2221 (abaque de l'IPC-2221, § 6.2) pour un courant (A), une élévation de
// température admise (°C) et une épaisseur de cuivre (mm) : I = k · ΔT^0,44 · S^0,725, S en mil², k = 0,048 en
// couche externe, 0,024 en couche interne. Méthode prudente, sans dissipation par les plans voisins ; à vérifier
// par un calcul thermique pour un courant élevé ou une carte confinée.
Nm largeurIPC2221(double courant, double echauffement, double cuivreMm, bool coucheExterne);
std::string hypothesesIPC2221(double courant, double echauffement, double cuivreMm, bool coucheExterne);

// Verrou d'un objet de cuivre (5.23, ROU-29) : figé (ni la souris ni la poussée ne le bougent), protégé (la
// poussée le contourne, la souris peut encore le déplacer)
enum class Verrou : std::uint8_t { Aucun, Fige, Protege };
struct EmpreintePlacee {
  std::string composant, empreinte;  // UUID du composant ; identifiant d'empreinte
  Pt position;
  double rotation = 0;  // degrés, sens direct
  bool dessous = false, verrou = false;
  // Repère de sérigraphie déplacé à la main (5.5, C6) : sa place dans le repère de l'empreinte, qui le suit quand elle
  // bouge ; absent, il se met au-dessus de l'encombrement
  std::optional<Pt> repere;
  // Copie locale de l'empreinte retouchée sur la carte (7.24, EMP-22) : « empreinte » désigne alors la copie
  // (« SOIC-8~local~U2 ») et « copieLocale » l'empreinte de bibliothèque d'où elle vient ; l'écart reste affiché
  std::string copieLocale;
  std::string couleur;  // couleur du corps (8.13, CAR-34), vide : celle de l'empreinte
  friend bool operator==(const EmpreintePlacee&, const EmpreintePlacee&) = default;
};
struct Piste {
  std::string uuid, reseau;
  Couche couche = Couche::CuivreDessus;
  Pt a, b;
  Nm largeur = 250'000;
  // Arc de piste (8.4, ROU-13) : de a à b en passant par « milieu »
  std::optional<Pt> milieu;
  // Segment effilé (5.22, ROU-26) : largeur en b ; 0 garde « largeur » d'un bout à l'autre. Bouts carrés : écrits en
  // région Gerber plutôt qu'en tracé à bouts ronds
  Nm largeurFin = 0;
  bool boutsCarres = false;
  Verrou verrou = Verrou::Aucun;
  std::string motif;  // motif d'accord (5.22) dont ce segment fait partie
  friend bool operator==(const Piste&, const Piste&) = default;
};
// Segments d'une piste : le segment lui-même, ou l'arc découpé en cordes d'au plus « fleche » de flèche
std::vector<std::pair<Pt, Pt>> segmentsPiste(const Piste& p, Nm fleche = 2'000);
double longueurPiste(const Piste& p);  // nm, arc compris
// Cercle passant par trois points (arc de piste) ; faux s'ils sont alignés
bool cercleParTroisPoints(Pt a, Pt m, Pt b, double& cx, double& cy, double& r);
// Contour de cuivre d'une piste (bouts ronds, ou trapèze d'un segment effilé, ou arc), gonflé de « marge »
Polygones formePiste(const Piste& p, Nm marge = 0);

// Via (8.22, CAR-06) : couches de départ et d'arrivée (traversante, borgne, enterrée, micro-via laser), pile de
// pastilles (diamètres du dessus, des couches internes, du dessous), remplissage IPC-4761, couronnes non
// fonctionnelles retirées, contre-perçage (8.23)
enum class TypeVia : std::uint8_t { Traversante, Borgne, Enterree, Micro };
struct ContrePercage {
  bool dessous = true;                  // percé par le dessous (sinon par le dessus)
  Couche jusqua = Couche::CuivreInt1;   // dernière couche gardée reliée : le contre-perçage s'arrête juste avant
  Nm diametre = 0;                      // 0 : perçage de la via + 0,2 mm
  Nm talonMax = 200'000;                // longueur de moignon tolérée
  friend bool operator==(const ContrePercage&, const ContrePercage&) = default;
};
struct Via {
  std::string uuid, reseau;
  Pt p;
  Nm diametre = 800'000, percage = 400'000;
  Couche de = Couche::CuivreDessus, vers = Couche::CuivreDessous;
  TypeVia type = TypeVia::Traversante;
  bool laser = false;
  std::string remplissage;            // IPC-4761 : « » (aucun), « I-a », « II-b », « III-a », « IV-b », « V », « VI », « VII »
  Nm diametreInterne = 0, diametreDessous = 0;  // 0 : « diametre »
  bool retirerCouronnes = false;      // couronnes des couches internes non reliées retirées (8.23)
  std::optional<ContrePercage> contrePercage;
  Verrou verrou = Verrou::Aucun;
  std::string groupe;                 // couture : « couture:<zone> » (5.11, 5.25)
  friend bool operator==(const Via&, const Via&) = default;
};
// Cuivres que la via relie, d'après ses couches de départ et d'arrivée et l'empilage
Couches couchesVia(const Via& v, int couchesCuivre);
Nm diametreViaSur(const Via& v, Couche c, int couchesCuivre);
// Via que le tracé pose pour passer de « de » à « vers » (8.22, ROU-05) : la première permise dans l'ordre de priorité
// de l'empilage, dimensions des règles du réseau ou de la micro-via
Via viaPour(const Empilage& e, const Regles& r, const std::string& reseau, Couche de, Couche vers);

enum class RemplissageZone : std::uint8_t { Plein, Hachure };
enum class EtatZone : std::uint8_t { Normale, Figee, MiseDeCote };
struct Zone {
  std::string uuid, reseau;
  Couche couche = Couche::CuivreDessous;
  Polygone contour;
  int priorite = 0;
  Nm isolement = 0;  // 0 : celui des règles
  Nm epaisseurMin = 250'000;
  bool thermique = true;
  Nm largeurRayon = 500'000, ecartThermique = 500'000;
  // 0.34 (5.11, 5.25, 5.26)
  std::string nom;                                     // nom unique donné d'office
  RemplissageZone remplissage = RemplissageZone::Plein;
  Nm hachureLargeur = 300'000, hachureEcart = 600'000;
  double hachureAngle = 45;
  bool thermiquesTraversantsSeuls = false;             // liaisons thermiques réservées aux traversants (5.25)
  double aireIlotMin = 0;                              // mm² : îlots plus petits retirés (0 : règle de la carte)
  bool relierIlotsParVias = false;                     // îlots reliés par des vias posées d'office (5.25)
  Verrou verrou = Verrou::Aucun;
  EtatZone etat = EtatZone::Normale;
  Polygones remplissageFige;                           // zone figée : son cuivre gardé tel quel
  std::string signature;                               // empreinte des données de son dernier remplissage (5.26)
  std::string partition;                               // plan partagé entre réseaux (5.25) : même nom, même couche
  std::string garde;                                   // anneau de garde : réseau ou objet gardé (5.25)
  Nm pasCouture = 0;                                   // couture de vias (5.11) : 0, aucune
  friend bool operator==(const Zone&, const Zone&) = default;
};
// Zone de règles, d'interdiction ou de placement (5.4, 5.27, 6.5, 5.24, 5.30) : contour, couches, objets interdits,
// jeu de contraintes appliqué dans la région, hauteur maximale des composants, feuille ou classe à y placer
struct ZoneRegle {
  std::string uuid, nom;
  Polygone contour;
  Couches couches = 0;  // 0 : toutes les couches de cuivre
  bool interditPistes = false, interditVias = false, interditPastilles = false, interditZones = false, interditEmpreintes = false;
  std::string jeu;      // jeu de contraintes appliqué dans la région (8.24)
  Nm hauteurMax = 0;    // hauteur maximale des corps (5.24) ; 0 : libre
  std::string placement;  // « feuille:<uuid> » ou « classe:<nom> » : composants à ranger dans la zone (5.30)
  bool depuisBoitier = false;  // dictée par le boîtier (6.5), refaite quand il change
  Verrou verrou = Verrou::Aucun;
  friend bool operator==(const ZoneRegle&, const ZoneRegle&) = default;
};
// Région de cuivre pleine (5.22 gouttes, 7.24 cuivre propre à la carte, logos de cuivre) : polygone d'un réseau
struct RegionCuivre {
  std::string uuid, reseau, genre;  // genre : « goutte », « libre », « bobine », « logo »…
  Couche couche = Couche::CuivreDessus;
  Polygone forme;
  std::string objet;  // goutte : la pastille ou la via qu'elle élargit (« <composant>:<numéro> », uuid de via)
  Verrou verrou = Verrou::Aucun;
  friend bool operator==(const RegionCuivre&, const RegionCuivre&) = default;
};
// Motif d'accord gardé comme objet paramétré (5.22, ROU-07) : réglé pendant la pose, refait après un changement de
// règle ; ses segments sont des pistes qui le citent
enum class GenreMotifAccord : std::uint8_t { Accordeon, Trombone, DentsDeScie };
struct MotifAccord {
  std::string uuid, reseau;
  GenreMotifAccord genre = GenreMotifAccord::Accordeon;
  Couche couche = Couche::CuivreDessus;
  Pt a, b;              // segment d'origine que le motif remplace
  Nm largeur = 250'000, amplitude = 1'000'000, pas = 0, rayon = 0;
  Nm longueurCible = 0; // 0 : celle du groupe d'accord
  bool deuxCotes = true;
  friend bool operator==(const MotifAccord&, const MotifAccord&) = default;
};
// Groupe d'objets du circuit (5.3, 5.24, 5.32) : composants, pistes, vias, zones ; un groupe emboîté cite les siens
struct GroupeCircuit {
  std::string uuid, nom, parent;
  std::vector<std::string> membres;
  bool verrou = false;
  friend bool operator==(const GroupeCircuit&, const GroupeCircuit&) = default;
};
// Cote du circuit (5.15, 5.31, DOC-04) : alignée, horizontale, verticale, angulaire, rayon, diamètre, ordonnée,
// marque de centre ; les points donnent ce qui est mesuré, « decalage » la place de la ligne de cote
enum class GenreCote : std::uint8_t { Alignee, Horizontale, Verticale, Angulaire, Rayon, Diametre, Ordonnee, Centre };
struct Cote {
  std::string uuid;
  GenreCote genre = GenreCote::Alignee;
  Couche couche = Couche::Dessins;
  std::vector<Pt> points;
  Nm decalage = 2'000'000, hauteurTexte = 1'000'000, epaisseur = 150'000;
  int precision = 2;
  std::string unite = "mm", prefixe, suffixe, texte;  // texte non vide : remplace la valeur mesurée
  std::string base;     // cote en ligne de base : uuid de la cote de base
  friend bool operator==(const Cote&, const Cote&) = default;
};
// Valeur mesurée d'une cote (mm ou degrés) et son texte affiché
double valeurCote(const Cote& c);
std::string texteCote(const Cote& c);
struct TexteCircuit {
  std::string uuid, texte;
  Couche couche = Couche::SerigraphieDessus;
  Pt p;
  Nm hauteur = 1'000'000;
  double rotation = 0;
  // 0.34 : miroir imposé (absent : celui de la face, 5.28 DRC-66), police TrueType incorporée au projet (7.22, vide :
  // la police vectorielle), texte en réserve (lettres évidées dans un cadre plein)
  std::optional<bool> miroir;
  std::string police;
  bool reserve = false;
  friend bool operator==(const TexteCircuit&, const TexteCircuit&) = default;
};
struct ContourCarte {
  Pt origine;  // coin haut gauche
  Nm largeur = 50 * NM_PAR_MM, hauteur = 40 * NM_PAR_MM, rayon = NM_PAR_MM;
  Polygone libre;  // s'il n'est pas vide, remplace le rectangle
  // Découpes internes : fenêtres fraisées de part en part (passage de câble, isolement galvanique, logement d'un
  // composant) ; chacune est un polygone fermé tenu à l'intérieur du contour (EL-44)
  Polygones decoupes;
  // Traitement d'un côté du contour (8.12, CAR-35) : arêtes de « premiere » à « derniere » (rangs dans aretes()),
  // biseau d'un connecteur de tranche, bord métallisé, rangée de demi-trous
  struct Traitement {
    enum class Genre : std::uint8_t { Biseau, BordMetallise, DemiTrous } genre = Genre::Biseau;
    int premiere = 0, derniere = 0;
    double angle = 30;       // biseau
    Nm profondeur = 0;       // biseau
    friend bool operator==(const Traitement&, const Traitement&) = default;
  };
  std::vector<Traitement> traitements;
  Polygone polygone() const;
  // Arêtes de fraisage (contour et découpes) et appartenance d'un point à la matière de la carte
  std::vector<std::pair<Pt, Pt>> aretes() const;
  bool contient(Pt p) const;
  friend bool operator==(const ContourCarte&, const ContourCarte&) = default;
};
// Textes de sérigraphie tirés des composants (EL-06) : repère, et valeur au choix
struct OptionsSerigraphie {
  bool reperes = true, valeurs = false;
  Nm hauteur = 1'000'000;  // hauteur des capitales
  friend bool operator==(const OptionsSerigraphie&, const OptionsSerigraphie&) = default;
};
// Réglages des couches de fabrication (EL-31) : marge du vernis autour des pastilles, vias couvertes ou non,
// taux de pâte des grandes pastilles CMS (pastilles centrales), posée en fenêtres
struct ReglagesFabrication {
  Nm margeVernis = 50'000;
  bool viasCouvertes = true;
  double tauxPateGrandes = 0.6;      // 0,5 à 1 ; 1 : pastille entière
  double seuilGrandePastille = 4.0;  // mm² : au-delà, la pâte est fenêtrée
  // 0.34 : marge globale de la pâte (5.26, négative : réduite), pont de vernis minimal (8.14), procédé de brasage et
  // densité IPC qui choisissent la variante d'empreinte de chaque article (4.26)
  Nm margePate = 0;
  Nm pontVernisMin = 0;
  std::string procede = "refusion";  // « refusion », « vague », « fer »
  char densite = 'N';                // IPC-7351 : 'M' (plus), 'N' (nominale), 'L' (moins)
  std::string formatNumeroSerie;     // cadre de numéro de série (8.13) : « SN-{n:04} » ; vide, aucun
  bool fentesG85 = false;            // fentes du perçage en G85 plutôt que détourées (4.13, option du profil)
  friend bool operator==(const ReglagesFabrication&, const ReglagesFabrication&) = default;
};
// Réglages de travail du circuit (EL-37) : unité des champs et des mesures (« mm », « mil », « in ») et pas de la
// grille ; 0 garde le pas automatique (0,1 mm au placement, 0,05 mm au routage)
// Jeu de visibilité nommé (5.32) : couches visibles, opacité par genre d'objet
struct JeuVisibilite {
  std::string nom;
  Couches couches = ~Couches{0};
  std::map<std::string, double> opacites;  // « pistes », « vias », « zones », « pastilles », « textes », « dessins »
  friend bool operator==(const JeuVisibilite&, const JeuVisibilite&) = default;
};
// Image de référence calée par deux points (5.31, CAR-20) : fichier (chemin relatif au projet), deux points de
// l'image (pixels) et leurs places sur la carte
struct ImageReference {
  std::string fichier;
  double px1 = 0, py1 = 0, px2 = 100, py2 = 0;
  Pt carte1, carte2{100 * 1'000'000, 0};
  double opacite = 0.5;
  bool visible = true;
  friend bool operator==(const ImageReference&, const ImageReference&) = default;
};
struct AffichageCircuit {
  std::string unite = "mm";
  Nm grille = 0;
  double opaciteInactives = 0.45;  // couches de cuivre autres que l'active (EL-25)
  // 0.34 : origine de travail (5.31) suivie par l'inspecteur, les mesures, les cotes et, au choix, les fichiers de
  // fabrication ; axe y vers le haut au choix
  std::optional<Pt> origineTravail;
  bool yVersLeHaut = false;
  bool origineFabrication = false;
  Couches couchesVisibles = ~Couches{0};  // couches visibles gardées (5.12)
  std::map<std::string, std::string> couleursReseaux;  // réseau → couleur (5.12)
  std::vector<JeuVisibilite> jeux;        // jeux de visibilité nommés (5.32)
  std::map<std::string, std::string> nomsCouches;  // nom affiché des couches personnelles (7.22) : code → nom
  std::vector<ImageReference> images;     // images de référence (5.31)
  bool contoursIsolement = false;         // contours d'isolement permanents (5.32)
  friend bool operator==(const AffichageCircuit&, const AffichageCircuit&) = default;
};
// Dessin sur la carte (7.22, CAR-26) : trait, arc (début, milieu, fin), cercle (centre, point du bord), polygone,
// rectangle (deux coins, coins arrondis de « rayon ») ou courbe de Bézier (départ, deux points de contrôle,
// arrivée) sur une couche de dessin (sérigraphie, encombrement, documentation…), plein, vide ou hachuré
struct DessinCircuit {
  enum class Forme : std::uint8_t { Trait, Arc, Cercle, Polygone, Rectangle, Bezier } forme = Forme::Trait;
  std::string uuid;
  Couche couche = Couche::SerigraphieDessus;
  std::vector<Pt> points;
  Nm epaisseur = 150'000;
  bool plein = false;
  bool hachure = false;
  Nm rayon = 0;  // rectangle : rayon des coins
  friend bool operator==(const DessinCircuit&, const DessinCircuit&) = default;
};
// Contour d'un dessin en segments (arcs, cercles et courbes de Bézier découpés), pour l'écran, les Gerber et la DRC
std::vector<Pt> contourDessin(const DessinCircuit& d);
struct Circuit {
  ContourCarte contour;
  AffichageCircuit affichage;
  OptionsSerigraphie serigraphie;
  ReglagesFabrication fabrication;
  Empilage empilage;
  Regles regles;
  std::vector<EmpreintePlacee> empreintes;
  std::vector<Piste> pistes;
  std::vector<Via> vias;
  std::vector<Zone> zones;
  std::vector<TexteCircuit> textes;
  std::vector<VolumeLibre> volumes;  // volumes libres posés sur la carte (repère de la carte)
  std::vector<DessinCircuit> dessins;  // dessins sur les couches de dessin (7.22)
  // 0.34
  std::vector<ZoneRegle> zonesRegles;       // zones de règles, d'interdiction et de placement (5.4, 5.27)
  std::vector<RegionCuivre> regions;        // cuivre plein : gouttes, cuivre propre, logos (5.22, 7.24)
  std::vector<MotifAccord> motifs;          // motifs d'accord paramétrés (5.22)
  std::vector<GroupeCircuit> groupes;       // groupes d'objets (5.3, 5.24)
  std::vector<Cote> cotes;                  // cotes (5.15, 5.31)
  std::map<std::string, GabaritPastille> gabarits;  // piles de pastilles nommées du projet (4.24)
  friend bool operator==(const Circuit&, const Circuit&) = default;
};

// Géométrie d'une empreinte placée.
Pt versCarte(const EmpreintePlacee& e, Pt local);
Pt versEmpreinte(const EmpreintePlacee& e, Pt carte);  // inverse de versCarte
Couche coucheSurCarte(const EmpreintePlacee& e, Couche c);
Polygone formePastille(const Pastille& p);   // contour du cuivre, dans le repère où p est exprimée
Pastille pastilleSurCarte(const EmpreintePlacee& e, const Pastille& p);
// Point du repère de la pastille (centre en 0, avant rotation) vers son repère d'expression (trous, formes libres)
Pt surPastille(const Pastille& p, Pt local);
// Couches réelles d'une pastille posée (dessous : retournée), limitées aux cuivres de l'empilage ; les couches
// explicites de la pastille l'emportent sur celles que son type suppose (X2)
Couches couchesPastille(const Pastille& p, bool dessous, int couchesCuivre);
Polygone encombrementSurCarte(const EmpreintePlacee& e, const DefEmpreinte& d);
// Point de placement d'une empreinte, dans son repère : le centre de ses pastilles de cuivre, qui n'est pas son
// origine pour un traversant de KiCad (broche 1) ni pour bien des connecteurs (F3, EMP-17) ; l'origine sans pastille
Pt pointDePlacement(const DefEmpreinte& d);
// Famille de boîtier d'une empreinte, au sens du brochage (B17) : « 2 bornes », « SOT-23 », « SOT-223 », « TO-220 »,
// « DIL » (SOIC, TSSOP, DIP : même numérotation à nombre de broches égal), « QFN », « QFP », « barrette »… ;
// vide si l'identifiant n'est pas reconnu (empreinte personnelle, KiCad inconnue).
std::string familleBoitier(const std::string& idEmpreinte);

}  // namespace prom::ecad

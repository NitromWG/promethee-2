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

#include "promethee/ecad.hpp"

namespace prom::ecad {

enum class Couche : std::uint8_t {
  CuivreDessus, CuivreInt1, CuivreInt2, CuivreDessous, VernisDessus, VernisDessous, PateDessus, PateDessous,
  SerigraphieDessus, SerigraphieDessous, Contour, EncombrementDessus, EncombrementDessous, FabricationDessus, FabricationDessous
};
inline constexpr int NB_COUCHES = 15;
using Couches = std::uint32_t;
inline constexpr Couches bit(Couche c) { return Couches{1} << static_cast<unsigned>(c); }
const char* nomCouche(Couche c);   // « Cuivre dessus »
const char* codeCouche(Couche c);  // « F.Cu » : noms compris par tous les fabricants
std::optional<Couche> coucheDepuisCode(const std::string& code);
bool estCuivre(Couche c);
Couche opposee(Couche c);          // dessus ↔ dessous
std::vector<Couche> couchesCuivre(int nombre);  // 1 (simple face : dessous), 2 ou 4, du dessus vers le dessous
Couches tousLesCuivres(int nombre);

enum class FormePastille : std::uint8_t { Rond, Rect, Ovale, RectArrondi };
enum class TypePastille : std::uint8_t { CMS, Traversante, NonMetallisee };
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
  friend bool operator==(const Pastille&, const Pastille&) = default;
};
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
  friend bool operator==(const Corps3D&, const Corps3D&) = default;
};
struct DefEmpreinte {
  std::string id, description, famille;
  int version = 1;
  std::vector<Pastille> pastilles;
  std::vector<TraitEmpreinte> traits;
  Boite encombrement;  // zone réservée au composant (courtyard)
  Corps3D corps;
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

struct Empilage {
  int couchesCuivre = 2;  // 1 (simple face, cuivre dessous), 2 ou 4
  double epaisseur = 1.6;          // mm
  double cuivre = 0.035;           // mm (35 µm)
  std::string materiau = "FR-4", finition = "HASL sans plomb", vernis = "vert", serigraphie = "blanc";
  friend bool operator==(const Empilage&, const Empilage&) = default;
};
// Classe de réseaux : largeur et isolement propres. Le dimensionnement électrique n'est jamais déduit du nom
// des réseaux : « courant » (A, 0 = non renseigné) et « echauffement » (élévation de température admise, °C)
// sont saisis par l'utilisateur ; la largeur recommandée en découle par IPC-2221 (voir largeurIPC2221), et la
// DRC vérifie chaque piste de la classe.
struct ClasseReseau {
  std::string nom;
  Nm isolement = 200'000, largeur = 250'000, viaDiametre = 800'000, viaPercage = 400'000;
  double courant = 0;        // A, courant continu (ou efficace) à transporter ; 0 : pas de vérification
  double echauffement = 10;  // °C au-dessus de l'ambiante
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

struct EmpreintePlacee {
  std::string composant, empreinte;  // UUID du composant ; identifiant d'empreinte
  Pt position;
  double rotation = 0;  // degrés, sens direct
  bool dessous = false, verrou = false;
  friend bool operator==(const EmpreintePlacee&, const EmpreintePlacee&) = default;
};
struct Piste {
  std::string uuid, reseau;
  Couche couche = Couche::CuivreDessus;
  Pt a, b;
  Nm largeur = 250'000;
  friend bool operator==(const Piste&, const Piste&) = default;
};
struct Via {
  std::string uuid, reseau;
  Pt p;
  Nm diametre = 800'000, percage = 400'000;
  friend bool operator==(const Via&, const Via&) = default;
};
struct Zone {
  std::string uuid, reseau;
  Couche couche = Couche::CuivreDessous;
  Polygone contour;
  int priorite = 0;
  Nm isolement = 0;  // 0 : celui des règles
  Nm epaisseurMin = 250'000;
  bool thermique = true;
  Nm largeurRayon = 500'000, ecartThermique = 500'000;
  friend bool operator==(const Zone&, const Zone&) = default;
};
struct TexteCircuit {
  std::string uuid, texte;
  Couche couche = Couche::SerigraphieDessus;
  Pt p;
  Nm hauteur = 1'000'000;
  double rotation = 0;
  friend bool operator==(const TexteCircuit&, const TexteCircuit&) = default;
};
struct ContourCarte {
  Pt origine;  // coin haut gauche
  Nm largeur = 50 * NM_PAR_MM, hauteur = 40 * NM_PAR_MM, rayon = NM_PAR_MM;
  Polygone libre;  // s'il n'est pas vide, remplace le rectangle
  // Découpes internes : fenêtres fraisées de part en part (passage de câble, isolement galvanique, logement d'un
  // composant) ; chacune est un polygone fermé tenu à l'intérieur du contour (EL-44)
  Polygones decoupes;
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
  friend bool operator==(const ReglagesFabrication&, const ReglagesFabrication&) = default;
};
// Réglages de travail du circuit (EL-37) : unité des champs et des mesures (« mm », « mil », « in ») et pas de la
// grille ; 0 garde le pas automatique (0,1 mm au placement, 0,05 mm au routage)
struct AffichageCircuit {
  std::string unite = "mm";
  Nm grille = 0;
  double opaciteInactives = 0.45;  // couches de cuivre autres que l'active (EL-25)
  friend bool operator==(const AffichageCircuit&, const AffichageCircuit&) = default;
};
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
  friend bool operator==(const Circuit&, const Circuit&) = default;
};

// Géométrie d'une empreinte placée.
Pt versCarte(const EmpreintePlacee& e, Pt local);
Pt versEmpreinte(const EmpreintePlacee& e, Pt carte);  // inverse de versCarte
Couche coucheSurCarte(const EmpreintePlacee& e, Couche c);
Polygone formePastille(const Pastille& p);   // contour du cuivre, dans le repère où p est exprimée
Pastille pastilleSurCarte(const EmpreintePlacee& e, const Pastille& p);
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

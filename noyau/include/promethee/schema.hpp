// Prométhée : schéma électronique. Bibliothèque de symboles (versionnée, recopiée dans le projet qui l'utilise),
// composants identifiés par UUID, feuilles avec symboles, fils, jonctions, étiquettes, alimentations, marques de
// non-connexion et textes. Le dessin n'est pas la connexion : la connectivité est calculée à part (électronique.hpp).
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <vector>

#include "promethee/ecad.hpp"

namespace prom::ecad {

enum class TypeBroche : std::uint8_t { Entree, Sortie, Bidirectionnelle, TroisEtats, Passive, AlimEntree, AlimSortie, CollecteurOuvert, NonConnectee, NonSpecifiee };
const char* codeTypeBroche(TypeBroche t);  // « entree »… (fichiers)
const char* nomTypeBroche(TypeBroche t);   // « Entrée »… (interface)
TypeBroche typeBrocheDepuis(const std::string& code);

// Fonction alternative d'une broche (7.7, SYM-07, EMB-01) : « PA2 » peut être « USART2_TX » ou « ADC1_IN2 »
struct FonctionBroche {
  std::string nom;
  TypeBroche type = TypeBroche::Bidirectionnelle;
  friend bool operator==(const FonctionBroche&, const FonctionBroche&) = default;
};
// Broche : « extremite » est le point de connexion, en coordonnées du symbole ; le trait part de l'extrémité vers
// le corps selon « direction » (0 : +x, 1 : +y, 2 : −x, 3 : −y), sur « longueur ».
struct Broche {
  std::string numero, nom;
  TypeBroche type = TypeBroche::Passive;
  Pt extremite;
  int direction = 0;
  Nm longueur = 2'540'000;
  bool nomVisible = true, numeroVisible = true;
  int unite = 0;  // symbole à plusieurs unités : 0 commune à toutes, k ≥ 1 propre à l'unité k (A, B…)
  // 0.34 (7.7, 7.18, 7.20) : style de la broche et signes qualificatifs de la CEI 60617-12 (« », « inversee »,
  // « horloge », « horlogeInversee », « entreeBasse », « sortieBasse », « horlogeBasse », « nonLogique »,
  // « hysteresis », « collecteurOuvert », « emetteurOuvert », « troisEtats ») ; section, groupe et rang pour le
  // générateur de symboles ; fonctions alternatives ; broche cachée reliée par son nom (alimentation implicite) ; vue
  // (0 : toutes les vues, 1 ou 2 : corps normal ou De Morgan)
  std::string style, section, groupe;
  int rang = 0;
  std::vector<FonctionBroche> alternatives;
  bool cachee = false;
  int vue = 0;
  friend bool operator==(const Broche&, const Broche&) = default;
};

struct Trace {
  // Formes du symbole et des dessins du schéma (7.19, 7.20) : la 0.34 ajoute l'ellipse (centre, « rayon » en x,
  // « rayonY »), la courbe de Bézier (départ, deux points de contrôle, arrivée), l'image (position, « texte » :
  // fichier ou données) et le cadre de texte (deux coins, « texte » au-dedans)
  enum class Forme : std::uint8_t { Polyligne, Rectangle, Cercle, Arc, Texte, Ellipse, Bezier, Image, CadreTexte } forme = Forme::Polyligne;
  std::vector<Pt> points;     // polyligne : sommets ; rectangle : deux coins ; cercle, arc : centre ; texte : position
  Nm rayon = 0;
  double debut = 0, fin = 0;  // arc : angles en degrés, sens direct à l'écran
  bool rempli = false, ferme = false, fond = false;  // rempli : encre ; fond : couleur de fond (corps des circuits)
  std::string texte;
  Nm taille = 1'270'000;
  int unite = 0;  // 0 : dessiné dans toutes les unités
  // 0.34 : rayon en y d'une ellipse, coins arrondis d'un rectangle (« arrondi »), épaisseur, style du trait
  // (« », « tirets », « points », « mixte »), couleur, flèches (1 au départ, 2 à l'arrivée), vue (0 : toutes)
  Nm rayonY = 0, arrondi = 0, epaisseur = 0;
  std::string style, couleur;
  int fleches = 0;
  int vue = 0;
  friend bool operator==(const Trace&, const Trace&) = default;
};
// Contour d'une forme en segments (ellipses, arcs, cercles et courbes de Bézier découpés), dans son repère
std::vector<Pt> contourTrace(const Trace& t, int pas = 32);

// Provenance de la hauteur d'un composant (3.4, MEC-02) : générique (corps de l'empreinte), saisie (tapée, non
// confirmée), fiche (confirmée sur la fiche technique), modèle exact (boîte du modèle 3D associé)
enum class ProvenanceHauteur : std::uint8_t { Generique, Saisie, Fiche, ModeleExact };
// Genre d'un composant (3.17, SAI-45) : il décide de son passage au circuit, à la nomenclature et au fichier de
// placement. Standard : les trois. Mécanique (vis, entretoise, dissipateur) : circuit et nomenclature, pas de
// placement. Graphique (logo, mire) : circuit seul. Liaison de réseaux : circuit seul. Hors nomenclature : circuit
// et placement. Cavalier (strap, pont de soudure) : les trois.
enum class GenreComposant : std::uint8_t { Standard, Mecanique, Graphique, LiaisonReseaux, HorsNomenclature, Cavalier };
const char* nomGenreComposant(GenreComposant g);   // « standard », « mécanique »…
const char* codeGenreComposant(GenreComposant g);  // pour le fichier : « standard », « mecanique »…
GenreComposant genreComposantDepuisCode(const std::string& code);

// Définition de symbole : versionnée ; le projet en garde une copie (il verrouille la version utilisée).
struct DefSymbole {
  std::string id, nom, description, categorie, prefixe = "U", empreinte, motsCles;
  std::string fabricant, mpn, fiche;  // identité du composant réel (recopiée dans le composant posé)
  int version = 1;
  bool alimentation = false;  // symbole d'alimentation : nomme le réseau de sa broche (GND, +5V…)
  bool drapeau = false;       // drapeau d'alimentation : déclare le réseau alimenté
  bool mecanique = false;     // trou de fixation : pas de nomenclature
  // Passif traversant d'alimentation (ferrite, shunt, 0 Ω, fusible) : l'alimentation passe d'une broche à l'autre
  // pour l'ERC-22 (E3) ; liaison de réseaux (net-tie) : ses deux réseaux se touchent volontairement, l'ERC-23 et la
  // DRC l'acceptent entre ses seules broches
  bool traversant = false, liaisonReseaux = false;
  // Genre donné aux composants posés de ce symbole (3.17) : une entretoise ou une vis sans broche est « mécanique »
  GenreComposant genre = GenreComposant::Standard;
  // Symbole à plusieurs unités (portes d'un 74HC00, amplis d'un LM358) : chaque unité se pose à part, sous le même
  // repère suivi de sa lettre (U1A, U1B…) ; les broches de l'unité 0 (alimentation) sont communes
  int unites = 1;
  // Nom affiché de chaque unité (« bobine », « contact ») au lieu de la lettre (SYM-04) ; vide : A, B, C
  std::vector<std::string> nomsUnites;
  std::vector<Broche> broches;
  std::vector<Trace> traces;
  Pt posReference{2'540'000, -1'270'000}, posValeur{2'540'000, 1'270'000};
  // Données propres à un métier, écrites seulement si présentes (appareil électrique personnel : marquage,
  // dimensions, montage, pertes, prix indicatif)
  std::map<std::string, std::string> proprietes;
  // 0.34 (7.7, 7.20) : symbole dérivé d'un parent dont il garde le dessin et les broches, champs propres écrits ici ;
  // style du dessin (« CEI », « ANSI », « GOST ») ; vues alternatives (2 : corps De Morgan) ; groupes de broches
  // reliées à l'intérieur du composant
  std::string parent, style;
  int vues = 1;
  std::vector<std::vector<std::string>> brochesReliees;
  friend bool operator==(const DefSymbole&, const DefSymbole&) = default;
};
const std::vector<DefSymbole>& bibliothequeSymboles();
const DefSymbole* symboleBibliotheque(const std::string& id);

// Article (lot L4, 4.2 ; BIB-01) : un composant réel, achetable — symbole, empreinte, correspondance des broches
// aux pastilles, valeur, paramètres, hauteur, fabricant, références des fournisseurs. Le symbole est générique
// (« Q_NMOS ») ; le brochage de l'article dit quelle pastille porte chaque broche (IRLZ44N en TO-220 : G 1, D 2, S 3 ;
// 2N7002 en SOT-23 : G 1, S 2, D 3). Un article peut dériver d'un article de base, champ par champ (4.14).
struct OffreArticle {
  std::string distributeur, reference, devise = "EUR";
  int conditionnement = 1, stock = -1;            // −1 : inconnu
  std::vector<std::pair<int, double>> prix;       // quantité minimale → prix unitaire
  friend bool operator==(const OffreArticle&, const OffreArticle&) = default;
};
struct SecondeSource {
  std::string fabricant, mpn, cycleVie = "actif";  // « actif », « NRND », « fin de vie »
  int rang = 2;
  friend bool operator==(const SecondeSource&, const SecondeSource&) = default;
};
// Modèle de simulation d'un article (lot L11, 11.5 ; SIM-03) : texte .model ou .subckt (ou contenu d'un .lib), nom
// du modèle ou du sous-circuit appelé, broches du symbole dans l'ordre des nœuds de l'élément (une entrée par
// instance : « 3 2 8 4 1 », « 5 6 8 4 7 » pour les deux amplis d'un LM358), compatibilité du texte (« ps », « lt »,
// « hs » : PSpice, LTspice, HSPICE), fichier d'origine, genre (« sous-circuit », « diode », « bipolaire », « mos »,
// « jfet », « verilog-a », « ibis ») et source (fiche technique citée, ajustement…)
struct ModeleSpice {
  std::string texte, nom;
  std::vector<std::string> broches;
  std::string compatibilite, fichier, genre, source;
  bool vide() const { return texte.empty(); }
  friend bool operator==(const ModeleSpice&, const ModeleSpice&) = default;
};
struct Article {
  std::string id, nom, description, categorie, symbole, empreinte, valeur;
  std::string fabricant, mpn, fiche, lcsc;
  std::string modele3D;  // modèle 3D joint (STEP ou WRL), chemin relatif à la bibliothèque de l'article
  // Numéro de broche du symbole → numéro(s) de pastille (« 2,4 » : une broche sur deux pastilles) ; vide : mêmes numéros
  std::map<std::string, std::string> brochage;
  std::map<std::string, std::string> parametres;
  double hauteur = 0;   // mm au-dessus de la carte ; 0 : générique de l'empreinte
  std::string base;     // article dont celui-ci hérite les champs qu'il n'écrit pas (4.14)
  std::set<std::string> nonHerites;  // champs coupés de l'héritage : vides chez lui, ils restent vides
  std::vector<OffreArticle> offres;
  std::vector<SecondeSource> secondes;
  // Composant générique provisoire (4.21, BIB-20) : décrit par ses exigences (« LDO 3,3 V 500 mA »), signalé par
  // l'ERC et la publication jusqu'à son remplacement par un article réel
  bool generique = false;
  std::string exigences;
  int version = 1;
  // Équipe (lot L19, 19.3, 19.5) : état du cycle de vie (« brouillon », « pré-série », « production », « fin de
  // série », « obsolète ») et numéro d'article de l'entreprise, relié aux références fabricant validées
  std::string etatCycle, numeroInterne;
  ModeleSpice modeleSpice;  // 11.5
  friend bool operator==(const Article&, const Article&) = default;
};

// Composant créé par l'utilisateur : une liste de broches suffit, le symbole (corps rectangulaire, broches au pas
// de 2,54 mm, toutes sur la grille) est dessiné automatiquement. Côté : −1 automatique (alimentations en haut,
// masses en bas, entrées à gauche, sorties à droite, le reste réparti), 0 gauche, 1 droite, 2 haut, 3 bas.
struct SpecBroche {
  std::string numero, nom;
  TypeBroche type = TypeBroche::Bidirectionnelle;
  int cote = -1;
  int unite = 0;  // plusieurs unités : 1, 2… (0 : commune à toutes) ; dans la liste, « u2 », « unite=B » ou « commune »
};
struct SpecSymbole {
  std::string id, nom, prefixe = "U", categorie = "Mes composants", description, empreinte, motsCles, fabricant, mpn, fiche;
  int version = 1;
  std::vector<SpecBroche> broches;
  // Noms des unités (2.23, SYM-04), dans l'ordre des unités : « Ampli 1 », « Alimentation »… ; vide, les lettres
  std::vector<std::string> nomsUnites{};
};
DefSymbole genererSymbole(const SpecSymbole& s);
SpecSymbole specDepuisSymbole(const DefSymbole& d);           // pour modifier un composant créé
std::vector<SpecBroche> lireListeBroches(const std::string& texte);  // une broche par ligne : « 1 VCC alim haut »
std::string identifiantSymbole(const std::string& nom);       // « perso:ESP32-S3 »

// Variante de montage (lot L12, 12.1, 12.7, STR-07) : ce qu'une variante change sur un composant. « Hérite » et un
// champ vide gardent ce qu'écrit le composant ; l'empreinte de remplacement se pose sous le même repère.
enum class MontageVariante : std::uint8_t { Herite, Monte, NonMonte };
struct SurchargeVariante {
  MontageVariante montage = MontageVariante::Herite;
  std::string valeur, article, empreinte;
  friend bool operator==(const SurchargeVariante&, const SurchargeVariante&) = default;
};

// Composant de la conception : son identité est l'UUID, jamais la référence.
struct ComposantElec {
  std::string uuid, reference, valeur, symbole, empreinte;
  std::string fabricant, mpn, fournisseur, description, fiche;
  bool nePasMonter = false;
  // Hors carte : pièce du schéma qui ne va pas sur le circuit (câble, pièce du boîtier…), comme « Exclude from
  // board » de KiCad. Sans ce choix, un composant sans empreinte ou absent du circuit bloque la publication (EL-04).
  bool horsCarte = false;
  std::map<std::string, std::string> champs;
  // Paramètres typés de la famille (3.3, valeurs.hpp) : clé → texte tel que tapé (« tolerance » → « 1 % »)
  std::map<std::string, std::string> parametres;
  // Valeur calculée (3.14, SAI-44) : formule qui lit d'autres valeurs ou les variables de la conception
  // (« 2*R1 ») ; non vide, la valeur en est le résultat, recalculé dans l'ordre des dépendances
  std::string formule;
  // Hauteur du corps au-dessus de la carte (mm) : 0 garde celle, générique, de l'empreinte. Sa provenance (3.4) :
  // saisie tant qu'elle n'est pas confirmée sur la fiche technique ; « Fiche » sans hauteur confirme la générique.
  // Lue par hauteurComposant() (hauteurs.hpp), pour la 3D, le boîtier et la vérification.
  double hauteur = 0;
  ProvenanceHauteur provenanceHauteur = ProvenanceHauteur::Saisie;
  std::string sourceHauteur;  // « fiche Yageo RC_P »
  GenreComposant genre = GenreComposant::Standard;
  bool horsSimulation = false;  // STR-08 : grisé au schéma, ignoré par la simulation
  // Empreinte pour laquelle la correspondance broche ↔ pastille a été vérifiée sur la fiche technique (B17) : un
  // symbole écrit pour un boîtier (MOSFET en SOT-23 : 1 G, 2 S, 3 D) posé sur une autre famille (TO-220 : 1 G, 2 D,
  // 3 S) bloque la publication (ERC-09) tant que ce champ ne nomme pas l'empreinte actuelle.
  std::string brochageVerifie;
  // Attributs repris de KiCad (X7) : hors nomenclature (« exclude_from_bom » : mire, logo), hors fichier de
  // placement (« exclude_from_pos_files » : trou de fixation métallisé)
  bool horsNomenclature = false, horsPlacement = false;
  // Article d'où vient ce composant (4.2) et sa version au moment de la pose : une version plus récente propose la
  // mise à jour, avec les différences (4.10) ; code LCSC repris dans la nomenclature (4.12)
  std::string article;
  // Modèle 3D exact associé (6.2) : identifiant dans ConceptionElec::modeles3D ; il remplace la boîte du corps
  std::string modele3D;
  // Copie liée d'un composant d'une feuille de canal (7.5) : l'UUID du composant de la feuille source
  std::string modeleComposant;
  int versionArticle = 0;
  std::string lcsc;
  std::map<std::string, SurchargeVariante> variantes;  // nom de la variante → ce qu'elle change (12.1)
  // Posé sur la carte sans schéma (5.17, CAR-24) : la synchronisation le garde, ses réseaux viennent des pistes
  bool carteSeule = false;
  friend bool operator==(const ComposantElec&, const ComposantElec&) = default;
};

// Place propre d'un champ d'un symbole posé (repère, valeur ; SAI-14, SAI-34) : son point d'ancrage, décalé de la
// position du symbole, en coordonnées de la feuille ; bloqué, « Replacer les champs » ne le bouge pas
struct ChampPlace {
  Pt decalage;
  bool bloque = false;
  friend bool operator==(const ChampPlace&, const ChampPlace&) = default;
};
struct SymbolePlace {
  std::string uuid, symbole, composant;  // composant : UUID ; vide pour une alimentation ou un drapeau
  std::string nomAlim;                   // alimentation : nom du réseau (« +5V »)
  Pt position;
  int rotation = 0;                      // quarts de tour, sens direct
  bool miroir = false;                   // gauche-droite, avant la rotation
  int unite = 1;                         // unité posée (symbole à plusieurs unités)
  // Repères des bornes propres à cette pose (numéro de la définition → repère réel) : deux contacts d'un même
  // contacteur posés avec le même symbole « contact à fermeture » deviennent 13-14 et 23-24, une borne de bornier
  // prend son numéro. Vide : les numéros de la définition.
  std::map<std::string, std::string> numeros;
  // Places propres du repère et de la valeur ; absentes, celles de la définition, tournées avec le symbole
  std::optional<ChampPlace> champReference, champValeur;
  // 0.34 : vue posée (7.7, De Morgan), fonction retenue de chaque broche (numéro → nom de fonction, 7.7, 15.2),
  // noms des broches masqués sur ce composant (7.18)
  int vue = 1;
  std::map<std::string, std::string> fonctions;
  bool nomsMasques = false;
  friend bool operator==(const SymbolePlace&, const SymbolePlace&) = default;
};
// Repère réel d'une broche de cette pose
std::string numeroBroche(const SymbolePlace& s, const std::string& numeroDefinition);
// Unités : lettre (1 → « A »), appartenance d'une broche ou d'un trait à l'unité posée
std::string lettreUnite(int unite);
inline bool dansUnite(int uniteElement, const SymbolePlace& s, const DefSymbole& d) { return d.unites <= 1 || uniteElement == 0 || uniteElement == s.unite; }
struct Fil {
  std::string uuid;
  Pt a, b;
  // Mode courbe (2.19) : un coin partagé avec un autre fil courbe, à l'équerre, se dessine arrondi de ce rayon ; la
  // connexion reste au point du coin
  Nm rayon = 0;
  friend bool operator==(const Fil&, const Fil&) = default;
};
// Jonction et marque de non-connexion : un identifiant stable (S26), qui ne change ni au glissement ni à la
// rotation ; un fichier qui n'en a pas en reçoit un, tiré de la feuille et de la position (uuidDerive)
struct Jonction {
  Pt p;
  std::string uuid;
  friend bool operator==(const Jonction&, const Jonction&) = default;
};
struct NonConnexion {
  Pt p;
  std::string uuid;
  friend bool operator==(const NonConnexion&, const NonConnexion&) = default;
};
// Locale : la feuille ; globale : tout le projet ; connecteur hors feuille : comme une globale, mais dessiné en
// flèche selon le sens du signal, comme un renvoi de folio (SAI-31). 0.34 : hiérarchique (relie la feuille fille au
// port de même nom de son bloc, 7.2) et directive (7.14 : ne relie rien, pose une classe ou des règles sur le réseau
// qu'elle touche)
enum class TypeEtiquette : std::uint8_t { Locale, Globale, HorsFeuille, Hierarchique, Directive };
enum class SensSignal : std::uint8_t { Aucun, Entree, Sortie, Bidirectionnel, TroisEtats, Passif };
const char* codeSensSignal(SensSignal s);  // « entree »… (fichiers)
SensSignal sensSignalDepuis(const std::string& code);
struct Etiquette {
  std::string uuid, texte;
  Pt p;
  int rotation = 0;
  TypeEtiquette type = TypeEtiquette::Locale;
  SensSignal sens = SensSignal::Aucun;  // dessiné sur la globale et le connecteur hors feuille (SAI-11)
  // Directive (7.14) : classe de réseaux posée sur le réseau touché, et règles écrites (« isolement=3mm »,
  // « largeur=0,15/0,2 », « paire=USB », « impedance=90 »)
  std::map<std::string, std::string> proprietes;
  friend bool operator==(const Etiquette&, const Etiquette&) = default;
};
struct TexteSchema {
  std::string uuid, texte;
  Pt p;
  Nm taille = 1'800'000;
  int rotation = 0;  // quarts de tour, sens direct (EL-21)
  friend bool operator==(const TexteSchema&, const TexteSchema&) = default;
};
// Bloc de feuille (7.2, STR-02) : rectangle posé sur une feuille, qui désigne sa feuille fille ; chaque port porte le
// nom d'une étiquette hiérarchique de la fille et se relie au fil qui le touche. Paramètres propres à l'instance
// (7.15), lien au fichier d'une feuille réutilisée (7.16).
struct PortBloc {
  std::string uuid, nom;
  Pt p;              // point de connexion, sur le bord du bloc
  int cote = 0;      // 0 gauche, 1 droite, 2 haut, 3 bas
  SensSignal sens = SensSignal::Aucun;
  friend bool operator==(const PortBloc&, const PortBloc&) = default;
};
struct BlocFeuille {
  std::string uuid, nom, feuille;  // nom de l'instance ; UUID de la feuille fille
  Pt position;                     // coin haut gauche
  Nm largeur = 20'320'000, hauteur = 15'240'000;
  std::vector<PortBloc> ports;
  std::map<std::string, std::string> parametres;  // repris par les valeurs de la feuille fille (« ${GAIN} »)
  std::string fichier;  // feuille réutilisée depuis un fichier (7.16) ; vide : feuille du projet
  std::string couleur;
  friend bool operator==(const BlocFeuille&, const BlocFeuille&) = default;
};
// Forme dessinée sur la feuille (7.19) : la même géométrie que les traits des symboles, dans le repère de la feuille,
// sur un calque de notes (0 : dessin, 1 à 7 : calques, que le PDF peut omettre), avec son rang de superposition
struct FormeSchema {
  std::string uuid;
  Trace trace;
  int calque = 0, ordre = 0;
  friend bool operator==(const FormeSchema&, const FormeSchema&) = default;
};
// Tableau posé sur la feuille (7.6, 7.19) : cellules en lignes, largeurs des colonnes, bordure
struct TableauSchema {
  std::string uuid;
  Pt p;
  std::vector<std::vector<std::string>> cellules;
  std::vector<Nm> largeurs;
  Nm hauteurLigne = 3'810'000, taille = 1'270'000;
  bool bordure = true;
  friend bool operator==(const TableauSchema&, const TableauSchema&) = default;
};
// Image posée sur la feuille (7.6) : fichier du projet (chemin relatif) ou données PNG en base 64, échelle
struct ImageSchema {
  std::string uuid, fichier, donnees;
  Pt p;
  double echelle = 1;
  friend bool operator==(const ImageSchema&, const ImageSchema&) = default;
};
// Zone de règles du schéma (7.14) : ce qu'elle entoure reçoit une classe ou des règles ; « masque » retire son
// contenu de l'ERC et du passage au circuit
struct ZoneRegleSchema {
  std::string uuid, nom;
  Polygone contour;
  std::map<std::string, std::string> proprietes;  // comme une directive : « classe », règles
  bool masque = false;
  friend bool operator==(const ZoneRegleSchema&, const ZoneRegleSchema&) = default;
};
// Groupe d'objets de la feuille (7.6) : il se sélectionne et se déplace d'un bloc
struct GroupeSchema {
  std::string uuid, nom;
  std::vector<std::string> membres;
  bool verrou = false;
  friend bool operator==(const GroupeSchema&, const GroupeSchema&) = default;
};
struct Feuille {
  std::string uuid, nom = "Feuille 1", format = "A4";
  // Feuille de canal répétée (7.2, 7.5, STR-05, STR-22) : la source porte le nombre d'exemplaires (« 4 × canal ») ;
  // chaque copie porte l'UUID de sa source et son rang (2, 3…) ; ses composants sont des copies liées
  int repetitions = 0;
  std::string modele;
  int instance = 0;
  std::vector<SymbolePlace> symboles;
  std::vector<Fil> fils;
  std::vector<Jonction> jonctions;
  std::vector<NonConnexion> nonConnexions;
  std::vector<Etiquette> etiquettes;
  std::vector<TexteSchema> textes;
  // 0.34 (lot L7) : blocs de feuilles filles, formes, tableaux, images, zones de règles, groupes ; numéro de page
  // libre (vide : automatique) ; format libre (« libre » : largeur et hauteur ci-dessous) ; variables propres à la
  // feuille (elles priment sur celles du projet) ; modèle de feuille appliqué ; calques de notes masqués ; fichier
  // de la feuille réutilisée et sa révision (7.16)
  std::vector<BlocFeuille> blocs;
  std::vector<FormeSchema> formes;
  std::vector<TableauSchema> tableaux;
  std::vector<ImageSchema> images;
  std::vector<ZoneRegleSchema> zonesRegles;
  std::vector<GroupeSchema> groupes;
  std::string page;
  Nm largeurLibre = 0, hauteurLibre = 0;
  std::map<std::string, std::string> variables;
  std::string modeleFeuille;
  std::vector<int> calquesMasques;
  std::string fichier, revisionFichier;
  friend bool operator==(const Feuille&, const Feuille&) = default;
};
// Ligne de l'historique des révisions (12.6, DOC-10) : écrite au cartouche, une par révision publiée
struct RevisionCartouche {
  std::string revision, date, auteur, description;
  friend bool operator==(const RevisionCartouche&, const RevisionCartouche&) = default;
};
struct Cartouche {
  std::string titre, societe, auteur, revision = "A", date, commentaire;
  // 0.34 : champs libres du cartouche (7.6, lus par ${…}) et historique des révisions (12.6)
  std::map<std::string, std::string> champs;
  std::vector<RevisionCartouche> historique;
  friend bool operator==(const Cartouche&, const Cartouche&) = default;
};
// Styles et réglages par défaut du schéma (7.25, SAI-49, SAI-50) : fils et bus (épaisseur, couleur, style), hérités
// de la classe de réseau qui en a un ; jonctions ; tailles de texte ; format des renvois et des numéros de page (7.10)
struct StyleFil {
  std::string couleur, style;  // style : « », « tirets », « points »
  Nm epaisseur = 0;
  friend bool operator==(const StyleFil&, const StyleFil&) = default;
};
struct ReglagesSchema {
  StyleFil fil, bus;
  std::map<std::string, StyleFil> parClasse;  // classe de réseau → style de ses fils
  Nm diametreJonction = 0;
  std::string couleurJonction;
  Nm tailleTexte = 0, tailleEtiquette = 0, tailleChamp = 0;  // 0 : valeurs de la 0.33
  std::string formatRenvoi = "{page}.{zone}";  // renvois entre feuilles et contacts sous une bobine (7.10)
  std::string formatPage = "{n}/{N}";
  std::string porteeNoms = "projet";           // portée des noms des étiquettes globales (7.13) : « projet »
  bool zonesCadre = true;                      // bordure en zones lettres-chiffres
  friend bool operator==(const ReglagesSchema&, const ReglagesSchema&) = default;
};

// Géométrie d'un symbole placé, en coordonnées de la feuille.
Pt transformer(const SymbolePlace& s, Pt local);
int transformerDirection(const SymbolePlace& s, int direction);
struct BrochePlacee {
  const Broche* broche = nullptr;
  Pt p;           // extrémité (point de connexion)
  int direction;  // vers le corps
  std::string numero;  // repère réel de la broche dans cette pose (voir SymbolePlace::numeros)
};
std::vector<BrochePlacee> brochesPlacees(const SymbolePlace& s, const DefSymbole& d);
Boite boiteSymbole(const SymbolePlace& s, const DefSymbole& d);
// Point d'ancrage du repère ou de la valeur d'un symbole posé, en coordonnées de la feuille : sa place propre,
// sinon celle de la définition, tournée avec le symbole (2.6, 2.23)
Pt ancrageChamp(const SymbolePlace& s, const DefSymbole& d, bool valeur);
Boite cadreFeuille(const std::string& format);  // A4 ou A3 paysage, en partant de (0, 0)
inline constexpr Nm GRILLE_SCHEMA = 1'270'000;  // 50 mil : les broches tombent toujours sur la grille
Pt surGrille(Pt p, Nm pas = GRILLE_SCHEMA);

}  // namespace prom::ecad

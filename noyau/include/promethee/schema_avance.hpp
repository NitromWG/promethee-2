// Prométhée : schéma avancé de la 0.34 (lot L7). Hiérarchie (7.2, 7.3) : feuilles à plat migrées en blocs, fil
// d'Ariane, ports d'un bloc comparés aux étiquettes hiérarchiques de sa fille et synchronisés (ERC-33). Textes riches
// (7.6, 7.11) : variables ${…} de la feuille, de la conception et du cartouche, expressions @{…}, exposants, indices,
// barres et liens ; textes marqués et variables sans valeur relevés. Cadre, pages et renvois (7.10). Échange de
// portes et de broches rétroannoté (7.8). Conception compilée (7.14) : directives et zones de règles appliquées aux
// réseaux, classes transmises au circuit, masques ; graphe des réseaux rangé par ressorts. Symboles (7.7, 7.18,
// 7.20) : contrôles CS, broches en CSV, rangée de broches, assistant de symboles normalisés, symboles dérivés.
// Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- Hiérarchie (7.2, 7.3) ----------
// Indices des feuilles de la racine jusqu'à la feuille (fil d'Ariane) ; chemin d'instance « /Racine/Alimentation »
std::vector<std::size_t> filAriane(const ConceptionElec& c, std::size_t feuille);
std::string cheminInstance(const ConceptionElec& c, std::size_t feuille);
// Feuille mère d'une feuille (le bloc qui la désigne) ; vide pour une racine
std::optional<std::pair<std::size_t, std::string>> blocParent(const ConceptionElec& c, std::size_t feuille);
// Ports d'un bloc et étiquettes hiérarchiques de sa fille : ports qui manquent au bloc, ports sans étiquette
struct EcartPorts {
  std::vector<std::string> manquants, orphelins;
  bool vide() const { return manquants.empty() && orphelins.empty(); }
};
EcartPorts ecartPorts(const ConceptionElec& c, std::size_t feuille, const std::string& bloc);
// Ajoute au bloc un port par étiquette hiérarchique de la fille qui n'en a pas (sur son bord gauche, au pas de la
// grille), retire les orphelins si demandé ; rend le nombre de changements
int synchroniserPorts(ConceptionElec& c, std::size_t feuille, const std::string& bloc, bool retirerOrphelins = false);
// Feuilles à plat migrées en hiérarchie : une feuille racine porte un bloc par feuille ; étiquettes globales gardées,
// les réseaux sont les mêmes ; rend l'indice de la racine
std::size_t hierarchiserFeuilles(ConceptionElec& c, const std::string& nomRacine = "Racine");
// Contrôle ERC-33 de toute la conception : un avertissement par bloc dont les ports et la fille divergent
std::vector<Violation> verifierPortsHierarchiques(const ConceptionElec& c);

// ---------- Textes riches (7.6, 7.11) ----------
struct MorceauTexte {
  enum class Style : std::uint8_t { Normal, Exposant, Indice, Barre, Lien } style = Style::Normal;
  std::string texte, cible;  // lien : la cible (« https://… », « #Feuille 2 »)
  bool herite = false;       // vient d'une variable de la conception (montrée en gris là où la feuille la surcharge pas)
};
struct TexteResolu {
  std::string resolu;                   // texte final, sans balises
  std::vector<MorceauTexte> morceaux;   // avec leur style
  std::vector<std::string> inconnues;   // variables sans valeur
  std::vector<std::string> erreurs;     // expressions fausses
  int avertissement = 0;                // 1 : ${ERC_WARNING …} ou ${DRC_WARNING …} ; 2 : …_ERROR
  std::string message;                  // texte de l'avertissement ou de l'erreur marqué
};
// Variables : celles de la feuille (prioritaires), de la conception, puis le cartouche (REVISION, TITRE, DATE,
// AUTEUR, SOCIETE, COMMENTAIRE et ses champs libres), la feuille (FEUILLE, PAGE, PAGES, CHEMIN) ; « feuille » −1 : le
// circuit. ${NOM} se remplace, @{expression} se calcule après (« @{${LARGEUR}*2} ») ; ^{…} exposant, _{…} indice,
// ~{…} barre, [[cible|texte]] lien
TexteResolu resoudreTexte(const std::string& texte, const ConceptionElec& c, int feuille = -1);
std::string texteAffiche(const std::string& texte, const ConceptionElec& c, int feuille = -1);
// ERC-34 (texte marqué) et ERC-35 (variable sans valeur, expression fausse) sur les textes, champs et étiquettes du
// schéma ; DRC-83 et DRC-84 sur les textes de la carte
std::vector<Violation> verifierTextesRiches(const ConceptionElec& c, bool circuit);

// ---------- Cadre, pages et renvois (7.10) ----------
// Numéro de page d'une feuille : le sien, sinon son rang ; texte d'après le format des pages (« {n}/{N} »)
std::string numeroPage(const ConceptionElec& c, std::size_t feuille);
std::string textePage(const ConceptionElec& c, std::size_t feuille);
// Zone du cadre d'un point (colonnes numérotées de gauche à droite, lignes lettrées de haut en bas) : « B3 »
struct ZoneCadre {
  int colonne = 1;  // 1…
  int ligne = 0;    // 0 : A
  std::string texte() const;
};
int colonnesCadre(const Feuille& f);
int lignesCadre(const Feuille& f);
ZoneCadre zoneDuPoint(const Feuille& f, Pt p);
// Renvoi d'un symbole posé, au format des réglages (« {page}.{zone} » → « 5.B3 »)
std::string texteRenvoi(const ConceptionElec& c, std::size_t feuille, Pt p);
// Renvois d'un composant à plusieurs unités : sous sa première unité (la bobine), la liste des autres (« 5.B3,
// 5.E3 ») ; sous chaque autre, celle de la première ; clé : UUID du symbole posé
std::map<std::string, std::string> renvoisComposants(const ConceptionElec& c);
// Modèle de feuille appliqué en bloc (7.10) : format, cadre, variables fusionnées (celles de la feuille gardées), textes
// et formes marqués « répété » recopiés ; rend le nombre d'éléments ajoutés
int appliquerModeleFeuille(ConceptionElec& c, std::size_t feuille, const Feuille& modele);

// ---------- Échange de portes et de broches (7.8) ----------
// Deux unités d'un même composant échangent leur place au schéma (U1A ↔ U1B) : leurs fils suivent les broches ;
// le circuit reçoit les réseaux du schéma (pistes des pastilles changées retirées) ; rend vrai si l'échange est fait
struct BilanEchange {
  bool fait = false;
  int pistesRetirees = 0;
  std::string raison;
};
BilanEchange echangerPortes(ConceptionElec& c, const std::string& composant, int uniteA, int uniteB);
// Deux broches équivalentes d'une même pose échangent leurs numéros (entrées d'une porte NAND)
BilanEchange echangerBroches(ConceptionElec& c, const std::string& symbolePlace, const std::string& numeroA, const std::string& numeroB);

// ---------- Conception compilée (7.14) ----------
struct ReseauCompile {
  std::string nom, classe;
  std::map<std::string, std::string> regles;  // « isolement » → « 3mm », « impedance » → « 90 »…
  std::vector<std::string> sources;          // directives et zones qui l'ont réglé
  bool masque = false;
};
struct ConceptionCompilee {
  std::map<std::string, ReseauCompile> reseaux;
  std::set<std::string> composantsMasques;  // sous un masque : hors ERC et hors circuit
};
ConceptionCompilee compiler(const ConceptionElec& c, const Connectivite& k);
// Classes des directives transmises aux règles du circuit, par affectation exacte (pas de motif), classes créées au
// besoin ; rend le nombre de réseaux affectés
int appliquerDirectives(ConceptionElec& c);
// Graphe des réseaux (composants et réseaux en nœuds), rangé par ressorts (Fruchterman-Reingold), déterministe
struct NoeudGraphe {
  std::string id, nom;
  bool reseau = false;
  double x = 0, y = 0;
};
struct GrapheReseaux {
  std::vector<NoeudGraphe> noeuds;
  std::vector<std::pair<int, int>> aretes;
};
GrapheReseaux grapheReseaux(const ConceptionElec& c, const Connectivite& k, bool sansAlimentations = true);
void rangerParRessorts(GrapheReseaux& g, int iterations = 200, const std::atomic<bool>* arret = nullptr);

// ---------- Symboles (7.7, 7.18, 7.20) ----------
// Contrôles CS d'un symbole : broches hors grille (CS-01), numéros en double (CS-02), broches superposées (CS-03),
// broche sans nom ni numéro (CS-04), unité vide (CS-05), préfixe absent (CS-06), broches d'alimentation cachées sans
// nom (CS-07) ; vide s'il est sain
std::vector<std::string> controlerSymbole(const DefSymbole& d);
// Broches d'un symbole en CSV (numéro, nom, type, côté, rang, unité, section, groupe), et relues
std::string brochesVersCsv(const DefSymbole& d);
std::vector<SpecBroche> brochesDepuisCsv(const std::string& csv, std::vector<std::string>* journal = nullptr);
// Rangée posée en un geste : « PA », 0 à 15 → PA0 … PA15, numéros à partir de « premierNumero », sur un côté
std::vector<SpecBroche> rangeeBroches(const std::string& prefixe, int de, int a, int premierNumero, int cote, TypeBroche type = TypeBroche::Bidirectionnelle);
// Symbole dérivé : le parent (dessin, broches) complété des champs propres du dérivé ; aplati pour l'enregistrer seul
DefSymbole symboleResolu(const ConceptionElec& c, const DefSymbole& d);
// Assistant de symboles normalisés (7.20) : « MOSFET-P », « MOSFET-N », « NPN », « PNP », « DIODE », « ZENER », « LED »,
// « AOP », « REGULATEUR », dessinés selon le style (« CEI », « ANSI ») et numérotés comme l'empreinte donnée
// (« SOT-23 » : G, S, D sur 1, 2, 3 pour un MOSFET ; B, E, C pour un transistor) ; vide si le genre est inconnu
std::optional<DefSymbole> symboleNormalise(const std::string& genre, const std::string& empreinte, const std::string& style = "CEI");

// ---------- Formes de la feuille (7.19) ----------
struct BilanImportFormes {
  int formes = 0;
  std::vector<std::string> uuids, alertes;
};
// Traits DXF des calques choisis (tous les calques visibles si la liste est vide) en formes modifiables de la feuille,
// sur le calque de notes donné : lignes en polylignes, arcs et cercles tels quels ; le coin bas-gauche de
// l'encombrement en « ou », Y retourné (le schéma a Y vers le bas), « echelle » en plus de l'unité du fichier
BilanImportFormes importerDxfFeuille(ConceptionElec& c, std::size_t feuille, const std::string& texteDxf, const std::set<std::string>& calques, Pt ou, int calqueNotes = 1,
                                     double echelle = 1);
// Formes SVG (lecture_svg) : le coin haut-gauche du dessin en « ou » ; courbes de Bézier, cercles, ellipses,
// rectangles et polylignes gardent leur genre, avec trait, fond et épaisseur
BilanImportFormes importerSvgFeuille(ConceptionElec& c, std::size_t feuille, const std::string& texteSvg, Pt ou, int calqueNotes = 1, double echelle = 1);
// Formes à dessiner, par calque puis par rang de superposition ; calques masqués écartés (« pourPdf » : ceux que le
// PDF omet aussi)
std::vector<const FormeSchema*> formesRangees(const Feuille& f, bool pourPdf = false);
// Rang de superposition : au premier plan, à l'arrière-plan, d'un cran ; rend vrai si la forme existe
enum class Superposition : std::uint8_t { PremierPlan, ArrierePlan, Monter, Descendre };
bool changerOrdre(Feuille& f, const std::string& forme, Superposition s);
// Mesure (outil Mesurer) : longueur d'une polyligne ou distance de deux points, en mm
double mesurer(const std::vector<Pt>& points);

// ---------- Styles et jeux de réglages (7.25) ----------
// Valeurs par défaut des objets de la carte, par famille de couches (0 : valeurs de la 0.33) ; gardées dans
// ConceptionElec::extensions["defautsCarte"]
struct DefautsFamille {
  Nm tailleTexte = 0, epaisseurTexte = 0, epaisseurTrait = 0;
  friend bool operator==(const DefautsFamille&, const DefautsFamille&) = default;
};
struct DefautsCarte {
  DefautsFamille cuivre, serigraphie, fabrication, encombrement, documentation;
  Nm largeurPiste = 0, diametreVia = 0, percageVia = 0;
  friend bool operator==(const DefautsCarte&, const DefautsCarte&) = default;
};
DefautsCarte defautsCarte(const ConceptionElec& c);
void reglerDefautsCarte(ConceptionElec& c, const DefautsCarte& d);
const DefautsFamille& familleDefauts(const DefautsCarte& d, Couche couche);
// Texte de la carte posé avec les valeurs par défaut de sa couche
TexteCircuit texteParDefaut(const ConceptionElec& c, Couche couche, const std::string& texte, Pt p);
DessinCircuit dessinParDefaut(const ConceptionElec& c, Couche couche);
// Style d'un fil ou d'un bus du réseau donné : celui de la classe du réseau quand elle en a un, sinon celui des fils
// (ou des bus), complété de la couleur de la classe de réseau
StyleFil styleDuFil(const ConceptionElec& c, const std::string& reseau, bool bus = false);
// Jeu de réglages réutilisable : styles du schéma, valeurs par défaut de la carte, classes de réseau (couleur et
// règles), enregistré dans un fichier
struct JeuStyles {
  std::string nom, description;
  ReglagesSchema schema;
  DefautsCarte carte;
  std::vector<ClasseReseau> classes;  // créées si absentes, sinon leur couleur reprise
};
std::vector<JeuStyles> jeuxStylesIntegres();  // « standard », « école », « compact »
JeuStyles jeuDeLaConception(const ConceptionElec& c, const std::string& nom);
// Le jeu remplace les styles du schéma et les valeurs par défaut de la carte ; la hauteur de la sérigraphie des
// repères suit celle du jeu ; rend le nombre de classes créées
int appliquerJeuStyles(ConceptionElec& c, const JeuStyles& j);
std::string texteJeuStyles(const JeuStyles& j);
nlohmann::ordered_json reglagesSchemaVersJson(const ReglagesSchema& r);
ReglagesSchema reglagesSchemaDepuisJson(const nlohmann::ordered_json& j);
std::optional<JeuStyles> lireJeuStyles(const std::string& texte, std::string* erreur = nullptr);

}  // namespace prom::ecad

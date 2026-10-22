// Prométhée : articles, recherche dans les bibliothèques, empreintes compatibles, calcul IPC-7351B (lot L4).
//
// Un article est un composant réel : symbole générique, empreinte, brochage (broche → pastilles), valeur,
// paramètres, hauteur, fabricant, références de fournisseurs. La bibliothèque intégrée en compte environ trois
// cents ; une recherche (« 10k 0603 », « self 1210 », « soic8 », « Résistance>1k -0402 ») les trouve par un index
// inversé, et compose à la volée le passif demandé quand aucun article ne l'est encore. Les empreintes proposées
// pour un symbole sont filtrées par compatibilité (une inductance à deux broches : les empreintes d'inductance), et
// le générateur IPC-7351B calcule les pastilles depuis les cotes d'une fiche technique.
// Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- Fichier ----------
nlohmann::ordered_json articleVersJson(const Article& a);
Article articleDepuisJson(const nlohmann::ordered_json& j);
// Fichier d'articles d'une bibliothèque (personnelle, d'équipe) : {"format":"promethee-articles","articles":[…]}
std::vector<Article> lireArticles(const std::string& contenu, std::string* erreur = nullptr);
std::string texteArticles(const std::vector<Article>& articles);

// ---------- Bibliothèque intégrée et héritage ----------
const std::vector<Article>& bibliothequeArticles();
const Article* articleIntegre(const std::string& id);
// Champs d'un article résolus le long de sa chaîne de bases (4.14) : un champ vide prend celui de la base, sauf s'il
// est coupé de l'héritage ; une boucle s'arrête
Article resoudreArticle(const Article& a, const std::function<const Article*(const std::string&)>& trouver);

// ---------- Symbole de l'article et pose ----------
// Symbole générique renuméroté selon le brochage de l'article : chaque broche prend le numéro de sa pastille ; une
// broche sur plusieurs pastilles (« 2,4 ») y ajoute des broches empilées, cachées. Identifiant « art:<id> ».
DefSymbole symboleDeArticle(const Article& a, const DefSymbole& base);
// Installe l'article dans le projet (article, bases, symbole « art:<id> », empreinte) ; rend l'identifiant du symbole
std::string installerArticle(ConceptionElec& c, const Article& a);
// Composant posé d'un symbole « art:<id> » : valeur, empreinte, fabricant, hauteur, paramètres pris de l'article
void remplirDepuisArticle(const ConceptionElec& c, ComposantElec& k);
// Pose l'article : définitions figées dans le projet, composant rempli (valeur, fabricant, hauteur, paramètres…)
std::string poserArticle(ConceptionElec& c, std::size_t feuille, const Article& a, Pt position, int rotation = 0);

// ---------- Recherche (4.4, 4.15) ----------
std::string normaliserRecherche(const std::string& texte);  // minuscules, sans accents ni ponctuation
struct FiltresRecherche {
  bool avecEmpreinte = false, avec3D = false, avecSimulation = false, avecFiche = false, avecReference = false;
};
struct ResultatRecherche {
  Article article;
  double score = 0;
  bool compose = false;  // passif composé à la volée (4.8)
};
class IndexArticles {
 public:
  explicit IndexArticles(std::vector<Article> articles);
  // Requête libre : mots, valeurs (« 10k », « 100n »), boîtiers (« 0603 », « soic8 »), synonymes (« self »,
  // « condo »), fautes légères ; et requêtes riches : « -0402 » exclut, « "texte exact" », jokers « R_*06* »,
  // « /regex/ », comparaison sur un champ (« Résistance>1k », « tension>=25V », « boitier=0603 »)
  std::vector<ResultatRecherche> chercher(const std::string& requete, std::size_t maximum = 50, const FiltresRecherche& filtres = {}) const;
  // « Chercher des semblables » : même catégorie, valeur et boîtier proches, champ par champ
  std::vector<ResultatRecherche> semblables(const Article& a, std::size_t maximum = 20) const;
  const std::vector<Article>& articles() const { return m_articles; }

 private:
  std::vector<Article> m_articles;
  std::vector<std::vector<std::string>> m_jetons;           // jetons de chaque article
  std::map<std::string, std::vector<std::size_t>> m_index;  // jeton → articles
};
// Passif demandé par une requête (« 10k 0603 », « condo 100n 50v », « self 10u 1210 ») : un article composé, avec
// l'empreinte du boîtier demandé (0603 par défaut) et la hauteur générique de cette taille ; rien si ce n'en est pas un
std::optional<Article> passifCompose(const std::string& requete);
// Boîtier reconnu dans un mot (« soic8 » → « SOIC-8 », « sot23 » → « SOT-23 », « 0603 » → « 0603 »)
std::string boitierReconnu(const std::string& mot);

// ---------- Empreintes compatibles (4.6) ----------
// Empreintes du catalogue qui ont autant de pastilles numérotées que le symbole de broches ; « filtre » : de plus
// de la même sorte que l'empreinte pour laquelle le symbole est écrit (inductance : les empreintes d'inductance)
std::vector<InfoEmpreinte> empreintesCompatibles(const DefSymbole& s, bool filtre = true);
// Sorte d'une empreinte au sens de la compatibilité : « L », « R », « Fuse », « SOIC »… ; famille du catalogue sinon
std::string sorteEmpreinte(const std::string& idEmpreinte);

// ---------- Générateur IPC-7351B (4.7) ----------
enum class GabaritIPC : std::uint8_t { Puce, AileDeMouette, SansPatte };
enum class DensiteIPC : std::uint8_t { Plus, Nominale, Moins };  // M (la plus grande), N, L
struct CotesBoitier {
  GabaritIPC gabarit = GabaritIPC::AileDeMouette;
  int broches = 8;          // aile de mouette, sans patte : nombre total (deux rangées, ou quatre côtés si quatreCotes)
  bool quatreCotes = false;
  double pas = 1.27;        // mm
  double lMin = 0, lMax = 0;  // portée bout à bout des pattes (puce : longueur du corps)
  double tMin = 0, tMax = 0;  // longueur de la patte en contact (puce : terminaison)
  double wMin = 0, wMax = 0;  // largeur de la patte (puce : largeur du corps)
  double corpsL = 0, corpsW = 0, hauteur = 0;  // corps, mm
  double plotCentral = 0;   // sans patte : côté du plot thermique, 0 sans
};
struct ProfilIPC {
  double fabrication = 0.05, placement = 0.025;  // tolérances F et P (mm)
  double arrondiZG = 0.05, arrondiX = 0.01;      // arrondis : Z vers le haut, G vers le bas, X au plus proche
};
struct PastillesIPC {
  double z = 0, g = 0, x = 0;            // portée extérieure, écart intérieur, largeur (mm)
  double longueur = 0, largeur = 0;      // dimensions d'une pastille
  double centre = 0;                     // distance du centre de la pastille à l'axe
  double jt = 0, jh = 0, js = 0;         // gains de brasure appliqués
};
PastillesIPC calculerIPC7351(const CotesBoitier& c, DensiteIPC d = DensiteIPC::Nominale, const ProfilIPC& profil = {});
// Empreinte complète (pastilles, sérigraphie, encombrement, corps) et ses noms IPC et KiCad
DefEmpreinte empreinteIPC7351(const CotesBoitier& c, DensiteIPC d = DensiteIPC::Nominale, const ProfilIPC& profil = {});
std::string nomIPC7351(const CotesBoitier& c, DensiteIPC d);   // « SOIC127P600X175-8N »
std::string nomKicadIPC(const CotesBoitier& c);                // « SOIC-8_3.9x4.9mm_P1.27mm »

// ---------- Mise à jour des composants depuis leur article (4.10) ----------
struct DifferenceArticle {
  std::string champ, avant, apres;
};
std::vector<DifferenceArticle> differencesArticle(const ConceptionElec& c, const ComposantElec& k, const Article& a);
// Composants posés d'après cet article qui en diffèrent : « Mettre à jour les 6 composants »
std::vector<std::string> composantsAMettreAJour(const ConceptionElec& c, const Article& a);
int mettreAJourDepuisArticle(ConceptionElec& c, const Article& a, const std::vector<std::string>& composants);

// ---------- Composant générique et standardisation (4.21) ----------
// Article générique provisoire décrit par ses exigences ; ses broches s'ajoutent ensuite dans l'inspecteur
Article articleGenerique(const std::string& exigences, int broches = 0);
struct PropositionStandard {
  std::string composant, article;
  double confiance = 0;  // 0 à 1
  std::string raison;
};
// Pour chaque composant sans article réel (générique, ou posé d'un symbole nu), l'article que la recherche
// paramétrique trouve, avec un indice de confiance ; « exclus » : composants à ne pas toucher
std::vector<PropositionStandard> propositionsStandardisation(const ConceptionElec& c, const IndexArticles& index, const std::set<std::string>& exclus = {});
// Remplace chaque composant proposé par son article (symbole, empreinte, champs), en place ; rend le nombre remplacé
int appliquerStandardisation(ConceptionElec& c, const IndexArticles& index, const std::vector<PropositionStandard>& p);

// ---------- Contrôles de bibliothèque (4.19) et archives de portails (4.22) ----------
// Contrôle d'un article entier : symbole et empreinte trouvés, chaque broche sur une pastille, chaque pastille
// numérotée portée par une broche, numéros en double, préfixe, nom ; vide s'il est sain
std::vector<std::string> controlerArticle(const Article& a, const DefSymbole* s, const DefEmpreinte* e);
// Archive d'un portail (SnapEDA, Ultra Librarian, SamacSys) déposée : ZIP ouvert, symbole (.kicad_sym) et empreinte
// (.kicad_mod) lus par les lecteurs KiCad, modèle 3D joint, contrôles passés ; un seul article
struct ImportArchive {
  Article article;
  std::vector<DefSymbole> symboles;
  std::optional<DefEmpreinte> empreinte;
  std::string nomModele3D, modele3D;  // fichier STEP ou WRL de l'archive
  std::vector<std::string> alertes, controles;
};
ImportArchive importerArchiveComposant(const std::string& zip, const std::string& nomArchive = {});

// ---------- Sources de bibliothèques (4.16) ----------
// « ${BIBLIO_EQUIPE}/passifs.json » : chaque ${NOM} prend la valeur de l'environnement, sinon celle des réglages ;
// une variable inconnue reste telle quelle et se dit dans « inconnues »
std::string developperChemin(const std::string& chemin, const std::map<std::string, std::string>& reglages, std::vector<std::string>* inconnues = nullptr);

}  // namespace prom::ecad

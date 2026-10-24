// Prométhée : tableur et gestion de bibliothèque (lot L4, tâche 4.23 ; SYM-02, SYM-15, BIB-25). Articles créés par
// centaines depuis un CSV ou un XLSX aux colonnes désignées dans n'importe quel ordre ; broches d'un symbole lues d'un
// tableau avec côté, rang et unité ; séries E générées d'après un article modèle ; renommage par gabarit de nom ;
// édition en masse des articles entiers (liens vers les modèles et révisions périmées compris) ; copie vers une
// bibliothèque, nouvelle ou existante, avec tout ce dont chaque article dépend (bases, symbole, empreinte, modèle 3D).
// Licence GPL-3.0-only.
#pragma once

#include <algorithm>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "promethee/articles.hpp"
#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- Nom d'après un gabarit ----------
// Champs remplacés : {id} {nom} {categorie} {valeur} {valeurSI} {boitier} {empreinte} {symbole} {fabricant} {mpn}
// {lcsc} {tolerance} {puissance} {tension} {param:<clé>} ; « R_{boitier}_{valeur} » → « R_0603_4k7 »
std::string nomParGabarit(const Article& a, const std::string& gabarit);
// Identifiant, nom ou les deux renommés ; les doublons reçoivent « _2 », « _3 » ; rend le nombre d'articles renommés
struct RenommageGabarit {
  std::string gabaritId, gabaritNom;  // vide : inchangé
};
int renommerParGabarit(std::vector<Article>& articles, const RenommageGabarit& g, const std::vector<std::string>& ids = {});

// ---------- Séries E ----------
// Une valeur de la série E<n> par pas, de « min » à « max » inclus, sur le modèle d'un article (symbole, empreinte,
// catégorie, paramètres) ; la valeur s'écrit dans la notation compacte de sa famille et les noms suivent les gabarits
std::vector<Article> serieArticles(const Article& modele, int serie, double min, double max, const RenommageGabarit& noms);

// ---------- Articles depuis un tableau ----------
// Champ d'article pour un en-tête de colonne (français ou anglais, sans casse ni accents) : « id », « nom »,
// « valeur », « empreinte », « symbole », « fabricant », « mpn », « lcsc », « categorie », « description », « fiche »,
// « hauteur », « modele3D », « param:<clé> » ; vide pour une colonne inconnue (gardée en paramètre)
std::string champDeColonne(const std::string& entete);
struct ImportTableauArticles {
  std::vector<Article> articles;
  std::vector<std::string> journal;  // lignes refusées et colonnes non reconnues
  std::map<std::string, std::string> colonnes;  // en-tête → champ retenu
};
// La première ligne porte les en-têtes ; « correspondance » impose le champ d'une colonne (en-tête → champ) ; les
// lignes sans valeur ni MPN ni identifiant sont sautées ; un identifiant absent se compose d'après « gabaritId »
ImportTableauArticles articlesDepuisTableau(const std::vector<std::vector<std::string>>& lignes, const std::map<std::string, std::string>& correspondance = {}, const std::string& gabaritId = "{categorie}_{valeur}_{boitier}");
// Valeur d'un champ d'article par son nom de champ, et son écriture (tableau d'édition en masse)
std::string champArticle(const Article& a, const std::string& champ);
bool ecrireChampArticle(Article& a, const std::string& champ, const std::string& valeur);
// Tableau d'édition en masse : une ligne par article, les colonnes demandées ; et sa relecture (colonne « id »)
std::vector<std::vector<std::string>> tableauArticles(const std::vector<Article>& articles, const std::vector<std::string>& champs);
int appliquerTableauArticles(std::vector<Article>& articles, const std::vector<std::vector<std::string>>& tableau, std::vector<std::string>* journal = nullptr);

// ---------- Broches d'un symbole depuis un tableau ----------
// Colonnes reconnues : numéro, nom, type (entrée, sortie, alim…), côté (gauche, droite, haut, bas), rang (ordre sur
// le côté), unité ; rend la spécification prête pour genererSymbole
SpecSymbole specDepuisTableau(const std::vector<std::vector<std::string>>& lignes, const std::string& nom, std::vector<std::string>* journal = nullptr);

// ---------- Bibliothèque complète et copie avec dépendances ----------
struct ContenuBibliotheque {
  std::string nom;
  std::vector<Article> articles;
  std::vector<DefSymbole> symboles;
  std::vector<DefEmpreinte> empreintes;
  std::map<std::string, std::string> modeles3D;  // chemin relatif → contenu (STEP ou WRL)
};
std::string texteContenuBibliotheque(const ContenuBibliotheque& b);
ContenuBibliotheque lireContenuBibliotheque(const std::string& texte, std::string* erreur = nullptr);
// Ce que l'article tire avec lui : bases (héritage), symbole, empreinte et modèle 3D qui ne sont pas fournis par
// Prométhée ; les identifiants introuvables vont dans « manquants »
struct Dependances {
  std::vector<std::string> articles, symboles, empreintes, modeles3D, manquants;
};
Dependances dependancesArticle(const ContenuBibliotheque& source, const std::string& id);
struct RapportCopie {
  int articles = 0, symboles = 0, empreintes = 0, modeles3D = 0;
  std::vector<std::string> remplaces, gardes, manquants;
};
// Copie les articles nommés et leurs dépendances ; un objet déjà présent dans la destination est remplacé si
// « remplacer », gardé sinon (et listé)
RapportCopie copierVersBibliotheque(const ContenuBibliotheque& source, const std::vector<std::string>& ids, ContenuBibliotheque& destination, bool remplacer = false);
// Contrôles de toute une bibliothèque (4.19) : chaque article avec son symbole et son empreinte, cherchés dans la
// bibliothèque puis dans Prométhée ; et révisions périmées (article posé d'une version antérieure)
std::vector<std::string> controlerBibliotheque(const ContenuBibliotheque& b);

// ---------- Bibliothèques d'autres logiciels (10.4, 10.18) : rapport par objet ----------
struct LigneRapportObjet {
  std::string genre;              // « symbole », « empreinte », « article », « modèle 3D »
  std::string nom, etat, detail;  // état : « importé », « remplacé », « gardé », « refusé »
};
struct RapportBibliotheque {
  std::vector<LigneRapportObjet> objets;
  int compter(const std::string& genre, const std::string& etat = {}) const;
  std::string csv() const;  // une ligne par objet, pour un tableur
  // Pose « objet » dans « liste » (repéré par son identifiant) : ajouté, remplacé si « remplacer », gardé sinon ;
  // la ligne du rapport dit ce qui a été fait
  template <class T> void poser(std::vector<T>& liste, T objet, const std::string& genre, const std::string& nom, bool remplacer) {
    auto it = std::find_if(liste.begin(), liste.end(), [&](const T& x) { return x.id == objet.id; });
    if (it == liste.end()) {
      liste.push_back(std::move(objet));
      objets.push_back({genre, nom, "importé", {}});
    } else if (remplacer) {
      *it = std::move(objet);
      objets.push_back({genre, nom, "remplacé", "un objet de même identifiant existait"});
    } else
      objets.push_back({genre, nom, "gardé", "un objet de même identifiant existe déjà ; celui de la bibliothèque lue n'est pas copié"});
  }
  void refuser(const std::string& genre, const std::string& nom, const std::string& raison) { objets.push_back({genre, nom, "refusé", raison}); }
};

}  // namespace prom::ecad

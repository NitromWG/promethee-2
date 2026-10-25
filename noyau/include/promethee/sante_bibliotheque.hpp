// Prométhée : demandes d'articles et santé de la bibliothèque d'équipe (lot L19 de la 0.34 ; 19.4).
//
// Une demande d'article manquant (référence, fiche jointe, champs imposés par l'équipe) se dépose en fichier dans le
// dossier d'équipe (« demandes/<uuid>.json ») ; le bibliothécaire la suit (déposée, prise, publiée, refusée) et le
// demandeur apprend que l'article publié est apparu. Le tableau de bord de santé applique à toute la bibliothèque
// les contrôles d'article, relève les modèles 3D et de simulation manquants et repère les doublons par la référence
// fabricant normalisée (casse, espaces, tirets), puis par les paramètres et la géométrie des pastilles. Deux
// candidats se comparent côte à côte ; une fusion repointe les projets vers l'article gardé et s'inscrit au journal
// de la bibliothèque, et elle s'annule.
// Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"
#include "promethee/gestion_bibliotheque.hpp"

namespace prom {
struct Projet;
}

namespace prom::ecad {

// ---------- Demandes d'article ----------
enum class EtatDemande : std::uint8_t { Deposee, Prise, Publiee, Refusee };
const char* codeEtatDemande(EtatDemande e);  // « deposee », « prise », « publiee », « refusee »
struct DemandeArticle {
  std::string uuid, demandeur, reference, fabricant, fiche, commentaire, date;
  std::map<std::string, std::string> champs;  // champs imposés par l'équipe (« boitier », « tension »…)
  EtatDemande etat = EtatDemande::Deposee;
  std::string bibliothecaire, article, raison;  // article publié (identifiant), raison du refus
  friend bool operator==(const DemandeArticle&, const DemandeArticle&) = default;
};
// Champs que l'équipe impose à une demande : « demandes/champs.json » (liste), vide sans fichier
std::vector<std::string> champsImposes(const std::string& dossierEquipe);
bool imposerChamps(const std::string& dossierEquipe, const std::vector<std::string>& champs);
// Dépôt : refusé avec la raison si une référence ou un champ imposé manque ; la fiche jointe est copiée dans
// « demandes/fiches » ; rend l'UUID donné
bool deposerDemande(const std::string& dossierEquipe, DemandeArticle& d, std::string* erreur = nullptr);
std::vector<DemandeArticle> demandesDeposees(const std::string& dossierEquipe);  // les plus anciennes d'abord
bool suivreDemande(const std::string& dossierEquipe, const std::string& uuid, EtatDemande etat, const std::string& bibliothecaire, const std::string& article = {}, const std::string& raison = {});
// Demandes publiées dont l'article est apparu dans la bibliothèque : à notifier au demandeur
std::vector<DemandeArticle> demandesServies(const std::string& dossierEquipe, const std::string& demandeur, const std::vector<Article>& bibliotheque);

// ---------- Santé ----------
std::string referenceNormalisee(const std::string& mpn);  // « RC0603FR-07 10KL » → « RC0603FR0710KL »
struct ProblemeArticle {
  std::string article, genre, message;  // genre : « contrôle », « modèle 3D », « modèle de simulation »
};
struct DoublonArticles {
  std::vector<std::string> articles;  // identifiants, le premier d'abord
  std::string raison;                 // « même référence fabricant », « mêmes paramètres et pastilles »
};
struct SanteBibliotheque {
  int articles = 0;
  std::vector<ProblemeArticle> problemes;
  std::vector<DoublonArticles> doublons;
  bool interrompu = false;
};
// Sur le fil de l'appelant ; « arret » l'interrompt, « progres » reçoit (fait, total). Les symboles, empreintes et
// modèles 3D d'une bibliothèque d'équipe complète servent aux contrôles ; une simple liste d'articles se contrôle
// contre la bibliothèque intégrée.
SanteBibliotheque santeBibliotheque(const std::vector<Article>& bibliotheque, const std::atomic<bool>* arret = nullptr, const std::function<void(int, int)>& progres = {});
SanteBibliotheque santeBibliotheque(const ContenuBibliotheque& bibliotheque, const std::atomic<bool>* arret = nullptr, const std::function<void(int, int)>& progres = {});
// Articles d'un fichier de bibliothèque, quel qu'en soit le format (bibliothèque d'équipe ou liste d'articles)
ContenuBibliotheque lireFichierBibliotheque(const std::string& chemin, std::string* erreur = nullptr);
struct LigneComparaison {
  std::string champ, a, b;
  bool differe = false;
};
std::vector<LigneComparaison> comparerArticles(const Article& a, const Article& b);

// ---------- Corriger, fusionner, supprimer ----------
// Chaque opération réécrit le fichier de bibliothèque (seul son tableau « articles » change : symboles, empreintes et
// modèles d'une bibliothèque d'équipe restent tels quels), les projets touchés, et s'inscrit au journal de la
// bibliothèque (« journal-bibliotheque.csv » à côté du fichier). Elle garde le contenu d'avant pour s'annuler ;
// l'annulation remet les fichiers et s'inscrit à son tour au journal.
struct OperationBibliotheque {
  bool fait = false;
  std::string genre;                                        // « fusion », « correction », « suppression »
  std::string article, autre;                               // article visé ; fusion : l'article retiré
  std::string champ, valeur;                                // correction
  std::vector<std::string> projetsDonnes;                   // projets examinés
  int composants = 0;                                       // composants repointés, tous projets
  std::vector<std::string> projets;                         // projets réécrits
  std::vector<std::pair<std::string, std::string>> avant;   // chemin → contenu d'avant (bibliothèque comprise)
  std::string fichier, journal, auteur, erreur;
};
// L'article « supprime » rejoint « garde » : il quitte le fichier, les composants des projets donnés qui le citaient
// citent l'article gardé et reprennent ses champs
OperationBibliotheque fusionnerArticles(const std::string& fichierBibliotheque, const std::string& garde, const std::string& supprime, const std::vector<std::string>& projets, const std::string& auteur = {});
// Les composants d'un projet en mémoire qui citent « supprime » citent l'article gardé (symbole « art: » compris) ;
// rend le nombre de composants repointés
int repointerArticle(Projet& p, const Article& garde, const std::string& supprime);
// Champ d'un article : « nom », « description », « fabricant », « mpn », « valeur », « symbole », « empreinte »,
// « fiche », « lcsc », « modele3D », « etatCycle », « numeroInterne », « parametre:<clé> » (valeur vide : retiré)
OperationBibliotheque corrigerArticle(const std::string& fichierBibliotheque, const std::string& id, const std::string& champ, const std::string& valeur, const std::string& auteur = {});
// Refusée si un des projets donnés cite encore l'article (la fusion le repointe d'abord)
OperationBibliotheque supprimerArticle(const std::string& fichierBibliotheque, const std::string& id, const std::vector<std::string>& projets = {}, const std::string& auteur = {});
bool annulerOperation(const OperationBibliotheque& o);
// Opération refaite après une annulation (mêmes paramètres)
OperationBibliotheque refaireOperation(const OperationBibliotheque& o);

}  // namespace prom::ecad

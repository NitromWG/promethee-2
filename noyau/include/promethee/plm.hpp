// Prométhée : liaisons d'entreprise par fichiers (lot L19 de la 0.34 ; 19.14 ; PRJ-37, PRJ-33, BIB-41, BIB-42).
//
// La publication vers le PLM dépose dans le dossier d'équipe, sous « Vers le PLM/<numéro>-<révision> », la
// nomenclature commune (cartes, mécanique, faisceaux), la structure du produit en PLMXML, les modèles et les
// dossiers publiés joints par l'appelant, et un manifeste (numéros internes, révisions, sommes SHA-256). Le petit
// programme séparé de la décision D13 (promethee-en-ligne), seul à ouvrir le réseau et seulement si l'utilisateur
// l'active, lit ces dépôts, parle au PLM et écrit ce qui en revient sous « Depuis le PLM » : états (déposé, réservé,
// restitué en révision), données d'articles au format d'import des données d'achat (12.18). Prométhée ne voit que des
// fichiers. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace prom {
namespace ecad {
struct ConceptionElec;
}

struct Projet;

inline constexpr const char* kDossierVersPlm = "Vers le PLM";
inline constexpr const char* kDossierDepuisPlm = "Depuis le PLM";

struct ArticlePlm {
  std::string numero, revision = "A", designation;
  std::string genre;                       // « produit », « carte », « mécanique », « faisceau », « composant », « fil », « câble », « contact »
  std::string fabricant, mpn, unite = "pièce";
  double quantite = 1;                     // dans son parent
  std::vector<std::string> enfants;        // numéros, dans l'ordre
  std::vector<std::string> fichiers;       // fichiers joints (noms dans le dépôt)
  friend bool operator==(const ArticlePlm&, const ArticlePlm&) = default;
};
struct DepotPlm {
  std::string produit, numero, revision = "A", date, auteur;
  std::vector<ArticlePlm> articles;                           // le produit d'abord, puis sa structure
  std::vector<std::pair<std::string, std::string>> fichiers;  // nom → contenu (modèles STEP AP242, dossiers publiés…)
  const ArticlePlm* article(const std::string& numero) const;
};
// Structure du produit tirée du projet : produit, cartes (et leurs composants), pièces mécaniques, faisceaux des
// projets électriques ; numéros internes des articles (« numeroInterne ») sinon numéros tirés de « prefixe »
DepotPlm preparerDepotPlm(const Projet& p, const std::string& revision, const std::string& date, const std::string& auteur, const std::string& prefixe = "PRM-");
std::string csvNomenclatureCommune(const DepotPlm& d);  // une ligne par lien parent → enfant
std::string plmxml(const DepotPlm& d);                  // PLMXML 6 : Product, ProductRevision, Occurrence
std::string manifestePlm(const DepotPlm& d);            // JSON : produit, numéro, révision, fichiers et leurs SHA-256
// Écrit le dépôt sous « <dossier d'équipe>/Vers le PLM/<numéro>-<révision>/ » ; rend le chemin du dossier, vide en
// cas d'échec
std::string deposerPourPlm(const std::string& dossierEquipe, const DepotPlm& d, std::string* erreur = nullptr);
struct LectureDepotPlm {
  bool ok = false;
  std::string erreur;
  std::string produit, numero, revision;
  std::vector<std::string> fichiers, manquants, sommesFausses;
  int articles = 0;  // lus dans le PLMXML
};
LectureDepotPlm lireDepotPlm(const std::string& dossier);

// ---------- Retour du PLM ----------
struct EtatPlm {
  std::string numero, revision, etat, reservePar, date, message;  // état : « déposé », « réservé », « restitué »
  friend bool operator==(const EtatPlm&, const EtatPlm&) = default;
};
std::vector<EtatPlm> lireEtatsPlm(const std::string& dossierEquipe);  // « Depuis le PLM/etats.json »
std::string texteEtatsPlm(const std::vector<EtatPlm>& v);
bool ecrireEtatsPlm(const std::string& dossierEquipe, const std::vector<EtatPlm>& v);

// ---------- Tickets, synchronisés dans les deux sens par le programme d'accès en ligne ----------
// « Vers le PLM/tickets.json » : ceux que Prométhée ouvre (constats des revues formelles de 12.13) ; « Depuis le
// PLM/tickets.json » : ceux que le gestionnaire de tickets rend, avec leur état et leur numéro chez lui
struct TicketPlm {
  std::string id, distant, titre, texte, etat = "ouvert", objet, auteur, date;
  friend bool operator==(const TicketPlm&, const TicketPlm&) = default;
};
std::vector<TicketPlm> lireTickets(const std::string& dossierEquipe, bool depuisLePlm);
bool ecrireTickets(const std::string& dossierEquipe, bool depuisLePlm, const std::vector<TicketPlm>& v);
// Constats ouverts des revues de la conception, un ticket chacun (« revue-<n>-<constat> ») ; les numéros chez le
// gestionnaire déjà connus (« connus ») sont gardés
std::vector<TicketPlm> ticketsDepuisRevues(const ecad::ConceptionElec& c, const std::vector<TicketPlm>& connus = {});
// Tickets clos chez le gestionnaire : leurs constats se closent dans la conception ; rend le nombre de constats clos
int appliquerTickets(ecad::ConceptionElec& c, const std::vector<TicketPlm>& depuis, const std::string& par, const std::string& date);

}  // namespace prom

// Prométhée : Git du projet et des bibliothèques (lot L12, 12.16 ; BIB-14, BIB-37, PRJ-04). Le programme git du
// poste est lancé par processus.hpp : initialiser, cloner, rattacher un dépôt distant avec sa clé SSH et un essai de
// connexion, pousser, tirer, changer de branche, régler l'auteur, couper Git pour un projet. Rien ne part sur le
// réseau tant que l'utilisateur ne l'a pas activé pour ce dépôt : une adresse réseau (https, ssh, git, hôte:chemin)
// est refusée avant tout lancement, et git lui-même ne reçoit que le protocole « file » (GIT_ALLOW_PROTOCOL).
// Contribution d'un article à une bibliothèque partagée : une branche qui emporte l'article, son symbole, son
// empreinte et son modèle 3D. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom {

struct DepotGit {
  std::string dossier;          // racine du dépôt (le dossier du projet ou de la bibliothèque)
  bool reseauPermis = false;    // activé par l'utilisateur pour ce dépôt
  bool coupe = false;           // Git coupé pour ce projet : aucune commande
  std::string auteur, courriel; // auteur des validations (vide : réglage de git)
  std::string cleSsh;           // clé privée pour les dépôts distants en ssh
};

struct ResultatGit {
  bool ok = false;
  std::string message;  // en français : ce qui a été fait ou pourquoi c'est refusé
  std::string sortie;   // sortie de git
};

// git est-il installé ; « version » : « git version 2.43.0 »
bool gitDisponible(std::string* version = nullptr);
// Adresse qui passerait par le réseau (https://, http://, ssh://, git://, utilisateur@hôte:chemin)
bool adresseReseau(const std::string& url);

ResultatGit gitInitialiser(const DepotGit& d);
ResultatGit gitCloner(const std::string& source, const std::string& dossier, bool reseauPermis);
ResultatGit gitRattacherDistant(const DepotGit& d, const std::string& nom, const std::string& url);
ResultatGit gitEssaiConnexion(const DepotGit& d, const std::string& distant);  // ls-remote, refusé hors réseau permis
ResultatGit gitPousser(const DepotGit& d, const std::string& distant, const std::string& branche);
ResultatGit gitTirer(const DepotGit& d, const std::string& distant, const std::string& branche);
ResultatGit gitBrancher(const DepotGit& d, const std::string& branche, bool creer);
// « fichiers » vide : tout ce qui a changé
ResultatGit gitValider(const DepotGit& d, const std::string& message, const std::vector<std::string>& fichiers = {});
std::string gitBrancheCourante(const DepotGit& d);
std::vector<std::string> gitBranches(const DepotGit& d);
std::vector<std::string> gitFichiersDeBranche(const DepotGit& d, const std::string& branche);
std::string gitAdresseDistant(const DepotGit& d, const std::string& distant);

// Contribution d'un article (12.16) : branche « contribution/<article> » partie de la branche courante, qui emporte
// articles/<id>.json, symboles/<id>.json, empreintes/<id>.json et le modèle 3D (modeles3d/<fichier>) ; la branche de
// départ est rétablie à la fin
struct ContributionArticle {
  bool ok = false;
  std::string branche, erreur;
  std::vector<std::string> fichiers;  // chemins relatifs validés
};
ContributionArticle contribuerArticle(const DepotGit& bibliotheque, const ecad::Article& a, const ecad::DefSymbole* s, const ecad::DefEmpreinte* e,
                                      const std::string& modele3d = {});

}  // namespace prom

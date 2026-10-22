// Prométhée : travail à plusieurs (lot L12 ; 12.4, 12.10, 12.12, 12.14, 12.17 ; PRJ-07, PRJ-09, PRJ-10, PRJ-16,
// PRJ-21, PRJ-23, BIB-33). Verrou en petit fichier à côté du projet, lisible depuis un dossier partagé ; archive du
// projet avec ce qu'il cite hors de lui ; notes attachées aux objets, mentions, tâches, retour de revue de la
// visionneuse HTML ; modèles de projet et reprise de réglages par sections ; index des usages des empreintes,
// symboles et articles dans les dossiers de projets déclarés. Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom {
struct Projet;

// ---------- Verrou du projet (12.10) ----------
struct VerrouProjet {
  std::string personne, poste, depuis;  // depuis : ISO 8601
  std::int64_t vie = 0;                 // dernier signal de vie, secondes depuis 1970 (renouvelé chaque minute)
  std::int64_t processus = 0;
};
std::string cheminVerrou(const std::string& projet);  // « .~<nom>.verrou » à côté du projet
std::optional<VerrouProjet> lireVerrou(const std::string& projet);
// Un verrou sans signal de vie depuis « delai » secondes est périmé : son poste s'est arrêté sans le lever
bool verrouVivant(const VerrouProjet& v, std::int64_t maintenant, int delai = 180);
std::int64_t secondesMaintenant();
// Pose ou reprend le verrou ; faux s'il est tenu, vivant, par une autre personne ou un autre poste (« tenu » le dit)
bool poserVerrou(const std::string& projet, const std::string& personne, const std::string& poste, VerrouProjet* tenu = nullptr);
void renouvelerVerrou(const std::string& projet, const std::string& personne, const std::string& poste);
void leverVerrou(const std::string& projet, const std::string& personne, const std::string& poste);
std::string decrireVerrou(const VerrouProjet& v);  // « Tenu par Lou sur ATELIER-2 depuis le 22/10/2026 à 14:05 »

// ---------- Archive du projet (12.4) ----------
// Le projet et les fichiers qu'il cite hors de lui (modèles SPICE, sources de programme, schémas QElectroTech, carte
// KiCad d'origine, fiches locales), rangés sous « fichiers/ » avec leurs chemins réécrits ; MANIFESTE.json porte
// l'empreinte SHA-256 de chaque fichier. « manquants » : les fichiers cités introuvables.
std::string archiverProjet(const Projet& p, const std::string& cheminProjet, std::vector<std::string>* manquants = nullptr);
// Extrait l'archive dans « dossier » ; rend le chemin du projet extrait ; « defauts » : empreintes qui ne
// correspondent pas (archive abîmée)
std::string extraireArchive(const std::string& zip, const std::string& dossier, std::vector<std::string>* defauts = nullptr);

// ---------- Notes attachées aux objets (12.12) ----------
std::string ajouterNote(ecad::ConceptionElec& c, ecad::NoteObjet n);  // UUID et date remplis au besoin ; rend l'UUID
std::string repondre(ecad::ConceptionElec& c, const std::string& parent, const std::string& auteur, const std::string& texte);
bool clore(ecad::ConceptionElec& c, const std::string& note, bool close = true);
std::vector<const ecad::NoteObjet*> notesDe(const ecad::ConceptionElec& c, const std::string& objet);
std::vector<std::string> mentions(const std::string& texte);  // « @Lou, @Évan » → « Lou », « Évan »
// Ce qui attend une personne à l'ouverture : tâches ouvertes dont elle répond, notes ouvertes qui la mentionnent
std::vector<const ecad::NoteObjet*> enAttente(const ecad::ConceptionElec& c, const std::string& personne);
// Objet d'une note : repère du composant, nom du réseau, « carte »
std::string objetLisible(const ecad::ConceptionElec& c, const ecad::NoteObjet& n);
// Retour de revue écrit par la visionneuse HTML : {"format":"promethee-revue","conception":…,"remarques":[{"repere":
// "R7","texte":…,"auteur":…}]} ; chaque remarque devient une note sur l'objet ; rend leur nombre
int importerRetourRevue(ecad::ConceptionElec& c, const std::string& json, std::vector<std::string>* alertes = nullptr);

// ---------- Modèles de projet et reprise de réglages (12.14) ----------
// Copie d'un projet modèle sous un autre nom : conceptions renouvelées (UUID), publications, verrous et notes retirés
Projet projetDepuisModele(const Projet& modele, const std::string& nom);
// Sections : « regles » (règles et classes), « empilage », « cartouche », « serigraphie », « fabrication »,
// « variables », « jeuxSorties », « erc », « affichage » ; rend les sections reprises
std::vector<std::string> reprendreReglages(ecad::ConceptionElec& cible, const ecad::ConceptionElec& source, const std::set<std::string>& sections);

// ---------- Index des usages (12.17) ----------
struct UsageObjet {
  std::string projet, carte;          // chemin du fichier, nom de la conception
  std::vector<std::string> references;
};
struct IndexUsages {
  std::map<std::string, std::vector<UsageObjet>> empreintes, symboles, articles;
  int projets = 0;
  std::vector<std::string> illisibles;
};
// Parcourt les dossiers (fichiers .prom et .prom.json, sous-dossiers compris) ; « arret » l'interrompt
IndexUsages indexerUsages(const std::vector<std::string>& dossiers, const std::atomic<bool>* arret = nullptr);

}  // namespace prom

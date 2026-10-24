// Prométhée : remplacer un composant par un autre article (lot L3, tâche 3.16 ; SAI-42), et modifier une propriété
// objet par objet sur toute une sélection (tâche 3.15 ; SAI-15, SAI-16, SAI-41). Le remplacement garde le repère, la
// place et l'orientation ; chaque broche reliée retrouve sa remplaçante, par son nom ou par son numéro, et ses fils
// restent accrochés (retracés à l'équerre quand la broche n'est plus au même endroit). Une broche reliée qui n'aurait
// pas de remplaçante bloque le remplacement : l'aperçu la nomme. Le tout fait une seule commande ; l'empreinte du
// nouvel article se remplace en place sur le circuit par la synchronisation (3.7).
// Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"
#include "promethee/valeurs.hpp"

namespace prom::ecad {

// Ce qui l'emporte, champ par champ : l'ancien composant (« garder ») ou le nouvel article
struct RegleRemplacement {
  bool brochesParNom = true;  // sinon par numéro
  bool garderRepere = true;
  bool garderValeur = false;
  bool garderEmpreinte = false;
  bool garderFabricant = false;  // fabricant, référence du fabricant, fiche technique
};

struct ApercuRemplacement {
  struct Broche {
    std::string avant, apres;  // numéros réels, ancien et nouveau ; nom de la broche
    std::string nom;
    bool deplacee = false;     // le point de connexion change : ses fils sont retracés
  };
  std::vector<Broche> reliees;        // broches reliées et leur remplaçante
  std::vector<std::string> perdues;   // broches reliées sans remplaçante (« 3 (+IN) ») : le remplacement est refusé
  int fils = 0;                       // fils accrochés aux broches reliées, qui le restent
  std::string empreinteAvant, empreinteApres;
  bool possible() const { return perdues.empty(); }
};
ApercuRemplacement apercuRemplacement(const ConceptionElec& c, const std::string& composant, const std::string& symbole, const RegleRemplacement& r = {});
// Remplace chaque composant donné ; rend le nombre de composants remplacés, 0 et la raison dans « erreur » si l'un
// d'eux perdrait une broche reliée (rien n'est alors changé)
int remplacerComposants(ConceptionElec& c, const std::vector<std::string>& composants, const std::string& symbole, const RegleRemplacement& r = {}, std::string* erreur = nullptr);

// Modification d'un texte, objet par objet (3.15) : remplacement « de → à », ajout au début ou à la fin, ou formule où
// « x » est la valeur actuelle (« x*2 » double une valeur de résistance, écrite ensuite dans l'unité de sa famille)
struct RegleTexte {
  enum class Genre : std::uint8_t { Remplacer, Prefixe, Suffixe, Formule } genre = Genre::Suffixe;
  std::string de, a;  // Remplacer : « de » → « a » ; Préfixe et Suffixe : « a » ; Formule : « a »
};
std::optional<std::string> transformerTexte(const std::string& texte, const RegleTexte& r, FamilleValeur famille = FamilleValeur::Aucune, std::string* erreur = nullptr);

// ---------- Mise à jour et échange en masse (4.20 ; SYM-14, SYM-21, EMP-14) ----------
// Portée : la sélection, ou des motifs à jokers (« * », « ? », sans casse) sur le repère, la valeur, le symbole et
// l'empreinte ; une valeur sans joker se compare comme nombre (« 10k » vise « 10 kΩ » et « 10K »)
struct PorteeMasse {
  std::vector<std::string> selection;  // non vide : seulement ces composants
  std::string reference, valeur, symbole, empreinte;
};
bool correspondJoker(const std::string& motif, const std::string& texte);
std::vector<std::string> composantsVises(const ConceptionElec& c, const PorteeMasse& p);
// Empreintes échangées en masse sur la carte, par le remplacement en place de 3.7 ; ce qui est gardé ou réinitialisé :
// textes déplacés (repère), attributs (hors nomenclature, hors placement, couleur), marges des pastilles retouchées
// (copie locale, 7.24) et modèle 3D associé
struct RegleEchangeEmpreinte {
  bool garderTextes = true, garderAttributs = true, garderMarges = true, garderModele3D = true;
};
int echangerEmpreintesEnMasse(ConceptionElec& c, const std::vector<std::string>& composants, const std::string& empreinte, const RegleEchangeEmpreinte& r = {}, std::string* erreur = nullptr);

// ---------- Affectation des empreintes par règles d'équivalence (4.28 ; EMP-13) ----------
// « Résistance 0603 → R_0603 », « C* 0805 -> C_0805 », « Condensateur 100n* 0402 → C_0402 » : une famille (nom ou
// motif de repère), un boîtier facultatif, un motif de valeur facultatif, l'empreinte à donner
struct RegleEquivalence {
  FamilleValeur famille = FamilleValeur::Aucune;
  std::string reference;   // motif de repère (« C* »), à défaut de famille
  std::string boitier;     // « 0603 » : lu dans l'empreinte actuelle, le champ Boîtier ou la valeur
  std::string valeur;      // motif de valeur
  std::string empreinte;
  std::string texte;       // la règle telle qu'écrite
};
std::optional<RegleEquivalence> lireRegleEquivalence(const std::string& texte, std::string* erreur = nullptr);
std::string boitierDeComposant(const ComposantElec& k);
std::vector<std::string> composantsDeRegle(const ConceptionElec& c, const RegleEquivalence& r, bool aussiAffectes = false);
// Toutes les règles en une commande : chaque composant visé reçoit l'empreinte de la première règle qui le vise (déjà
// posé sur la carte : remplacée en place) ; rend le nombre de composants affectés
int appliquerEquivalences(ConceptionElec& c, const std::vector<RegleEquivalence>& regles, bool aussiAffectes = false, std::vector<std::string>* journal = nullptr);

// Pinceau de propriétés (SAI-41) : ce qu'on prend sur un composant pour l'appliquer à d'autres
struct ProprietesPinceau {
  bool valeur = true, empreinte = false, fabricant = false, parametres = true, champs = false, exclusions = true, hauteur = false;
};
// Recopie les propriétés choisies de « modele » sur le composant « cible » ; vrai si quelque chose a changé
bool appliquerPinceau(ConceptionElec& c, const ComposantElec& modele, const std::string& cible, const ProprietesPinceau& p);

}  // namespace prom::ecad

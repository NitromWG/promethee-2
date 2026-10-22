// Prométhée : une seule hauteur par composant (lot L3, tâche 3.4 ; MEC-02, P4, P5, P17, T2, T5). La 3D de l'atelier,
// le lien avec le boîtier et la vérification du circuit lisent la même fonction, hauteurComposant(), qui rend la
// hauteur en mm, sa provenance et un texte de source. Rangs : modèle exact, fiche (hauteur confirmée sur la fiche
// technique), saisie (tapée, non confirmée), générique (corps de l'empreinte, ou pastille plate). Le corps est posé
// sur le centre de son encombrement, décalé de l'origine de l'empreinte (connecteur USB dont l'origine est à la
// broche 1).
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "promethee/circuit.hpp"
#include "promethee/schema.hpp"

namespace prom::ecad {

struct ConceptionElec;

// ProvenanceHauteur : schema.hpp, avec le composant qui la porte
const char* nomProvenance(ProvenanceHauteur p);  // « générique », « saisie », « fiche », « modèle exact »
const char* codeProvenance(ProvenanceHauteur p);  // pour le fichier : « generique », « saisie », « fiche », « modele »
ProvenanceHauteur provenanceDepuisCode(const std::string& code);

struct HauteurComposant {
  double mm = 0;
  ProvenanceHauteur provenance = ProvenanceHauteur::Generique;
  std::string source;  // « corps de R_0805 », « tapée dans l'inspecteur », « fiche Yageo RC_P »
  // « 1,10 mm · fiche »
  std::string texte() const;
  // Le contrôle du boîtier n'est complet qu'avec une hauteur confirmée (fiche ou modèle exact)
  bool confirmee() const { return provenance == ProvenanceHauteur::Fiche || provenance == ProvenanceHauteur::ModeleExact; }
};

// Hauteur pour la 3D, le boîtier et la vérification : un composant sans empreinte connue n'a pas de corps (0 mm) ;
// une empreinte sans corps donne 0,1 mm (pastille plate), en générique
HauteurComposant hauteurComposant(const ComposantElec* x, const DefEmpreinte* d);
HauteurComposant hauteurComposant(const ConceptionElec& c, const ComposantElec& x);

// Hauteur générique de l'empreinte (son corps), en mm ; 0 sans corps
double hauteurGenerique(const DefEmpreinte& d);
bool empreinteCms(const DefEmpreinte& d);  // aucune pastille percée
// Vraisemblance d'une hauteur tapée : plus de trois fois la générique, ou plus de 25 mm sur un boîtier CMS, demande
// une confirmation ; rend la phrase à montrer, vide si la hauteur est vraisemblable
std::string hauteurAConfirmer(double mm, const DefEmpreinte* d);

// Corps posé sur la carte, tel que la 3D et le boîtier le dessinent : centre dans le repère de la carte, dimensions
// dans le repère de l'empreinte, rotation de l'empreinte, hauteur et sa provenance ; vide sans empreinte connue
struct CorpsPose {
  Pt centre;
  double largeur = 0, profondeur = 0;  // mm
  double rotation = 0;                 // degrés
  bool rond = false, dessous = false;
  HauteurComposant hauteur;
  std::string couleur;
};
std::optional<CorpsPose> corpsPose(const ConceptionElec& c, const EmpreintePlacee& e);

// Trou de fixation reconnu par sa géométrie (3.5, T6, EMP-18), quel que soit le nom de l'empreinte : une seule
// pastille percée d'au moins 2,0 mm (les autres sans perçage, ou de petits perçages de couture), et au plus un
// numéro de broche ; rend le diamètre du perçage en mm. Le réseau (aucun, ou une masse) se vérifie à part.
std::optional<double> trouDeFixation(const DefEmpreinte& d);
// Vis d'un perçage de passage : 2,2 → M2, 2,7 → M2.5, 3,2 → M3, 4,3 → M4, 5,3 → M5, 6,4 → M6 (à 0,3 mm près) ; vide
// sinon
std::string visDuPercage(double mm);
// Réseau qui laisse un trou de fixation en être un : aucun, une broche seule (« N-H1-1 »), ou une masse
bool reseauDeFixation(const std::string& reseau);

// Centre du corps dans le repère de l'empreinte : le décalage enregistré (T5)
Pt centreCorps(const DefEmpreinte& d);
// Décalage d'office : le centre de l'encombrement, arrondi au micromètre (import KiCad, génération, migration)
Pt decalageParDefaut(const DefEmpreinte& d);

}  // namespace prom::ecad

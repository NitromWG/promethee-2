// Prométhée : opérations de structure sur les pièces et les assemblages d'un projet. Licence GPL-3.0-only.
//
// Un composant d'assemblage désigne sa pièce ou son sous-assemblage par son rang dans le projet, et une
// contrainte ses deux composants par leur rang dans l'assemblage. Toute suppression ou réorganisation passe
// par ces fonctions, qui tiennent ces rangs à jour (AS-01, AS-02, AS-27), et toute lecture par
// validerAssemblages, qui écarte en le disant ce qui ne désigne rien (AS-28, AS-32). Les parcours de
// sous-assemblages sont bornés et protégés des boucles (AS-02, AS-17).
#pragma once

#include <set>
#include <string>
#include <vector>

#include "promethee/modele.hpp"

namespace prom {

// Profondeur maximale d'imbrication des sous-assemblages parcourue (au-delà : boucle ou fichier abîmé)
inline constexpr int PROFONDEUR_ASSEMBLAGE_MAX = 32;

// Types de contraintes d'assemblage que le solveur sait résoudre
bool typeContrainteAssemblageConnu(const std::string& type);
// Références d'assemblage (A2) : plans, axes et origine de l'assemblage lui-même, « assemblage:XY », « assemblage:XZ »,
// « assemblage:YZ », « assemblage:X », « assemblage:Y », « assemblage:Z », « assemblage:O ». Elles ne bougent pas ; le
// côté d'une contrainte qui en porte une garde l'indice du composant de l'autre côté (la contrainte vit et meurt avec lui).
inline constexpr const char* PREFIXE_REFERENCE_ASSEMBLAGE = "assemblage:";
bool referenceAssemblage(const std::string& ref);
// Côté réel d'une contrainte : faux si les deux côtés sont des références d'assemblage (contrainte vide de sens)
bool contrainteVersAssemblage(const ContrainteAssemblage& c);

bool rangPieceValide(const Projet& p, int piece);
bool rangAssemblageValide(const Projet& p, int assemblage);

// Assemblages qui ont la pièce (ou l'assemblage) pour composant direct, sans doublon, dans l'ordre du projet
std::vector<int> assemblagesUtilisantPiece(const Projet& p, int piece);
std::vector<int> assemblagesUtilisantAssemblage(const Projet& p, int assemblage);

// Vrai si « dans » contient « cherche », directement ou par ses sous-assemblages (ou lui est égal) ;
// sûr devant les rangs invalides et les boucles.
bool contientAssemblage(const Projet& p, int dans, int cherche);

// Quantité de chaque pièce dans un assemblage, sous-assemblages développés, composants suspendus exclus
// (rang de pièce → quantité). Les boucles et rangs invalides sont ignorés.
std::vector<std::pair<int, int>> quantitesPieces(const Projet& p, int assemblage);
// Nomenclature mécanique (EX-01, EX-11) : un assemblage (≥ 0) développé récursivement, ou tout le projet (−1) : ses
// assemblages racines (sous-assemblage d'aucun autre), plus les pièces qui ne sont dans aucun assemblage (une fois).
// Composants suspendus exclus ; un composant symétrique (nombre impair de symétries sur le chemin) est un article
// distinct de la pièce d'origine.
struct LigneNomenclatureMeca {
  int piece = 0;
  bool symetrique = false;
  int quantite = 0;
};
std::vector<LigneNomenclatureMeca> nomenclatureMecanique(const Projet& p, int assemblage = -1);

// Nomenclature à niveaux (M10) : l'arbre de l'assemblage, chaque niveau regroupant les composants identiques (même
// pièce ou même sous-assemblage, même symétrie) dans l'ordre de leur première apparition. « numero » suit la
// structure (« 2 », « 2.1 », « 2.1.3 ») ; « quantite » compte dans le parent, « quantiteTotale » dans l'assemblage
// de départ. Suspendus exclus, boucles et rangs invalides ignorés.
struct LigneNomenclatureNiveau {
  int niveau = 0;              // 0 : composant direct de l'assemblage
  std::string numero;
  bool assemblage = false;     // sous-assemblage (indice dans p.assemblages), sinon pièce (indice dans p.pieces)
  int indice = 0;
  bool symetrique = false;
  int quantite = 0, quantiteTotale = 0;
};
std::vector<LigneNomenclatureNiveau> nomenclatureNiveaux(const Projet& p, int assemblage);
// Repère stable d'un article : celui de la pièce (ou de sa version symétrique), ou du sous-assemblage ; 0 si aucun
int repereArticle(const Projet& p, bool assemblage, int indice, bool symetrique);
// Invariant des repères (M10) : chaque pièce et chaque assemblage a un repère ≥ 1, unique dans le projet ; la version
// symétrique d'une pièce employée symétriquement en a un aussi. Les manquants et les doublons reçoivent le suivant
// du plus grand ; rend le nombre de repères attribués.
int assurerReperes(Projet& p);

struct BilanRetrait {
  int composants = 0;   // composants retirés
  int contraintes = 0;  // contraintes retirées avec eux
};
// Retire le composant k et les contraintes qui le visent ; les rangs suivants sont décalés.
BilanRetrait retirerComposant(Assemblage& a, int k);
// Retire plusieurs composants en une fois.
BilanRetrait retirerComposants(Assemblage& a, const std::set<int>& ks);
// Supprime la pièce de rang « piece » : les composants qui la visent sont retirés de tous les assemblages, avec
// leurs contraintes, et les rangs des pièces suivantes sont décalés.
BilanRetrait supprimerPiece(Projet& p, int piece);
// Supprime l'assemblage de rang « assemblage » ; refuse (faux, raison remplie) s'il est utilisé comme
// sous-assemblage. Les rangs des assemblages suivants sont décalés.
bool supprimerAssemblage(Projet& p, int assemblage, std::string* raison = nullptr);

// Contraintes de « a » qui relient la sélection au reste : elles seraient coupées par un sous-assemblage.
std::vector<int> contraintesCoupees(const Assemblage& a, const std::set<int>& selection);
// Range les composants choisis de l'assemblage « parent » dans un nouvel assemblage, inséré à leur place comme
// sous-assemblage ; les contraintes internes suivent, celles qui reliaient la sélection au reste sont gardées et
// visent la face par son chemin dans le sous-assemblage (« @k/face », AS-16) ; une liaison y perd son état de
// départ, à recalculer (creerLiaison). Le sous-assemblage reste fixe si un composant fixe (ou le premier) en fait partie.
// Rend le rang du nouvel assemblage, −1 si rien n'a été fait (moins de deux composants valides).
int creerSousAssemblage(Projet& p, int parent, const std::set<int>& selection, const std::string& nom = {});

// Contrôle après lecture : contraintes dont un composant n'existe pas, composants dont la pièce ou le
// sous-assemblage n'existe pas, sous-assemblages qui se contiennent eux-mêmes. Les références impossibles
// sont retirées, les types inconnus gardés (le solveur les ignore) ; tout est signalé dans « alertes ».
void validerAssemblages(Projet& p, std::vector<std::string>* alertes);

// Invariant : chaque pièce a un identifiant non vide et unique (« id »), qui la désigne pendant l'édition (AP-02).
// Les pièces qui n'en ont pas, ou dont l'identifiant est pris, en reçoivent un ; rend le nombre de pièces changées.
int assurerIdentifiantsPieces(Projet& p, std::vector<std::string>* alertes = nullptr);

}  // namespace prom

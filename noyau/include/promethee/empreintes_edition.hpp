// Prométhée : édition des empreintes de la 0.34 (lot L7, tâches 7.9, 7.21, 7.24 ; EMP-06, EMP-21, EMP-22, EMP-23,
// EMP-24, EMP-30, EMP-31, PLA-33). Tableau des pastilles et doublons, création depuis un fichier de coordonnées,
// renumérotation en série ou en matrice (lettres JEDEC sans I, O, Q, S…), pastille modèle, cote entre deux pastilles
// qui les écarte ; mise à jour des composants posés sur demande. Cuivre propre à la carte : empreinte posée retouchée
// en copie locale dont l'écart à la bibliothèque reste lisible, empreinte éclatée en objets libres ; générateurs de
// bobines planaires (Wheeler modifiée), de pistes résistives et de capteurs capacitifs, avec leur zone dégagée.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- Pastilles (7.21) ----------
// Numéros en double (hors pastilles d'un même numéro voulues : languettes, plots) : chaque numéro et ses indices
std::vector<std::pair<std::string, std::vector<std::size_t>>> doublonsPastilles(const DefEmpreinte& d);
// Pastilles lues d'un fichier de coordonnées (CSV : numéro, x, y [, largeur, hauteur, forme, perçage]) sur le modèle
DefEmpreinte empreinteDepuisCoordonnees(const std::string& id, const std::string& csv, const Pastille& modele, std::vector<std::string>* journal = nullptr);
// Renumérotation : en série (« 1 », « 2 »… dans l'ordre de lecture : lignes de haut en bas, gauche à droite), ou en
// matrice JEDEC (rangée lettrée A, B… sans I, O, Q, S, X, Z ; colonne numérotée) ; rend le nombre de pastilles
int renumeroterSerie(DefEmpreinte& d, int premier = 1, const std::string& prefixe = "");
int renumeroterMatrice(DefEmpreinte& d);
// La pastille modèle donne sa forme et ses cotes aux pastilles choisies (numéros) ; rend leur nombre
int appliquerPastilleModele(DefEmpreinte& d, const Pastille& modele, const std::vector<std::string>& numeros);
// Cote entre deux pastilles (centre à centre) réglée : la seconde se déplace le long de leur axe ; faux si inconnues
bool reglerCote(DefEmpreinte& d, const std::string& a, const std::string& b, Nm distance);
// Composants posés de cette empreinte qui diffèrent de la nouvelle définition ; mis à jour sur demande
std::vector<std::string> composantsDeLEmpreinte(const ConceptionElec& c, const std::string& id);
int mettreAJourEmpreinte(ConceptionElec& c, const DefEmpreinte& nouvelle, const std::vector<std::string>& composants);

// ---------- Cuivre propre à la carte (7.24) ----------
// L'empreinte posée devient une copie locale (« SOIC-8~local~U2 ») qu'on retouche ; la bibliothèque et les autres
// poses ne changent pas. EmpreintePlacee::empreinte désigne la copie, copieLocale l'empreinte d'origine.
std::string copierEmpreinteLocale(ConceptionElec& c, const std::string& composant);
// Écart lisible entre la copie locale et sa bibliothèque (« pastille 1 : 1,550 × 0,600 → 1,950 × 0,600 mm ») ; vide
// sans copie ou sans écart
std::vector<std::string> ecartBibliotheque(const ConceptionElec& c, const std::string& composant);
bool revenirBibliotheque(ConceptionElec& c, const std::string& composant);
// Empreinte éclatée en objets libres de la carte : pastilles en régions de cuivre (de leur réseau), traits en dessins ;
// le composant posé sans schéma disparaît ; rend le nombre d'objets créés (0 si refusé : composant du schéma)
int eclaterEmpreinte(ConceptionElec& c, const std::string& composant, std::string* raison = nullptr);

// ---------- Générateurs (7.24) ----------
enum class FormeBobine : std::uint8_t { Carree, Hexagonale, Octogonale, Circulaire };
struct Bobine {
  FormeBobine forme = FormeBobine::Carree;
  int spires = 5;
  double diametreExterieur = 10, largeur = 0.25, ecart = 0.25;  // mm
  Couche couche = Couche::CuivreDessus;
};
// Inductance par la formule de Wheeler modifiée (Mohan, 1999) : L = K1 µ0 n² d_moy / (1 + K2 ρ), en henrys
double inductanceBobine(const Bobine& b);
double diametreInterieur(const Bobine& b);
// Spirale en pistes, centrée en « centre », sur le réseau donné ; rend les UUID posés
std::vector<std::string> poserBobine(ConceptionElec& c, const Bobine& b, Pt centre, const std::string& reseau = {});
// Piste résistive en méandre entre deux points : longueur pour la résistance demandée, d'après la résistance
// carrée du cuivre (épaisseur de l'empilage, 17 µΩ·mm) ; rend la longueur posée (mm)
double poserPisteResistive(ConceptionElec& c, Pt a, Pt b, double ohms, Nm largeur, const std::string& reseau, Couche couche = Couche::CuivreDessus);
// Capteur capacitif : bouton rond, curseur à n segments en chevrons ou roue ; zone d'interdiction de cuivre autour
enum class GenreCapteur : std::uint8_t { Bouton, Curseur, Roue };
struct Capteur {
  GenreCapteur genre = GenreCapteur::Bouton;
  int segments = 1;
  double taille = 12;   // diamètre du bouton ou de la roue, longueur du curseur (mm)
  double degage = 1.0;  // zone dégagée autour (mm)
  std::string prefixeReseau = "TOUCH";
};
// Rend le nombre de segments posés (régions de cuivre), avec la zone interdite posée autour
int poserCapteur(ConceptionElec& c, const Capteur& k, Pt centre);

}  // namespace prom::ecad

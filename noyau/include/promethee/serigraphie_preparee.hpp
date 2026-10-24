// Prométhée : sérigraphie préparée pour la fabrication (lot L8 de la 0.34 ; 8.13 ; DRC-38, CAR-33, CAR-34). Les traits
// des empreintes rognés par soustraction (Clipper2) du cuivre découvert et des ouvertures de vernis, élargis d'une
// marge ; les repères posés sur une pastille écartés par le placement automatique ; ce qui sort de la carte
// supprimé. Chaque changement est listé avant d'être appliqué ; un trait rogné passe dans une copie locale de
// l'empreinte (7.24), la bibliothèque n'est pas touchée. Numéro de série par copie d'un panneau, au format du projet.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct OptionsSerigraphie034 {
  Nm marge = 100'000;          // écart gardé autour des ouvertures de vernis et du cuivre découvert
  Nm longueurMin = 100'000;    // un morceau de trait plus court disparaît
  bool rogner = true, ecarterReperes = true, supprimerHorsCarte = true;
};
struct ChangementSerigraphie {
  enum class Genre : std::uint8_t { Rogne, Deplace, Supprime } genre = Genre::Rogne;
  std::string composant;  // UUID ; vide pour un objet de la carte
  std::string description;
};
// Ce que la préparation ferait, sans rien changer
std::vector<ChangementSerigraphie> preparerSerigraphie(const ConceptionElec& c, const OptionsSerigraphie034& o = {});
// Les changements appliqués ; rend leur liste
std::vector<ChangementSerigraphie> appliquerPreparationSerigraphie(ConceptionElec& c, const OptionsSerigraphie034& o = {});
// Traits de sérigraphie (empreintes posées, dessins et textes de la carte) qui touchent une ouverture de vernis
// élargie de la marge : vide après la préparation
std::vector<std::string> serigraphieSurPastilles(const ConceptionElec& c, Nm marge = 0);

// Numéro de série d'une copie (8.13) : format du projet (« SN-{n:04} », départ), gardé dans extensions["numeroSerie"] ;
// un texte de carte qui contient ${SERIE} le reçoit, copie par copie, dans les fichiers du panneau
struct FormatNumeroSerie {
  std::string format = "SN-{n:04}";
  int depart = 1;
};
FormatNumeroSerie formatNumeroSerie(const ConceptionElec& c);
void reglerNumeroSerie(ConceptionElec& c, const FormatNumeroSerie& f);
std::string numeroSerie(const FormatNumeroSerie& f, int copie);  // copie : 0, 1, 2…
// La conception de la copie : ${SERIE} remplacé dans les textes de la carte
ConceptionElec conceptionNumerotee(const ConceptionElec& c, int copie);

}  // namespace prom::ecad

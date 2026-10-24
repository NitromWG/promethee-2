// Prométhée : dossier de plans de la carte (lot L8 de la 0.34 ; 8.30 ; DOC-03, DOC-13, DOC-15, DOC-16, DOC-17). Des
// feuilles de plan (format, cadre, cartouche) portent des vues liées à la carte — couches tirées du modèle de
// fabrication, vue de côté des corps, coupe de l'empilage, détails rectangulaires ou circulaires — et des tableaux
// (perçages, lignes de transmission, nomenclature) recalculés à chaque rendu : un trou ajouté à la carte entre au
// tableau sans autre geste. Les notes viennent d'un catalogue de notes types, numérotées, et se changent en masse.
// Les mêmes tableaux se posent sur une couche de dessin du circuit ; l'empilage sort en XLSX. Le dossier est gardé
// dans ConceptionElec::extensions["plans"]. Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct VuePlan {
  enum class Genre : std::uint8_t { Couche, Cote, Empilage, Detail, TableauPercage, TableauLignes, Nomenclature, Notes } genre = Genre::Couche;
  std::string titre;
  std::string couche = "F.Cu";  // vue de couche : code ; vue de côté : « avant » ou « gauche »
  double x = 20, y = 20;        // mm : coin haut gauche sur la feuille
  double echelle = 1;           // vue de couche, détail
  Pt centre;                    // détail : centre sur la carte
  double rayon = 5;             // détail : demi-côté ou rayon (mm)
  bool circulaire = false;      // détail circulaire
  friend bool operator==(const VuePlan&, const VuePlan&) = default;
};
struct NotePlan {
  int numero = 1;
  std::string texte;
  friend bool operator==(const NotePlan&, const NotePlan&) = default;
};
struct FeuillePlan {
  std::string nom = "Plan de fabrication", format = "A3";
  std::vector<VuePlan> vues;
  std::vector<NotePlan> notes;
  friend bool operator==(const FeuillePlan&, const FeuillePlan&) = default;
};
struct DossierPlans {
  std::vector<FeuillePlan> feuilles;
  friend bool operator==(const DossierPlans&, const DossierPlans&) = default;
};
nlohmann::ordered_json dossierPlansVersJson(const DossierPlans& d);
DossierPlans dossierPlansDepuisJson(const nlohmann::ordered_json& o);
// Le dossier de la carte, ou celui proposé d'office (couches du dessus et du dessous, vue de côté, perçages, empilage,
// notes types)
DossierPlans dossierPlans(const ConceptionElec& c);
DossierPlans dossierPlansParDefaut(const ConceptionElec& c);
void reglerDossierPlans(ConceptionElec& c, const DossierPlans& d);

// Catalogue de notes types ; « ${EPAISSEUR} », « ${FINITION}», « ${CUIVRE} », « ${VERNIS} », « ${COUCHES} » sont
// remplacés au rendu
const std::vector<std::string>& catalogueNotes();
// Notes renumérotées de 1 à n dans l'ordre ; rend le nombre
int renumeroterNotes(FeuillePlan& f);
// Texte remplacé dans toutes les notes du dossier ; rend le nombre de notes changées
int remplacerDansNotes(DossierPlans& d, const std::string& ancien, const std::string& nouveau);
std::string texteNote(const ConceptionElec& c, const std::string& modele);

// Contenu d'un tableau (perçages, lignes, nomenclature), calculé sur la carte à cet instant
struct ContenuTableau {
  std::vector<std::string> entetes;
  std::vector<std::vector<std::string>> lignes;
  int total = 0;  // perçages : nombre de trous ; nomenclature : composants
};
ContenuTableau contenuTableau(const ConceptionElec& c, VuePlan::Genre genre);
// Feuille rendue en SVG (mm), cadre et cartouche compris
std::string svgFeuillePlan(const ConceptionElec& c, const FeuillePlan& f);
// Un tableau posé sur une couche de dessin du circuit (traits et textes) ; rend le nombre d'objets ajoutés
int poserTableauSurCouche(ConceptionElec& c, VuePlan::Genre genre, Pt origine, Couche couche = Couche::Dessins);
// Empilage en classeur XLSX
std::string xlsxEmpilage(const ConceptionElec& c);

}  // namespace prom::ecad

// Prométhée : résolution des contraintes du circuit (5.12, 5.27, 5.29, 8.24). Chaque objet expose ses propriétés au
// langage d'expressions ; une seule fonction dit, pour un genre de contrainte, la valeur qui s'applique, la règle qui
// l'emporte et pourquoi. Tables d'isolement sourcées (IPC-2221 table 6-1, CEI 60664-1 table F.4), classes de
// composants par règles, règles enregistrées en fichier. Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/electronique.hpp"
#include "promethee/expressions.hpp"

namespace prom::ecad {

// Propriétés d'un objet du circuit pour les expressions : genre, reseau, classe, couche, region (zones de règles
// qui le contiennent), largeur, diametre, percage, longueur ; pour un composant : reference, prefixe, valeur,
// empreinte, boitier, face, hauteur, classes (de composants) ; « interne » vaut 1 sur un cuivre interne
Proprietes proprietesPiste(const ConceptionElec& c, const Piste& p);
Proprietes proprietesVia(const ConceptionElec& c, const Via& v);
Proprietes proprietesComposant(const ConceptionElec& c, const ComposantElec& x);
Proprietes proprietesReseau(const ConceptionElec& c, const std::string& reseau);
// Zones de règles qui contiennent un point (noms), sur une couche (0 : toutes)
std::vector<std::string> regionsEn(const ConceptionElec& c, Pt p, Couches couches = 0);

// Classes de composants (5.27) : membres nommés et règle en expression ; un composant peut être dans plusieurs
std::vector<std::string> classesDuComposant(const ConceptionElec& c, const ComposantElec& x);
std::vector<const ComposantElec*> membresClasse(const ConceptionElec& c, const ClasseComposants& k);

// Résolution (8.24, 5.29) : contraintes actives du genre dont la portée prend l'objet (et la portée B l'autre objet,
// dans un sens ou dans l'autre) ; la plus haute priorité l'emporte, puis la plus stricte. Les classes de réseaux et la
// règle générale entrent comme contraintes implicites de priorité 0.
struct Resolution {
  bool trouvee = false;
  ValeurContrainte valeur;
  std::string regle;          // nom de la contrainte qui l'emporte (« règle générale », « classe Secteur »…)
  std::string justification;  // pourquoi, en clair, avec la règle écartée
  std::vector<std::string> ecartees;
};
Resolution resoudre(const ConceptionElec& c, GenreContrainte genre, const Proprietes& a, const Proprietes* b = nullptr, bool interne = false);
// Isolement exigé entre deux objets de cuivre : règles, classes, tensions de service (table de la carte) et
// contraintes ; la justification dit d'où vient la valeur
Resolution isolementRequis(const ConceptionElec& c, const Proprietes& a, const Proprietes& b, bool interne);

// Tables d'isolement. IPC-2221B table 6-1 : catégories B1 (interne), B2 (externe nu, < 3 050 m), B3 (externe nu,
// > 3 050 m), B4 (externe vernis permanent), A5, A6, A7 ; au-delà de 500 V, la valeur par volt s'ajoute à celle de
// 500 V. CEI 60664-1 table F.4 (lignes de fuite, tension efficace) : degré de pollution 1 à 3, groupe de matériau
// I, II, III ; « circuitImprime » : colonnes des matériaux de circuit imprimé (degrés 1 et 2)
double isolementIPC2221(double tension, const std::string& categorie);  // mm
double ligneFuiteCEI60664(double tensionEfficace, int pollution, int groupe, bool circuitImprime);  // mm
std::string sourceIPC2221();
std::string sourceCEI60664();

// Règles enregistrées en fichier (5.27) : classes, affectations et contraintes avancées, en JSON lisible ; chargées
// par ajout (les homonymes remplacés) ou par remplacement complet
std::string exporterRegles(const Regles& r);
bool importerRegles(Regles& r, const std::string& texte, bool remplacer, std::string* erreur = nullptr);
nlohmann::ordered_json contraintesVersJson(const ContraintesCarte& a);
ContraintesCarte contraintesDepuisJson(const nlohmann::ordered_json& o);

// Contrôle des contraintes contre le profil du fabricant (5.12) : chaque valeur sous le minimum du profil, avec la
// raison (pour la cellule rouge du gestionnaire)
struct EcartProfil {
  std::string ou, raison;
};
std::vector<EcartProfil> ecartsAuProfil(const Regles& r);

}  // namespace prom::ecad

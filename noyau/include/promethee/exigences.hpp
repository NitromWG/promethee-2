// Prométhée : exigences et ordres de modification du projet (lot L19 ; 19.6, 19.8, 19.9). Une exigence se relie aux
// objets qui la satisfont et se vérifie ; chiffrée, elle compare une grandeur du projet (coût, nombre de composants,
// surface, consommation…) à son seuil à chaque vérification. Un ordre de modification relie motif, objets touchés,
// révisions et approbations. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace prom {

struct Exigence {
  std::string id, texte, source;
  std::string methode = "revue";  // « analyse », « essai », « inspection », « revue »
  std::vector<std::string> liens; // UUID d'objets, noms de règles, repères de points de test
  // Chiffrée (19.9) : grandeur du projet (« cout », « composants », « surface », « consommation », « masse »),
  // comparateur et seuil avec son unité (« 40 € », « 3 W ») ; le coût s'entend pour « quantite » cartes
  std::string grandeur, comparateur = "<=", seuil;
  int quantite = 1;
  friend bool operator==(const Exigence&, const Exigence&) = default;
};

struct Approbation {
  std::string personne, role, date, decision, commentaire;  // décision : « approuvé » ou « refusé »
  friend bool operator==(const Approbation&, const Approbation&) = default;
};

struct OrdreModification {
  std::string id, titre, motif, revisionDepart, revisionArrivee, dateEffet;
  std::string sortStock = "utiliser";  // « utiliser », « retravailler », « rebuter »
  std::string etat = "ouvert";         // « ouvert », « approuvé », « refusé », « appliqué »
  std::vector<std::string> objets;     // repères ou UUID touchés
  std::vector<Approbation> approbations;
  friend bool operator==(const OrdreModification&, const OrdreModification&) = default;
};

nlohmann::ordered_json exigenceVersJson(const Exigence& e);
Exigence exigenceDepuisJson(const nlohmann::ordered_json& o);
nlohmann::ordered_json ordreVersJson(const OrdreModification& o);
OrdreModification ordreDepuisJson(const nlohmann::ordered_json& o);

}  // namespace prom

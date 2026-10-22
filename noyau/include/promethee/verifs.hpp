// Prométhée : vérifications continues entre la carte et le boîtier. Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "promethee/derive.hpp"
#include "promethee/modele.hpp"

namespace prom {

enum class Severite { Erreur, Alerte };
const char* nomSeverite(Severite s);  // "erreur" ou "alerte"

struct Correction {
  std::string libelle;
  std::string type;     // "hauteur", "ecart", "entretoise", "trou"
  std::string id;       // élément visé, le cas échéant
  double valeur = 0;
};

struct Probleme {
  Severite sev = Severite::Erreur;
  std::string code, msg;
  std::vector<std::string> ids;
  std::optional<Correction> fix;
};

std::vector<Probleme> verifier(const Projet& p, const Derive& d);

// Contrôle du boîtier : il n'est complet que si chaque composant a une hauteur vérifiée (fiche technique,
// mesure, modèle exact). Une hauteur typique, estimée d'après un nom d'empreinte ou tirée d'un corps générique
// laisse le contrôle « incomplet » : une collision avec le couvercle peut lui échapper. Aucun état « tout est
// cohérent » tant qu'il est incomplet.
struct ControleBoitier {
  bool complet = true;
  std::vector<std::string> nonVerifies;  // références des composants dont la hauteur n'est pas vérifiée
  std::string resume() const;            // une phrase, vide si complet
};
ControleBoitier controleBoitier(const Projet& p);
void corriger(Projet& p, const Correction& fix);
void repousserTrou(Projet& p, const std::string& id);
// Applique les corrections disponibles, la première à chaque tour, jusqu'à épuisement.
std::vector<std::string> corrigerTout(Projet& p, int toursMax = 12);

}  // namespace prom

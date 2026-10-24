// Prométhée : langage d'expressions unique sur les propriétés des objets (13.11, AUT-16), compilé une fois en arbre
// et évalué sur chaque objet : requêtes de sélection, portées des contraintes (8.24), classes de composants par
// règles (5.27), règles écrites de la DRC. Syntaxe :
//   reseau == 'GND' && couche ~ '*.Cu'        égalité, motif (« * », « ? »), et, ou, non (!), parenthèses
//   largeur < 0.2mm || classe != 'Puissance'  nombres avec unité (mm, mil, um, nm, V, A, ps, deg, %), comparaisons
//   dans(reseau, 'SDA', 'SCL')                appartenance ; commence(reference, 'C') ; contient(valeur, 'n')
//   proprietes : reseau, classe, couche, genre (piste, via, pastille, zone, empreinte…), reference, prefixe, valeur,
//   empreinte, boitier, region, largeur, diametre, percage, longueur, hauteur, face… selon l'objet
// Les noms d'identifiants et les chaînes se comparent sans accents ni casse pour les mots-clés, exactement pour les
// valeurs. Une expression vide vaut « vrai ». Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace prom::ecad {

// Valeur d'une propriété : texte ou nombre (longueurs en nanomètres, angles en degrés, autres grandeurs en unités SI)
using ValeurExpression = std::variant<std::string, double>;
// Résolution d'un identifiant sur l'objet évalué ; vide : propriété absente (une comparaison est alors fausse)
using Proprietes = std::function<std::optional<ValeurExpression>(const std::string& nom)>;

class Expression {
 public:
  Expression();
  ~Expression();
  Expression(const Expression&);
  Expression& operator=(const Expression&);
  Expression(Expression&&) noexcept;
  Expression& operator=(Expression&&) noexcept;

  // Compile le texte ; une erreur de syntaxe laisse l'expression invalide, avec la raison et sa position
  static Expression compiler(const std::string& texte);
  bool valide() const { return m_erreur.empty(); }
  const std::string& erreur() const { return m_erreur; }
  const std::string& texte() const { return m_texte; }
  // Identifiants que l'expression lit (pour l'éditeur et les index)
  std::vector<std::string> identifiants() const;
  bool evaluer(const Proprietes& p) const;

  struct Noeud;

 private:
  std::string m_texte, m_erreur;
  std::shared_ptr<const Noeud> m_racine;
};

// Lecture d'un nombre avec unité (« 0,2mm », « 6 mil », « 230V », « 45deg ») vers l'unité de base ; faux sinon
bool lireNombreUnite(const std::string& texte, double& valeur);

}  // namespace prom::ecad

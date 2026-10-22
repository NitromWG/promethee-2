// Prométhée : ce qui change dans une conception électronique, et les commandes qui la changent (lot L1 de l'audit du
// 15 octobre 2026, A2, A12, SAI-24). Une modification est décrite par un ChangementConception : quels objets, de
// quelle nature (ajout, retrait, propriété, géométrie, connectivité), quelles propriétés ; les abonnés (vues,
// inspecteur, arbre, moteurs de calcul) ne refont que ce qui les concerne. Une Commande sait s'appliquer, se
// défaire et se nommer (« Valeur de R3 : 10k → 4k7 ») ; les frappes d'un même champ se fusionnent en un pas.
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct ChangementConception {
  enum Nature : std::uint8_t { Ajout = 1, Retrait = 2, Propriete = 4, Geometrie = 8, Connectivite = 16 };
  std::string conception;                      // UUID de la conception
  std::map<std::string, std::uint8_t> objets;  // UUID de l'objet (composant, symbole posé, piste…) → natures
  std::set<std::string> proprietes;            // propriétés touchées : « valeur », « reference », « empreinte »…
  bool schema = false;        // dessin du schéma : symboles, fils, étiquettes, textes, feuilles
  bool connectivite = false;  // les réseaux du schéma peuvent avoir changé
  bool circuit = false;       // placement ou cuivre du circuit
  bool zones = false;         // zones de cuivre
  bool regles = false;        // règles, classes, empilage, contour de la carte
  bool definitions = false;   // définitions de symboles ou d'empreintes gardées dans le projet
  bool entiere = false;       // tout peut avoir changé (conception remplacée, migrée, rechargée)

  bool vide() const;
  void ajouter(const std::string& uuid, std::uint8_t natures);
  void fusionner(const ChangementConception& o);
  bool touche(const std::string& uuid) const { return entiere || objets.count(uuid) > 0; }
  // Moteurs à refaire : la connectivité du schéma (et l'ERC), puis le cuivre (zones, routage, DRC). Changer une
  // valeur, un fabricant, une hauteur ou le cartouche ne demande ni l'une ni l'autre.
  bool demandeConnectivite() const { return entiere || connectivite || definitions; }
  bool demandeCuivre() const { return entiere || connectivite || circuit || zones || regles || definitions; }
  // Un changement que la vérification électrique peut voir sans que les réseaux changent (brochage vérifié, empreinte)
  bool demandeErc() const;
};

// Ce qui sépare deux états d'une même conception, objet par objet (par UUID)
ChangementConception differencier(const ConceptionElec& avant, const ConceptionElec& apres);

class Commande {
 public:
  virtual ~Commande() = default;
  virtual std::string nom() const = 0;  // « Valeur de R3 : 10k → 4k7 »
  virtual void appliquer(ConceptionElec& c) = 0;
  virtual void defaire(ConceptionElec& c) = 0;
  // Réunit la commande suivante à celle-ci (frappes d'un même champ pendant une même saisie) : vrai si c'est fait
  virtual bool fusionner(const Commande&) { return false; }
  // Sans effet (une saisie revenue à sa valeur de départ) : le pas d'annulation peut disparaître
  virtual bool neutre() const { return false; }
  virtual ChangementConception changement() const = 0;
  virtual std::size_t octets() const { return 256; }  // mémoire tenue, pour borner l'historique
};

// Propriété texte d'un composant (repère, valeur, fabricant, référence fabricant, fournisseur, description, fiche).
// « saisie » identifie un passage dans le champ : les frappes d'un même passage ne font qu'un pas (P2, SAI-24).
class CommandeProprieteComposant : public Commande {
 public:
  CommandeProprieteComposant(std::string composant, std::string propriete, std::string avant, std::string apres, std::string repere, std::uint64_t saisie = 0);
  std::string nom() const override;
  void appliquer(ConceptionElec& c) override;
  void defaire(ConceptionElec& c) override;
  bool fusionner(const Commande& suivante) override;
  bool neutre() const override { return m_avant == m_apres; }
  ChangementConception changement() const override;
  std::size_t octets() const override { return 128 + m_avant.size() + m_apres.size(); }
  // Propriétés que cette commande sait écrire
  static bool connue(const std::string& propriete);
  const std::string& apres() const { return m_apres; }

 private:
  void ecrire(ConceptionElec& c, const std::string& v) const;
  std::string m_composant, m_propriete, m_avant, m_apres, m_repere;
  std::uint64_t m_saisie;
};

// Instantané de la conception avant et après une modification quelconque : la méthode des opérations rares ou
// massives (import, annotation, mise à jour du circuit) ; le changement est calculé en comparant les deux états
class CommandeInstantane : public Commande {
 public:
  CommandeInstantane(std::string nom, const ConceptionElec& avant, const ConceptionElec& apres);
  std::string nom() const override { return m_nom; }
  void appliquer(ConceptionElec& c) override;
  void defaire(ConceptionElec& c) override;
  ChangementConception changement() const override { return m_changement; }
  bool neutre() const override { return m_avant == m_apres; }
  std::size_t octets() const override { return m_avant.size() + m_apres.size() + 256; }
  // Pour fusionner deux instantanés successifs (appuis répétés sur les flèches, C18) : garde l'état de départ
  void prolonger(const CommandeInstantane& suivante);

 private:
  std::string m_nom, m_avant, m_apres;  // conceptions au format du fichier
  ChangementConception m_changement;
};

// Nom lisible d'une modification, d'après ce qu'elle a changé (« Déplacement de R1 », « 3 objets modifiés »)
std::string nommerChangement(const ConceptionElec& apres, const ChangementConception& ch);

}  // namespace prom::ecad

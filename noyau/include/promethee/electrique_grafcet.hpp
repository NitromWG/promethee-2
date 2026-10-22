// Prométhée : Grafcet (NF EN CEI 60848). Étapes (initiales), transitions et leurs réceptivités, actions continues,
// conditionnelles et mémorisées ; divergences et convergences en OU et en ET par le graphe lui-même (une transition
// a ses étapes amont et aval). Réceptivités lues comme on les écrit en France (« dcy·a0 », « /b », « ↑S1 »,
// « 5s/X3 »), vérification de la syntaxe et des règles, simulation selon les cinq règles d'évolution (évolutions
// fugaces comprises), disposition du dessin normalisé, programme en texte structuré CEI 61131-3. Aucune
// dépendance à l'interface. Licence GPL-3.0-only.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "promethee/electrique_distribution.hpp"  // AvisCalcul

namespace prom::elec {

// ---------- Modèle (ActionGrafcet, EtapeGrafcet, TransitionGrafcet, Grafcet : electrique.hpp) ----------
// Une action par ligne, comme on l'écrit : « KM1 », « KM1 si a », « KM1 := 1 » (ou « S KM1 »), « KM1 := 0 » (ou
// « R KM1 »), « ↓ KM1 := 0 » (à la désactivation). Rend faux si la ligne ne se lit pas (erreur expliquée).
bool lireAction(const std::string& texte, ActionGrafcet& action, std::string* erreur = nullptr);
std::string texteAction(const ActionGrafcet& a);

// Exemples de cours : chariot en aller-retour, tri de pièces (divergence en OU, temporisation), poste à deux
// opérations simultanées (divergence en ET), feux tricolores (temporisations)
std::vector<std::pair<std::string, std::string>> exemplesGrafcet();  // identifiant, nom
Grafcet exempleGrafcet(const std::string& id);

// ---------- Réceptivités ----------
// Grammaire : OU « + » (« | », « ou »), ET « · » (« . », « * », « & », « et »), NON préfixe « / » (« ! », « ¬ »,
// « non »), front montant « ↑a » (« ^a »), descendant « ↓a », constantes « 1 », « =1 », « 0 », variable d'étape
// « X3 », temporisation « 5s/X3 » (« 500ms/X3 », « 2min/X3 », ancienne écriture « t/X3/5s »), parenthèses.
struct Receptivite {
  enum class Type : std::uint8_t { Vrai, Faux, Variable, Etape, FrontMontant, FrontDescendant, Tempo, Non, Et, Ou };
  Type type = Type::Vrai;
  std::string nom;   // variable (front : sa variable, ou « X3 »)
  int etape = -1;    // variable d'étape, temporisation
  double duree = 0;  // s
  std::vector<Receptivite> enfants;
};
struct LectureReceptivite {
  std::optional<Receptivite> expression;
  std::string erreur;
  std::size_t position = 0;  // en caractères, là où la lecture s'est arrêtée
};
LectureReceptivite lireReceptivite(const std::string& texte);
std::string texteReceptivite(const Receptivite& r);  // forme normalisée (« dcy · a0 », « /b », « 5s/X3 »)
// Variables d'entrée lues (sans les variables d'étape), dans l'ordre d'apparition
void variablesReceptivite(const Receptivite& r, std::vector<std::string>& v);
std::vector<std::string> entreesGrafcet(const Grafcet& g);  // réceptivités et conditions d'actions
std::vector<std::string> sortiesGrafcet(const Grafcet& g);

// ---------- Vérification ----------
std::vector<AvisCalcul> verifierGrafcet(const Grafcet& g);

// ---------- Simulation ----------
// Les cinq règles : situation initiale ; transition validée quand toutes ses étapes amont sont actives, franchie si
// sa réceptivité est vraie ; franchir désactive l'amont et active l'aval ; tout ce qui est franchissable l'est en
// même temps ; une étape à la fois activée et désactivée reste active. On franchit jusqu'à la situation stable : les
// actions continues ne valent que dans une situation stable, les actions mémorisées s'exécutent même en passant.
class SimulateurGrafcet {
 public:
  explicit SimulateurGrafcet(Grafcet g);
  void initialiser();
  void fixer(const std::string& entree, bool valeur);
  bool entree(const std::string& nom) const;
  void avancer(double secondes);  // le temps passe (temporisations), puis évolution
  int evoluer();                  // rend le nombre de franchissements jusqu'à la stabilité
  const std::set<int>& actives() const { return m_actives; }
  bool active(int etape) const { return m_actives.count(etape) > 0; }
  std::vector<std::size_t> franchissables() const;  // transitions franchissables dans la situation actuelle
  std::vector<std::size_t> validees() const;        // transitions dont toutes les étapes amont sont actives
  std::map<std::string, bool> sorties() const;      // continues (situation stable) et mémorisées
  double temps() const { return m_temps; }
  double dureeActive(int etape) const;  // depuis son activation (0 si inactive)
  bool instable() const { return m_instable; }  // évolution fugace sans fin (cycle de réceptivités toujours vraies)
  const std::vector<std::string>& journal() const { return m_journal; }  // derniers franchissements, en clair
  const Grafcet& grafcet() const { return m_g; }

 private:
  bool vraie(const Receptivite& r, bool fronts) const;
  Grafcet m_g;
  std::vector<std::optional<Receptivite>> m_receptivites;
  std::map<int, std::vector<std::optional<Receptivite>>> m_conditions;  // par étape, une par action
  std::set<int> m_actives, m_activesAvant;
  std::map<std::string, bool> m_entrees, m_entreesAvant, m_memoires;
  std::map<int, double> m_activation;  // instant d'activation de chaque étape active
  double m_temps = 0;
  bool m_instable = false;
  std::vector<std::string> m_journal;
};

// ---------- Dessin ----------
// Disposition normalisée en millimètres (origine en haut à gauche, y vers le bas) : carré de 10 mm par étape (double
// pour une initiale), actions en rectangles à droite, transitions en traits épais avec leur réceptivité, liaisons
// verticales, traits simples des OU et doubles des ET, retours vers le haut par la gauche avec leur flèche.
struct TraitGrafcet {
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool epais = false;   // barre de transition
  bool fleche = false;  // flèche au milieu, dans le sens du trait (remontée)
  int transition = -1;  // barre : indice de sa transition
};
struct BoiteGrafcet {
  double x = 0, y = 0, l = 0, h = 0;
  bool double_ = false;  // étape initiale
  int etape = -1;        // carré d'une étape ; −1 : action
};
struct TexteGrafcet {
  double x = 0, y = 0, taille = 2.5;  // y : milieu de la ligne
  std::string texte;
  int alignement = -1;  // −1 gauche, 0 centré, 1 droite
  bool discret = false; // numéro de transition, commentaire
};
struct ZoneGrafcet {
  int etape = -1;
  int transition = -1;
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
struct DispositionGrafcet {
  std::vector<TraitGrafcet> traits;
  std::vector<BoiteGrafcet> boites;
  std::vector<TexteGrafcet> textes;
  std::vector<ZoneGrafcet> zones;  // pour désigner une étape ou une transition à la souris
  double largeur = 0, hauteur = 0;
};
DispositionGrafcet disposerGrafcet(const Grafcet& g);

// ---------- Programme ----------
// Texte structuré (CEI 61131-3) : un bit par étape, équations d'activation et de désactivation, fronts (R_TRIG,
// F_TRIG), temporisations (TON), sorties ; prêt à coller dans CODESYS, TIA Portal (SCL), Unity/Control Expert.
std::string texteStructureGrafcet(const Grafcet& g);

}  // namespace prom::elec

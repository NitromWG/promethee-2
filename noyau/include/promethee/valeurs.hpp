// Prométhée : valeurs des composants électroniques (lot L3, tâches 3.2 et 3.3 ; P3, P10, P18, P26, ERG-12). Le texte
// tapé reste ce qu'on voit ; à côté, sa lecture : un nombre dans l'unité de base de la famille (ohm, farad, henry…),
// les compléments reconnus (tension, tolérance, diélectrique, puissance, courant), la série E où tombe la valeur, et
// un état : comprise, texte libre (famille sans unité) ou refusée, avec sa raison. Grammaire : notation RKM de la
// CEI 60062 (4k7, 2u2, R47, 0R1, 15M0), nombres à préfixe et unité (4,7 kΩ, 100 nF), lettres de tolérance, codes
// EIA proposés pour un nombre nu ambigu. Séries E de la CEI 60063 en tables (E3 à E192, avec les huit valeurs de E24
// qui s'écartent du calcul et 9,20 dans E192). Paramètres typés par famille, repris des champs libres connus.
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace prom::ecad {

struct ConceptionElec;
struct ComposantElec;

enum class FamilleValeur : std::uint8_t { Aucune, Resistance, Condensateur, Inductance, Ferrite, Fusible, Quartz, Zener, Pile };
const char* nomFamille(FamilleValeur f);           // « résistance »
const char* uniteDeFamille(FamilleValeur f);       // « Ω », « F », « H », « Ω », « A », « Hz », « V », « V » ; « » sans unité
FamilleValeur familleDepuisPrefixe(const std::string& prefixe, const std::string& symbole = {});
FamilleValeur familleDe(const ConceptionElec& c, const ComposantElec& k);
std::string phraseUnite(FamilleValeur f);          // « une résistance se mesure en ohms »

enum class EtatValeur : std::uint8_t { Vide, Comprise, TexteLibre, Refusee };

struct ValeurComprise {
  EtatValeur etat = EtatValeur::Vide;
  FamilleValeur famille = FamilleValeur::Aucune;
  double nombre = 0;  // unité de base de la famille
  // Compléments reconnus
  std::optional<double> tension, tolerance, puissance, courant;  // V, %, W, A
  std::optional<double> toleranceAbsolue;                        // en unité de base (condensateurs sous 10 pF)
  std::string toleranceAsymetrique;                              // Q, S, T, Z : « −10 % +30 % »
  std::string dielectrique;                                      // C0G, X7R…
  bool zeroFinal = false;   // 15M0 : tolérance serrée (CEI 60062)
  int serieReference = 0;   // série de la tolérance, ou celle de la famille (E24, E12) ; 0 sans série
  int serie = 0;            // série où tombe la valeur (serieReference), 0 hors série
  double normalisee = 0;    // valeur normalisée la plus proche dans serieReference
  std::string raison;       // refus, ou remarque (complément non compris)
  std::string note;         // « = 4,7 µH » pour un R sur une inductance, zéro final…
  std::vector<std::string> propositions;  // nombre nu ambigu : « 10 pF (code 100) », « 100 pF »…
  bool comprise() const { return etat == EtatValeur::Comprise; }
};

ValeurComprise lireValeurComposant(const std::string& texte, FamilleValeur famille);

enum class AffichageValeur : std::uint8_t { CommeTapee, Compact, SystemeInternational };
// Le nombre seul, en CEI 60062 compacte (4k7, 2u2, R47, 470R) ou en SI (4,7 kΩ, avec une espace fine insécable)
std::string ecrireCompact(double base, FamilleValeur f);
std::string ecrireSI(double base, const std::string& unite, int chiffres = 3);
// La valeur entière, compléments compris, dans le mode voulu (le texte tapé pour « comme tapée », ou s'il n'est pas
// compris)
std::string ecrireValeurComposant(const ValeurComprise& v, AffichageValeur mode, const std::string& tape);
// Sous le champ Valeur de l'inspecteur : « = 4,7 kΩ · E24 », « = 4,8 kΩ · hors E24 (4,7 kΩ) », ou la raison du refus
std::string resumeValeur(const ValeurComprise& v);

// Séries E (CEI 60063:2015) : les valeurs d'une décade, de 1 inclus à 10 exclu ; n = 3, 6, 12, 24, 48, 96 ou 192
const std::vector<double>& serieE(int n);
int serieDeTolerance(double pourcent);       // 0,5 % et mieux : E192 ; 1 % : E96 ; 2 % : E48 ; 5 % : E24 ; 10 % : E12 ; 20 % : E6 ; au-delà E3
int serieDeFamille(FamilleValeur f);         // sans tolérance : E24 pour une résistance ou une Zener, E12 pour C et L
bool dansSerie(double valeur, int n);        // à 0,05 % près
double plusProcheDansSerie(double valeur, int n);  // distance logarithmique, la plus basse en cas d'égalité
double voisineDansSerie(double valeur, int n, int pas);  // suivante (+1) ou précédente (−1), d'une décade à l'autre
std::optional<double> toleranceDeLettre(char lettre);    // CEI 60062 : J → 5 %

// Clé de regroupement de la nomenclature : « 4k7 », « 4,7 kΩ » et « 4.7k » donnent la même ; un texte libre se
// compare sans casse ni espaces
std::string cleValeur(const ValeurComprise& v, const std::string& texte);

// Paramètres typés par famille (3.3) : tolérance, puissance, tension, diélectrique… Valeur gardée en texte, lue
// selon son genre
struct DefParametre {
  enum class Genre : std::uint8_t { Nombre, Choix, Texte, Booleen };
  std::string cle, libelle, unite;  // « tolerance », « Tolérance », « % »
  Genre genre = Genre::Nombre;
  std::vector<std::string> choix{};
  std::vector<std::string> synonymes{};  // noms de champs libres repris par la migration (« Tolerance », « Tol »)
};
const std::vector<DefParametre>& parametresDeFamille(FamilleValeur f);
const DefParametre* parametre(FamilleValeur f, const std::string& cle);
// Lecture d'un paramètre : nombre dans son unité (« 1 % » → 1, « 0,25 W » ou « 1/4 W » → 0,25, « 50 V » → 50)
std::optional<double> lireParametre(const DefParametre& p, const std::string& texte, std::string* erreur = nullptr);
// Champs libres connus repris en paramètres typés, texte gardé tel quel ; rend le nombre de champs repris
int migrerChampsEnParametres(ConceptionElec& c);

}  // namespace prom::ecad

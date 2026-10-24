// Prométhée : moteur de lignes de transmission (lot L8 de la 0.34 ; 8.21 ; CAR-05, SIM-08, ROU-08, CAR-36). Formules
// fermées : microruban de Hammerstad et Jensen avec la dispersion de Kirschning et Jansen, ruban (stripline) de
// Wadell, décentré entre ses deux plans, paire couplée en microruban de Kirschning et Jansen (modes pair et impair,
// dispersion comprise), paire couplée en ruban de Cohn ; pertes par la rugosité de Huray (boules de neige) ou de
// Hammerstad, diélectrique large bande de Djordjevic et Sarkar. Analyse et synthèse ; profils d'impédance de la carte
// (largeur par couche), retard par couche et retard des vias, en picosecondes. Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// Diélectrique large bande (Djordjevic et Sarkar) : εr et tan δ connus à « fRef » ; f1 et f2 bornent la bande où les
// pertes sont presque constantes (Hz)
struct Dielectrique {
  double er = 4.4, tanD = 0.02, fRef = 1e9;
  double f1 = 1e3, f2 = 1e12;
  bool largeBande = true;  // faux : εr et tan δ constants
};
double permittiviteA(const Dielectrique& d, double f);
double pertesA(const Dielectrique& d, double f);  // tan δ

// Rugosité du cuivre : Huray (rayon des boules, nombre par tuile, surface de la tuile), sinon Hammerstad (rugosité
// quadratique moyenne) ; facteur multiplicatif de la résistance de surface, ≥ 1
struct Rugosite {
  double rayon = 0, nombre = 0, surfaceTuile = 0;  // m, —, m²
  double rq = 0;                                   // m
};
double facteurRugosite(const Rugosite& r, double f, double sigma);

enum class GenreLigne : std::uint8_t { Microruban, Ruban, MicrorubanCouple, RubanCouple };
const char* nomGenreLigne(GenreLigne g);

// Géométrie (m). Microruban : « h » diélectrique jusqu'au plan, « couvercle » hauteur d'un capot (0 : aucun). Ruban :
// « h » distance entre les deux plans, « a » du plan du dessus au ruban (0 : centré). Paire : « ecart » entre bords.
struct GeometrieLigne {
  double largeur = 0, ecart = 0, h = 0, a = 0, t = 35e-6, couvercle = 0;
};
struct ResultatLigne {
  double z0 = 0;                 // Ω : ligne seule, ou mode impair d'une paire
  double zPair = 0, zImpair = 0, zDiff = 0, zCommun = 0;  // paire
  double erEff = 0, erEffPair = 0, erEffImpair = 0;
  double retard = 0;             // ps/mm (mode impair d'une paire)
  double pertesConducteur = 0, pertesDielectrique = 0;  // dB/m
  double profondeurPeau = 0;     // m
};

class LigneTransmission {
 public:
  explicit LigneTransmission(GenreLigne genre, Dielectrique d = {}, Rugosite r = {}, double sigma = 5.8e7);
  GenreLigne genre() const { return m_genre; }
  ResultatLigne analyser(const GeometrieLigne& g, double frequence) const;
  // Synthèse : la largeur qui donne « z » (Z0 d'une ligne seule, Zdiff d'une paire à écart fixé), ou l'écart qui
  // donne Zdiff à largeur fixée ; nullopt hors d'atteinte
  std::optional<double> largeurPour(double z, GeometrieLigne g, double frequence) const;
  std::optional<double> ecartPour(double zdiff, GeometrieLigne g, double frequence) const;

 private:
  GenreLigne m_genre;
  Dielectrique m_d;
  Rugosite m_r;
  double m_sigma;
};

// ---------- Sur la carte ----------
// Géométrie et diélectrique d'une couche de cuivre, tirés de l'empilage : microruban sur une face (plan : le cuivre
// voisin), ruban entre deux cuivres à l'intérieur
struct LigneSurCouche {
  GenreLigne genre = GenreLigne::Microruban;
  GeometrieLigne geometrie;
  Dielectrique dielectrique;
};
LigneSurCouche ligneSurCouche(const Empilage& e, Couche couche);
// Profil d'impédance (8.21) : pour chaque couche de cuivre de la carte, la largeur (et l'écart d'une paire, gardé
// à « ecart ») qui donne l'impédance visée ; une couche hors d'atteinte reste absente
void calculerProfil(const ConceptionElec& c, ProfilImpedance& p, Nm ecart = 0, double frequence = 1e9);
// Profil suivi par un réseau : celui qui porte le nom de sa classe ; nullptr sinon
const ProfilImpedance* profilDuReseau(const ConceptionElec& c, const std::string& reseau);
// Largeur de routage sur une couche : celle du profil d'impédance du réseau, sinon celle de sa classe (le routeur la
// reprend à chaque changement de couche)
Nm largeurSurCouche(const ConceptionElec& c, const std::string& reseau, Couche couche);
// Retards : ps/mm sur une couche pour une largeur, ps pour une via de « de » à « vers »
double retardCouche(const ConceptionElec& c, Couche couche, Nm largeur, double frequence = 1e9);
double retardVia(const ConceptionElec& c, Couche de, Couche vers);

}  // namespace prom::ecad

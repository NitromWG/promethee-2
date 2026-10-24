// Prométhée : import des documents Altium Designer (lot L10 de la 0.34 ; 10.15, 10.18 ; ECH-07, ECH-23). Projets
// (.PrjPcb) lus d'un coup : schémas binaires ou ASCII (.SchDoc), carte (.PcbDoc), reliés par les repères ; cartes et
// schémas de CircuitStudio (.CSPcbDoc, .CSSchDoc), CircuitMaker (.CMPcbDoc, .CMSchDoc) et SolidWorks PCB
// (.SWPcbDoc), qui partagent ce format ; bibliothèques de symboles (.SchLib) et d'empreintes (.PcbLib). Conteneur CFB
// (archive.hpp) et enregistrements lus d'après l'importeur Altium de KiCad (GPL), servant de référence, sans en
// reprendre le code. Licence GPL-3.0-only.
//
// Schéma : composants (une unité par partie), broches au point de connexion, fils, jonctions, étiquettes de réseau,
// ports d'alimentation, ports (reliés à tout le projet en conception à plat, aux entrées de feuille en hiérarchie),
// feuilles filles et leurs entrées, désignateur, commentaire (valeur, « =Value » résolu), empreinte retenue. Carte :
// composants (motif, place, angle, face), pastilles (formes, perçages, fentes), pistes, arcs, vias, zones de cuivre,
// contour ; réseaux tirés des pastilles. Ce qui ne passe pas est inscrit au rapport.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct RapportImportAltium {
  int feuilles = 0, composants = 0, reseaux = 0, empreintes = 0, pistes = 0, vias = 0, zones = 0;
  std::vector<std::string> fichiers;  // documents lus
  std::vector<std::string> alertes;
};
using LecteurAltium = std::function<std::optional<std::string>(const std::string&)>;

// Projet .PrjPcb (texte INI) : ses documents sont lus par « lire » (nom relatif → octets)
ConceptionElec conceptionDepuisProjetAltium(const std::string& prjpcb, const std::string& nom, const LecteurAltium& lire, RapportImportAltium* rapport = nullptr);
// Documents donnés directement : schémas (nom, octets) et carte facultative
ConceptionElec conceptionDepuisAltium(const std::vector<std::pair<std::string, std::string>>& schemas, const std::optional<std::string>& carte, const std::string& nom,
                                      RapportImportAltium* rapport = nullptr);
bool estSchemaAltium(const std::string& octets);  // binaire (CFB à « FileHeader ») ou ASCII (« |HEADER=Protel… »)
bool estCarteAltium(const std::string& octets);   // CFB à « Board6/Data »
bool estBibliothequeAltium(const std::string& octets);  // .SchLib ou .PcbLib

struct BibliothequeAltium {
  std::vector<DefSymbole> symboles;
  std::vector<DefEmpreinte> empreintes;
  std::vector<std::string> alertes;
};
// « bibliotheque » : nom donné aux identifiants (« altium:<bibliothèque>:<nom> »)
BibliothequeAltium lireBibliothequeAltium(const std::string& octets, const std::string& bibliotheque);

// ---------- Écrivains d'essai (format lu ci-dessus) ----------
// Enregistrements « |CLÉ=VALEUR|… » d'un schéma, écrits en SchDoc binaire ou ASCII, et carte minimale en PcbDoc :
// servent aux essais de l'import et aux échanges de test
using EnregistrementAltium = std::vector<std::pair<std::string, std::string>>;
std::string schDocAltium(const std::vector<EnregistrementAltium>& enregistrements, bool ascii = false);
struct PastilleAltium {
  std::string nom;
  int composant = -1, reseau = -1;  // indices dans « composants » et « reseaux » ; −1 : aucun
  double x = 0, y = 0, largeur = 1, hauteur = 1, percage = 0, angle = 0;  // mm, y vers le haut ; perçage 0 : CMS
  int forme = 2;    // 1 rond, 2 rectangle, 3 octogone
  int couche = 1;   // 1 dessus, 32 dessous, 74 toutes les couches
};
struct PisteAltium {
  int reseau = -1, couche = 1;
  double x1 = 0, y1 = 0, x2 = 0, y2 = 0, largeur = 0.25;
};
struct ViaAltium {
  int reseau = -1;
  double x = 0, y = 0, diametre = 0.6, percage = 0.3;
};
struct ComposantAltium {
  std::string designateur, motif, commentaire;
  double x = 0, y = 0, angle = 0;
  bool dessous = false;
};
struct CarteAltium {
  std::vector<std::pair<double, double>> contour;  // mm
  std::vector<ComposantAltium> composants;
  std::vector<std::string> reseaux;
  std::vector<PastilleAltium> pastilles;
  std::vector<PisteAltium> pistes;
  std::vector<ViaAltium> vias;
};
std::string pcbDocAltium(const CarteAltium& carte);

}  // namespace prom::ecad

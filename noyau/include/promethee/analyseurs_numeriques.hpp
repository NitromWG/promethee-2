// Prométhée : instruments numériques (lot L14 de la 0.34 ; 14.6 ; SIM-45, SIM-46). Une voie logique est une suite de
// fronts (temps, niveau), états indéfini et haute impédance compris, tirée d'une tension (seuils avec hystérésis) ou
// des événements numériques de XSPICE ; l'analyseur logique capture 24 à 40 voies autour d'un déclenchement (front,
// valeur du bus, octet décodé) ; les analyseurs I2C, SPI et CAN décodent bit à bit (accusés, bourrage, CRC-15), un
// fichier DBC nomme et met à l'échelle les signaux du CAN, des compteurs relèvent les erreurs et un défaut injecté
// (bit inversé) se retrouve. Des motifs maître (I2C, CAN) se jouent par des sources pour tenir le rôle du maître.
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "promethee/logique.hpp"
#include "promethee/simulation_continue.hpp"

namespace prom::ecad {

// Niveaux de la simulation logique (logique.hpp) : bas, haut, X indéfini, Z haute impédance
struct Front {
  double t = 0;
  Niveau niveau = Niveau::Bas;
};
using VoieLogique = std::vector<Front>;  // un front par changement, le premier au début du relevé
// Seuils bas et haut : entre les deux, le niveau précédent reste (hystérésis)
VoieLogique numeriser(const Releve& s, double seuilBas, double seuilHaut);
// Événements numériques (« 0 », « 1 », « U » indéfini, « Z » haute impédance), dans l'ordre des temps
VoieLogique voieDepuisEvenements(const std::vector<std::pair<double, std::string>>& e);
Niveau niveauA(const VoieLogique& v, double t);

// ---------- Analyseur logique ----------
struct DeclenchementLogique {
  enum class Genre : std::uint8_t { Front, Valeur } genre = Genre::Front;
  int voie = 0;
  bool montant = true;
  std::uint64_t valeur = 0, masque = ~std::uint64_t{0};  // valeur du bus (bit i : voie i)
};
struct CaptureLogique {
  std::vector<std::string> noms;
  std::vector<VoieLogique> voies;
  double debut = 0, fin = 0, instant = 0;
  bool declenche = false;
};
// Capture de « avant » à « apres » autour du premier déclenchement (au plus 40 voies)
CaptureLogique capturer(const std::vector<std::pair<std::string, VoieLogique>>& voies, const DeclenchementLogique& d, double avant, double apres);
// Capture déclenchée à un instant donné (par exemple le début d'un octet décodé)
CaptureLogique capturerA(const std::vector<std::pair<std::string, VoieLogique>>& voies, double instant, double avant, double apres);
std::uint64_t valeurBus(const std::vector<VoieLogique>& voies, double t);

// ---------- I2C ----------
struct BitI2C {
  double t = 0;
  int valeur = 0;
  std::string role;  // « départ », « adresse », « lecture/écriture », « accusé », « non-accusé », « donnée », « arrêt », « redépart »
};
struct OctetI2C {
  double debut = 0, fin = 0;
  std::uint8_t valeur = 0;
  bool accuse = false, adresse = false;
};
struct TrameI2C {
  double debut = 0, fin = 0;
  int adresse = -1;
  bool lecture = false, complete = false;
  std::vector<OctetI2C> octets;  // l'octet d'adresse d'abord
  std::vector<BitI2C> bits;
  std::vector<std::string> erreurs;
  std::string texte() const;  // « É 0x50 · 0x00 ✓ · 0xA5 ✓ »
};
std::vector<TrameI2C> decoderI2C(const VoieLogique& sda, const VoieLogique& scl);
double instantOctetI2C(const std::vector<TrameI2C>& t, std::uint8_t valeur);  // début du premier octet de cette valeur (−1)
// Maître I2C joué par deux sources (PWL de SDA et SCL) : écriture vers « adresse », accusés de l'esclave compris
struct MotifPwl {
  std::vector<std::pair<double, double>> points;
  std::string texte() const;  // « PWL(0 3.3 1e-05 3.3 …) »
};
std::pair<MotifPwl, MotifPwl> motifEcritureI2C(std::uint8_t adresse, const std::vector<std::uint8_t>& donnees, double frequence = 100e3, double vHaut = 3.3,
                                               double depart = 20e-6);

// ---------- SPI ----------
struct ReglagesSPI {
  int mode = 0;  // CPOL = mode / 2, CPHA = mode % 2
  bool poidsFortDabord = true, csActifBas = true;
  int bits = 8;
};
struct TrameSPI {
  double debut = 0, fin = 0;
  std::vector<std::uint32_t> mosi, miso;
};
std::vector<TrameSPI> decoderSPI(const VoieLogique& sck, const VoieLogique& mosi, const VoieLogique& miso, const VoieLogique& cs, const ReglagesSPI& r = {});

// ---------- CAN ----------
// Bits d'une trame de données (format standard ou étendu) avec le bourrage, champ d'accusé dominant
std::vector<int> bitsTrameCAN(std::uint32_t identifiant, const std::vector<std::uint8_t>& donnees, bool etendu = false, bool accusee = true);
std::uint16_t crcCAN(const std::vector<int>& bits);
VoieLogique voieDepuisBits(const std::vector<int>& bits, double debit, double debut = 0);  // 1 récessif (haut)
struct TrameCAN {
  double debut = 0, fin = 0;
  std::uint32_t identifiant = 0;
  bool etendu = false, distante = false, acquittee = false, crcJuste = false;
  std::vector<std::uint8_t> donnees;
  std::uint16_t crc = 0, crcCalcule = 0;
  std::vector<std::string> erreurs;  // « bourrage », « CRC », « forme », « accusé »
};
std::vector<TrameCAN> decoderCAN(const VoieLogique& rx, double debit);
struct CompteursCAN {
  int trames = 0, crc = 0, bourrage = 0, forme = 0, accuse = 0;
};
CompteursCAN compterErreurs(const std::vector<TrameCAN>& t);
// DBC (Vector) : messages et signaux, Intel ou Motorola, échelle et unité
struct SignalDBC {
  std::string nom, unite;
  int debut = 0, longueur = 8;
  bool intel = true, signe = false;
  double facteur = 1, decalage = 0, min = 0, max = 0;
};
struct MessageDBC {
  std::uint32_t identifiant = 0;
  std::string nom, emetteur;
  int longueur = 8;
  std::vector<SignalDBC> signaux;
};
struct BaseDBC {
  std::vector<MessageDBC> messages;
  const MessageDBC* message(std::uint32_t identifiant) const;
};
std::optional<BaseDBC> lireDBC(const std::string& texte, std::string* erreur = nullptr);
std::map<std::string, double> signauxCAN(const BaseDBC& b, const TrameCAN& t);

}  // namespace prom::ecad

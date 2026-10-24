// Prométhée : écrivain CSV unique du noyau (lot L10 de la 0.34 ; 10.20 ; ECH-15). Tous les CSV que Prométhée écrit
// pour un tableur passent par lui, réglé une fois pour le poste : séparateur, marque décimale, guillemets, marque
// d'ordre UTF-8, fin de ligne. Par défaut : point-virgule, virgule décimale, marque UTF-8 — un double-clic l'ouvre
// en colonnes justes dans LibreOffice Calc et dans Excel en français. Les fichiers destinés à une machine ou à un
// fabricant (placement JLCPCB, nomenclatures de fabricant) gardent leur format imposé par ReglagesCsv::international.
// Licence GPL-3.0-only.
#pragma once

#include <initializer_list>
#include <string>
#include <vector>

namespace prom {

struct ReglagesCsv {
  char separateur = ';';
  char decimale = ',';
  bool marqueUtf8 = true;
  bool guillemetsToujours = false;
  std::string finLigne = "\r\n";
  static ReglagesCsv tableur() { return {}; }
  static ReglagesCsv international() { return {',', '.', false, false, "\n"}; }
  friend bool operator==(const ReglagesCsv&, const ReglagesCsv&) = default;
};
ReglagesCsv reglagesCsv();                // réglage du poste (défaut : tableur)
void reglerCsv(const ReglagesCsv& r);     // fixé une fois par l'interface (réglages) ou la ligne de commande
// Réglage lu d'un texte « ; , bom crlf » (séparateur, décimale, « bom »/« sansbom », « crlf »/« lf », « guillemets »)
ReglagesCsv reglagesCsvDepuisTexte(const std::string& t);
std::string texteReglagesCsv(const ReglagesCsv& r);

class EcrivainCsv {
 public:
  explicit EcrivainCsv(ReglagesCsv r = reglagesCsv());
  EcrivainCsv& champ(const std::string& s);
  EcrivainCsv& nombre(double v, int decimales = 3);  // marque décimale du réglage, zéros de fin retirés
  EcrivainCsv& fixe(double v, int decimales);       // décimales gardées (montants : « 12,50 »)
  EcrivainCsv& entier(long long v);
  EcrivainCsv& ligne(const std::vector<std::string>& champs);  // une ligne entière, terminée
  EcrivainCsv& ligne(std::initializer_list<std::string> champs);
  EcrivainCsv& fin();  // termine la ligne en cours
  std::string texte() const;  // avec la marque UTF-8 si réglée
  const ReglagesCsv& reglages() const { return m_r; }

 private:
  ReglagesCsv m_r;
  std::string m_texte;
  bool m_debutLigne = true;
};
// Champ protégé selon le réglage (guillemets doublés s'il contient le séparateur, un guillemet ou une fin de ligne)
std::string champCsvRegle(const std::string& s, const ReglagesCsv& r = reglagesCsv());
std::string nombreCsv(double v, int decimales = 3, const ReglagesCsv& r = reglagesCsv());

}  // namespace prom

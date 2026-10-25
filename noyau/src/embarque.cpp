// Prométhée : programmes embarqués de la conception (lot L15 de la 0.34 ; 15.3, 15.6). Voir embarque.hpp.
// Licence GPL-3.0-only.
#include "promethee/embarque.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <regex>
#include <set>
#include <sstream>

#include "promethee/fichiers.hpp"
#include "promethee/hachage.hpp"
#include "promethee/processus.hpp"

namespace fs = std::filesystem;

namespace prom::ecad {

namespace {
std::string minuscules(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string majuscules(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
std::string nettoyer(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  return s.substr(i);
}
// Texte sans accents pour comparer (« température » ≈ « temperature »)
std::string pourComparer(const std::string& s) { return minuscules(translitterer(s)); }
std::vector<std::string> lignesDe(const std::string& t) {
  std::vector<std::string> v;
  std::istringstream in(t);
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    v.push_back(l);
  }
  return v;
}
// Ligne sans commentaire « // » ni « # » (MicroPython)
std::string sansCommentaire(const std::string& l, bool python) {
  bool chaine = false;
  for (std::size_t i = 0; i < l.size(); ++i) {
    if (l[i] == '"') chaine = !chaine;
    if (chaine) continue;
    if (!python && l[i] == '/' && i + 1 < l.size() && l[i + 1] == '/') return l.substr(0, i);
    if (python && l[i] == '#') return l.substr(0, i);
  }
  return l;
}
}  // namespace

// ======================================================================================================================
// Programmes de la conception
// ======================================================================================================================
std::vector<ProgrammeEmbarque> programmesDe(const ConceptionElec& c) {
  std::vector<ProgrammeEmbarque> v;
  const auto it = c.extensions.find("programmes");
  if (it == c.extensions.end() || !it->second.is_array()) return v;
  for (const auto& j : it->second) {
    if (!j.is_object()) continue;
    ProgrammeEmbarque p;
    p.uuid = j.value("uuid", std::string());
    p.nom = j.value("nom", std::string());
    p.langage = j.value("langage", std::string("c"));
    p.chemin = j.value("chemin", std::string());
    p.sha256 = j.value("sha256", std::string());
    p.cible = j.value("cible", std::string());
    p.carte = j.value("carte", std::string("arduino:avr:uno"));
    p.frequence = j.value("frequence", 16e6);
    v.push_back(p);
  }
  return v;
}

void ecrireProgrammes(ConceptionElec& c, const std::vector<ProgrammeEmbarque>& p) {
  if (p.empty()) {
    c.extensions.erase("programmes");
    return;
  }
  nlohmann::ordered_json a = nlohmann::ordered_json::array();
  for (const auto& x : p)
    a.push_back({{"uuid", x.uuid}, {"nom", x.nom}, {"langage", x.langage}, {"chemin", x.chemin}, {"sha256", x.sha256}, {"cible", x.cible}, {"carte", x.carte}, {"frequence", x.frequence}});
  c.extensions["programmes"] = a;
}

ProgrammeEmbarque ajouterProgramme(ConceptionElec& c, ProgrammeEmbarque p, const std::string& contenu) {
  if (p.uuid.empty()) p.uuid = nouvelUuid();
  if (p.nom.empty()) p.nom = utf8(cheminFichier(p.chemin).stem());
  p.sha256 = sha256Hex(contenu);
  std::vector<ProgrammeEmbarque> v = programmesDe(c);
  bool remplace = false;
  for (auto& x : v)
    if (x.uuid == p.uuid || (!p.chemin.empty() && x.chemin == p.chemin)) {
      p.uuid = x.uuid;
      x = p;
      remplace = true;
    }
  if (!remplace) v.push_back(p);
  ecrireProgrammes(c, v);
  return p;
}

bool programmeModifie(const ProgrammeEmbarque& p, const std::string& contenuActuel) { return !p.sha256.empty() && sha256Hex(contenuActuel) != p.sha256; }

std::string langageDuFichier(const std::string& chemin) {
  const std::string e = minuscules(utf8(cheminFichier(chemin).extension()));
  if (e == ".ino" || e == ".pde") return "arduino";
  if (e == ".py") return "micropython";
  return "c";
}

// ======================================================================================================================
// Broches
// ======================================================================================================================
std::string portArduinoUno(const std::string& broche0) {
  std::string b = majuscules(nettoyer(broche0));
  if (b == "LED_BUILTIN") return "PB5";
  if (b == "SDA") return "PC4";
  if (b == "SCL") return "PC5";
  if (b == "MOSI") return "PB3";
  if (b == "MISO") return "PB4";
  if (b == "SCK") return "PB5";
  if (b == "SS") return "PB2";
  if (b.size() >= 2 && b[0] == 'A' && std::isdigit(static_cast<unsigned char>(b[1]))) {
    const int n = std::atoi(b.c_str() + 1);
    return n >= 0 && n <= 5 ? "PC" + std::to_string(n) : std::string();
  }
  if (!b.empty() && b[0] == 'D') b = b.substr(1);
  if (b.empty() || !std::all_of(b.begin(), b.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); })) return {};
  const int n = std::atoi(b.c_str());
  if (n >= 0 && n <= 7) return "PD" + std::to_string(n);
  if (n >= 8 && n <= 13) return "PB" + std::to_string(n - 8);
  if (n >= 14 && n <= 19) return "PC" + std::to_string(n - 14);
  return {};
}

std::string brocheArduinoUno(const std::string& port) {
  if (port.size() != 3 || port[0] != 'P') return {};
  const int n = port[2] - '0';
  switch (port[1]) {
    case 'D': return "D" + std::to_string(n);
    case 'B': return n <= 5 ? "D" + std::to_string(n + 8) : std::string();
    case 'C': return n <= 5 ? "A" + std::to_string(n) : std::string();
    default: return {};
  }
}

std::string numeroDuPort(const DefSymbole& s, const std::string& port) {
  for (const auto& b : s.broches) {
    const std::string n = majuscules(b.nom);
    if (n == port || n.rfind(port + "/", 0) == 0) return b.numero;
    // « …/PB5 » (fonction en premier)
    if (n.size() > port.size() && n.compare(n.size() - port.size() - 1, std::string::npos, "/" + port) == 0) return b.numero;
  }
  return {};
}

std::string portDuNumero(const DefSymbole& s, const std::string& numero) {
  for (const auto& b : s.broches)
    if (b.numero == numero) {
      const std::string n = majuscules(b.nom);
      std::istringstream in(n);
      for (std::string partie; std::getline(in, partie, '/');)
        if (partie.size() == 3 && partie[0] == 'P' && std::isalpha(static_cast<unsigned char>(partie[1])) && std::isdigit(static_cast<unsigned char>(partie[2]))) return partie;
      return n;
    }
  return {};
}

const char* nomMode(ModeBroche m) {
  switch (m) {
    case ModeBroche::Entree: return "entrée";
    case ModeBroche::EntreeTirage: return "entrée avec tirage";
    case ModeBroche::Sortie: return "sortie";
    case ModeBroche::Analogique: return "entrée analogique";
    case ModeBroche::Pwm: return "sortie MLI";
    case ModeBroche::I2C: return "I2C";
    case ModeBroche::SPI: return "SPI";
    case ModeBroche::Serie: return "liaison série";
  }
  return "";
}

// ======================================================================================================================
// Configuration lue dans le programme
// ======================================================================================================================
namespace {
// Constantes du programme : « #define LED 13 », « const int led = 13; », « int bouton = A0; », « LED = 2 » (Python)
std::map<std::string, std::string> constantes(const std::vector<std::string>& lignes, bool python) {
  std::map<std::string, std::string> m;
  static const std::regex def(R"(^\s*#\s*define\s+(\w+)\s+(\w+)\s*$)");
  static const std::regex decl(R"(^\s*(?:static\s+)?(?:const(?:expr)?\s+)?(?:unsigned\s+)?(?:int|byte|uint8_t|uint16_t|char|short|long|int8_t)\s+(\w+)\s*=\s*(\w+)\s*;)");
  static const std::regex py(R"(^\s*(\w+)\s*=\s*(\d+)\s*$)");
  for (const auto& l0 : lignes) {
    const std::string l = sansCommentaire(l0, python);
    std::smatch x;
    if (!python && (std::regex_search(l, x, def) || std::regex_search(l, x, decl))) m[x[1].str()] = x[2].str();
    else if (python && std::regex_search(l, x, py)) m[x[1].str()] = x[2].str();
  }
  return m;
}
std::string resoudre(const std::map<std::string, std::string>& k, std::string v) {
  for (int i = 0; i < 4; ++i) {
    const auto it = k.find(v);
    if (it == k.end()) break;
    v = it->second;
  }
  return v;
}
void poser(std::vector<ConfigBroche>& v, const std::string& port, const std::string& ecrit, ModeBroche m, int ligne) {
  if (port.empty()) return;
  for (auto& x : v)
    if (x.port == port) {
      x.mode = m;
      x.ecrit = ecrit;
      x.ligne = ligne;
      return;
    }
  v.push_back({port, ecrit, m, ligne});
}
// Bits écrits dans un registre : « (1 << PB5) | (1<<PB0) », « _BV(5) », « 0x20 », « 0b00100000 », « 32 »
std::vector<int> bitsDe(const std::string& expr) {
  std::vector<int> b;
  static const std::regex decal(R"((?:1\s*<<\s*|_BV\s*\(\s*)(?:P[A-H]|DD[A-H]|PORT[A-H])?(\d)\s*\)?)");
  for (auto it = std::sregex_iterator(expr.begin(), expr.end(), decal); it != std::sregex_iterator(); ++it) b.push_back(std::stoi((*it)[1].str()));
  if (!b.empty()) return b;
  static const std::regex nombre(R"(^\s*\(?\s*(0[xX][0-9a-fA-F]+|0[bB][01]+|\d+)\s*\)?\s*$)");
  std::smatch x;
  if (std::regex_search(expr, x, nombre)) {
    const std::string n = x[1].str();
    unsigned long v = 0;
    if (n.size() > 2 && (n[1] == 'b' || n[1] == 'B')) v = std::stoul(n.substr(2), nullptr, 2);
    else v = std::stoul(n, nullptr, 0);
    for (int i = 0; i < 8; ++i)
      if (v & (1ul << i)) b.push_back(i);
  }
  return b;
}
}  // namespace

std::vector<ConfigBroche> lireConfiguration(const std::string& source, const std::string& langage) {
  std::vector<ConfigBroche> v;
  const std::vector<std::string> lignes = lignesDe(source);
  const bool python = langage == "micropython";
  const auto k = constantes(lignes, python);
  if (langage == "arduino") {
    static const std::regex pm(R"(pinMode\s*\(\s*(\w+)\s*,\s*(INPUT_PULLUP|INPUT|OUTPUT)\s*\))");
    static const std::regex ar(R"(analogRead\s*\(\s*(\w+)\s*\))");
    static const std::regex aw(R"(analogWrite\s*\(\s*(\w+)\s*,)");
    for (std::size_t i = 0; i < lignes.size(); ++i) {
      const std::string l = sansCommentaire(lignes[i], false);
      const int n = static_cast<int>(i) + 1;
      for (auto it = std::sregex_iterator(l.begin(), l.end(), pm); it != std::sregex_iterator(); ++it) {
        const std::string b = (*it)[1].str(), m = (*it)[2].str();
        poser(v, portArduinoUno(resoudre(k, b)), b, m == "OUTPUT" ? ModeBroche::Sortie : m == "INPUT_PULLUP" ? ModeBroche::EntreeTirage : ModeBroche::Entree, n);
      }
      for (auto it = std::sregex_iterator(l.begin(), l.end(), ar); it != std::sregex_iterator(); ++it) {
        std::string b = resoudre(k, (*it)[1].str());
        if (std::all_of(b.begin(), b.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); }) && std::atoi(b.c_str()) <= 5) b = "A" + b;  // analogRead(0) : A0
        poser(v, portArduinoUno(b), (*it)[1].str(), ModeBroche::Analogique, n);
      }
      for (auto it = std::sregex_iterator(l.begin(), l.end(), aw); it != std::sregex_iterator(); ++it) poser(v, portArduinoUno(resoudre(k, (*it)[1].str())), (*it)[1].str(), ModeBroche::Pwm, n);
      if (l.find("Wire.begin") != std::string::npos) {
        poser(v, "PC4", "SDA", ModeBroche::I2C, n);
        poser(v, "PC5", "SCL", ModeBroche::I2C, n);
      }
      if (l.find("SPI.begin") != std::string::npos)
        for (const char* p : {"PB3", "PB4", "PB5", "PB2"}) poser(v, p, p, ModeBroche::SPI, n);
      if (l.find("Serial.begin") != std::string::npos) {
        poser(v, "PD0", "RX", ModeBroche::Serie, n);
        poser(v, "PD1", "TX", ModeBroche::Serie, n);
      }
    }
  } else if (langage == "c") {
    // DDRx |= …, DDRx = …, DDRx &= ~(…) ; PORTx |= … sur une entrée : tirage
    static const std::regex ddr(R"((DDR|PORT)([A-H])\s*(\|=|&=|=)\s*([^;]+);)");
    std::map<std::string, bool> sortie;  // port → configuré en sortie
    for (std::size_t i = 0; i < lignes.size(); ++i) {
      const std::string l = sansCommentaire(lignes[i], false);
      const int n = static_cast<int>(i) + 1;
      for (auto it = std::sregex_iterator(l.begin(), l.end(), ddr); it != std::sregex_iterator(); ++it) {
        const std::string reg = (*it)[1].str(), lettre = (*it)[2].str(), op = (*it)[3].str(), expr = (*it)[4].str();
        const bool negation = op == "&=" && expr.find('~') != std::string::npos;
        const std::vector<int> bits = bitsDe(negation ? expr.substr(expr.find('~') + 1) : expr);
        if (reg == "DDR") {
          if (op == "=") {
            // Affectation complète : les bits à 1 en sortie, les autres en entrée (seules les broches déjà vues
            // ou citées changent)
            for (int b = 0; b < 8; ++b) {
              const std::string p = "P" + lettre + std::to_string(b);
              const bool s = std::find(bits.begin(), bits.end(), b) != bits.end();
              if (s || sortie.count(p)) {
                sortie[p] = s;
                poser(v, p, "DDR" + lettre, s ? ModeBroche::Sortie : ModeBroche::Entree, n);
              }
            }
          } else
            for (int b : bits) {
              const std::string p = "P" + lettre + std::to_string(b);
              sortie[p] = !negation;
              poser(v, p, "DDR" + lettre, negation ? ModeBroche::Entree : ModeBroche::Sortie, n);
            }
        } else if (op == "|=")
          for (int b : bits) {
            const std::string p = "P" + lettre + std::to_string(b);
            if (!sortie.count(p) || !sortie[p]) poser(v, p, "PORT" + lettre, ModeBroche::EntreeTirage, n);
          }
      }
      if (std::regex_search(l, std::regex(R"(TXEN0)"))) poser(v, "PD1", "TXEN0", ModeBroche::Serie, n);
      if (std::regex_search(l, std::regex(R"(RXEN0)"))) poser(v, "PD0", "RXEN0", ModeBroche::Serie, n);
      if (l.find("TWBR") != std::string::npos || l.find("TWEN") != std::string::npos) {
        poser(v, "PC4", "TWI", ModeBroche::I2C, n);
        poser(v, "PC5", "TWI", ModeBroche::I2C, n);
      }
      if (l.find("SPE") != std::string::npos && l.find("SPCR") != std::string::npos)
        for (const char* p : {"PB3", "PB4", "PB5", "PB2"}) poser(v, p, "SPCR", ModeBroche::SPI, n);
      std::smatch x;
      if (std::regex_search(l, x, std::regex(R"(ADMUX\s*=[^;]*?(?:MUX\w*|\|)\s*(\d)\s*;)"))) poser(v, "PC" + x[1].str(), "ADMUX", ModeBroche::Analogique, n);
    }
  } else if (python) {
    static const std::regex pin(R"(Pin\s*\(\s*(\w+)\s*,\s*(?:machine\.)?Pin\.(IN|OUT)(?:\s*,\s*(?:machine\.)?Pin\.(PULL_UP|PULL_DOWN))?)");
    static const std::regex adc(R"(ADC\s*\(\s*(?:(?:machine\.)?Pin\s*\(\s*)?(\w+))");
    static const std::regex pwm(R"(PWM\s*\(\s*(?:(?:machine\.)?Pin\s*\(\s*)?(\w+))");
    static const std::regex i2c(R"(I2C\s*\([^)]*scl\s*=\s*(?:(?:machine\.)?Pin\s*\(\s*)?(\w+)\)?\s*,\s*sda\s*=\s*(?:(?:machine\.)?Pin\s*\(\s*)?(\w+))");
    for (std::size_t i = 0; i < lignes.size(); ++i) {
      const std::string l = sansCommentaire(lignes[i], true);
      const int n = static_cast<int>(i) + 1;
      std::smatch x;
      auto nom = [&](const std::string& b) { return "GP" + resoudre(k, b); };
      if (std::regex_search(l, x, pin)) poser(v, nom(x[1].str()), x[1].str(), x[2].str() == "OUT" ? ModeBroche::Sortie : x[3].matched ? ModeBroche::EntreeTirage : ModeBroche::Entree, n);
      if (std::regex_search(l, x, adc)) poser(v, nom(x[1].str()), x[1].str(), ModeBroche::Analogique, n);
      if (std::regex_search(l, x, pwm)) poser(v, nom(x[1].str()), x[1].str(), ModeBroche::Pwm, n);
      if (std::regex_search(l, x, i2c)) {
        poser(v, nom(x[1].str()), x[1].str(), ModeBroche::I2C, n);
        poser(v, nom(x[2].str()), x[2].str(), ModeBroche::I2C, n);
      }
    }
  }
  return v;
}

// ======================================================================================================================
// Écarts entre le programme et le schéma
// ======================================================================================================================
std::vector<Raccordement> raccordements(const ConceptionElec& c, const std::string& composant, const std::string& numero) {
  std::vector<Raccordement> v;
  const Connectivite k = connectivite(c);
  const int r0 = k.reseau(composant, numero);
  if (r0 < 0) return v;
  std::set<std::pair<std::string, std::string>> vus{{composant, numero}};
  std::function<void(int, bool)> parcourir = [&](int r, bool parR) {
    for (const auto& b : k.reseaux[static_cast<std::size_t>(r)].broches) {
      if (!vus.insert({b.composant, b.broche}).second) continue;
      const ComposantElec* x = c.composant(b.composant);
      if (!x) continue;
      v.push_back({x->uuid, x->reference, x->symbole, b.broche, parR});
      // Au-delà d'une résistance en série (une seule)
      if (!parR && (x->symbole == "R" || x->symbole == "R_Small")) {
        const std::string autre = b.broche == "1" ? "2" : "1";
        const int r2 = k.reseau(x->uuid, autre);
        vus.insert({x->uuid, autre});
        if (r2 >= 0 && r2 != r) {
          // Pas au-delà d'une alimentation (tirage vers VCC, rappel vers GND)
          const auto& rs = k.reseaux[static_cast<std::size_t>(r2)];
          const std::string nr = majuscules(rs.nom);
          const bool alim = !rs.alimentations.empty() || rs.alimente || (!nr.empty() && nr[0] == '+') || nr == "VCC" || nr == "VDD" || nr == "GND" || nr == "VSS" || nr == "3V3" || nr == "5V";
          if (!alim) parcourir(r2, true);
          else v.push_back({x->uuid, x->reference, "alimentation:" + rs.nom, autre, true});
        }
      }
    }
  };
  parcourir(r0, false);
  return v;
}

namespace {
bool estCharge(const Raccordement& r) {
  if (r.symbole == "LED" || r.symbole == "Buzzer" || r.symbole == "Moteur" || r.symbole.find("Relais") != std::string::npos) return true;
  // Base d'un transistor, grille d'un MOSFET
  return (r.symbole.rfind("Q_", 0) == 0) && (r.broche == "1");
}
bool estCommande(const Raccordement& r) { return r.symbole == "Bouton" || r.symbole == "Interrupteur" || r.symbole == "Potentiometre"; }
bool estTirage(const Raccordement& r) { return r.symbole.rfind("alimentation:", 0) == 0; }
bool brocheMli(const std::string& port) {
  static const std::set<std::string> p{"PD3", "PD5", "PD6", "PB1", "PB2", "PB3"};
  return p.count(port) > 0;
}
std::string decrire(const std::string& port) {
  const std::string a = brocheArduinoUno(port);
  return a.empty() ? port : port + " (" + a + ")";
}
}  // namespace

std::vector<EcartProgramme> comparerProgramme(const ConceptionElec& c, const ProgrammeEmbarque& p, const std::string& source) {
  std::vector<EcartProgramme> v;
  const ComposantElec* mcu = c.parReference(p.cible);
  if (!mcu) {
    // Le seul microcontrôleur de la conception
    for (const auto& x : c.composants)
      if (const DefSymbole* d = c.symbole(x.symbole); d && d->categorie == "Microcontrôleurs") {
        mcu = &x;
        break;
      }
  }
  if (!mcu) return v;
  const DefSymbole* d = c.symbole(mcu->symbole);
  if (!d) return v;
  const std::vector<ConfigBroche> config = lireConfiguration(source, p.langage);
  std::map<std::string, const ConfigBroche*> parPort;
  for (const auto& x : config) parPort[x.port] = &x;
  bool i2c = false;
  for (const auto& b : d->broches) {
    if (b.type == TypeBroche::AlimEntree || b.type == TypeBroche::AlimSortie) continue;
    const std::string port = portDuNumero(*d, b.numero);
    const std::vector<Raccordement> r = raccordements(c, mcu->uuid, b.numero);
    const auto itc = parPort.find(port);
    const ConfigBroche* cf = itc == parPort.end() ? nullptr : itc->second;
    const ModeBroche mode = cf ? cf->mode : ModeBroche::Entree;
    auto ecart = [&](const std::string& genre, const std::string& message, const std::string& autre) {
      EcartProgramme e;
      e.genre = genre;
      e.message = message;
      e.port = port;
      e.numero = b.numero;
      e.composant = mcu->uuid;
      e.autre = autre;
      e.ligne = cf ? cf->ligne : 0;
      v.push_back(e);
    };
    const std::string ligne = cf ? " (ligne " + std::to_string(cf->ligne) + ")" : std::string();
    for (const auto& x : r) {
      if (estCharge(x) && (mode == ModeBroche::Entree || mode == ModeBroche::EntreeTirage))
        ecart("entrée vers une charge",
              decrire(port) + (cf ? " déclarée en " + std::string(nomMode(mode)) + " par le programme" + ligne : std::string(" laissée en entrée (jamais configurée) par le programme")) + ", câblée à " +
                  (x.symbole == "LED" ? "la LED " : "") + x.reference + (x.parResistance ? " par une résistance" : "") + " : elle ne sera pas commandée",
              x.composant);
      if (estCommande(x) && mode == ModeBroche::Sortie)
        ecart("sortie sur une commande", decrire(port) + " déclarée en sortie par le programme" + ligne + ", câblée à " + x.reference + " : court-circuit à l'appui", x.composant);
    }
    if (cf && r.empty() && cf->mode != ModeBroche::I2C && cf->mode != ModeBroche::SPI && cf->mode != ModeBroche::Serie)
      ecart("broche en l'air", decrire(port) + " " + nomMode(cf->mode) + " dans le programme" + ligne + ", reliée à rien sur le schéma", {});
    if (cf && cf->mode == ModeBroche::Analogique && port.rfind("PC", 0) != 0) ecart("analogique impossible", decrire(port) + " lue en analogique" + ligne + " : ce n'est pas une entrée du convertisseur", {});
    if (cf && cf->mode == ModeBroche::Pwm && !brocheMli(port)) ecart("MLI impossible", decrire(port) + " en MLI" + ligne + " : aucune sortie de temporisateur sur cette broche", {});
    if (cf && cf->mode == ModeBroche::I2C) {
      bool tirage = false;
      for (const auto& x : r) tirage |= estTirage(x);
      if (!tirage && !r.empty() && !i2c) {
        ecart("I2C sans tirage", decrire(port) + " : bus I2C sans résistance de tirage vers l'alimentation", {});
        i2c = true;
      }
    }
  }
  return v;
}

// ======================================================================================================================
// Initialisation d'après le câblage
// ======================================================================================================================
std::string initialisationPeripheriques(const ConceptionElec& c, const std::string& micro, const std::string& langage) {
  const ComposantElec* mcu = c.parReference(micro);
  const DefSymbole* d = mcu ? c.symbole(mcu->symbole) : nullptr;
  if (!d) return {};
  std::ostringstream ar, cr;
  std::map<char, int> ddr, port;
  bool i2c = false, spi = false, serie = false;
  std::set<int> analogiques;
  for (const auto& b : d->broches) {
    if (b.type == TypeBroche::AlimEntree || b.type == TypeBroche::AlimSortie) continue;
    const std::string p = portDuNumero(*d, b.numero);
    if (p.size() != 3) continue;
    const std::vector<Raccordement> r = raccordements(c, mcu->uuid, b.numero);
    if (r.empty()) continue;
    bool charge = false, commande = false, versMasse = false, analogique = false;
    for (const auto& x : r) {
      charge |= estCharge(x);
      commande |= x.symbole == "Bouton" || x.symbole == "Interrupteur";
      analogique |= x.symbole == "Potentiometre" || x.symbole.find("Thermistance") != std::string::npos || x.symbole.find("Photo") != std::string::npos;
    }
    // Bouton vers la masse : tirage interne
    if (commande) {
      const Connectivite k = connectivite(c);
      for (const auto& x : r)
        if (x.symbole == "Bouton" || x.symbole == "Interrupteur") {
          const std::string autre = x.broche == "1" ? "2" : "1";
          const int ra = k.reseau(x.composant, autre);
          if (ra >= 0) versMasse |= k.reseaux[static_cast<std::size_t>(ra)].nom == "GND";
        }
    }
    const std::string a = brocheArduinoUno(p);
    const std::string nomArduino = a.empty() ? p : (a[0] == 'D' ? a.substr(1) : a);
    if (p == "PC4" || p == "PC5") {
      bool autreQueTirage = false;
      for (const auto& x : r) autreQueTirage |= !estTirage(x) && x.symbole != "R";
      if (autreQueTirage) {
        i2c = true;
        continue;
      }
    }
    if ((p == "PB3" || p == "PB4" || p == "PB5") && !charge && !commande) {
      spi = true;
      continue;
    }
    if (p == "PD0" || p == "PD1") {
      serie = true;
      continue;
    }
    if (charge) {
      ar << "  pinMode(" << nomArduino << ", OUTPUT);  // " << r.front().reference << "\n";
      ddr[p[1]] |= 1 << (p[2] - '0');
    } else if (commande) {
      ar << "  pinMode(" << nomArduino << ", " << (versMasse ? "INPUT_PULLUP" : "INPUT") << ");  // " << r.front().reference << "\n";
      if (versMasse) port[p[1]] |= 1 << (p[2] - '0');
    } else if (analogique && p[1] == 'C')
      analogiques.insert(p[2] - '0');
  }
  if (serie) ar << "  Serial.begin(9600);\n";
  if (i2c) ar << "  Wire.begin();  // SDA A4, SCL A5, 100 kHz\n";
  if (spi) ar << "  SPI.begin();\n";
  for (const auto& [l, v] : ddr) cr << "  DDR" << l << " |= 0x" << std::hex << v << std::dec << ";  // sorties\n";
  for (const auto& [l, v] : port) cr << "  PORT" << l << " |= 0x" << std::hex << v << std::dec << ";  // tirages des boutons\n";
  if (serie) cr << "  UBRR0 = F_CPU / 16 / 9600 - 1;  // 9 600 bauds\n  UCSR0B = (1 << TXEN0) | (1 << RXEN0);\n  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);  // 8 bits, sans parité\n";
  if (i2c) cr << "  TWSR = 0;\n  TWBR = (F_CPU / 100000UL - 16) / 2;  // 100 kHz\n  TWCR = (1 << TWEN);\n";
  if (spi) cr << "  DDRB |= (1 << PB3) | (1 << PB5) | (1 << PB2);  // MOSI, SCK, SS\n  SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0);  // maître, F_CPU / 16\n";
  if (!analogiques.empty()) {
    cr << "  ADMUX = (1 << REFS0);  // AVcc\n  ADCSRA = (1 << ADEN) | (7 << ADPS0);  // convertisseur, F_CPU / 128\n";
    cr << "  DIDR0 = 0x" << std::hex;
    int m = 0;
    for (int a : analogiques) m |= 1 << a;
    cr << m << std::dec << ";  // entrées numériques coupées sur les voies analogiques\n";
  }
  return langage == "c" ? cr.str() : ar.str();
}

// ======================================================================================================================
// Compilation
// ======================================================================================================================
std::vector<ChaineEmbarquee> chainesDuPoste() {
  std::vector<ChaineEmbarquee> v;
  for (const char* n : {"avr-gcc", "avr-g++", "avr-objcopy", "avr-size", "arduino-cli", "avrdude", "mpremote", "avr-gdb", "avr-objdump", "simavr"}) {
    const std::string p = programmeDuPoste(n);
    v.push_back({n, p, !p.empty()});
  }
  return v;
}

std::vector<ErreurCompilation> lireErreursGcc(const std::string& sortie) {
  std::vector<ErreurCompilation> v;
  static const std::regex re(R"(^(.*?):(\d+):(?:(\d+):)?\s*(fatal error|error|warning|erreur|avertissement)\s*:\s*(.*)$)");
  for (const auto& l : lignesDe(sortie)) {
    std::smatch x;
    if (!std::regex_search(l, x, re)) continue;
    ErreurCompilation e;
    e.fichier = utf8(cheminFichier(x[1].str()).filename());
    e.ligne = std::stoi(x[2].str());
    e.colonne = x[3].matched ? std::stoi(x[3].str()) : 0;
    e.avertissement = x[4].str() == "warning" || x[4].str() == "avertissement";
    e.message = x[5].str();
    v.push_back(e);
  }
  return v;
}

namespace {
// Prototypes des fonctions d'un croquis (comme le fait l'environnement Arduino), pour les appeler avant leur
// définition
std::string prototypes(const std::string& croquis) {
  std::ostringstream o;
  static const std::regex def(R"(^([A-Za-z_][\w\s\*&:<>]*?[\s\*&])([A-Za-z_]\w*)\s*\(([^;{}()]*)\)\s*\{)");
  int profondeur = 0;
  for (const auto& l : lignesDe(croquis)) {
    if (profondeur == 0) {
      std::smatch x;
      const std::string t = nettoyer(sansCommentaire(l, false));
      if (std::regex_search(t, x, def)) {
        const std::string type = nettoyer(x[1].str()), nom = x[2].str();
        if (nom != "setup" && nom != "loop" && type.find('=') == std::string::npos && type != "else" && type.rfind("return", 0) != 0) o << type << " " << nom << "(" << x[3].str() << ");\n";
      }
    }
    for (const char ch : sansCommentaire(l, false)) profondeur += ch == '{' ? 1 : ch == '}' ? -1 : 0;
  }
  return o.str();
}
std::string nb(double v) {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o.precision(12);
  o << v;
  return o.str();
}
}  // namespace

ResultatCompilation compilerProgramme(const std::string& source, const std::string& langage, const std::string& dossierSortie, double frequence, const std::map<std::string, std::string>& fichiers,
                                      const std::string& carte) {
  ResultatCompilation r;
  std::error_code ec;
  const fs::path d = cheminFichier(dossierSortie);
  fs::create_directories(d, ec);
  for (const auto& [nom, texte] : fichiers) ecrireFichierAtomique(d / cheminFichier(nom), texte);
  const std::string f = nb(std::round(frequence)) + "UL";
  auto taille = [&](const std::string& elf) {
    if (programmeDuPoste("avr-size").empty()) return;
    const ResultatProcessus t = executerProcessus({"avr-size", "-A", elf}, {.dossier = {}, .entree = {}, .delai = 30, .environnement = {}});
    std::size_t total = 0;
    for (const auto& l : lignesDe(t.sortie)) {
      std::istringstream in(l);
      std::string section;
      std::size_t n = 0;
      if (in >> section >> n && (section == ".text" || section == ".data")) total += n;
    }
    r.tailleProgramme = total;
  };
  auto hex = [&](const std::string& elf) {
    if (programmeDuPoste("avr-objcopy").empty()) return;
    const std::string h = utf8(d / "programme.hex");
    const ResultatProcessus o = executerProcessus({"avr-objcopy", "-O", "ihex", "-R", ".eeprom", elf, h}, {.dossier = {}, .entree = {}, .delai = 30, .environnement = {}});
    if (o.reussi()) r.hex = h;
  };
  if (langage == "micropython") {
    r.chaine = "python3";
    const fs::path p = d / "main.py";
    ecrireFichierAtomique(p, source);
    const ResultatProcessus x = executerProcessus({"python3", "-I", "-c", "import ast,sys\nast.parse(open(sys.argv[1],encoding='utf-8').read(),sys.argv[1])", utf8(p)},
                                                  {.dossier = {}, .entree = {}, .delai = 30, .environnement = {}});
    r.journal = x.sortie + x.erreurs;
    if (!x.lance) {
      r.erreurs.push_back({"main.py", "python3 manque pour relire le programme", 0, 0, false});
      return r;
    }
    r.reussi = x.code == 0;
    static const std::regex ligne(R"(line (\d+))");
    std::smatch m;
    if (!r.reussi) {
      ErreurCompilation e;
      e.fichier = "main.py";
      e.message = lignesDe(x.erreurs).empty() ? "erreur de syntaxe" : lignesDe(x.erreurs).back();
      if (std::regex_search(x.erreurs, m, ligne)) e.ligne = std::stoi(m[1].str());
      r.erreurs.push_back(e);
    }
    return r;
  }
  if (langage == "arduino" && !programmeDuPoste("arduino-cli").empty()) {
    // arduino-cli : le croquis dans un dossier du même nom
    r.chaine = "arduino-cli";
    const fs::path dc = d / "croquis";
    fs::create_directories(dc, ec);
    ecrireFichierAtomique(dc / "croquis.ino", source);
    for (const auto& [nom, texte] : fichiers) ecrireFichierAtomique(dc / cheminFichier(nom), texte);
    const ResultatProcessus x = executerProcessus({"arduino-cli", "compile", "--fqbn", carte, "--output-dir", utf8(d), utf8(dc)}, {.dossier = {}, .entree = {}, .delai = 300, .environnement = {}});
    r.journal = x.sortie + x.erreurs;
    r.erreurs = lireErreursGcc(x.erreurs);
    r.reussi = x.reussi();
    if (r.reussi) {
      r.elf = utf8(d / "croquis.ino.elf");
      r.hex = utf8(d / "croquis.ino.hex");
      taille(r.elf);
    }
    return r;
  }
  if (programmeDuPoste("avr-gcc").empty()) {
    r.erreurs.push_back({"", "avr-gcc manque sur ce poste (paquets gcc-avr et avr-libc)", 0, 0, false});
    return r;
  }
  const std::string elf = utf8(d / "programme.elf");
  std::vector<std::string> cmd;
  if (langage == "arduino") {
    // Le petit noyau Arduino de Prométhée, avec avr-g++
    r.chaine = "noyau Arduino de Prométhée";
    for (const auto& [nom, texte] : noyauArduino()) ecrireFichierAtomique(d / cheminFichier(nom), texte);
    const std::string croquis = "#include \"Arduino.h\"\n" + prototypes(source) + "#line 1 \"croquis.ino\"\n" + source;
    ecrireFichierAtomique(d / "croquis.cpp", croquis);
    cmd = {"avr-g++", "-mmcu=atmega328p", "-DF_CPU=" + f, "-DARDUINO=10819", "-Os", "-g", "-std=gnu++14", "-fno-exceptions", "-fno-threadsafe-statics", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
           "-I", utf8(d), "-o", elf, utf8(d / "croquis.cpp")};
    for (const auto& [nom, texte] : noyauArduino())
      if (nom.size() > 4 && nom.compare(nom.size() - 4, 4, ".cpp") == 0) cmd.push_back(utf8(d / cheminFichier(nom)));
    for (const auto& [nom, texte] : fichiers)
      if (nom.size() > 4 && nom.compare(nom.size() - 4, 4, ".cpp") == 0) cmd.push_back(utf8(d / cheminFichier(nom)));
  } else {
    r.chaine = "avr-gcc";
    ecrireFichierAtomique(d / "programme.c", source);
    cmd = {"avr-gcc", "-mmcu=atmega328p", "-DF_CPU=" + f, "-Os", "-g", "-std=gnu11", "-I", utf8(d), "-o", elf, utf8(d / "programme.c")};
    for (const auto& [nom, texte] : fichiers)
      if (nom.size() > 2 && nom.compare(nom.size() - 2, 2, ".c") == 0) cmd.push_back(utf8(d / cheminFichier(nom)));
  }
  const ResultatProcessus x = executerProcessus(cmd, {.dossier = {}, .entree = {}, .delai = 300, .environnement = {}});
  r.journal = x.sortie + x.erreurs;
  r.erreurs = lireErreursGcc(x.erreurs);
  r.reussi = x.reussi() && fs::exists(cheminFichier(elf), ec);
  if (r.reussi) {
    r.elf = elf;
    hex(elf);
    taille(elf);
  }
  return r;
}

std::vector<std::string> commandeTeleversement(const ProgrammeEmbarque& p, const std::string& fichier, const std::string& port) {
  if (p.langage == "micropython") {
    if (programmeDuPoste("mpremote").empty()) return {};
    return {"mpremote", "connect", port, "fs", "cp", fichier, ":main.py", "+", "reset"};
  }
  if (p.langage == "arduino" && !programmeDuPoste("arduino-cli").empty()) return {"arduino-cli", "upload", "--fqbn", p.carte, "--port", port, "--input-file", fichier};
  if (programmeDuPoste("avrdude").empty()) return {};
  return {"avrdude", "-p", "m328p", "-c", "arduino", "-P", port, "-b", "115200", "-U", "flash:w:" + fichier + ":i"};
}

// ---------- Le petit noyau Arduino de Prométhée ----------
const std::map<std::string, std::string>& noyauArduino() {
  static const std::map<std::string, std::string> f = {
      {"Arduino.h", R"ARD(// Petit noyau Arduino de Prométhée (ATmega328P, brochage de l'Arduino Uno) : de quoi compiler un croquis simple
// quand arduino-cli n'est pas installé. Licence GPL-3.0-only.
#pragma once
#include <avr/interrupt.h>
#include <avr/io.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define HIGH 0x1
#define LOW 0x0
#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2
#define LED_BUILTIN 13
#define A0 14
#define A1 15
#define A2 16
#define A3 17
#define A4 18
#define A5 19
#define DEC 10
#define HEX 16
#define BIN 2
typedef uint8_t byte;
typedef bool boolean;
#ifdef __cplusplus
extern "C" {
#endif
void pinMode(uint8_t broche, uint8_t mode);
void digitalWrite(uint8_t broche, uint8_t valeur);
int digitalRead(uint8_t broche);
int analogRead(uint8_t broche);
void analogWrite(uint8_t broche, int valeur);
unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void setup(void);
void loop(void);
#ifdef __cplusplus
}
class Print {
 public:
  virtual size_t write(uint8_t c) = 0;
  size_t write(const char* s) {
    size_t n = 0;
    while (*s) n += write(static_cast<uint8_t>(*s++));
    return n;
  }
  size_t print(const char* s) { return write(s); }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t print(unsigned long v, int base = DEC);
  size_t print(long v, int base = DEC);
  size_t print(int v, int base = DEC) { return print(static_cast<long>(v), base); }
  size_t print(unsigned int v, int base = DEC) { return print(static_cast<unsigned long>(v), base); }
  size_t print(double v, int chiffres = 2);
  size_t println() { return write("\r\n"); }
  size_t println(const char* s) { return print(s) + println(); }
  size_t println(char c) { return print(c) + println(); }
  size_t println(int v, int base = DEC) { return print(v, base) + println(); }
  size_t println(unsigned int v, int base = DEC) { return print(v, base) + println(); }
  size_t println(long v, int base = DEC) { return print(v, base) + println(); }
  size_t println(unsigned long v, int base = DEC) { return print(v, base) + println(); }
  size_t println(double v, int chiffres = 2) { return print(v, chiffres) + println(); }
};
class HardwareSerial : public Print {
 public:
  void begin(unsigned long debit);
  size_t write(uint8_t c) override;
  using Print::write;
  int available();
  int read();
  void flush();
  operator bool() { return true; }
};
extern HardwareSerial Serial;
#endif
)ARD"},
      {"Wire.h", R"ARD(// Petit noyau Arduino de Prométhée : maître I2C sur le TWI de l'ATmega328P. Licence GPL-3.0-only.
#pragma once
#include "Arduino.h"
class TwoWire {
 public:
  void begin();
  void setClock(unsigned long frequence);
  void beginTransmission(uint8_t adresse);
  size_t write(uint8_t octet);
  uint8_t endTransmission(bool arret = true);
  uint8_t requestFrom(uint8_t adresse, uint8_t nombre);
  int available();
  int read();

 private:
  uint8_t m_adresse = 0, m_tampon[32] = {}, m_n = 0, m_lu = 0, m_recu = 0;
};
extern TwoWire Wire;
)ARD"},
      {"promethee_coeur.cpp", R"ARD(// Petit noyau Arduino de Prométhée : broches, temps, MLI, convertisseur, liaison série. Licence GPL-3.0-only.
#include "Arduino.h"
#include <util/delay.h>

// Broches de l'Uno : D0 à D7 sur le port D, D8 à D13 sur le port B, A0 à A5 (14 à 19) sur le port C
static volatile uint8_t* registreDdr(uint8_t b) { return b < 8 ? &DDRD : b < 14 ? &DDRB : &DDRC; }
static volatile uint8_t* registrePort(uint8_t b) { return b < 8 ? &PORTD : b < 14 ? &PORTB : &PORTC; }
static volatile uint8_t* registrePin(uint8_t b) { return b < 8 ? &PIND : b < 14 ? &PINB : &PINC; }
static uint8_t rang(uint8_t b) { return b < 8 ? b : b < 14 ? b - 8 : b - 14; }

void pinMode(uint8_t b, uint8_t mode) {
  if (b > 19) return;
  const uint8_t k = static_cast<uint8_t>(1u << rang(b));
  const uint8_t s = SREG;
  cli();
  if (mode == OUTPUT) *registreDdr(b) |= k;
  else {
    *registreDdr(b) &= static_cast<uint8_t>(~k);
    if (mode == INPUT_PULLUP) *registrePort(b) |= k;
    else *registrePort(b) &= static_cast<uint8_t>(~k);
  }
  SREG = s;
}
static void arreterMli(uint8_t b) {
  switch (b) {
    case 3: TCCR2A &= static_cast<uint8_t>(~_BV(COM2B1)); break;
    case 5: TCCR0A &= static_cast<uint8_t>(~_BV(COM0B1)); break;
    case 6: TCCR0A &= static_cast<uint8_t>(~_BV(COM0A1)); break;
    case 9: TCCR1A &= static_cast<uint8_t>(~_BV(COM1A1)); break;
    case 10: TCCR1A &= static_cast<uint8_t>(~_BV(COM1B1)); break;
    case 11: TCCR2A &= static_cast<uint8_t>(~_BV(COM2A1)); break;
    default: break;
  }
}
void digitalWrite(uint8_t b, uint8_t v) {
  if (b > 19) return;
  arreterMli(b);
  const uint8_t k = static_cast<uint8_t>(1u << rang(b));
  const uint8_t s = SREG;
  cli();
  if (v) *registrePort(b) |= k;
  else *registrePort(b) &= static_cast<uint8_t>(~k);
  SREG = s;
}
int digitalRead(uint8_t b) {
  if (b > 19) return LOW;
  arreterMli(b);
  return (*registrePin(b) & (1u << rang(b))) ? HIGH : LOW;
}
int analogRead(uint8_t b) {
  if (b >= 14) b = static_cast<uint8_t>(b - 14);
  ADMUX = static_cast<uint8_t>(_BV(REFS0) | (b & 7));
  ADCSRA |= _BV(ADEN) | _BV(ADSC);
  while (ADCSRA & _BV(ADSC)) {
  }
  const uint8_t bas = ADCL, haut = ADCH;
  return (haut << 8) | bas;
}
void analogWrite(uint8_t b, int v) {
  pinMode(b, OUTPUT);
  if (v <= 0) return digitalWrite(b, LOW);
  if (v >= 255) return digitalWrite(b, HIGH);
  const uint8_t x = static_cast<uint8_t>(v);
  switch (b) {
    case 3: TCCR2A |= _BV(COM2B1); OCR2B = x; break;
    case 5: TCCR0A |= _BV(COM0B1); OCR0B = x; break;
    case 6: TCCR0A |= _BV(COM0A1); OCR0A = x; break;
    case 9: TCCR1A |= _BV(COM1A1); OCR1A = x; break;
    case 10: TCCR1A |= _BV(COM1B1); OCR1B = x; break;
    case 11: TCCR2A |= _BV(COM2A1); OCR2A = x; break;
    default: digitalWrite(b, v < 128 ? LOW : HIGH);
  }
}

// Temps : temporisateur 0 en MLI rapide, diviseur 64 ; chaque débordement ajoute sa durée en microsecondes
static const unsigned long US_PAR_DEBORDEMENT = (64UL * 256UL) / (F_CPU / 1000000UL);
static volatile unsigned long g_debordements = 0, g_millis = 0;
static volatile unsigned int g_reste = 0;  // microsecondes au-delà de la milliseconde
ISR(TIMER0_OVF_vect) {
  unsigned long m = g_millis;
  unsigned int r = g_reste + static_cast<unsigned int>(US_PAR_DEBORDEMENT % 1000UL);
  m += US_PAR_DEBORDEMENT / 1000UL;
  if (r >= 1000) {
    r -= 1000;
    ++m;
  }
  g_reste = r;
  g_millis = m;
  ++g_debordements;
}
unsigned long millis(void) {
  const uint8_t s = SREG;
  cli();
  const unsigned long m = g_millis;
  SREG = s;
  return m;
}
unsigned long micros(void) {
  const uint8_t s = SREG;
  cli();
  unsigned long n = g_debordements;
  const uint8_t t = TCNT0;
  if ((TIFR0 & _BV(TOV0)) && t < 255) ++n;
  SREG = s;
  return ((n << 8) + t) * (64UL / (F_CPU / 1000000UL));
}
void delay(unsigned long ms) {
  unsigned long debut = micros();
  while (ms > 0)
    while (ms > 0 && micros() - debut >= 1000) {
      --ms;
      debut += 1000;
    }
}
void delayMicroseconds(unsigned int us) {
  while (us--) _delay_us(1);
}

// Liaison série : USART0, 8 bits, sans parité, un bit d'arrêt
HardwareSerial Serial;
void HardwareSerial::begin(unsigned long debit) {
  const uint16_t ubrr = static_cast<uint16_t>((F_CPU / 16UL + debit / 2) / debit - 1);
  UBRR0H = static_cast<uint8_t>(ubrr >> 8);
  UBRR0L = static_cast<uint8_t>(ubrr);
  UCSR0A = 0;
  UCSR0B = _BV(TXEN0) | _BV(RXEN0);
  UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
}
size_t HardwareSerial::write(uint8_t c) {
  while (!(UCSR0A & _BV(UDRE0))) {
  }
  UDR0 = c;
  return 1;
}
int HardwareSerial::available() { return (UCSR0A & _BV(RXC0)) ? 1 : 0; }
int HardwareSerial::read() { return (UCSR0A & _BV(RXC0)) ? UDR0 : -1; }
void HardwareSerial::flush() {
  while (!(UCSR0A & _BV(UDRE0))) {
  }
}
size_t Print::print(unsigned long v, int base) {
  char t[33];
  char* p = t + 32;
  *p = 0;
  if (base < 2) base = 10;
  do {
    const uint8_t ch = static_cast<uint8_t>(v % static_cast<unsigned long>(base));
    v /= static_cast<unsigned long>(base);
    *--p = static_cast<char>(ch < 10 ? '0' + ch : 'A' + ch - 10);
  } while (v);
  return write(p);
}
size_t Print::print(long v, int base) {
  if (base == 10 && v < 0) return write(static_cast<uint8_t>('-')) + print(static_cast<unsigned long>(-v), 10);
  return print(static_cast<unsigned long>(v), base);
}
size_t Print::print(double v, int chiffres) {
  size_t n = 0;
  if (isnan(v)) return write("nan");
  if (v < 0) {
    n += write(static_cast<uint8_t>('-'));
    v = -v;
  }
  double arrondi = 0.5;
  for (int i = 0; i < chiffres; ++i) arrondi /= 10;
  v += arrondi;
  const unsigned long entier = static_cast<unsigned long>(v);
  double reste = v - static_cast<double>(entier);
  n += print(entier);
  if (chiffres > 0) {
    n += write(static_cast<uint8_t>('.'));
    while (chiffres-- > 0) {
      reste *= 10;
      const int c = static_cast<int>(reste);
      n += write(static_cast<uint8_t>('0' + c));
      reste -= c;
    }
  }
  return n;
}

// Démarrage : temporisateurs de millis() et de analogWrite(), convertisseur, puis setup() et loop()
int main(void) {
  TCCR0A = _BV(WGM01) | _BV(WGM00);
  TCCR0B = _BV(CS01) | _BV(CS00);
  TIMSK0 = _BV(TOIE0);
  TCCR1A = _BV(WGM10);
  TCCR1B = _BV(CS11) | _BV(CS10);
  TCCR2A = _BV(WGM20);
  TCCR2B = _BV(CS22);
  ADCSRA = _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0) | _BV(ADEN);
  UCSR0B = 0;
  sei();
  setup();
  for (;;) loop();
  return 0;
}
)ARD"},
      {"promethee_wire.cpp", R"ARD(// Petit noyau Arduino de Prométhée : maître I2C (TWI). Licence GPL-3.0-only.
#include "Wire.h"

TwoWire Wire;
static bool attendre() {
  uint16_t n = 0;
  while (!(TWCR & _BV(TWINT)))
    if (++n == 0) return false;
  return true;
}
static uint8_t etat() { return TWSR & 0xF8; }
static bool depart() {
  TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);
  return attendre() && (etat() == 0x08 || etat() == 0x10);
}
static void arret() { TWCR = _BV(TWINT) | _BV(TWSTO) | _BV(TWEN); }
static bool envoyer(uint8_t o) {
  TWDR = o;
  TWCR = _BV(TWINT) | _BV(TWEN);
  return attendre();
}
void TwoWire::begin() {
  TWSR = 0;
  TWBR = static_cast<uint8_t>((F_CPU / 100000UL - 16) / 2);
  TWCR = _BV(TWEN);
  PORTC |= _BV(4) | _BV(5);
}
void TwoWire::setClock(unsigned long f) { TWBR = static_cast<uint8_t>((F_CPU / f - 16) / 2); }
void TwoWire::beginTransmission(uint8_t a) {
  m_adresse = a;
  m_n = 0;
}
size_t TwoWire::write(uint8_t o) {
  if (m_n >= sizeof m_tampon) return 0;
  m_tampon[m_n++] = o;
  return 1;
}
uint8_t TwoWire::endTransmission(bool fin) {
  if (!depart()) {
    arret();
    return 4;
  }
  if (!envoyer(static_cast<uint8_t>(m_adresse << 1)) || etat() != 0x18) {
    arret();
    return 2;
  }
  for (uint8_t i = 0; i < m_n; ++i)
    if (!envoyer(m_tampon[i]) || etat() != 0x28) {
      arret();
      return 3;
    }
  if (fin) arret();
  return 0;
}
uint8_t TwoWire::requestFrom(uint8_t a, uint8_t n) {
  if (n > sizeof m_tampon) n = sizeof m_tampon;
  m_recu = m_lu = 0;
  if (!depart()) {
    arret();
    return 0;
  }
  if (!envoyer(static_cast<uint8_t>((a << 1) | 1)) || etat() != 0x40) {
    arret();
    return 0;
  }
  for (uint8_t i = 0; i < n; ++i) {
    TWCR = static_cast<uint8_t>(_BV(TWINT) | _BV(TWEN) | (i + 1 < n ? _BV(TWEA) : 0));
    if (!attendre()) break;
    m_tampon[m_recu++] = TWDR;
  }
  arret();
  return m_recu;
}
int TwoWire::available() { return m_recu - m_lu; }
int TwoWire::read() { return m_lu < m_recu ? m_tampon[m_lu++] : -1; }
)ARD"},
  };
  return f;
}

// ======================================================================================================================
// Modules de la galerie
// ======================================================================================================================
namespace {
const char* PILOTE_BME280_H = R"ARD(// Pilote BME280 de Prométhée (capteur Bosch de température, pression et humidité, en I2C), d'après les formules de
// compensation de la fiche technique. Licence GPL-3.0-only.
#pragma once
#include "Wire.h"
class BME280 {
 public:
  bool begin(uint8_t adresse = 0x76);
  float readTemperature();  // °C
  float readPressure();     // Pa
  float readHumidity();     // % d'humidité relative

 private:
  uint8_t lire8(uint8_t r);
  void lire(uint8_t r, uint8_t* t, uint8_t n);
  void ecrire(uint8_t r, uint8_t v);
  int32_t brut20(uint8_t r);
  uint8_t m_adresse = 0x76;
  uint16_t m_t1 = 0, m_p1 = 0;
  int16_t m_t2 = 0, m_t3 = 0, m_p2 = 0, m_p3 = 0, m_p4 = 0, m_p5 = 0, m_p6 = 0, m_p7 = 0, m_p8 = 0, m_p9 = 0, m_h2 = 0, m_h4 = 0, m_h5 = 0;
  uint8_t m_h1 = 0, m_h3 = 0;
  int8_t m_h6 = 0;
  int32_t m_fin = 0;
};
)ARD";
const char* PILOTE_BME280_CPP = R"ARD(// Pilote BME280 de Prométhée. Licence GPL-3.0-only.
#include "bme280.h"

uint8_t BME280::lire8(uint8_t r) {
  uint8_t v = 0;
  lire(r, &v, 1);
  return v;
}
void BME280::lire(uint8_t r, uint8_t* t, uint8_t n) {
  Wire.beginTransmission(m_adresse);
  Wire.write(r);
  Wire.endTransmission(false);
  Wire.requestFrom(m_adresse, n);
  for (uint8_t i = 0; i < n; ++i) t[i] = static_cast<uint8_t>(Wire.read());
}
void BME280::ecrire(uint8_t r, uint8_t v) {
  Wire.beginTransmission(m_adresse);
  Wire.write(r);
  Wire.write(v);
  Wire.endTransmission();
}
int32_t BME280::brut20(uint8_t r) {
  uint8_t t[3];
  lire(r, t, 3);
  return (static_cast<int32_t>(t[0]) << 12) | (static_cast<int32_t>(t[1]) << 4) | (t[2] >> 4);
}
bool BME280::begin(uint8_t adresse) {
  m_adresse = adresse;
  Wire.begin();
  if (lire8(0xD0) != 0x60) return false;  // identifiant de la puce
  uint8_t c[26];
  lire(0x88, c, 26);
  m_t1 = static_cast<uint16_t>(c[0] | (c[1] << 8));
  m_t2 = static_cast<int16_t>(c[2] | (c[3] << 8));
  m_t3 = static_cast<int16_t>(c[4] | (c[5] << 8));
  m_p1 = static_cast<uint16_t>(c[6] | (c[7] << 8));
  m_p2 = static_cast<int16_t>(c[8] | (c[9] << 8));
  m_p3 = static_cast<int16_t>(c[10] | (c[11] << 8));
  m_p4 = static_cast<int16_t>(c[12] | (c[13] << 8));
  m_p5 = static_cast<int16_t>(c[14] | (c[15] << 8));
  m_p6 = static_cast<int16_t>(c[16] | (c[17] << 8));
  m_p7 = static_cast<int16_t>(c[18] | (c[19] << 8));
  m_p8 = static_cast<int16_t>(c[20] | (c[21] << 8));
  m_p9 = static_cast<int16_t>(c[22] | (c[23] << 8));
  m_h1 = c[25];
  uint8_t h[7];
  lire(0xE1, h, 7);
  m_h2 = static_cast<int16_t>(h[0] | (h[1] << 8));
  m_h3 = h[2];
  m_h4 = static_cast<int16_t>((h[3] << 4) | (h[4] & 0x0F));
  m_h5 = static_cast<int16_t>((h[5] << 4) | (h[4] >> 4));
  m_h6 = static_cast<int8_t>(h[6]);
  ecrire(0xF2, 0x01);  // humidité ×1
  ecrire(0xF4, 0x27);  // température et pression ×1, mode normal
  return true;
}
float BME280::readTemperature() {
  const int32_t adc = brut20(0xFA);
  const int32_t v1 = ((((adc >> 3) - (static_cast<int32_t>(m_t1) << 1))) * static_cast<int32_t>(m_t2)) >> 11;
  const int32_t d = (adc >> 4) - static_cast<int32_t>(m_t1);
  const int32_t v2 = (((d * d) >> 12) * static_cast<int32_t>(m_t3)) >> 14;
  m_fin = v1 + v2;
  return ((m_fin * 5 + 128) >> 8) / 100.0f;
}
float BME280::readPressure() {
  readTemperature();
  const int32_t adc = brut20(0xF7);
  float v1 = m_fin / 2.0f - 64000.0f;
  float v2 = v1 * v1 * m_p6 / 32768.0f;
  v2 = v2 + v1 * m_p5 * 2.0f;
  v2 = v2 / 4.0f + m_p4 * 65536.0f;
  v1 = (m_p3 * v1 * v1 / 524288.0f + m_p2 * v1) / 524288.0f;
  v1 = (1.0f + v1 / 32768.0f) * m_p1;
  if (v1 == 0.0f) return 0;
  float p = 1048576.0f - adc;
  p = (p - v2 / 4096.0f) * 6250.0f / v1;
  v1 = m_p9 * p * p / 2147483648.0f;
  v2 = p * m_p8 / 32768.0f;
  return p + (v1 + v2 + m_p7) / 16.0f;
}
float BME280::readHumidity() {
  readTemperature();
  uint8_t t[2];
  lire(0xFD, t, 2);
  const int32_t adc = (t[0] << 8) | t[1];
  float h = m_fin - 76800.0f;
  h = (adc - (m_h4 * 64.0f + m_h5 / 16384.0f * h)) * (m_h2 / 65536.0f * (1.0f + m_h6 / 67108864.0f * h * (1.0f + m_h3 / 67108864.0f * h)));
  h = h * (1.0f - m_h1 * h / 524288.0f);
  return h < 0 ? 0 : h > 100 ? 100 : h;
}
)ARD";
const char* PILOTE_BME280_PY = R"PY(# Pilote BME280 de Prométhée pour MicroPython (I2C), d'après les formules de la fiche technique.
# Licence GPL-3.0-only.
class BME280:
    def __init__(self, i2c, adresse=0x76):
        self.i2c, self.a = i2c, adresse
        c = i2c.readfrom_mem(adresse, 0x88, 26)
        u16 = lambda i: c[i] | (c[i + 1] << 8)
        s16 = lambda i: u16(i) - 65536 if u16(i) > 32767 else u16(i)
        self.t1, self.t2, self.t3 = u16(0), s16(2), s16(4)
        i2c.writeto_mem(adresse, 0xF4, b'\x27')
    def temperature(self):
        d = self.i2c.readfrom_mem(self.a, 0xFA, 3)
        adc = (d[0] << 12) | (d[1] << 4) | (d[2] >> 4)
        v1 = ((adc >> 3) - (self.t1 << 1)) * self.t2 >> 11
        v2 = ((((adc >> 4) - self.t1) ** 2) >> 12) * self.t3 >> 14
        return ((v1 + v2) * 5 + 128 >> 8) / 100
)PY";
}  // namespace

const std::vector<ModuleEmbarque>& galerieEmbarquee() {
  static const std::vector<ModuleEmbarque> g = [] {
    std::vector<ModuleEmbarque> v;
    ModuleEmbarque b;
    b.id = "bme280";
    b.nom = "BME280";
    b.description = "Capteur de température, de pression et d'humidité Bosch BME280 sur carte fille I2C (adresse 0x76)";
    b.interface = "i2c";
    b.adresse = 0x76;
    b.broches = {{"1", "VCC"}, {"2", "GND"}, {"3", "SCL"}, {"4", "SDA"}};
    b.pilote = {{"bme280.h", PILOTE_BME280_H}, {"bme280.cpp", PILOTE_BME280_CPP}, {"bme280.py", PILOTE_BME280_PY}};
    b.inclusion = "#include \"bme280.h\"";
    b.declaration = "BME280 bme;";
    b.initialisation = "bme.begin(0x76);";
    b.inclusionPython = "from bme280 import BME280";
    b.initialisationPython = "bme = BME280(I2C(0, scl=Pin(5), sda=Pin(4)))";
    b.blocs = {{"bme280.temperature", "lire la température", "{variable} = bme.readTemperature();", "{variable} = bme.temperature()", "temperature", "float"},
               {"bme280.pression", "lire la pression", "{variable} = bme.readPressure();", "{variable} = 0", "pression", "float"},
               {"bme280.humidite", "lire l'humidité", "{variable} = bme.readHumidity();", "{variable} = 0", "humidite", "float"}};
    v.push_back(b);
    return v;
  }();
  return g;
}

const ModuleEmbarque* moduleEmbarque(const std::string& id) {
  for (const auto& m : galerieEmbarquee())
    if (m.id == id || pourComparer(m.nom) == pourComparer(id)) return &m;
  return nullptr;
}

DefSymbole symboleModuleEmbarque(const ModuleEmbarque& m) {
  SpecSymbole s;
  s.id = "galerie:" + m.nom;
  s.nom = m.nom;
  s.prefixe = "U";
  s.categorie = "Modules";
  s.empreinte = "PinHeader_1x04_P2.54";
  s.description = m.description;
  s.mpn = m.nom;
  for (const auto& [n, nom] : m.broches) {
    SpecBroche x;
    x.numero = n;
    x.nom = nom;
    x.type = nom == "VCC" || nom == "GND" ? TypeBroche::AlimEntree : TypeBroche::Bidirectionnelle;
    s.broches.push_back(x);
  }
  return genererSymbole(s);
}

namespace {
// Fil de 2,54 mm tiré de la broche, terminé par une étiquette globale
void etiqueter(ConceptionElec& c, std::size_t feuille, const BrochePlacee& b, const std::string& reseau) {
  const Pt d = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
  const Pt bout{b.p.x - d.x * 2'540'000, b.p.y - d.y * 2'540'000};
  c.feuilles[feuille].fils.push_back({nouvelUuid(), b.p, bout});
  Etiquette e;
  e.uuid = nouvelUuid();
  e.texte = reseau;
  e.p = bout;
  e.type = TypeEtiquette::Globale;
  c.feuilles[feuille].etiquettes.push_back(e);
}
// Pose et relie (numéro de broche → réseau) ; rend l'UUID du composant
std::string poserRelie(ConceptionElec& c, std::size_t feuille, const std::string& symbole, Pt p, const std::string& ref, const std::string& valeur,
                       const std::map<std::string, std::string>& liaisons) {
  const std::string s = poserSymbole(c, feuille, symbole, p);
  for (auto& sp : c.feuilles[feuille].symboles) {
    if (sp.uuid != s) continue;
    if (ComposantElec* k = c.composant(sp.composant)) {
      k->reference = ref;
      k->valeur = valeur;
    }
    for (const auto& b : brochesPlacees(sp, *c.symbole(sp.symbole))) {
      const auto it = liaisons.find(b.broche->numero);
      if (it != liaisons.end()) etiqueter(c, feuille, b, it->second);
    }
    return sp.composant;
  }
  return {};
}
// Réseau d'une broche du microcontrôleur ; sans réseau nommé, une étiquette lui donne « nom »
std::string reseauDe(ConceptionElec& c, const ComposantElec& mcu, const std::string& numero, const std::string& nom) {
  const Connectivite k = connectivite(c);
  const int r = k.reseau(mcu.uuid, numero);
  if (r >= 0 && (k.reseaux[static_cast<std::size_t>(r)].nomme || k.reseaux[static_cast<std::size_t>(r)].broches.size() > 1)) return k.reseaux[static_cast<std::size_t>(r)].nom;
  for (std::size_t f = 0; f < c.feuilles.size(); ++f)
    for (const auto& sp : c.feuilles[f].symboles)
      if (sp.composant == mcu.uuid)
        for (const auto& b : brochesPlacees(sp, *c.symbole(sp.symbole)))
          if (b.broche->numero == numero) {
            etiqueter(c, f, b, nom);
            return nom;
          }
  return nom;
}
}  // namespace

std::vector<std::string> poserModuleEmbarque(ConceptionElec& c, const std::string& module, const std::string& micro, std::string* erreur) {
  std::vector<std::string> poses;
  auto refuser = [&](const std::string& e) {
    if (erreur) *erreur = e;
    return poses;
  };
  const ModuleEmbarque* m = moduleEmbarque(module);
  if (!m) return refuser("module « " + module + " » absent de la galerie");
  const ComposantElec* mcu0 = c.parReference(micro);
  if (!mcu0) return refuser("microcontrôleur « " + micro + " » absent de la conception");
  const ComposantElec mcu = *mcu0;
  const DefSymbole* d = c.symbole(mcu.symbole);
  if (!d) return refuser("symbole du microcontrôleur introuvable");
  // Feuille et place : à droite du microcontrôleur
  std::size_t feuille = 0;
  Pt pos = ptMm(150, 60);
  for (std::size_t f = 0; f < c.feuilles.size(); ++f)
    for (const auto& sp : c.feuilles[f].symboles)
      if (sp.composant == mcu.uuid) {
        feuille = f;
        pos = Pt{sp.position.x + nm(60), sp.position.y};
      }
  // Alimentations : celles du microcontrôleur
  std::string vcc = "+5V", gnd = "GND";
  {
    const Connectivite k = connectivite(c);
    for (const auto& b : d->broches) {
      const int r = k.reseau(mcu.uuid, b.numero);
      if (r < 0) continue;
      const std::string& n = k.reseaux[static_cast<std::size_t>(r)].nom;
      if (b.nom == "VCC" || b.nom == "AVCC") vcc = n;
      if (b.nom == "GND") gnd = n;
    }
  }
  std::map<std::string, std::string> liaisons;
  if (m->interface == "i2c") {
    const std::string nSda = numeroDuPort(*d, "PC4"), nScl = numeroDuPort(*d, "PC5");
    if (nSda.empty() || nScl.empty()) return refuser("le microcontrôleur n'a pas de broches SDA et SCL connues");
    const std::string sda = reseauDe(c, mcu, nSda, "SDA"), scl = reseauDe(c, mcu, nScl, "SCL");
    for (const auto& [n, nom] : m->broches) liaisons[n] = nom == "VCC" ? vcc : nom == "GND" ? gnd : nom == "SDA" ? sda : nom == "SCL" ? scl : std::string();
    const std::string id = "galerie:" + m->nom;
    if (!c.symboles.count(id)) c.symboles[id] = symboleModuleEmbarque(*m);
    const std::string u = poserRelie(c, feuille, id, pos, prochainRepere(c, "U"), m->nom, liaisons);
    if (ComposantElec* k = c.composant(u)) {
      k->champs["Module"] = m->id;
      k->champs["I2C.Adresse"] = "0x" + [&] {
        std::ostringstream o;
        o << std::hex << m->adresse;
        return o.str();
      }();
      poses.push_back(k->reference);
    }
    // Tirages s'il n'y en a pas
    bool tirageSda = false, tirageScl = false;
    for (const auto& x : raccordements(c, mcu.uuid, nSda)) tirageSda |= estTirage(x);
    for (const auto& x : raccordements(c, mcu.uuid, nScl)) tirageScl |= estTirage(x);
    double y = 25.4;
    for (const auto& [besoin, reseau] : {std::pair{!tirageSda, sda}, std::pair{!tirageScl, scl}}) {
      if (!besoin) continue;
      const std::string ref = prochainRepere(c, "R");
      poserRelie(c, feuille, "R", Pt{pos.x + nm(30), pos.y + nm(y)}, ref, "4k7", {{"1", vcc}, {"2", reseau}});
      poses.push_back(ref);
      y += 15.24;
    }
  }
  return poses;
}

std::vector<const ModuleEmbarque*> modulesDeLaConception(const ConceptionElec& c) {
  std::vector<const ModuleEmbarque*> v;
  std::set<std::string> vus;
  for (const auto& x : c.composants) {
    const auto it = x.champs.find("Module");
    if (it == x.champs.end() || !vus.insert(it->second).second) continue;
    if (const ModuleEmbarque* m = moduleEmbarque(it->second)) v.push_back(m);
  }
  return v;
}

// ======================================================================================================================
// Organigrammes
// ======================================================================================================================
const BlocOrganigramme* Organigramme::bloc(const std::string& id) const {
  for (const auto& b : blocs)
    if (b.id == id) return &b;
  return nullptr;
}
BlocOrganigramme* Organigramme::bloc(const std::string& id) {
  for (auto& b : blocs)
    if (b.id == id) return &b;
  return nullptr;
}
std::string Organigramme::debut() const {
  for (const auto& b : blocs)
    if (b.genre == "début") return b.id;
  return blocs.empty() ? std::string() : blocs.front().id;
}

std::vector<Organigramme> organigrammesDe(const ConceptionElec& c) {
  std::vector<Organigramme> v;
  const auto it = c.extensions.find("organigrammes");
  if (it == c.extensions.end() || !it->second.is_array()) return v;
  for (const auto& j : it->second) {
    Organigramme o;
    o.nom = j.value("nom", std::string("Programme"));
    for (const auto& b : j.value("blocs", nlohmann::ordered_json::array())) {
      BlocOrganigramme x;
      x.id = b.value("id", std::string());
      x.genre = b.value("genre", std::string("action"));
      x.texte = b.value("texte", std::string());
      x.suivant = b.value("suivant", std::string());
      x.siVrai = b.value("siVrai", std::string());
      x.siFaux = b.value("siFaux", std::string());
      x.x = b.value("x", 0.0);
      x.y = b.value("y", 0.0);
      if (b.contains("parametres") && b["parametres"].is_object())
        for (const auto& [k, val] : b["parametres"].items()) x.parametres[k] = val.is_string() ? val.get<std::string>() : val.dump();
      o.blocs.push_back(x);
    }
    v.push_back(o);
  }
  return v;
}

void ecrireOrganigrammes(ConceptionElec& c, const std::vector<Organigramme>& o) {
  if (o.empty()) {
    c.extensions.erase("organigrammes");
    return;
  }
  nlohmann::ordered_json a = nlohmann::ordered_json::array();
  for (const auto& x : o) {
    nlohmann::ordered_json bl = nlohmann::ordered_json::array();
    for (const auto& b : x.blocs) {
      nlohmann::ordered_json j = {{"id", b.id}, {"genre", b.genre}, {"texte", b.texte}};
      if (!b.parametres.empty()) j["parametres"] = b.parametres;
      if (!b.suivant.empty()) j["suivant"] = b.suivant;
      if (!b.siVrai.empty()) j["siVrai"] = b.siVrai;
      if (!b.siFaux.empty()) j["siFaux"] = b.siFaux;
      j["x"] = b.x;
      j["y"] = b.y;
      bl.push_back(j);
    }
    a.push_back({{"nom", x.nom}, {"blocs", bl}});
  }
  c.extensions["organigrammes"] = a;
}

std::vector<BlocPropose> blocsProposes(const ConceptionElec& c, const std::string& debut) {
  std::vector<BlocPropose> v;
  for (const ModuleEmbarque* m : modulesDeLaConception(c))
    for (const auto& b : m->blocs) v.push_back({"module", b.texte, m->id, b.id, {{"variable", b.variable}}});
  v.push_back({"action", "allumer une broche", {}, {}, {{"action", "ecrire"}, {"broche", "13"}, {"niveau", "HIGH"}}});
  v.push_back({"action", "éteindre une broche", {}, {}, {{"action", "ecrire"}, {"broche", "13"}, {"niveau", "LOW"}}});
  v.push_back({"action", "lire une broche", {}, {}, {{"action", "lire"}, {"broche", "2"}, {"variable", "etat"}}});
  v.push_back({"action", "écrire sur la liaison série", {}, {}, {{"action", "serie"}, {"texte", "bonjour"}}});
  v.push_back({"attente", "attendre", {}, {}, {{"duree", "1000"}}});
  v.push_back({"décision", "si", {}, {}, {{"condition", "etat == HIGH"}}});
  v.push_back({"boucle", "répéter", {}, {}, {{"fois", "10"}}});
  if (debut.empty()) return v;
  const std::string d = pourComparer(debut);
  std::vector<BlocPropose> f;
  for (const auto& b : v) {
    const std::string t = pourComparer(b.texte);
    // Début du texte ou d'un de ses mots (« temp » → « lire la température »)
    bool ok = t.rfind(d, 0) == 0;
    for (std::size_t i = t.find(' '); !ok && i != std::string::npos; i = t.find(' ', i + 1)) ok = t.compare(i + 1, d.size(), d) == 0;
    if (ok) f.push_back(b);
  }
  return f;
}

std::string CodeOrganigramme::blocDeLigne(int ligne) const {
  std::string meilleur;
  int etendue = 1 << 30;
  for (const auto& [id, l] : lignes)
    if (ligne >= l.first && ligne <= l.second && l.second - l.first < etendue) {
      meilleur = id;
      etendue = l.second - l.first;
    }
  return meilleur;
}

namespace {
struct Ecrivain {
  std::vector<std::string> lignes;
  std::map<std::string, std::pair<int, int>> parBloc;
  std::vector<std::string> erreurs;
  void ligne(const std::string& t, const std::string& bloc = {}) {
    lignes.push_back(t);
    if (bloc.empty()) return;
    const int n = static_cast<int>(lignes.size());
    auto it = parBloc.find(bloc);
    if (it == parBloc.end()) parBloc[bloc] = {n, n};
    else it->second.second = n;
  }
};
std::string remplacer(std::string t, const std::string& de, const std::string& par) {
  for (std::size_t i = t.find(de); i != std::string::npos; i = t.find(de, i + par.size())) t.replace(i, de.size(), par);
  return t;
}
std::string param(const BlocOrganigramme& b, const std::string& k, const std::string& defaut = {}) {
  const auto it = b.parametres.find(k);
  return it == b.parametres.end() || it->second.empty() ? defaut : it->second;
}
// Chaîne de blocs à partir de « id », jusqu'à « arret » (exclu) ou la fin
void ecrireChaine(const Organigramme& o, const std::string& id0, const std::string& arret, Ecrivain& e, int indent, bool python, std::set<std::string>& pile) {
  std::string id = id0;
  const std::string pas(static_cast<std::size_t>(indent) * (python ? 4 : 2), ' ');
  int garde = 0;
  while (!id.empty() && id != arret && ++garde < 1000) {
    const BlocOrganigramme* b = o.bloc(id);
    if (!b) {
      e.erreurs.push_back("bloc « " + id + " » introuvable");
      return;
    }
    if (b->genre == "fin" || b->genre == "principal") return;
    if (!pile.insert(b->id).second && b->genre != "boucle") {
      e.erreurs.push_back("boucle sans bloc « répéter » au bloc « " + b->texte + " »");
      return;
    }
    const std::string fin = python ? "" : ";";
    if (b->genre == "action") {
      const std::string a = param(*b, "action", "code");
      std::string br = param(*b, "broche", "13");
      if (a == "ecrire") {
        const std::string niv = param(*b, "niveau", "HIGH");
        const bool haut = niv == "HIGH" || niv == "1" || pourComparer(niv) == "haut";
        e.ligne(python ? pas + "Pin(" + br + ", Pin.OUT).value(" + (haut ? "1" : "0") + ")" : pas + "digitalWrite(" + br + ", " + (haut ? "HIGH" : "LOW") + ");", b->id);
      } else if (a == "lire")
        e.ligne(python ? pas + param(*b, "variable", "etat") + " = Pin(" + br + ", Pin.IN).value()" : pas + param(*b, "variable", "etat") + " = digitalRead(" + br + ");", b->id);
      else if (a == "serie") {
        const std::string var = param(*b, "variable"), txt = param(*b, "texte");
        if (python) e.ligne(pas + "print(" + (var.empty() ? "\"" + txt + "\"" : (txt.empty() ? var : "\"" + txt + "\", " + var)) + ")", b->id);
        else if (var.empty()) e.ligne(pas + "Serial.println(\"" + txt + "\");", b->id);
        else {
          if (!txt.empty()) e.ligne(pas + "Serial.print(\"" + txt + " \");", b->id);
          e.ligne(pas + "Serial.println(" + var + ");", b->id);
        }
      } else if (a == "variable")
        e.ligne(pas + param(*b, "variable", "x") + " = " + param(*b, "expression", "0") + fin, b->id);
      else
        e.ligne(pas + param(*b, "code", python ? "pass" : ";"), b->id);
      id = b->suivant;
    } else if (b->genre == "attente") {
      e.ligne(python ? pas + "time.sleep_ms(" + param(*b, "duree", "1000") + ")" : pas + "delay(" + param(*b, "duree", "1000") + ");", b->id);
      id = b->suivant;
    } else if (b->genre == "module") {
      const ModuleEmbarque* m = moduleEmbarque(param(*b, "module"));
      const BlocModule* bm = nullptr;
      if (m)
        for (const auto& x : m->blocs)
          if (x.id == param(*b, "bloc")) bm = &x;
      if (!bm) {
        e.erreurs.push_back("bloc de module « " + b->texte + " » inconnu");
        return;
      }
      e.ligne(pas + remplacer(python ? bm->codePython : bm->code, "{variable}", param(*b, "variable", bm->variable)), b->id);
      id = b->suivant;
    } else if (b->genre == "décision") {
      const std::string cond = param(*b, "condition", "true");
      e.ligne(python ? pas + "if " + cond + ":" : pas + "if (" + cond + ") {", b->id);
      const std::size_t avant = e.lignes.size();
      ecrireChaine(o, b->siVrai, b->suivant, e, indent + 1, python, pile);
      if (python && e.lignes.size() == avant) e.ligne(pas + "    pass", b->id);
      if (!b->siFaux.empty() && b->siFaux != b->suivant) {
        e.ligne(python ? pas + "else:" : pas + "} else {", b->id);
        const std::size_t avant2 = e.lignes.size();
        ecrireChaine(o, b->siFaux, b->suivant, e, indent + 1, python, pile);
        if (python && e.lignes.size() == avant2) e.ligne(pas + "    pass", b->id);
      }
      if (!python) e.ligne(pas + "}", b->id);
      id = b->suivant;
    } else if (b->genre == "boucle") {
      const std::string fois = param(*b, "fois"), cond = param(*b, "condition");
      if (!cond.empty()) e.ligne(python ? pas + "while " + cond + ":" : pas + "while (" + cond + ") {", b->id);
      else e.ligne(python ? pas + "for _ in range(" + (fois.empty() ? "1" : fois) + "):" : pas + "for (int i = 0; i < " + (fois.empty() ? "1" : fois) + "; ++i) {", b->id);
      const std::size_t avant = e.lignes.size();
      std::set<std::string> pileCorps = pile;
      ecrireChaine(o, b->siVrai, b->id, e, indent + 1, python, pileCorps);
      if (python && e.lignes.size() == avant) e.ligne(pas + "    pass", b->id);
      if (!python) e.ligne(pas + "}", b->id);
      id = b->suivant;
    } else
      id = b->suivant;  // début et blocs sans code
  }
}
// Variables des blocs (lire une broche, blocs de module, affectations), déclarées en tête
std::map<std::string, std::string> variablesDe(const Organigramme& o) {
  std::map<std::string, std::string> v;
  for (const auto& b : o.blocs) {
    if (b.genre == "module")
      if (const ModuleEmbarque* m = moduleEmbarque(param(b, "module")))
        for (const auto& x : m->blocs)
          if (x.id == param(b, "bloc")) v[param(b, "variable", x.variable)] = x.type;
    if (b.genre == "action" && param(b, "action") == "lire") v[param(b, "variable", "etat")] = "int";
    if (b.genre == "action" && param(b, "action") == "variable") v.emplace(param(b, "variable", "x"), param(b, "type", "long"));
  }
  return v;
}
CodeOrganigramme convertir(const Organigramme& o, const ConceptionElec& c, bool python) {
  CodeOrganigramme r;
  Ecrivain e;
  // Modules utilisés : ceux des blocs, et ceux posés au schéma
  std::vector<const ModuleEmbarque*> modules;
  std::set<std::string> vus;
  for (const auto& b : o.blocs)
    if (b.genre == "module")
      if (const ModuleEmbarque* m = moduleEmbarque(param(b, "module")); m && vus.insert(m->id).second) modules.push_back(m);
  (void)c;
  bool serie = false;
  std::set<std::string> sorties, entrees;
  for (const auto& b : o.blocs) {
    if (b.genre != "action") continue;
    const std::string a = param(b, "action");
    serie |= a == "serie";
    if (a == "ecrire") sorties.insert(param(b, "broche", "13"));
    if (a == "lire") entrees.insert(param(b, "broche", "2"));
  }
  const std::string debut = o.debut();
  std::string principal;
  for (const auto& b : o.blocs)
    if (b.genre == "principal") principal = b.id;
  const auto variables = variablesDe(o);
  if (python) {
    e.ligne("# " + o.nom + " : écrit par Prométhée d'après l'organigramme");
    e.ligne("from machine import Pin, I2C");
    e.ligne("import time");
    for (const auto* m : modules) e.ligne(m->inclusionPython);
    for (const auto* m : modules) e.ligne(m->initialisationPython);
    for (const auto& [v, t] : variables) e.ligne(v + " = 0");
    std::set<std::string> pile;
    ecrireChaine(o, debut, principal, e, 0, true, pile);
    if (!principal.empty()) {
      e.ligne("while True:", principal);
      const std::size_t avant = e.lignes.size();
      std::set<std::string> pileBoucle;
      ecrireChaine(o, o.bloc(principal)->suivant, principal, e, 1, true, pileBoucle);
      if (e.lignes.size() == avant) e.ligne("    pass", principal);
    }
  } else {
    e.ligne("// " + o.nom + " : écrit par Prométhée d'après l'organigramme");
    for (const auto* m : modules) {
      if (m->interface == "i2c") e.ligne("#include \"Wire.h\"");
      e.ligne(m->inclusion);
    }
    for (const auto* m : modules) e.ligne(m->declaration);
    for (const auto& [v, t] : variables) e.ligne(t + " " + v + " = 0;");
    e.ligne("");
    e.ligne("void setup() {", debut);
    if (serie) e.ligne("  Serial.begin(9600);");
    for (const auto& s : sorties) e.ligne("  pinMode(" + s + ", OUTPUT);");
    for (const auto& s : entrees) e.ligne("  pinMode(" + s + ", INPUT);");
    for (const auto* m : modules) e.ligne("  " + m->initialisation);
    std::set<std::string> pile;
    ecrireChaine(o, debut, principal, e, 1, false, pile);
    e.ligne("}", debut);
    e.ligne("");
    e.ligne("void loop() {", principal);
    if (!principal.empty()) {
      std::set<std::string> pileBoucle;
      ecrireChaine(o, o.bloc(principal)->suivant, principal, e, 1, false, pileBoucle);
    }
    e.ligne("}", principal);
  }
  for (const auto* m : modules)
    for (const auto& [nom, texte] : m->pilote) {
      const bool py = nom.size() > 3 && nom.compare(nom.size() - 3, 3, ".py") == 0;
      if (py == python) r.fichiers[nom] = texte;
    }
  std::ostringstream t;
  for (const auto& l : e.lignes) t << l << "\n";
  r.code = t.str();
  r.lignes = e.parBloc;
  r.erreurs = e.erreurs;
  r.ok = e.erreurs.empty();
  return r;
}
}  // namespace

CodeOrganigramme versArduino(const Organigramme& o, const ConceptionElec& c) { return convertir(o, c, false); }
CodeOrganigramme versPython(const Organigramme& o, const ConceptionElec& c) { return convertir(o, c, true); }

Organigramme organigrammeExemple(const std::string& module, const std::string& broche) {
  Organigramme o;
  o.nom = "Thermomètre";
  const ModuleEmbarque* m = moduleEmbarque(module);
  const std::string bloc = m && !m->blocs.empty() ? m->blocs.front().id : std::string();
  const std::string var = m && !m->blocs.empty() ? m->blocs.front().variable : std::string("valeur");
  auto ajouter = [&](BlocOrganigramme b, double y) {
    b.x = 40;
    b.y = y;
    o.blocs.push_back(b);
  };
  ajouter({"debut", "début", "début", {}, "principal", {}, {}, 0, 0}, 10);
  ajouter({"principal", "principal", "toujours", {}, "lire", {}, {}, 0, 0}, 25);
  ajouter({"lire", "module", m && !m->blocs.empty() ? m->blocs.front().texte : "lire", {{"module", module}, {"bloc", bloc}, {"variable", var}}, "envoyer", {}, {}, 0, 0}, 40);
  ajouter({"envoyer", "action", "écrire sur la liaison série", {{"action", "serie"}, {"texte", var}, {"variable", var}}, "seuil", {}, {}, 0, 0}, 55);
  ajouter({"seuil", "décision", "si " + var + " > 25", {{"condition", var + " > 25"}}, "attendre", "allumer", "eteindre", 0, 0}, 70);
  ajouter({"allumer", "action", "allumer la LED", {{"action", "ecrire"}, {"broche", broche}, {"niveau", "HIGH"}}, "attendre", {}, {}, 0, 0}, 85);
  ajouter({"eteindre", "action", "éteindre la LED", {{"action", "ecrire"}, {"broche", broche}, {"niveau", "LOW"}}, "attendre", {}, {}, 0, 0}, 85);
  ajouter({"attendre", "attente", "attendre 1 s", {{"duree", "1000"}}, "principal", {}, {}, 0, 0}, 100);
  return o;
}

}  // namespace prom::ecad

// Prométhée : microcontrôleur simulé (lot L15 de la 0.34 ; 15.4, 15.5, 15.8). Voir micro_simulation.hpp.
// Licence GPL-3.0-only.
#include "promethee/micro_simulation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <numbers>
#include <sstream>

#include "promethee/embarque.hpp"
#include "promethee/fichiers.hpp"

#ifdef PROMETHEE_SIMAVR
extern "C" {
#include <simavr/avr_adc.h>
#include <simavr/avr_ioport.h>
#include <simavr/avr_uart.h>
#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
}
#endif

namespace prom::ecad {

namespace {
std::string minuscules(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string nb(double v, int p = 6) {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o.precision(p);
  o << v;
  return o.str();
}
std::vector<std::string> lignesDe(const std::string& t) {
  std::vector<std::string> v;
  std::istringstream in(t);
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    v.push_back(l);
  }
  return v;
}
std::string hex2(unsigned v) {
  static const char* c = "0123456789ABCDEF";
  return std::string{c[(v >> 4) & 15], c[v & 15]};
}
}  // namespace

std::string microDuSymbole(const std::string& symbole) {
  const std::string s = minuscules(symbole);
  for (const char* m : {"atmega328p", "atmega328", "atmega168", "atmega88", "atmega48", "atmega2560", "atmega32u4", "attiny85", "attiny84", "atmega644"})
    if (s.find(m) != std::string::npos) return m;
  return {};
}

// ======================================================================================================================
// simavr
// ======================================================================================================================
#ifdef PROMETHEE_SIMAVR
namespace {
// simavr écrit ses messages sur la sortie d'erreurs : gardés pour le journal, sans rien écrire
void journalSimavr(avr_t*, const int, const char*, va_list) {}
// Le sommeil n'attend pas en temps réel : seul le compteur de cycles avance
void dormir(avr_t*, avr_cycle_count_t) {}

class EmulateurSimavr : public EmulateurMicro {
 public:
  EmulateurSimavr(std::string micro, double f) : m_micro(std::move(micro)), m_f(f) {
    static const bool silence = [] {
      avr_global_logger_set(journalSimavr);
      return true;
    }();
    (void)silence;
  }
  ~EmulateurSimavr() override {
    if (m_avr) avr_terminate(m_avr);
  }
  std::string nom() const override { return "simavr (" + m_micro + ")"; }
  bool charger(const std::string& elf, std::string* erreur) override {
    auto refuser = [&](const std::string& e) {
      if (erreur) *erreur = e;
      return false;
    };
    elf_firmware_t fw{};
    if (elf_read_firmware(elf.c_str(), &fw) != 0) return refuser("programme illisible : " + elf);
    if (fw.mmcu[0] == 0) std::snprintf(fw.mmcu, sizeof fw.mmcu, "%s", m_micro.c_str());
    fw.frequency = static_cast<uint32_t>(m_f);
    m_avr = avr_make_mcu_by_name(fw.mmcu);
    if (!m_avr) return refuser("simavr ne connaît pas le microcontrôleur « " + std::string(fw.mmcu) + " »");
    if (avr_init(m_avr) != 0) return refuser("simavr n'a pas pu préparer le microcontrôleur");
    m_avr->frequency = static_cast<uint32_t>(m_f);
    m_avr->vcc = m_avr->avcc = m_avr->aref = 5000;
    avr_load_firmware(m_avr, &fw);
    m_avr->frequency = static_cast<uint32_t>(m_f);
    m_avr->sleep = dormir;
    m_avr->log = 0;
    // USART0 : sans écriture sur la console, octets émis relevés
    uint32_t drapeaux = 0;
    avr_ioctl(m_avr, AVR_IOCTL_UART_GET_FLAGS('0'), &drapeaux);
    drapeaux &= ~static_cast<uint32_t>(AVR_UART_FLAG_STDIO);
    avr_ioctl(m_avr, AVR_IOCTL_UART_SET_FLAGS('0'), &drapeaux);
    if (avr_irq_t* irq = avr_io_getirq(m_avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT)) avr_irq_register_notify(irq, surOctet, this);
    return true;
  }
  void avancer(double jusqua) override {
    arreteSurPoint = false;
    if (!m_avr || m_termine) return;
    const auto cible = static_cast<avr_cycle_count_t>(std::max(0.0, std::floor(jusqua * m_f)));
    while (m_avr->cycle < cible) {
      if (!reprise && !pointsArret.empty() && pointsArret.count(m_avr->pc)) {
        arreteSurPoint = true;
        return;
      }
      reprise = false;
      const int s = avr_run(m_avr);
      if (s == cpu_Done || s == cpu_Crashed) {
        m_termine = true;
        return;
      }
    }
  }
  void pas() override {
    if (!m_avr || m_termine) return;
    reprise = false;
    const int s = avr_run(m_avr);
    if (s == cpu_Done || s == cpu_Crashed) m_termine = true;
  }
  double temps() const override { return m_avr ? static_cast<double>(m_avr->cycle) / m_f : 0; }
  double frequence() const override { return m_f; }
  std::uint64_t cycles() const override { return m_avr ? m_avr->cycle : 0; }
  EtatBroche broche(const std::string& port) const override {
    if (!m_avr || port.size() != 3) return EtatBroche::Entree;
    avr_ioport_state_t e{};
    if (avr_ioctl(m_avr, AVR_IOCTL_IOPORT_GETSTATE(port[1]), &e) != 0) return EtatBroche::Entree;
    const unsigned b = 1u << (port[2] - '0');
    if (e.ddr & b) return (e.port & b) ? EtatBroche::Haut : EtatBroche::Bas;
    return (e.port & b) ? EtatBroche::Tirage : EtatBroche::Entree;
  }
  void entree(const std::string& port, bool niveau) override {
    if (!m_avr || port.size() != 3) return;
    if (avr_irq_t* irq = avr_io_getirq(m_avr, AVR_IOCTL_IOPORT_GETIRQ(port[1]), port[2] - '0')) avr_raise_irq(irq, niveau ? 1 : 0);
  }
  void analogique(int voie, double volts) override {
    if (!m_avr || voie < 0 || voie > 7) return;
    if (avr_irq_t* irq = avr_io_getirq(m_avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0 + voie)) avr_raise_irq(irq, static_cast<uint32_t>(std::clamp(volts, 0.0, 6.0) * 1000));
  }
  void alimentation(double volts) override {
    if (!m_avr) return;
    const auto mv = static_cast<uint32_t>(std::clamp(volts, 0.0, 6.0) * 1000);
    m_avr->vcc = m_avr->avcc = m_avr->aref = mv;
  }
  void envoyerSerie(std::uint8_t octet) override {
    if (!m_avr) return;
    if (avr_irq_t* irq = avr_io_getirq(m_avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT)) avr_raise_irq(irq, octet);
  }
  FormatSerie formatSerie() const override {
    FormatSerie f;
    if (!m_avr) return f;
    const unsigned a = memoire(0xC0), b = memoire(0xC1), c = memoire(0xC2);
    const unsigned ubrr = memoire(0xC4) | ((memoire(0xC5) & 0x0F) << 8);
    f.emission = b & (1u << 3);
    f.reception = b & (1u << 4);
    if (f.emission || f.reception) f.debit = static_cast<int>(std::lround(m_f / ((a & 2u) ? 8.0 : 16.0) / (ubrr + 1)));
    f.bits = 5 + static_cast<int>((c >> 1) & 3u) + ((b & (1u << 2)) ? 4 : 0);
    const unsigned upm = (c >> 4) & 3u;
    f.parite = upm == 2 ? 'E' : upm == 3 ? 'O' : 'N';
    f.arret = (c & (1u << 3)) ? 2 : 1;
    return f;
  }
  double rapportCyclique(const std::string& port) const override {
    // Sorties des temporisateurs de l'ATmega328P : registre de contrôle, bit COMx1, registre de comparaison
    struct Sortie {
      const char* port;
      unsigned tccr, bit, ocr;
    };
    static const Sortie s[] = {{"PD6", 0x44, 7, 0x47}, {"PD5", 0x44, 5, 0x48}, {"PB1", 0x80, 7, 0x88}, {"PB2", 0x80, 5, 0x8A}, {"PB3", 0xB0, 7, 0xB3}, {"PD3", 0xB0, 5, 0xB4}};
    for (const auto& x : s)
      if (port == x.port && (memoire(x.tccr) & (1u << x.bit))) return memoire(x.ocr) / 255.0;
    return broche(port) == EtatBroche::Haut ? 1.0 : 0.0;
  }
  std::uint32_t pc() const override { return m_avr ? m_avr->pc : 0; }
  std::vector<std::uint8_t> registres() const override {
    std::vector<std::uint8_t> r(32, 0);
    if (m_avr) std::copy(m_avr->data, m_avr->data + 32, r.begin());
    return r;
  }
  std::uint8_t sreg() const override {
    if (!m_avr) return 0;
    std::uint8_t v = 0;
    for (int i = 0; i < 8; ++i) v = static_cast<std::uint8_t>(v | ((m_avr->sreg[i] ? 1 : 0) << i));
    return v;
  }
  std::uint16_t sp() const override { return m_avr ? static_cast<std::uint16_t>(m_avr->data[R_SPL] | (m_avr->data[R_SPH] << 8)) : 0; }
  std::uint8_t memoire(std::uint32_t a) const override { return m_avr && a <= m_avr->ramend ? m_avr->data[a] : 0; }
  void ecrireMemoire(std::uint32_t a, std::uint8_t v) override {
    if (m_avr && a <= m_avr->ramend) m_avr->data[a] = v;
  }
  bool termine() const override { return m_termine; }

 private:
  static void surOctet(avr_irq_t*, uint32_t valeur, void* param) {
    auto* e = static_cast<EmulateurSimavr*>(param);
    if (e->surSerie) e->surSerie(static_cast<std::uint8_t>(valeur), e->temps());
  }
  std::string m_micro;
  double m_f;
  avr_t* m_avr = nullptr;
  bool m_termine = false;
};
}  // namespace
#endif

bool emulateurDisponible(const std::string& micro) {
#ifdef PROMETHEE_SIMAVR
  const std::string m = minuscules(micro);
  return m.rfind("atmega", 0) == 0 || m.rfind("attiny", 0) == 0;
#else
  (void)micro;
  return false;
#endif
}

std::unique_ptr<EmulateurMicro> creerEmulateur(const std::string& micro, double frequence, std::string* erreur) {
#ifdef PROMETHEE_SIMAVR
  if (emulateurDisponible(micro)) return std::make_unique<EmulateurSimavr>(minuscules(micro), frequence > 0 ? frequence : 16e6);
  if (erreur) *erreur = "pas d'émulateur pour « " + micro + " » (simavr émule les AVR)";
#else
  (void)micro;
  (void)frequence;
  if (erreur) *erreur = "Prométhée a été construit sans simavr : la co-simulation du microcontrôleur n'est pas disponible";
#endif
  return nullptr;
}

// ======================================================================================================================
// Table des lignes et symboles
// ======================================================================================================================
TableLignes lireTableLignes(const std::string& sortie) {
  TableLignes t;
  std::map<std::uint32_t, LigneSource> parAdresse;  // la dernière ligne d'une adresse l'emporte
  // Désassemblage annoté (« avr-objdump -d -l ») : « chemin/fichier.c:12 », puis les instructions « 80: … »
  std::optional<LigneSource> enAttente;
  for (const auto& l : lignesDe(sortie)) {
    const std::size_t dp = l.rfind(':');
    if (!l.empty() && l[0] != ' ' && dp != std::string::npos && dp + 1 < l.size() && l.find('\t') == std::string::npos) {
      std::string n = l.substr(dp + 1);
      if (const std::size_t sp = n.find(' '); sp != std::string::npos) n = n.substr(0, sp);  // « (discriminator 1) »
      if (!n.empty() && std::all_of(n.begin(), n.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        LigneSource s;
        s.fichier = utf8(cheminFichier(l.substr(0, dp)).filename());
        s.ligne = std::stoi(n);
        enAttente = s;
        continue;
      }
    }
    if (enAttente && l.size() > 2 && l[0] == ' ') {
      const std::size_t c = l.find(':');
      if (c != std::string::npos && c > 0) {
        std::string a = l.substr(0, c);
        a.erase(0, a.find_first_not_of(' '));
        if (!a.empty() && std::all_of(a.begin(), a.end(), [](char x) { return std::isxdigit(static_cast<unsigned char>(x)); })) {
          enAttente->adresse = static_cast<std::uint32_t>(std::stoul(a, nullptr, 16));
          parAdresse[enAttente->adresse] = *enAttente;
          enAttente.reset();
          continue;
        }
      }
    }
  }
  // Table décodée (« --dwarf=decodedline ») : « fichier ligne adresse »
  for (const auto& l : lignesDe(sortie)) {
    std::istringstream in(l);
    std::string fichier, ligne, adresse;
    if (!(in >> fichier >> ligne >> adresse)) continue;
    if (ligne.empty() || !std::all_of(ligne.begin(), ligne.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) continue;
    if (adresse.rfind("0x", 0) != 0) continue;
    LigneSource s;
    s.fichier = fichier;
    s.ligne = std::stoi(ligne);
    s.adresse = static_cast<std::uint32_t>(std::stoul(adresse, nullptr, 16));
    parAdresse[s.adresse] = s;
  }
  for (const auto& [a, s] : parAdresse) t.lignes.push_back(s);
  return t;
}

TableLignes tableLignes(const std::string& elf, std::string* erreur) {
  const ResultatProcessus r = executerProcessus({"avr-objdump", "--dwarf=decodedline", elf}, {.dossier = {}, .entree = {}, .delai = 60, .environnement = {}});
  if (!r.reussi()) {
    if (erreur) *erreur = r.lance ? r.erreurs : "avr-objdump manque sur ce poste";
    return {};
  }
  TableLignes t = lireTableLignes(r.sortie);
  if (!t.lignes.empty()) return t;
  // Les anciennes versions de binutils ne décodent pas la table : le désassemblage annoté des lignes
  const ResultatProcessus d = executerProcessus({"avr-objdump", "-d", "-l", elf}, {.dossier = {}, .entree = {}, .delai = 60, .environnement = {}});
  if (d.reussi()) t = lireTableLignes(d.sortie);
  if (t.lignes.empty() && erreur) *erreur = "table des lignes vide : programme compilé sans « -g » ?";
  return t;
}

std::optional<std::uint32_t> TableLignes::adresse(const std::string& fichier, int ligne) const {
  const std::string f = utf8(cheminFichier(fichier).filename());
  std::optional<std::uint32_t> meilleure;
  int ligneMeilleure = 1 << 30;
  for (const auto& s : lignes) {
    if (s.fichier != f || s.ligne < ligne) continue;
    if (s.ligne < ligneMeilleure || (s.ligne == ligneMeilleure && meilleure && s.adresse < *meilleure)) {
      ligneMeilleure = s.ligne;
      meilleure = s.adresse;
    }
  }
  return meilleure;
}

std::optional<LigneSource> TableLignes::ligneDe(std::uint32_t adresse) const {
  const LigneSource* r = nullptr;
  for (const auto& s : lignes) {
    if (s.adresse > adresse) break;
    r = &s;
  }
  if (!r) return std::nullopt;
  return *r;
}

std::vector<SymboleElf> symbolesElf(const std::string& elf) {
  std::vector<SymboleElf> v;
  const ResultatProcessus r = executerProcessus({"avr-nm", "-S", "-C", elf}, {.dossier = {}, .entree = {}, .delai = 60, .environnement = {}});
  if (!r.reussi()) return v;
  for (const auto& l : lignesDe(r.sortie)) {
    std::istringstream in(l);
    std::vector<std::string> m;
    for (std::string x; in >> x;) m.push_back(x);
    SymboleElf s;
    if (m.size() == 4) {
      s.adresse = static_cast<std::uint32_t>(std::stoul(m[0], nullptr, 16));
      s.taille = static_cast<std::uint32_t>(std::stoul(m[1], nullptr, 16));
      s.genre = m[2][0];
      s.nom = m[3];
    } else if (m.size() == 3) {
      s.adresse = static_cast<std::uint32_t>(std::stoul(m[0], nullptr, 16));
      s.genre = m[1][0];
      s.nom = m[2];
    } else
      continue;
    if (s.adresse >= 0x800000) s.adresse -= 0x800000;  // données de l'AVR
    v.push_back(s);
  }
  return v;
}

// ======================================================================================================================
// GDB/MI
// ======================================================================================================================
namespace {
struct LecteurMi {
  const std::string& t;
  std::size_t i = 0;
  bool fin() const { return i >= t.size(); }
  char c() const { return i < t.size() ? t[i] : '\0'; }
  std::string chaine() {
    std::string r;
    ++i;  // "
    while (i < t.size() && t[i] != '"') {
      if (t[i] == '\\' && i + 1 < t.size()) {
        const char e = t[++i];
        r += e == 'n' ? '\n' : e == 't' ? '\t' : e;
      } else
        r += t[i];
      ++i;
    }
    ++i;
    return r;
  }
  std::string nom() {
    const std::size_t d = i;
    while (i < t.size() && t[i] != '=' && t[i] != ',' && t[i] != '}' && t[i] != ']') ++i;
    return t.substr(d, i - d);
  }
  nlohmann::ordered_json valeur() {
    if (c() == '"') return chaine();
    if (c() == '{') {
      ++i;
      nlohmann::ordered_json o = nlohmann::ordered_json::object();
      while (!fin() && c() != '}') {
        const std::string k = nom();
        if (c() == '=') ++i;
        o[k] = valeur();
        if (c() == ',') ++i;
      }
      ++i;
      return o;
    }
    if (c() == '[') {
      ++i;
      nlohmann::ordered_json a = nlohmann::ordered_json::array();
      while (!fin() && c() != ']') {
        if (c() == '"' || c() == '{' || c() == '[') a.push_back(valeur());
        else {
          // résultat « nom=valeur » dans une liste
          const std::string k = nom();
          if (c() == '=') ++i;
          a.push_back({{k, valeur()}});
        }
        if (c() == ',') ++i;
      }
      ++i;
      return a;
    }
    return nom();
  }
};
}  // namespace

bool lireEnregistrementMi(const std::string& ligne, std::string& classe, nlohmann::ordered_json& valeurs) {
  std::size_t i = 0;
  while (i < ligne.size() && std::isdigit(static_cast<unsigned char>(ligne[i]))) ++i;
  if (i >= ligne.size() || (ligne[i] != '^' && ligne[i] != '*' && ligne[i] != '=')) return false;
  ++i;
  const std::size_t v = ligne.find(',', i);
  classe = ligne.substr(i, v == std::string::npos ? std::string::npos : v - i);
  valeurs = nlohmann::ordered_json::object();
  if (v == std::string::npos) return true;
  LecteurMi l{ligne, v + 1};
  while (!l.fin()) {
    const std::string k = l.nom();
    if (l.c() == '=') ++l.i;
    valeurs[k] = l.valeur();
    if (l.c() == ',') ++l.i;
    else break;
  }
  return true;
}

bool PiloteGdb::demarrer(const std::string& elf, std::string* erreur, const std::string& gdb) {
  arreter();
  m_journal.clear();
  if (!m_p.demarrer({gdb, "--interpreter=mi2", "-nx", "-q", elf}, {}, erreur)) return false;
  // Jusqu'à la première invite
  for (int i = 0; i < 200; ++i) {
    const auto l = m_p.lireLigne(15);
    if (!l) break;
    m_journal.push_back(*l);
    if (l->rfind("(gdb)", 0) == 0) return true;
  }
  if (erreur) *erreur = "GDB ne répond pas : " + m_p.erreurs();
  m_p.arreter();
  return false;
}

void PiloteGdb::arreter() {
  if (m_p.enCours()) {
    m_p.ecrire("-gdb-exit\n");
    m_p.fermerEntree();
  }
  m_p.arreter();
}

bool PiloteGdb::enCours() const { return m_p.enCours(); }

PiloteGdb::Reponse PiloteGdb::commande(const std::string& mi, double delai) {
  Reponse r;
  const std::string jeton = std::to_string(++m_jeton);
  m_journal.push_back(jeton + mi);
  if (!m_p.ecrire(jeton + mi + "\n")) return r;
  const auto t0 = std::chrono::steady_clock::now();
  bool resultat = false;
  while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < delai) {
    const auto l = m_p.lireLigne(delai);
    if (!l) break;
    m_journal.push_back(*l);
    if (!l->empty() && (*l)[0] == '~') {
      std::string c;
      nlohmann::ordered_json v;
      LecteurMi lm{*l, 1};
      if (lm.c() == '"') r.console.push_back(lm.chaine());
      continue;
    }
    if (l->rfind(jeton + "^", 0) == 0) {
      r.brut = *l;
      lireEnregistrementMi(*l, r.classe, r.valeurs);
      r.ok = r.classe == "done" || r.classe == "running" || r.classe == "connected";
      resultat = true;
      continue;
    }
    if (resultat && l->rfind("(gdb)", 0) == 0) break;
  }
  return r;
}

std::optional<std::uint32_t> PiloteGdb::adresseLigne(const std::string& fichier, int ligne) {
  const Reponse r = commande("-break-insert " + utf8(cheminFichier(fichier).filename()) + ":" + std::to_string(ligne));
  if (!r.ok || !r.valeurs.contains("bkpt")) return std::nullopt;
  const auto& b = r.valeurs["bkpt"];
  std::optional<std::uint32_t> a;
  if (b.contains("addr") && b["addr"].is_string()) {
    const std::string x = b["addr"].get<std::string>();
    if (x.rfind("0x", 0) == 0) a = static_cast<std::uint32_t>(std::stoul(x, nullptr, 16));
  }
  if (b.contains("number")) commande("-break-delete " + b["number"].get<std::string>());
  return a;
}

std::optional<std::uint32_t> PiloteGdb::adresseVariable(const std::string& nom) {
  const Reponse r = commande("-data-evaluate-expression &" + nom);
  if (!r.ok || !r.valeurs.contains("value")) return std::nullopt;
  const std::string v = r.valeurs["value"].get<std::string>();
  const std::size_t i = v.find("0x");
  if (i == std::string::npos) return std::nullopt;
  std::uint32_t a = static_cast<std::uint32_t>(std::stoul(v.substr(i), nullptr, 16));
  if (a >= 0x800000) a -= 0x800000;
  return a;
}

std::string PiloteGdb::typeVariable(const std::string& nom) {
  const Reponse r = commande("-var-create - * " + nom);
  if (!r.ok || !r.valeurs.contains("type")) return {};
  const std::string t = r.valeurs["type"].get<std::string>();
  if (r.valeurs.contains("name")) commande("-var-delete " + r.valeurs["name"].get<std::string>());
  return t;
}

// ======================================================================================================================
// Mise au point
// ======================================================================================================================
std::optional<std::uint32_t> DebogueurMicro::ajouterPoint(const std::string& fichier, int ligne) {
  const auto a = m_table.adresse(fichier, ligne);
  if (a) m_e.pointsArret.insert(*a);
  return a;
}

void DebogueurMicro::surveiller(const std::string& nom, std::uint32_t adresse, int taille) {
  VariableSurveillee v{nom, adresse, std::clamp(taille, 1, 8), 0};
  v.valeur = lireVariable(adresse, v.taille);
  m_surveillees.push_back(v);
}

std::int64_t DebogueurMicro::lireVariable(std::uint32_t adresse, int taille) const {
  std::uint64_t v = 0;
  for (int i = 0; i < taille; ++i) v |= static_cast<std::uint64_t>(m_e.memoire(adresse + static_cast<std::uint32_t>(i))) << (8 * i);  // petit-boutiste
  return static_cast<std::int64_t>(v);
}

std::vector<VariableSurveillee> DebogueurMicro::variables() const {
  std::vector<VariableSurveillee> v = m_surveillees;
  for (auto& x : v) x.valeur = lireVariable(x.adresse, x.taille);
  return v;
}

bool DebogueurMicro::variableChangee() {
  bool change = false;
  for (auto& x : m_surveillees) {
    const std::int64_t n = lireVariable(x.adresse, x.taille);
    if (n != x.valeur) change = true;
    x.valeur = n;
  }
  return change;
}

void DebogueurMicro::pasLigne(int maxInstructions) {
  const auto l0 = ligneCourante();
  const std::uint16_t sp0 = m_e.sp();
  for (int i = 0; i < maxInstructions && !m_e.termine(); ++i) {
    m_e.pas();
    const auto l = ligneCourante();
    // Comme « next » : une autre ligne, hors des fonctions appelées et des interruptions (pile pas plus profonde)
    if (l && (!l0 || l->ligne != l0->ligne || l->fichier != l0->fichier) && m_e.sp() >= sp0) return;
  }
}

// ======================================================================================================================
// Co-simulation
// ======================================================================================================================
CosimulationMicro::CosimulationMicro(const ConceptionElec& c, const std::string& reference, const NetlistSpice& n, std::unique_ptr<EmulateurMicro> e)
    : m_e(std::move(e)), m_reference(reference) {
  m_debogueur = std::make_unique<DebogueurMicro>(*m_e);
  const ComposantElec* x = c.parReference(reference);
  const DefSymbole* d = x ? c.symbole(x->symbole) : nullptr;
  if (d) {
    const Connectivite k = connectivite(c);
    for (const auto& b : d->broches) {
      const std::string reseau = k.nom(x->uuid, b.numero);
      if (reseau.empty()) continue;
      if (b.type == TypeBroche::AlimEntree || b.type == TypeBroche::AlimSortie) {
        if (minuscules(b.nom) == "vcc" || minuscules(b.nom) == "vdd") m_vecteurVcc = vecteurTension(n, reseau);
        continue;
      }
      m_broches.push_back({b.numero, portDuNumero(*d, b.numero), reseau, vecteurTension(n, reseau)});
    }
  }
  m_e->surSerie = [this](std::uint8_t o, double t) {
    // Appelé pendant que l'émulateur tourne, verrou déjà tenu
    m_recus.push_back({t, o});
    m_traces.push_back({t, "série 0x" + hex2(o) + (o >= 32 && o < 127 ? std::string(" '") + static_cast<char>(o) + "'" : std::string())});
  };
}

CosimulationMicro::~CosimulationMicro() { interrompre(); }

std::vector<std::string> CosimulationMicro::sondes() const {
  std::vector<std::string> v;
  for (const auto& b : m_broches)
    if (!b.vecteur.empty()) v.push_back(b.vecteur);
  if (!m_vecteurVcc.empty()) v.push_back(m_vecteurVcc);
  return v;
}

void CosimulationMicro::noterSorties(double t) {
  for (const auto& b : m_broches) {
    const EtatBroche e = m_e->broche(b.port);
    auto it = m_etats.find(b.port);
    if (it != m_etats.end() && it->second == e) continue;
    static const char* noms[] = {"0", "1", "entrée", "entrée tirée"};
    if (it != m_etats.end() || e != EtatBroche::Entree) m_traces.push_back({t, b.port + " → " + noms[static_cast<int>(e)]});
    m_etats[b.port] = e;
  }
  if (m_traces.size() > 20000) m_traces.erase(m_traces.begin(), m_traces.begin() + 10000);
}

void CosimulationMicro::pas(double t, const std::map<std::string, double>& valeurs) {
  std::unique_lock<std::mutex> g(m_verrou);
  if (m_interrompu) return;
  if (const auto it = valeurs.find(m_vecteurVcc); it != valeurs.end() && std::isfinite(it->second) && it->second > 0.5) m_vcc = it->second;
  m_e->alimentation(m_vcc);
  // Entrées aux seuils de l'AVR, avec leur hystérésis ; voies analogiques
  for (const auto& b : m_broches) {
    const auto it = valeurs.find(b.vecteur);
    if (it == valeurs.end() || !std::isfinite(it->second)) continue;
    const double v = it->second;
    const EtatBroche e = m_e->broche(b.port);
    if (e == EtatBroche::Entree || e == EtatBroche::Tirage) {
      const auto ie = m_entrees.find(b.port);
      const bool avant = ie != m_entrees.end() && ie->second;
      const bool niveau = avant ? v > 0.3 * m_vcc : v > 0.6 * m_vcc;
      if (ie == m_entrees.end() || niveau != avant) {
        m_entrees[b.port] = niveau;
        m_e->entree(b.port, niveau);
      }
    }
    if (b.port.size() == 3 && b.port[1] == 'C') m_e->analogique(b.port[2] - '0', v);
  }
  if (conditionCircuit) {
    const bool cnd = conditionCircuit(valeurs);
    if (cnd && !m_conditionAvant && m_mode == Mode::Libre) {
      m_mode = Mode::Arret;
      m_raison = "condition du circuit vraie";
    }
    m_conditionAvant = cnd;
  }
  for (int garde = 0; garde < 1'000'000 && !m_interrompu; ++garde) {
    if (m_mode == Mode::Libre) {
      m_e->avancer(t);
      if (m_e->arreteSurPoint) {
        m_mode = Mode::Arret;
        const auto l = m_debogueur->ligneCourante();
        m_raison = l ? "point d'arrêt, " + l->fichier + " ligne " + std::to_string(l->ligne) : "point d'arrêt";
        continue;
      }
      if (arretSurVariable && m_debogueur->variableChangee()) {
        m_mode = Mode::Arret;
        m_raison = "variable surveillée changée";
        continue;
      }
      break;
    }
    if (m_mode == Mode::Arret) {
      noterSorties(t);
      m_cv.notify_all();
      m_cv.wait(g, [&] { return m_mode != Mode::Arret || m_interrompu; });
      continue;
    }
    if (m_mode == Mode::PasLigne || m_mode == Mode::PasInstruction) {
      if (m_mode == Mode::PasLigne) m_debogueur->pasLigne();
      else m_e->pas();
      m_e->reprise = true;
      const auto l = m_debogueur->ligneCourante();
      m_raison = l ? "pas, " + l->fichier + " ligne " + std::to_string(l->ligne) : "pas";
      m_mode = Mode::ApresPas;  // le circuit fait ce pas, puis tout s'arrête
      break;
    }
    if (m_mode == Mode::ApresPas) {
      m_mode = Mode::Arret;
      continue;
    }
  }
  noterSorties(t);
  m_cv.notify_all();
}

bool CosimulationMicro::source(const std::string& nom, double, double& valeur) {
  const std::string prefixe = "v" + minuscules(m_reference) + "_";
  if (nom.size() <= prefixe.size() + 1 || nom.compare(0, prefixe.size(), prefixe) != 0) return false;
  const char genre = nom.back();
  const std::string numero = nom.substr(prefixe.size(), nom.size() - prefixe.size() - 1);
  std::lock_guard<std::mutex> g(m_verrou);
  for (const auto& b : m_broches) {
    if (minuscules(b.numero) != numero) continue;
    const auto it = m_etats.find(b.port);
    const EtatBroche e = it == m_etats.end() ? EtatBroche::Entree : it->second;
    switch (genre) {
      case 'd': valeur = e == EtatBroche::Haut ? m_vcc : 0; return true;
      case 'c': valeur = e == EtatBroche::Haut || e == EtatBroche::Bas ? 1 : 0; return true;
      case 'p': valeur = e == EtatBroche::Tirage ? 1 : 0; return true;
      default: return false;
    }
  }
  return false;
}

void CosimulationMicro::interrompre() {
  std::lock_guard<std::mutex> g(m_verrou);
  m_interrompu = true;
  m_cv.notify_all();
}

std::vector<OctetSerie> CosimulationMicro::recus() const {
  std::lock_guard<std::mutex> g(m_verrou);
  return m_recus;
}

bool CosimulationMicro::reglagesCompatibles() const {
  const FormatSerie f = m_e->formatSerie();
  if (f.debit <= 0) return false;
  return std::abs(terminal.debit - f.debit) <= 0.03 * f.debit && terminal.bits == f.bits && terminal.parite == f.parite && terminal.arret == f.arret;
}

std::string CosimulationMicro::texteRecu() const {
  const std::vector<OctetSerie> r = recus();
  std::string t;
  const bool ok = reglagesCompatibles();
  for (const auto& o : r) {
    if (ok) t += static_cast<char>(o.octet);
    else t += static_cast<char>(0x80 | ((o.octet * 37 + 11) & 0x7F));  // ce que lit un terminal mal réglé : des octets faux
  }
  return t;
}

void CosimulationMicro::envoyer(const std::string& texte) {
  std::lock_guard<std::mutex> g(m_verrou);
  for (const unsigned char ch : texte) m_e->envoyerSerie(ch);
}

bool CosimulationMicro::arrete() const {
  std::lock_guard<std::mutex> g(m_verrou);
  return m_mode == Mode::Arret;
}

bool CosimulationMicro::attendreArret(double delai) {
  std::unique_lock<std::mutex> g(m_verrou);
  return m_cv.wait_for(g, std::chrono::duration<double>(delai), [&] { return m_mode == Mode::Arret || m_interrompu; }) && m_mode == Mode::Arret;
}

void CosimulationMicro::continuer() {
  std::lock_guard<std::mutex> g(m_verrou);
  if (m_mode != Mode::Arret) return;
  m_e->reprise = true;
  m_mode = Mode::Libre;
  m_raison.clear();
  m_cv.notify_all();
}

void CosimulationMicro::pasLigne() {
  std::lock_guard<std::mutex> g(m_verrou);
  if (m_mode != Mode::Arret) return;
  m_mode = Mode::PasLigne;
  m_cv.notify_all();
}

void CosimulationMicro::pasInstruction() {
  std::lock_guard<std::mutex> g(m_verrou);
  if (m_mode != Mode::Arret) return;
  m_mode = Mode::PasInstruction;
  m_cv.notify_all();
}

std::string CosimulationMicro::raisonArret() const {
  std::lock_guard<std::mutex> g(m_verrou);
  return m_raison;
}

std::vector<TraceMicro> CosimulationMicro::traces() const {
  std::lock_guard<std::mutex> g(m_verrou);
  return m_traces;
}

double CosimulationMicro::vcc() const {
  std::lock_guard<std::mutex> g(m_verrou);
  return m_vcc;
}

EtatBroche CosimulationMicro::etat(const std::string& port) const {
  std::lock_guard<std::mutex> g(m_verrou);
  const auto it = m_etats.find(port);
  return it == m_etats.end() ? EtatBroche::Entree : it->second;
}

// ======================================================================================================================
// Robot sur une piste
// ======================================================================================================================
std::uint8_t PisteRobot::pixel(double x, double y) const {
  const long i = std::lround(std::floor(x)), j = std::lround(std::floor(y));
  if (i < 0 || j < 0 || i >= largeur || j >= hauteur) return 255;
  return gris[static_cast<std::size_t>(j) * static_cast<std::size_t>(largeur) + static_cast<std::size_t>(i)];
}

PisteDepart pisteOvale(double longueur, double epaisseur, double rayon) {
  PisteDepart r;
  const double marge = 60;
  const double droite = std::max(10.0, (longueur - 2 * std::numbers::pi * rayon) / 2);
  PisteRobot& p = r.piste;
  p.largeur = static_cast<int>(std::ceil(droite + 2 * rayon + 2 * marge));
  p.hauteur = static_cast<int>(std::ceil(2 * rayon + 2 * marge));
  p.gris.assign(static_cast<std::size_t>(p.largeur) * static_cast<std::size_t>(p.hauteur), 255);
  const double cx1 = marge + rayon, cx2 = marge + rayon + droite, cy = marge + rayon;
  const double xBarre = cx1 + droite / 2, yBas = cy + rayon;
  for (int j = 0; j < p.hauteur; ++j)
    for (int i = 0; i < p.largeur; ++i) {
      const double x = i + 0.5, y = j + 0.5;
      double d;
      if (x < cx1) d = std::abs(std::hypot(x - cx1, y - cy) - rayon);
      else if (x > cx2) d = std::abs(std::hypot(x - cx2, y - cy) - rayon);
      else d = std::min(std::abs(y - (cy - rayon)), std::abs(y - yBas));
      const bool ligne = d <= epaisseur / 2;
      // Barre de départ : 12 mm de large, 70 mm en travers de la droite du bas
      const bool barre = std::abs(x - xBarre) <= 6 && std::abs(y - yBas) <= 35;
      if (ligne || barre) p.gris[static_cast<std::size_t>(j) * static_cast<std::size_t>(p.largeur) + static_cast<std::size_t>(i)] = 0;
    }
  r.x = xBarre - 80;
  r.y = yBas;
  r.cap = 0;
  return r;
}

std::optional<PisteRobot> lirePiste(const std::string& o, std::string* erreur) {
  auto refuser = [&](const std::string& e) -> std::optional<PisteRobot> {
    if (erreur) *erreur = e;
    return std::nullopt;
  };
  if (o.size() < 2 || o[0] != 'P' || (o[1] != '5' && o[1] != '2')) return refuser("la piste doit être une image PGM (P5 ou P2)");
  std::size_t i = 2;
  auto mot = [&]() {
    for (;;) {
      while (i < o.size() && std::isspace(static_cast<unsigned char>(o[i]))) ++i;
      if (i < o.size() && o[i] == '#')
        while (i < o.size() && o[i] != '\n') ++i;
      else
        break;
    }
    const std::size_t d = i;
    while (i < o.size() && !std::isspace(static_cast<unsigned char>(o[i]))) ++i;
    return o.substr(d, i - d);
  };
  PisteRobot p;
  try {
    p.largeur = std::stoi(mot());
    p.hauteur = std::stoi(mot());
    const int max = std::stoi(mot());
    if (p.largeur <= 0 || p.hauteur <= 0 || max <= 0 || max > 255) return refuser("dimensions de la piste invalides");
    const std::size_t n = static_cast<std::size_t>(p.largeur) * static_cast<std::size_t>(p.hauteur);
    p.gris.resize(n);
    if (o[1] == '5') {
      ++i;
      if (o.size() < i + n) return refuser("image de la piste tronquée");
      for (std::size_t k = 0; k < n; ++k) p.gris[k] = static_cast<std::uint8_t>(static_cast<unsigned char>(o[i + k]) * 255 / max);
    } else
      for (std::size_t k = 0; k < n; ++k) p.gris[k] = static_cast<std::uint8_t>(std::stoi(mot()) * 255 / max);
  } catch (const std::exception&) {
    return refuser("image de la piste illisible");
  }
  return p;
}

std::string ecrirePgm(const PisteRobot& p) {
  std::string t = "P5\n# PisteRobot de Prométhée, 1 pixel par millimètre\n" + std::to_string(p.largeur) + " " + std::to_string(p.hauteur) + "\n255\n";
  t.append(reinterpret_cast<const char*>(p.gris.data()), p.gris.size());
  return t;
}

double distanceObstacle(const PisteRobot& p, double x, double y, double angle, double portee) {
  const double dx = std::cos(angle), dy = std::sin(angle);
  for (double d = 0; d <= portee; d += 1) {
    const std::uint8_t v = p.pixel(x + dx * d, y + dy * d);
    if (v >= 1 && v <= 15) return d;
  }
  return portee;
}

ResultatRobot simulerRobot(EmulateurMicro& e, const PisteRobot& p, RobotDeuxRoues r, const BrochesRobot& b, const OptionsRobot& o) {
  ResultatRobot res;
  std::string serie;
  e.surSerie = [&](std::uint8_t c, double) { serie += static_cast<char>(c); };
  std::vector<int> derniers(b.capteursLigne.size(), -1);
  int n = 0;
  for (double t = 0; t < o.duree; t += o.pas, ++n) {
    const double fx = std::cos(r.cap), fy = std::sin(r.cap), gx = fy, gy = -fx;  // avant ; gauche (y vers le bas)
    bool surLigne = false;
    for (std::size_t i = 0; i < b.capteursLigne.size() && i < r.capteursLigne.size(); ++i) {
      const auto [av, ga] = r.capteursLigne[i];
      const bool noir = p.pixel(r.x + fx * av + gx * ga, r.y + fy * av + gy * ga) < 128;
      surLigne |= noir;
      if (derniers[i] != static_cast<int>(noir)) {
        e.entree(b.capteursLigne[i], noir);
        derniers[i] = noir;
      }
    }
    if (!surLigne) ++res.horsPiste;
    for (std::size_t i = 0; i < b.voiesDistance.size() && i < r.capteursDistance.size(); ++i) {
      const double d = distanceObstacle(p, r.x + fx * 45, r.y + fy * 45, r.cap + r.capteursDistance[i], r.porteeDistance);
      e.analogique(b.voiesDistance[i], 5.0 * (1 - d / r.porteeDistance));
    }
    e.avancer(t + o.pas);
    // Moteurs : rapport cyclique, sens arrière quand la broche de sens est haute
    const double cg = e.rapportCyclique(b.mliGauche) * (e.broche(b.sensGauche) == EtatBroche::Haut ? -1 : 1);
    const double cd = e.rapportCyclique(b.mliDroite) * (e.broche(b.sensDroite) == EtatBroche::Haut ? -1 : 1);
    const double vg = cg * r.vitesseMax, vd = cd * r.vitesseMax;
    const double v = (vg + vd) / 2, w = (vd - vg) / r.entraxe;
    r.x += v * fx * o.pas;
    r.y += v * fy * o.pas;
    r.cap -= w * o.pas;  // la roue droite plus vite : virage à gauche, sens direct à l'écran
    res.distance += std::abs(v) * o.pas;
    if (n % 10 == 0) res.trajet.emplace_back(r.x, r.y);
    res.temps = t + o.pas;
    if (!o.attendu.empty() && serie.find(o.attendu) != std::string::npos) {
      res.attenduRecu = true;
      res.tempsAttendu = res.temps;
      break;
    }
    if (e.termine()) break;
  }
  res.serie = serie;
  e.surSerie = nullptr;
  return res;
}

int commandeRobot(const std::vector<std::string>& a, std::string* sortie) {
  std::ostringstream o;
  auto fin = [&](int code) {
    if (sortie) *sortie = o.str();
    return code;
  };
  std::string elf, piste, attendu;
  double duree = 60, frequence = 16e6;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const std::string& x = a[i];
    auto suivant = [&]() { return i + 1 < a.size() ? a[++i] : std::string(); };
    if (x == "--robot") elf = suivant();
    else if (x == "--piste") piste = suivant();
    else if (x == "--attendre") attendu = suivant();
    else if (x == "--duree") duree = std::atof(suivant().c_str());
    else if (x == "--frequence") frequence = std::atof(suivant().c_str());
  }
  if (elf.empty()) {
    o << "usage : --robot programme.elf [--piste piste.pgm] [--attendre texte] [--duree s] [--frequence Hz]\n";
    return fin(2);
  }
  std::string erreur;
  std::unique_ptr<EmulateurMicro> e = creerEmulateur("atmega328p", frequence, &erreur);
  if (!e || !e->charger(elf, &erreur)) {
    o << "erreur : " << erreur << "\n";
    return fin(2);
  }
  PisteDepart d = pisteOvale();
  if (!piste.empty()) {
    std::optional<PisteRobot> p;
    try {
      p = lirePiste(lireFichier(cheminFichier(piste)), &erreur);
    } catch (const std::exception& x) {
      erreur = x.what();
    }
    if (!p) {
      o << "erreur : " << erreur << "\n";
      return fin(2);
    }
    d.piste = *p;
  }
  RobotDeuxRoues r;
  r.x = d.x;
  r.y = d.y;
  r.cap = d.cap;
  OptionsRobot op;
  op.duree = duree;
  op.attendu = attendu;
  const ResultatRobot res = simulerRobot(*e, d.piste, r, BrochesRobot{}, op);
  o << "temps simulé : " << nb(res.temps) << " s, distance : " << nb(res.distance / 1000, 4) << " m\n";
  o << "liaison série : " << res.serie << (res.serie.empty() || res.serie.back() == '\n' ? "" : "\n");
  if (!attendu.empty()) o << (res.attenduRecu ? "attendu reçu à " + nb(res.tempsAttendu) + " s : « " + attendu + " »\n" : "attendu non reçu : « " + attendu + " »\n");
  return fin(attendu.empty() || res.attenduRecu ? 0 : 1);
}

// ======================================================================================================================
// Deux versions comparées
// ======================================================================================================================
TraceBroches tracerBroches(EmulateurMicro& e, const std::vector<std::string>& ports, double duree, double pas) {
  TraceBroches t;
  std::map<std::string, int> avant;
  for (double x = 0; x <= duree; x += pas) {
    e.avancer(x);
    for (const auto& p : ports) {
      const int n = e.broche(p) == EtatBroche::Haut ? 1 : 0;
      auto it = avant.find(p);
      if (it == avant.end() || it->second != n) {
        t[p].emplace_back(e.temps(), n == 1);
        avant[p] = n;
      }
    }
    if (e.termine()) break;
  }
  return t;
}

EcartTraces comparerTraces(const TraceBroches& a, const TraceBroches& b, double tolerance) {
  EcartTraces r;
  std::set<std::string> ports;
  for (const auto& [p, v] : a) ports.insert(p);
  for (const auto& [p, v] : b) ports.insert(p);
  for (const auto& p : ports) {
    static const std::vector<std::pair<double, bool>> vide;
    const auto ia = a.find(p), ib = b.find(p);
    const auto& va = ia == a.end() ? vide : ia->second;
    const auto& vb = ib == b.end() ? vide : ib->second;
    if (va.size() != vb.size()) r.differences.push_back(p + " : " + std::to_string(va.size()) + " fronts contre " + std::to_string(vb.size()));
    for (std::size_t i = 0; i < std::min(va.size(), vb.size()); ++i) {
      if (va[i].second != vb[i].second) {
        r.differences.push_back(p + " : front " + std::to_string(i + 1) + " de sens contraire");
        break;
      }
      if (std::abs(va[i].first - vb[i].first) > tolerance) {
        r.differences.push_back(p + " : front " + std::to_string(i + 1) + " à " + nb(vb[i].first) + " s au lieu de " + nb(va[i].first) + " s");
        break;
      }
    }
  }
  r.identiques = r.differences.empty();
  return r;
}

}  // namespace prom::ecad

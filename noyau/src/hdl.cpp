// Prométhée : composants décrits par du code, machines à états et logique programmable (lot L15 de la 0.34 ; 15.7,
// 15.10, 15.12). Voir hdl.hpp. Licence GPL-3.0-only.
#include "promethee/hdl.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <functional>
#include <regex>
#include <set>
#include <sstream>

#include "promethee/fichiers.hpp"

namespace fs = std::filesystem;

namespace prom::ecad {

namespace {
std::string minuscules(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
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
// Identifiant VHDL et Verilog sûr : lettres, chiffres, un seul « _ » à la suite ; une lettre en tête
std::string identifiant(const std::string& s0) {
  static const std::set<std::string> reserves{"abs", "access", "after", "alias", "all", "and", "architecture", "array", "assert", "attribute", "begin", "block", "body", "buffer", "bus", "case",
                                              "component", "configuration", "constant", "disconnect", "downto", "else", "elsif", "end", "entity", "exit", "file", "for", "function", "generate",
                                              "generic", "group", "guarded", "if", "impure", "in", "inertial", "inout", "is", "label", "library", "linkage", "literal", "loop", "map", "mod", "nand",
                                              "new", "next", "nor", "not", "null", "of", "on", "open", "or", "others", "out", "package", "port", "postponed", "procedure", "process", "pure", "range",
                                              "record", "register", "reject", "rem", "report", "return", "rol", "ror", "select", "severity", "signal", "shared", "sla", "sll", "sra", "srl", "subtype",
                                              "then", "to", "transport", "type", "unaffected", "units", "until", "use", "variable", "wait", "when", "while", "with", "xnor", "xor", "module",
                                              "input", "output", "wire", "reg", "assign", "always", "initial", "endmodule"};
  std::string s;
  for (unsigned char c : translitterer(s0)) {
    if (std::isalnum(c)) s += static_cast<char>(c);
    else if (!s.empty() && s.back() != '_') s += '_';
  }
  while (!s.empty() && s.back() == '_') s.pop_back();
  if (s.empty() || !std::isalpha(static_cast<unsigned char>(s[0]))) s = "s_" + s;
  if (reserves.count(minuscules(s))) s = "s_" + s;
  return s;
}
std::string bitsTexte(const std::vector<int>& b) {
  std::string s;
  for (std::size_t i = b.size(); i-- > 0;) s += b[i] ? '1' : '0';  // poids fort d'abord
  return s;
}
}  // namespace

// ======================================================================================================================
// Modules HDL
// ======================================================================================================================
std::vector<std::string> ModuleHdl::bitsEntree() const {
  std::vector<std::string> v;
  for (const auto& p : ports)
    if (!p.sortie) {
      if (p.largeur == 1) v.push_back(p.nom);
      else
        for (int i = 0; i < p.largeur; ++i) v.push_back(p.nom + "[" + std::to_string(i) + "]");
    }
  return v;
}
std::vector<std::string> ModuleHdl::bitsSortie() const {
  std::vector<std::string> v;
  for (const auto& p : ports)
    if (p.sortie) {
      if (p.largeur == 1) v.push_back(p.nom);
      else
        for (int i = 0; i < p.largeur; ++i) v.push_back(p.nom + "[" + std::to_string(i) + "]");
    }
  return v;
}

std::string langageHdl(const std::string& chemin) {
  const std::string e = minuscules(utf8(cheminFichier(chemin).extension()));
  return e == ".vhd" || e == ".vhdl" ? "vhdl" : "verilog";
}

std::optional<ModuleHdl> lireModuleHdl(const std::string& source0, const std::string& langage, std::string* erreur) {
  auto refuser = [&](const std::string& e) -> std::optional<ModuleHdl> {
    if (erreur) *erreur = e;
    return std::nullopt;
  };
  ModuleHdl m;
  m.langage = langage;
  m.source = source0;
  // Sans commentaires
  std::string s;
  {
    std::istringstream in(source0);
    for (std::string l; std::getline(in, l);) {
      const std::size_t c = l.find(langage == "vhdl" ? "--" : "//");
      s += (c == std::string::npos ? l : l.substr(0, c)) + "\n";
    }
    if (langage != "vhdl")
      for (std::size_t a = s.find("/*"); a != std::string::npos; a = s.find("/*")) {
        const std::size_t b = s.find("*/", a);
        s.erase(a, b == std::string::npos ? std::string::npos : b + 2 - a);
      }
  }
  if (langage == "vhdl") {
    std::smatch x;
    static const std::regex ent(R"(entity\s+(\w+)\s+is\s+port\s*\(([\s\S]*?)\)\s*;\s*end)", std::regex::icase);
    if (!std::regex_search(s, x, ent)) return refuser("entité VHDL introuvable (entity … is port (…); end)");
    m.nom = x[1].str();
    std::string ports = x[2].str();
    // « a, b : in std_logic; q : out std_logic_vector(3 downto 0) »
    static const std::regex decl(R"(([\w\s,]+?)\s*:\s*(in|out|inout|buffer)\s+([\w]+)\s*(?:\(\s*(\d+)\s+(downto|to)\s+(\d+)\s*\))?)", std::regex::icase);
    for (auto it = std::sregex_iterator(ports.begin(), ports.end(), decl); it != std::sregex_iterator(); ++it) {
      const std::string dir = minuscules((*it)[2].str());
      int largeur = 1;
      if ((*it)[4].matched) largeur = std::abs(std::stoi((*it)[4].str()) - std::stoi((*it)[6].str())) + 1;
      std::istringstream noms((*it)[1].str());
      for (std::string n; std::getline(noms, n, ',');) {
        n.erase(std::remove_if(n.begin(), n.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), n.end());
        if (!n.empty()) m.ports.push_back({n, dir != "in", largeur});
      }
    }
  } else {
    std::smatch x;
    static const std::regex mod(R"(module\s+(\w+)\s*(?:#\s*\([^;]*?\)\s*)?\(([\s\S]*?)\)\s*;)");
    if (!std::regex_search(s, x, mod)) return refuser("module Verilog introuvable (module … (…);)");
    m.nom = x[1].str();
    const std::string liste = x[2].str();
    const std::string corps = x.suffix().str();
    static const std::regex ansi(R"((input|output|inout)\s+(?:wire|reg|logic)?\s*(?:signed\s+)?(?:\[\s*(\d+)\s*:\s*(\d+)\s*\])?\s*([\w\s,]+?)(?=,\s*(?:input|output|inout)\b|$))");
    const bool styleAnsi = std::regex_search(liste, std::regex(R"(\b(input|output|inout)\b)"));
    auto ajouter = [&](const std::string& dir, const std::string& noms, int largeur) {
      std::istringstream in(noms);
      for (std::string n; std::getline(in, n, ',');) {
        n.erase(std::remove_if(n.begin(), n.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), n.end());
        if (!n.empty()) m.ports.push_back({n, dir != "input", largeur});
      }
    };
    if (styleAnsi) {
      for (auto it = std::sregex_iterator(liste.begin(), liste.end(), ansi); it != std::sregex_iterator(); ++it)
        ajouter((*it)[1].str(), (*it)[4].str(), (*it)[2].matched ? std::abs(std::stoi((*it)[2].str()) - std::stoi((*it)[3].str())) + 1 : 1);
    } else {
      // Style ancien : la liste des noms, puis « input clk; output [3:0] q; » dans le corps
      std::map<std::string, std::pair<std::string, int>> decl;
      static const std::regex d(R"((input|output|inout)\s+(?:wire|reg)?\s*(?:\[\s*(\d+)\s*:\s*(\d+)\s*\])?\s*([\w\s,]+?)\s*;)");
      for (auto it = std::sregex_iterator(corps.begin(), corps.end(), d); it != std::sregex_iterator(); ++it) {
        std::istringstream in((*it)[4].str());
        for (std::string n; std::getline(in, n, ',');) {
          n.erase(std::remove_if(n.begin(), n.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), n.end());
          decl[n] = {(*it)[1].str(), (*it)[2].matched ? std::abs(std::stoi((*it)[2].str()) - std::stoi((*it)[3].str())) + 1 : 1};
        }
      }
      std::istringstream in(liste);
      for (std::string n; std::getline(in, n, ',');) {
        n.erase(std::remove_if(n.begin(), n.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), n.end());
        if (const auto it = decl.find(n); it != decl.end()) m.ports.push_back({n, it->second.first != "input", it->second.second});
      }
    }
  }
  if (m.ports.empty()) return refuser("aucun port lu dans « " + m.nom + " »");
  return m;
}

std::string outilHdl(const std::string& langage) {
  if (langage == "vhdl") return programmeDuPoste("ghdl").empty() ? std::string() : "ghdl";
  if (!programmeDuPoste("iverilog").empty() && !programmeDuPoste("vvp").empty()) return "iverilog";
  if (!programmeDuPoste("verilator").empty()) return "verilator";
  return {};
}

DefSymbole symboleHdl(const ModuleHdl& m) {
  SpecSymbole s;
  s.id = "hdl:" + m.nom;
  s.nom = m.nom;
  s.prefixe = "U";
  s.categorie = "Logique programmable";
  s.description = "Composant décrit en " + std::string(m.langage == "vhdl" ? "VHDL" : "Verilog") + " (module « " + m.nom + " »), co-simulé par le simulateur de logique du poste";
  int n = 1;
  auto ajouter = [&](const std::string& nom, TypeBroche t, int cote) {
    SpecBroche b;
    b.numero = std::to_string(n++);
    b.nom = nom;
    b.type = t;
    b.cote = cote;
    s.broches.push_back(b);
  };
  for (const auto& p : m.ports)
    if (!p.sortie)
      for (int i = 0; i < p.largeur; ++i) ajouter(p.largeur == 1 ? p.nom : p.nom + std::to_string(i), TypeBroche::Entree, 0);
  for (const auto& p : m.ports)
    if (p.sortie)
      for (int i = 0; i < p.largeur; ++i) ajouter(p.largeur == 1 ? p.nom : p.nom + std::to_string(i), TypeBroche::Sortie, 2);
  ajouter("VCC", TypeBroche::AlimEntree, 1);
  ajouter("GND", TypeBroche::AlimEntree, 3);
  return genererSymbole(s);
}

std::string poserComposantHdl(ConceptionElec& c, const ModuleHdl& m, std::size_t feuille, Pt position, const std::string& reference) {
  const DefSymbole d = symboleHdl(m);
  c.symboles[d.id] = d;
  const std::string s = poserSymbole(c, feuille, d.id, position);
  for (const auto& sp : c.feuilles[feuille].symboles)
    if (sp.uuid == s)
      if (ComposantElec* k = c.composant(sp.composant)) {
        k->reference = reference.empty() ? prochainRepere(c, "U") : reference;
        k->valeur = m.nom;
        k->champs["HDL.Langage"] = m.langage;
        k->champs["HDL.Module"] = m.nom;
        k->champs["HDL.Code"] = m.source;
        return k->uuid;
      }
  return {};
}

std::optional<ModuleHdl> moduleDuComposant(const ComposantElec& x) {
  const auto code = x.champs.find("HDL.Code");
  if (code == x.champs.end() || code->second.empty()) return std::nullopt;
  const auto l = x.champs.find("HDL.Langage");
  return lireModuleHdl(code->second, l == x.champs.end() ? std::string("verilog") : l->second);
}

// ---------- Simulateur pas à pas ----------
namespace {
// Liaison d'un port aux bits du vecteur « e » ou « s » du banc
std::string tranche(const std::string& v, int debut, int largeur, bool vhdl) {
  if (vhdl) return largeur == 1 ? v + "(" + std::to_string(debut) + ")" : v + "(" + std::to_string(debut + largeur - 1) + " downto " + std::to_string(debut) + ")";
  return largeur == 1 ? v + "[" + std::to_string(debut) + "]" : v + "[" + std::to_string(debut + largeur - 1) + ":" + std::to_string(debut) + "]";
}
std::string bancVerilog(const ModuleHdl& m) {
  const int ne = std::max<int>(1, static_cast<int>(m.bitsEntree().size())), ns = std::max<int>(1, static_cast<int>(m.bitsSortie().size()));
  std::ostringstream o;
  o << "// Banc d'essai écrit par Prométhée : entrées lues sur l'entrée standard, sorties écrites après chaque changement\n";
  o << "`timescale 1ns/1ps\nmodule promethee_banc;\n  reg [" << ne - 1 << ":0] e = 0;\n  wire [" << ns - 1 << ":0] s;\n  " << m.nom << " dut (";
  int ie = 0, is = 0;
  bool premier = true;
  for (const auto& p : m.ports) {
    o << (premier ? "" : ", ") << "." << p.nom << "(" << (p.sortie ? tranche("s", is, p.largeur, false) : tranche("e", ie, p.largeur, false)) << ")";
    (p.sortie ? is : ie) += p.largeur;
    premier = false;
  }
  o << ");\n  integer r;\n  initial begin\n    r = 1;\n    while (r == 1) begin\n      r = $fscanf(32'h8000_0000, \"%b\", e);  // sans « \\n » : la lecture rendrait la main à la ligne suivante\n      if (r == 1) begin\n        #1;\n";
  o << "        $display(\"%b\", s);\n        $fflush;\n      end\n    end\n    $finish;\n  end\nendmodule\n";
  return o.str();
}
std::string bancVhdl(const ModuleHdl& m) {
  const int ne = std::max<int>(1, static_cast<int>(m.bitsEntree().size())), ns = std::max<int>(1, static_cast<int>(m.bitsSortie().size()));
  std::ostringstream o;
  o << "-- Banc d'essai écrit par Prométhée : entrées lues sur l'entrée standard, sorties écrites après chaque changement\n";
  o << "library ieee;\nuse ieee.std_logic_1164.all;\nuse std.textio.all;\nentity promethee_banc is\nend entity;\narchitecture banc of promethee_banc is\n";
  o << "  signal e : std_logic_vector(" << ne - 1 << " downto 0) := (others => '0');\n  signal s : std_logic_vector(" << ns - 1 << " downto 0);\nbegin\n";
  o << "  dut : entity work." << m.nom << " port map (";
  int ie = 0, is = 0;
  bool premier = true;
  for (const auto& p : m.ports) {
    o << (premier ? "" : ", ") << p.nom << " => " << (p.sortie ? tranche("s", is, p.largeur, true) : tranche("e", ie, p.largeur, true));
    (p.sortie ? is : ie) += p.largeur;
    premier = false;
  }
  o << ");\n  process\n    variable l, w : line;\n    variable c : character;\n  begin\n    while not endfile(input) loop\n      readline(input, l);\n";
  o << "      for i in " << ne - 1 << " downto 0 loop\n        read(l, c);\n        if c = '1' then e(i) <= '1'; else e(i) <= '0'; end if;\n      end loop;\n      wait for 1 ns;\n";
  o << "      for i in " << ns - 1 << " downto 0 loop\n        if s(i) = '1' then write(w, character'('1')); else write(w, character'('0')); end if;\n      end loop;\n";
  o << "      writeline(output, w);\n      flush(output);\n    end loop;\n    wait;\n  end process;\nend architecture;\n";
  return o.str();
}
std::string bancVerilator(const ModuleHdl& m) {
  std::ostringstream o;
  o << "// Banc d'essai écrit par Prométhée pour Verilator : entrées lues sur l'entrée standard, sorties écrites\n";
  o << "#include <iostream>\n#include <string>\n#include \"V" << m.nom << ".h\"\n#include \"verilated.h\"\nint main(int argc, char** argv) {\n  Verilated::commandArgs(argc, argv);\n";
  o << "  V" << m.nom << " m;\n  std::string l;\n  while (std::getline(std::cin, l)) {\n    const std::size_t n = l.size();\n    auto bit = [&](std::size_t i) { return i < n && l[n - 1 - i] == '1' ? 1u : 0u; };\n";
  std::size_t ie = 0;
  for (const auto& p : m.ports)
    if (!p.sortie) {
      o << "    m." << p.nom << " = 0";
      for (int i = 0; i < p.largeur; ++i) o << " | (bit(" << ie + static_cast<std::size_t>(i) << ") << " << i << ")";
      o << ";\n";
      ie += static_cast<std::size_t>(p.largeur);
    }
  o << "    m.eval();\n    std::string s;\n";
  // Sorties, du dernier bit au premier (poids fort d'abord)
  std::vector<std::pair<std::string, int>> bits;
  for (const auto& p : m.ports)
    if (p.sortie)
      for (int i = 0; i < p.largeur; ++i) bits.emplace_back(p.nom, i);
  for (std::size_t k = bits.size(); k-- > 0;) o << "    s += ((m." << bits[k].first << " >> " << bits[k].second << ") & 1) ? '1' : '0';\n";
  o << "    std::cout << s << std::endl;\n  }\n  m.final();\n  return 0;\n}\n";
  return o.str();
}
}  // namespace

bool SimulateurHdl::demarrer(const ModuleHdl& m, const std::string& dossier, std::string* erreur) {
  arreter();
  m_m = m;
  m_journal.clear();
  auto refuser = [&](const std::string& e) {
    if (erreur) *erreur = e;
    return false;
  };
  m_outil = outilHdl(m.langage);
  if (m_outil.empty()) return refuser(m.langage == "vhdl" ? "GHDL manque sur ce poste" : "ni Icarus Verilog ni Verilator sur ce poste");
  std::error_code ec;
  const fs::path d = cheminFichier(dossier);
  fs::create_directories(d, ec);
  const std::string source = utf8(d / (m.langage == "vhdl" ? m.nom + ".vhd" : m.nom + ".v"));
  ecrireFichierAtomique(cheminFichier(source), m.source);
  OptionsProcessus o;
  o.dossier = utf8(d);
  o.delai = 300;
  auto lancer = [&](const std::vector<std::string>& cmd) {
    const ResultatProcessus r = executerProcessus(cmd, o);
    m_journal += r.sortie + r.erreurs;
    return r.reussi();
  };
  std::vector<std::string> run;
  if (m_outil == "ghdl") {
    ecrireFichierAtomique(d / "promethee_banc.vhd", bancVhdl(m));
    if (!lancer({"ghdl", "-a", "--std=08", source}) || !lancer({"ghdl", "-a", "--std=08", "promethee_banc.vhd"}) || !lancer({"ghdl", "-e", "--std=08", "promethee_banc"}))
      return refuser("GHDL refuse le composant : " + m_journal);
    run = {"ghdl", "-r", "--std=08", "promethee_banc"};
  } else if (m_outil == "iverilog") {
    ecrireFichierAtomique(d / "promethee_banc.v", bancVerilog(m));
    if (!lancer({"iverilog", "-g2012", "-o", "promethee_banc.vvp", "-s", "promethee_banc", source, "promethee_banc.v"})) return refuser("Icarus Verilog refuse le composant : " + m_journal);
    run = {"vvp", "-n", "promethee_banc.vvp"};
  } else {
    ecrireFichierAtomique(d / "promethee_banc.cpp", bancVerilator(m));
    if (!lancer({"verilator", "--cc", "--exe", "--build", "-j", "2", "-Wno-fatal", "--top-module", m.nom, "-o", "promethee_banc", source, "promethee_banc.cpp"}))
      return refuser("Verilator refuse le composant : " + m_journal);
    run = {utf8(d / "obj_dir" / "promethee_banc")};
  }
  OptionsProcessus oi;
  oi.dossier = utf8(d);
  return m_p.demarrer(run, oi, erreur);
}

std::optional<std::vector<int>> SimulateurHdl::evaluer(const std::vector<int>& entrees) {
  if (!m_p.enCours()) return std::nullopt;
  std::vector<int> e = entrees;
  e.resize(std::max<std::size_t>(1, m_m.bitsEntree().size()), 0);
  if (!m_p.ecrire(bitsTexte(e) + "\n")) return std::nullopt;
  const auto l = m_p.lireLigne(20);
  if (!l) return std::nullopt;
  std::vector<int> s;
  for (std::size_t i = l->size(); i-- > 0;) s.push_back((*l)[i] == '1' ? 1 : 0);  // poids faible d'abord
  s.resize(m_m.bitsSortie().size(), 0);
  return s;
}

void SimulateurHdl::arreter() {
  if (m_p.enCours()) m_p.fermerEntree();
  m_p.arreter();
}

// ---------- Participant ----------
CosimulationHdl::CosimulationHdl(const ConceptionElec& c, const std::string& reference, const NetlistSpice& n) : m_reference(reference) {
  const ComposantElec* x = c.parReference(reference);
  if (!x) return;
  m_m = moduleDuComposant(*x);
  const DefSymbole* d = c.symbole(x->symbole);
  if (!m_m || !d) return;
  const Connectivite k = connectivite(c);
  const std::vector<std::string> be = m_m->bitsEntree(), bs = m_m->bitsSortie();
  auto nomBroche = [](std::string bit) {
    // « q[2] » → « q2 »
    bit.erase(std::remove(bit.begin(), bit.end(), '['), bit.end());
    bit.erase(std::remove(bit.begin(), bit.end(), ']'), bit.end());
    return bit;
  };
  m_entrees.assign(be.size(), 0);
  m_sorties.assign(bs.size(), 0);
  for (const auto& b : d->broches) {
    const std::string r = k.nom(x->uuid, b.numero);
    if (b.nom == "VCC") {
      if (!r.empty()) m_vecteurVcc = vecteurTension(n, r);
      continue;
    }
    for (const auto& bit : be)
      if (nomBroche(bit) == b.nom) m_broches.push_back({b.numero, bit, r.empty() ? std::string() : vecteurTension(n, r), false});
    for (const auto& bit : bs)
      if (nomBroche(bit) == b.nom) m_broches.push_back({b.numero, bit, {}, true});
  }
}

bool CosimulationHdl::demarrer(const std::string& dossier, std::string* erreur) {
  if (!m_m) {
    if (erreur) *erreur = "composant « " + m_reference + " » sans module HDL lisible (champ HDL.Code)";
    return false;
  }
  m_pret = m_sim.demarrer(*m_m, dossier, erreur);
  if (m_pret)
    if (const auto s = m_sim.evaluer(m_entrees)) m_sorties = *s;
  return m_pret;
}

std::vector<std::string> CosimulationHdl::sondes() const {
  std::vector<std::string> v;
  for (const auto& b : m_broches)
    if (!b.sortie && !b.vecteur.empty()) v.push_back(b.vecteur);
  if (!m_vecteurVcc.empty()) v.push_back(m_vecteurVcc);
  return v;
}

void CosimulationHdl::pas(double, const std::map<std::string, double>& valeurs) {
  std::lock_guard<std::mutex> g(m_verrou);
  if (!m_pret || m_interrompu || !m_m) return;
  if (const auto it = valeurs.find(m_vecteurVcc); it != valeurs.end() && std::isfinite(it->second) && it->second > 0.5) m_vcc = it->second;
  const std::vector<std::string> be = m_m->bitsEntree();
  std::vector<int> e = m_entrees;
  for (const auto& b : m_broches) {
    if (b.sortie) continue;
    const auto it = valeurs.find(b.vecteur);
    if (it == valeurs.end() || !std::isfinite(it->second)) continue;
    const auto ib = std::find(be.begin(), be.end(), b.bit);
    if (ib == be.end()) continue;
    const std::size_t i = static_cast<std::size_t>(ib - be.begin());
    // Seuils CMOS avec hystérésis
    e[i] = e[i] ? (it->second > 0.3 * m_vcc ? 1 : 0) : (it->second > 0.6 * m_vcc ? 1 : 0);
  }
  if (e == m_entrees) return;
  m_entrees = e;
  if (const auto s = m_sim.evaluer(e)) {
    m_sorties = *s;
    ++m_evaluations;
  }
}

bool CosimulationHdl::source(const std::string& nom, double, double& valeur) {
  const std::string prefixe = "v" + minuscules(m_reference) + "_";
  if (nom.size() <= prefixe.size() + 1 || nom.compare(0, prefixe.size(), prefixe) != 0 || nom.back() != 'd') return false;
  const std::string numero = nom.substr(prefixe.size(), nom.size() - prefixe.size() - 1);
  std::lock_guard<std::mutex> g(m_verrou);
  if (!m_m) return false;
  const std::vector<std::string> bs = m_m->bitsSortie();
  for (const auto& b : m_broches) {
    if (!b.sortie || minuscules(b.numero) != numero) continue;
    const auto ib = std::find(bs.begin(), bs.end(), b.bit);
    const std::size_t i = static_cast<std::size_t>(ib - bs.begin());
    valeur = ib != bs.end() && i < m_sorties.size() && m_sorties[i] ? m_vcc : 0;
    return true;
  }
  return false;
}

void CosimulationHdl::interrompre() {
  std::lock_guard<std::mutex> g(m_verrou);
  m_interrompu = true;
  m_sim.arreter();
}

int CosimulationHdl::evaluations() const {
  std::lock_guard<std::mutex> g(m_verrou);
  return m_evaluations;
}

std::map<std::string, int> CosimulationHdl::sorties() const {
  std::lock_guard<std::mutex> g(m_verrou);
  std::map<std::string, int> r;
  if (!m_m) return r;
  const std::vector<std::string> bs = m_m->bitsSortie();
  for (std::size_t i = 0; i < bs.size() && i < m_sorties.size(); ++i) r[bs[i]] = m_sorties[i];
  return r;
}

// ======================================================================================================================
// Quine et McCluskey
// ======================================================================================================================
std::vector<Implicant> reduire(int n, const std::vector<std::uint32_t>& uns, const std::vector<std::uint32_t>& indifferents) {
  if (uns.empty()) return {};
  const std::uint32_t tout = n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1);
  // Implicants premiers : regroupements successifs des termes qui ne diffèrent que d'un bit
  std::set<std::pair<std::uint32_t, std::uint32_t>> courant, premiers;  // (valeur, masque)
  for (auto u : uns) courant.insert({u & tout, 0});
  for (auto u : indifferents) courant.insert({u & tout, 0});
  while (!courant.empty()) {
    std::set<std::pair<std::uint32_t, std::uint32_t>> suivant, utilises;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> v(courant.begin(), courant.end());
    for (std::size_t i = 0; i < v.size(); ++i)
      for (std::size_t j = i + 1; j < v.size(); ++j) {
        if (v[i].second != v[j].second) continue;
        const std::uint32_t d = v[i].first ^ v[j].first;
        if (d && !(d & (d - 1))) {
          suivant.insert({v[i].first & ~d, v[i].second | d});
          utilises.insert(v[i]);
          utilises.insert(v[j]);
        }
      }
    for (const auto& x : v)
      if (!utilises.count(x)) premiers.insert(x);
    courant = suivant;
  }
  std::vector<Implicant> p;
  for (const auto& [val, m] : premiers) p.push_back({val, m});
  auto couvre = [](const Implicant& i, std::uint32_t x) { return (x & ~i.masque) == (i.valeur & ~i.masque); };
  // Couverture : essentiels d'abord, puis le plus couvrant (glouton)
  std::set<std::uint32_t> reste(uns.begin(), uns.end());
  std::vector<Implicant> choisis;
  for (bool change = true; change && !reste.empty();) {
    change = false;
    for (auto x : std::vector<std::uint32_t>(reste.begin(), reste.end())) {
      if (!reste.count(x)) continue;
      const Implicant* seul = nullptr;
      int n2 = 0;
      for (const auto& i : p)
        if (couvre(i, x)) ++n2, seul = &i;
      if (n2 == 1 && std::find(choisis.begin(), choisis.end(), *seul) == choisis.end()) {
        choisis.push_back(*seul);
        for (auto y : std::vector<std::uint32_t>(reste.begin(), reste.end()))
          if (couvre(*seul, y)) reste.erase(y);
        change = true;
      }
    }
  }
  while (!reste.empty()) {
    const Implicant* meilleur = nullptr;
    int nMeilleur = 0;
    for (const auto& i : p) {
      int k = 0;
      for (auto x : reste) k += couvre(i, x);
      if (k > nMeilleur || (k == nMeilleur && meilleur && std::popcount(i.masque) > std::popcount(meilleur->masque))) nMeilleur = k, meilleur = &i;
    }
    if (!meilleur) break;
    choisis.push_back(*meilleur);
    for (auto y : std::vector<std::uint32_t>(reste.begin(), reste.end()))
      if (couvre(*meilleur, y)) reste.erase(y);
  }
  std::sort(choisis.begin(), choisis.end(), [](const Implicant& a, const Implicant& b) { return a.valeur != b.valeur ? a.valeur < b.valeur : a.masque < b.masque; });
  return choisis;
}

bool evaluerProduits(const std::vector<Implicant>& p, std::uint32_t e) {
  for (const auto& i : p)
    if ((e & ~i.masque) == (i.valeur & ~i.masque)) return true;
  return false;
}

std::string sommeDeProduits(const std::vector<Implicant>& p, const std::vector<std::string>& noms, int n) {
  if (p.empty()) return "0";
  std::string s;
  for (const auto& i : p) {
    std::string t;
    for (int b = n - 1; b >= 0; --b) {
      if (i.masque & (1u << b)) continue;
      t += (t.empty() ? "" : "·") + std::string((i.valeur & (1u << b)) ? "" : "/") + (b < static_cast<int>(noms.size()) ? noms[static_cast<std::size_t>(b)] : "x" + std::to_string(b));
    }
    if (t.empty()) return "1";
    s += (s.empty() ? "" : " + ") + t;
  }
  return s;
}

// ======================================================================================================================
// Machines à états
// ======================================================================================================================
namespace {
bool evaluerReceptivite(const prom::elec::Receptivite& r, const std::map<std::string, bool>& e) {
  using T = prom::elec::Receptivite::Type;
  switch (r.type) {
    case T::Vrai: return true;
    case T::Faux: return false;
    case T::Variable: {
      const auto it = e.find(r.nom);
      return it != e.end() && it->second;
    }
    case T::Non: return !r.enfants.empty() && !evaluerReceptivite(r.enfants[0], e);
    case T::Et:
      for (const auto& x : r.enfants)
        if (!evaluerReceptivite(x, e)) return false;
      return true;
    case T::Ou:
      for (const auto& x : r.enfants)
        if (evaluerReceptivite(x, e)) return true;
      return false;
    default: return false;
  }
}
std::string vhdlReceptivite(const prom::elec::Receptivite& r) {
  using T = prom::elec::Receptivite::Type;
  switch (r.type) {
    case T::Vrai: return "true";
    case T::Faux: return "false";
    case T::Variable: return "(" + identifiant(r.nom) + " = '1')";
    case T::Non: return r.enfants.empty() ? "false" : "(not " + vhdlReceptivite(r.enfants[0]) + ")";
    case T::Et:
    case T::Ou: {
      std::string s;
      for (const auto& x : r.enfants) s += (s.empty() ? "" : r.type == T::Et ? " and " : " or ") + vhdlReceptivite(x);
      return "(" + s + ")";
    }
    default: return "false";
  }
}
std::vector<std::string> variablesDe(const std::string& condition) {
  std::vector<std::string> v;
  const auto l = prom::elec::lireReceptivite(condition);
  if (l.expression) prom::elec::variablesReceptivite(*l.expression, v);
  return v;
}
}  // namespace

bool conditionVraie(const std::string& condition, const std::map<std::string, bool>& entrees) {
  const auto l = prom::elec::lireReceptivite(condition.empty() ? "1" : condition);
  return l.expression && evaluerReceptivite(*l.expression, entrees);
}

std::optional<MachineEtats> machineDepuisGrafcet(const prom::elec::Grafcet& g, std::string* erreur) {
  auto refuser = [&](const std::string& e) -> std::optional<MachineEtats> {
    if (erreur) *erreur = e;
    return std::nullopt;
  };
  MachineEtats m;
  m.nom = g.nom;
  std::map<int, int> indice;
  int initiales = 0;
  for (const auto& e : g.etapes) {
    indice[e.numero] = static_cast<int>(m.etats.size());
    EtatMachine x;
    x.nom = "X" + std::to_string(e.numero);
    for (const auto& a : e.actions)
      if (a.type == prom::elec::ActionGrafcet::Type::Continue && a.condition.empty()) x.sorties.push_back(a.sortie);
    if (e.initiale) {
      m.initial = static_cast<int>(m.etats.size());
      ++initiales;
    }
    m.etats.push_back(x);
  }
  if (initiales != 1) return refuser("une machine à états demande une seule étape initiale");
  std::set<std::string> entrees, sorties;
  for (const auto& t : g.transitions) {
    if (t.amont.size() != 1 || t.aval.size() != 1) return refuser("transition en ET (parallélisme) : le GRAFCET n'est pas une machine à états");
    if (!indice.count(t.amont[0]) || !indice.count(t.aval[0])) return refuser("transition vers une étape absente");
    const auto l = prom::elec::lireReceptivite(t.receptivite);
    if (!l.expression) return refuser("réceptivité illisible : « " + t.receptivite + " »");
    m.transitions.push_back({indice[t.amont[0]], indice[t.aval[0]], t.receptivite});
    for (const auto& v : variablesDe(t.receptivite)) entrees.insert(v);
  }
  for (const auto& e : m.etats)
    for (const auto& s : e.sorties) sorties.insert(s);
  m.entrees.assign(entrees.begin(), entrees.end());
  m.sorties.assign(sorties.begin(), sorties.end());
  return m;
}

prom::elec::Grafcet grafcetDepuisMachine(const MachineEtats& m) {
  prom::elec::Grafcet g;
  g.nom = m.nom;
  for (std::size_t i = 0; i < m.etats.size(); ++i) {
    prom::elec::EtapeGrafcet e;
    e.numero = static_cast<int>(i) + 1;
    e.initiale = static_cast<int>(i) == m.initial;
    e.commentaire = m.etats[i].nom;
    for (const auto& s : m.etats[i].sorties) {
      prom::elec::ActionGrafcet a;
      a.sortie = s;
      e.actions.push_back(a);
    }
    g.etapes.push_back(e);
  }
  for (const auto& t : m.transitions) {
    prom::elec::TransitionGrafcet x;
    x.amont = {t.de + 1};
    x.aval = {t.vers + 1};
    x.receptivite = t.condition.empty() ? "1" : t.condition;
    g.transitions.push_back(x);
  }
  return g;
}

MachineEtats machineFeuxTricolores() {
  MachineEtats m;
  m.nom = "feux";
  m.etats = {{"R1", {"rouge"}}, {"R2", {"rouge"}}, {"R3", {"rouge"}}, {"V1", {"vert"}}, {"V2", {"vert"}}, {"O1", {"orange"}}};
  m.initial = 0;
  m.transitions = {{0, 1, "1"}, {1, 2, "1"}, {2, 3, "1"}, {3, 4, "1"}, {4, 5, "pieton"}, {5, 0, "1"}};
  m.entrees = {"pieton"};
  m.sorties = {"orange", "rouge", "vert"};
  return m;
}

SequenceSorties simulerMachine(const MachineEtats& m, const std::vector<std::map<std::string, bool>>& entrees, int periodes) {
  SequenceSorties r;
  int etat = m.initial;
  for (int p = 0; p < periodes; ++p) {
    std::map<std::string, bool> s;
    for (const auto& x : m.sorties) s[x] = false;
    if (etat >= 0 && etat < static_cast<int>(m.etats.size()))
      for (const auto& x : m.etats[static_cast<std::size_t>(etat)].sorties) s[x] = true;
    r.push_back(s);
    const std::map<std::string, bool> e = p < static_cast<int>(entrees.size()) ? entrees[static_cast<std::size_t>(p)] : std::map<std::string, bool>{};
    for (const auto& t : m.transitions)
      if (t.de == etat && conditionVraie(t.condition, e)) {
        etat = t.vers;
        break;
      }
  }
  return r;
}

std::vector<LigneTransition> tableTransitions(const MachineEtats& m) {
  std::vector<LigneTransition> v;
  for (const auto& t : m.transitions) {
    LigneTransition l;
    l.etat = m.etats[static_cast<std::size_t>(t.de)].nom;
    l.condition = t.condition.empty() ? "1" : t.condition;
    l.suivant = m.etats[static_cast<std::size_t>(t.vers)].nom;
    l.sorties = m.etats[static_cast<std::size_t>(t.de)].sorties;
    v.push_back(l);
  }
  return v;
}

EquationsMachine equationsMachine(const MachineEtats& m) {
  EquationsMachine q;
  // L'état initial reçoit le code 0 ; les autres, à la suite
  std::vector<int> code(m.etats.size());
  int k = 1;
  for (std::size_t i = 0; i < m.etats.size(); ++i) code[i] = static_cast<int>(i) == m.initial ? 0 : k++;
  int bits = 1;
  while ((1 << bits) < static_cast<int>(m.etats.size())) ++bits;
  q.bits = bits;
  for (int b = 0; b < bits; ++b) q.variables.push_back("q" + std::to_string(b));
  for (const auto& e : m.entrees) q.variables.push_back(e);
  const int n = static_cast<int>(q.variables.size());
  std::map<int, int> etatDuCode;
  for (std::size_t i = 0; i < code.size(); ++i) etatDuCode[code[i]] = static_cast<int>(i);
  std::map<std::string, std::vector<std::uint32_t>> unsD, unsS;
  std::vector<std::uint32_t> indiff;
  for (std::uint32_t x = 0; x < (1u << n); ++x) {
    const int c = static_cast<int>(x & ((1u << bits) - 1));
    const auto it = etatDuCode.find(c);
    if (it == etatDuCode.end()) {
      indiff.push_back(x);  // code inutilisé
      continue;
    }
    std::map<std::string, bool> e;
    for (std::size_t i = 0; i < m.entrees.size(); ++i) e[m.entrees[i]] = (x >> (bits + static_cast<int>(i))) & 1u;
    int suivant = it->second;
    for (const auto& t : m.transitions)
      if (t.de == it->second && conditionVraie(t.condition, e)) {
        suivant = t.vers;
        break;
      }
    for (int b = 0; b < bits; ++b)
      if ((code[static_cast<std::size_t>(suivant)] >> b) & 1) unsD["d" + std::to_string(b)].push_back(x);
    for (const auto& s : m.etats[static_cast<std::size_t>(it->second)].sorties) unsS[s].push_back(x);
  }
  for (int b = 0; b < bits; ++b) {
    const std::string d = "d" + std::to_string(b);
    q.d[d] = reduire(n, unsD[d], indiff);
    q.textes[d] = sommeDeProduits(q.d[d], q.variables, n);
  }
  for (const auto& s : m.sorties) {
    q.sorties[s] = reduire(n, unsS[s], indiff);
    q.textes[s] = sommeDeProduits(q.sorties[s], q.variables, n);
  }
  return q;
}

namespace {
// Pose d'un symbole relié par des étiquettes globales (numéro de broche → signal)
struct Poseur {
  ConceptionElec& c;
  double x = 20, y = 30;
  int n = 0;
  void poser(const std::string& symbole, const std::string& ref, const std::map<std::string, std::string>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, y));
    x += 30;
    if (x > 380) x = 20, y += 30;
    for (const auto& sp : c.feuilles[0].symboles) {
      if (sp.uuid != s) continue;
      if (ComposantElec* k = c.composant(sp.composant)) k->reference = ref;
      for (const auto& b : brochesPlacees(sp, *c.symbole(sp.symbole))) {
        const auto it = liaisons.find(b.broche->numero);
        if (it == liaisons.end()) continue;
        const Pt d = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
        const Pt bout{b.p.x - d.x * 2'540'000, b.p.y - d.y * 2'540'000};
        c.feuilles[0].fils.push_back({nouvelUuid(), b.p, bout});
        Etiquette e;
        e.uuid = nouvelUuid();
        e.texte = it->second;
        e.p = bout;
        e.type = TypeEtiquette::Globale;
        c.feuilles[0].etiquettes.push_back(e);
      }
    }
  }
};
}  // namespace

ConceptionElec circuitBascules(const MachineEtats& m, EquationsMachine* sortieEq) {
  const EquationsMachine q = equationsMachine(m);
  if (sortieEq) *sortieEq = q;
  ConceptionElec c = nouvelleConception("Machine " + m.nom + " en bascules");
  c.feuilles[0].format = "A3";
  Poseur p{c};
  int nPortes = 0, nInv = 0;
  std::set<std::string> inverses;
  auto inverse = [&](const std::string& v) {
    const std::string n = "n_" + v;
    if (inverses.insert(v).second) p.poser("Porte_NON", "UN" + std::to_string(++nInv), {{"1", v}, {"2", n}});
    return n;
  };
  auto porte = [&](const char* sym, const std::string& a, const std::string& b, const std::string& s) { p.poser(sym, "U" + std::to_string(++nPortes), {{"1", a}, {"2", b}, {"3", s}}); };
  int nSignal = 0;
  // Fonction en portes à deux entrées ; « sortie » : le nom du signal rendu
  auto fonction = [&](const std::vector<Implicant>& produits, const std::string& sortie) {
    const int n = static_cast<int>(q.variables.size());
    std::vector<std::string> termes;
    for (const auto& i : produits) {
      std::vector<std::string> lit;
      for (int b = 0; b < n; ++b)
        if (!(i.masque & (1u << b))) lit.push_back((i.valeur & (1u << b)) ? q.variables[static_cast<std::size_t>(b)] : inverse(q.variables[static_cast<std::size_t>(b)]));
      if (lit.empty()) lit.push_back("constante_1");
      std::string t = lit[0];
      for (std::size_t k = 1; k < lit.size(); ++k) {
        const std::string s = "p" + std::to_string(++nSignal);
        porte("Porte_ET", t, lit[k], s);
        t = s;
      }
      termes.push_back(t);
    }
    if (termes.empty()) termes.push_back("constante_0");
    std::string t = termes[0];
    for (std::size_t k = 1; k < termes.size(); ++k) {
      const std::string s = k + 1 == termes.size() ? sortie : "o" + std::to_string(++nSignal);
      porte("Porte_OU", t, termes[k], s);
      t = s;
    }
    if (t != sortie) porte("Porte_ET", t, t, sortie);  // le signal prend son nom (tampon)
  };
  // Bascules : D = f · /raz
  const std::string nraz = inverse("raz");
  for (int b = 0; b < q.bits; ++b) {
    const std::string d = "d" + std::to_string(b), f = "f" + std::to_string(b);
    fonction(q.d.at(d), f);
    porte("Porte_ET", f, nraz, d);
    p.poser("Bascule_D", "FF" + std::to_string(b + 1), {{"1", d}, {"2", "horloge"}, {"3", "q" + std::to_string(b)}});
  }
  for (const auto& s : m.sorties) fonction(q.sorties.at(s), s);
  return c;
}

SequenceSorties simulerCircuitBascules(const ConceptionElec& c, const MachineEtats& m, const std::vector<std::map<std::string, bool>>& entrees, int periodes) {
  SequenceSorties r;
  SimulateurLogique sim(circuitLogique(c));
  const EquationsMachine q = equationsMachine(m);
  const double T = 1e-6;
  sim.forcer("constante_0", Niveau::Bas);
  sim.forcer("constante_1", Niveau::Haut);
  sim.forcer("raz", Niveau::Bas);
  sim.forcer("horloge", Niveau::Bas);
  for (int b = 0; b < q.bits; ++b) sim.forcer("q" + std::to_string(b), Niveau::Bas);  // mise sous tension dans l'état initial
  for (int p = 0; p < periodes; ++p) {
    const std::map<std::string, bool> e = p < static_cast<int>(entrees.size()) ? entrees[static_cast<std::size_t>(p)] : std::map<std::string, bool>{};
    for (const auto& x : m.entrees) {
      const auto it = e.find(x);
      sim.forcer(x, it != e.end() && it->second ? Niveau::Haut : Niveau::Bas);
    }
    sim.forcer("horloge", Niveau::Bas);
    sim.avancer(p * T + 0.45 * T);
    std::map<std::string, bool> s;
    for (const auto& x : m.sorties) s[x] = sim.niveau(x) == Niveau::Haut;
    r.push_back(s);
    sim.avancer(p * T + 0.5 * T);
    sim.forcer("horloge", Niveau::Haut);
    sim.avancer((p + 1) * T - 1e-9);
  }
  return r;
}

std::string vhdlMachine(const MachineEtats& m) {
  std::ostringstream o;
  const std::string ent = identifiant(m.nom);
  o << "-- Écrit par Prométhée : machine à états « " << m.nom << " » (Moore), remise à zéro synchrone\n";
  o << "library ieee;\nuse ieee.std_logic_1164.all;\n\nentity " << ent << " is\n  port (\n    horloge, raz : in std_logic";
  for (const auto& e : m.entrees) o << ";\n    " << identifiant(e) << " : in std_logic";
  for (const auto& s : m.sorties) o << ";\n    " << identifiant(s) << " : out std_logic";
  o << "\n  );\nend entity;\n\narchitecture machine of " << ent << " is\n  type etat_t is (";
  for (std::size_t i = 0; i < m.etats.size(); ++i) o << (i ? ", " : "") << "e_" << identifiant(m.etats[i].nom);
  o << ");\n  signal etat : etat_t := e_" << identifiant(m.etats[static_cast<std::size_t>(m.initial)].nom) << ";\nbegin\n";
  o << "  process (horloge)\n  begin\n    if rising_edge(horloge) then\n      if raz = '1' then\n        etat <= e_" << identifiant(m.etats[static_cast<std::size_t>(m.initial)].nom) << ";\n      else\n        case etat is\n";
  for (std::size_t i = 0; i < m.etats.size(); ++i) {
    o << "          when e_" << identifiant(m.etats[i].nom) << " =>\n";
    bool premier = true;
    for (const auto& t : m.transitions) {
      if (t.de != static_cast<int>(i)) continue;
      const auto l = prom::elec::lireReceptivite(t.condition.empty() ? "1" : t.condition);
      const std::string cond = l.expression ? vhdlReceptivite(*l.expression) : "false";
      o << "            " << (premier ? "if " : "elsif ") << cond << " then\n              etat <= e_" << identifiant(m.etats[static_cast<std::size_t>(t.vers)].nom) << ";\n";
      premier = false;
    }
    if (!premier) o << "            end if;\n";
    else o << "            null;\n";
  }
  o << "        end case;\n      end if;\n    end if;\n  end process;\n";
  for (const auto& s : m.sorties) {
    std::string cond;
    for (const auto& e : m.etats)
      if (std::find(e.sorties.begin(), e.sorties.end(), s) != e.sorties.end()) cond += (cond.empty() ? "" : " or ") + std::string("etat = e_") + identifiant(e.nom);
    o << "  " << identifiant(s) << " <= " << (cond.empty() ? std::string("'0'") : "'1' when " + cond + " else '0'") << ";\n";
  }
  o << "end architecture;\n";
  return o.str();
}

std::string bancVhdlMachine(const MachineEtats& m, const std::vector<std::map<std::string, bool>>& entrees, int periodes) {
  std::ostringstream o;
  const std::string ent = identifiant(m.nom);
  o << "-- Banc d'essai écrit par Prométhée : une ligne de sorties par période, avant le front de l'horloge\n";
  o << "library ieee;\nuse ieee.std_logic_1164.all;\nuse std.textio.all;\nentity banc_" << ent << " is\nend entity;\narchitecture banc of banc_" << ent << " is\n";
  o << "  signal horloge, raz : std_logic := '0';\n";
  for (const auto& e : m.entrees) o << "  signal " << identifiant(e) << " : std_logic := '0';\n";
  for (const auto& s : m.sorties) o << "  signal " << identifiant(s) << " : std_logic;\n";
  for (const auto& e : m.entrees) {
    o << "  constant suite_" << identifiant(e) << " : string(1 to " << std::max(1, periodes) << ") := \"";
    for (int p = 0; p < std::max(1, periodes); ++p) {
      const std::map<std::string, bool> x = p < static_cast<int>(entrees.size()) ? entrees[static_cast<std::size_t>(p)] : std::map<std::string, bool>{};
      const auto it = x.find(e);
      o << (it != x.end() && it->second ? '1' : '0');
    }
    o << "\";\n";
  }
  o << "begin\n  dut : entity work." << ent << " port map (horloge => horloge, raz => raz";
  for (const auto& e : m.entrees) o << ", " << identifiant(e) << " => " << identifiant(e);
  for (const auto& s : m.sorties) o << ", " << identifiant(s) << " => " << identifiant(s);
  o << ");\n  process\n    variable l : line;\n  begin\n    raz <= '1';\n    wait for 5 ns;\n    horloge <= '1';\n    wait for 5 ns;\n    raz <= '0';\n";
  o << "    for p in 1 to " << periodes << " loop\n";
  for (const auto& e : m.entrees) o << "      if suite_" << identifiant(e) << "(p) = '1' then " << identifiant(e) << " <= '1'; else " << identifiant(e) << " <= '0'; end if;\n";
  o << "      horloge <= '0';\n      wait for 4 ns;\n";
  for (const auto& s : m.sorties) o << "      write(l, string'(\"" << s << "=\"));\n      if " << identifiant(s) << " = '1' then write(l, string'(\"1 \")); else write(l, string'(\"0 \")); end if;\n";
  o << "      writeline(output, l);\n      wait for 1 ns;\n      horloge <= '1';\n      wait for 5 ns;\n    end loop;\n    wait;\n  end process;\nend architecture;\n";
  return o.str();
}

ResultatGhdl simulerGhdl(const std::map<std::string, std::string>& fichiers, const std::string& entite, const std::string& dossier, double delai) {
  ResultatGhdl r;
  if (programmeDuPoste("ghdl").empty()) {
    r.journal = "GHDL manque sur ce poste";
    return r;
  }
  std::error_code ec;
  const fs::path d = cheminFichier(dossier);
  fs::create_directories(d, ec);
  OptionsProcessus o;
  o.dossier = utf8(d);
  o.delai = delai;
  std::vector<std::string> noms;
  for (const auto& [nom, texte] : fichiers) {
    ecrireFichierAtomique(d / cheminFichier(nom), texte);
    noms.push_back(nom);
  }
  auto lancer = [&](std::vector<std::string> cmd) {
    const ResultatProcessus x = executerProcessus(cmd, o);
    r.journal += x.sortie + x.erreurs;
    return x;
  };
  // Analyse dans l'ordre des dépendances : les entités avant les bancs qui les instancient (« banc » en dernier)
  std::sort(noms.begin(), noms.end(), [](const std::string& a, const std::string& b) {
    const bool ba = a.find("banc") != std::string::npos, bb = b.find("banc") != std::string::npos;
    return ba != bb ? bb : a < b;
  });
  for (const auto& n : noms)
    if (!lancer({"ghdl", "-a", "--std=08", n}).reussi()) return r;
  if (!lancer({"ghdl", "-e", "--std=08", entite}).reussi()) return r;
  const ResultatProcessus x = lancer({"ghdl", "-r", "--std=08", entite});
  r.ok = x.reussi();
  r.lignes = lignesDe(x.sortie);
  return r;
}

SequenceSorties lireSequenceGhdl(const std::vector<std::string>& lignes, const std::vector<std::string>& sorties) {
  SequenceSorties r;
  for (const auto& l : lignes) {
    std::map<std::string, bool> s;
    bool trouve = false;
    std::istringstream in(l);
    for (std::string mot; in >> mot;) {
      const std::size_t e = mot.find('=');
      if (e == std::string::npos) continue;
      s[mot.substr(0, e)] = mot.substr(e + 1) == "1";
      trouve = true;
    }
    if (!trouve) continue;
    for (const auto& x : sorties) s.emplace(x, false);
    r.push_back(s);
  }
  return r;
}

// ======================================================================================================================
// Schéma à contacts
// ======================================================================================================================
bool AutomateContacts::valeur(const std::string& v) const {
  const auto it = m_v.find(v);
  return it != m_v.end() && it->second;
}
int AutomateContacts::compte(const std::string& c) const {
  const auto it = m_compte.find(c);
  return it == m_compte.end() ? 0 : it->second;
}
void AutomateContacts::cycle(double dt) {
  const std::map<std::string, bool> image = m_v;  // image des entrées en début de cycle
  for (const auto& e : m_p.echelons) {
    bool r = e.branches.empty();
    for (const auto& b : e.branches) {
      bool s = true;
      for (const auto& x : b) {
        const auto it = m_v.find(x.variable);
        const bool v = it != m_v.end() && it->second;
        s = s && (x.genre == "contactInverse" ? !v : v);
      }
      r = r || s;
    }
    const ElementContacts& o = e.sortie;
    if (o.genre == "bobine") m_v[o.variable] = r;
    else if (o.genre == "miseA1") {
      if (r) m_v[o.variable] = true;
    } else if (o.genre == "miseA0") {
      if (r) m_v[o.variable] = false;
    } else if (o.genre == "tempo") {
      double& t = m_tempo[o.variable];
      t = r ? t + dt : 0;
      m_v[o.variable] = r && t >= o.duree - 1e-12;
    } else if (o.genre == "compteur") {
      const bool avant = m_avant[o.variable];
      if (r && !avant) ++m_compte[o.variable];
      m_avant[o.variable] = r;
      m_v[o.variable] = m_compte[o.variable] >= o.consigne;
    }
  }
  (void)image;
}

// ======================================================================================================================
// Logique programmable
// ======================================================================================================================
std::string vhdlStructurel(const CircuitLogique& c, const std::string& entite) {
  std::vector<int> pilotes(c.signaux.size(), 0);
  for (const auto& p : c.portes)
    for (int s : p.sorties)
      if (s >= 0) pilotes[static_cast<std::size_t>(s)]++;
  std::map<int, std::string> noms;
  std::set<std::string> pris;
  for (std::size_t i = 0; i < c.signaux.size(); ++i) {
    std::string n = identifiant(c.signaux[i]);
    for (int k = 2; pris.count(minuscules(n)); ++k) n = identifiant(c.signaux[i]) + "_" + std::to_string(k);
    pris.insert(minuscules(n));
    noms[static_cast<int>(i)] = n;
  }
  const auto libre = [&](std::size_t i) { return c.signaux[i].rfind("nc_", 0) == 0; };
  std::vector<std::string> entrees, sorties, fils;
  for (std::size_t i = 0; i < c.signaux.size(); ++i) {
    if (libre(i)) continue;
    const bool nomme = c.signaux[i].find('(') == std::string::npos && c.signaux[i].rfind("Net-", 0) != 0 && c.signaux[i].rfind("N$", 0) != 0;
    if (pilotes[i] == 0) entrees.push_back(noms[static_cast<int>(i)]);
    else if (nomme) sorties.push_back(noms[static_cast<int>(i)]);
    else fils.push_back(noms[static_cast<int>(i)]);
  }
  std::ostringstream o;
  o << "-- Écrit par Prométhée : circuit logique de la conception, en VHDL structurel\nlibrary ieee;\nuse ieee.std_logic_1164.all;\n\nentity " << identifiant(entite) << " is\n  port (\n";
  std::vector<std::string> ports;
  for (const auto& e : entrees) ports.push_back("    " + e + " : in std_logic");
  for (const auto& s : sorties) ports.push_back("    " + s + " : out std_logic");
  for (std::size_t i = 0; i < ports.size(); ++i) o << ports[i] << (i + 1 < ports.size() ? ";\n" : "\n");
  o << "  );\nend entity;\n\narchitecture structure of " << identifiant(entite) << " is\n";
  // Les sorties relues à l'intérieur passent par un signal interne
  for (const auto& f : fils) o << "  signal " << f << " : std_logic;\n";
  for (const auto& s : sorties) o << "  signal i_" << s << " : std_logic;\n";
  o << "begin\n";
  auto nom = [&](int s) {
    if (s < 0 || libre(static_cast<std::size_t>(s))) return std::string("'Z'");
    const std::string n = noms[s];
    return std::find(sorties.begin(), sorties.end(), n) != sorties.end() ? "i_" + n : n;
  };
  for (const auto& p : c.portes) {
    if (p.sorties.empty() || p.sorties[0] < 0) continue;
    const std::string y = nom(p.sorties[0]);
    if (p.genre == "D") {
      o << "  process (" << nom(p.entrees[1]) << ")\n  begin\n    if rising_edge(" << nom(p.entrees[1]) << ") then\n      " << y << " <= " << nom(p.entrees[0]) << ";\n    end if;\n  end process;\n";
      if (p.sorties.size() > 1 && p.sorties[1] >= 0) o << "  " << nom(p.sorties[1]) << " <= not " << y << ";\n";
      continue;
    }
    static const std::map<std::string, std::string> op = {{"ET", "and"}, {"OU", "or"}, {"NON_ET", "nand"}, {"NON_OU", "nor"}, {"OU_X", "xor"}};
    if (p.genre == "NON") o << "  " << y << " <= not " << nom(p.entrees[0]) << ";\n";
    else if (p.genre == "TAMPON") o << "  " << y << " <= " << nom(p.entrees[0]) << ";\n";
    else if (const auto it = op.find(p.genre); it != op.end() && p.entrees.size() >= 2) {
      // nand et nor ne s'enchaînent pas : écrits « not (a and b) »
      const bool inverse = p.genre == "NON_ET" || p.genre == "NON_OU";
      const std::string base = inverse ? (p.genre == "NON_ET" ? "and" : "or") : it->second;
      std::string e = nom(p.entrees[0]);
      for (std::size_t k = 1; k < p.entrees.size(); ++k) e += " " + base + " " + nom(p.entrees[k]);
      o << "  " << y << " <= " << (inverse ? "not (" + e + ")" : e) << ";\n";
    }
  }
  for (const auto& s : sorties) o << "  " << s << " <= i_" << s << ";\n";
  o << "end architecture;\n";
  return o.str();
}

std::string bancTableVerite(const std::string& entite0, const std::vector<std::string>& entrees, const std::vector<std::string>& sorties) {
  const std::string entite = identifiant(entite0);
  std::ostringstream o;
  o << "-- Banc d'essai écrit par Prométhée : toutes les combinaisons des entrées, une ligne « entrées sorties »\n";
  o << "library ieee;\nuse ieee.std_logic_1164.all;\nuse ieee.numeric_std.all;\nuse std.textio.all;\nentity banc_" << entite << " is\nend entity;\narchitecture banc of banc_" << entite << " is\n";
  for (const auto& e : entrees) o << "  signal " << identifiant(e) << " : std_logic := '0';\n";
  for (const auto& s : sorties) o << "  signal " << identifiant(s) << " : std_logic;\n";
  o << "begin\n  dut : entity work." << entite << " port map (";
  bool premier = true;
  for (const auto& e : entrees) o << (premier ? "" : ", ") << identifiant(e) << " => " << identifiant(e), premier = false;
  for (const auto& s : sorties) o << (premier ? "" : ", ") << identifiant(s) << " => " << identifiant(s), premier = false;
  o << ");\n  process\n    variable l : line;\n    variable v : unsigned(" << std::max<std::size_t>(1, entrees.size()) - 1 << " downto 0);\n  begin\n";
  o << "    for i in 0 to " << (1 << entrees.size()) - 1 << " loop\n      v := to_unsigned(i, " << std::max<std::size_t>(1, entrees.size()) << ");\n";
  // La première entrée est le bit de poids fort, comme dans tableVerite
  for (std::size_t k = 0; k < entrees.size(); ++k) o << "      " << identifiant(entrees[k]) << " <= v(" << entrees.size() - 1 - k << ");\n";
  o << "      wait for 10 ns;\n";
  for (std::size_t k = 0; k < entrees.size(); ++k) o << "      if " << identifiant(entrees[k]) << " = '1' then write(l, character'('1')); else write(l, character'('0')); end if;\n";
  o << "      write(l, character'(' '));\n";
  for (const auto& s : sorties) o << "      if " << identifiant(s) << " = '1' then write(l, character'('1')); elsif " << identifiant(s) << " = '0' then write(l, character'('0')); else write(l, character'('X')); end if;\n";
  o << "      writeline(output, l);\n    end loop;\n    wait;\n  end process;\nend architecture;\n";
  return o.str();
}

std::vector<std::pair<std::string, std::string>> tableVeriteGhdl(const CircuitLogique& c, const std::vector<std::string>& entrees, const std::vector<std::string>& sorties, const std::string& dossier,
                                                                 std::string* erreur) {
  std::vector<std::pair<std::string, std::string>> t;
  const std::string entite = "circuit";
  const ResultatGhdl r = simulerGhdl({{"circuit.vhd", vhdlStructurel(c, entite)}, {"banc_circuit.vhd", bancTableVerite(entite, entrees, sorties)}}, "banc_" + entite, dossier);
  if (!r.ok) {
    if (erreur) *erreur = r.journal;
    return t;
  }
  for (const auto& l : r.lignes) {
    std::istringstream in(l);
    std::string a, b;
    if (in >> a >> b && a.size() == entrees.size()) t.emplace_back(a, b);
  }
  return t;
}

SyntheseYosys synthetiserYosys(const std::string& verilog, const std::string& module, const std::string& dossier) {
  SyntheseYosys r;
  if (programmeDuPoste("yosys").empty()) {
    r.journal = "Yosys manque sur ce poste";
    return r;
  }
  std::error_code ec;
  const fs::path d = cheminFichier(dossier);
  fs::create_directories(d, ec);
  ecrireFichierAtomique(d / "circuit.v", verilog);
  OptionsProcessus o;
  o.dossier = utf8(d);
  o.delai = 120;
  const ResultatProcessus x = executerProcessus({"yosys", "-p", "read_verilog circuit.v; synth -top " + module + "; stat"}, o);
  r.journal = x.sortie + x.erreurs;
  r.ok = x.reussi();
  static const std::regex cellules(R"(Number of cells:\s+(\d+))");
  std::smatch m;
  if (std::regex_search(r.journal, m, cellules)) r.cellules = std::stoi(m[1].str());
  return r;
}

// ---------- GAL16V8 en mode simple ----------
namespace {
// Colonnes du réseau ET du GAL16V8 en mode simple (vraie ; complément = +1), par broche ; −1 : pas d'entrée
int colonneGal(int broche) {
  static const int c[21] = {-1, 2, 0, 4, 8, 12, 16, 20, 24, 28, -1, 30, 26, 22, 18, -1, -1, 14, 10, 6, -1};
  return broche >= 1 && broche <= 20 ? c[broche] : -1;
}
// Première ligne de produits de la macrocellule d'une broche de sortie (19 : lignes 0 à 7 … 12 : 56 à 63)
int ligneGal(int broche) { return (19 - broche) * 8; }
constexpr int FUSIBLES_GAL16V8 = 2194;
}  // namespace

PlacementGal placerGal16V8(const std::vector<std::string>& entrees, const std::map<std::string, std::vector<Implicant>>& equations, const std::string& nom) {
  PlacementGal r;
  static const int brochesEntree[] = {2, 3, 4, 5, 6, 7, 8, 9, 1, 11};
  static const int brochesSortie[] = {19, 18, 17, 16, 15, 14, 13, 12};
  if (entrees.size() > 10) r.erreurs.push_back("trop d'entrées pour le GAL16V8 en mode simple (10 au plus)");
  if (equations.size() > 8) r.erreurs.push_back("trop de sorties pour le GAL16V8 (8 au plus)");
  if (!r.erreurs.empty()) return r;
  for (std::size_t i = 0; i < entrees.size(); ++i) r.brochesEntrees[entrees[i]] = brochesEntree[i];
  std::vector<char> f(FUSIBLES_GAL16V8, '0');  // réseau ET : tous les fusibles intacts, produits faux
  std::size_t k = 0;
  for (const auto& [sortie, produits] : equations) {
    const int b = brochesSortie[k++];
    r.brochesSorties[sortie] = b;
    if (produits.size() > 7) {
      r.erreurs.push_back(sortie + " : " + std::to_string(produits.size()) + " produits, la macrocellule en a 7 en mode simple");
      continue;
    }
    const int l0 = ligneGal(b);
    for (std::size_t p = 0; p < produits.size(); ++p) {
      const int ligne = l0 + 1 + static_cast<int>(p);  // la première ligne, l'activation de la sortie, n'est pas utilisée en mode simple
      for (int c = 0; c < 32; ++c) f[static_cast<std::size_t>(ligne * 32 + c)] = '1';  // tous coupés : produit vrai
      for (std::size_t v = 0; v < entrees.size(); ++v) {
        if (produits[p].masque & (1u << v)) continue;
        const int col = colonneGal(brochesEntree[v]) + ((produits[p].valeur & (1u << v)) ? 0 : 1);
        f[static_cast<std::size_t>(ligne * 32 + col)] = '0';  // littéral relié
      }
    }
    const int olmc = 19 - b;
    f[static_cast<std::size_t>(2048 + olmc)] = '1';  // XOR : sortie au niveau haut
    f[static_cast<std::size_t>(2120 + olmc)] = '0';  // AC1 : sortie combinatoire
  }
  // Macrocellules restantes en entrée (AC1 = 1), produits validés (PTD), mode simple (SYN = 1, AC0 = 0)
  for (int o = 0; o < 8; ++o) {
    bool utilisee = false;
    for (const auto& [s, b] : r.brochesSorties) utilisee |= 19 - b == o;
    if (!utilisee) f[static_cast<std::size_t>(2120 + o)] = '1';
  }
  for (int p = 0; p < 64; ++p) f[static_cast<std::size_t>(2128 + p)] = '1';
  f[2192] = '1';
  f[2193] = '0';
  // Signature : huit octets du nom
  for (int i = 0; i < 8; ++i) {
    const unsigned char ch = i < static_cast<int>(nom.size()) ? static_cast<unsigned char>(nom[static_cast<std::size_t>(i)]) : 0;
    for (int b = 0; b < 8; ++b) f[static_cast<std::size_t>(2056 + i * 8 + b)] = (ch >> (7 - b)) & 1 ? '1' : '0';
  }
  // JEDEC : en-tête, fusibles par lignes de 32, somme des fusibles, somme du fichier
  std::ostringstream j;
  j << "\x02" << "Prométhée : GAL16V8 en mode simple\n*QP20*QF" << FUSIBLES_GAL16V8 << "*QV0*F0*G0*\n";
  for (int a = 0; a < FUSIBLES_GAL16V8; a += 32) {
    j << "L" << std::string(5 - std::min<std::size_t>(5, std::to_string(a).size()), '0') << a << " ";
    for (int b = a; b < std::min(a + 32, FUSIBLES_GAL16V8); ++b) j << f[static_cast<std::size_t>(b)];
    j << "*\n";
  }
  unsigned somme = 0;
  for (int a = 0; a < FUSIBLES_GAL16V8; a += 8) {
    unsigned octet = 0;
    for (int b = 0; b < 8 && a + b < FUSIBLES_GAL16V8; ++b)
      if (f[static_cast<std::size_t>(a + b)] == '1') octet |= 1u << b;
    somme = (somme + octet) & 0xFFFF;
  }
  char cs[8];
  std::snprintf(cs, sizeof cs, "%04X", somme);
  j << "C" << cs << "*\n\x03";
  std::string texte = j.str();
  unsigned sf = 0;
  for (unsigned char ch : texte) sf = (sf + ch) & 0xFFFF;
  std::snprintf(cs, sizeof cs, "%04X", sf);
  texte += cs;
  texte += "\n";
  r.jedec = texte;
  r.ok = r.erreurs.empty();
  return r;
}

std::optional<FusiblesJedec> lireJedec(const std::string& texte) {
  FusiblesJedec r;
  // Champs séparés par « * » : QF (nombre de fusibles), L (adresse puis fusibles), C (somme des fusibles)
  std::vector<std::string> champs;
  for (std::size_t d = 0; d < texte.size();) {
    std::size_t f = texte.find('*', d);
    if (f == std::string::npos) f = texte.size();
    std::string c = texte.substr(d, f - d);
    const std::size_t k = c.find_first_not_of(" \t\r\n");
    champs.push_back(k == std::string::npos ? std::string() : c.substr(k));
    d = f + 1;
  }
  for (const auto& c : champs)
    if (c.rfind("QF", 0) == 0) r.nombre = std::atoi(c.c_str() + 2);
  if (r.nombre <= 0 || r.nombre > 100000) return std::nullopt;
  r.fusibles.assign(static_cast<std::size_t>(r.nombre), false);
  std::optional<unsigned> attendue;
  for (const auto& c : champs) {
    if (c.size() > 1 && c[0] == 'L' && std::isdigit(static_cast<unsigned char>(c[1]))) {
      std::size_t j = 1;
      int a = 0;
      while (j < c.size() && std::isdigit(static_cast<unsigned char>(c[j]))) a = a * 10 + (c[j++] - '0');
      for (; j < c.size(); ++j)
        if (c[j] == '0' || c[j] == '1') {
          if (a < r.nombre) r.fusibles[static_cast<std::size_t>(a)] = c[j] == '1';
          ++a;
        }
    } else if (c.size() >= 5 && c[0] == 'C' && std::isxdigit(static_cast<unsigned char>(c[1])))
      attendue = static_cast<unsigned>(std::stoul(c.substr(1, 4), nullptr, 16));
  }
  if (attendue) {
    unsigned somme = 0;
    for (int a = 0; a < r.nombre; a += 8) {
      unsigned octet = 0;
      for (int b = 0; b < 8 && a + b < r.nombre; ++b)
        if (r.fusibles[static_cast<std::size_t>(a + b)]) octet |= 1u << b;
      somme = (somme + octet) & 0xFFFF;
    }
    r.sommeJuste = somme == attendue;
  }
  return r;
}

bool evaluerGal16V8(const FusiblesJedec& f, int broche, const std::map<int, bool>& entrees) {
  if (f.nombre != FUSIBLES_GAL16V8 || broche < 12 || broche > 19) return false;
  const int l0 = ligneGal(broche);
  bool somme = false;
  for (int ligne = l0 + 1; ligne < l0 + 8; ++ligne) {
    if (!f.fusibles[static_cast<std::size_t>(2128 + ligne)]) continue;  // produit désactivé
    bool produit = true;
    for (int b = 1; b <= 20 && produit; ++b) {
      const int col = colonneGal(b);
      if (col < 0) continue;
      const auto it = entrees.find(b);
      const bool v = it != entrees.end() && it->second;
      // Fusible intact (0) : le signal entre dans le produit
      if (!f.fusibles[static_cast<std::size_t>(ligne * 32 + col)] && !v) produit = false;
      if (!f.fusibles[static_cast<std::size_t>(ligne * 32 + col + 1)] && v) produit = false;
    }
    somme = somme || produit;
  }
  const bool xorBit = f.fusibles[static_cast<std::size_t>(2048 + 19 - broche)];
  return xorBit ? somme : !somme;
}

}  // namespace prom::ecad

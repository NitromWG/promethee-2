// Prométhée : logique numérique (lot L15). Licence GPL-3.0-only.
#include "promethee/logique.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>

namespace prom::ecad {

namespace {
Niveau non(Niveau a) { return a == Niveau::Bas ? Niveau::Haut : a == Niveau::Haut ? Niveau::Bas : Niveau::X; }
Niveau et(const std::vector<Niveau>& v) {
  bool tous = true;
  for (Niveau x : v) {
    if (x == Niveau::Bas) return Niveau::Bas;
    tous &= x == Niveau::Haut;
  }
  return tous ? Niveau::Haut : Niveau::X;
}
Niveau ou(const std::vector<Niveau>& v) {
  bool tous = true;
  for (Niveau x : v) {
    if (x == Niveau::Haut) return Niveau::Haut;
    tous &= x == Niveau::Bas;
  }
  return tous ? Niveau::Bas : Niveau::X;
}
Niveau ouX(const std::vector<Niveau>& v) {
  bool impair = false;
  for (Niveau x : v) {
    if (x != Niveau::Bas && x != Niveau::Haut) return Niveau::X;
    impair ^= x == Niveau::Haut;
  }
  return impair ? Niveau::Haut : Niveau::Bas;
}
double retardDe(const ComposantElec& k) {
  const auto it = k.champs.find("Retard");
  if (it == k.champs.end()) return 10e-9;
  std::string t;
  for (char ch : it->second)
    if (ch != ' ') t += ch == ',' ? '.' : ch;
  char* fin = nullptr;
  const double v = std::strtod(t.c_str(), &fin);
  const std::string unite = fin ? std::string(fin) : std::string();
  const double f = unite.rfind("ps", 0) == 0 ? 1e-12 : unite.rfind("ns", 0) == 0 || unite == "n" ? 1e-9 : unite.rfind("us", 0) == 0 || unite.rfind("µs", 0) == 0 ? 1e-6 : 1e-9;
  return v > 0 ? v * f : 10e-9;
}
std::string identifiant(const std::string& nom) {
  std::string r;
  for (unsigned char ch : nom) r += std::isalnum(ch) || ch == '_' ? static_cast<char>(ch) : '_';
  if (r.empty() || std::isdigit(static_cast<unsigned char>(r[0]))) r = "n_" + r;
  return r;
}
}  // namespace

char lettreNiveau(Niveau n) { return n == Niveau::Bas ? '0' : n == Niveau::Haut ? '1' : n == Niveau::X ? 'X' : 'Z'; }

CircuitLogique circuitLogique(const ConceptionElec& c) {
  CircuitLogique r;
  const Connectivite k = connectivite(c);
  const auto signal = [&](const ComposantElec& x, const std::string& broche) {
    std::string nom = k.nom(x.uuid, broche);
    if (nom.empty()) nom = "nc_" + x.reference + "_" + broche;  // broche libre : un signal à elle, en haute impédance
    const auto it = r.indice.find(nom);
    if (it != r.indice.end()) return it->second;
    const int i = static_cast<int>(r.signaux.size());
    r.signaux.push_back(nom);
    r.indice[nom] = i;
    return i;
  };
  static const std::map<std::string, std::string> genres = {{"Porte_ET", "ET"},         {"Porte_OU", "OU"},         {"Porte_NON", "NON"},
                                                            {"Porte_NON_ET", "NON_ET"}, {"Porte_NON_OU", "NON_OU"}, {"Porte_OU_X", "OU_X"}};
  std::set<std::string> poses;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles) poses.insert(s.composant);
  for (const auto& x : c.composants) {
    if (!poses.count(x.uuid)) continue;
    const double retard = retardDe(x);
    if (const auto it = genres.find(x.symbole); it != genres.end()) {
      Porte p{x.reference, it->second, {}, {}, retard};
      if (it->second == "NON") {
        p.entrees = {signal(x, "1")};
        p.sorties = {signal(x, "2")};
      } else {
        p.entrees = {signal(x, "1"), signal(x, "2")};
        p.sorties = {signal(x, "3")};
      }
      r.portes.push_back(p);
    } else if (x.symbole == "Bascule_D") {
      Porte p{x.reference, "D", {signal(x, "1"), signal(x, "2")}, {signal(x, "3"), k.nom(x.uuid, "4").empty() ? -1 : signal(x, "4")}, retard};
      r.portes.push_back(p);
    } else if (x.symbole == "74HC00_unites") {
      static const char* broches[4][3] = {{"1", "2", "3"}, {"4", "5", "6"}, {"9", "10", "8"}, {"12", "13", "11"}};
      for (int u = 0; u < 4; ++u) {
        if (k.nom(x.uuid, broches[u][2]).empty()) continue;  // porte inemployée
        r.portes.push_back({x.reference + static_cast<char>('A' + u), "NON_ET", {signal(x, broches[u][0]), signal(x, broches[u][1])}, {signal(x, broches[u][2])}, retard});
      }
    }
  }
  std::vector<int> pilotes(r.signaux.size(), 0);
  for (const auto& p : r.portes)
    for (int s : p.sorties)
      if (s >= 0) pilotes[static_cast<std::size_t>(s)]++;
  for (std::size_t i = 0; i < r.signaux.size(); ++i) {
    if (pilotes[i] > 1) r.alertes.push_back("« " + r.signaux[i] + " » est piloté par " + std::to_string(pilotes[i]) + " sorties.");
    if (pilotes[i] == 0 && r.signaux[i].rfind("nc_", 0) != 0) r.entrees.push_back(r.signaux[i]);
  }
  std::sort(r.entrees.begin(), r.entrees.end(), ordreNaturel);
  return r;
}

SimulateurLogique::SimulateurLogique(CircuitLogique c) : m_c(std::move(c)) {
  m_niveaux.assign(m_c.signaux.size(), Niveau::X);
  m_prevu = m_niveaux;
  for (std::size_t i = 0; i < m_c.signaux.size(); ++i)
    if (m_c.signaux[i].rfind("nc_", 0) == 0) m_niveaux[i] = m_prevu[i] = Niveau::Z;
  m_lecteurs.assign(m_c.signaux.size(), {});
  for (std::size_t p = 0; p < m_c.portes.size(); ++p)
    for (int s : m_c.portes[p].entrees) m_lecteurs[static_cast<std::size_t>(s)].push_back(p);
  m_horlogesAvant.assign(m_c.portes.size(), Niveau::X);
}

void SimulateurLogique::planifier(double t, int s, Niveau v) {
  if (s < 0 || m_prevu[static_cast<std::size_t>(s)] == v) return;
  m_prevu[static_cast<std::size_t>(s)] = v;
  m_file.push_back({t, m_ordre++, s, v});
  std::push_heap(m_file.begin(), m_file.end(), [](const Evenement& a, const Evenement& b) { return a.t != b.t ? a.t > b.t : a.ordre > b.ordre; });
}

void SimulateurLogique::forcer(const std::string& signal, Niveau v) {
  const int s = m_c.signal(signal);
  if (s < 0) return;
  // Une entrée forcée s'impose, même si la même valeur était planifiée
  m_prevu[static_cast<std::size_t>(s)] = m_niveaux[static_cast<std::size_t>(s)] == v ? v : Niveau(static_cast<std::uint8_t>(255));
  planifier(m_t, s, v);
}

void SimulateurLogique::evaluer(std::size_t i) {
  const Porte& p = m_c.portes[i];
  std::vector<Niveau> e;
  for (int s : p.entrees) e.push_back(m_niveaux[static_cast<std::size_t>(s)]);
  m_actives.push_back(static_cast<int>(i));
  if (p.genre == "D") {
    const Niveau horloge = e.size() > 1 ? e[1] : Niveau::X;
    const bool front = m_horlogesAvant[i] == Niveau::Bas && horloge == Niveau::Haut;
    m_horlogesAvant[i] = horloge;
    if (!front) return;
    const Niveau q = e[0] == Niveau::Bas || e[0] == Niveau::Haut ? e[0] : Niveau::X;
    planifier(m_t + p.retard, p.sorties[0], q);
    if (p.sorties.size() > 1) planifier(m_t + p.retard, p.sorties[1], non(q));
    return;
  }
  Niveau v = Niveau::X;
  if (p.genre == "ET") v = et(e);
  else if (p.genre == "OU") v = ou(e);
  else if (p.genre == "NON") v = non(e.front());
  else if (p.genre == "NON_ET") v = non(et(e));
  else if (p.genre == "NON_OU") v = non(ou(e));
  else if (p.genre == "OU_X") v = ouX(e);
  else if (p.genre == "TAMPON") v = e.front() == Niveau::Bas || e.front() == Niveau::Haut ? e.front() : Niveau::X;
  for (int s : p.sorties) planifier(m_t + p.retard, s, v);
}

int SimulateurLogique::avancer(double jusqua) {
  int n = 0;
  m_actives.clear();
  const auto plusTot = [](const Evenement& a, const Evenement& b) { return a.t != b.t ? a.t > b.t : a.ordre > b.ordre; };
  while (!m_file.empty() && m_file.front().t <= jusqua + 1e-15) {
    std::pop_heap(m_file.begin(), m_file.end(), plusTot);
    const Evenement ev = m_file.back();
    m_file.pop_back();
    m_t = ev.t;
    ++n;
    Niveau& actuel = m_niveaux[static_cast<std::size_t>(ev.signal)];
    if (actuel == ev.v) continue;
    actuel = ev.v;
    m_trace.push_back({ev.t, ev.signal, ev.v});
    for (std::size_t p : m_lecteurs[static_cast<std::size_t>(ev.signal)]) evaluer(p);
  }
  m_t = std::max(m_t, jusqua);
  return n;
}

int SimulateurLogique::avancerJusquauRepos(double limite) {
  const double fin = m_t + limite;
  int n = 0;
  while (!m_file.empty() && m_file.front().t <= fin) n += avancer(m_file.front().t);
  return n;
}

Niveau SimulateurLogique::niveau(const std::string& signal) const {
  const int s = m_c.signal(signal);
  return s < 0 ? Niveau::Z : m_niveaux[static_cast<std::size_t>(s)];
}

std::vector<std::pair<std::string, std::string>> tableVerite(const CircuitLogique& c, const std::vector<std::string>& entrees, const std::vector<std::string>& sorties) {
  std::vector<std::pair<std::string, std::string>> r;
  if (entrees.size() > 12) return r;
  for (std::uint32_t combinaison = 0; combinaison < (1u << entrees.size()); ++combinaison) {
    SimulateurLogique s(c);
    std::string ligne;
    for (std::size_t j = 0; j < entrees.size(); ++j) {
      const bool haut = (combinaison >> (entrees.size() - 1 - j)) & 1u;
      s.forcer(entrees[j], haut ? Niveau::Haut : Niveau::Bas);
      ligne += haut ? '1' : '0';
    }
    s.avancerJusquauRepos(1e-6);
    std::string sortie;
    for (const auto& o : sorties) sortie += lettreNiveau(s.niveau(o));
    r.emplace_back(ligne, sortie);
  }
  return r;
}

std::string verilogStructurel(const CircuitLogique& c, const std::string& module) {
  std::vector<int> pilotes(c.signaux.size(), 0);
  for (const auto& p : c.portes)
    for (int s : p.sorties)
      if (s >= 0) pilotes[static_cast<std::size_t>(s)]++;
  std::map<int, std::string> noms;
  std::set<std::string> pris;
  for (std::size_t i = 0; i < c.signaux.size(); ++i) {
    std::string n = identifiant(c.signaux[i]);
    for (int k = 2; pris.count(n); ++k) n = identifiant(c.signaux[i]) + "_" + std::to_string(k);
    pris.insert(n);
    noms[static_cast<int>(i)] = n;
  }
  const auto libre = [&](std::size_t i) { return c.signaux[i].rfind("nc_", 0) == 0; };
  // Ports : réseaux nommés (étiquettes) sans pilote en entrée, pilotés en sortie ; les autres sont des fils
  std::vector<std::string> ports, fils;
  for (std::size_t i = 0; i < c.signaux.size(); ++i) {
    if (libre(i)) continue;
    const bool nomme = c.signaux[i].find('(') == std::string::npos && c.signaux[i].rfind("Net-", 0) != 0 && c.signaux[i].rfind("N$", 0) != 0;
    if (pilotes[i] == 0) ports.push_back("input wire " + noms[static_cast<int>(i)]);
    else if (nomme) ports.push_back("output wire " + noms[static_cast<int>(i)]);
    else fils.push_back(noms[static_cast<int>(i)]);
  }
  std::ostringstream s;
  s << "// Écrit par Prométhée : circuit logique de la conception, en Verilog structurel\n`timescale 1ns / 1ps\n\nmodule " << identifiant(module) << " (\n";
  for (std::size_t i = 0; i < ports.size(); ++i) s << "  " << ports[i] << (i + 1 < ports.size() ? ",\n" : "\n");
  s << ");\n";
  for (const auto& f : fils) s << "  wire " << f << ";\n";
  static const std::map<std::string, std::string> primitives = {{"ET", "and"}, {"OU", "or"}, {"NON", "not"}, {"NON_ET", "nand"}, {"NON_OU", "nor"}, {"OU_X", "xor"}, {"TAMPON", "buf"}};
  for (const auto& p : c.portes) {
    char retard[32];
    std::snprintf(retard, sizeof retard, "%g", p.retard * 1e9);
    if (p.genre == "D") {
      const std::string q = "q_" + identifiant(p.nom);
      s << "  reg " << q << " = 1'bx;\n  always @(posedge " << noms[p.entrees[1]] << ") " << q << " <= #" << retard << " " << noms[p.entrees[0]] << ";\n";
      if (p.sorties[0] >= 0) s << "  assign " << noms[p.sorties[0]] << " = " << q << ";\n";
      if (p.sorties.size() > 1 && p.sorties[1] >= 0) s << "  assign " << noms[p.sorties[1]] << " = ~" << q << ";\n";
      continue;
    }
    const auto it = primitives.find(p.genre);
    if (it == primitives.end() || p.sorties.empty() || p.sorties[0] < 0) continue;
    s << "  " << it->second << " #" << retard << " " << identifiant(p.nom) << " (" << noms[p.sorties[0]];
    for (int e : p.entrees) s << ", " << (libre(static_cast<std::size_t>(e)) ? std::string("1'bz") : noms[e]);
    s << ");\n";
  }
  s << "endmodule\n";
  return s.str();
}

DefSymbole symboleDepuisBrochageFpga(const std::string& csv, const std::string& id, const std::string& nom, std::vector<std::string>* alertes) {
  struct Ligne {
    std::string broche, nom, banc, type;
  };
  std::vector<Ligne> lignes;
  std::istringstream s(csv);
  std::string l;
  std::set<std::string> vues;
  while (std::getline(s, l)) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (l.empty() || l[0] == '#') continue;
    const char sep = l.find(';') != std::string::npos ? ';' : ',';
    std::vector<std::string> champs;
    std::string c;
    std::istringstream ls(l);
    while (std::getline(ls, c, sep)) {
      while (!c.empty() && (c.front() == ' ' || c.front() == '"')) c.erase(c.begin());
      while (!c.empty() && (c.back() == ' ' || c.back() == '"')) c.pop_back();
      champs.push_back(c);
    }
    if (champs.size() < 3) continue;
    std::string tete = champs[0];
    for (auto& ch : tete) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (tete == "pin" || tete == "broche" || tete == "pin number") continue;  // en-tête
    if (!vues.insert(champs[0]).second) {
      if (alertes) alertes->push_back("Broche « " + champs[0] + " » en double : la seconde est ignorée.");
      continue;
    }
    lignes.push_back({champs[0], champs[1], champs[2], champs.size() > 3 ? champs[3] : std::string()});
  }
  const auto alimentation = [](const Ligne& x) {
    std::string n = x.nom, t = x.type;
    for (auto& ch : n) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (auto& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return t == "power" || t == "ground" || t == "alimentation" || n.rfind("VCC", 0) == 0 || n.rfind("VDD", 0) == 0 || n.rfind("GND", 0) == 0 || n.rfind("VSS", 0) == 0 ||
           n.rfind("VREF", 0) == 0 || n.rfind("VCCO", 0) == 0;
  };
  // Bancs dans l'ordre naturel ; les alimentations à part, en dernière unité
  std::vector<std::string> bancs;
  for (const auto& x : lignes)
    if (!alimentation(x) && std::find(bancs.begin(), bancs.end(), x.banc) == bancs.end()) bancs.push_back(x.banc);
  std::sort(bancs.begin(), bancs.end(), ordreNaturel);
  DefSymbole d;
  d.id = id;
  d.nom = nom;
  d.categorie = "FPGA";
  d.prefixe = "U";
  d.description = "Symbole tiré du fichier de brochage : une unité par banc, alimentations à part";
  const int unitesBancs = static_cast<int>(bancs.size());
  d.unites = unitesBancs + 1;
  for (const auto& b : bancs) d.nomsUnites.push_back("Banc " + b);
  d.nomsUnites.push_back("Alimentations");
  // Clé de rangement : paires différentielles côte à côte (nom sans _P/_N, P avant N)
  // « IO_L2P_1 », « IO_L2N_1 », « CLK_P », « CLK_N » : la lettre P ou N retirée de sa place, remise en fin de clé
  const auto cle = [](const std::string& n) {
    for (std::size_t i = n.size(); i-- > 1;) {
      const char ch = n[i];
      if ((ch == 'P' || ch == 'N') && (i + 1 == n.size() || n[i + 1] == '_') && (std::isdigit(static_cast<unsigned char>(n[i - 1])) || n[i - 1] == '_'))
        return n.substr(0, i) + n.substr(i + 1) + (ch == 'P' ? "\x01" : "\x02");
    }
    return n;
  };
  for (int u = 1; u <= d.unites; ++u) {
    std::vector<const Ligne*> lesquelles;
    for (const auto& x : lignes)
      if (u <= unitesBancs ? (!alimentation(x) && x.banc == bancs[static_cast<std::size_t>(u - 1)]) : alimentation(x)) lesquelles.push_back(&x);
    std::sort(lesquelles.begin(), lesquelles.end(), [&](const Ligne* a, const Ligne* b) { return ordreNaturel(cle(a->nom), cle(b->nom)); });
    const int n = static_cast<int>(lesquelles.size());
    const int gauche = (n + 1) / 2;
    for (int i = 0; i < n; ++i) {
      const Ligne& x = *lesquelles[static_cast<std::size_t>(i)];
      Broche b;
      b.numero = x.broche;
      b.nom = x.nom;
      std::string t = x.type;
      for (auto& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      b.type = alimentation(x) ? TypeBroche::AlimEntree : t == "input" || t == "entree" ? TypeBroche::Entree : t == "output" || t == "sortie" ? TypeBroche::Sortie
                                                         : t == "nc" ? TypeBroche::NonConnectee : TypeBroche::Bidirectionnelle;
      const bool aGauche = i < gauche;
      const int rang = aGauche ? i : i - gauche;
      b.extremite = ptMm(aGauche ? -20.32 : 20.32, 2.54 * rang);
      b.direction = aGauche ? 0 : 2;
      b.longueur = 5'080'000;
      b.unite = u;
      d.broches.push_back(b);
    }
    Trace cadre;
    cadre.forme = Trace::Forme::Rectangle;
    cadre.points = {ptMm(-15.24, -2.54), ptMm(15.24, 2.54 * std::max(1, gauche))};
    cadre.fond = true;
    cadre.unite = u;
    d.traces.push_back(cadre);
  }
  if (alertes && lignes.empty()) alertes->push_back("Aucune broche lue : colonnes attendues « broche, nom, banc[, type] ».");
  return d;
}

}  // namespace prom::ecad

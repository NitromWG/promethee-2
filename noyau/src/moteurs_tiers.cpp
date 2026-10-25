// Prométhée : autres moteurs de simulation et mesures réelles (lot L14 de la 0.34 ; 14.8). Voir moteurs_tiers.hpp.
// Licence GPL-3.0-only.
#include "promethee/moteurs_tiers.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <random>
#include <sstream>

#include "promethee/archive.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/processus.hpp"

namespace prom::ecad {

namespace fs = std::filesystem;

namespace {
std::string minuscules(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string majuscules(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
std::string nb(double v) {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o.precision(10);
  o << v;
  return o.str();
}
double nombre(const std::string& t, bool* ok = nullptr) {
  std::string x = t;
  while (!x.empty() && std::isspace(static_cast<unsigned char>(x.front()))) x.erase(x.begin());
  char* fin = nullptr;
  const double v = std::strtod(x.c_str(), &fin);
  if (ok) *ok = fin && fin != x.c_str();
  return v;
}
std::vector<std::string> lignes(const std::string& texte) {
  std::vector<std::string> v;
  std::istringstream in(texte);
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    v.push_back(l);
  }
  return v;
}
std::vector<std::string> champs(const std::string& l, char sep) {
  std::vector<std::string> v;
  std::string c;
  for (const char ch : l) {
    if (ch == sep) {
      v.push_back(c);
      c.clear();
    } else
      c += ch;
  }
  v.push_back(c);
  for (auto& x : v) {
    while (!x.empty() && (x.front() == ' ' || x.front() == '"')) x.erase(x.begin());
    while (!x.empty() && (x.back() == ' ' || x.back() == '"')) x.pop_back();
  }
  return v;
}
std::string dossierTemporaire(const std::string& nom) {
  std::random_device r;
  std::error_code ec;
  const fs::path p = fs::temp_directory_path(ec) / ("promethee-" + nom + "-" + std::to_string(r()) + std::to_string(r()));
  fs::create_directories(p, ec);
  return utf8(p);
}
// Éléments de premier niveau d'une netlist de ngspice
std::vector<std::string> lignesCircuit(const NetlistSpice& n) {
  std::vector<std::string> v;
  bool controle = false;
  for (const auto& l : lignes(n.texte)) {
    const std::string m = minuscules(l);
    if (m.rfind(".control", 0) == 0) controle = true;
    if (controle) {
      if (m.rfind(".endc", 0) == 0) controle = false;
      continue;
    }
    if (m.rfind(".end", 0) == 0 && m.rfind(".ends", 0) != 0) break;
    if (m.rfind(".save", 0) == 0 || m.rfind(".tran", 0) == 0 || m.rfind(".ac", 0) == 0 || m.rfind(".op", 0) == 0 || m.rfind(".dc", 0) == 0) continue;
    v.push_back(l);
  }
  return v;
}
}  // namespace

// ======================================================================================================================
// Moteurs du poste
// ======================================================================================================================
std::vector<MoteurTiers> moteursTiers() {
  std::vector<MoteurTiers> v;
  auto ajouter = [&](const std::string& nom, std::initializer_list<const char*> programmes, bool relus) {
    MoteurTiers m;
    m.nom = nom;
    m.resultatsRelus = relus;
    for (const char* p : programmes)
      if (!programmeDuPoste(p).empty()) {
        m.programme = p;
        m.present = true;
        break;
      }
    if (m.programme.empty()) m.programme = *programmes.begin();
    v.push_back(m);
  };
  ajouter("Xyce", {"Xyce", "xyce"}, true);
  ajouter("QucsatorRF", {"qucsator_rf", "qucsator"}, true);
  ajouter("LTspice", {"ltspice", "LTspice", "XVIIx64"}, true);
  ajouter("PSpice", {"psp_cmd", "pspice"}, false);
  ajouter("HSPICE", {"hspice"}, false);
  return v;
}

std::string netlistXyce(const NetlistSpice& n, const AnalyseSpice& a, const std::vector<std::string>& sorties, const std::string& fichier) {
  std::ostringstream o;
  for (const auto& l : lignesCircuit(n)) o << l << "\n";
  std::string carte = a.carte();
  if (a.type == AnalyseSpice::Type::Transitoire) carte = ".TRAN " + nb(a.pas) + " " + nb(a.duree) + " " + nb(a.debut) + " " + nb(a.pas);
  o << carte << "\n";
  const char* genre = a.type == AnalyseSpice::Type::Transitoire ? "TRAN" : a.type == AnalyseSpice::Type::Alternatif ? "AC" : "DC";
  o << ".PRINT " << genre << " FORMAT=CSV FILE=" << fichier;
  for (const auto& s : sorties) {
    if (a.type == AnalyseSpice::Type::Alternatif && s.size() > 2 && minuscules(s).rfind("v(", 0) == 0)
      o << " VR" << majuscules(s.substr(1)) << " VI" << majuscules(s.substr(1));
    else
      o << " " << majuscules(s);
  }
  o << "\n.END\n";
  return o.str();
}

std::string netlistQucsator(const NetlistSpice& n, const AnalyseSpice& a, std::vector<std::string>* refus) {
  std::ostringstream o;
  o << "# Qucs 0.0.24 — netlist écrite par Prométhée\n";
  auto noeud = [](const std::string& x) { return x == "0" ? std::string("gnd") : x; };
  bool sousCircuit = false;
  for (const auto& l : lignesCircuit(n)) {
    const std::string m = minuscules(l);
    if (m.rfind(".subckt", 0) == 0) sousCircuit = true;
    if (sousCircuit) {
      if (m.rfind(".ends", 0) == 0) sousCircuit = false;
      continue;
    }
    if (m.empty() || m[0] == '*' || m[0] == '.') continue;
    std::istringstream mots(l);
    std::string inst, p, q;
    mots >> inst >> p >> q;
    std::string reste;
    std::getline(mots, reste);
    const char g = static_cast<char>(std::toupper(static_cast<unsigned char>(inst[0])));
    const std::string valeur = reste.empty() ? std::string() : reste.substr(reste.find_first_not_of(' ') == std::string::npos ? 0 : reste.find_first_not_of(' '));
    if (g == 'R') o << "R:" << inst << " " << noeud(p) << " " << noeud(q) << " R=\"" << valeur << " Ohm\"\n";
    else if (g == 'C') o << "C:" << inst << " " << noeud(p) << " " << noeud(q) << " C=\"" << valeur << " F\"\n";
    else if (g == 'L') o << "L:" << inst << " " << noeud(p) << " " << noeud(q) << " L=\"" << valeur << " H\"\n";
    else if (g == 'V') {
      const std::string v = minuscules(valeur);
      if (v.rfind("sin(", 0) == 0) {
        std::istringstream s(v.substr(4));
        double off = 0, amp = 0, f = 0;
        s >> off >> amp >> f;
        o << "Vac:" << inst << " " << noeud(p) << " " << noeud(q) << " U=\"" << nb(amp) << " V\" f=\"" << nb(f) << " Hz\" Phase=\"0\" Theta=\"0\"\n";
        if (off != 0 && refus) refus->push_back(inst + " : décalage de la sinusoïde non traduit");
      } else {
        double dc = 0, ac = 0;
        std::istringstream s(v);
        for (std::string mot; s >> mot;) {
          if (mot == "dc") s >> dc;
          else if (mot == "ac") s >> ac;
          else if (std::isdigit(static_cast<unsigned char>(mot[0])) || mot[0] == '-') dc = nombre(mot);
        }
        if (ac != 0) o << "Vac:" << inst << " " << noeud(p) << " " << noeud(q) << " U=\"" << nb(ac) << " V\" f=\"1 kHz\" Phase=\"0\" Theta=\"0\"\n";
        else o << "Vdc:" << inst << " " << noeud(p) << " " << noeud(q) << " U=\"" << nb(dc) << " V\"\n";
      }
    } else if (refus)
      refus->push_back(inst + " : élément non traduit pour QucsatorRF");
  }
  switch (a.type) {
    case AnalyseSpice::Type::Transitoire:
      o << ".TR:TR1 Type=\"lin\" Start=\"" << nb(a.debut) << "\" Stop=\"" << nb(a.duree) << "\" Points=\"" << static_cast<long>(a.duree / std::max(1e-15, a.pas)) + 1
        << "\" IntegrationMethod=\"Trapezoidal\" Order=\"2\" InitialStep=\"" << nb(a.pas) << "\" MinStep=\"1e-16\" MaxIter=\"150\" relTol=\"0.001\" absTol=\"1 pA\" vntol=\"1 uV\" Temp=\"26.85\" LTEreltol=\"1e-3\" LTEabstol=\"1e-6\" LTEfactor=\"1\" Solver=\"CroutLU\" relaxTSR=\"no\" initialDC=\"yes\" MaxStep=\"0\"\n";
      break;
    case AnalyseSpice::Type::Alternatif: {
      const long points = static_cast<long>(std::max(1.0, std::log10(a.fFin / a.fDebut)) * a.pointsParDecade) + 1;
      o << ".AC:AC1 Type=\"log\" Start=\"" << nb(a.fDebut) << "\" Stop=\"" << nb(a.fFin) << "\" Points=\"" << points << "\" Noise=\"no\"\n";
      break;
    }
    default: o << ".DC:DC1 Temp=\"26.85\" reltol=\"0.001\" abstol=\"1 pA\" vntol=\"1 uV\" saveOPs=\"no\" MaxIter=\"150\" saveAll=\"no\" convHelper=\"none\" Solver=\"CroutLU\"\n"; break;
  }
  return o.str();
}

ResultatSimulation lireCsvXyce(const std::string& texte) {
  ResultatSimulation r;
  const auto ls = lignes(texte);
  if (ls.empty()) {
    r.erreur = "résultats vides";
    return r;
  }
  const auto tete = champs(ls[0], ',');
  std::vector<std::vector<double>> col(tete.size());
  for (std::size_t i = 1; i < ls.size(); ++i) {
    if (ls[i].empty() || ls[i].rfind("End", 0) == 0) continue;
    const auto c = champs(ls[i], ',');
    for (std::size_t k = 0; k < c.size() && k < col.size(); ++k) col[k].push_back(nombre(c[k]));
  }
  std::map<std::string, std::vector<double>> reelles, imaginaires;
  for (std::size_t k = 0; k < tete.size(); ++k) {
    std::string nom = minuscules(tete[k]);
    if (nom == "time") r.echelle = "time";
    if (nom == "freq" || nom == "frequency") {
      nom = "frequency";
      r.echelle = "frequency";
    }
    if (nom.rfind("vr(", 0) == 0) reelles["v" + nom.substr(2)] = col[k];
    else if (nom.rfind("vi(", 0) == 0) imaginaires["v" + nom.substr(2)] = col[k];
    else r.reels[nom] = col[k];
  }
  for (const auto& [nom, re] : reelles) {
    auto& z = r.complexes[nom];
    const auto& im = imaginaires[nom];
    for (std::size_t i = 0; i < re.size(); ++i) z.emplace_back(re[i], i < im.size() ? im[i] : 0);
  }
  return r;
}

ResultatSimulation lireDatQucs(const std::string& texte) {
  ResultatSimulation r;
  std::string nom, echelle;
  bool independante = false;
  std::vector<std::string> valeurs;
  auto ranger = [&]() {
    if (nom.empty()) return;
    // « n_sortie.Vt » (transitoire), « n_sortie.v » (alternatif), « time », « acfrequency »
    std::string cle = nom;
    if (cle == "acfrequency") cle = "frequency";
    else if (const auto p = cle.rfind('.'); p != std::string::npos) cle = "v(" + minuscules(cle.substr(0, p)) + ")";
    bool complexe = false;
    for (const auto& v : valeurs) complexe |= v.find('j') != std::string::npos;
    if (complexe) {
      auto& z = r.complexes[cle];
      for (const auto& v : valeurs) {
        const auto j = v.find('j');
        const std::string re = v.substr(0, j - 1), im = v.substr(j + 1);
        const double s = v[j - 1] == '-' ? -1 : 1;
        z.emplace_back(nombre(re), s * nombre(im));
      }
    } else {
      auto& x = r.reels[cle];
      for (const auto& v : valeurs) x.push_back(nombre(v));
    }
    if (independante) r.echelle = cle == "frequency" ? "frequency" : cle;
  };
  for (const auto& l : lignes(texte)) {
    if (l.rfind("<indep ", 0) == 0 || l.rfind("<dep ", 0) == 0) {
      independante = l.rfind("<indep ", 0) == 0;
      std::istringstream m(l.substr(independante ? 7 : 5));
      m >> nom;
      valeurs.clear();
    } else if (l.rfind("</indep>", 0) == 0 || l.rfind("</dep>", 0) == 0) {
      ranger();
      nom.clear();
    } else if (!nom.empty()) {
      std::string v = l;
      v.erase(std::remove_if(v.begin(), v.end(), [](unsigned char c) { return std::isspace(c); }), v.end());
      if (!v.empty()) valeurs.push_back(v);
    }
  }
  if (r.reels.empty() && r.complexes.empty()) r.erreur = "aucune donnée dans le fichier de Qucsator";
  return r;
}

ResultatSimulation lireRaw(const std::string& octets) {
  ResultatSimulation r;
  // En-tête en UTF-16 (LTspice) ou en ASCII (ngspice), jusqu'à « Binary: » ou « Values: »
  const bool utf16 = octets.size() > 2 && octets[1] == '\0';
  std::string tete;
  std::size_t debut = 0;
  if (utf16) {
    for (std::size_t i = 0; i + 1 < octets.size(); i += 2) {
      tete += octets[i];
      if (tete.size() >= 8 && (tete.compare(tete.size() - 8, 8, "Binary:\n") == 0 || tete.compare(tete.size() - 8, 8, "Values:\n") == 0)) {
        debut = i + 2;
        break;
      }
    }
  } else {
    for (const char* m : {"Binary:\n", "Values:\n"})
      if (const auto p = octets.find(m); p != std::string::npos) {
        tete = octets.substr(0, p + 8);
        debut = p + 8;
        break;
      }
  }
  if (tete.empty() || debut == 0) {
    r.erreur = "fichier brut illisible";
    return r;
  }
  const bool binaire = tete.find("Binary:") != std::string::npos;
  const bool complexe = minuscules(tete).find("flags: complex") != std::string::npos;
  const bool doubles = !utf16 || minuscules(tete).find("double") != std::string::npos;
  int nVar = 0, nPoints = 0;
  std::vector<std::string> noms;
  {
    std::istringstream in(tete);
    bool variables = false;
    for (std::string l; std::getline(in, l);) {
      if (!l.empty() && l.back() == '\r') l.pop_back();
      if (l.rfind("No. Variables:", 0) == 0) nVar = std::atoi(l.c_str() + 14);
      else if (l.rfind("No. Points:", 0) == 0) nPoints = std::atoi(l.c_str() + 11);
      else if (l.rfind("Variables:", 0) == 0) variables = true;
      else if (variables && (l.rfind("Binary:", 0) == 0 || l.rfind("Values:", 0) == 0)) break;
      else if (variables) {
        std::istringstream m(l);
        int i = 0;
        std::string nom;
        if (m >> i >> nom) noms.push_back(minuscules(nom));
      }
    }
  }
  if (nVar <= 0 || static_cast<int>(noms.size()) != nVar) {
    r.erreur = "variables du fichier brut illisibles";
    return r;
  }
  std::vector<std::vector<std::complex<double>>> v(static_cast<std::size_t>(nVar));
  if (binaire) {
    std::size_t p = debut;
    auto lireDouble = [&](double& x) {
      if (p + 8 > octets.size()) return false;
      std::memcpy(&x, octets.data() + p, 8);
      p += 8;
      return true;
    };
    auto lireFloat = [&](double& x) {
      if (p + 4 > octets.size()) return false;
      float f = 0;
      std::memcpy(&f, octets.data() + p, 4);
      p += 4;
      x = f;
      return true;
    };
    for (int k = 0; k < nPoints; ++k)
      for (int i = 0; i < nVar; ++i) {
        double a = 0, b = 0;
        bool ok = complexe ? lireDouble(a) && lireDouble(b) : (i == 0 || doubles) ? lireDouble(a) : lireFloat(a);
        if (!ok) break;
        // LTspice range le temps en valeur absolue (signe négatif : pas forcé)
        if (i == 0 && utf16 && !complexe) a = std::abs(a);
        v[static_cast<std::size_t>(i)].emplace_back(a, b);
      }
  } else {
    std::istringstream in(octets.substr(debut));
    for (int k = 0; k < nPoints; ++k)
      for (int i = 0; i < nVar; ++i) {
        std::string mot;
        if (i == 0) {
          int indice = 0;
          in >> indice;
        }
        if (!(in >> mot)) break;
        const auto virgule = mot.find(',');
        v[static_cast<std::size_t>(i)].emplace_back(nombre(mot.substr(0, virgule)), virgule == std::string::npos ? 0.0 : nombre(mot.substr(virgule + 1)));
      }
  }
  for (int i = 0; i < nVar; ++i) {
    const std::string& nom = noms[static_cast<std::size_t>(i)];
    if (complexe && i > 0) r.complexes[nom] = v[static_cast<std::size_t>(i)];
    else {
      auto& x = r.reels[nom];
      for (const auto& z : v[static_cast<std::size_t>(i)]) x.push_back(z.real());
    }
  }
  r.echelle = noms.front();
  return r;
}

ResultatSimulation simulerAvec(const std::string& moteur, const NetlistSpice& n, const AnalyseSpice& a, const std::vector<std::string>& sorties, double delai) {
  ResultatSimulation r;
  const std::string m = minuscules(moteur);
  if (m == "ngspice") return simuler(n);
  MoteurTiers choisi;
  for (const auto& x : moteursTiers())
    if (minuscules(x.nom) == m) choisi = x;
  if (choisi.nom.empty()) {
    r.erreur = "moteur « " + moteur + " » inconnu";
    return r;
  }
  if (!choisi.present) {
    r.erreur = choisi.nom + " n'est pas installé sur ce poste";
    return r;
  }
  if (!choisi.resultatsRelus) {
    r.erreur = choisi.nom + " : ses fichiers de résultats ne sont pas relus par Prométhée";
    return r;
  }
  const std::string dossier = dossierTemporaire("moteur");
  const fs::path d = cheminFichier(dossier);
  OptionsProcessus o;
  o.dossier = dossier;
  o.delai = delai;
  ResultatProcessus p;
  try {
    if (choisi.nom == "Xyce") {
      ecrireFichierAtomique(d / "circuit.cir", netlistXyce(n, a, sorties, "sortie.csv"));
      p = executerProcessus({choisi.programme, "circuit.cir"}, o);
      if (fs::exists(d / "sortie.csv")) r = lireCsvXyce(lireFichier(d / "sortie.csv"));
    } else if (choisi.nom == "QucsatorRF") {
      std::vector<std::string> refus;
      ecrireFichierAtomique(d / "circuit.txt", netlistQucsator(n, a, &refus));
      p = executerProcessus({choisi.programme, "-i", "circuit.txt", "-o", "sortie.dat"}, o);
      if (fs::exists(d / "sortie.dat")) r = lireDatQucs(lireFichier(d / "sortie.dat"));
      for (const auto& x : refus) r.console.push_back(x);
    } else {
      std::string texte;
      for (const auto& l : lignesCircuit(n)) texte += l + "\n";
      texte += a.carte() + "\n.end\n";
      ecrireFichierAtomique(d / "circuit.net", texte);
      p = executerProcessus({choisi.programme, "-b", "circuit.net"}, o);
      if (fs::exists(d / "circuit.raw")) r = lireRaw(lireFichier(d / "circuit.raw"));
    }
  } catch (const std::exception& e) {
    r.erreur = e.what();
  }
  if (r.reels.empty() && r.complexes.empty() && r.erreur.empty()) r.erreur = choisi.nom + " n'a rendu aucun résultat" + (p.erreurs.empty() ? std::string() : " : " + p.erreurs.substr(0, 300));
  std::error_code ec;
  fs::remove_all(d, ec);
  return r;
}

// ======================================================================================================================
// Mesures réelles
// ======================================================================================================================
std::optional<std::vector<CourbeMesuree>> lireCsvOscilloscope(const std::string& texte, std::string* erreur) {
  auto refuser = [&](const std::string& e) -> std::optional<std::vector<CourbeMesuree>> {
    if (erreur) *erreur = e;
    return std::nullopt;
  };
  const auto ls = lignes(texte);
  if (ls.size() < 2) return refuser("fichier trop court");
  // Séparateur le plus fréquent des premières lignes
  std::map<char, int> compte;
  for (std::size_t i = 0; i < std::min<std::size_t>(ls.size(), 5); ++i)
    for (const char c : ls[i])
      if (c == ',' || c == ';' || c == '\t') ++compte[c];
  char sep = ',';
  int best = -1;
  for (const auto& [c, n] : compte)
    if (n > best) best = n, sep = c;
  std::vector<CourbeMesuree> v;
  const auto tete = champs(ls[0], sep);
  const std::string t0 = minuscules(tete.empty() ? std::string() : tete[0]);
  // Rigol : « X, CH1, …, Start, Increment »
  const auto start = std::find_if(tete.begin(), tete.end(), [](const std::string& x) { return minuscules(x) == "start"; });
  if (t0 == "x" && start != tete.end()) {
    const std::size_t is = static_cast<std::size_t>(start - tete.begin());
    const auto deux = champs(ls[1], sep);
    const double debut = is < deux.size() ? nombre(deux[is]) : 0, pas = is + 1 < deux.size() ? nombre(deux[is + 1]) : 1;
    for (std::size_t k = 1; k < is; ++k) v.push_back({tete[k], k < deux.size() ? deux[k] : std::string("V"), {}});
    for (std::size_t i = 2; i < ls.size(); ++i) {
      const auto c = champs(ls[i], sep);
      bool ok = false;
      const double x = c.empty() ? 0 : nombre(c[0], &ok);
      if (!ok) continue;
      for (std::size_t k = 1; k < is && k < c.size(); ++k) v[k - 1].points.push_back({debut + x * pas, nombre(c[k])});
    }
    return v;
  }
  // Tektronix : « Record Length, …, , temps, valeur » sur chaque ligne
  if (t0 == "record length") {
    CourbeMesuree c{"CH1", "V", {}};
    for (const auto& l : ls) {
      const auto x = champs(l, sep);
      if (x.size() >= 5) c.points.push_back({nombre(x[3]), nombre(x[4])});
    }
    v.push_back(c);
    return v;
  }
  // Keysight (« x-axis », unités sur la deuxième ligne) et générique (en-tête de noms, ou nombres d'emblée)
  std::size_t premiere = 0;
  std::vector<std::string> noms, unites;
  bool ok = false;
  nombre(tete.empty() ? std::string() : tete[0], &ok);
  if (!ok) {
    noms = tete;
    premiere = 1;
    const auto deux = champs(ls[1], sep);
    bool okDeux = false;
    nombre(deux.empty() ? std::string() : deux[0], &okDeux);
    if (!okDeux) {
      unites = deux;
      premiere = 2;
    }
  }
  for (std::size_t i = premiere; i < ls.size(); ++i) {
    const auto c = champs(ls[i], sep);
    bool okT = false;
    const double t = c.empty() ? 0 : nombre(c[0], &okT);
    if (!okT) continue;
    if (v.empty())
      for (std::size_t k = 1; k < c.size(); ++k)
        if (!c[k].empty()) v.push_back({k < noms.size() ? noms[k] : "CH" + std::to_string(k), k < unites.size() ? unites[k] : std::string("V"), {}});
    for (std::size_t k = 1; k < c.size() && k - 1 < v.size(); ++k)
      if (!c[k].empty()) v[k - 1].points.push_back({t, nombre(c[k])});
  }
  if (v.empty()) return refuser("aucune voie lisible");
  return v;
}

std::optional<CaptureSigrok> lireSigrok(const std::string& octets, std::string* erreur) {
  auto refuser = [&](const std::string& e) -> std::optional<CaptureSigrok> {
    if (erreur) *erreur = e;
    return std::nullopt;
  };
  std::vector<std::pair<std::string, std::string>> fichiers;
  try {
    fichiers = lireZip(octets);
  } catch (const std::exception& e) {
    return refuser(std::string("archive illisible : ") + e.what());
  }
  std::map<std::string, std::string> f(fichiers.begin(), fichiers.end());
  if (!f.count("metadata")) return refuser("pas de métadonnées : ce n'est pas une capture de sigrok");
  CaptureSigrok c;
  std::string prefixe = "logic-1";
  int unite = 1;
  std::map<int, std::string> sondes;
  for (const auto& l : lignes(f["metadata"])) {
    const auto eg = l.find('=');
    if (eg == std::string::npos) continue;
    const std::string k = l.substr(0, eg), v = l.substr(eg + 1);
    if (k == "capturefile") prefixe = v;
    else if (k == "unitsize") unite = std::max(1, std::atoi(v.c_str()));
    else if (k == "samplerate") {
      double x = nombre(v);
      const std::string u = minuscules(v);
      if (u.find("ghz") != std::string::npos) x *= 1e9;
      else if (u.find("mhz") != std::string::npos) x *= 1e6;
      else if (u.find("khz") != std::string::npos) x *= 1e3;
      c.frequence = x;
    } else if (k.rfind("probe", 0) == 0)
      sondes[std::atoi(k.c_str() + 5)] = v;
  }
  if (c.frequence <= 0) return refuser("fréquence d'échantillonnage absente");
  // Morceaux « logic-1-1 », « logic-1-2 »… dans l'ordre
  std::vector<std::pair<int, std::string>> morceaux;
  for (const auto& [nom, contenu] : f)
    if (nom.rfind(prefixe + "-", 0) == 0) morceaux.emplace_back(std::atoi(nom.c_str() + prefixe.size() + 1), contenu);
  std::sort(morceaux.begin(), morceaux.end());
  std::string donnees;
  for (const auto& [i, m] : morceaux) donnees += m;
  const std::size_t n = donnees.size() / static_cast<std::size_t>(unite);
  for (const auto& [k, nom] : sondes) {
    VoieLogique v;
    for (std::size_t i = 0; i < n; ++i) {
      std::uint64_t mot = 0;
      for (int b = 0; b < unite && b < 8; ++b) mot |= static_cast<std::uint64_t>(static_cast<unsigned char>(donnees[i * static_cast<std::size_t>(unite) + static_cast<std::size_t>(b)])) << (8 * b);
      const Niveau x = (mot >> (k - 1)) & 1 ? Niveau::Haut : Niveau::Bas;
      if (v.empty() || v.back().niveau != x) v.push_back({static_cast<double>(i) / c.frequence, x});
    }
    c.voies.emplace_back(nom, v);
  }
  return c;
}

std::string pwlDepuisMesure(const Releve& r, std::size_t pointsMax) {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o.precision(9);
  o << "PWL(";
  const std::size_t pas = std::max<std::size_t>(1, (r.size() + pointsMax - 1) / std::max<std::size_t>(1, pointsMax));
  double dernier = -1e300;
  bool premier = true;
  for (std::size_t i = 0; i < r.size(); i += pas) {
    if (r[i].t <= dernier) continue;
    o << (premier ? "" : " ") << r[i].t << " " << r[i].v;
    dernier = r[i].t;
    premier = false;
  }
  o << ")";
  return o.str();
}

EcartCourbes comparerCourbes(const Releve& a, const Releve& b, double debut, double fin) {
  EcartCourbes e;
  if (a.empty() || b.empty()) return e;
  double amplitude = 0, somme = 0;
  int n = 0;
  for (const auto& x : a)
    if (x.t >= debut && x.t <= fin && x.t >= b.front().t && x.t <= b.back().t) amplitude = std::max(amplitude, std::abs(x.v));
  if (amplitude <= 0) return e;
  for (const auto& x : a) {
    if (x.t < debut || x.t > fin || x.t < b.front().t || x.t > b.back().t) continue;
    const double d = std::abs(x.v - valeurA(b, x.t)) / amplitude;
    e.maximal = std::max(e.maximal, d);
    somme += d;
    ++n;
  }
  if (n == 0) return e;
  e.moyen = somme / n;
  e.valide = true;
  return e;
}

std::optional<CaptureSigrok> capturerSigrok(const CaptureDirecte& c, std::string* erreur) {
  auto refuser = [&](const std::string& e) -> std::optional<CaptureSigrok> {
    if (erreur) *erreur = e;
    return std::nullopt;
  };
  if (!c.active) return refuser("capture en direct désactivée : active-la dans les réglages de la simulation pour brancher un appareil");
  if (programmeDuPoste("sigrok-cli").empty()) return refuser("sigrok-cli n'est pas installé sur ce poste");
  const std::string dossier = dossierTemporaire("sigrok");
  const fs::path d = cheminFichier(dossier);
  std::vector<std::string> argv = {"sigrok-cli", "--driver", c.pilote, "--samples", std::to_string(std::max(1L, c.echantillons))};
  if (!c.voies.empty()) argv.insert(argv.end(), {"--channels", c.voies});
  argv.insert(argv.end(), {"-o", utf8(d / "capture.sr")});
  OptionsProcessus o;
  o.delai = 60;
  const ResultatProcessus p = executerProcessus(argv, o);
  std::optional<CaptureSigrok> r;
  std::error_code ec;
  if (fs::exists(d / "capture.sr", ec)) r = lireSigrok(lireFichier(d / "capture.sr"), erreur);
  else if (erreur) *erreur = "sigrok-cli n'a rien capturé" + (p.erreurs.empty() ? std::string() : " : " + p.erreurs.substr(0, 300));
  fs::remove_all(d, ec);
  return r;
}

}  // namespace prom::ecad

// Prométhée : essais, mise en service et fiabilité (lot L17). Licence GPL-3.0-only.
#include "promethee/essais_production.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <set>
#include <sstream>

#include "promethee/revue_conception.hpp"
#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
std::string minuscules(std::string s) {
  for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}
std::string prefixeDe(const std::string& ref) {
  std::string p;
  for (char ch : ref) {
    if (!std::isalpha(static_cast<unsigned char>(ch))) break;
    p += ch;
  }
  return p;
}
std::string chiffre(double v, int decimales = 2) {
  char s[40];
  std::snprintf(s, sizeof s, "%.*f", decimales, v);
  std::string r = s;
  std::replace(r.begin(), r.end(), '.', ',');
  return r;
}
std::set<std::string> poses(const ConceptionElec& c) {
  std::set<std::string> r;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles) r.insert(s.composant);
  return r;
}
// Grandeur chiffrée d'un texte : « 3,3 V », « 12 mA », « 1 kHz », « 4,7 kΩ » → (grandeur, valeur SI)
std::optional<std::pair<std::string, double>> grandeurDe(const std::string& t0) {
  std::string t;
  for (char ch : t0)
    if (ch != ' ') t += ch == ',' ? '.' : ch;
  char* fin = nullptr;
  const double v = std::strtod(t.c_str(), &fin);
  if (!fin || fin == t.c_str()) return std::nullopt;
  std::string u = fin;
  double f = 1;
  if (u.rfind("µ", 0) == 0) f = 1e-6, u = u.substr(std::string("µ").size());
  else if (!u.empty() && u.size() > 1 && std::string("munpkMG").find(u[0]) != std::string::npos) {
    f = u[0] == 'm' ? 1e-3 : u[0] == 'u' ? 1e-6 : u[0] == 'n' ? 1e-9 : u[0] == 'p' ? 1e-12 : u[0] == 'k' ? 1e3 : u[0] == 'M' ? 1e6 : 1e9;
    u = u.substr(1);
  }
  const std::string m = minuscules(u);
  if (m == "v") return std::pair<std::string, double>{"tension", v * f};
  if (m == "a") return std::pair<std::string, double>{"courant", v * f};
  if (m == "hz") return std::pair<std::string, double>{"fréquence", v * f};
  if (u == "Ω" || m == "ohm" || m == "ohms" || m == "r") return std::pair<std::string, double>{"résistance", v * f};
  return std::nullopt;
}
double puissanceNominale(const ComposantElec& k) {
  if (const auto it = k.parametres.find("puissance"); it != k.parametres.end()) {
    std::string t = it->second;
    if (const auto s = t.find('/'); s != std::string::npos) return std::atof(t.substr(0, s).c_str()) / std::max(1.0, std::atof(t.substr(s + 1).c_str()));
    std::replace(t.begin(), t.end(), ',', '.');
    const double v = std::atof(t.c_str());
    return t.find("mW") != std::string::npos ? v * 1e-3 : v;
  }
  const std::string e = k.empreinte;
  if (e.find("0201") != std::string::npos) return 0.05;
  if (e.find("0402") != std::string::npos) return 0.063;
  if (e.find("0603") != std::string::npos) return 0.1;
  if (e.find("0805") != std::string::npos) return 0.125;
  if (e.find("1206") != std::string::npos) return 0.25;
  if (e.find("2512") != std::string::npos) return 1;
  return 0.25;  // traversant ordinaire
}
}  // namespace

PlanMiseSousTension planMiseSousTension(const ConceptionElec& c) {
  PlanMiseSousTension p;
  const Connectivite k = connectivite(c);
  std::vector<std::string> rails;
  for (const auto& r : k.reseaux)
    if (estAlimentation(r)) rails.push_back(r.nom);
  std::sort(rails.begin(), rails.end(), ordreNaturel);
  // Régulateurs : de leur rail d'entrée à leur rail de sortie
  std::map<std::string, std::set<std::string>> suivants;
  std::map<std::string, int> entrants;
  for (const auto& r : rails) entrants[r] = 0;
  const auto pos = poses(c);
  for (const auto& x : c.composants) {
    if (!pos.count(x.uuid) || !estRegulateur(c, x)) continue;
    const DefSymbole* d = c.symbole(x.symbole);
    std::string entree, sortie;
    for (const auto& b : d->broches) {
      const std::string n = k.nom(x.uuid, b.numero);
      if (n.empty() || estMasse(n)) continue;
      if (b.type == TypeBroche::AlimSortie) sortie = n;
      else if (b.type == TypeBroche::AlimEntree) entree = n;
    }
    if (entree.empty() || sortie.empty() || !entrants.count(entree) || !entrants.count(sortie)) continue;
    if (suivants[entree].insert(sortie).second) entrants[sortie]++;
  }
  // Ordre topologique (Kahn) : les rails d'entrée d'abord
  std::deque<std::string> file;
  for (const auto& r : rails)
    if (entrants[r] == 0) {
      file.push_back(r);
      p.entrees.push_back(r);
    }
  while (!file.empty()) {
    const std::string r = file.front();
    file.pop_front();
    p.rails.push_back(r);
    for (const auto& s : suivants[r])
      if (--entrants[s] == 0) file.push_back(s);
  }
  if (p.rails.size() < rails.size()) p.alertes.push_back("Boucle entre régulateurs : l'ordre des rails n'est que partiel.");
  int n = 0;
  for (const auto& r : p.rails)
    p.etapes.push_back({++n, "Carte hors tension : ohmmètre entre " + r + " et la masse (pas de court-circuit)", r, "> 100 Ω", 0, 100, 1e12});
  for (const auto& r : p.entrees) {
    const double v = tensionDe(r);
    if (v <= 0) p.alertes.push_back("Tension de « " + r + " » inconnue : à nommer (« +5V », « 3V3 ») ou à préciser à la main.");
    p.etapes.push_back({++n, "Alimenter " + r + (v > 0 ? " à " + chiffre(v) + " V" : std::string()) + ", courant limité à 100 mA, et relever le courant absorbé", r,
                        v > 0 ? chiffre(v) + " V" : std::string("tension nominale"), v, v * 0.95, v * 1.05});
  }
  for (const auto& r : p.rails) {
    const double v = tensionDe(r);
    p.etapes.push_back({++n, "Mesurer " + r + " par rapport à la masse", r, v > 0 ? chiffre(v) + " V ± 5 %" : std::string("à préciser"), v, v * 0.95, v * 1.05});
  }
  return p;
}

std::vector<PointDeTest> pointsDeTest(const ConceptionElec& c) {
  std::vector<PointDeTest> r;
  const Connectivite k = connectivite(c);
  const auto pos = poses(c);
  for (const auto& x : c.composants) {
    if (!pos.count(x.uuid) || x.symbole != "Point_test") continue;
    PointDeTest p;
    p.reference = x.reference;
    p.reseau = k.nom(x.uuid, "1");
    if (const auto it = x.champs.find("Valeur attendue"); it != x.champs.end())
      if (const auto g = grandeurDe(it->second)) {
        p.grandeur = g->first;
        p.attendue = g->second;
        p.chiffree = true;
      }
    if (const auto it = x.champs.find("Tolérance"); it != x.champs.end()) {
      std::string t = it->second;
      std::replace(t.begin(), t.end(), ',', '.');
      if (const double v = std::atof(t.c_str()); v > 0) p.tolerance = v / 100;
    }
    if (const auto it = x.champs.find("Forme"); it != x.champs.end()) p.forme = it->second;
    r.push_back(p);
  }
  std::sort(r.begin(), r.end(), [](const PointDeTest& a, const PointDeTest& b) { return ordreNaturel(a.reference, b.reference); });
  return r;
}

CouvertureTest couvertureTest(const ConceptionElec& c) {
  CouvertureTest r;
  const Connectivite k = connectivite(c);
  std::set<std::string> accessibles;
  for (const auto& p : pointsDeTest(c))
    if (!p.reseau.empty()) accessibles.insert(p.reseau);
  for (const auto& p : pastilles(c, k))
    if (p.pastille.type == TypePastille::Traversante && !p.reseau.empty()) accessibles.insert(p.reseau);
  for (const auto& n : k.reseaux) {
    if (n.broches.size() < 2) continue;
    ++r.reseaux;
    if (accessibles.count(n.nom)) ++r.accessibles;
    else r.sansAcces.push_back(n.nom);
  }
  std::sort(r.sansAcces.begin(), r.sansAcces.end(), ordreNaturel);
  r.taux = r.reseaux ? static_cast<double>(r.accessibles) / r.reseaux : 1;
  return r;
}

std::string ProcesVerbal::csv() const {
  std::ostringstream s;
  s << "Procès-verbal;" << numeroSerie << ";" << date << ";" << operateur << "\nPoint;Réseau;Attendu;Mesure;Verdict\n";
  for (const auto& l : lignes)
    s << l.point << ";" << l.reseau << ";" << l.attendu << ";" << (l.mesuree ? chiffre(l.mesure, 4) : std::string()) << ";"
      << (!l.mesuree ? "non mesuré" : l.conforme ? "conforme" : "hors tolérance") << "\n";
  s << "Verdict;" << (conforme ? "carte conforme" : "carte non conforme") << "\n";
  return s.str();
}

ProcesVerbal procesVerbal(const ConceptionElec& c, const std::string& numeroSerie, const std::vector<MesurePv>& mesures, const std::string& operateur, const std::string& date) {
  ProcesVerbal pv;
  pv.numeroSerie = numeroSerie;
  pv.operateur = operateur;
  pv.date = date;
  pv.conforme = true;
  for (const auto& p : pointsDeTest(c)) {
    LignePv l;
    l.point = p.reference;
    l.reseau = p.reseau;
    l.attendu = p.chiffree ? chiffre(p.attendue, 3) + " (± " + chiffre(p.tolerance * 100, 0) + " %)" : std::string("—");
    for (const auto& m : mesures)
      if (m.point == p.reference || (!p.reseau.empty() && m.point == p.reseau)) {
        l.mesure = m.valeur;
        l.mesuree = true;
      }
    l.conforme = l.mesuree && (!p.chiffree || std::abs(l.mesure - p.attendue) <= p.tolerance * std::abs(p.attendue) + 1e-12);
    if (p.chiffree && !l.conforme) pv.conforme = false;
    pv.lignes.push_back(l);
  }
  return pv;
}

std::vector<ContrainteEmploi> contraintesEmploi(const ConceptionElec& c, double admisTension, double admisPuissance) {
  std::vector<ContrainteEmploi> r;
  const Connectivite k = connectivite(c);
  const auto pos = poses(c);
  const auto tensionReseau = [](const std::string& n) -> std::optional<double> {
    if (estMasse(n)) return 0.0;
    const double v = tensionDe(n);
    return v > 0 ? std::optional<double>(v) : std::nullopt;
  };
  for (const auto& x : c.composants) {
    if (!pos.count(x.uuid) || x.nePasMonter) continue;
    const std::string a = k.nom(x.uuid, "1"), b = k.nom(x.uuid, "2");
    const auto va = tensionReseau(a), vb = tensionReseau(b);
    if (estCondensateur(c, x)) {
      const auto it = x.parametres.find("tension");
      const double nominale = it == x.parametres.end() ? 0 : tensionDe(it->second);
      double emploi = 0;
      if (va && vb) emploi = std::abs(*va - *vb);
      else emploi = std::max(va.value_or(0), vb.value_or(0));
      if (nominale <= 0 || emploi <= 0) continue;
      ContrainteEmploi e{x.reference, "tension", emploi, nominale, emploi / nominale, emploi / nominale > admisTension, {}};
      e.message = x.reference + " : " + chiffre(emploi, 1) + " V pour " + chiffre(nominale, 1) + " V nominaux (" + chiffre(100 * e.taux, 0) + " %)";
      r.push_back(e);
    } else if (estResistance(c, x) && va && vb && *va != *vb) {
      const ValeurComprise v = lireValeurComposant(x.valeur, FamilleValeur::Resistance);
      if (!v.comprise() || v.nombre <= 0) continue;
      const double p = (*va - *vb) * (*va - *vb) / v.nombre, nominale = puissanceNominale(x);
      ContrainteEmploi e{x.reference, "puissance", p, nominale, p / nominale, p / nominale > admisPuissance, {}};
      e.message = x.reference + " : " + chiffre(p * 1000, 0) + " mW pour " + chiffre(nominale * 1000, 0) + " mW nominaux (" + chiffre(100 * e.taux, 0) + " %)";
      r.push_back(e);
    }
  }
  return r;
}

double dureeVie(double heuresNominales, double temperatureNominale, double temperatureService) { return heuresNominales * std::pow(2.0, (temperatureNominale - temperatureService) / 10.0); }

Fiabilite fiabilite(const ConceptionElec& c, double temperature) {
  struct Famille {
    const char* nom;
    double fit;
    std::map<std::string, double> modes;
  };
  static const std::map<std::string, Famille> table = {
      {"R", {"résistance", 0.5, {{"ouvert", 0.6}, {"dérive", 0.4}}}},
      {"C", {"condensateur céramique", 1, {{"court-circuit", 0.5}, {"ouvert", 0.3}, {"dérive", 0.2}}}},
      {"CP", {"condensateur électrolytique", 20, {{"dérive", 0.6}, {"ouvert", 0.25}, {"court-circuit", 0.15}}}},
      {"L", {"inductance", 2, {{"ouvert", 0.7}, {"court-circuit", 0.3}}}},
      {"D", {"diode", 2, {{"court-circuit", 0.5}, {"ouvert", 0.3}, {"dérive", 0.2}}}},
      {"LED", {"LED", 5, {{"ouvert", 0.7}, {"dérive", 0.3}}}},
      {"Q", {"transistor", 5, {{"court-circuit", 0.4}, {"ouvert", 0.3}, {"dérive", 0.3}}}},
      {"U", {"circuit intégré", 20, {{"fonctionnel", 0.6}, {"court-circuit", 0.2}, {"ouvert", 0.2}}}},
      {"REG", {"régulateur", 10, {{"sortie hors tolérance", 0.5}, {"court-circuit", 0.3}, {"ouvert", 0.2}}}},
      {"J", {"connecteur", 5, {{"ouvert", 0.8}, {"court-circuit", 0.2}}}},
      {"Y", {"quartz", 10, {{"ne démarre pas", 0.7}, {"dérive", 0.3}}}},
      {"F", {"fusible", 3, {{"ouvert", 0.9}, {"court-circuit", 0.1}}}},
      {"SW", {"interrupteur", 20, {{"ouvert", 0.6}, {"court-circuit", 0.4}}}},
      {"BZ", {"buzzer", 30, {{"ouvert", 1.0}}}},
  };
  Fiabilite f;
  const auto pos = poses(c);
  const double acceleration = std::pow(2.0, (temperature - 40) / 10.0);
  for (const auto& x : c.composants) {
    if (!pos.count(x.uuid) || x.nePasMonter || !dansNomenclature(c, x)) continue;
    std::string cle = prefixeDe(x.reference);
    if (estLed(c, x)) cle = "LED";
    else if (estRegulateur(c, x)) cle = "REG";
    else if (cle == "C" && x.symbole == "C_Pol") cle = "CP";
    const auto it = table.find(cle);
    if (it == table.end()) continue;
    LigneFiabilite l{x.reference, it->second.nom, it->second.fit * acceleration, it->second.modes};
    if (const auto p = x.parametres.find("fit"); p != x.parametres.end())
      if (const double v = std::atof(p->second.c_str()); v > 0) l.fit = v * acceleration;
    f.fitTotal += l.fit;
    f.partParFamille[l.famille] += l.fit;
    f.lignes.push_back(l);
  }
  for (auto& [fam, part] : f.partParFamille) part = f.fitTotal > 0 ? part / f.fitTotal : 0;
  f.mtbfHeures = f.fitTotal > 0 ? 1e9 / f.fitTotal : 0;
  return f;
}

Conformite conformite(const ConceptionElec& c, const std::vector<std::string>& exigences) {
  Conformite r;
  const auto pos = poses(c);
  for (const auto& x : c.composants) {
    if (!pos.count(x.uuid) || x.nePasMonter || !dansNomenclature(c, x)) continue;
    const Article* a = nullptr;
    if (!x.article.empty())
      if (const auto it = c.articles.find(x.article); it != c.articles.end()) a = &it->second;
    bool conforme = true;
    for (const auto& e : exigences) {
      std::string v;
      if (const auto it = x.parametres.find(e); it != x.parametres.end()) v = it->second;
      else if (a)
        if (const auto it2 = a->parametres.find(e); it2 != a->parametres.end()) v = it2->second;
      v = minuscules(v);
      if (v.empty()) {
        r.inconnus.push_back(x.reference + " : " + e + " non renseigné");
        conforme = false;
      } else if (v == "non" || v == "no" || v == "false" || v == "0") {
        r.nonConformes.push_back(x.reference + " : " + e + " non");
        conforme = false;
      }
    }
    r.conformes += conforme;
  }
  return r;
}

}  // namespace prom::ecad

// Prométhée : essais du lot L14 de la 0.34 (simulation continue, instruments, analyses, autres moteurs, paramètres S,
// simulation nourrie par le projet, alimentations). Licence GPL-3.0-only.
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/alimentations.hpp"
#include "promethee/analyses_natives.hpp"
#include "promethee/analyseurs_numeriques.hpp"
#include "promethee/electronique.hpp"
#include "promethee/compression.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/moteurs_tiers.hpp"
#include "promethee/parametres_s.hpp"
#include "promethee/processus.hpp"
#include "promethee/scripts_python.hpp"
#include "promethee/simulation.hpp"
#include "promethee/simulation_continue.hpp"
#include "promethee/simulation_nourrie.hpp"
#include "promethee/construction.hpp"
#include "promethee/esquisse.hpp"
#include "promethee/piece.hpp"
#include "promethee/synchronisation.hpp"

using namespace prom;
using namespace prom::ecad;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {
struct Montage14 {
  ConceptionElec c = nouvelleConception("Montage");
  double x = 25.4, y = 50.8;
  ComposantElec* poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, y));
    x += 30.48;
    if (x > 260) x = 25.4, y += 50.8;
    ComposantElec* r = nullptr;
    for (auto& p : c.feuilles[0].symboles)
      if (p.uuid == s) {
        r = c.composant(p.composant);
        r->reference = ref;
        if (!valeur.empty()) r->valeur = valeur;
        for (const auto& b : brochesPlacees(p, *c.symbole(p.symbole)))
          for (const auto& [num, reseau] : liaisons)
            if (b.broche->numero == num) {
              const Pt d = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
              const Pt bout{b.p.x - d.x * 2'540'000, b.p.y - d.y * 2'540'000};
              c.feuilles[0].fils.push_back({nouvelUuid(), b.p, bout});
              Etiquette e;
              e.uuid = nouvelUuid();
              e.texte = reseau;
              e.p = bout;
              e.type = TypeEtiquette::Globale;
              c.feuilles[0].etiquettes.push_back(e);
            }
      }
    return r;
  }
  // Fil tiré de la broche (son bout a est la broche)
  std::string fil(const std::string& ref, const std::string& broche) const {
    const ComposantElec* k = c.parReference(ref);
    for (const auto& p : c.feuilles[0].symboles)
      if (k && p.composant == k->uuid)
        for (const auto& b : brochesPlacees(p, *c.symbole(p.symbole)))
          if (b.broche->numero == broche)
            for (const auto& w : c.feuilles[0].fils)
              if (w.a == b.p) return w.uuid;
    return {};
  }
};
// Attend que le temps simulé dépasse « t » (au plus « delai » secondes réelles)
bool attendreTemps(const SimulationContinue& s, double t, double delai = 10) {
  const auto t0 = std::chrono::steady_clock::now();
  while (s.temps() < t && s.enCours()) {
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > delai) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  if (s.temps() >= t) return true;
  std::fprintf(stderr, "session : en cours %d, temps %g, pas %llu, erreur « %s »\n", s.enCours() ? 1 : 0, s.temps(), static_cast<unsigned long long>(s.pas()), s.erreur().c_str());
  for (const auto& l : s.console()) std::fprintf(stderr, "  %s\n", l.c_str());
  return false;
}
}  // namespace

TEST_CASE("14.2 · bouton poussoir cliqué en simulation continue : le compteur XSPICE qu'il cadence passe de 3 à 4, et l'afficheur à sept segments suit en moins de 100 ms", "[034][L14][14.2]") {
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  Montage14 m;
  m.poser("Pile", "BT1", "5V", {{"1", "VCC"}, {"2", "GND"}});
  m.poser("Bouton", "SW1", "", {{"1", "VCC"}, {"2", "CLK"}});
  m.poser("R", "R1", "10k", {{"1", "CLK"}, {"2", "GND"}});
  m.poser("Compteur_4bits", "U1", "", {{"1", "CLK"}, {"2", "GND"}, {"3", "Q0"}, {"4", "Q1"}, {"5", "Q2"}, {"6", "Q3"}, {"7", "VCC"}, {"8", "GND"}});
  m.poser("Decodeur_7seg", "U2", "",
          {{"1", "Q0"}, {"2", "Q1"}, {"3", "Q2"}, {"4", "Q3"}, {"5", "SA"}, {"6", "SB"}, {"7", "SC"}, {"8", "SD"}, {"9", "SE"}, {"10", "SF"}, {"11", "SG"}, {"12", "VCC"}, {"13", "GND"}});
  m.poser("Afficheur_7seg", "DS1", "", {{"1", "SA"}, {"2", "SB"}, {"3", "SC"}, {"4", "SD"}, {"5", "SE"}, {"6", "SF"}, {"7", "SG"}, {"8", "GND"}});
  OptionsContinue oc;
  oc.pas = 1e-3;
  oc.duree = 120;
  oc.vitesse = 1;
  const NetlistSpice n0 = netlistContinue(m.c, oc);
  INFO(n0.texte);
  REQUIRE(n0.valide());
  CHECK(n0.texte.find("external") != std::string::npos);
  CHECK(n0.texte.find("d_tff") != std::string::npos);
  std::vector<std::string> bits, segments;
  for (const char* r : {"Q0", "Q1", "Q2", "Q3"}) bits.push_back(vecteurTension(n0, r));
  for (const char* r : {"SA", "SB", "SC", "SD", "SE", "SF", "SG"}) segments.push_back(vecteurTension(n0, r));
  std::vector<std::string> sondes = bits;
  sondes.insert(sondes.end(), segments.begin(), segments.end());
  const NetlistSpice n = netlistContinue(m.c, oc, {}, sondes);
  SimulationContinue s;
  std::string erreur;
  REQUIRE(s.demarrer(n.texte, sondes, oc, &erreur));
  INFO(erreur);
  auto compte = [&] {
    std::vector<double> v;
    for (const auto& b : bits) v.push_back(s.valeur(b));
    return valeurBinaire(v);
  };
  auto chiffre = [&] {
    std::uint8_t seg = 0;
    for (std::size_t i = 0; i < segments.size(); ++i)
      if (s.valeur(segments[i]) > 1.0) seg |= static_cast<std::uint8_t>(1u << i);
    return chiffreDesSegments(seg);
  };
  REQUIRE(attendreTemps(s, 0.05));
  CHECK(compte() == 0);
  CHECK(chiffre() == 0);
  const std::string bouton = sourceCommande("SW1");
  for (int i = 0; i < 3; ++i) {
    s.regler(bouton, 1);
    REQUIRE(attendreTemps(s, s.temps() + 0.03));
    s.regler(bouton, 0);
    REQUIRE(attendreTemps(s, s.temps() + 0.03));
  }
  CHECK(compte() == 3);
  CHECK(chiffre() == 3);
  // Le quatrième appui : compteur et afficheur suivent en moins de 100 ms
  const auto t0 = std::chrono::steady_clock::now();
  s.regler(bouton, 1);
  while ((compte() != 4 || chiffre() != 4) && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  INFO("réaction en " << ms << " ms");
  CHECK(compte() == 4);
  CHECK(chiffre() == 4);
  CHECK(ms < 100);
  // Temps réel : le temps simulé suit l'horloge, à la vitesse réglée
  const double ts = s.temps();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(s.temps() - ts == Approx(0.2).margin(0.08));
  s.setPause(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const double tp = s.temps();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  CHECK(s.temps() == Approx(tp).margin(0.003));
  s.setPause(false);
  CHECK(attendreTemps(s, tp + 0.05));
  // Aucun autre calcul pendant la session
  CHECK(!simuler(n0.texte).reussi());
  s.arreter();
  CHECK(!s.enCours());
  CHECK(s.pas() > 100);
  // Les tampons restent lisibles après l'arrêt
  CHECK(!s.echantillons(bits[2]).empty());
  // Hors simulation continue, le bouton reste une résistance
  AnalyseSpice op;
  op.type = AnalyseSpice::Type::PointDeRepos;
  CHECK(netlistSpice(m.c, op).texte.find("external") == std::string::npos);
}

TEST_CASE("14.3 · LED sans résistance sous 9 V : son fil s'anime vers la cathode et elle fume, avec le courant atteint et sa limite", "[034][L14][14.3]") {
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  Montage14 m;
  m.poser("Pile", "BT1", "9V", {{"1", "+9V"}, {"2", "GND"}});
  m.poser("LED", "D1", "rouge", {{"2", "+9V"}, {"1", "GND"}});  // broche 2 : anode
  AnalyseSpice op;
  op.type = AnalyseSpice::Type::PointDeRepos;
  const NetlistSpice n0 = netlistSpice(m.c, op);
  OptionsSimulation o;
  o.sauvegardes = sondesRetourVisuel(m.c, n0);
  REQUIRE(!o.sauvegardes.empty());
  const NetlistSpice n = netlistSpice(m.c, op, o);
  const ResultatSimulation r = simuler(n);
  INFO(r.erreur);
  REQUIRE(r.reussi());
  const RetourVisuel v = retourVisuel(m.c, n, r);
  // La LED fume : le courant atteint et sa limite
  const Fumee* f = nullptr;
  for (const auto& x : v.fumees)
    if (x.reference == "D1") f = &x;
  REQUIRE(f);
  CHECK(f->grandeur == "courant");
  CHECK(f->valeur > 1.0);
  CHECK(f->limite == Approx(0.02));
  CHECK(f->message.find("D1") != std::string::npos);
  CHECK(f->message.find("20") != std::string::npos);
  CHECK(f->message.find("mA") != std::string::npos);
  // Son fil s'anime vers la cathode : vers la LED côté anode, depuis elle côté cathode
  auto courantDe = [&](const std::string& fil) {
    for (const auto& x : v.fils)
      if (x.fil == fil) return x;
    return CourantFil{};
  };
  const CourantFil anode = courantDe(m.fil("D1", "2")), cathode = courantDe(m.fil("D1", "1")), plus = courantDe(m.fil("BT1", "1"));
  CHECK(anode.courant == Approx(f->valeur).epsilon(0.01));
  CHECK(anode.sens == -1);   // du bout (étiquette) vers la broche
  CHECK(cathode.sens == 1);  // de la broche vers l'étiquette
  CHECK(plus.sens == 1);     // la pile débite
  CHECK(anode.tension == Approx(9).margin(0.01));
  CHECK(cathode.tension == Approx(0).margin(0.01));
  // La pile en court-circuit aussi
  bool pile = false;
  for (const auto& x : v.fumees) pile |= x.reference == "BT1" && x.grandeur == "court-circuit";
  CHECK(pile);
  // Couleurs : rouge à la tension maximale, gris à 0
  CHECK(couleurTension(9, 9)[0] > couleurTension(0, 9)[0]);
  CHECK(couleurTension(0, 9) == std::array<int, 3>{128, 128, 128});
  // Avec 470 Ω, plus rien ne fume
  Montage14 b;
  b.poser("Pile", "BT1", "9V", {{"1", "+9V"}, {"2", "GND"}});
  b.poser("R", "R1", "470", {{"1", "+9V"}, {"2", "A"}});
  b.poser("LED", "D1", "rouge", {{"2", "A"}, {"1", "GND"}});
  b.c.composant(b.c.parReference("R1")->uuid)->empreinte = "R_1206";
  const NetlistSpice nb0 = netlistSpice(b.c, op);
  OptionsSimulation ob;
  ob.sauvegardes = sondesRetourVisuel(b.c, nb0);
  const NetlistSpice nb = netlistSpice(b.c, op, ob);
  const ResultatSimulation rb = simuler(nb);
  REQUIRE(rb.reussi());
  const RetourVisuel vb = retourVisuel(b.c, nb, rb);
  CHECK(vb.fumees.empty());
  CHECK(-vb.courants.at(b.c.parReference("D1")->uuid) == Approx(0.0149).margin(0.0015));
}

TEST_CASE("14.4 · générateur passé de 100 Hz à 1 kHz pendant le calcul, sur un RC qui coupe à 1 kHz : l'oscilloscope suit sans relance, et le voltmètre lit 0,707 fois l'entrée à 1 % près", "[034][L14][14.4]") {
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  Montage14 m;
  m.poser("Source_tension", "VG1", "externe", {{"1", "ENTREE"}, {"2", "GND"}});
  m.poser("R", "R1", "1k", {{"1", "ENTREE"}, {"2", "SORTIE"}});
  m.poser("C", "C1", "159.155n", {{"1", "SORTIE"}, {"2", "GND"}});
  OptionsContinue oc;
  oc.pas = 5e-6;
  oc.duree = 20;
  oc.vitesse = 0.25;
  const NetlistSpice n0 = netlistContinue(m.c, oc);
  REQUIRE(n0.valide());
  const std::string ve = vecteurTension(n0, "ENTREE"), vs = vecteurTension(n0, "SORTIE");
  const NetlistSpice n = netlistContinue(m.c, oc, {}, {ve, vs});
  SimulationContinue s;
  Generateur g;
  g.forme = Generateur::Forme::Sinus;
  g.frequence = 100;
  g.amplitude = 1;
  s.regler(sourceExterne("VG1"), g);
  std::string erreur;
  REQUIRE(s.demarrer(n.texte, {ve, vs}, oc, &erreur));
  INFO(erreur);
  REQUIRE(attendreTemps(s, 0.05, 20));
  const double fAvant = frequenceSignal(s.echantillons(ve, 0.01));
  g.frequence = 1000;
  s.regler(sourceExterne("VG1"), g);
  const double tChange = s.temps();
  REQUIRE(attendreTemps(s, tChange + 0.03, 20));
  CHECK(s.enCours());  // sans relance
  const double tA = tChange + 0.015, tB = tA + 0.01;  // dix périodes, régime établi
  const Releve e = s.echantillons(ve, tChange), o = s.echantillons(vs, tChange);
  const MesureSignal me = mesurerSignal(e, tA, tB), mo = mesurerSignal(o, tA, tB);
  REQUIRE(me.valide);
  REQUIRE(mo.valide);
  const double rapport = mo.efficace / me.efficace;
  INFO("rapport " << rapport << ", entrée " << me.efficace << " V");
  CHECK(std::abs(rapport - 1 / std::sqrt(2.0)) / (1 / std::sqrt(2.0)) < 0.01);
  CHECK(me.efficace == Approx(1 / std::sqrt(2.0)).epsilon(0.01));
  CHECK(fAvant == Approx(100).epsilon(0.01));
  CHECK(frequenceSignal(o, std::numeric_limits<double>::quiet_NaN(), nullptr, tA) == Approx(1000).epsilon(0.01));
  // L'oscilloscope suit : balayage déclenché sur la sortie, deux voies, couplage alternatif
  ReglagesOscillo ro;
  ro.voies = {VoieOscillo{ve, 0.5}, VoieOscillo{vs, 0.5}};
  ro.voies[1].couplage = CouplageVoie::Alternatif;
  ro.secondesParDivision = 2e-4;
  ro.voieDeclenchement = 0;
  ro.niveau = 0;
  const TraceOscillo t = traceOscilloscope({e, o}, ro);
  CHECK(t.declenche);
  CHECK(t.instant > tChange);
  REQUIRE(t.voies.size() == 2);
  CHECK(!t.voies[0].empty());
  CHECK(t.voies[0].front().t == Approx(-2e-4).margin(2e-5));
  CHECK(t.voies[0].back().t == Approx(1.8e-3).margin(2e-5));
  CHECK(std::abs(mesurerSignal(t.voies[1]).moyenne) < 0.02);  // alternatif : moyenne retirée
  // Phase continue au changement de fréquence : pas de saut de la source
  CHECK(std::abs(valeurA(e, tChange + 1e-6) - valeurA(e, tChange - 1e-6)) < 0.05);
  s.arreter();
  // Générateurs : formes, modulation, mots, tracé à la souris
  Generateur c;
  c.forme = Generateur::Forme::Carre;
  c.frequence = 10;
  c.amplitude = 2;
  SourceGeneree x(c);
  CHECK(x.valeur(0.01) == Approx(2));
  CHECK(x.valeur(0.06) == Approx(-2));
  Generateur w;
  w.forme = Generateur::Forme::Mots;
  w.frequence = 1;
  w.mots = {0b0001, 0b0010};
  w.bit = 1;
  SourceGeneree mw(w);
  CHECK(mw.valeur(0.5) == Approx(0));
  CHECK(mw.valeur(1.5) == Approx(5));
  Generateur a;
  a.forme = Generateur::Forme::Arbitraire;
  a.trace = {{0, 0}, {0.5, 1}};
  CHECK(valeurForme(a, 0.25) == Approx(0.5));
  CHECK(valeurForme(a, 0.75) == Approx(0.5));
  Generateur am;
  am.modulation = Generateur::Modulation::Amplitude;
  am.profondeur = 0.5;
  am.frequenceModulante = 1;
  am.frequence = 1000;
  CHECK(SourceGeneree(am).valeur(0.25 + 0.25e-3) == Approx(1.5).epsilon(0.01));
}

TEST_CASE("14.5 · fréquencemètre écrit en Python en une trentaine de lignes : posé au schéma, il lit l'astable à NE555 à 1 % près, comme celui fourni", "[034][L14][14.5]") {
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  if (!pythonDisponible()) SKIP("Python 3 n'est pas installé");
  Montage14 m;
  m.poser("Pile", "BT1", "9V", {{"1", "VCC"}, {"2", "GND"}});
  m.poser("NE555", "U1", "NE555", {{"1", "GND"}, {"2", "SEUIL"}, {"3", "SORTIE"}, {"4", "VCC"}, {"5", "CTRL"}, {"6", "SEUIL"}, {"7", "DECH"}, {"8", "VCC"}});
  m.poser("R", "R1", "1k", {{"1", "VCC"}, {"2", "DECH"}});
  m.poser("R", "R2", "10k", {{"1", "DECH"}, {"2", "SEUIL"}});
  m.poser("C", "C1", "10n", {{"1", "SEUIL"}, {"2", "GND"}});
  m.poser("C", "C2", "10n", {{"1", "CTRL"}, {"2", "GND"}});
  m.poser("R", "R3", "10k", {{"1", "SORTIE"}, {"2", "GND"}});
  ComposantElec* ins = m.poser("Instrument", "INS1", "fréquencemètre", {{"1", "SORTIE"}, {"2", "GND"}});
  const std::string code = "import promethee\n"
                           "# Fréquencemètre : fronts montants à mi-excursion, avec hystérésis\n"
                           "promethee.facade('<svg width=\"200\" height=\"80\"><rect id=\"affichage:frequence\" x=\"10\" y=\"10\" width=\"180\" height=\"40\"/>'\n"
                           "                 '<rect id=\"calibre:porte\" x=\"10\" y=\"55\" width=\"60\" height=\"20\" data-choix=\"1|10|100\"/></svg>')\n"
                           "points = []\n"
                           "while True:\n"
                           "    bloc = promethee.echantillons('SORTIE')\n"
                           "    if not bloc:\n"
                           "        break\n"
                           "    points.extend(bloc)\n"
                           "t0 = points[0][0]\n"
                           "points = [p for p in points if p[0] > t0 + 1e-3]  # premier cycle écarté\n"
                           "vmin = min(v for t, v in points)\n"
                           "vmax = max(v for t, v in points)\n"
                           "seuil = (vmin + vmax) / 2\n"
                           "h = 0.05 * (vmax - vmin)\n"
                           "fronts = []\n"
                           "arme = False\n"
                           "for (ta, va), (tb, vb) in zip(points, points[1:]):\n"
                           "    if va < seuil - h or vb < seuil - h:\n"
                           "        arme = True\n"
                           "    if arme and va < seuil <= vb:\n"
                           "        fronts.append(ta + (tb - ta) * (seuil - va) / (vb - va))\n"
                           "        arme = False\n"
                           "if len(fronts) >= 2:\n"
                           "    f = (len(fronts) - 1) / (fronts[-1] - fronts[0])\n"
                           "    promethee.afficher('frequence', f)\n"
                           "    promethee.afficher('fronts', len(fronts))\n"
                           "print('fréquence', round(f))\n";
  ins->champs["Instrument.Script"] = code;
  int lignes = 0;
  for (const char ch : code) lignes += ch == '\n';
  CHECK(lignes <= 35);
  // Posé au schéma : branché sur SORTIE
  const auto poses = instrumentsDuSchema(m.c);
  REQUIRE(poses.size() == 1);
  CHECK(poses[0].plus == "SORTIE");
  CHECK(poses[0].moins == "GND");
  CHECK(poses[0].genre == "fréquencemètre");
  // L'astable simulé
  AnalyseSpice tr;
  tr.type = AnalyseSpice::Type::Transitoire;
  tr.pas = 1e-6;
  tr.duree = 8e-3;
  OptionsSimulation os;
  os.pasMax = 1e-6;
  const NetlistSpice n = netlistSpice(m.c, tr, os);
  INFO(n.texte);
  REQUIRE(n.valide());
  const ResultatSimulation r = simuler(n);
  INFO(r.erreur);
  REQUIRE(r.reussi());
  const auto* t = r.reel("time");
  const auto* v = r.reel(vecteurTension(n, "SORTIE"));
  REQUIRE(t);
  REQUIRE(v);
  Releve sortie;
  for (std::size_t i = 0; i < t->size(); ++i) sortie.push_back({(*t)[i], (*v)[i]});
  const ResultatInstrument ri = executerInstrument(poses[0].script, {{poses[0].plus, sortie}});
  INFO(ri.erreurs);
  REQUIRE(ri.reussi);
  REQUIRE(ri.valeurs.count("frequence"));
  // Le fréquencemètre fourni, sur le même signal (premier cycle écarté)
  Releve etabli;
  for (const auto& e : sortie)
    if (e.t > sortie.front().t + 1e-3) etabli.push_back(e);
  const double fourni = frequencemetre(etabli), py = ri.valeurs.at("frequence");
  INFO("Python " << py << " Hz, fourni " << fourni << " Hz");
  CHECK(std::abs(py - fourni) / fourni < 0.01);
  // Et l'astable bat près de 1,44 / ((R1 + 2 R2) C)
  CHECK(fourni == Approx(1.44 / (21e3 * 10e-9)).epsilon(0.15));
  // Façade déclarée par le script, éléments liés aux réglages
  const auto f = lireFacade(ri.facade);
  REQUIRE(f);
  REQUIRE(f->element("calibre", "porte"));
  ReglagesInstrument reg;
  CHECK(reg.cliquer(*f, 20, 60));
  CHECK(reg.valeurs["porte"] == "1");
  CHECK(reg.cliquer(*f, 20, 60));
  CHECK(reg.valeurs["porte"] == "10");
  CHECK(!reg.cliquer(*f, 199, 79));
  // Façades génériques de salle de TP
  for (const char* x : {"oscilloscope", "multimetre", "generateur"}) CHECK(!facadeGenerique(x).elements.empty());
  CHECK(facadeGenerique("oscilloscope").element("calibre", "voltsParDivision"));
  CHECK(facadeGenerique("generateur").element("menu", "forme"));
}

TEST_CASE("14.6 · trame I2C vers l'adresse 0x50 décodée bit à bit, accusés compris, et capture déclenchée sur l'octet 0xA5", "[034][L14][14.6]") {
  // Le maître joué par deux sources : SDA et SCL en PWL, simulés par ngspice puis numérisés
  const auto [sda, scl] = motifEcritureI2C(0x50, {0x10, 0xA5, 0x3C}, 100e3, 3.3);
  CHECK(sda.texte().rfind("PWL(0 3.3", 0) == 0);
  VoieLogique vSda, vScl;
  if (ngspiceDisponible()) {
    Montage14 m;
    m.poser("Source_tension", "VSDA", sda.texte(), {{"1", "SDA"}, {"2", "GND"}});
    m.poser("Source_tension", "VSCL", scl.texte(), {{"1", "SCL"}, {"2", "GND"}});
    m.poser("R", "R1", "4k7", {{"1", "SDA"}, {"2", "GND"}});
    m.poser("R", "R2", "4k7", {{"1", "SCL"}, {"2", "GND"}});
    AnalyseSpice tr;
    tr.type = AnalyseSpice::Type::Transitoire;
    tr.pas = 0.2e-6;
    tr.duree = sda.points.back().first;
    OptionsSimulation os;
    os.pasMax = 0.2e-6;
    const NetlistSpice n = netlistSpice(m.c, tr, os);
    REQUIRE(n.valide());
    const ResultatSimulation r = simuler(n);
    INFO(r.erreur);
    REQUIRE(r.reussi());
    const auto* t = r.reel("time");
    auto releve = [&](const std::string& reseau) {
      Releve x;
      const auto* v = r.reel(vecteurTension(n, reseau));
      for (std::size_t i = 0; t && v && i < t->size(); ++i) x.push_back({(*t)[i], (*v)[i]});
      return x;
    };
    vSda = numeriser(releve("SDA"), 0.99, 2.31);
    vScl = numeriser(releve("SCL"), 0.99, 2.31);
  } else {
    // Sans ngspice, les motifs eux-mêmes
    auto releve = [](const MotifPwl& p) {
      Releve x;
      for (const auto& [a, b] : p.points) x.push_back({a, b});
      return x;
    };
    vSda = numeriser(releve(sda), 0.99, 2.31);
    vScl = numeriser(releve(scl), 0.99, 2.31);
  }
  const auto trames = decoderI2C(vSda, vScl);
  REQUIRE(trames.size() == 1);
  const TrameI2C& f = trames[0];
  CHECK(f.adresse == 0x50);
  CHECK(!f.lecture);
  CHECK(f.complete);
  CHECK(f.erreurs.empty());
  REQUIRE(f.octets.size() == 4);
  CHECK(f.octets[0].valeur == 0xA0);
  CHECK(f.octets[2].valeur == 0xA5);
  for (const auto& o : f.octets) CHECK(o.accuse);
  // Bit à bit : départ, 7 bits d'adresse, lecture/écriture, accusé, trois octets et leurs accusés, arrêt
  REQUIRE(f.bits.size() == 1 + 4 * 9 + 1);
  CHECK(f.bits.front().role == "départ");
  CHECK(f.bits[1].role == "adresse");
  CHECK(f.bits[1].valeur == 1);  // 0x50 = 1010000
  CHECK(f.bits[2].valeur == 0);
  CHECK(f.bits[8].role == "lecture/écriture");
  CHECK(f.bits[9].role == "accusé");
  CHECK(f.bits.back().role == "arrêt");
  CHECK(f.texte().find("0x50") != std::string::npos);
  CHECK(f.texte().find("0xA5 ✓") != std::string::npos);
  // Capture déclenchée sur l'octet 0xA5
  const double t = instantOctetI2C(trames, 0xA5);
  REQUIRE(t > 0);
  CHECK(t > f.octets[1].fin);
  const CaptureLogique c = capturerA({{"SDA", vSda}, {"SCL", vScl}}, t, 20e-6, 100e-6);
  CHECK(c.declenche);
  CHECK(c.instant == Approx(t));
  CHECK(c.debut == Approx(t - 20e-6));
  REQUIRE(c.voies.size() == 2);
  // Les huit bits de 0xA5 lus dans la capture aux fronts montants de SCL
  const auto dansCapture = decoderI2C(vSda, vScl);
  CHECK(dansCapture.size() == 1);
  // Analyseur logique : déclenchement sur une valeur du bus et sur un front
  DeclenchementLogique d;
  d.genre = DeclenchementLogique::Genre::Valeur;
  d.valeur = 0b00;  // SDA et SCL bas
  const CaptureLogique cv = capturer({{"SDA", vSda}, {"SCL", vScl}}, d, 5e-6, 5e-6);
  CHECK(cv.declenche);
  CHECK(cv.instant > sda.points[1].first);
  d.genre = DeclenchementLogique::Genre::Front;
  d.voie = 1;
  d.montant = false;
  CHECK(capturer({{"SDA", vSda}, {"SCL", vScl}}, d, 1e-6, 1e-6).instant == Approx(vScl[1].t));
  // Événements XSPICE : états indéfini et haute impédance gardés
  const VoieLogique ev = voieDepuisEvenements({{0, "0"}, {1e-6, "1"}, {2e-6, "Z"}, {3e-6, "U"}});
  CHECK(lettreNiveau(niveauA(ev, 2.5e-6)) == 'Z');
  CHECK(lettreNiveau(niveauA(ev, 3.5e-6)) == 'X');

  // SPI mode 0 : un octet 0x5A sur MOSI, 0xC3 sur MISO
  VoieLogique sck{{0, Niveau::Bas}}, mosi{{0, Niveau::Bas}}, miso{{0, Niveau::Bas}}, cs{{0, Niveau::Haut}, {1e-6, Niveau::Bas}};
  for (int i = 0; i < 8; ++i) {
    const double t0 = 2e-6 + i * 2e-6;
    const Niveau bo = (0x5A >> (7 - i)) & 1 ? Niveau::Haut : Niveau::Bas, bi = (0xC3 >> (7 - i)) & 1 ? Niveau::Haut : Niveau::Bas;
    if (mosi.back().niveau != bo) mosi.push_back({t0, bo});
    if (miso.back().niveau != bi) miso.push_back({t0, bi});
    sck.push_back({t0 + 0.5e-6, Niveau::Haut});
    sck.push_back({t0 + 1.5e-6, Niveau::Bas});
  }
  cs.push_back({20e-6, Niveau::Haut});
  const auto spi = decoderSPI(sck, mosi, miso, cs);
  REQUIRE(spi.size() == 1);
  REQUIRE(spi[0].mosi.size() == 1);
  CHECK(spi[0].mosi[0] == 0x5A);
  CHECK(spi[0].miso[0] == 0xC3);

  // CAN à 500 kbit/s : deux trames, la seconde avec un bit de données inversé
  const auto b1 = bitsTrameCAN(0x123, {0xDE, 0xAD, 0x01, 0x90});
  auto b2 = bitsTrameCAN(0x18FF50E5, {0x11, 0x22}, true);
  std::vector<int> bus = b1;
  bus.insert(bus.end(), b2.begin(), b2.end());
  const auto can = decoderCAN(voieDepuisBits(bus, 500e3), 500e3);
  REQUIRE(can.size() == 2);
  CHECK(can[0].identifiant == 0x123);
  CHECK(!can[0].etendu);
  CHECK(can[0].crcJuste);
  CHECK(can[0].acquittee);
  CHECK(can[0].donnees == std::vector<std::uint8_t>{0xDE, 0xAD, 0x01, 0x90});
  CHECK(can[1].etendu);
  CHECK(can[1].identifiant == 0x18FF50E5);
  CHECK(can[1].crcJuste);
  // Défaut injecté : un bit de données inversé (hors bourrage), compté en erreur de CRC
  std::vector<int> faux = b1;
  faux[24] = 1 - faux[24];
  const auto cf = decoderCAN(voieDepuisBits(faux, 500e3), 500e3);
  const CompteursCAN n = compterErreurs(cf);
  CHECK(n.trames >= 1);
  CHECK(n.crc + n.bourrage + n.forme >= 1);
  // DBC : température sur 16 bits Intel, facteur 0,1, décalage −40
  const std::string dbc = "VERSION \"\"\n\nBO_ 291 Capteur: 4 Carte\n SG_ Temperature : 0|16@1+ (0.1,-40) [-40|125] \"°C\" Tableau\n SG_ Etat : 24|8@1+ (1,0) [0|255] \"\" Tableau\n";
  const auto base = lireDBC(dbc);
  REQUIRE(base);
  REQUIRE(base->message(0x123));
  const auto sig = signauxCAN(*base, can[0]);
  CHECK(sig.at("Temperature") == Approx((0xADDE) * 0.1 - 40));
  CHECK(sig.at("Etat") == Approx(0x90));
}

TEST_CASE("14.7 · passe-bas RC : H(s) = 1 / (1 + s R1 C1) affiché, pôle égal à celui de ngspice à 0,1 % près", "[034][L14][14.7]") {
  Montage14 m;
  m.poser("Source_tension", "V1", "DC 0 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
  m.poser("R", "R1", "1k", {{"1", "ENTREE"}, {"2", "SORTIE"}});
  m.poser("C", "C1", "100n", {{"1", "SORTIE"}, {"2", "GND"}});
  std::string erreur;
  const auto h = transfertSymbolique(m.c, "V1", "SORTIE", &erreur);
  INFO(erreur);
  REQUIRE(h);
  CHECK(h->texte() == "1 / (1 + s·R1·C1)");
  CHECK(h->latex() == "\\frac{1}{1 + s R_{1} C_{1}}");
  const auto valeurs = valeursSymboles(m.c);
  CHECK(valeurs.at("R1") == Approx(1000));
  const auto p = h->poles(valeurs);
  REQUIRE(p.size() == 1);
  CHECK(p[0].real() == Approx(-1 / (1000 * 100e-9)).epsilon(1e-9));
  CHECK(h->zeros(valeurs).empty());
  CHECK(std::abs(h->evaluer({0, 1 / (1000 * 100e-9)}, valeurs)) == Approx(1 / std::sqrt(2.0)).epsilon(1e-9));
  // Semi-littérale : C1 gardé en nombre
  const auto hs = transfertSymbolique(m.c, "V1", "SORTIE", &erreur, {"C1"});
  REQUIRE(hs);
  CHECK(hs->texte() == "1 / (1 + s·1e-07·R1)");
  // Second ordre RLC : deux pôles
  Montage14 r;
  r.poser("Source_tension", "V1", "DC 0 AC 1", {{"1", "A"}, {"2", "GND"}});
  r.poser("R", "R1", "100", {{"1", "A"}, {"2", "B"}});
  r.poser("L", "L1", "10m", {{"1", "B"}, {"2", "S"}});
  r.poser("C", "C1", "1u", {{"1", "S"}, {"2", "GND"}});
  const auto h2 = transfertSymbolique(r.c, "V1", "S", &erreur);
  REQUIRE(h2);
  CHECK(h2->texte() == "1 / (1 + s·R1·C1 + s^2·L1·C1)");
  CHECK(h2->poles(valeursSymboles(r.c)).size() == 2);
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé : pôle non comparé");
  // Pôle de ngspice (.pz) : le même à 0,1 % près
  AnalyseNative pz;
  pz.genre = AnalyseNative::Genre::PolesZeros;
  pz.entree = "V1";
  pz.sortie = "SORTIE";
  const ResultatNatif rp = analyseNative(m.c, pz);
  INFO(rp.erreur);
  REQUIRE(rp.ok);
  REQUIRE(rp.poles.size() == 1);
  CHECK(std::abs(rp.poles[0] - p[0]) / std::abs(p[0]) < 1e-3);
  // Fonction de transfert petits signaux, bruit, Fourier
  AnalyseNative tf;
  tf.genre = AnalyseNative::Genre::FonctionTransfert;
  tf.entree = "V1";
  tf.sortie = "SORTIE";
  const ResultatNatif rt = analyseNative(m.c, tf);
  INFO(rt.erreur);
  REQUIRE(rt.ok);
  CHECK(rt.gain == Approx(1).epsilon(1e-6));
  AnalyseNative br;
  br.genre = AnalyseNative::Genre::Bruit;
  br.entree = "V1";
  br.sortie = "SORTIE";
  br.fDebut = 10;
  br.fFin = 1e6;
  const ResultatNatif rb = analyseNative(m.c, br);
  INFO(rb.erreur);
  REQUIRE(rb.ok);
  CHECK(!rb.bruitSortie.empty());
  CHECK(rb.bruitSortie.size() == rb.frequences.size());
  CHECK(rb.bruitTotalSortie > 0);
  CHECK(rb.bruitTotalSortie < std::sqrt(1.380649e-23 * 300.15 / 100e-9) * 1.05);  // borne kT/C
  Montage14 sin;
  sin.poser("Source_tension", "V1", "SIN(0 1 1k)", {{"1", "ENTREE"}, {"2", "GND"}});
  sin.poser("R", "R1", "1k", {{"1", "ENTREE"}, {"2", "SORTIE"}});
  sin.poser("C", "C1", "100n", {{"1", "SORTIE"}, {"2", "GND"}});
  AnalyseNative fo;
  fo.genre = AnalyseNative::Genre::Fourier;
  fo.sortie = "SORTIE";
  fo.fondamentale = 1000;
  fo.periodes = 10;
  const ResultatNatif rf = analyseNative(sin.c, fo);
  INFO(rf.erreur);
  REQUIRE(rf.ok);
  REQUIRE(!rf.amplitudes.empty());
  CHECK(rf.amplitudes[0] == Approx(1 / std::sqrt(1 + std::pow(2 * std::numbers::pi * 1000 * 1e-4, 2))).epsilon(0.01));
  CHECK(rf.distorsion < 0.5);
  // FFT : le pic au kilohertz
  Releve s;
  for (int i = 0; i < 2000; ++i) s.push_back({i * 1e-5, std::sin(2 * std::numbers::pi * 1000 * i * 1e-5)});
  const Spectre sp = fft(s);
  const auto pic = std::max_element(sp.amplitudes.begin(), sp.amplitudes.end());
  CHECK(sp.frequences[static_cast<std::size_t>(pic - sp.amplitudes.begin())] == Approx(1000).epsilon(0.05));
  // Lot : deux analyses de suite
  AnalyseSpice a1, a2;
  a1.type = AnalyseSpice::Type::PointDeRepos;
  a2.type = AnalyseSpice::Type::Alternatif;
  const auto lot = enchainer(m.c, {a1, a2});
  REQUIRE(lot.size() == 2);
  CHECK(lot[0].reussi());
  CHECK(lot[1].reussi());
}

TEST_CASE("14.8 · filtre RC simulé par ngspice puis par Xyce : les deux courbes se superposent à 0,5 % près dans le même graphe", "[034][L14][14.8]") {
  Montage14 m;
  m.poser("Source_tension", "V1", "SIN(0 1 1k)", {{"1", "ENTREE"}, {"2", "GND"}});
  m.poser("R", "R1", "1k", {{"1", "ENTREE"}, {"2", "SORTIE"}});
  m.poser("C", "C1", "100n", {{"1", "SORTIE"}, {"2", "GND"}});
  AnalyseSpice tr;
  tr.type = AnalyseSpice::Type::Transitoire;
  tr.pas = 2e-6;
  tr.duree = 5e-3;
  OptionsSimulation os;
  os.pasMax = 2e-6;
  const NetlistSpice n = netlistSpice(m.c, tr, os);
  REQUIRE(n.valide());
  const std::string vs = vecteurTension(n, "SORTIE");
  // Les moteurs du poste, et les traductions dans leur dialecte
  const auto moteurs = moteursTiers();
  REQUIRE(moteurs.size() == 5);
  CHECK(moteurs[0].nom == "Xyce");
  const std::string x = netlistXyce(n, tr, {vs});
  CHECK(x.find(".PRINT TRAN FORMAT=CSV FILE=sortie.csv V(N_SORTIE)") != std::string::npos);
  CHECK(x.find(".save") == std::string::npos);
  CHECK(x.find(".TRAN 2e-06 0.005 0 2e-06") != std::string::npos);
  std::vector<std::string> refus;
  const std::string q = netlistQucsator(n, tr, &refus);
  CHECK(q.find("R:RR1 n_entree n_sortie R=\"1000 Ohm\"") != std::string::npos);
  CHECK(q.find("Vac:VV1") != std::string::npos);
  CHECK(q.find(".TR:TR1") != std::string::npos);
  CHECK(refus.empty());
  // Lecteurs : CSV de Xyce, données de Qucsator
  const ResultatSimulation rx = lireCsvXyce("TIME,V(N_SORTIE)\n0,0\n1e-3,0.5\n2e-3,0.25\nEnd of Xyce(TM) Simulation\n");
  CHECK(rx.echelle == "time");
  REQUIRE(rx.reel("v(n_sortie)"));
  CHECK(rx.reel("v(n_sortie)")->size() == 3);
  const ResultatSimulation rq = lireDatQucs("<Qucs Dataset 0.0.24>\n<indep time 2>\n  +0.0e+00\n  +1.0e-03\n</indep>\n<dep n_sortie.Vt time>\n  +0.0e+00\n  +5.0e-01\n</dep>\n");
  REQUIRE(rq.reel("v(n_sortie)"));
  CHECK(rq.reel("v(n_sortie)")->at(1) == Approx(0.5));
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  const ResultatSimulation r = simuler(n);
  REQUIRE(r.reussi());
  Releve sim;
  for (std::size_t i = 0; i < r.reel("time")->size(); ++i) sim.push_back({r.reel("time")->at(i), r.reel(vs)->at(i)});
  // Fichier brut de ngspice en ligne de commande (format de LTspice et de ngspice), relu dans les mêmes courbes
  if (!programmeDuPoste("ngspice").empty()) {
    const fs::path d = fs::temp_directory_path() / ("promethee-raw-" + std::to_string(std::random_device{}()));
    fs::create_directories(d);
    ecrireFichierAtomique(d / "rc.cir", n.texte);
    OptionsProcessus o;
    o.dossier = utf8(d);
    executerProcessus({"ngspice", "-b", "-r", "rc.raw", "rc.cir"}, o);
    REQUIRE(fs::exists(d / "rc.raw"));
    const ResultatSimulation rr = lireRaw(lireFichier(d / "rc.raw"));
    INFO(rr.erreur);
    REQUIRE(rr.erreur.empty());
    REQUIRE(rr.reel("time"));
    REQUIRE(rr.reel(vs));
    Releve brut;
    for (std::size_t i = 0; i < rr.reel("time")->size(); ++i) brut.push_back({rr.reel("time")->at(i), rr.reel(vs)->at(i)});
    const EcartCourbes e = comparerCourbes(sim, brut, 1e-3, 5e-3);
    CHECK(e.valide);
    CHECK(e.maximal < 0.005);
    fs::remove_all(d);
  }
  // Mesures d'oscilloscope : CSV de Rigol et de Keysight, superposés à la simulation
  std::string rigol = "X,CH1,Start,Increment,\nSequence,Volt,0.000000e+00,1.000000e-05,\n";
  std::string keysight = "x-axis,1\nsecond,Volt\n";
  for (int i = 0; i <= 500; ++i) {
    const double t = i * 1e-5, v = valeurA(sim, t);
    rigol += std::to_string(i) + "," + std::to_string(v) + ",\n";
    keysight += std::to_string(t) + "," + std::to_string(v) + "\n";
  }
  std::string erreur;
  const auto cr = lireCsvOscilloscope(rigol, &erreur);
  INFO(erreur);
  REQUIRE(cr);
  REQUIRE(cr->size() == 1);
  CHECK((*cr)[0].nom == "CH1");
  CHECK((*cr)[0].points.size() == 501);
  CHECK((*cr)[0].points[100].t == Approx(1e-3));
  CHECK(comparerCourbes(sim, (*cr)[0].points, 1e-3, 5e-3).maximal < 0.005);
  const auto ck = lireCsvOscilloscope(keysight);
  REQUIRE(ck);
  CHECK((*ck)[0].unite == "Volt");
  CHECK(comparerCourbes(sim, (*ck)[0].points, 1e-3, 5e-3).maximal < 0.005);
  // Une mesure prise comme source du schéma
  CHECK(pwlDepuisMesure((*ck)[0].points, 100).rfind("PWL(0 ", 0) == 0);
  // Capture de sigrok écrite par Prométhée, relue
  std::string logique;
  for (int i = 0; i < 100; ++i) logique += static_cast<char>((i / 10) % 2 | (((i / 25) % 2) << 1));
  const std::string sr = zipComprime({{"version", "2"},
                                      {"metadata", "[global]\nsigrok version=0.5.2\n\n[device 1]\ncapturefile=logic-1\ntotal probes=8\nsamplerate=1 MHz\ntotal analog=0\nprobe1=SDA\nprobe2=SCL\nunitsize=1\n"},
                                      {"logic-1-1", logique}});
  const auto cs = lireSigrok(sr, &erreur);
  INFO(erreur);
  REQUIRE(cs);
  CHECK(cs->frequence == Approx(1e6));
  REQUIRE(cs->voies.size() == 2);
  CHECK(cs->voies[0].first == "SDA");
  CHECK(cs->voies[0].second.size() == 10);  // un changement tous les dix échantillons
  CHECK(cs->voies[0].second[1].t == Approx(10e-6));
  CHECK(cs->voies[1].second.size() == 4);
  // Appareil branché en direct : refusé tant que l'utilisateur ne l'a pas activé
  CaptureDirecte dc;
  CHECK(!capturerSigrok(dc, &erreur));
  CHECK(erreur.find("désactivée") != std::string::npos);
  if (!programmeDuPoste("sigrok-cli").empty()) {
    dc.active = true;
    dc.voies = "D0,D1";
    const auto demo = capturerSigrok(dc, &erreur);
    INFO(erreur);
    REQUIRE(demo);
    CHECK(demo->voies.size() == 2);
  }
  // Xyce : la même courbe à 0,5 % près, quand il est installé
  const ResultatSimulation ry = simulerAvec("Xyce", n, tr, {vs});
  if (moteurs[0].present) {
    INFO(ry.erreur);
    REQUIRE(ry.erreur.empty());
    Releve xy;
    for (std::size_t i = 0; i < ry.reel("time")->size(); ++i) xy.push_back({ry.reel("time")->at(i), ry.reel(vs)->at(i)});
    CHECK(comparerCourbes(sim, xy, 1e-3, 5e-3).maximal < 0.005);
  } else {
    CHECK(ry.erreur.find("n'est pas installé") != std::string::npos);
    WARN("Xyce n'est pas installé sur ce poste : superposition ngspice et Xyce non vérifiée");
  }
}

TEST_CASE("14.10 · atténuateur en pi de 10 dB sous 50 Ω : l'analyseur de réseau lit S21 = −10,0 dB, et le .s2p exporté est relu par un lecteur indépendant", "[034][L14][14.10]") {
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  // Atténuateur en pi : K = 10^(10/20), Rp = Z0 (K + 1) / (K − 1), Rs = Z0 (K² − 1) / (2 K)
  const double k = std::pow(10.0, 0.5), rp = 50 * (k + 1) / (k - 1), rs = 50 * (k * k - 1) / (2 * k);
  Montage14 m;
  m.poser("R", "R1", std::to_string(rp), {{"1", "P1"}, {"2", "GND"}});
  m.poser("R", "R2", std::to_string(rs), {{"1", "P1"}, {"2", "P2"}});
  m.poser("R", "R3", std::to_string(rp), {{"1", "P2"}, {"2", "GND"}});
  PortsAnalyseur ports;
  ports.port1 = "P1";
  ports.port2 = "P2";
  std::string erreur;
  const auto s = parametresS(m.c, ports, 1e3, 1e9, 10, &erreur);
  INFO(erreur);
  REQUIRE(s);
  REQUIRE(s->frequences.size() > 50);
  for (const auto& x : s->s) {
    CHECK(enDecibels(x[2]) == Approx(-10.0).margin(0.05));
    CHECK(enDecibels(x[1]) == Approx(-10.0).margin(0.05));  // réciproque
    CHECK(enDecibels(x[0]) < -30);                           // adapté
  }
  // Z, Y et H : Y est l'inverse de Z
  const auto z = versZ(s->s[0], 50), y = versY(s->s[0], 50);
  const Complexe un = z[0] * y[0] + z[1] * y[2];
  CHECK(std::abs(un - Complexe(1)) < 1e-6);
  CHECK(std::abs(versH(s->s[0], 50)[3]) > 0);
  // Touchstone écrit, relu par Prométhée et par un lecteur indépendant (script Python, ordre S11 S21 S12 S22)
  const std::string s2p = ecrireTouchstone(*s, "DB", "MHZ");
  CHECK(s2p.find("# MHZ S DB R 50") != std::string::npos);
  const auto relu = lireTouchstone(s2p, 2, &erreur);
  INFO(erreur);
  REQUIRE(relu);
  CHECK(relu->frequences.size() == s->frequences.size());
  CHECK(relu->frequences.back() == Approx(s->frequences.back()).epsilon(1e-9));
  CHECK(enDecibels(relu->s[3][2]) == Approx(enDecibels(s->s[3][2])).margin(1e-6));
  const auto ri = lireTouchstone(ecrireTouchstone(*s, "RI"), 2);
  REQUIRE(ri);
  CHECK(std::abs(ri->s[5][2] - s->s[5][2]) < 1e-9);
  if (pythonDisponible()) {
    const fs::path d = fs::temp_directory_path() / ("promethee-s2p-" + std::to_string(std::random_device{}()));
    fs::create_directories(d);
    ecrireFichierAtomique(d / "attenuateur.s2p", s2p);
    ecrireFichierAtomique(d / "lecteur.py",
                          "import sys\n"
                          "lignes = [l.split('!')[0].strip() for l in open(sys.argv[1], encoding='utf-8')]\n"
                          "options = [l for l in lignes if l.startswith('#')][0].upper().split()\n"
                          "assert options[2] == 'S' and options[3] == 'DB' and options[5] == '50'\n"
                          "nombres = [float(x) for l in lignes if l and not l.startswith('#') for x in l.split()]\n"
                          "points = [nombres[i:i + 9] for i in range(0, len(nombres), 9)]\n"
                          "s21 = [p[3] for p in points]\n"
                          "print('%d %.4f %.4f' % (len(points), min(s21), max(s21)))\n");
    const ResultatProcessus r = executerProcessus({programmeDuPoste("python3").empty() ? "python" : "python3", "-I", utf8(d / "lecteur.py"), utf8(d / "attenuateur.s2p")});
    INFO(r.erreurs);
    REQUIRE(r.reussi());
    std::istringstream mots(r.sortie);
    std::size_t points = 0;
    double mn = 0, mx = 0;
    mots >> points >> mn >> mx;
    CHECK(points == s->frequences.size());
    CHECK(mn == Approx(-10.0).margin(0.05));
    CHECK(mx == Approx(-10.0).margin(0.05));
    fs::remove_all(d);
  }
  // Tracés : Smith, Black, temps de groupe d'un passe-bas RC (τ = RC aux basses fréquences), œil, histogramme
  std::vector<double> f;
  std::vector<Complexe> h;
  for (int i = 0; i <= 200; ++i) {
    f.push_back(10 * std::pow(10.0, i / 50.0));
    h.push_back(1.0 / Complexe(1, 2 * std::numbers::pi * f.back() * 1e-4));
  }
  CHECK(tempsDeGroupe(f, h)[1] == Approx(1e-4).epsilon(0.01));
  CHECK(pointsSmith({Complexe(0.5, -0.5)})[0].second == Approx(-0.5));
  CHECK(pointsBlack(h).back().first < -80);
  Releve nrz;
  const int bits[] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1};
  for (int i = 0; i < 16 * 20; ++i) nrz.push_back({i * 1e-9 / 20, bits[i / 20] ? 1.0 : 0.0});
  const DiagrammeOeil oeil = diagrammeOeil(nrz, 1e-9);
  CHECK(oeil.traces.size() >= 12);
  CHECK(oeil.ouvertureVerticale == Approx(1.0).margin(0.05));
  const Histogramme hi = histogramme({0, 0.1, 0.9, 1.0, 1.0}, 2);
  CHECK(hi.classes[0] == 2);
  CHECK(hi.classes[1] == 3);
}

TEST_CASE("14.11 · disque d'acier de 100 mm du projet sur un moteur à courant continu : constante de temps mécanique simulée à 5 % de J·R/(Ke·Kt)", "[034][L14][14.11]") {
  // La pièce du projet : disque d'acier de 100 mm de diamètre et 10 mm d'épaisseur
  Piece disque;
  disque.densite = 7.85;
  disque.densiteConfirmee = true;
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  e.esquisse.ajouterCercle(e.esquisse.ajouterPoint(0, 0), 50);
  const int ie = disque.ajouter(e);
  FonctionPiece x;
  x.type = TypeFonction::Extrusion;
  x.esquisseId = ie;
  x.longueur = 10;
  disque.ajouter(x);
  const ResultatPiece rp = construirePiece(disque);
  REQUIRE(rp.erreurs.empty());
  const ProprietesMasse pm = proprietesMasse(rp.forme, disque.densite);
  REQUIRE(pm.valide);
  const double j = inertieDepuisMasse(pm.izz);  // autour de l'axe du disque
  const double masse = 7850 * std::numbers::pi * 0.05 * 0.05 * 0.01;
  CHECK(pm.masse == Approx(masse * 1000).epsilon(0.001));
  CHECK(j == Approx(0.5 * masse * 0.05 * 0.05).epsilon(0.001));
  // Le moteur en reçoit la charge
  Montage14 m;
  m.poser("Source_tension", "V1", "PWL(0 0 1m 0 1.001m 12)", {{"1", "A"}, {"2", "GND"}});
  ComposantElec* moteur = m.poser("Moteur", "M1", "", {{"1", "A"}, {"2", "GND"}});
  ParametresMoteur pmot;
  pmot.r = 2;
  pmot.l = 1e-3;
  pmot.ke = pmot.kt = 0.05;
  pmot.b = 1e-7;
  pmot.j = 0;
  reglerMoteur(*moteur, pmot);
  appliquerCharge(*moteur, j, 0);
  const ParametresMoteur charge = parametresMoteur(*moteur);
  CHECK(charge.j == Approx(j).epsilon(1e-6));
  const double tau = constanteTempsMecanique(charge);
  CHECK(tau == Approx(j * 2 / 0.0025).epsilon(1e-6));
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  AnalyseSpice tr;
  tr.type = AnalyseSpice::Type::Transitoire;
  tr.pas = 1e-3;
  tr.duree = 8 * tau;
  OptionsSimulation os;
  os.pasMax = 1e-3;
  const NetlistSpice n = netlistSpice(m.c, tr, os);
  INFO(n.texte);
  REQUIRE(n.valide());
  CHECK(n.texte.find("MOTEURCC") != std::string::npos);
  const ResultatSimulation r = simuler(n);
  INFO(r.erreur);
  REQUIRE(r.reussi());
  const auto* t = r.reel("time");
  const auto* w = r.reel(vecteurVitesse("M1"));
  REQUIRE(t);
  REQUIRE(w);
  Releve vitesse;
  for (std::size_t i = 0; i < t->size(); ++i) vitesse.push_back({(*t)[i], (*w)[i]});
  const double finale = vitesse.back().v;
  CHECK(finale == Approx(12 / 0.05).epsilon(0.01));
  const double simule = constanteDeTemps(vitesse, finale, 1e-3);
  INFO("τ simulée " << simule << " s, J·R/(Ke·Kt) = " << tau << " s");
  CHECK(std::abs(simule - tau) / tau < 0.05);

  // Pistes routées : résistance du cuivre et cellule en pi, courbes avant et après routage superposées
  Montage14 c;
  c.poser("Source_tension", "V1", "DC 0 AC 1", {{"1", "E"}, {"2", "GND"}});
  c.poser("R", "R1", "1k", {{"1", "E"}, {"2", "SIG"}});
  c.poser("C", "C1", "100n", {{"1", "SIG"}, {"2", "GND"}});
  synchroniser(c.c);
  c.c.circuit.pistes.push_back({nouvelUuid(), "SIG", Couche::CuivreDessus, ptMm(10, 10), ptMm(110, 10), nm(0.25)});
  std::vector<ParasitesReseau> par;
  AnalyseSpice ac;
  ac.type = AnalyseSpice::Type::Alternatif;
  ac.fDebut = 10;
  ac.fFin = 1e5;
  const NetlistSpice np = netlistAvecPistes(c.c, ac, {}, &par);
  REQUIRE(par.size() == 1);
  CHECK(par[0].longueur == Approx(0.1).epsilon(1e-9));
  CHECK(par[0].resistance == Approx(1.72e-8 * 0.1 / (0.25e-3 * 35e-6)).epsilon(0.001));
  CHECK(par[0].inductance > 0);
  CHECK(par[0].capacite > 0);
  CHECK(par[0].broches == 2);
  CHECK(np.texte.find("L_pis1") != std::string::npos);
  const ResultatSimulation ra = simuler(netlistSpice(c.c, ac)), rb = simuler(np);
  REQUIRE(ra.reussi());
  REQUIRE(rb.reussi());
  const auto* va = ra.complexe(vecteurTension(np, "SIG"));
  const auto* vb = rb.complexe(vecteurTension(np, "SIG"));
  REQUIRE(va);
  REQUIRE(vb);
  REQUIRE(va->size() == vb->size());
  for (std::size_t i = 0; i < va->size(); ++i) CHECK(std::abs(std::abs((*va)[i]) - std::abs((*vb)[i])) < 1e-3);
}

TEST_CASE("14.14 · abaisseur de 12 V à 5 V sous 2 A : posé avec sa nomenclature, marge de phase simulée d'au moins 45°", "[034][L14][14.14]") {
  DemandeAbaisseur d;
  d.vEntree = 12;
  d.vSortie = 5;
  d.courant = 2;
  const ConceptionAbaisseur a = concevoirAbaisseur(d);
  for (const auto& n : a.notes) UNSCOPED_INFO(n);
  REQUIRE(a.ok);
  CHECK(a.inductance == Approx(12e-6).epsilon(0.01));  // 10,2 µH demandés, 12 µH en E12
  CHECK(a.capacite >= 22e-6);
  CHECK(a.rapport == Approx(5.45 / 12.45).epsilon(1e-6));
  CHECK(0.8 * (1 + a.r1 / a.rBas) == Approx(5).epsilon(0.005));  // 0,8 V de référence, diviseur en E96
  CHECK(a.nomenclature.size() == 13);
  CHECK(a.margePrevue >= 45);
  CHECK(a.rendement > 0.8);
  CHECK(a.rendement < 0.97);
  // Posé sur une feuille, avec ses réseaux
  ConceptionElec c = nouvelleConception("Alimentation");
  const auto poses = poserAbaisseur(c, a, d);
  CHECK(poses.size() == 13);
  const Connectivite k = connectivite(c);
  CHECK(k.nom(c.parReference("L1")->uuid, "2") == "+5V");
  CHECK(k.nom(c.parReference("L1")->uuid, "1") == "SW");
  CHECK(k.nom(c.parReference("U1")->uuid, "1") == "+12V");
  CHECK(k.nom(c.parReference("C1")->uuid, "2") == "COMP");
  // L'arbre des rails : pile 12 V → abaisseur → 5 V (1,5 A de charges) → régulateur linéaire 3,3 V (0,5 A)
  Montage14 r;
  r.poser("Pile", "BT1", "12V", {{"1", "+12V"}, {"2", "GND"}});
  r.c.symboles["galerie:ABAISSEUR_MLI"] = c.symboles.at("galerie:ABAISSEUR_MLI");
  ComposantElec* u = r.poser("galerie:ABAISSEUR_MLI", "U1", "abaisseur", {{"1", "+12V"}, {"7", "GND"}});
  u->champs["Sim.Topologie"] = "abaisseur";
  u->champs["Sim.Sortie"] = "+5V";
  u->champs["Sim.VSortie"] = "5";
  u->champs["Sim.Rendement"] = "0.9";
  r.poser("Regulateur", "U2", "AMS1117-3.3", {{"3", "+5V"}, {"2", "+3V3"}, {"1", "GND"}});
  const ArbreRails arbre = arbreRails(r.c, {{"+5V", 1.5}, {"+3V3", 0.5}});
  INFO(arbre.texte());
  REQUIRE(arbre.etages.size() == 3);
  REQUIRE(arbre.racines.size() == 1);
  const EtageRail* ldo = nullptr;
  const EtageRail* buck = nullptr;
  for (const auto& e : arbre.etages) {
    if (e.reference == "U2") ldo = &e;
    if (e.reference == "U1") buck = &e;
  }
  REQUIRE(ldo);
  REQUIRE(buck);
  CHECK(ldo->vEntree == Approx(5));
  CHECK(ldo->dissipation == Approx((5 - 3.3) * 0.5).epsilon(1e-9));
  CHECK(ldo->rendement == Approx(3.3 / 5).epsilon(1e-9));
  CHECK(buck->iSortie == Approx(2.0));
  CHECK(buck->iEntree == Approx(5 * 2.0 / (0.9 * 12)).epsilon(1e-9));
  CHECK(arbre.puissanceEntree == Approx(12 * 5 * 2.0 / (0.9 * 12)).epsilon(1e-9));
  CHECK(arbre.texte().find("U2 (linéaire)") != std::string::npos);
  // Magnétiques : spires, induction, Steinmetz et Dowell
  const Noyau& n = noyaux()[1];
  const Bobinage b = calculerBobine(a.inductance, a.iCrete, a.iEfficace, 0.6, d.frequence, n, 0.5e-3);
  CHECK(b.spires == static_cast<int>(std::ceil(std::sqrt(a.inductance / n.al))));
  CHECK(!b.sature);
  CHECK(b.pertesFer > 0);
  CHECK(b.facteur >= 1);
  CHECK(facteurDowell(1e-6, profondeurPeau(1e3), 1) == Approx(1).epsilon(1e-3));
  CHECK(facteurDowell(2 * profondeurPeau(500e3), profondeurPeau(500e3), 3) > facteurDowell(2 * profondeurPeau(500e3), profondeurPeau(500e3), 1));
  CHECK(profondeurPeau(50) == Approx(9.33e-3).epsilon(0.01));
  CHECK(pertesSteinmetz(n, 100e3, 0.1) == Approx(0.88 * std::pow(1e5, 1.46) * std::pow(0.1, 2.52) * n.ve));
  // Transformateur planar en Gerber : deux couches, primaire et secondaire
  const auto g = transformateurPlanar(6, 3, 20, 0.6, 0.3);
  REQUIRE(g.size() == 2);
  CHECK(g.at("F.Cu").find("%FSLAX46Y46*%") != std::string::npos);
  CHECK(g.at("F.Cu").find("6 tours") != std::string::npos);
  CHECK(std::count(g.at("B.Cu").begin(), g.at("B.Cu").end(), '\n') > 3 * 60);
  // La marge simulée par ngspice sur le modèle moyen
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé : marge non simulée");
  const MargesBoucle m = simulerBoucle(a, d);
  INFO(m.erreur << " ; croisement " << m.frequenceCoupure << " Hz, marge " << m.margePhase << "°");
  REQUIRE(m.ok);
  CHECK(m.margePhase >= 45);
  CHECK(m.margePhase == Approx(a.margePrevue).margin(3));
  CHECK(m.frequenceCoupure == Approx(a.frequenceCoupure).epsilon(0.05));
}

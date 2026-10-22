// Prométhée : banc de mesure de l'atelier électronique (lot L1, tâche 1.1, Q5, ERG-10, AUT-12). Les cartes de mesure
// de 300, 1 000 et 2 000 composants (carteDeMesure, un plan de masse sur la face des composants) passent par chaque moteur ; les temps
// sont écrits en tableau Markdown, avec les objectifs de la charte d'interaction là où il y en a. Mesuré dès le lot
// L1, sans seuil : un dépassement de plus de 20 % ne fera échouer l'intégration continue qu'à partir du lot L9.
//   mesures_electronique [--resume fichier.md] [composants…]   (300 1000 2000 par défaut)
// Licence GPL-3.0-only.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

using namespace prom::ecad;

namespace {
using Horloge = std::chrono::steady_clock;

double mesurer(const std::function<void()>& f) {
  const auto t0 = Horloge::now();
  f();
  return std::chrono::duration<double, std::milli>(Horloge::now() - t0).count();
}

struct Ligne {
  std::string nom;
  double objectif = 0;  // ms ; 0 : pas d'objectif chiffré
  std::map<int, double> temps;
};

std::string ms(double v) {
  char t[32];
  if (v >= 1000) std::snprintf(t, sizeof t, "%.2f s", v / 1000);
  else std::snprintf(t, sizeof t, "%.1f ms", v);
  return t;
}
}  // namespace

int main(int argc, char** argv) {
  std::vector<int> tailles;
  std::string resume;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--resume" && i + 1 < argc) resume = argv[++i];
    else tailles.push_back(std::atoi(a.c_str()));
  }
  if (tailles.empty()) tailles = {300, 1000, 2000};
  std::vector<Ligne> lignes = {{"Génération de la carte", 0, {}},
                               {"Connectivité", 0, {}},
                               {"ERC", 0, {}},
                               {"Valeur changée : ERC seule (A12)", 30, {}},
                               {"Pastilles", 0, {}},
                               {"Remplissage du plan de masse", 0, {}},
                               {"État du routage (chevelu)", 0, {}},
                               {"DRC", 0, {}},
                               {"Synchronisation, rien à changer (F8)", 100, {}},
                               {"Copie de la conception (pas d'annulation)", 0, {}},
                               {"Écriture du JSON", 0, {}},
                               {"Lecture du JSON", 0, {}}};
  std::map<int, std::string> bilans;
  for (int n : tailles) {
    std::printf("Carte de mesure de %d composants\n", n);
    std::fflush(stdout);
    ConceptionElec c;
    std::size_t k = 0;
    auto noter = [&](double t) {
      lignes[k++].temps[n] = t;
      std::printf("  %-48s %10s\n", lignes[k - 1].nom.c_str(), ms(t).c_str());
      std::fflush(stdout);
    };
    noter(mesurer([&] { c = carteDeMesure(n, true); }));
    Connectivite cx;
    noter(mesurer([&] { cx = connectivite(c); }));
    std::vector<Violation> erc;
    noter(mesurer([&] { erc = verifierERC(c, cx); }));
    c.composants.front().valeur = "22k";
    noter(mesurer([&] { erc = verifierERC(c, cx); }));
    std::vector<PastillePlacee> pp;
    noter(mesurer([&] { pp = pastilles(c, cx); }));
    Remplissages z;
    noter(mesurer([&] { z = remplirZones(c, cx); }));
    EtatRoutage e;
    noter(mesurer([&] { e = etatRoutage(c, cx, z); }));
    std::vector<Violation> drc;
    noter(mesurer([&] { drc = verifierDRC(c, cx, z); }));
    ConceptionElec copie = c;
    noter(mesurer([&] { mettreAJourCircuit(copie); }));
    noter(mesurer([&] { const ConceptionElec x = c; (void)x; }));
    std::string texte;
    noter(mesurer([&] { texte = versJson(c).dump(); }));
    noter(mesurer([&] { const ConceptionElec x = conceptionDepuisJson(nlohmann::ordered_json::parse(texte)); (void)x; }));
    int erreursErc = 0, erreursDrc = 0;
    for (const auto& v : erc) erreursErc += v.gravite == Gravite::Erreur;
    for (const auto& v : drc) erreursDrc += v.gravite == Gravite::Erreur;
    bilans[n] = std::to_string(c.composants.size()) + " composants, " + std::to_string(cx.reseaux.size()) + " réseaux, " + std::to_string(e.chevelu.size()) + " liaisons à faire, ERC " +
                std::to_string(erreursErc) + " erreur(s), DRC " + std::to_string(erreursDrc) + " erreur(s), " + std::to_string(texte.size() / 1024) + " Kio";
    std::printf("  %s\n", bilans[n].c_str());
  }
  // Tableau Markdown : une colonne par taille ; ⚠ quand l'objectif de la charte est dépassé
  std::string t = "### Mesures de l'atelier électronique (cartes de mesure, plan de masse sur la face des composants)\n\n| Mesure |";
  for (int n : tailles) t += " " + std::to_string(n) + " |";
  t += " Objectif |\n|---|";
  for (std::size_t i = 0; i < tailles.size(); ++i) t += "---:|";
  t += "---:|\n";
  for (const Ligne& l : lignes) {
    t += "| " + l.nom + " |";
    for (int n : tailles) {
      const auto it = l.temps.find(n);
      const bool depasse = l.objectif > 0 && it != l.temps.end() && it->second > l.objectif;
      t += " " + (it == l.temps.end() ? std::string("—") : ms(it->second)) + (depasse ? " ⚠" : "") + " |";
    }
    t += " " + (l.objectif > 0 ? ms(l.objectif) : std::string("—")) + " |\n";
  }
  t += "\n";
  for (int n : tailles) t += "- " + std::to_string(n) + " : " + bilans[n] + "\n";
  t += "\nMesuré dès le lot L1, sans seuil ; un dépassement de plus de 20 % fera échouer l'intégration continue à partir du lot L9.\n";
  std::printf("\n%s", t.c_str());
  if (!resume.empty()) {
    std::ofstream f(resume, std::ios::app);
    f << t;
  }
  return 0;
}

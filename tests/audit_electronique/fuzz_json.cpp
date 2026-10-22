// Mutation aléatoire du fichier de l'exemple : chaque copie abîmée est relue puis passée dans tous les moteurs.
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

using namespace prom::ecad;
using J = nlohmann::ordered_json;

static std::mt19937_64 alea(12345);

static void muter(J& j, int profondeur = 0) {
  if (j.is_object() || j.is_array()) {
    if (j.empty()) return;
    std::uniform_int_distribution<std::size_t> d(0, j.size() - 1);
    auto it = j.begin();
    std::advance(it, static_cast<long>(d(alea)));
    if (profondeur < 6 && (it->is_object() || it->is_array()) && alea() % 4 != 0) {
      muter(*it, profondeur + 1);
      return;
    }
    switch (alea() % 9) {
      case 0: *it = 1e300; break;
      case 1: *it = -1e300; break;
      case 2: *it = "texte"; break;
      case 3: *it = J::array(); break;
      case 4: *it = J::object(); break;
      case 5: *it = nullptr; break;
      case 6: *it = -1; break;
      case 7: *it = 0; break;
      default:
        if (j.is_object()) j.erase(it);
        else j.erase(it);
        break;
    }
  }
}

int main(int argc, char** argv) {
  const int n = argc > 1 ? std::atoi(argv[1]) : 200;
  const J base = versJson(exempleElectronique());
  int exceptions = 0;
  for (int i = 0; i < n; ++i) {
    J j = base;
    const int m = 1 + static_cast<int>(alea() % 4);
    for (int k = 0; k < m; ++k) muter(j);
    try {
      ConceptionElec c = conceptionDepuisJson(j);
      const Connectivite k = connectivite(c);
      const auto erc = verifierERC(c, k);
      const Remplissages z = remplirZones(c, k);
      const auto e = etatRoutage(c, k, z);
      const auto drc = verifierDRC(c, k, z);
      const ModeleFabrication mf = modeleFabrication(c, k, z);
      for (const auto& cf : mf.couches) (void)gerber(mf, cf.couche, "f");
      (void)excellon(mf, true, "f");
      (void)csvPlacement(c);
      (void)csvNomenclature(c);
      (void)valider(c);
    } catch (const std::exception& ex) {
      ++exceptions;
      if (exceptions <= 5) std::printf("exception %d : %s\n", i, ex.what());
    }
  }
  std::printf("%d fichiers abîmés relus et traités, %d exception(s)\n", n, exceptions);
}

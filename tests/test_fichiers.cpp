// Prométhée : essais de robustesse des fichiers (matrice QA-02, audit T2). Licence GPL-3.0-only.
// Écriture atomique et interruption, fichiers vides, tronqués ou abîmés, format plus récent jamais écrasé,
// copie avant migration, chemins accentués et non latins, champs inconnus conservés, aller-retour exact.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "promethee/fichiers.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/hachage.hpp"
#include "promethee/modele.hpp"

using namespace prom;
namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;

namespace {
// Dossier d'essai neuf, effacé à la fin.
struct DossierEssai {
  fs::path chemin;
  explicit DossierEssai(const std::string& nom) : chemin(fs::temp_directory_path() / cheminFichier(nom)) {
    fs::remove_all(chemin);
    fs::create_directories(chemin);
  }
  ~DossierEssai() {
    std::error_code ec;
    fs::remove_all(chemin, ec);
  }
  std::string operator/(const std::string& f) const { return utf8(chemin / cheminFichier(f)); }
};
std::string lire(const std::string& chemin) { return lireFichier(cheminFichier(chemin)); }
void ecrireBrut(const std::string& chemin, const std::string& texte) {
  std::ofstream f(cheminFichier(chemin), std::ios::binary);
  f << texte;
}
std::size_t fichiersDans(const fs::path& d) {
  std::size_t n = 0;
  for (const auto& e : fs::directory_iterator(d)) n += e.is_regular_file() ? 1 : 0;
  return n;
}
}  // namespace

TEST_CASE("SHA-256 : vecteurs de référence FIPS 180-4", "[fichiers][QA-02]") {
  CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  Sha256 h;
  const std::string mille(1000, 'a');
  for (int i = 0; i < 1000; ++i) h.ajouter(mille);
  CHECK(h.terminerHex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("Écriture atomique : une interruption laisse l'ancien fichier intact", "[fichiers][QA-02]") {
  const DossierEssai d("promethee_atomique");
  const std::string f = d / "projet.prom";
  ecrireFichierAtomique(cheminFichier(f), "version 1");
  CHECK(lire(f) == "version 1");
  // Plantage simulé entre l'écriture du fichier temporaire et son renommage
  essais::g_avantRenommage = [](const fs::path&) { throw std::runtime_error("coupure simulée"); };
  CHECK_THROWS_WITH(ecrireFichierAtomique(cheminFichier(f), "version 2, jamais terminée"), "coupure simulée");
  essais::g_avantRenommage = nullptr;
  CHECK(lire(f) == "version 1");
  CHECK(fichiersDans(d.chemin) == 1);  // aucun fichier temporaire oublié
  ecrireFichierAtomique(cheminFichier(f), "version 2");
  CHECK(lire(f) == "version 2");
  CHECK(fichiersDans(d.chemin) == 1);
  // Dossier absent : erreur claire, rien d'écrit
  CHECK_THROWS_WITH(ecrireFichierAtomique(cheminFichier(d / "absent/x.prom"), "x"), ContainsSubstring("dossier introuvable"));
  CHECK_THROWS_AS(ecrireFichierAtomique(cheminFichier(d / "absent/x.prom"), "x"), ErreurFichier);
}

TEST_CASE("Projet vide, tronqué ou abîmé : message précis, jamais de projet faux", "[fichiers][QA-02]") {
  const DossierEssai d("promethee_corrompus");
  const std::string bon = d / "bon.prom";
  ecrireProjet(exemple(), bon);
  const std::string texte = lire(bon);
  REQUIRE(texte.size() > 400);

  const std::string vide = d / "vide.prom";
  ecrireBrut(vide, "");
  CHECK_THROWS_WITH(lireProjet(vide), ContainsSubstring("est vide"));
  ecrireBrut(vide, "  \n\t ");
  CHECK_THROWS_WITH(lireProjet(vide), ContainsSubstring("est vide"));

  // Coupé à divers endroits, comme par un enregistrement interrompu
  for (const double fraction : {0.1, 0.5, 0.9, 0.999}) {
    const std::string coupe = d / "coupe.prom";
    ecrireBrut(coupe, texte.substr(0, static_cast<std::size_t>(static_cast<double>(texte.size()) * fraction)));
    CHECK_THROWS_WITH(lireProjet(coupe), ContainsSubstring("tronqué"));
  }
  const std::string abime = d / "abime.prom";
  std::string a = texte;
  a[a.size() / 2] = '\x01';
  ecrireBrut(abime, a);
  CHECK_THROWS_WITH(lireProjet(abime), ContainsSubstring("n’est pas un fichier JSON valide"));
  ecrireBrut(abime, "[1, 2, 3]");
  CHECK_THROWS_WITH(lireProjet(abime), ContainsSubstring("n’est pas un projet"));
  CHECK_THROWS_WITH(lireProjet(d / "absent.prom"), ContainsSubstring("introuvable"));
  // Le bon fichier se lit toujours
  CHECK_NOTHROW(lireProjet(bon));
}

TEST_CASE("Un projet d'un format plus récent n'est ni ouvert ni écrasé", "[fichiers][QA-02]") {
  const DossierEssai d("promethee_format_futur");
  const std::string f = d / "futur.prom";
  Json j = versJson(exemple());
  j["version"] = VERSION_FORMAT + 1;
  j["nouveauté"] = {{"a", 1}};
  const std::string futur = j.dump(2);
  ecrireBrut(f, futur);
  CHECK_THROWS_WITH(lireProjet(f), ContainsSubstring("plus récente"));
  CHECK(formatProjet(futur) == VERSION_FORMAT + 1);
  CHECK_THROWS_WITH(ecrireProjet(exemple(), f), ContainsSubstring("Enregistrement refusé"));
  CHECK(lire(f) == futur);
}

TEST_CASE("Copie de secours avant de réécrire un ancien format", "[fichiers][QA-02]") {
  const DossierEssai d("promethee_migration");
  const std::string f = d / "ancien.prom.json";
  // Format 1 : projet du prototype web, sans numéro de format
  const std::string web = R"({"nom": "Ancien", "carte": {"L": 50, "W": 30}, "composants": [], "trous": []})";
  ecrireBrut(f, web);
  RapportLecture r;
  const Projet p = lireProjet(f, &r);
  CHECK(r.versionFormat == 1);
  CHECK_FALSE(r.avertissements.empty());  // la migration est annoncée
  ecrireProjet(p, f);
  CHECK(lire(f + ".format1.bak") == web);
  CHECK(formatProjet(lire(f)) == VERSION_FORMAT);
  // Enregistrements suivants : la copie de l'original n'est pas remplacée
  ecrireProjet(p, f);
  CHECK(lire(f + ".format1.bak") == web);
}

TEST_CASE("Chemins avec accents, espaces et caractères non latins", "[fichiers][QA-02]") {
  const DossierEssai d("promethee Été 2026 — carte d’essai 東京");
  fs::create_directories(cheminFichier(d / "sous dossier ü"));
  const std::string f = d / "sous dossier ü/projet été №1.prom";
  const Projet p = exemple();
  ecrireProjet(p, f);
  CHECK(fs::exists(cheminFichier(f)));
  const Projet q = lireProjet(f);
  CHECK(versJson(q) == versJson(p));
  const EtatDisque e = etatDisque(cheminFichier(f));
  CHECK(e.existe);
  CHECK(e.sha256.size() == 64);
  // Export géométrique dans le même dossier (OpenCascade reçoit le chemin en UTF-8)
  const std::string step = d / "sous dossier ü/boîtier été.step";
  exporterStep({{"Boîtier", construireCorps(p, deriver(p)), 0.8, 0.8, 0.8}}, step);
  CHECK(fs::file_size(cheminFichier(step)) > 1000);
}

TEST_CASE("Enregistrer, relire, réenregistrer : octets identiques ; champs inconnus conservés", "[fichiers][QA-02]") {
  const DossierEssai d("promethee_aller_retour");
  const std::string f1 = d / "a.prom", f2 = d / "b.prom";
  Projet p = exemple();
  p.inconnus["extension_future"] = {{"cle", "valeur"}, {"n", 3}};
  ecrireProjet(p, f1);
  RapportLecture r;
  const Projet q = lireProjet(f1, &r);
  REQUIRE(r.avertissements.size() == 1);
  CHECK_THAT(r.avertissements[0], ContainsSubstring("extension_future"));
  ecrireProjet(q, f2);
  CHECK(lire(f1) == lire(f2));
  // Un composant d'un type inconnu est signalé, pas effacé en silence
  Json j = versJson(exemple());
  j["composants"].push_back({{"id", "x1"}, {"ref", "Z9"}, {"type", "teleporteur"}, {"x", 0}, {"y", 0}});
  ecrireBrut(f1, j.dump());
  RapportLecture r2;
  lireProjet(f1, &r2);
  bool signale = false;
  for (const auto& a : r2.avertissements) signale = signale || a.find("teleporteur") != std::string::npos;
  CHECK(signale);
}

TEST_CASE("L'état sur disque reconnaît une modification faite ailleurs", "[fichiers][QA-02]") {
  const DossierEssai d("promethee_etat_disque");
  const std::string f = d / "p.prom";
  ecrireProjet(exemple(), f);
  const EtatDisque avant = etatDisque(cheminFichier(f));
  CHECK(avant == etatDisque(cheminFichier(f)));
  Projet autre = exemple();
  autre.nom = "Modifié par une autre fenêtre";
  ecrireProjet(autre, f);
  CHECK(avant != etatDisque(cheminFichier(f)));
  CHECK_FALSE(etatDisque(cheminFichier(d / "absent.prom")).existe);
}

TEST_CASE("EL-46 noms de fichiers translittérés", "[fichiers][EL-46]") {
  CHECK(prom::nomFichierAscii("récepteur", "carte") == "recepteur");
  CHECK(prom::nomFichierAscii("Bœuf & Ø6", "carte") == "Boeuf_D6");
  CHECK(prom::nomFichierAscii("«  »", "carte") == "carte");
  CHECK(prom::translitterer("Équerre « L » – 2 µm") == "Equerre \" L \" - 2 um");
}

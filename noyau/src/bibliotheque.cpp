// Prométhée : bibliothèque personnelle de symboles électroniques. Licence GPL-3.0-only.
#include "promethee/bibliotheque.hpp"

#include <system_error>

#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"

namespace prom::ecad {

namespace {
using J = nlohmann::ordered_json;

// Fin de l'objet JSON qui commence en « debut » (une accolade), chaînes et échappements compris ; npos s'il est coupé
std::size_t finObjet(const std::string& t, std::size_t debut) {
  int profondeur = 0;
  bool chaine = false, echappe = false;
  for (std::size_t i = debut; i < t.size(); ++i) {
    const char ch = t[i];
    if (chaine) {
      if (echappe) echappe = false;
      else if (ch == '\\') echappe = true;
      else if (ch == '"') chaine = false;
      continue;
    }
    if (ch == '"') chaine = true;
    else if (ch == '{') ++profondeur;
    else if (ch == '}' && --profondeur == 0) return i;
  }
  return std::string::npos;
}

void ajouter(LectureBibliotheque& r, const J& x) {
  if (!x.is_object()) return;
  DefSymbole d = symboleDepuisJson(x);
  if (!d.id.empty()) r.symboles.push_back(std::move(d));
}
void ajouterEmpreinte(LectureBibliotheque& r, const J& x) {
  if (!x.is_object()) return;
  DefEmpreinte d = empreinteDepuisJson(x);
  if (!d.id.empty() && !d.pastilles.empty()) r.empreintes.push_back(std::move(d));
}
}  // namespace

LectureBibliotheque lireBibliotheque(const std::string& contenu) {
  LectureBibliotheque r;
  try {
    const J j = J::parse(contenu);
    if (!j.is_object() || !j.contains("symboles") || !j["symboles"].is_array()) {
      r.abimee = true;
      r.erreur = "ce n’est pas une bibliothèque Prométhée";
      return r;
    }
    for (const auto& x : j["symboles"]) ajouter(r, x);
    if (j.contains("empreintes") && j["empreintes"].is_array())
      for (const auto& x : j["empreintes"]) ajouterEmpreinte(r, x);
    return r;
  } catch (const std::exception& e) {
    r.abimee = true;
    r.erreur = e.what();
  }
  // Récupération : les objets entiers des listes « symboles » et « empreintes », jusqu'à la coupure
  auto recuperer = [&](const char* cleListe, auto&& ajout) {
    const std::size_t cle = contenu.find(std::string("\"") + cleListe + "\"");
    std::size_t i = cle == std::string::npos ? std::string::npos : contenu.find('[', cle);
    if (i == std::string::npos) return;
    ++i;
    while (i < contenu.size()) {
      while (i < contenu.size() && (std::isspace(static_cast<unsigned char>(contenu[i])) || contenu[i] == ',')) ++i;
      if (i >= contenu.size() || contenu[i] != '{') break;
      const std::size_t fin = finObjet(contenu, i);
      if (fin == std::string::npos) break;
      try {
        ajout(r, J::parse(contenu.substr(i, fin - i + 1)));
      } catch (const std::exception&) {
        // objet entier mais illisible : sauté
      }
      i = fin + 1;
    }
  };
  recuperer("symboles", ajouter);
  recuperer("empreintes", ajouterEmpreinte);
  return r;
}

std::string texteBibliotheque(const std::vector<DefSymbole>& symboles, const std::vector<DefEmpreinte>& empreintes) {
  J j = {{"format", "promethee-bibliotheque-electronique"}, {"version", 1}, {"symboles", J::array()}};
  for (const auto& x : symboles) j["symboles"].push_back(symboleVersJson(x));
  if (!empreintes.empty()) {
    j["empreintes"] = J::array();
    for (const auto& x : empreintes) j["empreintes"].push_back(empreinteVersJson(x));
  }
  return j.dump(1) + "\n";
}

LectureBibliotheque lireFichierBibliotheque(const std::filesystem::path& chemin) {
  std::error_code ec;
  if (!std::filesystem::exists(chemin, ec)) return {};
  return lireBibliotheque(lireFichier(chemin));
}

std::filesystem::path mettreDeCoteBibliotheque(const std::filesystem::path& chemin) {
  std::filesystem::path cible;
  for (int n = 1;; ++n) {
    cible = chemin;
    cible.replace_filename(chemin.stem().u8string() + u8".abime-" + std::filesystem::path(std::to_string(n)).u8string() + chemin.extension().u8string());
    std::error_code ec;
    if (!std::filesystem::exists(cible, ec)) break;
  }
  std::error_code ec;
  std::filesystem::rename(chemin, cible, ec);
  if (ec) {
    std::filesystem::copy_file(chemin, cible, ec);
    if (ec) throw ErreurFichier("Impossible de mettre de côté la bibliothèque abîmée « " + utf8(chemin.filename()) + " » : " + ec.message() + ".");
  }
  return cible;
}

void ecrireFichierBibliotheque(const std::filesystem::path& chemin, const std::vector<DefSymbole>& symboles, const std::vector<DefEmpreinte>& empreintes) {
  std::error_code ec;
  if (chemin.has_parent_path()) std::filesystem::create_directories(chemin.parent_path(), ec);
  if (std::filesystem::exists(chemin, ec)) {
    // La version précédente, si elle se lit, est gardée ; un fichier abîmé n'est jamais écrasé (voir mettreDeCote)
    const LectureBibliotheque avant = lireFichierBibliotheque(chemin);
    if (avant.abimee) throw ErreurFichier("La bibliothèque « " + utf8(chemin.filename()) + " » est abîmée : mets-la de côté avant de l’écrire.");
    std::filesystem::path bak = chemin;
    bak += ".bak";
    ecrireFichierAtomique(bak, lireFichier(chemin));
  }
  ecrireFichierAtomique(chemin, texteBibliotheque(symboles, empreintes));
}

}  // namespace prom::ecad

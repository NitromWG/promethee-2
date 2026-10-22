// Prométhée : lecture et écriture sûres des fichiers. Licence GPL-3.0-only.
// Tous les chemins passent par cheminFichier() : une chaîne UTF-8 (accents, espaces, caractères non latins)
// devient un chemin natif sans dépendre de la page de code de Windows. Les enregistrements sont atomiques :
// le contenu va dans un fichier temporaire du même dossier, vidé sur le disque, puis renommé par-dessus la
// cible ; un plantage, une coupure de courant ou un disque plein en cours d'écriture laisse l'ancien fichier
// intact, jamais un fichier à moitié écrit.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace prom {

// Erreur de fichier, avec un message en français prêt à afficher.
class ErreurFichier : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

std::filesystem::path cheminFichier(std::string_view utf8);
// Texte UTF-8 ramené à l'ASCII : « é » devient « e », « œ » « oe », « Ø » « D », guillemets et tirets droits
std::string translitterer(const std::string& utf8);
// Fichier ISO 10303-21 (STEP) dont les chaînes contiennent de l'UTF-8 brut : chaque caractère non ASCII y est codé
// selon la norme (\X2\hhhh\X0\ dans le plan multilingue de base, \X4\hhhhhhhh\X0\ au-delà). Les coupures de
// ligne qu'un écrivain aurait glissées au milieu d'un caractère disparaissent. Le texte rendu est en ASCII pur.
std::string texteStepAscii(const std::string& contenu);
// Nom de fichier sûr partout (fabricants, archives, anciens systèmes) : translittéré, lettres, chiffres, « - » et « _ »
std::string nomFichierAscii(const std::string& utf8, const std::string& defaut);
std::string utf8(const std::filesystem::path& p);

// Contenu complet d'un fichier (octets bruts). Lève ErreurFichier.
std::string lireFichier(const std::filesystem::path& p);

// Écriture atomique (voir en tête). Lève ErreurFichier avec la cause (droits, disque plein…) ; en cas
// d'échec, la cible n'est pas touchée et aucun fichier temporaire ne reste.
void ecrireFichierAtomique(const std::filesystem::path& p, std::string_view contenu);

// État d'un fichier sur le disque, pour reconnaître une modification faite ailleurs (autre fenêtre, autre
// programme, synchronisation) entre l'ouverture et l'enregistrement.
struct EtatDisque {
  bool existe = false;
  std::uintmax_t taille = 0;
  std::string sha256;  // contenu
  bool operator==(const EtatDisque& o) const { return existe == o.existe && taille == o.taille && sha256 == o.sha256; }
  bool operator!=(const EtatDisque& o) const { return !(*this == o); }
};
EtatDisque etatDisque(const std::filesystem::path& p);

namespace essais {
// Point d'injection des essais : appelé entre l'écriture du fichier temporaire et son renommage. Un essai y
// lève une exception pour simuler une interruption et vérifier que l'ancien fichier reste intact.
extern std::function<void(const std::filesystem::path& temporaire)> g_avantRenommage;
}  // namespace essais

}  // namespace prom

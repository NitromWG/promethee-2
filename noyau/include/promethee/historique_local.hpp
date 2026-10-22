// Prométhée : historique local du projet (lot L12, 12.9 ; PRJ-15, PRJ-18, PRJ-19). Chaque enregistrement (ou chaque
// intervalle réglé) dépose un instantané du fichier. Le texte est découpé en morceaux définis par leur contenu (une
// empreinte roulante coupe aux mêmes endroits d'une version à l'autre) ; chaque morceau est rangé une seule fois sous
// son empreinte SHA-256, compressé : ce qui n'a pas changé ne coûte rien. Le plafond retire les instantanés les plus
// anciens, sauf ceux qui portent un nom. La restitution redonne le texte exact, octet pour octet.
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace prom {

class HistoriqueLocal {
 public:
  struct Instantane {
    std::string id;     // date et heure, triées : « 20261022-153012-001 »
    std::string date;   // ISO 8601
    std::string nom;    // donné par l'utilisateur ; un instantané nommé n'est jamais retiré par le plafond
    std::uint64_t taille = 0;  // du texte restitué
    std::string empreinte;     // SHA-256 du texte
  };
  // « dossier » : celui de l'historique (créé au besoin) ; « plafond » : octets occupés sur le disque
  explicit HistoriqueLocal(std::string dossier, std::uint64_t plafond = std::uint64_t{50} << 20);
  // Dépose un instantané ; rend son identifiant, ou celui du dernier si le texte n'a pas changé
  std::string enregistrer(const std::string& texte, const std::string& nom = {});
  std::vector<Instantane> chronologie() const;  // du plus ancien au plus récent
  std::string restaurer(const std::string& id) const;  // lève std::runtime_error si l'instantané est perdu ou abîmé
  bool nommer(const std::string& id, const std::string& nom);
  std::uint64_t occupation() const;  // octets sur le disque (morceaux et listes)
  int morceaux() const;              // nombre de morceaux rangés
  const std::string& dossier() const { return m_dossier; }

  // Découpage défini par le contenu (exposé pour les essais) : bornes moyennes de 4 Kio, entre 1 et 16 Kio
  static std::vector<std::pair<std::size_t, std::size_t>> decouper(const std::string& texte);

 private:
  void appliquerPlafond();
  std::string m_dossier;
  std::uint64_t m_plafond;
};

}  // namespace prom

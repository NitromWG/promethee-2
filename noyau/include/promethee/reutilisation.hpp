// Prométhée : réutilisation (lot L7, tâche 7.16 ; STR-23, STR-24, STR-06, PLA-32). Extraits du schéma ou du circuit
// en fichiers, avec révision et cycle de vie ; feuille posée liée à son fichier, retouches locales signalées et cas
// d'emploi ; disposition d'un canal recopiée sur les canaux semblables d'une conception à plat, composants appariés
// par raffinement de couleurs (Weisfeiler-Lehman) sur le graphe composants-réseaux.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct Extrait {
  std::string nom, genre = "schema";  // « schema » ou « circuit »
  std::string description, etat = "brouillon";  // cycle de vie : brouillon, validé, obsolète
  int revision = 1;
  nlohmann::ordered_json contenu;  // presse-papiers du schéma ou du circuit
};
Extrait extraitSchema(const ConceptionElec& c, std::size_t feuille, const std::set<std::string>& selection, const std::string& nom);
Extrait extraitCircuit(const ConceptionElec& c, const std::set<std::string>& selection, const std::string& nom);
std::string texteExtrait(const Extrait& e);
std::optional<Extrait> lireExtrait(const std::string& texte, std::string* erreur = nullptr);
// Posé à « ou » : sur la feuille (schéma) ou sur la carte (circuit) ; rend les UUID créés
std::set<std::string> poserExtrait(ConceptionElec& c, const Extrait& e, std::size_t feuille, Pt ou);

// Feuille liée à son fichier (une conception d'une feuille, enregistrée à part) : écarts entre la feuille posée et
// la version du fichier (retouches locales), et projets qui l'emploient
std::vector<std::string> retouchesLocales(const Feuille& posee, const Feuille& fichier);
struct CasEmploi {
  std::string conception, feuille, revision;
};
std::vector<CasEmploi> casEmploi(const std::vector<ConceptionElec>& conceptions, const std::string& fichier);

// Canaux semblables : pour la source (composants d'un canal), chaque autre canal et sa correspondance source → cible
std::vector<std::map<std::string, std::string>> apparierCanaux(const ConceptionElec& c, const std::vector<std::string>& source);
// Disposition de la source recopiée sur la cible : même place relative à l'ancre (premier composant) décalée de
// « decalage » par canal, même angle, même face ; pistes des réseaux internes recopiées sur les réseaux appariés ;
// rend le nombre de composants placés
int recopierDisposition(ConceptionElec& c, const std::vector<std::string>& source, const std::vector<std::map<std::string, std::string>>& canaux, Pt decalage, bool pistes = true);

}  // namespace prom::ecad

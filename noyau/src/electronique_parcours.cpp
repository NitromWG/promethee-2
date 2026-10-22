// Prométhée : parcours « Apprendre » de l'atelier électronique (lot L2, tâche 2.17). Licence GPL-3.0-only.
#include "promethee/electronique_parcours.hpp"

#include <algorithm>
#include <set>

namespace prom::ecad {
namespace {

// Les composants posés avec ce symbole de la bibliothèque
std::vector<const ComposantElec*> poses(const ConceptionElec& c, const std::string& symbole) {
  std::set<std::string> ids;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (!s.composant.empty() && s.symbole == symbole) ids.insert(s.composant);
  std::vector<const ComposantElec*> r;
  for (const auto& k : c.composants)
    if (ids.count(k.uuid)) r.push_back(&k);
  return r;
}

// Un symbole d'alimentation posé : une masse (son nom contient GND) ou une tension
bool alimentationPosee(const ConceptionElec& c, bool masse) {
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (const DefSymbole* d = c.symbole(s.symbole); d && d->alimentation) {
        const std::string nom = s.nomAlim.empty() ? d->nom : s.nomAlim;
        if ((nom.find("GND") != std::string::npos) == masse) return true;
      }
  return false;
}

bool sansErreur(const std::vector<Violation>& v) {
  return std::none_of(v.begin(), v.end(), [](const Violation& x) { return x.gravite == Gravite::Erreur; });
}

// La LED et sa résistance : chaque broche reliée à une autre ou à une alimentation, et l'un de leurs réseaux alimenté
bool relies(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  bool alimente = false;
  std::size_t n = 0;
  for (const char* symbole : {"LED", "R"})
    for (const ComposantElec* x : poses(c, symbole)) {
      const DefSymbole* d = c.symbole(x->symbole);
      if (!d) return false;
      for (const auto& b : d->broches) {
        const int r = k.reseau(x->uuid, b.numero);
        if (r < 0) return false;
        const Reseau& res = k.reseaux[static_cast<std::size_t>(r)];
        // Relié à une autre broche, ou à une alimentation (un symbole d'alimentation n'est pas une broche de composant)
        if (res.broches.size() < 2 && res.alimentations.empty()) return false;
        alimente |= !res.alimentations.empty();
      }
      ++n;
    }
  return n >= 2 && alimente;
}

// Chaque composant qui va sur la carte y a son empreinte
bool circuitCree(const ConceptionElec& c) {
  if (c.schemaSeul) return false;
  std::set<std::string> places;
  for (const auto& e : c.circuit.empreintes) places.insert(e.composant);
  std::size_t attendus = 0;
  for (const auto& k : c.composants) {
    const DefSymbole* d = c.symbole(k.symbole);
    if (!d || d->alimentation || d->drapeau || k.empreinte.empty() || k.horsCarte) continue;
    ++attendus;
    if (!places.count(k.uuid)) return false;
  }
  return attendus > 0;
}

std::vector<ParcoursApprendre> construire() {
  std::vector<ParcoursApprendre> v;
  ParcoursApprendre p;
  p.id = "premiere-carte";
  p.nom = "Ta première carte";
  p.description = "Une LED et sa résistance, du schéma à la carte publiée pour la fabrication.";
  p.etapes.push_back({"Pose une LED",
                      "Une LED s’allume quand le courant la traverse dans le bon sens. Au schéma, elle est un symbole : un dessin et deux broches numérotées.",
                      "Dans la bibliothèque à gauche, cherche « LED », puis glisse-la sur la feuille (ou clique-la, puis clique sur la feuille).", "symbole", 0,
                      [](const ConceptionElec& c) { return !poses(c, "LED").empty(); }});
  p.etapes.push_back({"Ajoute sa résistance",
                      "Sans résistance, la LED laisserait passer trop de courant et brûlerait. La résistance le limite.",
                      "Cherche « Résistance » dans la bibliothèque et pose-la à côté de la LED. R la tourne pendant la pose.", "symbole", 0,
                      [](const ConceptionElec& c) { return !poses(c, "R").empty(); }});
  p.etapes.push_back({"Ajoute l’alimentation et la masse",
                      "Un symbole d’alimentation nomme un réseau : tout ce qui touche « +5V » est relié, même sans fil tracé entre eux.",
                      "Au ruban, groupe Alimentations : pose « +5V » et « GND » (P pose une alimentation au nom libre).", "réseau", 0,
                      [](const ConceptionElec& c) { return alimentationPosee(c, false) && alimentationPosee(c, true); }});
  p.etapes.push_back({"Relie-les par des fils",
                      "+5V, la résistance, la LED, puis la masse : le courant doit pouvoir faire le tour.",
                      "Touche W ou bouton Fil : clique sur le bout d’une broche, puis sur l’autre. Le fil contourne les symboles.", "réseau", 0, relies});
  p.etapes.push_back({"Vérifie le schéma",
                      "La vérification électrique (ERC) cherche les broches oubliées, les sorties en conflit, les repères en double.",
                      "F7, ou « Vérifier » au ruban. Le panneau Vérifications liste ce qui reste ; un clic mène au problème.", "ERC", 0,
                      [](const ConceptionElec& c) { return relies(c) && sansErreur(verifierERC(c, connectivite(c), NiveauErc::Complet)); }});
  p.etapes.push_back({"Crée le circuit",
                      "Le circuit imprimé reçoit une empreinte par composant : les pastilles de cuivre où il sera soudé.",
                      "Onglet Circuit, puis « Mettre à jour le circuit » (F8) : les empreintes arrivent, reliées par le chevelu.", "empreinte", 1, circuitCree});
  p.etapes.push_back({"Place les composants sur la carte",
                      "Chaque empreinte doit tenir dans le contour de la carte, avec la place de la soudure autour.",
                      "Glisse chaque empreinte à l’intérieur du contour ; R la tourne.", "pastille", 1,
                      [](const ConceptionElec& c) {
                        if (!circuitCree(c)) return false;
                        return std::all_of(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [&](const EmpreintePlacee& e) { return c.circuit.contour.contient(e.position); });
                      }});
  p.etapes.push_back({"Route les pistes",
                      "Le chevelu montre ce qui reste à relier ; une piste de cuivre remplace chaque ligne.",
                      "Touche X ou bouton Piste : clique sur une pastille, puis sur l’autre bout de son chevelu.", "chevelu", 1,
                      [](const ConceptionElec& c) {
                        if (!circuitCree(c)) return false;
                        const Connectivite k = connectivite(c);
                        const EtatRoutage e = etatRoutage(c, k, remplirZones(c, k));
                        return e.reseauxTotal > 0 && e.chevelu.empty();
                      }});
  p.etapes.push_back({"Vérifie la carte",
                      "La vérification des règles de dessin (DRC) contrôle les écarts entre cuivres, les largeurs et les bords.",
                      "F7 au circuit : le panneau Vérifications liste les écarts ; un clic mène à la piste ou à la pastille.", "DRC", 1,
                      [](const ConceptionElec& c) {
                        if (!circuitCree(c)) return false;
                        const Connectivite k = connectivite(c);
                        const Remplissages z = remplirZones(c, k);
                        return etatRoutage(c, k, z).chevelu.empty() && sansErreur(verifierDRC(c, k, z));
                      }});
  p.etapes.push_back({"Publie pour la fabrication",
                      "La publication rassemble les fichiers du fabricant et verrouille cette révision : ce qui est fabriqué reste connu.",
                      "Onglet Fabrication, « Publier pour la fabrication… ».", "Gerber", 3, [](const ConceptionElec& c) { return !c.publications.empty(); }});
  v.push_back(std::move(p));
  return v;
}

}  // namespace

const std::vector<ParcoursApprendre>& parcoursElectroniques() {
  static const std::vector<ParcoursApprendre> v = construire();
  return v;
}

const ParcoursApprendre* parcoursElectronique(const std::string& id) {
  for (const auto& p : parcoursElectroniques())
    if (p.id == id) return &p;
  return nullptr;
}

std::size_t etapeEnCours(const ParcoursApprendre& p, const ConceptionElec& c) {
  for (std::size_t i = 0; i < p.etapes.size(); ++i)
    if (!p.etapes[i].faite || !p.etapes[i].faite(c)) return i;
  return p.etapes.size();
}

}  // namespace prom::ecad

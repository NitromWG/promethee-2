// Prométhée : commandes de l'outil en ligne de commande pour la conception électronique (lot L13 ; 13.1, 13.6 ;
// AUT-01, AUT-02, F15). Vérifications (erc, drc, revue, valider, exigences) avec rapport JSON et code de retour :
// 0 conforme, 1 défauts trouvés, 2 erreur d'exécution, 64 mauvaise utilisation. Jeux de sorties avec variables
// -D CLÉ=VALEUR, comparaison et fusion, archive, placement automatique, interface JSON-RPC sur l'entrée standard,
// gravure à l'anglaise, plan de mise sous tension. Licence GPL-3.0-only.
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/api_commandes.hpp"
#include "promethee/collaboration.hpp"
#include "promethee/comparaison.hpp"
#include "promethee/electronique.hpp"
#include "promethee/equipe.hpp"
#include "promethee/essais_production.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/placement_auto.hpp"
#include "promethee/revue_conception.hpp"
#include "promethee/sorties.hpp"
#include "promethee/usinage.hpp"
#include "promethee/variantes.hpp"
#include "promethee/version.hpp"

namespace fs = std::filesystem;
using namespace prom;
using J = nlohmann::ordered_json;

namespace {
struct Options {
  bool json = false;
  std::size_t conception = 1;
  std::map<std::string, std::string> variables;
  std::vector<std::string> positionnels;
};
Options lireOptions(const std::vector<std::string>& a) {
  Options o;
  for (std::size_t i = 1; i < a.size(); ++i) {
    if (a[i] == "--json") o.json = true;
    else if (a[i] == "-n" && i + 1 < a.size()) o.conception = static_cast<std::size_t>(std::stoul(a[++i]));
    else if (a[i] == "-D" && i + 1 < a.size()) {
      const std::string v = a[++i];
      const auto egal = v.find('=');
      o.variables[v.substr(0, egal)] = egal == std::string::npos ? std::string() : v.substr(egal + 1);
    } else if (a[i].rfind("-D", 0) == 0 && a[i].size() > 2) {
      const std::string v = a[i].substr(2);
      const auto egal = v.find('=');
      o.variables[v.substr(0, egal)] = egal == std::string::npos ? std::string() : v.substr(egal + 1);
    } else
      o.positionnels.push_back(a[i]);
  }
  return o;
}
const ecad::ConceptionElec& conceptionDe(const Projet& p, std::size_t n) {
  if (n == 0 || n > p.electronique.size())
    throw std::invalid_argument("le projet contient " + std::to_string(p.electronique.size()) + " conception(s) électronique(s) : pas de numéro " + std::to_string(n));
  return p.electronique[n - 1];
}
J violationsJson(const std::vector<ecad::Violation>& v, int& erreurs, int& avertissements) {
  J r = J::array();
  for (const auto& x : v) {
    const bool e = x.gravite == ecad::Gravite::Erreur;
    (e ? erreurs : avertissements)++;
    r.push_back({{"gravite", e ? "erreur" : "avertissement"}, {"code", x.code}, {"message", x.message}, {"x", ecad::mm(x.position.x)}, {"y", ecad::mm(x.position.y)}});
  }
  return r;
}
int rendre(const Options& o, const std::string& commande, const J& rapport, int erreurs, const std::vector<std::string>& lignes) {
  if (o.json) {
    J r = {{"commande", commande}, {"logiciel", ligneVersion()}, {"erreurs", erreurs}};
    for (auto it = rapport.begin(); it != rapport.end(); ++it) r[it.key()] = it.value();
    std::cout << r.dump(2) << '\n';
  } else {
    for (const auto& l : lignes) std::cout << l << '\n';
    std::cout << (erreurs ? std::to_string(erreurs) + (erreurs > 1 ? " erreurs." : " erreur.") : std::string("Conforme.")) << '\n';
  }
  return erreurs > 0 ? 1 : 0;
}
std::vector<std::string> lignesViolations(const J& v) {
  std::vector<std::string> r;
  for (const auto& x : v) r.push_back("  " + x["gravite"].get<std::string>() + "  " + x["code"].get<std::string>() + "  " + x["message"].get<std::string>());
  return r;
}
}  // namespace

// Rend −1 si la commande n'est pas de celles-ci
int commandeAtelier(const std::vector<std::string>& a) {
  if (a.empty()) return -1;
  const std::string& c = a[0];
  const Options o = lireOptions(a);
  const auto& pos = o.positionnels;
  if ((c == "erc" || c == "drc") && pos.size() == 1) {
    const Projet p = lireProjet(pos[0]);
    const ecad::ConceptionElec& ce = conceptionDe(p, o.conception);
    const ecad::Connectivite k = ecad::connectivite(ce);
    const auto v = c == "erc" ? ecad::verifierERC(ce, k, ecad::NiveauErc::Complet) : ecad::verifierDRCReglee(ce, k, ecad::remplirZones(ce, k));
    int e = 0, w = 0;
    const J vj = violationsJson(v, e, w);
    return rendre(o, c, {{"conception", ce.nom}, {"avertissements", w}, {"violations", vj}}, e, lignesViolations(vj));
  }
  if (c == "revue" && pos.size() == 1) {
    const Projet p = lireProjet(pos[0]);
    J r = J::array();
    std::vector<std::string> l;
    int e = 0;
    for (const auto& x : ecad::revueConception(conceptionDe(p, o.conception))) {
      e += x.gravite == "erreur";
      r.push_back({{"regle", x.regle}, {"gravite", x.gravite}, {"objet", x.objet}, {"message", x.message}, {"correction", x.correction}});
      l.push_back("  " + x.gravite + "  " + x.regle + "  " + x.message + "  → " + x.correction);
    }
    return rendre(o, c, {{"points", r}}, e, l);
  }
  if (c == "valider" && pos.size() == 1) {
    // Tout ce qui empêche de publier : ERC, DRC, revue, variantes, exigences, contrôles des sorties
    const Projet p = lireProjet(pos[0]);
    const ecad::ConceptionElec& ce = conceptionDe(p, o.conception);
    const ecad::Connectivite k = ecad::connectivite(ce);
    int e = 0, w = 0;
    const J erc = violationsJson(ecad::verifierERC(ce, k, ecad::NiveauErc::Complet), e, w);
    const J drc = violationsJson(ecad::verifierDRCReglee(ce, k, ecad::remplirZones(ce, k)), e, w);
    J revue = J::array(), variantes = J::array(), controles = J::array(), exigences = J::array();
    std::vector<std::string> l = lignesViolations(erc);
    for (const auto& x : lignesViolations(drc)) l.push_back(x);
    for (const auto& x : ecad::revueConception(ce)) {
      e += x.gravite == "erreur";
      revue.push_back({{"regle", x.regle}, {"gravite", x.gravite}, {"message", x.message}});
      l.push_back("  " + x.gravite + "  " + x.regle + "  " + x.message);
    }
    for (const auto& x : ecad::verifierVariantes(ce)) {
      ++e;
      variantes.push_back(x);
      l.push_back("  erreur  VARIANTE  " + x);
    }
    for (const auto& x : ecad::controlerAvantSorties(ce)) {
      ++e;
      controles.push_back(x);
      l.push_back("  erreur  SORTIES  " + x);
    }
    for (const auto& x : verifierExigences(p)) {
      e += x.etat == "en défaut";
      exigences.push_back({{"id", x.id}, {"etat", x.etat}, {"mesure", x.mesure}});
      if (x.etat == "en défaut") l.push_back("  erreur  EXIGENCE  " + x.id + " : " + x.mesure);
    }
    return rendre(o, c, {{"conception", ce.nom}, {"avertissements", w}, {"erc", erc}, {"drc", drc}, {"revue", revue}, {"variantes", variantes}, {"sorties", controles}, {"exigences", exigences}}, e, l);
  }
  if (c == "exigences" && pos.size() == 1) {
    const Projet p = lireProjet(pos[0]);
    J r = J::array();
    std::vector<std::string> l;
    int e = 0;
    for (const auto& x : verifierExigences(p)) {
      e += x.etat == "en défaut";
      r.push_back({{"id", x.id}, {"etat", x.etat}, {"mesure", x.mesure}, {"liensManquants", x.liensManquants}});
      l.push_back("  " + x.id + "  " + x.etat + (x.mesure.empty() ? "" : "  (" + x.mesure + ")"));
    }
    return rendre(o, c, {{"exigences", r}}, e, l);
  }
  if (c == "sorties" && pos.size() == 2) {
    // promethee sorties <projet> <jeu> [-D CLE=VALEUR]… : un jeu enregistré dans la conception, ou « jlcpcb »
    const Projet p = lireProjet(pos[0]);
    const ecad::ConceptionElec& ce = conceptionDe(p, o.conception);
    std::optional<ecad::JeuSorties> jeu;
    for (const auto& j : ce.jeuxSorties)
      if (j.nom == pos[1]) jeu = j;
    if (!jeu && (pos[1] == "jlcpcb" || pos[1] == "Dossier JLCPCB")) jeu = ecad::jeuJlcpcb();
    if (!jeu) {
      std::cerr << "Jeu de sorties « " << pos[1] << " » inconnu de la conception « " << ce.nom << " ».\n";
      return 64;
    }
    const std::string base = utf8(fs::absolute(cheminFichier(pos[0])).parent_path());
    const ecad::ResultatJeu r = ecad::executerJeu(ce, *jeu, base, utf8(cheminFichier(pos[0]).stem()), infosVersion().identifiant, o.variables);
    std::vector<std::string> l = r.journal;
    for (const auto& x : r.refus) l.push_back("  refus : " + x);
    return rendre(o, c, {{"jeu", jeu->nom}, {"publie", r.publie}, {"dossier", r.dossier}, {"fichiers", r.fichiers}, {"refus", r.refus}, {"journal", r.journal}},
                  static_cast<int>(r.refus.size()), l);
  }
  if (c == "cout" && (pos.size() == 2 || pos.size() == 3)) {
    const Projet p = lireProjet(pos[0]);
    const ecad::CoutNomenclature n = ecad::coutNomenclature(conceptionDe(p, o.conception), std::stoi(pos[1]), pos.size() == 3 ? pos[2] : std::string());
    std::vector<std::string> l;
    J lignes = J::array();
    for (const auto& x : n.lignes) {
      std::string refs;
      for (const auto& r : x.references) refs += (refs.empty() ? "" : ",") + r;
      lignes.push_back({{"references", refs}, {"article", x.article}, {"commandee", x.commandee}, {"prixUnitaire", x.prixUnitaire}, {"total", x.total}, {"prixConnu", x.prixConnu}});
      l.push_back("  " + refs + "  ×" + std::to_string(x.commandee) + (x.prixConnu ? "  " + std::to_string(x.total) + " " + x.devise : std::string("  prix inconnu")));
    }
    l.push_back("Total pour " + std::to_string(n.cartes) + " cartes : " + std::to_string(n.total) + " " + n.devise + " (" + std::to_string(n.lignesSansPrix) + " ligne(s) sans prix)");
    return rendre(o, c, {{"cartes", n.cartes}, {"total", n.total}, {"parCarte", n.parCarte}, {"devise", n.devise}, {"lignesSansPrix", n.lignesSansPrix}, {"lignes", lignes}}, 0, l);
  }
  if (c == "comparer" && pos.size() == 2) {
    const Projet a1 = lireProjet(pos[0]), b1 = lireProjet(pos[1]);
    const ecad::ConceptionElec& x = conceptionDe(a1, o.conception);
    const ecad::ConceptionElec& y = conceptionDe(b1, o.conception);
    const ecad::ComparaisonConceptions d = ecad::comparerConceptions(x, x.uuid == y.uuid ? y : ecad::aligner(x, y));
    J r = J::array();
    std::vector<std::string> l;
    for (const auto& z : d.differences) {
      r.push_back({{"categorie", z.categorie}, {"genre", z.genre}, {"repere", z.repere}, {"detail", z.detail}});
      l.push_back("  " + z.categorie + " " + z.genre + "  " + z.repere + (z.detail.empty() ? "" : "  " + z.detail));
    }
    if (o.json) std::cout << J{{"commande", "comparer"}, {"identiques", d.identiques()}, {"differences", r}}.dump(2) << '\n';
    else {
      for (const auto& x : l) std::cout << x << '\n';
      std::cout << (d.identiques() ? std::string("Identiques.") : std::to_string(d.differences.size()) + " différence(s).") << '\n';
    }
    return d.identiques() ? 0 : 1;
  }
  if (c == "fusionner" && pos.size() == 4) {
    const Projet anc = lireProjet(pos[0]), nous = lireProjet(pos[1]), eux = lireProjet(pos[2]);
    Projet sortie = nous;
    std::vector<std::string> l;
    J conflits = J::array();
    for (std::size_t i = 0; i < nous.electronique.size(); ++i) {
      const ecad::ConceptionElec* a0 = nullptr;
      const ecad::ConceptionElec* e0 = nullptr;
      for (const auto& x : anc.electronique)
        if (x.uuid == nous.electronique[i].uuid) a0 = &x;
      for (const auto& x : eux.electronique)
        if (x.uuid == nous.electronique[i].uuid) e0 = &x;
      if (!a0 || !e0) continue;
      const ecad::ResultatFusion f = ecad::fusionner(*a0, nous.electronique[i], *e0);
      sortie.electronique[i] = f.conception;
      for (const auto& x : f.conflits) {
        conflits.push_back({{"chemin", x.chemin}, {"categorie", x.categorie}, {"repere", x.repere}, {"propriete", x.propriete}, {"ancetre", x.ancetre}, {"nous", x.nous}, {"eux", x.eux}});
        l.push_back("  conflit  " + x.categorie + " " + x.repere + " " + x.propriete + " : " + x.ancetre + " → nous " + x.nous + ", eux " + x.eux + " (gardé : nous)");
      }
    }
    ecrireProjet(sortie, pos[3]);
    l.push_back("Écrit : " + pos[3]);
    return rendre(o, c, {{"sortie", pos[3]}, {"conflits", conflits}}, static_cast<int>(conflits.size()), l);
  }
  if (c == "archiver" && pos.size() == 2) {
    const Projet p = lireProjet(pos[0]);
    std::vector<std::string> manquants;
    ecrireFichierAtomique(cheminFichier(pos[1]), archiverProjet(p, pos[0], &manquants));
    std::vector<std::string> l{"Archive écrite : " + pos[1]};
    for (const auto& m : manquants) l.push_back("  fichier cité introuvable : " + m);
    return rendre(o, c, {{"archive", pos[1]}, {"manquants", manquants}}, static_cast<int>(manquants.size()), l);
  }
  if (c == "extraire" && pos.size() == 2) {
    std::vector<std::string> defauts;
    const std::string projet = extraireArchive(lireFichier(cheminFichier(pos[0])), pos[1], &defauts);
    std::vector<std::string> l{"Projet extrait : " + projet};
    for (const auto& d : defauts) l.push_back("  " + d);
    return rendre(o, c, {{"projet", projet}, {"defauts", defauts}}, static_cast<int>(defauts.size()), l);
  }
  if (c == "placer" && pos.size() == 2) {
    Projet p = lireProjet(pos[0]);
    if (o.conception == 0 || o.conception > p.electronique.size()) return 64;
    ecad::ConceptionElec& ce = p.electronique[o.conception - 1];
    const double avant = ecad::longueurChevelu(ce);
    ecad::OptionsPlacementAuto op;
    if (const auto it = o.variables.find("GRAINE"); it != o.variables.end()) op.graine = static_cast<unsigned>(std::stoul(it->second));
    const ecad::ResultatPlacementAuto r = ecad::placerAutomatiquement(ce, op);
    ecrireProjet(p, pos[1]);
    return rendre(o, c, {{"cheveluAvant", avant}, {"chevelu", r.chevelu}, {"collisions", r.collisions}, {"deplaces", r.deplaces}}, r.collisions,
                  {"Chevelu : " + std::to_string(avant) + " mm → " + std::to_string(r.chevelu) + " mm, " + std::to_string(r.collisions) + " collision(s). Écrit : " + pos[1]});
  }
  if (c == "api" && (pos.size() == 1 || pos.size() == 2)) {
    // Une requête JSON-RPC par ligne sur l'entrée standard, une réponse par ligne ; le projet modifié est enregistré
    // à la fin dans le second fichier s'il est donné
    Projet p = lireProjet(pos[0]);
    bool modifie = false;
    std::string ligne;
    while (std::getline(std::cin, ligne)) {
      if (ligne.empty()) continue;
      const ReponseApi r = traiterTexte(p, ligne);
      modifie |= r.modifie;
      std::cout << r.reponse.dump() << '\n' << std::flush;
    }
    if (modifie && pos.size() == 2) ecrireProjet(p, pos[1]);
    return 0;
  }
  if (c == "isolement" && pos.size() == 2) {
    const Projet p = lireProjet(pos[0]);
    const ecad::ConceptionElec& ce = conceptionDe(p, o.conception);
    ecad::OptionsUsinage u;
    if (const auto it = o.variables.find("OUTIL"); it != o.variables.end()) u.diametreOutil = std::stod(it->second);
    if (const auto it = o.variables.find("PASSES"); it != o.variables.end()) u.passes = std::stoi(it->second);
    fs::create_directories(cheminFichier(pos[1]));
    std::vector<std::string> l, alertes;
    for (const auto couche : {ecad::Couche::CuivreDessus, ecad::Couche::CuivreDessous}) {
      if (couche == ecad::Couche::CuivreDessus && ce.circuit.empilage.couchesCuivre < 2) continue;
      u.miroir = couche == ecad::Couche::CuivreDessous;
      const ecad::ResultatIsolement r = ecad::sillonsIsolement(ce, couche, u);
      const std::string nom = couche == ecad::Couche::CuivreDessus ? "isolement-dessus.nc" : "isolement-dessous.nc";
      const std::string g = ecad::gcodeIsolement(ce, r, u);
      ecrireFichierAtomique(cheminFichier(pos[1]) / nom, g);
      l.push_back(nom + " : " + std::to_string(r.sillons.size()) + " sillons, " + std::to_string(static_cast<int>(ecad::dureeGcode(g))) + " min");
      alertes.insert(alertes.end(), r.alertes.begin(), r.alertes.end());
    }
    ecrireFichierAtomique(cheminFichier(pos[1]) / "percages.nc", ecad::gcodePercages(ce, u));
    ecrireFichierAtomique(cheminFichier(pos[1]) / "decoupe.nc", ecad::gcodeDecoupe(ce, u));
    ecrireFichierAtomique(cheminFichier(pos[1]) / "pochoir-dessus.svg", ecad::pochoirSvg(ce, false));
    for (const auto& a1 : alertes) l.push_back("  alerte : " + a1);
    return rendre(o, c, {{"dossier", pos[1]}, {"alertes", alertes}}, static_cast<int>(alertes.size()), l);
  }
  if (c == "mise-en-service" && pos.size() == 1) {
    const Projet p = lireProjet(pos[0]);
    const ecad::PlanMiseSousTension m = ecad::planMiseSousTension(conceptionDe(p, o.conception));
    J etapes = J::array();
    std::vector<std::string> l;
    for (const auto& e : m.etapes) {
      etapes.push_back({{"numero", e.numero}, {"action", e.action}, {"reseau", e.reseau}, {"attendu", e.attendu}, {"min", e.min}, {"max", e.max}});
      l.push_back(std::to_string(e.numero) + ". " + e.action + (e.attendu.empty() ? "" : "  [" + e.attendu + "]"));
    }
    for (const auto& a1 : m.alertes) l.push_back("  alerte : " + a1);
    return rendre(o, c, {{"rails", m.rails}, {"etapes", etapes}, {"alertes", m.alertes}}, 0, l);
  }
  return -1;
}

void aideAtelier() {
  std::cout << "\nConception électronique (rapport --json, code de retour 0 conforme, 1 défauts, 2 erreur) :\n"
               "  promethee erc|drc|revue|valider|exigences <projet.prom> [-n N] [--json]\n"
               "  promethee sorties <projet.prom> <jeu|jlcpcb> [-D CLE=VALEUR]…   lance un jeu de sorties enregistré\n"
               "  promethee cout <projet.prom> <cartes> [variante]                coût de la nomenclature aux prix par palier\n"
               "  promethee comparer <a.prom> <b.prom> [--json]                   différences par catégorie (1 si différents)\n"
               "  promethee fusionner <ancetre> <nous> <eux> <sortie>             fusion à trois voies (1 s'il reste des conflits)\n"
               "  promethee archiver <projet.prom> <archive.zip> | extraire <archive.zip> <dossier>\n"
               "  promethee placer <projet.prom> <sortie.prom> [-D GRAINE=n]      placement automatique\n"
               "  promethee api <projet.prom> [sortie.prom]                       requêtes JSON-RPC, une par ligne, sur l'entrée\n"
               "  promethee isolement <projet.prom> <dossier> [-D OUTIL=0.2]      G-code de gravure, perçage, découpe, pochoir\n"
               "  promethee mise-en-service <projet.prom>                         plan de mise sous tension\n";
}

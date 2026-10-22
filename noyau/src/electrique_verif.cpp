// Prométhée : vérifications de la CAO électrique. Le contrôle électrique du schéma (fils, bornes en l'air,
// courts-circuits entre potentiels, repères en double) vient du moteur commun ; s'y ajoutent les règles du
// métier d'électricien : contacts rattachés à leur appareil, nombre de contacts, bornes, renvois, couleurs et
// sections des conducteurs, câbles, protection des moteurs, tensions de commande. Licence GPL-3.0-only.
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "electrique_interne.hpp"
#include "promethee/electrique.hpp"

namespace prom::elec {

using ecad::ComposantElec;
using ecad::DefSymbole;
using ecad::Gravite;
using ecad::Violation;

namespace {

bool estContact(RoleSymbole r) {
  switch (r) {
    case RoleSymbole::ContactNO:
    case RoleSymbole::ContactNF:
    case RoleSymbole::ContactInverseur:
    case RoleSymbole::ContactNOTempo:
    case RoleSymbole::ContactNFTempo:
    case RoleSymbole::PolesPuissance:
    case RoleSymbole::ContactThermiqueNF:
    case RoleSymbole::ContactThermiqueNO: return true;
    default: return false;
  }
}

// Section minimale d'un conducteur en cuivre protégé par un appareil de ce calibre (disjoncteur, fusible gG) :
// correspondance usuelle de la NF C 15-100 et de l'EN 60204-1 pour le filerie d'armoire
double sectionMinimale(double calibre) {
  static const std::vector<std::pair<double, double>> t = {{3, 0.5},  {6, 0.75}, {10, 1.0}, {16, 1.5}, {20, 2.5}, {25, 4},   {32, 6},    {50, 10},
                                                           {63, 16}, {80, 25},  {100, 35}, {125, 50}, {160, 70}, {200, 95}, {250, 120}};
  for (const auto& [in, s] : t)
    if (calibre <= in + 1e-9) return s;
  return 150;
}

bool protectionDeCircuit(const std::string& s) {
  return s.rfind("iec:disj", 0) == 0 || s == "iec:fusible" || s == "iec:sect-fusible-3p" || s == "iec:u-disj" || s == "iec:agcp" || s == "iec:u-agcp";
}

bool sortieDeProtection(const std::string& n) {
  if (n == "N2") return true;
  return n.size() == 1 && (n[0] == '2' || n[0] == '4' || n[0] == '6' || n[0] == '8');
}

}  // namespace

std::vector<Violation> verifierElectrique(const ProjetElectrique& p) {
  const ecad::ConceptionElec& c = p.schema;
  const ecad::Connectivite k = ecad::connectivite(c);
  std::vector<Violation> v;
  auto ajouter = [&](Gravite g, const char* code, std::string message, std::string conseil, const std::string& feuille, ecad::Pt ou, std::vector<std::string> objets = {}) {
    v.push_back({g, code, std::move(message), std::move(conseil), feuille, ou, std::move(objets)});
  };

  // Potentiels qui aboutissent à un symbole graphique (départ d'un unifilaire, renvoi dessiné) : leurs bornes ne
  // sont pas « en l'air »
  std::set<int> departs;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.composant.empty())
        if (const auto it = k.parSymbole.find(s.uuid); it != k.parSymbole.end()) {
          const DefSymbole* d = detail::definitionDe(p, s.symbole);
          if (d && !d->alimentation && !d->drapeau) departs.insert(it->second);
        }
  // ----- Contrôle électrique commun, dit dans les mots de l'électricien -----
  for (Violation x : ecad::verifierERC(c, k)) {
    // Pas d'empreintes ; renvois traités plus bas ; un appareil se pose en plusieurs symboles (bobine, contacts) dont
    // les poses sont vérifiées plus bas (ERC-41 ne vaut qu'en électronique). Les contrôles de dessin propres à
    // l'électronique (broche au milieu d'un fil, cadre et grille de 1,27 mm de la feuille, broches bout à bout)
    // ne valent pas pour un folio : les contacts s'y enchaînent borne contre borne, sur la grille du folio
    if (x.code == "ERC-03" || x.code == "ERC-04" || x.code == "ERC-05" || x.code == "ERC-31" || x.code == "ERC-41" || x.code == "ERC-14" || x.code == "ERC-15" ||
        x.code == "ERC-18" || x.code == "ERC-19")
      continue;
    if (x.code == "ERC-23" && x.message.find("0V") != std::string::npos && x.message.find("PE") != std::string::npos &&
        std::count(x.message.begin(), x.message.end(), ',') == 1)
      continue;  // 0 V relié à la terre : pratique courante des circuits de commande (EN 60204-1)
    if (x.code == "ERC-10") {
      const auto i = x.message.find(" broche ");
      if (i != std::string::npos) {
        std::string numero = x.message.substr(i + 8);
        if (const auto j = numero.find(" ("); j != std::string::npos) numero.resize(j);
        // Entrée ou sortie d'automate libre : une réserve, pas un oubli
        if (numero.size() >= 4 && (numero[0] == 'I' || numero[0] == 'Q') && numero.find('.') != std::string::npos) continue;
        if (!x.objets.empty() && departs.count(k.reseau(x.objets.front(), numero))) continue;
        const std::string ref = x.message.substr(std::string("Broche non connectée : ").size(), i - std::string("Broche non connectée : ").size());
        x.message = "Borne non raccordée : " + (p.reglages.tiret ? std::string("-") : std::string()) + ref + ":" + numero;
        x.conseil = "Raccorde-la, ou pose une marque de non-connexion si elle reste libre.";
      }
    }
    if (x.code == "ERC-02") {
      x.message.replace(0, std::string("Composant non annoté").size(), "Appareil sans repère");
      x.conseil = "Lance « Repérer les appareils » : chaque appareil reçoit sa lettre et son numéro (-KM1, -S2…).";
    }
    if (x.code == "ERC-06") x.message.replace(0, std::string("Référence en double").size(), "Repère en double");
    if (x.code == "ERC-23") x.conseil = "Deux potentiels différents se retrouvent reliés : vérifie les fils, les renvois et les symboles de potentiel.";
    v.push_back(x);
  }

  // ----- Poses : contacts, bornes, symboles principaux -----
  std::map<std::string, std::vector<std::pair<std::size_t, const ecad::SymbolePlace*>>> poses;  // appareil → poses
  for (std::size_t f = 0; f < c.feuilles.size(); ++f)
    for (const auto& s : c.feuilles[f].symboles) {
      const RoleSymbole role = roleSymbole(s.symbole);
      const ComposantElec* x = s.composant.empty() ? nullptr : c.composant(s.composant);
      if (!s.composant.empty()) poses[s.composant].push_back({f, &s});
      if ((estContact(role) || role == RoleSymbole::Borne) && !x) {
        ajouter(Gravite::Erreur, "ELE-01", std::string(role == RoleSymbole::Borne ? "Borne" : "Contact") + " rattaché à aucun appareil",
                "Ouvre ses propriétés et choisis l'appareil (bobine, disjoncteur, bornier) auquel il appartient.", c.feuilles[f].uuid, s.position, {s.uuid});
        continue;
      }
      if (!x) continue;
      const RoleSymbole roleAppareil = roleSymbole(x->symbole);
      if (estContact(role) && roleAppareil == RoleSymbole::Borne)
        ajouter(Gravite::Erreur, "ELE-02", "Contact rattaché au bornier " + repereAffiche(p, *x), "Rattache ce contact à la bobine ou à l'appareil qui le porte.", c.feuilles[f].uuid,
                s.position, {s.uuid});
      if (role == RoleSymbole::Borne && roleAppareil != RoleSymbole::Borne)
        ajouter(Gravite::Erreur, "ELE-06", "Borne rattachée à " + repereAffiche(p, *x) + ", qui n'est pas un bornier", "Rattache la borne à un bornier (-X1, -X2…).", c.feuilles[f].uuid,
                s.position, {s.uuid});
    }
  for (const auto& x : c.composants) {
    const auto it = poses.find(x.uuid);
    const std::string repere = repereAffiche(p, x);
    if (it == poses.end()) {
      ajouter(Gravite::Avertissement, "ELE-22", repere + " n'est posé sur aucun folio", "Pose-le, ou supprime-le de la liste des appareils.", {}, {}, {x.uuid});
      continue;
    }
    const auto& liste = it->second;
    const std::string feuille = c.feuilles[liste.front().first].uuid;
    const ecad::Pt ou = liste.front().second->position;
    if (detail::estBornier(x)) {
      // Une même borne posée deux fois réunit deux circuits
      std::map<std::string, int> fois;
      for (const auto& [f, s] : liste) {
        const DefSymbole* d = detail::definitionDe(p, s->symbole);
        if (d && !d->broches.empty()) ++fois[ecad::numeroBroche(*s, d->broches.front().numero)];
      }
      for (const auto& [n, nb] : fois)
        if (nb > 1)
          ajouter(Gravite::Avertissement, "ELE-05", "Borne " + repere + ":" + n + " posée " + std::to_string(nb) + " fois : ses poses sont un seul point électrique",
                  "Donne à chaque pose son propre numéro de borne, sauf si c'est voulu.", feuille, ou, {x.uuid});
      continue;
    }
    bool principal = false;
    int no = 0, nf = 0, inverseurs = 0, poles = 0, thermiqueNF = 0;
    for (const auto& [f, s] : liste) {
      const RoleSymbole r = roleSymbole(s->symbole);
      if (s->symbole == x.symbole || r == RoleSymbole::Principal || r == RoleSymbole::Autre) principal = true;
      switch (r) {
        case RoleSymbole::ContactNO:
        case RoleSymbole::ContactNOTempo: ++no; break;
        case RoleSymbole::ContactNF:
        case RoleSymbole::ContactNFTempo: ++nf; break;
        case RoleSymbole::ContactInverseur: ++inverseurs; break;
        case RoleSymbole::PolesPuissance: ++poles; break;
        case RoleSymbole::ContactThermiqueNF: ++thermiqueNF; break;
        default: break;
      }
    }
    if (!principal)
      ajouter(Gravite::Avertissement, "ELE-03", "Le symbole principal de " + repere + " (bobine, corps) n'est posé sur aucun folio",
              "Pose-le : ses contacts renvoient à lui, et sans lui leurs renvois restent vides.", feuille, ou, {x.uuid});
    if (const EntreeCatalogue* e = detail::catalogueDe(x)) {
      const bool trop = e->inverseurs > 0 ? no + nf + inverseurs > e->inverseurs + e->contactsNO + e->contactsNF
                                          : (no > e->contactsNO && (e->contactsNO > 0 || e->contactsNF > 0)) || (nf > e->contactsNF && (e->contactsNO > 0 || e->contactsNF > 0)) ||
                                                (inverseurs > 0);
      if (trop && (no + nf + inverseurs) > 0 && x.symbole.find("bobine") != std::string::npos)
        ajouter(Gravite::Avertissement, "ELE-04",
                repere + " : " + std::to_string(no) + " contact(s) à fermeture, " + std::to_string(nf) + " à ouverture et " + std::to_string(inverseurs) +
                    " inverseur(s) posés, plus que l'appareil n'en a (" + e->designation + ")",
                "Ajoute un bloc de contacts auxiliaires (et sa référence), ou un relais.", feuille, ou, {x.uuid});
    }
    if (x.symbole == "iec:relais-thermique-3p" && thermiqueNF == 0)
      ajouter(Gravite::Avertissement, "ELE-15", "Le contact 95-96 du relais thermique " + repere + " n'est pas dans la commande : une surcharge ne couperait rien",
              "Pose « Contact du relais thermique (95-96) » en série dans la commande de la bobine.", feuille, ou, {x.uuid});
    if (x.symbole == "iec:bobine-km" && poles == 0)
      ajouter(Gravite::Avertissement, "ELE-16", "Les pôles de puissance du contacteur " + repere + " ne sont posés sur aucun folio",
              "Pose « Pôles de contacteur » sur le folio de puissance et rattache-les à " + repere + ".", feuille, ou, {x.uuid});
    // Tension de bobine et tension de commande
    if (const EntreeCatalogue* e = detail::catalogueDe(x);
        e && e->tensionBobine > 0 && (x.symbole.rfind("iec:bobine", 0) == 0 || x.symbole == "iec:electrovanne") && p.reglages.tensionCommande > 0) {
      const bool memeTension = std::abs(e->tensionBobine - p.reglages.tensionCommande) < 0.5 && e->bobineContinue == p.reglages.commandeContinue;
      const bool multitension = x.symbole.find("tempo") != std::string::npos;
      if (!memeTension && !multitension)
        ajouter(Gravite::Avertissement, "ELE-11",
                repere + " : bobine " + detail::nombreFr(e->tensionBobine) + (e->bobineContinue ? " V continu" : " V alternatif") + ", la commande est en " +
                    detail::nombreFr(p.reglages.tensionCommande) + (p.reglages.commandeContinue ? " V continu" : " V alternatif"),
                "Choisis la référence à la tension de commande, ou change la tension de commande dans les réglages du projet.", feuille, ou, {x.uuid});
    }
  }

  // ----- Moteurs : protection contre les surcharges, liaison à la terre -----
  for (const auto& x : c.composants) {
    if (x.symbole.rfind("iec:moteur", 0) != 0) continue;  // les moteurs (pas le disjoncteur moteur)
    const auto it = poses.find(x.uuid);
    if (it == poses.end()) continue;
    const std::string feuille = c.feuilles[it->second.front().first].uuid;
    const ecad::Pt ou = it->second.front().second->position;
    // De proche en proche depuis les bornes du moteur, en remontant à travers les pôles de contacteur et les
    // sectionneurs (le disjoncteur moteur est le plus souvent en amont du contacteur), trois appareils au plus
    bool protege = false, alimente = false;
    auto oppose = [](const std::string& n) -> std::string {
      if (n.size() != 1 || n[0] < '1' || n[0] > '8') return {};
      const int v = n[0] - '0';
      return std::string(1, static_cast<char>('0' + (v % 2 ? v + 1 : v - 1)));
    };
    std::set<int> vus;
    std::vector<int> aVoir;
    for (const char* b : {"U1", "V1", "W1", "U2", "A1"})
      if (const int i = k.reseau(x.uuid, b); i >= 0 && vus.insert(i).second) aVoir.push_back(i);
    for (int etape = 0; etape < 3 && !aVoir.empty() && !protege; ++etape) {
      std::vector<int> suivants;
      for (const int i : aVoir)
        for (const auto& n : k.reseaux[static_cast<std::size_t>(i)].broches) {
          if (n.composant == x.uuid) continue;
          const ComposantElec* y = c.composant(n.composant);
          if (!y) continue;
          alimente = alimente || !detail::estBornier(*y);
          if (y->symbole == "iec:relais-thermique-3p" || y->symbole == "iec:disj-moteur-3p" || y->symbole == "iec:variateur" || y->symbole == "iec:demarreur") protege = true;
          const bool traversable = y->symbole == "iec:bobine-km" || y->symbole.rfind("iec:sect", 0) == 0 || y->symbole.rfind("iec:inter-sect", 0) == 0;
          if (!traversable) continue;
          if (const std::string o = oppose(n.broche); !o.empty())
            if (const int j = k.reseau(y->uuid, o); j >= 0 && vus.insert(j).second) suivants.push_back(j);
        }
      aVoir = std::move(suivants);
    }
    if (alimente && !protege && x.symbole != "iec:moteur-cc")
      ajouter(Gravite::Avertissement, "ELE-13", "Moteur " + repereAffiche(p, x) + " sans protection contre les surcharges",
              "Ajoute un relais thermique ou un disjoncteur moteur réglé sur son courant nominal (ou un variateur qui le protège).", feuille, ou, {x.uuid});
    if (x.symbole != "iec:moteur-cc") {
      const int i = k.reseau(x.uuid, "PE");
      const bool terre = i >= 0 && classeConducteur(k.reseaux[static_cast<std::size_t>(i)].nom) == ClasseConducteur::Protection;
      if (!terre)
        ajouter(Gravite::Avertissement, "ELE-14", "La masse du moteur " + repereAffiche(p, x) + " n'est pas reliée au conducteur de protection (PE)",
                "Relie sa borne PE au potentiel PE (borne vert-jaune du bornier, symbole de terre).", feuille, ou, {x.uuid});
    }
  }

  // ----- Renvois et potentiels posés une seule fois -----
  {
    std::map<std::string, std::vector<std::pair<std::size_t, const ecad::Etiquette*>>> globales;
    std::set<std::string> alimentations;
    for (std::size_t f = 0; f < c.feuilles.size(); ++f) {
      for (const auto& e : c.feuilles[f].etiquettes)
        if (e.type == ecad::TypeEtiquette::Globale) globales[e.texte].push_back({f, &e});
      for (const auto& s : c.feuilles[f].symboles)
        if (const DefSymbole* d = detail::definitionDe(p, s.symbole); d && d->alimentation) alimentations.insert(s.nomAlim.empty() ? d->nom : s.nomAlim);
    }
    // Les potentiels d'alimentation (L1, L2, L3, N, PE, 24V, 0V…) posés une fois sont des arrivées : la source
    // est hors du dessin (tableau amont, alimentation d'un autre projet) ; seuls les autres noms sont des renvois
    for (const auto& [nom, liste] : globales)
      if (liste.size() == 1 && !alimentations.count(nom) && classeConducteur(nom) == ClasseConducteur::Autre) {
        const auto& [f, e] = liste.front();
        ajouter(Gravite::Avertissement, "ELE-07", "Le potentiel « " + nom + " » n'est renvoyé nulle part (posé une seule fois, folio " + position(p, f, e->p).texte() + ")",
                "Pose le même potentiel sur le folio où il continue, ou supprime ce renvoi.", c.feuilles[f].uuid, e->p, {e->uuid});
      }
  }

  // ----- Conducteurs : couleur du PE, sections et calibres -----
  for (const auto& [fil, pr] : p.conducteurs) {
    const bool pe = classeConducteur(fil) == ClasseConducteur::Protection;
    if (pe && !pr.couleur.empty() && pr.couleur != "GNYE")
      ajouter(Gravite::Erreur, "ELE-08", "Le conducteur de protection « " + fil + " » est en " + pr.couleur + " : il doit être vert-jaune", "Remets-le en vert-jaune (GNYE), réservé à la protection.", {}, {});
    if (!pe && pr.couleur == "GNYE")
      ajouter(Gravite::Erreur, "ELE-08", "Le conducteur « " + fil + " » est vert-jaune sans être un conducteur de protection", "Le vert-jaune est réservé au PE : choisis une autre couleur.", {}, {});
  }
  for (const auto& x : c.composants) {
    if (!protectionDeCircuit(x.symbole)) continue;
    // Protection tirée d'un calcul (installation, photovoltaïque) : sa canalisation y est justifiée (pose, température…)
    if (!detail::champ(x, "origine").empty()) continue;
    const double in = detail::calibreDe(x);
    if (in <= 0) continue;
    const double minimum = sectionMinimale(in);
    std::set<int> vus;
    for (const auto& [cle, pos] : k.parBroche) {
      if (cle.first != x.uuid || !sortieDeProtection(cle.second) || !vus.insert(pos).second) continue;
      const ecad::Reseau& r = k.reseaux[static_cast<std::size_t>(pos)];
      if (r.broches.size() < 2) continue;
      const ProprietesConducteur pr = detail::proprietes(p, k, r.nom);
      if (pr.section > 0 && pr.section + 1e-9 < minimum) {
        const auto it = poses.find(x.uuid);
        ajouter(Gravite::Avertissement, "ELE-09",
                "Fil " + (detail::nomAutomatique(r.nom) ? std::string("non numéroté") : r.nom) + " en " + detail::nombreFr(pr.section) + " mm² derrière " + repereAffiche(p, x) + " (" +
                    detail::nombreFr(in) + " A) : " + detail::nombreFr(minimum) + " mm² au moins",
                "Augmente la section du conducteur, ou baisse le calibre de la protection.", it != poses.end() ? c.feuilles[it->second.front().first].uuid : std::string(),
                it != poses.end() ? it->second.front().second->position : ecad::Pt{}, {x.uuid});
      }
    }
  }

  // ----- Câbles -----
  {
    std::map<std::string, int> references;
    for (const auto& w : p.cables) {
      ++references[w.reference];
      if (w.reference.empty()) ajouter(Gravite::Avertissement, "ELE-10", "Câble sans repère (" + w.designation + ")", "Donne-lui un repère (-W1, -W2…).", {}, {});
      if (static_cast<int>(w.affectation.size()) > w.conducteurs)
        ajouter(Gravite::Erreur, "ELE-10", "Le câble " + w.reference + " a " + std::to_string(w.conducteurs) + " conducteurs mais " + std::to_string(w.affectation.size()) + " sont affectés",
                "Choisis un câble avec plus de conducteurs, ou retire des affectations.", {}, {});
      for (const auto& a : w.affectation) {
        if (a.fil.empty()) continue;
        const bool pe = classeConducteur(a.fil) == ClasseConducteur::Protection;
        if (pe && a.repere != "GNYE")
          ajouter(Gravite::Erreur, "ELE-10", "Le câble " + w.reference + " porte le PE sur son conducteur " + a.repere, "Le PE passe par le conducteur vert-jaune du câble.", {}, {});
        if (!pe && a.repere == "GNYE")
          ajouter(Gravite::Erreur, "ELE-10", "Le conducteur vert-jaune du câble " + w.reference + " porte « " + a.fil + " »", "Le vert-jaune est réservé au PE.", {}, {});
        if (k.indice(a.fil) < 0)
          ajouter(Gravite::Avertissement, "ELE-10", "Le câble " + w.reference + " porte le fil « " + a.fil + " », absent du schéma", "Corrige l'affectation du conducteur " + a.repere + ".", {}, {});
      }
    }
    for (const auto& [ref, n] : references)
      if (n > 1 && !ref.empty())
        ajouter(Gravite::Erreur, "ELE-10", "Repère de câble en double : " + ref, "Chaque câble a son propre repère.", {}, {});
  }

  // ----- Folios -----
  for (std::size_t f = 0; f < c.feuilles.size(); ++f)
    if (c.feuilles[f].nom.empty())
      ajouter(Gravite::Avertissement, "ELE-12", "Le folio " + std::to_string(f + 1) + " n'a pas de titre", "Donne-lui un titre : il apparaît dans le cartouche et la liste des folios.",
              c.feuilles[f].uuid, {});

  // ----- Dossier : références et numéros de fils -----
  {
    std::vector<std::string> sansReference, objets;
    for (const auto& x : c.composants) {
      if (detail::estBornier(x) || !poses.count(x.uuid)) continue;
      if (detail::catalogueDe(x) || !x.mpn.empty()) continue;
      if (!detail::champ(x, "origine").empty()) continue;  // désigné par un calcul (installation, photovoltaïque)
      sansReference.push_back(repereAffiche(p, x));
      objets.push_back(x.uuid);
    }
    if (!sansReference.empty()) {
      std::sort(sansReference.begin(), sansReference.end(), ecad::ordreNaturel);
      std::string liste;
      for (std::size_t i = 0; i < sansReference.size() && i < 6; ++i) liste += (i ? ", " : "") + sansReference[i];
      if (sansReference.size() > 6) liste += "…";
      ajouter(Gravite::Avertissement, "ELE-17", std::to_string(sansReference.size()) + " appareil(s) sans référence de catalogue (" + liste + ")",
              "Choisis une référence dans le panneau de l'appareil : la nomenclature et l'implantation de l'armoire en dépendent.", {}, {}, objets);
    }
    int sansNumero = 0;
    for (const auto& r : k.reseaux)
      if (!r.nomme && r.broches.size() >= 2 && detail::nomAutomatique(r.nom)) ++sansNumero;
    if (sansNumero > 0)
      ajouter(Gravite::Avertissement, "ELE-18", std::to_string(sansNumero) + " potentiel(s) sans numéro de fil", "Lance « Numéroter les fils » : les numéros donnés ne bougent plus ensuite.", {}, {});
  }
  std::stable_sort(v.begin(), v.end(), [](const Violation& a, const Violation& b) { return a.gravite < b.gravite; });
  return v;
}

}  // namespace prom::elec

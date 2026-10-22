// Prométhée : parcours d'apprentissage de l'atelier électrique (premier schéma de commande d'un moteur,
// installation calculée, toit photovoltaïque, tableau de logement). Chaque étape se vérifie sur le projet.
// Licence GPL-3.0-only.
#include "promethee/electrique_parcours.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

#include "promethee/electrique_distribution.hpp"
#include "promethee/electrique_grafcet.hpp"
#include "promethee/electrique_pv.hpp"

namespace prom::elec {

namespace {

// Identifiants des onglets de l'atelier électrique (AtelierElectrique::Mode)
constexpr int SCHEMA = 0, RAPPORTS = 3, LOGEMENT = 5, DISTRIBUTION = 6, PHOTOVOLTAIQUE = 7, GRAFCET = 8;

const ecad::ComposantElec* premier(const ProjetElectrique& p, const std::string& symbole) {
  for (const auto& c : p.schema.composants)
    if (c.symbole == symbole) return &c;
  return nullptr;
}
bool pose(const ProjetElectrique& p, const std::string& symbole) {
  for (const auto& f : p.schema.feuilles)
    for (const auto& s : f.symboles)
      if (s.symbole == symbole) return true;
  return false;
}
// Un élément (pôles, contact) posé et rattaché à un appareil d'un symbole donné
bool rattache(const ProjetElectrique& p, const std::string& element, const std::string& symboleAppareil) {
  for (const auto& f : p.schema.feuilles)
    for (const auto& s : f.symboles)
      if (s.symbole == element && !s.composant.empty())
        if (const ecad::ComposantElec* c = p.schema.composant(s.composant); c && c->symbole == symboleAppareil) return true;
  return false;
}
// Deux appareils reliés : une borne du premier et une du second sur le même potentiel
bool relies(const ProjetElectrique& p, const ecad::Connectivite& k, const std::string& a, const std::string& broche, const std::string& b) {
  const int r = k.reseau(a, broche);
  if (r < 0) return false;
  for (const auto& n : k.reseaux[static_cast<std::size_t>(r)].broches)
    if (n.composant == b) return true;
  (void)p;
  return false;
}
int erreurs(const ProjetElectrique& p) {
  int n = 0;
  for (const auto& v : verifierElectrique(p)) n += v.gravite == ecad::Gravite::Erreur ? 1 : 0;
  return n;
}
bool foliosOrigine(const ProjetElectrique& p, const std::string& origine) {
  for (const auto& f : p.schema.feuilles)
    if (p.folio(f.uuid).origine == origine) return true;
  return false;
}

std::vector<Parcours> construire() {
  std::vector<Parcours> v;
  // ----- Premier schéma : démarrer un moteur -----
  {
    Parcours m;
    m.id = "moteur";
    m.nom = "Premier schéma : démarrer un moteur";
    m.description = "Le schéma le plus classique de l'automatisme : un moteur protégé, un contacteur, Marche et Arrêt, l'auto-maintien. Neuf étapes, "
                    "chacune vérifiée par Prométhée.";
    m.genre = "schema";
    m.etapes.push_back({"Pose le disjoncteur moteur",
                        "Il protège le moteur contre la surcharge (réglé sur son courant nominal) et contre le court-circuit. C'est le premier appareil du "
                        "départ moteur.",
                        "Onglet Schéma, folio de puissance : tape « disjoncteur moteur » dans la recherche de la palette, à gauche, puis clique sur le folio.",
                        "ir", SCHEMA, [](const ProjetElectrique& p) { return pose(p, "iec:disj-moteur-3p"); }});
    m.etapes.push_back({"Pose la bobine du contacteur",
                        "Le contacteur est un interrupteur commandé à distance : sa bobine, alimentée par la commande, ferme ses pôles de puissance. On la "
                        "dessine sur le folio de commande.",
                        "Va sur le folio de commande (liste des folios à gauche), puis ruban : Appareil ▸ Bobine de contacteur, et clique sur le folio.", "",
                        SCHEMA, [](const ProjetElectrique& p) { return pose(p, "iec:bobine-km"); }});
    m.etapes.push_back({"Pose les pôles du contacteur",
                        "Les pôles sont dessinés sur le folio de puissance, mais appartiennent à la bobine : Prométhée leur donne les numéros 1-2, 3-4, 5-6 et "
                        "écrit sous la bobine où ils se trouvent (renvois).",
                        "Clique sur la bobine KM1, puis, dans le panneau de droite, « Pôles de puissance » : clique sous le disjoncteur pour les poser.", "renvoi",
                        SCHEMA, [](const ProjetElectrique& p) { return rattache(p, "iec:poles-km-3p", "iec:bobine-km"); }});
    m.etapes.push_back({"Pose le moteur", "Le récepteur : un moteur asynchrone triphasé, bornes U1, V1, W1 et la terre.",
                        "Folio de puissance : ruban Appareil ▸ Moteur triphasé, sous les pôles du contacteur.", "", SCHEMA,
                        [](const ProjetElectrique& p) { return pose(p, "iec:moteur-3"); }});
    m.etapes.push_back({"Relie la puissance",
                        "Le courant passe par le disjoncteur, les pôles du contacteur, puis le moteur. Chaque fil relie deux bornes ; les fils qui se touchent "
                        "forment un même potentiel.",
                        "Outil Fil (touche W) : clique sur une borne du disjoncteur, puis sur le pôle 1 du contacteur ; de même pour 3 et 5. Puis des pôles 2, "
                        "4, 6 vers U1, V1, W1 du moteur.",
                        "potentiel", SCHEMA, [](const ProjetElectrique& p) {
                          const ecad::ComposantElec* mot = premier(p, "iec:moteur-3");
                          const ecad::ComposantElec* km = premier(p, "iec:bobine-km");
                          if (!mot || !km) return false;
                          const ecad::Connectivite k = ecad::connectivite(p.schema);
                          return relies(p, k, mot->uuid, "U1", km->uuid) || relies(p, k, mot->uuid, "V1", km->uuid) || relies(p, k, mot->uuid, "W1", km->uuid);
                        }});
    m.etapes.push_back({"Pose Marche et Arrêt",
                        "La commande : un bouton d'arrêt (contact à ouverture, en série) puis un bouton de marche (contact à fermeture) alimentent la bobine.",
                        "Folio de commande : cherche « poussoir » dans la palette, pose « Bouton poussoir arrêt » puis « Bouton poussoir marche » au-dessus de la bobine.",
                        "", SCHEMA, [](const ProjetElectrique& p) { return pose(p, "iec:bp-nf") && pose(p, "iec:bp-no"); }});
    m.etapes.push_back({"Ajoute l'auto-maintien",
                        "Quand on relâche Marche, la bobine resterait sans courant : un contact à fermeture du contacteur, en parallèle du bouton Marche, la "
                        "maintient. Arrêt l'ouvre.",
                        "Clique sur la bobine KM1, puis « Contact à fermeture » dans le panneau : pose-le à côté de Marche (il devient 13-14) et relie-le en "
                        "parallèle.",
                        "", SCHEMA, [](const ProjetElectrique& p) { return rattache(p, "iec:contact-no", "iec:bobine-km"); }});
    m.etapes.push_back({"Numérote les fils",
                        "Chaque potentiel reçoit un numéro, écrit sur les fils et dans le carnet de câblage : c'est ce que le câbleur lit dans l'armoire.",
                        "Ruban : Numéroter ▸ Numéroter les fils.", "", SCHEMA, [](const ProjetElectrique& p) {
                          std::size_t fils = 0;
                          for (const auto& f : p.schema.feuilles) fils += f.fils.size();
                          if (fils < 4) return false;
                          const auto v = verifierElectrique(p);
                          return std::none_of(v.begin(), v.end(), [](const ecad::Violation& x) { return x.code == "ELE-18"; });
                        }});
    m.etapes.push_back({"Vérifie le schéma",
                        "Prométhée relit le schéma comme un électricien : bornes en l'air, contact sans bobine, moteur sans protection, conducteur de terre… "
                        "Chaque problème mène à son endroit d'un clic.",
                        "Ruban : Vérifier. Corrige les erreurs listées à droite (un clic sur l'une d'elles montre où elle est).", "", SCHEMA,
                        [](const ProjetElectrique& p) { return p.schema.composants.size() >= 5 && erreurs(p) == 0; }});
    m.etapes.push_back({"Regarde ce qui en sort",
                        "Du schéma sortent la liste du matériel, le plan des borniers, le carnet de câblage, et l'implantation dans l'armoire.",
                        "Onglet Rapports : Nomenclature, Borniers, Carnet de câblage ; onglet Armoire : Implanter.", "", RAPPORTS, {}});
    v.push_back(std::move(m));
  }
  // ----- Installation calculée -----
  {
    Parcours m;
    m.id = "installation";
    m.nom = "Calculer une installation";
    m.description = "De la source aux prises : courants, câbles, chutes de tension, courts-circuits, protection des personnes ; puis l'unifilaire et la "
                    "note de calcul.";
    m.genre = "distribution";
    m.etapes.push_back({"Pars d'une installation vide",
                        "Une installation part de sa source (réseau public, transformateur, groupe) qui fixe le courant de court-circuit, puis du tableau général.",
                        "Au centre, carte « Installation vide ».", "source", DISTRIBUTION, [](const ProjetElectrique& p) { return p.distribution.has_value(); }});
    m.etapes.push_back({"Ajoute un départ de prises",
                        "Un départ, c'est une protection dans le tableau, un câble, et ce qu'il alimente. Prométhée choisit le calibre et la section.",
                        "Ruban : Départ ▸ Prises de courant (il va dans le tableau choisi).", "ib", DISTRIBUTION, [](const ProjetElectrique& p) {
                          return p.distribution && std::any_of(p.distribution->departs.begin(), p.distribution->departs.end(),
                                                               [](const DepartBT& d) { return d.charge == TypeCharge::Prises; });
                        }});
    m.etapes.push_back({"Lis le calcul du départ",
                        "Ib est le courant d'emploi ; la protection In est au-dessus ; le câble supporte Iz, au-dessus de In ; la chute de tension reste sous "
                        "la limite ; le court-circuit minimal fait encore déclencher la protection.",
                        "Choisis le départ dans l'arbre : sa justification est en haut du panneau de droite, chaque grandeur avec son ⓘ.", "iz", DISTRIBUTION, {}});
    m.etapes.push_back({"Allonge la ligne à 60 m",
                        "Plus la ligne est longue, plus la tension chute et plus le court-circuit en bout est faible : la section grossit pour rester conforme.",
                        "Dans le panneau, groupe Canalisation : Longueur 60 m, puis Entrée. Regarde la section et la chute de tension changer.", "chute",
                        DISTRIBUTION, [](const ProjetElectrique& p) {
                          return p.distribution && std::any_of(p.distribution->departs.begin(), p.distribution->departs.end(),
                                                               [](const DepartBT& d) { return d.charge == TypeCharge::Prises && d.longueur >= 60 - 1e-9; });
                        }});
    m.etapes.push_back({"Ajoute un moteur",
                        "Un moteur appelle 5 à 8 fois son courant au démarrage : Prométhée vérifie la chute de tension à ce moment-là et choisit un disjoncteur moteur.",
                        "Ruban : Départ ▸ Moteur ; règle sa puissance dans le panneau.", "demarrage", DISTRIBUTION, [](const ProjetElectrique& p) {
                          return p.distribution && std::any_of(p.distribution->departs.begin(), p.distribution->departs.end(),
                                                               [](const DepartBT& d) { return d.charge == TypeCharge::Moteur; });
                        }});
    m.etapes.push_back({"Dessine l'unifilaire",
                        "L'unifilaire montre l'installation d'un coup d'œil : un trait par circuit, la protection, le câble et les résultats du calcul.",
                        "Bouton « Dessiner l'unifilaire » au-dessus du tableau des résultats.", "unifilaire", DISTRIBUTION,
                        [](const ProjetElectrique& p) { return foliosOrigine(p, "distribution"); }});
    m.etapes.push_back({"Sors la note de calcul",
                        "La note de calcul justifie chaque choix, règle à l'appui : c'est ce qu'on remet au bureau de contrôle.",
                        "Onglet Distribution : bouton « Note de calcul… » (PDF, page web ou tableur).", "", DISTRIBUTION, {}});
    v.push_back(std::move(m));
  }
  // ----- Photovoltaïque -----
  {
    Parcours m;
    m.id = "photovoltaique";
    m.nom = "Un toit photovoltaïque";
    m.description = "Des modules au compteur : chaînes, onduleur, protections, câbles, parafoudres, production, unifilaire.";
    m.genre = "photovoltaique";
    m.etapes.push_back({"Choisis une taille", "Une maison de 3 à 9 kWc, un bâtiment, une grande toiture : l'installation type arrive avec un matériel courant.",
                        "Au centre, clique sur une carte (« Maison · 6 kWc » par exemple).", "kwc", PHOTOVOLTAIQUE,
                        [](const ProjetElectrique& p) { return p.photovoltaique.has_value(); }});
    m.etapes.push_back({"Comprends les chaînes",
                        "Les modules sont mis en série : leurs tensions s'additionnent. Par grand froid la tension monte : elle ne doit pas dépasser ce que "
                        "supporte l'onduleur. Par forte chaleur elle baisse : elle doit rester dans sa plage MPPT.",
                        "Regarde le calepinage dessiné au centre et la ligne « Tension à vide au froid ».", "uoc", PHOTOVOLTAIQUE, {}});
    m.etapes.push_back({"Indique ton site",
                        "La température minimale du lieu fixe la tension maximale des chaînes ; en montagne, il faut des chaînes plus courtes.",
                        "Panneau de droite, groupe Site : Température minimale (par exemple −20 °C), puis Entrée.", "", PHOTOVOLTAIQUE,
                        [](const ProjetElectrique& p) { return p.photovoltaique && p.photovoltaique->tMin < -10 - 1e-9; }});
    m.etapes.push_back({"Oriente le toit", "Plein sud incliné à 30° donne le plus ; à l'est ou à l'ouest, environ 10 % de moins ; à plat, 7 % de moins.",
                        "Groupe Production : Écart au sud (−90 est, +90 ouest) et Inclinaison.", "productible", PHOTOVOLTAIQUE, [](const ProjetElectrique& p) {
                          return p.photovoltaique && (std::abs(p.photovoltaique->orientation) > 1e-9 || std::abs(p.photovoltaique->inclinaison - 30) > 1e-9);
                        }});
    m.etapes.push_back({"Essaie un autre module",
                        "Un module plus puissant change le nombre de modules par chaîne et la répartition sur l'onduleur : tout se recalcule.",
                        "Ruban : Module ▸ choisis-en un autre (ou saisis ta fiche technique dans le groupe Module).", "chaine", PHOTOVOLTAIQUE,
                        [](const ProjetElectrique& p) { return p.photovoltaique && p.photovoltaique->module.nom.find("425 Wc") == std::string::npos; }});
    m.etapes.push_back({"Dessine l'unifilaire", "L'unifilaire de la Consuel : chaînes, sectionneur continu, onduleur, protection alternative, branchement.",
                        "Bouton « Dessiner l'unifilaire » au centre.", "", PHOTOVOLTAIQUE, [](const ProjetElectrique& p) { return foliosOrigine(p, "photovoltaique"); }});
    v.push_back(std::move(m));
  }
  // ----- Logement -----
  {
    Parcours m;
    m.id = "logement";
    m.nom = "Le tableau d'un logement";
    m.description = "NF C 15-100 : les pièces et les équipements donnent les circuits, les différentiels, l'unifilaire et le coffret.";
    m.genre = "logement";
    m.etapes.push_back({"Décris le logement",
                        "La norme fixe un minimum de prises et de points lumineux par pièce, et des circuits spécialisés (plaque, four, lave-linge…).",
                        "Bouton « Logement type… » pour remplir les pièces, puis ajuste-les ; coche les équipements.", "", LOGEMENT, {}});
    m.etapes.push_back({"Crée l'unifilaire et le coffret",
                        "Prométhée répartit les circuits sous les différentiels 30 mA (8 circuits au plus chacun), choisit sections et calibres, et implante le "
                        "coffret.",
                        "Bouton « Créer le schéma unifilaire et le coffret ».", "ddr", LOGEMENT,
                        [](const ProjetElectrique& p) { return p.logement.has_value() && !p.schema.composants.empty(); }});
    m.etapes.push_back({"Lis l'unifilaire",
                        "En tête, le disjoncteur de branchement ; dessous, les interrupteurs différentiels, puis les disjoncteurs de chaque circuit, avec leur "
                        "calibre et la section du câble.",
                        "Onglet Schéma : le folio unifilaire. Un clic sur un appareil montre sa fiche « C'est quoi ? ».", "agcp", SCHEMA, {}});
    m.etapes.push_back({"Vérifie", "Aucune erreur : le tableau respecte les règles que Prométhée sait contrôler.", "Ruban Schéma : Vérifier.", "", SCHEMA,
                        [](const ProjetElectrique& p) { return p.logement.has_value() && erreurs(p) == 0; }});
    v.push_back(std::move(m));
  }
  // ----- Grafcet -----
  {
    Parcours m;
    m.id = "grafcet";
    m.nom = "Mon premier grafcet";
    m.description = "Le cycle d'un chariot : étapes, transitions, actions ; puis la simulation, le folio et le programme de l'automate.";
    m.genre = "grafcet";
    auto unGrafcet = [](const ProjetElectrique& p, const std::function<bool(const Grafcet&)>& f) { return std::any_of(p.grafcets.begin(), p.grafcets.end(), f); };
    m.etapes.push_back({"Lis l'étape initiale",
                        "L'étape 0, au double carré, est active au démarrage : c'est le repos de la machine. Chaque étape décrit une situation, chaque "
                        "transition ce qu'on attend pour en sortir.",
                        "Elle est posée au centre. Clique dessus : sa fiche s'ouvre à droite.", "etape", GRAFCET, {}});
    m.etapes.push_back({"Ajoute l'étape 1 et sa transition", "L'étape 1 sera l'avance du chariot ; la transition entre 0 et 1 dira quand partir.",
                        "Ruban « Étape » (ou « Étape suivante » à droite).", "transition", GRAFCET,
                        [unGrafcet](const ProjetElectrique& p) { return unGrafcet(p, [](const Grafcet& g) { return g.etapes.size() >= 2 && !g.transitions.empty(); }); }});
    m.etapes.push_back({"Écris la réceptivité",
                        "Ce que la machine attend pour passer à l'étape 1 : le départ cycle appuyé ET le chariot à gauche, « dcy · a ».",
                        "Clique la transition, écris dans « Réceptivité » (les boutons · + / ↑ aident), puis Entrée.", "receptivite", GRAFCET,
                        [unGrafcet](const ProjetElectrique& p) {
                          return unGrafcet(p, [](const Grafcet& g) {
                            return std::any_of(g.transitions.begin(), g.transitions.end(), [](const TransitionGrafcet& t) {
                              const LectureReceptivite l = lireReceptivite(t.receptivite);
                              return l.expression && l.expression->type != Receptivite::Type::Vrai && l.expression->type != Receptivite::Type::Faux;
                            });
                          });
                        }});
    m.etapes.push_back({"Donne une action à l'étape 1", "KM1 : le contacteur du moteur d'avance, alimenté tant que l'étape 1 est active (action continue).",
                        "Clique l'étape 1, écris KM1 dans « Actions », puis « Appliquer les actions ».", "action", GRAFCET,
                        [unGrafcet](const ProjetElectrique& p) {
                          return unGrafcet(p, [](const Grafcet& g) {
                            return std::any_of(g.etapes.begin(), g.etapes.end(), [](const EtapeGrafcet& e) { return !e.initiale && !e.actions.empty(); });
                          });
                        }});
    m.etapes.push_back({"Ferme le cycle", "Un automatisme revient au repos : une transition ramène à l'étape initiale (le retour se dessine à gauche, flèche vers le haut).",
                        "Choisis la dernière étape, ruban « Retour » › vers l'étape 0, puis écris sa réceptivité.", "", GRAFCET,
                        [unGrafcet](const ProjetElectrique& p) {
                          return unGrafcet(p, [](const Grafcet& g) {
                            return std::any_of(g.transitions.begin(), g.transitions.end(), [&](const TransitionGrafcet& t) {
                              return std::any_of(t.aval.begin(), t.aval.end(), [&](int n) {
                                const EtapeGrafcet* e = g.etape(n);
                                return e && e->initiale && std::find(t.amont.begin(), t.amont.end(), n) == t.amont.end();
                              });
                            });
                          });
                        }});
    m.etapes.push_back({"Simule", "Le grafcet évolue selon les règles de la norme : coche les entrées, regarde le jeton passer d'une étape à l'autre et les sorties changer.",
                        "Ruban « Simuler » : coche dcy et a, l'étape 1 s'active et KM1 passe à 1.", "grafcet", GRAFCET, {}});
    m.etapes.push_back({"Dessine le folio", "Le grafcet normalisé rejoint les folios du dossier, tenu à jour à chaque modification.", "Ruban « Folio ».", "", GRAFCET,
                        [](const ProjetElectrique& p) {
                          return std::any_of(p.schema.feuilles.begin(), p.schema.feuilles.end(), [&](const ecad::Feuille& f) { return p.folio(f.uuid).type == TypeFolio::Grafcet; });
                        }});
    m.etapes.push_back({"Le programme de l'automate",
                        "Le même grafcet en texte structuré CEI 61131-3 : un bit par étape, les transitions, les sorties, prêt à coller dans le bloc fonctionnel de "
                        "ton automate.",
                        "Ruban « Automate » › Copier le texte structuré.", "st", GRAFCET, {}});
    v.push_back(std::move(m));
  }
  return v;
}

}  // namespace

const std::vector<Parcours>& parcoursElectriques() {
  static const std::vector<Parcours> v = construire();
  return v;
}

const Parcours* parcoursElectrique(const std::string& id) {
  for (const auto& p : parcoursElectriques())
    if (p.id == id) return &p;
  return nullptr;
}

}  // namespace prom::elec

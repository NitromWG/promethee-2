// Prométhée : registre des actions de l'atelier électronique (lot L1, tâche 1.8). Voir RegistreActions.hpp.
// Licence GPL-3.0-only.
#include "RegistreActions.hpp"

#include <algorithm>
#include <utility>

#include <QKeyEvent>
#include <QObject>
#include <QSettings>
#include <QVariant>

namespace {
QKeySequence k(QKeyCombination c) { return QKeySequence(c); }
QKeySequence k(Qt::Key c) { return QKeySequence(c); }

bool lettre(int c) { return c >= Qt::Key_A && c <= Qt::Key_Z; }
bool symbole(int c) { return c > Qt::Key_Space && c <= Qt::Key_AsciiTilde && !lettre(c); }

// Paires prévues : deux actions sur une touche, l'outil actif n'en acceptant qu'une selon son état
bool paireVoulue(const std::string& a, const std::string& b) {
  static const std::pair<const char*, const char*> paires[] = {{"selection.supprimer", "trace.retirerCoude"}};
  for (const auto& [x, y] : paires)
    if ((a == x && b == y) || (a == y && b == x)) return true;
  return false;
}
}  // namespace

QKeyCombination RegistreActions::normaliser(QKeyCombination c) {
  Qt::KeyboardModifiers m = c.keyboardModifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
  int touche = c.key();
  if (touche == Qt::Key_Enter) touche = Qt::Key_Return;
  if (symbole(touche)) m &= ~Qt::ShiftModifier;
  return QKeyCombination(m, static_cast<Qt::Key>(touche));
}

void marquerAction(QObject* porteur, std::string_view identifiant) {
  if (!porteur) return;
  QStringList l = porteur->property("actionsAtelier").toStringList();
  const QString id = QString::fromUtf8(identifiant.data(), static_cast<qsizetype>(identifiant.size()));
  if (!l.contains(id)) l << id;
  porteur->setProperty("actionsAtelier", l);
}

QStringList actionsMarquees(const QObject* porteur) { return porteur ? porteur->property("actionsAtelier").toStringList() : QStringList(); }

RegistreActions& RegistreActions::atelierElectronique() {
  static RegistreActions r;
  return r;
}

RegistreActions::RegistreActions() {
  const auto CTRL = Qt::ControlModifier, MAJ = Qt::ShiftModifier;
  auto ajouter = [this](std::string id, QString libelle, PorteeAction p, QKeySequence a, QKeySequence b = {}, bool horsCanevas = false, bool cachee = false) {
    ActionAtelier x;
    x.identifiant = std::move(id);
    x.libelle = std::move(libelle);
    x.portee = p;
    x.raccourcis = {std::move(a), std::move(b)};
    x.horsCanevas = horsCanevas;
    x.cachee = cachee;
    m_actions.push_back(std::move(x));
  };
  using P = PorteeAction;

  // Partout dans Prométhée (la fenêtre les tient ; listés pour F1)
  ajouter("projet.nouveau", QStringLiteral("Nouveau projet"), P::Partout, k(CTRL | Qt::Key_N));
  ajouter("projet.ouvrir", QStringLiteral("Ouvrir un projet"), P::Partout, k(CTRL | Qt::Key_O));
  ajouter("projet.enregistrer", QStringLiteral("Enregistrer, à la fin du geste en cours"), P::Partout, k(CTRL | Qt::Key_S));
  ajouter("projet.enregistrerSous", QStringLiteral("Enregistrer sous"), P::Partout, k(CTRL | MAJ | Qt::Key_S));
  ajouter("historique.annuler", QStringLiteral("Annuler le dernier pas"), P::Partout, k(CTRL | Qt::Key_Z));
  ajouter("historique.retablir", QStringLiteral("Rétablir"), P::Partout, k(CTRL | Qt::Key_Y), k(CTRL | MAJ | Qt::Key_Z));
  ajouter("palette", QStringLiteral("Palette de commandes"), P::Partout, k(CTRL | Qt::Key_K));
  ajouter("atelier.carte", QStringLiteral("Atelier Carte et boîtier"), P::Partout, k(CTRL | Qt::Key_1));
  ajouter("atelier.pieces", QStringLiteral("Atelier Pièces"), P::Partout, k(CTRL | Qt::Key_2));
  ajouter("atelier.electronique", QStringLiteral("Atelier électronique"), P::Partout, k(CTRL | Qt::Key_3));
  ajouter("atelier.electrique", QStringLiteral("Atelier électrique"), P::Partout, k(CTRL | Qt::Key_4));
  ajouter("onglet.nouveau", QStringLiteral("Nouvel onglet"), P::Partout, k(CTRL | Qt::Key_T));
  ajouter("onglet.fermer", QStringLiteral("Fermer l’onglet"), P::Partout, k(CTRL | Qt::Key_W));

  // Atelier : jamais gardées par un canevas (S22)
  ajouter("atelier.raccourcis", QStringLiteral("Liste des raccourcis de l’atelier et de l’outil"), P::Atelier, k(Qt::Key_F1));
  ajouter("atelier.synchroniser", QStringLiteral("Synchroniser maintenant le circuit et le schéma (panneau des écarts, tout appliqué)"), P::Atelier, k(Qt::Key_F8));

  // Communes au schéma et au circuit
  ajouter("commun.echap", QStringLiteral("Annuler le geste en cours, sinon revenir à l’état neutre"), P::Canevas, k(Qt::Key_Escape));
  ajouter("selection.supprimer", QStringLiteral("Supprimer la sélection"), P::Canevas, k(Qt::Key_Delete), k(Qt::Key_Backspace), true);
  ajouter("trace.retirerCoude", QStringLiteral("Retirer le dernier coude ou sommet du tracé"), P::Canevas, k(Qt::Key_Backspace));
  ajouter("trace.annuler", QStringLiteral("Retirer le tracé en cours, sans toucher à l’historique"), P::Canevas, k(CTRL | Qt::Key_Z));
  ajouter("trace.coude", QStringLiteral("Inverser le coude du fil ou de la piste"), P::Canevas, k(Qt::Key_Slash), k(Qt::Key_Space));
  ajouter("trace.mode", QStringLiteral("Mode du fil : équerre, 45°, libre"), P::Schema, k(MAJ | Qt::Key_Space));
  ajouter("trace.terminer", QStringLiteral("Terminer la piste ou fermer le contour"), P::Canevas, k(Qt::Key_Return), k(Qt::Key_End));
  ajouter("edition.copier", QStringLiteral("Copier"), P::Canevas, k(CTRL | Qt::Key_C));
  ajouter("edition.couper", QStringLiteral("Couper"), P::Canevas, k(CTRL | Qt::Key_X));
  ajouter("edition.coller", QStringLiteral("Coller sous la souris"), P::Canevas, k(CTRL | Qt::Key_V));
  ajouter("edition.dupliquer", QStringLiteral("Dupliquer"), P::Canevas, k(CTRL | Qt::Key_D));
  ajouter("selection.tout", QStringLiteral("Tout choisir"), P::Canevas, k(CTRL | Qt::Key_A));
  ajouter("objet.tourner", QStringLiteral("Tourner d’un quart de tour (la sélection, ou le fantôme à poser)"), P::Canevas, k(Qt::Key_R), {}, true);
  ajouter("objet.tournerInverse", QStringLiteral("Tourner d’un quart de tour dans l’autre sens"), P::Canevas, k(MAJ | Qt::Key_R), {}, true);
  ajouter("selection.gauche", QStringLiteral("Déplacer d’un pas de grille vers la gauche"), P::Canevas, k(Qt::Key_Left));
  ajouter("selection.droite", QStringLiteral("Déplacer d’un pas de grille vers la droite"), P::Canevas, k(Qt::Key_Right));
  ajouter("selection.haut", QStringLiteral("Déplacer d’un pas de grille vers le haut"), P::Canevas, k(Qt::Key_Up));
  ajouter("selection.bas", QStringLiteral("Déplacer d’un pas de grille vers le bas"), P::Canevas, k(Qt::Key_Down));
  ajouter("selection.gauche10", QStringLiteral("Déplacer de dix pas vers la gauche"), P::Canevas, k(MAJ | Qt::Key_Left));
  ajouter("selection.droite10", QStringLiteral("Déplacer de dix pas vers la droite"), P::Canevas, k(MAJ | Qt::Key_Right));
  ajouter("selection.haut10", QStringLiteral("Déplacer de dix pas vers le haut"), P::Canevas, k(MAJ | Qt::Key_Up));
  ajouter("selection.bas10", QStringLiteral("Déplacer de dix pas vers le bas"), P::Canevas, k(MAJ | Qt::Key_Down));
  ajouter("commun.inspecteur", QStringLiteral("Inspecteur, curseur dans son premier champ"), P::Canevas, k(Qt::Key_E));
  ajouter("objet.modifierTexte", QStringLiteral("Modifier sur place le texte de l’objet choisi (valeur, étiquette, texte)"), P::Schema, k(Qt::Key_F2));
  ajouter("vue.toutVoir", QStringLiteral("Tout voir"), P::Canevas, k(Qt::Key_Home));
  ajouter("vue.zoomPlus", QStringLiteral("Zoomer"), P::Canevas, k(Qt::Key_Plus));
  ajouter("vue.zoomMoins", QStringLiteral("Dézoomer"), P::Canevas, k(Qt::Key_Minus));
  ajouter("vue.zoomSelection", QStringLiteral("Zoom sur la sélection"), P::Canevas, k(Qt::Key_Z));
  ajouter("vue.vueEnsemble", QStringLiteral("Vue d’ensemble de la feuille, en bas à droite"), P::Schema, {});

  // Schéma
  ajouter("schema.choisirComposant", QStringLiteral("Choisir un composant dans la bibliothèque"), P::Schema, k(Qt::Key_A));
  ajouter("schema.fil", QStringLiteral("Fil"), P::Schema, k(Qt::Key_W));
  ajouter("schema.etiquette", QStringLiteral("Étiquette locale"), P::Schema, k(Qt::Key_L));
  ajouter("schema.etiquetteGlobale", QStringLiteral("Étiquette globale"), P::Schema, k(MAJ | Qt::Key_L));
  ajouter("schema.alimentation", QStringLiteral("Alimentation"), P::Schema, k(Qt::Key_P));
  ajouter("schema.nonConnexion", QStringLiteral("Marque de non-connexion"), P::Schema, k(Qt::Key_Q));
  ajouter("schema.jonction", QStringLiteral("Jonction"), P::Schema, k(Qt::Key_J));
  ajouter("schema.texte", QStringLiteral("Texte"), P::Schema, k(Qt::Key_T));
  ajouter("schema.repeter", QStringLiteral("Reposer le dernier composant au même pas, repère suivant"), P::Schema, k(Qt::Key_Insert));
  ajouter("schema.poserCopie", QStringLiteral("Poser une copie du composant choisi (même valeur, même orientation)"), P::Schema, {});
  ajouter("objet.miroirX", QStringLiteral("Retourner de gauche à droite (aussi le fantôme)"), P::Schema, k(Qt::Key_X));
  ajouter("objet.miroirY", QStringLiteral("Retourner de haut en bas (aussi le fantôme)"), P::Schema, k(Qt::Key_Y));
  ajouter("vue.ajusterDessin", QStringLiteral("Ajuster la vue au dessin du folio"), P::Schema, k(MAJ | Qt::Key_Home));
  ajouter("schema.glisserFils", QStringLiteral("Déplacer la sélection, ses fils suivent à l’équerre (pendant un glisser : fils élastiques)"), P::Schema, k(Qt::Key_G));
  ajouter("schema.deplacerSeul", QStringLiteral("Déplacer la sélection en décrochant ses fils (pendant un glisser : fils décrochés)"), P::Schema, k(Qt::Key_M));
  // F ne recadre plus (C13) : pendant une version, il dit une fois où est passé « Tout voir »
  ajouter("schema.ancienF", QStringLiteral("Ancienne touche de recadrage"), P::Schema, k(Qt::Key_F), {}, false, true);

  // Circuit
  ajouter("circuit.router", QStringLiteral("Router"), P::Circuit, k(Qt::Key_X));
  ajouter("circuit.via", QStringLiteral("Via ; pendant le routage, via et changement de couche"), P::Circuit, k(Qt::Key_V));
  ajouter("circuit.coucheDessus", QStringLiteral("Couche active : dessus ; pendant le routage, via vers le dessus"), P::Circuit, k(Qt::Key_PageUp));
  ajouter("circuit.coucheDessous", QStringLiteral("Couche active : dessous ; pendant le routage, via vers le dessous"), P::Circuit, k(Qt::Key_PageDown));
  ajouter("circuit.changerFace", QStringLiteral("Changer de face"), P::Circuit, k(Qt::Key_F));

  // Commandes de l'atelier, sans touche, que le ruban porte (1.20) : le fichier du ruban ne cite que des
  // identifiants du registre
  ajouter("composant.creer", QStringLiteral("Créer un composant"), P::Atelier, {});
  ajouter("schema.alimentation.gnd", QStringLiteral("Alimentation GND"), P::Schema, {});
  ajouter("schema.alimentation.5v", QStringLiteral("Alimentation +5V"), P::Schema, {});
  ajouter("schema.alimentation.3v3", QStringLiteral("Alimentation +3V3"), P::Schema, {});
  ajouter("schema.alimentation.12v", QStringLiteral("Alimentation +12V"), P::Schema, {});
  ajouter("schema.alimentation.vcc", QStringLiteral("Alimentation VCC"), P::Schema, {});
  ajouter("schema.drapeauAlimentation", QStringLiteral("Drapeau d’alimentation"), P::Schema, {});
  ajouter("schema.champs", QStringLiteral("Tableau des champs"), P::Atelier, {});
  ajouter("vue.mosaique", QStringLiteral("Schéma et circuit côte à côte, ou une vue à la fois"), P::Atelier, {});
  ajouter("vue.tableauObjets", QStringLiteral("Tableau de tous les objets, modifiable comme un tableur"), P::Atelier, {});
  ajouter("schema.annoter", QStringLiteral("Annoter les nouveaux composants"), P::Atelier, {});
  ajouter("schema.renumeroter", QStringLiteral("Tout renuméroter"), P::Atelier, {});
  ajouter("schema.erc", QStringLiteral("Vérifier : vérification électrique (ERC) et règles de dessin (DRC) complètes"), P::Atelier, k(Qt::Key_F7));
  ajouter("verif.suivant", QStringLiteral("Problème suivant de la vérification"), P::Atelier, k(Qt::Key_F4));
  ajouter("edition.chercher", QStringLiteral("Chercher et remplacer dans toutes les feuilles"), P::Atelier, k(CTRL | Qt::Key_F));
  ajouter("composant.empreinte", QStringLiteral("Changer l’empreinte du composant choisi"), P::Schema, {});
  ajouter("schema.relier", QStringLiteral("Relier la broche à une autre, par un fil qui contourne les symboles"), P::Schema, {});
  ajouter("schema.relierListe", QStringLiteral("Relier une liste de paires de broches (U1:PA9 → J2:3)"), P::Schema, {});
  ajouter("outil.reglages", QStringLiteral("Réglages de l’outil avant la pose (nom et pas d’une étiquette)"), P::Schema, k(Qt::Key_Tab));
  ajouter("edition.collerMatrice", QStringLiteral("Coller en matrice : lignes, colonnes, pas, ordre de numérotation"), P::Schema, {});
  ajouter("edition.collageSpecial", QStringLiteral("Collage spécial : repères gardés, libres ou effacés"), P::Schema, {});
  ajouter("edition.copierTexte", QStringLiteral("Copier comme texte : repères, valeurs, empreintes, réseaux"), P::Schema, {});
  ajouter("vue.pleinEcran", QStringLiteral("Plein écran"), P::Atelier, k(Qt::Key_F11));
  ajouter("vue.consultation", QStringLiteral("Mode consultation : lire et parcourir sans rien modifier"), P::Atelier, {});
  ajouter("vue.cadrageEnregistrer", QStringLiteral("Enregistrer ce cadrage sous un nom"), P::Atelier, {});
  ajouter("vue.etalonner", QStringLiteral("Étalonner l’écran à la règle, pour le zoom à la taille réelle"), P::Atelier, {});
  ajouter("selection.relieSuivant", QStringLiteral("L’objet relié suivant : le lecteur d’écran le décrit"), P::Schema, k(Qt::AltModifier | Qt::Key_Right));
  ajouter("atelier.mosaique", QStringLiteral("Mosaïque des feuilles du projet"), P::Atelier, {});
  ajouter("selection.inverser", QStringLiteral("Inverser la sélection : ce qui n’est pas choisi sur la feuille"), P::Schema, {});
  ajouter("selection.etendre", QStringLiteral("Étendre la sélection d’un palier de connectivité"), P::Schema, {});
  ajouter("selection.reduire", QStringLiteral("Réduire la sélection au palier d’avant"), P::Schema, {});
  ajouter("selection.semblables", QStringLiteral("Choisir les semblables du composant choisi"), P::Schema, {});
  ajouter("selection.memoires", QStringLiteral("Mémoires de sélection"), P::Atelier, {});
  ajouter("schema.gomme", QStringLiteral("Gomme : chaque clic supprime l’objet visé, jusqu’à « Terminer »"), P::Schema, {});
  ajouter("schema.decalerCroisements", QStringLiteral("Décaler les croisements à quatre branches en deux T"), P::Schema, {});
  ajouter("schema.terminerBroches", QStringLiteral("Terminer les broches libres des composants choisis par une étiquette à leur nom"), P::Schema, {});
  ajouter("schema.replacerChamps", QStringLiteral("Replacer le repère et la valeur des symboles choisis"), P::Schema, {});
  ajouter("schema.echanger", QStringLiteral("Échanger les places de deux symboles choisis"), P::Schema, {});
  ajouter("atelier.apprendre", QStringLiteral("Apprendre : le parcours de la première carte et le lexique"), P::Atelier, {});
  ajouter("schema.creerCircuit", QStringLiteral("Créer le circuit imprimé d’un schéma seul"), P::Atelier, {});
  ajouter("schema.feuilleNouvelle", QStringLiteral("Nouvelle feuille"), P::Atelier, {});
  ajouter("schema.feuilleSuivante", QStringLiteral("Feuille suivante"), P::Atelier, {});
  ajouter("schema.feuillePrecedente", QStringLiteral("Feuille précédente"), P::Atelier, {});
  ajouter("schema.feuilleSupprimer", QStringLiteral("Supprimer la feuille"), P::Atelier, {});
  ajouter("circuit.routerAuto", QStringLiteral("Router automatiquement"), P::Atelier, {});
  ajouter("circuit.zone", QStringLiteral("Zone de cuivre"), P::Circuit, {});
  ajouter("circuit.contour", QStringLiteral("Contour de la carte"), P::Circuit, {});
  ajouter("circuit.decoupe", QStringLiteral("Découpe dans la carte"), P::Circuit, {});
  ajouter("circuit.texte", QStringLiteral("Texte sur la carte"), P::Circuit, {});
  ajouter("circuit.mesurer", QStringLiteral("Mesurer"), P::Circuit, {});
  ajouter("circuit.drc", QStringLiteral("Vérification des règles (DRC)"), P::Atelier, {});
  ajouter("circuit.coucheActive", QStringLiteral("Couche active et couches visibles"), P::Atelier, {});
  ajouter("circuit.grilleUnite", QStringLiteral("Grille et unité"), P::Atelier, {});
  ajouter("vue3d.recadrer", QStringLiteral("Recadrer la vue 3D"), P::Atelier, {});
  ajouter("fabrication.valider", QStringLiteral("Valider la conception"), P::Atelier, {});
  ajouter("fabrication.publier", QStringLiteral("Publier pour la fabrication"), P::Atelier, {});
  ajouter("fabrication.dossier", QStringLiteral("Dossier de fabrication (brouillon)"), P::Atelier, {});
  ajouter("fabrication.dessinSchema", QStringLiteral("Dessin du schéma (PDF)"), P::Atelier, {});
  ajouter("fabrication.typon", QStringLiteral("Typon de gravure (PDF, SVG)"), P::Atelier, {});
  ajouter("fabrication.planMontage", QStringLiteral("Plan de montage (PDF)"), P::Atelier, {});
  ajouter("fabrication.nomenclature", QStringLiteral("Nomenclature (CSV)"), P::Atelier, {});
  ajouter("fabrication.reseaux", QStringLiteral("Liste des réseaux (netlist)"), P::Atelier, {});
  ajouter("fabrication.kicad", QStringLiteral("Exporter vers KiCad (.kicad_pcb)"), P::Atelier, {});
  ajouter("fabrication.netlistKicad", QStringLiteral("Liste de réseaux KiCad (.net)"), P::Atelier, {});
  ajouter("fabrication.ipc", QStringLiteral("Liste IPC-D-356 (test électrique)"), P::Atelier, {});
  ajouter("fabrication.boitier", QStringLiteral("Concevoir le boîtier de cette carte"), P::Atelier, {});
  // Touches qui ouvrent un menu au curseur (ERG-03)
  ajouter("schema.menuPoser", QStringLiteral("Menu Poser, au curseur"), P::Schema, k(MAJ | Qt::Key_A));
  ajouter("circuit.menuPoser", QStringLiteral("Menu Poser, au curseur"), P::Circuit, k(MAJ | Qt::Key_A));

  // Présentation au ruban : nom court, icône, bulle
  struct Presentation {
    const char* id;
    const char* nom;
    const char* icone;
    const char* bulle;
  };
  static constexpr Presentation presentations[] = {
      {"schema.choisirComposant", "Placer un composant", "composants", "bibliothèque de symboles, avec aperçu"},
      {"composant.creer", "Créer un composant…", "creer-composant", "son symbole est dessiné depuis la liste de ses broches"},
      {"schema.fil", "Fil", "fil", "en équerre, d’une broche à l’autre"},
      {"schema.etiquette", "Étiquette", "etiquette", "relie les fils du même nom sur la feuille"},
      {"schema.etiquetteGlobale", "Étiquette globale", "etiquette", "relie les fils du même nom sur toutes les feuilles"},
      {"schema.alimentation", "Alimentation", "alimentation", "symbole d’alimentation"},
      {"schema.alimentation.gnd", "GND", "masse-terre", "symbole d’alimentation"},
      {"schema.alimentation.5v", "+5V", "alimentation", "symbole d’alimentation"},
      {"schema.alimentation.3v3", "+3V3", "alimentation", "symbole d’alimentation"},
      {"schema.alimentation.12v", "+12V", "alimentation", "symbole d’alimentation"},
      {"schema.alimentation.vcc", "VCC", "alimentation", "symbole d’alimentation"},
      {"schema.drapeauAlimentation", "Drapeau d’alimentation", "drapeau", "déclare un réseau alimenté par l’extérieur"},
      {"schema.jonction", "Jonction", "point", "relie deux fils qui se croisent"},
      {"schema.nonConnexion", "Non connecté", "annuler", "broche laissée libre volontairement"},
      {"schema.texte", "Texte", "texte", "note sur la feuille"},
      {"schema.gomme", "Gomme", "gomme", "chaque clic supprime l’objet visé, jusqu’à « Terminer »"},
      {"schema.repeter", "Répéter la pose", "repetition-lineaire", "le dernier composant, au même pas, repère suivant"},
      {"schema.poserCopie", "Poser une copie", "poser-copie", "même article, même valeur, même orientation"},
      {"objet.tourner", "Tourner", "pivoter", "les fils et les pistes accrochés suivent ; pendant une pose, le fantôme tourne"},
      {"objet.tournerInverse", "Tourner dans l’autre sens", "pivoter", ""},
      {"objet.miroirX", "Miroir gauche-droite", "symetrie", ""},
      {"objet.miroirY", "Miroir haut-bas", "symetrie", ""},
      {"selection.supprimer", "Supprimer", "corbeille", ""},
      {"selection.tout", "Tout choisir", "selection", "Maj + clic ajoute, un cadre choisit plusieurs objets"},
      {"edition.copier", "Copier", "copier", ""},
      {"edition.coller", "Coller", "coller", "sous la souris"},
      {"edition.dupliquer", "Dupliquer", "dupliquer", ""},
      {"edition.couper", "Couper", "couper", ""},
      {"vue.toutVoir", "Tout voir", "recadrer", ""},
      {"vue.ajusterDessin", "Ajuster au dessin du folio", "plan", ""},
      {"vue.zoomPlus", "Zoomer", "zoom-plus", "ou la molette"},
      {"vue.zoomMoins", "Dézoomer", "zoom-moins", "ou la molette"},
      {"vue.zoomSelection", "Zoom sur la sélection", "zoom-plus", ""},
      {"commun.inspecteur", "Inspecteur", "proprietes", "le curseur dans son premier champ"},
      {"atelier.raccourcis", "Raccourcis", "aide", "ceux de l’atelier et de l’outil en cours"},
      {"atelier.synchroniser", "Synchroniser maintenant", "convertir", "le panneau des écarts s’ouvre, et tout écart avec le schéma s’applique, en un pas"},
      {"schema.champs", "Tableau des champs…", "proprietes", "valeurs, empreintes, fabricants et références de tous les composants, en une vue"},
      {"schema.annoter", "Annoter les nouveaux", "annoter", "R?, C?… reçoivent leur numéro"},
      {"schema.renumeroter", "Tout renuméroter", "annoter", "dans l’ordre de lecture des feuilles"},
      {"schema.erc", "Vérifier", "erc", "ERC et DRC complètes : broches libres, sorties en conflit, alimentation manquante…"},
      {"schema.creerCircuit", "Circuit", "pcb", "créer le circuit imprimé de ce schéma : les empreintes arrivent, prêtes à placer"},
      {"schema.feuilleNouvelle", "Nouvelle feuille", "plan", ""},
      {"schema.feuilleSuivante", "Feuille suivante", "plan", ""},
      {"schema.feuillePrecedente", "Feuille précédente", "plan", ""},
      {"schema.feuilleSupprimer", "Supprimer la feuille…", "corbeille", ""},
      {"circuit.router", "Router une piste", "piste", "à 45°, isolement vérifié en direct"},
      {"circuit.via", "Poser une via", "via", "pendant le routage, le bouton Via du bandeau"},
      {"circuit.routerAuto", "Router automatiquement", "piste", "les liaisons qui passent sans détour, jamais au mépris de l’isolement ; le reste attend au chevelu"},
      {"circuit.zone", "Zone de cuivre", "zone", "plan de masse : clique les sommets, double-clic pour fermer ; le réseau est demandé à la fermeture"},
      {"circuit.contour", "Contour de la carte", "pcb", "clique les sommets, double-clic pour fermer : remplace le rectangle"},
      {"circuit.decoupe", "Découpe dans la carte", "coupe", "fenêtre fraisée de part en part, à l’intérieur du contour"},
      {"circuit.texte", "Texte sur la carte", "texte", "sérigraphie : nom, révision, repères de connecteur, sur la face de la couche active"},
      {"circuit.mesurer", "Mesurer", "cote", ""},
      {"circuit.changerFace", "Retourner", "symetrie", "passe le composant sur l’autre face"},
      {"circuit.drc", "Vérification des règles (DRC)", "drc", "isolements, bords, perçages, liaisons non routées…"},
      {"vue3d.recadrer", "Recadrer", "recadrer", ""},
      {"fabrication.valider", "Valider la conception", "valider", "intégrité, bibliothèques, ERC, DRC, nomenclature, fabrication, documentation"},
      {"fabrication.publier", "Publier pour la fabrication…", "publier", "dossier complet, révision verrouillée, empreinte de la conception"},
      {"fabrication.dossier", "Dossier de fabrication (brouillon)…", "exporter", "Gerber, perçage, placement, nomenclature, sans publier"},
      {"fabrication.dessinSchema", "Dessin du schéma (PDF)…", "plan", "toutes les feuilles, avec leur cartouche"},
      {"fabrication.typon", "Typon de gravure (PDF, SVG)…", "pcb", "une couche de cuivre à l’échelle 1, miroir, négatif, perçages réduits : insoleuse ou transfert de toner"},
      {"fabrication.planMontage", "Plan de montage (PDF)…", "pcb", "faces dessus et dessous à l’échelle, repères, cotes de la carte"},
      {"fabrication.nomenclature", "Nomenclature (CSV)…", "composants", "quantités, références, valeurs, fabricants"},
      {"fabrication.reseaux", "Liste des réseaux (netlist)…", "fil", "chaque réseau et ses broches, en texte"},
      {"fabrication.kicad", "Exporter vers KiCad (.kicad_pcb)…", "kicad", "empreintes, pistes, vias, zones, réseaux, contour : la carte se rouvre dans KiCad 7, 8 ou 9"},
      {"fabrication.netlistKicad", "Liste de réseaux KiCad (.net)…", "fil", "composants et réseaux pour KiCad ou un autre outil"},
      {"fabrication.ipc", "Liste IPC-D-356 (test électrique)…", "ipc", "pour le test sous pointes des cartes nues chez le fabricant"},
      {"fabrication.boitier", "Concevoir le boîtier de cette carte", "metier-mixte", "la carte du projet suit ce circuit : contour, trous, hauteur des composants"},
  };
  for (const Presentation& p : presentations)
    for (auto& a : m_actions)
      if (a.identifiant == p.id) {
        a.nom = QString::fromUtf8(p.nom);
        a.icone = QString::fromUtf8(p.icone);
        a.bulle = QString::fromUtf8(p.bulle);
      }
  for (auto& a : m_actions) {
    if (a.identifiant == "circuit.coucheActive" || a.identifiant == "circuit.grilleUnite") a.controle = true;
    if (a.identifiant == "schema.jonction" || a.identifiant == "schema.nonConnexion" || a.identifiant == "circuit.via") a.auCurseur = true;
    if (a.identifiant == "trace.coude" || a.identifiant == "trace.mode") a.bascule = true;
    if (a.identifiant == "schema.menuPoser")
      a.menu = {"schema.fil", "schema.etiquette", "schema.etiquetteGlobale", "schema.alimentation", "schema.jonction", "schema.nonConnexion", "schema.texte"};
    if (a.identifiant == "circuit.menuPoser") a.menu = {"circuit.router", "circuit.via", "circuit.zone", "circuit.contour", "circuit.decoupe", "circuit.texte"};
  }
  // « P puis V » : poser une via au circuit, comme dans les autres logiciels de circuit imprimé (ERG-03)
  for (auto& a : m_actions)
    if (a.identifiant == "circuit.via") a.raccourcis[1] = QKeySequence(Qt::Key_P, Qt::Key_V);

  // Ce qui se fait à la souris par un geste plutôt que par un bouton (ERG-17)
  const std::pair<const char*, QString> gestes[] = {
      {"commun.echap", QStringLiteral("clic droit › Terminer ; « Terminer » ou « Annuler » du bandeau ; clic dans le vide")},
      {"commun.inspecteur", QStringLiteral("clic dans l’inspecteur")},
      {"vue.zoomSelection", QStringLiteral("menu à marques, vers le haut")},
      {"schema.menuPoser", QStringLiteral("menu Ajouter de la fenêtre")},
      {"circuit.menuPoser", QStringLiteral("menu Ajouter de la fenêtre")},
      {"schema.glisserFils", QStringLiteral("glisser la sélection : ses fils suivent")},
      {"objet.modifierTexte", QStringLiteral("double-clic sur le texte ; clic droit › Modifier")},
      {"composant.empreinte", QStringLiteral("clic droit sur le symbole › Changer d’empreinte ; liste Empreinte de l’inspecteur")},
      {"schema.relier", QStringLiteral("clic droit sur une broche › Relier à…")},
      {"schema.terminerBroches", QStringLiteral("clic droit sur un symbole › Terminer les broches libres")},
      {"schema.replacerChamps", QStringLiteral("clic droit sur un symbole › Replacer les champs")},
      {"schema.echanger", QStringLiteral("clic droit sur deux symboles choisis › Échanger leurs places")},
      {"schema.deplacerSeul", QStringLiteral("bouton « Fils » du bandeau pendant un glisser")},
      {"selection.gauche", QStringLiteral("glisser la sélection")},
      {"selection.droite", QStringLiteral("glisser la sélection")},
      {"selection.haut", QStringLiteral("glisser la sélection")},
      {"selection.bas", QStringLiteral("glisser la sélection")},
      {"selection.gauche10", QStringLiteral("glisser la sélection")},
      {"selection.droite10", QStringLiteral("glisser la sélection")},
      {"selection.haut10", QStringLiteral("glisser la sélection")},
      {"selection.bas10", QStringLiteral("glisser la sélection")},
      {"atelier.carte", QStringLiteral("la carte, dans l’arbre du projet")},
      {"atelier.pieces", QStringLiteral("une pièce, dans l’arbre du projet")},
      {"atelier.electronique", QStringLiteral("une carte électronique, dans l’arbre du projet")},
      {"atelier.electrique", QStringLiteral("un projet électrique, dans l’arbre du projet")},
      {"onglet.nouveau", QStringLiteral("« + » de la barre des onglets")},
      {"onglet.fermer", QStringLiteral("croix de l’onglet")},
  };
  for (const auto& [id, geste] : gestes)
    for (auto& a : m_actions)
      if (a.identifiant == id) a.souris = geste;
  for (auto& a : m_actions) a.raccourcisOrigine = a.raccourcis;
  relireRaccourcis();
}

void RegistreActions::relireRaccourcis() {
  QSettings s;
  for (auto& a : m_actions) {
    a.raccourcis = a.raccourcisOrigine;
    const QString cle = QStringLiteral("raccourcis/") + QString::fromStdString(a.identifiant);
    if (!s.contains(cle)) continue;
    const QStringList t = s.value(cle).toStringList();
    for (std::size_t i = 0; i < a.raccourcis.size(); ++i)
      a.raccourcis[i] = i < static_cast<std::size_t>(t.size()) ? QKeySequence::fromString(t[static_cast<qsizetype>(i)], QKeySequence::PortableText) : QKeySequence();
  }
}

const ActionAtelier* RegistreActions::action(std::string_view identifiant) const {
  for (const auto& a : m_actions)
    if (a.identifiant == identifiant) return &a;
  return nullptr;
}

std::vector<const ActionAtelier*> RegistreActions::suites(PorteeAction vue, QKeyCombination premiere) const {
  std::vector<const ActionAtelier*> r;
  const QKeyCombination f = normaliser(premiere);
  for (const auto& a : m_actions) {
    if (a.portee != PorteeAction::Canevas && a.portee != vue) continue;
    for (const QKeySequence& s : a.raccourcis)
      if (s.count() == 2 && normaliser(s[0]) == f) {
        r.push_back(&a);
        break;
      }
  }
  return r;
}

std::vector<const ActionAtelier*> RegistreActions::pourSuite(PorteeAction vue, QKeyCombination premiere, QKeyCombination seconde) const {
  std::vector<const ActionAtelier*> r;
  const QKeyCombination f = normaliser(premiere), g = normaliser(seconde);
  for (const auto& a : m_actions) {
    if (a.portee != PorteeAction::Canevas && a.portee != vue) continue;
    for (const QKeySequence& s : a.raccourcis)
      if (s.count() == 2 && normaliser(s[0]) == f && normaliser(s[1]) == g) {
        r.push_back(&a);
        break;
      }
  }
  return r;
}

bool RegistreActions::ajouterAction(ActionAtelier a) {
  if (action(a.identifiant)) return false;
  a.raccourcisOrigine = a.raccourcis;
  m_actions.push_back(std::move(a));
  return true;
}

PremierAppui premierAppui() { return static_cast<PremierAppui>(std::clamp(QSettings().value(QStringLiteral("clavier/premierAppui"), 0).toInt(), 0, 1)); }
void setPremierAppui(PremierAppui p) { QSettings().setValue(QStringLiteral("clavier/premierAppui"), static_cast<int>(p)); }

std::vector<const ActionAtelier*> RegistreActions::pour(PorteeAction vue, const QKeyEvent& e) const {
  std::vector<const ActionAtelier*> r;
  if (e.key() == 0 || e.key() == Qt::Key_unknown) return r;
  const QKeyCombination frappe = normaliser(e.keyCombination());
  for (const auto& a : m_actions) {
    if (a.portee != PorteeAction::Canevas && a.portee != vue) continue;
    for (const QKeySequence& s : a.raccourcis)
      if (s.count() == 1 && normaliser(s[0]) == frappe) {
        r.push_back(&a);
        break;
      }
  }
  return r;
}

bool RegistreActions::setRaccourcis(std::string_view identifiant, const QKeySequence& premier, const QKeySequence& second) {
  for (auto& a : m_actions)
    if (a.identifiant == identifiant) {
      a.raccourcis = {premier, second};
      QSettings s;
      const QString cle = QStringLiteral("raccourcis/") + QString::fromStdString(a.identifiant);
      if (a.raccourcis == a.raccourcisOrigine) s.remove(cle);
      else s.setValue(cle, QStringList{premier.toString(QKeySequence::PortableText), second.toString(QKeySequence::PortableText)});
      return true;
    }
  return false;
}

QStringList RegistreActions::conflits() const {
  QStringList r;
  for (std::size_t i = 0; i < m_actions.size(); ++i)
    for (std::size_t j = i + 1; j < m_actions.size(); ++j) {
      const ActionAtelier &a = m_actions[i], &b = m_actions[j];
      // Même vue : même portée, ou l'une commune aux canevas et l'autre propre à une vue ; Partout et Atelier sont
      // comparées entre elles et avec tout
      const bool canevasA = a.portee >= PorteeAction::Canevas, canevasB = b.portee >= PorteeAction::Canevas;
      const bool memeVue = a.portee == b.portee || (canevasA && canevasB && (a.portee == PorteeAction::Canevas || b.portee == PorteeAction::Canevas)) ||
                           (!canevasA && !canevasB);
      if (!memeVue || paireVoulue(a.identifiant, b.identifiant)) continue;
      for (const QKeySequence& x : a.raccourcis)
        for (const QKeySequence& y : b.raccourcis) {
          if (x.isEmpty() || y.isEmpty()) continue;
          bool memes = x.count() == y.count();
          for (int k = 0; memes && k < x.count(); ++k) memes = normaliser(x[k]) == normaliser(y[k]);
          if (memes) r << QStringLiteral("%1 : %2 et %3").arg(nomTouche(x), QString::fromStdString(a.identifiant), QString::fromStdString(b.identifiant));
          // Une suite ne commence pas par une touche qui agit seule dans la même vue (ERG-03)
          const QKeySequence& suite = x.count() == 2 ? x : y;
          const QKeySequence& seule = x.count() == 2 ? y : x;
          if (suite.count() == 2 && seule.count() == 1 && normaliser(suite[0]) == normaliser(seule[0]))
            r << QStringLiteral("%1 agit déjà seule : la suite %2 a besoin d’une autre première touche").arg(nomTouche(seule), nomTouche(suite));
        }
    }
  return r;
}

QString RegistreActions::nomTouche(const QKeySequence& s) {
  if (s.isEmpty()) return {};
  // Suite de deux touches : « P puis V »
  if (s.count() == 2) return nomTouche(QKeySequence(s[0])) + QStringLiteral(" puis ") + nomTouche(QKeySequence(s[1]));
  const QKeyCombination c = s[0];
  QString r;
  const auto m = c.keyboardModifiers();
  if (m & Qt::ControlModifier) r += QStringLiteral("Ctrl+");
  if (m & Qt::AltModifier) r += QStringLiteral("Alt+");
  if (m & Qt::MetaModifier) r += QStringLiteral("Méta+");
  if (m & Qt::ShiftModifier) r += QStringLiteral("Maj+");
  switch (c.key()) {
    case Qt::Key_Escape: return r + QStringLiteral("Échap");
    case Qt::Key_Delete: return r + QStringLiteral("Suppr");
    case Qt::Key_Backspace: return r + QStringLiteral("Retour arrière");
    case Qt::Key_Return:
    case Qt::Key_Enter: return r + QStringLiteral("Entrée");
    case Qt::Key_End: return r + QStringLiteral("Fin");
    case Qt::Key_Home: return r + QStringLiteral("Origine");
    case Qt::Key_PageUp: return r + QStringLiteral("Page préc.");
    case Qt::Key_PageDown: return r + QStringLiteral("Page suiv.");
    case Qt::Key_Left: return r + QStringLiteral("←");
    case Qt::Key_Right: return r + QStringLiteral("→");
    case Qt::Key_Up: return r + QStringLiteral("↑");
    case Qt::Key_Down: return r + QStringLiteral("↓");
    case Qt::Key_Space: return r + QStringLiteral("Espace");
    case Qt::Key_Insert: return r + QStringLiteral("Inser");
    case Qt::Key_Tab: return r + QStringLiteral("Tab");
    case Qt::Key_Minus: return r + QStringLiteral("−");
    default: return r + QKeySequence(c.key()).toString(QKeySequence::PortableText);
  }
}

QString RegistreActions::touches(std::string_view identifiant) const {
  const ActionAtelier* a = action(identifiant);
  if (!a) return {};
  QStringList r;
  for (const QKeySequence& s : a->raccourcis)
    if (!s.isEmpty()) r << nomTouche(s);
  return r.join(QStringLiteral(", "));
}

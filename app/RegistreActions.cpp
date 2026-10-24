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
  ajouter("trace.largeur", QStringLiteral("Pendant le tracé : largeur suivante (celle de la classe, puis les courantes)"), P::Circuit, k(Qt::Key_W));
  ajouter("trace.largeurPrecedente", QStringLiteral("Pendant le tracé : largeur précédente"), P::Circuit, k(MAJ | Qt::Key_W));
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
  ajouter("bibliotheque.articles", QStringLiteral("Choisir un article réel dans la fenêtre de choix"), P::Atelier, {});
  ajouter("bibliotheque.standardiser", QStringLiteral("Standardiser les composants sans article réel"), P::Atelier, {});
  ajouter("bibliotheque.misesAJour", QStringLiteral("Mettre à jour les composants depuis leurs articles"), P::Atelier, {});
  ajouter("bibliotheque.gerer", QStringLiteral("Bibliothèque d'articles en tableau : séries E, import, renommage, copie"), P::Atelier, {});
  ajouter("bibliotheque.serieEmpreintes", QStringLiteral("Empreintes générées d'après une liste de noms"), P::Atelier, {});
  ajouter("bibliotheque.affecter", QStringLiteral("Affecter les empreintes en tableau et par règles d'équivalence"), P::Atelier, {});
  ajouter("bibliotheque.achats", QStringLiteral("Reste à acheter pour N cartes, d'après le stock et les catalogues"), P::Atelier, {});
  ajouter("circuit.gabaritsPastilles", QStringLiteral("Piles de pastilles du projet : une cote changée, toutes leurs pastilles suivent"), P::Atelier, {});
  ajouter("circuit.echangerEmpreintesMasse", QStringLiteral("Échanger en masse l'empreinte des composants visés"), P::Atelier, {});
  ajouter("circuit.aPlacer", QStringLiteral("Liste des composants à placer sur la carte"), P::Circuit, {});
  ajouter("circuit.disposerCercle", QStringLiteral("Disposer les empreintes choisies en cercle"), P::Circuit, {});
  ajouter("verification.rapport", QStringLiteral("Rapport de vérification, groupé par règle"), P::Atelier, {});
  ajouter("carte.modelesDossier", QStringLiteral("Associer un dossier de modèles 3D aux empreintes"), P::Atelier, {});
  ajouter("carte.exporterStep", QStringLiteral("Exporter la carte équipée en STEP"), P::Atelier, {});
  ajouter("carte.contourEsquisse", QStringLiteral("Contour de la carte piloté par une esquisse"), P::Atelier, {});
  ajouter("boitier.facade", QStringLiteral("Façade du boîtier"), P::Atelier, {});
  ajouter("boitier.impression", QStringLiteral("Pièces du boîtier pour l’impression 3D"), P::Atelier, {});
  ajouter("schema.repeterFeuille", QStringLiteral("Feuille de canal : répéter la feuille"), P::Schema, {});
  ajouter("schema.deplacerVersFeuille", QStringLiteral("Déplacer les symboles choisis vers une autre feuille"), P::Schema, {});
  // 0.34, lot L7 : hiérarchie, échange de portes, directives, graphe, formes, extraits, styles, assistant
  ajouter("schema.migrerHierarchie", QStringLiteral("Migrer les feuilles à plat en hiérarchie"), P::Schema, {});
  ajouter("schema.synchroniserPorts", QStringLiteral("Synchroniser les ports des blocs avec leurs feuilles filles"), P::Schema, {});
  ajouter("schema.feuilleMere", QStringLiteral("Feuille mère (un niveau plus haut)"), P::Schema, {});
  ajouter("schema.echangerPortes", QStringLiteral("Échanger les deux portes choisies"), P::Schema, {});
  ajouter("schema.directives", QStringLiteral("Appliquer les directives aux règles du circuit"), P::Schema, {});
  ajouter("schema.grapheReseaux", QStringLiteral("Graphe des réseaux"), P::Schema, {});
  ajouter("schema.importerFormes", QStringLiteral("Formes depuis un DXF ou un SVG"), P::Schema, {});
  ajouter("schema.enregistrerExtrait", QStringLiteral("Enregistrer la sélection en extrait réutilisable"), P::Atelier, {});
  ajouter("schema.poserExtrait", QStringLiteral("Poser un extrait réutilisable"), P::Atelier, {});
  ajouter("schema.jeuxStyles", QStringLiteral("Jeux de styles et de réglages"), P::Atelier, {});
  ajouter("schema.assistantSymbole", QStringLiteral("Assistant de symboles normalisés"), P::Schema, {});
  ajouter("circuit.reproduireCanal", QStringLiteral("Reproduire le placement et le routage du canal sur ses copies"), P::Circuit, {});
  ajouter("circuit.recopierDisposition", QStringLiteral("Recopier la disposition des composants choisis sur les voies semblables"), P::Circuit, {});
  ajouter("circuit.copieLocale", QStringLiteral("Cuivre propre au composant : copie locale de son empreinte"), P::Circuit, {});
  ajouter("circuit.revenirBibliotheque", QStringLiteral("Revenir à l'empreinte de la bibliothèque"), P::Circuit, {});
  ajouter("circuit.eclaterEmpreinte", QStringLiteral("Éclater l'empreinte en objets libres"), P::Circuit, {});
  ajouter("circuit.generateurs", QStringLiteral("Bobine planaire, piste résistive, capteur capacitif"), P::Circuit, {});
  ajouter("bibliotheque.pastilles", QStringLiteral("Tableau des pastilles de l'empreinte"), P::Atelier, {});
  ajouter("regles.contraintes", QStringLiteral("Gestionnaire de contraintes : règles et classes, communes au schéma et au circuit"), P::Atelier, {});
  ajouter("circuit.routerComplet", QStringLiteral("Router tout : recherche de chemin pour chaque liaison"), P::Atelier, {});
  ajouter("fabrication.panneau", QStringLiteral("Mise en panneau en languettes perforées"), P::Atelier, {});
  ajouter("fabrication.devis", QStringLiteral("Devis de fabrication hors ligne"), P::Atelier, {});
  ajouter("fabrication.controler", QStringLiteral("Contrôler un dossier de fabrication avant l’envoi"), P::Atelier, {});
  ajouter("fabrication.visionneuse", QStringLiteral("Visionneuse HTML autonome"), P::Atelier, {});
  ajouter("fabrication.dsn", QStringLiteral("Carte Specctra (DSN) pour un routeur externe"), P::Atelier, {});
  ajouter("fabrication.ses", QStringLiteral("Poser la session (SES) d’un routeur externe"), P::Atelier, {});
  ajouter("fabrication.dxfMeca", QStringLiteral("DXF de la mécanique de la carte"), P::Atelier, {});
  ajouter("fabrication.boardview", QStringLiteral("BoardView pour la réparation"), P::Atelier, {});
  ajouter("fabrication.ipc2581", QStringLiteral("IPC-2581"), P::Atelier, {});
  ajouter("fabrication.importerNetlist", QStringLiteral("Mettre à jour depuis une netlist"), P::Atelier, {});
  ajouter("fabrication.gerberCarte", QStringLiteral("Relire un dossier Gerber en carte"), P::Atelier, {});
  ajouter("simulation.ouvrir", QStringLiteral("Simuler le schéma (ngspice)"), P::Atelier, {});
  ajouter("outils.conception", QStringLiteral("Outils de conception (variantes, revue, système, essais, intégrité, logique, atelier, exigences)"), P::Atelier, {});
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
  ajouter("vue3d.dessus", QStringLiteral("Vue 3D de dessus"), P::Atelier, {});
  ajouter("vue3d.dessous", QStringLiteral("Vue 3D de dessous"), P::Atelier, {});
  ajouter("vue3d.avant", QStringLiteral("Vue 3D de face"), P::Atelier, {});
  ajouter("vue3d.arriere", QStringLiteral("Vue 3D de l'arrière"), P::Atelier, {});
  ajouter("vue3d.gauche", QStringLiteral("Vue 3D de gauche"), P::Atelier, {});
  ajouter("vue3d.droite", QStringLiteral("Vue 3D de droite"), P::Atelier, {});
  ajouter("vue3d.iso", QStringLiteral("Vue 3D isométrique"), P::Atelier, {});
  ajouter("vue3d.quartDeTour", QStringLiteral("Quart de tour autour de l'axe de visée"), P::Atelier, {});
  ajouter("vue3d.projection", QStringLiteral("Perspective ou orthographique"), P::Atelier, {});
  ajouter("vue3d.grille", QStringLiteral("Grille au sol de 1 à 10 mm"), P::Atelier, {});
  ajouter("vue3d.enregistrerVue", QStringLiteral("Enregistrer le point de vue dans le projet"), P::Atelier, {});
  ajouter("vue3d.pointsDeVue", QStringLiteral("Revenir à un point de vue enregistré"), P::Atelier, {});
  ajouter("vue3d.apparence", QStringLiteral("Apparence de la 3D : vernis, familles, boîtes, pièces"), P::Atelier, {});
  ajouter("vue3d.mesurer", QStringLiteral("Mesure exacte entre les deux composants choisis"), P::Atelier, {});
  ajouter("vue3d.rendu", QStringLiteral("Rendu de la carte par lancer de rayons, images clés"), P::Atelier, {});
  ajouter("carte.boitierCommerce", QStringLiteral("Boîtier du commerce importé en STEP pour les contrôles"), P::Atelier, {});
  ajouter("carte.pieceMontee", QStringLiteral("Pièce mécanique montée sur la carte"), P::Atelier, {});
  ajouter("carte.exporterIdf", QStringLiteral("Exporter la carte en IDF 3.0 (.emn et .emp)"), P::Atelier, {});
  ajouter("carte.importerIdf", QStringLiteral("Importer une carte IDF 3.0"), P::Atelier, {});
  ajouter("fabrication.valider", QStringLiteral("Valider la conception"), P::Atelier, {});
  ajouter("fabrication.publier", QStringLiteral("Publier pour la fabrication"), P::Atelier, {});
  ajouter("fabrication.dossier", QStringLiteral("Dossier de fabrication (brouillon)"), P::Atelier, {});
  ajouter("fabrication.dessinSchema", QStringLiteral("Dessin du schéma (PDF)"), P::Atelier, {});
  ajouter("fabrication.typon", QStringLiteral("Typon de gravure (PDF, SVG)"), P::Atelier, {});
  ajouter("fabrication.pdfCouches", QStringLiteral("PDF du circuit par couches"), P::Atelier, {});
  ajouter("fabrication.signauxXlsx", QStringLiteral("Récapitulatif des signaux (XLSX)"), P::Atelier, {});
  ajouter("fabrication.preparerSerigraphie", QStringLiteral("Préparer la sérigraphie pour la fabrication"), P::Atelier, {});
  ajouter("regles.signauxSerie", QStringLiteral("Signaux à travers les composants série"), P::Atelier, {});
  ajouter("bibliotheque.importerAutres", QStringLiteral("Importer des bibliothèques d'autres logiciels (KiCad, EAGLE)"), P::Atelier, {});
  ajouter("composants.nomenclatureEntree", QStringLiteral("Nomenclature en entrée (CSV, XLSX)"), P::Atelier, {});
  ajouter("carte.importerDansCarte", QStringLiteral("Ajouter à la carte une carte d'un autre logiciel"), P::Atelier, {});
  ajouter("fabrication.netlistTierce", QStringLiteral("Netlist pour un autre logiciel (OrCAD, PADS, Allegro, CADSTAR, EDIF, SPICE, gEDA, TinyCAD, gabarit)"), P::Atelier, {});
  ajouter("carte.importerDessin", QStringLiteral("Poser un dessin DXF, SVG ou DWG sur une couche de la carte"), P::Atelier, {});
  ajouter("carte.exporterDxfCouches", QStringLiteral("DXF des couches de la carte (jeux enregistrés, miroir)"), P::Atelier, {});
  ajouter("carte.exporter3D", QStringLiteral("Exporter la carte en 3D (GLB, VRML, STL, COLLADA, AMF, OpenSCAD, XAO, STEP compressé)"), P::Atelier, {});
  ajouter("carte.idxEnvoyer", QStringLiteral("Envoyer la carte à la mécanique (IDX)"), P::Atelier, {});
  ajouter("carte.idxRecevoir", QStringLiteral("Recevoir une proposition de la mécanique (IDX)"), P::Atelier, {});
  ajouter("carte.idxHistorique", QStringLiteral("Historique des échanges IDX"), P::Atelier, {});
  ajouter("regles.gabaritInterface", QStringLiteral("Gabarit d'interface (USB, HDMI, PCIe, DDR…)"), P::Atelier, {});
  ajouter("regles.profilImpedance", QStringLiteral("Profil d'impédance d'une classe"), P::Atelier, {});
  ajouter("circuit.routerStrategies", QStringLiteral("Routage par stratégies comparées"), P::Atelier, {});
  ajouter("circuit.echangesBroches", QStringLiteral("Optimiser les échanges de broches"), P::Atelier, {});
  ajouter("fabrication.planMontage", QStringLiteral("Plan de montage (PDF)"), P::Atelier, {});
  ajouter("fabrication.nomenclature", QStringLiteral("Nomenclature (CSV)"), P::Atelier, {});
  ajouter("fabrication.reseaux", QStringLiteral("Liste des réseaux (netlist)"), P::Atelier, {});
  ajouter("fabrication.kicad", QStringLiteral("Exporter vers KiCad (.kicad_pcb)"), P::Atelier, {});
  ajouter("fabrication.netlistKicad", QStringLiteral("Liste de réseaux KiCad (.net)"), P::Atelier, {});
  ajouter("fabrication.ipc", QStringLiteral("Liste IPC-D-356 (test électrique)"), P::Atelier, {});
  ajouter("fabrication.boitier", QStringLiteral("Concevoir le boîtier de cette carte"), P::Atelier, {});

  // Lot L5 (0.34) : cuivre, zones, placement, dessin de la carte (OutilsCuivre.cpp)
  ajouter("vue.reseaux", QStringLiteral("Navigateur de réseaux"), P::Atelier, {});
  ajouter("circuit.poserEmpreinte", QStringLiteral("Poser une empreinte sans schéma (carte seule)"), P::Circuit, {});
  ajouter("circuit.renumeroter", QStringLiteral("Renuméroter les repères selon leur place sur la carte"), P::Circuit, {});
  ajouter("circuit.importerPlacement", QStringLiteral("Importer un placement (CSV, texte, XLSX ; KiCad, Altium, PADS, Allegro)"), P::Circuit, {});
  ajouter("circuit.placerReperes", QStringLiteral("Placer les repères de sérigraphie autour des corps"), P::Circuit, {});
  ajouter("circuit.decouplages", QStringLiteral("Placer les condensateurs de découplage du circuit choisi"), P::Circuit, {});
  ajouter("circuit.serrer", QStringLiteral("Serrer les empreintes choisies vers leur centre"), P::Circuit, {});
  ajouter("circuit.echangerEmpreintes", QStringLiteral("Échanger la place de deux empreintes"), P::Circuit, {});
  ajouter("circuit.ecarterVoisines", QStringLiteral("Écarter les voisines de l’empreinte choisie (placement poussé)"), P::Circuit, {});
  ajouter("circuit.nettoyer", QStringLiteral("Nettoyer les pistes (segments nuls, doublons, alignés, vias orphelines)"), P::Circuit, {});
  ajouter("circuit.lisser", QStringLiteral("Lisser les pistes choisies (coins à 45°)"), P::Circuit, {});
  ajouter("circuit.derouter", QStringLiteral("Dérouter une connexion, un réseau, un composant ou la carte"), P::Circuit, {});
  ajouter("circuit.pinceauReseau", QStringLiteral("Donner un réseau aux objets choisis"), P::Circuit, {});
  ajouter("circuit.couperPiste", QStringLiteral("Couper les pistes choisies en leur milieu"), P::Circuit, {});
  ajouter("circuit.routerChoisies", QStringLiteral("Router les liaisons des composants choisis"), P::Circuit, {});
  ajouter("circuit.routerPaire", QStringLiteral("Router une paire différentielle à écart constant"), P::Circuit, {});
  ajouter("circuit.accorder", QStringLiteral("Accorder la longueur d’un réseau"), P::Circuit, {});
  ajouter("circuit.accorderGroupe", QStringLiteral("Accorder un groupe de réseaux (longueur ou retard)"), P::Circuit, {});
  ajouter("circuit.regenererMotifs", QStringLiteral("Refaire les motifs d’accord d’après les règles"), P::Circuit, {});
  ajouter("circuit.gouttes", QStringLiteral("Poser les gouttes sur les pastilles et les vias"), P::Circuit, {});
  ajouter("circuit.retirerGouttes", QStringLiteral("Retirer les gouttes"), P::Circuit, {});
  ajouter("circuit.zoneCarte", QStringLiteral("Zone sur toute la carte, couche active"), P::Circuit, {});
  ajouter("circuit.planReseau", QStringLiteral("Plan d’un réseau sur toutes les couches"), P::Circuit, {});
  ajouter("circuit.coudre", QStringLiteral("Coudre la zone choisie de vias"), P::Circuit, {});
  ajouter("circuit.relierIlots", QStringLiteral("Relier les îlots de la zone par des vias"), P::Circuit, {});
  ajouter("circuit.dupliquerZone", QStringLiteral("Dupliquer la zone choisie sur une autre couche"), P::Circuit, {});
  ajouter("circuit.fusionnerZones", QStringLiteral("Fusionner les zones choisies"), P::Circuit, {});
  ajouter("circuit.soustraireZone", QStringLiteral("Soustraire la seconde zone choisie de la première"), P::Circuit, {});
  ajouter("circuit.couperZone", QStringLiteral("Couper la zone choisie en deux"), P::Circuit, {});
  ajouter("circuit.zoneDepuisPistes", QStringLiteral("Zone qui épouse les pistes choisies"), P::Circuit, {});
  ajouter("circuit.arrondirZone", QStringLiteral("Arrondir ou chanfreiner les coins de la zone"), P::Circuit, {});
  ajouter("circuit.figerZones", QStringLiteral("Figer les zones (cuivre gardé)"), P::Circuit, {});
  ajouter("circuit.mettreDeCote", QStringLiteral("Mettre les zones de côté (sans cuivre)"), P::Circuit, {});
  ajouter("circuit.zonesNormales", QStringLiteral("Rendre les zones à la normale"), P::Circuit, {});
  ajouter("circuit.collageSpecial", QStringLiteral("Collage spécial du circuit (réseaux, classe, composants)"), P::Circuit, {});
  ajouter("circuit.codeQR", QStringLiteral("Poser un code QR ou Micro QR"), P::Circuit, {});
  ajouter("circuit.code128", QStringLiteral("Poser un code-barres (Code 128)"), P::Circuit, {});
  ajouter("circuit.logo", QStringLiteral("Poser un logo tiré d’une image"), P::Circuit, {});
  ajouter("circuit.gabarit", QStringLiteral("Appliquer un gabarit de carte (Arduino, Raspberry Pi, Eurocarte…)"), P::Circuit, {});
  ajouter("circuit.reprendreReglages", QStringLiteral("Reprendre les réglages d’une autre carte"), P::Circuit, {});
  ajouter("circuit.echangerCouches", QStringLiteral("Échanger deux couches"), P::Circuit, {});
  ajouter("circuit.textesMasse", QStringLiteral("Hauteur de tous les textes"), P::Circuit, {});
  ajouter("circuit.origineTravail", QStringLiteral("Origine de travail sous la souris, y vers le haut"), P::Circuit, {});
  ajouter("circuit.origineFichiers", QStringLiteral("Fichiers de fabrication dans le repère de travail (ou depuis le coin de la carte)"), P::Circuit, {});
  ajouter("circuit.contourPoints", QStringLiteral("Contour de la carte par ses sommets"), P::Circuit, {});
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
      {"vue.reseaux", "Réseaux", "fil", "tous les réseaux, par classe ou par motif ; un clic les surligne partout"},
      {"circuit.poserEmpreinte", "Empreinte seule…", "composants", "son réseau vient des pistes tirées entre les pastilles"},
      {"circuit.renumeroter", "Renuméroter…", "proprietes", "huit parcours, préfixe du dessous, aperçu ; le schéma suit"},
      {"circuit.importerPlacement", "Importer un placement…", "composants", "positions et faces relues, journal de ce qui est fait"},
      {"circuit.placerReperes", "Repères…", "texte", "pavé numérique autour du corps, ou la place libre la plus proche"},
      {"circuit.decouplages", "Découplages", "composants", "chacun au plus près de sa broche, le plus petit d’abord, puis relié"},
      {"circuit.serrer", "Serrer", "composants", "elles se rapprochent sans se recouvrir"},
      {"circuit.echangerEmpreintes", "Échanger", "pivoter", "positions, rotations et faces échangées"},
      {"circuit.ecarterVoisines", "Écarter", "composants", "les voisines qui la touchent s’écartent ; un verrou bloque"},
      {"circuit.nettoyer", "Nettoyer…", "piste", "changements prévus, puis choisis"},
      {"circuit.lisser", "Lisser", "piste", "coins droits coupés à 45°, isolement tenu"},
      {"circuit.derouter", "Dérouter…", "gomme", "le cuivre verrouillé reste"},
      {"circuit.pinceauReseau", "Réseau…", "fil", "pinceau de réseau : pistes, vias et zones"},
      {"circuit.couperPiste", "Couper", "piste", "les deux morceaux restent reliés"},
      {"circuit.routerChoisies", "Router le choix", "piste", "plusieurs liaisons en une commande, les plus courtes d’abord"},
      {"circuit.routerPaire", "Paire…", "piste", "le brin négatif suit le positif à écart constant"},
      {"circuit.accorder", "Accorder…", "piste", "accordéon, trombone ou dents de scie sur le plus long segment"},
      {"circuit.accorderGroupe", "Accorder un groupe…", "piste", "chaque réseau du groupe amené à la cible"},
      {"circuit.regenererMotifs", "Refaire les motifs", "piste", "largeur et pas repris des règles, cible tenue"},
      {"circuit.gouttes", "Gouttes", "piste", "refaites à chaque appel, isolement tenu"},
      {"circuit.retirerGouttes", "Retirer les gouttes", "gomme", ""},
      {"circuit.zoneCarte", "Zone carte…", "zone", "le contour de la carte, nommée d’office"},
      {"circuit.planReseau", "Plan…", "zone", "une zone par cuivre, sauf celles qui y sont déjà"},
      {"circuit.coudre", "Coudre…", "zone", "grille de vias dans le cuivre rempli, refaite à chaque appel"},
      {"circuit.relierIlots", "Relier les îlots", "zone", "une via vers le plan du même réseau de l’autre face"},
      {"circuit.dupliquerZone", "Dupliquer…", "zone", ""},
      {"circuit.fusionnerZones", "Fusionner", "zone", "même réseau, même couche"},
      {"circuit.soustraireZone", "Soustraire", "zone", ""},
      {"circuit.couperZone", "Couper la zone…", "zone", ""},
      {"circuit.zoneDepuisPistes", "Depuis les pistes…", "zone", ""},
      {"circuit.arrondirZone", "Arrondir…", "zone", ""},
      {"circuit.figerZones", "Figer", "zone", "une vérification signale celles devenues périmées"},
      {"circuit.mettreDeCote", "De côté", "zone", ""},
      {"circuit.zonesNormales", "Normales", "zone", ""},
      {"circuit.collageSpecial", "Collage spécial…", "coller", "réseaux gardés ou retirés, classe, composants neufs"},
      {"circuit.codeQR", "Code QR…", "texte", "sérigraphie, vernis ou cuivre"},
      {"circuit.code128", "Code-barres…", "texte", ""},
      {"circuit.logo", "Logo…", "texte", "seuil, puis contours lissés"},
      {"circuit.gabarit", "Gabarit…", "proprietes", "contour et trous de fixation"},
      {"circuit.reprendreReglages", "Reprendre les réglages…", "proprietes", "règles, classes, contraintes, empilage, couches, vues, fabrication"},
      {"circuit.echangerCouches", "Échanger les couches…", "proprietes", "pistes, zones, vias, textes et dessins"},
      {"circuit.textesMasse", "Textes…", "texte", "textes et repères en une commande"},
      {"circuit.origineTravail", "Origine…", "point", ""},
      {"circuit.origineFichiers", "Origine des fichiers", "point", "les fichiers de fabrication et de placement depuis l’origine de travail, ou depuis le coin de la carte"},
      {"circuit.contourPoints", "Contour par sommets…", "proprietes", "tableau de coordonnées dans le repère de travail"},
      {"schema.choisirComposant", "Placer un composant", "composants", "bibliothèque de symboles, avec aperçu"},
      {"composant.creer", "Créer un composant…", "creer-composant", "son symbole est dessiné depuis la liste de ses broches"},
      {"bibliotheque.articles", "Articles…", "composants", "composants réels : valeur, empreinte, fabricant, hauteur ; recherche « 10k 0603 »"},
      {"bibliotheque.standardiser", "Standardiser…", "proprietes", "remplace les composants sans article réel par l’article trouvé"},
      {"bibliotheque.misesAJour", "Mises à jour…", "proprietes", "composants dont l’article a changé dans la bibliothèque"},
      {"bibliotheque.gerer", "Articles en tableau…", "proprietes", "séries E, import CSV ou XLSX, renommage par gabarit, copie avec les dépendances"},
      {"bibliotheque.serieEmpreintes", "Empreintes par noms…", "creer-composant", "une liste de noms donne des empreintes IPC aux densités M, N et L"},
      {"bibliotheque.affecter", "Affecter les empreintes…", "proprietes", "tableau des composants et règles « Résistance 0603 → R_0603 »"},
      {"bibliotheque.achats", "Reste à acheter…", "proprietes", "pour N cartes, d’après le stock de l’atelier et les catalogues importés"},
      {"circuit.gabaritsPastilles", "Piles de pastilles…", "proprietes", "gabarits IPC-7351 : une cote changée, les pastilles qui les citent suivent en une commande"},
      {"circuit.echangerEmpreintesMasse", "Empreintes en masse…", "proprietes", "une empreinte donnée aux composants visés, remplacée en place"},
      {"circuit.aPlacer", "À placer", "composants", "empreintes qui attendent hors de la carte, par feuille ; elles se glissent sur la carte"},
      {"circuit.disposerCercle", "En cercle…", "pivoter", "les empreintes choisies sur un cercle ou un arc, tournées vers le centre"},
      {"verification.rapport", "Rapport…", "drc", "rapport HTML groupé par règle, exclusions à part ; chaque ligne mène à l’objet"},
      {"carte.modelesDossier", "Modèles 3D…", "composants", "un dossier de modèles STEP rapproché des empreintes, chaque modèle calé"},
      {"carte.exporterStep", "STEP de la carte…", "composants", "carte équipée, un produit nommé par composant"},
      {"carte.contourEsquisse", "Contour esquissé…", "proprietes", "le contour suit les cotes d’une esquisse, trous de fixation compris"},
      {"boitier.facade", "Façade…", "proprietes", "découpes, légendes et échelles qui suivent leurs composants ; SVG et DXF"},
      {"boitier.impression", "Impression 3D…", "composants", "pièces du boîtier à plat en STL, volume, masse et coût"},
      {"schema.repeterFeuille", "Canal ×…", "composants", "la feuille répétée en copies liées, repères par canal"},
      {"schema.deplacerVersFeuille", "Vers la feuille…", "composants", "symboles déplacés, repères, empreintes et pistes gardés"},
      {"schema.migrerHierarchie", "En hiérarchie", "plan", "une racine, un bloc par feuille ; mêmes réseaux"},
      {"schema.synchroniserPorts", "Synchroniser les ports", "etiquette", "ports des blocs ajoutés ou retirés d’après les feuilles filles"},
      {"schema.feuilleMere", "Feuille mère", "recadrer", "un niveau plus haut ; le fil d’Ariane y mène aussi"},
      {"schema.echangerPortes", "Échanger les portes", "pivoter", "deux unités choisies d’un même composant ; le circuit suit"},
      {"schema.directives", "Appliquer les directives", "proprietes", "classes des directives reprises par les règles du circuit"},
      {"schema.grapheReseaux", "Graphe des réseaux", "vue", "composants et réseaux rangés par ressorts"},
      {"schema.importerFormes", "Formes DXF, SVG…", "importer", "dessin importé en formes modifiables, calques choisis"},
      {"schema.enregistrerExtrait", "Enregistrer l’extrait…", "exporter", "la sélection du schéma ou du circuit, réutilisable"},
      {"schema.poserExtrait", "Poser un extrait…", "coller", "un extrait enregistré, posé au centre"},
      {"schema.jeuxStyles", "Jeux de styles…", "proprietes", "styles des fils, jonctions, textes ; valeurs par défaut de la carte"},
      {"schema.assistantSymbole", "Assistant de symboles…", "creer-composant", "MOSFET, transistor, diode, AOP, régulateur numérotés comme l’empreinte"},
      {"circuit.reproduireCanal", "Reproduire le canal", "composants", "placement et routage du canal source recopiés sur les autres"},
      {"circuit.recopierDisposition", "Recopier la disposition…", "repetition-lineaire", "voies semblables d’une conception à plat, composants appariés seuls"},
      {"circuit.copieLocale", "Cuivre propre", "piste", "copie locale de l’empreinte du composant, écart à la bibliothèque affiché"},
      {"circuit.revenirBibliotheque", "Revenir à la bibliothèque", "annuler", "la copie locale abandonnée"},
      {"circuit.eclaterEmpreinte", "Éclater l’empreinte", "convertir", "pastilles et traits en objets libres (composant sans schéma)"},
      {"circuit.generateurs", "Générateurs…", "piste", "bobine planaire, piste résistive, capteur capacitif"},
      {"bibliotheque.pastilles", "Pastilles…", "pcb", "tableau des pastilles, doublons, renumérotation, coordonnées"},
      {"regles.contraintes", "Contraintes…", "proprietes", "règles de la carte et des classes, les mêmes au schéma et au circuit ; un changement, un pas"},
      {"circuit.routerComplet", "Router tout", "piste", "recherche de chemin autour des obstacles, sur les deux faces ; jamais sous l’isolement"},
      {"fabrication.panneau", "Panneau…", "exporter", "cartes en lignes et colonnes, languettes perforées, contrôle avant la sortie"},
      {"fabrication.devis", "Devis…", "proprietes", "JLCPCB et PCBWay côte à côte, d’après les grilles datées, sans réseau"},
      {"fabrication.controler", "Contrôler les fichiers…", "drc", "fichiers relus, connectivité comparée à la conception, avant l’envoi"},
      {"fabrication.visionneuse", "Visionneuse…", "exporter", "une page autonome : schéma, circuit, 3D et nomenclature liés"},
      {"fabrication.dsn", "Specctra DSN…", "piste", "la carte pour Freerouting ou un autre routeur ; sa session se relit ensuite"},
      {"fabrication.ses", "Session SES…", "piste", "le routage d’un routeur externe posé en un pas"},
      {"fabrication.dxfMeca", "DXF mécanique…", "exporter", "contour, découpes, perçages et encombrements pour la mécanique"},
      {"fabrication.boardview", "BoardView…", "exporter", "pour la réparation : composants, broches et réseaux des deux faces"},
      {"fabrication.ipc2581", "IPC-2581…", "exporter", "un seul fichier XML pour la fabrication et l’assemblage"},
      {"fabrication.importerNetlist", "Netlist…", "fil", "la conception mise à jour d’une netlist KiCad ou Prométhée, rapport CSV"},
      {"fabrication.gerberCarte", "Gerber en carte…", "composants", "un dossier de fabrication relu en carte modifiable, réseaux tirés du cuivre"},
      {"simulation.ouvrir", "Simuler…", "erc", "point de repos, balayage, fréquence, transitoire par ngspice ; courbes, curseurs et mesures"},
      {"outils.conception", "Outils…", "proprietes", "variantes et coût, revue, comparaison, système, notes, historique, mise en service, intégrité, logique, atelier, exigences"},
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
      {"vue3d.dessus", "Dessus", "vue", ""},
      {"vue3d.dessous", "Dessous", "vue", ""},
      {"vue3d.avant", "Face", "vue", ""},
      {"vue3d.arriere", "Arrière", "vue", ""},
      {"vue3d.gauche", "Gauche", "vue", ""},
      {"vue3d.droite", "Droite", "vue", ""},
      {"vue3d.iso", "Iso", "vue", ""},
      {"vue3d.quartDeTour", "Quart de tour", "pivoter", "90° autour de l’axe de visée"},
      {"vue3d.projection", "Projection", "vue", "perspective ou orthographique"},
      {"vue3d.grille", "Grille…", "grille", "grille au sol de 1 à 10 mm"},
      {"vue3d.enregistrerVue", "Garder la vue…", "point", "point de vue nommé, gardé dans le projet"},
      {"vue3d.pointsDeVue", "Points de vue…", "point", "revenir à un point de vue enregistré"},
      {"vue3d.apparence", "Apparence…", "proprietes", "vernis, familles masquées, boîtes à la place des modèles"},
      {"vue3d.mesurer", "Mesurer", "mesure", "distance exacte entre deux corps, ΔX ΔY ΔZ et distance à la carte"},
      {"vue3d.rendu", "Rendu…", "exporter", "image par lancer de rayons, fond transparent ; images clés"},
      {"carte.boitierCommerce", "Boîtier du commerce…", "importer", "fichier STEP du fabricant, pour les contrôles de collision"},
      {"carte.pieceMontee", "Pièce montée…", "composants", "dissipateur, entretoise, blindage : dans la nomenclature commune"},
      {"carte.exporterIdf", "IDF…", "exporter", "IDF 3.0 pour la CAO mécanique"},
      {"carte.importerIdf", "Lire un IDF…", "importer", "carte IDF 3.0 ajoutée au projet"},
      {"fabrication.valider", "Valider la conception", "valider", "intégrité, bibliothèques, ERC, DRC, nomenclature, fabrication, documentation"},
      {"fabrication.publier", "Publier pour la fabrication…", "publier", "dossier complet, révision verrouillée, empreinte de la conception"},
      {"fabrication.dossier", "Dossier de fabrication (brouillon)…", "exporter", "Gerber, perçage, placement, nomenclature, sans publier"},
      {"fabrication.dessinSchema", "Dessin du schéma (PDF)…", "plan", "toutes les feuilles, avec leur cartouche"},
      {"fabrication.typon", "Typon de gravure (PDF, SVG)…", "pcb", "une couche de cuivre à l’échelle 1, miroir, négatif, perçages réduits : insoleuse ou transfert de toner"},
      {"fabrication.pdfCouches", "PDF par couches…", "plan", "une page par couche, repères cherchables, empreintes cliquables, signets et métadonnées"},
      {"fabrication.signauxXlsx", "Signaux (XLSX)…", "exporter", "signaux, paires et groupes d’accord avec leurs longueurs et retards"},
      {"fabrication.preparerSerigraphie", "Préparer la sérigraphie…", "texte", "traits rognés autour des pastilles, repères écartés, hors carte supprimé ; la liste d’abord"},
      {"regles.signauxSerie", "Signaux série", "fil", "un signal suivi à travers les résistances, condensateurs et ferrites série"},
      {"bibliotheque.importerAutres", "Bibliothèques tierces…", "importer-cao", "KiCad (.kicad_sym, .pretty) et EAGLE (.lbr) vers la bibliothèque personnelle ou d’équipe, avec un rapport par objet"},
      {"composants.nomenclatureEntree", "Nomenclature en entrée…", "composants", "valeurs, fabricants, MPN et codes LCSC d’une nomenclature CSV ou XLSX, la liste des changements d’abord"},
      {"carte.importerDansCarte", "Ajouter une carte tierce…", "importer-cao", "carte KiCad ou EAGLE ajoutée à la carte ouverte : couches associées, repères renumérotés, un seul Ctrl+Z"},
      {"fabrication.netlistTierce", "Netlist tierce…", "fil", "OrCAD PCB II, PADS, Allegro, CADSTAR, EDIF, SPICE, gEDA, TinyCAD ou gabarit maison"},
      {"carte.importerDessin", "Dessin DXF, SVG…", "dxf", "logo ou marquage sur la couche choisie, à la largeur voulue, discontinuités réparées"},
      {"carte.exporterDxfCouches", "DXF des couches…", "dxf", "un calque par couche, cotes comprises, jeux enregistrés avec leur miroir"},
      {"carte.exporter3D", "Export 3D…", "exporter", "GLB, VRML, STL, COLLADA, AMF, OpenSCAD, XAO ou STEP compressé, couleurs par face"},
      {"carte.idxEnvoyer", "Envoyer en IDX…", "exporter", "ligne de base pour la mécanique : contour, trous, composants, zones, cuivre au choix"},
      {"carte.idxRecevoir", "Recevoir un IDX…", "importer-cao", "proposition de la mécanique : chaque changement accepté ou refusé, une commande"},
      {"carte.idxHistorique", "Échanges IDX", "rapport", "envois et propositions reçues, avec ce qui a été accepté"},
      {"regles.gabaritInterface", "Gabarit d’interface…", "contraintes", "USB, HDMI, PCIe, MIPI, Ethernet, DDR3, DDR4 : signaux, paires, classes et règles d’un geste"},
      {"regles.profilImpedance", "Profil d’impédance…", "calcul", "largeur par couche pour une impédance visée, suivie par le routeur au changement de couche"},
      {"circuit.routerStrategies", "Router par stratégies…", "piste", "deux stratégies essayées sur une copie, la meilleure appliquée en une commande"},
      {"circuit.echangesBroches", "Échanges de broches…", "convertir", "affectation optimale des broches d’un même groupe qui démêle le chevelu"},
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

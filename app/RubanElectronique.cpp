// Prométhée : ruban de l'atelier électronique (lot L1, tâche 1.15, A1) : ses quatre onglets (Schéma, Circuit
// imprimé, 3D, Fabrication), les couches proposées au circuit, les feuilles qu'on déplace ou supprime, le schéma
// seul et son circuit créé au premier besoin, le contour importé d'un DXF et le profil de fabricant. Le reste de
// l'atelier est dans AtelierElectronique.cpp ; le découpage se poursuit aux lots L2 à L5.
// Licence GPL-3.0-only.
#include "AtelierElectronique.hpp"
#include "SelectionElectronique.hpp"
#include "FenetresMeca034.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include <QComboBox>
#include <QSettings>
#include <QPainter>
#include <QLineEdit>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "DescriptionRuban.hpp"
#include "Document.hpp"
#include "Icones.hpp"
#include "PanneauVerifications.hpp"
#include "RegistreActions.hpp"
#include "TableauChamps.hpp"
#include "MosaiqueVues.hpp"
#include "TableauObjets.hpp"
#include "Theme.hpp"
#include "VuePiece.hpp"
#include "VueSchema.hpp"
#include "Vignettes.hpp"
#include "promethee/echanges_electronique.hpp"
#include "promethee/bibliotheque.hpp"
#include "promethee/dxf.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/kicad_electronique.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

// ---------- Ruban ----------
// Feuilles (EL-41) : une feuille se déplace dans la liste ou se supprime, avec ce qu'elle contient ; la dernière
// reste. Les composants qui n'ont plus de symbole disparaissent avec leur empreinte.
void AtelierElectronique::deplacerFeuille(int feuille, int sens) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  const int j = feuille + sens;
  if (feuille < 0 || j < 0 || j >= static_cast<int>(c->feuilles.size())) return;
  modifierConception([&](ConceptionElec& c2) { std::swap(c2.feuilles[static_cast<std::size_t>(feuille)], c2.feuilles[static_cast<std::size_t>(j)]); });
  m_schema->setConception(m_conception, j);
  majPanneau();
}

void AtelierElectronique::supprimerFeuille(int feuille) {
  const ConceptionElec* c = conceptionCourante();
  if (!c || feuille < 0 || feuille >= static_cast<int>(c->feuilles.size())) return;
  if (c->feuilles.size() < 2) { afficherMessage(QStringLiteral("La dernière feuille ne se supprime pas."), NiveauMessage::Information); return; }
  const Feuille& f = c->feuilles[static_cast<std::size_t>(feuille)];
  if (QMessageBox::question(this, QStringLiteral("Supprimer la feuille"),
                            QStringLiteral("Supprimer « %1 » et ses %2 symbole(s), %3 fil(s) ?").arg(q(f.nom)).arg(f.symboles.size()).arg(f.fils.size())) != QMessageBox::Yes)
    return;
  modifierConception([&](ConceptionElec& c2) {
    c2.feuilles.erase(c2.feuilles.begin() + feuille);
    std::set<std::string> utilises;
    for (const auto& g : c2.feuilles) for (const auto& sy : g.symboles) if (!sy.composant.empty()) utilises.insert(sy.composant);
    c2.composants.erase(std::remove_if(c2.composants.begin(), c2.composants.end(), [&](const ComposantElec& x) { return !utilises.count(x.uuid); }), c2.composants.end());
    c2.circuit.empreintes.erase(std::remove_if(c2.circuit.empreintes.begin(), c2.circuit.empreintes.end(), [&](const EmpreintePlacee& e) { return !utilises.count(e.composant); }),
                                c2.circuit.empreintes.end());
  });
  m_schema->setConception(m_conception, std::max(0, feuille - 1));
  majPanneau();
}

// Commandes de l'atelier, par identifiant du registre (1.20) : le ruban, les menus et les scripts les lancent par
// executerAction ; les actions des vues (outils, presse-papiers, vue) vont à la vue qui les fait
void AtelierElectronique::enregistrerCommandes() {
  using O = VueSchema::Outil;
  using OC = VueCircuit::Outil;
  auto& c = m_commandes;
  auto alimentation = [this](const char* symbole) {
    return [this, symbole] {
      setMode(Schema);
      m_schema->setOutil(O::Alimentation, symbole);
      m_schema->setFocus();
    };
  };
  auto outilCircuit = [this](OC o) {
    return [this, o] {
      setMode(Circuit);
      m_circuit->setOutil(o);
      m_circuit->setFocus();
    };
  };
  c["schema.choisirComposant"] = [this] { montrerBibliotheque(); };
  c["composant.creer"] = [this] { creerComposant(); };
  c["bibliotheque.articles"] = [this] { ouvrirChoixComposant(); };
  c["bibliotheque.standardiser"] = [this] { ouvrirStandardisation(); };
  c["bibliotheque.misesAJour"] = [this] { ouvrirMisesAJourArticles(); };
  c["bibliotheque.gerer"] = [this] { ouvrirGestionBibliotheque(); };
  c["bibliotheque.serieEmpreintes"] = [this] { ouvrirSerieEmpreintes(); };
  c["bibliotheque.affecter"] = [this] { ouvrirAffectation(); };
  c["circuit.gabaritsPastilles"] = [this] { ouvrirGabaritsPastilles(); };
  c["bibliotheque.achats"] = [this] { ouvrirAchats(); };
  c["circuit.echangerEmpreintesMasse"] = [this] { ouvrirEchangeEmpreintes(); };
  c["circuit.aPlacer"] = [this] { montrerAPlacer(); };
  c["circuit.disposerCercle"] = [this] { disposerEnCercle(); };
  c["verification.rapport"] = [this] { ouvrirRapport(); };
  c["carte.modelesDossier"] = [this] {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier de modèles 3D"));
    if (!d.isEmpty()) associerDossierModeles(d);
  };
  c["carte.exporterStep"] = [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Carte équipée en STEP"), QStringLiteral("carte.step"), QStringLiteral("STEP (*.step *.stp)"));
    if (!f.isEmpty()) exporterStepCarte(f);
  };
  c["carte.contourEsquisse"] = [this] { ouvrirContourEsquisse(); };
  c["schema.repeterFeuille"] = [this] {
    bool ok = false;
    const int n = QInputDialog::getInt(this, QStringLiteral("Feuille de canal"), QStringLiteral("Nombre d’exemplaires de la feuille"), 4, 2, 64, 1, &ok);
    if (ok) repeterFeuille(vueSchema()->feuille(), n);
  };
  // 0.34, lot L7
  c["schema.migrerHierarchie"] = [this] { migrerEnHierarchie(); };
  c["schema.synchroniserPorts"] = [this] { synchroniserPortsBlocs(false); };
  c["schema.feuilleMere"] = [this] { allerFeuilleMere(); };
  c["schema.echangerPortes"] = [this] { echangerPortesChoisies(); };
  c["schema.directives"] = [this] { appliquerDirectivesSchema(); };
  c["schema.grapheReseaux"] = [this] { ouvrirGrapheReseaux(); };
  c["schema.importerFormes"] = [this] { ouvrirImportFormes(); };
  c["schema.enregistrerExtrait"] = [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Enregistrer l’extrait"), QStringLiteral("extrait.json"), QStringLiteral("Extraits Prométhée (*.json)"));
    if (!f.isEmpty()) enregistrerExtrait(f, QFileInfo(f).completeBaseName());
  };
  c["schema.poserExtrait"] = [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Poser un extrait"), {}, QStringLiteral("Extraits Prométhée (*.json)"));
    if (!f.isEmpty()) poserExtraitFichier(f);
  };
  c["schema.jeuxStyles"] = [this] { ouvrirJeuxStyles(); };
  c["schema.assistantSymbole"] = [this] { ouvrirAssistantSymbole(); };
  c["circuit.recopierDisposition"] = [this] {
    bool ok = false;
    const QString t = QInputDialog::getText(this, QStringLiteral("Recopier la disposition"), QStringLiteral("Décalage d’une voie à la suivante, x et y en mm :"), QLineEdit::Normal, QStringLiteral("0 15"), &ok);
    const QStringList m = t.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (ok && m.size() == 2) recopierDispositionChoisie(QString(m[0]).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(), QString(m[1]).replace(QLatin1Char(','), QLatin1Char('.')).toDouble());
  };
  c["circuit.copieLocale"] = [this] { copieLocaleChoisie(); };
  c["circuit.revenirBibliotheque"] = [this] { revenirBibliothequeChoisie(); };
  c["circuit.eclaterEmpreinte"] = [this] { eclaterEmpreinteChoisie(); };
  c["circuit.generateurs"] = [this] { ouvrirGenerateurs(); };
  c["bibliotheque.pastilles"] = [this] { ouvrirPastilles(); };
  c["schema.deplacerVersFeuille"] = [this] {
    const prom::ecad::ConceptionElec* cc = conceptionCourante();
    if (!cc) return;
    QStringList noms;
    for (const auto& f : cc->feuilles) noms << QString::fromStdString(f.nom);
    bool ok = false;
    const QString choix = QInputDialog::getItem(this, QStringLiteral("Déplacer vers la feuille"), QStringLiteral("Feuille"), noms, 0, false, &ok);
    if (ok) deplacerVersFeuille(vueSchema()->selection(), static_cast<int>(noms.indexOf(choix)));
  };
  c["circuit.reproduireCanal"] = [this] {
    const prom::ecad::ConceptionElec* cc = conceptionCourante();
    if (!cc) return;
    for (std::size_t i = 0; i < cc->feuilles.size(); ++i)
      if (cc->feuilles[i].repetitions > 1) {
        reproduireCanal(static_cast<int>(i));
        return;
      }
    afficherMessage(QStringLiteral("Aucune feuille de canal : répète d’abord une feuille (Canal ×…)."));
  };
  c["boitier.facade"] = [this] { ouvrirFacade(); };
  c["regles.contraintes"] = [this] { ouvrirContraintes(); };
  c["circuit.routerComplet"] = [this] { routerAutomatiquement(true); };
  c["fabrication.panneau"] = [this] { exporterPanneau(); };
  c["fabrication.devis"] = [this] { ouvrirDevis(); };
  c["fabrication.controler"] = [this] { controlerFichiersEcrits(); };
  c["fabrication.visionneuse"] = [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Visionneuse HTML"), QStringLiteral("visionneuse.html"), QStringLiteral("Pages (*.html)"));
    if (!f.isEmpty()) exporterVisionneuse(f);
  };
  c["boitier.impression"] = [this] {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier des fichiers STL"));
    if (!d.isEmpty()) exporterImpression(d);
  };
  c["schema.alimentation.gnd"] = alimentation("GND");
  c["schema.alimentation.5v"] = alimentation("+5V");
  c["schema.alimentation.3v3"] = alimentation("+3V3");
  c["schema.alimentation.12v"] = alimentation("+12V");
  c["schema.alimentation.vcc"] = alimentation("VCC");
  c["schema.drapeauAlimentation"] = alimentation("Drapeau_alim");
  c["schema.champs"] = [this] { ouvrirTableauChamps(); };
  c["vue.mosaique"] = [this] { setMosaique(!mosaique()->mosaique()); };
  c["vue.tableauObjets"] = [this] { montrerTableauObjets(!mosaique()->visibleDansMosaique(tableauObjets())); };
  c["schema.annoter"] = [this] { annoter(false); };
  c["schema.renumeroter"] = [this] { annoter(true); };
  c["schema.erc"] = [this] {
    m_schema->selectionner({});
    verifier();
    majPanneau();
  };
  c["edition.chercher"] = [this] { chercher(); };
  c["composant.empreinte"] = [this] { changerEmpreinte(); };
  c["atelier.apprendre"] = [this] { montrerApprendre(); };
  c["schema.relierListe"] = [this] { relierListe(); };
  c["schema.decalerCroisements"] = [this] {
    setMode(Schema);
    const QStringList portees{QStringLiteral("Sur la sélection"), QStringLiteral("Sur cette feuille"), QStringLiteral("Sur tout le schéma")};
    bool ok = false;
    const QString choix = QInputDialog::getItem(this, QStringLiteral("Décaler les croisements"), QStringLiteral("Croisements à quatre branches changés en deux T décalés d’un pas :"), portees,
                                                m_schema->selection().empty() ? 1 : 0, false, &ok);
    if (ok) m_schema->decalerCroisementsSur(static_cast<int>(portees.indexOf(choix)));
  };
  c["schema.terminerBroches"] = [this] { m_schema->terminerBrochesChoisies(true); };
  c["edition.collerMatrice"] = [this] {
    setMode(Schema);
    m_schema->dialogueMatrice();
  };
  c["edition.collageSpecial"] = [this] {
    setMode(Schema);
    m_schema->dialogueCollageSpecial();
  };
  c["edition.copierTexte"] = [this] { m_schema->copierCommeTexte(); };
  c["vue.pleinEcran"] = [this] {
    QWidget* f = window();
    if (f->isFullScreen()) f->showNormal();
    else f->showFullScreen();
  };
  c["vue.consultation"] = [this] {
    const bool oui = !m_doc->consultation();
    m_doc->setConsultation(oui);
    if (oui) m_schema->setOutil(VueSchema::Outil::Selection);
    afficherMessage(oui ? QStringLiteral("Mode consultation : le projet se lit et se parcourt, rien ne se modifie. Affichage › Consultation le quitte.")
                        : QStringLiteral("Mode consultation quitté : les modifications sont de nouveau permises."));
  };
  c["vue.cadrageEnregistrer"] = [this] {
    setMode(Schema);
    bool ok = false;
    const QString nom = QInputDialog::getText(this, QStringLiteral("Enregistrer ce cadrage"), QStringLiteral("Nom du cadrage :"), QLineEdit::Normal,
                                              QStringLiteral("Cadrage %1").arg(m_schema->cadrages().size() + 1), &ok);
    if (ok) m_schema->enregistrerCadrage(nom);
  };
  c["vue.etalonner"] = [this] { etalonnerEcran(); };
  c["atelier.mosaique"] = [this] { Vignettes::ouvrirMosaique(*this); };
  c["selection.memoires"] = [this] {
    setMode(Schema);
    m_schema->basculerMemoires();
  };
  c["schema.replacerChamps"] = [this] { m_schema->replacerChampsChoisis(); };
  c["schema.echanger"] = [this] { m_schema->echangerChoisis(); };
  c["verif.suivant"] = [this] {
    if (m_dockVerifications->isHidden()) montrerVerifications();
    if (!m_verifications->suivant()) afficherMessage(QStringLiteral("Aucun problème à montrer : « Vérifier » (F7) refait la vérification complète."));
  };
  c["atelier.synchroniser"] = [this] { mettreAJourCircuit(); };
  c["atelier.raccourcis"] = [this] { montrerRaccourcis(); };
  c["commun.inspecteur"] = [this] { ouvrirInspecteur(); };
  c["schema.creerCircuit"] = [this] { creerCircuit(); };
  c["schema.feuilleNouvelle"] = [this] {
    modifierConception([](ConceptionElec& x) {
      Feuille f;
      f.uuid = nouvelUuid();
      f.nom = "Feuille " + std::to_string(x.feuilles.size() + 1);
      x.feuilles.push_back(f);
    });
    if (const ConceptionElec* x = conceptionCourante()) m_schema->setConception(m_conception, static_cast<int>(x->feuilles.size()) - 1);
  };
  c["schema.feuilleSuivante"] = [this] {
    if (const ConceptionElec* x = conceptionCourante()) m_schema->setConception(m_conception, (m_schema->feuille() + 1) % static_cast<int>(x->feuilles.size()));
    majPanneau();
  };
  c["schema.feuillePrecedente"] = [this] {
    if (const ConceptionElec* x = conceptionCourante()) {
      const int n = static_cast<int>(x->feuilles.size());
      m_schema->setConception(m_conception, (m_schema->feuille() + n - 1) % n);
    }
    majPanneau();
  };
  c["schema.feuilleSupprimer"] = [this] { supprimerFeuille(m_schema->feuille()); };
  c["circuit.routerAuto"] = [this] { routerAutomatiquement(); };
  c["circuit.zone"] = outilCircuit(OC::Zone);
  c["circuit.contour"] = outilCircuit(OC::Contour);
  c["circuit.decoupe"] = outilCircuit(OC::Decoupe);
  c["circuit.texte"] = outilCircuit(OC::Texte);
  c["circuit.mesurer"] = outilCircuit(OC::Mesure);
  c["circuit.drc"] = [this] {
    m_circuit->choisir(VueCircuit::Genre::Rien, {});
    recalculer();
    majPanneau();
  };
  c["vue3d.recadrer"] = [this] { m_3d->recadrer(); };
  using F3 = Navigation3D::Face;
  for (const auto& [id, face] : std::vector<std::pair<const char*, F3>>{{"vue3d.dessus", F3::Dessus}, {"vue3d.dessous", F3::Dessous}, {"vue3d.avant", F3::Avant}, {"vue3d.arriere", F3::Arriere},
                                                                         {"vue3d.gauche", F3::Gauche}, {"vue3d.droite", F3::Droite}, {"vue3d.iso", F3::Iso}})
    c[id] = [this, face = face] {
      setMode(Vue3D);
      m_3d->navigation().vueStandard(face);
    };
  c["vue3d.quartDeTour"] = [this] { m_3d->navigation().quartDeTour(true); };
  c["vue3d.projection"] = [this] {
    m_3d->navigation().basculerProjection();
    afficherMessage(m_3d->navigation().orthographique() ? QStringLiteral("Projection orthographique.") : QStringLiteral("Projection en perspective."), NiveauMessage::Information);
  };
  c["vue3d.grille"] = [this] {
    bool ok = false;
    const int pas = QInputDialog::getInt(this, QStringLiteral("Grille de la 3D"), QStringLiteral("Pas de la grille au sol (mm, 0 : aucune) :"), static_cast<int>(m_3d->navigation().grille()), 0, 10, 1, &ok);
    if (ok) m_3d->navigation().setGrille(pas);
  };
  c["vue3d.enregistrerVue"] = [this] {
    bool ok = false;
    const QString nom = QInputDialog::getText(this, QStringLiteral("Point de vue"), QStringLiteral("Nom du point de vue :"), QLineEdit::Normal, QStringLiteral("Face USB"), &ok);
    if (ok) enregistrerPointDeVue3D(nom);
  };
  c["vue3d.pointsDeVue"] = [this] {
    const ConceptionElec* cc = conceptionCourante();
    if (!cc || cc->pointsDeVue.empty()) {
      afficherMessage(QStringLiteral("Aucun point de vue enregistré : « Garder la vue… » en ajoute un."), NiveauMessage::Information);
      return;
    }
    QStringList noms;
    for (const auto& v : cc->pointsDeVue) noms << QString::fromStdString(v.nom);
    bool ok = false;
    const QString nom = QInputDialog::getItem(this, QStringLiteral("Points de vue"), QStringLiteral("Revenir à :"), noms, 0, false, &ok);
    if (!ok) return;
    for (const auto& v : cc->pointsDeVue)
      if (QString::fromStdString(v.nom) == nom) {
        setMode(Vue3D);
        appliquerPointDeVue3D(v);
      }
  };
  c["vue3d.apparence"] = [this] {
    auto* d = new DialogueApparence3D(*this, this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->show();
  };
  c["vue3d.mesurer"] = [this] {
    std::vector<std::string> ids;
    for (const auto& id : m_selection->composants()) ids.push_back(id);
    if (ids.size() != 2) {
      afficherMessage(QStringLiteral("Choisis exactement deux composants pour les mesurer."), NiveauMessage::Information);
      return;
    }
    mesurerCorps3D(ids[0], ids[1]);
  };
  c["vue3d.rendu"] = [this] { ouvrirRendu(); };
  c["carte.boitierCommerce"] = [this] { importerBoitierCommerce(); };
  c["carte.pieceMontee"] = [this] { ajouterPieceMontee(); };
  c["carte.exporterIdf"] = [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Carte en IDF 3.0"), QStringLiteral("carte.emn"), QStringLiteral("IDF (*.emn)"));
    if (!f.isEmpty()) exporterIdf(f);
  };
  c["carte.importerIdf"] = [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Carte IDF 3.0"), {}, QStringLiteral("IDF (*.emn)"));
    if (!f.isEmpty()) importerIdf(f);
  };
  c["fabrication.valider"] = [this] {
    recalculer();
    majPanneau();
  };
  c["fabrication.publier"] = [this] { publierInteractif(); };
  c["fabrication.dossier"] = [this] { exporterFabricationInteractif(); };
  c["fabrication.dessinSchema"] = [this] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Dessin du schéma"), q(x->nom) + QStringLiteral(".pdf"), QStringLiteral("PDF (*.pdf)"));
    if (!f.isEmpty() && exporterDessin(f)) afficherMessage(QStringLiteral("<b>Dessin enregistré</b> en PDF."), NiveauMessage::Succes);
  };
  c["fabrication.typon"] = [this] { exporterTyponInteractif(); };
  c["fabrication.planMontage"] = [this] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Plan de montage"), q(x->nom) + QStringLiteral(" - plan de montage.pdf"), QStringLiteral("PDF (*.pdf)"));
    if (!f.isEmpty() && exporterPlanMontage(f)) afficherMessage(QStringLiteral("<b>Plan de montage enregistré</b> en PDF."), NiveauMessage::Succes);
  };
  c["fabrication.nomenclature"] = [this] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Nomenclature"), q(x->nom) + QStringLiteral("-nomenclature.csv"), QStringLiteral("CSV (*.csv)"));
    if (f.isEmpty()) return;
    QFile fi(f);
    if (fi.open(QIODevice::WriteOnly)) fi.write(QByteArray::fromStdString(csvNomenclature(*x)));
  };
  c["fabrication.reseaux"] = [this] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Liste des réseaux"), q(x->nom) + QStringLiteral("-reseaux.txt"), QStringLiteral("Texte (*.txt)"));
    if (f.isEmpty()) return;
    QFile fi(f);
    if (fi.open(QIODevice::WriteOnly)) fi.write(QByteArray::fromStdString(netlistTexte(*x, m_calculs.k)));
  };
  auto enregistrerTexte = [this](const QString& titre, const QString& nom, const QString& filtre, const std::function<std::string(const ConceptionElec&)>& produire) {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const QString f = QFileDialog::getSaveFileName(this, titre, q(x->nom) + nom, filtre);
    if (f.isEmpty()) return;
    try {
      prom::ecrireFichierAtomique(prom::cheminFichier(f.toStdString()), produire(*x));
      afficherMessage(QStringLiteral("<b>%1</b> enregistré.").arg(titre), NiveauMessage::Succes);
    } catch (const std::exception& e) {
      afficherMessage(QStringLiteral("%1 : %2").arg(titre, QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    }
  };
  c["fabrication.kicad"] = [enregistrerTexte] {
    enregistrerTexte(QStringLiteral("Carte KiCad"), QStringLiteral(".kicad_pcb"), QStringLiteral("Cartes KiCad (*.kicad_pcb)"), [](const ConceptionElec& x) { return kicadDepuisConception(x, connectivite(x)); });
  };
  c["fabrication.netlistKicad"] = [enregistrerTexte] {
    enregistrerTexte(QStringLiteral("Liste de réseaux KiCad"), QStringLiteral(".net"), QStringLiteral("Listes de réseaux (*.net)"), [](const ConceptionElec& x) { return netlistKicad(x, connectivite(x)); });
  };
  c["fabrication.ipc"] = [enregistrerTexte] {
    enregistrerTexte(QStringLiteral("Liste IPC-D-356"), QStringLiteral(".ipc"), QStringLiteral("IPC-D-356 (*.ipc *.d356)"), [](const ConceptionElec& x) { return ipcD356(x, connectivite(x)); });
  };
  c["fabrication.boitier"] = [this] { lierAuBoitier(); };
  // Échanges (lot L10)
  c["fabrication.dsn"] = [enregistrerTexte] {
    enregistrerTexte(QStringLiteral("Carte Specctra pour un routeur externe"), QStringLiteral(".dsn"), QStringLiteral("Specctra (*.dsn)"), [](const ConceptionElec& x) { return specctraDsn(x); });
  };
  c["fabrication.ses"] = [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Session du routeur externe"), {}, QStringLiteral("Sessions Specctra (*.ses)"));
    if (!f.isEmpty()) importerSessionSpecctra(f);
  };
  c["fabrication.dxfMeca"] = [enregistrerTexte] {
    enregistrerTexte(QStringLiteral("DXF de la mécanique de la carte"), QStringLiteral(".dxf"), QStringLiteral("DXF (*.dxf)"), [](const ConceptionElec& x) { return dxfMecaniqueCarte(x); });
  };
  c["fabrication.boardview"] = [enregistrerTexte] {
    enregistrerTexte(QStringLiteral("BoardView pour la réparation"), QStringLiteral(".brd"), QStringLiteral("BoardView (*.brd)"), [](const ConceptionElec& x) { return boardView(x); });
  };
  c["fabrication.ipc2581"] = [enregistrerTexte] {
    enregistrerTexte(QStringLiteral("IPC-2581"), QStringLiteral(".xml"), QStringLiteral("IPC-2581 (*.xml *.cvg)"), [](const ConceptionElec& x) { return ipc2581(x); });
  };
  c["fabrication.importerNetlist"] = [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Netlist à appliquer"), {},
                                                   QStringLiteral("Netlists (*.net *.txt *.asc *.edf *.edn *.edif *.frp *.cir *.sp *.spi);;Tous les fichiers (*)"));
    if (f.isEmpty()) return;
    const QString csv = QFileInfo(f).dir().filePath(QFileInfo(f).completeBaseName() + QStringLiteral("-changements.csv"));
    importerNetlist(f, csv);
  };
  c["simulation.ouvrir"] = [this] { ouvrirSimulation(); };
  c["outils.conception"] = [this] { ouvrirOutilsConception(); };
  c["fabrication.gerberCarte"] = [this] {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier Gerber et perçages à relire en carte"));
    if (!d.isEmpty()) ajouterCarteDepuisGerber(d);
  };
  enregistrerCommandesCuivre();  // lot L5 (OutilsCuivre.cpp)
  enregistrerCommandesL8();      // 0.34, lot L8 (OutilsFabrication034.cpp)
  enregistrerCommandesL10();     // 0.34, lot L10 (ImportsTiers034.cpp)
  enregistrerCommandesL11();     // 0.34, lot L11 (OutilsSimulation034.cpp)
  enregistrerCommandesL13();     // 0.34, lot L13 (Automatisation034.cpp)
  enregistrerCommandesL14();     // 0.34, lot L14 (Laboratoire034.cpp)
  enregistrerCommandesL15();     // 0.34, lot L15 (Embarque034.cpp)
}

// Couche active, couches visibles et opacité des cuivres inactifs (EL-25)
QWidget* AtelierElectronique::controleCouches() {
  auto* boite = new QWidget;
  auto* v = new QVBoxLayout(boite);
  v->setContentsMargins(6, 4, 6, 0);
  m_coucheActive = new QComboBox;
  m_coucheActive->setToolTip(QStringLiteral("Couche active (Page préc., Page suiv.) ; pendant le routage, la liste Via du bandeau pose la via"));
  marquerAction(m_coucheActive, "circuit.coucheDessus");
  marquerAction(m_coucheActive, "circuit.coucheDessous");
  connect(m_coucheActive, &QComboBox::currentIndexChanged, this, [this](int) {
    if (m_coucheActive->currentData().isValid()) m_circuit->setCoucheActive(static_cast<Couche>(m_coucheActive->currentData().toInt()));
  });
  v->addWidget(m_coucheActive);
  auto* visibles = new QToolButton;
  visibles->setText(QStringLiteral("Couches visibles"));
  visibles->setPopupMode(QToolButton::InstantPopup);
  auto* menu = new QMenu(visibles);
  for (int i = 0; i < NB_COUCHES; ++i) {
    const Couche c = static_cast<Couche>(i);
    if (c == Couche::CuivreInt1 || c == Couche::VernisDessus || c == Couche::SerigraphieDessus || c == Couche::Contour || c == Couche::EncombrementDessus || c == Couche::FabricationDessus) menu->addSeparator();
    QAction* a = menu->addAction(QStringLiteral("%1  (%2)").arg(QString::fromUtf8(nomCouche(c)), QString::fromUtf8(codeCouche(c))));
    a->setCheckable(true);
    a->setChecked(m_circuit->coucheVisible(c));
    a->setData(i);
    connect(a, &QAction::toggled, this, [this, c](bool on) { m_circuit->setCoucheVisible(c, on); });
  }
  menu->addSeparator();
  QAction* tout = menu->addAction(QStringLiteral("Tout afficher"));
  connect(tout, &QAction::triggered, this, [menu] { for (QAction* a : menu->actions()) if (a->isCheckable()) a->setChecked(true); });
  QAction* cuivre = menu->addAction(QStringLiteral("Cuivre seulement"));
  connect(cuivre, &QAction::triggered, this, [menu] {
    for (QAction* a : menu->actions())
      if (a->isCheckable()) a->setChecked(estCuivre(static_cast<Couche>(a->data().toInt())) || static_cast<Couche>(a->data().toInt()) == Couche::Contour);
  });
  QMenu* opacite = menu->addMenu(QStringLiteral("Opacité des cuivres inactifs"));
  for (int pc : {15, 30, 45, 70, 100}) {
    QAction* a = opacite->addAction(QStringLiteral("%1 %").arg(pc));
    connect(a, &QAction::triggered, this, [this, pc] {
      m_circuit->setOpaciteInactives(pc / 100.0);
      m_doc->reglerSansHistorique([&](prom::Projet& p) {
        if (m_conception >= 0 && m_conception < static_cast<int>(p.electronique.size())) p.electronique[static_cast<std::size_t>(m_conception)].circuit.affichage.opaciteInactives = pc / 100.0;
      });
    });
  }
  visibles->setMenu(menu);
  v->addWidget(visibles);
  auto* l = new QLabel(QStringLiteral("Couche active"));
  l->setAlignment(Qt::AlignHCenter);
  l->setStyleSheet(QStringLiteral("color: #4A5270; font-size: 11px;"));
  v->addWidget(l);
  return boite;
}

// Grille et unité (EL-37) : pas métriques et pas au pouce (2,54 et 1,27 mm), mm, mil ou pouce
QWidget* AtelierElectronique::controleGrille() {
  auto* boite = new QWidget;
  auto* v = new QVBoxLayout(boite);
  v->setContentsMargins(6, 4, 6, 0);
  m_grille = new QComboBox;
  m_grille->setToolTip(QStringLiteral("Pas de la grille du placement et du routage"));
  m_grille->addItem(QStringLiteral("Grille automatique"), 0);
  for (const auto& [texte, pas] : std::vector<std::pair<const char*, int>>{{"0,025 mm", 25'000}, {"0,05 mm", 50'000}, {"0,1 mm", 100'000}, {"0,25 mm", 250'000}, {"0,5 mm", 500'000},
                                                                           {"1 mm", 1'000'000}, {"5 mil (0,127 mm)", 127'000}, {"10 mil (0,254 mm)", 254'000}, {"25 mil (0,635 mm)", 635'000},
                                                                           {"50 mil (1,27 mm)", 1'270'000}, {"100 mil (2,54 mm)", 2'540'000}})
    m_grille->addItem(QString::fromUtf8(texte), pas);
  connect(m_grille, &QComboBox::activated, this, [this](int) {
    const Nm pas = m_grille->currentData().toLongLong();
    m_doc->reglerSansHistorique([&](prom::Projet& p) {
      if (m_conception >= 0 && m_conception < static_cast<int>(p.electronique.size())) p.electronique[static_cast<std::size_t>(m_conception)].circuit.affichage.grille = pas;
    });
    m_circuit->setFocus();
  });
  v->addWidget(m_grille);
  m_unite = new QComboBox;
  m_unite->setToolTip(QStringLiteral("Unité des champs, des mesures et des coordonnées"));
  m_unite->addItem(QStringLiteral("millimètres"), QStringLiteral("mm"));
  m_unite->addItem(QStringLiteral("mil (millième de pouce)"), QStringLiteral("mil"));
  m_unite->addItem(QStringLiteral("pouces"), QStringLiteral("in"));
  connect(m_unite, &QComboBox::activated, this, [this](int) {
    const std::string u = m_unite->currentData().toString().toStdString();
    m_doc->reglerSansHistorique([&](prom::Projet& p) {
      if (m_conception >= 0 && m_conception < static_cast<int>(p.electronique.size())) p.electronique[static_cast<std::size_t>(m_conception)].circuit.affichage.unite = u;
    });
    majPanneau();
  });
  v->addWidget(m_unite);
  auto* l = new QLabel(QStringLiteral("Grille et unité"));
  l->setAlignment(Qt::AlignHCenter);
  l->setStyleSheet(QStringLiteral("color: #4A5270; font-size: 11px;"));
  v->addWidget(l);
  return boite;
}

// Le ruban, construit d'après sa description (1.20), qui ne cite que des identifiants du registre ; reconstruit à
// chaque changement de la description (réglages, scripts)
void AtelierElectronique::construireRuban() {
  if (m_commandes.empty()) enregistrerCommandes();
  const RegistreActions& registre = RegistreActions::atelierElectronique();
  const int courant = m_ruban->currentIndex();
  const QSignalBlocker bloque(m_ruban);
  // L'ancien ruban quitte l'atelier tout de suite (plus aucun de ses boutons n'y est trouvé), puis il est détruit
  // quand la main revient à la boucle : un de ses boutons peut être en train d'agir
  while (m_ruban->count() > 0) {
    QWidget* w = m_ruban->widget(0);
    m_ruban->removeTab(0);
    w->hide();
    w->setParent(nullptr);
    w->deleteLater();
  }
  m_coucheActive = m_grille = m_unite = nullptr;
  m_actionCreerCircuit = m_actionMajDepuisSchema = nullptr;
  // Une entrée : l'action du registre, son nom, son icône, sa bulle avec ses touches
  auto entree = [this, &registre](const std::string& id, QObject* parent) -> QAction* {
    const ActionAtelier* a = registre.action(id);
    if (!a || a->controle) return nullptr;
    auto* x = new QAction(icone(a->icone.isEmpty() ? QStringLiteral("aide") : a->icone), a->nomAffiche(), parent);
    const QString touches = registre.touches(id);
    QString bulle = a->nomAffiche();
    if (!a->bulle.isEmpty()) bulle += QStringLiteral(" — ") + a->bulle;
    if (!touches.isEmpty()) bulle += QStringLiteral(" (%1)").arg(touches);
    x->setToolTip(bulle);
    x->setObjectName(QStringLiteral("ruban:") + QString::fromStdString(id));
    marquerAction(x, id);
    connect(x, &QAction::triggered, this, [this, id] { executerAction(id); });
    return x;
  };
  auto gros = [&](QToolBar* b, const DescriptionRuban::Groupe& g) {
    auto* t = new QToolButton;
    t->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    t->setIconSize(QSize(30, 30));
    t->setAutoRaise(true);
    std::vector<QAction*> acts;
    for (const std::string& id : g.actions)
      if (QAction* x = entree(id, t)) {
        x->setIconText(g.titre.isEmpty() ? x->text() : g.titre);
        acts.push_back(x);
      }
    if (acts.empty()) {
      delete t;
      return;
    }
    const QString titre = acts.front()->iconText();
    t->setMinimumWidth(std::max(62, QFontMetrics(t->font()).horizontalAdvance(titre) + (acts.size() > 1 ? 30 : 16)));
    t->setProperty("rubanLargeur", t->minimumWidth());  // ajusterRuban la rend quand la place revient
    t->setDefaultAction(acts.front());
    if (acts.size() > 1) {
      auto* m = new QMenu(t);
      for (QAction* x : acts) m->addAction(x);
      t->setMenu(m);
      t->setPopupMode(QToolButton::MenuButtonPopup);
    }
    QAction* place = b->addWidget(t);
    // Les entrées dont l'atelier règle la présence : Circuit (schéma seul) et Mettre à jour au schéma
    if (acts.front()->objectName() == QLatin1String("ruban:schema.creerCircuit")) {
      t->setObjectName(QStringLiteral("creerCircuit"));
      m_actionCreerCircuit = place;
    }
    if (b->objectName() == QLatin1String("ruban:schema") && acts.front()->objectName() == QLatin1String("ruban:atelier.synchroniser")) m_actionMajDepuisSchema = place;
  };
  auto grille = [&](QToolBar* b, const DescriptionRuban::Groupe& g) {
    auto* boite = new QWidget;
    auto* v = new QVBoxLayout(boite);
    v->setContentsMargins(4, 0, 4, 0);
    v->setSpacing(1);
    auto* grilleCases = new QGridLayout;
    grilleCases->setSpacing(1);
    int k = 0;
    for (const std::string& id : g.actions) {
      QAction* x = entree(id, boite);
      if (!x) continue;
      auto* t = new QToolButton;
      t->setIconSize(QSize(20, 20));
      t->setAutoRaise(true);
      t->setDefaultAction(x);
      grilleCases->addWidget(t, k % 2, k / 2);
      ++k;
    }
    v->addLayout(grilleCases);
    auto* l = new QLabel(g.titre);
    l->setAlignment(Qt::AlignHCenter);
    l->setStyleSheet(QStringLiteral("color: #4A5270; font-size: 11px;"));
    v->addWidget(l);
    b->addWidget(boite);
  };
  for (const DescriptionRuban::Onglet& onglet : RubanEnCours::instance().description().onglets) {
    auto* b = new QToolBar;
    b->setObjectName(QStringLiteral("ruban:") + QString::fromStdString(onglet.id));
    b->setIconSize(QSize(30, 30));
    m_ruban->addTab(b, onglet.titre);
    for (const DescriptionRuban::Groupe& g : onglet.groupes) switch (g.forme) {
        case DescriptionRuban::Forme::Separateur: b->addSeparator(); break;
        case DescriptionRuban::Forme::Grille: grille(b, g); break;
        case DescriptionRuban::Forme::Controle:
          for (const std::string& id : g.actions) {
            QWidget* w = id == "circuit.coucheActive" ? controleCouches() : id == "circuit.grilleUnite" ? controleGrille() : nullptr;
            if (w) marquerAction(b->addWidget(w), id);
          }
          break;
        case DescriptionRuban::Forme::Grand: gros(b, g); break;
      }
  }
  if (courant >= 0) m_ruban->setCurrentIndex(std::min(courant, m_ruban->count() - 1));
  majSchemaSeul();
  majCouches();
  ajusterRuban();
}

// Le ruban se replie selon la largeur (2.15, G12) : un onglet qui ne tient plus montre ses grands boutons en icônes
// seules (leur nom reste dans la bulle), plutôt que de cacher des commandes derrière le bouton de débordement
void AtelierElectronique::ajusterRuban() {
  if (!m_ruban) return;
  const int dispo = m_ruban->width() - 12;
  for (int i = 0; i < m_ruban->count(); ++i) {
    auto* b = qobject_cast<QToolBar*>(m_ruban->widget(i));
    if (!b) continue;
    std::vector<QToolButton*> gros;
    for (QToolButton* t : b->findChildren<QToolButton*>())
      if (t->property("rubanLargeur").isValid()) gros.push_back(t);
    auto largeur = [b] {
      int l = 0;
      for (QAction* x : b->actions())
        if (x->isVisible())
          if (QWidget* w = b->widgetForAction(x)) l += std::max(w->sizeHint().width(), w->minimumWidth()) + b->layout()->spacing();
      return l + 2 * b->layout()->contentsMargins().left() + 16;
    };
    for (QToolButton* t : gros) {
      t->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
      t->setMinimumWidth(t->property("rubanLargeur").toInt());
    }
    if (largeur() <= dispo) continue;
    for (QToolButton* t : gros) {
      t->setToolButtonStyle(Qt::ToolButtonIconOnly);
      t->setMinimumWidth(0);
    }
  }
}

void AtelierElectronique::majCouches() {
  const ConceptionElec* c = conceptionCourante();
  if (!c || !m_coucheActive) return;
  const QSignalBlocker b(m_coucheActive);
  m_coucheActive->clear();
  for (Couche x : couchesCuivre(c->circuit.empilage.couchesCuivre)) m_coucheActive->addItem(QString::fromUtf8(nomCouche(x)), static_cast<int>(x));
  m_coucheActive->setCurrentIndex(std::max(0, m_coucheActive->findData(static_cast<int>(m_circuit->coucheActive()))));
  // Réglages de travail de la carte : grille, unité, opacité
  if (m_grille) {
    const QSignalBlocker bg(m_grille);
    int i = m_grille->findData(QVariant::fromValue<qlonglong>(c->circuit.affichage.grille));
    if (i < 0) i = m_grille->findData(static_cast<int>(c->circuit.affichage.grille));
    m_grille->setCurrentIndex(std::max(0, i));
  }
  if (m_unite) {
    const QSignalBlocker bu(m_unite);
    m_unite->setCurrentIndex(std::max(0, m_unite->findData(q(c->circuit.affichage.unite))));
  }
  m_circuit->setOpaciteInactives(c->circuit.affichage.opaciteInactives);
}

// Schéma seul (STR-13) : les onglets du circuit, de la 3D et de la fabrication n'apparaissent qu'avec le circuit
void AtelierElectronique::majSchemaSeul() {
  const ConceptionElec* c = conceptionCourante();
  const bool seul = c && c->schemaSeul;
  for (int i : {static_cast<int>(Circuit), static_cast<int>(Vue3D), static_cast<int>(Fabrication)}) m_ruban->setTabVisible(i, !seul);
  if (m_actionCreerCircuit) m_actionCreerCircuit->setVisible(seul);
  if (m_actionMajDepuisSchema) m_actionMajDepuisSchema->setVisible(!seul);
  if (seul && mode() != Schema) m_ruban->setCurrentIndex(Schema);
}

// La mise à jour du circuit le crée, dans son propre pas (elle efface le drapeau « schéma seul »)
void AtelierElectronique::creerCircuit() {
  const ConceptionElec* c = conceptionCourante();
  if (!c || !c->schemaSeul) return;
  mettreAJourCircuit();
}

// Contour et découpes importés d'un DXF (EL-44) : le contour est recalé sur le coin haut gauche de la carte
// actuelle, pour que les composants déjà placés restent où ils sont
void AtelierElectronique::importerContourDxf() {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Contour de la carte (DXF)"), QString(), QStringLiteral("DXF (*.dxf *.DXF)"));
  if (chemin.isEmpty()) return;
  QFile f(chemin);
  if (!f.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, QStringLiteral("Contour DXF"), QStringLiteral("Lecture impossible : %1").arg(f.errorString()));
    return;
  }
  const ContourDxf d = lireContourDxf(f.readAll().toStdString());
  QStringList alertes;
  for (const auto& a : d.alertes) alertes << q(a);
  if (d.exterieur.empty()) {
    QMessageBox::warning(this, QStringLiteral("Contour DXF"), QStringLiteral("Aucun contour fermé dans ce fichier.\n\n%1").arg(alertes.join(QLatin1Char('\n'))));
    return;
  }
  const Boite actuel = boite(c->circuit.contour.polygone()), nouveau = boite(d.exterieur);
  const Pt decalage = Pt{actuel.x0, actuel.y0} - Pt{nouveau.x0, nouveau.y0};
  Polygone ext = d.exterieur;
  for (Pt& p : ext) p = p + decalage;
  Polygones decoupes = d.decoupes;
  for (auto& x : decoupes)
    for (Pt& p : x) p = p + decalage;
  modifierConception([&](ConceptionElec& c2) {
    c2.circuit.contour.libre = ext;
    c2.circuit.contour.decoupes = decoupes;
  });
  m_circuit->recadrer();
  afficherMessage(QStringLiteral("<b>Contour importé</b> : %1 × %2, %3 découpe(s).%4")
                      .arg(m_circuit->longueur(static_cast<double>(nouveau.largeur())), m_circuit->longueur(static_cast<double>(nouveau.hauteur())))
                      .arg(decoupes.size())
                      .arg(alertes.isEmpty() ? QString() : QStringLiteral(" <span style='color:#A8620E'>%1</span>").arg(alertes.join(QStringLiteral(" ")).toHtmlEscaped())), NiveauMessage::Avertissement);
}

void AtelierElectronique::choisirProfil(std::size_t indice, QComboBox* liste) {
  const ConceptionElec* c = conceptionCourante();
  if (!c || indice >= profilsFabricants().size()) return;
  const ProfilFabricant& p = profilsFabricants()[indice];
  const Regles& M = p.regles;
  Regles R = c->circuit.regles;
  QStringList changes;
  auto relever = [&](const QString& quoi, Nm& valeur, Nm minimum) {
    if (valeur >= minimum) return;
    changes << QStringLiteral("%1 : %2 → %3").arg(quoi, m_circuit->longueur(static_cast<double>(valeur)), m_circuit->longueur(static_cast<double>(minimum)));
    valeur = minimum;
  };
  relever(QStringLiteral("Isolement"), R.isolement, M.isolement);
  relever(QStringLiteral("Largeur minimale de piste"), R.largeurMin, M.largeurMin);
  relever(QStringLiteral("Largeur de piste"), R.largeur, R.largeurMin);
  relever(QStringLiteral("Perçage minimal"), R.percageMin, M.percageMin);
  relever(QStringLiteral("Anneau minimal"), R.anneauMin, M.anneauMin);
  relever(QStringLiteral("Via : perçage"), R.viaPercage, R.percageMin);
  relever(QStringLiteral("Via : diamètre"), R.viaDiametre, R.viaPercage + 2 * R.anneauMin);
  relever(QStringLiteral("Distance au bord"), R.bord, M.bord);
  relever(QStringLiteral("Écart entre perçages"), R.ecartTrous, M.ecartTrous);
  relever(QStringLiteral("Cuivre au bord d’un trou"), R.cuivreTrou, M.cuivreTrou);
  for (auto& cl : R.classes) {
    const QString n = QStringLiteral("Classe « %1 », ").arg(q(cl.nom));
    relever(n + QStringLiteral("isolement"), cl.isolement, R.isolement);
    relever(n + QStringLiteral("largeur"), cl.largeur, R.largeurMin);
    relever(n + QStringLiteral("via : perçage"), cl.viaPercage, R.percageMin);
    relever(n + QStringLiteral("via : diamètre"), cl.viaDiametre, cl.viaPercage + 2 * R.anneauMin);
  }
  R.profil = p.nom;
  if (!changes.isEmpty()) {
    QMessageBox b(QMessageBox::Question, QStringLiteral("Profil « %1 »").arg(q(p.nom)),
                  QStringLiteral("%1 règle(s) sont sous les minimums de « %2 » et seront relevées. Les autres, réglées à la main, sont gardées.").arg(changes.size()).arg(q(p.nom)),
                  QMessageBox::Yes | QMessageBox::Cancel, this);
    b.setDetailedText(changes.join(QLatin1Char('\n')));
    b.button(QMessageBox::Yes)->setText(QStringLiteral("Relever et appliquer"));
    b.button(QMessageBox::Cancel)->setText(QStringLiteral("Annuler"));
    if (b.exec() != QMessageBox::Yes) {
      const QSignalBlocker bl(liste);
      liste->setCurrentText(q(c->circuit.regles.profil));
      return;
    }
  }
  modifierConception([&](ConceptionElec& c2) { c2.circuit.regles = R; });
  afficherMessage(QStringLiteral("<b>%1</b> : %2 %3").arg(q(p.nom), q(p.description),
                                                       changes.isEmpty() ? QStringLiteral("Tes règles respectaient déjà ses minimums : rien n’a changé.")
                                                                         : QStringLiteral("%1 règle(s) relevée(s).").arg(changes.size())), NiveauMessage::Information);
}

// Étalonnage de l'écran (2.29, ERG-30) : un trait de 50 mm à la densité que l'écran déclare ; mesuré à la règle,
// sa vraie longueur corrige une fois pour toutes la taille réelle
void AtelierElectronique::etalonnerEcran() {
  QDialog d(this);
  d.setObjectName(QStringLiteral("dialogueEtalonnage"));
  d.setWindowTitle(QStringLiteral("Étalonner l’écran"));
  auto* l = new QVBoxLayout(&d);
  auto* texte = new QLabel(QStringLiteral("Pose une règle sur l’écran et mesure le trait : il devrait faire 50 mm."));
  texte->setWordWrap(true);
  l->addWidget(texte);
  class Trait : public QWidget {
   public:
    explicit Trait(QWidget* p) : QWidget(p) { setMinimumHeight(40); }
    void paintEvent(QPaintEvent*) override {
      QPainter g(this);
      const double px = 50.0 * AtelierElectronique::pixelsParMm(this);
      g.setPen(QPen(theme().c().texte, 2));
      const double y = height() / 2.0, x0 = 10;
      g.drawLine(QPointF(x0, y), QPointF(x0 + px, y));
      g.drawLine(QPointF(x0, y - 8), QPointF(x0, y + 8));
      g.drawLine(QPointF(x0 + px, y - 8), QPointF(x0 + px, y + 8));
    }
  };
  auto* trait = new Trait(&d);
  trait->setMinimumWidth(static_cast<int>(50.0 * pixelsParMm(this)) + 20);
  l->addWidget(trait);
  auto* mesure = new QDoubleSpinBox(&d);
  mesure->setObjectName(QStringLiteral("longueurMesuree"));
  mesure->setRange(10, 200);
  mesure->setDecimals(1);
  mesure->setSuffix(QStringLiteral(" mm"));
  mesure->setValue(50);
  auto* f = new QFormLayout;
  f->addRow(QStringLiteral("Longueur mesurée"), mesure);
  l->addLayout(f);
  auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
  connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  l->addWidget(b);
  if (d.exec() != QDialog::Accepted) return;
  QSettings s;
  const double facteur = s.value(QStringLiteral("ecran/etalonnage"), 1.0).toDouble() * 50.0 / mesure->value();
  s.setValue(QStringLiteral("ecran/etalonnage"), facteur);
  majZoom();
  afficherMessage(QStringLiteral("Écran étalonné : la taille réelle tient compte de ta mesure."));
}

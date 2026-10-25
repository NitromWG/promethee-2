// Prométhée : enseignement dans l'atelier électronique (lot L16 de la 0.34). Voir Enseignement034.hpp.
// Licence GPL-3.0-only.
#include "Enseignement034.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "ChienDeGarde.hpp"
#include "Document.hpp"
#include "RegistreActions.hpp"
#include "ZoneMessages.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s8(const QString& s) { return s.toStdString(); }
QPushButton* bouton(const char* nom, const QString& texte) {
  auto* b = new QPushButton(texte);
  b->setObjectName(QLatin1String(nom));
  return b;
}
QString dossierTemporaire(const QString& nom) {
  std::random_device r;
  const QString d = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-%1-%2").arg(nom).arg(r()));
  QDir().mkpath(d);
  return d;
}
}  // namespace

// ======================================================================================================================
// Établissement (16.10)
// ======================================================================================================================
namespace etablissement {
namespace {
std::optional<ReglagesEtablissement> g_actifs;
QString g_fichier, g_erreur;
}  // namespace

std::optional<ReglagesEtablissement> charger(QString* chemin, QString* erreur) {
  QStringList dossiers;
  const QString force = qEnvironmentVariable("PROMETHEE_DOSSIER_PROGRAMME");
  dossiers << (force.isEmpty() ? QCoreApplication::applicationDirPath() : force);
#ifdef _WIN32
  if (const QString commun = qEnvironmentVariable("PROGRAMDATA"); !commun.isEmpty()) dossiers << QDir(commun).filePath(QStringLiteral("Promethee"));
#else
  dossiers << QStringLiteral("/etc/promethee");
#endif
  for (const QString& d : dossiers) {
    const QString f = QDir(d).filePath(QString::fromLatin1(kFichierEtablissement));
    if (!QFileInfo::exists(f)) continue;
    std::string e;
    std::optional<ReglagesEtablissement> r;
    try {
      r = lireReglagesEtablissement(prom::lireFichier(prom::cheminFichier(s8(f))), &e);
    } catch (const std::exception& x) {
      e = x.what();
    }
    if (chemin) *chemin = f;
    if (!r && erreur) *erreur = q(e);
    return r;
  }
  return std::nullopt;
}

bool appliquer(const ReglagesEtablissement& r, const QString& dossierDuFichier) {
  QSettings s;
  if (!r.dossierBibliotheques.empty()) {
    const QString d = q(resoudreDossier(s8(dossierDuFichier), r.dossierBibliotheques));
    QDir().mkpath(d);
    s.setValue(QStringLiteral("electronique/dossierBibliotheque"), d);
  }
  if (!r.profilFabricant.empty()) s.setValue(QStringLiteral("electronique/profilCartesNeuves"), q(r.profilFabricant));
  s.setValue(QStringLiteral("electronique/premierLancement"), true);
  s.setValue(QStringLiteral("etablissement/nom"), q(r.nom));
  s.setValue(QStringLiteral("etablissement/interfaceSimplifiee"), r.interfaceSimplifiee);
  if (!r.dossierClasse.empty()) s.setValue(QStringLiteral("etablissement/dossierClasse"), q(resoudreDossier(s8(dossierDuFichier), r.dossierClasse)));
  s.sync();
  g_actifs = r;
  return s.status() == QSettings::NoError;
}

const std::optional<ReglagesEtablissement>& actifs() {
  static bool lu = false;
  if (!lu) {
    lu = true;
    QString f, e;
    if (auto r = charger(&f, &e)) {
      g_fichier = f;
      appliquer(*r, QFileInfo(f).absolutePath());
    } else
      g_erreur = e;
  }
  return g_actifs;
}

bool actionMasquee(const std::string& id) {
  const auto& r = actifs();
  return r && std::find(r->actionsMasquees.begin(), r->actionsMasquees.end(), id) != r->actionsMasquees.end();
}

bool interfaceSimplifiee() {
  const auto& r = actifs();
  return r && r->interfaceSimplifiee && (r->interfaceVerrouillee || QSettings().value(QStringLiteral("etablissement/interfaceSimplifiee"), true).toBool());
}

QStringList verifierPoste() {
  QStringList l;
  int echecs = 0;
  auto noter = [&](bool ok, const QString& quoi) {
    l << (ok ? QStringLiteral("OK      ") : QStringLiteral("ÉCHEC   ")) + quoi;
    echecs += !ok;
  };
  const auto& r = actifs();
  // Paquet portable : tout s'écrit dans son dossier, sans droits d'administrateur
  if (portable::actif()) {
    const QString d = portable::dossier();
    QFile essai(QDir(d).filePath(QStringLiteral("essai-ecriture.tmp")));
    const bool ecrit = essai.open(QIODevice::WriteOnly) && essai.write("ok") == 2;
    essai.close();
    essai.remove();
    noter(ecrit, QStringLiteral("Paquet portable : données écrites dans %1").arg(d));
  } else
    noter(true, QStringLiteral("Installation classique : données dans le profil de l'utilisateur"));
  if (!g_erreur.isEmpty()) noter(false, QStringLiteral("Réglages de l'établissement illisibles : %1").arg(g_erreur));
  else if (r) {
    noter(true, QStringLiteral("Réglages de l'établissement appliqués : %1 (%2)").arg(q(r->nom), g_fichier));
    const QString d = QSettings().value(QStringLiteral("electronique/dossierBibliotheque")).toString();
    if (!r->dossierBibliotheques.empty()) {
      QFile essai(QDir(d).filePath(QStringLiteral("essai-ecriture.tmp")));
      const bool ok = QDir(d).exists() && essai.open(QIODevice::WriteOnly);
      essai.close();
      essai.remove();
      noter(ok, QStringLiteral("Dossier des bibliothèques imposé : %1").arg(d));
    }
    if (!r->profilFabricant.empty()) noter(QSettings().value(QStringLiteral("electronique/profilCartesNeuves")).toString() == q(r->profilFabricant), QStringLiteral("Profil des cartes neuves : %1").arg(q(r->profilFabricant)));
    if (r->interfaceSimplifiee) noter(interfaceSimplifiee(), QStringLiteral("Interface simplifiée%1").arg(r->interfaceVerrouillee ? QStringLiteral(", verrouillée") : QString()));
    for (const auto& a : r->actionsMasquees) noter(actionMasquee(a), QStringLiteral("Action masquée : %1").arg(q(a)));
  } else
    noter(true, QStringLiteral("Aucun réglage d'établissement : réglages du poste"));
  // Une conception d'essai : vérifiée, simulée, écrite et relue
  try {
    const ConceptionElec c = exempleDiviseur();
    AnalyseSpice a;
    a.type = AnalyseSpice::Type::PointDeRepos;
    const NetlistSpice n = netlistSpice(c, a);
    noter(n.valide(), QStringLiteral("Netlist d'un diviseur de tension"));
    if (ngspiceDisponible()) {
      const ResultatSimulation s = simuler(n);
      const auto* v = s.reel(vecteurTension(n, "MID"));
      noter(v && !v->empty() && std::abs(v->back() - 2.5) < 0.01, QStringLiteral("Simulation par %1 : 2,5 V au milieu du diviseur").arg(q(versionNgspice())));
    } else
      l << QStringLiteral("ATTENTION ngspice absent : les simulations ne marcheront pas sur ce poste");
    prom::Projet p = prom::projetVide();
    p.nom = "Essai du poste";
    p.sansCarte = true;
    p.electronique.push_back(c);
    const QString f = QDir(dossierTemporaire(QStringLiteral("poste"))).filePath(QStringLiteral("essai.promethee"));
    prom::ecrireProjet(p, s8(f));
    const prom::Projet relu = prom::lireProjet(s8(f));
    noter(relu.electronique.size() == 1 && relu.electronique[0].composants.size() == c.composants.size(), QStringLiteral("Projet écrit et relu"));
    QDir(QFileInfo(f).absolutePath()).removeRecursively();
    int prets = 0;
    for (const auto& x : recueilExemples()) {
      AnalyseSpice t;
      t.pas = 1e-5, t.duree = 1e-3;
      prets += netlistSpice(x.creer(), t).valide();
    }
    noter(prets == static_cast<int>(recueilExemples().size()), QStringLiteral("Recueil d'exemples prêts à simuler : %1 sur %2").arg(prets).arg(recueilExemples().size()));
  } catch (const std::exception& e) {
    noter(false, QStringLiteral("Essai d'une conception : %1").arg(q(e.what())));
  }
  l << QStringLiteral("Essais intégrés : %1 vert(s), %2 échec(s)").arg(l.size() - echecs).arg(echecs);
  return l;
}
}  // namespace etablissement

// ======================================================================================================================
// Fenêtre de l'enseignant
// ======================================================================================================================
FenetreEnseignement::FenetreEnseignement(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreEnseignement"));
  setWindowTitle(QStringLiteral("Enseignement"));
  resize(1100, 720);
  auto* col = new QVBoxLayout(this);
  m_onglets = new QTabWidget;
  m_onglets->setObjectName(QStringLiteral("ongletsEnseignement"));
  col->addWidget(m_onglets);
  m_etat = new QLabel;
  m_etat->setObjectName(QStringLiteral("etatEnseignement"));
  m_etat->setWordWrap(true);
  col->addWidget(m_etat);
  const std::vector<std::pair<const char*, QString>> pages{{"consignes", QStringLiteral("Consignes")},
                                                           {"enseignant", QStringLiteral("Couche de l'enseignant")},
                                                           {"evaluation", QStringLiteral("Évaluation")},
                                                           {"classe", QStringLiteral("Classe")},
                                                           {"diffusion", QStringLiteral("Page HTML")}};
  for (const auto& [id, titre] : pages) {
    auto* p = new QWidget;
    p->setObjectName(QLatin1String(id));
    m_onglets->addTab(p, titre);
    const std::string s = id;
    if (s == "consignes") construireConsignes(p);
    else if (s == "enseignant") construireEnseignant(p);
    else if (s == "evaluation") construireEvaluation(p);
    else if (s == "classe") construireClasse(p);
    else construireDiffusion(p);
  }
}

bool FenetreEnseignement::montrer(const QString& onglet) {
  recharger();
  show();
  raise();
  if (onglet.isEmpty()) return true;
  for (int i = 0; i < m_onglets->count(); ++i)
    if (m_onglets->widget(i)->objectName() == onglet) {
      m_onglets->setCurrentIndex(i);
      return true;
    }
  return false;
}

void FenetreEnseignement::recharger() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const Document* d = m_atelier.document();
  // Cartes du projet : celle de l'élève, celle de référence
  for (QComboBox* b : {m_carteEleve, m_reference}) {
    const int avant = b->currentIndex();
    b->clear();
    if (d)
      for (const auto& x : d->projet().electronique) b->addItem(q(x.nom));
    if (avant >= 0 && avant < b->count()) b->setCurrentIndex(avant);
  }
  if (!c) return;
  // Verrous : un composant par ligne, coché s'il est verrouillé
  const auto couche = coucheEnseignant(*c);
  m_verrous->clear();
  for (const auto& x : c->composants) {
    if (x.reference.empty() || x.reference[0] == '#') continue;
    auto* it = new QListWidgetItem(q(x.reference + " " + x.valeur));
    it->setData(Qt::UserRole, q(x.uuid));
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    const bool v = couche && std::find(couche->verrous.begin(), couche->verrous.end(), x.uuid) != couche->verrous.end();
    it->setCheckState(v ? Qt::Checked : Qt::Unchecked);
    m_verrous->addItem(it);
  }
  for (int i = 0; i < m_actions->count(); ++i) {
    QListWidgetItem* it = m_actions->item(i);
    const std::string id = s8(it->data(Qt::UserRole).toString());
    const bool m = couche && std::find(couche->restrictions.actionsMasquees.begin(), couche->restrictions.actionsMasquees.end(), id) != couche->restrictions.actionsMasquees.end();
    it->setCheckState(m ? Qt::Checked : Qt::Unchecked);
  }
  if (couche) m_simplifiee->setChecked(couche->restrictions.interfaceSimplifiee);
  const auto consignes = consignesDe(*c);
  if (!consignes.empty() && m_etapes->rowCount() == 0) {
    m_idConsigne->setText(q(consignes.front().id));
    m_titreConsigne->setText(q(consignes.front().titre));
    for (const auto& e : consignes.front().etapes) ajouterEtape(e);
  }
}

// ---------- Consignes ----------
namespace {
enum ColonneEtape { Titre, Texte, Condition, A, B, Min, Max, Question, Media, NColonnes };
}

void FenetreEnseignement::construireConsignes(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  auto* f = new QFormLayout;
  m_idConsigne = new QLineEdit;
  m_idConsigne->setObjectName(QStringLiteral("idConsigne"));
  m_titreConsigne = new QLineEdit;
  m_titreConsigne->setObjectName(QStringLiteral("titreConsigne"));
  f->addRow(QStringLiteral("Identifiant"), m_idConsigne);
  f->addRow(QStringLiteral("Titre"), m_titreConsigne);
  l->addLayout(f);
  m_etapes = new QTableWidget(0, NColonnes);
  m_etapes->setObjectName(QStringLiteral("etapesConsigne"));
  m_etapes->setHorizontalHeaderLabels({QStringLiteral("Titre"), QStringLiteral("Texte"), QStringLiteral("Vérification"), QStringLiteral("Composant, broche ou réseau"), QStringLiteral("Valeur ou autre broche"),
                                       QStringLiteral("Min (ou nombre)"), QStringLiteral("Max"), QStringLiteral("Question / réponse"), QStringLiteral("Média")});
  m_etapes->horizontalHeaderItem(A)->setToolTip(QStringLiteral("Composant posé : symbole (R, LED…) ; broches reliées : « R1.2 » ou un réseau ; mesure : le réseau ; réponse : la réponse attendue"));
  m_etapes->horizontalHeaderItem(B)->setToolTip(QStringLiteral("Composant posé : valeur (10k) ; broches reliées : l'autre broche ou réseau"));
  m_etapes->horizontalHeader()->setStretchLastSection(true);
  l->addWidget(m_etapes, 1);
  auto* barre = new QHBoxLayout;
  auto* ajouter = bouton("ajouterEtape", QStringLiteral("Ajouter une étape"));
  auto* retirer = bouton("retirerEtape", QStringLiteral("Retirer l'étape"));
  auto* enregistrer = bouton("enregistrerConsigne", QStringLiteral("Ranger dans le projet"));
  auto* exporter = bouton("exporterConsigne", QStringLiteral("Exporter…"));
  auto* importer = bouton("importerConsigne", QStringLiteral("Importer…"));
  m_carteEleve = new QComboBox;
  m_carteEleve->setObjectName(QStringLiteral("carteEleve"));
  auto* verifier = bouton("verifierConsigne", QStringLiteral("Vérifier sur"));
  for (QWidget* w : std::initializer_list<QWidget*>{ajouter, retirer, enregistrer, exporter, importer}) barre->addWidget(w);
  barre->addStretch();
  barre->addWidget(verifier);
  barre->addWidget(m_carteEleve);
  l->addLayout(barre);
  m_resultats = new QListWidget;
  m_resultats->setObjectName(QStringLiteral("resultatsConsigne"));
  m_resultats->setMaximumHeight(150);
  l->addWidget(m_resultats);
  connect(ajouter, &QPushButton::clicked, this, [this] {
    EtapeConsigne e;
    e.titre = "Étape " + std::to_string(m_etapes->rowCount() + 1);
    ajouterEtape(e);
  });
  connect(retirer, &QPushButton::clicked, this, [this] {
    if (m_etapes->currentRow() >= 0) m_etapes->removeRow(m_etapes->currentRow());
  });
  connect(enregistrer, &QPushButton::clicked, this, [this] { enregistrerConsigne(); });
  connect(verifier, &QPushButton::clicked, this, [this] { verifierSur(m_carteEleve->currentIndex()); });
  connect(exporter, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter la consigne"), m_idConsigne->text() + QStringLiteral(".consigne.json"));
    if (!f.isEmpty()) exporterConsigne(f);
  });
  connect(importer, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Importer une consigne"), {}, QStringLiteral("Consignes (*.consigne.json *.json)"));
    if (!f.isEmpty()) importerConsigne(f);
  });
}

void FenetreEnseignement::nouvelleConsigne(const QString& id, const QString& titre) {
  m_idConsigne->setText(id);
  m_titreConsigne->setText(titre);
  m_etapes->setRowCount(0);
  m_resultats->clear();
}

int FenetreEnseignement::ajouterEtape(const EtapeConsigne& e) {
  const int i = m_etapes->rowCount();
  m_etapes->insertRow(i);
  auto texte = [&](int col, const std::string& t) { m_etapes->setItem(i, col, new QTableWidgetItem(q(t))); };
  texte(Titre, e.titre);
  texte(Texte, e.texte);
  auto* genre = new QComboBox;
  for (const auto& [code, libelle] : conditionsProposees()) genre->addItem(q(libelle), q(code));
  genre->setCurrentIndex(std::max(0, genre->findData(QString::fromLatin1(codeCondition(e.condition.genre)))));
  m_etapes->setCellWidget(i, Condition, genre);
  const ConditionEtape& k = e.condition;
  auto nombre = [](double v) { return QString::number(v, 'g', 6).toStdString(); };
  switch (k.genre) {
    case GenreCondition::ComposantPose: texte(A, k.symbole), texte(B, k.valeur), texte(Min, std::to_string(k.nombre)), texte(Max, ""); break;
    case GenreCondition::BrochesReliees: texte(A, k.a), texte(B, k.b), texte(Min, ""), texte(Max, ""); break;
    case GenreCondition::MesureDansPlage: texte(A, k.a), texte(B, ""), texte(Min, nombre(k.min)), texte(Max, nombre(k.max)); break;
    case GenreCondition::Reponse: texte(A, k.reponse), texte(B, ""), texte(Min, ""), texte(Max, ""); break;
    default: texte(A, ""), texte(B, ""), texte(Min, ""), texte(Max, ""); break;
  }
  texte(Question, e.question);
  texte(Media, e.media);
  return i;
}

Consigne FenetreEnseignement::consigne() const {
  Consigne c;
  c.id = s8(m_idConsigne->text());
  c.titre = s8(m_titreConsigne->text());
  auto texte = [&](int i, int col) { return m_etapes->item(i, col) ? s8(m_etapes->item(i, col)->text()) : std::string(); };
  auto nombre = [&](int i, int col) { return QString::fromStdString(texte(i, col)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(); };
  for (int i = 0; i < m_etapes->rowCount(); ++i) {
    EtapeConsigne e;
    e.titre = texte(i, Titre);
    e.texte = texte(i, Texte);
    e.question = texte(i, Question);
    e.media = texte(i, Media);
    const auto* genre = qobject_cast<QComboBox*>(m_etapes->cellWidget(i, Condition));
    ConditionEtape& k = e.condition;
    k.genre = conditionDepuis(genre ? s8(genre->currentData().toString()) : std::string("aucune"));
    switch (k.genre) {
      case GenreCondition::ComposantPose: k.symbole = texte(i, A), k.valeur = texte(i, B), k.nombre = std::max(1, static_cast<int>(nombre(i, Min))); break;
      case GenreCondition::BrochesReliees: k.a = texte(i, A), k.b = texte(i, B); break;
      case GenreCondition::MesureDansPlage: k.a = texte(i, A), k.min = nombre(i, Min), k.max = nombre(i, Max); break;
      case GenreCondition::Reponse: k.reponse = texte(i, A); break;
      default: break;
    }
    c.etapes.push_back(e);
  }
  return c;
}

bool FenetreEnseignement::enregistrerConsigne() {
  const Consigne k = consigne();
  if (k.id.empty()) {
    m_etat->setText(QStringLiteral("Donne un identifiant à la consigne."));
    return false;
  }
  const bool ok = m_atelier.modifierConception(
      [&](ConceptionElec& c) {
        std::vector<Consigne> v = consignesDe(c);
        const auto it = std::find_if(v.begin(), v.end(), [&](const Consigne& x) { return x.id == k.id; });
        if (it != v.end()) *it = k;
        else v.push_back(k);
        ecrireConsignes(c, v);
      },
      QStringLiteral("Consigne « %1 »").arg(q(k.titre)));
  m_etat->setText(ok ? QStringLiteral("Consigne « %1 » rangée dans le projet : %2 étape(s).").arg(q(k.titre)).arg(k.etapes.size()) : QStringLiteral("Consigne inchangée."));
  return ok;
}

std::vector<EtatEtape> FenetreEnseignement::verifierSur(int conception) {
  std::vector<EtatEtape> r;
  const Document* d = m_atelier.document();
  if (!d || conception < 0 || conception >= static_cast<int>(d->projet().electronique.size())) return r;
  const ConceptionElec& eleve = d->projet().electronique[static_cast<std::size_t>(conception)];
  const Consigne k = consigne();
  m_resultats->clear();
  for (const auto& e : k.etapes) {
    r.push_back(verifierEtape(e, eleve));
    auto* it = new QListWidgetItem((r.back().faite ? QStringLiteral("✔ ") : QStringLiteral("✘ ")) + q(e.titre) + QStringLiteral(" — ") + q(r.back().raison));
    it->setForeground(r.back().faite ? QColor(0x2E, 0x7D, 0x32) : QColor(0xC6, 0x28, 0x28));
    m_resultats->addItem(it);
  }
  const auto faites = std::count_if(r.begin(), r.end(), [](const EtatEtape& x) { return x.faite; });
  m_etat->setText(QStringLiteral("%1 étape(s) sur %2 validée(s) sur « %3 ».").arg(faites).arg(r.size()).arg(q(eleve.nom)));
  return r;
}

bool FenetreEnseignement::exporterConsigne(const QString& chemin) {
  try {
    prom::ecrireFichierAtomique(prom::cheminFichier(s8(chemin)), consigneEnTexte(consigne()));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool FenetreEnseignement::importerConsigne(const QString& chemin) {
  std::string e;
  std::optional<Consigne> k;
  try {
    k = consigneDepuisTexte(prom::lireFichier(prom::cheminFichier(s8(chemin))), &e);
  } catch (const std::exception& x) {
    e = x.what();
  }
  if (!k) {
    m_etat->setText(q(e));
    return false;
  }
  nouvelleConsigne(q(k->id), q(k->titre));
  for (const auto& x : k->etapes) ajouterEtape(x);
  return true;
}

// ---------- Couche de l'enseignant ----------
void FenetreEnseignement::construireEnseignant(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  auto* f = new QFormLayout;
  m_secret = new QLineEdit;
  m_secret->setObjectName(QStringLiteral("secretEnseignant"));
  m_secret->setEchoMode(QLineEdit::Password);
  m_secret->setPlaceholderText(QStringLiteral("phrase connue de l'enseignant seul"));
  f->addRow(QStringLiteral("Secret du sceau"), m_secret);
  m_simplifiee = new QCheckBox(QStringLiteral("Interface simplifiée verrouillée pour l'élève"));
  m_simplifiee->setObjectName(QStringLiteral("interfaceSimplifiee"));
  f->addRow(QString(), m_simplifiee);
  l->addLayout(f);
  auto* listes = new QHBoxLayout;
  auto* v1 = new QVBoxLayout;
  v1->addWidget(new QLabel(QStringLiteral("Composants verrouillés")));
  m_verrous = new QListWidget;
  m_verrous->setObjectName(QStringLiteral("verrous"));
  v1->addWidget(m_verrous);
  auto* v2 = new QVBoxLayout;
  v2->addWidget(new QLabel(QStringLiteral("Actions masquées à l'élève")));
  m_actions = new QListWidget;
  m_actions->setObjectName(QStringLiteral("actionsMasquees"));
  for (const ActionAtelier& a : RegistreActions::atelierElectronique().actions()) {
    if (a.cachee) continue;
    auto* it = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(a.nomAffiche(), q(a.identifiant)));
    it->setData(Qt::UserRole, q(a.identifiant));
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(Qt::Unchecked);
    m_actions->addItem(it);
  }
  v2->addWidget(m_actions);
  listes->addLayout(v1);
  listes->addLayout(v2);
  l->addLayout(listes, 1);
  l->addWidget(new QLabel(QStringLiteral("Pannes cachées (appliquées à la netlist, jamais à la feuille)")));
  m_pannes = new QTableWidget(0, 5);
  m_pannes->setObjectName(QStringLiteral("pannesCachees"));
  m_pannes->setHorizontalHeaderLabels({QStringLiteral("Panne"), QStringLiteral("Composant ou réseau A"), QStringLiteral("Broche, valeur ou réseau B"), QStringLiteral("Titre"), QString()});
  m_pannes->horizontalHeader()->setStretchLastSection(true);
  m_pannes->setMaximumHeight(160);
  l->addWidget(m_pannes);
  auto* barre = new QHBoxLayout;
  auto* ajouter = bouton("ajouterPanne", QStringLiteral("Ajouter une panne"));
  auto* sceller = bouton("scellerCouche", QStringLiteral("Appliquer et sceller"));
  auto* relever = bouton("releverSceau", QStringLiteral("Relever le sceau"));
  m_sceau = new QLabel;
  m_sceau->setObjectName(QStringLiteral("etatSceau"));
  barre->addWidget(ajouter);
  barre->addStretch();
  barre->addWidget(sceller);
  barre->addWidget(relever);
  barre->addWidget(m_sceau);
  l->addLayout(barre);
  connect(ajouter, &QPushButton::clicked, this, [this] { ajouterPanne(PanneCachee{}); });
  connect(sceller, &QPushButton::clicked, this, [this] { this->sceller(); });
  connect(relever, &QPushButton::clicked, this, [this] { this->relever(); });
}

void FenetreEnseignement::setSecret(const QString& s) { m_secret->setText(s); }

bool FenetreEnseignement::verrouiller(const QString& reference, bool oui) {
  for (int i = 0; i < m_verrous->count(); ++i)
    if (m_verrous->item(i)->text().section(QLatin1Char(' '), 0, 0) == reference) {
      m_verrous->item(i)->setCheckState(oui ? Qt::Checked : Qt::Unchecked);
      return true;
    }
  return false;
}

void FenetreEnseignement::ajouterPanne(const PanneCachee& p) {
  const int i = m_pannes->rowCount();
  m_pannes->insertRow(i);
  auto* genre = new QComboBox;
  genre->addItem(QStringLiteral("coupure"));
  genre->addItem(QStringLiteral("court-circuit"));
  genre->addItem(QStringLiteral("valeur fausse"));
  genre->setCurrentIndex(static_cast<int>(p.genre));
  m_pannes->setCellWidget(i, 0, genre);
  const bool cc = p.genre == GenrePanne::CourtCircuit;
  m_pannes->setItem(i, 1, new QTableWidgetItem(q(cc ? p.a : p.composant)));
  m_pannes->setItem(i, 2, new QTableWidgetItem(q(cc ? p.b : p.genre == GenrePanne::ValeurFausse ? p.valeur : p.broche)));
  m_pannes->setItem(i, 3, new QTableWidgetItem(q(p.titre)));
}

void FenetreEnseignement::masquerAction(const QString& id, bool oui) {
  for (int i = 0; i < m_actions->count(); ++i)
    if (m_actions->item(i)->data(Qt::UserRole).toString() == id) m_actions->item(i)->setCheckState(oui ? Qt::Checked : Qt::Unchecked);
}

bool FenetreEnseignement::sceller() {
  if (m_secret->text().isEmpty()) {
    m_etat->setText(QStringLiteral("Saisis le secret du sceau : il servira au relevé."));
    return false;
  }
  CoucheEnseignant e;
  for (int i = 0; i < m_verrous->count(); ++i)
    if (m_verrous->item(i)->checkState() == Qt::Checked) e.verrous.push_back(s8(m_verrous->item(i)->data(Qt::UserRole).toString()));
  for (int i = 0; i < m_actions->count(); ++i)
    if (m_actions->item(i)->checkState() == Qt::Checked) e.restrictions.actionsMasquees.push_back(s8(m_actions->item(i)->data(Qt::UserRole).toString()));
  e.restrictions.interfaceSimplifiee = e.restrictions.verrouillee = m_simplifiee->isChecked();
  for (int i = 0; i < m_pannes->rowCount(); ++i) {
    PanneCachee p;
    const auto* genre = qobject_cast<QComboBox*>(m_pannes->cellWidget(i, 0));
    p.genre = static_cast<GenrePanne>(genre ? genre->currentIndex() : 0);
    const std::string a = m_pannes->item(i, 1) ? s8(m_pannes->item(i, 1)->text()) : std::string(), b = m_pannes->item(i, 2) ? s8(m_pannes->item(i, 2)->text()) : std::string();
    if (p.genre == GenrePanne::CourtCircuit) p.a = a, p.b = b;
    else if (p.genre == GenrePanne::ValeurFausse) p.composant = a, p.valeur = b;
    else p.composant = a, p.broche = b;
    p.titre = m_pannes->item(i, 3) ? s8(m_pannes->item(i, 3)->text()) : std::string();
    if (!a.empty()) e.pannes.push_back(p);
  }
  if (const ConceptionElec* c = m_atelier.conceptionCourante())
    for (const auto& k : consignesDe(*c)) e.consignes.push_back(k);
  const std::string secret = s8(m_secret->text());
  // La couche se pose d'un coup : le refus des parties verrouillées ne vaut qu'après elle
  Document* doc = m_atelier.document();
  if (doc) doc->autoriserCoucheEnseignant(true);
  const bool ok = m_atelier.modifierConception(
      [&](ConceptionElec& c) {
        c.extensions.erase("enseignant");
        ecrireCoucheEnseignant(c, e);
        prom::ecad::sceller(c, secret);
      },
      QStringLiteral("Couche de l'enseignant scellée"));
  if (doc) doc->autoriserCoucheEnseignant(false);
  m_atelier.appliquerRestrictions();
  m_sceau->setText(ok ? QStringLiteral("Scellée") : QStringLiteral("Inchangée"));
  m_etat->setText(QStringLiteral("Couche scellée : %1 verrou(s), %2 action(s) masquée(s), %3 panne(s) cachée(s).").arg(e.verrous.size()).arg(e.restrictions.actionsMasquees.size()).arg(e.pannes.size()));
  return ok;
}

EtatSceau FenetreEnseignement::relever() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const EtatSceau e = c ? verifierSceau(*c, s8(m_secret->text())) : EtatSceau::Absent;
  m_sceau->setText(e == EtatSceau::Intact ? QStringLiteral("Sceau intact") : e == EtatSceau::Retouche ? QStringLiteral("Fichier retouché depuis le sceau") : QStringLiteral("Pas de sceau"));
  return e;
}

// ---------- Évaluation ----------
void FenetreEnseignement::construireEvaluation(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  auto* f = new QFormLayout;
  m_reference = new QComboBox;
  m_reference->setObjectName(QStringLiteral("montageReference"));
  m_mode = new QComboBox;
  m_mode->setObjectName(QStringLiteral("modeControle"));
  m_mode->addItems({QStringLiteral("entraînement"), QStringLiteral("examen")});
  f->addRow(QStringLiteral("Montage de référence"), m_reference);
  f->addRow(QStringLiteral("Mode"), m_mode);
  l->addLayout(f);
  auto* barre = new QHBoxLayout;
  auto* rapide = bouton("controleRapide", QStringLiteral("Contrôle rapide"));
  auto* complet = bouton("controleComplet", QStringLiteral("Contrôle complet (compté)"));
  barre->addWidget(rapide);
  barre->addWidget(complet);
  barre->addStretch();
  l->addLayout(barre);
  m_ecarts = new QListWidget;
  m_ecarts->setObjectName(QStringLiteral("ecartsEvaluation"));
  l->addWidget(m_ecarts, 1);
  auto* formule = new QHBoxLayout;
  auto* fe = new QLineEdit;
  fe->setObjectName(QStringLiteral("formuleEleve"));
  fe->setPlaceholderText(QStringLiteral("formule de l'élève : R1*R2/(R1+R2)"));
  auto* fr = new QLineEdit;
  fr->setObjectName(QStringLiteral("formuleReference"));
  fr->setPlaceholderText(QStringLiteral("formule attendue"));
  auto* comparer = bouton("comparerFormules", QStringLiteral("Comparer"));
  formule->addWidget(fe);
  formule->addWidget(fr);
  formule->addWidget(comparer);
  l->addLayout(formule);
  connect(rapide, &QPushButton::clicked, this, [this] { evaluer(m_reference->currentIndex(), false); });
  connect(complet, &QPushButton::clicked, this, [this] { evaluer(m_reference->currentIndex(), true); });
  connect(comparer, &QPushButton::clicked, this, [this, fe, fr] { verifierFormule(fe->text(), fr->text()); });
}

ResultatEvaluation FenetreEnseignement::evaluer(int reference, bool complet) {
  ResultatEvaluation r;
  const Document* d = m_atelier.document();
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!d || !c || reference < 0 || reference >= static_cast<int>(d->projet().electronique.size())) return r;
  r = evaluerMontage(*c, d->projet().electronique[static_cast<std::size_t>(reference)]);
  if (complet) ++m_comptes;
  m_ecarts->clear();
  const bool examen = m_mode->currentIndex() == 1;
  // Mode examen : le résultat est enregistré pour l'enseignant, rien n'est montré à l'élève
  if (examen) {
    m_ecarts->addItem(QStringLiteral("Contrôle enregistré."));
    m_etat->setText(QStringLiteral("Mode examen : résultat enregistré, montré à l'enseignant au relevé."));
    return r;
  }
  if (r.juste) m_ecarts->addItem(QStringLiteral("✔ Montage juste : il correspond à la référence, aux repères près."));
  for (const auto& e : r.ecarts) m_ecarts->addItem(QStringLiteral("✘ ") + q(e));
  m_etat->setText(complet ? QStringLiteral("Contrôle complet n° %1 compté.").arg(m_comptes) : QStringLiteral("Contrôle rapide : non compté."));
  return r;
}

bool FenetreEnseignement::verifierFormule(const QString& eleve, const QString& reference) {
  const bool ok = formulesEquivalentes(s8(eleve), s8(reference));
  m_etat->setText(ok ? QStringLiteral("Formule juste.") : QStringLiteral("Formule différente de celle attendue."));
  return ok;
}

// ---------- Classe et diffusion ----------
void FenetreEnseignement::construireClasse(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  auto* f = new QHBoxLayout;
  m_dossierClasse = new QLineEdit(QSettings().value(QStringLiteral("etablissement/dossierClasse")).toString());
  m_dossierClasse->setObjectName(QStringLiteral("dossierClasse"));
  auto* choisir = bouton("choisirDossierClasse", QStringLiteral("…"));
  f->addWidget(new QLabel(QStringLiteral("Dossier partagé de la classe")));
  f->addWidget(m_dossierClasse, 1);
  f->addWidget(choisir);
  l->addLayout(f);
  m_rendus = new QTableWidget(0, 5);
  m_rendus->setObjectName(QStringLiteral("rendusClasse"));
  m_rendus->setHorizontalHeaderLabels({QStringLiteral("Élève"), QStringLiteral("Déposé"), QStringLiteral("Retard"), QStringLiteral("Note"), QStringLiteral("Remarques")});
  m_rendus->horizontalHeader()->setStretchLastSection(true);
  l->addWidget(m_rendus, 1);
  connect(choisir, &QPushButton::clicked, this, [this] {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier de la classe"), m_dossierClasse->text());
    if (!d.isEmpty()) setDossierClasse(d);
  });
}

void FenetreEnseignement::construireDiffusion(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  l->addWidget(new QLabel(QStringLiteral("La page HTML est autonome : schéma navigable, courbe et simulation dans le navigateur, sans réseau.")));
  auto* exporter = bouton("exporterPageHtml", QStringLiteral("Exporter la page HTML…"));
  l->addWidget(exporter);
  l->addStretch();
  connect(exporter, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Page HTML"), QStringLiteral("circuit.html"), QStringLiteral("Page HTML (*.html)"));
    if (!f.isEmpty()) exporterPage(f);
  });
}

void FenetreEnseignement::setDossierClasse(const QString& d) { m_dossierClasse->setText(d); }

std::string FenetreEnseignement::projetEnTexte() const {
  const Document* d = m_atelier.document();
  if (!d) return {};
  const QString f = QDir(dossierTemporaire(QStringLiteral("rendu"))).filePath(QStringLiteral("projet.promethee"));
  prom::ecrireProjet(d->projet(), s8(f));
  const std::string t = prom::lireFichier(prom::cheminFichier(s8(f)));
  QDir(QFileInfo(f).absolutePath()).removeRecursively();
  return t;
}

bool FenetreEnseignement::publier(const Devoir& d) {
  std::string e;
  const bool ok = publierDevoir(s8(m_dossierClasse->text()), d, projetEnTexte(), &e);
  m_etat->setText(ok ? QStringLiteral("Devoir « %1 » publié, à rendre avant le %2.").arg(q(d.titre), q(d.echeance)) : q(e));
  return ok;
}

bool FenetreEnseignement::deposer(const QString& devoir, const QString& eleve) {
  std::string e;
  const bool ok = deposerRendu(s8(m_dossierClasse->text()), s8(devoir), s8(eleve), projetEnTexte(), s8(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm"))), &e);
  m_etat->setText(ok ? QStringLiteral("Rendu de %1 déposé.").arg(eleve) : q(e));
  return ok;
}

std::vector<Rendu> FenetreEnseignement::relever(const QString& devoir) {
  const auto r = releverRendus(s8(m_dossierClasse->text()), s8(devoir));
  m_rendus->setRowCount(static_cast<int>(r.size()));
  for (int i = 0; i < static_cast<int>(r.size()); ++i) {
    const Rendu& x = r[static_cast<std::size_t>(i)];
    m_rendus->setItem(i, 0, new QTableWidgetItem(q(x.eleve)));
    m_rendus->setItem(i, 1, new QTableWidgetItem(q(x.depose)));
    m_rendus->setItem(i, 2, new QTableWidgetItem(x.enRetard ? QStringLiteral("en retard") : QString()));
    m_rendus->setItem(i, 3, new QTableWidgetItem(x.note >= 0 ? QString::number(x.note) : QString()));
    m_rendus->setItem(i, 4, new QTableWidgetItem(q(x.remarques)));
  }
  m_etat->setText(QStringLiteral("%1 rendu(s) relevé(s).").arg(r.size()));
  return r;
}

bool FenetreEnseignement::noter(const QString& devoir, const QString& eleve, double note, const QString& remarques) {
  return prom::ecad::noter(s8(m_dossierClasse->text()), s8(devoir), s8(eleve), note, s8(remarques));
}

bool FenetreEnseignement::exporterNotes(const QString& devoir, const QString& chemin) {
  try {
    prom::ecrireFichierAtomique(prom::cheminFichier(s8(chemin)), notesCsv(releverRendus(s8(m_dossierClasse->text()), s8(devoir)), s8(devoir)));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool FenetreEnseignement::exporterCartouche(const QString& devoir, const QString& chemin) {
  for (const auto& d : devoirsPublies(s8(m_dossierClasse->text())))
    if (d.id == s8(devoir)) try {
        prom::ecrireFichierAtomique(prom::cheminFichier(s8(chemin)), paquetCommonCartridge(d, projetEnTexte()));
        return true;
      } catch (const std::exception&) {
        return false;
      }
  return false;
}

bool FenetreEnseignement::exporterPage(const QString& chemin) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  try {
    prom::ecrireFichierAtomique(prom::cheminFichier(s8(chemin)), pageHtmlCircuit(*c));
    m_etat->setText(QStringLiteral("Page écrite : %1").arg(chemin));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// ======================================================================================================================
// Atelier
// ======================================================================================================================
FenetreEnseignement* AtelierElectronique::enseignement(const QString& onglet) {
  if (!m_enseignement) m_enseignement = new FenetreEnseignement(*this, this);
  m_enseignement->montrer(onglet);
  return m_enseignement;
}

QString AtelierElectronique::refusAction(const std::string& action) const {
  const ActionAtelier* a = RegistreActions::atelierElectronique().action(action);
  const QString nom = a ? a->nomAffiche() : q(action);
  if (etablissement::actionMasquee(action)) return QStringLiteral("« %1 » est masquée par les réglages de l'établissement.").arg(nom);
  if (const ConceptionElec* c = conceptionCourante(); c && !actionPermise(*c, action)) return QStringLiteral("« %1 » est masquée par l'enseignant pour ce projet.").arg(nom);
  return {};
}

bool AtelierElectronique::interfaceSimplifiee() const {
  if (etablissement::interfaceSimplifiee()) return true;
  const ConceptionElec* c = conceptionCourante();
  if (!c) return false;
  const auto e = coucheEnseignant(*c);
  return e && e->restrictions.interfaceSimplifiee;
}

void AtelierElectronique::appliquerRestrictions() { construireRuban(); }

void AtelierElectronique::enregistrerCommandesL16b() {
  auto& c = m_commandes;
  c["enseignement.consignes"] = [this] { enseignement(QStringLiteral("consignes")); };
  c["enseignement.couche"] = [this] { enseignement(QStringLiteral("enseignant")); };
  c["enseignement.evaluer"] = [this] { enseignement(QStringLiteral("evaluation")); };
  c["enseignement.classe"] = [this] { enseignement(QStringLiteral("classe")); };
  c["enseignement.page"] = [this] { enseignement(QStringLiteral("diffusion")); };
}

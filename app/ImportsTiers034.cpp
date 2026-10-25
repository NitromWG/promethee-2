// Prométhée : imports des autres logiciels (lot L10 de la 0.34). Voir ImportsTiers034.hpp. Licence GPL-3.0-only.
#include "ImportsTiers034.hpp"

#include <filesystem>
#include <set>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QDirIterator>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QProcess>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QThread>
#include <QVBoxLayout>

#include <nlohmann/json.hpp>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "FenetrePrincipale.hpp"
#include "promethee/carte3d.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/dessins_carte.hpp"
#include "promethee/exports3d.hpp"
#include "promethee/echanges_electronique.hpp"
#include "promethee/archive.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/formats_cao.hpp"
#include "promethee/netlists_tierces.hpp"
#include "promethee/reprise_existant.hpp"

using namespace prom::ecad;
using prom::Projet;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QString rouge(const QString& t) { return QStringLiteral("<span style='color:#C0392B'>%1</span>").arg(t); }
QString liste(const std::vector<std::string>& v, std::size_t max = 12) {
  QString r;
  for (std::size_t i = 0; i < v.size() && i < max; ++i) r += (r.isEmpty() ? QString() : QStringLiteral(", ")) + q(v[i]).toHtmlEscaped();
  if (v.size() > max) r += QStringLiteral(" … (%1 de plus)").arg(v.size() - max);
  return r;
}
std::optional<std::string> lireVoisin(const QDir& dossier, const std::string& nom) {
  const QString f = dossier.filePath(q(nom));
  if (!QFileInfo::exists(f)) return std::nullopt;
  try {
    return prom::lireFichier(prom::cheminFichier(f.toStdString()));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}
}  // namespace

// ======================================================================================================================
// Conception d'un autre logiciel
// ======================================================================================================================
std::map<std::string, std::string> DialogueImportConception::associationGardee(const std::string& logiciel) {
  std::map<std::string, std::string> r;
  const QString texte = QSettings().value(QStringLiteral("import/couches/") + q(logiciel)).toString();
  if (texte.isEmpty()) return r;
  try {
    const auto j = nlohmann::json::parse(texte.toStdString());
    for (auto it = j.begin(); it != j.end(); ++it)
      if (it.value().is_string()) r[it.key()] = it.value().get<std::string>();
  } catch (const std::exception&) {
  }
  return r;
}

void DialogueImportConception::garderAssociation(const AssociationCouches& a) {
  nlohmann::json j = nlohmann::json::object();
  for (const auto& [origine, code] : associationAGarder(a)) j[origine] = code;
  QSettings().setValue(QStringLiteral("import/couches/") + q(a.logiciel), q(j.dump()));
}

DialogueImportConception::DialogueImportConception(ImportTiers import, const ConceptionElec* ouverte, QWidget* parent)
    : QDialog(parent), m_import(std::move(import)), m_ouverte(ouverte) {
  setWindowTitle(QStringLiteral("Importer de %1").arg(q(m_import.logiciel)));
  setObjectName(QStringLiteral("dialogueImportConception"));
  auto* l = new QVBoxLayout(this);
  QStringList fichiers;
  for (const auto& f : m_import.fichiers) fichiers << q(f);
  l->addWidget(new QLabel(QStringLiteral("<b>%1</b> : %2 — %3 composant(s), %4 feuille(s), %5 piste(s), %6 cuivre(s).")
                              .arg(q(m_import.logiciel), fichiers.join(QStringLiteral(", ")).toHtmlEscaped())
                              .arg(m_import.conception.composants.size())
                              .arg(m_import.conception.feuilles.size())
                              .arg(m_import.conception.circuit.pistes.size())
                              .arg(m_import.conception.circuit.empilage.couchesCuivre)));
  auto* cibles = new QGroupBox(QStringLiteral("Où"));
  auto* lc = new QHBoxLayout(cibles);
  m_nouveau = new QRadioButton(QStringLiteral("Nouveau projet"));
  m_projet = new QRadioButton(QStringLiteral("Ajoutée au projet ouvert"));
  m_carte = new QRadioButton(QStringLiteral("Dans la carte ouverte"));
  m_carte->setEnabled(m_ouverte != nullptr);
  m_nouveau->setChecked(true);
  for (auto* b : {m_nouveau, m_projet, m_carte}) lc->addWidget(b);
  l->addWidget(cibles);
  // Tableau des couches
  l->addWidget(new QLabel(QStringLiteral("Couches : chaque couche d’origine va sur la couche de Prométhée choisie ; le choix est gardé pour %1.").arg(q(m_import.logiciel))));
  m_couches = new QTableWidget(0, 4);
  m_couches->setObjectName(QStringLiteral("tableCouches"));
  m_couches->setHorizontalHeaderLabels({QStringLiteral("Couche d’origine"), QStringLiteral("Fonction"), QStringLiteral("Objets"), QStringLiteral("Couche de Prométhée")});
  m_couches->horizontalHeader()->setStretchLastSection(true);
  m_couches->verticalHeader()->hide();
  const int cuivresDest = m_ouverte ? m_ouverte->circuit.empilage.couchesCuivre : 2;
  m_proposee = proposerAssociation(m_import.logiciel, m_import.conception, cuivresDest, associationGardee(m_import.logiciel));
  for (const auto& ligne : m_proposee.lignes) {
    const int r = m_couches->rowCount();
    m_couches->insertRow(r);
    m_couches->setItem(r, 0, new QTableWidgetItem(q(ligne.origine)));
    m_couches->setItem(r, 1, new QTableWidgetItem(q(ligne.fonction)));
    m_couches->setItem(r, 2, new QTableWidgetItem(QString::number(ligne.objets)));
    for (int k = 0; k < 3; ++k) m_couches->item(r, k)->setFlags(Qt::ItemIsEnabled);
    auto* choix = new QComboBox;
    choix->addItem(QStringLiteral("— ignorée —"), -1);
    for (int i = 0; i < NB_COUCHES; ++i) {
      const auto k = static_cast<Couche>(i);
      if (estCuivreInterne(k) && rangCuivre(k, MAX_CUIVRES) > MAX_CUIVRES - 2) continue;
      choix->addItem(QStringLiteral("%1 (%2)").arg(QString::fromUtf8(nomCouche(k)), QString::fromUtf8(codeCouche(k))), i);
    }
    choix->setCurrentIndex(std::max(0, choix->findData(ligne.cible ? static_cast<int>(*ligne.cible) : -1)));
    m_couches->setCellWidget(r, 3, choix);
    connect(choix, &QComboBox::currentIndexChanged, this, [this] { refaireApercu(); });
  }
  m_couches->resizeColumnsToContents();
  l->addWidget(m_couches, 1);
  m_reunir = new QCheckBox(QStringLiteral("Réunir les réseaux homonymes (sinon, ceux de la carte ajoutée prennent le suffixe « _2 »)"));
  m_reunir->setChecked(true);
  l->addWidget(m_reunir);
  m_apercu = new QLabel;
  m_apercu->setObjectName(QStringLiteral("apercuImport"));
  m_apercu->setWordWrap(true);
  l->addWidget(m_apercu);
  if (!m_import.alertes.empty()) {
    auto* a = new QLabel(QStringLiteral("<b>À vérifier</b> : ") + liste(m_import.alertes, 8));
    a->setWordWrap(true);
    l->addWidget(a);
  }
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  boutons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Importer"));
  connect(boutons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  l->addWidget(boutons);
  for (auto* b : {m_nouveau, m_projet, m_carte}) connect(b, &QRadioButton::toggled, this, [this] { refaireApercu(); });
  connect(m_reunir, &QCheckBox::toggled, this, [this] { refaireApercu(); });
  resize(760, 560);
  refaireApercu();
}

int DialogueImportConception::cible() const { return m_carte->isChecked() ? CarteOuverte : m_projet->isChecked() ? Projet : NouveauProjet; }

void DialogueImportConception::reglerCible(int c) {
  (c == CarteOuverte && m_carte->isEnabled() ? m_carte : c == Projet ? m_projet : m_nouveau)->setChecked(true);
  refaireApercu();
}

AssociationCouches DialogueImportConception::association() const {
  AssociationCouches a = m_proposee;
  for (int r = 0; r < m_couches->rowCount() && r < static_cast<int>(a.lignes.size()); ++r) {
    const auto* choix = qobject_cast<QComboBox*>(m_couches->cellWidget(r, 3));
    const int v = choix ? choix->currentData().toInt() : -1;
    a.lignes[static_cast<std::size_t>(r)].cible = v < 0 ? std::nullopt : std::optional<Couche>(static_cast<Couche>(v));
  }
  int besoin = m_ouverte ? m_ouverte->circuit.empilage.couchesCuivre : 2;
  for (const auto& ligne : a.lignes)
    if (ligne.cible && estCuivreInterne(*ligne.cible)) besoin = std::max(besoin, rangCuivre(*ligne.cible, MAX_CUIVRES) + 2);
  a.cuivresNecessaires = std::max(besoin, 2);
  return a;
}

OptionsFusion DialogueImportConception::options() const {
  OptionsFusion o;
  o.reunirHomonymes = m_reunir->isChecked();
  return o;
}

void DialogueImportConception::refaireApercu() {
  const AssociationCouches a = association();
  int ignores = 0;
  for (const auto& ligne : a.lignes)
    if (!ligne.cible) ignores += ligne.objets;
  QString t = QStringLiteral("Cuivres : %1.").arg(a.cuivresNecessaires);
  if (ignores) t += QStringLiteral(" %1 objet(s) sur des couches ignorées ne seront pas repris.").arg(ignores);
  m_reunir->setEnabled(cible() == CarteOuverte);
  if (cible() == CarteOuverte && m_ouverte) {
    ConceptionElec x = m_import.conception;
    appliquerAssociation(x, a);
    const RapportFusion f = apercuFusion(*m_ouverte, x, options());
    QStringList renum;
    for (const auto& [ancien, nouveau] : f.renumerotes) renum << QStringLiteral("%1 → %2").arg(q(ancien), q(nouveau));
    t += QStringLiteral("<br><b>Repères renumérotés</b> : %1").arg(renum.isEmpty() ? QStringLiteral("aucun") : renum.mid(0, 15).join(QStringLiteral(", ")).toHtmlEscaped());
    t += QStringLiteral("<br><b>Réseaux homonymes</b> : %1").arg(f.homonymes.empty() ? QStringLiteral("aucun") : liste(f.homonymes, 15));
    if (f.cuivresApres > f.cuivresAvant) t += QStringLiteral("<br>La carte ouverte passe de %1 à %2 cuivres.").arg(f.cuivresAvant).arg(f.cuivresApres);
    t += QStringLiteral("<br>La carte ajoutée est posée à droite de la carte ouverte ; son contour devient un dessin. Un seul Ctrl+Z retire l’import.");
  }
  m_apercu->setText(t);
}

bool FenetrePrincipale::importerConceptionTierce(const QString& chemin, int cible, bool dialogue) {
  std::string contenu, erreur;
  try {
    contenu = prom::lireFichier(prom::cheminFichier(chemin.toStdString()));
  } catch (const std::exception& e) {
    QMessageBox::warning(this, QStringLiteral("Import"), QStringLiteral("Fichier illisible : %1").arg(QString::fromUtf8(e.what())));
    return false;
  }
  const QDir dossier = QFileInfo(chemin).dir();
  auto import = prom::ecad::importerConceptionTierce(QFileInfo(chemin).fileName().toStdString(), contenu, [&](const std::string& nom) { return lireVoisin(dossier, nom); }, &erreur);
  if (!import) {
    if (dialogue) QMessageBox::warning(this, QStringLiteral("Import"), QStringLiteral("Import impossible : %1").arg(q(erreur)));
    return false;
  }
  const ConceptionElec* ouverte = nullptr;
  int indiceOuverte = -1;
  if (m_electronique && m_ateliers->currentWidget() == m_electronique && m_electronique->conception() < static_cast<int>(m_doc->projet().electronique.size())) {
    indiceOuverte = m_electronique->conception();
    ouverte = &m_doc->projet().electronique[static_cast<std::size_t>(indiceOuverte)];
  }
  DialogueImportConception d(*import, ouverte, this);
  d.reglerCible(cible);
  if (dialogue && d.exec() != QDialog::Accepted) return false;
  ConceptionElec c = d.import().conception;
  const AssociationCouches a = d.association();
  appliquerAssociation(c, a);
  DialogueImportConception::garderAssociation(a);
  const int ou = d.cible();
  if (ou == DialogueImportConception::NouveauProjet) {
    if (!confirmerAbandon()) return false;
    Projet p = prom::projetVide();
    p.sansCarte = true;
    p.nom = c.nom;
    p.electronique.push_back(c);
    m_doc->creer(p);
  } else if (ou == DialogueImportConception::Projet || indiceOuverte < 0)
    m_doc->modifier([&c](Projet& p) { p.electronique.push_back(c); }, QStringLiteral("Import de %1").arg(q(d.import().logiciel)));
  else {
    RapportFusion r;
    const OptionsFusion o = d.options();
    m_doc->modifier([&](Projet& p) { r = fusionnerConception(p.electronique[static_cast<std::size_t>(indiceOuverte)], c, o); },
                    QStringLiteral("Import de %1 dans la carte").arg(q(d.import().logiciel)));
    montrerElectronique(indiceOuverte, AtelierElectronique::Circuit);
    m_electronique->afficherMessage(QStringLiteral("<b>%1 composant(s) ajouté(s)</b> de %2 ; %3 repère(s) renuméroté(s), %4 réseau(x) homonyme(s) %5.")
                                        .arg(r.composants)
                                        .arg(q(d.import().logiciel))
                                        .arg(r.renumerotes.size())
                                        .arg(r.homonymes.size())
                                        .arg(o.reunirHomonymes ? QStringLiteral("réunis") : QStringLiteral("séparés")),
                                    NiveauMessage::Succes);
    return true;
  }
  montrerProjet();
  montrerElectronique(static_cast<int>(m_doc->projet().electronique.size()) - 1, c.schemaSeul ? AtelierElectronique::Schema : AtelierElectronique::Circuit);
  if (!d.import().alertes.empty() && m_electronique)
    m_electronique->afficherMessage(QStringLiteral("<b>Import de %1</b> : à vérifier — %2").arg(q(d.import().logiciel), liste(d.import().alertes, 6)), NiveauMessage::Avertissement);
  return true;
}

// ======================================================================================================================
// Bibliothèques d'autres logiciels
// ======================================================================================================================
DialogueImportBibliotheques::DialogueImportBibliotheques(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setWindowTitle(QStringLiteral("Bibliothèques d’autres logiciels"));
  setObjectName(QStringLiteral("dialogueImportBibliotheques"));
  auto* l = new QVBoxLayout(this);
  l->addWidget(new QLabel(QStringLiteral("Symboles (.kicad_sym), empreintes (.kicad_mod, dossiers .pretty) et bibliothèques EAGLE (.lbr) : chaque objet lu a sa ligne au rapport.")));
  auto* barre = new QHBoxLayout;
  auto* fichiers = new QPushButton(QStringLiteral("Fichiers…"));
  auto* dossierPretty = new QPushButton(QStringLiteral("Dossier .pretty…"));
  auto* remplacer = new QCheckBox(QStringLiteral("Remplacer les objets de même identifiant"));
  auto* equipe = new QCheckBox(QStringLiteral("Vers une bibliothèque d’équipe (fichier)"));
  auto* csv = new QPushButton(QStringLiteral("Enregistrer le rapport (CSV)…"));
  for (QWidget* w : std::initializer_list<QWidget*>{fichiers, dossierPretty, remplacer, equipe}) barre->addWidget(w);
  barre->addStretch();
  barre->addWidget(csv);
  l->addLayout(barre);
  m_table = new QTableWidget(0, 4);
  m_table->setObjectName(QStringLiteral("rapportBibliotheques"));
  m_table->setHorizontalHeaderLabels({QStringLiteral("Genre"), QStringLiteral("Objet"), QStringLiteral("État"), QStringLiteral("Détail")});
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->verticalHeader()->hide();
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  l->addWidget(m_table, 1);
  m_bilan = new QLabel;
  m_bilan->setWordWrap(true);
  l->addWidget(m_bilan);
  auto lancer = [this, remplacer, equipe](const QStringList& f) {
    if (f.isEmpty()) return;
    QString dest;
    if (equipe->isChecked()) {
      dest = QFileDialog::getSaveFileName(this, QStringLiteral("Bibliothèque d’équipe"), QStringLiteral("bibliotheque-equipe.json"), QStringLiteral("Bibliothèque Prométhée (*.json)"), nullptr,
                                          QFileDialog::DontConfirmOverwrite);
      if (dest.isEmpty()) return;
    }
    importer(f, dest, remplacer->isChecked());
  };
  connect(fichiers, &QPushButton::clicked, this, [this, lancer] {
    lancer(QFileDialog::getOpenFileNames(this, QStringLiteral("Bibliothèques"), {}, QStringLiteral("Bibliothèques (%1)").arg(q(filtreBibliotheques()))));
  });
  connect(dossierPretty, &QPushButton::clicked, this, [this, lancer] {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier d’empreintes KiCad (.pretty)"));
    if (d.isEmpty()) return;
    QStringList f;
    for (const QFileInfo& x : QDir(d).entryInfoList({QStringLiteral("*.kicad_mod")}, QDir::Files)) f << x.filePath();
    lancer(f);
  });
  connect(csv, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Rapport d’import"), QStringLiteral("rapport-import.csv"), QStringLiteral("CSV (*.csv)"));
    if (!f.isEmpty()) enregistrerRapport(f);
  });
  resize(900, 560);
}

int DialogueImportBibliotheques::importer(const QStringList& fichiers, const QString& destination, bool remplacer) {
  std::vector<FichierBibliotheque> lus;
  for (const QString& f : fichiers) {
    try {
      // Le dossier .pretty qui contient une empreinte nomme sa bibliothèque : il reste dans le chemin
      const QFileInfo i(f);
      lus.push_back({(i.dir().dirName() + QLatin1Char('/') + i.fileName()).toStdString(), prom::lireFichier(prom::cheminFichier(f.toStdString()))});
    } catch (const std::exception& e) {
      m_rapport.refuser("fichier", QFileInfo(f).fileName().toStdString(), e.what());
    }
  }
  ContenuBibliotheque b;
  std::filesystem::path chemin;
  if (destination.isEmpty()) {
    b.nom = "personnelle";
    b.articles = AtelierElectronique::articlesPerso();
    b.symboles = AtelierElectronique::bibliothequePerso();
    b.empreintes = AtelierElectronique::empreintesPerso();
  } else {
    chemin = prom::cheminFichier(destination.toStdString());
    std::error_code ec;
    if (std::filesystem::exists(chemin, ec)) {
      std::string erreur;
      b = lireContenuBibliotheque(prom::lireFichier(chemin), &erreur);
      if (!erreur.empty()) {
        m_bilan->setText(rouge(QStringLiteral("« %1 » n’est pas une bibliothèque lisible : %2").arg(destination, q(erreur))));
        return -1;
      }
    } else
      b.nom = QFileInfo(destination).completeBaseName().toStdString();
  }
  m_rapport = importerBibliotheques(lus, b, remplacer);
  int articles = m_rapport.compter("article", "importé") + m_rapport.compter("article", "remplacé");
  try {
    if (destination.isEmpty()) {
      m_atelier.ajouterSymbolesPerso(b.symboles);
      for (const auto& e : b.empreintes) m_atelier.ajouterEmpreintePerso(e);
      m_atelier.rangerArticlesPerso(b.articles);
    } else
      prom::ecrireFichierAtomique(chemin, texteContenuBibliotheque(b));
  } catch (const std::exception& e) {
    m_bilan->setText(rouge(QString::fromUtf8(e.what()).toHtmlEscaped()));
    return -1;
  }
  montrer();
  m_bilan->setText(QStringLiteral("%1 : %2 article(s), %3 symbole(s), %4 empreinte(s) importés ou remplacés ; %5 gardé(s), %6 refusé(s).")
                       .arg(destination.isEmpty() ? QStringLiteral("Bibliothèque personnelle") : QFileInfo(destination).fileName().toHtmlEscaped())
                       .arg(articles)
                       .arg(m_rapport.compter("symbole", "importé") + m_rapport.compter("symbole", "remplacé"))
                       .arg(m_rapport.compter("empreinte", "importé") + m_rapport.compter("empreinte", "remplacé"))
                       .arg(static_cast<int>(std::count_if(m_rapport.objets.begin(), m_rapport.objets.end(), [](const LigneRapportObjet& l) { return l.etat == "gardé"; })))
                       .arg(static_cast<int>(std::count_if(m_rapport.objets.begin(), m_rapport.objets.end(), [](const LigneRapportObjet& l) { return l.etat == "refusé"; }))));
  return articles;
}

void DialogueImportBibliotheques::montrer() {
  m_table->setRowCount(0);
  for (const auto& l : m_rapport.objets) {
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    m_table->setItem(r, 0, new QTableWidgetItem(q(l.genre)));
    m_table->setItem(r, 1, new QTableWidgetItem(q(l.nom)));
    auto* etat = new QTableWidgetItem(q(l.etat));
    if (l.etat == "refusé") etat->setForeground(QColor(0xC0, 0x39, 0x2B));
    m_table->setItem(r, 2, etat);
    m_table->setItem(r, 3, new QTableWidgetItem(q(l.detail)));
  }
  m_table->resizeColumnsToContents();
}

bool DialogueImportBibliotheques::enregistrerRapport(const QString& chemin) const {
  try {
    prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), m_rapport.csv());
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// ======================================================================================================================
// Nomenclature en entrée
// ======================================================================================================================
DialogueNomenclatureEntree::DialogueNomenclatureEntree(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setWindowTitle(QStringLiteral("Nomenclature en entrée"));
  setObjectName(QStringLiteral("dialogueNomenclatureEntree"));
  auto* l = new QVBoxLayout(this);
  l->addWidget(new QLabel(QStringLiteral("Une nomenclature CSV ou XLSX met à jour les composants désignés par leurs repères : valeur, fabricant, MPN, code LCSC, empreinte connue, non monté, et les autres colonnes en champs.")));
  auto* barre = new QHBoxLayout;
  auto* ouvrir = new QPushButton(QStringLiteral("Lire une nomenclature…"));
  auto* appliquerB = new QPushButton(QStringLiteral("Appliquer"));
  appliquerB->setObjectName(QStringLiteral("appliquerNomenclature"));
  barre->addWidget(ouvrir);
  barre->addStretch();
  barre->addWidget(appliquerB);
  l->addLayout(barre);
  m_table = new QTableWidget(0, 4);
  m_table->setHorizontalHeaderLabels({QStringLiteral("Repère"), QStringLiteral("Champ"), QStringLiteral("Avant"), QStringLiteral("Après")});
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->verticalHeader()->hide();
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  l->addWidget(m_table, 1);
  m_bilan = new QLabel;
  m_bilan->setWordWrap(true);
  l->addWidget(m_bilan);
  connect(ouvrir, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Nomenclature"), {}, QStringLiteral("Nomenclatures (*.csv *.txt *.tsv *.xlsx)"));
    if (!f.isEmpty()) lire(f);
  });
  connect(appliquerB, &QPushButton::clicked, this, [this] { appliquer(); });
  resize(820, 520);
}

bool DialogueNomenclatureEntree::lire(const QString& chemin) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  try {
    const std::string octets = prom::lireFichier(prom::cheminFichier(chemin.toStdString()));
    m_tableau = chemin.endsWith(QStringLiteral(".xlsx"), Qt::CaseInsensitive) ? lireXlsx(octets) : lireTableau(octets);
  } catch (const std::exception& e) {
    m_bilan->setText(rouge(QString::fromUtf8(e.what()).toHtmlEscaped()));
    return false;
  }
  ConceptionElec copie = *c;
  m_apercu = appliquerNomenclature(copie, m_tableau, false);
  m_table->setRowCount(0);
  for (const auto& ch : m_apercu.changements) {
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    m_table->setItem(r, 0, new QTableWidgetItem(q(ch.reference)));
    m_table->setItem(r, 1, new QTableWidgetItem(q(ch.champ)));
    m_table->setItem(r, 2, new QTableWidgetItem(q(ch.avant)));
    m_table->setItem(r, 3, new QTableWidgetItem(q(ch.apres)));
  }
  m_table->resizeColumnsToContents();
  QString t = QStringLiteral("%1 ligne(s), %2 composant(s) visé(s), <b>%3 changement(s)</b>.").arg(m_apercu.lignes).arg(m_apercu.composants).arg(m_apercu.changements.size());
  if (!m_apercu.inconnus.empty()) t += QStringLiteral("<br>Repères inconnus : ") + liste(m_apercu.inconnus);
  if (!m_apercu.alertes.empty()) t += QStringLiteral("<br>") + liste(m_apercu.alertes, 6);
  m_bilan->setText(t);
  return true;
}

int DialogueNomenclatureEntree::appliquer() {
  if (m_tableau.empty()) return 0;
  int n = 0;
  m_atelier.modifierConception([&](ConceptionElec& c) { n = static_cast<int>(appliquerNomenclature(c, m_tableau).changements.size()); }, QStringLiteral("Nomenclature en entrée"));
  m_atelier.afficherMessage(QStringLiteral("<b>Nomenclature appliquée</b> : %1 changement(s), un seul Ctrl+Z les retire.").arg(n), NiveauMessage::Succes);
  m_bilan->setText(QStringLiteral("%1 changement(s) appliqué(s).").arg(n));
  return n;
}

// ======================================================================================================================
// IDX
// ======================================================================================================================
DialogueIdx::DialogueIdx(AtelierElectronique& a, PropositionIdx p, QString fichier, QWidget* parent)
    : QDialog(parent), m_atelier(a), m_proposition(std::move(p)), m_fichier(std::move(fichier)) {
  setWindowTitle(QStringLiteral("Proposition de la mécanique (IDX)"));
  setObjectName(QStringLiteral("dialogueIdx"));
  auto* l = new QVBoxLayout(this);
  l->addWidget(new QLabel(QStringLiteral("<b>%1</b>%2 : %3 changement(s) proposé(s). Coche ceux que tu acceptes.")
                              .arg(QFileInfo(m_fichier).fileName().toHtmlEscaped(), m_proposition.logiciel.empty() ? QString() : QStringLiteral(" (") + q(m_proposition.logiciel).toHtmlEscaped() + QLatin1Char(')'))
                              .arg(m_proposition.changements.size())));
  m_table = new QTableWidget(0, 2);
  m_table->setHorizontalHeaderLabels({QStringLiteral("Accepté"), QStringLiteral("Changement")});
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->verticalHeader()->hide();
  for (const auto& ch : m_proposition.changements) {
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    auto* c = new QTableWidgetItem;
    c->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    c->setCheckState(Qt::Checked);
    m_table->setItem(r, 0, c);
    auto* d = new QTableWidgetItem(q(ch.description));
    d->setFlags(Qt::ItemIsEnabled);
    m_table->setItem(r, 1, d);
  }
  m_table->resizeColumnsToContents();
  l->addWidget(m_table, 1);
  if (!m_proposition.alertes.empty()) {
    auto* a2 = new QLabel(QStringLiteral("<b>Non compris</b> : ") + liste(m_proposition.alertes, 6));
    a2->setWordWrap(true);
    l->addWidget(a2);
  }
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  boutons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Appliquer et répondre"));
  connect(boutons, &QDialogButtonBox::accepted, this, [this] {
    appliquer();
    accept();
  });
  connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  l->addWidget(boutons);
  resize(720, 420);
}

std::vector<bool> DialogueIdx::acceptes() const {
  std::vector<bool> r;
  for (int i = 0; i < m_table->rowCount(); ++i) r.push_back(m_table->item(i, 0)->checkState() == Qt::Checked);
  return r;
}

int DialogueIdx::appliquer() {
  const std::vector<bool> ok = acceptes();
  int n = 0;
  m_atelier.modifierConception([&](ConceptionElec& c) { n = appliquerPropositionIdx(c, m_proposition, ok, QFileInfo(m_fichier).fileName().toStdString()); },
                               QStringLiteral("Proposition IDX de la mécanique"));
  // Réponse à côté du fichier reçu
  const QString reponse = QFileInfo(m_fichier).dir().filePath(QFileInfo(m_fichier).completeBaseName() + QStringLiteral("-reponse.idx"));
  try {
    prom::ecrireFichierAtomique(prom::cheminFichier(reponse.toStdString()), idxReponse(m_proposition, ok));
  } catch (const std::exception&) {
  }
  m_atelier.afficherMessage(QStringLiteral("<b>Proposition IDX</b> : %1 changement(s) appliqué(s), %2 refusé(s) ; réponse « %3 » écrite, échange inscrit à l’historique.")
                                .arg(n)
                                .arg(static_cast<int>(std::count(ok.begin(), ok.end(), false)))
                                .arg(QFileInfo(reponse).fileName().toHtmlEscaped()),
                            NiveauMessage::Succes);
  return n;
}

bool AtelierElectronique::recevoirIdx(const QString& chemin, bool dialogue) {
  const ConceptionElec* x = conceptionCourante();
  if (!x) return false;
  PropositionIdx p;
  try {
    p = lirePropositionIdx(prom::lireFichier(prom::cheminFichier(chemin.toStdString())), *x);
  } catch (const std::exception& e) {
    afficherMessage(QStringLiteral("IDX illisible : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    return false;
  }
  if (p.changements.empty()) {
    afficherMessage(QStringLiteral("La proposition IDX ne change rien à la carte.%1").arg(p.alertes.empty() ? QString() : QStringLiteral(" Non compris : ") + liste(p.alertes, 4)), NiveauMessage::Information);
    return false;
  }
  DialogueIdx d(*this, p, chemin, this);
  if (!dialogue) return d.appliquer() > 0;
  return d.exec() == QDialog::Accepted;
}

// ======================================================================================================================
// Commandes de l'atelier
// ======================================================================================================================
DialogueImportBibliotheques* AtelierElectronique::importBibliotheques() {
  if (!m_importBibliotheques) m_importBibliotheques = new DialogueImportBibliotheques(*this, this);
  return m_importBibliotheques;
}

DialogueNomenclatureEntree* AtelierElectronique::nomenclatureEntree() {
  if (!m_nomenclatureEntree) m_nomenclatureEntree = new DialogueNomenclatureEntree(*this, this);
  return m_nomenclatureEntree;
}

// ======================================================================================================================
// Reprise de l'existant (10.19) : conversion d'un dossier en série, conception de référence, guide de l'ancien outil
// ======================================================================================================================
namespace {
// Nom de fichier tiré du nom d'un projet (« E1/ampli » → « E1_ampli »)
QString nomFichierProjet(const std::string& nom) {
  QString n = q(nom);
  for (QChar& c : n)
    if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':')) c = QLatin1Char('_');
  return n;
}
}  // namespace

DialogueConversionSerie::DialogueConversionSerie(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setWindowTitle(QStringLiteral("Convertir un dossier de projets"));
  setObjectName(QStringLiteral("dialogueConversionSerie"));
  auto* l = new QVBoxLayout(this);
  auto* aide = new QLabel(QStringLiteral("Projets KiCad, EAGLE, Altium, PADS, gEDA, Sprint-Layout… du dossier et de ses sous-dossiers : chacun devient un projet "
                                         "Prométhée avec son rapport, ses composants rattachés aux articles par la référence du fabricant. La série continue "
                                         "après un échec ; les articles communs sont rangés une seule fois."));
  aide->setWordWrap(true);
  l->addWidget(aide);
  auto* g = new QGridLayout;
  auto* source = new QPushButton(QStringLiteral("Dossier des projets…"));
  m_source = new QLabel(QStringLiteral("aucun dossier choisi"));
  auto* dest = new QPushButton(QStringLiteral("Destination…"));
  m_dest = new QLineEdit;
  m_dest->setObjectName(QStringLiteral("destinationConversion"));
  auto* biblio = new QPushButton(QStringLiteral("Bibliothèque d’équipe…"));
  m_biblio = new QLineEdit;
  m_biblio->setObjectName(QStringLiteral("bibliothequeConversion"));
  m_biblio->setPlaceholderText(QStringLiteral("vide : la bibliothèque personnelle"));
  g->addWidget(source, 0, 0);
  g->addWidget(m_source, 0, 1);
  g->addWidget(dest, 1, 0);
  g->addWidget(m_dest, 1, 1);
  g->addWidget(biblio, 2, 0);
  g->addWidget(m_biblio, 2, 1);
  g->setColumnStretch(1, 1);
  l->addLayout(g);
  m_table = new QTableWidget(0, 5);
  m_table->setObjectName(QStringLiteral("projetsConversion"));
  m_table->setHorizontalHeaderLabels({QStringLiteral("Projet"), QStringLiteral("Fichier"), QStringLiteral("Logiciel"), QStringLiteral("Résultat"), QStringLiteral("Articles rattachés / créés")});
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->verticalHeader()->hide();
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  l->addWidget(m_table, 1);
  m_barre = new QProgressBar;
  m_barre->setValue(0);
  l->addWidget(m_barre);
  m_etat = new QLabel;
  m_etat->setWordWrap(true);
  l->addWidget(m_etat);
  auto* boutons = new QHBoxLayout;
  m_lancer = new QPushButton(QStringLiteral("Convertir"));
  m_stop = new QPushButton(QStringLiteral("Arrêter après le projet en cours"));
  m_guide = new QPushButton(QStringLiteral("Guide de l’ancien outil…"));
  auto* fermer = new QPushButton(QStringLiteral("Fermer"));
  m_stop->setEnabled(false);
  for (QPushButton* b : {m_lancer, m_stop, m_guide}) boutons->addWidget(b);
  boutons->addStretch();
  boutons->addWidget(fermer);
  l->addLayout(boutons);
  connect(source, &QPushButton::clicked, this, [this] {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier des projets à convertir"));
    if (!d.isEmpty()) choisirDossier(d);
  });
  connect(dest, &QPushButton::clicked, this, [this] {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier des projets convertis"), m_dest->text());
    if (!d.isEmpty()) reglerDestination(d);
  });
  connect(biblio, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Bibliothèque d’équipe"), QStringLiteral("bibliotheque-equipe.json"), QStringLiteral("Bibliothèque Prométhée (*.json)"), nullptr,
                                                   QFileDialog::DontConfirmOverwrite);
    if (!f.isEmpty()) reglerBibliotheque(f);
  });
  connect(m_lancer, &QPushButton::clicked, this, [this] { lancer(); });
  connect(m_stop, &QPushButton::clicked, this, [this] { arreter(); });
  connect(m_guide, &QPushButton::clicked, this, [this] { montrerGuide(); });
  connect(fermer, &QPushButton::clicked, this, &QDialog::close);
  resize(1000, 620);
}

DialogueConversionSerie::~DialogueConversionSerie() {
  if (m_fil) {
    m_arret = true;
    m_fil->wait();
  }
}

int DialogueConversionSerie::choisirDossier(const QString& dossier) {
  if (enCours()) return -1;
  m_dossier = dossier;
  std::vector<std::string> fichiers;
  const QDir racine(dossier);
  QDirIterator it(dossier, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) fichiers.push_back(racine.relativeFilePath(it.next()).toStdString());
  // Les schémas sont lus pour reconnaître les feuilles filles qu'ils appellent
  m_projets = prom::ecad::projetsDuDossier(fichiers, [&racine](const std::string& f) -> std::optional<std::string> {
    try {
      return prom::lireFichier(prom::cheminFichier(racine.filePath(q(f)).toStdString()));
    } catch (const std::exception&) {
      return std::nullopt;
    }
  });
  m_source->setText(QStringLiteral("%1 — %2 projet(s)").arg(QDir::toNativeSeparators(dossier).toHtmlEscaped()).arg(m_projets.size()));
  if (m_dest->text().isEmpty()) m_dest->setText(racine.filePath(QStringLiteral("convertis")));
  m_table->setRowCount(0);
  for (const auto& p : m_projets) {
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    m_table->setItem(r, 0, new QTableWidgetItem(q(p.nom)));
    m_table->setItem(r, 1, new QTableWidgetItem(q(p.fichier)));
    m_table->setItem(r, 2, new QTableWidgetItem);
    m_table->setItem(r, 3, new QTableWidgetItem(QStringLiteral("à convertir")));
    m_table->setItem(r, 4, new QTableWidgetItem);
  }
  m_table->resizeColumnsToContents();
  m_barre->setMaximum(std::max<int>(1, static_cast<int>(m_projets.size())));
  m_barre->setValue(0);
  m_etat->setText(m_projets.empty() ? QStringLiteral("Aucun projet reconnu dans ce dossier.") : QString());
  return static_cast<int>(m_projets.size());
}

void DialogueConversionSerie::reglerDestination(const QString& dossier) { m_dest->setText(dossier); }
void DialogueConversionSerie::reglerBibliotheque(const QString& fichier) { m_biblio->setText(fichier); }
bool DialogueConversionSerie::enCours() const { return m_fil != nullptr; }
void DialogueConversionSerie::arreter() {
  m_arret = true;
  m_etat->setText(QStringLiteral("Arrêt demandé : la série s’arrête après le projet en cours."));
}

bool DialogueConversionSerie::lancer(bool attendre) {
  if (enCours() || m_projets.empty()) return false;
  m_destination = m_dest->text().isEmpty() ? QDir(m_dossier).filePath(QStringLiteral("convertis")) : m_dest->text();
  m_bibliotheque = m_biblio->text();
  if (!QDir().mkpath(m_destination)) {
    m_etat->setText(rouge(QStringLiteral("Dossier de destination impossible à créer : %1").arg(m_destination.toHtmlEscaped())));
    return false;
  }
  m_equipe = {};
  if (m_bibliotheque.isEmpty()) {
    m_equipe.nom = "personnelle";
    m_equipe.articles = AtelierElectronique::articlesPerso();
    m_equipe.symboles = AtelierElectronique::bibliothequePerso();
    m_equipe.empreintes = AtelierElectronique::empreintesPerso();
  } else if (QFileInfo::exists(m_bibliotheque)) {
    std::string erreur;
    try {
      m_equipe = lireContenuBibliotheque(prom::lireFichier(prom::cheminFichier(m_bibliotheque.toStdString())), &erreur);
    } catch (const std::exception& e) {
      erreur = e.what();
    }
    if (!erreur.empty()) {
      m_etat->setText(rouge(QStringLiteral("« %1 » n’est pas une bibliothèque lisible : %2").arg(m_bibliotheque.toHtmlEscaped(), q(erreur).toHtmlEscaped())));
      return false;
    }
  } else
    m_equipe.nom = QFileInfo(m_bibliotheque).completeBaseName().toStdString();
  m_arret = false;
  m_fini = false;
  m_bilan = {};
  m_lancer->setEnabled(false);
  m_stop->setEnabled(true);
  m_barre->setMaximum(static_cast<int>(m_projets.size()));
  m_barre->setValue(0);
  m_etat->setText(QStringLiteral("Conversion en cours…"));
  const std::string dossier = m_dossier.toStdString(), dest = m_destination.toStdString();
  m_fil = QThread::create([this, dossier, dest, projets = m_projets] {
    auto lire = [dossier](const std::string& f) -> std::optional<std::string> {
      try {
        const auto chemin = prom::cheminFichier(dossier + "/" + f);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(chemin, ec)) return std::nullopt;
        return prom::lireFichier(chemin);
      } catch (const std::exception&) {
        return std::nullopt;
      }
    };
    auto ecrire = [dest](const RapportProjet& r, const ConceptionElec& c) {
      Projet p = prom::projetVide();
      p.sansCarte = true;
      p.nom = c.nom;
      p.electronique.push_back(c);
      const std::string base = dest + "/" + nomFichierProjet(r.projet).toStdString();
      prom::ecrireProjet(p, base + ".promethee");
      prom::ecrireFichierAtomique(prom::cheminFichier(base + ".rapport.md"), r.texte());
    };
    m_bilan = convertirEnSerie(projets, m_equipe, lire, ecrire, [this](std::size_t fait, std::size_t total) {
      QMetaObject::invokeMethod(this, [this, fait, total] {
        m_barre->setMaximum(static_cast<int>(total));
        m_barre->setValue(static_cast<int>(fait));
      }, Qt::QueuedConnection);
    }, &m_arret);
  });
  connect(m_fil, &QThread::finished, this, &DialogueConversionSerie::finir);
  m_fil->start();
  if (attendre) {
    m_fil->wait();
    finir();
  }
  return true;
}

void DialogueConversionSerie::finir() {
  if (m_fini) return;
  m_fini = true;
  if (m_fil) {
    m_fil->wait();
    m_fil->deleteLater();
    m_fil = nullptr;
  }
  QStringList erreurs;
  try {
    // Rapports des projets en échec (ceux des réussis sont écrits avec leur projet) et bilan de la série
    for (const auto& r : m_bilan.projets)
      if (!r.reussi) prom::ecrireFichierAtomique(prom::cheminFichier(m_destination.toStdString() + "/" + nomFichierProjet(r.projet).toStdString() + ".rapport.md"), r.texte());
    prom::ecrireFichierAtomique(prom::cheminFichier(m_destination.toStdString() + "/bilan-conversion.md"), m_bilan.texte());
    if (m_bibliotheque.isEmpty()) {
      m_atelier.ajouterSymbolesPerso(m_equipe.symboles);
      for (const auto& e : m_equipe.empreintes) m_atelier.ajouterEmpreintePerso(e);
      m_atelier.rangerArticlesPerso(m_equipe.articles);
    } else
      prom::ecrireFichierAtomique(prom::cheminFichier(m_bibliotheque.toStdString()), texteContenuBibliotheque(m_equipe));
  } catch (const std::exception& e) {
    erreurs << QString::fromUtf8(e.what()).toHtmlEscaped();
  }
  for (const auto& r : m_bilan.projets)
    for (int i = 0; i < m_table->rowCount(); ++i)
      if (m_table->item(i, 1)->text() == q(r.fichier)) {
        m_table->item(i, 2)->setText(q(r.logiciel));
        auto* res = m_table->item(i, 3);
        res->setText(r.reussi ? QStringLiteral("converti") : QStringLiteral("échec : ") + q(r.erreur));
        res->setForeground(r.reussi ? QColor(0x1E, 0x84, 0x49) : QColor(0xC0, 0x39, 0x2B));
        m_table->item(i, 4)->setText(r.reussi ? QStringLiteral("%1 / %2").arg(r.articles.rattaches).arg(r.articles.crees) : QString());
      }
  m_table->resizeColumnsToContents();
  m_etat->setText(QStringLiteral("<b>%1 projet(s) converti(s)</b>, %2 en échec%3 ; %4 article(s) ajouté(s) à la bibliothèque %5, dont %6 commun(s) à plusieurs projets. Rapports dans %7.%8")
                      .arg(m_bilan.reussis)
                      .arg(m_bilan.echecs)
                      .arg(m_bilan.interrompu ? QStringLiteral(" (série arrêtée)") : QString())
                      .arg(m_bilan.articlesCrees)
                      .arg(m_bibliotheque.isEmpty() ? QStringLiteral("personnelle") : QStringLiteral("d’équipe"))
                      .arg(m_bilan.articlesCommuns)
                      .arg(QDir::toNativeSeparators(m_destination).toHtmlEscaped())
                      .arg(erreurs.isEmpty() ? QString() : QStringLiteral("<br>") + rouge(erreurs.join(QStringLiteral("<br>")))));
  m_lancer->setEnabled(true);
  m_stop->setEnabled(false);
  emit termine();
}

QString DialogueConversionSerie::logicielPrincipal() const {
  std::map<std::string, int> n;
  for (const auto& r : m_bilan.projets)
    if (!r.logiciel.empty()) ++n[r.logiciel];
  std::string meilleur;
  int max = 0;
  for (const auto& [l, k] : n)
    if (k > max) max = k, meilleur = l;
  return q(meilleur);
}

void DialogueConversionSerie::montrerGuide() { montrerGuideAncienOutil(this, logicielPrincipal()); }

void montrerGuideAncienOutil(QWidget* parent, const QString& logiciel) {
  auto* d = new QDialog(parent);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->setObjectName(QStringLiteral("guideAncienOutil"));
  d->setWindowTitle(QStringLiteral("Guide de l’ancien outil"));
  auto* l = new QVBoxLayout(d);
  auto* choix = new QComboBox;
  choix->setObjectName(QStringLiteral("logicielGuide"));
  const auto noms = logicielsDuGuide();
  int courant = 0;
  for (std::size_t i = 0; i < noms.size(); ++i) {
    choix->addItem(q(noms[i]));
    if (!logiciel.isEmpty() && guideAncienOutil(logiciel.toStdString()).front().ancien == guideAncienOutil(noms[i]).front().ancien) courant = static_cast<int>(i);
  }
  auto* page = new QTextBrowser;
  page->setObjectName(QStringLiteral("pageGuide"));
  l->addWidget(choix);
  l->addWidget(page, 1);
  auto montrer = [page, choix] { page->setHtml(q(guideHtml(choix->currentText().toStdString()))); };
  QObject::connect(choix, &QComboBox::currentIndexChanged, d, montrer);
  choix->setCurrentIndex(courant);
  montrer();
  d->resize(820, 640);
  d->show();
}

bool AtelierElectronique::importerConceptionReference(const QString& chemin) {
  std::string contenu, erreur;
  try {
    contenu = prom::lireFichier(prom::cheminFichier(chemin.toStdString()));
  } catch (const std::exception& e) {
    afficherMessage(QStringLiteral("Fichier illisible : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    return false;
  }
  // Fiches IPC/DAC-2552 et boîtiers JEP30 posés à côté du projet
  const QDir dossier = QFileInfo(chemin).dir();
  std::vector<std::string> fiches;
  for (const QFileInfo& x : dossier.entryInfoList({QStringLiteral("*.xml"), QStringLiteral("*.XML")}, QDir::Files)) try {
      fiches.push_back(prom::lireFichier(prom::cheminFichier(x.filePath().toStdString())));
    } catch (const std::exception&) {
    }
  ContenuBibliotheque b;
  b.nom = "personnelle";
  b.articles = articlesPerso();
  b.symboles = bibliothequePerso();
  b.empreintes = empreintesPerso();
  const auto r = prom::ecad::importerConceptionReference(QFileInfo(chemin).fileName().toStdString(), contenu, [&](const std::string& n) { return lireVoisin(dossier, n); }, b, fiches, &erreur);
  if (!r) {
    afficherMessage(QStringLiteral("Import impossible : %1").arg(q(erreur).toHtmlEscaped()), NiveauMessage::Erreur);
    return false;
  }
  ajouterSymbolesPerso(b.symboles);
  for (const auto& e : b.empreintes) ajouterEmpreintePerso(e);
  rangerArticlesPerso(b.articles);
  const ConceptionElec c = r->import.conception;
  m_doc->modifier([&c](Projet& p) { p.electronique.push_back(c); }, QStringLiteral("Conception de référence de %1").arg(q(r->import.logiciel)));
  ouvrir(static_cast<int>(m_doc->projet().electronique.size()) - 1, c.schemaSeul ? Schema : Circuit);
  afficherMessage(QStringLiteral("<b>Conception de référence</b> (%1) : %2 article(s) rattaché(s), %3 créé(s), %4 composant(s) sans référence ; %5 fiche(s) IPC/DAC-2552, %6 boîtier(s) JEP30.%7")
                      .arg(q(r->import.logiciel))
                      .arg(r->articles.rattaches)
                      .arg(r->articles.crees)
                      .arg(r->articles.sansReference)
                      .arg(r->fichesLues)
                      .arg(r->boitiers)
                      .arg(r->alertes.empty() && r->import.alertes.empty() ? QString() : QStringLiteral("<br>À vérifier : ") + liste(r->alertes.empty() ? r->import.alertes : r->alertes, 6)),
                  r->alertes.empty() ? NiveauMessage::Succes : NiveauMessage::Avertissement);
  return true;
}

DialogueConversionSerie* AtelierElectronique::conversionSerie() {
  if (!m_conversionSerie) m_conversionSerie = new DialogueConversionSerie(*this, this);
  return m_conversionSerie;
}

void AtelierElectronique::enregistrerCommandesL10() {
  auto& c = m_commandes;
  // Reprise de l'existant (10.19)
  c["bibliotheque.convertirDossier"] = [this] {
    auto* d = conversionSerie();
    d->show();
    d->raise();
  };
  c["bibliotheque.conceptionReference"] = [this] {
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Conception de référence d’un fabricant"), {}, QStringLiteral("Cartes et projets (%1)").arg(q(filtreConceptionsTierces())));
    if (!chemin.isEmpty()) importerConceptionReference(chemin);
  };
  c["aide.guideAncienOutil"] = [this] { montrerGuideAncienOutil(this, {}); };
  c["bibliotheque.importerAutres"] = [this] {
    auto* d = importBibliotheques();
    d->show();
    d->raise();
  };
  c["composants.nomenclatureEntree"] = [this] {
    auto* d = nomenclatureEntree();
    d->show();
    d->raise();
  };
  // Netlists des autres logiciels (10.20) : un format au choix, ou un gabarit maison
  c["fabrication.netlistTierce"] = [this] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    QStringList noms;
    for (const auto& f : formatsNetlist()) noms << QString::fromUtf8(f.nom);
    noms << QStringLiteral("Gabarit maison…");
    bool ok = false;
    const QString choix = QInputDialog::getItem(this, QStringLiteral("Netlist pour un autre logiciel"), QStringLiteral("Format"), noms, 0, false, &ok);
    if (!ok) return;
    const int i = static_cast<int>(noms.indexOf(choix));
    std::string texte;
    QString extension = QStringLiteral("net");
    if (i == static_cast<int>(formatsNetlist().size())) {
      const QString g = QFileDialog::getOpenFileName(this, QStringLiteral("Gabarit de netlist"), {}, QStringLiteral("Gabarits (*.txt *.gabarit)"));
      if (g.isEmpty()) return;
      try {
        texte = netlistGabarit(netlistDe(*x), prom::lireFichier(prom::cheminFichier(g.toStdString())), x->nom);
      } catch (const std::exception& e) {
        afficherMessage(QStringLiteral("Gabarit illisible : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
        return;
      }
      extension = QStringLiteral("txt");
    } else {
      const auto& f = formatsNetlist()[static_cast<std::size_t>(std::max(0, i))];
      texte = ecrireNetlist(netlistDe(*x), f.format, x->nom);
      extension = QString::fromUtf8(f.extension);
    }
    const QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Netlist"), q(x->nom) + QLatin1Char('.') + extension, QStringLiteral("Netlist (*.%1)").arg(extension));
    if (chemin.isEmpty()) return;
    try {
      prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), texte);
      afficherMessage(QStringLiteral("<b>Netlist</b> écrite : %1 (%2)").arg(QFileInfo(chemin).fileName().toHtmlEscaped(), choix.toHtmlEscaped()), NiveauMessage::Succes);
    } catch (const std::exception& e) {
      afficherMessage(QStringLiteral("Netlist : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    }
  };
  // Dessins 2D (10.21) : DXF, SVG (et DWG si LibreDWG est installé) posés sur une couche ; DXF par jeux de couches
  c["carte.importerDessin"] = [this] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const bool dwg = !QStandardPaths::findExecutable(QStringLiteral("dwg2dxf")).isEmpty();
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Dessin à poser sur la carte"), {},
                                                   dwg ? QStringLiteral("Dessins (*.dxf *.svg *.dwg)") : QStringLiteral("Dessins (*.dxf *.svg)"));
    if (f.isEmpty()) return;
    importerDessinCarte(f, true);
  };
  c["carte.exporterDxfCouches"] = [this] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    std::vector<JeuDxf> jeux = jeuxDxf(*x);
    if (jeux.empty()) jeux.push_back(JeuDxf{});
    QStringList noms;
    for (const auto& j : jeux) noms << q(j.nom) + (j.miroir ? QStringLiteral(" (miroir)") : QString());
    noms << QStringLiteral("Nouveau jeu : vue de dessous en miroir…");
    bool ok = false;
    const QString choix = QInputDialog::getItem(this, QStringLiteral("DXF des couches"), QStringLiteral("Jeu de couches"), noms, 0, false, &ok);
    if (!ok) return;
    const int i = static_cast<int>(noms.indexOf(choix));
    JeuDxf j = i < static_cast<int>(jeux.size()) ? jeux[static_cast<std::size_t>(i)] : JeuDxf{};
    if (i >= static_cast<int>(jeux.size())) {
      j.nom = "Vue de dessous";
      j.miroir = true;
      j.couches = {Couche::CuivreDessous, Couche::SerigraphieDessous, Couche::VernisDessous};
      modifierConception([&](ConceptionElec& y) { enregistrerJeuDxf(y, j); }, QStringLiteral("Jeu de couches DXF enregistré"));
    }
    const QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("DXF des couches"), q(x->nom) + QStringLiteral(".dxf"), QStringLiteral("DXF (*.dxf)"));
    if (chemin.isEmpty()) return;
    try {
      prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), dxfCouches(*conceptionCourante(), j));
      afficherMessage(QStringLiteral("<b>DXF</b> écrit : %1, un calque par couche%2.").arg(QFileInfo(chemin).fileName().toHtmlEscaped(), j.miroir ? QStringLiteral(", en miroir") : QString()), NiveauMessage::Succes);
    } catch (const std::exception& e) {
      afficherMessage(QStringLiteral("DXF : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    }
  };
  // Exports vers les autres outils de CAO (10.22) : chaque fichier dit ce qu'il ne porte pas
  auto exporterCao = [this](const QString& titre, const QString& extension, const std::function<std::string(const ConceptionElec&, RapportEcritureCao&)>& produire) {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const QString chemin = QFileDialog::getSaveFileName(this, titre, q(x->nom) + extension, QStringLiteral("%1 (*%2)").arg(titre, extension));
    if (chemin.isEmpty()) return;
    RapportEcritureCao r;
    try {
      prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), produire(*x, r));
    } catch (const std::exception& e) {
      afficherMessage(QStringLiteral("%1 : %2").arg(titre, QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
      return;
    }
    afficherMessage(QStringLiteral("<b>%1</b> écrit : %2 composant(s), %3 réseau(x), %4 segment(s) de piste, %5 via(s).%6")
                        .arg(titre)
                        .arg(r.composants)
                        .arg(r.reseaux)
                        .arg(r.pistes)
                        .arg(r.vias)
                        .arg(r.nonEcrits.empty() ? QString() : QStringLiteral("<br><b>Non transmis</b> : ") + liste(r.nonEcrits, 8)),
                    r.nonEcrits.empty() ? NiveauMessage::Succes : NiveauMessage::Avertissement);
  };
  c["fabrication.exporterPads"] = [exporterCao] {
    exporterCao(QStringLiteral("Carte PADS ASCII"), QStringLiteral(".asc"), [](const ConceptionElec& x, RapportEcritureCao& r) { return padsAsciiCarte(x, &r); });
  };
  c["fabrication.exporterPadsLogic"] = [exporterCao] {
    exporterCao(QStringLiteral("Schéma PADS Logic"), QStringLiteral(".txt"), [](const ConceptionElec& x, RapportEcritureCao& r) { return padsAsciiSchema(x, &r); });
  };
  c["fabrication.exporterPcad"] = [exporterCao] {
    exporterCao(QStringLiteral("Carte P-CAD ASCII"), QStringLiteral(".pcb"), [](const ConceptionElec& x, RapportEcritureCao& r) { return pcadAsciiCarte(x, &r); });
  };
  c["fabrication.archiveAltium"] = [exporterCao] {
    exporterCao(QStringLiteral("Archive pour Altium"), QStringLiteral(".zip"), [](const ConceptionElec& x, RapportEcritureCao& r) { return prom::zipStocke(archivePourAltium(x, &r)); });
  };
  // Exports 3D de la carte équipée (10.10, 10.24)
  c["carte.exporter3D"] = [this] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    QStringList noms;
    for (const auto& f : prom::formats3D()) noms << QStringLiteral("%1 (.%2) — %3").arg(QString::fromUtf8(f.nom), QString::fromUtf8(f.extension), QString::fromUtf8(f.usage));
    bool ok = false;
    const QString choix = QInputDialog::getItem(this, QStringLiteral("Exporter la carte en 3D"), QStringLiteral("Format"), noms, 0, false, &ok);
    if (!ok) return;
    const auto& f = prom::formats3D()[static_cast<std::size_t>(std::max(0, static_cast<int>(noms.indexOf(choix))))];
    const QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter la carte en 3D"), q(x->nom) + QLatin1Char('.') + QString::fromUtf8(f.extension),
                                                        QStringLiteral("%1 (*.%2)").arg(QString::fromUtf8(f.nom), QString::fromUtf8(f.extension)));
    if (chemin.isEmpty()) return;
    try {
      QApplication::setOverrideCursor(Qt::WaitCursor);
      prom::exporter3D(prom::piecesCarteCouleurs(*x), f.format, chemin.toStdString());
      QApplication::restoreOverrideCursor();
      afficherMessage(QStringLiteral("<b>Carte exportée</b> en %1 : %2").arg(QString::fromUtf8(f.nom), QFileInfo(chemin).fileName().toHtmlEscaped()), NiveauMessage::Succes);
    } catch (const std::exception& e) {
      QApplication::restoreOverrideCursor();
      afficherMessage(QStringLiteral("Export 3D impossible : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    }
  };
  // IDX : ligne de base envoyée à la mécanique, proposition reçue, historique des échanges (10.10, 10.24)
  c["carte.idxEnvoyer"] = [this] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    bool ok = false;
    const QStringList cuivres{QStringLiteral("Sans le cuivre"), QStringLiteral("Cuivre en décalques"), QStringLiteral("Cuivre en volumes")};
    const QString choix = QInputDialog::getItem(this, QStringLiteral("Envoyer à la mécanique (IDX)"), QStringLiteral("Cuivre"), cuivres, 0, false, &ok);
    if (!ok) return;
    const QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Ligne de base IDX"), q(x->nom) + QStringLiteral(".idx"), QStringLiteral("IDX (*.idx)"));
    if (chemin.isEmpty()) return;
    OptionsIdx o;
    o.cuivre = static_cast<OptionsIdx::Cuivre>(std::max(0, static_cast<int>(cuivres.indexOf(choix))));
    try {
      prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), idxLigneDeBase(*x, o));
    } catch (const std::exception& e) {
      afficherMessage(QStringLiteral("IDX : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
      return;
    }
    modifierConception([&](ConceptionElec& y) { inscrireEchangeIdx(y, {"", "envoyé", QFileInfo(chemin).fileName().toStdString(), "Prométhée", "ligne de base", 0, 0}); }, QStringLiteral("Ligne de base IDX envoyée"));
    afficherMessage(QStringLiteral("<b>Ligne de base IDX</b> écrite : %1").arg(QFileInfo(chemin).fileName().toHtmlEscaped()), NiveauMessage::Succes);
  };
  c["carte.idxRecevoir"] = [this] {
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Proposition de la mécanique (IDX)"), {}, QStringLiteral("IDX (*.idx *.xml)"));
    if (!chemin.isEmpty()) recevoirIdx(chemin, true);
  };
  c["carte.idxHistorique"] = [this] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    QString t;
    for (const auto& e : historiqueIdx(*x))
      t += QStringLiteral("• %1 — %2 %3 %4 : %5<br>").arg(q(e.date), q(e.sens), q(e.fichier).toHtmlEscaped(), e.logiciel.empty() ? QString() : QStringLiteral("(") + q(e.logiciel).toHtmlEscaped() + QLatin1Char(')'), q(e.resume).toHtmlEscaped());
    QMessageBox::information(this, QStringLiteral("Échanges avec la mécanique"), t.isEmpty() ? QStringLiteral("Aucun échange IDX pour cette carte.") : t);
  };
  // Import d'une carte ou d'un projet d'un autre logiciel dans la carte ouverte, en une commande annulable
  c["carte.importerDansCarte"] = [this] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Ajouter à la carte une carte d’un autre logiciel"), {},
                                                        QStringLiteral("Cartes et projets (%1)").arg(q(filtreConceptionsTierces())));
    if (chemin.isEmpty()) return;
    std::string erreur;
    std::optional<ImportTiers> imp;
    try {
      const QDir dossier = QFileInfo(chemin).dir();
      imp = prom::ecad::importerConceptionTierce(QFileInfo(chemin).fileName().toStdString(), prom::lireFichier(prom::cheminFichier(chemin.toStdString())),
                                     [&](const std::string& nom) { return lireVoisin(dossier, nom); }, &erreur);
    } catch (const std::exception& e) {
      erreur = e.what();
    }
    if (!imp) {
      afficherMessage(QStringLiteral("Import impossible : %1").arg(q(erreur).toHtmlEscaped()), NiveauMessage::Erreur);
      return;
    }
    DialogueImportConception d(*imp, x, this);
    d.reglerCible(DialogueImportConception::CarteOuverte);
    if (d.exec() != QDialog::Accepted) return;
    ConceptionElec ajout = d.import().conception;
    const AssociationCouches a = d.association();
    appliquerAssociation(ajout, a);
    DialogueImportConception::garderAssociation(a);
    RapportFusion r;
    const OptionsFusion o = d.options();
    if (modifierConception([&](ConceptionElec& y) { r = fusionnerConception(y, ajout, o); }, QStringLiteral("Import dans la carte")))
      afficherMessage(QStringLiteral("<b>%1 composant(s) ajouté(s)</b> ; %2 repère(s) renuméroté(s), %3 réseau(x) homonyme(s).").arg(r.composants).arg(r.renumerotes.size()).arg(r.homonymes.size()),
                      NiveauMessage::Succes);
  };
}

int AtelierElectronique::importerDessinCarte(const QString& chemin, bool dialogue, const prom::ecad::OptionsDessinCarte* options) {
  const ConceptionElec* x = conceptionCourante();
  if (!x) return 0;
  QString fichier = chemin;
  // DWG : converti en DXF par LibreDWG (dwg2dxf), installé à part
  if (chemin.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive)) {
    const QString outil = QStandardPaths::findExecutable(QStringLiteral("dwg2dxf"));
    if (outil.isEmpty()) {
      afficherMessage(QStringLiteral("Les fichiers DWG se lisent avec LibreDWG (dwg2dxf), absent de ce poste."), NiveauMessage::Avertissement);
      return 0;
    }
    fichier = QDir::temp().filePath(QFileInfo(chemin).completeBaseName() + QStringLiteral("-promethee.dxf"));
    QProcess p;
    p.start(outil, {QStringLiteral("-y"), QStringLiteral("-o"), fichier, chemin});
    if (!p.waitForFinished(60000) || !QFileInfo::exists(fichier)) {
      afficherMessage(QStringLiteral("DWG non converti par LibreDWG : %1").arg(QString::fromLocal8Bit(p.readAllStandardError()).toHtmlEscaped()), NiveauMessage::Erreur);
      return 0;
    }
  }
  std::string texte;
  try {
    texte = prom::lireFichier(prom::cheminFichier(fichier.toStdString()));
  } catch (const std::exception& e) {
    afficherMessage(QStringLiteral("Dessin illisible : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    return 0;
  }
  OptionsDessinCarte o = options ? *options : OptionsDessinCarte{};
  if (!options) {
    const Boite b = boite(x->circuit.contour.polygone());
    o.centre = b.centre();
  }
  if (dialogue) {
    QStringList couches;
    std::vector<Couche> liste{Couche::SerigraphieDessus, Couche::SerigraphieDessous, Couche::CuivreDessus, Couche::CuivreDessous, Couche::VernisDessus, Couche::VernisDessous,
                              Couche::FabricationDessus, Couche::FabricationDessous, Couche::Dessins, Couche::Commentaires};
    for (Couche k : liste) couches << QString::fromUtf8(nomCouche(k));
    bool ok = false;
    const QString c = QInputDialog::getItem(this, QStringLiteral("Dessin sur la carte"), QStringLiteral("Couche"), couches, 0, false, &ok);
    if (!ok) return 0;
    o.couche = liste[static_cast<std::size_t>(std::max(0, static_cast<int>(couches.indexOf(c))))];
    o.miroir = o.couche == Couche::SerigraphieDessous || o.couche == Couche::CuivreDessous || o.couche == Couche::VernisDessous || o.couche == Couche::FabricationDessous;
    o.largeurVoulue = QInputDialog::getDouble(this, QStringLiteral("Dessin sur la carte"), QStringLiteral("Largeur voulue (mm, 0 : à l’échelle du fichier) :"), 0, 0, 1000, 2, &ok);
    if (!ok) return 0;
  }
  RapportDessinCarte r;
  const auto d = fichier.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive) ? dessinsDepuisSvg(texte, o, &r) : dessinsDepuisDxf(texte, o, &r);
  if (d.empty()) {
    afficherMessage(QStringLiteral("Le dessin ne contient aucune géométrie à poser."), NiveauMessage::Avertissement);
    return 0;
  }
  modifierConception([&](ConceptionElec& y) { y.circuit.dessins.insert(y.circuit.dessins.end(), d.begin(), d.end()); }, QStringLiteral("Dessin posé sur la carte"));
  afficherMessage(QStringLiteral("<b>Dessin posé</b> sur %1 : %2 × %3 mm, %4 région(s) pleine(s), %5 contour(s), %6 trait(s)%7.")
                      .arg(QString::fromUtf8(nomCouche(o.couche)))
                      .arg(r.largeur, 0, 'f', 2)
                      .arg(r.hauteur, 0, 'f', 2)
                      .arg(r.ilots)
                      .arg(r.contours - (r.ilots ? 0 : 0))
                      .arg(r.traits)
                      .arg(r.reunis ? QStringLiteral(", %1 discontinuité(s) réparée(s)").arg(r.reunis) : QString()),
                  NiveauMessage::Succes);
  return static_cast<int>(d.size());
}

// Prométhée : fenêtres de bibliothèque et d'approvisionnement de la 0.34 (lot L4). Voir FenetresBibliotheque.hpp.
// Licence GPL-3.0-only.
#include "FenetresBibliotheque.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "InspecteurElectronique.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/gabarits_ipc.hpp"
#include "promethee/piles_pastilles.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& t) { return t.trimmed().toStdString(); }
QTableWidgetItem* cellule(const QString& t, bool modifiable = false) {
  auto* it = new QTableWidgetItem(t);
  if (!modifiable) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
  return it;
}
// Tableau lu d'un fichier : XLSX par son contenu, sinon texte (CSV, point-virgule, tabulations)
std::vector<std::vector<std::string>> lireFichierTableau(const QString& chemin, QString* erreur) {
  try {
    const std::string octets = prom::lireFichier(prom::cheminFichier(chemin.toStdString()));
    if (octets.size() > 2 && octets[0] == 'P' && octets[1] == 'K') return lireXlsx(octets);
    return lireTableau(octets);
  } catch (const std::exception& e) {
    if (erreur) *erreur = QString::fromUtf8(e.what());
    return {};
  }
}
QString rouge(const QString& t) { return QStringLiteral("<span style='color:%1'>%2</span>").arg(teintes::erreur(), t.toHtmlEscaped()); }
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// 4.18 · Reste à acheter
DialogueAchats::DialogueAchats(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setObjectName(QStringLiteral("dialogueAchats"));
  setWindowTitle(QStringLiteral("Reste à acheter"));
  auto* col = new QVBoxLayout(this);
  auto* haut = new QHBoxLayout;
  haut->addWidget(new QLabel(QStringLiteral("Cartes à fabriquer :")));
  m_cartes = new QSpinBox;
  m_cartes->setObjectName(QStringLiteral("nombreCartes"));
  m_cartes->setRange(1, 1'000'000);
  m_cartes->setValue(10);
  haut->addWidget(m_cartes);
  haut->addStretch(1);
  auto* stock = new QPushButton(QStringLiteral("Stock de l’atelier…"));
  stock->setObjectName(QStringLiteral("ajouterStock"));
  auto* catalogue = new QPushButton(QStringLiteral("Catalogue d’assembleur…"));
  catalogue->setObjectName(QStringLiteral("ajouterCatalogue"));
  haut->addWidget(stock);
  haut->addWidget(catalogue);
  col->addLayout(haut);
  m_liste = new QListWidget;
  m_liste->setObjectName(QStringLiteral("sourcesStock"));
  m_liste->setMaximumHeight(80);
  col->addWidget(m_liste);
  m_table = new QTableWidget(0, 7);
  m_table->setObjectName(QStringLiteral("tableAchats"));
  m_table->setHorizontalHeaderLabels({QStringLiteral("Repères"), QStringLiteral("Valeur"), QStringLiteral("Empreinte"), QStringLiteral("Besoin"), QStringLiteral("En stock"),
                                      QStringLiteral("À acheter"), QStringLiteral("Source")});
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setStretchLastSection(true);
  col->addWidget(m_table, 1);
  m_bilan = inspecteur::texte();
  m_bilan->setObjectName(QStringLiteral("bilanAchats"));
  col->addWidget(m_bilan);
  auto* bas = new QHBoxLayout;
  auto* exporter = new QPushButton(QStringLiteral("Enregistrer en CSV…"));
  exporter->setObjectName(QStringLiteral("exporterAchats"));
  bas->addStretch(1);
  bas->addWidget(exporter);
  auto* fermer = new QPushButton(QStringLiteral("Fermer"));
  bas->addWidget(fermer);
  col->addLayout(bas);
  auto choisir = [this](bool propre) {
    const QString f = QFileDialog::getOpenFileName(this, propre ? QStringLiteral("Stock de l’atelier") : QStringLiteral("Catalogue d’assembleur"), {},
                                                   QStringLiteral("Tableaux (*.csv *.tsv *.txt *.xlsx)"));
    if (!f.isEmpty()) ajouterSource(f, propre);
  };
  connect(stock, &QPushButton::clicked, this, [choisir] { choisir(true); });
  connect(catalogue, &QPushButton::clicked, this, [choisir] { choisir(false); });
  connect(m_cartes, &QSpinBox::valueChanged, this, [this] { refaire(); });
  connect(exporter, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Reste à acheter"), QStringLiteral("achats.csv"), QStringLiteral("CSV (*.csv)"));
    if (f.isEmpty()) return;
    try {
      prom::ecrireFichierAtomique(prom::cheminFichier(f.toStdString()), csv().toStdString());
    } catch (const std::exception& e) {
      m_bilan->setText(rouge(QString::fromUtf8(e.what())));
    }
  });
  connect(fermer, &QPushButton::clicked, this, &QDialog::close);
  resize(860, 520);
  refaire();
}

bool DialogueAchats::ajouterSource(const QString& chemin, bool stockPropre) {
  QString erreur;
  std::string contenu;
  try {
    contenu = prom::lireFichier(prom::cheminFichier(chemin.toStdString()));
  } catch (const std::exception& e) {
    m_bilan->setText(rouge(QString::fromUtf8(e.what())));
    return false;
  }
  std::string err;
  SourceStock src = lireSourceStock(contenu, QFileInfo(chemin).completeBaseName().toStdString(), stockPropre, &err);
  if (!err.empty() || src.lignes.empty()) {
    m_bilan->setText(rouge(QStringLiteral("« %1 » : %2").arg(QFileInfo(chemin).fileName(), err.empty() ? QStringLiteral("aucune ligne lue") : q(err))));
    return false;
  }
  ajouterSource(src);
  return true;
}

void DialogueAchats::ajouterSource(const SourceStock& src) {
  m_sources.push_back(src);
  m_liste->addItem(QStringLiteral("%1 — %2 (%3 lignes)").arg(q(src.nom), src.stockPropre ? QStringLiteral("stock de l’atelier") : QStringLiteral("catalogue")).arg(src.lignes.size()));
  refaire();
}

void DialogueAchats::reglerCartes(int n) { m_cartes->setValue(n); }

void DialogueAchats::refaire() {
  m_table->setRowCount(0);
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  m_besoins = besoinsAchat(*c, m_cartes->value(), m_sources, m_atelier.articlesDisponibles());
  int acheter = 0;
  double montant = 0;
  for (const auto& b : m_besoins) {
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    QStringList refs;
    for (const auto& x : b.ligne.references) refs << q(x);
    m_table->setItem(r, 0, cellule(refs.join(QStringLiteral(", "))));
    m_table->setItem(r, 1, cellule(q(b.ligne.valeur)));
    m_table->setItem(r, 2, cellule(q(b.ligne.empreinte)));
    m_table->setItem(r, 3, cellule(QString::number(b.besoin)));
    m_table->setItem(r, 4, cellule(QString::number(b.enStock)));
    m_table->setItem(r, 5, cellule(QString::number(b.aAcheter)));
    m_table->setItem(r, 6, cellule(q(b.source)));
    if (b.aAcheter > 0) m_table->item(r, 5)->setForeground(QColor(teintes::erreur()));
    acheter += b.aAcheter;
    montant += b.montant;
  }
  m_bilan->setText(QStringLiteral("%1 ligne(s) ; %2 pièce(s) à acheter pour %3 carte(s)%4. Rien ne passe par le réseau : sources et offres sont celles des fichiers importés et de la bibliothèque.")
                       .arg(m_besoins.size())
                       .arg(acheter)
                       .arg(m_cartes->value())
                       .arg(montant > 0 ? QStringLiteral(", %1 € au total").arg(montant, 0, 'f', 2) : QString()));
}

QString DialogueAchats::csv() const { return q(csvBesoins(m_besoins)); }

// ---------------------------------------------------------------------------------------------------------------
// 4.20 · Échange d'empreintes en masse
DialogueEchangeEmpreintes::DialogueEchangeEmpreintes(AtelierElectronique& a, std::vector<std::string> composants, QWidget* parent)
    : QDialog(parent), m_atelier(a), m_composants(std::move(composants)) {
  setObjectName(QStringLiteral("dialogueEchangeEmpreintes"));
  setWindowTitle(QStringLiteral("Échanger les empreintes"));
  auto* col = new QVBoxLayout(this);
  auto* f = new QFormLayout;
  m_repere = new QLineEdit;
  m_repere->setObjectName(QStringLiteral("echangeRepere"));
  m_repere->setPlaceholderText(QStringLiteral("R*, C1?… vide : la sélection"));
  m_valeur = new QLineEdit;
  m_valeur->setObjectName(QStringLiteral("echangeValeur"));
  m_valeur->setPlaceholderText(QStringLiteral("10k, 100n*…"));
  m_empreinte = new QLineEdit;
  m_empreinte->setObjectName(QStringLiteral("echangeEmpreinte"));
  m_empreinte->setPlaceholderText(QStringLiteral("R_0805, SOIC-8…"));
  f->addRow(QStringLiteral("Repère"), m_repere);
  f->addRow(QStringLiteral("Valeur"), m_valeur);
  f->addRow(QStringLiteral("Nouvelle empreinte"), m_empreinte);
  col->addLayout(f);
  auto* g = new QGroupBox(QStringLiteral("Garder"));
  auto* lg = new QVBoxLayout(g);
  m_textes = new QCheckBox(QStringLiteral("Textes déplacés (repère)"));
  m_attributs = new QCheckBox(QStringLiteral("Attributs (hors nomenclature, hors placement, couleur)"));
  m_marges = new QCheckBox(QStringLiteral("Marges des pastilles retouchées"));
  m_modele = new QCheckBox(QStringLiteral("Modèle 3D associé"));
  for (QCheckBox* k : {m_textes, m_attributs, m_marges, m_modele}) {
    k->setChecked(true);
    lg->addWidget(k);
  }
  col->addWidget(g);
  m_bilan = inspecteur::texte();
  m_bilan->setObjectName(QStringLiteral("bilanEchange"));
  m_bilan->setText(QStringLiteral("%1 composant(s) visé(s).").arg(m_composants.size()));
  col->addWidget(m_bilan);
  auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  b->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Échanger"));
  b->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annuler"));
  connect(b, &QDialogButtonBox::accepted, this, [this] {
    if (appliquer() > 0) accept();
  });
  connect(b, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_repere, &QLineEdit::editingFinished, this, [this] { viser(m_repere->text(), m_valeur->text()); });
  connect(m_valeur, &QLineEdit::editingFinished, this, [this] { viser(m_repere->text(), m_valeur->text()); });
  col->addWidget(b);
}

int DialogueEchangeEmpreintes::viser(const QString& repere, const QString& valeur) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return 0;
  if (m_repere->text() != repere) m_repere->setText(repere);
  if (m_valeur->text() != valeur) m_valeur->setText(valeur);
  if (repere.trimmed().isEmpty() && valeur.trimmed().isEmpty()) return static_cast<int>(m_composants.size());
  PorteeMasse p;
  p.reference = s(repere);
  p.valeur = s(valeur);
  m_composants = composantsVises(*c, p);
  m_bilan->setText(QStringLiteral("%1 composant(s) visé(s).").arg(m_composants.size()));
  return static_cast<int>(m_composants.size());
}

void DialogueEchangeEmpreintes::reglerEmpreinte(const QString& id) { m_empreinte->setText(id); }

RegleEchangeEmpreinte DialogueEchangeEmpreintes::regle() const {
  RegleEchangeEmpreinte r;
  r.garderTextes = m_textes->isChecked();
  r.garderAttributs = m_attributs->isChecked();
  r.garderMarges = m_marges->isChecked();
  r.garderModele3D = m_modele->isChecked();
  return r;
}

int DialogueEchangeEmpreintes::appliquer() {
  const std::string e = s(m_empreinte->text());
  if (e.empty() || m_composants.empty()) {
    m_bilan->setText(rouge(e.empty() ? QStringLiteral("Donne la nouvelle empreinte.") : QStringLiteral("Aucun composant visé.")));
    return 0;
  }
  int n = 0;
  std::string erreur;
  const auto ids = m_composants;
  const RegleEchangeEmpreinte r = regle();
  m_atelier.modifierConception([&](ConceptionElec& c) { n = echangerEmpreintesEnMasse(c, ids, e, r, &erreur); }, QStringLiteral("Échanger %1 empreinte(s) pour %2").arg(ids.size()).arg(q(e)));
  m_bilan->setText(n > 0 ? QStringLiteral("%1 empreinte(s) échangée(s) en place, en un pas d’annulation.").arg(n) : rouge(erreur.empty() ? QStringLiteral("Rien à échanger.") : q(erreur)));
  return n;
}

// ---------------------------------------------------------------------------------------------------------------
// 4.23 · Gestion de bibliothèque
DialogueGestionBibliotheque::DialogueGestionBibliotheque(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setObjectName(QStringLiteral("dialogueGestionBibliotheque"));
  setWindowTitle(QStringLiteral("Bibliothèque d’articles"));
  m_champs = {"nom", "valeur", "empreinte", "symbole", "categorie", "fabricant", "mpn", "lcsc", "param:tolerance", "param:puissance", "modele3D", "version"};
  auto* col = new QVBoxLayout(this);
  auto* barre = new QHBoxLayout;
  auto bouton = [&](const QString& t, const char* nom) {
    auto* b = new QPushButton(t);
    b->setObjectName(QLatin1String(nom));
    barre->addWidget(b);
    return b;
  };
  QPushButton* serie = bouton(QStringLiteral("Série E…"), "serieE");
  QPushButton* importer = bouton(QStringLiteral("Importer un tableau…"), "importerTableau");
  QPushButton* renommer = bouton(QStringLiteral("Renommer par gabarit…"), "renommerGabarit");
  QPushButton* copier = bouton(QStringLiteral("Copier vers…"), "copierVers");
  QPushButton* controler = bouton(QStringLiteral("Contrôler"), "controlerBibliotheque");
  QPushButton* enregistrer = bouton(QStringLiteral("Enregistrer le tableau"), "appliquerTableau");
  barre->addStretch(1);
  col->addLayout(barre);
  m_table = new QTableWidget;
  m_table->setObjectName(QStringLiteral("tableArticles"));
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  col->addWidget(m_table, 1);
  m_bilan = inspecteur::texte();
  m_bilan->setObjectName(QStringLiteral("bilanBibliotheque"));
  col->addWidget(m_bilan);
  auto selection = [this] {
    std::vector<std::string> ids;
    std::set<int> lignes;
    for (const auto* it : m_table->selectedItems()) lignes.insert(it->row());
    for (int r : lignes)
      if (auto* it = m_table->item(r, 0)) ids.push_back(s(it->text()));
    return ids;
  };
  connect(serie, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const QString texte = QInputDialog::getText(this, QStringLiteral("Série E"), QStringLiteral("Série, de, à, boîtier (« E24 1 1M 0603 ») :"), QLineEdit::Normal, QStringLiteral("E24 1 1M 0603"), &ok);
    if (!ok) return;
    const QStringList m = texte.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (m.size() < 4) return;
    const ValeurComprise de = lireValeurComposant(s(m[1]), FamilleValeur::Resistance), a = lireValeurComposant(s(m[2]), FamilleValeur::Resistance);
    Article modele;
    modele.symbole = "R";
    modele.categorie = "Résistances";
    modele.empreinte = "R_" + s(m[3]);
    RenommageGabarit g;
    g.gabaritId = "R_{boitier}_{valeur}";
    g.gabaritNom = "Résistance {valeurSI} {boitier}";
    genererSerie(modele, m[0].mid(1).toInt(), de.nombre, a.nombre, g);
  });
  connect(importer, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Articles depuis un tableau"), {}, QStringLiteral("Tableaux (*.csv *.tsv *.txt *.xlsx)"));
    if (!f.isEmpty()) importerTableau(f);
  });
  connect(renommer, &QPushButton::clicked, this, [this, selection] {
    bool ok = false;
    const QString g = QInputDialog::getText(this, QStringLiteral("Renommer par gabarit"), QStringLiteral("Gabarit d’identifiant ({valeur}, {boitier}, {tolerance}, {fabricant}, {mpn}…) :"), QLineEdit::Normal,
                                            QStringLiteral("R_{boitier}_{valeur}"), &ok);
    if (!ok || g.trimmed().isEmpty()) return;
    RenommageGabarit r;
    r.gabaritId = s(g);
    this->renommer(r, selection());
  });
  connect(copier, &QPushButton::clicked, this, [this, selection] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Copier vers la bibliothèque"), QStringLiteral("bibliotheque.json"), QStringLiteral("Bibliothèque Prométhée (*.json)"),
                                                   nullptr, QFileDialog::DontConfirmOverwrite);
    if (!f.isEmpty()) copierVers(f, selection());
  });
  connect(controler, &QPushButton::clicked, this, [this] {
    const QStringList c = this->controler();
    m_bilan->setText(c.isEmpty() ? QStringLiteral("Contrôles sans erreur.") : rouge(c.mid(0, 8).join(QStringLiteral(" · ")) + (c.size() > 8 ? QStringLiteral(" …") : QString())));
  });
  connect(enregistrer, &QPushButton::clicked, this, [this] { appliquerTableau(); });
  resize(1000, 600);
  refaire();
}

void DialogueGestionBibliotheque::refaire() {
  const auto articles = AtelierElectronique::articlesPerso();
  const auto t = tableauArticles(articles, m_champs);
  const QSignalBlocker b(m_table);
  m_table->clear();
  m_table->setColumnCount(static_cast<int>(t.front().size()));
  QStringList entetes;
  for (const auto& e : t.front()) entetes << q(e);
  m_table->setHorizontalHeaderLabels(entetes);
  m_table->setRowCount(static_cast<int>(t.size()) - 1);
  for (std::size_t i = 1; i < t.size(); ++i)
    for (std::size_t j = 0; j < t[i].size(); ++j) m_table->setItem(static_cast<int>(i) - 1, static_cast<int>(j), cellule(q(t[i][j]), j > 0));
  m_bilan->setText(QStringLiteral("%1 article(s) dans la bibliothèque personnelle.").arg(articles.size()));
}

int DialogueGestionBibliotheque::genererSerie(const Article& modele, int serie, double min, double max, const RenommageGabarit& g) {
  const auto v = serieArticles(modele, serie, min, max, g);
  if (v.empty()) {
    m_bilan->setText(rouge(QStringLiteral("Série vide : vérifie la série (E3 à E192) et les bornes.")));
    return 0;
  }
  m_atelier.rangerArticlesPerso(v);
  refaire();
  m_bilan->setText(QStringLiteral("%1 article(s) de la série E%2 rangés, nommés d’après le gabarit « %3 ».").arg(v.size()).arg(serie).arg(q(g.gabaritId).toHtmlEscaped()));
  return static_cast<int>(v.size());
}

int DialogueGestionBibliotheque::importerTableau(const QString& chemin) {
  QString erreur;
  const auto t = lireFichierTableau(chemin, &erreur);
  if (!erreur.isEmpty()) {
    m_bilan->setText(rouge(erreur));
    return 0;
  }
  const ImportTableauArticles imp = articlesDepuisTableau(t);
  if (!imp.articles.empty()) m_atelier.rangerArticlesPerso(imp.articles);
  refaire();
  QStringList j;
  for (const auto& x : imp.journal) j << q(x);
  m_bilan->setText(QStringLiteral("%1 article(s) importé(s) de « %2 ».%3").arg(imp.articles.size()).arg(QFileInfo(chemin).fileName().toHtmlEscaped(), j.isEmpty() ? QString() : QStringLiteral(" ") + j.join(QStringLiteral(" · ")).toHtmlEscaped()));
  return static_cast<int>(imp.articles.size());
}

int DialogueGestionBibliotheque::renommer(const RenommageGabarit& g, const std::vector<std::string>& ids) {
  auto v = AtelierElectronique::articlesPerso();
  const int n = renommerParGabarit(v, g, ids);
  if (n == 0) return 0;
  // Les identifiants changent : le fichier est réécrit en entier
  try {
    prom::ecrireFichierAtomique(prom::cheminFichier(AtelierElectronique::cheminArticlesPerso().toStdString()), texteArticles(v));
  } catch (const std::exception& e) {
    m_bilan->setText(rouge(QString::fromUtf8(e.what())));
    return 0;
  }
  refaire();
  m_bilan->setText(QStringLiteral("%1 article(s) renommé(s) ; les articles qui en héritent suivent.").arg(n));
  return n;
}

RapportCopie DialogueGestionBibliotheque::copierVers(const QString& fichier, const std::vector<std::string>& ids, bool remplacer) {
  ContenuBibliotheque src;
  src.nom = "personnelle";
  src.articles = AtelierElectronique::articlesPerso();
  src.symboles = AtelierElectronique::bibliothequePerso();
  src.empreintes = AtelierElectronique::empreintesPerso();
  std::vector<std::string> vises = ids;
  if (vises.empty())
    for (const auto& a : src.articles) vises.push_back(a.id);
  // Modèles 3D joints : lus à côté de la bibliothèque personnelle
  const QDir dossier = QFileInfo(AtelierElectronique::cheminArticlesPerso()).dir();
  for (const auto& a : src.articles)
    if (!a.modele3D.empty()) try {
        src.modeles3D[a.modele3D] = prom::lireFichier(prom::cheminFichier(dossier.filePath(q(a.modele3D)).toStdString()));
      } catch (const std::exception&) {
      }
  ContenuBibliotheque dst;
  const std::filesystem::path chemin = prom::cheminFichier(fichier.toStdString());
  std::error_code ec;
  if (std::filesystem::exists(chemin, ec)) {
    std::string erreur;
    dst = lireContenuBibliotheque(prom::lireFichier(chemin), &erreur);
    if (!erreur.empty()) {
      m_bilan->setText(rouge(QStringLiteral("« %1 » n’est pas une bibliothèque lisible : %2").arg(fichier, q(erreur))));
      return {};
    }
  } else
    dst.nom = QFileInfo(fichier).completeBaseName().toStdString();
  const RapportCopie r = copierVersBibliotheque(src, vises, dst, remplacer);
  try {
    prom::ecrireFichierAtomique(chemin, texteContenuBibliotheque(dst));
  } catch (const std::exception& e) {
    m_bilan->setText(rouge(QString::fromUtf8(e.what())));
    return {};
  }
  m_bilan->setText(QStringLiteral("Copié dans « %1 » : %2 article(s), %3 symbole(s), %4 empreinte(s), %5 modèle(s) 3D.%6%7")
                       .arg(QFileInfo(fichier).fileName().toHtmlEscaped())
                       .arg(r.articles)
                       .arg(r.symboles)
                       .arg(r.empreintes)
                       .arg(r.modeles3D)
                       .arg(r.gardes.empty() ? QString() : QStringLiteral(" %1 déjà présent(s), gardé(s).").arg(r.gardes.size()))
                       .arg(r.manquants.empty() ? QString() : QStringLiteral(" Introuvables : %1.").arg(q(r.manquants.front()).toHtmlEscaped())));
  return r;
}

QStringList DialogueGestionBibliotheque::controler() const {
  ContenuBibliotheque b;
  b.articles = AtelierElectronique::articlesPerso();
  b.symboles = AtelierElectronique::bibliothequePerso();
  b.empreintes = AtelierElectronique::empreintesPerso();
  QStringList r;
  for (const auto& x : controlerBibliotheque(b))
    if (x.find("modèle 3D") == std::string::npos) r << q(x);
  return r;
}

int DialogueGestionBibliotheque::appliquerTableau() {
  std::vector<std::vector<std::string>> t;
  std::vector<std::string> entete;
  for (int j = 0; j < m_table->columnCount(); ++j) entete.push_back(s(m_table->horizontalHeaderItem(j)->text()));
  t.push_back(entete);
  for (int i = 0; i < m_table->rowCount(); ++i) {
    std::vector<std::string> l;
    for (int j = 0; j < m_table->columnCount(); ++j) l.push_back(m_table->item(i, j) ? s(m_table->item(i, j)->text()) : std::string());
    t.push_back(l);
  }
  auto v = AtelierElectronique::articlesPerso();
  std::vector<std::string> journal;
  const int n = appliquerTableauArticles(v, t, &journal);
  if (n > 0) {
    try {
      prom::ecrireFichierAtomique(prom::cheminFichier(AtelierElectronique::cheminArticlesPerso().toStdString()), texteArticles(v));
    } catch (const std::exception& e) {
      m_bilan->setText(rouge(QString::fromUtf8(e.what())));
      return 0;
    }
    m_atelier.signalerMisesAJourArticles();
  }
  refaire();
  m_bilan->setText(QStringLiteral("%1 article(s) modifié(s), une révision de plus chacun ; les composants posés d’après eux se proposent à la mise à jour.").arg(n));
  return n;
}

// ---------------------------------------------------------------------------------------------------------------
// 4.26 · Série d'empreintes par noms
DialogueSerieEmpreintes::DialogueSerieEmpreintes(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setObjectName(QStringLiteral("dialogueSerieEmpreintes"));
  setWindowTitle(QStringLiteral("Empreintes par noms"));
  auto* col = new QVBoxLayout(this);
  col->addWidget(new QLabel(QStringLiteral("Un nom par ligne : SOIC-8, TSSOP-20, QFN-24-4x4-0.5, BGA-256_17.0x17.0mm_Layout16x16_P1.0mm, R_0603, SMB, PLCC-44…")));
  m_noms = new QPlainTextEdit;
  m_noms->setObjectName(QStringLiteral("nomsEmpreintes"));
  col->addWidget(m_noms, 1);
  auto* d = new QHBoxLayout;
  d->addWidget(new QLabel(QStringLiteral("Densités :")));
  m_m = new QCheckBox(QStringLiteral("M (la plus grande)"));
  m_n = new QCheckBox(QStringLiteral("N"));
  m_l = new QCheckBox(QStringLiteral("L"));
  m_n->setChecked(true);
  for (QCheckBox* k : {m_m, m_n, m_l}) d->addWidget(k);
  d->addStretch(1);
  col->addLayout(d);
  m_bilan = inspecteur::texte();
  m_bilan->setObjectName(QStringLiteral("bilanSerieEmpreintes"));
  col->addWidget(m_bilan);
  auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Close);
  b->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Générer et ranger"));
  b->button(QDialogButtonBox::Close)->setText(QStringLiteral("Fermer"));
  connect(b, &QDialogButtonBox::accepted, this, [this] { generer(); });
  connect(b, &QDialogButtonBox::rejected, this, &QDialog::reject);
  col->addWidget(b);
  resize(640, 480);
}

void DialogueSerieEmpreintes::reglerNoms(const QStringList& noms) { m_noms->setPlainText(noms.join(QLatin1Char('\n'))); }
void DialogueSerieEmpreintes::reglerDensites(bool m, bool n, bool l) {
  m_m->setChecked(m);
  m_n->setChecked(n);
  m_l->setChecked(l);
}

int DialogueSerieEmpreintes::generer() {
  std::vector<std::string> noms;
  for (const QString& l : m_noms->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts))
    if (!l.trimmed().isEmpty()) noms.push_back(s(l));
  std::vector<DensiteIPC> densites;
  if (m_m->isChecked()) densites.push_back(DensiteIPC::Plus);
  if (m_n->isChecked()) densites.push_back(DensiteIPC::Nominale);
  if (m_l->isChecked()) densites.push_back(DensiteIPC::Moins);
  std::vector<std::string> refus;
  const auto v = serieEmpreintes(noms, densites, &refus);
  m_refus.clear();
  for (const auto& r : refus) m_refus << q(r);
  const int n = m_atelier.rangerEmpreintesPerso(v);
  m_bilan->setText(QStringLiteral("%1 empreinte(s) générée(s) pour %2 nom(s), %3 rangée(s) dans la bibliothèque personnelle.%4")
                       .arg(v.size())
                       .arg(noms.size())
                       .arg(n)
                       .arg(m_refus.isEmpty() ? QStringLiteral(" Contrôles sans erreur.") : QStringLiteral(" ") + rouge(m_refus.mid(0, 6).join(QStringLiteral(" · ")))));
  return n;
}

// ---------------------------------------------------------------------------------------------------------------
// 4.24 · Gabarits de pastilles
DialogueGabaritsPastilles::DialogueGabaritsPastilles(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setObjectName(QStringLiteral("dialogueGabaritsPastilles"));
  setWindowTitle(QStringLiteral("Piles de pastilles"));
  auto* col = new QVBoxLayout(this);
  auto* barre = new QHBoxLayout;
  auto* relier = new QPushButton(QStringLiteral("Relier les empreintes du projet"));
  relier->setObjectName(QStringLiteral("relierGabarits"));
  auto* elargir = new QPushButton(QStringLiteral("Modifier le gabarit…"));
  elargir->setObjectName(QStringLiteral("modifierGabarit"));
  barre->addWidget(relier);
  barre->addWidget(elargir);
  barre->addStretch(1);
  col->addLayout(barre);
  m_table = new QTableWidget(0, 6);
  m_table->setObjectName(QStringLiteral("tableGabarits"));
  m_table->setHorizontalHeaderLabels({QStringLiteral("Nom IPC-7351"), QStringLiteral("Forme"), QStringLiteral("Largeur"), QStringLiteral("Hauteur"), QStringLiteral("Perçage"), QStringLiteral("Pastilles")});
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  col->addWidget(m_table, 1);
  m_bilan = inspecteur::texte();
  m_bilan->setObjectName(QStringLiteral("bilanGabarits"));
  col->addWidget(m_bilan);
  connect(relier, &QPushButton::clicked, this, [this] { this->relier(); });
  connect(elargir, &QPushButton::clicked, this, [this] {
    const int r = m_table->currentRow();
    if (r < 0) return;
    bool ok = false;
    const QString t = QInputDialog::getText(this, QStringLiteral("Modifier le gabarit"), QStringLiteral("Élargir de (mm, en largeur et en hauteur ; « 0,1 » ou « 0,1 0 ») :"), QLineEdit::Normal, QStringLiteral("0,1"), &ok);
    if (!ok) return;
    QStringList v = t.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (v.isEmpty()) return;
    const double dx = QString(v[0]).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
    const double dy = v.size() > 1 ? QString(v[1]).replace(QLatin1Char(','), QLatin1Char('.')).toDouble() : dx;
    modifier(s(m_table->item(r, 0)->text()), dx, dy);
  });
  resize(720, 440);
  refaire();
}

int DialogueGabaritsPastilles::emplois(const std::string& nom) const {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  int n = 0;
  if (c)
    for (const auto& e : c->circuit.empreintes)
      if (const DefEmpreinte* d = c->empreinte(e.empreinte))
        for (const auto& p : d->pastilles) n += p.gabarit == nom ? 1 : 0;
  return n;
}

void DialogueGabaritsPastilles::refaire() {
  m_table->setRowCount(0);
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  for (const auto& [nom, g] : c->circuit.gabarits) {
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    static const char* formes[] = {"rond", "rectangle", "ovale", "rectangle arrondi", "chanfreiné", "trapèze", "libre"};
    m_table->setItem(r, 0, cellule(q(nom)));
    m_table->setItem(r, 1, cellule(QString::fromUtf8(formes[static_cast<int>(g.forme)])));
    m_table->setItem(r, 2, cellule(QString::number(mm(g.largeur), 'f', 3)));
    m_table->setItem(r, 3, cellule(QString::number(mm(g.hauteur), 'f', 3)));
    m_table->setItem(r, 4, cellule(g.percage > 0 ? QString::number(mm(g.percage), 'f', 3) : QStringLiteral("—")));
    m_table->setItem(r, 5, cellule(QString::number(emplois(nom))));
  }
  m_bilan->setText(QStringLiteral("%1 gabarit(s) dans le projet.").arg(c->circuit.gabarits.size()));
}

int DialogueGabaritsPastilles::relier() {
  int n = 0;
  m_atelier.modifierConception([&](ConceptionElec& c) {
    // Les empreintes posées, figées dans le projet, reliées une à une
    for (const auto& e : c.circuit.empreintes)
      if (!c.empreintes.count(e.empreinte))
        if (const DefEmpreinte* d = c.empreinte(e.empreinte)) c.empreintes[e.empreinte] = *d;
    for (auto& [id, d] : c.empreintes) n += lierAuxGabarits(d, c.circuit.gabarits);
  }, QStringLiteral("Relier les pastilles aux gabarits"));
  refaire();
  m_bilan->setText(QStringLiteral("%1 pastille(s) reliée(s) à %2 gabarit(s).").arg(n).arg(m_table->rowCount()));
  return n;
}

int DialogueGabaritsPastilles::modifier(const std::string& nom, double dx, double dy) {
  const ConceptionElec* c0 = m_atelier.conceptionCourante();
  if (!c0 || !c0->circuit.gabarits.count(nom)) return 0;
  GabaritPastille g = c0->circuit.gabarits.at(nom);
  g.largeur += nm(dx);
  g.hauteur += nm(dy);
  g.nom = nomIPCGabarit(g);
  int n = 0;
  m_atelier.modifierConception([&](ConceptionElec& c) { n = appliquerGabaritPartout(c, nom, g); }, QStringLiteral("Gabarit %1 modifié").arg(q(nom)));
  refaire();
  m_bilan->setText(QStringLiteral("Gabarit « %1 » devenu « %2 » : %3 pastille(s) l’ont suivi, en un pas.").arg(q(nom).toHtmlEscaped(), q(g.nom).toHtmlEscaped()).arg(n));
  return n;
}

// ---------------------------------------------------------------------------------------------------------------
// 4.28 · Affectation des empreintes
FenetreAffectation::FenetreAffectation(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Tool), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreAffectation"));
  setWindowTitle(QStringLiteral("Affecter les empreintes"));
  auto* col = new QVBoxLayout(this);
  auto* g = new QGroupBox(QStringLiteral("Règles d’équivalence, une par ligne"));
  auto* lg = new QVBoxLayout(g);
  m_regles = new QPlainTextEdit;
  m_regles->setObjectName(QStringLiteral("reglesEquivalence"));
  m_regles->setPlaceholderText(QStringLiteral("Résistance 0603 → R_0603\nCondensateur 0402 → C_0402\nC* 0805 -> C_0805"));
  m_regles->setMaximumHeight(110);
  lg->addWidget(m_regles);
  auto* lb = new QHBoxLayout;
  auto* appliquer = new QPushButton(QStringLiteral("Appliquer les règles"));
  appliquer->setObjectName(QStringLiteral("appliquerRegles"));
  auto* tableau = new QPushButton(QStringLiteral("Appliquer le tableau"));
  tableau->setObjectName(QStringLiteral("appliquerTableauEmpreintes"));
  m_sans = new QCheckBox(QStringLiteral("Composants sans empreinte seulement"));
  m_sans->setObjectName(QStringLiteral("sansEmpreinteSeuls"));
  m_sans->setChecked(true);
  lb->addWidget(appliquer);
  lb->addWidget(tableau);
  lb->addStretch(1);
  lb->addWidget(m_sans);
  lg->addLayout(lb);
  col->addWidget(g);
  m_table = new QTableWidget(0, 4);
  m_table->setObjectName(QStringLiteral("tableAffectation"));
  m_table->setHorizontalHeaderLabels({QStringLiteral("Repère"), QStringLiteral("Valeur"), QStringLiteral("Boîtier"), QStringLiteral("Empreinte")});
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  col->addWidget(m_table, 1);
  m_bilan = inspecteur::texte();
  m_bilan->setObjectName(QStringLiteral("bilanAffectation"));
  col->addWidget(m_bilan);
  connect(appliquer, &QPushButton::clicked, this, [this] { appliquerRegles(); });
  connect(tableau, &QPushButton::clicked, this, [this] { appliquerTableau(); });
  connect(m_sans, &QCheckBox::toggled, this, [this] { refaire(); });
  resize(720, 560);
  refaire();
}

void FenetreAffectation::refaire() {
  const QSignalBlocker b(m_table);
  m_table->setRowCount(0);
  m_lignes.clear();
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  int sans = 0;
  for (const auto& k : c->composants) {
    if (k.horsCarte) continue;
    sans += k.empreinte.empty() ? 1 : 0;
    if (m_sans->isChecked() && !k.empreinte.empty()) continue;
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    m_table->setItem(r, 0, cellule(q(k.reference)));
    m_table->setItem(r, 1, cellule(q(k.valeur)));
    m_table->setItem(r, 2, cellule(q(boitierDeComposant(k))));
    m_table->setItem(r, 3, cellule(q(k.empreinte), true));
    m_lignes.push_back(k.uuid);
  }
  m_bilan->setText(QStringLiteral("%1 composant(s) sans empreinte.").arg(sans));
}

void FenetreAffectation::reglerRegles(const QString& texte) { m_regles->setPlainText(texte); }

int FenetreAffectation::appliquerRegles() {
  std::vector<RegleEquivalence> regles;
  QStringList erreurs;
  for (const QString& l : m_regles->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
    if (l.trimmed().isEmpty() || l.trimmed().startsWith(QLatin1Char('#'))) continue;
    std::string e;
    if (const auto r = lireRegleEquivalence(s(l), &e)) regles.push_back(*r);
    else erreurs << q(e);
  }
  if (regles.empty()) {
    m_bilan->setText(rouge(erreurs.isEmpty() ? QStringLiteral("Écris au moins une règle : « Résistance 0603 → R_0603 ».") : erreurs.join(QStringLiteral(" · "))));
    return 0;
  }
  int n = 0;
  std::vector<std::string> journal;
  const bool aussi = !m_sans->isChecked();
  m_atelier.modifierConception([&](ConceptionElec& c) { n = appliquerEquivalences(c, regles, aussi, &journal); }, QStringLiteral("Affecter les empreintes par règles"));
  refaire();
  QStringList j;
  for (const auto& x : journal) j << q(x);
  m_bilan->setText(QStringLiteral("%1 composant(s) affecté(s) en un pas : %2").arg(n).arg(j.join(QStringLiteral(" · ")).toHtmlEscaped()) + (erreurs.isEmpty() ? QString() : QStringLiteral(" ") + rouge(erreurs.join(QStringLiteral(" · ")))));
  return n;
}

int FenetreAffectation::appliquerTableau() {
  const ConceptionElec* c0 = m_atelier.conceptionCourante();
  if (!c0) return 0;
  std::map<std::string, std::vector<std::string>> parEmpreinte;
  for (int r = 0; r < m_table->rowCount() && r < static_cast<int>(m_lignes.size()); ++r) {
    const std::string e = m_table->item(r, 3) ? s(m_table->item(r, 3)->text()) : std::string();
    const ComposantElec* k = c0->composant(m_lignes[static_cast<std::size_t>(r)]);
    if (!e.empty() && k && k->empreinte != e) parEmpreinte[e].push_back(k->uuid);
  }
  if (parEmpreinte.empty()) return 0;
  int n = 0;
  QStringList inconnues;
  m_atelier.modifierConception([&](ConceptionElec& c) {
    for (const auto& [e, ids] : parEmpreinte) {
      std::string erreur;
      const int k = echangerEmpreintesEnMasse(c, ids, e, {}, &erreur);
      if (k == 0 && !erreur.empty()) inconnues << q(e);
      n += k;
    }
  }, QStringLiteral("Affecter les empreintes du tableau"));
  refaire();
  m_bilan->setText(QStringLiteral("%1 composant(s) affecté(s) en un pas.").arg(n) + (inconnues.isEmpty() ? QString() : QStringLiteral(" ") + rouge(QStringLiteral("Empreintes inconnues : ") + inconnues.join(QStringLiteral(", ")))));
  return n;
}

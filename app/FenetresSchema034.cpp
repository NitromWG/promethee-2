// Prométhée : fenêtres du schéma avancé de la 0.34 (lot L7). Voir FenetresSchema034.hpp.
// Licence GPL-3.0-only.
#include "FenetresSchema034.hpp"

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "Theme.hpp"
#include "VueSchema.hpp"
#include "promethee/dxf_dessin.hpp"
#include "promethee/gabarits_ipc.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& x) { return x.toStdString(); }
QString mmTexte(double v) { return QString::number(v, 'f', 3).replace(QLatin1Char('.'), QLatin1Char(',')); }
}  // namespace

// ---------- Graphe des réseaux (7.14) ----------
VueGrapheReseaux::VueGrapheReseaux(QWidget* parent) : QWidget(parent) {
  setMinimumSize(480, 360);
  setMouseTracking(true);
}

void VueGrapheReseaux::setGraphe(GrapheReseaux g) {
  m_g = std::move(g);
  m_choisi = -1;
  update();
}

QPointF VueGrapheReseaux::ecran(const NoeudGraphe& n) const { return {m_x0 + n.x * m_k, m_y0 + n.y * m_k}; }

int VueGrapheReseaux::noeudSous(QPointF q) const {
  for (std::size_t i = 0; i < m_g.noeuds.size(); ++i)
    if (std::hypot(ecran(m_g.noeuds[i]).x() - q.x(), ecran(m_g.noeuds[i]).y() - q.y()) < 9) return static_cast<int>(i);
  return -1;
}

void VueGrapheReseaux::paintEvent(QPaintEvent*) {
  QPainter g(this);
  g.setRenderHint(QPainter::Antialiasing);
  g.fillRect(rect(), theme().c().champ);
  if (m_g.noeuds.empty()) {
    g.setPen(theme().c().texteSecondaire);
    g.drawText(rect(), Qt::AlignCenter, QStringLiteral("Aucun réseau à montrer"));
    return;
  }
  // Cadrage : tous les nœuds dans la vue, marge de 30 px
  double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
  for (const auto& n : m_g.noeuds) {
    x0 = std::min(x0, n.x), y0 = std::min(y0, n.y), x1 = std::max(x1, n.x), y1 = std::max(y1, n.y);
  }
  m_k = std::min((width() - 60) / std::max(1.0, x1 - x0), (height() - 60) / std::max(1.0, y1 - y0));
  m_x0 = 30 - x0 * m_k + (width() - 60 - (x1 - x0) * m_k) / 2;
  m_y0 = 30 - y0 * m_k + (height() - 60 - (y1 - y0) * m_k) / 2;
  g.setPen(QPen(theme().c().bordure, 1));
  for (const auto& [a, b] : m_g.aretes) g.drawLine(ecran(m_g.noeuds[static_cast<std::size_t>(a)]), ecran(m_g.noeuds[static_cast<std::size_t>(b)]));
  QFont f = font();
  f.setPixelSize(10);
  g.setFont(f);
  for (std::size_t i = 0; i < m_g.noeuds.size(); ++i) {
    const auto& n = m_g.noeuds[i];
    const QPointF p = ecran(n);
    const bool choisi = static_cast<int>(i) == m_choisi;
    g.setPen(QPen(choisi ? theme().c().accentTexte : theme().c().texte, choisi ? 2 : 1));
    g.setBrush(n.reseau ? theme().c().accent : theme().c().surface);
    if (n.reseau) g.drawEllipse(p, 5, 5);
    else g.drawRect(QRectF(p - QPointF(6, 6), QSizeF(12, 12)));
    g.setPen(theme().c().texte);
    g.drawText(p + QPointF(8, 4), q(n.nom));
  }
}

void VueGrapheReseaux::mousePressEvent(QMouseEvent* e) {
  m_choisi = noeudSous(e->position());
  if (m_choisi >= 0 && m_g.noeuds[static_cast<std::size_t>(m_choisi)].reseau) emit reseauChoisi(q(m_g.noeuds[static_cast<std::size_t>(m_choisi)].nom));
  update();
}

DialogueGrapheReseaux::DialogueGrapheReseaux(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setWindowTitle(QStringLiteral("Graphe des réseaux"));
  auto* l = new QVBoxLayout(this);
  m_vue = new VueGrapheReseaux(this);
  l->addWidget(m_vue, 1);
  auto* bas = new QHBoxLayout;
  m_sansAlim = new QCheckBox(QStringLiteral("Sans les réseaux d’alimentation"), this);
  m_sansAlim->setChecked(true);
  auto* ranger = new QPushButton(QStringLiteral("Ranger"), this);
  m_bilan = new QLabel(this);
  bas->addWidget(m_sansAlim);
  bas->addWidget(ranger);
  bas->addStretch();
  bas->addWidget(m_bilan);
  l->addLayout(bas);
  connect(m_sansAlim, &QCheckBox::toggled, this, [this] { recalculer(); });
  connect(ranger, &QPushButton::clicked, this, [this] { recalculer(); });
  connect(m_vue, &VueGrapheReseaux::reseauChoisi, this, [this](const QString& nom) {
    m_atelier.vueSchema()->setReseauSurligne(s(nom));
    m_bilan->setText(QStringLiteral("Réseau %1 surligné au schéma").arg(nom));
  });
  resize(760, 560);
  recalculer();
}

void DialogueGrapheReseaux::recalculer() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  const Connectivite k = connectivite(*c);
  GrapheReseaux g = grapheReseaux(*c, k, m_sansAlim->isChecked());
  rangerParRessorts(g, 300);
  int reseaux = 0;
  for (const auto& n : g.noeuds) reseaux += n.reseau ? 1 : 0;
  m_bilan->setText(QStringLiteral("%1 réseaux, %2 composants").arg(reseaux).arg(static_cast<int>(g.noeuds.size()) - reseaux));
  m_vue->setGraphe(std::move(g));
}

// ---------- Jeux de styles (7.25) ----------
DialogueJeuxStyles::DialogueJeuxStyles(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setWindowTitle(QStringLiteral("Jeux de styles et de réglages"));
  auto* l = new QVBoxLayout(this);
  l->addWidget(new QLabel(QStringLiteral("Styles des fils et des bus, jonctions, tailles de texte du schéma, valeurs par défaut de chaque famille de couches de la carte."), this));
  m_liste = new QListWidget(this);
  l->addWidget(m_liste, 1);
  m_detail = new QLabel(this);
  m_detail->setWordWrap(true);
  l->addWidget(m_detail);
  auto* boutons = new QHBoxLayout;
  auto* appliquer = new QPushButton(QStringLiteral("Appliquer"), this);
  auto* ouvrir = new QPushButton(QStringLiteral("Ouvrir un fichier…"), this);
  auto* garder = new QPushButton(QStringLiteral("Enregistrer celui de la conception…"), this);
  boutons->addWidget(appliquer);
  boutons->addWidget(ouvrir);
  boutons->addWidget(garder);
  boutons->addStretch();
  l->addLayout(boutons);
  m_jeux = jeuxStylesIntegres();
  remplir();
  connect(m_liste, &QListWidget::currentRowChanged, this, [this](int i) {
    if (i < 0 || i >= static_cast<int>(m_jeux.size())) return;
    const JeuStyles& j = m_jeux[static_cast<std::size_t>(i)];
    QString t = q(j.description);
    if (j.carte.serigraphie.tailleTexte > 0) t += QStringLiteral(" · sérigraphie %1 mm").arg(mmTexte(mm(j.carte.serigraphie.tailleTexte)));
    for (const auto& [classe, st] : j.schema.parClasse) t += QStringLiteral(" · fils %1 : %2 %3 mm").arg(q(classe), q(st.couleur), mmTexte(mm(st.epaisseur)));
    m_detail->setText(t);
  });
  connect(appliquer, &QPushButton::clicked, this, [this] {
    if (QListWidgetItem* it = m_liste->currentItem()) this->appliquer(it->text());
  });
  connect(ouvrir, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Jeu de styles"), {}, QStringLiteral("Jeux de styles Prométhée (*.json)"));
    if (!f.isEmpty()) charger(f);
  });
  connect(garder, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Enregistrer le jeu de styles"), QStringLiteral("styles.json"), QStringLiteral("Jeux de styles Prométhée (*.json)"));
    if (!f.isEmpty()) enregistrer(f, QFileInfo(f).completeBaseName());
  });
  resize(560, 380);
}

void DialogueJeuxStyles::remplir() {
  m_liste->clear();
  for (const auto& j : m_jeux) m_liste->addItem(q(j.nom));
  if (!m_jeux.empty()) m_liste->setCurrentRow(0);
}

bool DialogueJeuxStyles::appliquer(const QString& nom) {
  const auto it = std::find_if(m_jeux.begin(), m_jeux.end(), [&](const JeuStyles& j) { return q(j.nom) == nom; });
  if (it == m_jeux.end()) return false;
  const JeuStyles j = *it;
  int creees = 0;
  const bool ok = m_atelier.modifierConception([&](ConceptionElec& c) { creees = appliquerJeuStyles(c, j); }, QStringLiteral("Jeu de styles « %1 »").arg(nom));
  if (ok)
    m_atelier.afficherMessage(QStringLiteral("Jeu de styles <b>%1</b> appliqué%2.").arg(nom.toHtmlEscaped(), creees ? QStringLiteral(" (%1 classe(s) de réseau créée(s))").arg(creees) : QString()),
                              NiveauMessage::Succes);
  return ok;
}

bool DialogueJeuxStyles::charger(const QString& chemin) {
  QFile f(chemin);
  if (!f.open(QIODevice::ReadOnly)) return false;
  std::string erreur;
  const auto j = lireJeuStyles(f.readAll().toStdString(), &erreur);
  if (!j) {
    m_atelier.afficherMessage(QStringLiteral("Jeu de styles illisible : %1").arg(q(erreur).toHtmlEscaped()), NiveauMessage::Erreur);
    return false;
  }
  std::erase_if(m_jeux, [&](const JeuStyles& x) { return x.nom == j->nom; });
  m_jeux.push_back(*j);
  remplir();
  m_liste->setCurrentRow(static_cast<int>(m_jeux.size()) - 1);
  return appliquer(q(j->nom));
}

bool DialogueJeuxStyles::enregistrer(const QString& chemin, const QString& nom) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  QFile f(chemin);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  f.write(QByteArray::fromStdString(texteJeuStyles(jeuDeLaConception(*c, s(nom)))));
  return true;
}

// ---------- Assistant de symboles (7.20) ----------
DialogueAssistantSymbole::DialogueAssistantSymbole(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setWindowTitle(QStringLiteral("Assistant de symboles normalisés"));
  auto* l = new QVBoxLayout(this);
  auto* form = new QFormLayout;
  m_genre = new QComboBox(this);
  for (const char* g : {"MOSFET-N", "MOSFET-P", "NPN", "PNP", "DIODE", "ZENER", "LED", "AOP", "REGULATEUR"}) m_genre->addItem(QString::fromUtf8(g));
  m_empreinte = new QLineEdit(QStringLiteral("SOT-23"), this);
  m_style = new QComboBox(this);
  m_style->addItems({QStringLiteral("CEI"), QStringLiteral("ANSI")});
  form->addRow(QStringLiteral("Genre"), m_genre);
  form->addRow(QStringLiteral("Empreinte (numérotation)"), m_empreinte);
  form->addRow(QStringLiteral("Dessin"), m_style);
  l->addLayout(form);
  m_apercu = new QLabel(this);
  m_apercu->setMinimumSize(200, 160);
  m_apercu->setAlignment(Qt::AlignCenter);
  l->addWidget(m_apercu);
  m_broches = new QLabel(this);
  l->addWidget(m_broches);
  auto* poser = new QPushButton(QStringLiteral("Poser sur la feuille"), this);
  l->addWidget(poser);
  connect(m_genre, &QComboBox::currentTextChanged, this, [this] { majApercu(); });
  connect(m_style, &QComboBox::currentTextChanged, this, [this] { majApercu(); });
  connect(m_empreinte, &QLineEdit::textChanged, this, [this] { majApercu(); });
  connect(poser, &QPushButton::clicked, this, [this] {
    if (this->poser()) accept();
  });
  majApercu();
}

void DialogueAssistantSymbole::regler(const QString& genre, const QString& empreinte, const QString& style) {
  m_genre->setCurrentText(genre);
  m_empreinte->setText(empreinte);
  m_style->setCurrentText(style);
  majApercu();
}

std::optional<DefSymbole> DialogueAssistantSymbole::symbole() const { return symboleNormalise(s(m_genre->currentText()), s(m_empreinte->text().trimmed()), s(m_style->currentText())); }

void DialogueAssistantSymbole::majApercu() {
  const auto d = symbole();
  if (!d) {
    m_apercu->clear();
    m_broches->setText(QStringLiteral("Genre inconnu"));
    return;
  }
  m_apercu->setPixmap(VueSchema::apercu(*d, QSize(200, 160)));
  QStringList b;
  for (const auto& x : d->broches) b << QStringLiteral("%1 → %2").arg(q(x.nom), q(x.numero));
  m_broches->setText(QStringLiteral("Broches : %1").arg(b.join(QStringLiteral(", "))));
}

bool DialogueAssistantSymbole::poser() {
  const auto d = symbole();
  if (!d) return false;
  m_atelier.setMode(AtelierElectronique::Schema);
  m_atelier.vueSchema()->armerPose(*d);
  m_atelier.afficherMessage(QStringLiteral("Symbole <b>%1</b> armé : un clic sur la feuille le pose.").arg(q(d->nom).toHtmlEscaped()));
  return true;
}

// ---------- Générateurs (7.24) ----------
DialogueGenerateurs::DialogueGenerateurs(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setWindowTitle(QStringLiteral("Bobines, pistes résistives et capteurs"));
  auto* l = new QVBoxLayout(this);
  m_onglets = new QTabWidget(this);
  l->addWidget(m_onglets);
  // Bobine
  auto* pb = new QWidget(this);
  auto* fb = new QFormLayout(pb);
  m_forme = new QComboBox(pb);
  m_forme->addItems({QStringLiteral("Carrée"), QStringLiteral("Hexagonale"), QStringLiteral("Octogonale"), QStringLiteral("Circulaire")});
  m_spires = new QSpinBox(pb);
  m_spires->setRange(1, 60);
  m_spires->setValue(5);
  auto nombre = [&](double v, double min, double max) {
    auto* x = new QDoubleSpinBox(pb);
    x->setRange(min, max);
    x->setDecimals(3);
    x->setSuffix(QStringLiteral(" mm"));
    x->setValue(v);
    return x;
  };
  m_diametre = nombre(10, 1, 200);
  m_largeur = nombre(0.25, 0.05, 5);
  m_ecart = nombre(0.25, 0.05, 5);
  auto* reseau = new QLineEdit(QStringLiteral("L1"), pb);
  m_inductance = new QLabel(pb);
  auto* poserB = new QPushButton(QStringLiteral("Poser au centre de la carte"), pb);
  fb->addRow(QStringLiteral("Forme"), m_forme);
  fb->addRow(QStringLiteral("Spires"), m_spires);
  fb->addRow(QStringLiteral("Diamètre extérieur"), m_diametre);
  fb->addRow(QStringLiteral("Largeur de piste"), m_largeur);
  fb->addRow(QStringLiteral("Écart"), m_ecart);
  fb->addRow(QStringLiteral("Réseau"), reseau);
  fb->addRow(QStringLiteral("Inductance"), m_inductance);
  fb->addRow(poserB);
  m_onglets->addTab(pb, QStringLiteral("Bobine planaire"));
  for (QDoubleSpinBox* x : {m_diametre, m_largeur, m_ecart}) connect(x, &QDoubleSpinBox::valueChanged, this, [this] { majInductance(); });
  connect(m_spires, &QSpinBox::valueChanged, this, [this] { majInductance(); });
  connect(m_forme, &QComboBox::currentIndexChanged, this, [this] { majInductance(); });
  connect(poserB, &QPushButton::clicked, this, [this, reseau] { poserBobine(reseau->text()); });
  // Piste résistive
  auto* pr = new QWidget(this);
  auto* fr = new QFormLayout(pr);
  auto* ohms = new QDoubleSpinBox(pr);
  ohms->setRange(0.001, 100);
  ohms->setDecimals(3);
  ohms->setValue(0.1);
  ohms->setSuffix(QStringLiteral(" Ω"));
  auto* largeur = new QDoubleSpinBox(pr);
  largeur->setRange(0.05, 5);
  largeur->setDecimals(3);
  largeur->setValue(0.2);
  largeur->setSuffix(QStringLiteral(" mm"));
  auto* reseauR = new QLineEdit(QStringLiteral("SHUNT"), pr);
  auto* poserR = new QPushButton(QStringLiteral("Poser au centre de la carte"), pr);
  fr->addRow(QStringLiteral("Résistance"), ohms);
  fr->addRow(QStringLiteral("Largeur"), largeur);
  fr->addRow(QStringLiteral("Réseau"), reseauR);
  fr->addRow(poserR);
  m_onglets->addTab(pr, QStringLiteral("Piste résistive"));
  connect(poserR, &QPushButton::clicked, this, [this, ohms, largeur, reseauR] { poserPisteResistive(ohms->value(), largeur->value(), reseauR->text()); });
  // Capteur
  auto* pc = new QWidget(this);
  auto* fc = new QFormLayout(pc);
  auto* genre = new QComboBox(pc);
  genre->addItems({QStringLiteral("Bouton"), QStringLiteral("Curseur"), QStringLiteral("Roue")});
  auto* segments = new QSpinBox(pc);
  segments->setRange(1, 16);
  segments->setValue(5);
  auto* taille = new QDoubleSpinBox(pc);
  taille->setRange(3, 200);
  taille->setValue(30);
  taille->setSuffix(QStringLiteral(" mm"));
  auto* poserC = new QPushButton(QStringLiteral("Poser au centre de la carte"), pc);
  fc->addRow(QStringLiteral("Genre"), genre);
  fc->addRow(QStringLiteral("Segments"), segments);
  fc->addRow(QStringLiteral("Taille"), taille);
  fc->addRow(poserC);
  m_onglets->addTab(pc, QStringLiteral("Capteur capacitif"));
  connect(poserC, &QPushButton::clicked, this, [this, genre, segments, taille] { poserCapteur(static_cast<GenreCapteur>(genre->currentIndex()), segments->value(), taille->value()); });
  majInductance();
}

Bobine DialogueGenerateurs::bobine() const {
  Bobine b;
  b.forme = static_cast<FormeBobine>(m_forme->currentIndex());
  b.spires = m_spires->value();
  b.diametreExterieur = m_diametre->value();
  b.largeur = m_largeur->value();
  b.ecart = m_ecart->value();
  return b;
}

double DialogueGenerateurs::inductanceNh() const { return inductanceBobine(bobine()) * 1e9; }

void DialogueGenerateurs::majInductance() {
  const Bobine b = bobine();
  m_inductance->setText(diametreInterieur(b) <= 0 ? QStringLiteral("trop de spires pour ce diamètre")
                                                  : QStringLiteral("%1 nH (diamètre intérieur %2 mm)").arg(QString::number(inductanceNh(), 'f', 1).replace(QLatin1Char('.'), QLatin1Char(',')), mmTexte(diametreInterieur(b))));
}

namespace {
Pt centreCarte(const ConceptionElec& c) {
  const Boite b = boite(c.circuit.contour.polygone());
  return {(b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2};
}
}  // namespace

int DialogueGenerateurs::poserBobine(const QString& reseau) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return 0;
  const Pt centre = centreCarte(*c);
  const Bobine b = bobine();
  std::size_t n = 0;
  m_atelier.modifierConception([&](ConceptionElec& x) { n = prom::ecad::poserBobine(x, b, centre, s(reseau)).size(); }, QStringLiteral("Bobine planaire"));
  m_atelier.afficherMessage(QStringLiteral("Bobine de %1 spires posée : %2 nH.").arg(b.spires).arg(QString::number(inductanceNh(), 'f', 1)), NiveauMessage::Succes);
  return static_cast<int>(n);
}

double DialogueGenerateurs::poserPisteResistive(double ohms, double largeurMm, const QString& reseau) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return 0;
  const Pt centre = centreCarte(*c);
  double l = 0;
  m_atelier.modifierConception(
      [&](ConceptionElec& x) { l = prom::ecad::poserPisteResistive(x, {centre.x - nm(5), centre.y}, {centre.x + nm(5), centre.y}, ohms, nm(largeurMm), s(reseau)); },
      QStringLiteral("Piste résistive"));
  return l;
}

int DialogueGenerateurs::poserCapteur(GenreCapteur genre, int segments, double taille) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return 0;
  const Pt centre = centreCarte(*c);
  Capteur k;
  k.genre = genre;
  k.segments = segments;
  k.taille = taille;
  int n = 0;
  m_atelier.modifierConception([&](ConceptionElec& x) { n = prom::ecad::poserCapteur(x, k, centre); }, QStringLiteral("Capteur capacitif"));
  return n;
}

// ---------- Pastilles d'une empreinte (7.9, 7.21) ----------
DialoguePastilles::DialoguePastilles(AtelierElectronique& a, const std::string& empreinte, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setWindowTitle(QStringLiteral("Pastilles de l’empreinte"));
  auto* l = new QVBoxLayout(this);
  m_tableau = new QTableWidget(this);
  m_tableau->setColumnCount(6);
  m_tableau->setHorizontalHeaderLabels({QStringLiteral("Numéro"), QStringLiteral("x (mm)"), QStringLiteral("y (mm)"), QStringLiteral("Largeur"), QStringLiteral("Hauteur"), QStringLiteral("Forme")});
  m_tableau->horizontalHeader()->setStretchLastSection(true);
  l->addWidget(m_tableau, 1);
  m_bilan = new QLabel(this);
  m_bilan->setWordWrap(true);
  l->addWidget(m_bilan);
  auto* b1 = new QHBoxLayout;
  auto* serie = new QPushButton(QStringLiteral("Renuméroter en série"), this);
  auto* matrice = new QPushButton(QStringLiteral("Renuméroter en matrice (A1…)"), this);
  auto* coord = new QPushButton(QStringLiteral("Depuis des coordonnées…"), this);
  auto* modele = new QPushButton(QStringLiteral("Pastille modèle → choisies"), this);
  auto* cote = new QPushButton(QStringLiteral("Cote entre deux pastilles…"), this);
  for (QPushButton* b : {serie, matrice, coord, modele, cote}) b1->addWidget(b);
  l->addLayout(b1);
  auto* b2 = new QHBoxLayout;
  m_majComposants = new QCheckBox(QStringLiteral("Mettre à jour les composants posés"), this);
  auto* enregistrerB = new QPushButton(QStringLiteral("Enregistrer au projet"), this);
  b2->addWidget(m_majComposants);
  b2->addStretch();
  b2->addWidget(enregistrerB);
  l->addLayout(b2);
  connect(serie, &QPushButton::clicked, this, [this] { renumeroterSerie(); });
  connect(matrice, &QPushButton::clicked, this, [this] { renumeroterMatrice(); });
  connect(coord, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Coordonnées des pastilles"), {}, QStringLiteral("Tableaux (*.csv *.txt *.tsv)"));
    if (!f.isEmpty()) lireCoordonnees(f);
  });
  connect(modele, &QPushButton::clicked, this, [this] {
    std::vector<std::string> numeros;
    for (const QModelIndex& i : m_tableau->selectionModel()->selectedRows())
      if (i.row() >= 0 && i.row() < static_cast<int>(m_def.pastilles.size())) numeros.push_back(m_def.pastilles[static_cast<std::size_t>(i.row())].numero);
    appliquerModele(numeros);
  });
  connect(cote, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const QString t = QInputDialog::getText(this, QStringLiteral("Cote"), QStringLiteral("Pastilles et distance (« 1 2 1,27 ») :"), QLineEdit::Normal, {}, &ok);
    const QStringList m = t.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (ok && m.size() == 3) reglerCote(s(m[0]), s(m[1]), QString(m[2]).replace(QLatin1Char(','), QLatin1Char('.')).toDouble());
  });
  connect(enregistrerB, &QPushButton::clicked, this, [this] { enregistrer(m_majComposants->isChecked()); });
  connect(m_tableau, &QTableWidget::cellChanged, this, [this](int r, int col) {
    if (r < 0 || r >= static_cast<int>(m_def.pastilles.size()) || col != 0) return;
    m_def.pastilles[static_cast<std::size_t>(r)].numero = s(m_tableau->item(r, 0)->text().trimmed());
    QTimer::singleShot(0, this, [this] { remplir(); });
  });
  if (const ConceptionElec* c = a.conceptionCourante())
    if (const DefEmpreinte* d = c->empreinte(empreinte)) m_def = *d;
  if (m_def.id.empty()) m_def.id = empreinte;
  remplir();
  resize(720, 520);
}

void DialoguePastilles::setEmpreinte(const DefEmpreinte& d) {
  m_def = d;
  remplir();
}

void DialoguePastilles::remplir() {
  const QSignalBlocker bloque(m_tableau);
  m_tableau->setRowCount(static_cast<int>(m_def.pastilles.size()));
  std::set<std::size_t> enDouble;
  const auto doubles = doublonsPastilles(m_def);
  for (const auto& [n, v] : doubles) enDouble.insert(v.begin(), v.end());
  for (std::size_t i = 0; i < m_def.pastilles.size(); ++i) {
    const Pastille& p = m_def.pastilles[i];
    const QStringList cases{q(p.numero), mmTexte(mm(p.position.x)), mmTexte(mm(p.position.y)), mmTexte(mm(p.largeur)), mmTexte(mm(p.hauteur)),
                            p.forme == FormePastille::Rond ? QStringLiteral("rond") : p.forme == FormePastille::Rect ? QStringLiteral("rectangle") : p.forme == FormePastille::Ovale ? QStringLiteral("ovale") : QStringLiteral("autre")};
    for (int j = 0; j < cases.size(); ++j) {
      auto* it = new QTableWidgetItem(cases[j]);
      if (j > 0) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
      if (enDouble.count(i)) it->setBackground(QColor(0xFF, 0xCD, 0xD2));
      m_tableau->setItem(static_cast<int>(i), j, it);
    }
  }
  const QStringList c = controler();
  m_bilan->setText(QStringLiteral("%1 pastilles · %2 numéro(s) en double · %3")
                       .arg(m_def.pastilles.size())
                       .arg(doubles.size())
                       .arg(c.isEmpty() ? QStringLiteral("contrôles sans erreur") : c.join(QStringLiteral(" ; "))));
}

int DialoguePastilles::renumeroterSerie() {
  const int n = prom::ecad::renumeroterSerie(m_def);
  remplir();
  return n;
}

int DialoguePastilles::renumeroterMatrice() {
  const int n = prom::ecad::renumeroterMatrice(m_def);
  remplir();
  return n;
}

bool DialoguePastilles::lireCoordonnees(const QString& chemin) {
  QFile f(chemin);
  if (!f.open(QIODevice::ReadOnly)) return false;
  Pastille modele = m_def.pastilles.empty() ? Pastille{} : m_def.pastilles.front();
  if (m_def.pastilles.empty()) {
    modele.forme = FormePastille::Rect;
    modele.largeur = 1'000'000;
    modele.hauteur = 1'000'000;
  }
  std::vector<std::string> journal;
  const DefEmpreinte d = empreinteDepuisCoordonnees(m_def.id, f.readAll().toStdString(), modele, &journal);
  if (d.pastilles.empty()) return false;
  m_def.pastilles = d.pastilles;
  m_def.encombrement = d.encombrement;
  remplir();
  return true;
}

int DialoguePastilles::appliquerModele(const std::vector<std::string>& numeros) {
  if (numeros.size() < 2) return 0;
  const auto it = std::find_if(m_def.pastilles.begin(), m_def.pastilles.end(), [&](const Pastille& p) { return p.numero == numeros.front(); });
  if (it == m_def.pastilles.end()) return 0;
  const Pastille modele = *it;
  const int n = appliquerPastilleModele(m_def, modele, std::vector<std::string>(numeros.begin() + 1, numeros.end()));
  remplir();
  return n;
}

bool DialoguePastilles::reglerCote(const std::string& a, const std::string& b, double distanceMm) {
  const bool ok = prom::ecad::reglerCote(m_def, a, b, nm(distanceMm));
  remplir();
  return ok;
}

QStringList DialoguePastilles::controler() const {
  QStringList r;
  for (const auto& x : controlerEmpreinte(m_def)) r << q(x);
  return r;
}

int DialoguePastilles::doublons() const { return static_cast<int>(doublonsPastilles(m_def).size()); }

int DialoguePastilles::enregistrer(bool mettreAJourComposants) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return 0;
  const std::vector<std::string> composants = mettreAJourComposants ? composantsDeLEmpreinte(*c, m_def.id) : std::vector<std::string>{};
  int n = 0;
  const DefEmpreinte d = m_def;
  m_atelier.modifierConception([&](ConceptionElec& x) { n = mettreAJourEmpreinte(x, d, composants); }, QStringLiteral("Empreinte %1 retouchée").arg(q(d.id)));
  m_atelier.afficherMessage(QStringLiteral("Empreinte <b>%1</b> enregistrée au projet%2.").arg(q(d.id).toHtmlEscaped(), n ? QStringLiteral(", %1 composant(s) repris").arg(n) : QString()), NiveauMessage::Succes);
  return n;
}

// ---------- Import de formes (7.19) ----------
DialogueImportFormes::DialogueImportFormes(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setWindowTitle(QStringLiteral("Formes depuis un DXF ou un SVG"));
  auto* l = new QVBoxLayout(this);
  auto* haut = new QHBoxLayout;
  m_fichier = new QLabel(QStringLiteral("Aucun fichier"), this);
  auto* choisir = new QPushButton(QStringLiteral("Choisir…"), this);
  haut->addWidget(m_fichier, 1);
  haut->addWidget(choisir);
  l->addLayout(haut);
  l->addWidget(new QLabel(QStringLiteral("Calques repris (DXF) :"), this));
  m_calques = new QListWidget(this);
  l->addWidget(m_calques, 1);
  auto* form = new QFormLayout;
  m_echelle = new QDoubleSpinBox(this);
  m_echelle->setRange(0.001, 1000);
  m_echelle->setDecimals(3);
  m_echelle->setValue(1);
  m_calqueNotes = new QSpinBox(this);
  m_calqueNotes->setRange(0, 7);
  m_calqueNotes->setValue(1);
  form->addRow(QStringLiteral("Échelle"), m_echelle);
  form->addRow(QStringLiteral("Calque de notes (0 : dessin)"), m_calqueNotes);
  l->addLayout(form);
  m_bilan = new QLabel(this);
  l->addWidget(m_bilan);
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  boutons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Importer"));
  l->addWidget(boutons);
  connect(choisir, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Dessin"), {}, QStringLiteral("Dessins (*.dxf *.svg)"));
    if (!f.isEmpty()) choisirFichier(f);
  });
  connect(boutons, &QDialogButtonBox::accepted, this, [this] {
    if (importer() > 0) accept();
  });
  connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  resize(480, 420);
}

bool DialogueImportFormes::choisirFichier(const QString& chemin) {
  QFile f(chemin);
  if (!f.open(QIODevice::ReadOnly)) return false;
  m_chemin = chemin;
  m_contenu = f.readAll();
  m_fichier->setText(QFileInfo(chemin).fileName());
  m_calques->clear();
  if (chemin.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive)) {
    const prom::dxf::DessinDxf d = prom::dxf::lireDessinDxf(m_contenu.toStdString());
    for (const auto& k : d.calques) {
      auto* it = new QListWidgetItem(QStringLiteral("%1 (%2 traits)").arg(q(k.nom)).arg(k.traits), m_calques);
      it->setData(Qt::UserRole, q(k.nom));
      it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
      it->setCheckState(k.visible && k.traits > 0 ? Qt::Checked : Qt::Unchecked);
    }
  }
  return true;
}

std::set<std::string> DialogueImportFormes::calquesChoisis() const {
  std::set<std::string> r;
  for (int i = 0; i < m_calques->count(); ++i)
    if (m_calques->item(i)->checkState() == Qt::Checked) r.insert(s(m_calques->item(i)->data(Qt::UserRole).toString()));
  return r;
}

void DialogueImportFormes::choisirCalques(const std::set<std::string>& calques) {
  for (int i = 0; i < m_calques->count(); ++i)
    m_calques->item(i)->setCheckState(calques.count(s(m_calques->item(i)->data(Qt::UserRole).toString())) ? Qt::Checked : Qt::Unchecked);
}

int DialogueImportFormes::importer() {
  if (m_chemin.isEmpty()) return 0;
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return 0;
  const int feuille = std::max(0, m_atelier.vueSchema()->feuille());
  const Boite cadre = cadreFeuille(c->feuilles[static_cast<std::size_t>(feuille)].format);
  const bool dxf = m_chemin.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive);
  const std::set<std::string> calques = calquesChoisis();
  const std::string texte = m_contenu.toStdString();
  const double echelle = m_echelle->value();
  const int notes = m_calqueNotes->value();
  BilanImportFormes b;
  // DXF : coin bas-gauche à 20 mm du coin bas-gauche du cadre ; SVG : coin haut-gauche à 20 mm du coin haut-gauche
  m_atelier.modifierConception(
      [&](ConceptionElec& x) {
        b = dxf ? importerDxfFeuille(x, static_cast<std::size_t>(feuille), texte, calques, {cadre.x0 + nm(20), cadre.y1 - nm(20)}, notes, echelle)
                : importerSvgFeuille(x, static_cast<std::size_t>(feuille), texte, {cadre.x0 + nm(20), cadre.y0 + nm(20)}, notes, echelle);
      },
      QStringLiteral("Formes de %1").arg(QFileInfo(m_chemin).fileName()));
  m_bilan->setText(QStringLiteral("%1 forme(s) posée(s)%2").arg(b.formes).arg(b.alertes.empty() ? QString() : QStringLiteral(" · ") + q(b.alertes.front())));
  if (b.formes > 0) m_atelier.afficherMessage(QStringLiteral("%1 forme(s) modifiable(s) posée(s) sur la feuille.").arg(b.formes), NiveauMessage::Succes);
  return b.formes;
}

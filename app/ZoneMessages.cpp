// Prométhée : messages de l'atelier électronique (lot L1, tâche 1.11). Voir ZoneMessages.hpp.
// Licence GPL-3.0-only.
#include "ZoneMessages.hpp"

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Theme.hpp"

namespace {
QString texteSeul(const QString& html) {
  QTextDocument d;
  d.setHtml(html);
  return d.toPlainText().simplified();
}
QColor couleur(NiveauMessage n) {
  const CouleursInterface& c = Theme::instance().c();
  switch (n) {
    case NiveauMessage::Erreur: return c.erreur;
    case NiveauMessage::Avertissement: return c.alerte;
    case NiveauMessage::Succes: return c.succes;
    case NiveauMessage::Information: break;
  }
  return c.accentTexte;
}
QString nom(NiveauMessage n) {
  switch (n) {
    case NiveauMessage::Erreur: return QStringLiteral("Erreur");
    case NiveauMessage::Avertissement: return QStringLiteral("Avertissement");
    case NiveauMessage::Succes: return QStringLiteral("Fait");
    case NiveauMessage::Information: break;
  }
  return QStringLiteral("Information");
}
}  // namespace

// ---------- Journal ----------
void JournalMessages::ajouter(MessageAtelier m) {
  if (!m.quand.isValid()) m.quand = QDateTime::currentDateTime();
  m_messages.push_back(m);
  while (m_messages.size() > MAXIMUM) m_messages.pop_front();
  emit ajoute(m_messages.back());
}

// ---------- Bulle ----------
int BulleMessage::duree(const QString& html) {
  const int mots = static_cast<int>(texteSeul(html).split(QLatin1Char(' '), Qt::SkipEmptyParts).size());
  return std::min(12'000, 4'000 + 60 * std::max(0, mots - 10));
}

BulleMessage::BulleMessage(const MessageAtelier& m, QWidget* parent) : QLabel(parent), m_aller(m.aller), m_reste(duree(m.html)) {
  setObjectName(QStringLiteral("bulleMessage"));
  setTextFormat(Qt::RichText);
  setWordWrap(true);
  setText(m.html);
  setToolTip(m.aller ? QStringLiteral("Clic : aller à l’objet concerné") : QString());
  setCursor(m.aller ? Qt::PointingHandCursor : Qt::ArrowCursor);
  setProperty("themeFixe", true);
  const CouleursInterface& c = Theme::instance().c();
  setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border: 1px solid %3; border-left: 4px solid %4; border-radius: 6px; padding: 6px 10px; }")
                    .arg(c.infobulle.name(), c.texteInfobulle.name(), c.bordure.name(), couleur(m.niveau).name()));
  setMaximumWidth(460);
  m_minuterie = new QTimer(this);
  m_minuterie->setSingleShot(true);
  connect(m_minuterie, &QTimer::timeout, this, [this] { emit finie(this); });
  m_depart = QDateTime::currentMSecsSinceEpoch();
  m_minuterie->start(m_reste);
}

// La souris posée sur le message suspend le compte ; il reprend là où il en était
void BulleMessage::enterEvent(QEnterEvent* e) {
  QLabel::enterEvent(e);
  if (!m_minuterie->isActive()) return;
  m_reste = std::max(500, m_reste - static_cast<int>(QDateTime::currentMSecsSinceEpoch() - m_depart));
  m_minuterie->stop();
}
void BulleMessage::leaveEvent(QEvent* e) {
  QLabel::leaveEvent(e);
  m_depart = QDateTime::currentMSecsSinceEpoch();
  m_minuterie->start(m_reste);
}
void BulleMessage::mousePressEvent(QMouseEvent* e) {
  QLabel::mousePressEvent(e);
  if (m_aller) m_aller();
  emit finie(this);
}

// ---------- Pile ----------
PileMessages::PileMessages(QWidget* hote) : QWidget(hote) {
  setObjectName(QStringLiteral("pileMessages"));
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(0, 0, 0, 0);
  col->setSpacing(6);
  hote->installEventFilter(this);
  hide();
}

QString PileMessages::texte(int i) const {
  return i >= 0 && i < nombre() && m_bulles[static_cast<std::size_t>(i)] ? m_bulles[static_cast<std::size_t>(i)]->text() : QString();
}

void PileMessages::montrer(const MessageAtelier& m) {
  auto* b = new BulleMessage(m, this);
  layout()->addWidget(b);
  m_bulles.push_back(b);
  connect(b, &BulleMessage::finie, this, [this](BulleMessage* x) {
    m_bulles.erase(std::remove(m_bulles.begin(), m_bulles.end(), QPointer<BulleMessage>(x)), m_bulles.end());
    x->hide();
    x->deleteLater();
    placer();
  });
  // Trois au plus : le plus ancien part
  while (m_bulles.size() > 3) {
    if (BulleMessage* vieux = m_bulles.front()) {
      vieux->hide();
      vieux->deleteLater();
    }
    m_bulles.pop_front();
  }
  placer();
}

void PileMessages::placer() {
  m_bulles.erase(std::remove(m_bulles.begin(), m_bulles.end(), QPointer<BulleMessage>()), m_bulles.end());
  if (m_bulles.empty()) {
    hide();
    return;
  }
  QWidget* hote = parentWidget();
  const int largeur = std::max(160, std::min(460, hote->width() - 24));
  setFixedWidth(largeur);
  for (auto& b : m_bulles)
    if (b) b->setMaximumWidth(largeur);
  layout()->activate();
  const int hauteur = layout()->sizeHint().height();
  setFixedHeight(hauteur);
  move(12, std::max(0, hote->height() - hauteur - 34));  // au-dessus de la ligne des coordonnées du circuit
  show();
  raise();
}

bool PileMessages::eventFilter(QObject* o, QEvent* e) {
  if (o == parentWidget() && (e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest) && !m_bulles.empty()) placer();
  return QWidget::eventFilter(o, e);
}

// ---------- Panneau Messages ----------
PanneauMessages::PanneauMessages(JournalMessages& journal, QWidget* parent) : QWidget(parent), m_journal(journal) {
  setObjectName(QStringLiteral("panneauMessages"));
  setWindowTitle(QStringLiteral("Messages de la session"));
  auto* col = new QVBoxLayout(this);
  auto* haut = new QHBoxLayout;
  m_niveau = new QComboBox;
  m_niveau->setObjectName(QStringLiteral("niveauMessages"));
  m_niveau->addItem(QStringLiteral("Tous les messages"), -1);
  m_niveau->addItem(QStringLiteral("Erreurs"), static_cast<int>(NiveauMessage::Erreur));
  m_niveau->addItem(QStringLiteral("Avertissements"), static_cast<int>(NiveauMessage::Avertissement));
  m_niveau->addItem(QStringLiteral("Faits"), static_cast<int>(NiveauMessage::Succes));
  m_niveau->addItem(QStringLiteral("Informations"), static_cast<int>(NiveauMessage::Information));
  haut->addWidget(m_niveau);
  haut->addStretch(1);
  auto* copier = new QPushButton(QStringLiteral("Copier"));
  copier->setObjectName(QStringLiteral("copierMessages"));
  copier->setToolTip(QStringLiteral("Copie les messages montrés, une ligne chacun"));
  haut->addWidget(copier);
  col->addLayout(haut);
  m_liste = new QTreeWidget;
  m_liste->setColumnCount(3);
  m_liste->setHeaderLabels({QStringLiteral("Heure"), QStringLiteral("Niveau"), QStringLiteral("Message")});
  m_liste->setRootIsDecorated(false);
  m_liste->setWordWrap(true);
  m_liste->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_liste->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_liste->header()->setSectionResizeMode(2, QHeaderView::Stretch);
  col->addWidget(m_liste, 1);
  connect(m_niveau, &QComboBox::currentIndexChanged, this, [this] { remplir(); });
  connect(&m_journal, &JournalMessages::ajoute, this, [this] {
    if (isVisible()) remplir();
  });
  connect(copier, &QPushButton::clicked, this, [this] {
    QStringList lignes;
    for (int i = 0; i < m_liste->topLevelItemCount(); ++i) {
      const QTreeWidgetItem* it = m_liste->topLevelItem(i);
      lignes << it->text(0) + QLatin1Char('\t') + it->text(1) + QLatin1Char('\t') + it->text(2);
    }
    QApplication::clipboard()->setText(lignes.join(QLatin1Char('\n')));
  });
  remplir();
}

int PanneauMessages::lignes() const { return m_liste->topLevelItemCount(); }

void PanneauMessages::showEvent(QShowEvent* e) {
  remplir();
  QWidget::showEvent(e);
}

void PanneauMessages::remplir() {
  const int filtre = m_niveau->currentData().toInt();
  m_liste->clear();
  // Le plus récent en tête
  for (auto it = m_journal.messages().rbegin(); it != m_journal.messages().rend(); ++it) {
    if (filtre >= 0 && static_cast<int>(it->niveau) != filtre) continue;
    auto* l = new QTreeWidgetItem(m_liste, {it->quand.toString(QStringLiteral("HH:mm:ss")), nom(it->niveau), texteSeul(it->html)});
    l->setForeground(1, couleur(it->niveau));
    l->setToolTip(2, texteSeul(it->html));
  }
}

// ---------- Barre d'état ----------
BarreEtatAtelier::BarreEtatAtelier(JournalMessages& journal, QWidget* parent) : QWidget(parent), m_journal(journal) {
  setObjectName(QStringLiteral("barreEtatAtelier"));
  auto* l = new QHBoxLayout(this);
  l->setContentsMargins(8, 2, 10, 2);
  m_messages = new QToolButton;
  m_messages->setObjectName(QStringLiteral("boutonMessages"));
  m_messages->setText(QStringLiteral("Messages"));
  m_messages->setToolTip(QStringLiteral("Les messages de la session, horodatés, filtrables et copiables"));
  m_messages->setAutoRaise(true);
  connect(m_messages, &QToolButton::clicked, this, &BarreEtatAtelier::ouvrirMessages);
  l->addWidget(m_messages);
  l->addStretch(1);
  m_bilan = new QLabel;
  m_bilan->setObjectName(QStringLiteral("bilanAtelier"));
  m_bilan->setTextFormat(Qt::RichText);
  m_bilan->setProperty("themeFixe", true);
  m_bilan->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  m_bilan->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  connect(m_bilan, &QLabel::linkActivated, this, &BarreEtatAtelier::lienBilan);
  l->addWidget(m_bilan, 4);
  connect(&m_journal, &JournalMessages::ajoute, this, [this] { m_messages->setText(QStringLiteral("Messages (%1)").arg(m_journal.messages().size())); });
}

void BarreEtatAtelier::ajouter(QWidget* w) {
  if (auto* l = qobject_cast<QHBoxLayout*>(layout())) l->insertWidget(l->indexOf(m_messages) + 1, w);
}

void BarreEtatAtelier::setBilan(const QString& html) {
  if (m_bilan->text() == html) return;
  m_bilan->setText(html);
  m_bilan->setToolTip(texteSeul(html));
}

QString BarreEtatAtelier::bilan() const { return m_bilan->text(); }

void BarreEtatAtelier::setPanneau(PanneauMessages* p) {
  m_panneau = p;
  m_heberge = p != nullptr;
}

void BarreEtatAtelier::ouvrirMessages() {
  if (m_heberge && m_panneau) {
    emit messagesDemandes();
    return;
  }
  if (!m_panneau) {
    m_panneau = new PanneauMessages(m_journal, this);
    m_panneau->setWindowFlag(Qt::Window);
    m_panneau->resize(640, 420);
  }
  m_panneau->show();
  m_panneau->raise();
  m_panneau->activateWindow();
}

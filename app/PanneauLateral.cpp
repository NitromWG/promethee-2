// Prométhée : panneau latéral d'outil (U1). Licence GPL-3.0-only.
#include "PanneauLateral.hpp"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "Icones.hpp"

PanneauLateral::PanneauLateral(QWidget* parent) : QFrame(parent) {
  setObjectName(QStringLiteral("panneauLateral"));
  setFrameShape(QFrame::StyledPanel);
  setMinimumWidth(320);
  setFocusPolicy(Qt::StrongFocus);
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(10, 8, 10, 10);
  col->setSpacing(6);
  auto* tete = new QHBoxLayout;
  m_icone = new QLabel;
  m_titre = new QLabel;
  m_titre->setObjectName(QStringLiteral("titrePanneau"));
  QFont f = m_titre->font();
  f.setBold(true);
  f.setPointSizeF(f.pointSizeF() * 1.15);
  m_titre->setFont(f);
  auto* croix = new QToolButton;
  croix->setText(QStringLiteral("✕"));
  croix->setAutoRaise(true);
  croix->setToolTip(QStringLiteral("Fermer (Échap)"));
  connect(croix, &QToolButton::clicked, this, [this] { annuler(); });
  tete->addWidget(m_icone);
  tete->addWidget(m_titre, 1);
  tete->addWidget(croix);
  col->addLayout(tete);
  m_defilement = new QScrollArea;
  m_defilement->setWidgetResizable(true);
  m_defilement->setFrameShape(QFrame::NoFrame);
  m_defilement->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  col->addWidget(m_defilement, 1);
  m_message = new QLabel;
  m_message->setWordWrap(true);
  m_message->hide();
  col->addWidget(m_message);
  auto* pied = new QHBoxLayout;
  pied->addStretch(1);
  m_annuler = new QPushButton(QStringLiteral("Annuler"));
  m_annuler->setObjectName(QStringLiteral("annulerPanneau"));
  m_valider = new QPushButton(QStringLiteral("Valider"));
  m_valider->setObjectName(QStringLiteral("validerPanneau"));
  m_valider->setDefault(true);
  connect(m_annuler, &QPushButton::clicked, this, [this] { annuler(); });
  connect(m_valider, &QPushButton::clicked, this, [this] { valider(); });
  pied->addWidget(m_annuler);
  pied->addWidget(m_valider);
  col->addLayout(pied);
  hide();
}

void PanneauLateral::ouvrir(const QString& id, const QString& titre, const QString& ic, QWidget* contenu, Boutons boutons, std::function<bool()> surValider,
                            std::function<void()> surAnnuler) {
  if (!m_page.isEmpty()) annuler();  // une seule page : la précédente est abandonnée
  m_page = id;
  m_icone->setPixmap(icone(ic).pixmap(24, 24));
  m_titre->setText(titre);
  m_surValider = std::move(surValider);
  m_surAnnuler = std::move(surAnnuler);
  m_contenu = contenu;
  m_defilement->setWidget(contenu);  // le panneau en devient propriétaire
  m_message->clear();
  m_message->hide();
  const bool validation = boutons == Boutons::ValiderAnnuler;
  m_valider->setVisible(validation);
  m_annuler->setText(validation ? QStringLiteral("Annuler") : QStringLiteral("Fermer"));
  show();
  emit ouvert(m_page);
}

void PanneauLateral::valider() {
  if (m_page.isEmpty()) return;
  if (m_surValider && !m_surValider()) return;  // erreur montrée par l'appelant, la page reste
  fermer();
}

void PanneauLateral::annuler() {
  if (m_page.isEmpty()) return;
  const auto f = m_surAnnuler;
  fermer();
  if (f) f();
}

void PanneauLateral::fermer() {
  const QString page = m_page;
  m_page.clear();
  m_surValider = {};
  m_surAnnuler = {};
  if (QWidget* w = m_defilement->takeWidget()) w->deleteLater();
  m_contenu = nullptr;
  hide();
  if (!page.isEmpty()) emit referme(page);
}

void PanneauLateral::setMessage(const QString& html) {
  m_message->setText(html);
  m_message->setVisible(!html.isEmpty());
}

void PanneauLateral::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape) { annuler(); return; }
  QFrame::keyPressEvent(e);
}

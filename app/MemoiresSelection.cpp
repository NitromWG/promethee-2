// Prométhée : mémoires de sélection (lot L2, tâche 2.26, ERG-29). Licence GPL-3.0-only.
#include "MemoiresSelection.hpp"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>

#include "SelectionElectronique.hpp"
#include "Theme.hpp"

PanneauMemoires::PanneauMemoires(SelectionElectronique* sel, QWidget* parent) : QFrame(parent) {
  setObjectName(QStringLiteral("panneauMemoires"));
  setFrameShape(QFrame::StyledPanel);
  setAutoFillBackground(true);
  setCursor(Qt::ArrowCursor);
  auto* l = new QHBoxLayout(this);
  l->setContentsMargins(4, 2, 4, 2);
  l->setSpacing(2);
  auto* titre = new QLabel(QStringLiteral("Mémoires"));
  titre->setToolTip(QStringLiteral("Clic : rappeler la sélection gardée (Maj : l’ajouter) · mémoire vide ou Ctrl+clic : y garder la sélection"));
  l->addWidget(titre);
  for (int i = 0; i < static_cast<int>(m_boutons.size()); ++i) {
    auto* b = new QToolButton(this);
    b->setObjectName(QStringLiteral("memoire%1").arg(i + 1));
    b->setText(QString::number(i + 1));
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);  // le canevas garde le clavier
    b->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(b, &QToolButton::clicked, this, [this, i] {
      const Qt::KeyboardModifiers m = QGuiApplication::keyboardModifiers();
      cliquer(i, (m & Qt::ControlModifier) != 0, (m & Qt::ShiftModifier) != 0);
    });
    connect(b, &QToolButton::customContextMenuRequested, this, [this, i, b](QPoint ou) {
      auto* menu = new QMenu(b);
      menu->setAttribute(Qt::WA_DeleteOnClose);
      menu->addAction(QStringLiteral("Garder la sélection ici"), this, [this, i] { cliquer(i, true, false); });
      if (m_sel && m_sel->tailleMemoire(i) > 0) {
        menu->addAction(QStringLiteral("Rappeler"), this, [this, i] { cliquer(i, false, false); });
        menu->addAction(QStringLiteral("Ajouter à la sélection"), this, [this, i] { cliquer(i, false, true); });
      }
      menu->popup(b->mapToGlobal(ou));
    });
    l->addWidget(b);
    m_boutons[static_cast<std::size_t>(i)] = b;
  }
  setSelection(sel);
}

void PanneauMemoires::setSelection(SelectionElectronique* sel) {
  if (sel == m_sel) return;
  if (m_sel) disconnect(m_sel, nullptr, this, nullptr);
  m_sel = sel;
  if (m_sel) connect(m_sel, &SelectionElectronique::memoiresChange, this, [this] { maj(); });
  maj();
}

void PanneauMemoires::cliquer(int i, bool garder, bool ajouter) {
  if (!m_sel) return;
  if (garder || m_sel->tailleMemoire(i) == 0) m_sel->memoriser(i);
  else m_sel->rappeler(i, ajouter ? SelectionElectronique::Mode::Ajouter : SelectionElectronique::Mode::Remplacer);
}

void PanneauMemoires::maj() {
  for (int i = 0; i < static_cast<int>(m_boutons.size()); ++i) {
    QToolButton* b = m_boutons[static_cast<std::size_t>(i)];
    const std::size_t n = m_sel ? m_sel->tailleMemoire(i) : 0;
    QFont f = b->font();
    f.setBold(n > 0);
    b->setFont(f);
    b->setToolTip(n ? QStringLiteral("Mémoire %1 : %2 objet(s) · clic : rappeler, Maj+clic : ajouter, Ctrl+clic : y garder la sélection").arg(i + 1).arg(n)
                    : QStringLiteral("Mémoire %1, vide · clic : y garder la sélection").arg(i + 1));
  }
  adjustSize();
}

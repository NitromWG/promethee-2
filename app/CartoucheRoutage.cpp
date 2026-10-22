// Prométhée : cartouche du tracé (lot L5). Licence GPL-3.0-only.
#include "CartoucheRoutage.hpp"

#include <algorithm>

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QToolButton>

using namespace prom::ecad;

namespace {
QString mmCourt(Nm v) { return QLocale(QLocale::French).toString(mm(v), 'g', 3) + QStringLiteral(" mm"); }
}  // namespace

std::vector<Nm> CartoucheRoutage::largeurs(Nm classe) {
  std::vector<Nm> r{classe};
  for (const Nm l : {150'000, 200'000, 250'000, 300'000, 400'000, 500'000, 800'000, 1'000'000, 1'500'000})
    if (l != classe) r.push_back(l);
  std::sort(r.begin() + 1, r.end());
  return r;
}

CartoucheRoutage::CartoucheRoutage(VueCircuit& v) : QFrame(&v), m_vue(v) {
  setObjectName(QStringLiteral("cartoucheRoutage"));
  setFrameShape(QFrame::StyledPanel);
  setAutoFillBackground(true);
  auto* ligne = new QHBoxLayout(this);
  ligne->setContentsMargins(6, 2, 6, 2);
  ligne->setSpacing(4);
  m_reseau = new QLabel;
  m_reseau->setObjectName(QStringLiteral("reseauCartouche"));
  ligne->addWidget(m_reseau);
  m_largeur = new QToolButton;
  m_largeur->setObjectName(QStringLiteral("largeurCartouche"));
  m_largeur->setToolTip(QStringLiteral("Largeur des segments suivants : un clic passe à la suivante, la flèche les liste"));
  m_largeur->setPopupMode(QToolButton::MenuButtonPopup);
  m_largeur->setAutoRaise(true);
  auto* menu = new QMenu(m_largeur);
  m_largeur->setMenu(menu);
  connect(menu, &QMenu::aboutToShow, this, [this, menu] {
    menu->clear();
    for (const Nm l : largeurs(m_classe)) {
      QAction* a = menu->addAction(l == m_classe ? QStringLiteral("%1 (classe)").arg(mmCourt(l)) : mmCourt(l), this, [this, l] { m_vue.setLargeurTrace(l); });
      a->setCheckable(true);
      a->setChecked(l == m_valeur);
    }
  });
  connect(m_largeur, &QToolButton::clicked, this, [this] {
    const auto l = largeurs(m_classe);
    auto it = std::find(l.begin(), l.end(), m_valeur);
    m_vue.setLargeurTrace(it == l.end() || it + 1 == l.end() ? l.front() : *(it + 1));
  });
  ligne->addWidget(m_largeur);
  m_via = new QToolButton;
  m_via->setObjectName(QStringLiteral("viaCartouche"));
  m_via->setToolTip(QStringLiteral("Poser une via au dernier point et passer sur l’autre couche (V)"));
  m_via->setAutoRaise(true);
  connect(m_via, &QToolButton::clicked, this, [this] { m_vue.executerAction("circuit.via"); });
  ligne->addWidget(m_via);
  m_mode = new QToolButton;
  m_mode->setObjectName(QStringLiteral("modeCartouche"));
  m_mode->setToolTip(QStringLiteral("Mode du routeur : un clic passe au suivant (contourner, pousser, ignorer)"));
  m_mode->setAutoRaise(true);
  connect(m_mode, &QToolButton::clicked, this, [this] {
    const auto m = m_vue.modeRouteur();
    m_vue.setModeRouteur(m == VueCircuit::ModeRouteur::Contourner ? VueCircuit::ModeRouteur::Pousser
                         : m == VueCircuit::ModeRouteur::Pousser  ? VueCircuit::ModeRouteur::Ignorer
                                                                   : VueCircuit::ModeRouteur::Contourner);
  });
  ligne->addWidget(m_mode);
  hide();
}

void CartoucheRoutage::maj(const QString& reseau, Couche couche, Nm largeur, Nm classe, Nm via, VueCircuit::ModeRouteur mode) {
  m_valeur = largeur;
  m_classe = classe;
  QString c = QString::fromUtf8(nomCouche(couche));
  if (c.startsWith(QStringLiteral("Cuivre "))) c = c.mid(7);
  m_reseau->setText(QStringLiteral("<b>%1</b> · %2").arg((reseau.isEmpty() ? QStringLiteral("sans réseau") : reseau).toHtmlEscaped(), c.toHtmlEscaped()));
  m_largeur->setText(mmCourt(largeur));
  m_via->setText(QStringLiteral("Via ⌀ %1").arg(mmCourt(via)));
  m_mode->setText(VueCircuit::nomModeRouteur(mode));
  adjustSize();
}

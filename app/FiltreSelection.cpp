// Prométhée : filtre de sélection de l'atelier électronique. Voir FiltreSelection.hpp. Licence GPL-3.0-only.
#include "FiltreSelection.hpp"

#include <utility>

#include <QMenu>
#include <QTimer>

#include "SelectionElectronique.hpp"
#include "Theme.hpp"

namespace {
using G = SelectionElectronique::Genre;
struct Ligne {
  G genre;
  const char* libelle;
};
constexpr Ligne SCHEMA[] = {{G::Symbole, "Symboles"},       {G::Fil, "Fils"},           {G::Etiquette, "Étiquettes"},
                            {G::TexteSchema, "Textes"},     {G::Jonction, "Jonctions"}, {G::NonConnexion, "Non-connexions"}};
constexpr Ligne CIRCUIT[] = {{G::Empreinte, "Empreintes"}, {G::Piste, "Pistes"}, {G::Via, "Vias"}, {G::Zone, "Zones"}, {G::TexteCarte, "Textes"}};
}  // namespace

BoutonFiltre::BoutonFiltre(SelectionElectronique* selection, QWidget* parent) : QToolButton(parent), m_selection(selection), m_minuterie(new QTimer(this)) {
  setObjectName(QStringLiteral("filtreSelection"));
  setAutoRaise(true);
  setPopupMode(QToolButton::InstantPopup);
  setToolTip(QStringLiteral("Filtre de sélection : ce que le clic et le cadre peuvent prendre"));
  setProperty("themeFixe", true);
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("menuFiltre"));
  auto section = [this, menu](const QString& titre, const auto& lignes) {
    menu->addSection(titre);
    for (const Ligne& l : lignes) {
      QAction* a = menu->addAction(QString::fromUtf8(l.libelle));
      a->setCheckable(true);
      a->setChecked(m_selection->admis(l.genre));
      a->setData(static_cast<int>(l.genre));
      connect(a, &QAction::toggled, this, [this, g = l.genre](bool oui) { m_selection->setAdmis(g, oui); });
    }
  };
  section(QStringLiteral("Schéma"), SCHEMA);
  section(QStringLiteral("Circuit imprimé"), CIRCUIT);
  menu->addSeparator();
  connect(menu->addAction(QStringLiteral("Tout admettre")), &QAction::triggered, this, [menu] {
    for (QAction* a : menu->actions())
      if (a->isCheckable()) a->setChecked(true);
  });
  setMenu(menu);
  connect(m_selection, &SelectionElectronique::filtreChange, this, [this] { majTexte(); });
  connect(m_selection, &SelectionElectronique::filtreBloque, this, [this] { clignoter(); });
  // Trois éclats de 150 ms : assez pour se voir, sans gêner la suite
  m_minuterie->setInterval(150);
  connect(m_minuterie, &QTimer::timeout, this, [this] {
    allumer(!m_allume);
    if (++m_etapes >= 6) {
      m_minuterie->stop();
      allumer(false);
    }
  });
  majTexte();
}

void BoutonFiltre::majTexte() {
  const std::size_t n = m_selection->exclus().size();
  setText(n == 0 ? QStringLiteral("Filtre") : QStringLiteral("Filtre · %1 exclu%2").arg(n).arg(n > 1 ? QStringLiteral("s") : QString()));
}

void BoutonFiltre::clignoter() {
  ++m_clignotements;
  m_etapes = 0;
  allumer(true);
  m_minuterie->start();
}

void BoutonFiltre::allumer(bool oui) {
  m_allume = oui;
  const CouleursInterface& c = Theme::instance().c();
  setStyleSheet(oui ? QStringLiteral("QToolButton { background: %1; color: %2; border-radius: 4px; }").arg(c.alerte.name(), c.texte.name()) : QString());
}

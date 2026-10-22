// Prométhée : formulaire de panneau lié au document. Licence GPL-3.0-only.
#include "FormulaireLie.hpp"

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include "promethee/electrique_distribution.hpp"

QFormLayout* FormulaireLie::groupe(const QString& titre, const QString& nom) const {
  auto* g = new QGroupBox(titre);
  if (!nom.isEmpty()) g->setObjectName(nom);
  auto* f = new QFormLayout(g);
  f->setContentsMargins(6, 4, 6, 4);
  f->setSpacing(4);
  m_col->addWidget(g);
  return f;
}

QDoubleSpinBox* FormulaireLie::reel(QFormLayout* f, const QString& etiquette, double valeur, double min, double max, int decimales, const QString& suffixe, const QString& nom,
                                    const std::function<void(double)>& appliquer, const QString& bulle, const QString& auto0) const {
  auto* sp = new QDoubleSpinBox;
  sp->setObjectName(nom);
  sp->setMinimumWidth(64);  // les grandes valeurs admises n'élargissent pas le panneau
  sp->setRange(min, max);
  sp->setDecimals(decimales);
  sp->setSuffix(suffixe);
  if (!auto0.isEmpty()) sp->setSpecialValueText(auto0);
  sp->setValue(valeur);
  sp->setKeyboardTracking(false);
  if (!bulle.isEmpty()) sp->setToolTip(bulle);
  QObject::connect(sp, &QDoubleSpinBox::editingFinished, m_contexte, [sp, valeur, appliquer] {
    if (std::abs(sp->value() - valeur) > 1e-9) appliquer(sp->value());
  });
  f->addRow(etiquette, sp);
  return sp;
}

QSpinBox* FormulaireLie::entier(QFormLayout* f, const QString& etiquette, int valeur, int min, int max, const QString& nom, const std::function<void(int)>& appliquer,
                                const QString& bulle, const QString& auto0) const {
  auto* sp = new QSpinBox;
  sp->setObjectName(nom);
  sp->setMinimumWidth(56);
  sp->setRange(min, max);
  if (!auto0.isEmpty()) sp->setSpecialValueText(auto0);
  sp->setValue(valeur);
  sp->setKeyboardTracking(false);
  if (!bulle.isEmpty()) sp->setToolTip(bulle);
  QObject::connect(sp, &QSpinBox::editingFinished, m_contexte, [sp, valeur, appliquer] {
    if (sp->value() != valeur) appliquer(sp->value());
  });
  f->addRow(etiquette, sp);
  return sp;
}

QComboBox* FormulaireLie::choix(QFormLayout* f, const QString& etiquette, const QStringList& items, int courant, const QString& nom, const std::function<void(int)>& appliquer,
                                const QString& bulle) const {
  auto* c = new QComboBox;
  c->setObjectName(nom);
  c->addItems(items);
  c->setCurrentIndex(std::clamp(courant, 0, static_cast<int>(items.size()) - 1));
  if (!bulle.isEmpty()) c->setToolTip(bulle);
  QObject::connect(c, &QComboBox::currentIndexChanged, m_contexte, [courant, appliquer](int i) {
    if (i != courant && i >= 0) appliquer(i);
  });
  f->addRow(etiquette, c);
  return c;
}

QCheckBox* FormulaireLie::coche(QFormLayout* f, const QString& texte, bool valeur, const QString& nom, const std::function<void(bool)>& appliquer, const QString& bulle) const {
  auto* c = new QCheckBox(texte);
  c->setObjectName(nom);
  c->setChecked(valeur);
  if (!bulle.isEmpty()) c->setToolTip(bulle);
  QObject::connect(c, &QCheckBox::toggled, m_contexte, [appliquer](bool v) { appliquer(v); });
  f->addRow(QString(), c);
  return c;
}

QLineEdit* FormulaireLie::texte(QFormLayout* f, const QString& etiquette, const std::string& valeur, const QString& nom,
                                const std::function<void(const std::string&)>& appliquer) const {
  auto* e = new QLineEdit(QString::fromStdString(valeur));
  e->setObjectName(nom);
  e->setCursorPosition(0);  // le début du nom, pas sa fin
  QObject::connect(e, &QLineEdit::editingFinished, m_contexte, [e, valeur, appliquer] {
    const std::string v = e->text().trimmed().toStdString();
    if (v != valeur && !v.empty()) appliquer(v);
  });
  f->addRow(etiquette, e);
  return e;
}

void FormulaireLie::avis(const std::vector<prom::elec::AvisCalcul>& liste, const QString& nom) const {
  if (liste.empty()) return;
  auto* g = new QGroupBox(QStringLiteral("Avis"));
  g->setObjectName(nom);
  auto* v = new QVBoxLayout(g);
  v->setSpacing(4);
  using prom::elec::NiveauAvis;
  for (const auto& a : liste) {
    const QString signe = a.niveau == NiveauAvis::Erreur ? QStringLiteral("⛔") : a.niveau == NiveauAvis::Alerte ? QStringLiteral("⚠") : QStringLiteral("ⓘ");
    const QString couleur = a.niveau == NiveauAvis::Erreur ? QStringLiteral("#C2402E") : a.niveau == NiveauAvis::Alerte ? QStringLiteral("#A8620E") : QStringLiteral("#4A5270");
    auto* l = new QLabel(QStringLiteral("<span style='color:%1'>%2 %3</span> <span style='color:#8A91AD'>(%4)</span>")
                             .arg(couleur, signe, QString::fromStdString(a.message).toHtmlEscaped(), QString::fromStdString(a.regle).toHtmlEscaped()));
    l->setWordWrap(true);
    v->addWidget(l);
  }
  m_col->addWidget(g);
}

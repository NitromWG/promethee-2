// Prométhée : champ numérique qui accepte le point comme la virgule (pavé numérique). Licence GPL-3.0-only.
#pragma once

#include <QDoubleSpinBox>
#include <QLocale>

class ChampNombre : public QDoubleSpinBox {
public:
  using QDoubleSpinBox::QDoubleSpinBox;
  QValidator::State validate(QString& texte, int& position) const override {
    convertir(texte);
    return QDoubleSpinBox::validate(texte, position);
  }
  double valueFromText(const QString& texte) const override {
    QString t = texte;
    convertir(t);
    return QDoubleSpinBox::valueFromText(t);
  }

private:
  void convertir(QString& t) const {
    const QString separateur = locale().decimalPoint();
    if (separateur != QStringLiteral(".")) t.replace(QLatin1Char('.'), separateur);
  }
};

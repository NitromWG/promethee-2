// Prométhée : disposition en cercle (lot L5). Licence GPL-3.0-only.
#include "DialogueCercle.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>

using namespace prom::ecad;

DialogueCercle::DialogueCercle(Pt centre, QWidget* parent) : QDialog(parent) {
  setObjectName(QStringLiteral("dialogueCercle"));
  setWindowTitle(QStringLiteral("Disposer en cercle"));
  auto* fl = new QFormLayout(this);
  auto mm = [](double v, double min, double max, const QString& suffixe = QStringLiteral(" mm")) {
    auto* b = new QDoubleSpinBox;
    b->setDecimals(3);
    b->setRange(min, max);
    b->setValue(v);
    b->setSuffix(suffixe);
    return b;
  };
  centreX = mm(prom::ecad::mm(centre.x), -10000, 10000);
  centreX->setObjectName(QStringLiteral("centreX"));
  centreY = mm(prom::ecad::mm(centre.y), -10000, 10000);
  centreY->setObjectName(QStringLiteral("centreY"));
  rayon = mm(20, 0.1, 5000);
  rayon->setObjectName(QStringLiteral("rayon"));
  depart = mm(-90, -360, 360, QStringLiteral(" °"));
  depart->setObjectName(QStringLiteral("angleDepart"));
  balayage = mm(360, -360, 360, QStringLiteral(" °"));
  balayage->setObjectName(QStringLiteral("balayage"));
  versCentre = new QCheckBox(QStringLiteral("Tournées vers le centre"));
  versCentre->setChecked(true);
  renumeroter = new QCheckBox(QStringLiteral("Renuméroter dans l’ordre du cercle"));
  renumeroter->setChecked(true);
  fl->addRow(QStringLiteral("Centre X"), centreX);
  fl->addRow(QStringLiteral("Centre Y"), centreY);
  fl->addRow(QStringLiteral("Rayon"), rayon);
  fl->addRow(QStringLiteral("Angle de départ"), depart);
  fl->addRow(QStringLiteral("Balayage"), balayage);
  fl->addRow(QString(), versCentre);
  fl->addRow(QString(), renumeroter);
  auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
  fl->addRow(bb);
}

OptionsCercle DialogueCercle::options() const {
  OptionsCercle o;
  o.centre = ptMm(centreX->value(), centreY->value());
  o.rayon = nm(rayon->value());
  o.angleDepart = depart->value();
  o.balayage = balayage->value();
  o.versCentre = versCentre->isChecked();
  o.renumeroter = renumeroter->isChecked();
  return o;
}

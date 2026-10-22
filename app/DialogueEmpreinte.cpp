// Prométhée : création d'une empreinte par tableau de pastilles. Licence GPL-3.0-only.
#include "DialogueEmpreinte.hpp"

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace prom::ecad;

class ApercuEmpreinte : public QWidget {
 public:
  explicit ApercuEmpreinte(QWidget* parent = nullptr) : QWidget(parent) { setMinimumSize(320, 280); }
  void setEmpreinte(const DefEmpreinte& d) {
    m_d = d;
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    g.fillRect(rect(), QColor(0x0B, 0x0F, 0x24));
    Boite b = m_d.encombrement;
    for (const auto& p : m_d.pastilles) b.etendre(boite(formePastille(p)));
    if (b.vide()) return;
    const double echelle = std::min((width() - 30) / mm(b.largeur()), (height() - 30) / mm(b.hauteur()));
    const QPointF centre(width() / 2.0, height() / 2.0);
    auto e = [&](Pt p) { return centre + QPointF((mm(p.x) - mm(b.centre().x)) * echelle, (mm(p.y) - mm(b.centre().y)) * echelle); };
    for (const auto& t : m_d.traits) {
      const QColor col = t.couche == Couche::SerigraphieDessus ? QColor(0xF2, 0xF2, 0xF2) : t.couche == Couche::EncombrementDessus ? QColor(0xE0, 0x6C, 0xD0) : QColor(0x8A, 0x91, 0xAC);
      g.setPen(QPen(col, std::max(1.0, mm(t.epaisseur) * echelle), t.couche == Couche::EncombrementDessus ? Qt::DashLine : Qt::SolidLine));
      g.setBrush(Qt::NoBrush);
      if (t.rayon > 0) g.drawEllipse(e(t.a), mm(t.rayon) * echelle, mm(t.rayon) * echelle);
      else g.drawLine(e(t.a), e(t.b));
    }
    QFont f = font();
    f.setPixelSize(std::clamp(static_cast<int>(echelle * 0.7), 8, 16));
    f.setBold(true);
    g.setFont(f);
    for (const auto& p : m_d.pastilles) {
      QPainterPath chemin;
      const Polygone poly = formePastille(p);
      if (poly.size() < 3) continue;
      chemin.moveTo(e(poly.front()));
      for (std::size_t i = 1; i < poly.size(); ++i) chemin.lineTo(e(poly[i]));
      chemin.closeSubpath();
      g.fillPath(chemin, QColor(0xD9, 0xB6, 0x4A));
      if (p.percage > 0) {
        g.setPen(Qt::NoPen);
        g.setBrush(QColor(0x0B, 0x0F, 0x24));
        g.drawEllipse(e(p.position), mm(p.percage) / 2 * echelle, mm(p.percage) / 2 * echelle);
      }
      g.setPen(Qt::white);
      g.drawText(QRectF(e(p.position) - QPointF(20, 10), QSizeF(40, 20)), Qt::AlignCenter, QString::fromStdString(p.numero));
    }
  }

 private:
  DefEmpreinte m_d;
};

DialogueEmpreinte::DialogueEmpreinte(int voulues, QWidget* parent) : QDialog(parent), m_voulues(voulues) {
  setWindowTitle(QStringLiteral("Créer une empreinte"));
  auto* col = new QHBoxLayout(this);
  auto* gauche = new QVBoxLayout;
  auto* fl = new QFormLayout;
  m_nom = new QLineEdit(QStringLiteral("Connecteur"));
  fl->addRow(QStringLiteral("Nom"), m_nom);
  m_type = new QComboBox;
  m_type->addItem(QStringLiteral("Traversante"), static_cast<int>(TypePastille::Traversante));
  m_type->addItem(QStringLiteral("CMS"), static_cast<int>(TypePastille::CMS));
  m_type->addItem(QStringLiteral("Trou non métallisé"), static_cast<int>(TypePastille::NonMetallisee));
  fl->addRow(QStringLiteral("Pastilles"), m_type);
  m_forme = new QComboBox;
  m_forme->addItem(QStringLiteral("Rondes"), static_cast<int>(FormePastille::Rond));
  m_forme->addItem(QStringLiteral("Ovales"), static_cast<int>(FormePastille::Ovale));
  m_forme->addItem(QStringLiteral("Rectangulaires"), static_cast<int>(FormePastille::Rect));
  m_forme->addItem(QStringLiteral("Rectangles arrondis"), static_cast<int>(FormePastille::RectArrondi));
  fl->addRow(QStringLiteral("Forme"), m_forme);
  auto entier = [](int mini, int maxi, int v) {
    auto* b = new QSpinBox;
    b->setRange(mini, maxi);
    b->setValue(v);
    return b;
  };
  auto reel = [](double mini, double maxi, double v, double pas = 0.01) {
    auto* b = new QDoubleSpinBox;
    b->setRange(mini, maxi);
    b->setDecimals(3);
    b->setSingleStep(pas);
    b->setSuffix(QStringLiteral(" mm"));
    b->setValue(v);
    return b;
  };
  m_rangees = entier(1, 64, 1);
  m_colonnes = entier(1, 256, std::max(1, voulues));
  fl->addRow(QStringLiteral("Rangées"), m_rangees);
  fl->addRow(QStringLiteral("Colonnes"), m_colonnes);
  m_pasX = reel(0.1, 50, 2.54);
  m_pasY = reel(0.1, 50, 2.54);
  fl->addRow(QStringLiteral("Pas entre colonnes"), m_pasX);
  fl->addRow(QStringLiteral("Pas entre rangées"), m_pasY);
  m_largeur = reel(0.1, 20, 1.7);
  m_hauteur = reel(0.1, 20, 1.7);
  m_percage = reel(0.1, 10, 1.0);
  fl->addRow(QStringLiteral("Largeur des pastilles"), m_largeur);
  fl->addRow(QStringLiteral("Hauteur des pastilles"), m_hauteur);
  fl->addRow(QStringLiteral("Perçage"), m_percage);
  m_numerotation = new QComboBox;
  m_numerotation->addItem(QStringLiteral("Par rangée (1, 2, 3… sur la première rangée)"), static_cast<int>(SpecEmpreinte::Numerotation::ParRangee));
  m_numerotation->addItem(QStringLiteral("Par colonne (barrette double : 1 et 2 l’un sous l’autre)"), static_cast<int>(SpecEmpreinte::Numerotation::ParColonne));
  m_numerotation->addItem(QStringLiteral("En tour (boîtier DIL : en bas de gauche à droite, puis en haut)"), static_cast<int>(SpecEmpreinte::Numerotation::Tour));
  fl->addRow(QStringLiteral("Numérotation"), m_numerotation);
  m_carree = new QCheckBox(QStringLiteral("Broche 1 carrée"));
  m_carree->setChecked(true);
  fl->addRow(m_carree);
  m_corps = reel(0.1, 100, 8.5, 0.1);
  fl->addRow(QStringLiteral("Hauteur par défaut de l’empreinte"), m_corps);  // distincte de la hauteur d'un composant (T2)
  gauche->addLayout(fl);
  m_bilan = new QLabel;
  m_bilan->setWordWrap(true);
  gauche->addWidget(m_bilan);
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  m_creer = boutons->button(QDialogButtonBox::Ok);
  m_creer->setText(QStringLiteral("Créer"));
  boutons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annuler"));
  connect(boutons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  gauche->addWidget(boutons);
  col->addLayout(gauche);
  m_apercu = new ApercuEmpreinte;
  col->addWidget(m_apercu, 1);
  for (QComboBox* b : {m_type, m_forme, m_numerotation}) connect(b, &QComboBox::currentIndexChanged, this, [this] { majApercu(); });
  for (QSpinBox* b : {m_rangees, m_colonnes}) connect(b, &QSpinBox::valueChanged, this, [this] { majApercu(); });
  for (QDoubleSpinBox* b : {m_pasX, m_pasY, m_largeur, m_hauteur, m_percage, m_corps}) connect(b, &QDoubleSpinBox::valueChanged, this, [this] { majApercu(); });
  connect(m_carree, &QCheckBox::toggled, this, [this] { majApercu(); });
  connect(m_nom, &QLineEdit::textChanged, this, [this] { majApercu(); });
  majApercu();
}

void DialogueEmpreinte::setContexte(const std::set<std::string>& idsPris, prom::ecad::Nm couronneMin) {
  m_pris = idsPris;
  m_couronneMin = couronneMin;
  // Nom proposé toujours libre : « Connecteur », sinon « Connecteur-2 », « Connecteur-3 »…
  const QString base = m_nom->text().trimmed().isEmpty() ? QStringLiteral("Connecteur") : m_nom->text().trimmed();
  QString nom = base;
  for (int i = 2; m_pris.count(identifiantEmpreintePerso(nom.toStdString())); ++i) nom = base + QStringLiteral("-%1").arg(i);
  m_nom->setText(nom);
  majApercu();
}

SpecEmpreinte DialogueEmpreinte::spec() const {
  SpecEmpreinte s;
  s.nom = m_nom->text().trimmed().toStdString();
  s.type = static_cast<TypePastille>(m_type->currentData().toInt());
  s.forme = static_cast<FormePastille>(m_forme->currentData().toInt());
  s.rangees = m_rangees->value();
  s.colonnes = m_colonnes->value();
  s.pasX = m_pasX->value();
  s.pasY = m_pasY->value();
  s.largeur = m_largeur->value();
  s.hauteur = m_hauteur->value();
  s.percage = m_percage->value();
  s.numerotation = static_cast<SpecEmpreinte::Numerotation>(m_numerotation->currentData().toInt());
  s.premiereCarree = m_carree->isChecked();
  s.hauteurCorps = m_corps->value();
  return s;
}

void DialogueEmpreinte::setSpec(const SpecEmpreinte& s) {
  m_nom->setText(QString::fromStdString(s.nom));
  m_type->setCurrentIndex(std::max(0, m_type->findData(static_cast<int>(s.type))));
  m_forme->setCurrentIndex(std::max(0, m_forme->findData(static_cast<int>(s.forme))));
  m_rangees->setValue(s.rangees);
  m_colonnes->setValue(s.colonnes);
  m_pasX->setValue(s.pasX);
  m_pasY->setValue(s.pasY);
  m_largeur->setValue(s.largeur);
  m_hauteur->setValue(s.hauteur);
  m_percage->setValue(s.percage);
  m_numerotation->setCurrentIndex(std::max(0, m_numerotation->findData(static_cast<int>(s.numerotation))));
  m_carree->setChecked(s.premiereCarree);
  m_corps->setValue(s.hauteurCorps);
  majApercu();
}

void DialogueEmpreinte::majApercu() {
  const bool trou = m_type->currentData().toInt() != static_cast<int>(TypePastille::CMS);
  m_percage->setEnabled(trou);
  m_hauteur->setEnabled(m_forme->currentData().toInt() != static_cast<int>(FormePastille::Rond));
  m_numerotation->model()->setData(m_numerotation->model()->index(2, 0), m_rangees->value() == 2 ? QVariant() : QVariant(0), Qt::UserRole - 1);
  const DefEmpreinte d = empreinte();
  m_apercu->setEmpreinte(d);
  const int n = static_cast<int>(d.pastilles.size());
  QString t = QStringLiteral("<b>%1</b> · %2 pastille(s) · encombrement %3 × %4 mm")
                  .arg(QString::fromStdString(d.id)).arg(n).arg(QLocale().toString(mm(d.encombrement.largeur()), 'f', 2), QLocale().toString(mm(d.encombrement.hauteur()), 'f', 2));
  if (m_voulues > 0 && n != m_voulues) t += QStringLiteral("<br><span style='color:#A8620E'>Le symbole a %1 broche(s).</span>").arg(m_voulues);
  // Une valeur hors bornes est refusée avec sa raison, jamais ramenée en silence (B15)
  const std::string refus = verifierSpecEmpreinte(spec(), m_couronneMin);
  if (!refus.empty()) t += QStringLiteral("<br><span style='color:#C2402E'>%1</span>").arg(QString::fromStdString(refus).toHtmlEscaped());
  if (m_pasX->value() <= m_largeur->value() && m_colonnes->value() > 1) t += QStringLiteral("<br><span style='color:#C2402E'>Les pastilles se touchent entre colonnes.</span>");
  // Un nom déjà pris ne remplace rien en silence (B10) : la création demandera confirmation
  const bool pris = m_pris.count(d.id) > 0;
  if (pris) t += QStringLiteral("<br><span style='color:#A8620E'>Ce nom est déjà pris : créer demandera s’il faut remplacer l’empreinte existante.</span>");
  if (m_creer) {
    m_creer->setEnabled(refus.empty() && !m_nom->text().trimmed().isEmpty());
    m_creer->setText(pris ? QStringLiteral("Remplacer…") : QStringLiteral("Créer"));
  }
  m_bilan->setText(t);
}

// Prométhée : insertion d'un dessin DXF dans l'esquisse en cours. Licence GPL-3.0-only.
#include "DialogueImportDxf.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

using namespace prom;
using namespace prom::dxf;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QString nb(double v, int dec = 2) { return QLocale().toString(v, 'f', dec); }
}  // namespace

class ApercuDxf : public QWidget {
 public:
  explicit ApercuDxf(QWidget* parent = nullptr) : QWidget(parent) { setMinimumSize(380, 300); }
  void setEsquisse(const Esquisse& e) {
    m_e = e;
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    const QColor fond = palette().color(QPalette::Base), encre = palette().color(QPalette::Text);
    g.fillRect(rect(), fond);
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // l'origine reste dans le cadre
    auto etendre = [&](double x, double y) {
      x0 = std::min(x0, x);
      y0 = std::min(y0, y);
      x1 = std::max(x1, x);
      y1 = std::max(y1, y);
    };
    for (const auto& p : m_e.points) etendre(p.x, p.y);
    for (const auto& el : m_e.elements)
      if (el.type == TypeElement::Cercle)
        if (const PointEsquisse* c = m_e.point(el.centre)) {
          etendre(c->x - el.rayon, c->y - el.rayon);
          etendre(c->x + el.rayon, c->y + el.rayon);
        }
    const double l = std::max({x1 - x0, y1 - y0, 1e-6});
    const double e = std::min((width() - 30) / std::max(x1 - x0, l * 0.05), (height() - 30) / std::max(y1 - y0, l * 0.05));
    const QPointF centre(width() / 2.0 - (x0 + x1) / 2 * e, height() / 2.0 + (y0 + y1) / 2 * e);
    auto E = [&](double x, double y) { return centre + QPointF(x * e, -y * e); };
    // Origine de l'esquisse
    g.setPen(QPen(QColor(0xC2, 0x40, 0x2E), 1));
    g.drawLine(E(0, 0) + QPointF(-10, 0), E(0, 0) + QPointF(10, 0));
    g.drawLine(E(0, 0) + QPointF(0, -10), E(0, 0) + QPointF(0, 10));
    auto couleur = [&](const ElementEsquisse& el) {
      for (const auto& k : m_e.calques)
        if (k.nom == el.calque && k.couleur != "#000000" && k.couleur != "#FFFFFF") return QColor(q(k.couleur));
      return encre;
    };
    for (const auto& el : m_e.elements) {
      g.setPen(QPen(couleur(el), 1.2, el.construction ? Qt::DashLine : Qt::SolidLine));
      g.setBrush(Qt::NoBrush);
      const PointEsquisse *a = m_e.point(el.p1), *b = m_e.point(el.p2), *c = m_e.point(el.centre);
      if (el.type == TypeElement::Ligne && a && b) g.drawLine(E(a->x, a->y), E(b->x, b->y));
      else if (el.type == TypeElement::Cercle && c) g.drawEllipse(E(c->x, c->y), el.rayon * e, el.rayon * e);
      else if (el.type == TypeElement::Arc && a && b && c) {
        const double r = std::hypot(a->x - c->x, a->y - c->y);
        const double d0 = std::atan2(a->y - c->y, a->x - c->x) * 180 / std::numbers::pi;
        double d1 = std::atan2(b->y - c->y, b->x - c->x) * 180 / std::numbers::pi;
        while (d1 <= d0) d1 += 360;
        QPainterPath p;
        const QPointF ec = E(c->x, c->y);
        p.arcMoveTo(QRectF(ec.x() - r * e, ec.y() - r * e, 2 * r * e, 2 * r * e), d0);
        p.arcTo(QRectF(ec.x() - r * e, ec.y() - r * e, 2 * r * e, 2 * r * e), d0, d1 - d0);
        g.drawPath(p);
      }
    }
  }

 private:
  Esquisse m_e;
};

DialogueImportDxf::DialogueImportDxf(DessinDxf dessin, const QString& nomFichier, QWidget* parent) : QDialog(parent), m_dessin(std::move(dessin)) {
  setWindowTitle(QStringLiteral("Insérer un DXF dans l’esquisse"));
  auto* colonnes = new QHBoxLayout(this);
  auto* gauche = new QVBoxLayout;
  auto* titre = new QLabel(QStringLiteral("<b>%1</b><br><span style='color:#6B7393'>Unité du fichier : %2%3</span>")
                               .arg(nomFichier.toHtmlEscaped(), q(m_dessin.unite), m_dessin.uniteDeclaree ? QString() : QStringLiteral(" (non déclarée : mm supposé)")));
  titre->setWordWrap(true);
  gauche->addWidget(titre);
  auto* gp = new QGroupBox(QStringLiteral("Taille et position"));
  auto* fl = new QFormLayout(gp);
  m_unite = new QComboBox;
  m_unite->setObjectName(QStringLiteral("uniteDxf"));
  m_unite->addItem(QStringLiteral("Celle du fichier (%1)").arg(q(m_dessin.unite)), 0.0);
  for (const auto& [nom, f] : std::initializer_list<std::pair<const char*, double>>{{"mm", 1.0}, {"cm", 10.0}, {"m", 1000.0}, {"pouce", 25.4}, {"pied", 304.8}, {"mil", 0.0254}})
    m_unite->addItem(QString::fromUtf8(nom), f);
  m_unite->setToolTip(QStringLiteral("Un DXF sans unité déclarée, ou mal déclarée, se corrige ici : les nombres du fichier sont lus dans cette unité."));
  fl->addRow(QStringLiteral("Unité"), m_unite);
  m_echelle = new QDoubleSpinBox;
  m_echelle->setRange(0.0001, 10000);
  m_echelle->setDecimals(4);
  m_echelle->setValue(1);
  fl->addRow(QStringLiteral("Échelle"), m_echelle);
  m_placement = new QComboBox;
  m_placement->addItem(QStringLiteral("Origine du DXF sur l’origine"), static_cast<int>(PlacementDxf::CommeLeFichier));
  m_placement->addItem(QStringLiteral("Centre du dessin sur l’origine"), static_cast<int>(PlacementDxf::Centre));
  m_placement->addItem(QStringLiteral("Coin bas-gauche sur l’origine"), static_cast<int>(PlacementDxf::CoinBasGauche));
  fl->addRow(QStringLiteral("Placement"), m_placement);
  auto mm = [] {
    auto* b = new QDoubleSpinBox;
    b->setRange(-1e6, 1e6);
    b->setDecimals(3);
    b->setSuffix(QStringLiteral(" mm"));
    return b;
  };
  m_dx = mm();
  m_dy = mm();
  fl->addRow(QStringLiteral("Décalage X"), m_dx);
  fl->addRow(QStringLiteral("Décalage Y"), m_dy);
  gauche->addWidget(gp);
  auto* gc = new QGroupBox(QStringLiteral("Calques"));
  auto* lc = new QVBoxLayout(gc);
  m_calques = new QListWidget;
  m_calques->setObjectName(QStringLiteral("calquesDxf"));
  for (const auto& k : m_dessin.calques) {
    if (k.traits == 0) continue;
    auto* it = new QListWidgetItem(QStringLiteral("%1  ·  %2 trait(s)%3").arg(q(k.nom)).arg(k.traits).arg(k.visible ? QString() : QStringLiteral("  ·  éteint ou gelé")));
    it->setData(Qt::UserRole, q(k.nom));
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(k.visible ? Qt::Checked : Qt::Unchecked);
    QPixmap pm(12, 12);
    pm.fill(QColor(q(couleurAci(k.couleur))));
    it->setIcon(QIcon(pm));
    m_calques->addItem(it);
  }
  m_calques->setMaximumHeight(150);
  lc->addWidget(m_calques);
  m_calquesEsquisse = new QCheckBox(QStringLiteral("Calques de l’esquisse à leur couleur"));
  m_calquesEsquisse->setChecked(true);
  lc->addWidget(m_calquesEsquisse);
  gauche->addWidget(gc);
  auto* go = new QGroupBox(QStringLiteral("Géométrie"));
  auto* lo = new QVBoxLayout(go);
  m_relations = new QCheckBox(QStringLiteral("Relations horizontale et verticale sur les lignes droites"));
  m_relations->setChecked(true);
  m_fixer = new QCheckBox(QStringLiteral("Fixer la géométrie (entièrement contrainte)"));
  m_fixer->setToolTip(QStringLiteral("Chaque point est fixé et chaque cercle coté : le profil importé ne bouge plus. Décoché, il se cote et se modifie comme un tracé."));
  m_construction = new QCheckBox(QStringLiteral("En géométrie de construction (gabarit à décalquer)"));
  for (QCheckBox* c : {m_relations, m_fixer, m_construction}) lo->addWidget(c);
  gauche->addWidget(go);
  m_bilan = new QLabel;
  m_bilan->setWordWrap(true);
  gauche->addWidget(m_bilan);
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  boutons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Insérer"));
  boutons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annuler"));
  connect(boutons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  gauche->addWidget(boutons);
  colonnes->addLayout(gauche);
  m_apercu = new ApercuDxf;
  colonnes->addWidget(m_apercu, 1);
  for (QComboBox* c : {m_unite, m_placement}) connect(c, &QComboBox::currentIndexChanged, this, [this] { majApercu(); });
  for (QDoubleSpinBox* b : {m_echelle, m_dx, m_dy}) connect(b, &QDoubleSpinBox::valueChanged, this, [this] { majApercu(); });
  for (QCheckBox* c : {m_calquesEsquisse, m_relations, m_fixer, m_construction}) connect(c, &QCheckBox::toggled, this, [this] { majApercu(); });
  connect(m_fixer, &QCheckBox::toggled, m_relations, [this](bool on) { m_relations->setEnabled(!on); });
  connect(m_calques, &QListWidget::itemChanged, this, [this] { majApercu(); });
  majApercu();
}

OptionsImportDxf DialogueImportDxf::options() const {
  OptionsImportDxf o;
  o.facteurMm = m_unite->currentData().toDouble();
  o.echelle = m_echelle->value();
  o.placement = static_cast<PlacementDxf>(m_placement->currentData().toInt());
  o.dx = m_dx->value();
  o.dy = m_dy->value();
  // Liste explicite : un calque éteint coché est repris, un calque visible décoché est laissé
  for (int i = 0; i < m_calques->count(); ++i)
    if (m_calques->item(i)->checkState() == Qt::Checked) o.calques.insert(m_calques->item(i)->data(Qt::UserRole).toString().toStdString());
  o.calquesChoisis = true;
  o.calquesEsquisse = m_calquesEsquisse->isChecked();
  o.horizontalesVerticales = m_relations->isChecked();
  o.fixer = m_fixer->isChecked();
  o.construction = m_construction->isChecked();
  return o;
}

void DialogueImportDxf::setOptions(const OptionsImportDxf& o) {
  m_unite->setCurrentIndex(std::max(0, m_unite->findData(o.facteurMm)));
  m_echelle->setValue(o.echelle);
  m_placement->setCurrentIndex(std::max(0, m_placement->findData(static_cast<int>(o.placement))));
  m_dx->setValue(o.dx);
  m_dy->setValue(o.dy);
  if (!o.calques.empty())
    for (int i = 0; i < m_calques->count(); ++i)
      m_calques->item(i)->setCheckState(o.calques.count(m_calques->item(i)->data(Qt::UserRole).toString().toStdString()) ? Qt::Checked : Qt::Unchecked);
  m_calquesEsquisse->setChecked(o.calquesEsquisse);
  m_relations->setChecked(o.horizontalesVerticales);
  m_fixer->setChecked(o.fixer);
  m_construction->setChecked(o.construction);
  majApercu();
}

RapportImportDxf DialogueImportDxf::essai() const {
  Esquisse e;
  return importerDxf(e, m_dessin, options());
}

void DialogueImportDxf::majApercu() {
  Esquisse e;
  const RapportImportDxf r = importerDxf(e, m_dessin, options());
  m_apercu->setEsquisse(e);
  QString t = QStringLiteral("<b>%1 ligne(s), %2 arc(s), %3 cercle(s)</b>").arg(r.lignes).arg(r.arcs).arg(r.cercles);
  if (r.points) t += QStringLiteral(", %1 point(s)").arg(r.points);
  if (r.total() > 0)
    t += QStringLiteral("<br>Encombrement %1 × %2 mm, de (%3 ; %4) à (%5 ; %6)")
             .arg(nb(r.xmax - r.xmin), nb(r.ymax - r.ymin), nb(r.xmin), nb(r.ymin), nb(r.xmax), nb(r.ymax));
  if (r.reunis) t += QStringLiteral("<br><span style='color:#2E8B57'>%1 extrémité(s) réunie(s) : les contours restent fermés.</span>").arg(r.reunis);
  for (const auto& a : r.alertes)
    if (a != "Rien à insérer.") t += QStringLiteral("<br><span style='color:#A8620E'>%1</span>").arg(q(a).toHtmlEscaped());
  for (const auto& a : m_dessin.alertes) t += QStringLiteral("<br><span style='color:#A8620E'>%1</span>").arg(q(a).toHtmlEscaped());
  m_bilan->setText(t);
  if (auto* b = findChild<QDialogButtonBox*>()) b->button(QDialogButtonBox::Ok)->setEnabled(r.total() > 0);
}

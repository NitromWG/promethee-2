// Prométhée : fenêtre de simulation (lot L11). Licence GPL-3.0-only.
#include "FenetreSimulation.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QDateTime>
#include <QFileDialog>
#include <QInputDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "SelectionElectronique.hpp"
#include "VueSchema.hpp"
#include "promethee/fichiers.hpp"
#include "FilDeFond.hpp"
#include "promethee/csv.hpp"
#include "promethee/valeurs.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
// Nombre écrit avec un préfixe SI : 1,59 k ; 10 µ
QString si(double v, int chiffres = 3) {
  if (v == 0 || !std::isfinite(v)) return QStringLiteral("0");
  static const std::pair<double, const char*> prefixes[] = {{1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1, ""}, {1e-3, "m"}, {1e-6, "µ"}, {1e-9, "n"}, {1e-12, "p"}};
  const double a = std::abs(v);
  for (const auto& [f, p] : prefixes)
    if (a >= f * 0.9999) return QLocale().toString(v / f, 'g', chiffres) + QString::fromUtf8(p);
  return QLocale().toString(v, 'g', chiffres);
}
std::vector<double> graduations(double a, double b, bool log) {
  std::vector<double> r;
  if (!(b > a)) return r;
  if (log) {
    for (double d = std::floor(std::log10(a)); d <= std::ceil(std::log10(b)); ++d)
      if (const double x = std::pow(10.0, d); x >= a * 0.999 && x <= b * 1.001) r.push_back(x);
    return r;
  }
  const double brut = (b - a) / 6, mag = std::pow(10.0, std::floor(std::log10(brut)));
  const double pas = brut / mag < 1.5 ? mag : brut / mag < 3.5 ? 2 * mag : brut / mag < 7.5 ? 5 * mag : 10 * mag;
  for (double x = std::ceil(a / pas) * pas; x <= b + pas * 1e-6; x += pas) r.push_back(std::abs(x) < pas * 1e-9 ? 0 : x);
  return r;
}
const QColor TEINTES[] = {QColor(0x2F, 0x6B, 0xF5), QColor(0xE0, 0x6C, 0x2A), QColor(0x2E, 0xA0, 0x5A), QColor(0xB0, 0x3A, 0xC8), QColor(0xC8, 0x3A, 0x4A), QColor(0x1A, 0x9A, 0xA8)};
}  // namespace

// ---------- Tracé ----------
TraceCourbes::TraceCourbes(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("traceCourbes"));
  setMinimumSize(420, 260);
  setMouseTracking(true);
}

void TraceCourbes::setCourbes(std::vector<Courbe> c, const QString& axeX, const QString& axeY, bool logX) {
  m_courbes = std::move(c);
  m_axeX = axeX;
  m_axeY = axeY;
  m_logX = logX;
  cadrer();
}

// Abscisses tracées d'une courbe : les siennes, ou celles de la première en tracé XY
namespace {
const std::vector<double>& abscisses(const std::vector<TraceCourbes::Courbe>& c, std::size_t i, bool xy) { return xy && c.size() > 1 ? c[0].y : c[i].x; }
}  // namespace

void TraceCourbes::cadrer() {
  bool premier = true;
  for (std::size_t j = 0; j < m_courbes.size(); ++j) {
    if (m_xy && m_courbes.size() > 1 && j == 0) continue;
    const auto& k = m_courbes[j];
    const auto& xs = abscisses(m_courbes, j, m_xy);
    for (std::size_t i = 0; i < xs.size() && i < k.y.size(); ++i) {
      if (!std::isfinite(k.y[i]) || (m_logX && xs[i] <= 0) || (m_logY && k.y[i] <= 0)) continue;
      if (premier) {
        m_x0 = m_x1 = xs[i];
        m_y0 = m_y1 = k.y[i];
        premier = false;
      }
      m_x0 = std::min(m_x0, xs[i]);
      m_x1 = std::max(m_x1, xs[i]);
      m_y0 = std::min(m_y0, k.y[i]);
      m_y1 = std::max(m_y1, k.y[i]);
    }
  }
  if (premier) m_x0 = m_logX ? 1 : 0, m_x1 = m_logX ? 10 : 1, m_y0 = m_logY ? 1 : 0, m_y1 = m_logY ? 10 : 1;
  if (m_x1 - m_x0 < 1e-300) m_x1 = m_x0 + 1;
  if (m_logY) {
    if (m_y1 / m_y0 < 1.0001) m_y0 /= 2, m_y1 *= 2;
  } else {
    if (m_y1 - m_y0 < 1e-12) m_y0 -= 0.5, m_y1 += 0.5;
    const double marge = (m_y1 - m_y0) * 0.06;
    m_y0 -= marge;
    m_y1 += marge;
  }
  m_curseurs[0] = m_logX ? std::pow(10.0, std::log10(m_x0) + (std::log10(m_x1) - std::log10(m_x0)) / 3) : m_x0 + (m_x1 - m_x0) / 3;
  m_curseurs[1] = m_logX ? std::pow(10.0, std::log10(m_x0) + 2 * (std::log10(m_x1) - std::log10(m_x0)) / 3) : m_x0 + 2 * (m_x1 - m_x0) / 3;
  update();
  emit curseursChanges();
}

void TraceCourbes::setLogY(bool v) {
  m_logY = v;
  cadrer();
}
void TraceCourbes::setXY(bool v) {
  m_xy = v;
  cadrer();
}

bool TraceCourbes::exporterPng(const QString& chemin) { return grab().save(chemin, "PNG"); }

std::string TraceCourbes::svg() const {
  // Dessin vectoriel écrit par Prométhée : cadre, graduations, courbes, légende ; 800 × 500 points
  const double L = 800, H = 500, x0 = 70, y0 = 20, w = L - 90, h = H - 70;
  auto X = [&](double x) { return x0 + (m_logX ? (std::log10(std::max(x, 1e-300)) - std::log10(m_x0)) / (std::log10(m_x1) - std::log10(m_x0)) : (x - m_x0) / (m_x1 - m_x0)) * w; };
  auto Y = [&](double y) { return y0 + h - (m_logY ? (std::log10(std::max(y, 1e-300)) - std::log10(m_y0)) / (std::log10(m_y1) - std::log10(m_y0)) : (y - m_y0) / (m_y1 - m_y0)) * h; };
  auto nb = [](double v) { return QString::number(v, 'f', 2).toStdString(); };
  auto esc = [](const QString& t) { return t.toHtmlEscaped().toStdString(); };
  std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" height=\"500\" viewBox=\"0 0 800 500\" font-family=\"sans-serif\" font-size=\"11\">\n";
  s += "<rect width=\"800\" height=\"500\" fill=\"white\"/>\n";
  for (double x : graduations(m_x0, m_x1, m_logX))
    s += "<line x1=\"" + nb(X(x)) + "\" y1=\"" + nb(y0) + "\" x2=\"" + nb(X(x)) + "\" y2=\"" + nb(y0 + h) + "\" stroke=\"#ddd\"/><text x=\"" + nb(X(x)) + "\" y=\"" + nb(y0 + h + 15) +
         "\" text-anchor=\"middle\">" + esc(si(x)) + "</text>\n";
  for (double y : graduations(m_y0, m_y1, m_logY))
    s += "<line x1=\"" + nb(x0) + "\" y1=\"" + nb(Y(y)) + "\" x2=\"" + nb(x0 + w) + "\" y2=\"" + nb(Y(y)) + "\" stroke=\"#ddd\"/><text x=\"" + nb(x0 - 6) + "\" y=\"" + nb(Y(y) + 4) +
         "\" text-anchor=\"end\">" + esc(si(y)) + "</text>\n";
  s += "<rect x=\"" + nb(x0) + "\" y=\"" + nb(y0) + "\" width=\"" + nb(w) + "\" height=\"" + nb(h) + "\" fill=\"none\" stroke=\"#333\"/>\n";
  s += "<text x=\"" + nb(x0 + w / 2) + "\" y=\"" + nb(H - 12) + "\" text-anchor=\"middle\">" + esc(m_axeX) + "</text>\n";
  s += "<text transform=\"translate(14," + nb(y0 + h / 2) + ") rotate(-90)\" text-anchor=\"middle\">" + esc(m_axeY) + "</text>\n";
  double ly = y0 + 14;
  for (std::size_t j = 0; j < m_courbes.size(); ++j) {
    const auto& k = m_courbes[j];
    if (m_xy && m_courbes.size() > 1 && j == 0) continue;
    const auto& xs = abscisses(m_courbes, j, m_xy);
    std::string pts;
    for (std::size_t i = 0; i < xs.size() && i < k.y.size(); ++i)
      if (std::isfinite(k.y[i]) && (!m_logX || xs[i] > 0) && (!m_logY || k.y[i] > 0)) pts += nb(X(xs[i])) + "," + nb(Y(k.y[i])) + " ";
    s += "<polyline fill=\"none\" stroke=\"" + k.couleur.name().toStdString() + "\" stroke-width=\"1.6\"" + (k.attenuee ? " stroke-opacity=\"0.3\"" : "") + " points=\"" + pts + "\"/>\n";
    if (!k.attenuee) {
      s += "<line x1=\"" + nb(x0 + w - 150) + "\" y1=\"" + nb(ly - 4) + "\" x2=\"" + nb(x0 + w - 130) + "\" y2=\"" + nb(ly - 4) + "\" stroke=\"" + k.couleur.name().toStdString() +
           "\" stroke-width=\"3\"/><text x=\"" + nb(x0 + w - 125) + "\" y=\"" + nb(ly) + "\">" + esc(k.nom) + "</text>\n";
      ly += 15;
    }
  }
  return s + "</svg>\n";
}

std::string TraceCourbes::csv() const {
  // Une paire de colonnes par courbe tracée (abscisse, ordonnée), au réglage CSV du poste
  prom::EcrivainCsv w;
  std::vector<std::string> tete;
  std::size_t n = 0;
  for (const auto& k : m_courbes) {
    tete.push_back(m_axeX.toStdString() + " — " + k.nom.toStdString());
    tete.push_back(k.nom.toStdString());
    n = std::max(n, k.y.size());
  }
  w.ligne(tete);
  for (std::size_t i = 0; i < n; ++i) {
    for (const auto& k : m_courbes) {
      if (i < k.x.size() && i < k.y.size()) {
        w.nombre(k.x[i], 9);
        w.nombre(k.y[i], 9);
      } else {
        w.champ({});
        w.champ({});
      }
    }
    w.fin();
  }
  return w.texte();
}

void TraceCourbes::setCurseur(int i, double x) {
  m_curseurs[i] = std::clamp(x, m_x0, m_x1);
  update();
  emit curseursChanges();
}

QRectF TraceCourbes::zone() const { return QRectF(64, 16, std::max(10, width() - 64 - 16), std::max(10, height() - 16 - 44)); }
double TraceCourbes::versEcranX(double x) const {
  const QRectF z = zone();
  const double t = m_logX ? (std::log10(std::max(x, 1e-30)) - std::log10(m_x0)) / (std::log10(m_x1) - std::log10(m_x0)) : (x - m_x0) / (m_x1 - m_x0);
  return z.left() + t * z.width();
}
double TraceCourbes::depuisEcranX(double px) const {
  const QRectF z = zone();
  const double t = std::clamp((px - z.left()) / z.width(), 0.0, 1.0);
  return m_logX ? std::pow(10.0, std::log10(m_x0) + t * (std::log10(m_x1) - std::log10(m_x0))) : m_x0 + t * (m_x1 - m_x0);
}

void TraceCourbes::paintEvent(QPaintEvent*) {
  QPainter g(this);
  g.setRenderHint(QPainter::Antialiasing);
  const QColor fond = palette().color(QPalette::Base), texte = palette().color(QPalette::Text);
  QColor grille = texte;
  grille.setAlphaF(0.15);
  g.fillRect(rect(), fond);
  const QRectF z = zone();
  auto Y = [&](double y) {
    const double t = m_logY ? (std::log10(std::max(y, 1e-300)) - std::log10(m_y0)) / (std::log10(m_y1) - std::log10(m_y0)) : (y - m_y0) / (m_y1 - m_y0);
    return z.bottom() - t * z.height();
  };
  g.setFont(QFont(font().family(), 8));
  for (double x : graduations(m_x0, m_x1, m_logX)) {
    const double px = versEcranX(x);
    g.setPen(QPen(grille, 1));
    g.drawLine(QPointF(px, z.top()), QPointF(px, z.bottom()));
    g.setPen(texte);
    g.drawText(QRectF(px - 40, z.bottom() + 4, 80, 14), Qt::AlignCenter, si(x));
  }
  for (double y : graduations(m_y0, m_y1, m_logY)) {
    const double py = Y(y);
    g.setPen(QPen(grille, 1));
    g.drawLine(QPointF(z.left(), py), QPointF(z.right(), py));
    g.setPen(texte);
    g.drawText(QRectF(0, py - 7, z.left() - 6, 14), Qt::AlignRight | Qt::AlignVCenter, si(y));
  }
  g.setPen(texte);
  g.drawRect(z);
  g.drawText(QRectF(z.left(), z.bottom() + 22, z.width(), 16), Qt::AlignCenter, m_axeX);
  g.save();
  g.translate(12, z.center().y());
  g.rotate(-90);
  g.drawText(QRectF(-z.height() / 2, -8, z.height(), 16), Qt::AlignCenter, m_axeY);
  g.restore();
  g.setClipRect(z);
  for (std::size_t j = 0; j < m_courbes.size(); ++j) {
    const auto& k = m_courbes[j];
    if (m_xy && m_courbes.size() > 1 && j == 0) continue;
    const auto& xs = abscisses(m_courbes, j, m_xy);
    QPolygonF p;
    for (std::size_t i = 0; i < xs.size() && i < k.y.size(); ++i)
      if (std::isfinite(k.y[i]) && (!m_logX || xs[i] > 0) && (!m_logY || k.y[i] > 0)) p << QPointF(versEcranX(xs[i]), Y(k.y[i]));
    QColor c = k.couleur;
    if (k.attenuee) c.setAlphaF(0.3);
    g.setPen(QPen(c, k.attenuee ? 1.2 : 1.8));
    g.drawPolyline(p);
    // Tronçons hors de la bande de la référence : épais, hachurés
    for (const auto& [a, b] : k.surlignes) {
      QPolygonF h;
      for (std::size_t i = a; i <= b && i < xs.size() && i < k.y.size(); ++i) h << QPointF(versEcranX(xs[i]), Y(k.y[i]));
      g.setPen(QPen(QColor(0xC8, 0x2A, 0x2A), 4, Qt::DashLine));
      g.drawPolyline(h);
    }
  }
  // Curseurs
  for (int i = 0; i < 2; ++i) {
    const double px = versEcranX(m_curseurs[i]);
    g.setPen(QPen(QColor(0xF2, 0xC1, 0x4E), 1, Qt::DashLine));
    g.drawLine(QPointF(px, z.top()), QPointF(px, z.bottom()));
    g.drawText(QPointF(px + 3, z.top() + 12), i == 0 ? QStringLiteral("A") : QStringLiteral("B"));
  }
  g.setClipping(false);
  // Légende
  double ly = z.top() + 6;
  for (const auto& k : m_courbes) {
    if (k.attenuee) continue;
    g.setPen(QPen(k.couleur, 3));
    g.drawLine(QPointF(z.right() - 120, ly + 6), QPointF(z.right() - 104, ly + 6));
    g.setPen(texte);
    g.drawText(QRectF(z.right() - 100, ly, 96, 14), Qt::AlignLeft | Qt::AlignVCenter, k.nom);
    ly += 15;
  }
}

QPointF TraceCourbes::versEcran(double x, double y) const {
  const QRectF z = zone();
  const double t = m_logY ? (std::log10(std::max(y, 1e-300)) - std::log10(m_y0)) / (std::log10(m_y1) - std::log10(m_y0)) : (y - m_y0) / (m_y1 - m_y0);
  return {versEcranX(x), z.bottom() - t * z.height()};
}

int TraceCourbes::courbeA(const QPointF& p, double tolerance) const {
  int meilleure = -1;
  double d = tolerance;
  for (std::size_t j = 0; j < m_courbes.size(); ++j) {
    const auto& k = m_courbes[j];
    if (k.attenuee || (m_xy && m_courbes.size() > 1 && j == 0)) continue;
    const auto& xs = abscisses(m_courbes, j, m_xy);
    for (std::size_t i = 1; i < xs.size() && i < k.y.size(); ++i) {
      if (!std::isfinite(k.y[i]) || !std::isfinite(k.y[i - 1]) || (m_logX && (xs[i] <= 0 || xs[i - 1] <= 0))) continue;
      const QPointF a = versEcran(xs[i - 1], k.y[i - 1]), b = versEcran(xs[i], k.y[i]);
      const QPointF ab = b - a, ap = p - a;
      const double l2 = QPointF::dotProduct(ab, ab);
      const double t = l2 > 0 ? std::clamp(QPointF::dotProduct(ap, ab) / l2, 0.0, 1.0) : 0.0;
      const QPointF h = a + t * ab;
      const double e = std::hypot(p.x() - h.x(), p.y() - h.y());
      if (e < d) d = e, meilleure = static_cast<int>(j);
    }
  }
  return meilleure;
}

void TraceCourbes::mousePressEvent(QMouseEvent* e) {
  if (const int j = courbeA(e->position()); j >= 0) emit courbeCliquee(m_courbes[static_cast<std::size_t>(j)].nom);
  const double px = e->position().x();
  m_tenu = std::abs(px - versEcranX(m_curseurs[0])) <= std::abs(px - versEcranX(m_curseurs[1])) ? 0 : 1;
  setCurseur(m_tenu, depuisEcranX(px));
}
void TraceCourbes::mouseMoveEvent(QMouseEvent* e) {
  if (m_tenu >= 0) setCurseur(m_tenu, depuisEcranX(e->position().x()));
}
void TraceCourbes::mouseReleaseEvent(QMouseEvent*) { m_tenu = -1; }

// ---------- Fenêtre ----------
FenetreSimulation::FenetreSimulation(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreSimulation"));
  setWindowTitle(QStringLiteral("Simulation"));
  resize(1100, 700);
  auto* racine = new QHBoxLayout(this);
  auto* gauche = new QVBoxLayout;
  auto* f = new QFormLayout;
  m_type = new QComboBox;
  m_type->setObjectName(QStringLiteral("typeAnalyse"));
  m_type->addItems({QStringLiteral("Point de repos"), QStringLiteral("Balayage continu"), QStringLiteral("Réponse en fréquence"), QStringLiteral("Transitoire")});
  m_type->setCurrentIndex(3);
  f->addRow(QStringLiteral("Analyse"), m_type);
  auto champ = [&](const QString& nom, const QString& v, const QString& etiquette) {
    auto* e = new QLineEdit(v);
    e->setObjectName(nom);
    f->addRow(etiquette, e);
    return e;
  };
  m_pas = champ(QStringLiteral("pas"), QStringLiteral("10u"), QStringLiteral("Pas (s)"));
  m_duree = champ(QStringLiteral("duree"), QStringLiteral("10m"), QStringLiteral("Durée (s)"));
  m_fDebut = champ(QStringLiteral("fDebut"), QStringLiteral("1"), QStringLiteral("De (Hz)"));
  m_fFin = champ(QStringLiteral("fFin"), QStringLiteral("1meg"), QStringLiteral("À (Hz)"));
  m_points = champ(QStringLiteral("pointsDecade"), QStringLiteral("20"), QStringLiteral("Points par décade"));
  m_source = champ(QStringLiteral("source"), QStringLiteral("V1"), QStringLiteral("Source balayée"));
  m_dcDebut = champ(QStringLiteral("dcDebut"), QStringLiteral("0"), QStringLiteral("De"));
  m_dcFin = champ(QStringLiteral("dcFin"), QStringLiteral("5"), QStringLiteral("À"));
  m_dcPas = champ(QStringLiteral("dcPas"), QStringLiteral("0.1"), QStringLiteral("Pas"));
  // Compatibilité des modèles d'autres simulateurs (11.8) : réglage ngbehavior de ngspice
  m_compat = new QComboBox;
  m_compat->setObjectName(QStringLiteral("compatibilite"));
  m_compat->addItem(QStringLiteral("ngspice"), QString());
  m_compat->addItem(QStringLiteral("PSpice"), QStringLiteral("ps"));
  m_compat->addItem(QStringLiteral("LTspice"), QStringLiteral("lt"));
  m_compat->addItem(QStringLiteral("HSPICE"), QStringLiteral("hs"));
  m_compat->addItem(QStringLiteral("LTspice et PSpice"), QStringLiteral("ltps"));
  m_compat->setToolTip(QStringLiteral("Les modèles des articles disent leur compatibilité ; ce réglage l’impose à tout le calcul"));
  f->addRow(QStringLiteral("Compatibilité"), m_compat);
  // Partie simulée seule (11.8) : liaisons coupées réglées dans l'onglet Partie
  m_partie = new QComboBox;
  m_partie->setObjectName(QStringLiteral("partieSimulee"));
  m_partie->addItems({QStringLiteral("Tout le schéma"), QStringLiteral("La feuille affichée"), QStringLiteral("La sélection")});
  f->addRow(QStringLiteral("Simuler"), m_partie);
  gauche->addLayout(f);
  gauche->addWidget(new QLabel(QStringLiteral("<b>Sondes</b> (tensions des réseaux)")));
  m_sondes = new QListWidget;
  m_sondes->setObjectName(QStringLiteral("sondes"));
  gauche->addWidget(m_sondes, 1);
  // Courbes calculées (11.13) : expressions évaluées par Prométhée à chaque calcul
  gauche->addWidget(new QLabel(QStringLiteral("<b>Courbes calculées</b> (db(V(SORTIE)/V(ENTREE)), V(A,B), V(S)*I(R3)…)")));
  m_expressions = new QListWidget;
  m_expressions->setObjectName(QStringLiteral("expressions"));
  m_expressions->setMaximumHeight(80);
  gauche->addWidget(m_expressions);
  auto* ajoutExpr = new QLineEdit;
  ajoutExpr->setObjectName(QStringLiteral("nouvelleExpression"));
  ajoutExpr->setPlaceholderText(QStringLiteral("Expression, puis Entrée"));
  gauche->addWidget(ajoutExpr);
  connect(ajoutExpr, &QLineEdit::returnPressed, this, [this, ajoutExpr] {
    if (!ajoutExpr->text().trimmed().isEmpty()) ajouterExpression(ajoutExpr->text().trimmed());
    ajoutExpr->clear();
  });
  auto* reglage = new QFormLayout;
  m_regle = new QComboBox;
  m_regle->setObjectName(QStringLiteral("composantRegle"));
  m_facteur = new QDoubleSpinBox;
  m_facteur->setObjectName(QStringLiteral("facteurRegle"));
  m_facteur->setRange(0.1, 10);
  m_facteur->setSingleStep(0.1);
  m_facteur->setValue(1);
  m_facteur->setPrefix(QStringLiteral("× "));
  reglage->addRow(QStringLiteral("Régler"), m_regle);
  reglage->addRow(QStringLiteral("Valeur"), m_facteur);
  gauche->addLayout(reglage);
  auto* appliquer = new QPushButton(QStringLiteral("Écrire la valeur réglée dans le schéma"));
  appliquer->setObjectName(QStringLiteral("appliquerReglage"));
  gauche->addWidget(appliquer);
  auto* boutons = new QHBoxLayout;
  m_lancer = new QPushButton(QStringLiteral("Lancer"));
  m_lancer->setObjectName(QStringLiteral("lancerSimulation"));
  m_arreter = new QPushButton(QStringLiteral("Arrêter"));
  m_arreter->setObjectName(QStringLiteral("arreterSimulation"));
  m_arreter->setEnabled(false);
  boutons->addWidget(m_lancer);
  boutons->addWidget(m_arreter);
  gauche->addLayout(boutons);
  m_etat = new QLabel;
  m_etat->setObjectName(QStringLiteral("etatSimulation"));
  m_etat->setWordWrap(true);
  gauche->addWidget(m_etat);
  // Après un échec (11.9) : les remèdes, essayés dans l'ordre jusqu'au premier qui fait aboutir le calcul
  m_boutonRemedes = new QPushButton(QStringLiteral("Essayer les remèdes dans l’ordre"));
  m_boutonRemedes->setObjectName(QStringLiteral("essayerRemedes"));
  m_boutonRemedes->setVisible(false);
  gauche->addWidget(m_boutonRemedes);
  auto* g = new QWidget;
  g->setLayout(gauche);
  g->setMaximumWidth(330);
  racine->addWidget(g);
  m_onglets = new QTabWidget;
  m_trace = new TraceCourbes;
  m_onglets->addTab(m_trace, QStringLiteral("Courbes"));
  m_tableMesures = new QTableWidget(0, 2);
  m_tableMesures->setObjectName(QStringLiteral("tableMesures"));
  m_tableMesures->setHorizontalHeaderLabels({QStringLiteral("Mesure"), QStringLiteral("Valeur")});
  m_tableMesures->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_onglets->addTab(m_tableMesures, QStringLiteral("Mesures"));
  // Préparation (11.9) : chaque constat mène à son composant d'un double-clic
  auto* prep = new QWidget;
  auto* pl = new QVBoxLayout(prep);
  m_tablePreparation = new QTableWidget(0, 3);
  m_tablePreparation->setObjectName(QStringLiteral("tablePreparation"));
  m_tablePreparation->setHorizontalHeaderLabels({QStringLiteral("Niveau"), QStringLiteral("Constat"), QStringLiteral("Remèdes")});
  m_tablePreparation->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_tablePreparation->setEditTriggers(QAbstractItemView::NoEditTriggers);
  pl->addWidget(m_tablePreparation, 1);
  auto* deduire = new QPushButton(QStringLiteral("Déduire du repère les modèles qui manquent (un seul Ctrl+Z)"));
  deduire->setObjectName(QStringLiteral("deduireModeles"));
  pl->addWidget(deduire);
  m_onglets->addTab(prep, QStringLiteral("Préparation"));
  // Partie (11.8) : chaque réseau qui relie la partie au reste, bouclé sur une source, une charge ou rien
  m_tableLiaisons = new QTableWidget(0, 3);
  m_tableLiaisons->setObjectName(QStringLiteral("tableLiaisons"));
  m_tableLiaisons->setHorizontalHeaderLabels({QStringLiteral("Liaison coupée"), QStringLiteral("Bouclée sur"), QStringLiteral("Valeur (V ou Ω)")});
  m_tableLiaisons->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_onglets->addTab(m_tableLiaisons, QStringLiteral("Partie"));
  // Classeur (11.14, 11.15) : analyses enregistrées, résultats figés, référence et verdict, courbe sur la feuille
  auto* cla = new QWidget;
  auto* cl = new QVBoxLayout(cla);
  m_listeClasseur = new QListWidget;
  m_listeClasseur->setObjectName(QStringLiteral("listeClasseur"));
  cl->addWidget(m_listeClasseur, 1);
  auto* bc = new QGridLayout;
  auto* enr = new QPushButton(QStringLiteral("Enregistrer l’analyse…"));
  auto* ouv = new QPushButton(QStringLiteral("Ouvrir"));
  auto* fig = new QPushButton(QStringLiteral("Figer le résultat…"));
  auto* ref = new QPushButton(QStringLiteral("Prendre un figé pour référence…"));
  auto* cmp = new QPushButton(QStringLiteral("Comparer à la référence"));
  auto* pos = new QPushButton(QStringLiteral("Poser la courbe sur la feuille"));
  auto* csvO = new QPushButton(QStringLiteral("Exporter (CSV)…"));
  auto* csvI = new QPushButton(QStringLiteral("Importer un CSV en figé…"));
  auto* img = new QPushButton(QStringLiteral("Exporter le tracé (PNG, SVG)…"));
  auto* logy = new QCheckBox(QStringLiteral("Axe vertical logarithmique"));
  auto* xy = new QCheckBox(QStringLiteral("Tracé XY (en fonction de la première courbe)"));
  int ib = 0;
  for (QWidget* w : std::initializer_list<QWidget*>{enr, ouv, fig, ref, cmp, pos, csvO, csvI, img, logy, xy}) bc->addWidget(w, ib / 2, ib % 2), ++ib;
  cl->addLayout(bc);
  m_verdict = new QLabel;
  m_verdict->setObjectName(QStringLiteral("verdictReference"));
  m_verdict->setWordWrap(true);
  cl->addWidget(m_verdict);
  m_onglets->addTab(cla, QStringLiteral("Classeur"));
  connect(deduire, &QPushButton::clicked, this, [this] {
    const ConceptionElec* c = m_atelier.conceptionCourante();
    if (!c) return;
    std::vector<std::string> u;
    for (const auto& x : c->composants) u.push_back(x.uuid);
    std::vector<std::string> aSaisir;
    int n = 0;
    m_atelier.modifierConception([&](ConceptionElec& y) { n = deduireModeles(y, u, &aSaisir); }, QStringLiteral("Modèles de simulation déduits du repère"));
    m_etat->setText(QStringLiteral("%1 modèle(s) déduit(s)%2").arg(n).arg(aSaisir.empty() ? QString() : QStringLiteral(" ; valeur à saisir : ") + q([&] {
      std::string t;
      for (const auto& r : aSaisir) t += (t.empty() ? "" : ", ") + r;
      return t;
    }())));
    preparer();
  });
  connect(m_tablePreparation, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
    if (auto* it = m_tablePreparation->item(r, 1); it && !it->data(Qt::UserRole).toString().isEmpty()) m_atelier.montrerComposant(it->data(Qt::UserRole).toString().toStdString());
  });
  connect(enr, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const QString n = QInputDialog::getText(this, QStringLiteral("Classeur"), QStringLiteral("Nom de l’analyse"), QLineEdit::Normal, m_onglet.isEmpty() ? QStringLiteral("Analyse") : m_onglet, &ok);
    if (ok && !n.trimmed().isEmpty()) enregistrerOnglet(n.trimmed());
  });
  connect(ouv, &QPushButton::clicked, this, [this] {
    if (auto* it = m_listeClasseur->currentItem(); it && it->data(Qt::UserRole).toString() == QStringLiteral("onglet")) ouvrirOnglet(it->text());
  });
  connect(m_listeClasseur, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
    if (it->data(Qt::UserRole).toString() == QStringLiteral("onglet")) ouvrirOnglet(it->text());
  });
  connect(fig, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const QString n = QInputDialog::getText(this, QStringLiteral("Figer le résultat"), QStringLiteral("Nom du résultat figé"), QLineEdit::Normal,
                                            QStringLiteral("Figé %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))), &ok);
    if (ok && !n.trimmed().isEmpty()) figerResultat(n.trimmed());
  });
  connect(ref, &QPushButton::clicked, this, [this] {
    QStringList figes;
    for (const auto& x : classeur().figes) figes << q(x.nom);
    if (figes.isEmpty()) return;
    bool ok = false;
    const QString f = QInputDialog::getItem(this, QStringLiteral("Référence"), QStringLiteral("Résultat figé"), figes, 0, false, &ok);
    if (!ok) return;
    QString courbe;
    for (int i = 0; i < m_sondes->count() && courbe.isEmpty(); ++i)
      if (m_sondes->item(i)->checkState() == Qt::Checked) courbe = m_sondes->item(i)->text();
    const QString c = QInputDialog::getText(this, QStringLiteral("Référence"), QStringLiteral("Courbe comparée (réseau ou expression)"), QLineEdit::Normal, courbe, &ok);
    if (!ok || c.isEmpty()) return;
    const double b = QInputDialog::getDouble(this, QStringLiteral("Référence"), QStringLiteral("Bande de tolérance (%)"), 1, 0.001, 100, 3, &ok);
    if (ok) prendreReference(f, c, b / 100);
  });
  connect(cmp, &QPushButton::clicked, this, [this] { comparer(); });
  connect(pos, &QPushButton::clicked, this, [this] { poserCourbeSurFeuille(); });
  connect(csvO, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Résultat en CSV"), QStringLiteral("simulation.csv"), QStringLiteral("CSV (*.csv)"));
    if (f.isEmpty()) return;
    try {
      prom::ecrireFichierAtomique(prom::cheminFichier(f.toStdString()), csvResultat(m_resultat));
    } catch (const std::exception& e) {
      m_etat->setText(QString::fromUtf8(e.what()).toHtmlEscaped());
    }
  });
  connect(csvI, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("CSV à figer"), {}, QStringLiteral("CSV (*.csv *.txt)"));
    if (f.isEmpty()) return;
    std::string erreur;
    std::optional<ResultatFige> r;
    try {
      r = resultatDepuisCsv(prom::lireFichier(prom::cheminFichier(f.toStdString())), QFileInfo(f).completeBaseName().toStdString(), &erreur);
    } catch (const std::exception& e) {
      erreur = e.what();
    }
    if (!r) {
      m_etat->setText(QStringLiteral("CSV illisible : %1").arg(q(erreur).toHtmlEscaped()));
      return;
    }
    m_atelier.modifierConception([&](ConceptionElec& c) {
      ClasseurSimulation k = classeurDe(c);
      k.figes.push_back(*r);
      ecrireClasseur(c, k);
    }, QStringLiteral("CSV figé dans le classeur"));
    refaire();
  });
  connect(img, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Tracé"), QStringLiteral("courbes.png"), QStringLiteral("Image PNG (*.png);;Dessin SVG (*.svg);;CSV (*.csv)"));
    if (f.isEmpty()) return;
    try {
      if (f.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) prom::ecrireFichierAtomique(prom::cheminFichier(f.toStdString()), m_trace->svg());
      else if (f.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) prom::ecrireFichierAtomique(prom::cheminFichier(f.toStdString()), m_trace->csv());
      else m_trace->exporterPng(f);
    } catch (const std::exception& e) {
      m_etat->setText(QString::fromUtf8(e.what()).toHtmlEscaped());
    }
  });
  connect(logy, &QCheckBox::toggled, this, [this](bool v) { m_trace->setLogY(v); });
  connect(xy, &QCheckBox::toggled, this, [this](bool v) { m_trace->setXY(v); });
  auto* netlist = new QWidget;
  auto* nl = new QVBoxLayout(netlist);
  m_texteNetlist = new QPlainTextEdit;
  m_texteNetlist->setObjectName(QStringLiteral("texteNetlist"));
  m_texteNetlist->setFont(QFont(QStringLiteral("monospace")));
  nl->addWidget(m_texteNetlist);
  auto* essai = new QPushButton(QStringLiteral("Simuler cette netlist (le schéma ne change pas)"));
  nl->addWidget(essai);
  m_onglets->addTab(netlist, QStringLiteral("Netlist"));
  auto* cons = new QWidget;
  auto* col = new QVBoxLayout(cons);
  m_console = new QPlainTextEdit;
  m_console->setReadOnly(true);
  m_console->setFont(QFont(QStringLiteral("monospace")));
  col->addWidget(m_console, 1);
  // Commandes de lecture et de calcul passées à ngspice (11.8) ; « shell » et l'écriture de fichiers sont refusées
  m_ligneConsole = new QLineEdit;
  m_ligneConsole->setObjectName(QStringLiteral("ligneConsole"));
  m_ligneConsole->setPlaceholderText(QStringLiteral("print v(n_sortie), meas, let, display… puis Entrée"));
  col->addWidget(m_ligneConsole);
  connect(m_ligneConsole, &QLineEdit::returnPressed, this, [this] {
    console(m_ligneConsole->text());
    m_ligneConsole->clear();
  });
  m_onglets->addTab(cons, QStringLiteral("Console de ngspice"));
  racine->addWidget(m_onglets, 1);
  connect(m_type, &QComboBox::currentIndexChanged, this, [this] { majChamps(); });
  connect(m_lancer, &QPushButton::clicked, this, [this] { lancer(); });
  connect(m_arreter, &QPushButton::clicked, this, [this] { arreter(); });
  connect(essai, &QPushButton::clicked, this, [this] { lancerNetlist(m_texteNetlist->toPlainText()); });
  connect(m_trace, &TraceCourbes::curseursChanges, this, [this] { afficher(false); });
  connect(m_facteur, &QDoubleSpinBox::valueChanged, this, [this](double v) {
    if (!m_regle->currentText().isEmpty()) regler(m_regle->currentText().toStdString(), v);
  });
  connect(appliquer, &QPushButton::clicked, this, [this] { appliquerReglage(); });
  connect(m_boutonRemedes, &QPushButton::clicked, this, [this] { essayerRemedes(); });
  connect(m_compat, &QComboBox::currentIndexChanged, this, [this] { m_options.compatibilite = m_compat->currentData().toString().toStdString(); });
  connect(m_partie, &QComboBox::activated, this, [this](int i) { setPartie(i); });
  connect(m_trace, &TraceCourbes::courbeCliquee, this, [this](const QString& nom) {
    // Le fil de la courbe : son réseau, ou le premier V(…) de son expression
    QString r = nom;
    r.remove(QStringLiteral(" (comparée)"));
    static const QRegularExpression v(QStringLiteral("[Vv]\\(\\s*([^,)\\s]+)"));
    if (const auto m = v.match(r); m.hasMatch()) r = m.captured(1);
    if (!r.isEmpty()) m_atelier.surligner(r);
  });
  majChamps();
  refaire();
  if (!ngspiceDisponible()) {
    m_lancer->setEnabled(false);
    m_etat->setText(QStringLiteral("<span style='color:#c53030'>ngspice n’est pas installé : la simulation est indisponible. Installe ngspice (bibliothèque libngspice) puis rouvre cette fenêtre.</span>"));
  } else
    m_etat->setText(QStringLiteral("Moteur : %1").arg(q(versionNgspice())));
}

FenetreSimulation::~FenetreSimulation() {
  if (m_arret) *m_arret = true;
  if (m_fil.joinable()) m_fil.join();
}

void FenetreSimulation::majChamps() {
  const int t = m_type->currentIndex();
  for (QLineEdit* e : {m_pas, m_duree}) e->setEnabled(t == 3);
  for (QLineEdit* e : {m_fDebut, m_fFin, m_points}) e->setEnabled(t == 2);
  for (QLineEdit* e : {m_source, m_dcDebut, m_dcFin, m_dcPas}) e->setEnabled(t == 1);
}

void FenetreSimulation::refaire() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  // Autre conception (projet rouvert ou nouveau) : les réglages de la précédente ne la suivent pas
  if (c->uuid != m_conceptionVue) {
    if (!m_conceptionVue.empty()) {
      m_options = {};
      m_diagnostic = {};
      m_retenu.clear();
      m_onglet.clear();
      m_reglee.clear();
      m_facteurRegle = 1;
      m_precedentes.clear();
      if (m_expressions) m_expressions->clear();
      if (m_partie) m_partie->setCurrentIndex(0);
      if (m_compat) m_compat->setCurrentIndex(0);
      if (m_boutonRemedes) m_boutonRemedes->setVisible(false);
      majLiaisons();
    }
    m_conceptionVue = c->uuid;
  }
  std::set<std::string> coches;
  for (int i = 0; i < m_sondes->count(); ++i)
    if (m_sondes->item(i)->checkState() == Qt::Checked) coches.insert(m_sondes->item(i)->text().toStdString());
  const bool premier = m_sondes->count() == 0;
  m_sondes->clear();
  const Connectivite k = connectivite(*c);
  std::vector<std::string> noms;
  for (const auto& r : k.reseaux)
    if (!r.broches.empty() && !r.nom.empty()) noms.push_back(r.nom);
  std::sort(noms.begin(), noms.end());
  noms.erase(std::unique(noms.begin(), noms.end()), noms.end());
  for (const auto& n : noms) {
    auto* it = new QListWidgetItem(q(n));
    const bool nomme = n.rfind("N-", 0) != 0 && n != "GND";
    it->setCheckState((premier ? nomme : coches.count(n) > 0) ? Qt::Checked : Qt::Unchecked);
    m_sondes->addItem(it);
  }
  const QString avant = m_regle->currentText();
  m_regle->clear();
  m_regle->addItem(QString());
  for (const auto& x : c->composants)
    if (x.symbole == "R" || x.symbole == "C" || x.symbole == "L" || x.symbole == "Potentiometre") m_regle->addItem(q(x.reference));
  m_regle->setCurrentText(avant);
  // Classeur : analyses enregistrées, puis résultats figés
  if (m_listeClasseur) {
    m_listeClasseur->clear();
    const ClasseurSimulation k = classeurDe(*c);
    for (const auto& o : k.onglets) {
      auto* it = new QListWidgetItem(q(o.nom));
      it->setData(Qt::UserRole, QStringLiteral("onglet"));
      it->setToolTip(o.verdict.empty() ? QStringLiteral("Analyse enregistrée") : QStringLiteral("Dernier verdict : ") + q(o.verdict));
      m_listeClasseur->addItem(it);
    }
    for (const auto& f : k.figes) {
      auto* it = new QListWidgetItem(QStringLiteral("❄ ") + q(f.nom));
      it->setData(Qt::UserRole, QStringLiteral("fige"));
      it->setToolTip(QStringLiteral("Résultat figé de « %1 » %2").arg(q(f.onglet), q(f.date)));
      m_listeClasseur->addItem(it);
    }
  }
}

void FenetreSimulation::setAnalyse(const AnalyseSpice& a) {
  m_analyse = a;  // ce que les champs ne montrent pas reste tel quel
  m_type->setCurrentIndex(static_cast<int>(a.type));
  const QLocale c = QLocale::c();
  m_points->setText(QString::number(a.pointsParDecade));
  m_pas->setText(c.toString(a.pas, 'g', 6));
  m_duree->setText(c.toString(a.duree, 'g', 6));
  m_fDebut->setText(c.toString(a.fDebut, 'g', 6));
  m_fFin->setText(c.toString(a.fFin, 'g', 6));
  m_source->setText(q(a.source));
  m_dcDebut->setText(c.toString(a.dcDebut, 'g', 6));
  m_dcFin->setText(c.toString(a.dcFin, 'g', 6));
  m_dcPas->setText(c.toString(a.dcPas, 'g', 6));
}

AnalyseSpice FenetreSimulation::analyse() const {
  // Les champs comprennent les suffixes SPICE (10u, 1meg, 2.2k) et la virgule
  auto lire = [](const QLineEdit* e, double defaut) {
    QString t = e->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.'));
    double f = 1;
    const QString m = t.toLower();
    if (m.endsWith(QLatin1String("meg"))) f = 1e6, t.chop(3);
    else if (m.endsWith(QLatin1Char('k'))) f = 1e3, t.chop(1);
    else if (m.endsWith(QLatin1Char('m'))) f = 1e-3, t.chop(1);
    else if (m.endsWith(QLatin1Char('u')) || m.endsWith(QStringLiteral("µ"))) f = 1e-6, t.chop(1);
    else if (m.endsWith(QLatin1Char('n'))) f = 1e-9, t.chop(1);
    else if (m.endsWith(QLatin1Char('p'))) f = 1e-12, t.chop(1);
    else if (m.endsWith(QLatin1Char('g'))) f = 1e9, t.chop(1);
    bool ok = false;
    const double v = t.toDouble(&ok);
    return ok ? v * f : defaut;
  };
  AnalyseSpice a = m_analyse;
  a.type = static_cast<AnalyseSpice::Type>(m_type->currentIndex());
  a.pointsParDecade = std::max(1, static_cast<int>(lire(m_points, 20)));
  a.pas = lire(m_pas, 1e-5);
  a.duree = lire(m_duree, 1e-2);
  a.fDebut = lire(m_fDebut, 1);
  a.fFin = lire(m_fFin, 1e6);
  a.source = m_source->text().toStdString();
  a.dcDebut = lire(m_dcDebut, 0);
  a.dcFin = lire(m_dcFin, 5);
  a.dcPas = lire(m_dcPas, 0.1);
  return a;
}

void FenetreSimulation::sonder(const std::vector<std::string>& reseaux) {
  for (int i = 0; i < m_sondes->count(); ++i)
    m_sondes->item(i)->setCheckState(std::find(reseaux.begin(), reseaux.end(), m_sondes->item(i)->text().toStdString()) != reseaux.end() ? Qt::Checked : Qt::Unchecked);
}

ConceptionElec FenetreSimulation::conceptionReglee() const {
  ConceptionElec c = *m_atelier.conceptionCourante();
  if (!m_reglee.empty() && m_facteurRegle != 1)
    for (auto& x : c.composants)
      if (x.reference == m_reglee) {
        const FamilleValeur fa = familleDe(c, x);
        const ValeurComprise v = lireValeurComposant(x.valeur, fa);
        if (v.comprise()) x.valeur = ecrireCompact(v.nombre * m_facteurRegle, fa);
      }
  return c;
}

void FenetreSimulation::lancer() {
  if (!m_atelier.conceptionCourante() || m_enCours) return;
  OptionsSimulation o = m_options;
  o.parametres = classeurDe(*m_atelier.conceptionCourante()).parametres;
  m_netlist = netlistSpice(conceptionReglee(), analyse(), o);
  m_texteNetlist->setPlainText(q(m_netlist.texte));
  majPreparation(preparerSimulation(conceptionReglee(), analyse(), o));
  m_boutonRemedes->setVisible(false);
  if (!m_netlist.valide()) {
    QString t = QStringLiteral("<span style='color:#c53030'><b>Avant le calcul :</b></span><ul>");
    for (const auto& e : m_netlist.erreurs) t += QStringLiteral("<li>%1</li>").arg(q(e).toHtmlEscaped());
    m_etat->setText(t + QStringLiteral("</ul>"));
    m_resultat = {};
    m_resultat.erreur = m_netlist.erreurs.front();
    emit calculTermine();
    return;
  }
  lancerNetlist(q(m_netlist.texte));
}

void FenetreSimulation::lancerNetlist(const QString& texte) {
  if (m_enCours) return;
  if (m_fil.joinable()) m_fil.join();
  m_enCours = true;
  m_lancer->setEnabled(false);
  m_arreter->setEnabled(true);
  QString avertissements;
  for (const auto& w : m_netlist.avertissements) avertissements += QStringLiteral("<br>· %1").arg(q(w).toHtmlEscaped());
  m_etat->setText(QStringLiteral("Calcul en cours…") + avertissements);
  m_arret = std::make_shared<std::atomic<bool>>(false);
  QPointer<FenetreSimulation> moi(this);
  m_fil = std::thread([moi, arret = m_arret, t = texte.toStdString(), mo = OptionsMoteur{m_netlist.compatibilite, m_netlist.chargements}] {
    prioriteDeFond();
    auto r = std::make_shared<ResultatSimulation>(simuler(t, mo, arret.get()));
    QMetaObject::invokeMethod(qApp, [moi, r] {
      if (!moi) return;
      moi->m_enCours = false;
      moi->m_lancer->setEnabled(true);
      moi->m_arreter->setEnabled(false);
      moi->m_resultat = std::move(*r);
      QString console;
      for (const auto& l : moi->m_resultat.console) console += q(l) + QLatin1Char('\n');
      moi->m_console->setPlainText(console);
      if (moi->m_resultat.reussi()) moi->m_etat->setText(QStringLiteral("Calcul terminé."));
      else {
        // Échec analysé (11.9) : la cause en français, les remèdes proposés
        moi->m_diagnostic = analyserEchec(moi->m_resultat, moi->m_netlist);
        QString t = QStringLiteral("<span style='color:#c53030'>%1</span>").arg(q(moi->m_diagnostic.cause).toHtmlEscaped());
        if (!moi->m_diagnostic.remedes.empty()) {
          t += QStringLiteral("<br>Remèdes proposés :");
          for (const auto& x : moi->m_diagnostic.remedes) t += QStringLiteral("<br>· %1").arg(q(libelleRemede(x)).toHtmlEscaped());
        }
        moi->m_etat->setText(t);
        moi->m_boutonRemedes->setVisible(!moi->m_diagnostic.remedes.empty() && !moi->m_resultat.interrompu);
      }
      moi->afficher();
      moi->majImagesCourbes();
      emit moi->calculTermine();
    }, Qt::QueuedConnection);
  });
}

void FenetreSimulation::arreter() {
  if (m_arret) *m_arret = true;
}

void FenetreSimulation::afficher(bool refaireTrace) {
  const ResultatSimulation& r = m_resultat;
  m_tableMesures->setRowCount(0);
  auto ligne = [this](const QString& a, const QString& b) {
    const int i = m_tableMesures->rowCount();
    m_tableMesures->insertRow(i);
    m_tableMesures->setItem(i, 0, new QTableWidgetItem(a));
    m_tableMesures->setItem(i, 1, new QTableWidgetItem(b));
  };
  if (!r.reussi()) return;
  std::vector<std::string> sondes;
  for (int i = 0; i < m_sondes->count(); ++i)
    if (m_sondes->item(i)->checkState() == Qt::Checked) sondes.push_back(m_sondes->item(i)->text().toStdString());
  // Point de repos : la tension de chaque nœud
  if (r.echelle.empty()) {
    for (const auto& [reseau, nd] : m_netlist.noeuds)
      if (const auto* v = r.reel(vecteurTension(m_netlist, reseau)); v && !v->empty()) ligne(q(reseau), si(v->front()) + QStringLiteral("V"));
    m_onglets->setCurrentWidget(m_tableMesures);
    return;
  }
  const bool ac = r.echelle == "frequency";
  const std::vector<double>* x = r.reel(r.echelle);
  if (!x) return;
  std::vector<TraceCourbes::Courbe> courbes = m_precedentes;
  int teinte = 0;
  for (const auto& s : sondes) {
    const std::string nom = vecteurTension(m_netlist, s);
    if (nom.empty()) continue;
    TraceCourbes::Courbe k;
    k.nom = q(s);
    k.couleur = TEINTES[teinte++ % 6];
    k.x = *x;
    if (ac) {
      if (const auto* h = r.complexe(nom)) {
        k.y = gainDb(*h);
        if (const double fc = frequenceCoupure(*x, *h); fc > 0) ligne(QStringLiteral("%1 : coupure à −3 dB").arg(q(s)), si(fc, 4) + QStringLiteral("Hz"));
        ligne(QStringLiteral("%1 : gain en basse fréquence").arg(q(s)), QLocale().toString(k.y.front(), 'f', 2) + QStringLiteral(" dB"));
      }
    } else if (const auto* v = r.reel(nom)) {
      k.y = *v;
      if (r.echelle == "time")
        if (const double f = frequenceMesuree(*x, *v); f > 0) ligne(QStringLiteral("%1 : fréquence").arg(q(s)), si(f, 4) + QStringLiteral("Hz"));
      const auto [mn, mx] = std::minmax_element(v->begin(), v->end());
      ligne(QStringLiteral("%1 : min / max").arg(q(s)), si(*mn) + QStringLiteral("V / ") + si(*mx) + QStringLiteral("V"));
    }
    if (k.y.empty()) continue;
    ligne(QStringLiteral("%1 : en A / en B").arg(q(s)), si(valeurA(k.x, k.y, m_trace->curseur(0))) + QStringLiteral(" / ") + si(valeurA(k.x, k.y, m_trace->curseur(1))));
    courbes.push_back(std::move(k));
  }
  // Courbes calculées (11.13)
  for (int i = 0; i < m_expressions->count(); ++i) {
    if (m_expressions->item(i)->checkState() != Qt::Checked) continue;
    const CourbeCalculee c = evaluerCourbe(m_expressions->item(i)->text().toStdString(), r, m_netlist);
    if (!c.valide()) {
      ligne(m_expressions->item(i)->text(), q(c.erreur));
      continue;
    }
    TraceCourbes::Courbe k;
    k.nom = m_expressions->item(i)->text();
    k.couleur = TEINTES[teinte++ % 6];
    k.x = c.x;
    k.y = c.complexe && ac ? gainDb(c.y) : c.reels();
    courbes.push_back(std::move(k));
  }
  // Mesures automatiques des sondes (11.15), refaites à chaque calcul
  for (const auto& sd : sondes) {
    const std::vector<GenreMesure> genres = ac ? std::vector<GenreMesure>{GenreMesure::Bande, GenreMesure::Q}
                                               : std::vector<GenreMesure>{GenreMesure::Periode, GenreMesure::Montee, GenreMesure::Descente, GenreMesure::Depassement, GenreMesure::Moyenne, GenreMesure::Efficace};
    for (const GenreMesure g : genres)
      if (const ResultatMesure x = mesurer({g, sd, 0}, r, m_netlist); x.valide) ligne(q(sd) + QStringLiteral(" : ") + q(nomMesure(g)), si(x.valeur, 4) + q(x.unite));
  }
  ligne(QStringLiteral("Curseurs A, B, B − A"), si(m_trace->curseur(0)) + QStringLiteral(", ") + si(m_trace->curseur(1)) + QStringLiteral(", ") + si(m_trace->curseur(1) - m_trace->curseur(0)));
  // Le tracé n'est refait qu'à un nouveau résultat (un curseur glissé ne remet pas les curseurs en place)
  if (refaireTrace)
    m_trace->setCourbes(std::move(courbes), ac ? QStringLiteral("Fréquence (Hz)") : r.echelle == "time" ? QStringLiteral("Temps (s)") : QStringLiteral("Balayage"),
                        ac ? QStringLiteral("Gain (dB)") : QStringLiteral("Tension (V)"), ac);
}

QString FenetreSimulation::mesures() const {
  QString t;
  for (int i = 0; i < m_tableMesures->rowCount(); ++i) t += m_tableMesures->item(i, 0)->text() + QStringLiteral(" = ") + m_tableMesures->item(i, 1)->text() + QLatin1Char('\n');
  return t;
}

void FenetreSimulation::regler(const std::string& reference, double facteur) {
  // La courbe d'avant reste, atténuée, sous la nouvelle
  m_precedentes.clear();
  for (auto k : m_trace->courbes())
    if (!k.attenuee) {
      k.attenuee = true;
      m_precedentes.push_back(std::move(k));
    }
  m_reglee = reference;
  m_facteurRegle = facteur;
  lancer();
}

bool FenetreSimulation::appliquerReglage() {
  if (m_reglee.empty() || m_facteurRegle == 1) return false;
  const ConceptionElec reglee = conceptionReglee();
  std::string valeur;
  for (const auto& x : reglee.composants)
    if (x.reference == m_reglee) valeur = x.valeur;
  const std::string ref = m_reglee;
  const bool fait = m_atelier.modifierConception(
      [&](ConceptionElec& c) {
        for (auto& x : c.composants)
          if (x.reference == ref) x.valeur = valeur;
      },
      QStringLiteral("Valeur réglée en simulation (%1)").arg(q(ref)));
  m_reglee.clear();
  m_facteurRegle = 1;
  m_facteur->blockSignals(true);
  m_facteur->setValue(1);
  m_facteur->blockSignals(false);
  return fait;
}

// ======================================================================================================================
// 0.34 : compatibilité, courbes calculées, préparation, remèdes, classeur, console (11.8, 11.9, 11.13 à 11.15)
// ======================================================================================================================
void FenetreSimulation::setCompatibilite(const std::string& c) {
  const int i = m_compat->findData(q(c));
  m_compat->setCurrentIndex(std::max(0, i));
  m_options.compatibilite = c;
}

void FenetreSimulation::ajouterExpression(const QString& expression) {
  auto* it = new QListWidgetItem(expression);
  it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
  it->setCheckState(Qt::Checked);
  m_expressions->addItem(it);
  if (m_resultat.reussi()) afficher();
}

void FenetreSimulation::majPreparation(const std::vector<LignePreparation>& l) {
  m_tablePreparation->setRowCount(0);
  for (const auto& x : l) {
    const int r = m_tablePreparation->rowCount();
    m_tablePreparation->insertRow(r);
    auto* n = new QTableWidgetItem(q(x.niveau));
    n->setForeground(x.niveau == "erreur" ? QColor(0xC5, 0x30, 0x30) : x.niveau == "avertissement" ? QColor(0xB0, 0x6A, 0x10) : palette().color(QPalette::Text));
    m_tablePreparation->setItem(r, 0, n);
    auto* m = new QTableWidgetItem(q(x.message));
    m->setData(Qt::UserRole, q(x.composant));
    m_tablePreparation->setItem(r, 1, m);
    std::string rem;
    for (const auto& y : x.remedes) rem += (rem.empty() ? "" : " · ") + y;
    m_tablePreparation->setItem(r, 2, new QTableWidgetItem(q(rem)));
  }
}

std::vector<LignePreparation> FenetreSimulation::preparer() {
  std::vector<LignePreparation> l;
  if (const ConceptionElec* c = m_atelier.conceptionCourante()) {
    OptionsSimulation o = m_options;
    o.parametres = classeurDe(*c).parametres;
    l = preparerSimulation(*c, analyse(), o);
  }
  majPreparation(l);
  return l;
}

void FenetreSimulation::essayerRemedes() {
  if (m_enCours || !m_atelier.conceptionCourante()) return;
  if (m_fil.joinable()) m_fil.join();
  m_enCours = true;
  m_lancer->setEnabled(false);
  m_boutonRemedes->setEnabled(false);
  m_etat->setText(QStringLiteral("Remèdes essayés dans l’ordre…"));
  m_arret = std::make_shared<std::atomic<bool>>(false);
  QPointer<FenetreSimulation> moi(this);
  OptionsSimulation o = m_options;
  o.parametres = classeurDe(*m_atelier.conceptionCourante()).parametres;
  m_fil = std::thread([moi, arret = m_arret, c = conceptionReglee(), a = analyse(), o]() mutable {
    prioriteDeFond();
    auto e = std::make_shared<EssaiRemedes>(simulerAvecRemedes(c, a, o, arret.get()));
    auto opts = std::make_shared<OptionsSimulation>(o);
    QMetaObject::invokeMethod(qApp, [moi, e, opts] {
      if (!moi) return;
      moi->m_enCours = false;
      moi->m_lancer->setEnabled(true);
      moi->m_boutonRemedes->setEnabled(true);
      moi->m_resultat = e->resultat;
      moi->m_netlist = e->netlist;
      moi->m_texteNetlist->setPlainText(q(moi->m_netlist.texte));
      if (e->resultat.reussi()) {
        // Le remède retenu entre dans les réglages, avec sa note, et dans l'onglet ouvert du classeur
        moi->m_options = *opts;
        moi->m_retenu = q(e->retenu);
        moi->m_boutonRemedes->setVisible(false);
        moi->m_etat->setText(QStringLiteral("Calcul abouti avec « %1 » (noté dans les réglages de l’analyse).").arg(q(libelleRemede(e->retenu)).toHtmlEscaped()));
        if (!moi->m_onglet.isEmpty()) moi->enregistrerOnglet(moi->m_onglet);
      } else
        moi->m_etat->setText(QStringLiteral("<span style='color:#c53030'>Aucun remède n’a fait aboutir le calcul (%1 essayé(s)).</span>").arg(e->essayes.size()));
      moi->afficher();
      emit moi->calculTermine();
    }, Qt::QueuedConnection);
  });
}

ClasseurSimulation FenetreSimulation::classeur() const {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  return c ? classeurDe(*c) : ClasseurSimulation{};
}

bool FenetreSimulation::enregistrerOnglet(const QString& nom) {
  OngletClasseur o;
  o.nom = nom.toStdString();
  o.analyse = analyse();
  o.options = m_options;
  for (int i = 0; i < m_sondes->count(); ++i)
    if (m_sondes->item(i)->checkState() == Qt::Checked) o.sondes.push_back(m_sondes->item(i)->text().toStdString());
  for (int i = 0; i < m_expressions->count(); ++i) o.expressions.push_back(m_expressions->item(i)->text().toStdString());
  const ClasseurSimulation avant = classeur();
  for (const auto& x : avant.onglets)
    if (x.nom == o.nom) {
      o.mesures = x.mesures;
      o.reference = x.reference;
      o.courbeReference = x.courbeReference;
      o.bande = x.bande;
      o.genreBande = x.genreBande;
      o.verdict = x.verdict;
    }
  const bool fait = m_atelier.modifierConception([&](ConceptionElec& c) {
    ClasseurSimulation k = classeurDe(c);
    auto it = std::find_if(k.onglets.begin(), k.onglets.end(), [&](const OngletClasseur& x) { return x.nom == o.nom; });
    if (it == k.onglets.end()) k.onglets.push_back(o);
    else *it = o;
    ecrireClasseur(c, k);
  }, QStringLiteral("Analyse « %1 » enregistrée dans le classeur").arg(nom));
  if (fait) m_onglet = nom;
  refaire();
  return fait;
}

void FenetreSimulation::setPartie(int genre) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  m_options.composants.clear();
  m_options.liaisons.clear();
  if (c && genre == 1 && m_atelier.vueSchema()) m_options.composants = composantsDeFeuille(*c, static_cast<std::size_t>(std::max(0, m_atelier.vueSchema()->feuille())));
  if (c && genre == 2 && m_atelier.selection()) m_options.composants = m_atelier.selection()->composants();
  if (c && !m_options.composants.empty()) m_options.liaisons = liaisonsDeLaPartie(*c, m_options.composants);
  {
    const QSignalBlocker b(m_partie);
    m_partie->setCurrentIndex(m_options.composants.empty() ? 0 : genre);
  }
  majLiaisons();
  if (genre != 0 && m_options.composants.empty()) m_etat->setText(QStringLiteral("Rien à simuler seul : choisis d’abord des composants (ou une feuille qui en porte)."));
  else if (genre != 0)
    m_etat->setText(QStringLiteral("%1 composant(s) simulé(s) seuls, %2 liaison(s) coupée(s) : leur bouclage se règle dans l’onglet Partie.").arg(m_options.composants.size()).arg(m_options.liaisons.size()));
}

bool FenetreSimulation::setBouclage(const std::string& reseau, BouclageLiaison b, double valeur) {
  for (auto& l : m_options.liaisons)
    if (l.reseau == reseau) {
      l.bouclage = b;
      if (b != BouclageLiaison::Rien) l.valeur = valeur;
      majLiaisons();
      return true;
    }
  return false;
}

void FenetreSimulation::majLiaisons() {
  if (!m_tableLiaisons) return;
  m_tableLiaisons->setRowCount(static_cast<int>(m_options.liaisons.size()));
  for (int i = 0; i < static_cast<int>(m_options.liaisons.size()); ++i) {
    const LiaisonCoupee& l = m_options.liaisons[static_cast<std::size_t>(i)];
    auto* nom = new QTableWidgetItem(q(l.reseau));
    nom->setFlags(nom->flags() & ~Qt::ItemIsEditable);
    m_tableLiaisons->setItem(i, 0, nom);
    auto* choix = new QComboBox;
    choix->addItems({QStringLiteral("Rien"), QStringLiteral("Une source"), QStringLiteral("Une charge")});
    choix->setCurrentIndex(static_cast<int>(l.bouclage));
    m_tableLiaisons->setCellWidget(i, 1, choix);
    auto* valeur = new QLineEdit(QLocale::c().toString(l.valeur, 'g', 6));
    valeur->setEnabled(l.bouclage != BouclageLiaison::Rien);
    m_tableLiaisons->setCellWidget(i, 2, valeur);
    const std::string reseau = l.reseau;
    connect(choix, &QComboBox::activated, this, [this, reseau, valeur](int k) {
      setBouclage(reseau, static_cast<BouclageLiaison>(k), QLocale::c().toDouble(valeur->text()));
    });
    connect(valeur, &QLineEdit::editingFinished, this, [this, reseau, valeur] {
      for (auto& x : m_options.liaisons)
        if (x.reseau == reseau) x.valeur = QLocale::c().toDouble(valeur->text().replace(QLatin1Char(','), QLatin1Char('.')));
    });
  }
}

bool FenetreSimulation::ouvrirOnglet(const QString& nom) {
  for (const auto& o : classeur().onglets)
    if (q(o.nom) == nom) {
      setAnalyse(o.analyse);
      m_options = o.options;
      {
        const QSignalBlocker b(m_partie);
        m_partie->setCurrentIndex(m_options.composants.empty() ? 0 : 2);
      }
      majLiaisons();
      setCompatibilite(o.options.compatibilite);
      sonder(o.sondes);
      m_expressions->clear();
      for (const auto& e : o.expressions) ajouterExpression(q(e));
      m_onglet = nom;
      m_verdict->setText(o.verdict.empty() ? QString() : QStringLiteral("Dernier verdict : <b>%1</b>").arg(q(o.verdict)));
      return true;
    }
  return false;
}

bool FenetreSimulation::figerResultat(const QString& nom) {
  if (!m_resultat.reussi()) return false;
  const ResultatFige f = figer(m_resultat, m_netlist, nom.toStdString(), m_onglet.toStdString(), QDateTime::currentDateTime().toString(Qt::ISODate).toStdString());
  const bool fait = m_atelier.modifierConception([&](ConceptionElec& c) {
    ClasseurSimulation k = classeurDe(c);
    k.figes.erase(std::remove_if(k.figes.begin(), k.figes.end(), [&](const ResultatFige& x) { return x.nom == f.nom; }), k.figes.end());
    k.figes.push_back(f);
    ecrireClasseur(c, k);
  }, QStringLiteral("Résultat figé : %1").arg(nom));
  refaire();
  return fait;
}

bool FenetreSimulation::prendreReference(const QString& fige, const QString& courbe, double bande) {
  if (m_onglet.isEmpty() && !enregistrerOnglet(QStringLiteral("Analyse"))) return false;
  const bool fait = m_atelier.modifierConception([&](ConceptionElec& c) {
    ClasseurSimulation k = classeurDe(c);
    for (auto& o : k.onglets)
      if (q(o.nom) == m_onglet) {
        o.reference = fige.toStdString();
        o.courbeReference = courbe.toStdString();
        o.bande = bande;
      }
    ecrireClasseur(c, k);
  }, QStringLiteral("Référence de « %1 » : %2").arg(m_onglet, fige));
  refaire();
  return fait;
}

Comparaison FenetreSimulation::comparer() {
  Comparaison c;
  const ClasseurSimulation k = classeur();
  const OngletClasseur* o = nullptr;
  for (const auto& x : k.onglets)
    if (q(x.nom) == m_onglet) o = &x;
  if (!o || o->reference.empty() || !m_resultat.reussi()) {
    m_verdict->setText(QStringLiteral("Ouvre une analyse du classeur qui a une référence, puis lance le calcul."));
    return c;
  }
  const ResultatFige* f = k.fige(o->reference);
  if (!f) return c;
  std::string expr = o->courbeReference;
  if (expr.find('(') == std::string::npos) expr = "mag(V(" + expr + "))";
  const CourbeCalculee ref = evaluerCourbe(expr, resultatDe(*f), noeudsDe(*f)), cour = evaluerCourbe(expr, m_resultat, m_netlist);
  if (!ref.valide() || !cour.valide()) {
    m_verdict->setText(QStringLiteral("Courbe « %1 » introuvable dans le résultat ou la référence.").arg(q(o->courbeReference)));
    return c;
  }
  c = comparerCourbes(cour.x, cour.complexe ? cour.modules() : cour.reels(), ref.x, ref.complexe ? ref.modules() : ref.reels(), o->bande, o->genreBande);
  // Écarts surlignés sur la courbe comparée, tracée en plus si besoin
  TraceCourbes::Courbe k2;
  k2.nom = q(o->courbeReference) + QStringLiteral(" (comparée)");
  k2.couleur = QColor(0x44, 0x44, 0x44);
  k2.x = cour.x;
  k2.y = cour.complexe ? cour.modules() : cour.reels();
  k2.surlignes = c.horsBande;
  auto courbes = m_trace->courbes();
  courbes.erase(std::remove_if(courbes.begin(), courbes.end(), [](const TraceCourbes::Courbe& x) { return x.nom.endsWith(QStringLiteral("(comparée)")); }), courbes.end());
  courbes.push_back(k2);
  const bool ac = m_resultat.echelle == "frequency";
  m_trace->setCourbes(std::move(courbes), ac ? QStringLiteral("Fréquence (Hz)") : QStringLiteral("Temps (s)"), QStringLiteral("Valeur"), ac);
  m_verdict->setText(QStringLiteral("<b>%1</b> : écart maximal %2 %3 à %4 ; %5 tronçon(s) hors de la bande de %6 %.")
                         .arg(q(c.verdict()))
                         .arg(QLocale().toString(o->genreBande == GenreBande::Relative ? 100 * c.ecartMax : c.ecartMax, 'g', 3))
                         .arg(o->genreBande == GenreBande::Relative ? QStringLiteral("%") : QString())
                         .arg(si(c.xPire))
                         .arg(c.horsBande.size())
                         .arg(QLocale().toString(100 * o->bande, 'g', 3)));
  const std::string verdict = c.verdict();
  m_atelier.modifierConception([&](ConceptionElec& cc) {
    ClasseurSimulation kk = classeurDe(cc);
    for (auto& x : kk.onglets)
      if (q(x.nom) == m_onglet) x.verdict = verdict;
    ecrireClasseur(cc, kk);
  }, QStringLiteral("Verdict de « %1 » : %2").arg(m_onglet, q(verdict)));
  return c;
}

namespace {
std::string pngDuTrace(TraceCourbes* t) {
  QByteArray octets;
  QBuffer b(&octets);
  b.open(QIODevice::WriteOnly);
  t->grab().toImage().save(&b, "PNG");
  return octets.toBase64().toStdString();
}
}  // namespace

bool FenetreSimulation::poserCourbeSurFeuille() {
  if (!m_resultat.reussi() || !m_atelier.vueSchema()) return false;
  const int feuille = std::max(0, m_atelier.vueSchema()->feuille());
  const std::string cle = "courbe:" + (m_onglet.isEmpty() ? std::string("analyse") : m_onglet.toStdString());
  const std::string donnees = pngDuTrace(m_trace);
  return m_atelier.modifierConception([&](ConceptionElec& c) {
    if (feuille >= static_cast<int>(c.feuilles.size())) return;
    auto& im = c.feuilles[static_cast<std::size_t>(feuille)].images;
    for (auto& x : im)
      if (x.fichier == cle) {
        x.donnees = donnees;
        return;
      }
    ImageSchema x;
    x.uuid = nouvelUuid();
    x.fichier = cle;
    x.donnees = donnees;
    x.p = ptMm(20, 20);
    x.echelle = 0.5;
    im.push_back(x);
  }, QStringLiteral("Courbe posée sur la feuille"));
}

void FenetreSimulation::majImagesCourbes() {
  // Les courbes posées sur les feuilles (« courbe:<analyse> ») sont refaites à chaque calcul de leur analyse
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c || !m_resultat.reussi()) return;
  const std::string cle = "courbe:" + (m_onglet.isEmpty() ? std::string("analyse") : m_onglet.toStdString());
  bool present = false;
  for (const auto& f : c->feuilles)
    for (const auto& im : f.images) present |= im.fichier == cle;
  if (!present) return;
  const std::string donnees = pngDuTrace(m_trace);
  m_atelier.modifierConception([&](ConceptionElec& y) {
    for (auto& f : y.feuilles)
      for (auto& im : f.images)
        if (im.fichier == cle) im.donnees = donnees;
  }, QStringLiteral("Courbe de la feuille refaite"), QStringLiteral("courbe-feuille"));
}

std::vector<std::string> FenetreSimulation::console(const QString& commande) {
  std::string refus;
  const auto r = commandeConsole(commande.toStdString(), &refus);
  m_console->appendPlainText(QStringLiteral("> ") + commande);
  if (!refus.empty()) m_console->appendPlainText(QStringLiteral("refusé : ") + q(refus));
  for (const auto& l : r) m_console->appendPlainText(q(l));
  return r;
}

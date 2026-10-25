// Prométhée : platine d'essai de l'atelier électronique (lot L16 de la 0.34). Voir Platine034.hpp.
// Licence GPL-3.0-only.
#include "Platine034.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "Laboratoire034.hpp"
#include "promethee/fichiers.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QPushButton* bouton(const char* nom, const QString& texte) {
  auto* b = new QPushButton(texte);
  b->setObjectName(QLatin1String(nom));
  return b;
}
constexpr double kHauteur = 48.26;  // mm, hauteur du dessin de la platine
constexpr double kPi = 3.14159265358979323846;
double largeurDessin(const VuePlatine& v) { return (v.modele.colonnes + 2) * 2.54; }
QColor couleurNoeud(int n) {
  static const QColor c[] = {QColor(0x1E, 0x88, 0xE5), QColor(0xFB, 0x8C, 0x00), QColor(0x8E, 0x24, 0xAA), QColor(0x00, 0x89, 0x7B), QColor(0xF4, 0x51, 0x1E), QColor(0x3F, 0x51, 0xB5),
                             QColor(0xC0, 0xCA, 0x33), QColor(0x6D, 0x4C, 0x41), QColor(0xD8, 0x1B, 0x60), QColor(0x00, 0xAC, 0xC1), QColor(0x7C, 0xB3, 0x42), QColor(0x5E, 0x35, 0xB1)};
  return c[(n < 0 ? 0 : n) % 12];
}
QColor couleurComposant(const std::string& symbole) {
  if (symbole == "R" || symbole == "Potentiometre") return QColor(0xD7, 0xC4, 0x9E);
  if (symbole == "LED") return QColor(0xE5, 0x39, 0x35);
  if (symbole == "C") return QColor(0xF9, 0xA8, 0x25);
  if (symbole == "C_Pol") return QColor(0x15, 0x65, 0xC0);
  if (symbole == "Interrupteur" || symbole == "Bouton") return QColor(0x45, 0x5A, 0x64);
  return QColor(0x60, 0x7D, 0x8B);
}
}  // namespace

// ======================================================================================================================
// Vue de dessus
// ======================================================================================================================
VuePlatineEssai::VuePlatineEssai(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("vuePlatine"));
  setMinimumSize(560, 220);
  setMouseTracking(true);
}

void VuePlatineEssai::setDonnees(const ConceptionElec& c, const VuePlatine& v) {
  m_c = c;
  m_v = v;
  m_k = connectivitePlatine(v);
  m_etats = etatsPattes(c, v);
  if (m_appui) allumer(m_appui);
  update();
}

double VuePlatineEssai::echelle() const { return std::max(1.0, std::min((width() - 24) / largeurDessin(m_v), (height() - 24) / kHauteur)); }

QPointF VuePlatineEssai::versEcran(double xmm, double ymm) const {
  const double s = echelle();
  return {(width() - largeurDessin(m_v) * s) / 2 + xmm * s, (height() - kHauteur * s) / 2 + ymm * s};
}

QPointF VuePlatineEssai::pointDuTrou(const Trou& t) const {
  const Pt p = positionTrou(t);
  return versEcran(mm(p.x), mm(p.y));
}

std::optional<Trou> VuePlatineEssai::trouSous(QPointF p) const {
  const double s = echelle();
  const QPointF o = versEcran(0, 0);
  return trouProche(m_v.modele, ptMm((p.x() - o.x()) / s, (p.y() - o.y()) / s), 1'270'000);
}

EtatPatte VuePlatineEssai::etat(const std::string& composant, const std::string& broche) const {
  const auto it = m_etats.find({composant, broche});
  return it == m_etats.end() ? EtatPatte::Libre : it->second;
}

void VuePlatineEssai::allumer(const std::optional<Trou>& t) {
  m_potentiel.clear();
  m_voulues.clear();
  m_parasites.clear();
  m_noeudAllume = -1;
  if (t) {
    m_potentiel = trousDuPotentiel(m_v, *t);
    m_noeudAllume = m_k.noeudDuTrou(*t);
    if (m_noeudAllume >= 0) {
      // Le réseau du schéma le plus présent sur le nœud : ses broches sont voulues, les autres parasites
      const Connectivite k = connectivite(m_c);
      std::map<int, int> compte;
      for (const auto& b : m_k.noeuds[static_cast<std::size_t>(m_noeudAllume)])
        if (const int r = k.reseau(b.composant, b.broche); r >= 0) ++compte[r];
      int voulu = -1, meilleur = 0;
      for (const auto& [r, n] : compte)
        if (n > meilleur) meilleur = n, voulu = r;
      for (const auto& b : m_k.noeuds[static_cast<std::size_t>(m_noeudAllume)]) {
        const int r = k.reseau(b.composant, b.broche);
        (r >= 0 && r == voulu ? m_voulues : m_parasites).insert({b.composant, b.broche});
      }
    }
  }
  update();
}

void VuePlatineEssai::signaler(const std::optional<Trou>& t) {
  m_signale = t;
  update();
}

void VuePlatineEssai::paintEvent(QPaintEvent*) {
  QPainter g(this);
  g.setRenderHint(QPainter::Antialiasing);
  g.fillRect(rect(), palette().window());
  const double s = echelle(), L = largeurDessin(m_v);
  // Corps, rainure centrale et rails
  g.setPen(QPen(QColor(0xB0, 0xA8, 0x90), 1));
  g.setBrush(QColor(0xF4, 0xF1, 0xE8));
  g.drawRoundedRect(QRectF(versEcran(0, 0), versEcran(L, kHauteur)), 6, 6);
  const double ye = mm(positionTrou({1, "e"}).y), yf = mm(positionTrou({1, "f"}).y);
  g.fillRect(QRectF(versEcran(1.27, ye + 1.7), versEcran(L - 1.27, yf - 1.7)), QColor(0xE2, 0xDC, 0xC8));
  for (const char* r : {"-h", "+h", "+b", "-b"}) {
    const double y = mm(positionTrou({1, r}).y) + (std::string(r) == "-h" || std::string(r) == "+b" ? -1.35 : 1.35);
    g.setPen(QPen(r[0] == '+' ? QColor(0xD3, 0x2F, 0x2F) : QColor(0x19, 0x76, 0xD2), std::max(1.0, 0.22 * s)));
    g.drawLine(versEcran(1.8, y), versEcran(L - 1.8, y));
  }
  // Trous, et le potentiel allumé dans la couleur de son nœud
  std::vector<std::string> tout{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "+h", "-h", "+b", "-b"};
  const std::set<Trou> allumes(m_potentiel.begin(), m_potentiel.end());
  const QColor teinte = couleurNoeud(m_noeudAllume);
  for (int col = 1; col <= m_v.modele.colonnes; ++col)
    for (const auto& r : tout) {
      const Trou t{col, r};
      const QPointF p = pointDuTrou(t);
      if (allumes.count(t)) {
        g.fillRect(QRectF(p - QPointF(0.75 * s, 0.75 * s), QSizeF(1.5 * s, 1.5 * s)), teinte);
      } else
        g.fillRect(QRectF(p - QPointF(0.42 * s, 0.42 * s), QSizeF(0.84 * s, 0.84 * s)), QColor(0x5A, 0x55, 0x4C));
    }
  // Repères de lignes et de colonnes
  QFont f = font();
  f.setPixelSize(std::max(8, static_cast<int>(1.5 * s)));
  g.setFont(f);
  g.setPen(QColor(0x80, 0x78, 0x68));
  for (const char* r : {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"}) g.drawText(versEcran(0.5, mm(positionTrou({1, r}).y) + 0.6), QString::fromLatin1(r));
  for (int col = 1; col <= m_v.modele.colonnes; col += (col == 1 ? 4 : 5)) g.drawText(versEcran(col * 2.54 - 1.0, mm(positionTrou({1, "a"}).y) - 1.6), QString::number(col));
  // Fils : courbés en arc ou coudés
  for (const auto& w : m_v.fils) {
    const QPointF a = pointDuTrou(w.a), b = pointDuTrou(w.b);
    QPainterPath p(a);
    if (w.courbe) {
      const double l = std::hypot(b.x() - a.x(), b.y() - a.y());
      p.quadTo((a + b) / 2 + QPointF(0, -std::min(3.0 * s, 0.18 * l)), b);
    } else {
      p.lineTo(QPointF(a.x(), std::min(a.y(), b.y()) - 0.9 * s));
      p.lineTo(QPointF(b.x(), std::min(a.y(), b.y()) - 0.9 * s));
      p.lineTo(b);
    }
    g.setBrush(Qt::NoBrush);
    g.setPen(QPen(QColor(q(w.couleur)), std::max(2.0, 0.75 * s), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    g.drawPath(p);
  }
  // Composants : corps et pattes
  for (const auto& p : m_v.poses) {
    const ComposantElec* x = m_c.composant(p.composant);
    if (!x || p.broches.empty()) continue;
    std::vector<QPointF> pts;
    for (const auto& [n, t] : p.broches) pts.push_back(pointDuTrou(t));
    QPointF c0(0, 0);
    for (const auto& z : pts) c0 += z;
    c0 /= static_cast<double>(pts.size());
    bool haut = false, bas = false;
    for (const auto& [n, t] : p.broches) haut |= t.rangee == "e", bas |= t.rangee == "f";
    if (p.broches.size() > 2 && haut && bas) {
      // Boîtier DIP à cheval sur la rainure, encoche à gauche
      double x0 = 1e9, x1 = -1e9;
      for (const auto& z : pts) x0 = std::min(x0, z.x()), x1 = std::max(x1, z.x());
      const QRectF r(QPointF(x0 - 1.1 * s, versEcran(0, ye + 0.9).y()), QPointF(x1 + 1.1 * s, versEcran(0, yf - 0.9).y()));
      g.setPen(Qt::NoPen);
      g.setBrush(QColor(0x26, 0x32, 0x38));
      g.drawRoundedRect(r, 0.4 * s, 0.4 * s);
      g.setBrush(QColor(0x45, 0x5A, 0x64));
      g.drawEllipse(QPointF(r.left(), r.center().y()), 0.9 * s, 0.9 * s);
      g.setPen(Qt::white);
      g.drawText(r, Qt::AlignCenter, q(x->reference + " " + x->valeur));
    } else {
      g.setPen(QPen(QColor(0x90, 0xA4, 0xAE), std::max(1.0, 0.25 * s)));
      for (const auto& z : pts) g.drawLine(z, c0);
      double angle = 0;
      if (pts.size() == 2) angle = std::atan2(pts[1].y() - pts[0].y(), pts[1].x() - pts[0].x()) * 180 / kPi;
      g.save();
      g.translate(c0);
      g.rotate(angle);
      g.setPen(QPen(couleurComposant(x->symbole).darker(140), 1));
      g.setBrush(couleurComposant(x->symbole));
      const QRectF r(-2.3 * s, -0.9 * s, 4.6 * s, 1.8 * s);
      g.drawRoundedRect(r, 0.8 * s, 0.8 * s);
      g.restore();
      g.setPen(QColor(0x21, 0x21, 0x21));
      g.drawText(c0 + QPointF(-2.2 * s, -1.4 * s), q(x->reference));
    }
    // Points des pattes : vert enfoncée et reliée, rouge sur une liaison ratée, gris sans liaison voulue
    for (const auto& [n, t] : p.broches) {
      const EtatPatte e = etat(p.composant, n);
      const QColor c = e == EtatPatte::Verte ? QColor(0x2E, 0x7D, 0x32) : e == EtatPatte::Rouge ? QColor(0xD3, 0x2F, 0x2F) : QColor(0x9E, 0x9E, 0x9E);
      const QPointF z = pointDuTrou(t);
      g.setPen(Qt::NoPen);
      g.setBrush(c);
      g.drawEllipse(z, 0.6 * s, 0.6 * s);
      if (m_voulues.count({p.composant, n}) || m_parasites.count({p.composant, n})) {
        g.setBrush(Qt::NoBrush);
        g.setPen(QPen(m_parasites.count({p.composant, n}) ? QColor(0xD3, 0x2F, 0x2F) : QColor(0x2E, 0x7D, 0x32), std::max(1.5, 0.3 * s)));
        g.drawEllipse(z, 1.25 * s, 1.25 * s);
      }
    }
  }
  if (m_signale) {
    g.setBrush(Qt::NoBrush);
    g.setPen(QPen(QColor(0xFF, 0x8F, 0x00), std::max(2.0, 0.35 * s)));
    g.drawEllipse(pointDuTrou(*m_signale), 1.8 * s, 1.8 * s);
  }
  if (m_appui && m_tire) {
    g.setPen(QPen(QColor(0x42, 0x42, 0x42), 2, Qt::DashLine, Qt::RoundCap));
    g.drawLine(pointDuTrou(*m_appui), m_curseur);
  }
}

void VuePlatineEssai::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  m_appui = trouSous(e->position());
  m_tire = false;
  m_curseur = e->position();
  if (m_appui) allumer(m_appui);
}

void VuePlatineEssai::mouseMoveEvent(QMouseEvent* e) {
  if (!m_appui || !(e->buttons() & Qt::LeftButton)) return;
  m_curseur = e->position();
  if (!m_tire && (m_curseur - pointDuTrou(*m_appui)).manhattanLength() > 6) m_tire = true;
  update();
}

void VuePlatineEssai::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  const std::optional<Trou> a = m_appui;
  const bool tire = m_tire;
  m_appui.reset();
  m_tire = false;
  allumer(std::nullopt);
  if (a && tire)
    if (const auto b = trouSous(e->position()); b && *b != *a) emit filTire(*a, *b);
}

// ======================================================================================================================
// Vue 3D
// ======================================================================================================================
VuePlatine3D::VuePlatine3D(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("vuePlatine3D"));
  setMinimumSize(560, 320);
}

void VuePlatine3D::setScene(std::vector<Objet3DPlatine> s) {
  m_scene = std::move(s);
  update();
}

void VuePlatine3D::setEtats(const std::set<std::string>& leds, const std::set<std::string>& fermes) {
  if (leds == m_leds && fermes == m_fermes) return;
  m_leds = leds;
  m_fermes = fermes;
  update();
}

void VuePlatine3D::tourner(double degres) {
  m_azimut += degres;
  update();
}

namespace {
struct Repere3D {
  double ca, sa, ce, se, k, ox, oy;
};
Repere3D repere(double azimut, double elevation, double zoom, double largeur, double hauteurDessin, int w, int h) {
  Repere3D r{std::cos(azimut * kPi / 180), std::sin(azimut * kPi / 180), std::cos(elevation * kPi / 180), std::sin(elevation * kPi / 180), 1, 0, 0};
  // Cadrage : les coins de la platine (dessus et dessous) tiennent dans la vue
  double u0 = 1e18, u1 = -1e18, v0 = 1e18, v1 = -1e18;
  for (double x : {0.0, largeur})
    for (double y : {0.0, hauteurDessin})
      for (double z : {-8.5, 14.0}) {
        const double X = x * r.ca - y * r.sa, Y = x * r.sa + y * r.ca;
        const double u = X, v = Y * r.se - z * r.ce;
        u0 = std::min(u0, u), u1 = std::max(u1, u), v0 = std::min(v0, v), v1 = std::max(v1, v);
      }
  r.k = std::min((w - 20) / (u1 - u0), (h - 20) / (v1 - v0)) * zoom;
  r.ox = w / 2.0 - (u0 + u1) / 2 * r.k;
  r.oy = h / 2.0 - (v0 + v1) / 2 * r.k;
  return r;
}
}  // namespace

QPointF VuePlatine3D::projeter(double x, double y, double z) const {
  const double L = m_scene.empty() ? 165.1 : m_scene.front().points.size() > 1 ? m_scene.front().points[1][0] : 165.1;
  const Repere3D r = repere(m_azimut, m_elevation, m_zoom, L, kHauteur, width(), height());
  const double X = x * r.ca - y * r.sa, Y = x * r.sa + y * r.ca;
  return {r.ox + X * r.k, r.oy + (Y * r.se - z * r.ce) * r.k};
}

double VuePlatine3D::profondeur(double x, double y, double z) const {
  const double ca = std::cos(m_azimut * kPi / 180), sa = std::sin(m_azimut * kPi / 180);
  const double ce = std::cos(m_elevation * kPi / 180), se = std::sin(m_elevation * kPi / 180);
  const double Y = x * sa + y * ca;
  return Y * ce + z * se;  // vers l'œil
}

namespace {
// Faces d'une boîte : quatre coins, la normale sortante et l'éclairage relatif (la face du dessous ne se voit pas)
struct Face {
  std::array<std::array<double, 3>, 4> c;
  std::array<double, 3> normale;
  double lumiere;
};
std::vector<Face> faces(const std::array<double, 3>& a, const std::array<double, 3>& b) {
  const double x0 = std::min(a[0], b[0]), x1 = std::max(a[0], b[0]), y0 = std::min(a[1], b[1]), y1 = std::max(a[1], b[1]), z0 = std::min(a[2], b[2]), z1 = std::max(a[2], b[2]);
  return {{{{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}, {0, 0, 1}, 1.18},
          {{{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}}, {0, 1, 0}, 0.92},
          {{{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, {0, -1, 0}, 0.80},
          {{{{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}}, {1, 0, 0}, 0.74},
          {{{{x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}}}, {-1, 0, 0}, 0.84}};
}
QColor eclairer(QColor c, double l) { return l >= 1 ? c.lighter(static_cast<int>(100 * l)) : c.darker(static_cast<int>(100 / l)); }
}  // namespace

void VuePlatine3D::paintEvent(QPaintEvent*) {
  QPainter g(this);
  g.setRenderHint(QPainter::Antialiasing);
  QLinearGradient fond(0, 0, 0, height());
  fond.setColorAt(0, QColor(0xEC, 0xEF, 0xF1));
  fond.setColorAt(1, QColor(0xCF, 0xD8, 0xDC));
  g.fillRect(rect(), fond);
  if (m_scene.empty()) return;
  struct Piece {
    double d;
    std::function<void(QPainter&)> dessin;
  };
  std::vector<Piece> pieces;
  auto P = [this](const std::array<double, 3>& p) { return projeter(p[0], p[1], p[2]); };
  auto D = [this](const std::array<double, 3>& p) { return profondeur(p[0], p[1], p[2]); };
  const double k = (P({10, 0, 0}) - P({0, 0, 0})).manhattanLength() / 10;  // pixels par mm, environ
  // Direction de l'œil dans le repère de la platine
  const double ca = std::cos(m_azimut * kPi / 180), sa = std::sin(m_azimut * kPi / 180), ce = std::cos(m_elevation * kPi / 180), se = std::sin(m_elevation * kPi / 180);
  const std::array<double, 3> vue{sa * ce, ca * ce, se};
  for (const auto& o : m_scene) {
    if (o.genre == "fil" || o.genre == "patte") {
      const QColor c(q(o.couleur));
      for (std::size_t i = 1; i < o.points.size(); ++i) {
        const auto a = o.points[i - 1], b = o.points[i];
        const std::array<double, 3> m{(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2};
        const double largeur = std::max(1.2, 2 * o.rayon * k);
        pieces.push_back({D(m) + 0.01, [=](QPainter& p) {
                            p.setPen(QPen(c.darker(130), largeur, Qt::SolidLine, Qt::RoundCap));
                            p.drawLine(P(a), P(b));
                            p.setPen(QPen(c.lighter(150), std::max(0.6, largeur * 0.3), Qt::SolidLine, Qt::RoundCap));
                            p.drawLine(P(a) + QPointF(-largeur * 0.18, -largeur * 0.18), P(b) + QPointF(-largeur * 0.18, -largeur * 0.18));
                          }});
      }
      continue;
    }
    if (o.points.size() < 2) continue;
    const bool platine = o.genre == "platine";
    const bool allumee = o.genre == "led" && m_leds.count(o.reference);
    QColor base(q(o.couleur));
    if (o.genre == "led") base = allumee ? QColor(0xFF, 0x3D, 0x3D) : QColor(0x7F, 0x1D, 0x1D);
    for (const Face& f : faces(o.points[0], o.points[1])) {
      QPolygonF poly;
      std::array<double, 3> centre{0, 0, 0};
      for (const auto& c : f.c) {
        poly << P(c);
        for (int i = 0; i < 3; ++i) centre[static_cast<std::size_t>(i)] += c[static_cast<std::size_t>(i)] / 4;
      }
      // Face tournée vers l'œil seulement
      if (f.normale[0] * vue[0] + f.normale[1] * vue[1] + f.normale[2] * vue[2] <= 1e-9) continue;
      const QColor c = eclairer(base, f.lumiere);
      pieces.push_back({platine ? -1e9 + f.lumiere : D(centre), [poly, c](QPainter& p) {
                          p.setPen(QPen(c.darker(125), 0.8));
                          p.setBrush(c);
                          p.drawPolygon(poly);
                        }});
    }
    if (platine) {
      // Trous sur le dessus, rainure et rails
      const double L = o.points[1][0];
      pieces.push_back({-1e9 + 2, [=](QPainter& p) {
                          const double ye = mm(positionTrou({1, "e"}).y), yf = mm(positionTrou({1, "f"}).y);
                          QPolygonF r{P({1.27, ye + 1.7, 0}), P({L - 1.27, ye + 1.7, 0}), P({L - 1.27, yf - 1.7, 0}), P({1.27, yf - 1.7, 0})};
                          p.setPen(Qt::NoPen);
                          p.setBrush(QColor(0xDE, 0xD7, 0xC0));
                          p.drawPolygon(r);
                          p.setBrush(QColor(0x5A, 0x55, 0x4C));
                          const double t = std::max(0.8, 0.45 * k);
                          for (int col = 1; col <= static_cast<int>(std::lround((L / 2.54) - 2)); ++col)
                            for (const char* rg : {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "+h", "-h", "+b", "-b"}) {
                              const Pt h = positionTrou({col, rg});
                              p.drawEllipse(P({mm(h.x), mm(h.y), 0}), t, t * 0.7);
                            }
                        }});
      continue;
    }
    const auto a = o.points[0], b = o.points[1];
    const std::array<double, 3> haut{(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, std::max(a[2], b[2])};
    if (allumee)
      pieces.push_back({D(haut) + 0.5, [=](QPainter& p) {
                          QRadialGradient halo(P(haut), 9 * k);
                          halo.setColorAt(0, QColor(255, 80, 60, 200));
                          halo.setColorAt(1, QColor(255, 80, 60, 0));
                          p.setPen(Qt::NoPen);
                          p.setBrush(halo);
                          p.drawEllipse(P(haut), 9 * k, 9 * k);
                        }});
    if (o.genre == "interrupteur") {
      // Levier : à droite et vert quand l'interrupteur est fermé
      const bool ferme = m_fermes.count(o.reference) > 0;
      const double dx = (b[0] - a[0]) / 2, cx = (a[0] + b[0]) / 2 + (ferme ? dx * 0.45 : -dx * 0.45), cy = (a[1] + b[1]) / 2;
      pieces.push_back({D(haut) + 0.4, [=](QPainter& p) {
                          p.setPen(QPen(ferme ? QColor(0x2E, 0x7D, 0x32) : QColor(0xEC, 0xEF, 0xF1), std::max(3.0, 1.6 * k), Qt::SolidLine, Qt::RoundCap));
                          p.drawLine(P({cx, cy, haut[2]}), P({cx, cy, haut[2] + 2.6}));
                        }});
    }
    if (!o.reference.empty())
      pieces.push_back({D(haut) + 0.6, [=](QPainter& p) {
                          p.setPen(QColor(0x21, 0x21, 0x21));
                          p.drawText(P({haut[0], haut[1], haut[2] + 3.6}) - QPointF(8, 0), q(o.reference));
                        }});
  }
  std::stable_sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return a.d < b.d; });
  for (const auto& p : pieces) p.dessin(g);
}

std::optional<Objet3DPlatine> VuePlatine3D::objetSous(QPointF p) const {
  std::optional<Objet3DPlatine> r;
  double meilleur = -1e18;
  for (const auto& o : m_scene) {
    if (o.genre != "corps" && o.genre != "led" && o.genre != "interrupteur") continue;
    if (o.points.size() < 2) continue;
    bool dedans = false;
    for (const Face& f : faces(o.points[0], o.points[1])) {
      QPolygonF poly;
      for (const auto& c : f.c) poly << projeter(c[0], c[1], c[2]);
      dedans |= poly.containsPoint(p, Qt::OddEvenFill);
    }
    if (!dedans) continue;
    const double d = profondeur((o.points[0][0] + o.points[1][0]) / 2, (o.points[0][1] + o.points[1][1]) / 2, std::max(o.points[0][2], o.points[1][2]));
    if (d > meilleur) meilleur = d, r = o;
  }
  return r;
}

std::optional<QPointF> VuePlatine3D::centre(const std::string& reference) const {
  for (const auto& o : m_scene)
    if (o.reference == reference && (o.genre == "corps" || o.genre == "led" || o.genre == "interrupteur") && o.points.size() >= 2)
      return projeter((o.points[0][0] + o.points[1][0]) / 2, (o.points[0][1] + o.points[1][1]) / 2, std::max(o.points[0][2], o.points[1][2]));
  return std::nullopt;
}

void VuePlatine3D::mousePressEvent(QMouseEvent* e) {
  m_glisse = e->position();
  if (e->button() != Qt::LeftButton) return;
  if (const auto o = objetSous(e->position())) emit composantClique(q(o->composant), q(o->reference), q(o->genre));
}

void VuePlatine3D::mouseMoveEvent(QMouseEvent* e) {
  if (!(e->buttons() & Qt::RightButton)) return;
  const QPointF d = e->position() - m_glisse;
  m_glisse = e->position();
  m_azimut += d.x() * 0.4;
  m_elevation = std::clamp(m_elevation - d.y() * 0.3, 12.0, 85.0);
  update();
}

void VuePlatine3D::wheelEvent(QWheelEvent* e) {
  m_zoom = std::clamp(m_zoom * (e->angleDelta().y() > 0 ? 1.12 : 1 / 1.12), 0.4, 6.0);
  update();
}

// ======================================================================================================================
// Fenêtre
// ======================================================================================================================
FenetrePlatine::FenetrePlatine(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetrePlatine"));
  setWindowTitle(QStringLiteral("Platine d'essai"));
  resize(1180, 720);
  auto* col = new QVBoxLayout(this);
  m_onglets = new QTabWidget;
  m_onglets->setObjectName(QStringLiteral("ongletsPlatine"));
  col->addWidget(m_onglets);
  m_etat = new QLabel;
  m_etat->setObjectName(QStringLiteral("etatPlatine"));
  m_etat->setWordWrap(true);
  col->addWidget(m_etat);
  const std::vector<std::pair<const char*, QString>> pages{
      {"platine", QStringLiteral("Platine")}, {"3d", QStringLiteral("En 3D")}, {"documents", QStringLiteral("Documents de câblage")}, {"guitare", QStringLiteral("Guitare")}};
  for (const auto& [id, titre] : pages) {
    auto* p = new QWidget;
    p->setObjectName(QLatin1String(id));
    m_onglets->addTab(p, titre);
    const std::string s = id;
    if (s == "platine") construirePlatine(p);
    else if (s == "3d") construire3D(p);
    else if (s == "documents") construireDocuments(p);
    else construireGuitare(p);
  }
  m_minuterie = new QTimer(this);
  m_minuterie->setInterval(120);
  connect(m_minuterie, &QTimer::timeout, this, [this] {
    if (m_vue3D->isVisible()) rafraichir3D();
  });
  m_minuterie->start();
  if (Document* d = m_atelier.document()) {
    connect(d, &Document::change, this, [this] {
      if (isVisible()) recharger();
    });
    connect(d, &Document::conceptionChangee, this, [this] {
      if (isVisible()) recharger();
    });
  }
}

FenetrePlatine::~FenetrePlatine() = default;

void FenetrePlatine::construirePlatine(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  auto* barre = new QHBoxLayout;
  auto* monter = bouton("monterPlatine", QStringLiteral("Monter de lui-même"));
  auto* etape = bouton("etapePlatine", QStringLiteral("Étape suivante"));
  auto* verifier = bouton("verifierPlatine", QStringLiteral("Comparer au schéma"));
  m_couleur = new QComboBox;
  m_couleur->setObjectName(QStringLiteral("couleurFil"));
  for (const auto& [nom, c] : std::vector<std::pair<QString, QString>>{{QStringLiteral("rouge"), QStringLiteral("#D32F2F")},
                                                                       {QStringLiteral("noir"), QStringLiteral("#212121")},
                                                                       {QStringLiteral("bleu"), QStringLiteral("#1565C0")},
                                                                       {QStringLiteral("jaune"), QStringLiteral("#FBC02D")},
                                                                       {QStringLiteral("vert"), QStringLiteral("#43A047")},
                                                                       {QStringLiteral("orange"), QStringLiteral("#FB8C00")}})
    m_couleur->addItem(nom, c);
  barre->addWidget(monter);
  barre->addWidget(etape);
  barre->addWidget(verifier);
  barre->addStretch();
  barre->addWidget(new QLabel(QStringLiteral("Fil :")));
  barre->addWidget(m_couleur);
  l->addLayout(barre);
  auto* sep = new QSplitter;
  m_vue = new VuePlatineEssai;
  sep->addWidget(m_vue);
  m_listeEcarts = new QListWidget;
  m_listeEcarts->setObjectName(QStringLiteral("ecartsPlatine"));
  sep->addWidget(m_listeEcarts);
  sep->setStretchFactor(0, 4);
  sep->setStretchFactor(1, 1);
  l->addWidget(sep, 1);
  auto* aide = new QLabel(QStringLiteral("Tire un fil d'un trou à un autre ; garde le bouton appuyé sur un trou pour allumer tout son potentiel. "
                                         "Pattes vertes : enfoncées et reliées comme au schéma ; rouges : liaison ratée ou en trop."));
  aide->setWordWrap(true);
  l->addWidget(aide);
  connect(monter, &QPushButton::clicked, this, [this] { this->monter(); });
  connect(etape, &QPushButton::clicked, this, [this] { etapeSuivante(); });
  connect(verifier, &QPushButton::clicked, this, [this] { this->verifier(); });
  connect(m_listeEcarts, &QListWidget::currentRowChanged, this, [this](int i) { montrerEcart(i); });
  connect(m_vue, &VuePlatineEssai::filTire, this, [this](Trou a, Trou b) {
    QString e;
    if (!ajouterFil(a, b, &e)) m_etat->setText(e);
  });
}

void FenetrePlatine::construire3D(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  m_vue3D = new VuePlatine3D;
  l->addWidget(m_vue3D, 1);
  auto* aide = new QLabel(QStringLiteral("Clique un interrupteur pendant la simulation du laboratoire : la LED s'allume ou s'éteint. Bouton droit : tourner ; molette : zoom."));
  aide->setWordWrap(true);
  l->addWidget(aide);
  connect(m_vue3D, &VuePlatine3D::composantClique, this, [this](const QString&, const QString& ref, const QString& genre) {
    if (genre != QLatin1String("interrupteur")) {
      m_etat->setText(ref);
      return;
    }
    FenetreLaboratoire* lab = m_atelier.laboratoireOuvert();
    QPushButton* b = lab ? lab->commande(ref) : nullptr;
    if (!b) {
      m_etat->setText(QStringLiteral("Démarre la simulation continue du laboratoire (Simuler → Laboratoire…) pour faire marcher %1.").arg(ref));
      return;
    }
    b->click();
    if (lab) lab->rafraichir();
    rafraichir3D();
    m_etat->setText(QStringLiteral("%1 %2").arg(ref, b->isChecked() ? QStringLiteral("fermé") : QStringLiteral("ouvert")));
  });
}

void FenetrePlatine::construireDocuments(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  auto* barre = new QHBoxLayout;
  const std::vector<std::tuple<const char*, QString, QString>> sorties{{"exporterPlan", QStringLiteral("Plan de chargement (SVG)…"), QStringLiteral("platine.svg")},
                                                                       {"imprimerPlatine", QStringLiteral("À l'échelle (PDF)…"), QStringLiteral("platine.pdf")},
                                                                       {"hpglPlaque", QStringLiteral("Plaque à bandes (HPGL)…"), QStringLiteral("plaque.plt")},
                                                                       {"coupuresPlaque", QStringLiteral("Plan miroir des coupures (SVG)…"), QStringLiteral("coupures.svg")}};
  for (const auto& [nom, texte, fichier] : sorties) {
    auto* b = bouton(nom, texte);
    barre->addWidget(b);
    const QString f = fichier;
    connect(b, &QPushButton::clicked, this, [this, f] {
      const QString c = QFileDialog::getSaveFileName(this, QStringLiteral("Enregistrer"), f);
      if (!c.isEmpty()) m_etat->setText(exporter(c) ? QStringLiteral("Écrit : %1").arg(c) : QStringLiteral("Écriture impossible : %1").arg(c));
    });
  }
  barre->addStretch();
  l->addLayout(barre);
  auto* sep = new QSplitter;
  m_plan = new QPlainTextEdit;
  m_plan->setObjectName(QStringLiteral("planChargement"));
  m_plan->setReadOnly(true);
  sep->addWidget(m_plan);
  m_fils = new QTableWidget(0, 4);
  m_fils->setObjectName(QStringLiteral("listeFils"));
  m_fils->setHorizontalHeaderLabels({QStringLiteral("De"), QStringLiteral("À"), QStringLiteral("Couleur"), QStringLiteral("Longueur")});
  m_fils->horizontalHeader()->setStretchLastSection(true);
  sep->addWidget(m_fils);
  l->addWidget(sep, 1);
}

void FenetrePlatine::construireGuitare(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  auto* barre = new QHBoxLayout;
  m_selecteur = new QComboBox;
  m_selecteur->setObjectName(QStringLiteral("selecteurGuitare"));
  m_sortie = new QComboBox;
  m_sortie->setObjectName(QStringLiteral("sortieGuitare"));
  auto* analyser = bouton("analyserGuitare", QStringLiteral("Parcourir les positions"));
  barre->addWidget(new QLabel(QStringLiteral("Sélecteur :")));
  barre->addWidget(m_selecteur);
  barre->addWidget(new QLabel(QStringLiteral("Sortie :")));
  barre->addWidget(m_sortie);
  barre->addWidget(analyser);
  barre->addStretch();
  l->addLayout(barre);
  m_tableGuitare = new QTableWidget(0, 4);
  m_tableGuitare->setObjectName(QStringLiteral("positionsGuitare"));
  m_tableGuitare->setHorizontalHeaderLabels({QStringLiteral("Position"), QStringLiteral("Combinaison"), QStringLiteral("Couplage"), QStringLiteral("Phase")});
  m_tableGuitare->horizontalHeader()->setStretchLastSection(true);
  l->addWidget(m_tableGuitare, 1);
  connect(analyser, &QPushButton::clicked, this, [this] { analyserGuitare(m_selecteur->currentText(), m_sortie->currentText()); });
}

bool FenetrePlatine::montrer(const QString& onglet) {
  recharger();
  show();
  raise();
  if (onglet.isEmpty()) return true;
  for (int i = 0; i < m_onglets->count(); ++i)
    if (m_onglets->widget(i)->objectName() == onglet) {
      m_onglets->setCurrentIndex(i);
      if (onglet == QLatin1String("3d")) rafraichir3D();
      return true;
    }
  return false;
}

void FenetrePlatine::recharger() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  const VuePlatine v = platineDe(*c);
  m_vue->setDonnees(*c, v);
  m_vue3D->setScene(scenePlatine3D(*c, v));
  m_ecarts = comparerPlatine(*c, v);
  m_listeEcarts->blockSignals(true);
  m_listeEcarts->clear();
  for (const auto& e : m_ecarts) m_listeEcarts->addItem(q(e.message));
  m_listeEcarts->blockSignals(false);
  if (v.poses.empty())
    m_etat->setText(QStringLiteral("La platine est vide : « Monter de lui-même » pose tout le montage, « Étape suivante » un composant à la fois."));
  else
    m_etat->setText(m_ecarts.empty() ? QStringLiteral("Aucun écart avec le schéma (%1 composant(s), %2 fil(s)).").arg(v.poses.size()).arg(v.fils.size())
                                     : QStringLiteral("%1 écart(s) avec le schéma.").arg(m_ecarts.size()));
  majDocuments();
  // Sélecteurs et sorties d'une guitare
  const QString s0 = m_selecteur->currentText(), j0 = m_sortie->currentText();
  m_selecteur->clear();
  m_sortie->clear();
  for (const auto& x : c->composants) {
    if (x.symbole == "Selecteur_5") m_selecteur->addItem(q(x.reference));
    if (x.symbole == "Jack") m_sortie->addItem(q(x.reference));
  }
  if (!s0.isEmpty()) m_selecteur->setCurrentText(s0);
  if (!j0.isEmpty()) m_sortie->setCurrentText(j0);
}

bool FenetrePlatine::enregistrerVue(const VuePlatine& v, const QString& nom) {
  const bool ok = m_atelier.modifierConception([&](ConceptionElec& c) { ecrirePlatine(c, v); }, nom);
  recharger();
  return ok;
}

bool FenetrePlatine::monter() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  const VuePlatine v = monterSurPlatine(*c);
  m_etapes = static_cast<int>(v.poses.size());
  return enregistrerVue(v, QStringLiteral("Platine montée"));
}

bool FenetrePlatine::etapeSuivante() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  const VuePlatine avant = platineDe(*c);
  const VuePlatine tout = monterSurPlatine(*c);
  if (avant.poses.size() >= tout.poses.size()) {
    m_etat->setText(QStringLiteral("Le montage est complet."));
    return false;
  }
  m_etapes = static_cast<int>(avant.poses.size()) + 1;
  const VuePlatine v = monterSurPlatine(*c, m_etapes);
  const ComposantElec* x = v.poses.empty() ? nullptr : c->composant(v.poses.back().composant);
  const bool ok = enregistrerVue(v, QStringLiteral("Platine : étape %1").arg(m_etapes));
  if (x) m_etat->setText(QStringLiteral("Étape %1 : %2 posé.").arg(m_etapes).arg(q(x->reference)));
  return ok;
}

int FenetrePlatine::verifier() {
  recharger();
  return static_cast<int>(m_ecarts.size());
}

bool FenetrePlatine::montrerEcart(int i) {
  if (i < 0 || i >= static_cast<int>(m_ecarts.size())) return false;
  const EcartPlatine& e = m_ecarts[static_cast<std::size_t>(i)];
  m_onglets->setCurrentIndex(0);
  m_vue->signaler(e.trou.colonne > 0 ? std::optional<Trou>(e.trou) : std::nullopt);
  if (!e.composant.empty()) m_atelier.montrerBroche(e.composant, e.broche);
  m_etat->setText(q(e.message));
  return true;
}

std::optional<Trou> FenetrePlatine::trouDe(const QString& reference, const QString& broche) const {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const ComposantElec* x = c ? c->parReference(reference.toStdString()) : nullptr;
  if (!x) return std::nullopt;
  const PosePlatine* p = m_vue->vue().pose(x->uuid);
  if (!p) return std::nullopt;
  const auto it = p->broches.find(broche.toStdString());
  return it == p->broches.end() ? std::nullopt : std::optional<Trou>(it->second);
}

std::optional<Trou> FenetrePlatine::trouLibre(const Trou& memePotentiel) const {
  const VuePlatine& v = m_vue->vue();
  std::set<Trou> occupes;
  for (const auto& p : v.poses)
    for (const auto& [n, t] : p.broches) occupes.insert(t);
  for (const auto& f : v.fils) occupes.insert(f.a), occupes.insert(f.b);
  for (const auto& t : trousDuPotentiel(v, memePotentiel))
    if (!occupes.count(t)) return t;
  return std::nullopt;
}

bool FenetrePlatine::ajouterFil(const Trou& a, const Trou& b, QString* erreur) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  const VuePlatine& v = m_vue->vue();
  if (!trouValide(v.modele, a) || !trouValide(v.modele, b) || a == b) {
    if (erreur) *erreur = QStringLiteral("Le fil doit relier deux trous de la platine.");
    return false;
  }
  FilPlatine f;
  f.a = a;
  f.b = b;
  f.couleur = m_couleur->currentData().toString().toStdString();
  std::string nom, e;
  // Un seul pas d'annulation : le fil dans la vue et la liaison au schéma
  const bool ok = m_atelier.modifierConception([&](ConceptionElec& x) { nom = ajouterFilPlatine(x, f, &e); }, QStringLiteral("Fil de platine %1 → %2").arg(q(texteTrou(a)), q(texteTrou(b))));
  recharger();
  if (!e.empty()) {
    if (erreur) *erreur = q(e);
    return false;
  }
  if (ok && !nom.empty()) m_etat->setText(QStringLiteral("Fil %1 → %2 : réseau %3 relié au schéma et au circuit.").arg(q(texteTrou(a)), q(texteTrou(b)), q(nom)));
  return ok;
}

bool FenetrePlatine::deplacerPatte(const QString& reference, const QString& broche, const Trou& t) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const ComposantElec* x = c ? c->parReference(reference.toStdString()) : nullptr;
  if (!x) return false;
  VuePlatine v = platineDe(*c);
  PosePlatine* p = v.pose(x->uuid);
  if (!p || !p->broches.count(broche.toStdString())) return false;
  p->broches[broche.toStdString()] = t;
  return enregistrerVue(v, QStringLiteral("Patte %1.%2 en %3").arg(reference, broche, q(texteTrou(t))));
}

bool FenetrePlatine::deplacerComposant(const QString& reference, int colonnes, int rangees, QString* erreur) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const ComposantElec* x = c ? c->parReference(reference.toStdString()) : nullptr;
  if (!x) return false;
  VuePlatine v = platineDe(*c);
  std::string e;
  if (!deplacerSurPlatine(v, x->uuid, colonnes, rangees, &e)) {
    if (erreur) *erreur = q(e);
    m_etat->setText(q(e));
    return false;
  }
  return enregistrerVue(v, QStringLiteral("%1 déplacé sur la platine").arg(reference));
}

EtatPatte FenetrePlatine::etatPatte(const QString& reference, const QString& broche) const {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const ComposantElec* x = c ? c->parReference(reference.toStdString()) : nullptr;
  return x ? m_vue->etat(x->uuid, broche.toStdString()) : EtatPatte::Libre;
}

void FenetrePlatine::rafraichir3D() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  std::set<std::string> leds, fermes;
  FenetreLaboratoire* lab = m_atelier.laboratoireOuvert();
  if (lab)
    for (const auto& x : c->composants) {
      if (x.symbole == "LED" && lab->ledAllumee(q(x.reference))) leds.insert(x.reference);
      if (x.symbole == "Interrupteur")
        if (QPushButton* b = lab->commande(q(x.reference)); b && b->isChecked()) fermes.insert(x.reference);
    }
  m_vue3D->setEtats(leds, fermes);
}

void FenetrePlatine::majDocuments() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  const VuePlatine& v = m_vue->vue();
  m_plan->setPlainText(q(planDeChargement(*c, v)));
  const auto l = listeFils(v);
  m_fils->setRowCount(static_cast<int>(l.size()));
  for (int i = 0; i < static_cast<int>(l.size()); ++i) {
    const auto& f = l[static_cast<std::size_t>(i)];
    m_fils->setItem(i, 0, new QTableWidgetItem(q(f.de)));
    m_fils->setItem(i, 1, new QTableWidgetItem(q(f.a)));
    auto* couleur = new QTableWidgetItem(q(f.couleur));
    couleur->setBackground(QColor(q(f.couleur)));
    m_fils->setItem(i, 2, couleur);
    m_fils->setItem(i, 3, new QTableWidgetItem(QStringLiteral("%1 mm").arg(f.longueur, 0, 'f', 0)));
  }
}

QString FenetrePlatine::plan() const { return m_plan->toPlainText(); }

bool FenetrePlatine::exporter(const QString& chemin) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  const VuePlatine& v = m_vue->vue();
  const QString bas = chemin.toLower();
  try {
    if (bas.endsWith(QStringLiteral(".pdf"))) {
      OptionsTypon o;
      o.couche = Couche::CuivreDessus;
      o.trouCentrage = 0;
      return AtelierElectronique::ecrireTypon(chemin, modeleTyponPlatine(v, o), o, c->nom + " — platine d'essai");
    }
    std::string texte;
    if (bas.endsWith(QStringLiteral(".plt")) || bas.endsWith(QStringLiteral(".hpgl")))
      texte = hpglPlaque(v);
    else if (bas.endsWith(QStringLiteral("coupures.svg")))
      texte = svgCoupures(v);
    else if (bas.endsWith(QStringLiteral(".svg")))
      texte = svgPlatine(*c, v);
    else
      texte = planDeChargement(*c, v);
    prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), texte);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool FenetrePlatine::analyserGuitare(const QString& selecteur, const QString& sortie) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  m_guitare = prom::ecad::analyserGuitare(*c, selecteur.toStdString(), sortie.toStdString());
  m_tableGuitare->setRowCount(static_cast<int>(m_guitare.size()));
  for (int i = 0; i < static_cast<int>(m_guitare.size()); ++i) {
    const auto& p = m_guitare[static_cast<std::size_t>(i)];
    m_tableGuitare->setItem(i, 0, new QTableWidgetItem(QString::number(p.position)));
    m_tableGuitare->setItem(i, 1, new QTableWidgetItem(q(p.texte)));
    m_tableGuitare->setItem(i, 2, new QTableWidgetItem(q(p.couplage)));
    m_tableGuitare->setItem(i, 3, new QTableWidgetItem(p.horsPhase ? QStringLiteral("hors phase") : QStringLiteral("en phase")));
  }
  m_etat->setText(m_guitare.empty() ? QStringLiteral("Sélecteur « %1 » ou sortie « %2 » introuvable.").arg(selecteur, sortie)
                                    : QStringLiteral("%1 positions parcourues.").arg(m_guitare.size()));
  return !m_guitare.empty();
}

// ======================================================================================================================
// Atelier
// ======================================================================================================================
FenetrePlatine* AtelierElectronique::platine(const QString& onglet) {
  if (!m_platine) m_platine = new FenetrePlatine(*this, this);
  m_platine->montrer(onglet);
  return m_platine;
}

FenetreLaboratoire* AtelierElectronique::laboratoireOuvert() const { return m_laboratoire.data(); }

void AtelierElectronique::enregistrerCommandesL16() {
  auto& c = m_commandes;
  c["platine.ouvrir"] = [this] { platine(QStringLiteral("platine")); };
  c["platine.monter"] = [this] { platine(QStringLiteral("platine"))->monter(); };
  c["platine.etape"] = [this] { platine(QStringLiteral("platine"))->etapeSuivante(); };
  c["platine.verifier"] = [this] { platine(QStringLiteral("platine"))->verifier(); };
  c["platine.3d"] = [this] { platine(QStringLiteral("3d")); };
  c["platine.documents"] = [this] { platine(QStringLiteral("documents")); };
  c["platine.guitare"] = [this] { platine(QStringLiteral("guitare")); };
}

// Prométhée : vue de dessus de la carte dans son boîtier. Port de la vue Carte du prototype.
// Licence GPL-3.0-only.
#include "VueCarte.hpp"

#include "Theme.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include "promethee/geometrie.hpp"

using namespace prom;

namespace {
// Encres du plan prises au thème (prendreEncres) ; les couleurs de la carte elle-même restent physiques
QColor PLAN, GRILLE, GRILLE_FORTE, PAROI, FOND, TRAIT, ENCRE, ENCRE2, BULLE;
void prendreEncres() {
  const EncresCarte& e = theme().carte();
  PLAN = e.plan, GRILLE = e.grille, GRILLE_FORTE = e.grilleForte, PAROI = e.paroi, FOND = e.fond, TRAIT = e.trait, ENCRE = e.encre, ENCRE2 = e.encre2;
  BULLE = e.bulle;
}
const QColor MASQUE(0x2E, 0x6A, 0x50), MASQUE_BORD(0x1E, 0x4C, 0x38), SERIGRAPHIE(0xF4, 0xF1, 0xE6), CUIVRE(0xB4, 0x65, 0x2B),
    PASTILLE(0xC9, 0x8A, 0x45), SELECTION(0x2F, 0x6B, 0xF5), ERREUR(0xC2, 0x40, 0x2E), ALERTE(0xA8, 0x74, 0x10),
    COTE_CARTE(0x2A, 0x62, 0x49);

double borne(double v, double a, double b) { return std::min(b, std::max(a, v)); }

QColor couleurLed(const std::string& valeur) {
  std::string s = valeur;
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const std::pair<const char*, QColor> table[] = {{"rouge", QColor(0xD2, 0x3A, 0x2C)}, {"vert", QColor(0x2E, 0x9F, 0x58)}, {"bleu", QColor(0x2F, 0x6B, 0xD0)},
                                                  {"jaune", QColor(0xE2, 0xB2, 0x2E)}, {"orange", QColor(0xE0, 0x7B, 0x26)}, {"blanc", QColor(0xEE, 0xEE, 0xE6)}};
  for (const auto& [mot, c] : table) if (s.find(mot) != std::string::npos) return c;
  return QColor(0xD2, 0x3A, 0x2C);
}

double magnetiser(double v) { return std::round(v * 2) / 2; }
}  // namespace

VueCarte::VueCarte(Document* doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMinimumSize(240, 200);
  connect(doc, &Document::change, this, [this] {
    if (!m_doc->enGeste() && m_geste == Geste::Aucun) garderEnVue();
    update();
  });
  connect(doc, &Document::selectionChangee, this, qOverload<>(&QWidget::update));
}

void VueCarte::recadrer() {
  if (width() < 10 || height() < 10) return;
  const Derive& d = m_doc->derive();
  const double haut = 16, bas = 16, cote = 24;
  m_s = borne(std::min((width() - 28.0) / (d.Lo + cote), (height() - haut - bas) / (d.Wo + cote)), 0.4, 80);
  m_ox = width() / 2.0 - (d.cx - 5) * m_s;
  m_oy = (haut + height() - bas) / 2.0 + (d.cy - 5) * m_s;
  m_cadre = true;
  update();
}

void VueCarte::setFaceArriere(bool dessous) {
  m_dessous = dessous;
  m_survol = {};
  update();
  emit faceChangee(dessous);
}

void VueCarte::garderEnVue() {
  if (!m_cadre) return;
  const Derive& d = m_doc->derive();
  const QPointF hg = ecran(d.cx - d.Lo / 2, d.cy + d.Wo / 2), bd = ecran(d.cx + d.Lo / 2, d.cy - d.Wo / 2);
  if (hg.x() - 46 < 0 || hg.y() - 6 < 0 || bd.x() + 6 > width() || bd.y() + 46 > height()) recadrer();
}

void VueCarte::resizeEvent(QResizeEvent*) {
  if (!m_cadre) recadrer();
}

QPainterPath VueCarte::chemin(const std::vector<Point>& poly) const {
  QPainterPath ch;
  for (size_t i = 0; i < poly.size(); ++i) {
    if (i == 0) ch.moveTo(ecran(poly[i].x, poly[i].y));
    else ch.lineTo(ecran(poly[i].x, poly[i].y));
  }
  ch.closeSubpath();
  return ch;
}

QPainterPath VueCarte::cheminCarte() const {
  QPainterPath ch;
  ch.setFillRule(Qt::OddEvenFill);
  for (const auto& k : m_doc->projet().carte.contours) ch.addPath(chemin(k.polygone(0.2)));
  return ch;
}

void VueCarte::actualiserParois() {
  const Projet& p = m_doc->projet();
  if (!p.carte.libre()) { m_cleParois.clear(); return; }
  std::string cle = std::to_string(p.boitier.jeu) + "|" + std::to_string(p.boitier.paroi);
  for (const auto& k : p.carte.contours)
    for (const auto& e : k.elements) cle += "|" + std::to_string(e.debut.x) + "," + std::to_string(e.debut.y);
  for (const auto& r : m_doc->derive().debords) cle += "|d" + std::to_string(r.x1) + "," + std::to_string(r.y1) + "," + std::to_string(r.x2) + "," + std::to_string(r.y2);
  if (cle == m_cleParois) return;
  // Pendant un glissement, on garde les parois précédentes : le calcul exact reprend au lâcher.
  if (m_doc->enGeste() && !m_paroiInt.empty()) return;
  m_cleParois = cle;
  try {
    m_paroiInt = contourDecale(p, m_doc->derive(), p.boitier.jeu);
    m_paroiExt = contourDecale(p, m_doc->derive(), p.boitier.jeu + p.boitier.paroi);
  } catch (const std::exception&) {
    m_paroiInt.clear();
    m_paroiExt.clear();
  }
}

void VueCarte::paintEvent(QPaintEvent*) {
  prendreEncres();
  if (!m_cadre) recadrer();
  actualiserParois();
  QPainter g(this);
  g.setRenderHint(QPainter::Antialiasing);
  g.fillRect(rect(), PLAN);
  const Projet& p = m_doc->projet();
  const Derive& d = m_doc->derive();
  const Carte& c = p.carte;
  const Cible& sel = m_doc->selection();
  auto rr = [this](double cx, double cy, double hx, double hy, double r) {
    QPainterPath chemin;
    const QPointF a = ecran(cx - hx, cy + hy), b = ecran(cx + hx, cy - hy);
    const double R = std::max(0.0, std::min({r * m_s, (b.x() - a.x()) / 2, (b.y() - a.y()) / 2}));
    chemin.addRoundedRect(QRectF(a, b), R, R);
    return chemin;
  };
  auto cercle = [this](double x, double y, double r) {
    QPainterPath chemin;
    chemin.addEllipse(ecran(x, y), std::max(0.5, r * m_s), std::max(0.5, r * m_s));
    return chemin;
  };

  // Grille millimétrique
  const double pas = m_s >= 7 ? 1 : m_s >= 2.2 ? 5 : 10;
  const QPointF hg = monde(QPointF(0, 0)), bd = monde(QPointF(width(), height()));
  for (int passe = 0; passe < 2; ++passe) {
    g.setPen(QPen(passe ? GRILLE_FORTE : GRILLE, 1));
    for (double x = std::floor(hg.x() / pas) * pas; x <= bd.x(); x += pas) {
      const bool fort = std::fmod(std::abs(x) + 1e-6, 10.0) < 1e-3;
      if (fort != (passe == 1)) continue;
      const double X = std::round(ecran(x, 0).x()) + 0.5;
      g.drawLine(QPointF(X, 0), QPointF(X, height()));
    }
    for (double y = std::floor(bd.y() / pas) * pas; y <= hg.y(); y += pas) {
      const bool fort = std::fmod(std::abs(y) + 1e-6, 10.0) < 1e-3;
      if (fort != (passe == 1)) continue;
      const double Y = std::round(ecran(0, y).y()) + 0.5;
      g.drawLine(QPointF(0, Y), QPointF(width(), Y));
    }
  }

  // Boîtier : parois, fond, découpes, piliers
  g.setPen(QPen(TRAIT, 1));
  g.setBrush(PAROI);
  g.drawPath(c.libre() ? chemin(m_paroiExt) : rr(d.cx, d.cy, d.Lo / 2, d.Wo / 2, d.ro));
  g.setBrush(FOND);
  g.drawPath(c.libre() ? chemin(m_paroiInt) : rr(d.cx, d.cy, d.Li / 2, d.Wi / 2, d.ri));
  auto estFocus = [&](const std::string& id) { return sel.id == id || m_survol.id == id; };
  for (const auto& gc : d.comps) {
    if (!gc.decoupe) continue;
    const auto& o = *gc.decoupe;
    double x1, x2, y1, y2;
    if (horizontal(o.mur)) {
      const double s = o.mur == Bord::N ? 1 : -1;
      x1 = o.u - o.w / 2; x2 = o.u + o.w / 2; y1 = d.bordCarte(o.mur, o.u) + s * p.boitier.jeu; y2 = y1 + s * p.boitier.paroi;
    } else {
      const double s = o.mur == Bord::E ? 1 : -1;
      y1 = o.u - o.w / 2; y2 = o.u + o.w / 2; x1 = d.bordCarte(o.mur, o.u) + s * p.boitier.jeu; x2 = x1 + s * p.boitier.paroi;
    }
    const QRectF r = QRectF(ecran(std::min(x1, x2), std::max(y1, y2)), ecran(std::max(x1, x2), std::min(y1, y2)));
    QColor fond = CUIVRE;
    fond.setAlphaF(estFocus(gc.id) ? 0.7 : 0.28);
    g.fillRect(r, fond);
    g.setPen(QPen(CUIVRE, estFocus(gc.id) ? 1.6 : 1, Qt::DashLine));
    g.setBrush(Qt::NoBrush);
    g.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
  }
  g.setPen(QPen(TRAIT, 1));
  g.setBrush(PAROI);
  for (const auto& t : d.trous) g.drawPath(cercle(t.x, t.y, t.Rb));

  // Carte
  g.setPen(QPen(MASQUE_BORD, 1.2));
  g.setBrush(MASQUE);
  g.drawPath(c.libre() ? cheminCarte() : rr(c.x0, c.y0, c.L / 2, c.W / 2, c.r));
  g.setPen(QPen(QColor(244, 241, 230, 130), 1, Qt::DashLine));
  g.setBrush(Qt::NoBrush);
  for (const auto& t : d.trous) g.drawPath(cercle(t.x, t.y, t.Rb));

  // Composants : ceux de l'autre face d'abord, estompés ; ceux de la face montrée par-dessus.
  for (int passe = 0; passe < 2; ++passe)
  for (const auto& gc : d.comps) {
    const bool actif = gc.dessous == m_dessous;
    if ((passe == 1) != actif) continue;
    const Composant* k = nullptr;
    for (const auto& q : p.composants) if (q.id == gc.id) k = &q;
    if (!k) continue;
    g.setOpacity(actif ? 1.0 : 0.18);
    const TypeComposant& T = *typeComposant(k->type);
    const double lx = (T.bord ? k->d : k->w) * m_s, ly = (T.bord ? k->w : k->d) * m_s, s = m_s;
    g.save();
    g.translate(ecran(gc.x, gc.y));
    g.rotate(-gc.rot);
    auto rect = [&](double x, double y, double w, double h, const QColor& rempli, std::optional<QColor> trait = std::nullopt) {
      const QRectF r(x, -y - h, w, h);
      g.fillRect(r, rempli);
      if (trait) { g.setPen(QPen(*trait, 1)); g.setBrush(Qt::NoBrush); g.drawRect(r); }
    };
    auto disque = [&](double x, double y, double r, const QColor& rempli, std::optional<QColor> trait = std::nullopt) {
      g.setPen(trait ? QPen(*trait, 1) : Qt::NoPen);
      g.setBrush(rempli);
      g.drawEllipse(QPointF(x, -y), std::max(0.5, r), std::max(0.5, r));
    };
    const std::string& type = k->type;
    if (type == "module") {
      rect(-lx / 2, -ly / 2, lx, ly, QColor(0x1E, 0x3A, 0x55), QColor(0x0F, 0x22, 0x35));
      rect(-lx / 2 + 0.7 * s, -ly / 2 + 0.5 * s, lx - 1.4 * s, ly * 0.72, QColor(0xB8, 0xBF, 0xC5), QColor(0x8E, 0x97, 0x9E));
    } else if (type == "ic") {
      rect(-lx / 2, -ly / 2, lx, ly, QColor(0x20, 0x23, 0x25), QColor(0x0E, 0x10, 0x11));
      disque(-lx / 2 + 0.9 * s, ly / 2 - 0.9 * s, 0.35 * s, QColor(0x7A, 0x80, 0x86));
    } else if (type == "capteur") {
      rect(-lx / 2, -ly / 2, lx, ly, QColor(0x2A, 0x2D, 0x30));
      rect(-lx * 0.4, -ly * 0.4, lx * 0.8, ly * 0.8, QColor(0xAE, 0xB4, 0xB9));
    } else if (type == "usbc") {
      rect(-lx / 2, -ly / 2, lx, ly, QColor(0xC2, 0xC7, 0xCB), QColor(0x80, 0x88, 0x8F));
      rect(lx / 2 - 1.0 * s, -ly / 2 + 0.8 * s, 1.0 * s, ly - 1.6 * s, QColor(0x3B, 0x40, 0x45));
    } else if (type == "jack") {
      rect(-lx / 2, -ly / 2, lx, ly, QColor(0x26, 0x29, 0x2B), QColor(0x0D, 0x0E, 0x0F));
      rect(lx / 2 - 1.4 * s, -ly * 0.3, 1.4 * s, ly * 0.6, QColor(0x4C, 0x50, 0x54));
    } else if (type == "jst") {
      rect(-lx / 2, -ly / 2, lx, ly, QColor(0xE8, 0xDF, 0xC8), QColor(0xA8, 0x9C, 0x7C));
      rect(-lx / 2 + 0.6 * s, -ly / 2 + 0.6 * s, lx - 1.2 * s, ly - 1.4 * s, QColor(0xD3, 0xC7, 0xA6));
    } else if (type == "led") {
      const double r = std::min(lx, ly) / 2;
      disque(0, 0, r, couleurLed(k->valeur), QColor(0, 0, 0, 90));
      disque(r * 0.25, r * 0.28, r * 0.25, QColor(255, 255, 255, 140));
    } else if (type == "bouton") {
      rect(-lx / 2, -ly / 2, lx, ly, QColor(0x2B, 0x2D, 0x2F), QColor(0x11, 0x11, 0x11));
      disque(0, 0, 1.75 * s, QColor(0x6A, 0x6E, 0x72), QColor(0x8D, 0x91, 0x95));
    } else if (type == "condo") {
      const double r = std::min(lx, ly) / 2;
      disque(0, 0, r, QColor(0x22, 0x31, 0x5A), QColor(0x12, 0x1B, 0x33));
      g.setPen(QPen(QColor(220, 226, 232, 190), 1));
      g.drawLine(QPointF(-r * 0.3, 0), QPointF(r * 0.55, 0));
      g.drawLine(QPointF(r * 0.12, -r * 0.42), QPointF(r * 0.12, r * 0.42));
    } else {
      rect(-lx / 2, -ly / 2, lx, ly, QColor(0x44, 0x44, 0x44));
    }
    g.restore();
    if (m_s >= 2.2) {
      QFont f = font();
      f.setPixelSize(static_cast<int>(borne(1.7 * m_s, 9, 13)));
      f.setBold(true);
      g.setFont(f);
      g.setPen(SERIGRAPHIE);
      const QPointF pos = ecran(gc.x, gc.y2);
      g.drawText(QRectF(pos.x() - 40, pos.y() - f.pixelSize() * 1.7, 80, f.pixelSize() * 1.4), Qt::AlignCenter, QString::fromStdString(gc.ref));
    }
    g.setOpacity(1.0);
  }
  if (m_dessous) {
    QFont f = font();
    f.setPixelSize(13);
    f.setBold(true);
    g.setFont(f);
    const QString texte = QStringLiteral("Face arrière, vue depuis le dessus · touche B");
    const QRectF bandeau(10, 8, QFontMetricsF(f).horizontalAdvance(texte) + 20, 24);
    g.fillRect(bandeau, QColor(0x2F, 0x6B, 0xF5, 230));
    g.setPen(Qt::white);
    g.drawText(bandeau, Qt::AlignCenter, texte);
  }

  // Trous de fixation, dessinés au-dessus des composants pour qu'un conflit reste visible
  for (const auto& t : d.trous) {
    g.setPen(Qt::NoPen);
    g.setBrush(PASTILLE);
    g.drawPath(cercle(t.x, t.y, t.vis.tete / 2));
    g.setBrush(PAROI);
    g.drawPath(cercle(t.x, t.y, t.vis.trou / 2));
    g.setBrush(QColor(0, 0, 0, 107));
    g.drawPath(cercle(t.x, t.y, t.rp));
  }

  // Perçages du couvercle
  for (const auto& gc : d.comps) {
    if (!gc.trouCouvercle) continue;
    g.setPen(QPen(CUIVRE, estFocus(gc.id) ? 2 : 1.2, estFocus(gc.id) ? Qt::SolidLine : Qt::DashLine));
    g.setBrush(Qt::NoBrush);
    g.drawPath(cercle(gc.trouCouvercle->x, gc.trouCouvercle->y, gc.trouCouvercle->d / 2));
  }

  // Éléments en erreur ou en alerte
  std::map<std::string, Severite> niveaux;
  for (const auto& q : m_doc->problemes())
    for (const auto& id : q.ids)
      if (!niveaux.count(id) || q.sev == Severite::Erreur) niveaux[id] = q.sev;
  for (const auto& [id, sev] : niveaux) {
    if (sel.id == id) continue;
    g.setPen(QPen(sev == Severite::Erreur ? ERREUR : ALERTE, 1.6));
    g.setBrush(Qt::NoBrush);
    bool trouve = false;
    for (const auto& gc : d.comps)
      if (gc.id == id) { g.drawRect(QRectF(ecran(gc.x1, gc.y2), ecran(gc.x2, gc.y1)).adjusted(-2, -2, 2, 2)); trouve = true; }
    if (!trouve)
      for (const auto& t : d.trous) if (t.id == id) g.drawPath(cercle(t.x, t.y, t.Rb + 1.5 / m_s));
  }

  // Cotes de la carte et du boîtier
  auto cote = [&](QPointF a, QPointF b, const QString& texte, const QColor& coul) {
    g.setPen(QPen(coul, 1));
    g.drawLine(a, b);
    const double ang = std::atan2(b.y() - a.y(), b.x() - a.x());
    auto fleche = [&](QPointF pt, double an) {
      QPolygonF tri;
      tri << pt << pt - QPointF(7 * std::cos(an - 0.38), 7 * std::sin(an - 0.38)) << pt - QPointF(7 * std::cos(an + 0.38), 7 * std::sin(an + 0.38));
      g.setBrush(coul);
      g.setPen(Qt::NoPen);
      g.drawPolygon(tri);
      g.setPen(QPen(coul, 1));
    };
    fleche(b, ang);
    fleche(a, ang + M_PI);
    g.save();
    g.translate((a + b) / 2);
    double an = ang * 180 / M_PI;
    if (an > 90 || an < -90) an += 180;
    g.rotate(an);
    QFont f = font();
    f.setPixelSize(12);
    f.setBold(true);
    g.setFont(f);
    g.drawText(QRectF(-40, -18, 80, 15), Qt::AlignCenter, texte);
    g.restore();
  };
  {
    const double yb = ecran(0, d.cy - d.Wo / 2).y(), xg = ecran(d.cx - d.Lo / 2, 0).x();
    g.setPen(QPen(ENCRE2, 1));
    const double Y1 = yb + 18, Y2 = yb + 38, X1 = xg - 18, X2 = xg - 38;
    for (double x : {c.x0 - c.L / 2, c.x0 + c.L / 2}) g.drawLine(QPointF(ecran(x, 0).x(), ecran(0, c.y0 - c.W / 2).y() + 3), QPointF(ecran(x, 0).x(), Y1 + 4));
    for (double x : {d.cx - d.Lo / 2, d.cx + d.Lo / 2}) g.drawLine(QPointF(ecran(x, 0).x(), yb + 3), QPointF(ecran(x, 0).x(), Y2 + 4));
    for (double y : {c.y0 - c.W / 2, c.y0 + c.W / 2}) g.drawLine(QPointF(ecran(c.x0 - c.L / 2, 0).x() - 3, ecran(0, y).y()), QPointF(X1 - 4, ecran(0, y).y()));
    for (double y : {d.cy - d.Wo / 2, d.cy + d.Wo / 2}) g.drawLine(QPointF(xg - 3, ecran(0, y).y()), QPointF(X2 - 4, ecran(0, y).y()));
    const auto txt = [](double v) { return QString::fromStdString(fmt(v, 2)); };
    cote(QPointF(ecran(c.x0 - c.L / 2, 0).x(), Y1), QPointF(ecran(c.x0 + c.L / 2, 0).x(), Y1), txt(c.L), COTE_CARTE);
    cote(QPointF(ecran(d.cx - d.Lo / 2, 0).x(), Y2), QPointF(ecran(d.cx + d.Lo / 2, 0).x(), Y2), txt(d.Lo), ENCRE2);
    cote(QPointF(X1, ecran(0, c.y0 - c.W / 2).y()), QPointF(X1, ecran(0, c.y0 + c.W / 2).y()), txt(c.W), COTE_CARTE);
    cote(QPointF(X2, ecran(0, d.cy - d.Wo / 2).y()), QPointF(X2, ecran(0, d.cy + d.Wo / 2).y()), txt(d.Wo), ENCRE2);
  }

  // Survol et sélection
  // Sélection en bleu franc (fond teinté, contour épais, étiquette) ; survol en bleu clair.
  auto etiquette = [&](const QPointF& ancre, const QString& texte) {
    QFont f = font();
    f.setPixelSize(12);
    f.setBold(true);
    g.setFont(f);
    const double l = QFontMetricsF(f).horizontalAdvance(texte) + 14;
    const QRectF r(ancre.x() - l / 2, ancre.y() - 26, l, 20);
    g.setPen(Qt::NoPen);
    g.setBrush(SELECTION);
    g.drawRoundedRect(r, 5, 5);
    g.setPen(Qt::white);
    g.drawText(r, Qt::AlignCenter, texte);
  };
  auto contour = [&](const Cible& cb, bool fort) {
    QColor fond = SELECTION;
    fond.setAlpha(fort ? 60 : 25);
    g.setPen(QPen(fort ? SELECTION : QColor(0x2F, 0x6B, 0xF5, 150), fort ? 3 : 1.6));
    g.setBrush(Qt::NoBrush);
    if (cb.type == Cible::Composant) {
      for (const auto& gc : d.comps)
        if (gc.id == cb.id) {
          const QRectF r = QRectF(ecran(gc.x1, gc.y2), ecran(gc.x2, gc.y1)).adjusted(-3, -3, 3, 3);
          g.fillRect(r, fond);
          g.drawRect(r);
          if (fort) etiquette(QPointF(r.center().x(), r.top()), QString::fromStdString(gc.ref) + (gc.dessous ? QStringLiteral(" · dessous") : QString()));
        }
    } else if (cb.type == Cible::Trou) {
      for (const auto& t : d.trous)
        if (t.id == cb.id) {
          g.setBrush(fond);
          g.drawPath(cercle(t.x, t.y, t.Rb + 2.5 / m_s));
          if (fort) etiquette(ecran(t.x, t.y + t.Rb + 2.5 / m_s), QString::fromStdString(t.ref));
        }
    } else if (cb.type == Cible::Carte) {
      if (c.libre()) { g.setPen(QPen(SELECTION, 3)); g.drawPath(cheminCarte()); }
      else g.drawPath(rr(c.x0, c.y0, c.L / 2 + 2.5 / m_s, c.W / 2 + 2.5 / m_s, c.r + 2.5 / m_s));
    }
  };
  if (m_survol.type == Touche::Composant && m_survol.id != sel.id) contour({Cible::Composant, m_survol.id}, false);
  if (m_survol.type == Touche::Trou && m_survol.id != sel.id) contour({Cible::Trou, m_survol.id}, false);
  contour(sel, true);
  // Contraintes : lignes de rappel entre l'élément sélectionné et son repère d'ancrage.
  auto rappel = [&](QPointF a, QPointF b, const QString& texte) {
    g.setPen(QPen(CUIVRE, 1.2, Qt::DashLine));
    g.drawLine(a, b);
    if (texte.isEmpty()) return;
    QFont f = font();
    f.setPixelSize(11);
    f.setBold(true);
    g.setFont(f);
    const QRectF r(QPointF((a.x() + b.x()) / 2 - 28, (a.y() + b.y()) / 2 - 9), QSizeF(56, 16));
    g.fillRect(r, BULLE);
    g.setPen(CUIVRE);
    g.drawText(r, Qt::AlignCenter, texte);
  };
  auto rappelsCoin = [&](double cx, double cy, double x, double y) {
    rappel(ecran(cx, cy), ecran(x, cy), QString::fromStdString(fmt(std::abs(x - cx), 2)));
    rappel(ecran(x, cy), ecran(x, y), QString::fromStdString(fmt(std::abs(y - cy), 2)));
    g.setBrush(CUIVRE);
    g.setPen(Qt::NoPen);
    g.drawEllipse(ecran(cx, cy), 3.5, 3.5);
  };
  if (sel.type == Cible::Composant) {
    for (const auto& k : p.composants) {
      if (k.id != sel.id || k.ancrage == "libre") continue;
      if (k.bord) {
        const bool hz = horizontal(*k.bord);
        const double centre = hz ? c.x0 : c.y0, demi = (hz ? c.L : c.W) / 2, u = leLongComposant(p, k);
        const double ref = k.ancrage == "debut" ? centre - demi : k.ancrage == "fin" ? centre + demi : centre;
        const double fixe = hz ? (*k.bord == Bord::N ? c.y0 + c.W / 2 : c.y0 - c.W / 2) : (*k.bord == Bord::E ? c.x0 + c.L / 2 : c.x0 - c.L / 2);
        const QPointF a = hz ? ecran(ref, fixe) : ecran(fixe, ref), b = hz ? ecran(u, fixe) : ecran(fixe, u);
        rappel(a, b, QString::fromStdString(fmt(std::abs(u - ref), 2)));
      } else {
        const Point q = posComposant(p, k);
        if (k.ancrage == "centre") rappelsCoin(c.x0, c.y0, q.x, q.y);
        else rappelsCoin(c.x0 + (k.ancrage[1] == 'E' ? 1 : -1) * c.L / 2, c.y0 + (k.ancrage[0] == 'N' ? 1 : -1) * c.W / 2, q.x, q.y);
      }
    }
  } else if (sel.type == Cible::Trou) {
    for (const auto& t : p.trous) {
      if (t.id != sel.id || !t.coin) continue;
      const Point q = posTrou(p, t);
      rappelsCoin(c.x0 + ((*t.coin)[1] == 'E' ? 1 : -1) * c.L / 2, c.y0 + ((*t.coin)[0] == 'N' ? 1 : -1) * c.W / 2, q.x, q.y);
    }
  }
  // Cadenas sur les composants verrouillés
  for (const auto& k : p.composants) {
    if (!k.verrou) continue;
    for (const auto& gc : d.comps) {
      if (gc.id != k.id) continue;
      const QPointF hd = ecran(gc.x2, gc.y2) + QPointF(2, -2);
      g.setPen(QPen(ENCRE, 1.4));
      g.setBrush(Qt::NoBrush);
      g.drawArc(QRectF(hd.x() - 1, hd.y() - 9, 8, 8), 0, 180 * 16);
      g.setBrush(ENCRE);
      g.drawRoundedRect(QRectF(hd.x() - 2, hd.y() - 5, 10, 7), 1.5, 1.5);
    }
  }
  if (sel.type == Cible::Carte && !c.libre()) {
    for (const auto& [bord, x, y] : {std::tuple{'E', c.x0 + c.L / 2, c.y0}, std::tuple{'W', c.x0 - c.L / 2, c.y0}, std::tuple{'N', c.x0, c.y0 + c.W / 2},
                                     std::tuple{'S', c.x0, c.y0 - c.W / 2}}) {
      (void)bord;
      g.setPen(QPen(CUIVRE, 2));
      g.setBrush(Qt::white);
      g.drawEllipse(ecran(x, y), 6.5, 6.5);
    }
  }
}

VueCarte::Touche VueCarte::toucher(const QPointF& pt) const {
  const QPointF w = monde(pt);
  const double tol = 6 / m_s;
  const Projet& p = m_doc->projet();
  const Derive& d = m_doc->derive();
  const Carte& c = p.carte;
  if (m_doc->selection().type == Cible::Carte && !c.libre()) {
    for (const auto& [bord, x, y] : {std::tuple{'E', c.x0 + c.L / 2, c.y0}, std::tuple{'W', c.x0 - c.L / 2, c.y0}, std::tuple{'N', c.x0, c.y0 + c.W / 2},
                                     std::tuple{'S', c.x0, c.y0 - c.W / 2}})
      if (std::hypot(x - w.x(), y - w.y()) <= tol * 1.4) return {Touche::Poignee, "", bord};
  }
  for (auto it = d.trous.rbegin(); it != d.trous.rend(); ++it)
    if (std::hypot(it->x - w.x(), it->y - w.y()) <= std::max(it->vis.tete / 2, 1.5) + tol * 0.5) return {Touche::Trou, it->id};
  const GeoComp* meilleur = nullptr;
  double aire = 1e18;
  for (const auto& gc : d.comps) {
    if (gc.dessous != m_dessous) continue;  // seule la face montrée se sélectionne
    const double m = tol * 0.5;
    if (w.x() >= gc.x1 - m && w.x() <= gc.x2 + m && w.y() >= gc.y1 - m && w.y() <= gc.y2 + m) {
      const double a = (gc.x2 - gc.x1) * (gc.y2 - gc.y1);
      if (a < aire) { aire = a; meilleur = &gc; }
    }
  }
  if (meilleur) return {Touche::Composant, meilleur->id};
  if (d.sdCarte(w.x(), w.y()) <= 0) return {Touche::Carte, "carte"};
  return {};
}

QPointF VueCarte::positionObjet(const Touche& t) const {
  const Derive& d = m_doc->derive();
  if (t.type == Touche::Composant)
    for (const auto& gc : d.comps) if (gc.id == t.id) return {gc.x, gc.y};
  if (t.type == Touche::Trou)
    for (const auto& tr : d.trous) if (tr.id == t.id) return {tr.x, tr.y};
  return {};
}

void VueCarte::deplacer(Projet& p, const Touche& t, double x, double y, bool fin) const {
  x = magnetiser(x);
  y = magnetiser(y);
  if (t.type == Touche::Composant) {
    for (auto& k : p.composants)
      if (k.id == t.id) {
        placerComposant(p, k, x, y);
      }
  } else if (t.type == Touche::Trou) {
    for (auto& tr : p.trous)
      if (tr.id == t.id) {
        if (fin) ancrerTrou(p, tr, x, y);
        else { tr.coin.reset(); tr.x = x; tr.y = y; }
      }
  }
}

void VueCarte::redimensionner(Projet& p, char bord, double delta, const Carte& dep) const {
  Carte& c = p.carte;
  const bool hz = bord == 'E' || bord == 'W';
  const double depart = hz ? dep.L : dep.W;
  const double n = borne(magnetiser(depart + delta), 10, 300), dl = n - depart;
  if (hz) { c.L = n; c.x0 = dep.x0 + (bord == 'E' ? dl / 2 : -dl / 2); c.y0 = dep.y0; }
  else { c.W = n; c.y0 = dep.y0 + (bord == 'N' ? dl / 2 : -dl / 2); c.x0 = dep.x0; }
  c.r = std::min(dep.r, std::min(c.L, c.W) / 2 - 0.5);
  contraindreBords(p);
}

void VueCarte::mousePressEvent(QMouseEvent* e) {
  setFocus();
  m_depart = e->position();
  m_departMonde = monde(e->position());
  m_bouge = false;
  if (e->button() != Qt::LeftButton) {
    m_geste = Geste::Deplacement;
    m_cible = {};
    m_cibleMenu = e->button() == Qt::RightButton ? toucher(e->position()) : Touche{};
    m_oxDepart = m_ox;
    m_oyDepart = m_oy;
    return;
  }
  m_cible = toucher(e->position());
  if (m_cible.type == Touche::Composant && verrouille(m_cible.id)) {
    m_doc->selectionner({Cible::Composant, m_cible.id});
    m_geste = Geste::Aucun;
    return;
  }
  if (m_cible.type == Touche::Composant || m_cible.type == Touche::Trou) {
    m_doc->selectionner({m_cible.type == Touche::Composant ? Cible::Composant : Cible::Trou, m_cible.id});
    m_origineObjet = positionObjet(m_cible);
    m_geste = Geste::Objet;
    m_doc->debutGeste();
  } else if (m_cible.type == Touche::Poignee) {
    m_carteDepart = m_doc->projet().carte;
    m_geste = Geste::Poignee;
    m_doc->debutGeste();
  } else {
    m_geste = Geste::Deplacement;
    m_oxDepart = m_ox;
    m_oyDepart = m_oy;
  }
}

void VueCarte::mouseMoveEvent(QMouseEvent* e) {
  if (m_geste == Geste::Aucun) {
    const Touche t = toucher(e->position());
    if (t.type != m_survol.type || t.id != m_survol.id) { m_survol = t; update(); }
    setCursor(t.type == Touche::Poignee ? (t.bord == 'E' || t.bord == 'W' ? Qt::SizeHorCursor : Qt::SizeVerCursor)
              : (t.type == Touche::Composant && verrouille(t.id)) ? Qt::ForbiddenCursor
              : (t.type == Touche::Composant || t.type == Touche::Trou) ? Qt::SizeAllCursor : Qt::ArrowCursor);
    return;
  }
  if (!m_bouge && (e->position() - m_depart).manhattanLength() < 3) return;
  m_bouge = true;
  const QPointF w = monde(e->position());
  if (m_geste == Geste::Objet) {
    const Touche cible = m_cible;
    const double x = m_origineObjet.x() + w.x() - m_departMonde.x(), y = m_origineObjet.y() + w.y() - m_departMonde.y();
    m_doc->pendantGeste([&](Projet& p) { deplacer(p, cible, x, y, false); });
  } else if (m_geste == Geste::Poignee) {
    const char b = m_cible.bord;
    const double delta = b == 'E' ? w.x() - m_departMonde.x() : b == 'W' ? m_departMonde.x() - w.x() : b == 'N' ? w.y() - m_departMonde.y() : m_departMonde.y() - w.y();
    const Carte dep = m_carteDepart;
    m_doc->pendantGeste([&](Projet& p) { redimensionner(p, b, delta, dep); });
  } else if (m_geste == Geste::Deplacement) {
    m_ox = m_oxDepart + e->position().x() - m_depart.x();
    m_oy = m_oyDepart + e->position().y() - m_depart.y();
    update();
  }
}

void VueCarte::mouseReleaseEvent(QMouseEvent* e) {
  const Geste geste = m_geste;
  m_geste = Geste::Aucun;
  if (e->button() == Qt::RightButton && !m_bouge) {
    menuContextuel(e->globalPosition().toPoint());
    return;
  }
  if (geste == Geste::Objet) {
    if (m_bouge && m_cible.type == Touche::Trou) {
      const QPointF pos = positionObjet(m_cible);
      const Touche cible = m_cible;
      m_doc->pendantGeste([&](Projet& p) { deplacer(p, cible, pos.x(), pos.y(), true); });
    }
    m_doc->finGeste();
  } else if (geste == Geste::Poignee) {
    m_doc->finGeste();
  } else if (geste == Geste::Deplacement && !m_bouge) {
    m_doc->selectionner(m_cible.type == Touche::Carte ? Cible{Cible::Carte, "carte"} : Cible{});
  }
}

void VueCarte::mouseDoubleClickEvent(QMouseEvent* e) {
  const Touche t = toucher(e->position());
  if (t.type != Touche::Composant || verrouille(t.id)) return;
  m_doc->modifier([&](Projet& p) {
    for (auto& k : p.composants) if (k.id == t.id && !k.bord) k.rot = (k.rot + 90) % 360;
  });
}

void VueCarte::wheelEvent(QWheelEvent* e) {
  const QPointF pt = e->position(), w = monde(pt);
  m_s = borne(m_s * std::exp(e->angleDelta().y() / 120.0 * 0.15), 0.4, 80);
  m_ox = pt.x() - w.x() * m_s;
  m_oy = pt.y() + w.y() * m_s;
  update();
}

void VueCarte::keyPressEvent(QKeyEvent* e) {
  const Cible sel = m_doc->selection();
  if (e->key() == Qt::Key_Escape) { m_doc->selectionner({}); return; }
  if (sel.type != Cible::Composant && sel.type != Cible::Trou) { QWidget::keyPressEvent(e); return; }
  const Touche t{sel.type == Cible::Composant ? Touche::Composant : Touche::Trou, sel.id};
  if (t.type == Touche::Composant && verrouille(t.id) && e->key() != Qt::Key_Delete && e->key() != Qt::Key_Backspace) return;
  const double pas = (e->modifiers() & Qt::ShiftModifier) ? 5 : 0.5;
  double dx = 0, dy = 0;
  switch (e->key()) {
    case Qt::Key_Left: dx = -pas; break;
    case Qt::Key_Right: dx = pas; break;
    case Qt::Key_Up: dy = pas; break;
    case Qt::Key_Down: dy = -pas; break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
      m_doc->modifier([&](Projet& p) {
        if (t.type == Touche::Composant) p.composants.erase(std::remove_if(p.composants.begin(), p.composants.end(), [&](const Composant& k) { return k.id == t.id; }), p.composants.end());
        else p.trous.erase(std::remove_if(p.trous.begin(), p.trous.end(), [&](const Trou& q) { return q.id == t.id; }), p.trous.end());
      });
      return;
    case Qt::Key_R:
      m_doc->modifier([&](Projet& p) { for (auto& k : p.composants) if (k.id == t.id && !k.bord) k.rot = (k.rot + 90) % 360; });
      return;
    default: QWidget::keyPressEvent(e); return;
  }
  const QPointF pos = positionObjet(t);
  m_doc->modifier([&](Projet& p) { deplacer(p, t, pos.x() + dx, pos.y() + dy, true); });
}

void VueCarte::menuContextuel(const QPoint& ou) {
  const Touche t = m_cibleMenu;
  QMenu menu(this);
  if (t.type == Touche::Composant) {
    m_doc->selectionner({Cible::Composant, t.id});
    const Composant* k = m_doc->composantSelectionne();
    if (!k) return;
    const std::string id = k->id;
    QAction* verrou = menu.addAction(k->verrou ? QStringLiteral("Déverrouiller la position") : QStringLiteral("Verrouiller la position"));
    verrou->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(verrou, &QAction::triggered, this, [this, id] { m_doc->modifier([&](Projet& p) { for (auto& q : p.composants) if (q.id == id) q.verrou = !q.verrou; }); });
    QAction* pivoter = menu.addAction(QStringLiteral("Pivoter de 90°"));
    pivoter->setEnabled(!k->verrou && !k->bord);
    connect(pivoter, &QAction::triggered, this, [this, id] { m_doc->modifier([&](Projet& p) { for (auto& q : p.composants) if (q.id == id) q.rot = (q.rot + 90) % 360; }); });
    QMenu* ancrages = menu.addMenu(QStringLiteral("Ancrage"));
    ancrages->setEnabled(!k->verrou);
    const std::vector<std::pair<const char*, QString>> choix = k->bord
        ? std::vector<std::pair<const char*, QString>>{{"libre", QStringLiteral("Reste à sa place")}, {"debut", QStringLiteral("Suit le début du bord")},
                                                        {"fin", QStringLiteral("Suit la fin du bord")}, {"milieu", QStringLiteral("Suit le milieu du bord")}}
        : std::vector<std::pair<const char*, QString>>{{"libre", QStringLiteral("Reste à sa place")}, {"NW", QStringLiteral("Suit le coin haut gauche")},
                                                        {"NE", QStringLiteral("Suit le coin haut droit")}, {"SW", QStringLiteral("Suit le coin bas gauche")},
                                                        {"SE", QStringLiteral("Suit le coin bas droit")}, {"centre", QStringLiteral("Suit le centre de la carte")}};
    for (const auto& [cle, texte] : choix) {
      QAction* a = ancrages->addAction(texte);
      a->setCheckable(true);
      a->setChecked(k->ancrage == cle);
      const std::string an = cle;
      connect(a, &QAction::triggered, this, [this, id, an] { m_doc->modifier([&](Projet& p) { for (auto& q : p.composants) if (q.id == id) changerAncrage(p, q, an); }); });
    }
    menu.addSeparator();
    QAction* supprimer = menu.addAction(QStringLiteral("Supprimer"));
    connect(supprimer, &QAction::triggered, this, [this, id] {
      m_doc->modifier([&](Projet& p) { p.composants.erase(std::remove_if(p.composants.begin(), p.composants.end(), [&](const Composant& q) { return q.id == id; }), p.composants.end()); });
    });
  } else if (t.type == Touche::Trou) {
    m_doc->selectionner({Cible::Trou, t.id});
    const std::string id = t.id;
    QAction* supprimer = menu.addAction(QStringLiteral("Supprimer le trou"));
    connect(supprimer, &QAction::triggered, this, [this, id] {
      m_doc->modifier([&](Projet& p) { p.trous.erase(std::remove_if(p.trous.begin(), p.trous.end(), [&](const Trou& q) { return q.id == id; }), p.trous.end()); });
    });
  } else {
    QAction* recadrerAct = menu.addAction(QStringLiteral("Recadrer la vue"));
    connect(recadrerAct, &QAction::triggered, this, &VueCarte::recadrer);
  }
  menu.exec(ou);
}

bool VueCarte::verrouille(const std::string& id) const {
  for (const auto& k : m_doc->projet().composants) if (k.id == id) return k.verrou;
  return false;
}

void VueCarte::leaveEvent(QEvent*) {
  if (m_survol.type != Touche::Rien) { m_survol = {}; update(); }
}

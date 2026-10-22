// Prométhée : périphérique de dessin DXF (R12). Voir PeintreDxf.hpp. Licence GPL-3.0-only.
#include "PeintreDxf.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <numbers>
#include <string>
#include <vector>

#include <QBrush>
#include <QFont>
#include <QFontMetricsF>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QTextItem>
#include <QTransform>

namespace {

std::string nombre(double v) {
  if (!std::isfinite(v)) v = 0;
  char b[40];
  std::snprintf(b, sizeof b, "%.4f", v);
  std::string s = b;
  while (s.size() > 1 && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  if (s == "-0") s = "0";
  return s;
}

// Texte DXF : ASCII tel quel, le reste en \U+XXXX (lu par AutoCAD, BricsCAD, LibreCAD, QCAD, ezdxf)
std::string texteDxf(const QString& t) {
  std::string r;
  for (const char32_t c : t.toUcs4()) {
    if (c == '\n' || c == '\r' || c == '\t') r += ' ';
    else if (c == '^') r += "^ ";
    else if (c >= 0x20 && c < 0x7F) r += static_cast<char>(c);
    else if (c <= 0xFFFF) {
      char b[12];
      std::snprintf(b, sizeof b, "\\U+%04X", static_cast<unsigned>(c));
      r += b;
    }
  }
  // « %% » ouvre un code de contrôle : un « % » littéral double se lit « %%% »
  for (std::size_t i = r.find("%%"); i != std::string::npos; i = r.find("%%", i + 3)) r.replace(i, 2, "%%%");
  return r;
}

// Couleur indexée AutoCAD la plus proche ; l'encre presque noire devient la couleur 7 (noire sur fond blanc)
int couleurAci(const QColor& c) {
  if (c.red() < 90 && c.green() < 90 && c.blue() < 90) return 7;
  struct Aci {
    int indice, r, g, b;
  };
  static const Aci palette[] = {{1, 255, 0, 0},   {2, 255, 255, 0}, {3, 0, 255, 0},     {4, 0, 255, 255},   {5, 0, 0, 255},   {6, 255, 0, 255},
                                {8, 128, 128, 128}, {9, 192, 192, 192}, {30, 255, 127, 0}, {14, 127, 0, 0},   {94, 0, 127, 0},  {174, 0, 0, 127},
                                {150, 0, 127, 255}, {210, 255, 0, 191}, {250, 51, 51, 51}, {252, 105, 105, 105}};
  int meilleur = 7;
  long distance = LONG_MAX;
  for (const auto& a : palette) {
    const long d = (a.r - c.red()) * (a.r - c.red()) + (a.g - c.green()) * (a.g - c.green()) + (a.b - c.blue()) * (a.b - c.blue());
    if (d < distance) {
      distance = d;
      meilleur = a.indice;
    }
  }
  return meilleur;
}

bool presqueBlanc(const QColor& c) { return c.red() > 235 && c.green() > 235 && c.blue() > 235; }

const char* typeLigne(Qt::PenStyle s) {
  switch (s) {
    case Qt::DashLine: return "DASHED";
    case Qt::DotLine: return "DOT";
    case Qt::DashDotLine:
    case Qt::DashDotDotLine: return "DASHDOT";
    default: return nullptr;
  }
}

}  // namespace

class PeintreDxf::Moteur : public QPaintEngine {
 public:
  Moteur(double hauteurMm, double pixelsParMm) : QPaintEngine(QPaintEngine::AllFeatures), m_hauteur(hauteurMm), m_ppmm(pixelsParMm) {}

  bool begin(QPaintDevice*) override { return true; }
  bool end() override { return true; }
  Type type() const override { return QPaintEngine::User; }

  void updateState(const QPaintEngineState& s) override {
    const DirtyFlags f = s.state();
    if (f & DirtyTransform) m_transform = s.transform();
    if (f & DirtyPen) m_pen = s.pen();
    if (f & DirtyBrush) m_brush = s.brush();
    if (f & DirtyFont) m_font = s.font();
    if (f & DirtyOpacity) m_opacite = s.opacity();
  }

  void drawPixmap(const QRectF&, const QPixmap&, const QRectF&) override {}
  void drawImage(const QRectF&, const QImage&, const QRectF&, Qt::ImageConversionFlags) override {}
  void drawPoints(const QPointF*, int) override {}

  void drawLines(const QLineF* l, int n) override {
    if (!trace()) return;
    for (int i = 0; i < n; ++i) {
      const QPointF a = mm(m_transform.map(l[i].p1())), b = mm(m_transform.map(l[i].p2()));
      if (a == b) continue;
      entete("LINE", m_pen.color(), typeLigne(m_pen.style()));
      paire(10, nombre(a.x()));
      paire(20, nombre(a.y()));
      paire(30, "0");
      paire(11, nombre(b.x()));
      paire(21, nombre(b.y()));
      paire(31, "0");
    }
  }

  void drawRects(const QRectF* r, int n) override {
    for (int i = 0; i < n; ++i) {
      QPolygonF p;
      p << r[i].topLeft() << r[i].topRight() << r[i].bottomRight() << r[i].bottomLeft();
      forme(m_transform.map(p), true);
    }
  }

  void drawPolygon(const QPointF* pts, int n, PolygonDrawMode mode) override {
    QPolygonF p;
    for (int i = 0; i < n; ++i) p << pts[i];
    forme(m_transform.map(p), mode != PolylineMode);
  }

  void drawEllipse(const QRectF& r) override {
    const bool similitude = std::abs(m_transform.m11() - m_transform.m22()) < 1e-9 && std::abs(m_transform.m12() + m_transform.m21()) < 1e-9 &&
                            m_transform.type() <= QTransform::TxRotate;
    if (!similitude || std::abs(r.width() - r.height()) > 1e-6 * std::max(1.0, r.width())) {
      QPainterPath chemin;
      chemin.addEllipse(r);
      drawPath(chemin);
      return;
    }
    const double echelle = std::hypot(m_transform.m11(), m_transform.m12());
    const QPointF c = mm(m_transform.map(r.center()));
    const double rayon = r.width() / 2 * echelle / m_ppmm;
    if (rayon <= 0) return;
    if (remplir()) {
      // Disque plein (jonction) : polyligne fermée de deux demi-cercles, de largeur égale au rayon
      entete("POLYLINE", m_brush.color(), nullptr);
      paire(66, "1");
      paire(10, "0");
      paire(20, "0");
      paire(30, "0");
      paire(70, "1");
      paire(40, nombre(rayon));
      paire(41, nombre(rayon));
      for (double sens : {-1.0, 1.0}) {
        entete("VERTEX", m_brush.color(), nullptr);
        paire(10, nombre(c.x() + sens * rayon / 2));
        paire(20, nombre(c.y()));
        paire(30, "0");
        paire(42, "1");
      }
      entete("SEQEND", m_brush.color(), nullptr);
    }
    if (trace() || !remplir()) {
      entete("CIRCLE", trace() ? m_pen.color() : m_brush.color(), trace() ? typeLigne(m_pen.style()) : nullptr);
      paire(10, nombre(c.x()));
      paire(20, nombre(c.y()));
      paire(30, "0");
      paire(40, nombre(rayon));
    }
  }

  void drawPath(const QPainterPath& chemin) override {
    for (const QPolygonF& p : chemin.toSubpathPolygons(m_transform)) {
      const bool fermee = p.size() > 2 && p.isClosed();
      QPolygonF q = p;
      if (fermee) q.removeLast();
      forme(q, fermee);
    }
  }

  void drawTextItem(const QPointF& p, const QTextItem& t) override {
    const QString texte = t.text();
    if (texte.trimmed().isEmpty() || m_opacite < 0.3) return;
    const QFont f = t.font();
    const QFontMetricsF m(f);
    const double echelle = std::hypot(m_transform.m11(), m_transform.m12());
    double capitale = m.capHeight();
    if (capitale <= 0) capitale = 0.7 * (f.pixelSize() > 0 ? f.pixelSize() : m.height());
    const double hauteur = capitale * echelle / m_ppmm;
    if (hauteur <= 0.05) return;
    const QPointF a = mm(m_transform.map(p)), b = mm(m_transform.map(p + QPointF(std::max(1e-3, static_cast<double>(t.width())), 0)));
    const double angle = std::atan2(b.y() - a.y(), b.x() - a.x()) * 180 / std::numbers::pi;
    entete("TEXT", m_pen.color(), nullptr, true);
    paire(10, nombre(a.x()));
    paire(20, nombre(a.y()));
    paire(30, "0");
    paire(40, nombre(hauteur));
    paire(1, texteDxf(texte));
    if (std::abs(angle) > 1e-6) paire(50, nombre(angle));
    paire(7, "STANDARD");
    // Ajusté entre ses deux points : le texte occupe exactement la place que Qt lui a donnée
    paire(72, "5");
    paire(11, nombre(b.x()));
    paire(21, nombre(b.y()));
    paire(31, "0");
  }

  void choisirCalque(const QString& nom) {
    m_calque = nom.isEmpty() ? QStringLiteral("0") : nom.toUpper();
    if (!m_calques.count(m_calque)) m_calques[m_calque] = m_calque == QStringLiteral("CADRE") || m_calque == QStringLiteral("RENVOIS") ? 8 : 7;
  }

  QByteArray document(double largeur, double hauteur) const {
    std::string o;
    auto p = [&](int code, const std::string& v) {
      o += std::to_string(code);
      o += "\r\n";
      o += v;
      o += "\r\n";
    };
    p(0, "SECTION");
    p(2, "HEADER");
    p(9, "$ACADVER");
    p(1, "AC1009");
    p(9, "$DWGCODEPAGE");
    p(3, "ANSI_1252");
    p(9, "$INSBASE");
    p(10, "0");
    p(20, "0");
    p(30, "0");
    p(9, "$EXTMIN");
    p(10, "0");
    p(20, "0");
    p(30, "0");
    p(9, "$EXTMAX");
    p(10, nombre(largeur));
    p(20, nombre(hauteur));
    p(30, "0");
    p(9, "$LIMMIN");
    p(10, "0");
    p(20, "0");
    p(9, "$LIMMAX");
    p(10, nombre(largeur));
    p(20, nombre(hauteur));
    p(9, "$INSUNITS");
    p(70, "4");
    p(0, "ENDSEC");
    p(0, "SECTION");
    p(2, "TABLES");
    // Types de ligne
    p(0, "TABLE");
    p(2, "LTYPE");
    p(70, "4");
    struct Type {
      const char* nom;
      const char* description;
      std::vector<double> motif;
    };
    const Type types[] = {{"CONTINUOUS", "Solid line", {}},
                          {"DASHED", "__ __ __ __", {2.5, -1.25}},
                          {"DOT", ". . . . . .", {0.0, -1.0}},
                          {"DASHDOT", "__ . __ . __", {2.5, -0.75, 0.0, -0.75}}};
    for (const auto& t : types) {
      p(0, "LTYPE");
      p(2, t.nom);
      p(70, "0");
      p(3, t.description);
      p(72, "65");
      p(73, std::to_string(t.motif.size()));
      double total = 0;
      for (double x : t.motif) total += std::abs(x);
      p(40, nombre(total));
      for (double x : t.motif) p(49, nombre(x));
    }
    p(0, "ENDTAB");
    // Calques
    p(0, "TABLE");
    p(2, "LAYER");
    p(70, std::to_string(m_calques.size() + 1));
    std::map<QString, int> calques = m_calques;
    calques.emplace(QStringLiteral("0"), 7);
    for (const auto& [nom, couleur] : calques) {
      p(0, "LAYER");
      p(2, nom.toStdString());
      p(70, "0");
      p(62, std::to_string(couleur));
      p(6, "CONTINUOUS");
    }
    p(0, "ENDTAB");
    // Style de texte
    p(0, "TABLE");
    p(2, "STYLE");
    p(70, "1");
    p(0, "STYLE");
    p(2, "STANDARD");
    p(70, "0");
    p(40, "0");
    p(41, "1");
    p(50, "0");
    p(71, "0");
    p(42, "2.5");
    p(3, "txt");
    p(4, "");
    p(0, "ENDTAB");
    p(0, "ENDSEC");
    p(0, "SECTION");
    p(2, "ENTITIES");
    o += m_entites;
    p(0, "ENDSEC");
    p(0, "EOF");
    return QByteArray::fromStdString(o);
  }

  int entites() const { return m_nombre; }

 private:
  QPointF mm(const QPointF& d) const { return {d.x() / m_ppmm, m_hauteur - d.y() / m_ppmm}; }
  bool trace() const { return m_pen.style() != Qt::NoPen && m_pen.color().alpha() >= 60 && m_opacite >= 0.3; }
  bool remplir() const {
    return m_brush.style() != Qt::NoBrush && m_brush.color().alpha() >= 128 && !presqueBlanc(m_brush.color()) && m_opacite >= 0.3;
  }

  void paire(int code, const std::string& v) {
    m_entites += std::to_string(code);
    m_entites += "\r\n";
    m_entites += v;
    m_entites += "\r\n";
  }

  void entete(const char* entite, const QColor& couleur, const char* ligne, bool texte = false) {
    QString calque = m_calque;
    if (texte && calque == QStringLiteral("SYMBOLES")) calque = QStringLiteral("REPERES");
    if (!m_calques.count(calque)) m_calques[calque] = 7;
    paire(0, entite);
    paire(8, calque.toStdString());
    const std::string e = entite;
    if (e == "VERTEX" || e == "SEQEND") return;  // la couleur et le type de ligne sont ceux de la polyligne
    if (ligne) paire(6, ligne);
    const int aci = couleurAci(couleur);
    if (aci != m_calques[calque]) paire(62, std::to_string(aci));
    ++m_nombre;
  }

  // Polygone en coordonnées de l'appareil : aplat (solide de 3 ou 4 sommets) et contour
  void forme(const QPolygonF& d, bool fermee) {
    if (d.size() < 2) return;
    std::vector<QPointF> v;
    for (const QPointF& x : d) {
      const QPointF y = mm(x);
      if (v.empty() || std::hypot(y.x() - v.back().x(), y.y() - v.back().y()) > 1e-6) v.push_back(y);
    }
    if (fermee && v.size() > 2 && std::hypot(v.front().x() - v.back().x(), v.front().y() - v.back().y()) <= 1e-6) v.pop_back();
    if (v.size() < 2) return;
    const bool plein = fermee && remplir() && (v.size() == 3 || v.size() == 4);
    if (plein) {
      // SOLID : sommets dans l'ordre 1, 2, 4, 3 pour un quadrilatère
      const QPointF s[4] = {v[0], v[1], v.size() == 4 ? v[3] : v[2], v[2]};
      entete("SOLID", m_brush.color(), nullptr);
      for (int i = 0; i < 4; ++i) {
        paire(10 + i, nombre(s[i].x()));
        paire(20 + i, nombre(s[i].y()));
        paire(30 + i, "0");
      }
    }
    // Le contour : celui du crayon, sinon celui d'un aplat qu'un solide ne rend pas
    const bool contour = trace() || (fermee && remplir() && !plein);
    if (!contour) return;
    const QColor c = trace() ? m_pen.color() : m_brush.color();
    const char* ligne = trace() ? typeLigne(m_pen.style()) : nullptr;
    if (v.size() == 2) {
      entete("LINE", c, ligne);
      paire(10, nombre(v[0].x()));
      paire(20, nombre(v[0].y()));
      paire(30, "0");
      paire(11, nombre(v[1].x()));
      paire(21, nombre(v[1].y()));
      paire(31, "0");
      return;
    }
    entete("POLYLINE", c, ligne);
    paire(66, "1");
    paire(10, "0");
    paire(20, "0");
    paire(30, "0");
    paire(70, fermee ? "1" : "0");
    for (const QPointF& x : v) {
      entete("VERTEX", c, nullptr);
      paire(10, nombre(x.x()));
      paire(20, nombre(x.y()));
      paire(30, "0");
    }
    entete("SEQEND", c, nullptr);
  }

  double m_hauteur, m_ppmm;
  QTransform m_transform;
  QPen m_pen;
  QBrush m_brush;
  QFont m_font;
  double m_opacite = 1;
  QString m_calque = QStringLiteral("0");
  std::map<QString, int> m_calques;
  std::string m_entites;
  int m_nombre = 0;
};

PeintreDxf::PeintreDxf(double largeurMm, double hauteurMm, double pixelsParMm)
    : m_largeur(largeurMm), m_hauteur(hauteurMm), m_pixelsParMm(pixelsParMm), m_moteur(std::make_unique<Moteur>(hauteurMm, pixelsParMm)) {}

PeintreDxf::~PeintreDxf() = default;

QPaintEngine* PeintreDxf::paintEngine() const { return m_moteur.get(); }

void PeintreDxf::choisirCalque(const QString& nom) { m_moteur->choisirCalque(nom); }

void PeintreDxf::calque(QPainter& g, const QString& nom) {
  if (auto* d = dynamic_cast<PeintreDxf*>(g.device())) d->choisirCalque(nom);
}

QByteArray PeintreDxf::document() const { return m_moteur->document(m_largeur, m_hauteur); }

int PeintreDxf::entites() const { return m_moteur->entites(); }

int PeintreDxf::metric(PaintDeviceMetric m) const {
  switch (m) {
    case PdmWidth: return static_cast<int>(std::lround(m_largeur * m_pixelsParMm));
    case PdmHeight: return static_cast<int>(std::lround(m_hauteur * m_pixelsParMm));
    case PdmWidthMM: return static_cast<int>(std::lround(m_largeur));
    case PdmHeightMM: return static_cast<int>(std::lround(m_hauteur));
    case PdmNumColors: return INT_MAX;
    case PdmDepth: return 32;
    case PdmDpiX:
    case PdmDpiY:
    case PdmPhysicalDpiX:
    case PdmPhysicalDpiY: return static_cast<int>(std::lround(m_pixelsParMm * 25.4));
    case PdmDevicePixelRatio: return 1;
    case PdmDevicePixelRatioScaled: return static_cast<int>(QPaintDevice::devicePixelRatioFScale());
    default: return QPaintDevice::metric(m);
  }
}

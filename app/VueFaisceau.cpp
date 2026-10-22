// Prométhée : planche de faisceau. Licence GPL-3.0-only.
#include "VueFaisceau.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "Document.hpp"
#include "Theme.hpp"
#include "promethee/modele.hpp"

using namespace prom::elec;
using prom::ecad::Pt;

namespace {
double mm(prom::ecad::Nm v) { return static_cast<double>(v) / static_cast<double>(prom::ecad::NM_PAR_MM); }
}  // namespace

VueFaisceau::VueFaisceau(Document* doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(300, 200);
  connect(m_doc, &Document::change, this, [this] { update(); });
}

const ProjetElectrique* VueFaisceau::projet() const {
  const auto& p = m_doc->projet();
  return m_projet >= 0 && m_projet < static_cast<int>(p.electrique.size()) ? &p.electrique[static_cast<std::size_t>(m_projet)] : nullptr;
}

const Faisceau* VueFaisceau::faisceauCourant() const {
  const ProjetElectrique* p = projet();
  return p && m_faisceau >= 0 && m_faisceau < static_cast<int>(p->faisceaux.size()) ? &p->faisceaux[static_cast<std::size_t>(m_faisceau)] : nullptr;
}

QPointF VueFaisceau::ecran(Pt p) const { return m_origine + QPointF(mm(p.x) * m_echelle, mm(p.y) * m_echelle); }
Pt VueFaisceau::monde(QPointF q) const {
  // Grille de 5 mm
  const double x = std::round((q.x() - m_origine.x()) / m_echelle / 5.0) * 5.0, y = std::round((q.y() - m_origine.y()) / m_echelle / 5.0) * 5.0;
  return prom::ecad::ptMm(x, y);
}

void VueFaisceau::setFaisceau(int projet, int faisceau) {
  const bool autre = projet != m_projet || faisceau != m_faisceau;
  m_projet = projet;
  m_faisceau = faisceau;
  if (autre) {
    m_selection.clear();
    m_debutBranche.clear();
    recadrer();
  }
  update();
}

void VueFaisceau::setOutil(Outil o) {
  m_outil = o;
  m_debutBranche.clear();
  setCursor(o == Outil::Selection ? Qt::ArrowCursor : Qt::CrossCursor);
  emit message(o == Outil::Noeud     ? QStringLiteral("<b>Nœud</b> : clique sur la planche pour poser un connecteur ou une dérivation · Échap")
               : o == Outil::Branche ? QStringLiteral("<b>Branche</b> : clique un nœud, puis un autre · Échap")
                                     : QString());
  update();
}

void VueFaisceau::selectionner(const std::string& s) {
  m_selection = s;
  emit selectionChangee();
  update();
}

void VueFaisceau::recadrer() {
  const Faisceau* f = faisceauCourant();
  if (!f || width() < 20) return;
  double x0 = 0, y0 = 0, x1 = 400, y1 = 250;
  bool premier = true;
  for (const auto& n : f->noeuds) {
    const double x = mm(n.p.x), y = mm(n.p.y);
    if (premier) {
      x0 = x1 = x;
      y0 = y1 = y;
      premier = false;
    }
    x0 = std::min(x0, x);
    x1 = std::max(x1, x);
    y0 = std::min(y0, y);
    y1 = std::max(y1, y);
  }
  const double l = std::max(100.0, x1 - x0 + 120), h = std::max(80.0, y1 - y0 + 120);
  m_echelle = std::min((width() - 40) / l, (height() - 40) / h);
  m_origine = QPointF(width() / 2.0 - (x0 + x1) / 2 * m_echelle, height() / 2.0 - (y0 + y1) / 2 * m_echelle);
  update();
}

void VueFaisceau::modifier(const std::function<void(Faisceau&)>& f) {
  const int pi = m_projet, fi = m_faisceau;
  m_doc->modifier([&](prom::Projet& p) {
    if (pi < 0 || pi >= static_cast<int>(p.electrique.size())) return;
    auto& pe = p.electrique[static_cast<std::size_t>(pi)];
    if (fi < 0 || fi >= static_cast<int>(pe.faisceaux.size())) return;
    f(pe.faisceaux[static_cast<std::size_t>(fi)]);
  });
}

std::string VueFaisceau::objetSous(QPointF q) const {
  const Faisceau* f = faisceauCourant();
  if (!f) return {};
  for (const auto& n : f->noeuds) {
    const QPointF e = ecran(n.p);
    if (std::hypot(e.x() - q.x(), e.y() - q.y()) < (n.composant.empty() ? 8 : 14)) return n.uuid;
  }
  for (std::size_t i = 0; i < f->branches.size(); ++i) {
    const auto& b = f->branches[i];
    const NoeudFaisceau* a = nullptr;
    const NoeudFaisceau* c = nullptr;
    for (const auto& n : f->noeuds) {
      if (n.uuid == b.a) a = &n;
      if (n.uuid == b.b) c = &n;
    }
    if (!a || !c) continue;
    const QPointF p0 = ecran(a->p), p1 = ecran(c->p), d = p1 - p0;
    const double l2 = d.x() * d.x() + d.y() * d.y();
    if (l2 < 1) continue;
    const double t = std::clamp(((q - p0).x() * d.x() + (q - p0).y() * d.y()) / l2, 0.0, 1.0);
    const QPointF pr = p0 + d * t;
    if (std::hypot(pr.x() - q.x(), pr.y() - q.y()) < 6) return "b:" + std::to_string(i);
  }
  return {};
}

void VueFaisceau::cliquerEn(double xmm, double ymm) { clic(ecran(prom::ecad::ptMm(xmm, ymm))); }

void VueFaisceau::clic(QPointF q) {
  const Faisceau* f = faisceauCourant();
  if (!f) return;
  const std::string o = objetSous(q);
  if (m_outil == Outil::Noeud) {
    if (!o.empty() && o.rfind("b:", 0) != 0) {
      selectionner(o);
      return;
    }
    const Pt p = monde(q);
    const std::string id = prom::ecad::nouvelUuid();
    const std::string nom = "N" + std::to_string(f->noeuds.size() + 1);
    modifier([&](Faisceau& x) { x.noeuds.push_back({id, nom, p, {}}); });
    selectionner(id);
    return;
  }
  if (m_outil == Outil::Branche) {
    if (o.empty() || o.rfind("b:", 0) == 0) return;
    if (m_debutBranche.empty()) {
      m_debutBranche = o;
      emit message(QStringLiteral("<b>Branche</b> : clique le nœud d’arrivée · Échap"));
      update();
      return;
    }
    if (o != m_debutBranche) {
      const std::string a = m_debutBranche;
      modifier([&](Faisceau& x) { x.branches.push_back({a, o, 0, {}}); });
      selectionner("b:" + std::to_string(faisceauCourant()->branches.size() - 1));
    }
    m_debutBranche = o;  // on enchaîne depuis le nœud atteint
    update();
    return;
  }
  selectionner(o);
}

void VueFaisceau::supprimerSelection() {
  if (m_selection.empty()) return;
  const std::string s = m_selection;
  modifier([&](Faisceau& f) {
    if (s.rfind("b:", 0) == 0) {
      const std::size_t i = static_cast<std::size_t>(std::stoul(s.substr(2)));
      if (i < f.branches.size()) f.branches.erase(f.branches.begin() + static_cast<std::ptrdiff_t>(i));
      return;
    }
    f.noeuds.erase(std::remove_if(f.noeuds.begin(), f.noeuds.end(), [&](const NoeudFaisceau& n) { return n.uuid == s; }), f.noeuds.end());
    f.branches.erase(std::remove_if(f.branches.begin(), f.branches.end(), [&](const BrancheFaisceau& b) { return b.a == s || b.b == s; }), f.branches.end());
  });
  selectionner({});
}

void VueFaisceau::paintEvent(QPaintEvent*) {
  QPainter g(this);
  g.setRenderHint(QPainter::Antialiasing);
  const Theme& th = theme();
  g.fillRect(rect(), th.fond(QColor(0xF4, 0xF1, 0xE8)));
  const ProjetElectrique* p = projet();
  const Faisceau* f = faisceauCourant();
  if (!p || !f) {
    g.setPen(th.texte(QColor(0x6B, 0x73, 0x93)));
    g.drawText(rect(), Qt::AlignCenter, QStringLiteral("Aucun faisceau : crée-en un depuis le ruban (« Nouveau faisceau »)."));
    return;
  }
  // Planche : quadrillage de 50 mm
  g.setPen(QPen(th.bordure(QColor(0xE2, 0xDC, 0xCB)), 1));
  const double pas = 50 * m_echelle;
  if (pas > 8) {
    for (double x = std::fmod(m_origine.x(), pas); x < width(); x += pas) g.drawLine(QPointF(x, 0), QPointF(x, height()));
    for (double y = std::fmod(m_origine.y(), pas); y < height(); y += pas) g.drawLine(QPointF(0, y), QPointF(width(), y));
  }
  auto noeud = [&](const std::string& id) -> const NoeudFaisceau* {
    for (const auto& n : f->noeuds)
      if (n.uuid == id) return &n;
    return nullptr;
  };
  QFont fc = font();
  fc.setPixelSize(11);
  // Branches : épaisseur selon le nombre de fils qui y passent
  std::map<std::string, int> passages;
  for (const auto& w : filsFaisceau(*p, static_cast<std::size_t>(m_faisceau)))
    for (const auto& b : w.branches) ++passages[b];
  for (std::size_t i = 0; i < f->branches.size(); ++i) {
    const auto& b = f->branches[i];
    const NoeudFaisceau* a = noeud(b.a);
    const NoeudFaisceau* c = noeud(b.b);
    if (!a || !c) continue;
    const QPointF p0 = ecran(a->p), p1 = ecran(c->p);
    const bool choisi = m_selection == "b:" + std::to_string(i);
    const std::string cle = (a->nom.empty() ? a->uuid : a->nom) + "–" + (c->nom.empty() ? c->uuid : c->nom);
    const int n = passages.count(cle) ? passages[cle] : 0;
    g.setPen(QPen(choisi ? QColor(0x5B, 0x4B, 0xF5) : QColor(0x9A, 0x5B, 0x00), std::clamp(3.0 + n * 0.8, 3.0, 12.0), Qt::SolidLine, Qt::RoundCap));
    g.drawLine(p0, p1);
    g.setFont(fc);
    g.setPen(th.texte(QColor(0x23, 0x2A, 0x45)));
    const QPointF m = (p0 + p1) / 2;
    const QString t = QStringLiteral("%1 mm%2%3").arg(std::lround(longueurBranche(*f, b))).arg(b.longueur > 0 ? QStringLiteral(" (imposée)") : QString())
                          .arg(n > 0 ? QStringLiteral(" · %1 fil(s)").arg(n) : QString());
    g.drawText(QRectF(m.x() - 90, m.y() - 22, 180, 16), Qt::AlignCenter, t);
  }
  // Nœuds : connecteur (rectangle repéré) ou dérivation (point)
  for (const auto& n : f->noeuds) {
    const QPointF e = ecran(n.p);
    const bool choisi = m_selection == n.uuid || m_debutBranche == n.uuid;
    QString texte = QString::fromStdString(n.nom);
    if (!n.composant.empty()) {
      if (const auto* c = p->schema.composant(n.composant)) texte = QString::fromStdString(repereAffiche(*p, *c));
      g.setPen(QPen(choisi ? QColor(0x5B, 0x4B, 0xF5) : QColor(0x23, 0x2A, 0x45), choisi ? 2.4 : 1.4));
      g.setBrush(QColor(0xF2, 0xA9, 0x00));
      g.drawRoundedRect(QRectF(e.x() - 12, e.y() - 9, 24, 18), 3, 3);
    } else {
      g.setPen(QPen(choisi ? QColor(0x5B, 0x4B, 0xF5) : QColor(0x23, 0x2A, 0x45), choisi ? 2.4 : 1.2));
      g.setBrush(QColor(0x23, 0x2A, 0x45));
      g.drawEllipse(e, 4.5, 4.5);
    }
    g.setFont(fc);
    g.setPen(th.texte(QColor(0x10, 0x14, 0x2A)));
    g.drawText(QRectF(e.x() - 60, e.y() + 11, 120, 16), Qt::AlignHCenter | Qt::AlignTop, texte);
  }
  if (m_outil == Outil::Branche && !m_debutBranche.empty())
    if (const NoeudFaisceau* a = noeud(m_debutBranche)) {
      g.setPen(QPen(QColor(0x5B, 0x4B, 0xF5), 2, Qt::DashLine));
      g.drawLine(ecran(a->p), m_souris);
    }
}

void VueFaisceau::mousePressEvent(QMouseEvent* e) {
  setFocus();
  m_souris = m_pression = e->position();
  if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
    m_panoramique = true;
    return;
  }
  if (e->button() != Qt::LeftButton) return;
  clic(e->position());
  const Faisceau* f = faisceauCourant();
  m_appuye = m_outil == Outil::Selection && f && !m_selection.empty() && m_selection.rfind("b:", 0) != 0;
  if (m_appuye)
    for (const auto& n : f->noeuds)
      if (n.uuid == m_selection) m_depart = n.p;
}

void VueFaisceau::mouseMoveEvent(QMouseEvent* e) {
  const QPointF avant = m_souris;
  m_souris = e->position();
  if (m_panoramique) {
    m_origine += m_souris - avant;
    update();
    return;
  }
  if (m_appuye) {
    if (!m_glisse && (m_souris - m_pression).manhattanLength() > 4) {
      m_glisse = true;
      m_doc->debutGeste();
    }
    if (m_glisse) {
      const Pt cible = monde(m_souris);
      const std::string id = m_selection;
      const int pi = m_projet, fi = m_faisceau;
      m_doc->pendantGeste([&](prom::Projet& p) {
        if (pi < 0 || pi >= static_cast<int>(p.electrique.size())) return;
        auto& pe = p.electrique[static_cast<std::size_t>(pi)];
        if (fi < 0 || fi >= static_cast<int>(pe.faisceaux.size())) return;
        for (auto& n : pe.faisceaux[static_cast<std::size_t>(fi)].noeuds)
          if (n.uuid == id) n.p = cible;
      });
    }
    return;
  }
  update();
}

void VueFaisceau::mouseReleaseEvent(QMouseEvent*) {
  m_appuye = false;
  m_panoramique = false;
  if (m_glisse) {
    m_glisse = false;
    m_doc->finGeste();
  }
}

void VueFaisceau::wheelEvent(QWheelEvent* e) {
  const double f = std::pow(1.0015, e->angleDelta().y());
  const double nouvelle = std::clamp(m_echelle * f, 0.05, 20.0);
  const QPointF q = e->position();
  m_origine = q - (q - m_origine) * (nouvelle / m_echelle);
  m_echelle = nouvelle;
  update();
}

void VueFaisceau::keyPressEvent(QKeyEvent* e) {
  switch (e->key()) {
    case Qt::Key_Escape:
      if (m_outil != Outil::Selection) setOutil(Outil::Selection);
      else selectionner({});
      return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace: supprimerSelection(); return;
    case Qt::Key_N: setOutil(Outil::Noeud); return;
    case Qt::Key_B: setOutil(Outil::Branche); return;
    case Qt::Key_F:
    case Qt::Key_Home: recadrer(); return;
    default: QWidget::keyPressEvent(e);
  }
}

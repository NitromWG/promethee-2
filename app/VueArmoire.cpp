// Prométhée : vue d'une armoire électrique. Licence GPL-3.0-only.
#include "VueArmoire.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include "Document.hpp"
#include "Theme.hpp"
#include "promethee/modele.hpp"

using namespace prom::elec;

namespace {
double mm(prom::ecad::Nm v) { return static_cast<double>(v) / static_cast<double>(prom::ecad::NM_PAR_MM); }
prom::ecad::Nm nm(double v) { return static_cast<prom::ecad::Nm>(std::llround(v * static_cast<double>(prom::ecad::NM_PAR_MM))); }

// Couleur d'un appareil selon sa famille (protection, commande, automatisme, alimentation, bornes, autres)
QColor couleurFamille(const std::string& s) {
  if (s.rfind("iec:disj", 0) == 0 || s.rfind("iec:diff", 0) == 0 || s == "iec:fusible" || s.find("sect") != std::string::npos || s == "iec:parafoudre" ||
      s.rfind("iec:u-", 0) == 0 || s == "iec:agcp")
    return QColor(0xE8, 0x8A, 0x7A);
  if (s.rfind("iec:bobine", 0) == 0 || s == "iec:relais-thermique-3p" || s == "iec:telerupteur") return QColor(0xF2, 0xB8, 0x5A);
  if (s == "iec:automate-8e8s" || s == "iec:variateur" || s == "iec:demarreur" || s == "iec:compteur") return QColor(0x7F, 0xA8, 0xF2);
  if (s == "iec:alim-dc" || s.rfind("iec:transfo", 0) == 0) return QColor(0x86, 0xC9, 0x9A);
  if (roleSymbole(s) == RoleSymbole::Borne) return QColor(0xB8, 0xBE, 0xCC);
  return QColor(0xC3, 0xB3, 0xF0);
}
}  // namespace

VueArmoire::VueArmoire(Document* doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(300, 200);
  connect(m_doc, &Document::change, this, [this] { update(); });
}

const ProjetElectrique* VueArmoire::projet() const {
  const auto& p = m_doc->projet();
  return m_projet >= 0 && m_projet < static_cast<int>(p.electrique.size()) ? &p.electrique[static_cast<std::size_t>(m_projet)] : nullptr;
}

const Armoire* VueArmoire::armoireCourante() const {
  const ProjetElectrique* p = projet();
  return p && m_armoire >= 0 && m_armoire < static_cast<int>(p->armoires.size()) ? &p->armoires[static_cast<std::size_t>(m_armoire)] : nullptr;
}

void VueArmoire::setArmoire(int projet, int armoire) {
  const bool autre = projet != m_projet || armoire != m_armoire;
  m_projet = projet;
  m_armoire = armoire;
  if (autre) {
    m_selection.clear();
    recadrer();
  }
  update();
}

void VueArmoire::setPorte(bool porte) {
  m_porte = porte;
  m_selection.clear();
  emit selectionChangee();
  update();
}

void VueArmoire::selectionner(const std::string& composant) {
  m_selection = composant;
  emit selectionChangee();
  update();
}

void VueArmoire::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  recadrer();
}

void VueArmoire::recadrer() {
  const Armoire* a = armoireCourante();
  if (!a || width() < 20 || height() < 20) return;
  // Toute la plaque, et ce qui déborde dessous
  double bas = a->hauteur;
  for (const auto& im : a->implantations) bas = std::max(bas, mm(im.p.y) + 100);
  m_echelle = std::min((width() - 60) / a->largeur, (height() - 60) / bas);
  m_origine = QPointF((width() - a->largeur * m_echelle) / 2, 30);
  update();
}

std::string VueArmoire::appareilSous(QPointF q) const {
  const ProjetElectrique* p = projet();
  const Armoire* a = armoireCourante();
  if (!p || !a) return {};
  const QPointF w = monde(q);
  for (const auto& b : boitesArmoire(*p, static_cast<std::size_t>(m_armoire))) {
    if (b.porte != m_porte) continue;
    if (w.x() >= mm(b.b.x0) && w.x() <= mm(b.b.x1) && w.y() >= mm(b.b.y0) && w.y() <= mm(b.b.y1)) return b.composant;
  }
  return {};
}

std::pair<QPointF, int> VueArmoire::accrocher(const std::string& composant, QPointF coin) const {
  const ProjetElectrique* p = projet();
  const Armoire* a = armoireCourante();
  if (!p || !a) return {coin, -1};
  const Encombrement e = encombrement(*p, composant);
  // Jamais hors de la plaque (ou de la porte) : l'appareil reste entier dedans
  const auto dedans = [&](QPointF q) {
    return QPointF(std::clamp(q.x(), 0.0, std::max(0.0, a->largeur - e.largeur)), std::clamp(q.y(), 0.0, std::max(0.0, a->hauteur - e.hauteur)));
  };
  QPointF r = dedans(QPointF(std::round(coin.x()), std::round(coin.y())));
  coin = r;
  if (m_porte) return {r, -1};
  // Rail le plus proche du centre de l'appareil, à 40 mm près
  int rail = -1;
  double meilleur = 40;
  for (std::size_t i = 0; i < a->rails.size(); ++i) {
    const double d = std::abs(coin.y() + e.hauteur / 2 - mm(a->rails[i].y));
    if (d < meilleur) {
      meilleur = d;
      rail = static_cast<int>(i);
    }
  }
  if (rail >= 0) {
    const auto& rl = a->rails[static_cast<std::size_t>(rail)];
    r.setY(mm(rl.y) - e.hauteur / 2);
    // Coffret : au module de 18 mm ; plaque : au millimètre, sans sortir du rail
    if (a->tableauModulaire) r.setX(mm(rl.x0) + std::round((coin.x() - mm(rl.x0)) / 18.0) * 18.0);
    r.setX(std::clamp(r.x(), mm(rl.x0), std::max(mm(rl.x0), mm(rl.x1) - e.largeur)));
    if (!e.rail) rail = -1;
  }
  return {r, rail};
}

void VueArmoire::appliquer(const std::string& composant, QPointF coin, int rail) {
  const int pi = m_projet, ai = m_armoire;
  const bool porte = m_porte;
  m_doc->pendantGeste([&](prom::Projet& p) {
    if (pi < 0 || pi >= static_cast<int>(p.electrique.size())) return;
    auto& pe = p.electrique[static_cast<std::size_t>(pi)];
    if (ai < 0 || ai >= static_cast<int>(pe.armoires.size())) return;
    for (auto& im : pe.armoires[static_cast<std::size_t>(ai)].implantations)
      if (im.composant == composant) {
        im.p = {nm(coin.x()), nm(coin.y())};
        im.rail = rail;
        im.porte = porte;
      }
  });
}

void VueArmoire::deplacerVers(const std::string& composant, double xmm, double ymm) {
  const auto [coin, rail] = accrocher(composant, QPointF(xmm, ymm));
  m_doc->debutGeste();
  appliquer(composant, coin, rail);
  m_doc->finGeste();
}

void VueArmoire::retirerSelection() {
  if (m_selection.empty()) return;
  const std::string id = m_selection;
  const int pi = m_projet, ai = m_armoire;
  m_doc->modifier([&](prom::Projet& p) {
    if (pi < 0 || pi >= static_cast<int>(p.electrique.size())) return;
    auto& pe = p.electrique[static_cast<std::size_t>(pi)];
    if (ai < 0 || ai >= static_cast<int>(pe.armoires.size())) return;
    auto& v = pe.armoires[static_cast<std::size_t>(ai)].implantations;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Implantation& im) { return im.composant == id; }), v.end());
  });
  m_selection.clear();
  emit selectionChangee();
}

void VueArmoire::paintEvent(QPaintEvent*) {
  QPainter g(this);
  g.setRenderHint(QPainter::Antialiasing);
  const Theme& th = theme();
  g.fillRect(rect(), th.fond(QColor(0xE9, 0xEC, 0xF3)));
  const ProjetElectrique* p = projet();
  const Armoire* a = armoireCourante();
  if (!p || !a) {
    g.setPen(th.texte(QColor(0x6B, 0x73, 0x93)));
    g.drawText(rect(), Qt::AlignCenter, QStringLiteral("Aucune armoire : crée-en une depuis le ruban (« Nouvelle armoire »)."));
    return;
  }
  auto R = [&](double x0, double y0, double x1, double y1) { return QRectF(versEcran(x0, y0), versEcran(x1, y1)); };
  // Enveloppe et plaque (ou porte)
  const QRectF enveloppe = R(0, 0, a->largeur, a->hauteur);
  g.setPen(QPen(QColor(0x5A, 0x63, 0x80), 2));
  g.setBrush(m_porte ? QColor(0xD9, 0xDE, 0xE8) : QColor(0xF3, 0xEE, 0xDF));
  g.drawRoundedRect(enveloppe, 4, 4);
  QFont f = font();
  f.setPixelSize(12);
  f.setBold(true);
  g.setFont(f);
  g.setPen(th.texte(QColor(0x4A, 0x52, 0x70)));  // lisible dans tous les thèmes
  g.drawText(QRectF(enveloppe.left(), enveloppe.top() - 24, std::max(enveloppe.width(), width() - enveloppe.left() - 8), 20), Qt::AlignLeft | Qt::AlignVCenter,
             QStringLiteral("%1 — %2 · %3 × %4 × %5 mm").arg(QString::fromStdString(a->nom), m_porte ? QStringLiteral("porte") : QStringLiteral("plaque de fond"))
                 .arg(a->largeur)
                 .arg(a->hauteur)
                 .arg(a->profondeur));
  if (!m_porte) {
    // Goulottes (peigne), puis rails DIN de 35 mm
    for (const auto& gl : a->goulottes) {
      const QRectF r = R(mm(gl.b.x0), mm(gl.b.y0), mm(gl.b.x1), mm(gl.b.y1));
      g.setPen(QPen(QColor(0x8A, 0x91, 0xAC), 1));
      g.setBrush(QColor(0xC9, 0xCE, 0xDA));
      g.drawRect(r);
      g.setPen(QPen(QColor(0xAE, 0xB4, 0xC6), 1));
      const bool horizontale = r.width() >= r.height();
      const double pas = 8 * m_echelle;
      if (pas > 3) {
        if (horizontale)
          for (double x = r.left() + pas; x < r.right(); x += pas) g.drawLine(QPointF(x, r.top()), QPointF(x, r.top() + r.height() * 0.3));
        else
          for (double y = r.top() + pas; y < r.bottom(); y += pas) g.drawLine(QPointF(r.left(), y), QPointF(r.left() + r.width() * 0.3, y));
      }
    }
    for (const auto& rl : a->rails) {
      const QRectF r = R(mm(rl.x0), mm(rl.y) - 17.5, mm(rl.x1), mm(rl.y) + 17.5);
      g.setPen(QPen(QColor(0x7A, 0x80, 0x94), 1));
      g.setBrush(QColor(0xD8, 0xDB, 0xE2));
      g.drawRect(r);
      g.setPen(QPen(QColor(0xA9, 0xAE, 0xBC), 1));
      g.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
    }
  }
  // Appareils
  QFont fr = font();
  QFont fd = font();
  for (const auto& b : boitesArmoire(*p, static_cast<std::size_t>(m_armoire))) {
    if (b.porte != m_porte) continue;
    const prom::ecad::ComposantElec* c = p->schema.composant(b.composant);
    if (!c) continue;
    const QRectF r = R(mm(b.b.x0), mm(b.b.y0), mm(b.b.x1), mm(b.b.y1));
    const bool choisi = b.composant == m_selection;
    const bool hors = mm(b.b.y0) > a->hauteur || mm(b.b.x1) > a->largeur + 0.1;
    g.setPen(QPen(choisi ? QColor(0x5B, 0x4B, 0xF5) : hors ? QColor(0xC2, 0x40, 0x2E) : QColor(0x3A, 0x40, 0x58), choisi ? 2.4 : 1.1));
    g.setBrush(couleurFamille(c->symbole));
    const Encombrement e = encombrement(*p, b.composant);
    if (m_porte && e.porte && std::abs(e.largeur - e.hauteur) < 1)
      g.drawEllipse(r);
    else
      g.drawRoundedRect(r, 2, 2);
    // Bornier : une ligne par borne
    if (roleSymbole(c->symbole) == RoleSymbole::Borne && r.width() > 20) {
      g.setPen(QPen(QColor(0x8A, 0x91, 0xAC), 0.8));
      const double pas = 5.2 * m_echelle;
      for (double x = r.left() + 9 * m_echelle + pas; x < r.right() - 9 * m_echelle; x += pas) g.drawLine(QPointF(x, r.top() + 2), QPointF(x, r.bottom() - 2));
    }
    const QString ref = QString::fromStdString(repereAffiche(*p, *c));
    fr.setPixelSize(std::clamp(static_cast<int>(std::min(r.width() / std::max<qsizetype>(3, ref.size()) * 1.5, r.height() / 4)), 7, 16));
    fr.setBold(true);
    g.setFont(fr);
    g.setPen(QColor(0x10, 0x14, 0x2A));
    g.save();
    if (r.width() < r.height() * 0.5 && r.width() < QFontMetricsF(fr).horizontalAdvance(ref) + 4) {
      // Appareil étroit (un module) : repère vertical
      g.translate(r.center());
      g.rotate(-90);
      g.drawText(QRectF(-r.height() / 2, -r.width() / 2, r.height(), r.width()), Qt::AlignCenter, ref);
    } else {
      g.drawText(r.adjusted(2, 2, -2, -r.height() / 2), Qt::AlignCenter, ref);
      const QString car = QString::fromStdString(caracteristiques(*c));
      if (!car.isEmpty() && r.height() > 30) {
        fd.setPixelSize(std::clamp(static_cast<int>(r.height() / 7), 7, 12));
        g.setFont(fd);
        g.setPen(QColor(0x2A, 0x30, 0x48));
        g.drawText(r.adjusted(2, r.height() / 2, -2, -2), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, car);
      }
    }
    g.restore();
  }
  // Ce qui reste à implanter
  int restants = 0;
  {
    std::set<std::string> poses;
    for (const auto& im : a->implantations) poses.insert(im.composant);
    for (const auto& id : appareilsArmoire(*p, *a)) restants += poses.count(id) ? 0 : 1;
  }
  if (restants > 0) {
    QFont fi = font();
    fi.setPixelSize(12);
    g.setFont(fi);
    g.setPen(QColor(0xA8, 0x62, 0x0E));
    g.drawText(QRectF(enveloppe.left(), enveloppe.bottom() + 6, enveloppe.width(), 20), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1 appareil(s) du schéma pas encore dans l’armoire : « Implanter » les place.").arg(restants));
  }
}

void VueArmoire::mousePressEvent(QMouseEvent* e) {
  setFocus();
  m_souris = m_pression = e->position();
  if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
    m_panoramique = true;
    setCursor(Qt::ClosedHandCursor);
    return;
  }
  if (e->button() != Qt::LeftButton) return;
  const std::string o = appareilSous(e->position());
  if (o != m_selection) selectionner(o);
  m_appuye = !o.empty();
  if (m_appuye) {
    const ProjetElectrique* p = projet();
    for (const auto& b : boitesArmoire(*p, static_cast<std::size_t>(m_armoire)))
      if (b.composant == o) m_decalage = monde(e->position()) - QPointF(mm(b.b.x0), mm(b.b.y0));
  }
}

void VueArmoire::mouseMoveEvent(QMouseEvent* e) {
  const QPointF avant = m_souris;
  m_souris = e->position();
  if (m_panoramique) {
    m_origine += m_souris - avant;
    update();
    return;
  }
  if (m_appuye && !m_selection.empty()) {
    if (!m_glisse && (m_souris - m_pression).manhattanLength() > 4) {
      m_glisse = true;
      m_doc->debutGeste();
    }
    if (m_glisse) {
      const auto [coin, rail] = accrocher(m_selection, monde(m_souris) - m_decalage);
      appliquer(m_selection, coin, rail);
      emit message(rail >= 0 ? QStringLiteral("Sur le rail %1 · x = %2 mm").arg(rail + 1).arg(coin.x()) : QStringLiteral("Fixé sur la plaque · x = %1 mm, y = %2 mm").arg(coin.x()).arg(coin.y()));
    }
    return;
  }
  const std::string o = appareilSous(m_souris);
  setCursor(o.empty() ? Qt::ArrowCursor : Qt::OpenHandCursor);
  if (!o.empty())
    if (const ProjetElectrique* p = projet())
      if (const auto* c = p->schema.composant(o))
        setToolTip(QStringLiteral("<b>%1</b><br>%2").arg(QString::fromStdString(repereAffiche(*p, *c)), QString::fromStdString(c->description).toHtmlEscaped()));
}

void VueArmoire::mouseReleaseEvent(QMouseEvent*) {
  m_appuye = false;
  if (m_panoramique) {
    m_panoramique = false;
    setCursor(Qt::ArrowCursor);
    return;
  }
  if (m_glisse) {
    m_glisse = false;
    m_doc->finGeste();
  }
}

void VueArmoire::wheelEvent(QWheelEvent* e) {
  const double f = std::pow(1.0015, e->angleDelta().y());
  const double nouvelle = std::clamp(m_echelle * f, 0.1, 30.0);
  const QPointF q = e->position();
  m_origine = q - (q - m_origine) * (nouvelle / m_echelle);
  m_echelle = nouvelle;
  update();
}

void VueArmoire::keyPressEvent(QKeyEvent* e) {
  switch (e->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace: retirerSelection(); return;
    case Qt::Key_F:
    case Qt::Key_Home: recadrer(); return;
    case Qt::Key_Escape: selectionner({}); return;
    default: QWidget::keyPressEvent(e);
  }
}

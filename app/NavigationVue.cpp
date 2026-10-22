// Prométhée : navigation commune aux canevas du schéma et du circuit. Voir NavigationVue.hpp.
// Licence GPL-3.0-only.
#include "NavigationVue.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointingDevice>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include "Icones.hpp"
#include "RegistreActions.hpp"

namespace {
constexpr double PI = std::numbers::pi;
QString cle(const char* nom) { return QStringLiteral("navigation/") + QLatin1String(nom); }
}  // namespace

// ---------- Réglages ----------
Navigation& Navigation::instance() {
  static Navigation n;
  return n;
}

Navigation::Navigation() {
  const QSettings s;
  const ReglagesNavigation d;
  m_r.zoomCran = std::clamp(s.value(cle("zoomCran"), d.zoomCran).toDouble(), 1.1, 2.0);
  m_r.moletteInverse = s.value(cle("moletteInverse"), d.moletteInverse).toBool();
  m_r.defilementCroise = s.value(cle("defilementCroise"), d.defilementCroise).toBool();
  m_r.zoomSurCurseur = s.value(cle("zoomSurCurseur"), d.zoomSurCurseur).toBool();
  m_r.defilementBord = s.value(cle("defilementBord"), d.defilementBord).toBool();
  m_r.deuxDoigts = static_cast<ReglagesNavigation::DeuxDoigts>(std::clamp(s.value(cle("deuxDoigts"), 0).toInt(), 0, 1));
  m_r.gestes = static_cast<ReglagesNavigation::BoutonGestes>(std::clamp(s.value(cle("boutonGestes"), 0).toInt(), 0, 2));
}

void Navigation::setReglages(const ReglagesNavigation& r) {
  if (r == m_r) return;
  m_r = r;
  m_r.zoomCran = std::clamp(m_r.zoomCran, 1.1, 2.0);
  QSettings s;
  s.setValue(cle("zoomCran"), m_r.zoomCran);
  s.setValue(cle("moletteInverse"), m_r.moletteInverse);
  s.setValue(cle("defilementCroise"), m_r.defilementCroise);
  s.setValue(cle("zoomSurCurseur"), m_r.zoomSurCurseur);
  s.setValue(cle("defilementBord"), m_r.defilementBord);
  s.setValue(cle("deuxDoigts"), static_cast<int>(m_r.deuxDoigts));
  s.setValue(cle("boutonGestes"), static_cast<int>(m_r.gestes));
  emit change();
}

// ---------- Vue ----------
NavigationVue::Secteurs NavigationVue::secteursOrigine(const std::string& miroir) {
  return {{{"historique.retablir", QStringLiteral("Rétablir"), QStringLiteral("retablir-fleche")},
           {"objet.tourner", QStringLiteral("Tourner"), QStringLiteral("pivoter")},
           {"vue.zoomSelection", QStringLiteral("Zoom sur la sélection"), QStringLiteral("zoom-plus")},
           {miroir, QStringLiteral("Miroir"), QStringLiteral("symetrie")},
           {"historique.annuler", QStringLiteral("Annuler"), QStringLiteral("annuler-fleche")},
           {"selection.supprimer", QStringLiteral("Supprimer"), QStringLiteral("supprimer")},
           {"vue.toutVoir", QStringLiteral("Tout voir"), QStringLiteral("recadrer")},
           {"edition.dupliquer", QStringLiteral("Dupliquer"), QStringLiteral("repetition-lineaire")}}};
}

int NavigationVue::secteurVers(QPointF d) {
  if (std::hypot(d.x(), d.y()) < TRAIT) return -1;
  double a = std::atan2(-d.y(), d.x());
  if (a < 0) a += 2 * PI;
  return static_cast<int>(std::floor((a + PI / 8) / (PI / 4))) % 8;
}

NavigationVue::NavigationVue(QWidget* vue, Secteurs secteurs, std::function<void(const std::string&)> faire, std::function<void(QPointF)> decaler)
    : QObject(vue), m_vue(vue), m_secteurs(std::move(secteurs)), m_faire(std::move(faire)), m_decaler(std::move(decaler)), m_attente(new QTimer(this)), m_defilement(new QTimer(this)) {
  // Bouton tenu immobile : le menu radial paraît, et sert d'aide
  m_attente->setSingleShot(true);
  m_attente->setInterval(ATTENTE_MENU);
  connect(m_attente, &QTimer::timeout, this, [this] {
    if (m_mode == Mode::Aucun || m_bouge || m_trace) return;
    m_menu = true;
    m_secteur = -1;
    m_vue->update();
  });
  // L'annonce s'efface d'elle-même ; une suite de touches non complétée cesse avec elle
  m_finAnnonce = new QTimer(this);
  m_finAnnonce->setSingleShot(true);
  connect(m_finAnnonce, &QTimer::timeout, this, [this] {
    m_annonce.clear();
    m_prefixe.reset();
    m_vue->update();
  });
  // Défilement au bord : un pas toutes les 30 ms, d'autant plus grand que le pointeur est près du bord
  m_defilement->setInterval(30);
  connect(m_defilement, &QTimer::timeout, this, [this] {
    const double w = m_vue->width(), h = m_vue->height();
    QPointF d;
    if (m_souris.x() < BORD) d.rx() = (BORD - m_souris.x()) * 0.5;
    else if (m_souris.x() > w - BORD) d.rx() = -(m_souris.x() - (w - BORD)) * 0.5;
    if (m_souris.y() < BORD) d.ry() = (BORD - m_souris.y()) * 0.5;
    else if (m_souris.y() > h - BORD) d.ry() = -(m_souris.y() - (h - BORD)) * 0.5;
    if (d.isNull()) m_defilement->stop();
    else m_decaler(d);
  });
}

bool NavigationVue::boutonGestes(const QMouseEvent* e) const {
  switch (Navigation::instance().reglages().gestes) {
    case ReglagesNavigation::BoutonGestes::DroitAvecCtrl: return e->button() == Qt::RightButton && (e->modifiers() & Qt::ControlModifier);
    case ReglagesNavigation::BoutonGestes::Lateral: return e->button() == Qt::BackButton || e->button() == Qt::ForwardButton;
    case ReglagesNavigation::BoutonGestes::Milieu: return e->button() == Qt::MiddleButton;
  }
  return false;
}

bool NavigationVue::appui(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton) return false;
  const bool gestes = boutonGestes(e);
  if (gestes) m_mode = Mode::Gestes;
  else if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) m_mode = Mode::Deplacer;
  else return false;
  m_bouton = e->button();
  m_depart = m_dernier = m_souris = e->position();
  m_bouge = m_trace = m_menu = false;
  m_secteur = -1;
  if (gestes || e->button() == Qt::RightButton) m_attente->start();
  return true;
}

bool NavigationVue::deplacer(QMouseEvent* e) {
  if (m_mode == Mode::Aucun) return false;
  m_souris = e->position();
  const QPointF total = m_souris - m_depart;
  if (m_menu) {
    const int s = secteurVers(total);
    if (s != m_secteur) {
      m_secteur = s;
      m_vue->update();
    }
    return true;
  }
  if (m_mode == Mode::Gestes) {
    // Tracé rapide : la commande part dès que la direction est sûre, sans attendre le menu
    if (!m_trace && std::hypot(total.x(), total.y()) >= TRAIT) {
      m_attente->stop();
      m_trace = true;
      lancer(secteurVers(total));
    }
    return true;
  }
  if (!m_bouge && total.manhattanLength() > 4) {
    m_bouge = true;
    m_attente->stop();
  }
  if (m_bouge) m_decaler(m_souris - m_dernier);
  m_dernier = m_souris;
  return true;
}

NavigationVue::Fin NavigationVue::relacher(QMouseEvent* e) {
  if (m_mode == Mode::Aucun || e->button() != m_bouton) return Fin::Rien;
  m_attente->stop();
  const Mode mode = std::exchange(m_mode, Mode::Aucun);
  if (m_menu) {
    // Relâché dans un secteur : sa commande ; dans la zone morte : le clic droit court
    m_menu = false;
    const int s = secteurVers(e->position() - m_depart);
    m_vue->update();
    if (s >= 0) {
      lancer(s);
      return Fin::Pris;
    }
    return m_bouton == Qt::RightButton ? Fin::ClicDroitCourt : Fin::Pris;
  }
  if (mode == Mode::Gestes) return !m_trace && m_bouton == Qt::RightButton ? Fin::ClicDroitCourt : Fin::Pris;
  return !m_bouge && m_bouton == Qt::RightButton ? Fin::ClicDroitCourt : Fin::Pris;
}

void NavigationVue::lancer(int secteur) {
  if (secteur < 0 || secteur >= static_cast<int>(m_secteurs.size()) || m_secteurs[static_cast<std::size_t>(secteur)].action.empty()) return;
  m_faire(m_secteurs[static_cast<std::size_t>(secteur)].action);
}

void NavigationVue::molette(QWheelEvent* e, QPointF& origine, double& echelle, double min, double max) const {
  const ReglagesNavigation& r = Navigation::instance().reglages();
  // Pavé tactile : son type d'appareil, sinon un défilement donné en pixels
  const QPointingDevice* appareil = e->pointingDevice();
  const bool pave = (appareil && appareil->type() == QInputDevice::DeviceType::TouchPad) || !e->pixelDelta().isNull();
  if (pave && r.deuxDoigts == ReglagesNavigation::DeuxDoigts::Defiler) {
    // Deux doigts font défiler : le dessin suit les doigts, l'échelle ne change pas
    origine += !e->pixelDelta().isNull() ? QPointF(e->pixelDelta()) : QPointF(e->angleDelta()) / 8.0;
    return;
  }
  // Maj + molette : défilement vertical ; Ctrl + molette : horizontal (ou l'inverse, réglé) ; 60 pixels par cran.
  // Certains systèmes rendent Maj + molette en défilement horizontal : le cran est pris où il est.
  if (!pave && (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) {
    const QPoint d = e->angleDelta();
    const double pas = 60.0 * (d.y() != 0 ? d.y() : d.x()) / 120.0;
    const bool vertical = ((e->modifiers() & Qt::ShiftModifier) != 0) != r.defilementCroise;
    if (vertical) origine.ry() += pas;
    else origine.rx() += pas;
    return;
  }
  double crans = e->angleDelta().y() / 120.0;
  if (crans == 0 && !e->pixelDelta().isNull()) crans = e->pixelDelta().y() / 50.0;
  if (r.moletteInverse) crans = -crans;
  const double nouvelle = std::clamp(echelle * std::pow(r.zoomCran, crans), min, max);
  const QPointF p = r.zoomSurCurseur ? e->position() : QPointF(m_vue->width() / 2.0, m_vue->height() / 2.0);
  origine = p - (p - origine) * (nouvelle / echelle);
  echelle = nouvelle;
}

bool NavigationVue::pincer(QNativeGestureEvent* e, QPointF& origine, double& echelle, double min, double max) const {
  if (e->gestureType() != Qt::ZoomNativeGesture) return false;
  const double nouvelle = std::clamp(echelle * (1.0 + e->value()), min, max);
  const QPointF p = e->position();
  origine = p - (p - origine) * (nouvelle / echelle);
  echelle = nouvelle;
  return true;
}

void NavigationVue::glisserGauche(QPointF souris, bool glisse) {
  m_souris = souris;
  const bool auBord = souris.x() < BORD || souris.y() < BORD || souris.x() > m_vue->width() - BORD || souris.y() > m_vue->height() - BORD;
  if (glisse && auBord && Navigation::instance().reglages().defilementBord) {
    if (!m_defilement->isActive()) m_defilement->start();
  } else {
    m_defilement->stop();
  }
}

void dessinerMenuRadial(QPainter& g, QPointF centre, int surligne, const std::array<EntreeRadiale, 8>& entrees, const QFont& police) {
  const double R = 92, r = NavigationVue::TRAIT + 4;
  g.save();
  g.setRenderHint(QPainter::Antialiasing);
  g.setPen(Qt::NoPen);
  g.setBrush(QColor(24, 33, 29, 205));
  QPainterPath anneau;
  anneau.addEllipse(centre, R, R);
  anneau.addEllipse(centre, r, r);
  g.drawPath(anneau);
  for (int i = 0; i < 8; ++i) {
    const double a = i * PI / 4;
    if (i == surligne) {
      QPainterPath part;
      part.moveTo(centre);
      part.arcTo(QRectF(centre.x() - R, centre.y() - R, 2 * R, 2 * R), (a - PI / 8) * 180 / PI, 45);
      part.closeSubpath();
      QPainterPath trou;
      trou.addEllipse(centre, r, r);
      g.setBrush(QColor(0x5B, 0x4B, 0xF5, 230));
      g.drawPath(part.subtracted(trou));
    }
    const EntreeRadiale& e = entrees[static_cast<std::size_t>(i)];
    const QPointF c = centre + QPointF(std::cos(a) * 62, -std::sin(a) * 62);
    g.drawPixmap(QRectF(c.x() - 13, c.y() - 17, 26, 26), icone(e.icone).pixmap(52, 52), QRectF(0, 0, 52, 52));
    QFont f = police;
    f.setPixelSize(10);
    f.setBold(true);
    g.setFont(f);
    g.setPen(Qt::white);
    g.drawText(QRectF(c.x() - 45, c.y() + 9, 90, 14), Qt::AlignCenter, e.libelle);
    g.setPen(Qt::NoPen);
  }
  g.restore();
}

// Le menu radial autour du point d'appui ; l'annonce à 16 px du curseur, sur un cartouche sombre
void NavigationVue::dessiner(QPainter& g, QPointF souris) const {
  if (m_menu) {
    std::array<EntreeRadiale, 8> entrees;
    for (std::size_t i = 0; i < entrees.size(); ++i) entrees[i] = {m_secteurs[i].icone, m_secteurs[i].libelle};
    dessinerMenuRadial(g, m_depart, m_secteur, entrees, m_vue->font());
  }
  if (m_annonce.isEmpty()) return;
  g.save();
  QFont f = m_vue->font();
  f.setPixelSize(12);
  f.setBold(true);
  g.setFont(f);
  const QFontMetricsF fm(f);
  QRectF r(souris + QPointF(16, 16), QSizeF(fm.horizontalAdvance(m_annonce) + 16, fm.height() + 8));
  if (r.right() > m_vue->width() - 4) r.moveRight(souris.x() - 8);
  if (r.bottom() > m_vue->height() - 4) r.moveBottom(souris.y() - 8);
  g.setRenderHint(QPainter::Antialiasing);
  g.setPen(Qt::NoPen);
  g.setBrush(QColor(0x10, 0x14, 0x2A, 225));
  g.drawRoundedRect(r, 6, 6);
  g.setPen(QColor(0xE9, 0xEE, 0xFF));
  g.drawText(r, Qt::AlignCenter, m_annonce);
  g.restore();
}

void NavigationVue::annoncer(const QString& texte, int duree) {
  m_annonce = texte;
  m_finAnnonce->start(duree);
  m_vue->update();
}

void NavigationVue::attendreSuite(QKeyCombination premiere, const QString& suites) {
  m_prefixe = premiere;
  annoncer(suites, 1500);
}

QString NavigationVue::texteSuites(QKeyCombination premiere, const std::vector<const ActionAtelier*>& suites) {
  QStringList l;
  for (const ActionAtelier* a : suites)
    for (const QKeySequence& s : a->raccourcis)
      if (s.count() == 2) l << RegistreActions::nomTouche(QKeySequence(s[1])) + QStringLiteral(" : ") + a->nomAffiche();
  return RegistreActions::nomTouche(QKeySequence(premiere)) + QStringLiteral("… ") + l.join(QStringLiteral(" · "));
}

void NavigationVue::finirSuite() {
  m_prefixe.reset();
  m_annonce.clear();
  m_finAnnonce->stop();
  m_vue->update();
}

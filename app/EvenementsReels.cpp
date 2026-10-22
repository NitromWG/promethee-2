// Prométhée : souris et clavier réels pour les essais de l'interface. Licence GPL-3.0-only.
#include "EvenementsReels.hpp"

#include <QApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QEvent>
#include <QPointer>
#include <QStyleHints>
#include <QWidget>
#include <QWindow>

#include <utility>

// Points d'entrée de QtGui par lesquels passent aussi les essais de Qt (QTest) : l'événement suit le chemin d'un
// événement du système de fenêtres (widget sous le pointeur, capture, double-clic, raccourcis)
QT_BEGIN_NAMESPACE
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow* window, const QPointF& local, const QPointF& global, Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type,
                                      Qt::KeyboardModifiers mods, int timestamp);
Q_GUI_EXPORT void qt_handleKeyEvent(QWindow* w, QEvent::Type t, int k, Qt::KeyboardModifiers mods, const QString& text, bool autorep, ushort count);
QT_END_NAMESPACE

namespace reel {
namespace {
Qt::MouseButtons g_tenus;
int g_horodatage = 1;
QStringList g_deplacements;
int g_gestes = 0;
int g_touches = 0;

// Le pointeur relevé autour d'un geste (ERG-18) : le système ne le bouge pas pour un événement simulé, seule une
// vue qui appellerait QCursor::setPos le ferait
class Releve {
 public:
  Releve(QWidget* w, const char* quoi) : m_avant(QCursor::pos()), m_quoi(quoi), m_classe(w ? w->metaObject()->className() : "?") {}
  ~Releve() {
    ++g_gestes;
    if (QCursor::pos() != m_avant) g_deplacements << QStringLiteral("%1 sur %2").arg(QLatin1String(m_quoi), QLatin1String(m_classe));
  }
  Releve(const Releve&) = delete;
  Releve& operator=(const Releve&) = delete;

 private:
  QPoint m_avant;
  const char* m_quoi;
  const char* m_classe;
};

const char* nomEvenement(QEvent::Type t) {
  switch (t) {
    case QEvent::MouseButtonPress: return "appui";
    case QEvent::MouseButtonRelease: return "relâcher";
    case QEvent::MouseMove: return "déplacement";
    default: return "souris";
  }
}

QWindow* fenetre(QWidget* w) {
  QWidget* haut = w->window();
  if (!haut->windowHandle()) haut->winId();  // crée la fenêtre native si besoin
  return haut->windowHandle();
}

void souris(QWidget* w, QPoint p, Qt::MouseButton bouton, QEvent::Type type, Qt::KeyboardModifiers mods) {
  QWindow* f = fenetre(w);
  if (!f) return;
  const QPoint local = w->mapTo(w->window(), p);
  const QPointF global = w->mapToGlobal(QPointF(p));
  const Releve releve(w, nomEvenement(type));
  g_horodatage += 16;
  qt_handleMouseEvent(f, local, global, g_tenus, bouton, type, mods, g_horodatage);
  QCoreApplication::processEvents();
}
}  // namespace

void appuyer(QWidget* w, QPoint p, Qt::MouseButton bouton, Qt::KeyboardModifiers mods) {
  g_tenus |= bouton;
  souris(w, p, bouton, QEvent::MouseButtonPress, mods);
}

void relacher(QWidget* w, QPoint p, Qt::MouseButton bouton, Qt::KeyboardModifiers mods) {
  g_tenus &= ~Qt::MouseButtons(bouton);
  souris(w, p, bouton, QEvent::MouseButtonRelease, mods);
  // Un appui suivant ne fera pas un double-clic par hasard
  g_horodatage += QGuiApplication::styleHints()->mouseDoubleClickInterval() + 1;
}

void deplacer(QWidget* w, QPoint p, Qt::KeyboardModifiers mods) { souris(w, p, Qt::NoButton, QEvent::MouseMove, mods); }

// Un geste peut détruire le widget visé (panneau reconstruit) : la suite du geste va alors à la fenêtre, à la
// même position, comme le ferait le système
void cliquer(QWidget* w, QPoint p, Qt::MouseButton bouton, Qt::KeyboardModifiers mods) {
  QPointer<QWidget> garde(w);
  QWidget* haut = w->window();
  const QPoint dansHaut = w->mapTo(haut, p);
  deplacer(w, p, mods);
  if (!garde) return;
  appuyer(w, p, bouton, mods);
  if (garde) relacher(w, p, bouton, mods);
  else relacher(haut, dansHaut, bouton, mods);
}

void doubleCliquer(QWidget* w, QPoint p, Qt::KeyboardModifiers mods) {
  QPointer<QWidget> garde(w);
  QWidget* haut = w->window();
  const QPoint dansHaut = w->mapTo(haut, p);
  auto suite = [&]() -> std::pair<QWidget*, QPoint> { return garde ? std::pair<QWidget*, QPoint>{w, p} : std::pair<QWidget*, QPoint>{haut, dansHaut}; };
  deplacer(w, p, mods);
  // Deux appuis rapprochés au même endroit : Qt en fait lui-même le double-clic
  g_tenus |= Qt::LeftButton;
  souris(suite().first, suite().second, Qt::LeftButton, QEvent::MouseButtonPress, mods);
  g_tenus &= ~Qt::MouseButtons(Qt::LeftButton);
  souris(suite().first, suite().second, Qt::LeftButton, QEvent::MouseButtonRelease, mods);
  g_tenus |= Qt::LeftButton;
  souris(suite().first, suite().second, Qt::LeftButton, QEvent::MouseButtonPress, mods);
  relacher(suite().first, suite().second, Qt::LeftButton, mods);
}

void glisser(QWidget* w, QPoint a, QPoint b, Qt::KeyboardModifiers mods, int etapes) {
  QPointer<QWidget> garde(w);
  deplacer(w, a, mods);
  if (!garde) return;
  appuyer(w, a, Qt::LeftButton, mods);
  for (int i = 1; i <= etapes && garde; ++i) deplacer(w, a + (b - a) * i / etapes, mods);
  if (garde) relacher(w, b, Qt::LeftButton, mods);
  else g_tenus &= ~Qt::MouseButtons(Qt::LeftButton);
}

void touche(QWidget* w, int cle, Qt::KeyboardModifiers mods, const QString& texte) {
  QWindow* f = fenetre(w);
  if (!f) return;
  QPointer<QWidget> garde(w);  // la touche peut fermer le champ qui la reçoit (Tab, Entrée)
  const Releve releve(w, "touche");
  ++g_touches;
  qt_handleKeyEvent(f, QEvent::KeyPress, cle, mods, texte, false, 1);
  QCoreApplication::processEvents();
  if (!garde || !fenetre(w)) return;
  qt_handleKeyEvent(f, QEvent::KeyRelease, cle, mods, texte, false, 1);
  QCoreApplication::processEvents();
}

void taper(QWidget* w, const QString& texte) {
  for (const QChar c : texte) {
    const int cle = c.isLetterOrNumber() ? c.toUpper().unicode() : c.unicode();
    touche(w, cle, c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier, QString(c));
  }
}

bool activer(QWidget* w) {
  QWidget* haut = w->window();
  haut->show();
  haut->raise();
  haut->activateWindow();
  QElapsedTimer t;
  t.start();
  while (QApplication::activeWindow() != haut && t.elapsed() < 2000) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  return QApplication::activeWindow() == haut;
}

QPoint pointeur() { return QCursor::pos(); }
QStringList pointeurDeplace() { return g_deplacements; }
int gestesReleves() { return g_gestes; }
int touchesEnvoyees() { return g_touches; }

}  // namespace reel

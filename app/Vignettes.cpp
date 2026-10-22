// Prométhée : vignettes des feuilles (lot L2, tâche 2.30). Licence GPL-3.0-only.
#include "Vignettes.hpp"

#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "Theme.hpp"
#include "VueSchema.hpp"
#include "promethee/commandes.hpp"
#include "promethee/modele.hpp"

Vignettes& Vignettes::instance() {
  static Vignettes* v = new Vignettes;
  return *v;
}

void Vignettes::suivre(Document* doc) {
  if (!doc || doc->property("vignettesSuivies").toBool()) return;
  doc->setProperty("vignettesSuivies", true);
  // Une conception changée : ses vignettes seules sont à refaire ; un projet remplacé : toutes
  connect(doc, &Document::conceptionChangee, this, [this](const prom::ecad::ChangementConception& ch) {
    if (!ch.schema && !ch.definitions && !ch.proprietes.count("valeur") && !ch.proprietes.count("reference")) return;
    const QString debut = QString::fromStdString(ch.conception) + QLatin1Char('|');
    for (auto it = m_cache.begin(); it != m_cache.end();) it = it->first.startsWith(debut) ? m_cache.erase(it) : std::next(it);
    for (auto it = m_fichiers.begin(); it != m_fichiers.end();) it = it->first.startsWith(debut) ? m_fichiers.erase(it) : std::next(it);
  });
  for (auto signal : {&Document::projetRemplace, &Document::change})
    connect(doc, signal, this, [this] {
      m_cache.clear();
      m_fichiers.clear();
    });
}

QPixmap Vignettes::vignette(Document* doc, int conception, int feuille, QSize taille) {
  if (!doc || conception < 0 || conception >= static_cast<int>(doc->projet().electronique.size())) return {};
  const prom::ecad::ConceptionElec& c = doc->projet().electronique[static_cast<std::size_t>(conception)];
  if (feuille < 0 || feuille >= static_cast<int>(c.feuilles.size())) return {};
  suivre(doc);
  const QString cle = QStringLiteral("%1|%2|%3x%4").arg(QString::fromStdString(c.uuid), QString::fromStdString(c.feuilles[static_cast<std::size_t>(feuille)].uuid)).arg(taille.width()).arg(taille.height());
  if (const auto it = m_cache.find(cle); it != m_cache.end()) return it->second;
  // La feuille dessinée par une vue hors écran, cadrée sur son dessin, sans ses enfants (bandeau, barres)
  VueSchema v(doc);
  v.setAttribute(Qt::WA_DontShowOnScreen);
  v.resize(taille);
  v.setConception(conception, feuille);
  v.recadrer();
  QPixmap image(taille);
  image.fill(theme().c().surface);
  v.render(&image, QPoint(), QRegion(), QWidget::DrawWindowBackground);
  QPainter g(&image);
  g.setPen(QPen(theme().c().bordure, 1));
  g.drawRect(QRectF(image.rect()).adjusted(0.5, 0.5, -0.5, -0.5));
  g.end();
  ++m_dessinees;
  m_cache[cle] = image;
  return image;
}

QString Vignettes::fichier(Document* doc, int conception, int feuille, QSize taille) {
  const QPixmap p = vignette(doc, conception, feuille, taille);
  if (p.isNull()) return {};
  const QString cle = QStringLiteral("%1-%2-%3").arg(conception).arg(feuille).arg(p.cacheKey());
  if (const auto it = m_fichiers.find(cle); it != m_fichiers.end() && QFile::exists(it->second)) return it->second;
  QDir d(QDir::temp().filePath(QStringLiteral("promethee-vignettes")));
  if (!d.exists() && !QDir().mkpath(d.path())) return {};
  const QString chemin = d.filePath(QStringLiteral("%1.png").arg(cle));
  if (!p.save(chemin, "PNG")) return {};
  // La clé garde l'identifiant de la conception en tête, pour l'invalidation
  if (conception >= 0 && conception < static_cast<int>(doc->projet().electronique.size()))
    m_fichiers[QString::fromStdString(doc->projet().electronique[static_cast<std::size_t>(conception)].uuid) + QLatin1Char('|') + cle] = chemin;
  return chemin;
}

void Vignettes::ouvrirMosaique(AtelierElectronique& a) {
  const prom::ecad::ConceptionElec* c = a.conceptionCourante();
  if (!c) return;
  auto* d = new QDialog(&a);
  d->setObjectName(QStringLiteral("mosaique"));
  d->setWindowTitle(QStringLiteral("Mosaïque des feuilles · %1").arg(QString::fromStdString(c->nom)));
  d->setAttribute(Qt::WA_DeleteOnClose);
  auto* l = new QVBoxLayout(d);
  l->addWidget(new QLabel(QStringLiteral("Un clic ouvre la feuille.")));
  auto* liste = new QListWidget(d);
  liste->setObjectName(QStringLiteral("feuillesMosaique"));
  liste->setViewMode(QListView::IconMode);
  liste->setIconSize(QSize(200, 140));
  liste->setResizeMode(QListView::Adjust);
  liste->setMovement(QListView::Static);
  liste->setSpacing(10);
  for (std::size_t f = 0; f < c->feuilles.size(); ++f) {
    auto* it = new QListWidgetItem(QIcon(Vignettes::instance().vignette(a.document(), a.conception(), static_cast<int>(f))), QString::fromStdString(c->feuilles[f].nom), liste);
    it->setData(Qt::UserRole, static_cast<int>(f));
  }
  QObject::connect(liste, &QListWidget::itemClicked, d, [&a, d](QListWidgetItem* it) {
    a.ouvrir(a.conception(), AtelierElectronique::Schema, it->data(Qt::UserRole).toInt());
    d->close();
  });
  l->addWidget(liste);
  d->resize(760, 520);
  d->show();
}

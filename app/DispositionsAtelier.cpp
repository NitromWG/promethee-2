// Prométhée : panneaux d'un atelier et leurs dispositions nommées (lot L1, tâche 1.21). Voir DispositionsAtelier.hpp.
// Licence GPL-3.0-only.
#include "DispositionsAtelier.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

#include "Document.hpp"
#include "promethee/modele.hpp"

namespace {
const QString FORMAT = QStringLiteral("promethee-disposition");
}  // namespace

DispositionsAtelier::DispositionsAtelier(QMainWindow* fenetre, Document* doc, QString cle, std::function<void()> origine, QObject* parent)
    : QObject(parent), m_fenetre(fenetre), m_doc(doc), m_cle(std::move(cle)), m_origine(std::move(origine)), m_retenue(new QTimer(this)) {
  m_retenue->setSingleShot(true);
  m_retenue->setInterval(400);
  connect(m_retenue, &QTimer::timeout, this, &DispositionsAtelier::retenir);
  m_fenetre->installEventFilter(this);
}

QString DispositionsAtelier::cleReglage(const char* suffixe) const { return m_cle + QLatin1Char('/') + QLatin1String(suffixe); }

void DispositionsAtelier::suivre(QDockWidget* d) {
  m_panneaux.push_back(d);
  d->installEventFilter(this);
  auto changer = [this] {
    if (m_montree && !m_application && !m_gel) m_retenue->start();
  };
  connect(d, &QDockWidget::topLevelChanged, this, changer);
  connect(d, &QDockWidget::dockLocationChanged, this, changer);
}

void DispositionsAtelier::setComplement(std::function<QJsonObject()> lire, std::function<void(const QJsonObject&)> appliquer) {
  m_lireComplement = std::move(lire);
  m_appliquerComplement = std::move(appliquer);
}

void DispositionsAtelier::setOuverture(std::function<bool(const QDockWidget*)> estOuvert, std::function<void(QDockWidget*, bool)> ouvrir) {
  m_estOuvert = std::move(estOuvert);
  m_ouvrir = std::move(ouvrir);
}

// La première apparition reprend la disposition de la dernière session, sinon celle d'origine ; ensuite, tout
// déplacement, redimensionnement, ancrage ou détachement d'un panneau est retenu (après 400 ms de calme)
bool DispositionsAtelier::eventFilter(QObject* o, QEvent* e) {
  if (o == m_fenetre && e->type() == QEvent::Show && !m_montree) {
    m_montree = true;
    m_application = true;
    if (!reprendre()) m_origine();
    m_application = false;
  } else if (o != m_fenetre && m_montree && !m_application && !m_gel) {
    switch (e->type()) {
      case QEvent::Resize:
      case QEvent::Move:
      case QEvent::Show:
      case QEvent::Hide: m_retenue->start(); break;
      default: break;
    }
  }
  return QObject::eventFilter(o, e);
}

QByteArray DispositionsAtelier::decrire(const QString& nom) const {
  QJsonObject o{{QStringLiteral("format"), FORMAT},
                {QStringLiteral("version"), 1},
                {QStringLiteral("atelier"), m_cle},
                {QStringLiteral("nom"), nom},
                {QStringLiteral("etat"), QString::fromLatin1(m_fenetre->saveState(VERSION_ETAT).toBase64())}};
  if (m_lireComplement) o.insert(QStringLiteral("complement"), m_lireComplement());
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

bool DispositionsAtelier::appliquer(const QByteArray& description, QString* nom) {
  const QJsonDocument j = QJsonDocument::fromJson(description);
  const QJsonObject o = j.object();
  if (!j.isObject() || o.value(QStringLiteral("format")).toString() != FORMAT) return false;
  if (const QString atelier = o.value(QStringLiteral("atelier")).toString(); !atelier.isEmpty() && atelier != m_cle) return false;
  const QByteArray etat = QByteArray::fromBase64(o.value(QStringLiteral("etat")).toString().toLatin1());
  const bool avant = m_application;
  m_application = true;
  const bool ok = !etat.isEmpty() && m_fenetre->restoreState(etat, VERSION_ETAT);
  if (ok && m_appliquerComplement) m_appliquerComplement(o.value(QStringLiteral("complement")).toObject());
  m_application = avant;
  if (!ok) return false;
  if (nom) *nom = o.value(QStringLiteral("nom")).toString();
  emit appliquee();
  retenir();
  return true;
}

void DispositionsAtelier::enregistrer(const QString& nom, Lieu lieu) {
  const QByteArray d = decrire(nom);
  if (lieu == Lieu::PourMoi) {
    QSettings reglages;
    QVariantMap m = reglages.value(cleReglage("dispositions")).toMap();
    m.insert(nom, QString::fromUtf8(d));
    reglages.setValue(cleReglage("dispositions"), m);
  } else
    m_doc->reglerSansHistorique([&](prom::Projet& p) { p.dispositions[nom.toStdString()] = d.toStdString(); });
}

bool DispositionsAtelier::rappeler(const QString& nom, Lieu lieu) {
  QByteArray d;
  if (lieu == Lieu::PourMoi) d = QSettings().value(cleReglage("dispositions")).toMap().value(nom).toString().toUtf8();
  else if (const auto it = m_doc->projet().dispositions.find(nom.toStdString()); it != m_doc->projet().dispositions.end())
    d = QByteArray::fromStdString(it->second);
  return !d.isEmpty() && appliquer(d);
}

void DispositionsAtelier::supprimer(const QString& nom, Lieu lieu) {
  if (lieu == Lieu::PourMoi) {
    QSettings reglages;
    QVariantMap m = reglages.value(cleReglage("dispositions")).toMap();
    m.remove(nom);
    reglages.setValue(cleReglage("dispositions"), m);
  } else if (m_doc->projet().dispositions.count(nom.toStdString()))
    m_doc->reglerSansHistorique([&](prom::Projet& p) { p.dispositions.erase(nom.toStdString()); });
}

QStringList DispositionsAtelier::noms(Lieu lieu) const {
  if (lieu == Lieu::PourMoi) return QSettings().value(cleReglage("dispositions")).toMap().keys();
  QStringList r;
  for (const auto& [nom, d] : m_doc->projet().dispositions) r << QString::fromStdString(nom);
  return r;
}

bool DispositionsAtelier::exporter(const QString& chemin, const QString& nom) const {
  QFile f(chemin);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  const QByteArray d = QJsonDocument::fromJson(decrire(nom)).toJson(QJsonDocument::Indented);
  return f.write(d) == d.size();
}

bool DispositionsAtelier::importer(const QString& chemin, QString* nom) {
  QFile f(chemin);
  if (!f.open(QIODevice::ReadOnly)) return false;
  return appliquer(f.readAll(), nom);
}

void DispositionsAtelier::origine() {
  const bool avant = m_application;
  m_application = true;
  m_origine();
  m_application = avant;
  emit appliquee();
  retenir();
}

void DispositionsAtelier::retenir() {
  if (!m_montree) return;
  QSettings().setValue(cleReglage("disposition"), QString::fromUtf8(decrire({})));
}

void DispositionsAtelier::retenirMaintenant() {
  m_retenue->stop();
  retenir();
}

bool DispositionsAtelier::reprendre() {
  const QString d = QSettings().value(cleReglage("disposition")).toString();
  return !d.isEmpty() && appliquer(d.toUtf8());
}

void DispositionsAtelier::remplir(QMenu* m) {
  m->clear();
  m->addSection(QStringLiteral("Panneaux"));
  for (QDockWidget* d : m_panneaux) {
    QAction* a = m->addAction(d->windowTitle());
    a->setObjectName(QStringLiteral("panneau:") + d->objectName());
    a->setCheckable(true);
    a->setChecked(m_estOuvert ? m_estOuvert(d) : d->isVisible());
    connect(a, &QAction::triggered, this, [this, d](bool oui) {
      if (m_ouvrir) m_ouvrir(d, oui);
      else {
        d->setVisible(oui);
        if (oui) d->raise();
      }
    });
  }
  auto lister = [&](Lieu lieu, const QString& titre) {
    const QStringList n = noms(lieu);
    if (n.isEmpty()) return;
    m->addSection(titre);
    for (const QString& nom : n) {
      QAction* a = m->addAction(nom, this, [this, nom, lieu] { rappeler(nom, lieu); });
      a->setObjectName(QStringLiteral("disposition:") + nom);
    }
  };
  lister(Lieu::PourMoi, QStringLiteral("Pour moi"));
  lister(Lieu::Projet, QStringLiteral("Dans ce projet"));
  m->addSeparator();
  m->addAction(QStringLiteral("Enregistrer la disposition…"), this, &DispositionsAtelier::enregistrerInteractif)->setObjectName(QStringLiteral("enregistrerDisposition"));
  m->addAction(QStringLiteral("Importer une disposition…"), this, [this] {
    const QString chemin = QFileDialog::getOpenFileName(m_fenetre, QStringLiteral("Importer une disposition"), QString(), QStringLiteral("Dispositions (*.json)"));
    if (!chemin.isEmpty() && !importer(chemin))
      QMessageBox::warning(m_fenetre, QStringLiteral("Importer une disposition"), QStringLiteral("Ce fichier n’est pas une disposition de cet atelier."));
  });
  m->addAction(QStringLiteral("Exporter la disposition…"), this, [this] {
    const QString chemin = QFileDialog::getSaveFileName(m_fenetre, QStringLiteral("Exporter la disposition"), QStringLiteral("disposition.json"), QStringLiteral("Dispositions (*.json)"));
    if (!chemin.isEmpty() && !exporter(chemin, QFileInfo(chemin).completeBaseName()))
      QMessageBox::warning(m_fenetre, QStringLiteral("Exporter la disposition"), QStringLiteral("Impossible d’écrire « %1 ».").arg(chemin));
  });
  const QStringList perso = noms(Lieu::PourMoi), projet = noms(Lieu::Projet);
  if (!perso.isEmpty() || !projet.isEmpty()) {
    QMenu* sup = m->addMenu(QStringLiteral("Supprimer une disposition"));
    for (const QString& nom : perso) sup->addAction(nom + QStringLiteral(" (pour moi)"), this, [this, nom] { supprimer(nom, Lieu::PourMoi); });
    for (const QString& nom : projet) sup->addAction(nom + QStringLiteral(" (dans ce projet)"), this, [this, nom] { supprimer(nom, Lieu::Projet); });
  }
  m->addAction(QStringLiteral("Disposition d’origine"), this, &DispositionsAtelier::origine)->setObjectName(QStringLiteral("dispositionOrigine"));
}

// Nom et lieu : pour moi, dans ce projet ou dans un fichier à partager
void DispositionsAtelier::enregistrerInteractif() {
  QDialog d(m_fenetre);
  d.setWindowTitle(QStringLiteral("Enregistrer la disposition"));
  auto* f = new QFormLayout(&d);
  auto* nom = new QLineEdit;
  nom->setObjectName(QStringLiteral("nomDisposition"));
  nom->setPlaceholderText(QStringLiteral("Routage, Saisie du schéma…"));
  auto* lieu = new QComboBox;
  lieu->setObjectName(QStringLiteral("lieuDisposition"));
  lieu->addItems({QStringLiteral("Pour moi"), QStringLiteral("Dans ce projet"), QStringLiteral("Dans un fichier…")});
  f->addRow(QStringLiteral("Nom"), nom);
  f->addRow(QStringLiteral("Garder"), lieu);
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  boutons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Enregistrer"));
  f->addRow(boutons);
  connect(boutons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  connect(boutons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  if (d.exec() != QDialog::Accepted || nom->text().trimmed().isEmpty()) return;
  const QString n = nom->text().trimmed();
  if (lieu->currentIndex() == 2) {
    const QString chemin = QFileDialog::getSaveFileName(m_fenetre, QStringLiteral("Enregistrer la disposition"), n + QStringLiteral(".json"), QStringLiteral("Dispositions (*.json)"));
    if (!chemin.isEmpty() && !exporter(chemin, n))
      QMessageBox::warning(m_fenetre, QStringLiteral("Enregistrer la disposition"), QStringLiteral("Impossible d’écrire « %1 ».").arg(chemin));
    return;
  }
  enregistrer(n, lieu->currentIndex() == 0 ? Lieu::PourMoi : Lieu::Projet);
}

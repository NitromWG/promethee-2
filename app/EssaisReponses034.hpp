// Prométhée : réponses des essais intégrés de la 0.34 aux boîtes de dialogue modales qu'ouvre une commande (saisie,
// question, fichier), données dans l'ordre : chaque réponse attend la boîte suivante. En-tête des essais seulement.
// Licence GPL-3.0-only.
#pragma once

#include <cstdio>
#include <functional>
#include <vector>

#include <QAbstractButton>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QStringList>
#include <QTimer>

namespace essais034 {
// Les boîtes de dialogue modales ouvertes par une commande, dans l'ordre : chaque réponse attend la suivante
struct Reponse {
  enum class Genre { Saisie, Question, Fichier } genre;
  std::function<void(QInputDialog*)> saisie;
  bool oui = true;
  QString fichier;
};
// Ce que les réponses ont fait (fichier choisi, boîte attendue en vain) : le détail d'un essai en échec le cite
inline QStringList& journalReponses() {
  static QStringList j;
  return j;
}
inline void repondreDans(const std::vector<Reponse>& suite, std::size_t i = 0, int essais = 0) {
  if (i >= suite.size()) return;
  if (essais > 80) {
    QWidget* w = QApplication::activeModalWidget();
    journalReponses() << QStringLiteral("réponse %1 abandonnée, boîte active : %2").arg(i).arg(w ? QString::fromLatin1(w->metaObject()->className()) + QStringLiteral(" « ") + w->windowTitle() + QStringLiteral(" »") : QStringLiteral("aucune"));
    return;
  }
  QTimer::singleShot(100, [suite, i, essais] {
    QWidget* w = QApplication::activeModalWidget();
    const Reponse& r = suite[i];
    bool fait = false;
    if (r.genre == Reponse::Genre::Saisie)
      if (auto* d = qobject_cast<QInputDialog*>(w)) {
        if (r.saisie) r.saisie(d);
        d->accept();
        fait = true;
      }
    if (r.genre == Reponse::Genre::Question)
      if (auto* m = qobject_cast<QMessageBox*>(w)) {
        // Le bouton cliqué fait la réponse de QMessageBox::question : done() seul rendrait « Non »
        if (QAbstractButton* b = m->button(r.oui ? QMessageBox::Yes : QMessageBox::No)) b->click();
        else m->done(r.oui ? QMessageBox::Yes : QMessageBox::No);
        fait = true;
      }
    if (r.genre == Reponse::Genre::Fichier)
      if (auto* d = qobject_cast<QFileDialog*>(w)) {
        d->setDirectory(QFileInfo(r.fichier).absolutePath());  // le dossier d'abord : le nom seul est repris ensuite
        d->selectFile(r.fichier);
        // Le nom proposé par la commande reste dans le champ : il prend celui de l'essai
        if (auto* e = d->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) e->setText(QFileInfo(r.fichier).fileName());
        journalReponses() << QStringLiteral("fichier %1 → %2").arg(r.fichier, d->selectedFiles().join(QLatin1Char(',')));
        if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DEBUG")) std::printf("(fichier : %s → %s)\n", qPrintable(r.fichier), qPrintable(d->selectedFiles().join(QLatin1Char(','))));
        static_cast<QDialog*>(d)->accept();  // protégé dans QFileDialog, public dans QDialog
        fait = true;
      }
    if (!fait && w && qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DEBUG")) std::printf("(attente : %s « %s »)\n", w->metaObject()->className(), qPrintable(w->windowTitle()));
    repondreDans(suite, fait ? i + 1 : i, fait ? 0 : essais + 1);
  });
}
inline Reponse saisie(std::function<void(QInputDialog*)> f) { return {Reponse::Genre::Saisie, std::move(f), true, {}}; }
inline Reponse oui() { return {Reponse::Genre::Question, {}, true, {}}; }
inline Reponse fichier(const QString& f) { return {Reponse::Genre::Fichier, {}, true, f}; }
}  // namespace essais034

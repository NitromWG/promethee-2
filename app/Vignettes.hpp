// Prométhée : vignettes des feuilles du schéma (lot L2, tâche 2.30, ERG-37). Chaque feuille dessinée une fois en
// petit, gardée tant que sa conception ne change pas (Document::conceptionChangee l'invalide, un projet remplacé
// les efface toutes) ; l'arbre du projet la montre au survol d'une feuille, la mosaïque du projet les montre
// toutes, et un clic ouvre la feuille.
// Licence GPL-3.0-only.
#pragma once

#include <map>

#include <QObject>
#include <QPixmap>
#include <QString>

class AtelierElectronique;
class Document;

class Vignettes : public QObject {
  Q_OBJECT
 public:
  static Vignettes& instance();
  void suivre(Document* doc);  // une fois par document
  QPixmap vignette(Document* doc, int conception, int feuille, QSize taille = QSize(200, 140));
  // La vignette en fichier PNG (dossier temporaire), pour une infobulle en texte riche ; vide si impossible
  QString fichier(Document* doc, int conception, int feuille, QSize taille = QSize(200, 140));
  int dessinees() const { return m_dessinees; }  // essais : vignettes dessinées depuis le lancement
  // La mosaïque des feuilles de la conception ouverte : un clic ouvre la feuille
  static void ouvrirMosaique(AtelierElectronique& a);

 private:
  Vignettes() = default;
  std::map<QString, QPixmap> m_cache;  // « uuid de la conception|uuid de la feuille|l×h » → image
  std::map<QString, QString> m_fichiers;
  int m_dessinees = 0;
};

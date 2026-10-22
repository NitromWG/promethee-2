// Prométhée : panneaux d'un atelier et leurs dispositions nommées (lot L1, tâche 1.21, ERG-23). L'atelier pose ses
// panneaux (QDockWidget) sur une fenêtre principale imbriquée : ils flottent, s'ancrent, se groupent en onglets ou se
// replient. Une disposition est l'état de cette fenêtre (QMainWindow::saveState), avec ce que l'atelier y ajoute (la
// bibliothèque ouverte ou repliée, quand le mode la cache) ; elle se garde pour soi (réglages de l'application), dans
// le projet (sans pas d'annulation) ou dans un fichier .json qui se partage. La disposition en cours est reprise
// d'une session à l'autre.
// Format d'une disposition : {"format":"promethee-disposition","version":1,"atelier":…,"nom":…,"etat":base64,
// "complement":{…}}
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class Document;
class QDockWidget;
class QMainWindow;
class QMenu;
class QTimer;

class DispositionsAtelier : public QObject {
  Q_OBJECT
 public:
  enum class Lieu : std::uint8_t { PourMoi, Projet };
  static constexpr int VERSION_ETAT = 1;
  // « cle » : nom de l'atelier, groupe de ses réglages (« electronique ») ; « origine » : remet chaque panneau à sa
  // place d'origine
  DispositionsAtelier(QMainWindow* fenetre, Document* doc, QString cle, std::function<void()> origine, QObject* parent = nullptr);
  // Un panneau suivi : ses déplacements sont retenus, il paraît au menu des panneaux
  void suivre(QDockWidget* d);
  const std::vector<QDockWidget*>& panneaux() const { return m_panneaux; }
  // Ce que l'atelier ajoute à l'état de la fenêtre, et comment il le reprend
  void setComplement(std::function<QJsonObject()> lire, std::function<void(const QJsonObject&)> appliquer);
  // Panneau ouvert ou fermé au menu : par défaut montré ou caché ; l'atelier dit ce que veut dire « ouvert » pour
  // un panneau que le mode cache
  void setOuverture(std::function<bool(const QDockWidget*)> estOuvert, std::function<void(QDockWidget*, bool)> ouvrir);

  QByteArray decrire(const QString& nom) const;
  bool appliquer(const QByteArray& description, QString* nom = nullptr);
  void enregistrer(const QString& nom, Lieu lieu);
  bool rappeler(const QString& nom, Lieu lieu);
  void supprimer(const QString& nom, Lieu lieu);
  QStringList noms(Lieu lieu) const;
  bool exporter(const QString& chemin, const QString& nom) const;
  bool importer(const QString& chemin, QString* nom = nullptr);
  void origine();
  // Disposition en cours, gardée d'une session à l'autre ; reprise à la première apparition de la fenêtre
  void retenir();
  void retenirMaintenant();  // sans attendre le calme ; ce qui suit ne sera retenu qu'après le dégel
  bool reprendre();
  // Gel : ce qui bouge pendant ne compte pas (panneaux flottants cachés avec l'atelier)
  void geler(bool oui) { m_gel = oui; }
  // Menu « Panneaux et dispositions » : les panneaux, les dispositions gardées, enregistrer, importer, exporter,
  // supprimer, disposition d'origine
  void remplir(QMenu* m);
  void enregistrerInteractif();

 signals:
  void appliquee();

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  QString cleReglage(const char* suffixe) const;
  QMainWindow* m_fenetre;
  Document* m_doc;
  QString m_cle;
  std::function<void()> m_origine;
  std::function<QJsonObject()> m_lireComplement;
  std::function<void(const QJsonObject&)> m_appliquerComplement;
  std::function<bool(const QDockWidget*)> m_estOuvert;
  std::function<void(QDockWidget*, bool)> m_ouvrir;
  std::vector<QDockWidget*> m_panneaux;
  QTimer* m_retenue;
  bool m_montree = false, m_application = false, m_gel = false;
};

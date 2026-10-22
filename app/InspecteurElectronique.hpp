// Prométhée : inspecteur de l'atelier électronique (lot L1, tâche 1.9, ERG-01). Panneau stable, à droite, commun au
// schéma et au circuit, qui montre la sélection courante. Une page par genre de sélection, créée une fois puis
// réutilisée : changer de sélection change de page et remplit ses champs, sans détruire de widget. Un changement du
// modèle ne fait que relire les champs, et le champ qui a le focus n'est jamais réécrit (P1). Les champs de texte
// s'appliquent pendant la frappe, en un pas d'annulation par passage dans le champ, et Échap y reprend la valeur
// d'avant (P2). La molette ne change pas un champ qui n'a pas le focus (P16). Une révision publiée met tout en
// lecture seule, sous un bandeau qui propose la révision suivante (P21). Couleurs prises dans le thème (P15).
// Pages du schéma : InspecteurSchema.cpp ; du circuit, de la 3D et de la fabrication : InspecteurCircuit.cpp. La
// bibliothèque a son propre panneau, à gauche (PanneauBibliotheque, 1.10).
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QLineEdit>
#include <QPointer>
#include <QScrollArea>
#include <QString>
#include <QWidget>

class AtelierElectronique;
class QAbstractButton;
class QLabel;
class QPushButton;
class QVBoxLayout;

// Couleurs du thème pour le texte riche de l'inspecteur (P15)
namespace teintes {
QString secondaire();  // texte de second rang
QString erreur();
QString alerte();
QString succes();
QString lien();
}  // namespace teintes

// Champ de texte lié à une propriété du modèle. Pendant la frappe, chaque texte valable est écrit (aperçu immédiat
// sur la feuille, P2) ; les écritures d'un même passage dans le champ portent le même numéro de saisie, qui les fond
// en un pas d'annulation. Un texte refusé par la validation prend un bord rouge et sa raison en bulle, sans rien
// écrire ; à la sortie du champ, la valeur du modèle revient. Le modèle changé ailleurs pendant la saisie ne réécrit
// pas le champ : il prend un bord orangé (« modifié ailleurs »), Échap reprenant la valeur.
class ChampTexteLie : public QLineEdit {
  Q_OBJECT
 public:
  using Lire = std::function<QString()>;
  using Ecrire = std::function<void(const QString& texte, std::uint64_t saisie)>;
  using Valider = std::function<QString(const QString& texte)>;  // raison du refus, vide si le texte est valable
  ChampTexteLie(Lire lire, Ecrire ecrire, bool pendantLaFrappe = true, QWidget* parent = nullptr);
  void setValidation(Valider v) { m_valider = std::move(v); }
  // La valeur du modèle, sauf pendant une saisie (le champ a le focus)
  void relire();
  // Échap : la valeur d'avant la saisie revient, dans le champ et dans le modèle (même pas d'annulation)
  void renoncer();
  bool modifieAilleurs() const { return m_ailleurs; }
  bool refuse() const { return m_refus; }

 protected:
  void focusInEvent(QFocusEvent* e) override;
  void focusOutEvent(QFocusEvent* e) override;

 private:
  void frappe();
  void fin();
  void signaler();
  Lire m_lire;
  Ecrire m_ecrire;
  Valider m_valider;
  bool m_frappe;
  QString m_avant;          // valeur à l'entrée dans le champ
  QString m_ecrit;          // dernier texte écrit dans le modèle pendant ce passage
  std::uint64_t m_saisie = 0;
  bool m_ailleurs = false, m_refus = false;
};

// Une page de l'inspecteur : ses widgets sont créés une fois ; « relire » les remplit depuis le modèle
class PageInspecteur : public QWidget {
  Q_OBJECT
 public:
  explicit PageInspecteur(AtelierElectronique& a, QWidget* parent = nullptr);
  // La sélection a changé : la page prend sa cible et relit tout
  virtual void montrer() { relire(); }
  // Le modèle a changé : chaque champ lié relit sa valeur (sous QSignalBlocker), sauf celui qui a le focus
  virtual void relire();
  // Les moteurs ont rendu leurs résultats (réseaux, ERC, DRC)
  virtual void resultats() { relire(); }
  // Conception publiée : champs en lecture seule, boutons qui modifient grisés
  void setLectureSeule(bool oui);
  bool lectureSeule() const { return m_lectureSeule; }

 protected:
  // Lie un widget de saisie à sa relecture ; « modifie » : il change le modèle (grisé en lecture seule)
  void lier(QWidget* w, std::function<void()> relireW, bool modifie = true);
  // Bouton qui modifie le modèle : grisé en lecture seule
  void action(QAbstractButton* b);
  // Relecture d'un élément sans widget de saisie (en-tête, listes calculées), toujours faite
  void suivre(std::function<void()> f) { m_suivis.push_back(std::move(f)); }
  QVBoxLayout* colonne() const { return m_col; }
  QLabel* entete();  // titre en grand de la page, relu par suivre()
  AtelierElectronique& m_atelier;

 private:
  struct Liaison {
    QPointer<QWidget> widget;
    std::function<void()> relire;
    bool modifie;
  };
  std::vector<Liaison> m_liaisons;
  std::vector<std::function<void()>> m_suivis;
  std::vector<QPointer<QAbstractButton>> m_actions;
  QVBoxLayout* m_col = nullptr;
  bool m_lectureSeule = false;
};

// Section dont le contenu dépend d'une liste du modèle (classes de réseaux, découpes, problèmes) : refaite quand sa
// signature change, jamais pendant que le focus y est (elle l'est alors dès que le focus en sort) ; la page et son
// défilement restent
class SectionRefaite : public QWidget {
  Q_OBJECT
 public:
  using Construire = std::function<void(QVBoxLayout*)>;
  explicit SectionRefaite(Construire c, QWidget* parent = nullptr);
  // Refait le contenu si la signature a changé ; faux si le focus y était (refait quand il en sortira)
  bool refaire(const std::string& signature);

 private:
  void construire(const std::string& signature);
  Construire m_construire;
  std::string m_signature = "\x01", m_attente;
  bool m_aFaire = false;
  QVBoxLayout* m_col;
};

// Bandeau d'une révision publiée, en tête de l'inspecteur (P21)
class BandeauPublication : public QWidget {
  Q_OBJECT
 public:
  explicit BandeauPublication(AtelierElectronique& a, QWidget* parent = nullptr);
  void relire();

 private:
  AtelierElectronique& m_atelier;
  QLabel* m_texte;
  QPushButton* m_revision;
};

class InspecteurElectronique : public QScrollArea {
  Q_OBJECT
 public:
  explicit InspecteurElectronique(AtelierElectronique& a, QWidget* parent = nullptr);
  ~InspecteurElectronique() override;
  // La vue ou la sélection a changé : la page qui convient, remplie (elle est créée à sa première venue)
  void montrer();
  // Le modèle a changé : la page relit ses champs
  void relire();
  // Les moteurs ont rendu leurs résultats
  void resultats();
  // Une page à refaire de zéro à sa prochaine venue
  void oublier(const QString& cle);
  QString clePage() const { return m_cle; }  // « schema.composant », « circuit.carte »…
  PageInspecteur* page() const { return m_page; }
  int pagesCreees() const { return m_creees; }  // essais : une page n'est créée qu'une fois
  // La molette ne change pas un champ qui n'a pas le focus : elle fait défiler le panneau (P16)
  void sansMolette(QWidget* racine);

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  QString cleVoulue() const;
  PageInspecteur* creer(const QString& cle);
  void appliquerTheme();
  AtelierElectronique& m_atelier;
  QWidget* m_contenu;
  QVBoxLayout* m_col;
  BandeauPublication* m_bandeau;
  std::map<QString, PageInspecteur*> m_pages;
  PageInspecteur* m_page = nullptr;
  QString m_cle;
  int m_creees = 0;
};

// Fabriques des pages (InspecteurSchema.cpp, InspecteurCircuit.cpp)
PageInspecteur* pageSchema(const QString& cle, AtelierElectronique& a);
PageInspecteur* pageCircuit(const QString& cle, AtelierElectronique& a);

namespace prom::ecad {
struct Violation;
}
namespace inspecteur {
// Texte riche aux couleurs du thème, que le filtre du thème ne retraduit pas
QLabel* texte(const QString& html = {});
// Lignes de violations (ERC au schéma, DRC au circuit) : un clic mène au problème
void listeProblemes(QVBoxLayout* col, const std::vector<prom::ecad::Violation>& v, bool schema, AtelierElectronique& a);
std::string signature(const std::vector<prom::ecad::Violation>& v);
// Champ d'une propriété texte d'un composant (repère, valeur, fabricant…) : une commande nommée par écriture
// (« Valeur de R3 : 10k → 4k7 », SAI-24), fondue avec les autres écritures du même passage dans le champ (P2)
ChampTexteLie* champComposant(AtelierElectronique& a, std::function<std::string()> composant, const std::string& propriete);
}  // namespace inspecteur

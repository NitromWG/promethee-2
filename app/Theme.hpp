// Prométhée : thèmes de couleur de l'interface. Licence GPL-3.0-only.
//
// Cinq thèmes. Prométhée (recommandé) : interface bleu nuit aux couleurs du logo, documents (schéma, esquisse,
// plan de carte) sur papier clair, comme une fois imprimés ; moins d'éblouissement autour de la zone de travail
// pendant les longues séances. Clair. Sombre : tout sombre, documents compris. Contraste élevé : noir et
// blanc, couleurs vives, pour une vue fatiguée ou un écran en plein soleil. Système : suit le réglage clair ou
// sombre du système, et ses changements. Le choix est gardé dans les réglages et s'applique aussitôt.
//
// Le code de l'interface écrit ses couleurs dans le jeu clair d'origine. Le thème les traduit selon leur usage
// (texte, fond, filet) : de lui-même pour les feuilles de style des widgets et les textes riches des étiquettes
// (un filtre d'événements repère chaque changement et garde l'original), à l'appel pour les dessins (texte(),
// fond(), bordure()). Un widget qui règle lui-même ses couleurs pour chaque thème porte la propriété
// « themeFixe » : sa feuille de style n'est pas traduite. Les vues de document prennent leurs encres dans des jeux propres ; l'impression et
// l'export PDF utilisent toujours le jeu clair.
#pragma once

#include <vector>

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QPalette>
#include <QString>
#include <QStyledItemDelegate>

class QApplication;
class QMenu;
class QWidget;

enum class IdTheme { Promethee, Clair, Sombre, ContrasteEleve, Systeme };

// Couleurs de l'interface (panneaux, menus, champs, textes)
struct CouleursInterface {
  QColor fenetre, surface, champ, survol, choisi, bordure;
  QColor texte, texteSecondaire, texteDiscret, texteDesactive;
  QColor accent;        // fond d'accent (sélection, bouton principal), texte blanc dessus
  QColor accentTexte;   // accent en texte ou en trait sur le fond du thème
  QColor marque;        // violet du logo
  QColor surAccent;     // texte sur accent
  QColor succes, alerte, erreur;
  QColor infobulle, texteInfobulle;
  QColor titreHaut, titreBas, titreTexte, titreBordure;  // barre de titre
  QColor vue3dHaut, vue3dBas, triedre;                   // vues 3D
  bool sombre = false;            // interface sombre
  bool documentsSombres = false;  // schéma, esquisse et plan de carte sur fond sombre
};

// Encres du schéma électronique
struct EncresSchema {
  QColor papier, hors, cadre, encre, fondCorps, fil, alim, choix, reseau, globale, apercu, grille, cartouche, marque, broche, fondEtiquette;
};
// Signes des canevas de l'atelier électronique (charte, 1.7) : chacun a sa forme, pas seulement sa couleur.
// Sélection : fond à 18 %, contour plein, poignées ; surlignage d'un réseau : halo ; survol : contour fin ; erreur :
// disque ; avertissement : triangle ; information : cercle creux ; à refaire : hachures. Contraste d'au moins 3:1
// avec le fond (4,5:1 en contraste élevé).
struct SignesCanevas {
  QColor sel, surl, survol, err, avert, info, perime;
};
// Encres de l'esquisse
struct EncresEsquisse {
  QColor fond, grille, grilleForte, axeX, axeY, libre, contraint, construction, choisi, survol, apercu, conflit, encre, cote, reference;
  QColor bulle, bulleBord, bulleTexte;  // cartouches d'information posés sur l'esquisse
};
// Encres de la vue de la carte et du boîtier (plan 2D)
struct EncresCarte {
  QColor plan, grille, grilleForte, paroi, fond, trait, encre, encre2, bulle;
};
// Encres du circuit imprimé (2.15, G3, G9), pensées par paires : la sélection tient au moins 3:1 sur chaque cuivre
// et sur la carte, la sérigraphie du dessous 3:1 sur le cuivre du dessous (formule du WCAG)
struct EncresCircuit {
  QColor fond, grille, carte, bord, traversant, via, serigDessus, serigDessous, chevelu, choix, rouge;
  QColor cuivreDessus, cuivreDessous, cuivreInt1, cuivreInt2;
};
// Rapport de contraste du WCAG entre deux couleurs (1 à 21)
double contraste(const QColor& a, const QColor& b);

class Theme : public QObject {
  Q_OBJECT
public:
  static Theme& instance();
  static QString nom(IdTheme id);
  static QString description(IdTheme id);
  static QString cle(IdTheme id);  // « promethee », « clair »… (réglages, PROMETHEE_THEME)
  static IdTheme depuisCle(const QString& cle, IdTheme parDefaut = IdTheme::Promethee);
  static constexpr IdTheme TOUS[] = {IdTheme::Promethee, IdTheme::Clair, IdTheme::Sombre, IdTheme::ContrasteEleve, IdTheme::Systeme};

  // Lit le réglage (ou PROMETHEE_THEME, qui n'est pas gardé), installe le filtre et applique le thème.
  void demarrer(QApplication& app);
  void choisir(IdTheme id);       // applique et garde le choix
  IdTheme choix() const { return m_choix; }
  IdTheme effectif() const { return m_effectif; }  // Système résolu en Clair ou Sombre
  int generation() const { return m_generation; }  // change à chaque application (caches)

  const CouleursInterface& c() const { return m_c; }
  const EncresSchema& schema(bool impression = false) const;
  // Signes du schéma (selon le papier du thème ; jeu clair à l'impression) et du circuit (toujours sur fond sombre)
  const SignesCanevas& signesSchema(bool impression = false) const;
  const SignesCanevas& signesCircuit() const;
  // Esquisse sur sa feuille, ou posée en transparence sur une vue 3D (dont le fond est sombre hors du thème Clair)
  const EncresEsquisse& esquisse(bool surVue3D = false) const { return surVue3D ? m_esquisseSur3D : m_esquisse; }
  const EncresCarte& carte() const { return m_carte; }
  const EncresCircuit& circuit() const { return m_circuit; }

  // Traduction d'une couleur du jeu clair d'origine, selon son usage
  QColor texte(const QColor& clair) const;    // texte, trait, pictogramme
  QColor fond(const QColor& clair) const;     // fond de panneau, de bouton, de case
  QColor bordure(const QColor& clair) const;  // filet, cadre
  QColor encreDocument(const QColor& c) const;  // couleur choisie par l'utilisateur, posée sur un document sombre
  static QColor pourFondSombre(const QColor& c);  // même teinte, assez claire pour un fond sombre
  QString css(const QString& feuille) const;  // feuille de style Qt
  QString html(const QString& texte) const;   // texte riche
  QRgb pixel(QRgb clair) const;               // pictogrammes dessinés dans le jeu clair
  bool traduit() const { return m_effectif != IdTheme::Clair; }

  // Sous-menu « Thème » : cases exclusives, tenues à jour
  QMenu* menu(QWidget* parent);

  // Jetons de couleur des canevas (2.31, ThemeJetons.cpp) : chaque encre nommée se lit et se règle ; un fichier de
  // thème les porte. Palettes : celles du thème, d'autres logiciels, ou lisibles par les daltoniens.
  struct Jeton {
    QString cle, libelle;
  };
  static std::vector<Jeton> jetons();
  QColor jeton(const QString& cle) const;
  void setJeton(const QString& cle, const QColor& c);  // couleur invalide : le jeton revient à la palette
  void effacerJetons();
  static QStringList palettes();
  int palette() const;
  void choisirPalette(int i);
  void rafraichir();  // réapplique (échelle des signes, lissage)
  QJsonObject exporterTheme() const;
  bool importerTheme(const QJsonObject& o, QString* erreur = nullptr);
  // Échelle des signes du canevas (poignées, réticule) et lissage des traits, réglés dans Prométhée
  static double echelleCanevas();
  static bool anticrenelage();
  // Daltonismes courants (modèle de Machado, 2009) : la couleur telle que la voit un œil daltonien
  enum class Daltonisme { Protanopie, Deuteranopie, Tritanopie };
  static QColor simuler(const QColor& c, Daltonisme d);
  // Pour les jetons : les encres appliquées, modifiables
  EncresSchema& encresSchemaModifiables() { return m_schema; }
  EncresCircuit& encresCircuitModifiables() { return m_circuit; }
  SignesCanevas& signesModifiables(bool circuit) { return circuit ? m_signesCircuit : m_signesSchema; }

signals:
  void change();

private:
  Theme() = default;
  void appliquer();
  void appliquerPaletteEtJetons();
  void retraduireWidgets();
  bool systemeSombre() const;
  friend class FiltreTheme;
  IdTheme m_choix = IdTheme::Promethee, m_effectif = IdTheme::Promethee;
  CouleursInterface m_c;
  EncresSchema m_schema, m_schemaClair;
  SignesCanevas m_signesSchema, m_signesClair, m_signesCircuit;
  EncresEsquisse m_esquisse, m_esquisseSur3D;
  EncresCarte m_carte;
  EncresCircuit m_circuit;
  QPalette m_paletteSysteme;
  QApplication* m_app = nullptr;
  int m_generation = 0;
  double m_echelleCanevas = 1.0;  // 2.31 : relus à chaque application
  bool m_anticrenelage = true;
  bool m_impose = false;  // PROMETHEE_THEME : ni gardé, ni remplacé par le système
};

inline const Theme& theme() { return Theme::instance(); }

// Délégué des listes et des arbres : traduit au dessin les couleurs de texte et de fond posées sur les éléments
// dans le jeu clair. Le filtre du thème le pose de lui-même sur les vues qui gardent le délégué par défaut.
class DelegueTheme : public QStyledItemDelegate {
  Q_OBJECT
public:
  using QStyledItemDelegate::QStyledItemDelegate;

protected:
  void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;
};

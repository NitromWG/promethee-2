// Prométhée : écran d'accueil. Licence GPL-3.0-only.
#include "Accueil.hpp"

#include <algorithm>
#include <iterator>
#include <map>

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>

#include "Icones.hpp"
#include "Theme.hpp"

namespace {
struct Metier {
  const char* code;
  const char* titre;
  const char* description;
  const char* icone;
  QColor couleur;
};
const Metier METIERS[] = {
    {"meca", "Mécanique", "Pièces 3D, assemblages, tôlerie, plans", "metier-meca", QColor(0x27, 0x60, 0xF1)},
    {"elec", "Électronique", "Schémas, circuits imprimés, fabrication", "metier-electronique", QColor(0x0E, 0x94, 0xB8)},
    {"electrique", "Électrique", "Schémas, installations, solaire, automatisme", "metier-electrique", QColor(0xC2, 0x6E, 0x00)},
    {"mixte", "Plusieurs métiers", "Carte, boîtier et pièces reliés", "metier-mixte", QColor(0x5B, 0x4B, 0xF5)},
};

struct Choix {
  const char* metier;
  const char* code;
  const char* titre;
  const char* description;
  const char* icone;
  bool disponible;
  const char* motsCles = "";  // pour la recherche : ce que l'utilisateur tape sans connaître le nom exact
};
const Choix CHOIX[] = {
    {"meca", "piece", "Pièce", "Une pièce en 3D : esquisses, volumes, congés. À imprimer, usiner ou mouler.", "piece", true, "volume 3d modelisation usinage impression mecanique extrusion"},
    {"meca", "assemblage", "Assemblage", "Les pièces du projet placées ensemble ; interférences, export STEP.", "assemblage", true, "contraintes liaisons mecanisme montage step"},
    {"meca", "tolerie", "Tôlerie", "Caisson ou capot en tôle pliée : bords tombés, déplié exact et DXF pour la découpe.", "tolerie", true, "tole pliage deplie capot caisson dxf decoupe laser"},
    {"meca", "cao-ouvrir", "Ouvrir un STEP ou un IGES", "Un fichier d'un autre logiciel de CAO : pièces, structure d'assemblage, noms et couleurs gardés.", "importer-cao", true, "import step iges stl brep solidworks inventor fusion catia"},
    {"meca", "plan", "Mise en plan", "Vues normalisées d'une pièce sur une feuille ; rangée sous elle dans l'arbre.", "plan", true, "plan dessin cotation 2d vues coupe cartouche"},
    {"elec", "carte-nouvelle", "Nouvelle carte électronique", "Schéma, circuit imprimé routé, 3D, puis dossier de fabrication et publication.", "pcb", true, "pcb circuit imprime routage gerber composants"},
    {"elec", "schema-elec", "Schéma électronique", "Un schéma seul, vérifié et dessiné en PDF ; le circuit imprimé suit quand tu veux.", "schema", true, "schematique electronique composants netlist"},
    {"elec", "kicad-ouvrir", "Ouvrir une carte KiCad", "Importe un .kicad_pcb : empreintes, pistes, vias, zones et réseaux, avec un schéma de liaison.", "kicad", true, "kicad import pcb"},
    {"elec", "exemple-elec", "Exemple : clignotant 555", "Une carte complète à explorer : schéma, circuit routé, 3D, fabrication.", "composants", true, "exemple 555 led apprendre"},
    {"electrique", "electrique-schema", "Schéma électrique", "Armoire ou machine : folios de puissance et de commande, contacts rattachés à leur bobine, borniers, fils numérotés.", "contacteur", true, "armoire machine automatisme contacteur moteur folio bornier cablage commande puissance"},
    {"electrique", "electrique-distribution", "Installation électrique (calcul)", "De la source aux récepteurs : protections, câbles, chutes de tension, courts-circuits et protection des personnes calculés ; unifilaire et note de calcul.", "distribution", true, "calcul installation distribution tgbt tableau divisionnaire section cable court-circuit icc chute de tension selectivite note de calcul bureau d'etudes tertiaire industrie transformateur caneco ecodial nf c 15-100 ute c 15-105"},
    {"electrique", "electrique-logement", "Tableau de logement", "NF C 15-100 : les pièces et les équipements donnent les circuits, les différentiels, l’unifilaire et le coffret.", "logement", true, "maison appartement nf c 15-100 tableau electrique differentiel disjoncteur habitat"},
    {"electrique", "electrique-armoire", "Armoire électrique", "Plaque de fond, rails et goulottes : les appareils du schéma s’y implantent ; plan de perçage DXF.", "armoire", true, "coffret plaque rail din goulotte implantation percage"},
    {"electrique", "electrique-faisceau", "Faisceau", "Planche de faisceau : connecteurs, branches, longueur et couleur de chaque fil.", "faisceau", true, "cable harnais connecteur longueur fil automobile"},
    {"electrique", "electrique-photovoltaique", "Installation photovoltaïque", "Modules, chaînes, onduleur, fusibles, câbles, parafoudres et protections calculés selon le guide UTE C 15-712-1 ; production estimée, unifilaire et note de calcul.", "photovoltaique", true, "photovoltaique solaire panneau module onduleur string chaine mppt autoconsommation vente totale kwc ute c 15-712-1 consuel"},
    {"electrique", "electrique-irve", "Bornes de recharge (IRVE)", "Parking ou maison : bornes réparties sur les phases, différentiels adaptés, sans foisonnement, câbles calculés.", "irve", true, "irve borne recharge vehicule electrique voiture parking wallbox nf c 15-100 722"},
    {"electrique", "electrique-grafcet", "Grafcet (automatisme)", "Étapes, transitions et actions selon la NF EN CEI 60848 : vérifié, simulé pas à pas, dessiné sur un folio, traduit en texte structuré pour l’automate.", "grafcet", true, "grafcet sfc automatisme automate api plc etape transition receptivite sequence cycle simulation texte structure st iec 61131 cei 60848"},
    {"electrique", "electrique-apprendre", "Apprendre pas à pas", "Des parcours guidés : un premier schéma de moteur, un premier grafcet, une installation calculée, un toit photovoltaïque, un tableau de logement ; chaque étape se vérifie toute seule.", "apprendre", true, "apprendre debutant tutoriel guide pas a pas formation cours decouvrir premier schema"},
    {"electrique", "electrique-exemple", "Exemple : démarrage d’un moteur", "Un projet complet à explorer : puissance, commande, renvois, borniers, armoire, câble.", "moteur", true, "exemple moteur contacteur apprendre"},
    {"electrique", "electrique-qet", "Ouvrir un projet QElectroTech", "Importe un .qet : folios, appareils, contacts, bornes et conducteurs repris dans Prométhée.", "cablage", true, "qelectrotech qet import"},
    {"mixte", "mixte-boitier", "Boîtier pour une carte", "Part d'une carte KiCad : son boîtier sur mesure, relié à elle.", "metier-mixte", true, "boitier enveloppe carte kicad impression 3d"},
    {"mixte", "mixte-vide", "Projet libre", "Un projet vide : ajoute pièces et cartes depuis l'arbre.", "dossier", true, "vide projet nouveau"},
};

// Tuile d'un métier : couleur franche, icône sur pastille blanche, titre et description qui tiennent toujours.
class Tuile : public QWidget {
public:
  Tuile(const Metier& m, std::function<void()> clic) : m_m(m), m_clic(std::move(clic)) {
    setMinimumWidth(160);
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    setToolTip(QString::fromUtf8(m.description));
  }
  void setEtat(bool choisie, bool autreChoisie) { m_choisie = choisie; m_autre = autreChoisie; update(); }
  QSize sizeHint() const override { return {230, heightForWidth(230)}; }
  bool hasHeightForWidth() const override { return true; }
  // Hauteur nécessaire pour que le titre et la description tiennent entièrement à cette largeur.
  int heightForWidth(int w) const override {
    const int l = std::max(40, w - 8 - 36);
    QFont titre = font();
    titre.setPixelSize(19);
    titre.setBold(true);
    QFont desc = font();
    desc.setPixelSize(13);
    const int ht = QFontMetrics(titre).boundingRect(QRect(0, 0, l, 1000), Qt::TextWordWrap, QString::fromUtf8(m_m.titre)).height();
    const int hd = QFontMetrics(desc).boundingRect(QRect(0, 0, l, 1000), Qt::TextWordWrap, QString::fromUtf8(m_m.description)).height();
    return 8 + 76 + ht + 6 + hd + 18;
  }

protected:
  void mousePressEvent(QMouseEvent*) override { m_clic(); }
  void paintEvent(QPaintEvent*) override {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(4, 4, -4, -4);
    QColor fond = m_m.couleur;
    if (underMouse() && !m_choisie) fond = fond.lighter(112);
    if (m_autre && !m_choisie) fond = QColor::fromHslF(fond.hslHueF(), fond.hslSaturationF() * 0.55, std::min(0.75, fond.lightnessF() + 0.18));
    g.setPen(Qt::NoPen);
    g.setBrush(fond);
    g.drawRoundedRect(r, 14, 14);
    if (m_choisie) {
      g.setPen(QPen(Qt::white, 3));
      g.setBrush(Qt::NoBrush);
      g.drawRoundedRect(r.adjusted(5, 5, -5, -5), 10, 10);
    }
    // Pastille (blanche dans le thème clair) et icône
    const QPointF c(r.left() + 40, r.top() + 42);
    g.setPen(Qt::NoPen);
    g.setBrush(theme().fond(Qt::white));
    g.drawEllipse(c, 24, 24);
    icone(QString::fromLatin1(m_m.icone)).paint(&g, QRect(int(c.x()) - 16, int(c.y()) - 16, 32, 32));
    // Titre et description, ajustés à la largeur
    QFont titre = font();
    titre.setPixelSize(19);
    titre.setBold(true);
    g.setFont(titre);
    g.setPen(Qt::white);
    const double l = r.width() - 36;
    const QRectF bt = QFontMetricsF(titre).boundingRect(QRectF(0, 0, l, 1000), Qt::TextWordWrap, QString::fromUtf8(m_m.titre));
    const QRectF zoneTitre(r.left() + 18, r.top() + 76, l, bt.height());
    g.drawText(zoneTitre, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, QString::fromUtf8(m_m.titre));
    QFont desc = font();
    desc.setPixelSize(13);
    g.setFont(desc);
    g.setPen(QColor(255, 255, 255, 230));
    const QRectF zoneDesc(r.left() + 18, zoneTitre.bottom() + 6, l, r.bottom() - zoneTitre.bottom() - 6);
    g.drawText(zoneDesc, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, QString::fromUtf8(m_m.description));
  }

private:
  Metier m_m;
  std::function<void()> m_clic;
  bool m_choisie = false, m_autre = false;
};

// Carte d'un choix : icône, titre, description sur deux lignes ; « Bientôt » si pas encore disponible.
class CarteChoix : public QWidget {
public:
  CarteChoix(const Choix& c, QColor couleur, std::function<void()> clic) : m_c(c), m_couleur(couleur), m_clic(std::move(clic)) {
    setMinimumWidth(220);
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    setAttribute(Qt::WA_Hover);
    if (c.disponible) setCursor(Qt::PointingHandCursor);
    setToolTip(c.disponible ? QString::fromUtf8(c.description) : QStringLiteral("Bientôt dans Prométhée"));
  }
  QSize sizeHint() const override { return {300, heightForWidth(300)}; }
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int w) const override {
    QFont desc = font();
    desc.setPixelSize(12);
    const int hd = QFontMetrics(desc).boundingRect(QRect(0, 0, std::max(40, w - 6 - 76), 1000), Qt::TextWordWrap, QString::fromUtf8(m_c.description)).height();
    return std::max(84, 6 + 34 + hd + 12);
  }

protected:
  void mousePressEvent(QMouseEvent*) override { if (m_c.disponible) m_clic(); }
  void paintEvent(QPaintEvent*) override {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(3, 3, -3, -3);
    const bool survol = underMouse() && m_c.disponible;
    const Theme& th = theme();
    g.setPen(QPen(survol ? th.texte(m_couleur) : th.bordure(QColor(0xD7, 0xDC, 0xEA)), survol ? 2 : 1));
    g.setBrush(m_c.disponible ? (survol ? QColor(m_couleur.red(), m_couleur.green(), m_couleur.blue(), th.c().sombre ? 40 : 18) : th.fond(Qt::white)) : th.fond(QColor(0xF3, 0xF5, 0xFA)));
    g.drawRoundedRect(r, 10, 10);
    g.setOpacity(m_c.disponible ? 1.0 : 0.45);
    icone(QString::fromLatin1(m_c.icone)).paint(&g, QRect(int(r.left()) + 14, int(r.top()) + 18, 40, 40));
    QFont titre = font();
    titre.setPixelSize(15);
    titre.setBold(true);
    g.setFont(titre);
    g.setPen(th.texte(QColor(0x10, 0x14, 0x2A)));
    const double x = r.left() + 66, w = r.width() - 76;
    double largeurTitre = w;
    if (!m_c.disponible) largeurTitre -= 64;
    g.drawText(QRectF(x, r.top() + 10, largeurTitre, 22), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(titre).elidedText(QString::fromUtf8(m_c.titre), Qt::ElideRight, int(largeurTitre)));
    QFont desc = font();
    desc.setPixelSize(12);
    g.setFont(desc);
    g.setPen(th.texte(QColor(0x4A, 0x52, 0x70)));
    g.drawText(QRectF(x, r.top() + 34, w, r.height() - 38), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, QString::fromUtf8(m_c.description));
    g.setOpacity(1.0);
    if (!m_c.disponible) {
      QFont b = font();
      b.setPixelSize(11);
      b.setBold(true);
      g.setFont(b);
      const QRectF badge(r.right() - 70, r.top() + 11, 60, 20);
      g.setPen(Qt::NoPen);
      g.setBrush(th.fond(QColor(0xE4, 0xE8, 0xF2)));
      g.drawRoundedRect(badge, 10, 10);
      g.setPen(th.texte(QColor(0x4A, 0x52, 0x70)));
      g.drawText(badge, Qt::AlignCenter, QStringLiteral("Bientôt"));
    }
  }

private:
  Choix m_c;
  QColor m_couleur;
  std::function<void()> m_clic;
};
}  // namespace

Accueil::Accueil(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("accueil"));
  setStyleSheet(QStringLiteral(
      "#accueil { background: #F4F6FB; }"
      "#colonne { background: #FFFFFF; border-right: 1px solid #DEE2EE; }"
      "#colonne QPushButton { text-align: left; padding: 9px 12px; border: 1px solid #D7DCEA; border-radius: 8px; background: #FFFFFF; font-size: 14px; }"
      "#colonne QPushButton:hover { background: #EFF3FF; border-color: #A9BCF0; }"
      "QPushButton#recent { border: none; padding: 7px 10px; font-size: 13px; }"
      "QPushButton#recent:hover { background: #EFF3FF; }"));
  auto* racine = new QHBoxLayout(this);
  racine->setContentsMargins(0, 0, 0, 0);
  racine->setSpacing(0);
  // Colonne de gauche : ouvrir, importer, récents
  auto* colonne = new QWidget;
  colonne->setObjectName(QStringLiteral("colonne"));
  colonne->setFixedWidth(280);
  auto* c = new QVBoxLayout(colonne);
  c->setContentsMargins(20, 24, 20, 20);
  c->setSpacing(10);
  auto* nom = new QLabel(QStringLiteral("<table cellspacing='0' cellpadding='0'><tr><td valign='middle'><img src=':/marque/icone.png' width='46' height='46'></td><td width='12'></td>"
                                       "<td valign='middle'><span style='font-size:26px; font-weight:700; color:#10142A'>Prométhée</span><br><span style='color:#6B7393; font-size:12px'>Plateforme libre d’ingénierie</span></td></tr></table>"));
  c->addWidget(nom);
  c->addSpacing(10);
  auto* nouveau = new QPushButton(icone(QStringLiteral("dossier")), QStringLiteral("  Nouveau projet vide"));
  nouveau->setObjectName(QStringLiteral("nouveauProjet"));
  nouveau->setIconSize(QSize(22, 22));
  nouveau->setToolTip(QStringLiteral("Un projet vide : ajoute ensuite pièces, cartes, schémas électriques depuis l’arbre à gauche"));
  nouveau->setStyleSheet(QStringLiteral("QPushButton { background: #5B4BF5; color: white; border: none; font-weight: 600; } QPushButton:hover { background: #4A3BE0; }"));
  connect(nouveau, &QPushButton::clicked, this, [this] { emit choisi(QStringLiteral("mixte-vide")); });
  c->addWidget(nouveau);
  auto* ouvrir = new QPushButton(icone(QStringLiteral("ouvrir")), QStringLiteral("  Ouvrir un projet…"));
  ouvrir->setIconSize(QSize(22, 22));
  connect(ouvrir, &QPushButton::clicked, this, [this] { emit choisi(QStringLiteral("ouvrir")); });
  c->addWidget(ouvrir);
  auto* importer = new QPushButton(icone(QStringLiteral("importer-cao")), QStringLiteral("  Importer…"));
  importer->setObjectName(QStringLiteral("importer"));
  importer->setIconSize(QSize(22, 22));
  auto* menuImport = new QMenu(importer);
  menuImport->addAction(icone(QStringLiteral("kicad")), QStringLiteral("Carte KiCad (.kicad_pcb)…"), this, [this] { emit choisi(QStringLiteral("kicad-ouvrir")); });
  menuImport->addAction(icone(QStringLiteral("importer-cao")), QStringLiteral("Fichier CAO (STEP, IGES, STL)…"), this, [this] { emit choisi(QStringLiteral("cao-ouvrir")); });
  menuImport->addAction(icone(QStringLiteral("cablage")), QStringLiteral("Projet QElectroTech (.qet)…"), this, [this] { emit choisi(QStringLiteral("electrique-qet")); });
  importer->setMenu(menuImport);
  c->addWidget(importer);
  c->addSpacing(14);
  c->addWidget(new QLabel(QStringLiteral("<span style='font-weight:600; color:#232A45; font-size:13px'>Récents</span>")));
  m_recents = new QVBoxLayout;
  m_recents->setSpacing(2);
  c->addLayout(m_recents);
  c->addStretch(1);
  // Apparence : le thème de couleur, choisi ici ou dans Affichage › Thème
  c->addWidget(new QLabel(QStringLiteral("<span style='font-weight:600; color:#232A45; font-size:13px'>Apparence</span>")));
  auto* themes = new QComboBox;
  themes->setObjectName(QStringLiteral("choixTheme"));
  for (IdTheme id : Theme::TOUS) {
    themes->addItem(Theme::nom(id), static_cast<int>(id));
    themes->setItemData(themes->count() - 1, Theme::description(id), Qt::ToolTipRole);
  }
  auto majTheme = [themes] {
    const QSignalBlocker bloque(themes);
    themes->setCurrentIndex(themes->findData(static_cast<int>(Theme::instance().choix())));
    themes->setToolTip(Theme::description(Theme::instance().choix()));
  };
  majTheme();
  connect(themes, &QComboBox::activated, this, [themes](int i) { Theme::instance().choisir(static_cast<IdTheme>(themes->itemData(i).toInt())); });
  connect(&Theme::instance(), &Theme::change, themes, majTheme);
  c->addWidget(themes);
  racine->addWidget(colonne);
  // Partie principale : une recherche, les quatre métiers, puis les créations d'un seul métier à la fois (celui
  // de la dernière fois), ou un aperçu « Pour commencer » ; rien ne déborde sur un écran ordinaire
  auto* defile = new QScrollArea;
  defile->setWidgetResizable(true);
  defile->setFrameShape(QFrame::NoFrame);
  defile->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* principal = new QWidget;
  principal->setObjectName(QStringLiteral("accueil"));
  auto* p = new QVBoxLayout(principal);
  p->setContentsMargins(40, 32, 40, 30);
  p->setSpacing(12);
  p->addWidget(new QLabel(QStringLiteral("<span style='font-size:28px; font-weight:700; color:#10142A'>Que veux-tu concevoir ?</span>")));
  m_recherche = new QLineEdit;
  m_recherche->setObjectName(QStringLiteral("rechercheAccueil"));
  m_recherche->setPlaceholderText(QStringLiteral("Tape ce que tu veux créer : pièce, carte, armoire, logement, photovoltaïque, faisceau…"));
  m_recherche->setClearButtonEnabled(true);
  m_recherche->setStyleSheet(QStringLiteral("QLineEdit { padding: 9px 12px; font-size: 14px; border: 1px solid #CDD3E4; border-radius: 9px; background: white; }"
                                            "QLineEdit:focus { border-color: #5B4BF5; }"));
  connect(m_recherche, &QLineEdit::textChanged, this, [this] { majChoix(); });
  connect(m_recherche, &QLineEdit::returnPressed, this, [this] {
    // Entrée : le premier résultat
    if (!m_premierResultat.isEmpty()) emit choisi(m_premierResultat);
  });
  p->addWidget(m_recherche);
  p->addSpacing(4);
  m_tuiles = new QWidget;
  auto* t = new QGridLayout(m_tuiles);
  t->setContentsMargins(0, 0, 0, 0);
  t->setSpacing(12);
  int col = 0;
  for (const auto& m : METIERS) {
    const QString code = QString::fromLatin1(m.code);
    // Un clic montre les créations du métier ; un second clic revient à l'aperçu
    auto* tu = new Tuile(m, [this, code] { choisirMetier(m_metier == code ? QString() : code); });
    tu->setObjectName(QStringLiteral("metier-%1").arg(code));
    t->addWidget(tu, col / 4, col % 4);
    ++col;
  }
  p->addWidget(m_tuiles);
  p->addSpacing(4);
  m_consigne = new QLabel;
  m_consigne->setWordWrap(true);
  p->addWidget(m_consigne);
  auto carte = [this](const Choix& ch, const QString& prefixe) {
    QColor couleur;
    for (const auto& m : METIERS)
      if (QString::fromLatin1(m.code) == QString::fromLatin1(ch.metier)) couleur = m.couleur;
    const QString code = QString::fromLatin1(ch.code);
    auto* w = new CarteChoix(ch, couleur, [this, code] { emit choisi(code); });
    w->setObjectName(prefixe + code);
    return w;
  };
  // Aperçu : les débuts les plus courants de chaque métier, et les exemples à explorer
  {
    m_commencer = new QWidget;
    m_commencer->setObjectName(QStringLiteral("section-commencer"));
    auto* v = new QVBoxLayout(m_commencer);
    v->setContentsMargins(0, 6, 0, 4);
    v->setSpacing(8);
    auto ligne = [&](const QString& titre, const std::vector<const char*>& codes) {
      v->addWidget(new QLabel(QStringLiteral("<span style='font-size:17px; font-weight:700; color:#232A45'>%1</span>").arg(titre)));
      auto* g = new QGridLayout;
      g->setSpacing(12);
      int n = 0;
      for (const char* code : codes)
        for (const auto& ch : CHOIX)
          if (QString::fromLatin1(ch.code) == QString::fromLatin1(code)) {
            g->addWidget(carte(ch, QStringLiteral("commencer-")), n / 2, n % 2);
            ++n;
          }
      v->addLayout(g);
    };
    ligne(QStringLiteral("Pour commencer"), {"piece", "assemblage", "carte-nouvelle", "electrique-schema", "electrique-distribution", "electrique-logement"});
    ligne(QStringLiteral("Exemples à explorer"), {"exemple-elec", "electrique-exemple"});
    p->addWidget(m_commencer);
  }
  // Les créations de chaque métier ; une seule section visible à la fois
  for (const auto& m : METIERS) {
    auto* section = new QWidget;
    section->setObjectName(QStringLiteral("section-%1").arg(QString::fromLatin1(m.code)));
    auto* v = new QVBoxLayout(section);
    v->setContentsMargins(0, 6, 0, 4);
    v->setSpacing(8);
    auto* t = new QLabel(QStringLiteral("<span style='font-size:17px; font-weight:700; color:%1'>%2</span>&nbsp;&nbsp;<span style='color:#6B7393'>%3</span>")
                             .arg(m.couleur.name(), QString::fromUtf8(m.titre), QString::fromUtf8(m.description)));
    v->addWidget(t);
    auto* g = new QGridLayout;
    g->setSpacing(12);
    int n = 0;
    for (const auto& ch : CHOIX) {
      if (QString::fromLatin1(ch.metier) != QString::fromLatin1(m.code)) continue;
      g->addWidget(carte(ch, QStringLiteral("choix-")), n / 2, n % 2);
      ++n;
    }
    v->addLayout(g);
    p->addWidget(section);
    m_sections[QString::fromLatin1(m.code)] = section;
  }
  // Résultats de la recherche, tous métiers confondus
  m_resultats = new QWidget;
  m_resultats->setObjectName(QStringLiteral("section-resultats"));
  auto* vr = new QVBoxLayout(m_resultats);
  vr->setContentsMargins(0, 6, 0, 4);
  vr->setSpacing(8);
  m_titreResultats = new QLabel;
  vr->addWidget(m_titreResultats);
  m_grilleResultats = new QGridLayout;
  m_grilleResultats->setSpacing(12);
  vr->addLayout(m_grilleResultats);
  p->addWidget(m_resultats);
  p->addStretch(1);
  defile->setWidget(principal);
  m_defile = defile;
  racine->addWidget(defile, 1);
  // Le métier de la dernière fois (le plus souvent, on travaille dans le même)
  m_metier = QSettings().value(QStringLiteral("accueil/metier")).toString();
  if (std::none_of(std::begin(METIERS), std::end(METIERS), [this](const Metier& x) { return QString::fromLatin1(x.code) == m_metier; })) m_metier.clear();
  majChoix();
}

void Accueil::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  // Quatre tuiles de front si la place le permet, sinon deux rangées de deux.
  auto* t = static_cast<QGridLayout*>(m_tuiles->layout());
  const int disponible = width() - 280 - 80 - 20;  // largeur de la partie principale (colonne et marges ôtées)
  const int colonnes = disponible >= 4 * 185 ? 4 : 2;
  std::vector<QWidget*> ws;
  for (int i = 0; i < t->count(); ++i) ws.push_back(t->itemAt(i)->widget());
  if (static_cast<int>(ws.size()) == 4 && m_colonnes != colonnes) {
    m_colonnes = colonnes;
    for (QWidget* w : ws) t->removeWidget(w);
    for (int i = 0; i < 4; ++i) t->addWidget(ws[i], i / colonnes, i % colonnes);
  }
}

void Accueil::choisirMetier(const QString& metier) {
  m_metier = metier;
  QSettings().setValue(QStringLiteral("accueil/metier"), metier);
  if (m_recherche && !m_recherche->text().isEmpty()) {
    const QSignalBlocker b(m_recherche);
    m_recherche->clear();
  }
  majChoix();
}

namespace {
// Texte comparable : minuscules, sans accents
QString sansAccents(const QString& t) {
  QString r;
  for (const QChar c : t.normalized(QString::NormalizationForm_D))
    if (c.category() != QChar::Mark_NonSpacing) r += c.toLower();
  return r;
}
}  // namespace

void Accueil::majChoix() {
  const QString recherche = m_recherche ? sansAccents(m_recherche->text().trimmed()) : QString();
  const auto tuiles = m_tuiles->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
  int i = 0;
  for (QWidget* w : tuiles)
    if (auto* tu = dynamic_cast<Tuile*>(w)) {
      tu->setEtat(recherche.isEmpty() && i < 4 && QString::fromLatin1(METIERS[i].code) == m_metier, recherche.isEmpty() && !m_metier.isEmpty());
      ++i;
    }
  m_premierResultat.clear();
  // Recherche : les créations de tous les métiers dont le nom, la description ou les mots-clés répondent
  if (!recherche.isEmpty()) {
    for (auto& [code, section] : m_sections) section->hide();
    m_commencer->hide();
    while (QLayoutItem* it = m_grilleResultats->takeAt(0)) {
      if (it->widget()) it->widget()->deleteLater();
      delete it;
    }
    int n = 0;
    for (const auto& ch : CHOIX) {
      QString metier;
      QColor couleur;
      for (const auto& m : METIERS)
        if (QString::fromLatin1(m.code) == QString::fromLatin1(ch.metier)) {
          metier = QString::fromUtf8(m.titre);
          couleur = m.couleur;
        }
      const QString texte = sansAccents(QString::fromUtf8(ch.titre) + QLatin1Char(' ') + QString::fromUtf8(ch.description) + QLatin1Char(' ') + QString::fromUtf8(ch.motsCles) +
                                        QLatin1Char(' ') + metier);
      bool ok = true;
      for (const QString& mot : recherche.split(QLatin1Char(' '), Qt::SkipEmptyParts)) ok = ok && texte.contains(mot);
      if (!ok) continue;
      const QString code = QString::fromLatin1(ch.code);
      if (m_premierResultat.isEmpty()) m_premierResultat = code;
      auto* w = new CarteChoix(ch, couleur, [this, code] { emit choisi(code); });
      w->setObjectName(QStringLiteral("resultat-") + code);
      m_grilleResultats->addWidget(w, n / 2, n % 2);
      ++n;
    }
    m_titreResultats->setText(n > 0 ? QStringLiteral("<span style='font-size:17px; font-weight:700; color:#232A45'>%1 résultat(s)</span>&nbsp;&nbsp;<span style='color:#6B7393'>Entrée ouvre le premier</span>").arg(n)
                                    : QStringLiteral("<span style='font-size:15px; color:#6B7393'>Rien ne correspond. Essaie un autre mot, ou choisis un métier.</span>"));
    m_resultats->show();
    m_consigne->hide();
    return;
  }
  m_resultats->hide();
  m_consigne->show();
  const Metier* m = nullptr;
  for (const auto& x : METIERS)
    if (QString::fromLatin1(x.code) == m_metier) m = &x;
  for (auto& [code, section] : m_sections) section->setVisible(m && code == m_metier);
  m_commencer->setVisible(!m);
  if (!m) {
    m_consigne->setText(QStringLiteral("<span style='font-size:14px; color:#6B7393'>Choisis un métier pour voir tout ce qu’il propose, ou commence par l’un de ces choix.</span>"));
    return;
  }
  m_consigne->setText(QStringLiteral("<span style='font-size:14px; color:%1'>%2 : choisis ce que tu veux créer.</span>&nbsp; <span style='font-size:13px; color:#6B7393'>Un second clic sur la tuile revient à l’aperçu.</span>")
                          .arg(m->couleur.name(), QString::fromUtf8(m->titre)));
}

void Accueil::setRecents(const QStringList& chemins) {
  while (QLayoutItem* it = m_recents->takeAt(0)) {
    if (it->widget()) it->widget()->deleteLater();
    delete it;
  }
  if (chemins.isEmpty()) {
    auto* vide = new QLabel(QStringLiteral("<span style='color:#8A91AC; font-size:12px'>Aucun projet récent.</span>"));
    m_recents->addWidget(vide);
    return;
  }
  // Deux projets du même nom : les dossiers qui les distinguent sont dits (un, puis deux niveaux)
  auto libelle = [](const QString& chemin, int niveaux) {
    const QFileInfo fi(chemin);
    if (niveaux == 0) return fi.completeBaseName();
    QStringList dossiers = QDir::cleanPath(fi.absolutePath()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    while (dossiers.size() > niveaux) dossiers.removeFirst();
    return QStringLiteral("%1  (%2)").arg(fi.completeBaseName(), dossiers.join(QLatin1Char('/')));
  };
  const QStringList liste = chemins.mid(0, 8);
  std::map<QString, int> niveaux;
  for (int n = 0; n < 3; ++n) {
    std::map<QString, int> memes;
    for (const QString& e : liste) ++memes[libelle(e.section(QLatin1Char('|'), 0, 0), niveaux[e])];
    bool encore = false;
    for (const QString& e : liste)
      if (memes[libelle(e.section(QLatin1Char('|'), 0, 0), niveaux[e])] > 1 && niveaux[e] < 2) {
        ++niveaux[e];
        encore = true;
      }
    if (!encore) break;
  }
  for (const QString& entree : liste) {
    const QString chemin = entree.section(QLatin1Char('|'), 0, 0);
    const QStringList metiers = entree.section(QLatin1Char('|'), 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
    // Le nom en entier, le dossier raccourci par le milieu s'il ne tient pas dans la colonne (chemin complet en bulle)
    auto* b = new QPushButton;
    b->setText(b->fontMetrics().elidedText(libelle(chemin, niveaux[entree]), Qt::ElideMiddle, 210));
    b->setObjectName(QStringLiteral("recent"));
    b->setToolTip(chemin);
    QIcon ic;
    if (metiers.size() > 1) ic = icone(QStringLiteral("metier-mixte"));
    else if (metiers.contains(QStringLiteral("elec"))) ic = icone(QStringLiteral("metier-electronique"));
    else if (metiers.contains(QStringLiteral("electrique"))) ic = icone(QStringLiteral("metier-electrique"));
    else ic = icone(QStringLiteral("metier-meca"));
    b->setIcon(ic);
    b->setIconSize(QSize(18, 18));
    connect(b, &QPushButton::clicked, this, [this, chemin] { emit ouvrirRecent(chemin); });
    m_recents->addWidget(b);
  }
}

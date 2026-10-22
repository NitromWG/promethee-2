// Prométhée : fenêtre de mise en plan. Licence GPL-3.0-only.
#include "MiseEnPlan.hpp"

#include "Theme.hpp"

#include <QCheckBox>
#include <QFrame>
#include <QSpinBox>
#include <QScrollArea>
#include <QPlainTextEdit>
#include <QLocale>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDate>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPushButton>
#include <QSettings>
#include <QFontMetricsF>
#include <QVBoxLayout>

#include "Icones.hpp"

using namespace prom;

namespace {
// Dessine la feuille dans un rectangle (aperçu à l'écran ou page PDF), y vers le haut comme sur un plan.
void peindreFeuille(QPainter& g, const Feuille& f, const QRectF& zone, bool ecran) {
  const double k = std::min(zone.width() / f.largeur, zone.height() / f.hauteur);
  const QPointF o(zone.left() + (zone.width() - f.largeur * k) / 2, zone.top() + (zone.height() - f.hauteur * k) / 2);
  auto P = [&](double x, double y) { return QPointF(o.x() + x * k, o.y() + (f.hauteur - y) * k); };
  g.setRenderHint(QPainter::Antialiasing);
  g.fillRect(QRectF(P(0, f.hauteur), P(f.largeur, 0)), Qt::white);
  if (ecran) { g.setPen(QPen(theme().bordure(QColor(0xCD, 0xD3, 0xE4)), 1)); g.drawRect(QRectF(P(0, f.hauteur), P(f.largeur, 0))); }
  // Mêmes épaisseurs et motifs ISO 128 qu'en SVG et en DXF (MP-10) ; cachés tracés avant les vus (MP-03)
  auto traits = [&](const Trait2D& t, const MotifTrait& m, const QColor& couleur) {
    const double largeurPinceau = std::max(ecran ? 0.8 : 0.0, m.epaisseur * k);
    QPen pen(couleur, largeurPinceau);
    if (!m.motif.empty()) {
      QList<qreal> motif;
      for (double x : m.motif) motif << std::max(0.5, x * k / largeurPinceau);  // Qt : multiples de l'épaisseur
      pen.setDashPattern(motif);
    }
    g.setPen(pen);
    QPainterPath ch;
    for (size_t i = 0; i < t.points.size(); ++i) i ? ch.lineTo(P(t.points[i].first, t.points[i].second)) : ch.moveTo(P(t.points[i].first, t.points[i].second));
    if (t.rempli) {
      ch.closeSubpath();
      g.fillPath(ch, couleur);
    }
    g.drawPath(ch);
  };
  // Hachures sous tout le reste, cotes et traits fins avant les contours vus
  for (TypeTrait type : {TypeTrait::Hachure, TypeTrait::Cache, TypeTrait::Axe, TypeTrait::Tangente, TypeTrait::Fin, TypeTrait::Cote, TypeTrait::Vu})
    for (const auto& v : f.vues)
      for (const auto& t : v.traits)
        if (t.type == type)
          traits(t, motifTrait(type), type == TypeTrait::Cache ? QColor(0x55, 0x55, 0x55) : type == TypeTrait::Axe ? QColor(0x44, 0x44, 0x44) : type == TypeTrait::Hachure ? QColor(0x33, 0x33, 0x33) : QColor(Qt::black));
  for (const auto& t : f.cadre) traits(t, MotifTrait{EPAISSEUR_CADRE, {}}, Qt::black);
  for (const auto& x : f.textes) {
    QFont fo(QStringLiteral("Sans Serif"));
    fo.setPixelSize(std::max(1, static_cast<int>(x.taille * k)));
    fo.setBold(x.gras);
    g.setFont(fo);
    g.setPen(Qt::black);
    const QString texte = QString::fromStdString(x.texte);
    const QPointF ancre = P(x.x, x.y);
    const double decalage = x.centre ? -QFontMetricsF(fo).horizontalAdvance(texte) / 2 : 0;
    g.save();
    g.translate(ancre);
    if (x.angle != 0) g.rotate(-x.angle);
    g.drawText(QPointF(decalage, 0), texte);
    g.restore();
  }
}
}  // namespace

class ApercuFeuille : public QWidget {
public:
  const Feuille* feuille = nullptr;
  QSize sizeHint() const override { return {760, 540}; }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter g(this);
    g.fillRect(rect(), theme().fond(QColor(0xE9, 0xEC, 0xF5)));  // la feuille reste blanche, comme imprimée
    if (feuille) peindreFeuille(g, *feuille, QRectF(rect()).adjusted(16, 16, -16, -16), true);
  }
};

MiseEnPlan::MiseEnPlan(const TopoDS_Shape& forme, const PlanPiece& reglages, const std::string& titre, const std::string& materiau, QWidget* parent,
                       const std::string& reference, const std::string& masse)
    : QDialog(parent), m_forme(forme), m_reglages(reglages), m_titre(titre), m_materiau(materiau), m_reference(reference), m_masse(masse) {
  setWindowTitle(QStringLiteral("Mise en plan — %1").arg(QString::fromStdString(titre)));
  auto* racine = new QHBoxLayout(this);
  auto* cote = new QVBoxLayout;
  auto* form = new QFormLayout;
  auto* nom = new QLineEdit(QString::fromStdString(m_reglages.nom));
  connect(nom, &QLineEdit::editingFinished, this, [this, nom] { m_reglages.nom = nom->text().trimmed().toStdString(); });
  form->addRow(QStringLiteral("Nom"), nom);
  auto* format = new QComboBox;
  format->setObjectName(QStringLiteral("formatFeuille"));
  format->addItems({QStringLiteral("A4"), QStringLiteral("A3"), QStringLiteral("A2"), QStringLiteral("A1"), QStringLiteral("A0")});
  format->setCurrentIndex(std::max(0, format->findText(QString::fromStdString(m_reglages.format))));
  connect(format, &QComboBox::currentIndexChanged, this, [this, format] { m_reglages.format = format->currentText().toStdString(); recalculer(); });
  auto* sens = new QComboBox;
  sens->setObjectName(QStringLiteral("sensFeuille"));
  sens->addItem(QStringLiteral("À l’italienne"), false);
  sens->addItem(QStringLiteral("À la française"), true);
  sens->setCurrentIndex(m_reglages.portrait ? 1 : 0);
  connect(sens, &QComboBox::currentIndexChanged, this, [this, sens] { m_reglages.portrait = sens->currentData().toBool(); recalculer(); });
  auto* ligneFormat = new QHBoxLayout;
  ligneFormat->addWidget(format);
  ligneFormat->addWidget(sens, 1);
  form->addRow(QStringLiteral("Format"), ligneFormat);
  // Projection et orientation (D1)
  auto* diedre = new QComboBox;
  diedre->setObjectName(QStringLiteral("projection"));
  diedre->addItem(QStringLiteral("Européenne (premier dièdre)"), false);
  diedre->addItem(QStringLiteral("Américaine (troisième dièdre)"), true);
  diedre->setCurrentIndex(m_reglages.troisiemeDiedre ? 1 : 0);
  connect(diedre, &QComboBox::currentIndexChanged, this, [this, diedre] { m_reglages.troisiemeDiedre = diedre->currentData().toBool(); recalculer(); });
  form->addRow(QStringLiteral("Projection"), diedre);
  auto* face = new QComboBox;
  face->setObjectName(QStringLiteral("vueFace"));
  for (const auto& [code, titreFace] : {std::pair{"avant", "de l’avant"}, std::pair{"arriere", "de l’arrière"}, std::pair{"dessus", "du dessus"}, std::pair{"dessous", "du dessous"},
                                        std::pair{"gauche", "de la gauche"}, std::pair{"droite", "de la droite"}})
    face->addItem(QStringLiteral("Pièce vue %1").arg(QString::fromUtf8(titreFace)), QString::fromLatin1(code));
  face->setCurrentIndex(std::max(0, face->findData(QString::fromStdString(m_reglages.vueFace))));
  connect(face, &QComboBox::currentIndexChanged, this, [this, face] { m_reglages.vueFace = face->currentData().toString().toStdString(); recalculer(); });
  auto* rotation = new QComboBox;
  rotation->setObjectName(QStringLiteral("rotationFace"));
  for (int a : {0, 90, 180, 270}) rotation->addItem(QStringLiteral("%1°").arg(a), a);
  rotation->setCurrentIndex(std::max(0, rotation->findData(m_reglages.rotationFace)));
  rotation->setToolTip(QStringLiteral("Rotation de la vue de face dans son plan"));
  connect(rotation, &QComboBox::currentIndexChanged, this, [this, rotation] { m_reglages.rotationFace = rotation->currentData().toInt(); recalculer(); });
  auto* ligneFace = new QHBoxLayout;
  ligneFace->addWidget(face, 1);
  ligneFace->addWidget(rotation);
  form->addRow(QStringLiteral("Vue de face"), ligneFace);
  auto* verticale = new QComboBox;
  verticale->setObjectName(QStringLiteral("vueVerticale"));
  verticale->addItem(QStringLiteral("de dessus"), QStringLiteral("dessus"));
  verticale->addItem(QStringLiteral("de dessous"), QStringLiteral("dessous"));
  verticale->setCurrentIndex(m_reglages.vueVerticale == "dessous" ? 1 : 0);
  connect(verticale, &QComboBox::currentIndexChanged, this, [this, verticale] { m_reglages.vueVerticale = verticale->currentData().toString().toStdString(); recalculer(); });
  auto* laterale = new QComboBox;
  laterale->setObjectName(QStringLiteral("vueLaterale"));
  laterale->addItem(QStringLiteral("de gauche"), QStringLiteral("gauche"));
  laterale->addItem(QStringLiteral("de droite"), QStringLiteral("droite"));
  laterale->setCurrentIndex(m_reglages.vueLaterale == "droite" ? 1 : 0);
  connect(laterale, &QComboBox::currentIndexChanged, this, [this, laterale] { m_reglages.vueLaterale = laterale->currentData().toString().toStdString(); recalculer(); });
  auto* ligneProjetees = new QHBoxLayout;
  ligneProjetees->addWidget(verticale, 1);
  ligneProjetees->addWidget(laterale, 1);
  form->addRow(QStringLiteral("Vues projetées"), ligneProjetees);
  auto* echelle = new QComboBox;
  const std::pair<const char*, double> echelles[] = {{"Automatique", 0}, {"5:1", 5}, {"2:1", 2}, {"1:1", 1}, {"1:2", 0.5}, {"1:5", 0.2}, {"1:10", 0.1}, {"1:20", 0.05}};
  for (const auto& [t, v] : echelles) echelle->addItem(QString::fromUtf8(t), v);
  for (int i = 0; i < echelle->count(); ++i) if (std::abs(echelle->itemData(i).toDouble() - m_reglages.echelle) < 1e-9) echelle->setCurrentIndex(i);
  connect(echelle, &QComboBox::currentIndexChanged, this, [this, echelle] { m_reglages.echelle = echelle->currentData().toDouble(); recalculer(); });
  form->addRow(QStringLiteral("Échelle"), echelle);
  auto coche = [&](const QString& t, bool& champ) {
    auto* c = new QCheckBox(t);
    c->setChecked(champ);
    connect(c, &QCheckBox::toggled, this, [this, &champ](bool on) { champ = on; recalculer(); });
    form->addRow(QString(), c);
  };
  coche(QStringLiteral("Vue de face"), m_reglages.face);
  coche(QStringLiteral("Vue projetée verticale"), m_reglages.dessus);
  coche(QStringLiteral("Vue projetée latérale"), m_reglages.gauche);
  coche(QStringLiteral("Vue isométrique"), m_reglages.iso);
  coche(QStringLiteral("Arêtes cachées en tirets"), m_reglages.cachees);
  coche(QStringLiteral("Axes des trous et des cylindres"), m_reglages.axes);
  coche(QStringLiteral("Arêtes tangentes (trait fin)"), m_reglages.tangentes);
  coche(QStringLiteral("Cotes d’encombrement et diamètres"), m_reglages.cotes);
  coche(QStringLiteral("Cotes du modèle (esquisses)"), m_reglages.cotesModele);
  // Coupe A-A
  auto* coupe = new QComboBox;
  coupe->setObjectName(QStringLiteral("coupe"));
  coupe->addItem(QStringLiteral("Aucune"), QString());
  coupe->addItem(QStringLiteral("Vue de face en coupe A-A"), QStringLiteral("face"));
  coupe->addItem(QStringLiteral("Vue projetée verticale en coupe A-A"), QStringLiteral("dessus"));
  coupe->addItem(QStringLiteral("Vue projetée latérale en coupe A-A"), QStringLiteral("gauche"));
  coupe->setCurrentIndex(std::max(0, coupe->findData(QString::fromStdString(m_reglages.coupe))));
  connect(coupe, &QComboBox::currentIndexChanged, this, [this, coupe] { m_reglages.coupe = coupe->currentData().toString().toStdString(); recalculer(); });
  form->addRow(QStringLiteral("Coupe"), coupe);
  auto* decalage = new QDoubleSpinBox;
  decalage->setRange(-1e5, 1e5);
  decalage->setDecimals(2);
  decalage->setSuffix(QStringLiteral(" mm"));
  decalage->setValue(m_reglages.decalageCoupe);
  decalage->setToolTip(QStringLiteral("Plan de coupe décalé du centre de la pièce, vers l’observateur"));
  connect(decalage, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_reglages.decalageCoupe = v; recalculer(); });
  form->addRow(QStringLiteral("Plan de coupe"), decalage);
  auto* demi = new QCheckBox(QStringLiteral("Demi-coupe (pièce symétrique)"));
  demi->setObjectName(QStringLiteral("demiCoupe"));
  demi->setChecked(m_reglages.demiCoupe);
  connect(demi, &QCheckBox::toggled, this, [this](bool on) { m_reglages.demiCoupe = on; recalculer(); });
  form->addRow(QString(), demi);
  // Vue de détail
  auto* detail = new QCheckBox(QStringLiteral("Vue de détail B"));
  detail->setObjectName(QStringLiteral("detail"));
  detail->setChecked(m_reglages.detail);
  connect(detail, &QCheckBox::toggled, this, [this](bool on) { m_reglages.detail = on; recalculer(); });
  form->addRow(QString(), detail);
  auto* vueDetail = new QComboBox;
  vueDetail->addItem(QStringLiteral("dans la vue de face"), QStringLiteral("face"));
  vueDetail->addItem(QStringLiteral("dans la vue de dessus"), QStringLiteral("dessus"));
  vueDetail->addItem(QStringLiteral("dans la vue de gauche"), QStringLiteral("gauche"));
  vueDetail->setCurrentIndex(std::max(0, vueDetail->findData(QString::fromStdString(m_reglages.vueDetail))));
  connect(vueDetail, &QComboBox::currentIndexChanged, this, [this, vueDetail] { m_reglages.vueDetail = vueDetail->currentData().toString().toStdString(); recalculer(); });
  form->addRow(QStringLiteral("Détail"), vueDetail);
  auto pourcent = [this](double v, double mini, double maxi, std::function<void(double)> ecrire) {
    auto* b = new QSpinBox;
    b->setRange(static_cast<int>(mini * 100), static_cast<int>(maxi * 100));
    b->setSuffix(QStringLiteral(" %"));
    b->setValue(static_cast<int>(std::lround(v * 100)));
    connect(b, &QSpinBox::valueChanged, this, [this, ecrire](int x) { ecrire(x / 100.0); recalculer(); });
    return b;
  };
  {
    auto* ligne = new QHBoxLayout;
    ligne->addWidget(pourcent(m_reglages.detailX, -0.5, 0.5, [this](double x) { m_reglages.detailX = x; }));
    ligne->addWidget(pourcent(m_reglages.detailY, -0.5, 0.5, [this](double x) { m_reglages.detailY = x; }));
    form->addRow(QStringLiteral("Centre (X, Y)"), ligne);
  }
  form->addRow(QStringLiteral("Rayon"), pourcent(m_reglages.detailRayon, 0.02, 0.5, [this](double x) { m_reglages.detailRayon = x; }));
  auto* agrandi = new QComboBox;
  for (const double k : {1.5, 2.0, 2.5, 4.0, 5.0, 10.0}) agrandi->addItem(QStringLiteral("× %1").arg(QLocale().toString(k)), k);
  agrandi->setCurrentIndex(std::max(0, agrandi->findData(m_reglages.detailEchelle)));
  connect(agrandi, &QComboBox::currentIndexChanged, this, [this, agrandi] { m_reglages.detailEchelle = agrandi->currentData().toDouble(); recalculer(); });
  form->addRow(QStringLiteral("Agrandissement"), agrandi);
  // Vue interrompue
  auto* interrompue = new QCheckBox(QStringLiteral("Vues de face et de dessus interrompues"));
  interrompue->setObjectName(QStringLiteral("interrompue"));
  interrompue->setChecked(m_reglages.interrompue);
  connect(interrompue, &QCheckBox::toggled, this, [this](bool on) { m_reglages.interrompue = on; recalculer(); });
  form->addRow(QString(), interrompue);
  {
    auto* ligne = new QHBoxLayout;
    ligne->addWidget(pourcent(m_reglages.interruptionDebut, 0.05, 0.9, [this](double x) { m_reglages.interruptionDebut = x; }));
    ligne->addWidget(pourcent(m_reglages.interruptionFin, 0.1, 0.95, [this](double x) { m_reglages.interruptionFin = x; }));
    form->addRow(QStringLiteral("Partie retirée"), ligne);
  }
  // Notes et tolérances générales
  auto* tolerance = new QComboBox;
  tolerance->setEditable(true);
  tolerance->addItems({QString(), QStringLiteral("ISO 2768-fH"), QStringLiteral("ISO 2768-mK"), QStringLiteral("ISO 2768-cL")});
  tolerance->setCurrentText(QString::fromStdString(m_reglages.toleranceGenerale));
  connect(tolerance, &QComboBox::currentTextChanged, this, [this](const QString& t) { m_reglages.toleranceGenerale = t.trimmed().toStdString(); recalculer(); });
  form->addRow(QStringLiteral("Tolérances générales"), tolerance);
  auto* notes = new QPlainTextEdit;
  notes->setObjectName(QStringLiteral("notes"));
  notes->setPlaceholderText(QStringLiteral("Une note par ligne (« Arêtes cassées 0,5 × 45° »)"));
  notes->setMaximumHeight(70);
  {
    QStringList l;
    for (const auto& n : m_reglages.notes) l << QString::fromStdString(n);
    notes->setPlainText(l.join(QLatin1Char('\n')));
  }
  connect(notes, &QPlainTextEdit::textChanged, this, [this, notes] {
    m_reglages.notes.clear();
    for (const QString& l : notes->toPlainText().split(QLatin1Char('\n')))
      if (!l.trimmed().isEmpty()) m_reglages.notes.push_back(l.trimmed().toStdString());
    recalculer();
  });
  form->addRow(QStringLiteral("Notes"), notes);
  // Auteur : mémorisé pour les plans suivants (MP-06)
  auto* auteur = new QLineEdit(QSettings().value(QStringLiteral("miseEnPlan/auteur")).toString());
  auteur->setPlaceholderText(QStringLiteral("nom ou initiales"));
  connect(auteur, &QLineEdit::editingFinished, this, [this, auteur] {
    QSettings().setValue(QStringLiteral("miseEnPlan/auteur"), auteur->text().trimmed());
    recalculer();
  });
  form->addRow(QStringLiteral("Dessiné par"), auteur);
  // Propriétaire légal du document (ISO 7200), mémorisé comme l'auteur
  auto* proprietaire = new QLineEdit(QSettings().value(QStringLiteral("miseEnPlan/proprietaire")).toString());
  proprietaire->setObjectName(QStringLiteral("proprietaire"));
  proprietaire->setPlaceholderText(QStringLiteral("entreprise ou atelier"));
  connect(proprietaire, &QLineEdit::editingFinished, this, [this, proprietaire] {
    QSettings().setValue(QStringLiteral("miseEnPlan/proprietaire"), proprietaire->text().trimmed());
    recalculer();
  });
  form->addRow(QStringLiteral("Propriétaire"), proprietaire);
  cote->addLayout(form);
  m_alertes = new QLabel;
  m_alertes->setWordWrap(true);
  m_alertes->setStyleSheet(QStringLiteral("color: #A8620E;"));
  cote->addWidget(m_alertes);
  auto exporter = [this](const QString& filtre, const QString& extension) {
    const QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter la mise en plan"), QString::fromStdString(m_titre) + extension, filtre);
    if (chemin.isEmpty()) return;
    bool ok = false;
    if (extension == QStringLiteral(".pdf")) ok = exporterPdf(m_feuille, chemin);
    else if (extension == QStringLiteral(".svg")) ok = ecrireSvg(m_feuille, chemin.toStdString());
    else ok = ecrireDxf(m_feuille, chemin.toStdString());
    if (!ok) QMessageBox::warning(this, QStringLiteral("Export"), QStringLiteral("Le fichier n’a pas pu être écrit."));
  };
  for (const auto& [t, filtre, ext] : {std::tuple{"Exporter en PDF…", "PDF (*.pdf)", ".pdf"}, std::tuple{"Exporter en SVG…", "SVG (*.svg)", ".svg"}, std::tuple{"Exporter en DXF…", "DXF (*.dxf)", ".dxf"}}) {
    auto* b = new QPushButton(icone(QStringLiteral("exporter")), QString::fromUtf8(t));
    const QString f2 = QString::fromLatin1(filtre), e2 = QString::fromLatin1(ext);
    connect(b, &QPushButton::clicked, this, [exporter, f2, e2] { exporter(f2, e2); });
    cote->addWidget(b);
  }
  cote->addStretch(1);
  m_fermer = new QPushButton(QStringLiteral("Fermer"));
  connect(m_fermer, &QPushButton::clicked, this, &QDialog::accept);
  cote->addWidget(m_fermer);
  m_apercu = new ApercuFeuille;
  // Les réglages défilent quand la fenêtre est basse
  auto* defile = new QScrollArea;
  defile->setWidgetResizable(true);
  defile->setFrameShape(QFrame::NoFrame);
  auto* panneau = new QWidget;
  panneau->setLayout(cote);
  defile->setWidget(panneau);
  defile->setMinimumWidth(360);
  racine->addWidget(defile);
  racine->addWidget(m_apercu, 1);
  recalculer();
  resize(1080, 640);
}

void MiseEnPlan::setCotesModele(std::vector<OptionsMiseEnPlan::CoteModele> cotes, std::vector<std::string> orphelines) {
  m_cotesModele = std::move(cotes);
  m_cotesOrphelines = std::move(orphelines);
  recalculer();
}

void MiseEnPlan::setProprietes(std::vector<std::pair<std::string, std::string>> proprietes) {
  m_proprietes = std::move(proprietes);
  recalculer();
}

void MiseEnPlan::recalculer() {
  OptionsMiseEnPlan o;
  o.format = m_reglages.format;
  o.portrait = m_reglages.portrait;
  o.troisiemeDiedre = m_reglages.troisiemeDiedre;
  o.vueFace = m_reglages.vueFace;
  o.rotationFace = m_reglages.rotationFace;
  o.vueVerticale = m_reglages.vueVerticale;
  o.vueLaterale = m_reglages.vueLaterale;
  o.echelle = m_reglages.echelle;
  o.face = m_reglages.face;
  o.dessus = m_reglages.dessus;
  o.gauche = m_reglages.gauche;
  o.iso = m_reglages.iso;
  o.cachees = m_reglages.cachees;
  o.axes = m_reglages.axes;
  o.tangentes = m_reglages.tangentes;
  o.coupe = m_reglages.coupe;
  o.decalageCoupe = m_reglages.decalageCoupe;
  o.demiCoupe = m_reglages.demiCoupe;
  o.detail = m_reglages.detail;
  o.vueDetail = m_reglages.vueDetail;
  o.detailX = m_reglages.detailX;
  o.detailY = m_reglages.detailY;
  o.detailRayon = m_reglages.detailRayon;
  o.detailEchelle = m_reglages.detailEchelle;
  o.interrompue = m_reglages.interrompue;
  o.interruptionDebut = m_reglages.interruptionDebut;
  o.interruptionFin = m_reglages.interruptionFin;
  o.cotes = m_reglages.cotes;
  o.notes = m_reglages.notes;
  o.toleranceGenerale = m_reglages.toleranceGenerale;
  o.nomenclature = m_nomenclature;
  o.bulles = m_bulles;
  o.filetages = m_filetages;
  if (m_reglages.cotesModele) {
    o.cotesModele = m_cotesModele;
    o.cotesOrphelines = m_cotesOrphelines;
  }
  o.titre = m_titre;
  o.materiau = m_materiau;
  o.reference = m_reference;
  o.masse = m_masse;
  o.auteur = QSettings().value(QStringLiteral("miseEnPlan/auteur")).toString().trimmed().toStdString();
  {
    Piece props;
    props.proprietes = m_proprietes;
    o.revision = props.propriete("Révision");
    if (o.revision.empty()) o.revision = props.propriete("Indice");
    o.designation = props.propriete("Désignation");
    o.projet = props.propriete("Projet");
    if (const std::string a = props.propriete("Auteur"); !a.empty()) o.auteur = a;  // l'auteur de la pièce l'emporte sur le réglage
    // Cartouche ISO 7200 (D2) : propriétaire légal (réglage de l'atelier, ou propriété de la pièce), approbateur,
    // type de document (plan d'ensemble pour un assemblage)
    o.proprietaire = QSettings().value(QStringLiteral("miseEnPlan/proprietaire")).toString().trimmed().toStdString();
    if (const std::string pr = props.propriete("Propriétaire"); !pr.empty()) o.proprietaire = pr;
    o.approbateur = props.propriete("Approuvé par");
    if (o.approbateur.empty()) o.approbateur = props.propriete("Approbateur");
    if (!m_nomenclature.empty()) o.typeDocument = "Plan d'ensemble";
    if (const std::string t = props.propriete("Type de document"); !t.empty()) o.typeDocument = t;
  }
  o.date = QDate::currentDate().toString(QStringLiteral("dd/MM/yyyy")).toStdString();
  m_feuille = miseEnPlan(m_forme, o);
  if (m_alertes) {
    QStringList a;
    for (const auto& x : m_feuille.alertes) a << QString::fromStdString(x);
    if (!m_feuille.tientDansLeCadre) a << QStringLiteral("Les vues dépassent du cadre à cette échelle : choisis « Automatique » ou un format plus grand.");
    m_alertes->setText(a.join(QStringLiteral("<br>")));
  }
  m_apercu->feuille = &m_feuille;
  m_apercu->update();
}

bool MiseEnPlan::exporterPdf(const Feuille& f, const QString& chemin) {
  QPdfWriter pdf(chemin);
  const QPageSize::PageSizeId taille = f.format == "A0" ? QPageSize::A0 : f.format == "A1" ? QPageSize::A1 : f.format == "A2" ? QPageSize::A2 : f.format == "A3" ? QPageSize::A3 : QPageSize::A4;
  pdf.setPageSize(QPageSize(taille));
  pdf.setPageOrientation(f.largeur >= f.hauteur ? QPageLayout::Landscape : QPageLayout::Portrait);
  pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
  pdf.setResolution(600);
  pdf.setTitle(QString::fromStdString(f.titre));
  pdf.setCreator(QStringLiteral("Prométhée"));
  QPainter g(&pdf);
  if (!g.isActive()) return false;
  peindreFeuille(g, f, QRectF(0, 0, pdf.width(), pdf.height()), false);
  return g.end();
}

void MiseEnPlan::setAssemblage(std::vector<OptionsMiseEnPlan::LigneNomenclature> nomenclature, std::vector<OptionsMiseEnPlan::Bulle> bulles) {
  m_nomenclature = std::move(nomenclature);
  m_bulles = std::move(bulles);
  recalculer();
}

void MiseEnPlan::setFiletages(std::vector<OptionsMiseEnPlan::FiletageDessin> filetages) {
  m_filetages = std::move(filetages);
  recalculer();
}

void MiseEnPlan::modeCreation() {
  m_fermer->setText(QStringLiteral("Garder la mise en plan"));
  auto* abandon = new QPushButton(QStringLiteral("Abandonner"));
  connect(abandon, &QPushButton::clicked, this, &QDialog::reject);
  // Le bouton se place sous « Garder la mise en plan », dans la même colonne
  for (QBoxLayout* l : findChildren<QBoxLayout*>())
    if (l->indexOf(m_fermer) >= 0) { l->insertWidget(l->indexOf(m_fermer) + 1, abandon); return; }
}

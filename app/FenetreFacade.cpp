// Prométhée : façade du boîtier et cotes du contour (lot L6). Licence GPL-3.0-only.
#include "FenetreFacade.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "InspecteurElectronique.hpp"
#include "SelectionElectronique.hpp"
#include "promethee/contour_esquisse.hpp"
#include "promethee/fichiers.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
const char* nomGenre(ElementFacade::Genre g) {
  switch (g) {
    case ElementFacade::Genre::DecoupeRonde: return "Découpe ronde";
    case ElementFacade::Genre::DecoupeRect: return "Découpe rectangulaire";
    case ElementFacade::Genre::Legende: return "Légende";
    case ElementFacade::Genre::Echelle: return "Échelle graduée";
  }
  return "";
}
}  // namespace

// ---------- Aperçu ----------
ApercuFacade::ApercuFacade(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) { setMinimumSize(360, 260); }

void ApercuFacade::paintEvent(QPaintEvent*) {
  QPainter g(this);
  g.setRenderHint(QPainter::Antialiasing);
  g.fillRect(rect(), QColor(0xF4, 0xF5, 0xF8));
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  const Polygone face = c->circuit.contour.polygone();
  const Boite b = boite(face);
  if (b.vide()) return;
  const double e = std::min((width() - 20) / mm(b.largeur()), (height() - 20) / mm(b.hauteur()));
  auto P = [&](double x, double y) { return QPointF(10 + (x - mm(b.x0)) * e, 10 + (y - mm(b.y0)) * e); };
  QPolygonF contour;
  for (const Pt p : face) contour << P(mm(p.x), mm(p.y));
  g.setPen(QPen(QColor(0x30, 0x34, 0x40), 1.2));
  g.setBrush(QColor(0xE2, 0xE5, 0xEC));
  g.drawPolygon(contour);
  if (c->facades.empty()) return;
  for (const auto& x : c->facades.front().elements) {
    const PositionFacade p = positionElement(*c, x);
    switch (x.genre) {
      case ElementFacade::Genre::DecoupeRonde:
        g.setPen(QPen(QColor(0xC2, 0x40, 0x2E), 1.2));
        g.setBrush(Qt::white);
        g.drawEllipse(P(p.x, p.y), x.diametre / 2 * e, x.diametre / 2 * e);
        break;
      case ElementFacade::Genre::DecoupeRect:
        g.setPen(QPen(QColor(0xC2, 0x40, 0x2E), 1.2));
        g.setBrush(Qt::white);
        g.drawRect(QRectF(P(p.x - x.largeur / 2, p.y - x.hauteur / 2), P(p.x + x.largeur / 2, p.y + x.hauteur / 2)));
        break;
      case ElementFacade::Genre::Legende: {
        QFont f = font();
        f.setPixelSize(std::max(6, static_cast<int>(x.taille * e)));
        g.setFont(f);
        g.setPen(QColor(0x10, 0x14, 0x2A));
        g.drawText(QRectF(P(p.x - 40, p.y - x.taille), P(p.x + 40, p.y + x.taille)), Qt::AlignCenter, q(x.texte));
        break;
      }
      case ElementFacade::Genre::Echelle: {
        g.setPen(QPen(QColor(0x10, 0x14, 0x2A), 1));
        const int n = std::max(2, x.graduations);
        for (int i = 0; i < n; ++i) {
          const double a = (x.angleDebut + (x.angleFin - x.angleDebut) * i / (n - 1)) * std::numbers::pi / 180;
          const double l = x.longueurTrait * (i % 5 == 0 ? 1.5 : 1.0);
          g.drawLine(P(p.x + x.rayon * std::cos(a), p.y - x.rayon * std::sin(a)), P(p.x + (x.rayon + l) * std::cos(a), p.y - (x.rayon + l) * std::sin(a)));
        }
        break;
      }
    }
  }
  // Défauts du contrôle, cerclés
  g.setPen(QPen(QColor(0xE0, 0x2A, 0x1A), 2, Qt::DashLine));
  g.setBrush(Qt::NoBrush);
  for (const auto& d : m_defauts) g.drawEllipse(P(d.x, d.y), 12, 12);
}

// ---------- Façade ----------
FenetreFacade::FenetreFacade(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreFacade"));
  setWindowTitle(QStringLiteral("Façade du boîtier"));
  resize(900, 540);
  auto* col = new QVBoxLayout(this);
  auto* partage = new QSplitter(Qt::Horizontal);
  m_liste = new QTreeWidget;
  m_liste->setObjectName(QStringLiteral("elementsFacade"));
  m_liste->setHeaderLabels({QStringLiteral("Élément"), QStringLiteral("Composant"), QStringLiteral("Position")});
  partage->addWidget(m_liste);
  m_apercu = new ApercuFacade(a);
  partage->addWidget(m_apercu);
  partage->setStretchFactor(1, 2);
  col->addWidget(partage, 1);
  auto* ligne = new QHBoxLayout;
  auto* decoupe = new QPushButton(QStringLiteral("Pour la sélection"));
  decoupe->setToolTip(QStringLiteral("Découpe et légende des composants choisis au circuit ; elles les suivent"));
  auto* echelle = new QCheckBox(QStringLiteral("avec échelle graduée"));
  auto* supprimer = new QPushButton(QStringLiteral("Supprimer"));
  auto* controle = new QPushButton(QStringLiteral("Contrôler"));
  auto* sortie = new QPushButton(QStringLiteral("Exporter SVG et DXF…"));
  m_bilan = inspecteur::texte();
  for (QWidget* w : std::initializer_list<QWidget*>{decoupe, echelle, supprimer, controle, sortie}) ligne->addWidget(w);
  ligne->addStretch();
  ligne->addWidget(m_bilan);
  col->addLayout(ligne);
  connect(decoupe, &QPushButton::clicked, this, [this, echelle] { ajouterPourSelection(echelle->isChecked()); });
  connect(supprimer, &QPushButton::clicked, this, [this] {
    std::set<std::string> ids;
    for (QTreeWidgetItem* it : m_liste->selectedItems()) ids.insert(it->data(0, Qt::UserRole).toString().toStdString());
    if (ids.empty()) return;
    m_atelier.modifierConception([&](ConceptionElec& c) {
      if (!c.facades.empty()) std::erase_if(c.facades.front().elements, [&](const ElementFacade& e) { return ids.count(e.uuid) > 0; });
    }, QStringLiteral("Façade : éléments retirés"));
    refaire();
  });
  connect(controle, &QPushButton::clicked, this, [this] { controler(); });
  connect(sortie, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Façade"), QStringLiteral("facade.svg"), QStringLiteral("SVG (*.svg)"));
    if (f.isEmpty()) return;
    QString dxf = f;
    dxf.replace(QRegularExpression(QStringLiteral("\\.svg$")), QStringLiteral(".dxf"));
    QString raison;
    if (!exporter(f, dxf, &raison)) m_bilan->setText(raison);
  });
  refaire();
}

const Facade* FenetreFacade::facade() const {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  return c && !c->facades.empty() ? &c->facades.front() : nullptr;
}

void FenetreFacade::refaire() {
  m_liste->clear();
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (const Facade* f = facade(); c && f)
    for (const auto& e : f->elements) {
      const PositionFacade p = positionElement(*c, e);
      const ComposantElec* k = c->composant(e.composant);
      auto* it = new QTreeWidgetItem(m_liste, {QString::fromUtf8(nomGenre(e.genre)) + (e.texte.empty() ? QString() : QStringLiteral(" « %1 »").arg(q(e.texte))), k ? q(k->reference) : QString(),
                                               QStringLiteral("%1 ; %2 mm").arg(p.x, 0, 'f', 2).arg(p.y, 0, 'f', 2)});
      it->setData(0, Qt::UserRole, q(e.uuid));
    }
  m_apercu->update();
}

int FenetreFacade::ajouterPourSelection(bool echelle) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return 0;
  std::vector<std::string> ids(m_atelier.selection()->composants().begin(), m_atelier.selection()->composants().end());
  if (ids.empty()) {
    m_bilan->setText(QStringLiteral("Choisis d’abord les composants au circuit."));
    return 0;
  }
  int n = 0;
  m_atelier.modifierConception([&](ConceptionElec& cc) {
    if (cc.facades.empty()) {
      Facade f;
      f.uuid = nouvelUuid();
      cc.facades.push_back(f);
    }
    for (const auto& id : ids)
      for (const auto& e : elementsPour(cc, id, echelle)) {
        cc.facades.front().elements.push_back(e);
        ++n;
      }
  }, QStringLiteral("Façade : %1 élément(s)").arg(ids.size()));
  refaire();
  m_bilan->setText(QStringLiteral("%1 élément(s) posé(s)").arg(n));
  return n;
}

std::vector<DefautFacade> FenetreFacade::controler() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const Facade* f = facade();
  std::vector<DefautFacade> d = c && f ? controlerFacade(*c, *f) : std::vector<DefautFacade>{};
  m_apercu->setDefauts(d);
  m_bilan->setText(d.empty() ? QStringLiteral("<span style='color:%1'>Façade prête à sortir.</span>").arg(teintes::succes())
                             : QStringLiteral("<span style='color:%1'>%2 défaut(s) : %3</span>").arg(teintes::erreur()).arg(d.size()).arg(q(d.front().message).toHtmlEscaped()));
  return d;
}

bool FenetreFacade::exporter(const QString& svg, const QString& dxf, QString* raison) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const Facade* f = facade();
  if (!c || !f) {
    if (raison) *raison = QStringLiteral("La façade est vide.");
    return false;
  }
  const auto d = controler();
  if (!d.empty()) {
    if (raison) *raison = QStringLiteral("Sortie refusée : %1.").arg(q(d.front().message));
    return false;
  }
  try {
    prom::ecrireFichierAtomique(prom::cheminFichier(svg.toStdString()), svgFacade(*c, *f));
    prom::ecrireFichierAtomique(prom::cheminFichier(dxf.toStdString()), dxfFacade(*c, *f));
  } catch (const std::exception& e) {
    if (raison) *raison = QString::fromUtf8(e.what());
    return false;
  }
  m_bilan->setText(QStringLiteral("Façade écrite en SVG et en DXF."));
  return true;
}

// ---------- Cotes du contour ----------
DialogueContourEsquisse::DialogueContourEsquisse(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setObjectName(QStringLiteral("dialogueContourEsquisse"));
  setWindowTitle(QStringLiteral("Contour piloté par une esquisse"));
  setModal(false);
  auto* col = new QVBoxLayout(this);
  col->addWidget(inspecteur::texte(QStringLiteral("Les cotes de l’esquisse du contour : la carte, ses découpes et ses trous de fixation suivent.")));
  m_table = new QTableWidget(0, 2);
  m_table->setObjectName(QStringLiteral("cotesContour"));
  m_table->setHorizontalHeaderLabels({QStringLiteral("Cote"), QStringLiteral("Valeur")});
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->verticalHeader()->hide();
  col->addWidget(m_table, 1);
  m_bilan = inspecteur::texte();
  col->addWidget(m_bilan);
  resize(420, 360);
  refaire();
}

void DialogueContourEsquisse::refaire() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const auto cotes = c ? prom::cotesContour(*c) : std::vector<prom::CoteContour>{};
  m_table->setRowCount(static_cast<int>(cotes.size()));
  for (int i = 0; i < static_cast<int>(cotes.size()); ++i) {
    const auto& x = cotes[static_cast<std::size_t>(i)];
    auto* nom = new QTableWidgetItem(q(x.nom));
    nom->setFlags(nom->flags() & ~Qt::ItemIsEditable);
    m_table->setItem(i, 0, nom);
    auto* v = new QDoubleSpinBox;
    v->setObjectName(QStringLiteral("cote:%1").arg(x.contrainte));
    v->setRange(-10000, 10000);
    v->setDecimals(3);
    v->setSuffix(QStringLiteral(" mm"));
    v->setValue(x.valeur);
    v->setKeyboardTracking(false);
    const int id = x.contrainte;
    connect(v, &QDoubleSpinBox::valueChanged, this, [this, id](double val) { changerCote(id, val); });
    m_table->setCellWidget(i, 1, v);
  }
  m_bilan->setText(cotes.empty() ? QStringLiteral("La conception n’a pas d’esquisse de contour.") : QStringLiteral("%1 cote(s)").arg(cotes.size()));
}

bool DialogueContourEsquisse::changerCote(int contrainte, double valeur) {
  bool ok = false;
  std::vector<std::string> remarques;
  m_atelier.modifierConception([&](ConceptionElec& c) { ok = prom::changerCoteContour(c, contrainte, valeur, &remarques); }, QStringLiteral("Cote du contour"));
  QStringList r;
  for (const auto& x : remarques) r << q(x).toHtmlEscaped();
  m_bilan->setText(ok ? (r.isEmpty() ? QStringLiteral("La carte suit l’esquisse.") : r.join(QStringLiteral(" ; "))) : QStringLiteral("Cote introuvable."));
  return ok;
}

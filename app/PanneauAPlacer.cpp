// Prométhée : liste « À placer » (lot L5). Licence GPL-3.0-only.
#include "PanneauAPlacer.hpp"

#include <set>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "InspecteurElectronique.hpp"
#include "VueCircuit.hpp"
#include "promethee/placement_circuit.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
// L'arbre donne ses lignes choisies à glisser sur la carte
class ArbreAPlacer : public QTreeWidget {
 public:
  explicit ArbreAPlacer(PanneauAPlacer& p) : m_panneau(p) {
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
  }

 protected:
  QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
    std::vector<std::string> ids;
    std::set<std::string> vus;
    for (const QTreeWidgetItem* it : items)
      for (const auto& id : m_panneau.composantsDe(it))
        if (vus.insert(id).second) ids.push_back(id);
    return PanneauAPlacer::donnees(ids);
  }
  QStringList mimeTypes() const override { return {QStringLiteral("application/x-promethee-aplacer")}; }

 private:
  PanneauAPlacer& m_panneau;
};
}  // namespace

PanneauAPlacer::PanneauAPlacer(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  setObjectName(QStringLiteral("panneauAPlacer"));
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(4, 4, 4, 4);
  m_bilan = inspecteur::texte();
  m_bilan->setWordWrap(true);
  col->addWidget(m_bilan);
  m_arbre = new ArbreAPlacer(*this);
  m_arbre->setObjectName(QStringLiteral("arbreAPlacer"));
  m_arbre->setHeaderLabels({QStringLiteral("Composant"), QStringLiteral("Valeur"), QStringLiteral("Empreinte")});
  m_arbre->header()->setStretchLastSection(true);
  col->addWidget(m_arbre, 1);
  auto* ligne = new QHBoxLayout;
  m_poser = new QPushButton(QStringLiteral("Poser sur la carte"));
  m_poser->setObjectName(QStringLiteral("poserAPlacer"));
  m_poser->setToolTip(QStringLiteral("Les lignes choisies, en rangées serrées au premier coin libre de la carte. On peut aussi les glisser sur la carte."));
  ligne->addWidget(m_poser);
  ligne->addStretch();
  col->addLayout(ligne);
  connect(m_poser, &QPushButton::clicked, this, [this] { poserChoisis(); });
  refaire();
}

std::vector<std::string> PanneauAPlacer::composantsDe(const QTreeWidgetItem* it) const {
  std::vector<std::string> r;
  if (!it) return r;
  if (it->childCount() == 0) {
    if (const QString id = it->data(0, Qt::UserRole).toString(); !id.isEmpty()) r.push_back(id.toStdString());
    return r;
  }
  for (int i = 0; i < it->childCount(); ++i) r.push_back(it->child(i)->data(0, Qt::UserRole).toString().toStdString());
  return r;
}

QMimeData* PanneauAPlacer::donnees(const std::vector<std::string>& composants) {
  auto* m = new QMimeData;
  QString t;
  for (const auto& id : composants) t += q(id) + QLatin1Char('\n');
  m->setData(QStringLiteral("application/x-promethee-aplacer"), t.toUtf8());
  return m;
}

void PanneauAPlacer::refaire() {
  m_arbre->clear();
  m_nombre = 0;
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) {
    m_bilan->setText(QStringLiteral("Aucune conception ouverte."));
    return;
  }
  for (const auto& g : groupesAPlacer(*c)) {
    auto* groupe = new QTreeWidgetItem(m_arbre, {QStringLiteral("%1 (%2)").arg(q(g.nom)).arg(g.composants.size())});
    groupe->setFlags(groupe->flags() | Qt::ItemIsDragEnabled);
    for (const auto& id : g.composants)
      if (const ComposantElec* k = c->composant(id)) {
        auto* it = new QTreeWidgetItem(groupe, {q(k->reference), q(k->valeur), q(k->empreinte)});
        it->setData(0, Qt::UserRole, q(id));
        it->setFlags(it->flags() | Qt::ItemIsDragEnabled);
        ++m_nombre;
      }
    groupe->setExpanded(true);
  }
  m_bilan->setText(m_nombre == 0 ? QStringLiteral("Tous les composants sont sur la carte.")
                                 : QStringLiteral("<b>%1 composant(s) à placer</b> : glisse un groupe ou un composant sur la carte.").arg(m_nombre));
  m_poser->setEnabled(m_nombre > 0);
  m_arbre->resizeColumnToContents(0);
}

int PanneauAPlacer::poserChoisis() {
  std::vector<std::string> ids;
  std::set<std::string> vus;
  auto lignes = m_arbre->selectedItems();
  if (lignes.isEmpty())
    for (int i = 0; i < m_arbre->topLevelItemCount(); ++i) lignes << m_arbre->topLevelItem(i);
  for (const QTreeWidgetItem* it : lignes)
    for (const auto& id : composantsDe(it))
      if (vus.insert(id).second) ids.push_back(id);
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c || ids.empty()) return 0;
  const Boite carte = boite(c->circuit.contour.polygone());
  const Nm marge = std::max<Nm>(c->circuit.regles.bord, 0) + 2 * NM_PAR_MM;
  const int n = m_atelier.vueCircuit()->poserGroupe(ids, {carte.x0 + marge, carte.y0 + marge});
  refaire();
  return n;
}

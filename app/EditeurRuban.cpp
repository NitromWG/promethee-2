// Prométhée : éditeur du ruban. Voir EditeurRuban.hpp. Licence GPL-3.0-only.
#include "EditeurRuban.hpp"

#include <algorithm>

#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "Icones.hpp"
#include "RegistreActions.hpp"

namespace {
QString nomForme(DescriptionRuban::Forme f) {
  switch (f) {
    case DescriptionRuban::Forme::Grille: return QStringLiteral("petits boutons");
    case DescriptionRuban::Forme::Controle: return QStringLiteral("réglage");
    case DescriptionRuban::Forme::Separateur: return QStringLiteral("séparateur");
    case DescriptionRuban::Forme::Grand: break;
  }
  return QStringLiteral("gros bouton");
}
}  // namespace

// ---------- Arbre ----------
ArbreRuban::ArbreRuban(QWidget* parent) : QTreeWidget(parent) {
  setObjectName(QStringLiteral("arbreRuban"));
  setColumnCount(2);
  setHeaderLabels({QStringLiteral("Ruban"), QStringLiteral("Forme")});
  header()->setSectionResizeMode(0, QHeaderView::Stretch);
  header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  setDragDropMode(QAbstractItemView::DragDrop);
  setDefaultDropAction(Qt::MoveAction);
  setDragEnabled(true);
  setAcceptDrops(true);
  setDropIndicatorShown(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int colonne) {
    if (colonne == 0 && niveau(it) == 1) emit modifie();  // titre d'un groupe renommé
  });
}

int ArbreRuban::niveau(const QTreeWidgetItem* it) {
  int n = 0;
  for (const QTreeWidgetItem* p = it ? it->parent() : nullptr; p; p = p->parent()) ++n;
  return it ? n : -1;
}

QTreeWidgetItem* ArbreRuban::nouvelleAction(const QString& id) {
  const ActionAtelier* a = RegistreActions::atelierElectronique().action(id.toStdString());
  auto* it = new QTreeWidgetItem({a ? a->nomAffiche() : id});
  it->setData(0, ROLE_ID, id);
  it->setToolTip(0, id);
  if (a && !a->icone.isEmpty()) it->setIcon(0, icone(a->icone));
  it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
  return it;
}

void ArbreRuban::remplir(const DescriptionRuban& d) {
  const QSignalBlocker bloque(this);
  clear();
  for (const auto& o : d.onglets) {
    auto* onglet = new QTreeWidgetItem(this, {o.titre});
    onglet->setData(0, ROLE_ID, QString::fromStdString(o.id));
    onglet->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
    for (const auto& g : o.groupes) {
      auto* groupe = new QTreeWidgetItem(onglet, {g.forme == DescriptionRuban::Forme::Separateur ? QStringLiteral("—") : g.titre, nomForme(g.forme)});
      groupe->setData(0, ROLE_FORME, static_cast<int>(g.forme));
      groupe->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled |
                       (g.forme == DescriptionRuban::Forme::Separateur ? Qt::NoItemFlags : Qt::ItemIsEditable));
      for (const std::string& a : g.actions) groupe->addChild(nouvelleAction(QString::fromStdString(a)));
    }
  }
  expandAll();
}

DescriptionRuban ArbreRuban::description() const {
  DescriptionRuban d;
  for (int i = 0; i < topLevelItemCount(); ++i) {
    const QTreeWidgetItem* o = topLevelItem(i);
    DescriptionRuban::Onglet onglet;
    onglet.id = o->data(0, ROLE_ID).toString().toStdString();
    onglet.titre = o->text(0);
    for (int j = 0; j < o->childCount(); ++j) {
      const QTreeWidgetItem* g = o->child(j);
      DescriptionRuban::Groupe groupe;
      groupe.forme = static_cast<DescriptionRuban::Forme>(g->data(0, ROLE_FORME).toInt());
      if (groupe.forme != DescriptionRuban::Forme::Separateur) groupe.titre = g->text(0);
      for (int k = 0; k < g->childCount(); ++k) groupe.actions.push_back(g->child(k)->data(0, ROLE_ID).toString().toStdString());
      onglet.groupes.push_back(std::move(groupe));
    }
    d.onglets.push_back(std::move(onglet));
  }
  return d;
}

QTreeWidgetItem* ArbreRuban::groupe(const QString& onglet, const QString& titre) const {
  for (int i = 0; i < topLevelItemCount(); ++i)
    if (topLevelItem(i)->text(0) == onglet)
      for (int j = 0; j < topLevelItem(i)->childCount(); ++j)
        if (topLevelItem(i)->child(j)->text(0) == titre) return topLevelItem(i)->child(j);
  return nullptr;
}

bool ArbreRuban::deposer(const QString& action, QTreeWidgetItem* groupe, int indice) {
  if (!groupe || niveau(groupe) != 1 || !RegistreActions::atelierElectronique().action(action.toStdString())) return false;
  const auto forme = static_cast<DescriptionRuban::Forme>(groupe->data(0, ROLE_FORME).toInt());
  const bool controle = RegistreActions::atelierElectronique().action(action.toStdString())->controle;
  // Un réglage va dans un groupe de réglages, un bouton dans un groupe de boutons ; rien dans un séparateur
  if (forme == DescriptionRuban::Forme::Separateur || controle != (forme == DescriptionRuban::Forme::Controle)) return false;
  groupe->insertChild(indice < 0 || indice > groupe->childCount() ? groupe->childCount() : indice, nouvelleAction(action));
  emit modifie();
  return true;
}

bool ArbreRuban::deplacer(QTreeWidgetItem* element, QTreeWidgetItem* parent, int indice) {
  if (!element || !parent || niveau(parent) != niveau(element) - 1 || niveau(element) < 1) return false;
  QTreeWidgetItem* ancien = element->parent();
  // Un groupe reste dans son onglet : les onglets sont les modes de l'atelier
  if (niveau(element) == 1 && parent != ancien) return false;
  if (niveau(element) == 2) {
    const auto forme = static_cast<DescriptionRuban::Forme>(parent->data(0, ROLE_FORME).toInt());
    const bool controle = RegistreActions::atelierElectronique().action(element->data(0, ROLE_ID).toString().toStdString())->controle;
    if (forme == DescriptionRuban::Forme::Separateur || controle != (forme == DescriptionRuban::Forme::Controle)) return false;
  }
  const int avant = ancien->indexOfChild(element);
  ancien->takeChild(avant);
  if (ancien == parent && indice > avant) --indice;
  parent->insertChild(std::clamp(indice < 0 ? parent->childCount() : indice, 0, parent->childCount()), element);
  setCurrentItem(element);
  emit modifie();
  return true;
}

bool ArbreRuban::cible(QPoint p, int niveauDepose, QTreeWidgetItem*& parent, int& indice) const {
  QTreeWidgetItem* sous = itemAt(p);
  if (!sous) return false;
  const QRect r = visualItemRect(sous);
  const bool dessous = p.y() > r.center().y();
  const int n = niveau(sous);
  if (n == niveauDepose - 1) {  // sur le parent : à la fin
    parent = sous;
    indice = -1;
    return true;
  }
  if (n == niveauDepose) {  // sur un frère : avant ou après lui
    parent = sous->parent();
    indice = parent->indexOfChild(sous) + (dessous ? 1 : 0);
    return true;
  }
  if (n == niveauDepose + 1) {  // sur un enfant d'un frère : après ce frère
    parent = sous->parent()->parent();
    indice = parent ? parent->indexOfChild(sous->parent()) + 1 : 0;
    return parent != nullptr;
  }
  return false;
}

void ArbreRuban::dragMoveEvent(QDragMoveEvent* e) {
  QTreeWidget::dragMoveEvent(e);
  QTreeWidgetItem* parent = nullptr;
  int indice = -1;
  const int n = e->source() == this && currentItem() ? niveau(currentItem()) : 2;
  if (cible(e->position().toPoint(), n, parent, indice)) e->acceptProposedAction();
  else e->ignore();
}

void ArbreRuban::dropEvent(QDropEvent* e) {
  QTreeWidgetItem* parent = nullptr;
  int indice = -1;
  bool fait = false;
  if (e->source() == this) {
    // Un élément de l'arbre déplacé
    if (QTreeWidgetItem* element = currentItem(); element && cible(e->position().toPoint(), niveau(element), parent, indice)) fait = deplacer(element, parent, indice);
  } else if (auto* liste = qobject_cast<QListWidget*>(e->source())) {
    // Une action de la liste du registre, ajoutée au groupe visé
    if (QListWidgetItem* it = liste->currentItem(); it && cible(e->position().toPoint(), 2, parent, indice)) fait = deposer(it->data(Qt::UserRole).toString(), parent, indice);
  }
  if (fait) {
    e->setDropAction(Qt::CopyAction);  // l'arbre a déjà placé l'élément : rien d'autre à retirer
    e->accept();
  } else {
    e->ignore();
  }
}

// ---------- Page ----------
EditeurRuban::EditeurRuban(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("page:ruban"));
  auto* col = new QVBoxLayout(this);
  col->addWidget(new QLabel(QStringLiteral("<b>Ruban de l’atelier électronique</b> · glisse une action de la liste dans un groupe, ou une entrée à une autre place")));
  auto* corps = new QHBoxLayout;
  m_arbre = new ArbreRuban;
  corps->addWidget(m_arbre, 3);
  m_disponibles = new QListWidget;
  m_disponibles->setObjectName(QStringLiteral("actionsDisponibles"));
  m_disponibles->setDragEnabled(true);
  m_disponibles->setDragDropMode(QAbstractItemView::DragOnly);
  for (const ActionAtelier& a : RegistreActions::atelierElectronique().actions()) {
    if (a.cachee || a.portee == PorteeAction::Partout) continue;
    auto* it = new QListWidgetItem(a.icone.isEmpty() ? QIcon() : icone(a.icone), a.nomAffiche());
    it->setData(Qt::UserRole, QString::fromStdString(a.identifiant));
    it->setToolTip(QString::fromStdString(a.identifiant));
    m_disponibles->addItem(it);
  }
  corps->addWidget(m_disponibles, 2);
  col->addLayout(corps, 1);
  auto* boutons = new QHBoxLayout;
  auto* retirer = new QPushButton(QStringLiteral("Retirer"));
  retirer->setObjectName(QStringLiteral("retirerDuRuban"));
  auto* groupe = new QPushButton(QStringLiteral("Nouveau groupe"));
  auto* original = new QPushButton(QStringLiteral("Rétablir l’original"));
  original->setObjectName(QStringLiteral("rubanOriginal"));
  boutons->addWidget(retirer);
  boutons->addWidget(groupe);
  boutons->addStretch(1);
  boutons->addWidget(original);
  col->addLayout(boutons);
  m_arbre->remplir(RubanEnCours::instance().description());
  connect(m_arbre, &ArbreRuban::modifie, this, [this] { enregistrer(); });
  connect(retirer, &QPushButton::clicked, this, [this] {
    QTreeWidgetItem* it = m_arbre->currentItem();
    if (!it || !it->parent()) return;  // un onglet ne se retire pas
    delete it;
    enregistrer();
  });
  connect(groupe, &QPushButton::clicked, this, [this] {
    QTreeWidgetItem* it = m_arbre->currentItem();
    while (it && it->parent()) it = it->parent();
    if (!it) it = m_arbre->topLevelItem(0);
    if (!it) return;
    auto* g = new QTreeWidgetItem(it, {QStringLiteral("Groupe"), QStringLiteral("petits boutons")});
    g->setData(0, ArbreRuban::ROLE_FORME, static_cast<int>(DescriptionRuban::Forme::Grille));
    g->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsEditable);
    m_arbre->setCurrentItem(g);
    enregistrer();
  });
  connect(original, &QPushButton::clicked, this, [] { RubanEnCours::instance().retablir(); });
  // Rétabli ailleurs (script, autre fenêtre de réglages) : l'arbre suit
  connect(&RubanEnCours::instance(), &RubanEnCours::change, this, [this] {
    if (!m_remplissage && m_arbre->description() != RubanEnCours::instance().description()) m_arbre->remplir(RubanEnCours::instance().description());
  });
}

void EditeurRuban::enregistrer() {
  m_remplissage = true;
  RubanEnCours::instance().remplacer(m_arbre->description());
  m_remplissage = false;
}

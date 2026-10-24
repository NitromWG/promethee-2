// Prométhée : navigateur de réseaux (lot L5, tâches 5.15 et 5.32). Licence GPL-3.0-only.
#include "PanneauReseaux.hpp"

#include <cctype>
#include <map>
#include <set>

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QLocale>
#include <QRegularExpression>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "VueCircuit.hpp"
#include "promethee/circuit_edition.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string motifDe(const std::string& nom) {
  // Motif : le nom sans ses chiffres de fin ni son suffixe de paire (« D3 » → « D », « USB_DP » → « USB_D »)
  std::string m = nom;
  while (!m.empty() && (std::isdigit(static_cast<unsigned char>(m.back())) || m.back() == '+' || m.back() == '-')) m.pop_back();
  if (m.size() > 1 && (m.back() == 'P' || m.back() == 'N' || m.back() == 'M') && m[m.size() - 2] == 'D') m.pop_back();
  return m.empty() ? nom : m;
}
}  // namespace

PanneauReseaux::PanneauReseaux(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  setObjectName(QStringLiteral("panneauReseaux"));
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(4, 4, 4, 4);
  auto* h = new QHBoxLayout;
  m_recherche = new QLineEdit;
  m_recherche->setObjectName(QStringLiteral("rechercheReseaux"));
  m_recherche->setPlaceholderText(QStringLiteral("Chercher un réseau (CLK, USB_*)…"));
  m_recherche->setClearButtonEnabled(true);
  h->addWidget(m_recherche, 1);
  m_groupement = new QComboBox;
  m_groupement->setObjectName(QStringLiteral("groupementReseaux"));
  m_groupement->addItems({QStringLiteral("Par classe"), QStringLiteral("Par motif"), QStringLiteral("Sans groupe")});
  h->addWidget(m_groupement);
  v->addLayout(h);
  m_arbre = new QTreeWidget;
  m_arbre->setObjectName(QStringLiteral("arbreReseaux"));
  m_arbre->setHeaderLabels({QStringLiteral("Réseau"), QStringLiteral("Broches"), QStringLiteral("Routé")});
  m_arbre->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_arbre->setRootIsDecorated(true);
  v->addWidget(m_arbre, 1);
  auto* b = new QHBoxLayout;
  auto* boutonRenommer = new QPushButton(QStringLiteral("Renommer…"));
  boutonRenommer->setObjectName(QStringLiteral("renommerReseau"));
  b->addWidget(boutonRenommer);
  auto* nouveau = new QPushButton(QStringLiteral("Nouveau réseau…"));
  nouveau->setObjectName(QStringLiteral("nouveauReseau"));
  b->addWidget(nouveau);
  b->addStretch();
  v->addLayout(b);
  m_bilan = new QLabel;
  m_bilan->setStyleSheet(QStringLiteral("color: #4A5270;"));
  v->addWidget(m_bilan);
  connect(m_recherche, &QLineEdit::textChanged, this, [this] { refaire(); });
  connect(m_groupement, &QComboBox::currentIndexChanged, this, [this] { refaire(); });
  connect(m_arbre, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) { choisir(it); });
  connect(m_arbre, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) {
    choisir(it);
    const std::string r = it->data(0, Qt::UserRole).toString().toStdString();
    if (r.empty() || !m_atelier.vueCircuit()) return;
    // Cadré : le premier point de son cuivre ou de ses pastilles
    for (const auto& p : m_atelier.calculs().pastilles)
      if (p.reseau == r) {
        m_atelier.vueCircuit()->centrerSur(p.pastille.position);
        return;
      }
  });
  connect(boutonRenommer, &QPushButton::clicked, this, [this] { renommer(); });
  connect(nouveau, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const QString nom = QInputDialog::getText(this, QStringLiteral("Nouveau réseau"), QStringLiteral("Nom du réseau (il se donne ensuite par le pinceau de réseau) :"), QLineEdit::Normal, {}, &ok);
    if (!ok || nom.trimmed().isEmpty()) return;
    m_atelier.afficherMessage(QStringLiteral("Réseau « %1 » prêt : choisis du cuivre puis « Réseau… » (pinceau) pour le lui donner.").arg(nom.trimmed().toHtmlEscaped()));
  });
}

QTreeWidgetItem* PanneauReseaux::ligne(const std::string& reseau) const {
  for (QTreeWidgetItemIterator it(m_arbre); *it; ++it)
    if ((*it)->data(0, Qt::UserRole).toString().toStdString() == reseau) return *it;
  return nullptr;
}

void PanneauReseaux::choisir(QTreeWidgetItem* it) {
  if (!it) return;
  const QString r = it->data(0, Qt::UserRole).toString();
  if (!r.isEmpty()) m_atelier.surligner(r);
}

void PanneauReseaux::renommer() {
  QTreeWidgetItem* it = m_arbre->currentItem();
  const std::string ancien = it ? it->data(0, Qt::UserRole).toString().toStdString() : std::string();
  if (ancien.empty()) {
    m_atelier.afficherMessage(QStringLiteral("Choisis d’abord le réseau à renommer."));
    return;
  }
  bool ok = false;
  const QString neuf = QInputDialog::getText(this, QStringLiteral("Renommer le réseau"), QStringLiteral("Nouveau nom :"), QLineEdit::Normal, q(ancien), &ok).trimmed();
  if (!ok || neuf.isEmpty() || neuf.toStdString() == ancien) return;
  const std::string n = neuf.toStdString();
  int touches = 0;
  m_atelier.modifierConception(
      [&](ConceptionElec& c) {
        // Réseau de la carte seule : ses broches, puis son cuivre ; un réseau du schéma garde le nom de ses étiquettes,
        // son cuivre seul ne se renomme pas (il redeviendrait étranger à ses pastilles)
        for (auto& [cle, r] : c.nomsReseaux)
          if (r == ancien) {
            const std::string comp = cle.substr(0, cle.find(':'));
            if (const ComposantElec* x = c.composant(comp); x && x->carteSeule) r = n, ++touches;
          }
        if (!touches) return;
        for (auto& p : c.circuit.pistes)
          if (p.reseau == ancien) p.reseau = n;
        for (auto& v : c.circuit.vias)
          if (v.reseau == ancien) v.reseau = n;
        for (auto& z : c.circuit.zones)
          if (z.reseau == ancien) z.reseau = n;
      },
      QStringLiteral("Renommer un réseau"));
  if (!touches)
    m_atelier.afficherMessage(QStringLiteral("« %1 » vient du schéma : renomme son étiquette, le cuivre suivra.").arg(q(ancien).toHtmlEscaped()));
}

void PanneauReseaux::refaire() {
  const QString filtre = m_recherche->text().trimmed();
  QRegularExpression motif;
  if (!filtre.isEmpty()) motif = QRegularExpression(QRegularExpression::wildcardToRegularExpression(filtre.contains('*') || filtre.contains('?') ? filtre : QStringLiteral("*") + filtre + QStringLiteral("*")), QRegularExpression::CaseInsensitiveOption);
  const std::string garde = m_arbre->currentItem() ? m_arbre->currentItem()->data(0, Qt::UserRole).toString().toStdString() : std::string();
  m_arbre->clear();
  m_nombre = 0;
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  const auto& calc = m_atelier.calculs();
  // Réseaux : ceux du schéma, puis ceux de la carte seule ; broches comptées sur la carte
  std::map<std::string, int> broches;
  for (const auto& p : calc.pastilles)
    if (!p.reseau.empty()) ++broches[p.reseau];
  std::set<std::string> noms;
  for (const auto& r : calc.k.reseaux)
    if (!r.nom.empty()) noms.insert(r.nom);
  for (const auto& [n, _] : broches) noms.insert(n);
  std::map<std::string, double> longueurs;
  for (const auto& p : c->circuit.pistes) longueurs[calc.k.canonique(p.reseau)] += longueurPiste(p);
  std::map<QString, QTreeWidgetItem*> groupes;
  const int mode = m_groupement->currentIndex();
  for (const auto& n : noms) {
    if (!filtre.isEmpty() && !motif.match(q(n)).hasMatch()) continue;
    QTreeWidgetItem* parent = nullptr;
    if (mode != 2) {
      QString g;
      if (mode == 0) {
        const ClasseReseau* k = classeDe(c->circuit.regles, n);
        g = k ? q(k->nom) : QStringLiteral("Défaut");
      } else
        g = q(motifDe(n));
      auto [it, neuf] = groupes.try_emplace(g, nullptr);
      if (neuf) {
        it->second = new QTreeWidgetItem(m_arbre, {g});
        it->second->setFirstColumnSpanned(false);
        QFont f = it->second->font(0);
        f.setBold(true);
        it->second->setFont(0, f);
      }
      parent = it->second;
    }
    auto* l = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_arbre);
    l->setText(0, q(n));
    l->setData(0, Qt::UserRole, q(n));
    l->setText(1, QString::number(broches[n]));
    l->setText(2, longueurs[n] > 0 ? QLocale().toString(longueurs[n] * 1e-6, 'f', 1) + QStringLiteral(" mm") : QStringLiteral("—"));
    if (n == garde) m_arbre->setCurrentItem(l);
    ++m_nombre;
  }
  m_arbre->expandAll();
  m_bilan->setText(QStringLiteral("%1 réseau(x)").arg(m_nombre));
}

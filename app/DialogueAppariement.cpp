// Prométhée : liens perdus entre un schéma et une carte (lot L3, tâche 3.19). Voir DialogueAppariement.hpp.
// Licence GPL-3.0-only.
#include "DialogueAppariement.hpp"

#include <algorithm>

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "InspecteurElectronique.hpp"
#include "PanneauEcarts.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QString nomPar(Appariement::Par p) {
  switch (p) {
    case Appariement::Par::Repere: return QStringLiteral("repère");
    case Appariement::Par::ValeurEmpreinte: return QStringLiteral("valeur et empreinte");
    case Appariement::Par::CoutMinimal: return QStringLiteral("coût minimal");
    case Appariement::Par::Main: return QStringLiteral("à la main");
  }
  return {};
}
}  // namespace

void TableauPaires::mousePressEvent(QMouseEvent* e) {
  m_depart = rowAt(static_cast<int>(e->position().y()));
  QTableWidget::mousePressEvent(e);
}

void TableauPaires::mouseReleaseEvent(QMouseEvent* e) {
  const int arrivee = rowAt(static_cast<int>(e->position().y()));
  if (m_depart >= 0 && arrivee >= 0 && arrivee != m_depart) emit echanger(m_depart, arrivee);
  m_depart = -1;
  QTableWidget::mouseReleaseEvent(e);
}

DialogueAppariement::DialogueAppariement(AtelierElectronique& a, ConceptionElec source, QWidget* parent) : QDialog(parent), m_atelier(a), m_source(std::move(source)) {
  setObjectName(QStringLiteral("dialogueAppariement"));
  setWindowTitle(QStringLiteral("Relier la carte « %1 » au schéma").arg(q(m_source.nom)));
  auto* col = new QVBoxLayout(this);
  col->addWidget(inspecteur::texte(QStringLiteral("Chaque composant du schéma, à gauche, et l’empreinte de la carte qui lui revient, à droite. "
                                                  "Glisse une ligne sur une autre pour échanger leurs empreintes.")));
  m_table = new TableauPaires(0, 3);
  m_table->setObjectName(QStringLiteral("tableauAppariement"));
  m_table->setHorizontalHeaderLabels({QStringLiteral("Schéma"), QStringLiteral("Carte"), QStringLiteral("Apparié par")});
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_table->verticalHeader()->hide();
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  connect(m_table, &TableauPaires::echanger, this, &DialogueAppariement::echanger);
  col->addWidget(m_table, 1);
  m_bilan = inspecteur::texte();
  m_bilan->setObjectName(QStringLiteral("bilanAppariement"));
  col->addWidget(m_bilan);
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  boutons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Appliquer"));
  boutons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("appliquerAppariement"));
  boutons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annuler"));
  connect(boutons->button(QDialogButtonBox::Ok), &QPushButton::clicked, this, [this] {
    if (appliquer() > 0) accept();
  });
  connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  col->addWidget(boutons);
  resize(760, 560);
  // Appariement automatique : repère, valeur et empreinte, coût minimal
  const ConceptionElec* c = a.conceptionCourante();
  m_carte = empreintesSansLien(m_source);
  if (c) {
    const ResultatAppariement r = apparier(*c, m_carte);
    std::vector<const ComposantElec*> tries;
    for (const auto& x : c->composants) tries.push_back(&x);
    std::sort(tries.begin(), tries.end(), [](const auto* x, const auto* y) { return ordreNaturel(x->reference, y->reference); });
    for (const auto* x : tries) {
      if (x->horsCarte) continue;
      m_composants.push_back(x->uuid);
      int e = -1;
      Appariement::Par par = Appariement::Par::Main;
      for (const auto& p : r.paires)
        if (p.composant == x->uuid) {
          e = static_cast<int>(p.empreinte);
          par = p.par;
        }
      m_empreinteDeLigne.push_back(e);
      m_par.push_back(par);
    }
    for (std::size_t i : r.empreintesSeules) {
      m_composants.push_back({});
      m_empreinteDeLigne.push_back(static_cast<int>(i));
      m_par.push_back(Appariement::Par::Main);
    }
  }
  remplir();
}

void DialogueAppariement::remplir() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  m_table->setRowCount(static_cast<int>(m_composants.size()));
  int apparies = 0, parRepere = 0;
  m_paires.clear();
  for (int l = 0; l < static_cast<int>(m_composants.size()); ++l) {
    const auto i = static_cast<std::size_t>(l);
    const ComposantElec* x = c && !m_composants[i].empty() ? c->composant(m_composants[i]) : nullptr;
    m_table->setItem(l, 0, new QTableWidgetItem(x ? q(x->reference + " · " + x->valeur + " · " + x->empreinte) : QStringLiteral("—")));
    const int e = m_empreinteDeLigne[i];
    const EmpreinteSansLien* em = e >= 0 && e < static_cast<int>(m_carte.size()) ? &m_carte[static_cast<std::size_t>(e)] : nullptr;
    m_table->setItem(l, 1, new QTableWidgetItem(em ? q(em->reference + " · " + em->valeur + " · " + em->empreinte) : QStringLiteral("—")));
    m_table->setItem(l, 2, new QTableWidgetItem(x && em ? nomPar(m_par[i]) : QString()));
    if (x && em) {
      ++apparies;
      parRepere += m_par[i] == Appariement::Par::Repere;
      m_paires.push_back({static_cast<std::size_t>(e), x->uuid, m_par[i], 0});
    }
  }
  m_bilan->setText(QStringLiteral("%1 composants appariés sur %2, dont %3 par leur repère.").arg(apparies).arg(m_carte.size()).arg(parRepere));
}

void DialogueAppariement::echanger(int a, int b) {
  if (a < 0 || b < 0 || a >= static_cast<int>(m_empreinteDeLigne.size()) || b >= static_cast<int>(m_empreinteDeLigne.size())) return;
  std::swap(m_empreinteDeLigne[static_cast<std::size_t>(a)], m_empreinteDeLigne[static_cast<std::size_t>(b)]);
  m_par[static_cast<std::size_t>(a)] = m_par[static_cast<std::size_t>(b)] = Appariement::Par::Main;
  remplir();
}

int DialogueAppariement::appliquer() {
  int n = 0;
  const std::vector<Appariement> paires = m_paires;
  m_atelier.modifierConception([&](ConceptionElec& c) { n = reporterCarte(c, m_source, paires); },
                               QStringLiteral("Carte « %1 » reliée au schéma (%2 empreintes)").arg(q(m_source.nom)).arg(paires.size()));
  if (n > 0) {
    m_atelier.afficherMessage(QStringLiteral("<b>Carte reliée</b> : %1 empreinte(s) reportée(s) sur le schéma, en un pas d’annulation. Les écarts qui restent sont au panneau des écarts.").arg(n));
    m_atelier.montrerEcarts();
  }
  return n;
}

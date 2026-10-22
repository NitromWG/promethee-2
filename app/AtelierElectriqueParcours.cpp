// Prométhée : atelier électrique — apprendre. La carte du parcours en tête du panneau de droite : l'étape, ce
// qu'on y apprend, où cliquer, la fiche du glossaire, et son état (faite ou à faire). Une étape faite pendant
// qu'elle est montrée passe d'elle-même à la suivante ; l'onglet de l'étape s'ouvre avec elle. Licence GPL-3.0-only.
#include <algorithm>

#include <QCursor>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include "AtelierElectrique.hpp"
#include "EtiquetteEtat.hpp"
#include "Icones.hpp"
#include "promethee/electrique_glossaire.hpp"
#include "promethee/electrique_parcours.hpp"

using namespace prom::elec;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }
QString h(const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); }

}  // namespace

void AtelierElectrique::demarrerParcours(const std::string& id) {
  const Parcours* m = parcoursElectrique(id);
  if (!m) return;
  m_parcours = id;
  allerEtapeParcours(0);
  m_etat->setText(QStringLiteral("Parcours « %1 » : %2 étapes. Suis la carte en haut à droite ; chaque étape se valide toute seule.").arg(h(m->nom)).arg(m->etapes.size()));
}

void AtelierElectrique::quitterParcours() {
  m_parcours.clear();
  m_etapeParcours = 0;
  majPanneau();
}

void AtelierElectrique::allerEtapeParcours(int etape) {
  const Parcours* m = parcoursElectrique(m_parcours);
  if (!m) return;
  const int n = static_cast<int>(m->etapes.size());
  m_etapeParcours = std::clamp(etape, 0, n);
  m_etapeDejaFaite = false;
  if (m_etapeParcours < n) {
    const EtapeParcours& e = m->etapes[static_cast<std::size_t>(m_etapeParcours)];
    const ProjetElectrique* pe = projetCourant();
    m_etapeDejaFaite = e.faite && pe && e.faite(*pe);
    if (e.mode != mode()) choisirMode(e.mode);
  }
  majPanneau();
}

void AtelierElectrique::suivreParcours() {
  const Parcours* m = parcoursElectrique(m_parcours);
  const ProjetElectrique* pe = projetCourant();
  if (!m || !pe || m_etapeParcours >= static_cast<int>(m->etapes.size())) return;
  const EtapeParcours& e = m->etapes[static_cast<std::size_t>(m_etapeParcours)];
  if (!e.faite || m_etapeDejaFaite || !e.faite(*pe)) return;
  const int suivante = m_etapeParcours + 1;
  m_etat->setText(suivante < static_cast<int>(m->etapes.size())
                      ? QStringLiteral("✓ <b>%1</b> : c’est fait. Étape suivante : <b>%2</b>.").arg(h(e.titre), h(m->etapes[static_cast<std::size_t>(suivante)].titre))
                      : QStringLiteral("✓ <b>%1</b> : c’est fait. Parcours terminé, bravo !").arg(h(e.titre)));
  allerEtapeParcours(suivante);
}

void AtelierElectrique::carteParcours(QVBoxLayout* col) {
  const Parcours* m = parcoursElectrique(m_parcours);
  if (!m || m->etapes.empty()) return;
  const int n = static_cast<int>(m->etapes.size()), i = std::clamp(m_etapeParcours, 0, n);
  const ProjetElectrique* pe = projetCourant();
  auto* g = new QGroupBox(QStringLiteral("Apprendre"));
  g->setToolTip(q(m->nom));
  g->setObjectName(QStringLiteral("carteParcours"));
  g->setStyleSheet(QStringLiteral("QGroupBox#carteParcours { border: 1px solid #E0A040; border-radius: 9px; margin-top: 14px; padding-top: 6px; }"
                                  "QGroupBox#carteParcours::title { color: #C27A12; font-weight: 700; }"));
  auto* v = new QVBoxLayout(g);
  v->setSpacing(6);
  auto* barre = new QProgressBar;
  barre->setRange(0, n);
  const bool finie = i >= n;
  const EtapeParcours* e = finie ? nullptr : &m->etapes[static_cast<std::size_t>(i)];
  const bool faite = e && e->faite && pe && e->faite(*pe);
  barre->setValue(i + (faite ? 1 : 0));
  barre->setTextVisible(false);
  barre->setFixedHeight(6);
  auto* nom = new QLabel(QStringLiteral("<span style='color:#C27A12; font-weight:600'>%1</span>").arg(h(m->nom)));
  nom->setWordWrap(true);
  v->addWidget(nom);
  v->addWidget(barre);
  auto lienGlossaire = [this](QLabel* l) {
    l->setTextFormat(Qt::RichText);
    l->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(l, &QLabel::linkActivated, this, [this](const QString& lien) {
      if (const TermeGlossaire* t = termeGlossaire(lien.mid(5).toStdString())) {
        QToolTip::showText(QCursor::pos(), QStringLiteral("<b>%1</b><br>%2<br><i>%3</i>").arg(h(t->terme), h(t->definition), h(t->exemple)));
        m_etat->setText(QStringLiteral("<b>%1</b> : %2").arg(h(t->terme), h(t->definition)));
      }
    });
  };
  auto* ligne = new QHBoxLayout;
  ligne->setSpacing(4);
  if (finie) {
    auto* fin = new QLabel(QStringLiteral("<span style='font-size:14px; font-weight:700; color:#2E7D32'>✓ Parcours terminé</span><br>%1").arg(h(m->description)));
    fin->setObjectName(QStringLiteral("titreEtape"));
    fin->setWordWrap(true);
    v->addWidget(fin);
  } else {
    auto* titre = new QLabel(QStringLiteral("<span style='color:#6B7393'>Étape %1 sur %2</span><br><span style='font-size:14px; font-weight:700'>%3</span>").arg(i + 1).arg(n).arg(h(e->titre)));
    titre->setObjectName(QStringLiteral("titreEtape"));
    titre->setWordWrap(true);
    v->addWidget(titre);
    auto* texte = new QLabel(QStringLiteral("%1%2<p style='margin-top:6px'><b>Comment :</b> %3</p>")
                                 .arg(h(e->pourquoi),
                                      e->glossaire.empty() ? QString()
                                                           : QStringLiteral("<a href='aide:%1' style='text-decoration:none; color:#5B4BF5'>&nbsp;ⓘ</a>").arg(q(e->glossaire)),
                                      h(e->comment)));
    texte->setObjectName(QStringLiteral("texteEtape"));
    texte->setWordWrap(true);
    lienGlossaire(texte);
    v->addWidget(texte);
    auto* etat = new QLabel(!e->faite ? QStringLiteral("<span style='color:#6B7393'>Lis, essaie, puis « Compris ».</span>")
                            : faite   ? QStringLiteral("<span style='color:#2E7D32; font-weight:600'>✓ C’est fait.</span>")
                                      : QStringLiteral("<span style='color:#6B7393'>À toi : l’étape se valide toute seule quand c’est fait.</span>"));
    etat->setObjectName(QStringLiteral("etatEtape"));
    etat->setWordWrap(true);
    v->addWidget(etat);
    if (e->mode != mode()) {
      auto* aller = new QPushButton(icone(QStringLiteral("apprendre")), QStringLiteral("Aller à l’onglet %1").arg(m_ruban->tabText(ongletMode(e->mode))));
      aller->setObjectName(QStringLiteral("allerOngletEtape"));
      const int voulu = e->mode;
      connect(aller, &QPushButton::clicked, this, [this, voulu] { choisirMode(voulu); });
      v->addWidget(aller);
    }
    auto* prec = new QToolButton;
    prec->setText(QStringLiteral("←"));
    prec->setToolTip(QStringLiteral("Étape précédente"));
    prec->setEnabled(i > 0);
    connect(prec, &QToolButton::clicked, this, [this] { allerEtapeParcours(m_etapeParcours - 1); });
    ligne->addWidget(prec);
    auto* suiv = new QPushButton(!e->faite ? QStringLiteral("Compris →") : faite ? QStringLiteral("Suivante →") : QStringLiteral("Passer →"));
    suiv->setObjectName(QStringLiteral("etapeSuivante"));
    if (!e->faite || faite) suiv->setStyleSheet(QStringLiteral("QPushButton { font-weight: 600; }"));
    connect(suiv, &QPushButton::clicked, this, [this] { allerEtapeParcours(m_etapeParcours + 1); });
    ligne->addWidget(suiv, 1);
  }
  auto* autres = new QToolButton;
  autres->setObjectName(QStringLiteral("autresParcours"));
  autres->setText(finie ? QStringLiteral("Autres parcours") : QStringLiteral("…"));
  autres->setToolTip(QStringLiteral("Changer de parcours (dans un projet neuf), ou quitter"));
  autres->setPopupMode(QToolButton::InstantPopup);
  auto* menu = new QMenu(autres);
  for (const auto& x : parcoursElectriques()) {
    const QString id = q(x.id);
    menu->addAction(icone(QStringLiteral("apprendre")), q(x.nom), this, [this, id] { emit parcoursDemande(id); });
  }
  menu->addSeparator();
  menu->addAction(QStringLiteral("Recommencer ce parcours"), this, [this] { allerEtapeParcours(0); });
  menu->addAction(QStringLiteral("Quitter le parcours"), this, [this] { quitterParcours(); });
  autres->setMenu(menu);
  ligne->addWidget(autres);
  if (finie) {
    auto* fermer = new QPushButton(QStringLiteral("Fermer"));
    fermer->setObjectName(QStringLiteral("fermerParcours"));
    connect(fermer, &QPushButton::clicked, this, [this] { quitterParcours(); });
    ligne->addWidget(fermer, 1);
  }
  v->addLayout(ligne);
  col->addWidget(g);
}

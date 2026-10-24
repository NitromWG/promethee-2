// Prométhée : « Remplacer… » un composant ou une sélection (lot L3, tâche 3.16). Voir DialogueRemplacer.hpp.
// Licence GPL-3.0-only.
#include "DialogueRemplacer.hpp"

#include <algorithm>
#include <set>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "InspecteurElectronique.hpp"
#include "VueSchema.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

DialogueRemplacer::DialogueRemplacer(AtelierElectronique& a, std::vector<std::string> composants, QWidget* parent)
    : QDialog(parent), m_atelier(a), m_composants(std::move(composants)) {
  setObjectName(QStringLiteral("dialogueRemplacer"));
  const ConceptionElec* c = a.conceptionCourante();
  QStringList refs;
  if (c)
    for (const auto& id : m_composants)
      if (const ComposantElec* x = c->composant(id)) refs << q(x->reference);
  setWindowTitle(refs.size() == 1 ? QStringLiteral("Remplacer %1").arg(refs.front()) : QStringLiteral("Remplacer %1 composants").arg(refs.size()));
  auto* col = new QVBoxLayout(this);
  // Portée (4.20) : la sélection, ou des motifs à jokers sur toute la conception
  auto* gp = new QGroupBox(QStringLiteral("Portée"));
  auto* lp = new QHBoxLayout(gp);
  m_porteeRepere = new QLineEdit;
  m_porteeRepere->setObjectName(QStringLiteral("porteeRepere"));
  m_porteeRepere->setPlaceholderText(QStringLiteral("Repère : R*, C1?"));
  m_porteeValeur = new QLineEdit;
  m_porteeValeur->setObjectName(QStringLiteral("porteeValeur"));
  m_porteeValeur->setPlaceholderText(QStringLiteral("Valeur : 10k, 100n*"));
  auto* viser = new QPushButton(QStringLiteral("Viser"));
  viser->setObjectName(QStringLiteral("viserPortee"));
  m_porteeBilan = new QLabel(QStringLiteral("%1 composant(s) de la sélection").arg(m_composants.size()));
  m_porteeBilan->setObjectName(QStringLiteral("bilanPortee"));
  lp->addWidget(m_porteeRepere);
  lp->addWidget(m_porteeValeur);
  lp->addWidget(viser);
  lp->addWidget(m_porteeBilan, 1);
  connect(viser, &QPushButton::clicked, this, [this] { appliquerPortee(m_porteeRepere->text(), m_porteeValeur->text()); });
  col->addWidget(gp);
  auto* haut = new QHBoxLayout;
  // Articles : ceux du projet, de la bibliothèque et de la bibliothèque personnelle
  auto* gauche = new QVBoxLayout;
  m_cherche = new QLineEdit;
  m_cherche->setObjectName(QStringLiteral("chercheArticle"));
  m_cherche->setPlaceholderText(QStringLiteral("Chercher un article : TL072, NE555…"));
  m_cherche->setClearButtonEnabled(true);
  connect(m_cherche, &QLineEdit::textChanged, this, &DialogueRemplacer::remplirArticles);
  m_articles = new QListWidget;
  m_articles->setObjectName(QStringLiteral("listeArticles"));
  connect(m_articles, &QListWidget::currentRowChanged, this, [this](int) {
    if (QListWidgetItem* it = m_articles->currentItem()) {
      m_article = it->data(Qt::UserRole).toString().toStdString();
      majApercu();
    }
  });
  gauche->addWidget(m_cherche);
  gauche->addWidget(m_articles, 1);
  haut->addLayout(gauche, 1);
  // Règle, champ par champ
  auto* droite = new QVBoxLayout;
  auto* gr = new QGroupBox(QStringLiteral("Garder du composant actuel"));
  auto* lr = new QVBoxLayout(gr);
  m_repere = new QCheckBox(QStringLiteral("Repère"));
  m_repere->setChecked(true);
  m_valeur = new QCheckBox(QStringLiteral("Valeur"));
  m_empreinte = new QCheckBox(QStringLiteral("Empreinte"));
  m_fabricant = new QCheckBox(QStringLiteral("Fabricant et référence"));
  for (QCheckBox* k : {m_repere, m_valeur, m_empreinte, m_fabricant}) {
    lr->addWidget(k);
    connect(k, &QCheckBox::toggled, this, [this] { majApercu(); });
  }
  droite->addWidget(gr);
  auto* gb = new QGroupBox(QStringLiteral("Correspondance des broches"));
  auto* lb = new QVBoxLayout(gb);
  m_parNom = new QRadioButton(QStringLiteral("Par nom (OUTA, +INA…)"));
  m_parNom->setObjectName(QStringLiteral("brochesParNom"));
  m_parNumero = new QRadioButton(QStringLiteral("Par numéro"));
  m_parNumero->setObjectName(QStringLiteral("brochesParNumero"));
  m_parNom->setChecked(true);
  lb->addWidget(m_parNom);
  lb->addWidget(m_parNumero);
  connect(m_parNom, &QRadioButton::toggled, this, [this] { majApercu(); });
  droite->addWidget(gb);
  // Comparaison en superposition : A (actuel) et B (remplaçant), curseur de transparence et bascule A/B
  auto* gs = new QGroupBox(QStringLiteral("Comparer"));
  auto* ls = new QVBoxLayout(gs);
  m_superposition = new Superposition;
  m_superposition->setObjectName(QStringLiteral("superpositionRemplacement"));
  m_superposition->setMinimumSize(200, 140);
  ls->addWidget(m_superposition);
  auto* lt = new QHBoxLayout;
  m_transparence = new QSlider(Qt::Horizontal);
  m_transparence->setObjectName(QStringLiteral("transparenceComparaison"));
  m_transparence->setRange(0, 100);
  m_transparence->setValue(50);
  m_transparence->setToolTip(QStringLiteral("0 : l’actuel seul ; 100 : le remplaçant seul"));
  m_ab = new QPushButton(QStringLiteral("A/B"));
  m_ab->setObjectName(QStringLiteral("basculeAB"));
  m_ab->setToolTip(QStringLiteral("Montrer l’un puis l’autre"));
  lt->addWidget(new QLabel(QStringLiteral("A")));
  lt->addWidget(m_transparence, 1);
  lt->addWidget(new QLabel(QStringLiteral("B")));
  lt->addWidget(m_ab);
  ls->addLayout(lt);
  connect(m_transparence, &QSlider::valueChanged, this, [this](int v) { m_superposition->reglerFondu(v / 100.0); });
  connect(m_ab, &QPushButton::clicked, this, &DialogueRemplacer::basculerAB);
  droite->addWidget(gs);
  droite->addStretch(1);
  haut->addLayout(droite);
  col->addLayout(haut, 1);
  // Aperçu : broches reliées et leur remplaçante
  m_apercu = new QTreeWidget;
  m_apercu->setObjectName(QStringLiteral("apercuRemplacement"));
  m_apercu->setHeaderLabels({QStringLiteral("Broche"), QStringLiteral("Devient"), QStringLiteral("Fils")});
  m_apercu->setRootIsDecorated(false);
  m_apercu->header()->setSectionResizeMode(QHeaderView::Stretch);
  m_apercu->setMaximumHeight(170);
  col->addWidget(m_apercu);
  m_bilan = inspecteur::texte();
  m_bilan->setObjectName(QStringLiteral("bilanRemplacement"));
  m_bilan->setWordWrap(true);
  col->addWidget(m_bilan);
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  m_ok = boutons->button(QDialogButtonBox::Ok);
  m_ok->setText(QStringLiteral("Remplacer"));
  m_ok->setObjectName(QStringLiteral("validerRemplacement"));
  boutons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annuler"));
  connect(m_ok, &QPushButton::clicked, this, [this] {
    if (appliquer() > 0) accept();
  });
  connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  col->addWidget(boutons);
  resize(760, 640);
  remplirArticles({});
  majApercu();
}

void DialogueRemplacer::remplirArticles(const QString& filtre) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const QSignalBlocker b(m_articles);
  m_articles->clear();
  std::set<std::string> vus;
  auto ajouter = [&](const DefSymbole& d) {
    if (d.alimentation || d.drapeau || !vus.insert(d.id).second) return;
    const QString t = q(d.nom) + QStringLiteral("  ·  ") + q(d.description);
    if (!filtre.isEmpty() && !t.contains(filtre, Qt::CaseInsensitive) && !q(d.id).contains(filtre, Qt::CaseInsensitive)) return;
    auto* it = new QListWidgetItem(t, m_articles);
    it->setData(Qt::UserRole, q(d.id));
    if (d.id == m_article) m_articles->setCurrentItem(it);
  };
  if (c)
    for (const auto& [id, d] : c->symboles) ajouter(d);
  for (const auto& d : AtelierElectronique::bibliothequePerso()) ajouter(d);
  for (const auto& d : bibliothequeSymboles()) ajouter(d);
}

void DialogueRemplacer::choisirArticle(const std::string& id) {
  m_article = id;
  for (int i = 0; i < m_articles->count(); ++i)
    if (m_articles->item(i)->data(Qt::UserRole).toString().toStdString() == id) {
      const QSignalBlocker b(m_articles);
      m_articles->setCurrentRow(i);
    }
  majApercu();
}

RegleRemplacement DialogueRemplacer::regle() const {
  RegleRemplacement r;
  r.brochesParNom = m_parNom->isChecked();
  r.garderRepere = m_repere->isChecked();
  r.garderValeur = m_valeur->isChecked();
  r.garderEmpreinte = m_empreinte->isChecked();
  r.garderFabricant = m_fabricant->isChecked();
  return r;
}

void DialogueRemplacer::majApercu() {
  m_apercu->clear();
  const ConceptionElec* c0 = m_atelier.conceptionCourante();
  if (!c0 || m_article.empty() || m_composants.empty()) {
    m_bilan->setText(QStringLiteral("Choisis l’article qui remplacera."));
    m_ok->setEnabled(false);
    return;
  }
  // L'article de la bibliothèque personnelle n'est pas encore dans le projet : l'aperçu se fait sur une copie
  ConceptionElec c = *c0;
  if (!c.symbole(m_article))
    for (const auto& d : AtelierElectronique::bibliothequePerso())
      if (d.id == m_article) c.symboles[d.id] = d;
  if (const ComposantElec* x = c.composant(m_composants.front()))
    if (const DefSymbole* da = c.symbole(x->symbole))
      if (const DefSymbole* db = c.symbole(m_article)) {
        const QSize t = m_superposition->size().expandedTo(QSize(200, 140));
        m_superposition->montrer(VueSchema::apercu(*da, t), VueSchema::apercu(*db, t));
      }
  int fils = 0;
  QStringList perdues;
  std::string avant, apres;
  for (const auto& id : m_composants) {
    const ApercuRemplacement a = apercuRemplacement(c, id, m_article, regle());
    fils += a.fils;
    for (const auto& p : a.perdues) perdues << q(p);
    if (id == m_composants.front()) {
      avant = a.empreinteAvant;
      apres = a.empreinteApres;
      for (const auto& b : a.reliees) {
        auto* it = new QTreeWidgetItem(m_apercu);
        it->setText(0, q(b.avant + (b.nom.empty() || b.nom == "~" ? std::string() : " " + b.nom)));
        it->setText(1, q(b.apres) + (b.deplacee ? QStringLiteral(" (fils retracés)") : QString()));
        it->setText(2, QStringLiteral("gardés"));
      }
      for (const auto& p : a.perdues) {
        auto* it = new QTreeWidgetItem(m_apercu);
        it->setText(0, q(p));
        it->setText(1, QStringLiteral("aucune"));
        it->setForeground(1, QColor(teintes::erreur()));
      }
    }
  }
  if (!perdues.isEmpty()) {
    m_bilan->setText(QStringLiteral("<span style='color:%1'>Impossible : broche(s) reliée(s) sans remplaçante (%2). Essaie la correspondance par numéro, ou un autre article.</span>")
                         .arg(teintes::erreur(), perdues.join(QStringLiteral(", ")).toHtmlEscaped()));
    m_ok->setEnabled(false);
    return;
  }
  m_bilan->setText(QStringLiteral("%1 fil(s) restent accrochés. Empreinte : %2 → %3, remplacée en place sur le circuit. Le tout en un pas d’annulation.")
                       .arg(fils)
                       .arg(q(avant.empty() ? std::string("aucune") : avant).toHtmlEscaped(), q(apres.empty() ? std::string("aucune") : apres).toHtmlEscaped()));
  m_ok->setEnabled(true);
}

int DialogueRemplacer::appliquer() {
  std::string erreur;
  int n = 0;
  std::optional<DefSymbole> perso;
  if (const ConceptionElec* c0 = m_atelier.conceptionCourante(); c0 && !c0->symbole(m_article))
    for (const auto& d : AtelierElectronique::bibliothequePerso())
      if (d.id == m_article) perso = d;
  const RegleRemplacement r = regle();
  const std::string article = m_article;
  const std::vector<std::string> ids = m_composants;
  m_atelier.modifierConception([&](ConceptionElec& c) {
    if (perso) c.symboles[perso->id] = *perso;
    n = remplacerComposants(c, ids, article, r, &erreur);
  }, ids.size() == 1 ? QStringLiteral("Remplacer par %1").arg(q(article)) : QStringLiteral("Remplacer %1 composants par %2").arg(ids.size()).arg(q(article)));
  if (n == 0) m_bilan->setText(QStringLiteral("<span style='color:%1'>%2</span>").arg(teintes::erreur(), q(erreur).toHtmlEscaped()));
  return n;
}

int DialogueRemplacer::appliquerPortee(const QString& repere, const QString& valeur) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return 0;
  PorteeMasse p;
  p.reference = repere.trimmed().toStdString();
  p.valeur = valeur.trimmed().toStdString();
  if (m_porteeRepere->text() != repere) m_porteeRepere->setText(repere);
  if (m_porteeValeur->text() != valeur) m_porteeValeur->setText(valeur);
  m_composants = composantsVises(*c, p);
  m_porteeBilan->setText(QStringLiteral("%1 composant(s) visé(s)").arg(m_composants.size()));
  setWindowTitle(QStringLiteral("Remplacer %1 composants").arg(m_composants.size()));
  majApercu();
  return static_cast<int>(m_composants.size());
}

void DialogueRemplacer::reglerTransparence(int pourcent) { m_transparence->setValue(std::clamp(pourcent, 0, 100)); }
int DialogueRemplacer::transparence() const { return m_transparence->value(); }
void DialogueRemplacer::basculerAB() { m_transparence->setValue(m_transparence->value() < 50 ? 100 : 0); }

Superposition::Superposition(QWidget* parent) : QWidget(parent) {}

void Superposition::montrer(const QPixmap& a, const QPixmap& b) {
  m_a = a;
  m_b = b;
  update();
}

void Superposition::reglerFondu(double t) {
  m_t = std::clamp(t, 0.0, 1.0);
  update();
}

void Superposition::paintEvent(QPaintEvent*) {
  QPainter g(this);
  g.fillRect(rect(), palette().base());
  auto poser = [&](const QPixmap& p, double opacite) {
    if (p.isNull() || opacite <= 0) return;
    g.setOpacity(opacite);
    const QPixmap r = p.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    g.drawPixmap((width() - r.width()) / 2, (height() - r.height()) / 2, r);
  };
  poser(m_a, 1.0 - m_t);
  poser(m_b, m_t);
}

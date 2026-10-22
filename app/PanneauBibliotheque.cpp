// Prométhée : panneau de bibliothèque de l'atelier électronique (lot L1, tâche 1.10). Voir PanneauBibliotheque.hpp.
// Licence GPL-3.0-only.
#include "PanneauBibliotheque.hpp"

#include "GlisserArticle.hpp"

#include <algorithm>
#include <map>
#include <utility>

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QSettings>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include "ApercuArticle.hpp"
#include "AtelierElectronique.hpp"
#include "Icones.hpp"
#include "InspecteurElectronique.hpp"
#include "Theme.hpp"
#include "VueSchema.hpp"
#include "promethee/bibliotheque.hpp"
#include "promethee/electronique.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }

// Accents et casse ignorés : « resistance » trouve « Résistance »
QString plat(const QString& s) {
  QString r;
  for (const QChar c : s.normalized(QString::NormalizationForm_D))
    if (c.category() != QChar::Mark_NonSpacing) r += c;
  return r.toLower();
}

struct Colonne {
  const char* id;
  const char* titre;
};
constexpr Colonne COLONNES[] = {{"description", "Description"}, {"categorie", "Catégorie"}, {"empreinte", "Empreinte"}, {"broches", "Broches"},
                                {"fabricant", "Fabricant"},     {"mpn", "Référence fabricant"}, {"prefixe", "Préfixe"}};

QString champ(const DefSymbole& d, const QString& id) {
  if (id == QLatin1String("description")) return q(d.description);
  if (id == QLatin1String("categorie")) return q(d.categorie);
  if (id == QLatin1String("empreinte")) return q(d.empreinte);
  if (id == QLatin1String("broches")) return QString::number(d.broches.size());
  if (id == QLatin1String("fabricant")) return q(d.fabricant);
  if (id == QLatin1String("mpn")) return q(d.mpn);
  if (id == QLatin1String("prefixe")) return q(d.prefixe);
  return {};
}

// Champs où chercher, aplatis, dans l'ordre de leur poids : nom, identifiant, fabricant et référence, mots-clés,
// description, catégorie
QStringList champsRecherche(const DefSymbole& d) {
  return {plat(q(d.nom)), plat(q(d.id)), plat(q(d.fabricant + " " + d.mpn)), plat(q(d.motsCles)), plat(q(d.description)), plat(q(d.categorie))};
}

int score(const QStringList& champs, const QStringList& mots) {
  if (champs.size() < 6) return 0;
  int total = 0;
  for (const QString& m : mots) {
    int meilleur = 0;
    const QString& nom = champs[0];
    if (nom == m) meilleur = 100;
    else if (nom.startsWith(m)) meilleur = 60;
    else if (nom.contains(m)) meilleur = 40;
    else if (champs[1].contains(m)) meilleur = 25;
    else if (champs[2].contains(m)) meilleur = 20;
    else if (champs[3].contains(m)) meilleur = 15;
    else if (champs[4].contains(m)) meilleur = 10;
    else if (champs[5].contains(m)) meilleur = 5;
    if (meilleur == 0) return 0;  // chaque mot doit être trouvé
    total += meilleur;
  }
  return total;
}

QStringList mots(const QString& texte) { return plat(texte).split(QLatin1Char(' '), Qt::SkipEmptyParts); }
}  // namespace

QStringList PanneauBibliotheque::colonnesPossibles() {
  QStringList r;
  for (const Colonne& c : COLONNES) r << QLatin1String(c.id);
  return r;
}

QString PanneauBibliotheque::titreColonne(const QString& id) {
  for (const Colonne& c : COLONNES)
    if (id == QLatin1String(c.id)) return QString::fromUtf8(c.titre);
  return id;
}

int PanneauBibliotheque::pertinence(const DefSymbole& d, const QString& recherche) {
  const QStringList m = mots(recherche);
  return m.isEmpty() ? 1 : score(champsRecherche(d), m);
}

void PanneauBibliotheque::setTri(Tri t) {
  m_triChoisi = t;
  QSettings().setValue(QStringLiteral("bibliotheque/tri"), static_cast<int>(t));
  if (m_tri->currentIndex() != static_cast<int>(t)) {
    const QSignalBlocker bloque(m_tri);
    m_tri->setCurrentIndex(static_cast<int>(t));
  }
  refaire();
}

void PanneauBibliotheque::setColonnes(const QStringList& ids) {
  m_colonnes.clear();
  for (const QString& id : ids)
    if (colonnesPossibles().contains(id) && !m_colonnes.contains(id)) m_colonnes << id;
  QSettings().setValue(QStringLiteral("bibliotheque/colonnes"), m_colonnes);
  refaire();
}

void PanneauBibliotheque::majEnTete() {
  QStringList titres{QStringLiteral("Nom")};
  for (const QString& id : m_colonnes) titres << titreColonne(id);
  m_arbre->setColumnCount(static_cast<int>(titres.size()));
  m_arbre->setHeaderLabels(titres);
  // Le nom seul prend la largeur ; avec des champs, chaque colonne prend la sienne et la liste défile
  QHeaderView* h = m_arbre->header();
  h->setStretchLastSection(false);
  for (int i = 0; i < titres.size(); ++i) h->setSectionResizeMode(i, m_colonnes.isEmpty() ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
}

// Menu de l'en-tête : les champs à montrer en colonnes
void PanneauBibliotheque::menuColonnes(const QPoint& p) {
  QMenu m(this);
  m.setObjectName(QStringLiteral("colonnesBibliotheque"));
  m.addSection(QStringLiteral("Colonnes"));
  for (const QString& id : colonnesPossibles()) {
    QAction* a = m.addAction(titreColonne(id));
    a->setCheckable(true);
    a->setChecked(m_colonnes.contains(id));
    a->setData(id);
  }
  const QAction* choisie = m.exec(m_arbre->header()->mapToGlobal(p));
  if (!choisie) return;
  QStringList c = m_colonnes;
  const QString id = choisie->data().toString();
  if (c.contains(id)) c.removeAll(id);
  else c << id;
  setColonnes(c);
}

std::optional<DefEmpreinte> PanneauBibliotheque::empreinteDe(const DefSymbole& d) const {
  if (d.empreinte.empty()) return std::nullopt;
  if (const ConceptionElec* c = m_atelier.conceptionCourante())
    if (const DefEmpreinte* e = c->empreinte(d.empreinte)) return *e;
  for (const DefEmpreinte& e : AtelierElectronique::empreintesPerso())
    if (e.id == d.empreinte) return e;
  if (const DefEmpreinte* e = empreinteEnCache(d.empreinte)) return *e;
  return std::nullopt;
}

PanneauBibliotheque::PanneauBibliotheque(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  setObjectName(QStringLiteral("panneauBibliotheque"));
  setMinimumWidth(230);
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(8, 8, 8, 8);
  // Encart « À poser » : l'article armé, et de quoi renoncer (S3)
  m_aPoser = new QWidget;
  m_aPoser->setObjectName(QStringLiteral("aPoser"));
  m_aPoser->setAttribute(Qt::WA_StyledBackground, true);
  {
    auto* h = new QHBoxLayout(m_aPoser);
    h->setContentsMargins(6, 6, 6, 6);
    m_aPoserApercu = new QLabel;
    m_aPoserApercu->setFixedSize(64, 44);
    m_aPoserApercu->setAlignment(Qt::AlignCenter);
    h->addWidget(m_aPoserApercu);
    m_aPoserTexte = inspecteur::texte();
    h->addWidget(m_aPoserTexte, 1);
    auto* annuler = new QPushButton(QStringLiteral("Annuler"));
    annuler->setObjectName(QStringLiteral("annulerPose"));
    connect(annuler, &QPushButton::clicked, this, [this] { m_atelier.vueSchema()->setOutil(VueSchema::Outil::Selection); });
    h->addWidget(annuler);
  }
  col->addWidget(m_aPoser);
  m_aPoser->hide();
  // La recherche et le bouton qui replie le panneau ; le titre est celui du panneau ancrable (1.21)
  {
    auto* tete = new QHBoxLayout;
    m_recherche = new QLineEdit;
    m_recherche->setObjectName(QStringLiteral("rechercheBibliotheque"));
    m_recherche->setPlaceholderText(QStringLiteral("Chercher : résistance, LED, 555, connecteur…"));
    m_recherche->setClearButtonEnabled(true);
    connect(m_recherche, &QLineEdit::textChanged, this, &PanneauBibliotheque::filtrer);
    // Entrée dans la recherche : le premier article trouvé est prêt à poser (B7)
    connect(m_recherche, &QLineEdit::returnPressed, this, [this] {
      for (QTreeWidgetItemIterator it(m_arbre); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).isValid() && !(*it)->isHidden() && (!(*it)->parent() || !(*it)->parent()->isHidden())) {
          m_arbre->setCurrentItem(*it);
          placer(*it, false);
          return;
        }
    });
    tete->addWidget(m_recherche, 1);
    auto* replier = new QToolButton;
    replier->setObjectName(QStringLiteral("replierBibliotheque"));
    replier->setText(QStringLiteral("«"));
    replier->setToolTip(QStringLiteral("Replier la bibliothèque (le bouton « Placer un composant » du ruban la rouvre)"));
    replier->setAutoRaise(true);
    connect(replier, &QToolButton::clicked, this, [this] { emit this->replier(); });
    tete->addWidget(replier);
    col->addLayout(tete);
  }
  m_alerte = inspecteur::texte();
  m_alerte->hide();
  col->addWidget(m_alerte);
  // Tri (BIB-04), gardé d'une session à l'autre
  {
    auto* h = new QHBoxLayout;
    h->addWidget(new QLabel(QStringLiteral("Trier")));
    m_tri = new QComboBox;
    m_tri->setObjectName(QStringLiteral("triBibliotheque"));
    m_tri->addItem(QStringLiteral("Par catégorie"), static_cast<int>(Tri::Categorie));
    m_tri->addItem(QStringLiteral("Par pertinence"), static_cast<int>(Tri::Pertinence));
    m_tri->addItem(QStringLiteral("Par ordre alphabétique"), static_cast<int>(Tri::Alphabetique));
    m_tri->setToolTip(QStringLiteral("Par pertinence : le nom exact d’abord, puis le début du nom, le nom, les mots-clés, la description"));
    h->addWidget(m_tri, 1);
    col->addLayout(h);
  }
  const QSettings reglages;
  m_triChoisi = static_cast<Tri>(std::clamp(reglages.value(QStringLiteral("bibliotheque/tri"), 0).toInt(), 0, 2));
  m_tri->setCurrentIndex(static_cast<int>(m_triChoisi));
  connect(m_tri, &QComboBox::currentIndexChanged, this, [this](int i) { setTri(static_cast<Tri>(m_tri->itemData(i).toInt())); });
  for (const QString& id : reglages.value(QStringLiteral("bibliotheque/colonnes")).toStringList())
    if (colonnesPossibles().contains(id) && !m_colonnes.contains(id)) m_colonnes << id;
  m_arbre = new QTreeWidget;
  m_arbre->setObjectName(QStringLiteral("arbreBibliotheque"));
  m_arbre->setMinimumHeight(160);
  m_arbre->header()->setContextMenuPolicy(Qt::CustomContextMenu);
  m_arbre->header()->setToolTip(QStringLiteral("Clic droit : les colonnes à montrer"));
  connect(m_arbre->header(), &QHeaderView::customContextMenuRequested, this, &PanneauBibliotheque::menuColonnes);
  col->addWidget(m_arbre, 1);
  connect(m_arbre, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) { montrerApercu(it); });
  // Un clic arme la pose, Entrée aussi ; l'article se glisse sur la feuille (ou, mécanique, sur le circuit) et
  // R, X, Y le tournent pendant le glisser (2.2, B7, SAI-01)
  connect(m_arbre, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) { placer(it, false); });
  connect(m_arbre, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) { placer(it, false); });
  m_glisser = new GlisserArticle(m_arbre, [this](const QModelIndex& i) -> std::optional<DefSymbole> {
    const QVariant id = i.siblingAtColumn(0).data(Qt::UserRole);
    const DefSymbole* d = id.isValid() ? trouver(id.toString().toStdString()) : nullptr;
    return d ? std::optional<DefSymbole>(*d) : std::nullopt;
  }, this);
  m_arbre->setToolTip(QStringLiteral("Clic : armer la pose · glisser sur la feuille pour poser (R tourne pendant le glisser) · Entrée : poser au clavier"));
  // Clic droit sur un article : poser, ou poser en série (SAI-03)
  m_arbre->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_arbre, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) {
    QTreeWidgetItem* it = m_arbre->itemAt(p);
    if (!it || !it->data(0, Qt::UserRole).isValid()) return;
    m_arbre->setCurrentItem(it);
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("menuArticle"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(icone(QStringLiteral("composants")), QStringLiteral("Poser"), this, [this, it] { placer(it, false); });
    menu->addAction(QStringLiteral("Poser en série"), this, [this, it] { placer(it, true); });
    menu->popup(m_arbre->viewport()->mapToGlobal(p));
  });
  // Aperçu à quatre faces (symbole, empreinte, 3D, photo) ; la face choisie est gardée
  m_apercu = new ApercuArticle;
  m_apercu->setMinimumHeight(170);
  m_apercu->setFace(static_cast<ApercuArticle::Face>(std::clamp(reglages.value(QStringLiteral("bibliotheque/face"), 0).toInt(), 0, ApercuArticle::NOMBRE_FACES - 1)));
  connect(m_apercu, &ApercuArticle::faceChangee, this, [](ApercuArticle::Face f) { QSettings().setValue(QStringLiteral("bibliotheque/face"), static_cast<int>(f)); });
  col->addWidget(m_apercu);
  m_info = inspecteur::texte();
  col->addWidget(m_info);
  {
    auto* h = new QHBoxLayout;
    auto* poser = new QPushButton(icone(QStringLiteral("composants")), QStringLiteral("Poser"));
    poser->setObjectName(QStringLiteral("poserArticle"));
    poser->setToolTip(QStringLiteral("Arme la pose de l’article choisi : un clic dans la feuille le pose"));
    connect(poser, &QPushButton::clicked, this, [this] { placer(m_arbre->currentItem(), false); });
    auto* serie = new QPushButton(QStringLiteral("Poser en série"));
    serie->setObjectName(QStringLiteral("poserEnSerie"));
    serie->setToolTip(QStringLiteral("Un clic par exemplaire, jusqu’à « Terminer » dans le bandeau"));
    connect(serie, &QPushButton::clicked, this, [this] { placer(m_arbre->currentItem(), true); });
    h->addWidget(poser);
    h->addWidget(serie);
    col->addLayout(h);
  }
  {
    auto* h = new QHBoxLayout;
    auto* creer = new QPushButton(icone(QStringLiteral("nouvelle-piece")), QStringLiteral("Créer…"));
    creer->setToolTip(QStringLiteral("Créer un composant : donne ses broches, le symbole se dessine tout seul"));
    connect(creer, &QPushButton::clicked, this, [this] { m_atelier.creerComposant(); });
    auto* kicad = new QPushButton(icone(QStringLiteral("kicad")), QStringLiteral("KiCad…"));
    kicad->setToolTip(QStringLiteral("Importer des symboles KiCad (.kicad_sym) : broches, préfixe, empreinte et fiche ; le corps est redessiné ici"));
    connect(kicad, &QPushButton::clicked, this, [this] { m_atelier.importerSymbolesKicad(); });
    h->addWidget(creer);
    h->addWidget(kicad);
    col->addLayout(h);
  }
  auto teinter = [this] {
    const CouleursInterface& c = Theme::instance().c();
    setProperty("themeFixe", true);
    setStyleSheet(QStringLiteral("#panneauBibliotheque { background: %1; } #aPoser { background: %2; border: 1px solid %3; border-radius: 8px; }")
                      .arg(c.surface.name(), c.choisi.name(), c.accentTexte.name()));
    majAPoser();
  };
  setAttribute(Qt::WA_StyledBackground, true);
  teinter();
  connect(&Theme::instance(), &Theme::change, this, teinter);
  connect(m_atelier.vueSchema(), &VueSchema::outilChange, this, &PanneauBibliotheque::majAPoser);
  refaire();
}

const DefSymbole* PanneauBibliotheque::trouver(const std::string& id) const {
  if (const DefSymbole* d = symboleBibliotheque(id)) return d;
  for (const auto& d : m_perso)
    if (d.id == id) return &d;
  return nullptr;
}

std::string PanneauBibliotheque::articleChoisi() const {
  const QTreeWidgetItem* it = m_arbre->currentItem();
  return it && it->data(0, Qt::UserRole).isValid() ? it->data(0, Qt::UserRole).toString().toStdString() : std::string();
}

void PanneauBibliotheque::refaire() {
  const std::string choisi = articleChoisi();
  // Bibliothèque intégrée, puis composants personnels, puis ceux qui ne sont que dans ce projet
  const LectureBibliotheque lecture = AtelierElectronique::lectureBibliothequePerso();
  m_perso = lecture.symboles;
  if (const ConceptionElec* c = m_atelier.conceptionCourante())
    for (const auto& [id, d] : c->symboles)
      if (!symboleBibliotheque(id) && std::none_of(m_perso.begin(), m_perso.end(), [&](const DefSymbole& x) { return x.id == id; })) {
        m_perso.push_back(d);
        m_perso.back().categorie = "Dans ce projet";
      }
  // Bibliothèque personnelle abîmée : dit, avec ce qui en a été récupéré (EL-08)
  m_alerte->setVisible(lecture.abimee);
  if (lecture.abimee)
    m_alerte->setText(QStringLiteral("<span style='color:%1'>Bibliothèque personnelle abîmée (%2) : %3 composant(s) récupéré(s). Elle sera mise de côté au prochain composant créé.</span>")
                          .arg(teintes::erreur(), q(lecture.erreur).toHtmlEscaped())
                          .arg(lecture.symboles.size()));
  m_arbre->clear();
  majEnTete();
  std::vector<const DefSymbole*> tous;
  for (const auto& d : m_perso) tous.push_back(&d);
  for (const auto& d : bibliothequeSymboles()) tous.push_back(&d);
  if (m_triChoisi == Tri::Alphabetique)
    std::stable_sort(tous.begin(), tous.end(), [](const DefSymbole* a, const DefSymbole* b) { return QString::localeAwareCompare(q(a->nom), q(b->nom)) < 0; });
  // Par catégorie : un arbre ; sinon une liste
  std::map<std::string, QTreeWidgetItem*> categories;
  QTreeWidgetItem* retrouve = nullptr;
  for (const DefSymbole* pd : tous) {
    const DefSymbole& d = *pd;
    QTreeWidgetItem* parent = nullptr;
    if (m_triChoisi == Tri::Categorie) {
      QTreeWidgetItem*& cat = categories[d.categorie];
      if (!cat) {
        cat = new QTreeWidgetItem(m_arbre, {q(d.categorie)});
        cat->setFirstColumnSpanned(true);
        QFont f = cat->font(0);
        f.setBold(true);
        cat->setFont(0, f);
        cat->setFlags(Qt::ItemIsEnabled);
      }
      parent = cat;
    }
    QStringList textes{q(d.nom)};
    for (const QString& id : m_colonnes) textes << champ(d, id);
    auto* it = parent ? new QTreeWidgetItem(parent, textes) : new QTreeWidgetItem(m_arbre, textes);
    it->setData(0, Qt::UserRole, q(d.id));
    it->setData(0, Qt::UserRole + 1, champsRecherche(d));
    it->setToolTip(0, q(d.description));
    if (d.id == choisi) retrouve = it;
  }
  m_arbre->expandAll();
  filtrer(m_recherche->text());
  if (retrouve) m_arbre->setCurrentItem(retrouve);
  else
    // Un aperçu tout de suite : le premier article visible
    for (QTreeWidgetItemIterator it(m_arbre); *it; ++it)
      if ((*it)->data(0, Qt::UserRole).isValid() && !(*it)->isHidden() && (!(*it)->parent() || !(*it)->parent()->isHidden())) {
        m_arbre->setCurrentItem(*it);
        break;
      }
}

// Les articles qui répondent à tous les mots de la recherche ; par pertinence, les mieux notés en tête
void PanneauBibliotheque::filtrer(const QString& texte) {
  const QStringList m = mots(texte);
  auto noter = [&m](QTreeWidgetItem* it) {
    const int n = m.isEmpty() ? 1 : score(it->data(0, Qt::UserRole + 1).toStringList(), m);
    it->setData(0, Qt::UserRole + 2, n);
    it->setHidden(n == 0);
    return n > 0;
  };
  if (m_triChoisi == Tri::Categorie) {
    for (int i = 0; i < m_arbre->topLevelItemCount(); ++i) {
      QTreeWidgetItem* cat = m_arbre->topLevelItem(i);
      int visibles = 0;
      for (int j = 0; j < cat->childCount(); ++j) visibles += noter(cat->child(j));
      cat->setHidden(visibles == 0);
    }
    return;
  }
  for (int i = 0; i < m_arbre->topLevelItemCount(); ++i) noter(m_arbre->topLevelItem(i));
  if (m_triChoisi != Tri::Pertinence) return;
  const QSignalBlocker bloque(m_arbre);
  QTreeWidgetItem* courant = m_arbre->currentItem();
  std::vector<QTreeWidgetItem*> items;
  while (m_arbre->topLevelItemCount() > 0) items.push_back(m_arbre->takeTopLevelItem(0));
  std::stable_sort(items.begin(), items.end(), [](const QTreeWidgetItem* a, const QTreeWidgetItem* b) {
    const int sa = a->data(0, Qt::UserRole + 2).toInt(), sb = b->data(0, Qt::UserRole + 2).toInt();
    return sa != sb ? sa > sb : QString::localeAwareCompare(a->text(0), b->text(0)) < 0;
  });
  for (QTreeWidgetItem* it : items) m_arbre->addTopLevelItem(it);
  if (courant) m_arbre->setCurrentItem(courant);
}

void PanneauBibliotheque::montrerApercu(QTreeWidgetItem* it) {
  if (!it || !it->data(0, Qt::UserRole).isValid()) return;
  const DefSymbole* d = trouver(it->data(0, Qt::UserRole).toString().toStdString());
  if (!d) return;
  const std::optional<DefEmpreinte> e = empreinteDe(*d);
  m_apercu->montrer(*d, e ? &*e : nullptr);
  m_info->setText(QStringLiteral("<b>%1</b><br><span style='color:%5'>%2</span><br><span style='color:%5'>%3 broche(s) · empreinte %4</span>")
                      .arg(q(d->nom).toHtmlEscaped(), q(d->description).toHtmlEscaped())
                      .arg(d->broches.size())
                      .arg(d->empreinte.empty() ? QStringLiteral("aucune") : q(d->empreinte), teintes::secondaire()) +
                  (d->unites > 1 ? QStringLiteral("<br><span style='color:%4'>%1 unités, posées une à une (%2 à %3)</span>").arg(d->unites).arg(q(lettreUnite(1)), q(lettreUnite(d->unites)), teintes::secondaire())
                                 : QString()));
}

// Arme la pose de l'article : la bibliothèque reste ouverte, l'inspecteur n'est pas touché (S3, B5)
void PanneauBibliotheque::placer(QTreeWidgetItem* it, bool serie) {
  if (!it || !it->data(0, Qt::UserRole).isValid()) return;
  const std::string id = it->data(0, Qt::UserRole).toString().toStdString();
  const DefSymbole* d = trouver(id);
  if (!d) return;
  VueSchema* vs = m_atelier.vueSchema();
  m_atelier.setMode(AtelierElectronique::Schema);
  // Un composant personnel entre dans le projet (définition verrouillée) avec la pose, en un pas (S4)
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (c && !symboleBibliotheque(id) && !c->symboles.count(id)) vs->armerPose(*d);
  else vs->setOutil(d->alimentation || d->drapeau ? VueSchema::Outil::Alimentation : VueSchema::Outil::Placer, id);
  if (serie) vs->setPoseEnSerie(true);
  vs->setFocus(Qt::OtherFocusReason);
}

void PanneauBibliotheque::chercher() {
  m_recherche->setFocus(Qt::ShortcutFocusReason);
  m_recherche->selectAll();
}

void PanneauBibliotheque::majAPoser() {
  const VueSchema* vs = m_atelier.vueSchema();
  const std::string id = vs ? vs->symboleArme() : std::string();
  const DefSymbole* d = id.empty() ? nullptr : trouver(id);
  if (!d)
    if (const ConceptionElec* c = m_atelier.conceptionCourante(); c && !id.empty()) d = c->symbole(id);
  m_aPoser->setVisible(d != nullptr);
  if (!d) return;
  m_aPoserApercu->setPixmap(VueSchema::apercu(*d, QSize(64, 44)));
  m_aPoserTexte->setText(QStringLiteral("<b>À poser : %1</b><br><span style='color:%2'>%3</span>")
                             .arg(q(d->nom).toHtmlEscaped(), teintes::secondaire(),
                                  vs->poseEnSerie() ? QStringLiteral("un clic par exemplaire, « Terminer » dans le bandeau") : QStringLiteral("un clic dans la feuille pour poser")));
}

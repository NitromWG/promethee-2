// Prométhée : atelier électrique — distribution. L'installation calculée de la source au dernier récepteur : à
// gauche l'arbre des tableaux et des départs, au centre le tableau des résultats (protection, canalisation, chute
// de tension, courts-circuits, protection des personnes, sélectivité), à droite les réglages de ce qui est choisi
// (la source ou un départ) et la justification de son calcul, chaque grandeur avec sa fiche d'aide. Tout se
// recalcule à chaque modification ; l'unifilaire et la note de calcul en sortent d'un clic. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMarginsF>
#include <QMenu>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextDocument>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include "AtelierElectrique.hpp"
#include "Document.hpp"
#include "EtiquetteEtat.hpp"
#include "Icones.hpp"
#include "promethee/electrique_glossaire.hpp"
#include "promethee/modele.hpp"

using namespace prom::elec;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& t) { return t.trimmed().toStdString(); }
QString nombre(double v, int decimales = -1) {
  if (decimales < 0) return QLocale().toString(v, 'g', 6);
  return QLocale().toString(v, 'f', decimales);
}
QString kA(double v) { return nombre(v, v < 10 ? 2 : 1); }

QString iconeCharge(TypeCharge t) {
  switch (t) {
    case TypeCharge::Tableau: return QStringLiteral("armoire");
    case TypeCharge::Eclairage: return QStringLiteral("eclairage");
    case TypeCharge::Prises: return QStringLiteral("prise");
    case TypeCharge::Chauffage: return QStringLiteral("chauffage");
    case TypeCharge::Moteur: return QStringLiteral("moteur");
    case TypeCharge::IRVE: return QStringLiteral("irve");
    case TypeCharge::Condensateurs: return QStringLiteral("condensateur");
    case TypeCharge::Autre: break;
  }
  return QStringLiteral("disjoncteur");
}
QString symboleEtat(int erreurs, int alertes) { return erreurs ? QStringLiteral("⛔") : alertes ? QStringLiteral("⚠") : QStringLiteral("✓"); }
// Dans l'arbre, seul ce qui demande attention porte un signe : les noms restent lisibles
QString signeArbre(int erreurs, int alertes) { return erreurs ? QStringLiteral("⛔ ") : alertes ? QStringLiteral("⚠ ") : QString(); }
QColor couleurEtat(int erreurs, int alertes) { return erreurs ? QColor(0xC2, 0x40, 0x2E) : alertes ? QColor(0xA8, 0x62, 0x0E) : QColor(0x2E, 0x7D, 0x32); }

// Lien « ⓘ » vers une fiche du glossaire
QString aide(const char* cle) { return QStringLiteral("<a href='aide:%1' style='text-decoration:none; color:#5B4BF5'>&nbsp;ⓘ</a>").arg(QString::fromLatin1(cle)); }

QString protectionCourte(const DepartBT& d, const ResultatDepartBT& y) {
  QString t;
  switch (y.protection) {
    case TypeProtection::Modulaire: t = QStringLiteral("%1 %2 A").arg(q(d.courbe == "B" || d.courbe == "D" ? d.courbe : std::string("C"))).arg(nombre(y.in)); break;
    case TypeProtection::Reglable: t = QStringLiteral("%1 A (Ir %2 A)").arg(nombre(y.in)).arg(nombre(y.ir, 0)); break;
    case TypeProtection::Moteur: t = QStringLiteral("Moteur %1 A").arg(nombre(y.ir, 1)); break;
    case TypeProtection::Fusible: t = QStringLiteral("gG %1 A").arg(nombre(y.in)); break;
    case TypeProtection::Auto: break;
  }
  t += QStringLiteral(" · %1 kA").arg(kA(y.pdc));
  if (d.differentiel > 0) t += QStringLiteral(" · %1 mA").arg(nombre(d.differentiel));
  return t;
}

// Justification du calcul d'un départ, chaque grandeur avec sa fiche
QString detailDepart(const DepartBT& d, const ResultatDepartBT& y, const std::string& amont) {
  QString h;
  auto ligne = [&h](const QString& t) { h += QStringLiteral("<p style='margin:0 0 5px 0'>%1</p>").arg(t); };
  if (d.charge == TypeCharge::Condensateurs)
    ligne(QStringLiteral("Courant d'emploi <b>%1 A</b> (1,36 fois le courant assigné des condensateurs)%2").arg(nombre(y.ib, 1), aide("ib")));
  else
    ligne(QStringLiteral("Puissance appelée <b>%1 kW</b>, %2 kvar, cos φ %3%4 ; courant d'emploi <b>Ib = %5 A</b>%6%7")
              .arg(nombre(y.p, 2), nombre(y.q, 2), nombre(y.cosPhi, 2), aide("cosphi"), nombre(y.ib, 1), aide("ib"),
                   y.iNeutre > 0 ? QStringLiteral(" ; neutre %1 A%2").arg(nombre(y.iNeutre, 1), aide("h3")) : QString()));
  ligne(QStringLiteral("Protection : <b>%1</b>%2 ; pouvoir de coupure %3 kA pour %4 kA au point de pose%5")
            .arg(q(y.designationProtection).toHtmlEscaped(), aide(y.protection == TypeProtection::Reglable || y.protection == TypeProtection::Moteur ? "ir" : "in"),
                 kA(y.pdc), kA(y.pdcRequis), aide("pdc")));
  ligne(QStringLiteral("Canalisation : <b>%1</b>, %2 m%3 ; K = %4%5 ; Iz = <b>%6 A</b> ≥ %7 A%8")
            .arg(q(y.designationCable).toHtmlEscaped(), nombre(d.longueur), aide("section"), nombre(y.k, 2), aide("k"), nombre(y.iz, 1), nombre(y.courantDimensionnement, 1),
                 aide("iz")));
  ligne(QStringLiteral("Chute de tension : %1 % sur la ligne, <b>%2 %</b> depuis l'origine (admise %3 %)%4%5")
            .arg(nombre(y.chute, 2), nombre(y.chuteCumulee, 2), nombre(y.chuteLimite, 0), aide("chute"),
                 y.chuteDemarrage > 0 ? QStringLiteral(" ; %1 % au démarrage%2").arg(nombre(y.chuteDemarrage, 1), aide("demarrage")) : QString()));
  ligne(QStringLiteral("Courts-circuits en bout de ligne : maximal <b>%1 kA</b>%2, minimal <b>%3 kA</b>%4 (instantané à %5 A) ; défaut à la masse %6 kA%7")
            .arg(kA(y.ik3max), aide("ik3"), kA(y.ikMin), aide("ikmin"), nombre(y.imHaut, 0), kA(y.ief), aide("ief")));
  ligne(QStringLiteral("Protection des personnes : <b>%1</b>%2%3")
            .arg(q(y.protectionPersonnes).toHtmlEscaped(), aide("personnes"),
                 y.longueurMax > 0 ? QStringLiteral(" ; longueur maximale %1 m%2").arg(nombre(y.longueurMax, 0), aide("lmax")) : QString()));
  if (y.i2tLaisse > 0)
    ligne(QStringLiteral("Contrainte thermique : I²t %1 MA²s ≤ k²S² %2 MA²s%3").arg(nombre(y.i2tLaisse / 1e6, 3), nombre(y.i2tAdmissible / 1e6, 3), aide("i2t")));
  if (!amont.empty()) ligne(QStringLiteral("Sélectivité avec %1 : <b>%2</b>%3").arg(q(amont), q(y.selectivite).toHtmlEscaped(), aide("selectivite")));
  return h;
}

}  // namespace

// ---------- Actions ----------
void AtelierElectrique::modifierReseau(const std::function<void(ReseauBT&)>& f) {
  modifierProjet([&](ProjetElectrique& p) {
    if (!p.distribution) p.distribution = reseauModele(ModeleReseau::Vide);
    f(*p.distribution);
  });
}

int AtelierElectrique::ajouterDepart(TypeCharge t) {
  if (!projetCourant()) return -1;
  std::string uuid, nomParent;
  int indice = -1;
  const std::string choisi = m_departChoisi;
  modifierReseau([&](ReseauBT& r) {
    // Sous le tableau choisi, ou celui du départ choisi, sinon le premier tableau alimenté par la source
    std::string parent;
    for (const auto& d : r.departs)
      if (d.uuid == choisi) parent = d.charge == TypeCharge::Tableau ? d.uuid : d.parent;
    if (parent.empty())
      for (const auto& d : r.departs)
        if (d.parent.empty() && d.charge == TypeCharge::Tableau) {
          parent = d.uuid;
          break;
        }
    // Premier tableau d'une installation vide : le tableau général
    if (t == TypeCharge::Tableau && std::none_of(r.departs.begin(), r.departs.end(), [](const DepartBT& d) { return d.charge == TypeCharge::Tableau; })) parent.clear();
    for (const auto& d : r.departs)
      if (d.uuid == parent) nomParent = d.repere;
    DepartBT d = nouveauDepart(r, t, parent);
    uuid = d.uuid;
    // Rangé après le dernier départ du même tableau
    std::size_t ou = r.departs.size();
    for (std::size_t k = 0; k < r.departs.size(); ++k)
      if (r.departs[k].parent == parent || r.departs[k].uuid == parent) ou = k + 1;
    r.departs.insert(r.departs.begin() + static_cast<std::ptrdiff_t>(std::min(ou, r.departs.size())), d);
    indice = static_cast<int>(std::min(ou, r.departs.size() - 1));
  });
  choisirDepart(uuid);
  if (const ProjetElectrique* pe = projetCourant(); pe && pe->distribution)
    for (const auto& d : pe->distribution->departs)
      if (d.uuid == uuid)
        m_etat->setText(QStringLiteral("<b>%1 — %2</b> ajouté%3 : règle sa puissance et sa longueur à droite, le reste se calcule.")
                            .arg(q(d.repere).toHtmlEscaped(), q(d.nom).toHtmlEscaped(), nomParent.empty() ? QString() : QStringLiteral(" sous %1").arg(q(nomParent).toHtmlEscaped())));
  return indice;
}

void AtelierElectrique::supprimerDepartChoisi() {
  const ProjetElectrique* pe = projetCourant();
  if (!pe || !pe->distribution || m_departChoisi.empty()) {
    m_etat->setText(QStringLiteral("Choisis d’abord un départ ou un tableau dans l’arbre."));
    return;
  }
  std::string parent, nom;
  for (const auto& d : pe->distribution->departs)
    if (d.uuid == m_departChoisi) {
      parent = d.parent;
      nom = d.repere + " — " + d.nom;
    }
  const std::string uuid = m_departChoisi;
  int n = 0;
  modifierReseau([&](ReseauBT& r) { n = supprimerDepart(r, uuid); });
  choisirDepart(parent);
  m_etat->setText(n > 1 ? QStringLiteral("<b>%1</b> supprimé avec ses %2 départ(s) ; Ctrl+Z le rétablit.").arg(q(nom).toHtmlEscaped()).arg(n - 1)
                        : QStringLiteral("<b>%1</b> supprimé ; Ctrl+Z le rétablit.").arg(q(nom).toHtmlEscaped()));
}

void AtelierElectrique::appliquerModele(ModeleReseau m) {
  if (!projetCourant()) return;
  const bool remplace = projetCourant()->distribution.has_value();
  modifierReseau([m](ReseauBT& r) { r = reseauModele(m); });
  choisirDepart({});
  m_etat->setText(remplace ? QStringLiteral("Modèle « %1 » chargé ; Ctrl+Z revient à l’installation précédente.").arg(QString::fromUtf8(nomModeleReseau(m)))
                           : QStringLiteral("Modèle « %1 » chargé : choisis un départ dans l’arbre pour le régler.").arg(QString::fromUtf8(nomModeleReseau(m))));
}

void AtelierElectrique::choisirDepart(const std::string& uuid) {
  m_departChoisi = uuid;
  if (m_arbreDistribution) {
    const QSignalBlocker b(m_arbreDistribution);
    for (QTreeWidgetItemIterator it(m_arbreDistribution); *it; ++it)
      if ((*it)->data(0, Qt::UserRole).toString() == q(uuid) && (*it)->data(0, Qt::UserRole + 1).toBool()) {
        m_arbreDistribution->setCurrentItem(*it);
        break;
      }
  }
  if (m_tableDistribution) {
    const QSignalBlocker b(m_tableDistribution);
    for (int r = 0; r < m_tableDistribution->rowCount(); ++r)
      if (QTableWidgetItem* it = m_tableDistribution->item(r, 1); it && it->data(Qt::UserRole).toString() == q(uuid)) {
        m_tableDistribution->selectRow(r);
        m_tableDistribution->scrollToItem(it);
        break;
      }
  }
  majPanneau();
}

int AtelierElectrique::genererUnifilaire() {
  const ProjetElectrique* pe = projetCourant();
  if (!pe || !pe->distribution || pe->distribution->departs.empty()) {
    m_etat->setText(QStringLiteral("Rien à dessiner : ajoute d’abord un tableau et ses départs (onglet Distribution)."));
    return -1;
  }
  std::size_t premier = 0;
  modifierProjet([&](ProjetElectrique& p) { premier = prom::elec::genererUnifilaire(p); });
  recalculer();
  majFolios();
  int n = 0;
  if (const ProjetElectrique* x = projetCourant())
    for (const auto& f : x->schema.feuilles) n += x->folio(f.uuid).origine == "distribution" ? 1 : 0;
  m_etat->setText(QStringLiteral("Unifilaire dessiné : %1 folio(s) à partir du folio %2, annotés des résultats du calcul ; il se redessine à chaque fois.")
                      .arg(n)
                      .arg(premier + 1));
  return static_cast<int>(premier);
}

bool AtelierElectrique::exporterNoteDeCalcul(const QString& chemin) {
  const ProjetElectrique* pe = projetCourant();
  if (!pe || !pe->distribution) return false;
  const ResultatReseauBT x = calculerReseau(*pe->distribution);
  const QString ext = QFileInfo(chemin).suffix().toLower();
  if (ext == QLatin1String("csv") || ext == QLatin1String("html") || ext == QLatin1String("htm")) {
    QFile f(chemin);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const std::string contenu = ext == QLatin1String("csv") ? csvNoteDeCalcul(*pe->distribution, x) : htmlNoteDeCalcul(*pe->distribution, x, pe->nom);
    return f.write(QByteArray::fromStdString(contenu)) == static_cast<qint64>(contenu.size());
  }
  QTextDocument doc;
  doc.setHtml(q(htmlNoteDeCalcul(*pe->distribution, x, pe->nom)));
  QPdfWriter pdf(chemin);
  pdf.setPageSize(QPageSize(QPageSize::A4));
  pdf.setPageOrientation(QPageLayout::Landscape);  // le tableau des départs a de la place
  pdf.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
  pdf.setTitle(QStringLiteral("Note de calcul — %1").arg(q(pe->distribution->nom)));
  pdf.setCreator(QStringLiteral("Prométhée"));
  doc.print(&pdf);
  return QFileInfo(chemin).size() > 0;
}

// ---------- Colonne de gauche : l'arbre de l'installation ----------
QWidget* AtelierElectrique::gaucheDistribution() {
  auto* w = new QWidget;
  auto* col = new QVBoxLayout(w);
  col->setContentsMargins(8, 6, 6, 6);
  col->setSpacing(4);
  auto* tete = new QHBoxLayout;
  tete->setSpacing(2);
  auto* t = new QLabel(QStringLiteral("<span style='font-weight:700; color:#232A45; font-size:13px'>Installation</span>"));
  t->setContentsMargins(2, 6, 2, 2);
  tete->addWidget(t);
  tete->addStretch(1);
  auto* plus = new QToolButton;
  plus->setObjectName(QStringLiteral("ajouterDepartArbre"));
  plus->setText(QStringLiteral("+"));
  plus->setToolTip(QStringLiteral("Ajouter un départ au tableau choisi"));
  plus->setPopupMode(QToolButton::InstantPopup);
  auto* menu = new QMenu(plus);
  for (TypeCharge c : {TypeCharge::Tableau, TypeCharge::Eclairage, TypeCharge::Prises, TypeCharge::Chauffage, TypeCharge::Moteur, TypeCharge::IRVE, TypeCharge::Condensateurs,
                       TypeCharge::Autre})
    menu->addAction(icone(iconeCharge(c)), QString::fromUtf8(nomTypeCharge(c)), this, [this, c] { ajouterDepart(c); });
  plus->setMenu(menu);
  tete->addWidget(plus);
  auto* moins = new QToolButton;
  moins->setIcon(icone(QStringLiteral("supprimer")));
  moins->setToolTip(QStringLiteral("Supprimer le départ choisi (un tableau part avec ses départs)"));
  connect(moins, &QToolButton::clicked, this, [this] { supprimerDepartChoisi(); });
  tete->addWidget(moins);
  for (QToolButton* b : {plus, moins}) {
    b->setAutoRaise(true);
    b->setFixedHeight(26);
    b->setMinimumWidth(26);
  }
  col->addLayout(tete);
  m_arbreDistribution = new QTreeWidget;
  m_arbreDistribution->setObjectName(QStringLiteral("arbreDistribution"));
  m_arbreDistribution->setHeaderHidden(true);
  m_arbreDistribution->setIconSize(QSize(20, 20));
  m_arbreDistribution->setIndentation(11);
  connect(m_arbreDistribution, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
    if (it && it->data(0, Qt::UserRole + 1).toBool()) choisirDepart(s(it->data(0, Qt::UserRole).toString()));
  });
  col->addWidget(m_arbreDistribution, 1);
  auto* aideArbre = new QLabel(QStringLiteral("<span style='color:#6B7393'>Choisis la source ou un départ : ses réglages sont à droite. Une valeur laissée sur "
                                              "« auto » est calculée ; une valeur donnée est vérifiée.</span>"));
  aideArbre->setWordWrap(true);
  col->addWidget(aideArbre);
  return w;
}

// ---------- Page centrale : modèles ou résultats ----------
QWidget* AtelierElectrique::pageDistribution() {
  auto* racine = new QWidget;
  racine->setObjectName(QStringLiteral("pageDistribution"));
  auto* pile = new QVBoxLayout(racine);
  pile->setContentsMargins(0, 0, 0, 0);
  m_pileDistribution = new QStackedWidget;
  pile->addWidget(m_pileDistribution);
  // Installation vide : on part d'un modèle
  {
    auto* defil = new QScrollArea;
    defil->setWidgetResizable(true);
    defil->setFrameShape(QFrame::NoFrame);
    auto* w = new QWidget;
    w->setObjectName(QStringLiteral("distributionVide"));
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(28, 22, 28, 22);
    col->setSpacing(10);
    auto* titre = new QLabel(
        QStringLiteral("<span style='font-size:20px; font-weight:700; color:#10142A'>Calcule une installation électrique</span><br>"
                       "<span style='color:#4A5270'>De la source (réseau public, transformateur, groupe électrogène) jusqu’aux récepteurs : Prométhée choisit les "
                       "protections et les câbles, vérifie les chutes de tension, les courts-circuits, la protection des personnes et la sélectivité selon la "
                       "NF C 15-100 et le guide UTE C 15-105, puis dessine l’unifilaire et rédige la note de calcul.</span>"));
    titre->setWordWrap(true);
    col->addWidget(titre);
    auto* grille = new QGridLayout;
    grille->setSpacing(10);
    const std::tuple<ModeleReseau, const char*, const char*> modeles[] = {
        {ModeleReseau::Vide, "plan", "Une source et un tableau général : tu ajoutes tes départs un à un."},
        {ModeleReseau::Tertiaire, "transformateur", "Poste privé, tableau général, deux tableaux d’étage, ventilation, ascenseur, groupe froid, compensation."},
        {ModeleReseau::Atelier, "moteur", "Tarif jaune, schéma TT, moteurs à démarrage direct, éclairage, prises et bureaux."},
        {ModeleReseau::ParkingIRVE, "irve", "Huit bornes de 7,4 kW réparties sur les phases, câbles enterrés, sans foisonnement."},
        {ModeleReseau::Maison, "logement", "Branchement monophasé de 12 kVA, différentiels 30 mA, pouvoir de coupure de 3 kA."}};
    int k = 0;
    for (const auto& [m, ic, desc] : modeles) {
      // Carte cliquable : icône, titre et description qui passent à la ligne
      auto* b = new QPushButton;
      b->setObjectName(QStringLiteral("modele-%1").arg(static_cast<int>(m)));
      b->setStyleSheet(QStringLiteral("QPushButton#%1 { border: 1px solid #CDD3E4; border-radius: 10px; background: palette(base); }"
                                      "QPushButton#%1:hover { border-color: #E0A040; }")
                           .arg(b->objectName()));
      auto* ligneCarte = new QHBoxLayout(b);
      ligneCarte->setContentsMargins(14, 10, 14, 10);
      ligneCarte->setSpacing(12);
      auto* image = new QLabel;
      image->setPixmap(icone(QString::fromLatin1(ic)).pixmap(QSize(34, 34)));
      image->setAttribute(Qt::WA_TransparentForMouseEvents);
      ligneCarte->addWidget(image, 0, Qt::AlignTop);
      auto* texte = new QLabel(QStringLiteral("<b>%1</b><br><span style='color:#6B7393'>%2</span>").arg(QString::fromUtf8(nomModeleReseau(m)), QString::fromUtf8(desc)));
      texte->setWordWrap(true);
      texte->setAttribute(Qt::WA_TransparentForMouseEvents);
      ligneCarte->addWidget(texte, 1);
      b->setMinimumHeight(82);
      b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
      connect(b, &QPushButton::clicked, this, [this, m = m] { appliquerModele(m); });
      grille->addWidget(b, k / 2, k % 2);
      ++k;
    }
    col->addLayout(grille);
    col->addStretch(1);
    defil->setWidget(w);
    m_pileDistribution->addWidget(defil);
  }
  // Résultats
  {
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(12, 10, 12, 8);
    col->setSpacing(6);
    m_bandeauDistribution = new QLabel;
    m_bandeauDistribution->setObjectName(QStringLiteral("bandeauDistribution"));
    m_bandeauDistribution->setWordWrap(true);
    m_bandeauDistribution->setTextFormat(Qt::RichText);
    m_bandeauDistribution->setStyleSheet(QStringLiteral("QLabel { background: #F3F5FB; border: 1px solid #E1E5F0; border-radius: 9px; padding: 8px 10px; }"));
    connect(m_bandeauDistribution, &QLabel::linkActivated, this, [this](const QString& l) {
      if (const TermeGlossaire* t = termeGlossaire(s(l.mid(5)))) {
        QToolTip::showText(QCursor::pos(), QStringLiteral("<b>%1</b><br>%2<br><i>%3</i>").arg(q(t->terme).toHtmlEscaped(), q(t->definition).toHtmlEscaped(), q(t->exemple).toHtmlEscaped()));
        m_etat->setText(QStringLiteral("<b>%1</b> : %2").arg(q(t->terme).toHtmlEscaped(), q(t->definition).toHtmlEscaped()));
      }
    });
    col->addWidget(m_bandeauDistribution);
    auto* boutons = new QHBoxLayout;
    boutons->setSpacing(4);
    auto compact = [](QPushButton* b) {
      b->setStyleSheet(QStringLiteral("QPushButton { padding: 4px 8px; }"));
      b->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    };
    auto* uni = new QPushButton(icone(QStringLiteral("schema")), QStringLiteral("Dessiner l’unifilaire"));
    uni->setObjectName(QStringLiteral("dessinerUnifilaire"));
    uni->setToolTip(QStringLiteral("Un folio par tableau, annoté des résultats ; redessiné à chaque fois"));
    connect(uni, &QPushButton::clicked, this, [this] {
      const int f = genererUnifilaire();
      if (f >= 0) {
        choisirMode(Schema);
        allerAuFolio(f);
      }
    });
    compact(uni);
    boutons->addWidget(uni);
    auto* note = new QPushButton(icone(QStringLiteral("exporter")), QStringLiteral("Note de calcul…"));
    note->setToolTip(QStringLiteral("Note de calcul en PDF : source, bilan, départs, justifications, avis, hypothèses"));
    connect(note, &QPushButton::clicked, this, [this] {
      const QString c = QFileDialog::getSaveFileName(this, QStringLiteral("Note de calcul"), q(projetCourant() ? projetCourant()->nom : std::string("installation")) + QStringLiteral(" - note de calcul.pdf"),
                                                     QStringLiteral("PDF (*.pdf);;Page web (*.html);;Tableur (*.csv)"));
      if (!c.isEmpty()) m_etat->setText(exporterNoteDeCalcul(c) ? QStringLiteral("Note de calcul écrite : %1").arg(c) : QStringLiteral("<span style='color:#C2402E'>Écriture impossible.</span>"));
    });
    compact(note);
    boutons->addWidget(note);
    boutons->addStretch(1);
    col->addLayout(boutons);
    m_tableDistribution = new QTableWidget;
    m_tableDistribution->setObjectName(QStringLiteral("tableDistribution"));
    m_tableDistribution->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableDistribution->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableDistribution->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableDistribution->setAlternatingRowColors(true);
    m_tableDistribution->verticalHeader()->setVisible(false);
    m_tableDistribution->horizontalHeader()->setStretchLastSection(true);
    m_tableDistribution->setWordWrap(false);
    connect(m_tableDistribution, &QTableWidget::itemSelectionChanged, this, [this] {
      const auto sel = m_tableDistribution->selectedItems();
      if (sel.isEmpty()) return;
      if (QTableWidgetItem* it = m_tableDistribution->item(sel.front()->row(), 1)) choisirDepart(s(it->data(Qt::UserRole).toString()));
    });
    col->addWidget(m_tableDistribution, 1);
    auto* legende = new QLabel(QStringLiteral("<span style='color:#6B7393'>✓ conforme · ⚠ à surveiller · ⛔ à corriger. Icc max : au bout de la ligne, il fixe le pouvoir de "
                                              "coupure du tableau suivant ; Icc min : il doit encore faire déclencher la protection.</span>"));
    legende->setWordWrap(true);
    col->addWidget(legende);
    m_pileDistribution->addWidget(w);
  }
  return racine;
}

void AtelierElectrique::majDistribution() {
  if (!m_tableDistribution || !m_arbreDistribution) return;
  const ProjetElectrique* pe = projetCourant();
  const bool existe = pe && pe->distribution;
  m_pileDistribution->setCurrentIndex(existe ? 1 : 0);
  {
    const QSignalBlocker b1(m_arbreDistribution), b2(m_tableDistribution);
    m_arbreDistribution->clear();
    m_tableDistribution->clear();
    m_tableDistribution->setRowCount(0);
  }
  if (!existe) {
    m_resultatDistribution = {};
    m_departChoisi.clear();
    auto* it = new QTreeWidgetItem(m_arbreDistribution);
    it->setText(0, QStringLiteral("Pas encore d’installation"));
    it->setForeground(0, QColor(0x6B, 0x73, 0x93));
    it->setToolTip(0, QStringLiteral("Choisis un modèle au centre"));
    return;
  }
  const ReseauBT& r = *pe->distribution;
  m_resultatDistribution = calculerReseau(r);
  const ResultatReseauBT& x = m_resultatDistribution;
  if (!m_departChoisi.empty() && std::none_of(r.departs.begin(), r.departs.end(), [&](const DepartBT& d) { return d.uuid == m_departChoisi; })) m_departChoisi.clear();
  const auto ordre = ordreArbre(r);
  std::map<std::string, std::string> repereDe;
  for (const auto& d : r.departs) repereDe[d.uuid] = d.repere;
  int erreursSource = 0, alertesSource = 0;
  for (const auto& a : x.avis) {
    if (a.niveau == NiveauAvis::Erreur) ++erreursSource;
    if (a.niveau == NiveauAvis::Alerte) ++alertesSource;
  }
  // ----- Arbre -----
  {
    const QSignalBlocker b(m_arbreDistribution);
    auto* source = new QTreeWidgetItem(m_arbreDistribution);
    source->setText(0, signeArbre(erreursSource, alertesSource) + QString::fromUtf8(nomTypeSource(r.source.type)));
    source->setIcon(0, icone(r.source.type == TypeSourceBT::Groupe ? QStringLiteral("moteur") : QStringLiteral("transformateur")));
    source->setData(0, Qt::UserRole, QString());
    source->setData(0, Qt::UserRole + 1, true);
    source->setToolTip(0, QStringLiteral("Icc à l’origine %1 kA · appel %2 kVA").arg(kA(x.ik3Source), nombre(x.s, 1)));
    if (erreursSource + alertesSource) source->setForeground(0, couleurEtat(erreursSource, alertesSource));
    std::vector<QTreeWidgetItem*> parents{source};
    QTreeWidgetItem* courant = m_departChoisi.empty() ? source : nullptr;
    for (const auto& [i, prof] : ordre) {
      const DepartBT& d = r.departs[i];
      const ResultatDepartBT& y = x.departs[i];
      parents.resize(static_cast<std::size_t>(prof) + 1);
      auto* it = new QTreeWidgetItem(parents[static_cast<std::size_t>(prof)]);
      it->setText(0, QStringLiteral("%1%2 · %3").arg(signeArbre(y.erreurs(), y.alertes()), q(d.repere), q(d.nom)));
      it->setIcon(0, icone(iconeCharge(d.charge)));
      it->setData(0, Qt::UserRole, q(d.uuid));
      it->setData(0, Qt::UserRole + 1, true);
      if (y.erreurs() + y.alertes()) it->setForeground(0, couleurEtat(y.erreurs(), y.alertes()));
      it->setToolTip(0, QStringLiteral("%1\n%2, %3 m\nΔU %4 % · Icc %5 kA").arg(q(y.designationProtection), q(y.designationCable), nombre(d.longueur), nombre(y.chuteCumulee, 2), kA(y.ik3max)));
      parents.push_back(it);
      if (d.uuid == m_departChoisi) courant = it;
    }
    m_arbreDistribution->expandAll();
    if (courant) m_arbreDistribution->setCurrentItem(courant);
  }
  // ----- Tableau des résultats -----
  {
    const QSignalBlocker b(m_tableDistribution);
    const QStringList entetes = {QStringLiteral(""),          QStringLiteral("Repère"),          QStringLiteral("Désignation"), QStringLiteral("P (kW)"),
                                 QStringLiteral("Ib (A)"),    QStringLiteral("Protection"),      QStringLiteral("Canalisation"), QStringLiteral("ΔU cumulée"),
                                 QStringLiteral("Icc max"),   QStringLiteral("Icc min"),         QStringLiteral("Personnes"),   QStringLiteral("Sélectivité")};
    m_tableDistribution->setColumnCount(static_cast<int>(entetes.size()));
    m_tableDistribution->setHorizontalHeaderLabels(entetes);
    m_tableDistribution->setRowCount(static_cast<int>(ordre.size()) + 1);
    auto cellule = [this](int ligne, int c, const QString& t, const QColor& couleur = QColor()) {
      auto* it = new QTableWidgetItem(t);
      if (couleur.isValid()) it->setForeground(couleur);
      m_tableDistribution->setItem(ligne, c, it);
      return it;
    };
    // La source d'abord
    cellule(0, 0, symboleEtat(erreursSource, alertesSource), couleurEtat(erreursSource, alertesSource));
    cellule(0, 1, QStringLiteral("Source"))->setData(Qt::UserRole, QString());
    cellule(0, 2, QString::fromUtf8(nomTypeSource(r.source.type)));
    cellule(0, 3, nombre(x.p, 1));
    cellule(0, 4, nombre(x.ib, 1));
    cellule(0, 5, QStringLiteral("—"));
    cellule(0, 6, QStringLiteral("—"));
    cellule(0, 7, QStringLiteral("—"));
    cellule(0, 8, kA(x.ik3Source));
    cellule(0, 9, QStringLiteral("—"));
    cellule(0, 10, QString::fromUtf8(nomRegime(r.source.regime)));
    cellule(0, 11, QStringLiteral("—"));
    int ligne = 1, choisie = m_departChoisi.empty() ? 0 : -1;
    for (const auto& [i, prof] : ordre) {
      const DepartBT& d = r.departs[i];
      const ResultatDepartBT& y = x.departs[i];
      const QColor c = couleurEtat(y.erreurs(), y.alertes());
      cellule(ligne, 0, symboleEtat(y.erreurs(), y.alertes()), c);
      cellule(ligne, 1, q(d.repere))->setData(Qt::UserRole, q(d.uuid));
      cellule(ligne, 2, QString(prof * 3, QLatin1Char(' ')) + q(d.nom));
      cellule(ligne, 3, d.charge == TypeCharge::Condensateurs ? QStringLiteral("%1 kvar").arg(nombre(-y.q, 0)) : nombre(y.p, 2));
      cellule(ligne, 4, nombre(y.ib, 1));
      cellule(ligne, 5, protectionCourte(d, y));
      cellule(ligne, 6, QStringLiteral("%1 · %2 m").arg(q(y.designationCable), nombre(d.longueur)));
      cellule(ligne, 7, QStringLiteral("%1 %").arg(nombre(y.chuteCumulee, 2)), y.chuteCumulee > y.chuteLimite + 1e-9 ? QColor(0xC2, 0x40, 0x2E) : QColor());
      cellule(ligne, 8, kA(y.ik3max));
      cellule(ligne, 9, kA(y.ikMin));
      cellule(ligne, 10, q(y.protectionPersonnes), y.personnesProtegees ? QColor() : QColor(0xC2, 0x40, 0x2E));
      cellule(ligne, 11, q(y.selectivite));
      for (int k = 0; k < m_tableDistribution->columnCount(); ++k)
        if (QTableWidgetItem* it = m_tableDistribution->item(ligne, k)) it->setToolTip(QStringLiteral("%1 — %2").arg(q(d.repere), q(d.nom)));
      if (d.uuid == m_departChoisi) choisie = ligne;
      ++ligne;
    }
    m_tableDistribution->resizeColumnsToContents();
    m_tableDistribution->setColumnWidth(0, 26);
    for (int c = 1; c < m_tableDistribution->columnCount(); ++c) m_tableDistribution->setColumnWidth(c, std::min(m_tableDistribution->columnWidth(c), 260));
    if (choisie >= 0) m_tableDistribution->selectRow(choisie);
  }
  // ----- Bandeau : la source et l'ensemble -----
  {
    QString t = QStringLiteral("<b>%1</b>").arg(QString::fromUtf8(nomTypeSource(r.source.type)));
    switch (r.source.type) {
      case TypeSourceBT::Transformateur: t += QStringLiteral(" %1 kVA, ucc %2 %").arg(nombre(r.source.puissance), nombre(r.source.ucc)); break;
      case TypeSourceBT::Groupe: t += QStringLiteral(" %1 kVA").arg(nombre(r.source.puissance)); break;
      case TypeSourceBT::ReseauPublic:
      case TypeSourceBT::BranchementLimite: t += QStringLiteral(" %1 kVA").arg(nombre(r.source.puissance)); break;
    }
    t += QStringLiteral(" · %1%2 · Icc à l’origine <b>%3 kA</b>%4 · appel <b>%5 kVA</b>")
             .arg(QString::fromUtf8(nomRegime(r.source.regime)), aide(r.source.regime == RegimeNeutre::TT ? "tt" : r.source.regime == RegimeNeutre::IT ? "it" : "tn"), kA(x.ik3Source),
                  aide("icc"), nombre(x.s, 1));
    if (r.source.puissance > 0) t += QStringLiteral(" (%1 %)").arg(nombre(x.tauxCharge, 0));
    t += QStringLiteral(", cos φ %1%2").arg(nombre(x.cosPhi, 2), aide("cosphi"));
    const QColor c = couleurEtat(x.erreurs, x.alertes);
    t += QStringLiteral("<br><span style='color:%1; font-weight:600'>%2 %3 erreur(s) · %4 alerte(s)</span> sur %5 départ(s)")
             .arg(c.name(), symboleEtat(x.erreurs, x.alertes))
             .arg(x.erreurs)
             .arg(x.alertes)
             .arg(r.departs.size());
    if (x.compensation > 0) t += QStringLiteral(" · compensation conseillée <b>%1 kvar</b>%2").arg(nombre(x.compensation), aide("compensation"));
    if (r.source.type == TypeSourceBT::Transformateur && x.transformateurConseille > 0 && x.transformateurConseille < r.source.puissance)
      t += QStringLiteral(" · %1 kVA suffiraient").arg(nombre(x.transformateurConseille));
    m_bandeauDistribution->setText(t);
  }
  // Le détail du départ choisi, dans le panneau, suit le calcul
  if (m_detailDistribution) {
    std::string amont;
    for (std::size_t i = 0; i < r.departs.size(); ++i)
      if (r.departs[i].uuid == m_departChoisi) {
        amont = repereDe.count(r.departs[i].parent) ? repereDe[r.departs[i].parent] : std::string();
        m_detailDistribution->setText(detailDepart(r.departs[i], x.departs[i], amont));
      }
  }
}

// ---------- Panneau de droite : réglages et justification ----------
void AtelierElectrique::panneauDistribution(QVBoxLayout* col) {
  m_detailDistribution = nullptr;
  const ProjetElectrique& pe = *projetCourant();
  if (!pe.distribution) {
    auto* l = new QLabel(QStringLiteral("<b>Pas encore d’installation.</b><br>Choisis un modèle au centre (ou « Installation vide ») : la source et le tableau général "
                                        "arrivent, tu ajoutes ensuite les départs avec le bouton « Départ » du ruban."));
    l->setWordWrap(true);
    col->addWidget(l);
    return;
  }
  const ReseauBT& r = *pe.distribution;
  // Le panneau peut se construire juste après une modification, avant le recalcul différé : résultat remis à jour
  bool aJour = m_resultatDistribution.departs.size() == r.departs.size();
  for (std::size_t i = 0; aJour && i < r.departs.size(); ++i) aJour = m_resultatDistribution.departs[i].uuid == r.departs[i].uuid;
  if (!aJour) m_resultatDistribution = calculerReseau(r);
  const ResultatReseauBT& x = m_resultatDistribution;
  auto groupe = [col](const QString& t, const QString& nom = {}) {
    auto* g = new QGroupBox(t);
    if (!nom.isEmpty()) g->setObjectName(nom);
    auto* f = new QFormLayout(g);
    f->setContentsMargins(6, 4, 6, 4);
    f->setSpacing(4);
    col->addWidget(g);
    return f;
  };
  // Champs liés : la modification passe par l'historique ; une valeur inchangée ne crée pas de pas
  auto reel = [this](QFormLayout* f, const QString& etiquette, double valeur, double min, double max, int decimales, const QString& suffixe, const QString& nom,
                     const std::function<void(double)>& appliquer, const QString& bulle = {}, const QString& auto0 = {}) {
    auto* sp = new QDoubleSpinBox;
    sp->setObjectName(nom);
    sp->setRange(min, max);
    sp->setDecimals(decimales);
    sp->setSuffix(suffixe);
    if (!auto0.isEmpty()) sp->setSpecialValueText(auto0);
    sp->setValue(valeur);
    sp->setKeyboardTracking(false);
    if (!bulle.isEmpty()) sp->setToolTip(bulle);
    connect(sp, &QDoubleSpinBox::editingFinished, this, [sp, valeur, appliquer] {
      if (std::abs(sp->value() - valeur) > 1e-9) appliquer(sp->value());
    });
    f->addRow(etiquette, sp);
    return sp;
  };
  auto entier = [this](QFormLayout* f, const QString& etiquette, int valeur, int min, int max, const QString& nom, const std::function<void(int)>& appliquer,
                       const QString& bulle = {}, const QString& auto0 = {}) {
    auto* sp = new QSpinBox;
    sp->setObjectName(nom);
    sp->setRange(min, max);
    if (!auto0.isEmpty()) sp->setSpecialValueText(auto0);
    sp->setValue(valeur);
    sp->setKeyboardTracking(false);
    if (!bulle.isEmpty()) sp->setToolTip(bulle);
    connect(sp, &QSpinBox::editingFinished, this, [sp, valeur, appliquer] {
      if (sp->value() != valeur) appliquer(sp->value());
    });
    f->addRow(etiquette, sp);
    return sp;
  };
  auto choix = [this](QFormLayout* f, const QString& etiquette, const QStringList& items, int courant, const QString& nom, const std::function<void(int)>& appliquer,
                      const QString& bulle = {}) {
    auto* c = new QComboBox;
    c->setObjectName(nom);
    c->addItems(items);
    c->setCurrentIndex(std::clamp(courant, 0, static_cast<int>(items.size()) - 1));
    if (!bulle.isEmpty()) c->setToolTip(bulle);
    connect(c, &QComboBox::currentIndexChanged, this, [courant, appliquer](int i) {
      if (i != courant && i >= 0) appliquer(i);
    });
    f->addRow(etiquette, c);
    return c;
  };
  auto coche = [this](QFormLayout* f, const QString& texte, bool valeur, const QString& nom, const std::function<void(bool)>& appliquer, const QString& bulle = {}) {
    auto* c = new QCheckBox(texte);
    c->setObjectName(nom);
    c->setChecked(valeur);
    if (!bulle.isEmpty()) c->setToolTip(bulle);
    connect(c, &QCheckBox::toggled, this, [appliquer](bool v) { appliquer(v); });
    f->addRow(QString(), c);
    return c;
  };
  auto ligneTexte = [this](QFormLayout* f, const QString& etiquette, const std::string& valeur, const QString& nom, const std::function<void(const std::string&)>& appliquer) {
    auto* e = new QLineEdit(q(valeur));
    e->setObjectName(nom);
    e->setCursorPosition(0);  // le début du nom, pas sa fin
    connect(e, &QLineEdit::editingFinished, this, [e, valeur, appliquer] {
      if (s(e->text()) != valeur && !s(e->text()).empty()) appliquer(s(e->text()));
    });
    f->addRow(etiquette, e);
    return e;
  };
  auto avisListe = [col](const std::vector<AvisCalcul>& avis) {
    if (avis.empty()) return;
    auto* g = new QGroupBox(QStringLiteral("Avis"));
    g->setObjectName(QStringLiteral("avisDistribution"));
    auto* v = new QVBoxLayout(g);
    v->setSpacing(4);
    for (const auto& a : avis) {
      const QString icone = a.niveau == NiveauAvis::Erreur ? QStringLiteral("⛔") : a.niveau == NiveauAvis::Alerte ? QStringLiteral("⚠") : QStringLiteral("ⓘ");
      const QString couleur = a.niveau == NiveauAvis::Erreur ? QStringLiteral("#C2402E") : a.niveau == NiveauAvis::Alerte ? QStringLiteral("#A8620E") : QStringLiteral("#4A5270");
      auto* l = new QLabel(QStringLiteral("<span style='color:%1'>%2 %3</span> <span style='color:#8A91AD'>(%4)</span>")
                               .arg(couleur, icone, q(a.message).toHtmlEscaped(), q(a.regle).toHtmlEscaped()));
      l->setWordWrap(true);
      v->addWidget(l);
    }
    col->addWidget(g);
  };
  auto lienAide = [this](QLabel* l) {
    l->setTextFormat(Qt::RichText);
    l->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(l, &QLabel::linkActivated, this, [this](const QString& lien) {
      if (const TermeGlossaire* t = termeGlossaire(s(lien.mid(5)))) {
        QToolTip::showText(QCursor::pos(), QStringLiteral("<b>%1</b><br>%2<br><i>%3</i><br><span style='color:#6B7393'>%4</span>")
                                               .arg(q(t->terme).toHtmlEscaped(), q(t->definition).toHtmlEscaped(), q(t->exemple).toHtmlEscaped(), q(t->regle).toHtmlEscaped()));
        m_etat->setText(QStringLiteral("<b>%1</b> : %2 <i>%3</i>").arg(q(t->terme).toHtmlEscaped(), q(t->definition).toHtmlEscaped(), q(t->exemple).toHtmlEscaped()));
      }
    });
  };

  // ===== Source =====
  const DepartBT* dp = nullptr;
  std::size_t di = 0;
  for (std::size_t i = 0; i < r.departs.size(); ++i)
    if (r.departs[i].uuid == m_departChoisi) {
      dp = &r.departs[i];
      di = i;
    }
  if (!dp) {
    const SourceBT& src = r.source;
    auto* tete = new QLabel(QStringLiteral("<span style='font-size:14px; font-weight:700'>Source</span><br><span style='color:#6B7393'>Ce qui alimente l’installation : "
                                           "son impédance fixe les courants de court-circuit de tous les tableaux.</span>%1")
                                .arg(aide("source")));
    tete->setWordWrap(true);
    lienAide(tete);
    col->addWidget(tete);
    auto modifierSource = [this](const std::function<void(SourceBT&)>& f) { modifierReseau([&](ReseauBT& x) { f(x.source); }); };
    QFormLayout* f = groupe(QStringLiteral("Alimentation"), QStringLiteral("groupeSource"));
    QStringList types;
    for (TypeSourceBT t : {TypeSourceBT::BranchementLimite, TypeSourceBT::ReseauPublic, TypeSourceBT::Transformateur, TypeSourceBT::Groupe}) types << QString::fromUtf8(nomTypeSource(t));
    choix(f, QStringLiteral("Type"), types, static_cast<int>(src.type), QStringLiteral("typeSource"), [modifierSource](int i) {
      modifierSource([i](SourceBT& x) {
        x.type = static_cast<TypeSourceBT>(i);
        if (x.type == TypeSourceBT::BranchementLimite || x.type == TypeSourceBT::ReseauPublic) x.regime = RegimeNeutre::TT;
        if (x.type == TypeSourceBT::BranchementLimite) x.puissance = std::min(x.puissance, 36.0);
        if (x.type == TypeSourceBT::ReseauPublic) x.puissance = std::clamp(x.puissance, 36.0, 250.0);
      });
    });
    const QString nomPuissance = src.type == TypeSourceBT::BranchementLimite ? QStringLiteral("Puissance souscrite")
                                 : src.type == TypeSourceBT::ReseauPublic      ? QStringLiteral("Puissance de raccordement")
                                                                               : QStringLiteral("Puissance");
    reel(f, nomPuissance, src.puissance, 1, 20000, 0, QStringLiteral(" kVA"), QStringLiteral("puissanceSource"), [modifierSource](double v) { modifierSource([v](SourceBT& x) { x.puissance = v; }); });
    if (src.type == TypeSourceBT::Transformateur) {
      reel(f, QStringLiteral("Tension de court-circuit"), src.ucc, 1, 15, 1, QStringLiteral(" %"), QStringLiteral("uccSource"),
           [modifierSource](double v) { modifierSource([v](SourceBT& x) { x.ucc = v; }); }, QStringLiteral("4 % jusqu’à 630 kVA, 6 % au-delà"));
      reel(f, QStringLiteral("Pertes dues à la charge"), src.pertesCuivre, 0, 100000, 0, QStringLiteral(" W"), QStringLiteral("pertesSource"),
           [modifierSource](double v) { modifierSource([v](SourceBT& x) { x.pertesCuivre = v; }); }, QStringLiteral("plaque du transformateur ; « auto » : R = 0,31 Z"), QStringLiteral("auto"));
      reel(f, QStringLiteral("Réseau amont (Scc)"), src.sccAmont, 0, 10000, 0, QStringLiteral(" MVA"), QStringLiteral("sccSource"),
           [modifierSource](double v) { modifierSource([v](SourceBT& x) { x.sccAmont = v; }); }, QStringLiteral("500 MVA par défaut (réseau HTA 20 kV)"));
    }
    if (src.type == TypeSourceBT::BranchementLimite) {
      reel(f, QStringLiteral("Icc au point de livraison"), src.icc, 0.5, 100, 1, QStringLiteral(" kA"), QStringLiteral("iccSource"),
           [modifierSource](double v) { modifierSource([v](SourceBT& x) { x.icc = v; }); }, QStringLiteral("donnée du distributeur (à défaut, NF C 14-100)"));
      coche(f, QStringLiteral("Branchement triphasé"), src.triphase, QStringLiteral("triphaseSource"), [modifierSource](bool v) { modifierSource([v](SourceBT& x) { x.triphase = v; }); });
      coche(f, QStringLiteral("Installation domestique (3 kA suffisent)"), src.domestique, QStringLiteral("domestiqueSource"),
            [modifierSource](bool v) { modifierSource([v](SourceBT& x) { x.domestique = v; }); });
    }
    if (src.type == TypeSourceBT::Groupe) {
      reel(f, QStringLiteral("Réactance subtransitoire x''d"), src.xdSub, 1, 50, 1, QStringLiteral(" %"), QStringLiteral("xdSub"),
           [modifierSource](double v) { modifierSource([v](SourceBT& x) { x.xdSub = v; }); });
      reel(f, QStringLiteral("Réactance transitoire x'd"), src.xdTrans, 1, 100, 1, QStringLiteral(" %"), QStringLiteral("xdTrans"),
           [modifierSource](double v) { modifierSource([v](SourceBT& x) { x.xdTrans = v; }); });
      reel(f, QStringLiteral("Réactance homopolaire x0"), src.x0, 0, 50, 1, QStringLiteral(" %"), QStringLiteral("x0"), [modifierSource](double v) { modifierSource([v](SourceBT& x) { x.x0 = v; }); });
    }
    reel(f, QStringLiteral("Tension entre phases"), src.tension, 100, 1000, 0, QStringLiteral(" V"), QStringLiteral("tensionSource"),
         [modifierSource](double v) { modifierSource([v](SourceBT& x) { x.tension = v; }); });
    QFormLayout* t = groupe(QStringLiteral("Liaisons à la terre"));
    choix(t, QStringLiteral("Schéma"), {QStringLiteral("TT"), QStringLiteral("TN-S"), QStringLiteral("TN-C"), QStringLiteral("IT")}, static_cast<int>(src.regime), QStringLiteral("regimeSource"),
          [modifierSource](int i) { modifierSource([i](SourceBT& x) { x.regime = static_cast<RegimeNeutre>(i); }); }, QStringLiteral("TT sur le réseau public ; TN ou IT depuis un poste privé"));
    if (src.regime == RegimeNeutre::TT)
      reel(t, QStringLiteral("Prise de terre RA"), src.resistanceTerre, 0.1, 1000, 1, QStringLiteral(" Ω"), QStringLiteral("terreSource"),
           [modifierSource](double v) { modifierSource([v](SourceBT& x) { x.resistanceTerre = v; }); }, QStringLiteral("résistance de la prise de terre des masses"));
    if (src.regime == RegimeNeutre::IT)
      coche(t, QStringLiteral("Contrôleur permanent d’isolement posé"), src.controleurIsolement, QStringLiteral("cpiSource"),
            [modifierSource](bool v) { modifierSource([v](SourceBT& x) { x.controleurIsolement = v; }); });
    QFormLayout* g = groupe(QStringLiteral("Installation"));
    ligneTexte(g, QStringLiteral("Nom"), r.nom, QStringLiteral("nomInstallation"), [this](const std::string& v) { modifierReseau([v](ReseauBT& x) { x.nom = v; }); });
    reel(g, QStringLiteral("cos φ visé"), r.cosPhiCible, 0.8, 1, 2, QString(), QStringLiteral("cosPhiCible"), [this](double v) { modifierReseau([v](ReseauBT& x) { x.cosPhiCible = v; }); });
    reel(g, QStringLiteral("ΔU max éclairage"), r.chuteEclairage, 0, 20, 1, QStringLiteral(" %"), QStringLiteral("chuteEclairage"),
         [this](double v) { modifierReseau([v](ReseauBT& x) { x.chuteEclairage = v; }); }, QStringLiteral("« auto » : 3 % sur le réseau public, 6 % depuis un poste privé"), QStringLiteral("auto"));
    reel(g, QStringLiteral("ΔU max autres usages"), r.chuteAutres, 0, 20, 1, QStringLiteral(" %"), QStringLiteral("chuteAutres"),
         [this](double v) { modifierReseau([v](ReseauBT& x) { x.chuteAutres = v; }); }, QStringLiteral("« auto » : 5 % sur le réseau public, 8 % depuis un poste privé"), QStringLiteral("auto"));
    coche(g, QStringLiteral("Sélectivité recherchée (tableaux ≥ 1,6 × départs)"), r.selectivite, QStringLiteral("selectiviteReseau"),
          [this](bool v) { modifierReseau([v](ReseauBT& x) { x.selectivite = v; }); });
    auto* res = new QLabel(QStringLiteral("Icc à l’origine <b>%1 kA</b>%2 (R %3 mΩ, X %4 mΩ)<br>Appel <b>%5 kW</b>, %6 kvar, <b>%7 kVA</b>, %8 A ; cos φ %9")
                               .arg(kA(x.ik3Source), aide("icc"), nombre(x.rSource, 2), nombre(x.xSource, 2), nombre(x.p, 1), nombre(x.q, 1), nombre(x.s, 1), nombre(x.ib, 1),
                                    nombre(x.cosPhi, 2)) +
                           (x.compensation > 0 ? QStringLiteral("<br>Compensation conseillée : <b>%1 kvar</b>%2").arg(nombre(x.compensation), aide("compensation")) : QString()) +
                           (src.type == TypeSourceBT::Transformateur ? QStringLiteral("<br>Transformateur conseillé (25 % de réserve) : %1 kVA").arg(nombre(x.transformateurConseille)) : QString()));
    res->setObjectName(QStringLiteral("resultatSource"));
    res->setWordWrap(true);
    lienAide(res);
    auto* gr = new QGroupBox(QStringLiteral("Résultat"));
    auto* vr = new QVBoxLayout(gr);
    vr->addWidget(res);
    col->addWidget(gr);
    avisListe(x.avis);
    return;
  }

  // ===== Départ =====
  const DepartBT& d = *dp;
  const ResultatDepartBT& y = x.departs[di];
  const std::string uuid = d.uuid;
  auto modifierDepart = [this, uuid](const std::function<void(DepartBT&)>& f) {
    modifierReseau([&](ReseauBT& x) {
      for (auto& z : x.departs)
        if (z.uuid == uuid) f(z);
    });
  };
  std::string amont;
  for (const auto& z : r.departs)
    if (z.uuid == d.parent) amont = z.repere;
  const QColor ce = couleurEtat(y.erreurs(), y.alertes());
  auto* tete = new QLabel(QStringLiteral("<span style='font-size:14px; font-weight:700'>%1 — %2</span><br><span style='color:%3'>%4 %5</span>")
                              .arg(q(d.repere).toHtmlEscaped(), q(d.nom).toHtmlEscaped(), ce.name(), symboleEtat(y.erreurs(), y.alertes()),
                                   y.erreurs() ? QStringLiteral("%1 erreur(s) à corriger").arg(y.erreurs())
                                   : y.alertes() ? QStringLiteral("%1 point(s) à surveiller").arg(y.alertes())
                                                 : QStringLiteral("conforme")));
  tete->setObjectName(QStringLiteral("teteDepart"));
  tete->setWordWrap(true);
  col->addWidget(tete);
  // Résultat d'abord : c'est ce qu'on vient voir ; il suit les modifications sans reconstruire le panneau
  {
    auto* g = new QGroupBox(QStringLiteral("Résultat du calcul"));
    g->setObjectName(QStringLiteral("groupeResultatDepart"));
    auto* v = new QVBoxLayout(g);
    auto* l = new QLabel(detailDepart(d, y, amont));
    l->setObjectName(QStringLiteral("detailDepart"));
    l->setWordWrap(true);
    lienAide(l);
    v->addWidget(l);
    m_detailDistribution = l;
    col->addWidget(g);
  }
  avisListe(y.avis);
  const bool tableau = d.charge == TypeCharge::Tableau, mono = d.polarite == Polarite::Monophase;
  // ----- Récepteur -----
  {
    QFormLayout* f = groupe(tableau ? QStringLiteral("Tableau") : QStringLiteral("Récepteur"), QStringLiteral("groupeRecepteur"));
    ligneTexte(f, QStringLiteral("Repère"), d.repere, QStringLiteral("repereDepart"), [modifierDepart](const std::string& v) { modifierDepart([v](DepartBT& z) { z.repere = v; }); });
    ligneTexte(f, QStringLiteral("Désignation"), d.nom, QStringLiteral("nomDepart"), [modifierDepart](const std::string& v) { modifierDepart([v](DepartBT& z) { z.nom = v; }); });
    QStringList types;
    for (TypeCharge c : {TypeCharge::Tableau, TypeCharge::Eclairage, TypeCharge::Prises, TypeCharge::Chauffage, TypeCharge::Moteur, TypeCharge::IRVE, TypeCharge::Condensateurs,
                         TypeCharge::Autre})
      types << QString::fromUtf8(nomTypeCharge(c));
    choix(f, QStringLiteral("Type"), types, static_cast<int>(d.charge), QStringLiteral("typeDepart"), [modifierDepart](int i) { modifierDepart([i](DepartBT& z) { z.charge = static_cast<TypeCharge>(i); }); });
    // Tableau amont : la source ou un tableau qui n'est pas alimenté par celui-ci
    {
      QStringList noms{QStringLiteral("la source")};
      std::vector<std::string> ids{std::string()};
      std::set<std::string> descendants{uuid};
      for (bool ajoute = true; ajoute;) {
        ajoute = false;
        for (const auto& z : r.departs)
          if (descendants.count(z.parent) && !descendants.count(z.uuid)) {
            descendants.insert(z.uuid);
            ajoute = true;
          }
      }
      int courant = 0;
      for (const auto& z : r.departs)
        if (z.charge == TypeCharge::Tableau && !descendants.count(z.uuid)) {
          if (z.uuid == d.parent) courant = static_cast<int>(ids.size());
          noms << QStringLiteral("%1 — %2").arg(q(z.repere), q(z.nom));
          ids.push_back(z.uuid);
        }
      choix(f, QStringLiteral("Alimenté par"), noms, courant, QStringLiteral("parentDepart"), [modifierDepart, ids](int i) {
        const std::string p = ids[static_cast<std::size_t>(i)];
        modifierDepart([p](DepartBT& z) { z.parent = p; });
      });
    }
    choix(f, QStringLiteral("Alimentation"), {QString::fromUtf8(nomPolarite(Polarite::TriphaseNeutre)), QString::fromUtf8(nomPolarite(Polarite::Triphase)), QString::fromUtf8(nomPolarite(Polarite::Monophase))},
          static_cast<int>(d.polarite), QStringLiteral("polariteDepart"), [modifierDepart](int i) { modifierDepart([i](DepartBT& z) { z.polarite = static_cast<Polarite>(i); }); });
    if (mono)
      choix(f, QStringLiteral("Phase"), {QStringLiteral("répartie"), QStringLiteral("L1"), QStringLiteral("L2"), QStringLiteral("L3")}, d.phase, QStringLiteral("phaseDepart"),
            [modifierDepart](int i) { modifierDepart([i](DepartBT& z) { z.phase = i; }); }, QStringLiteral("pour équilibrer les phases du tableau"));
    if (!tableau) {
      const bool cond = d.charge == TypeCharge::Condensateurs;
      reel(f, cond ? QStringLiteral("Puissance réactive") : d.charge == TypeCharge::Moteur ? QStringLiteral("Puissance utile") : QStringLiteral("Puissance"), d.puissance, 0, 100000, 2,
           cond ? QStringLiteral(" kvar") : QStringLiteral(" kW"), QStringLiteral("puissanceDepart"), [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.puissance = v; }); });
      if (!cond) {
        reel(f, QStringLiteral("cos φ"), d.cosPhi, 0.1, 1, 2, QString(), QStringLiteral("cosPhiDepart"), [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.cosPhi = v; }); });
        if (d.charge == TypeCharge::Moteur) {
          reel(f, QStringLiteral("Rendement"), d.rendement, 0.3, 1, 2, QString(), QStringLiteral("rendementDepart"),
               [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.rendement = v; }); });
          reel(f, QStringLiteral("Démarrage Id/In"), d.demarrage, 0, 12, 1, QString(), QStringLiteral("demarrageDepart"),
               [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.demarrage = v; }); }, QStringLiteral("direct ≈ 7, étoile-triangle ≈ 2,5, variateur ≈ 1,5 ; « non vérifié » à 0"),
               QStringLiteral("non vérifié"));
        }
        reel(f, QStringLiteral("Utilisation ku"), d.ku, 0, 1, 2, QString(), QStringLiteral("kuDepart"), [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.ku = v; }); });
        reel(f, QStringLiteral("Ib imposé"), d.courantImpose, 0, 10000, 1, QStringLiteral(" A"), QStringLiteral("ibDepart"),
             [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.courantImpose = v; }); }, QStringLiteral("courant d’emploi connu (sinon tiré de la puissance)"), QStringLiteral("auto"));
      }
    } else {
      reel(f, QStringLiteral("Simultanéité ks"), d.ks, 0, 1, 2, QString(), QStringLiteral("ksDepart"), [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.ks = v; }); },
           QStringLiteral("« auto » : 0,9 à 0,6 selon le nombre de départs (bornes de recharge à 1)"), QStringLiteral("auto"));
    }
    if (d.polarite == Polarite::TriphaseNeutre)
      reel(f, QStringLiteral("Harmonique 3"), d.tauxH3, 0, 100, 0, QStringLiteral(" %"), QStringLiteral("h3Depart"), [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.tauxH3 = v; }); },
           QStringLiteral("part d’harmonique de rang 3 du courant (informatique, LED : 20 à 40 %)"));
  }
  // ----- Canalisation -----
  {
    QFormLayout* f = groupe(QStringLiteral("Canalisation"), QStringLiteral("groupeCanalisation"));
    reel(f, QStringLiteral("Longueur"), d.longueur, 0, 100000, 1, QStringLiteral(" m"), QStringLiteral("longueurDepart"), [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.longueur = v; }); });
    choix(f, QStringLiteral("Âme"), {QStringLiteral("Cuivre"), QStringLiteral("Aluminium")}, static_cast<int>(d.ame), QStringLiteral("ameDepart"),
          [modifierDepart](int i) { modifierDepart([i](DepartBT& z) { z.ame = static_cast<Ame>(i); }); });
    choix(f, QStringLiteral("Isolant"), {QStringLiteral("PVC (70 °C)"), QStringLiteral("PR, U1000 R2V (90 °C)")}, static_cast<int>(d.isolant), QStringLiteral("isolantDepart"),
          [modifierDepart](int i) { modifierDepart([i](DepartBT& z) { z.isolant = static_cast<Isolant>(i); }); });
    QStringList poses;
    for (MethodePose m : {MethodePose::A1, MethodePose::A2, MethodePose::B1, MethodePose::B2, MethodePose::C, MethodePose::E, MethodePose::D1}) poses << QString::fromUtf8(nomMethodePose(m));
    choix(f, QStringLiteral("Pose"), poses, static_cast<int>(d.pose), QStringLiteral("poseDepart"), [modifierDepart](int i) {
      modifierDepart([i](DepartBT& z) {
        z.pose = static_cast<MethodePose>(i);
        if (z.pose == MethodePose::D1 && z.temperature >= 30) z.temperature = 20;  // le sol est plus frais que l'air
      });
    });
    reel(f, d.pose == MethodePose::D1 ? QStringLiteral("Température du sol") : QStringLiteral("Température ambiante"), d.temperature, -20, 70, 0, QStringLiteral(" °C"),
         QStringLiteral("temperatureDepart"), [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.temperature = v; }); });
    entier(f, QStringLiteral("Circuits jointifs"), d.circuitsJointifs, 1, 30, QStringLiteral("groupementDepart"), [modifierDepart](int v) { modifierDepart([v](DepartBT& z) { z.circuitsJointifs = v; }); },
           QStringLiteral("circuits posés côte à côte avec celui-ci (facteur de groupement)"));
    coche(f, QStringLiteral("Câbles unipolaires"), d.unipolaire, QStringLiteral("unipolaireDepart"), [modifierDepart](bool v) { modifierDepart([v](DepartBT& z) { z.unipolaire = v; }); });
    entier(f, QStringLiteral("Câbles par phase"), d.parallele, 0, 8, QStringLiteral("paralleleDepart"), [modifierDepart](int v) { modifierDepart([v](DepartBT& z) { z.parallele = v; }); },
           QStringLiteral("conducteurs en parallèle ; « auto » : au besoin, au-delà de 240 mm²"), QStringLiteral("auto"));
    QStringList sections{QStringLiteral("auto")};
    const std::vector<double>& sn = sectionsNormalisees();
    int courant = 0;
    for (std::size_t k = 0; k < sn.size(); ++k) {
      sections << QStringLiteral("%1 mm²").arg(nombre(sn[k]));
      if (std::abs(sn[k] - d.sectionImposee) < 1e-9) courant = static_cast<int>(k) + 1;
    }
    choix(f, QStringLiteral("Section imposée"), sections, courant, QStringLiteral("sectionDepart"), [modifierDepart, sn](int i) {
      const double v = i == 0 ? 0.0 : sn[static_cast<std::size_t>(i - 1)];
      modifierDepart([v](DepartBT& z) { z.sectionImposee = v; });
    });
    int courantMini = 0;
    for (std::size_t k = 0; k < sn.size(); ++k)
      if (std::abs(sn[k] - d.sectionMini) < 1e-9) courantMini = static_cast<int>(k) + 1;
    choix(f, QStringLiteral("Section minimale"), sections, courantMini, QStringLiteral("sectionMiniDepart"), [modifierDepart, sn](int i) {
      const double v = i == 0 ? 0.0 : sn[static_cast<std::size_t>(i - 1)];
      modifierDepart([v](DepartBT& z) { z.sectionMini = v; });
    });
    if (d.polarite == Polarite::TriphaseNeutre)
      coche(f, QStringLiteral("Neutre à demi-section permis"), d.neutreReduit, QStringLiteral("neutreReduitDepart"),
            [modifierDepart](bool v) { modifierDepart([v](DepartBT& z) { z.neutreReduit = v; }); }, QStringLiteral("au-delà de 16 mm², sans harmoniques"));
  }
  // ----- Protection -----
  {
    QFormLayout* f = groupe(QStringLiteral("Protection"), QStringLiteral("groupeProtection"));
    QStringList types;
    for (TypeProtection t : {TypeProtection::Auto, TypeProtection::Modulaire, TypeProtection::Reglable, TypeProtection::Fusible, TypeProtection::Moteur}) types << QString::fromUtf8(nomTypeProtection(t));
    choix(f, QStringLiteral("Appareil"), types, static_cast<int>(d.protection), QStringLiteral("protectionDepart"),
          [modifierDepart](int i) { modifierDepart([i](DepartBT& z) { z.protection = static_cast<TypeProtection>(i); }); });
    if (y.protection == TypeProtection::Modulaire)
      choix(f, QStringLiteral("Courbe"), {QStringLiteral("B (3 à 5 In)"), QStringLiteral("C (5 à 10 In)"), QStringLiteral("D (10 à 20 In)")}, d.courbe == "B" ? 0 : d.courbe == "D" ? 2 : 1,
            QStringLiteral("courbeDepart"), [modifierDepart](int i) { modifierDepart([i](DepartBT& z) { z.courbe = i == 0 ? "B" : i == 2 ? "D" : "C"; }); });
    reel(f, QStringLiteral("Calibre imposé"), d.calibreImpose, 0, 6300, 0, QStringLiteral(" A"), QStringLiteral("calibreDepart"),
         [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.calibreImpose = v; }); }, QStringLiteral("« auto » : le calibre normalisé juste au-dessus de Ib"), QStringLiteral("auto"));
    if (y.protection == TypeProtection::Reglable || y.protection == TypeProtection::Moteur)
      reel(f, QStringLiteral("Réglage Ir"), d.reglageIr, 0, 6300, 1, QStringLiteral(" A"), QStringLiteral("irDepart"), [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.reglageIr = v; }); },
           QStringLiteral("« auto » : au plus près du courant d’emploi"), QStringLiteral("auto"));
    if (y.protection == TypeProtection::Reglable) {
      reel(f, QStringLiteral("Seuil instantané Isd"), d.isd, 1.5, 15, 1, QStringLiteral(" × Ir"), QStringLiteral("isdDepart"), [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.isd = v; }); });
      coche(f, QStringLiteral("Court retard (sélectivité chronométrique)"), d.retarde, QStringLiteral("retardeDepart"),
            [modifierDepart](bool v) { modifierDepart([v](DepartBT& z) { z.retarde = v; }); });
    }
    reel(f, QStringLiteral("Pouvoir de coupure imposé"), d.pdcImpose, 0, 200, 1, QStringLiteral(" kA"), QStringLiteral("pdcDepart"),
         [modifierDepart](double v) { modifierDepart([v](DepartBT& z) { z.pdcImpose = v; }); }, QStringLiteral("« auto » : la gamme juste au-dessus de l’Icc au point de pose"), QStringLiteral("auto"));
    const std::vector<double> sensibilites = {0, 10, 30, 100, 300, 500, 1000, 3000};
    QStringList sens{QStringLiteral("aucun")};
    int courant = 0;
    for (std::size_t k = 1; k < sensibilites.size(); ++k) {
      sens << QStringLiteral("%1 mA").arg(nombre(sensibilites[k]));
      if (std::abs(sensibilites[k] - d.differentiel) < 1e-9) courant = static_cast<int>(k);
    }
    choix(f, QStringLiteral("Différentiel"), sens, courant, QStringLiteral("differentielDepart"), [modifierDepart, sensibilites](int i) {
      const double v = sensibilites[static_cast<std::size_t>(i)];
      modifierDepart([v](DepartBT& z) { z.differentiel = v; });
    }, QStringLiteral("30 mA : personnes (prises, salles d’eau, bornes) ; 300 à 500 mA : incendie et contacts indirects en TT"));
    if (d.differentiel > 0) {
      const QStringList typesDiff = {QStringLiteral("AC"), QStringLiteral("A"), QStringLiteral("F"), QStringLiteral("B")};
      choix(f, QStringLiteral("Type"), typesDiff, std::max(0, static_cast<int>(typesDiff.indexOf(q(d.typeDifferentiel)))), QStringLiteral("typeDifferentielDepart"),
            [modifierDepart, typesDiff](int i) {
              const std::string v = s(typesDiff[i]);
              modifierDepart([v](DepartBT& z) { z.typeDifferentiel = v; });
            });
      coche(f, QStringLiteral("Sélectif (type S, retardé)"), d.differentielSelectif, QStringLiteral("selectifDepart"),
            [modifierDepart](bool v) { modifierDepart([v](DepartBT& z) { z.differentielSelectif = v; }); });
    }
  }
  // ----- Actions -----
  {
    auto* g = new QGroupBox(QStringLiteral("Actions"));
    auto* v = new QVBoxLayout(g);
    if (tableau) {
      auto* ajout = new QPushButton(icone(QStringLiteral("disjoncteur")), QStringLiteral("Ajouter un départ à ce tableau"));
      ajout->setObjectName(QStringLiteral("ajouterDepartIci"));
      auto* menu = new QMenu(ajout);
      for (TypeCharge c : {TypeCharge::Eclairage, TypeCharge::Prises, TypeCharge::Chauffage, TypeCharge::Moteur, TypeCharge::IRVE, TypeCharge::Condensateurs, TypeCharge::Autre, TypeCharge::Tableau})
        menu->addAction(icone(iconeCharge(c)), QString::fromUtf8(nomTypeCharge(c)), this, [this, c] { ajouterDepart(c); });
      ajout->setMenu(menu);
      v->addWidget(ajout);
    }
    auto* sup = new QPushButton(icone(QStringLiteral("supprimer")), tableau ? QStringLiteral("Supprimer ce tableau et ses départs") : QStringLiteral("Supprimer ce départ"));
    sup->setObjectName(QStringLiteral("supprimerDepart"));
    connect(sup, &QPushButton::clicked, this, [this] { supprimerDepartChoisi(); });
    v->addWidget(sup);
    col->addWidget(g);
  }
}

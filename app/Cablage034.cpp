// Prométhée : faisceau de câblage dans l'atelier électrique (lot L19 de la 0.34 ; 19.10 à 19.13). Voir Cablage034.hpp.
// Licence GPL-3.0-only.
#include "Cablage034.hpp"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "AtelierElectrique.hpp"
#include "Document.hpp"
#include "VueFaisceau.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"

using namespace prom::elec;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& t) { return t.toStdString(); }
QString nb(double v, int d = 2) { return QLocale(QLocale::French).toString(v, 'f', d); }
bool ecrire(QWidget* w, const QString& chemin, const std::string& contenu) {
  try {
    prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), contenu);
    return true;
  } catch (const std::exception& e) {
    if (w) QMessageBox::warning(w, QStringLiteral("Écriture"), QString::fromUtf8(e.what()));
    return false;
  }
}
QPushButton* bouton(const QString& texte, const QString& nom, QWidget* parent, const std::function<void()>& f) {
  auto* b = new QPushButton(texte, parent);
  b->setObjectName(nom);
  QObject::connect(b, &QPushButton::clicked, parent, f);
  return b;
}
}  // namespace

FenetreCablage::FenetreCablage(AtelierElectrique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreCablage"));
  setWindowTitle(QStringLiteral("Faisceau de câblage"));
  resize(1100, 760);
  m_bibliotheque = bibliothequeCablageIntegree();
  // Bibliothèque de l'utilisateur (fichier de données), si elle a été choisie
  if (const QString f = QSettings().value(QStringLiteral("cablage/bibliotheque")).toString(); !f.isEmpty()) chargerBibliotheque(f, false);
  auto* l = new QVBoxLayout(this);
  auto* haut = new QHBoxLayout;
  m_choix = new QComboBox;
  m_choix->setObjectName(QStringLiteral("choixFaisceau"));
  connect(m_choix, &QComboBox::activated, this, [this](int i) { choisirFaisceau(i); });
  m_nom = new QLineEdit;
  m_nom->setObjectName(QStringLiteral("nomFaisceau"));
  connect(m_nom, &QLineEdit::editingFinished, this, [this] {
    if (!m_nom->text().trimmed().isEmpty()) modifier([&](Cablage& h, ProjetElectrique&) { h.nom = s(m_nom->text().trimmed()); }, QStringLiteral("Nom du faisceau"));
  });
  haut->addWidget(new QLabel(QStringLiteral("Faisceau")));
  haut->addWidget(m_choix, 1);
  haut->addWidget(m_nom, 1);
  haut->addWidget(bouton(QStringLiteral("Nouveau…"), QStringLiteral("nouveauCablage"), this, [this] {
    bool ok = false;
    const QString n = QInputDialog::getText(this, QStringLiteral("Nouveau faisceau"), QStringLiteral("Nom :"), QLineEdit::Normal, QStringLiteral("Faisceau"), &ok);
    if (ok && !n.trimmed().isEmpty()) nouveauFaisceau(n.trimmed());
  }));
  haut->addWidget(bouton(QStringLiteral("Bibliothèque…"), QStringLiteral("bibliothequeCablage"), this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Bibliothèque de câbles"), {}, QStringLiteral("Câbles (*.json)"));
    if (f.isEmpty()) return;
    QStringList migrations;
    if (chargerBibliotheque(f, true, &migrations)) QSettings().setValue(QStringLiteral("cablage/bibliotheque"), f);
  }));
  l->addLayout(haut);
  m_onglets = new QTabWidget;
  l->addWidget(m_onglets, 1);
  for (const auto& [id, titre] : std::vector<std::pair<QString, QString>>{{QStringLiteral("cablage"), QStringLiteral("Câblage")},
                                                                         {QStringLiteral("planche"), QStringLiteral("Planche")},
                                                                         {QStringLiteral("longueurs"), QStringLiteral("Longueurs et circuits")},
                                                                         {QStringLiteral("fabrication"), QStringLiteral("Fabrication")}}) {
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("onglet-") + id);
    if (id == QStringLiteral("cablage")) construireCablage(page);
    else if (id == QStringLiteral("planche")) construirePlanche(page);
    else if (id == QStringLiteral("longueurs")) construireLongueurs(page);
    else construireFabrication(page);
    m_onglets->addTab(page, titre);
  }
  m_etat = new QLabel;
  m_etat->setObjectName(QStringLiteral("etatCablage"));
  m_etat->setWordWrap(true);
  l->addWidget(m_etat);
  connect(a.document(), &Document::change, this, [this] {
    if (isVisible()) recharger();
  });
  recharger();
}

bool FenetreCablage::montrer(const QString& onglet) {
  for (int i = 0; i < m_onglets->count(); ++i)
    if (m_onglets->widget(i)->objectName() == QStringLiteral("onglet-") + onglet) {
      m_onglets->setCurrentIndex(i);
      return true;
    }
  return false;
}

std::vector<Cablage> FenetreCablage::faisceaux() const {
  const ProjetElectrique* pe = m_atelier.projetCourant();
  return pe ? cablages(*pe) : std::vector<Cablage>{};
}

std::optional<Cablage> FenetreCablage::courant() const {
  const auto v = faisceaux();
  if (m_indice < 0 || m_indice >= static_cast<int>(v.size())) return std::nullopt;
  return v[static_cast<std::size_t>(m_indice)];
}

void FenetreCablage::choisirFaisceau(int i) {
  m_indice = i;
  recharger();
}

int FenetreCablage::nouveauFaisceau(const QString& nom) {
  int i = -1;
  m_atelier.modifierProjetNomme(
      [&](ProjetElectrique& pe) {
        auto v = cablages(pe);
        Cablage h;
        h.uuid = prom::ecad::nouvelUuid();
        h.nom = s(nom);
        v.push_back(h);
        i = static_cast<int>(v.size()) - 1;
        ecrireCablages(pe, v);
      },
      QStringLiteral("Nouveau faisceau"));
  m_indice = i;
  recharger();
  return i;
}

bool FenetreCablage::modifier(const std::function<void(Cablage&, ProjetElectrique&)>& f, const QString& nom) {
  bool fait = false;
  const int i = m_indice;
  m_atelier.modifierProjetNomme(
      [&](ProjetElectrique& pe) {
        auto v = cablages(pe);
        if (i < 0 || i >= static_cast<int>(v.size())) return;
        f(v[static_cast<std::size_t>(i)], pe);
        ecrireCablages(pe, v);
        fait = true;
      },
      nom);
  recharger();
  return fait;
}

bool FenetreCablage::chargerBibliotheque(const QString& fichier, bool appliquer, QStringList* migrations) {
  std::string erreur;
  std::vector<std::string> m;
  std::optional<BibliothequeCablage> b;
  try {
    b = lireBibliothequeCablage(prom::lireFichier(prom::cheminFichier(s(fichier))), &m, &erreur);
  } catch (const std::exception& e) {
    erreur = e.what();
  }
  if (!b) {
    if (m_etat) m_etat->setText(QStringLiteral("Bibliothèque de câbles illisible : ") + q(erreur).toHtmlEscaped());
    return false;
  }
  // Les entrées du fichier complètent (ou remplacent, à désignation égale) la bibliothèque intégrée
  BibliothequeCablage r = bibliothequeCablageIntegree();
  for (const auto& c : b->cables) {
    const auto it = std::find_if(r.cables.begin(), r.cables.end(), [&](const EntreeCable& x) { return x.designation == c.designation; });
    if (it != r.cables.end()) *it = c;
    else r.cables.push_back(c);
  }
  for (const auto& c : b->contacts) r.contacts.push_back(c);
  for (const auto& c : b->connecteurs) r.connecteurs.push_back(c);
  for (const auto& c : b->protections) r.protections.push_back(c);
  m_bibliotheque = r;
  if (migrations)
    for (const auto& x : m) *migrations << q(x);
  if (appliquer) {
    int n = 0;
    modifier([&](Cablage& h, ProjetElectrique&) { n = mettreAJourCables(h, m_bibliotheque); }, QStringLiteral("Câbles mis à jour d’après la bibliothèque"));
    if (m_etat) m_etat->setText(QStringLiteral("%1 fil(s) mis à jour d’après la bibliothèque%2").arg(n).arg(m.empty() ? QString() : QStringLiteral(" (format migré)")));
  }
  majListes();
  return true;
}

// ======================================================================================================================
// Câblage
// ======================================================================================================================
void FenetreCablage::construireCablage(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  auto* ligne = new QHBoxLayout;
  m_connecteurs = new QTableWidget(0, 4);
  m_connecteurs->setObjectName(QStringLiteral("tableConnecteurs"));
  m_connecteurs->setHorizontalHeaderLabels({QStringLiteral("Repère"), QStringLiteral("Référence"), QStringLiteral("Genre"), QStringLiteral("Cavités")});
  m_connecteurs->horizontalHeader()->setStretchLastSection(true);
  m_connecteurs->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_connecteurs->setMaximumHeight(170);
  ligne->addWidget(m_connecteurs, 2);
  auto* ajout = new QVBoxLayout;
  ajout->addWidget(bouton(QStringLiteral("Connecteur de carte…"), QStringLiteral("connecteurCarte"), page, [this] {
    const auto& cartes = m_atelier.document()->projet().electronique;
    if (cartes.empty()) {
      m_etat->setText(QStringLiteral("Le projet n’a pas de carte électronique."));
      return;
    }
    QStringList noms;
    for (const auto& c : cartes) noms << q(c.nom);
    bool ok = false;
    const QString carte = QInputDialog::getItem(this, QStringLiteral("Connecteur de carte"), QStringLiteral("Carte :"), noms, 0, false, &ok);
    if (!ok) return;
    const int ic = static_cast<int>(noms.indexOf(carte));
    QStringList reperes;
    for (const auto& k : cartes[static_cast<std::size_t>(ic)].composants)
      if (!k.reference.empty() && (k.reference[0] == 'J' || k.reference[0] == 'P' || k.reference[0] == 'X' || k.symbole.rfind("Conn_", 0) == 0)) reperes << q(k.reference);
    const QString r = QInputDialog::getItem(this, QStringLiteral("Connecteur de carte"), QStringLiteral("Connecteur :"), reperes, 0, true, &ok);
    if (ok && !r.isEmpty()) ajouterConnecteurCarte(ic, r);
  }));
  ajout->addWidget(bouton(QStringLiteral("Bornier ou appareil du schéma…"), QStringLiteral("connecteurSchema"), page, [this] {
    bool ok = false;
    const QString r = QInputDialog::getText(this, QStringLiteral("Bornier"), QStringLiteral("Repère (« X1 ») :"), QLineEdit::Normal, QStringLiteral("X1"), &ok);
    if (ok && !r.trimmed().isEmpty()) ajouterConnecteurSchema(r.trimmed());
  }));
  ajout->addWidget(bouton(QStringLiteral("Connecteur volant…"), QStringLiteral("connecteurVolant"), page, [this] {
    QStringList refs = {QString()};
    for (const auto& c : m_bibliotheque.connecteurs) refs << q(c.reference);
    bool ok = false;
    const QString r = QInputDialog::getText(this, QStringLiteral("Connecteur volant"), QStringLiteral("Repère :"), QLineEdit::Normal, QStringLiteral("P1"), &ok);
    if (!ok || r.trimmed().isEmpty()) return;
    const QString ref = QInputDialog::getItem(this, QStringLiteral("Connecteur volant"), QStringLiteral("Boîtier de la bibliothèque :"), refs, 0, false, &ok);
    if (!ok) return;
    const EntreeConnecteurCablage* e = m_bibliotheque.connecteur(s(ref));
    const int n = e ? e->cavites : QInputDialog::getInt(this, QStringLiteral("Connecteur volant"), QStringLiteral("Cavités :"), 4, 1, 200, 1, &ok);
    if (ok) ajouterConnecteurVolant(r.trimmed(), ref, n);
  }));
  ajout->addStretch(1);
  ligne->addLayout(ajout, 1);
  l->addLayout(ligne);
  auto* pose = new QHBoxLayout;
  m_designation = new QComboBox;
  m_designation->setObjectName(QStringLiteral("designationCable"));
  m_de = new QComboBox;
  m_de->setObjectName(QStringLiteral("deCable"));
  m_vers = new QComboBox;
  m_vers->setObjectName(QStringLiteral("versCable"));
  pose->addWidget(new QLabel(QStringLiteral("Câble")));
  pose->addWidget(m_designation, 2);
  pose->addWidget(new QLabel(QStringLiteral("de")));
  pose->addWidget(m_de, 1);
  pose->addWidget(new QLabel(QStringLiteral("vers")));
  pose->addWidget(m_vers, 1);
  pose->addWidget(bouton(QStringLiteral("Poser le câble"), QStringLiteral("poserCable"), page, [this] { poserCable(m_designation->currentText(), m_de->currentText(), m_vers->currentText()); }));
  pose->addWidget(bouton(QStringLiteral("Fil seul…"), QStringLiteral("poserFil"), page, [this] {
    bool ok = false;
    const QString t = QInputDialog::getText(this, QStringLiteral("Fil seul"), QStringLiteral("De « J3:1 » vers « X1:2 » (ou « fil 3 » pour un piquage), section, couleur :"), QLineEdit::Normal,
                                            QStringLiteral("J3:1 X1:2 0,5 BU"), &ok);
    if (!ok) return;
    const QStringList v = t.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (v.size() < 4) return;
    const double section = QLocale(QLocale::French).toDouble(v[v.size() - 2]);
    if (v[1] == QStringLiteral("fil") && v.size() >= 5)
      poserFil(section, v.back(), v[0].section(QLatin1Char(':'), 0, 0), v[0].section(QLatin1Char(':'), 1), {}, {}, v[2]);
    else
      poserFil(section, v.back(), v[0].section(QLatin1Char(':'), 0, 0), v[0].section(QLatin1Char(':'), 1), v[1].section(QLatin1Char(':'), 0, 0), v[1].section(QLatin1Char(':'), 1));
  }));
  pose->addWidget(bouton(QStringLiteral("Cavalier…"), QStringLiteral("poserCavalier"), page, [this] {
    bool ok = false;
    const QString t = QInputDialog::getText(this, QStringLiteral("Cavalier"), QStringLiteral("Connecteur, deux cavités, section, longueur (m) :"), QLineEdit::Normal, QStringLiteral("X1 3 4 0,5 0,08"), &ok);
    const QStringList v = t.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (ok && v.size() == 5) poserCavalier(v[0], v[1], v[2], QLocale(QLocale::French).toDouble(v[3]), QLocale(QLocale::French).toDouble(v[4]));
  }));
  l->addLayout(pose);
  auto* actions = new QHBoxLayout;
  actions->addWidget(bouton(QStringLiteral("Contacts d’après les sections"), QStringLiteral("affecterContacts"), page, [this] { affecterContacts(); }));
  actions->addWidget(bouton(QStringLiteral("Épissures"), QStringLiteral("poserEpissures"), page, [this] { poserEpissures(); }));
  actions->addWidget(bouton(QStringLiteral("Folio de câblage"), QStringLiteral("folioCablage"), page, [this] { ajouterFolio(); }));
  actions->addStretch(1);
  l->addLayout(actions);
  m_fils = new QTableWidget(0, 11);
  m_fils->setObjectName(QStringLiteral("tableFils"));
  m_fils->setHorizontalHeaderLabels({QStringLiteral("Fil"), QStringLiteral("De"), QStringLiteral("Vers"), QStringLiteral("mm²"), QStringLiteral("Couleur"), QStringLiteral("Câble"),
                                     QStringLiteral("Paire"), QStringLiteral("Réseau"), QStringLiteral("Longueur (m)"), QStringLiteral("Provenance"), QStringLiteral("Contacts")});
  m_fils->horizontalHeader()->setStretchLastSection(true);
  m_fils->setEditTriggers(QAbstractItemView::NoEditTriggers);
  l->addWidget(m_fils, 1);
}

void FenetreCablage::majListes() {
  if (!m_designation) return;
  const QString d = m_designation->currentText();
  m_designation->clear();
  for (const auto& c : m_bibliotheque.cables) m_designation->addItem(q(c.designation));
  if (!d.isEmpty()) m_designation->setCurrentText(d);
  if (m_protection) {
    const QString p = m_protection->currentText();
    m_protection->clear();
    for (const auto& x : m_bibliotheque.protections) m_protection->addItem(q(x.reference));
    if (!p.isEmpty()) m_protection->setCurrentText(p);
  }
}

void FenetreCablage::recharger() {
  const auto v = faisceaux();
  {
    const QSignalBlocker b(m_choix);
    m_choix->clear();
    for (const auto& h : v) m_choix->addItem(q(h.nom));
    if (m_indice >= static_cast<int>(v.size())) m_indice = static_cast<int>(v.size()) - 1;
    if (m_indice < 0 && !v.empty()) m_indice = 0;
    m_choix->setCurrentIndex(m_indice);
  }
  majListes();
  const auto h = courant();
  m_nom->setText(h ? q(h->nom) : QString());
  for (QComboBox* c : {m_de, m_vers}) {
    const QString t = c->currentText();
    c->clear();
    if (h)
      for (const auto& k : h->connecteurs) c->addItem(q(k.repere));
    c->setCurrentText(t);
  }
  if (m_vers->count() > 1 && m_vers->currentIndex() == m_de->currentIndex()) m_vers->setCurrentIndex(1);
  m_connecteurs->setRowCount(h ? static_cast<int>(h->connecteurs.size()) : 0);
  if (h)
    for (int r = 0; r < static_cast<int>(h->connecteurs.size()); ++r) {
      const auto& k = h->connecteurs[static_cast<std::size_t>(r)];
      const QStringList cells = {q(k.repere), q(k.reference), k.carte ? QStringLiteral("carte") : k.composant.empty() ? QStringLiteral("volant") : QStringLiteral("schéma"),
                                 QString::number(k.cavites.size())};
      for (int c = 0; c < 4; ++c) m_connecteurs->setItem(r, c, new QTableWidgetItem(cells[c]));
    }
  const auto fils = h ? listeFils(*h, m_bibliotheque) : std::vector<LigneFil>{};
  m_fils->setRowCount(static_cast<int>(fils.size()));
  for (int r = 0; r < static_cast<int>(fils.size()); ++r) {
    const auto& f = fils[static_cast<std::size_t>(r)];
    const QStringList cells = {q(f.numero), q(f.de), q(f.vers), nb(f.section), q(f.couleur) + (f.drain ? QStringLiteral(" (drain)") : QString()), q(f.cable), f.paire ? QString::number(f.paire) : QString(),
                               q(f.reseau), f.longueur > 0 ? nb(f.longueur, 3) : QStringLiteral("à mesurer"), q(f.provenance), q(f.contactDe) + QStringLiteral(" / ") + q(f.contactVers)};
    for (int c = 0; c < cells.size(); ++c) m_fils->setItem(r, c, new QTableWidgetItem(cells[c]));
  }
  m_fils->resizeColumnsToContents();
  // Planche : réglages et tronçons
  if (m_foisonnement && h) {
    const QSignalBlocker b1(m_foisonnement), b2(m_surlongueur);
    m_foisonnement->setValue(h->foisonnement);
    m_surlongueur->setValue(h->surlongueur);
  }
  if (m_troncons) {
    const auto occ = h ? occupationTroncons(*h, m_bibliotheque) : std::vector<OccupationTroncon>{};
    const QSignalBlocker b(m_troncons);
    m_troncons->setRowCount(h ? static_cast<int>(h->troncons.size()) : 0);
    m_tronconProtection->clear();
    for (int r = 0; h && r < static_cast<int>(h->troncons.size()); ++r) {
      const auto& t = h->troncons[static_cast<std::size_t>(r)];
      QString a = q(t.a), b2 = q(t.b);
      for (const auto& n : h->noeuds) {
        if (n.uuid == t.a) a = q(n.nom);
        if (n.uuid == t.b) b2 = q(n.nom);
      }
      const double d = r < static_cast<int>(occ.size()) ? occ[static_cast<std::size_t>(r)].diametre : 0;
      const QStringList cells = {a + QStringLiteral(" – ") + b2, nb(longueurTroncon(*h, t), 0), t.coupure ? QStringLiteral("oui") : QString(), nb(d, 1),
                                 r < static_cast<int>(occ.size()) ? QString::number(occ[static_cast<std::size_t>(r)].fils.size()) : QString()};
      for (int c = 0; c < cells.size(); ++c) {
        auto* it = new QTableWidgetItem(cells[c]);
        if (c != 1 && c != 2) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        m_troncons->setItem(r, c, it);
      }
      m_tronconProtection->addItem(a + QStringLiteral(" – ") + b2);
    }
  }
  if (m_cable3D) {
    const QString t = m_cable3D->currentText();
    m_cable3D->clear();
    if (h)
      for (const auto& w : h->cables) m_cable3D->addItem(q(w.reference));
    m_cable3D->setCurrentText(t);
  }
}

bool FenetreCablage::ajouterConnecteurCarte(int conception, const QString& repere, const QString& reference) {
  const auto& cartes = m_atelier.document()->projet().electronique;
  if (conception < 0 || conception >= static_cast<int>(cartes.size())) return false;
  auto k = connecteurDeCarte(cartes[static_cast<std::size_t>(conception)], conception, s(repere));
  if (!k) {
    m_etat->setText(QStringLiteral("%1 introuvable sur la carte").arg(repere));
    return false;
  }
  if (!reference.isEmpty()) k->reference = s(reference);
  return modifier(
      [&](Cablage& h, ProjetElectrique&) {
        for (auto& x : h.connecteurs)
          if (x.repere == k->repere) {
            x.cavites = k->cavites;  // réseaux relus de la carte
            x.carte = true;
            return;
          }
        h.connecteurs.push_back(*k);
      },
      QStringLiteral("Connecteur de carte %1").arg(repere));
}

bool FenetreCablage::ajouterConnecteurSchema(const QString& repere, int cavites) {
  const ProjetElectrique* pe = m_atelier.projetCourant();
  if (!pe) return false;
  const ConnecteurCablage k = connecteurDuSchema(*pe, s(repere), cavites);
  if (k.cavites.empty()) {
    m_etat->setText(QStringLiteral("%1 : aucune borne au schéma").arg(repere));
    return false;
  }
  return modifier(
      [&](Cablage& h, ProjetElectrique&) {
        if (!h.connecteur(k.repere)) h.connecteurs.push_back(k);
      },
      QStringLiteral("Bornier %1").arg(repere));
}

bool FenetreCablage::ajouterConnecteurVolant(const QString& repere, const QString& reference, int cavites) {
  ConnecteurCablage k;
  k.uuid = prom::ecad::nouvelUuid();
  k.repere = s(repere);
  k.reference = s(reference);
  if (const EntreeConnecteurCablage* e = m_bibliotheque.connecteur(k.reference)) k.etanche = e->etanche;
  for (int i = 0; i < cavites; ++i) k.cavites.push_back({std::to_string(i + 1), {}, {}, {}, {}, 0, 0});
  return modifier(
      [&](Cablage& h, ProjetElectrique&) {
        if (!h.connecteur(k.repere)) h.connecteurs.push_back(k);
      },
      QStringLiteral("Connecteur %1").arg(repere));
}

QString FenetreCablage::poserCable(const QString& designation, const QString& de, const QString& vers) {
  std::string w;
  modifier([&](Cablage& h, ProjetElectrique&) { w = prom::elec::poserCable(h, m_bibliotheque, s(designation), s(de), s(vers)); }, QStringLiteral("Câble %1").arg(designation));
  m_etat->setText(w.empty() ? QStringLiteral("Câble non posé : entrée ou connecteurs introuvables") : QStringLiteral("Câble %1 posé de %2 à %3").arg(q(w), de, vers));
  return q(w);
}

QString FenetreCablage::poserFil(double section, const QString& couleur, const QString& de, const QString& deCavite, const QString& vers, const QString& versCavite, const QString& surFil) {
  std::string n;
  modifier([&](Cablage& h, ProjetElectrique&) { n = prom::elec::poserFil(h, section, s(couleur), s(de), s(deCavite), s(vers), s(versCavite), s(surFil)); }, QStringLiteral("Fil"));
  return q(n);
}

QString FenetreCablage::poserCavalier(const QString& connecteur, const QString& a, const QString& b, double section, double longueur) {
  std::string n;
  modifier([&](Cablage& h, ProjetElectrique&) { n = prom::elec::poserCavalier(h, s(connecteur), s(a), s(b), section, longueur); }, QStringLiteral("Cavalier"));
  return q(n);
}

int FenetreCablage::affecterContacts() {
  int n = 0;
  modifier([&](Cablage& h, ProjetElectrique&) { n = prom::elec::affecterContacts(h, m_bibliotheque); }, QStringLiteral("Contacts d’après les sections"));
  m_etat->setText(QStringLiteral("%1 cavité(s) servie(s) : contact accepté par la section, dénudage et recul ; joints et bouchons des connecteurs étanches").arg(n));
  return n;
}

int FenetreCablage::poserEpissures() {
  int n = 0;
  modifier([&](Cablage& h, ProjetElectrique&) { n = prom::elec::poserEpissures(h); }, QStringLiteral("Épissures"));
  return n;
}

int FenetreCablage::ajouterFolio() {
  const auto h = courant();
  if (!h) return -1;
  int i = -1;
  m_atelier.modifierProjetNomme(
      [&](ProjetElectrique& pe) {
        i = static_cast<int>(prom::elec::ajouterFolio(pe, TypeFolio::Cablage, "Câblage " + h->nom));
        pe.folios[pe.schema.feuilles[static_cast<std::size_t>(i)].uuid].faisceau = h->uuid;
      },
      QStringLiteral("Folio de câblage"));
  if (i >= 0) {
    m_atelier.choisirMode(AtelierElectrique::Schema);
    m_atelier.allerAuFolio(i);
  }
  return i;
}

// ======================================================================================================================
// Planche
// ======================================================================================================================
void FenetreCablage::construirePlanche(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  auto* reglages = new QHBoxLayout;
  m_foisonnement = new QDoubleSpinBox;
  m_foisonnement->setObjectName(QStringLiteral("foisonnement"));
  m_foisonnement->setRange(0.5, 3);
  m_foisonnement->setSingleStep(0.05);
  m_surlongueur = new QDoubleSpinBox;
  m_surlongueur->setObjectName(QStringLiteral("surlongueurCablage"));
  m_surlongueur->setRange(0, 1000);
  m_surlongueur->setSuffix(QStringLiteral(" mm"));
  reglages->addWidget(new QLabel(QStringLiteral("Foisonnement")));
  reglages->addWidget(m_foisonnement);
  reglages->addWidget(new QLabel(QStringLiteral("Surlongueur par bout")));
  reglages->addWidget(m_surlongueur);
  reglages->addWidget(bouton(QStringLiteral("Appliquer"), QStringLiteral("appliquerReglagesPlanche"), page, [this] { regler(m_foisonnement->value(), m_surlongueur->value()); }));
  reglages->addStretch(1);
  l->addLayout(reglages);
  auto* actions = new QHBoxLayout;
  actions->addWidget(bouton(QStringLiteral("Topologie par défaut"), QStringLiteral("topologieDefaut"), page, [this] {
    modifier([](Cablage& h, ProjetElectrique&) { topologieParDefaut(h); }, QStringLiteral("Topologie du faisceau"));
  }));
  actions->addWidget(bouton(QStringLiteral("Dessiner la planche"), QStringLiteral("dessinerPlanche"), page, [this] { dessinerPlanche(); }));
  actions->addWidget(bouton(QStringLiteral("Reprendre la planche"), QStringLiteral("reprendrePlanche"), page, [this] { reprendrePlanche(); }));
  actions->addWidget(bouton(QStringLiteral("Longueurs depuis la planche"), QStringLiteral("longueursPlanche"), page, [this] {
    modifier([](Cablage& h, ProjetElectrique&) { longueursDepuisPlanche(h); }, QStringLiteral("Longueurs depuis la planche"));
  }));
  actions->addStretch(1);
  l->addLayout(actions);
  m_troncons = new QTableWidget(0, 5);
  m_troncons->setObjectName(QStringLiteral("tableTroncons"));
  m_troncons->setHorizontalHeaderLabels({QStringLiteral("Tronçon"), QStringLiteral("Longueur (mm)"), QStringLiteral("Coupure"), QStringLiteral("Ø faisceau (mm)"), QStringLiteral("Fils")});
  m_troncons->horizontalHeader()->setStretchLastSection(true);
  connect(m_troncons, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
    const int r = it->row();
    const double l2 = QLocale(QLocale::French).toDouble(m_troncons->item(r, 1)->text());
    const bool coupure = m_troncons->item(r, 2) && !m_troncons->item(r, 2)->text().trimmed().isEmpty();
    reglerTroncon(r, l2, coupure);
  });
  l->addWidget(m_troncons, 1);
  auto* prot = new QHBoxLayout;
  m_tronconProtection = new QComboBox;
  m_tronconProtection->setObjectName(QStringLiteral("tronconProtection"));
  m_protection = new QComboBox;
  m_protection->setObjectName(QStringLiteral("referenceProtection"));
  auto* texte = new QLineEdit;
  texte->setPlaceholderText(QStringLiteral("texte de l’étiquette (lignes séparées par « / »)"));
  prot->addWidget(new QLabel(QStringLiteral("Protection")));
  prot->addWidget(m_tronconProtection, 1);
  prot->addWidget(m_protection, 1);
  prot->addWidget(texte, 1);
  prot->addWidget(bouton(QStringLiteral("Poser"), QStringLiteral("poserProtection"), page, [this, texte] {
    poserProtection(m_tronconProtection->currentIndex(), m_protection->currentText(), 0, 0, texte->text().replace(QStringLiteral(" / "), QStringLiteral("\n")));
  }));
  l->addLayout(prot);
  auto* sorties = new QHBoxLayout;
  sorties->addWidget(bouton(QStringLiteral("Planche à l’échelle 1 (SVG)…"), QStringLiteral("exporterPlanche"), page, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Planche"), QStringLiteral("planche.svg"), QStringLiteral("SVG (*.svg)"));
    if (!f.isEmpty()) exporterPlanche(f, 1);
  }));
  sorties->addWidget(bouton(QStringLiteral("Pages assemblées (PDF)…"), QStringLiteral("exporterPages"), page, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Planche en pages"), QStringLiteral("planche.pdf"), QStringLiteral("PDF (*.pdf)"));
    if (f.isEmpty()) return;
    bool ok = false;
    const QString format = QInputDialog::getItem(this, QStringLiteral("Format"), QStringLiteral("Pages :"), {QStringLiteral("A4"), QStringLiteral("A3"), QStringLiteral("A2")}, 0, false, &ok);
    if (ok) exporterPages(f, format);
  }));
  sorties->addWidget(bouton(QStringLiteral("Coupe du tronçon (SVG)…"), QStringLiteral("exporterCoupe"), page, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Coupe"), QStringLiteral("coupe.svg"), QStringLiteral("SVG (*.svg)"));
    if (!f.isEmpty()) exporterCoupe(std::max(0, m_troncons->currentRow()), f);
  }));
  sorties->addStretch(1);
  l->addLayout(sorties);
}

bool FenetreCablage::regler(double foisonnement, double surlongueur) {
  return modifier(
      [&](Cablage& h, ProjetElectrique&) {
        h.foisonnement = foisonnement;
        h.surlongueur = surlongueur;
        longueursDepuisPlanche(h);
      },
      QStringLiteral("Réglages de la planche"));
}

bool FenetreCablage::dessinerPlanche() {
  std::string uuid;
  const bool fait = modifier(
      [&](Cablage& h, ProjetElectrique& pe) {
        if (h.troncons.empty()) topologieParDefaut(h);
        prom::elec::dessinerPlanche(h, m_bibliotheque, true);
        longueursDepuisPlanche(h);
        // La planche de l'atelier (mode Faisceau) : même UUID, remplacée si elle existe
        const Faisceau f = planchePourAtelier(h, pe);
        uuid = f.uuid;
        auto it = std::find_if(pe.faisceaux.begin(), pe.faisceaux.end(), [&](const Faisceau& x) { return x.uuid == f.uuid; });
        if (it != pe.faisceaux.end()) *it = f;
        else pe.faisceaux.push_back(f);
      },
      QStringLiteral("Planche du faisceau"));
  if (!fait) return false;
  // La planche se montre dans l'atelier : les nœuds s'y rangent à la souris
  const ProjetElectrique* pe = m_atelier.projetCourant();
  for (std::size_t i = 0; pe && i < pe->faisceaux.size(); ++i)
    if (pe->faisceaux[i].uuid == uuid) {
      m_atelier.choisirMode(AtelierElectrique::Faisceau);
      m_atelier.vueFaisceau()->setFaisceau(m_atelier.projet(), static_cast<int>(i));
      m_atelier.vueFaisceau()->recadrer();
    }
  m_etat->setText(QStringLiteral("Planche dessinée : le tronc à l’horizontale, les branches de part et d’autre ; range-les à la souris dans l’atelier, puis « Reprendre la planche »."));
  return true;
}

int FenetreCablage::reprendrePlanche() {
  int n = 0;
  modifier(
      [&](Cablage& h, ProjetElectrique& pe) {
        for (const auto& f : pe.faisceaux)
          if (f.uuid == h.uuid) n = prom::elec::reprendrePlanche(h, f);
        longueursDepuisPlanche(h);
      },
      QStringLiteral("Planche reprise"));
  m_etat->setText(QStringLiteral("%1 nœud(s) ou tronçon(s) repris de la planche ; longueurs des fils recalculées").arg(n));
  return n;
}

bool FenetreCablage::reglerTroncon(int i, double longueur, bool coupure) {
  return modifier(
      [&](Cablage& h, ProjetElectrique&) {
        if (i < 0 || i >= static_cast<int>(h.troncons.size())) return;
        h.troncons[static_cast<std::size_t>(i)].longueur = longueur;
        h.troncons[static_cast<std::size_t>(i)].coupure = coupure;
        longueursDepuisPlanche(h);
      },
      QStringLiteral("Tronçon"));
}

bool FenetreCablage::poserProtection(int troncon, const QString& reference, double debut, double fin, const QString& texte) {
  return modifier(
      [&](Cablage& h, ProjetElectrique&) {
        if (troncon < 0 || troncon >= static_cast<int>(h.troncons.size())) return;
        // Protections superposées : chacune décalée de l'épaisseur de celles déjà posées
        double decalage = 0;
        for (const auto& p : h.protections)
          if (p.troncon == h.troncons[static_cast<std::size_t>(troncon)].uuid)
            if (const EntreeProtection* e = m_bibliotheque.protection(p.reference)) decalage += e->epaisseur;
        h.protections.push_back({h.troncons[static_cast<std::size_t>(troncon)].uuid, s(reference), debut, fin, decalage, s(texte)});
      },
      QStringLiteral("Protection %1").arg(reference));
}

bool FenetreCablage::exporterPlanche(const QString& chemin, double echelle) {
  auto h = courant();
  if (!h) return false;
  const PlancheDessinee d = prom::elec::dessinerPlanche(*h, m_bibliotheque, true);
  return ecrire(this, chemin, svgPlanche(*h, m_bibliotheque, d, echelle));
}

int FenetreCablage::exporterPages(const QString& chemin, const QString& format) {
  auto h = courant();
  if (!h) return 0;
  const PlancheDessinee d = prom::elec::dessinerPlanche(*h, m_bibliotheque, true);
  const PagesPlanche p = pagesPlanche(*h, m_bibliotheque, d, s(format), 10);
  return ecrire(this, chemin, p.pdf) ? p.colonnes * p.lignes : 0;
}

bool FenetreCablage::exporterCoupe(int troncon, const QString& chemin) {
  const auto h = courant();
  if (!h || troncon < 0 || troncon >= static_cast<int>(h->troncons.size())) return false;
  return ecrire(this, chemin, svgCoupe(*h, m_bibliotheque, h->troncons[static_cast<std::size_t>(troncon)].uuid));
}

// ======================================================================================================================
// Longueurs et circuits
// ======================================================================================================================
void FenetreCablage::construireLongueurs(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  auto* f = new QFormLayout;
  m_cable3D = new QComboBox;
  m_cable3D->setObjectName(QStringLiteral("cable3D"));
  m_points3D = new QLineEdit(QStringLiteral("0,0,0; 2000,0,0; 2000,2000,0"));
  m_points3D->setObjectName(QStringLiteral("points3D"));
  auto* rayon = new QDoubleSpinBox;
  rayon->setRange(0, 1000);
  rayon->setValue(50);
  rayon->setSuffix(QStringLiteral(" mm"));
  f->addRow(QStringLiteral("Câble"), m_cable3D);
  f->addRow(QStringLiteral("Points d’attache (mm)"), m_points3D);
  f->addRow(QStringLiteral("Rayon de courbure minimal"), rayon);
  l->addLayout(f);
  l->addWidget(bouton(QStringLiteral("Router en 3D"), QStringLiteral("router3D"), page, [this, rayon] {
    std::vector<std::array<double, 3>> p;
    for (const QString& t : m_points3D->text().split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
      const QStringList c = t.split(QLatin1Char(','), Qt::SkipEmptyParts);
      if (c.size() == 3) p.push_back({c[0].trimmed().toDouble(), c[1].trimmed().toDouble(), c[2].trimmed().toDouble()});
    }
    QStringList alertes;
    const double mm = routerEn3D(m_cable3D->currentText(), p, rayon->value(), &alertes);
    m_resultat->setText(QStringLiteral("Route 3D : %1 mm%2").arg(nb(mm, 1), alertes.isEmpty() ? QString() : QStringLiteral(" — ") + alertes.join(QStringLiteral(" ; "))));
  }));
  auto* topo = new QHBoxLayout;
  auto* de = new QLineEdit;
  de->setPlaceholderText(QStringLiteral("de (point)"));
  auto* vers = new QLineEdit;
  vers->setPlaceholderText(QStringLiteral("vers (point)"));
  auto* imposes = new QLineEdit;
  imposes->setPlaceholderText(QStringLiteral("passages imposés (points séparés par des espaces)"));
  topo->addWidget(de);
  topo->addWidget(vers);
  topo->addWidget(imposes, 1);
  topo->addWidget(bouton(QStringLiteral("Router sur la topologie…"), QStringLiteral("routerTopologie"), page, [this, de, vers, imposes] {
    const QString fich = QFileDialog::getOpenFileName(this, QStringLiteral("Feuille de topologie"), {}, QStringLiteral("Topologie (*.json)"));
    if (fich.isEmpty()) return;
    double l2 = 0;
    if (routerTopologie(fich, m_cable3D->currentText(), de->text().trimmed(), vers->text().trimmed(), imposes->text().split(QLatin1Char(' '), Qt::SkipEmptyParts), &l2))
      m_resultat->setText(QStringLiteral("Route sur la topologie : %1 mm (plus court chemin, passages imposés compris)").arg(nb(l2, 0)));
  }));
  l->addLayout(topo);
  // Circuit continu
  auto* c = new QFormLayout;
  auto* tension = new QDoubleSpinBox;
  tension->setRange(0, 1000);
  tension->setValue(12);
  tension->setSuffix(QStringLiteral(" V"));
  auto* aller = new QLineEdit;
  aller->setPlaceholderText(QStringLiteral("fils de l’aller (numéros)"));
  auto* retour = new QLineEdit;
  retour->setPlaceholderText(QStringLiteral("fils du retour"));
  auto* genre = new QComboBox;
  genre->addItems({QStringLiteral("courant (A)"), QStringLiteral("résistance (Ω)"), QStringLiteral("puissance (W)")});
  auto* valeur = new QDoubleSpinBox;
  valeur->setRange(0, 100000);
  valeur->setValue(6);
  auto* fusible = new QDoubleSpinBox;
  fusible->setRange(0, 1000);
  fusible->setSuffix(QStringLiteral(" A"));
  auto* chute = new QDoubleSpinBox;
  chute->setRange(0.1, 50);
  chute->setValue(5);
  chute->setSuffix(QStringLiteral(" %"));
  c->addRow(QStringLiteral("Tension de la source"), tension);
  c->addRow(QStringLiteral("Aller"), aller);
  c->addRow(QStringLiteral("Retour"), retour);
  c->addRow(QStringLiteral("Charge"), genre);
  c->addRow(QStringLiteral("Valeur"), valeur);
  c->addRow(QStringLiteral("Fusible"), fusible);
  c->addRow(QStringLiteral("Chute admise"), chute);
  l->addLayout(c);
  l->addWidget(bouton(QStringLiteral("Calculer le circuit"), QStringLiteral("calculerCircuit"), page, [=, this] {
    CircuitContinu k;
    k.tension = tension->value();
    for (const QString& x : aller->text().split(QRegularExpression(QStringLiteral("[ ,;]+")), Qt::SkipEmptyParts)) k.aller.push_back(s(x));
    for (const QString& x : retour->text().split(QRegularExpression(QStringLiteral("[ ,;]+")), Qt::SkipEmptyParts)) k.retour.push_back(s(x));
    k.charge.nom = "charge";
    (genre->currentIndex() == 0 ? k.charge.courant : genre->currentIndex() == 1 ? k.charge.resistance : k.charge.puissance) = valeur->value();
    k.fusible = fusible->value();
    k.chuteMax = chute->value();
    circuit(k);
  }));
  m_resultat = new QLabel;
  m_resultat->setObjectName(QStringLiteral("resultatLongueurs"));
  m_resultat->setWordWrap(true);
  l->addWidget(m_resultat);
  l->addStretch(1);
}

double FenetreCablage::routerEn3D(const QString& cable, const std::vector<std::array<double, 3>>& attaches, double rayon, QStringList* alertes) {
  const Route3D r = prom::elec::routerEn3D(attaches, rayon);
  if (alertes)
    for (const auto& a : r.alertes) *alertes << q(a);
  modifier([&](Cablage& h, ProjetElectrique&) { longueurDepuis3D(h, s(cable), r.longueur); }, QStringLiteral("Longueur venue de la 3D"));
  return r.longueur;
}

bool FenetreCablage::routerTopologie(const QString& fichier, const QString& cable, const QString& de, const QString& vers, const QStringList& imposes, double* longueur) {
  std::string erreur;
  std::optional<Topologie> t;
  try {
    t = lireTopologie(prom::lireFichier(prom::cheminFichier(s(fichier))), &erreur);
  } catch (const std::exception& e) {
    erreur = e.what();
  }
  if (!t) {
    m_resultat->setText(QStringLiteral("Topologie illisible : ") + q(erreur).toHtmlEscaped());
    return false;
  }
  std::vector<std::string> v;
  for (const QString& x : imposes) v.push_back(s(x));
  const RouteTopologie r = prom::elec::routerTopologie(*t, s(de), s(vers), v);
  if (!r.trouvee) {
    m_resultat->setText(QStringLiteral("Aucun chemin de %1 à %2 par les passages ouverts").arg(de, vers));
    return false;
  }
  if (longueur) *longueur = r.longueur;
  const auto h = courant();
  m_atelier.modifierProjetNomme([&](ProjetElectrique& pe) { longueurDepuisTopologie(pe, s(cable), r, h ? h->surlongueur : 0); }, QStringLiteral("Longueur venue de la topologie"));
  recharger();
  return true;
}

BilanContinu FenetreCablage::circuit(const CircuitContinu& c) {
  const auto h = courant();
  if (!h) return {};
  const BilanContinu b = resoudreContinu(*h, m_bibliotheque, c);
  QString t = QStringLiteral("Courant %1 A, tension à la charge %2 V, chute %3 V (%4 %)").arg(nb(b.courant), nb(b.tensionCharge), nb(b.chute), nb(b.pourcent, 1));
  for (const auto& a : b.alertes) t += QStringLiteral("<br><span style='color:#B91C1C'>⚠ %1</span>").arg(q(a).toHtmlEscaped());
  m_resultat->setText(t);
  return b;
}

// ======================================================================================================================
// Fabrication
// ======================================================================================================================
void FenetreCablage::construireFabrication(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  l->addWidget(bouton(QStringLiteral("Vérifier le faisceau"), QStringLiteral("verifierFaisceau"), page, [this] { verifier(); }));
  m_defauts = new QListWidget;
  m_defauts->setObjectName(QStringLiteral("defautsFaisceau"));
  l->addWidget(m_defauts, 1);
  auto* sorties = new QHBoxLayout;
  sorties->addWidget(bouton(QStringLiteral("Classeur XLSX…"), QStringLiteral("classeurFaisceau"), page, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Classeur du faisceau"), QStringLiteral("faisceau.xlsx"), QStringLiteral("Classeur (*.xlsx)"));
    if (!f.isEmpty()) exporterClasseur(f);
  }));
  sorties->addWidget(bouton(QStringLiteral("Plan de fabrication (PDF)…"), QStringLiteral("planFaisceau"), page, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Plan du faisceau"), QStringLiteral("plan-faisceau.pdf"), QStringLiteral("PDF (*.pdf)"));
    if (!f.isEmpty()) exporterPlan(f);
  }));
  m_gabarit = new QComboBox;
  m_gabarit->setObjectName(QStringLiteral("gabaritMachine"));
  for (const auto& g : gabaritsMachines()) m_gabarit->addItem(q(g.nom));
  sorties->addWidget(m_gabarit, 1);
  sorties->addWidget(bouton(QStringLiteral("Fichier machine…"), QStringLiteral("fichierMachine"), page, [this] {
    const auto& g = gabaritsMachines()[static_cast<std::size_t>(std::max(0, m_gabarit->currentIndex()))];
    const QString f = QFileDialog::getSaveFileName(this, q(g.nom), QStringLiteral("faisceau.") + q(g.extension), QStringLiteral("Fichier (*.") + q(g.extension) + QLatin1Char(')'));
    if (!f.isEmpty()) exporterMachine(m_gabarit->currentIndex(), f);
  }));
  sorties->addWidget(bouton(QStringLiteral("Devis…"), QStringLiteral("devisFaisceau"), page, [this] {
    const QString temps = QFileDialog::getOpenFileName(this, QStringLiteral("Temps unitaires de l’équipe (facultatif)"), {}, QStringLiteral("Temps (*.json)"));
    const QString pdf = QFileDialog::getSaveFileName(this, QStringLiteral("Devis du faisceau"), QStringLiteral("devis-faisceau.pdf"), QStringLiteral("PDF (*.pdf)"));
    if (pdf.isEmpty()) return;
    QString csv = pdf;
    csv.replace(QRegularExpression(QStringLiteral("\\.pdf$")), QStringLiteral(".csv"));
    devis(temps, pdf, csv);
  }));
  l->addLayout(sorties);
}

std::vector<DefautFaisceau> FenetreCablage::verifier() {
  const auto h = courant();
  m_defauts->clear();
  if (!h) return {};
  const auto v = verifierFaisceau(*h, m_bibliotheque);
  for (const auto& d : v) m_defauts->addItem(QStringLiteral("⚠ %1 — %2 : %3").arg(q(d.genre), q(d.objet), q(d.message)));
  if (v.empty()) m_defauts->addItem(QStringLiteral("✓ Faisceau sans défaut : extrémités, contacts, étanchéité, protections"));
  return v;
}

bool FenetreCablage::exporterClasseur(const QString& chemin) {
  const auto h = courant();
  return h && ecrire(this, chemin, xlsxFaisceau(*h, m_bibliotheque));
}

bool FenetreCablage::exporterPlan(const QString& chemin, const QString& revision) {
  const auto h = courant();
  return h && ecrire(this, chemin, pdfPlanFaisceau(*h, m_bibliotheque, s(m_atelier.projetCourant() ? q(m_atelier.projetCourant()->nom) : QString()), s(revision)));
}

bool FenetreCablage::exporterMachine(int gabarit, const QString& chemin) {
  const auto h = courant();
  const auto& g = gabaritsMachines();
  if (!h || gabarit < 0 || gabarit >= static_cast<int>(g.size())) return false;
  return ecrire(this, chemin, fichierMachine(*h, m_bibliotheque, g[static_cast<std::size_t>(gabarit)]));
}

DevisFaisceau FenetreCablage::devis(const QString& fichierTemps, const QString& pdf, const QString& csv) {
  const auto h = courant();
  if (!h) return {};
  TempsUnitaires t;
  if (!fichierTemps.isEmpty()) try {
      if (const auto x = lireTempsUnitaires(prom::lireFichier(prom::cheminFichier(s(fichierTemps))))) t = *x;
    } catch (const std::exception&) {
    }
  const DevisFaisceau d = devisFaisceau(*h, m_bibliotheque, t);
  if (!pdf.isEmpty()) ecrire(this, pdf, pdfDevis(d, h->nom));
  if (!csv.isEmpty()) ecrire(this, csv, csvDevis(d));
  m_etat->setText(QStringLiteral("Devis : matériel %1 €, main-d’œuvre %2 h (%3 €), total %4 € HT").arg(nb(d.materiel), nb(d.secondes / 3600, 3), nb(d.mainOeuvre), nb(d.total)));
  return d;
}

// ======================================================================================================================
// Atelier
// ======================================================================================================================
FenetreCablage* AtelierElectrique::cablage(const QString& onglet) {
  if (!m_cablage) m_cablage = new FenetreCablage(*this, this);
  m_cablage->recharger();
  if (!onglet.isEmpty()) m_cablage->montrer(onglet);
  m_cablage->show();
  m_cablage->raise();
  return m_cablage;
}

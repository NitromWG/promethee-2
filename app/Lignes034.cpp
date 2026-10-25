// Prométhée : schéma de lignes et topologies (18.4), schéma déduit et retouches (17.6). Voir Lignes034.hpp.
// Licence GPL-3.0-only.
#include "Lignes034.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "SelectionElectronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/simulation.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s8(const QString& s) { return s.toStdString(); }
QPushButton* bouton(const char* nom, const QString& texte) {
  auto* b = new QPushButton(texte);
  b->setObjectName(QLatin1String(nom));
  return b;
}
enum ColonneElement { Genre, Id, A, B, Impedance, Longueur, Valeur, NColonnes };
}  // namespace

// ======================================================================================================================
// Vue des topologies
// ======================================================================================================================
VueTopologie::VueTopologie(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("vueTopologie"));
  setMinimumSize(420, 200);
}

void VueTopologie::setTopologies(const SchemaLignes& prevue, const SchemaLignes& extraite) {
  m_prevue = prevue;
  m_extraite = extraite;
  update();
}

void VueTopologie::paintEvent(QPaintEvent*) {
  QPainter g(this);
  g.setRenderHint(QPainter::Antialiasing);
  g.fillRect(rect(), Qt::white);
  const double W = width(), x0 = 50, yp = 50, ye = height() * 0.55;
  double total = 1;
  for (const auto& e : m_extraite.elements) total = std::max(total, e.distance);
  const double k = (W - 100) / total;
  // Prévue : récepteurs espacés dans l'ordre prévu, en tireté
  g.setPen(QPen(QColor(0x9E, 0x9E, 0x9E), 1.5, Qt::DashLine));
  g.drawLine(QPointF(x0, yp), QPointF(W - 50, yp));
  for (std::size_t i = 0; i < m_prevue.ordre.size(); ++i) {
    const double x = x0 + (W - 100) * (m_prevue.ordre.size() > 1 ? static_cast<double>(i) / static_cast<double>(m_prevue.ordre.size() - 1) : 0);
    g.drawLine(QPointF(x, yp), QPointF(x, yp + 16));
    g.drawText(QPointF(x - 16, yp - 8), q(m_prevue.ordre[i]));
  }
  // Extraite : ligne principale à l'échelle, branches et vias
  g.setPen(QPen(QColor(0x15, 0x65, 0xC0), 3));
  g.drawLine(QPointF(x0, ye), QPointF(x0 + total * k, ye));
  for (const auto& e : m_extraite.elements) {
    const double x = x0 + std::max(0.0, e.distance) * k;
    if (e.genre == GenreElementLigne::Pilote) {
      g.fillRect(QRectF(x0 - 8, ye - 8, 16, 16), QColor(0x2E, 0x7D, 0x32));
      g.setPen(Qt::black);
      g.drawText(QPointF(x0 - 24, ye + 28), q(e.id));
    } else if (e.genre == GenreElementLigne::Recepteur && e.distance >= 0) {
      const bool trop = e.longueur > m_prevue.moignonMax + 1e-9;
      const double l = std::max(10.0, e.longueur * k);
      g.setPen(QPen(trop ? QColor(0xD3, 0x2F, 0x2F) : QColor(0x15, 0x65, 0xC0), 2));
      g.drawLine(QPointF(x, ye), QPointF(x, ye + l));
      g.setBrush(QColor(0x15, 0x65, 0xC0));
      g.drawEllipse(QPointF(x, ye + l), 5, 5);
      g.setPen(Qt::black);
      g.drawText(QPointF(x - 16, ye + l + 18), q(e.id));
    } else if (e.genre == GenreElementLigne::Via) {
      const bool trop = e.longueur > m_prevue.moignonViaMax + 1e-9;
      const QColor c = trop ? QColor(0xD3, 0x2F, 0x2F) : QColor(0x42, 0x42, 0x42);
      g.setPen(QPen(c, 2));
      g.setBrush(Qt::NoBrush);
      g.drawEllipse(QPointF(x, ye), 7, 7);
      g.drawText(QPointF(x - 18, ye - 14), QStringLiteral("%1 : %2 mm").arg(q(e.id)).arg(e.longueur, 0, 'f', 1));
    }
  }
}

// ======================================================================================================================
// Schéma de lignes
// ======================================================================================================================
FenetreLignes::FenetreLignes(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreLignes"));
  setWindowTitle(QStringLiteral("Schéma de lignes et topologie"));
  resize(1000, 720);
  auto* l = new QVBoxLayout(this);
  auto* f = new QFormLayout;
  m_reseau = new QComboBox;
  m_reseau->setObjectName(QStringLiteral("reseauLignes"));
  m_reseau->setEditable(true);
  m_topologie = new QComboBox;
  m_topologie->setObjectName(QStringLiteral("topologieLignes"));
  m_topologie->addItems({QStringLiteral("libre"), QStringLiteral("chaîne"), QStringLiteral("fly-by"), QStringLiteral("étoile"), QStringLiteral("T")});
  m_ordre = new QLineEdit;
  m_ordre->setObjectName(QStringLiteral("ordreLignes"));
  m_ordre->setPlaceholderText(QStringLiteral("U1.7 U2.3 U3.3 … (pilote d'abord)"));
  m_moignon = new QDoubleSpinBox;
  m_moignon->setObjectName(QStringLiteral("moignonMax"));
  m_moignon->setRange(0, 100);
  m_moignon->setValue(1);
  m_moignon->setSuffix(QStringLiteral(" mm"));
  m_moignonVia = new QDoubleSpinBox;
  m_moignonVia->setObjectName(QStringLiteral("moignonViaMax"));
  m_moignonVia->setRange(0, 10);
  m_moignonVia->setDecimals(2);
  m_moignonVia->setValue(0.5);
  m_moignonVia->setSuffix(QStringLiteral(" mm"));
  f->addRow(QStringLiteral("Réseau"), m_reseau);
  f->addRow(QStringLiteral("Topologie prévue"), m_topologie);
  f->addRow(QStringLiteral("Ordre de raccordement"), m_ordre);
  f->addRow(QStringLiteral("Branche au plus"), m_moignon);
  f->addRow(QStringLiteral("Moignon de via au plus"), m_moignonVia);
  l->addLayout(f);
  m_elements = new QTableWidget(0, NColonnes);
  m_elements->setObjectName(QStringLiteral("elementsLignes"));
  m_elements->setHorizontalHeaderLabels({QStringLiteral("Élément"), QStringLiteral("Repère"), QStringLiteral("Nœud A"), QStringLiteral("Nœud B"), QStringLiteral("Z0 (Ω)"), QStringLiteral("Longueur (mm)"),
                                         QStringLiteral("R, C ou IBIS")});
  m_elements->horizontalHeader()->setStretchLastSection(true);
  m_elements->setMaximumHeight(200);
  l->addWidget(m_elements);
  auto* barre = new QHBoxLayout;
  auto* extraire = bouton("extraireTopologie", QStringLiteral("Extraire du cuivre et comparer"));
  auto* simuler = bouton("simulerLignes", QStringLiteral("Simuler (moignon balayé)"));
  auto* regles = bouton("lignesVersRegles", QStringLiteral("Vers les règles"));
  auto* enregistrer = bouton("enregistrerLignes", QStringLiteral("Enregistrer dans la carte"));
  for (QWidget* w : std::initializer_list<QWidget*>{extraire, simuler, regles, enregistrer}) barre->addWidget(w);
  barre->addStretch();
  l->addLayout(barre);
  m_vue = new VueTopologie;
  l->addWidget(m_vue, 1);
  m_liste = new QListWidget;
  m_liste->setObjectName(QStringLiteral("ecartsTopologie"));
  m_liste->setMaximumHeight(120);
  l->addWidget(m_liste);
  m_etat = new QLabel;
  m_etat->setObjectName(QStringLiteral("etatLignes"));
  m_etat->setWordWrap(true);
  l->addWidget(m_etat);
  connect(m_reseau, &QComboBox::currentTextChanged, this, [this](const QString& r) { choisirReseau(r); });
  connect(extraire, &QPushButton::clicked, this, [this] { extraireEtComparer(); });
  connect(simuler, &QPushButton::clicked, this, [this] {
    QString t;
    for (double l : {0.0, 1.0, 2.0, 3.0}) t += QStringLiteral("moignon %1 mm : %2 V  ").arg(l).arg(this->simuler(l), 0, 'f', 3);
    m_etat->setText(t);
  });
  connect(regles, &QPushButton::clicked, this, [this] { versRegles(); });
  connect(enregistrer, &QPushButton::clicked, this, [this] { this->enregistrer(); });
}

void FenetreLignes::recharger() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  const QString avant = m_reseau->currentText();
  m_reseau->blockSignals(true);
  m_reseau->clear();
  std::set<std::string> noms;
  for (const auto& t : c->circuit.pistes)
    if (!t.reseau.empty()) noms.insert(t.reseau);
  for (const auto& n : noms) m_reseau->addItem(q(n));
  m_reseau->blockSignals(false);
  if (!avant.isEmpty()) m_reseau->setCurrentText(avant);
  choisirReseau(m_reseau->currentText());
}

void FenetreLignes::choisirReseau(const QString& reseau) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  for (const auto& s : schemasLignes(*c))
    if (s.reseau == s8(reseau)) {
      setPrevue(s);
      return;
    }
  SchemaLignes s;
  s.reseau = s8(reseau);
  setPrevue(s);
}

void FenetreLignes::setPrevue(const SchemaLignes& s) {
  m_reseau->blockSignals(true);
  m_reseau->setCurrentText(q(s.reseau));
  m_reseau->blockSignals(false);
  m_topologie->setCurrentIndex(static_cast<int>(s.topologie));
  QStringList o;
  for (const auto& x : s.ordre) o << q(x);
  m_ordre->setText(o.join(QLatin1Char(' ')));
  m_moignon->setValue(s.moignonMax);
  m_moignonVia->setValue(s.moignonViaMax);
  m_elements->setRowCount(static_cast<int>(s.elements.size()));
  for (int i = 0; i < static_cast<int>(s.elements.size()); ++i) {
    const ElementLigne& e = s.elements[static_cast<std::size_t>(i)];
    auto* genre = new QComboBox;
    genre->addItems({QStringLiteral("pilote"), QStringLiteral("troncon"), QStringLiteral("via"), QStringLiteral("terminaison"), QStringLiteral("recepteur")});
    genre->setCurrentIndex(static_cast<int>(e.genre));
    m_elements->setCellWidget(i, Genre, genre);
    m_elements->setItem(i, Id, new QTableWidgetItem(q(e.id)));
    m_elements->setItem(i, A, new QTableWidgetItem(q(e.a)));
    m_elements->setItem(i, B, new QTableWidgetItem(q(e.b)));
    m_elements->setItem(i, Impedance, new QTableWidgetItem(QString::number(e.impedance)));
    m_elements->setItem(i, Longueur, new QTableWidgetItem(QString::number(e.longueur)));
    m_elements->setItem(i, Valeur, new QTableWidgetItem(e.genre == GenreElementLigne::Recepteur ? QString::number(e.capacite) : !e.ibis.empty() ? q(e.ibis + " " + e.modele) : QString::number(e.resistance)));
  }
}

SchemaLignes FenetreLignes::prevue() const {
  SchemaLignes s;
  s.reseau = s8(m_reseau->currentText());
  s.topologie = static_cast<TopologieSignal>(m_topologie->currentIndex());
  for (const QString& x : m_ordre->text().split(QLatin1Char(' '), Qt::SkipEmptyParts)) s.ordre.push_back(s8(x));
  s.moignonMax = m_moignon->value();
  s.moignonViaMax = m_moignonVia->value();
  s.provenance = "schéma de lignes de " + s.reseau;
  auto texte = [&](int i, int col) { return m_elements->item(i, col) ? m_elements->item(i, col)->text() : QString(); };
  for (int i = 0; i < m_elements->rowCount(); ++i) {
    ElementLigne e;
    const auto* genre = qobject_cast<QComboBox*>(m_elements->cellWidget(i, Genre));
    e.genre = static_cast<GenreElementLigne>(genre ? genre->currentIndex() : 1);
    e.id = s8(texte(i, Id));
    e.a = s8(texte(i, A));
    e.b = s8(texte(i, B));
    e.impedance = texte(i, Impedance).toDouble();
    e.longueur = texte(i, Longueur).toDouble();
    const QString v = texte(i, Valeur);
    if (e.genre == GenreElementLigne::Recepteur) e.capacite = v.toDouble();
    else if (e.genre == GenreElementLigne::Pilote && v.contains(QLatin1Char(' '))) e.ibis = s8(v.section(QLatin1Char(' '), 0, 0)), e.modele = s8(v.section(QLatin1Char(' '), 1));
    else e.resistance = v.toDouble();
    s.elements.push_back(e);
  }
  return s;
}

bool FenetreLignes::enregistrer() {
  const SchemaLignes s = prevue();
  const bool ok = m_atelier.modifierConception(
      [&](ConceptionElec& c) {
        auto v = schemasLignes(c);
        const auto it = std::find_if(v.begin(), v.end(), [&](const SchemaLignes& x) { return x.reseau == s.reseau; });
        if (it != v.end()) *it = s;
        else v.push_back(s);
        ecrireSchemasLignes(c, v);
      },
      QStringLiteral("Schéma de lignes de %1").arg(q(s.reseau)));
  m_etat->setText(ok ? QStringLiteral("Schéma de lignes de %1 enregistré : la DRC compare désormais le cuivre à cette topologie.").arg(q(s.reseau)) : QStringLiteral("Schéma de lignes inchangé."));
  return ok;
}

int FenetreLignes::extraireEtComparer() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return 0;
  const SchemaLignes p = prevue();
  m_extraite = extraireTopologie(*c, p.reseau, p.ordre.empty() ? nullptr : &p);
  m_ecarts = comparerTopologie(p, m_extraite);
  m_vue->setTopologies(p, m_extraite);
  m_liste->clear();
  for (const auto& e : m_ecarts) m_liste->addItem(q(e.message));
  QStringList o;
  for (const auto& x : m_extraite.ordre) o << q(x);
  m_etat->setText(QStringLiteral("Topologie extraite : %1 ; %2 écart(s).").arg(o.join(QStringLiteral(" → "))).arg(m_ecarts.size()));
  return static_cast<int>(m_ecarts.size());
}

double FenetreLignes::simuler(double moignon) {
  if (!ngspiceDisponible()) return std::nan("");
  const SchemaLignes s = m_extraite.elements.empty() ? prevue() : m_extraite;
  const ResultatSimulation r = prom::ecad::simuler(netlistLignes(s, 0.3e-9, 20e-9, moignon));
  if (!r.reussi() || s.ordre.empty()) return std::nan("");
  std::string nom = "v(n_" + s.ordre.back() + ")";
  for (auto& ch : nom) ch = ch == '.' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  const auto* v = r.reel(nom);
  return v && !v->empty() ? *std::max_element(v->begin(), v->end()) : std::nan("");
}

bool FenetreLignes::versRegles() {
  const SchemaLignes s = prevue();
  int n = 0;
  const bool ok = m_atelier.modifierConception([&](ConceptionElec& c) { n = appliquerAuxRegles(c, s); }, QStringLiteral("Règles du schéma de lignes de %1").arg(q(s.reseau)));
  m_etat->setText(QStringLiteral("Règles de %1 posées : topologie, ordre et %2 tronçon(s) bornés ; provenance notée.").arg(q(s.reseau)).arg(n));
  return ok;
}

// ======================================================================================================================
// Retouches
// ======================================================================================================================
FenetreRetouches::FenetreRetouches(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreRetouches"));
  setWindowTitle(QStringLiteral("Retouches de la révision fabriquée"));
  resize(820, 620);
  auto* l = new QVBoxLayout(this);
  m_liste = new QListWidget;
  m_liste->setObjectName(QStringLiteral("listeRetouches"));
  l->addWidget(m_liste, 1);
  auto* f = new QFormLayout;
  m_genre = new QComboBox;
  m_genre->setObjectName(QStringLiteral("genreRetouche"));
  m_genre->addItems({QStringLiteral("fil de reprise"), QStringLiteral("piste coupée"), QStringLiteral("composant changé")});
  m_a = new QLineEdit;
  m_a->setObjectName(QStringLiteral("retoucheA"));
  m_a->setPlaceholderText(QStringLiteral("fil : « R1.2 » ; composant : son repère ; piste : la piste choisie au circuit"));
  m_b = new QLineEdit;
  m_b->setObjectName(QStringLiteral("retoucheB"));
  m_b->setPlaceholderText(QStringLiteral("fil : l'autre broche"));
  m_valeur = new QLineEdit;
  m_valeur->setObjectName(QStringLiteral("retoucheValeur"));
  m_series = new QLineEdit;
  m_series->setObjectName(QStringLiteral("retoucheSeries"));
  m_series->setPlaceholderText(QStringLiteral("SN001 SN002 (vide : toutes les cartes)"));
  f->addRow(QStringLiteral("Retouche"), m_genre);
  f->addRow(QStringLiteral("De"), m_a);
  f->addRow(QStringLiteral("À"), m_b);
  f->addRow(QStringLiteral("Nouvelle valeur"), m_valeur);
  f->addRow(QStringLiteral("Numéros de série"), m_series);
  l->addLayout(f);
  auto* barre = new QHBoxLayout;
  auto* ajouter = bouton("ajouterRetouche", QStringLiteral("Noter la retouche"));
  auto* porter = bouton("porterRetouches", QStringLiteral("Porter au schéma"));
  auto* exporter = bouton("listeRetouches", QStringLiteral("Liste par carte (CSV)…"));
  for (QWidget* w : std::initializer_list<QWidget*>{ajouter, porter, exporter}) barre->addWidget(w);
  barre->addStretch();
  l->addLayout(barre);
  auto* g = new QHBoxLayout;
  m_fabriquee = new QComboBox;
  m_fabriquee->setObjectName(QStringLiteral("revisionFabriquee"));
  auto* accepter = bouton("accepterRetouche", QStringLiteral("Accepter la retouche choisie"));
  g->addWidget(new QLabel(QStringLiteral("Proposées par la révision fabriquée :")));
  g->addWidget(m_fabriquee, 1);
  g->addWidget(accepter);
  l->addLayout(g);
  m_proposees = new QListWidget;
  m_proposees->setObjectName(QStringLiteral("retouchesProposees"));
  m_proposees->setMaximumHeight(120);
  l->addWidget(m_proposees);
  m_etat = new QLabel;
  m_etat->setObjectName(QStringLiteral("etatRetouches"));
  m_etat->setWordWrap(true);
  l->addWidget(m_etat);
  connect(ajouter, &QPushButton::clicked, this, [this] {
    Retouche r;
    r.genre = static_cast<GenreRetouche>(m_genre->currentIndex());
    if (r.genre == GenreRetouche::FilDeReprise) r.a = s8(m_a->text()), r.b = s8(m_b->text());
    else if (r.genre == GenreRetouche::ComposantChange) r.composant = s8(m_a->text()), r.valeur = s8(m_valeur->text());
    else {
      // La piste choisie au circuit, sinon l'identifiant tapé
      r.piste = s8(m_a->text());
      if (SelectionElectronique* s = m_atelier.selection())
        for (const auto& x : s->objets())
          if (x.genre == SelectionElectronique::Genre::Piste) r.piste = x.id;
    }
    for (const QString& n : m_series->text().split(QLatin1Char(' '), Qt::SkipEmptyParts)) r.numerosSerie.push_back(s8(n));
    this->ajouter(r);
  });
  connect(porter, &QPushButton::clicked, this, [this] { porterAuSchema(); });
  connect(exporter, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Liste des retouches"), QStringLiteral("retouches.csv"));
    if (!f.isEmpty()) prom::ecrireFichierAtomique(prom::cheminFichier(s8(f)), s8(liste({})));
  });
  connect(m_fabriquee, &QComboBox::currentIndexChanged, this, [this](int i) {
    m_proposees->clear();
    for (const auto& r : proposees(i)) m_proposees->addItem(q(r.description.empty() ? std::string(r.genre == GenreRetouche::FilDeReprise ? "fil de reprise " + r.a + " → " + r.b : r.genre == GenreRetouche::ComposantChange ? r.composant + " → " + r.valeur : "piste coupée") : r.description));
  });
  connect(accepter, &QPushButton::clicked, this, [this] { this->accepter(m_fabriquee->currentIndex(), m_proposees->currentRow()); });
}

void FenetreRetouches::recharger() {
  m_liste->clear();
  if (const ConceptionElec* c = m_atelier.conceptionCourante())
    for (const auto& r : retouchesDe(*c)) {
      QString t = r.genre == GenreRetouche::FilDeReprise ? QStringLiteral("Fil de reprise %1 → %2").arg(q(r.a), q(r.b))
                  : r.genre == GenreRetouche::ComposantChange ? QStringLiteral("%1 changé : %2").arg(q(r.composant), q(r.valeur))
                                                              : QStringLiteral("Piste coupée");
      t += QStringLiteral(" (révision %1%2)%3").arg(q(r.revision), r.numerosSerie.empty() ? QString() : QStringLiteral(", ") + QString::number(r.numerosSerie.size()) + QStringLiteral(" carte(s)"),
                                                     r.appliquee ? QStringLiteral(" — au schéma") : QString());
      m_liste->addItem(t);
    }
  const int avant = m_fabriquee->currentIndex();
  m_fabriquee->blockSignals(true);
  m_fabriquee->clear();
  if (const Document* d = m_atelier.document())
    for (const auto& x : d->projet().electronique) m_fabriquee->addItem(q(x.nom + " (révision " + x.cartouche.revision + ")"));
  m_fabriquee->blockSignals(false);
  if (avant >= 0 && avant < m_fabriquee->count()) m_fabriquee->setCurrentIndex(avant);
}

bool FenetreRetouches::ajouter(const Retouche& r) {
  std::string u;
  const bool ok = m_atelier.modifierConception([&](ConceptionElec& c) { u = ajouterRetouche(c, r); }, QStringLiteral("Retouche notée"));
  recharger();
  m_etat->setText(ok ? QStringLiteral("Retouche notée sur le calque de la révision.") : QStringLiteral("Retouche refusée."));
  return ok;
}

int FenetreRetouches::porterAuSchema() {
  int n = 0;
  m_atelier.modifierConception([&](ConceptionElec& c) { n = appliquerRetouches(c); }, QStringLiteral("Retouches portées au schéma"));
  recharger();
  m_etat->setText(QStringLiteral("%1 retouche(s) portée(s) au schéma.").arg(n));
  return n;
}

QString FenetreRetouches::liste(const QString& numeroSerie) const {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  return c ? q(listeRetouches(*c, s8(numeroSerie))) : QString();
}

std::vector<Retouche> FenetreRetouches::proposees(int revisionFabriquee) const {
  const Document* d = m_atelier.document();
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!d || !c || revisionFabriquee < 0 || revisionFabriquee >= static_cast<int>(d->projet().electronique.size())) return {};
  return retouchesProposees(d->projet().electronique[static_cast<std::size_t>(revisionFabriquee)], *c);
}

bool FenetreRetouches::accepter(int revisionFabriquee, int rang) {
  const auto p = proposees(revisionFabriquee);
  if (rang < 0 || rang >= static_cast<int>(p.size())) return false;
  std::string e;
  bool fait = false;
  m_atelier.modifierConception([&](ConceptionElec& c) { fait = accepterRetouche(c, p[static_cast<std::size_t>(rang)], &e); }, QStringLiteral("Retouche de la révision fabriquée acceptée"));
  m_etat->setText(fait ? QStringLiteral("Retouche acceptée dans cette révision.") : q(e));
  emit m_fabriquee->currentIndexChanged(m_fabriquee->currentIndex());
  return fait;
}

// ======================================================================================================================
// Atelier
// ======================================================================================================================
FenetreLignes* AtelierElectronique::lignes() {
  if (!m_lignes) m_lignes = new FenetreLignes(*this, this);
  m_lignes->recharger();
  m_lignes->show();
  m_lignes->raise();
  return m_lignes;
}

FenetreRetouches* AtelierElectronique::retouches() {
  if (!m_retouches) m_retouches = new FenetreRetouches(*this, this);
  m_retouches->recharger();
  m_retouches->show();
  m_retouches->raise();
  return m_retouches;
}

bool AtelierElectronique::deduireSchema() {
  BilanSchemaDeduit b;
  const bool ok = modifierConception([&](ConceptionElec& c) { b = schemaDepuisCircuit(c, 0); }, QStringLiteral("Schéma déduit du circuit"));
  afficherMessage(ok ? QStringLiteral("<b>Schéma déduit du circuit</b> : %1 symbole(s) posé(s), %2 étiquette(s), %3 réseau(x) ; range les symboles à la main.").arg(b.symboles).arg(b.etiquettes).arg(b.reseaux)
                     : QStringLiteral("Rien à déduire : chaque composant de la carte a déjà son symbole."),
                  ok ? NiveauMessage::Succes : NiveauMessage::Information);
  return ok;
}

void AtelierElectronique::enregistrerCommandesL17() {
  auto& c = m_commandes;
  c["schema.depuisCircuit"] = [this] { deduireSchema(); };
  c["circuit.retouches"] = [this] { retouches(); };
  c["circuit.schemaLignes"] = [this] { lignes(); };
}

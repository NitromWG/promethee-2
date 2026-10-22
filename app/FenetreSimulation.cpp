// Prométhée : fenêtre de simulation (lot L11). Licence GPL-3.0-only.
#include "FenetreSimulation.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "promethee/valeurs.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
// Nombre écrit avec un préfixe SI : 1,59 k ; 10 µ
QString si(double v, int chiffres = 3) {
  if (v == 0 || !std::isfinite(v)) return QStringLiteral("0");
  static const std::pair<double, const char*> prefixes[] = {{1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1, ""}, {1e-3, "m"}, {1e-6, "µ"}, {1e-9, "n"}, {1e-12, "p"}};
  const double a = std::abs(v);
  for (const auto& [f, p] : prefixes)
    if (a >= f * 0.9999) return QLocale().toString(v / f, 'g', chiffres) + QString::fromUtf8(p);
  return QLocale().toString(v, 'g', chiffres);
}
std::vector<double> graduations(double a, double b, bool log) {
  std::vector<double> r;
  if (!(b > a)) return r;
  if (log) {
    for (double d = std::floor(std::log10(a)); d <= std::ceil(std::log10(b)); ++d)
      if (const double x = std::pow(10.0, d); x >= a * 0.999 && x <= b * 1.001) r.push_back(x);
    return r;
  }
  const double brut = (b - a) / 6, mag = std::pow(10.0, std::floor(std::log10(brut)));
  const double pas = brut / mag < 1.5 ? mag : brut / mag < 3.5 ? 2 * mag : brut / mag < 7.5 ? 5 * mag : 10 * mag;
  for (double x = std::ceil(a / pas) * pas; x <= b + pas * 1e-6; x += pas) r.push_back(std::abs(x) < pas * 1e-9 ? 0 : x);
  return r;
}
const QColor TEINTES[] = {QColor(0x2F, 0x6B, 0xF5), QColor(0xE0, 0x6C, 0x2A), QColor(0x2E, 0xA0, 0x5A), QColor(0xB0, 0x3A, 0xC8), QColor(0xC8, 0x3A, 0x4A), QColor(0x1A, 0x9A, 0xA8)};
}  // namespace

// ---------- Tracé ----------
TraceCourbes::TraceCourbes(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("traceCourbes"));
  setMinimumSize(420, 260);
  setMouseTracking(true);
}

void TraceCourbes::setCourbes(std::vector<Courbe> c, const QString& axeX, const QString& axeY, bool logX) {
  m_courbes = std::move(c);
  m_axeX = axeX;
  m_axeY = axeY;
  m_logX = logX;
  bool premier = true;
  for (const auto& k : m_courbes)
    for (std::size_t i = 0; i < k.x.size() && i < k.y.size(); ++i) {
      if (!std::isfinite(k.y[i]) || (m_logX && k.x[i] <= 0)) continue;
      if (premier) {
        m_x0 = m_x1 = k.x[i];
        m_y0 = m_y1 = k.y[i];
        premier = false;
      }
      m_x0 = std::min(m_x0, k.x[i]);
      m_x1 = std::max(m_x1, k.x[i]);
      m_y0 = std::min(m_y0, k.y[i]);
      m_y1 = std::max(m_y1, k.y[i]);
    }
  if (premier) m_x0 = 0, m_x1 = 1, m_y0 = 0, m_y1 = 1;
  if (m_y1 - m_y0 < 1e-12) m_y0 -= 0.5, m_y1 += 0.5;
  const double marge = (m_y1 - m_y0) * 0.06;
  m_y0 -= marge;
  m_y1 += marge;
  m_curseurs[0] = m_logX ? std::pow(10.0, std::log10(m_x0) + (std::log10(m_x1) - std::log10(m_x0)) / 3) : m_x0 + (m_x1 - m_x0) / 3;
  m_curseurs[1] = m_logX ? std::pow(10.0, std::log10(m_x0) + 2 * (std::log10(m_x1) - std::log10(m_x0)) / 3) : m_x0 + 2 * (m_x1 - m_x0) / 3;
  update();
  emit curseursChanges();
}

void TraceCourbes::setCurseur(int i, double x) {
  m_curseurs[i] = std::clamp(x, m_x0, m_x1);
  update();
  emit curseursChanges();
}

QRectF TraceCourbes::zone() const { return QRectF(64, 16, std::max(10, width() - 64 - 16), std::max(10, height() - 16 - 44)); }
double TraceCourbes::versEcranX(double x) const {
  const QRectF z = zone();
  const double t = m_logX ? (std::log10(std::max(x, 1e-30)) - std::log10(m_x0)) / (std::log10(m_x1) - std::log10(m_x0)) : (x - m_x0) / (m_x1 - m_x0);
  return z.left() + t * z.width();
}
double TraceCourbes::depuisEcranX(double px) const {
  const QRectF z = zone();
  const double t = std::clamp((px - z.left()) / z.width(), 0.0, 1.0);
  return m_logX ? std::pow(10.0, std::log10(m_x0) + t * (std::log10(m_x1) - std::log10(m_x0))) : m_x0 + t * (m_x1 - m_x0);
}

void TraceCourbes::paintEvent(QPaintEvent*) {
  QPainter g(this);
  g.setRenderHint(QPainter::Antialiasing);
  const QColor fond = palette().color(QPalette::Base), texte = palette().color(QPalette::Text);
  QColor grille = texte;
  grille.setAlphaF(0.15);
  g.fillRect(rect(), fond);
  const QRectF z = zone();
  auto Y = [&](double y) { return z.bottom() - (y - m_y0) / (m_y1 - m_y0) * z.height(); };
  g.setFont(QFont(font().family(), 8));
  for (double x : graduations(m_x0, m_x1, m_logX)) {
    const double px = versEcranX(x);
    g.setPen(QPen(grille, 1));
    g.drawLine(QPointF(px, z.top()), QPointF(px, z.bottom()));
    g.setPen(texte);
    g.drawText(QRectF(px - 40, z.bottom() + 4, 80, 14), Qt::AlignCenter, si(x));
  }
  for (double y : graduations(m_y0, m_y1, false)) {
    const double py = Y(y);
    g.setPen(QPen(grille, 1));
    g.drawLine(QPointF(z.left(), py), QPointF(z.right(), py));
    g.setPen(texte);
    g.drawText(QRectF(0, py - 7, z.left() - 6, 14), Qt::AlignRight | Qt::AlignVCenter, si(y));
  }
  g.setPen(texte);
  g.drawRect(z);
  g.drawText(QRectF(z.left(), z.bottom() + 22, z.width(), 16), Qt::AlignCenter, m_axeX);
  g.save();
  g.translate(12, z.center().y());
  g.rotate(-90);
  g.drawText(QRectF(-z.height() / 2, -8, z.height(), 16), Qt::AlignCenter, m_axeY);
  g.restore();
  g.setClipRect(z);
  for (const auto& k : m_courbes) {
    QPolygonF p;
    for (std::size_t i = 0; i < k.x.size() && i < k.y.size(); ++i)
      if (std::isfinite(k.y[i]) && (!m_logX || k.x[i] > 0)) p << QPointF(versEcranX(k.x[i]), Y(k.y[i]));
    QColor c = k.couleur;
    if (k.attenuee) c.setAlphaF(0.3);
    g.setPen(QPen(c, k.attenuee ? 1.2 : 1.8));
    g.drawPolyline(p);
  }
  // Curseurs
  for (int i = 0; i < 2; ++i) {
    const double px = versEcranX(m_curseurs[i]);
    g.setPen(QPen(QColor(0xF2, 0xC1, 0x4E), 1, Qt::DashLine));
    g.drawLine(QPointF(px, z.top()), QPointF(px, z.bottom()));
    g.drawText(QPointF(px + 3, z.top() + 12), i == 0 ? QStringLiteral("A") : QStringLiteral("B"));
  }
  g.setClipping(false);
  // Légende
  double ly = z.top() + 6;
  for (const auto& k : m_courbes) {
    if (k.attenuee) continue;
    g.setPen(QPen(k.couleur, 3));
    g.drawLine(QPointF(z.right() - 120, ly + 6), QPointF(z.right() - 104, ly + 6));
    g.setPen(texte);
    g.drawText(QRectF(z.right() - 100, ly, 96, 14), Qt::AlignLeft | Qt::AlignVCenter, k.nom);
    ly += 15;
  }
}

void TraceCourbes::mousePressEvent(QMouseEvent* e) {
  const double px = e->position().x();
  m_tenu = std::abs(px - versEcranX(m_curseurs[0])) <= std::abs(px - versEcranX(m_curseurs[1])) ? 0 : 1;
  setCurseur(m_tenu, depuisEcranX(px));
}
void TraceCourbes::mouseMoveEvent(QMouseEvent* e) {
  if (m_tenu >= 0) setCurseur(m_tenu, depuisEcranX(e->position().x()));
}
void TraceCourbes::mouseReleaseEvent(QMouseEvent*) { m_tenu = -1; }

// ---------- Fenêtre ----------
FenetreSimulation::FenetreSimulation(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreSimulation"));
  setWindowTitle(QStringLiteral("Simulation"));
  resize(1100, 700);
  auto* racine = new QHBoxLayout(this);
  auto* gauche = new QVBoxLayout;
  auto* f = new QFormLayout;
  m_type = new QComboBox;
  m_type->setObjectName(QStringLiteral("typeAnalyse"));
  m_type->addItems({QStringLiteral("Point de repos"), QStringLiteral("Balayage continu"), QStringLiteral("Réponse en fréquence"), QStringLiteral("Transitoire")});
  m_type->setCurrentIndex(3);
  f->addRow(QStringLiteral("Analyse"), m_type);
  auto champ = [&](const QString& nom, const QString& v, const QString& etiquette) {
    auto* e = new QLineEdit(v);
    e->setObjectName(nom);
    f->addRow(etiquette, e);
    return e;
  };
  m_pas = champ(QStringLiteral("pas"), QStringLiteral("10u"), QStringLiteral("Pas (s)"));
  m_duree = champ(QStringLiteral("duree"), QStringLiteral("10m"), QStringLiteral("Durée (s)"));
  m_fDebut = champ(QStringLiteral("fDebut"), QStringLiteral("1"), QStringLiteral("De (Hz)"));
  m_fFin = champ(QStringLiteral("fFin"), QStringLiteral("1meg"), QStringLiteral("À (Hz)"));
  m_source = champ(QStringLiteral("source"), QStringLiteral("V1"), QStringLiteral("Source balayée"));
  m_dcDebut = champ(QStringLiteral("dcDebut"), QStringLiteral("0"), QStringLiteral("De"));
  m_dcFin = champ(QStringLiteral("dcFin"), QStringLiteral("5"), QStringLiteral("À"));
  m_dcPas = champ(QStringLiteral("dcPas"), QStringLiteral("0.1"), QStringLiteral("Pas"));
  gauche->addLayout(f);
  gauche->addWidget(new QLabel(QStringLiteral("<b>Sondes</b> (tensions des réseaux)")));
  m_sondes = new QListWidget;
  m_sondes->setObjectName(QStringLiteral("sondes"));
  gauche->addWidget(m_sondes, 1);
  auto* reglage = new QFormLayout;
  m_regle = new QComboBox;
  m_regle->setObjectName(QStringLiteral("composantRegle"));
  m_facteur = new QDoubleSpinBox;
  m_facteur->setObjectName(QStringLiteral("facteurRegle"));
  m_facteur->setRange(0.1, 10);
  m_facteur->setSingleStep(0.1);
  m_facteur->setValue(1);
  m_facteur->setPrefix(QStringLiteral("× "));
  reglage->addRow(QStringLiteral("Régler"), m_regle);
  reglage->addRow(QStringLiteral("Valeur"), m_facteur);
  gauche->addLayout(reglage);
  auto* appliquer = new QPushButton(QStringLiteral("Écrire la valeur réglée dans le schéma"));
  appliquer->setObjectName(QStringLiteral("appliquerReglage"));
  gauche->addWidget(appliquer);
  auto* boutons = new QHBoxLayout;
  m_lancer = new QPushButton(QStringLiteral("Lancer"));
  m_lancer->setObjectName(QStringLiteral("lancerSimulation"));
  m_arreter = new QPushButton(QStringLiteral("Arrêter"));
  m_arreter->setObjectName(QStringLiteral("arreterSimulation"));
  m_arreter->setEnabled(false);
  boutons->addWidget(m_lancer);
  boutons->addWidget(m_arreter);
  gauche->addLayout(boutons);
  m_etat = new QLabel;
  m_etat->setObjectName(QStringLiteral("etatSimulation"));
  m_etat->setWordWrap(true);
  gauche->addWidget(m_etat);
  auto* g = new QWidget;
  g->setLayout(gauche);
  g->setMaximumWidth(330);
  racine->addWidget(g);
  m_onglets = new QTabWidget;
  m_trace = new TraceCourbes;
  m_onglets->addTab(m_trace, QStringLiteral("Courbes"));
  m_tableMesures = new QTableWidget(0, 2);
  m_tableMesures->setObjectName(QStringLiteral("tableMesures"));
  m_tableMesures->setHorizontalHeaderLabels({QStringLiteral("Mesure"), QStringLiteral("Valeur")});
  m_tableMesures->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_onglets->addTab(m_tableMesures, QStringLiteral("Mesures"));
  auto* netlist = new QWidget;
  auto* nl = new QVBoxLayout(netlist);
  m_texteNetlist = new QPlainTextEdit;
  m_texteNetlist->setObjectName(QStringLiteral("texteNetlist"));
  m_texteNetlist->setFont(QFont(QStringLiteral("monospace")));
  nl->addWidget(m_texteNetlist);
  auto* essai = new QPushButton(QStringLiteral("Simuler cette netlist (le schéma ne change pas)"));
  nl->addWidget(essai);
  m_onglets->addTab(netlist, QStringLiteral("Netlist"));
  m_console = new QPlainTextEdit;
  m_console->setReadOnly(true);
  m_console->setFont(QFont(QStringLiteral("monospace")));
  m_onglets->addTab(m_console, QStringLiteral("Console de ngspice"));
  racine->addWidget(m_onglets, 1);
  connect(m_type, &QComboBox::currentIndexChanged, this, [this] { majChamps(); });
  connect(m_lancer, &QPushButton::clicked, this, [this] { lancer(); });
  connect(m_arreter, &QPushButton::clicked, this, [this] { arreter(); });
  connect(essai, &QPushButton::clicked, this, [this] { lancerNetlist(m_texteNetlist->toPlainText()); });
  connect(m_trace, &TraceCourbes::curseursChanges, this, [this] { afficher(false); });
  connect(m_facteur, &QDoubleSpinBox::valueChanged, this, [this](double v) {
    if (!m_regle->currentText().isEmpty()) regler(m_regle->currentText().toStdString(), v);
  });
  connect(appliquer, &QPushButton::clicked, this, [this] { appliquerReglage(); });
  majChamps();
  refaire();
  if (!ngspiceDisponible()) {
    m_lancer->setEnabled(false);
    m_etat->setText(QStringLiteral("<span style='color:#c53030'>ngspice n’est pas installé : la simulation est indisponible. Installe ngspice (bibliothèque libngspice) puis rouvre cette fenêtre.</span>"));
  } else
    m_etat->setText(QStringLiteral("Moteur : %1").arg(q(versionNgspice())));
}

FenetreSimulation::~FenetreSimulation() {
  if (m_arret) *m_arret = true;
  if (m_fil.joinable()) m_fil.join();
}

void FenetreSimulation::majChamps() {
  const int t = m_type->currentIndex();
  for (QLineEdit* e : {m_pas, m_duree}) e->setEnabled(t == 3);
  for (QLineEdit* e : {m_fDebut, m_fFin}) e->setEnabled(t == 2);
  for (QLineEdit* e : {m_source, m_dcDebut, m_dcFin, m_dcPas}) e->setEnabled(t == 1);
}

void FenetreSimulation::refaire() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  std::set<std::string> coches;
  for (int i = 0; i < m_sondes->count(); ++i)
    if (m_sondes->item(i)->checkState() == Qt::Checked) coches.insert(m_sondes->item(i)->text().toStdString());
  const bool premier = m_sondes->count() == 0;
  m_sondes->clear();
  const Connectivite k = connectivite(*c);
  std::vector<std::string> noms;
  for (const auto& r : k.reseaux)
    if (!r.broches.empty() && !r.nom.empty()) noms.push_back(r.nom);
  std::sort(noms.begin(), noms.end());
  noms.erase(std::unique(noms.begin(), noms.end()), noms.end());
  for (const auto& n : noms) {
    auto* it = new QListWidgetItem(q(n));
    const bool nomme = n.rfind("N-", 0) != 0 && n != "GND";
    it->setCheckState((premier ? nomme : coches.count(n) > 0) ? Qt::Checked : Qt::Unchecked);
    m_sondes->addItem(it);
  }
  const QString avant = m_regle->currentText();
  m_regle->clear();
  m_regle->addItem(QString());
  for (const auto& x : c->composants)
    if (x.symbole == "R" || x.symbole == "C" || x.symbole == "L" || x.symbole == "Potentiometre") m_regle->addItem(q(x.reference));
  m_regle->setCurrentText(avant);
}

void FenetreSimulation::setAnalyse(const AnalyseSpice& a) {
  m_type->setCurrentIndex(static_cast<int>(a.type));
  const QLocale c = QLocale::c();
  m_pas->setText(c.toString(a.pas, 'g', 6));
  m_duree->setText(c.toString(a.duree, 'g', 6));
  m_fDebut->setText(c.toString(a.fDebut, 'g', 6));
  m_fFin->setText(c.toString(a.fFin, 'g', 6));
  m_source->setText(q(a.source));
  m_dcDebut->setText(c.toString(a.dcDebut, 'g', 6));
  m_dcFin->setText(c.toString(a.dcFin, 'g', 6));
  m_dcPas->setText(c.toString(a.dcPas, 'g', 6));
}

AnalyseSpice FenetreSimulation::analyse() const {
  // Les champs comprennent les suffixes SPICE (10u, 1meg, 2.2k) et la virgule
  auto lire = [](const QLineEdit* e, double defaut) {
    QString t = e->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.'));
    double f = 1;
    const QString m = t.toLower();
    if (m.endsWith(QLatin1String("meg"))) f = 1e6, t.chop(3);
    else if (m.endsWith(QLatin1Char('k'))) f = 1e3, t.chop(1);
    else if (m.endsWith(QLatin1Char('m'))) f = 1e-3, t.chop(1);
    else if (m.endsWith(QLatin1Char('u')) || m.endsWith(QStringLiteral("µ"))) f = 1e-6, t.chop(1);
    else if (m.endsWith(QLatin1Char('n'))) f = 1e-9, t.chop(1);
    else if (m.endsWith(QLatin1Char('p'))) f = 1e-12, t.chop(1);
    else if (m.endsWith(QLatin1Char('g'))) f = 1e9, t.chop(1);
    bool ok = false;
    const double v = t.toDouble(&ok);
    return ok ? v * f : defaut;
  };
  AnalyseSpice a;
  a.type = static_cast<AnalyseSpice::Type>(m_type->currentIndex());
  a.pas = lire(m_pas, 1e-5);
  a.duree = lire(m_duree, 1e-2);
  a.fDebut = lire(m_fDebut, 1);
  a.fFin = lire(m_fFin, 1e6);
  a.source = m_source->text().toStdString();
  a.dcDebut = lire(m_dcDebut, 0);
  a.dcFin = lire(m_dcFin, 5);
  a.dcPas = lire(m_dcPas, 0.1);
  return a;
}

void FenetreSimulation::sonder(const std::vector<std::string>& reseaux) {
  for (int i = 0; i < m_sondes->count(); ++i)
    m_sondes->item(i)->setCheckState(std::find(reseaux.begin(), reseaux.end(), m_sondes->item(i)->text().toStdString()) != reseaux.end() ? Qt::Checked : Qt::Unchecked);
}

ConceptionElec FenetreSimulation::conceptionReglee() const {
  ConceptionElec c = *m_atelier.conceptionCourante();
  if (!m_reglee.empty() && m_facteurRegle != 1)
    for (auto& x : c.composants)
      if (x.reference == m_reglee) {
        const FamilleValeur fa = familleDe(c, x);
        const ValeurComprise v = lireValeurComposant(x.valeur, fa);
        if (v.comprise()) x.valeur = ecrireCompact(v.nombre * m_facteurRegle, fa);
      }
  return c;
}

void FenetreSimulation::lancer() {
  if (!m_atelier.conceptionCourante() || m_enCours) return;
  m_netlist = netlistSpice(conceptionReglee(), analyse());
  m_texteNetlist->setPlainText(q(m_netlist.texte));
  if (!m_netlist.valide()) {
    QString t = QStringLiteral("<span style='color:#c53030'><b>Avant le calcul :</b></span><ul>");
    for (const auto& e : m_netlist.erreurs) t += QStringLiteral("<li>%1</li>").arg(q(e).toHtmlEscaped());
    m_etat->setText(t + QStringLiteral("</ul>"));
    m_resultat = {};
    m_resultat.erreur = m_netlist.erreurs.front();
    emit calculTermine();
    return;
  }
  lancerNetlist(q(m_netlist.texte));
}

void FenetreSimulation::lancerNetlist(const QString& texte) {
  if (m_enCours) return;
  if (m_fil.joinable()) m_fil.join();
  m_enCours = true;
  m_lancer->setEnabled(false);
  m_arreter->setEnabled(true);
  QString avertissements;
  for (const auto& w : m_netlist.avertissements) avertissements += QStringLiteral("<br>· %1").arg(q(w).toHtmlEscaped());
  m_etat->setText(QStringLiteral("Calcul en cours…") + avertissements);
  m_arret = std::make_shared<std::atomic<bool>>(false);
  QPointer<FenetreSimulation> moi(this);
  m_fil = std::thread([moi, arret = m_arret, t = texte.toStdString()] {
    auto r = std::make_shared<ResultatSimulation>(simuler(t, arret.get()));
    QMetaObject::invokeMethod(qApp, [moi, r] {
      if (!moi) return;
      moi->m_enCours = false;
      moi->m_lancer->setEnabled(true);
      moi->m_arreter->setEnabled(false);
      moi->m_resultat = std::move(*r);
      QString console;
      for (const auto& l : moi->m_resultat.console) console += q(l) + QLatin1Char('\n');
      moi->m_console->setPlainText(console);
      moi->m_etat->setText(moi->m_resultat.reussi() ? QStringLiteral("Calcul terminé.") : QStringLiteral("<span style='color:#c53030'>%1</span>").arg(q(moi->m_resultat.erreur).toHtmlEscaped()));
      moi->afficher();
      emit moi->calculTermine();
    }, Qt::QueuedConnection);
  });
}

void FenetreSimulation::arreter() {
  if (m_arret) *m_arret = true;
}

void FenetreSimulation::afficher(bool refaireTrace) {
  const ResultatSimulation& r = m_resultat;
  m_tableMesures->setRowCount(0);
  auto ligne = [this](const QString& a, const QString& b) {
    const int i = m_tableMesures->rowCount();
    m_tableMesures->insertRow(i);
    m_tableMesures->setItem(i, 0, new QTableWidgetItem(a));
    m_tableMesures->setItem(i, 1, new QTableWidgetItem(b));
  };
  if (!r.reussi()) return;
  std::vector<std::string> sondes;
  for (int i = 0; i < m_sondes->count(); ++i)
    if (m_sondes->item(i)->checkState() == Qt::Checked) sondes.push_back(m_sondes->item(i)->text().toStdString());
  // Point de repos : la tension de chaque nœud
  if (r.echelle.empty()) {
    for (const auto& [reseau, nd] : m_netlist.noeuds)
      if (const auto* v = r.reel(vecteurTension(m_netlist, reseau)); v && !v->empty()) ligne(q(reseau), si(v->front()) + QStringLiteral("V"));
    m_onglets->setCurrentWidget(m_tableMesures);
    return;
  }
  const bool ac = r.echelle == "frequency";
  const std::vector<double>* x = r.reel(r.echelle);
  if (!x) return;
  std::vector<TraceCourbes::Courbe> courbes = m_precedentes;
  int teinte = 0;
  for (const auto& s : sondes) {
    const std::string nom = vecteurTension(m_netlist, s);
    if (nom.empty()) continue;
    TraceCourbes::Courbe k;
    k.nom = q(s);
    k.couleur = TEINTES[teinte++ % 6];
    k.x = *x;
    if (ac) {
      if (const auto* h = r.complexe(nom)) {
        k.y = gainDb(*h);
        if (const double fc = frequenceCoupure(*x, *h); fc > 0) ligne(QStringLiteral("%1 : coupure à −3 dB").arg(q(s)), si(fc, 4) + QStringLiteral("Hz"));
        ligne(QStringLiteral("%1 : gain en basse fréquence").arg(q(s)), QLocale().toString(k.y.front(), 'f', 2) + QStringLiteral(" dB"));
      }
    } else if (const auto* v = r.reel(nom)) {
      k.y = *v;
      if (r.echelle == "time")
        if (const double f = frequenceMesuree(*x, *v); f > 0) ligne(QStringLiteral("%1 : fréquence").arg(q(s)), si(f, 4) + QStringLiteral("Hz"));
      const auto [mn, mx] = std::minmax_element(v->begin(), v->end());
      ligne(QStringLiteral("%1 : min / max").arg(q(s)), si(*mn) + QStringLiteral("V / ") + si(*mx) + QStringLiteral("V"));
    }
    if (k.y.empty()) continue;
    ligne(QStringLiteral("%1 : en A / en B").arg(q(s)), si(valeurA(k.x, k.y, m_trace->curseur(0))) + QStringLiteral(" / ") + si(valeurA(k.x, k.y, m_trace->curseur(1))));
    courbes.push_back(std::move(k));
  }
  ligne(QStringLiteral("Curseurs A, B, B − A"), si(m_trace->curseur(0)) + QStringLiteral(", ") + si(m_trace->curseur(1)) + QStringLiteral(", ") + si(m_trace->curseur(1) - m_trace->curseur(0)));
  // Le tracé n'est refait qu'à un nouveau résultat (un curseur glissé ne remet pas les curseurs en place)
  if (refaireTrace)
    m_trace->setCourbes(std::move(courbes), ac ? QStringLiteral("Fréquence (Hz)") : r.echelle == "time" ? QStringLiteral("Temps (s)") : QStringLiteral("Balayage"),
                        ac ? QStringLiteral("Gain (dB)") : QStringLiteral("Tension (V)"), ac);
}

QString FenetreSimulation::mesures() const {
  QString t;
  for (int i = 0; i < m_tableMesures->rowCount(); ++i) t += m_tableMesures->item(i, 0)->text() + QStringLiteral(" = ") + m_tableMesures->item(i, 1)->text() + QLatin1Char('\n');
  return t;
}

void FenetreSimulation::regler(const std::string& reference, double facteur) {
  // La courbe d'avant reste, atténuée, sous la nouvelle
  m_precedentes.clear();
  for (auto k : m_trace->courbes())
    if (!k.attenuee) {
      k.attenuee = true;
      m_precedentes.push_back(std::move(k));
    }
  m_reglee = reference;
  m_facteurRegle = facteur;
  lancer();
}

bool FenetreSimulation::appliquerReglage() {
  if (m_reglee.empty() || m_facteurRegle == 1) return false;
  const ConceptionElec reglee = conceptionReglee();
  std::string valeur;
  for (const auto& x : reglee.composants)
    if (x.reference == m_reglee) valeur = x.valeur;
  const std::string ref = m_reglee;
  const bool fait = m_atelier.modifierConception(
      [&](ConceptionElec& c) {
        for (auto& x : c.composants)
          if (x.reference == ref) x.valeur = valeur;
      },
      QStringLiteral("Valeur réglée en simulation (%1)").arg(q(ref)));
  m_reglee.clear();
  m_facteurRegle = 1;
  m_facteur->blockSignals(true);
  m_facteur->setValue(1);
  m_facteur->blockSignals(false);
  return fait;
}

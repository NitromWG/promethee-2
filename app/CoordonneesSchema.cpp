// Prométhée : coordonnées, réticule, grilles et saisie près du curseur du schéma (lot L2, tâche 2.28, ERG-24,
// ERG-25, ERG-26, SAI-22). La barre d'état dit le point visé et sa place relative à une origine posée d'un clic (ou,
// pendant un tracé, au dernier point fixé), en cartésien ou en polaire. Les grilles sont nommées, à pas X et Y
// distincts ; la grille active accroche fils, symboles et étiquettes, une autre accroche les textes ; deux grilles
// rapides se basculent au bandeau. Pendant un tracé de fil, un chiffre tapé ouvre près du curseur la longueur et
// l'angle du segment suivant (comme la saisie dynamique de l'esquisseur) : Tab passe de l'une à l'autre, Entrée pose.
// Méthodes de VueSchema.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QTableWidget>
#include <QTabletEvent>
#include <QVBoxLayout>

#include "ChampValeur.hpp"
#include "OutilVue.hpp"
#include "VueSchema.hpp"
#include "promethee/unites.hpp"

using namespace prom::ecad;

namespace {
QString mmTexte(double v) { return QLocale().toString(v, 'f', 2); }
// Une touche prise dans un champ de la saisie près du curseur (Échap la referme, Entrée la valide, Tab passe au
// champ voisin) : elle fait son effet et ne va pas plus loin
class EchapSaisie : public QObject {
 public:
  EchapSaisie(QObject* parent, int touche, std::function<void()> f) : QObject(parent), m_touche(touche), m_f(std::move(f)) {}
  bool eventFilter(QObject*, QEvent* e) override {
    if (e->type() != QEvent::KeyPress || static_cast<QKeyEvent*>(e)->key() != m_touche) return false;  // champ de saisie
    m_f();
    return true;
  }

 private:
  int m_touche;
  std::function<void()> m_f;
};
const VueSchema::GrilleNommee GRILLE_DEFAUT{QStringLiteral("Schéma 1,27"), 1.27, 1.27};
}  // namespace

// ---------- Coordonnées ----------
QString VueSchema::texteRelatif(Pt o, Pt p) {
  // Y vers le haut, comme les angles : 90° monte
  const double dx = mm(p.x - o.x), dy = mm(o.y - p.y);
  if (coordonneesPolaires()) {
    double a = std::atan2(dy, dx) * 180.0 / std::numbers::pi;
    if (a < 0) a += 360.0;
    if (std::abs(a - 360.0) < 0.005) a = 0;
    return QStringLiteral("%1 mm à %2°").arg(mmTexte(std::hypot(dx, dy)), QLocale().toString(a, 'f', 1).remove(QRegularExpression(QStringLiteral("[,.]0$"))));
  }
  return QStringLiteral("ΔX %1  ΔY %2 mm").arg(mmTexte(dx), mmTexte(dy));
}

QString VueSchema::texteCoordonnees() const {
  const Pt p = accroche(m_souris);
  QString t = QStringLiteral("X %1  Y %2 mm").arg(mmTexte(mm(p.x)), mmTexte(mm(p.y)));
  if (!m_relatifFige.isEmpty()) return t + QStringLiteral("  ·  ") + m_relatifFige;
  std::optional<Pt> o = dernierPointTrace();
  if (!o) o = m_origineRelative;
  if (o) t += QStringLiteral("  ·  ") + texteRelatif(*o, p);
  if (m_armeOrigine) t += QStringLiteral("  ·  clic : l’origine relative");
  return t;
}

void VueSchema::armerOrigineRelative() {
  m_armeOrigine = true;
  setCursor(Qt::CrossCursor);
  emit message(QStringLiteral("Clic sur le point qui sert d’origine aux coordonnées relatives."));
  emit coordonneesChangees();
}

void VueSchema::effacerOrigineRelative() {
  m_origineRelative.reset();
  m_armeOrigine = false;
  emit coordonneesChangees();
  update();
}

bool VueSchema::coordonneesPolaires() { return QSettings().value(QStringLiteral("schema/coordonneesPolaires"), true).toBool(); }
void VueSchema::setCoordonneesPolaires(bool oui) { QSettings().setValue(QStringLiteral("schema/coordonneesPolaires"), oui); }

VueSchema::Reticule VueSchema::styleReticule() {
  return static_cast<Reticule>(std::clamp(QSettings().value(QStringLiteral("schema/reticule"), 0).toInt(), 0, 2));
}
void VueSchema::setStyleReticule(Reticule r) { QSettings().setValue(QStringLiteral("schema/reticule"), static_cast<int>(r)); }

// ---------- Grilles nommées ----------
std::vector<VueSchema::GrilleNommee> VueSchema::grillesNommees() {
  std::vector<GrilleNommee> r;
  QSettings s;
  const int n = s.beginReadArray(QStringLiteral("schema/grilles"));
  for (int i = 0; i < n; ++i) {
    s.setArrayIndex(i);
    GrilleNommee g{s.value(QStringLiteral("nom")).toString(), s.value(QStringLiteral("pasX"), 1.27).toDouble(), s.value(QStringLiteral("pasY"), 1.27).toDouble()};
    if (!g.nom.isEmpty() && g.pasX > 0 && g.pasY > 0) r.push_back(g);
  }
  s.endArray();
  if (r.empty())
    r = {GRILLE_DEFAUT, {QStringLiteral("Large 2,54"), 2.54, 2.54}, {QStringLiteral("Fine 0,635"), 0.635, 0.635}, {QStringLiteral("Rangées 2,54 × 1,27"), 2.54, 1.27}};
  return r;
}

void VueSchema::setGrillesNommees(const std::vector<GrilleNommee>& g) {
  QSettings s;
  s.beginWriteArray(QStringLiteral("schema/grilles"), static_cast<int>(g.size()));
  for (int i = 0; i < static_cast<int>(g.size()); ++i) {
    s.setArrayIndex(i);
    s.setValue(QStringLiteral("nom"), g[static_cast<std::size_t>(i)].nom);
    s.setValue(QStringLiteral("pasX"), g[static_cast<std::size_t>(i)].pasX);
    s.setValue(QStringLiteral("pasY"), g[static_cast<std::size_t>(i)].pasY);
  }
  s.endArray();
}

VueSchema::GrilleNommee VueSchema::grilleNommee(const QString& nom) {
  for (const auto& g : grillesNommees())
    if (g.nom == nom) return g;
  return GRILLE_DEFAUT;
}

VueSchema::GrilleNommee VueSchema::grilleActive() const { return grilleNommee(m_grille.isEmpty() ? QSettings().value(QStringLiteral("schema/grille"), GRILLE_DEFAUT.nom).toString() : m_grille); }

VueSchema::GrilleNommee VueSchema::grilleTextes() const {
  return grilleNommee(m_grilleTextes.isEmpty() ? QSettings().value(QStringLiteral("schema/grilleTextes"), GRILLE_DEFAUT.nom).toString() : m_grilleTextes);
}

void VueSchema::choisirGrille(const QString& nom) {
  m_grille = grilleNommee(nom).nom;
  QSettings().setValue(QStringLiteral("schema/grille"), m_grille);
  emit message(QStringLiteral("Grille : %1").arg(m_grille));
  ++m_invalidations;
  majBandeau();
  update();
}

void VueSchema::choisirGrilleTextes(const QString& nom) {
  m_grilleTextes = grilleNommee(nom).nom;
  QSettings().setValue(QStringLiteral("schema/grilleTextes"), m_grilleTextes);
}

QString VueSchema::grilleRapide(int i) {
  const QString defaut = i == 0 ? GRILLE_DEFAUT.nom : QStringLiteral("Large 2,54");
  return QSettings().value(QStringLiteral("schema/grilleRapide%1").arg(i + 1), defaut).toString();
}
void VueSchema::setGrilleRapide(int i, const QString& nom) { QSettings().setValue(QStringLiteral("schema/grilleRapide%1").arg(i + 1), nom); }

namespace {
Pt caler(Pt p, double pasX, double pasY) {
  if (std::abs(pasX - 1.27) < 1e-9 && std::abs(pasY - 1.27) < 1e-9) return surGrille(p);  // la grille du schéma, telle quelle
  const double gx = pasX * 1e6, gy = pasY * 1e6;
  return Pt{static_cast<Nm>(std::llround(std::round(static_cast<double>(p.x) / gx) * gx)), static_cast<Nm>(std::llround(std::round(static_cast<double>(p.y) / gy) * gy))};
}
}  // namespace

Pt VueSchema::surGrilleActive(Pt p) const {
  if (domaine() != Domaine::Electronique) return surGrille(p);  // les folios de l'électricien gardent leur grille
  const GrilleNommee g = grilleActive();
  return caler(p, g.pasX, g.pasY);
}

Pt VueSchema::surGrilleTextes(Pt p) const {
  if (domaine() != Domaine::Electronique) return surGrille(p);
  const GrilleNommee g = grilleTextes();
  return caler(p, g.pasX, g.pasY);
}

// La grille au bandeau des outils qui posent : le bouton bascule entre les deux grilles rapides, sa liste les offre toutes
void VueSchema::ajouterGrillesBandeau(ContenuBandeau& c) {
  if (domaine() != Domaine::Electronique) return;
  const GrilleNommee g = grilleActive();
  ActionBandeau b;
  b.id = QStringLiteral("grille");
  b.libelle = QStringLiteral("Grille : %1").arg(g.nom);
  VueSchema* v = this;
  b.faire = [v] {
    const QString actuelle = v->grilleActive().nom, r1 = grilleRapide(0), r2 = grilleRapide(1);
    v->choisirGrille(actuelle == r1 ? r2 : r1);
  };
  for (const auto& x : grillesNommees()) {
    ChoixBandeau ch;
    ch.libelle = x.nom;
    ch.coche = x.nom == g.nom;
    ch.faire = [v, nom = x.nom] { v->choisirGrille(nom); };
    b.choix.push_back(ch);
  }
  // La sortie reste la dernière
  const auto fin = std::find_if(c.actions.begin(), c.actions.end(), [](const ActionBandeau& a) { return a.sortie; });
  c.actions.insert(fin, b);
}

void VueSchema::dialogueGrilles() {
  QDialog d(this);
  d.setObjectName(QStringLiteral("dialogueGrilles"));
  d.setWindowTitle(QStringLiteral("Grilles"));
  auto* l = new QVBoxLayout(&d);
  l->addWidget(new QLabel(QStringLiteral("Grilles nommées : la grille active accroche fils, symboles et étiquettes ; « Textes » accroche les textes.")));
  auto* t = new QTableWidget(0, 3, &d);
  t->setObjectName(QStringLiteral("tableGrilles"));
  t->setHorizontalHeaderLabels({QStringLiteral("Nom"), QStringLiteral("Pas X (mm)"), QStringLiteral("Pas Y (mm)")});
  t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  auto ligne = [t](const GrilleNommee& g) {
    const int r = t->rowCount();
    t->insertRow(r);
    t->setItem(r, 0, new QTableWidgetItem(g.nom));
    t->setItem(r, 1, new QTableWidgetItem(QLocale().toString(g.pasX, 'f', 3)));
    t->setItem(r, 2, new QTableWidgetItem(QLocale().toString(g.pasY, 'f', 3)));
  };
  for (const auto& g : grillesNommees()) ligne(g);
  l->addWidget(t);
  auto* boutons = new QHBoxLayout;
  auto* ajouter = new QPushButton(QStringLiteral("＋ Grille"));
  auto* retirer = new QPushButton(QStringLiteral("Retirer"));
  connect(ajouter, &QPushButton::clicked, &d, [ligne] { ligne({QStringLiteral("Nouvelle grille"), 2.54, 2.54}); });
  connect(retirer, &QPushButton::clicked, &d, [t] {
    if (t->currentRow() >= 0 && t->rowCount() > 1) t->removeRow(t->currentRow());
  });
  boutons->addWidget(ajouter);
  boutons->addWidget(retirer);
  boutons->addStretch(1);
  l->addLayout(boutons);
  auto* fin = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
  connect(fin, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  connect(fin, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  l->addWidget(fin);
  if (d.exec() != QDialog::Accepted) return;
  std::vector<GrilleNommee> g;
  for (int r = 0; r < t->rowCount(); ++r) {
    bool okX = false, okY = false;
    const QString nom = t->item(r, 0) ? t->item(r, 0)->text().trimmed() : QString();
    const double x = t->item(r, 1) ? QLocale().toDouble(t->item(r, 1)->text(), &okX) : 0, y = t->item(r, 2) ? QLocale().toDouble(t->item(r, 2)->text(), &okY) : 0;
    if (!nom.isEmpty() && okX && okY && x > 0.01 && y > 0.01) g.push_back({nom, x, y});
  }
  if (!g.empty()) setGrillesNommees(g);
  ++m_invalidations;
  majBandeau();
  update();
}

// ---------- Saisie près du curseur pendant un tracé ----------
void VueSchema::ouvrirSaisieTrace(const QString& premier) {
  const std::optional<Pt> depart = dernierPointTrace();
  if (!depart) return;
  if (m_saisieTrace) m_saisieTrace->deleteLater();
  auto* f = new QFrame(this);
  f->setObjectName(QStringLiteral("saisieTrace"));
  f->setFrameShape(QFrame::StyledPanel);
  f->setAutoFillBackground(true);
  auto* l = new QHBoxLayout(f);
  l->setContentsMargins(6, 3, 6, 3);
  const prom::Unites unites{};
  auto* longueur = new ChampValeur(false, unites, f);
  longueur->setObjectName(QStringLiteral("saisieLongueur"));
  longueur->setPlaceholderText(QStringLiteral("longueur"));
  longueur->setFixedWidth(90);
  auto* angle = new ChampValeur(true, unites, f);
  angle->setObjectName(QStringLiteral("saisieAngle"));
  angle->setFixedWidth(70);
  // L'angle proposé : la direction du curseur, au quart de tour près en équerre
  const Pt c = accroche(m_souris);
  double a = std::atan2(static_cast<double>(depart->y - c.y), static_cast<double>(c.x - depart->x)) * 180.0 / std::numbers::pi;
  if (m_modeFil % 4 == 0) a = std::round(a / 90.0) * 90.0;
  if (a < 0) a += 360.0;
  angle->setValeur(a);
  l->addWidget(new QLabel(QStringLiteral("Longueur")));
  l->addWidget(longueur);
  l->addWidget(new QLabel(QStringLiteral("Angle")));
  l->addWidget(angle);
  f->adjustSize();
  f->move(std::clamp(static_cast<int>(m_souris.x()) + 16, 4, std::max(4, width() - f->width() - 4)), std::clamp(static_cast<int>(m_souris.y()) + 16, 4, std::max(4, height() - f->height() - 4)));
  f->show();
  f->raise();
  longueur->setText(premier);
  longueur->setFocus(Qt::OtherFocusReason);
  longueur->deselect();
  longueur->setCursorPosition(static_cast<int>(premier.size()));
  m_saisieTrace = f;
  auto fermer = [this, f] {
    f->hide();
    f->deleteLater();
    setFocus();
  };
  auto valider = [this, longueur, angle, fermer] {
    double L = 0, A = 0;
    if (!prom::lireValeur(longueur->text().toStdString(), false, prom::Unites{}, L) || L <= 0) {
      longueur->setStyleSheet(QStringLiteral("QLineEdit { border: 2px solid #C2402E; }"));
      return;
    }
    if (!prom::lireValeur(angle->text().toStdString(), true, prom::Unites{}, A)) {
      angle->setStyleSheet(QStringLiteral("QLineEdit { border: 2px solid #C2402E; }"));
      return;
    }
    const std::optional<Pt> d = dernierPointTrace();
    fermer();
    if (!d) return;
    const double r = A * std::numbers::pi / 180.0;
    const Pt cible = surGrilleActive(Pt{d->x + static_cast<Nm>(std::llround(L * 1e6 * std::cos(r))), d->y - static_cast<Nm>(std::llround(L * 1e6 * std::sin(r)))});
    if (poserPointTrace(cible)) m_relatifFige = texteRelatif(*d, cible);
    emit coordonneesChangees();
    update();
  };
  // Entrée valide la saisie et s'arrête là : laissée filer (QLineEdit l'ignore), elle finirait le fil (« trace.terminer »)
  for (QLineEdit* e : {static_cast<QLineEdit*>(longueur), static_cast<QLineEdit*>(angle)}) {
    e->installEventFilter(new EchapSaisie(e, Qt::Key_Return, valider));
    e->installEventFilter(new EchapSaisie(e, Qt::Key_Enter, valider));
  }
  for (QLineEdit* e : {static_cast<QLineEdit*>(longueur), static_cast<QLineEdit*>(angle)}) e->installEventFilter(new EchapSaisie(e, Qt::Key_Escape, fermer));
  // Tab passe de la longueur à l'angle, Maj+Tab revient : le focus reste dans la saisie, la vue ne le prend pas pour
  // ses réglages d'outil
  auto vers = [](QLineEdit* e) {
    return [e] {
      e->setFocus(Qt::TabFocusReason);
      e->selectAll();
    };
  };
  longueur->installEventFilter(new EchapSaisie(longueur, Qt::Key_Tab, vers(angle)));
  angle->installEventFilter(new EchapSaisie(angle, Qt::Key_Backtab, vers(longueur)));
  angle->installEventFilter(new EchapSaisie(angle, Qt::Key_Tab, vers(longueur)));
}

QFrame* VueSchema::saisieTrace() const { return m_saisieTrace.data(); }

// ---------- Cadrages nommés (2.29) ----------
namespace {
QString cleCadrages(const ConceptionElec* c) { return QStringLiteral("cadrages/") + QString::fromStdString(c ? c->uuid : std::string()); }
}  // namespace

QStringList VueSchema::cadrages() const {
  QStringList r;
  for (const QJsonValue& v : QJsonDocument::fromJson(QSettings().value(cleCadrages(conception())).toByteArray()).array()) r << v.toObject().value(QStringLiteral("nom")).toString();
  return r;
}

void VueSchema::enregistrerCadrage(const QString& nom) {
  const ConceptionElec* c = conception();
  if (!c || nom.trimmed().isEmpty()) return;
  QJsonArray t;
  for (const QJsonValue& v : QJsonDocument::fromJson(QSettings().value(cleCadrages(c)).toByteArray()).array())
    if (v.toObject().value(QStringLiteral("nom")).toString() != nom.trimmed()) t.append(v);
  t.append(QJsonObject{{QStringLiteral("nom"), nom.trimmed()}, {QStringLiteral("feuille"), m_feuille}, {QStringLiteral("x"), m_origine.x()}, {QStringLiteral("y"), m_origine.y()},
                       {QStringLiteral("echelle"), m_echelle}, {QStringLiteral("largeur"), width()}, {QStringLiteral("hauteur"), height()}});
  QSettings().setValue(cleCadrages(c), QJsonDocument(t).toJson(QJsonDocument::Compact));
  emit message(QStringLiteral("Cadrage « %1 » enregistré.").arg(nom.trimmed().toHtmlEscaped()));
}

bool VueSchema::rappelerCadrage(const QString& nom) {
  const ConceptionElec* c = conception();
  if (!c) return false;
  for (const QJsonValue& v : QJsonDocument::fromJson(QSettings().value(cleCadrages(c)).toByteArray()).array()) {
    const QJsonObject o = v.toObject();
    if (o.value(QStringLiteral("nom")).toString() != nom) continue;
    const int f = o.value(QStringLiteral("feuille")).toInt();
    if (f >= 0 && f < static_cast<int>(c->feuilles.size()) && f != m_feuille) setConception(m_conception, f);
    // Le même centre, quelle que soit la taille de la vue aujourd'hui
    const double e = o.value(QStringLiteral("echelle")).toDouble(m_echelle);
    const QPointF centre((o.value(QStringLiteral("largeur")).toDouble(width()) / 2.0 - o.value(QStringLiteral("x")).toDouble()) / e,
                         (o.value(QStringLiteral("hauteur")).toDouble(height()) / 2.0 - o.value(QStringLiteral("y")).toDouble()) / e);
    deplacerVue(QPointF(width() / 2.0, height() / 2.0) - centre * e, e);
    return true;
  }
  return false;
}

void VueSchema::deplacerVue(QPointF origine, double echelle) {
  m_origine = origine;
  m_echelle = std::clamp(echelle, 0.4, 80.0);
  emit vueChangee();
  update();
}

// Stylet (2.29) : la pointe vaut le bouton gauche (Qt en fait des événements de souris) ; le bouton du fût ouvre le
// menu de l'objet visé
void VueSchema::tabletEvent(QTabletEvent* e) {
  if (e->type() == QEvent::TabletPress && (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton)) {
    m_souris = e->position();
    menuContextuel(e->position());
    e->accept();
    return;
  }
  e->ignore();
}

// Prométhée : éditeur d'esquisse 2D. Licence GPL-3.0-only.
#include "Esquisseur.hpp"
#include "NavigationVue.hpp"
#include "Theme.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QWheelEvent>
#include "ChampValeur.hpp"
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QLabel>
#include "Icones.hpp"
#include <map>
#include <set>
#include <functional>
#include <QToolButton>
#include <QTimer>
#include <QHBoxLayout>
#include <QFrame>
#include <QAbstractItemView>

using namespace prom;

namespace {
// Encres du dessin en cours, prises au thème (prendreEncres) : feuille de l'esquisse, ou vue 3D sous l'esquisse
QColor FOND, GRILLE, GRILLE_FORTE, AXE_X, AXE_Y, LIBRE, CONTRAINT, CONSTRUCTION, CHOISI, SURVOL, APERCU, CONFLIT, ENCRE, COTE, REFERENCE, BULLE, BULLE_BORD, BULLE_TEXTE;
bool g_encresSombres = false;
void prendreEncres(bool surVue3D) {
  const EncresEsquisse& e = theme().esquisse(surVue3D);
  FOND = e.fond, GRILLE = e.grille, GRILLE_FORTE = e.grilleForte, AXE_X = e.axeX, AXE_Y = e.axeY, LIBRE = e.libre, CONTRAINT = e.contraint;
  CONSTRUCTION = e.construction, CHOISI = e.choisi, SURVOL = e.survol, APERCU = e.apercu, CONFLIT = e.conflit, ENCRE = e.encre, COTE = e.cote;
  REFERENCE = e.reference, BULLE = e.bulle, BULLE_BORD = e.bulleBord, BULLE_TEXTE = e.bulleTexte;
  g_encresSombres = e.encre.lightness() > 128;
}
// Couleur lisible sur le fond de l'esquisse (couleurs de calque, couleurs d'état)
QColor lisible(const QColor& c) { return g_encresSombres ? Theme::pourFondSombre(c) : c; }
QColor alpha(QColor c, int a) {
  c.setAlpha(a);
  return c;
}
// Texte posé sur un fond plein : sombre sur fond clair, blanc sur fond foncé
QColor surFond(const QColor& fond) { return fond.lightness() > 150 ? QColor(0x10, 0x14, 0x2A) : QColor(Qt::white); }
constexpr double PI = std::numbers::pi;

double distSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
  const double vx = b.x() - a.x(), vy = b.y() - a.y(), l2 = vx * vx + vy * vy;
  double t = l2 > 0 ? ((p.x() - a.x()) * vx + (p.y() - a.y()) * vy) / l2 : 0;
  t = std::clamp(t, 0.0, 1.0);
  return std::hypot(p.x() - (a.x() + t * vx), p.y() - (a.y() + t * vy));
}
QPointF unitaire(QPointF v) {
  const double l = std::hypot(v.x(), v.y());
  return l > 1e-12 ? v / l : QPointF(1, 0);
}

// Mot de commande sans accents ni majuscules (« congé » = « conge », « Égal » = « egal »)
QString motCommande(const QString& m) {
  QString r;
  for (const QChar c : m.normalized(QString::NormalizationForm_D))
    if (c.category() != QChar::Mark_NonSpacing) r += c.toLower();
  return r;
}

// Tables uniques des outils et des relations de la ligne de commande : la complétion en est tirée (EI-17)
const std::map<QString, Esquisseur::Outil>& tableOutils() {
  using O = Esquisseur::Outil;
  static const std::map<QString, O> t = {
      {"sel", O::Selection}, {"selection", O::Selection}, {"l", O::Ligne}, {"ligne", O::Ligne}, {"lc", O::LigneConstruction}, {"lm", O::LigneMilieu},
      {"r", O::Rectangle}, {"rect", O::Rectangle}, {"rectangle", O::Rectangle}, {"rc", O::RectangleCentre}, {"r3", O::Rectangle3Points},
      {"pgm", O::Parallelogramme}, {"parallelogramme", O::Parallelogramme}, {"c", O::Cercle}, {"cercle", O::Cercle}, {"c3", O::Cercle3Points},
      {"a", O::Arc}, {"arc", O::Arc}, {"a3", O::Arc3Points}, {"at", O::ArcTangent}, {"o", O::Rainure}, {"rainure", O::Rainure},
      {"pol", O::Polygone}, {"polygone", O::Polygone}, {"p", O::Point}, {"point", O::Point}, {"d", O::Cote}, {"cote", O::Cote},
      {"j", O::Conge}, {"conge", O::Conge}, {"chanfrein", O::ChanfreinEsquisse}, {"ajuster", O::Ajuster}, {"prolonger", O::Prolonger}};
  return t;
}
const std::map<QString, TypeContrainte>& tableRelations() {
  using T = TypeContrainte;
  static const std::map<QString, T> t = {
      {"h", T::Horizontal}, {"horizontale", T::Horizontal}, {"v", T::Vertical}, {"verticale", T::Vertical},
      {"coin", T::Coincidence}, {"coinc", T::Coincidence}, {"coincidence", T::Coincidence}, {"par", T::Parallele}, {"para", T::Parallele}, {"parallele", T::Parallele},
      {"perp", T::Perpendiculaire}, {"perpendiculaire", T::Perpendiculaire}, {"egal", T::Egal}, {"egale", T::Egal}, {"tan", T::Tangente}, {"tang", T::Tangente},
      {"tangente", T::Tangente}, {"fixe", T::Fixe}, {"fixer", T::Fixe}, {"milieu", T::Milieu}, {"sym", T::Symetrie}, {"symetrie", T::Symetrie},
      {"conc", T::Concentrique}, {"concentrique", T::Concentrique}, {"col", T::Colineaire}, {"colineaire", T::Colineaire}};
  return t;
}
const QStringList& motsDivers() {
  static const QStringList m = {QStringLiteral("x"), QStringLiteral("construction"), QStringLiteral("ok"), QStringLiteral("fin"), QStringLiteral("terminer"),
                                QStringLiteral("annuler"), QStringLiteral("aide"), QStringLiteral("?"), QStringLiteral("=")};
  return m;
}

// Découpe la suite d'une commande en expressions : une unité ou un opérateur se rattache à ce qui le précède
// (« 2.5 cm » et « 40 + 10 » sont une seule valeur, EI-18)
QStringList expressions(const QStringList& mots) {
  static const QStringList unitesConnues = {QStringLiteral("mm"), QStringLiteral("cm"), QStringLiteral("m"), QStringLiteral("in"), QStringLiteral("po"),
                                            QStringLiteral("pouce"), QStringLiteral("pouces"), QStringLiteral("\""), QStringLiteral("°"), QStringLiteral("deg"),
                                            QStringLiteral("rad"), QStringLiteral("ft"), QStringLiteral("pi")};
  QStringList r;
  bool lier = false;
  for (const QString& m : mots) {
    const bool operateur = m == QStringLiteral("+") || m == QStringLiteral("-") || m == QStringLiteral("*") || m == QStringLiteral("/");
    const bool debutOperateur = m.startsWith(QLatin1Char('+')) || m.startsWith(QLatin1Char('*')) || m.startsWith(QLatin1Char('/')) || m.startsWith(QLatin1Char(')'));
    const bool unite = unitesConnues.contains(m);
    if (!r.isEmpty() && (lier || operateur || debutOperateur || unite)) r.back() += QLatin1Char(' ') + m;
    else r << m;
    const QChar dernier = m.isEmpty() ? QChar() : m.back();
    lier = operateur || dernier == QLatin1Char('+') || dernier == QLatin1Char('-') || dernier == QLatin1Char('*') || dernier == QLatin1Char('/') || dernier == QLatin1Char('(');
  }
  return r;
}
}  // namespace

Esquisseur::Esquisseur(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMinimumSize(300, 240);
  m_editeur = new QLineEdit(this);
  m_editeur->hide();
  m_editeur->setAlignment(Qt::AlignCenter);
  m_editeur->setStyleSheet(QStringLiteral("QLineEdit { border: 2px solid #2F6BF5; border-radius: 4px; padding: 2px; font-weight: 600; background: white; }"));
  connect(m_editeur, &QLineEdit::editingFinished, this, &Esquisseur::validerEditeur);
  m_barreContexte = new QFrame(this);
  m_barreContexte->setObjectName(QStringLiteral("barreContexte"));
  m_barreContexte->setStyleSheet(QStringLiteral("#barreContexte { background: rgba(255,255,255,240); border: 1px solid #CDD3E4; border-radius: 9px; }"
                                                "QToolButton { border: none; border-radius: 6px; padding: 3px; } QToolButton:hover { background: #E9EEFF; }"));
  m_barreContexte->hide();
  m_commande = new QLineEdit(this);
  {
    QStringList mots;
    for (const auto& [m, o] : tableOutils()) mots << m;
    for (const auto& [m, t] : tableRelations()) mots << m;
    mots << motsDivers();
    mots.sort();
    auto* completion = new QCompleter(mots, m_commande);
    completion->setCaseSensitivity(Qt::CaseInsensitive);
    completion->setCompletionMode(QCompleter::PopupCompletion);
    m_commande->setCompleter(completion);
  }
  m_commande->hide();
  m_commande->installEventFilter(this);  // Échap et perte du focus la referment (EI-25)
  m_editeur->installEventFilter(this);   // Échap abandonne la saisie de la cote
  m_commande->setPlaceholderText(QStringLiteral("Commande : c 50 (cercle Ø 50), r 40 30, rc, l, a, d (cote), h, v, x (construction), ok, ? pour l'aide"));
  m_commande->setStyleSheet(QStringLiteral("QLineEdit { border: 2px solid #5B4BF5; border-radius: 8px; padding: 6px 10px; font-size: 14px; background: white; }"));
  connect(m_commande, &QLineEdit::returnPressed, this, [this] {
    QString message;
    const bool ok = executerCommande(m_commande->text(), &message);
    m_commande->clear();
    m_commande->hide();
    setFocus();
    if (!message.isEmpty()) emit etatChange(message, ok ? 1 : 2);
  });
  m_minuterieRadial = new QTimer(this);
  m_minuterieRadial->setSingleShot(true);
  m_minuterieRadial->setInterval(260);
  connect(m_minuterieRadial, &QTimer::timeout, this, [this] {
    if (m_geste != Geste::Vue || m_bouge || m_vueBouton != Qt::RightButton) return;
    m_radialVisible = true;
    m_centreRadial = m_depart;
    m_secteur = -1;
    update();
  });
}

void Esquisseur::setProjection(std::optional<Projection> p) {
  m_proj = std::move(p);
  setAttribute(Qt::WA_OpaquePaintEvent, !m_proj.has_value());
  setAttribute(Qt::WA_TranslucentBackground, m_proj.has_value());
  m_cadre = m_proj.has_value();
  update();
}

// En contexte, l'échelle (pixels par millimètre) se lit sur la projection, au voisinage de l'origine.
void Esquisseur::majEchelle() const {
  if (!m_proj) return;
  const QPointF a = m_proj->versEcran(0, 0), b = m_proj->versEcran(1, 0), c = m_proj->versEcran(0, 1);
  m_s = std::max(0.05, std::max(std::hypot(b.x() - a.x(), b.y() - a.y()), std::hypot(c.x() - a.x(), c.y() - a.y())));
}

void Esquisseur::cercle(QPainter& g, const Point& c, double r) const {
  QPainterPath chemin;
  const int n = 96;
  for (int i = 0; i <= n; ++i) {
    const QPointF q = ecran(c.x + r * std::cos(2 * PI * i / n), c.y + r * std::sin(2 * PI * i / n));
    if (i == 0) chemin.moveTo(q); else chemin.lineTo(q);
  }
  g.drawPath(chemin);
}

Point Esquisseur::pt(int id) const {
  if (id == 0) return {0, 0};
  const PointEsquisse* p = m_e.point(id);
  return p ? Point{p->x, p->y} : Point{0, 0};
}

void Esquisseur::editer(const Esquisse& e) {
  // Ce qui appartenait à l'esquisse précédente ne passe pas à celle-ci (EI-06, EI-19, EI-29)
  m_pointsOutil.clear();
  m_editeur->hide();
  m_coteEditee = 0;
  m_texteInitialEditeur.clear();
  m_commande->clear();
  m_commande->hide();
  m_surbrillanceElements.clear();
  m_surbrillancePoints.clear();
  m_survol = {};
  m_aDernierClic = false;
  m_dernierClicPoint = -1;
  m_calqueActif.clear();
  m_arcPrecedent = 0;
  m_geste = Geste::Aucun;
  m_apercu.reset();
  m_e = e;
  m_selection.clear();
  finirOutil();
  m_annulations.clear();
  m_retablissements.clear();
  m_etat = resoudre(m_e);
  m_stable = m_e;
  majReferencesExternes();
  majBarreContexte();
  emit historiqueChange();
  m_cadre = false;
  recadrer();
  emit etatChange(QString::fromStdString(m_etat.message), !m_etat.enConflit.empty() ? 2 : m_etat.degresLiberte > 0 ? 1 : 0);
}

void Esquisseur::recadrer() {
  // En contexte, recadrer pilote la caméra de la vue 3D ; on note que c'est fait, sinon chaque dessin
  // recadrerait et annulerait le zoom et le déplacement de l'utilisateur.
  if (m_proj) { m_cadre = true; m_proj->recadrer(); return; }
  if (width() < 10) return;
  double x0 = -10, x1 = 50, y0 = -10, y1 = 40;
  if (m_e.points.size() > 1) {
    x0 = y0 = 0; x1 = y1 = 0;  // l'origine reste dans le cadre
    for (const auto& p : m_e.points) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    for (const auto& el : m_e.elements)
      if (el.type == TypeElement::Cercle) {
        const Point c = pt(el.centre);
        x0 = std::min(x0, c.x - el.rayon); x1 = std::max(x1, c.x + el.rayon); y0 = std::min(y0, c.y - el.rayon); y1 = std::max(y1, c.y + el.rayon);
      }
    const double m = std::max({x1 - x0, y1 - y0, 10.0}) * 0.2;
    x0 -= m; x1 += m; y0 -= m; y1 += m;
  }
  m_s = std::clamp(std::min(width() / (x1 - x0), height() / (y1 - y0)), 0.2, 200.0);
  m_ox = width() / 2.0 - (x0 + x1) / 2 * m_s;
  m_oy = height() / 2.0 + (y0 + y1) / 2 * m_s;
  m_cadre = true;
  update();
}

void Esquisseur::resizeEvent(QResizeEvent*) { if (!m_cadre) recadrer(); }

void Esquisseur::finirOutil() {
  m_dyn = SaisieDynamique{};
  m_clics.clear();
  m_libres.clear();
  m_libresPoints.clear();
  m_coteRefs.clear();
  m_arcPrecedent = 0;
  retirerPointsOutil();
}

void Esquisseur::retirerPointsOutil() {
  // Un tracé interrompu ne laisse pas de point isolé : les points créés par l'outil qu'aucun élément n'utilise
  // disparaissent, avec leurs relations d'accroche (EI-08)
  std::vector<int> aRetirer;
  for (int id : m_pointsOutil) {
    if (id <= 0 || !m_e.point(id)) continue;
    const bool utilise = std::any_of(m_e.elements.begin(), m_e.elements.end(), [id](const ElementEsquisse& el) { return el.p1 == id || el.p2 == id || el.centre == id; });
    if (!utilise) aRetirer.push_back(id);
  }
  m_pointsOutil.clear();
  if (aRetirer.empty()) return;
  bool dejaEnregistre = false;
  auto retirer = [&aRetirer](Esquisse& e) {
    for (int id : aRetirer) {
      e.points.erase(std::remove_if(e.points.begin(), e.points.end(), [id](const PointEsquisse& q) { return q.id == id; }), e.points.end());
      e.contraintes.erase(std::remove_if(e.contraintes.begin(), e.contraintes.end(), [id](const ContrainteEsquisse& c) { return std::find(c.refs.begin(), c.refs.end(), id) != c.refs.end(); }),
                          e.contraintes.end());
    }
  };
  for (int id : aRetirer) dejaEnregistre = dejaEnregistre || m_stable.point(id) != nullptr;
  retirer(m_e);
  if (dejaEnregistre) retirer(m_stable);
  std::erase_if(m_selection, [this](const Cible& c) { return !cibleExiste(c); });
  m_etat = resoudre(m_e);
  update();
  if (dejaEnregistre) emit modifiee();
}

void Esquisseur::setOutil(Outil o) {
  if (m_coteEditee) validerEditeur();
  m_outil = o;
  finirOutil();
  setCursor(o == Outil::Selection ? Qt::ArrowCursor : Qt::CrossCursor);
  emit outilChange(o);
  const char* consigne = "";
  switch (o) {
    case Outil::Selection: consigne = "Sélection : clic ou cadre (de gauche à droite : éléments entièrement dedans ; de droite à gauche : éléments touchés). Glisse pour déplacer."; break;
    case Outil::Ligne: consigne = "Ligne : clique les points successifs, ou tape la longueur (Tab : l'angle, Entrée : pose cotée) ; double-clic, clic droit ou Échap pour finir (Alt : sans relation H ou V)."; break;
    case Outil::LigneConstruction: consigne = "Ligne de construction : géométrie d'aide, hors profil."; break;
    case Outil::LigneMilieu: consigne = "Ligne par le milieu : clique le milieu, puis une extrémité."; break;
    case Outil::Rectangle: consigne = "Rectangle par deux coins opposés ; après le premier, tape la largeur (Tab : la hauteur, Entrée : pose cotée)."; break;
    case Outil::RectangleCentre: consigne = "Rectangle par le centre : clique le centre, puis un coin."; break;
    case Outil::Rectangle3Points: consigne = "Rectangle incliné : deux points pour un côté, un troisième pour la largeur."; break;
    case Outil::Parallelogramme: consigne = "Parallélogramme : trois sommets consécutifs."; break;
    case Outil::Cercle: consigne = "Cercle : centre, puis un point du cercle, ou tape le diamètre et Entrée."; break;
    case Outil::Cercle3Points: consigne = "Cercle par trois points de son contour."; break;
    case Outil::Arc: consigne = "Arc : centre, début, puis fin (sens inverse des aiguilles d'une montre)."; break;
    case Outil::Arc3Points: consigne = "Arc par trois points : début, fin, puis un point de passage."; break;
    case Outil::ArcTangent: consigne = "Arc tangent : clique l'extrémité d'une ligne ou d'un arc, puis l'arrivée."; break;
    case Outil::Rainure: consigne = "Rainure : premier centre, second centre, puis la largeur."; break;
    case Outil::Polygone: consigne = "Polygone : centre, puis un sommet."; break;
    case Outil::Point: consigne = "Point : clique où le poser."; break;
    case Outil::Cote: consigne = "Cote : clique un ou deux éléments (points, lignes, cercles, origine, axes), puis clique pour placer la cote et tape la valeur."; break;
    case Outil::Conge: consigne = "Congé d'esquisse : clique le coin entre deux lignes, puis tape le rayon."; break;
    case Outil::ChanfreinEsquisse: consigne = "Chanfrein d'esquisse : règle la distance en haut à gauche, puis clique le coin entre deux lignes."; break;
    case Outil::RainureArc: consigne = "Rainure en arc : règle la largeur en haut à gauche, puis clique le centre de l'arc, le début et la fin (sens direct)."; break;
    case Outil::Ajuster: consigne = "Ajuster : clique le morceau d'élément à retirer, entre deux intersections (un élément isolé disparaît)."; break;
    case Outil::Prolonger: consigne = "Prolonger : clique une ligne près de l'extrémité à allonger jusqu'au prochain élément."; break;
  }
  emit etatChange(QString::fromUtf8(consigne), 1);
  majReglageOutil();
  update();
}

// Réglage de l'outil (U1) : distance du chanfrein, largeur de la rainure en arc, dans un champ posé sur la vue, avec
// les unités du document, réglé avant les clics : plus de boîte modale en millimètres
void Esquisseur::majReglageOutil() {
  const bool chanfrein = m_outil == Outil::ChanfreinEsquisse, rainure = m_outil == Outil::RainureArc;
  if (!chanfrein && !rainure) {
    if (m_reglageOutil) m_reglageOutil->hide();
    return;
  }
  if (!m_reglageOutil) {
    m_reglageOutil = new QFrame(this);
    m_reglageOutil->setObjectName(QStringLiteral("reglageOutil"));
    m_reglageOutil->setStyleSheet(QStringLiteral("#reglageOutil { background: rgba(255,255,255,240); border: 1px solid #CDD3E4; border-radius: 8px; } QLabel { color: #10142A; }"));
    auto* l = new QHBoxLayout(m_reglageOutil);
    l->setContentsMargins(8, 4, 8, 4);
    m_libelleReglage = new QLabel;
    l->addWidget(m_libelleReglage);
    m_champReglage = new ChampValeur(false, m_unites);
    m_champReglage->setObjectName(QStringLiteral("champReglageOutil"));
    m_champReglage->setBornes(0.001, 1e5);
    m_champReglage->setFixedWidth(110);
    l->addWidget(m_champReglage);
    connect(m_champReglage, &ChampValeur::valeurChangee, this, [this](double v) {
      if (m_outil == Outil::ChanfreinEsquisse) m_distanceChanfrein = v;
      else if (m_outil == Outil::RainureArc) m_largeurRainureArc = v;
      update();
    });
  }
  m_champReglage->setUnites(m_unites);
  m_libelleReglage->setText(chanfrein ? QStringLiteral("Distance du chanfrein") : QStringLiteral("Largeur de la rainure"));
  m_champReglage->setValeur(chanfrein ? m_distanceChanfrein : m_largeurRainureArc);
  m_reglageOutil->adjustSize();
  m_reglageOutil->move(12, 12);
  m_reglageOutil->show();
  m_reglageOutil->raise();
}

bool Esquisseur::selectionne(const Cible& c) const { return std::find(m_selection.begin(), m_selection.end(), c) != m_selection.end(); }

void Esquisseur::apresModification(bool signaler) {
  m_surbrillanceElements.clear();
  m_surbrillancePoints.clear();
  // Mode construction : ce qui vient d'être tracé devient géométrie d'aide.
  for (auto& el : m_e.elements)
    if (!m_stable.element(el.id)) {
      if (m_modeConstruction) el.construction = true;
      if (!m_calqueActif.empty() && el.calque.empty()) el.calque = m_calqueActif;
    }
  m_etat = resoudre(m_e);
  if (signaler && !aChange()) signaler = false;  // rien n'a changé : pas d'étape d'annulation vide (EI-16)
  if (signaler) {
    m_annulations.push_back(m_stable);
    if (m_annulations.size() > 300) m_annulations.erase(m_annulations.begin());
    m_retablissements.clear();
    m_stable = m_e;
    emit historiqueChange();
  }
  purgerReferences();
  majReferencesExternes();
  majBarreContexte();
  update();
  emit etatChange(QString::fromStdString(m_etat.message), !m_etat.enConflit.empty() ? 2 : m_etat.degresLiberte > 0 ? 1 : 0);
  if (signaler) emit modifiee();
}

bool Esquisseur::aChange() const { return esquisseVersJson(m_e) != esquisseVersJson(m_stable); }

void Esquisseur::majReferencesExternes() {
  // Une arête de la pièce n'est plus proposée quand l'esquisse en a déjà une copie (mêmes extrémités, ou même
  // cercle) ; une annulation qui retire la copie la rend de nouveau disponible
  m_ext = m_extTout;
  m_ext.elements.clear();
  m_extNoms.clear();
  auto P = [this](int id) { const PointEsquisse* q = m_extTout.point(id); return q ? Point{q->x, q->y} : Point{0, 0}; };
  auto proche = [](const Point& a, const Point& b) { return std::hypot(a.x - b.x, a.y - b.y) < 1e-6; };
  auto dejaRepris = [&](const ElementEsquisse& x) {
    for (const auto& el : m_e.elements) {
      if (el.type != x.type) continue;
      if (x.type == TypeElement::Ligne) {
        const Point a = P(x.p1), b = P(x.p2), c = pt(el.p1), d = pt(el.p2);
        if ((proche(a, c) && proche(b, d)) || (proche(a, d) && proche(b, c))) return true;
      } else if (x.type == TypeElement::Cercle) {
        if (proche(P(x.centre), pt(el.centre)) && std::abs(x.rayon - m_e.rayonDe(el)) < 1e-6) return true;
      } else if (proche(P(x.centre), pt(el.centre)) && proche(P(x.p1), pt(el.p1)) && proche(P(x.p2), pt(el.p2))) {
        return true;
      }
    }
    return false;
  };
  for (std::size_t i = 0; i < m_extTout.elements.size(); ++i) {
    if (dejaRepris(m_extTout.elements[i])) continue;
    m_ext.elements.push_back(m_extTout.elements[i]);
    m_extNoms.push_back(i < m_extNomsTout.size() ? m_extNomsTout[i] : std::string());
  }
  std::erase_if(m_externesChoisies, [&](int k) { return k >= static_cast<int>(m_ext.elements.size()); });
}

void Esquisseur::choisirExterne(int indice, bool ajouter) {
  if (indice < 0 || indice >= static_cast<int>(m_ext.elements.size())) return;
  if (!ajouter) m_externesChoisies.clear();
  if (!m_externesChoisies.insert(indice).second && ajouter) m_externesChoisies.erase(indice);
  emit etatChange(QStringLiteral("%1 arête(s) de la pièce choisie(s) : « Convertir les entités » les reprend (double-clic : une seule).").arg(m_externesChoisies.size()), 0);
  update();
}

int Esquisseur::convertirExternes() {
  std::vector<int> indices(m_externesChoisies.begin(), m_externesChoisies.end());
  if (indices.empty())
    for (int i = 0; i < static_cast<int>(m_ext.elements.size()); ++i) indices.push_back(i);
  const Esquisse source = m_ext;
  const std::vector<std::string> noms = m_extNoms;
  int n = 0;
  for (int i : indices)
    if (i >= 0 && i < static_cast<int>(source.elements.size()) && importerElementExterne(source.elements[static_cast<size_t>(i)], source, i < static_cast<int>(noms.size()) ? noms[static_cast<size_t>(i)] : std::string())) ++n;
  m_externesChoisies.clear();
  if (n) apresModification();  // les arêtes reprises ne sont plus proposées ; Ctrl+Z les propose de nouveau (EI-26)
  return n;
}

bool Esquisseur::coteRedondante(const Esquisse& avecCote, int id, bool resolvaitAvant) const {
  // Une cote n'est « de référence » que si elle est vraiment déjà imposée : une esquisse déjà en conflit avant la
  // pose ne rend pas redondante toute nouvelle cote (EI-20)
  Esquisse essai = avecCote;
  const EtatEsquisse s = resoudre(essai);
  if (std::find(s.redondantes.begin(), s.redondantes.end(), id) != s.redondantes.end()) return true;
  return resolvaitAvant && !s.resolue;
}

bool Esquisseur::cibleExiste(const Cible& c) const {
  switch (c.type) {
    case Cible::Axe:
    case Cible::Externe: return true;
    case Cible::Point: return c.id == 0 || m_e.point(c.id) != nullptr;
    case Cible::Element: return m_e.element(c.id) != nullptr;
    case Cible::Contrainte: return std::any_of(m_e.contraintes.begin(), m_e.contraintes.end(), [&](const ContrainteEsquisse& k) { return k.id == c.id; });
    case Cible::Rien: return false;
  }
  return false;
}

void Esquisseur::purgerReferences() {
  // Ce que l'esquisseur tient en main (sélection, clics de l'outil, cote en attente) ne désigne que des objets
  // existants : un outil dont un clic a disparu est interrompu plutôt que de repartir d'un point fantôme
  std::erase_if(m_selection, [this](const Cible& c) { return !cibleExiste(c); });
  const bool clicPerdu = std::any_of(m_clics.begin(), m_clics.end(), [this](int id) { return id != 0 && !m_e.point(id); });
  const bool refPerdue = std::any_of(m_coteRefs.begin(), m_coteRefs.end(), [this](const Cible& c) { return !cibleExiste(c); });
  if (clicPerdu || refPerdue) finirOutil();
#ifndef NDEBUG
  // Intégrité des références de l'esquisse elle-même (mode débogage)
  for (const auto& el : m_e.elements)
    for (int id : {el.p1, el.p2, el.centre})
      if (id > 0) Q_ASSERT_X(m_e.point(id), "Esquisseur", "élément qui vise un point absent");
#endif
}

void Esquisseur::annulerEtape() {
  if (m_annulations.empty()) return;
  m_pointsOutil.clear();
  m_retablissements.push_back(m_e);
  m_e = m_annulations.back();
  m_annulations.pop_back();
  m_stable = m_e;
  m_selection.clear();
  finirOutil();
  m_etat = resoudre(m_e);
  majReferencesExternes();
  majBarreContexte();
  update();
  emit historiqueChange();
  emit etatChange(QStringLiteral("Annulé (Ctrl+Y pour rétablir)."), 1);
  emit modifiee();
}

void Esquisseur::retablirEtape() {
  if (m_retablissements.empty()) return;
  m_pointsOutil.clear();
  m_annulations.push_back(m_e);
  m_e = m_retablissements.back();
  m_retablissements.pop_back();
  m_stable = m_e;
  m_selection.clear();
  finirOutil();
  m_etat = resoudre(m_e);
  majReferencesExternes();
  majBarreContexte();
  update();
  emit historiqueChange();
  emit etatChange(QStringLiteral("Rétabli."), 1);
  emit modifiee();
}

void Esquisseur::setModeConstruction(bool oui) {
  m_modeConstruction = oui;
  emit modeConstructionChange(oui);
  emit etatChange(oui ? QStringLiteral("Mode construction : ce que tu traces devient géométrie d'aide (X pour revenir).") : QStringLiteral("Mode construction désactivé."), 1);
  update();
}

std::vector<int> Esquisseur::cotes() const {
  std::vector<int> r;
  for (const auto& c : m_e.contraintes) if (estCote(c.type)) r.push_back(c.id);
  return r;
}

void Esquisseur::changerValeurCote(int id, double valeur) {
  for (auto& c : m_e.contraintes)
    if (c.id == id) {
      c.valeur = valeurReecrite(c, valeur);
      c.reference = false;
      apresModification();
      return;
    }
}

QString Esquisseur::texteContrainte(const ContrainteEsquisse& c) const {
  auto nombre = [this](double v, bool angle = false) {
    return QString::fromStdString(ecrireValeur(std::abs(v), angle, m_unites, false));
  };
  QString t;
  switch (c.type) {
    case TypeContrainte::Distance: case TypeContrainte::Longueur: case TypeContrainte::DistanceX: case TypeContrainte::DistanceY:
      t = nombre(c.reference ? mesurer(m_e, c) : c.valeur); break;
    case TypeContrainte::Rayon: t = QStringLiteral("R") + nombre(c.reference ? mesurer(m_e, c) : c.valeur); break;
    case TypeContrainte::Diametre: t = QStringLiteral("Ø") + nombre(c.reference ? mesurer(m_e, c) : c.valeur); break;
    case TypeContrainte::Angle: t = nombre(std::abs(c.reference ? mesurer(m_e, c) : c.valeur), true) + QString::fromUtf8(m_unites.angle == UniteAngle::Radian ? " rad" : "°"); break;
    case TypeContrainte::Horizontal: return QStringLiteral("H");
    case TypeContrainte::Vertical: return QStringLiteral("V");
    case TypeContrainte::Parallele: return QStringLiteral("∥");
    case TypeContrainte::Perpendiculaire: return QStringLiteral("⊥");
    case TypeContrainte::Tangente: return QStringLiteral("T");
    case TypeContrainte::Egal: return QStringLiteral("=");
    case TypeContrainte::Concentrique: return QStringLiteral("◎");
    case TypeContrainte::Colineaire: return QStringLiteral("≡");
    case TypeContrainte::Fixe: return QStringLiteral("⚓");
    case TypeContrainte::PointSur: return QStringLiteral("∈");
    case TypeContrainte::Milieu: return QStringLiteral("M");
    case TypeContrainte::Symetrie: return QStringLiteral("⇋");
    case TypeContrainte::Coincidence: return QString();
  }
  if (c.reference) return QStringLiteral("(") + t + QStringLiteral(")");
  // Cote qui suit une variable : l'expression est montrée avec sa valeur (« L/2 = 25 »)
  if (!c.expression.empty()) return QString::fromStdString(c.expression) + QStringLiteral(" = ") + t;
  return t;
}

// Position par défaut d'une étiquette : près de ce qu'elle concerne ; ou celle choisie par l'utilisateur.
QPointF Esquisseur::positionEtiquette(const ContrainteEsquisse& c) const {
  if (c.place) return ecran(c.lx, c.ly);
  if (c.refs.empty()) return {};
  auto milieuLigne = [&](int id, double decalage) -> QPointF {
    if (id == AXE_H) return ecran(0, 0) + QPointF(60, decalage);
    if (id == AXE_V) return ecran(0, 0) + QPointF(decalage, -60);
    const ElementEsquisse* l = m_e.element(id);
    if (!l) return {};
    const QPointF A = ecran(pt(l->p1)), B = ecran(pt(l->p2));
    const QPointF u = unitaire(B - A), n(-u.y(), u.x());
    return (A + B) / 2 + n * decalage;
  };
  const int r0 = c.refs[0];
  if (estCote(c.type)) {
    if (c.type == TypeContrainte::Rayon || c.type == TypeContrainte::Diametre) {
      const ElementEsquisse* el = m_e.element(r0);
      if (!el) return {};
      const double r = m_e.rayonDe(*el) * m_s;
      return ecran(pt(el->centre)) + QPointF(r * 0.75 + 18, -r * 0.75 - 18);
    }
    if (c.type == TypeContrainte::Angle) {
      const QPointF a = milieuLigne(r0, 0), b = milieuLigne(c.refs.size() > 1 ? c.refs[1] : r0, 0);
      return (a + b) / 2 + QPointF(16, -16);
    }
    if (m_e.element(r0)) return milieuLigne(r0, 22);
    if (c.refs.size() >= 2 && m_e.point(c.refs[1])) {
      const QPointF A = ecran(pt(r0)), B = ecran(pt(c.refs[1]));
      return (A + B) / 2 + QPointF(0, -22);
    }
    if (c.refs.size() >= 2) return (ecran(pt(r0)) + milieuLigne(c.refs[1], 0)) / 2 + QPointF(14, -14);
    return ecran(pt(r0)) + (c.type == TypeContrainte::DistanceX ? QPointF(-30, 22) : QPointF(22, 30));
  }
  if (m_e.element(r0) || r0 == AXE_H || r0 == AXE_V) {
    if (const ElementEsquisse* el = m_e.element(r0); el && el->type != TypeElement::Ligne) {
      const double r = m_e.rayonDe(*el) * m_s;
      return ecran(pt(el->centre)) + QPointF(-r * 0.75 - 12, -r * 0.75 - 12);
    }
    return milieuLigne(r0, -15);
  }
  return ecran(pt(r0)) + QPointF(12, -12);
}

void Esquisseur::dessinerCote(QPainter& g, const ContrainteEsquisse& c, const QColor& couleur) const {
  const QPointF L = positionEtiquette(c);
  const QPointF Lw = monde(L);
  g.setPen(QPen(couleur, 1.2));
  g.setBrush(couleur);
  auto fleche = [&](QPointF pointe, QPointF depuis) {
    const QPointF u = unitaire(pointe - depuis), n(-u.y(), u.x());
    QPolygonF tri;
    tri << pointe << pointe - u * 9 + n * 3.2 << pointe - u * 9 - n * 3.2;
    g.drawPolygon(tri);
  };
  auto ligneRef = [&](int id, Point& a, Point& b) {
    if (id == AXE_H) { a = {0, 0}; b = {1, 0}; return true; }
    if (id == AXE_V) { a = {0, 0}; b = {0, 1}; return true; }
    const ElementEsquisse* l = m_e.element(id);
    if (!l || l->type != TypeElement::Ligne) return false;
    a = pt(l->p1); b = pt(l->p2);
    return true;
  };
  auto projection = [](Point p, Point a, Point b) {
    const double vx = b.x - a.x, vy = b.y - a.y, l2 = std::max(1e-12, vx * vx + vy * vy);
    const double t = ((p.x - a.x) * vx + (p.y - a.y) * vy) / l2;
    return Point{a.x + t * vx, a.y + t * vy};
  };
  if (c.type == TypeContrainte::Rayon || c.type == TypeContrainte::Diametre) {
    const ElementEsquisse* el = m_e.element(c.refs.empty() ? 0 : c.refs[0]);
    if (el) {
      const Point C = pt(el->centre);
      const double r = m_e.rayonDe(*el);
      QPointF v = unitaire(QPointF(Lw.x() - C.x, Lw.y() - C.y));
      const QPointF A = ecran(C.x + v.x() * r, C.y + v.y() * r);
      if (c.type == TypeContrainte::Diametre) {
        const QPointF B = ecran(C.x - v.x() * r, C.y - v.y() * r);
        g.drawLine(L, B);
        fleche(A, B);
        fleche(B, A);
      } else {
        g.drawLine(L, ecran(C));
        fleche(A, ecran(C));
      }
    }
  } else if (c.type == TypeContrainte::Angle && c.refs.size() >= 2) {
    Point a1, b1, a2, b2;
    if (ligneRef(c.refs[0], a1, b1) && ligneRef(c.refs[1], a2, b2)) {
      const int secteur = static_cast<int>(std::lround(c.valeur2)) & 3;
      const double s1 = (secteur & 1) ? -1 : 1, s2 = (secteur & 2) ? -1 : 1;
      const double d1x = s1 * (b1.x - a1.x), d1y = s1 * (b1.y - a1.y), d2x = s2 * (b2.x - a2.x), d2y = s2 * (b2.y - a2.y);
      const double det = d1x * d2y - d1y * d2x;
      if (std::abs(det) > 1e-12) {
        const double t = ((a2.x - a1.x) * d2y - (a2.y - a1.y) * d2x) / det;
        const Point I{a1.x + t * d1x, a1.y + t * d1y};
        const double R = std::max(8.0 / m_s, std::hypot(Lw.x() - I.x, Lw.y() - I.y));
        const double ang1 = std::atan2(d1y, d1x), balayage = mesurer(m_e, c) * PI / 180;
        QPainterPath arc;
        const int n = 32;
        for (int i = 0; i <= n; ++i) {
          const double t2 = ang1 + balayage * i / n;
          const QPointF q = ecran(I.x + R * std::cos(t2), I.y + R * std::sin(t2));
          if (i == 0) arc.moveTo(q); else arc.lineTo(q);
        }
        g.setBrush(Qt::NoBrush);
        g.drawPath(arc);
        g.setBrush(couleur);
        const QPointF debut = ecran(I.x + R * std::cos(ang1), I.y + R * std::sin(ang1));
        const QPointF fin = ecran(I.x + R * std::cos(ang1 + balayage), I.y + R * std::sin(ang1 + balayage));
        fleche(debut, ecran(I.x + R * std::cos(ang1 + balayage * 0.1), I.y + R * std::sin(ang1 + balayage * 0.1)));
        fleche(fin, ecran(I.x + R * std::cos(ang1 + balayage * 0.9), I.y + R * std::sin(ang1 + balayage * 0.9)));
      }
    }
  } else {
    // Cotes linéaires : alignée, horizontale ou verticale, avec lignes d'attache et flèches.
    Point P1{0, 0}, P2{0, 0};
    bool ok = true;
    const ElementEsquisse* l0 = c.refs.empty() ? nullptr : m_e.element(c.refs[0]);
    if (l0 && l0->type == TypeElement::Ligne && c.refs.size() == 1) { P1 = pt(l0->p1); P2 = pt(l0->p2); }
    else if (c.refs.size() == 1) { P1 = {0, 0}; P2 = pt(c.refs[0]); }
    else if (c.refs.size() >= 2) {
      Point a, b;
      if (m_e.point(c.refs[0]) || c.refs[0] == 0) {
        P1 = pt(c.refs[0]);
        if (m_e.point(c.refs[1]) || c.refs[1] == 0) P2 = pt(c.refs[1]);
        else if (ligneRef(c.refs[1], a, b)) P2 = projection(P1, a, b);
        else ok = false;
      } else if (ligneRef(c.refs[0], a, b) && ligneRef(c.refs[1], P1, P2)) {
        const Point m{(P1.x + P2.x) / 2, (P1.y + P2.y) / 2};
        P2 = m;
        P1 = projection(m, a, b);
      } else ok = false;
    }
    if (ok) {
      QPointF u = c.type == TypeContrainte::DistanceX ? QPointF(1, 0) : c.type == TypeContrainte::DistanceY ? QPointF(0, 1) : unitaire(QPointF(P2.x - P1.x, P2.y - P1.y));
      const QPointF n(-u.y(), u.x());
      auto dot = [](QPointF a, QPointF b) { return a.x() * b.x() + a.y() * b.y(); };
      const QPointF p1(P1.x, P1.y), p2(P2.x, P2.y);
      const QPointF D1 = p1 + n * dot(Lw - p1, n), D2 = p2 + n * dot(Lw - p2, n);
      const QPointF e1 = ecran(P1.x, P1.y), e2 = ecran(P2.x, P2.y), d1 = ecran(D1.x(), D1.y()), d2 = ecran(D2.x(), D2.y());
      g.setPen(QPen(couleur, 1));
      for (const auto& [e, d] : {std::pair{e1, d1}, std::pair{e2, d2}})
        if (std::hypot(d.x() - e.x(), d.y() - e.y()) > 3) g.drawLine(e + unitaire(d - e) * 3, d + unitaire(d - e) * 6);
      g.setPen(QPen(couleur, 1.2));
      // La ligne de cote va jusqu'à l'étiquette quand celle-ci est placée en dehors des lignes d'attache.
      const QPointF axe = unitaire(d2 - d1);
      const double tL = dot(L - d1, axe), longueur = std::hypot(d2.x() - d1.x(), d2.y() - d1.y());
      const QPointF debut = d1 + axe * std::min(0.0, tL), fin = d1 + axe * std::max(longueur, tL);
      g.drawLine(debut, fin);
      if (longueur > 2) { fleche(d1, d2); fleche(d2, d1); }
    }
  }
  // Étiquette
  QFont f = font();
  f.setPixelSize(13);
  f.setBold(true);
  g.setFont(f);
  const QString t = texteContrainte(c);
  const QFontMetricsF fm(f);
  const QRectF zone(L.x() - fm.horizontalAdvance(t) / 2 - 5, L.y() - 10, fm.horizontalAdvance(t) + 10, 20);
  g.setPen(Qt::NoPen);
  g.setBrush(BULLE);
  g.drawRoundedRect(zone, 4, 4);
  g.setPen(lisible(couleur));
  g.drawText(zone, Qt::AlignCenter, t);
  m_zones.push_back({zone, c.id});
}

void Esquisseur::paintEvent(QPaintEvent*) {
  prendreEncres(m_proj.has_value());
  majEchelle();
  if (!m_cadre) recadrer();
  QPainter g(this);
  g.setRenderHint(QPainter::Antialiasing);
  if (!m_proj) g.fillRect(rect(), FOND);
  // Étendue visible dans le plan de l'esquisse (coins de l'écran ramenés dans le plan), bornée : vu par la
  // tranche, le plan s'étend à l'infini et ses coordonnées deviennent démesurées ou indéfinies.
  const double pas = m_s >= 8 ? 1 : m_s >= 2 ? 5 : m_s >= 0.6 ? 10 : 50;
  double u0 = 1e18, u1 = -1e18, v0 = 1e18, v1 = -1e18;
  for (const QPointF& coin : {QPointF(0, 0), QPointF(width(), 0), QPointF(0, height()), QPointF(width(), height())}) {
    const QPointF w = monde(coin);
    if (!std::isfinite(w.x()) || !std::isfinite(w.y())) continue;
    u0 = std::min(u0, w.x()); u1 = std::max(u1, w.x()); v0 = std::min(v0, w.y()); v1 = std::max(v1, w.y());
  }
  const double borne = 1e5;
  if (!(u1 > u0) || !(v1 > v0)) { u0 = v0 = -borne; u1 = v1 = borne; }
  u0 = std::max(u0, -borne); u1 = std::min(u1, borne); v0 = std::max(v0, -borne); v1 = std::min(v1, borne);
  // Grille seulement pour l'esquisse hors contexte : dans la vue 3D, l'esquisse se pose sur la pièce, sans grille.
  if (!m_proj && (u1 - u0) / pas < 2000 && (v1 - v0) / pas < 2000) {
    for (int passe = 0; passe < 2; ++passe) {
      g.setPen(QPen(passe ? GRILLE_FORTE : GRILLE, 1));
      for (double x = std::floor(u0 / pas) * pas; x <= u1; x += pas) {
        const bool fort = std::abs(std::fmod(std::abs(x) + 1e-9, pas * 10)) < 1e-6;
        if (fort == (passe == 1)) g.drawLine(ecran(x, v0), ecran(x, v1));
      }
      for (double y = std::floor(v0 / pas) * pas; y <= v1; y += pas) {
        const bool fort = std::abs(std::fmod(std::abs(y) + 1e-9, pas * 10)) < 1e-6;
        if (fort == (passe == 1)) g.drawLine(ecran(u0, y), ecran(u1, y));
      }
    }
  }
  // Axes, sélectionnables comme références
  const Cible axeH{Cible::Axe, AXE_H}, axeV{Cible::Axe, AXE_V};
  g.setPen(QPen(selectionne(axeH) ? CHOISI : m_survol == axeH ? SURVOL : AXE_X, selectionne(axeH) || m_survol == axeH ? 2.5 : 1.3, Qt::DashDotLine));
  g.drawLine(ecran(u0, 0), ecran(u1, 0));
  g.setPen(QPen(selectionne(axeV) ? CHOISI : m_survol == axeV ? SURVOL : AXE_Y, selectionne(axeV) || m_survol == axeV ? 2.5 : 1.3, Qt::DashDotLine));
  g.drawLine(ecran(0, v0), ecran(0, v1));

  auto couleurElement = [&](const ElementEsquisse& el) {
    if (selectionne({Cible::Element, el.id})) return CHOISI;
    if (m_survol == Cible{Cible::Element, el.id}) return SURVOL;
    for (int c : m_etat.enConflit)
      for (const auto& k : m_e.contraintes)
        if (k.id == c && std::find(k.refs.begin(), k.refs.end(), el.id) != k.refs.end()) return CONFLIT;
    if (el.construction) return CONSTRUCTION;
    // Un calque coloré donne sa couleur à ses éléments (le calque par défaut garde la couleur d'état).
    if (!el.calque.empty()) return couleurCalque(el.calque);
    return std::find(m_etat.elementsLibres.begin(), m_etat.elementsLibres.end(), el.id) != m_etat.elementsLibres.end() ? LIBRE : CONTRAINT;
  };
  {
    // Arêtes de la pièce en référence : gris pointillé fin, surlignées au survol
    QPen pr(REFERENCE, 1.2, Qt::DotLine);
    g.setPen(pr);
    g.setBrush(Qt::NoBrush);
    for (size_t i = 0; i < m_ext.elements.size(); ++i) {
      const auto& el = m_ext.elements[i];
      const bool survol = m_survol.type == Cible::Externe && m_survol.id == static_cast<int>(i);
      const bool choisie = m_externesChoisies.count(static_cast<int>(i)) > 0;
      if (survol) g.setPen(QPen(SURVOL, 2.4));
      else if (choisie) g.setPen(QPen(CHOISI, 2.2, Qt::DashLine));
      auto P = [&](int id) { const PointEsquisse* p = m_ext.point(id); return p ? Point{p->x, p->y} : Point{0, 0}; };
      if (el.type == TypeElement::Ligne) g.drawLine(ecran(P(el.p1)), ecran(P(el.p2)));
      else if (el.type == TypeElement::Cercle) cercle(g, P(el.centre), el.rayon);
      else {
        const Point c = P(el.centre), a = P(el.p1), b = P(el.p2);
        const double r = std::hypot(a.x - c.x, a.y - c.y), t0 = std::atan2(a.y - c.y, a.x - c.x);
        double t1 = std::atan2(b.y - c.y, b.x - c.x);
        while (t1 <= t0) t1 += 2 * PI;
        QPainterPath ch(ecran(c.x + r * std::cos(t0), c.y + r * std::sin(t0)));
        for (int k = 1; k <= 48; ++k) ch.lineTo(ecran(c.x + r * std::cos(t0 + (t1 - t0) * k / 48), c.y + r * std::sin(t0 + (t1 - t0) * k / 48)));
        g.drawPath(ch);
      }
      if (survol || choisie) g.setPen(pr);
    }
  }
  for (const auto& el : m_e.elements) {
    if (!calqueVisible(el.calque)) continue;
    const bool fort = selectionne({Cible::Element, el.id}) || m_survol == Cible{Cible::Element, el.id};
    QPen pen(couleurElement(el), fort ? 3.2 : 2.0);
    if (el.construction) { pen.setStyle(Qt::DashLine); pen.setWidthF(fort ? 2.6 : 1.6); }
    g.setPen(pen);
    g.setBrush(Qt::NoBrush);
    if (el.type == TypeElement::Ligne) {
      g.drawLine(ecran(pt(el.p1)), ecran(pt(el.p2)));
    } else if (el.type == TypeElement::Cercle) {
      cercle(g, pt(el.centre), el.rayon);
    } else {
      const Point c = pt(el.centre), a = pt(el.p1), b = pt(el.p2);
      const double r = std::hypot(a.x - c.x, a.y - c.y);
      double a0 = std::atan2(a.y - c.y, a.x - c.x), a1 = std::atan2(b.y - c.y, b.x - c.x);
      while (a1 <= a0) a1 += 2 * PI;
      QPainterPath chemin(ecran(c.x + r * std::cos(a0), c.y + r * std::sin(a0)));
      const int n = std::max(8, static_cast<int>((a1 - a0) * 24));
      for (int i = 1; i <= n; ++i) chemin.lineTo(ecran(c.x + r * std::cos(a0 + (a1 - a0) * i / n), c.y + r * std::sin(a0 + (a1 - a0) * i / n)));
      g.drawPath(chemin);
    }
  }
  // Contraintes géométriques et cotes
  m_zones.clear();
  for (const auto& c : m_e.contraintes) {
    const bool conflit = std::find(m_etat.enConflit.begin(), m_etat.enConflit.end(), c.id) != m_etat.enConflit.end();
    const bool choisi = selectionne({Cible::Contrainte, c.id});
    const bool survol = m_survol == Cible{Cible::Contrainte, c.id};
    if (estCote(c.type)) {
      if (m_coteEditee == c.id && m_editeur->isVisible()) continue;
      dessinerCote(g, c, choisi ? CHOISI : survol ? SURVOL : conflit ? CONFLIT : c.reference ? REFERENCE : COTE);
      continue;
    }
    if (m_affichageRelations == 2 && !conflit) continue;
    if (m_affichageRelations == 1 && !conflit && !choisi) {
      bool lieeSelection = false;
      for (int r : c.refs)
        for (const auto& s : m_selection) {
          if (s.id == r) lieeSelection = true;
          if (s.type == Cible::Element)
            if (const ElementEsquisse* el = m_e.element(s.id); el && (el->p1 == r || el->p2 == r || el->centre == r)) lieeSelection = true;
        }
      if (!lieeSelection) continue;
    }
    const QString t = texteContrainte(c);
    if (t.isEmpty()) continue;
    const QPointF a = positionEtiquette(c);
    QFont f = font();
    f.setPixelSize(11);
    f.setBold(true);
    g.setFont(f);
    const QFontMetricsF fm(f);
    const QRectF zone(a.x() - fm.horizontalAdvance(t) / 2 - 4, a.y() - 8, fm.horizontalAdvance(t) + 8, 16);
    g.setPen(Qt::NoPen);
    const QColor fondZone = choisi ? CHOISI : survol ? SURVOL : conflit ? CONFLIT : g_encresSombres ? BULLE_BORD : QColor(0xE7, 0xEA, 0xF3);
    g.setBrush(fondZone);
    g.drawRoundedRect(zone, 4, 4);
    // Lettre en encre, sauf si l'encre se confond avec le fond (contraste élevé : bulle blanche, encre blanche)
    const bool encreLisible = std::abs(ENCRE.lightness() - fondZone.lightness()) >= 96;
    g.setPen(choisi || conflit || !encreLisible ? surFond(fondZone) : ENCRE);
    g.drawText(zone, Qt::AlignCenter, t);
    m_zones.push_back({zone, c.id});
  }
  // Points (l'origine comprise), dessinés par-dessus
  for (const auto& p : m_e.points) {
    const QPointF q = ecran(p.x, p.y);
    const Cible cp{Cible::Point, p.id};
    const bool choisi = selectionne(cp), survol = m_survol == cp;
    if (p.id == 0) {
      g.setPen(QPen(choisi ? CHOISI : survol ? SURVOL : ENCRE, choisi || survol ? 2.5 : 1.6));
      g.setBrush(Qt::NoBrush);
      g.drawEllipse(q, 5, 5);
      g.drawLine(q + QPointF(-9, 0), q + QPointF(9, 0));
      g.drawLine(q + QPointF(0, -9), q + QPointF(0, 9));
      continue;
    }
    const bool libre = std::find(m_etat.pointsLibres.begin(), m_etat.pointsLibres.end(), p.id) != m_etat.pointsLibres.end();
    const double t = choisi || survol ? 5.5 : 3.5;
    g.setPen(Qt::NoPen);
    g.setBrush(choisi ? CHOISI : survol ? SURVOL : libre ? LIBRE : CONTRAINT);
    g.drawRect(QRectF(q - QPointF(t, t), QSizeF(2 * t, 2 * t)));
  }
  // Aperçus des outils (la saisie dynamique remplace la souris par le point qui respecte les valeurs tapées)
  g.setPen(QPen(APERCU, 1.6, Qt::DashLine));
  g.setBrush(Qt::NoBrush);
  const QPointF sourisReelle = m_souris;
  if (m_dyn.active && saisieDynamiquePossible()) m_souris = ecran(finDynamique().x(), finDynamique().y());
  if (!m_clics.empty()) {
    const QPointF A = ecran(pt(m_clics.back())), C = ecran(pt(m_clics.front()));
    switch (m_outil) {
      case Outil::Ligne: case Outil::LigneConstruction: {
        // Inférence (S1) : arrivée accrochée, lignes d'aide en pointillés fins et symbole de la relation devinée
        const Inference inf = m_dyn.active ? Inference{} : inferer(m_clics.back(), sourisReelle, QGuiApplication::keyboardModifiers() & Qt::AltModifier);
        if (inf.active) {
          const QPen trace = g.pen();
          g.setPen(QPen(APERCU.lighter(130), 1.0, Qt::DotLine));
          for (const auto& [u, v] : inf.guides) g.drawLine(ecran(u.x(), u.y()), ecran(v.x(), v.y()));
          g.setPen(trace);
          const QPointF F = ecran(inf.fin.x(), inf.fin.y());
          g.drawLine(A, F);
          g.save();
          g.setPen(APERCU);
          QFont police = g.font();
          police.setPointSizeF(police.pointSizeF() * 1.05);
          police.setBold(true);
          g.setFont(police);
          g.drawText(F + QPointF(12, -10), inf.symbole);
          g.restore();
        } else {
          g.drawLine(A, m_souris);
        }
        break;
      }
      case Outil::Rectangle: {
        // Coins pris dans le plan de l'esquisse, puis projetés : juste aussi quand la vue est tournée (EI-15)
        const Point a = pt(m_clics.front());
        const QPointF b = monde(m_souris);
        g.drawPolygon(QPolygonF({ecran(a.x, a.y), ecran(b.x(), a.y), ecran(b.x(), b.y()), ecran(a.x, b.y())}));
        (void)C;
        break;
      }
      case Outil::Cercle: case Outil::Polygone: {
        const Point c = pt(m_clics.front());
        const QPointF w = monde(m_souris);
        const double r = std::hypot(w.x() - c.x, w.y() - c.y);
        cercle(g, c, r);
        if (m_outil == Outil::Polygone) {
          QPolygonF poly;
          const int n = std::max(3, m_cotesPolygone);
          // Inscrit : la souris est un sommet ; circonscrit : la souris est le milieu d'un côté
          const double rs = m_polygoneCirconscrit ? r / std::cos(PI / n) : r;
          const double t0 = std::atan2(w.y() - c.y, w.x() - c.x) + (m_polygoneCirconscrit ? PI / n : 0);
          for (int i = 0; i < n; ++i) poly << ecran(c.x + rs * std::cos(t0 + 2 * PI * i / n), c.y + rs * std::sin(t0 + 2 * PI * i / n));
          g.drawPolygon(poly);
        }
        break;
      }
      case Outil::Arc: {
        const Point c = pt(m_clics.front());
        const QPointF w = monde(m_souris);
        if (m_clics.size() == 1) {
          QPen p = g.pen(); p.setStyle(Qt::DotLine); g.setPen(p);
          cercle(g, c, std::hypot(w.x() - c.x, w.y() - c.y));
          g.drawLine(ecran(c), m_souris);
        } else {
          const Point a = pt(m_clics.back());
          const double r = std::hypot(a.x - c.x, a.y - c.y);
          const double a0 = std::atan2(a.y - c.y, a.x - c.x);
          double a1 = std::atan2(w.y() - c.y, w.x() - c.x);
          while (a1 <= a0) a1 += 2 * PI;
          QPainterPath ch(ecran(c.x + r * std::cos(a0), c.y + r * std::sin(a0)));
          for (int i = 1; i <= 64; ++i) ch.lineTo(ecran(c.x + r * std::cos(a0 + (a1 - a0) * i / 64), c.y + r * std::sin(a0 + (a1 - a0) * i / 64)));
          g.setPen(QPen(APERCU, 2.2));
          g.drawPath(ch);
        }
        break;
      }
      case Outil::Arc3Points: {
        const QPointF w = monde(m_souris);
        if (m_clics.size() == 1) { g.drawLine(C, m_souris); break; }
        const Point a = pt(m_clics[0]), b = pt(m_clics[1]);
        const double d = 2 * (a.x * (b.y - w.y()) + b.x * (w.y() - a.y) + w.x() * (a.y - b.y));
        if (std::abs(d) < 1e-12) { g.drawLine(ecran(a), ecran(b)); break; }
        const double a2 = a.x * a.x + a.y * a.y, b2 = b.x * b.x + b.y * b.y, p2 = w.x() * w.x() + w.y() * w.y();
        const double cx = (a2 * (b.y - w.y()) + b2 * (w.y() - a.y) + p2 * (a.y - b.y)) / d, cy = (a2 * (w.x() - b.x) + b2 * (a.x - w.x()) + p2 * (b.x - a.x)) / d;
        const double r = std::hypot(a.x - cx, a.y - cy);
        double t0 = std::atan2(a.y - cy, a.x - cx), t1 = std::atan2(b.y - cy, b.x - cx), tp = std::atan2(w.y() - cy, w.x() - cx);
        while (t1 <= t0) t1 += 2 * PI;
        while (tp <= t0) tp += 2 * PI;
        if (tp > t1) { std::swap(t0, t1); while (t1 <= t0) t1 += 2 * PI; }
        QPainterPath ch(ecran(cx + r * std::cos(t0), cy + r * std::sin(t0)));
        for (int i = 1; i <= 64; ++i) ch.lineTo(ecran(cx + r * std::cos(t0 + (t1 - t0) * i / 64), cy + r * std::sin(t0 + (t1 - t0) * i / 64)));
        g.setPen(QPen(APERCU, 2.2));
        g.drawPath(ch);
        break;
      }
      case Outil::Rainure: {
        if (m_clics.size() == 1) g.drawLine(C, m_souris);
        else {
          const Point c1 = pt(m_clics.front()), c2 = pt(m_clics.back());
          const QPointF w = monde(m_souris);
          const double L = std::max(1e-9, std::hypot(c2.x - c1.x, c2.y - c1.y)), nx = -(c2.y - c1.y) / L, ny = (c2.x - c1.x) / L;
          const double r = std::abs((w.x() - c1.x) * nx + (w.y() - c1.y) * ny);
          g.drawLine(ecran(c1.x + nx * r, c1.y + ny * r), ecran(c2.x + nx * r, c2.y + ny * r));
          g.drawLine(ecran(c1.x - nx * r, c1.y - ny * r), ecran(c2.x - nx * r, c2.y - ny * r));
          cercle(g, c1, r);
          cercle(g, c2, r);
        }
        break;
      }
      default: break;
    }
  }
  m_souris = sourisReelle;
  // Saisie dynamique : champs près du curseur (valeur tapée en gras, valeur mesurée en gris)
  if (saisieDynamiquePossible()) {
    const int n = champsDynamiques();
    const QPointF fin = finDynamique();
    QString libelles[2];
    double mesures[2] = {0, 0};
    const Point a = pt(m_outil == Outil::Ligne || m_outil == Outil::LigneConstruction ? m_clics.back() : m_clics.front());
    if (m_outil == Outil::Ligne || m_outil == Outil::LigneConstruction) {
      libelles[0] = QStringLiteral("L");
      libelles[1] = QStringLiteral("∠");
      mesures[0] = std::hypot(fin.x() - a.x, fin.y() - a.y);
      mesures[1] = std::atan2(fin.y() - a.y, fin.x() - a.x) * 180 / PI;
    } else if (m_outil == Outil::Rectangle) {
      libelles[0] = QStringLiteral("↔");
      libelles[1] = QStringLiteral("↕");
      mesures[0] = std::abs(fin.x() - a.x);
      mesures[1] = std::abs(fin.y() - a.y);
    } else {
      libelles[0] = QStringLiteral("Ø");
      mesures[0] = 2 * std::hypot(fin.x() - a.x, fin.y() - a.y);
    }
    QFont f = font();
    f.setPointSizeF(9);
    g.setFont(f);
    QPointF ou = ecran(fin.x(), fin.y()) + QPointF(16, 14);
    for (int k2 = 0; k2 < n; ++k2) {
      const bool actif = m_dyn.active && m_dyn.champ == k2;
      QString t;
      if (actif && !m_dyn.texte[k2].isEmpty()) t = m_dyn.texte[k2] + QStringLiteral("▏");
      else if (m_dyn.fige[k2]) t = m_dyn.texte[k2];
      else t = QString::fromStdString(ecrireValeur(std::round(mesures[k2] * 100) / 100, k2 == 1 && libelles[1] == QStringLiteral("∠"), m_unites));
      const QString texte = libelles[k2] + QStringLiteral(" ") + t;
      const QRectF r(ou, QSizeF(QFontMetricsF(f).horizontalAdvance(texte) + 12, QFontMetricsF(f).height() + 6));
      g.setPen(QPen(actif ? CHOISI : QColor(0xB6, 0xBE, 0xD6), actif ? 1.6 : 1));
      g.setBrush(QColor(255, 255, 255, 235));
      g.drawRoundedRect(r, 4, 4);
      f.setBold(m_dyn.fige[k2] || actif);
      g.setFont(f);
      g.setPen(m_dyn.fige[k2] || actif ? QColor(0x10, 0x14, 0x2A) : QColor(0x6B, 0x73, 0x93));
      g.drawText(r, Qt::AlignCenter, texte);
      ou += QPointF(r.width() + 6, 0);
    }
  }

  // Aperçus des outils à points libres
  if (!m_libres.empty()) {
    g.setPen(QPen(APERCU, 1.6, Qt::DashLine));
    g.setBrush(Qt::NoBrush);
    const QPointF w = monde(m_souris);
    std::vector<QPointF> pts = m_libres;
    pts.push_back(w);
    QPolygonF poly;
    if ((m_outil == Outil::Rectangle3Points || m_outil == Outil::Parallelogramme) && pts.size() == 3) {
      QPointF d = pts[2];
      if (m_outil == Outil::Rectangle3Points) {
        const QPointF ab = pts[1] - pts[0];
        const double L = std::max(1e-9, std::hypot(ab.x(), ab.y()));
        const QPointF n(-ab.y() / L, ab.x() / L);
        const double lw = (w.x() - pts[1].x()) * n.x() + (w.y() - pts[1].y()) * n.y();
        pts[2] = pts[1] + n * lw;
        d = pts[0] + n * lw;
      } else {
        d = pts[0] + pts[2] - pts[1];
      }
      pts.push_back(d);
      for (const auto& q : pts) poly << ecran(q.x(), q.y());
      g.drawPolygon(poly);
    } else if (m_outil == Outil::Cercle3Points && pts.size() == 3) {
      const QPointF a = pts[0], b = pts[1], c = pts[2];
      const double d = 2 * (a.x() * (b.y() - c.y()) + b.x() * (c.y() - a.y()) + c.x() * (a.y() - b.y()));
      if (std::abs(d) > 1e-12) {
        const double a2 = a.x() * a.x() + a.y() * a.y(), b2 = b.x() * b.x() + b.y() * b.y(), c2 = c.x() * c.x() + c.y() * c.y();
        const Point centre{(a2 * (b.y() - c.y()) + b2 * (c.y() - a.y()) + c2 * (a.y() - b.y())) / d, (a2 * (c.x() - b.x()) + b2 * (a.x() - c.x()) + c2 * (b.x() - a.x())) / d};
        g.setPen(QPen(APERCU, 2.2));
        cercle(g, centre, std::hypot(a.x() - centre.x, a.y() - centre.y));
      }
    } else if (m_outil == Outil::Cercle3Points && pts.size() == 2) {
      const Point centre{(pts[0].x() + pts[1].x()) / 2, (pts[0].y() + pts[1].y()) / 2};
      QPen p = g.pen(); p.setStyle(Qt::DotLine); g.setPen(p);
      cercle(g, centre, std::hypot(pts[0].x() - centre.x, pts[0].y() - centre.y));
    } else {
      for (const auto& q : pts) poly << ecran(q.x(), q.y());
      g.drawPolyline(poly);
    }
    if (m_outil == Outil::RainureArc) {
      const QPointF w = monde(m_souris);
      const QPointF c = m_libres[0];
      g.setPen(QPen(APERCU, 1.6, Qt::DashLine));
      g.setBrush(Qt::NoBrush);
      if (m_libres.size() == 1) {
        // Premier clic : le centre ; on montre le rayon et le cercle sur lequel la rainure va s'enrouler
        g.drawLine(ecran(c.x(), c.y()), m_souris);
        cercle(g, {c.x(), c.y()}, std::hypot(w.x() - c.x(), w.y() - c.y()));
      } else if (m_libres.size() == 2) {
        // Deuxième clic : le début ; la rainure s'étire jusqu'à la souris (sens direct), avec sa largeur
        const QPointF a = m_libres[1];
        const double r = std::hypot(a.x() - c.x(), a.y() - c.y()), l = m_largeurRainureArc / 2;
        const double a0 = std::atan2(a.y() - c.y(), a.x() - c.x());
        double a1 = std::atan2(w.y() - c.y(), w.x() - c.x());
        while (a1 <= a0) a1 += 2 * PI;
        auto arc = [&](double cx, double cy, double rr, double d, double f) {
          QPainterPath ch(ecran(cx + rr * std::cos(d), cy + rr * std::sin(d)));
          for (int i = 1; i <= 72; ++i) ch.lineTo(ecran(cx + rr * std::cos(d + (f - d) * i / 72), cy + rr * std::sin(d + (f - d) * i / 72)));
          g.drawPath(ch);
        };
        g.setPen(QPen(APERCU, 2.2, Qt::SolidLine));
        if (r > l) {
          arc(c.x(), c.y(), r + l, a0, a1);
          arc(c.x(), c.y(), r - l, a0, a1);
          arc(c.x() + r * std::cos(a1), c.y() + r * std::sin(a1), l, a1, a1 + PI);
          arc(c.x() + r * std::cos(a0), c.y() + r * std::sin(a0), l, a0 + PI, a0 + 2 * PI);
        }
        g.setPen(QPen(APERCU, 1.2, Qt::DashLine));
        arc(c.x(), c.y(), r, a0, a1);
      }
    }
    for (const auto& q : m_libres) { g.setPen(Qt::NoPen); g.setBrush(CHOISI); g.drawEllipse(ecran(q.x(), q.y()), 4, 4); }
  }
  if (!m_clics.empty() && (m_outil == Outil::RectangleCentre || m_outil == Outil::LigneMilieu || m_outil == Outil::ArcTangent)) {
    g.setPen(QPen(APERCU, 1.6, Qt::DashLine));
    g.setBrush(Qt::NoBrush);
    const Point c = pt(m_clics.front());
    const QPointF w = monde(m_souris);
    if (m_outil == Outil::RectangleCentre) {
      const double dx = std::abs(w.x() - c.x), dy = std::abs(w.y() - c.y);
      QPolygonF r;
      r << ecran(c.x - dx, c.y - dy) << ecran(c.x + dx, c.y - dy) << ecran(c.x + dx, c.y + dy) << ecran(c.x - dx, c.y + dy);
      g.drawPolygon(r);
    } else if (m_outil == Outil::LigneMilieu) {
      g.drawLine(ecran(2 * c.x - w.x(), 2 * c.y - w.y()), ecran(w.x(), w.y()));
    } else {
      Point centre{0, 0};
      bool direct = true;
      if (m_e.calculerArcTangent(m_clics.front(), w.x(), w.y(), centre, direct, nullptr, m_arcPrecedent)) {
        const double r = std::hypot(c.x - centre.x, c.y - centre.y);
        double t0 = std::atan2(c.y - centre.y, c.x - centre.x), t1 = std::atan2(w.y() - centre.y, w.x() - centre.x);
        if (!direct) std::swap(t0, t1);
        while (t1 <= t0) t1 += 2 * PI;
        QPainterPath ch(ecran(centre.x + r * std::cos(t0), centre.y + r * std::sin(t0)));
        for (int i = 1; i <= 64; ++i) ch.lineTo(ecran(centre.x + r * std::cos(t0 + (t1 - t0) * i / 64), centre.y + r * std::sin(t0 + (t1 - t0) * i / 64)));
        g.setPen(QPen(APERCU, 2.2));
        g.drawPath(ch);
      } else {
        g.drawLine(ecran(c), m_souris);
      }
    }
  }
  // Cote en cours de placement : aperçu
  if (m_outil == Outil::Cote && !m_coteRefs.empty()) {
    for (const auto& c : m_coteRefs) {
      if (c.type == Cible::Point) { g.setPen(Qt::NoPen); g.setBrush(CHOISI); g.drawEllipse(ecran(pt(c.id)), 6, 6); }
    }
    const Proposition pr = proposerCote(m_coteRefs, m_souris);
    if (pr.valide) {
      ContrainteEsquisse k;
      k.type = pr.type;
      k.refs = pr.refs;
      k.valeur2 = pr.valeur2;
      k.place = true;
      const QPointF w = monde(m_souris);
      k.lx = w.x();
      k.ly = w.y();
      k.reference = true;  // l'aperçu affiche la valeur mesurée
      const size_t avant = m_zones.size();
      dessinerCote(g, k, CHOISI);
      m_zones.resize(avant);
    }
  }
  // Cadre de sélection
  if (m_geste == Geste::Cadre && m_bouge) {
    const bool fenetre = m_souris.x() >= m_depart.x();
    g.setPen(QPen(LIBRE, 1, fenetre ? Qt::SolidLine : Qt::DashLine));
    g.setBrush(alpha(CHOISI, fenetre ? 30 : 15));
    g.drawRect(QRectF(m_depart, m_souris).normalized());
  }
  if (!m_surbrillanceElements.empty() || !m_surbrillancePoints.empty()) {
    const QColor halo = alpha(APERCU, 110);
    g.setPen(QPen(halo, 9, Qt::SolidLine, Qt::RoundCap));
    g.setBrush(Qt::NoBrush);
    for (int id : m_surbrillanceElements) {
      const ElementEsquisse* el = m_e.element(id);
      if (!el) continue;
      if (el->type == TypeElement::Ligne) g.drawLine(ecran(pt(el->p1)), ecran(pt(el->p2)));
      else if (el->type == TypeElement::Cercle) cercle(g, pt(el->centre), el->rayon);
      else {
        const Point c = pt(el->centre), a0 = pt(el->p1), b0 = pt(el->p2);
        const double r = std::hypot(a0.x - c.x, a0.y - c.y), t0 = std::atan2(a0.y - c.y, a0.x - c.x);
        double t1 = std::atan2(b0.y - c.y, b0.x - c.x);
        while (t1 <= t0) t1 += 2 * PI;
        QPainterPath ch(ecran(c.x + r * std::cos(t0), c.y + r * std::sin(t0)));
        for (int i = 1; i <= 48; ++i) ch.lineTo(ecran(c.x + r * std::cos(t0 + (t1 - t0) * i / 48), c.y + r * std::sin(t0 + (t1 - t0) * i / 48)));
        g.drawPath(ch);
      }
    }
    g.setPen(QPen(APERCU, 2.4));
    for (int pid : m_surbrillancePoints) {
      const QPointF q = ecran(pt(pid));
      g.setBrush(alpha(APERCU, 60));
      g.drawEllipse(q, 9, 9);
    }
  }
  if (!m_titre.isEmpty()) {
    QFont f = font();
    f.setPixelSize(12);
    f.setBold(true);
    g.setFont(f);
    const QFontMetricsF fm(f);
    const QRectF zone(14, 12, fm.horizontalAdvance(m_titre) + 22, 26);
    g.setPen(QPen(BULLE_BORD, 1));
    g.setBrush(BULLE);
    g.drawRoundedRect(zone, 8, 8);
    g.setPen(BULLE_TEXTE);
    g.drawText(zone.adjusted(11, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, m_titre);
  }
  if (m_apercu) {
    g.setPen(QPen(APERCU, 2.2, Qt::DashLine));
    g.setBrush(Qt::NoBrush);
    for (const auto& el : m_apercu->elements) {
      const ElementEsquisse* avant = m_e.element(el.id);
      bool change = !avant;
      if (avant)
        for (int pid : {el.p1, el.p2, el.centre}) {
          const PointEsquisse* a = m_apercu->point(pid);
          const PointEsquisse* b = m_e.point(pid);
          if (a && (!b || std::hypot(a->x - b->x, a->y - b->y) > 1e-9)) change = true;
        }
      if (avant && el.type == TypeElement::Cercle && std::abs(avant->rayon - el.rayon) > 1e-9) change = true;
      if (!change) continue;
      auto P = [&](int pid) { const PointEsquisse* p = m_apercu->point(pid); return p ? Point{p->x, p->y} : Point{0, 0}; };
      if (el.type == TypeElement::Ligne) g.drawLine(ecran(P(el.p1)), ecran(P(el.p2)));
      else if (el.type == TypeElement::Cercle) cercle(g, P(el.centre), el.rayon);
      else {
        const Point c = P(el.centre), a = P(el.p1), b = P(el.p2);
        const double r = std::hypot(a.x - c.x, a.y - c.y);
        const double a0 = std::atan2(a.y - c.y, a.x - c.x);
        double a1 = std::atan2(b.y - c.y, b.x - c.x);
        while (a1 <= a0) a1 += 2 * PI;
        QPainterPath ch(ecran(c.x + r * std::cos(a0), c.y + r * std::sin(a0)));
        for (int i = 1; i <= 64; ++i) ch.lineTo(ecran(c.x + r * std::cos(a0 + (a1 - a0) * i / 64), c.y + r * std::sin(a0 + (a1 - a0) * i / 64)));
        g.drawPath(ch);
      }
    }
  }
  dessinerInspecteur(g);
  if (m_radialVisible) dessinerRadial(g);
  // Accroche visible sous le curseur pendant un tracé
  if (m_outil != Outil::Selection && m_outil != Outil::Cote) {
    const int survolPoint = pointSous(m_souris);
    if (survolPoint >= 0) {
      g.setPen(QPen(CHOISI, 2));
      g.setBrush(Qt::NoBrush);
      g.drawEllipse(ecran(pt(survolPoint)), 9, 9);
    }
  }
}

int Esquisseur::pointSous(const QPointF& p, int exclu) const {
  int meilleur = -1;
  double dmin = 9;
  for (const auto& q : m_e.points) {
    if (q.id == exclu) continue;
    const QPointF s = ecran(q.x, q.y);
    const double d = std::hypot(s.x() - p.x(), s.y() - p.y());
    if (d < dmin) { dmin = d; meilleur = q.id; }
  }
  return meilleur;
}

// Point existant sous le curseur, sinon nouveau point accroché (milieu d'une ligne, sur une courbe ou un axe).
Esquisseur::Inference Esquisseur::inferer(int depart, const QPointF& souris, bool sansInference) const {
  Inference r;
  if (sansInference || pointSous(souris) >= 0) return r;
  // Une accroche sur un élément (milieu, point sur une ligne ou un cercle) l'emporte sur l'inférence
  for (const auto& el : m_e.elements) {
    if (el.type == TypeElement::Ligne) {
      if (el.p1 == depart || el.p2 == depart) continue;
      if (distSegment(souris, ecran(pt(el.p1)), ecran(pt(el.p2))) < 6) return r;
    } else {
      const Point c = pt(el.centre);
      const QPointF wc = monde(souris);
      if (std::abs(std::hypot(wc.x() - c.x, wc.y() - c.y) - m_e.rayonDe(el)) * m_s < 6) return r;
    }
  }
  const Point A = pt(depart);
  const QPointF w = monde(souris);
  const double bx = w.x() - A.x, by = w.y() - A.y, longueur = std::hypot(bx, by);
  if (longueur * m_s < 12) return r;  // trop près du départ pour deviner quoi que ce soit
  const double angle = std::atan2(by, bx);
  auto ecartAngulaire = [](double a, double b) {  // écart entre deux directions non orientées, en degrés
    double d = std::fmod(std::abs(a - b), PI);
    return std::min(d, PI - d) * 180 / PI;
  };
  // Trait précédent : la dernière ligne qui arrive au point de départ
  const ElementEsquisse* precedent = nullptr;
  const ElementEsquisse* arcDepart = nullptr;
  for (auto it = m_e.elements.rbegin(); it != m_e.elements.rend(); ++it) {
    if (it->type == TypeElement::Ligne && !precedent && (it->p1 == depart || it->p2 == depart)) precedent = &*it;
    if (it->type == TypeElement::Arc && !arcDepart && (it->p1 == depart || it->p2 == depart)) arcDepart = &*it;
  }
  // Direction : tangente à l'arc de départ, horizontale ou verticale, perpendiculaire au trait précédent,
  // parallèle ou perpendiculaire à une autre ligne ; la plus proche, dans l'ordre de priorité, sous sa tolérance
  struct Choix { double direction; double ecart; prom::TypeContrainte type; int ref; QString symbole; int priorite; };
  std::vector<Choix> choix;
  if (arcDepart) {
    const Point c = pt(arcDepart->centre);
    const double t = std::atan2(A.y - c.y, A.x - c.x) + PI / 2;
    if (const double d = ecartAngulaire(angle, t); d < 4) choix.push_back({t, d, TypeContrainte::Tangente, arcDepart->id, QStringLiteral("tangente"), 0});
  }
  for (const double t : {0.0, PI / 2})
    if (const double d = ecartAngulaire(angle, t); d < 3) choix.push_back({t, d, t == 0 ? TypeContrainte::Horizontal : TypeContrainte::Vertical, 0, t == 0 ? QStringLiteral("H") : QStringLiteral("V"), 1});
  for (const auto& el : m_e.elements) {
    if (el.type != TypeElement::Ligne) continue;
    const Point a = pt(el.p1), b = pt(el.p2);
    if (std::hypot(b.x - a.x, b.y - a.y) < 1e-9) continue;
    const double t = std::atan2(b.y - a.y, b.x - a.x);
    // Une ligne horizontale ou verticale est déjà couverte par H et V
    if (ecartAngulaire(t, 0) < 1e-6 || ecartAngulaire(t, PI / 2) < 1e-6) continue;
    const bool voisin = &el == precedent;
    if (const double d = ecartAngulaire(angle, t + PI / 2); d < 2) choix.push_back({t + PI / 2, d, TypeContrainte::Perpendiculaire, el.id, QStringLiteral("⊥"), voisin ? 2 : 3});
    if (!voisin)
      if (const double d = ecartAngulaire(angle, t); d < 2) choix.push_back({t, d, TypeContrainte::Parallele, el.id, QStringLiteral("∥"), 3});
  }
  // Perpendiculaire au trait précédent même s'il est horizontal ou vertical : c'est alors V ou H, déjà proposé
  QPointF fin = w;
  bool direction = false;
  double dirX = 0, dirY = 0;
  if (!choix.empty()) {
    const auto meilleur = std::min_element(choix.begin(), choix.end(), [](const Choix& a, const Choix& b) { return a.priorite != b.priorite ? a.priorite < b.priorite : a.ecart < b.ecart; });
    dirX = std::cos(meilleur->direction);
    dirY = std::sin(meilleur->direction);
    const double l = bx * dirX + by * dirY;
    fin = QPointF(A.x + dirX * l, A.y + dirY * l);
    direction = true;
    if (meilleur->type == TypeContrainte::Horizontal || meilleur->type == TypeContrainte::Vertical) r.relations.push_back({meilleur->type, {-1}});
    else r.relations.push_back({meilleur->type, {-1, meilleur->ref}});
    r.symbole = meilleur->symbole;
    // Ligne d'aide : la ligne de référence prolongée jusqu'au tracé
    r.guides.push_back({QPointF(A.x - dirX * longueur * 0.3, A.y - dirY * longueur * 0.3), QPointF(fin.x() + dirX * longueur * 0.3, fin.y() + dirY * longueur * 0.3)});
  }
  // Alignement sur un point existant (lignes d'alignement) : libre, ou dans l'axe libre d'un tracé horizontal ou
  // vertical
  const bool horizontal = direction && std::abs(dirY) < 1e-12, vertical = direction && std::abs(dirX) < 1e-12;
  if (!direction || horizontal || vertical) {
    int alignX = -1, alignY = -1;
    double meilleurX = 6, meilleurY = 6;
    for (const auto& q : m_e.points) {
      if (q.id == depart) continue;
      const QPointF e = ecran(q.x, q.y), f = ecran(fin.x(), fin.y());
      if (!vertical && std::abs(e.x() - f.x()) < meilleurX && std::abs(e.y() - f.y()) > 10) { meilleurX = std::abs(e.x() - f.x()); alignX = q.id; }
      if (!horizontal && std::abs(e.y() - f.y()) < meilleurY && std::abs(e.x() - f.x()) > 10) { meilleurY = std::abs(e.y() - f.y()); alignY = q.id; }
    }
    if (alignX >= 0) {
      const Point q = pt(alignX);
      fin.setX(q.x);
      r.relations.push_back({TypeContrainte::Vertical, {-2, alignX}});
      r.guides.push_back({QPointF(q.x, q.y), fin});
      r.symbole += r.symbole.isEmpty() ? QStringLiteral("⋮") : QStringLiteral(" ⋮");
    }
    if (alignY >= 0) {
      const Point q = pt(alignY);
      fin.setY(q.y);
      r.relations.push_back({TypeContrainte::Horizontal, {-2, alignY}});
      r.guides.push_back({QPointF(q.x, q.y), fin});
      r.symbole += r.symbole.isEmpty() ? QStringLiteral("⋯") : QStringLiteral(" ⋯");
    }
  }
  // Longueur égale au trait précédent, le long de la direction retenue
  if (precedent) {
    const Point a = pt(precedent->p1), b = pt(precedent->p2);
    const double l0 = std::hypot(b.x - a.x, b.y - a.y), l = std::hypot(fin.x() - A.x, fin.y() - A.y);
    const bool aligne = std::any_of(r.relations.begin(), r.relations.end(), [](const auto& x) { return x.second.size() == 2 && x.second[0] == -2; });
    // Tolérance : 2 % de la longueur, ou 4 pixels à l'écran, sans dépasser 10 %
    const double tolerance = std::min(0.1 * l0, std::max(0.02 * l0, 4 / std::max(1e-9, m_s)));
    if (l0 > 1e-9 && l > 1e-9 && std::abs(l - l0) < tolerance && !aligne) {
      fin = QPointF(A.x + (fin.x() - A.x) / l * l0, A.y + (fin.y() - A.y) / l * l0);
      r.relations.push_back({TypeContrainte::Egal, {-1, precedent->id}});
      r.symbole += r.symbole.isEmpty() ? QStringLiteral("=") : QStringLiteral(" =");
    }
  }
  r.active = !r.relations.empty();
  r.fin = fin;
  return r;
}

int Esquisseur::obtenirPoint(const QPointF& p) {
  const int existant = pointSous(p);
  if (existant >= 0) return existant;
  const std::size_t avant = m_e.points.size();
  const QPointF w = monde(p);
  const int cree = [&] {
    for (const auto& el : m_e.elements)
      if (el.type == TypeElement::Ligne) {
        const Point a = pt(el.p1), b = pt(el.p2);
        const QPointF m = ecran((a.x + b.x) / 2, (a.y + b.y) / 2);
        if (std::hypot(m.x() - p.x(), m.y() - p.y()) < 8) {
          const int n = m_e.ajouterPoint((a.x + b.x) / 2, (a.y + b.y) / 2);
          m_e.contraindre(TypeContrainte::Milieu, {n, el.id});
          return n;
        }
      }
    for (const auto& el : m_e.elements) {
      if (el.type == TypeElement::Ligne) {
        const Point a = pt(el.p1), b = pt(el.p2);
        if (distSegment(p, ecran(a), ecran(b)) < 6) {
          const double vx = b.x - a.x, vy = b.y - a.y, t = ((w.x() - a.x) * vx + (w.y() - a.y) * vy) / std::max(1e-12, vx * vx + vy * vy);
          const int n = m_e.ajouterPoint(a.x + t * vx, a.y + t * vy);
          m_e.contraindre(TypeContrainte::PointSur, {n, el.id});
          return n;
        }
      } else {
        const Point c = pt(el.centre);
        const double r = m_e.rayonDe(el), d = std::hypot(w.x() - c.x, w.y() - c.y);
        if (std::abs(d - r) * m_s < 6 && d > 1e-9) {
          const int n = m_e.ajouterPoint(c.x + (w.x() - c.x) / d * r, c.y + (w.y() - c.y) / d * r);
          m_e.contraindre(TypeContrainte::PointSur, {n, el.id});
          return n;
        }
      }
    }
    const double grille = m_s >= 8 ? 0.5 : m_s >= 2 ? 1 : 5;
    if (std::abs(w.y()) * m_s < 6) {
      const int n = m_e.ajouterPoint(std::round(w.x() / grille) * grille, 0);
      m_e.contraindre(TypeContrainte::PointSur, {n, AXE_H});
      return n;
    }
    if (std::abs(w.x()) * m_s < 6) {
      const int n = m_e.ajouterPoint(0, std::round(w.y() / grille) * grille);
      m_e.contraindre(TypeContrainte::PointSur, {n, AXE_V});
      return n;
    }
    return m_e.ajouterPoint(std::round(w.x() / grille) * grille, std::round(w.y() / grille) * grille);
  }();
  // Point créé pour un outil (pas pour l'outil Point, dont c'est le résultat) : retiré si la forme n'aboutit pas
  if (m_outil != Outil::Point && m_e.points.size() > avant) m_pointsOutil.push_back(cree);
  return cree;
}

Esquisseur::Cible Esquisseur::toucher(const QPointF& p, bool avecCotes) const {
  const Cible c = toucherEsquisse(p, avecCotes);
  if (c.type != Cible::Rien || m_ext.elements.empty() || (m_outil != Outil::Selection && m_outil != Outil::Cote)) return c;
  for (size_t i = 0; i < m_ext.elements.size(); ++i) {
    const auto& el = m_ext.elements[i];
    auto P = [&](int id) { const PointEsquisse* q = m_ext.point(id); return q ? Point{q->x, q->y} : Point{0, 0}; };
    double d = 1e9;
    if (el.type == TypeElement::Ligne) {
      const QPointF a = ecran(P(el.p1)), b = ecran(P(el.p2));
      const QPointF ab = b - a;
      const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
      const double t = l2 > 1e-12 ? std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / l2, 0.0, 1.0) : 0;
      d = std::hypot(p.x() - a.x() - t * ab.x(), p.y() - a.y() - t * ab.y());
    } else {
      const Point cc = P(el.centre);
      const QPointF wp = monde(p);
      const double r = el.type == TypeElement::Cercle ? el.rayon : std::hypot(P(el.p1).x - cc.x, P(el.p1).y - cc.y);
      const double dc = std::hypot(wp.x() - cc.x, wp.y() - cc.y);
      if (dc > 1e-12) { const QPointF q = ecran(cc.x + r * (wp.x() - cc.x) / dc, cc.y + r * (wp.y() - cc.y) / dc); d = std::hypot(p.x() - q.x(), p.y() - q.y()); }
    }
    if (d < 7) return {Cible::Externe, static_cast<int>(i)};
  }
  return c;
}

Esquisseur::Cible Esquisseur::toucherEsquisse(const QPointF& p, bool avecCotes) const {
  const int pt0 = pointSous(p);
  if (pt0 >= 0) return {Cible::Point, pt0};
  if (avecCotes)
    for (const auto& [zone, id] : m_zones)
      if (zone.adjusted(-2, -2, 2, 2).contains(p)) return {Cible::Contrainte, id};
  Cible meilleur;
  double dmin = 7;
  for (const auto& el : m_e.elements) {
    if (!calqueVisible(el.calque)) continue;
    double d = 1e9;
    if (el.type == TypeElement::Ligne) d = distSegment(p, ecran(pt(el.p1)), ecran(pt(el.p2)));
    else {
      // Distance mesurée à l'écran jusqu'au point le plus proche du cercle, calculé dans le plan de l'esquisse :
      // juste aussi quand la vue est inclinée (le cercle apparaît alors en ellipse).
      const Point c = pt(el.centre);
      const QPointF wp = monde(p);
      const double r = m_e.rayonDe(el), dc = std::hypot(wp.x() - c.x, wp.y() - c.y);
      if (std::isfinite(dc) && dc > 1e-12) {
        const QPointF q = ecran(c.x + r * (wp.x() - c.x) / dc, c.y + r * (wp.y() - c.y) / dc);
        d = std::hypot(p.x() - q.x(), p.y() - q.y());
      }
      if (el.type == TypeElement::Arc) {
        // Hors de l'arc : on mesure la distance aux extrémités.
        const Point c = pt(el.centre), a = pt(el.p1), b = pt(el.p2);
        const QPointF w = monde(p);
        double a0 = std::atan2(a.y - c.y, a.x - c.x), a1 = std::atan2(b.y - c.y, b.x - c.x), t = std::atan2(w.y() - c.y, w.x() - c.x);
        while (a1 <= a0) a1 += 2 * PI;
        while (t < a0) t += 2 * PI;
        if (t > a1) d = std::min(std::hypot(p.x() - ecran(a).x(), p.y() - ecran(a).y()), std::hypot(p.x() - ecran(b).x(), p.y() - ecran(b).y()));
      }
    }
    if (d < dmin) { dmin = d; meilleur = {Cible::Element, el.id}; }
  }
  if (meilleur.type != Cible::Rien) return meilleur;
  const QPointF o = ecran(0, 0), ox = ecran(1000, 0), oy = ecran(0, 1000);
  auto distDroite = [&](QPointF a, QPointF b) {
    const double l = std::max(1e-9, std::hypot(b.x() - a.x(), b.y() - a.y()));
    return std::abs((b.x() - a.x()) * (p.y() - a.y()) - (b.y() - a.y()) * (p.x() - a.x())) / l;
  };
  if (distDroite(o, ox) < 6) return {Cible::Axe, AXE_H};
  if (distDroite(o, oy) < 6) return {Cible::Axe, AXE_V};
  return {};
}

// Cote proposée pour les éléments cliqués ; pour deux points ou une ligne, la position de la souris choisit
// entre cote alignée, horizontale et verticale, comme dans SolidWorks.
int Esquisseur::secteurAngle(int ligne1, int ligne2, const QPointF& w) const {
  auto ligne = [&](int id, Point& a, Point& b) {
    if (id == AXE_H) { a = {0, 0}; b = {1, 0}; return true; }
    if (id == AXE_V) { a = {0, 0}; b = {0, 1}; return true; }
    const ElementEsquisse* l = m_e.element(id);
    if (!l || l->type != TypeElement::Ligne) return false;
    a = pt(l->p1);
    b = pt(l->p2);
    return true;
  };
  Point a1, b1, a2, b2;
  if (!ligne(ligne1, a1, b1) || !ligne(ligne2, a2, b2)) return 0;
  const double d1x = b1.x - a1.x, d1y = b1.y - a1.y, d2x = b2.x - a2.x, d2y = b2.y - a2.y;
  const double det = d1x * d2y - d1y * d2x;
  if (std::abs(det) < 1e-12) return 0;
  const double t = ((a2.x - a1.x) * d2y - (a2.y - a1.y) * d2x) / det;
  const double ix = a1.x + t * d1x, iy = a1.y + t * d1y;
  const double av = std::atan2(w.y() - iy, w.x() - ix);
  // Parmi les quatre secteurs (chaque ligne dans un sens ou l'autre), celui qui contient la souris : l'angle orienté
  // de la première direction à la souris a le signe de l'angle du secteur et une ouverture moindre
  for (int s = 0; s < 4; ++s) {
    const double s1 = (s & 1) ? -1 : 1, s2 = (s & 2) ? -1 : 1;
    const double t1 = std::atan2(s1 * d1y, s1 * d1x), t2 = std::atan2(s2 * d2y, s2 * d2x);
    double ouverture = t2 - t1, versSouris = av - t1;
    while (ouverture <= -PI) ouverture += 2 * PI;
    while (ouverture > PI) ouverture -= 2 * PI;
    while (versSouris <= -PI) versSouris += 2 * PI;
    while (versSouris > PI) versSouris -= 2 * PI;
    if (versSouris * ouverture > 0 && std::abs(versSouris) <= std::abs(ouverture)) return s;
  }
  return 0;
}

Esquisseur::Proposition Esquisseur::proposerCote(const std::vector<Cible>& refs, const QPointF& souris) const {
  Proposition pr;
  if (refs.empty()) return pr;
  auto estPt = [](const Cible& c) { return c.type == Cible::Point; };
  auto estLigne = [&](const Cible& c) {
    if (c.type == Cible::Axe) return true;
    const ElementEsquisse* el = c.type == Cible::Element ? m_e.element(c.id) : nullptr;
    return el && el->type == TypeElement::Ligne;
  };
  auto estCourbe = [&](const Cible& c) {
    const ElementEsquisse* el = c.type == Cible::Element ? m_e.element(c.id) : nullptr;
    return el && el->type != TypeElement::Ligne;
  };
  const QPointF w = monde(souris);
  auto orientation = [&](Point a, Point b) {
    const double m = 3 / m_s;
    const double x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    const bool dansX = w.x() >= x0 - m && w.x() <= x1 + m, dansY = w.y() >= y0 - m && w.y() <= y1 + m;
    if (dansX && !dansY && x1 - x0 > 1e-6) return 1;  // horizontale
    if (dansY && !dansX && y1 - y0 > 1e-6) return 2;  // verticale
    return 0;
  };
  // Une référence disparue (supprimée pendant le placement de la cote) ne donne aucune proposition (EI-01)
  if (!std::all_of(refs.begin(), refs.end(), [this](const Cible& c) { return cibleExiste(c); })) return pr;
  pr.valide = true;
  if (refs.size() == 1) {
    const Cible& c = refs[0];
    if (estCourbe(c)) {
      const ElementEsquisse* el = m_e.element(c.id);
      pr.type = el->type == TypeElement::Cercle ? TypeContrainte::Diametre : TypeContrainte::Rayon;
      pr.refs = {c.id};
    } else if (c.type == Cible::Element) {
      const ElementEsquisse* l = m_e.element(c.id);
      const int o = orientation(pt(l->p1), pt(l->p2));
      if (o == 0) { pr.type = TypeContrainte::Longueur; pr.refs = {c.id}; }
      else { pr.type = o == 1 ? TypeContrainte::DistanceX : TypeContrainte::DistanceY; pr.refs = {l->p1, l->p2}; }
    } else if (estPt(c) && c.id != 0) {
      const int o = orientation({0, 0}, pt(c.id));
      pr.type = o == 2 ? TypeContrainte::DistanceY : o == 1 ? TypeContrainte::DistanceX : TypeContrainte::Distance;
      pr.refs = {0, c.id};
    } else {
      pr.valide = false;
    }
    return pr;
  }
  const Cible a = refs[0], b = refs[1];
  if (estPt(a) && estPt(b)) {
    const int o = orientation(pt(a.id), pt(b.id));
    pr.type = o == 1 ? TypeContrainte::DistanceX : o == 2 ? TypeContrainte::DistanceY : TypeContrainte::Distance;
    pr.refs = {a.id, b.id};
  } else if (estPt(a) && estLigne(b)) {
    pr.type = TypeContrainte::Distance;
    pr.refs = {a.id, b.id};
  } else if (estLigne(a) && estPt(b)) {
    pr.type = TypeContrainte::Distance;
    pr.refs = {b.id, a.id};
  } else if (estLigne(a) && estLigne(b)) {
    auto dir = [&](const Cible& c) {
      if (c.id == AXE_H) return QPointF(1, 0);
      if (c.id == AXE_V) return QPointF(0, 1);
      const ElementEsquisse* l = m_e.element(c.id);
      const Point p1 = pt(l->p1), p2 = pt(l->p2);
      return unitaire(QPointF(p2.x - p1.x, p2.y - p1.y));
    };
    const QPointF u = dir(a), v = dir(b);
    const bool paralleles = std::abs(u.x() * v.y() - u.y() * v.x()) < 0.02;
    pr.type = paralleles ? TypeContrainte::Distance : TypeContrainte::Angle;
    pr.refs = {a.id, b.id};
    if (!paralleles) pr.valeur2 = secteurAngle(a.id, b.id, w);  // le quart d'angle où se trouve la souris (S2)
  } else if ((estPt(a) && estCourbe(b)) || (estCourbe(a) && estPt(b))) {
    // Distance d'un point au centre d'un cercle.
    const Cible p = estPt(a) ? a : b, cc = estPt(a) ? b : a;
    pr.type = TypeContrainte::Distance;
    pr.refs = {p.id, m_e.element(cc.id)->centre};
  } else if (estCourbe(a) && estCourbe(b)) {
    pr.type = TypeContrainte::Distance;
    pr.refs = {m_e.element(a.id)->centre, m_e.element(b.id)->centre};
  } else {
    pr.valide = false;
  }
  return pr;
}

void Esquisseur::ouvrirEditeur(int id) {
  for (const auto& c : m_e.contraintes) {
    if (c.id != id || !estCote(c.type)) continue;
    // Une cote de référence mesure sans piloter : comme dans le tableau des cotes, elle ne s'édite pas (EI-21)
    if (c.reference) {
      emit etatChange(QStringLiteral("Cote de référence : elle mesure sans piloter. Rends-la pilotante (barre contextuelle) pour imposer une valeur."), 1);
      return;
    }
    m_coteEditee = id;
    const QPointF L = positionEtiquette(c);
    // Une cote qui suit une variable s'ouvre sur son expression, qui n'est réécrite que si le texte change (EI-07)
    m_texteInitialEditeur = !c.expression.empty() ? c.expression : ecrireValeur(std::abs(c.valeur), c.type == TypeContrainte::Angle, m_unites);
    const int largeur = std::max(90, QFontMetrics(m_editeur->font()).horizontalAdvance(QString::fromStdString(m_texteInitialEditeur)) + 28);
    m_editeur->setGeometry(QRect(static_cast<int>(L.x()) - largeur / 2, static_cast<int>(L.y()) - 14, largeur, 28));
    m_editeur->setText(QString::fromStdString(m_texteInitialEditeur));
    m_editeur->show();
    m_editeur->setFocus();
    m_editeur->selectAll();
    emit etatChange(QStringLiteral("Tape la valeur puis Entrée (Échap pour abandonner) : unités, opérations et variables acceptées (« 2,5 cm », « 1 in », « L/2 »)."), 1);
    update();
    return;
  }
}

void Esquisseur::validerEditeur() {
  // La saisie compte même si l'éditeur vient d'être caché avec la vue (✔ cliqué sans Entrée, EI-06)
  if (!m_coteEditee) return;
  const int id = m_coteEditee;
  const std::string texteSaisi = m_editeur->text().trimmed().toStdString();
  m_editeur->hide();
  m_coteEditee = 0;
  if (isVisible()) setFocus();
  if (texteSaisi == m_texteInitialEditeur) { update(); return; }  // rien de changé : expression et état gardés
  QString erreur;
  if (!appliquerTexteCote(id, texteSaisi, &erreur)) {
    emit etatChange(erreur, 2);
    update();
  }
}

void Esquisseur::terminerSaisie(bool garder) {
  if (!m_coteEditee) return;
  if (garder) { validerEditeur(); return; }
  m_editeur->hide();
  m_coteEditee = 0;
  update();
}

bool Esquisseur::appliquerTexteCote(int id, const std::string& texteSaisi, QString* erreur) {
  auto refus = [erreur](const QString& m) { if (erreur) *erreur = m; return false; };
  for (auto& c : m_e.contraintes) {
    if (c.id != id || !estCote(c.type)) continue;
    if (c.reference) return refus(QStringLiteral("Cote de référence : elle mesure sans piloter, et ne se modifie pas."));
    const bool angle = c.type == TypeContrainte::Angle;
    double v = 0;
    std::string err;
    if (!lireValeur(texteSaisi, angle, m_unites, v, &err, &m_variables))
      return refus(QStringLiteral("Valeur illisible (%1) : la cote garde sa valeur.").arg(QString::fromStdString(err)));
    const bool signe = c.type == TypeContrainte::DistanceX || c.type == TypeContrainte::DistanceY;
    if (!signe && !angle && v <= 0) return refus(QStringLiteral("Une longueur doit être positive : la cote garde sa valeur."));
    if (angle && (v <= -360 || v >= 360)) return refus(QStringLiteral("Un angle s'écrit entre −360° et 360° : la cote garde sa valeur."));
    // L'expression est gardée si elle cite une variable de la pièce : la cote suivra la variable.
    bool citeVariable = false;
    for (size_t k = 0; k < texteSaisi.size();) {
      if (std::isalpha(static_cast<unsigned char>(texteSaisi[k])) || texteSaisi[k] == '_') {
        const size_t d = k;
        while (k < texteSaisi.size() && (std::isalnum(static_cast<unsigned char>(texteSaisi[k])) || texteSaisi[k] == '_')) ++k;
        if (m_variables.count(texteSaisi.substr(d, k - d))) citeVariable = true;
      } else ++k;
    }
    const std::string expression = citeVariable ? texteSaisi : std::string();
    const double valeur = valeurReecrite(c, v);
    if (std::abs(valeur - c.valeur) < 1e-12 && expression == c.expression) return true;  // rien à changer : pas d'étape
    c.valeur = valeur;
    c.expression = expression;
    apresModification();
    return true;
  }
  return refus(QStringLiteral("Cette cote n'existe plus."));
}

bool Esquisseur::changerCote(int id, const QString& texte, QString* erreur) { return appliquerTexteCote(id, texte.trimmed().toStdString(), erreur); }

// Lie la forme qui vient d'être créée aux points existants que l'outil a cliqués : coïncidence avec le point
// nouveau posé au même endroit, sinon « sur la courbe » d'un cercle, d'un arc ou d'une ligne nouvelle qui y passe.
void Esquisseur::lierAuxPointsCliques(const std::set<int>& pointsAvant, const std::set<int>& elementsAvant, const std::vector<std::pair<QPointF, int>>& clics) {
  for (const auto& [pos, existant] : clics) {
    if (existant < 0 || (existant > 0 && !m_e.point(existant))) continue;
    const double tol = 1e-6 * std::max(1.0, std::hypot(pos.x(), pos.y()));
    int nouveau = -1;
    for (const auto& q : m_e.points)
      if (!pointsAvant.count(q.id) && std::hypot(q.x - pos.x(), q.y - pos.y()) < tol) { nouveau = q.id; break; }
    if (nouveau >= 0) { m_e.contraindre(TypeContrainte::Coincidence, {nouveau, existant}); continue; }
    for (const auto& el : m_e.elements) {
      if (elementsAvant.count(el.id)) continue;
      bool passe = false;
      if (el.type == TypeElement::Ligne) {
        const Point a = pt(el.p1), b = pt(el.p2);
        const double l = std::hypot(b.x - a.x, b.y - a.y);
        passe = l > 1e-12 && std::abs((b.x - a.x) * (pos.y() - a.y) - (b.y - a.y) * (pos.x() - a.x)) / l < tol;
      } else {
        const Point c = pt(el.centre);
        passe = std::abs(std::hypot(pos.x() - c.x, pos.y() - c.y) - m_e.rayonDe(el)) < tol;
      }
      if (passe) { m_e.contraindre(TypeContrainte::PointSur, {existant, el.id}); break; }
    }
  }
}

void Esquisseur::mousePressEvent(QMouseEvent* e) {
  setFocus();
  m_dyn = SaisieDynamique{};  // un clic pose à la souris : la saisie en cours est abandonnée
  if (m_coteEditee) validerEditeur();
  m_souris = e->position();
  // Clic sur une arête de la pièce : elle entre dans l'esquisse (construction, fixe), puis le clic continue normalement
  if (e->button() == Qt::LeftButton && (m_outil == Outil::Selection || m_outil == Outil::Cote)) {
    const Cible c = toucher(e->position());
    if (c.type == Cible::Externe) {
      // Outil Cote : l'arête est reprise pour qu'on puisse coter jusqu'à elle ; sélection : elle est seulement
      // choisie (Ctrl ou Maj : ajoutée), un double-clic ou « Convertir les entités » la reprend (S3)
      if (m_outil == Outil::Cote) importerExterne(c.id);
      else {
        choisirExterne(c.id, e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
        return;
      }
    }
  }
  m_depart = e->position();
  m_souris0 = e->position();
  m_bouge = false;
  majEchelle();
  if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
    if (e->button() == Qt::RightButton && (!m_clics.empty() || !m_coteRefs.empty())) { finirOutil(); update(); return; }
    m_geste = Geste::Vue;
    m_vueBouton = e->button();
    if (e->button() == Qt::RightButton) m_minuterieRadial->start();
    m_oxDepart = m_ox;
    m_oyDepart = m_oy;
    return;
  }
  const bool ajout = m_selectionCumulative || (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier));
  m_dernierClic = monde(e->position());
  m_aDernierClic = true;
  m_dernierClicPoint = pointSous(e->position());
  m_barreContexte->hide();
  if (m_outil == Outil::Selection) {
    const Cible c = toucher(e->position());
    if (c.type == Cible::Rien) {
      if (!ajout) m_selection.clear();
      m_geste = Geste::Cadre;
      update();
      return;
    }
    if (ajout) {
      auto it = std::find(m_selection.begin(), m_selection.end(), c);
      if (it != m_selection.end()) m_selection.erase(it); else m_selection.push_back(c);
    } else if (!selectionne(c)) {
      m_selection = {c};
    }
    m_departPoints.clear();
    m_geste = Geste::Aucun;
    if (c.type == Cible::Contrainte) {
      for (const auto& k : m_e.contraintes)
        if (k.id == c.id && estCote(k.type)) { m_geste = Geste::Etiquette; m_etiquette = c.id; }
      update();
      return;
    }
    auto retenir = [&](int id) {
      if (id <= 0) return;
      for (const auto& x : m_departPoints) if (x.first == id) return;
      m_departPoints.push_back({id, pt(id)});
    };
    if (c.type == Cible::Point) retenir(c.id);
    if (c.type == Cible::Element)
      if (const ElementEsquisse* el = m_e.element(c.id)) {
        if (el->type == TypeElement::Cercle) retenir(el->centre);
        else { retenir(el->p1); retenir(el->p2); if (el->type == TypeElement::Arc) retenir(el->centre); }
      }
    if (!m_departPoints.empty()) m_geste = Geste::Points;
    update();
    return;
  }
  if (m_outil == Outil::Cote) {
    const Cible c = toucher(e->position(), false);
    if (c.type != Cible::Rien && (m_coteRefs.empty() || c != m_coteRefs.back()) && m_coteRefs.size() < 2) {
      m_coteRefs.push_back(c);
      if (m_coteRefs.size() == 2 && m_coteRefs[0].type == Cible::Point && m_coteRefs[1].type == Cible::Point)
        emit etatChange(QStringLiteral("Place la cote de distance, ou clique un troisième point : angle par trois points, le premier étant le sommet."), 1);
      update();
      return;
    }
    // Angle par trois points (S2) : le premier point est le sommet ; deux lignes de construction le relient aux deux
    // autres, et l'angle entre elles est coté
    if (m_coteRefs.size() == 2 && c.type == Cible::Point && m_coteRefs[0].type == Cible::Point && m_coteRefs[1].type == Cible::Point &&
        c != m_coteRefs[0] && c != m_coteRefs[1]) {
      const int sommet = m_coteRefs[0].id, p1 = m_coteRefs[1].id, p2 = c.id;
      const Point S = pt(sommet), A = pt(p1), B = pt(p2);
      const double cr = (A.x - S.x) * (B.y - S.y) - (A.y - S.y) * (B.x - S.x);
      if (std::abs(cr) < 1e-9 * std::max(1.0, std::hypot(A.x - S.x, A.y - S.y) * std::hypot(B.x - S.x, B.y - S.y))) {
        emit etatChange(QStringLiteral("Les trois points sont alignés : pas d'angle à coter."), 2);
        m_coteRefs.clear();
        update();
        return;
      }
      const bool resolvaitAvant = m_etat.resolue && m_etat.enConflit.empty();
      const int l1 = m_e.ajouterLigne(sommet, p1, true), l2 = m_e.ajouterLigne(sommet, p2, true);
      ContrainteEsquisse k;
      k.id = m_e.prochainId++;
      k.type = TypeContrainte::Angle;
      k.refs = {l1, l2};
      k.valeur = mesurer(m_e, k);
      m_e.contraintes.push_back(k);
      const bool redondante = coteRedondante(m_e, k.id, resolvaitAvant);
      if (redondante) m_e.contraintes.back().reference = true;
      m_coteRefs.clear();
      apresModification();
      if (redondante) emit etatChange(QStringLiteral("Cote de référence : cet angle est déjà imposé par les autres contraintes."), 1);
      else ouvrirEditeur(k.id);
      return;
    }
    const Proposition pr = proposerCote(m_coteRefs, e->position());
    if (!pr.valide) {
      if (!m_coteRefs.empty()) emit etatChange(QStringLiteral("Pas de cote possible entre ces éléments."), 2);
      m_coteRefs.clear();
      update();
      return;
    }
    const bool resolvaitAvant = m_etat.resolue && m_etat.enConflit.empty();
    ContrainteEsquisse k;
    k.id = m_e.prochainId++;
    k.type = pr.type;
    k.refs = pr.refs;
    k.valeur2 = pr.valeur2;
    k.valeur = mesurer(m_e, k);
    k.place = true;
    const QPointF w = monde(e->position());
    k.lx = w.x();
    k.ly = w.y();
    m_e.contraintes.push_back(k);
    // Déjà imposée par le reste : la cote devient une cote de référence, sans conflit.
    const bool redondante = coteRedondante(m_e, k.id, resolvaitAvant);
    if (redondante) m_e.contraintes.back().reference = true;
    m_coteRefs.clear();
    apresModification();
    if (redondante) emit etatChange(QStringLiteral("Cote de référence : cette mesure est déjà imposée par les autres contraintes."), 1);
    else ouvrirEditeur(k.id);
    return;
  }
  if (m_outil == Outil::Point) {
    obtenirPoint(e->position());
    if (!aChange()) { emit etatChange(QStringLiteral("Un point existe déjà ici."), 1); return; }
    apresModification();
    return;
  }
  if (m_outil == Outil::Conge) {
    // Le coin : le point cliqué, ou le plus proche à moins de 30 pixels ; le clic dans l'angle choisit lequel arrondir
    // quand plus de deux lignes partent du coin.
    int p = pointSous(e->position());
    if (p <= 0) p = coinProche(e->position());
    if (p <= 0) { emit etatChange(QStringLiteral("Clique le coin entre deux lignes (ou juste à côté, dans l'angle à arrondir)."), 2); return; }
    const QPointF wc = monde(e->position());
    double lmin = 1e9;
    for (const auto& el : m_e.elements)
      if (el.type == TypeElement::Ligne && (el.p1 == p || el.p2 == p)) {
        const Point a = pt(el.p1), b = pt(el.p2);
        lmin = std::min(lmin, std::hypot(b.x - a.x, b.y - a.y));
      }
    const double r = std::max(0.5, std::round(lmin / 5 * 2) / 2);
    std::string err;
    if (!m_e.congeEsquisse(p, r, &err, wc.x(), wc.y())) { emit etatChange(QString::fromStdString(err), 2); return; }
    apresModification();
    for (auto it = m_e.contraintes.rbegin(); it != m_e.contraintes.rend(); ++it)
      if (it->type == TypeContrainte::Rayon) { ouvrirEditeur(it->id); break; }
    return;
  }
  QPointF w = monde(e->position());
  const int clicExistant = pointSous(e->position());  // point existant cliqué : la forme y sera liée (EI-09)
  auto pointsActuels = [this] { std::set<int> r; for (const auto& q : m_e.points) r.insert(q.id); return r; };
  auto elementsActuels = [this] { std::set<int> r; for (const auto& el : m_e.elements) r.insert(el.id); return r; };
  // Taille à l'écran sous laquelle une forme est refusée (deux clics confondus, EI-16)
  auto tropPetit = [this](double mm) { return mm * m_s < 2; };
  // Point que ce clic vient de créer pour une forme refusée : retiré aussitôt s'il ne sert à rien
  auto retirerSiCree = [this](int id) {
    if (std::find(m_pointsOutil.begin(), m_pointsOutil.end(), id) == m_pointsOutil.end()) return;
    auto restes = m_pointsOutil;
    std::erase(restes, id);
    m_pointsOutil = {id};
    retirerPointsOutil();
    m_pointsOutil = restes;
  };
  {
    const int existant = clicExistant;
    if (existant >= 0) { const Point q = pt(existant); w = QPointF(q.x, q.y); }
    else {
      const double grille = m_s >= 8 ? 0.5 : m_s >= 2 ? 1 : 5;
      w = QPointF(std::round(w.x() / grille) * grille, std::round(w.y() / grille) * grille);
    }
  }
  if (m_outil == Outil::ChanfreinEsquisse) {
    int p = pointSous(e->position());
    if (p <= 0) p = coinProche(e->position());
    if (p <= 0) { emit etatChange(QStringLiteral("Clique le coin entre deux lignes (ou juste à côté, dans l'angle voulu)."), 2); return; }
    const QPointF wc = monde(e->position());
    const double d = m_distanceChanfrein;  // réglée avant le clic, sans boîte modale (U1)
    if (m_e.chanfreinEsquisse(p, d, wc.x(), wc.y()) == 0) { emit etatChange(QStringLiteral("Il faut un coin entre deux lignes, plus longues que la distance."), 2); return; }
    apresModification();
    return;
  }
  if (m_outil == Outil::RainureArc) {
    m_libres.push_back(w);
    m_libresPoints.push_back(clicExistant);
    if (m_libres.size() < 3) { update(); return; }
    const bool ok = true;
    const double l = m_largeurRainureArc;  // réglée avant les clics, sans boîte modale (U1)
    const QPointF c = m_libres[0], a = m_libres[1], b = m_libres[2];
    std::vector<std::pair<QPointF, int>> clics;
    for (std::size_t i = 0; i < m_libres.size(); ++i) clics.push_back({m_libres[i], m_libresPoints[i]});
    m_libres.clear();
    m_libresPoints.clear();
    if (!ok) { update(); return; }
    const auto pa = pointsActuels(), ea = elementsActuels();
    if (m_e.ajouterRainureArc(c.x(), c.y(), a.x(), a.y(), b.x(), b.y(), l).empty()) { emit etatChange(QStringLiteral("La largeur doit être plus petite que le diamètre de l'arc."), 2); update(); return; }
    lierAuxPointsCliques(pa, ea, clics);
    apresModification();
    return;
  }
  if (m_outil == Outil::Ajuster || m_outil == Outil::Prolonger) {
    const Cible c = toucher(e->position());
    if (c.type != Cible::Element) { emit etatChange(QStringLiteral("Clique un élément de l'esquisse."), 2); return; }
    const QPointF wc = monde(e->position());
    const bool ok = m_outil == Outil::Ajuster ? m_e.ajuster(c.id, wc.x(), wc.y()) : m_e.prolonger(c.id, wc.x(), wc.y());
    if (!ok) { emit etatChange(m_outil == Outil::Prolonger ? QStringLiteral("Rien à rejoindre dans cette direction (seules les lignes se prolongent).") : QStringLiteral("Rien à ajuster ici."), 2); return; }
    apresModification();
    return;
  }
  // Outils à plusieurs points libres
  if (m_outil == Outil::Rectangle3Points || m_outil == Outil::Parallelogramme || m_outil == Outil::Cercle3Points) {
    m_libres.push_back(w);
    m_libresPoints.push_back(clicExistant);
    if (m_libres.size() < 3) { update(); return; }
    const QPointF a = m_libres[0], b = m_libres[1], c = m_libres[2];
    std::vector<std::pair<QPointF, int>> clics;
    for (std::size_t i = 0; i < m_libres.size(); ++i) clics.push_back({m_libres[i], m_libresPoints[i]});
    const auto pa = pointsActuels(), ea = elementsActuels();
    bool ok = true;
    if (m_outil == Outil::Rectangle3Points) ok = !m_e.ajouterRectangle3Points(a.x(), a.y(), b.x(), b.y(), c.x(), c.y()).empty();
    else if (m_outil == Outil::Parallelogramme) ok = !m_e.ajouterParallelogramme(a.x(), a.y(), b.x(), b.y(), c.x(), c.y()).empty();
    else ok = m_e.ajouterCercle3Points(a.x(), a.y(), b.x(), b.y(), c.x(), c.y()) != 0;
    m_libres.clear();
    m_libresPoints.clear();
    if (!ok) { emit etatChange(QStringLiteral("Points alignés : la forme ne peut pas être construite."), 2); update(); return; }
    lierAuxPointsCliques(pa, ea, clics);
    apresModification();
    return;
  }
  if (m_outil == Outil::RectangleCentre) {
    if (m_clics.empty()) { m_clics.push_back(obtenirPoint(e->position())); update(); return; }
    const int pc = m_clics.front();
    const Point c = pt(pc);
    if (tropPetit(std::abs(w.x() - c.x)) || tropPetit(std::abs(w.y() - c.y))) {
      emit etatChange(QStringLiteral("Le coin est aligné avec le centre : clique un coin en dehors des axes du centre."), 2);
      update();
      return;
    }
    const auto pa = pointsActuels(), ea = elementsActuels();
    int centre = 0;
    m_e.ajouterRectangleCentre(c.x, c.y, w.x(), w.y(), &centre);
    lierAuxPointsCliques(pa, ea, {{w, clicExistant}});
    // Le centre cliqué (accroché ou non) devient celui du rectangle.
    bool utilise = pc == 0;
    for (const auto& el : m_e.elements) if (el.p1 == pc || el.p2 == pc || el.centre == pc) utilise = true;
    if (utilise) m_e.contraindre(TypeContrainte::Coincidence, {centre, pc});
    else {
      for (auto& k : m_e.contraintes) for (int& r : k.refs) if (r == pc) r = centre;
      m_e.points.erase(std::remove_if(m_e.points.begin(), m_e.points.end(), [pc](const PointEsquisse& x) { return x.id == pc; }), m_e.points.end());
    }
    m_clics.clear();
    apresModification();
    return;
  }
  if (m_outil == Outil::LigneMilieu) {
    if (m_clics.empty()) { m_clics.push_back(obtenirPoint(e->position())); update(); return; }
    const auto pa = pointsActuels(), ea = elementsActuels();
    if (m_e.ajouterLigneMilieu(m_clics.front(), w.x(), w.y()) == 0) { emit etatChange(QStringLiteral("L'extrémité est confondue avec le milieu."), 2); update(); return; }
    lierAuxPointsCliques(pa, ea, {{w, clicExistant}});
    m_clics.clear();
    apresModification();
    return;
  }
  if (m_outil == Outil::ArcTangent) {
    if (m_clics.empty()) {
      const int p = pointSous(e->position());
      bool extremite = false;
      for (const auto& el : m_e.elements) if (el.type != TypeElement::Cercle && (el.p1 == p || el.p2 == p)) extremite = true;
      if (p < 0 || !extremite) { emit etatChange(QStringLiteral("Clique d'abord l'extrémité d'une ligne ou d'un arc."), 2); return; }
      m_clics.push_back(p);
      m_arcPrecedent = 0;  // élément de départ choisi par le geste (ES-34)
      update();
      return;
    }
    const int depart = m_clics.front();
    if (clicExistant == depart) { emit etatChange(QStringLiteral("L'arrivée doit être différente du départ."), 2); return; }
    const auto ptsAvant = pointsActuels(), elsAvant = elementsActuels();
    const int arc = m_e.ajouterArcTangent(depart, w.x(), w.y(), m_arcPrecedent);
    if (arc == 0) { emit etatChange(QStringLiteral("Arrivée dans le prolongement : pas d'arc tangent possible."), 2); update(); return; }
    lierAuxPointsCliques(ptsAvant, elsAvant, {{w, clicExistant}});
    // On enchaîne : l'arrivée devient le départ du tracé suivant.
    const ElementEsquisse* ea = m_e.element(arc);
    m_clics = {ea->p1 == depart ? ea->p2 : ea->p1};
    m_arcPrecedent = arc;  // l'arc suivant prolonge celui-ci
    apresModification();
    return;
  }
  switch (m_outil) {
    case Outil::Ligne:
    case Outil::LigneConstruction: {
      // Inférence (S1) : l'arrivée est accrochée à la direction ou à l'alignement deviné, les relations suivent
      const bool alt = e->modifiers() & Qt::AltModifier;
      const Inference inf = m_clics.empty() ? Inference{} : inferer(m_clics.back(), e->position(), alt);
      const int p = inf.active ? m_e.ajouterPoint(inf.fin.x(), inf.fin.y()) : obtenirPoint(e->position());
      if (!m_clics.empty() && m_clics.back() != p) {
        const int a = m_clics.back();
        const int l = m_e.ajouterLigne(a, p, m_outil == Outil::LigneConstruction || m_modeConstruction);
        // Une relation n'est posée que si elle ne contredit pas l'existant (EI-23)
        auto poser = [&](TypeContrainte t, const std::vector<int>& refs) {
          Esquisse essai = m_e;
          essai.contraindre(t, refs);
          Esquisse sans = m_e;
          const EtatEsquisse avant = resoudre(sans), apres = resoudre(essai);
          if (apres.enConflit.empty() && (apres.resolue || !avant.resolue)) m_e.contraindre(t, refs);
        };
        if (inf.active) {
          for (const auto& [type, refs] : inf.relations) {
            std::vector<int> r;
            for (int x : refs) r.push_back(x == -1 ? l : x == -2 ? p : x);
            poser(type, r);
          }
        } else if (!alt) {
          // Arrivée sur un point existant : relation H ou V devinée
          const Point A = pt(a), B = pt(p);
          const double ang = std::abs(std::atan2(B.y - A.y, B.x - A.x)) * 180 / PI;
          const bool horiz = ang < 3 || ang > 177, vert = std::abs(ang - 90) < 3;
          if (horiz || vert) poser(horiz ? TypeContrainte::Horizontal : TypeContrainte::Vertical, {l});
        }
        const bool ferme = p == m_clics.front();
        m_clics.push_back(p);
        if (ferme) m_clics.clear();
        apresModification();
        return;
      }
      if (m_clics.empty()) m_clics.push_back(p);
      break;
    }
    case Outil::Rectangle: {
      const int p = obtenirPoint(e->position());
      if (m_clics.empty()) { m_clics.push_back(p); break; }
      const int pa = m_clics.front(), pb = p;
      const Point A = pt(pa), B = pt(pb);
      if (tropPetit(std::abs(B.x - A.x)) || tropPetit(std::abs(B.y - A.y))) {
        // Deux coins alignés : rien n'est créé, le premier coin reste en attente (EI-16)
        if (pb != pa) retirerSiCree(pb);
        emit etatChange(QStringLiteral("Les deux coins sont alignés : clique le coin opposé en diagonale."), 2);
        update();
        return;
      }
      {
        const auto l = m_e.ajouterRectangle(A.x, A.y, B.x, B.y);
        // Les coins cliqués deviennent ceux du rectangle : on remplace les points de clic par ses coins.
        for (const auto& [ancien, coin] : {std::pair{pa, m_e.element(l[0])->p1}, std::pair{pb, m_e.element(l[1])->p2}}) {
          bool utilise = false;
          for (const auto& el : m_e.elements) if (el.p1 == ancien || el.p2 == ancien || el.centre == ancien) utilise = true;
          if (ancien == 0 || utilise) { m_e.contraindre(TypeContrainte::Coincidence, {coin, ancien}); continue; }
          // Point de clic libre : ses contraintes d'accroche passent au coin, puis il disparaît.
          for (auto& k : m_e.contraintes) for (int& r : k.refs) if (r == ancien) r = coin;
          m_e.points.erase(std::remove_if(m_e.points.begin(), m_e.points.end(), [ancien](const PointEsquisse& x) { return x.id == ancien; }), m_e.points.end());
        }
      }
      m_clics.clear();
      apresModification();
      return;
    }
    case Outil::Cercle: {
      if (m_clics.empty()) { m_clics.push_back(obtenirPoint(e->position())); break; }
      const Point c = pt(m_clics.front());
      const double r = std::hypot(w.x() - c.x, w.y() - c.y);
      if (tropPetit(r)) { emit etatChange(QStringLiteral("Cercle trop petit : clique un point du cercle, à distance du centre."), 2); update(); return; }
      const auto pa = pointsActuels(), ea = elementsActuels();
      m_e.ajouterCercle(m_clics.front(), r);
      lierAuxPointsCliques(pa, ea, {{w, clicExistant}});
      m_clics.clear();
      apresModification();
      return;
    }
    case Outil::Arc: {
      const int q = obtenirPoint(e->position());
      if (m_clics.size() == 1 && (q == m_clics[0] || tropPetit(std::hypot(pt(q).x - pt(m_clics[0]).x, pt(q).y - pt(m_clics[0]).y)))) {
        if (q != m_clics[0]) retirerSiCree(q);
        emit etatChange(QStringLiteral("Le début de l'arc doit être à distance du centre."), 2);
        update();
        return;
      }
      if (m_clics.size() == 2 && (q == m_clics[1] || tropPetit(std::hypot(pt(q).x - pt(m_clics[1]).x, pt(q).y - pt(m_clics[1]).y)))) {
        if (q != m_clics[1]) retirerSiCree(q);
        emit etatChange(QStringLiteral("La fin de l'arc doit être distincte de son début."), 2);
        update();
        return;
      }
      m_clics.push_back(q);
      if (m_clics.size() == 3) {
        m_e.ajouterArc(m_clics[0], m_clics[1], m_clics[2]);
        m_clics.clear();
        apresModification();
        return;
      }
      break;
    }
    case Outil::Arc3Points: {
      if (m_clics.size() < 2) { m_clics.push_back(obtenirPoint(e->position())); break; }
      if (m_e.ajouterArc3Points(m_clics[0], m_clics[1], w.x(), w.y()) == 0) emit etatChange(QStringLiteral("Trois points alignés : pas d'arc possible."), 2);
      m_clics.clear();
      apresModification();
      return;
    }
    case Outil::Rainure: {
      if (m_clics.size() < 2) {
        if (m_clics.size() == 1 && tropPetit(std::hypot(w.x() - pt(m_clics[0]).x, w.y() - pt(m_clics[0]).y))) {
          emit etatChange(QStringLiteral("Le second centre doit être à distance du premier."), 2);
          return;
        }
        const int q = m_e.ajouterPoint(w.x(), w.y());  // repère provisoire, retiré à la fin ou à l'abandon
        m_pointsOutil.push_back(q);
        m_clics.push_back(q);
        m_libresPoints.push_back(clicExistant);
        break;
      }
      const Point c1 = pt(m_clics[0]), c2 = pt(m_clics[1]);
      const double L = std::max(1e-9, std::hypot(c2.x - c1.x, c2.y - c1.y));
      const double r = std::abs((w.x() - c1.x) * -(c2.y - c1.y) / L + (w.y() - c1.y) * (c2.x - c1.x) / L);
      if (tropPetit(r)) { emit etatChange(QStringLiteral("Largeur nulle : clique à distance de l'axe de la rainure."), 2); return; }
      const std::vector<std::pair<QPointF, int>> clics = {{QPointF(c1.x, c1.y), m_libresPoints.size() > 0 ? m_libresPoints[0] : -1},
                                                        {QPointF(c2.x, c2.y), m_libresPoints.size() > 1 ? m_libresPoints[1] : -1}};
      for (int q : m_clics) m_e.points.erase(std::remove_if(m_e.points.begin(), m_e.points.end(), [q](const PointEsquisse& x) { return x.id == q; }), m_e.points.end());
      const auto pa = pointsActuels(), ea = elementsActuels();
      m_e.ajouterRainure(c1.x, c1.y, c2.x, c2.y, r);
      lierAuxPointsCliques(pa, ea, clics);
      m_clics.clear();
      m_libresPoints.clear();
      apresModification();
      return;
    }
    case Outil::Polygone: {
      if (m_clics.empty()) {
        const int q = m_e.ajouterPoint(w.x(), w.y());  // repère provisoire du centre
        m_pointsOutil.push_back(q);
        m_clics.push_back(q);
        m_libresPoints = {clicExistant};
        break;
      }
      const Point c = pt(m_clics[0]);
      if (tropPetit(std::hypot(w.x() - c.x, w.y() - c.y))) { emit etatChange(QStringLiteral("Polygone trop petit : clique un sommet à distance du centre."), 2); return; }
      const std::vector<std::pair<QPointF, int>> clics = {{QPointF(c.x, c.y), m_libresPoints.empty() ? -1 : m_libresPoints[0]}, {w, clicExistant}};
      m_e.points.erase(std::remove_if(m_e.points.begin(), m_e.points.end(), [q = m_clics[0]](const PointEsquisse& x) { return x.id == q; }), m_e.points.end());
      const auto pa = pointsActuels(), ea = elementsActuels();
      if (m_polygoneCirconscrit) m_e.ajouterPolygoneCirconscrit(c.x, c.y, w.x(), w.y(), m_cotesPolygone);
      else m_e.ajouterPolygone(c.x, c.y, w.x(), w.y(), m_cotesPolygone);
      lierAuxPointsCliques(pa, ea, clics);
      m_clics.clear();
      m_libresPoints.clear();
      apresModification();
      return;
    }
    default: break;
  }
  update();
}

void Esquisseur::mouseMoveEvent(QMouseEvent* e) {
  m_souris = e->position();
  majEchelle();
  if (m_radialVisible) {
    m_secteur = secteurSous(e->position());
    update();
    return;
  }
  if (m_geste == Geste::Vue && (e->buttons() & (Qt::LeftButton | Qt::RightButton | Qt::MiddleButton))) {
    if (!m_bouge && (e->position() - m_depart).manhattanLength() < 5 && m_vueBouton == Qt::RightButton) return;
    m_minuterieRadial->stop();
    if (m_proj) {
      const QPointF d = e->position() - m_souris0;
      m_souris0 = e->position();
      if (m_vueBouton == Qt::MiddleButton) m_proj->tourner(d.x(), d.y());
      else m_proj->deplacer(d.x(), d.y());
    } else {
      m_ox = m_oxDepart + e->position().x() - m_depart.x();
      m_oy = m_oyDepart + e->position().y() - m_depart.y();
    }
    m_bouge = true;
    update();
    return;
  }
  if ((m_geste == Geste::Points || m_geste == Geste::Etiquette || m_geste == Geste::Cadre) && (e->buttons() & Qt::LeftButton)) {
    if (!m_bouge && (e->position() - m_depart).manhattanLength() < 3) return;
    m_bouge = true;
    if (m_geste == Geste::Points) {
      const QPointF d = monde(e->position()) - monde(m_depart);
      std::vector<std::pair<int, Point>> cibles;
      for (const auto& [id, q] : m_departPoints) cibles.push_back({id, Point{q.x + d.x(), q.y + d.y()}});
      m_etat = resoudre(m_e, cibles);
    } else if (m_geste == Geste::Etiquette) {
      for (auto& k : m_e.contraintes)
        if (k.id == m_etiquette) {
          const QPointF w = monde(e->position());
          k.place = true;
          k.lx = w.x();
          k.ly = w.y();
          // Cote d'angle glissée dans un autre quart (S2) : elle cote ce secteur, à sa valeur actuelle (la forme ne
          // bouge pas) ; une cote qui suit une expression garde son secteur
          if (k.type == TypeContrainte::Angle && k.refs.size() >= 2 && k.expression.empty()) {
            const int s = secteurAngle(k.refs[0], k.refs[1], w);
            if (s != (static_cast<int>(std::lround(k.valeur2)) & 3)) {
              k.valeur2 = s;
              const double v = mesurer(m_e, k);
              if (!k.reference) k.valeur = v;
            }
          }
        }
    }
    update();
    return;
  }
  const Cible s = (m_outil == Outil::Selection || m_outil == Outil::Cote) ? toucher(e->position(), m_outil == Outil::Selection) : Cible{};
  if (s != m_survol) m_survol = s;
  update();
}

void Esquisseur::mouseReleaseEvent(QMouseEvent* e) {
  m_minuterieRadial->stop();
  if (m_radialVisible) {
    m_radialVisible = false;
    m_geste = Geste::Aucun;
    const int s = secteurSous(e->position());
    update();
    switch (s) {
      case 0: setOutil(Outil::Cercle); break;
      case 1: setModeConstruction(!m_modeConstruction); break;
      case 2: setOutil(Outil::Cote); break;
      case 3: setOutil(Outil::Arc); break;
      case 4: setOutil(Outil::Rectangle); break;
      case 5: setOutil(Outil::Selection); break;
      case 6: setOutil(Outil::Ligne); break;
      case 7: supprimerSelection(); break;
      default: break;
    }
    return;
  }
  const Geste g = m_geste;
  m_geste = Geste::Aucun;
  if (g == Geste::Points && m_bouge) { apresModification(); return; }
  if (g == Geste::Etiquette && m_bouge) { apresModification(); return; }
  if (g == Geste::Cadre && m_bouge) {
    // Fenêtre (de gauche à droite) : éléments entièrement dedans ; capture (de droite à gauche) : éléments que le
    // cadre touche, même sans extrémité dedans (EI-14). Les courbes sont suivies sur leur tracé réel, projeté à
    // l'écran (juste aussi en vue inclinée) ; l'origine et les calques masqués ne sont jamais pris.
    const QRectF r = QRectF(m_depart, e->position()).normalized();
    const bool fenetre = e->position().x() >= m_depart.x();
    auto coupeSegment = [&r](QPointF a, QPointF b) {
      if (r.contains(a) || r.contains(b)) return true;
      const QPointF coins[4] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
      auto cote = [](QPointF p, QPointF q, QPointF x) { return (q.x() - p.x()) * (x.y() - p.y()) - (q.y() - p.y()) * (x.x() - p.x()); };
      for (int i = 0; i < 4; ++i) {
        const QPointF c = coins[i], d = coins[(i + 1) % 4];
        const double d1 = cote(a, b, c), d2 = cote(a, b, d), d3 = cote(c, d, a), d4 = cote(c, d, b);
        if (((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0))) return true;
      }
      return false;
    };
    for (const auto& p : m_e.points)
      if (p.id != 0 && r.contains(ecran(p.x, p.y)) && !selectionne({Cible::Point, p.id})) m_selection.push_back({Cible::Point, p.id});
    for (const auto& el : m_e.elements) {
      if (!calqueVisible(el.calque)) continue;
      std::vector<QPointF> trace;
      if (el.type == TypeElement::Ligne) trace = {ecran(pt(el.p1)), ecran(pt(el.p2))};
      else {
        const Point c = pt(el.centre);
        const double ray = m_e.rayonDe(el);
        double a0 = 0, a1 = 2 * PI;
        if (el.type == TypeElement::Arc) {
          const Point A = pt(el.p1), B = pt(el.p2);
          a0 = std::atan2(A.y - c.y, A.x - c.x);
          a1 = std::atan2(B.y - c.y, B.x - c.x);
          while (a1 <= a0) a1 += 2 * PI;
        }
        const int n = 64;
        for (int i = 0; i <= n; ++i) {
          const double t = a0 + (a1 - a0) * i / n;
          trace.push_back(ecran(c.x + ray * std::cos(t), c.y + ray * std::sin(t)));
        }
      }
      bool prendre = false;
      if (fenetre) prendre = std::all_of(trace.begin(), trace.end(), [&](const QPointF& q) { return r.contains(q); });
      else
        for (std::size_t i = 0; i + 1 < trace.size() && !prendre; ++i) prendre = coupeSegment(trace[i], trace[i + 1]);
      if (prendre && !selectionne({Cible::Element, el.id})) m_selection.push_back({Cible::Element, el.id});
    }
  }
  majBarreContexte();
  update();
}

void Esquisseur::mouseDoubleClickEvent(QMouseEvent* e) {
  const Cible c = toucher(e->position());
  if (c.type == Cible::Contrainte) { ouvrirEditeur(c.id); return; }
  if (c.type == Cible::Externe && m_outil == Outil::Selection) { importerExterne(c.id); return; }  // S3
  if (m_outil == Outil::Ligne || m_outil == Outil::LigneConstruction) { finirOutil(); update(); }
}

void Esquisseur::wheelEvent(QWheelEvent* e) {
  if (m_coteEditee) validerEditeur();
  if (m_proj) { m_proj->zoomer(e->position(), e->angleDelta().y() / 120.0); update(); return; }
  const QPointF p = e->position(), w = monde(p);
  m_s = std::clamp(m_s * std::exp(e->angleDelta().y() / 120.0 * 0.15), 0.2, 200.0);
  m_ox = p.x() - w.x() * m_s;
  m_oy = p.y() + w.y() * m_s;
  update();
}

void Esquisseur::leaveEvent(QEvent*) {
  m_survol = {};
  update();
}

// Touches que l'esquisseur traite lui-même : elles passent avant les raccourcis de l'atelier (Échap) et de la
// fenêtre (H, F, B, T, 0, 1, 3…), qui visent d'autres vues ; les autres (F1, Ctrl+S…) gardent leur rôle.
bool Esquisseur::toucheDeLEsquisse(const QKeyEvent* e) const {
  const Qt::KeyboardModifiers m = e->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
  const int k = e->key();
  // Pendant un tracé, la saisie dynamique prend les chiffres et, ouverte, toutes les touches imprimables
  if (saisieDynamiquePossible() && m == Qt::NoModifier) {
    const QString t = e->text();
    if (m_dyn.active && (!t.isEmpty() || k == Qt::Key_Tab || k == Qt::Key_Backtab || k == Qt::Key_Return || k == Qt::Key_Enter)) return true;
    if (!t.isEmpty() && (t[0].isDigit() || t[0] == QLatin1Char('.') || t[0] == QLatin1Char(',') || t[0] == QLatin1Char('-') || t[0] == QLatin1Char('('))) return true;
    if (k == Qt::Key_Tab) return true;
  }
  if (m == Qt::ControlModifier) return k == Qt::Key_Z || k == Qt::Key_Y || k == Qt::Key_A || k == Qt::Key_Return || k == Qt::Key_Enter;
  if (m != Qt::NoModifier) return false;
  switch (k) {
    case Qt::Key_Escape: case Qt::Key_Delete: case Qt::Key_Backspace: case Qt::Key_Space:
    case Qt::Key_L: case Qt::Key_K: case Qt::Key_R: case Qt::Key_C: case Qt::Key_A: case Qt::Key_O: case Qt::Key_P:
    case Qt::Key_D: case Qt::Key_J: case Qt::Key_H: case Qt::Key_V: case Qt::Key_F: case Qt::Key_X:
    case Qt::Key_0: case Qt::Key_1: case Qt::Key_3: case Qt::Key_7: case Qt::Key_B: case Qt::Key_T:
      return true;
    default: return false;
  }
}

bool Esquisseur::event(QEvent* e) {
  if (e->type() == QEvent::ShortcutOverride && toucheDeLEsquisse(static_cast<QKeyEvent*>(e))) {
    e->accept();
    return true;
  }
  return QWidget::event(e);
}

bool Esquisseur::eventFilter(QObject* o, QEvent* e) {
  // Ligne de commande : Échap la referme, comme la perte du focus (EI-25) ; éditeur de cote : Échap abandonne
  if (o == m_commande) {
    if ((e->type() == QEvent::ShortcutOverride || e->type() == QEvent::KeyPress) && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
      if (e->type() == QEvent::ShortcutOverride) { e->accept(); return true; }
      m_commande->clear();
      m_commande->hide();
      setFocus();
      return true;
    }
    if (e->type() == QEvent::FocusOut) {
      const QCompleter* c = m_commande->completer();
      const bool popup = c && c->popup() && c->popup()->isVisible();
      if (!popup) QTimer::singleShot(0, this, [this] { if (!m_commande->hasFocus()) { m_commande->clear(); m_commande->hide(); } });
    }
  }
  if (o == m_editeur && (e->type() == QEvent::ShortcutOverride || e->type() == QEvent::KeyPress) && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
    if (e->type() == QEvent::ShortcutOverride) { e->accept(); return true; }
    terminerSaisie(false);
    setFocus();
    emit etatChange(QStringLiteral("Saisie abandonnée : la cote garde sa valeur."), 1);
    return true;
  }
  return QWidget::eventFilter(o, e);
}

void Esquisseur::keyPressEvent(QKeyEvent* e) {
  const bool maj = e->modifiers() & Qt::ShiftModifier;
  const bool ctrl = e->modifiers() & Qt::ControlModifier;
  const bool alt = e->modifiers() & (Qt::AltModifier | Qt::MetaModifier);
  if (ctrl && e->key() == Qt::Key_Z) { if (maj) retablirEtape(); else annulerEtape(); return; }
  if (ctrl && e->key() == Qt::Key_Y) { retablirEtape(); return; }
  if (ctrl && e->key() == Qt::Key_A) { toutSelectionner(); return; }
  if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && ctrl) { emit demandeTerminer(); return; }
  // Avec Ctrl ou Alt, les lettres ne choisissent ni outil ni relation : Ctrl+C, Ctrl+V… restent libres (EI-24)
  if (ctrl || alt) { QWidget::keyPressEvent(e); return; }
  // Saisie dynamique : un chiffre (ou Tab) pendant le tracé ouvre la saisie ; tant qu'elle est ouverte, les touches
  // vont aux champs (les lettres des outils ne s'appliquent pas : « 2 in », « L/2 » s'écrivent)
  if (saisieDynamiquePossible()) {
    const QString t = e->text();
    const bool debut = !t.isEmpty() && (t[0].isDigit() || t[0] == QLatin1Char('.') || t[0] == QLatin1Char(',') || t[0] == QLatin1Char('-') || t[0] == QLatin1Char('('));
    const int n = champsDynamiques();
    if (!m_dyn.active && (debut || e->key() == Qt::Key_Tab)) {
      m_dyn.active = true;
      m_dyn.champ = e->key() == Qt::Key_Tab && n > 1 ? 1 : 0;
      if (debut) m_dyn.texte[m_dyn.champ] = t;
      update();
      return;
    }
    if (m_dyn.active) {
      switch (e->key()) {
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
          if (figerChampDynamique(m_dyn.champ)) m_dyn.champ = (m_dyn.champ + 1) % n;
          update();
          return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
          if (figerChampDynamique(m_dyn.champ)) validerSaisieDynamique();
          update();
          return;
        case Qt::Key_Escape:
          m_dyn = SaisieDynamique{};
          update();
          return;
        case Qt::Key_Backspace:
          if (!m_dyn.texte[m_dyn.champ].isEmpty()) m_dyn.texte[m_dyn.champ].chop(1);
          if (m_dyn.texte[m_dyn.champ].isEmpty()) m_dyn.fige[m_dyn.champ] = false;
          update();
          return;
        default:
          if (!t.isEmpty() && t[0].isPrint()) {
            m_dyn.texte[m_dyn.champ] += t;
            m_dyn.fige[m_dyn.champ] = false;
            update();
            return;
          }
      }
    }
  }
  if (e->key() == Qt::Key_Space) { ouvrirCommande(); return; }
  if (e->key() == Qt::Key_X) { setModeConstruction(!m_modeConstruction); return; }
  switch (e->key()) {
    case Qt::Key_Escape:
      // Un pas en arrière à chaque appui : saisie, tracé en cours, outil, puis sélection
      if (m_commande->isVisible()) { m_commande->clear(); m_commande->hide(); }
      else if (m_coteEditee) terminerSaisie(false);
      else if (!m_clics.empty() || !m_coteRefs.empty() || !m_libres.empty()) finirOutil();
      else if (m_outil != Outil::Selection) setOutil(Outil::Selection);
      else { m_selection.clear(); majBarreContexte(); }
      update();
      return;
    case Qt::Key_Delete: case Qt::Key_Backspace: supprimerSelection(); return;
    case Qt::Key_L: setOutil(Outil::Ligne); return;
    case Qt::Key_K: setOutil(Outil::LigneConstruction); return;
    case Qt::Key_R: setOutil(Outil::Rectangle); return;
    case Qt::Key_C: if (maj) basculerConstruction(); else setOutil(Outil::Cercle); return;
    case Qt::Key_A: setOutil(maj ? Outil::Arc3Points : Outil::Arc); return;
    case Qt::Key_O: setOutil(Outil::Rainure); return;
    case Qt::Key_P: setOutil(maj ? Outil::Polygone : Outil::Point); return;
    case Qt::Key_D: setOutil(Outil::Cote); return;
    case Qt::Key_J: setOutil(Outil::Conge); return;
    case Qt::Key_H: contraindre(TypeContrainte::Horizontal); return;
    case Qt::Key_V: contraindre(TypeContrainte::Vertical); return;
    case Qt::Key_F: recadrer(); return;
    case Qt::Key_0: case Qt::Key_1: case Qt::Key_3: case Qt::Key_7: case Qt::Key_B: case Qt::Key_T:
      return;  // touches des vues 3D et de la carte : sans effet dans l'esquisse (elles ne visent pas une vue cachée)
    default: QWidget::keyPressEvent(e);
  }
}

bool Esquisseur::focusNextPrevChild(bool suivant) {
  // Tab sert à passer d'un champ de la saisie dynamique à l'autre pendant un tracé
  if (saisieDynamiquePossible()) return false;
  return QWidget::focusNextPrevChild(suivant);
}

bool Esquisseur::saisieDynamiquePossible() const {
  if (m_clics.empty()) return false;
  return m_outil == Outil::Ligne || m_outil == Outil::LigneConstruction || m_outil == Outil::Rectangle || m_outil == Outil::Cercle;
}

int Esquisseur::champsDynamiques() const { return m_outil == Outil::Cercle ? 1 : 2; }

bool Esquisseur::figerChampDynamique(int k) {
  if (m_dyn.texte[k].trimmed().isEmpty()) {
    m_dyn.fige[k] = false;
    return true;
  }
  const bool angle = (m_outil == Outil::Ligne || m_outil == Outil::LigneConstruction) && k == 1;
  double v = 0;
  std::string err;
  if (!lireValeur(m_dyn.texte[k].trimmed().toStdString(), angle, m_unites, v, &err, &m_variables)) {
    emit etatChange(QStringLiteral("Valeur illisible : %1").arg(QString::fromStdString(err)), 2);
    return false;
  }
  if (!angle && v <= 0) {
    emit etatChange(QStringLiteral("Une longueur doit être positive."), 2);
    return false;
  }
  m_dyn.valeur[k] = v;
  m_dyn.fige[k] = true;
  return true;
}

QPointF Esquisseur::finDynamique() const {
  const QPointF m = monde(m_souris);
  if (m_clics.empty()) return m;
  if (m_outil == Outil::Ligne || m_outil == Outil::LigneConstruction) {
    const Point a = pt(m_clics.back());
    double l = std::hypot(m.x() - a.x, m.y() - a.y), t = std::atan2(m.y() - a.y, m.x() - a.x);
    if (m_dyn.fige[0]) l = m_dyn.valeur[0];
    if (m_dyn.fige[1]) t = m_dyn.valeur[1] * PI / 180;
    return {a.x + l * std::cos(t), a.y + l * std::sin(t)};
  }
  const Point a = pt(m_clics.front());
  if (m_outil == Outil::Rectangle) {
    double w = m.x() - a.x, h = m.y() - a.y;
    if (m_dyn.fige[0]) w = (w < 0 ? -1 : 1) * m_dyn.valeur[0];
    if (m_dyn.fige[1]) h = (h < 0 ? -1 : 1) * m_dyn.valeur[1];
    return {a.x + w, a.y + h};
  }
  double r = std::hypot(m.x() - a.x, m.y() - a.y), t = std::atan2(m.y() - a.y, m.x() - a.x);
  if (r < 1e-12) t = 0;
  if (m_dyn.fige[0]) r = m_dyn.valeur[0] / 2;
  return {a.x + r * std::cos(t), a.y + r * std::sin(t)};
}

// Pose la forme avec les valeurs tapées, cotées : longueur et angle (horizontale ou verticale quand l'angle l'est),
// largeur et hauteur, diamètre. Une ligne enchaîne sur la suivante, comme au clic.
void Esquisseur::validerSaisieDynamique() {
  if (!saisieDynamiquePossible()) return;
  const QPointF fin = finDynamique();
  SaisieDynamique d = m_dyn;
  m_dyn = SaisieDynamique{};
  auto tropPetit = [this](double mm) { return mm * m_s < 2; };
  if (m_outil == Outil::Ligne || m_outil == Outil::LigneConstruction) {
    const int a = m_clics.back();
    const Point A = pt(a);
    if (std::hypot(fin.x() - A.x, fin.y() - A.y) < 1e-9) return;
    const int p = m_e.ajouterPoint(fin.x(), fin.y());
    const int l = m_e.ajouterLigne(a, p, m_outil == Outil::LigneConstruction || m_modeConstruction);
    if (d.fige[0]) m_e.contraindre(TypeContrainte::Longueur, {l}, d.valeur[0]);
    if (d.fige[1]) {
      const double ang = std::fmod(std::fmod(d.valeur[1], 360) + 360, 360);
      if (std::abs(ang) < 1e-9 || std::abs(ang - 180) < 1e-9 || std::abs(ang - 360) < 1e-9) m_e.contraindre(TypeContrainte::Horizontal, {l});
      else if (std::abs(ang - 90) < 1e-9 || std::abs(ang - 270) < 1e-9) m_e.contraindre(TypeContrainte::Vertical, {l});
      else m_e.contraindre(TypeContrainte::Angle, {AXE_H, l}, d.valeur[1]);
    }
    m_clics.push_back(p);
    apresModification();
    return;
  }
  if (m_outil == Outil::Rectangle) {
    const int pa = m_clics.front();
    const Point A = pt(pa);
    if (tropPetit(std::abs(fin.x() - A.x)) || tropPetit(std::abs(fin.y() - A.y))) return;
    const auto l = m_e.ajouterRectangle(A.x, A.y, fin.x(), fin.y());
    const int coin = m_e.element(l[0])->p1;
    bool utilise = false;
    for (const auto& el : m_e.elements) if (el.p1 == pa || el.p2 == pa || el.centre == pa) utilise = true;
    if (pa == 0 || utilise) m_e.contraindre(TypeContrainte::Coincidence, {coin, pa});
    else {
      for (auto& k : m_e.contraintes) for (int& r : k.refs) if (r == pa) r = coin;
      m_e.points.erase(std::remove_if(m_e.points.begin(), m_e.points.end(), [pa](const PointEsquisse& x) { return x.id == pa; }), m_e.points.end());
    }
    // Côtés horizontaux et verticaux cotés : la largeur sur un côté horizontal, la hauteur sur un vertical
    for (const int id : l) {
      const ElementEsquisse* el = m_e.element(id);
      if (!el) continue;
      const Point P1 = pt(el->p1), P2 = pt(el->p2);
      const bool horizontal = std::abs(P1.y - P2.y) < 1e-9;
      if (horizontal && d.fige[0]) { m_e.contraindre(TypeContrainte::Longueur, {id}, d.valeur[0]); d.fige[0] = false; }
      if (!horizontal && d.fige[1]) { m_e.contraindre(TypeContrainte::Longueur, {id}, d.valeur[1]); d.fige[1] = false; }
    }
    m_clics.clear();
    apresModification();
    return;
  }
  // Cercle
  const Point c = pt(m_clics.front());
  const double r = std::hypot(fin.x() - c.x, fin.y() - c.y);
  if (tropPetit(r)) return;
  const int cercle = m_e.ajouterCercle(m_clics.front(), r);
  if (d.fige[0]) m_e.contraindre(TypeContrainte::Diametre, {cercle}, d.valeur[0]);
  m_clics.clear();
  apresModification();
}

void Esquisseur::toutSelectionner() {
  m_selection.clear();
  for (const auto& p : m_e.points) if (p.id != 0) m_selection.push_back({Cible::Point, p.id});
  for (const auto& el : m_e.elements) if (calqueVisible(el.calque)) m_selection.push_back({Cible::Element, el.id});
  majBarreContexte();
  update();
}

void Esquisseur::supprimerSelection() {
  if (m_selection.empty()) return;
  // Toute suppression interrompt l'outil en cours : ses clics et sa cote en attente visent peut-être ce qui
  // disparaît (EI-01, EI-02)
  finirOutil();
  for (const auto& c : m_selection) if (c.type != Cible::Axe && !(c.type == Cible::Point && c.id == 0)) m_e.supprimer(c.id);
  m_selection.clear();
  apresModification();
}

void Esquisseur::basculerConstruction() {
  bool change = false;
  for (const auto& c : m_selection)
    if (c.type == Cible::Element)
      if (auto* el = m_e.element(c.id)) { el->construction = !el->construction; change = true; }
  if (change) apresModification();
}

void Esquisseur::basculerReference() {
  bool change = false;
  for (const auto& c : m_selection)
    if (c.type == Cible::Contrainte)
      for (auto& k : m_e.contraintes)
        if (k.id == c.id && estCote(k.type)) {
          k.reference = !k.reference;
          if (!k.reference) k.valeur = mesurer(m_e, k);
          change = true;
        }
  if (change) apresModification();
}

bool Esquisseur::contraindre(TypeContrainte t) {
  std::vector<int> pts, lignes, courbes;
  for (const auto& c : m_selection) {
    if (c.type == Cible::Point) pts.push_back(c.id);
    if (c.type == Cible::Axe) lignes.push_back(c.id);
    if (c.type == Cible::Element)
      if (const ElementEsquisse* el = m_e.element(c.id)) (el->type == TypeElement::Ligne ? lignes : courbes).push_back(c.id);
  }
  bool fait = false;
  switch (t) {
    case TypeContrainte::Horizontal:
    case TypeContrainte::Vertical:
      for (int l : lignes) if (l > 0) { m_e.contraindre(t, {l}); fait = true; }
      if (!fait && pts.size() == 2) { m_e.contraindre(t, {pts[0], pts[1]}); fait = true; }
      break;
    case TypeContrainte::Coincidence:
      if (pts.size() == 2) { m_e.contraindre(t, {pts[0], pts[1]}); fait = true; }
      else if (pts.size() == 1 && lignes.size() + courbes.size() == 1) { m_e.contraindre(TypeContrainte::PointSur, {pts[0], lignes.empty() ? courbes[0] : lignes[0]}); fait = true; }
      break;
    case TypeContrainte::Parallele:
    case TypeContrainte::Perpendiculaire:
      // Deux axes de l'esquisse sont déjà perpendiculaires : une relation entre eux serait un conflit permanent
      if (lignes.size() == 2 && (lignes[0] > 0 || lignes[1] > 0)) { m_e.contraindre(t, {lignes[0], lignes[1]}); fait = true; }
      break;
    case TypeContrainte::Concentrique:
      if (courbes.size() >= 2) { for (size_t i = 1; i < courbes.size(); ++i) m_e.contraindre(t, {courbes[0], courbes[i]}); fait = true; }
      break;
    case TypeContrainte::Colineaire:
      if (lignes.size() >= 2) { for (size_t i = 1; i < lignes.size(); ++i) if (lignes[i] > 0 && lignes[0] > 0) { m_e.contraindre(t, {lignes[0], lignes[i]}); fait = true; } }
      break;
    case TypeContrainte::Egal:
      // Les axes, infinis, n'ont pas de longueur
      if (lignes.size() >= 2 && std::all_of(lignes.begin(), lignes.end(), [](int l) { return l > 0; })) {
        for (size_t i = 1; i < lignes.size(); ++i) m_e.contraindre(t, {lignes[0], lignes[i]});
        fait = true;
      }
      else if (courbes.size() >= 2) { for (size_t i = 1; i < courbes.size(); ++i) m_e.contraindre(t, {courbes[0], courbes[i]}); fait = true; }
      break;
    case TypeContrainte::Tangente:
      if (lignes.size() == 1 && courbes.size() == 1) { m_e.contraindre(t, {lignes[0], courbes[0]}); fait = true; }
      else if (courbes.size() == 2) {
        const ElementEsquisse* a = m_e.element(courbes[0]);
        const ElementEsquisse* b = m_e.element(courbes[1]);
        const Point ca = pt(a->centre), cb = pt(b->centre);
        const bool interieure = std::hypot(cb.x - ca.x, cb.y - ca.y) < std::max(m_e.rayonDe(*a), m_e.rayonDe(*b));
        m_e.contraindre(t, {courbes[0], courbes[1]}, interieure ? 1 : 0);
        fait = true;
      }
      break;
    case TypeContrainte::Fixe:
      for (int p : pts) if (p != 0) { const Point q = pt(p); m_e.contraindre(t, {p}, q.x, q.y); fait = true; }
      break;
    case TypeContrainte::Milieu:
      if (pts.size() == 1 && lignes.size() == 1 && lignes[0] > 0) { m_e.contraindre(t, {pts[0], lignes[0]}); fait = true; }
      break;
    case TypeContrainte::Symetrie:
      if (pts.size() == 2 && lignes.size() == 1) { m_e.contraindre(t, {pts[0], pts[1], lignes[0]}); fait = true; }
      break;
    default: break;
  }
  if (fait) { m_selection.clear(); apresModification(); return true; }
  emit etatChange(QStringLiteral("Sélection inadaptée pour « %1 » : sélectionne d'abord les éléments (Maj pour en ajouter).").arg(QString::fromUtf8(nomContrainte(t))), 2);
  return false;
}

// ---------- Barre contextuelle : les actions utiles pour ce qui est sélectionné, près du curseur ----------
void Esquisseur::majBarreContexte() {
  emit selectionAChange();
  for (QWidget* w : m_barreContexte->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) w->deleteLater();
  delete m_barreContexte->layout();
  if (m_commandeActive || m_outil != Outil::Selection || m_selection.empty()) { m_barreContexte->hide(); return; }
  int pts = 0, lignes = 0, courbes = 0, cotesSel = 0, axes = 0;
  for (const auto& c : m_selection) {
    if (c.type == Cible::Point) ++pts;
    if (c.type == Cible::Axe) { ++lignes; ++axes; }
    if (c.type == Cible::Contrainte) ++cotesSel;
    if (c.type == Cible::Element)
      if (const ElementEsquisse* el = m_e.element(c.id)) ++(el->type == TypeElement::Ligne ? lignes : courbes);
  }
  struct A { const char* icone; QString bulle; std::function<void()> f; };
  std::vector<A> actions;
  auto rel = [this](TypeContrainte t) { return [this, t] { contraindre(t); }; };
  if (cotesSel == 1 && pts + lignes + courbes == 0) {
    actions.push_back({"cote", QStringLiteral("Modifier la valeur"), [this] { ouvrirEditeur(m_selection.front().id); }});
    actions.push_back({"reference", QStringLiteral("Pilotante ↔ référence"), [this] { basculerReference(); }});
  } else if (lignes == 1 && axes == 0 && pts + courbes == 0) {
    actions.push_back({"horizontal", QStringLiteral("Horizontale (H)"), rel(TypeContrainte::Horizontal)});
    actions.push_back({"vertical", QStringLiteral("Verticale (V)"), rel(TypeContrainte::Vertical)});
    actions.push_back({"cote", QStringLiteral("Coter la longueur"), [this] { coterSelection(); }});
    actions.push_back({"construction", QStringLiteral("Construction"), [this] { basculerConstruction(); }});
  } else if (lignes == 2 && axes < 2 && pts + courbes == 0) {
    // Une ligne et un axe : parallèles ou perpendiculaires seulement (un axe n'a ni longueur ni extrémités)
    actions.push_back({"parallele", QStringLiteral("Parallèles"), rel(TypeContrainte::Parallele)});
    actions.push_back({"perpendiculaire", QStringLiteral("Perpendiculaires"), rel(TypeContrainte::Perpendiculaire)});
    if (axes == 0) {
      actions.push_back({"egal", QStringLiteral("Égales"), rel(TypeContrainte::Egal)});
      actions.push_back({"colineaire", QStringLiteral("Colinéaires"), rel(TypeContrainte::Colineaire)});
    }
    actions.push_back({"cote", QStringLiteral("Coter l'angle ou l'écart"), [this] { coterSelection(); }});
  } else if (courbes >= 1 && pts + lignes == 0) {
    actions.push_back({"cote", courbes == 1 ? QStringLiteral("Coter le diamètre ou le rayon") : QStringLiteral("Coter l'entraxe"), [this] { coterSelection(); }});
    if (courbes == 2) {
      actions.push_back({"egal", QStringLiteral("Égaux"), rel(TypeContrainte::Egal)});
      actions.push_back({"concentrique", QStringLiteral("Concentriques"), rel(TypeContrainte::Concentrique)});
      actions.push_back({"arc-tangent", QStringLiteral("Tangents"), rel(TypeContrainte::Tangente)});
    }
    actions.push_back({"construction", QStringLiteral("Construction"), [this] { basculerConstruction(); }});
  } else if (courbes == 1 && lignes == 1 && pts == 0) {
    actions.push_back({"arc-tangent", QStringLiteral("Tangents"), rel(TypeContrainte::Tangente)});
  } else if (pts == 2 && lignes + courbes == 0) {
    actions.push_back({"coincident", QStringLiteral("Coïncidents"), rel(TypeContrainte::Coincidence)});
    actions.push_back({"horizontal", QStringLiteral("Alignés horizontalement"), rel(TypeContrainte::Horizontal)});
    actions.push_back({"vertical", QStringLiteral("Alignés verticalement"), rel(TypeContrainte::Vertical)});
    actions.push_back({"cote", QStringLiteral("Coter la distance"), [this] { coterSelection(); }});
  } else if (pts == 1 && lignes + courbes == 1) {
    actions.push_back({"coincident", QStringLiteral("Sur la courbe"), rel(TypeContrainte::Coincidence)});
    if (lignes == 1 && axes == 0) actions.push_back({"ligne-milieu", QStringLiteral("Au milieu"), rel(TypeContrainte::Milieu)});
    actions.push_back({"cote", QStringLiteral("Coter la distance"), [this] { coterSelection(); }});
  } else if (pts == 2 && lignes == 1) {
    actions.push_back({"symetrique", QStringLiteral("Symétriques"), rel(TypeContrainte::Symetrie)});
  } else if (pts == 1 && lignes + courbes == 0) {
    actions.push_back({"fixe", QStringLiteral("Fixer"), rel(TypeContrainte::Fixe)});
  }
  if (lignes + courbes >= 1 && cotesSel == 0) {
      actions.push_back({"symetrie", QStringLiteral("Symétrie (panneau)"), [this] { emit demandeCommande(QStringLiteral("symetrie")); }});
    actions.push_back({"decaler", QStringLiteral("Décaler (panneau)"), [this] { emit demandeCommande(QStringLiteral("decaler")); }});
    actions.push_back({"repetition-lineaire", QStringLiteral("Réseau linéaire (panneau)"), [this] { emit demandeCommande(QStringLiteral("repetition-lineaire")); }});
    actions.push_back({"repetition-circulaire", QStringLiteral("Réseau circulaire (panneau)"), [this] { emit demandeCommande(QStringLiteral("repetition-circulaire")); }});
  }
  actions.push_back({"supprimer", QStringLiteral("Supprimer (Suppr)"), [this] { supprimerSelection(); }});
  auto* l = new QHBoxLayout(m_barreContexte);
  l->setContentsMargins(4, 3, 4, 3);
  l->setSpacing(2);
  for (const auto& a : actions) {
    auto* b = new QToolButton(m_barreContexte);
    b->setIcon(icone(QString::fromLatin1(a.icone)));
    b->setIconSize(QSize(22, 22));
    b->setToolTip(a.bulle);
    const auto f = a.f;
    connect(b, &QToolButton::clicked, this, [f] { f(); });
    l->addWidget(b);
  }
  m_barreContexte->adjustSize();
  QPoint p = (m_souris + QPointF(18, 18)).toPoint();
  p.setX(std::clamp(p.x(), 4, std::max(4, width() - m_barreContexte->width() - 4)));
  p.setY(std::clamp(p.y(), 4, std::max(4, height() - m_barreContexte->height() - 4)));
  m_barreContexte->move(p);
  m_barreContexte->show();
  m_barreContexte->raise();
}

// Cote à partir de la sélection (barre contextuelle) : placée près des éléments, valeur à taper.
void Esquisseur::coterSelection() {
  std::vector<Cible> refs;
  for (const auto& c : m_selection) if (c.type != Cible::Contrainte && refs.size() < 2) refs.push_back(c);
  const Proposition pr = proposerCote(refs, m_souris + QPointF(0, -30));
  if (!pr.valide) { emit etatChange(QStringLiteral("Pas de cote possible pour cette sélection."), 2); return; }
  const bool resolvaitAvant = m_etat.resolue && m_etat.enConflit.empty();
  ContrainteEsquisse k;
  k.id = m_e.prochainId++;
  k.type = pr.type;
  k.refs = pr.refs;
  k.valeur2 = pr.valeur2;
  k.valeur = mesurer(m_e, k);
  m_e.contraintes.push_back(k);
  const bool redondante = coteRedondante(m_e, k.id, resolvaitAvant);
  if (redondante) m_e.contraintes.back().reference = true;
  m_selection.clear();
  apresModification();
  if (!redondante) ouvrirEditeur(k.id);
}

// ---------- Ligne de commande (barre d'espace) ----------
void Esquisseur::ouvrirCommande() {
  m_commande->setGeometry(QRect(width() / 2 - 280, height() - 66, 560, 38));
  m_commande->show();
  m_commande->raise();
  m_commande->setFocus();
}

bool Esquisseur::executerCommande(const QString& texteBrut, QString* message) {
  QString texte = texteBrut.trimmed();
  texte.replace(QLatin1Char(','), QLatin1Char('.'));
  QStringList mots = texte.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  auto dire = [message](const QString& m) { if (message) *message = m; };
  if (mots.isEmpty()) { dire(QString()); return true; }  // ligne vide : rien à dire (EI-25)
  QString cmd = motCommande(mots.takeFirst());
  // « =45 » collé
  if (cmd.size() > 1 && cmd.startsWith(QLatin1Char('='))) { mots.prepend(cmd.mid(1)); cmd = QStringLiteral("="); }
  // Valeurs : une unité ou un opérateur se rattache à ce qui le précède (« 2.5 cm », « 40 + 10 », EI-18)
  const QStringList exprs = expressions(mots);
  auto lire = [&](int i, bool angle, double& v, QString& erreur) {
    std::string err;
    if (lireValeur(exprs[i].toStdString(), angle, m_unites, v, &err, &m_variables)) return true;
    erreur = QStringLiteral("Valeur illisible « %1 » (%2).").arg(exprs[i], QString::fromStdString(err));
    return false;
  };
  auto longueurs = [&](int n, std::vector<double>& v, QString& erreur) {
    if (exprs.size() < n) { erreur = QStringLiteral("Il faut %1 valeur(s).").arg(n); return false; }
    v.assign(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
      if (!lire(i, false, v[static_cast<std::size_t>(i)], erreur)) return false;
      if (v[static_cast<std::size_t>(i)] <= 0) { erreur = QStringLiteral("Les tailles doivent être positives."); return false; }
    }
    return true;
  };
  // Base des formes : point sélectionné, sinon point existant du dernier clic (lié à la forme), sinon position du
  // dernier clic dans cette esquisse, sinon l'origine (EI-19)
  int pointBase = pointSelectionne();
  if (pointBase < 0 && m_aDernierClic && m_dernierClicPoint >= 0 && (m_dernierClicPoint == 0 || m_e.point(m_dernierClicPoint))) pointBase = m_dernierClicPoint;
  if (pointBase < 0 && !m_aDernierClic) pointBase = 0;
  const Point base = pointBase >= 0 ? pt(pointBase) : Point{m_dernierClic.x(), m_dernierClic.y()};
  if (cmd == "ok" || cmd == "fin" || cmd == "terminer") { emit demandeTerminer(); dire(QStringLiteral("Esquisse terminée.")); return true; }
  if (cmd == "annuler") { emit demandeAnnuler(); dire(QStringLiteral("Esquisse annulée.")); return true; }
  if (cmd == "x" || cmd == "construction") { setModeConstruction(!m_modeConstruction); dire(m_modeConstruction ? QStringLiteral("Mode construction.") : QStringLiteral("Mode normal.")); return true; }
  if (cmd == "?" || cmd == "aide") {
    dire(QStringLiteral("Outils : l, lc, lm, r, rc, r3, pgm, c, c3, a, a3, at, o, pol, p, d, j. Formes cotées : c 50, r 40 30, rc 40 30, pol 6 20 (unités et opérations : c 2,5 cm ; r 40+10 30). "
                        "Relations : h, v, coin, par, perp, egal, tan, fixe, milieu, sym, conc, col. Cote choisie : = 25. Aussi x, ok, annuler."));
    return true;
  }
  if (cmd == "=" || (cmd == "cote" && !exprs.isEmpty())) {
    if (exprs.isEmpty()) { dire(QStringLiteral("Tape la valeur après « = »."));
      return false; }
    for (const auto& c : m_selection)
      if (c.type == Cible::Contrainte) {
        // La valeur est lue selon la cote (angle ou longueur), avec les contrôles de l'éditeur
        QString erreur;
        if (!changerCote(c.id, exprs.join(QLatin1Char(' ')), &erreur)) { dire(erreur); return false; }
        dire(QStringLiteral("Cote modifiée."));
        return true;
      }
    dire(QStringLiteral("Sélectionne d'abord une cote."));
    return false;
  }
  QString erreur;
  if ((cmd == "c" || cmd == "cercle") && !exprs.isEmpty()) {
    std::vector<double> n;
    if (!longueurs(1, n, erreur)) { dire(erreur); return false; }
    const int centre = pointBase >= 0 ? pointBase : m_e.ajouterPoint(base.x, base.y);
    const int cer = m_e.ajouterCercle(centre, n[0] / 2);
    m_e.contraindre(TypeContrainte::Diametre, {cer}, n[0]);
    apresModification();
    dire(QStringLiteral("Cercle de Ø %1 créé.").arg(QString::fromStdString(ecrireValeur(n[0], false, m_unites))));
    return true;
  }
  if ((cmd == "r" || cmd == "rect" || cmd == "rectangle" || cmd == "rc") && !exprs.isEmpty()) {
    std::vector<double> n;
    if (!longueurs(2, n, erreur)) { dire(erreur); return false; }
    std::vector<int> l;
    int centre = 0;
    if (cmd == "rc") l = m_e.ajouterRectangleCentre(base.x, base.y, base.x + n[0] / 2, base.y + n[1] / 2, &centre);
    else l = m_e.ajouterRectangle(base.x, base.y, base.x + n[0], base.y + n[1]);
    if (pointBase >= 0) m_e.contraindre(TypeContrainte::Coincidence, {cmd == "rc" ? centre : m_e.element(l[0])->p1, pointBase});
    m_e.contraindre(TypeContrainte::Longueur, {l[0]}, n[0]);
    m_e.contraindre(TypeContrainte::Longueur, {l[1]}, n[1]);
    apresModification();
    dire(QStringLiteral("Rectangle créé."));
    return true;
  }
  if ((cmd == "pol" || cmd == "polygone") && exprs.size() >= 2) {
    double nombre = 0, rayon = 0;
    if (!lire(0, false, nombre, erreur) || !lire(1, false, rayon, erreur)) { dire(erreur); return false; }
    // Le nombre de côtés n'a pas d'unité : relu sans conversion
    bool entier = false;
    const int cotesPol = exprs[0].toInt(&entier);
    if (!entier || cotesPol < 3 || cotesPol > 64) { dire(QStringLiteral("Nombre de côtés entre 3 et 64.")); return false; }
    if (rayon <= 0) { dire(QStringLiteral("Le rayon du polygone doit être positif.")); return false; }
    const std::set<int> avant = [this] { std::set<int> r; for (const auto& q : m_e.points) r.insert(q.id); return r; }();
    const auto cotes = m_e.ajouterPolygone(base.x, base.y, base.x + rayon, base.y, cotesPol);
    if (cotes.empty()) { dire(QStringLiteral("Polygone impossible.")); return false; }
    // Polygone coté : rayon du cercle de construction imposé, centre lié à la base
    for (const auto& el : m_e.elements)
      if (el.type == TypeElement::Cercle && el.construction && !avant.count(el.centre)) {
        m_e.contraindre(TypeContrainte::Rayon, {el.id}, rayon);
        if (pointBase >= 0) m_e.contraindre(TypeContrainte::Coincidence, {el.centre, pointBase});
        break;
      }
    apresModification();
    dire(QStringLiteral("Polygone à %1 côtés créé.").arg(cotesPol));
    return true;
  }
  if (const auto it = tableOutils().find(cmd); it != tableOutils().end()) {
    if (it->second == Outil::Polygone && !exprs.isEmpty()) {
      bool entier = false;
      const int n = exprs[0].toInt(&entier);
      if (!entier || n < 3 || n > 64) { dire(QStringLiteral("Nombre de côtés entre 3 et 64.")); return false; }
      m_cotesPolygone = n;
    }
    setOutil(it->second);
    dire(QStringLiteral("Outil choisi."));
    return true;
  }
  if (const auto it = tableRelations().find(cmd); it != tableRelations().end()) {
    // Le message d'échec de contraindre reste affiché : rien n'est annoncé comme appliqué à tort (EI-17)
    if (!contraindre(it->second)) { dire(QString()); return false; }
    dire(QStringLiteral("Relation appliquée à la sélection."));
    return true;
  }
  dire(QStringLiteral("Commande inconnue : « %1 ». Tape ? pour la liste.").arg(cmd));
  return false;
}

// ---------- Menu radial : huit directions autour du curseur ----------
int Esquisseur::secteurSous(const QPointF& p) const { return NavigationVue::secteurVers(p - m_centreRadial); }

// Le même menu radial que les vues de l'atelier électronique (NavigationVue)
void Esquisseur::dessinerRadial(QPainter& g) {
  static const std::array<EntreeRadiale, 8> entrees = {{{QStringLiteral("cercle"), QStringLiteral("Cercle")},
                                                        {QStringLiteral("construction"), QStringLiteral("Construction")},
                                                        {QStringLiteral("cote"), QStringLiteral("Cote")},
                                                        {QStringLiteral("arc-centre"), QStringLiteral("Arc")},
                                                        {QStringLiteral("rectangle"), QStringLiteral("Rectangle")},
                                                        {QStringLiteral("selection"), QStringLiteral("Sélection")},
                                                        {QStringLiteral("ligne"), QStringLiteral("Ligne")},
                                                        {QStringLiteral("supprimer"), QStringLiteral("Supprimer")}}};
  dessinerMenuRadial(g, m_centreRadial, m_secteur, entrees, font());
}

// ---------- Inspecteur de viabilité (coin inférieur gauche) ----------
void Esquisseur::dessinerInspecteur(QPainter& g) {
  std::map<int, int> liens;
  std::map<int, int> parent;
  std::function<int(int)> racine = [&](int a) { auto it = parent.find(a); if (it == parent.end() || it->second == a) return a; return parent[a] = racine(it->second); };
  for (const auto& c : m_e.contraintes)
    if (c.type == TypeContrainte::Coincidence && c.refs.size() == 2) parent[racine(c.refs[0])] = racine(c.refs[1]);
  int nulles = 0;
  for (const auto& el : m_e.elements) {
    if (el.construction || el.type == TypeElement::Cercle) continue;
    ++liens[racine(el.p1)];
    ++liens[racine(el.p2)];
    if (el.type == TypeElement::Ligne) {
      const Point a = pt(el.p1), b = pt(el.p2);
      if (std::hypot(b.x - a.x, b.y - a.y) < 1e-6) ++nulles;
    }
  }
  int libres = 0;
  for (const auto& [p, n] : liens) if (n == 1) ++libres;
  std::vector<std::pair<QString, QColor>> lignes;
  if (m_modeConstruction) lignes.push_back({QStringLiteral("Mode construction (X)"), REFERENCE});
  if (!m_etat.enConflit.empty()) lignes.push_back({QStringLiteral("⚠ %1 contrainte(s) en conflit").arg(m_etat.enConflit.size()), CONFLIT});
  if (libres > 0) lignes.push_back({QStringLiteral("⚠ %1 extrémité(s) libre(s) : profil ouvert").arg(libres), lisible(QColor(0xC7, 0x7A, 0x1E))});
  if (nulles > 0) lignes.push_back({QStringLiteral("⚠ %1 ligne(s) de longueur nulle").arg(nulles), CONFLIT});
  if (libres == 0 && !m_e.elements.empty() && m_etat.enConflit.empty()) lignes.push_back({QStringLiteral("✔ Profils fermés"), CONTRAINT});
  lignes.push_back({m_etat.degresLiberte == 0 ? QStringLiteral("✔ Entièrement contrainte") : QStringLiteral("%1 degré(s) de liberté").arg(m_etat.degresLiberte),
                    m_etat.degresLiberte == 0 ? CONTRAINT : LIBRE});
  QFont f = font();
  f.setPixelSize(12);
  f.setBold(true);
  g.setFont(f);
  const QFontMetricsF fm(f);
  double l = 0;
  for (const auto& x : lignes) l = std::max(l, fm.horizontalAdvance(x.first));
  const double h = 20.0 * lignes.size() + 8;
  const QRectF cadre(width() - l - 22 - 12, height() - h - 12, l + 22, h);
  g.setPen(QPen(BULLE_BORD, 1));
  g.setBrush(BULLE);
  g.drawRoundedRect(cadre, 8, 8);
  for (size_t i = 0; i < lignes.size(); ++i) {
    g.setPen(lignes[i].second);
    g.drawText(QRectF(cadre.x() + 11, cadre.y() + 4 + 20.0 * i, l + 4, 20), Qt::AlignVCenter | Qt::AlignLeft, lignes[i].first);
  }
}

// ---------- Symétrie, répétitions, calques ----------
std::vector<int> Esquisseur::elementsSelectionnes() const {
  std::vector<int> r;
  for (const auto& c : m_selection) if (c.type == Cible::Element) r.push_back(c.id);
  return r;
}

void Esquisseur::symetriserSelection() {
  // L'axe : un axe de l'esquisse sélectionné, sinon la dernière ligne sélectionnée.
  int axe = 0;
  for (const auto& c : m_selection) if (c.type == Cible::Axe) axe = c.id;
  std::vector<int> elts = elementsSelectionnes();
  if (!axe)
    for (auto it = m_selection.rbegin(); it != m_selection.rend() && !axe; ++it)
      if (it->type == Cible::Element)
        if (const ElementEsquisse* el = m_e.element(it->id); el && el->type == TypeElement::Ligne) axe = el->id;
  elts.erase(std::remove(elts.begin(), elts.end(), axe), elts.end());
  if (!axe || elts.empty()) {
    emit etatChange(QStringLiteral("Symétrie : sélectionne les éléments à copier, puis une ligne ou un axe comme miroir."), 2);
    return;
  }
  const auto crees = m_e.symetriser(elts, axe);
  m_selection.clear();
  for (int id : crees) m_selection.push_back({Cible::Element, id});
  apresModification();
  emit etatChange(QStringLiteral("Symétrie : %1 élément(s) copiés, liés à leurs originaux.").arg(crees.size()), 0);
}

void Esquisseur::repeterSelection(bool circulaire) {
  std::vector<int> elts = elementsSelectionnes();
  int point = -1;
  for (const auto& c : m_selection) if (c.type == Cible::Point) point = c.id;
  if (elts.empty()) { emit etatChange(QStringLiteral("Répétition : sélectionne d'abord les éléments à répéter."), 2); return; }
  QDialog d(this);
  d.setWindowTitle(circulaire ? QStringLiteral("Répétition circulaire") : QStringLiteral("Répétition linéaire"));
  auto* f = new QFormLayout(&d);
  auto* nombre = new QSpinBox;
  nombre->setRange(2, 500);
  nombre->setValue(circulaire ? 6 : 3);
  f->addRow(QStringLiteral("Nombre d’exemplaires (original compris)"), nombre);
  auto* valeur = new ChampValeur(circulaire, m_unites);
  valeur->setValeur(circulaire ? 360 : 20);
  auto* direction = new QComboBox;
  if (circulaire) {
    f->addRow(QStringLiteral("Angle total"), valeur);
    direction->addItem(point > 0 ? QStringLiteral("Autour du point sélectionné") : QStringLiteral("Autour de l’origine"));
  } else {
    f->addRow(QStringLiteral("Pas"), valeur);
    direction->addItems({QStringLiteral("Selon X (horizontal)"), QStringLiteral("Selon Y (vertical)")});
  }
  f->addRow(QStringLiteral("Direction"), direction);
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(boutons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  connect(boutons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  f->addRow(boutons);
  if (d.exec() != QDialog::Accepted) return;
  std::vector<int> crees;
  if (circulaire) {
    const Point c = point > 0 ? pt(point) : Point{0, 0};
    crees = m_e.repeterCirculaire(elts, c.x, c.y, nombre->value(), valeur->valeur());
  } else {
    const double p = valeur->valeur();
    crees = m_e.repeterLineaire(elts, direction->currentIndex() == 0 ? p : 0, direction->currentIndex() == 1 ? p : 0, nombre->value());
  }
  m_selection.clear();
  apresModification();
  emit etatChange(QStringLiteral("Répétition : %1 élément(s) créés.").arg(crees.size()), 0);
}

void Esquisseur::appliquer(const Esquisse& e) {
  m_e = e;
  m_selection.clear();
  apresModification();
}

bool Esquisseur::calqueVisible(const std::string& nom) const {
  if (nom.empty()) return true;
  for (const auto& k : m_e.calques) if (k.nom == nom) return k.visible;
  return true;
}

QColor Esquisseur::couleurCalque(const std::string& nom) const {
  for (const auto& k : m_e.calques) if (k.nom == nom) return lisible(QColor(QString::fromStdString(k.couleur)));
  return LIBRE;
}

void Esquisseur::setCalqueActif(const std::string& nom) { m_calqueActif = nom; }

void Esquisseur::ajouterCalque(const std::string& nom, const std::string& couleur) {
  for (const auto& k : m_e.calques) if (k.nom == nom) return;
  m_e.calques.push_back({nom, couleur, true});
  apresModification();
}

void Esquisseur::supprimerCalque(const std::string& nom) {
  for (auto& el : m_e.elements) if (el.calque == nom) el.calque.clear();
  m_e.calques.erase(std::remove_if(m_e.calques.begin(), m_e.calques.end(), [&](const Calque& k) { return k.nom == nom; }), m_e.calques.end());
  if (m_calqueActif == nom) m_calqueActif.clear();
  apresModification();
}

void Esquisseur::setCalqueVisible(const std::string& nom, bool visible) {
  for (auto& k : m_e.calques) if (k.nom == nom) k.visible = visible;
  m_selection.clear();
  apresModification();
}

void Esquisseur::mettreSelectionSurCalque(const std::string& nom) {
  for (int id : elementsSelectionnes())
    for (auto& el : m_e.elements) if (el.id == id) el.calque = nom;
  m_selection.clear();
  apresModification();
}

// Décaler, déplacer, pivoter, mettre à l'échelle la sélection, par une petite fenêtre de valeurs.
void Esquisseur::transformerSelection(int quoi) {
  const std::vector<int> elts = elementsSelectionnes();
  if (elts.empty()) { emit etatChange(QStringLiteral("Sélectionne d'abord les éléments à modifier."), 2); return; }
  int point = -1;
  for (const auto& c : m_selection) if (c.type == Cible::Point) point = c.id;
  const Point centre = point > 0 ? pt(point) : Point{0, 0};
  static const char* titres[] = {"Décaler", "Déplacer", "Pivoter", "Mettre à l'échelle"};
  QDialog d(this);
  d.setWindowTitle(QString::fromUtf8(titres[quoi]));
  auto* f = new QFormLayout(&d);
  ChampValeur* v1 = nullptr;
  ChampValeur* v2 = nullptr;
  QCheckBox* copie = nullptr;
  QCheckBox* inverse = nullptr;
  if (quoi == 0) {
    v1 = new ChampValeur(false, m_unites);
    v1->setValeur(2);
    f->addRow(QStringLiteral("Distance"), v1);
    inverse = new QCheckBox(QStringLiteral("Vers l'intérieur (ou à gauche pour une chaîne ouverte)"));
    f->addRow(QString(), inverse);
  } else if (quoi == 1) {
    v1 = new ChampValeur(false, m_unites);
    v2 = new ChampValeur(false, m_unites);
    v1->setValeur(10);
    v2->setValeur(0);
    f->addRow(QStringLiteral("Décalage horizontal"), v1);
    f->addRow(QStringLiteral("Décalage vertical"), v2);
  } else {
    v1 = new ChampValeur(quoi == 2, m_unites);
    if (quoi == 3) v1->setBornes(1e-6, 1e6);
    v1->setValeur(quoi == 2 ? 90 : 2);
    f->addRow(quoi == 2 ? QStringLiteral("Angle") : QStringLiteral("Facteur"), v1);
    f->addRow(QStringLiteral("Centre"), new QLabel(point > 0 ? QStringLiteral("le point sélectionné") : QStringLiteral("l'origine")));
  }
  if (quoi != 0) {
    copie = new QCheckBox(QStringLiteral("Garder l'original (copier)"));
    f->addRow(QString(), copie);
  }
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(boutons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  connect(boutons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  f->addRow(boutons);
  if (d.exec() != QDialog::Accepted) return;
  std::vector<int> crees;
  if (quoi == 0) {
    crees = m_e.decaler(elts, inverse && inverse->isChecked() ? -v1->valeur() : v1->valeur());
  } else if (quoi == 1) {
    const double dx = v1->valeur(), dy = v2->valeur();
    crees = m_e.transformer(elts, [dx, dy](Point p) { return Point{p.x + dx, p.y + dy}; }, 1, copie->isChecked());
  } else if (quoi == 2) {
    const double a = v1->valeur() * PI / 180, c = std::cos(a), s = std::sin(a);
    crees = m_e.transformer(elts, [=](Point p) { return Point{centre.x + (p.x - centre.x) * c - (p.y - centre.y) * s, centre.y + (p.x - centre.x) * s + (p.y - centre.y) * c}; }, 1, copie->isChecked());
  } else {
    const double k = v1->valeur();
    crees = m_e.transformer(elts, [=](Point p) { return Point{centre.x + (p.x - centre.x) * k, centre.y + (p.y - centre.y) * k}; }, k, copie->isChecked());
  }
  m_selection.clear();
  apresModification();
  emit etatChange(QStringLiteral("%1 : fait.").arg(QString::fromUtf8(titres[quoi])), 0);
}

// Cote modifiée depuis le tableau des cotes : même lecture que l'éditeur (variables, expression conservée).
void Esquisseur::changerCoteTexte(int id, const QString& texte) {
  QString erreur;
  if (!changerCote(id, texte, &erreur)) emit etatChange(erreur, 2);
}

// Coin le plus proche du curseur (point où partent au moins deux lignes), à moins de 30 pixels.
int Esquisseur::coinProche(const QPointF& ecranPos) const {
  int meilleur = -1;
  double dmin = 30;
  for (const auto& p : m_e.points) {
    int n = 0;
    for (const auto& el : m_e.elements) if (el.type == TypeElement::Ligne && !el.construction && (el.p1 == p.id || el.p2 == p.id)) ++n;
    if (n < 2) continue;
    const QPointF q = ecran(p.x, p.y);
    const double d = std::hypot(q.x() - ecranPos.x(), q.y() - ecranPos.y());
    if (d < dmin) { dmin = d; meilleur = p.id; }
  }
  return meilleur;
}

void Esquisseur::setApercu(const Esquisse* e) {
  if (e) m_apercu = *e;
  else m_apercu.reset();
  update();
}

void Esquisseur::selectionnerElements(const std::vector<int>& ids) {
  m_selection.clear();
  for (int id : ids) if (m_e.element(id)) m_selection.push_back({Cible::Element, id});
  update();
}

int Esquisseur::ligneOuAxeSelectionne() const {
  for (auto it = m_selection.rbegin(); it != m_selection.rend(); ++it) {
    if (it->type == Cible::Axe) return it->id;
    if (it->type == Cible::Element)
      if (const ElementEsquisse* el = m_e.element(it->id); el && el->type == TypeElement::Ligne) return el->id;
  }
  return 0;
}

int Esquisseur::pointSelectionne() const {
  for (auto it = m_selection.rbegin(); it != m_selection.rend(); ++it) if (it->type == Cible::Point) return it->id;
  return -1;
}

void Esquisseur::supprimerContrainte(int id) {
  m_e.contraintes.erase(std::remove_if(m_e.contraintes.begin(), m_e.contraintes.end(), [id](const ContrainteEsquisse& c) { return c.id == id; }), m_e.contraintes.end());
  apresModification();
}

std::vector<int> Esquisseur::contraintesDe(const std::vector<int>& refs) const {
  std::vector<int> r;
  std::set<int> voulus(refs.begin(), refs.end());
  // Les points des éléments comptent aussi
  for (int id : refs)
    if (const ElementEsquisse* el = m_e.element(id)) for (int p : {el->p1, el->p2, el->centre}) if (p > 0) voulus.insert(p);
  for (const auto& c : m_e.contraintes)
    if (std::any_of(c.refs.begin(), c.refs.end(), [&](int x) { return voulus.count(x) > 0; })) r.push_back(c.id);
  return r;
}

QString Esquisseur::nomContrainteLisible(int id) const {
  for (const auto& c : m_e.contraintes)
    if (c.id == id) {
      QString t = QString::fromUtf8(nomContrainte(c.type));
      if (estCote(c.type)) t += QStringLiteral(" ") + texteContrainte(c);
      return t;
    }
  return {};
}

void Esquisseur::setSurbrillance(const std::vector<int>& elements, const std::vector<int>& points) {
  m_surbrillanceElements = elements;
  m_surbrillancePoints = points;
  update();
}

int Esquisseur::importerExterne(int i) {
  if (i < 0 || i >= static_cast<int>(m_ext.elements.size())) return 0;
  const int nouveau = importerElementExterne(m_ext.elements[static_cast<size_t>(i)], m_ext, i < static_cast<int>(m_extNoms.size()) ? m_extNoms[static_cast<size_t>(i)] : std::string());
  m_externesChoisies.clear();
  apresModification();  // l'arête reprise disparaît des propositions ; une annulation la rend de nouveau disponible
  const bool lie = i < static_cast<int>(m_extNoms.size()) && !m_extNoms[static_cast<size_t>(i)].empty();
  emit etatChange(lie ? QStringLiteral("Arête de la pièce reprise dans l'esquisse (construction) : elle suivra la pièce si celle-ci change.")
                      : QStringLiteral("Contour de la pièce repris dans l'esquisse (construction, fixe) : tu peux t'y relier ou coter jusqu'à lui."), 0);
  return nouveau;
}

// Reprise d'un élément de la pièce (S3) : points fixes, liés à l'arête quand elle a un nom ; un point de l'esquisse
// tracé au même endroit lui devient coïncident (il suivra l'arête) ; un point déjà repris est réutilisé
int Esquisseur::importerElementExterne(const ElementEsquisse& src, const Esquisse& source, const std::string& nom) {
  std::map<int, int> corr;
  auto fixe = [this](int id) {
    return std::any_of(m_e.contraintes.begin(), m_e.contraintes.end(), [id](const ContrainteEsquisse& c) { return c.type == TypeContrainte::Fixe && !c.refs.empty() && c.refs[0] == id; });
  };
  auto point = [&](int id, bool centre) {
    if (id <= 0) return id;
    if (corr.count(id)) return corr[id];
    const PointEsquisse* q = source.point(id);
    if (!q) return corr[id] = 0;
    for (const auto& p : m_e.points)
      if (p.id != 0 && std::hypot(p.x - q->x, p.y - q->y) < 1e-6 && fixe(p.id)) return corr[id] = p.id;
    const int n = m_e.ajouterPoint(q->x, q->y);
    m_e.contraindre(TypeContrainte::Fixe, {n}, q->x, q->y);
    if (!nom.empty()) m_e.liensExternes.push_back({n, nom, centre, 0});
    for (const auto& p : m_e.points)
      if (p.id != n && p.id != 0 && std::hypot(p.x - q->x, p.y - q->y) < 1e-6 && !fixe(p.id)) { m_e.contraindre(TypeContrainte::Coincidence, {p.id, n}); break; }
    return corr[id] = n;
  };
  int nouveau = 0;
  if (src.type == TypeElement::Ligne) nouveau = m_e.ajouterLigne(point(src.p1, false), point(src.p2, false), true);
  else if (src.type == TypeElement::Cercle) {
    const int c = point(src.centre, true);
    nouveau = m_e.ajouterCercle(c, src.rayon);
    m_e.contraindre(TypeContrainte::Rayon, {nouveau}, src.rayon);
    for (auto& l : m_e.liensExternes) if (l.point == c && l.arete == nom) l.cercle = nouveau;
  } else nouveau = m_e.ajouterArc(point(src.centre, true), point(src.p1, false), point(src.p2, false));
  for (auto& el : m_e.elements) if (el.id == nouveau) el.construction = true;
  return nouveau;
}

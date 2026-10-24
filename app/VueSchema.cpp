// Prométhée : éditeur de schéma électronique (canevas). Licence GPL-3.0-only.
#include "promethee/fichiers.hpp"
#include "promethee/pdf_documents.hpp"
#include "promethee/valeurs.hpp"
#include "promethee/formules.hpp"
#include "promethee/schema_avance.hpp"
#include "RegistreReglages.hpp"
#include "VueSchema.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <numbers>
#include <set>
#include <tuple>
#include <utility>

#include <QAbstractSpinBox>
#include <QApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QEnterEvent>
#include <QMimeData>
#include <QFile>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPageSize>
#include <QPainterPath>
#include <QPdfWriter>
#include <QToolButton>
#include <QMenu>
#include <QNativeGestureEvent>
#include <QSettings>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include "Document.hpp"
#include "AideElectronique.hpp"
#include "BandeauOutil.hpp"
#include "CadreVue.hpp"
#include "AccessibiliteCanevas.hpp"
#include "DescriptionObjet.hpp"
#include "PeripheriquesEntree.hpp"
#include "GestesSelection.hpp"
#include "GlisserArticle.hpp"
#include "Icones.hpp"
#include "NavigationVue.hpp"
#include "DessinSchema.hpp"
#include "PeintreDxf.hpp"
#include "RegistreActions.hpp"
#include "SignesCanevas.hpp"
#include "SelectionElectronique.hpp"
#include "Theme.hpp"
#include "VueGrafcet.hpp"
#include "promethee/synchronisation.hpp"
#include "promethee/commandes.hpp"
#include "promethee/electrique_grafcet.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;
using namespace dessin_schema;

namespace {
// Le nom suivant d'une série d'étiquettes (D3 → D4)
std::string suivantSerie(const std::string& t) {
  std::size_t i = t.size();
  while (i > 0 && std::isdigit(static_cast<unsigned char>(t[i - 1]))) --i;
  if (i == t.size()) return t;
  return t.substr(0, i) + std::to_string(std::stoi(t.substr(i, 9)) + 1);
}
// Une touche saisie avant le champ qui l'ignorerait (Échap d'une saisie sur place) ; vit avec le champ
class FiltreTouche : public QObject {
 public:
  FiltreTouche(QObject* parent, int touche, std::function<void()> action) : QObject(parent), m_touche(touche), m_action(std::move(action)) {}
  bool eventFilter(QObject*, QEvent* e) override {
    if (e->type() != QEvent::KeyPress || static_cast<QKeyEvent*>(e)->key() != m_touche) return false;
    m_action();
    return true;
  }

 private:
  int m_touche;
  std::function<void()> m_action;
};
// Presse-papiers du schéma, partagé par toutes les vues (on colle d'une carte à l'autre)
PressePapiersSchema& pressePapiers() {
  static PressePapiersSchema pp;
  return pp;
}
}  // namespace

VueSchema::VueSchema(Document* doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
  accessibilite::installer();  // le lecteur d'écran lit la description de l'objet tenu (2.30)
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(300, 200);
  setAcceptDrops(true);
  setSelectionPartagee(nullptr);
  m_bandeau = new BandeauOutil(this);
  // Navigation commune (1.19) : molette, pavé, boutons du milieu et de droite, menu à marques
  m_nav = new NavigationVue(this, NavigationVue::secteursOrigine("objet.miroirX"), [this](const std::string& a) { executerGeste(a); }, [this](QPointF d) {
    m_origine += d;
    if (QGuiApplication::mouseButtons() & Qt::LeftButton) {
      m_outils.actif()->glisser(GesteVue{m_souris, Qt::NoButton, Qt::LeftButton, QGuiApplication::keyboardModifiers()});
      majBandeau();
    }
    update();
  });
  // Appui long immobile de l'outil Sélection sur des objets superposés : leur liste, comme Alt+clic (SAI-18)
  m_appuiLong = new AppuiLong(this, [this](QPointF p) {
    if (outil() == Outil::Selection && objetsSous(p).size() >= 2) listerObjetsSous(p);
  });
  connect(m_bandeau, &BandeauOutil::actionFaite, this, [this] {
    majBandeau();
    update();
  });
  m_outils.armer(creerOutil(Outil::Selection, {}));
  // Souris 3D (2.29) : la vue montrée et active suit la translation et la poussée
  connect(&SourisEspace::instance(), &SourisEspace::mouvement, this, [this](double tx, double ty, double tz, double, double, double) {
    // La vue montrée, dans la fenêtre active (sans fenêtre active, comme sous un serveur graphique sans gestionnaire
    // de fenêtres, la vue montrée suffit)
    if (!isVisible() || (QApplication::activeWindow() && !window()->isActiveWindow())) return;
    SourisEspace::appliquer2D(this, m_origine, m_echelle, tx, ty, tz, 0.4, 80.0);
    emit vueChangee();
    update();
  });
  connect(m_doc, &Document::change, this, [this] {
    ++m_generation;
    ++m_modifications;
    // Sélection propre à la vue : ce qui a disparu en sort ; la sélection partagée, elle, est tenue par l'atelier
    if (m_sel == m_selPropre)
      if (const ConceptionElec* c = conception()) m_sel->filtrer(*c);
    if (majSelectionVue()) emit selectionChangee();
    update();
  });
  connect(m_doc, &Document::conceptionChangee, this, [this](const ChangementConception& ch) {
    ++m_modifications;
    if (domaine() != Domaine::Electronique) return;
    const ConceptionElec* c = conception();
    if (c && (ch.conception.empty() || ch.conception == c->uuid)) {
      if (majSelectionVue()) emit selectionChangee();
      update();
    }
  });
  connect(m_doc, &Document::gesteInterrompu, this, [this] {
    m_outils.actif()->gesteInterrompu();
    majBandeau();
    update();
  });
}

const ConceptionElec* VueSchema::conception() const {
  const auto& p = m_doc->projet();
  return m_conception >= 0 && m_conception < static_cast<int>(p.electronique.size()) ? &p.electronique[static_cast<std::size_t>(m_conception)] : nullptr;
}
ConceptionElec* VueSchema::cible(prom::Projet& p) const {
  return m_conception >= 0 && m_conception < static_cast<int>(p.electronique.size()) ? &p.electronique[static_cast<std::size_t>(m_conception)] : nullptr;
}
const DefSymbole* VueSchema::definition(const std::string& id) const {
  if (const ConceptionElec* c = conception())
    if (const DefSymbole* d = c->symbole(id)) return d;
  if (const DefSymbole* d = prom::elec::symboleElectrique(id)) return d;
  return symboleBibliotheque(id);
}
const Feuille* VueSchema::feuilleCourante() const {
  const ConceptionElec* c = conception();
  return c && m_feuille >= 0 && m_feuille < static_cast<int>(c->feuilles.size()) ? &c->feuilles[static_cast<std::size_t>(m_feuille)] : nullptr;
}

void VueSchema::setConception(int conception, int feuille) {
  const bool neuve = conception != m_conception || feuille != m_feuille;
  m_conception = conception;
  m_feuille = feuille;
  if (neuve) {
    if (m_sel == m_selPropre) m_sel->vider();
    if (majSelectionVue()) emit selectionChangee();
    m_outils.actif()->echap();  // un tracé ou un glissement commencé ailleurs s'arrête
    ++m_generation;  // caches des classes de domaine (folios dessinés d'office) à refaire
    recadrer();
  }
  update();
}

bool VueSchema::refuseVerrou() {
  const ConceptionElec* c = conception();
  if (!c || !c->verrouillee) return false;
  emit message(QStringLiteral("🔒 <b>%1 est publiée</b> (%2) : lecture seule. Pour la modifier, passe à une nouvelle révision dans l’onglet Fabrication.")
                   .arg(QString::fromStdString(c->nom).toHtmlEscaped(), QString::fromStdString(c->publications.empty() ? std::string("verrouillée") : c->publications.back().id)));
  return true;
}

bool VueSchema::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* h = static_cast<QHelpEvent*>(e);
    const QString t = texteSurvol(h->pos());
    if (t.isEmpty()) QToolTip::hideText();
    else QToolTip::showText(h->globalPos(), t, this);
    return true;
  }
  // Pincer sur le pavé tactile : zoomer
  if (e->type() == QEvent::NativeGesture && m_nav->pincer(static_cast<QNativeGestureEvent*>(e), m_origine, m_echelle, 0.4, 80.0)) {
    update();
    return true;
  }
  // Le canevas ne garde que les touches d'une action qu'il ferait maintenant (registre, 1.8) : F8, F1 et les
  // raccourcis de la fenêtre lui passent au-dessus (S22)
  if (e->type() == QEvent::ShortcutOverride) {
    auto* k = static_cast<QKeyEvent*>(e);
    const RegistreActions& r = RegistreActions::atelierElectronique();
    // Une suite en attente garde la touche suivante ; une première touche de suite est gardée aussi (ERG-03)
    if (m_nav->prefixe() || !r.suites(PorteeAction::Schema, k->keyCombination()).empty()) {
      e->accept();
      return true;
    }
    for (const ActionAtelier* a : r.pour(PorteeAction::Schema, *k))
      if (!a->menu.empty() || actionPossible(a->identifiant)) {
        e->accept();
        return true;
      }
  }
  return QWidget::event(e);
}

// Tab, quand l'outil armé a des réglages avant la pose (2.22) : la frappe va au registre (« outil.reglages »)
// plutôt qu'au contrôle suivant
bool VueSchema::focusNextPrevChild(bool suivant) {
  // Tab sur la vue elle-même : les réglages de l'outil ; dans un champ posé sur la vue, le champ suivant
  if (suivant && hasFocus() && outilPrend("outil.reglages")) return false;
  return QWidget::focusNextPrevChild(suivant);
}

bool VueSchema::outilPrend(std::string_view action) { return m_outils.actif() && m_outils.actif()->agir(action, false); }
bool VueSchema::actionPossible(std::string_view action) { return outilPrend(action) || agirVue(action, false); }

bool VueSchema::executerAction(std::string_view action) {
  m_outils.recycler();
  if (m_outils.actif() && m_outils.actif()->agir(action, true)) {
    majBandeau();
    return true;
  }
  return agirVue(action, true);
}

QString VueSchema::nomOutil() const {
  const ContenuBandeau b = m_outils.actif() ? m_outils.actif()->bandeau() : ContenuBandeau{};
  return b.vide() || outil() == Outil::Selection ? QStringLiteral("Sélection") : b.titre;
}

// Actions de la vue : armer un outil, la vue, le presse-papiers, l'inspecteur, l'état neutre
bool VueSchema::agirVue(std::string_view a, bool faire) {
  struct Armement {
    std::string_view action;
    Outil outil;
  };
  static constexpr Armement armements[] = {{"schema.fil", Outil::Fil},          {"schema.etiquette", Outil::Etiquette}, {"schema.etiquetteGlobale", Outil::EtiquetteGlobale},
                                           {"schema.nonConnexion", Outil::NonConnexion}, {"schema.jonction", Outil::Jonction}, {"schema.texte", Outil::Texte},
                                           {"schema.gomme", Outil::Gomme}};
  for (const auto& [action, o] : armements)
    if (a == action) {
      if (faire) setOutil(o);
      return true;
    }
  using Faire = void (VueSchema::*)();
  struct Simple {
    std::string_view action;
    Faire f;
  };
  static constexpr Simple simples[] = {{"commun.echap", &VueSchema::echap},           {"edition.copier", &VueSchema::copier},   {"edition.couper", &VueSchema::couper},
                                       {"edition.coller", &VueSchema::coller},        {"edition.dupliquer", &VueSchema::dupliquer}, {"selection.tout", &VueSchema::toutChoisir},
                                       {"vue.toutVoir", &VueSchema::recadrer},        {"vue.ajusterDessin", &VueSchema::ajusterAuDessin}};
  for (const auto& [action, f] : simples)
    if (a == action) {
      if (faire) (this->*f)();
      return true;
    }
  if (a == "objet.modifierTexte") {
    if (m_selection.size() != 1) return false;
    if (faire) modifierTexteChoisi();
  } else if (a == "schema.repeter") {
    // Possible dès qu'un composant a été posé sur cette feuille (Inser, 2.3)
    const Feuille* f = feuilleCourante();
    if (domaine() != Domaine::Electronique || !f ||
        (std::none_of(f->symboles.begin(), f->symboles.end(), [&](const SymbolePlace& x) { return x.uuid == m_dernierePose; }) &&
         std::none_of(f->etiquettes.begin(), f->etiquettes.end(), [&](const Etiquette& x) { return x.uuid == m_dernierePose; })))
      return false;
    if (faire) repeterPose();
  } else if (a == "schema.poserCopie") {
    const Feuille* f = feuilleCourante();
    if (domaine() != Domaine::Electronique || !f || m_selection.size() != 1 ||
        std::none_of(f->symboles.begin(), f->symboles.end(), [&](const SymbolePlace& x) { return m_selection.count(x.uuid) && !x.composant.empty(); }))
      return false;
    if (faire) poserCopie();
  } else if (a == "selection.relieSuivant") {
    if (domaine() != Domaine::Electronique || m_selection.empty()) return false;
    if (faire) objetRelieSuivant();
  } else if (a == "selection.inverser") {
    // Sélections avancées (2.26)
    if (domaine() != Domaine::Electronique) return false;
    if (faire) inverserSelection();
  } else if (a == "selection.etendre") {
    if (domaine() != Domaine::Electronique || m_selection.empty()) return false;
    if (faire) etendreSelection();
  } else if (a == "selection.reduire") {
    if (!m_sel || m_sel->paliers() == 0) return false;
    if (faire) reduireSelection();
  } else if (a == "selection.semblables") {
    if (domaine() != Domaine::Electronique || !m_sel || m_sel->composants().empty()) return false;
    if (faire) dialogueSemblables();
  } else if (a == "schema.alimentation") {
    if (faire) armerAlimentation();
  } else if (a == "schema.choisirComposant") {
    if (faire) emit bibliothequeDemandee();
  } else if (a == "commun.inspecteur") {
    if (faire) emit inspecteurDemande();
  } else if (a == "vue.zoomPlus" || a == "vue.zoomMoins") {
    // Le cran de la molette, réglable (1.19)
    const double f = Navigation::instance().reglages().zoomCran;
    if (faire) zoomer(a == "vue.zoomPlus" ? f : 1 / f);
  } else if (a == "vue.zoomSelection") {
    if (faire) zoomerSurSelection();
  } else if (a == "vue.vueEnsemble") {
    if (!m_cadre) return false;
    if (faire) m_cadre->basculerEnsemble();
  } else if (a == "schema.ancienF") {
    // F ne recadre plus, pour que le réflexe ne retourne pas une empreinte au circuit (C13) ; dit une fois
    if (faire && !m_astuceF) {
      m_astuceF = true;
      emit message(QStringLiteral("<b>Tout voir</b> : touche <b>Origine</b>. F ne recadre plus le schéma ; au circuit, il change l’empreinte de face."));
    }
  } else
    return false;
  return true;
}

// Article venu d'une autre instance par le glisser-déposer du système (type MIME de GlisserArticle, 2.2)
void VueSchema::dragEnterEvent(QDragEnterEvent* e) {
  const auto d = GlisserArticle::article(e->mimeData());
  if (!d || domaine() != Domaine::Electronique || refuseVerrou()) return;
  glisserArticle(*d, e->position());
  e->acceptProposedAction();
}
void VueSchema::dragMoveEvent(QDragMoveEvent* e) {
  if (const auto d = GlisserArticle::article(e->mimeData())) {
    glisserArticle(*d, e->position());
    e->acceptProposedAction();
  }
}
void VueSchema::dragLeaveEvent(QDragLeaveEvent*) { abandonnerArticle(); }
void VueSchema::dropEvent(QDropEvent* e) {
  if (const auto d = GlisserArticle::article(e->mimeData())) {
    glisserArticle(*d, e->position());
    if (deposerArticle(e->position())) e->acceptProposedAction();
  }
}

void VueSchema::enterEvent(QEnterEvent* e) {
  QWidget::enterEvent(e);
  // Pas pendant une saisie : un champ qui a le focus le garde (S21)
  const QWidget* f = QApplication::focusWidget();
  const bool saisie = f && (qobject_cast<const QLineEdit*>(f) || qobject_cast<const QAbstractSpinBox*>(f) || qobject_cast<const QTextEdit*>(f) || qobject_cast<const QPlainTextEdit*>(f));
  if (!saisie && isActiveWindow() && focusPolicy() != Qt::NoFocus) setFocus(Qt::MouseFocusReason);
}
void VueSchema::focusInEvent(QFocusEvent* e) {
  QWidget::focusInEvent(e);
  update();
}
void VueSchema::focusOutEvent(QFocusEvent* e) {
  QWidget::focusOutEvent(e);
  update();
}

// Électronique : un pas qui ne photographie que cette conception (A2)
void VueSchema::modifier(const std::function<void(Feuille&, ConceptionElec&)>& f, const QString& fusion, const QString& nom) {
  if (refuseVerrou()) return;
  const int fi = m_feuille;
  m_doc->modifierConception(m_conception, [&](ConceptionElec& c) {
    if (fi >= 0 && fi < static_cast<int>(c.feuilles.size())) f(c.feuilles[static_cast<std::size_t>(fi)], c);
  }, nom, fusion);
  // Une commande imbriquée a fait son pas : le fil suspendu reprend (après l'appel, l'outil n'est pas détruit sous
  // lui-même)
  if (m_outils.suspendus()) QTimer::singleShot(0, this, [this] {
      if (m_outils.suspendus()) reprendreTrace();
    });
}

void VueSchema::reprendreTrace() {
  if (!m_outils.depiler()) return;
  appliquerCurseur();
  majBandeau();
  emit outilChange();
  emit message(QStringLiteral("Le fil reprend au dernier point fixé."));
  update();
}

void VueSchema::commencerGeste() { m_doc->debutGesteConception(m_conception); }

QString VueSchema::repereFantome(const DefSymbole& d) const { return QString::fromStdString(d.prefixe + "?"); }

void VueSchema::armerAlimentation() { setOutil(Outil::Alimentation, m_styleAlim.empty() ? std::string("GND") : m_styleAlim); }

QPointF VueSchema::ecran(Pt p) const { return m_origine + QPointF(mm(p.x) * m_echelle, mm(p.y) * m_echelle); }
Pt VueSchema::monde(QPointF q) const { return ptMm((q.x() - m_origine.x()) / m_echelle, (q.y() - m_origine.y()) / m_echelle); }

std::vector<Pt> VueSchema::pointsConnexion() const {
  std::vector<Pt> r;
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f) return r;
  for (const auto& s : f->symboles)
    if (const DefSymbole* d = c->symbole(s.symbole))
      for (const auto& b : brochesPlacees(s, *d)) r.push_back(b.p);
  for (const auto& w : f->fils) { r.push_back(w.a); r.push_back(w.b); }
  for (const auto& e : f->etiquettes) r.push_back(e.p);
  return r;
}

bool VueSchema::estPointConnexion(Pt p, bool interieurFil) const {
  for (const Pt& q : pointsConnexion())
    if (q == p) return true;
  if (interieurFil)
    if (const Feuille* f = feuilleCourante())
      for (const auto& w : f->fils)
        if (surSegment(p, w.a, w.b)) return true;
  return false;
}

Pt VueSchema::accroche(QPointF q) const {
  double meilleur = 7;
  Pt r = surGrilleActive(monde(q));
  for (const Pt& p : pointsConnexion()) {
    const QPointF e = ecran(p);
    const double d = std::hypot(e.x() - q.x(), e.y() - q.y());
    if (d < meilleur) { meilleur = d; r = p; }
  }
  return r;
}

std::vector<std::string> VueSchema::objetsSous(QPointF q) const {
  std::vector<std::string> r;
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f) return r;
  const Pt w = monde(q);
  const double tol = 5.0 / m_echelle * 1e6;  // 5 pixels, en nm
  for (const auto& j : f->jonctions)
    if (distance(j.p, w) < tol) r.push_back(idJonction(j));
  for (const auto& n : f->nonConnexions)
    if (distance(n.p, w) < tol * 1.4) r.push_back(idNonConnexion(n));
  for (const auto& e : f->etiquettes) {
    const double l = (e.texte.size() * 1.1 + 1.5) * 1e6;
    // L'étiquette s'étend depuis son point d'accroche dans le sens de sa rotation
    const double dx = static_cast<double>(w.x - e.p.x), dy = static_cast<double>(w.y - e.p.y);
    double le = 0, tr = 0;
    switch (e.rotation & 3) {
      case 0: le = dx; tr = dy; break;
      case 1: le = -dy; tr = dx; break;
      case 2: le = -dx; tr = dy; break;
      default: le = dy; tr = dx; break;
    }
    if (le >= -tol && le <= l && std::abs(tr) < 1.2e6) r.push_back(e.uuid);
  }
  for (const auto& t : f->textes) {
    const double l = t.texte.size() * mm(t.taille) * 0.6e6;
    // Électricien : texte écrit à partir de son point (à gauche) ; électronique : centré sur lui. La zone suit la
    // rotation du texte (S25) : « le » le long du texte, « tr » en travers
    const double dx = static_cast<double>(w.x - t.p.x), dy = static_cast<double>(w.y - t.p.y);
    double le = 0, tr = 0;
    switch (t.rotation & 3) {
      case 0: le = dx; tr = dy; break;
      case 1: le = -dy; tr = dx; break;
      case 2: le = -dx; tr = -dy; break;
      default: le = dy; tr = -dx; break;
    }
    const double x0 = texteAncreAGauche() ? 0.0 : -l / 2;
    if (le >= x0 && le <= x0 + l && std::abs(tr) < static_cast<double>(t.taille)) r.push_back(t.uuid);
  }
  // Symboles : du plus petit qui contient le point au plus grand
  std::vector<std::pair<double, std::string>> symboles;
  for (const auto& s : f->symboles)
    if (const DefSymbole* d = c->symbole(s.symbole)) {
      const Boite b = boiteSymbole(s, *d).gonflee(400'000);
      if (b.contient(w)) symboles.push_back({static_cast<double>(b.largeur()) * static_cast<double>(b.hauteur()), s.uuid});
    }
  std::stable_sort(symboles.begin(), symboles.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
  for (const auto& [aire, id] : symboles) r.push_back(id);
  for (const auto& fil : f->fils)
    if (distancePointSegment(w, fil.a, fil.b) < tol) r.push_back(fil.uuid);
  return r;
}

std::string VueSchema::objetSous(QPointF q) const {
  const Feuille* f = feuilleCourante();
  for (const std::string& id : objetsSous(q)) {
    SelectionElectronique::Ref r;
    // Le masque (2.26, ERG-28) : ce qui n'est pas choisi ne se prend pas
    if (m_sel && m_sel->intouchable() && !m_selection.count(id)) continue;
    if (!f || !m_sel || !SelectionElectronique::refSchema(*f, id, r) || m_sel->admis(r.genre)) return id;
  }
  return {};
}

QString VueSchema::libelleObjet(const std::string& id) const {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  SelectionElectronique::Ref r;
  if (!c || !f || !SelectionElectronique::refSchema(*f, id, r)) return {};
  const QString reseau = QString::fromStdString(reseauDe(id));
  switch (r.genre) {
    case SelectionElectronique::Genre::Symbole: {
      const ComposantElec* k = c->composant(r.composant);
      return QStringLiteral("Symbole %1").arg(k ? QString::fromStdString(k->reference) : QStringLiteral("?"));
    }
    case SelectionElectronique::Genre::Fil: return reseau.isEmpty() ? QStringLiteral("Fil") : QStringLiteral("Fil %1").arg(reseau);
    case SelectionElectronique::Genre::Etiquette:
      for (const auto& e : f->etiquettes)
        if (e.uuid == id) return QStringLiteral("Étiquette %1").arg(QString::fromStdString(e.texte));
      return QStringLiteral("Étiquette");
    case SelectionElectronique::Genre::TexteSchema:
      for (const auto& t : f->textes)
        if (t.uuid == id) return QStringLiteral("Texte « %1 »").arg(QString::fromStdString(t.texte).left(24));
      return QStringLiteral("Texte");
    case SelectionElectronique::Genre::Jonction: return QStringLiteral("Jonction");
    case SelectionElectronique::Genre::NonConnexion: return QStringLiteral("Non-connexion");
    default: return {};
  }
}

void VueSchema::listerObjetsSous(QPointF q) {
  const std::vector<std::string> v = objetsSous(q);
  if (v.empty()) {
    emit message(QStringLiteral("Rien sous le curseur."));
    return;
  }
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("objetsSous"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  menu->addSection(QStringLiteral("Objets sous le curseur"));
  for (const std::string& id : v) {
    connect(menu->addAction(libelleObjet(id)), &QAction::triggered, this, [this, id] {
      const Feuille* g = feuilleCourante();
      SelectionElectronique::Ref r;
      if (!g || !SelectionElectronique::refSchema(*g, id, r)) return;
      const auto mode = m_sel->modeClic(QGuiApplication::keyboardModifiers());  // le mode du bandeau vaut pour la liste
      m_sel->appliquer({r}, mode);
      if (mode == SelectionElectronique::Mode::Remplacer) m_sel->surligner(reseauDe(id));
    });
  }
  menu->popup(mapToGlobal(q.toPoint()));
}

void VueSchema::resizeEvent(QResizeEvent* e) { QWidget::resizeEvent(e); }

double VueSchema::pasGrille() const {
  if (m_echelle < 2) return 0;
  return m_echelle >= 8 ? mm(GRILLE_SCHEMA) : 2 * mm(GRILLE_SCHEMA);
}

// Le cadre (barres, vue d'ensemble) et l'indicateur de zoom suivent la vue : refaits quand elle a bougé
void VueSchema::majCadre() {
  if (!m_cadre && domaine() == Domaine::Electronique && !m_impression) {
    CadreVue::Acces a;
    a.contenu = [this] {
      const Feuille* f = feuilleCourante();
      if (!f) return QRectF(0, 0, 297, 210);
      const Boite b = cadreFeuille(f->format);
      return QRectF(mm(b.x0), mm(b.y0), mm(b.largeur()), mm(b.hauteur()));
    };
    a.origine = [this] { return m_origine; };
    a.echelle = [this] { return m_echelle; };
    a.deplacer = [this](QPointF o) {
      m_origine = o;
      update();
    };
    a.generation = [this] { return m_generation * 1'000'003 + m_invalidations * 131 + static_cast<std::uint64_t>(m_feuille + 1); };
    a.peindre = [this](QPainter& g, QPointF origine, double echelle) {
      // Le dessin de la feuille, à l'échelle de la vignette, sans sélection ni marques
      const QPointF o = std::exchange(m_origine, origine);
      const double e = std::exchange(m_echelle, echelle);
      const bool imp = std::exchange(m_impression, true);
      peindre(g);
      m_origine = o;
      m_echelle = e;
      m_impression = imp;
      prendreEncres(false);
    };
    m_cadre = new CadreVue(this, std::move(a), QStringLiteral("schema/vueEnsemble"), avecBandeau() ? BandeauOutil::HAUTEUR : 0);
    m_cadre->ensemble()->setVisible(m_cadre->ensembleVisible());
  }
  if (m_origine == m_origineVue && m_echelle == m_echelleVue && size() == m_tailleVue && m_feuille == m_feuilleVue) {
    if (m_cadre && m_cadre->ensembleVisible()) m_cadre->ensemble()->update();
    return;
  }
  m_origineVue = m_origine;
  m_echelleVue = m_echelle;
  m_tailleVue = size();
  m_feuilleVue = m_feuille;
  if (m_cadre) m_cadre->maj();
  emit vueChangee();
}

void VueSchema::zoomer(double facteur) {
  const double nouvelle = std::clamp(m_echelle * facteur, 0.4, 80.0);
  const QPointF p(width() / 2.0, height() / 2.0);
  m_origine = p - (p - m_origine) * (nouvelle / m_echelle);
  m_echelle = nouvelle;
  update();
}

void VueSchema::recadrer() { cadrer(recadrerSurDessin()); }
void VueSchema::ajusterAuDessin() { cadrer(true); }

void VueSchema::cadrer(bool dessin) {
  const Feuille* f = feuilleCourante();
  const ConceptionElec* c = conception();
  if (width() < 10 || height() < 10) return;
  // Le dessin s'il y en a un (marge de 12 mm), sinon la feuille entière ; l'électricien voit d'abord son folio entier
  Boite b;
  if (c && f && dessin) {
    for (const auto& s : f->symboles)
      if (const DefSymbole* d = c->symbole(s.symbole)) b.etendre(boiteSymbole(s, *d));
    for (const auto& w : f->fils) { b.etendre(w.a); b.etendre(w.b); }
    for (const auto& e : f->etiquettes) b.etendre(Boite{e.p.x, e.p.y, e.p.x + static_cast<Nm>(e.texte.size()) * 1'100'000, e.p.y});
    for (const auto& t : f->textes) {
      const Nm demi = static_cast<Nm>(0.3 * static_cast<double>(t.taille) * static_cast<double>(t.texte.size()));
      const Nm avant = texteAncreAGauche() ? 0 : demi, apres = texteAncreAGauche() ? 2 * demi : demi;
      // Le texte tourné s'étend en hauteur (S25)
      switch (t.rotation & 3) {
        case 0: b.etendre(Boite{t.p.x - avant, t.p.y, t.p.x + apres, t.p.y}); break;
        case 1: b.etendre(Boite{t.p.x, t.p.y - apres, t.p.x, t.p.y + avant}); break;
        case 2: b.etendre(Boite{t.p.x - apres, t.p.y, t.p.x + avant, t.p.y}); break;
        default: b.etendre(Boite{t.p.x, t.p.y - avant, t.p.x, t.p.y + apres}); break;
      }
    }
  }
  const bool contenu = !b.vide();
  b = contenu ? b.gonflee(12'000'000) : cadreFeuille(f ? f->format : "A4");
  // Les commandes de vue gardent le bas libre : le cartouche ne passe pas dessous
  const double bas = margeBas();
  m_echelle = std::min((width() - 40) / mm(std::max<Nm>(b.largeur(), 1)), (height() - 40 - bas) / mm(std::max<Nm>(b.hauteur(), 1)));
  if (contenu) m_echelle = std::min(m_echelle, 12.0);
  m_origine = QPointF(width() / 2.0 - mm(b.centre().x) * m_echelle, (height() - bas) / 2.0 - mm(b.centre().y) * m_echelle);
  update();
}


// Commande du menu à marques (1.19) : Annuler retire d'abord un tracé en cours, sinon le dernier pas ; les autres
// sont des actions du registre, faites comme leur touche
void VueSchema::executerGeste(const std::string& action) {
  m_outils.recycler();
  if (action == "historique.annuler") {
    if (!m_outils.actif()->annulerTrace()) m_doc->annuler();
  } else if (action == "historique.retablir") {
    m_doc->retablir();
  } else {
    executerAction(action);
  }
  majBandeau();
  appliquerCurseur();
  update();
}

// Premier appui « agir au curseur » : la jonction ou la non-connexion se pose où est le curseur, sans armer d'outil
bool VueSchema::agirAuCurseur(std::string_view action) {
  const Outil o = action == "schema.jonction" ? Outil::Jonction : action == "schema.nonConnexion" ? Outil::NonConnexion : Outil::Selection;
  if (o == Outil::Selection || m_outils.actif()->geste()) return false;
  const std::unique_ptr<OutilVue> outil = creerOutil(o, {});
  outil->appui(GesteVue{m_souris, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier});
  update();
  return true;
}

QString VueSchema::annonceClavier() const { return m_nav ? m_nav->annonce() : QString(); }

bool VueSchema::menuMarquesVisible() const { return m_nav && m_nav->menuVisible(); }

// Zoom sur la sélection (menu à marques, vers le haut) : les objets choisis cadrés, sinon tout le dessin
void VueSchema::zoomerSurSelection() {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  Boite b;
  if (c && f) {
    for (const auto& s : f->symboles)
      if (m_selection.count(s.uuid))
        if (const DefSymbole* d = c->symbole(s.symbole)) b.etendre(boiteSymbole(s, *d));
    for (const auto& w : f->fils)
      if (m_selection.count(w.uuid)) {
        b.etendre(w.a);
        b.etendre(w.b);
      }
    for (const auto& e : f->etiquettes)
      if (m_selection.count(e.uuid)) b.etendre(e.p);
    for (const auto& t : f->textes)
      if (m_selection.count(t.uuid)) b.etendre(t.p);
    for (const auto& j : f->jonctions)
      if (m_selection.count(idJonction(j))) b.etendre(j.p);
  }
  if (b.vide() || width() < 10 || height() < 10) {
    recadrer();
    return;
  }
  b = b.gonflee(6'000'000);
  m_echelle = std::clamp(std::min((width() - 40) / mm(std::max<Nm>(b.largeur(), 1)), (height() - 40) / mm(std::max<Nm>(b.hauteur(), 1))), 0.4, 20.0);
  m_origine = QPointF(width() / 2.0 - mm(b.centre().x) * m_echelle, height() / 2.0 - mm(b.centre().y) * m_echelle);
  update();
}

void VueSchema::centrerSur(Pt p) {
  m_echelle = std::max(m_echelle, 6.0);
  m_origine = QPointF(width() / 2.0 - mm(p.x) * m_echelle, height() / 2.0 - mm(p.y) * m_echelle);
  update();
}

void VueSchema::setSelectionPartagee(SelectionElectronique* s) {
  if (!s) {
    if (!m_selPropre) m_selPropre = new SelectionElectronique(this);
    s = m_selPropre;
  }
  if (s == m_sel) return;
  if (m_sel) disconnect(m_sel, nullptr, this, nullptr);
  m_sel = s;
  connect(m_sel, &SelectionElectronique::changee, this, [this] {
    accessibilite::annoncer(this);
    // Estompage actif : ce qui est choisi ailleurs (une autre feuille) change aussi ce qui s'estompe ici
    if (m_sel->estompage() != SelectionElectronique::Estompage::Aucun) ++m_invalidations;
    if (majSelectionVue() && !m_choixLocal) emit selectionChangee();
    majBandeau();  // le bandeau de sélection suit ce qui est choisi
  });
  connect(m_sel, &SelectionElectronique::modeBandeauChange, this, [this] { majBandeau(); });
  for (auto signal : {&SelectionElectronique::estompageChange, &SelectionElectronique::formeChange, &SelectionElectronique::memoiresChange})
    connect(m_sel, signal, this, [this] {
      ++m_invalidations;
      majBandeau();
      update();
    });
  connect(m_sel, &SelectionElectronique::surlignageChange, this, [this] {
    m_reseau = m_sel->reseau();
    update();
  });
  m_reseau = m_sel->reseau();
  if (majSelectionVue()) emit selectionChangee();
}

void VueSchema::setReseauSurligne(const std::string& nom) { m_sel->surligner(nom); }
const std::string& VueSchema::reseauSurligne() const { return m_sel->reseau(); }

bool VueSchema::majSelectionVue() {
  std::set<std::string> s;
  const Feuille* f = feuilleCourante();
  if (f && m_sel && !m_sel->vide()) {
    std::set<std::string> choisis, parEmpreinte;
    for (const auto& r : m_sel->objets())
      if (SelectionElectronique::auSchema(r.genre)) choisis.insert(r.id);
      else if (r.genre == SelectionElectronique::Genre::Empreinte) parEmpreinte.insert(r.id);
    // Ce qui est sur la feuille montrée ; un composant choisi au circuit l'est ici par tous ses symboles (1.5), une
    // unité choisie au schéma ne choisit qu'elle
    for (const auto& x : f->symboles)
      if (choisis.count(x.uuid) || (!x.composant.empty() && parEmpreinte.count(x.composant))) s.insert(x.uuid);
    for (const auto& x : f->fils) if (choisis.count(x.uuid)) s.insert(x.uuid);
    for (const auto& x : f->etiquettes) if (choisis.count(x.uuid)) s.insert(x.uuid);
    for (const auto& x : f->textes) if (choisis.count(x.uuid)) s.insert(x.uuid);
    for (const auto& x : f->jonctions) if (choisis.count(idJonction(x))) s.insert(idJonction(x));
    for (const auto& x : f->nonConnexions) if (choisis.count(idNonConnexion(x))) s.insert(idNonConnexion(x));
  }
  update();
  if (s == m_selection) return false;
  m_selection = std::move(s);
  ++m_generationSel;
  return true;
}

void VueSchema::selectionner(const std::set<std::string>& ids) {
  std::vector<SelectionElectronique::Ref> refs;
  if (const Feuille* f = feuilleCourante())
    for (const auto& id : ids) {
      SelectionElectronique::Ref r;
      if (SelectionElectronique::refSchema(*f, id, r)) refs.push_back(std::move(r));
    }
  m_choixLocal = true;
  m_sel->choisir(refs);
  m_choixLocal = false;
  majSelectionVue();
  emit selectionChangee();
}

void VueSchema::toutChoisir() {
  const Feuille* f = feuilleCourante();
  if (!f) return;
  std::set<std::string> ids;
  for (const auto& x : f->symboles) ids.insert(x.uuid);
  for (const auto& x : f->fils) ids.insert(x.uuid);
  for (const auto& x : f->etiquettes) ids.insert(x.uuid);
  for (const auto& x : f->textes) ids.insert(x.uuid);
  for (const auto& x : f->jonctions) ids.insert(idJonction(x));
  for (const auto& x : f->nonConnexions) ids.insert(idNonConnexion(x));
  selectionner(ids);
  emit message(QStringLiteral("%1 objet(s) choisi(s) sur la feuille.").arg(m_selection.size()));
}

std::string VueSchema::reseauDe(const std::string& objet) const {
  if (!m_k) return {};
  if (const auto it = m_k->parFil.find(objet); it != m_k->parFil.end()) return m_k->reseaux[static_cast<std::size_t>(it->second)].nom;
  if (const auto it = m_k->parEtiquette.find(objet); it != m_k->parEtiquette.end()) return m_k->reseaux[static_cast<std::size_t>(it->second)].nom;
  if (const auto it = m_k->parSymbole.find(objet); it != m_k->parSymbole.end()) return m_k->reseaux[static_cast<std::size_t>(it->second)].nom;
  return {};
}

// Clic de l'outil Sélection : dans le vide, l'état neutre (rien de choisi, rien de surligné, S5) ; sur un objet,
// remplacer, ajouter (Maj), basculer (Ctrl) ou retirer (Ctrl+Maj), comme au circuit (S23). Le réseau d'un fil ou
// d'une étiquette passe à l'autre vue par le surlignage.
void VueSchema::cliquerSelection(QPointF q, Qt::KeyboardModifiers mods) {
  const auto mode = m_sel->modeClic(mods);
  const std::string o = objetSous(q);
  const Feuille* f = feuilleCourante();
  SelectionElectronique::Ref r;
  // Un clic que le filtre de sélection bloque le fait clignoter (SAI-18)
  if (o.empty() && !objetsSous(q).empty()) m_sel->signalerBlocage();
  if (o.empty() || !f || !SelectionElectronique::refSchema(*f, o, r)) {
    if (mode == SelectionElectronique::Mode::Remplacer) m_sel->neutre();
    return;
  }
  // Un clic sur un objet déjà choisi garde la sélection multiple (pour la glisser) et en fait l'objet tenu
  if (mode == SelectionElectronique::Mode::Remplacer && m_selection.count(o)) m_sel->tenir(r);
  else m_sel->appliquer({r}, mode);
  if (mode == SelectionElectronique::Mode::Remplacer) m_sel->surligner(reseauDe(o));
}

// Double-clic sur un fil : tous les fils de son réseau sur la feuille, et le réseau surligné partout
void VueSchema::choisirReseauDuFil(const std::string& fil) {
  const Feuille* f = feuilleCourante();
  if (!f || !m_k) return;
  const auto it = m_k->parFil.find(fil);
  if (it == m_k->parFil.end()) return;
  const int reseau = it->second;
  std::vector<SelectionElectronique::Ref> refs;
  for (const auto& w : f->fils)
    if (const auto jt = m_k->parFil.find(w.uuid); jt != m_k->parFil.end() && jt->second == reseau) refs.push_back({SelectionElectronique::Genre::Fil, w.uuid, {}});
  m_sel->choisir(refs);
  m_sel->surligner(m_k->reseaux[static_cast<std::size_t>(reseau)].nom);
}

std::vector<std::string> VueSchema::objetsDansCadre(Pt a, Pt b, bool touches) const {
  std::vector<std::string> r;
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f) return r;
  Boite cadre;
  cadre.etendre(a);
  cadre.etendre(b);
  const Polygone bords{{cadre.x0, cadre.y0}, {cadre.x1, cadre.y0}, {cadre.x1, cadre.y1}, {cadre.x0, cadre.y1}};
  auto segmentTouche = [&](Pt p, Pt q) {
    if (cadre.contient(p) || cadre.contient(q)) return true;
    for (std::size_t i = 0; i < 4; ++i)
      if (distanceSegments(bords[i], bords[(i + 1) % 4], p, q) < 1.0) return true;
    return false;
  };
  // De gauche à droite : les objets entièrement dedans ; de droite à gauche : ceux que le cadre touche (S23)
  for (const auto& x : f->symboles)
    if (const DefSymbole* d = c->symbole(x.symbole)) {
      const Boite bs = boiteSymbole(x, *d);
      if (touches ? bs.touche(cadre) : cadre.contient(bs)) r.push_back(x.uuid);
    }
  for (const auto& w : f->fils)
    if (touches ? segmentTouche(w.a, w.b) : cadre.contient(w.a) && cadre.contient(w.b)) r.push_back(w.uuid);
  for (const auto& x : f->etiquettes) if (cadre.contient(x.p)) r.push_back(x.uuid);
  for (const auto& x : f->textes) if (cadre.contient(x.p)) r.push_back(x.uuid);
  for (const auto& x : f->jonctions) if (cadre.contient(x.p)) r.push_back(idJonction(x));
  for (const auto& x : f->nonConnexions) if (cadre.contient(x.p)) r.push_back(idNonConnexion(x));
  return r;
}

void VueSchema::setOutil(Outil o, const std::string& parametre) {
  const OutilVue* actif = m_outils.actif();
  if (m_outils.suspendus()) {
    // Une commande imbriquée : Sélection ou Fil reprennent le fil ; un autre outil prend sa place
    if (o == Outil::Selection || o == Outil::Fil) {
      reprendreTrace();
      return;
    }
    m_outils.remplacer(creerOutil(o, parametre));
  } else if (actif && actif->geste() && actif->genre() == static_cast<int>(Outil::Fil) && o != Outil::Selection && o != Outil::Fil) {
    m_outils.empiler(creerOutil(o, parametre));  // le fil en cours attend, avec son dernier point fixé
  } else {
    m_outils.actif()->quitter();  // un fil en cours pose ce qui est fixé
    m_outils.armer(creerOutil(o, parametre));
  }
  // Un outil armé vide la sélection : R, X et Y n'agissent plus sur l'ancienne (S11)
  if (o != Outil::Selection && avecBandeau() && !m_sel->vide()) m_sel->vider();
  appliquerCurseur();
  if (const QString aide = aideOutil(o); !aide.isEmpty()) emit message(aide);
  majBandeau();
  emit outilChange();
  update();
}

VueSchema::Outil VueSchema::outil() const { return static_cast<Outil>(m_outils.actif()->genre()); }

GesteVue VueSchema::geste(QMouseEvent* e) const { return {e->position(), e->button(), e->buttons(), e->modifiers()}; }

// Bandeau d'outil : l'outil armé, sinon le réseau surligné ; rien à l'état neutre (charte, 1.7)
void VueSchema::majBandeau() {
  if (!m_bandeau) return;
  if (!avecBandeau() || m_impression) {
    m_bandeau->montrer({});
    return;
  }
  ContenuBandeau c = m_outils.actif()->bandeau();
  // Commande imbriquée : le bandeau dit ce qui attend, et sa sortie reprend le fil (ERG-21)
  if (const OutilVue* attente = m_outils.suspendu(); attente && !c.vide()) {
    QString t = attente->bandeau().titre.section(QStringLiteral(" · "), 0, 0);  // « Fil · équerre » : le fil
    if (!t.isEmpty()) t[0] = t[0].toLower();
    c.detail = (c.detail.isEmpty() ? QString() : c.detail + QStringLiteral(" · ")) + t + QStringLiteral(" suspendu");
    for (ActionBandeau& x : c.actions)
      if (x.sortie) x.libelle = QStringLiteral("Reprendre le fil");
  }
  // Bandeau de sélection : le réseau surligné y reste dit
  if (!c.vide() && !m_reseau.empty() && outil() == Outil::Selection && !m_outils.actif()->geste())
    c.detail += QStringLiteral(" · réseau %1 surligné").arg(QString::fromStdString(m_reseau));
  if (c.vide() && !m_reseau.empty()) {
    c.titre = QStringLiteral("Réseau %1 surligné").arg(QString::fromStdString(m_reseau));
    if (m_k)
      if (const int i = m_k->indice(m_reseau); i >= 0) c.detail = QStringLiteral("%1 broche(s)").arg(m_k->reseaux[static_cast<std::size_t>(i)].broches.size());
    ActionBandeau effacer;
    effacer.id = QStringLiteral("effacer");
    effacer.libelle = QStringLiteral("Effacer");
    effacer.touche = QStringLiteral("Échap");
    effacer.faire = [this] { m_sel->surligner({}); };
    effacer.sortie = true;
    c.actions.push_back(effacer);
  }
  m_bandeau->montrer(c);
}

// Échap, une seule règle (charte) : un geste en cours est annulé ; sinon l'état neutre en une fois (outil
// Sélection, rien de choisi, rien de surligné, bandeau masqué). Un Échap de plus ne fait rien.
// Curseur (charte) : main fermée pendant un déplacement de la vue, flèche et sablier pendant un calcul long, cercle
// barré pour un outil sur une conception publiée, sinon celui de l'outil
void VueSchema::appliquerCurseur() {
  if (m_nav && m_nav->panoramique()) setCursor(Qt::ClosedHandCursor);
  else if (m_calculLong) setCursor(Qt::BusyCursor);
  else if (const prom::ecad::ConceptionElec* c = conception(); c && c->verrouillee && outil() != Outil::Selection) setCursor(Qt::ForbiddenCursor);
  else setCursor(m_outils.actif()->curseur());
}

void VueSchema::setCalculLong(bool oui) {
  if (oui == m_calculLong) return;
  m_calculLong = oui;
  appliquerCurseur();
}

void VueSchema::echap() {
  if (!m_relierDepuis.empty()) {
    m_relierDepuis.clear();
    majBandeau();
    update();
    return;
  }
  if (m_outils.actif()->echap()) {
    majBandeau();
    return;
  }
  if (m_outils.suspendus()) {
    reprendreTrace();  // Échap dans une commande imbriquée : le fil reprend, rien d'autre ne change
    return;
  }
  if (outil() != Outil::Selection) setOutil(Outil::Selection);
  m_sel->neutre();
  majBandeau();
  update();
}

void VueSchema::cliquerEn(Pt p, bool double_) {
  m_outils.recycler();
  m_souris = ecran(p);
  GesteVue g{m_souris, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
  OutilVue* o = m_outils.actif();
  o->survol(g);
  o->appui(g);
  if (m_outils.actif() == o) {
    if (double_) o->doubleClic(g);
    g.boutons = Qt::NoButton;
    if (m_outils.actif() == o) o->relacher(g);
  }
  majBandeau();
  update();
}

void VueSchema::copier() {
  const ConceptionElec* c = conception();
  if (!c || m_selection.empty()) return;
  pressePapiers() = copierSchema(*c, static_cast<std::size_t>(m_feuille), m_selection);
  // Le presse-papiers du système aussi (2.13, SAI-27) : un autre projet ou une autre fenêtre de Prométhée le
  // collent, définitions comprises ; les autres programmes reçoivent les repères en texte
  if (domaine() == Domaine::Electronique) {
    const PressePapiersSchema& pp = pressePapiers();
    QStringList reperes;
    for (const auto& k : pp.composants) reperes << QString::fromStdString(k.reference);
    auto* m = new QMimeData;
    m->setData(QString::fromLatin1(MIME_SCHEMA), QByteArray::fromStdString(presseVersJson(pp).dump()));
    m->setText(reperes.isEmpty() ? QStringLiteral("%1 objet(s) du schéma").arg(m_selection.size()) : reperes.join(QStringLiteral(", ")));
    QGuiApplication::clipboard()->setMimeData(m);
  }
  emit message(QStringLiteral("%1 élément(s) copié(s) : Ctrl+V pour coller sous la souris.").arg(m_selection.size()));
}

PressePapiersSchema VueSchema::presseCourant() const {
  if (domaine() == Domaine::Electronique)
    if (const QMimeData* m = QGuiApplication::clipboard()->mimeData(); m && m->hasFormat(QString::fromLatin1(MIME_SCHEMA))) {
      try {
        PressePapiersSchema pp = presseDepuisJson(nlohmann::ordered_json::parse(m->data(QString::fromLatin1(MIME_SCHEMA)).toStdString()));
        if (!pp.vide()) return pp;
      } catch (...) {
        // un bloc abîmé : celui de la session
      }
    }
  return pressePapiers();
}

void VueSchema::couper() {
  copier();
  supprimerSelection();
}

bool VueSchema::peutColler() const {
  if (!pressePapiers().vide()) return true;
  const QMimeData* m = domaine() == Domaine::Electronique ? QGuiApplication::clipboard()->mimeData() : nullptr;
  return m && m->hasFormat(QString::fromLatin1(MIME_SCHEMA));
}

void VueSchema::coller() {
  const PressePapiersSchema pp = presseCourant();
  if (pp.vide()) return;
  // Sous la souris, le bloc la suit jusqu'au clic qui le pose (2.13) ; Échap le retire
  if (domaine() == Domaine::Electronique && rect().contains(m_souris.toPoint()) && collerSousLaSouris(pp)) {
    emit message(QStringLiteral("Le bloc suit la souris : un clic le pose, Échap le retire."));
    return;
  }
  const Pt ou = rect().contains(m_souris.toPoint()) ? surGrille(monde(m_souris)) : pp.reference + ptMm(5.08, 5.08);
  const std::set<std::string> crees = collerDans(pp, ou);
  if (!crees.empty()) {
    selectionner(crees);
    emit message(domaine() == Domaine::Electrique ? QStringLiteral("%1 élément(s) collé(s) : les appareils neufs ont reçu leur repère.").arg(crees.size())
                                       : annotationALaPose() ? QStringLiteral("%1 élément(s) collé(s), repères libres donnés.").arg(crees.size())
                                                             : QStringLiteral("%1 élément(s) collé(s) : les composants neufs sont à annoter (R?, C?…).").arg(crees.size()));
  }
}

std::set<std::string> VueSchema::collerDans(const PressePapiersSchema& pp, Pt ou) {
  const int fi = m_feuille;
  std::set<std::string> crees;
  // Repère donné à la pose (S15) : les composants collés reçoivent un repère libre (SAI-27)
  const bool libres = annotationALaPose();
  modifier([&](Feuille&, ConceptionElec& c) {
    crees = libres ? collerSchemaAvec(c, static_cast<std::size_t>(fi), pp, ou, ReperesCollage::Libres) : collerSchema(c, static_cast<std::size_t>(fi), pp, ou);
  });
  return crees;
}

void VueSchema::dupliquer() {
  const ConceptionElec* c = conception();
  if (!c || m_selection.empty()) return;
  const PressePapiersSchema pp = copierSchema(*c, static_cast<std::size_t>(m_feuille), m_selection);
  const std::set<std::string> crees = collerDans(pp, pp.reference + ptMm(5.08, 5.08));
  if (!crees.empty()) selectionner(crees);
}

void VueSchema::modifierTexte(const std::string& uuid) {
  const Feuille* f = feuilleCourante();
  if (!f) return;
  for (const auto& t : f->textes)
    if (t.uuid == uuid) {
      const std::string ancien = t.texte;
      saisirSurPlace(t.p, QStringLiteral("Texte écrit sur la feuille"), QString::fromStdString(t.texte), [this, uuid, ancien](const QString& nouveau) {
        if (nouveau.toStdString() == ancien) return;
        modifier([&](Feuille& g, ConceptionElec&) { for (auto& x : g.textes) if (x.uuid == uuid) x.texte = nouveau.toStdString(); });
      });
      return;
    }
}

QLineEdit* VueSchema::saisieEnCours() const { return m_saisie.data(); }

void VueSchema::saisirSurPlace(Pt p, const QString& invite, const QString& valeur, std::function<void(const QString&)> valider) {
  if (m_saisie) m_saisie->deleteLater();
  auto* e = new QLineEdit(this);
  e->setObjectName(QStringLiteral("saisieSurPlace"));
  e->setPlaceholderText(invite);
  e->setToolTip(invite + QStringLiteral(" — Entrée valide, Échap renonce"));
  e->setText(valeur);
  e->setMinimumWidth(240);
  e->adjustSize();
  e->resize(std::max(240, e->sizeHint().width()), e->sizeHint().height());
  const QPointF q = ecran(p);
  e->move(std::clamp(static_cast<int>(q.x()) + 10, 4, std::max(4, width() - e->width() - 4)), std::clamp(static_cast<int>(q.y()) - e->height() / 2, 4, std::max(4, height() - e->height() - 4)));
  e->show();
  e->raise();
  e->setFocus(Qt::OtherFocusReason);
  e->selectAll();
  m_saisie = e;
  equiperIncrement(e);
  auto fini = std::make_shared<bool>(false);
  auto conclure = [this, e, fini, valider](bool garder) {
    if (*fini) return;
    *fini = true;
    const QString t = e->text().trimmed();
    if (m_saisie == e) m_saisie.clear();
    e->hide();
    e->deleteLater();
    setFocus();
    if (garder && !t.isEmpty()) valider(t);
  };
  // Entrée valide ; un clic ailleurs ne valide que ce qui a été retouché (S20)
  connect(e, &QLineEdit::returnPressed, this, [conclure] { conclure(true); });
  connect(e, &QLineEdit::editingFinished, this, [conclure, e] { conclure(e->isModified()); });
  e->installEventFilter(new FiltreTouche(e, Qt::Key_Escape, [conclure] { conclure(false); }));
}

void VueSchema::supprimerSelection() {
  if (m_selection.empty()) return;
  const std::set<std::string> sel = m_selection;
  modifier([&](Feuille& f, ConceptionElec& c) {
    std::set<std::string> composants;
    for (const auto& s : f.symboles) if (sel.count(s.uuid) && !s.composant.empty()) composants.insert(s.composant);
    f.symboles.erase(std::remove_if(f.symboles.begin(), f.symboles.end(), [&](const SymbolePlace& s) { return sel.count(s.uuid) > 0; }), f.symboles.end());
    f.fils.erase(std::remove_if(f.fils.begin(), f.fils.end(), [&](const Fil& w) { return sel.count(w.uuid) > 0; }), f.fils.end());
    f.etiquettes.erase(std::remove_if(f.etiquettes.begin(), f.etiquettes.end(), [&](const Etiquette& e) { return sel.count(e.uuid) > 0; }), f.etiquettes.end());
    f.textes.erase(std::remove_if(f.textes.begin(), f.textes.end(), [&](const TexteSchema& t) { return sel.count(t.uuid) > 0; }), f.textes.end());
    f.jonctions.erase(std::remove_if(f.jonctions.begin(), f.jonctions.end(), [&](const Jonction& j) { return sel.count(idJonction(j)) > 0; }), f.jonctions.end());
    f.nonConnexions.erase(std::remove_if(f.nonConnexions.begin(), f.nonConnexions.end(), [&](const NonConnexion& n) { return sel.count(idNonConnexion(n)) > 0; }), f.nonConnexions.end());
    // Un composant qui n'a plus de symbole disparaît ; son empreinte suit par la synchronisation, dans la même
    // commande si aucun cuivre ne la touche, sinon en écart en attente (3.6, S18) : jamais de pistes orphelines en
    // silence
    retirerComposantsSansSymbole(c, composants);
  });
  m_sel->vider();
}

// La feuille et la sélection au départ d'un déplacement : chaque pas du glisser repart d'elles (2.8)
void VueSchema::preparerDeplacement() {
  const Feuille* f = feuilleCourante();
  m_feuilleDepart.reset();
  m_selDepart = m_selection;
  if (f) m_feuilleDepart = *f;
}

void VueSchema::decaler(ConceptionElec& cc, int fi, Pt d) const {
  if (!m_feuilleDepart || fi < 0 || fi >= static_cast<int>(cc.feuilles.size())) return;
  cc.feuilles[static_cast<std::size_t>(fi)] = *m_feuilleDepart;
  deplacerSchema(cc, static_cast<std::size_t>(fi), m_selDepart, d, m_elastique);
}

void VueSchema::deplacerSelection(Pt delta) {
  if (m_selection.empty() || refuseVerrou()) return;
  const std::set<std::string> sel = m_selection;
  const int fi = m_feuille;
  // Appuis rapprochés sur les flèches : un seul pas d'annulation (C18) ; fils élastiques
  modifier([&](Feuille&, ConceptionElec& cc) { deplacerSchema(cc, static_cast<std::size_t>(fi), sel, delta, true); }, QStringLiteral("fleches-schema"));
}

// ---------- Dessin ----------
void VueSchema::dessinerSymbole(QPainter& g, const Repere& r, const DefSymbole& d, const SymbolePlace& s, const QString& reference, const QString& valeur, bool fantome) {
  const QColor encre = fantome ? APERCU : (d.alimentation || d.drapeau) ? ALIM : ENCRE;
  const double e = r.echelle;
  const QPen trait(encre, std::max(1.0, 0.2 * e), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  auto E = [&](Pt p) { return r.ecran(transformer(s, p)); };
  for (const auto& t : d.traces) {
    if (!dansUnite(t.unite, s, d)) continue;
    g.setPen(trait);
    g.setBrush(t.rempli ? QBrush(encre) : t.fond && !fantome ? QBrush(FOND_CORPS) : QBrush(Qt::NoBrush));
    switch (t.forme) {
      case Trace::Forme::Polyligne: {
        QPolygonF poly;
        for (const Pt& p : t.points) poly << E(p);
        if (t.ferme) g.drawPolygon(poly);
        else g.drawPolyline(poly);
        break;
      }
      case Trace::Forme::Rectangle:
        if (t.points.size() >= 2) {
          const Pt a = t.points[0], b = t.points[1];
          QPolygonF poly;
          poly << E(a) << E({b.x, a.y}) << E(b) << E({a.x, b.y});
          g.drawPolygon(poly);
        }
        break;
      case Trace::Forme::Cercle:
        if (!t.points.empty()) g.drawEllipse(E(t.points.front()), mm(t.rayon) * e, mm(t.rayon) * e);
        break;
      case Trace::Forme::Arc:
        if (!t.points.empty()) {
          QPolygonF poly;
          for (int i = 0; i <= 16; ++i) {
            const double a = (t.debut + (t.fin - t.debut) * i / 16.0) * std::numbers::pi / 180;
            poly << E(t.points.front() + Pt{static_cast<Nm>(std::llround(static_cast<double>(t.rayon) * std::cos(a))), static_cast<Nm>(std::llround(-static_cast<double>(t.rayon) * std::sin(a)))});
          }
          g.setBrush(Qt::NoBrush);
          g.drawPolyline(poly);
        }
        break;
      case Trace::Forme::Texte:
        if (!t.points.empty()) texte(g, E(t.points.front()), QString::fromStdString(t.texte), mm(t.taille) * e, 0, encre);
        break;
      // 0.34 (7.20) : ellipses et courbes de Bézier du corps, découpées dans le repère du symbole puis tournées avec lui ;
      // cadre de texte et image
      case Trace::Forme::Ellipse:
      case Trace::Forme::Bezier: {
        QPolygonF poly;
        for (const Pt& p : contourTrace(t, 32)) poly << E(p);
        if (t.forme == Trace::Forme::Ellipse) g.drawPolygon(poly);
        else g.drawPolyline(poly);
        break;
      }
      case Trace::Forme::CadreTexte:
      case Trace::Forme::Image:
        if (t.points.size() >= 2) {
          const QRectF cadre = QRectF(E(t.points[0]), E(t.points[1])).normalized();
          g.drawRect(cadre);
          if (t.forme == Trace::Forme::CadreTexte) {
            QFont fo = g.font();
            fo.setPixelSize(std::max(1, static_cast<int>(mm(t.taille) * e)));
            g.setFont(fo);
            g.setPen(encre);
            g.drawText(cadre.adjusted(2, 2, -2, -2), Qt::AlignCenter | Qt::TextWordWrap, QString::fromStdString(t.texte));
          }
        }
        break;
    }
  }
  // Broches : trait, numéro le long du trait, nom dans le corps
  for (const auto& b : brochesPlacees(s, d)) {
    const QPointF p = r.ecran(b.p), v = direction(b.direction), fin = p + v * mm(b.broche->longueur) * e;
    g.setPen(trait);
    if (b.broche->longueur > 0) g.drawLine(p, fin);
    const bool horizontal = b.direction == 0 || b.direction == 2;
    if (b.broche->numeroVisible && b.broche->longueur > 0 && !fantome) {
      const QPointF milieu = (p + fin) / 2 + (horizontal ? QPointF(0, -0.75 * e) : QPointF(-0.75 * e, 0));
      texte(g, milieu, QString::fromStdString(b.numero), 1.0 * e, horizontal ? 0 : 1, BROCHE);
    }
    if (b.broche->nomVisible && b.broche->nom != "~" && !b.broche->nom.empty()) {
      const QPointF ou = fin + v * 0.6 * e;
      const int al = b.direction == 0 ? -1 : b.direction == 2 ? 1 : 0;
      texte(g, ou + (horizontal ? QPointF() : v * 0.6 * e), QString::fromStdString(b.broche->nom), 1.15 * e, al, encre);
    }
  }
  // Référence et valeur restent horizontales, à leur place propre ou à celle de la définition (2.6)
  const PlacesChamps pc = placesChamps(d, s, reference, valeur);
  if (!reference.isEmpty()) texte(g, r.ecran(pc.ref), reference, 1.4 * e, pc.alRef, encre);
  if (!valeur.isEmpty()) texte(g, r.ecran(pc.val), valeur, 1.3 * e, pc.alVal, encre);
}

// Places du repère et de la valeur : leur ancre et leur alignement (texte à droite d'une ancre à droite du symbole,
// à gauche d'une ancre à gauche, centré sinon). Sans place propre, si la rotation met les deux côte à côte au point
// de se chevaucher, la valeur passe sous la référence.
VueSchema::PlacesChamps VueSchema::placesChamps(const DefSymbole& d, const SymbolePlace& s, const QString& reference, const QString& valeur) {
  auto alignement = [&](Pt q) { return q.x > s.position.x + 500'000 ? -1 : q.x < s.position.x - 500'000 ? 1 : 0; };
  auto etendue = [&](Pt q, const QString& t, double tailleMm) {
    const double l = 0.62 * tailleMm * t.size(), x = mm(q.x);
    const int al = alignement(q);
    return al < 0 ? std::pair{x, x + l} : al > 0 ? std::pair{x - l, x} : std::pair{x - l / 2, x + l / 2};
  };
  PlacesChamps p;
  p.ref = ancrageChamp(s, d, false);
  p.val = ancrageChamp(s, d, true);
  p.alRef = alignement(p.ref);
  p.alVal = alignement(p.val);
  if (!s.champReference && !s.champValeur && !reference.isEmpty() && !valeur.isEmpty() && std::abs(mm(p.ref.y - p.val.y)) < 1.3) {
    const auto [a0, a1] = etendue(p.ref, reference, 1.4);
    const auto [b0, b1] = etendue(p.val, valeur, 1.3);
    if (a0 < b1 && b0 < a1) {
      p.val = p.ref + ptMm(0, 1.6);
      p.alVal = p.alRef;
    }
  }
  return p;
}

void VueSchema::textesDe(const Feuille& f, const SymbolePlace& s, const DefSymbole& d, QString& ref, QString& val) const {
  const ConceptionElec* c = conception();
  const ComposantElec* x = c ? c->composant(s.composant) : nullptr;
  ref = x ? QString::fromStdString(x->reference + (d.unites > 1 ? lettreUnite(s.unite) : std::string())) : QString();
  val = d.alimentation ? QString::fromStdString(s.nomAlim.empty() ? d.nom : s.nomAlim) : d.drapeau ? QString() : x ? QString::fromStdString(x->valeur) : QString();
  // Mode d'affichage des valeurs (3.2) : comme tapées, en CEI 60062 compacte (4k7) ou en SI (4,7 kΩ) ; une valeur non
  // comprise reste comme tapée
  if (x && c && !d.alimentation && !d.drapeau) {
    const int mode = RegistreReglages::instance().valeur(QStringLiteral("schema/affichageValeurs")).toInt();
    if (mode > 0) {
      const ValeurComprise v = lireValeurComposant(x->valeur, familleDe(*c, *x));
      val = QString::fromStdString(ecrireValeurComposant(v, mode == 1 ? AffichageValeur::Compact : AffichageValeur::SystemeInternational, x->valeur));
    }
  }
  textesSymbole(f, s, d, x, ref, val);
  if (d.alimentation || d.drapeau) ref.clear();
}

QPixmap VueSchema::apercu(const DefSymbole& d, QSize taille) {
  prendreEncres(false);
  QPixmap pm(taille);
  pm.fill(PAPIER);
  QPainter g(&pm);
  g.setRenderHint(QPainter::Antialiasing);
  // Symbole à plusieurs unités : les unités côte à côte (U?A, U?B…), comme elles seront posées
  std::vector<SymbolePlace> unites;
  Boite b;
  Nm x = 0;
  for (int u = 1; u <= std::max(1, d.unites); ++u) {
    SymbolePlace sp;
    sp.unite = u;
    const Boite bu = boiteSymbole(sp, d);
    sp.position = {x - bu.x0, 0};
    x += bu.largeur() + 5'080'000;
    unites.push_back(sp);
  }
  auto reference = [&](const SymbolePlace& sp) {
    return d.alimentation || d.drapeau ? QString() : QString::fromStdString(d.prefixe + "?" + (d.unites > 1 ? lettreUnite(sp.unite) : std::string()));
  };
  for (const auto& sp : unites) {
    b.etendre(boiteSymbole(sp, d));
    // Place des textes : référence et valeur, alignés comme au dessin
    for (const auto& [p, t] : {std::pair{d.posReference, reference(sp)}, std::pair{d.posValeur, QString::fromStdString(d.nom)}}) {
      if (t.isEmpty()) continue;
      const Nm l = static_cast<Nm>(0.62 * 1.4 * 1e6 * t.size());
      const Nm x0 = sp.position.x + (p.x > 500'000 ? p.x : p.x < -500'000 ? p.x - l : p.x - l / 2);
      b.etendre(Boite{x0, p.y - 800'000, x0 + l, p.y + 800'000});
    }
  }
  b = b.gonflee(2'000'000);
  const double e = std::min({(taille.width() - 20) / mm(std::max<Nm>(b.largeur(), 1)), (taille.height() - 20) / mm(std::max<Nm>(b.hauteur(), 1)), 14.0});
  const Repere r{QPointF(taille.width() / 2.0 - mm(b.centre().x) * e, taille.height() / 2.0 - mm(b.centre().y) * e), e};
  for (const auto& sp : unites) dessinerSymbole(g, r, d, sp, reference(sp), QString::fromStdString(d.nom));
  return pm;
}

void VueSchema::dessinerFond(QPainter& g, const Feuille& f, const ConceptionElec& c) {
  const Boite b = cadreFeuille(f.format);
  const QRectF feuille(ecran({b.x0, b.y0}), ecran({b.x1, b.y1}));
  g.fillRect(feuille, PAPIER);
  g.setPen(QPen(CADRE, 1));
  g.setBrush(Qt::NoBrush);
  g.drawRect(feuille);
  const double m = 10 * m_echelle;
  const QRectF interieur = feuille.adjusted(m, m, -m, -m);
  g.setPen(QPen(CADRE, std::max(1.0, 0.35 * m_echelle)));
  g.drawRect(interieur);
  // Grille de points qui suit l'accroche (S27), sur la seule partie visible de la feuille
  if (const double pas = pasGrille(); pas > 0 && !m_impression) {
    const Pt hg = monde(QPointF(0, 0)), bd = monde(QPointF(width(), height()));
    const double gauche = std::max(mm(b.x0), mm(hg.x)), droite = std::min(mm(b.x1), mm(bd.x)), haut = std::max(mm(b.y0), mm(hg.y)), bas = std::min(mm(b.y1), mm(bd.y));
    QPolygonF points;
    for (double x = std::ceil(gauche / pas) * pas; x <= droite; x += pas)
      for (double y = std::ceil(haut / pas) * pas; y <= bas; y += pas) points << ecran(ptMm(x, y));
    g.setPen(QPen(GRILLE, 1.2));
    g.drawPoints(points);
  }
  // Cartouche
  const double l = 170 * m_echelle, h = 30 * m_echelle;
  const QRectF ca(interieur.right() - l, interieur.bottom() - h, l, h);
  g.setPen(QPen(CADRE, std::max(1.0, 0.25 * m_echelle)));
  g.setBrush(CARTOUCHE);
  g.drawRect(ca);
  const double lig = h / 3;
  g.drawLine(QPointF(ca.left(), ca.top() + lig * 1.4), QPointF(ca.right(), ca.top() + lig * 1.4));
  g.drawLine(QPointF(ca.left(), ca.top() + lig * 2.2), QPointF(ca.right(), ca.top() + lig * 2.2));
  for (double fx : {0.30, 0.55, 0.72, 0.86}) g.drawLine(QPointF(ca.left() + l * fx, ca.top() + lig * 1.4), QPointF(ca.left() + l * fx, ca.bottom()));
  const Cartouche& t = c.cartouche;
  texte(g, QPointF(ca.left() + 3 * m_echelle, ca.top() + lig * 0.7), QString::fromStdString(t.titre.empty() ? c.nom : t.titre), 5 * m_echelle, -1, ENCRE);
  const double petit = 1.8 * m_echelle, normal = 2.6 * m_echelle;
  const std::pair<const char*, std::string> cases[] = {{"Société", t.societe}, {"Auteur", t.auteur}, {"Révision", t.revision}, {"Date", t.date}, {"Feuille", ""}};
  const double xs[] = {0, 0.30, 0.55, 0.72, 0.86, 1.0};
  for (int i = 0; i < 5; ++i) {
    const double x = ca.left() + l * xs[i] + 2 * m_echelle;
    texte(g, QPointF(x, ca.top() + lig * 1.75), QString::fromUtf8(cases[i].first), petit, -1, CADRE);
    QString v = QString::fromStdString(cases[i].second);
    if (i == 4) v = QStringLiteral("%1 / %2").arg(m_feuille + 1).arg(c.feuilles.size());
    texte(g, QPointF(x, ca.top() + lig * 2.65), v, normal, -1, ENCRE);
  }
  texte(g, QPointF(ca.right() - 2 * m_echelle, ca.top() + lig * 0.7), QStringLiteral("Prométhée"), 2.2 * m_echelle, 1, MARQUE);
  texte(g, QPointF(ca.right() - 2 * m_echelle, ca.top() + lig * 1.15), QString::fromStdString(f.nom) + QStringLiteral(" · ") + QString::fromStdString(f.format), 1.8 * m_echelle, 1, CADRE);
}

void VueSchema::dessinerFils(QPainter& g, const Feuille& f) {
  const double ep = std::max(1.2, 0.25 * m_echelle);
  const SignesCanevas& sg = theme().signesSchema(m_impression);
  // Saut aux croisements sans jonction, en option (2.9, SAI-10) : le fil horizontal enjambe le vertical d'un
  // demi-cercle, pour qu'un croisement ne se lise jamais comme une liaison
  const bool sauts = sautsAuxCroisements();
  std::set<Pt> joints;
  if (sauts) {
    for (const auto& j : f.jonctions) joints.insert(j.p);
    if (m_k)
      if (const auto it = m_k->jonctions.find(f.uuid); it != m_k->jonctions.end()) joints.insert(it->second.begin(), it->second.end());
  }
  g.setPen(QPen(FIL, ep, Qt::SolidLine, Qt::RoundCap));
  g.setBrush(Qt::NoBrush);
  // Fils courbes (2.19) : deux fils courbes à l'équerre qui partagent un bout se rejoignent par un arrondi ; chacun
  // est dessiné en retrait du coin, la connexion restant au coin
  auto longueur = [](const Fil& x) { return std::max(std::abs(x.b.x - x.a.x), std::abs(x.b.y - x.a.y)); };
  auto partenaire = [&f](const Fil& w, Pt p) -> const Fil* {
    if (w.rayon <= 0) return nullptr;
    for (const auto& v : f.fils)
      if (&v != &w && v.rayon > 0 && (v.a == p || v.b == p) && (w.a.y == w.b.y) != (v.a.y == v.b.y)) return &v;
    return nullptr;
  };
  auto rayonCoin = [&](const Fil& w, const Fil& v) { return std::min({w.rayon, v.rayon, longueur(w) / 2, longueur(v) / 2}); };
  auto retrait = [](const Fil& w, Pt p, Nm r) {
    const Pt autre = w.a == p ? w.b : w.a;
    return Pt{p.x + (autre.x > p.x ? r : autre.x < p.x ? -r : 0), p.y + (autre.y > p.y ? r : autre.y < p.y ? -r : 0)};
  };
  for (const auto& w : f.fils) {
    g.setOpacity(opaciteObjet(w.uuid));  // estompage (2.26)
    Pt da = w.a, db = w.b;
    if (const Fil* v = partenaire(w, w.a)) da = retrait(w, w.a, rayonCoin(w, *v));
    if (const Fil* v = partenaire(w, w.b)) db = retrait(w, w.b, rayonCoin(w, *v));
    // L'arrondi, tracé depuis le fil horizontal
    if (w.rayon > 0 && w.a.y == w.b.y)
      for (const Pt p : {w.a, w.b})
        if (const Fil* v = partenaire(w, p)) {
          const Nm r = rayonCoin(w, *v);
          QPainterPath arc(ecran(retrait(w, p, r)));
          arc.quadTo(ecran(p), ecran(retrait(*v, p, r)));
          g.drawPath(arc);
        }
    if (!sauts || w.a.y != w.b.y || w.a.x == w.b.x) {
      g.drawLine(ecran(da), ecran(db));
      continue;
    }
    const Nm x0 = std::min(da.x, db.x), x1 = std::max(da.x, db.x), y = w.a.y;
    std::vector<Nm> croisements;
    for (const auto& v : f.fils)
      if (v.a.x == v.b.x && v.a.x > x0 && v.a.x < x1 && std::min(v.a.y, v.b.y) < y && std::max(v.a.y, v.b.y) > y && !joints.count(Pt{v.a.x, y})) croisements.push_back(v.a.x);
    std::sort(croisements.begin(), croisements.end());
    const double r = std::max(2.5, 0.6 * m_echelle);
    QPainterPath chemin(ecran({x0, y}));
    for (const Nm x : croisements) {
      const QPointF c = ecran({x, y});
      chemin.lineTo(c - QPointF(r, 0));
      chemin.arcTo(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), 180, -180);
    }
    chemin.lineTo(ecran({x1, y}));
    g.drawPath(chemin);
  }
  g.setOpacity(1.0);
  // Fils choisis : bande, contour et poignées (le surlignage d'un réseau, lui, est un halo dessiné à part)
  if (!m_impression)
    for (const auto& w : f.fils)
      if (m_selection.count(w.uuid)) signes::selectionTrait(g, ecran(w.a), ecran(w.b), ep, sg.sel);
  // Jonctions (posées ou calculées), marques de non-connexion
  g.setPen(Qt::NoPen);
  g.setBrush(FIL);
  std::vector<Pt> jonctions;
  for (const auto& j : f.jonctions) jonctions.push_back(j.p);
  if (m_k)
    if (const auto it = m_k->jonctions.find(f.uuid); it != m_k->jonctions.end()) jonctions.insert(jonctions.end(), it->second.begin(), it->second.end());
  for (const Pt& p : jonctions) g.drawEllipse(ecran(p), std::max(2.5, 0.5 * m_echelle), std::max(2.5, 0.5 * m_echelle));
  const double x = 0.8 * m_echelle;
  auto boitePoint = [&](Pt p) { return QRectF(ecran(p) - QPointF(x + 2, x + 2), QSizeF(2 * x + 4, 2 * x + 4)); };
  if (!m_impression)
    for (const auto& j : f.jonctions)
      if (m_selection.count(idJonction(j))) signes::selectionBoite(g, boitePoint(j.p), sg.sel);
  // Marques de non-connexion à leur encre propre (S26) ; choisies, le signe de la sélection
  for (const auto& n : f.nonConnexions) {
    g.setPen(QPen(MARQUE, std::max(1.5, 0.25 * m_echelle)));
    const QPointF c = ecran(n.p);
    g.drawLine(c + QPointF(-x, -x), c + QPointF(x, x));
    g.drawLine(c + QPointF(-x, x), c + QPointF(x, -x));
    if (!m_impression && m_selection.count(idNonConnexion(n))) signes::selectionBoite(g, boitePoint(n.p), sg.sel);
  }
  // Extrémités libres : petit carré d'alerte
  if (m_k)
    if (const auto it = m_k->extremitesLibres.find(f.uuid); it != m_k->extremitesLibres.end()) {
      g.setPen(QPen(APERCU, 1.2));
      g.setBrush(Qt::NoBrush);
      for (const Pt& p : it->second) g.drawRect(QRectF(ecran(p) - QPointF(0.5, 0.5) * m_echelle, QSizeF(m_echelle, m_echelle)));
    }
}

void VueSchema::dessinerSurlignage(QPainter& g, const Feuille& f, const ConceptionElec& c) {
  const SignesCanevas& sg = theme().signesSchema();
  const Boite cadre = cadreFeuille(f.format);
  QColor voile = PAPIER;
  voile.setAlphaF(0.40);  // le reste de la vue s'éclaircit à 60 %
  g.fillRect(QRectF(ecran({cadre.x0, cadre.y0}), ecran({cadre.x1, cadre.y1})).normalized(), voile);
  const double ep = std::max(1.2, 0.25 * m_echelle);
  if (m_k)
    for (const auto& w : f.fils)
      if (const auto it = m_k->parFil.find(w.uuid); it != m_k->parFil.end() && m_k->reseaux[static_cast<std::size_t>(it->second)].nom == m_reseau) {
        signes::haloTrait(g, ecran(w.a), ecran(w.b), ep, sg.surl);
        g.setPen(QPen(FIL, ep, Qt::SolidLine, Qt::RoundCap));
        g.drawLine(ecran(w.a), ecran(w.b));
      }
  // Broches du réseau, alimentations et étiquettes qui le nomment
  for (const auto& s : f.symboles) {
    const DefSymbole* d = c.symbole(s.symbole);
    if (!d) continue;
    if (d->alimentation) {
      if (s.nomAlim == m_reseau) signes::haloPoint(g, ecran(s.position), 0.8 * m_echelle, sg.surl);
      continue;
    }
    if (!m_k || s.composant.empty()) continue;
    for (const auto& b : brochesPlacees(s, *d))
      if (m_k->nom(s.composant, b.numero) == m_reseau) signes::haloPoint(g, ecran(b.p), 0.4 * m_echelle, sg.surl);
  }
  for (const auto& e : f.etiquettes)
    if (m_k)
      if (const auto it = m_k->parEtiquette.find(e.uuid); it != m_k->parEtiquette.end() && m_k->reseaux[static_cast<std::size_t>(it->second)].nom == m_reseau)
        signes::haloPoint(g, ecran(e.p), 0.6 * m_echelle, sg.surl);
}

bool VueSchema::etiquetteAuBoutOuvert() { return QSettings().value(QStringLiteral("schema/etiquetteBoutOuvert"), false).toBool(); }
void VueSchema::setEtiquetteAuBoutOuvert(bool oui) { QSettings().setValue(QStringLiteral("schema/etiquetteBoutOuvert"), oui); }

void VueSchema::etiquetterBout(Pt bout, Pt avant) {
  // Le texte s'écarte du fil : à droite d'un fil venu de la gauche, au-dessus d'un fil venu d'en bas…
  const int rotation = bout.x > avant.x ? 0 : bout.x < avant.x ? 2 : bout.y < avant.y ? 1 : 3;
  const QString propose = m_derniereEtiquette.empty() ? QString() : QString::fromStdString(suivantSerie(m_derniereEtiquette));
  const QPointer<VueSchema> garde(this);
  saisirSurPlace(bout, QStringLiteral("Nom du réseau au bout de ce fil"), propose, [this, garde, bout, rotation](const QString& t) {
    const std::string nom = t.trimmed().toStdString();
    if (!garde || nom.empty()) return;
    modifier([&](Feuille& f, ConceptionElec&) {
      Etiquette e;
      e.uuid = nouvelUuid();
      e.texte = nom;
      e.p = bout;
      e.rotation = rotation;
      f.etiquettes.push_back(e);
    }, {}, QStringLiteral("Étiquette %1").arg(QString::fromStdString(nom)));
    m_derniereEtiquette = nom;
  });
}

bool VueSchema::sautsAuxCroisements() { return QSettings().value(QStringLiteral("schema/sautsCroisements"), false).toBool(); }
void VueSchema::setSautsAuxCroisements(bool oui) { QSettings().setValue(QStringLiteral("schema/sautsCroisements"), oui); }

VueSchema::CleCache VueSchema::cleCache() const {
  CleCache k;
  k.generation = m_generation;
  k.selection = m_generationSel;
  k.invalidations = m_invalidations;
  k.theme = theme().generation();
  k.conception = m_conception;
  k.feuille = m_feuille;
  k.echelle = m_echelle;
  k.origine = m_origine;
  k.taille = size();
  k.dpr = devicePixelRatioF();
  k.reseau = m_reseau;
  return k;
}

// Le dessin fixe (feuille, fils, symboles, textes, sélection, surlignage, marques) vient de l'image gardée tant que
// rien ne l'a changé ; le survol, l'outil armé, le menu à marques et le liseré sont dessinés par-dessus (2.4, A10)
void VueSchema::paintEvent(QPaintEvent*) {
  prendreEncres(m_impression);
  const CleCache cle = cleCache();
  if (m_cache.isNull() || !(cle == m_cleCache)) {
    m_cache = QPixmap(size() * cle.dpr);
    m_cache.setDevicePixelRatio(cle.dpr);
    QPainter c(&m_cache);
    c.setRenderHint(QPainter::Antialiasing, Theme::anticrenelage());  // réglable (2.31)
    c.fillRect(rect(), HORS);
    peindre(c);
    m_cleCache = cle;
  }
  QPainter g(this);
  g.drawPixmap(0, 0, m_cache);
  g.setRenderHint(QPainter::Antialiasing);
  if (!m_impression) {
    dessinerSurvol(g);
    m_outils.actif()->dessiner(g);
    dessinerFilAriane(g);
  }
  if (const ConceptionElec* c = conception(); c && c->verrouillee) {
    const QString t = QStringLiteral("🔒 Publiée %1 · révision %2 · lecture seule").arg(QString::fromStdString(c->publications.empty() ? std::string() : c->publications.back().id),
                                                                                      QString::fromStdString(c->cartouche.revision));
    QFont f = font();
    f.setPixelSize(12);
    f.setBold(true);
    g.setFont(f);
    const QRectF r(10, 10, QFontMetricsF(f).horizontalAdvance(t) + 20, 26);
    g.setPen(Qt::NoPen);
    g.setBrush(QColor(0x10, 0x14, 0x2A, 225));
    g.drawRoundedRect(r, 13, 13);
    g.setPen(QColor(0x9F, 0xE8, 0xF8));
    g.drawText(r, Qt::AlignCenter, t);
  }
  m_nav->dessiner(g, m_souris);  // le menu à marques, l'annonce près du curseur
  if (!m_impression) majCadre();
  // Le canevas qui reçoit les touches porte un liseré de 2 px à la couleur d'accent (S21)
  if (hasFocus()) {
    g.setRenderHint(QPainter::Antialiasing, false);
    g.setPen(QPen(theme().c().accentTexte, 2));
    g.setBrush(Qt::NoBrush);
    g.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
  }
}

bool VueSchema::exporterPdf(const QString& chemin) {
  const ConceptionElec* c = conception();
  if (!c || c->feuilles.empty()) return false;
  // PDF/A-1b (8.29) si le projet le demande : polices incorporées et métadonnées XMP par QPdfWriter
  const auto itA = c->extensions.find("pdfA");
  const bool pdfA = itA != c->extensions.end() && itA->second.is_boolean() && itA->second.get<bool>();
  {
  QPdfWriter pdf(chemin);
  pdf.setResolution(300);
  pdf.setTitle(QString::fromStdString(c->cartouche.titre.empty() ? c->nom : c->cartouche.titre));
  pdf.setCreator(QStringLiteral("Prométhée"));
  if (pdfA) pdf.setPdfVersion(QPagedPaintDevice::PdfVersion_A1b);
  const int feuille = m_feuille;
  const double echelle = m_echelle;
  const QPointF origine = m_origine;
  const auto selection = m_selection;
  const std::string reseau = m_reseau;
  m_impression = true;
  m_selection.clear();
  m_reseau.clear();
  QPainter g;
  bool ok = true;
  for (std::size_t f = 0; f < c->feuilles.size() && ok; ++f) {
    const Boite b = cadreFeuille(c->feuilles[f].format);
    pdf.setPageSize(QPageSize(QSizeF(mm(b.largeur()), mm(b.hauteur())), QPageSize::Millimeter));
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
    if (f == 0) ok = g.begin(&pdf);
    else ok = pdf.newPage();
    if (!ok) break;
    g.setRenderHint(QPainter::Antialiasing);
    m_feuille = static_cast<int>(f);
    m_echelle = g.viewport().width() / mm(b.largeur());
    m_origine = QPointF(0, 0);
    peindre(g);
  }
  if (g.isActive()) g.end();
  m_impression = false;
  m_feuille = feuille;
  m_echelle = echelle;
  m_origine = origine;
  m_selection = selection;
  m_reseau = reseau;
  update();
  if (!ok) return false;
  }
  // Signet par feuille et liens entre étiquettes de même nom (8.11), ajoutés par une mise à jour incrémentale
  try {
    const std::string chemin8 = chemin.toStdString();
    const std::string avant = prom::lireFichier(prom::cheminFichier(chemin8));
    const std::string apres = completerPdf(avant, signetsSchema(*c), liensSchema(*c), pdfA ? std::nullopt : std::optional<MetaPdf>(metaDepuisCartouche(*c)));
    if (apres != avant) prom::ecrireFichierAtomique(prom::cheminFichier(chemin8), apres);
  } catch (const std::exception&) {
    // Le PDF de QPdfWriter reste, sans signets
  }
  return true;
}

int VueSchema::exporterDxf(const QString& chemin, int feuille) {
  const ConceptionElec* c = conception();
  if (!c || feuille < 0 || feuille >= static_cast<int>(c->feuilles.size())) return -1;
  const Boite b = cadreFeuille(c->feuilles[static_cast<std::size_t>(feuille)].format);
  PeintreDxf dxf(mm(b.largeur()), mm(b.hauteur()), 10);
  const int ancienne = m_feuille;
  const double echelle = m_echelle;
  const QPointF origine = m_origine;
  const auto selection = m_selection;
  const std::string reseau = m_reseau;
  m_impression = true;
  m_selection.clear();
  m_reseau.clear();
  m_feuille = feuille;
  m_echelle = dxf.pixelsParMm();
  m_origine = QPointF(0, 0);
  QPainter g;
  const bool ok = g.begin(&dxf);
  if (ok) {
    peindre(g);
    g.end();
  }
  m_impression = false;
  m_feuille = ancienne;
  m_echelle = echelle;
  m_origine = origine;
  m_selection = selection;
  m_reseau = reseau;
  update();
  if (!ok) return -1;
  QFile f(chemin);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return -1;
  const QByteArray d = dxf.document();
  return f.write(d) == d.size() ? dxf.entites() : -1;
}

void VueSchema::peindre(QPainter& g) {
  prendreEncres(m_impression);
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f) return;
  PeintreDxf::calque(g, QStringLiteral("CADRE"));
  dessinerFond(g, *f, *c);
  PeintreDxf::calque(g, QStringLiteral("DESSIN"));
  dessinerObjets034(g, *f, *c);
  PeintreDxf::calque(g, QStringLiteral("FILS"));
  dessinerFils(g, *f);
  PeintreDxf::calque(g, QStringLiteral("NUMEROS"));
  dessinerApresFils(g, *f);
  // Symboles
  PeintreDxf::calque(g, QStringLiteral("SYMBOLES"));
  for (const auto& s : f->symboles) {
    const DefSymbole* d = c->symbole(s.symbole);
    if (!d) continue;
    QString ref, val;
    textesDe(*f, s, *d, ref, val);
    // Non monté désaturé et barré d'une croix rouge ; exclu de la simulation grisé, avec son signe (3.17, STR-08)
    const ComposantElec* x = s.composant.empty() ? nullptr : c->composant(s.composant);
    const RegistreReglages& reg = RegistreReglages::instance();
    const bool barre = x && x->nePasMonter && reg.valeur(QStringLiteral("schema/marquerNonMontes")).toBool();
    const bool grise = x && x->horsSimulation && reg.valeur(QStringLiteral("schema/marquerHorsSimulation")).toBool();
    g.setOpacity(opaciteObjet(s.uuid) * (barre || grise ? 0.45 : 1.0));  // estompage de ce qui n'est pas choisi (2.26)
    dessinerSymbole(g, {m_origine, m_echelle}, *d, s, ref, val);
    g.setOpacity(1.0);
    if (barre || grise) {
      const Boite b = boiteSymbole(s, *d).gonflee(300'000);
      const QRectF r(ecran({b.x0, b.y0}), ecran({b.x1, b.y1}));
      g.save();
      if (barre) {
        g.setPen(QPen(QColor(reg.valeur(QStringLiteral("schema/couleurNonMontes")).toString()), std::max(1.5, 0.3 * m_echelle)));
        g.drawLine(r.topLeft(), r.bottomRight());
        g.drawLine(r.topRight(), r.bottomLeft());
      }
      if (grise) {
        // Signe « exclu de la simulation » : un cercle barré, en haut à droite
        const double t = std::max(6.0, 1.6 * m_echelle);
        const QRectF o(r.right() - t, r.top() - t / 2, t, t);
        g.setPen(QPen(QColor(reg.valeur(QStringLiteral("schema/couleurHorsSimulation")).toString()), std::max(1.0, 0.2 * m_echelle)));
        g.setBrush(Qt::NoBrush);
        g.drawEllipse(o);
        g.drawLine(o.bottomLeft(), o.topRight());
      }
      g.restore();
    }
    if (!m_impression && m_selection.count(s.uuid)) {
      const Boite b = boiteSymbole(s, *d).gonflee(600'000);
      signes::selectionBoite(g, QRectF(ecran({b.x0, b.y0}), ecran({b.x1, b.y1})), theme().signesSchema().sel);
    }
  }
  PeintreDxf::calque(g, QStringLiteral("RENVOIS"));
  dessinerApresSymboles(g, *f);
  dessinerRenvois034(g, *f, *c);
  // Étiquettes : locale soulignée, globale dans un cartouche pointu selon le sens du signal, connecteur hors feuille
  // à double chevron suivi de ses renvois (les feuilles où le même nom passe), qui s'ouvrent d'un clic (2.20)
  PeintreDxf::calque(g, QStringLiteral("ETIQUETTES"));
  if (!m_impression) m_zonesRenvoi.clear();
  for (const auto& e : f->etiquettes) {
    const QPointF p = ecran(e.p);
    const QString t = QString::fromStdString(e.texte);
    QFont fo = g.font();
    fo.setPixelSize(std::max(1, static_cast<int>(1.4 * m_echelle)));
    const double l = QFontMetricsF(fo).horizontalAdvance(t);
    const bool choisie = !m_impression && m_selection.count(e.uuid) > 0;
    const QColor c2 = e.type == TypeEtiquette::Globale ? GLOBALE : ENCRE;
    g.save();
    g.setOpacity(opaciteObjet(e.uuid));
    g.translate(p);
    // Tournée d'un demi-tour, l'étiquette pointe vers la gauche mais son texte reste à l'endroit
    const bool retournee = (e.rotation & 3) == 2;
    g.rotate(retournee ? 0.0 : -90.0 * e.rotation);
    const double sens = retournee ? -1.0 : 1.0;
    if (e.type == TypeEtiquette::Globale && domaine() != Domaine::Electronique) {
      const double h = 1.1 * m_echelle;
      QPolygonF forme;
      forme << QPointF(0, 0) << QPointF(sens * h, -h) << QPointF(sens * (h + l + 0.6 * m_echelle), -h) << QPointF(sens * (h + l + 0.6 * m_echelle), h) << QPointF(sens * h, h);
      g.setPen(QPen(c2, std::max(1.0, 0.18 * m_echelle)));
      g.setBrush(FOND_ETIQUETTE);
      g.drawPolygon(forme);
      texte(g, QPointF(retournee ? -(h + 0.3 * m_echelle + l) : h + 0.3 * m_echelle, 0), t, 1.4 * m_echelle, -1, c2);
      if (choisie) signes::selectionBoite(g, QRectF(QPointF(0, -h), QPointF(sens * (h + l + 0.6 * m_echelle), h)), theme().signesSchema().sel);
    } else if (e.type == TypeEtiquette::Globale || e.type == TypeEtiquette::HorsFeuille) {
      // Pointe côté connexion pour une entrée (et sans sens donné), à l'autre bout pour une sortie, aux deux pour un
      // signal bidirectionnel ou trois états, aucune pour un passif (SAI-11)
      const bool horsFeuille = e.type == TypeEtiquette::HorsFeuille;
      const double h = 1.1 * m_echelle, marge = horsFeuille ? 1.6 * m_echelle : 0.0, L = h + marge + l + 0.6 * m_echelle;
      const bool debut = e.sens == SensSignal::Aucun || e.sens == SensSignal::Entree || e.sens == SensSignal::Bidirectionnel || e.sens == SensSignal::TroisEtats;
      const bool fin = e.sens == SensSignal::Sortie || e.sens == SensSignal::Bidirectionnel || e.sens == SensSignal::TroisEtats;
      QPolygonF forme;
      if (debut) forme << QPointF(0, 0) << QPointF(sens * h, -h);
      else forme << QPointF(0, -h);
      if (fin) forme << QPointF(sens * L, -h) << QPointF(sens * (L + h), 0) << QPointF(sens * L, h);
      else forme << QPointF(sens * L, -h) << QPointF(sens * L, h);
      forme << (debut ? QPointF(sens * h, h) : QPointF(0, h));
      g.setPen(QPen(c2, std::max(1.0, 0.18 * m_echelle), horsFeuille ? Qt::DashLine : Qt::SolidLine));
      g.setBrush(FOND_ETIQUETTE);
      g.drawPolygon(forme);
      const double x0 = h + marge + 0.3 * m_echelle;
      if (horsFeuille) {
        // Double chevron, le sens du connecteur hors feuille vu de loin
        g.setPen(QPen(c2, std::max(1.0, 0.18 * m_echelle)));
        for (const double dx : {h + 0.2 * m_echelle, h + 0.8 * m_echelle}) {
          QPolygonF chevron;
          chevron << QPointF(sens * dx, -0.55 * h) << QPointF(sens * (dx + 0.5 * m_echelle), 0) << QPointF(sens * dx, 0.55 * h);
          g.drawPolyline(chevron);
        }
      }
      texte(g, QPointF(retournee ? -(x0 + l) : x0, 0), t, 1.4 * m_echelle, -1, c2);
      if (choisie) signes::selectionBoite(g, QRectF(QPointF(0, -h), QPointF(sens * (L + (fin ? h : 0)), h)), theme().signesSchema().sel);
      // Renvois : les autres feuilles où passe ce nom
      if (horsFeuille && !m_impression) {
        QStringList numeros;
        int premiere = -1;
        for (std::size_t i = 0; i < c->feuilles.size(); ++i) {
          if (static_cast<int>(i) == m_feuille) continue;
          bool ici = false;
          for (const auto& y : c->feuilles[i].etiquettes) ici |= y.texte == e.texte && y.type != TypeEtiquette::Locale;
          if (ici) {
            numeros << QString::number(i + 1);
            if (premiere < 0) premiere = static_cast<int>(i);
          }
        }
        if (!numeros.isEmpty()) {
          const QString renvoi = QStringLiteral("→ %1").arg(numeros.join(QStringLiteral(", ")));
          QFont fr = g.font();
          fr.setPixelSize(std::max(1, static_cast<int>(1.1 * m_echelle)));
          const double lr = QFontMetricsF(fr).horizontalAdvance(renvoi);
          const double xr = sens > 0 ? L + (fin ? h : 0) + 0.6 * m_echelle : -(L + (fin ? h : 0) + 0.6 * m_echelle + lr);
          texte(g, QPointF(xr, 0), renvoi, 1.1 * m_echelle, -1, GLOBALE);
          m_zonesRenvoi.push_back({g.transform().mapRect(QRectF(xr, -0.8 * m_echelle, lr, 1.6 * m_echelle)), premiere, e.texte});
        }
      }
    } else if (e.type == TypeEtiquette::Directive) {
      // Directive (7.14) : un fanion relié au fil, le texte en italique, sans nommer le réseau
      const double h = 2.4 * m_echelle;
      g.setPen(QPen(GLOBALE, std::max(1.0, 0.15 * m_echelle), Qt::DashLine));
      g.drawLine(QPointF(0, 0), QPointF(0, -h));
      QFont fi = g.font();
      fi.setItalic(true);
      fi.setPixelSize(std::max(1, static_cast<int>(1.2 * m_echelle)));
      g.setFont(fi);
      const double ld = QFontMetricsF(fi).horizontalAdvance(t) + 0.8 * m_echelle;
      const QRectF fanion(QPointF(0, -h - 1.6 * m_echelle), QSizeF(sens * ld, 1.6 * m_echelle));
      g.setPen(QPen(GLOBALE, std::max(1.0, 0.15 * m_echelle)));
      g.setBrush(FOND_ETIQUETTE);
      g.drawRect(fanion.normalized());
      g.drawText(fanion.normalized(), Qt::AlignCenter, t);
      if (choisie) signes::selectionBoite(g, fanion.normalized().adjusted(-2, -2, 2, 2), theme().signesSchema().sel);
    } else {
      texte(g, QPointF(retournee ? -(0.3 * m_echelle + l) : 0.3 * m_echelle, -0.9 * m_echelle), t, 1.4 * m_echelle, -1, c2);
      g.setPen(QPen(c2, std::max(1.0, 0.15 * m_echelle)));
      g.drawLine(QPointF(0, 0), QPointF(sens * (l + 0.6 * m_echelle), 0));
      if (choisie) signes::selectionBoite(g, QRectF(QPointF(0, -1.7 * m_echelle), QPointF(sens * (l + 0.6 * m_echelle), 0.3 * m_echelle)), theme().signesSchema().sel);
    }
    g.restore();
  }
  PeintreDxf::calque(g, QStringLiteral("TEXTES"));
  for (const auto& t : f->textes) {
    g.save();
    g.setOpacity(opaciteObjet(t.uuid));
    g.translate(ecran(t.p));
    g.rotate(-90.0 * t.rotation);
    const double taille = mm(t.taille) * m_echelle;
    // Calcul tenu à jour entre « {= » et « } » (3.14) : « fc = {=1/(2π·R3·C5)} » montre « fc = 1,59 kHz » ; puis
    // variables, expressions et styles (7.6, 7.11)
    const std::string brut = t.texte.find("{=") != std::string::npos ? texteCalcule(*c, t.texte) : t.texte;
    const QString montre = QString::fromStdString(texteAffiche(brut, *c, m_feuille));
    texteRiche(g, QPointF(0, 0), *c, brut, taille, ENCRE, texteAncreAGauche() ? -1 : 0);
    if (!m_impression && m_selection.count(t.uuid)) {
      QFont fo = g.font();
      fo.setPixelSize(std::max(1, static_cast<int>(taille)));
      const double l = QFontMetricsF(fo).horizontalAdvance(montre);
      const double x0 = texteAncreAGauche() ? 0 : -l / 2;
      signes::selectionBoite(g, QRectF(x0 - 2, -taille / 2 - 2, l + 4, taille + 4), theme().signesSchema().sel);
    }
    g.restore();
  }
  // Réseau surligné : le reste s'éclaircit, ses fils et ses broches prennent un halo (S5, charte)
  if (!m_impression && !m_reseau.empty()) dessinerSurlignage(g, *f, *c);
  // Problèmes de la vérification électrique
  if (m_impression) return;
  const SignesCanevas& sg = theme().signesSchema();
  for (const MarqueErc& m : marquesErc()) {
    if (m.genre == GenreMarque::Dessin) {
      // Niveau du dessin (S9) : un petit cercle gris au bout de la broche ou du fil, rien d'autre
      g.save();
      g.setPen(QPen(sg.perime, 1));
      g.setBrush(Qt::NoBrush);
      g.drawEllipse(m.p, 3.0, 3.0);
      g.restore();
      continue;
    }
    const bool erreur = m.genre == GenreMarque::Erreur;
    g.save();
    if (m.perimee) g.setOpacity(0.55);
    signes::marque(g, m.p, erreur ? signes::Marque::Erreur : signes::Marque::Avertissement, erreur ? sg.err : sg.avert);
    if (m.perimee) {
      // « À refaire » : la marque hachurée, le schéma a changé depuis la vérification
      g.setOpacity(1);
      g.setPen(Qt::NoPen);
      g.setBrush(QBrush(sg.perime, Qt::BDiagPattern));
      g.drawEllipse(m.p, 6.0, 6.0);
    }
    g.restore();
  }
}

std::vector<VueSchema::MarqueErc> VueSchema::marquesErc() const {
  std::vector<MarqueErc> r;
  const Feuille* f = feuilleCourante();
  if (!f) return r;
  auto ajouter = [&](const Violation& v, bool dessin, bool perimee) {
    if (v.feuille != f->uuid || (v.position.x == 0 && v.position.y == 0)) return;
    const QPointF p = ecran(v.position);
    if (dessin) r.push_back({p, GenreMarque::Dessin, false, &v});
    else r.push_back({p + QPointF(5, -5), v.gravite == Gravite::Erreur ? GenreMarque::Erreur : GenreMarque::Avertissement, perimee, &v});
  };
  if (m_violations)
    for (const auto& v : *m_violations) {
      if (!m_niveauxErc || v.niveau == NiveauErc::Immediat) ajouter(v, false, false);
      else if (v.niveau == NiveauErc::Dessin) ajouter(v, true, false);
    }
  if (m_niveauxErc && m_verification)
    for (const auto& v : *m_verification)
      if (v.niveau == NiveauErc::Complet) ajouter(v, false, m_verificationPerimee);
  return r;
}

// Contour fin de l'objet sous la souris (S6, SAI-19) : ce qu'un clic prendrait
void VueSchema::dessinerSurvol(QPainter& g) {
  if (m_survol.empty() || outil() != Outil::Selection || m_outils.actif()->geste()) return;
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f) return;
  QColor teinte = theme().schema().choix;
  teinte.setAlphaF(0.75);
  g.setPen(QPen(teinte, 1, Qt::DashLine));
  g.setBrush(Qt::NoBrush);
  for (const auto& s : f->symboles)
    if (s.uuid == m_survol)
      if (const DefSymbole* d = c->symbole(s.symbole)) {
        const Boite b = boiteSymbole(s, *d).gonflee(400'000);
        g.drawRect(QRectF(ecran({b.x0, b.y0}), ecran({b.x1, b.y1})));
        return;
      }
  for (const auto& w : f->fils)
    if (w.uuid == m_survol) {
      QColor voile = teinte;
      voile.setAlphaF(0.25);
      g.setPen(QPen(voile, std::max(5.0, 0.9 * m_echelle), Qt::SolidLine, Qt::RoundCap));
      g.drawLine(ecran(w.a), ecran(w.b));
      return;
    }
  for (const auto& e : f->etiquettes)
    if (e.uuid == m_survol) {
      const double l = (static_cast<double>(e.texte.size()) * 1.1 + 1.5) * m_echelle;
      g.drawRect(QRectF(ecran(e.p) - QPointF(2, 1.4 * m_echelle), QSizeF(l, 2.6 * m_echelle)));
      return;
    }
  for (const auto& t : f->textes)
    if (t.uuid == m_survol) {
      g.drawEllipse(ecran(t.p), 0.6 * m_echelle, 0.6 * m_echelle);
      return;
    }
}

// Infobulle de ce qui est sous q (SAI-19, S28) : une marque ERC dit son message et son conseil ; un symbole, son
// repère, sa valeur, son empreinte et le réseau de la broche visée ; un fil, son réseau et les feuilles où il passe
QString VueSchema::texteSurvol(QPointF q) const {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f) return {};
  auto echapper = [](const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); };
  // Une marque parle (S28) : le problème, ce qu'il faut faire, et ce qu'est la règle
  for (const MarqueErc& m : marquesErc()) {
    if (std::hypot(m.p.x() - q.x(), m.p.y() - q.y()) > 8) continue;
    const Violation& v = *m.violation;
    QString t = QStringLiteral("<b>%1</b> · %2<br>%3").arg(QString::fromStdString(v.code), echapper(v.message), echapper(v.conseil));
    if (const QString quoi = aide::explicationRegle(v.code); !quoi.isEmpty())
      t += QStringLiteral("<br><span style='color:%1'>%2 : %3</span>").arg(theme().c().texteSecondaire.name(), aide::titreRegle(v.code).toHtmlEscaped(), quoi.toHtmlEscaped());
    if (m.perimee) t += QStringLiteral("<br><i>À refaire : le schéma a changé depuis la vérification (F7).</i>");
    return t;
  }
  const std::string o = objetSous(q);
  if (o.empty()) return {};
  // Les feuilles où passe un réseau
  auto feuillesDu = [&](const std::string& reseau) {
    QStringList noms;
    if (!m_k) return noms;
    for (std::size_t i = 0; i < c->feuilles.size(); ++i) {
      bool ici = false;
      for (const auto& w : c->feuilles[i].fils)
        if (const auto it = m_k->parFil.find(w.uuid); it != m_k->parFil.end() && m_k->reseaux[static_cast<std::size_t>(it->second)].nom == reseau) ici = true;
      if (ici) noms << QString::fromStdString(c->feuilles[i].nom);
    }
    return noms;
  };
  for (const auto& s : f->symboles) {
    if (s.uuid != o) continue;
    const DefSymbole* d = c->symbole(s.symbole);
    const ComposantElec* k = c->composant(s.composant);
    if (!d) return {};
    if (d->alimentation) return QStringLiteral("Alimentation <b>%1</b>").arg(echapper(s.nomAlim.empty() ? d->nom : s.nomAlim));
    // La description écrite une fois (2.30), le repère en gras
    QString t = descriptionObjet(s.uuid).toHtmlEscaped();
    if (k) {
      const QString ref = QString::fromStdString(k->reference + (d->unites > 1 ? lettreUnite(s.unite) : std::string())).toHtmlEscaped();
      if (t.startsWith(ref)) t = QStringLiteral("<b>%1</b>").arg(ref) + t.mid(ref.size());
    }
    if (k && !k->empreinte.empty()) t += QStringLiteral(" · %1").arg(echapper(k->empreinte));
    // La broche visée, à moins de 6 pixels de son extrémité
    for (const auto& b : brochesPlacees(s, *d)) {
      const QPointF p = ecran(b.p);
      if (std::hypot(p.x() - q.x(), p.y() - q.y()) > 6) continue;
      const std::string reseau = m_k && k ? m_k->nom(k->uuid, b.numero) : std::string();
      t += QStringLiteral("<br>broche %1%2 · %3").arg(echapper(b.numero), b.broche->nom.empty() || b.broche->nom == "~" ? QString() : QStringLiteral(" (%1)").arg(echapper(b.broche->nom)),
                                                     reseau.empty() ? QStringLiteral("non reliée") : QStringLiteral("réseau %1").arg(echapper(reseau)));
      break;
    }
    return t;
  }
  const std::string reseau = reseauDe(o);
  for (const auto& w : f->fils)
    if (w.uuid == o) {
      const QStringList feuilles = feuillesDu(reseau);
      return QStringLiteral("Fil · réseau <b>%1</b>%2").arg(echapper(reseau.empty() ? std::string("?") : reseau),
                                                           feuilles.size() > 1 ? QStringLiteral("<br>passe sur : %1").arg(feuilles.join(QStringLiteral(", "))) : QString());
    }
  return libelleObjet(o).toHtmlEscaped();
}

// ---------- Souris et clavier ----------
// La vue garde la navigation (bouton du milieu, clic droit glissé, molette) ; le reste va à l'outil armé (1.6)
void VueSchema::mousePressEvent(QMouseEvent* e) {
  m_outils.recycler();
  setFocus();
  m_souris = e->position();
  m_pression = e->position();
  // Boutons du milieu et de droite, bouton des gestes : la navigation commune (1.19)
  if (m_nav->appui(e)) {
    appliquerCurseur();
    return;
  }
  if (e->button() != Qt::LeftButton) return;
  if (ouvrirAriane(e->position())) return;  // fil d'Ariane de la hiérarchie (7.2)
  // L'origine des coordonnées relatives, posée d'un clic (2.28)
  if (m_armeOrigine) {
    m_armeOrigine = false;
    m_origineRelative = accroche(e->position());
    appliquerCurseur();
    emit coordonneesChangees();
    emit message(QStringLiteral("Origine relative posée : la barre d’état mesure depuis ce point."));
    update();
    return;
  }
  if (outil() == Outil::Selection && !(e->modifiers() & Qt::AltModifier)) m_appuiLong->commencer(e->position());
  m_outils.actif()->appui(geste(e));
  majBandeau();
  appliquerCurseur();
}

void VueSchema::mouseMoveEvent(QMouseEvent* e) {
  m_souris = e->position();
  m_relatifFige.clear();
  emit coordonneesChangees();
  if (m_nav->deplacer(e)) {
    update();
    return;
  }
  m_outils.recycler();
  m_appuiLong->bouger(e->position());
  if (e->buttons() & Qt::LeftButton) m_outils.actif()->glisser(geste(e));
  else m_outils.actif()->survol(geste(e));
  // Survol (S6) : seul le contour change, le dessin fixe vient de l'image gardée
  const std::string survol = outil() == Outil::Selection && !(e->buttons() & Qt::LeftButton) ? objetSous(e->position()) : std::string();
  const std::string avant = std::exchange(m_survol, survol);
  // Pendant un glisser, le pointeur au bord fait défiler la vue
  m_nav->glisserGauche(e->position(), (e->buttons() & Qt::LeftButton) && m_outils.actif()->geste());
  if (m_outils.actif()->geste()) majBandeau();
  appliquerCurseur();
  // Survol avec Sélection, sans geste, menu ni annonce : seuls l'ancien et le nouveau contour se repeignent (A10)
  if (outil() == Outil::Selection && !e->buttons() && !m_outils.actif()->geste() && !m_outils.suspendus() && !m_nav->menuVisible() && !m_nav->annonceVisible()) {
    if (avant != m_survol) update(zoneSurvol(avant).united(zoneSurvol(m_survol)));
    return;
  }
  update();
}

QRect VueSchema::zoneSurvol(const std::string& objet) const {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (objet.empty() || !c || !f) return {};
  QRectF r;
  for (const auto& s : f->symboles)
    if (s.uuid == objet)
      if (const DefSymbole* d = c->symbole(s.symbole)) {
        const Boite b = boiteSymbole(s, *d).gonflee(400'000);
        r = QRectF(ecran({b.x0, b.y0}), ecran({b.x1, b.y1})).normalized();
      }
  for (const auto& w : f->fils)
    if (w.uuid == objet) {
      const double e = std::max(5.0, 0.9 * m_echelle);
      r = QRectF(ecran(w.a), ecran(w.b)).normalized().adjusted(-e, -e, e, e);
    }
  for (const auto& x : f->etiquettes)
    if (x.uuid == objet) r = QRectF(ecran(x.p) - QPointF(2, 1.4 * m_echelle), QSizeF((static_cast<double>(x.texte.size()) * 1.1 + 1.5) * m_echelle, 2.6 * m_echelle));
  for (const auto& t : f->textes)
    if (t.uuid == objet) r = QRectF(ecran(t.p) - QPointF(0.6, 0.6) * m_echelle, QSizeF(1.2, 1.2) * m_echelle);
  return r.toAlignedRect().adjusted(-4, -4, 4, 4);
}

void VueSchema::mouseReleaseEvent(QMouseEvent* e) {
  switch (m_nav->relacher(e)) {
    case NavigationVue::Fin::ClicDroitCourt:
      // Clic droit sans glisser : le menu de l'objet visé, du vide, ou de l'outil armé (2.5, S7)
      menuContextuel(e->position());
      majBandeau();
      appliquerCurseur();
      return;
    case NavigationVue::Fin::Pris:
      appliquerCurseur();
      update();
      return;
    case NavigationVue::Fin::Rien: break;
  }
  if (e->button() != Qt::LeftButton) return;
  m_appuiLong->arreter();
  m_nav->glisserGauche(e->position(), false);
  m_outils.recycler();
  m_outils.actif()->relacher(geste(e));
  majBandeau();
  appliquerCurseur();
  update();
}

void VueSchema::mouseDoubleClickEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  if (outil() == Outil::Selection && entrerDansBloc(e->position())) return;  // la feuille fille du bloc (7.2)
  m_outils.recycler();
  m_outils.actif()->doubleClic(geste(e));
  majBandeau();
  update();
}

void VueSchema::wheelEvent(QWheelEvent* e) {
  m_nav->molette(e, m_origine, m_echelle, 0.4, 80.0);
  update();
}

// Une frappe désigne des actions du registre ; l'outil armé passe d'abord (R d'une pose, / d'un fil, flèches et Suppr
// de la sélection), puis la vue. Une frappe que rien ne prend remonte à l'atelier.
void VueSchema::keyPressEvent(QKeyEvent* e) {
  m_outils.recycler();
  // Pendant un tracé de fil, un chiffre ouvre la longueur et l'angle près du curseur (2.28, ERG-26)
  if (outil() == Outil::Fil && m_outils.actif()->geste() && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) && e->text().size() == 1 &&
      QStringLiteral("0123456789,.").contains(e->text().front())) {
    ouvrirSaisieTrace(e->text());
    return;
  }
  const RegistreActions& r = RegistreActions::atelierElectronique();
  std::vector<const ActionAtelier*> actions;
  // Suite en attente : la seconde touche la complète ; sinon elle vaut seule (ERG-03)
  if (const auto premiere = m_nav->prefixe()) {
    actions = r.pourSuite(PorteeAction::Schema, *premiere, e->keyCombination());
    m_nav->finirSuite();
  }
  if (actions.empty()) actions = r.pour(PorteeAction::Schema, *e);
  if (toucheAction(actions)) return;
  // Première touche d'une suite : elle attend la seconde, les suites possibles annoncées près du curseur
  if (const auto suites = r.suites(PorteeAction::Schema, e->keyCombination()); !suites.empty()) {
    m_nav->attendreSuite(e->keyCombination(), NavigationVue::texteSuites(e->keyCombination(), suites));
    return;
  }
  QWidget::keyPressEvent(e);
}

// Une frappe désigne ces actions : une touche qui ouvre un menu l'ouvre au curseur ; l'outil armé passe d'abord ; avec
// le réglage « Premier appui : agir au curseur », une touche d'outil agit là où est le curseur ; puis la vue. Une
// bascule faite au clavier s'annonce près du curseur (ERG-03).
bool VueSchema::toucheAction(const std::vector<const ActionAtelier*>& actions) {
  for (const ActionAtelier* a : actions)
    if (!a->menu.empty()) {
      ouvrirMenuTouche(*a);
      return true;
    }
  for (const ActionAtelier* a : actions)
    if (m_outils.actif()->agir(a->identifiant, true)) {
      majBandeau();
      if (a->bascule) annoncerBascule(*a);
      return true;
    }
  if (premierAppui() == PremierAppui::AgirAuCurseur && rect().contains(m_souris.toPoint()))
    for (const ActionAtelier* a : actions)
      if (a->auCurseur && agirAuCurseur(a->identifiant)) return true;
  for (const ActionAtelier* a : actions)
    if (agirVue(a->identifiant, true)) {
      if (a->bascule) annoncerBascule(*a);
      return true;
    }
  return false;
}

void VueSchema::annoncerBascule(const ActionAtelier& a) {
  QString texte = a.nomAffiche();
  for (const ActionBandeau& x : m_outils.actif()->bandeau().actions)
    if (x.action == a.identifiant) texte = x.libelle;
  m_nav->annoncer(texte);
}

void VueSchema::ouvrirMenuTouche(const ActionAtelier& a) {
  const RegistreActions& r = RegistreActions::atelierElectronique();
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("menuTouche"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  menu->addSection(a.nomAffiche());
  for (const std::string& id : a.menu)
    if (const ActionAtelier* x = r.action(id)) {
      const QString touche = r.touches(id).section(QStringLiteral(", "), 0, 0);
      QAction* ligne = menu->addAction(icone(x->icone), touche.isEmpty() ? x->nomAffiche() : x->nomAffiche() + QLatin1Char('\t') + touche);
      marquerAction(ligne, id);
      connect(ligne, &QAction::triggered, this, [this, id] { emit actionDemandee(QString::fromStdString(id)); });
    }
  menu->popup(mapToGlobal(m_souris.toPoint()));
}

bool VueSchema::ouvrirRenvoi(QPointF q) {
  for (const ZoneRenvoi& z : m_zonesRenvoi) {
    if (!z.zone.contains(q) || z.feuille < 0) continue;
    const std::string nom = z.nom;
    setConception(m_conception, z.feuille);
    if (const Feuille* f = feuilleCourante())
      for (const auto& e : f->etiquettes)
        if (e.texte == nom && e.type != TypeEtiquette::Locale) {
          selectionner({e.uuid});
          centrerSur(e.p);
          break;
        }
    emit message(QStringLiteral("Feuille %1 : le renvoi de <b>%2</b>.").arg(z.feuille + 1).arg(QString::fromStdString(nom).toHtmlEscaped()));
    return true;
  }
  return false;
}

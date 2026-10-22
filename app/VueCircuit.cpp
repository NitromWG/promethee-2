// Prométhée : éditeur de circuit imprimé (canevas). Licence GPL-3.0-only.
#include "VueCircuit.hpp"

#include "Theme.hpp"

#include <algorithm>
#include <limits>
#include <cmath>
#include <cstdio>
#include <set>

#include <QAbstractSpinBox>
#include <QApplication>
#include <QEnterEvent>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QInputDialog>
#include <QMenu>
#include <QNativeGestureEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QLineF>
#include <map>

#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QTimer>
#include <QWheelEvent>

#include "BandeauOutil.hpp"
#include "CartoucheRoutage.hpp"
#include "GestesSelection.hpp"
#include "GlisserArticle.hpp"
#include "Icones.hpp"
#include "NavigationVue.hpp"
#include "Document.hpp"
#include "EncresCircuit.hpp"
#include "RegistreActions.hpp"
#include "SelectionElectronique.hpp"
#include "SignesCanevas.hpp"
#include "promethee/commandes.hpp"
#include "promethee/dessins_circuit.hpp"
#include "promethee/placement_circuit.hpp"
#include "promethee/routeur.hpp"
#include "promethee/modele.hpp"
#include "promethee/police.hpp"

using namespace prom::ecad;

using namespace encres_circuit;

namespace {
QPainterPath chemin(const Polygones& ps, const std::function<QPointF(Pt)>& e) {
  QPainterPath p;
  p.setFillRule(Qt::OddEvenFill);
  for (const auto& poly : ps) {
    if (poly.size() < 3) continue;
    p.moveTo(e(poly.front()));
    for (std::size_t i = 1; i < poly.size(); ++i) p.lineTo(e(poly[i]));
    p.closeSubpath();
  }
  return p;
}
}  // namespace

VueCircuit::VueCircuit(Document* doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(300, 200);
  setAcceptDrops(true);
  m_visibles.fill(true);
  m_visibles[static_cast<std::size_t>(Couche::EncombrementDessus)] = false;
  m_visibles[static_cast<std::size_t>(Couche::EncombrementDessous)] = false;
  setSelectionPartagee(nullptr);
  m_bandeau = new BandeauOutil(this);
  m_cartouche = new CartoucheRoutage(*this);
  // Navigation commune (1.19) : molette, pavé, boutons du milieu et de droite, menu à marques
  m_nav = new NavigationVue(this, NavigationVue::secteursOrigine("circuit.changerFace"), [this](const std::string& a) { executerGeste(a); }, [this](QPointF d) {
    m_origine += d;
    if (QGuiApplication::mouseButtons() & Qt::LeftButton) {
      m_outils.actif()->glisser(GesteVue{m_souris, Qt::NoButton, Qt::LeftButton, QGuiApplication::keyboardModifiers()});
      majBandeau();
    }
    update();
  });
  // Appui long immobile de l'outil Sélection sur des objets superposés : leur liste, comme Alt+clic (SAI-18)
  m_appuiLong = new AppuiLong(this, [this](QPointF p) {
    if (outil() == Outil::Selection && objetsSous(p, true).size() >= 2) listerObjetsSous(p);
  });
  connect(m_bandeau, &BandeauOutil::actionFaite, this, [this] {
    majBandeau();
    update();
  });
  m_outils.armer(creerOutil(Outil::Selection));
  connect(m_doc, &Document::change, this, [this] {
    ++m_generationDoc;
    if (m_sel == m_selPropre)
      if (const ConceptionElec* c = conception()) m_sel->filtrer(*c);
    if (majSelectionVue()) emit selectionChangee();
    update();
  });
  connect(m_doc, &Document::conceptionChangee, this, [this](const ChangementConception& ch) {
    const ConceptionElec* c = conception();
    if (c && (ch.conception.empty() || ch.conception == c->uuid)) {
      ++m_generationDoc;
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

const ConceptionElec* VueCircuit::conception() const {
  const auto& p = m_doc->projet();
  return m_conception >= 0 && m_conception < static_cast<int>(p.electronique.size()) ? &p.electronique[static_cast<std::size_t>(m_conception)] : nullptr;
}

void VueCircuit::setConception(int i) {
  if (i != m_conception) {
    m_conception = i;
    if (m_sel == m_selPropre) m_sel->vider();
    majSelectionVue();
    m_outils.actif()->annulerTrace();  // un tracé commencé sur une autre carte ne se pose pas sur celle-ci
    recadrer();
  }
  // Couche active absente de l'empilage (carte simple face : cuivre dessous seulement)
  if (const ConceptionElec* c = conception(); c && !(bit(m_coucheActive) & tousLesCuivres(c->circuit.empilage.couchesCuivre)))
    m_coucheActive = couchesCuivre(c->circuit.empilage.couchesCuivre).back();
  update();
}

bool VueCircuit::refuseVerrou() {
  const ConceptionElec* c = conception();
  if (!c || !c->verrouillee) return false;
  emit message(QStringLiteral("🔒 <b>%1 est publiée</b> (%2) : lecture seule. Pour la modifier, passe à une nouvelle révision dans l’onglet Fabrication.")
                   .arg(QString::fromStdString(c->nom).toHtmlEscaped(), QString::fromStdString(c->publications.empty() ? std::string("verrouillée") : c->publications.back().id)));
  return true;
}

bool VueCircuit::event(QEvent* e) {
  // Pincer sur le pavé tactile : zoomer
  if (e->type() == QEvent::NativeGesture && m_nav->pincer(static_cast<QNativeGestureEvent*>(e), m_origine, m_echelle, 1.0, 400.0)) {
    update();
    return true;
  }
  // Le canevas ne garde que les touches d'une action qu'il ferait maintenant (registre, 1.8) : F8, F1 et les
  // raccourcis de la fenêtre lui passent au-dessus (S22) ; Ctrl+Z n'est gardé que pendant un tracé (C14)
  if (e->type() == QEvent::ShortcutOverride) {
    auto* k = static_cast<QKeyEvent*>(e);
    const RegistreActions& r = RegistreActions::atelierElectronique();
    // Une suite en attente garde la touche suivante ; une première touche de suite est gardée aussi (ERG-03)
    if (m_nav->prefixe() || !r.suites(PorteeAction::Circuit, k->keyCombination()).empty()) {
      e->accept();
      return true;
    }
    for (const ActionAtelier* a : r.pour(PorteeAction::Circuit, *k))
      if (!a->menu.empty() || actionPossible(a->identifiant)) {
        e->accept();
        return true;
      }
  }
  return QWidget::event(e);
}

bool VueCircuit::outilPrend(std::string_view action) { return m_outils.actif() && m_outils.actif()->agir(action, false); }
bool VueCircuit::actionPossible(std::string_view action) { return outilPrend(action) || agirVue(action, false); }

bool VueCircuit::executerAction(std::string_view action) {
  m_outils.recycler();
  if (m_outils.actif() && m_outils.actif()->agir(action, true)) {
    majBandeau();
    return true;
  }
  return agirVue(action, true);
}

QString VueCircuit::nomOutil() const {
  const ContenuBandeau b = m_outils.actif() ? m_outils.actif()->bandeau() : ContenuBandeau{};
  return b.vide() || outil() == Outil::Selection ? QStringLiteral("Sélection") : b.titre;
}

// Actions de la vue : armer un outil, la couche active, la vue, le presse-papiers, l'état neutre
bool VueCircuit::agirVue(std::string_view a, bool faire) {
  if (a == "commun.echap") {
    if (faire) echap();
  } else if (a == "circuit.router" || a == "circuit.via") {
    if (faire) setOutil(a == "circuit.router" ? Outil::Router : Outil::Via);
  } else if (a == "circuit.coucheDessus" || a == "circuit.coucheDessous") {
    if (faire) {
      setCoucheActive(a == "circuit.coucheDessus" ? Couche::CuivreDessus : Couche::CuivreDessous);
      emit coucheActiveChangee();
    }
  } else if (a == "edition.copier") {
    if (faire) copier();
  } else if (a == "edition.couper") {
    if (faire) {
      copier();
      supprimerChoix();
    }
  } else if (a == "edition.coller") {
    if (faire) coller();
  } else if (a == "edition.dupliquer") {
    if (faire) dupliquer();
  } else if (a == "selection.tout") {
    if (faire) toutChoisir();
  } else if (a == "vue.toutVoir") {
    if (faire) recadrer();
  } else if (a == "vue.zoomPlus" || a == "vue.zoomMoins") {
    // Le cran de la molette, réglable (1.19)
    const double f = Navigation::instance().reglages().zoomCran;
    if (faire) zoomer(a == "vue.zoomPlus" ? f : 1 / f);
  } else if (a == "vue.zoomSelection") {
    if (faire) zoomerSurSelection();
  } else if (a == "commun.inspecteur") {
    if (faire) emit inspecteurDemande();
  } else
    return false;
  return true;
}

void VueCircuit::zoomer(double facteur) {
  const double nouvelle = std::clamp(m_echelle * facteur, 1.0, 400.0);
  const QPointF p(width() / 2.0, height() / 2.0);
  m_origine = p - (p - m_origine) * (nouvelle / m_echelle);
  m_echelle = nouvelle;
  update();
}

void VueCircuit::enterEvent(QEnterEvent* e) {
  QWidget::enterEvent(e);
  // Pas pendant une saisie : un champ qui a le focus le garde (S21)
  const QWidget* f = QApplication::focusWidget();
  const bool saisie = f && (qobject_cast<const QLineEdit*>(f) || qobject_cast<const QAbstractSpinBox*>(f) || qobject_cast<const QTextEdit*>(f) || qobject_cast<const QPlainTextEdit*>(f));
  if (!saisie && isActiveWindow() && focusPolicy() != Qt::NoFocus) setFocus(Qt::MouseFocusReason);
}
void VueCircuit::focusInEvent(QFocusEvent* e) {
  QWidget::focusInEvent(e);
  update();
}
void VueCircuit::focusOutEvent(QFocusEvent* e) {
  QWidget::focusOutEvent(e);
  update();
}

void VueCircuit::modifier(const std::function<void(ConceptionElec&)>& f, const QString& fusion) {
  if (refuseVerrou()) return;
  m_doc->modifierConception(m_conception, [&](ConceptionElec& c) {
    f(c);
    // Le cuivre posé ou modifié porte le nom actuel de son réseau, et ce nom est figé (EL-01)
    synchroniserReseaux(c);
  }, {}, fusion);
  // Une commande imbriquée a fait son pas : la piste suspendue reprend (après l'appel, l'outil n'est pas détruit
  // sous lui-même)
  if (m_outils.suspendus()) QTimer::singleShot(0, this, [this] {
      if (m_outils.suspendus()) reprendreTrace();
    });
}

// ---------- Article mécanique glissé (2.2) ----------
void VueCircuit::suivreArticle(const DefSymbole& d, QPointF q) {
  if (!m_article || m_article->id != d.id) {
    m_article = d;
    m_rotationArticle = 0;
  }
  m_souris = q;
  m_posArticle = surGrille(monde(q), pasPlacement());
  update();
}

void VueCircuit::tournerArticle(bool inverse) {
  if (!m_article) return;
  m_rotationArticle = std::fmod(m_rotationArticle + (inverse ? 270 : 90), 360.0);
  update();
}

bool VueCircuit::poserArticle(QPointF q) {
  if (!m_article) return false;
  const DefSymbole d = *m_article;
  const Pt p = surGrille(monde(q), pasPlacement());
  const double rotation = m_rotationArticle;
  m_article.reset();
  std::string pose;
  modifier([&](ConceptionElec& c) {
    if (!c.symbole(d.id)) c.symboles[d.id] = d;
    pose = poserArticleCircuit(c, d.id, p, rotation);
  });
  update();
  if (pose.empty()) return false;
  choisir(Genre::Empreinte, pose);
  if (const ConceptionElec* c = conception())
    if (const ComposantElec* k = c->composant(pose)) emit message(QStringLiteral("%1 posé sur la carte ; son symbole est entré au schéma, sur la feuille « Mécanique ».").arg(QString::fromStdString(k->reference)));
  return true;
}

void VueCircuit::refuserArticle(const DefSymbole& d) {
  emit message(QStringLiteral("<b>%1</b> se pose d’abord au schéma : ses broches doivent y être reliées. Le circuit le reçoit à la mise à jour (F8).").arg(QString::fromStdString(d.nom).toHtmlEscaped()));
}

void VueCircuit::abandonnerArticle() {
  m_article.reset();
  update();
}

void VueCircuit::dragEnterEvent(QDragEnterEvent* e) {
  if (e->mimeData()->hasFormat(QStringLiteral("application/x-promethee-aplacer"))) {
    if (!refuseVerrou()) e->acceptProposedAction();
    return;
  }
  const auto d = GlisserArticle::article(e->mimeData());
  if (!d || !d->mecanique || refuseVerrou()) return;
  suivreArticle(*d, e->position());
  e->acceptProposedAction();
}
void VueCircuit::dragMoveEvent(QDragMoveEvent* e) {
  if (e->mimeData()->hasFormat(QStringLiteral("application/x-promethee-aplacer"))) {
    e->acceptProposedAction();
    return;
  }
  if (const auto d = GlisserArticle::article(e->mimeData()); d && d->mecanique) {
    suivreArticle(*d, e->position());
    e->acceptProposedAction();
  }
}
void VueCircuit::dragLeaveEvent(QDragLeaveEvent*) { abandonnerArticle(); }
void VueCircuit::dropEvent(QDropEvent* e) {
  if (e->mimeData()->hasFormat(QStringLiteral("application/x-promethee-aplacer"))) {
    std::vector<std::string> ids;
    for (const QString& id : QString::fromUtf8(e->mimeData()->data(QStringLiteral("application/x-promethee-aplacer"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) ids.push_back(id.toStdString());
    if (poserGroupe(ids, monde(e->position())) > 0) e->acceptProposedAction();
    return;
  }
  if (const auto d = GlisserArticle::article(e->mimeData()); d && d->mecanique) {
    suivreArticle(*d, e->position());
    if (poserArticle(e->position())) e->acceptProposedAction();
  }
}

QPointF VueCircuit::ecran(Pt p) const { return m_origine + QPointF(mm(p.x) * m_echelle, mm(p.y) * m_echelle); }
Pt VueCircuit::monde(QPointF q) const { return ptMm((q.x() - m_origine.x()) / m_echelle, (q.y() - m_origine.y()) / m_echelle); }
Pt VueCircuit::surGrilleRoutage(Pt p) const { return surGrille(p, pasRoutage()); }

// Grille réglable (EL-37) : 0 garde les pas automatiques de 0,1 mm au placement et 0,05 mm au routage
Nm VueCircuit::pasPlacement() const {
  const ConceptionElec* c = conception();
  return c && c->circuit.affichage.grille > 0 ? c->circuit.affichage.grille : 100'000;
}
Nm VueCircuit::pasRoutage() const {
  const ConceptionElec* c = conception();
  return c && c->circuit.affichage.grille > 0 ? c->circuit.affichage.grille : 50'000;
}
QString VueCircuit::longueur(double v) const {
  const ConceptionElec* c = conception();
  const std::string u = c ? c->circuit.affichage.unite : std::string("mm");
  if (u == "mil") return QLocale().toString(v / 25'400.0, 'f', 1) + QStringLiteral(" mil");
  if (u == "in") return QLocale().toString(v / 25'400'000.0, 'f', 4) + QStringLiteral(" in");
  return QLocale().toString(v * 1e-6, 'f', 3) + QStringLiteral(" mm");
}

void VueCircuit::setOutil(Outil o) {
  const OutilVue* actif = m_outils.actif();
  if (m_outils.suspendus()) {
    // Une commande imbriquée : Sélection ou Router reprennent la piste ; un autre outil prend sa place
    if (o == Outil::Selection || o == Outil::Router) {
      reprendreTrace();
      return;
    }
    m_outils.remplacer(creerOutil(o));
  } else if (actif && actif->geste() && actif->genre() == static_cast<int>(Outil::Router) && o != Outil::Selection && o != Outil::Router) {
    // Pendant une piste, l'outil demandé la suspend avec son tracé (ERG-21)
    m_outils.empiler(creerOutil(o));
  } else {
    // Une piste en cours est posée telle quelle (ce qui est fixé reste) avant de changer d'outil
    m_outils.actif()->quitter();
    m_outils.armer(creerOutil(o));
  }
  if (o != Outil::Selection && !m_sel->vide()) m_sel->vider();  // un outil armé vide la sélection (S11)
  appliquerCurseur();
  majBandeau();
  majCartouche();
  emit outilChange();
  update();
}

VueCircuit::Outil VueCircuit::outil() const { return static_cast<Outil>(m_outils.actif()->genre()); }

GesteVue VueCircuit::geste(QMouseEvent* e) const { return {e->position(), e->button(), e->buttons(), e->modifiers()}; }

// Bandeau d'outil : l'outil armé, sinon le réseau surligné ; rien à l'état neutre (charte, 1.7)
void VueCircuit::majBandeau() {
  if (!m_bandeau) return;
  ContenuBandeau c = m_outils.actif()->bandeau();
  // Commande imbriquée : le bandeau dit ce qui attend, et sa sortie reprend la piste (ERG-21)
  if (const OutilVue* attente = m_outils.suspendu(); attente && !c.vide()) {
    QString t = attente->bandeau().titre;
    if (!t.isEmpty()) t[0] = t[0].toLower();
    c.detail = (c.detail.isEmpty() ? QString() : c.detail + QStringLiteral(" · ")) + t + QStringLiteral(" suspendu");
    for (ActionBandeau& x : c.actions)
      if (x.sortie) x.libelle = QStringLiteral("Reprendre la piste");
  }
  // Bandeau de sélection : le réseau surligné y reste dit
  if (!c.vide() && !m_reseau.empty() && outil() == Outil::Selection && !m_outils.actif()->geste())
    c.detail += QStringLiteral(" · réseau %1 surligné").arg(QString::fromStdString(m_reseau));
  if (c.vide() && !m_reseau.empty()) {
    c.titre = QStringLiteral("Réseau %1 surligné").arg(QString::fromStdString(m_reseau));
    if (m_calc)
      if (const int i = m_calc->k.indice(m_reseau); i >= 0) c.detail = QStringLiteral("%1 broche(s)").arg(m_calc->k.reseaux[static_cast<std::size_t>(i)].broches.size());
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

// Échap, une seule règle (charte) : le geste en cours d'abord, puis l'état neutre en une fois
// Curseur (charte) : main fermée pendant un déplacement de la vue, flèche et sablier pendant un calcul long, cercle
// barré pour un outil sur une conception publiée, sinon celui de l'outil
void VueCircuit::appliquerCurseur() {
  if (m_nav && m_nav->panoramique()) setCursor(Qt::ClosedHandCursor);
  else if (m_calculLong) setCursor(Qt::BusyCursor);
  else if (const prom::ecad::ConceptionElec* c = conception(); c && c->verrouillee && outil() != Outil::Selection) setCursor(Qt::ForbiddenCursor);
  else setCursor(m_outils.actif()->curseur());
}

void VueCircuit::setCalculLong(bool oui) {
  if (oui == m_calculLong) return;
  m_calculLong = oui;
  appliquerCurseur();
}

void VueCircuit::echap() {
  if (m_outils.actif()->echap()) {
    majBandeau();
    return;
  }
  if (m_outils.suspendus()) {
    reprendreTrace();  // Échap dans une commande imbriquée : la piste reprend, rien d'autre ne change
    return;
  }
  if (outil() != Outil::Selection) setOutil(Outil::Selection);
  m_sel->neutre();
  majBandeau();
  update();
}

void VueCircuit::setCoucheActive(Couche c) {
  m_coucheActive = c;
  update();
}

void VueCircuit::setCoucheVisible(Couche c, bool v) {
  m_visibles[static_cast<std::size_t>(c)] = v;
  update();
}

void VueCircuit::recadrer() {
  const ConceptionElec* c = conception();
  if (!c || width() < 10 || height() < 10) return;
  Boite b = boite(c->circuit.contour.polygone());
  for (const auto& e : c->circuit.empreintes)
    if (const DefEmpreinte* d = c->empreinte(e.empreinte)) b.etendre(boite(encombrementSurCarte(e, *d)));
  b = b.gonflee(3 * NM_PAR_MM);
  m_echelle = std::clamp(std::min((width() - 20) / mm(b.largeur()), (height() - 20) / mm(b.hauteur())), 1.0, 200.0);
  m_origine = QPointF(width() / 2.0 - mm(b.centre().x) * m_echelle, height() / 2.0 - mm(b.centre().y) * m_echelle);
  update();
}


// Commande du menu à marques (1.19) : Annuler retire d'abord un tracé en cours, sinon le dernier pas ; les autres
// sont des actions du registre, faites comme leur touche
void VueCircuit::executerGeste(const std::string& action) {
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

// Premier appui « agir au curseur » : la via se pose où est le curseur, sans armer d'outil
bool VueCircuit::agirAuCurseur(std::string_view action) {
  if (action != "circuit.via" || m_outils.actif()->geste()) return false;
  const std::unique_ptr<OutilVue> outil = creerOutil(Outil::Via);
  outil->appui(GesteVue{m_souris, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier});
  update();
  return true;
}

QString VueCircuit::annonceClavier() const { return m_nav ? m_nav->annonce() : QString(); }

bool VueCircuit::menuMarquesVisible() const { return m_nav && m_nav->menuVisible(); }

// Zoom sur la sélection (menu à marques, vers le haut) : les objets choisis cadrés, sinon toute la carte
void VueCircuit::zoomerSurSelection() {
  const ConceptionElec* c = conception();
  Boite b;
  if (c)
    for (const auto& o : m_selection) switch (o.genre) {
        case Genre::Empreinte:
          for (const auto& e : c->circuit.empreintes)
            if (e.composant == o.id)
              if (const DefEmpreinte* d = c->empreinte(e.empreinte)) b.etendre(boite(encombrementSurCarte(e, *d)));
          break;
        case Genre::Piste:
          for (const auto& t : c->circuit.pistes)
            if (t.uuid == o.id) {
              b.etendre(t.a);
              b.etendre(t.b);
            }
          break;
        case Genre::Via:
          for (const auto& v : c->circuit.vias)
            if (v.uuid == o.id) b.etendre(v.p);
          break;
        case Genre::Zone:
          for (const auto& z : c->circuit.zones)
            if (z.uuid == o.id) b.etendre(boite(z.contour));
          break;
        case Genre::Texte:
          for (const auto& t : c->circuit.textes)
            if (t.uuid == o.id) b.etendre(t.p);
          break;
        case Genre::Rien: break;
      }
  if (b.vide() || width() < 10 || height() < 10) {
    recadrer();
    return;
  }
  b = b.gonflee(2 * NM_PAR_MM);
  m_echelle = std::clamp(std::min((width() - 20) / mm(std::max<Nm>(b.largeur(), 1)), (height() - 20) / mm(std::max<Nm>(b.hauteur(), 1))), 1.0, 200.0);
  m_origine = QPointF(width() / 2.0 - mm(b.centre().x) * m_echelle, height() / 2.0 - mm(b.centre().y) * m_echelle);
  update();
}

void VueCircuit::centrerSur(Pt p) {
  m_echelle = std::max(m_echelle, 20.0);
  m_origine = QPointF(width() / 2.0 - mm(p.x) * m_echelle, height() / 2.0 - mm(p.y) * m_echelle);
  update();
}

namespace {
SelectionElectronique::Genre versSelection(VueCircuit::Genre g) {
  switch (g) {
    case VueCircuit::Genre::Piste: return SelectionElectronique::Genre::Piste;
    case VueCircuit::Genre::Via: return SelectionElectronique::Genre::Via;
    case VueCircuit::Genre::Zone: return SelectionElectronique::Genre::Zone;
    case VueCircuit::Genre::Texte: return SelectionElectronique::Genre::TexteCarte;
    default: return SelectionElectronique::Genre::Empreinte;
  }
}
VueCircuit::Genre depuisSelection(SelectionElectronique::Genre g) {
  switch (g) {
    case SelectionElectronique::Genre::Empreinte: return VueCircuit::Genre::Empreinte;
    case SelectionElectronique::Genre::Piste: return VueCircuit::Genre::Piste;
    case SelectionElectronique::Genre::Via: return VueCircuit::Genre::Via;
    case SelectionElectronique::Genre::Zone: return VueCircuit::Genre::Zone;
    case SelectionElectronique::Genre::TexteCarte: return VueCircuit::Genre::Texte;
    default: return VueCircuit::Genre::Rien;
  }
}
SelectionElectronique::Ref ref(const VueCircuit::ObjetChoisi& o) {
  SelectionElectronique::Ref r{versSelection(o.genre), o.id, {}};
  if (o.genre == VueCircuit::Genre::Empreinte) r.composant = o.id;
  return r;
}
}  // namespace

void VueCircuit::setSelectionPartagee(SelectionElectronique* s) {
  if (!s) {
    if (!m_selPropre) m_selPropre = new SelectionElectronique(this);
    s = m_selPropre;
  }
  if (s == m_sel) return;
  if (m_sel) disconnect(m_sel, nullptr, this, nullptr);
  m_sel = s;
  connect(m_sel, &SelectionElectronique::changee, this, [this] {
    if (majSelectionVue()) emit selectionChangee();
    majBandeau();  // le bandeau de sélection suit ce qui est choisi
  });
  connect(m_sel, &SelectionElectronique::modeBandeauChange, this, [this] { majBandeau(); });
  connect(m_sel, &SelectionElectronique::estompageChange, this, [this] { update(); });
  connect(m_sel, &SelectionElectronique::surlignageChange, this, [this] {
    m_reseau = m_sel->reseau();
    update();
  });
  m_reseau = m_sel->reseau();
  if (majSelectionVue()) emit selectionChangee();
}

void VueCircuit::setReseauSurligne(const std::string& n) { m_sel->surligner(n); }

bool VueCircuit::majSelectionVue() {
  std::vector<ObjetChoisi> s;
  const ConceptionElec* c = conception();
  if (c && m_sel)
    for (const auto& r : m_sel->objets()) {
      if (const Genre g = depuisSelection(r.genre); g != Genre::Rien) {
        const ObjetChoisi o{g, r.id};
        if (std::find(s.begin(), s.end(), o) == s.end()) s.push_back(o);
      } else if (!r.composant.empty()) {
        // Symbole choisi au schéma : son empreinte l'est ici (1.5)
        const ObjetChoisi o{Genre::Empreinte, r.composant};
        if (std::find(s.begin(), s.end(), o) == s.end() &&
            std::any_of(c->circuit.empreintes.begin(), c->circuit.empreintes.end(), [&](const EmpreintePlacee& e) { return e.composant == r.composant; }))
          s.push_back(o);
      }
    }
  update();
  const Genre genre = s.empty() ? Genre::Rien : s.back().genre;
  const std::string choisi = s.empty() ? std::string() : s.back().id;
  if (s == m_selection && genre == m_genre && choisi == m_choisi) return false;
  m_selection = std::move(s);
  m_genre = genre;
  m_choisi = choisi;
  return true;
}

void VueCircuit::choisir(Genre g, const std::string& id) {
  if (g == Genre::Rien || id.empty()) m_sel->vider();
  else m_sel->choisir({ref({g, id})});
  if (majSelectionVue()) emit selectionChangee();
}

void VueCircuit::setSelection(const std::vector<ObjetChoisi>& s) {
  std::vector<SelectionElectronique::Ref> refs;
  for (const auto& o : s)
    if (o.genre != Genre::Rien && !o.id.empty()) refs.push_back(ref(o));
  m_sel->choisir(refs);
  if (majSelectionVue()) emit selectionChangee();
}

bool VueCircuit::estChoisi(Genre g, const std::string& id) const { return std::find(m_selection.begin(), m_selection.end(), ObjetChoisi{g, id}) != m_selection.end(); }

int VueCircuit::empreinteSous(Pt p) const {
  const ConceptionElec* c = conception();
  if (!c) return -1;
  int meilleur = -1;
  double aire = 1e300;
  for (std::size_t i = 0; i < c->circuit.empreintes.size(); ++i) {
    const DefEmpreinte* d = c->empreinte(c->circuit.empreintes[i].empreinte);
    if (!d) continue;
    const Polygone enc = encombrementSurCarte(c->circuit.empreintes[i], *d);
    if (dedans(enc, p) && aireMm2(enc) < aire) { aire = aireMm2(enc); meilleur = static_cast<int>(i); }
  }
  return meilleur;
}

int VueCircuit::pastilleSous(Pt p) const {
  if (!m_calc) return -1;
  for (std::size_t i = 0; i < m_calc->pastilles.size(); ++i) {
    const auto& x = m_calc->pastilles[i];
    if ((x.couches & tousLesCuivres(4)) && dedans(x.forme, p)) return static_cast<int>(i);
  }
  return -1;
}

// Le premier objet sous le point que le filtre de sélection admet (SAI-18)
VueCircuit::ObjetChoisi VueCircuit::objetSous(QPointF q, std::string* reseau) const {
  std::vector<std::string> reseaux;
  const std::vector<ObjetChoisi> v = objetsSous(q, false, &reseaux);
  // Masque et ombre (2.26, ERG-28) : ce qui n'est pas choisi ne se prend plus
  const bool masque = m_sel && m_sel->intouchable();
  for (std::size_t i = 0; i < v.size(); ++i)
    if ((!m_sel || m_sel->admis(ref(v[i]).genre)) && (!masque || estChoisi(v[i].genre, v[i].id))) {
      if (reseau) *reseau = reseaux[i];
      return v[i];
    }
  return {};
}

std::vector<VueCircuit::ObjetChoisi> VueCircuit::objetsSous(QPointF q, bool interieurs, std::vector<std::string>* reseaux) const {
  std::vector<ObjetChoisi> r;
  const ConceptionElec* c = conception();
  if (!c) return r;
  auto ajouter = [&](Genre g, const std::string& id, const std::string& reseau) {
    r.push_back({g, id});
    if (reseaux) reseaux->push_back(reseau);
  };
  const Pt w = monde(q);
  const double tol = 3.0 / m_echelle * 1e6;
  for (const auto& v : c->circuit.vias)
    if (distance(v.p, w) <= static_cast<double>(v.diametre) / 2 + tol) ajouter(Genre::Via, v.uuid, v.reseau);
  const Couches presentes = tousLesCuivres(c->circuit.empilage.couchesCuivre);  // rien d'invisible ne se choisit
  for (int pass = 0; pass < 2; ++pass)
    for (const auto& t : c->circuit.pistes) {
      if (!coucheVisible(t.couche) || !(bit(t.couche) & presentes) || (pass == 0) != (t.couche == m_coucheActive)) continue;
      if (distancePointSegment(w, t.a, t.b) <= static_cast<double>(t.largeur) / 2 + tol) ajouter(Genre::Piste, t.uuid, t.reseau);
    }
  for (const auto& t : c->circuit.textes)
    if (coucheVisible(t.couche) && boiteTexte(t).gonflee(static_cast<Nm>(tol)).contient(w)) ajouter(Genre::Texte, t.uuid, {});
  if (const int e = empreinteSous(w); e >= 0) {
    const int p = pastilleSous(w);
    ajouter(Genre::Empreinte, c->circuit.empreintes[static_cast<std::size_t>(e)].composant, p >= 0 ? m_calc->pastilles[static_cast<std::size_t>(p)].reseau : std::string());
  }
  // Une zone se prend par son bord (4 px) : son intérieur compte comme le vide, pour la désélection et le cadre (C10)
  // Deux bords proches (un plan sur le contour de la carte, un autre en retrait) : le plus proche du clic d'abord
  const double bord = 4.0 / m_echelle * 1e6;
  std::vector<std::pair<double, const Zone*>> zones;
  for (const auto& z : c->circuit.zones) {
    if (!coucheVisible(z.couche) || !(bit(z.couche) & presentes) || z.contour.size() < 3) continue;
    double d = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < z.contour.size(); ++i) d = std::min(d, distancePointSegment(w, z.contour[i], z.contour[(i + 1) % z.contour.size()]));
    if (d <= bord) zones.push_back({d, &z});
    else if (interieurs && dedans(z.contour, w)) zones.push_back({bord + 1, &z});
  }
  std::stable_sort(zones.begin(), zones.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& [d, z] : zones) ajouter(Genre::Zone, z->uuid, z->reseau);
  return r;
}

QString VueCircuit::libelleObjet(const ObjetChoisi& o) const {
  const ConceptionElec* c = conception();
  if (!c) return {};
  auto reseau = [](const std::string& n) { return n.empty() ? QStringLiteral("sans réseau") : QString::fromStdString(n); };
  switch (o.genre) {
    case Genre::Via:
      for (const auto& v : c->circuit.vias)
        if (v.uuid == o.id) return QStringLiteral("Via %1").arg(reseau(v.reseau));
      break;
    case Genre::Piste:
      for (const auto& t : c->circuit.pistes)
        if (t.uuid == o.id) return QStringLiteral("Piste %1 · %2").arg(reseau(t.reseau), QString::fromUtf8(nomCouche(t.couche)));
      break;
    case Genre::Texte:
      for (const auto& t : c->circuit.textes)
        if (t.uuid == o.id) return QStringLiteral("Texte « %1 »").arg(QString::fromStdString(t.texte));
      break;
    case Genre::Empreinte:
      if (const ComposantElec* k = c->composant(o.id)) return QStringLiteral("%1 (empreinte)").arg(QString::fromStdString(k->reference));
      break;
    case Genre::Zone:
      for (const auto& z : c->circuit.zones)
        if (z.uuid == o.id) return QStringLiteral("Zone %1 · %2").arg(reseau(z.reseau), QString::fromUtf8(nomCouche(z.couche)));
      break;
    case Genre::Rien: break;
  }
  return {};
}

void VueCircuit::listerObjetsSous(QPointF q) {
  std::vector<std::string> reseaux;
  const std::vector<ObjetChoisi> v = objetsSous(q, true, &reseaux);
  if (v.empty()) {
    emit message(QStringLiteral("Rien sous le curseur."));
    return;
  }
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("objetsSous"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  menu->addSection(QStringLiteral("Objets sous le curseur"));
  for (std::size_t i = 0; i < v.size(); ++i) {
    const ObjetChoisi o = v[i];
    const std::string reseau = reseaux[i];
    connect(menu->addAction(libelleObjet(o)), &QAction::triggered, this, [this, o, reseau] {
      // Le mode du bandeau (ou Maj, Ctrl) vaut aussi pour la liste : « Ajouter » y complète la sélection (SAI-18)
      const auto mode = m_sel->modeClic(QGuiApplication::keyboardModifiers());
      m_sel->appliquer({ref(o)}, mode);
      if (majSelectionVue()) emit selectionChangee();
      if (mode == SelectionElectronique::Mode::Remplacer) m_sel->surligner(reseau);
    });
  }
  menu->popup(mapToGlobal(q.toPoint()));
}

// Clic de l'outil Sélection : dans le vide, l'état neutre (S5) ; sur un objet, remplacer, ajouter (Maj),
// basculer (Ctrl) ou retirer (Ctrl+Maj), comme au schéma (S23) ; le réseau de l'objet cliqué est surligné
void VueCircuit::choisirSous(QPointF q, Qt::KeyboardModifiers mods) {
  const auto mode = m_sel->modeClic(mods);
  std::vector<std::string> reseaux;
  std::vector<ObjetChoisi> sous = objetsSous(q, false, &reseaux);
  // Le filtre de sélection écarte ce qu'il exclut ; un clic qu'il bloque entièrement le fait clignoter (SAI-18)
  // Le masque et l'ombre (2.26, ERG-28) écartent ce qui n'est pas choisi : un clic dessus ne change rien
  const bool quelqueChose = !sous.empty();
  const bool masque = m_sel->intouchable();
  bool masques = false;
  for (std::size_t i = sous.size(); i-- > 0;) {
    const bool cache = masque && !estChoisi(sous[i].genre, sous[i].id);
    if (!m_sel->admis(ref(sous[i]).genre) || cache) {
      masques = masques || cache;
      sous.erase(sous.begin() + static_cast<std::ptrdiff_t>(i));
      reseaux.erase(reseaux.begin() + static_cast<std::ptrdiff_t>(i));
    }
  }
  if (quelqueChose && sous.empty()) {
    m_sel->signalerBlocage();
    if (masques) return;
  }
  std::size_t k = 0;
  // Un clic répété au même endroit, sans bouger, passe à l'objet suivant dessous (une piste sur une pastille…)
  const qint64 maintenant = QDateTime::currentMSecsSinceEpoch();
  if (mode == SelectionElectronique::Mode::Remplacer && sous.size() > 1 && (q - m_dernierClic).manhattanLength() <= 2 && maintenant - m_quandDernierClic < 2000 && m_selection.size() == 1)
    for (std::size_t i = 0; i < sous.size(); ++i)
      if (sous[i] == m_selection.front()) k = (i + 1) % sous.size();
  m_dernierClic = q;
  m_quandDernierClic = maintenant;
  const ObjetChoisi o = sous.empty() ? ObjetChoisi{} : sous[k];
  const std::string reseau = sous.empty() ? std::string() : reseaux[k];
  if (o.genre == Genre::Rien) {
    if (mode == SelectionElectronique::Mode::Remplacer) m_sel->neutre();
    return;
  }
  // Un clic sur un objet déjà choisi garde la sélection multiple (pour la glisser) et en fait l'objet tenu
  if (mode == SelectionElectronique::Mode::Remplacer && estChoisi(o.genre, o.id)) m_sel->tenir(ref(o));
  else m_sel->appliquer({ref(o)}, mode);
  if (majSelectionVue()) emit selectionChangee();
  if (mode == SelectionElectronique::Mode::Remplacer) m_sel->surligner(reseau);
}

void VueCircuit::choisirDansCadre(Pt a, Pt b, bool touches, int mode) {
  const ConceptionElec* c = conception();
  if (!c) return;
  const Nm x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
  auto dans = [&](Pt p) { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; };
  const Polygone cadre{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
  auto touche = [&](Pt p, Pt q) {
    if (dans(p) || dans(q)) return true;
    for (std::size_t i = 0; i < 4; ++i)
      if (distanceSegments(cadre[i], cadre[(i + 1) % 4], p, q) < 1.0) return true;
    return false;
  };
  std::vector<ObjetChoisi> s;
  for (const auto& e : c->circuit.empreintes) {
    const DefEmpreinte* d = c->empreinte(e.empreinte);
    if (!d) continue;
    const Polygone enc = encombrementSurCarte(e, *d);
    bool tout = true, un = false;
    for (std::size_t i = 0; i < enc.size(); ++i) {
      tout &= dans(enc[i]);
      un |= touche(enc[i], enc[(i + 1) % enc.size()]);
    }
    if (touches ? (un || dedans(enc, a)) : tout) s.push_back({Genre::Empreinte, e.composant});
  }
  for (const auto& t : c->circuit.pistes)
    if (coucheVisible(t.couche) && (touches ? touche(t.a, t.b) : dans(t.a) && dans(t.b))) s.push_back({Genre::Piste, t.uuid});
  for (const auto& v : c->circuit.vias)
    if (dans(v.p)) s.push_back({Genre::Via, v.uuid});
  for (const auto& t : c->circuit.textes) {
    const Boite bt = boiteTexte(t);
    if (coucheVisible(t.couche) && (touches ? bt.touche(Boite{x0, y0, x1, y1}) : dans({bt.x0, bt.y0}) && dans({bt.x1, bt.y1}))) s.push_back({Genre::Texte, t.uuid});
  }
  for (const auto& z : c->circuit.zones) {
    if (!coucheVisible(z.couche) || z.contour.empty()) continue;
    bool tout = true, un = false;
    for (std::size_t i = 0; i < z.contour.size(); ++i) {
      tout &= dans(z.contour[i]);
      un |= touche(z.contour[i], z.contour[(i + 1) % z.contour.size()]);
    }
    if (touches ? un : tout) s.push_back({Genre::Zone, z.uuid});
  }
  std::vector<SelectionElectronique::Ref> refs;
  for (const auto& o : s)
    if (m_sel->admis(ref(o).genre)) refs.push_back(ref(o));  // le filtre de sélection vaut aussi pour le cadre
  m_sel->appliquer(refs, static_cast<SelectionElectronique::Mode>(mode));
  if (majSelectionVue()) emit selectionChangee();
  if (!s.empty()) emit message(QStringLiteral("%1 objet(s) choisi(s) · glisse pour les déplacer · Suppr retire le cuivre choisi").arg(m_selection.size()));
}

void VueCircuit::toutChoisir() {
  const ConceptionElec* c = conception();
  if (!c) return;
  const Couches presentes = tousLesCuivres(c->circuit.empilage.couchesCuivre);
  std::vector<SelectionElectronique::Ref> refs;
  for (const auto& e : c->circuit.empreintes) refs.push_back(ref({Genre::Empreinte, e.composant}));
  for (const auto& t : c->circuit.pistes)
    if (coucheVisible(t.couche) && (bit(t.couche) & presentes)) refs.push_back(ref({Genre::Piste, t.uuid}));
  for (const auto& v : c->circuit.vias) refs.push_back(ref({Genre::Via, v.uuid}));
  for (const auto& z : c->circuit.zones)
    if (coucheVisible(z.couche) && (bit(z.couche) & presentes)) refs.push_back(ref({Genre::Zone, z.uuid}));
  for (const auto& t : c->circuit.textes)
    if (coucheVisible(t.couche)) refs.push_back(ref({Genre::Texte, t.uuid}));
  m_sel->choisir(refs);
  if (majSelectionVue()) emit selectionChangee();
  emit message(QStringLiteral("%1 objet(s) choisi(s) sur les couches visibles.").arg(m_selection.size()));
}

// Bouts de piste qui suivent les objets déplacés : pistes accrochées aux pastilles des empreintes choisies
// (coordonnées locales de l'empreinte, pour suivre aussi une rotation ou un retournement), et pistes non choisies
// qui touchent une piste ou une via choisie (translation) (EL-19)
std::vector<VueCircuit::Accroche> VueCircuit::accroches(const std::vector<ObjetChoisi>& objets) const {
  std::vector<Accroche> r;
  const ConceptionElec* c = conception();
  if (!c) return r;
  auto choisi = [&](Genre g, const std::string& id) { return std::find(objets.begin(), objets.end(), ObjetChoisi{g, id}) != objets.end(); };
  auto deja = [&](const std::string& piste, bool b) { return std::any_of(r.begin(), r.end(), [&](const Accroche& x) { return x.piste == piste && x.b == b; }); };
  if (m_calc)
    for (const auto& p : m_calc->pastilles) {
      if (p.empreinte < 0 || p.empreinte >= static_cast<int>(c->circuit.empreintes.size())) continue;
      const EmpreintePlacee& e = c->circuit.empreintes[static_cast<std::size_t>(p.empreinte)];
      if (!choisi(Genre::Empreinte, e.composant) || e.verrou) continue;
      for (const auto& t : c->circuit.pistes) {
        if (choisi(Genre::Piste, t.uuid) || !(p.couches & bit(t.couche))) continue;
        for (const bool b : {false, true}) {
          const Pt bout = b ? t.b : t.a;
          if (!deja(t.uuid, b) && dedans(p.forme, bout)) r.push_back({t.uuid, b, p.empreinte, versEmpreinte(e, bout), bout});
        }
      }
    }
  std::vector<std::pair<Pt, double>> points;  // extrémités des pistes choisies et centres des vias choisies
  for (const auto& t : c->circuit.pistes)
    if (choisi(Genre::Piste, t.uuid)) {
      points.push_back({t.a, static_cast<double>(t.largeur) / 2});
      points.push_back({t.b, static_cast<double>(t.largeur) / 2});
    }
  for (const auto& v : c->circuit.vias)
    if (choisi(Genre::Via, v.uuid)) points.push_back({v.p, static_cast<double>(v.diametre) / 2});
  for (const auto& t : c->circuit.pistes) {
    if (choisi(Genre::Piste, t.uuid)) continue;
    for (const bool b : {false, true}) {
      const Pt bout = b ? t.b : t.a;
      if (deja(t.uuid, b)) continue;
      for (const auto& [p, rayon] : points)
        if (distance(p, bout) <= rayon) { r.push_back({t.uuid, b, -1, {}, bout}); break; }
    }
  }
  return r;
}

// Position des objets choisis décalés de d depuis leur position de départ (m_circuitDepart)
void VueCircuit::appliquerDeplacement(ConceptionElec& c, Pt d) const {
  auto choisi = [&](Genre g, const std::string& id) { return estChoisi(g, id); };
  for (auto& e : c.circuit.empreintes)
    if (choisi(Genre::Empreinte, e.composant) && !e.verrou)
      for (const auto& e0 : m_circuitDepart.empreintes)
        if (e0.composant == e.composant) e.position = e0.position + d;
  for (auto& t : c.circuit.pistes)
    if (choisi(Genre::Piste, t.uuid))
      for (const auto& t0 : m_circuitDepart.pistes)
        if (t0.uuid == t.uuid) { t.a = t0.a + d; t.b = t0.b + d; }
  for (auto& v : c.circuit.vias)
    if (choisi(Genre::Via, v.uuid))
      for (const auto& v0 : m_circuitDepart.vias)
        if (v0.uuid == v.uuid) v.p = v0.p + d;
  for (auto& z : c.circuit.zones)
    if (choisi(Genre::Zone, z.uuid))
      for (const auto& z0 : m_circuitDepart.zones)
        if (z0.uuid == z.uuid) {
          z.contour = z0.contour;
          for (Pt& p : z.contour) p = p + d;
        }
  for (auto& t : c.circuit.textes)
    if (choisi(Genre::Texte, t.uuid))
      for (const auto& t0 : m_circuitDepart.textes)
        if (t0.uuid == t.uuid) t.p = t0.p + d;
  for (const Accroche& a : m_accroches)
    for (auto& t : c.circuit.pistes) {
      if (t.uuid != a.piste) continue;
      const Pt nouveau = a.empreinte >= 0 && a.empreinte < static_cast<int>(c.circuit.empreintes.size()) ? versCarte(c.circuit.empreintes[static_cast<std::size_t>(a.empreinte)], a.local) : a.depart + d;
      (a.b ? t.b : t.a) = nouveau;
    }
}

void VueCircuit::deplacerSelection(Pt delta) {
  const ConceptionElec* c = conception();
  if (!c || m_selection.empty() || refuseVerrou()) return;
  m_circuitDepart = c->circuit;
  m_accroches = accroches(m_selection);
  modifier([&](ConceptionElec& c2) { appliquerDeplacement(c2, delta); }, QStringLiteral("fleches-circuit"));
  m_accroches.clear();
}

// Tourner ou retourner les empreintes choisies : chacune autour de son origine, les pistes accrochées suivent
void VueCircuit::transformerEmpreintes(const std::function<void(EmpreintePlacee&)>& f) {
  const ConceptionElec* c = conception();
  if (!c) return;
  std::vector<ObjetChoisi> empreintes;
  bool verrou = false;
  for (const auto& o : m_selection)
    if (o.genre == Genre::Empreinte)
      for (const auto& e : c->circuit.empreintes)
        if (e.composant == o.id) {
          if (e.verrou) verrou = true;
          else empreintes.push_back(o);
        }
  if (empreintes.empty()) {
    if (verrou) emit message(QStringLiteral("Empreinte verrouillée : déverrouille-la dans le panneau pour la tourner ou la retourner."));
    return;
  }
  const auto acc = accroches(empreintes);
  modifier([&](ConceptionElec& c2) {
    for (auto& e : c2.circuit.empreintes)
      if (!e.verrou && std::find(empreintes.begin(), empreintes.end(), ObjetChoisi{Genre::Empreinte, e.composant}) != empreintes.end()) f(e);
    for (const Accroche& a : acc)
      for (auto& t : c2.circuit.pistes)
        if (t.uuid == a.piste && a.empreinte >= 0 && a.empreinte < static_cast<int>(c2.circuit.empreintes.size()))
          (a.b ? t.b : t.a) = versCarte(c2.circuit.empreintes[static_cast<std::size_t>(a.empreinte)], a.local);
  });
}

void VueCircuit::tournerChoix(bool inverse) {
  transformerEmpreintes([inverse](EmpreintePlacee& e) { e.rotation = std::fmod(e.rotation + (inverse ? 270.0 : 90.0), 360.0); });
}

void VueCircuit::retournerChoix() {
  transformerEmpreintes([](EmpreintePlacee& e) { e.dessous = !e.dessous; });
}

void VueCircuit::supprimerChoix() {
  std::set<std::string> pistes, vias, zones, textes;
  bool empreinte = false;
  for (const auto& o : m_selection) {
    if (o.genre == Genre::Texte) textes.insert(o.id);
    if (o.genre == Genre::Piste) pistes.insert(o.id);
    else if (o.genre == Genre::Via) vias.insert(o.id);
    else if (o.genre == Genre::Zone) zones.insert(o.id);
    else if (o.genre == Genre::Empreinte) empreinte = true;
  }
  if (pistes.empty() && vias.empty() && zones.empty() && textes.empty()) {
    if (empreinte) emit message(QStringLiteral("Une empreinte vient du schéma : supprime le composant dans le schéma, puis mets le circuit à jour."));
    return;
  }
  modifier([&](ConceptionElec& c) {
    auto& p = c.circuit.pistes;
    p.erase(std::remove_if(p.begin(), p.end(), [&](const Piste& x) { return pistes.count(x.uuid) > 0; }), p.end());
    auto& v = c.circuit.vias;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Via& x) { return vias.count(x.uuid) > 0; }), v.end());
    auto& z = c.circuit.zones;
    z.erase(std::remove_if(z.begin(), z.end(), [&](const Zone& x) { return zones.count(x.uuid) > 0; }), z.end());
    auto& t = c.circuit.textes;
    t.erase(std::remove_if(t.begin(), t.end(), [&](const TexteCircuit& x) { return textes.count(x.uuid) > 0; }), t.end());
  });
  std::vector<ObjetChoisi> reste;
  for (const auto& o : m_selection)
    if (o.genre == Genre::Empreinte) reste.push_back(o);
  setSelection(reste);
  if (empreinte) emit message(QStringLiteral("Cuivre choisi supprimé ; les empreintes restent (elles viennent du schéma)."));
}

// ---------- Textes de la carte ----------
Boite VueCircuit::boiteTexte(const TexteCircuit& t) const {
  Boite b;
  const bool dessous = t.couche == Couche::SerigraphieDessous;
  for (const auto& tr : traitsTexte(t.texte, t.p, std::max<Nm>(t.hauteur, 300'000), t.rotation, dessous)) {
    b.etendre(tr.a);
    b.etendre(tr.b);
  }
  if (b.vide()) b = Boite{t.p.x, t.p.y, t.p.x, t.p.y};
  return b.gonflee(epaisseurTexte(t.hauteur));
}

std::string VueCircuit::poserTexte(Pt p, const std::string& texte) {
  if (texte.empty() || refuseVerrou()) return {};
  TexteCircuit t;
  t.uuid = nouvelUuid();
  t.texte = texte;
  t.p = surGrille(p, pasPlacement());
  t.couche = m_coucheActive == Couche::CuivreDessous ? Couche::SerigraphieDessous : Couche::SerigraphieDessus;
  modifier([&](ConceptionElec& c) { c.circuit.textes.push_back(t); });
  choisir(Genre::Texte, t.uuid);
  return t.uuid;
}

// ---------- Presse-papiers du cuivre (EL-22) ----------
void VueCircuit::copier() {
  const ConceptionElec* c = conception();
  if (!c) return;
  PressePapiers p;
  Boite b;
  for (const auto& t : c->circuit.pistes)
    if (estChoisi(Genre::Piste, t.uuid)) { p.pistes.push_back(t); b.etendre(t.a); b.etendre(t.b); }
  for (const auto& v : c->circuit.vias)
    if (estChoisi(Genre::Via, v.uuid)) { p.vias.push_back(v); b.etendre(v.p); }
  for (const auto& z : c->circuit.zones)
    if (estChoisi(Genre::Zone, z.uuid)) { p.zones.push_back(z); b.etendre(boite(z.contour)); }
  for (const auto& t : c->circuit.textes)
    if (estChoisi(Genre::Texte, t.uuid)) { p.textes.push_back(t); b.etendre(t.p); }
  if (p.pistes.empty() && p.vias.empty() && p.zones.empty() && p.textes.empty()) {
    emit message(QStringLiteral("Rien à copier : choisis des pistes, des vias ou des zones (les empreintes se copient dans le schéma)."));
    return;
  }
  p.origine = surGrille(b.centre(), pasPlacement());
  m_presse = p;
  emit message(QStringLiteral("%1 piste(s), %2 via(s), %3 zone(s) copiée(s).").arg(p.pistes.size()).arg(p.vias.size()).arg(p.zones.size()));
}

void VueCircuit::coller() {
  if (!peutColler() || refuseVerrou()) return;
  const QPointF souris = rect().contains(m_souris.toPoint()) ? m_souris : QPointF(width() / 2.0, height() / 2.0);
  const Pt d = surGrille(monde(souris), pasPlacement()) - m_presse.origine;
  std::vector<ObjetChoisi> colles;
  modifier([&](ConceptionElec& c) {
    for (Piste t : m_presse.pistes) {
      t.uuid = nouvelUuid();
      t.a = t.a + d;
      t.b = t.b + d;
      colles.push_back({Genre::Piste, t.uuid});
      c.circuit.pistes.push_back(t);
    }
    for (Via v : m_presse.vias) {
      v.uuid = nouvelUuid();
      v.p = v.p + d;
      colles.push_back({Genre::Via, v.uuid});
      c.circuit.vias.push_back(v);
    }
    for (Zone z : m_presse.zones) {
      z.uuid = nouvelUuid();
      for (Pt& p : z.contour) p = p + d;
      colles.push_back({Genre::Zone, z.uuid});
      c.circuit.zones.push_back(z);
    }
    for (TexteCircuit t : m_presse.textes) {
      t.uuid = nouvelUuid();
      t.p = t.p + d;
      colles.push_back({Genre::Texte, t.uuid});
      c.circuit.textes.push_back(t);
    }
  });
  setSelection(colles);
  emit message(QStringLiteral("Collé sous la souris : le cuivre garde son réseau ; la DRC signale tout court-circuit."));
}

void VueCircuit::dupliquer() {
  const PressePapiers garde = m_presse;
  copier();
  if (!peutColler()) { m_presse = garde; return; }
  const QPointF ancienne = m_souris;
  m_souris = ecran(m_presse.origine + Pt{NM_PAR_MM, NM_PAR_MM});
  coller();
  m_souris = ancienne;
  m_presse = garde;  // dupliquer ne touche pas au presse-papiers
}

// ---------- Routage ----------
// ---------- Dessin ----------
void VueCircuit::dessinerCouche(QPainter& g, Couche couche, double opacite) {
  const ConceptionElec* c = conception();
  if (!c || !m_calc || !coucheVisible(couche)) return;
  const auto e = [this](Pt p) { return ecran(p); };
  QColor col = couleurCuivre(couche);
  g.setOpacity(opacite);
  for (const auto& z : c->circuit.zones) {
    if (z.couche != couche) continue;
    if (const auto it = m_calc->zones.find(z.uuid); it != m_calc->zones.end()) {
      QColor f = col;
      f.setAlpha(150);
      g.fillPath(chemin(it->second, e), f);
    }
    g.setPen(QPen(col, 1, Qt::DashLine));
    g.setBrush(Qt::NoBrush);
    g.drawPath(chemin({z.contour}, e));
  }
  for (const auto& t : c->circuit.pistes) {
    if (t.couche != couche) continue;
    g.setPen(QPen(col, std::max(1.0, mm(t.largeur) * m_echelle), Qt::SolidLine, Qt::RoundCap));
    g.drawLine(ecran(t.a), ecran(t.b));
  }
  g.setPen(Qt::NoPen);
  g.setBrush(col);
  for (const auto& p : m_calc->pastilles)
    if (p.pastille.type == TypePastille::CMS && (p.couches & bit(couche))) g.drawPath(chemin({p.forme}, e));
  g.setOpacity(1);
}

// Le dessin fixe (grille, carte, cuivre, pastilles, sérigraphie, chevelu, surlignage, sélection, marques) vient de
// l'image gardée tant que rien ne l'a changé ; l'outil, l'article glissé et les repères sont dessinés par-dessus
void VueCircuit::paintEvent(QPaintEvent*) {
  const ConceptionElec* c = conception();
  CleCache cle;
  cle.generation = m_generationDoc;
  cle.calculs = m_generationCalculs;
  cle.theme = theme().generation();
  cle.conception = m_conception;
  cle.echelle = m_echelle;
  cle.opacite = m_opacite;
  cle.origine = m_origine;
  cle.taille = size();
  cle.dpr = devicePixelRatioF();
  cle.active = m_coucheActive;
  cle.visibles.assign(m_visibles.begin(), m_visibles.end());
  cle.reseau = m_reseau;
  cle.selection = m_selection;
  if (m_sel && m_sel->estompageActif()) {
    cle.estompage = static_cast<int>(m_sel->estompage());
    cle.niveau = m_sel->niveauEstompage();
  }
  if (m_cache.isNull() || !(cle == m_cleCache)) {
    m_cache = QPixmap(size() * cle.dpr);
    m_cache.setDevicePixelRatio(cle.dpr);
    QPainter p(&m_cache);
    p.setRenderHint(QPainter::Antialiasing, Theme::anticrenelage());  // réglable (2.31)
    p.fillRect(rect(), FOND);
    if (c) peindreFixe(p, *c);
    m_cleCache = cle;
  }
  QPainter g(this);
  g.drawPixmap(0, 0, m_cache);
  g.setRenderHint(QPainter::Antialiasing);
  if (!c) return;
  const SignesCanevas& sg = theme().signesCircuit();
  // Article mécanique glissé de la bibliothèque : ses pastilles en fantôme (2.2)
  if (m_article)
    if (const DefEmpreinte* de = c->empreinte(m_article->empreinte)) {
      EmpreintePlacee ep;
      ep.position = m_posArticle;
      ep.rotation = m_rotationArticle;
      g.setPen(QPen(sg.sel, 1.5, Qt::DashLine));
      g.setBrush(Qt::NoBrush);
      for (const auto& pa : de->pastilles) {
        QPolygonF poly;
        for (const Pt& q : formePastille(pastilleSurCarte(ep, pa))) poly << ecran(q);
        g.drawPolygon(poly);
      }
      g.drawEllipse(ecran(m_posArticle), 3, 3);
    }
  // Aperçu de l'outil armé (piste en cours, contour, mesure, cadre)
  m_outils.actif()->dessiner(g);
  peindreReperes(g, *c);
  // Le cartouche du tracé suit la tête quand la vue défile ou change d'échelle
  if (m_cartouche && m_cartouche->isVisible()) QTimer::singleShot(0, this, [this] { majCartouche(); });
}

void VueCircuit::peindreFixe(QPainter& g, const ConceptionElec& cref) {
  const ConceptionElec* c = &cref;
  const auto e = [this](Pt p) { return ecran(p); };
  const Polygone carte = c->circuit.contour.polygone();
  // Grille au pas réglé (EL-37), sur la partie visible ; le pas affiché grandit tant que les points seraient trop serrés
  {
    double pas = mm(pasPlacement());
    while (pas * m_echelle < 8) pas *= pas * m_echelle < 1.6 ? 10 : (pas * m_echelle < 4 ? 5 : 2);
    const Pt h = monde(QPointF(0, 0)), b = monde(QPointF(width(), height()));
    const double x0 = std::floor(mm(h.x) / pas) * pas, y0 = std::floor(mm(h.y) / pas) * pas;
    const double nx = (mm(b.x) - x0) / pas, ny = (mm(b.y) - y0) / pas;
    if (nx > 0 && ny > 0 && nx * ny < 60'000) {
      QPolygonF points;
      for (double x = x0; x <= mm(b.x); x += pas)
        for (double y = y0; y <= mm(b.y); y += pas) points << ecran(ptMm(x, y));
      g.setPen(QPen(GRILLE, 1.5));
      g.drawPoints(points);
    }
  }
  // Carte : contour et découpes (EL-44), le fond de la vue visible à travers les découpes
  {
    QPainterPath plaque = chemin({carte}, e);
    for (const auto& d : c->circuit.contour.decoupes) plaque.addPath(chemin({d}, e));
    plaque.setFillRule(Qt::OddEvenFill);
    g.setPen(QPen(BORD, std::max(1.5, 0.15 * m_echelle)));
    g.setBrush(CARTE);
    g.drawPath(plaque);
  }
  // Cuivre : couches inactives atténuées (opacité réglable, EL-25), la couche active par-dessus
  for (Couche x : couchesCuivre(c->circuit.empilage.couchesCuivre))
    if (x != m_coucheActive) dessinerCouche(g, x, m_opacite);
  dessinerCouche(g, m_coucheActive, 1.0);
  if (m_calc) {
    for (const auto& p : m_calc->pastilles) {
      if (p.pastille.type == TypePastille::CMS) continue;
      if (p.pastille.type == TypePastille::Traversante) {
        g.setPen(Qt::NoPen);
        g.setBrush(TRAVERSANT);
        g.drawPath(chemin({p.forme}, e));
      }
      g.setBrush(FOND);
      g.drawEllipse(ecran(p.pastille.position), mm(p.pastille.percage) / 2 * m_echelle, mm(p.pastille.percage) / 2 * m_echelle);
    }
  }
  for (const auto& v : c->circuit.vias) {
    g.setPen(Qt::NoPen);
    g.setBrush(VIA);
    g.drawEllipse(ecran(v.p), mm(v.diametre) / 2 * m_echelle, mm(v.diametre) / 2 * m_echelle);
    g.setBrush(FOND);
    g.drawEllipse(ecran(v.p), mm(v.percage) / 2 * m_echelle, mm(v.percage) / 2 * m_echelle);
  }
  // Sérigraphie et références
  for (const auto& em : c->circuit.empreintes) {
    const DefEmpreinte* d = c->empreinte(em.empreinte);
    if (!d) continue;
    for (const auto& t : d->traits) {
      const Couche cs = coucheSurCarte(em, t.couche);
      if ((cs != Couche::SerigraphieDessus && cs != Couche::SerigraphieDessous) || !coucheVisible(cs)) continue;
      g.setPen(QPen(cs == Couche::SerigraphieDessus ? SERIG_DESSUS : SERIG_DESSOUS, std::max(1.0, mm(t.epaisseur) * m_echelle)));
      g.setBrush(Qt::NoBrush);
      if (t.rayon > 0) g.drawEllipse(ecran(versCarte(em, t.a)), mm(t.rayon) * m_echelle, mm(t.rayon) * m_echelle);
      else g.drawLine(ecran(versCarte(em, t.a)), ecran(versCarte(em, t.b)));
    }
  }
  // Repères, valeurs et textes de carte : les mêmes traits que dans les Gerber (police vectorielle commune, EL-06)
  for (const auto& t : textesSerigraphie(*c)) {
    if (!coucheVisible(t.couche)) continue;
    g.setPen(QPen(t.couche == Couche::SerigraphieDessous ? SERIG_DESSOUS : SERIG_DESSUS, std::max(1.0, mm(t.epaisseur) * m_echelle), Qt::SolidLine, Qt::RoundCap));
    g.drawLine(ecran(t.a), ecran(t.b));
  }
  // Dessins sur la carte (7.22) : traits, arcs, cercles, polygones, sur leur couche
  for (const auto& d : c->circuit.dessins) {
    if (!coucheVisible(d.couche) || d.points.empty()) continue;
    const QColor teinte = d.couche == Couche::SerigraphieDessous ? SERIG_DESSOUS : d.couche == Couche::SerigraphieDessus ? SERIG_DESSUS : QColor(0xD0, 0xB0, 0x60);
    g.setPen(QPen(teinte, std::max(1.0, mm(d.epaisseur) * m_echelle), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QColor plein = teinte;
    plein.setAlphaF(0.35);
    g.setBrush(d.plein ? QBrush(plein) : QBrush(Qt::NoBrush));
    QPolygonF poly;
    switch (d.forme) {
      case DessinCircuit::Forme::Trait:
        if (d.points.size() >= 2) g.drawLine(ecran(d.points[0]), ecran(d.points[1]));
        break;
      case DessinCircuit::Forme::Arc:
        if (d.points.size() == 3)
          for (const Pt q : pointsArc(d.points[0], d.points[1], d.points[2])) poly << ecran(q);
        g.drawPolyline(poly);
        break;
      case DessinCircuit::Forme::Cercle:
        if (d.points.size() >= 2) {
          const double r = distance(d.points[0], d.points[1]) * 1e-6 * m_echelle;
          g.drawEllipse(ecran(d.points[0]), r, r);
        }
        break;
      case DessinCircuit::Forme::Polygone:
        for (const Pt q : d.points) poly << ecran(q);
        g.drawPolygon(poly);
        break;
    }
  }
  g.setBrush(Qt::NoBrush);
  // Chevelu
  if (m_calc) {
    g.setPen(QPen(CHEVELU, 1));
    for (const auto& l : m_calc->etat.chevelu) g.drawLine(ecran(l.a), ecran(l.b));
  }
  const SignesCanevas& sg = theme().signesCircuit();
  // Réseau surligné : le reste s'estompe, son cuivre prend un halo (charte, S5)
  if (!m_reseau.empty()) {
    QColor voile = FOND;
    voile.setAlphaF(0.40);
    g.fillRect(rect(), voile);
    for (const auto& t : c->circuit.pistes)
      if (t.reseau == m_reseau && coucheVisible(t.couche)) {
        signes::haloTrait(g, ecran(t.a), ecran(t.b), mm(t.largeur) * m_echelle, sg.surl);
        g.setPen(QPen(couleurCuivre(t.couche), std::max(1.0, mm(t.largeur) * m_echelle), Qt::SolidLine, Qt::RoundCap));
        g.drawLine(ecran(t.a), ecran(t.b));
      }
    if (m_calc)
      for (const auto& p : m_calc->pastilles)
        if (p.reseau == m_reseau) {
          signes::haloChemin(g, chemin({p.forme}, e), sg.surl);
          g.setPen(Qt::NoPen);
          g.setBrush(p.pastille.type == TypePastille::CMS ? couleurCuivre(p.dessous ? Couche::CuivreDessous : Couche::CuivreDessus) : TRAVERSANT);
          g.drawPath(chemin({p.forme}, e));
        }
    for (const auto& v : c->circuit.vias)
      if (v.reseau == m_reseau) signes::haloPoint(g, ecran(v.p), mm(v.diametre) / 2 * m_echelle, sg.surl);
  }
  // Ce qui n'est pas choisi s'estompe (2.26, ERG-28) : un voile au niveau réglé, puis le cuivre et la sérigraphie des
  // objets choisis repeints par-dessus ; en masque et en ombre, ce qui est voilé ne se prend plus (objetSous)
  if (m_sel && m_sel->estompageActif() && !m_selection.empty()) {
    QColor voile = FOND;
    voile.setAlphaF(std::clamp(1.0 - m_sel->opaciteNonChoisi(), 0.0, 0.95));
    g.fillRect(rect(), voile);
    for (const auto& z : c->circuit.zones)
      if (estChoisi(Genre::Zone, z.uuid) && coucheVisible(z.couche) && m_calc)
        if (const auto it = m_calc->zones.find(z.uuid); it != m_calc->zones.end()) {
          QColor f = couleurCuivre(z.couche);
          f.setAlpha(150);
          g.fillPath(chemin(it->second, e), f);
        }
    for (const auto& t : c->circuit.pistes)
      if (estChoisi(Genre::Piste, t.uuid) && coucheVisible(t.couche)) {
        g.setPen(QPen(couleurCuivre(t.couche), std::max(1.0, mm(t.largeur) * m_echelle), Qt::SolidLine, Qt::RoundCap));
        g.drawLine(ecran(t.a), ecran(t.b));
      }
    g.setPen(Qt::NoPen);
    for (const auto& v : c->circuit.vias)
      if (estChoisi(Genre::Via, v.uuid)) {
        g.setBrush(VIA);
        g.drawEllipse(ecran(v.p), mm(v.diametre) / 2 * m_echelle, mm(v.diametre) / 2 * m_echelle);
        g.setBrush(FOND);
        g.drawEllipse(ecran(v.p), mm(v.percage) / 2 * m_echelle, mm(v.percage) / 2 * m_echelle);
      }
    if (m_calc)
      for (const auto& p : m_calc->pastilles)
        if (estChoisi(Genre::Empreinte, p.composant)) {
          g.setPen(Qt::NoPen);
          g.setBrush(p.pastille.type == TypePastille::CMS ? couleurCuivre(p.dessous ? Couche::CuivreDessous : Couche::CuivreDessus) : TRAVERSANT);
          g.drawPath(chemin({p.forme}, e));
        }
    for (const auto& em : c->circuit.empreintes)
      if (estChoisi(Genre::Empreinte, em.composant))
        if (const DefEmpreinte* d = c->empreinte(em.empreinte))
          for (const auto& t : d->traits) {
            const Couche cs = coucheSurCarte(em, t.couche);
            if ((cs != Couche::SerigraphieDessus && cs != Couche::SerigraphieDessous) || !coucheVisible(cs)) continue;
            g.setPen(QPen(cs == Couche::SerigraphieDessus ? SERIG_DESSUS : SERIG_DESSOUS, std::max(1.0, mm(t.epaisseur) * m_echelle)));
            g.setBrush(Qt::NoBrush);
            if (t.rayon > 0) g.drawEllipse(ecran(versCarte(em, t.a)), mm(t.rayon) * m_echelle, mm(t.rayon) * m_echelle);
            else g.drawLine(ecran(versCarte(em, t.a)), ecran(versCarte(em, t.b)));
          }
  }
  // Objets choisis : fond, contour plein, poignées
  for (const auto& em : c->circuit.empreintes)
    if (estChoisi(Genre::Empreinte, em.composant))
      if (const DefEmpreinte* d = c->empreinte(em.empreinte)) signes::selectionChemin(g, chemin({encombrementSurCarte(em, *d)}, e), sg.sel);
  for (const auto& t : c->circuit.pistes)
    if (estChoisi(Genre::Piste, t.uuid)) signes::selectionTrait(g, ecran(t.a), ecran(t.b), mm(t.largeur) * m_echelle, sg.sel);
  for (const auto& v : c->circuit.vias)
    if (estChoisi(Genre::Via, v.uuid)) {
      const double r = mm(v.diametre) / 2 * m_echelle + 2;
      signes::selectionBoite(g, QRectF(ecran(v.p) - QPointF(r, r), QSizeF(2 * r, 2 * r)), sg.sel);
    }
  for (const auto& z : c->circuit.zones)
    if (estChoisi(Genre::Zone, z.uuid)) {
      signes::selectionChemin(g, chemin({z.contour}, e), sg.sel, false);
      // Poignées des sommets : glisser pour déplacer, double-clic pour retirer ; double-clic sur un côté : ajouter
      for (const Pt& p : z.contour) signes::poignee(g, ecran(p), sg.sel);
    }
  for (const auto& t : c->circuit.textes)
    if (estChoisi(Genre::Texte, t.uuid)) {
      const Boite bt = boiteTexte(t);
      signes::selectionBoite(g, QRectF(ecran({bt.x0, bt.y0}), ecran({bt.x1, bt.y1})), sg.sel);
    }
  // Marques de la DRC : disque pour une erreur, triangle pour un avertissement
  if (m_calc)
    for (const auto& v : m_calc->drc) {
      if (v.position.x == 0 && v.position.y == 0) continue;
      const bool erreur = v.gravite == Gravite::Erreur;
      signes::marque(g, ecran(v.position), erreur ? signes::Marque::Erreur : signes::Marque::Avertissement, erreur ? sg.err : sg.avert);
    }
}

// Repères par-dessus le dessin : couche active, position de la souris, verrou, menu à marques, liseré du focus
void VueCircuit::peindreReperes(QPainter& g, const ConceptionElec& cref) {
  const ConceptionElec* c = &cref;
  // Couche active, en bas à gauche
  g.setPen(couleurCuivre(m_coucheActive));
  QFont f = font();
  f.setBold(true);
  g.setFont(f);
  g.drawText(QPointF(12, height() - 12), QString::fromUtf8(nomCouche(m_coucheActive)));
  // Position de la souris dans l'unité de la carte et pas de la grille (EL-37)
  if (rect().contains(m_souris.toPoint())) {
    const bool selection = outil() == Outil::Selection;
    const Pt w = surGrille(monde(m_souris), selection ? pasPlacement() : pasRoutage());
    const QString t = QStringLiteral("X %1   Y %2   · grille %3").arg(longueur(static_cast<double>(w.x)), longueur(static_cast<double>(w.y)), longueur(static_cast<double>(selection ? pasPlacement() : pasRoutage())));
    QFont fp = font();
    fp.setBold(false);
    g.setFont(fp);
    g.setPen(QColor(0xC9, 0xCE, 0xE6));
    g.drawText(QPointF(width() - QFontMetricsF(fp).horizontalAdvance(t) - 12, height() - 12), t);
  }
  if (c->verrouillee) {
    const QString t = QStringLiteral("🔒 Publiée %1 · révision %2 · lecture seule").arg(QString::fromStdString(c->publications.empty() ? std::string() : c->publications.back().id),
                                                                                      QString::fromStdString(c->cartouche.revision));
    QFont fb = font();
    fb.setPixelSize(12);
    fb.setBold(true);
    g.setFont(fb);
    const QRectF r(10, 10, QFontMetricsF(fb).horizontalAdvance(t) + 20, 26);
    g.setPen(Qt::NoPen);
    g.setBrush(theme().fond(QColor(0xF2, 0xF5, 0xFF, 235)));
    g.drawRoundedRect(r, 13, 13);
    g.setPen(theme().texte(QColor(0x10, 0x14, 0x2A)));
    g.drawText(r, Qt::AlignCenter, t);
  }
  m_nav->dessiner(g, m_souris);  // le menu à marques, l'annonce près du curseur
  // Le canevas qui reçoit les touches porte un liseré de 2 px à la couleur d'accent, éclaircie pour le fond sombre
  if (hasFocus()) {
    g.setRenderHint(QPainter::Antialiasing, false);
    g.setPen(QPen(Theme::pourFondSombre(theme().c().accentTexte), 2));
    g.setBrush(Qt::NoBrush);
    g.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
  }
}

// ---------- Souris et clavier ----------
void VueCircuit::mousePressEvent(QMouseEvent* ev) {
  m_outils.recycler();
  setFocus();
  m_souris = m_pression = ev->position();
  // Boutons du milieu et de droite, bouton des gestes : la navigation commune (1.19)
  if (m_nav->appui(ev)) {
    appliquerCurseur();
    return;
  }
  if (ev->button() != Qt::LeftButton) return;
  if (outil() == Outil::Selection && !(ev->modifiers() & Qt::AltModifier)) m_appuiLong->commencer(ev->position());
  m_outils.actif()->appui(geste(ev));
  majBandeau();
  appliquerCurseur();
}

void VueCircuit::mouseMoveEvent(QMouseEvent* ev) {
  m_souris = ev->position();
  if (m_nav->deplacer(ev)) {
    update();
    return;
  }
  m_outils.recycler();
  m_appuiLong->bouger(ev->position());
  if (ev->buttons() & Qt::LeftButton) m_outils.actif()->glisser(geste(ev));
  else m_outils.actif()->survol(geste(ev));
  // Pendant un glisser, le pointeur au bord fait défiler la vue
  m_nav->glisserGauche(ev->position(), (ev->buttons() & Qt::LeftButton) && m_outils.actif()->geste());
  if (m_outils.actif()->geste()) majBandeau();
  appliquerCurseur();
  if (m_calc && outil() != Outil::Router) {
    const int i = pastilleSous(monde(m_souris));
    if (i >= 0) {
      const auto& p = m_calc->pastilles[static_cast<std::size_t>(i)];
      setToolTip(QStringLiteral("%1.%2 — %3").arg(QString::fromStdString(p.reference), QString::fromStdString(p.numero), QString::fromStdString(p.reseau.empty() ? std::string("sans réseau") : p.reseau)));
    } else setToolTip(QString());
  }
  update();
}

void VueCircuit::mouseReleaseEvent(QMouseEvent* ev) {
  switch (m_nav->relacher(ev)) {
    case NavigationVue::Fin::ClicDroitCourt:
      // Clic droit sans glisser : pendant une piste, son menu (supprimer une piste gênante, via, coude, fin : ERG-21) ;
      // ailleurs, fin du geste puis de l'outil (le menu contextuel général viendra au lot L2)
      if (outil() == Outil::Router && m_outils.actif()->geste()) menuRoutage(ev->position());
      else if (const auto l = outil() == Outil::Selection ? liaisonSous(ev->position()) : std::nullopt) menuLiaison(ev->position(), *l);
      else if (const auto coin = outil() == Outil::Selection ? coinSous(ev->position()) : std::nullopt) menuCoin(ev->position(), coin->first, coin->second);
      else if (const int ie = outil() == Outil::Selection ? empreinteSous(monde(ev->position())) : -1; ie >= 0 && conception())
        menuEmpreinte(ev->position(), conception()->circuit.empreintes[static_cast<std::size_t>(ie)].composant);
      else if (!m_outils.actif()->echap() && outil() != Outil::Selection) setOutil(Outil::Selection);
      majBandeau();
      appliquerCurseur();
      return;
    case NavigationVue::Fin::Pris:
      appliquerCurseur();
      update();
      return;
    case NavigationVue::Fin::Rien: break;
  }
  if (ev->button() != Qt::LeftButton) return;
  m_nav->glisserGauche(ev->position(), false);
  m_appuiLong->arreter();
  m_outils.recycler();
  m_outils.actif()->relacher(geste(ev));
  majBandeau();
  appliquerCurseur();
  update();
}

// Sommet de la zone choisie sous la souris (à 6 pixels près)
int VueCircuit::sommetSous(QPointF q) const {
  const ConceptionElec* c = conception();
  if (!c || m_genre != Genre::Zone || m_selection.size() != 1) return -1;
  for (const auto& z : c->circuit.zones)
    if (z.uuid == m_choisi)
      for (std::size_t i = 0; i < z.contour.size(); ++i)
        if (QLineF(ecran(z.contour[i]), q).length() <= 6) return static_cast<int>(i);
  return -1;
}

// Côté de la zone choisie sous la souris : indice du sommet qui le commence, et point de la grille sur ce côté
std::pair<int, Pt> VueCircuit::areteSous(QPointF q) const {
  const ConceptionElec* c = conception();
  if (!c || m_genre != Genre::Zone) return {-1, {}};
  const Pt w = monde(q);
  for (const auto& z : c->circuit.zones)
    if (z.uuid == m_choisi)
      for (std::size_t i = 0; i < z.contour.size(); ++i) {
        const Pt a = z.contour[i], b = z.contour[(i + 1) % z.contour.size()];
        if (distancePointSegment(w, a, b) * 1e-6 * m_echelle <= 5) return {static_cast<int>(i), surGrilleRoutage(projeteSurSegment(w, a, b))};
      }
  return {-1, {}};
}

std::string VueCircuit::reseauPropose(const Polygone& contour) const {
  if (!m_calc || contour.empty()) return {};
  // Pastille sous un sommet, le premier d'abord
  for (const Pt& p : contour)
    if (const int i = pastilleSous(p); i >= 0 && !m_calc->pastilles[static_cast<std::size_t>(i)].reseau.empty()) return m_calc->pastilles[static_cast<std::size_t>(i)].reseau;
  // Sinon une masse présente sur la carte, puis le réseau qui a le plus de pastilles dans le contour
  auto masse = [](std::string n) {
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return n == "GND" || n == "0V" || n == "VSS" || n == "MASSE" || n.find("GND") != std::string::npos;
  };
  std::map<std::string, int> compte;
  for (const auto& p : m_calc->pastilles)
    if (!p.reseau.empty() && dedans(contour, p.pastille.position)) compte[p.reseau] += masse(p.reseau) ? 1000 : 1;
  std::string meilleur;
  int n = 0;
  for (const auto& [r, k] : compte)
    if (k > n) { n = k; meilleur = r; }
  return meilleur;
}

// Fermeture d'une zone, du contour de la carte ou d'une découpe
void VueCircuit::mouseDoubleClickEvent(QMouseEvent* ev) {
  if (ev->button() != Qt::LeftButton) return;
  m_outils.recycler();
  m_outils.actif()->doubleClic(geste(ev));
  majBandeau();
  update();
}

void VueCircuit::choisirChaine(const std::string& piste, bool reseauEntier) {
  const ConceptionElec* c = conception();
  if (!c) return;
  const Piste* depart = nullptr;
  for (const auto& t : c->circuit.pistes)
    if (t.uuid == piste) depart = &t;
  if (!depart) return;
  std::vector<SelectionElectronique::Ref> refs;
  if (reseauEntier) {
    for (const auto& t : c->circuit.pistes)
      if (t.reseau == depart->reseau) refs.push_back(ref({Genre::Piste, t.uuid}));
    for (const auto& v : c->circuit.vias)
      if (v.reseau == depart->reseau) refs.push_back(ref({Genre::Via, v.uuid}));
  } else {
    // Un bout est un nœud d'arrêt s'il touche une pastille, une via, ou plus d'un autre segment
    auto arret = [&](Pt p) {
      if (m_calc)
        for (const auto& pa : m_calc->pastilles)
          if ((pa.couches & bit(depart->couche)) && dedans(pa.forme, p)) return true;
      for (const auto& v : c->circuit.vias)
        if (distance(v.p, p) <= static_cast<double>(v.diametre) / 2) return true;
      return false;
    };
    std::set<std::string> vus{depart->uuid};
    std::vector<const Piste*> pile{depart};
    while (!pile.empty()) {
      const Piste* t = pile.back();
      pile.pop_back();
      refs.push_back(ref({Genre::Piste, t->uuid}));
      for (const Pt bout : {t->a, t->b}) {
        if (arret(bout)) continue;
        std::vector<const Piste*> voisins;
        for (const auto& u : c->circuit.pistes)
          if (u.uuid != t->uuid && u.couche == t->couche && u.reseau == t->reseau && (u.a == bout || u.b == bout)) voisins.push_back(&u);
        if (voisins.size() != 1) continue;  // embranchement ou extrémité libre
        if (vus.insert(voisins.front()->uuid).second) pile.push_back(voisins.front());
      }
    }
  }
  m_sel->choisir(refs);
  m_sel->surligner(depart->reseau);
  if (majSelectionVue()) emit selectionChangee();
}

void VueCircuit::leaveEvent(QEvent*) { update(); }

void VueCircuit::wheelEvent(QWheelEvent* ev) {
  m_nav->molette(ev, m_origine, m_echelle, 1.0, 400.0);
  update();
}

// Une frappe désigne des actions du registre ; l'outil armé passe d'abord (V, PageUp, Retour arrière d'un tracé ;
// flèches, Suppr, R, F de la sélection), puis la vue. Une frappe que rien ne prend remonte à l'atelier.
void VueCircuit::keyPressEvent(QKeyEvent* ev) {
  m_outils.recycler();
  const RegistreActions& r = RegistreActions::atelierElectronique();
  std::vector<const ActionAtelier*> actions;
  // Suite en attente : la seconde touche la complète ; sinon elle vaut seule (ERG-03)
  if (const auto premiere = m_nav->prefixe()) {
    actions = r.pourSuite(PorteeAction::Circuit, *premiere, ev->keyCombination());
    m_nav->finirSuite();
  }
  if (actions.empty()) actions = r.pour(PorteeAction::Circuit, *ev);
  if (toucheAction(actions)) return;
  // Première touche d'une suite : elle attend la seconde, les suites possibles annoncées près du curseur
  if (const auto suites = r.suites(PorteeAction::Circuit, ev->keyCombination()); !suites.empty()) {
    m_nav->attendreSuite(ev->keyCombination(), NavigationVue::texteSuites(ev->keyCombination(), suites));
    return;
  }
  QWidget::keyPressEvent(ev);
}

// Une frappe désigne ces actions : une touche qui ouvre un menu l'ouvre au curseur ; l'outil armé passe d'abord ; avec
// le réglage « Premier appui : agir au curseur », une touche d'outil agit là où est le curseur ; puis la vue. Une
// bascule faite au clavier s'annonce près du curseur (ERG-03).
bool VueCircuit::toucheAction(const std::vector<const ActionAtelier*>& actions) {
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

void VueCircuit::annoncerBascule(const ActionAtelier& a) {
  QString texte = a.nomAffiche();
  for (const ActionBandeau& x : m_outils.actif()->bandeau().actions)
    if (x.action == a.identifiant) texte = x.libelle;
  m_nav->annoncer(texte);
}

void VueCircuit::ouvrirMenuTouche(const ActionAtelier& a) {
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

// ---------- Plan de montage ----------
bool VueCircuit::exporterPlanMontage(const QString& chemin) const {
  const ConceptionElec* c = conception();
  if (!c) return false;
  const auto pp = pastilles(*c, connectivite(*c));
  QPdfWriter pdf(chemin);
  pdf.setResolution(300);
  pdf.setTitle(QString::fromStdString(c->nom) + QStringLiteral(" — plan de montage"));
  pdf.setCreator(QStringLiteral("Prométhée"));
  pdf.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Landscape, QMarginsF(0, 0, 0, 0)));
  QPainter g;
  if (!g.begin(&pdf)) return false;
  g.setRenderHint(QPainter::Antialiasing);
  const double u = g.viewport().width() / 297.0;  // points de la page par millimètre
  auto P = [u](double x, double y) { return QPointF(x * u, y * u); };
  auto police = [&](double tailleMm, bool gras = false) {
    QFont f(QStringLiteral("Sans"));
    f.setPixelSize(std::max(1, static_cast<int>(tailleMm * u)));
    f.setBold(gras);
    g.setFont(f);
  };
  const QColor ENCRE_P(0x10, 0x14, 0x2A), GRIS(0x8A, 0x91, 0xAC), PASTILLE(0xD5, 0xD9, 0xE6);
  // Cadre et cartouche
  g.setPen(QPen(ENCRE_P, 0.35 * u));
  g.setBrush(Qt::NoBrush);
  g.drawRect(QRectF(P(10, 10), P(287, 200)));
  const QRectF ca(P(167, 178), P(287, 200));
  g.setBrush(QColor(0xFA, 0xFB, 0xFE));
  g.drawRect(ca);
  g.drawLine(P(167, 189), P(287, 189));
  for (double x : {207.0, 237.0, 262.0}) g.drawLine(P(x, 189), P(x, 200));
  g.setPen(ENCRE_P);
  police(4.2, true);
  g.drawText(QRectF(P(170, 178), P(284, 189)), Qt::AlignLeft | Qt::AlignVCenter, QString::fromStdString(c->cartouche.titre.empty() ? c->nom : c->cartouche.titre));
  police(2.2);
  g.setPen(QColor(0x5B, 0x4B, 0xF5));
  g.drawText(QRectF(P(170, 178), P(284, 189)), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("Plan de montage · Prométhée"));
  const std::pair<QString, QString> cases[] = {{QStringLiteral("Société"), QString::fromStdString(c->cartouche.societe)},
                                               {QStringLiteral("Auteur"), QString::fromStdString(c->cartouche.auteur)},
                                               {QStringLiteral("Révision"), QString::fromStdString(c->cartouche.revision)},
                                               {QStringLiteral("Date"), QString::fromStdString(c->cartouche.date)}};
  const double xs[] = {167, 207, 237, 262, 287};
  for (int i = 0; i < 4; ++i) {
    police(1.8);
    g.setPen(GRIS);
    g.drawText(QRectF(P(xs[i] + 1.5, 189.5), P(xs[i + 1] - 1, 193.5)), Qt::AlignLeft | Qt::AlignVCenter, cases[i].first);
    police(2.6);
    g.setPen(ENCRE_P);
    g.drawText(QRectF(P(xs[i] + 1.5, 193.5), P(xs[i + 1] - 1, 199)), Qt::AlignLeft | Qt::AlignVCenter, cases[i].second);
  }
  // Échelle commune aux deux faces, choisie parmi les échelles usuelles
  const Polygone contour = c->circuit.contour.polygone();
  Boite b = boite(contour);
  for (const auto& e : c->circuit.empreintes)
    if (const DefEmpreinte* d = c->empreinte(e.empreinte)) b.etendre(boite(encombrementSurCarte(e, *d)));
  const double lb = std::max(1.0, mm(b.largeur())), hb = std::max(1.0, mm(b.hauteur()));
  const double maxi = std::min(118.0 / lb, 138.0 / hb);
  double echelle = 0.1;
  for (double e : {10.0, 5.0, 4.0, 3.0, 2.5, 2.0, 1.5, 1.0, 0.75, 0.5, 0.25, 0.2, 0.1})
    if (e <= maxi) { echelle = e; break; }
  const Pt centre = b.centre();
  auto face = [&](bool dessous, double cx, double cy) {
    // Vue par-dessous : symétrie gauche-droite, comme on tient la carte retournée
    auto Q = [&](Pt p) {
      const double x = (mm(p.x) - mm(centre.x)) * echelle * (dessous ? -1 : 1), y = (mm(p.y) - mm(centre.y)) * echelle;
      return P(cx + x, cy + y);
    };
    auto cheminP = [&](const Polygone& poly) {
      QPainterPath ch;
      if (poly.size() < 3) return ch;
      ch.moveTo(Q(poly.front()));
      for (std::size_t i = 1; i < poly.size(); ++i) ch.lineTo(Q(poly[i]));
      ch.closeSubpath();
      return ch;
    };
    const Couche cuivre = dessous ? Couche::CuivreDessous : Couche::CuivreDessus;
    const Couche serig = dessous ? Couche::SerigraphieDessous : Couche::SerigraphieDessus;
    g.setPen(QPen(ENCRE_P, 0.35 * u));
    g.setBrush(Qt::white);
    g.drawPath(cheminP(contour));
    // Pastilles de la face, trous
    g.setPen(QPen(GRIS, 0.08 * u));
    g.setBrush(PASTILLE);
    for (const auto& p : pp)
      if (p.couches & bit(cuivre)) g.drawPath(cheminP(p.forme));
    g.setPen(QPen(ENCRE_P, 0.1 * u));
    g.setBrush(Qt::white);
    for (const auto& p : pp)
      if (p.pastille.percage > 0) {
        const double r = mm(p.pastille.percage) / 2 * echelle * u;
        g.drawEllipse(Q(p.pastille.position), r, r);
      }
    for (const auto& v : c->circuit.vias) {
      const double r = mm(v.percage) / 2 * echelle * u;
      g.setPen(QPen(GRIS, 0.06 * u));
      g.drawEllipse(Q(v.p), r, r);
    }
    // Sérigraphie, encombrements et repères des composants de cette face
    for (const auto& e : c->circuit.empreintes) {
      const DefEmpreinte* d = c->empreinte(e.empreinte);
      if (!d || e.dessous != dessous) continue;
      g.setPen(QPen(ENCRE_P, std::max(0.12, mm(nm(0.15)) * echelle) * u));
      g.setBrush(Qt::NoBrush);
      for (const auto& t : d->traits) {
        if (coucheSurCarte(e, t.couche) != serig) continue;
        if (t.rayon > 0) {
          const double r = mm(t.rayon) * echelle * u;
          g.drawEllipse(Q(versCarte(e, t.a)), r, r);
        } else g.drawLine(Q(versCarte(e, t.a)), Q(versCarte(e, t.b)));
      }
      g.setPen(QPen(GRIS, 0.08 * u, Qt::DashLine));
      g.drawPath(cheminP(encombrementSurCarte(e, *d)));
      if (const ComposantElec* x = c->composant(e.composant)) {
        const double cote = std::min(mm(d->encombrement.largeur()), mm(d->encombrement.hauteur())) * echelle;
        // Repère et valeur, jamais sous 1,4 et 1,2 mm de haut à l'impression (EL-45)
        const double tRef = std::clamp(cote * 0.38, 1.4, 4.0), tVal = std::clamp(cote * 0.28, 1.2, 3.0);
        police(tRef, true);
        g.setPen(ENCRE_P);
        const QPointF q = Q(e.position);
        const bool valeur = !x->valeur.empty();
        g.drawText(QRectF(q.x() - 40 * u, q.y() - 6 * u - (valeur ? tVal * u * 0.6 : 0), 80 * u, 12 * u), Qt::AlignCenter, QString::fromStdString(x->reference));
        if (valeur) {
          police(tVal);
          g.setPen(QColor(0x4A, 0x52, 0x70));
          g.drawText(QRectF(q.x() - 40 * u, q.y() - 6 * u + tRef * u * 0.7, 80 * u, 12 * u), Qt::AlignCenter, QString::fromStdString(x->valeur));
        }
      }
    }
    police(3.0, true);
    g.setPen(ENCRE_P);
    g.drawText(QRectF(P(cx - 65, 14), P(cx + 65, 21)), Qt::AlignCenter, dessous ? QStringLiteral("Face dessous (vue par-dessous)") : QStringLiteral("Face dessus"));
    // Cotes d'encombrement de la carte
    const Boite bc = boite(contour);
    const QPointF a = Q({dessous ? bc.x1 : bc.x0, bc.y1}), bq = Q({dessous ? bc.x0 : bc.x1, bc.y1});
    const double yCote = std::max(a.y(), bq.y()) + 6 * u;
    g.setPen(QPen(GRIS, 0.15 * u));
    g.drawLine(QPointF(a.x(), a.y() + 1 * u), QPointF(a.x(), yCote + 1.5 * u));
    g.drawLine(QPointF(bq.x(), bq.y() + 1 * u), QPointF(bq.x(), yCote + 1.5 * u));
    g.setPen(QPen(ENCRE_P, 0.18 * u));
    g.drawLine(QPointF(a.x(), yCote), QPointF(bq.x(), yCote));
    police(2.6);
    g.drawText(QRectF(QPointF(std::min(a.x(), bq.x()), yCote - 5 * u), QPointF(std::max(a.x(), bq.x()), yCote - 0.5 * u)), Qt::AlignCenter,
               QLocale().toString(mm(bc.largeur()), 'f', 2) + QStringLiteral(" mm"));
    const QPointF h0 = Q({dessous ? bc.x1 : bc.x0, bc.y0}), h1 = Q({dessous ? bc.x1 : bc.x0, bc.y1});
    const double xCote = std::min(h0.x(), h1.x()) - 6 * u;
    g.setPen(QPen(GRIS, 0.15 * u));
    g.drawLine(QPointF(h0.x() - 1 * u, h0.y()), QPointF(xCote - 1.5 * u, h0.y()));
    g.drawLine(QPointF(h1.x() - 1 * u, h1.y()), QPointF(xCote - 1.5 * u, h1.y()));
    g.setPen(QPen(ENCRE_P, 0.18 * u));
    g.drawLine(QPointF(xCote, h0.y()), QPointF(xCote, h1.y()));
    g.save();
    g.translate(xCote - 1 * u, (h0.y() + h1.y()) / 2);
    g.rotate(-90);
    g.drawText(QRectF(-30 * u, -4.5 * u, 60 * u, 4 * u), Qt::AlignCenter, QLocale().toString(mm(bc.hauteur()), 'f', 2) + QStringLiteral(" mm"));
    g.restore();
  };
  face(false, 79, 95);
  face(true, 218, 95);
  police(2.4);
  g.setPen(GRIS);
  const auto& em = c->circuit.empilage;
  g.drawText(QRectF(P(14, 178), P(160, 199)), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
             QStringLiteral("Échelle %1:1 · %2 composant(s) · carte %3 couches, %4 mm, vernis %5 · repères en gras, encombrements en pointillé")
                 .arg(QLocale().toString(echelle, 'g', 3))
                 .arg(c->circuit.empreintes.size())
                 .arg(em.couchesCuivre)
                 .arg(QLocale().toString(em.epaisseur, 'f', 1), QString::fromStdString(em.vernis)));
  g.end();
  return true;
}

// ---------- Placement et liaisons (lot L5) ----------
std::string VueCircuit::repereSous(QPointF q) const {
  const ConceptionElec* c = conception();
  if (!c || !c->circuit.serigraphie.reperes) return {};
  const Pt w = monde(q);
  const Nm h = std::max<Nm>(c->circuit.serigraphie.hauteur, 500'000);
  for (const auto& e : c->circuit.empreintes) {
    const ComposantElec* k = c->composant(e.composant);
    const DefEmpreinte* d = c->empreinte(e.empreinte);
    if (!k || !d || k->reference.empty() || !coucheVisible(e.dessous ? Couche::SerigraphieDessous : Couche::SerigraphieDessus)) continue;
    const Pt centre = positionRepere(e, *d, c->circuit.serigraphie.hauteur);
    const Nm demiL = static_cast<Nm>(static_cast<double>(k->reference.size()) * static_cast<double>(h) * 0.42) + 100'000, demiH = h * 6 / 10;
    if (std::abs(w.x - centre.x) <= demiL && std::abs(w.y - centre.y) <= demiH) return e.composant;
  }
  return {};
}

std::optional<Liaison> VueCircuit::liaisonSous(QPointF q) const {
  if (!m_calc) return std::nullopt;
  const double tolerance = 5.0 / m_echelle * 1e6;
  const Pt w = monde(q);
  std::optional<Liaison> r;
  double mieux = tolerance;
  for (const auto& l : m_calc->etat.chevelu) {
    const double d = distancePointSegment(w, l.a, l.b);
    if (d <= mieux) {
      mieux = d;
      r = l;
    }
  }
  return r;
}

std::string VueCircuit::routerLiaisonChoisie(const Liaison& l) {
  const ConceptionElec* c = conception();
  if (!c || refuseVerrou()) return "la conception ne se modifie pas";
  ConceptionElec essai = *c;
  const std::string raison = routerLiaison(essai, l);
  if (!raison.empty()) {
    emit message(QStringLiteral("<span style='color:#C2402E'><b>Liaison %1 restée au chevelu</b></span> : %2.").arg(QString::fromStdString(l.reseau).toHtmlEscaped(), QString::fromStdString(raison).toHtmlEscaped()));
    return raison;
  }
  modifier([&](ConceptionElec& cc) { cc.circuit.pistes = essai.circuit.pistes; });
  emit message(QStringLiteral("Liaison <b>%1</b> routée.").arg(QString::fromStdString(l.reseau).toHtmlEscaped()));
  return {};
}

void VueCircuit::menuLiaison(QPointF q, const Liaison& l) {
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("menuLiaison"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  menu->addSection(QStringLiteral("Liaison %1").arg(QString::fromStdString(l.reseau)));
  QAction* router = menu->addAction(QStringLiteral("Router cette liaison"), this, [this, l] { routerLiaisonChoisie(l); });
  router->setObjectName(QStringLiteral("routerLiaison"));
  menu->addAction(QStringLiteral("Surligner le réseau"), this, [this, l] { setReseauSurligne(l.reseau); });
  menu->popup(mapToGlobal(q.toPoint()));
}

int VueCircuit::poserGroupe(const std::vector<std::string>& composants, Pt coin) {
  if (composants.empty() || refuseVerrou()) return 0;
  int n = 0;
  modifier([&](ConceptionElec& c) { n = placerGroupe(c, composants, surGrille(coin, pasPlacement())); });
  emit message(QStringLiteral("%1 composant(s) posé(s) sur la carte.").arg(n));
  return n;
}

// ---------- Dessins (lot L7) ----------
std::optional<std::pair<std::string, std::string>> VueCircuit::coinSous(QPointF q) const {
  const ConceptionElec* c = conception();
  if (!c) return std::nullopt;
  const Pt w = monde(q);
  const double tolerance = 6.0 / m_echelle * 1e6;
  const auto& d = c->circuit.dessins;
  for (std::size_t i = 0; i < d.size(); ++i)
    for (std::size_t j = i + 1; j < d.size(); ++j) {
      if (d[i].forme != DessinCircuit::Forme::Trait || d[j].forme != DessinCircuit::Forme::Trait || !coucheVisible(d[i].couche)) continue;
      Pt coin;
      if (coinCommun(d[i], d[j], coin) && distance(coin, w) <= tolerance) return std::make_pair(d[i].uuid, d[j].uuid);
    }
  return std::nullopt;
}

std::string VueCircuit::poserCongeOuChanfrein(const std::string& a, const std::string& b, Nm r, bool chanfrein) {
  if (refuseVerrou()) return {};
  std::string raison, id;
  const ConceptionElec* c = conception();
  if (!c) return {};
  Circuit essai = c->circuit;
  id = chanfrein ? poserChanfrein(essai, a, b, r, &raison) : poserConge(essai, a, b, r, &raison);
  if (id.empty()) {
    emit message(QStringLiteral("<span style='color:#C2402E'>%1 impossible</span> : %2.").arg(chanfrein ? QStringLiteral("Chanfrein") : QStringLiteral("Congé"), QString::fromStdString(raison).toHtmlEscaped()));
    return {};
  }
  modifier([&](ConceptionElec& cc) { cc.circuit.dessins = essai.dessins; });
  emit message(QStringLiteral("%1 de %2 mm posé.").arg(chanfrein ? QStringLiteral("Chanfrein") : QStringLiteral("Congé"), QLocale(QLocale::French).toString(mm(r), 'g', 4)));
  return id;
}

void VueCircuit::menuCoin(QPointF q, const std::string& a, const std::string& b) {
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("menuCoin"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  menu->addSection(QStringLiteral("Coin de deux traits"));
  for (const double r : {0.5, 1.0, 2.0, 5.0}) {
    QAction* x = menu->addAction(QStringLiteral("Congé de %1 mm").arg(QLocale(QLocale::French).toString(r)), this, [this, a, b, r] { poserCongeOuChanfrein(a, b, nm(r), false); });
    x->setObjectName(QStringLiteral("conge:%1").arg(r));
  }
  menu->addAction(QStringLiteral("Congé d’un autre rayon…"), this, [this, a, b] {
    bool ok = false;
    const double r = QInputDialog::getDouble(this, QStringLiteral("Congé"), QStringLiteral("Rayon (mm)"), 2, 0.01, 1000, 3, &ok);
    if (ok) poserCongeOuChanfrein(a, b, nm(r), false);
  });
  menu->addAction(QStringLiteral("Chanfrein de 1 mm"), this, [this, a, b] { poserCongeOuChanfrein(a, b, nm(1), true); });
  menu->popup(mapToGlobal(q.toPoint()));
}

int VueCircuit::contourAutour(const std::vector<std::string>& composants, Nm d, Couche couche) {
  const ConceptionElec* c = conception();
  if (!c || refuseVerrou()) return 0;
  Polygones p;
  for (const auto& id : composants)
    for (const auto& x : pastillesDe(*c, id)) p.push_back(x);
  const auto contours = contourDecale(p, d, couche);
  if (contours.empty()) return 0;
  modifier([&](ConceptionElec& cc) {
    for (const auto& x : contours) cc.circuit.dessins.push_back(x);
  });
  emit message(QStringLiteral("Contour décalé de %1 mm : %2 polygone(s).").arg(QLocale(QLocale::French).toString(mm(d), 'g', 4)).arg(contours.size()));
  return static_cast<int>(contours.size());
}

void VueCircuit::menuEmpreinte(QPointF q, const std::string& composant) {
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("menuEmpreinte"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  const ConceptionElec* c = conception();
  const ComposantElec* k = c ? c->composant(composant) : nullptr;
  menu->addSection(k ? QString::fromStdString(k->reference) : QStringLiteral("Empreinte"));
  // Les empreintes choisies, ou celle-ci
  std::vector<std::string> ids;
  for (const auto& o : m_selection)
    if (o.genre == Genre::Empreinte) ids.push_back(o.id);
  if (std::find(ids.begin(), ids.end(), composant) == ids.end()) ids = {composant};
  QAction* x = menu->addAction(QStringLiteral("Contour décalé de 0,25 mm autour des pastilles"), this, [this, ids] { contourAutour(ids, 250'000, Couche::EncombrementDessus); });
  x->setObjectName(QStringLiteral("contourPastilles"));
  menu->popup(mapToGlobal(q.toPoint()));
}

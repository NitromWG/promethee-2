// Prométhée : inspecteur de l'atelier électronique (lot L1, tâche 1.9). Voir InspecteurElectronique.hpp.
// Licence GPL-3.0-only.
#include "InspecteurElectronique.hpp"

#include "PanneauVerifications.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QFocusEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "Theme.hpp"
#include "VueSchema.hpp"
#include "promethee/valeurs.hpp"
#include "promethee/commandes.hpp"
#include "promethee/electronique.hpp"

using namespace prom::ecad;

namespace teintes {
QString secondaire() { return Theme::instance().c().texteSecondaire.name(); }
QString erreur() { return Theme::instance().c().erreur.name(); }
QString alerte() { return Theme::instance().c().alerte.name(); }
QString succes() { return Theme::instance().c().succes.name(); }
QString lien() { return Theme::instance().c().accentTexte.name(); }
}  // namespace teintes

namespace {
std::uint64_t g_saisies = 0;

// Le focus du clavier est dans ce widget (ou une liste déroulante ouverte lui appartient)
bool focusDedans(const QWidget* w) {
  const QWidget* f = QApplication::focusWidget();
  if (f && (f == w || w->isAncestorOf(f))) return true;
  if (const auto* c = qobject_cast<const QComboBox*>(w); c && c->view() && c->view()->isVisible()) return true;
  return false;
}

void repolir(QWidget* w) {
  w->style()->unpolish(w);
  w->style()->polish(w);
  w->update();
}
}  // namespace

// ---------- Outils communs aux pages ----------
namespace inspecteur {
QLabel* texte(const QString& html) {
  auto* l = new QLabel(html);
  l->setWordWrap(true);
  l->setTextFormat(Qt::RichText);
  l->setProperty("themeFixe", true);
  return l;
}

std::string signature(const std::vector<Violation>& v) {
  std::string s = std::to_string(v.size());
  for (const auto& x : v) s += '|' + x.code + ':' + x.message;
  return s;
}

void listeProblemes(QVBoxLayout* col, const std::vector<Violation>& v, bool schema, AtelierElectronique& a) {
  // Les premiers problèmes seulement : chaque ligne est un texte riche que la mise en page du panneau mesure à
  // chaque passage ; quatre-vingts lignes coûtaient plus de 100 ms au passage du schéma au circuit (ERG-10)
  constexpr std::size_t MAX = 12;
  for (std::size_t n = 0; n < v.size() && n < MAX; ++n) {
    const Violation& x = v[n];
    const bool erreur = x.gravite == Gravite::Erreur;
    const QString couleur = erreur ? teintes::erreur() : teintes::alerte();
    auto* l = texte(QStringLiteral("<span style='color:%1'>%2</span> <a href='#' style='color:%1; text-decoration:none'>%3</a>")
                        .arg(couleur, erreur ? QStringLiteral("✖") : QStringLiteral("⚠"), QString::fromStdString(x.message).toHtmlEscaped()));
    l->setToolTip(QString::fromStdString(x.conseil));
    l->setCursor(Qt::PointingHandCursor);
    l->setContentsMargins(4, 2, 4, 2);
    const Violation copie = x;
    AtelierElectronique* atelier = &a;
    // Le problème cliqué mène à sa feuille ou au circuit et choisit le bon objet, comme dans le panneau (P29, P30)
    QObject::connect(l, &QLabel::linkActivated, l, [atelier, copie, schema] {
      if (PanneauVerifications* p = atelier->verifications()) p->aller(copie, schema);
    });
    col->addWidget(l);
  }
  // Au-delà, la liste entière est au panneau Vérifications, groupée par cause
  if (v.size() > MAX) {
    auto* reste = texte(QStringLiteral("… et %1 autre(s) : <a href='#' style='color:%2'>la liste entière au panneau Vérifications</a>").arg(v.size() - MAX).arg(teintes::lien()));
    reste->setObjectName(QStringLiteral("problemesRestants"));
    reste->setContentsMargins(4, 4, 4, 2);
    AtelierElectronique* atelier = &a;
    QObject::connect(reste, &QLabel::linkActivated, reste, [atelier] { atelier->montrerVerifications(); });
    col->addWidget(reste);
  }
}

ChampTexteLie* champComposant(AtelierElectronique& a, std::function<std::string()> composant, const std::string& propriete) {
  AtelierElectronique* atelier = &a;
  auto lire = [atelier, composant, propriete]() -> QString {
    const ConceptionElec* c = atelier->conceptionCourante();
    const ComposantElec* k = c ? c->composant(composant()) : nullptr;
    if (!k) return {};
    if (propriete == "reference") return QString::fromStdString(k->reference);
    if (propriete == "valeur") return QString::fromStdString(k->valeur);
    if (propriete == "fabricant") return QString::fromStdString(k->fabricant);
    if (propriete == "mpn") return QString::fromStdString(k->mpn);
    if (propriete == "fournisseur") return QString::fromStdString(k->fournisseur);
    if (propriete == "description") return QString::fromStdString(k->description);
    if (propriete == "fiche") return QString::fromStdString(k->fiche);
    if (propriete == "lcsc") return QString::fromStdString(k->lcsc);
    return {};
  };
  auto ecrire = [atelier, composant, propriete, lire](const QString& t, std::uint64_t saisie) {
    const ConceptionElec* c = atelier->conceptionCourante();
    const ComposantElec* k = c ? c->composant(composant()) : nullptr;
    if (!k) return;
    const std::string avant = lire().toStdString(), apres = t.trimmed().toStdString();
    if (avant == apres) return;
    atelier->document()->executer(atelier->conception(), std::make_unique<CommandeProprieteComposant>(k->uuid, propriete, avant, apres, k->reference, saisie));
  };
  auto* champ = new ChampTexteLie(lire, ecrire, true);
  champ->setObjectName(QString::fromStdString("champ:" + propriete));
  if (propriete == "reference")
    champ->setValidation([atelier, composant](const QString& t) -> QString {
      const QString r = t.trimmed();
      if (r.isEmpty()) return QStringLiteral("Le repère ne peut pas être vide");
      if (const ConceptionElec* c = atelier->conceptionCourante())
        for (const auto& x : c->composants)
          if (x.uuid != composant() && QString::fromStdString(x.reference) == r && !r.contains(QLatin1Char('?'))) return QStringLiteral("%1 existe déjà").arg(r);
      return {};
    });
  // Valeur lue selon la grammaire de sa famille (3.2, P3) : une unité qui ne convient pas est refusée, avec sa raison
  if (propriete == "valeur")
    champ->setValidation([atelier, composant](const QString& t) -> QString {
      const ConceptionElec* c = atelier->conceptionCourante();
      const ComposantElec* k = c ? c->composant(composant()) : nullptr;
      if (!k || t.trimmed().isEmpty()) return {};
      const ValeurComprise v = lireValeurComposant(t.toStdString(), familleDe(*c, *k));
      return v.etat == EtatValeur::Refusee ? QString::fromStdString(v.raison) : QString();
    });
  return champ;
}
}  // namespace inspecteur

// ---------- Champ de texte lié ----------
ChampTexteLie::ChampTexteLie(Lire lire, Ecrire ecrire, bool pendantLaFrappe, QWidget* parent)
    : QLineEdit(parent), m_lire(std::move(lire)), m_ecrire(std::move(ecrire)), m_frappe(pendantLaFrappe) {
  connect(this, &QLineEdit::textEdited, this, [this] { frappe(); });
  connect(this, &QLineEdit::editingFinished, this, [this] { fin(); });
}

void ChampTexteLie::relire() {
  const QString v = m_lire ? m_lire() : QString();
  if (hasFocus()) {
    // Pendant la saisie, le texte tapé reste ; un changement venu d'ailleurs (annulation, autre vue) est signalé
    m_ailleurs = v != text() && v != m_ecrit;
    signaler();
    return;
  }
  m_ailleurs = m_refus = false;
  if (text() != v) {
    const QSignalBlocker b(this);
    setText(v);
    setCursorPosition(0);
  }
  signaler();
}

void ChampTexteLie::frappe() {
  const QString t = text();
  const QString raison = m_valider ? m_valider(t) : QString();
  m_refus = !raison.isEmpty();
  setToolTip(raison);
  if (!m_refus && m_frappe && t != m_ecrit) {
    m_ecrit = t;
    m_ecrire(t, m_saisie);
  }
  signaler();
}

void ChampTexteLie::fin() {
  if (m_refus) {
    // Un texte refusé n'est pas gardé : la valeur du modèle revient
    m_refus = false;
    setToolTip(QString());
    const QSignalBlocker b(this);
    setText(m_lire ? m_lire() : QString());
  } else if (text() != m_ecrit && !m_ailleurs) {
    m_ecrit = text();
    m_ecrire(text(), m_saisie);
  }
  // Entrée valide la saisie : elle devient la valeur de départ ; Échap, ensuite, ne la défait plus, et une nouvelle
  // frappe fait un nouveau pas (1.1, trouvé par la frappe réelle)
  if (!m_refus && !m_ailleurs) {
    m_avant = m_ecrit = text();
    m_saisie = ++g_saisies;
  }
  signaler();
}

void ChampTexteLie::renoncer() {
  const QSignalBlocker b(this);
  if (m_ailleurs) {
    // Modifié ailleurs pendant la saisie : Échap reprend la valeur actuelle du modèle
    setText(m_lire ? m_lire() : QString());
  } else {
    if (m_ecrit != m_avant) m_ecrire(m_avant, m_saisie);
    setText(m_avant);
  }
  m_ecrit = text();
  m_ailleurs = m_refus = false;
  setToolTip(QString());
  signaler();
}

void ChampTexteLie::focusInEvent(QFocusEvent* e) {
  QLineEdit::focusInEvent(e);
  m_avant = m_ecrit = m_lire ? m_lire() : text();
  m_saisie = ++g_saisies;
  m_ailleurs = m_refus = false;
  signaler();
}

void ChampTexteLie::focusOutEvent(QFocusEvent* e) {
  QLineEdit::focusOutEvent(e);
  if (m_refus) fin();
  m_ailleurs = false;
  relire();
}

void ChampTexteLie::signaler() {
  const bool a = property("ailleurs").toBool(), r = property("refus").toBool();
  if (a == m_ailleurs && r == m_refus) return;
  setProperty("ailleurs", m_ailleurs);
  setProperty("refus", m_refus);
  if (m_ailleurs) setToolTip(QStringLiteral("Modifié ailleurs pendant la saisie · Échap pour reprendre la valeur actuelle"));
  repolir(this);
}

// ---------- Page ----------
PageInspecteur::PageInspecteur(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  m_col = new QVBoxLayout(this);
  m_col->setContentsMargins(0, 0, 0, 0);
}

void PageInspecteur::lier(QWidget* w, std::function<void()> relireW, bool modifie) { m_liaisons.push_back({w, std::move(relireW), modifie}); }
void PageInspecteur::action(QAbstractButton* b) { m_actions.push_back(b); }

QLabel* PageInspecteur::entete() {
  auto* l = new QLabel;
  l->setWordWrap(true);
  l->setTextFormat(Qt::RichText);
  l->setProperty("themeFixe", true);
  l->setObjectName(QStringLiteral("enteteInspecteur"));
  return l;
}

void PageInspecteur::relire() {
  for (auto& f : m_suivis) f();
  for (auto& l : m_liaisons) {
    if (!l.widget) continue;
    // Le champ qui a le focus n'est jamais réécrit ; un champ de texte lié le sait lui-même
    if (!qobject_cast<ChampTexteLie*>(l.widget) && focusDedans(l.widget)) continue;
    const QSignalBlocker b(l.widget);
    l.relire();
  }
}

void PageInspecteur::setLectureSeule(bool oui) {
  if (oui == m_lectureSeule) return;
  m_lectureSeule = oui;
  for (auto& l : m_liaisons) {
    if (!l.widget || !l.modifie) continue;
    if (auto* t = qobject_cast<QLineEdit*>(l.widget)) t->setReadOnly(oui);
    else if (auto* p = qobject_cast<QPlainTextEdit*>(l.widget)) p->setReadOnly(oui);
    else l.widget->setEnabled(!oui);
  }
  for (auto& b : m_actions)
    if (b) b->setEnabled(!oui);
  relire();
}

// ---------- Section refaite ----------
SectionRefaite::SectionRefaite(Construire c, QWidget* parent) : QWidget(parent), m_construire(std::move(c)) {
  m_col = new QVBoxLayout(this);
  m_col->setContentsMargins(0, 0, 0, 0);
  connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* maintenant) {
    if (m_aFaire && !(maintenant && isAncestorOf(maintenant))) {
      m_aFaire = false;
      construire(m_attente);
    }
  });
}

bool SectionRefaite::refaire(const std::string& signature) {
  if (signature == m_signature) {
    m_aFaire = false;
    return true;
  }
  if (focusDedans(this)) {
    m_attente = signature;
    m_aFaire = true;
    return false;
  }
  construire(signature);
  return true;
}

namespace {
// Vide une disposition, dispositions imbriquées comprises : leurs champs sont des enfants de la section, que la
// suppression de la seule disposition laisserait en place, visibles et empilés d'une construction à l'autre
void viderDisposition(QLayout* l) {
  while (QLayoutItem* it = l->takeAt(0)) {
    if (QWidget* w = it->widget()) {
      w->hide();
      w->deleteLater();
    } else if (QLayout* sous = it->layout()) {
      viderDisposition(sous);
    }
    delete it;
  }
}
}  // namespace

void SectionRefaite::construire(const std::string& signature) {
  // Le contenu part au prochain tour de boucle : un bouton de la section qui a déclenché ce changement finit son signal
  viderDisposition(m_col);
  m_construire(m_col);
  m_signature = signature;
  for (QWidget* p = parentWidget(); p; p = p->parentWidget())
    if (auto* i = qobject_cast<InspecteurElectronique*>(p)) {
      i->sansMolette(this);
      break;
    }
}

// ---------- Bandeau d'une révision publiée ----------
BandeauPublication::BandeauPublication(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  setObjectName(QStringLiteral("verrou"));
  setAttribute(Qt::WA_StyledBackground, true);
  // Couleurs de la marque, les mêmes dans tous les thèmes : le bandeau doit se voir
  setProperty("themeFixe", true);
  setStyleSheet(QStringLiteral("#verrou { background: #10142A; border-radius: 9px; } #verrou QLabel { color: #DCE2FA; }"
                               "#verrou QPushButton { background: #2760F1; color: white; border: none; border-radius: 6px; padding: 6px 10px; text-align: center; }"
                               "#verrou QPushButton:hover { background: #3DC9FE; color: #10142A; }"));
  auto* l = new QVBoxLayout(this);
  m_texte = new QLabel;
  m_texte->setWordWrap(true);
  m_texte->setProperty("themeFixe", true);
  l->addWidget(m_texte);
  m_revision = new QPushButton;
  m_revision->setObjectName(QStringLiteral("nouvelleRevision"));
  connect(m_revision, &QPushButton::clicked, this, [this] { m_atelier.nouvelleRevision(); });
  l->addWidget(m_revision);
  hide();
}

void BandeauPublication::relire() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c || !c->verrouillee) {
    hide();
    return;
  }
  const Publication* p = c->publications.empty() ? nullptr : &c->publications.back();
  m_texte->setText(QStringLiteral("🔒 <b>Publiée %1</b> · révision %2<br><span style='color:#9AA3C7'>%3 — lecture seule</span>")
                       .arg(p ? QString::fromStdString(p->id) : QString(), QString::fromStdString(c->cartouche.revision), p ? QString::fromStdString(p->date).toHtmlEscaped() : QString()));
  m_revision->setText(QStringLiteral("Nouvelle révision %1").arg(QString::fromStdString(revisionSuivante(c->cartouche.revision))));
  show();
}

// ---------- Inspecteur ----------
InspecteurElectronique::InspecteurElectronique(AtelierElectronique& a, QWidget* parent) : QScrollArea(parent), m_atelier(a) {
  setObjectName(QStringLiteral("inspecteur"));
  setWidgetResizable(true);
  setFrameShape(QFrame::NoFrame);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);  // jamais de contenu coupé (EL-23)
  setMinimumWidth(300);
  m_contenu = new QWidget;
  m_contenu->setObjectName(QStringLiteral("panneauElectronique"));
  m_contenu->setProperty("themeFixe", true);
  m_col = new QVBoxLayout(m_contenu);
  m_bandeau = new BandeauPublication(a);
  m_col->addWidget(m_bandeau);
  m_col->addStretch(1);
  setWidget(m_contenu);
  appliquerTheme();
  connect(&Theme::instance(), &Theme::change, this, [this] {
    appliquerTheme();
    relire();
  });
}

InspecteurElectronique::~InspecteurElectronique() = default;

void InspecteurElectronique::appliquerTheme() {
  const CouleursInterface& c = Theme::instance().c();
  const QString t = QStringLiteral(
                        "#panneauElectronique { background: %1; }"
                        "#panneauElectronique QGroupBox { background: %2; border: 1px solid %3; border-radius: 9px; margin-top: 16px; padding: 12px 8px 8px 8px; color: %4; }"
                        "#panneauElectronique QGroupBox::title { subcontrol-origin: margin; left: 10px; top: 2px; padding: 0 4px; color: %5; font-weight: 600; }"
                        "#panneauElectronique QPushButton { text-align: left; padding: 6px 9px; border: 1px solid %3; border-radius: 7px; background: %6; color: %4; }"
                        "#panneauElectronique QPushButton:hover { border-color: %7; }"
                        "#panneauElectronique QPushButton:disabled { color: %8; }"
                        "#panneauElectronique QPushButton#erreur { color: %9; } #panneauElectronique QPushButton#alerte { color: %10; }"
                        "#panneauElectronique QLineEdit[refus=\"true\"] { border: 2px solid %9; }"
                        "#panneauElectronique QLineEdit[ailleurs=\"true\"] { border: 2px solid %10; }")
                        .arg(c.surface.name(), c.fenetre.name(), c.bordure.name(), c.texte.name(), c.texteSecondaire.name(), c.champ.name(), c.accentTexte.name(),
                             c.texteDesactive.name(), c.erreur.name())
                        .arg(c.alerte.name());
  m_contenu->setStyleSheet(t);
}

QString InspecteurElectronique::cleVoulue() const {
  const int mode = m_atelier.mode();
  if (mode == AtelierElectronique::Fabrication) return QStringLiteral("fabrication");
  if (mode == AtelierElectronique::Vue3D) return QStringLiteral("vue3d");
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (mode == AtelierElectronique::Schema) {
    const VueSchema* vs = m_atelier.vueSchema();
    if (c && !c->feuilles.empty() && vs->selection().size() > 1) return QStringLiteral("schema.plusieurs");  // 3.9
    if (!c || c->feuilles.empty() || vs->selection().size() != 1) return QStringLiteral("schema.feuille");
    const Feuille& f = c->feuilles[static_cast<std::size_t>(std::clamp(vs->feuille(), 0, static_cast<int>(c->feuilles.size()) - 1))];
    const std::string& id = *vs->selection().begin();
    for (const auto& s : f.symboles)
      if (s.uuid == id) {
        const DefSymbole* d = c->symbole(s.symbole);
        if (d && c->composant(s.composant)) return QStringLiteral("schema.composant");
        if (d && (d->alimentation || d->drapeau)) return QStringLiteral("schema.alimentation");
        return QStringLiteral("schema.feuille");
      }
    for (const auto& e : f.etiquettes)
      if (e.uuid == id) return QStringLiteral("schema.etiquette");
    for (const auto& t : f.textes)
      if (t.uuid == id) return QStringLiteral("schema.texte");
    for (const auto& w : f.fils)
      if (w.uuid == id) return QStringLiteral("schema.fil");
    return QStringLiteral("schema.feuille");
  }
  const VueCircuit* vc = m_atelier.vueCircuit();
  if (vc->selection().size() > 1) {
    // Que des empreintes : la page des composants (3.9) ; du cuivre mêlé : la page du cuivre
    const bool composants = std::all_of(vc->selection().begin(), vc->selection().end(), [](const auto& o) { return o.genre == VueCircuit::Genre::Empreinte; });
    return composants ? QStringLiteral("circuit.composants") : QStringLiteral("circuit.plusieurs");
  }
  switch (vc->genreChoisi()) {
    case VueCircuit::Genre::Empreinte: return c && c->composant(vc->choisi()) ? QStringLiteral("circuit.empreinte") : QStringLiteral("circuit.placement");
    case VueCircuit::Genre::Piste: return QStringLiteral("circuit.piste");
    case VueCircuit::Genre::Via: return QStringLiteral("circuit.via");
    case VueCircuit::Genre::Zone: return QStringLiteral("circuit.zone");
    case VueCircuit::Genre::Texte: return QStringLiteral("circuit.texte");
    case VueCircuit::Genre::Rien: break;
  }
  return QStringLiteral("circuit.carte");
}

PageInspecteur* InspecteurElectronique::creer(const QString& cle) {
  PageInspecteur* p = cle.startsWith(QLatin1String("schema.")) ? pageSchema(cle, m_atelier) : pageCircuit(cle, m_atelier);
  p->setObjectName(QStringLiteral("page:") + cle);
  ++m_creees;
  return p;
}

namespace {
// Une liste aux lignes longues (empreintes, articles) ne force pas la largeur de l'inspecteur : le panneau tient sur un
// écran de 1 024 px sans barre de défilement horizontale
void compacter(QWidget* page) {
  for (QComboBox* b : page->findChildren<QComboBox*>())
    if (b->sizeAdjustPolicy() == QComboBox::AdjustToContentsOnFirstShow) {
      b->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
      b->setMinimumContentsLength(12);
    }
}
}  // namespace

void InspecteurElectronique::montrer() {
  const QString cle = cleVoulue();
  PageInspecteur*& p = m_pages[cle];
  if (!p) {
    p = creer(cle);
    m_col->insertWidget(m_col->count() - 1, p);
    sansMolette(p);
  }
  if (m_page != p) {
    if (m_page) m_page->hide();
    m_page = p;
    m_cle = cle;
  }
  m_bandeau->relire();
  const ConceptionElec* c = m_atelier.conceptionCourante();
  p->montrer();
  p->setLectureSeule(c && c->verrouillee);
  compacter(p);
  p->show();
}

void InspecteurElectronique::relire() {
  m_bandeau->relire();
  if (!m_page) return;
  const ConceptionElec* c = m_atelier.conceptionCourante();
  m_page->setLectureSeule(c && c->verrouillee);
  m_page->relire();
  compacter(m_page);  // les sections refaites ont pu poser de nouvelles listes
}

void InspecteurElectronique::resultats() {
  if (!m_page) return;
  m_page->resultats();
}

void InspecteurElectronique::oublier(const QString& cle) {
  const auto it = m_pages.find(cle);
  if (it == m_pages.end() || !it->second) return;
  if (m_page == it->second) {
    m_page = nullptr;
    m_cle.clear();
  }
  it->second->hide();
  it->second->deleteLater();
  m_pages.erase(it);
}

// La molette ne change pas un champ qui n'a pas le focus : elle fait défiler le panneau (P16)
void InspecteurElectronique::sansMolette(QWidget* racine) {
  auto poser = [this](QWidget* w) {
    if (w->property("sansMolette").toBool()) return;
    w->setProperty("sansMolette", true);
    w->setFocusPolicy(Qt::StrongFocus);
    w->installEventFilter(this);
  };
  for (QComboBox* c : racine->findChildren<QComboBox*>()) poser(c);
  for (QAbstractSpinBox* s : racine->findChildren<QAbstractSpinBox*>()) poser(s);
}

bool InspecteurElectronique::eventFilter(QObject* o, QEvent* e) {
  // Seulement les champs marqués : la zone défilante filtre aussi ses propres barres
  if (e->type() == QEvent::Wheel && o->property("sansMolette").toBool())
    if (auto* w = qobject_cast<QWidget*>(o); w && !w->hasFocus()) {
      QCoreApplication::sendEvent(verticalScrollBar(), e);
      return true;
    }
  return QScrollArea::eventFilter(o, e);
}

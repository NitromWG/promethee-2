// Prométhée : menu contextuel du schéma et édition sur place des textes (lot L2, tâches 2.5 et 2.6, SAI-13, SAI-14,
// SAI-20, S7, S8). Le clic droit court ouvre le menu de l'objet visé, d'une broche, du vide ou de l'outil armé ;
// le double-clic sur un repère ou une valeur ouvre un champ sur la feuille, Tab passe de l'un à l'autre ; un champ
// se prend à la souris et garde sa place. Méthodes de VueSchema, à part pour la lisibilité.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cctype>
#include <cmath>

#include <QApplication>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLineEdit>
#include <QGuiApplication>
#include <QMenu>
#include <QTimer>
#include <QToolButton>

#include "BandeauOutil.hpp"
#include "Document.hpp"
#include "Icones.hpp"
#include "RegistreActions.hpp"
#include "RegistreReglages.hpp"
#include "SelectionElectronique.hpp"
#include "VueSchema.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
// Ligne de menu qui fait une action du registre : son nom, sa première touche, marquée pour l'essai « la souris
// suffit » (ERG-17)
QAction* ligneAction(QMenu* m, const char* action, const QString& libelle, std::function<void()> faire) {
  const QString touche = RegistreActions::atelierElectronique().touches(action).section(QStringLiteral(", "), 0, 0);
  QAction* a = m->addAction(touche.isEmpty() ? libelle : libelle + QLatin1Char('\t') + touche);
  marquerAction(a, action);
  QObject::connect(a, &QAction::triggered, m, std::move(faire));
  return a;
}
}  // namespace

// Renommer un réseau (2.20) : le champ sur place, prérempli du nom ; un nom déjà pris s'annonce et demande une
// seconde Entrée, les deux réseaux devenant un
void VueSchema::renommerReseauSurPlace(const std::string& ancien, Pt ou, int portee) {
  saisirSurPlace(ou, portee == 0 ? QStringLiteral("Nom de ce tronçon") : portee == 1 ? QStringLiteral("Nouveau nom du réseau sur cette feuille") : QStringLiteral("Nouveau nom du réseau, toutes feuilles"),
                 QString::fromStdString(ancien), [this, ancien, ou, portee](const QString& saisi) {
                   const std::string nouveau = saisi.trimmed().toStdString();
                   if (nouveau.empty() || nouveau == ancien) return;
                   // Un nom déjà pris : les deux réseaux seront reliés ; la même saisie validée une seconde fois le confirme
                   bool pris = false;
                   if (m_k)
                     for (const auto& r : m_k->reseaux) pris |= r.nom == nouveau;
                   if (pris && m_renommageConfirme != nouveau) {
                     m_renommageConfirme = nouveau;
                     emit message(QStringLiteral("<b>%1</b> existe déjà, les deux réseaux seront reliés · Entrée pour renommer quand même, Échap pour annuler").arg(saisi.trimmed().toHtmlEscaped()));
                     QMetaObject::invokeMethod(this, [this, ancien, ou, portee, saisi] {
                       renommerReseauSurPlace(ancien, ou, portee);
                       if (m_saisie) m_saisie->setText(saisi);
                     }, Qt::QueuedConnection);
                     return;
                   }
                   m_renommageConfirme.clear();
                   const int fi = m_feuille;
                   int n = 0;
                   modifier([&](Feuille& f, ConceptionElec& cc) {
                     if (portee > 0) n = renommerReseau(cc, ancien, nouveau, portee == 1 ? fi : -1);
                     // Ce tronçon, ou un réseau qui n'avait pas encore de nom posé : une étiquette au point visé
                     if (n == 0) {
                       Etiquette e;
                       e.uuid = nouvelUuid();
                       e.texte = nouveau;
                       e.p = ou;
                       f.etiquettes.push_back(e);
                       n = 1;
                     }
                   }, {}, QStringLiteral("Renommer %1 en %2").arg(QString::fromStdString(ancien), QString::fromStdString(nouveau)));
                   emit message(QStringLiteral("%1 renommé en <b>%2</b> (%3 objet(s)).").arg(QString::fromStdString(ancien).toHtmlEscaped(), QString::fromStdString(nouveau).toHtmlEscaped()).arg(n));
                 });
}

// Renvoyer vers une autre feuille (2.20, SAI-31) : un connecteur hors feuille au nom du réseau, au bout libre du fil
// le plus proche du point visé (sinon au point visé)
void VueSchema::renvoyerFil(const std::string& fil, const std::string& reseau, Pt ou) {
  const Feuille* f = feuilleCourante();
  if (!f) return;
  std::set<Pt> libres;
  if (m_k)
    if (const auto it = m_k->extremitesLibres.find(f->uuid); it != m_k->extremitesLibres.end()) libres.insert(it->second.begin(), it->second.end());
  Pt p = ou;
  for (const auto& w : f->fils)
    if (w.uuid == fil) {
      const bool la = libres.count(w.a) > 0, lb = libres.count(w.b) > 0;
      if (la && lb) p = distance(w.a, ou) <= distance(w.b, ou) ? w.a : w.b;
      else if (la) p = w.a;
      else if (lb) p = w.b;
    }
  modifier([&](Feuille& fe, ConceptionElec&) {
    Etiquette e;
    e.uuid = nouvelUuid();
    e.texte = reseau;
    e.p = p;
    e.type = TypeEtiquette::HorsFeuille;
    fe.etiquettes.push_back(e);
  }, {}, QStringLiteral("Renvoi %1").arg(QString::fromStdString(reseau)));
}

std::optional<VueSchema::BrocheVisee> VueSchema::brocheLibreSous(QPointF q) const {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f) return std::nullopt;
  for (const auto& s : f->symboles) {
    const DefSymbole* d = c->symbole(s.symbole);
    if (!d || s.composant.empty()) continue;
    for (const auto& b : brochesPlacees(s, *d)) {
      const QPointF p = ecran(b.p);
      if (std::hypot(p.x() - q.x(), p.y() - q.y()) > 8) continue;
      // Libre : aucun fil n'y arrive ni n'y passe, aucune étiquette, aucune marque, aucun autre réseau nommé
      bool touche = false;
      for (const auto& w : f->fils) touche |= w.a == b.p || w.b == b.p || surSegment(b.p, w.a, w.b);
      for (const auto& e : f->etiquettes) touche |= e.p == b.p;
      for (const auto& n : f->nonConnexions) touche |= n.p == b.p;
      if (m_k)
        if (const int r = m_k->reseau(s.composant, b.numero); r >= 0) touche |= m_k->reseaux[static_cast<std::size_t>(r)].broches.size() > 1 || m_k->reseaux[static_cast<std::size_t>(r)].nomme;
      if (touche) return std::nullopt;
      return BrocheVisee{s.uuid, s.composant, b.numero, b.broche->nom, b.p, b.direction};
    }
  }
  return std::nullopt;
}

void VueSchema::relierDepuis(const std::string& composant, const std::string& numero) {
  const ConceptionElec* c = conception();
  const ComposantElec* k = c ? c->composant(composant) : nullptr;
  if (!k) return;
  if (outil() != Outil::Selection) setOutil(Outil::Selection);
  m_relierDepuis = k->reference + ":" + numero;
  emit message(QStringLiteral("Relier <b>%1</b> à… : clique sur l’autre broche ; Échap renonce.").arg(QString::fromStdString(m_relierDepuis).toHtmlEscaped()));
  majBandeau();
  update();
}

bool VueSchema::relierVers(QPointF q) {
  const std::string de = std::exchange(m_relierDepuis, std::string());
  majBandeau();
  const auto b = brocheSous(q);
  const ConceptionElec* c = conception();
  const ComposantElec* k = b && c ? c->composant(b->composant) : nullptr;
  if (!k || refuseVerrou()) return false;
  const std::string vers = k->reference + ":" + b->numero;
  if (vers == de) return false;
  int segments = -1;
  modifier([&](Feuille&, ConceptionElec& cc) { segments = relierBroches(cc, de, vers); }, {}, QStringLiteral("Relier %1 à %2").arg(QString::fromStdString(de), QString::fromStdString(vers)));
  if (segments < 0)
    emit message(QStringLiteral("Pas de chemin libre de %1 à %2 · Trace le fil à la main.").arg(QString::fromStdString(de).toHtmlEscaped(), QString::fromStdString(vers).toHtmlEscaped()));
  update();
  return segments >= 0;
}

std::optional<VueSchema::BrocheVisee> VueSchema::brocheSous(QPointF q) const {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f) return std::nullopt;
  for (const auto& s : f->symboles) {
    const DefSymbole* d = c->symbole(s.symbole);
    if (!d || s.composant.empty()) continue;
    for (const auto& b : brochesPlacees(s, *d)) {
      const QPointF p = ecran(b.p);
      if (std::hypot(p.x() - q.x(), p.y() - q.y()) <= 6) return BrocheVisee{s.uuid, s.composant, b.numero, b.broche->nom, b.p, b.direction};
    }
  }
  return std::nullopt;
}

// ---------- Champs (2.6) ----------
QRectF VueSchema::zoneChamp(const std::string& symbole, bool valeur) const {
  const ZonesChamps& z = zonesChamps();
  const auto it = z.zones.find(symbole);
  if (it == z.zones.end()) return {};
  return valeur ? it->second.second : it->second.first;
}

const VueSchema::ZonesChamps& VueSchema::zonesChamps() const {
  CleCache cle = cleCache();
  cle.selection = 0;  // la sélection ne déplace aucun champ
  const int affichage = RegistreReglages::instance().valeur(QStringLiteral("schema/affichageValeurs")).toInt();
  if (cle == m_zonesChamps.cle && affichage == m_zonesChamps.affichage && m_modifications == m_zonesChamps.modifications) return m_zonesChamps;
  m_zonesChamps.cle = cle;
  m_zonesChamps.affichage = affichage;
  m_zonesChamps.modifications = m_modifications;
  m_zonesChamps.zones.clear();
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f) return m_zonesChamps;
  QFont fo = font();
  for (const auto& s : f->symboles) {
    const DefSymbole* d = c->symbole(s.symbole);
    if (!d) continue;
    QString ref, val;
    textesDe(*f, s, *d, ref, val);
    const PlacesChamps p = placesChamps(*d, s, ref, val);
    auto zone = [&](bool valeur) -> QRectF {
      const QString& t = valeur ? val : ref;
      const double taille = (valeur ? 1.3 : 1.4) * m_echelle;
      if (t.isEmpty() || taille < 3) return {};
      fo.setPixelSize(std::max(1, static_cast<int>(taille)));
      const double l = QFontMetricsF(fo).horizontalAdvance(t);
      const QPointF a = ecran(valeur ? p.val : p.ref);
      const int al = valeur ? p.alVal : p.alRef;
      const double x = al < 0 ? a.x() : al > 0 ? a.x() - l : a.x() - l / 2;
      return QRectF(x, a.y() - taille / 2, l, taille);
    };
    m_zonesChamps.zones[s.uuid] = {zone(false), zone(true)};
  }
  return m_zonesChamps;
}

std::optional<VueSchema::ChampVise> VueSchema::champSous(QPointF q) const {
  const Feuille* f = feuilleCourante();
  if (!f) return std::nullopt;
  const ZonesChamps& z = zonesChamps();
  for (auto it = f->symboles.rbegin(); it != f->symboles.rend(); ++it)
    if (const auto zs = z.zones.find(it->uuid); zs != z.zones.end())
      for (const bool valeur : {true, false})
        if (const QRectF& r = valeur ? zs->second.second : zs->second.first; !r.isEmpty() && r.adjusted(-2, -2, 2, 2).contains(q)) return ChampVise{it->uuid, valeur};
  return std::nullopt;
}

namespace {
// La partie numérique d'un nom, avancée de « pas » (B2 → B3) ; avec « lettre », la lettre qui la précède (B2 → C2)
QString incrementer(const QString& t, int pas, bool lettre) {
  int fin = static_cast<int>(t.size());
  while (fin > 0 && !t[fin - 1].isDigit()) --fin;
  int debut = fin;
  while (debut > 0 && t[debut - 1].isDigit()) --debut;
  if (lettre) {
    const int i = debut > 0 ? debut - 1 : -1;
    if (i < 0 || !t[i].isLetter()) return t;
    QString r = t;
    r[i] = QChar(t[i].unicode() + pas);
    return r[i].isLetter() ? r : t;
  }
  if (debut == fin) return t;
  const int n = std::max(0, t.mid(debut, fin - debut).toInt() + pas);
  return t.left(debut) + QString::number(n) + t.mid(fin);
}
// Flèches haut et bas d'un champ sur place : la partie numérique suivante ou précédente ; avec Maj, la lettre
class FlechesIncrement : public QObject {
 public:
  explicit FlechesIncrement(QLineEdit* e) : QObject(e), m_e(e) {}
  bool eventFilter(QObject*, QEvent* ev) override {
    if (ev->type() != QEvent::KeyPress) return false;
    auto* k = static_cast<QKeyEvent*>(ev);
    if (k->key() != Qt::Key_Up && k->key() != Qt::Key_Down) return false;  // champ de saisie
    m_e->setText(incrementer(m_e->text(), k->key() == Qt::Key_Up ? 1 : -1, (k->modifiers() & Qt::ShiftModifier) != 0));  // champ de saisie
    return true;
  }

 private:
  QLineEdit* m_e;
};
}  // namespace

// Tout champ sur place (2.22) : flèches du clavier, et deux petites flèches au bout du champ quand le texte porte un
// nombre ; Maj (ou Maj+clic) avance la lettre qui précède le nombre
void VueSchema::equiperIncrement(QLineEdit* e) {
  e->installEventFilter(new FlechesIncrement(e));
  const int l = 16;
  e->setTextMargins(0, 0, l + 2, 0);
  QToolButton* boutons[2];
  for (int i = 0; i < 2; ++i) {
    auto* b = new QToolButton(e);
    b->setObjectName(i == 0 ? QStringLiteral("incrementPlus") : QStringLiteral("incrementMoins"));
    b->setText(i == 0 ? QStringLiteral("▴") : QStringLiteral("▾"));
    b->setToolTip(i == 0 ? QStringLiteral("Nombre suivant (B2 → B3) · Maj+clic : lettre suivante (B2 → C2)") : QStringLiteral("Nombre précédent · Maj+clic : lettre précédente"));
    b->setFocusPolicy(Qt::NoFocus);  // le champ garde la frappe
    b->setAutoRaise(true);
    b->setCursor(Qt::ArrowCursor);
    const int h = e->height() / 2;
    b->setGeometry(e->width() - l - 1, i * h, l, h);
    connect(b, &QToolButton::clicked, e, [e, i] {
      e->setText(incrementer(e->text(), i == 0 ? 1 : -1, (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0));
      e->setModified(true);
    });
    boutons[i] = b;
  }
  auto montrer = [e, boutons](const QString& t) {
    const bool nombre = std::any_of(t.begin(), t.end(), [](QChar c) { return c.isDigit(); });
    for (QToolButton* b : boutons) b->setVisible(nombre);
    Q_UNUSED(e);
  };
  montrer(e->text());
  connect(e, &QLineEdit::textChanged, e, montrer);
}

void VueSchema::editerChamp(const std::string& symbole, bool valeur) {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f || refuseVerrou()) return;
  const SymbolePlace* s = nullptr;
  for (const auto& x : f->symboles)
    if (x.uuid == symbole) s = &x;
  const DefSymbole* d = s ? c->symbole(s->symbole) : nullptr;
  if (!d) return;
  const ComposantElec* k = c->composant(s->composant);
  // Une alimentation n'a qu'un nom ; un composant, son repère et sa valeur
  if (d->alimentation) valeur = true;
  else if (!k) return;
  QString ref, val;
  textesDe(*f, *s, *d, ref, val);
  const PlacesChamps p = placesChamps(*d, *s, ref, val);
  const std::string uuid = s->uuid, composant = s->composant;
  const bool alim = d->alimentation, unites = d->unites > 1;
  const QString actuel = valeur ? val : (k ? q(k->reference) : ref);
  const QString invite = alim ? QStringLiteral("Nom du réseau d’alimentation") : valeur ? QStringLiteral("Valeur") : QStringLiteral("Repère");
  const QString repere = k ? q(k->reference) : QString();
  saisirSurPlace(valeur ? p.val : p.ref, invite, actuel, [this, uuid, composant, valeur, alim, actuel, unites, repere](const QString& t) {
    if (t == actuel) return;
    const std::string v = t.toStdString();
    // Le pas dit ce qui change : « Valeur de R1 : 10k → 220 », « Repère : R1 → R5 »
    QString nom;
    if (alim) nom = QStringLiteral("Alimentation : %1 → %2").arg(actuel, t);
    else if (valeur) nom = QStringLiteral("Valeur de %1 : %2 → %3").arg(repere, actuel, t);
    else nom = QStringLiteral("Repère : %1 → %2").arg(actuel, t);
    modifier([&](Feuille& fe, ConceptionElec& cc) {
      if (alim) {
        for (auto& x : fe.symboles)
          if (x.uuid == uuid) x.nomAlim = v;
        return;
      }
      if (ComposantElec* x = cc.composant(composant)) (valeur ? x->valeur : x->reference) = unites && !valeur && !v.empty() && std::isupper(static_cast<unsigned char>(v.back())) && v.size() > 1 && std::isdigit(static_cast<unsigned char>(v[v.size() - 2])) ? v.substr(0, v.size() - 1) : v;
    }, {}, nom);
  });
  // Tab passe à l'autre champ, la frappe gardée (les flèches sont celles de tout champ sur place)
  if (QLineEdit* e = m_saisie.data()) {
    if (!alim) {
      class Tab : public QObject {
       public:
        Tab(QLineEdit* e, std::function<void()> f) : QObject(e), m_f(std::move(f)) {}
        bool eventFilter(QObject*, QEvent* ev) override {
          if (ev->type() != QEvent::KeyPress || (static_cast<QKeyEvent*>(ev)->key() != Qt::Key_Tab && static_cast<QKeyEvent*>(ev)->key() != Qt::Key_Backtab)) return false;  // champ de saisie
          m_f();
          return true;
        }

       private:
        std::function<void()> m_f;
      };
      e->installEventFilter(new Tab(e, [this, e, uuid, valeur] {
        emit e->editingFinished();  // la frappe est appliquée, puis l'autre champ s'ouvre
        QTimer::singleShot(0, this, [this, uuid, valeur] { editerChamp(uuid, !valeur); });
      }));
    }
  }
}

// F2 : le texte de l'objet choisi, sur place
bool VueSchema::modifierTexteChoisi() {
  const Feuille* f = feuilleCourante();
  if (!f || m_selection.size() != 1) return false;
  const std::string id = *m_selection.begin();
  for (const auto& s : f->symboles)
    if (s.uuid == id) {
      editerChamp(id, true);
      return true;
    }
  for (const auto& t : f->textes)
    if (t.uuid == id) {
      modifierTexte(id);
      return true;
    }
  for (const auto& e : f->etiquettes)
    if (e.uuid == id) {
      const std::string ancien = e.texte;
      saisirSurPlace(e.p, QStringLiteral("Nom du réseau"), q(ancien), [this, id, ancien](const QString& t) {
        if (t.toStdString() == ancien) return;
        modifier([&](Feuille& fe, ConceptionElec&) {
          for (auto& x : fe.etiquettes)
            if (x.uuid == id) x.texte = t.toStdString();
        }, {}, QStringLiteral("Étiquette : %1 → %2").arg(q(ancien), t));
      });
      return true;
    }
  return false;
}

// « Décaler les croisements » (2.19, SAI-09) : sur la sélection, la feuille ou tout le schéma
void VueSchema::sousMenuCroisements(QMenu* menu) {
  QMenu* m = menu->addMenu(QStringLiteral("Décaler les croisements"));
  m->setObjectName(QStringLiteral("menuDecalerCroisements"));
  const char* portees[] = {"Sur la sélection", "Sur cette feuille", "Sur tout le schéma"};
  for (int p = 0; p < 3; ++p) {
    QAction* a = m->addAction(QString::fromUtf8(portees[p]), this, [this, p] { decalerCroisementsSur(p); });
    if (p == 1) marquerAction(a, "schema.decalerCroisements");
    a->setEnabled(p > 0 || !m_selection.empty());
  }
}

int VueSchema::decalerCroisementsSur(int portee) {
  const ConceptionElec* c = conception();
  if (!c || refuseVerrou()) return 0;
  const std::set<std::string> sel = m_selection;
  const int fi = m_feuille;
  const std::size_t nf = c->feuilles.size();
  int n = 0;
  modifier([&](Feuille&, ConceptionElec& cc) {
    if (portee == 2)
      for (std::size_t i = 0; i < nf; ++i) n += decalerCroisements(cc, i);
    else n = decalerCroisements(cc, static_cast<std::size_t>(fi), portee == 0 ? &sel : nullptr);
  }, {}, QStringLiteral("Décaler les croisements"));
  emit message(n ? QStringLiteral("%1 croisement(s) à quatre branches changé(s) en deux T.").arg(n) : QStringLiteral("Aucun croisement à quatre branches ici."));
  return n;
}

int VueSchema::terminerBrochesChoisies(bool etiquettes) {
  const Feuille* f = feuilleCourante();
  if (!f || refuseVerrou()) return 0;
  std::set<std::string> composants;
  for (const auto& s : f->symboles)
    if (m_selection.count(s.uuid) && !s.composant.empty()) composants.insert(s.composant);
  if (composants.empty()) return 0;
  const int fi = m_feuille;
  int n = 0;
  modifier([&](Feuille&, ConceptionElec& cc) { n = terminerBrochesLibres(cc, static_cast<std::size_t>(fi), composants, etiquettes); }, {},
           etiquettes ? QStringLiteral("Terminer les broches libres (étiquettes)") : QStringLiteral("Terminer les broches libres (non connectées)"));
  emit message(QStringLiteral("%1 broche(s) libre(s) terminée(s).").arg(n));
  return n;
}

int VueSchema::replacerChampsChoisis() {
  const Feuille* f = feuilleCourante();
  if (!f || refuseVerrou()) return 0;
  std::vector<std::string> ids;
  for (const auto& s : f->symboles)
    if (m_selection.count(s.uuid)) ids.push_back(s.uuid);
  if (ids.empty()) return 0;
  const int fi = m_feuille;
  int n = 0;
  modifier([&](Feuille&, ConceptionElec& cc) {
    for (const auto& id : ids) n += replacerChamps(cc, static_cast<std::size_t>(fi), id);
  }, {}, QStringLiteral("Replacer les champs"));
  return n;
}

void VueSchema::bloquerChamp(const std::string& symbole, bool valeur, bool bloque) {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f || refuseVerrou()) return;
  Pt ancre{};
  bool trouve = false;
  for (const auto& s : f->symboles)
    if (s.uuid == symbole)
      if (const DefSymbole* d = c->symbole(s.symbole)) {
        ancre = ancrageChamp(s, *d, valeur) - s.position;
        trouve = true;
      }
  if (!trouve) return;
  modifier([&](Feuille& fe, ConceptionElec&) {
    for (auto& s : fe.symboles)
      if (s.uuid == symbole) {
        auto& champ = valeur ? s.champValeur : s.champReference;
        if (!champ) champ = ChampPlace{ancre, false};
        champ->bloque = bloque;
      }
  }, {}, bloque ? QStringLiteral("Bloquer le champ") : QStringLiteral("Libérer le champ"));
}

int VueSchema::alignerChoisis(Alignement a) {
  if (m_selection.empty() || refuseVerrou()) return 0;
  const std::set<std::string> sel = m_selection;
  const int fi = m_feuille;
  int n = 0;
  modifier([&](Feuille&, ConceptionElec& cc) { n = alignerSchema(cc, static_cast<std::size_t>(fi), sel, a); }, {},
           a == Alignement::RepartirHorizontal || a == Alignement::RepartirVertical ? QStringLiteral("Répartir") : QStringLiteral("Aligner"));
  return n;
}

bool VueSchema::echangerChoisis() {
  const Feuille* f = feuilleCourante();
  if (!f || m_selection.size() != 2 || refuseVerrou()) return false;
  std::vector<std::string> ids;
  for (const auto& s : f->symboles)
    if (m_selection.count(s.uuid)) ids.push_back(s.uuid);
  if (ids.size() != 2) return false;
  const int fi = m_feuille;
  bool fait = false;
  modifier([&](Feuille&, ConceptionElec& cc) { fait = echangerSymboles(cc, static_cast<std::size_t>(fi), ids[0], ids[1]); }, {}, QStringLiteral("Échanger deux symboles"));
  return fait;
}

// ---------- Menu contextuel (2.5) ----------
void VueSchema::menuOutil(QPointF ou) {
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("menuContextuel"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  const ContenuBandeau b = m_outils.actif()->bandeau();
  menu->addSection(b.titre.isEmpty() ? nomOutil() : b.titre);
  for (const ActionBandeau& a : b.actions) {
    if (a.sortie) continue;
    QAction* x = menu->addAction(a.libelle);
    if (!a.action.empty()) marquerAction(x, a.action);
    connect(x, &QAction::triggered, this, [this, f = a.faire] {
      if (f) f();
      majBandeau();
      update();
    });
  }
  menu->addSeparator();
  // Terminer : le tracé en cours finit, l'outil reste ; Annuler : l'outil rend la main à Sélection
  if (m_outils.actif()->geste()) ligneAction(menu, "commun.echap", QStringLiteral("Terminer le tracé"), [this] { echap(); });
  ligneAction(menu, "commun.echap", QStringLiteral("Terminer l’outil"), [this] {
    if (m_outils.actif()->geste()) m_outils.actif()->echap();
    setOutil(Outil::Selection);
  });
  menu->popup(mapToGlobal(ou.toPoint()));
}

void VueSchema::menuContextuel(QPointF ou) {
  m_outils.recycler();
  if (outil() != Outil::Selection || m_outils.actif()->geste()) {
    menuOutil(ou);
    return;
  }
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f) return;
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("menuContextuel"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  const bool electronique = domaine() == Domaine::Electronique;
  // Une broche visée : ses actions d'abord (SAI-13)
  if (const auto b = brocheSous(ou); b && electronique) {
    const ComposantElec* k = c->composant(b->composant);
    menu->addSection(QStringLiteral("Broche %1%2").arg(q(k ? k->reference + "." : std::string()) + q(b->numero), b->nom.empty() || b->nom == "~" ? QString() : QStringLiteral(" (%1)").arg(q(b->nom))));
    const std::string nom = b->nom.empty() || b->nom == "~" ? (k ? k->reference : std::string("U")) + "_" + b->numero : b->nom;
    const BrocheVisee br = *b;
    menu->addAction(icone(QStringLiteral("etiquette")), QStringLiteral("Étiquette « %1 »").arg(q(nom)), this, [this, br, nom] {
      const Pt versCorps = br.direction == 0 ? Pt{1, 0} : br.direction == 1 ? Pt{0, 1} : br.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
      const Pt bout{br.p.x - versCorps.x * 2 * GRILLE_SCHEMA, br.p.y - versCorps.y * 2 * GRILLE_SCHEMA};
      const int rotation = br.direction == 0 ? 2 : br.direction == 1 ? 1 : br.direction == 2 ? 0 : 3;
      modifier([&](Feuille& fe, ConceptionElec&) {
        fe.fils.push_back({nouvelUuid(), br.p, bout});
        Etiquette e;
        e.uuid = nouvelUuid();
        e.texte = nom;
        e.p = bout;
        e.rotation = rotation;
        fe.etiquettes.push_back(e);
      }, {}, QStringLiteral("Étiquette %1").arg(q(nom)));
    });
    ligneAction(menu, "schema.fil", QStringLiteral("Fil depuis cette broche"), [this, br] {
      setOutil(Outil::Fil);
      m_outils.actif()->appui(GesteVue{ecran(br.p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier});
      update();
    });
    ligneAction(menu, "schema.relier", QStringLiteral("Relier à…"), [this, br] { relierDepuis(br.composant, br.numero); });
    ligneAction(menu, "schema.nonConnexion", QStringLiteral("Marque de non-connexion"), [this, br] {
      modifier([&](Feuille& fe, ConceptionElec&) { fe.nonConnexions.push_back({br.p, nouvelUuid()}); }, {}, QStringLiteral("Non connectée"));
    });
    menu->addSeparator();
  }
  // Un repère ou une valeur visé : le modifier, le bloquer à sa place ou le libérer (2.23, SAI-34)
  if (const auto ch = champSous(ou); ch && electronique) {
    bool bloque = false;
    for (const auto& s : f->symboles)
      if (s.uuid == ch->symbole) {
        const auto& champ = ch->valeur ? s.champValeur : s.champReference;
        bloque = champ && champ->bloque;
      }
    menu->addSection(ch->valeur ? QStringLiteral("Valeur") : QStringLiteral("Repère"));
    menu->addAction(ch->valeur ? QStringLiteral("Modifier la valeur…") : QStringLiteral("Modifier le repère…"), this, [this, id = ch->symbole, v = ch->valeur] { editerChamp(id, v); });
    QAction* b = menu->addAction(QStringLiteral("Bloquer ce champ à sa place"), this, [this, id = ch->symbole, v = ch->valeur, bloque] { bloquerChamp(id, v, !bloque); });
    b->setObjectName(QStringLiteral("bloquerChamp"));
    b->setCheckable(true);
    b->setChecked(bloque);
    menu->addSeparator();
  }
  // L'objet visé devient la sélection, sauf s'il en fait déjà partie
  const std::string o = objetSous(ou);
  if (!o.empty() && !m_selection.count(o)) {
    selectionner({o});
    m_sel->surligner(reseauDe(o));
  }
  if (m_selection.empty()) {
    // Le vide : la vue
    ligneAction(menu, "edition.coller", QStringLiteral("Coller"), [this] { coller(); })->setEnabled(peutColler());
    if (electronique) {
      ligneAction(menu, "edition.collageSpecial", QStringLiteral("Collage spécial…"), [this] { dialogueCollageSpecial(); })->setEnabled(peutColler());
      ligneAction(menu, "edition.collerMatrice", QStringLiteral("Coller en matrice…"), [this] { dialogueMatrice(); })->setEnabled(peutColler());
    }
    ligneAction(menu, "selection.tout", QStringLiteral("Tout choisir"), [this] { toutChoisir(); });
    if (electronique) {
      // La forme de la prochaine sélection (2.26, SAI-38)
      QMenu* forme = menu->addMenu(QStringLiteral("Prendre par"));
      forme->setObjectName(QStringLiteral("menuFormeSelection"));
      const char* formes[] = {"Cadre", "Hors du cadre", "Polygone", "Trait"};
      for (int i = 0; i < 4; ++i) {
        QAction* x = forme->addAction(QString::fromUtf8(formes[i]), this, [this, i] { m_sel->setForme(static_cast<SelectionElectronique::Forme>(i)); });
        x->setCheckable(true);
        x->setChecked(static_cast<int>(m_sel->forme()) == i);
      }
    }
    ligneAction(menu, "vue.toutVoir", QStringLiteral("Tout voir"), [this] { recadrer(); });
    menu->popup(mapToGlobal(ou.toPoint()));
    return;
  }
  // Ce qui est choisi : symboles, fils, étiquettes, textes
  std::vector<const SymbolePlace*> symboles;
  std::vector<const Fil*> fils;
  std::vector<const Etiquette*> etiquettes;
  std::vector<const TexteSchema*> textes;
  for (const auto& s : f->symboles) if (m_selection.count(s.uuid)) symboles.push_back(&s);
  for (const auto& w : f->fils) if (m_selection.count(w.uuid)) fils.push_back(&w);
  for (const auto& e : f->etiquettes) if (m_selection.count(e.uuid)) etiquettes.push_back(&e);
  for (const auto& t : f->textes) if (m_selection.count(t.uuid)) textes.push_back(&t);
  const std::size_t n = m_selection.size();
  if (n == 1 && symboles.size() == 1) {
    const SymbolePlace& s = *symboles.front();
    const ComposantElec* k = c->composant(s.composant);
    const DefSymbole* d = c->symbole(s.symbole);
    menu->addSection(k ? q(k->reference) + QStringLiteral(" · ") + q(k->valeur) : d ? q(d->nom) : QString());
    ligneAction(menu, "objet.modifierTexte", d && d->alimentation ? QStringLiteral("Renommer l’alimentation…") : QStringLiteral("Modifier la valeur…"),
                [this, id = s.uuid] { editerChamp(id, true); });
    if (k) menu->addAction(QStringLiteral("Modifier le repère…"), this, [this, id = s.uuid] { editerChamp(id, false); });
    if (k && electronique) {
      ligneAction(menu, "schema.poserCopie", QStringLiteral("Poser une copie"), [this] { poserCopie(); });
      ligneAction(menu, "composant.empreinte", QStringLiteral("Changer d’empreinte…"), [this] { emit actionDemandee(QStringLiteral("composant.empreinte")); });
      ligneAction(menu, "schema.replacerChamps", QStringLiteral("Replacer les champs"), [this] { replacerChampsChoisis(); });
      if (d && d->unites > 1 && uniteSuivante(*c, s.composant) > 0)
        menu->addAction(QStringLiteral("Poser l’unité suivante"), this, [this, id = s.uuid] {
          std::string pose;
          const int fi = m_feuille;
          modifier([&](Feuille&, ConceptionElec& cc) { pose = poserUnite(cc, static_cast<std::size_t>(fi), id); }, {}, QStringLiteral("Poser l’unité suivante"));
          if (!pose.empty()) selectionner({pose});
        });
      if (!c->schemaSeul) menu->addAction(icone(QStringLiteral("pcb")), QStringLiteral("Montrer sur le circuit"), this, [this, comp = s.composant] { emit circuitDemande(q(comp)); });
    }
    menu->addAction(icone(QStringLiteral("proprietes")), QStringLiteral("Propriétés…"), this, [this] { emit inspecteurDemande(); });
  } else if (n == 1 && fils.size() == 1) {
    const Fil& w = *fils.front();
    const std::string reseau = reseauDe(w.uuid);
    menu->addSection(QStringLiteral("Fil %1").arg(q(reseau)));
    menu->addAction(QStringLiteral("Choisir tout le réseau sur la feuille"), this, [this, id = w.uuid] { choisirReseauDuFil(id); });
    const Pt ici = surGrille(monde(ou));
    if (surSegment(ici, w.a, w.b) && ici != w.a && ici != w.b) {
      menu->addAction(QStringLiteral("Couper ici"), this, [this, id = w.uuid, ici] {
        const int fi = m_feuille;
        modifier([&](Feuille&, ConceptionElec& cc) { couperFil(cc, static_cast<std::size_t>(fi), id, ici); }, {}, QStringLiteral("Couper le fil"));
      });
      // Trancher (2.19) : deux morceaux disjoints, quand « Couper » garde la connexion
      menu->addAction(QStringLiteral("Trancher ici"), this, [this, id = w.uuid, ici] {
        const int fi = m_feuille;
        modifier([&](Feuille&, ConceptionElec& cc) { trancherFil(cc, static_cast<std::size_t>(fi), id, ici); }, {}, QStringLiteral("Trancher le fil"));
      });
    }
    // Renommer le réseau (2.20, SAI-32) : ce tronçon, la feuille ou toutes les feuilles, en un pas
    if (!reseau.empty()) {
      QMenu* renommer = menu->addMenu(QStringLiteral("Renommer le réseau %1").arg(q(reseau)));
      renommer->setObjectName(QStringLiteral("menuRenommerReseau"));
      const char* portees[] = {"Ce tronçon…", "Sur cette feuille…", "Sur toutes les feuilles…"};
      for (int p = 0; p < 3; ++p) renommer->addAction(QString::fromUtf8(portees[p]), this, [this, reseau, ici, p] { renommerReseauSurPlace(reseau, ici, p); });
      // Renvoyer vers une autre feuille (2.20, SAI-31) : un connecteur hors feuille au bout libre le plus proche
      menu->addAction(QStringLiteral("Renvoyer vers une autre feuille"), this, [this, id = w.uuid, reseau, ici] { renvoyerFil(id, reseau, ici); });
    }
    sousMenuCroisements(menu);
  } else if (n == 1 && (etiquettes.size() == 1 || textes.size() == 1)) {
    menu->addSection(etiquettes.empty() ? QStringLiteral("Texte") : QStringLiteral("Étiquette %1").arg(q(etiquettes.front()->texte)));
    ligneAction(menu, "objet.modifierTexte", QStringLiteral("Modifier le texte…"), [this] { modifierTexteChoisi(); });
    if (!etiquettes.empty() && electronique) {
      // Genre et sens changés au menu, sans redessiner l'étiquette (2.20, SAI-11)
      const Etiquette& e = *etiquettes.front();
      QMenu* genre = menu->addMenu(QStringLiteral("Genre"));
      genre->setObjectName(QStringLiteral("menuGenreEtiquette"));
      for (const auto& [t, nom] : {std::pair{TypeEtiquette::Locale, "Locale (cette feuille)"}, std::pair{TypeEtiquette::Globale, "Globale (toutes les feuilles)"},
                                   std::pair{TypeEtiquette::HorsFeuille, "Connecteur hors feuille"}}) {
        QAction* x = genre->addAction(QString::fromUtf8(nom), this, [this, id = e.uuid, t] {
          modifier([&](Feuille& f, ConceptionElec&) {
            for (auto& y : f.etiquettes)
              if (y.uuid == id) y.type = t;
          }, {}, QStringLiteral("Genre de l’étiquette"));
        });
        x->setCheckable(true);
        x->setChecked(e.type == t);
      }
      if (e.type != TypeEtiquette::Locale) {
        QMenu* sens = menu->addMenu(QStringLiteral("Sens du signal"));
        sens->setObjectName(QStringLiteral("menuSensEtiquette"));
        const char* noms[] = {"Aucun", "Entrée", "Sortie", "Bidirectionnel", "Trois états", "Passif"};
        for (int i = 0; i <= static_cast<int>(SensSignal::Passif); ++i) {
          QAction* x = sens->addAction(QString::fromUtf8(noms[i]), this, [this, id = e.uuid, i] {
            modifier([&](Feuille& f, ConceptionElec&) {
              for (auto& y : f.etiquettes)
                if (y.uuid == id) y.sens = static_cast<SensSignal>(i);
            }, {}, QStringLiteral("Sens du signal"));
          });
          x->setCheckable(true);
          x->setChecked(static_cast<int>(e.sens) == i);
        }
      }
      menu->addAction(QStringLiteral("Renommer le réseau sur toutes les feuilles…"), this, [this, nom = e.texte, p = e.p] { renommerReseauSurPlace(nom, p, 2); });
    }
  } else {
    menu->addSection(QStringLiteral("%1 objets").arg(n));
    // Aligner et répartir sur la grille (2.25, SAI-35) : symboles et étiquettes, fils élastiques, en un pas
    if (symboles.size() + etiquettes.size() >= 2) {
      QMenu* aligner = menu->addMenu(QStringLiteral("Aligner"));
      aligner->setObjectName(QStringLiteral("menuAligner"));
      const std::pair<Alignement, const char*> choix[] = {{Alignement::Gauche, "À gauche"},
                                                          {Alignement::Droite, "À droite"},
                                                          {Alignement::Haut, "En haut"},
                                                          {Alignement::Bas, "En bas"},
                                                          {Alignement::CentreVertical, "Centres sur une verticale"},
                                                          {Alignement::CentreHorizontal, "Centres sur une horizontale"}};
      for (const auto& [a, nom] : choix) aligner->addAction(QString::fromUtf8(nom), this, [this, a] { alignerChoisis(a); });
      if (symboles.size() + etiquettes.size() >= 3) {
        aligner->addSeparator();
        aligner->addAction(QStringLiteral("Répartir à l’horizontale"), this, [this] { alignerChoisis(Alignement::RepartirHorizontal); });
        aligner->addAction(QStringLiteral("Répartir à la verticale"), this, [this] { alignerChoisis(Alignement::RepartirVertical); });
      }
    }
    // Échanger deux symboles de même genre (2.25, SAI-36)
    if (n == 2 && symboles.size() == 2 && (symboles[0]->composant.empty()) == (symboles[1]->composant.empty()))
      ligneAction(menu, "schema.echanger", QStringLiteral("Échanger leurs places"), [this] { echangerChoisis(); });
    if (!symboles.empty() && electronique) ligneAction(menu, "schema.replacerChamps", QStringLiteral("Replacer les champs"), [this] { replacerChampsChoisis(); });
    if (!fils.empty()) sousMenuCroisements(menu);
  }
  // Terminer les broches libres des composants choisis (2.21, SAI-13)
  if (electronique && std::any_of(symboles.begin(), symboles.end(), [](const SymbolePlace* s) { return !s->composant.empty(); })) {
    QMenu* terminer = menu->addMenu(QStringLiteral("Terminer les broches libres"));
    terminer->setObjectName(QStringLiteral("menuTerminerBroches"));
    QAction* et = terminer->addAction(QStringLiteral("Par une étiquette à leur nom"), this, [this] { terminerBrochesChoisies(true); });
    marquerAction(et, "schema.terminerBroches");
    terminer->addAction(QStringLiteral("Par une marque de non-connexion"), this, [this] { terminerBrochesChoisies(false); });
  }
  // Ce qui vaut pour tout ce qui est choisi, en un pas
  menu->addSeparator();
  if (!symboles.empty() || !etiquettes.empty() || !textes.empty()) {
    ligneAction(menu, "objet.tourner", QStringLiteral("Tourner"), [this] { tournerSelection(); });
    ligneAction(menu, "objet.tournerInverse", QStringLiteral("Tourner dans l’autre sens"), [this] { tournerSelection(true); });
    if (!symboles.empty()) {
      ligneAction(menu, "objet.miroirX", QStringLiteral("Miroir gauche-droite"), [this] { miroirSelection(); });
      ligneAction(menu, "objet.miroirY", QStringLiteral("Miroir haut-bas"), [this] { miroirSelection(true); });
    }
    menu->addSeparator();
  }
  if (electronique) {
    // Sélections avancées (2.26) : un palier de plus ou de moins, les semblables, l'inverse
    ligneAction(menu, "selection.etendre", QStringLiteral("Étendre la sélection"), [this] { etendreSelection(); });
    if (m_sel->paliers() > 0) ligneAction(menu, "selection.reduire", QStringLiteral("Réduire la sélection"), [this] { reduireSelection(); });
    if (!m_sel->composants().empty()) ligneAction(menu, "selection.semblables", QStringLiteral("Choisir les semblables…"), [this] { dialogueSemblables(); });
    ligneAction(menu, "selection.inverser", QStringLiteral("Inverser la sélection"), [this] { inverserSelection(); });
    menu->addSeparator();
  }
  ligneAction(menu, "edition.copier", QStringLiteral("Copier"), [this] { copier(); });
  if (electronique) ligneAction(menu, "edition.copierTexte", QStringLiteral("Copier comme texte"), [this] { copierCommeTexte(); });
  ligneAction(menu, "edition.couper", QStringLiteral("Couper"), [this] { couper(); });
  ligneAction(menu, "edition.dupliquer", QStringLiteral("Dupliquer"), [this] { dupliquer(); });
  menu->addSeparator();
  ligneAction(menu, "selection.supprimer", QStringLiteral("Supprimer"), [this] { supprimerSelection(); });
  menu->popup(mapToGlobal(ou.toPoint()));
}

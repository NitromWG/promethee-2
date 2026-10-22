// Prométhée : outils du canevas de schéma (lot L1, tâche 1.6, A5). Sélection, pose d'un symbole (à un coup, en série
// au bouton, fantôme tourné ou retourné par les boutons du bandeau ou par R, X, Y), fil, étiquette, jonction,
// non-connexion, texte. Chaque outil est une classe imbriquée de VueSchema : il en lit l'état et passe par ses
// fonctions de modification (une modification, un pas d'annulation).
// Licence GPL-3.0-only.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <QLocale>
#include <QCompleter>
#include <QLineEdit>
#include <QPointer>
#include <QTimer>
#include <QPainter>
#include <QSettings>

#include "BandeauOutil.hpp"
#include "Curseurs.hpp"
#include "GestesSelection.hpp"
#include "Document.hpp"
#include "SelectionElectronique.hpp"
#include "Theme.hpp"
#include "VueSchema.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;

namespace {
// Le nom suivant d'une série : sa partie numérique finale avancée de « pas » (D0 → D2 au pas 2), jamais négative
std::string suivant(const std::string& t, int pas = 1) {
  std::size_t i = t.size();
  while (i > 0 && std::isdigit(static_cast<unsigned char>(t[i - 1]))) --i;
  if (i == t.size()) return t;
  return t.substr(0, i) + std::to_string(std::max(0, std::stoi(t.substr(i, 9)) + pas));
}
QString q(const std::string& s) { return QString::fromStdString(s); }
// Un nom de réseau : 1 à 64 caractères, lettres, chiffres, _ + - . / ~ { } [ ] ; l'espace seulement dans un groupe
// entre accolades (2.10). Vide : le nom est bon ; sinon la raison du refus.
QString refusNomReseau(const QString& t) {
  if (t.isEmpty() || t.size() > 64) return QStringLiteral("un nom de réseau a de 1 à 64 caractères");
  int accolades = 0;
  for (const QChar ch : t) {
    if (ch == QLatin1Char('{')) ++accolades;
    if (ch == QLatin1Char('}')) --accolades;
    if (ch.isLetterOrNumber() || QStringLiteral("_+-./~{}[]").contains(ch)) continue;
    if (ch == QLatin1Char(' ')) {
      if (accolades > 0) continue;
      return QStringLiteral("l’espace n’est permise que dans un groupe de bus : remplace-la par _");
    }
    return QStringLiteral("le caractère « %1 » n’est pas permis dans un nom de réseau").arg(ch);
  }
  return {};
}
}  // namespace

// ---------- Base ----------
class VueSchema::OutilBase : public OutilVue {
 public:
  OutilBase(VueSchema& v, Outil genre) : m_vue(v), m_genre(genre) {}
  int genre() const override { return static_cast<int>(m_genre); }

 protected:
  bool electrique() const { return m_vue.domaine() == Domaine::Electrique; }
  Pt grille(const GesteVue& g) const { return m_vue.surGrilleActive(m_vue.monde(g.ecran)); }
  Pt accroche(const GesteVue& g) const { return m_vue.accroche(g.ecran); }
  // Petit réticule au point visé (outils sans fantôme)
  // Le réticule réglable (2.28) : petit, sur toute la vue, ou aucun
  void reticule(QPainter& g, Pt p) const {
    const VueSchema::Reticule style = VueSchema::styleReticule();
    if (style == VueSchema::Reticule::Aucun) return;
    g.setPen(QPen(theme().schema().apercu, 1, style == VueSchema::Reticule::Grand ? Qt::DotLine : Qt::SolidLine));
    const QPointF c = m_vue.ecran(p);
    if (style == VueSchema::Reticule::Grand) {
      g.drawLine(QPointF(0, c.y()), QPointF(m_vue.width(), c.y()));
      g.drawLine(QPointF(c.x(), 0), QPointF(c.x(), m_vue.height()));
      return;
    }
    const double t = 6 * Theme::echelleCanevas();
    g.drawLine(c + QPointF(-t, 0), c + QPointF(t, 0));
    g.drawLine(c + QPointF(0, -t), c + QPointF(0, t));
  }
  ActionBandeau sortie(const QString& libelle = QStringLiteral("Terminer")) const {
    ActionBandeau a;
    a.id = QStringLiteral("terminer");
    a.libelle = libelle;
    a.touche = QStringLiteral("Échap");
    VueSchema* v = &m_vue;
    a.faire = [v] { v->echap(); };
    a.sortie = true;
    a.action = "commun.echap";
    return a;
  }
  VueSchema& m_vue;
  Outil m_genre;
  Pt m_curseur{};
  bool m_dedans = false;  // la souris est passée sur la feuille depuis que l'outil est armé
};

// ---------- Sélection ----------
class VueSchema::OutilSelection : public OutilBase {
 public:
  explicit OutilSelection(VueSchema& v) : OutilBase(v, Outil::Selection) {}
  // Croix à quatre flèches sur un objet choisi qu'on peut glisser, et pendant le glisser
  // Main ouverte au-dessus d'un objet qu'on peut prendre (S6), croix à quatre flèches pendant le glisser
  QCursor curseur() const override { return m_glisse || m_surChoisi ? Qt::SizeAllCursor : m_surObjet ? Qt::OpenHandCursor : Qt::ArrowCursor; }
  void survol(const GesteVue& g) override {
    // Déplacement flottant (G, M sans bouton) : la sélection suit la souris jusqu'au clic
    if (m_flottant) {
      glisser(g);
      return;
    }
    if (!m_polygone.empty()) {
      m_souris = g.ecran;
      m_vue.update();
      return;
    }
    const std::string o = m_vue.objetSous(g.ecran);
    m_surChoisi = !o.empty() && m_vue.m_selection.count(o) > 0;
    m_surObjet = !o.empty();
  }
  bool geste() const override { return m_glisse || (m_cadre && m_appuyeCadre) || !m_polygone.empty() || !m_trait.empty(); }

  void appui(const GesteVue& g) override {
    if (m_flottant) {
      // Le clic pose la sélection là où elle est ; en tampon, un nouveau bloc suit aussitôt la souris (2.24)
      m_flottant = false;
      const bool collage = !m_titreGeste.isEmpty();
      relacher(g);
      if (collage && m_vue.m_tampon && m_ppFlottant) {
        const PressePapiersSchema pp = *m_ppFlottant;
        // Le relâcher de ce clic ne pose pas le bloc qui naît : il suit la souris jusqu'au clic suivant
        m_relacherIgnore = collerFlottant(pp, m_reperesFlottant);
      }
      return;
    }
    m_pression = m_souris = g.ecran;
    // Un renvoi de connecteur hors feuille : la feuille partenaire (2.20)
    if (m_vue.ouvrirRenvoi(g.ecran)) {
      m_cadre = m_appuyeCadre = m_appuye = false;
      return;
    }
    // « Relier à… » en cours : ce clic désigne l'autre broche (2.18)
    if (m_vue.relierEnCours()) {
      m_cadre = m_appuyeCadre = m_appuye = false;
      m_vue.relierVers(g.ecran);
      return;
    }
    // Sur le bout d'une broche libre (hors du texte d'un champ, qui se prend d'abord), l'appui commence un fil, aux
    // gestes du Fil (2.18, SAI-29)
    if (!(g.mods & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier)) && m_vue.domaine() == Domaine::Electronique && !m_vue.champSous(g.ecran))
      if (const auto b = m_vue.brocheLibreSous(g.ecran)) {
        m_cadre = m_appuyeCadre = m_appuye = false;
        m_vue.commencerFilDepuis(*b);
        return;
      }
    // Alt+clic : la liste de tous les objets sous le curseur, où l'on choisit le bon (SAI-18, comme au circuit)
    if (g.mods & Qt::AltModifier) {
      m_cadre = m_appuyeCadre = m_appuye = false;
      m_vue.listerObjetsSous(g.ecran);
      return;
    }
    m_mode = static_cast<int>(m_vue.m_sel->modeClic(g.mods));  // Maj, Ctrl, ou le mode du bandeau
    // Polygone (2.26, SAI-38) : chaque clic dans le vide ajoute un sommet ; un clic sur le premier, ou un double-clic,
    // le ferme et prend ce qui est dedans
    const auto forme = m_vue.m_sel->forme();
    if (forme == SelectionElectronique::Forme::Polygone && (!m_polygone.empty() || m_vue.objetSous(g.ecran).empty())) {
      m_cadre = m_appuyeCadre = m_appuye = false;
      if (m_polygone.size() >= 3 && (m_vue.ecran(m_polygone.front()) - g.ecran).manhattanLength() < 8) {
        fermerPolygone();
        return;
      }
      if (m_polygone.empty()) m_modePolygone = m_mode;
      m_polygone.push_back(m_vue.monde(g.ecran));
      m_vue.majBandeau();
      m_vue.update();
      return;
    }
    // Trait : la ligne tracée en glissant prend ce qu'elle touche
    if (forme == SelectionElectronique::Forme::Trait && m_vue.objetSous(g.ecran).empty()) {
      m_cadre = m_appuyeCadre = m_appuye = false;
      m_trait = {m_vue.monde(g.ecran)};
      return;
    }
    // Un repère ou une valeur se prend à la souris (2.6) : son symbole est choisi, le champ suit le glisser
    m_champ.reset();
    m_ancreDepart.reset();
    m_glisseChamp = false;
    if (!(g.mods & (Qt::ShiftModifier | Qt::ControlModifier)) && m_vue.domaine() == Domaine::Electronique)
      if (const auto ch = m_vue.champSous(g.ecran)) {
        m_champ = ch;
        m_cadre = m_appuyeCadre = false;
        m_appuye = true;
        if (!m_vue.m_selection.count(ch->symbole)) m_vue.selectionner({ch->symbole});
        return;
      }
    // Dans le vide : un glisser trace un cadre ; un objet n'est glissé que par un clic simple
    m_cadre = m_vue.objetSous(g.ecran).empty();
    m_appuyeCadre = m_cadre;
    m_appuye = !m_cadre && m_mode == static_cast<int>(SelectionElectronique::Mode::Remplacer);
    // Ctrl sur un objet : un glisser en dépose une copie (SAI-03) ; un clic sans glisser bascule, au relâcher
    m_copie = !m_cadre && (g.mods & Qt::ControlModifier) && !(g.mods & Qt::ShiftModifier) && m_vue.domaine() == Domaine::Electronique;
    if (m_copie) {
      m_appuye = true;
      m_modsAppui = g.mods;
      return;
    }
    m_vue.cliquerSelection(g.ecran, g.mods);
  }
  void glisser(const GesteVue& g) override {
    m_souris = g.ecran;
    if (!m_trait.empty()) {
      const Pt p = m_vue.monde(g.ecran);
      if ((m_vue.ecran(m_trait.back()) - g.ecran).manhattanLength() >= 3) m_trait.push_back(p);
      m_vue.update();
      return;
    }
    if (m_cadre) {
      m_vue.update();
      return;
    }
    if (m_champ && m_appuye) {
      glisserChamp();
      return;
    }
    // Ctrl sur un objet non choisi : c'est lui que la copie emporte
    if (!m_appuye || (m_vue.m_selection.empty() && !m_copie)) return;
    if (!m_glisse && (m_souris - m_pression).manhattanLength() > 4) commencer();
    if (!m_glisse) return;
    const Pt d = surGrille(m_vue.monde(m_souris)) - m_vue.m_departGlisse;
    const int fi = m_vue.m_feuille;
    VueSchema& v = m_vue;
    m_vue.m_doc->pendantGeste([&](prom::Projet& p) {
      if (ConceptionElec* cc = v.cible(p)) v.decaler(*cc, fi, d);
    }, true);
    m_delta = d;
  }
  void relacher(const GesteVue& g) override {
    m_souris = g.ecran;
    if (std::exchange(m_relacherIgnore, false)) return;
    m_appuye = false;
    if (!m_trait.empty()) {
      if (m_trait.size() >= 2) m_vue.appliquerIds(m_vue.objetsDansForme(m_trait, true), m_mode);
      m_trait.clear();
      m_vue.update();
      return;
    }
    if (m_champ) {
      if (m_glisseChamp) m_vue.m_doc->finGeste(m_champ->valeur ? QStringLiteral("Déplacer la valeur") : QStringLiteral("Déplacer le repère"));
      m_glisseChamp = false;
      m_champ.reset();
      return;
    }
    if (m_glisse) {
      m_glisse = false;
      m_vue.m_doc->finGeste(!m_titreGeste.isEmpty() ? std::exchange(m_titreGeste, QString())
                            : m_copie               ? QStringLiteral("Copie glissée (%1 objet(s))").arg(m_vue.m_selection.size())
                                                    : QString());
      m_copie = m_flottant = false;
      m_vue.m_elastique = true;
      return;
    }
    if (m_copie) {
      m_copie = false;
      m_vue.cliquerSelection(m_pression, m_modsAppui);
      return;
    }
    if (m_cadre) {
      m_cadre = m_appuyeCadre = false;
      const Feuille* f = m_vue.feuilleCourante();
      if (f && (m_souris - m_pression).manhattanLength() > 4) {
        // De droite à gauche : les objets touchés ; de gauche à droite : ceux qui sont dedans (S23) ; hors du cadre
        // (2.26) : tout ce qui n'est pas dedans. Filtre de sélection et masque respectés (SAI-18, ERG-28).
        const bool touches = m_souris.x() < m_pression.x();
        std::vector<std::string> ids = m_vue.objetsDansCadre(m_vue.monde(m_pression), m_vue.monde(m_souris), touches);
        if (m_vue.m_sel->forme() == SelectionElectronique::Forme::Exterieur) {
          const std::set<std::string> dedans(ids.begin(), ids.end());
          ids.clear();
          for (const std::string& id : m_vue.tousLesObjets())
            if (!dedans.count(id)) ids.push_back(id);
        }
        m_vue.appliquerIds(ids, m_mode);
      }
      m_vue.update();
    }
  }
  void doubleClic(const GesteVue& g) override {
    if (!m_polygone.empty()) {
      fermerPolygone();
      return;
    }
    // Double-clic sur un repère ou une valeur : le champ s'ouvre sur la feuille (2.6, SAI-14)
    if (const auto ch = m_vue.champSous(g.ecran)) {
      m_vue.editerChamp(ch->symbole, ch->valeur);
      return;
    }
    const std::string o = m_vue.objetSous(g.ecran);
    if (o.empty()) return;
    if (const Feuille* f = m_vue.feuilleCourante()) {
      for (const auto& t : f->textes)
        if (t.uuid == o) {
          m_vue.modifierTexte(o);
          return;
        }
      // Double-clic sur un fil : tout son réseau sur la feuille, surligné partout
      for (const auto& w : f->fils)
        if (w.uuid == o) {
          m_vue.choisirReseauDuFil(o);
          return;
        }
    }
    emit m_vue.proprietesDemandees();
  }
  bool agir(std::string_view a, bool faire) override {
    // G et M : pendant un glisser, fils élastiques ou décrochés ; sinon, la sélection suit la souris jusqu'au clic
    if (a == "schema.glisserFils" || a == "schema.deplacerSeul") {
      if (!m_glisse && m_vue.m_selection.empty()) return false;
      if (!faire) return true;
      m_vue.m_elastique = a == "schema.glisserFils";
      if (m_glisse) {
        glisser(GesteVue{m_souris, Qt::NoButton, Qt::LeftButton, Qt::NoModifier});
      } else {
        m_pression = m_souris = m_vue.m_souris;
        m_appuye = m_flottant = true;
        m_copie = false;
        commencer();
      }
      return true;
    }
    // Pendant un glisser, seul Échap (tenu par la vue) agit
    if (m_glisse) return false;
    if (a == "selection.supprimer") {
      if (faire) m_vue.supprimerSelection();
      return true;
    }
    if (a == "objet.tourner" || a == "objet.tournerInverse") {
      if (faire) m_vue.tournerSelection(a == "objet.tournerInverse");
      return true;
    }
    if (a == "objet.miroirX" || a == "objet.miroirY") {
      if (faire) m_vue.miroirSelection(a == "objet.miroirY");
      return true;
    }
    // Flèches : un pas de grille, dix avec Maj (S23) ; les appuis rapprochés font un seul pas (C18)
    if (a.substr(0, 10) == "selection.") {
      const bool dix = a.back() == '0';
      const std::string_view sens = a.substr(10, a.size() - 10 - (dix ? 2 : 0));
      if (sens != "gauche" && sens != "droite" && sens != "haut" && sens != "bas") return false;
      if (m_vue.m_selection.empty()) return false;
      if (faire) {
        const Nm pas = GRILLE_SCHEMA * (dix ? 10 : 1);
        m_vue.deplacerSelection({sens == "gauche" ? -pas : sens == "droite" ? pas : 0, sens == "haut" ? -pas : sens == "bas" ? pas : 0});
      }
      return true;
    }
    return false;
  }
  bool echap() override {
    if (!m_polygone.empty() || !m_trait.empty()) {
      m_polygone.clear();
      m_trait.clear();
      m_vue.majBandeau();
      m_vue.update();
      return true;
    }
    if (m_glisseChamp) {
      m_glisseChamp = m_appuye = false;
      m_champ.reset();
      m_vue.m_doc->annulerGeste();
      return true;
    }
    if (m_glisse) {
      // Le geste est annulé : les objets reviennent à leur place (une copie glissée disparaît), sans pas d'annulation
      m_glisse = m_appuye = m_copie = m_flottant = false;
      m_titreGeste.clear();
      m_vue.m_elastique = true;
      m_vue.m_doc->annulerGeste();
      return true;
    }
    if (m_cadre && m_appuyeCadre) {
      m_cadre = m_appuyeCadre = false;
      m_vue.update();
      return true;
    }
    return false;
  }
  void gesteInterrompu() override {
    m_glisse = m_appuye = m_flottant = false;
    m_vue.m_elastique = true;
  }
  void dessiner(QPainter& g) const override {
    if (m_glisse) {
      dessinerGuides(g);
      return;
    }
    // Polygone en cours (jusqu'au curseur) et trait : la couleur du choix, sur un fond léger
    if (!m_polygone.empty() || !m_trait.empty()) {
      const QColor choix = theme().schema().choix;
      QColor fond = choix;
      fond.setAlpha(25);
      QPolygonF p;
      for (const Pt& x : m_polygone.empty() ? m_trait : m_polygone) p << m_vue.ecran(x);
      if (!m_polygone.empty()) p << m_souris;
      g.setPen(QPen(choix, 1.5, m_trait.empty() ? Qt::SolidLine : Qt::DashLine));
      g.setBrush(m_trait.empty() ? QBrush(fond) : QBrush(Qt::NoBrush));
      if (m_trait.empty()) g.drawPolygon(p);
      else g.drawPolyline(p);
      return;
    }
    if (!m_cadre || !m_appuyeCadre || (m_souris - m_pression).manhattanLength() <= 4) return;
    const QColor choix = theme().schema().choix;
    QColor fond = choix;
    fond.setAlpha(25);
    // Cadre de droite à gauche (objets touchés) en tirets, de gauche à droite (objets contenus) en trait plein
    g.setPen(QPen(choix, 1, m_souris.x() < m_pression.x() ? Qt::DashLine : Qt::SolidLine));
    g.setBrush(fond);
    g.drawRect(QRectF(m_pression, m_souris).normalized());
  }
  void fermerPolygone() {
    if (m_polygone.size() >= 3) m_vue.appliquerIds(m_vue.objetsDansForme(m_polygone, false), m_modePolygone);
    m_polygone.clear();
    m_vue.majBandeau();
    m_vue.update();
  }
  // Réglages de la sélection au bandeau (2.26) : forme du cadre, « Étendre » et « Réduire », estompage du reste
  void ajouterReglages(ContenuBandeau& c) const {
    SelectionElectronique* sel = m_vue.m_sel;
    using F = SelectionElectronique::Forme;
    using E = SelectionElectronique::Estompage;
    if (c.vide() && sel->forme() == F::Cadre) return;
    if (c.vide()) {
      c.titre = QStringLiteral("Sélection");
      c.detail = QStringLiteral("rien de choisi");
      ActionBandeau fin = sortie(QStringLiteral("Tout désélectionner"));
      c.actions.push_back(fin);
    }
    static const char* formes[] = {"cadre", "hors du cadre", "polygone", "trait"};
    ActionBandeau forme;
    forme.id = QStringLiteral("forme-selection");
    forme.libelle = QStringLiteral("Prendre par : %1").arg(QString::fromUtf8(formes[static_cast<int>(sel->forme())]));
    for (int i = 0; i < 4; ++i) {
      ChoixBandeau x;
      x.libelle = QString::fromUtf8(formes[i]);
      x.coche = static_cast<int>(sel->forme()) == i;
      x.faire = [sel, i] { sel->setForme(static_cast<F>(i)); };
      forme.choix.push_back(x);
    }
    c.actions.insert(c.actions.end() - 1, forme);
    if (sel->vide()) return;
    VueSchema* v = &m_vue;
    c.actions.insert(c.actions.end() - 1, ActionBandeau::du("selection.etendre", QStringLiteral("etendre"), QStringLiteral("Étendre"), [v] { v->etendreSelection(); }));
    if (sel->paliers() > 0) c.actions.insert(c.actions.end() - 1, ActionBandeau::du("selection.reduire", QStringLiteral("reduire"), QStringLiteral("Réduire"), [v] { v->reduireSelection(); }));
    ActionBandeau estompe;
    estompe.id = QStringLiteral("estompage");
    static const char* noms[] = {"aucun", "estomper", "masquer", "ombre"};
    estompe.libelle = sel->estompage() == E::Estomper ? QStringLiteral("Reste : estompé %1 %").arg(static_cast<int>(sel->niveauEstompage() * 100 + 0.5))
                                                       : QStringLiteral("Reste : %1").arg(QString::fromUtf8(noms[static_cast<int>(sel->estompage())]));
    const std::tuple<E, double, const char*> choix[] = {{E::Aucun, -1, "Montré tel quel"},        {E::Estomper, 0.3, "Estompé à 30 %"}, {E::Estomper, 0.6, "Estompé à 60 %"},
                                                        {E::Estomper, 0.85, "Estompé à 85 %"},   {E::Masquer, -1, "Masqué : ni pris ni modifié"},
                                                        {E::Ombre, -1, "En ombre : à peine visible, ni pris ni modifié"}};
    for (const auto& [e, n, nom] : choix) {
      ChoixBandeau x;
      x.libelle = QString::fromUtf8(nom);
      x.coche = sel->estompage() == e && (n < 0 || std::abs(sel->niveauEstompage() - n) < 0.01);
      x.faire = [sel, e = e, n = n] { sel->setEstompage(e, n); };
      estompe.choix.push_back(x);
    }
    c.actions.insert(c.actions.end() - 1, estompe);
  }
  // Le champ pris suit la souris, calé sur la grille : sa place propre, gardée (2.6)
  void glisserChamp() {
    if (!m_glisseChamp) {
      if ((m_souris - m_pression).manhattanLength() <= 4 || m_vue.refuseVerrou()) return;
      m_vue.commencerGeste();
      m_glisseChamp = true;
    }
    const ConceptionElec* c = m_vue.conception();
    const Feuille* f = m_vue.feuilleCourante();
    if (!c || !f) return;
    // Décalage de l'ancre : celui du départ, plus le chemin de la souris
    Pt ancre{};
    for (const auto& s : f->symboles)
      if (s.uuid == m_champ->symbole)
        if (const DefSymbole* d = c->symbole(s.symbole)) ancre = ancrageChamp(s, *d, m_champ->valeur) - s.position;
    if (!m_ancreDepart) m_ancreDepart = ancre;
    const Pt d = surGrille(m_vue.monde(m_souris)) - surGrille(m_vue.monde(m_pression));
    const Pt decalage = *m_ancreDepart + d;
    const std::string id = m_champ->symbole;
    const bool valeur = m_champ->valeur;
    VueSchema& v = m_vue;
    const int fi = m_vue.m_feuille;
    m_vue.m_doc->pendantGeste([&](prom::Projet& p) {
      if (ConceptionElec* cc = v.cible(p))
        for (auto& s : cc->feuilles[static_cast<std::size_t>(fi)].symboles)
          if (s.uuid == id) (valeur ? s.champValeur : s.champReference) = ChampPlace{decalage, true};
    }, true);
  }
// Guides d'alignement pendant un glisser (2.8) : une broche déplacée alignée sur une broche immobile, en x ou en y
  void dessinerGuides(QPainter& g) const {
    const ConceptionElec* c = m_vue.conception();
    const Feuille* f = m_vue.feuilleCourante();
    if (!c || !f) return;
    std::vector<Pt> mobiles, fixes;
    for (const auto& s : f->symboles)
      if (const DefSymbole* d = c->symbole(s.symbole))
        for (const auto& b : brochesPlacees(s, *d)) (m_vue.m_selDepart.count(s.uuid) ? mobiles : fixes).push_back(b.p);
    g.setPen(QPen(theme().schema().apercu, 1, Qt::DotLine));
    int n = 0;
    for (const Pt& m : mobiles)
      for (const Pt& x : fixes) {
        if (n >= 12) return;
        if ((m.x == x.x || m.y == x.y) && m != x) {
          g.drawLine(m_vue.ecran(m), m_vue.ecran(x));
          ++n;
        }
      }
  }
  ContenuBandeau bandeau() const override {
    if (m_vue.relierEnCours()) {
      ContenuBandeau c;
      c.titre = QStringLiteral("Relier %1 à…").arg(QString::fromStdString(m_vue.m_relierDepuis));
      c.detail = QStringLiteral("clic sur l’autre broche : le fil contourne les symboles");
      c.actions.push_back(sortie(QStringLiteral("Annuler")));
      return c;
    }
    if (!m_polygone.empty()) {
      ContenuBandeau c;
      c.titre = QStringLiteral("Sélection par polygone");
      c.detail = QStringLiteral("%1 sommet(s) · clic sur le premier, ou double-clic, pour fermer").arg(m_polygone.size());
      c.actions.push_back(sortie(QStringLiteral("Annuler")));
      return c;
    }
    if (!m_glisse) {
      ContenuBandeau c = bandeauSelection(m_vue.m_sel, sortie());
      if (m_vue.domaine() == Domaine::Electronique) ajouterReglages(c);
      return c;
    }
    ContenuBandeau c;
    c.titre = !m_titreGeste.isEmpty() ? QStringLiteral("Collage") : m_copie ? QStringLiteral("Copie") : QStringLiteral("Déplacement");
    c.detail = QStringLiteral("Δx %1 mm, Δy %2 mm").arg(QLocale().toString(mm(m_delta.x), 'f', 2), QLocale().toString(mm(m_delta.y), 'f', 2));
    if (m_flottant) c.detail += m_vue.m_tampon && !m_titreGeste.isEmpty() ? QStringLiteral(" · chaque clic pose une copie, Échap finit") : QStringLiteral(" · clic pour poser");
    // Fils élastiques (G) ou décrochés (M), basculés d'un clic
    OutilSelection* moi = const_cast<OutilSelection*>(this);
    if (m_flottant && !m_titreGeste.isEmpty()) {
      // Tampon (2.24) : le collage reste armé après chaque clic
      ActionBandeau t;
      t.id = QStringLiteral("tampon");
      t.libelle = QStringLiteral("Tampon");
      t.cochable = true;
      t.coche = m_vue.m_tampon;
      t.faire = [moi] {
        moi->m_vue.m_tampon = !moi->m_vue.m_tampon;
        moi->m_vue.majBandeau();
      };
      c.actions.push_back(t);
    }
    const bool elastique = m_vue.m_elastique;
    ActionBandeau fils = ActionBandeau::du(elastique ? "schema.deplacerSeul" : "schema.glisserFils", QStringLiteral("fils"),
                                           elastique ? QStringLiteral("Fils : suivent") : QStringLiteral("Fils : décrochés"),
                                           [moi, elastique] { moi->agir(elastique ? "schema.deplacerSeul" : "schema.glisserFils", true); });
    c.actions.push_back(fils);
    c.actions.push_back(sortie(QStringLiteral("Annuler")));
    return c;
  }

 public:
  // Collage qui suit la souris (2.13) : le bloc naît sous le pointeur, dans un geste, et le suit jusqu'au clic, qui
  // le pose en un pas ; Échap le retire sans laisser de pas
  bool collerFlottant(const PressePapiersSchema& pp, std::optional<ReperesCollage> choix = std::nullopt) {
    if (m_glisse || m_vue.refuseVerrou()) return false;
    const Pt ou = surGrille(m_vue.monde(m_vue.m_souris));
    const int fi = m_vue.m_feuille;
    const ReperesCollage reperes = choix.value_or(VueSchema::annotationALaPose() ? ReperesCollage::Libres : ReperesCollage::Effaces);
    m_ppFlottant = pp;
    m_reperesFlottant = reperes;
    std::set<std::string> crees;
    VueSchema& v = m_vue;
    m_vue.commencerGeste();
    m_vue.m_doc->pendantGeste([&](prom::Projet& p) {
      if (ConceptionElec* cc = v.cible(p)) crees = collerSchemaAvec(*cc, static_cast<std::size_t>(fi), pp, ou, reperes);
    }, true);
    if (crees.empty()) {
      m_vue.m_doc->annulerGeste();
      return false;
    }
    m_vue.selectionner(crees);
    m_vue.m_departGlisse = ou;
    m_vue.preparerDeplacement();
    m_pression = m_souris = m_vue.m_souris;
    m_appuye = m_flottant = m_glisse = true;
    m_copie = false;
    m_titreGeste = QStringLiteral("Coller (%1 objet(s))").arg(crees.size());
    m_vue.majBandeau();
    m_vue.update();
    return true;
  }

 private:
  void commencer() {
    if (m_vue.refuseVerrou()) {
      m_appuye = m_copie = false;
      return;
    }
    m_vue.m_departGlisse = surGrille(m_vue.monde(m_pression));
    if (m_copie) {
      // La copie naît sur place dans le geste, puis c'est elle qui glisse : un seul pas d'annulation
      const ConceptionElec* c = m_vue.conception();
      if (!c) return;
      std::set<std::string> sel = m_vue.m_selection;
      if (const std::string o = m_vue.objetSous(m_pression); !sel.count(o)) sel = {o};
      const PressePapiersSchema pp = copierSchema(*c, static_cast<std::size_t>(m_vue.m_feuille), sel);
      const int fi = m_vue.m_feuille;
      const bool annoter = VueSchema::annotationALaPose();
      std::set<std::string> crees;
      VueSchema& v = m_vue;
      m_vue.commencerGeste();
      m_vue.m_doc->pendantGeste([&](prom::Projet& p) {
        ConceptionElec* cc = v.cible(p);
        if (!cc) return;
        crees = collerSchema(*cc, static_cast<std::size_t>(fi), pp, pp.reference);
        if (annoter)
          for (const auto& s2 : cc->feuilles[static_cast<std::size_t>(fi)].symboles)
            if (crees.count(s2.uuid) && !s2.composant.empty()) annoterALaPose(*cc, s2.composant);
      }, true);
      m_vue.selectionner(crees);
      m_vue.preparerDeplacement();
      m_glisse = true;
      return;
    }
    m_vue.preparerDeplacement();
    m_vue.commencerGeste();
    m_glisse = true;
  }
  QPointF m_pression, m_souris;
  int m_mode = 0;
  bool m_appuye = false, m_glisse = false, m_cadre = false, m_appuyeCadre = false, m_surChoisi = false, m_copie = false, m_flottant = false, m_surObjet = false;
  QString m_titreGeste;  // nom du pas d'un collage qui suit la souris
  std::optional<PressePapiersSchema> m_ppFlottant;  // ce bloc, que le tampon recolle après chaque clic (2.24)
  Polygone m_polygone, m_trait;  // sélection par polygone, par trait (2.26)
  int m_modePolygone = 0;
  bool m_relacherIgnore = false;
  ReperesCollage m_reperesFlottant = ReperesCollage::Libres;
  Qt::KeyboardModifiers m_modsAppui;
  Pt m_delta{};
  std::optional<ChampVise> m_champ;  // repère ou valeur pris à la souris
  std::optional<Pt> m_ancreDepart;
  bool m_glisseChamp = false;
};

// ---------- Pose d'un symbole (composant, alimentation) ----------
class VueSchema::OutilPose : public OutilBase {
 public:
  OutilPose(VueSchema& v, Outil genre, std::string symbole) : OutilBase(v, genre), m_symbole(std::move(symbole)) {
    // Alimentation : le dernier nom posé avec ce style (P), sinon le nom du symbole (SAI-12)
    if (m_genre == Outil::Alimentation) {
      if (m_symbole == m_vue.m_styleAlim && !m_vue.m_derniereAlim.empty()) m_nomAlim = m_vue.m_derniereAlim;
      else if (const DefSymbole* d = m_vue.definition(m_symbole)) m_nomAlim = d->nom;
    }
  }
  void setDefinition(const DefSymbole& d) { m_definition = d; }
  const std::string& symbole() const { return m_symbole; }
  void setSerie(bool oui) {
    m_serie = oui;
    m_vue.majBandeau();
  }
  bool serie() const { return m_serie; }
  int poses() const { return m_poses; }
  // « Poser une copie » (2.3) : chaque exemplaire reprend la valeur et les champs du modèle, et son orientation
  void setModele(const ComposantElec& k, int rotation, bool miroir) {
    m_modele = k;
    m_rotation = rotation;
    m_miroir = miroir;
  }
  // Un composant se pose une fois puis la main revient à Sélection (S2) ; une alimentation, une pose électrique ou
  // la pose en série restent armées. En série, un symbole à plusieurs unités enchaîne ses unités (SYM-04), puis
  // repasse à un composant neuf ; posé seul, il pose l'unité A (« Poser l'unité suivante » au menu du symbole).
  bool unCoup() const override { return m_genre == Outil::Placer && m_vue.poseUnCoup() && !m_serie; }

  void survol(const GesteVue& g) override {
    m_curseur = grille(g);
    m_dedans = true;
    m_vue.update();
  }
  void glisser(const GesteVue& g) override { survol(g); }
  void appui(const GesteVue& g) override { survol(g); }
  // Pose au relâcher : un clic, ou un glisser-relâcher sur la feuille (1.6) ; avec Alt, un composant à deux broches
  // posé sur un fil s'y insère en série (2.3)
  void relacher(const GesteVue& g) override { poser(grille(g), (g.mods & Qt::AltModifier) != 0); }
  // R, Maj+R, X et Y agissent sur le fantôme, jamais sur un autre objet (S11, SAI-04). Au clavier seul (G11, ERG-05) :
  // les flèches mènent le fantôme d'un pas de grille (dix avec Maj), depuis le centre de la vue si la souris n'y est
  // pas, et Entrée le pose
  bool agir(std::string_view a, bool faire) override {
    static constexpr std::pair<std::string_view, std::pair<int, int>> pas[] = {
        {"selection.gauche", {-1, 0}},    {"selection.droite", {1, 0}},    {"selection.haut", {0, -1}},    {"selection.bas", {0, 1}},
        {"selection.gauche10", {-10, 0}}, {"selection.droite10", {10, 0}}, {"selection.haut10", {0, -10}}, {"selection.bas10", {0, 10}}};
    for (const auto& [id, d] : pas)
      if (a == id) {
        if (faire) {
          if (!m_dedans) {
            m_curseur = surGrille(m_vue.monde(QPointF(m_vue.width() / 2.0, m_vue.height() / 2.0)));
            m_dedans = true;
          }
          m_curseur = m_curseur + Pt{d.first * GRILLE_SCHEMA, d.second * GRILLE_SCHEMA};
          m_vue.update();
        }
        return true;
      }
    if (a == "trace.terminer") {
      if (faire) poserIci();
      return true;
    }
    if (a == "objet.tourner" || a == "objet.tournerInverse") {
      if (faire) tourner(a == "objet.tournerInverse");
      return true;
    }
    if (a == "objet.miroirX" || a == "objet.miroirY") {
      if (faire) retourner(a == "objet.miroirY");
      return true;
    }
    return false;
  }
  // Entrée ou « Poser ici » : au fantôme, ou au centre de la vue s'il n'est pas encore sur la feuille
  void poserIci() {
    if (!m_dedans) m_curseur = surGrille(m_vue.monde(QPointF(m_vue.width() / 2.0, m_vue.height() / 2.0)));
    poser(m_curseur, false);
  }
  void tourner(bool inverse = false) {
    m_rotation = (m_rotation + (inverse ? 3 : 1)) % 4;
    m_vue.update();
  }
  void retourner(bool hautBas) {
    // Même écriture que transformerSelectionSchema : un miroir vu à l'écran change la rotation et bascule le miroir
    m_rotation = hautBas ? (6 - m_rotation % 4) % 4 : (4 - m_rotation % 4) % 4;
    m_miroir = !m_miroir;
    m_vue.update();
  }
  void dessiner(QPainter& g) const override {
    if (!m_dedans || m_symbole.empty()) return;
    const DefSymbole* d = definition();
    if (!d) return;
    SymbolePlace s;
    s.position = m_curseur;
    s.rotation = m_rotation;
    s.miroir = m_miroir;
    g.setOpacity(0.55);
    // Le repère que la pose donnera : l'unité suivante du composant, le prochain repère libre (annotation à la
    // pose), sinon « R? »
    QString ref = m_vue.repereFantome(*d);
    const ConceptionElec* c = m_vue.conception();
    if (const int u = c && !m_composantUnites.empty() ? uniteSuivante(*c, m_composantUnites) : 0; u > 0) {
      s.unite = u;
      if (const ComposantElec* k = c->composant(m_composantUnites)) ref = q(k->reference + lettreUnite(u));
    } else if (c && !electrique() && !d->alimentation && !d->drapeau && VueSchema::annotationALaPose()) {
      ref = q(prochainRepere(*c, d->prefixe.empty() ? "U" : d->prefixe) + (d->unites > 1 ? lettreUnite(1) : std::string()));
    }
    VueSchema::dessinerSymbole(g, {m_vue.m_origine, m_vue.m_echelle}, *d, s, d->alimentation ? QString() : ref, d->alimentation && !m_nomAlim.empty() ? q(m_nomAlim) : q(d->nom), true);
    g.setOpacity(1);
  }
  ContenuBandeau bandeau() const override {
    const DefSymbole* d = definition();
    ContenuBandeau c;
    const QString nom = d ? q(d->nom) : q(m_symbole);
    if (m_genre == Outil::Alimentation) c.titre = QStringLiteral("Alimentation : %1").arg(m_nomAlim.empty() || (d && d->drapeau) ? nom : q(m_nomAlim));
    else c.titre = (m_serie ? QStringLiteral("Pose en série : %1") : m_modele ? QStringLiteral("Copie : %1") : QStringLiteral("Pose : %1")).arg(nom);
    c.detail = m_serie && m_poses > 0 ? QStringLiteral("%1 posé(s) · un clic par exemplaire").arg(m_poses) : QStringLiteral("clic sur la feuille pour poser");
    if (const ConceptionElec* cc = m_vue.conception(); cc && !m_composantUnites.empty())
      if (const int u = uniteSuivante(*cc, m_composantUnites); u > 0)
        if (const ComposantElec* k = cc->composant(m_composantUnites))
          c.detail = QStringLiteral("unité %1 de %2 · clic pour la poser · « Terminer » : les autres plus tard").arg(q(lettreUnite(u)), q(k->reference));
    OutilPose* moi = const_cast<OutilPose*>(this);
    c.actions.push_back(ActionBandeau::du("trace.terminer", QStringLiteral("valider"), QStringLiteral("Poser ici"), [moi] { moi->poserIci(); }));
    c.actions.push_back(ActionBandeau::du("objet.tourner", QStringLiteral("tourner"), QStringLiteral("Tourner"), [moi] { moi->tourner(); }));
    c.actions.push_back(ActionBandeau::du("objet.tournerInverse", QStringLiteral("tourner-inverse"), QStringLiteral("↺"), [moi] { moi->tourner(true); }));
    c.actions.push_back(ActionBandeau::du("objet.miroirX", QStringLiteral("retourner-x"), QStringLiteral("Retourner ↔"), [moi] { moi->retourner(false); }));
    c.actions.push_back(ActionBandeau::du("objet.miroirY", QStringLiteral("retourner-y"), QStringLiteral("Retourner ↕"), [moi] { moi->retourner(true); }));
    if (m_genre == Outil::Alimentation && !electrique()) {
      // Nom libre (liste des noms courants, ou un autre) et style du symbole (S13, SAI-12)
      ActionBandeau nomAlim;
      nomAlim.id = QStringLiteral("nom-alim");
      nomAlim.libelle = QStringLiteral("Nom : %1").arg(q(m_nomAlim));
      for (const char* n : {"GND", "+5V", "+3V3", "+12V", "VCC", "-12V", "+24V", "GNDA", "GNDPWR", "VBAT", "+1V8"}) {
        ChoixBandeau x;
        x.libelle = QString::fromUtf8(n);
        x.coche = m_nomAlim == n;
        x.faire = [moi, n] { moi->m_nomAlim = n; };
        nomAlim.choix.push_back(x);
      }
      ChoixBandeau autre;
      autre.libelle = QStringLiteral("Autre nom…");
      autre.faire = [moi] { moi->demanderNom(); };
      nomAlim.choix.push_back(autre);
      c.actions.push_back(nomAlim);
      ActionBandeau style;
      style.id = QStringLiteral("style-alim");
      static const std::pair<const char*, const char*> styles[] = {{"Alim_barre", "barre"}, {"Alim_fleche", "flèche"}, {"GND", "masse"},
                                                                   {"Masse_chassis", "masse de châssis"}, {"Masse_analogique", "masse analogique"}, {"Drapeau_alim", "drapeau d’alimentation"}};
      QString actuel = QStringLiteral("barre");
      for (const auto& [id, nom] : styles)
        if (m_symbole == id) actuel = QString::fromUtf8(nom);
      style.libelle = QStringLiteral("Style : %1").arg(actuel);
      for (const auto& [id, nom] : styles) {
        ChoixBandeau x;
        x.libelle = QString::fromUtf8(nom);
        x.coche = m_symbole == id;
        x.faire = [moi, id = std::string(id)] {
          moi->m_symbole = id;
          moi->m_vue.m_styleAlim = id;
          moi->m_vue.update();
        };
        style.choix.push_back(x);
      }
      c.actions.push_back(style);
    }
    if (m_genre == Outil::Placer && !electrique()) {
      ActionBandeau serie{QStringLiteral("serie"), QStringLiteral("Poser en série"), QString(), [moi] { moi->setSerie(!moi->serie()); }};
      serie.cochable = true;
      serie.coche = m_serie;
      c.actions.push_back(serie);
    }
    c.actions.push_back(sortie(m_serie || m_genre == Outil::Alimentation ? QStringLiteral("Terminer") : QStringLiteral("Annuler")));
    if (!electrique()) m_vue.ajouterGrillesBandeau(c);
    return c;
  }

 private:
  const DefSymbole* definition() const {
    if (const DefSymbole* d = m_vue.definition(m_symbole)) return d;
    return m_definition && m_definition->id == m_symbole ? &*m_definition : nullptr;
  }
  void poser(Pt p, bool inserer = false) {
    if (m_symbole.empty()) return;
    if (m_vue.m_poseur && m_genre == Outil::Placer) {
      m_vue.m_poseur(p, m_rotation);
      return;
    }
    if (m_vue.refuseVerrou()) return;
    const DefSymbole* d = definition();
    const std::string id = m_symbole;
    const int rot = m_rotation, fi = m_vue.m_feuille;
    const bool miroir = m_miroir;
    const std::optional<DefSymbole> def = m_definition;
    const std::optional<ComposantElec> modele = m_modele;
    const std::string unites = m_composantUnites;
    const bool annoter = !electrique() && VueSchema::annotationALaPose();
    const std::string nomAlim = m_genre == Outil::Alimentation && !electrique() ? m_nomAlim : std::string();
    if (!nomAlim.empty()) {
      m_vue.m_derniereAlim = nomAlim;
      m_vue.m_styleAlim = m_symbole;
    }
    std::string pose, composant;
    bool insertionRefusee = false;
    const bool champs = !electrique();
    // Le pas porte le repère que la pose donne : « Poser C3 (Condensateur) »
    QString nom = d ? QStringLiteral("Poser %1 (%2)").arg(q(d->prefixe.empty() ? d->nom : d->prefixe + "?"), q(d->nom)) : QStringLiteral("Pose");
    if (const ConceptionElec* c = m_vue.conception(); c && d && annoter && !d->prefixe.empty() && !d->alimentation && !d->drapeau)
      nom = QStringLiteral("Poser %1 (%2)").arg(q(prochainRepere(*c, d->prefixe)), q(d->nom));
    if (const ConceptionElec* c = m_vue.conception(); c && !unites.empty())
      if (const ComposantElec* k = c->composant(unites); k && uniteSuivante(*c, unites) > 0)
        nom = QStringLiteral("Poser %1%2").arg(q(k->reference), q(lettreUnite(uniteSuivante(*c, unites))));
    m_vue.modifier(
        [&](Feuille&, ConceptionElec& cc) {
          // Définition et pose en un seul pas d'annulation (S4)
          if (def && !cc.symbole(def->id)) cc.symboles[def->id] = *def;
          // Unités enchaînées : l'unité suivante du même composant (SYM-04)
          const int u = unites.empty() || !cc.composant(unites) ? 0 : uniteSuivante(cc, unites);
          pose = u > 0 ? poserUniteEn(cc, static_cast<std::size_t>(fi), unites, u, p, rot) : poserSymbole(cc, static_cast<std::size_t>(fi), id, p, rot);
          for (auto& s : cc.feuilles[static_cast<std::size_t>(fi)].symboles)
            if (s.uuid == pose) {
              s.miroir = miroir;
              composant = s.composant;
              if (!nomAlim.empty() && cc.symbole(s.symbole) && cc.symbole(s.symbole)->alimentation) s.nomAlim = nomAlim;
            }
          // Insertion en série (Alt) : dans l'axe du fil, tourné d'office au besoin ; sinon refusée
          if (inserer && !pose.empty() && !insererDansFil(cc, static_cast<std::size_t>(fi), pose)) {
            bool fait = false;
            for (auto& s : cc.feuilles[static_cast<std::size_t>(fi)].symboles)
              if (s.uuid == pose) {
                s.rotation = (s.rotation + 1) % 4;
                fait = insererDansFil(cc, static_cast<std::size_t>(fi), pose);
                if (!fait) s.rotation = (s.rotation + 3) % 4;
              }
            insertionRefusee = !fait;
          }
          ComposantElec* k = cc.composant(composant);
          if (k && u == 0) {
            // Copie : même valeur, mêmes champs que le modèle ; puis le repère libre suivant (SAI-05, S15)
            if (modele) {
              const std::string uuid = k->uuid, ref = k->reference;
              *k = *modele;
              k->uuid = uuid;
              k->reference = ref;
            }
            if (annoter) annoterALaPose(cc, composant);
          }
          // Repère et valeur posés là où ils ne chevauchent rien (2.23, SAI-34)
          if (champs && !pose.empty()) replacerChamps(cc, static_cast<std::size_t>(fi), pose);
        },
        {}, nom);
    if (pose.empty()) return;
    ++m_poses;
    m_vue.noterPose(pose);
    if (insertionRefusee) emit m_vue.message(QStringLiteral("Insertion impossible : le fil doit traverser le composant dans son axe."));
    // Sans Alt, un composant à deux broches lâché dans l'axe d'un fil : la bulle propose de l'y insérer, d'un clic
    else if (!inserer && d && d->broches.size() == 2 && !electrique())
      if (const ConceptionElec* c = m_vue.conception()) {
        bool surUnFil = false;
        if (const Feuille* f = m_vue.feuilleCourante())
          for (const auto& s : f->symboles)
            if (s.uuid == pose)
              if (const auto bp = brochesPlacees(s, *d); bp.size() == 2)
                for (const auto& w : f->fils) surUnFil |= surSegment(bp[0].p, w.a, w.b) && surSegment(bp[1].p, w.a, w.b);
        if (surUnFil) {
          VueSchema* v = &m_vue;
          const std::string id = pose;
          emit m_vue.proposition(QStringLiteral("Le composant est posé sur un fil : <b>l’insérer dans le fil</b> ? (clic ici, ou Alt pendant la pose)"), [v, id, fi] {
            v->modifier([&](Feuille&, ConceptionElec& cc) { insererDansFil(cc, static_cast<std::size_t>(fi), id); }, {}, QStringLiteral("Insérer dans le fil"));
          });
        }
      }
    // En série, un symbole à plusieurs unités : la pose suivante est l'unité suivante de ce composant, jusqu'à la
    // dernière ; la suivante encore, un composant neuf
    if (const ConceptionElec* c = m_vue.conception(); c && d && d->unites > 1 && !composant.empty() && !electrique() && m_serie)
      m_composantUnites = uniteSuivante(*c, composant) > 0 ? composant : std::string();
    if (!unCoup()) {
      m_vue.majBandeau();
      m_vue.update();
      return;
    }
    // Une pose dure une pose : l'outil Sélection reprend la main, le composant posé choisi (S2, S4, SAI-02).
    // L'outil remplacé reste en vie jusqu'au geste suivant (GestionnaireOutils::recycler).
    VueSchema& v = m_vue;
    v.setOutil(Outil::Selection);
    v.selectionner({pose});
    emit v.composantPose(q(pose));
  }
  std::string m_symbole;
  std::optional<DefSymbole> m_definition;
  // Un autre nom d'alimentation, saisi sur place près du curseur
  void demanderNom() {
    OutilPose* moi = this;
    VueSchema* v = &m_vue;
    m_vue.saisirSurPlace(m_curseur, QStringLiteral("Nom de l’alimentation (−12V, VBAT, GNDA…)"), q(m_nomAlim), [moi, v](const QString& t) {
      if (dynamic_cast<OutilPose*>(v->m_outils.actif()) != moi) return;
      if (const QString r = refusNomReseau(t); !r.isEmpty()) {
        emit v->message(QStringLiteral("Nom refusé : %1.").arg(r));
        return;
      }
      moi->m_nomAlim = t.toStdString();
      v->majBandeau();
      v->update();
    });
  }
  std::string m_nomAlim;                  // alimentation : le nom posé (SAI-12)
  std::optional<ComposantElec> m_modele;  // « Poser une copie »
  std::string m_composantUnites;          // composant dont les unités suivantes restent à poser
  int m_rotation = 0, m_poses = 0;
  bool m_miroir = false, m_serie = false;
};

// ---------- Fil (2.7, SAI-06, S10, S19) ----------
// Un fil se trace en une fois : chaque clic fixe le tracé jusqu'au point, un clic sur un point de connexion ou un
// double-clic le termine, et le fil entier fait un seul pas d'annulation. Trois modes : à l'équerre, qui contourne
// les symboles (recherche A* du noyau) quand le coude simple les traverserait ; à 45° ; libre. Échap pose ce qui est
// fixé et retire l'aperçu ; Ctrl+Z pendant le tracé retire tout ; Retour arrière retire le dernier coude fixé.
class VueSchema::OutilFil : public OutilBase {
 public:
  explicit OutilFil(VueSchema& v) : OutilBase(v, Outil::Fil) {}
  bool geste() const override { return !m_points.empty(); }
  // Réticule cerclé quand le point visé est un point de connexion (broche, bout ou milieu d'un fil)
  QCursor curseur() const override { return m_dedans && m_vue.estPointConnexion(m_curseur, true) ? curseurs::accroche() : QCursor(Qt::CrossCursor); }
  void survol(const GesteVue& g) override {
    m_curseur = accroche(g);
    m_dedans = true;
    if (!m_points.empty()) calculerApercu();
    m_vue.update();
  }
  void glisser(const GesteVue& g) override { survol(g); }
  void appui(const GesteVue& g) override {
    const Pt p = accroche(g);
    m_curseur = p;
    if (m_points.empty()) {
      m_points = {p};
      m_apercu.clear();
      m_vue.update();
      return;
    }
    calculerApercu();
    for (std::size_t i = 1; i < m_apercu.size(); ++i)
      if (m_apercu[i] != m_points.back()) m_points.push_back(m_apercu[i]);
    m_apercu.clear();
    // Un clic sur un point de connexion (autre que le départ) termine le fil
    if (p != m_points.front() && m_vue.estPointConnexion(p, true)) valider();
    m_vue.majBandeau();
    m_vue.update();
  }
  void doubleClic(const GesteVue&) override {
    valider();
    m_vue.update();
  }
  // Un point posé sans la souris (saisie près du curseur, 2.28) : comme un clic à cet endroit
  bool poserPoint(Pt p) {
    if (m_points.empty()) return false;
    m_curseur = p;
    calculerApercu();
    for (std::size_t i = 1; i < m_apercu.size(); ++i)
      if (m_apercu[i] != m_points.back()) m_points.push_back(m_apercu[i]);
    m_apercu.clear();
    if (p != m_points.front() && m_vue.estPointConnexion(p, true)) valider();
    m_vue.majBandeau();
    m_vue.update();
    return true;
  }
  std::optional<Pt> dernier() const { return m_points.empty() ? std::nullopt : std::optional<Pt>(m_points.back()); }
  bool agir(std::string_view a, bool faire) override {
    if (a == "trace.coude") {
      if (faire) basculerCoude();
      return true;
    }
    if (a == "trace.mode") {
      if (faire) changerMode();
      return true;
    }
    if (m_points.empty()) return false;
    if (a == "trace.retirerCoude") {
      if (m_points.size() < 2) return false;  // aucun coude fixé à retirer
      if (faire) retirerCoude();
      return true;
    }
    if (a == "trace.annuler") {
      if (faire) annulerTrace();
      return true;
    }
    if (a == "trace.terminer") {
      if (faire) valider();
      return true;
    }
    return false;
  }
  // Échap retire le segment en aperçu et pose les segments fixés ; l'outil reste armé
  bool echap() override {
    if (m_points.empty()) return false;
    valider();
    m_vue.update();
    return true;
  }
  bool annulerTrace() override {
    if (m_points.empty()) return false;
    m_points.clear();
    m_apercu.clear();
    m_vue.majBandeau();
    m_vue.update();
    m_vue.finFilDepuisSelection();
    return true;
  }
  void quitter() override { valider(); }
  void basculerCoude() {
    m_vue.m_coudeFil = !m_vue.m_coudeFil;
    if (!m_points.empty()) calculerApercu();
    m_vue.update();
    m_vue.majBandeau();
  }
  void changerMode() {
    m_vue.m_modeFil = (m_vue.m_modeFil + 1) % 4;
    if (!m_points.empty()) calculerApercu();
    m_vue.update();
    m_vue.majBandeau();
  }
  void retirerCoude() {
    if (m_points.size() > 1) m_points.pop_back();
    calculerApercu();
    m_vue.majBandeau();
    m_vue.update();
  }
  void dessiner(QPainter& g) const override {
    if (!m_dedans) return;
    const double ep = std::max(1.2, 0.25 * m_vue.m_echelle);
    if (m_points.size() >= 2) {
      g.setPen(QPen(theme().schema().apercu, ep, Qt::SolidLine, Qt::RoundCap));
      for (std::size_t i = 0; i + 1 < m_points.size(); ++i) g.drawLine(m_vue.ecran(m_points[i]), m_vue.ecran(m_points[i + 1]));
    }
    if (m_apercu.size() >= 2) {
      g.setPen(QPen(theme().schema().apercu, ep, Qt::DashLine, Qt::RoundCap));
      for (std::size_t i = 0; i + 1 < m_apercu.size(); ++i) g.drawLine(m_vue.ecran(m_apercu[i]), m_vue.ecran(m_apercu[i + 1]));
    }
    // La broche ou le fil visé : un anneau (S6)
    if (m_vue.estPointConnexion(m_curseur, true)) {
      g.setPen(QPen(theme().schema().choix, 1.5));
      g.setBrush(Qt::NoBrush);
      g.drawEllipse(m_vue.ecran(m_curseur), 5, 5);
    }
    reticule(g, m_curseur);
  }
  ContenuBandeau bandeau() const override {
    ContenuBandeau c;
    static const char* modes[] = {"équerre", "45°", "libre", "courbe"};
    c.titre = QStringLiteral("Fil · %1").arg(QString::fromUtf8(modes[m_vue.m_modeFil % 4]));
    c.detail = m_points.empty() ? QStringLiteral("clic sur une broche ou un fil pour commencer") : relie();
    OutilFil* moi = const_cast<OutilFil*>(this);
    c.actions.push_back(ActionBandeau::du("trace.coude", QStringLiteral("coude"), m_vue.m_coudeFil ? QStringLiteral("Coude : horizontal d’abord") : QStringLiteral("Coude : vertical d’abord"),
                                          [moi] { moi->basculerCoude(); }));
    c.actions.push_back(ActionBandeau::du("trace.mode", QStringLiteral("mode"), QStringLiteral("Mode : %1").arg(QString::fromUtf8(modes[m_vue.m_modeFil % 4])), [moi] { moi->changerMode(); }));
    if (m_vue.m_modeFil % 4 == 3) {
      // Courbe : le rayon des coins, montré et réglé ici (2.19)
      ActionBandeau rayon;
      rayon.id = QStringLiteral("rayon-fil");
      rayon.libelle = QStringLiteral("Rayon : %1 mm").arg(QLocale().toString(mm(m_vue.m_rayonFil), 'f', 2));
      for (const double r : {1.27, 2.54, 5.08}) {
        ChoixBandeau x;
        x.libelle = QStringLiteral("%1 mm").arg(QLocale().toString(r, 'f', 2));
        x.coche = m_vue.m_rayonFil == nm(r);
        x.faire = [moi, r] {
          moi->m_vue.m_rayonFil = nm(r);
          moi->m_vue.update();
        };
        rayon.choix.push_back(x);
      }
      c.actions.push_back(rayon);
    }
    // Bout laissé ouvert : une étiquette posée d'office, son nom demandé sur place (2.21)
    ActionBandeau bout;
    bout.id = QStringLiteral("etiquette-bout");
    bout.libelle = QStringLiteral("Étiquette au bout ouvert");
    bout.cochable = true;
    bout.coche = VueSchema::etiquetteAuBoutOuvert();
    bout.faire = [moi] {
      VueSchema::setEtiquetteAuBoutOuvert(!VueSchema::etiquetteAuBoutOuvert());
      moi->m_vue.majBandeau();
    };
    c.actions.push_back(bout);
    if (!m_points.empty()) {
      if (m_points.size() > 1) c.actions.push_back(ActionBandeau::du("trace.retirerCoude", QStringLiteral("retirer-coude"), QStringLiteral("Retirer le coude"), [moi] { moi->retirerCoude(); }));
      c.actions.push_back(ActionBandeau::du("trace.annuler", QStringLiteral("retirer-trace"), QStringLiteral("Retirer le tracé"), [moi] { moi->annulerTrace(); }));
    }
    ActionBandeau fin = sortie();
    if (!m_points.empty()) {
      fin.libelle = QStringLiteral("Terminer le fil");
      fin.action = "trace.terminer";
    }
    c.actions.push_back(fin);
    m_vue.ajouterGrillesBandeau(c);
    if (!m_points.empty()) c.detail += QStringLiteral(" · un chiffre : longueur et angle");
    return c;
  }

 private:
  // « relie N-R1-2 et GND » : ce que le clic suivant relierait, avant de le faire
  QString relie() const {
    const std::string a = reseauEn(m_points.front()), b = reseauEn(m_curseur);
    if (!a.empty() && !b.empty() && a != b) return QStringLiteral("relie %1 et %2").arg(q(a), q(b));
    return QStringLiteral("clic pour fixer · clic sur une broche, ou double-clic, pour finir");
  }
  std::string reseauEn(Pt p) const {
    const Feuille* f = m_vue.feuilleCourante();
    const ConceptionElec* c = m_vue.conception();
    if (!f || !c || !m_vue.m_k) return {};
    for (const auto& w : f->fils)
      if (p == w.a || p == w.b || surSegment(p, w.a, w.b)) return m_vue.reseauDe(w.uuid);
    for (const auto& s : f->symboles)
      if (const DefSymbole* d = c->symbole(s.symbole))
        for (const auto& b : brochesPlacees(s, *d))
          if (b.p == p) return s.composant.empty() ? (d->alimentation ? (s.nomAlim.empty() ? d->nom : s.nomAlim) : std::string()) : m_vue.m_k->nom(s.composant, b.numero);
    return {};
  }
  // Corps d'un symbole (ses traits, sans les broches), gonflé d'un pas : ce qu'un fil ne traverse pas
  bool coupeUnCorps(Pt a, Pt b) const {
    const Feuille* f = m_vue.feuilleCourante();
    const ConceptionElec* c = m_vue.conception();
    if (!f || !c) return false;
    for (const auto& s : f->symboles) {
      const DefSymbole* d = c->symbole(s.symbole);
      if (!d) continue;
      Boite corps;
      for (const auto& t : d->traces) {
        if (t.forme == Trace::Forme::Texte || !dansUnite(t.unite, s, *d)) continue;
        if (t.forme == Trace::Forme::Cercle || t.forme == Trace::Forme::Arc) {
          const Pt o = t.points.empty() ? Pt{} : t.points.front();
          corps.etendre(transformer(s, o + Pt{t.rayon, t.rayon}));
          corps.etendre(transformer(s, o - Pt{t.rayon, t.rayon}));
        } else
          for (const Pt& p : t.points) corps.etendre(transformer(s, p));
      }
      if (corps.vide()) continue;
      const Boite x = corps.gonflee(GRILLE_SCHEMA);
      if (a.y == b.y && a.y > x.y0 && a.y < x.y1 && std::max(a.x, b.x) > x.x0 && std::min(a.x, b.x) < x.x1) return true;
      if (a.x == b.x && a.x > x.x0 && a.x < x.x1 && std::max(a.y, b.y) > x.y0 && std::min(a.y, b.y) < x.y1) return true;
    }
    return false;
  }
  void calculerApercu() {
    m_apercu.clear();
    if (m_points.empty()) return;
    const Pt a = m_points.back(), b = m_curseur;
    if (a == b) return;
    switch (m_vue.m_modeFil % 4) {
      case 2: m_apercu = {a, b}; return;  // libre
      case 1: {                           // 45° : la diagonale, puis le reste droit (ou l'inverse)
        const Nm dx = b.x - a.x, dy = b.y - a.y, m = std::min(std::abs(dx), std::abs(dy));
        const Pt diag{(dx > 0 ? 1 : dx < 0 ? -1 : 0) * m, (dy > 0 ? 1 : dy < 0 ? -1 : 0) * m};
        const Pt coin = m_vue.m_coudeFil ? a + diag : b - diag;
        m_apercu = {a, coin, b};
        break;
      }
      default: {  // équerre : le coude simple s'il est libre, sinon le chemin qui contourne
        const Pt coin = m_vue.m_coudeFil ? Pt{b.x, a.y} : Pt{a.x, b.y};
        if (!coupeUnCorps(a, coin) && !coupeUnCorps(coin, b)) {
          m_apercu = {a, coin, b};
          break;
        }
        const ConceptionElec* c = m_vue.conception();
        // Charte : 3 pas par coude, 20 par croisement, 2 000 nœuds au plus (au-delà, le coude simple)
        ReglagesTraceFil r;
        r.coude = 3;
        r.croisement = 20;
        r.maxNoeuds = 2000;
        std::vector<Pt> chemin = c ? tracerFil(*c, static_cast<std::size_t>(m_vue.m_feuille), a, b, r) : std::vector<Pt>{};
        m_apercu = chemin.size() >= 2 ? chemin : std::vector<Pt>{a, coin, b};
      }
    }
    m_apercu.erase(std::unique(m_apercu.begin(), m_apercu.end()), m_apercu.end());
  }
  // Le fil entier, en un pas : segments, puis fusion des segments alignés et contigus
  void valider() {
    std::vector<Pt> pts = m_points;
    m_points.clear();
    m_apercu.clear();
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    if (pts.size() < 2 || m_vue.refuseVerrou()) {
      m_vue.majBandeau();
      m_vue.finFilDepuisSelection();
      return;
    }
    const int fi = m_vue.m_feuille;
    const Nm rayon = m_vue.m_modeFil % 4 == 3 ? m_vue.m_rayonFil : 0;  // mode courbe : coins arrondis (2.19)
    // Le bout ne touche rien (ni broche, ni fil) : avec le réglage, une étiquette l'y attend (2.21)
    const bool ouvert = VueSchema::etiquetteAuBoutOuvert() && !m_vue.estPointConnexion(pts.back(), true);
    m_vue.modifier([&](Feuille& f, ConceptionElec& cc) {
      for (std::size_t i = 0; i + 1 < pts.size(); ++i) f.fils.push_back({nouvelUuid(), pts[i], pts[i + 1], rayon});
      fusionnerFils(cc, static_cast<std::size_t>(fi));
    }, {}, QStringLiteral("Fil (%1 segment(s))").arg(pts.size() - 1));
    m_vue.majBandeau();
    m_vue.finFilDepuisSelection();
    if (ouvert) m_vue.etiquetterBout(pts.back(), pts[pts.size() - 2]);
  }
  std::vector<Pt> m_points, m_apercu;
};

// ---------- Étiquette (locale, globale, connecteur hors feuille ; potentiel électrique) ----------
// Un clic pose l'ancrage (de préférence sur un bout de fil ou de broche) et ouvre un champ sur place, prérempli du
// nom suivant de la série (D3 → D4), sinon du réseau du fil visé, sinon vide (S20) ; une liste propose les noms déjà
// présents. Entrée pose et laisse l'outil armé pour la suivante ; Échap renonce à celle-ci (2.10, SAI-11).
class VueSchema::OutilEtiquette : public OutilBase {
 public:
  OutilEtiquette(VueSchema& v, Outil genre, std::string nom)
      : OutilBase(v, genre), m_nom(std::move(nom)), m_type(genre == Outil::Etiquette ? TypeEtiquette::Locale : TypeEtiquette::Globale) {}
  void survol(const GesteVue& g) override {
    m_curseur = accroche(g);
    m_dedans = true;
    m_vue.update();
  }
  void appui(const GesteVue& g) override {
    const Pt p = accroche(g);
    if (m_vue.refuseVerrou()) return;
    // Nom imposé par l'outil (potentiel choisi dans la palette), sinon saisi sur place
    if (!m_nom.empty()) {
      poser(p, m_nom);
      return;
    }
    // Noms collés (2.20) : le suivant de la liste, d'un clic
    if (!m_vue.m_nomsColles.empty()) {
      const std::string n = m_vue.m_nomsColles.front();
      m_vue.m_nomsColles.erase(m_vue.m_nomsColles.begin());
      poser(p, n);
      return;
    }
    // Nom réglé avant la pose (2.22, SAI-33) : posé d'un clic, puis avancé du pas
    if (!m_vue.m_prochaineEtiquette.empty()) {
      const std::string n = m_vue.m_prochaineEtiquette;
      poser(p, n);
      m_vue.m_prochaineEtiquette = suivant(n, m_vue.m_pasEtiquette);
      m_vue.majBandeau();
      return;
    }
    QString propose;
    if (!m_vue.m_derniereEtiquette.empty()) propose = q(suivant(m_vue.m_derniereEtiquette, m_vue.m_pasEtiquette));
    else if (const std::string r = reseauVise(p); !r.empty() && r.rfind("N-", 0) != 0) propose = q(r);
    demander(p, propose);
  }
  bool agir(std::string_view a, bool faire) override {
    // Tab : les réglages de l'outil avant la pose (2.22)
    if (a == "outil.reglages" && m_nom.empty()) {
      if (faire) reglerNom();
      return true;
    }
    // Ctrl+V : une liste de noms collée, une étiquette par nom (2.20)
    if (a == "edition.coller" && m_nom.empty()) {
      const QStringList noms = nomsDuPressePapiers();
      if (noms.isEmpty()) return false;
      if (faire) collerNoms(noms);
      return true;
    }
    if (a != "objet.tourner" && a != "objet.tournerInverse") return false;
    if (faire) tourner(a == "objet.tournerInverse");
    return true;
  }
  void tourner(bool inverse = false) {
    m_rotation = (m_rotation + (inverse ? 3 : 1)) % 4;
    m_vue.update();
  }
  // Les noms d'une liste copiée (une colonne d'un tableau, des noms séparés par des virgules) : chacun valable
  static QStringList nomsDuPressePapiers() {
    const QMimeData* m = QGuiApplication::clipboard()->mimeData();
    if (!m || !m->hasText() || m->hasFormat(QString::fromLatin1(VueSchema::MIME_SCHEMA))) return {};
    QStringList noms;
    for (const QString& x : m->text().split(QRegularExpression(QStringLiteral("[\\r\\n\\t,;]+")), Qt::SkipEmptyParts)) {
      const QString t = x.trimmed();
      if (!t.isEmpty() && refus(t).isEmpty()) noms << t;
    }
    return noms.size() > 200 ? QStringList() : noms;
  }
  void collerNoms(const QStringList& noms) {
    m_vue.m_nomsColles.clear();
    for (const QString& n : noms) m_vue.m_nomsColles.push_back(n.toStdString());
    emit m_vue.message(QStringLiteral("%1 nom(s) collé(s) · chaque clic pose le suivant : <b>%2</b> d’abord").arg(noms.size()).arg(noms.front().toHtmlEscaped()));
    m_vue.majBandeau();
  }
  // Le nom de la prochaine étiquette, sur place près du curseur ; les suivantes avancent du pas
  void reglerNom() {
    VueSchema* v = &m_vue;
    const QPointer<VueSchema> garde(v);
    const QString propose = !m_vue.m_prochaineEtiquette.empty() ? q(m_vue.m_prochaineEtiquette)
                            : !m_vue.m_derniereEtiquette.empty() ? q(suivant(m_vue.m_derniereEtiquette, m_vue.m_pasEtiquette))
                                                                  : QString();
    m_vue.saisirSurPlace(m_curseur, QStringLiteral("Nom de la prochaine étiquette · chaque clic la pose, puis avance du pas"), propose, [v, garde](const QString& t) {
      if (!garde || t.trimmed().isEmpty()) return;
      if (const QString r = refus(t.trimmed()); !r.isEmpty()) {
        emit v->message(QStringLiteral("Nom refusé : %1.").arg(r));
        return;
      }
      v->m_prochaineEtiquette = t.trimmed().toStdString();
      v->m_nomsColles.clear();
      v->majBandeau();
    });
  }
  void dessiner(QPainter& g) const override {
    if (m_dedans) reticule(g, m_curseur);
  }
  ContenuBandeau bandeau() const override {
    ContenuBandeau c;
    c.titre = m_type == TypeEtiquette::Locale ? QStringLiteral("Étiquette") : m_type == TypeEtiquette::Globale ? QStringLiteral("Étiquette globale") : QStringLiteral("Connecteur hors feuille");
    const int pas = m_vue.m_pasEtiquette;
    c.detail = !m_nom.empty() ? QStringLiteral("« %1 » · clic pour poser").arg(q(m_nom))
               : !m_vue.m_nomsColles.empty() ? QStringLiteral("« %1 » au prochain clic · encore %2 nom(s) collé(s)").arg(q(m_vue.m_nomsColles.front())).arg(m_vue.m_nomsColles.size())
               : !m_vue.m_prochaineEtiquette.empty() ? QStringLiteral("« %1 » au prochain clic · pas %2").arg(q(m_vue.m_prochaineEtiquette)).arg(pas)
               : m_vue.m_derniereEtiquette.empty() ? QStringLiteral("clic où poser le nom du réseau · Tab : nom et pas avant la pose")
                                                   : QStringLiteral("suivante : %1 · clic pour poser").arg(q(suivant(m_vue.m_derniereEtiquette, pas)));
    OutilEtiquette* moi = const_cast<OutilEtiquette*>(this);
    c.actions.push_back(ActionBandeau::du("objet.tourner", QStringLiteral("tourner"), QStringLiteral("Tourner"), [moi] { moi->tourner(); }));
    c.actions.push_back(ActionBandeau::du("objet.tournerInverse", QStringLiteral("tourner-inverse"), QStringLiteral("↺"), [moi] { moi->tourner(true); }));
    if (m_nom.empty()) {
      // Réglages avant la pose (2.22, SAI-33) : le nom de départ, le pas d'incrément ; une liste collée (2.20)
      c.actions.push_back(ActionBandeau::du("outil.reglages", QStringLiteral("nom-etiquette"),
                                            m_vue.m_prochaineEtiquette.empty() ? QStringLiteral("Nom…") : QStringLiteral("Nom : %1").arg(q(m_vue.m_prochaineEtiquette)), [moi] { moi->reglerNom(); }));
      ActionBandeau increment;
      increment.id = QStringLiteral("pas-etiquette");
      increment.libelle = QStringLiteral("Pas : %1").arg(pas);
      for (const int x : {1, 2, 4, 8, 10, -1}) {
        ChoixBandeau ch;
        ch.libelle = x > 0 ? QStringLiteral("+%1").arg(x) : QStringLiteral("%1").arg(x);
        ch.coche = pas == x;
        ch.faire = [moi, x] {
          moi->m_vue.m_pasEtiquette = x;
          moi->m_vue.majBandeau();
        };
        increment.choix.push_back(ch);
      }
      c.actions.push_back(increment);
      ActionBandeau colle = ActionBandeau::du("edition.coller", QStringLiteral("coller-noms"), QStringLiteral("Coller des noms"), [moi] {
        const QStringList noms = nomsDuPressePapiers();
        if (noms.isEmpty()) emit moi->m_vue.message(QStringLiteral("Le presse-papiers ne porte aucun nom de réseau valable : copie une liste, un nom par ligne."));
        else moi->collerNoms(noms);
      });
      c.actions.push_back(colle);
      if (!m_vue.m_nomsColles.empty() || !m_vue.m_prochaineEtiquette.empty())
        c.actions.push_back(ActionBandeau{QStringLiteral("oublier-noms"), QStringLiteral("Oublier ces noms"), {}, [moi] {
                                            moi->m_vue.m_nomsColles.clear();
                                            moi->m_vue.m_prochaineEtiquette.clear();
                                            moi->m_vue.majBandeau();
                                          }});
    }
    if (!electrique()) {
      // Style choisi à la pose : locale, globale, connecteur hors feuille ; et le sens du signal
      ActionBandeau type;
      type.id = QStringLiteral("type");
      type.libelle = QStringLiteral("Style : %1").arg(m_type == TypeEtiquette::Locale ? QStringLiteral("locale") : m_type == TypeEtiquette::Globale ? QStringLiteral("globale") : QStringLiteral("hors feuille"));
      for (const auto& [t, nom] : {std::pair{TypeEtiquette::Locale, "Locale (la feuille)"}, std::pair{TypeEtiquette::Globale, "Globale (tout le projet)"},
                                   std::pair{TypeEtiquette::HorsFeuille, "Connecteur hors feuille"}}) {
        ChoixBandeau x;
        x.libelle = QString::fromUtf8(nom);
        x.coche = m_type == t;
        x.faire = [moi, t] { moi->m_type = t; };
        type.choix.push_back(x);
      }
      c.actions.push_back(type);
      if (m_type != TypeEtiquette::Locale) {
        ActionBandeau sens;
        sens.id = QStringLiteral("sens");
        static const char* noms[] = {"aucun", "entrée", "sortie", "bidirectionnel", "trois états", "passif"};
        sens.libelle = QStringLiteral("Sens : %1").arg(QString::fromUtf8(noms[static_cast<int>(m_sens)]));
        for (int i = 0; i <= static_cast<int>(SensSignal::Passif); ++i) {
          ChoixBandeau x;
          x.libelle = QString::fromUtf8(noms[i]);
          x.coche = static_cast<int>(m_sens) == i;
          x.faire = [moi, i] { moi->m_sens = static_cast<SensSignal>(i); };
          sens.choix.push_back(x);
        }
        c.actions.push_back(sens);
      }
    }
    c.actions.push_back(sortie());
    m_vue.ajouterGrillesBandeau(c);
    return c;
  }

  static QString refus(const QString& t) { return refusNomReseau(t); }

 private:
  std::string reseauVise(Pt p) const {
    const Feuille* f = m_vue.feuilleCourante();
    if (!f || !m_vue.m_k) return {};
    for (const auto& w : f->fils)
      if (p == w.a || p == w.b || surSegment(p, w.a, w.b)) return m_vue.reseauDe(w.uuid);
    return {};
  }
  void demander(Pt p, const QString& propose) {
    VueSchema* v = &m_vue;
    OutilEtiquette* moi = this;
    const QPointer<VueSchema> garde(v);
    m_vue.saisirSurPlace(p, electrique() ? QStringLiteral("Nom du potentiel (L1, N, PE, 24V, 0V…) ou du renvoi") : QStringLiteral("Nom du réseau"), propose,
                         [v, moi, p, garde](const QString& t) {
                           if (!garde) return;
                           if (const QString r = refus(t); !r.isEmpty() && v->domaine() == Domaine::Electronique) {
                             emit v->message(QStringLiteral("Nom refusé : %1.").arg(r));
                             // Le champ revient, avec ce qui a été tapé
                             QTimer::singleShot(0, v, [v, moi, p, t, garde] {
                               if (garde && dynamic_cast<OutilEtiquette*>(v->m_outils.actif()) == moi) moi->demander(p, t);
                             });
                             return;
                           }
                           if (dynamic_cast<OutilEtiquette*>(v->m_outils.actif()) == moi) moi->poser(p, t.toStdString());
                         });
    // Liste des noms déjà présents : ceux de la feuille et les globaux, les plus utilisés en tête
    if (QLineEdit* e = m_vue.saisieEnCours()) {
      std::map<std::string, int> compte;
      if (const ConceptionElec* c = m_vue.conception())
        for (std::size_t i = 0; i < c->feuilles.size(); ++i)
          for (const auto& x : c->feuilles[i].etiquettes)
            if (static_cast<int>(i) == m_vue.m_feuille || x.type != TypeEtiquette::Locale) ++compte[x.texte];
      std::vector<std::pair<int, std::string>> tri;
      for (const auto& [n, k] : compte) tri.push_back({-k, n});
      std::sort(tri.begin(), tri.end());
      QStringList noms;
      for (const auto& [k, n] : tri) noms << q(n);
      auto* completion = new QCompleter(noms, e);
      completion->setCaseSensitivity(Qt::CaseInsensitive);
      e->setCompleter(completion);
    }
  }
  void poser(Pt p, const std::string& t) {
    const TypeEtiquette type = m_type;
    const SensSignal sens = m_sens;
    const int rotation = m_rotation;
    std::string id;
    m_vue.modifier([&](Feuille& f, ConceptionElec&) {
      Etiquette e;
      e.uuid = id = nouvelUuid();
      e.texte = t;
      e.p = p;
      e.rotation = rotation;
      e.type = type;
      e.sens = sens;
      f.etiquettes.push_back(e);
    }, {}, QStringLiteral("Étiquette %1").arg(q(t)));
    if (m_nom.empty()) m_vue.m_derniereEtiquette = t;
    m_vue.noterPose(id);
    m_vue.majBandeau();
  }
  std::string m_nom;
  TypeEtiquette m_type;
  SensSignal m_sens = SensSignal::Aucun;
  int m_rotation = 0;
};

// ---------- Jonction, non-connexion ----------
class VueSchema::OutilPoint : public OutilBase {
 public:
  OutilPoint(VueSchema& v, Outil genre) : OutilBase(v, genre) {}
  void survol(const GesteVue& g) override {
    m_curseur = point(g);
    m_dedans = true;
    m_vue.update();
  }
  void appui(const GesteVue& g) override {
    const Pt p = point(g);
    if (m_genre == Outil::Jonction) m_vue.modifier([&](Feuille& f, ConceptionElec&) { f.jonctions.push_back({p, nouvelUuid()}); });
    else m_vue.modifier([&](Feuille& f, ConceptionElec&) { f.nonConnexions.push_back({p, nouvelUuid()}); });
  }
  void dessiner(QPainter& g) const override {
    if (m_dedans) reticule(g, m_curseur);
  }
  ContenuBandeau bandeau() const override {
    ContenuBandeau c;
    c.titre = m_genre == Outil::Jonction ? QStringLiteral("Jonction") : QStringLiteral("Non connecté");
    c.detail = m_genre == Outil::Jonction ? QStringLiteral("clic sur un croisement de fils pour les relier") : QStringLiteral("clic sur une broche qui doit rester libre");
    c.actions.push_back(sortie());
    return c;
  }

 private:
  Pt point(const GesteVue& g) const { return m_genre == Outil::NonConnexion ? accroche(g) : grille(g); }
};

// ---------- Gomme (2.25, SAI-37) : chaque clic supprime l'objet visé, en un pas, jusqu'à « Terminer » ----------
class VueSchema::OutilGomme : public OutilBase {
 public:
  explicit OutilGomme(VueSchema& v) : OutilBase(v, Outil::Gomme) {}
  QCursor curseur() const override { return QCursor(m_vise.empty() ? Qt::CrossCursor : Qt::PointingHandCursor); }
  void survol(const GesteVue& g) override {
    const std::string o = m_vue.objetSous(g.ecran);
    m_dedans = true;
    if (o != m_vise) {
      m_vise = o;
      m_vue.setCursor(curseur());
      m_vue.majBandeau();
    }
  }
  void appui(const GesteVue& g) override {
    m_vise = m_vue.objetSous(g.ecran);
    if (m_vise.empty() || m_vue.refuseVerrou()) return;
    const QString nom = m_vue.libelleObjet(m_vise);
    m_vue.selectionner({m_vise});
    m_vue.supprimerSelection();
    ++m_effaces;
    m_vise.clear();
    emit m_vue.message(QStringLiteral("%1 supprimé · clic sur le suivant, « Terminer » pour finir").arg(nom));
    m_vue.majBandeau();
  }
  ContenuBandeau bandeau() const override {
    ContenuBandeau c;
    c.titre = QStringLiteral("Gomme");
    c.detail = !m_vise.empty() ? QStringLiteral("clic : supprime %1").arg(m_vue.libelleObjet(m_vise))
               : m_effaces ? QStringLiteral("%1 objet(s) supprimé(s) · clic sur le suivant").arg(m_effaces)
                           : QStringLiteral("clic sur un objet pour le supprimer");
    c.actions.push_back(sortie());
    return c;
  }

 private:
  std::string m_vise;
  int m_effaces = 0;
};

// ---------- Texte ----------
class VueSchema::OutilTexte : public OutilBase {
 public:
  explicit OutilTexte(VueSchema& v) : OutilBase(v, Outil::Texte) {}
  // Les textes ont leur grille (2.28)
  void survol(const GesteVue& g) override {
    m_curseur = m_vue.surGrilleTextes(m_vue.monde(g.ecran));
    m_dedans = true;
    m_vue.update();
  }
  void appui(const GesteVue& g) override {
    const Pt p = m_vue.surGrilleTextes(m_vue.monde(g.ecran));
    if (m_vue.refuseVerrou()) return;
    VueSchema* v = &m_vue;
    m_vue.saisirSurPlace(p, QStringLiteral("Texte à écrire sur la feuille"), QString(), [v, p](const QString& t) {
      v->modifier([&](Feuille& f, ConceptionElec&) { f.textes.push_back({nouvelUuid(), t.toStdString(), p, 1'800'000}); });
    });
  }
  void dessiner(QPainter& g) const override {
    if (m_dedans) reticule(g, m_curseur);
  }
  ContenuBandeau bandeau() const override {
    ContenuBandeau c;
    c.titre = QStringLiteral("Texte");
    c.detail = QStringLiteral("clic où l’écrire · grille des textes : %1").arg(m_vue.grilleTextes().nom);
    c.actions.push_back(sortie());
    return c;
  }
};

// ---------- Fabrique et commandes de la vue qui touchent aux outils ----------
std::unique_ptr<OutilVue> VueSchema::creerOutil(Outil o, const std::string& parametre) {
  switch (o) {
    case Outil::Selection: return std::make_unique<OutilSelection>(*this);
    case Outil::Placer:
    case Outil::Alimentation: return std::make_unique<OutilPose>(*this, o, parametre);
    case Outil::Fil: return std::make_unique<OutilFil>(*this);
    case Outil::Etiquette:
    case Outil::EtiquetteGlobale: return std::make_unique<OutilEtiquette>(*this, o, parametre);
    case Outil::Jonction:
    case Outil::NonConnexion: return std::make_unique<OutilPoint>(*this, o);
    case Outil::Texte: return std::make_unique<OutilTexte>(*this);
    case Outil::Gomme: return std::make_unique<OutilGomme>(*this);
  }
  return std::make_unique<OutilSelection>(*this);
}

void VueSchema::armerPose(const DefSymbole& d) {
  setOutil(d.alimentation || d.drapeau ? Outil::Alimentation : Outil::Placer, d.id);
  if (auto* p = dynamic_cast<OutilPose*>(m_outils.actif())) p->setDefinition(d);
  update();
}

void VueSchema::glisserArticle(const DefSymbole& d, QPointF q) {
  if (!m_poseGlissee || symboleArme() != d.id) {
    armerPose(d);
    m_poseGlissee = true;
  }
  m_souris = q;
  m_outils.actif()->survol(GesteVue{q, Qt::NoButton, Qt::LeftButton, QGuiApplication::keyboardModifiers()});
  update();
}

bool VueSchema::deposerArticle(QPointF q) {
  if (!m_poseGlissee) return false;
  m_poseGlissee = false;
  auto* p = dynamic_cast<OutilPose*>(m_outils.actif());
  if (!p) return false;
  const int avant = p->poses();
  const GesteVue g{q, Qt::LeftButton, Qt::NoButton, QGuiApplication::keyboardModifiers()};
  p->survol(g);
  p->relacher(g);  // l'outil remplacé par Sélection (pose à un coup) reste en vie jusqu'au geste suivant
  const bool pose = p->poses() > avant;
  setFocus(Qt::OtherFocusReason);
  majBandeau();
  update();
  return pose;
}

void VueSchema::abandonnerArticle() {
  if (!m_poseGlissee) return;
  m_poseGlissee = false;
  setOutil(Outil::Selection);
}

bool VueSchema::annotationALaPose() { return QSettings().value(QStringLiteral("schema/annotationALaPose"), true).toBool(); }
void VueSchema::setAnnotationALaPose(bool oui) { QSettings().setValue(QStringLiteral("schema/annotationALaPose"), oui); }

// La dernière pose, et l'écart avec la précédente quand elles sont du même article (Inser, 2.3)
void VueSchema::noterPose(const std::string& symbole) {
  const Feuille* f = feuilleCourante();
  if (!f) return;
  // Une étiquette : l'écart avec la précédente étiquette posée (D0, D1, D2… au même pas)
  for (const auto& e : f->etiquettes)
    if (e.uuid == symbole) {
      const auto avant = std::find_if(f->etiquettes.begin(), f->etiquettes.end(), [&](const Etiquette& x) { return x.uuid == m_dernierePose; });
      if (avant != f->etiquettes.end() && avant->p != e.p) m_ecartPose = e.p - avant->p;
      else if (avant == f->etiquettes.end()) m_ecartPose.reset();
      m_dernierePose = symbole;
      return;
    }
  const SymbolePlace *avant = nullptr, *apres = nullptr;
  for (const auto& s : f->symboles) {
    if (s.uuid == m_dernierePose) avant = &s;
    if (s.uuid == symbole) apres = &s;
  }
  if (!apres) return;
  if (avant && avant->symbole == apres->symbole && avant->position != apres->position) m_ecartPose = apres->position - avant->position;
  else if (!avant || avant->symbole != apres->symbole) m_ecartPose.reset();
  m_dernierePose = symbole;
}

bool VueSchema::repeterPose() {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f || domaine() != Domaine::Electronique) return false;
  // La dernière pose est une étiquette : la suivante de la série, au même pas (2,54 mm plus bas à défaut)
  for (const auto& e : f->etiquettes)
    if (e.uuid == m_dernierePose) {
      if (refuseVerrou()) return false;
      Etiquette n = e;
      n.uuid = nouvelUuid();
      n.texte = suivant(e.texte);
      const Pt ecart = m_ecartPose.value_or(ptMm(0, 2.54));
      n.p = e.p + ecart;
      modifier([&](Feuille& fe, ConceptionElec&) { fe.etiquettes.push_back(n); }, {}, QStringLiteral("Étiquette %1").arg(QString::fromStdString(n.texte)));
      m_derniereEtiquette = n.texte;
      noterPose(n.uuid);
      m_ecartPose = ecart;
      selectionner({n.uuid});
      return true;
    }
  const SymbolePlace* s = nullptr;
  for (const auto& x : f->symboles)
    if (x.uuid == m_dernierePose) s = &x;
  if (!s || s->composant.empty() || refuseVerrou()) return false;
  const SymbolePlace modele = *s;
  const ComposantElec* k = c->composant(s->composant);
  if (!k) return false;
  const ComposantElec champs = *k;
  const Pt ecart = m_ecartPose.value_or(ptMm(5.08, 0));
  const int fi = m_feuille;
  const bool annoter = annotationALaPose();
  std::string pose, repere;
  modifier(
      [&](Feuille&, ConceptionElec& cc) {
        pose = poserSymbole(cc, static_cast<std::size_t>(fi), modele.symbole, modele.position + ecart, modele.rotation);
        std::string composant;
        for (auto& x : cc.feuilles[static_cast<std::size_t>(fi)].symboles)
          if (x.uuid == pose) {
            x.miroir = modele.miroir;
            composant = x.composant;
          }
        if (ComposantElec* n = cc.composant(composant)) {
          const std::string uuid = n->uuid;
          *n = champs;
          n->uuid = uuid;
          n->reference = (champs.reference.empty() ? std::string("U") : champs.reference.substr(0, champs.reference.find_first_of("0123456789?"))) + "?";
          repere = annoter ? annoterALaPose(cc, composant) : n->reference;
        }
        if (!pose.empty()) replacerChamps(cc, static_cast<std::size_t>(fi), pose);  // 2.23
      },
      {}, QStringLiteral("Répéter la pose : %1").arg(QString::fromStdString(k->reference)));
  if (pose.empty()) return false;
  noterPose(pose);
  m_ecartPose = ecart;
  selectionner({pose});
  emit message(QStringLiteral("%1 posé à %2 mm, %3 mm de %4 · <b>Inser</b> : le suivant")
                   .arg(QString::fromStdString(repere), QLocale().toString(mm(ecart.x), 'f', 2), QLocale().toString(mm(ecart.y), 'f', 2), QString::fromStdString(champs.reference)));
  return true;
}

bool VueSchema::poserCopie() {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f || m_selection.size() != 1 || domaine() != Domaine::Electronique) return false;
  for (const auto& s : f->symboles)
    if (m_selection.count(s.uuid) && !s.composant.empty())
      if (const ComposantElec* k = c->composant(s.composant)) {
        const ComposantElec modele = *k;
        const int rotation = s.rotation;
        const bool miroir = s.miroir;
        setOutil(Outil::Placer, s.symbole);
        if (auto* p = dynamic_cast<OutilPose*>(m_outils.actif())) p->setModele(modele, rotation, miroir);
        majBandeau();
        update();
        return true;
      }
  return false;
}

void VueSchema::setPoseEnSerie(bool oui) {
  if (auto* p = dynamic_cast<OutilPose*>(m_outils.actif())) {
    p->setSerie(oui);
    emit outilChange();
  }
}

std::string VueSchema::symboleArme() const {
  const auto* p = dynamic_cast<const OutilPose*>(m_outils.actif());
  return p ? p->symbole() : std::string();
}

bool VueSchema::poseEnSerie() const {
  const auto* p = dynamic_cast<const OutilPose*>(m_outils.actif());
  return p && p->serie();
}

void VueSchema::tournerSelection(bool inverse) {
  // Pendant une pose, R tourne le fantôme (S11) ; sinon la sélection, fils accrochés compris (EL-19) ; Maj+R dans
  // l'autre sens, en un pas
  if (auto* p = dynamic_cast<OutilPose*>(m_outils.actif())) {
    p->tourner(inverse);
    return;
  }
  if (auto* e = dynamic_cast<OutilEtiquette*>(m_outils.actif())) {
    e->tourner(inverse);
    return;
  }
  if (m_selection.empty()) return;
  const std::set<std::string> sel = m_selection;
  const int fi = m_feuille;
  const bool champs = domaine() == Domaine::Electronique;
  modifier([&](Feuille& f, ConceptionElec& c) {
    for (int i = 0; i < (inverse ? 3 : 1); ++i) transformerSelectionSchema(c, static_cast<std::size_t>(fi), sel, OperationSchema::Tourner);
    // Repère et valeur reprennent une place libre autour du corps tourné, sauf un champ bloqué (2.23, SAI-34)
    if (champs)
      for (const auto& s : f.symboles)
        if (sel.count(s.uuid)) replacerChamps(c, static_cast<std::size_t>(fi), s.uuid);
  });
}

void VueSchema::miroirSelection(bool hautBas) {
  if (auto* p = dynamic_cast<OutilPose*>(m_outils.actif())) {
    p->retourner(hautBas);
    return;
  }
  if (m_selection.empty()) return;
  const std::set<std::string> sel = m_selection;
  const int fi = m_feuille;
  modifier([&](Feuille&, ConceptionElec& c) {
    transformerSelectionSchema(c, static_cast<std::size_t>(fi), sel, hautBas ? OperationSchema::MiroirHautBas : OperationSchema::MiroirGaucheDroite);
  });
}

bool VueSchema::collerSousLaSouris(const PressePapiersSchema& pp, std::optional<ReperesCollage> reperes) {
  if (outil() != Outil::Selection) setOutil(Outil::Selection);
  auto* s = dynamic_cast<OutilSelection*>(m_outils.actif());
  return s && s->collerFlottant(pp, reperes);
}

void VueSchema::commencerFilDepuis(const BrocheVisee& b) {
  m_filDepuisSelection = true;
  setOutil(Outil::Fil);
  m_outils.actif()->appui(GesteVue{ecran(b.p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier});
  majBandeau();
  update();
}

// Le fil commencé depuis Sélection est fini (posé, ou retiré) : la main revient à Sélection, au retour à la boucle
// (l'outil Fil termine son geste avant d'être rendu)
void VueSchema::finFilDepuisSelection() {
  if (!m_filDepuisSelection) return;
  m_filDepuisSelection = false;
  QMetaObject::invokeMethod(this, [this] {
    if (outil() == Outil::Fil) setOutil(Outil::Selection);
  }, Qt::QueuedConnection);
}

std::optional<Pt> VueSchema::dernierPointTrace() const {
  const auto* f = dynamic_cast<const OutilFil*>(m_outils.actif());
  return f ? f->dernier() : std::nullopt;
}

bool VueSchema::poserPointTrace(Pt p) {
  auto* f = dynamic_cast<OutilFil*>(m_outils.actif());
  return f && f->poserPoint(p);
}

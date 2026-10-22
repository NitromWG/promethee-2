// Prométhée : outils du canevas du circuit imprimé (lot L1, tâche 1.6, A5). Sélection (glisser, cadre, sommets de
// zone), routage interactif à 45° avec vias et isolement vérifié en direct, via, zone de cuivre, contour et découpe,
// mesure, texte. Chaque outil est une classe imbriquée de VueCircuit. Échap retire ce qui suit la souris et garde ce
// qui est fixé, pour une piste comme pour un polygone ; Ctrl+Z pendant le tracé le retire en entier, sans toucher à
// l'historique ; Entrée ou un double-clic valide (C14).
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <optional>

#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPainter>

#include "BandeauOutil.hpp"
#include "Curseurs.hpp"
#include "GestesSelection.hpp"
#include "Document.hpp"
#include "EncresCircuit.hpp"
#include "RegistreActions.hpp"
#include "SelectionElectronique.hpp"
#include "VueCircuit.hpp"
#include "CartoucheRoutage.hpp"
#include "promethee/modele.hpp"
#include "promethee/routeur.hpp"

using namespace prom::ecad;
using namespace encres_circuit;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

// ---------- Base ----------
class VueCircuit::OutilBase : public OutilVue {
 public:
  OutilBase(VueCircuit& v, Outil genre) : m_vue(v), m_genre(genre) {}
  int genre() const override { return static_cast<int>(m_genre); }

 protected:
  ActionBandeau sortie(const QString& libelle = QStringLiteral("Terminer")) const {
    ActionBandeau a;
    a.id = QStringLiteral("terminer");
    a.libelle = libelle;
    a.touche = QStringLiteral("Échap");
    VueCircuit* v = &m_vue;
    a.faire = [v] { v->echap(); };
    a.sortie = true;
    a.action = "commun.echap";
    return a;
  }
  VueCircuit& m_vue;
  Outil m_genre;
};

// ---------- Sélection ----------
class VueCircuit::OutilSelection : public OutilBase {
 public:
  explicit OutilSelection(VueCircuit& v) : OutilBase(v, Outil::Selection) {}
  // Croix à quatre flèches sur un objet choisi qu'on peut glisser ; cercle barré sur une empreinte verrouillée
  QCursor curseur() const override {
    if (m_glisse) return Qt::SizeAllCursor;
    return m_surVerrou ? Qt::ForbiddenCursor : m_surChoisi ? Qt::SizeAllCursor : Qt::ArrowCursor;
  }
  void survol(const GesteVue& g) override {
    const ObjetChoisi o = m_vue.objetSous(g.ecran);
    m_surChoisi = o.genre != Genre::Rien && m_vue.estChoisi(o.genre, o.id);
    m_surVerrou = false;
    if (m_surChoisi && o.genre == Genre::Empreinte)
      if (const ConceptionElec* c = m_vue.conception())
        for (const auto& e : c->circuit.empreintes)
          if (e.composant == o.id) m_surVerrou = e.verrou;
  }
  bool geste() const override { return m_glisse || m_cadre; }

  void appui(const GesteVue& g) override {
    m_pression = m_souris = g.ecran;
    const Pt w = m_vue.monde(g.ecran);
    // Alt+clic : la liste de tous les objets sous le curseur, zones comprises (C10)
    if (g.mods & Qt::AltModifier) {
      m_vue.listerObjetsSous(g.ecran);
      return;
    }
    // Sommet d'une zone choisie : il se glisse
    m_sommet = m_vue.sommetSous(g.ecran);
    if (m_sommet >= 0) {
      if (m_vue.refuseVerrou()) {
        m_sommet = -1;
        return;
      }
      m_appuye = true;
      return;
    }
    // Repère de sérigraphie (5.5, C6) : il se glisse seul et garde sa place, qui suit ensuite l'empreinte
    if (!(g.mods & (Qt::ShiftModifier | Qt::ControlModifier)))
      if (const std::string k = m_vue.repereSous(g.ecran); !k.empty()) {
        m_repere = k;
        m_appuye = true;
        m_cadre = false;
        m_vue.choisir(Genre::Empreinte, k);
        if (const ConceptionElec* c = m_vue.conception())
          for (const auto& e : c->circuit.empreintes)
            if (e.composant == k)
              if (const DefEmpreinte* d = c->empreinte(e.empreinte)) m_ecartRepere = w - positionRepere(e, *d, c->circuit.serigraphie.hauteur);
        return;
      }
    const ObjetChoisi sous = m_vue.objetSous(g.ecran);
    const auto mode = m_vue.m_sel->modeClic(g.mods);  // Maj, Ctrl, ou le mode du bandeau
    m_mode = static_cast<int>(mode);
    // Dans le vide : l'état neutre, puis un cadre si la souris glisse ; un objet n'est glissé que par un clic simple
    m_cadre = sous.genre == Genre::Rien;
    m_appuye = !m_cadre && mode == SelectionElectronique::Mode::Remplacer;
    m_vue.choisirSous(g.ecran, g.mods);
    m_vue.m_departGlisse = w;
  }
  void glisser(const GesteVue& g) override {
    m_souris = g.ecran;
    if (m_cadre) {
      m_vue.update();
      return;
    }
    VueCircuit& v = m_vue;
    if (m_appuye && !m_repere.empty()) {
      if (!m_glisse) {
        if ((m_souris - m_pression).manhattanLength() <= 3) return;
        if (v.refuseVerrou()) {
          m_appuye = false;
          m_repere.clear();
          return;
        }
        v.m_doc->debutGesteConception(v.m_conception);
        m_glisse = true;
      }
      const Pt p = v.surGrilleRoutage(v.monde(m_souris) - m_ecartRepere);
      const std::string id = m_repere;
      const int ci = v.m_conception;
      v.m_doc->pendantGeste([&](prom::Projet& pr) {
        if (ci < 0 || ci >= static_cast<int>(pr.electronique.size())) return;
        for (auto& e : pr.electronique[static_cast<std::size_t>(ci)].circuit.empreintes)
          if (e.composant == id) e.repere = versEmpreinte(e, p);
      }, true);
      return;
    }
    if (m_appuye && m_sommet >= 0 && v.m_genre == Genre::Zone) {
      if (!m_glisse) {
        v.m_doc->debutGesteConception(v.m_conception);
        m_glisse = true;
      }
      const Pt p = v.surGrilleRoutage(v.monde(m_souris));
      const std::string id = v.m_choisi;
      const int ci = v.m_conception, k = m_sommet;
      v.m_doc->pendantGeste([&](prom::Projet& pr) {
        if (ci < 0 || ci >= static_cast<int>(pr.electronique.size())) return;
        for (auto& z : pr.electronique[static_cast<std::size_t>(ci)].circuit.zones)
          if (z.uuid == id && k < static_cast<int>(z.contour.size())) z.contour[static_cast<std::size_t>(k)] = p;
      }, true);
      return;
    }
    if (!m_appuye || v.m_selection.empty()) return;
    if (!m_glisse && (m_souris - m_pression).manhattanLength() > 3) {
      const ConceptionElec* c = v.conception();
      if (!c || v.refuseVerrou()) {
        m_appuye = false;
        return;
      }
      bool bouge = false, verrou = false;
      for (const auto& o : v.m_selection) {
        if (o.genre != Genre::Empreinte) {
          bouge = true;
          continue;
        }
        for (const auto& em : c->circuit.empreintes)
          if (em.composant == o.id) (em.verrou ? verrou : bouge) = true;
      }
      if (!bouge) {
        if (verrou) emit v.message(QStringLiteral("Empreinte verrouillée : déverrouille-la dans le panneau pour la déplacer."));
        m_appuye = false;
        return;
      }
      v.m_circuitDepart = c->circuit;
      v.m_accroches = v.accroches(v.m_selection);
      v.m_doc->debutGesteConception(v.m_conception);
      m_glisse = true;
      v.commencerGlissement();  // grande carte : fond peint une fois, zones suivies sur place (9.4, 9.9)
      // L'empreinte tenue se cale sur la grille de placement ; sinon le premier objet sur celle du routage
      v.m_positionDepart = Pt{};
      for (const auto& em : c->circuit.empreintes)
        if (v.m_genre == Genre::Empreinte && em.composant == v.m_choisi) v.m_positionDepart = em.position;
    }
    if (!m_glisse) return;
    const bool empreinte = v.m_genre == Genre::Empreinte;
    const Pt brut = v.monde(m_souris) - v.m_departGlisse;
    const Pt d = surGrille(v.m_positionDepart + brut, empreinte ? v.pasPlacement() : v.pasRoutage()) - v.m_positionDepart;
    const int ci = v.m_conception;
    v.m_doc->pendantGeste([&](prom::Projet& pr) {
      if (ci < 0 || ci >= static_cast<int>(pr.electronique.size())) return;
      v.appliquerDeplacement(pr.electronique[static_cast<std::size_t>(ci)], d);
    }, true);
    m_delta = d;
  }
  void relacher(const GesteVue& g) override {
    m_souris = g.ecran;
    m_appuye = false;
    if (m_cadre) {
      m_cadre = false;
      if ((g.ecran - m_pression).manhattanLength() > 3) m_vue.choisirDansCadre(m_vue.monde(m_pression), m_vue.monde(g.ecran), g.ecran.x() < m_pression.x(), m_mode);
      m_vue.update();
      return;
    }
    m_sommet = -1;
    m_repere.clear();
    if (m_glisse) {
      m_glisse = false;
      m_vue.m_accroches.clear();
      // L'image du glissement reste jusqu'au calcul qui suit : la carte n'est repeinte entière qu'une fois (9.2)
      m_vue.m_doc->finGeste();
      m_vue.garderGlissement();
    }
  }
  void doubleClic(const GesteVue& g) override {
    VueCircuit& v = m_vue;
    if (v.m_genre == Genre::Zone) {
      // Zone choisie : double-clic sur un sommet pour le retirer, sur un côté pour en ajouter un
      const std::string id = v.m_choisi;
      if (const int k = v.sommetSous(g.ecran); k >= 0) {
        v.modifier([&](ConceptionElec& c) {
          for (auto& z : c.circuit.zones)
            if (z.uuid == id && z.contour.size() > 3) z.contour.erase(z.contour.begin() + k);
        });
        return;
      }
      const auto [i, p] = v.areteSous(g.ecran);
      if (i >= 0) {
        v.modifier([&](ConceptionElec& c) {
          for (auto& z : c.circuit.zones)
            if (z.uuid == id) z.contour.insert(z.contour.begin() + i + 1, p);
        });
        return;
      }
    }
    // Double-clic sur une piste : la chaîne de segments ; avec Maj, tout le cuivre du réseau (SAI-18)
    std::string reseau;
    const ObjetChoisi o = v.objetSous(g.ecran, &reseau);
    if (o.genre == Genre::Piste) v.choisirChaine(o.id, g.mods & Qt::ShiftModifier);
  }
  bool agir(std::string_view a, bool faire) override {
    // Pendant un glisser, seul Échap (tenu par la vue) agit
    if (m_glisse) return false;
    VueCircuit& v = m_vue;
    if (a == "selection.supprimer") {
      if (faire) v.supprimerChoix();
      return true;
    }
    if (a == "objet.tourner" || a == "objet.tournerInverse") {
      if (faire) v.tournerChoix(a == "objet.tournerInverse");
      return true;
    }
    if (a == "circuit.changerFace") {
      if (faire) v.retournerChoix();
      return true;
    }
    // Flèches : un pas de la grille de placement ou de routage, dix avec Maj ; les appuis rapprochés font un seul
    // pas et un seul recalcul (C18)
    if (a.substr(0, 10) == "selection.") {
      const bool dix = a.back() == '0';
      const std::string_view sens = a.substr(10, a.size() - 10 - (dix ? 2 : 0));
      if (sens != "gauche" && sens != "droite" && sens != "haut" && sens != "bas") return false;
      if (v.m_selection.empty()) return false;
      if (faire) {
        const Nm pas = (v.m_genre == Genre::Empreinte ? v.pasPlacement() : v.pasRoutage()) * (dix ? 10 : 1);
        v.deplacerSelection({sens == "gauche" ? -pas : sens == "droite" ? pas : 0, sens == "haut" ? -pas : sens == "bas" ? pas : 0});
      }
      return true;
    }
    return false;
  }
  bool echap() override {
    if (m_glisse) {
      m_glisse = m_appuye = false;
      m_sommet = -1;
      m_vue.m_accroches.clear();
      m_vue.finirGlissement();
      m_vue.m_doc->annulerGeste();
      return true;
    }
    if (m_cadre) {
      m_cadre = false;
      m_vue.update();
      return true;
    }
    return false;
  }
  void gesteInterrompu() override {
    m_glisse = m_appuye = false;
    m_sommet = -1;
    m_vue.m_accroches.clear();
    m_vue.finirGlissement();
  }
  void dessiner(QPainter& g) const override {
    if (!m_cadre || (m_souris - m_pression).manhattanLength() <= 3) return;
    // Cadre de sélection : plein de gauche à droite (objets entiers), pointillé de droite à gauche (objets touchés)
    const bool touches = m_souris.x() < m_pression.x();
    g.setPen(QPen(CHOIX, 1, touches ? Qt::DashLine : Qt::SolidLine));
    QColor f = CHOIX;
    f.setAlpha(touches ? 25 : 40);
    g.setBrush(f);
    g.drawRect(QRectF(m_pression, m_souris).normalized());
    g.setBrush(Qt::NoBrush);
  }
  ContenuBandeau bandeau() const override {
    if (!m_glisse) return bandeauSelection(m_vue.m_sel, sortie());
    ContenuBandeau c;
    c.titre = QStringLiteral("Déplacement");
    c.detail = QStringLiteral("Δx %1, Δy %2").arg(m_vue.longueur(static_cast<double>(m_delta.x)), m_vue.longueur(static_cast<double>(m_delta.y)));
    c.actions.push_back(sortie(QStringLiteral("Annuler")));
    return c;
  }

 private:
  QPointF m_pression, m_souris;
  int m_mode = 0, m_sommet = -1;
  bool m_appuye = false, m_glisse = false, m_cadre = false, m_surChoisi = false, m_surVerrou = false;
  Pt m_delta{};
  std::string m_repere;  // composant dont le repère de sérigraphie se glisse
  Pt m_ecartRepere{};
};

// ---------- Routage ----------
class VueCircuit::OutilRoutage : public OutilBase {
 public:
  explicit OutilRoutage(VueCircuit& v) : OutilBase(v, Outil::Router) {}
  bool geste() const override { return m_route; }
  // Réticule cerclé sur une pastille, où la piste commence ou finit
  QCursor curseur() const override { return m_vue.pastilleSous(m_vue.monde(m_souris)) >= 0 ? curseurs::accroche() : QCursor(Qt::CrossCursor); }
  void survol(const GesteVue& g) override {
    m_souris = g.ecran;
    m_vue.update();
  }
  void glisser(const GesteVue& g) override { survol(g); }
  bool enCours() const { return m_route; }
  Pt tete() const { return m_courant; }
  const std::string& reseau() const { return m_reseau; }
  Nm largeur() const { return m_route ? m_largeur : 0; }
  QPointF souris() const { return m_souris; }
  void appui(const GesteVue& g) override {
    m_souris = g.ecran;
    VueCircuit& v = m_vue;
    const Pt w = v.monde(g.ecran);
    if (!m_route) {
      commencer(w);
      return;
    }
    Pt cible = v.surGrilleRoutage(w);
    const int i = v.pastilleSous(w);
    bool arrivee = i >= 0 && v.m_calc->pastilles[static_cast<std::size_t>(i)].reseau == m_reseau && !m_reseau.empty();
    if (arrivee) cible = v.m_calc->pastilles[static_cast<std::size_t>(i)].pastille.position;
    // Arrivée sur une piste du même réseau, en T : elle se coupera au point d'arrivée (EL-26)
    std::string pisteArrivee;
    if (!arrivee && !m_reseau.empty())
      if (const ConceptionElec* c = v.conception())
        for (const auto& t : c->circuit.pistes)
          if (pisteArrivee.empty() && t.couche == v.m_coucheActive && t.reseau == m_reseau && distancePointSegment(w, t.a, t.b) <= static_cast<double>(t.largeur) / 2 + 3.0 / v.m_echelle * 1e6) {
            pisteArrivee = t.uuid;
            cible = distance(w, t.a) <= static_cast<double>(t.largeur) ? t.a : distance(w, t.b) <= static_cast<double>(t.largeur) ? t.b : projeteSurSegment(w, t.a, t.b);
            arrivee = true;
          }
    // Ctrl + clic force le segment malgré une collision (pas de sélection à changer pendant un tracé)
    if (avancer(cible, g.mods & Qt::ControlModifier) && arrivee) {
      if (!pisteArrivee.empty()) m_coupures.push_back({pisteArrivee, cible});
      finir();
    }
  }
  void doubleClic(const GesteVue&) override { finir(); }
  void quitter() override { finir(); }
  bool agir(std::string_view a, bool faire) override {
    if (a == "trace.coude") {
      if (faire) basculerAngle();
      return true;
    }
    // Le reste ne vaut que pendant un tracé ; hors tracé, V arme l'outil Via et PageUp change la couche active (vue)
    if (!m_route) return false;
    if (a == "circuit.via") {
      if (faire) poserVia();
    } else if (a == "circuit.coucheDessus" || a == "circuit.coucheDessous") {
      // Changer de couche pendant un tracé pose une via au dernier point, comme V (C11, ROU-05)
      if (faire) poserVia(a == "circuit.coucheDessus" ? Couche::CuivreDessus : Couche::CuivreDessous);
    } else if (a == "trace.retirerCoude") {
      if (faire) retirerCoude();
    } else if (a == "trace.terminer") {
      if (faire) finir();
    } else if (a == "trace.annuler") {
      if (faire) annulerTrace();
    } else
      return false;
    return true;
  }
  // Échap garde les segments fixés (la piste est posée), et retire celui qui suit la souris
  bool echap() override {
    if (!m_route) return false;
    finir();
    return true;
  }
  bool annulerTrace() override {
    if (!m_route) return false;
    m_route = false;
    m_segments.clear();
    m_vias.clear();
    m_coupures.clear();
    m_vue.majCartouche();
    emit m_vue.message(QStringLiteral("Tracé retiré : rien n’a été posé."));
    m_vue.update();
    return true;
  }
  void dessiner(QPainter& g) const override {
    if (!m_route) return;
    const VueCircuit& v = m_vue;
    // Auréole d'isolement autour des objets proches (5.20) : le bord que le cuivre du tracé ne doit pas franchir
    {
      QPen pen(QColor(255, 196, 64, 170), 1, Qt::DashLine);
      g.setPen(pen);
      g.setBrush(Qt::NoBrush);
      for (const auto& a : v.aureolesRoutage()) {
        QPolygonF poly;
        for (const Pt q : a.contour) poly << v.ecran(q);
        g.drawPolygon(poly);
      }
    }
    for (const auto& s : m_segments) {
      g.setPen(QPen(couleurCuivre(s.couche), std::max(1.0, mm(s.largeur) * v.m_echelle), Qt::SolidLine, Qt::RoundCap));
      g.drawLine(v.ecran(s.a), v.ecran(s.b));
    }
    const auto segs = trajet(v.surGrilleRoutage(v.monde(m_souris)));
    const bool heurt = collision(segs);
    for (const auto& [a, b] : segs) {
      g.setPen(QPen(heurt ? ROUGE : QColor(255, 255, 255, 200), std::max(1.0, mm(m_largeur) * v.m_echelle), Qt::SolidLine, Qt::RoundCap));
      g.drawLine(v.ecran(a), v.ecran(b));
    }
    for (const Pt& p : m_vias) {
      g.setPen(Qt::NoPen);
      g.setBrush(VIA);
      g.drawEllipse(v.ecran(p), 0.4 * v.m_echelle, 0.4 * v.m_echelle);
    }
  }
  // Barre du routeur (ROU-05, ERG-17) : tout s'y fait à la souris. Via (sa flèche : passer sur une autre couche,
  // type et arrivée de la prochaine via), paire de couches, largeur, retrait du dernier coude ou du tracé, coude,
  // fin
  ContenuBandeau bandeau() const override {
    ContenuBandeau c;
    OutilRoutage* moi = const_cast<OutilRoutage*>(this);
    VueCircuit* v = &m_vue;
    const ConceptionElec* conc = m_vue.conception();
    const std::vector<Couche> cuivres = couchesCuivre(conc ? conc->circuit.empilage.couchesCuivre : 2);
    auto courte = [](Couche x) {
      QString n = QString::fromUtf8(nomCouche(x));
      if (n.startsWith(QStringLiteral("Cuivre "))) n = n.mid(7);
      if (!n.isEmpty()) n[0] = n[0].toUpper();
      return n;
    };
    if (!m_route) {
      c.titre = QStringLiteral("Routage");
      c.detail = QStringLiteral("clic sur une pastille, une via ou une piste pour commencer");
    } else {
      c.titre = QStringLiteral("Routage %1").arg(m_reseau.empty() ? QStringLiteral("(sans réseau)") : q(m_reseau));
      c.detail = QString::fromUtf8(nomCouche(m_vue.m_coucheActive));
      ActionBandeau via = ActionBandeau::du("circuit.via", QStringLiteral("via"), QStringLiteral("Via → %1").arg(courte(m_vue.coucheApresVia())), [moi] { moi->poserVia(); });
      via.choix.push_back({QStringLiteral("Poser la via et passer sur"), {}, false, true, {}, {}, true});
      for (const Couche x : cuivres) {
        if (x == m_vue.m_coucheActive) continue;
        ChoixBandeau ligne{QString::fromUtf8(nomCouche(x)), [moi, x] { moi->poserVia(x); }};
        if (x == Couche::CuivreDessus) ligne.action = "circuit.coucheDessus";
        if (x == Couche::CuivreDessous) ligne.action = "circuit.coucheDessous";
        via.choix.push_back(ligne);
      }
      // Seule la traversante existe avant l'empilage complet (lot L8) : les autres sont grisées, leur raison en bulle
      via.choix.push_back({QStringLiteral("Prochaine via"), {}, false, true, {}, {}, true});
      via.choix.push_back({QStringLiteral("Traversante"), [] {}, true});
      for (const char* type : {"Borgne", "Enterrée", "Micro-via"})
        via.choix.push_back({QString::fromUtf8(type), {}, false, false, QStringLiteral("L’empilage de la carte n’en a pas : elles viendront avec l’empilage complet.")});
      via.choix.push_back({QStringLiteral("Arrivée de la prochaine via"), {}, false, true, {}, {}, true});
      via.choix.push_back({QStringLiteral("L’autre couche de la paire"), [v] { v->setArriveeVia(std::nullopt); }, !m_vue.arriveeVia().has_value()});
      for (const Couche x : cuivres)
        via.choix.push_back({QString::fromUtf8(nomCouche(x)), [v, x] { v->setArriveeVia(x); }, m_vue.arriveeVia() == x});
      c.actions.push_back(via);
    }
    // Paire de couches : V passe de l'une à l'autre
    const auto paire = m_vue.paireCouches();
    ActionBandeau choixPaire;
    choixPaire.id = QStringLiteral("paire");
    choixPaire.libelle = QStringLiteral("Paire %1 ↔ %2").arg(courte(paire.first), courte(paire.second));
    choixPaire.choix.push_back({QStringLiteral("Paire de couches"), {}, false, true, {}, {}, true});
    for (const auto& [x, y] : VueCircuit::pairesPossibles(static_cast<int>(cuivres.size())))
      choixPaire.choix.push_back({QStringLiteral("%1 ↔ %2").arg(courte(x), courte(y)), [v, x = x, y = y] { v->setPaireCouches(x, y); }, paire == std::pair{x, y}});
    c.actions.push_back(choixPaire);
    // Mode du routeur (5.7) : contourner, pousser, ignorer
    ActionBandeau mode;
    mode.id = QStringLiteral("modeRouteur");
    mode.libelle = VueCircuit::nomModeRouteur(m_vue.m_modeRouteur);
    mode.choix.push_back({QStringLiteral("Quand le tracé heurte un obstacle"), {}, false, true, {}, {}, true});
    for (const auto x : {ModeRouteur::Contourner, ModeRouteur::Pousser, ModeRouteur::Ignorer})
      mode.choix.push_back({VueCircuit::nomModeRouteur(x), [v, x] { v->setModeRouteur(x); }, m_vue.m_modeRouteur == x});
    c.actions.push_back(mode);
    if (m_route) {
      // Largeur : celle de la classe du réseau, ou une valeur courante, pour les segments qui suivent
      ActionBandeau largeur;
      largeur.id = QStringLiteral("largeur");
      largeur.libelle = QStringLiteral("Largeur %1").arg(mmTexte(static_cast<double>(m_largeur)));
      const Nm classe = conc ? largeurPiste(conc->circuit.regles, m_reseau) : m_largeur;
      largeur.choix.push_back({QStringLiteral("Largeur des segments suivants"), {}, false, true, {}, {}, true});
      largeur.choix.push_back({QStringLiteral("De la classe (%1)").arg(mmTexte(static_cast<double>(classe))), [moi, classe] { moi->setLargeur(classe); }, m_largeur == classe});
      for (const Nm l : {150'000, 200'000, 250'000, 300'000, 400'000, 500'000, 800'000, 1'000'000, 1'500'000})
        if (l != classe) largeur.choix.push_back({mmTexte(static_cast<double>(l)), [moi, l] { moi->setLargeur(l); }, m_largeur == l});
      c.actions.push_back(largeur);
      c.actions.push_back(ActionBandeau::du("trace.retirerCoude", QStringLiteral("retirer-coude"), QStringLiteral("Retirer le coude"), [moi] { moi->retirerCoude(); }));
      c.actions.push_back(ActionBandeau::du("trace.annuler", QStringLiteral("retirer-trace"), QStringLiteral("Retirer le tracé"), [moi] { moi->annulerTrace(); }));
    }
    c.actions.push_back(ActionBandeau::du("trace.coude", QStringLiteral("coude"), m_droitDabord ? QStringLiteral("Coude : droit d’abord") : QStringLiteral("Coude : 45° d’abord"),
                                          [moi] { moi->basculerAngle(); }));
    ActionBandeau fin = sortie();
    if (m_route) fin.action = "trace.terminer";  // pendant le tracé, Terminer pose ce qui est fixé
    c.actions.push_back(fin);
    return c;
  }

  // Routage piloté (VueCircuit::router, essais)
  bool commencer(Pt p) {
    VueCircuit& v = m_vue;
    const ConceptionElec* c = v.conception();
    if (!c || !v.m_calc || v.refuseVerrou()) return false;
    m_coupures.clear();
    const int i = v.pastilleSous(p);
    if (i >= 0) {
      const auto& x = v.m_calc->pastilles[static_cast<std::size_t>(i)];
      m_reseau = x.reseau;
      m_courant = x.pastille.position;
      if (x.pastille.type == TypePastille::CMS) v.m_coucheActive = x.dessous ? Couche::CuivreDessous : Couche::CuivreDessus;
    } else {
      bool trouve = false;
      for (const auto& t : c->circuit.pistes)
        for (const Pt bout : {t.a, t.b})
          if (!trouve && distance(bout, p) <= static_cast<double>(t.largeur)) {
            m_reseau = t.reseau;
            m_courant = bout;
            v.m_coucheActive = t.couche;
            trouve = true;
          }
      for (const auto& via : c->circuit.vias)
        if (!trouve && distance(via.p, p) <= static_cast<double>(via.diametre) / 2) {
          m_reseau = via.reseau;
          m_courant = via.p;
          trouve = true;
        }
      // N'importe quel point d'une piste, la couche active d'abord : la piste se coupera en deux au raccord (EL-26)
      for (int pass = 0; pass < 2 && !trouve; ++pass)
        for (const auto& t : c->circuit.pistes) {
          if (trouve || !v.coucheVisible(t.couche) || (pass == 0) != (t.couche == v.m_coucheActive)) continue;
          if (distancePointSegment(p, t.a, t.b) <= static_cast<double>(t.largeur) / 2 + 3.0 / v.m_echelle * 1e6) {
            const Pt qp = projeteSurSegment(p, t.a, t.b);
            m_reseau = t.reseau;
            m_courant = qp;
            v.m_coucheActive = t.couche;
            m_coupures.push_back({t.uuid, qp});
            trouve = true;
          }
        }
      if (!trouve) {
        emit v.message(QStringLiteral("Commence la piste sur une pastille, une via ou une piste."));
        return false;
      }
    }
    m_largeur = largeurPiste(c->circuit.regles, m_reseau);
    m_route = true;
    m_segments.clear();
    m_vias.clear();
    m_cacheValide = false;
    v.update();
    v.majBandeau();
    v.majCartouche();
    return true;
  }
  bool avancer(Pt cible, bool forcer) {
    // Pousser (5.7, ROU-02) : les pistes gênantes s'écartent ; ce qui est déjà tracé est posé avec elles, en un pas
    if (!forcer && m_vue.m_modeRouteur == ModeRouteur::Pousser && collision(trajetDirect(cible)))
      if (const ConceptionElec* c = m_vue.conception()) {
        ConceptionElec essai = *c;
        for (const auto& s : m_segments) essai.circuit.pistes.push_back({nouvelUuid(), m_reseau, s.couche, s.a, s.b, s.largeur});
        for (const Pt& p : m_vias) essai.circuit.vias.push_back({nouvelUuid(), m_reseau, p, viaDiametre(essai.circuit.regles, m_reseau), viaPercage(essai.circuit.regles, m_reseau)});
        OptionsTrace o;
        o.reseau = m_reseau;
        o.couche = m_vue.m_coucheActive;
        o.largeur = m_largeur;
        const ResultatPoussee r = pousser(essai, m_courant, cible, o);
        if (!r.ok) {
          emit m_vue.message(QStringLiteral("<span style='color:#C2402E'><b>Rien ne s’écarte</b> : %1.</span>").arg(q(r.raison).toHtmlEscaped()));
          return false;
        }
        m_vue.modifier([&](ConceptionElec& cc) { cc.circuit = essai.circuit; });
        m_segments.clear();
        m_vias.clear();
        m_courant = cible;
        m_cacheValide = false;
        QStringList noms;
        for (const auto& n : r.deplacees) noms << q(n).toHtmlEscaped();
        emit m_vue.message(QStringLiteral("Segment posé ; %1 écartée(s) : %2.").arg(noms.size()).arg(noms.join(QStringLiteral(", "))));
        m_vue.update();
        m_vue.majCartouche();
        return true;
      }
    const auto segs = trajet(cible);
    std::string avec;
    if (!forcer && collision(segs, &avec)) {
      emit m_vue.message(QStringLiteral("<span style='color:#C2402E'><b>Collision</b> avec %1 : l’isolement ne serait pas respecté.</span> Contourne, ou Ctrl + clic pour forcer.").arg(q(avec)));
      return false;
    }
    for (const auto& [a, b] : segs) m_segments.push_back({a, b, m_vue.m_coucheActive, m_largeur});
    m_courant = cible;
    m_cacheValide = false;
    m_vue.update();
    m_vue.majCartouche();
    return true;
  }
  void finir() {
    if (!m_route) return;
    m_route = false;
    const auto segments = m_segments;
    const auto vias = m_vias;
    const std::string reseau = m_reseau;
    const auto coupures = m_coupures;
    m_segments.clear();
    m_vias.clear();
    m_coupures.clear();
    m_cacheValide = false;
    m_vue.majBandeau();
    m_vue.majCartouche();
    if (segments.empty() && vias.empty()) {
      m_vue.update();
      return;
    }
    m_vue.modifier([&](ConceptionElec& c) {
      for (const auto& s : segments) c.circuit.pistes.push_back({nouvelUuid(), reseau, s.couche, s.a, s.b, s.largeur});
      for (const Pt& p : vias) c.circuit.vias.push_back({nouvelUuid(), reseau, p, viaDiametre(c.circuit.regles, reseau), viaPercage(c.circuit.regles, reseau)});
      // Raccords en T : la piste touchée se coupe en deux au point de raccord, qui devient une extrémité commune
      for (const Coupure& k : coupures) {
        auto& v = c.circuit.pistes;
        for (std::size_t i = 0; i < v.size(); ++i) {
          if (v[i].uuid != k.piste || distance(v[i].a, k.p) < 1000 || distance(v[i].b, k.p) < 1000) continue;
          Piste suite = v[i];
          suite.uuid = nouvelUuid();
          suite.a = k.p;
          v[i].b = k.p;
          v.push_back(suite);
          break;
        }
      }
    });
    emit m_vue.message(QStringLiteral("Piste posée."));
  }
  // Via au dernier point ; la piste continue sur la couche suivante, ou sur « vers » si elle est donnée
  void poserVia(std::optional<Couche> vers = std::nullopt) {
    VueCircuit& v = m_vue;
    const ConceptionElec* c = v.conception();
    if (!m_route || !c) return;
    const auto couches = couchesCuivre(c->circuit.empilage.couchesCuivre);
    // Sans couche donnée : celle choisie pour la prochaine via, sinon l'autre couche de la paire (ROU-05)
    const Couche suivante = vers && std::find(couches.begin(), couches.end(), *vers) != couches.end() ? *vers : v.coucheApresVia();
    if (suivante == v.m_coucheActive) return;
    m_vias.push_back(m_courant);
    v.m_coucheActive = suivante;
    emit v.message(QStringLiteral("Via posée : la piste continue sur %1.").arg(QString::fromUtf8(nomCouche(v.m_coucheActive))));
    emit v.coucheActiveChangee();
    m_cacheValide = false;
    v.update();
    v.majBandeau();
    v.majCartouche();
  }
  void retirerCoude() {
    if (!m_route || m_segments.empty()) return;
    const Segment s = m_segments.back();
    m_segments.pop_back();
    m_courant = s.a;
    // Une via posée au bout du segment retiré part avec lui ; la tête revient sur la couche du segment
    if (!m_vias.empty() && m_vias.back() == s.b) m_vias.pop_back();
    if (m_vue.m_coucheActive != s.couche) {
      m_vue.m_coucheActive = s.couche;
      emit m_vue.coucheActiveChangee();
    }
    m_vue.update();
  }
  // Suspendue par une commande (ERG-21) : la tête garde sa couche, que la commande a pu changer
  void suspendre() override { m_coucheSuspendue = m_vue.m_coucheActive; }
  void reprendre() override {
    if (m_route && m_vue.m_coucheActive != m_coucheSuspendue) {
      m_vue.m_coucheActive = m_coucheSuspendue;
      emit m_vue.coucheActiveChangee();
    }
    if (m_route) emit m_vue.message(QStringLiteral("La piste %1 reprend au dernier coude.").arg(q(m_reseau).toHtmlEscaped()));
    m_vue.update();
  }
  void setLargeur(Nm l) {
    m_largeur = l;
    m_cacheValide = false;
    m_vue.update();
    m_vue.majBandeau();
    m_vue.majCartouche();
  }
  void basculerAngle() {
    m_droitDabord = !m_droitDabord;
    m_cacheValide = false;
    m_vue.update();
    m_vue.majBandeau();
  }

 private:
  // Tracé vers la cible : direct ; en mode Contourner, autour de l'obstacle qu'il heurterait (5.7, ROU-01), sans Ctrl
  std::vector<std::pair<Pt, Pt>> trajet(Pt cible) const {
    auto direct = trajetDirect(cible);
    if (m_vue.m_modeRouteur != ModeRouteur::Contourner || !collision(direct)) return direct;
    const ConceptionElec* c = m_vue.conception();
    if (!c || !m_vue.m_calc) return direct;
    if (m_cacheValide && m_cacheCible == cible && m_cacheGeneration == m_vue.m_generationCalculs && m_cacheCouche == m_vue.m_coucheActive) return m_cacheTrajet;
    OptionsTrace o;
    o.reseau = m_reseau;
    o.couche = m_vue.m_coucheActive;
    o.largeur = m_largeur;
    o.pastilles = &m_vue.m_calc->pastilles;
    o.etendue = 8 * NM_PAR_MM;
    // Le tracé en cours, déjà fixé, compte comme obstacle pour lui-même : non (même réseau) ; le reste de la carte, oui
    const ResultatTrace r = contourner(*c, m_courant, cible, o);
    std::vector<std::pair<Pt, Pt>> t;
    if (r.ok)
      for (std::size_t i = 1; i < r.points.size(); ++i) t.push_back({r.points[i - 1], r.points[i]});
    m_cacheTrajet = r.ok ? t : direct;
    m_cacheCible = cible;
    m_cacheGeneration = m_vue.m_generationCalculs;
    m_cacheCouche = m_vue.m_coucheActive;
    m_cacheValide = true;
    return m_cacheTrajet;
  }
  std::vector<std::pair<Pt, Pt>> trajetDirect(Pt cible) const {
    const Nm dx = cible.x - m_courant.x, dy = cible.y - m_courant.y, ax = std::abs(dx), ay = std::abs(dy), d = std::min(ax, ay);
    const Nm sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sy = dy > 0 ? 1 : dy < 0 ? -1 : 0;
    Pt milieu = m_droitDabord ? (ax > ay ? Pt{m_courant.x + sx * (ax - d), m_courant.y} : Pt{m_courant.x, m_courant.y + sy * (ay - d)}) : Pt{m_courant.x + sx * d, m_courant.y + sy * d};
    std::vector<std::pair<Pt, Pt>> r;
    if (milieu != m_courant) r.push_back({m_courant, milieu});
    if (cible != milieu) r.push_back({milieu, cible});
    return r;
  }
  bool collision(const std::vector<std::pair<Pt, Pt>>& segments, std::string* avec = nullptr) const {
    const VueCircuit& v = m_vue;
    const ConceptionElec* c = v.conception();
    if (!c || !v.m_calc) return false;
    const Regles& R = c->circuit.regles;
    const double w = static_cast<double>(m_largeur) / 2;
    const Polygone carte = c->circuit.contour.polygone();
    for (const auto& [a, b] : segments) {
      if (!dedans(carte, a) || !dedans(carte, b)) {
        if (avec) *avec = "le bord de la carte";
        return true;
      }
      for (std::size_t i = 0; i < carte.size(); ++i)
        if (distanceSegments(carte[i], carte[(i + 1) % carte.size()], a, b) - w < static_cast<double>(R.bord)) {
          if (avec) *avec = "le bord de la carte";
          return true;
        }
      for (const auto& p : v.m_calc->pastilles) {
        if (!(p.couches & bit(v.m_coucheActive)) || (!p.reseau.empty() && p.reseau == m_reseau)) continue;
        if (distancePolygoneSegment(p.forme, a, b) - w < static_cast<double>(isolementEntre(R, m_reseau, p.reseau))) {
          if (avec) *avec = "la pastille " + p.reference + "." + p.numero;
          return true;
        }
      }
      for (const auto& t : c->circuit.pistes) {
        if (t.couche != v.m_coucheActive || (!t.reseau.empty() && t.reseau == m_reseau)) continue;
        if (distanceSegments(a, b, t.a, t.b) - w - static_cast<double>(t.largeur) / 2 < static_cast<double>(isolementEntre(R, m_reseau, t.reseau))) {
          if (avec) *avec = "une piste " + t.reseau;
          return true;
        }
      }
      for (const auto& via : c->circuit.vias) {
        if (!via.reseau.empty() && via.reseau == m_reseau) continue;
        if (distancePointSegment(via.p, a, b) - w - static_cast<double>(via.diametre) / 2 < static_cast<double>(isolementEntre(R, m_reseau, via.reseau))) {
          if (avec) *avec = "une via " + via.reseau;
          return true;
        }
      }
    }
    return false;
  }
  struct Segment {
    Pt a, b;
    Couche couche;
    Nm largeur;
  };
  // Départ ou arrivée au milieu d'une piste (EL-26) : la piste se coupe en deux au point de raccord
  struct Coupure {
    std::string piste;
    Pt p;
  };
  bool m_route = false, m_droitDabord = false;
  std::string m_reseau;
  Nm m_largeur = 250'000;
  Pt m_courant{};
  std::vector<Segment> m_segments;
  std::vector<Pt> m_vias;
  std::vector<Coupure> m_coupures;
  QPointF m_souris;
  Couche m_coucheSuspendue = Couche::CuivreDessus;
  // Contournement calculé pour la dernière cible (la souris bouge souvent sans changer de point de grille)
  mutable bool m_cacheValide = false;
  mutable Pt m_cacheCible{};
  mutable std::uint64_t m_cacheGeneration = 0;
  mutable Couche m_cacheCouche = Couche::CuivreDessus;
  mutable std::vector<std::pair<Pt, Pt>> m_cacheTrajet;
};

// ---------- Via ----------
class VueCircuit::OutilVia : public OutilBase {
 public:
  explicit OutilVia(VueCircuit& v) : OutilBase(v, Outil::Via) {}
  void appui(const GesteVue& g) override {
    VueCircuit& v = m_vue;
    const ConceptionElec* c = v.conception();
    if (!c || v.refuseVerrou()) return;
    const Pt w = v.monde(g.ecran);
    // Réseau de la pastille, de la piste, sinon de la zone remplie qui est dessous (via de couture, EL-12)
    std::string reseau;
    if (const int i = v.pastilleSous(w); i >= 0) reseau = v.m_calc->pastilles[static_cast<std::size_t>(i)].reseau;
    for (const auto& t : c->circuit.pistes)
      if (reseau.empty() && distancePointSegment(w, t.a, t.b) <= static_cast<double>(t.largeur) / 2) reseau = t.reseau;
    const Pt p = v.surGrilleRoutage(w);
    if (reseau.empty() && v.m_calc) reseau = reseauDeZoneSous(*c, v.m_calc->k, v.m_calc->zones, p);
    v.modifier([&](ConceptionElec& c2) { c2.circuit.vias.push_back({nouvelUuid(), reseau, p, viaDiametre(c2.circuit.regles, reseau), viaPercage(c2.circuit.regles, reseau)}); });
    emit v.message(reseau.empty() ? QStringLiteral("<span style='color:#A8620E'>Via posée sans réseau</span> : choisis-en un dans le panneau, sinon la DRC la signale.")
                                  : QStringLiteral("Via posée sur <b>%1</b>.").arg(q(reseau).toHtmlEscaped()));
  }
  ContenuBandeau bandeau() const override {
    ContenuBandeau c;
    c.titre = QStringLiteral("Via");
    c.detail = QStringLiteral("clic pour poser ; elle prend le réseau de la pastille, de la piste ou de la zone dessous");
    c.actions.push_back(sortie());
    return c;
  }
};

// ---------- Polygone : zone de cuivre, contour de la carte, découpe ----------
class VueCircuit::OutilPolygone : public OutilBase {
 public:
  OutilPolygone(VueCircuit& v, Outil genre) : OutilBase(v, genre) {}
  bool geste() const override { return !m_points.empty(); }
  void survol(const GesteVue& g) override {
    m_souris = g.ecran;
    if (!m_points.empty()) m_vue.update();
  }
  void glisser(const GesteVue& g) override { survol(g); }
  void appui(const GesteVue& g) override {
    if (m_vue.refuseVerrou()) return;
    const Pt w = m_vue.monde(g.ecran);
    m_points.push_back(m_genre == Outil::Zone ? m_vue.surGrilleRoutage(w) : surGrille(w, m_vue.pasPlacement()));
    m_vue.update();
    m_vue.majBandeau();
  }
  void doubleClic(const GesteVue&) override {
    if (m_points.size() >= 3) fermer();
  }
  bool agir(std::string_view a, bool faire) override {
    if (m_points.empty()) return false;
    if (a == "trace.retirerCoude") {
      if (faire) retirerSommet();
    } else if (a == "trace.terminer") {
      if (m_points.size() < 3) return false;  // un contour se ferme à partir de trois sommets
      if (faire) fermer();
    } else if (a == "trace.annuler") {
      if (faire) annulerTrace();
    } else
      return false;
    return true;
  }
  // Échap garde ce qui est fixé : un contour d'au moins trois sommets se ferme, un début plus court disparaît (C14)
  bool echap() override {
    if (m_points.empty()) return false;
    if (m_points.size() >= 3) fermer();
    else {
      m_points.clear();
      m_vue.update();
      m_vue.majBandeau();
    }
    return true;
  }
  bool annulerTrace() override {
    if (m_points.empty()) return false;
    m_points.clear();
    m_vue.update();
    m_vue.majBandeau();
    return true;
  }
  void dessiner(QPainter& g) const override {
    if (m_points.empty()) return;
    g.setPen(QPen(m_genre == Outil::Zone ? couleurCuivre(m_vue.m_coucheActive) : BORD, 1.5, Qt::DashLine));
    QPolygonF poly;
    for (const Pt& p : m_points) poly << m_vue.ecran(p);
    poly << m_souris;
    g.drawPolyline(poly);
  }
  ContenuBandeau bandeau() const override {
    ContenuBandeau c;
    c.titre = m_genre == Outil::Zone ? QStringLiteral("Zone de cuivre") : m_genre == Outil::Contour ? QStringLiteral("Contour de la carte") : QStringLiteral("Découpe");
    c.detail = m_points.empty() ? QStringLiteral("clic pour chaque sommet") : QStringLiteral("%1 sommet(s) · double-clic ferme le contour").arg(m_points.size());
    OutilPolygone* moi = const_cast<OutilPolygone*>(this);
    if (m_points.size() >= 3) c.actions.push_back(ActionBandeau::du("trace.terminer", QStringLiteral("fermer"), QStringLiteral("Fermer"), [moi] { moi->fermer(); }));
    if (!m_points.empty()) {
      c.actions.push_back(ActionBandeau::du("trace.retirerCoude", QStringLiteral("retirer-sommet"), QStringLiteral("Retirer le sommet"), [moi] { moi->retirerSommet(); }));
      c.actions.push_back(ActionBandeau::du("trace.annuler", QStringLiteral("retirer-trace"), QStringLiteral("Tout retirer"), [moi] { moi->annulerTrace(); }));
    }
    c.actions.push_back(sortie(m_points.empty() ? QStringLiteral("Terminer") : QStringLiteral("Annuler")));
    return c;
  }
  void retirerSommet() {
    if (m_points.empty()) return;
    m_points.pop_back();
    m_vue.update();
    m_vue.majBandeau();
  }
  void fermer() {
    VueCircuit& v = m_vue;
    if (m_points.size() < 3) return;
    const ConceptionElec* c = v.conception();
    if (!c) return;
    const Polygone contour = m_points;
    if (aireMm2(contour) < 0.01) {
      emit v.message(QStringLiteral("Contour trop petit ou aplati : ajoute des sommets."));
      return;
    }
    if (m_genre == Outil::Zone) {
      std::string reseau = v.m_reseauZone;
      if (reseau.empty()) {
        // Réseau demandé à la fermeture (EL-20) : la pastille sous le premier sommet, une masse, sinon le réseau le
        // plus présent dans la zone sont proposés d'abord
        const std::string propose = v.reseauPropose(contour);
        QStringList noms;
        if (v.m_calc)
          for (const auto& r : v.m_calc->k.reseaux)
            if (!r.broches.empty() && !r.nom.empty() && !noms.contains(q(r.nom))) noms << q(r.nom);
        noms.sort(Qt::CaseInsensitive);
        const QString aucun = QStringLiteral("— aucun réseau (cuivre isolé) —");
        noms << aucun;
        const int defaut = std::max(0, static_cast<int>(noms.indexOf(q(propose))));
        bool ok = false;
        const QString choix = QInputDialog::getItem(&v, QStringLiteral("Réseau de la zone"), QStringLiteral("Réseau relié par cette zone de cuivre (%1) :").arg(QString::fromUtf8(nomCouche(v.m_coucheActive))),
                                                    noms, defaut, false, &ok);
        if (!ok) {
          emit v.message(QStringLiteral("Zone non posée : double-clique de nouveau pour choisir son réseau, ou Échap pour l’abandonner."));
          return;
        }
        reseau = choix == aucun ? std::string() : choix.toStdString();
      }
      const Couche couche = v.m_coucheActive;
      const std::string id = nouvelUuid();
      m_points.clear();
      v.modifier([&](ConceptionElec& c2) {
        Zone z;
        z.uuid = id;
        z.reseau = reseau;
        z.couche = couche;
        z.contour = contour;
        c2.circuit.zones.push_back(z);
      });
      v.setOutil(Outil::Selection);
      v.choisir(Genre::Zone, id);
      return;
    }
    m_points.clear();
    if (m_genre == Outil::Contour) {
      v.modifier([&](ConceptionElec& c2) { c2.circuit.contour.libre = orienter(contour); });
      emit v.message(QStringLiteral("<b>Contour de la carte</b> redessiné (%1 sommets). Le panneau de la carte le ramène au rectangle si besoin.").arg(contour.size()));
    } else {
      bool dedansCarte = true;
      for (const Pt& p : contour) dedansCarte &= dedans(c->circuit.contour.polygone(), p);
      if (!dedansCarte) {
        emit v.message(QStringLiteral("<span style='color:#C2402E'>Découpe refusée</span> : elle doit rester à l’intérieur du contour (pour une encoche, redessine le contour)."));
        return;
      }
      v.modifier([&](ConceptionElec& c2) { c2.circuit.contour.decoupes.push_back(orienter(contour)); });
      emit v.message(QStringLiteral("<b>Découpe</b> ajoutée : %1 mm².").arg(QLocale().toString(aireMm2(contour), 'f', 1)));
    }
    v.setOutil(Outil::Selection);
  }

 private:
  std::vector<Pt> m_points;
  QPointF m_souris;
};

// ---------- Mesure ----------
class VueCircuit::OutilMesure : public OutilBase {
 public:
  explicit OutilMesure(VueCircuit& v) : OutilBase(v, Outil::Mesure) {}
  void survol(const GesteVue& g) override {
    m_souris = g.ecran;
    if (m_points.size() == 1) m_vue.update();
  }
  void appui(const GesteVue& g) override {
    if (m_points.size() >= 2) m_points.clear();
    m_points.push_back(m_vue.monde(g.ecran));
    if (m_points.size() == 2) {
      const double dx = static_cast<double>(m_points[1].x - m_points[0].x), dy = static_cast<double>(m_points[1].y - m_points[0].y);
      m_resultat = QStringLiteral("%1 (Δx %2, Δy %3)").arg(m_vue.longueur(std::hypot(dx, dy)), m_vue.longueur(dx), m_vue.longueur(dy));
      emit m_vue.message(QStringLiteral("<b>Mesure</b> : ") + m_resultat);
    }
    m_vue.update();
    m_vue.majBandeau();
  }
  void dessiner(QPainter& g) const override {
    if (m_points.empty()) return;
    g.setPen(QPen(CHOIX, 1.5, Qt::DashLine));
    QPolygonF poly;
    for (const Pt& p : m_points) poly << m_vue.ecran(p);
    if (m_points.size() == 1) poly << m_souris;
    g.drawPolyline(poly);
  }
  ContenuBandeau bandeau() const override {
    ContenuBandeau c;
    c.titre = QStringLiteral("Mesure");
    c.detail = m_points.size() == 2 ? m_resultat : QStringLiteral("clic sur deux points");
    c.actions.push_back(sortie());
    return c;
  }

 private:
  std::vector<Pt> m_points;
  QPointF m_souris;
  QString m_resultat;
};

// ---------- Texte de carte ----------
class VueCircuit::OutilTexte : public OutilBase {
 public:
  explicit OutilTexte(VueCircuit& v) : OutilBase(v, Outil::Texte) {}
  void appui(const GesteVue& g) override {
    VueCircuit& v = m_vue;
    const Pt w = v.monde(g.ecran);
    bool ok = false;
    const QString t = QInputDialog::getText(&v, QStringLiteral("Texte sur la carte"),
                                            QStringLiteral("Texte de sérigraphie (%1) :").arg(v.m_coucheActive == Couche::CuivreDessous ? QStringLiteral("dessous") : QStringLiteral("dessus")),
                                            QLineEdit::Normal, QString(), &ok);
    if (ok && !t.trimmed().isEmpty()) {
      v.poserTexte(w, t.trimmed().toStdString());
      v.setOutil(Outil::Selection);
    }
  }
  bool unCoup() const override { return true; }
  ContenuBandeau bandeau() const override {
    ContenuBandeau c;
    c.titre = QStringLiteral("Texte");
    c.detail = QStringLiteral("clic où poser le texte de sérigraphie, sur la face de la couche active");
    c.actions.push_back(sortie(QStringLiteral("Annuler")));
    return c;
  }
};

// ---------- Fabrique et commandes de la vue qui touchent aux outils ----------
// ---------- Routage : paire de couches, prochaine via, commandes imbriquées (1.17) ----------
std::vector<std::pair<Couche, Couche>> VueCircuit::pairesPossibles(int couchesCuivre) {
  const std::vector<Couche> c = prom::ecad::couchesCuivre(couchesCuivre);
  std::vector<std::pair<Couche, Couche>> r;
  if (c.size() < 2) return r;
  r.push_back({c.front(), c.back()});  // la paire extérieure d'abord
  for (std::size_t i = 0; i + 1 < c.size(); ++i)
    if (std::pair{c[i], c[i + 1]} != r.front()) r.push_back({c[i], c[i + 1]});
  return r;
}

void VueCircuit::setPaireCouches(Couche a, Couche b) {
  m_paire = {a, b};
  // Hors tracé, la couche active passe sur la paire choisie ; pendant une piste, la prochaine via l'y mène
  const bool trace = outil() == Outil::Router && m_outils.actif()->geste();
  if (!trace && m_coucheActive != a && m_coucheActive != b) {
    setCoucheActive(a);
    emit coucheActiveChangee();
  }
  majBandeau();
  emit message(QStringLiteral("Paire de couches : %1 et %2.").arg(QString::fromUtf8(nomCouche(a)), QString::fromUtf8(nomCouche(b))));
}

void VueCircuit::setArriveeVia(std::optional<Couche> c) {
  m_arriveeVia = c;
  majBandeau();
}

Couche VueCircuit::coucheApresVia() const {
  if (m_arriveeVia && *m_arriveeVia != m_coucheActive) return *m_arriveeVia;
  if (m_coucheActive == m_paire.first) return m_paire.second;
  if (m_coucheActive == m_paire.second) return m_paire.first;
  return m_paire.first;
}

void VueCircuit::reprendreTrace() {
  if (!m_outils.depiler()) return;
  appliquerCurseur();
  majBandeau();
  emit outilChange();
  update();
}

void VueCircuit::menuRoutage(QPointF q) {
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("menuRoutage"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  // La piste posée sous le curseur, qui gêne : elle se supprime en un pas, et le tracé en cours reprend au dernier
  // coude, sur la même paire de couches (ERG-21)
  std::vector<std::string> reseaux;
  const std::vector<ObjetChoisi> sous = objetsSous(q, false, &reseaux);
  for (std::size_t i = 0; i < sous.size(); ++i)
    if (sous[i].genre == Genre::Piste) {
      const std::string id = sous[i].id;
      const QString reseau = reseaux[i].empty() ? QStringLiteral("sans réseau") : QString::fromStdString(reseaux[i]);
      QAction* x = menu->addAction(QStringLiteral("Supprimer la piste %1 sous le curseur").arg(reseau));
      x->setObjectName(QStringLiteral("supprimerPisteSous"));
      connect(x, &QAction::triggered, this, [this, id] {
        modifier([&](ConceptionElec& c) {
          auto& v = c.circuit.pistes;
          v.erase(std::remove_if(v.begin(), v.end(), [&](const Piste& t) { return t.uuid == id; }), v.end());
        });
        emit message(QStringLiteral("Piste supprimée : le tracé reprend au dernier coude."));
        majBandeau();
        update();
      });
      menu->addSeparator();
      break;
    }
  const RegistreActions& registre = RegistreActions::atelierElectronique();
  const std::pair<const char*, const char*> lignes[] = {{"circuit.via", "Via au dernier coude"},
                                                        {"trace.retirerCoude", "Retirer le dernier coude"},
                                                        {"trace.annuler", "Retirer le tracé"},
                                                        {"trace.terminer", "Terminer la piste"}};
  for (const auto& [id, libelle] : lignes) {
    const QString touche = registre.touches(id).section(QStringLiteral(", "), 0, 0);
    QAction* x = menu->addAction(touche.isEmpty() ? QString::fromUtf8(libelle) : QString::fromUtf8(libelle) + QLatin1Char('\t') + touche);
    marquerAction(x, id);
    connect(x, &QAction::triggered, this, [this, id = std::string(id)] {
      executerAction(id);
      majBandeau();
      update();
    });
  }
  menu->popup(mapToGlobal(q.toPoint()));
}

std::unique_ptr<OutilVue> VueCircuit::creerOutil(Outil o) {
  switch (o) {
    case Outil::Selection: return std::make_unique<OutilSelection>(*this);
    case Outil::Router: return std::make_unique<OutilRoutage>(*this);
    case Outil::Via: return std::make_unique<OutilVia>(*this);
    case Outil::Zone:
    case Outil::Contour:
    case Outil::Decoupe: return std::make_unique<OutilPolygone>(*this, o);
    case Outil::Mesure: return std::make_unique<OutilMesure>(*this);
    case Outil::Texte: return std::make_unique<OutilTexte>(*this);
  }
  return std::make_unique<OutilSelection>(*this);
}

bool VueCircuit::router(const std::vector<Pt>& points, bool forcer) {
  if (points.size() < 2) return false;
  setOutil(Outil::Router);
  auto* r = dynamic_cast<OutilRoutage*>(m_outils.actif());
  if (!r || !r->commencer(points.front())) return false;
  bool ok = true;
  for (std::size_t i = 1; i < points.size() && ok; ++i) ok = r->avancer(points[i], forcer);
  r->finir();
  return ok;
}

// ---------- Routeur interactif (lot L5) ----------
void VueCircuit::setModeRouteur(ModeRouteur m) {
  m_modeRouteur = m;
  update();
  majBandeau();
  majCartouche();
}

QString VueCircuit::nomModeRouteur(ModeRouteur m) {
  switch (m) {
    case ModeRouteur::Contourner: return QStringLiteral("Contourner");
    case ModeRouteur::Pousser: return QStringLiteral("Pousser");
    case ModeRouteur::Ignorer: return QStringLiteral("Ignorer");
  }
  return {};
}

Nm VueCircuit::largeurTrace() const {
  const auto* r = dynamic_cast<const OutilRoutage*>(m_outils.actif());
  return r ? r->largeur() : 0;
}

void VueCircuit::setLargeurTrace(Nm l) {
  if (auto* r = dynamic_cast<OutilRoutage*>(m_outils.actif()); r && r->enCours()) r->setLargeur(l);
}

std::vector<VueCircuit::AureoleRoutage> VueCircuit::aureolesRoutage() const {
  std::vector<AureoleRoutage> res;
  const auto* r = dynamic_cast<const OutilRoutage*>(m_outils.actif());
  const ConceptionElec* c = conception();
  if (!r || !r->enCours() || !c || !m_calc) return res;
  const Regles& R = c->circuit.regles;
  const Nm w2 = r->largeur() / 2;
  // Les objets à moins de trois millimètres (et d'un isolement) de la tête ou de la souris
  const Pt tete = r->tete(), souris = monde(r->souris());
  auto proche = [&](const Boite& b) {
    const Nm d = 3 * NM_PAR_MM + R.isolement + w2;
    return b.gonflee(d).contient(tete) || b.gonflee(d).contient(souris);
  };
  for (const auto& p : m_calc->pastilles) {
    if (!(p.couches & bit(m_coucheActive)) || (!p.reseau.empty() && p.reseau == r->reseau())) continue;
    if (!proche(boite(p.forme))) continue;
    const Nm iso = isolementEntre(R, r->reseau(), p.reseau);
    const Polygones g = gonfler({p.forme}, iso);
    if (g.empty()) continue;
    res.push_back({p.reference + "." + p.numero, p.forme, g.front(), iso});
  }
  for (const auto& t : c->circuit.pistes) {
    if (t.couche != m_coucheActive || (!t.reseau.empty() && t.reseau == r->reseau())) continue;
    Boite b;
    b.etendre(t.a);
    b.etendre(t.b);
    if (!proche(b.gonflee(t.largeur / 2))) continue;
    const Nm iso = isolementEntre(R, r->reseau(), t.reseau);
    res.push_back({t.uuid, oblong(t.a, t.b, t.largeur), oblong(t.a, t.b, t.largeur + 2 * iso), iso});
  }
  for (const auto& v : c->circuit.vias) {
    if (!v.reseau.empty() && v.reseau == r->reseau()) continue;
    Boite b;
    b.etendre(v.p);
    if (!proche(b.gonflee(v.diametre / 2))) continue;
    const Nm iso = isolementEntre(R, r->reseau(), v.reseau);
    res.push_back({v.uuid, cercle(v.p, v.diametre / 2), cercle(v.p, v.diametre / 2 + iso), iso});
  }
  return res;
}

void VueCircuit::majCartouche() {
  if (!m_cartouche) return;
  const auto* r = dynamic_cast<const OutilRoutage*>(m_outils.actif());
  const ConceptionElec* c = conception();
  if (!r || !r->enCours() || !c) {
    m_cartouche->hide();
    return;
  }
  m_cartouche->maj(QString::fromStdString(r->reseau()), m_coucheActive, r->largeur(), largeurPiste(c->circuit.regles, r->reseau()), viaDiametre(c->circuit.regles, r->reseau()), m_modeRouteur);
  // Près de la tête du tracé, en bas à droite, gardé dans la vue
  QPoint ou = ecran(r->tete()).toPoint() + QPoint(28, 28);
  ou.setX(std::clamp(ou.x(), 0, std::max(0, width() - m_cartouche->width())));
  ou.setY(std::clamp(ou.y(), 0, std::max(0, height() - m_cartouche->height())));
  m_cartouche->move(ou);
  m_cartouche->show();
  m_cartouche->raise();
}

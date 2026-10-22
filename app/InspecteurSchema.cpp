// Prométhée : pages de l'inspecteur pour le schéma (lot L1, tâche 1.9) : composant, alimentation, étiquette, texte,
// fil, feuille. Chaque page crée ses widgets une fois ; « relire » les remplit depuis le modèle, sans toucher au
// champ qui a le focus. Voir InspecteurElectronique.hpp.
// Licence GPL-3.0-only.
#include <algorithm>
#include <memory>
#include <optional>
#include <set>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QHeaderView>
#include <QTableWidget>
#include <QLineEdit>
#include <QWheelEvent>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "Icones.hpp"
#include "InspecteurElectronique.hpp"
#include "VueSchema.hpp"
#include "ChampNombre.hpp"
#include "SelectionElectronique.hpp"
#include "VueCircuit.hpp"
#include "PanneauEcarts.hpp"
#include "ChampValeur.hpp"
#include "promethee/circuit.hpp"
#include "promethee/electronique.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/remplacement.hpp"
#include "promethee/unites.hpp"
#include "promethee/synchronisation.hpp"
#include "promethee/valeurs.hpp"
#include "promethee/formules.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }

// Alt+molette sur le champ Valeur (3.2) : la valeur voisine dans sa série E (celle de sa tolérance, ou celle de sa
// famille), d'une décade à l'autre ; compléments gardés
class MoletteSerieE : public QObject {
 public:
  MoletteSerieE(QObject* parent, std::function<void(int pas)> f) : QObject(parent), m_f(std::move(f)) {}

 protected:
  bool eventFilter(QObject* o, QEvent* e) override {
    if (e->type() == QEvent::Wheel) {
      auto* w = static_cast<QWheelEvent*>(e);
      if (w->modifiers() & Qt::AltModifier) {
        const int d = w->angleDelta().y() != 0 ? w->angleDelta().y() : w->angleDelta().x();
        if (d != 0) m_f(d > 0 ? 1 : -1);
        return true;
      }
    }
    return QObject::eventFilter(o, e);
  }

 private:
  std::function<void(int)> m_f;
};

// Valeur décalée de « pas » crans dans sa série E, écrite dans le style tapé (compact ou SI), compléments gardés
std::optional<std::string> valeurVoisine(const std::string& tape, FamilleValeur f, int pas) {
  ValeurComprise v = lireValeurComposant(tape, f);
  if (!v.comprise() || v.nombre <= 0) return std::nullopt;
  const int serie = v.serieReference ? v.serieReference : 24;
  v.nombre = voisineDansSerie(v.nombre, serie, pas);
  const bool si = tape.find(' ') != std::string::npos || tape.find("Ω") != std::string::npos || tape.find(uniteDeFamille(f)) != std::string::npos;
  return ecrireValeurComposant(v, si ? AffichageValeur::SystemeInternational : AffichageValeur::Compact, tape);
}

// Une page du schéma : la feuille montrée et l'objet choisi
class PageSchema : public PageInspecteur {
 public:
  using PageInspecteur::PageInspecteur;
  void montrer() override {
    const VueSchema* vs = m_atelier.vueSchema();
    m_feuille = vs->feuille();
    m_objet = vs->selection().size() == 1 ? *vs->selection().begin() : std::string();
    PageInspecteur::montrer();
  }

 protected:
  const ConceptionElec* conception() const { return m_atelier.conceptionCourante(); }
  const Feuille* feuille() const {
    const ConceptionElec* c = conception();
    return c && m_feuille >= 0 && m_feuille < static_cast<int>(c->feuilles.size()) ? &c->feuilles[static_cast<std::size_t>(m_feuille)] : nullptr;
  }
  // Modifie la feuille montrée, en un pas ; « saisie » non nul : les écritures d'un même passage dans un champ font un pas
  void modifierFeuille(const std::function<void(Feuille&)>& f, std::uint64_t saisie = 0) {
    const int fi = m_feuille;
    m_atelier.modifierConception([&](ConceptionElec& c) {
      if (fi >= 0 && fi < static_cast<int>(c.feuilles.size())) f(c.feuilles[static_cast<std::size_t>(fi)]);
    }, {}, saisie ? QStringLiteral("saisie:%1").arg(saisie) : QString());
  }
  QGroupBox* groupe(const QString& titre, QFormLayout*& fl) {
    auto* g = new QGroupBox(titre);
    fl = new QFormLayout(g);
    fl->setRowWrapPolicy(QFormLayout::WrapLongRows);
    colonne()->addWidget(g);
    return g;
  }
  int m_feuille = 0;
  std::string m_objet;
};

// ---------- Composant ----------
// La même page au schéma et au circuit (3.10, P12) : au circuit, une section Placement s'ajoute en tête ; valeur,
// empreinte et hauteur s'y changent comme au schéma. L'en-tête dit où en est le composant sur le circuit, et ses
// problèmes (ERC, DRC).
class PageComposant : public PageSchema {
 public:
  PageComposant(AtelierElectronique& a, bool circuit) : PageSchema(a), m_circuit(circuit) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] {
      const SymbolePlace* s = symbole();
      const DefSymbole* d = definition();
      const ComposantElec* x = composant();
      if (!d || !x) return;
      titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>%1</span> <span style='color:%5'>%2</span><br><span style='color:%5'>%3 · %4 broche(s)</span>")
                         .arg(q(x->reference + (s && d->unites > 1 ? lettreUnite(s->unite) : std::string())).toHtmlEscaped(), q(x->valeur).toHtmlEscaped(), q(d->nom).toHtmlEscaped())
                         .arg(d->broches.size())
                         .arg(teintes::secondaire()));
    });
    construireEtat();
    if (m_circuit) {
      construirePlacement();
      construirePastilles();
    }
    construireUnites();
    construireComposant();
    construireParametres();
    construireApprovisionnement();
    construireExclusions();
    construireBroches();
    colonne()->addStretch(1);
  }
  void montrer() override {
    if (m_circuit) {
      // Au circuit : le composant de l'empreinte choisie ; sa première pose au schéma pour les unités
      m_composant = m_atelier.vueCircuit()->choisi();
      m_objet.clear();
      m_feuille = 0;
      if (const ConceptionElec* c = conception())
        for (std::size_t i = 0; i < c->feuilles.size() && m_objet.empty(); ++i)
          for (const auto& s : c->feuilles[i].symboles)
            if (s.composant == m_composant) {
              m_objet = s.uuid;
              m_feuille = static_cast<int>(i);
              break;
            }
      PageInspecteur::montrer();
      return;
    }
    const VueSchema* vs = m_atelier.vueSchema();
    m_feuille = vs->feuille();
    m_objet = vs->selection().size() == 1 ? *vs->selection().begin() : std::string();
    const SymbolePlace* s = symbole();
    m_composant = s ? s->composant : std::string();
    PageInspecteur::montrer();
  }
  void resultats() override { relire(); }

 private:
  const SymbolePlace* symbole() const {
    if (const Feuille* f = feuille())
      for (const auto& s : f->symboles)
        if (s.uuid == m_objet) return &s;
    return nullptr;
  }
  const ComposantElec* composant() const { return conception() ? conception()->composant(m_composant) : nullptr; }
  const DefSymbole* definition() const {
    const ComposantElec* x = composant();
    return x && conception() ? conception()->symbole(x->symbole) : nullptr;
  }
  const EmpreintePlacee* empreintePlacee() const {
    if (const ConceptionElec* c = conception())
      for (const auto& e : c->circuit.empreintes)
        if (e.composant == m_composant) return &e;
    return nullptr;
  }
  std::string idComposant() const { return m_composant; }

  // État sur le circuit (À jour, À placer, Écart en attente, Hors carte, Ne pas monter) et problèmes du composant
  void construireEtat() {
    auto* etat = inspecteur::texte();
    etat->setObjectName(QStringLiteral("etatComposant"));
    connect(etat, &QLabel::linkActivated, this, [this](const QString& lien) {
      if (lien == QLatin1String("ecarts")) m_atelier.montrerEcarts();
    });
    colonne()->addWidget(etat);
    suivre([this, etat] {
      const ConceptionElec* c = conception();
      const ComposantElec* x = composant();
      if (!c || !x) return;
      const EmpreintePlacee* e = empreintePlacee();
      bool ecart = false;
      if (m_atelier.ecarts())
        for (const auto& ec : m_atelier.ecarts()->ecarts()) ecart |= ec.composant == x->uuid;
      QString texte, couleur = teintes::secondaire();
      if (x->nePasMonter) texte = QStringLiteral("<s>Ne pas monter</s>");
      else if (x->horsCarte) texte = QStringLiteral("Hors carte");
      else if (ecart) {
        texte = QStringLiteral("Écart en attente · <a href=\"ecarts\" style='color:%1'>Voir</a>").arg(teintes::lien());
        couleur = teintes::alerte();
      } else if (!e || aPlacer(*c, *e)) texte = QStringLiteral("À placer");
      else {
        texte = QStringLiteral("À jour");
        couleur = teintes::succes();
      }
      etat->setText(QStringLiteral("<span style='color:%1'>● %2</span>").arg(couleur, texte));
    });
    // Problèmes du composant, cinq au plus, au schéma l'ERC, au circuit la DRC
    auto* problemes = new SectionRefaite([this](QVBoxLayout* col) {
      std::vector<Violation> v;
      for (const auto& x : m_circuit ? m_atelier.calculs().drc : m_atelier.ercMontree())
        if (std::find(x.objets.begin(), x.objets.end(), m_composant) != x.objets.end()) v.push_back(x);
      if (v.empty()) return;
      const std::size_t n = v.size();
      if (v.size() > 5) v.resize(5);
      col->addWidget(inspecteur::texte(QStringLiteral("<b>Problèmes</b> (%1)").arg(n)));
      inspecteur::listeProblemes(col, v, !m_circuit, m_atelier);
      if (n > 5) {
        auto* tout = inspecteur::texte(QStringLiteral("<a href=\"tout\" style='color:%1'>tout voir</a>").arg(teintes::lien()));
        connect(tout, &QLabel::linkActivated, this, [this] { m_atelier.montrerVerifications(); });
        col->addWidget(tout);
      }
    });
    problemes->setObjectName(QStringLiteral("problemesComposant"));
    colonne()->addWidget(problemes);
    suivre([this, problemes] {
      std::vector<Violation> v;
      for (const auto& x : m_circuit ? m_atelier.calculs().drc : m_atelier.ercMontree())
        if (std::find(x.objets.begin(), x.objets.end(), m_composant) != x.objets.end()) v.push_back(x);
      problemes->refaire(m_composant + "#" + inspecteur::signature(v));
    });
  }

  // Pastilles de l'empreinte (au circuit) : propriété de fabrication pastille par pastille, écrite dans les Gerber X2
  // (3.20, EMP-18) ; volumes libres extrudés entre deux hauteurs (EMP-11). La définition de l'empreinte du projet
  // change : toutes ses poses suivent.
  void construirePastilles() {
    auto* g = new QGroupBox(QStringLiteral("Pastilles et volumes"));
    auto* lg = new QVBoxLayout(g);
    auto* section = new SectionRefaite([this](QVBoxLayout* col) {
      const ConceptionElec* c = conception();
      const EmpreintePlacee* e = empreintePlacee();
      const DefEmpreinte* d = c && e ? c->empreinte(e->empreinte) : nullptr;
      if (!d) return;
      const std::string idEmp = d->id;
      auto modifierDef = [this, idEmp](const std::function<void(DefEmpreinte&)>& f, const QString& nom) {
        m_atelier.modifierConception([&](ConceptionElec& cc) {
          const DefEmpreinte* d0 = cc.empreinte(idEmp);
          if (!d0) return;
          DefEmpreinte copie = *d0;
          f(copie);
          cc.empreintes[idEmp] = copie;
        }, nom);
      };
      auto* table = new QTableWidget(static_cast<int>(d->pastilles.size()), 3);
      table->setObjectName(QStringLiteral("pastillesEmpreinte"));
      table->setHorizontalHeaderLabels({QStringLiteral("N°"), QStringLiteral("Type"), QStringLiteral("Fabrication")});
      table->verticalHeader()->hide();
      table->horizontalHeader()->setStretchLastSection(true);
      table->setMaximumHeight(160);
      static const char* libelles[] = {"ordinaire", "BGA", "fiducial local", "fiducial global", "point de test", "dissipateur", "castellée", "insertion en force"};
      for (int i = 0; i < static_cast<int>(d->pastilles.size()); ++i) {
        const Pastille& p = d->pastilles[static_cast<std::size_t>(i)];
        table->setItem(i, 0, new QTableWidgetItem(q(p.numero.empty() ? std::string("—") : p.numero)));
        table->setItem(i, 1, new QTableWidgetItem(p.type == TypePastille::CMS ? QStringLiteral("CMS") : p.type == TypePastille::Traversante ? QStringLiteral("traversante") : QStringLiteral("non métallisée")));
        auto* fab = new QComboBox;
        const auto& codes = proprietesFabricationPastille();
        for (std::size_t k = 0; k < codes.size(); ++k) fab->addItem(QString::fromUtf8(libelles[std::min<std::size_t>(k, 7)]), q(codes[k]));
        fab->setCurrentIndex(std::max(0, fab->findData(q(p.fabrication))));
        connect(fab, &QComboBox::activated, this, [fab, i, modifierDef](int) {
          const std::string v = fab->currentData().toString().toStdString();
          modifierDef([&](DefEmpreinte& x) {
            if (i < static_cast<int>(x.pastilles.size())) x.pastilles[static_cast<std::size_t>(i)].fabrication = v;
          }, QStringLiteral("Propriété de fabrication de la pastille"));
        });
        table->setCellWidget(i, 2, fab);
      }
      col->addWidget(table);
      // Volumes libres
      for (std::size_t k = 0; k < d->volumes.size(); ++k) {
        const VolumeLibre& v = d->volumes[k];
        const Boite b = boite(v.contour);
        auto* ligne = new QWidget;
        auto* hl = new QHBoxLayout(ligne);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(inspecteur::texte(QStringLiteral("Volume %1 × %2 mm, de %3 à %4 mm").arg(mm(b.largeur())).arg(mm(b.hauteur())).arg(mm(v.z0)).arg(mm(v.z1))), 1);
        auto* retirer = new QToolButton;
        retirer->setText(QStringLiteral("Retirer"));
        connect(retirer, &QToolButton::clicked, this, [k, modifierDef] {
          modifierDef([k](DefEmpreinte& x) {
            if (k < x.volumes.size()) x.volumes.erase(x.volumes.begin() + static_cast<long>(k));
          }, QStringLiteral("Volume libre retiré"));
        });
        hl->addWidget(retirer);
        col->addWidget(ligne);
      }
      // Deux lignes de deux champs, pour tenir dans un inspecteur étroit (écran de 1 024 px)
      auto* ajout = new QWidget;
      auto* fa = new QGridLayout(ajout);
      fa->setContentsMargins(0, 0, 0, 0);
      auto* l = new ChampNombre;
      auto* p = new ChampNombre;
      auto* z0 = new ChampNombre;
      auto* z1 = new ChampNombre;
      for (auto* ch : {l, p, z0, z1}) {
        ch->setRange(0, 200);
        ch->setDecimals(2);
        ch->setSuffix(QStringLiteral(" mm"));
      }
      l->setValue(mm(d->encombrement.largeur()));
      p->setValue(mm(d->encombrement.hauteur()));
      z1->setValue(std::max(1.0, hauteurGenerique(*d)));
      l->setToolTip(QStringLiteral("Largeur"));
      p->setToolTip(QStringLiteral("Profondeur"));
      z0->setToolTip(QStringLiteral("Bas, au-dessus de la carte"));
      z1->setToolTip(QStringLiteral("Haut, au-dessus de la carte"));
      auto* ajouter = new QPushButton(QStringLiteral("＋ Volume"));
      ajouter->setObjectName(QStringLiteral("ajouterVolume"));
      connect(ajouter, &QPushButton::clicked, this, [l, p, z0, z1, modifierDef] {
        if (z1->value() <= z0->value() || l->value() <= 0 || p->value() <= 0) return;
        VolumeLibre v;
        const Nm hl = nm(l->value() / 2), hp = nm(p->value() / 2);
        v.contour = {{-hl, -hp}, {hl, -hp}, {hl, hp}, {-hl, hp}};
        v.z0 = nm(z0->value());
        v.z1 = nm(z1->value());
        modifierDef([&](DefEmpreinte& x) {
          // Le volume est posé au centre du corps de l'empreinte
          for (auto& pt : v.contour) pt = pt + x.corps.decalage;
          x.volumes.push_back(v);
        }, QStringLiteral("Volume libre ajouté"));
      });
      fa->addWidget(new QLabel(QStringLiteral("L × P")), 0, 0);
      fa->addWidget(l, 0, 1);
      fa->addWidget(p, 0, 2);
      fa->addWidget(new QLabel(QStringLiteral("De → à")), 1, 0);
      fa->addWidget(z0, 1, 1);
      fa->addWidget(z1, 1, 2);
      fa->addWidget(ajouter, 2, 1, 1, 2);
      for (ChampNombre* ch : {l, p, z0, z1}) ch->setMinimumWidth(60);
      col->addWidget(ajout);
    });
    lg->addWidget(section);
    colonne()->addWidget(g);
    suivre([this, section] {
      const ConceptionElec* c = conception();
      const EmpreintePlacee* e = empreintePlacee();
      const DefEmpreinte* d = c && e ? c->empreinte(e->empreinte) : nullptr;
      std::string sig = d ? d->id : std::string();
      if (d) {
        for (const auto& p : d->pastilles) sig += "|" + p.fabrication;
        sig += "#" + std::to_string(d->volumes.size());
      }
      section->refaire(sig);
    });
  }

  // Placement (au circuit) : X et Y depuis l'origine de la carte, unités et expressions acceptées, rotation, face,
  // verrou (3.10)
  void construirePlacement() {
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Placement"), fl);
    auto unites = [this] {
      prom::Unites u;
      if (const ConceptionElec* c = conception()) u.longueur = prom::uniteLongueurDepuis(c->circuit.affichage.unite);
      return u;
    };
    auto champ = [this, fl, unites](const QString& libelle, bool angle, std::function<double(const EmpreintePlacee&)> lire, std::function<void(EmpreintePlacee&, double)> ecrire) {
      auto* ch = new ChampValeur(angle, unites());
      ch->setObjectName(QStringLiteral("placement:") + libelle);
      ch->setBornes(angle ? -360 : -2000, angle ? 360 : 2000);
      connect(ch, &ChampValeur::valeurChangee, this, [this, ecrire](double v) {
        const std::string id = m_composant;
        m_atelier.modifierConception([&](ConceptionElec& c) {
          for (auto& e : c.circuit.empreintes)
            if (e.composant == id) ecrire(e, v);
        });
      });
      lier(ch, [this, ch, lire, unites] {
        const ConceptionElec* c = conception();
        const EmpreintePlacee* e = empreintePlacee();
        if (!c || !e) return;
        if (ch->property("unite").toString().toStdString() != c->circuit.affichage.unite) {
          ch->setUnites(unites());
          ch->setProperty("unite", q(c->circuit.affichage.unite));
        }
        ch->setValeur(lire(*e));
      });
      fl->addRow(libelle, ch);
    };
    champ(QStringLiteral("X"), false, [](const EmpreintePlacee& e) { return mm(e.position.x); }, [](EmpreintePlacee& e, double v) { e.position.x = nm(v); });
    champ(QStringLiteral("Y"), false, [](const EmpreintePlacee& e) { return mm(e.position.y); }, [](EmpreintePlacee& e, double v) { e.position.y = nm(v); });
    champ(QStringLiteral("Rotation"), true, [](const EmpreintePlacee& e) { return e.rotation; }, [](EmpreintePlacee& e, double v) { e.rotation = v; });
    auto* face = new QComboBox;
    face->setObjectName(QStringLiteral("placement:face"));
    face->addItems({QStringLiteral("Dessus"), QStringLiteral("Dessous")});
    connect(face, &QComboBox::activated, this, [this](int i) {
      const std::string id = m_composant;
      m_atelier.modifierConception([&](ConceptionElec& c) {
        for (auto& e : c.circuit.empreintes)
          if (e.composant == id) e.dessous = i == 1;
      });
    });
    lier(face, [this, face] {
      if (const EmpreintePlacee* e = empreintePlacee()) face->setCurrentIndex(e->dessous ? 1 : 0);
    });
    fl->addRow(QStringLiteral("Face"), face);
    auto* verrou = new QCheckBox(QStringLiteral("Verrouillée (L)"));
    verrou->setObjectName(QStringLiteral("placement:verrou"));
    connect(verrou, &QCheckBox::toggled, this, [this](bool on) {
      const EmpreintePlacee* e0 = empreintePlacee();
      if (!e0 || e0->verrou == on) return;
      const std::string id = m_composant;
      m_atelier.modifierConception([&](ConceptionElec& c) {
        for (auto& e : c.circuit.empreintes)
          if (e.composant == id) e.verrou = on;
      });
    });
    lier(verrou, [this, verrou] {
      if (const EmpreintePlacee* e = empreintePlacee()) verrou->setChecked(e->verrou);
    });
    fl->addRow(verrou);
  }
  void modifierComposant(const std::function<void(ComposantElec&)>& f) {
    const std::string id = idComposant();
    m_atelier.modifierConception([&](ConceptionElec& c) {
      if (ComposantElec* k = c.composant(id)) f(*k);
    });
  }

  // Symbole à plusieurs unités : unité de ce symbole, unités posées et pose de la suivante
  void construireUnites() {
    QFormLayout* fu = nullptr;
    auto* g = groupe(QStringLiteral("Unités"), fu);
    auto* choix = new QComboBox;
    choix->setObjectName(QStringLiteral("uniteSymbole"));
    connect(choix, &QComboBox::activated, this, [this, choix](int) {
      const int u = choix->currentData().toInt();
      const std::string id = m_objet;
      modifierFeuille([&](Feuille& f) {
        for (auto& y : f.symboles)
          if (y.uuid == id) y.unite = u;
      });
    });
    fu->addRow(QStringLiteral("Unité"), choix);
    lier(choix, [this, choix] {
      const DefSymbole* d = definition();
      const SymbolePlace* s = symbole();
      if (!d || !s) return;
      const std::string signature = d->id + '#' + std::to_string(d->unites);
      if (choix->property("signature").toString().toStdString() != signature) {
        choix->clear();
        for (int u = 1; u <= d->unites; ++u) {
          QStringList bs;
          for (const auto& b : d->broches)
            if (b.unite == u) bs << q(b.numero);
          const auto iu = static_cast<std::size_t>(u - 1);
          const QString nomU = iu < d->nomsUnites.size() && !d->nomsUnites[iu].empty() ? QStringLiteral(" · ") + q(d->nomsUnites[iu]) : QString();
          choix->addItem(QStringLiteral("%1%2 (%3)").arg(q(lettreUnite(u)), nomU, bs.isEmpty() ? QStringLiteral("sans broche") : QStringLiteral("broches ") + bs.join(QStringLiteral(", "))), u);
        }
        choix->setProperty("signature", q(signature));
      }
      choix->setCurrentIndex(std::max(0, choix->findData(s->unite)));
    });
    auto* etat = inspecteur::texte();
    fu->addRow(etat);
    auto* suivante = new QPushButton(QStringLiteral("Poser l’unité suivante"));
    suivante->setObjectName(QStringLiteral("poserUniteSuivante"));
    connect(suivante, &QPushButton::clicked, this, [this] { m_atelier.poserUniteSuivante(m_objet, m_feuille); });
    fu->addRow(suivante);
    action(suivante);
    suivre([this, g, etat, suivante] {
      const DefSymbole* d = definition();
      const ConceptionElec* c = conception();
      g->setVisible(d && d->unites > 1);
      if (!d || !c || d->unites <= 1) return;
      const std::set<int> posees = unitesPosees(*c, idComposant());
      QStringList manquantes;
      for (int u = 1; u <= d->unites; ++u)
        if (!posees.count(u)) manquantes << q(lettreUnite(u));
      etat->setText(manquantes.isEmpty() ? QStringLiteral("Toutes les unités sont posées.")
                                         : QStringLiteral("<span style='color:%1'>Unité(s) non posée(s) : %2</span>").arg(teintes::alerte(), manquantes.join(QStringLiteral(", "))));
      suivante->setEnabled(!manquantes.isEmpty() && !lectureSeule());
    });
  }

  void construireComposant() {
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Composant"), fl);
    auto idc = [this] { return idComposant(); };
    auto* reference = inspecteur::champComposant(m_atelier, idc, "reference");
    fl->addRow(QStringLiteral("Référence"), reference);
    lier(reference, [reference] { reference->relire(); });
    auto* valeur = inspecteur::champComposant(m_atelier, idc, "valeur");
    // ƒx, à droite de la valeur : ouvre la ligne de la formule (fermée tant que la valeur n'en a pas, Tab passe de la
    // valeur à l'empreinte)
    auto* ligneValeur = new QWidget;
    auto* hv = new QHBoxLayout(ligneValeur);
    hv->setContentsMargins(0, 0, 0, 0);
    hv->setSpacing(2);
    hv->addWidget(valeur, 1);
    auto* fx = new QToolButton;
    fx->setObjectName(QStringLiteral("ouvrirFormule"));
    fx->setText(QStringLiteral("ƒx"));
    fx->setAutoRaise(true);
    fx->setFocusPolicy(Qt::NoFocus);
    fx->setToolTip(QStringLiteral("Valeur calculée : 2*R1, R1 ∥ R2, rled(5V, 2V, 20mA)…"));
    hv->addWidget(fx);
    fl->addRow(QStringLiteral("Valeur"), ligneValeur);
    lier(valeur, [valeur] { valeur->relire(); });
    valeur->installEventFilter(new MoletteSerieE(valeur, [this](int pas) {
      const ConceptionElec* c = conception();
      const ComposantElec* x = composant();
      if (!c || !x || !x->formule.empty() || lectureSeule()) return;
      if (const auto v = valeurVoisine(x->valeur, familleDe(*c, *x), pas)) {
        const std::string id = x->uuid, t = *v;
        m_atelier.modifierConception([&](ConceptionElec& c2) {
          if (ComposantElec* k = c2.composant(id)) k->valeur = t;
        }, {}, QStringLiteral("serieE:%1").arg(q(id)));
      }
    }));
    valeur->setToolTip(QStringLiteral("4k7, 4,7 kΩ, 100n, 10µF 25V X7R… Alt+molette : valeur voisine de la série E"));
    // Lecture de la valeur, sous le champ : « = 4,7 kΩ · E24 », un point orangé hors série, ou la raison d'un refus
    // avec les lectures possibles d'un nombre nu (3.2, P26)
    auto* resume = inspecteur::texte();
    resume->setObjectName(QStringLiteral("resumeValeur"));
    resume->setWordWrap(true);
    connect(resume, &QLabel::linkActivated, this, [this](const QString& lien) {
      const std::string t = lien.toStdString();
      modifierComposant([&](ComposantElec& k) { k.valeur = t; });
    });
    fl->addRow(QString(), resume);
    suivre([this, resume, valeur] {
      const ConceptionElec* c = conception();
      const ComposantElec* x = composant();
      if (!c || !x) return;
      valeur->setReadOnly(!x->formule.empty() || lectureSeule());  // valeur calculée : la formule la donne ; publiée : rien ne s'écrit
      const ValeurComprise v = lireValeurComposant(x->valeur, familleDe(*c, *x));
      QString t;
      if (v.comprise()) {
        const bool horsSerie = v.serieReference && v.nombre > 0 && !v.serie;
        t = QStringLiteral("<span style='color:%1'>%2%3</span>").arg(horsSerie ? teintes::alerte() : teintes::secondaire(), horsSerie ? QStringLiteral("● ") : QString(), q(resumeValeur(v)).toHtmlEscaped());
      } else if (v.etat == EtatValeur::Refusee) {
        t = QStringLiteral("<span style='color:%1'>%2</span>").arg(teintes::erreur(), q(v.raison).toHtmlEscaped());
        QStringList liens;
        for (const auto& p : v.propositions) {
          const QString montre = q(p), valeurProposee = montre.section(QStringLiteral(" ("), 0, 0);
          liens << QStringLiteral("<a href=\"%1\" style='color:%2'>%3</a>").arg(valeurProposee.toHtmlEscaped(), teintes::lien(), montre.toHtmlEscaped());
        }
        if (!liens.isEmpty()) t += QStringLiteral("<br>") + liens.join(QStringLiteral(" · "));
      }
      if (!x->formule.empty()) t = QStringLiteral("<span style='color:%1'>calculée : %2</span>%3").arg(teintes::secondaire(), q(x->formule).toHtmlEscaped(), t.isEmpty() ? QString() : QStringLiteral("<br>") + t);
      resume->setVisible(!t.isEmpty());
      resume->setText(t);
    });
    // Valeur calculée (3.14, SAI-44) : « 2*R1 », « 1/(2π·R3·C5) », avec les variables de la conception
    auto* formule = new QLineEdit;
    formule->setObjectName(QStringLiteral("formuleValeur"));
    formule->setPlaceholderText(QStringLiteral("aucune"));
    formule->setToolTip(QStringLiteral("La valeur suit cette formule : 2*R1, R1 ∥ R2, rled(5V, 2V, 20mA), eserie(4,8k, 24)… Vide : la valeur se tape."));
    connect(formule, &QLineEdit::editingFinished, this, [this, formule] {
      const ComposantElec* x = composant();
      const std::string t = formule->text().trimmed().toStdString();
      if (!x || x->formule == t) return;
      modifierComposant([&](ComposantElec& k) { k.formule = t; });
    });
    fl->addRow(QStringLiteral("Formule"), formule);
    fl->setRowVisible(formule, false);
    connect(fx, &QToolButton::clicked, this, [fl, formule] {
      fl->setRowVisible(formule, true);
      formule->setFocus(Qt::OtherFocusReason);
    });
    lier(formule, [this, formule, fl] {
      const ConceptionElec* c = conception();
      const ComposantElec* x = composant();
      if (!x || !c) return;
      // La ligne reste ouverte pendant la saisie ; sinon elle suit la présence d'une formule
      if (!formule->hasFocus()) fl->setRowVisible(formule, !x->formule.empty());
      if (formule->text().toStdString() != x->formule) formule->setText(q(x->formule));
      // L'erreur de la formule (cycle, nom inconnu, unité) au survol, le bord en rouge
      std::string erreur;
      if (!x->formule.empty()) {
        const ResultatFormule r = evaluerFormule(x->formule, [c](const std::string& n) { return grandeurDe(*c, n); });
        if (!r.ok) erreur = r.erreur;
        else if (const FamilleValeur f = familleDe(*c, *x); f != FamilleValeur::Aucune && !r.q.dim.sans() && !(r.q.dim == dimensionDeFamille(f))) erreur = phraseUnite(f);
      }
      formule->setStyleSheet(erreur.empty() ? QString() : QStringLiteral("QLineEdit { border: 1px solid %1; }").arg(teintes::erreur()));
      formule->setToolTip(erreur.empty() ? QStringLiteral("La valeur suit cette formule.") : q(erreur));
    });

    // Empreinte : celles du catalogue au bon nombre de pastilles, puis celles du projet et de la bibliothèque personnelle
    auto* emp = new QComboBox;
    emp->setObjectName(QStringLiteral("empreinteComposant"));
    emp->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    emp->setMinimumContentsLength(14);
    connect(emp, &QComboBox::activated, this, [this, emp](int) {
      const std::string v = emp->currentData().toString().toStdString();
      // Une empreinte de la bibliothèque personnelle est figée dans le projet au moment du choix
      std::optional<DefEmpreinte> perso;
      if (const ConceptionElec* c0 = conception(); c0 && !c0->empreintes.count(v))
        for (const auto& de : AtelierElectronique::empreintesPerso())
          if (de.id == v) perso = de;
      const std::string id = idComposant();
      m_atelier.modifierConception([&](ConceptionElec& c2) {
        if (perso) c2.empreintes[v] = *perso;
        if (auto* k = c2.composant(id)) k->empreinte = v;
      });
    });
    fl->addRow(QStringLiteral("Empreinte"), emp);
    lier(emp, [this, emp] {
      const ConceptionElec* c = conception();
      const DefSymbole* d = definition();
      const ComposantElec* x = composant();
      if (!c || !d || !x) return;
      std::set<std::string> numeros;
      for (const auto& b : d->broches) numeros.insert(b.numero);
      const std::vector<DefEmpreinte> persos = AtelierElectronique::empreintesPerso();
      std::string signature = d->id + '#' + std::to_string(numeros.size()) + '#' + x->empreinte + '#' + std::to_string(persos.size()) + '#';
      for (const auto& [ide, de] : c->empreintes) signature += ide + ',';
      if (emp->property("signature").toString().toStdString() != signature) {
        emp->clear();
        emp->addItem(QStringLiteral("— aucune —"), QString());
        for (const auto& i : catalogueEmpreintes())
          if (i.pastilles == static_cast<int>(numeros.size()) || i.pastilles == static_cast<int>(numeros.size()) + 1 || i.id == x->empreinte)
            emp->addItem(q(i.id) + QStringLiteral("  ·  ") + q(i.description), q(i.id));
        std::set<std::string> vues;
        auto proposer = [&](const DefEmpreinte& de) {
          std::set<std::string> p;
          for (const auto& pa : de.pastilles) p.insert(pa.numero);
          if (!vues.insert(de.id).second || emp->findData(q(de.id)) >= 0) return;
          if (p.size() == numeros.size() || de.id == x->empreinte) emp->addItem(q(de.id) + QStringLiteral("  ·  ") + q(de.description), q(de.id));
        };
        for (const auto& [ide, de] : c->empreintes)
          if (ide.rfind("perso:", 0) == 0 || ide.rfind("kicad:", 0) == 0) proposer(de);
        for (const auto& de : persos) proposer(de);
        if (!x->empreinte.empty() && emp->findData(q(x->empreinte)) < 0) emp->addItem(q(x->empreinte), q(x->empreinte));
        emp->setProperty("signature", q(signature));
      }
      emp->setCurrentIndex(std::max(0, emp->findData(q(x->empreinte))));
    });

    // Garde-fou du brochage (B17, ERC-09) : une empreinte d'une autre famille de boîtier que celle pour laquelle le
    // symbole est numéroté bloque la publication tant que la correspondance n'est pas vérifiée sur la fiche
    auto* alerte = inspecteur::texte();
    fl->addRow(alerte);
    // Libellés courts : une case ne passe pas à la ligne, et le panneau suit sa largeur (P25)
    auto* verifie = new QCheckBox(QStringLiteral("Brochage vérifié sur la fiche"));
    verifie->setObjectName(QStringLiteral("brochageVerifie"));
    verifie->setToolTip(QStringLiteral("Brochage vérifié sur la fiche technique pour cette empreinte. Coche seulement après avoir vérifié que chaque broche du symbole arrive sur la pastille de même fonction."));
    connect(verifie, &QCheckBox::toggled, this, [this](bool on) { modifierComposant([on](ComposantElec& k) { k.brochageVerifie = on ? k.empreinte : std::string(); }); });
    fl->addRow(verifie);
    lier(verifie, [this, verifie] {
      if (const ComposantElec* x = composant()) verifie->setChecked(!x->brochageVerifie.empty() && x->brochageVerifie == x->empreinte);
    });
    suivre([this, alerte, verifie] {
      const ConceptionElec* c = conception();
      const ComposantElec* x = composant();
      const DefSymbole* d = definition();
      const bool aVerifier = c && x && brochageAVerifier(*c, *x);
      alerte->setVisible(aVerifier);
      verifie->setVisible(aVerifier || (x && !x->brochageVerifie.empty() && x->brochageVerifie == x->empreinte));
      if (aVerifier && d)
        alerte->setText(QStringLiteral("<span style='color:%1'><b>Brochage à vérifier</b></span> : le symbole est numéroté pour <b>%2</b>, l’empreinte est d’une autre famille de boîtier. "
                                       "Une broche peut arriver sur la mauvaise pastille (drain et source d’un MOSFET).")
                            .arg(teintes::erreur(), q(d->empreinte).toHtmlEscaped()));
    });
    {
      auto* boutons = new QWidget;
      auto* hb = new QHBoxLayout(boutons);
      hb->setContentsMargins(0, 0, 0, 0);
      auto* creerE = new QPushButton(icone(QStringLiteral("pcb")), QStringLiteral("Créer…"));
      creerE->setToolTip(QStringLiteral("Créer une empreinte par tableau de pastilles (connecteur, barrette, bornier, boîtier DIL)"));
      connect(creerE, &QPushButton::clicked, this, [this] { m_atelier.creerEmpreinte(idComposant()); });
      auto* kicadE = new QPushButton(icone(QStringLiteral("kicad")), QStringLiteral("KiCad…"));
      kicadE->setToolTip(QStringLiteral("Importer une empreinte KiCad (.kicad_mod)"));
      connect(kicadE, &QPushButton::clicked, this, [this] { m_atelier.importerEmpreinteKicad(idComposant()); });
      hb->addWidget(creerE);
      hb->addWidget(kicadE);
      action(creerE);
      action(kicadE);
      fl->addRow(QString(), boutons);
    }
    // Hauteur de ce composant (3.4, P5, P17) : une seule hauteur, lue par la 3D, le boîtier et la vérification, avec sa
    // provenance. Taper une hauteur la met en « saisie » ; « vérifiée sur la fiche » est une case à part ; une hauteur
    // invraisemblable (plus de trois fois la générique, plus de 25 mm en CMS) se confirme dans le panneau.
    auto* h = new ChampNombre;
    h->setObjectName(QStringLiteral("hauteurComposant"));
    h->setRange(0, 100);
    h->setDecimals(2);
    h->setSuffix(QStringLiteral(" mm"));
    auto* provenance = inspecteur::texte();
    provenance->setObjectName(QStringLiteral("provenanceHauteur"));
    auto* aConfirmer = new QWidget;
    aConfirmer->setObjectName(QStringLiteral("hauteurAConfirmer"));
    auto* lc = new QHBoxLayout(aConfirmer);
    lc->setContentsMargins(0, 0, 0, 0);
    auto* question = inspecteur::texte();
    question->setWordWrap(true);
    auto* garder = new QPushButton(QStringLiteral("Confirmer"));
    garder->setObjectName(QStringLiteral("confirmerHauteur"));
    lc->addWidget(question, 1);
    lc->addWidget(garder);
    aConfirmer->hide();
    const auto enAttente = std::make_shared<double>(0.0);
    auto ecrire = [this](double v) {
      modifierComposant([v](ComposantElec& k) {
        k.hauteur = v;
        k.provenanceHauteur = ProvenanceHauteur::Saisie;  // une hauteur tapée n'est pas confirmée (P5)
        k.sourceHauteur.clear();
      });
    };
    connect(h, &QDoubleSpinBox::editingFinished, this, [this, h, aConfirmer, question, enAttente, ecrire] {
      const double v = h->value();
      const ComposantElec* x = composant();
      const ConceptionElec* c = conception();
      if (!x || !c || std::abs(x->hauteur - v) < 1e-9) return;
      const std::string q = hauteurAConfirmer(v, x->empreinte.empty() ? nullptr : c->empreinte(x->empreinte));
      if (!q.empty()) {
        *enAttente = v;
        question->setText(QStringLiteral("<span style='color:%1'>%2</span>").arg(teintes::alerte(), QString::fromStdString(q).toHtmlEscaped()));
        aConfirmer->show();
        return;
      }
      aConfirmer->hide();
      ecrire(v);
    });
    connect(garder, &QPushButton::clicked, this, [aConfirmer, enAttente, ecrire] {
      aConfirmer->hide();
      ecrire(*enAttente);
    });
    action(garder);
    auto* ligneH = new QWidget;
    auto* lh = new QHBoxLayout(ligneH);
    lh->setContentsMargins(0, 0, 0, 0);
    lh->addWidget(h, 1);
    lh->addWidget(provenance);
    h->setMinimumWidth(72);
    h->setToolTip(QStringLiteral("Hauteur de ce composant, distincte de la hauteur par défaut de son empreinte : la 3D, le boîtier et la vérification la lisent"));
    // Libellé court : un libellé long poussait le champ hors d'un inspecteur étroit
    fl->addRow(QStringLiteral("Hauteur"), ligneH);  // distincte de la hauteur par défaut de l'empreinte (T2)
    fl->addRow(aConfirmer);
    lier(h, [this, h, provenance] {
      const ConceptionElec* c = conception();
      const ComposantElec* x = composant();
      if (!c || !x) return;
      const DefEmpreinte* de = x->empreinte.empty() ? nullptr : c->empreinte(x->empreinte);
      const double generique = de ? hauteurGenerique(*de) : 0;
      h->setSpecialValueText(generique > 0 ? QStringLiteral("%1 mm").arg(QLocale().toString(generique, 'f', 2)) : QStringLiteral("générique"));
      h->setValue(x->hauteur);
      const HauteurComposant hc = hauteurComposant(x, de);
      const bool gris = hc.provenance == ProvenanceHauteur::Generique;
      provenance->setText(QStringLiteral("<span style='color:%1'>%2</span>").arg(gris ? teintes::secondaire() : teintes::succes(), QString::fromUtf8(nomProvenance(hc.provenance))));
      provenance->setToolTip(QString::fromStdString(hc.texte() + " — " + hc.source));
    });
    auto* verif = new QCheckBox(QStringLiteral("Vérifiée sur la fiche technique"));
    verif->setObjectName(QStringLiteral("hauteurVerifiee"));
    verif->setToolTip(QStringLiteral("Hauteur confirmée sur la fiche technique. Sans elle, le contrôle du boîtier qui contient cette carte reste « incomplet »."));
    connect(verif, &QCheckBox::toggled, this, [this](bool on) {
      modifierComposant([on](ComposantElec& k) { k.provenanceHauteur = on ? ProvenanceHauteur::Fiche : ProvenanceHauteur::Saisie; });
    });
    auto* generique = new QPushButton(QStringLiteral("Remettre la hauteur générique"));
    generique->setObjectName(QStringLiteral("hauteurGenerique"));
    connect(generique, &QPushButton::clicked, this, [this, aConfirmer] {
      aConfirmer->hide();
      modifierComposant([](ComposantElec& k) {
        k.hauteur = 0;
        k.provenanceHauteur = ProvenanceHauteur::Saisie;  // la case se décoche (P5)
        k.sourceHauteur.clear();
      });
    });
    action(generique);
    fl->addRow(verif);
    fl->addRow(QString(), generique);
    lier(verif, [this, verif] {
      if (const ComposantElec* x = composant()) verif->setChecked(x->provenanceHauteur == ProvenanceHauteur::Fiche);
    });
    auto* remplacer = new QPushButton(QStringLiteral("Remplacer le composant…"));
    remplacer->setObjectName(QStringLiteral("remplacerComposant"));
    remplacer->setToolTip(QStringLiteral("Un autre article à sa place : repère, place et fils gardés, broches par nom ou par numéro"));
    connect(remplacer, &QPushButton::clicked, this, [this] { m_atelier.remplacerComposants({idComposant()}); });
    fl->addRow(remplacer);
    action(remplacer);
    auto* edition = new QPushButton(icone(QStringLiteral("nouvelle-piece")), QString());
    edition->setObjectName(QStringLiteral("editionComposant"));
    connect(edition, &QPushButton::clicked, this, [this] {
      const DefSymbole* d = definition();
      if (!d) return;
      const DefSymbole copie = *d;
      m_atelier.creerComposant(&copie, copie.id.rfind("perso:", 0) == 0);
    });
    fl->addRow(edition);
    action(edition);
    suivre([this, edition] {
      const DefSymbole* d = definition();
      if (!d) return;
      const bool perso = d->id.rfind("perso:", 0) == 0;
      edition->setText(perso ? QStringLiteral("Modifier ce composant…") : QStringLiteral("Créer une variante…"));
      edition->setToolTip(perso ? QStringLiteral("Broches, empreinte, fabricant : tous les symboles de ce composant suivent")
                                : QStringLiteral("Un nouveau composant à toi, qui part de celui-ci (broches, empreinte)"));
    });
  }

  void construireApprovisionnement() {
    QFormLayout* f2 = nullptr;
    groupe(QStringLiteral("Approvisionnement"), f2);
    auto idc = [this] { return idComposant(); };
    for (const auto& [libelle, propriete] : std::initializer_list<std::pair<QString, std::string>>{{QStringLiteral("Fabricant"), "fabricant"},
                                                                                                    {QStringLiteral("Réf. fabricant"), "mpn"},
                                                                                                    {QStringLiteral("Fournisseur"), "fournisseur"},
                                                                                                    {QStringLiteral("Description"), "description"},
                                                                                                    {QStringLiteral("Fiche technique"), "fiche"}}) {
      auto* champ = inspecteur::champComposant(m_atelier, idc, propriete);
      f2->addRow(libelle, champ);
      lier(champ, [champ] { champ->relire(); });
    }
    // Fiche technique : le lien qui l'ouvre (EL-42)
    auto* lien = inspecteur::texte();
    lien->setOpenExternalLinks(true);
    f2->addRow(QString(), lien);
    suivre([this, lien] {
      const ComposantElec* x = composant();
      lien->setVisible(x && !x->fiche.empty());
      if (x && !x->fiche.empty()) lien->setText(QStringLiteral("<a href=\"%1\" style='color:%2'>Ouvrir la fiche</a>").arg(q(x->fiche).toHtmlEscaped(), teintes::lien()));
    });
    // Champs libres (P10, P18) : un tableau nom et valeur, appliqué ligne par ligne ; la dernière ligne, vide, en
    // ajoute un ; un nom effacé retire le champ. Pas de gabarit grisé qui ressemble à des valeurs.
    auto* table = new QTableWidget(0, 2);
    table->setObjectName(QStringLiteral("champsLibres"));
    table->setHorizontalHeaderLabels({QStringLiteral("Nom"), QStringLiteral("Valeur")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->hide();
    table->setMaximumHeight(150);
    table->setToolTip(QStringLiteral("Champs libres, repris dans la nomenclature. La dernière ligne en ajoute un ; effacer un nom retire le champ."));
    connect(table, &QTableWidget::itemChanged, this, [this, table](QTableWidgetItem*) {
      std::map<std::string, std::string> m;
      for (int r = 0; r < table->rowCount(); ++r) {
        const QTableWidgetItem* a = table->item(r, 0);
        const QTableWidgetItem* b = table->item(r, 1);
        const QString nom = a ? a->text().trimmed() : QString();
        if (!nom.isEmpty()) m[nom.toStdString()] = b ? b->text().trimmed().toStdString() : std::string();
      }
      const ComposantElec* x = composant();
      if (!x || x->champs == m) return;
      modifierComposant([&](ComposantElec& k) { k.champs = m; });
    });
    lier(table, [this, table] {
      const ComposantElec* x = composant();
      if (!x) return;
      const QSignalBlocker b(table);
      table->setRowCount(static_cast<int>(x->champs.size()) + 1);
      int r = 0;
      for (const auto& [nom, valeur] : x->champs) {
        table->setItem(r, 0, new QTableWidgetItem(q(nom)));
        table->setItem(r, 1, new QTableWidgetItem(q(valeur)));
        ++r;
      }
      table->setItem(r, 0, new QTableWidgetItem());
      table->setItem(r, 1, new QTableWidgetItem());
    });
    f2->addRow(QStringLiteral("Champs libres"), table);
  }

  // Paramètres typés de la famille (3.3) : tolérance, puissance, tension, diélectrique… chacun avec son unité, lu à la
  // saisie ; la section se refait quand la famille change
  void construireParametres() {
    auto* g = new QGroupBox(QStringLiteral("Paramètres"));
    g->setObjectName(QStringLiteral("parametresComposant"));
    auto* lg = new QVBoxLayout(g);
    auto* section = new SectionRefaite([this](QVBoxLayout* col) {
      const ConceptionElec* c = conception();
      const ComposantElec* x = composant();
      if (!c || !x) return;
      auto* form = new QFormLayout;
      form->setRowWrapPolicy(QFormLayout::WrapLongRows);
      col->addLayout(form);
      for (const DefParametre& p : parametresDeFamille(familleDe(*c, *x))) {
        const std::string cle = p.cle;
        const QString libelle = q(p.libelle) + (p.unite.empty() ? QString() : QStringLiteral(" (%1)").arg(q(p.unite)));
        auto ecrire = [this, cle](const std::string& t) {
          const ComposantElec* y = composant();
          if (!y) return;
          const auto it = y->parametres.find(cle);
          if ((it == y->parametres.end() && t.empty()) || (it != y->parametres.end() && it->second == t)) return;
          modifierComposant([&](ComposantElec& k) {
            if (t.empty()) k.parametres.erase(cle);
            else k.parametres[cle] = t;
          });
        };
        auto lu = [this, cle]() -> QString {
          const ComposantElec* y = composant();
          if (!y) return {};
          const auto it = y->parametres.find(cle);
          return it == y->parametres.end() ? QString() : q(it->second);
        };
        if (p.genre == DefParametre::Genre::Choix || p.genre == DefParametre::Genre::Booleen) {
          auto* cb = new QComboBox;
          cb->setObjectName(QStringLiteral("parametre:") + q(cle));
          cb->addItem(QStringLiteral("—"), QString());
          if (p.genre == DefParametre::Genre::Booleen) {
            cb->addItem(QStringLiteral("oui"), QStringLiteral("oui"));
            cb->addItem(QStringLiteral("non"), QStringLiteral("non"));
          } else
            for (const auto& choix : p.choix) cb->addItem(q(choix), q(choix));
          connect(cb, &QComboBox::activated, this, [cb, ecrire](int) { ecrire(cb->currentData().toString().toStdString()); });
          lier(cb, [cb, lu] {
            const QString v = lu();
            int i = cb->findData(v);
            if (i < 0 && !v.isEmpty()) {
              cb->addItem(v, v);  // texte d'avant la migration, gardé tel quel
              i = cb->count() - 1;
            }
            cb->setCurrentIndex(std::max(0, i));
          });
          form->addRow(libelle, cb);
        } else {
          auto* le = new QLineEdit;
          le->setObjectName(QStringLiteral("parametre:") + q(cle));
          le->setPlaceholderText(q(p.unite));
          const DefParametre def = p;
          connect(le, &QLineEdit::editingFinished, this, [le, def, ecrire] {
            const std::string t = le->text().trimmed().toStdString();
            std::string erreur;
            if (!t.empty() && def.genre == DefParametre::Genre::Nombre && !lireParametre(def, t, &erreur)) {
              le->setStyleSheet(QStringLiteral("QLineEdit { border: 1px solid %1; }").arg(teintes::erreur()));
              le->setToolTip(q(erreur));
              return;
            }
            le->setStyleSheet(QString());
            le->setToolTip(QString());
            ecrire(t);
          });
          lier(le, [le, lu] {
            if (le->text() != lu()) le->setText(lu());
          });
          form->addRow(libelle, le);
        }
      }
    });
    lg->addWidget(section);
    colonne()->addWidget(g);
    suivre([this, section] {
      const ConceptionElec* c = conception();
      const ComposantElec* x = composant();
      if (!c || !x) return;
      section->refaire(x->uuid + "#" + std::to_string(static_cast<int>(familleDe(*c, *x))));
    });
  }

  // Genre et exclusions (3.17, SAI-45, STR-08) : passage au circuit, à la nomenclature, au fichier de placement, à la
  // simulation
  void construireExclusions() {
    QFormLayout* f3 = nullptr;
    groupe(QStringLiteral("Genre et exclusions"), f3);
    auto* genre = new QComboBox;
    genre->setObjectName(QStringLiteral("genreComposant"));
    for (auto g : {GenreComposant::Standard, GenreComposant::Mecanique, GenreComposant::Graphique, GenreComposant::LiaisonReseaux, GenreComposant::HorsNomenclature, GenreComposant::Cavalier})
      genre->addItem(QString::fromUtf8(nomGenreComposant(g)), static_cast<int>(g));
    genre->setToolTip(QStringLiteral("Standard : circuit, nomenclature et placement. Mécanique : pas de placement. Graphique, liaison de réseaux : circuit seul. "
                                     "Hors nomenclature : circuit et placement. Cavalier : les trois."));
    connect(genre, &QComboBox::activated, this, [this, genre](int) {
      const auto g = static_cast<GenreComposant>(genre->currentData().toInt());
      modifierComposant([g](ComposantElec& k) { k.genre = g; });
    });
    lier(genre, [this, genre] {
      if (const ComposantElec* x = composant()) genre->setCurrentIndex(std::max(0, genre->findData(static_cast<int>(x->genre))));
    });
    f3->addRow(QStringLiteral("Genre"), genre);
    struct Case {
      const char* objet;
      QString libelle;
      bool ComposantElec::*champ;
    };
    for (const Case& k : {Case{"nePasMonter", QStringLiteral("Ne pas monter (variante)"), &ComposantElec::nePasMonter},
                          Case{"horsCarte", QStringLiteral("Hors carte"), &ComposantElec::horsCarte},
                          Case{"horsNomenclature", QStringLiteral("Hors nomenclature"), &ComposantElec::horsNomenclature},
                          Case{"horsPlacement", QStringLiteral("Hors fichier de placement"), &ComposantElec::horsPlacement},
                          Case{"horsSimulation", QStringLiteral("Hors simulation"), &ComposantElec::horsSimulation}}) {
      auto* ck = new QCheckBox(k.libelle);
      ck->setObjectName(QString::fromLatin1(k.objet));
      const auto champ = k.champ;
      connect(ck, &QCheckBox::toggled, this, [this, champ](bool on) {
        const ComposantElec* x = composant();
        if (!x || x->*champ == on) return;
        modifierComposant([on, champ](ComposantElec& y) { y.*champ = on; });
      });
      lier(ck, [this, ck, champ] {
        if (const ComposantElec* x = composant()) ck->setChecked(x->*champ);
      });
      f3->addRow(ck);
    }
  }

  // Onglet Broches (3.16, SAI-43, P13) : numéro, nom, type, unité, réseau, repère de borne, nom visible, marque de
  // non-connexion. Les colonnes modifiables s'éditent là, sur plusieurs lignes à la fois (la case cochée s'applique
  // à toutes les lignes choisies) ; un clic sur une ligne montre la broche sur la feuille et surligne son réseau.
  enum ColonneBroche { NUMERO, NOM, TYPE, UNITE, RESEAU, BORNE, VISIBLE, NC, NB_COLONNES };
  void construireBroches() {
    auto* g = new QGroupBox(QStringLiteral("Broches"));
    auto* l3 = new QVBoxLayout(g);
    auto* table = new QTableWidget(0, NB_COLONNES);
    table->setObjectName(QStringLiteral("brochesComposant"));
    table->setHorizontalHeaderLabels({QStringLiteral("N°"), QStringLiteral("Nom"), QStringLiteral("Type"), QStringLiteral("Unité"), QStringLiteral("Réseau"),
                                      QStringLiteral("Borne"), QStringLiteral("Nom visible"), QStringLiteral("Non connectée")});
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->setMinimumHeight(120);
    l3->addWidget(table);
    colonne()->addWidget(g);
    // Point de connexion d'une broche, sur la feuille montrée
    auto point = [this](const std::string& numero, Pt& p) {
      const SymbolePlace* s = symbole();
      const DefSymbole* d = definition();
      if (!s || !d) return false;
      for (const auto& b : brochesPlacees(*s, *d))
        if (b.broche->numero == numero) {
          p = b.p;
          return true;
        }
      return false;
    };
    connect(table, &QTableWidget::cellClicked, this, [this, table, point](int ligne, int) {
      const QTableWidgetItem* it = table->item(ligne, NUMERO);
      if (!it) return;
      const std::string numero = it->text().toStdString();
      if (const QTableWidgetItem* r = table->item(ligne, RESEAU); r && !r->text().isEmpty() && r->text() != QLatin1String("—")) m_atelier.surligner(r->text());
      Pt p;
      if (!m_circuit && point(numero, p)) m_atelier.vueSchema()->centrerSur(p);
    });
    connect(table, &QTableWidget::itemChanged, this, [this, table, point](QTableWidgetItem* it) {
      if (!it || lectureSeule()) return;
      const int col = it->column();
      // La même valeur sur toutes les lignes choisies de cette colonne
      std::vector<int> lignes{it->row()};
      for (const QModelIndex& m : table->selectionModel()->selectedRows())
        if (m.row() != it->row()) lignes.push_back(m.row());
      std::vector<std::string> numeros;
      for (int l : lignes)
        if (const QTableWidgetItem* n = table->item(l, NUMERO)) numeros.push_back(n->data(Qt::UserRole).toString().toStdString());
      const std::string uuidPose = m_objet, idSymbole = definition() ? definition()->id : std::string();
      const int fi = m_feuille;
      if (col == BORNE) {
        const std::string borne = it->text().trimmed().toStdString();
        m_atelier.modifierConception([&](ConceptionElec& c) {
          if (fi < 0 || fi >= static_cast<int>(c.feuilles.size())) return;
          for (auto& sp : c.feuilles[static_cast<std::size_t>(fi)].symboles)
            if (sp.uuid == uuidPose)
              for (const auto& n : numeros) {
                if (borne.empty() || borne == n) sp.numeros.erase(n);
                else if (lignes.size() == 1) sp.numeros[n] = borne;  // un repère de borne est propre à une broche
              }
        }, QStringLiteral("Repère de borne"));
      } else if (col == VISIBLE) {
        const bool on = it->checkState() == Qt::Checked;
        m_atelier.modifierConception([&](ConceptionElec& c) {
          const DefSymbole* d0 = c.symbole(idSymbole);
          if (!d0) return;
          DefSymbole d = *d0;
          for (auto& b : d.broches)
            if (std::find(numeros.begin(), numeros.end(), b.numero) != numeros.end()) b.nomVisible = on;
          c.symboles[idSymbole] = d;  // la définition du projet
        }, on ? QStringLiteral("Noms de broche montrés") : QStringLiteral("Noms de broche cachés"));
      } else if (col == NC) {
        const bool on = it->checkState() == Qt::Checked;
        std::vector<Pt> points;
        for (const auto& n : numeros) {
          Pt p;
          if (point(n, p)) points.push_back(p);
        }
        m_atelier.modifierConception([&](ConceptionElec& c) {
          if (fi < 0 || fi >= static_cast<int>(c.feuilles.size())) return;
          Feuille& f = c.feuilles[static_cast<std::size_t>(fi)];
          for (const Pt& p : points) {
            const bool deja = std::any_of(f.nonConnexions.begin(), f.nonConnexions.end(), [&](const NonConnexion& x) { return x.p == p; });
            if (on && !deja) f.nonConnexions.push_back({p, nouvelUuid()});
            if (!on) std::erase_if(f.nonConnexions, [&](const NonConnexion& x) { return x.p == p; });
          }
        }, on ? QStringLiteral("Marque de non-connexion") : QStringLiteral("Marque de non-connexion retirée"));
      }
    });
    lier(table, [this, table, point] {
      const DefSymbole* d = definition();
      const std::string id = idComposant();
      if (!d) return;
      const SymbolePlace* s = symbole();
      const Feuille* f = feuille();
      const Connectivite& k = m_atelier.calculs().k;
      const QSignalBlocker b(table);
      std::vector<const Broche*> broches;
      std::set<std::string> listees;  // une broche commune répétée sur chaque unité n'est listée qu'une fois
      for (const auto& x : d->broches)
        if (listees.insert(x.numero).second) broches.push_back(&x);
      table->setRowCount(static_cast<int>(broches.size()));
      for (int l = 0; l < static_cast<int>(broches.size()); ++l) {
        const Broche& x = *broches[static_cast<std::size_t>(l)];
        const std::string reel = s ? numeroBroche(*s, x.numero) : x.numero;
        const std::string r = k.nom(id, reel);
        auto lecture = [](const QString& t) {
          auto* it = new QTableWidgetItem(t);
          it->setFlags(it->flags() & ~Qt::ItemIsEditable);
          return it;
        };
        auto* num = lecture(q(x.numero));
        num->setData(Qt::UserRole, q(x.numero));
        table->setItem(l, NUMERO, num);
        table->setItem(l, NOM, lecture(x.nom == "~" ? QString() : q(x.nom)));
        table->setItem(l, TYPE, lecture(QString::fromUtf8(nomTypeBroche(x.type))));
        table->setItem(l, UNITE, lecture(d->unites > 1 && x.unite > 0 ? q(lettreUnite(x.unite)) : QString()));
        auto* reseau = lecture(r.empty() ? QStringLiteral("—") : q(r));
        reseau->setForeground(QColor(teintes::lien()));
        reseau->setToolTip(QStringLiteral("Un clic surligne le réseau partout"));
        table->setItem(l, RESEAU, reseau);
        table->setItem(l, BORNE, new QTableWidgetItem(reel == x.numero ? QString() : q(reel)));
        auto* vis = new QTableWidgetItem;
        vis->setFlags((vis->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        vis->setCheckState(x.nomVisible ? Qt::Checked : Qt::Unchecked);
        table->setItem(l, VISIBLE, vis);
        auto* nc = new QTableWidgetItem;
        nc->setFlags((nc->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        Pt p;
        const bool marque = f && point(x.numero, p) && std::any_of(f->nonConnexions.begin(), f->nonConnexions.end(), [&](const NonConnexion& m) { return m.p == p; });
        nc->setCheckState(marque ? Qt::Checked : Qt::Unchecked);
        table->setItem(l, NC, nc);
      }
    });
  }

  bool m_circuit = false;
  std::string m_composant;
};

// ---------- Plusieurs composants (3.9, 3.15 ; SAI-15, SAI-16, SAI-41, P11) ----------
// L'en-tête compte et range ; les champs communs montrent la valeur commune ou « (différentes) », qui n'est pas une
// valeur ; taper l'applique à tous, en un seul pas d'annulation. Une règle de texte (remplacer « de » par « à »,
// ajouter au début ou à la fin, formule en x) s'applique objet par objet ; le pinceau recopie les propriétés d'un
// composant sur ceux qu'on choisit ensuite, jusqu'à « Terminer » ou Échap. Au schéma comme au circuit.
class PagePlusieurs : public PageSchema {
 public:
  PagePlusieurs(AtelierElectronique& a, bool circuit) : PageSchema(a), m_circuit(circuit) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] { titre->setText(resume()); });
    // Champs communs
    QFormLayout* fl = nullptr;
    m_communs = groupe(QStringLiteral("Composants choisis"), fl);
    auto texte = [this, fl](const QString& libelle, const char* nom, std::string ComposantElec::*champ) {
      auto* le = new QLineEdit;
      le->setObjectName(QString::fromLatin1(nom));
      connect(le, &QLineEdit::editingFinished, this, [this, le, champ, libelle] {
        if (!le->isModified()) return;
        le->setModified(false);
        const std::string t = le->text().trimmed().toStdString();
        const auto ids = composants();
        if (ids.empty()) return;
        m_atelier.modifierConception([&](ConceptionElec& c) {
          for (const auto& id : ids)
            if (ComposantElec* k = c.composant(id)) {
              k->*champ = t;
              if (champ == &ComposantElec::valeur) k->formule.clear();
            }
        }, QStringLiteral("%1 de %2 composants : %3").arg(libelle).arg(ids.size()).arg(q(t)));
      });
      lier(le, [this, le, champ] {
        std::set<std::string> v;
        for (const ComposantElec* k : choisis()) v.insert(k->*champ);
        const bool differentes = v.size() > 1;
        le->setText(v.size() == 1 ? q(*v.begin()) : QString());
        le->setPlaceholderText(differentes ? QStringLiteral("(différentes)") : QString());
        QFont f = le->font();
        f.setItalic(differentes);
        le->setFont(f);
      });
      fl->addRow(libelle, le);
      return le;
    };
    texte(QStringLiteral("Valeur"), "valeurPlusieurs", &ComposantElec::valeur);
    texte(QStringLiteral("Fabricant"), "fabricantPlusieurs", &ComposantElec::fabricant);
    texte(QStringLiteral("Réf. fabricant"), "mpnPlusieurs", &ComposantElec::mpn);
    // Empreinte : seulement si tous ont le même nombre de broches
    auto* emp = new QComboBox;
    emp->setObjectName(QStringLiteral("empreintePlusieurs"));
    emp->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    emp->setMinimumContentsLength(14);
    connect(emp, &QComboBox::activated, this, [this, emp](int) {
      const std::string v = emp->currentData().toString().toStdString();
      if (v.empty()) return;
      const auto ids = composants();
      m_atelier.modifierConception([&](ConceptionElec& c) {
        for (const auto& id : ids)
          if (ComposantElec* k = c.composant(id)) k->empreinte = v;
      }, QStringLiteral("Empreinte de %1 composants : %2").arg(ids.size()).arg(q(v)));
    });
    fl->addRow(QStringLiteral("Empreinte"), emp);
    lier(emp, [this, emp, fl] {
      const ConceptionElec* c = conception();
      std::set<std::size_t> broches;
      std::set<std::string> actuelles;
      for (const ComposantElec* k : choisis()) {
        const DefSymbole* d = c ? c->symbole(k->symbole) : nullptr;
        std::set<std::string> n;
        if (d)
          for (const auto& b : d->broches) n.insert(b.numero);
        broches.insert(n.size());
        actuelles.insert(k->empreinte);
      }
      fl->setRowVisible(emp, broches.size() == 1);
      if (broches.size() != 1) return;
      const std::size_t nb = *broches.begin();
      emp->clear();
      emp->addItem(actuelles.size() == 1 ? q(*actuelles.begin()) : QStringLiteral("(différentes)"), QString());
      for (const auto& i : catalogueEmpreintes())
        if (i.pastilles == static_cast<int>(nb)) emp->addItem(q(i.id) + QStringLiteral("  ·  ") + q(i.description), q(i.id));
      emp->setCurrentIndex(0);
    });
    // Hauteur : appliquée à tous, chacun en « saisie »
    auto* h = new ChampNombre;
    h->setObjectName(QStringLiteral("hauteurPlusieurs"));
    h->setRange(0, 100);
    h->setDecimals(2);
    h->setSuffix(QStringLiteral(" mm"));
    h->setSpecialValueText(QStringLiteral("(générique ou différentes)"));
    connect(h, &QDoubleSpinBox::editingFinished, this, [this, h] {
      const double v = h->value();
      const auto ids = composants();
      bool change = false;
      for (const ComposantElec* k : choisis()) change |= std::abs(k->hauteur - v) > 1e-9;
      if (!change || ids.empty()) return;
      m_atelier.modifierConception([&](ConceptionElec& c) {
        for (const auto& id : ids)
          if (ComposantElec* k = c.composant(id)) {
            k->hauteur = v;
            k->provenanceHauteur = ProvenanceHauteur::Saisie;
          }
      }, QStringLiteral("Hauteur de %1 composants").arg(ids.size()));
    });
    lier(h, [this, h] {
      std::set<double> v;
      for (const ComposantElec* k : choisis()) v.insert(k->hauteur);
      h->setValue(v.size() == 1 ? *v.begin() : 0);
    });
    fl->addRow(QStringLiteral("Hauteur"), h);
    // Cases à trois états
    struct Case {
      const char* objet;
      QString libelle;
      bool ComposantElec::*champ;
    };
    for (const Case& k : {Case{"nePasMonterPlusieurs", QStringLiteral("Ne pas monter"), &ComposantElec::nePasMonter}, Case{"horsCartePlusieurs", QStringLiteral("Hors carte"), &ComposantElec::horsCarte},
                          Case{"horsNomenclaturePlusieurs", QStringLiteral("Hors nomenclature"), &ComposantElec::horsNomenclature}}) {
      auto* ck = new QCheckBox(k.libelle);
      ck->setObjectName(QString::fromLatin1(k.objet));
      ck->setTristate(true);
      const auto champ = k.champ;
      connect(ck, &QCheckBox::clicked, this, [this, ck, champ] {
        // Un clic sur l'état mêlé coche tout ; ensuite la case bascule
        const bool on = ck->checkState() != Qt::Unchecked;
        ck->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        const auto ids = composants();
        m_atelier.modifierConception([&](ConceptionElec& c) {
          for (const auto& id : ids)
            if (ComposantElec* y = c.composant(id)) y->*champ = on;
        });
      });
      lier(ck, [this, ck, champ] {
        int oui = 0, n = 0;
        for (const ComposantElec* x : choisis()) {
          ++n;
          oui += x->*champ;
        }
        ck->setCheckState(oui == 0 ? Qt::Unchecked : oui == n ? Qt::Checked : Qt::PartiallyChecked);
      });
      fl->addRow(ck);
    }
    construireRegle();
    construirePinceau();
    construireActions();
    colonne()->addStretch(1);
  }
  void montrer() override {
    PageSchema::montrer();
    m_communs->setVisible(!composants().empty());
  }

 private:
  std::vector<std::string> composants() const {
    std::vector<std::string> r;
    const ConceptionElec* c = conception();
    if (!c) return r;
    for (const auto& id : m_atelier.selection()->composants())
      if (c->composant(id)) r.push_back(id);
    return r;
  }
  std::vector<const ComposantElec*> choisis() const {
    std::vector<const ComposantElec*> r;
    if (const ConceptionElec* c = conception())
      for (const auto& id : composants())
        if (const ComposantElec* k = c->composant(id)) r.push_back(k);
    return r;
  }
  // « 8 composants · 6 condensateurs, 2 résistances », puis les autres objets
  QString resume() const {
    const ConceptionElec* c = conception();
    if (!c) return {};
    std::map<std::string, int> familles;
    for (const ComposantElec* k : choisis()) {
      const FamilleValeur f = familleDe(*c, *k);
      const DefSymbole* d = c->symbole(k->symbole);
      ++familles[f != FamilleValeur::Aucune ? std::string(nomFamille(f)) : d ? d->nom : std::string("composant")];
    }
    QStringList parties;
    for (const auto& [nom, n] : familles) {
      QString t = q(nom);
      if (n > 1 && !t.endsWith(QLatin1Char('s'))) {
        QStringList mots = t.split(QLatin1Char(' '));
        mots.front() += QLatin1Char('s');
        t = mots.join(QLatin1Char(' '));
      }
      parties << QStringLiteral("%1 %2").arg(n).arg(t);
    }
    std::map<std::string, int> autres;
    for (const auto& r : m_atelier.selection()->objets()) {
      using G = SelectionElectronique::Genre;
      switch (r.genre) {
        case G::Fil: ++autres["fil(s)"]; break;
        case G::Etiquette: ++autres["étiquette(s)"]; break;
        case G::TexteSchema:
        case G::TexteCarte: ++autres["texte(s)"]; break;
        case G::Piste: ++autres["piste(s)"]; break;
        case G::Via: ++autres["via(s)"]; break;
        case G::Zone: ++autres["zone(s)"]; break;
        default: break;
      }
    }
    QStringList reste;
    for (const auto& [nom, n] : autres) reste << QStringLiteral("%1 %2").arg(n).arg(q(nom));
    const std::size_t n = choisis().size();
    QString t = QStringLiteral("<span style='font-size:15px; font-weight:700'>%1</span>").arg(n == 1 ? QStringLiteral("1 composant") : QStringLiteral("%1 composants").arg(n));
    if (!parties.isEmpty()) t += QStringLiteral(" <span style='color:%1'>· %2</span>").arg(teintes::secondaire(), parties.join(QStringLiteral(", ")));
    if (!reste.isEmpty()) t += QStringLiteral("<br><span style='color:%1'>et %2</span>").arg(teintes::secondaire(), reste.join(QStringLiteral(", ")));
    return t;
  }

  // Règle appliquée objet par objet (3.15) : sur un champ, remplacer « de » par « à », ajouter, ou une formule en x
  void construireRegle() {
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Modifier objet par objet"), fl);
    auto* champ = new QComboBox;
    champ->setObjectName(QStringLiteral("regleChamp"));
    champ->addItem(QStringLiteral("Valeur"), QStringLiteral("valeur"));
    champ->addItem(QStringLiteral("Référence"), QStringLiteral("reference"));
    champ->addItem(QStringLiteral("Fabricant"), QStringLiteral("fabricant"));
    champ->addItem(QStringLiteral("Réf. fabricant"), QStringLiteral("mpn"));
    champ->addItem(QStringLiteral("Description"), QStringLiteral("description"));
    auto* genre = new QComboBox;
    genre->setObjectName(QStringLiteral("regleGenre"));
    genre->addItem(QStringLiteral("Ajouter à la fin"), static_cast<int>(RegleTexte::Genre::Suffixe));
    genre->addItem(QStringLiteral("Ajouter au début"), static_cast<int>(RegleTexte::Genre::Prefixe));
    genre->addItem(QStringLiteral("Remplacer « de » par « à »"), static_cast<int>(RegleTexte::Genre::Remplacer));
    genre->addItem(QStringLiteral("Formule en x (valeur actuelle)"), static_cast<int>(RegleTexte::Genre::Formule));
    auto* de = new QLineEdit;
    de->setObjectName(QStringLiteral("regleDe"));
    de->setPlaceholderText(QStringLiteral("de"));
    auto* a = new QLineEdit;
    a->setObjectName(QStringLiteral("regleA"));
    a->setPlaceholderText(QStringLiteral("à, texte ajouté, ou formule (2*x)"));
    auto* bilan = inspecteur::texte();
    bilan->setObjectName(QStringLiteral("regleBilan"));
    bilan->setWordWrap(true);
    auto* aller = new QPushButton(QStringLiteral("Appliquer à tous"));
    aller->setObjectName(QStringLiteral("regleAppliquer"));
    connect(genre, &QComboBox::currentIndexChanged, this, [genre, de, fl] { fl->setRowVisible(de, genre->currentData().toInt() == static_cast<int>(RegleTexte::Genre::Remplacer)); });
    connect(aller, &QPushButton::clicked, this, [this, champ, genre, de, a, bilan] {
      RegleTexte r;
      r.genre = static_cast<RegleTexte::Genre>(genre->currentData().toInt());
      r.de = de->text().toStdString();
      r.a = a->text().toStdString();
      const std::string quoi = champ->currentData().toString().toStdString();
      const ConceptionElec* c0 = conception();
      if (!c0) return;
      // D'abord tout lire : une seule erreur et rien ne change
      std::map<std::string, std::string> nouveaux;
      QStringList erreurs;
      for (const ComposantElec* k : choisis()) {
        const std::string avant = quoi == "valeur" ? k->valeur : quoi == "reference" ? k->reference : quoi == "fabricant" ? k->fabricant : quoi == "mpn" ? k->mpn : k->description;
        std::string err;
        const auto apres = transformerTexte(avant, r, quoi == "valeur" ? familleDe(*c0, *k) : FamilleValeur::Aucune, &err);
        if (!apres) erreurs << QStringLiteral("%1 : %2").arg(q(k->reference), q(err));
        else if (*apres != avant) nouveaux[k->uuid] = *apres;
      }
      if (!erreurs.isEmpty()) {
        bilan->setText(QStringLiteral("<span style='color:%1'>Rien n’est changé : %2</span>").arg(teintes::erreur(), erreurs.join(QStringLiteral(" ; ")).toHtmlEscaped()));
        return;
      }
      if (nouveaux.empty()) {
        bilan->setText(QStringLiteral("<span style='color:%1'>Rien à changer.</span>").arg(teintes::secondaire()));
        return;
      }
      m_atelier.modifierConception([&](ConceptionElec& c) {
        for (const auto& [id, t] : nouveaux)
          if (ComposantElec* k = c.composant(id)) {
            if (quoi == "valeur") {
              k->valeur = t;
              k->formule.clear();
            } else if (quoi == "reference") k->reference = t;
            else if (quoi == "fabricant") k->fabricant = t;
            else if (quoi == "mpn") k->mpn = t;
            else k->description = t;
          }
      }, QStringLiteral("%1 de %2 composants modifiés objet par objet").arg(champ->currentText()).arg(nouveaux.size()));
      bilan->setText(QStringLiteral("<span style='color:%1'>%2 composant(s) modifié(s), en un pas d’annulation.</span>").arg(teintes::succes()).arg(nouveaux.size()));
    });
    action(aller);
    fl->addRow(QStringLiteral("Champ"), champ);
    fl->addRow(QStringLiteral("Règle"), genre);
    fl->addRow(QStringLiteral("De"), de);
    fl->addRow(QStringLiteral("À"), a);
    fl->setRowVisible(de, false);
    fl->addRow(aller);
    fl->addRow(bilan);
  }

  // Pinceau de propriétés (SAI-41) : pris sur le premier composant choisi, appliqué à chaque composant choisi ensuite
  void construirePinceau() {
    auto* b = new QPushButton(QStringLiteral("Pinceau : prendre sur le premier composant…"));
    b->setObjectName(QStringLiteral("pinceau"));
    b->setToolTip(QStringLiteral("Valeur, paramètres et exclusions du premier composant choisi, recopiés sur chaque composant choisi ensuite, jusqu’à « Terminer » ou Échap"));
    connect(b, &QPushButton::clicked, this, [this] {
      const auto c = choisis();
      if (!c.empty()) m_atelier.armerPinceau(c.front()->uuid);
    });
    action(b);
    colonne()->addWidget(b);
  }

  void construireActions() {
    auto* ligne = new QWidget;
    auto* hl = new QHBoxLayout(ligne);
    hl->setContentsMargins(0, 0, 0, 0);
    auto* renum = new QPushButton(QStringLiteral("Renuméroter la sélection"));
    renum->setObjectName(QStringLiteral("renumeroterSelection"));
    connect(renum, &QPushButton::clicked, this, [this] { m_atelier.renumeroterSelection(); });
    action(renum);
    auto* garder = new QPushButton(QStringLiteral("Ne garder que les composants"));
    garder->setObjectName(QStringLiteral("garderComposants"));
    connect(garder, &QPushButton::clicked, this, [this] {
      std::vector<SelectionElectronique::Ref> refs;
      for (const auto& r : m_atelier.selection()->objets())
        if ((r.genre == SelectionElectronique::Genre::Symbole && !r.composant.empty()) || r.genre == SelectionElectronique::Genre::Empreinte) refs.push_back(r);
      m_atelier.selection()->choisir(refs);
    });
    auto* remplacer = new QPushButton(QStringLiteral("Remplacer…"));
    remplacer->setObjectName(QStringLiteral("remplacerSelection"));
    connect(remplacer, &QPushButton::clicked, this, [this] { m_atelier.remplacerComposants(composants()); });
    action(remplacer);
    hl->addWidget(renum);
    hl->addWidget(remplacer);
    hl->addWidget(garder);
    colonne()->addWidget(ligne);
    suivre([this, garder] {
      bool autres = false;
      for (const auto& r : m_atelier.selection()->objets())
        autres |= !((r.genre == SelectionElectronique::Genre::Symbole && !r.composant.empty()) || r.genre == SelectionElectronique::Genre::Empreinte);
      garder->setVisible(autres && !composants().empty());
    });
  }

  bool m_circuit = false;
  QGroupBox* m_communs = nullptr;
};

// ---------- Alimentation, drapeau ----------
class PageAlimentation : public PageSchema {
 public:
  explicit PageAlimentation(AtelierElectronique& a) : PageSchema(a) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] {
      const DefSymbole* d = definition();
      if (d)
        titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>%1</span><br><span style='color:%3'>%2</span>")
                           .arg(q(d->nom).toHtmlEscaped(), q(d->description).toHtmlEscaped(), teintes::secondaire()));
    });
    QFormLayout* fl = nullptr;
    auto* g = groupe(QStringLiteral("Alimentation"), fl);
    auto* nom = new ChampTexteLie([this]() -> QString { const SymbolePlace* s = symbole(); return s ? q(s->nomAlim) : QString(); },
                                  [this](const QString& t, std::uint64_t saisie) {
                                    const std::string v = t.trimmed().toStdString(), id = m_objet;
                                    modifierFeuille([&](Feuille& f) { for (auto& y : f.symboles) if (y.uuid == id) y.nomAlim = v; }, saisie);
                                  },
                                  false);
    nom->setObjectName(QStringLiteral("champ:nomAlim"));
    nom->setValidation([](const QString& t) { return t.trimmed().isEmpty() ? QStringLiteral("Le nom du réseau ne peut pas être vide") : QString(); });
    fl->addRow(QStringLiteral("Nom du réseau"), nom);
    lier(nom, [nom] { nom->relire(); });
    suivre([this, g] {
      const DefSymbole* d = definition();
      g->setVisible(d && d->alimentation);
    });
    colonne()->addStretch(1);
  }

 private:
  const SymbolePlace* symbole() const {
    if (const Feuille* f = feuille())
      for (const auto& s : f->symboles)
        if (s.uuid == m_objet) return &s;
    return nullptr;
  }
  const DefSymbole* definition() const {
    const SymbolePlace* s = symbole();
    return s && conception() ? conception()->symbole(s->symbole) : nullptr;
  }
};

// ---------- Étiquette ----------
class PageEtiquette : public PageSchema {
 public:
  explicit PageEtiquette(AtelierElectronique& a) : PageSchema(a) {
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Étiquette"), fl);
    auto* nom = new ChampTexteLie([this]() -> QString { const Etiquette* e = etiquette(); return e ? q(e->texte) : QString(); },
                                  [this](const QString& t, std::uint64_t saisie) {
                                    const std::string v = t.trimmed().toStdString(), id = m_objet;
                                    modifierFeuille([&](Feuille& f) { for (auto& y : f.etiquettes) if (y.uuid == id) y.texte = v; }, saisie);
                                  },
                                  false);
    nom->setObjectName(QStringLiteral("champ:etiquette"));
    nom->setValidation([](const QString& t) { return t.trimmed().isEmpty() ? QStringLiteral("Le nom du réseau ne peut pas être vide") : QString(); });
    fl->addRow(QStringLiteral("Nom du réseau"), nom);
    lier(nom, [nom] { nom->relire(); });
    auto* glob = new QCheckBox(QStringLiteral("Globale (toutes les feuilles)"));
    connect(glob, &QCheckBox::toggled, this, [this](bool on) {
      const std::string id = m_objet;
      modifierFeuille([&](Feuille& f) { for (auto& y : f.etiquettes) if (y.uuid == id) y.type = on ? TypeEtiquette::Globale : TypeEtiquette::Locale; });
    });
    lier(glob, [this, glob] {
      if (const Etiquette* e = etiquette()) glob->setChecked(e->type == TypeEtiquette::Globale);
    });
    fl->addRow(glob);
    colonne()->addStretch(1);
  }

 private:
  const Etiquette* etiquette() const {
    if (const Feuille* f = feuille())
      for (const auto& e : f->etiquettes)
        if (e.uuid == m_objet) return &e;
    return nullptr;
  }
};

// ---------- Texte de la feuille : contenu, taille et orientation (EL-21) ----------
class PageTexte : public PageSchema {
 public:
  explicit PageTexte(AtelierElectronique& a) : PageSchema(a) {
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Texte"), fl);
    auto* contenu = new ChampTexteLie([this]() -> QString { const TexteSchema* t = texteChoisi(); return t ? q(t->texte) : QString(); },
                                      [this](const QString& t, std::uint64_t saisie) {
                                        const std::string v = t.toStdString(), id = m_objet;
                                        modifierFeuille([&](Feuille& f) { for (auto& y : f.textes) if (y.uuid == id) y.texte = v; }, saisie);
                                      });
    contenu->setObjectName(QStringLiteral("champ:texte"));
    contenu->setValidation([](const QString& t) { return t.trimmed().isEmpty() ? QStringLiteral("Un texte vide ne se verrait plus : supprime-le plutôt") : QString(); });
    fl->addRow(QStringLiteral("Contenu"), contenu);
    lier(contenu, [contenu] { contenu->relire(); });
    auto* taille = new QDoubleSpinBox;
    taille->setRange(0.5, 20);
    taille->setDecimals(2);
    taille->setSuffix(QStringLiteral(" mm"));
    connect(taille, &QDoubleSpinBox::editingFinished, this, [this, taille] {
      const Nm v = static_cast<Nm>(std::llround(taille->value() * 1e6));
      const TexteSchema* t = texteChoisi();
      if (!t || t->taille == v) return;
      const std::string id = m_objet;
      modifierFeuille([&](Feuille& f) { for (auto& y : f.textes) if (y.uuid == id) y.taille = v; });
    });
    lier(taille, [this, taille] {
      if (const TexteSchema* t = texteChoisi()) taille->setValue(mm(t->taille));
    });
    fl->addRow(QStringLiteral("Hauteur"), taille);
    auto* rotation = new QComboBox;
    rotation->addItems({QStringLiteral("0°"), QStringLiteral("90°"), QStringLiteral("180°"), QStringLiteral("270°")});
    connect(rotation, &QComboBox::activated, this, [this](int r) {
      const std::string id = m_objet;
      modifierFeuille([&](Feuille& f) { for (auto& y : f.textes) if (y.uuid == id) y.rotation = r; });
    });
    lier(rotation, [this, rotation] {
      if (const TexteSchema* t = texteChoisi()) rotation->setCurrentIndex(((t->rotation % 4) + 4) % 4);
    });
    fl->addRow(QStringLiteral("Orientation"), rotation);
    colonne()->addStretch(1);
  }

 private:
  const TexteSchema* texteChoisi() const {
    if (const Feuille* f = feuille())
      for (const auto& t : f->textes)
        if (t.uuid == m_objet) return &t;
    return nullptr;
  }
};

// ---------- Fil ----------
class PageFil : public PageSchema {
 public:
  explicit PageFil(AtelierElectronique& a) : PageSchema(a) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] { titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>Fil</span><br>Réseau <b>%1</b>").arg(reseau().toHtmlEscaped())); });
    auto* b = new QPushButton(icone(QStringLiteral("afficher")), QStringLiteral("Surligner ce réseau"));
    b->setObjectName(QStringLiteral("surlignerReseau"));
    connect(b, &QPushButton::clicked, this, [this] { m_atelier.surligner(reseau()); });
    colonne()->addWidget(b);
    colonne()->addStretch(1);
  }

 private:
  QString reseau() const {
    const Connectivite& k = m_atelier.calculs().k;
    if (const auto it = k.parFil.find(m_objet); it != k.parFil.end()) return q(k.reseaux[static_cast<std::size_t>(it->second)].nom);
    return QStringLiteral("—");
  }
};

// ---------- Feuille (rien de choisi) : nom, format, feuilles, cartouche, vérification électrique ----------
class PageFeuille : public PageSchema {
 public:
  explicit PageFeuille(AtelierElectronique& a) : PageSchema(a) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] {
      const ConceptionElec* c = conception();
      if (c)
        titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>%1</span><br><span style='color:%4'>Feuille %2 / %3</span>")
                           .arg(q(c->nom).toHtmlEscaped())
                           .arg(m_feuille + 1)
                           .arg(c->feuilles.size())
                           .arg(teintes::secondaire()));
    });
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Feuille"), fl);
    auto* nom = new ChampTexteLie([this]() -> QString { const Feuille* f = feuille(); return f ? q(f->nom) : QString(); },
                                  [this](const QString& t, std::uint64_t saisie) {
                                    const std::string v = t.trimmed().toStdString();
                                    modifierFeuille([&](Feuille& f) { f.nom = v; }, saisie);
                                  });
    nom->setObjectName(QStringLiteral("champ:nomFeuille"));
    nom->setValidation([](const QString& t) { return t.trimmed().isEmpty() ? QStringLiteral("Une feuille a besoin d’un nom") : QString(); });
    fl->addRow(QStringLiteral("Nom"), nom);
    lier(nom, [nom] { nom->relire(); });
    auto* format = new QComboBox;
    format->addItems({QStringLiteral("A4"), QStringLiteral("A3"), QStringLiteral("A2")});
    connect(format, &QComboBox::activated, this, [this, format](int) {
      const std::string t = format->currentText().toStdString();
      modifierFeuille([&](Feuille& f) { f.format = t; });
      m_atelier.vueSchema()->recadrer();
    });
    lier(format, [this, format] {
      if (const Feuille* f = feuille()) format->setCurrentText(q(f->format));
    });
    fl->addRow(QStringLiteral("Format"), format);

    // Feuilles de la conception : changer de feuille, réordonner, supprimer
    auto* gf = new QGroupBox(QStringLiteral("Feuilles"));
    auto* lf = new QVBoxLayout(gf);
    auto* liste = new QListWidget;
    liste->setObjectName(QStringLiteral("listeFeuilles"));
    liste->setMaximumHeight(120);
    connect(liste, &QListWidget::currentRowChanged, this, [this](int r) {
      if (r >= 0 && r != m_atelier.vueSchema()->feuille()) {
        m_atelier.vueSchema()->setConception(m_atelier.conception(), r);
        m_atelier.montrerInspecteur();
      }
    });
    lier(liste, [this, liste] {
      const ConceptionElec* c = conception();
      if (!c) return;
      QStringList noms;
      for (const auto& x : c->feuilles) noms << q(x.nom);
      QStringList actuels;
      for (int i = 0; i < liste->count(); ++i) actuels << liste->item(i)->text();
      if (actuels != noms) {
        liste->clear();
        liste->addItems(noms);
      }
      liste->setCurrentRow(m_feuille);
    }, false);
    lf->addWidget(liste);
    auto* boutons = new QWidget;
    auto* hb = new QHBoxLayout(boutons);
    hb->setContentsMargins(0, 0, 0, 0);
    auto bouton = [this, hb](const QString& t, const QString& bulle, std::function<void()> fn) {
      auto* b = new QToolButton;
      b->setText(t);
      b->setToolTip(bulle);
      connect(b, &QToolButton::clicked, this, [fn] { fn(); });
      hb->addWidget(b);
      action(b);
    };
    bouton(QStringLiteral("↑"), QStringLiteral("Monter la feuille"), [this] { m_atelier.deplacerFeuille(m_feuille, -1); });
    bouton(QStringLiteral("↓"), QStringLiteral("Descendre la feuille"), [this] { m_atelier.deplacerFeuille(m_feuille, +1); });
    bouton(QStringLiteral("Supprimer…"), QStringLiteral("Supprimer cette feuille et ce qu’elle contient"), [this] { m_atelier.supprimerFeuille(m_feuille); });
    hb->addStretch(1);
    lf->addWidget(boutons);
    colonne()->addWidget(gf);

    // Cartouche
    QFormLayout* fc = nullptr;
    groupe(QStringLiteral("Cartouche"), fc);
    using Champ = std::string Cartouche::*;
    for (const auto& [libelle, champ] : std::initializer_list<std::pair<QString, Champ>>{{QStringLiteral("Titre"), &Cartouche::titre},
                                                                                          {QStringLiteral("Société"), &Cartouche::societe},
                                                                                          {QStringLiteral("Auteur"), &Cartouche::auteur},
                                                                                          {QStringLiteral("Révision"), &Cartouche::revision},
                                                                                          {QStringLiteral("Date"), &Cartouche::date},
                                                                                          {QStringLiteral("Commentaire"), &Cartouche::commentaire}}) {
      const Champ m = champ;
      auto* e = new ChampTexteLie([this, m]() -> QString { const ConceptionElec* c = conception(); return c ? q(c->cartouche.*m) : QString(); },
                                  [this, m](const QString& t, std::uint64_t saisie) {
                                    const std::string v = t.trimmed().toStdString();
                                    m_atelier.modifierConception([&](ConceptionElec& c) { c.cartouche.*m = v; }, {}, QStringLiteral("saisie:%1").arg(saisie));
                                  });
      fc->addRow(libelle, e);
      lier(e, [e] { e->relire(); });
    }

    // Vérification électrique : la liste se refait quand l'ERC change, le reste de la page ne bouge pas
    auto* ge = new QGroupBox(QStringLiteral("Vérification électrique"));
    auto* le = new QVBoxLayout(ge);
    auto* bilan = inspecteur::texte();
    le->addWidget(bilan);
    auto* liste2 = new SectionRefaite([this](QVBoxLayout* col) { inspecteur::listeProblemes(col, m_atelier.erc(), true, m_atelier); });
    le->addWidget(liste2);
    colonne()->addWidget(ge);
    suivre([this, bilan, liste2] {
      const auto& erc = m_atelier.erc();
      const int erreurs = static_cast<int>(std::count_if(erc.begin(), erc.end(), [](const Violation& v) { return v.gravite == Gravite::Erreur; }));
      bilan->setText(erc.empty() ? QStringLiteral("<span style='color:%1'><b>✓ Rien à signaler</b></span>").arg(teintes::succes())
                                 : QStringLiteral("<b>%1 erreur(s), %2 avertissement(s)</b> — clique pour y aller").arg(erreurs).arg(erc.size() - static_cast<std::size_t>(erreurs)));
      liste2->refaire(inspecteur::signature(erc));
    });
    colonne()->addStretch(1);
  }
};
}  // namespace

PageInspecteur* pageComposantCircuit(AtelierElectronique& a) { return new PageComposant(a, true); }
PageInspecteur* pagePlusieursComposants(AtelierElectronique& a, bool circuit) { return new PagePlusieurs(a, circuit); }

PageInspecteur* pageSchema(const QString& cle, AtelierElectronique& a) {
  if (cle == QLatin1String("schema.composant")) return new PageComposant(a, false);
  if (cle == QLatin1String("schema.plusieurs")) return new PagePlusieurs(a, false);
  if (cle == QLatin1String("schema.alimentation")) return new PageAlimentation(a);
  if (cle == QLatin1String("schema.etiquette")) return new PageEtiquette(a);
  if (cle == QLatin1String("schema.texte")) return new PageTexte(a);
  if (cle == QLatin1String("schema.fil")) return new PageFil(a);
  return new PageFeuille(a);
}

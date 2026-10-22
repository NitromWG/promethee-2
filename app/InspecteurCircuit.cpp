// Prométhée : pages de l'inspecteur pour le circuit (lot L1, tâche 1.9) : plusieurs objets, placement d'une
// empreinte, piste, via, zone, texte de carte, carte ; et les pages de la 3D et de la fabrication. Chaque page crée ses widgets une fois ; « relire » les remplit depuis le modèle, sans toucher au champ
// qui a le focus. Voir InspecteurElectronique.hpp.
// Licence GPL-3.0-only.
#include <algorithm>
#include <memory>
#include <set>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "ChampValeur.hpp"
#include "Document.hpp"
#include "Icones.hpp"
#include "InspecteurElectronique.hpp"
#include "Theme.hpp"
#include "VueSchema.hpp"
#include "promethee/circuit.hpp"
#include "promethee/electronique.hpp"
#include "promethee/synchronisation.hpp"
#include "promethee/unites.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& t) { return t.trimmed().toStdString(); }

// Une page du circuit : l'objet choisi, les unités de la carte, des champs de longueur liés
class PageCircuit : public PageInspecteur {
 public:
  using PageInspecteur::PageInspecteur;
  void montrer() override {
    m_choisi = m_atelier.vueCircuit()->choisi();
    PageInspecteur::montrer();
  }

 protected:
  const ConceptionElec* conception() const { return m_atelier.conceptionCourante(); }
  prom::Unites unites() const {
    prom::Unites u;
    if (const ConceptionElec* c = conception()) u.longueur = prom::uniteLongueurDepuis(c->circuit.affichage.unite);  // mm, mil ou pouce (EL-37)
    return u;
  }
  void modifier(const std::function<void(ConceptionElec&)>& f) { m_atelier.modifierConception(f); }
  QGroupBox* groupe(const QString& titre, QFormLayout*& fl) {
    auto* g = new QGroupBox(titre);
    fl = new QFormLayout(g);
    fl->setRowWrapPolicy(QFormLayout::WrapLongRows);
    colonne()->addWidget(g);
    return g;
  }
  // Longueur ou angle lié : relu dans les unités de la carte, écrit à la validation du champ
  ChampValeur* valeur(QFormLayout* fl, const QString& libelle, bool angle, double mini, double maxi, std::function<double()> lire, std::function<void(ConceptionElec&, double)> ecrire) {
    auto* ch = new ChampValeur(angle, unites());
    ch->setBornes(mini, maxi);
    connect(ch, &ChampValeur::valeurChangee, this, [this, ecrire](double x) { modifier([&](ConceptionElec& c) { ecrire(c, x); }); });
    lier(ch, [this, ch, lire] {
      const ConceptionElec* c = conception();
      if (!c) return;
      if (ch->property("unite").toString().toStdString() != c->circuit.affichage.unite) {
        ch->setUnites(unites());
        ch->setProperty("unite", q(c->circuit.affichage.unite));
      }
      ch->setValeur(lire());
    });
    fl->addRow(libelle, ch);
    return ch;
  }
  // Liste des couches de cuivre de l'empilage, refaite quand il change
  void couchesCuivreDans(QComboBox* liste) const {
    const ConceptionElec* c = conception();
    if (!c) return;
    const int n = c->circuit.empilage.couchesCuivre;
    if (liste->property("couches").toInt() == n) return;
    liste->clear();
    for (Couche x : couchesCuivre(n)) liste->addItem(QString::fromUtf8(nomCouche(x)), static_cast<int>(x));
    liste->setProperty("couches", n);
  }
  // Réseaux de la carte (au moins une broche), refaits quand ils changent
  void reseauxDans(QComboBox* liste, const std::string& actuel) const {
    QStringList noms;
    for (const auto& r : m_atelier.calculs().k.reseaux)
      if (!r.broches.empty() && !r.nom.empty() && !noms.contains(q(r.nom))) noms << q(r.nom);
    if (!actuel.empty() && !noms.contains(q(actuel))) noms << q(actuel);
    if (liste->property("noms").toStringList() != noms) {
      liste->clear();
      liste->addItem(QStringLiteral("— aucun —"), QString());
      for (const QString& n : noms) liste->addItem(n, n);
      liste->setProperty("noms", noms);
    }
    liste->setCurrentIndex(std::max(0, liste->findData(q(actuel))));
  }
  // Réseau renommé depuis le circuit (3.18, PLA-16) : son étiquette au schéma change dans le même pas, ou une étiquette
  // y est posée s'il n'avait pas de nom ; le cuivre suit
  void renommage(QFormLayout* fl, std::function<std::string()> reseau) {
    auto* nom = new QLineEdit;
    nom->setObjectName(QStringLiteral("renommerReseauCircuit"));
    nom->setPlaceholderText(QStringLiteral("nouveau nom du réseau"));
    nom->setToolTip(QStringLiteral("Le réseau prend ce nom au circuit et au schéma, en un pas d’annulation"));
    connect(nom, &QLineEdit::editingFinished, this, [this, nom, reseau] {
      const std::string ancien = reseau(), nouveau = nom->text().trimmed().toStdString();
      if (ancien.empty() || nouveau.empty() || ancien == nouveau || !nom->isModified()) return;
      nom->setModified(false);
      m_atelier.modifierConception([&](ConceptionElec& c) { renommerReseauDepuisCircuit(c, ancien, nouveau); },
                                   QStringLiteral("Réseau renommé : %1 → %2").arg(q(ancien), q(nouveau)));
    });
    lier(nom, [nom, reseau] {
      const QString r = q(reseau());
      if (nom->text() != r) nom->setText(r);
    });
    fl->addRow(QStringLiteral("Nom du réseau"), nom);
  }
  std::string m_choisi;
};

// ---------- Plusieurs objets choisis (EL-43) : bilan, largeur commune des pistes, déplacement, suppression ----------
class PagePlusieurs : public PageCircuit {
 public:
  explicit PagePlusieurs(AtelierElectronique& a) : PageCircuit(a) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] {
      int empreintes = 0, pistes = 0, vias = 0, zones = 0;
      for (const auto& o : m_atelier.vueCircuit()->selection()) {
        empreintes += o.genre == VueCircuit::Genre::Empreinte;
        pistes += o.genre == VueCircuit::Genre::Piste;
        vias += o.genre == VueCircuit::Genre::Via;
        zones += o.genre == VueCircuit::Genre::Zone;
      }
      titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>%1 objets choisis</span><br><span style='color:%6'>%2 empreinte(s) · %3 piste(s) · %4 via(s) · %5 zone(s)</span>")
                         .arg(m_atelier.vueCircuit()->selection().size())
                         .arg(empreintes)
                         .arg(pistes)
                         .arg(vias)
                         .arg(zones)
                         .arg(teintes::secondaire()));
    });
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Modifier ensemble"), fl);
    auto* largeur = valeur(fl, QStringLiteral("Largeur des pistes"), false, 0.05, 20, [this] {
      Nm l = 0;
      if (const ConceptionElec* c = conception())
        for (const auto& t : c->circuit.pistes)
          if (m_atelier.vueCircuit()->estChoisi(VueCircuit::Genre::Piste, t.uuid)) l = std::max(l, t.largeur);
      return mm(l);
    }, [this](ConceptionElec& c, double v) {
      for (auto& y : c.circuit.pistes)
        if (m_atelier.vueCircuit()->estChoisi(VueCircuit::Genre::Piste, y.uuid)) y.largeur = nm(v);
    });
    suivre([this, fl, largeur] {
      bool pistes = false;
      for (const auto& o : m_atelier.vueCircuit()->selection()) pistes |= o.genre == VueCircuit::Genre::Piste;
      fl->setRowVisible(largeur, pistes);
    });
    auto* deplacer = new QWidget;
    auto* hd = new QHBoxLayout(deplacer);
    hd->setContentsMargins(0, 0, 0, 0);
    auto* dx = new ChampValeur(false, unites());
    auto* dy = new ChampValeur(false, unites());
    dx->setValeur(0);
    dy->setValeur(0);
    auto* go = new QPushButton(QStringLiteral("Déplacer"));
    connect(go, &QPushButton::clicked, this, [this, dx, dy] { m_atelier.vueCircuit()->deplacerSelection({nm(dx->valeur()), nm(dy->valeur())}); });
    action(go);
    hd->addWidget(dx);
    hd->addWidget(dy);
    hd->addWidget(go);
    fl->addRow(QStringLiteral("Δx, Δy"), deplacer);
    auto* sup = new QPushButton(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer le cuivre choisi"));
    connect(sup, &QPushButton::clicked, this, [this] { m_atelier.vueCircuit()->supprimerChoix(); });
    action(sup);
    fl->addRow(sup);
    fl->addRow(inspecteur::texte(QStringLiteral("<span style='color:%1'>Glisse un des objets pour tout déplacer : les pistes accrochées s’étirent. "
                                                "R tourne et F retourne les empreintes choisies ; Ctrl+C, Ctrl+V et Ctrl+D copient le cuivre.</span>")
                                     .arg(teintes::secondaire())));
    colonne()->addStretch(1);
  }
};

// ---------- Placement d'une empreinte ----------
class PagePlacement : public PageCircuit {
 public:
  explicit PagePlacement(AtelierElectronique& a) : PageCircuit(a) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] {
      const ConceptionElec* c = conception();
      const EmpreintePlacee* e = empreinte();
      if (!c || !e) return;
      const ComposantElec* x = c->composant(e->composant);
      const DefEmpreinte* d = c->empreinte(e->empreinte);
      titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>%1</span> <span style='color:%4'>%2</span><br><span style='color:%4'>%3</span>")
                         .arg(x ? q(x->reference).toHtmlEscaped() : QStringLiteral("?"), x ? q(x->valeur).toHtmlEscaped() : QString(), d ? q(d->description).toHtmlEscaped() : q(e->empreinte).toHtmlEscaped(),
                              teintes::secondaire()));
    });
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Placement"), fl);
    valeur(fl, QStringLiteral("X"), false, -2000, 2000, [this] { const EmpreintePlacee* e = empreinte(); return e ? mm(e->position.x) : 0.0; },
           [this](ConceptionElec& c, double v) { for (auto& y : c.circuit.empreintes) if (y.composant == m_choisi) y.position.x = nm(v); });
    valeur(fl, QStringLiteral("Y"), false, -2000, 2000, [this] { const EmpreintePlacee* e = empreinte(); return e ? mm(e->position.y) : 0.0; },
           [this](ConceptionElec& c, double v) { for (auto& y : c.circuit.empreintes) if (y.composant == m_choisi) y.position.y = nm(v); });
    valeur(fl, QStringLiteral("Rotation"), true, -360, 360, [this] { const EmpreintePlacee* e = empreinte(); return e ? e->rotation : 0.0; },
           [this](ConceptionElec& c, double v) { for (auto& y : c.circuit.empreintes) if (y.composant == m_choisi) y.rotation = v; });
    auto* face = new QComboBox;
    face->addItems({QStringLiteral("Dessus"), QStringLiteral("Dessous")});
    connect(face, &QComboBox::activated, this, [this](int i) {
      modifier([&](ConceptionElec& c) { for (auto& y : c.circuit.empreintes) if (y.composant == m_choisi) y.dessous = i == 1; });
    });
    lier(face, [this, face] {
      if (const EmpreintePlacee* e = empreinte()) face->setCurrentIndex(e->dessous ? 1 : 0);
    });
    fl->addRow(QStringLiteral("Face"), face);
    auto* verrou = new QCheckBox(QStringLiteral("Verrouillée (ne bouge plus à la souris)"));
    connect(verrou, &QCheckBox::toggled, this, [this](bool on) {
      modifier([&](ConceptionElec& c) { for (auto& y : c.circuit.empreintes) if (y.composant == m_choisi) y.verrou = on; });
    });
    lier(verrou, [this, verrou] {
      if (const EmpreintePlacee* e = empreinte()) verrou->setChecked(e->verrou);
    });
    fl->addRow(verrou);
    colonne()->addStretch(1);
  }

 private:
  const EmpreintePlacee* empreinte() const {
    if (const ConceptionElec* c = conception())
      for (const auto& e : c->circuit.empreintes)
        if (e.composant == m_choisi) return &e;
    return nullptr;
  }
};

// ---------- Piste ----------
class PagePiste : public PageCircuit {
 public:
  explicit PagePiste(AtelierElectronique& a) : PageCircuit(a) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] {
      if (const Piste* t = piste())
        titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>Piste</span><br>Réseau <b>%1</b>").arg(q(t->reseau.empty() ? std::string("sans réseau") : t->reseau).toHtmlEscaped()));
    });
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Piste"), fl);
    valeur(fl, QStringLiteral("Largeur"), false, 0.05, 20, [this] { const Piste* t = piste(); return t ? mm(t->largeur) : 0.0; },
           [this](ConceptionElec& c, double v) { for (auto& y : c.circuit.pistes) if (y.uuid == m_choisi) y.largeur = nm(v); });
    auto* couche = new QComboBox;
    connect(couche, &QComboBox::activated, this, [this, couche](int) {
      const Couche x = static_cast<Couche>(couche->currentData().toInt());
      modifier([&](ConceptionElec& c) { for (auto& y : c.circuit.pistes) if (y.uuid == m_choisi) y.couche = x; });
    });
    lier(couche, [this, couche] {
      couchesCuivreDans(couche);
      if (const Piste* t = piste()) couche->setCurrentIndex(std::max(0, couche->findData(static_cast<int>(t->couche))));
    });
    fl->addRow(QStringLiteral("Couche"), couche);
    renommage(fl, [this] {
      const Piste* t = piste();
      return t ? t->reseau : std::string();
    });
    colonne()->addStretch(1);
  }

 private:
  const Piste* piste() const {
    if (const ConceptionElec* c = conception())
      for (const auto& t : c->circuit.pistes)
        if (t.uuid == m_choisi) return &t;
    return nullptr;
  }
};

// ---------- Via ----------
class PageVia : public PageCircuit {
 public:
  explicit PageVia(AtelierElectronique& a) : PageCircuit(a) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] {
      if (const Via* v = via())
        titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>Via</span><br>Réseau <b>%1</b>").arg(q(v->reseau.empty() ? std::string("sans réseau") : v->reseau).toHtmlEscaped()));
    });
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Via"), fl);
    valeur(fl, QStringLiteral("Diamètre"), false, 0.2, 10, [this] { const Via* v = via(); return v ? mm(v->diametre) : 0.0; },
           [this](ConceptionElec& c, double x) { for (auto& y : c.circuit.vias) if (y.uuid == m_choisi) y.diametre = nm(x); });
    valeur(fl, QStringLiteral("Perçage"), false, 0.1, 8, [this] { const Via* v = via(); return v ? mm(v->percage) : 0.0; },
           [this](ConceptionElec& c, double x) { for (auto& y : c.circuit.vias) if (y.uuid == m_choisi) y.percage = nm(x); });
    // Réseau de la via (EL-12) : couture d'un plan, dissipation sous un régulateur
    auto* reseau = new QComboBox;
    connect(reseau, &QComboBox::activated, this, [this, reseau](int) {
      const std::string r = reseau->currentData().toString().toStdString();
      modifier([&](ConceptionElec& c) {
        for (auto& y : c.circuit.vias)
          if (y.uuid == m_choisi) {
            // La via prend les dimensions de la classe de son nouveau réseau si elle avait celles de l'ancien
            if (y.diametre == viaDiametre(c.circuit.regles, y.reseau) && y.percage == viaPercage(c.circuit.regles, y.reseau)) {
              y.diametre = viaDiametre(c.circuit.regles, r);
              y.percage = viaPercage(c.circuit.regles, r);
            }
            y.reseau = r;
          }
      });
    });
    lier(reseau, [this, reseau] {
      if (const Via* v = via()) reseauxDans(reseau, v->reseau);
    });
    fl->addRow(QStringLiteral("Réseau"), reseau);
    auto* sans = inspecteur::texte();
    fl->addRow(sans);
    suivre([this, sans] {
      const Via* v = via();
      sans->setVisible(v && v->reseau.empty());
      sans->setText(QStringLiteral("<span style='color:%1'>Sans réseau, la via n’est reliée à rien : la DRC la signale.</span>").arg(teintes::alerte()));
    });
    colonne()->addStretch(1);
  }
  void resultats() override { relire(); }

 private:
  const Via* via() const {
    if (const ConceptionElec* c = conception())
      for (const auto& v : c->circuit.vias)
        if (v.uuid == m_choisi) return &v;
    return nullptr;
  }
};

// ---------- Zone de cuivre ----------
class PageZone : public PageCircuit {
 public:
  explicit PageZone(AtelierElectronique& a) : PageCircuit(a) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] {
      const Zone* z = zone();
      if (!z) return;
      const auto& zones = m_atelier.calculs().zones;
      titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>Zone de cuivre</span><br>%1 mm² de cuivre après remplissage")
                         .arg(QLocale().toString(zones.count(z->uuid) ? aireMm2(zones.at(z->uuid)) : 0.0, 'f', 1)));
    });
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Zone"), fl);
    auto* reseau = new QComboBox;
    connect(reseau, &QComboBox::activated, this, [this, reseau](int) {
      const std::string v = reseau->currentData().toString().toStdString();
      modifier([&](ConceptionElec& c) { for (auto& y : c.circuit.zones) if (y.uuid == m_choisi) y.reseau = v; });
    });
    lier(reseau, [this, reseau] {
      if (const Zone* z = zone()) reseauxDans(reseau, z->reseau);
    });
    fl->addRow(QStringLiteral("Réseau"), reseau);
    renommage(fl, [this] {
      const Zone* z = zone();
      return z ? z->reseau : std::string();
    });
    auto* couche = new QComboBox;
    connect(couche, &QComboBox::activated, this, [this, couche](int) {
      const Couche x = static_cast<Couche>(couche->currentData().toInt());
      modifier([&](ConceptionElec& c) { for (auto& y : c.circuit.zones) if (y.uuid == m_choisi) y.couche = x; });
    });
    lier(couche, [this, couche] {
      couchesCuivreDans(couche);
      if (const Zone* z = zone()) couche->setCurrentIndex(std::max(0, couche->findData(static_cast<int>(z->couche))));
    });
    fl->addRow(QStringLiteral("Couche"), couche);
    // Priorité : appliquée à la sortie du champ, pas à chaque chiffre (P16)
    auto* prio = new QSpinBox;
    prio->setObjectName(QStringLiteral("prioriteZone"));
    prio->setRange(0, 99);
    connect(prio, &QSpinBox::editingFinished, this, [this, prio] {
      const Zone* z = zone();
      if (!z || z->priorite == prio->value()) return;
      const int v = prio->value();
      modifier([&](ConceptionElec& c) { for (auto& y : c.circuit.zones) if (y.uuid == m_choisi) y.priorite = v; });
    });
    lier(prio, [this, prio] {
      if (const Zone* z = zone()) prio->setValue(z->priorite);
    });
    fl->addRow(QStringLiteral("Priorité"), prio);
    valeur(fl, QStringLiteral("Isolement (0 : règle)"), false, 0, 10, [this] { const Zone* z = zone(); return z ? mm(z->isolement) : 0.0; },
           [this](ConceptionElec& c, double v) { for (auto& y : c.circuit.zones) if (y.uuid == m_choisi) y.isolement = nm(v); });
    valeur(fl, QStringLiteral("Épaisseur minimale"), false, 0, 5, [this] { const Zone* z = zone(); return z ? mm(z->epaisseurMin) : 0.0; },
           [this](ConceptionElec& c, double v) { for (auto& y : c.circuit.zones) if (y.uuid == m_choisi) y.epaisseurMin = nm(v); });
    auto* th = new QCheckBox(QStringLiteral("Liaisons thermiques (rayons)"));
    connect(th, &QCheckBox::toggled, this, [this](bool on) { modifier([&](ConceptionElec& c) { for (auto& y : c.circuit.zones) if (y.uuid == m_choisi) y.thermique = on; }); });
    lier(th, [this, th] {
      if (const Zone* z = zone()) th->setChecked(z->thermique);
    });
    fl->addRow(th);
    // Rayons thermiques réglables (EL-20) : largeur de chaque rayon et écart entre la pastille et le plan
    auto* rayon = valeur(fl, QStringLiteral("Largeur des rayons"), false, 0.1, 5, [this] { const Zone* z = zone(); return z ? mm(z->largeurRayon) : 0.0; },
                         [this](ConceptionElec& c, double v) { for (auto& y : c.circuit.zones) if (y.uuid == m_choisi) y.largeurRayon = nm(v); });
    auto* ecart = valeur(fl, QStringLiteral("Écart thermique"), false, 0.1, 5, [this] { const Zone* z = zone(); return z ? mm(z->ecartThermique) : 0.0; },
                         [this](ConceptionElec& c, double v) { for (auto& y : c.circuit.zones) if (y.uuid == m_choisi) y.ecartThermique = nm(v); });
    auto* sommets = inspecteur::texte();
    fl->addRow(sommets);
    suivre([this, fl, rayon, ecart, sommets] {
      const Zone* z = zone();
      fl->setRowVisible(rayon, z && z->thermique);
      fl->setRowVisible(ecart, z && z->thermique);
      if (z)
        sommets->setText(QStringLiteral("<span style='color:%1'>%2 sommets : glisse une poignée pour la déplacer ; double-clic sur un sommet pour le retirer, sur un côté pour en ajouter un.</span>")
                             .arg(teintes::secondaire())
                             .arg(z->contour.size()));
    });
    colonne()->addStretch(1);
  }
  void resultats() override { relire(); }

 private:
  const Zone* zone() const {
    if (const ConceptionElec* c = conception())
      for (const auto& z : c->circuit.zones)
        if (z.uuid == m_choisi) return &z;
    return nullptr;
  }
};

// ---------- Texte sur la carte ----------
class PageTexteCarte : public PageCircuit {
 public:
  explicit PageTexteCarte(AtelierElectronique& a) : PageCircuit(a) {
    colonne()->addWidget(inspecteur::texte(QStringLiteral("<span style='font-size:15px; font-weight:700'>Texte sur la carte</span><br><span style='color:%1'>Écrit avec la police vectorielle des Gerber</span>")
                                              .arg(teintes::secondaire())));
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Texte"), fl);
    auto* contenu = new ChampTexteLie([this]() -> QString { const TexteCircuit* t = texte(); return t ? q(t->texte) : QString(); },
                                      [this](const QString& t, std::uint64_t saisie) {
                                        const std::string v = s(t);
                                        m_atelier.modifierConception([&](ConceptionElec& c) { for (auto& y : c.circuit.textes) if (y.uuid == m_choisi) y.texte = v; }, {},
                                                                     QStringLiteral("saisie:%1").arg(saisie));
                                      });
    contenu->setObjectName(QStringLiteral("champ:texteCarte"));
    contenu->setValidation([](const QString& t) { return t.trimmed().isEmpty() ? QStringLiteral("Un texte vide ne se verrait plus : supprime-le plutôt") : QString(); });
    lier(contenu, [contenu] { contenu->relire(); });
    fl->addRow(QStringLiteral("Contenu"), contenu);
    valeur(fl, QStringLiteral("Hauteur"), false, 0.3, 20, [this] { const TexteCircuit* t = texte(); return t ? mm(t->hauteur) : 0.0; },
           [this](ConceptionElec& c, double v) { for (auto& y : c.circuit.textes) if (y.uuid == m_choisi) y.hauteur = nm(v); });
    valeur(fl, QStringLiteral("Rotation"), true, -360, 360, [this] { const TexteCircuit* t = texte(); return t ? t->rotation : 0.0; },
           [this](ConceptionElec& c, double v) { for (auto& y : c.circuit.textes) if (y.uuid == m_choisi) y.rotation = v; });
    valeur(fl, QStringLiteral("X"), false, -2000, 2000, [this] { const TexteCircuit* t = texte(); return t ? mm(t->p.x) : 0.0; },
           [this](ConceptionElec& c, double v) { for (auto& y : c.circuit.textes) if (y.uuid == m_choisi) y.p.x = nm(v); });
    valeur(fl, QStringLiteral("Y"), false, -2000, 2000, [this] { const TexteCircuit* t = texte(); return t ? mm(t->p.y) : 0.0; },
           [this](ConceptionElec& c, double v) { for (auto& y : c.circuit.textes) if (y.uuid == m_choisi) y.p.y = nm(v); });
    auto* face = new QComboBox;
    face->addItem(QStringLiteral("Sérigraphie dessus"), static_cast<int>(Couche::SerigraphieDessus));
    face->addItem(QStringLiteral("Sérigraphie dessous (en miroir)"), static_cast<int>(Couche::SerigraphieDessous));
    connect(face, &QComboBox::activated, this, [this, face](int) {
      const Couche x = static_cast<Couche>(face->currentData().toInt());
      modifier([&](ConceptionElec& c) { for (auto& y : c.circuit.textes) if (y.uuid == m_choisi) y.couche = x; });
    });
    lier(face, [this, face] {
      if (const TexteCircuit* t = texte()) face->setCurrentIndex(std::max(0, face->findData(static_cast<int>(t->couche))));
    });
    fl->addRow(QStringLiteral("Face"), face);
    colonne()->addStretch(1);
  }

 private:
  const TexteCircuit* texte() const {
    if (const ConceptionElec* c = conception())
      for (const auto& t : c->circuit.textes)
        if (t.uuid == m_choisi) return &t;
    return nullptr;
  }
};

// ---------- Carte (rien de choisi) : contour, empilage, règles, classes, boîtier, DRC ----------
class PageCarte : public PageCircuit {
 public:
  explicit PageCarte(AtelierElectronique& a) : PageCircuit(a) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] {
      const ConceptionElec* c = conception();
      if (!c) return;
      const prom::ecad::Circuit& k = c->circuit;
      titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>%1</span><br><span style='color:%6'>%2 empreinte(s) · %3 piste(s) · %4 via(s) · %5 zone(s)</span>")
                         .arg(q(c->nom).toHtmlEscaped())
                         .arg(k.empreintes.size())
                         .arg(k.pistes.size())
                         .arg(k.vias.size())
                         .arg(k.zones.size())
                         .arg(teintes::secondaire()));
    });
    construireCarte();
    construireRegles();
    construireClasses();
    construireBoitier();
    construireDrc();
    colonne()->addStretch(1);
  }
  void resultats() override { relire(); }

 private:
  void construireCarte() {
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Carte"), fl);
    // Contour : rectangle à coins arrondis, ou contour libre dessiné ou importé, avec ses découpes (EL-44)
    auto* libre = inspecteur::texte();
    fl->addRow(libre);
    auto* rect = new QPushButton(icone(QStringLiteral("rectangle")), QStringLiteral("Revenir au rectangle"));
    connect(rect, &QPushButton::clicked, this, [this] { modifier([](ConceptionElec& c) { c.circuit.contour.libre.clear(); }); });
    action(rect);
    fl->addRow(rect);
    auto* largeur = valeur(fl, QStringLiteral("Largeur"), false, 2, 1000, [this] { return conception() ? mm(conception()->circuit.contour.largeur) : 0.0; },
                           [](ConceptionElec& c, double v) { c.circuit.contour.largeur = nm(v); });
    auto* hauteur = valeur(fl, QStringLiteral("Hauteur"), false, 2, 1000, [this] { return conception() ? mm(conception()->circuit.contour.hauteur) : 0.0; },
                           [](ConceptionElec& c, double v) { c.circuit.contour.hauteur = nm(v); });
    auto* rayon = valeur(fl, QStringLiteral("Rayon des coins"), false, 0, 50, [this] { return conception() ? mm(conception()->circuit.contour.rayon) : 0.0; },
                         [](ConceptionElec& c, double v) { c.circuit.contour.rayon = nm(v); });
    suivre([this, fl, libre, rect, largeur, hauteur, rayon] {
      const ConceptionElec* c = conception();
      if (!c) return;
      const auto& k = c->circuit;
      const bool estLibre = k.contour.libre.size() >= 3;
      fl->setRowVisible(libre, estLibre);
      fl->setRowVisible(rect, estLibre);
      for (QWidget* w : {static_cast<QWidget*>(largeur), static_cast<QWidget*>(hauteur), static_cast<QWidget*>(rayon)}) fl->setRowVisible(w, !estLibre);
      if (estLibre) {
        const Boite b = boite(k.contour.libre);
        libre->setText(QStringLiteral("Contour libre : %1 sommets, %2 × %3, %4 mm²")
                           .arg(k.contour.libre.size())
                           .arg(m_atelier.vueCircuit()->longueur(static_cast<double>(b.largeur())), m_atelier.vueCircuit()->longueur(static_cast<double>(b.hauteur())),
                                QLocale().toString(aireMm2(k.contour.libre), 'f', 1)));
      }
    });
    {
      auto* boutons = new QWidget;
      auto* hb = new QHBoxLayout(boutons);
      hb->setContentsMargins(0, 0, 0, 0);
      auto* dessiner = new QPushButton(icone(QStringLiteral("pcb")), QStringLiteral("Dessiner"));
      dessiner->setToolTip(QStringLiteral("Contour libre : clique les sommets dans la vue, double-clic pour fermer"));
      connect(dessiner, &QPushButton::clicked, this, [this] {
        m_atelier.vueCircuit()->setOutil(VueCircuit::Outil::Contour);
        m_atelier.vueCircuit()->setFocus();
      });
      auto* importer = new QPushButton(icone(QStringLiteral("ouvrir")), QStringLiteral("DXF…"));
      importer->setToolTip(QStringLiteral("Importer le contour et ses découpes d’un fichier DXF (lignes, arcs, cercles, polylignes)"));
      connect(importer, &QPushButton::clicked, this, [this] { m_atelier.importerContourDxf(); });
      auto* decoupe = new QPushButton(icone(QStringLiteral("coupe")), QStringLiteral("Découpe"));
      decoupe->setToolTip(QStringLiteral("Fenêtre fraisée de part en part : clique ses sommets dans la carte"));
      connect(decoupe, &QPushButton::clicked, this, [this] {
        m_atelier.vueCircuit()->setOutil(VueCircuit::Outil::Decoupe);
        m_atelier.vueCircuit()->setFocus();
      });
      for (QPushButton* b : {dessiner, importer, decoupe}) {
        hb->addWidget(b);
        action(b);
      }
      fl->addRow(QStringLiteral("Contour"), boutons);
    }
    // Découpes : une ligne chacune, avec « montrer » et « supprimer »
    auto* decoupes = new SectionRefaite([this](QVBoxLayout* col) {
      const ConceptionElec* c = conception();
      if (!c) return;
      const auto& k = c->circuit;
      for (std::size_t i = 0; i < k.contour.decoupes.size(); ++i) {
        const Boite b = boite(k.contour.decoupes[i]);
        auto* ligne = new QWidget;
        auto* hl = new QHBoxLayout(ligne);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(new QLabel(QStringLiteral("Découpe %1 : %2 × %3").arg(i + 1).arg(m_atelier.vueCircuit()->longueur(static_cast<double>(b.largeur())),
                                                                                   m_atelier.vueCircuit()->longueur(static_cast<double>(b.hauteur())))),
                      1);
        auto* voir = new QToolButton;
        voir->setIcon(icone(QStringLiteral("recadrer")));
        voir->setToolTip(QStringLiteral("Montrer la découpe"));
        connect(voir, &QToolButton::clicked, this, [this, b] { m_atelier.vueCircuit()->centrerSur(b.centre()); });
        auto* sup = new QToolButton;
        sup->setIcon(icone(QStringLiteral("supprimer")));
        sup->setToolTip(QStringLiteral("Supprimer la découpe"));
        sup->setEnabled(!lectureSeule());
        connect(sup, &QToolButton::clicked, this, [this, i] {
          modifier([&](ConceptionElec& c2) {
            if (i < c2.circuit.contour.decoupes.size()) c2.circuit.contour.decoupes.erase(c2.circuit.contour.decoupes.begin() + static_cast<std::ptrdiff_t>(i));
          });
        });
        hl->addWidget(voir);
        hl->addWidget(sup);
        col->addWidget(ligne);
      }
    });
    fl->addRow(decoupes);
    suivre([this, decoupes] {
      const ConceptionElec* c = conception();
      if (!c) return;
      std::string sig = lectureSeule() ? "L" : "M";
      for (const auto& d : c->circuit.contour.decoupes) {
        const Boite b = boite(d);
        sig += std::to_string(b.x0) + ',' + std::to_string(b.y0) + ',' + std::to_string(b.x1) + ',' + std::to_string(b.y1) + ';';
      }
      decoupes->refaire(sig + c->circuit.affichage.unite);
    });
    // Empilage : cuivre, épaisseur, vernis
    auto* couches = new QComboBox;
    couches->setObjectName(QStringLiteral("couchesCuivre"));
    couches->addItems({QStringLiteral("2 couches"), QStringLiteral("4 couches"), QStringLiteral("1 couche (simple face, cuivre dessous)")});
    auto indice = [](int n) { return n >= 4 ? 1 : n == 1 ? 2 : 0; };
    connect(couches, &QComboBox::activated, this, [this, couches, indice](int i) {
      const ConceptionElec* c0 = conception();
      if (!c0) return;
      const int nombre = i == 1 ? 4 : i == 2 ? 1 : 2;
      if (nombre == c0->circuit.empilage.couchesCuivre) return;
      const Couches gardees = tousLesCuivres(nombre);
      // Moins de couches : les objets des couches retirées sont annoncés, puis retirés avec elles (U09)
      int pistes = 0, zones = 0, vias = 0;
      for (const auto& p : c0->circuit.pistes) pistes += !(bit(p.couche) & gardees);
      for (const auto& z : c0->circuit.zones) zones += !(bit(z.couche) & gardees);
      if (nombre == 1) vias = static_cast<int>(c0->circuit.vias.size());
      if (pistes + zones > 0) {
        QMessageBox b(QMessageBox::Warning, QStringLiteral("Changer l’empilage"),
                      QStringLiteral("%1 piste(s) et %2 zone(s) sont sur des couches qui disparaissent. Elles seront supprimées.%3")
                          .arg(pistes)
                          .arg(zones)
                          .arg(vias ? QStringLiteral(" Les %1 via(s) deviendront des ponts de fil, signalés par la DRC.").arg(vias) : QString()),
                      QMessageBox::Yes | QMessageBox::Cancel, this);
        b.button(QMessageBox::Yes)->setText(QStringLiteral("Les supprimer et changer"));
        b.button(QMessageBox::Cancel)->setText(QStringLiteral("Annuler"));
        b.setDefaultButton(QMessageBox::Cancel);
        if (b.exec() != QMessageBox::Yes) {
          const QSignalBlocker bl(couches);
          couches->setCurrentIndex(indice(c0->circuit.empilage.couchesCuivre));
          return;
        }
      }
      modifier([&](ConceptionElec& c2) {
        c2.circuit.empilage.couchesCuivre = nombre;
        auto& p = c2.circuit.pistes;
        p.erase(std::remove_if(p.begin(), p.end(), [&](const Piste& x) { return !(bit(x.couche) & gardees); }), p.end());
        auto& z = c2.circuit.zones;
        z.erase(std::remove_if(z.begin(), z.end(), [&](const Zone& x) { return !(bit(x.couche) & gardees); }), z.end());
      });
      if (!(bit(m_atelier.vueCircuit()->coucheActive()) & gardees)) m_atelier.vueCircuit()->setCoucheActive(couchesCuivre(nombre).back());
      m_atelier.majCouches();
    });
    lier(couches, [this, couches, indice] {
      if (const ConceptionElec* c = conception()) couches->setCurrentIndex(indice(c->circuit.empilage.couchesCuivre));
    });
    fl->addRow(QStringLiteral("Cuivre"), couches);
    auto* ep = new QComboBox;
    for (double v : {0.8, 1.0, 1.2, 1.6, 2.0}) ep->addItem(QLocale().toString(v, 'f', 1) + QStringLiteral(" mm"), v);
    connect(ep, &QComboBox::activated, this, [this, ep](int) {
      const double v = ep->currentData().toDouble();
      modifier([&](ConceptionElec& c) { c.circuit.empilage.epaisseur = v; });
    });
    lier(ep, [this, ep] {
      if (const ConceptionElec* c = conception()) ep->setCurrentIndex(std::max(0, ep->findData(c->circuit.empilage.epaisseur)));
    });
    fl->addRow(QStringLiteral("Épaisseur"), ep);
    auto* vernis = new QComboBox;
    vernis->addItems({QStringLiteral("vert"), QStringLiteral("bleu"), QStringLiteral("rouge"), QStringLiteral("noir"), QStringLiteral("blanc"), QStringLiteral("violet")});
    connect(vernis, &QComboBox::activated, this, [this, vernis](int) {
      const std::string t = vernis->currentText().toStdString();
      modifier([&](ConceptionElec& c) { c.circuit.empilage.vernis = t; });
    });
    lier(vernis, [this, vernis] {
      if (const ConceptionElec* c = conception()) vernis->setCurrentText(q(c->circuit.empilage.vernis));
    });
    fl->addRow(QStringLiteral("Vernis"), vernis);
  }

  void construireRegles() {
    QFormLayout* fr = nullptr;
    groupe(QStringLiteral("Règles de conception"), fr);
    auto* profil = new QComboBox;
    profil->setObjectName(QStringLiteral("profilFabricant"));
    for (const auto& p : profilsFabricants()) profil->addItem(q(p.nom));
    profil->setToolTip(QStringLiteral("Profil de fabricant : applique ses minimums"));
    connect(profil, &QComboBox::activated, this, [this, profil](int i) { m_atelier.choisirProfil(static_cast<std::size_t>(i), profil); });
    lier(profil, [this, profil] {
      if (const ConceptionElec* c = conception()) profil->setCurrentText(q(c->circuit.regles.profil));
    });
    fr->addRow(QStringLiteral("Profil"), profil);
    using Champ = Nm Regles::*;
    for (const auto& [libelle, champ, mini, maxi] : std::initializer_list<std::tuple<QString, Champ, double, double>>{
             {QStringLiteral("Isolement"), &Regles::isolement, 0.05, 5},
             {QStringLiteral("Largeur de piste"), &Regles::largeur, 0.05, 10},
             {QStringLiteral("Largeur minimale"), &Regles::largeurMin, 0.05, 10},
             {QStringLiteral("Via : diamètre"), &Regles::viaDiametre, 0.2, 5},
             {QStringLiteral("Via : perçage"), &Regles::viaPercage, 0.1, 4},
             {QStringLiteral("Distance au bord"), &Regles::bord, 0, 5}}) {
      const Champ m = champ;
      valeur(fr, libelle, false, mini, maxi, [this, m] { return conception() ? mm(conception()->circuit.regles.*m) : 0.0; }, [m](ConceptionElec& c, double v) { c.circuit.regles.*m = nm(v); });
    }
  }

  // Classes de réseaux : largeur et isolement propres (alimentation, puissance…), suivies par le routage et la DRC.
  // Le bloc de chaque classe se refait quand les classes ou les réseaux changent, jamais pendant une saisie.
  void construireClasses() {
    auto* gcl = new QGroupBox(QStringLiteral("Classes de réseaux"));
    gcl->setObjectName(QStringLiteral("classesReseaux"));
    auto* lcl = new QVBoxLayout(gcl);
    lcl->addWidget(inspecteur::texte(QStringLiteral("<span style='color:%1'>Une classe donne aux réseaux qu’on y range leur largeur de piste et leur isolement. "
                                                    "Rien n’est déduit du nom d’un réseau : indique le courant à transporter, la largeur recommandée par l’IPC-2221 "
                                                    "s’affiche et la DRC vérifie chaque piste.</span>")
                                         .arg(teintes::secondaire())));
    auto* blocs = new SectionRefaite([this](QVBoxLayout* col) { construireBlocs(col); });
    lcl->addWidget(blocs);
    auto* ajout = new QPushButton(QStringLiteral("＋ Nouvelle classe"));
    ajout->setObjectName(QStringLiteral("nouvelleClasse"));
    connect(ajout, &QPushButton::clicked, this, [this] {
      modifier([](ConceptionElec& c) {
        ClasseReseau cl;
        const Regles& R = c.circuit.regles;
        cl.nom = "Puissance";
        for (int n = 2; std::any_of(R.classes.begin(), R.classes.end(), [&](const ClasseReseau& x) { return x.nom == cl.nom; }); ++n) cl.nom = "Puissance " + std::to_string(n);
        cl.largeur = std::max<Nm>(2 * R.largeur, 500'000);
        cl.isolement = std::max<Nm>(R.isolement, 250'000);
        cl.viaDiametre = std::max<Nm>(R.viaDiametre, 1'000'000);
        cl.viaPercage = std::max<Nm>(R.viaPercage, 500'000);
        c.circuit.regles.classes.push_back(cl);
      });
    });
    action(ajout);
    lcl->addWidget(ajout);
    colonne()->addWidget(gcl);
    suivre([this, blocs] {
      const ConceptionElec* c = conception();
      if (!c) return;
      std::string sig = (lectureSeule() ? "L|" : "M|") + c->circuit.affichage.unite + '|' + std::to_string(c->circuit.empilage.cuivre) + '|';
      for (const auto& cl : c->circuit.regles.classes)
        sig += cl.nom + ',' + std::to_string(cl.largeur) + ',' + std::to_string(cl.isolement) + ',' + std::to_string(cl.viaDiametre) + ',' + std::to_string(cl.viaPercage) + ',' +
               std::to_string(cl.courant) + ',' + std::to_string(cl.echauffement) + ';';
      for (const auto& [r, cls] : c->circuit.regles.classeReseau) sig += r + '=' + cls + ';';
      for (const auto& r : m_atelier.calculs().k.reseaux)
        if (!r.broches.empty()) sig += r.nom + ',';
      blocs->refaire(sig);
    });
  }

  void construireBlocs(QVBoxLayout* col) {
    const ConceptionElec* c = conception();
    if (!c) return;
    const prom::ecad::Circuit& k = c->circuit;
    const bool lecture = lectureSeule();
    const prom::Unites u = unites();
    for (const auto& cl : k.regles.classes) {
      const std::string nom = cl.nom;
      auto* bloc = new QWidget;
      auto* fb = new QFormLayout(bloc);
      fb->setRowWrapPolicy(QFormLayout::WrapLongRows);
      fb->setContentsMargins(0, 6, 0, 0);
      auto* nomEd = new QLineEdit(q(nom));
      nomEd->setReadOnly(lecture);
      connect(nomEd, &QLineEdit::editingFinished, this, [this, nomEd, nom] {
        const std::string nouveau = s(nomEd->text());
        if (nouveau.empty() || nouveau == nom) return;
        modifier([&](ConceptionElec& c2) {
          for (auto& x : c2.circuit.regles.classes)
            if (x.nom == nom) x.nom = nouveau;
          for (auto& [r, cls] : c2.circuit.regles.classeReseau)
            if (cls == nom) cls = nouveau;
        });
      });
      fb->addRow(QStringLiteral("Classe"), nomEd);
      using Champ = Nm ClasseReseau::*;
      for (const auto& [libelle, champ, mini, maxi] : std::initializer_list<std::tuple<QString, Champ, double, double>>{
               {QStringLiteral("Largeur de piste"), &ClasseReseau::largeur, 0.05, 20},
               {QStringLiteral("Isolement"), &ClasseReseau::isolement, 0.05, 10},
               {QStringLiteral("Via : diamètre"), &ClasseReseau::viaDiametre, 0.2, 5},
               {QStringLiteral("Via : perçage"), &ClasseReseau::viaPercage, 0.1, 4}}) {
        const Champ m = champ;
        auto* ch = new ChampValeur(false, u);
        ch->setBornes(mini, maxi);
        ch->setValeur(mm(cl.*m));
        ch->setReadOnly(lecture);
        connect(ch, &ChampValeur::valeurChangee, this, [this, nom, m](double v) {
          modifier([&](ConceptionElec& c2) {
            for (auto& x : c2.circuit.regles.classes)
              if (x.nom == nom) x.*m = nm(v);
          });
        });
        fb->addRow(libelle, ch);
      }
      // Dimensionnement électrique : courant saisi, largeur IPC-2221 recommandée et hypothèses affichées
      auto* courant = new QDoubleSpinBox;
      courant->setRange(0, 100);
      courant->setDecimals(2);
      courant->setSuffix(QStringLiteral(" A"));
      courant->setSpecialValueText(QStringLiteral("non renseigné"));
      courant->setValue(cl.courant);
      courant->setEnabled(!lecture);
      connect(courant, &QDoubleSpinBox::editingFinished, this, [this, courant, nom] {
        const double v = courant->value();
        modifier([&](ConceptionElec& c2) {
          for (auto& x : c2.circuit.regles.classes)
            if (x.nom == nom) x.courant = v;
        });
      });
      fb->addRow(QStringLiteral("Courant"), courant);
      auto* dt = new QDoubleSpinBox;
      dt->setRange(1, 100);
      dt->setDecimals(0);
      dt->setSuffix(QStringLiteral(" °C"));
      dt->setValue(cl.echauffement);
      dt->setEnabled(!lecture);
      connect(dt, &QDoubleSpinBox::editingFinished, this, [this, dt, nom] {
        const double v = dt->value();
        modifier([&](ConceptionElec& c2) {
          for (auto& x : c2.circuit.regles.classes)
            if (x.nom == nom) x.echauffement = v;
        });
      });
      fb->addRow(QStringLiteral("Échauffement admis"), dt);
      QString reco = QStringLiteral("<span style='color:%1'>Sans courant, aucune vérification électrique.</span>").arg(teintes::secondaire());
      if (cl.courant > 0) {
        const Nm w = largeurIPC2221(cl.courant, cl.echauffement, k.empilage.cuivre, true);
        reco = QStringLiteral("<b>%1 mm</b> recommandés (%2) %3")
                   .arg(QLocale().toString(mm(w), 'f', 2), q(hypothesesIPC2221(cl.courant, cl.echauffement, k.empilage.cuivre, true)).toHtmlEscaped(),
                        cl.largeur >= w ? QStringLiteral("<span style='color:%1'>✓</span>").arg(teintes::succes())
                                        : QStringLiteral("<span style='color:%1'>largeur de la classe insuffisante</span>").arg(teintes::erreur()));
      }
      fb->addRow(QStringLiteral("IPC-2221"), inspecteur::texte(reco));
      // Réseaux de la classe choisis dans la liste des réseaux de la carte (plus de faute de frappe), et motifs à la
      // façon de KiCad (« +*V », « USB_D? ») pour ceux qui viendront (EL-39)
      auto* liste = new QListWidget;
      liste->setMaximumHeight(130);
      liste->setEnabled(!lecture);
      std::vector<std::string> nomsReseaux;
      for (const auto& r : m_atelier.calculs().k.reseaux)
        if (!r.broches.empty() && !r.nom.empty() && std::find(nomsReseaux.begin(), nomsReseaux.end(), r.nom) == nomsReseaux.end()) nomsReseaux.push_back(r.nom);
      std::sort(nomsReseaux.begin(), nomsReseaux.end());
      QString motifs;
      for (const auto& [r, cls] : k.regles.classeReseau)
        if (cls == nom && motifReseau(r)) motifs += (motifs.isEmpty() ? QString() : QStringLiteral(", ")) + q(r);
      for (const auto& r : nomsReseaux) {
        auto* it = new QListWidgetItem(q(r), liste);
        const auto aff = k.regles.classeReseau.find(m_atelier.calculs().k.canonique(r));
        const ClasseReseau* parMotif = aff == k.regles.classeReseau.end() ? classeDe(k.regles, r) : nullptr;
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(aff != k.regles.classeReseau.end() && aff->second == nom ? Qt::Checked : Qt::Unchecked);
        if (aff != k.regles.classeReseau.end() && aff->second != nom) it->setToolTip(QStringLiteral("Dans la classe « %1 »").arg(q(aff->second)));
        if (parMotif) {
          it->setToolTip(QStringLiteral("Pris par un motif de la classe « %1 »").arg(q(parMotif->nom)));
          if (parMotif->nom == nom) it->setForeground(Theme::instance().c().accentTexte);
        }
      }
      connect(liste, &QListWidget::itemChanged, this, [this, nom](QListWidgetItem* it) {
        const std::string r = it->text().toStdString();
        const bool coche = it->checkState() == Qt::Checked;
        QTimer::singleShot(0, this, [this, nom, r, coche] {
          modifier([&](ConceptionElec& c2) {
            auto& a = c2.circuit.regles.classeReseau;
            if (coche) a[r] = nom;
            else if (const auto x = a.find(r); x != a.end() && x->second == nom) a.erase(x);
          });
        });
      });
      fb->addRow(QStringLiteral("Réseaux"), liste);
      auto* motifsEd = new QLineEdit(motifs);
      motifsEd->setReadOnly(lecture);
      motifsEd->setPlaceholderText(QStringLiteral("+*V, USB_D?…"));
      motifsEd->setToolTip(QStringLiteral("Motifs : « * » pour toute suite de caractères, « ? » pour un caractère ; séparés par des virgules. Un nom exact passe avant un motif."));
      connect(motifsEd, &QLineEdit::editingFinished, this, [this, motifsEd, nom] {
        std::vector<std::string> noms;
        for (const QString& x : motifsEd->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
          if (!x.trimmed().isEmpty()) noms.push_back(x.trimmed().toStdString());
        modifier([&](ConceptionElec& c2) {
          auto& a = c2.circuit.regles.classeReseau;
          for (auto it = a.begin(); it != a.end();) it = it->second == nom && motifReseau(it->first) ? a.erase(it) : std::next(it);
          for (const auto& n : noms) a[n] = nom;  // un nom sans joker est une affectation exacte
        });
      });
      fb->addRow(QStringLiteral("Motifs"), motifsEd);
      auto* sup = new QPushButton(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer la classe"));
      sup->setEnabled(!lecture);
      connect(sup, &QPushButton::clicked, this, [this, nom] {
        modifier([&](ConceptionElec& c2) {
          auto& v = c2.circuit.regles.classes;
          v.erase(std::remove_if(v.begin(), v.end(), [&](const ClasseReseau& x) { return x.nom == nom; }), v.end());
          auto& a = c2.circuit.regles.classeReseau;
          for (auto it = a.begin(); it != a.end();) it = it->second == nom ? a.erase(it) : std::next(it);
        });
      });
      fb->addRow(sup);
      col->addWidget(bloc);
    }
  }

  void construireBoitier() {
    auto* gb = new QGroupBox(QStringLiteral("Boîtier"));
    auto* lb = new QVBoxLayout(gb);
    auto* suit = new QCheckBox(QStringLiteral("Le boîtier du projet suit cette carte"));
    suit->setObjectName(QStringLiteral("boitierSuitCarte"));
    connect(suit, &QCheckBox::toggled, this, [this](bool on) {
      if (on) m_atelier.lierAuBoitier(false);  // on reste dans l'atelier (P22)
      else modifier([](ConceptionElec& c) { c.pilotageBoitier = false; });
    });
    lier(suit, [this, suit] {
      if (const ConceptionElec* c = conception()) suit->setChecked(c->pilotageBoitier);
    });
    lb->addWidget(suit);
    colonne()->addWidget(gb);
  }

  void construireDrc() {
    auto* gd = new QGroupBox(QStringLiteral("Vérification des règles (DRC)"));
    auto* ld = new QVBoxLayout(gd);
    auto* bilan = inspecteur::texte();
    bilan->setObjectName(QStringLiteral("bilanDrc"));
    ld->addWidget(bilan);
    auto* liste = new SectionRefaite([this](QVBoxLayout* col) { inspecteur::listeProblemes(col, m_atelier.calculs().drc, false, m_atelier); });
    ld->addWidget(liste);
    colonne()->addWidget(gd);
    suivre([this, bilan, liste] {
      const ConceptionElec* c = conception();
      if (!c) return;
      const auto& calc = m_atelier.calculs();
      const int erreurs = static_cast<int>(std::count_if(calc.drc.begin(), calc.drc.end(), [](const Violation& v) { return v.gravite == Gravite::Erreur; }));
      // « Prêt pour la fabrication » vient de la validation complète, pas de la seule DRC (P28) : l'ERC, les
      // bibliothèques, la nomenclature ou les capacités du fabricant peuvent encore bloquer la publication
      const prom::ecad::Validation validation = prom::ecad::valider(*c, calc.k, calc.etat, m_atelier.erc(), calc.drc);
      QString etat;
      if (erreurs) etat = QStringLiteral("<b>%1 erreur(s), %2 avertissement(s)</b> — clique pour y aller").arg(erreurs).arg(calc.drc.size() - static_cast<std::size_t>(erreurs));
      else if (validation.publiable()) etat = QStringLiteral("<span style='color:%1'><b>✓ Prêt pour la fabrication</b></span>").arg(teintes::succes());
      else {
        QStringList ailleurs;
        int nb = 0;
        for (const auto& e : validation.etapes)
          if (e.erreurs > 0 && e.nom != "Circuit imprimé (DRC)") {
            ailleurs << q(e.nom);
            nb += e.erreurs;
          }
        etat = QStringLiteral("DRC sans erreur · <span style='color:%1'><b>%2 erreur(s) ailleurs</b></span> (%3) : pas encore prêt pour la fabrication")
                   .arg(teintes::erreur())
                   .arg(nb)
                   .arg(ailleurs.join(QStringLiteral(", ")));
      }
      bilan->setText(QStringLiteral("%1 / %2 réseaux routés<br>%3").arg(calc.etat.reseauxComplets).arg(calc.etat.reseauxTotal).arg(etat));
      liste->refaire(inspecteur::signature(calc.drc));
    });
  }
};

// ---------- Vue 3D ----------
class PageVue3D : public PageInspecteur {
 public:
  explicit PageVue3D(AtelierElectronique& a) : PageInspecteur(a) {
    // Ce que la vue montre vraiment (T2) : des boîtes, à la hauteur saisie du composant, sinon à la hauteur générique
    colonne()->addWidget(inspecteur::texte(QStringLiteral("<b>Vue 3D</b><br>La carte, son cuivre, et chaque composant en boîte : à la hauteur saisie dans son panneau, sinon à la hauteur "
                                                          "générique de son empreinte. Bouton gauche pour tourner, droit pour déplacer, molette pour zoomer.")));
    colonne()->addStretch(1);
  }
};

QString couleurScore(int score) {
  return score >= 95 ? teintes::succes() : score >= 70 ? teintes::lien() : score >= 40 ? teintes::alerte() : teintes::erreur();
}

// ---------- Fabrication : santé de la conception, publication, nomenclature, fabricant ----------
// La page ne tient pas de champ de saisie : son contenu se refait quand la validation, les publications ou la
// nomenclature changent, dans la même zone défilante.
class PageFabrication : public PageInspecteur {
 public:
  explicit PageFabrication(AtelierElectronique& a) : PageInspecteur(a) {
    auto* contenu = new SectionRefaite([this](QVBoxLayout* col) { construire(col); });
    colonne()->addWidget(contenu);
    colonne()->addStretch(1);
    suivre([this, contenu] {
      const ConceptionElec* c = m_atelier.conceptionCourante();
      if (!c) return;
      const Validation v = m_atelier.validation();
      std::string sig = c->nom + '|' + c->cartouche.revision + '|' + (c->verrouillee ? "L" : "M") + '|';
      for (const auto& e : v.etapes) {
        sig += e.nom + ':' + std::to_string(e.score) + ':' + std::to_string(e.erreurs) + ':' + std::to_string(e.avertissements) + ';';
        for (const auto& d : e.details) sig += d.code + d.message + ';';
      }
      for (const auto& p : c->publications) sig += p.id + ',';
      for (const auto& l : nomenclature(*c)) {
        sig += std::to_string(l.quantite) + l.valeur;
        for (const auto& r : l.references) sig += r + ',';
      }
      const auto& e = c->circuit.empilage;
      sig += c->circuit.regles.profil + std::to_string(e.couchesCuivre) + std::to_string(e.epaisseur) + std::to_string(e.cuivre) + e.vernis + e.finition;
      contenu->refaire(sig);
    });
  }

 private:
  void construire(QVBoxLayout* col) {
    const ConceptionElec* c = m_atelier.conceptionCourante();
    if (!c) return;
    const Validation v = m_atelier.validation();
    col->addWidget(inspecteur::texte(
        QStringLiteral("<span style='font-size:15px; font-weight:700'>%1</span> <span style='color:%4'>révision %2</span><br>%3")
            .arg(q(c->nom).toHtmlEscaped(), q(c->cartouche.revision))
            .arg(v.publiable()        ? QStringLiteral("<span style='color:%1'><b>✓ Prête à publier</b></span> — les fichiers seront contrôlés à l’écriture").arg(teintes::succes())
                 : v.critiques() > 0 ? QStringLiteral("<span style='color:%1'><b>%2 erreur(s) bloquante(s)</b></span>, dont %3 critique(s) sans dérogation possible")
                                           .arg(teintes::erreur())
                                           .arg(v.bloquants())
                                           .arg(v.critiques())
                                     : QStringLiteral("<span style='color:%1'><b>%2 erreur(s) bloquante(s)</b></span> avant de publier (dérogation possible)").arg(teintes::erreur()).arg(v.bloquants()))
            .arg(teintes::secondaire())));
    // Santé de la conception : un score par étape, tiré de mesures réelles ; un clic mène au problème
    auto* gs = new QGroupBox(QStringLiteral("Santé de la conception"));
    auto* ls = new QVBoxLayout(gs);
    auto* arbre = new QTreeWidget;
    arbre->setHeaderHidden(true);
    arbre->setRootIsDecorated(true);
    arbre->setWordWrap(true);
    arbre->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    arbre->setStyleSheet(QStringLiteral("QTreeWidget { border: none; background: transparent; } QTreeWidget::item { padding: 2px; }"));
    int lignes = 0;
    const QColor fondBarre = Theme::instance().c().bordure;
    for (const auto& e : v.etapes) {
      auto* it = new QTreeWidgetItem(arbre);
      it->setSizeHint(0, QSize(10, 28));
      auto* rang = new QWidget;
      auto* h = new QHBoxLayout(rang);
      h->setContentsMargins(0, 0, 4, 0);
      auto* nom = new QLabel(q(e.nom));
      nom->setMinimumWidth(150);
      h->addWidget(nom);
      auto* barre = new QProgressBar;
      barre->setRange(0, 100);
      barre->setValue(e.score);
      barre->setTextVisible(false);
      barre->setFixedHeight(8);
      const QString couleur = couleurScore(e.erreurs ? std::min(e.score, 39) : e.score);
      barre->setProperty("themeFixe", true);
      barre->setStyleSheet(QStringLiteral("QProgressBar { background: %1; border: none; border-radius: 4px; } QProgressBar::chunk { background: %2; border-radius: 4px; }")
                               .arg(fondBarre.name(), couleur));
      h->addWidget(barre, 1);
      const QString etat = e.erreurs ? QStringLiteral("✖ %1").arg(e.erreurs) : e.avertissements ? QStringLiteral("⚠ %1").arg(e.avertissements) : QStringLiteral("✓");
      auto* score = inspecteur::texte(QStringLiteral("<span style='color:%1'><b>%2 %</b> %3</span>").arg(couleur).arg(e.score).arg(etat));
      score->setWordWrap(false);
      score->setMinimumWidth(78);
      score->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      h->addWidget(score);
      arbre->setItemWidget(it, 0, rang);
      ++lignes;
      for (const auto& d : e.details) {
        auto* x = new QTreeWidgetItem(it, {(d.gravite == Gravite::Erreur ? QStringLiteral("✖ ") : QStringLiteral("⚠ ")) + q(d.message)});
        x->setToolTip(0, q(d.conseil));
        x->setForeground(0, d.gravite == Gravite::Erreur ? Theme::instance().c().erreur : Theme::instance().c().alerte);
        x->setData(0, Qt::UserRole, q(d.code));
        x->setData(0, Qt::UserRole + 1, QPointF(mm(d.position.x), mm(d.position.y)));
        x->setData(0, Qt::UserRole + 2, q(d.feuille));
        x->setData(0, Qt::UserRole + 3, d.objets.empty() ? QString() : q(d.objets.front()));
        if (e.erreurs > 0) ++lignes;
      }
      it->setExpanded(e.erreurs > 0);
    }
    arbre->setMinimumHeight(std::min(420, 12 + 30 * lignes));
    connect(arbre, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
      const QString code = it->data(0, Qt::UserRole).toString();
      if (code.isEmpty()) {
        it->setExpanded(!it->isExpanded());
        return;
      }
      m_atelier.afficherMessage(QStringLiteral("<b>%1</b> — %2").arg(it->text(0).toHtmlEscaped(), it->toolTip(0).toHtmlEscaped()));
      const QPointF p = it->data(0, Qt::UserRole + 1).toPointF();
      const Pt pos = ptMm(p.x(), p.y());
      const std::string objet = it->data(0, Qt::UserRole + 3).toString().toStdString();
      if (code.startsWith(QLatin1String("ERC")) || code.startsWith(QLatin1String("INT")) || code.startsWith(QLatin1String("BOM"))) {
        if (const ConceptionElec* c2 = m_atelier.conceptionCourante()) {
          const std::string feuille = it->data(0, Qt::UserRole + 2).toString().toStdString();
          m_atelier.setMode(AtelierElectronique::Schema);
          for (std::size_t i = 0; i < c2->feuilles.size(); ++i)
            if (c2->feuilles[i].uuid == feuille) m_atelier.vueSchema()->setConception(m_atelier.conception(), static_cast<int>(i));
          if (!objet.empty() && c2->composant(objet)) m_atelier.montrerComposant(objet);
          else if (pos.x != 0 || pos.y != 0) m_atelier.vueSchema()->centrerSur(pos);
        }
      } else if (code.startsWith(QLatin1String("DRC")) || code.startsWith(QLatin1String("PCB"))) {
        m_atelier.setMode(AtelierElectronique::Circuit);
        if (pos.x != 0 || pos.y != 0) m_atelier.vueCircuit()->centrerSur(pos);
        if (!objet.empty()) m_atelier.vueCircuit()->choisir(VueCircuit::Genre::Empreinte, objet);
      }
    });
    ls->addWidget(arbre);
    col->addWidget(gs);
    // Publication
    auto* gp = new QGroupBox(QStringLiteral("Publication"));
    auto* lp = new QVBoxLayout(gp);
    if (c->verrouillee) {
      lp->addWidget(new QLabel(QStringLiteral("Cette révision est publiée et ne se modifie plus.")));
    } else {
      lp->addWidget(inspecteur::texte(QStringLiteral("<span style='color:%1'>Tout est vérifié, le dossier complet est écrit (Gerber, perçage, placement, nomenclature, fiche de publication) "
                                                     "avec l’empreinte de la conception, puis la révision est verrouillée.</span>")
                                          .arg(teintes::secondaire())));
      auto* b = new QPushButton(icone(QStringLiteral("publier")), QStringLiteral("Publier pour la fabrication…"));
      b->setObjectName(QStringLiteral("publier"));
      connect(b, &QPushButton::clicked, this, [this] { m_atelier.publierInteractif(); });
      lp->addWidget(b);
    }
    if (!c->publications.empty()) {
      QString h;
      for (auto it = c->publications.rbegin(); it != c->publications.rend(); ++it) {
        h += QStringLiteral("<b>%1</b> · rév. %2 · %3 · %4<br><span style='color:%6'>empreinte %5</span>")
                 .arg(q(it->id), q(it->revision), q(it->date).toHtmlEscaped(), q(it->auteur.empty() ? std::string("—") : it->auteur).toHtmlEscaped(), q(it->empreinte), teintes::secondaire());
        if (!it->derogation.empty())
          h += QStringLiteral("<br><span style='color:%1'>Dérogation (%2 erreur(s)) : %3</span>").arg(teintes::alerte()).arg(it->violations.size()).arg(q(it->derogation).toHtmlEscaped());
        h += QStringLiteral("<br>");
      }
      auto* l = inspecteur::texte(h);
      l->setTextInteractionFlags(Qt::TextSelectableByMouse);
      lp->addWidget(l);
      // Contrôle d'intégrité : chaque fichier publié garde son empreinte SHA-256
      auto* verif = new QPushButton(QStringLiteral("Vérifier les fichiers de %1…").arg(q(c->publications.back().id)));
      connect(verif, &QPushButton::clicked, this, [this] {
        const ConceptionElec* cc = m_atelier.conceptionCourante();
        if (!cc || cc->publications.empty()) return;
        const QString parent = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier qui contient la publication %1").arg(q(cc->publications.back().id)));
        if (parent.isEmpty()) return;
        const auto ecarts = verifierPublication(cc->publications.back(), parent.toStdString());
        QString t;
        for (const auto& e : ecarts) t += QStringLiteral("<li>%1</li>").arg(q(e).toHtmlEscaped());
        if (ecarts.empty())
          QMessageBox::information(this, QStringLiteral("Publication intacte"),
                                   QStringLiteral("Les %1 fichiers de %2 sont identiques à ceux publiés (SHA-256).").arg(cc->publications.back().fichiers.size()).arg(q(cc->publications.back().id)));
        else
          QMessageBox::warning(this, QStringLiteral("Publication modifiée"), QStringLiteral("<p>Écarts trouvés :</p><ul>%1</ul>").arg(t));
      });
      lp->addWidget(verif);
    }
    col->addWidget(gp);
    // Nomenclature
    const auto bom = nomenclature(*c);
    auto* gb = new QGroupBox(QStringLiteral("Nomenclature — %1 ligne(s)").arg(bom.size()));
    auto* lb = new QVBoxLayout(gb);
    auto* table = new QTableWidget(static_cast<int>(bom.size()), 3);
    table->setHorizontalHeaderLabels({QStringLiteral("Qté"), QStringLiteral("Références"), QStringLiteral("Valeur")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setWordWrap(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    for (std::size_t i = 0; i < bom.size(); ++i) {
      QString refs;
      for (const auto& r : bom[i].references) refs += (refs.isEmpty() ? QString() : QStringLiteral(", ")) + q(r);
      const int r = static_cast<int>(i);
      const QString bulle = bom[i].mpn.empty() ? QStringLiteral("Sans référence fabricant") : q(bom[i].fabricant + " " + bom[i].mpn);
      table->setItem(r, 0, new QTableWidgetItem(QString::number(bom[i].quantite)));
      table->setItem(r, 1, new QTableWidgetItem(refs));
      table->setItem(r, 2, new QTableWidgetItem(q(bom[i].valeur)));
      for (int col2 = 0; col2 < 3; ++col2) table->item(r, col2)->setToolTip(bulle);
    }
    table->resizeRowsToContents();
    table->setMinimumHeight(std::min(360, 34 + 28 * static_cast<int>(bom.size())));
    lb->addWidget(table);
    col->addWidget(gb);
    // Empilage et fabricant, rappel
    const auto& e = c->circuit.empilage;
    auto* gf = new QGroupBox(QStringLiteral("Fabricant"));
    auto* lf = new QVBoxLayout(gf);
    lf->addWidget(inspecteur::texte(QStringLiteral("Profil <b>%1</b><br>%2 couches · %3 mm · cuivre %4 µm · vernis %5 · %6")
                                        .arg(q(c->circuit.regles.profil).toHtmlEscaped())
                                        .arg(e.couchesCuivre)
                                        .arg(QLocale().toString(e.epaisseur, 'f', 1))
                                        .arg(QLocale().toString(e.cuivre * 1000, 'f', 0))
                                        .arg(q(e.vernis).toHtmlEscaped(), q(e.finition).toHtmlEscaped())));
    col->addWidget(gf);
  }
};

}  // namespace

PageInspecteur* pageCircuit(const QString& cle, AtelierElectronique& a) {
  if (cle == QLatin1String("fabrication")) return new PageFabrication(a);
  if (cle == QLatin1String("vue3d")) return new PageVue3D(a);
  if (cle == QLatin1String("circuit.plusieurs")) return new PagePlusieurs(a);
  if (cle == QLatin1String("circuit.empreinte")) return pageComposantCircuit(a);  // la page du composant (3.10)
  if (cle == QLatin1String("circuit.composants")) return pagePlusieursComposants(a, true);
  if (cle == QLatin1String("circuit.placement")) return new PagePlacement(a);  // empreinte sans composant
  if (cle == QLatin1String("circuit.piste")) return new PagePiste(a);
  if (cle == QLatin1String("circuit.via")) return new PageVia(a);
  if (cle == QLatin1String("circuit.zone")) return new PageZone(a);
  if (cle == QLatin1String("circuit.texte")) return new PageTexteCarte(a);
  return new PageCarte(a);
}

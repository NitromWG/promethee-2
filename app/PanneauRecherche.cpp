// Prométhée : panneau Chercher de l'atelier électronique (lot L2, tâche 2.12). Licence GPL-3.0-only.
#include "PanneauRecherche.hpp"

#include <algorithm>
#include <map>
#include <set>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "InspecteurElectronique.hpp"
#include "RegistreActions.hpp"
#include "SelectionElectronique.hpp"
#include "VueCircuit.hpp"
#include "VueSchema.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QString nomGenre(PanneauRecherche::Genre g) {
  switch (g) {
    case PanneauRecherche::Genre::Repere: return QStringLiteral("Repère");
    case PanneauRecherche::Genre::Valeur: return QStringLiteral("Valeur");
    case PanneauRecherche::Genre::Champ: return QStringLiteral("Champ");
    case PanneauRecherche::Genre::Etiquette: return QStringLiteral("Étiquette");
    case PanneauRecherche::Genre::Alimentation: return QStringLiteral("Alimentation");
    case PanneauRecherche::Genre::Texte: return QStringLiteral("Texte");
    case PanneauRecherche::Genre::Reseau: return QStringLiteral("Réseau");
    case PanneauRecherche::Genre::Zone: return QStringLiteral("Zone");
    case PanneauRecherche::Genre::Vias: return QStringLiteral("Vias");
    case PanneauRecherche::Genre::Pistes: return QStringLiteral("Pistes");
    case PanneauRecherche::Genre::TexteCarte: return QStringLiteral("Texte de la carte");
  }
  return {};
}
// Jokers : * pour une suite de caractères, ? pour un seul ; le reste est pris tel quel
QString jokersVersMotif(const QString& t) {
  QString r;
  for (const QChar c : t) {
    if (c == QLatin1Char('*')) r += QStringLiteral(".*");
    else if (c == QLatin1Char('?')) r += QLatin1Char('.');
    else r += QRegularExpression::escape(QString(c));
  }
  return r;
}
// Les flèches du champ de recherche passent d'un résultat à l'autre
class FlechesResultats : public QObject {
 public:
  FlechesResultats(QLineEdit* e, PanneauRecherche& p) : QObject(e), m_p(p) {}
  bool eventFilter(QObject*, QEvent* ev) override {
    if (ev->type() != QEvent::KeyPress) return false;
    const int k = static_cast<QKeyEvent*>(ev)->key();
    if (k != Qt::Key_Up && k != Qt::Key_Down) return false;  // champ de saisie
    m_p.suivant(k == Qt::Key_Down ? 1 : -1);  // champ de saisie
    return true;
  }

 private:
  PanneauRecherche& m_p;
};
}  // namespace

PanneauRecherche::PanneauRecherche(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  setObjectName(QStringLiteral("panneauRecherche"));
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(6, 6, 6, 6);
  auto* ligne = new QHBoxLayout;
  m_champ = new QLineEdit;
  m_champ->setObjectName(QStringLiteral("champRecherche"));
  m_champ->setPlaceholderText(QStringLiteral("Chercher : repère, valeur, réseau, étiquette, texte…"));
  m_champ->setClearButtonEnabled(true);
  m_champ->installEventFilter(new FlechesResultats(m_champ, *this));
  ligne->addWidget(m_champ, 1);
  auto fleche = [&](const char* nom, const QString& texte, const QString& bulle, int sens) {
    auto* b = new QToolButton;
    b->setObjectName(QLatin1String(nom));
    b->setText(texte);
    b->setToolTip(bulle);
    b->setAutoRaise(true);
    connect(b, &QToolButton::clicked, this, [this, sens] { suivant(sens); });
    ligne->addWidget(b);
  };
  fleche("resultatPrecedent", QStringLiteral("▲"), QStringLiteral("Résultat précédent (↑ dans le champ)"), -1);
  fleche("resultatSuivant", QStringLiteral("▼"), QStringLiteral("Résultat suivant (Entrée ou ↓ dans le champ)"), 1);
  col->addLayout(ligne);
  auto* options = new QHBoxLayout;
  auto option = [&](const char* nom, const QString& texte, const QString& bulle) {
    auto* c = new QCheckBox(texte);
    c->setObjectName(QLatin1String(nom));
    c->setToolTip(bulle);
    connect(c, &QCheckBox::toggled, this, [this] { majResultats(); });
    options->addWidget(c);
    return c;
  };
  m_casse = option("rechercheCasse", QStringLiteral("Casse"), QStringLiteral("Majuscules et minuscules distinguées"));
  m_motEntier = option("rechercheMotEntier", QStringLiteral("Mot entier"), QStringLiteral("R1 ne trouve pas R10"));
  m_jokers = option("rechercheJokers", QStringLiteral("Jokers * ?"), QStringLiteral("* : une suite de caractères ; ? : un seul"));
  m_regex = option("rechercheRegex", QStringLiteral("Expression régulière"), QStringLiteral("Le texte cherché est une expression régulière"));
  m_approche = option("rechercheApprochee", QStringLiteral("Approchée"), QStringLiteral("Trouve aussi un texte à une ou deux fautes près (GDN trouve GND)"));
  // Genres d'objets cherchés et portée (2.27)
  m_genresCherches.fill(true);
  m_genres = new QToolButton;
  m_genres->setObjectName(QStringLiteral("rechercheGenres"));
  m_genres->setText(QStringLiteral("Quoi"));
  m_genres->setToolTip(QStringLiteral("Genres d’objets cherchés"));
  m_genres->setPopupMode(QToolButton::InstantPopup);
  auto* menu = new QMenu(m_genres);
  for (int g = 0; g < GENRES; ++g) {
    QAction* x = menu->addAction(nomGenre(static_cast<Genre>(g)));
    x->setCheckable(true);
    x->setChecked(true);
    connect(x, &QAction::toggled, this, [this, g](bool oui) { setGenreCherche(static_cast<Genre>(g), oui); });
  }
  m_genres->setMenu(menu);
  options->addWidget(m_genres);
  m_portee = new QComboBox;
  m_portee->setObjectName(QStringLiteral("recherchePortee"));
  m_portee->addItems({QStringLiteral("Schéma et circuit"), QStringLiteral("Schéma"), QStringLiteral("Circuit"), QStringLiteral("Feuille ouverte")});
  connect(m_portee, &QComboBox::currentIndexChanged, this, [this] { filtrer(); });
  options->addWidget(m_portee);
  options->addStretch(1);
  col->addLayout(options);
  auto* remplacer = new QHBoxLayout;
  m_remplacer = new QLineEdit;
  m_remplacer->setObjectName(QStringLiteral("champRemplacer"));
  m_remplacer->setPlaceholderText(QStringLiteral("Remplacer par…"));
  remplacer->addWidget(m_remplacer, 1);
  auto* tout = new QPushButton(QStringLiteral("Remplacer tout"));
  tout->setObjectName(QStringLiteral("remplacerTout"));
  tout->setToolTip(QStringLiteral("La partie trouvée, dans chaque résultat (sauf les réseaux), en un pas"));
  connect(tout, &QPushButton::clicked, this, [this] { remplacerTout(m_remplacer->text()); });
  remplacer->addWidget(tout);
  auto* choisir = new QPushButton(QStringLiteral("Tout choisir"));
  choisir->setObjectName(QStringLiteral("choisirResultats"));
  choisir->setToolTip(QStringLiteral("Tous les objets trouvés choisis ensemble, au schéma et au circuit : l’inspecteur les montre"));
  connect(choisir, &QPushButton::clicked, this, [this] { choisirTout(); });
  remplacer->addWidget(choisir);
  col->addLayout(remplacer);
  m_resume = inspecteur::texte();
  m_resume->setObjectName(QStringLiteral("resumeRecherche"));
  col->addWidget(m_resume);
  m_liste = new QTreeWidget;
  m_liste->setObjectName(QStringLiteral("resultatsRecherche"));
  m_liste->setHeaderLabels({QStringLiteral("Quoi"), QStringLiteral("Texte"), QStringLiteral("Feuille")});
  m_liste->setRootIsDecorated(false);
  m_liste->setUniformRowHeights(true);
  m_liste->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  connect(m_liste, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) { aller(m_liste->indexOfTopLevelItem(it)); });
  connect(m_liste, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) { aller(m_liste->indexOfTopLevelItem(it)); });
  col->addWidget(m_liste, 1);
  connect(m_champ, &QLineEdit::textChanged, this, [this] { filtrer(); });
  connect(m_champ, &QLineEdit::returnPressed, this, [this] { suivant(1); });
}

void PanneauRecherche::focaliser() {
  m_champ->setFocus(Qt::ShortcutFocusReason);
  m_champ->selectAll();
}

void PanneauRecherche::chercher(const QString& texte) {
  m_champ->setText(texte);
  filtrer();
}

void PanneauRecherche::setPortee(Portee p) { m_portee->setCurrentIndex(static_cast<int>(p)); }

void PanneauRecherche::setGenreCherche(Genre g, bool oui) {
  m_genresCherches[static_cast<std::size_t>(g)] = oui;
  if (QMenu* m = m_genres->menu())
    if (QAction* x = m->actions().value(static_cast<int>(g)); x && x->isChecked() != oui) {
      const QSignalBlocker b(x);
      x->setChecked(oui);
    }
  const int n = static_cast<int>(std::count(m_genresCherches.begin(), m_genresCherches.end(), true));
  m_genres->setText(n == GENRES ? QStringLiteral("Quoi") : QStringLiteral("Quoi (%1)").arg(n));
  filtrer();
}

void PanneauRecherche::setApproche(bool oui) { m_approche->setChecked(oui); }

// Distance d'édition de Damerau-Levenshtein restreinte : insertion, suppression, substitution, et deux lettres voisines
// inversées comptées pour une faute (GDN pour GND) ; arrêtée dès qu'elle dépasse la borne
int PanneauRecherche::distanceEdition(const QString& a, const QString& b, int borne) {
  if (std::abs(static_cast<int>(a.size() - b.size())) > borne) return borne + 1;
  const std::size_t n = static_cast<std::size_t>(b.size()) + 1;
  std::vector<int> avantAvant(n), avant(n), ici(n);
  for (std::size_t j = 0; j < n; ++j) avant[j] = static_cast<int>(j);
  for (int i = 1; i <= a.size(); ++i) {
    ici[0] = i;
    int mini = i;
    for (int j = 1; j <= b.size(); ++j) {
      const auto u = static_cast<std::size_t>(j);
      const int sub = avant[u - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
      int d = std::min({avant[u] + 1, ici[u - 1] + 1, sub});
      if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) d = std::min(d, avantAvant[u - 2] + 1);
      ici[u] = d;
      mini = std::min(mini, d);
    }
    if (mini > borne) return borne + 1;
    avantAvant = avant;
    std::swap(avant, ici);
  }
  return avant.back();
}

QRegularExpression PanneauRecherche::motif() const {
  const QString t = m_champ->text();
  if (t.isEmpty()) return QRegularExpression();
  QString m = m_regex->isChecked() ? t : m_jokers->isChecked() ? jokersVersMotif(t) : QRegularExpression::escape(t);
  if (m_motEntier->isChecked()) m = QStringLiteral("\\b(?:%1)\\b").arg(m);
  QRegularExpression r(m, m_casse->isChecked() ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
  return r.isValid() ? r : QRegularExpression();
}

void PanneauRecherche::majResultats() {
  m_indexPerime = true;
  filtrer();
}

void PanneauRecherche::indexer() {
  m_index.clear();
  m_indexPerime = false;
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  std::set<std::string> composantsVus;
  for (std::size_t fi = 0; fi < c->feuilles.size(); ++fi) {
    const Feuille& f = c->feuilles[fi];
    const int feuille = static_cast<int>(fi);
    for (const auto& s : f.symboles) {
      if (s.composant.empty()) {
        // Symbole d'alimentation : son nom
        const DefSymbole* d = c->symbole(s.symbole);
        const std::string nom = d && d->alimentation ? (s.nomAlim.empty() ? d->nom : s.nomAlim) : std::string();
        if (!nom.empty()) m_index.push_back({Genre::Alimentation, feuille, s.uuid, nom, {}, s.position});
        continue;
      }
      // Un composant à plusieurs unités ne sort qu'une fois, à sa première pose
      if (!composantsVus.insert(s.composant).second) continue;
      const ComposantElec* k = c->composant(s.composant);
      if (!k) continue;
      m_index.push_back({Genre::Repere, feuille, k->uuid, k->reference, {}, s.position});
      if (!k->valeur.empty()) m_index.push_back({Genre::Valeur, feuille, k->uuid, k->valeur, {}, s.position});
      for (const auto& [champ, texte] : std::initializer_list<std::pair<const char*, const std::string*>>{{"fabricant", &k->fabricant}, {"mpn", &k->mpn}, {"fournisseur", &k->fournisseur}})
        if (!texte->empty()) m_index.push_back({Genre::Champ, feuille, k->uuid, *texte, champ, s.position});
    }
    for (const auto& e : f.etiquettes) m_index.push_back({Genre::Etiquette, feuille, e.uuid, e.texte, {}, e.p});
    for (const auto& t : f.textes) m_index.push_back({Genre::Texte, feuille, t.uuid, t.texte, {}, t.p});
  }
  // Les réseaux, une fois par feuille où ils passent (2.27), là où passe leur premier fil
  const Connectivite& k = m_atelier.calculs().k;
  for (std::size_t i = 0; i < k.reseaux.size(); ++i) {
    const Reseau& res = k.reseaux[i];
    if (res.broches.size() < 2 && res.alimentations.empty()) continue;
    for (std::size_t fi = 0; fi < c->feuilles.size(); ++fi)
      for (const auto& w : c->feuilles[fi].fils)
        if (const auto it = k.parFil.find(w.uuid); it != k.parFil.end() && it->second == static_cast<int>(i)) {
          m_index.push_back({Genre::Reseau, static_cast<int>(fi), res.nom, res.nom, {}, w.a});
          break;
        }
  }
  // Le circuit : zones, vias et pistes d'un réseau (groupées), textes de la carte
  if (!c->schemaSeul) {
    const Circuit& ci = c->circuit;
    for (const auto& z : ci.zones)
      if (!z.reseau.empty()) m_index.push_back({Genre::Zone, -1, z.uuid, z.reseau, nomCouche(z.couche), z.contour.empty() ? Pt{} : z.contour.front()});
    std::map<std::string, Trouve> vias, pistes;
    for (const auto& v : ci.vias) {
      Trouve& t = vias[v.reseau];
      if (t.ids.empty()) t = {Genre::Vias, -1, v.reseau, v.reseau, {}, v.p};
      t.ids.push_back(v.uuid);
    }
    for (const auto& p : ci.pistes) {
      Trouve& t = pistes[p.reseau];
      if (t.ids.empty()) t = {Genre::Pistes, -1, p.reseau, p.reseau, {}, p.a};
      t.ids.push_back(p.uuid);
    }
    for (auto* groupe : {&vias, &pistes})
      for (auto& [nom, t] : *groupe)
        if (!nom.empty()) {
          t.champ = std::to_string(t.ids.size());
          m_index.push_back(std::move(t));
        }
    for (const auto& t : ci.textes) m_index.push_back({Genre::TexteCarte, -1, t.uuid, t.texte, {}, t.p});
  }
}

void PanneauRecherche::filtrer() {
  if (m_indexPerime) indexer();
  m_resultats.clear();
  m_liste->clear();
  m_courant = -1;
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const QRegularExpression r = motif();
  if (!c || m_champ->text().isEmpty()) {
    m_resume->setText(QString());
    return;
  }
  if (!r.isValid() || r.pattern().isEmpty()) {
    m_resume->setText(QStringLiteral("Expression invalide"));
    return;
  }
  const QString cherche = m_champ->text();
  const Qt::CaseSensitivity cs = m_casse->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
  // Approchée : le texte entier à une ou deux fautes près (une jusqu'à 4 caractères, deux jusqu'à 8, trois au-delà)
  const bool approche = m_approche->isChecked() && !m_regex->isChecked() && !m_jokers->isChecked();
  const int borne = cherche.size() <= 4 ? 1 : cherche.size() <= 8 ? 2 : 3;
  const QString chercheBas = cs == Qt::CaseSensitive ? cherche : cherche.toLower();
  auto trouve = [&](const std::string& t) {
    if (t.empty()) return false;
    const QString x = q(t);
    if (r.match(x).hasMatch()) return true;
    return approche && distanceEdition(cs == Qt::CaseSensitive ? x : x.toLower(), chercheBas, borne) <= borne;
  };
  const Portee portee = static_cast<Portee>(m_portee->currentIndex());
  const int ouverte = m_atelier.vueSchema() ? m_atelier.vueSchema()->feuille() : 0;
  for (const Trouve& x : m_index) {
    if (!m_genresCherches[static_cast<std::size_t>(x.genre)]) continue;
    if ((portee == Portee::Schema || portee == Portee::Feuille) && x.feuille < 0) continue;
    if (portee == Portee::Circuit && x.feuille >= 0) continue;
    if (portee == Portee::Feuille && x.feuille != ouverte) continue;
    if (trouve(x.texte)) m_resultats.push_back(x);
  }
  // Le texte entier d'abord (R12 avant R120), puis le genre, l'ordre des feuilles, le circuit à la fin
  std::stable_sort(m_resultats.begin(), m_resultats.end(), [&](const Trouve& a, const Trouve& b) {
    const bool ea = q(a.texte).compare(cherche, cs) == 0, eb = q(b.texte).compare(cherche, cs) == 0;
    if (ea != eb) return ea;
    if (a.genre != b.genre) return a.genre < b.genre;
    return a.feuille < b.feuille;
  });
  for (const Trouve& x : m_resultats) {
    auto* it = new QTreeWidgetItem(m_liste);
    QString quoi = nomGenre(x.genre);
    if (x.genre == Genre::Vias || x.genre == Genre::Pistes) quoi = QStringLiteral("%1 (%2)").arg(quoi, q(x.champ));
    else if (!x.champ.empty()) quoi += QStringLiteral(" (%1)").arg(q(x.champ));
    it->setText(0, quoi);
    it->setText(1, q(x.texte));
    it->setText(2, x.feuille >= 0 && x.feuille < static_cast<int>(c->feuilles.size()) ? q(c->feuilles[static_cast<std::size_t>(x.feuille)].nom) : QStringLiteral("Circuit"));
  }
  m_resume->setText(m_resultats.empty() ? QStringLiteral("Rien trouvé") : QStringLiteral("%1 résultat(s) · Entrée ou ▼ : le suivant").arg(m_resultats.size()));
}

bool PanneauRecherche::aller(int i) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c || i < 0 || i >= static_cast<int>(m_resultats.size())) return false;
  m_courant = i;
  const Trouve x = m_resultats[static_cast<std::size_t>(i)];
  if (QTreeWidgetItem* it = m_liste->topLevelItem(i)) {
    const QSignalBlocker b(m_liste);
    m_liste->setCurrentItem(it);
    m_liste->scrollToItem(it);
  }
  switch (x.genre) {
    case Genre::Repere:
    case Genre::Valeur:
    case Genre::Champ: m_atelier.montrerComposant(x.objet); break;
    case Genre::Reseau:
      m_atelier.ouvrir(m_atelier.conception(), AtelierElectronique::Schema, x.feuille);
      m_atelier.surligner(q(x.objet));
      m_atelier.vueSchema()->centrerSur(x.position);
      break;
    case Genre::Zone:
    case Genre::Vias:
    case Genre::Pistes:
    case Genre::TexteCarte: {
      // Au circuit : les objets choisis, la vue centrée sur le premier ; le réseau surligné
      using G = SelectionElectronique::Genre;
      const G g = x.genre == Genre::Zone ? G::Zone : x.genre == Genre::Vias ? G::Via : x.genre == Genre::Pistes ? G::Piste : G::TexteCarte;
      m_atelier.ouvrir(m_atelier.conception(), AtelierElectronique::Circuit);
      std::vector<SelectionElectronique::Ref> refs;
      for (const std::string& id : x.ids.empty() ? std::vector<std::string>{x.objet} : x.ids) refs.push_back({g, id, {}});
      m_atelier.selection()->choisir(refs);
      if (x.genre != Genre::TexteCarte) m_atelier.surligner(q(x.texte));
      if (VueCircuit* v = m_atelier.vueCircuit()) v->centrerSur(x.position);
      break;
    }
    default:
      m_atelier.ouvrir(m_atelier.conception(), AtelierElectronique::Schema, x.feuille);
      m_atelier.vueSchema()->selectionner({x.objet});
      m_atelier.vueSchema()->centrerSur(x.position);
      break;
  }
  return true;
}

bool PanneauRecherche::suivant(int sens) {
  if (m_resultats.empty()) return false;
  const int n = static_cast<int>(m_resultats.size());
  return aller(m_courant < 0 ? (sens > 0 ? 0 : n - 1) : ((m_courant + sens) % n + n) % n);
}

int PanneauRecherche::remplacerTout(const QString& par) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const QRegularExpression r = motif();
  if (!c || !r.isValid() || r.pattern().isEmpty() || m_resultats.empty()) return 0;
  // Les repères pris, pour refuser un double (« R101 existe déjà · passé »)
  std::set<std::string> pris;
  for (const auto& k : c->composants) pris.insert(k.reference);
  std::vector<Trouve> faits;
  QStringList passes;
  for (const Trouve& x : m_resultats) {
    // Un réseau se renomme par ses étiquettes ; une zone, des vias ou des pistes portent le nom d'un réseau
    if (x.genre == Genre::Reseau || x.genre == Genre::Zone || x.genre == Genre::Vias || x.genre == Genre::Pistes) continue;
    const std::string nouveau = q(x.texte).replace(r, par).toStdString();
    if (nouveau == x.texte) continue;
    if (x.genre == Genre::Repere) {
      if (nouveau.empty() || pris.count(nouveau)) {
        passes << QStringLiteral("%1 existe déjà").arg(q(nouveau));
        continue;
      }
      pris.erase(x.texte);
      pris.insert(nouveau);
    }
    Trouve y = x;
    y.texte = nouveau;
    faits.push_back(y);
  }
  if (!faits.empty())
    m_atelier.modifierConception([&](ConceptionElec& cc) {
      for (const Trouve& y : faits) {
        if (y.genre == Genre::Repere || y.genre == Genre::Valeur || y.genre == Genre::Champ) {
          for (auto& k : cc.composants)
            if (k.uuid == y.objet) {
              if (y.genre == Genre::Repere) k.reference = y.texte;
              else if (y.genre == Genre::Valeur) k.valeur = y.texte;
              else if (y.champ == "fabricant") k.fabricant = y.texte;
              else if (y.champ == "mpn") k.mpn = y.texte;
              else if (y.champ == "fournisseur") k.fournisseur = y.texte;
            }
          continue;
        }
        if (y.genre == Genre::TexteCarte) {
          for (auto& t : cc.circuit.textes)
            if (t.uuid == y.objet) t.texte = y.texte;
          continue;
        }
        Feuille& f = cc.feuilles[static_cast<std::size_t>(y.feuille)];
        for (auto& e : f.etiquettes)
          if (e.uuid == y.objet) e.texte = y.texte;
        for (auto& t : f.textes)
          if (t.uuid == y.objet) t.texte = y.texte;
        for (auto& s : f.symboles)
          if (s.uuid == y.objet) s.nomAlim = y.texte;
      }
    }, QStringLiteral("Remplacer « %1 » (%2)").arg(m_champ->text()).arg(faits.size()));
  m_atelier.afficherMessage(QStringLiteral("%1 remplacement(s)").arg(faits.size()) + (passes.isEmpty() ? QString() : QStringLiteral(" · passés : %1").arg(passes.join(QStringLiteral(", ")).toHtmlEscaped())),
                            passes.isEmpty() ? NiveauMessage::Information : NiveauMessage::Avertissement);
  majResultats();
  return static_cast<int>(faits.size());
}

int PanneauRecherche::choisirTout() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c || m_resultats.empty()) return 0;
  using G = SelectionElectronique::Genre;
  std::vector<SelectionElectronique::Ref> refs;
  std::set<std::string> composants;
  for (const Trouve& x : m_resultats) {
    switch (x.genre) {
      case Genre::Repere:
      case Genre::Valeur:
      case Genre::Champ:
        // Le composant par tous ses symboles, sur toutes les feuilles
        if (composants.insert(x.objet).second)
          for (const auto& f : c->feuilles)
            for (const auto& s : f.symboles)
              if (s.composant == x.objet) refs.push_back({G::Symbole, s.uuid, s.composant});
        break;
      case Genre::Alimentation: refs.push_back({G::Symbole, x.objet, {}}); break;
      case Genre::Etiquette: refs.push_back({G::Etiquette, x.objet, {}}); break;
      case Genre::Texte: refs.push_back({G::TexteSchema, x.objet, {}}); break;
      case Genre::Reseau:
        if (x.feuille >= 0 && x.feuille < static_cast<int>(c->feuilles.size()))
          for (const auto& w : c->feuilles[static_cast<std::size_t>(x.feuille)].fils)
            if (const auto it = m_atelier.calculs().k.parFil.find(w.uuid); it != m_atelier.calculs().k.parFil.end() && m_atelier.calculs().k.reseaux[static_cast<std::size_t>(it->second)].nom == x.objet)
              refs.push_back({G::Fil, w.uuid, {}});
        break;
      case Genre::Zone: refs.push_back({G::Zone, x.objet, {}}); break;
      case Genre::Vias:
        for (const auto& id : x.ids) refs.push_back({G::Via, id, {}});
        break;
      case Genre::Pistes:
        for (const auto& id : x.ids) refs.push_back({G::Piste, id, {}});
        break;
      case Genre::TexteCarte: refs.push_back({G::TexteCarte, x.objet, {}}); break;
    }
  }
  m_atelier.selection()->choisir(refs);
  m_atelier.afficherMessage(QStringLiteral("%1 objet(s) trouvé(s) choisi(s) ensemble : l’inspecteur les montre.").arg(refs.size()));
  return static_cast<int>(refs.size());
}

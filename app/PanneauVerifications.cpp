// Prométhée : panneau Vérifications de l'atelier électronique (lot L2, tâches 2.11 et 2.33). Licence GPL-3.0-only.
#include "PanneauVerifications.hpp"

#include <algorithm>
#include <map>

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "AideElectronique.hpp"
#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "InspecteurElectronique.hpp"
#include "RegistreActions.hpp"
#include "Theme.hpp"
#include "VueCircuit.hpp"
#include "VueSchema.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
constexpr int ROLE_INDICE = Qt::UserRole + 1;
constexpr int ROLE_IGNORE = Qt::UserRole + 2;  // nœud d'un contrôle ignoré dans ce projet (2.33)
}  // namespace

PanneauVerifications::PanneauVerifications(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  setObjectName(QStringLiteral("panneauVerifications"));
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(6, 6, 6, 6);
  auto* tete = new QHBoxLayout;
  auto* verifier = new QPushButton(QStringLiteral("Vérifier"));
  verifier->setObjectName(QStringLiteral("verifierTout"));
  verifier->setToolTip(QStringLiteral("Vérification complète : ERC et DRC (F7)"));
  marquerAction(verifier, "schema.erc");
  connect(verifier, &QPushButton::clicked, this, [this] { m_atelier.executerAction("schema.erc"); });
  tete->addWidget(verifier);
  auto* suivant = new QPushButton(QStringLiteral("Suivant"));
  suivant->setObjectName(QStringLiteral("problemeSuivant"));
  suivant->setToolTip(QStringLiteral("Le problème suivant de la liste, montré dans sa vue (F4)"));
  marquerAction(suivant, "verif.suivant");
  connect(suivant, &QPushButton::clicked, this, [this] { m_atelier.executerAction("verif.suivant"); });
  tete->addWidget(suivant);
  m_filtre = new QComboBox;
  m_filtre->setObjectName(QStringLiteral("filtreVerifications"));
  m_filtre->addItem(QStringLiteral("Tout"));
  m_filtre->addItem(QStringLiteral("Erreurs"));
  m_filtre->addItem(QStringLiteral("Avertissements"));
  m_filtre->addItem(QStringLiteral("Exclus et ignorés"));
  connect(m_filtre, &QComboBox::currentIndexChanged, this, [this] { majResultats(); });
  tete->addWidget(m_filtre);
  m_resume = inspecteur::texte();
  tete->addWidget(m_resume, 1);
  col->addLayout(tete);
  m_liste = new QTreeWidget;
  m_liste->setObjectName(QStringLiteral("listeVerifications"));
  m_liste->setHeaderLabels({QStringLiteral("Problème"), QStringLiteral("Où")});
  m_liste->header()->setStretchLastSection(false);
  m_liste->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_liste->setContextMenuPolicy(Qt::CustomContextMenu);
  m_liste->setUniformRowHeights(true);
  connect(m_liste, &QWidget::customContextMenuRequested, this, &PanneauVerifications::menuElement);
  // Un clic, ou Entrée au clavier, mène au problème (P29 : les liens s'atteignent aussi au clavier)
  auto allerA = [this](QTreeWidgetItem* it) {
    if (!it || !it->data(0, ROLE_INDICE).isValid()) return;
    const int i = it->data(0, ROLE_INDICE).toInt();
    if (i < 0 || i >= static_cast<int>(m_affichees.size())) return;
    const auto [v, erc] = m_affichees[static_cast<std::size_t>(i)];  // copie : aller peut refaire la liste
    aller(v, erc);
  };
  connect(m_liste, &QTreeWidget::itemClicked, this, allerA);
  connect(m_liste, &QTreeWidget::itemActivated, this, allerA);
  col->addWidget(m_liste, 1);
  // Exclusion : la raison, tapée dans le panneau
  m_exclusion = new QWidget;
  auto* ligne = new QHBoxLayout(m_exclusion);
  ligne->setContentsMargins(0, 0, 0, 0);
  ligne->addWidget(new QLabel(QStringLiteral("Raison")));
  m_raison = new QLineEdit;
  m_raison->setObjectName(QStringLiteral("raisonExclusion"));
  m_raison->setPlaceholderText(QStringLiteral("pourquoi cette alerte est voulue"));
  ligne->addWidget(m_raison, 1);
  m_valider = new QPushButton(QStringLiteral("Exclure"));
  m_valider->setObjectName(QStringLiteral("validerExclusion"));
  ligne->addWidget(m_valider);
  auto* renoncer = new QPushButton(QStringLiteral("Annuler"));
  connect(renoncer, &QPushButton::clicked, m_exclusion, &QWidget::hide);
  ligne->addWidget(renoncer);
  m_exclusion->hide();
  col->addWidget(m_exclusion);
  majResultats();
}

void PanneauVerifications::majPlusTard() {
  if (m_majPrevue) return;
  m_majPrevue = true;
  QTimer::singleShot(0, this, [this] {
    m_majPrevue = false;
    majResultats();
  });
}

void PanneauVerifications::majResultats() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  // L'élément choisi reste choisi : son code et son message
  QString codeChoisi, messageChoisi;
  if (QTreeWidgetItem* it = m_liste->currentItem(); it && it->data(0, ROLE_INDICE).isValid()) {
    const int i = it->data(0, ROLE_INDICE).toInt();
    if (i >= 0 && i < static_cast<int>(m_affichees.size())) {
      codeChoisi = q(m_affichees[static_cast<std::size_t>(i)].first.code);
      messageChoisi = q(m_affichees[static_cast<std::size_t>(i)].first.message);
    }
  }
  m_liste->clear();
  m_affichees.clear();
  if (!c) return;
  const Filtre filtre = static_cast<Filtre>(m_filtre->currentIndex());
  if (filtre == Filtre::Exclus) {
    // Les alertes exclues, de l'ERC comme de la DRC : celles qui seraient là sans leur exclusion
    ConceptionElec sans = *c;
    sans.erc.exclusions.clear();
    for (const Violation& v : verifierERC(sans, m_atelier.calculs().k, NiveauErc::Complet))
      if (exclusionDe(*c, v)) m_affichees.push_back({v, true});
    if (m_atelier.verificationFaite())
      for (const Violation& v : verifierDRC(*c, m_atelier.calculs().k, m_atelier.calculs().zones))
        if (exclusionDe(*c, v)) m_affichees.push_back({v, false});
  } else {
    for (const Violation& v : m_atelier.ercMontree()) m_affichees.push_back({v, true});
    for (const Violation& v : m_atelier.calculs().drc) m_affichees.push_back({v, false});
  }
  // Groupés par cause (2.33) : un nœud par code, dépliable, les erreurs en tête
  std::map<std::pair<int, std::string>, QTreeWidgetItem*> groupes;
  int erreurs = 0, avertissements = 0;
  QTreeWidgetItem* retrouve = nullptr;
  for (std::size_t i = 0; i < m_affichees.size(); ++i) {
    const auto& [v, erc] = m_affichees[i];
    const bool erreur = v.gravite == Gravite::Erreur;
    if (filtre == Filtre::Erreurs && !erreur) continue;
    if (filtre == Filtre::Avertissements && erreur) continue;
    (erreur ? erreurs : avertissements)++;
    QTreeWidgetItem*& g = groupes[{erreur ? 0 : 1, v.code}];
    if (!g) {
      g = new QTreeWidgetItem(m_liste);
      g->setFirstColumnSpanned(true);
      g->setData(0, Qt::UserRole, q(v.code));
    }
    auto* it = new QTreeWidgetItem(g);
    it->setText(0, q(v.message));
    QString ou;
    if (erc)
      for (const auto& f : c->feuilles)
        if (f.uuid == v.feuille) ou = q(f.nom);
    if (!erc) ou = QStringLiteral("circuit");
    it->setText(1, ou);
    it->setToolTip(0, q(v.conseil));
    it->setData(0, ROLE_INDICE, static_cast<int>(i));
    it->setForeground(0, QColor(erreur ? teintes::erreur() : teintes::alerte()));
    if (q(v.code) == codeChoisi && q(v.message) == messageChoisi) retrouve = it;
  }
  for (auto& [cle, g] : groupes) {
    const QString code = g->data(0, Qt::UserRole).toString();
    const QString titre = aide::titreRegle(code.toStdString());
    g->setText(0, QStringLiteral("%1 %2 · %3 (%4)").arg(cle.first == 0 ? QStringLiteral("✖") : QStringLiteral("⚠"), code, titre).arg(g->childCount()));
    g->setToolTip(0, aide::explicationRegle(code.toStdString()));
    g->setExpanded(g->childCount() <= 20);
  }
  // Les contrôles ignorés dans ce projet, chacun sur sa ligne, rétablis au clic droit (2.33)
  int ignores = 0;
  if (filtre == Filtre::Exclus)
    for (const auto& [code, g] : c->erc.gravites)
      if (g == GraviteReglee::Ignorer) {
        auto* it = new QTreeWidgetItem;
        m_liste->insertTopLevelItem(ignores++, it);
        it->setFirstColumnSpanned(true);
        it->setData(0, Qt::UserRole, q(code));
        it->setData(0, ROLE_IGNORE, true);
        it->setText(0, QStringLiteral("⊘ %1 · %2 · contrôle ignoré dans ce projet").arg(q(code), aide::titreRegle(code)));
        it->setToolTip(0, QStringLiteral("Clic droit : rétablir ce contrôle"));
        it->setForeground(0, QColor(teintes::secondaire()));
      }
  QString resume = filtre == Filtre::Exclus ? QStringLiteral("%1 alerte(s) exclue(s), %2 contrôle(s) ignoré(s)").arg(m_affichees.size()).arg(ignores)
                                            : QStringLiteral("%1 erreur(s), %2 avertissement(s)").arg(erreurs).arg(avertissements);
  // La vérification complète date d'avant la dernière modification : ses résultats sont « à refaire » (S9)
  if (m_atelier.verificationPerimee()) resume += QStringLiteral(" · <i>à refaire (F7)</i>");
  else if (!m_atelier.verificationFaite()) resume += QStringLiteral(" · vérification complète : F7");
  m_resume->setText(resume);
  if (retrouve) m_liste->setCurrentItem(retrouve);
}

bool PanneauVerifications::suivant() {
  // L'élément après le courant, dans l'ordre de l'arbre ; au bout, le premier
  std::vector<QTreeWidgetItem*> feuilles;
  for (int i = 0; i < m_liste->topLevelItemCount(); ++i)
    for (int j = 0; j < m_liste->topLevelItem(i)->childCount(); ++j) feuilles.push_back(m_liste->topLevelItem(i)->child(j));
  if (feuilles.empty()) return false;
  auto it = std::find(feuilles.begin(), feuilles.end(), m_liste->currentItem());
  QTreeWidgetItem* x = it == feuilles.end() || std::next(it) == feuilles.end() ? feuilles.front() : *std::next(it);
  if (x->parent()) x->parent()->setExpanded(true);
  m_liste->setCurrentItem(x);
  m_liste->scrollToItem(x);
  const auto [v, erc] = m_affichees[static_cast<std::size_t>(x->data(0, ROLE_INDICE).toInt())];
  aller(v, erc);
  return true;
}

void PanneauVerifications::aller(const Violation& v, bool erc) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  m_atelier.afficherMessage(QStringLiteral("<b>%1</b> — %2").arg(q(v.message).toHtmlEscaped(), q(v.conseil).toHtmlEscaped()));
  if (erc) {
    int feuille = -1;
    for (std::size_t i = 0; i < c->feuilles.size(); ++i)
      if (c->feuilles[i].uuid == v.feuille) feuille = static_cast<int>(i);
    if (feuille < 0)
      // Sans feuille (repère en double) : celle du premier symbole visé
      for (std::size_t i = 0; i < c->feuilles.size() && feuille < 0; ++i)
        for (const auto& s : c->feuilles[i].symboles)
          if (std::find(v.objets.begin(), v.objets.end(), s.composant) != v.objets.end()) feuille = static_cast<int>(i);
    if (feuille < 0) return;
    m_atelier.ouvrir(m_atelier.conception(), AtelierElectronique::Schema, feuille);
    // Les objets visés : symboles d'un composant, ou l'objet lui-même (étiquette, symbole d'alimentation)
    const Feuille& f = c->feuilles[static_cast<std::size_t>(feuille)];
    std::set<std::string> ids;
    for (const auto& id : v.objets) {
      for (const auto& s : f.symboles)
        if (s.uuid == id || s.composant == id) ids.insert(s.uuid);
      for (const auto& e : f.etiquettes)
        if (e.uuid == id) ids.insert(e.uuid);
    }
    VueSchema* vs = m_atelier.vueSchema();
    if (!ids.empty()) vs->selectionner(ids);
    if (v.position.x != 0 || v.position.y != 0) vs->centrerSur(v.position);
    return;
  }
  // DRC : le bon genre d'objet, pas toujours une empreinte (P30)
  m_atelier.setMode(AtelierElectronique::Circuit);
  VueCircuit* vc = m_atelier.vueCircuit();
  std::vector<VueCircuit::ObjetChoisi> sel;
  for (const auto& id : v.objets) {
    VueCircuit::Genre g = VueCircuit::Genre::Rien;
    for (const auto& p : c->circuit.pistes) if (p.uuid == id) g = VueCircuit::Genre::Piste;
    for (const auto& x : c->circuit.vias) if (x.uuid == id) g = VueCircuit::Genre::Via;
    for (const auto& z : c->circuit.zones) if (z.uuid == id) g = VueCircuit::Genre::Zone;
    for (const auto& t : c->circuit.textes) if (t.uuid == id) g = VueCircuit::Genre::Texte;
    for (const auto& e : c->circuit.empreintes) if (e.composant == id) g = VueCircuit::Genre::Empreinte;
    if (g != VueCircuit::Genre::Rien) sel.push_back({g, id});
  }
  if (!sel.empty()) vc->setSelection(sel);
  if (v.position.x != 0 || v.position.y != 0) vc->centrerSur(v.position);
}

QString PanneauVerifications::correction(const Violation& v) {
  if (v.code == "ERC-02" || v.code == "ERC-06") return QStringLiteral("Annoter les nouveaux composants");
  if (v.code == "ERC-10") return QStringLiteral("Poser une marque de non-connexion");
  if (v.code == "ERC-26") return QStringLiteral("Donner sa valeur…");
  if (v.code == "ERC-16" || v.code == "ERC-17" || v.code == "ERC-13") return QStringLiteral("Supprimer");
  return {};
}

void PanneauVerifications::corriger(const Violation& v) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  if (v.code == "ERC-02" || v.code == "ERC-06") {
    m_atelier.annoter(v.code == "ERC-06");
    return;
  }
  int feuille = -1;
  for (std::size_t i = 0; i < c->feuilles.size(); ++i)
    if (c->feuilles[i].uuid == v.feuille) feuille = static_cast<int>(i);
  if (feuille < 0) return;
  const Pt p = v.position;
  const std::vector<std::string> objets = v.objets;
  if (v.code == "ERC-10")
    m_atelier.modifierConception([&](ConceptionElec& cc) { cc.feuilles[static_cast<std::size_t>(feuille)].nonConnexions.push_back({p, nouvelUuid()}); }, QStringLiteral("Non connectée"));
  else if (v.code == "ERC-13")
    m_atelier.modifierConception([&](ConceptionElec& cc) { std::erase_if(cc.feuilles[static_cast<std::size_t>(feuille)].nonConnexions, [&](const NonConnexion& n) { return n.p == p; }); },
                                 QStringLiteral("Supprimer la marque"));
  else if (v.code == "ERC-16" || v.code == "ERC-17")
    m_atelier.modifierConception([&](ConceptionElec& cc) {
      Feuille& f = cc.feuilles[static_cast<std::size_t>(feuille)];
      std::erase_if(f.etiquettes, [&](const Etiquette& e) { return std::find(objets.begin(), objets.end(), e.uuid) != objets.end(); });
      std::erase_if(f.symboles, [&](const SymbolePlace& s) { return std::find(objets.begin(), objets.end(), s.uuid) != objets.end(); });
    }, QStringLiteral("Supprimer"));
  else if (v.code == "ERC-26") {
    aller(v, true);
    for (const auto& s : c->feuilles[static_cast<std::size_t>(feuille)].symboles)
      if (!objets.empty() && s.composant == objets.front()) {
        m_atelier.vueSchema()->editerChamp(s.uuid, true);
        return;
      }
  }
}

void PanneauVerifications::exclure(const Violation& v) {
  m_exclusion->show();
  m_raison->clear();
  m_raison->setFocus();
  disconnect(m_valider, nullptr, this, nullptr);
  disconnect(m_raison, nullptr, this, nullptr);
  auto faire = [this, v] {
    const QString raison = m_raison->text().trimmed();
    if (raison.isEmpty()) {
      m_raison->setPlaceholderText(QStringLiteral("une raison est demandée"));
      return;
    }
    ExclusionErc e;
    e.code = v.code;
    e.objets = v.objets;
    e.raison = raison.toStdString();
    e.auteur = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME")).toStdString();
    e.date = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
    m_atelier.modifierConception([&](ConceptionElec& c) { c.erc.exclusions.push_back(e); }, QStringLiteral("Exclure %1").arg(q(v.code)));
    m_exclusion->hide();
    refaire();
  };
  connect(m_valider, &QPushButton::clicked, this, faire);
  connect(m_raison, &QLineEdit::returnPressed, this, faire);
}

void PanneauVerifications::refaire() {
  if (m_atelier.verificationFaite()) m_atelier.verifier();
  else m_atelier.recalculer();
}

void PanneauVerifications::regler(const std::string& code, std::optional<GraviteReglee> g) {
  m_atelier.modifierConception([&](ConceptionElec& c) {
    if (g) c.erc.gravites[code] = *g;
    else c.erc.gravites.erase(code);
  }, QStringLiteral("Gravité de %1").arg(q(code)));
  refaire();
}

void PanneauVerifications::menuElement(const QPoint& p) {
  QTreeWidgetItem* it = m_liste->itemAt(p);
  if (!it) return;
  const bool groupe = !it->data(0, ROLE_INDICE).isValid();
  const std::string code = groupe ? it->data(0, Qt::UserRole).toString().toStdString() : m_affichees[static_cast<std::size_t>(it->data(0, ROLE_INDICE).toInt())].first.code;
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("menuVerification"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  if (it->data(0, ROLE_IGNORE).toBool()) {
    menu->addAction(QStringLiteral("Rétablir ce contrôle"), this, [this, code] { regler(code, std::nullopt); });
    menu->addAction(QStringLiteral("C’est quoi ?"), this, [this, code] { m_atelier.afficherMessage(QStringLiteral("<b>%1</b> · %2").arg(q(code), aide::explicationRegle(code))); });
    menu->popup(m_liste->viewport()->mapToGlobal(p));
    return;
  }
  if (!groupe) {
    const auto& [v, estErc] = m_affichees[static_cast<std::size_t>(it->data(0, ROLE_INDICE).toInt())];
    const Violation copie = v;
    menu->addAction(QStringLiteral("Aller au problème"), this, [this, copie, estErc] { aller(copie, estErc); });
    if (const QString c = correction(v); !c.isEmpty() && estErc) menu->addAction(c, this, [this, copie] { corriger(copie); });
    {  // ERC comme DRC : les exclusions du projet valent pour les deux (2.33)
      if (static_cast<Filtre>(m_filtre->currentIndex()) == Filtre::Exclus)
        menu->addAction(QStringLiteral("Rétablir l’alerte"), this, [this, copie] {
          m_atelier.modifierConception([&](ConceptionElec& c) {
            std::erase_if(c.erc.exclusions, [&](const ExclusionErc& e) { return e.code == copie.code && (e.objets.empty() || e.objets == copie.objets); });
          }, QStringLiteral("Rétablir %1").arg(q(copie.code)));
          refaire();
        });
      else
        menu->addAction(QStringLiteral("Exclure cette alerte…"), this, [this, copie] { exclure(copie); });
    }
  }
  {  // Gravités réglables à l'ERC comme à la DRC (2.33)
    QMenu* g = menu->addMenu(QStringLiteral("Gravité de %1 dans ce projet").arg(q(code)));
    g->addAction(QStringLiteral("Erreur"), this, [this, code] { regler(code, GraviteReglee::Erreur); });
    g->addAction(QStringLiteral("Avertissement"), this, [this, code] { regler(code, GraviteReglee::Avertissement); });
    g->addAction(QStringLiteral("Ignorer"), this, [this, code] { regler(code, GraviteReglee::Ignorer); });
    g->addSeparator();
    g->addAction(QStringLiteral("Par défaut"), this, [this, code] { regler(code, std::nullopt); });
  }
  menu->addAction(QStringLiteral("C’est quoi ?"), this, [this, code] { m_atelier.afficherMessage(QStringLiteral("<b>%1</b> · %2").arg(q(code), aide::explicationRegle(code))); });
  menu->popup(m_liste->viewport()->mapToGlobal(p));
}

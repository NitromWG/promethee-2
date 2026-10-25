// Prométhée : navigateur du projet. Licence GPL-3.0-only.
#include "Navigateur.hpp"

#include <algorithm>

#include <QFileInfo>
#include <map>
#include <QDateTime>
#include <QHeaderView>
#include <QMenu>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QHelpEvent>
#include <QRegularExpression>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidgetItemIterator>

#include "Icones.hpp"
#include "Theme.hpp"
#include "Qet.hpp"
#include "promethee/projet_partage.hpp"
#include "promethee/structure.hpp"
#include "promethee/piece.hpp"

using namespace prom;

namespace {
// Couleurs des métiers, tirées du logo (bleu, cyan, violet) ; l'électrique garde son orange
const QColor MECA(0x27, 0x60, 0xF1), ELEC(0x0E, 0x94, 0xB8), ELECTRIQUE(0xC2, 0x6E, 0x00), PROJET(0x5B, 0x4B, 0xF5);

// Liseré de couleur du métier à gauche de chaque ligne : on sait d'un coup d'œil à quel métier appartient l'élément.
class Delegue : public DelegueTheme {
public:
  using DelegueTheme::DelegueTheme;
  void paint(QPainter* g, const QStyleOptionViewItem& opt, const QModelIndex& i) const override {
    DelegueTheme::paint(g, opt, i);
    const QString detail = i.data(Navigateur::ROLE_DETAIL).toString();
    if (!detail.isEmpty()) {
      QFont f = opt.font;
      f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1));
      const QFontMetrics fmNom(i.data(Qt::FontRole).isValid() ? i.data(Qt::FontRole).value<QFont>() : opt.font), fm(f);
      const int occupe = opt.decorationSize.width() + 10 + fmNom.horizontalAdvance(i.data(Qt::DisplayRole).toString());
      const int largeur = fm.horizontalAdvance(detail);
      if (occupe + largeur + 16 < opt.rect.width()) {
        g->save();
        g->setFont(f);
        const QVariant couleurDetail = i.data(Qt::UserRole + 4);
        g->setPen(theme().texte(couleurDetail.isValid() ? couleurDetail.value<QColor>() : QColor(0x8A, 0x91, 0xAC)));
        g->drawText(opt.rect.adjusted(0, 0, -6, 0), Qt::AlignRight | Qt::AlignVCenter, detail);
        g->restore();
      }
    }
    const QVariant c = i.data(Navigateur::ROLE_COULEUR);
    if (!c.isValid()) return;
    g->save();
    g->setRenderHint(QPainter::Antialiasing);
    g->setPen(Qt::NoPen);
    g->setBrush(theme().texte(c.value<QColor>()));
    g->drawRoundedRect(QRectF(opt.rect.left() - 3, opt.rect.top() + 4, 3, opt.rect.height() - 8), 1.5, 1.5);
    g->restore();
  }
  QSize sizeHint(const QStyleOptionViewItem& o, const QModelIndex& i) const override {
    QSize s = DelegueTheme::sizeHint(o, i);
    s.setHeight(std::max(s.height(), 26));
    return s;
  }
};

QTreeWidgetItem* element(QTreeWidgetItem* parent, const QString& texte, const QString& ic, const QString& cle, const QColor& couleur, const QString& detail = {}) {
  auto* it = new QTreeWidgetItem(parent);
  it->setText(0, texte);
  it->setToolTip(0, detail.isEmpty() ? texte : texte + QStringLiteral(" — ") + detail);
  if (!detail.isEmpty()) it->setData(0, Navigateur::ROLE_DETAIL, detail);
  it->setIcon(0, icone(ic));
  it->setData(0, Navigateur::ROLE_CLE, cle);
  it->setData(0, Navigateur::ROLE_COULEUR, couleur);
  return it;
}

QTreeWidgetItem* section(QTreeWidgetItem* parent, const QString& texte, const QString& ic, const QString& cle, const QColor& couleur) {
  auto* it = element(parent, texte, ic, cle, couleur);
  QFont f = it->font(0);
  f.setBold(true);
  it->setFont(0, f);
  it->setForeground(0, couleur.darker(115));
  return it;
}
}  // namespace

Navigateur::Navigateur(QWidget* parent) : QTreeWidget(parent) {
  setColumnCount(1);
  setHeaderHidden(true);
  setIconSize(QSize(20, 20));
  setIndentation(18);
  setAnimated(true);
  setUniformRowHeights(false);
  setItemDelegate(new Delegue(this));
  setContextMenuPolicy(Qt::CustomContextMenu);
  setMinimumWidth(240);
  setTextElideMode(Qt::ElideRight);
  setStyleSheet(QStringLiteral(
      "QTreeWidget { border: none; background: #FAFBFE; font-size: 13px; }"
      "QTreeWidget::item { padding: 2px 4px; border-radius: 5px; }"
      "QTreeWidget::item:hover { background: #EFF3FF; }"
      "QTreeWidget::item:selected { background: #DDE5FF; color: #10142A; }"));
  connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
    const QString cle = it->data(0, ROLE_CLE).toString();
    if (cle == QLatin1String("carte") || cle.startsWith(QLatin1String("carte:")) || cle == QLatin1String("boitier")) emit ouvrirCarte();
    else if (cle.startsWith(QLatin1String("piece:"))) {
      const QStringList p = cle.split(QLatin1Char(':'));
      const int index = p.value(1).toInt();
      if (p.size() >= 4 && p[2] == QLatin1String("f")) emit choisirFonctionPiece(index, p[3].toInt());
      else emit ouvrirPiece(index);
    } else if (cle.startsWith(QLatin1String("assemblage:"))) {
      const QStringList p = cle.split(QLatin1Char(':'));
      emit ouvrirAssemblage(p.value(1).toInt(), p.size() >= 4 ? p[3].toInt() : -1);
    } else if (cle.startsWith(QLatin1String("contrainteAssemblage:"))) emit ouvrirContrainteAssemblage(cle.section(QLatin1Char(':'), 1, 1).toInt(), cle.section(QLatin1Char(':'), 2, 2).toInt());
    else if (cle.startsWith(QLatin1String("schema:"))) emit ouvrirSchema(cle.section(QLatin1Char(':'), 1, 1).toInt());
    else if (cle == QLatin1String("nomenclature")) emit ouvrirNomenclature();
    else if (cle == QLatin1String("integration")) emit ouvrirIntegration();
    else if (cle.startsWith(QLatin1String("plan:"))) emit ouvrirPlan(cle.section(QLatin1Char(':'), 1, 1).toInt(), cle.section(QLatin1Char(':'), 2, 2).toInt());
    else if (cle.startsWith(QLatin1String("elec:"))) {
      const QStringList p = cle.split(QLatin1Char(':'));
      const int i = p.value(1).toInt();
      const QString quoi = p.value(2);
      if (quoi == QLatin1String("comp") && p.size() >= 4) emit ouvrirComposantElec(i, p[3]);
      else if (quoi == QLatin1String("emp") && p.size() >= 4) emit ouvrirEmpreinteElec(i, p[3]);
      else if (quoi == QLatin1String("reseau") && p.size() >= 4) emit surlignerReseauElec(i, cle.section(QLatin1Char(':'), 3));
      else if (quoi == QLatin1String("empreintes")) emit ouvrirElectronique(i, 1, 0);
      else if (quoi == QLatin1String("feuille")) emit ouvrirElectronique(i, 0, p.value(3).toInt());
      else if (quoi == QLatin1String("circuit")) emit ouvrirElectronique(i, 1, 0);
      else if (quoi == QLatin1String("3d")) emit ouvrirElectronique(i, 2, 0);
      else if (quoi == QLatin1String("fab")) emit ouvrirElectronique(i, 3, 0);
      else emit ouvrirElectronique(i, 0, 0);
    } else if (cle.startsWith(QLatin1String("electrique:"))) {
      const QStringList p = cle.split(QLatin1Char(':'));
      const int i = p.value(1).toInt();
      const QString quoi = p.value(2);
      if (quoi == QLatin1String("folio")) emit ouvrirElectrique(i, 0, p.value(3).toInt());
      else if (quoi == QLatin1String("armoire")) emit ouvrirElectrique(i, 1, 0);
      else if (quoi == QLatin1String("faisceau")) emit ouvrirElectrique(i, 2, 0);
      else if (quoi == QLatin1String("rapports")) emit ouvrirElectrique(i, 3, 0);
      else if (quoi == QLatin1String("calculs")) emit ouvrirElectrique(i, 4, 0);
      else if (quoi == QLatin1String("logement")) emit ouvrirElectrique(i, 5, 0);
      else if (quoi == QLatin1String("distribution")) emit ouvrirElectrique(i, 6, 0);
      else if (quoi == QLatin1String("photovoltaique")) emit ouvrirElectrique(i, 7, 0);
      else if (quoi == QLatin1String("grafcet")) emit ouvrirElectrique(i, 8, 0);
      else emit ouvrirElectrique(i, 0, 0);
    } else if (cle == QLatin1String("ajouter:electrique")) emit nouveauProjetElectrique();
    else if (cle == QLatin1String("ajouter:elec")) emit nouvelleConceptionElec();
    else if (cle == QLatin1String("ajouter:piece")) emit nouvellePiece();
    else if (cle == QLatin1String("ajouter:kicad")) emit importerKicadElectronique();  // l'import pour le boîtier reste au menu Fichier (G4)
  });
  // Éditeur refermé (validé ou abandonné) : l'élément redevient un élément ordinaire
  connect(itemDelegate(), &QAbstractItemDelegate::closeEditor, this, [this] {
    const QSignalBlocker bloque(this);
    for (QTreeWidgetItemIterator it(this); *it; ++it)
      if ((*it)->data(0, ROLE_RENOMMAGE).toBool()) {
        (*it)->setData(0, ROLE_RENOMMAGE, false);
        (*it)->setFlags((*it)->flags() & ~Qt::ItemIsEditable);
      }
  });
  // Nom d'une carte tapé sur place (G7) : seul l'élément mis en édition par « Renommer » est lu
  connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int colonne) {
    if (colonne != 0 || !it->data(0, ROLE_RENOMMAGE).toBool()) return;
    it->setData(0, ROLE_RENOMMAGE, false);
    const QString cle = it->data(0, ROLE_CLE).toString();
    const QString nom = it->text(0).trimmed();
    // Après coup : la modification reconstruit l'arbre, et l'élément en cours d'écriture avec lui
    if (cle.startsWith(QLatin1String("elec:")) && cle.count(QLatin1Char(':')) == 1 && !nom.isEmpty())
      QTimer::singleShot(0, this, [this, i = cle.section(QLatin1Char(':'), 1, 1).toInt(), nom] { emit renommerConceptionElec(i, nom); });
  });
  connect(this, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& p) {
    QTreeWidgetItem* it = itemAt(p);
    // Dans la pièce active, c'est l'atelier qui propose son menu (renommer, désactiver, déplacer…).
    for (QTreeWidgetItem* q = it; q; q = q->parent())
      if (q == m_pieceActive && it != m_pieceActive) { emit menuDansPieceActive(p); return; }
    QMenu menu(this);
    remplirMenu(menu, it ? it->data(0, ROLE_CLE).toString() : QString());
    menu.exec(viewport()->mapToGlobal(p));
  });
}

void Navigateur::remplirMenu(QMenu& menu, const QString& cle) {
  if (cle.startsWith(QLatin1String("piece:")) && cle.count(QLatin1Char(':')) == 1) {
    const int index = cle.section(QLatin1Char(':'), 1, 1).toInt();
    menu.addAction(icone(QStringLiteral("piece")), QStringLiteral("Ouvrir la pièce"), this, [this, index] { emit ouvrirPiece(index); });
    menu.addAction(icone(QStringLiteral("plan")), QStringLiteral("Nouvelle mise en plan"), this, [this, index] { emit nouveauPlan(index); });
    menu.addSeparator();
    menu.addAction(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer la pièce"), this, [this, index] { emit supprimerPiece(index); });
  } else if (cle.startsWith(QLatin1String("plan:"))) {
    // Mise en plan : ouvrir, renommer, supprimer (AP-30)
    const int i = cle.section(QLatin1Char(':'), 1, 1).toInt(), k = cle.section(QLatin1Char(':'), 2, 2).toInt();
    menu.addAction(icone(QStringLiteral("plan")), QStringLiteral("Ouvrir la mise en plan"), this, [this, i, k] { emit ouvrirPlan(i, k); });
    menu.addAction(QStringLiteral("Renommer…"), this, [this, i, k] { emit renommerPlan(i, k); });
    menu.addAction(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer…"), this, [this, i, k] { emit supprimerPlan(i, k); });
  } else if (cle == QLatin1String("carte") || cle.startsWith(QLatin1String("carte:"))) {
    menu.addAction(icone(QStringLiteral("kicad")), QStringLiteral("Ouvrir dans KiCad"), this, [this] { emit ouvrirDansKicad(); });
    menu.addAction(icone(QStringLiteral("kicad")), QStringLiteral("Recharger depuis KiCad…"), this, [this] { emit importerKicad(); });
  } else if (cle.startsWith(QLatin1String("assemblage:")) && cle.count(QLatin1Char(':')) == 1) {
    const int a = cle.section(QLatin1Char(':'), 1, 1).toInt();
    menu.addAction(icone(QStringLiteral("assemblage")), QStringLiteral("Ouvrir l’assemblage"), this, [this, a] { emit ouvrirAssemblage(a, -1); });
    menu.addAction(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer l’assemblage"), this, [this, a] { emit supprimerAssemblage(a); });
  } else if (cle.startsWith(QLatin1String("contrainteAssemblage:"))) {
    const int a = cle.section(QLatin1Char(':'), 1, 1).toInt(), k = cle.section(QLatin1Char(':'), 2, 2).toInt();
    menu.addAction(icone(QStringLiteral("contraintes")), QStringLiteral("Modifier"), this, [this, a, k] { emit ouvrirContrainteAssemblage(a, k); });
    menu.addAction(QStringLiteral("Suspendre ou réactiver"), this, [this, a, k] { emit basculerContrainteAssemblage(a, k); });
    menu.addAction(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer la contrainte"), this, [this, a, k] { emit supprimerContrainteAssemblage(a, k); });
  } else if (cle.startsWith(QLatin1String("elec:")) && cle.count(QLatin1Char(':')) >= 2) {
    // Menu propre à l'élément cliqué (G2) : un composant, une feuille ou une vue de la carte n'offre jamais de
    // supprimer la carte entière
    const QStringList p = cle.split(QLatin1Char(':'));
    const int i = p.value(1).toInt();
    const QString quoi = p.value(2);
    if (quoi == QLatin1String("comp") && p.size() >= 4) {
      const QString uuid = p[3];
      menu.addAction(icone(QStringLiteral("schema")), QStringLiteral("Montrer dans le schéma"), this, [this, i, uuid] { emit ouvrirComposantElec(i, uuid); });
      menu.addAction(icone(QStringLiteral("pcb")), QStringLiteral("Ouvrir le circuit imprimé"), this, [this, i] { emit ouvrirElectronique(i, 1, 0); });
      menu.addSeparator();
      menu.addAction(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer le composant"), this, [this, i, uuid] { emit supprimerComposantElec(i, uuid); });
    } else if (quoi == QLatin1String("feuille")) {
      const int f = p.value(3).toInt();
      menu.addAction(icone(QStringLiteral("plan")), QStringLiteral("Ouvrir la feuille"), this, [this, i, f] { emit ouvrirElectronique(i, 0, f); });
      menu.addSeparator();
      menu.addAction(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer la feuille"), this, [this, i, f] { emit supprimerFeuilleElec(i, f); });
    } else if (quoi == QLatin1String("circuit")) {
      menu.addAction(icone(QStringLiteral("pcb")), QStringLiteral("Ouvrir le circuit imprimé"), this, [this, i] { emit ouvrirElectronique(i, 1, 0); });
    } else if (quoi == QLatin1String("3d")) {
      menu.addAction(icone(QStringLiteral("piece")), QStringLiteral("Ouvrir la vue 3D"), this, [this, i] { emit ouvrirElectronique(i, 2, 0); });
    } else if (quoi == QLatin1String("fab")) {
      menu.addAction(icone(QStringLiteral("exporter")), QStringLiteral("Fabrication et publication"), this, [this, i] { emit ouvrirElectronique(i, 3, 0); });
    } else {
      menu.addAction(icone(QStringLiteral("schema")), QStringLiteral("Ouvrir le schéma"), this, [this, i] { emit ouvrirElectronique(i, 0, 0); });
    }
  } else if (cle.startsWith(QLatin1String("elec:"))) {
    const int i = cle.section(QLatin1Char(':'), 1, 1).toInt();
    menu.addAction(icone(QStringLiteral("schema")), QStringLiteral("Ouvrir le schéma"), this, [this, i] { emit ouvrirElectronique(i, 0, 0); });
    menu.addAction(icone(QStringLiteral("pcb")), QStringLiteral("Ouvrir le circuit imprimé"), this, [this, i] { emit ouvrirElectronique(i, 1, 0); });
    menu.addAction(icone(QStringLiteral("exporter")), QStringLiteral("Fabrication et publication"), this, [this, i] { emit ouvrirElectronique(i, 3, 0); });
    menu.addSeparator();
    menu.addAction(icone(QStringLiteral("cote")), QStringLiteral("Renommer"), this, [this, i] { editerNomConception(i); });
    menu.addAction(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer la carte"), this, [this, i] { emit supprimerConceptionElec(i); });
  } else if (cle.startsWith(QLatin1String("electrique:"))) {
    const int i = cle.section(QLatin1Char(':'), 1, 1).toInt();
    menu.addAction(icone(QStringLiteral("contacteur")), QStringLiteral("Ouvrir le schéma"), this, [this, i] { emit ouvrirElectrique(i, 0, 0); });
    menu.addAction(icone(QStringLiteral("armoire")), QStringLiteral("Armoire"), this, [this, i] { emit ouvrirElectrique(i, 1, 0); });
    menu.addAction(icone(QStringLiteral("distribution")), QStringLiteral("Installation (calcul)"), this, [this, i] { emit ouvrirElectrique(i, 6, 0); });
    menu.addAction(icone(QStringLiteral("photovoltaique")), QStringLiteral("Photovoltaïque"), this, [this, i] { emit ouvrirElectrique(i, 7, 0); });
    menu.addAction(icone(QStringLiteral("grafcet")), QStringLiteral("Grafcet"), this, [this, i] { emit ouvrirElectrique(i, 8, 0); });
    menu.addAction(icone(QStringLiteral("rapport")), QStringLiteral("Rapports"), this, [this, i] { emit ouvrirElectrique(i, 3, 0); });
    menu.addSeparator();
    menu.addAction(icone(QStringLiteral("cote")), QStringLiteral("Renommer…"), this, [this, i] { emit renommerProjetElectrique(i); });
    menu.addAction(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer le projet électrique"), this, [this, i] { emit supprimerProjetElectrique(i); });
  } else if (cle.startsWith(QLatin1String("schema:"))) {
    const int k = cle.section(QLatin1Char(':'), 1, 1).toInt();
    menu.addAction(icone(QStringLiteral("cablage")), QStringLiteral("Ouvrir dans QElectroTech"), this, [this, k] { emit ouvrirSchemaExterne(k); });
    menu.addAction(icone(QStringLiteral("supprimer")), QStringLiteral("Retirer du projet"), this, [this, k] { emit retirerSchema(k); });
  } else {
    menu.addAction(icone(QStringLiteral("piece")), QStringLiteral("Nouvelle pièce"), this, [this] { emit nouvellePiece(); });
    menu.addAction(icone(QStringLiteral("assemblage")), QStringLiteral("Nouvel assemblage"), this, [this] { emit nouvelAssemblage(); });
    menu.addAction(icone(QStringLiteral("pcb")), QStringLiteral("Nouvelle carte électronique"), this, [this] { emit nouvelleConceptionElec(); });
    menu.addAction(icone(QStringLiteral("kicad")), QStringLiteral("Importer une carte KiCad (électronique)…"), this, [this] { emit importerKicadElectronique(); });
    menu.addAction(icone(QStringLiteral("kicad")), QStringLiteral("Importer une carte KiCad pour le boîtier…"), this, [this] { emit importerKicad(); });
    menu.addAction(icone(QStringLiteral("contacteur")), QStringLiteral("Nouveau schéma électrique"), this, [this] { emit nouveauProjetElectrique(); });
    menu.addAction(icone(QStringLiteral("cablage")), QStringLiteral("Importer un projet QElectroTech…"), this, [this] { emit importerQetElectrique(); });
    menu.addAction(icone(QStringLiteral("cablage")), QStringLiteral("Relier un schéma QElectroTech (sans l’importer)…"), this, [this] { emit ajouterSchema(); });
  }
}

void Navigateur::retenirOuverts(QTreeWidgetItem* it) {
  if (!it) return;
  const QString cle = it->data(0, ROLE_CLE).toString();
  if (!cle.isEmpty()) {
    if (it->isExpanded()) m_ouverts.insert(cle);
    else m_ouverts.erase(cle);
  }
  for (int i = 0; i < it->childCount(); ++i) retenirOuverts(it->child(i));
}

std::string Navigateur::signatureConception(const prom::ecad::ConceptionElec& c) {
  constexpr char champ = '\x1f', ligne = '\x1e';
  std::string s = c.nom + champ + c.cartouche.revision + champ + (c.verrouillee ? '1' : '0') + (c.pilotageBoitier ? '1' : '0') + (c.schemaSeul ? '1' : '0') + champ + std::to_string(c.publications.size()) + champ +
                  (c.publications.empty() ? std::string() : c.publications.back().id) + champ + std::to_string(c.circuit.contour.largeur) + champ + std::to_string(c.circuit.contour.hauteur) + champ +
                  std::to_string(c.circuit.empilage.couchesCuivre) + ligne;
  for (const auto& f : c.feuilles) {
    s += f.nom + champ + f.format + ligne;
    for (const auto& e : f.etiquettes) s += e.texte + champ;  // réseaux nommés
    for (const auto& y : f.symboles) s += y.nomAlim + champ;
  }
  for (const auto& x : c.composants) s += x.uuid + champ;
  for (const auto& e : c.circuit.empreintes) s += e.composant + champ + e.empreinte + ligne;
  return s;
}

void Navigateur::majTextes(int conception, const prom::ecad::ConceptionElec& c) {
  const QString debut = QStringLiteral("elec:%1:comp:").arg(conception), debutEmp = QStringLiteral("elec:%1:emp:").arg(conception);
  const QSignalBlocker b(this);
  for (QTreeWidgetItemIterator it(this); *it; ++it) {
    const QString cle = (*it)->data(0, ROLE_CLE).toString();
    const bool comp = cle.startsWith(debut), emp = cle.startsWith(debutEmp);
    if (!comp && !emp) continue;
    const std::string uuid = cle.mid(comp ? debut.size() : debutEmp.size()).toStdString();
    const prom::ecad::ComposantElec* x = c.composant(uuid);
    if (!x) continue;
    const QString texte = QString::fromStdString(x->reference);
    const QString detail = comp ? QString::fromStdString(x->valeur) : QString::fromStdString(x->empreinte);
    if ((*it)->text(0) != texte) (*it)->setText(0, texte);
    if ((*it)->data(0, ROLE_DETAIL).toString() != detail) (*it)->setData(0, ROLE_DETAIL, detail);
    (*it)->setToolTip(0, texte + QStringLiteral(" — ") + detail);
  }
}

void Navigateur::suivreSelection(int conception, const std::set<std::string>& composants) {
  const QString debut = QStringLiteral("elec:%1:comp:").arg(conception);
  const QSignalBlocker b(this);
  QTreeWidgetItem* premier = nullptr;
  for (QTreeWidgetItemIterator it(this); *it; ++it) {
    const QString cle = (*it)->data(0, ROLE_CLE).toString();
    if (!cle.startsWith(debut)) continue;
    const bool choisi = composants.count(cle.mid(debut.size()).toStdString()) > 0;
    (*it)->setSelected(choisi);
    if (choisi && !premier) premier = *it;
  }
  if (premier) {
    setCurrentItem(premier, 0, QItemSelectionModel::NoUpdate);
    scrollToItem(premier);
  }
}

bool Navigateur::editerNomConception(int conception) {
  const QString voulue = QStringLiteral("elec:%1").arg(conception);
  for (QTreeWidgetItemIterator it(this); *it; ++it)
    if ((*it)->data(0, ROLE_CLE).toString() == voulue) {
      {
        const QSignalBlocker b(this);
        (*it)->setFlags((*it)->flags() | Qt::ItemIsEditable);
        (*it)->setData(0, ROLE_RENOMMAGE, true);
      }
      scrollToItem(*it);
      setCurrentItem(*it);
      editItem(*it, 0);
      return true;
    }
  return false;
}

void Navigateur::maj(const Projet& p, const QString& nomProjet, int pieceActive, bool atelierPieces, int conceptionElec, int projetElectrique) {
  if (!m_premiere)
    for (int i = 0; i < topLevelItemCount(); ++i) retenirOuverts(topLevelItem(i));
  const QSignalBlocker b(this);
  m_pieceActive = nullptr;
  QTreeWidgetItem* elecActive = nullptr;
  clear();
  auto* racine = new QTreeWidgetItem(this);
  racine->setText(0, nomProjet);
  racine->setIcon(0, icone(QStringLiteral("metier-mixte")));
  racine->setData(0, ROLE_CLE, QStringLiteral("projet"));
  QFont f = racine->font(0);
  f.setBold(true);
  f.setPointSizeF(f.pointSizeF() + 1);
  racine->setFont(0, f);
  const bool carte = !p.sansCarte;
  // Électronique : les cartes conçues ici (schéma, circuit imprimé, composants, 3D, fabrication), puis la carte du
  // boîtier (importée de KiCad, paramétrique, ou pilotée par une carte conçue ici).
  if (carte || !p.electronique.empty()) {
    QTreeWidgetItem* s = section(racine, QStringLiteral("Électronique"), QStringLiteral("metier-electronique"), QStringLiteral("section:elec"), ELEC);
    for (size_t i = 0; i < p.electronique.size(); ++i) {
      const auto& ce = p.electronique[i];
      const QString cle = QStringLiteral("elec:%1").arg(i);
      QString etat = ce.verrouillee ? QStringLiteral("🔒 %1 · rév. %2").arg(ce.publications.empty() ? QStringLiteral("publiée") : QString::fromStdString(ce.publications.back().id), QString::fromStdString(ce.cartouche.revision))
                                    : QStringLiteral("rév. %1").arg(QString::fromStdString(ce.cartouche.revision));
      QTreeWidgetItem* it = element(s, QString::fromStdString(ce.nom), QStringLiteral("pcb"), cle, ELEC, etat);
      if (static_cast<int>(i) == conceptionElec) {
        elecActive = it;
        QFont fi = it->font(0);
        fi.setBold(true);
        it->setFont(0, fi);
      }
      QTreeWidgetItem* sch = element(it, QStringLiteral("Schéma"), QStringLiteral("schema"), cle + QStringLiteral(":schema"), ELEC, QStringLiteral("%1 feuille(s)").arg(ce.feuilles.size()));
      for (size_t f = 0; f < ce.feuilles.size(); ++f) {
        auto* x = element(sch, QString::fromStdString(ce.feuilles[f].nom), QStringLiteral("plan"), cle + QStringLiteral(":feuille:%1").arg(f), ELEC, QString::fromStdString(ce.feuilles[f].format));
        x->setForeground(0, QColor(0x4A, 0x52, 0x70));
      }
      QTreeWidgetItem* comps = element(it, QStringLiteral("Composants"), QStringLiteral("composants"), cle + QStringLiteral(":composants"), ELEC, QString::number(ce.composants.size()));
      std::vector<const prom::ecad::ComposantElec*> tries;
      for (const auto& x : ce.composants) tries.push_back(&x);
      std::sort(tries.begin(), tries.end(), [](const auto* a, const auto* b) { return prom::ecad::ordreNaturel(a->reference, b->reference); });
      for (const auto* x : tries) {
        auto* y = element(comps, QString::fromStdString(x->reference), QStringLiteral("composants"), cle + QStringLiteral(":comp:") + QString::fromStdString(x->uuid), ELEC, QString::fromStdString(x->valeur));
        y->setForeground(0, QColor(0x23, 0x2A, 0x45));
      }
      // Réseaux nommés (3.11, G13) : par une étiquette ou une alimentation, avec leur nombre de broches
      {
        const prom::ecad::Connectivite kc = prom::ecad::connectivite(ce);
        std::vector<const prom::ecad::Reseau*> nommes;
        for (const auto& r : kc.reseaux)
          if (r.nomme && !r.broches.empty()) nommes.push_back(&r);
        std::sort(nommes.begin(), nommes.end(), [](const auto* a, const auto* b) { return prom::ecad::ordreNaturel(a->nom, b->nom); });
        QTreeWidgetItem* res = element(it, QStringLiteral("Réseaux"), QStringLiteral("reseau"), cle + QStringLiteral(":reseaux"), ELEC, QString::number(nommes.size()));
        for (const auto* r : nommes)
          element(res, QString::fromStdString(r->nom), QStringLiteral("reseau"), cle + QStringLiteral(":reseau:") + QString::fromStdString(r->nom), ELEC,
                  QStringLiteral("%1 broche(s)").arg(r->broches.size()));
      }
      // Schéma seul (STR-13) : ni circuit, ni 3D, ni fabrication tant que le circuit n'est pas créé
      if (ce.schemaSeul) {
        it->setIcon(0, icone(QStringLiteral("schema")));
        continue;
      }
      const auto& k = ce.circuit;
      QTreeWidgetItem* circ = element(it, QStringLiteral("Circuit imprimé"), QStringLiteral("piste"), cle + QStringLiteral(":circuit"), ELEC,
                                      QStringLiteral("%1 × %2 mm · %3 couches").arg(prom::ecad::mm(k.contour.largeur)).arg(prom::ecad::mm(k.contour.hauteur)).arg(k.empilage.couchesCuivre));
      // Empreintes placées (3.11, G13), triées par repère
      {
        std::vector<std::pair<std::string, const prom::ecad::EmpreintePlacee*>> emps;
        for (const auto& e : k.empreintes) {
          const prom::ecad::ComposantElec* x = ce.composant(e.composant);
          emps.push_back({x ? x->reference : std::string("?"), &e});
        }
        std::sort(emps.begin(), emps.end(), [](const auto& a, const auto& b) { return prom::ecad::ordreNaturel(a.first, b.first); });
        QTreeWidgetItem* ens = element(circ, QStringLiteral("Empreintes"), QStringLiteral("pcb"), cle + QStringLiteral(":empreintes"), ELEC, QString::number(emps.size()));
        for (const auto& [ref, e] : emps)
          element(ens, QString::fromStdString(ref), QStringLiteral("pcb"), cle + QStringLiteral(":emp:") + QString::fromStdString(e->composant), ELEC, QString::fromStdString(e->empreinte));
      }
      element(it, QStringLiteral("Vue 3D"), QStringLiteral("piece"), cle + QStringLiteral(":3d"), ELEC);
      element(it, QStringLiteral("Fabrication"), QStringLiteral("exporter"), cle + QStringLiteral(":fab"), ELEC,
              ce.publications.empty() ? QStringLiteral("non publiée") : QStringLiteral("%1 publication(s)").arg(ce.publications.size()));
    }
  }
  if (carte) {
    QTreeWidgetItem* s = nullptr;
    for (int i = 0; i < racine->childCount(); ++i)
      if (racine->child(i)->data(0, ROLE_CLE).toString() == QLatin1String("section:elec")) s = racine->child(i);
    std::string pilote;
    for (const auto& ce : p.electronique) if (ce.pilotageBoitier) pilote = ce.nom;
    const QString nomCarte = !pilote.empty() ? QStringLiteral("Carte du boîtier") : p.kicad ? QFileInfo(QString::fromStdString(p.kicad->fichier)).completeBaseName() : QStringLiteral("Carte");
    QTreeWidgetItem* c = element(s, nomCarte, QStringLiteral("pcb"), QStringLiteral("carte"), ELEC,
                                 !pilote.empty() ? QStringLiteral("suit « %1 »").arg(QString::fromStdString(pilote)) : p.kicad ? QStringLiteral("KiCad") : QStringLiteral("paramétrique"));
    // Carte pilotée par une carte conçue ici : ses composants sont déjà rangés sous elle, on ne les montre pas deux fois
    if (pilote.empty()) {
      QTreeWidgetItem* comps = element(c, QStringLiteral("Composants"), QStringLiteral("composants"), QStringLiteral("carte:composants"), ELEC, QString::number(p.composants.size()));
      for (const auto& k : p.composants) {
        auto* x = element(comps, QString::fromStdString(k.ref.empty() ? k.id : k.ref), QStringLiteral("composants"), QStringLiteral("carte:composant:") + QString::fromStdString(k.id), ELEC,
                          QString::fromStdString(k.valeur));
        x->setForeground(0, QColor(0x23, 0x2A, 0x45));
      }
    }
    element(c, QStringLiteral("Contour et trous"), QStringLiteral("pcb"), QStringLiteral("carte:contour"), ELEC, QString::number(p.trous.size()) + QStringLiteral(" trous"));
  }
  // Mécanique : le boîtier dérivé de la carte, puis chaque pièce, qui se déplie jusqu'à ses fonctions.
  if (carte || !p.pieces.empty() || !p.assemblages.empty()) {
    QTreeWidgetItem* s = section(racine, QStringLiteral("Mécanique"), QStringLiteral("metier-meca"), QStringLiteral("section:meca"), MECA);
    if (carte) {
      // Lien vérifié par la mécanique (12.20) : un contour changé depuis l'accusé de lecture marque le boîtier
      const auto revoir = prom::liensARevoir(p, "boitier");
      QTreeWidgetItem* b = element(s, QStringLiteral("Boîtier de la carte"), QStringLiteral("piece"), QStringLiteral("boitier"), MECA, revoir.empty() ? QStringLiteral("lié à la carte") : QStringLiteral("à revoir"));
      if (!revoir.empty()) {
        QString changements;
        for (const auto* l : revoir) {
          prom::Projet copie = p;
          prom::verifierLiens(copie);
          for (const auto& x : copie.liensDisciplines)
            if (x.id == l->id)
              for (const auto& c : x.changements) changements += QStringLiteral("\n") + QString::fromStdString(c);
        }
        b->setForeground(0, QColor(0xC5, 0x30, 0x30));
        b->setToolTip(0, QStringLiteral("Boîtier de la carte — à revoir jusqu’à l’accusé de lecture :") + changements);
      }
    }
    // Une pièce utilisée dans un assemblage ne s'affiche que là, comme composant ; un assemblage utilisé comme
    // sous-assemblage ne s'affiche que dans son parent. Au premier niveau : pièces libres et assemblages racines.
    std::vector<bool> pieceUtilisee(p.pieces.size(), false), assemblageUtilise(p.assemblages.size(), false);
    for (const auto& as : p.assemblages)
      for (const auto& c : as.composants) {
        if (c.assemblage >= 0 && c.assemblage < static_cast<int>(p.assemblages.size())) assemblageUtilise[static_cast<size_t>(c.assemblage)] = true;
        else if (c.piece >= 0 && c.piece < static_cast<int>(p.pieces.size())) pieceUtilisee[static_cast<size_t>(c.piece)] = true;
      }
    // Contenu d'une pièce (fonctions en lecture, mises en plan) sous un élément ; la pièce active est remplie par l'atelier
    auto contenuPiece = [&](QTreeWidgetItem* it, size_t i) {
      Piece pc;
      try { pc = pieceDepuisJson(p.pieces[i]); } catch (...) { return; }
      const QString cle = QStringLiteral("piece:%1").arg(i);
      if (atelierPieces && static_cast<int>(i) == pieceActive && !m_pieceActive) {
        m_pieceActive = it;  // l'atelier y range ses fonctions, interactives
        QFont fi = it->font(0);
        fi.setBold(true);
        it->setFont(0, fi);
        return;
      }
      for (const auto& fn : pc.fonctions) {
        const char* ic = fn.type == TypeFonction::Esquisse ? "esquisse" : fn.type == TypeFonction::Extrusion ? (fn.enlever ? "enlevement" : "extrusion")
                         : fn.type == TypeFonction::Revolution ? "revolution" : fn.type == TypeFonction::Conge ? "conge" : fn.type == TypeFonction::Chanfrein ? "chanfrein"
                         : fn.type == TypeFonction::Coque ? "coque" : fn.type == TypeFonction::Esquisse3D ? "esquisse3d" : fn.type == TypeFonction::Import ? "ouvrir"
                         : fn.type == TypeFonction::Tole ? "tolerie" : fn.type == TypeFonction::BordTombe ? "bord-tombe" : fn.type == TypeFonction::Nervure ? "nervure"
                         : fn.type == TypeFonction::Filetage ? "filetage" : "point";
        auto* x = element(it, QString::fromStdString(fn.nom), QString::fromLatin1(ic), cle + QStringLiteral(":f:%1").arg(fn.id), MECA);
        x->setForeground(0, QColor(0x4A, 0x52, 0x70));
      }
      if (!pc.plans.empty()) {
        QTreeWidgetItem* dossier = element(it, QStringLiteral("Mises en plan"), QStringLiteral("dossier"), cle + QStringLiteral(":plans"), MECA);
        for (size_t k = 0; k < pc.plans.size(); ++k) element(dossier, QString::fromStdString(pc.plans[k].nom), QStringLiteral("plan"), QStringLiteral("plan:%1:%2").arg(i).arg(k), MECA);
      }
    };
    auto nomPiece = [&](int i) { return i >= 0 && i < static_cast<int>(p.pieces.size()) ? QString::fromStdString(p.pieces[static_cast<size_t>(i)].value("nom", std::string("Pièce"))) : QStringLiteral("Pièce manquante"); };
    // Assemblage, avec ses composants (pièces ou sous-assemblages, dépliables) et ses contraintes
    std::function<void(QTreeWidgetItem*, size_t, int)> remplirAssemblage = [&](QTreeWidgetItem* it, size_t a, int profondeur) {
      const auto& as = p.assemblages[a];
      for (size_t k = 0; k < as.composants.size(); ++k) {
        const auto& c = as.composants[k];
        const bool sous = c.assemblage >= 0 && c.assemblage < static_cast<int>(p.assemblages.size());
        QString etat = k == 0 || c.fixe ? QStringLiteral("🔒 fixe") : QStringLiteral("🔓");
        if (c.supprime) etat = QStringLiteral("⏸ suspendu");
        else if (c.masque) etat += QStringLiteral(" · masqué");
        if (c.miroir) etat += QStringLiteral(" · symétrique");
        QTreeWidgetItem* ci = element(it, sous ? QString::fromStdString(p.assemblages[static_cast<size_t>(c.assemblage)].nom) : nomPiece(c.piece),
                                      sous ? QStringLiteral("assemblage") : QStringLiteral("piece"), QStringLiteral("assemblage:%1:c:%2").arg(a).arg(k), MECA, etat);
        if (c.supprime || c.masque) ci->setForeground(0, QColor(0x9A, 0xA0, 0xB8));
        if (sous && profondeur < 6) remplirAssemblage(ci, static_cast<size_t>(c.assemblage), profondeur + 1);
        else if (!sous && c.piece >= 0 && c.piece < static_cast<int>(p.pieces.size())) contenuPiece(ci, static_cast<size_t>(c.piece));
      }
      if (!as.contraintes.empty()) {
        QTreeWidgetItem* dossier = element(it, QStringLiteral("Contraintes"), QStringLiteral("coincident"), QStringLiteral("assemblage:%1:contraintes").arg(a), MECA, QString::number(as.contraintes.size()));
        for (size_t k = 0; k < as.contraintes.size(); ++k) {
          const auto& c = as.contraintes[k];
          const QString type = !typeContrainteAssemblageConnu(c.type) ? QStringLiteral("Type inconnu « %1 »").arg(QString::fromStdString(c.type))
                             : c.type == "concentrique" ? QStringLiteral("Concentrique") : c.type == "parallele" ? QStringLiteral("Parallèle") : c.type == "perpendiculaire" ? QStringLiteral("Perpendiculaire")
                             : c.type == "angle" ? QStringLiteral("Angle %1°").arg(c.valeur) : c.type == "tangente" ? QStringLiteral("Tangente")
                             : c.type == "pivot" ? QStringLiteral("Pivot") : c.type == "glissiere" ? QStringLiteral("Glissière") : c.type == "rotule" ? QStringLiteral("Rotule")
                             : c.type == "engrenage" ? QStringLiteral("Engrenage ×%1").arg(c.valeur) : c.type == "vis" ? QStringLiteral("Vis-écrou")
                             : c.inverse ? QStringLiteral("Alignée") : std::abs(c.valeur) > 1e-12 ? QStringLiteral("Distance %1 mm").arg(c.valeur) : QStringLiteral("Coïncidente");
          auto nomC = [&](int ic) { return ic >= 0 && ic < static_cast<int>(as.composants.size()) ? (as.composants[static_cast<size_t>(ic)].assemblage >= 0 ? QStringLiteral("sous-assemblage") : nomPiece(as.composants[static_cast<size_t>(ic)].piece)) : QStringLiteral("?"); };
          QTreeWidgetItem* ki = element(dossier, QStringLiteral("%1 %2 : %3 ↔ %4").arg(c.suspendue ? QStringLiteral("⏸") : QStringLiteral("✓"), type, referenceAssemblage(c.refA) ? QStringLiteral("assemblage") : nomC(c.a), referenceAssemblage(c.refB) ? QStringLiteral("assemblage") : nomC(c.b)),
                                        QStringLiteral("coincident"), QStringLiteral("contrainteAssemblage:%1:%2").arg(a).arg(k), MECA);
          if (c.suspendue) ki->setForeground(0, QColor(0x9A, 0xA0, 0xB8));
        }
      }
    };
    for (size_t a = 0; a < p.assemblages.size(); ++a) {
      if (assemblageUtilise[a]) continue;
      const auto& as = p.assemblages[a];
      QTreeWidgetItem* it = element(s, QString::fromStdString(as.nom), QStringLiteral("assemblage"), QStringLiteral("assemblage:%1").arg(a), MECA, QStringLiteral("%1 composant(s)").arg(as.composants.size()));
      QFont fa = it->font(0);
      fa.setBold(true);
      it->setFont(0, fa);
      remplirAssemblage(it, a, 0);
    }
    for (size_t i = 0; i < p.pieces.size(); ++i) {
      if (pieceUtilisee[i]) continue;
      Piece pc;
      try { pc = pieceDepuisJson(p.pieces[i]); } catch (...) { continue; }
      QTreeWidgetItem* it = element(s, QString::fromStdString(pc.nom), QStringLiteral("piece"), QStringLiteral("piece:%1").arg(i), MECA, QString::number(pc.fonctions.size()) + QStringLiteral(" fonctions"));
      contenuPiece(it, i);
    }
  }
  // Électrique : projets conçus ici (folios, armoire, faisceau, rapports), puis schémas QElectroTech reliés
  QTreeWidgetItem* sectionElectrique = nullptr;
  if (!p.electrique.empty() || !p.schemasElectriques.empty())
    sectionElectrique = section(racine, QStringLiteral("Électrique"), QStringLiteral("metier-electrique"), QStringLiteral("section:electrique"), ELECTRIQUE);
  for (std::size_t i = 0; i < p.electrique.size(); ++i) {
    const auto& pe = p.electrique[i];
    int appareils = 0;
    for (const auto& c : pe.schema.composants) appareils += prom::elec::roleSymbole(c.symbole) == prom::elec::RoleSymbole::Borne ? 0 : 1;
    QTreeWidgetItem* it = element(sectionElectrique, QString::fromStdString(pe.nom), pe.logement ? QStringLiteral("logement") : QStringLiteral("contacteur"),
                                  QStringLiteral("electrique:%1").arg(i), ELECTRIQUE, QStringLiteral("%1 appareil(s)").arg(appareils));
    QFont fe = it->font(0);
    fe.setBold(true);
    it->setFont(0, fe);
    if (static_cast<int>(i) == projetElectrique) elecActive = it;
    QTreeWidgetItem* folios = element(it, QStringLiteral("Folios"), QStringLiteral("schema"), QStringLiteral("electrique:%1:folios").arg(i), ELECTRIQUE, QString::number(pe.schema.feuilles.size()));
    for (std::size_t k = 0; k < pe.schema.feuilles.size(); ++k)
      element(folios, QStringLiteral("%1 · %2").arg(k + 1).arg(QString::fromStdString(pe.schema.feuilles[k].nom)), QStringLiteral("schema"), QStringLiteral("electrique:%1:folio:%2").arg(i).arg(k),
              ELECTRIQUE, QString::fromUtf8(prom::elec::nomTypeFolio(pe.folio(pe.schema.feuilles[k].uuid).type)));
    element(it, pe.armoires.size() > 1 ? QStringLiteral("Armoires") : QStringLiteral("Armoire"), QStringLiteral("armoire"), QStringLiteral("electrique:%1:armoire").arg(i), ELECTRIQUE,
            pe.armoires.empty() ? QStringLiteral("à créer") : QString::number(pe.armoires.size()));
    if (!pe.faisceaux.empty()) element(it, QStringLiteral("Faisceaux"), QStringLiteral("faisceau"), QStringLiteral("electrique:%1:faisceau").arg(i), ELECTRIQUE, QString::number(pe.faisceaux.size()));
    if (!pe.grafcets.empty())
      element(it, pe.grafcets.size() > 1 ? QStringLiteral("Grafcets") : QStringLiteral("Grafcet"), QStringLiteral("grafcet"), QStringLiteral("electrique:%1:grafcet").arg(i), ELECTRIQUE,
              pe.grafcets.size() > 1 ? QStringLiteral("%1 grafcets").arg(pe.grafcets.size()) : QStringLiteral("%1 étape(s)").arg(pe.grafcets.front().etapes.size()));
    element(it, QStringLiteral("Rapports"), QStringLiteral("rapport"), QStringLiteral("electrique:%1:rapports").arg(i), ELECTRIQUE, QStringLiteral("borniers, câbles, nomenclature"));
    element(it, QStringLiteral("Calculs"), QStringLiteral("calcul"), QStringLiteral("electrique:%1:calculs").arg(i), ELECTRIQUE, QStringLiteral("sections, Icc, terre, éclairage…"));
    if (pe.logement) element(it, QStringLiteral("Logement"), QStringLiteral("logement"), QStringLiteral("electrique:%1:logement").arg(i), ELECTRIQUE, QStringLiteral("NF C 15-100"));
    if (pe.distribution)
      element(it, QStringLiteral("Installation"), QStringLiteral("distribution"), QStringLiteral("electrique:%1:distribution").arg(i), ELECTRIQUE,
              QStringLiteral("%1 départ(s)").arg(pe.distribution->departs.size()));
    if (pe.photovoltaique)
      element(it, QStringLiteral("Photovoltaïque"), QStringLiteral("photovoltaique"), QStringLiteral("electrique:%1:photovoltaique").arg(i), ELECTRIQUE,
              QStringLiteral("%1 modules").arg(pe.photovoltaique->nombreModules));
  }
  if (!p.schemasElectriques.empty()) {
    QTreeWidgetItem* s = sectionElectrique;
    static std::map<QString, std::pair<QDateTime, InfosQet>> cache;
    for (size_t k = 0; k < p.schemasElectriques.size(); ++k) {
      const QString chemin = QString::fromStdString(p.schemasElectriques[k].fichier);
      const QDateTime date = QFileInfo(chemin).lastModified();
      auto itc = cache.find(chemin);
      if (itc == cache.end() || itc->second.first != date) itc = cache.insert_or_assign(chemin, std::pair{date, lireQet(chemin)}).first;
      const InfosQet& q = itc->second.second;
      QTreeWidgetItem* it = element(s, q.lu ? q.titre : QString::fromStdString(p.schemasElectriques[k].titre), QStringLiteral("cablage"), QStringLiteral("schema:%1").arg(k), ELECTRIQUE, q.lu ? QStringLiteral("QElectroTech") : QStringLiteral("introuvable"));
      if (!q.lu) continue;
      QTreeWidgetItem* folios = element(it, QStringLiteral("Folios"), QStringLiteral("schema"), QStringLiteral("schema:%1:folios").arg(k), ELECTRIQUE, QString::number(q.folios.size()));
      for (const QString& f : q.folios) element(folios, f, QStringLiteral("schema"), QStringLiteral("schema:%1:f").arg(k), ELECTRIQUE);
      QTreeWidgetItem* app = element(it, QStringLiteral("Appareils"), QStringLiteral("armoire"), QStringLiteral("schema:%1:appareils").arg(k), ELECTRIQUE, QString::number(q.appareils.size()));
      for (const auto& ap : q.appareils) element(app, ap.repere + (ap.designation.isEmpty() ? QString() : QStringLiteral(" — ") + ap.designation), QStringLiteral("armoire"), QStringLiteral("schema:%1:a").arg(k), ELECTRIQUE, ap.folio);
    }
  }
  // Intégration mécatronique : la carte dans son boîtier
  if (carte) {
    QTreeWidgetItem* s = section(racine, QStringLiteral("Intégration"), QStringLiteral("assemblage"), QStringLiteral("integration"), QColor(0x5A, 0x63, 0x80));
    element(s, QStringLiteral("Encombrement"), QStringLiteral("recadrer"), QStringLiteral("integration"), QColor(0x5A, 0x63, 0x80), QStringLiteral("%1 × %2 mm").arg(p.carte.L).arg(p.carte.W));
    element(s, QStringLiteral("Fixations"), QStringLiteral("percage"), QStringLiteral("integration"), QColor(0x5A, 0x63, 0x80), QString::number(p.trous.size()));
    int nc = 0;
    for (const auto& k : p.composants) if (k.bord) ++nc;
    element(s, QStringLiteral("Connecteurs de bord"), QStringLiteral("composants"), QStringLiteral("integration"), QColor(0x5A, 0x63, 0x80), QString::number(nc));
    element(s, QStringLiteral("Vérification des collisions"), QStringLiteral("coupe"), QStringLiteral("integration"), QColor(0x5A, 0x63, 0x80));
  }
  // Nomenclature commune : toutes les pièces, composants électroniques et appareils électriques du projet
  if (!p.pieces.empty() || carte || !p.schemasElectriques.empty() || !p.electronique.empty() || !p.electrique.empty())
    element(racine, QStringLiteral("Nomenclature"), QStringLiteral("composants"), QStringLiteral("nomenclature"), QColor(0x5A, 0x63, 0x80));
  // Ajouter : toujours à portée, au bas de l'arbre.
  auto* plus = element(racine, QStringLiteral("Ajouter une pièce"), QStringLiteral("nouvelle-piece"), QStringLiteral("ajouter:piece"), QColor());
  plus->setData(0, ROLE_COULEUR, QVariant());
  plus->setForeground(0, QColor(0x2F, 0x5F, 0xD0));
  auto* plusElec = element(racine, QStringLiteral("Ajouter une carte électronique"), QStringLiteral("pcb"), QStringLiteral("ajouter:elec"), QColor());
  plusElec->setData(0, ROLE_COULEUR, QVariant());
  plusElec->setForeground(0, QColor(0x0E, 0x94, 0xB8));
  auto* plusElectrique = element(racine, QStringLiteral("Ajouter un schéma électrique"), QStringLiteral("contacteur"), QStringLiteral("ajouter:electrique"), QColor());
  plusElectrique->setData(0, ROLE_COULEUR, QVariant());
  plusElectrique->setForeground(0, QColor(0xC2, 0x6E, 0x00));
  if (!carte) {
    auto* k = element(racine, QStringLiteral("Ajouter une carte KiCad…"), QStringLiteral("kicad"), QStringLiteral("ajouter:kicad"), QColor());
    k->setData(0, ROLE_COULEUR, QVariant());
    k->setForeground(0, QColor(0x0E, 0x94, 0xB8));
  }
  // Repli : par défaut, projet, métiers et pièce active ouverts ; ensuite, ce que l'utilisateur a choisi.
  std::function<void(QTreeWidgetItem*)> deplier = [&](QTreeWidgetItem* it) {
    const QString cle = it->data(0, ROLE_CLE).toString();
    const bool parDefaut = cle == QLatin1String("projet") || cle.startsWith(QLatin1String("section:")) || it == m_pieceActive || it == elecActive;
    it->setExpanded(m_premiere ? parDefaut : (m_ouverts.count(cle) > 0 || (parDefaut && !m_ouverts.count(QStringLiteral("~") + cle))));
    for (int i = 0; i < it->childCount(); ++i) deplier(it->child(i));
  };
  deplier(racine);
  if (m_pieceActive) m_pieceActive->setExpanded(true);
  m_premiere = false;
}

// Au survol d'une feuille de schéma, sa vignette (2.30) ; les autres éléments gardent leur infobulle
bool Navigateur::viewportEvent(QEvent* e) {
  if (e->type() == QEvent::ToolTip && m_vignette) {
    auto* h = static_cast<QHelpEvent*>(e);
    if (QTreeWidgetItem* it = itemAt(h->pos())) {
      static const QRegularExpression motif(QStringLiteral("^elec:(\\d+):feuille:(\\d+)$"));
      const auto m = motif.match(it->data(0, ROLE_CLE).toString());
      if (m.hasMatch()) {
        const QString chemin = m_vignette(m.captured(1).toInt(), m.captured(2).toInt());
        if (!chemin.isEmpty()) {
          QToolTip::showText(h->globalPos(), QStringLiteral("<b>%1</b><br><img src=\"%2\">").arg(it->text(0).toHtmlEscaped(), chemin.toHtmlEscaped()), viewport());
          return true;
        }
      }
    }
  }
  return QTreeWidget::viewportEvent(e);
}

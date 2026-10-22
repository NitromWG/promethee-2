// Prométhée : nomenclature commune aux métiers. Licence GPL-3.0-only.
#include "Nomenclature.hpp"

#include <map>

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include "Document.hpp"
#include "Qet.hpp"
#include "promethee/construction.hpp"
#include "promethee/piece.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/nomenclature.hpp"
#include "promethee/nomenclature_meca.hpp"
#include "promethee/structure.hpp"

using namespace prom;

PageNomenclature::PageNomenclature(Document* doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(24, 18, 24, 18);
  auto* haut = new QHBoxLayout;
  haut->addWidget(new QLabel(QStringLiteral("<h2>Nomenclature</h2><p style='color:#4A5270'>Tout ce que contient le projet, tous métiers confondus.</p>")));
  haut->addStretch(1);
  auto* csv = new QPushButton(QStringLiteral("Exporter…"));
  csv->setToolTip(QStringLiteral("Classeur Excel (XLSX) ou CSV"));
  connect(csv, &QPushButton::clicked, this, [this] { exporterCsv(); });
  haut->addWidget(csv);
  col->addLayout(haut);
  m_table = new QTableWidget(0, 10);
  m_table->setHorizontalHeaderLabels({QStringLiteral("Métier"), QStringLiteral("Repère"), QStringLiteral("Désignation"), QStringLiteral("Matière ou valeur"), QStringLiteral("Quantité"),
                                      QStringLiteral("Masse (g)"), QStringLiteral("Volume (cm³)"), QStringLiteral("Masse volumique"), QStringLiteral("Référence"), QStringLiteral("Fournisseur")});
  m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  m_table->verticalHeader()->hide();
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSortingEnabled(true);
  col->addWidget(m_table, 1);
}

namespace {
// Cellule numérique : affiche un texte (unité, mention « provisoire »), se trie sur sa valeur (EX-20)
class CelluleNombre : public QTableWidgetItem {
public:
  CelluleNombre(const QString& texte, double valeur) : QTableWidgetItem(texte) { setData(Qt::UserRole, valeur); setTextAlignment(Qt::AlignRight | Qt::AlignVCenter); }
  bool operator<(const QTableWidgetItem& autre) const override { return data(Qt::UserRole).toDouble() < autre.data(Qt::UserRole).toDouble(); }
};
}  // namespace

void PageNomenclature::actualiser() {
  const Projet& p = m_doc->projet();
  m_table->setSortingEnabled(false);
  m_table->setRowCount(0);
  struct Nombre { QString texte; double valeur = -1; };
  auto ligne = [this](const QString& metier, const QColor& couleur, const QString& repere, const QString& designation, const QString& matiere, int quantite, const Nombre& masse,
                      const Nombre& volume = {}, const QString& densite = QString(), const QString& reference = QString(), const QString& fournisseur = QString()) {
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    const QStringList v = {metier, repere, designation, matiere, QString(), QString(), QString(), densite, reference, fournisseur};
    for (int c = 0; c < v.size(); ++c) {
      QTableWidgetItem* it = c == 4 ? new CelluleNombre(QString::number(quantite), quantite)
                           : c == 5 ? new CelluleNombre(masse.texte, masse.valeur)
                           : c == 6 ? new CelluleNombre(volume.texte, volume.valeur)
                                    : new QTableWidgetItem(v[c]);
      if (c == 0) it->setForeground(couleur);
      m_table->setItem(r, c, it);
    }
  };
  // Mécanique : quantités tirées des assemblages racines, sous-assemblages développés, suspendus exclus ; une pièce
  // symétrique est un article distinct (EX-01, EX-11)
  std::map<int, std::pair<Piece, ProprietesMasse>> calculees;
  for (const auto& l : nomenclatureMecanique(p)) {
    if (l.piece < 0 || l.piece >= static_cast<int>(p.pieces.size())) continue;
    auto it = calculees.find(l.piece);
    if (it == calculees.end()) {
      const Piece pc = pieceDepuisJson(p.pieces[static_cast<size_t>(l.piece)]);
      const ResultatPiece r = construirePiece(pc);
      it = calculees.emplace(l.piece, std::pair{pc, proprietesMasse(r.forme, pc.densite)}).first;
    }
    const Piece& pc = it->second.first;
    const ProprietesMasse& pm = it->second.second;
    // Repère stable de la pièce (M10) : il ne change pas quand l'ordre des pièces change
    const int numero = repereArticle(p, false, l.piece, l.symetrique);
    const QString repere = numero > 0 ? QString::number(numero) : QStringLiteral("P%1").arg(l.piece + 1);
    const QString designation = QString::fromStdString(pc.nom) + (l.symetrique ? QStringLiteral(" (symétrique)") : QString());
    Nombre masse, volume;
    if (pm.statut != StatutMasse::Vide) {
      masse = pm.valide ? Nombre{QStringLiteral("%1%2%3").arg(pc.densiteConfirmee ? QString() : QStringLiteral("≈ "), QLocale().toString(pm.masse, 'f', 1),
                                                             pc.densiteConfirmee ? QString() : QStringLiteral(" (provisoire)")),
                                 pm.masse}
                        : Nombre{QStringLiteral("non fiable (géométrie)"), -1};
      volume = {QStringLiteral("%1%2").arg(pm.valide ? QString() : QStringLiteral("≈ "), QLocale().toString(pm.volume / 1000, 'f', 2)), pm.volume / 1000};
    }
    ligne(QStringLiteral("Mécanique"), QColor(0x27, 0x60, 0xF1), repere, designation, QString::fromStdString(pc.materiau), l.quantite, masse, volume,
          QStringLiteral("%1 g/cm³%2").arg(QLocale().toString(pc.densite, 'f', 2), pc.densiteConfirmee ? QString() : QStringLiteral(" (par défaut)")),
          QString::fromStdString(pc.reference), QString::fromStdString(pc.fournisseur));
  }
  // Boîtier : les mêmes articles que le dossier de fabrication (carte nue, boîtier, couvercle, visserie, inserts), EX-05
  bool cartePilotee = false;
  for (const auto& ce : p.electronique) cartePilotee |= ce.pilotageBoitier;
  if (!p.sansCarte)
    for (const auto& l : prom::nomenclature(p, m_doc->derive())) {
      if (l.groupe == "Électronique") continue;      // listés plus bas, une seule fois
      if (l.ref == "PCB1" && cartePilotee) continue;  // la carte conçue ici est déjà listée
      ligne(QString::fromStdString(l.groupe), QColor(0x27, 0x60, 0xF1), QString::fromStdString(l.ref), QString::fromStdString(l.designation), QString::fromStdString(l.appro), l.qte, {});
    }
  // Électronique : cartes conçues dans Prométhée (le circuit nu, puis ses composants regroupés)
  bool pilotee = false;
  for (const auto& ce : p.electronique) {
    pilotee |= ce.pilotageBoitier;
    const auto& k = ce.circuit;
    ligne(QStringLiteral("Électronique"), QColor(0x0E, 0x94, 0xB8), QStringLiteral("PCB"),
          QStringLiteral("Circuit imprimé « %1 », révision %2").arg(QString::fromStdString(ce.nom), QString::fromStdString(ce.cartouche.revision)),
          QStringLiteral("%1 × %2 mm, %3 couches, %4 mm").arg(QLocale().toString(prom::ecad::mm(k.contour.largeur), 'f', 1), QLocale().toString(prom::ecad::mm(k.contour.hauteur), 'f', 1))
              .arg(k.empilage.couchesCuivre)
              .arg(QLocale().toString(k.empilage.epaisseur, 'f', 1)),
          1, {});
    for (const auto& l : prom::ecad::nomenclature(ce)) {
      QString refs;
      for (const auto& r : l.references) refs += (refs.isEmpty() ? QString() : QStringLiteral(", ")) + QString::fromStdString(r);
      ligne(QStringLiteral("Électronique"), QColor(0x0E, 0x94, 0xB8), refs, QString::fromStdString(l.description.empty() ? l.empreinte : l.description), QString::fromStdString(l.valeur),
            l.quantite, {}, {}, QString(), QString::fromStdString(l.mpn.empty() ? std::string() : l.fabricant + " " + l.mpn), QString::fromStdString(l.fournisseur));
    }
  }
  // Électronique : composants de la carte du boîtier (sauf s'ils ne sont que la copie d'une carte conçue ici)
  if (!p.sansCarte && !pilotee)
    for (const auto& c : p.composants)
      ligne(QStringLiteral("Électronique"), QColor(0x0E, 0x94, 0xB8), QString::fromStdString(c.ref.empty() ? c.id : c.ref), QString::fromStdString(c.type), QString::fromStdString(c.valeur), 1, {});
  // Électrique : appareils des projets électriques conçus ici (regroupés par référence), puis des schémas
  // QElectroTech reliés
  for (const auto& pe : p.electrique)
    for (const auto& l : prom::elec::nomenclatureElectrique(pe)) {
      QString refs;
      for (const auto& r : l.reperes) refs += (refs.isEmpty() ? QString() : QStringLiteral(", ")) + QString::fromStdString(r);
      ligne(QStringLiteral("Électrique"), QColor(0xC2, 0x6E, 0x00), refs, QString::fromStdString(l.designation), QString::fromStdString(pe.nom), l.quantite, {}, {}, QString(),
            QString::fromStdString(l.reference.empty() ? std::string() : l.fabricant + " " + l.reference), QString());
    }
  for (const auto& s : p.schemasElectriques) {
    const InfosQet q = lireQet(QString::fromStdString(s.fichier));
    for (const auto& a : q.appareils) ligne(QStringLiteral("Électrique"), QColor(0xC2, 0x6E, 0x00), a.repere, a.designation, a.folio, 1, {});
  }
  m_table->setSortingEnabled(true);
  m_table->resizeColumnsToContents();
  m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
}

void PageNomenclature::exporterCsv() {
  QString filtre;
  QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter la nomenclature"), QStringLiteral("nomenclature.xlsx"), QStringLiteral("Classeur Excel (*.xlsx);;CSV (*.csv)"), &filtre);
  if (chemin.isEmpty()) return;
  const QString suffixe = QFileInfo(chemin).suffix().toLower();
  if (suffixe != QLatin1String("xlsx") && suffixe != QLatin1String("csv")) chemin += filtre.contains(QLatin1String("csv")) ? QStringLiteral(".csv") : QStringLiteral(".xlsx");
  // Le tableau affiché, colonnes et lignes telles quelles ; quantités, masses et volumes écrits en nombres
  prom::TableauNomenclature t;
  t.titre = m_doc->projet().nom;
  for (int c = 0; c < m_table->columnCount(); ++c) t.colonnes.push_back({"c" + std::to_string(c), m_table->horizontalHeaderItem(c)->text().toStdString(), c >= 4 && c <= 6});
  for (int r = 0; r < m_table->rowCount(); ++r) {
    std::vector<prom::CelluleNomenclature> l;
    for (int c = 0; c < m_table->columnCount(); ++c) {
      const QTableWidgetItem* it = m_table->item(r, c);
      prom::CelluleNomenclature x{it ? it->text().toStdString() : std::string()};
      bool ok = false;
      const double v = it ? QLocale().toDouble(it->text(), &ok) : 0;
      if (c >= 4 && c <= 6 && ok) { x.nombre = v; x.estNombre = true; }
      if (c == 4) t.quantitePieces += static_cast<int>(v);
      l.push_back(x);
    }
    t.lignes.push_back(std::move(l));
    t.niveaux.push_back(0);
    t.assemblages.push_back(0);
  }
  try {
    const bool xlsx = QFileInfo(chemin).suffix().toLower() == QLatin1String("xlsx");
    prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), xlsx ? prom::xlsxNomenclature(t) : prom::csvNomenclature(t));
  } catch (const std::exception& e) {
    QMessageBox::warning(this, QStringLiteral("Exporter la nomenclature"), QStringLiteral("Le fichier n’a pas pu être écrit (ouvert dans un tableur ?) : %1").arg(QString::fromUtf8(e.what())));
  }
}

#include <QScrollArea>

#include "promethee/verifs.hpp"

PageIntegration::PageIntegration(Document* doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(0, 0, 0, 0);
  auto* defil = new QScrollArea;
  defil->setWidgetResizable(true);
  defil->setFrameShape(QFrame::NoFrame);
  m_texte = new QLabel;
  m_texte->setWordWrap(true);
  m_texte->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_texte->setContentsMargins(24, 18, 24, 18);
  m_texte->setTextFormat(Qt::RichText);
  defil->setWidget(m_texte);
  col->addWidget(defil);
}

void PageIntegration::actualiser() {
  const Projet& p = m_doc->projet();
  auto mm = [](double v) { return QLocale().toString(v, 'f', 1) + QStringLiteral(" mm"); };
  QString h = QStringLiteral("<h2>Intégration de la carte dans son boîtier</h2>");
  if (p.sansCarte) { m_texte->setText(h + QStringLiteral("<p>Ce projet n’a pas de carte électronique.</p>")); return; }
  const auto& c = p.carte;
  const auto& b = p.boitier;
  const double extL = c.L + 2 * (b.jeu + b.paroi), extW = c.W + 2 * (b.jeu + b.paroi), extH = b.fond + b.entretoise + c.t + b.hauteur + b.couvercle;
  h += QStringLiteral("<h3>Encombrement</h3><p>Carte : %1 × %2, épaisseur %3.<br>Boîtier, hors tout (estimé) : %4 × %5 × %6 — paroi %7, jeu %8, entretoises %9.</p>")
           .arg(mm(c.L), mm(c.W), mm(c.t), mm(extL), mm(extW), mm(extH), mm(b.paroi), mm(b.jeu), mm(b.entretoise));
  h += QStringLiteral("<h3>Fixations (%1)</h3>").arg(p.trous.size());
  if (p.trous.empty()) h += QStringLiteral("<p>Aucun trou de fixation : la carte n’est tenue par rien.</p>");
  else {
    h += QStringLiteral("<table cellpadding='4'><tr><th align='left'>Repère</th><th align='left'>Vis</th><th align='left'>Fixation</th><th align='left'>Position</th></tr>");
    for (const auto& t : p.trous)
      h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
               .arg(QString::fromStdString(t.ref.empty() ? t.id : t.ref), QString::fromStdString(t.vis.value_or(b.vis)), QString::fromStdString(t.fixation),
                    t.coin ? QStringLiteral("coin %1, à %2 et %3").arg(QString::fromStdString(*t.coin), mm(t.ox), mm(t.oy)) : QStringLiteral("x %1, y %2").arg(mm(t.x), mm(t.y)));
    h += QStringLiteral("</table>");
  }
  int nc = 0;
  QString connecteurs;
  for (const auto& k : p.composants) {
    const QString type = QString::fromStdString(k.type).toLower();
    const bool estConnecteur = k.bord.has_value() || type.contains(QLatin1String("conn")) || type.contains(QLatin1String("usb")) || type.contains(QLatin1String("jack")) ||
                               type.contains(QLatin1String("rj")) || type.contains(QLatin1String("header")) || type.contains(QLatin1String("bornier"));
    if (!estConnecteur) continue;
    ++nc;
    connecteurs += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                       .arg(QString::fromStdString(k.ref.empty() ? k.id : k.ref), QString::fromStdString(k.type), k.bord ? QStringLiteral("sur un bord, découpe dans la paroi") : QStringLiteral("sur la carte"),
                            k.trou_couvercle ? QStringLiteral("passage dans le couvercle") : QString());
  }
  h += QStringLiteral("<h3>Connecteurs (%1)</h3>").arg(nc);
  h += nc ? QStringLiteral("<table cellpadding='4'><tr><th align='left'>Repère</th><th align='left'>Type</th><th align='left'>Emplacement</th><th align='left'></th></tr>%1</table>").arg(connecteurs)
          : QStringLiteral("<p>Aucun connecteur reconnu.</p>");
  const auto& pb = m_doc->problemes();
  h += QStringLiteral("<h3>Vérifications (%1)</h3>").arg(pb.size());
  const ControleBoitier controle = controleBoitier(p);
  if (!controle.complet) h += QStringLiteral("<p style='color:#A87410'><b>⚠ %1</b></p>").arg(QString::fromStdString(controle.resume()).toHtmlEscaped());
  if (pb.empty() && controle.complet) h += QStringLiteral("<p style='color:#2E8B57'><b>✓ Aucune collision, aucun composant hors de la carte, boîtier cohérent.</b></p>");
  else if (pb.empty()) h += QStringLiteral("<p>Aucune collision détectée avec les hauteurs connues ; le contrôle reste à compléter.</p>");
  for (const auto& x : pb)
    h += QStringLiteral("<p style='color:%1'>%2 %3</p>").arg(x.sev == Severite::Erreur ? QStringLiteral("#C2402E") : QStringLiteral("#A87410"), x.sev == Severite::Erreur ? QStringLiteral("✖") : QStringLiteral("⚠"), QString::fromStdString(x.msg).toHtmlEscaped());
  m_texte->setText(h);
}

// Prométhée : création d'un composant électronique par l'utilisateur. Licence GPL-3.0-only.
#include "DialogueComposant.hpp"

#include <algorithm>
#include <map>
#include <set>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "Icones.hpp"
#include "VueSchema.hpp"
#include "promethee/electronique.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& t) { return t.trimmed().toStdString(); }
const char* COTES[] = {"Auto", "Gauche", "Droite", "Haut", "Bas"};
constexpr int NB_TYPES = static_cast<int>(TypeBroche::NonSpecifiee) + 1;
}  // namespace

DialogueComposant::DialogueComposant(QWidget* parent) : QDialog(parent) {
  setWindowTitle(QStringLiteral("Créer un composant"));
  setObjectName(QStringLiteral("dialogueComposant"));
  setStyleSheet(QStringLiteral("#dialogueComposant { background: #FAFBFE; } QLineEdit, QComboBox { padding: 3px 6px; }"
                               "QTableWidget { background: white; border: 1px solid #E1E5F0; border-radius: 6px; gridline-color: #EEF1F8; }"
                               "QHeaderView::section { background: #F3F5FB; border: none; padding: 4px; color: #4A5270; font-weight: 600; }"));
  resize(980, 640);
  auto* lignes = new QHBoxLayout(this);
  // Colonne de gauche : identité et broches
  auto* gauche = new QVBoxLayout;
  auto* formulaire = new QFormLayout;
  m_nom = new QLineEdit;
  m_nom->setPlaceholderText(QStringLiteral("ESP32-S3-WROOM, MCP2515, capteur…"));
  m_prefixe = new QLineEdit(QStringLiteral("U"));
  m_prefixe->setMaximumWidth(80);
  m_categorie = new QLineEdit(QStringLiteral("Mes composants"));
  m_description = new QLineEdit;
  m_motsCles = new QLineEdit;
  m_motsCles->setPlaceholderText(QStringLiteral("mots pour le retrouver dans la bibliothèque"));
  m_fabricant = new QLineEdit;
  m_mpn = new QLineEdit;
  m_fiche = new QLineEdit;
  m_fiche->setPlaceholderText(QStringLiteral("https://… (fiche technique)"));
  m_empreinte = new QComboBox;
  m_empreinte->setEditable(true);
  m_empreinte->addItem(QString());
  for (const auto& i : catalogueEmpreintes()) {
    m_empreinte->addItem(q(i.id));
    m_empreinte->setItemData(m_empreinte->count() - 1, q(i.description), Qt::ToolTipRole);
  }
  m_empreinte->setToolTip(QStringLiteral("Choisis dans le catalogue ou tape un nom paramétrique : SOIC-14, QFN-32-5x5-0.5, PinHeader_1x07_P2.54…"));
  formulaire->addRow(QStringLiteral("Nom"), m_nom);
  auto* ligne = new QHBoxLayout;
  ligne->addWidget(m_prefixe);
  ligne->addWidget(new QLabel(QStringLiteral("Catégorie")));
  ligne->addWidget(m_categorie, 1);
  formulaire->addRow(QStringLiteral("Préfixe"), ligne);
  formulaire->addRow(QStringLiteral("Description"), m_description);
  formulaire->addRow(QStringLiteral("Mots-clés"), m_motsCles);
  formulaire->addRow(QStringLiteral("Empreinte"), m_empreinte);
  formulaire->addRow(QStringLiteral("Fabricant"), m_fabricant);
  formulaire->addRow(QStringLiteral("Réf. fabricant"), m_mpn);
  formulaire->addRow(QStringLiteral("Fiche technique"), m_fiche);
  m_nomsUnites = new QLineEdit;
  m_nomsUnites->setObjectName(QStringLiteral("nomsUnites"));
  m_nomsUnites->setPlaceholderText(QStringLiteral("Ampli 1, Ampli 2… (symbole à plusieurs unités)"));
  m_nomsUnites->setToolTip(QStringLiteral("Un nom par unité, séparés par des virgules, dans l’ordre des unités : il est montré à la place de la lettre."));
  formulaire->addRow(QStringLiteral("Noms des unités"), m_nomsUnites);
  gauche->addLayout(formulaire);
  auto* titre = new QLabel(QStringLiteral("<b>Broches</b> <span style='color:#6B7393'>— numéro, nom, type électrique, côté du symbole</span>"));
  gauche->addWidget(titre);
  m_broches = new QTableWidget(0, 5);
  m_broches->setHorizontalHeaderLabels({QStringLiteral("N°"), QStringLiteral("Nom"), QStringLiteral("Type"), QStringLiteral("Côté"), QStringLiteral("Unité")});
  m_broches->horizontalHeaderItem(4)->setToolTip(QStringLiteral("Symbole à plusieurs unités (portes, amplis) : chaque unité se pose à part. « Commune » : broche répétée sur chaque unité."));
  m_broches->verticalHeader()->setVisible(false);
  m_broches->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_broches->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_broches->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_broches->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  m_broches->setSelectionBehavior(QAbstractItemView::SelectRows);
  gauche->addWidget(m_broches, 1);
  auto* boutons = new QHBoxLayout;
  auto* plus = new QPushButton(QStringLiteral("＋ Broche"));
  auto* moins = new QPushButton(icone(QStringLiteral("supprimer")), QStringLiteral("Retirer"));
  auto* depuisEmpreinte = new QPushButton(icone(QStringLiteral("pcb")), QStringLiteral("Depuis l’empreinte"));
  auto* coller = new QPushButton(icone(QStringLiteral("dossier")), QStringLiteral("Coller une liste…"));
  for (auto* b : {plus, moins, depuisEmpreinte, coller}) boutons->addWidget(b);
  boutons->addStretch(1);
  m_propagation = new QCheckBox(QStringLiteral("Propager aux autres unités"));
  m_propagation->setObjectName(QStringLiteral("propagerUnites"));
  m_propagation->setToolTip(QStringLiteral("Une broche changée (nom, type, côté) l’est aussi à la même place dans les autres unités de même taille ; chaque unité garde ses numéros."));
  m_propagation->setChecked(true);
  boutons->addWidget(m_propagation);
  gauche->addLayout(boutons);
  lignes->addLayout(gauche, 3);
  // Colonne de droite : aperçu et bilan
  auto* droite = new QVBoxLayout;
  m_apercu = new QLabel;
  m_apercu->setAlignment(Qt::AlignCenter);
  m_apercu->setMinimumSize(340, 340);
  m_apercu->setStyleSheet(QStringLiteral("background: white; border: 1px solid #E1E5F0; border-radius: 8px;"));
  droite->addWidget(m_apercu, 1);
  m_bilan = new QLabel;
  m_bilan->setWordWrap(true);
  droite->addWidget(m_bilan);
  auto* aide = new QLabel(QStringLiteral("<span style='color:#6B7393'>Liste collée : une broche par ligne, « numéro nom type côté unité » — par exemple "
                                         "« 1 VCC alim », « 5 SDA io droite », « 8 GND », « 3 1Y out u1 » (unité A d’un circuit à plusieurs portes). Sans côté, les alimentations vont en haut, les masses en bas, "
                                         "les entrées à gauche et les sorties à droite.</span>"));
  aide->setWordWrap(true);
  droite->addWidget(aide);
  auto* fin = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  m_ok = fin->button(QDialogButtonBox::Ok);
  m_ok->setText(QStringLiteral("Créer le composant"));
  fin->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annuler"));
  droite->addWidget(fin);
  lignes->addLayout(droite, 2);

  connect(fin, &QDialogButtonBox::accepted, this, [this] { if (valide()) accept(); });
  connect(fin, &QDialogButtonBox::rejected, this, &QDialog::reject);
  for (QLineEdit* e : {m_nom, m_prefixe, m_categorie, m_description, m_fabricant, m_mpn, m_fiche, m_motsCles})
    connect(e, &QLineEdit::textChanged, this, [this] { majApercu(); });
  connect(m_empreinte, &QComboBox::currentTextChanged, this, [this] { majApercu(); });
  connect(m_nomsUnites, &QLineEdit::textChanged, this, [this] {
    majLibellesUnites();
    majApercu();
  });
  connect(m_broches, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
    if (it && it->column() == 1) propager(it->row());
    majApercu();
  });
  connect(plus, &QPushButton::clicked, this, [this] {
    SpecBroche b;
    b.numero = std::to_string(m_broches->rowCount() + 1);
    ajouterLigne(b);
    m_broches->setCurrentCell(m_broches->rowCount() - 1, 1);
    m_broches->editItem(m_broches->item(m_broches->rowCount() - 1, 1));
  });
  connect(moins, &QPushButton::clicked, this, [this] {
    std::set<int> rangs;
    for (const auto* it : m_broches->selectedItems()) rangs.insert(it->row());
    if (rangs.empty() && m_broches->currentRow() >= 0) rangs.insert(m_broches->currentRow());
    for (auto it = rangs.rbegin(); it != rangs.rend(); ++it) m_broches->removeRow(*it);
    majApercu();
  });
  connect(depuisEmpreinte, &QPushButton::clicked, this, [this] { brochesDepuisEmpreinte(); });
  connect(coller, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const QString t = QInputDialog::getMultiLineText(this, QStringLiteral("Coller une liste de broches"),
                                                     QStringLiteral("Une broche par ligne : numéro, nom, puis type (alim, in, out, io, passive, oc, nc) et côté (gauche, droite, haut, bas)"),
                                                     QString(), &ok);
    if (ok) collerBroches(t);
  });
  majApercu();
}

void DialogueComposant::ajouterLigne(const SpecBroche& b) {
  const bool avant = m_remplissage;
  m_remplissage = true;
  const int r = m_broches->rowCount();
  m_broches->insertRow(r);
  m_broches->setItem(r, 0, new QTableWidgetItem(q(b.numero)));
  m_broches->setItem(r, 1, new QTableWidgetItem(q(b.nom)));
  auto* type = new QComboBox;
  for (int t = 0; t < NB_TYPES; ++t) type->addItem(QString::fromUtf8(nomTypeBroche(static_cast<TypeBroche>(t))), t);
  type->setCurrentIndex(static_cast<int>(b.type));
  connect(type, &QComboBox::currentIndexChanged, this, [this, type] {
    propager(ligneDe(type));
    majApercu();
  });
  m_broches->setCellWidget(r, 2, type);
  auto* cote = new QComboBox;
  for (int c = 0; c < 5; ++c) cote->addItem(QString::fromUtf8(COTES[c]), c - 1);
  cote->setCurrentIndex(std::clamp(b.cote, -1, 3) + 1);
  connect(cote, &QComboBox::currentIndexChanged, this, [this, cote] {
    propager(ligneDe(cote));
    majApercu();
  });
  m_broches->setCellWidget(r, 3, cote);
  auto* unite = new QComboBox;
  unite->addItem(QStringLiteral("Commune"), 0);
  for (int u = 1; u <= 26; ++u) unite->addItem(libelleUnite(u), u);
  unite->setCurrentIndex(std::max(0, unite->findData(std::clamp(b.unite, 0, 26))));
  connect(unite, &QComboBox::currentIndexChanged, this, [this] { majApercu(); });
  m_broches->setCellWidget(r, 4, unite);
  m_remplissage = avant;
}

QString DialogueComposant::libelleUnite(int u) const {
  const QString lettre = q(lettreUnite(u));
  if (!m_nomsUnites) return lettre;
  const QStringList noms = m_nomsUnites->text().split(QLatin1Char(','));
  const QString nom = u >= 1 && u <= noms.size() ? noms[u - 1].trimmed() : QString();
  return nom.isEmpty() ? lettre : QStringLiteral("%1 · %2").arg(lettre, nom);
}

void DialogueComposant::majLibellesUnites() {
  for (int i = 0; i < m_broches->rowCount(); ++i)
    if (auto* c = qobject_cast<QComboBox*>(m_broches->cellWidget(i, 4)))
      for (int k = 1; k < c->count(); ++k) c->setItemText(k, libelleUnite(c->itemData(k).toInt()));
}

int DialogueComposant::ligneDe(const QWidget* w) const {
  for (int i = 0; i < m_broches->rowCount(); ++i)
    for (int c = 2; c <= 4; ++c)
      if (m_broches->cellWidget(i, c) == w) return i;
  return -1;
}

void DialogueComposant::setPropagation(bool oui) { m_propagation->setChecked(oui); }

int DialogueComposant::propager(int ligne) {
  if (m_remplissage || m_propage || ligne < 0 || !m_propagation || !m_propagation->isChecked()) return 0;
  auto unite = [this](int i) {
    auto* u = qobject_cast<QComboBox*>(m_broches->cellWidget(i, 4));
    return u ? u->currentData().toInt() : 0;
  };
  const int u = unite(ligne);
  if (u <= 0) return 0;
  // Les lignes de chaque unité, dans l'ordre de la table ; la place de la broche dans la sienne
  std::map<int, std::vector<int>> parUnite;
  for (int i = 0; i < m_broches->rowCount(); ++i)
    if (const int x = unite(i); x > 0) parUnite[x].push_back(i);
  const auto& miennes = parUnite[u];
  const auto rang = std::find(miennes.begin(), miennes.end(), ligne) - miennes.begin();
  const QString nom = m_broches->item(ligne, 1) ? m_broches->item(ligne, 1)->text() : QString();
  auto* type = qobject_cast<QComboBox*>(m_broches->cellWidget(ligne, 2));
  auto* cote = qobject_cast<QComboBox*>(m_broches->cellWidget(ligne, 3));
  m_propage = true;
  int n = 0;
  for (const auto& [v, lignes] : parUnite) {
    if (v == u || lignes.size() != miennes.size()) continue;
    const int autre = lignes[static_cast<std::size_t>(rang)];
    if (auto* it = m_broches->item(autre, 1)) it->setText(nom);
    else m_broches->setItem(autre, 1, new QTableWidgetItem(nom));
    if (auto* t = qobject_cast<QComboBox*>(m_broches->cellWidget(autre, 2)); t && type) t->setCurrentIndex(type->currentIndex());
    if (auto* c = qobject_cast<QComboBox*>(m_broches->cellWidget(autre, 3)); c && cote) c->setCurrentIndex(cote->currentIndex());
    ++n;
  }
  m_propage = false;
  return n;
}

void DialogueComposant::setSpec(const SpecSymbole& sp) {
  m_remplissage = true;
  m_id = sp.id;
  m_version = sp.version;
  m_nom->setText(q(sp.nom));
  m_prefixe->setText(q(sp.prefixe));
  m_categorie->setText(q(sp.categorie));
  m_description->setText(q(sp.description));
  m_motsCles->setText(q(sp.motsCles));
  m_fabricant->setText(q(sp.fabricant));
  m_mpn->setText(q(sp.mpn));
  m_fiche->setText(q(sp.fiche));
  m_empreinte->setCurrentText(q(sp.empreinte));
  QStringList noms;
  for (const auto& n : sp.nomsUnites) noms << q(n);
  m_nomsUnites->setText(noms.join(QStringLiteral(", ")));
  m_broches->setRowCount(0);
  for (const auto& b : sp.broches) ajouterLigne(b);
  m_remplissage = false;
  if (!m_id.empty()) setWindowTitle(QStringLiteral("Modifier le composant"));
  majApercu();
}

SpecSymbole DialogueComposant::spec() const {
  SpecSymbole r;
  r.id = m_id;
  r.version = m_version;
  r.nom = s(m_nom->text());
  r.prefixe = s(m_prefixe->text());
  r.categorie = s(m_categorie->text());
  r.description = s(m_description->text());
  r.motsCles = s(m_motsCles->text());
  r.fabricant = s(m_fabricant->text());
  r.mpn = s(m_mpn->text());
  r.fiche = s(m_fiche->text());
  r.empreinte = s(m_empreinte->currentText());
  for (const QString& n : m_nomsUnites->text().split(QLatin1Char(','))) r.nomsUnites.push_back(s(n));
  while (!r.nomsUnites.empty() && r.nomsUnites.back().empty()) r.nomsUnites.pop_back();
  for (int i = 0; i < m_broches->rowCount(); ++i) {
    SpecBroche b;
    b.numero = m_broches->item(i, 0) ? s(m_broches->item(i, 0)->text()) : std::string();
    b.nom = m_broches->item(i, 1) ? s(m_broches->item(i, 1)->text()) : std::string();
    if (auto* t = qobject_cast<QComboBox*>(m_broches->cellWidget(i, 2))) b.type = static_cast<TypeBroche>(t->currentData().toInt());
    if (auto* c = qobject_cast<QComboBox*>(m_broches->cellWidget(i, 3))) b.cote = c->currentData().toInt();
    if (auto* u = qobject_cast<QComboBox*>(m_broches->cellWidget(i, 4))) b.unite = u->currentData().toInt();
    r.broches.push_back(b);
  }
  return r;
}

bool DialogueComposant::valide(QString* raison) const {
  const SpecSymbole sp = spec();
  auto non = [&](const QString& t) {
    if (raison) *raison = t;
    return false;
  };
  if (sp.nom.empty()) return non(QStringLiteral("Donne un nom au composant."));
  if (sp.broches.empty()) return non(QStringLiteral("Ajoute au moins une broche."));
  std::set<std::string> vus;
  for (const auto& b : sp.broches) {
    if (b.numero.empty()) return non(QStringLiteral("Chaque broche a besoin d’un numéro."));
    if (!vus.insert(b.numero).second) return non(QStringLiteral("Le numéro « %1 » est utilisé deux fois.").arg(q(b.numero)));
  }
  if (!sp.empreinte.empty() && !empreinteEnCache(sp.empreinte)) return non(QStringLiteral("Empreinte inconnue : « %1 ».").arg(q(sp.empreinte)));
  return true;
}

void DialogueComposant::collerBroches(const QString& texte) {
  for (const auto& b : lireListeBroches(texte.toStdString())) ajouterLigne(b);
  majApercu();
}

void DialogueComposant::brochesDepuisEmpreinte() {
  const DefEmpreinte* d = empreinteEnCache(s(m_empreinte->currentText()));
  if (!d) {
    m_bilan->setText(QStringLiteral("<span style='color:#C2402E'>Choisis d’abord une empreinte connue.</span>"));
    return;
  }
  std::set<std::string> presents;
  for (const auto& b : spec().broches) presents.insert(b.numero);
  std::vector<std::string> numeros;
  for (const auto& p : d->pastilles)
    if (!p.numero.empty() && !presents.count(p.numero) && std::find(numeros.begin(), numeros.end(), p.numero) == numeros.end()) numeros.push_back(p.numero);
  std::sort(numeros.begin(), numeros.end(), ordreNaturel);
  for (const auto& n : numeros) {
    SpecBroche b;
    b.numero = n;
    ajouterLigne(b);
  }
  majApercu();
}

void DialogueComposant::majApercu() {
  if (m_remplissage) return;
  QString raison;
  const bool ok = valide(&raison);
  m_ok->setEnabled(ok);
  const SpecSymbole sp = spec();
  const DefSymbole d = genererSymbole(sp);
  m_apercu->setPixmap(VueSchema::apercu(d, QSize(std::max(340, m_apercu->width() - 4), std::max(340, m_apercu->height() - 4))));
  if (!ok) {
    m_bilan->setText(QStringLiteral("<span style='color:#C2402E'>%1</span>").arg(raison));
    return;
  }
  // Correspondance avec les pastilles de l'empreinte
  QString t = QStringLiteral("<b>%1</b> · %2 broche(s)").arg(q(d.nom).toHtmlEscaped()).arg(sp.broches.size());
  if (d.unites > 1) {
    QStringList us;
    for (int u = 1; u <= d.unites; ++u) us << libelleUnite(u).toHtmlEscaped();
    t += QStringLiteral(" · %1 unités (%2), posées une à une").arg(d.unites).arg(us.join(QStringLiteral(", ")));
  }
  if (const DefEmpreinte* e = sp.empreinte.empty() ? nullptr : empreinteEnCache(sp.empreinte)) {
    std::set<std::string> pastilles;
    for (const auto& p : e->pastilles) if (!p.numero.empty()) pastilles.insert(p.numero);
    std::vector<std::string> manquantes;
    for (const auto& b : sp.broches) if (!pastilles.count(b.numero)) manquantes.push_back(b.numero);
    if (manquantes.empty()) t += QStringLiteral("<br><span style='color:#2E8B57'>✓ Chaque broche a sa pastille sur %1.</span>").arg(q(sp.empreinte));
    else {
      QString l;
      for (std::size_t i = 0; i < manquantes.size() && i < 8; ++i) l += (i ? QStringLiteral(", ") : QString()) + q(manquantes[i]);
      t += QStringLiteral("<br><span style='color:#A8620E'>⚠ %1 n’a pas de pastille pour : %2</span>").arg(q(sp.empreinte), l);
    }
  } else
    t += QStringLiteral("<br><span style='color:#A8620E'>⚠ Sans empreinte, le composant n’ira pas sur le circuit imprimé.</span>");
  m_bilan->setText(t);
}

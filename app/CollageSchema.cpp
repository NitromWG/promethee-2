// Prométhée : collages du schéma (lot L2, tâche 2.24, SAI-03, SAI-27). « Coller en matrice » (lignes, colonnes,
// pas X et Y, numérotation d'abord en ligne ou d'abord en colonne), « Collage spécial » (repères gardés, libres ou
// effacés), « Copier comme texte » (un tableau que les autres programmes lisent) ; le tampon, qui garde le collage
// armé après chaque clic, est un réglage du collage qui suit la souris (OutilsSchema.cpp). Méthodes de VueSchema.
// Licence GPL-3.0-only.
#include <algorithm>

#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMimeData>
#include <QSpinBox>

#include "VueSchema.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

std::set<std::string> VueSchema::collerMatrice(int lignes, int colonnes, Pt pas, bool ligneDabord) {
  const PressePapiersSchema pp = presseCourant();
  if (pp.vide() || lignes < 1 || colonnes < 1 || refuseVerrou()) return {};
  // La première case sous la souris, sinon à côté du bloc copié
  const Pt origine = rect().contains(m_souris.toPoint()) ? surGrille(monde(m_souris)) : pp.reference + Pt{pas.x, 0};
  const int fi = m_feuille;
  std::set<std::string> crees;
  modifier([&](Feuille&, ConceptionElec& c) { crees = collerEnMatrice(c, static_cast<std::size_t>(fi), pp, origine, lignes, colonnes, pas, ligneDabord); }, {},
           QStringLiteral("Coller en matrice %1 × %2").arg(lignes).arg(colonnes));
  if (!crees.empty()) {
    selectionner(crees);
    emit message(QStringLiteral("Matrice de %1 × %2 collée, numérotée d’abord %3.").arg(lignes).arg(colonnes).arg(ligneDabord ? QStringLiteral("en ligne") : QStringLiteral("en colonne")));
  }
  return crees;
}

void VueSchema::dialogueMatrice() {
  if (presseCourant().vide()) {
    emit message(QStringLiteral("Rien à coller : copie d’abord un bloc (Ctrl+C)."));
    return;
  }
  QDialog d(this);
  d.setObjectName(QStringLiteral("dialogueMatrice"));
  d.setWindowTitle(QStringLiteral("Coller en matrice"));
  auto* f = new QFormLayout(&d);
  auto* lignes = new QSpinBox(&d);
  lignes->setObjectName(QStringLiteral("lignes"));
  lignes->setRange(1, 100);
  lignes->setValue(2);
  auto* colonnes = new QSpinBox(&d);
  colonnes->setObjectName(QStringLiteral("colonnes"));
  colonnes->setRange(1, 100);
  colonnes->setValue(2);
  auto pasMm = [&d](const char* nom) {
    auto* x = new QDoubleSpinBox(&d);
    x->setObjectName(QString::fromLatin1(nom));
    x->setRange(-500, 500);
    x->setDecimals(2);
    x->setSingleStep(1.27);
    x->setSuffix(QStringLiteral(" mm"));
    x->setValue(5.08);
    return x;
  };
  QDoubleSpinBox* pasX = pasMm("pasX");
  QDoubleSpinBox* pasY = pasMm("pasY");
  auto* ordre = new QComboBox(&d);
  ordre->setObjectName(QStringLiteral("ordre"));
  ordre->addItems({QStringLiteral("D’abord en ligne (D1, D2… à droite)"), QStringLiteral("D’abord en colonne (D1, D2… vers le bas)")});
  f->addRow(QStringLiteral("Lignes"), lignes);
  f->addRow(QStringLiteral("Colonnes"), colonnes);
  f->addRow(QStringLiteral("Pas X"), pasX);
  f->addRow(QStringLiteral("Pas Y"), pasY);
  f->addRow(QStringLiteral("Numérotation"), ordre);
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
  connect(boutons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  connect(boutons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  f->addRow(boutons);
  if (d.exec() != QDialog::Accepted) return;
  // Le pas est arrondi à la grille du schéma : les broches restent accrochables
  const Pt pas = surGrille(ptMm(pasX->value(), pasY->value()));
  collerMatrice(lignes->value(), colonnes->value(), pas, ordre->currentIndex() == 0);
}

bool VueSchema::collageSpecial(ReperesCollage reperes) {
  const PressePapiersSchema pp = presseCourant();
  if (pp.vide() || refuseVerrou()) return false;
  if (rect().contains(m_souris.toPoint()) && collerSousLaSouris(pp, reperes)) return true;
  const Pt ou = pp.reference + ptMm(5.08, 5.08);
  const int fi = m_feuille;
  std::set<std::string> crees;
  modifier([&](Feuille&, ConceptionElec& c) { crees = collerSchemaAvec(c, static_cast<std::size_t>(fi), pp, ou, reperes); }, {}, QStringLiteral("Collage spécial"));
  if (!crees.empty()) selectionner(crees);
  return !crees.empty();
}

void VueSchema::dialogueCollageSpecial() {
  if (presseCourant().vide()) {
    emit message(QStringLiteral("Rien à coller : copie d’abord un bloc (Ctrl+C)."));
    return;
  }
  const QStringList choix{QStringLiteral("Repères gardés, même en double (R1 reste R1)"), QStringLiteral("Repères libres suivants (R1 devient R7)"), QStringLiteral("Repères effacés (R?, à annoter)")};
  bool ok = false;
  const QString c = QInputDialog::getItem(this, QStringLiteral("Collage spécial"), QStringLiteral("Repères des composants collés :"), choix, 1, false, &ok);
  if (!ok) return;
  const int i = static_cast<int>(choix.indexOf(c));
  collageSpecial(i == 0 ? ReperesCollage::Gardes : i == 1 ? ReperesCollage::Libres : ReperesCollage::Effaces);
}

QString VueSchema::texteSelection() const {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f) return {};
  // Un composant par ligne (repère, valeur, empreinte, article), puis les noms de réseaux et les textes
  QStringList lignes{QStringLiteral("Repère\tValeur\tEmpreinte\tArticle")};
  std::set<std::string> vus;
  std::vector<const ComposantElec*> composants;
  for (const auto& s : f->symboles)
    if (m_selection.count(s.uuid) && !s.composant.empty() && vus.insert(s.composant).second)
      if (const ComposantElec* k = c->composant(s.composant)) composants.push_back(k);
  std::sort(composants.begin(), composants.end(), [](const ComposantElec* a, const ComposantElec* b) { return ordreNaturel(a->reference, b->reference); });
  for (const ComposantElec* k : composants) {
    const DefSymbole* d = c->symbole(k->symbole);
    lignes << QStringLiteral("%1\t%2\t%3\t%4").arg(q(k->reference), q(k->valeur), q(k->empreinte), d ? q(d->nom) : QString());
  }
  QStringList reseaux, textes;
  for (const auto& e : f->etiquettes)
    if (m_selection.count(e.uuid)) reseaux << q(e.texte);
  for (const auto& t : f->textes)
    if (m_selection.count(t.uuid)) textes << q(t.texte);
  reseaux.removeDuplicates();
  if (!reseaux.isEmpty()) lignes << QString() << QStringLiteral("Réseaux\t%1").arg(reseaux.join(QStringLiteral(", ")));
  for (const QString& t : textes) lignes << QStringLiteral("Texte\t%1").arg(t);
  return composants.empty() && reseaux.isEmpty() && textes.isEmpty() ? QString() : lignes.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

void VueSchema::copierCommeTexte() {
  const QString t = texteSelection();
  if (t.isEmpty()) {
    emit message(QStringLiteral("Rien à copier en texte : choisis des composants, des étiquettes ou des textes."));
    return;
  }
  QGuiApplication::clipboard()->setText(t);
  emit message(QStringLiteral("Copié en texte : %1 ligne(s), à coller dans un tableur ou un message.").arg(t.count(QLatin1Char('\n'))));
}

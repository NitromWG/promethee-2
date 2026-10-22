// Prométhée : formulaire d'un appareil électrique personnel, dans le panneau. Licence GPL-3.0-only.
#include "FormulaireAppareil.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "Icones.hpp"
#include "VueSchema.hpp"

using prom::elec::MontageAppareil;
using prom::elec::SpecAppareil;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& t) { return t.trimmed().toStdString(); }

constexpr MontageAppareil MONTAGES[] = {MontageAppareil::Rail, MontageAppareil::Plaque, MontageAppareil::Porte, MontageAppareil::HorsArmoire};

QDoubleSpinBox* nombre(const QString& nom, double min, double max, int decimales, const QString& suffixe, const QString& special, double valeur) {
  auto* sp = new QDoubleSpinBox;
  sp->setObjectName(nom);
  sp->setMinimumWidth(64);
  sp->setRange(min, max);
  sp->setDecimals(decimales);
  sp->setSuffix(suffixe);
  sp->setSpecialValueText(special);
  sp->setValue(valeur);
  return sp;
}

}  // namespace

QStringList FormulaireAppareil::lettresProposees() {
  return {QStringLiteral("A — ensemble, module, appareil divers"), QStringLiteral("K — relais, automate, régulateur"),
          QStringLiteral("B — capteur, détecteur, transmetteur"),  QStringLiteral("P — mesure, signalisation, affichage"),
          QStringLiteral("Q — commutation de puissance"),          QStringLiteral("F — protection"),
          QStringLiteral("S — commande manuelle"),                 QStringLiteral("T — transformateur, alimentation"),
          QStringLiteral("U — convertisseur, variateur"),          QStringLiteral("G — source, générateur, batterie"),
          QStringLiteral("M — moteur, actionneur"),                QStringLiteral("E — chauffage, éclairage"),
          QStringLiteral("X — connexion, prise")};
}

QLineEdit* FormulaireAppareil::ligne(const QString& nom, const QString& valeur, const QString& indice) {
  auto* e = new QLineEdit(valeur);
  e->setObjectName(nom);
  e->setPlaceholderText(indice);
  e->setClearButtonEnabled(true);
  connect(e, &QLineEdit::textChanged, this, [this] { changement(); });
  return e;
}

FormulaireAppareil::FormulaireAppareil(const SpecAppareil& a, bool modification, int poses, QWidget* parent)
    : QGroupBox(modification ? QStringLiteral("Modifier « %1 »").arg(q(a.nom)) : QStringLiteral("Nouvel appareil"), parent), m_id(a.id) {
  setObjectName(QStringLiteral("formulaireAppareil"));
  auto* v = new QVBoxLayout(this);
  v->setSpacing(4);
  auto* intro = new QLabel(modification ? QStringLiteral("<span style='color:#6B7393'>%1</span>")
                                              .arg(poses > 0 ? QStringLiteral("%1 appareil(s) posé(s) suivront. Si une borne change de place, la vérification montre les fils à reprendre.").arg(poses)
                                                             : QStringLiteral("Aucun appareil de ce type n’est encore posé dans ce projet."))
                                        : QStringLiteral("<span style='color:#6B7393'>Un appareil que la bibliothèque n’a pas : donne ses bornes, le symbole se dessine "
                                                         "tout seul. Il servira dans tous tes projets.</span>"));
  intro->setWordWrap(true);
  v->addWidget(intro);
  m_apercu = new QLabel;
  m_apercu->setObjectName(QStringLiteral("apercuAppareilPerso"));
  m_apercu->setAlignment(Qt::AlignCenter);
  m_apercu->setMinimumHeight(130);
  m_apercu->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // l'aperçu n'élargit pas le panneau
  v->addWidget(m_apercu);

  // Étiquettes au-dessus des champs : les bornes se lisent en entier, même dans un panneau étroit
  auto* f = new QFormLayout;
  f->setSpacing(4);
  f->setRowWrapPolicy(QFormLayout::WrapAllRows);
  f->setProperty("dispositionGardee", true);
  m_nom = ligne(QStringLiteral("nomAppareilPerso"), q(a.nom), QStringLiteral("Relais de sécurité"));
  f->addRow(QStringLiteral("Nom"), m_nom);
  m_lettre = new QComboBox;
  m_lettre->setObjectName(QStringLiteral("lettreAppareilPerso"));
  m_lettre->setEditable(true);
  m_lettre->addItems(lettresProposees());
  m_lettre->setToolTip(QStringLiteral("Lettre du repère (CEI 81346-2) : choisis-la dans la liste ou tape-la (1 à 3 lettres : K, KF, PG…)"));
  {
    const QString l = q(a.prefixe);
    int i = -1;
    for (int k = 0; k < m_lettre->count(); ++k)
      if (m_lettre->itemText(k).section(QStringLiteral(" — "), 0, 0) == l) i = k;
    if (i >= 0) m_lettre->setCurrentIndex(i);
    else m_lettre->setEditText(l);
    m_lettre->lineEdit()->setCursorPosition(0);  // « K — relais… » se lit par son début
  }
  connect(m_lettre, &QComboBox::currentTextChanged, this, [this] { changement(); });
  connect(m_lettre, &QComboBox::currentIndexChanged, this, [this] { m_lettre->lineEdit()->setCursorPosition(0); });
  f->addRow(QStringLiteral("Lettre de repère"), m_lettre);
  m_marquage = ligne(QStringLiteral("marquageAppareilPerso"), q(a.marquage), QStringLiteral("texte dans le cadre (facultatif)"));
  m_marquage->setToolTip(QStringLiteral("Court texte écrit dans le cadre du symbole : « SÉCU », « T° », « U< », « API »…"));
  f->addRow(QStringLiteral("Marquage"), m_marquage);
  m_haut = ligne(QStringLiteral("bornesHaut"), q(prom::elec::texteBornes(a.haut)), QStringLiteral("A1 13 23"));
  f->addRow(QStringLiteral("Bornes en haut"), m_haut);
  m_bas = ligne(QStringLiteral("bornesBas"), q(prom::elec::texteBornes(a.bas)), QStringLiteral("A2 14 24"));
  f->addRow(QStringLiteral("Bornes en bas"), m_bas);
  m_gauche = ligne(QStringLiteral("bornesGauche"), q(prom::elec::texteBornes(a.gauche)), QStringLiteral("entrées (facultatif)"));
  f->addRow(QStringLiteral("Bornes à gauche"), m_gauche);
  m_droite = ligne(QStringLiteral("bornesDroite"), q(prom::elec::texteBornes(a.droite)), QStringLiteral("sorties (facultatif)"));
  f->addRow(QStringLiteral("Bornes à droite"), m_droite);
  for (QLineEdit* e : {m_haut, m_bas, m_gauche, m_droite})
    e->setToolTip(QStringLiteral("Numéros des bornes, séparés par des espaces, dans l’ordre de gauche à droite (ou de haut en bas). "
                                 "Le courant descend : alimentation et entrées en haut, sorties en bas."));
  v->addLayout(f);

  // Nomenclature et armoire : replié tant que rien n'y est donné, pour que le bouton reste en vue
  const bool details = !a.fabricant.empty() || !a.reference.empty() || !a.description.empty() || a.prix > 0 || a.largeur > 0 || a.hauteur > 0 || a.profondeur > 0 ||
                       a.pertes >= 0 || a.montage != MontageAppareil::Rail;
  auto* deplier = new QToolButton;
  deplier->setObjectName(QStringLiteral("detailsAppareilPerso"));
  deplier->setText(QStringLiteral("Nomenclature et armoire"));
  deplier->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  deplier->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  deplier->setAutoRaise(true);
  deplier->setCheckable(true);
  deplier->setChecked(details);
  deplier->setArrowType(details ? Qt::DownArrow : Qt::RightArrow);
  deplier->setToolTip(QStringLiteral("Facultatif : fabricant, référence et prix pour la nomenclature ; montage, dimensions et pertes pour l’armoire et son bilan "
                                     "thermique"));
  v->addWidget(deplier);
  auto* zone = new QWidget;
  zone->setObjectName(QStringLiteral("zoneDetailsAppareilPerso"));
  zone->setVisible(details);
  connect(deplier, &QToolButton::toggled, zone, [zone, deplier](bool oui) {
    zone->setVisible(oui);
    deplier->setArrowType(oui ? Qt::DownArrow : Qt::RightArrow);
  });
  auto* g = new QFormLayout(zone);
  g->setContentsMargins(0, 0, 0, 0);
  g->setSpacing(4);
  g->setRowWrapPolicy(QFormLayout::WrapAllRows);
  g->setProperty("dispositionGardee", true);
  m_fabricant = ligne(QStringLiteral("fabricantAppareilPerso"), q(a.fabricant), QStringLiteral("fabricant"));
  g->addRow(QStringLiteral("Fabricant"), m_fabricant);
  m_reference = ligne(QStringLiteral("referenceAppareilPerso"), q(a.reference), QStringLiteral("référence de commande"));
  g->addRow(QStringLiteral("Référence"), m_reference);
  m_prix = nombre(QStringLiteral("prixAppareilPerso"), 0, 1e6, 2, QStringLiteral(" € HT"), QStringLiteral("à saisir"), a.prix);
  m_prix->setToolTip(QStringLiteral("Prix indicatif repris par la nomenclature chiffrée (un prix saisi dans la nomenclature l’emporte)"));
  g->addRow(QStringLiteral("Prix"), m_prix);
  m_montage = new QComboBox;
  m_montage->setObjectName(QStringLiteral("montageAppareilPerso"));
  for (MontageAppareil m : MONTAGES) m_montage->addItem(QString::fromUtf8(prom::elec::nomMontage(m)), static_cast<int>(m));
  m_montage->setCurrentIndex(std::max(0, m_montage->findData(static_cast<int>(a.montage))));
  connect(m_montage, &QComboBox::currentIndexChanged, this, [this] { changement(); });
  g->addRow(QStringLiteral("Montage"), m_montage);
  m_largeur = nombre(QStringLiteral("largeurAppareilPerso"), 0, 2000, 1, QStringLiteral(" mm"), QStringLiteral("d’office"), a.largeur);
  g->addRow(QStringLiteral("Largeur"), m_largeur);
  m_hauteur = nombre(QStringLiteral("hauteurAppareilPerso"), 0, 2000, 1, QStringLiteral(" mm"), QStringLiteral("d’office"), a.hauteur);
  g->addRow(QStringLiteral("Hauteur"), m_hauteur);
  m_profondeur = nombre(QStringLiteral("profondeurAppareilPerso"), 0, 2000, 1, QStringLiteral(" mm"), QStringLiteral("d’office"), a.profondeur);
  g->addRow(QStringLiteral("Profondeur"), m_profondeur);
  m_pertes = nombre(QStringLiteral("pertesAppareilPerso"), -1, 10000, 1, QStringLiteral(" W"), QStringLiteral("inconnues"), a.pertes < 0 ? -1 : a.pertes);
  m_pertes->setToolTip(QStringLiteral("Puissance dissipée en service, pour le bilan thermique de l’armoire (fiche technique du fabricant)"));
  g->addRow(QStringLiteral("Pertes"), m_pertes);
  for (QDoubleSpinBox* sp : {m_prix, m_largeur, m_hauteur, m_profondeur, m_pertes}) connect(sp, &QDoubleSpinBox::valueChanged, this, [this] { changement(); });
  m_description = ligne(QStringLiteral("descriptionAppareilPerso"), q(a.description), QStringLiteral("ce qu’il fait (bulle de la palette)"));
  g->addRow(QStringLiteral("Description"), m_description);
  v->addWidget(zone);

  m_defauts = new QLabel;
  m_defauts->setObjectName(QStringLiteral("defautsAppareilPerso"));
  m_defauts->setWordWrap(true);
  v->addWidget(m_defauts);
  auto* boutons = new QHBoxLayout;
  m_valider = new QPushButton(icone(modification ? QStringLiteral("valider") : QStringLiteral("appareil-perso")),
                              modification ? QStringLiteral("Appliquer") : QStringLiteral("Créer et poser"));
  m_valider->setObjectName(QStringLiteral("validerAppareilPerso"));
  m_valider->setDefault(true);
  connect(m_valider, &QPushButton::clicked, this, [this] { emit valide(spec()); });
  auto* annuler = new QPushButton(icone(QStringLiteral("annuler")), QStringLiteral("Annuler"));
  annuler->setObjectName(QStringLiteral("annulerAppareilPerso"));
  connect(annuler, &QPushButton::clicked, this, [this] { emit annule(); });
  boutons->addWidget(m_valider, 1);
  boutons->addWidget(annuler);
  v->addLayout(boutons);
  // Entrée dans un champ : comme le bouton, si l'appareil se dessine ; chaque champ se lit par son début
  for (QLineEdit* e : findChildren<QLineEdit*>()) {
    connect(e, &QLineEdit::returnPressed, this, [this] {
      if (m_valider->isEnabled()) m_valider->click();
    });
    e->setCursorPosition(0);
  }
  changement();
}

SpecAppareil FormulaireAppareil::spec() const {
  SpecAppareil a;
  a.id = m_id;
  a.nom = s(m_nom->text());
  // « K — relais… » choisi dans la liste, ou lettres tapées
  a.prefixe = s(m_lettre->currentText().section(QStringLiteral(" — "), 0, 0));
  a.marquage = s(m_marquage->text());
  a.haut = prom::elec::lireBornes(s(m_haut->text()));
  a.bas = prom::elec::lireBornes(s(m_bas->text()));
  a.gauche = prom::elec::lireBornes(s(m_gauche->text()));
  a.droite = prom::elec::lireBornes(s(m_droite->text()));
  a.fabricant = s(m_fabricant->text());
  a.reference = s(m_reference->text());
  a.description = s(m_description->text());
  a.prix = m_prix->value();
  a.montage = static_cast<MontageAppareil>(m_montage->currentData().toInt());
  a.largeur = m_largeur->value();
  a.hauteur = m_hauteur->value();
  a.profondeur = m_profondeur->value();
  a.pertes = m_pertes->value() < 0 ? -1 : m_pertes->value();
  return a;
}

void FormulaireAppareil::changement() {
  if (!m_valider) return;  // construction en cours
  const SpecAppareil a = spec();
  const std::vector<std::string> defauts = prom::elec::defautsAppareil(a);
  m_apercu->setPixmap(VueSchema::apercu(prom::elec::symboleAppareil(a), QSize(220, 130)));
  QString t;
  for (const auto& d : defauts) t += QStringLiteral("<div>• %1</div>").arg(q(d).toHtmlEscaped());
  m_defauts->setText(defauts.empty() ? QString() : QStringLiteral("<span style='color:#A8620E'>%1</span>").arg(t));
  m_defauts->setVisible(!defauts.empty());
  m_valider->setEnabled(defauts.empty());
  emit modifie(a);
}

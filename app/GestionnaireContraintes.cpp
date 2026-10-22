// Prométhée : gestionnaire de contraintes (lot L8). Licence GPL-3.0-only.
#include "GestionnaireContraintes.hpp"

#include <algorithm>

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QBrush>
#include <QColor>
#include <QTimer>
#include <QTableWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
// Bornes de chaque colonne (mm), comme dans l'inspecteur
constexpr double MINI[] = {0.05, 0.05, 0.2, 0.1};
constexpr double MAXI[] = {20, 10, 5, 4};
using ChampR = Nm Regles::*;
using ChampC = Nm ClasseReseau::*;
ChampR champRegles(int c) {
  switch (c) {
    case GestionnaireContraintes::Largeur: return &Regles::largeur;
    case GestionnaireContraintes::Isolement: return &Regles::isolement;
    case GestionnaireContraintes::ViaDiametre: return &Regles::viaDiametre;
    default: return &Regles::viaPercage;
  }
}
ChampC champClasse(int c) {
  switch (c) {
    case GestionnaireContraintes::Largeur: return &ClasseReseau::largeur;
    case GestionnaireContraintes::Isolement: return &ClasseReseau::isolement;
    case GestionnaireContraintes::ViaDiametre: return &ClasseReseau::viaDiametre;
    default: return &ClasseReseau::viaPercage;
  }
}
}  // namespace

GestionnaireContraintes::GestionnaireContraintes(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("gestionnaireContraintes"));
  setWindowTitle(QStringLiteral("Gestionnaire de contraintes"));
  resize(720, 560);
  auto* col = new QVBoxLayout(this);
  m_etat = new QLabel;
  m_etat->setWordWrap(true);
  col->addWidget(m_etat);
  col->addWidget(new QLabel(QStringLiteral("<b>Règles</b> — toute la carte, puis chaque classe de réseaux (mm)")));
  m_regles = new QTableWidget(0, NbColonnes);
  m_regles->setObjectName(QStringLiteral("tableRegles"));
  m_regles->setHorizontalHeaderLabels({QStringLiteral("Largeur"), QStringLiteral("Isolement"), QStringLiteral("Via : diamètre"), QStringLiteral("Via : perçage")});
  m_regles->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  col->addWidget(m_regles, 1);
  auto* ligne = new QHBoxLayout;
  auto* nouvelle = new QPushButton(QStringLiteral("＋ Nouvelle classe"));
  nouvelle->setObjectName(QStringLiteral("nouvelleClasseContraintes"));
  ligne->addWidget(nouvelle);
  ligne->addStretch();
  col->addLayout(ligne);
  col->addWidget(new QLabel(QStringLiteral("<b>Affectations</b> — classe de chaque réseau")));
  m_affectations = new QTableWidget(0, 2);
  m_affectations->setObjectName(QStringLiteral("tableAffectations"));
  m_affectations->setHorizontalHeaderLabels({QStringLiteral("Réseau"), QStringLiteral("Classe")});
  m_affectations->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  col->addWidget(m_affectations, 1);
  connect(m_regles, &QTableWidget::cellChanged, this, [this](int l, int c) { editee(l, c); });
  connect(nouvelle, &QPushButton::clicked, this, [this] { ajouterClasse(); });
  refaire();
}

QString GestionnaireContraintes::origine() const { return m_atelier.mode() == AtelierElectronique::Schema ? QStringLiteral("schéma") : QStringLiteral("circuit"); }

void GestionnaireContraintes::refaire() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  m_refait = true;
  const QSignalBlocker bloque(m_regles);
  const Regles& R = c->circuit.regles;
  m_etat->setText(QStringLiteral("Les règles de « %1 », communes au schéma et au circuit (profil %2). Chaque changement est un pas d’annulation.").arg(q(c->nom), q(R.profil)));
  m_regles->setRowCount(static_cast<int>(R.classes.size()) + 1);
  QStringList lignes{QStringLiteral("Toute la carte")};
  for (const auto& cl : R.classes) lignes << q(cl.nom);
  m_regles->setVerticalHeaderLabels(lignes);
  const QLocale l;
  for (int col = 0; col < NbColonnes; ++col) {
    auto poser = [&](int ligne, Nm v) {
      auto* it = m_regles->item(ligne, col);
      if (!it) {
        it = new QTableWidgetItem;
        m_regles->setItem(ligne, col, it);
      }
      it->setText(l.toString(mm(v), 'f', 3));
      it->setToolTip({});
      it->setForeground(QBrush());
    };
    poser(0, R.*champRegles(col));
    for (std::size_t i = 0; i < R.classes.size(); ++i) poser(static_cast<int>(i) + 1, R.classes[i].*champClasse(col));
  }
  // Affectations : un réseau par ligne, sa classe dans une liste
  std::vector<std::string> reseaux;
  for (const auto& r : m_atelier.calculs().k.reseaux)
    if (!r.broches.empty() && !r.nom.empty()) reseaux.push_back(r.nom);
  std::sort(reseaux.begin(), reseaux.end());
  reseaux.erase(std::unique(reseaux.begin(), reseaux.end()), reseaux.end());
  m_affectations->setRowCount(static_cast<int>(reseaux.size()));
  for (std::size_t i = 0; i < reseaux.size(); ++i) {
    const int r = static_cast<int>(i);
    auto* nom = new QTableWidgetItem(q(reseaux[i]));
    nom->setFlags(nom->flags() & ~Qt::ItemIsEditable);
    m_affectations->setItem(r, 0, nom);
    auto* choix = new QComboBox;
    choix->setObjectName(QStringLiteral("classe:") + q(reseaux[i]));
    choix->addItem(QStringLiteral("—"));
    for (const auto& cl : R.classes) choix->addItem(q(cl.nom));
    const auto aff = R.classeReseau.find(reseaux[i]);
    if (aff != R.classeReseau.end()) choix->setCurrentText(q(aff->second));
    const std::string reseau = reseaux[i];
    connect(choix, &QComboBox::activated, this, [this, choix, reseau](int k) { affecter(reseau, k == 0 ? std::string() : choix->currentText().toStdString()); });
    m_affectations->setCellWidget(r, 1, choix);
  }
  m_refait = false;
}

void GestionnaireContraintes::editee(int ligne, int colonne) {
  if (m_refait) return;
  auto* it = m_regles->item(ligne, colonne);
  if (!it) return;
  bool ok = false;
  const double v = QLocale().toDouble(it->text().trimmed(), &ok);
  const double w = ok ? v : it->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
  if (!ok || !changer(ligne, static_cast<Colonne>(colonne), w)) {
    // Refusée : la cellule dit pourquoi, la valeur du modèle reste
    QTimer::singleShot(0, this, [this, ligne, colonne] {
      refaire();
      if (auto* x = m_regles->item(ligne, colonne)) {
        x->setForeground(QColor(QStringLiteral("#d33")));
        x->setToolTip(QStringLiteral("Valeur refusée : entre %1 et %2 mm").arg(QLocale().toString(MINI[colonne])).arg(QLocale().toString(MAXI[colonne])));
      }
    });
  }
}

bool GestionnaireContraintes::changer(int ligne, Colonne colonne, double v) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c || colonne < 0 || colonne >= NbColonnes || ligne < 0 || ligne > static_cast<int>(c->circuit.regles.classes.size())) return false;
  if (v < MINI[colonne] || v > MAXI[colonne]) return false;
  const std::string classe = ligne == 0 ? std::string() : c->circuit.regles.classes[static_cast<std::size_t>(ligne - 1)].nom;
  const bool fait = m_atelier.modifierConception(
      [&](ConceptionElec& k) {
        if (classe.empty()) k.circuit.regles.*champRegles(colonne) = nm(v);
        else
          for (auto& cl : k.circuit.regles.classes)
            if (cl.nom == classe) cl.*champClasse(colonne) = nm(v);
      },
      QStringLiteral("Règle changée (%1)").arg(origine()));
  refaire();
  return fait;
}

bool GestionnaireContraintes::affecter(const std::string& reseau, const std::string& classe) {
  const bool fait = m_atelier.modifierConception(
      [&](ConceptionElec& k) {
        if (classe.empty()) k.circuit.regles.classeReseau.erase(reseau);
        else k.circuit.regles.classeReseau[reseau] = classe;
      },
      QStringLiteral("Réseau rangé dans une classe"));
  refaire();
  return fait;
}

bool GestionnaireContraintes::ajouterClasse() {
  const bool fait = m_atelier.modifierConception(
      [](ConceptionElec& k) {
        Regles& R = k.circuit.regles;
        ClasseReseau cl;
        cl.nom = "Classe 1";
        for (int n = 2; std::any_of(R.classes.begin(), R.classes.end(), [&](const ClasseReseau& x) { return x.nom == cl.nom; }); ++n) cl.nom = "Classe " + std::to_string(n);
        cl.largeur = R.largeur;
        cl.isolement = R.isolement;
        cl.viaDiametre = R.viaDiametre;
        cl.viaPercage = R.viaPercage;
        R.classes.push_back(cl);
      },
      QStringLiteral("Classe de réseaux ajoutée"));
  refaire();
  return fait;
}

// Prométhée : outils de simulation de l'atelier électronique (lot L11 de la 0.34). Voir OutilsSimulation034.hpp.
// Licence GPL-3.0-only.
#include "OutilsSimulation034.hpp"

#include <cmath>
#include <limits>
#include <memory>
#include <thread>

#include <QApplication>
#include <QFileInfo>
#include <QLocale>
#include <QMetaObject>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "SelectionElectronique.hpp"
#include "ZoneMessages.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/simulation.hpp"

using namespace prom::ecad;
using prom::elec::Calculateur;
using prom::elec::NiveauAvis;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

// ======================================================================================================================
// Calculateurs
// ======================================================================================================================
FenetreCalculateurs::FenetreCalculateurs(QWidget* parent) : QWidget(parent, Qt::Window) {
  setObjectName(QStringLiteral("fenetreCalculateurs"));
  setWindowTitle(QStringLiteral("Calculateurs de l’électronique"));
  resize(980, 640);
  auto* racine = new QHBoxLayout(this);
  m_liste = new QTreeWidget;
  m_liste->setObjectName(QStringLiteral("listeCalculateurs"));
  m_liste->setHeaderHidden(true);
  m_liste->setMaximumWidth(300);
  for (const std::string& famille : famillesCalculateursElectroniques()) {
    auto* f = new QTreeWidgetItem(m_liste);
    f->setText(0, q(famille));
    f->setFlags(Qt::ItemIsEnabled);
    QFont gras = f->font(0);
    gras.setBold(true);
    f->setFont(0, gras);
    for (const auto& c : calculateursElectroniques())
      if (c.famille == famille) {
        auto* it = new QTreeWidgetItem(f);
        it->setText(0, q(c.nom));
        it->setToolTip(0, q(c.description));
        it->setData(0, Qt::UserRole, q(c.id));
      }
  }
  m_liste->expandAll();
  racine->addWidget(m_liste);
  auto* droite = new QVBoxLayout;
  m_titre = new QLabel;
  m_titre->setWordWrap(true);
  droite->addWidget(m_titre);
  m_form = new QGroupBox(QStringLiteral("Données"));
  m_champs = new QFormLayout(m_form);
  droite->addWidget(m_form);
  m_resultat = new QLabel;
  m_resultat->setObjectName(QStringLiteral("resultatCalculateur"));
  m_resultat->setWordWrap(true);
  m_resultat->setTextFormat(Qt::RichText);
  m_resultat->setTextInteractionFlags(Qt::TextSelectableByMouse);
  droite->addWidget(m_resultat, 1);
  racine->addLayout(droite, 1);
  connect(m_liste, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
    if (it && !it->data(0, Qt::UserRole).toString().isEmpty()) choisir(it->data(0, Qt::UserRole).toString().toStdString());
  });
  choisir("piste");
}

bool FenetreCalculateurs::choisir(const std::string& id) {
  const Calculateur* c = calculateurElectronique(id);
  if (!c) return false;
  m_id = id;
  m_titre->setText(QStringLiteral("<span style='font-size:17px; font-weight:700'>%1</span><br><span style='color:#6B7393'>%2</span><br><span style='color:#8A91AD; font-size:11px'>%3</span>")
                       .arg(q(c->nom).toHtmlEscaped(), q(c->description).toHtmlEscaped(), q(c->regle).toHtmlEscaped()));
  while (m_champs->rowCount() > 0) m_champs->removeRow(0);
  m_lire.clear();
  m_ecrire.clear();
  for (const auto& ch : c->champs) {
    if (!ch.choix.empty()) {
      auto* b = new QComboBox;
      b->setObjectName(QStringLiteral("champ-%1").arg(q(ch.id)));
      for (const auto& x : ch.choix) b->addItem(q(x));
      b->setCurrentIndex(static_cast<int>(ch.valeur));
      connect(b, &QComboBox::currentIndexChanged, this, [this] { calculer(); });
      m_champs->addRow(q(ch.nom), b);
      m_lire.push_back([b] { return static_cast<double>(b->currentIndex()); });
      m_ecrire.push_back([b](double v) { b->setCurrentIndex(static_cast<int>(v)); });
    } else {
      // Champ borné : une saisie hors bornes est refusée (le champ reste rouge, le calcul l'ignore)
      auto* e = new QLineEdit(QLocale().toString(ch.valeur, 'g', 8));
      e->setObjectName(QStringLiteral("champ-%1").arg(q(ch.id)));
      e->setToolTip(QStringLiteral("De %1 à %2 %3%4").arg(QLocale().toString(ch.min, 'g', 6), QLocale().toString(ch.max, 'g', 6), q(ch.unite), ch.aide.empty() ? QString() : QStringLiteral(" — ") + q(ch.aide)));
      connect(e, &QLineEdit::textChanged, this, [this] { calculer(); });
      m_champs->addRow(q(ch.nom) + (ch.unite.empty() ? QString() : QStringLiteral(" (%1)").arg(q(ch.unite))), e);
      const double mn = ch.min, mx = ch.max;
      m_lire.push_back([e, mn, mx] {
        bool ok = false;
        const double v = QLocale().toDouble(e->text(), &ok);
        const double w = ok ? v : e->text().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        const bool dedans = ok && w >= mn && w <= mx;
        e->setStyleSheet(dedans ? QString() : QStringLiteral("QLineEdit { background: #F9D6D2; }"));
        return dedans ? w : std::numeric_limits<double>::quiet_NaN();
      });
      m_ecrire.push_back([e](double v) { e->setText(QLocale().toString(v, 'g', 10)); });
    }
  }
  calculer();
  return true;
}

bool FenetreCalculateurs::regler(const std::vector<double>& valeurs) {
  for (std::size_t i = 0; i < valeurs.size() && i < m_ecrire.size(); ++i) m_ecrire[i](valeurs[i]);
  calculer();
  for (const auto& l : m_lire)
    if (std::isnan(l())) return false;
  return true;
}

void FenetreCalculateurs::calculer() {
  const Calculateur* c = calculateurElectronique(m_id);
  if (!c) return;
  std::vector<double> v;
  for (const auto& l : m_lire) {
    const double x = l();
    if (std::isnan(x)) {
      m_resultat->setText(QStringLiteral("<span style='color:#c53030'>Une valeur est hors des bornes de son champ : corrige-la (les bornes sont dans l’infobulle).</span>"));
      return;
    }
    v.push_back(x);
  }
  const auto r = prom::elec::executer(*c, v);
  QString h;
  for (const auto& l : r.lignes)
    if (l.principal) h += QStringLiteral("<span style='font-size:19px; font-weight:700'>%1</span>&nbsp; <span style='color:#4A5270'>%2</span><br>").arg(q(l.valeur).toHtmlEscaped(), q(l.nom).toHtmlEscaped());
  for (const auto& l : r.lignes)
    if (!l.principal) h += QStringLiteral("%1 : <b>%2</b><br>").arg(q(l.nom).toHtmlEscaped(), q(l.valeur).toHtmlEscaped());
  if (!r.explication.empty()) h += QStringLiteral("<p>%1</p>").arg(q(r.explication).toHtmlEscaped());
  for (const auto& a : r.avis) {
    const QString couleur = a.niveau == NiveauAvis::Erreur ? QStringLiteral("#C2402E") : a.niveau == NiveauAvis::Alerte ? QStringLiteral("#A8620E") : QStringLiteral("#2E7D32");
    h += QStringLiteral("<span style='color:%1'>%2</span> <span style='color:#8A91AD'>(%3)</span><br>").arg(couleur, q(a.message).toHtmlEscaped(), q(a.regle).toHtmlEscaped());
  }
  if (!r.formule.empty()) h += QStringLiteral("<p style='color:#6B7393'><i>Comment c’est calculé :</i> %1</p>").arg(q(r.formule).toHtmlEscaped());
  m_resultat->setText(h);
}

QString FenetreCalculateurs::resultat() const { return m_resultat->text(); }

// ======================================================================================================================
// Modèle de simulation d'un composant
// ======================================================================================================================
DialogueModeleSimulation::DialogueModeleSimulation(AtelierElectronique& a, std::string composant, QWidget* parent) : QDialog(parent), m_atelier(a), m_composant(std::move(composant)) {
  setObjectName(QStringLiteral("dialogueModeleSimulation"));
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const ComposantElec* k = c ? c->composant(m_composant) : nullptr;
  setWindowTitle(QStringLiteral("Modèle de simulation de %1").arg(k ? q(k->reference) : QStringLiteral("?")));
  resize(860, 640);
  auto* l = new QVBoxLayout(this);
  auto* onglets = new QTabWidget;
  l->addWidget(onglets, 1);
  // Fiche de modèle intégré, à champs bornés
  {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    m_fiche = new QComboBox;
    m_fiche->setObjectName(QStringLiteral("ficheModele"));
    for (const auto& f : fichesModeles()) m_fiche->addItem(q(f.nom), q(f.id));
    v->addWidget(m_fiche);
    auto* g = new QGroupBox(QStringLiteral("Paramètres"));
    m_champsFiche = new QFormLayout(g);
    v->addWidget(g, 1);
    auto* ecrire = new QPushButton(QStringLiteral("Écrire le modèle"));
    v->addWidget(ecrire);
    auto remplir = [this] {
      while (m_champsFiche->rowCount() > 0) m_champsFiche->removeRow(0);
      m_lireFiche.clear();
      const FicheModele* f = ficheModele(m_fiche->currentData().toString().toStdString());
      if (!f) return;
      for (const auto& ch : f->champs) {
        auto* e = new QLineEdit(QLocale::c().toString(ch.valeur, 'g', 6));
        e->setToolTip(QStringLiteral("De %1 à %2 %3").arg(QLocale::c().toString(ch.min, 'g', 6), QLocale::c().toString(ch.max, 'g', 6), q(ch.unite)));
        m_champsFiche->addRow(QStringLiteral("%1 — %2").arg(q(ch.id), q(ch.nom)), e);
        m_lireFiche.push_back([e] {
          bool ok = false;
          const double x = QLocale::c().toDouble(e->text().replace(QLatin1Char(','), QLatin1Char('.')), &ok);
          return ok ? x : std::numeric_limits<double>::quiet_NaN();
        });
      }
    };
    connect(m_fiche, &QComboBox::currentIndexChanged, this, remplir);
    connect(ecrire, &QPushButton::clicked, this, [this] {
      std::vector<double> v;
      for (const auto& x : m_lireFiche) v.push_back(x());
      QString refus;
      const QString t = ecrireFiche(m_fiche->currentData().toString().toStdString(), v, &refus);
      if (t.isEmpty()) m_note->setText(QStringLiteral("<span style='color:#c53030'>%1</span>").arg(refus.toHtmlEscaped()));
    });
    remplir();
    onglets->addTab(w, QStringLiteral("Modèle intégré"));
  }
  // Depuis la fiche technique : points relevés, ajustement sur son fil
  {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->addWidget(new QLabel(QStringLiteral("Diode : courant direct (A) et tension directe (V) relevés sur la courbe de la fiche, trois points au moins.")));
    m_points = new QTableWidget(3, 2);
    m_points->setObjectName(QStringLiteral("pointsFiche"));
    m_points->setHorizontalHeaderLabels({QStringLiteral("Courant (A)"), QStringLiteral("Tension (V)")});
    const double defauts[3][2] = {{1e-3, 0.6}, {10e-3, 0.72}, {100e-3, 0.93}};
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 2; ++j) m_points->setItem(i, j, new QTableWidgetItem(QLocale::c().toString(defauts[i][j], 'g', 6)));
    v->addWidget(m_points, 1);
    auto* ajust = new QPushButton(QStringLiteral("Ajuster IS, N et RS"));
    v->addWidget(ajust);
    connect(ajust, &QPushButton::clicked, this, [this] {
      std::vector<PointFiche> p;
      for (int i = 0; i < m_points->rowCount(); ++i) {
        bool a1 = false, a2 = false;
        const double x = m_points->item(i, 0) ? m_points->item(i, 0)->text().toDouble(&a1) : 0, y = m_points->item(i, 1) ? m_points->item(i, 1)->text().toDouble(&a2) : 0;
        if (a1 && a2) p.push_back({x, y});
      }
      ajusterDiode(p, false);
    });
    onglets->addTab(w, QStringLiteral("Depuis la fiche"));
  }
  // IBIS
  {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* ouvrir = new QPushButton(QStringLiteral("Fichier IBIS…"));
    m_modelesIbis = new QComboBox;
    m_coin = new QComboBox;
    m_coin->addItems({QStringLiteral("Typique"), QStringLiteral("Minimal"), QStringLiteral("Maximal")});
    auto* gen = new QPushButton(QStringLiteral("Tampon comportemental"));
    for (QWidget* x : std::initializer_list<QWidget*>{ouvrir, m_modelesIbis, m_coin, gen}) v->addWidget(x);
    v->addStretch(1);
    connect(ouvrir, &QPushButton::clicked, this, [this] {
      const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Fichier IBIS"), {}, QStringLiteral("IBIS (*.ibs *.IBS)"));
      if (!f.isEmpty()) lireIbis(f);
    });
    connect(gen, &QPushButton::clicked, this, [this] { tamponIbis(m_modelesIbis->currentText(), m_coin->currentIndex()); });
    onglets->addTab(w, QStringLiteral("IBIS"));
  }
  // Fichier de modèle d'un autre simulateur
  {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* ouvrir = new QPushButton(QStringLiteral("Fichier .lib, .mod, .cir…"));
    auto* compat = new QComboBox;
    compat->addItem(QStringLiteral("ngspice"), QString());
    compat->addItem(QStringLiteral("PSpice"), QStringLiteral("ps"));
    compat->addItem(QStringLiteral("LTspice"), QStringLiteral("lt"));
    compat->addItem(QStringLiteral("HSPICE"), QStringLiteral("hs"));
    v->addWidget(ouvrir);
    v->addWidget(compat);
    v->addStretch(1);
    connect(ouvrir, &QPushButton::clicked, this, [this, compat] {
      const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Modèle SPICE"), {}, QStringLiteral("Modèles (*.lib *.mod *.cir *.sub *.txt)"));
      if (!f.isEmpty()) lireModele(f, compat->currentData().toString());
    });
    onglets->addTab(w, QStringLiteral("Fichier de modèle"));
  }
  m_texte = new QPlainTextEdit;
  m_texte->setObjectName(QStringLiteral("texteModele"));
  m_texte->setFont(QFont(QStringLiteral("monospace")));
  m_texte->setMaximumHeight(180);
  l->addWidget(m_texte);
  m_note = new QLabel;
  m_note->setWordWrap(true);
  l->addWidget(m_note);
  auto* b = new QHBoxLayout;
  auto* rat = new QPushButton(QStringLiteral("Rattacher à l’article du composant"));
  rat->setObjectName(QStringLiteral("rattacherModele"));
  auto* fermer = new QPushButton(QStringLiteral("Fermer"));
  b->addStretch(1);
  b->addWidget(rat);
  b->addWidget(fermer);
  l->addLayout(b);
  connect(rat, &QPushButton::clicked, this, [this] {
    m_propose.texte = m_texte->toPlainText().toStdString();
    rattacher(m_propose);
  });
  connect(fermer, &QPushButton::clicked, this, &QDialog::close);
}

void DialogueModeleSimulation::proposer(const ModeleSpice& m, const QString& note) {
  m_propose = m;
  m_texte->setPlainText(q(m.texte));
  m_note->setText(note);
}

QString DialogueModeleSimulation::ecrireFiche(const std::string& fiche, const std::vector<double>& valeurs, QString* refus) {
  const FicheModele* f = ficheModele(fiche);
  if (!f) return {};
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const ComposantElec* k = c ? c->composant(m_composant) : nullptr;
  const std::string nom = (k ? k->reference : std::string("M")) + "_" + f->id;
  std::string r;
  const auto t = ecrireModele(*f, nom, valeurs, &r);
  if (!t) {
    if (refus) *refus = q(r);
    return {};
  }
  ModeleSpice m;
  m.texte = *t;
  m.nom = nom;
  m.genre = f->genre;
  m.source = "fiche « " + f->nom + " » de Prométhée";
  proposer(m, QStringLiteral("Modèle écrit d’après la fiche « %1 ».").arg(q(f->nom)));
  return q(*t);
}

AjustementModele DialogueModeleSimulation::ajusterDiode(const std::vector<PointFiche>& points, bool attendre) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const ComposantElec* k = c ? c->composant(m_composant) : nullptr;
  const std::string nom = (k ? k->reference : std::string("D")) + "_FICHE";
  auto r = std::make_shared<AjustementModele>();
  m_note->setText(QStringLiteral("Ajustement en cours…"));
  QPointer<DialogueModeleSimulation> moi(this);
  // Levenberg-Marquardt sur son fil : l'interface reste libre
  std::thread fil([moi, r, points, nom] {
    *r = prom::ecad::ajusterDiode(points, nom);
    QMetaObject::invokeMethod(qApp, [moi, r] {
      if (!moi) return;
      if (!r->reussi) {
        moi->m_note->setText(QStringLiteral("<span style='color:#c53030'>%1</span>").arg(q(r->erreur).toHtmlEscaped()));
        return;
      }
      ModeleSpice m;
      m.texte = r->texte;
      m.nom = r->nom;
      m.genre = r->genre;
      m.source = r->source;
      moi->proposer(m, QStringLiteral("Écart maximal aux points de la fiche : %1 %.").arg(QLocale().toString(100 * r->ecartMax, 'g', 3)));
    }, Qt::QueuedConnection);
  });
  if (attendre) {
    fil.join();
    QCoreApplication::processEvents();
  } else
    fil.detach();
  return *r;
}

bool DialogueModeleSimulation::lireModele(const QString& chemin, const QString& compatibilite) {
  try {
    ModeleSpice m;
    m.texte = prom::lireFichier(prom::cheminFichier(chemin.toStdString()));
    m.nom = nomModele(m.texte);
    m.compatibilite = compatibilite.toStdString();
    m.fichier = QFileInfo(chemin).fileName().toStdString();
    m.genre = QString::fromStdString(m.texte).contains(QStringLiteral(".subckt"), Qt::CaseInsensitive) ? "sous-circuit" : "modèle";
    m.source = "fichier « " + m.fichier + " »";
    if (m.nom.empty()) {
      m_note->setText(QStringLiteral("<span style='color:#c53030'>Aucun .model ni .subckt dans « %1 ».</span>").arg(q(m.fichier).toHtmlEscaped()));
      return false;
    }
    const AdaptationModele ad = adapterModele(m.texte, m.compatibilite);
    QString note = QStringLiteral("« %1 » : %2 adaptation(s) pour ngspice au calcul").arg(q(m.nom).toHtmlEscaped()).arg(ad.notes.size());
    for (const auto& n : ad.notes) note += QStringLiteral("<br>%1").arg(q(n).toHtmlEscaped());
    for (const auto& r : ad.refus) note += QStringLiteral("<br><span style='color:#c53030'>%1</span>").arg(q(r).toHtmlEscaped());
    proposer(m, note);
    return ad.lisible();
  } catch (const std::exception& e) {
    m_note->setText(QString::fromUtf8(e.what()).toHtmlEscaped());
    return false;
  }
}

bool DialogueModeleSimulation::lireIbis(const QString& chemin) {
  std::string erreur;
  try {
    m_ibis = prom::ecad::lireIbis(prom::lireFichier(prom::cheminFichier(chemin.toStdString())), &erreur);
  } catch (const std::exception& e) {
    erreur = e.what();
    m_ibis.reset();
  }
  m_modelesIbis->clear();
  if (!m_ibis) {
    m_note->setText(QStringLiteral("<span style='color:#c53030'>%1</span>").arg(q(erreur).toHtmlEscaped()));
    return false;
  }
  for (const auto& m : m_ibis->modeles) m_modelesIbis->addItem(q(m.nom));
  m_note->setText(QStringLiteral("IBIS %1 : %2 (%3), %4 modèle(s).").arg(q(m_ibis->version), q(m_ibis->composant), q(m_ibis->fabricant)).arg(m_ibis->modeles.size()));
  return true;
}

AjustementModele DialogueModeleSimulation::tamponIbis(const QString& modele, int coin) {
  AjustementModele r;
  if (!m_ibis) return r;
  OptionsTamponIbis o;
  o.modele = modele.toStdString();
  o.coin = coin;
  r = prom::ecad::tamponIbis(*m_ibis, o, modele.toStdString() + (coin == 0 ? "_TYP" : coin == 1 ? "_MIN" : "_MAX"));
  if (!r.reussi) {
    m_note->setText(QStringLiteral("<span style='color:#c53030'>%1</span>").arg(q(r.erreur).toHtmlEscaped()));
    return r;
  }
  ModeleSpice m;
  m.texte = r.texte;
  m.nom = r.nom;
  m.genre = r.genre;
  m.source = r.source;
  proposer(m, q(r.source));
  return r;
}

bool DialogueModeleSimulation::rattacher(const ModeleSpice& m) {
  if (m.vide()) return false;
  const std::string uuid = m_composant;
  const bool fait = m_atelier.modifierConception([&](ConceptionElec& c) {
    ComposantElec* k = c.composant(uuid);
    if (!k) return;
    if (k->article.empty() || !c.articles.count(k->article)) {
      Article a;
      a.id = "modele-" + k->reference;
      a.nom = k->reference + " (modèle de simulation)";
      a.mpn = k->mpn;
      a.fabricant = k->fabricant;
      a.symbole = k->symbole;
      a.empreinte = k->empreinte;
      c.articles[a.id] = a;
      k->article = a.id;
    }
    c.articles[k->article].modeleSpice = m;
    k->champs.erase("Sim.Modele");
    k->champs.erase("Sim.Bibliotheque");
  }, QStringLiteral("Modèle de simulation rattaché à l’article"));
  if (fait) m_note->setText(QStringLiteral("Modèle « %1 » rattaché à l’article du composant.").arg(q(m.nom)));
  return fait;
}

// ======================================================================================================================
// Commandes de l'atelier (lot L11)
// ======================================================================================================================
FenetreCalculateurs* AtelierElectronique::calculateurs() {
  if (!m_calculateurs) m_calculateurs = new FenetreCalculateurs(this);
  m_calculateurs->show();
  m_calculateurs->raise();
  return m_calculateurs;
}

DialogueModeleSimulation* AtelierElectronique::modeleSimulation(const std::string& composant) {
  std::string uuid = composant;
  if (uuid.empty() && m_selection && !m_selection->composants().empty()) uuid = *m_selection->composants().begin();
  const ConceptionElec* c = conceptionCourante();
  if (uuid.empty() || !c || !c->composant(uuid)) {
    afficherMessage(QStringLiteral("Choisis d’abord le composant dont il faut le modèle de simulation."), NiveauMessage::Avertissement);
    return nullptr;
  }
  if (m_modeleSimulation) m_modeleSimulation->close();
  m_modeleSimulation = new DialogueModeleSimulation(*this, uuid, this);
  m_modeleSimulation->setAttribute(Qt::WA_DeleteOnClose);
  m_modeleSimulation->show();
  return m_modeleSimulation;
}

void AtelierElectronique::enregistrerCommandesL11() {
  auto& c = m_commandes;
  c["simulation.calculateurs"] = [this] { calculateurs(); };
  c["simulation.modele"] = [this] { modeleSimulation(); };
}

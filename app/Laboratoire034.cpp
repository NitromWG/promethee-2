// Prométhée : laboratoire de simulation de l'atelier électronique (lot L14 de la 0.34). Voir Laboratoire034.hpp.
// Licence GPL-3.0-only.
#include "Laboratoire034.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "FenetreSimulation.hpp"
#include "VueSchema.hpp"
#include "ZoneMessages.hpp"
#include "promethee/analyses_natives.hpp"
#include "promethee/analyseurs_numeriques.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/valeurs.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string minuscules(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
bool estExterne(const ComposantElec& x) {
  if (x.symbole != "Source_tension") return false;
  const std::string v = minuscules(x.valeur);
  return v == "externe" || v == "external" || v.rfind("generateur", 0) == 0 || v.rfind("générateur", 0) == 0;
}
QDoubleSpinBox* champ(const char* nom, double min, double max, double valeur, const QString& suffixe, int decimales = 3) {
  auto* s = new QDoubleSpinBox;
  s->setObjectName(QLatin1String(nom));
  s->setRange(min, max);
  s->setDecimals(decimales);
  s->setValue(valeur);
  s->setSuffix(suffixe);
  return s;
}
}  // namespace

// ======================================================================================================================
// Afficheur
// ======================================================================================================================
AfficheurSegments::AfficheurSegments(QWidget* parent) : QWidget(parent) { setMinimumSize(40, 64); }

void AfficheurSegments::setSegments(std::uint8_t s) {
  if (s == m_segments) return;
  m_segments = s;
  update();
}

void AfficheurSegments::paintEvent(QPaintEvent*) {
  QPainter g(this);
  g.setRenderHint(QPainter::Antialiasing);
  g.fillRect(rect(), QColor(0x18, 0x1A, 0x1F));
  const double w = width(), h = height(), e = std::max(3.0, w / 9);
  const double x0 = w * 0.22, x1 = w * 0.78, y0 = h * 0.12, y1 = h * 0.5, y2 = h * 0.88;
  struct Seg {
    QPointF a, b;
  };
  const Seg segs[7] = {{{x0, y0}, {x1, y0}}, {{x1, y0}, {x1, y1}}, {{x1, y1}, {x1, y2}}, {{x0, y2}, {x1, y2}}, {{x0, y1}, {x0, y2}}, {{x0, y0}, {x0, y1}}, {{x0, y1}, {x1, y1}}};
  for (int i = 0; i < 7; ++i) {
    g.setPen(QPen((m_segments >> i) & 1 ? QColor(0xFF, 0x3B, 0x30) : QColor(0x3A, 0x22, 0x22), e, Qt::SolidLine, Qt::RoundCap));
    g.drawLine(segs[i].a, segs[i].b);
  }
}

// ======================================================================================================================
// Fenêtre
// ======================================================================================================================
FenetreLaboratoire::FenetreLaboratoire(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a), m_session(std::make_unique<SimulationContinue>()) {
  setObjectName(QStringLiteral("fenetreLaboratoire"));
  setWindowTitle(QStringLiteral("Laboratoire de simulation"));
  resize(1180, 760);
  auto* col = new QVBoxLayout(this);
  m_onglets = new QTabWidget;
  m_onglets->setObjectName(QStringLiteral("ongletsLaboratoire"));
  col->addWidget(m_onglets);
  m_etat = new QLabel;
  m_etat->setObjectName(QStringLiteral("etatLaboratoire"));
  m_etat->setWordWrap(true);
  col->addWidget(m_etat);
  auto page = [&](const char* id, const QString& titre) {
    auto* w = new QWidget;
    w->setObjectName(QLatin1String(id));
    m_onglets->addTab(w, titre);
    return w;
  };
  construireContinu(page("continu", QStringLiteral("Simulation continue")));
  construireLogique(page("logique", QStringLiteral("Logique")));
  construireSymbolique(page("symbolique", QStringLiteral("Symbolique et analyses")));
  construireReseau(page("reseau", QStringLiteral("Analyseur de réseau")));
  construireAlimentation(page("alimentation", QStringLiteral("Alimentation")));
  construireMesures(page("mesures", QStringLiteral("Mesures et moteurs")));
  m_minuterie = new QTimer(this);
  m_minuterie->setInterval(30);
  connect(m_minuterie, &QTimer::timeout, this, &FenetreLaboratoire::rafraichir);
  majReseaux();
}

// La vue du schéma peut déjà être détruite (fenêtre détruite avec l'atelier) : la session seule s'arrête
FenetreLaboratoire::~FenetreLaboratoire() {
  if (m_minuterie) m_minuterie->stop();
  if (m_session) m_session->arreter();
}

bool FenetreLaboratoire::montrer(const QString& onglet) {
  majReseaux();  // la conception ouverte a pu changer
  if (!enCours()) construireCommandes();
  if (onglet.isEmpty()) {
    show();
    raise();
    return true;
  }
  for (int i = 0; i < m_onglets->count(); ++i)
    if (m_onglets->widget(i)->objectName() == onglet) {
      m_onglets->setCurrentIndex(i);
      show();
      raise();
      return true;
    }
  return false;
}

void FenetreLaboratoire::majReseaux() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  QStringList reseaux, sources;
  if (c) {
    const Connectivite k = connectivite(*c);
    for (const auto& r : k.reseaux)
      if (!r.broches.empty()) reseaux << q(r.nom);
    for (const auto& x : c->composants)
      if (x.symbole == "Source_tension" || x.symbole == "Pile") sources << q(x.reference);
  }
  reseaux.sort();
  for (QComboBox* b : {m_voie1, m_voie2, m_reseauMulti, m_sda, m_scl, m_sortie, m_port1, m_port2}) {
    if (!b) continue;
    const QString avant = b->currentText();
    b->clear();
    b->addItems(reseaux);
    if (!avant.isEmpty()) b->setCurrentText(avant);
  }
  if (m_entree) {
    m_entree->clear();
    m_entree->addItems(sources);
  }
}

std::string FenetreLaboratoire::sonde(const QString& reseau) const { return vecteurTension(m_netlist, reseau.toStdString()); }

// ---------- Simulation continue ----------
void FenetreLaboratoire::construireContinu(QWidget* page) {
  auto* col = new QVBoxLayout(page);
  auto* barre = new QHBoxLayout;
  auto* demarrer = new QPushButton(QStringLiteral("▶ Démarrer"));
  demarrer->setObjectName(QStringLiteral("demarrerContinu"));
  auto* pause = new QPushButton(QStringLiteral("❚❚ Pause"));
  pause->setObjectName(QStringLiteral("pauseContinu"));
  pause->setCheckable(true);
  auto* arreter = new QPushButton(QStringLiteral("■ Arrêter"));
  arreter->setObjectName(QStringLiteral("arreterContinu"));
  m_vitesse = champ("vitesseContinu", 0.01, 10, 1, QStringLiteral(" × temps réel"), 2);
  m_pas = champ("pasContinu", 0.001, 10, 1, QStringLiteral(" ms"), 3);
  m_temps = new QLabel(QStringLiteral("t = 0 s"));
  m_temps->setObjectName(QStringLiteral("tempsContinu"));
  for (QWidget* w : std::initializer_list<QWidget*>{demarrer, pause, arreter, new QLabel(QStringLiteral("Vitesse")), m_vitesse, new QLabel(QStringLiteral("Pas")), m_pas, m_temps}) barre->addWidget(w);
  barre->addStretch(1);
  col->addLayout(barre);
  auto* coupe = new QSplitter(Qt::Horizontal);
  auto* gauche = new QGroupBox(QStringLiteral("Commandes et voyants du schéma"));
  m_dispositionCommandes = new QVBoxLayout(gauche);
  m_commandes = gauche;
  coupe->addWidget(gauche);
  auto* droite = new QWidget;
  auto* dcol = new QVBoxLayout(droite);
  auto* rangOscillo = new QHBoxLayout;
  m_voie1 = new QComboBox;
  m_voie1->setObjectName(QStringLiteral("voie1Oscillo"));
  m_voie2 = new QComboBox;
  m_voie2->setObjectName(QStringLiteral("voie2Oscillo"));
  m_secDiv = champ("secDivOscillo", 1e-3, 1000, 1, QStringLiteral(" ms/div"), 3);
  for (QWidget* w : std::initializer_list<QWidget*>{new QLabel(QStringLiteral("Voie 1")), m_voie1, new QLabel(QStringLiteral("Voie 2")), m_voie2, m_secDiv}) rangOscillo->addWidget(w);
  dcol->addLayout(rangOscillo);
  m_oscillo = new TraceCourbes;
  m_oscillo->setObjectName(QStringLiteral("oscilloscope"));
  m_oscillo->setMinimumHeight(240);
  dcol->addWidget(m_oscillo, 1);
  auto* rangMulti = new QHBoxLayout;
  m_reseauMulti = new QComboBox;
  m_reseauMulti->setObjectName(QStringLiteral("reseauMultimetre"));
  m_modeMulti = new QComboBox;
  m_modeMulti->setObjectName(QStringLiteral("modeMultimetre"));
  m_modeMulti->addItems({QStringLiteral("V continu"), QStringLiteral("V efficace alternatif"), QStringLiteral("Fréquence")});
  m_multimetre = new QLabel(QStringLiteral("—"));
  m_multimetre->setObjectName(QStringLiteral("multimetre"));
  QFont f = m_multimetre->font();
  f.setPointSize(f.pointSize() + 6);
  f.setBold(true);
  m_multimetre->setFont(f);
  for (QWidget* w : std::initializer_list<QWidget*>{new QLabel(QStringLiteral("Multimètre")), m_reseauMulti, m_modeMulti, m_multimetre}) rangMulti->addWidget(w);
  rangMulti->addStretch(1);
  auto* instruments = new QPushButton(QStringLiteral("Instruments du schéma"));
  instruments->setObjectName(QStringLiteral("instrumentsSchema"));
  rangMulti->addWidget(instruments);
  dcol->addLayout(rangMulti);
  coupe->addWidget(droite);
  coupe->setStretchFactor(1, 3);
  col->addWidget(coupe, 1);
  connect(demarrer, &QPushButton::clicked, this, [this] {
    QString e;
    if (!this->demarrer(&e)) m_etat->setText(e);
  });
  connect(pause, &QPushButton::toggled, this, [this](bool oui) { m_session->setPause(oui); });
  connect(arreter, &QPushButton::clicked, this, &FenetreLaboratoire::arreter);
  connect(m_vitesse, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_session->setVitesse(v); });
  connect(instruments, &QPushButton::clicked, this, [this] {
    QStringList t;
    for (const auto& [r, v] : mesurerInstruments()) t << QStringLiteral("%1 : %2").arg(r).arg(v, 0, 'g', 6);
    m_etat->setText(t.isEmpty() ? QStringLiteral("Aucun instrument posé au schéma (symbole « Instrument »).") : t.join(QStringLiteral(" · ")));
  });
  construireCommandes();
}

void FenetreLaboratoire::construireCommandes() {
  // Réglages gardés d'une construction à l'autre (même repère) : générateurs et interrupteurs
  struct Reglage {
    int forme = 0;
    double frequence = 1000, amplitude = 1;
  };
  std::map<QString, Reglage> generateurs;
  std::map<QString, bool> fermes;
  for (const auto& [ref, f] : m_frequences) generateurs[ref] = {m_formes.count(ref) ? m_formes[ref]->currentIndex() : 0, f->value(), m_amplitudes.count(ref) ? m_amplitudes[ref]->value() : 1};
  for (const auto& [ref, b] : m_boutons)
    if (b->isCheckable()) fermes[ref] = b->isChecked();
  while (QLayoutItem* it = m_dispositionCommandes->takeAt(0)) {
    if (it->widget()) it->widget()->deleteLater();
    delete it;
  }
  m_boutons.clear();
  m_frequences.clear();
  m_formes.clear();
  m_amplitudes.clear();
  m_afficheurs.clear();
  m_leds.clear();
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  for (const auto& x : c->composants) {
    const QString ref = q(x.reference);
    if (x.symbole == "Bouton" || x.symbole == "Interrupteur") {
      auto* b = new QPushButton(x.symbole == "Bouton" ? QStringLiteral("%1 (appuyer)").arg(ref) : ref);
      b->setObjectName(QStringLiteral("commande_") + ref);
      const std::string source = sourceCommande(x.reference);
      if (x.symbole == "Interrupteur") {
        b->setCheckable(true);
        b->setChecked(fermes.count(ref) ? fermes[ref] : x.champs.count("Sim.Ferme") && x.champs.at("Sim.Ferme") == "1");
        connect(b, &QPushButton::toggled, this, [this, source](bool oui) { m_session->regler(source, oui ? 1 : 0); });
      } else {
        connect(b, &QPushButton::pressed, this, [this, source] { m_session->regler(source, 1); });
        connect(b, &QPushButton::released, this, [this, source] { m_session->regler(source, 0); });
      }
      m_boutons[ref] = b;
      m_dispositionCommandes->addWidget(b);
    } else if (estExterne(x)) {
      auto* ligne = new QWidget;
      auto* l = new QHBoxLayout(ligne);
      l->setContentsMargins(0, 0, 0, 0);
      auto* forme = new QComboBox;
      forme->setObjectName(QStringLiteral("forme_") + ref);
      forme->addItems({QStringLiteral("sinus"), QStringLiteral("carré"), QStringLiteral("triangle"), QStringLiteral("dent de scie"), QStringLiteral("impulsions")});
      auto* fr = new QDoubleSpinBox;
      fr->setObjectName(QStringLiteral("frequence_") + ref);
      fr->setRange(0.01, 1e7);
      fr->setDecimals(2);
      fr->setValue(1000);
      fr->setSuffix(QStringLiteral(" Hz"));
      auto* am = new QDoubleSpinBox;
      am->setObjectName(QStringLiteral("amplitude_") + ref);
      am->setRange(0, 1000);
      am->setDecimals(3);
      am->setValue(1);
      am->setSuffix(QStringLiteral(" V"));
      if (const auto it = generateurs.find(ref); it != generateurs.end()) {
        forme->setCurrentIndex(it->second.forme);
        fr->setValue(it->second.frequence);
        am->setValue(it->second.amplitude);
      }
      l->addWidget(new QLabel(ref));
      l->addWidget(forme);
      l->addWidget(fr);
      l->addWidget(am);
      const std::string source = sourceExterne(x.reference);
      auto regler = [this, source, forme, fr, am] {
        Generateur g;
        static const Generateur::Forme formes[] = {Generateur::Forme::Sinus, Generateur::Forme::Carre, Generateur::Forme::Triangle, Generateur::Forme::DentDeScie, Generateur::Forme::Impulsions};
        g.forme = formes[std::clamp(forme->currentIndex(), 0, 4)];
        g.frequence = fr->value();
        g.amplitude = am->value();
        m_session->regler(source, g);
      };
      connect(forme, &QComboBox::currentIndexChanged, this, regler);
      connect(fr, &QDoubleSpinBox::valueChanged, this, regler);
      connect(am, &QDoubleSpinBox::valueChanged, this, regler);
      regler();
      m_frequences[ref] = fr;
      m_formes[ref] = forme;
      m_amplitudes[ref] = am;
      m_dispositionCommandes->addWidget(ligne);
    } else if (x.symbole == "Afficheur_7seg") {
      auto* ligne = new QWidget;
      auto* l = new QHBoxLayout(ligne);
      l->setContentsMargins(0, 0, 0, 0);
      auto* a = new AfficheurSegments;
      a->setObjectName(QStringLiteral("afficheur_") + ref);
      l->addWidget(new QLabel(ref));
      l->addWidget(a);
      l->addStretch(1);
      m_afficheurs[ref] = a;
      m_dispositionCommandes->addWidget(ligne);
    } else if (x.symbole == "LED") {
      auto* v = new QLabel(QStringLiteral("● %1").arg(ref));
      v->setObjectName(QStringLiteral("led_") + ref);
      v->setProperty("allumee", false);
      v->setStyleSheet(QStringLiteral("color: #555;"));
      m_leds[ref] = v;
      m_dispositionCommandes->addWidget(v);
    }
  }
  m_dispositionCommandes->addStretch(1);
}

bool FenetreLaboratoire::demarrer(QString* erreur) {
  arreter();
  const ConceptionElec* c = m_atelier.conceptionCourante();
  auto refuser = [&](const QString& e) {
    m_etat->setText(e);
    if (erreur) *erreur = e;
    return false;
  };
  if (!c) return refuser(QStringLiteral("Aucune conception ouverte."));
  if (!ngspiceDisponible()) return refuser(QStringLiteral("ngspice n’est pas installé : la simulation continue en a besoin."));
  construireCommandes();
  majReseaux();
  OptionsContinue oc;
  oc.pas = m_pas->value() * 1e-3;
  // Au plus deux millions de pas : ngspice réserve ses vecteurs d'après la durée et le pas
  oc.duree = std::min(3600.0, 2e6 * oc.pas);
  oc.vitesse = m_vitesse->value();
  OptionsSimulation os;
  os.cosimulation = m_cosimulation;
  const NetlistSpice n0 = netlistContinue(*c, oc, os);
  if (!n0.valide()) return refuser(q(n0.erreurs.front()));
  std::vector<std::string> sondes;
  for (const auto& [reseau, noeud] : n0.noeuds)
    if (noeud != "0") sondes.push_back(vecteurTension(n0, reseau));
  for (const auto& v : sondesRetourVisuel(*c, n0)) sondes.push_back(v);
  for (const auto& p : m_participants)
    for (const auto& v : p->sondes()) sondes.push_back(v);
  m_netlist = netlistContinue(*c, oc, os, sondes);
  m_session->retirerParticipants();
  for (const auto& p : m_participants) m_session->ajouter(p);
  for (const auto& [ref, b] : m_boutons) m_session->regler(sourceCommande(ref.toStdString()), b->isCheckable() && b->isChecked() ? 1 : 0);
  // Vecteurs lus par les voyants : segments et commun des afficheurs, courant des diodes
  m_vecteursAfficheurs.clear();
  m_vecteursLed.clear();
  const Connectivite k = connectivite(*c);
  for (const auto& [ref, a] : m_afficheurs)
    if (const ComposantElec* x = c->parReference(ref.toStdString())) {
      std::array<std::string, 8> v;
      for (std::size_t i = 0; i < v.size(); ++i) v[i] = vecteurTension(m_netlist, k.nom(x->uuid, std::to_string(i + 1)));
      m_vecteursAfficheurs[ref] = v;
    }
  for (const auto& [inst, ref] : m_netlist.instances)
    if (!inst.empty() && inst[0] == 'd' && m_leds.count(q(ref))) m_vecteursLed[q(ref)] = "@" + inst + "[id]";
  std::string e;
  if (!m_session->demarrer(m_netlist.texte, sondes, oc, &e)) return refuser(QStringLiteral("La simulation continue ne démarre pas : %1").arg(q(e)));
  m_actifs = std::move(m_participants);
  m_participants.clear();
  m_cosimulation.clear();
  m_tours = 0;
  m_minuterie->start();
  m_etat->setText(QStringLiteral("Simulation continue en cours : boutons, interrupteurs et générateurs se règlent pendant le calcul."));
  return true;
}

void FenetreLaboratoire::arreter() {
  if (m_minuterie) m_minuterie->stop();
  if (m_session) {
    m_session->arreter();
    m_session->retirerParticipants();
  }
  m_actifs.clear();
  if (m_atelier.vueSchema()) m_atelier.vueSchema()->effacerRetourVisuel();
}

void FenetreLaboratoire::ajouterParticipant(std::shared_ptr<ParticipantContinu> p, const std::string& reference, const std::string& genre) {
  if (!p) return;
  m_participants.push_back(std::move(p));
  m_cosimulation[reference] = genre;
}

void FenetreLaboratoire::setPas(double secondes) { m_pas->setValue(secondes * 1e3); }

bool FenetreLaboratoire::enCours() const { return m_session && m_session->enCours(); }
void FenetreLaboratoire::setVitesse(double v) { m_vitesse->setValue(v); }
QPushButton* FenetreLaboratoire::commande(const QString& r) const {
  const auto it = m_boutons.find(r);
  return it == m_boutons.end() ? nullptr : it->second;
}
QDoubleSpinBox* FenetreLaboratoire::frequence(const QString& r) const {
  const auto it = m_frequences.find(r);
  return it == m_frequences.end() ? nullptr : it->second;
}
int FenetreLaboratoire::chiffre(const QString& r) const {
  const auto it = m_afficheurs.find(r);
  return it == m_afficheurs.end() ? -1 : chiffreDesSegments(it->second->segments());
}
bool FenetreLaboratoire::ledAllumee(const QString& r) const {
  const auto it = m_leds.find(r);
  return it != m_leds.end() && it->second->property("allumee").toBool();
}

void FenetreLaboratoire::reglerOscilloscope(const QString& v1, const QString& v2, double spd) {
  m_voie1->setCurrentText(v1);
  m_voie2->setCurrentText(v2);
  m_secDiv->setValue(spd * 1e3);
}
void FenetreLaboratoire::reglerMultimetre(const QString& r, int mode) {
  m_reseauMulti->setCurrentText(r);
  m_modeMulti->setCurrentIndex(mode);
}

void FenetreLaboratoire::rafraichir() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  const double t = m_session->temps();
  m_temps->setText(QStringLiteral("t = %1 s%2").arg(t, 0, 'f', 3).arg(m_session->enCours() ? QString() : QStringLiteral(" (arrêtée)")));
  // Masse (vecteur vide) et valeur inconnue : 0
  auto lire = [this](const std::string& v) {
    const double x = v.empty() ? 0.0 : m_session->valeur(v);
    return std::isfinite(x) ? x : 0.0;
  };
  for (const auto& [ref, a] : m_afficheurs) {
    const auto it = m_vecteursAfficheurs.find(ref);
    if (it == m_vecteursAfficheurs.end()) continue;
    std::uint8_t s = 0;
    const double com = lire(it->second[7]);
    for (std::size_t i = 0; i < 7; ++i)
      if (lire(it->second[i]) - com > 1.0) s |= static_cast<std::uint8_t>(1u << i);
    a->setSegments(s);
  }
  for (const auto& [ref, l] : m_leds) {
    const auto it = m_vecteursLed.find(ref);
    const double i = it == m_vecteursLed.end() ? 0.0 : lire(it->second);
    const bool allumee = i > 1e-3;
    if (l->property("allumee").toBool() != allumee) {
      l->setProperty("allumee", allumee);
      l->setStyleSheet(allumee ? QStringLiteral("color: #FF3B30; font-weight: bold;") : QStringLiteral("color: #555;"));
    }
  }
  // Multimètre : la dernière fenêtre de 0,2 s
  if (const std::string v = sonde(m_reseauMulti->currentText()); !v.empty()) {
    const Releve r = m_session->echantillons(v, t - 0.2);
    const MesureSignal m = mesurerSignal(r);
    if (m_modeMulti->currentIndex() == 2) {
      m_mesure = frequenceSignal(r);
      m_multimetre->setText(q(ecrireSI(m_mesure, "Hz")));
    } else {
      m_mesure = m_modeMulti->currentIndex() == 1 ? m.efficaceAlternatif : m.moyenne;
      m_multimetre->setText(q(ecrireSI(m_mesure, "V")));
    }
  }
  // Oscilloscope
  const double fenetre = m_secDiv->value() * 1e-3 * 10;
  std::vector<Releve> voies;
  QStringList noms;
  for (QComboBox* b : {m_voie1, m_voie2})
    if (const std::string v = sonde(b->currentText()); !v.empty()) {
      voies.push_back(m_session->echantillons(v, t - 2.5 * fenetre));
      noms << b->currentText();
    }
  if (!voies.empty()) {
    ReglagesOscillo ro;
    ro.voies.assign(voies.size(), VoieOscillo{});
    ro.secondesParDivision = fenetre / 10;
    const MesureSignal m0 = mesurerSignal(voies[0]);
    ro.niveau = m0.valide ? (m0.min + m0.max) / 2 : 0;
    const TraceOscillo tr = traceOscilloscope(voies, ro);
    std::vector<TraceCourbes::Courbe> courbes;
    const QColor teintes[2] = {QColor(0xF5, 0xC5, 0x18), QColor(0x2A, 0xC3, 0xE8)};
    for (std::size_t i = 0; i < tr.voies.size(); ++i) {
      TraceCourbes::Courbe cb;
      cb.nom = noms[static_cast<int>(i)];
      cb.couleur = teintes[i % 2];
      for (const auto& e : tr.voies[i]) cb.x.push_back(e.t), cb.y.push_back(e.v);
      courbes.push_back(cb);
    }
    m_oscillo->setCourbes(std::move(courbes), QStringLiteral("t (s)"), QStringLiteral("V"), false);
  }
  // Retour visuel sur le schéma, toutes les 100 ms
  if (++m_tours % 3 == 0 && m_atelier.vueSchema()) {
    std::map<std::string, double> valeurs;
    double vmax = 0;
    for (const auto& [reseau, noeud] : m_netlist.noeuds) {
      if (noeud == "0") continue;
      const double v = m_session->valeur(vecteurTension(m_netlist, reseau));
      if (std::isfinite(v)) valeurs[noeud] = v, vmax = std::max(vmax, std::abs(v));
    }
    for (const auto& v : sondesRetourVisuel(*c, m_netlist)) valeurs[v] = m_session->valeur(v);
    const RetourVisuel r = retourVisuel(*c, m_netlist, valeurs);
    m_atelier.vueSchema()->setRetourVisuel(r, vmax);
    if (!r.fumees.empty()) {
      QStringList t;
      for (const auto& f : r.fumees) t << q(f.message);
      m_etat->setText(QStringLiteral("Hors limites : %1").arg(t.join(QStringLiteral(" · "))));
    }
  }
}

std::map<QString, double> FenetreLaboratoire::mesurerInstruments() {
  std::map<QString, double> v;
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return v;
  for (const auto& p : instrumentsDuSchema(*c)) {
    // Porte de 0,2 s, comme un compteur de laboratoire
    const Releve r = m_session->echantillons(sonde(q(p.plus)), m_session->temps() - 0.2);
    if (!p.script.empty()) {
      const ResultatInstrument ri = executerInstrument(p.script, {{p.plus, r}});
      for (const auto& [nom, x] : ri.valeurs) v[q(p.reference) + QStringLiteral(".") + q(nom)] = x;
      if (!ri.reussi) m_etat->setText(q(ri.erreurs));
    } else if (minuscules(p.genre).find("fr") != std::string::npos)
      v[q(p.reference)] = frequencemetre(r);
    else
      v[q(p.reference)] = mesurerSignal(r).efficace;
  }
  return v;
}

// ---------- Logique ----------
void FenetreLaboratoire::construireLogique(QWidget* page) {
  auto* col = new QVBoxLayout(page);
  auto* l = new QHBoxLayout;
  m_sda = new QComboBox;
  m_sda->setObjectName(QStringLiteral("sdaLogique"));
  m_scl = new QComboBox;
  m_scl->setObjectName(QStringLiteral("sclLogique"));
  auto* i2c = new QPushButton(QStringLiteral("Décoder I2C"));
  i2c->setObjectName(QStringLiteral("decoderI2C"));
  for (QWidget* w : std::initializer_list<QWidget*>{new QLabel(QStringLiteral("SDA")), m_sda, new QLabel(QStringLiteral("SCL")), m_scl, i2c}) l->addWidget(w);
  l->addStretch(1);
  col->addLayout(l);
  m_trames = new QPlainTextEdit;
  m_trames->setObjectName(QStringLiteral("tramesLogique"));
  m_trames->setReadOnly(true);
  col->addWidget(m_trames, 1);
  connect(i2c, &QPushButton::clicked, this, [this] { decoderI2C(m_sda->currentText(), m_scl->currentText()); });
}

QString FenetreLaboratoire::decoderI2C(const QString& sda, const QString& scl) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return {};
  majReseaux();
  m_sda->setCurrentText(sda);
  m_scl->setCurrentText(scl);
  Releve rs, rc;
  if (enCours() && !m_session->echantillons(sonde(sda)).empty()) {
    rs = m_session->echantillons(sonde(sda));
    rc = m_session->echantillons(sonde(scl));
  } else {
    // Sans session : un transitoire de 2 ms du schéma
    AnalyseSpice a;
    a.type = AnalyseSpice::Type::Transitoire;
    a.pas = 0.2e-6;
    a.duree = 2e-3;
    OptionsSimulation o;
    o.pasMax = a.pas;
    const NetlistSpice n = netlistSpice(*c, a, o);
    const ResultatSimulation r = simuler(n);
    const auto* t = r.reel("time");
    const auto* vs = r.reel(vecteurTension(n, sda.toStdString()));
    const auto* vc = r.reel(vecteurTension(n, scl.toStdString()));
    for (std::size_t i = 0; t && vs && vc && i < t->size(); ++i) rs.push_back({(*t)[i], (*vs)[i]}), rc.push_back({(*t)[i], (*vc)[i]});
  }
  double haut = 0;
  for (const auto& e : rs) haut = std::max(haut, e.v);
  const auto trames = prom::ecad::decoderI2C(numeriser(rs, 0.3 * haut, 0.7 * haut), numeriser(rc, 0.3 * haut, 0.7 * haut));
  QStringList t;
  for (const auto& f : trames) {
    QString bits;
    for (const auto& b : f.bits) bits += b.role == "départ" ? QStringLiteral("S ") : b.role == "arrêt" ? QStringLiteral(" P") : b.role == "accusé" ? QStringLiteral("A ") : b.role == "non-accusé" ? QStringLiteral("N ") : QString::number(b.valeur);
    t << QStringLiteral("%1 s : %2   [%3]").arg(f.debut, 0, 'g', 6).arg(q(f.texte()), bits);
  }
  const QString r = t.isEmpty() ? QStringLiteral("Aucune trame I2C.") : t.join(QLatin1Char('\n'));
  m_trames->setPlainText(r);
  return r;
}

// ---------- Symbolique ----------
void FenetreLaboratoire::construireSymbolique(QWidget* page) {
  auto* col = new QVBoxLayout(page);
  auto* l = new QHBoxLayout;
  m_entree = new QComboBox;
  m_entree->setObjectName(QStringLiteral("entreeSymbolique"));
  m_sortie = new QComboBox;
  m_sortie->setObjectName(QStringLiteral("sortieSymbolique"));
  auto* calculer = new QPushButton(QStringLiteral("H(s)"));
  calculer->setObjectName(QStringLiteral("calculerSymbolique"));
  for (QWidget* w : std::initializer_list<QWidget*>{new QLabel(QStringLiteral("Entrée")), m_entree, new QLabel(QStringLiteral("Sortie")), m_sortie, calculer}) l->addWidget(w);
  l->addStretch(1);
  col->addLayout(l);
  m_formule = new QLabel;
  m_formule->setObjectName(QStringLiteral("formuleSymbolique"));
  QFont f = m_formule->font();
  f.setPointSize(f.pointSize() + 4);
  m_formule->setFont(f);
  m_formule->setTextInteractionFlags(Qt::TextSelectableByMouse);
  col->addWidget(m_formule);
  m_latex = new QLineEdit;
  m_latex->setObjectName(QStringLiteral("latexSymbolique"));
  m_latex->setReadOnly(true);
  col->addWidget(m_latex);
  auto* natifs = new QHBoxLayout;
  const char* noms[4] = {"Fonction de transfert", "Pôles et zéros", "Bruit", "Fourier"};
  for (int i = 0; i < 4; ++i) {
    auto* b = new QPushButton(QString::fromUtf8(noms[i]));
    b->setObjectName(QStringLiteral("natif%1").arg(i));
    connect(b, &QPushButton::clicked, this, [this, i] { analyseNative(i); });
    natifs->addWidget(b);
  }
  natifs->addStretch(1);
  col->addLayout(natifs);
  m_natif = new QPlainTextEdit;
  m_natif->setObjectName(QStringLiteral("resultatNatif"));
  m_natif->setReadOnly(true);
  col->addWidget(m_natif, 1);
  connect(calculer, &QPushButton::clicked, this, [this] { calculerSymbolique(m_entree->currentText(), m_sortie->currentText()); });
}

bool FenetreLaboratoire::calculerSymbolique(const QString& entree, const QString& sortie) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  majReseaux();
  m_entree->setCurrentText(entree);
  m_sortie->setCurrentText(sortie);
  std::string e;
  m_fonction = transfertSymbolique(*c, entree.toStdString(), sortie.toStdString(), &e);
  if (!m_fonction) {
    m_formule->setText(QStringLiteral("Analyse symbolique impossible : %1").arg(q(e)));
    m_latex->clear();
    return false;
  }
  m_poles = m_fonction->poles(valeursSymboles(*c));
  m_formule->setText(QStringLiteral("H(s) = %1").arg(q(m_fonction->texte())));
  m_latex->setText(q(m_fonction->latex()));
  QStringList p;
  for (const auto& x : m_poles) p << QStringLiteral("%1%2%3j").arg(x.real(), 0, 'g', 6).arg(x.imag() < 0 ? "−" : "+").arg(std::abs(x.imag()), 0, 'g', 6);
  m_natif->setPlainText(QStringLiteral("Pôles (rad/s) : %1").arg(p.isEmpty() ? QStringLiteral("aucun") : p.join(QStringLiteral(", "))));
  return true;
}
QString FenetreLaboratoire::formule() const { return m_formule->text(); }
QString FenetreLaboratoire::latex() const { return m_latex->text(); }

QString FenetreLaboratoire::analyseNative(int genre) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return {};
  AnalyseNative a;
  static const AnalyseNative::Genre genres[] = {AnalyseNative::Genre::FonctionTransfert, AnalyseNative::Genre::PolesZeros, AnalyseNative::Genre::Bruit, AnalyseNative::Genre::Fourier};
  a.genre = genres[std::clamp(genre, 0, 3)];
  a.entree = m_entree->currentText().toStdString();
  a.sortie = m_sortie->currentText().toStdString();
  const ResultatNatif r = prom::ecad::analyseNative(*c, a);
  QString t;
  if (!r.ok) t = QStringLiteral("Échec : %1").arg(q(r.erreur));
  else
    switch (a.genre) {
      case AnalyseNative::Genre::FonctionTransfert:
        t = QStringLiteral("Gain %1, impédance d’entrée %2, de sortie %3").arg(r.gain, 0, 'g', 6).arg(q(ecrireSI(r.impedanceEntree, "Ω")), q(ecrireSI(r.impedanceSortie, "Ω")));
        break;
      case AnalyseNative::Genre::PolesZeros: {
        QStringList p, z;
        for (const auto& x : r.poles) p << QStringLiteral("%1%2%3j").arg(x.real(), 0, 'g', 6).arg(x.imag() < 0 ? "−" : "+").arg(std::abs(x.imag()), 0, 'g', 6);
        for (const auto& x : r.zeros) z << QStringLiteral("%1%2%3j").arg(x.real(), 0, 'g', 6).arg(x.imag() < 0 ? "−" : "+").arg(std::abs(x.imag()), 0, 'g', 6);
        t = QStringLiteral("Pôles de ngspice : %1\nZéros : %2").arg(p.join(QStringLiteral(", ")), z.isEmpty() ? QStringLiteral("aucun") : z.join(QStringLiteral(", ")));
        break;
      }
      case AnalyseNative::Genre::Bruit:
        t = QStringLiteral("Bruit total en sortie %1 efficaces, ramené à l’entrée %2").arg(q(ecrireSI(r.bruitTotalSortie, "V")), q(ecrireSI(r.bruitTotalEntree, "V")));
        break;
      case AnalyseNative::Genre::Fourier: t = QStringLiteral("Taux de distorsion harmonique %1 %").arg(r.distorsion, 0, 'g', 4); break;
    }
  m_natif->setPlainText(t);
  return t;
}

// ---------- Analyseur de réseau ----------
void FenetreLaboratoire::construireReseau(QWidget* page) {
  auto* col = new QVBoxLayout(page);
  auto* l = new QHBoxLayout;
  m_port1 = new QComboBox;
  m_port1->setObjectName(QStringLiteral("port1Reseau"));
  m_port2 = new QComboBox;
  m_port2->setObjectName(QStringLiteral("port2Reseau"));
  auto* lancer = new QPushButton(QStringLiteral("Analyser"));
  lancer->setObjectName(QStringLiteral("analyserReseau"));
  auto* exporter = new QPushButton(QStringLiteral("Touchstone…"));
  exporter->setObjectName(QStringLiteral("exporterTouchstone"));
  for (QWidget* w : std::initializer_list<QWidget*>{new QLabel(QStringLiteral("Port 1")), m_port1, new QLabel(QStringLiteral("Port 2")), m_port2, lancer, exporter}) l->addWidget(w);
  l->addStretch(1);
  col->addLayout(l);
  m_traceS = new TraceCourbes;
  m_traceS->setObjectName(QStringLiteral("traceParametresS"));
  col->addWidget(m_traceS, 1);
  connect(lancer, &QPushButton::clicked, this, [this] { analyserReseau(m_port1->currentText(), m_port2->currentText()); });
  connect(exporter, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Paramètres S"), QStringLiteral("quadripole.s2p"), QStringLiteral("Touchstone (*.s2p *.s1p)"));
    if (!f.isEmpty()) exporterTouchstone(f);
  });
}

bool FenetreLaboratoire::analyserReseau(const QString& p1, const QString& p2, double fDebut, double fFin) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  majReseaux();
  m_port1->setCurrentText(p1);
  m_port2->setCurrentText(p2);
  PortsAnalyseur p;
  p.port1 = p1.toStdString();
  p.port2 = p2.toStdString();
  std::string e;
  m_s = prom::ecad::parametresS(*c, p, fDebut, fFin, 20, &e);
  if (!m_s) {
    m_etat->setText(QStringLiteral("Analyse de réseau impossible : %1").arg(q(e)));
    return false;
  }
  TraceCourbes::Courbe s21, s11;
  s21.nom = QStringLiteral("S21 (dB)");
  s11.nom = QStringLiteral("S11 (dB)");
  s21.couleur = QColor(0x5B, 0x4B, 0xF5);
  s11.couleur = QColor(0xD9, 0x48, 0x2B);
  for (std::size_t i = 0; i < m_s->frequences.size(); ++i) {
    s21.x.push_back(m_s->frequences[i]);
    s21.y.push_back(enDecibels(m_s->s[i][2]));
    s11.x.push_back(m_s->frequences[i]);
    s11.y.push_back(std::max(-80.0, enDecibels(m_s->s[i][0])));
  }
  m_traceS->setCourbes({s21, s11}, QStringLiteral("f (Hz)"), QStringLiteral("dB"), true);
  m_etat->setText(QStringLiteral("S21 = %1 dB à %2").arg(enDecibels(m_s->s.front()[2]), 0, 'f', 2).arg(q(ecrireSI(m_s->frequences.front(), "Hz"))));
  return true;
}

bool FenetreLaboratoire::exporterTouchstone(const QString& chemin) {
  if (!m_s) return false;
  try {
    prom::ecrireFichierAtomique(chemin.toStdString(), ecrireTouchstone(*m_s, "DB", "HZ"));
  } catch (const std::exception& e) {
    m_etat->setText(q(e.what()));
    return false;
  }
  return true;
}

// ---------- Alimentation ----------
void FenetreLaboratoire::construireAlimentation(QWidget* page) {
  auto* col = new QVBoxLayout(page);
  auto* f = new QFormLayout;
  m_vin = champ("vinAbaisseur", 3, 60, 12, QStringLiteral(" V"), 2);
  m_vout = champ("voutAbaisseur", 0.9, 48, 5, QStringLiteral(" V"), 2);
  m_iout = champ("ioutAbaisseur", 0.05, 30, 2, QStringLiteral(" A"), 2);
  f->addRow(QStringLiteral("Entrée"), m_vin);
  f->addRow(QStringLiteral("Sortie"), m_vout);
  f->addRow(QStringLiteral("Courant"), m_iout);
  col->addLayout(f);
  auto* l = new QHBoxLayout;
  auto* concevoir = new QPushButton(QStringLiteral("Concevoir l’abaisseur"));
  concevoir->setObjectName(QStringLiteral("concevoirAbaisseur"));
  auto* poser = new QPushButton(QStringLiteral("Poser sur la feuille"));
  poser->setObjectName(QStringLiteral("poserAbaisseur"));
  auto* rails = new QPushButton(QStringLiteral("Arbre des rails"));
  rails->setObjectName(QStringLiteral("arbreRails"));
  l->addWidget(concevoir);
  l->addWidget(poser);
  l->addWidget(rails);
  l->addStretch(1);
  col->addLayout(l);
  m_rapportAlim = new QPlainTextEdit;
  m_rapportAlim->setObjectName(QStringLiteral("rapportAlimentation"));
  m_rapportAlim->setReadOnly(true);
  col->addWidget(m_rapportAlim, 1);
  connect(concevoir, &QPushButton::clicked, this, [this] { concevoirAbaisseur(m_vin->value(), m_vout->value(), m_iout->value()); });
  connect(poser, &QPushButton::clicked, this, [this] { poserAbaisseur(); });
  connect(rails, &QPushButton::clicked, this, [this] { m_rapportAlim->setPlainText(arbreRails({})); });
}

bool FenetreLaboratoire::concevoirAbaisseur(double vin, double vout, double i) {
  m_demande = {};
  m_demande.vEntree = vin;
  m_demande.vSortie = vout;
  m_demande.courant = i;
  m_vin->setValue(vin);
  m_vout->setValue(vout);
  m_iout->setValue(i);
  m_abaisseur = prom::ecad::concevoirAbaisseur(m_demande);
  QStringList t;
  for (const auto& n : m_abaisseur.notes) t << q(n);
  t << QString();
  for (const auto& [r, v] : m_abaisseur.nomenclature) t << QStringLiteral("%1\t%2").arg(q(r), q(v));
  m_marge = 0;
  if (m_abaisseur.ok && ngspiceDisponible()) {
    const MargesBoucle m = simulerBoucle(m_abaisseur, m_demande);
    if (m.ok) {
      m_marge = m.margePhase;
      t << QString() << QStringLiteral("Boucle simulée par ngspice : croisement à %1, marge de phase %2°, marge de gain %3 dB").arg(q(ecrireSI(m.frequenceCoupure, "Hz"))).arg(m.margePhase, 0, 'f', 1).arg(m.margeGain, 0, 'f', 1);
    } else
      t << QStringLiteral("Boucle non simulée : %1").arg(q(m.erreur));
  }
  m_rapportAlim->setPlainText(t.join(QLatin1Char('\n')));
  return m_abaisseur.ok;
}

bool FenetreLaboratoire::poserAbaisseur() {
  if (!m_abaisseur.ok) return false;
  std::vector<std::string> poses;
  const bool ok = m_atelier.modifierConception(
      [&](ConceptionElec& c) {
        // Sous ce qui est déjà posé sur la première feuille
        double y = 40;
        for (const auto& s : c.feuilles.empty() ? std::vector<SymbolePlace>{} : c.feuilles[0].symboles) y = std::max(y, static_cast<double>(s.position.y) / 1e6 + 30);
        poses = prom::ecad::poserAbaisseur(c, m_abaisseur, m_demande, 0, ptMm(40, std::min(y, 160.0)));
      },
      QStringLiteral("Abaisseur %1 V vers %2 V").arg(m_demande.vEntree).arg(m_demande.vSortie));
  if (ok) m_rapportAlim->appendPlainText(QStringLiteral("\nPosé : %1 composant(s), un Ctrl+Z les retire.").arg(poses.size()));
  return ok && !poses.empty();
}

QString FenetreLaboratoire::arbreRails(const std::map<std::string, double>& charges) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return {};
  const ArbreRails a = prom::ecad::arbreRails(*c, charges);
  return a.etages.empty() ? QStringLiteral("Aucune source ni aucun régulateur sur le schéma.")
                          : q(a.texte()) + QStringLiteral("\nPuissance prise aux sources : %1, dissipée : %2").arg(q(ecrireSI(a.puissanceEntree, "W")), q(ecrireSI(a.dissipationTotale, "W")));
}

// ---------- Mesures ----------
void FenetreLaboratoire::construireMesures(QWidget* page) {
  auto* col = new QVBoxLayout(page);
  auto* l = new QHBoxLayout;
  auto* importer = new QPushButton(QStringLiteral("Importer une mesure…"));
  importer->setObjectName(QStringLiteral("importerMesure"));
  l->addWidget(importer);
  l->addStretch(1);
  col->addLayout(l);
  m_traceMesures = new TraceCourbes;
  m_traceMesures->setObjectName(QStringLiteral("traceMesures"));
  col->addWidget(m_traceMesures, 1);
  m_rapportMesures = new QPlainTextEdit;
  m_rapportMesures->setObjectName(QStringLiteral("rapportMesures"));
  m_rapportMesures->setReadOnly(true);
  m_rapportMesures->setMaximumHeight(130);
  m_rapportMesures->setPlainText(moteurs());
  col->addWidget(m_rapportMesures);
  connect(importer, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Mesure"), {}, QStringLiteral("Mesures (*.csv *.sr *.txt)"));
    if (!f.isEmpty()) importerMesure(f);
  });
}

QString FenetreLaboratoire::moteurs() const {
  QStringList t{QStringLiteral("Moteurs de simulation du poste :"), QStringLiteral("  ngspice : %1").arg(ngspiceDisponible() ? q(versionNgspice()) : QStringLiteral("absent"))};
  for (const auto& m : moteursTiers())
    t << QStringLiteral("  %1 : %2%3").arg(q(m.nom), m.present ? QStringLiteral("présent") : QStringLiteral("absent"), m.resultatsRelus ? QString() : QStringLiteral(" (résultats non relus)"));
  return t.join(QLatin1Char('\n'));
}

bool FenetreLaboratoire::importerMesure(const QString& chemin) {
  std::string contenu, e;
  try {
    contenu = prom::lireFichier(chemin.toStdString());
  } catch (const std::exception& x) {
    m_etat->setText(q(x.what()));
    return false;
  }
  std::vector<CourbeMesuree> lues;
  if (chemin.endsWith(QLatin1String(".sr"), Qt::CaseInsensitive)) {
    const auto s = lireSigrok(contenu, &e);
    if (!s) {
      m_etat->setText(q(e));
      return false;
    }
    for (const auto& [nom, v] : s->voies) {
      CourbeMesuree c{nom, "logique", {}};
      for (const auto& f : v) c.points.push_back({f.t, f.niveau == Niveau::Haut ? 1.0 : 0.0});
      lues.push_back(c);
    }
  } else {
    const auto m = lireCsvOscilloscope(contenu, &e);
    if (!m) {
      m_etat->setText(q(e));
      return false;
    }
    lues = *m;
  }
  m_mesures.insert(m_mesures.end(), lues.begin(), lues.end());
  std::vector<TraceCourbes::Courbe> cb;
  for (const auto& m : m_mesures) {
    TraceCourbes::Courbe x;
    x.nom = q(m.nom);
    x.couleur = QColor(0x1E, 0x88, 0x5A);
    for (const auto& p : m.points) x.x.push_back(p.t), x.y.push_back(p.v);
    cb.push_back(x);
  }
  m_traceMesures->setCourbes(std::move(cb), QStringLiteral("t (s)"), QString(), false);
  m_rapportMesures->setPlainText(QStringLiteral("%1 courbe(s) mesurée(s) ; %2\n%3").arg(m_mesures.size()).arg(QFileInfo(chemin).fileName(), moteurs()));
  return true;
}

double FenetreLaboratoire::ecartMesureSimulation(const QString& reseau) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c || m_mesures.empty() || m_mesures.front().points.size() < 2) return -1;
  const Releve& m = m_mesures.front().points;
  AnalyseSpice a;
  a.type = AnalyseSpice::Type::Transitoire;
  a.duree = m.back().t;
  a.pas = std::max(1e-9, (m.back().t - m.front().t) / 2000);
  OptionsSimulation o;
  o.pasMax = a.pas;
  const NetlistSpice n = netlistSpice(*c, a, o);
  const ResultatSimulation r = simuler(n);
  const auto* t = r.reel("time");
  const auto* v = r.reel(vecteurTension(n, reseau.toStdString()));
  if (!t || !v) return -1;
  Releve s;
  for (std::size_t i = 0; i < t->size(); ++i) s.push_back({(*t)[i], (*v)[i]});
  std::vector<TraceCourbes::Courbe> cb = m_traceMesures->courbes();
  TraceCourbes::Courbe x;
  x.nom = QStringLiteral("simulation ") + reseau;
  x.couleur = QColor(0x5B, 0x4B, 0xF5);
  for (const auto& p : s) x.x.push_back(p.t), x.y.push_back(p.v);
  cb.push_back(x);
  m_traceMesures->setCourbes(std::move(cb), QStringLiteral("t (s)"), QString(), false);
  const EcartCourbes e = comparerCourbes(s, m, m.front().t + 0.2 * (m.back().t - m.front().t), m.back().t);
  return e.valide ? e.maximal : -1;
}

// ======================================================================================================================
// Atelier
// ======================================================================================================================
FenetreLaboratoire* AtelierElectronique::laboratoire(const QString& onglet) {
  if (!m_laboratoire) m_laboratoire = new FenetreLaboratoire(*this, this);
  m_laboratoire->montrer(onglet);
  return m_laboratoire;
}

void AtelierElectronique::enregistrerCommandesL14() {
  auto& c = m_commandes;
  c["simulation.laboratoire"] = [this] { laboratoire(QStringLiteral("continu")); };
  c["simulation.logique"] = [this] { laboratoire(QStringLiteral("logique")); };
  c["simulation.symbolique"] = [this] { laboratoire(QStringLiteral("symbolique")); };
  c["simulation.reseau"] = [this] { laboratoire(QStringLiteral("reseau")); };
  c["simulation.alimentation"] = [this] { laboratoire(QStringLiteral("alimentation")); };
  c["simulation.mesures"] = [this] { laboratoire(QStringLiteral("mesures")); };
}

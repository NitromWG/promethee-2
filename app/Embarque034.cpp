// Prométhée : microcontrôleurs, FPGA et logique dans l'atelier électronique (lot L15 de la 0.34). Voir
// Embarque034.hpp. Licence GPL-3.0-only.
#include "Embarque034.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "Laboratoire034.hpp"
#include "VueSchema.hpp"
#include "ZoneMessages.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/processus.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QPushButton* bouton(const char* nom, const QString& texte) {
  auto* b = new QPushButton(texte);
  b->setObjectName(QLatin1String(nom));
  return b;
}
QString dossierTemporaire(const QString& nom) {
  std::random_device r;
  const QString d = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-%1-%2").arg(nom).arg(r()));
  QDir().mkpath(d);
  return d;
}
}  // namespace

// ======================================================================================================================
// Vue du robot
// ======================================================================================================================
VueRobot::VueRobot(QWidget* parent) : QWidget(parent) { setMinimumSize(320, 180); }

void VueRobot::setPiste(const PisteRobot& p) {
  m_image = QImage(p.largeur, p.hauteur, QImage::Format_Grayscale8);
  for (int j = 0; j < p.hauteur; ++j) std::copy(p.gris.begin() + static_cast<std::ptrdiff_t>(j) * p.largeur, p.gris.begin() + static_cast<std::ptrdiff_t>(j + 1) * p.largeur, m_image.scanLine(j));
  update();
}

void VueRobot::setTrajet(std::vector<std::pair<double, double>> t) {
  m_trajet = std::move(t);
  update();
}

void VueRobot::paintEvent(QPaintEvent*) {
  QPainter g(this);
  g.fillRect(rect(), Qt::white);
  if (m_image.isNull()) return;
  const double k = std::min(width() / static_cast<double>(m_image.width()), height() / static_cast<double>(m_image.height()));
  g.translate((width() - m_image.width() * k) / 2, (height() - m_image.height() * k) / 2);
  g.scale(k, k);
  g.drawImage(0, 0, m_image);
  g.setRenderHint(QPainter::Antialiasing);
  g.setPen(QPen(QColor(0xE8, 0x5D, 0x2A), 4 / k));
  for (std::size_t i = 1; i < m_trajet.size(); ++i) g.drawLine(QPointF(m_trajet[i - 1].first, m_trajet[i - 1].second), QPointF(m_trajet[i].first, m_trajet[i].second));
  if (!m_trajet.empty()) {
    g.setBrush(QColor(0x2A, 0x7F, 0xE8));
    g.drawEllipse(QPointF(m_trajet.back().first, m_trajet.back().second), 30, 30);
  }
}

// ======================================================================================================================
// Fenêtre
// ======================================================================================================================
FenetreEmbarque::FenetreEmbarque(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreEmbarque"));
  setWindowTitle(QStringLiteral("Programme, logique et machines à états"));
  resize(1180, 780);
  auto* col = new QVBoxLayout(this);
  m_onglets = new QTabWidget;
  m_onglets->setObjectName(QStringLiteral("ongletsEmbarque"));
  col->addWidget(m_onglets);
  m_etat = new QLabel;
  m_etat->setObjectName(QStringLiteral("etatEmbarque"));
  m_etat->setWordWrap(true);
  col->addWidget(m_etat);
  auto page = [&](const char* id, const QString& titre) {
    auto* w = new QWidget;
    w->setObjectName(QLatin1String(id));
    m_onglets->addTab(w, titre);
    return w;
  };
  construireProgramme(page("programme", QStringLiteral("Programme")));
  construireCosimulation(page("cosimulation", QStringLiteral("Co-simulation et mise au point")));
  construireOrganigramme(page("organigramme", QStringLiteral("Modules et organigramme")));
  construireHdl(page("hdl", QStringLiteral("Composant HDL")));
  construireMachine(page("machine", QStringLiteral("Machine à états")));
  construireRobot(page("robot", QStringLiteral("Robot")));
  construireLogique(page("logique", QStringLiteral("Logique programmable")));
  m_minuterie = new QTimer(this);
  m_minuterie->setInterval(100);
  connect(m_minuterie, &QTimer::timeout, this, &FenetreEmbarque::rafraichir);
}

FenetreEmbarque::~FenetreEmbarque() = default;

bool FenetreEmbarque::montrer(const QString& onglet) {
  // Microcontrôleurs de la conception ouverte
  if (const ConceptionElec* c = m_atelier.conceptionCourante()) {
    const QString avant = m_cible->currentText();
    m_cible->clear();
    for (const auto& x : c->composants)
      if (!microDuSymbole(x.symbole).empty() || x.symbole.find("ATmega") != std::string::npos) m_cible->addItem(q(x.reference));
    if (!avant.isEmpty()) m_cible->setCurrentText(avant);
  }
  show();
  raise();
  if (onglet.isEmpty()) return true;
  for (int i = 0; i < m_onglets->count(); ++i)
    if (m_onglets->widget(i)->objectName() == onglet) {
      m_onglets->setCurrentIndex(i);
      return true;
    }
  return false;
}

QString FenetreEmbarque::langageCourant() const { return m_langage->currentText(); }
QString FenetreEmbarque::cibleCourante() const { return m_cible->currentText().isEmpty() ? QStringLiteral("U1") : m_cible->currentText(); }
double FenetreEmbarque::frequence() const { return m_frequence->currentIndex() == 0 ? 16e6 : 8e6; }
void FenetreEmbarque::setFrequence(double hz) { m_frequence->setCurrentIndex(hz > 12e6 ? 0 : 1); }

QString FenetreEmbarque::dossierProgrammes() const {
  const QString chemin = m_atelier.document() ? m_atelier.document()->chemin() : QString();
  static const QString temporaire = dossierTemporaire(QStringLiteral("programmes"));
  const QString d = chemin.isEmpty() ? temporaire : QFileInfo(chemin).absoluteDir().filePath(QStringLiteral("programmes"));
  QDir().mkpath(d);
  return d;
}

std::string FenetreEmbarque::nomFichierSource() const {
  const QString l = langageCourant();
  return l == QLatin1String("arduino") ? "croquis.ino" : l == QLatin1String("micropython") ? "main.py" : "programme.c";
}

// ---------- Programme ----------
void FenetreEmbarque::construireProgramme(QWidget* page) {
  auto* col = new QVBoxLayout(page);
  auto* l = new QHBoxLayout;
  m_nom = new QLineEdit(QStringLiteral("programme"));
  m_nom->setObjectName(QStringLiteral("nomProgramme"));
  m_langage = new QComboBox;
  m_langage->setObjectName(QStringLiteral("langageProgramme"));
  m_langage->addItems({QStringLiteral("c"), QStringLiteral("arduino"), QStringLiteral("micropython")});
  m_cible = new QComboBox;
  m_cible->setObjectName(QStringLiteral("cibleProgramme"));
  m_frequence = new QComboBox;
  m_frequence->setObjectName(QStringLiteral("frequenceProgramme"));
  m_frequence->addItems({QStringLiteral("16 MHz"), QStringLiteral("8 MHz")});
  for (QWidget* w : std::initializer_list<QWidget*>{new QLabel(QStringLiteral("Nom")), m_nom, new QLabel(QStringLiteral("Langage")), m_langage, new QLabel(QStringLiteral("Microcontrôleur")), m_cible, m_frequence})
    l->addWidget(w);
  auto* nouveau = bouton("nouveauProgramme", QStringLiteral("Nouveau"));
  auto* ouvrir = bouton("ouvrirProgramme", QStringLiteral("Ouvrir…"));
  auto* enreg = bouton("enregistrerProgramme", QStringLiteral("Enregistrer"));
  auto* comp = bouton("compilerProgramme", QStringLiteral("Compiler"));
  auto* ecarts = bouton("ecartsProgramme", QStringLiteral("Comparer au schéma"));
  auto* init = bouton("initialisationProgramme", QStringLiteral("Initialisation"));
  auto* telev = bouton("televerserProgramme", QStringLiteral("Téléverser…"));
  for (QWidget* w : std::initializer_list<QWidget*>{nouveau, ouvrir, enreg, comp, ecarts, init, telev}) l->addWidget(w);
  l->addStretch(1);
  col->addLayout(l);
  auto* coupe = new QSplitter(Qt::Vertical);
  m_editeur = new QPlainTextEdit;
  m_editeur->setObjectName(QStringLiteral("editeurProgramme"));
  m_editeur->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  m_editeur->setLineWrapMode(QPlainTextEdit::NoWrap);
  coupe->addWidget(m_editeur);
  auto* bas = new QWidget;
  auto* lb = new QHBoxLayout(bas);
  m_erreurs = new QListWidget;
  m_erreurs->setObjectName(QStringLiteral("erreursProgramme"));
  m_listeEcarts = new QListWidget;
  m_listeEcarts->setObjectName(QStringLiteral("ecartsSchema"));
  lb->addWidget(m_erreurs);
  lb->addWidget(m_listeEcarts);
  coupe->addWidget(bas);
  coupe->setStretchFactor(0, 3);
  col->addWidget(coupe, 1);
  connect(nouveau, &QPushButton::clicked, this, [this] { nouveauProgramme(m_nom->text(), langageCourant(), cibleCourante()); });
  connect(ouvrir, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Programme"), dossierProgrammes(), QStringLiteral("Programmes (*.c *.ino *.py)"));
    if (f.isEmpty()) return;
    try {
      m_editeur->setPlainText(q(prom::lireFichier(f.toStdString())));
      m_langage->setCurrentText(q(langageDuFichier(f.toStdString())));
      m_nom->setText(QFileInfo(f).completeBaseName());
    } catch (const std::exception& e) {
      m_etat->setText(q(e.what()));
    }
  });
  connect(enreg, &QPushButton::clicked, this, [this] {
    QString e;
    if (!enregistrer(&e)) m_etat->setText(e);
  });
  connect(comp, &QPushButton::clicked, this, &FenetreEmbarque::compiler);
  connect(ecarts, &QPushButton::clicked, this, [this] { this->ecarts(); });
  connect(init, &QPushButton::clicked, this, &FenetreEmbarque::insererInitialisation);
  connect(telev, &QPushButton::clicked, this, [this] {
    ProgrammeEmbarque p;
    p.langage = langageCourant().toStdString();
    p.carte = "arduino:avr:uno";
    const auto cmd = commandeTeleversement(p, p.langage == "micropython" ? (dossierProgrammes() + "/main.py").toStdString() : m_compilation.hex, "/dev/ttyACM0");
    QStringList t;
    for (const auto& x : cmd) t << q(x);
    m_etat->setText(cmd.empty() ? QStringLiteral("Aucun outil de téléversement sur ce poste (avrdude, arduino-cli ou mpremote).")
                                : QStringLiteral("Téléversement par : %1 (la carte doit être branchée)").arg(t.join(QLatin1Char(' '))));
  });
  connect(m_erreurs, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
    const int ligne = it->data(Qt::UserRole).toInt();
    if (ligne <= 0) return;
    QTextCursor c(m_editeur->document()->findBlockByNumber(ligne - 1));
    m_editeur->setTextCursor(c);
    m_editeur->setFocus();
  });
  connect(m_listeEcarts, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { montrerEcart(m_listeEcarts->row(it)); });
}

void FenetreEmbarque::nouveauProgramme(const QString& nom, const QString& langage, const QString& cible) {
  m_nom->setText(nom);
  m_langage->setCurrentText(langage);
  if (!cible.isEmpty()) m_cible->setCurrentText(cible);
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const std::string init = c ? initialisationPeripheriques(*c, cible.toStdString(), langage.toStdString()) : std::string();
  if (langage == QLatin1String("arduino")) m_editeur->setPlainText(QStringLiteral("void setup() {\n%1}\n\nvoid loop() {\n}\n").arg(q(init)));
  else if (langage == QLatin1String("micropython")) m_editeur->setPlainText(QStringLiteral("from machine import Pin\nimport time\n\nwhile True:\n    time.sleep_ms(500)\n"));
  else m_editeur->setPlainText(QStringLiteral("#include <avr/io.h>\n#include <util/delay.h>\n\nint main(void) {\n%1  for (;;) {\n  }\n}\n").arg(q(init)));
}

bool FenetreEmbarque::enregistrer(QString* erreur) {
  const QString ext = langageCourant() == QLatin1String("arduino") ? QStringLiteral(".ino") : langageCourant() == QLatin1String("micropython") ? QStringLiteral(".py") : QStringLiteral(".c");
  const QString fichier = QDir(dossierProgrammes()).filePath(m_nom->text() + ext);
  const std::string contenu = m_editeur->toPlainText().toStdString();
  try {
    prom::ecrireFichierAtomique(fichier.toStdString(), contenu);
  } catch (const std::exception& e) {
    if (erreur) *erreur = q(e.what());
    return false;
  }
  ProgrammeEmbarque p;
  p.nom = m_nom->text().toStdString();
  p.langage = langageCourant().toStdString();
  p.chemin = "programmes/" + (m_nom->text() + ext).toStdString();
  p.cible = cibleCourante().toStdString();
  p.frequence = frequence();
  const bool ok = m_atelier.modifierConception([&](ConceptionElec& c) { ajouterProgramme(c, p, contenu); }, QStringLiteral("Programme %1").arg(m_nom->text()));
  m_etat->setText(QStringLiteral("Enregistré dans %1, cité par le projet avec son empreinte SHA-256.").arg(fichier));
  return ok;
}

bool FenetreEmbarque::compiler() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  std::map<std::string, std::string> pilotes;
  if (c)
    for (const ModuleEmbarque* m : modulesDeLaConception(*c))
      for (const auto& [nom, texte] : m->pilote)
        if (nom.size() < 3 || nom.compare(nom.size() - 3, 3, ".py") != 0) pilotes[nom] = texte;
  m_compilation = compilerProgramme(m_editeur->toPlainText().toStdString(), langageCourant().toStdString(), QDir(dossierProgrammes()).filePath(QStringLiteral("construction-") + m_nom->text()).toStdString(),
                                    frequence(), pilotes);
  m_erreurs->clear();
  for (const auto& e : m_compilation.erreurs) {
    auto* it = new QListWidgetItem(QStringLiteral("%1 %2 : %3").arg(e.avertissement ? QStringLiteral("⚠") : QStringLiteral("✖")).arg(e.ligne).arg(q(e.message)));
    it->setData(Qt::UserRole, e.ligne);
    m_erreurs->addItem(it);
  }
  m_etat->setText(m_compilation.reussi ? QStringLiteral("Compilé par %1 : %2 octets de programme.").arg(q(m_compilation.chaine)).arg(m_compilation.tailleProgramme)
                                       : QStringLiteral("La compilation a échoué (%1 erreur(s)).").arg(m_compilation.erreurs.size()));
  return m_compilation.reussi;
}

const std::vector<EcartProgramme>& FenetreEmbarque::ecarts() {
  m_ecarts.clear();
  m_listeEcarts->clear();
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return m_ecarts;
  ProgrammeEmbarque p;
  p.langage = langageCourant().toStdString();
  p.cible = cibleCourante().toStdString();
  m_ecarts = comparerProgramme(*c, p, m_editeur->toPlainText().toStdString());
  for (const auto& e : m_ecarts) m_listeEcarts->addItem(q(e.message));
  if (m_ecarts.empty()) m_listeEcarts->addItem(QStringLiteral("Le programme et le schéma s’accordent."));
  return m_ecarts;
}

bool FenetreEmbarque::montrerEcart(int i) {
  if (i < 0 || i >= static_cast<int>(m_ecarts.size())) return false;
  const EcartProgramme& e = m_ecarts[static_cast<std::size_t>(i)];
  m_atelier.montrerBroche(e.composant, e.numero);
  if (e.ligne > 0) {
    QTextCursor c(m_editeur->document()->findBlockByNumber(e.ligne - 1));
    m_editeur->setTextCursor(c);
  }
  return true;
}

void FenetreEmbarque::insererInitialisation() {
  if (const ConceptionElec* c = m_atelier.conceptionCourante()) m_editeur->insertPlainText(q(initialisationPeripheriques(*c, cibleCourante().toStdString(), langageCourant().toStdString())));
}

// ---------- Co-simulation ----------
void FenetreEmbarque::construireCosimulation(QWidget* page) {
  auto* col = new QVBoxLayout(page);
  auto* l = new QHBoxLayout;
  auto* dem = bouton("demarrerCosimulation", QStringLiteral("▶ Co-simuler avec le circuit"));
  auto* arr = bouton("arreterCosimulation", QStringLiteral("■ Arrêter"));
  auto* cont = bouton("continuerDebogueur", QStringLiteral("Continuer"));
  auto* pl = bouton("pasLigne", QStringLiteral("Pas (ligne)"));
  auto* pi = bouton("pasInstruction", QStringLiteral("Pas (instruction)"));
  auto* point = bouton("pointArretLigne", QStringLiteral("Point d’arrêt sur la ligne"));
  for (QWidget* w : std::initializer_list<QWidget*>{dem, arr, cont, pl, pi, point}) l->addWidget(w);
  l->addStretch(1);
  col->addLayout(l);
  m_etatDebogueur = new QLabel(QStringLiteral("—"));
  m_etatDebogueur->setObjectName(QStringLiteral("etatDebogueur"));
  col->addWidget(m_etatDebogueur);
  auto* coupe = new QSplitter(Qt::Horizontal);
  auto* gauche = new QWidget;
  auto* lg = new QVBoxLayout(gauche);
  auto* lt = new QHBoxLayout;
  m_debit = new QComboBox;
  m_debit->setObjectName(QStringLiteral("debitTerminal"));
  for (int d : {300, 1200, 2400, 4800, 9600, 19200, 38400, 57600}) m_debit->addItem(QString::number(d), d);
  m_debit->setCurrentText(QStringLiteral("9600"));
  m_envoi = new QLineEdit;
  m_envoi->setObjectName(QStringLiteral("envoiTerminal"));
  auto* envoyer = bouton("envoyerTerminal", QStringLiteral("Envoyer"));
  for (QWidget* w : std::initializer_list<QWidget*>{new QLabel(QStringLiteral("Terminal (bauds)")), m_debit, m_envoi, envoyer}) lt->addWidget(w);
  lg->addLayout(lt);
  m_terminal = new QPlainTextEdit;
  m_terminal->setObjectName(QStringLiteral("terminalSerie"));
  m_terminal->setReadOnly(true);
  m_terminal->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  lg->addWidget(m_terminal, 1);
  m_traces = new QPlainTextEdit;
  m_traces->setObjectName(QStringLiteral("tracesMicro"));
  m_traces->setReadOnly(true);
  lg->addWidget(m_traces, 1);
  coupe->addWidget(gauche);
  m_registres = new QTableWidget(35, 2);
  m_registres->setObjectName(QStringLiteral("registresMicro"));
  m_registres->setHorizontalHeaderLabels({QStringLiteral("Registre"), QStringLiteral("Valeur")});
  m_registres->verticalHeader()->hide();
  coupe->addWidget(m_registres);
  col->addWidget(coupe, 1);
  connect(dem, &QPushButton::clicked, this, [this] {
    QString e;
    if (!demarrerCosimulation(&e)) m_etat->setText(e);
  });
  connect(arr, &QPushButton::clicked, this, &FenetreEmbarque::arreterCosimulation);
  connect(cont, &QPushButton::clicked, this, &FenetreEmbarque::continuer);
  connect(pl, &QPushButton::clicked, this, &FenetreEmbarque::pasLigne);
  connect(pi, &QPushButton::clicked, this, &FenetreEmbarque::pasInstruction);
  connect(point, &QPushButton::clicked, this, [this] { basculerPoint(m_editeur->textCursor().blockNumber() + 1); });
  connect(m_debit, &QComboBox::currentIndexChanged, this, [this] { reglerTerminal(m_debit->currentData().toInt()); });
  connect(envoyer, &QPushButton::clicked, this, [this] {
    envoyerTerminal(m_envoi->text() + QStringLiteral("\r\n"));
    m_envoi->clear();
  });
}

bool FenetreEmbarque::demarrerCosimulation(QString* erreur) {
  auto refuser = [&](const QString& e) {
    m_etat->setText(e);
    if (erreur) *erreur = e;
    return false;
  };
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return refuser(QStringLiteral("Aucune conception ouverte."));
  if (!m_compilation.reussi && !compiler()) return refuser(QStringLiteral("Le programme ne compile pas."));
  const std::string ref = cibleCourante().toStdString();
  const ComposantElec* x = c->parReference(ref);
  if (!x) return refuser(QStringLiteral("Microcontrôleur %1 absent du schéma.").arg(cibleCourante()));
  std::string e;
  std::unique_ptr<EmulateurMicro> emu = creerEmulateur(microDuSymbole(x->symbole), frequence(), &e);
  if (!emu) return refuser(q(e));
  if (!emu->charger(m_compilation.elf, &e)) return refuser(q(e));
  FenetreLaboratoire* lab = m_atelier.laboratoire(QStringLiteral("continu"));
  lab->arreter();
  OptionsContinue oc;
  oc.pas = 1e-4;
  OptionsSimulation os;
  os.cosimulation[ref] = "micro";
  const NetlistSpice n0 = netlistContinue(*c, oc, os);
  if (!n0.valide()) return refuser(q(n0.erreurs.front()));
  m_micro = std::make_shared<CosimulationMicro>(*c, ref, n0, std::move(emu));
  m_micro->terminal.debit = m_debit->currentData().toInt();
  m_micro->debogueur().setTable(tableLignes(m_compilation.elf));
  for (int l : m_points) m_micro->debogueur().ajouterPoint(nomFichierSource(), l);
  lab->setPas(1e-4);
  lab->ajouterParticipant(m_micro, ref, "micro");
  QString el;
  if (!lab->demarrer(&el)) return refuser(el);
  m_minuterie->start();
  m_etat->setText(QStringLiteral("Le programme tourne sur %1, pas à pas avec le circuit.").arg(q(m_micro->emulateur().nom())));
  return true;
}

void FenetreEmbarque::arreterCosimulation() {
  if (FenetreLaboratoire* lab = m_atelier.laboratoire()) lab->arreter();
  m_minuterie->stop();
  rafraichir();
}

bool FenetreEmbarque::basculerPoint(int ligne) {
  if (ligne <= 0) return false;
  const bool mis = !m_points.count(ligne);
  if (mis) m_points.insert(ligne);
  else m_points.erase(ligne);
  // Pendant la co-simulation, seulement à l'arrêt (le fil du circuit lit les points d'arrêt)
  if (m_micro && m_micro->arrete()) {
    m_micro->debogueur().retirerPoints();
    for (int l : m_points) m_micro->debogueur().ajouterPoint(nomFichierSource(), l);
  }
  m_etat->setText(mis ? QStringLiteral("Point d’arrêt ligne %1.").arg(ligne) : QStringLiteral("Point d’arrêt de la ligne %1 retiré.").arg(ligne));
  return mis;
}

void FenetreEmbarque::continuer() {
  if (m_micro) m_micro->continuer();
}
void FenetreEmbarque::pasLigne() {
  if (m_micro) m_micro->pasLigne();
}
void FenetreEmbarque::pasInstruction() {
  if (m_micro) m_micro->pasInstruction();
}
QString FenetreEmbarque::terminal() const { return m_micro ? q(m_micro->texteRecu()) : QString(); }
void FenetreEmbarque::reglerTerminal(int debit) {
  m_debit->setCurrentText(QString::number(debit));
  if (m_micro) m_micro->terminal.debit = debit;
}
void FenetreEmbarque::envoyerTerminal(const QString& texte) {
  if (m_micro) m_micro->envoyer(texte.toStdString());
}

void FenetreEmbarque::rafraichir() {
  if (!m_micro) return;
  const QString t = terminal();
  if (m_terminal->toPlainText() != t) m_terminal->setPlainText(t);
  const bool arrete = m_micro->arrete();
  const auto l = arrete ? m_micro->debogueur().ligneCourante() : std::nullopt;
  m_etatDebogueur->setText(arrete ? QStringLiteral("Arrêté : %1").arg(q(m_micro->raisonArret())) : QStringLiteral("En cours, t = %1 s").arg(m_micro->emulateur().temps(), 0, 'f', 3));
  if (arrete) {
    const auto r = m_micro->emulateur().registres();
    for (int i = 0; i < 32 && i < static_cast<int>(r.size()); ++i) {
      m_registres->setItem(i, 0, new QTableWidgetItem(QStringLiteral("r%1").arg(i)));
      m_registres->setItem(i, 1, new QTableWidgetItem(QStringLiteral("0x%1").arg(r[static_cast<std::size_t>(i)], 2, 16, QLatin1Char('0'))));
    }
    m_registres->setItem(32, 0, new QTableWidgetItem(QStringLiteral("SREG")));
    m_registres->setItem(32, 1, new QTableWidgetItem(QStringLiteral("0x%1").arg(m_micro->emulateur().sreg(), 2, 16, QLatin1Char('0'))));
    m_registres->setItem(33, 0, new QTableWidgetItem(QStringLiteral("SP")));
    m_registres->setItem(33, 1, new QTableWidgetItem(QStringLiteral("0x%1").arg(m_micro->emulateur().sp(), 4, 16, QLatin1Char('0'))));
    m_registres->setItem(34, 0, new QTableWidgetItem(QStringLiteral("PC")));
    m_registres->setItem(34, 1, new QTableWidgetItem(QStringLiteral("0x%1").arg(m_micro->emulateur().pc(), 4, 16, QLatin1Char('0'))));
    if (l) {
      QTextCursor c(m_editeur->document()->findBlockByNumber(l->ligne - 1));
      m_editeur->setTextCursor(c);
    }
  }
  const auto tr = m_micro->traces();
  QStringList lignes;
  for (std::size_t i = tr.size() > 200 ? tr.size() - 200 : 0; i < tr.size(); ++i) lignes << QStringLiteral("%1 s  %2").arg(tr[i].t, 0, 'f', 6).arg(q(tr[i].texte));
  m_traces->setPlainText(lignes.join(QLatin1Char('\n')));
}

// ---------- Modules et organigramme ----------
void FenetreEmbarque::construireOrganigramme(QWidget* page) {
  auto* col = new QVBoxLayout(page);
  auto* l = new QHBoxLayout;
  m_module = new QComboBox;
  m_module->setObjectName(QStringLiteral("moduleGalerie"));
  for (const auto& m : galerieEmbarquee()) m_module->addItem(q(m.nom), q(m.id));
  auto* poser = bouton("poserModule", QStringLiteral("Poser le module"));
  auto* exemple = bouton("exempleOrganigramme", QStringLiteral("Exemple"));
  m_ajoutBloc = new QLineEdit;
  m_ajoutBloc->setObjectName(QStringLiteral("ajoutBloc"));
  m_ajoutBloc->setPlaceholderText(QStringLiteral("Bloc à ajouter (complétion)"));
  auto* ajouter = bouton("ajouterBloc", QStringLiteral("Ajouter"));
  auto* versC = bouton("versArduino", QStringLiteral("Croquis Arduino"));
  auto* versPy = bouton("versPython", QStringLiteral("MicroPython"));
  auto* comp = bouton("compilerOrganigramme", QStringLiteral("Compiler"));
  for (QWidget* w : std::initializer_list<QWidget*>{m_module, poser, exemple, m_ajoutBloc, ajouter, versC, versPy, comp}) l->addWidget(w);
  col->addLayout(l);
  auto* coupe = new QSplitter(Qt::Horizontal);
  m_blocs = new QListWidget;
  m_blocs->setObjectName(QStringLiteral("blocsOrganigramme"));
  coupe->addWidget(m_blocs);
  m_codeOrganigramme = new QPlainTextEdit;
  m_codeOrganigramme->setObjectName(QStringLiteral("codeOrganigramme"));
  m_codeOrganigramme->setReadOnly(true);
  m_codeOrganigramme->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  coupe->addWidget(m_codeOrganigramme);
  coupe->setStretchFactor(1, 2);
  col->addWidget(coupe, 1);
  connect(poser, &QPushButton::clicked, this, [this] { poserModule(m_module->currentData().toString()); });
  connect(exemple, &QPushButton::clicked, this, &FenetreEmbarque::exempleOrganigramme);
  connect(ajouter, &QPushButton::clicked, this, [this] { ajouterBloc(m_ajoutBloc->text()); });
  connect(m_ajoutBloc, &QLineEdit::textEdited, this, [this](const QString& t) {
    auto* comp = new QCompleter(propositions(t), m_ajoutBloc);
    comp->setCaseSensitivity(Qt::CaseInsensitive);
    comp->setFilterMode(Qt::MatchContains);
    m_ajoutBloc->setCompleter(comp);
  });
  connect(versC, &QPushButton::clicked, this, [this] { convertir(false); });
  connect(versPy, &QPushButton::clicked, this, [this] { convertir(true); });
  connect(comp, &QPushButton::clicked, this, &FenetreEmbarque::compilerOrganigramme);
  // Un clic sur un bloc : ses lignes dans le code
  connect(m_blocs, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
    const CodeOrganigramme c = versArduino(m_organigramme, m_atelier.conceptionCourante() ? *m_atelier.conceptionCourante() : ConceptionElec{});
    const auto l = c.lignes.find(it->data(Qt::UserRole).toString().toStdString());
    if (l == c.lignes.end()) return;
    m_codeOrganigramme->setPlainText(q(c.code));
    QTextCursor cur(m_codeOrganigramme->document()->findBlockByNumber(l->second.first - 1));
    cur.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, l->second.second - l->second.first + 1);
    m_codeOrganigramme->setTextCursor(cur);
  });
}

bool FenetreEmbarque::poserModule(const QString& id) {
  std::vector<std::string> poses;
  std::string e;
  const bool ok = m_atelier.modifierConception([&](ConceptionElec& c) { poses = poserModuleEmbarque(c, id.toStdString(), cibleCourante().toStdString(), &e); }, QStringLiteral("Module %1").arg(id));
  m_etat->setText(poses.empty() ? QStringLiteral("Module non posé : %1").arg(q(e)) : QStringLiteral("Module posé et relié : %1 composant(s).").arg(poses.size()));
  return ok && !poses.empty();
}

void FenetreEmbarque::exempleOrganigramme() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const auto modules = c ? modulesDeLaConception(*c) : std::vector<const ModuleEmbarque*>{};
  m_organigramme = organigrammeExemple(modules.empty() ? "bme280" : modules.front()->id, "13");
  m_atelier.modifierConception([&](ConceptionElec& x) { ecrireOrganigrammes(x, {m_organigramme}); }, QStringLiteral("Organigramme"));
  majOrganigramme();
}

void FenetreEmbarque::majOrganigramme() {
  m_blocs->clear();
  for (const auto& b : m_organigramme.blocs) {
    auto* it = new QListWidgetItem(QStringLiteral("%1 — %2").arg(q(b.genre), q(b.texte)));
    it->setData(Qt::UserRole, q(b.id));
    m_blocs->addItem(it);
  }
}

QStringList FenetreEmbarque::propositions(const QString& debut) const {
  QStringList l;
  if (const ConceptionElec* c = m_atelier.conceptionCourante())
    for (const auto& b : blocsProposes(*c, debut.toStdString())) l << q(b.texte);
  return l;
}

bool FenetreEmbarque::ajouterBloc(const QString& texte) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  for (const auto& p : blocsProposes(*c)) {
    if (q(p.texte) != texte) continue;
    BlocOrganigramme b;
    b.id = "b" + std::to_string(m_organigramme.blocs.size() + 1);
    b.genre = p.genre;
    b.texte = p.texte;
    b.parametres = p.parametres;
    if (!p.module.empty()) {
      b.parametres["module"] = p.module;
      b.parametres["bloc"] = p.bloc;
    }
    // Avant le retour de la boucle principale, sinon à la fin
    std::string principal;
    for (const auto& x : m_organigramme.blocs)
      if (x.genre == "principal") principal = x.id;
    BlocOrganigramme* dernier = nullptr;
    for (auto& x : m_organigramme.blocs)
      if (!principal.empty() ? x.suivant == principal && x.genre != "début" : x.suivant.empty()) dernier = &x;
    if (dernier) {
      b.suivant = dernier->suivant;
      dernier->suivant = b.id;
    }
    m_organigramme.blocs.push_back(b);
    m_atelier.modifierConception([&](ConceptionElec& x) { ecrireOrganigrammes(x, {m_organigramme}); }, QStringLiteral("Bloc %1").arg(texte));
    majOrganigramme();
    return true;
  }
  return false;
}

CodeOrganigramme FenetreEmbarque::convertir(bool python) {
  const ConceptionElec vide;
  const ConceptionElec& c = m_atelier.conceptionCourante() ? *m_atelier.conceptionCourante() : vide;
  const CodeOrganigramme r = python ? versPython(m_organigramme, c) : versArduino(m_organigramme, c);
  QString t = q(r.code) + QStringLiteral("\n// ---- Table des blocs ----\n");
  for (const auto& [id, l] : r.lignes) t += QStringLiteral("// %1 : lignes %2 à %3\n").arg(q(id)).arg(l.first).arg(l.second);
  for (const auto& e : r.erreurs) t += QStringLiteral("// erreur : %1\n").arg(q(e));
  m_codeOrganigramme->setPlainText(t);
  return r;
}

bool FenetreEmbarque::compilerOrganigramme() {
  const CodeOrganigramme r = convertir(false);
  if (!r.ok) return false;
  m_langage->setCurrentText(QStringLiteral("arduino"));
  m_editeur->setPlainText(q(r.code));
  m_compilation = compilerProgramme(r.code, "arduino", QDir(dossierProgrammes()).filePath(QStringLiteral("construction-organigramme")).toStdString(), frequence(), r.fichiers);
  m_etat->setText(m_compilation.reussi ? QStringLiteral("Croquis tiré de l’organigramme compilé : %1 octets.").arg(m_compilation.tailleProgramme)
                                       : QStringLiteral("Le croquis tiré de l’organigramme ne compile pas : %1").arg(q(m_compilation.journal).left(400)));
  return m_compilation.reussi;
}

// ---------- Composant HDL ----------
void FenetreEmbarque::construireHdl(QWidget* page) {
  auto* col = new QVBoxLayout(page);
  auto* l = new QHBoxLayout;
  m_langageHdl = new QComboBox;
  m_langageHdl->setObjectName(QStringLiteral("langageHdl"));
  m_langageHdl->addItems({QStringLiteral("verilog"), QStringLiteral("vhdl")});
  auto* ouvrir = bouton("ouvrirHdl", QStringLiteral("Ouvrir…"));
  auto* poser = bouton("poserHdl", QStringLiteral("Poser au schéma"));
  auto* ref = new QLineEdit(QStringLiteral("U2"));
  ref->setObjectName(QStringLiteral("referenceHdl"));
  auto* cosim = bouton("cosimulerHdl", QStringLiteral("Co-simuler"));
  for (QWidget* w : std::initializer_list<QWidget*>{m_langageHdl, ouvrir, poser, ref, cosim}) l->addWidget(w);
  l->addStretch(1);
  col->addLayout(l);
  m_codeHdl = new QPlainTextEdit;
  m_codeHdl->setObjectName(QStringLiteral("codeHdl"));
  m_codeHdl->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  m_codeHdl->setPlainText(QStringLiteral("module compteur(input clk, input raz, output reg [3:0] q);\n  initial q = 0;\n  always @(posedge clk or posedge raz)\n    if (raz) q <= 0;\n    else q <= q + 1;\nendmodule\n"));
  col->addWidget(m_codeHdl, 1);
  connect(ouvrir, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Composant HDL"), {}, QStringLiteral("Verilog ou VHDL (*.v *.sv *.vhd *.vhdl)"));
    if (f.isEmpty()) return;
    try {
      m_codeHdl->setPlainText(q(prom::lireFichier(f.toStdString())));
      m_langageHdl->setCurrentText(q(langageHdl(f.toStdString())));
    } catch (const std::exception& e) {
      m_etat->setText(q(e.what()));
    }
  });
  connect(poser, &QPushButton::clicked, this, [this] { poserHdl(m_codeHdl->toPlainText(), m_langageHdl->currentText()); });
  connect(cosim, &QPushButton::clicked, this, [this, ref] {
    QString e;
    if (!cosimulerHdl(ref->text(), &e)) m_etat->setText(e);
  });
}

bool FenetreEmbarque::poserHdl(const QString& code, const QString& langage) {
  std::string e;
  const auto m = lireModuleHdl(code.toStdString(), langage.toStdString(), &e);
  if (!m) {
    m_etat->setText(QStringLiteral("Module illisible : %1").arg(q(e)));
    return false;
  }
  std::string ref;
  const bool ok = m_atelier.modifierConception(
      [&](ConceptionElec& c) {
        const std::string u = poserComposantHdl(c, *m, 0, ptMm(200, 120));
        if (const ComposantElec* x = c.composant(u)) ref = x->reference;
      },
      QStringLiteral("Composant %1").arg(q(m->nom)));
  m_etat->setText(ok ? QStringLiteral("%1 posé (module « %2 », %3 port(s)).").arg(q(ref), q(m->nom)).arg(m->ports.size()) : QStringLiteral("Composant non posé."));
  return ok;
}

bool FenetreEmbarque::cosimulerHdl(const QString& reference, QString* erreur) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  FenetreLaboratoire* lab = m_atelier.laboratoire(QStringLiteral("continu"));
  lab->arreter();
  OptionsContinue oc;
  OptionsSimulation os;
  os.cosimulation[reference.toStdString()] = "hdl";
  const NetlistSpice n0 = netlistContinue(*c, oc, os);
  m_hdl = std::make_shared<CosimulationHdl>(*c, reference.toStdString(), n0);
  std::string e;
  if (!m_hdl->demarrer(dossierTemporaire(QStringLiteral("hdl")).toStdString(), &e)) {
    if (erreur) *erreur = q(e);
    return false;
  }
  lab->ajouterParticipant(m_hdl, reference.toStdString(), "hdl");
  return lab->demarrer(erreur);
}

// ---------- Machine à états ----------
void FenetreEmbarque::construireMachine(QWidget* page) {
  auto* col = new QVBoxLayout(page);
  auto* l = new QHBoxLayout;
  auto* feux = bouton("exempleFeux", QStringLiteral("Feux tricolores"));
  auto* comp = bouton("comparerMachine", QStringLiteral("Machine, bascules et GHDL"));
  auto* poser = bouton("poserBascules", QStringLiteral("Poser le circuit à bascules"));
  auto* vhdl = bouton("vhdlMachine", QStringLiteral("VHDL"));
  for (QWidget* w : std::initializer_list<QWidget*>{feux, comp, poser, vhdl}) l->addWidget(w);
  l->addStretch(1);
  col->addLayout(l);
  auto* coupe = new QSplitter(Qt::Horizontal);
  m_tableTransitions = new QTableWidget(0, 4);
  m_tableTransitions->setObjectName(QStringLiteral("tableTransitions"));
  m_tableTransitions->setHorizontalHeaderLabels({QStringLiteral("État"), QStringLiteral("Condition"), QStringLiteral("Suivant"), QStringLiteral("Sorties")});
  coupe->addWidget(m_tableTransitions);
  m_equations = new QPlainTextEdit;
  m_equations->setObjectName(QStringLiteral("equationsMachine"));
  m_equations->setReadOnly(true);
  coupe->addWidget(m_equations);
  m_sequences = new QTableWidget(0, 3);
  m_sequences->setObjectName(QStringLiteral("sequencesMachine"));
  m_sequences->setHorizontalHeaderLabels({QStringLiteral("Machine"), QStringLiteral("Bascules"), QStringLiteral("GHDL")});
  coupe->addWidget(m_sequences);
  col->addWidget(coupe, 1);
  connect(feux, &QPushButton::clicked, this, &FenetreEmbarque::exempleFeux);
  connect(comp, &QPushButton::clicked, this, [this] { comparerMachine(24); });
  connect(poser, &QPushButton::clicked, this, &FenetreEmbarque::poserBascules);
  connect(vhdl, &QPushButton::clicked, this, [this] { m_equations->setPlainText(q(vhdlMachine(m_machine))); });
}

void FenetreEmbarque::exempleFeux() {
  m_machine = machineFeuxTricolores();
  majMachine();
}

void FenetreEmbarque::majMachine() {
  const auto t = tableTransitions(m_machine);
  m_tableTransitions->setRowCount(static_cast<int>(t.size()));
  for (int i = 0; i < static_cast<int>(t.size()); ++i) {
    const auto& x = t[static_cast<std::size_t>(i)];
    QStringList s;
    for (const auto& y : x.sorties) s << q(y);
    m_tableTransitions->setItem(i, 0, new QTableWidgetItem(q(x.etat)));
    m_tableTransitions->setItem(i, 1, new QTableWidgetItem(q(x.condition)));
    m_tableTransitions->setItem(i, 2, new QTableWidgetItem(q(x.suivant)));
    m_tableTransitions->setItem(i, 3, new QTableWidgetItem(s.join(QStringLiteral(", "))));
  }
  const EquationsMachine eq = equationsMachine(m_machine);
  QString e = QStringLiteral("Codage sur %1 bascule(s) D, état initial en 0, remise à zéro synchrone :\n").arg(eq.bits);
  for (const auto& [k, v] : eq.textes) e += QStringLiteral("%1 = %2\n").arg(q(k), q(v));
  m_equations->setPlainText(e);
}

int FenetreEmbarque::comparerMachine(int periodes) {
  if (m_machine.etats.empty()) exempleFeux();
  std::vector<std::map<std::string, bool>> entrees(static_cast<std::size_t>(periodes));
  for (int p : {6, 7, 15})
    if (p < periodes)
      for (const auto& x : m_machine.entrees) entrees[static_cast<std::size_t>(p)][x] = true;
  const SequenceSorties a = simulerMachine(m_machine, entrees, periodes);
  const ConceptionElec c = circuitBascules(m_machine);
  const SequenceSorties b = simulerCircuitBascules(c, m_machine, entrees, periodes);
  SequenceSorties g;
  const bool ghdl = !prom::programmeDuPoste("ghdl").empty();
  if (ghdl) {
    const ResultatGhdl r = simulerGhdl({{"machine.vhd", vhdlMachine(m_machine)}, {"banc_machine.vhd", bancVhdlMachine(m_machine, entrees, periodes)}}, "banc_" + m_machine.nom,
                                       dossierTemporaire(QStringLiteral("machine")).toStdString());
    if (r.ok) g = lireSequenceGhdl(r.lignes, m_machine.sorties);
  }
  auto texte = [](const std::map<std::string, bool>& s) {
    QStringList t;
    for (const auto& [k, v] : s)
      if (v) t << q(k);
    return t.join(QLatin1Char('+'));
  };
  m_sequences->setRowCount(periodes);
  int accord = 0;
  for (int p = 0; p < periodes; ++p) {
    const auto& x = a[static_cast<std::size_t>(p)];
    const bool okB = p < static_cast<int>(b.size()) && b[static_cast<std::size_t>(p)] == x;
    const bool okG = !ghdl || (p < static_cast<int>(g.size()) && g[static_cast<std::size_t>(p)] == x);
    m_sequences->setItem(p, 0, new QTableWidgetItem(texte(x)));
    m_sequences->setItem(p, 1, new QTableWidgetItem(p < static_cast<int>(b.size()) ? texte(b[static_cast<std::size_t>(p)]) : QStringLiteral("—")));
    m_sequences->setItem(p, 2, new QTableWidgetItem(!ghdl ? QStringLiteral("GHDL absent") : p < static_cast<int>(g.size()) ? texte(g[static_cast<std::size_t>(p)]) : QStringLiteral("—")));
    accord += okB && okG;
  }
  m_etat->setText(QStringLiteral("%1 période(s) sur %2 identiques en machine, en bascules%3.").arg(accord).arg(periodes).arg(ghdl ? QStringLiteral(" et en VHDL simulé par GHDL") : QString()));
  return ghdl || accord != periodes ? accord : -1;
}

bool FenetreEmbarque::poserBascules() {
  if (m_machine.etats.empty()) exempleFeux();
  Document* d = m_atelier.document();
  if (!d) return false;
  const ConceptionElec c = circuitBascules(m_machine);
  d->modifier([&](prom::Projet& p) { p.electronique.push_back(c); }, QStringLiteral("Circuit à bascules"));
  m_etat->setText(QStringLiteral("Circuit à bascules ajouté au projet : %1 composant(s).").arg(c.composants.size()));
  return true;
}

// ---------- Robot ----------
void FenetreEmbarque::construireRobot(QWidget* page) {
  auto* col = new QVBoxLayout(page);
  auto* l = new QHBoxLayout;
  auto* sim = bouton("simulerRobot", QStringLiteral("Simuler un tour"));
  auto* telev = bouton("televerserRobot", QStringLiteral("Téléverser au robot…"));
  m_resultatRobot = new QLabel(QStringLiteral("—"));
  m_resultatRobot->setObjectName(QStringLiteral("resultatRobot"));
  l->addWidget(sim);
  l->addWidget(telev);
  l->addWidget(m_resultatRobot, 1);
  col->addLayout(l);
  m_vueRobot = new VueRobot;
  m_vueRobot->setObjectName(QStringLiteral("vueRobot"));
  m_vueRobot->setPiste(pisteOvale().piste);
  col->addWidget(m_vueRobot, 1);
  connect(sim, &QPushButton::clicked, this, [this] { simulerRobot(40, QStringLiteral("tour fini")); });
  connect(telev, &QPushButton::clicked, this, [this] {
    ProgrammeEmbarque p;
    const auto cmd = commandeTeleversement(p, m_compilation.hex, "/dev/ttyACM0");
    m_etat->setText(cmd.empty() ? QStringLiteral("avrdude manque sur ce poste.") : QStringLiteral("Le robot branché, le programme se téléverse par avrdude."));
  });
}

bool FenetreEmbarque::simulerRobot(double duree, const QString& attendu) {
  if (!m_compilation.reussi) {
    m_resultatRobot->setText(QStringLiteral("Compile d’abord le programme du robot."));
    return false;
  }
  std::string e;
  auto emu = creerEmulateur("atmega328p", frequence(), &e);
  if (!emu || !emu->charger(m_compilation.elf, &e)) {
    m_resultatRobot->setText(q(e));
    return false;
  }
  const PisteDepart d = pisteOvale();
  RobotDeuxRoues r;
  r.x = d.x;
  r.y = d.y;
  r.cap = d.cap;
  r.capteursLigne = {{40, 14}, {40, 0}, {40, -14}};
  OptionsRobot o;
  o.duree = duree;
  o.attendu = attendu.toStdString();
  m_robot = prom::ecad::simulerRobot(*emu, d.piste, r, BrochesRobot{}, o);
  m_vueRobot->setPiste(d.piste);
  m_vueRobot->setTrajet(m_robot.trajet);
  m_resultatRobot->setText(m_robot.attenduRecu ? QStringLiteral("« %1 » reçu à %2 s, après %3 m.").arg(attendu).arg(m_robot.tempsAttendu, 0, 'f', 2).arg(m_robot.distance / 1000, 0, 'f', 2)
                                               : QStringLiteral("« %1 » non reçu en %2 s (%3 m parcourus).").arg(attendu).arg(m_robot.temps, 0, 'f', 1).arg(m_robot.distance / 1000, 0, 'f', 2));
  return m_robot.attenduRecu;
}

// ---------- Logique programmable ----------
void FenetreEmbarque::construireLogique(QWidget* page) {
  auto* col = new QVBoxLayout(page);
  auto* l = new QHBoxLayout;
  auto* vhdl = bouton("vhdlLogique", QStringLiteral("VHDL du schéma"));
  auto* table = bouton("tableGhdl", QStringLiteral("Table de vérité par GHDL"));
  auto* synth = bouton("syntheseYosys", QStringLiteral("Synthèse Yosys"));
  auto* jedec = bouton("exporterJedec", QStringLiteral("JEDEC GAL16V8…"));
  for (QWidget* w : std::initializer_list<QWidget*>{vhdl, table, synth, jedec}) l->addWidget(w);
  l->addStretch(1);
  col->addLayout(l);
  m_sortieLogique = new QPlainTextEdit;
  m_sortieLogique->setObjectName(QStringLiteral("sortieLogique"));
  m_sortieLogique->setReadOnly(true);
  m_sortieLogique->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  col->addWidget(m_sortieLogique, 1);
  connect(vhdl, &QPushButton::clicked, this, [this] { vhdlLogique(); });
  connect(table, &QPushButton::clicked, this, [this] {
    QString d;
    verifierTableGhdl(&d);
    m_sortieLogique->setPlainText(d);
  });
  connect(synth, &QPushButton::clicked, this, [this] {
    const ConceptionElec* c = m_atelier.conceptionCourante();
    if (!c) return;
    const SyntheseYosys y = synthetiserYosys(verilogStructurel(circuitLogique(*c), "circuit"), "circuit", dossierTemporaire(QStringLiteral("yosys")).toStdString());
    m_sortieLogique->setPlainText(y.ok ? QStringLiteral("Yosys : %1 cellule(s).\n\n%2").arg(y.cellules).arg(q(y.journal).right(3000)) : QStringLiteral("Synthèse impossible : %1").arg(q(y.journal)));
  });
  connect(jedec, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("JEDEC"), QStringLiteral("circuit.jed"), QStringLiteral("JEDEC (*.jed)"));
    if (!f.isEmpty()) exporterJedec(f);
  });
}

namespace {
// Entrées et sorties d'un circuit logique : réseaux sans pilote, réseaux nommés pilotés
std::pair<std::vector<std::string>, std::vector<std::string>> portsDuCircuit(const CircuitLogique& cl) {
  std::vector<int> pilotes(cl.signaux.size(), 0);
  for (const auto& p : cl.portes)
    for (int s : p.sorties)
      if (s >= 0) pilotes[static_cast<std::size_t>(s)]++;
  std::vector<std::string> e = cl.entrees, s;
  std::vector<int> lus(cl.signaux.size(), 0);
  for (const auto& p : cl.portes)
    for (int x : p.entrees) lus[static_cast<std::size_t>(x)]++;
  for (std::size_t i = 0; i < cl.signaux.size(); ++i) {
    const std::string& n = cl.signaux[i];
    if (pilotes[i] && !lus[i] && n.rfind("nc_", 0) != 0 && n.find('(') == std::string::npos && n.rfind("Net-", 0) != 0) s.push_back(n);
  }
  return {e, s};
}
}  // namespace

QString FenetreEmbarque::vhdlLogique() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return {};
  const QString t = q(vhdlStructurel(circuitLogique(*c), "circuit"));
  m_sortieLogique->setPlainText(t);
  return t;
}

bool FenetreEmbarque::verifierTableGhdl(QString* detail) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  const CircuitLogique cl = circuitLogique(*c);
  const auto [e, s] = portsDuCircuit(cl);
  const auto schema = tableVerite(cl, e, s);
  std::string erreur;
  const auto ghdl = tableVeriteGhdl(cl, e, s, dossierTemporaire(QStringLiteral("ghdl")).toStdString(), &erreur);
  QString t = QStringLiteral("Entrées %1 · sorties %2\n").arg(q([&] {
                                                                 std::string x;
                                                                 for (const auto& v : e) x += v + " ";
                                                                 return x;
                                                               }()),
                                                               q([&] {
                                                                 std::string x;
                                                                 for (const auto& v : s) x += v + " ";
                                                                 return x;
                                                               }()));
  for (std::size_t i = 0; i < schema.size(); ++i)
    t += QStringLiteral("%1 → schéma %2, GHDL %3\n").arg(q(schema[i].first), q(schema[i].second), i < ghdl.size() ? q(ghdl[i].second) : QStringLiteral("—"));
  const bool ok = !ghdl.empty() && ghdl == schema;
  t += ok ? QStringLiteral("Même table de vérité.") : QStringLiteral("Tables différentes ou GHDL en échec : %1").arg(q(erreur).left(500));
  if (detail) *detail = t;
  m_sortieLogique->setPlainText(t);
  return ok;
}

bool FenetreEmbarque::exporterJedec(const QString& chemin) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return false;
  const CircuitLogique cl = circuitLogique(*c);
  const auto [e, s] = portsDuCircuit(cl);
  const auto table = tableVerite(cl, e, s);
  std::map<std::string, std::vector<Implicant>> eq;
  const int n = static_cast<int>(e.size());
  for (std::size_t k = 0; k < s.size(); ++k) {
    std::vector<std::uint32_t> uns;
    for (const auto& [entree, sortie] : table) {
      if (k >= sortie.size() || sortie[k] != '1') continue;
      // « entree » : première entrée en poids fort ; variable i : entrée i
      std::uint32_t x = 0;
      for (int i = 0; i < n; ++i)
        if (entree[static_cast<std::size_t>(i)] == '1') x |= 1u << i;
      uns.push_back(x);
    }
    eq[s[k]] = reduire(n, uns);
  }
  const PlacementGal g = placerGal16V8(e, eq, "PROMETHEE");
  if (!g.ok) {
    QStringList t;
    for (const auto& x : g.erreurs) t << q(x);
    m_sortieLogique->setPlainText(t.join(QLatin1Char('\n')));
    return false;
  }
  try {
    prom::ecrireFichierAtomique(chemin.toStdString(), g.jedec);
  } catch (const std::exception& x) {
    m_etat->setText(q(x.what()));
    return false;
  }
  QString t = QStringLiteral("GAL16V8 en mode simple :\n");
  for (const auto& [nom, b] : g.brochesEntrees) t += QStringLiteral("  entrée %1 → broche %2\n").arg(q(nom)).arg(b);
  for (const auto& [nom, b] : g.brochesSorties) t += QStringLiteral("  sortie %1 → broche %2 : %3\n").arg(q(nom)).arg(b).arg(q(sommeDeProduits(eq[nom], e, n)));
  m_sortieLogique->setPlainText(t + QStringLiteral("\nÉcrit dans %1").arg(chemin));
  return true;
}

// ======================================================================================================================
// Atelier
// ======================================================================================================================
FenetreEmbarque* AtelierElectronique::embarque(const QString& onglet) {
  if (!m_embarque) m_embarque = new FenetreEmbarque(*this, this);
  m_embarque->montrer(onglet);
  return m_embarque;
}

void AtelierElectronique::montrerBroche(const std::string& composant, const std::string& numero) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  montrerComposant(composant);
  const Connectivite k = connectivite(*c);
  for (const auto& f : c->feuilles)
    for (const auto& sp : f.symboles) {
      if (sp.composant != composant) continue;
      const DefSymbole* d = c->symbole(sp.symbole);
      if (!d) continue;
      for (const auto& b : brochesPlacees(sp, *d))
        if (b.broche->numero == numero) {
          const Pt p = b.p;
          QTimer::singleShot(0, this, [this, p] { m_schema->centrerSur(p); });
          if (const std::string r = k.nom(composant, numero); !r.empty()) surligner(QString::fromStdString(r));
          return;
        }
    }
}

void AtelierElectronique::enregistrerCommandesL15() {
  auto& c = m_commandes;
  c["programme.ouvrir"] = [this] { embarque(QStringLiteral("programme")); };
  c["programme.cosimuler"] = [this] { embarque(QStringLiteral("cosimulation")); };
  c["programme.organigramme"] = [this] { embarque(QStringLiteral("organigramme")); };
  c["programme.hdl"] = [this] { embarque(QStringLiteral("hdl")); };
  c["programme.machine"] = [this] { embarque(QStringLiteral("machine")); };
  c["programme.robot"] = [this] { embarque(QStringLiteral("robot")); };
  c["programme.logique"] = [this] { embarque(QStringLiteral("logique")); };
}

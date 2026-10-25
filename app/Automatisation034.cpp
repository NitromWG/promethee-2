// Prométhée : automatisation et assistant de l'atelier électronique (lot L13 de la 0.34). Voir Automatisation034.hpp.
// Licence GPL-3.0-only.
#include "Automatisation034.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStringListModel>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "FenetreSimulation.hpp"
#include "SelectionElectronique.hpp"
#include "promethee/brochages.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/requetes_objets.hpp"

using namespace prom::ecad;
using J = nlohmann::ordered_json;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QString h(const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); }

// Travail long (programme du poste, routage) sur un fil : l'interface se repeint pendant ce temps, sans recevoir de
// clic ni de frappe, et le résultat revient ici
template <class F>
auto surUnFil(F f) -> decltype(f()) {
  using R = decltype(f());
  std::optional<R> r;
  std::atomic<bool> fini{false};
  std::thread t([&] {
    r.emplace(f());
    fini = true;
  });
  QApplication::setOverrideCursor(Qt::BusyCursor);
  while (!fini) QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 30);
  t.join();
  QApplication::restoreOverrideCursor();
  return std::move(*r);
}

QStringList listeDe(const std::vector<std::string>& v) {
  QStringList l;
  for (const auto& x : v) l << q(x);
  return l;
}
}  // namespace

// ======================================================================================================================
// 13.8 · Ligne de commande
// ======================================================================================================================
LigneCommandes::LigneCommandes(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  setObjectName(QStringLiteral("ligneCommandes"));
  auto* l = new QHBoxLayout(this);
  l->setContentsMargins(6, 2, 6, 2);
  l->setSpacing(6);
  auto* invite = new QLabel(QStringLiteral("›"));
  invite->setToolTip(QStringLiteral("Ligne de commande : chaque action a sa forme écrite (Ctrl+: y mène)"));
  m_champ = new QLineEdit;
  m_champ->setObjectName(QStringLiteral("champCommande"));
  m_champ->setPlaceholderText(QStringLiteral("Commande : carte.contour largeur=100 hauteur=80 · composant.valeur ref=R1 valeur=4k7 · ↑ ↓ l’historique, Tab la complétion"));
  m_completions = new QStringListModel(this);
  auto* completion = new QCompleter(m_completions, m_champ);
  completion->setCaseSensitivity(Qt::CaseInsensitive);
  completion->setCompletionMode(QCompleter::PopupCompletion);
  m_champ->setCompleter(completion);
  connect(m_champ, &QLineEdit::textEdited, this, &LigneCommandes::majCompletion);
  connect(m_champ, &QLineEdit::returnPressed, this, [this] {
    const QString t = m_champ->text().trimmed();
    if (t.isEmpty()) return;
    if (executer(t)) m_champ->clear();
  });
  m_champ->installEventFilter(this);
  l->addWidget(invite);
  l->addWidget(m_champ, 3);
  for (int i = 0; i < 3; ++i) {
    auto* b = new QPushButton;
    b->setObjectName(QStringLiteral("commandeProbable%1").arg(i));
    b->setFlat(true);
    b->setToolTip(QStringLiteral("Commande probable après la dernière, d’après les successions observées sur ce poste"));
    b->hide();
    connect(b, &QPushButton::clicked, this, [this, b] {
      m_champ->setText(b->property("ligne").toString());
      m_champ->setFocus();
    });
    m_probables.push_back(b);
    l->addWidget(b);
  }
  m_message = new QLabel;
  m_message->setObjectName(QStringLiteral("messageCommande"));
  m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
  l->addWidget(m_message, 2);
  m_bouton = new QToolButton;
  m_bouton->setObjectName(QStringLiteral("enregistrerMacro"));
  m_bouton->setCheckable(true);
  m_bouton->setText(QStringLiteral("● Macro"));
  m_bouton->setToolTip(QStringLiteral("Enregistre les commandes réussies : la macro se garde dans un fichier texte et se rejoue d’un coup"));
  connect(m_bouton, &QToolButton::toggled, this, &LigneCommandes::setEnregistrement);
  l->addWidget(m_bouton);
  auto* menu = new QToolButton;
  menu->setObjectName(QStringLiteral("menuMacros"));
  menu->setText(QStringLiteral("…"));
  menu->setPopupMode(QToolButton::InstantPopup);
  auto* m = new QMenu(menu);
  m->addAction(QStringLiteral("Enregistrer la macro…"), this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Macro"), QStringLiteral("macro.prom-macro"), QStringLiteral("Macro Prométhée (*.prom-macro *.txt)"));
    if (f.isEmpty()) return;
    bool ok = false;
    const QString nom = QInputDialog::getText(this, QStringLiteral("Macro"), QStringLiteral("Nom de la macro"), QLineEdit::Normal, QFileInfo(f).completeBaseName(), &ok);
    if (ok) enregistrerMacro(f, nom);
  });
  m->addAction(QStringLiteral("Rejouer une macro…"), this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Rejouer une macro"), {}, QStringLiteral("Macro Prométhée (*.prom-macro *.txt);;Tous les fichiers (*)"));
    if (!f.isEmpty()) rejouerFichier(f);
  });
  m->addAction(QStringLiteral("Affecter la commande à une touche…"), this, [this] {
    const QString ligne = m_champ->text().trimmed().isEmpty() && !m_historique.isEmpty() ? m_historique.front() : m_champ->text().trimmed();
    if (ligne.isEmpty()) return dire(QStringLiteral("Tape d’abord la commande à affecter."), true);
    bool ok = false;
    const QString t = QInputDialog::getText(this, QStringLiteral("Touche"), QStringLiteral("Touche pour « %1 » (par exemple Ctrl+Alt+1)").arg(ligne), QLineEdit::Normal, {}, &ok);
    if (ok) affecter(t, ligne);
  });
  menu->setMenu(m);
  l->addWidget(menu);
  QSettings s;
  m_historique = s.value(QStringLiteral("electronique/historiqueCommandes")).toStringList();
  m_predicteur.lire(s.value(QStringLiteral("electronique/predicteurCommandes")).toString().toStdString());
  installerRaccourcis();
  majPropositions();
}

void LigneCommandes::activer() {
  show();
  m_champ->setFocus();
  m_champ->selectAll();
}

QString LigneCommandes::dernierMessage() const { return m_message->text(); }

void LigneCommandes::dire(const QString& m, bool erreur) {
  m_message->setText(m);
  m_message->setStyleSheet(erreur ? QStringLiteral("color: #B3261E;") : QString());
}

void LigneCommandes::majCompletion(const QString& texte) {
  m_completions->setStringList(listeDe(prom::completer(texte.toStdString(), 12)));
}

void LigneCommandes::majPropositions() {
  const auto v = m_predicteur.proposer(3);
  for (std::size_t i = 0; i < m_probables.size(); ++i) {
    QPushButton* b = m_probables[i];
    if (i >= v.size()) {
      b->hide();
      continue;
    }
    const QString ligne = q(prom::ligneDeRequete(J{{"method", v[i]}, {"params", J::object()}})).trimmed() + QLatin1Char(' ');
    b->setProperty("ligne", ligne);
    b->setText(QStringLiteral("%1").arg(q(v[i])));
    b->show();
  }
}

QStringList LigneCommandes::propositions() const { return listeDe(m_predicteur.proposer(3)); }

bool LigneCommandes::executer(const QString& ligne, QString* message) {
  auto finir = [&](const QString& m, bool ok) {
    dire(m, !ok);
    if (message) *message = m;
    return ok;
  };
  std::string erreur;
  const auto requete = prom::requeteDeLigne(ligne.toStdString(), &erreur);
  if (!requete) return finir(erreur.empty() ? QStringLiteral("Rien à faire : ligne vide ou commentaire.") : q(erreur), false);
  Document* doc = m_atelier.document();
  J r = *requete;
  if (!r.contains("params") || !r["params"].is_object()) r["params"] = J::object();
  if (!r["params"].contains("conception") && m_atelier.conception() >= 0) r["params"]["conception"] = m_atelier.conception();
  prom::Projet copie = doc->projet();
  const prom::ReponseApi rep = prom::traiterRequete(copie, r);
  const std::string methode = r.value("method", std::string());
  if (rep.reponse.contains("error")) return finir(q(rep.reponse["error"].value("message", std::string("erreur"))), false);
  if (rep.modifie) doc->modifier([&copie](prom::Projet& p) { p = std::move(copie); }, q(rep.nomPas.empty() ? "Commande " + methode : rep.nomPas));
  m_historique.removeAll(ligne);
  m_historique.prepend(ligne);
  while (m_historique.size() > 100) m_historique.removeLast();
  m_rang = -1;
  m_predicteur.observer(methode);
  QSettings s;
  s.setValue(QStringLiteral("electronique/historiqueCommandes"), m_historique);
  s.setValue(QStringLiteral("electronique/predicteurCommandes"), q(m_predicteur.texte()));
  if (m_enregistre) m_macro.lignes.push_back(ligne.toStdString());
  majPropositions();
  QString m;
  if (rep.modifie) m = q(rep.nomPas);
  else if (rep.reponse.contains("result")) m = q(rep.reponse["result"].dump()).left(200);
  return finir(m.isEmpty() ? QStringLiteral("Fait.") : m, true);
}

void LigneCommandes::setEnregistrement(bool oui) {
  if (oui == m_enregistre) return;
  m_enregistre = oui;
  if (oui) m_macro = {};
  if (m_bouton->isChecked() != oui) m_bouton->setChecked(oui);
  dire(oui ? QStringLiteral("Enregistrement de la macro : les commandes réussies s’ajoutent.") : QStringLiteral("Macro enregistrée : %1 commande(s).").arg(m_macro.lignes.size()), false);
}

bool LigneCommandes::rejouer(const prom::Macro& m, QString* message) {
  auto finir = [&](const QString& t, bool ok) {
    dire(t, !ok);
    if (message) *message = t;
    return ok;
  };
  // Les lignes visent la conception ouverte ; tout ou rien, en un seul pas
  J lot = J::array();
  int n = 0;
  for (const auto& l : m.lignes) {
    ++n;
    std::string e;
    const auto x = prom::requeteDeLigne(l, &e);
    if (!x) {
      if (!e.empty()) return finir(QStringLiteral("Ligne %1 : %2").arg(n).arg(q(e)), false);
      continue;
    }
    J r = *x;
    r["id"] = n;
    if (!r.contains("params") || !r["params"].is_object()) r["params"] = J::object();
    if (!r["params"].contains("conception") && m_atelier.conception() >= 0) r["params"]["conception"] = m_atelier.conception();
    lot.push_back(r);
  }
  Document* doc = m_atelier.document();
  prom::Projet copie = doc->projet();
  const prom::ReponseApi rep = prom::traiterRequete(copie, J{{"jsonrpc", "2.0"}, {"id", 0}, {"method", "lot"}, {"params", lot}});
  if (rep.reponse.contains("error")) return finir(q(rep.reponse["error"].value("message", std::string("erreur"))), false);
  const QString nom = QStringLiteral("Macro « %1 »").arg(m.nom.empty() ? QStringLiteral("sans nom") : q(m.nom));
  if (rep.modifie) doc->modifier([&copie](prom::Projet& p) { p = std::move(copie); }, nom);
  return finir(QStringLiteral("%1 : %2 commande(s) rejouée(s).").arg(nom).arg(lot.size()), true);
}

bool LigneCommandes::enregistrerMacro(const QString& chemin, const QString& nom) {
  prom::Macro m = m_macro;
  m.nom = nom.toStdString();
  try {
    prom::ecrireFichierAtomique(chemin.toStdString(), prom::texteMacro(m));
  } catch (const std::exception& e) {
    dire(q(e.what()), true);
    return false;
  }
  m_macro.nom = m.nom;
  dire(QStringLiteral("Macro « %1 » écrite : %2 commande(s).").arg(nom).arg(m.lignes.size()), false);
  return true;
}

bool LigneCommandes::rejouerFichier(const QString& chemin, QString* message) {
  std::string texte;
  try {
    texte = prom::lireFichier(chemin.toStdString());
  } catch (const std::exception& e) {
    dire(q(e.what()), true);
    if (message) *message = q(e.what());
    return false;
  }
  return rejouer(prom::lireMacro(texte), message);
}

bool LigneCommandes::affecter(const QString& touche, const QString& ligne) {
  const QKeySequence k(touche);
  if (k.isEmpty()) {
    dire(QStringLiteral("Touche « %1 » non comprise.").arg(touche), true);
    return false;
  }
  QSettings s;
  QVariantMap v = s.value(QStringLiteral("electronique/touchesCommandes")).toMap();
  v[k.toString(QKeySequence::PortableText)] = ligne;
  s.setValue(QStringLiteral("electronique/touchesCommandes"), v);
  installerRaccourcis();
  dire(QStringLiteral("%1 lance « %2 ».").arg(k.toString(QKeySequence::NativeText), ligne), false);
  return true;
}

void LigneCommandes::installerRaccourcis() {
  QWidget* cible = m_atelier.fenetrePanneaux() ? static_cast<QWidget*>(m_atelier.fenetrePanneaux()) : static_cast<QWidget*>(&m_atelier);
  for (QShortcut* x : cible->findChildren<QShortcut*>(QStringLiteral("toucheCommande"))) x->deleteLater();
  const QVariantMap v = QSettings().value(QStringLiteral("electronique/touchesCommandes")).toMap();
  for (auto it = v.begin(); it != v.end(); ++it) {
    auto* r = new QShortcut(QKeySequence(it.key(), QKeySequence::PortableText), cible);
    r->setObjectName(QStringLiteral("toucheCommande"));
    r->setContext(Qt::WidgetWithChildrenShortcut);
    const QString ligne = it.value().toString();
    connect(r, &QShortcut::activated, this, [this, ligne] { executer(ligne); });
  }
}

bool LigneCommandes::eventFilter(QObject* o, QEvent* e) {
  if (o == m_champ && e->type() == QEvent::KeyPress) {
    auto* k = static_cast<QKeyEvent*>(e);
    const bool popup = m_champ->completer() && m_champ->completer()->popup() && m_champ->completer()->popup()->isVisible();
    if (!popup && (k->key() == Qt::Key_Up || k->key() == Qt::Key_Down) && !m_historique.isEmpty()) {
      m_rang = std::clamp(m_rang + (k->key() == Qt::Key_Up ? 1 : -1), -1, static_cast<int>(m_historique.size()) - 1);
      m_champ->setText(m_rang < 0 ? QString() : m_historique[m_rang]);
      return true;
    }
    if (!popup && k->key() == Qt::Key_Tab) {
      const auto v = prom::completer(m_champ->text().toStdString(), 1);
      if (!v.empty()) m_champ->setText(q(v.front()));
      return true;
    }
  }
  return QWidget::eventFilter(o, e);
}

// ======================================================================================================================
// 13.3, 13.9 · Console Python
// ======================================================================================================================
ConsolePython::ConsolePython(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  setObjectName(QStringLiteral("consolePython"));
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(4, 4, 4, 4);
  auto* barre = new QHBoxLayout;
  auto bouton = [&](const char* nom, const QString& texte, const QString& aide) {
    auto* b = new QPushButton(texte);
    b->setObjectName(QLatin1String(nom));
    b->setToolTip(aide);
    barre->addWidget(b);
    return b;
  };
  m_lancer = bouton("lancerScript", QStringLiteral("▶ Lancer"), QStringLiteral("Le script tourne sur une copie du projet ; ses modifications font un seul pas d’annulation"));
  m_continuer = bouton("continuerScript", QStringLiteral("Continuer"), QStringLiteral("Jusqu’au point d’arrêt suivant"));
  m_pas = bouton("pasScript", QStringLiteral("Pas"), QStringLiteral("Une ligne"));
  m_arreter = bouton("arreterScript", QStringLiteral("■ Arrêter"), QStringLiteral("Arrête le script : rien n’est appliqué"));
  auto* point = bouton("pointArret", QStringLiteral("● Point d’arrêt"), QStringLiteral("Sur la ligne du curseur ; un second clic le retire"));
  m_pasAPas = new QCheckBox(QStringLiteral("Pas à pas"));
  m_pasAPas->setObjectName(QStringLiteral("pasAPasScript"));
  barre->addWidget(m_pasAPas);
  barre->addStretch(1);
  m_ranges = new QComboBox;
  m_ranges->setObjectName(QStringLiteral("scriptsRanges"));
  m_ranges->setMinimumWidth(140);
  barre->addWidget(m_ranges);
  auto* ouvrirRange = bouton("ouvrirRange", QStringLiteral("Ouvrir"), QStringLiteral("Le script rangé choisi, dans l’éditeur"));
  auto* ranger = bouton("rangerScript", QStringLiteral("Ranger…"), QStringLiteral("Garde le script sous un nom, sur une touche s’il en a une"));
  auto* fichier = bouton("fichierScript", QStringLiteral("Fichier…"), QStringLiteral("Ouvre un script .py"));
  col->addLayout(barre);
  auto* coupe = new QSplitter(Qt::Vertical);
  m_editeur = new QPlainTextEdit;
  m_editeur->setObjectName(QStringLiteral("editeurScript"));
  QFont fixe(QStringLiteral("monospace"));
  fixe.setStyleHint(QFont::Monospace);
  m_editeur->setFont(fixe);
  m_editeur->setPlainText(QStringLiteral("# Le module « promethee » pilote le projet ouvert ; tout le script fait un seul pas d’annulation\n"
                                         "import promethee\n"
                                         "for i in range(10):\n"
                                         "    promethee.poser_symbole(\"R\", x=20 + 10 * i, y=60, valeur=\"10k\")\n"
                                         "print(len(promethee.composants()), \"composants\")\n"));
  coupe->addWidget(m_editeur);
  auto* bas = new QSplitter(Qt::Horizontal);
  m_sortie = new QPlainTextEdit;
  m_sortie->setObjectName(QStringLiteral("sortieScript"));
  m_sortie->setReadOnly(true);
  m_sortie->setFont(fixe);
  bas->addWidget(m_sortie);
  m_trace = new TraceCourbes;
  m_trace->setObjectName(QStringLiteral("traceScript"));
  m_trace->setMinimumWidth(260);
  bas->addWidget(m_trace);
  coupe->addWidget(bas);
  coupe->setStretchFactor(0, 3);
  coupe->setStretchFactor(1, 2);
  col->addWidget(coupe, 1);
  m_etat = new QLabel;
  m_etat->setObjectName(QStringLiteral("etatScript"));
  m_etat->setWordWrap(true);
  col->addWidget(m_etat);
  connect(m_lancer, &QPushButton::clicked, this, [this] { executer(false); });
  connect(m_continuer, &QPushButton::clicked, this, [this] { reprendre("continuer"); });
  connect(m_pas, &QPushButton::clicked, this, [this] { reprendre("pas"); });
  connect(m_arreter, &QPushButton::clicked, this, [this] { reprendre("arreter"); });
  connect(point, &QPushButton::clicked, this, [this] { basculerPointArret(m_editeur->textCursor().blockNumber() + 1); });
  connect(m_pasAPas, &QCheckBox::toggled, this, [this](bool oui) { setPasAPas(oui); });
  connect(ouvrirRange, &QPushButton::clicked, this, [this] {
    const QVariantMap v = QSettings().value(QStringLiteral("electronique/scriptsRanges")).toMap();
    if (v.contains(m_ranges->currentText())) setCode(v.value(m_ranges->currentText()).toMap().value(QStringLiteral("code")).toString());
  });
  connect(ranger, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const QString nom = QInputDialog::getText(this, QStringLiteral("Ranger le script"), QStringLiteral("Nom"), QLineEdit::Normal, m_ranges->currentText(), &ok);
    if (!ok || nom.trimmed().isEmpty()) return;
    const QString touche = QInputDialog::getText(this, QStringLiteral("Ranger le script"), QStringLiteral("Touche (facultative, par exemple Ctrl+Alt+P)"), QLineEdit::Normal, {}, &ok);
    if (ok) this->ranger(nom.trimmed(), touche.trimmed());
  });
  connect(fichier, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Script Python"), {}, QStringLiteral("Script Python (*.py)"));
    if (f.isEmpty()) return;
    try {
      setCode(q(prom::lireFichier(f.toStdString())));
    } catch (const std::exception& e) {
      m_etat->setText(q(e.what()));
    }
  });
  connect(m_editeur, &QPlainTextEdit::textChanged, this, [this] { marquer(); });
  m_ranges->addItems(scriptsRanges());
  // Les scripts rangés sur une touche la retrouvent
  const QVariantMap v = QSettings().value(QStringLiteral("electronique/scriptsRanges")).toMap();
  for (auto it = v.begin(); it != v.end(); ++it) {
    const QString touche = it.value().toMap().value(QStringLiteral("touche")).toString();
    if (touche.isEmpty() || !m_atelier.fenetrePanneaux()) continue;
    auto* r = new QShortcut(QKeySequence(touche, QKeySequence::PortableText), m_atelier.fenetrePanneaux());
    r->setContext(Qt::WidgetWithChildrenShortcut);
    const QString code = it.value().toMap().value(QStringLiteral("code")).toString();
    connect(r, &QShortcut::activated, this, [this, code] {
      setCode(code);
      executer(false);
    });
  }
  std::string version;
  m_etat->setText(prom::pythonDisponible(&version) ? QStringLiteral("Python %1 sur ce poste.").arg(q(version))
                                                   : QStringLiteral("Python 3 n’est pas installé sur ce poste : la console attend python3 (ou python)."));
  m_continuer->setEnabled(false);
  m_pas->setEnabled(false);
  m_arreter->setEnabled(false);
}

ConsolePython::~ConsolePython() {
  m_arret = true;
  {
    std::lock_guard<std::mutex> v(m_verrou);
    m_choix = "arreter";
  }
  m_reveil.notify_all();
  if (m_fil.joinable()) m_fil.join();
}

void ConsolePython::setCode(const QString& code) { m_editeur->setPlainText(code); }
QString ConsolePython::code() const { return m_editeur->toPlainText(); }
QString ConsolePython::sortie() const { return m_sortie->toPlainText(); }

void ConsolePython::basculerPointArret(int ligne) {
  if (ligne < 1) return;
  if (!m_points.erase(ligne)) m_points.insert(ligne);
  marquer();
}

void ConsolePython::setPasAPas(bool oui) {
  if (m_pasAPas->isChecked() != oui) m_pasAPas->setChecked(oui);
}

// Points d'arrêt en rouge pâle, ligne arrêtée en jaune, ligne fautive soulignée
void ConsolePython::marquer() {
  QList<QTextEdit::ExtraSelection> v;
  auto ligne = [&](int n, const QColor& fond) {
    const QTextBlock b = m_editeur->document()->findBlockByNumber(n - 1);
    if (!b.isValid()) return;
    QTextEdit::ExtraSelection s;
    s.cursor = QTextCursor(b);
    s.format.setBackground(fond);
    s.format.setProperty(QTextFormat::FullWidthSelection, true);
    v.push_back(s);
  };
  for (int n : m_points) ligne(n, QColor(0xF8, 0xD7, 0xDA));
  if (m_enCours && m_ligneArret > 0) ligne(m_ligneArret, QColor(0xFF, 0xF1, 0xB8));
  if (!m_enCours && !m_resultat.reussi && m_resultat.ligneErreur > 0) ligne(m_resultat.ligneErreur, QColor(0xFF, 0xCD, 0xD2));
  m_editeur->setExtraSelections(v);
}

bool ConsolePython::executer(bool attendre) {
  if (m_enCours) return false;
  if (m_fil.joinable()) m_fil.join();
  std::string version;
  if (!prom::pythonDisponible(&version)) {
    m_etat->setText(QStringLiteral("Python 3 n’est pas installé sur ce poste : la console attend python3 (ou python)."));
    return false;
  }
  Document* doc = m_atelier.document();
  m_sortie->clear();
  m_trace->setCourbes({}, {}, {}, false);
  m_enCours = true;
  m_arret = false;
  m_choix.reset();
  m_ligneArret = 0;
  m_resultat = {};
  m_lancer->setEnabled(false);
  m_arreter->setEnabled(true);
  m_etat->setText(QStringLiteral("Le script tourne…"));
  prom::Projet copie = doc->projet();
  const QStringList historique = doc->historique();
  const int position = doc->positionHistorique();
  prom::OptionsScript o;
  o.pointsArret = m_points;
  o.pasAPas = m_pasAPas->isChecked();
  o.arret = &m_arret;
  o.nom = "console";
  o.conception = m_atelier.conception();
  QPointer<ConsolePython> moi(this);
  o.sortie = [moi](const std::string& l) {
    QMetaObject::invokeMethod(qApp, [moi, t = q(l)] { if (moi) moi->m_sortie->appendPlainText(t); }, Qt::QueuedConnection);
  };
  o.pause = [this, moi](int ligne, const J& variables) -> std::string {
    QMetaObject::invokeMethod(qApp, [moi, ligne, variables] { if (moi) moi->arrete(ligne, variables); }, Qt::QueuedConnection);
    std::unique_lock<std::mutex> v(m_verrou);
    m_reveil.wait(v, [this] { return m_choix.has_value() || m_arret.load(); });
    const std::string c = m_arret ? std::string("arreter") : *m_choix;
    m_choix.reset();
    return c;
  };
  const std::string code = m_editeur->toPlainText().toStdString();
  m_fil = std::thread([moi, copie = std::move(copie), code, o, historique, position]() mutable {
    prom::ResultatScript r = prom::executerScript(copie, code, o);
    QMetaObject::invokeMethod(qApp, [moi, r = std::move(r), copie = std::move(copie), historique, position]() mutable {
      if (!moi) return;
      // Le projet a changé pendant le script (une autre modification) : rien n'est appliqué, pour ne rien écraser
      if (moi->m_atelier.document()->historique() != historique || moi->m_atelier.document()->positionHistorique() != position) {
        r.modifications = 0;
        r.erreurs += (r.erreurs.empty() ? "" : "\n") + std::string("Le projet a changé pendant le script : ses modifications ne sont pas appliquées ; relance-le.");
      }
      moi->fini(std::move(r), std::move(copie));
    }, Qt::QueuedConnection);
  });
  if (attendre)
    while (m_enCours) QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 30);
  return true;
}

void ConsolePython::arrete(int ligne, const J& variables) {
  m_ligneArret = ligne;
  m_continuer->setEnabled(true);
  m_pas->setEnabled(true);
  QString t = QStringLiteral("Arrêt à la ligne %1").arg(ligne);
  QStringList vars;
  if (variables.is_object())
    for (auto it = variables.begin(); it != variables.end(); ++it) vars << QStringLiteral("%1 = %2").arg(q(it.key()), q(it.value().is_string() ? it.value().get<std::string>() : it.value().dump()).left(60));
  if (!vars.isEmpty()) t += QStringLiteral(" — ") + vars.join(QStringLiteral(", "));
  m_etat->setText(t);
  marquer();
}

void ConsolePython::reprendre(const std::string& choix) {
  if (!m_enCours) return;
  if (choix == "arreter") m_arret = true;
  {
    std::lock_guard<std::mutex> v(m_verrou);
    m_choix = choix;
  }
  m_reveil.notify_all();
  m_ligneArret = 0;
  m_continuer->setEnabled(false);
  m_pas->setEnabled(false);
  marquer();
}

void ConsolePython::fini(prom::ResultatScript r, prom::Projet copie) {
  if (m_fil.joinable()) m_fil.join();
  m_enCours = false;
  m_ligneArret = 0;
  m_resultat = std::move(r);
  m_lancer->setEnabled(true);
  m_continuer->setEnabled(false);
  m_pas->setEnabled(false);
  m_arreter->setEnabled(false);
  if (m_resultat.reussi && m_resultat.modifications > 0 && !m_arret)
    m_atelier.document()->modifier([&copie](prom::Projet& p) { p = std::move(copie); }, q(m_resultat.nomPas()));
  if (!m_resultat.erreurs.empty()) m_sortie->appendPlainText(q(m_resultat.erreurs));
  for (const auto& v : m_resultat.violations)
    m_sortie->appendPlainText(QStringLiteral("%1 %2 : %3").arg(v.erreur ? QStringLiteral("Erreur") : QStringLiteral("Avertissement"), q(v.code), q(v.message)));
  // Courbes du script (promethee.courbe) : tracées à côté de la sortie
  std::vector<TraceCourbes::Courbe> courbes;
  const QColor teintes[] = {QColor(0x5B, 0x4B, 0xF5), QColor(0xD9, 0x48, 0x2B), QColor(0x1E, 0x88, 0x5A), QColor(0xB5, 0x83, 0x0B)};
  QString axeX, axeY;
  for (std::size_t i = 0; i < m_resultat.courbes.size(); ++i) {
    const auto& c = m_resultat.courbes[i];
    courbes.push_back({q(c.nom), c.x, c.y, teintes[i % 4], false, {}});
    if (axeX.isEmpty()) axeX = q(c.axeX), axeY = q(c.axeY);
  }
  if (!courbes.empty()) m_trace->setCourbes(std::move(courbes), axeX, axeY, false);
  if (m_resultat.pythonAbsent) m_etat->setText(QStringLiteral("Python 3 n’est pas installé sur ce poste."));
  else if (m_arret) m_etat->setText(QStringLiteral("Script arrêté : rien n’est appliqué."));
  else if (!m_resultat.reussi)
    m_etat->setText(m_resultat.ligneErreur > 0 ? QStringLiteral("Erreur ligne %1 : rien n’est appliqué.").arg(m_resultat.ligneErreur) : QStringLiteral("Le script a échoué : rien n’est appliqué."));
  else
    m_etat->setText(m_resultat.modifications > 0 ? QStringLiteral("%1 — un Ctrl+Z les retire.").arg(q(m_resultat.nomPas())) : QStringLiteral("Script terminé, sans modification du projet."));
  marquer();
}

bool ConsolePython::ranger(const QString& nom, const QString& touche) {
  QSettings s;
  QVariantMap v = s.value(QStringLiteral("electronique/scriptsRanges")).toMap();
  v[nom] = QVariantMap{{QStringLiteral("code"), code()}, {QStringLiteral("touche"), touche}};
  s.setValue(QStringLiteral("electronique/scriptsRanges"), v);
  m_ranges->clear();
  m_ranges->addItems(scriptsRanges());
  m_ranges->setCurrentText(nom);
  if (!touche.isEmpty() && m_atelier.fenetrePanneaux()) {
    auto* r = new QShortcut(QKeySequence(touche), m_atelier.fenetrePanneaux());
    r->setContext(Qt::WidgetWithChildrenShortcut);
    const QString c = code();
    connect(r, &QShortcut::activated, this, [this, c] {
      setCode(c);
      executer(false);
    });
  }
  m_etat->setText(QStringLiteral("Script « %1 » rangé%2.").arg(nom, touche.isEmpty() ? QString() : QStringLiteral(" sur %1").arg(touche)));
  return true;
}

QStringList ConsolePython::scriptsRanges() const { return QSettings().value(QStringLiteral("electronique/scriptsRanges")).toMap().keys(); }

// ======================================================================================================================
// 13.4 · Propositions d'un assistant
// ======================================================================================================================
PanneauPropositions::PanneauPropositions(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  setObjectName(QStringLiteral("panneauPropositions"));
  auto* col = new QVBoxLayout(this);
  auto* aide = new QLabel(QStringLiteral("Un assistant relié par MCP (<code>promethee-atelier mcp projet.prom</code>) lit le projet et propose des commandes. Chacune s’accepte ou se refuse ici, une à une ; une acceptation fait un pas d’annulation."));
  aide->setWordWrap(true);
  col->addWidget(aide);
  m_liste = new QListWidget;
  m_liste->setObjectName(QStringLiteral("listePropositions"));
  col->addWidget(m_liste, 1);
  auto* ligne = new QHBoxLayout;
  auto* accepter = new QPushButton(QStringLiteral("Accepter"));
  accepter->setObjectName(QStringLiteral("accepterProposition"));
  auto* refuser = new QPushButton(QStringLiteral("Refuser…"));
  refuser->setObjectName(QStringLiteral("refuserProposition"));
  auto* relire = new QPushButton(QStringLiteral("Relire"));
  ligne->addWidget(accepter);
  ligne->addWidget(refuser);
  ligne->addStretch(1);
  ligne->addWidget(relire);
  col->addLayout(ligne);
  m_etat = new QLabel;
  m_etat->setObjectName(QStringLiteral("etatPropositions"));
  m_etat->setWordWrap(true);
  col->addWidget(m_etat);
  auto choisie = [this]() -> int {
    const QListWidgetItem* it = m_liste->currentItem();
    return it ? it->data(Qt::UserRole).toInt() : -1;
  };
  connect(accepter, &QPushButton::clicked, this, [this, choisie] {
    if (const int n = choisie(); n >= 0) this->accepter(n);
  });
  connect(refuser, &QPushButton::clicked, this, [this, choisie] {
    const int n = choisie();
    if (n < 0) return;
    bool ok = false;
    const QString raison = QInputDialog::getText(this, QStringLiteral("Refuser"), QStringLiteral("Raison (facultative), transmise à l’assistant"), QLineEdit::Normal, {}, &ok);
    if (ok) this->refuser(n, raison);
  });
  connect(relire, &QPushButton::clicked, this, &PanneauPropositions::relire);
  m_veille = new QFileSystemWatcher(this);
  connect(m_veille, &QFileSystemWatcher::fileChanged, this, [this] { QTimer::singleShot(100, this, &PanneauPropositions::relire); });
  connect(m_veille, &QFileSystemWatcher::directoryChanged, this, [this] { QTimer::singleShot(100, this, &PanneauPropositions::relire); });
  connect(m_atelier.document(), &Document::etatChange, this, [this] {
    const QString f = cheminFichier();
    if (!f.isEmpty() && !m_veille->files().contains(f) && QFileInfo::exists(f)) this->relire();
  });
  this->relire();
}

QString PanneauPropositions::cheminFichier() const {
  const QString p = m_atelier.document()->chemin();
  return p.isEmpty() ? QString() : q(prom::cheminPropositions(p.toStdString()));
}

void PanneauPropositions::relire() {
  const QString f = cheminFichier();
  if (f.isEmpty()) {
    m_propositions.clear();
    m_etat->setText(QStringLiteral("Enregistre le projet : l’assistant lit son fichier et écrit ses propositions à côté."));
    majListe();
    return;
  }
  if (!m_veille->files().isEmpty()) m_veille->removePaths(m_veille->files());
  if (!m_veille->directories().isEmpty()) m_veille->removePaths(m_veille->directories());
  m_veille->addPath(QFileInfo(f).absolutePath());
  if (QFileInfo::exists(f)) {
    m_veille->addPath(f);
    try {
      m_propositions = prom::lirePropositions(prom::lireFichier(f.toStdString()));
    } catch (const std::exception& e) {
      m_etat->setText(q(e.what()));
    }
  } else
    m_propositions.clear();
  majListe();
}

void PanneauPropositions::majListe() {
  const int avant = m_liste->currentRow();
  m_liste->clear();
  int attente = 0;
  for (const auto& p : m_propositions) {
    const bool enAttente = p.etat == "en attente";
    attente += enAttente;
    QString t = QStringLiteral("n° %1 · %2 — %3").arg(p.numero).arg(q(p.methode), q(p.raison));
    if (!enAttente) t += QStringLiteral(" [%1]").arg(q(p.etat));
    else if (!p.essai.empty()) t += QStringLiteral(" (essai : %1)").arg(q(p.essai));
    auto* it = new QListWidgetItem(t, m_liste);
    it->setData(Qt::UserRole, p.numero);
    it->setToolTip(q(p.params.dump(2)));
    if (!enAttente) it->setForeground(QColor(0x80, 0x80, 0x80));
  }
  if (avant >= 0 && avant < m_liste->count()) m_liste->setCurrentRow(avant);
  else if (m_liste->count() > 0) m_liste->setCurrentRow(0);
  if (!cheminFichier().isEmpty()) m_etat->setText(QStringLiteral("%1 proposition(s) en attente.").arg(attente));
}

bool PanneauPropositions::ecrire() {
  try {
    prom::ecrireFichierAtomique(cheminFichier().toStdString(), prom::textePropositions(m_propositions));
  } catch (const std::exception& e) {
    m_etat->setText(q(e.what()));
    return false;
  }
  return true;
}

bool PanneauPropositions::accepter(int numero, QString* message) {
  for (auto& p : m_propositions) {
    if (p.numero != numero || p.etat != "en attente") continue;
    Document* doc = m_atelier.document();
    prom::Projet copie = doc->projet();
    const prom::ReponseApi r = prom::accepterProposition(copie, p);
    if (r.reponse.contains("error")) {
      const QString m = q(r.reponse["error"].value("message", std::string("refusée par Prométhée")));
      m_etat->setText(m);
      if (message) *message = m;
      ecrire();
      majListe();
      return false;
    }
    if (r.modifie) doc->modifier([&copie](prom::Projet& x) { x = std::move(copie); }, QStringLiteral("Proposition n° %1 de l’assistant : %2").arg(numero).arg(q(r.nomPas)));
    ecrire();
    majListe();
    if (message) *message = q(p.resultat);
    return true;
  }
  return false;
}

bool PanneauPropositions::refuser(int numero, const QString& raison) {
  for (auto& p : m_propositions)
    if (p.numero == numero && p.etat == "en attente") {
      prom::refuserProposition(p, raison.toStdString());
      ecrire();
      majListe();
      return true;
    }
  return false;
}

// ======================================================================================================================
// 13.15 à 13.17 · Assistant de conception
// ======================================================================================================================
PanneauAssistant::PanneauAssistant(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  setObjectName(QStringLiteral("panneauAssistant"));
  auto* col = new QVBoxLayout(this);
  auto* moteur = new QGroupBox(QStringLiteral("Moteur"));
  auto* fm = new QFormLayout(moteur);
  m_genre = new QComboBox;
  m_genre->setObjectName(QStringLiteral("genreMoteur"));
  m_genre->addItem(QStringLiteral("Aucun"), QStringLiteral("aucun"));
  m_genre->addItem(QStringLiteral("Modèle local (llama.cpp)"), QStringLiteral("llama.cpp"));
  m_genre->addItem(QStringLiteral("Ollama sur le poste"), QStringLiteral("ollama"));
  m_genre->addItem(QStringLiteral("Programme du poste (question sur l’entrée)"), QStringLiteral("commande"));
  m_programme = new QLineEdit;
  m_programme->setObjectName(QStringLiteral("programmeMoteur"));
  m_programme->setPlaceholderText(QStringLiteral("llama-cli, chemin d’un programme…"));
  m_modele = new QLineEdit;
  m_modele->setObjectName(QStringLiteral("modeleMoteur"));
  m_modele->setPlaceholderText(QStringLiteral("modèle .gguf, ou nom pour Ollama"));
  auto* garder = new QPushButton(QStringLiteral("Garder"));
  garder->setObjectName(QStringLiteral("garderMoteur"));
  fm->addRow(QStringLiteral("Genre"), m_genre);
  fm->addRow(QStringLiteral("Programme"), m_programme);
  fm->addRow(QStringLiteral("Modèle"), m_modele);
  fm->addRow(QString(), garder);
  auto* note = new QLabel(QStringLiteral("Prométhée n’ouvre aucune connexion : le moteur est un programme du poste. Un service en ligne ne passe que par le programme d’accès en ligne, s’il est activé."));
  note->setWordWrap(true);
  fm->addRow(note);
  col->addWidget(moteur);
  auto* memoire = new QGroupBox(QStringLiteral("Mémoire des règles maison"));
  auto* vm = new QVBoxLayout(memoire);
  m_memoire = new QPlainTextEdit;
  m_memoire->setObjectName(QStringLiteral("memoireAssistant"));
  m_memoire->setPlaceholderText(QStringLiteral("Une règle par ligne : « découplage 100 nF au plus près de chaque broche d’alimentation »"));
  m_memoire->setMaximumHeight(90);
  m_suspendue = new QCheckBox(QStringLiteral("Suspendre la mémoire"));
  m_suspendue->setObjectName(QStringLiteral("suspendreMemoire"));
  vm->addWidget(m_memoire);
  vm->addWidget(m_suspendue);
  col->addWidget(memoire);
  m_conversation = new QTextBrowser;
  m_conversation->setObjectName(QStringLiteral("conversationAssistant"));
  col->addWidget(m_conversation, 2);
  m_trace = new TraceCourbes;
  m_trace->setObjectName(QStringLiteral("traceAssistant"));
  m_trace->setMinimumHeight(140);
  m_trace->hide();
  col->addWidget(m_trace, 1);
  m_question = new QLineEdit;
  m_question->setObjectName(QStringLiteral("questionAssistant"));
  m_question->setPlaceholderText(QStringLiteral("Question : « fréquence de coupure de ce filtre ? », « courant de sortie maximal ? »"));
  col->addWidget(m_question);
  auto* boutons = new QHBoxLayout;
  auto bouton = [&](const char* nom, const QString& texte) {
    auto* b = new QPushButton(texte);
    b->setObjectName(QLatin1String(nom));
    boutons->addWidget(b);
    return b;
  };
  auto* demander = bouton("demanderAssistant", QStringLiteral("Demander"));
  auto* expliquer = bouton("expliquerBloc", QStringLiteral("Expliquer ce bloc"));
  auto* simuler = bouton("simulerAssistant", QStringLiteral("Simuler"));
  auto* fiche = bouton("ficheAssistant", QStringLiteral("Fiche…"));
  auto* surFiche = bouton("questionFiche", QStringLiteral("Sur la fiche"));
  col->addLayout(boutons);
  m_ficheLue = new QLabel(QStringLiteral("Aucune fiche technique lue."));
  m_ficheLue->setObjectName(QStringLiteral("ficheLue"));
  col->addWidget(m_ficheLue);
  connect(garder, &QPushButton::clicked, this, [this] {
    prom::MoteurAssistant m = m_moteur;
    m.genre = m_genre->currentData().toString().toStdString();
    m.programme = m_programme->text().trimmed().toStdString();
    m.modele = m_modele->text().trimmed().toStdString();
    setMoteur(m);
  });
  connect(m_memoire, &QPlainTextEdit::textChanged, this, [this] {
    QSettings().setValue(QStringLiteral("assistant/memoire"), q(this->memoire().texte()));
  });
  connect(m_suspendue, &QCheckBox::toggled, this, [](bool oui) { QSettings().setValue(QStringLiteral("assistant/memoireSuspendue"), oui); });
  connect(demander, &QPushButton::clicked, this, [this] {
    if (!m_question->text().trimmed().isEmpty()) poser(m_question->text().trimmed());
  });
  connect(m_question, &QLineEdit::returnPressed, this, [this] {
    if (!m_question->text().trimmed().isEmpty()) poser(m_question->text().trimmed());
  });
  connect(expliquer, &QPushButton::clicked, this, [this] { this->expliquer({}); });
  connect(simuler, &QPushButton::clicked, this, [this] {
    if (!m_question->text().trimmed().isEmpty()) questionSimulation(m_question->text().trimmed());
  });
  connect(fiche, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Fiche technique"), {}, QStringLiteral("PDF (*.pdf)"));
    if (!f.isEmpty()) lireFiche(f);
  });
  connect(surFiche, &QPushButton::clicked, this, [this] {
    if (!m_question->text().trimmed().isEmpty()) questionFiche(m_question->text().trimmed());
  });
  lireReglages();
}

void PanneauAssistant::lireReglages() {
  QSettings s;
  const J j = J::parse(s.value(QStringLiteral("assistant/moteur")).toString().toStdString(), nullptr, false);
  m_moteur = j.is_object() ? prom::moteurDepuisJson(j) : prom::MoteurAssistant{};
  m_genre->setCurrentIndex(std::max(0, m_genre->findData(q(m_moteur.genre))));
  m_programme->setText(q(m_moteur.programme));
  m_modele->setText(q(m_moteur.modele));
  const QSignalBlocker b(m_memoire);
  m_memoire->setPlainText(s.value(QStringLiteral("assistant/memoire")).toString());
  m_suspendue->setChecked(s.value(QStringLiteral("assistant/memoireSuspendue"), false).toBool());
}

void PanneauAssistant::setMoteur(const prom::MoteurAssistant& m) {
  m_moteur = m;
  QSettings().setValue(QStringLiteral("assistant/moteur"), q(prom::moteurVersJson(m).dump()));
  m_genre->setCurrentIndex(std::max(0, m_genre->findData(q(m.genre))));
  m_programme->setText(q(m.programme));
  m_modele->setText(q(m.modele));
}

prom::MemoireAssistant PanneauAssistant::memoire() const {
  prom::MemoireAssistant m;
  m.lire(m_memoire->toPlainText().toStdString());
  m.suspendue = m_suspendue->isChecked();
  return m;
}

void PanneauAssistant::setMemoire(const prom::MemoireAssistant& m) {
  m_memoire->setPlainText(q(m.texte()));
  m_suspendue->setChecked(m.suspendue);
}

void PanneauAssistant::ajouter(const QString& qui, const QString& html) {
  m_texte += QStringLiteral("<p><b>%1</b> · %2</p>").arg(qui, html);
  m_conversation->setHtml(m_texte);
  m_conversation->moveCursor(QTextCursor::End);
}

QString PanneauAssistant::conversation() const { return m_conversation->toPlainText(); }

namespace {
QString enHtml(const std::string& t) { return h(t).replace(QLatin1Char('\n'), QStringLiteral("<br>")); }
prom::ReponseAssistant pasDeMoteur() {
  prom::ReponseAssistant r;
  r.erreur = "Aucun moteur réglé : choisis un modèle local (llama.cpp), Ollama ou un programme du poste.";
  return r;
}
}  // namespace

prom::ReponseAssistant PanneauAssistant::expliquer(const std::set<std::string>& composants) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  std::set<std::string> ks = composants;
  if (ks.empty() && m_atelier.selection()) ks = m_atelier.selection()->composants();
  prom::ReponseAssistant r;
  if (!c || ks.empty()) {
    r.erreur = "Choisis d'abord les composants du bloc à expliquer.";
    ajouter(QStringLiteral("Assistant"), enHtml(r.erreur));
    return r;
  }
  QStringList refs;
  for (const auto& id : ks)
    if (const ComposantElec* k = c->composant(id)) refs << q(k->reference);
  refs.sort();
  ajouter(QStringLiteral("Toi"), QStringLiteral("Expliquer ce bloc : %1").arg(refs.join(QStringLiteral(", ")).toHtmlEscaped()));
  if (!m_moteur.configure()) {
    r = pasDeMoteur();
    ajouter(QStringLiteral("Assistant"), enHtml(r.erreur));
    return r;
  }
  const std::string invite = prom::inviteExpliquerBloc(*c, ks, memoire());
  const prom::MoteurAssistant m = m_moteur;
  r = surUnFil([m, invite] { return prom::demander(m, prom::systemeAssistant(), invite); });
  ajouter(QStringLiteral("Assistant"), enHtml(r.ok ? r.texte : r.erreur));
  return r;
}

prom::ReponseAssistant PanneauAssistant::demander(const Violation& v) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  ajouter(QStringLiteral("Toi"), QStringLiteral("Que faire de %1 : %2 ?").arg(q(v.code), h(v.message)));
  prom::ReponseAssistant r;
  if (!c) {
    r.erreur = "Aucune conception ouverte.";
  } else if (!m_moteur.configure()) {
    r = pasDeMoteur();
  } else {
    const std::string invite = prom::inviteViolation(*c, v, memoire());
    const prom::MoteurAssistant m = m_moteur;
    r = surUnFil([m, invite] { return prom::demander(m, prom::systemeAssistant(), invite); });
  }
  ajouter(QStringLiteral("Assistant"), enHtml(r.ok ? r.texte + "\n(Une correction reste une proposition : elle s'accepte une à une.)" : r.erreur));
  return r;
}

prom::ReponseAssistant PanneauAssistant::poser(const QString& question) {
  ajouter(QStringLiteral("Toi"), question.toHtmlEscaped());
  prom::ReponseAssistant r;
  if (!m_moteur.configure()) {
    r = pasDeMoteur();
    ajouter(QStringLiteral("Assistant"), enHtml(r.erreur));
    return r;
  }
  std::string contexte;
  if (const ConceptionElec* c = m_atelier.conceptionCourante()) {
    std::set<std::string> tous;
    for (const auto& k : c->composants) tous.insert(k.uuid);
    if (tous.size() <= 60) contexte = prom::contexteBloc(*c, tous);
  }
  const prom::MemoireAssistant mem = memoire();
  std::string invite;
  if (!mem.suspendue && !mem.regles.empty()) {
    invite += "Règles maison :\n";
    for (const auto& x : mem.regles) invite += "- " + x + "\n";
  }
  if (!contexte.empty()) invite += "Conception :\n" + contexte + "\n";
  invite += "Question : " + question.toStdString();
  const prom::MoteurAssistant m = m_moteur;
  r = surUnFil([m, invite] { return prom::demander(m, prom::systemeAssistant(), invite); });
  ajouter(QStringLiteral("Assistant"), enHtml(r.ok ? r.texte : r.erreur));
  return r;
}

bool PanneauAssistant::lireFiche(const QString& pdf) {
  std::string erreur, contenu;
  try {
    contenu = prom::lireFichier(pdf.toStdString());
  } catch (const std::exception& e) {
    m_ficheLue->setText(QStringLiteral("Fiche illisible : %1").arg(q(e.what())));
    return false;
  }
  const std::string nom = QFileInfo(pdf).fileName().toStdString();
  auto f = surUnFil([&contenu, nom, &erreur] { return prom::lireFicheTechnique(contenu, nom, &erreur); });
  if (!f) {
    m_ficheLue->setText(QStringLiteral("Fiche illisible : %1").arg(q(erreur)));
    return false;
  }
  m_fiche = std::move(f);
  m_ficheLue->setText(QStringLiteral("Fiche « %1 » : %2 page(s).").arg(q(m_fiche->nom)).arg(m_fiche->pages.size()));
  return true;
}

prom::ReponseFiche PanneauAssistant::questionFiche(const QString& question) {
  ajouter(QStringLiteral("Toi"), QStringLiteral("Sur la fiche : %1").arg(question.toHtmlEscaped()));
  if (!m_fiche) {
    ajouter(QStringLiteral("Assistant"), QStringLiteral("Lis d’abord une fiche technique (bouton Fiche…)."));
    return {};
  }
  const prom::FicheTechnique f = *m_fiche;
  const prom::MoteurAssistant m = m_moteur;
  const std::string qst = question.toStdString();
  const prom::ReponseFiche r = surUnFil([f, m, qst] { return prom::repondreSurFiche(f, qst, m.configure() ? &m : nullptr); });
  if (!r.trouvee) ajouter(QStringLiteral("Assistant"), QStringLiteral("La fiche ne le dit pas dans les passages retrouvés."));
  else
    ajouter(QStringLiteral("Assistant"), enHtml(r.texte) + QStringLiteral("<br><i>Page %1 : « %2 »</i>").arg(r.extrait.page).arg(h(r.extrait.passage.substr(0, 300))));
  return r;
}

prom::ReponseSimulation PanneauAssistant::questionSimulation(const QString& question) {
  ajouter(QStringLiteral("Toi"), question.toHtmlEscaped());
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) {
    ajouter(QStringLiteral("Assistant"), QStringLiteral("Aucune conception ouverte."));
    return {};
  }
  const ConceptionElec copie = *c;
  const std::string qst = question.toStdString();
  const prom::ReponseSimulation r = surUnFil([copie, qst] { return prom::repondreSimulation(copie, qst); });
  QString t = r.ok ? enHtml(r.texte) : enHtml(r.erreur);
  if (!r.journal.empty()) {
    QStringList j;
    for (const auto& x : r.journal) j << h(x);
    t += QStringLiteral("<br><small>%1</small>").arg(j.join(QStringLiteral(" · ")));
  }
  ajouter(QStringLiteral("Assistant"), t);
  if (r.ok && !r.x.empty()) {
    m_trace->setCourbes({{q(r.axeY.empty() ? r.reseau : r.axeY), r.x, r.y, QColor(0x5B, 0x4B, 0xF5), false, {}}}, q(r.axeX), q(r.axeY), r.analyse == "ac");
    m_trace->show();
  }
  return r;
}

// ======================================================================================================================
// 13.11 · Éditeur de requêtes
// ======================================================================================================================
DialogueRequetes::DialogueRequetes(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setObjectName(QStringLiteral("dialogueRequetes"));
  setWindowTitle(QStringLiteral("Requêtes sur les objets"));
  resize(760, 460);
  auto* col = new QVBoxLayout(this);
  auto* aide = new QLabel(QStringLiteral("Le langage des règles : <code>classe == 'Secteur' &amp;&amp; externe == 1</code>, <code>genre == 'piste' &amp;&amp; largeur &lt; 0.2mm</code>. Les objets visés se comptent à la frappe ; une requête invalide est marquée."));
  aide->setWordWrap(true);
  col->addWidget(aide);
  m_champ = new QLineEdit;
  m_champ->setObjectName(QStringLiteral("champRequete"));
  col->addWidget(m_champ);
  m_compte = new QLabel;
  m_compte->setObjectName(QStringLiteral("compteRequete"));
  m_compte->setWordWrap(true);
  col->addWidget(m_compte);
  auto* grille = new QFormLayout;
  m_historique = new QComboBox;
  m_historique->setObjectName(QStringLiteral("historiqueRequetes"));
  m_exemples = new QComboBox;
  m_exemples->setObjectName(QStringLiteral("exemplesRequetes"));
  for (const auto& [r, d] : exemplesRequetes()) m_exemples->addItem(q(d), q(r));
  grille->addRow(QStringLiteral("Récentes"), m_historique);
  grille->addRow(QStringLiteral("Exemples"), m_exemples);
  col->addLayout(grille);
  m_favoris = new QListWidget;
  m_favoris->setObjectName(QStringLiteral("favorisRequetes"));
  m_favoris->setMaximumHeight(110);
  col->addWidget(new QLabel(QStringLiteral("Favoris")));
  col->addWidget(m_favoris);
  auto* ligne = new QHBoxLayout;
  auto* favori = new QPushButton(QStringLiteral("★ Favori"));
  favori->setObjectName(QStringLiteral("favoriRequete"));
  auto* choisir = new QPushButton(QStringLiteral("Sélectionner"));
  choisir->setObjectName(QStringLiteral("selectionnerRequete"));
  auto* regle = new QPushButton(QStringLiteral("Créer une règle d’isolement…"));
  regle->setObjectName(QStringLiteral("regleRequete"));
  auto* fermer = new QPushButton(QStringLiteral("Fermer"));
  ligne->addWidget(favori);
  ligne->addWidget(choisir);
  ligne->addWidget(regle);
  ligne->addStretch(1);
  ligne->addWidget(fermer);
  col->addLayout(ligne);
  m_memo.lire(QSettings().value(QStringLiteral("electronique/requetes")).toString().toStdString());
  majListes();
  connect(m_champ, &QLineEdit::textChanged, this, [this] { compter(); });
  connect(m_champ, &QLineEdit::returnPressed, this, [this] {
    if (!m_valide) return;
    m_memo.noter(requete().toStdString());
    QSettings().setValue(QStringLiteral("electronique/requetes"), q(m_memo.texte()));
    majListes();
  });
  connect(m_historique, &QComboBox::activated, this, [this](int i) { setRequete(m_historique->itemText(i)); });
  connect(m_exemples, &QComboBox::activated, this, [this](int i) { setRequete(m_exemples->itemData(i).toString()); });
  connect(m_favoris, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) { setRequete(it->text()); });
  connect(favori, &QPushButton::clicked, this, [this] {
    if (requete().isEmpty()) return;
    m_memo.basculerFavori(requete().toStdString());
    QSettings().setValue(QStringLiteral("electronique/requetes"), q(m_memo.texte()));
    majListes();
  });
  connect(choisir, &QPushButton::clicked, this, &DialogueRequetes::selectionner);
  connect(regle, &QPushButton::clicked, this, [this] {
    if (!m_valide) return;
    bool ok = false;
    const QString nom = QInputDialog::getText(this, QStringLiteral("Règle"), QStringLiteral("Nom de la règle"), QLineEdit::Normal, QStringLiteral("Isolement de la requête"), &ok);
    if (!ok) return;
    const QString autre = QInputDialog::getText(this, QStringLiteral("Règle"), QStringLiteral("Face à quels objets ? (requête ; vide : tout le reste)"), QLineEdit::Normal, {}, &ok);
    if (!ok) return;
    const double mm = QInputDialog::getDouble(this, QStringLiteral("Règle"), QStringLiteral("Isolement minimal (mm)"), 3.0, 0.05, 50.0, 2, &ok);
    if (!ok) return;
    const bool ext = QMessageBox::question(this, QStringLiteral("Règle"), QStringLiteral("Sur les couches extérieures seulement ?")) == QMessageBox::Yes;
    creerRegle(nom, autre, mm, ext);
  });
  connect(fermer, &QPushButton::clicked, this, &QDialog::close);
  compter();
}

void DialogueRequetes::majListes() {
  m_historique->clear();
  m_historique->addItems(listeDe(m_memo.recentes));
  m_favoris->clear();
  m_favoris->addItems(listeDe(m_memo.favoris));
}

void DialogueRequetes::setRequete(const QString& r) { m_champ->setText(r); }
QString DialogueRequetes::requete() const { return m_champ->text().trimmed(); }
QString DialogueRequetes::compte() const { return m_compte->text(); }

void DialogueRequetes::compter() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  m_vises = {};
  m_valide = false;
  if (!c || requete().isEmpty()) {
    m_compte->setText(c ? QStringLiteral("Tape une requête.") : QStringLiteral("Aucune conception ouverte."));
    m_champ->setStyleSheet({});
    return;
  }
  std::string erreur;
  m_vises = objetsVises(*c, requete().toStdString(), &erreur);
  m_valide = erreur.empty();
  m_champ->setStyleSheet(m_valide ? QString() : QStringLiteral("QLineEdit { border: 2px solid #B3261E; }"));
  m_compte->setText(m_valide ? QStringLiteral("%1 objet(s) visé(s)%2").arg(m_vises.total()).arg(m_vises.total() ? QStringLiteral(" : ") + q(m_vises.texte()) : QString())
                             : QStringLiteral("Requête invalide : %1").arg(q(erreur)));
}

bool DialogueRequetes::creerRegle(const QString& nom, const QString& autre, double minimumMm, bool exterieures) {
  if (!m_valide) return false;
  // Face à « autre », ou à tout le reste (la négation de la requête)
  const std::string a = requete().toStdString(), b = autre.trimmed().isEmpty() ? "!(" + a + ")" : autre.trimmed().toStdString();
  const Contrainte k = regleDepuisRequetes(nom.toStdString(), GenreContrainte::Isolement, a, b, nm(minimumMm), exterieures ? couchesExternes() : 0);
  const bool ok = m_atelier.modifierConception([&](ConceptionElec& c) { c.circuit.regles.avancees.contraintes.push_back(k); }, QStringLiteral("Règle « %1 »").arg(nom));
  if (ok) {
    m_memo.noter(requete().toStdString());
    QSettings().setValue(QStringLiteral("electronique/requetes"), q(m_memo.texte()));
    majListes();
    m_compte->setText(m_compte->text() + QStringLiteral(" — règle « %1 » ajoutée aux contraintes de la carte.").arg(nom));
  }
  return ok;
}

void DialogueRequetes::selectionner() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c || !m_valide || !m_atelier.selection()) return;
  std::vector<SelectionElectronique::Ref> refs;
  for (const auto& id : m_vises.uuids) {
    SelectionElectronique::Ref r;
    r.id = id;
    bool trouve = false;
    for (const auto& p : c->circuit.pistes)
      if (p.uuid == id) r.genre = SelectionElectronique::Genre::Piste, trouve = true;
    for (const auto& v : c->circuit.vias)
      if (!trouve && v.uuid == id) r.genre = SelectionElectronique::Genre::Via, trouve = true;
    for (const auto& z : c->circuit.zones)
      if (!trouve && z.uuid == id) r.genre = SelectionElectronique::Genre::Zone, trouve = true;
    if (!trouve && c->composant(id)) r.genre = SelectionElectronique::Genre::Empreinte, r.composant = id, trouve = true;
    if (trouve) refs.push_back(r);
  }
  m_atelier.setMode(AtelierElectronique::Circuit);
  m_atelier.selection()->choisir(refs);
}

// ======================================================================================================================
// 13.14 · Conception guidée
// ======================================================================================================================
DialogueConceptionGuidee::DialogueConceptionGuidee(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setObjectName(QStringLiteral("dialogueConceptionGuidee"));
  setWindowTitle(QStringLiteral("Conception guidée par modules"));
  resize(720, 560);
  auto* col = new QVBoxLayout(this);
  auto* f = new QFormLayout;
  m_largeur = new QDoubleSpinBox;
  m_largeur->setObjectName(QStringLiteral("largeurGuidee"));
  m_largeur->setRange(20, 500);
  m_largeur->setValue(50);
  m_largeur->setSuffix(QStringLiteral(" mm"));
  m_hauteur = new QDoubleSpinBox;
  m_hauteur->setObjectName(QStringLiteral("hauteurGuidee"));
  m_hauteur->setRange(20, 500);
  m_hauteur->setValue(30);
  m_hauteur->setSuffix(QStringLiteral(" mm"));
  m_module = new QComboBox;
  m_module->setObjectName(QStringLiteral("moduleGuide"));
  for (const auto& m : galerieModules()) m_module->addItem(q(m.nom), q(m.id));
  f->addRow(QStringLiteral("Largeur de la carte"), m_largeur);
  f->addRow(QStringLiteral("Hauteur de la carte"), m_hauteur);
  f->addRow(QStringLiteral("Module"), m_module);
  auto* interfaces = new QVBoxLayout;
  for (const auto& [id, description] : interfacesGuidees()) {
    auto* c = new QCheckBox(q(description));
    c->setObjectName(QStringLiteral("interface_") + q(id));
    c->setChecked(id == "usb");
    interfaces->addWidget(c);
    m_interfaces.emplace_back(id, c);
  }
  f->addRow(QStringLiteral("Interfaces"), interfaces);
  col->addLayout(f);
  auto* concevoir = new QPushButton(QStringLiteral("Concevoir la carte"));
  concevoir->setObjectName(QStringLiteral("concevoirGuidee"));
  col->addWidget(concevoir);
  m_journal = new QTextBrowser;
  m_journal->setObjectName(QStringLiteral("journalGuide"));
  col->addWidget(m_journal, 1);
  col->addWidget(new QLabel(QStringLiteral("Circuits types proposés pour la conception ouverte (découplages, tirages, protections)")));
  m_circuits = new QListWidget;
  m_circuits->setObjectName(QStringLiteral("circuitsTypes"));
  m_circuits->setMaximumHeight(140);
  col->addWidget(m_circuits);
  auto* ligne = new QHBoxLayout;
  auto* proposer = new QPushButton(QStringLiteral("Proposer"));
  proposer->setObjectName(QStringLiteral("proposerCircuits"));
  auto* poser = new QPushButton(QStringLiteral("Poser"));
  poser->setObjectName(QStringLiteral("poserCircuit"));
  ligne->addWidget(proposer);
  ligne->addWidget(poser);
  ligne->addStretch(1);
  col->addLayout(ligne);
  connect(concevoir, &QPushButton::clicked, this, [this] { this->concevoir(); });
  connect(proposer, &QPushButton::clicked, this, [this] { this->proposer(); });
  connect(poser, &QPushButton::clicked, this, [this] {
    if (const int i = m_circuits->currentRow(); i >= 0) this->poser(static_cast<std::size_t>(i));
  });
  connect(m_circuits, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { this->poser(static_cast<std::size_t>(m_circuits->currentRow())); });
}

void DialogueConceptionGuidee::regler(double largeur, double hauteur, const std::string& module, const std::vector<std::string>& interfaces) {
  m_largeur->setValue(largeur);
  m_hauteur->setValue(hauteur);
  m_module->setCurrentIndex(std::max(0, m_module->findData(q(module))));
  for (auto& [id, c] : m_interfaces) c->setChecked(std::find(interfaces.begin(), interfaces.end(), id) != interfaces.end());
}

QString DialogueConceptionGuidee::journal() const { return m_journal->toPlainText(); }

ResultatGuide DialogueConceptionGuidee::concevoir() {
  DemandeGuidee d;
  d.largeur = m_largeur->value();
  d.hauteur = m_hauteur->value();
  d.module = m_module->currentData().toString().toStdString();
  d.interfaces.clear();
  for (const auto& [id, c] : m_interfaces)
    if (c->isChecked()) d.interfaces.push_back(id);
  d.nom = "Carte " + m_module->currentText().toStdString();
  ResultatGuide r = surUnFil([d] { return concevoirParModules(d); });
  QString t;
  for (const auto& x : r.refus) t += QStringLiteral("<p style='color:#B3261E'>%1</p>").arg(h(x));
  for (const auto& x : r.journal) t += QStringLiteral("<p>%1</p>").arg(h(x));
  for (const auto& x : r.violations) t += QStringLiteral("<p style='color:#B3261E'>%1</p>").arg(h(x));
  if (r.refus.empty()) {
    Document* doc = m_atelier.document();
    const ConceptionElec c = r.conception;
    doc->modifier([&c](prom::Projet& p) { p.electronique.push_back(c); }, QStringLiteral("Conception guidée : %1").arg(q(c.nom)));
    m_atelier.ouvrir(static_cast<int>(doc->projet().electronique.size()) - 1, AtelierElectronique::Circuit);
    t += QStringLiteral("<p><b>%1</b> : la carte est ouverte ; elle se modifie comme toute carte, et la vue 3D la montre.</p>")
             .arg(r.reussi() ? QStringLiteral("ERC et DRC sans erreur") : QStringLiteral("Erreurs à reprendre"));
  }
  m_journal->setHtml(t);
  return r;
}

std::vector<PropositionCircuitType> DialogueConceptionGuidee::proposer() {
  m_propositions.clear();
  m_circuits->clear();
  if (const ConceptionElec* c = m_atelier.conceptionCourante()) m_propositions = circuitsTypes(*c);
  for (const auto& p : m_propositions) m_circuits->addItem(q(p.description));
  if (m_propositions.empty()) m_circuits->addItem(QStringLiteral("Rien à proposer : découplages, tirages et protections sont là."));
  else m_circuits->setCurrentRow(0);
  return m_propositions;
}

bool DialogueConceptionGuidee::poser(std::size_t i) {
  if (i >= m_propositions.size()) return false;
  const PropositionCircuitType p = m_propositions[i];
  std::string erreur;
  std::vector<std::string> poses;
  const bool ok = m_atelier.modifierConception([&](ConceptionElec& c) { poses = poserCircuitType(c, p, &erreur); }, QStringLiteral("Circuit type : %1").arg(q(p.description)));
  if (!ok || poses.empty()) {
    m_journal->append(QStringLiteral("<p style='color:#B3261E'>%1</p>").arg(h(erreur.empty() ? std::string("Rien n'a été posé.") : erreur)));
    return false;
  }
  m_journal->append(QStringLiteral("<p>Posé : %1 — à annoter et placer.</p>").arg(h(p.description)));
  proposer();
  return true;
}

// ======================================================================================================================
// Atelier : panneaux, dialogues, commandes
// ======================================================================================================================
namespace {
QDockWidget* dock(AtelierElectronique& a, QPointer<QDockWidget>& d, const char* nom, const QString& titre, QWidget* contenu, Qt::DockWidgetArea zone) {
  if (!d) {
    d = new QDockWidget(titre, a.fenetrePanneaux());
    d->setObjectName(QLatin1String(nom));
    d->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    d->setWidget(contenu);
    a.fenetrePanneaux()->addDockWidget(zone, d);
  } else
    delete contenu;
  d->show();
  d->raise();
  return d;
}
}  // namespace

ConsolePython* AtelierElectronique::consolePython() {
  if (m_dockPython) {
    m_dockPython->show();
    m_dockPython->raise();
    return qobject_cast<ConsolePython*>(m_dockPython->widget());
  }
  return qobject_cast<ConsolePython*>(dock(*this, m_dockPython, "dockPython", QStringLiteral("Python"), new ConsolePython(*this), Qt::BottomDockWidgetArea)->widget());
}

PanneauAssistant* AtelierElectronique::assistant() {
  if (m_dockAssistant) {
    m_dockAssistant->show();
    m_dockAssistant->raise();
    return qobject_cast<PanneauAssistant*>(m_dockAssistant->widget());
  }
  return qobject_cast<PanneauAssistant*>(dock(*this, m_dockAssistant, "dockAssistant", QStringLiteral("Assistant"), new PanneauAssistant(*this), Qt::RightDockWidgetArea)->widget());
}

PanneauPropositions* AtelierElectronique::propositions() {
  if (m_dockPropositions) {
    m_dockPropositions->show();
    m_dockPropositions->raise();
    auto* p = qobject_cast<PanneauPropositions*>(m_dockPropositions->widget());
    if (p) p->relire();
    return p;
  }
  return qobject_cast<PanneauPropositions*>(dock(*this, m_dockPropositions, "dockPropositions", QStringLiteral("Propositions"), new PanneauPropositions(*this), Qt::RightDockWidgetArea)->widget());
}

DialogueRequetes* AtelierElectronique::requetes() {
  if (!m_requetes) m_requetes = new DialogueRequetes(*this, this);
  m_requetes->show();
  m_requetes->raise();
  return m_requetes;
}

DialogueConceptionGuidee* AtelierElectronique::conceptionGuidee() {
  if (!m_conceptionGuidee) m_conceptionGuidee = new DialogueConceptionGuidee(*this, this);
  m_conceptionGuidee->show();
  m_conceptionGuidee->raise();
  return m_conceptionGuidee;
}

void AtelierElectronique::demanderAssistant(const Violation& v) { assistant()->demander(v); }

void AtelierElectronique::enregistrerCommandesL13() {
  auto& c = m_commandes;
  c["automatisation.ligne"] = [this] { m_ligneCommandes->activer(); };
  c["automatisation.macro"] = [this] { m_ligneCommandes->setEnregistrement(!m_ligneCommandes->enregistrement()); };
  c["automatisation.rejouer"] = [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Rejouer une macro"), {}, QStringLiteral("Macro Prométhée (*.prom-macro *.txt);;Tous les fichiers (*)"));
    if (!f.isEmpty()) m_ligneCommandes->rejouerFichier(f);
  };
  c["automatisation.python"] = [this] { consolePython(); };
  c["assistant.ouvrir"] = [this] { assistant(); };
  c["assistant.expliquer"] = [this] { assistant()->expliquer({}); };
  c["assistant.propositions"] = [this] { propositions(); };
  c["assistant.fiche"] = [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Fiche technique"), {}, QStringLiteral("PDF (*.pdf)"));
    if (!f.isEmpty()) assistant()->lireFiche(f);
  };
  c["regles.requetes"] = [this] { requetes(); };
  c["conception.guidee"] = [this] { conceptionGuidee(); };
  c["conception.circuitsTypes"] = [this] { conceptionGuidee()->proposer(); };
  c["composant.brochage"] = [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Brochage"), {}, QStringLiteral("BSDL ou tableau (*.bsd *.bsdl *.csv *.txt);;Tous les fichiers (*)"));
    if (f.isEmpty()) return;
    std::string texte, erreur;
    try {
      texte = prom::lireFichier(f.toStdString());
    } catch (const std::exception& e) {
      afficherMessage(q(e.what()).toHtmlEscaped(), NiveauMessage::Erreur);
      return;
    }
    const bool bsdl = f.endsWith(QLatin1String(".bsd"), Qt::CaseInsensitive) || f.endsWith(QLatin1String(".bsdl"), Qt::CaseInsensitive) || texte.find("entity") != std::string::npos;
    const std::optional<Brochage> b = bsdl ? lireBsdl(texte, &erreur) : lireTableauBrochage(texte, &erreur);
    if (!b) {
      afficherMessage(QStringLiteral("Brochage illisible : %1").arg(h(erreur)), NiveauMessage::Erreur);
      return;
    }
    const DefSymbole s = symboleDepuisBrochage(*b, b->entite.empty() ? QFileInfo(f).completeBaseName().toStdString() : b->entite);
    afficherMessage(QStringLiteral("Brochage lu : %1 broche(s)%2 ; le composant s’ouvre pour être vérifié.").arg(b->broches.size()).arg(b->alertes.empty() ? QString() : QStringLiteral(", %1 alerte(s)").arg(b->alertes.size())),
                    NiveauMessage::Succes);
    creerComposant(&s);
  };
}

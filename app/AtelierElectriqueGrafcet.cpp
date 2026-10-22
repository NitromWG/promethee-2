// Prométhée : atelier électrique — Grafcet (NF EN CEI 60848). Au centre le dessin normalisé ; à gauche les grafcets
// du projet ; à droite ce qui est choisi : l'étape (numéro, initiale, actions), la transition (réceptivité lue au fil
// de la frappe, étapes amont et aval), sinon le grafcet et sa vérification. La simulation marque les étapes actives
// d'un jeton ; les entrées se cochent, le temps avance d'un clic. Le folio Grafcet et le texte structuré suivent le
// grafcet. Licence GPL-3.0-only.
#include <algorithm>
#include <sstream>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QRegularExpression>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "AtelierElectrique.hpp"
#include "EtiquetteEtat.hpp"
#include "FormulaireLie.hpp"
#include "Icones.hpp"
#include "VueGrafcet.hpp"
#include "VueSchema.hpp"
#include "promethee/electrique_grafcet.hpp"

using namespace prom::elec;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }
QString h(const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); }
std::string s(const QString& t) { return t.trimmed().toStdString(); }

// Ce qui fait le grafcet : la simulation repart quand il change
std::string signature(const prom::elec::Grafcet& g) {
  std::ostringstream o;
  for (const auto& e : g.etapes) {
    o << 'E' << e.numero << (e.initiale ? "i" : "");
    for (const auto& a : e.actions) o << '|' << texteAction(a);
    o << ';';
  }
  for (const auto& t : g.transitions) {
    o << 'T';
    for (int a : t.amont) o << a << ',';
    o << '>';
    for (int b : t.aval) o << b << ',';
    o << t.receptivite << ';';
  }
  return o.str();
}

std::vector<int> lireNumeros(const QString& t) {
  std::vector<int> v;
  for (const QString& x : t.split(QRegularExpression(QStringLiteral("[,; ]+")), Qt::SkipEmptyParts)) {
    bool ok = false;
    const int n = x.trimmed().remove(QLatin1Char('X')).toInt(&ok);
    if (ok && std::find(v.begin(), v.end(), n) == v.end()) v.push_back(n);
  }
  return v;
}

QString ecrireNumeros(const std::vector<int>& v) {
  QStringList l;
  for (int n : v) l << QString::number(n);
  return l.join(QStringLiteral(", "));
}

}  // namespace

// ---------- Construction ----------
QWidget* AtelierElectrique::pageGrafcet() {
  auto* w = new QWidget;
  auto* col = new QVBoxLayout(w);
  col->setContentsMargins(12, 10, 12, 10);
  col->setSpacing(6);
  m_bandeauGrafcet = new QLabel;
  m_bandeauGrafcet->setObjectName(QStringLiteral("bandeauGrafcet"));
  m_bandeauGrafcet->setWordWrap(true);
  col->addWidget(m_bandeauGrafcet);
  m_vueGrafcet = new VueGrafcet;
  m_vueGrafcet->setObjectName(QStringLiteral("vueGrafcet"));
  connect(m_vueGrafcet, &VueGrafcet::choisi, this, [this](int e, int t) { choisirDansGrafcet(e, t); });
  connect(m_vueGrafcet, &VueGrafcet::supprimerDemande, this, [this] { supprimerDansGrafcet(); });
  connect(m_vueGrafcet, &VueGrafcet::doubleClic, this, [this](int e, int t) {
    choisirDansGrafcet(e, t);
    // Double-clic : le champ principal prêt à la frappe (réceptivité, actions)
    if (QWidget* contenu = m_panneau->widget()) {
      QWidget* champ = t >= 0 ? static_cast<QWidget*>(contenu->findChild<QLineEdit*>(QStringLiteral("receptivite")))
                              : static_cast<QWidget*>(contenu->findChild<QPlainTextEdit*>(QStringLiteral("actionsEtape")));
      if (champ) champ->setFocus(Qt::MouseFocusReason);
    }
  });
  col->addWidget(m_vueGrafcet, 1);
  return w;
}

QWidget* AtelierElectrique::gaucheGrafcet() {
  auto* w = new QWidget;
  auto* col = new QVBoxLayout(w);
  col->setContentsMargins(8, 6, 6, 6);
  col->setSpacing(4);
  auto* t = new QLabel(QStringLiteral("<span style='font-weight:700; color:#232A45; font-size:13px'>Grafcets</span>"));
  t->setContentsMargins(2, 6, 2, 2);
  col->addWidget(t);
  m_listeGrafcets = new QListWidget;
  m_listeGrafcets->setObjectName(QStringLiteral("listeGrafcets"));
  m_listeGrafcets->setMaximumHeight(170);
  connect(m_listeGrafcets, &QListWidget::currentRowChanged, this, [this](int r) {
    if (r >= 0 && r != m_grafcet) choisirGrafcet(r);
  });
  col->addWidget(m_listeGrafcets);
  auto* nouveau = new QPushButton(icone(QStringLiteral("grafcet")), QStringLiteral("Nouveau grafcet"));
  nouveau->setObjectName(QStringLiteral("nouveauGrafcet"));
  auto* menu = new QMenu(nouveau);
  menu->addAction(icone(QStringLiteral("grafcet")), QStringLiteral("Grafcet vierge"), this, [this] { nouveauGrafcet(); });
  menu->addSeparator();
  for (const auto& [id, nom] : exemplesGrafcet()) {
    const std::string ex = id;
    menu->addAction(icone(QStringLiteral("apprendre")), QStringLiteral("Exemple : %1").arg(q(nom)), this, [this, ex] { nouveauGrafcet(ex); });
  }
  nouveau->setMenu(menu);
  col->addWidget(nouveau);
  auto* aide = new QLabel(QStringLiteral(
      "<span style='color:#6B7393'>Un grafcet décrit ce que fait un automatisme : des <b>étapes</b> (ce que fait la machine, ses actions) et des "
      "<b>transitions</b> (ce qu’elle attend pour passer à la suite, la réceptivité).<br><br>Clique une étape ou une transition pour la modifier à "
      "droite ; double-clic : directement dans son champ ; Suppr la retire (Ctrl+Z la rend).</span>"));
  aide->setWordWrap(true);
  col->addWidget(aide);
  col->addStretch(1);
  return w;
}

// ---------- Accès ----------
const prom::elec::Grafcet* AtelierElectrique::grafcetChoisi() const {
  const ProjetElectrique* pe = projetCourant();
  return pe && m_grafcet >= 0 && m_grafcet < static_cast<int>(pe->grafcets.size()) ? &pe->grafcets[static_cast<std::size_t>(m_grafcet)] : nullptr;
}

void AtelierElectrique::modifierGrafcet(const std::function<void(prom::elec::Grafcet&)>& f) {
  const int i = m_grafcet;
  modifierProjet([&](ProjetElectrique& p) {
    if (i >= 0 && i < static_cast<int>(p.grafcets.size())) f(p.grafcets[static_cast<std::size_t>(i)]);
  });
}

// ---------- Mise à jour ----------
void AtelierElectrique::majGrafcet() {
  const ProjetElectrique* pe = projetCourant();
  if (!pe || !m_vueGrafcet) return;
  const int n = static_cast<int>(pe->grafcets.size());
  if (m_grafcet >= n) m_grafcet = n - 1;
  if (m_grafcet < 0 && n > 0) m_grafcet = 0;
  {
    const QSignalBlocker b(m_listeGrafcets);
    m_listeGrafcets->clear();
    for (const auto& g : pe->grafcets)
      m_listeGrafcets->addItem(new QListWidgetItem(icone(QStringLiteral("grafcet")), QStringLiteral("%1 · %2 étape(s)").arg(q(g.nom)).arg(g.etapes.size())));
    m_listeGrafcets->setCurrentRow(m_grafcet);
  }
  const prom::elec::Grafcet* g = grafcetChoisi();
  // La simulation suit le grafcet : modifié, il repart de sa situation initiale (les entrées gardent leur état)
  if (m_simulation) {
    if (!g) m_simulation.reset();
    else if (signature(*g) != m_signatureSimulation) {
      std::map<std::string, bool> entrees;
      for (const auto& e : entreesGrafcet(*g)) entrees[e] = m_simulation->entree(e);
      m_simulation = std::make_unique<SimulateurGrafcet>(*g);
      for (const auto& [e, v] : entrees) m_simulation->fixer(e, v);
      m_simulation->evoluer();
      m_signatureSimulation = signature(*g);
    }
  }
  if (g && m_etapeGrafcet >= 0 && !g->etape(m_etapeGrafcet)) m_etapeGrafcet = -1;
  if (g && m_transitionGrafcet >= static_cast<int>(g->transitions.size())) m_transitionGrafcet = -1;
  m_vueGrafcet->setGrafcet(g);
  m_vueGrafcet->setChoix(m_etapeGrafcet, m_transitionGrafcet);
  if (m_simulation) {
    const auto f = m_simulation->franchissables();
    m_vueGrafcet->setSimulation(true, m_simulation->actives(), std::set<std::size_t>(f.begin(), f.end()));
  } else {
    m_vueGrafcet->setSimulation(false);
  }
  if (!g) {
    m_bandeauGrafcet->setText(QStringLiteral("<span style='font-size:16px; font-weight:700; color:#10142A'>Grafcet</span><br><span style='color:#6B7393'>Crée un grafcet vierge ou pars d’un "
                                             "exemple de cours (ruban « Nouveau » ou bouton à gauche).</span>"));
    return;
  }
  int erreurs = 0, alertes = 0;
  for (const auto& a : verifierGrafcet(*g)) {
    if (a.niveau == NiveauAvis::Erreur) ++erreurs;
    else if (a.niveau == NiveauAvis::Alerte) ++alertes;
  }
  QStringList entrees, sorties;
  for (const auto& e : entreesGrafcet(*g)) entrees << q(e);
  for (const auto& x : sortiesGrafcet(*g)) sorties << q(x);
  QString etat = erreurs > 0 ? QStringLiteral("<span style='color:#C2402E'>⛔ %1 erreur(s)</span>").arg(erreurs)
                 : alertes > 0 ? QStringLiteral("<span style='color:#A8620E'>⚠ %1 point(s) à voir</span>").arg(alertes)
                               : QStringLiteral("<span style='color:#2E8B57'>✓ conforme à la NF EN CEI 60848</span>");
  QString simulation;
  if (m_simulation) {
    QStringList actives;
    for (int a : m_simulation->actives()) actives << QString::number(a);
    simulation = QStringLiteral("<br><b style='color:#C2402E'>Simulation</b> · t = %1 s · étape(s) active(s) : %2").arg(QLocale().toString(m_simulation->temps(), 'f', 1), actives.join(QStringLiteral(", ")));
  }
  m_bandeauGrafcet->setText(QStringLiteral("<span style='font-size:16px; font-weight:700; color:#10142A'>Grafcet %1</span> · %2 étape(s), %3 transition(s) · %4<br>"
                                           "<span style='color:#6B7393'>Entrées : %5 · Sorties : %6</span>%7")
                                .arg(h(g->nom))
                                .arg(g->etapes.size())
                                .arg(g->transitions.size())
                                .arg(etat, entrees.isEmpty() ? QStringLiteral("aucune") : entrees.join(QStringLiteral(", ")).toHtmlEscaped(),
                                     sorties.isEmpty() ? QStringLiteral("aucune") : sorties.join(QStringLiteral(", ")).toHtmlEscaped(), simulation));
}

// ---------- Actions ----------
int AtelierElectrique::nouveauGrafcet(const std::string& exemple) {
  int i = -1;
  modifierProjet([&](ProjetElectrique& p) {
    prom::elec::Grafcet g = exemple.empty() ? prom::elec::Grafcet{} : exempleGrafcet(exemple);
    if (exemple.empty()) {
      EtapeGrafcet e;
      e.numero = 0;
      e.initiale = true;
      g.etapes.push_back(e);
      int k = 1;
      while (std::any_of(p.grafcets.begin(), p.grafcets.end(), [&](const prom::elec::Grafcet& x) { return x.nom == "G" + std::to_string(k); })) ++k;
      g.nom = "G" + std::to_string(k);
    }
    g.uuid = prom::ecad::nouvelUuid();
    p.grafcets.push_back(g);
    i = static_cast<int>(p.grafcets.size()) - 1;
  });
  m_grafcet = i;
  m_etapeGrafcet = exemple.empty() ? 0 : -1;
  m_transitionGrafcet = -1;
  m_simulation.reset();
  if (mode() != Grafcet) choisirMode(Grafcet);
  majGrafcet();
  majPanneau();
  m_etat->setText(exemple.empty() ? QStringLiteral("Grafcet vierge : l’étape initiale 0 est posée. « Étape suivante » ajoute l’étape 1 et sa transition.")
                                  : QStringLiteral("Exemple chargé : « Simuler » au ruban, puis coche les entrées pour voir les étapes s’enchaîner."));
  return i;
}

void AtelierElectrique::choisirGrafcet(int indice) {
  m_grafcet = indice;
  m_etapeGrafcet = -1;
  m_transitionGrafcet = -1;
  m_simulation.reset();
  majGrafcet();
  majPanneau();
}

void AtelierElectrique::choisirDansGrafcet(int etape, int transition) {
  m_etapeGrafcet = transition >= 0 ? -1 : etape;
  m_transitionGrafcet = transition;
  if (m_vueGrafcet) m_vueGrafcet->setChoix(m_etapeGrafcet, m_transitionGrafcet);
  majPanneau();
}

int AtelierElectrique::etapeSuivanteGrafcet() {
  const prom::elec::Grafcet* g = grafcetChoisi();
  if (!g) return nouveauGrafcet() >= 0 ? 0 : -1;
  const int n = g->numeroLibre();
  int depuis = m_etapeGrafcet;
  const int tr = m_transitionGrafcet;
  if (depuis < 0 && tr < 0)
    for (const auto& e : g->etapes) depuis = std::max(depuis, e.numero);  // rien de choisi : après la dernière étape
  int nouvelle = -1;
  modifierGrafcet([&](prom::elec::Grafcet& x) {
    EtapeGrafcet e;
    e.numero = n;
    x.etapes.push_back(e);
    if (tr >= 0 && tr < static_cast<int>(x.transitions.size())) {
      // Après une transition : elle mène à la nouvelle étape, qui mène où elle menait
      TransitionGrafcet& t = x.transitions[static_cast<std::size_t>(tr)];
      const std::vector<int> ancien = t.aval;
      t.aval = {n};
      if (!ancien.empty()) x.transitions.push_back({{n}, ancien, "1", {}});
    } else if (depuis >= 0) {
      // Après une étape : ce qui la quittait part maintenant de la nouvelle étape (insertion dans la séquence)
      for (auto& t : x.transitions)
        if (t.amont.size() == 1 && t.amont.front() == depuis) t.amont.front() = n;
      x.transitions.push_back({{depuis}, {n}, "1", {}});
      nouvelle = static_cast<int>(x.transitions.size()) - 1;
    }
  });
  // La transition qui vient d'être créée est choisie : sa réceptivité est à écrire
  if (nouvelle >= 0) choisirDansGrafcet(-1, nouvelle);
  else choisirDansGrafcet(n, -1);
  majGrafcet();
  if (nouvelle >= 0)
    if (QWidget* contenu = m_panneau->widget())
      if (auto* r = contenu->findChild<QLineEdit*>(QStringLiteral("receptivite"))) {
        r->setFocus(Qt::OtherFocusReason);
        r->selectAll();
      }
  m_etat->setText(QStringLiteral("Étape %1 ajoutée, avec sa transition : écris ce qu’elle attend (la réceptivité), puis donne ses actions à l’étape.").arg(n));
  return n;
}

void AtelierElectrique::brancheGrafcet(bool et) {
  const prom::elec::Grafcet* g = grafcetChoisi();
  if (!g) return;
  const int n = g->numeroLibre();
  if (!et) {
    const int s0 = m_etapeGrafcet;
    if (s0 < 0) {
      m_etat->setText(QStringLiteral("Choisis d’abord l’étape d’où part le choix (divergence en OU)."));
      return;
    }
    int ouverte = -1;
    modifierGrafcet([&](prom::elec::Grafcet& x) {
      std::vector<int> rejoint;
      for (const auto& t : x.transitions)
        if (t.amont.size() == 1 && t.amont.front() == s0) {
          rejoint = t.aval;
          break;
        }
      EtapeGrafcet e;
      e.numero = n;
      x.etapes.push_back(e);
      x.transitions.push_back({{s0}, {n}, "1", {}});
      ouverte = static_cast<int>(x.transitions.size()) - 1;
      if (!rejoint.empty()) x.transitions.push_back({{n}, rejoint, "1", {}});
    });
    choisirDansGrafcet(-1, ouverte);
    majGrafcet();
    m_etat->setText(QStringLiteral("Branche en OU depuis l’étape %1 : écris sa réceptivité, exclusive de celle de l’autre branche (« a » et « /a »).").arg(s0));
    return;
  }
  const int t0 = m_transitionGrafcet;
  if (t0 < 0) {
    m_etat->setText(QStringLiteral("Choisis d’abord la transition qui lance les séquences en parallèle (divergence en ET)."));
    return;
  }
  modifierGrafcet([&](prom::elec::Grafcet& x) {
    if (t0 >= static_cast<int>(x.transitions.size())) return;
    const std::vector<int> aval = x.transitions[static_cast<std::size_t>(t0)].aval;
    EtapeGrafcet e;
    e.numero = n;
    x.etapes.push_back(e);
    x.transitions[static_cast<std::size_t>(t0)].aval.push_back(n);
    // La séquence parallèle rejoint la première à sa transition suivante (convergence en ET)
    if (!aval.empty())
      for (auto& t : x.transitions)
        if (std::find(t.amont.begin(), t.amont.end(), aval.front()) != t.amont.end()) {
          t.amont.push_back(n);
          break;
        }
  });
  choisirDansGrafcet(n, -1);
  majGrafcet();
  m_etat->setText(QStringLiteral("Étape %1 en parallèle (divergence en ET) : donne-lui ses actions ; les deux séquences se rejoignent à la transition suivante.").arg(n));
}

void AtelierElectrique::bouclerGrafcet(int vers) {
  const prom::elec::Grafcet* g = grafcetChoisi();
  const int s0 = m_etapeGrafcet;
  if (!g || s0 < 0 || !g->etape(vers)) {
    m_etat->setText(QStringLiteral("Choisis d’abord l’étape d’où part le retour."));
    return;
  }
  int t = -1;
  modifierGrafcet([&](prom::elec::Grafcet& x) {
    x.transitions.push_back({{s0}, {vers}, "1", {}});
    t = static_cast<int>(x.transitions.size()) - 1;
  });
  choisirDansGrafcet(-1, t);
  majGrafcet();
  m_etat->setText(QStringLiteral("Retour de l’étape %1 vers l’étape %2 : écris ce qui le déclenche.").arg(s0).arg(vers));
}

void AtelierElectrique::supprimerDansGrafcet() {
  const prom::elec::Grafcet* g = grafcetChoisi();
  if (!g) return;
  const int e0 = m_etapeGrafcet, t0 = m_transitionGrafcet;
  if (t0 >= 0) {
    modifierGrafcet([&](prom::elec::Grafcet& x) {
      if (t0 < static_cast<int>(x.transitions.size())) x.transitions.erase(x.transitions.begin() + t0);
    });
    m_etat->setText(QStringLiteral("Transition %1 supprimée — Ctrl+Z la rend.").arg(t0 + 1));
  } else if (e0 >= 0) {
    modifierGrafcet([&](prom::elec::Grafcet& x) {
      std::vector<std::size_t> entrent, sortent;
      for (std::size_t i = 0; i < x.transitions.size(); ++i) {
        const auto& t = x.transitions[i];
        if (std::find(t.aval.begin(), t.aval.end(), e0) != t.aval.end()) entrent.push_back(i);
        if (std::find(t.amont.begin(), t.amont.end(), e0) != t.amont.end()) sortent.push_back(i);
      }
      // Au milieu d'une séquence : la transition d'avant mène là où menait celle d'après
      if (entrent.size() == 1 && sortent.size() == 1 && entrent.front() != sortent.front() && x.transitions[sortent.front()].amont.size() == 1) {
        auto& avant = x.transitions[entrent.front()].aval;
        const std::vector<int> suite = x.transitions[sortent.front()].aval;
        avant.erase(std::remove(avant.begin(), avant.end(), e0), avant.end());
        for (int s2 : suite)
          if (std::find(avant.begin(), avant.end(), s2) == avant.end()) avant.push_back(s2);
        x.transitions.erase(x.transitions.begin() + static_cast<std::ptrdiff_t>(sortent.front()));
      } else {
        for (auto& t : x.transitions) {
          t.amont.erase(std::remove(t.amont.begin(), t.amont.end(), e0), t.amont.end());
          t.aval.erase(std::remove(t.aval.begin(), t.aval.end(), e0), t.aval.end());
        }
        x.transitions.erase(std::remove_if(x.transitions.begin(), x.transitions.end(), [](const TransitionGrafcet& t) { return t.amont.empty() || t.aval.empty(); }),
                            x.transitions.end());
      }
      x.etapes.erase(std::remove_if(x.etapes.begin(), x.etapes.end(), [&](const EtapeGrafcet& e) { return e.numero == e0; }), x.etapes.end());
    });
    m_etat->setText(QStringLiteral("Étape %1 supprimée — Ctrl+Z la rend.").arg(e0));
  } else {
    m_etat->setText(QStringLiteral("Choisis l’étape ou la transition à supprimer (clic sur le dessin)."));
    return;
  }
  m_etapeGrafcet = -1;
  m_transitionGrafcet = -1;
  majGrafcet();
  majPanneau();
}

void AtelierElectrique::simulerGrafcet(bool oui) {
  const prom::elec::Grafcet* g = grafcetChoisi();
  if (oui && g) {
    m_simulation = std::make_unique<SimulateurGrafcet>(*g);
    m_signatureSimulation = signature(*g);
    m_etat->setText(QStringLiteral("Simulation : coche les entrées à droite, le grafcet évolue selon les règles de la norme ; « +1 s » fait passer le temps."));
  } else {
    m_simulation.reset();
    if (!oui) m_etat->setText(QStringLiteral("Simulation arrêtée."));
  }
  majGrafcet();
  majPanneau();
}

void AtelierElectrique::entreeSimulation(const std::string& entree, bool valeur) {
  if (!m_simulation) return;
  m_simulation->fixer(entree, valeur);
  const int n = m_simulation->evoluer();
  majGrafcet();
  majPanneau();
  if (m_simulation->instable()) m_etat->setText(QStringLiteral("<span style='color:#C2402E'>Grafcet instable</span> : des réceptivités toujours vraies forment un cycle."));
  else if (n > 0 && !m_simulation->journal().empty()) m_etat->setText(q(m_simulation->journal().back()).toHtmlEscaped());
}

void AtelierElectrique::avancerSimulation(double secondes) {
  if (!m_simulation) return;
  m_simulation->avancer(secondes);
  majGrafcet();
  majPanneau();
}

int AtelierElectrique::dessinerFolioGrafcet() {
  const prom::elec::Grafcet* g = grafcetChoisi();
  const ProjetElectrique* pe = projetCourant();
  if (!g || !pe) return -1;
  const std::string uuid = g->uuid, nom = g->nom;
  for (std::size_t i = 0; i < pe->schema.feuilles.size(); ++i) {
    const InfosFolio& f = pe->folio(pe->schema.feuilles[i].uuid);
    if (f.type == TypeFolio::Grafcet && f.grafcet == uuid) return static_cast<int>(i);
  }
  int indice = -1;
  modifierProjet([&](ProjetElectrique& x) {
    const std::size_t i = prom::elec::ajouterFolio(x, TypeFolio::Grafcet, "Grafcet " + nom);
    x.folios[x.schema.feuilles[i].uuid].grafcet = uuid;
    indice = static_cast<int>(i);
  });
  m_etat->setText(QStringLiteral("Folio « Grafcet %1 » créé : il se redessine à chaque modification du grafcet, et part dans le PDF et le DXF des folios.").arg(h(nom)));
  return indice;
}

bool AtelierElectrique::exporterTexteStructure(const QString& chemin) {
  const prom::elec::Grafcet* g = grafcetChoisi();
  if (!g) return false;
  QFile f(chemin);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  const std::string t = texteStructureGrafcet(*g);
  return f.write(QByteArray::fromStdString(t)) == static_cast<qint64>(t.size());
}

// ---------- Panneau ----------
void AtelierElectrique::panneauGrafcet(QVBoxLayout* col) {
  const prom::elec::Grafcet* g = grafcetChoisi();
  auto ligne = [](QVBoxLayout* v, const QString& texte, const QString& nom = {}) {
    auto* l = new QLabel(texte);
    l->setWordWrap(true);
    if (!nom.isEmpty()) l->setObjectName(nom);
    v->addWidget(l);
    return l;
  };
  if (!g) {
    auto* gr = new QGroupBox(QStringLiteral("Grafcet"));
    auto* v = new QVBoxLayout(gr);
    ligne(v, QStringLiteral("Le Grafcet (NF EN CEI 60848) décrit un automatisme étape par étape. Pars d’un exemple pour voir comment il se lit, ou d’un grafcet vierge."));
    auto* vierge = new QPushButton(icone(QStringLiteral("grafcet")), QStringLiteral("Grafcet vierge"));
    vierge->setObjectName(QStringLiteral("principal"));
    connect(vierge, &QPushButton::clicked, this, [this] { nouveauGrafcet(); });
    v->addWidget(vierge);
    for (const auto& [id, nom] : exemplesGrafcet()) {
      const std::string ex = id;
      auto* b = new QPushButton(icone(QStringLiteral("apprendre")), q(nom));
      connect(b, &QPushButton::clicked, this, [this, ex] { nouveauGrafcet(ex); });
      v->addWidget(b);
    }
    col->addWidget(gr);
    return;
  }
  // ----- Simulation -----
  if (m_simulation) {
    SimulateurGrafcet& sim = *m_simulation;
    auto* gs = new QGroupBox(QStringLiteral("Simulation"));
    gs->setObjectName(QStringLiteral("groupeSimulation"));
    auto* v = new QVBoxLayout(gs);
    QStringList actives;
    for (int a : sim.actives()) actives << QString::number(a);
    ligne(v, QStringLiteral("<b>t = %1 s</b> · active(s) : <b style='color:#C2402E'>%2</b>").arg(QLocale().toString(sim.temps(), 'f', 1), actives.join(QStringLiteral(", "))),
          QStringLiteral("etatSimulation"));
    const std::vector<std::string> entrees = entreesGrafcet(sim.grafcet());
    if (!entrees.empty()) {
      ligne(v, QStringLiteral("<span style='color:#6B7393'>Entrées (capteurs, boutons) :</span>"));
      for (const auto& e : entrees) {
        auto* c = new QCheckBox(q(e));
        c->setObjectName(QStringLiteral("entree-%1").arg(q(e)));
        c->setChecked(sim.entree(e));
        connect(c, &QCheckBox::toggled, this, [this, e](bool oui) { entreeSimulation(e, oui); });
        v->addWidget(c);
      }
    }
    auto* temps = new QHBoxLayout;
    temps->setSpacing(3);
    for (double dt : {0.5, 1.0, 5.0}) {
      auto* b = new QPushButton(QStringLiteral("+%1 s").arg(QLocale().toString(dt)));
      b->setObjectName(QStringLiteral("avancer-%1").arg(dt * 10));
      b->setToolTip(QStringLiteral("Faire passer %1 s : les temporisations (« 5s/X3 ») avancent").arg(QLocale().toString(dt)));
      connect(b, &QPushButton::clicked, this, [this, dt] { avancerSimulation(dt); });
      temps->addWidget(b);
    }
    v->addLayout(temps);
    QString sorties;
    for (const auto& [nom, val] : sim.sorties())
      sorties += QStringLiteral("<br><span style='color:%1'>%2</span> %3").arg(val ? QStringLiteral("#2E8B57") : QStringLiteral("#9AA0B8"), val ? QStringLiteral("●") : QStringLiteral("○"), h(nom));
    if (!sorties.isEmpty()) ligne(v, QStringLiteral("<span style='color:#6B7393'>Sorties (actions) :</span>") + sorties, QStringLiteral("sortiesSimulation"));
    if (sim.instable()) ligne(v, QStringLiteral("<span style='color:#C2402E'>Grafcet instable : des réceptivités toujours vraies forment un cycle.</span>"));
    QString journal;
    const auto& j = sim.journal();
    for (std::size_t k = j.size() > 4 ? j.size() - 4 : 0; k < j.size(); ++k) journal += (journal.isEmpty() ? QString() : QStringLiteral("<br>")) + h(j[k]);
    if (!journal.isEmpty()) ligne(v, QStringLiteral("<span style='color:#6B7393; font-size:11px'>%1</span>").arg(journal));
    auto* boutons = new QHBoxLayout;
    auto* init = new QPushButton(icone(QStringLiteral("annuler-fleche")), QStringLiteral("Initialiser"));
    init->setObjectName(QStringLiteral("initialiserSimulation"));
    connect(init, &QPushButton::clicked, this, [this] {
      if (m_simulation) m_simulation->initialiser();
      majGrafcet();
      majPanneau();
    });
    boutons->addWidget(init);
    auto* arret = new QPushButton(icone(QStringLiteral("abandonner")), QStringLiteral("Arrêter"));
    arret->setObjectName(QStringLiteral("arreterSimulation"));
    connect(arret, &QPushButton::clicked, this, [this] { simulerGrafcet(false); });
    boutons->addWidget(arret);
    v->addLayout(boutons);
    col->addWidget(gs);
  }
  // ----- Étape choisie -----
  if (const EtapeGrafcet* e = m_etapeGrafcet >= 0 ? g->etape(m_etapeGrafcet) : nullptr) {
    const int numero = e->numero;
    auto modifierEtape = [this, numero](const std::function<void(EtapeGrafcet&)>& f) {
      modifierGrafcet([&](prom::elec::Grafcet& x) {
        if (EtapeGrafcet* y = x.etape(numero)) f(*y);
      });
    };
    auto* ge = new QGroupBox(QStringLiteral("Étape %1").arg(numero));
    ge->setObjectName(QStringLiteral("groupeEtape"));
    auto* f = new QFormLayout(ge);
    auto* num = new QSpinBox;
    num->setObjectName(QStringLiteral("numeroEtape"));
    num->setRange(0, 9999);
    num->setValue(numero);
    connect(num, &QSpinBox::editingFinished, this, [this, num, numero] {
      const int nouveau = num->value();
      const prom::elec::Grafcet* g2 = grafcetChoisi();
      if (!g2 || nouveau == numero) return;
      if (g2->etape(nouveau)) {
        m_etat->setText(QStringLiteral("L’étape %1 existe déjà : chaque étape a son numéro.").arg(nouveau));
        return;
      }
      modifierGrafcet([&](prom::elec::Grafcet& x) {
        if (EtapeGrafcet* y = x.etape(numero)) y->numero = nouveau;
        for (auto& t : x.transitions) {
          std::replace(t.amont.begin(), t.amont.end(), numero, nouveau);
          std::replace(t.aval.begin(), t.aval.end(), numero, nouveau);
        }
      });
      m_etapeGrafcet = nouveau;
      m_etat->setText(QStringLiteral("Étape %1 renumérotée %2 ; les réceptivités qui citent X%1 sont à reprendre à la main.").arg(numero).arg(nouveau));
    });
    f->addRow(QStringLiteral("Numéro"), num);
    auto* initiale = new QCheckBox(QStringLiteral("Étape initiale (active au départ)"));
    initiale->setObjectName(QStringLiteral("etapeInitiale"));
    initiale->setChecked(e->initiale);
    connect(initiale, &QCheckBox::toggled, this, [modifierEtape](bool oui) { modifierEtape([oui](EtapeGrafcet& y) { y.initiale = oui; }); });
    f->addRow(QString(), initiale);
    auto* com = new QLineEdit(q(e->commentaire));
    com->setObjectName(QStringLiteral("commentaireEtape"));
    com->setPlaceholderText(QStringLiteral("ce que fait la machine (avance, attente…)"));
    connect(com, &QLineEdit::editingFinished, this, [com, modifierEtape] { modifierEtape([&](EtapeGrafcet& y) { y.commentaire = s(com->text()); }); });
    f->addRow(QStringLiteral("Commentaire"), com);
    auto* actions = new QPlainTextEdit;
    actions->setObjectName(QStringLiteral("actionsEtape"));
    QStringList lignes;
    for (const auto& a : e->actions) lignes << q(texteAction(a));
    actions->setPlainText(lignes.join(QLatin1Char('\n')));
    actions->setPlaceholderText(QStringLiteral("une action par ligne : KM1 · KM2 si a · H1 := 1 · H1 := 0"));
    actions->setMaximumHeight(90);
    actions->setToolTip(QStringLiteral("« KM1 » : continue, vraie tant que l’étape est active\n« KM1 si a » : conditionnelle\n« H1 := 1 » : mémorisée à l’activation (« S H1 »)\n"
                                       "« H1 := 0 » : remise à zéro (« R H1 »)\n« ↓ H1 := 0 » : à la désactivation"));
    f->addRow(QStringLiteral("Actions"), actions);
    auto* appliquer = new QPushButton(icone(QStringLiteral("valider")), QStringLiteral("Appliquer les actions"));
    appliquer->setObjectName(QStringLiteral("appliquerActions"));
    connect(appliquer, &QPushButton::clicked, this, [this, actions, modifierEtape] {
      std::vector<ActionGrafcet> liste;
      QStringList erreurs;
      for (const QString& l : actions->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        ActionGrafcet a;
        std::string erreur;
        if (lireAction(s(l), a, &erreur)) liste.push_back(a);
        else erreurs << QStringLiteral("« %1 » : %2").arg(l.trimmed().toHtmlEscaped(), h(erreur));
      }
      if (!erreurs.isEmpty()) {
        m_etat->setText(QStringLiteral("<span style='color:#C2402E'>Action non comprise</span> — ") + erreurs.join(QStringLiteral(" ; ")));
        return;
      }
      modifierEtape([&](EtapeGrafcet& y) { y.actions = liste; });
      m_etat->setText(QStringLiteral("%1 action(s) enregistrée(s).").arg(liste.size()));
    });
    f->addRow(QString(), appliquer);
    col->addWidget(ge);
    auto* go = new QGroupBox(QStringLiteral("Construire"));
    auto* vo = new QVBoxLayout(go);
    auto* suite = new QPushButton(icone(QStringLiteral("grafcet")), QStringLiteral("Étape suivante"));
    suite->setObjectName(QStringLiteral("etapeSuivanteGrafcet"));
    suite->setToolTip(QStringLiteral("Ajoute l’étape qui suit celle-ci, avec la transition entre les deux"));
    connect(suite, &QPushButton::clicked, this, [this] { etapeSuivanteGrafcet(); });
    vo->addWidget(suite);
    auto* ou = new QPushButton(icone(QStringLiteral("grafcet")), QStringLiteral("Choix (branche en OU)"));
    ou->setObjectName(QStringLiteral("brancheOU"));
    ou->setToolTip(QStringLiteral("Un autre chemin possible depuis cette étape : l’une ou l’autre branche, selon la réceptivité vraie"));
    connect(ou, &QPushButton::clicked, this, [this] { brancheGrafcet(false); });
    vo->addWidget(ou);
    auto* retour = new QPushButton(icone(QStringLiteral("annuler-fleche")), QStringLiteral("Retour vers une étape"));
    retour->setObjectName(QStringLiteral("boucler"));
    auto* menu = new QMenu(retour);
    for (const auto& x : g->etapes) {
      const int vers = x.numero;
      menu->addAction(QStringLiteral("Vers l’étape %1%2").arg(vers).arg(x.initiale ? QStringLiteral(" (initiale)") : QString()), this, [this, vers] { bouclerGrafcet(vers); });
    }
    retour->setMenu(menu);
    vo->addWidget(retour);
    auto* suppr = new QPushButton(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer l’étape"));
    connect(suppr, &QPushButton::clicked, this, [this] { supprimerDansGrafcet(); });
    vo->addWidget(suppr);
    col->addWidget(go);
  } else if (m_transitionGrafcet >= 0 && m_transitionGrafcet < static_cast<int>(g->transitions.size())) {
    // ----- Transition choisie -----
    const int ti = m_transitionGrafcet;
    const TransitionGrafcet& t = g->transitions[static_cast<std::size_t>(ti)];
    auto modifierTransition = [this, ti](const std::function<void(TransitionGrafcet&)>& f) {
      modifierGrafcet([&](prom::elec::Grafcet& x) {
        if (ti < static_cast<int>(x.transitions.size())) f(x.transitions[static_cast<std::size_t>(ti)]);
      });
    };
    auto* gt = new QGroupBox(QStringLiteral("Transition %1").arg(ti + 1));
    gt->setObjectName(QStringLiteral("groupeTransition"));
    auto* v = new QVBoxLayout(gt);
    ligne(v, QStringLiteral("<span style='color:#6B7393'>Réceptivité : ce que la machine attend pour passer à l’étape suivante.</span>"));
    auto* r = new QLineEdit(q(t.receptivite));
    r->setObjectName(QStringLiteral("receptivite"));
    r->setPlaceholderText(QStringLiteral("dcy · a0, /b, ↑S1, 5s/X3…"));
    v->addWidget(r);
    // Les signes difficiles à taper, en boutons
    auto* signes = new QHBoxLayout;
    signes->setSpacing(2);
    for (auto [signe, bulle] : {std::pair{"·", "ET"}, std::pair{"+", "OU"}, std::pair{"/", "NON (complément)"}, std::pair{"↑", "front montant"}, std::pair{"↓", "front descendant"},
                                std::pair{"( )", "parenthèses"}, std::pair{"s/X", "temporisation : 5s/X3"}}) {
      auto* b = new QToolButton;
      b->setText(QString::fromUtf8(signe));
      b->setToolTip(QString::fromUtf8(bulle));
      const QString texte = QString::fromUtf8(signe) == QStringLiteral("( )") ? QStringLiteral("()") : QString::fromUtf8(signe);
      connect(b, &QToolButton::clicked, this, [r, texte] {
        const QString insere = texte == QStringLiteral("·") || texte == QStringLiteral("+") ? QStringLiteral(" %1 ").arg(texte) : texte;
        r->insert(insere);
        if (texte == QStringLiteral("()")) r->cursorBackward(false);
        r->setFocus();
      });
      signes->addWidget(b);
    }
    signes->addStretch(1);
    v->addLayout(signes);
    auto* lecture = ligne(v, QString(), QStringLiteral("lectureReceptivite"));
    auto relire = [lecture](const QString& texte) {
      const LectureReceptivite l = lireReceptivite(s(texte));
      if (l.expression) {
        std::vector<std::string> vars;
        variablesReceptivite(*l.expression, vars);
        QStringList noms;
        for (const auto& x : vars) noms << q(x);
        lecture->setText(QStringLiteral("<span style='color:#2E8B57'>✓ %1</span>%2")
                             .arg(h(texteReceptivite(*l.expression)), noms.isEmpty() ? QString() : QStringLiteral("<br><span style='color:#6B7393'>entrées : %1</span>").arg(noms.join(QStringLiteral(", ")).toHtmlEscaped())));
      } else {
        lecture->setText(QStringLiteral("<span style='color:#C2402E'>✗ caractère %1 : %2</span>").arg(l.position + 1).arg(h(l.erreur)));
      }
    };
    relire(r->text());
    connect(r, &QLineEdit::textChanged, this, relire);
    connect(r, &QLineEdit::editingFinished, this, [this, r, modifierTransition] {
      const std::string texte = s(r->text()).empty() ? std::string("1") : s(r->text());
      const prom::elec::Grafcet* g2 = grafcetChoisi();
      if (!g2 || m_transitionGrafcet < 0 || m_transitionGrafcet >= static_cast<int>(g2->transitions.size()) ||
          g2->transitions[static_cast<std::size_t>(m_transitionGrafcet)].receptivite == texte)
        return;
      modifierTransition([&](TransitionGrafcet& y) { y.receptivite = texte; });
    });
    auto* f = new QFormLayout;
    auto* amont = new QLineEdit(ecrireNumeros(t.amont));
    amont->setObjectName(QStringLiteral("amontTransition"));
    amont->setToolTip(QStringLiteral("Étape(s) avant la transition ; plusieurs (« 2, 4 ») : elle attend qu’elles soient toutes actives (convergence en ET)"));
    connect(amont, &QLineEdit::editingFinished, this, [amont, modifierTransition] {
      const std::vector<int> v2 = lireNumeros(amont->text());
      if (!v2.empty()) modifierTransition([&](TransitionGrafcet& y) { y.amont = v2; });
    });
    f->addRow(QStringLiteral("Après l’étape"), amont);
    auto* aval = new QLineEdit(ecrireNumeros(t.aval));
    aval->setObjectName(QStringLiteral("avalTransition"));
    aval->setToolTip(QStringLiteral("Étape(s) activées ; plusieurs (« 1, 3 ») : séquences en parallèle (divergence en ET)"));
    connect(aval, &QLineEdit::editingFinished, this, [aval, modifierTransition] {
      const std::vector<int> v2 = lireNumeros(aval->text());
      if (!v2.empty()) modifierTransition([&](TransitionGrafcet& y) { y.aval = v2; });
    });
    f->addRow(QStringLiteral("Vers l’étape"), aval);
    v->addLayout(f);
    col->addWidget(gt);
    auto* go = new QGroupBox(QStringLiteral("Construire"));
    auto* vo = new QVBoxLayout(go);
    auto* apres = new QPushButton(icone(QStringLiteral("grafcet")), QStringLiteral("Étape après"));
    apres->setToolTip(QStringLiteral("Insère une étape après cette transition"));
    connect(apres, &QPushButton::clicked, this, [this] { etapeSuivanteGrafcet(); });
    vo->addWidget(apres);
    auto* et = new QPushButton(icone(QStringLiteral("grafcet")), QStringLiteral("En parallèle (branche en ET)"));
    et->setObjectName(QStringLiteral("brancheET"));
    et->setToolTip(QStringLiteral("Une séquence de plus, lancée par cette transition en même temps que la première"));
    connect(et, &QPushButton::clicked, this, [this] { brancheGrafcet(true); });
    vo->addWidget(et);
    auto* suppr = new QPushButton(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer la transition"));
    connect(suppr, &QPushButton::clicked, this, [this] { supprimerDansGrafcet(); });
    vo->addWidget(suppr);
    col->addWidget(go);
  } else {
    // ----- Le grafcet -----
    FormulaireLie fl(this, col);
    QFormLayout* f = fl.groupe(QStringLiteral("Grafcet"), QStringLiteral("groupeGrafcet"));
    fl.texte(f, QStringLiteral("Nom"), g->nom, QStringLiteral("nomGrafcet"), [this](const std::string& v) {
      if (!v.empty()) modifierGrafcet([&](prom::elec::Grafcet& x) { x.nom = v; });
    });
    auto* gb = new QGroupBox(QStringLiteral("Aller plus loin"));
    auto* vb = new QVBoxLayout(gb);
    auto* simuler = new QPushButton(icone(QStringLiteral("simuler")), m_simulation ? QStringLiteral("Arrêter la simulation") : QStringLiteral("Simuler"));
    simuler->setObjectName(QStringLiteral("simulerGrafcet"));
    connect(simuler, &QPushButton::clicked, this, [this] { simulerGrafcet(!m_simulation); });
    vb->addWidget(simuler);
    auto* folio = new QPushButton(icone(QStringLiteral("plan")), QStringLiteral("Dessiner sur un folio"));
    folio->setObjectName(QStringLiteral("folioGrafcet"));
    connect(folio, &QPushButton::clicked, this, [this] {
      const int i = dessinerFolioGrafcet();
      if (i >= 0) {
        choisirMode(Schema);
        allerAuFolio(i);
      }
    });
    vb->addWidget(folio);
    auto* st = new QPushButton(icone(QStringLiteral("code")), QStringLiteral("Copier le programme (ST)"));
    st->setObjectName(QStringLiteral("copierST"));
    st->setToolTip(QStringLiteral("Texte structuré CEI 61131-3 : à coller dans CODESYS, TIA Portal (SCL), Control Expert…"));
    connect(st, &QPushButton::clicked, this, [this] {
      if (const prom::elec::Grafcet* g2 = grafcetChoisi()) {
        QApplication::clipboard()->setText(q(texteStructureGrafcet(*g2)));
        m_etat->setText(QStringLiteral("Programme en texte structuré copié : colle-le dans le bloc fonctionnel de ton automate."));
      }
    });
    vb->addWidget(st);
    col->addWidget(gb);
    fl.avis(verifierGrafcet(*g), QStringLiteral("avisGrafcet"));
  }
}

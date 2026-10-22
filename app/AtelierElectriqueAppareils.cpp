// Prométhée : atelier électrique — appareils personnels. Un appareil absent de la bibliothèque se crée dans le
// panneau d'après ses bornes ; il se range dans le projet (qui s'ouvre partout) et dans la bibliothèque de
// l'utilisateur (tous ses projets), se pose comme les autres, porte des contacts, et se modifie : les appareils
// posés suivent. Le fichier de la bibliothèque ne se perd jamais en silence : abîmé, il est mis de côté, écrit par
// fichier temporaire, la version précédente gardée en .bak. Licence GPL-3.0-only.
#include <algorithm>
#include <filesystem>
#include <functional>

#include <QAction>
#include <QDir>
#include <QMenu>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "AtelierElectrique.hpp"
#include "ChienDeGarde.hpp"
#include "EtiquetteEtat.hpp"
#include "FormulaireAppareil.hpp"
#include "Icones.hpp"
#include "VueSchema.hpp"
#include "promethee/bibliotheque.hpp"
#include "promethee/fichiers.hpp"

using namespace prom::elec;
using prom::ecad::DefSymbole;

namespace {

QString h(const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); }

std::filesystem::path chemin() { return prom::cheminFichier(AtelierElectrique::cheminMesAppareils().toStdString()); }

}  // namespace

QString AtelierElectrique::cheminMesAppareils() {
  const QString env = qEnvironmentVariable("PROMETHEE_BIBLIOTHEQUE_ELECTRIQUE");
  if (!env.isEmpty()) return env;
  return QDir(portable::dossierDonneesAppli()).filePath(QStringLiteral("bibliotheque-electrique.json"));
}

void AtelierElectrique::chargerMesAppareils() {
  m_mesAppareils.clear();
  try {
    const prom::ecad::LectureBibliotheque l = prom::ecad::lireFichierBibliotheque(chemin());
    for (const auto& d : l.symboles)
      if (estAppareilPersonnel(d.id)) m_mesAppareils.push_back(d);
    if (l.abimee && m_etat)
      m_etat->setText(QStringLiteral("<span style='color:#A8620E'>Ta bibliothèque d’appareils est abîmée (%1) : %2 appareil(s) repris ; elle sera mise de côté à la prochaine "
                                     "écriture.</span>")
                          .arg(h(l.erreur))
                          .arg(m_mesAppareils.size()));
  } catch (const std::exception& e) {
    if (m_etat) m_etat->setText(QStringLiteral("<span style='color:#C2402E'>Bibliothèque d’appareils illisible : %1</span>").arg(QString::fromUtf8(e.what()).toHtmlEscaped()));
  }
  std::sort(m_mesAppareils.begin(), m_mesAppareils.end(), [](const DefSymbole& a, const DefSymbole& b) { return a.nom != b.nom ? a.nom < b.nom : a.id < b.id; });
}

namespace {
// Réécrit la bibliothèque de l'utilisateur avec « change » appliqué ; rend une note (fichier abîmé mis de côté) ou
// lève l'erreur d'écriture
QString reecrire(const std::function<void(std::vector<DefSymbole>&)>& change) {
  const std::filesystem::path c = chemin();
  prom::ecad::LectureBibliotheque l;
  QString note;
  try {
    l = prom::ecad::lireFichierBibliotheque(c);
  } catch (const std::exception& e) {
    l.abimee = true;
    l.erreur = e.what();
  }
  if (l.abimee && std::filesystem::exists(c)) {
    const std::filesystem::path cote = prom::ecad::mettreDeCoteBibliotheque(c);
    note = QStringLiteral(" L’ancienne bibliothèque était abîmée (%1) : mise de côté sous « %2 », %3 appareil(s) repris.")
               .arg(h(l.erreur), h(prom::utf8(cote.filename())))
               .arg(l.symboles.size());
  }
  std::vector<DefSymbole> v = l.symboles;
  change(v);
  std::filesystem::create_directories(c.parent_path());
  prom::ecad::ecrireFichierBibliotheque(c, v, l.empreintes);
  return note;
}
}  // namespace

void AtelierElectrique::rangerAppareil(const DefSymbole& d) {
  try {
    const QString note = reecrire([&](std::vector<DefSymbole>& v) {
      const auto it = std::find_if(v.begin(), v.end(), [&](const DefSymbole& x) { return x.id == d.id; });
      if (it != v.end()) *it = d;
      else v.push_back(d);
    });
    if (!note.isEmpty()) m_etat->setText(QStringLiteral("<span style='color:#A8620E'>%1</span>").arg(note.trimmed()));
  } catch (const std::exception& e) {
    m_etat->setText(QStringLiteral("<span style='color:#C2402E'>« %1 » reste dans ce projet mais n’a pas pu être rangé dans ta bibliothèque : %2</span>")
                        .arg(h(d.nom), QString::fromUtf8(e.what()).toHtmlEscaped()));
  }
  chargerMesAppareils();
}

void AtelierElectrique::retirerAppareil(const std::string& id) {
  std::string nom = id;
  for (const auto& d : m_mesAppareils)
    if (d.id == id) nom = d.nom;
  try {
    reecrire([&](std::vector<DefSymbole>& v) { v.erase(std::remove_if(v.begin(), v.end(), [&](const DefSymbole& x) { return x.id == id; }), v.end()); });
    m_etat->setText(QStringLiteral("« %1 » retiré de ta bibliothèque ; les projets qui s’en servent gardent leur copie.").arg(h(nom)));
  } catch (const std::exception& e) {
    m_etat->setText(QStringLiteral("<span style='color:#C2402E'>Bibliothèque non écrite : %1</span>").arg(QString::fromUtf8(e.what()).toHtmlEscaped()));
  }
  chargerMesAppareils();
  majPalette();
}

const DefSymbole* AtelierElectrique::definitionSymbole(const std::string& id) const {
  if (const ProjetElectrique* pe = projetCourant())
    if (const DefSymbole* d = pe->schema.symbole(id)) return d;
  if (const DefSymbole* d = symboleElectrique(id)) return d;
  for (const auto& d : m_mesAppareils)
    if (d.id == id) return &d;
  return nullptr;
}

void AtelierElectrique::nouvelAppareil() {
  if (mode() != Schema) choisirMode(Schema);
  m_specAppareil = SpecAppareil{};
  m_modifAppareil = false;
  m_formAppareil = true;
  m_etat->setText(QStringLiteral("<b>Nouvel appareil</b> : son nom, sa lettre de repère et ses bornes côté par côté ; l’aperçu suit la frappe."));
  majPanneau();
}

void AtelierElectrique::modifierAppareilPersonnel(const std::string& id) {
  const DefSymbole* d = definitionSymbole(id);
  if (!d || !estAppareilPersonnel(id)) return;
  if (mode() != Schema) choisirMode(Schema);
  m_specAppareil = specAppareil(*d);
  m_modifAppareil = true;
  m_formAppareil = true;
  majPanneau();
}

std::string AtelierElectrique::enregistrerAppareilPersonnel(const SpecAppareil& a0) {
  const ProjetElectrique* pe = projetCourant();
  if (!pe || !defautsAppareil(a0).empty()) return {};
  SpecAppareil a = a0;
  if (a.id.empty()) {
    // Nouveau : un identifiant libre dans le projet et dans la bibliothèque (un homonyme ne remplace rien)
    std::vector<std::string> pris;
    for (const auto& d : m_mesAppareils) pris.push_back(d.id);
    a.id = identifiantAppareil(*pe, a.nom, pris);
  }
  int poses = 0;
  for (const auto& c : pe->schema.composants) poses += c.symbole == a.id ? 1 : 0;
  const bool dansProjet = pe->schema.symbole(a.id) != nullptr;
  const bool nouveau = !dansProjet && std::none_of(m_mesAppareils.begin(), m_mesAppareils.end(), [&](const DefSymbole& d) { return d.id == a.id; });
  if (dansProjet || nouveau) {
    std::string id;
    modifierProjet([&](ProjetElectrique& y) { id = enregistrerAppareil(y, a); });
    if (const ProjetElectrique* p2 = projetCourant())
      if (const DefSymbole* d = p2->schema.symbole(id)) rangerAppareil(*d);
  } else {
    // Seulement dans la bibliothèque : modifié là, le projet n'en a pas besoin
    DefSymbole d = symboleAppareil(a);
    for (const auto& x : m_mesAppareils)
      if (x.id == a.id) d.version = x.version + 1;
    rangerAppareil(d);
  }
  m_formAppareil = false;
  majPalette();
  if (nouveau)
    m_etat->setText(QStringLiteral("<b>%1</b> créé et rangé dans tes appareils (palette, « Mes appareils ») : clique sur le folio pour le poser.").arg(h(a.nom)));
  else
    m_etat->setText(poses > 0 ? QStringLiteral("<b>%1</b> modifié : %2 appareil(s) posé(s) suivent. Si une borne a changé de place, la vérification montre les fils à reprendre.")
                                    .arg(h(a.nom))
                                    .arg(poses)
                              : QStringLiteral("<b>%1</b> modifié.").arg(h(a.nom)));
  majPanneau();
  return a.id;
}

void AtelierElectrique::panneauAppareil(QVBoxLayout* col) {
  int poses = 0;
  if (const ProjetElectrique* pe = projetCourant())
    for (const auto& c : pe->schema.composants) poses += c.symbole == m_specAppareil.id && !m_specAppareil.id.empty() ? 1 : 0;
  auto* f = new FormulaireAppareil(m_specAppareil, m_modifAppareil, poses);
  connect(f, &FormulaireAppareil::modifie, this, [this](const SpecAppareil& a) { m_specAppareil = a; });
  connect(f, &FormulaireAppareil::annule, this, [this] {
    m_formAppareil = false;
    m_etat->setText(QString());
    majPanneau();
  });
  connect(f, &FormulaireAppareil::valide, this, [this](const SpecAppareil& a) {
    const bool creation = !m_modifAppareil;
    // Le panneau se refait pendant l'enregistrement : le formulaire n'est plus là ensuite
    QTimer::singleShot(0, this, [this, a, creation] {
      const std::string id = enregistrerAppareilPersonnel(a);
      if (creation && !id.empty()) {
        const QString message = m_etat->text();
        choisirSymbole(id);
        m_etat->setText(message);
      }
    });
  });
  col->addWidget(f);
}

void AtelierElectrique::menuPalette(const QPoint& ou) {
  QTreeWidgetItem* it = m_palette->itemAt(ou);
  if (!it || it->data(0, Qt::UserRole).toString() != QLatin1String("symbole")) return;
  const std::string id = it->data(0, Qt::UserRole + 1).toString().toStdString();
  if (!estAppareilPersonnel(id)) return;
  const bool dansBibliotheque = std::any_of(m_mesAppareils.begin(), m_mesAppareils.end(), [&](const DefSymbole& d) { return d.id == id; });
  QMenu menu(this);
  menu.setObjectName(QStringLiteral("menuMesAppareils"));
  QAction* modifier = menu.addAction(icone(QStringLiteral("proprietes")), QStringLiteral("Modifier cet appareil…"));
  QAction* ranger = dansBibliotheque ? nullptr : menu.addAction(icone(QStringLiteral("dossier")), QStringLiteral("Ranger dans ma bibliothèque"));
  QAction* retirer = dansBibliotheque ? menu.addAction(icone(QStringLiteral("supprimer")), QStringLiteral("Retirer de ma bibliothèque")) : nullptr;
  QAction* r = menu.exec(m_palette->viewport()->mapToGlobal(ou));
  if (!r) return;
  if (r == modifier) modifierAppareilPersonnel(id);
  else if (r == ranger) {
    if (const DefSymbole* d = definitionSymbole(id)) {
      const DefSymbole copie = *d;
      rangerAppareil(copie);
      m_etat->setText(QStringLiteral("« %1 » rangé dans ta bibliothèque : il sert maintenant dans tous tes projets.").arg(h(copie.nom)));
      majPalette();
    }
  } else if (r == retirer)
    retirerAppareil(id);
}

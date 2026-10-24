// Prométhée : atelier électronique, schéma avancé de la 0.34 (lot L7) — hiérarchie (7.2, 7.3), échange de portes
// (7.8), directives et graphe des réseaux (7.14), réutilisation (7.16), formes importées (7.19), assistant de
// symboles (7.20), empreintes et cuivre propre à la carte (7.9, 7.21, 7.24), jeux de styles (7.25).
// Licence GPL-3.0-only.
#include <algorithm>

#include <QFile>
#include <QFileInfo>

#include "AtelierElectronique.hpp"
#include "FenetresSchema034.hpp"
#include "SelectionElectronique.hpp"
#include "VueCircuit.hpp"
#include "VueSchema.hpp"
#include "promethee/empreintes_edition.hpp"
#include "promethee/reutilisation.hpp"
#include "promethee/schema_avance.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

int AtelierElectronique::migrerEnHierarchie() {
  const ConceptionElec* c = conceptionCourante();
  if (!c || c->feuilles.size() < 2) {
    afficherMessage(QStringLiteral("Une hiérarchie se fait à partir de deux feuilles au moins."), NiveauMessage::Avertissement);
    return -1;
  }
  for (const auto& f : c->feuilles)
    if (!f.blocs.empty()) {
      afficherMessage(QStringLiteral("La conception est déjà hiérarchique."), NiveauMessage::Information);
      return -1;
    }
  const std::size_t avant = connectivite(*c).reseaux.size();
  int racine = -1;
  modifierConception([&](ConceptionElec& x) { racine = static_cast<int>(hierarchiserFeuilles(x)); }, QStringLiteral("Feuilles migrées en hiérarchie"));
  m_schema->setConception(m_conception, std::max(0, racine));
  const std::size_t apres = conceptionCourante() ? connectivite(*conceptionCourante()).reseaux.size() : 0;
  afficherMessage(QStringLiteral("Racine créée : un bloc par feuille, %1 réseaux avant comme après%2.").arg(static_cast<int>(apres)).arg(apres == avant ? QString() : QStringLiteral(" (à vérifier : %1 avant)").arg(static_cast<int>(avant))),
                  apres == avant ? NiveauMessage::Succes : NiveauMessage::Avertissement);
  return racine;
}

int AtelierElectronique::synchroniserPortsBlocs(bool retirerOrphelins) {
  const ConceptionElec* c = conceptionCourante();
  const int feuille = m_schema->feuille();
  if (!c || feuille < 0 || static_cast<std::size_t>(feuille) >= c->feuilles.size()) return 0;
  std::vector<std::string> blocs;
  for (const auto& b : c->feuilles[static_cast<std::size_t>(feuille)].blocs) blocs.push_back(b.uuid);
  if (blocs.empty()) {
    afficherMessage(QStringLiteral("Aucun bloc sur cette feuille."), NiveauMessage::Information);
    return 0;
  }
  int n = 0;
  modifierConception([&](ConceptionElec& x) {
    for (const auto& b : blocs) n += synchroniserPorts(x, static_cast<std::size_t>(feuille), b, retirerOrphelins);
  }, QStringLiteral("Ports des blocs synchronisés"));
  afficherMessage(n ? QStringLiteral("%1 port(s) ajouté(s) ou retiré(s).").arg(n) : QStringLiteral("Les ports des blocs suivent déjà leurs feuilles filles."), NiveauMessage::Succes);
  recalculer();
  return n;
}

void AtelierElectronique::allerFeuilleMere() {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  if (const auto p = blocParent(*c, static_cast<std::size_t>(std::max(0, m_schema->feuille())))) m_schema->setConception(m_conception, static_cast<int>(p->first));
  else afficherMessage(QStringLiteral("Cette feuille est à la racine."), NiveauMessage::Information);
}

bool AtelierElectronique::echangerPortesChoisies() {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return false;
  std::vector<const SymbolePlace*> poses;
  for (const auto& id : m_selection->ids(SelectionElectronique::Genre::Symbole))
    for (const auto& f : c->feuilles)
      for (const auto& s : f.symboles)
        if (s.uuid == id) poses.push_back(&s);
  if (poses.size() != 2 || poses[0]->composant != poses[1]->composant || poses[0]->composant.empty()) {
    afficherMessage(QStringLiteral("Choisis deux unités d’un même composant (deux portes d’un 74HC00, par exemple)."), NiveauMessage::Avertissement);
    return false;
  }
  const std::string composant = poses[0]->composant;
  const int a = poses[0]->unite, b = poses[1]->unite;
  BilanEchange bilan;
  modifierConception([&](ConceptionElec& x) { bilan = echangerPortes(x, composant, a, b); }, QStringLiteral("Portes échangées"));
  if (!bilan.fait) {
    afficherMessage(QStringLiteral("Échange refusé : %1.").arg(q(bilan.raison)), NiveauMessage::Avertissement);
    return false;
  }
  recalculer();
  afficherMessage(QStringLiteral("Portes échangées : le circuit suit le schéma%1.").arg(bilan.pistesRetirees ? QStringLiteral(", %1 piste(s) à refaire retirée(s)").arg(bilan.pistesRetirees) : QString()),
                  NiveauMessage::Succes);
  return true;
}

int AtelierElectronique::appliquerDirectivesSchema() {
  int n = 0;
  modifierConception([&](ConceptionElec& x) { n = appliquerDirectives(x); }, QStringLiteral("Directives appliquées au circuit"));
  afficherMessage(n ? QStringLiteral("%1 réseau(x) rangé(s) dans la classe de leur directive.").arg(n) : QStringLiteral("Les règles du circuit suivent déjà les directives."), NiveauMessage::Succes);
  return n;
}

DialogueGrapheReseaux* AtelierElectronique::grapheReseaux() const { return m_grapheReseaux; }

void AtelierElectronique::ouvrirGrapheReseaux() {
  if (!m_grapheReseaux) m_grapheReseaux = new DialogueGrapheReseaux(*this, this);
  else m_grapheReseaux->recalculer();
  m_grapheReseaux->show();
  m_grapheReseaux->raise();
}

void AtelierElectronique::ouvrirImportFormes() {
  auto* d = new DialogueImportFormes(*this, this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->show();
}

int AtelierElectronique::importerFormesFeuille(const QString& chemin, const std::set<std::string>& calques) {
  DialogueImportFormes d(*this, this);
  if (!d.choisirFichier(chemin)) return 0;
  if (!calques.empty()) d.choisirCalques(calques);
  return d.importer();
}

bool AtelierElectronique::enregistrerExtrait(const QString& chemin, const QString& nom) {
  const ConceptionElec* c = conceptionCourante();
  if (!c || m_selection->vide()) return false;
  std::set<std::string> schema, carte;
  for (const auto& r : m_selection->objets()) (SelectionElectronique::auSchema(r.genre) ? schema : carte).insert(r.id);
  const bool auCircuit = mode() == Circuit ? !carte.empty() : schema.empty();
  const Extrait e = auCircuit ? extraitCircuit(*c, carte, nom.toStdString()) : extraitSchema(*c, static_cast<std::size_t>(std::max(0, m_schema->feuille())), schema, nom.toStdString());
  QFile f(chemin);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  f.write(QByteArray::fromStdString(texteExtrait(e)));
  afficherMessage(QStringLiteral("Extrait <b>%1</b> enregistré (%2).").arg(nom.toHtmlEscaped(), auCircuit ? QStringLiteral("circuit") : QStringLiteral("schéma")), NiveauMessage::Succes);
  return true;
}

int AtelierElectronique::poserExtraitFichier(const QString& chemin) {
  QFile f(chemin);
  const ConceptionElec* c = conceptionCourante();
  if (!c || !f.open(QIODevice::ReadOnly)) return 0;
  std::string erreur;
  const auto e = lireExtrait(f.readAll().toStdString(), &erreur);
  if (!e) {
    afficherMessage(QStringLiteral("Extrait illisible : %1").arg(q(erreur).toHtmlEscaped()), NiveauMessage::Erreur);
    return 0;
  }
  const std::size_t feuille = static_cast<std::size_t>(std::max(0, m_schema->feuille()));
  Pt ou;
  if (e->genre == "circuit") {
    const Boite b = boite(c->circuit.contour.polygone());
    ou = {(b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2};
  } else {
    const Boite b = cadreFeuille(c->feuilles[feuille].format);
    ou = surGrille({(b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2}, GRILLE_SCHEMA);
  }
  std::set<std::string> poses;
  modifierConception([&](ConceptionElec& x) { poses = poserExtrait(x, *e, feuille, ou); }, QStringLiteral("Extrait « %1 » posé").arg(q(e->nom)));
  recalculer();
  afficherMessage(QStringLiteral("Extrait <b>%1</b> (révision %2, %3) posé : %4 objet(s).").arg(q(e->nom).toHtmlEscaped()).arg(e->revision).arg(q(e->etat)).arg(static_cast<int>(poses.size())),
                  NiveauMessage::Succes);
  return static_cast<int>(poses.size());
}

int AtelierElectronique::recopierDispositionChoisie(double dxMm, double dyMm) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return 0;
  const std::vector<std::string> source(m_selection->composants().begin(), m_selection->composants().end());
  if (source.empty()) {
    afficherMessage(QStringLiteral("Choisis au circuit les composants de la voie modèle, placés et routés."), NiveauMessage::Avertissement);
    return 0;
  }
  const auto canaux = apparierCanaux(*c, source);
  if (canaux.empty()) {
    afficherMessage(QStringLiteral("Aucune voie semblable à la sélection n’a été trouvée."), NiveauMessage::Avertissement);
    return 0;
  }
  int n = 0;
  modifierConception([&](ConceptionElec& x) { n = recopierDisposition(x, source, canaux, ptMm(dxMm, dyMm)); }, QStringLiteral("Disposition recopiée"));
  recalculer();
  afficherMessage(QStringLiteral("Disposition recopiée sur %1 voie(s) : %2 composant(s) appariés et placés.").arg(static_cast<int>(canaux.size())).arg(n), NiveauMessage::Succes);
  return n;
}

void AtelierElectronique::ouvrirJeuxStyles() {
  auto* d = new DialogueJeuxStyles(*this, this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->show();
}

bool AtelierElectronique::appliquerJeuStylesNomme(const QString& nom) {
  DialogueJeuxStyles d(*this, this);
  const bool ok = d.appliquer(nom);
  if (ok) {
    m_schema->invaliderCache();
    m_circuit->update();
  }
  return ok;
}

void AtelierElectronique::ouvrirAssistantSymbole() {
  auto* d = new DialogueAssistantSymbole(*this, this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->show();
}

namespace {
std::string composantChoisi(const SelectionElectronique& s) { return s.composants().empty() ? std::string() : *s.composants().begin(); }
}  // namespace

QString AtelierElectronique::copieLocaleChoisie() {
  const std::string composant = composantChoisi(*m_selection);
  if (composant.empty()) {
    afficherMessage(QStringLiteral("Choisis un composant posé sur la carte."), NiveauMessage::Avertissement);
    return {};
  }
  std::string copie;
  modifierConception([&](ConceptionElec& x) { copie = copierEmpreinteLocale(x, composant); }, QStringLiteral("Cuivre propre au composant"));
  if (copie.empty()) return {};
  afficherMessage(QStringLiteral("Copie locale <b>%1</b> : ses retouches ne touchent ni la bibliothèque ni les autres composants.").arg(q(copie).toHtmlEscaped()), NiveauMessage::Succes);
  ouvrirPastilles(copie);
  return q(copie);
}

bool AtelierElectronique::revenirBibliothequeChoisie() {
  const std::string composant = composantChoisi(*m_selection);
  bool ok = false;
  modifierConception([&](ConceptionElec& x) { ok = revenirBibliotheque(x, composant); }, QStringLiteral("Retour à l’empreinte de la bibliothèque"));
  if (ok) recalculer();
  return ok;
}

int AtelierElectronique::eclaterEmpreinteChoisie() {
  const std::string composant = composantChoisi(*m_selection);
  std::string raison;
  int n = 0;
  modifierConception([&](ConceptionElec& x) { n = eclaterEmpreinte(x, composant, &raison); }, QStringLiteral("Empreinte éclatée"));
  if (!n && !raison.empty()) afficherMessage(q(raison).toHtmlEscaped(), NiveauMessage::Avertissement);
  if (n) recalculer();
  return n;
}

void AtelierElectronique::ouvrirGenerateurs() {
  auto* d = new DialogueGenerateurs(*this, this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->show();
}

DialoguePastilles* AtelierElectronique::pastillesEmpreinte() const { return m_pastillesEmpreinte; }

void AtelierElectronique::ouvrirPastilles(const std::string& empreinte) {
  std::string id = empreinte;
  if (id.empty())
    if (const ConceptionElec* c = conceptionCourante()) {
      const std::string composant = composantChoisi(*m_selection);
      for (const auto& e : c->circuit.empreintes)
        if (e.composant == composant) id = e.empreinte;
      if (id.empty())
        if (const ComposantElec* x = c->composant(composant)) id = x->empreinte;
    }
  if (id.empty()) {
    afficherMessage(QStringLiteral("Choisis un composant : le tableau montre les pastilles de son empreinte."), NiveauMessage::Information);
    return;
  }
  if (m_pastillesEmpreinte) m_pastillesEmpreinte->close();
  m_pastillesEmpreinte = new DialoguePastilles(*this, id, this);
  m_pastillesEmpreinte->setAttribute(Qt::WA_DeleteOnClose);
  m_pastillesEmpreinte->show();
}

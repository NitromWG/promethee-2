// Prométhée : articles réels dans l'atelier électronique (lot L4) : bibliothèque personnelle d'articles, fenêtres de
// choix non modales, archives de portails, standardisation et mises à jour. Licence GPL-3.0-only.
#include <algorithm>
#include <filesystem>
#include <map>

#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "DialoguesArticles.hpp"
#include "FenetreChoixComposant.hpp"
#include "FenetreChoixEmpreinte.hpp"
#include "SelectionElectronique.hpp"
#include "VueSchema.hpp"
#include "promethee/fichiers.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
// Une fenêtre non modale amenée devant, à sa taille, au centre de l'atelier la première fois
void montrerAuCentre(QWidget* fenetre, QWidget* atelier) {
  if (!fenetre->isVisible()) {
    const QRect g = atelier->window()->geometry();
    fenetre->move(g.center() - QPoint(fenetre->width() / 2, fenetre->height() / 2));
  }
  fenetre->show();
  fenetre->raise();
  fenetre->activateWindow();
}
}  // namespace

FenetreChoixComposant* AtelierElectronique::choixComposant() const { return m_choixComposant; }
FenetreChoixEmpreinte* AtelierElectronique::choixEmpreinte() const { return m_choixEmpreinte; }
DialogueStandardisation* AtelierElectronique::standardisation() const { return m_standardisation; }
DialogueMisesAJour* AtelierElectronique::misesAJourArticles() const { return m_misesAJour; }

QString AtelierElectronique::cheminArticlesPerso() {
  const QFileInfo bib(cheminBibliothequePerso());
  return bib.dir().filePath(QStringLiteral("articles-electronique.json"));
}

std::vector<Article> AtelierElectronique::articlesPerso() {
  try {
    const std::filesystem::path p = prom::cheminFichier(cheminArticlesPerso().toStdString());
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return {};
    return lireArticles(prom::lireFichier(p));
  } catch (const std::exception&) {
    return {};
  }
}

void AtelierElectronique::rangerArticlesPerso(const std::vector<Article>& nouveaux) {
  std::vector<Article> v = articlesPerso();
  for (const Article& n : nouveaux) {
    const auto it = std::find_if(v.begin(), v.end(), [&](const Article& x) { return x.id == n.id; });
    if (it != v.end()) *it = n;
    else v.push_back(n);
  }
  try {
    const std::filesystem::path p = prom::cheminFichier(cheminArticlesPerso().toStdString());
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    prom::ecrireFichierAtomique(p, texteArticles(v));
  } catch (const std::exception& e) {
    afficherMessage(QStringLiteral("Articles personnels non écrits : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    return;
  }
  if (m_choixComposant) m_choixComposant->refaire();
  signalerMisesAJourArticles();
}

std::vector<Article> AtelierElectronique::articlesDisponibles() const {
  std::vector<Article> r = bibliothequeArticles();
  std::map<std::string, std::size_t> place;
  for (std::size_t i = 0; i < r.size(); ++i) place[r[i].id] = i;
  for (const Article& a : articlesPerso()) {
    if (const auto it = place.find(a.id); it != place.end()) r[it->second] = a;
    else {
      place[a.id] = r.size();
      r.push_back(a);
    }
  }
  if (const ConceptionElec* c = conceptionCourante())
    for (const auto& [id, a] : c->articles)
      if (!place.count(id) && !a.generique) {
        place[id] = r.size();
        r.push_back(a);
      }
  return r;
}

void AtelierElectronique::armerArticle(const Article& a) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  // Symbole et empreinte d'un article personnel : figés dans le projet avant la pose
  std::optional<DefSymbole> sym;
  std::optional<DefEmpreinte> emp;
  if (!a.symbole.empty() && !c->symbole(a.symbole))
    for (const auto& s : bibliothequePerso())
      if (s.id == a.symbole) sym = s;
  if (!a.empreinte.empty() && !c->empreinte(a.empreinte))
    for (const auto& e : empreintesPerso())
      if (e.id == a.empreinte) emp = e;
  if (sym || emp)
    modifierConception(
        [&](ConceptionElec& c2) {
          if (sym) c2.symboles[sym->id] = *sym;
          if (emp) c2.empreintes[emp->id] = *emp;
        },
        QStringLiteral("Définitions de %1 ajoutées au projet").arg(q(a.id)));
  setMode(Schema);
  m_schema->armerArticle(a);
  m_schema->setFocus(Qt::OtherFocusReason);
}

void AtelierElectronique::rangerArchive(const ImportArchive& imp) {
  Article a = imp.article;
  // Le modèle 3D à côté des articles, cité en chemin relatif
  if (!imp.nomModele3D.empty()) {
    const QDir dossier = QFileInfo(cheminArticlesPerso()).dir();
    const QString rel = QStringLiteral("modeles3d/") + q(imp.nomModele3D);
    try {
      QDir().mkpath(dossier.filePath(QStringLiteral("modeles3d")));
      prom::ecrireFichierAtomique(prom::cheminFichier(dossier.filePath(rel).toStdString()), imp.modele3D);
      a.modele3D = rel.toStdString();
    } catch (const std::exception& e) {
      afficherMessage(QStringLiteral("Modèle 3D non rangé : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Avertissement);
    }
  }
  ajouterSymbolesPerso(imp.symboles);
  if (imp.empreinte) ajouterEmpreintePerso(*imp.empreinte);
  rangerArticlesPerso({a});
  afficherMessage(QStringLiteral("Article <b>%1</b> rangé : symbole, empreinte (%2 pastilles)%3, contrôles sans erreur.")
                      .arg(q(a.id).toHtmlEscaped())
                      .arg(imp.empreinte ? imp.empreinte->pastilles.size() : 0)
                      .arg(a.modele3D.empty() ? QString() : QStringLiteral(" et modèle 3D")),
                  NiveauMessage::Succes);
}

void AtelierElectronique::ouvrirChoixComposant(const QString& recherche) {
  if (!m_choixComposant) m_choixComposant = new FenetreChoixComposant(*this, this);
  else m_choixComposant->refaire();
  if (!recherche.isNull()) m_choixComposant->recherche()->setText(recherche);
  montrerAuCentre(m_choixComposant, this);
  m_choixComposant->recherche()->setFocus(Qt::ShortcutFocusReason);
  m_choixComposant->recherche()->selectAll();
}

void AtelierElectronique::ouvrirChoixEmpreinte(std::vector<std::string> composants) {
  const ConceptionElec* c = conceptionCourante();
  if (composants.empty() && c)
    for (const auto& id : m_selection->composants()) composants.push_back(id);
  if (!m_choixEmpreinte) m_choixEmpreinte = new FenetreChoixEmpreinte(*this, this);
  m_choixEmpreinte->viser(composants);
  montrerAuCentre(m_choixEmpreinte, this);
}

void AtelierElectronique::ouvrirStandardisation() {
  if (!m_standardisation) m_standardisation = new DialogueStandardisation(*this, this);
  else m_standardisation->refaire();
  montrerAuCentre(m_standardisation, this);
}

void AtelierElectronique::ouvrirMisesAJourArticles() {
  if (!m_misesAJour) m_misesAJour = new DialogueMisesAJour(*this, this);
  else m_misesAJour->refaire();
  montrerAuCentre(m_misesAJour, this);
}

int AtelierElectronique::signalerMisesAJourArticles() {
  const ConceptionElec* c = conceptionCourante();
  if (!c || c->articles.empty()) return 0;
  int articles = 0, composants = 0;
  for (const auto& a : articlesDisponibles())
    if (c->articles.count(a.id))
      if (const auto ids = composantsAMettreAJour(*c, a); !ids.empty()) {
        ++articles;
        composants += static_cast<int>(ids.size());
      }
  if (articles > 0)
    afficherMessage(QStringLiteral("%1 article(s) ont changé dans la bibliothèque (%2 composant(s) posés d’après eux). Voir les différences et mettre à jour.").arg(articles).arg(composants),
                    NiveauMessage::Information, [this] { ouvrirMisesAJourArticles(); });
  if (m_misesAJour && m_misesAJour->isVisible()) m_misesAJour->refaire();
  return articles;
}

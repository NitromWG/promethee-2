// Prométhée : sélections avancées du schéma (lot L2, tâche 2.26, SAI-38, SAI-39, ERG-28, ERG-29). Objets pris par un
// polygone, par un trait ou hors d'un cadre ; sélection inversée ; « Étendre » et « Réduire » par paliers de
// connectivité ; « Choisir les semblables… » sur des propriétés cochées, à la feuille ou au projet ; estompage de ce
// qui n'est pas choisi. Méthodes de VueSchema ; la forme, l'estompage, les mémoires et les paliers sont portés par la
// sélection partagée (SelectionElectronique), commune au schéma et au circuit.
// Licence GPL-3.0-only.
#include <algorithm>
#include <map>
#include <set>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

#include "BandeauOutil.hpp"
#include "MemoiresSelection.hpp"
#include "SelectionElectronique.hpp"
#include "VueSchema.hpp"

using namespace prom::ecad;

namespace {
// Un segment touche-t-il une ligne brisée (ou un polygone fermé) ?
bool segmentTouche(Pt a, Pt b, const Polygone& ligne, bool fermee) {
  const std::size_t n = ligne.size();
  for (std::size_t i = 0; i + 1 < n + (fermee ? 1 : 0); ++i)
    if (distanceSegments(a, b, ligne[i], ligne[(i + 1) % n]) < 1.0) return true;
  return false;
}
}  // namespace

std::vector<std::string> VueSchema::objetsDansForme(const Polygone& forme, bool trait) const {
  std::vector<std::string> r;
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f || forme.size() < 2) return r;
  // Polygone : l'objet entièrement dedans ; trait : l'objet que la ligne touche (bords d'un symbole compris)
  auto pris = [&](const std::vector<Pt>& points, bool boite) {
    if (trait) {
      if (points.size() == 1) {
        for (std::size_t i = 0; i + 1 < forme.size(); ++i)
          if (distancePointSegment(points[0], forme[i], forme[i + 1]) < 600'000) return true;
        return false;
      }
      const std::size_t n = points.size();
      for (std::size_t i = 0; i + 1 < n + (boite ? 1 : 0); ++i)
        if (segmentTouche(points[i], points[(i + 1) % n], forme, false)) return true;
      // Un trait entièrement dans la boîte d'un symbole le prend aussi
      return boite && dedans(Polygone(points.begin(), points.end()), forme.front());
    }
    return std::all_of(points.begin(), points.end(), [&](Pt p) { return dedans(forme, p); });
  };
  for (const auto& x : f->symboles)
    if (const DefSymbole* d = c->symbole(x.symbole)) {
      const Boite b = boiteSymbole(x, *d);
      if (pris({{b.x0, b.y0}, {b.x1, b.y0}, {b.x1, b.y1}, {b.x0, b.y1}}, true)) r.push_back(x.uuid);
    }
  for (const auto& w : f->fils)
    if (pris({w.a, w.b}, false)) r.push_back(w.uuid);
  for (const auto& x : f->etiquettes)
    if (pris({x.p}, false)) r.push_back(x.uuid);
  for (const auto& x : f->textes)
    if (pris({x.p}, false)) r.push_back(x.uuid);
  for (const auto& x : f->jonctions)
    if (pris({x.p}, false)) r.push_back(idJonction(x));
  for (const auto& x : f->nonConnexions)
    if (pris({x.p}, false)) r.push_back(idNonConnexion(x));
  return r;
}

std::vector<std::string> VueSchema::tousLesObjets() const {
  std::vector<std::string> r;
  const Feuille* f = feuilleCourante();
  if (!f) return r;
  for (const auto& x : f->symboles) r.push_back(x.uuid);
  for (const auto& x : f->fils) r.push_back(x.uuid);
  for (const auto& x : f->etiquettes) r.push_back(x.uuid);
  for (const auto& x : f->textes) r.push_back(x.uuid);
  for (const auto& x : f->jonctions) r.push_back(idJonction(x));
  for (const auto& x : f->nonConnexions) r.push_back(idNonConnexion(x));
  return r;
}

void VueSchema::appliquerIds(const std::vector<std::string>& ids, int mode) {
  const Feuille* f = feuilleCourante();
  if (!f) return;
  std::vector<SelectionElectronique::Ref> refs;
  for (const std::string& id : ids) {
    SelectionElectronique::Ref r;
    // Le filtre de sélection (SAI-18), et le masque : ce qui n'est pas choisi ne se prend pas (ERG-28)
    if (SelectionElectronique::refSchema(*f, id, r) && m_sel->admis(r.genre) && !(m_sel->intouchable() && !m_selection.count(id))) refs.push_back(std::move(r));
  }
  m_sel->appliquer(refs, static_cast<SelectionElectronique::Mode>(mode));
}

void VueSchema::inverserSelection() {
  std::vector<std::string> ids;
  for (const std::string& id : tousLesObjets())
    if (!m_selection.count(id)) ids.push_back(id);
  const bool masque = m_sel->intouchable();
  if (masque) m_sel->setEstompage(SelectionElectronique::Estompage::Aucun);  // l'inverse d'une sélection masquée se prend
  appliquerIds(ids, static_cast<int>(SelectionElectronique::Mode::Remplacer));
  emit message(QStringLiteral("Sélection inversée : %1 objet(s).").arg(m_selection.size()));
}

bool VueSchema::etendreSelection() {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f || !m_k || m_selection.empty()) return false;
  // Les réseaux que touche la sélection : broches des symboles choisis, fils, étiquettes
  std::set<int> reseaux;
  for (const auto& s : f->symboles)
    if (m_selection.count(s.uuid) && !s.composant.empty())
      if (const DefSymbole* d = c->symbole(s.symbole))
        for (const auto& b : brochesPlacees(s, *d))
          if (const int r = m_k->reseau(s.composant, b.numero); r >= 0) reseaux.insert(r);
  for (const auto& [id, r] : m_k->parFil)
    if (m_selection.count(id)) reseaux.insert(r);
  for (const auto& [id, r] : m_k->parEtiquette)
    if (m_selection.count(id)) reseaux.insert(r);
  for (const auto& [id, r] : m_k->parSymbole)
    if (m_selection.count(id)) reseaux.insert(r);
  auto dans = [&](const std::map<std::string, int>& m, const std::string& id) {
    const auto it = m.find(id);
    return it != m.end() && reseaux.count(it->second);
  };
  // Premier palier : les fils, étiquettes et jonctions de ces réseaux ; puis les symboles qui y ont une broche
  std::vector<SelectionElectronique::Ref> refs;
  for (const auto& w : f->fils)
    if (!m_selection.count(w.uuid) && dans(m_k->parFil, w.uuid)) refs.push_back({SelectionElectronique::Genre::Fil, w.uuid, {}});
  for (const auto& e : f->etiquettes)
    if (!m_selection.count(e.uuid) && dans(m_k->parEtiquette, e.uuid)) refs.push_back({SelectionElectronique::Genre::Etiquette, e.uuid, {}});
  for (const auto& j : f->jonctions)
    for (const auto& w : f->fils)
      if ((w.a == j.p || w.b == j.p || surSegment(j.p, w.a, w.b)) && dans(m_k->parFil, w.uuid) && !m_selection.count(idJonction(j))) {
        refs.push_back({SelectionElectronique::Genre::Jonction, idJonction(j), {}});
        break;
      }
  if (refs.empty())
    for (const auto& s : f->symboles) {
      if (m_selection.count(s.uuid)) continue;
      bool relie = dans(m_k->parSymbole, s.uuid);
      if (!s.composant.empty())
        if (const DefSymbole* d = c->symbole(s.symbole))
          for (const auto& b : brochesPlacees(s, *d)) relie |= reseaux.count(m_k->reseau(s.composant, b.numero)) > 0;
      if (relie) refs.push_back({SelectionElectronique::Genre::Symbole, s.uuid, s.composant});
    }
  if (refs.empty()) {
    emit message(QStringLiteral("Rien de plus à prendre : la sélection couvre déjà ce qui lui est relié sur la feuille."));
    return false;
  }
  m_sel->choisirPalier(refs);
  emit message(QStringLiteral("Étendue de %1 objet(s) · « Réduire » revient au palier d’avant.").arg(refs.size()));
  return true;
}

bool VueSchema::reduireSelection() { return m_sel->reduire(); }

int VueSchema::choisirSemblables(unsigned proprietes, bool projet) {
  const ConceptionElec* c = conception();
  if (!c || !m_sel) return 0;
  // Le modèle : le dernier composant choisi
  const ComposantElec* modele = nullptr;
  for (auto it = m_sel->objets().rbegin(); it != m_sel->objets().rend() && !modele; ++it)
    if (!it->composant.empty()) modele = c->composant(it->composant);
  if (!modele || proprietes == 0) return 0;
  auto semblable = [&](const ComposantElec& k) {
    if ((proprietes & SemblableArticle) && k.symbole != modele->symbole) return false;
    if ((proprietes & SemblableValeur) && k.valeur != modele->valeur) return false;
    if ((proprietes & SemblableEmpreinte) && k.empreinte != modele->empreinte) return false;
    if ((proprietes & SemblableFabricant) && (k.fabricant != modele->fabricant || k.mpn != modele->mpn)) return false;
    return true;
  };
  std::vector<SelectionElectronique::Ref> refs;
  std::set<std::string> composants;
  std::set<std::size_t> feuilles;
  for (std::size_t i = 0; i < c->feuilles.size(); ++i) {
    if (!projet && static_cast<int>(i) != m_feuille) continue;
    for (const auto& s : c->feuilles[i].symboles)
      if (const ComposantElec* k = c->composant(s.composant); k && semblable(*k)) {
        refs.push_back({SelectionElectronique::Genre::Symbole, s.uuid, s.composant});
        composants.insert(k->uuid);
        feuilles.insert(i);
      }
  }
  m_sel->choisir(refs);
  emit message(QStringLiteral("%1 composant(s) semblable(s) choisi(s), sur %2 feuille(s).").arg(composants.size()).arg(feuilles.size()));
  return static_cast<int>(composants.size());
}

void VueSchema::dialogueSemblables() {
  QDialog d(this);
  d.setObjectName(QStringLiteral("dialogueSemblables"));
  d.setWindowTitle(QStringLiteral("Choisir les semblables"));
  auto* l = new QVBoxLayout(&d);
  l->addWidget(new QLabel(QStringLiteral("Les composants qui ont, comme le composant choisi :")));
  const std::pair<unsigned, const char*> proprietes[] = {
      {SemblableArticle, "le même article"}, {SemblableValeur, "la même valeur"}, {SemblableEmpreinte, "la même empreinte"}, {SemblableFabricant, "le même fabricant et la même référence"}};
  std::vector<std::pair<unsigned, QCheckBox*>> cases;
  for (const auto& [p, nom] : proprietes) {
    auto* x = new QCheckBox(QString::fromUtf8(nom), &d);
    x->setObjectName(QStringLiteral("semblable%1").arg(p));
    x->setChecked(p == SemblableValeur || p == SemblableArticle);
    l->addWidget(x);
    cases.push_back({p, x});
  }
  auto* feuille = new QRadioButton(QStringLiteral("Sur cette feuille"), &d);
  auto* projet = new QRadioButton(QStringLiteral("Dans tout le projet"), &d);
  feuille->setObjectName(QStringLiteral("porteeFeuille"));
  projet->setObjectName(QStringLiteral("porteeProjet"));
  projet->setChecked(true);
  l->addWidget(feuille);
  l->addWidget(projet);
  auto* estomper = new QCheckBox(QStringLiteral("Estomper le reste"), &d);
  estomper->setObjectName(QStringLiteral("estomperReste"));
  estomper->setChecked(true);
  l->addWidget(estomper);
  auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
  connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  l->addWidget(b);
  if (d.exec() != QDialog::Accepted) return;
  unsigned masque = 0;
  for (const auto& [p, x] : cases)
    if (x->isChecked()) masque |= p;
  if (choisirSemblables(masque, projet->isChecked()) > 0 && estomper->isChecked() && m_sel->estompage() == SelectionElectronique::Estompage::Aucun)
    m_sel->setEstompage(SelectionElectronique::Estompage::Estomper);
}

double VueSchema::opaciteObjet(const std::string& id) const {
  if (m_impression || !m_sel || !m_sel->estompageActif() || m_selection.count(id)) return 1.0;
  return m_sel->opaciteNonChoisi();
}

void VueSchema::basculerMemoires() {
  if (!m_memoires) {
    m_memoires = new PanneauMemoires(m_sel, this);
    m_memoires->move(8, (avecBandeau() ? BandeauOutil::HAUTEUR : 0) + 8);
    m_memoires->hide();
  }
  m_memoires->setSelection(m_sel);
  m_memoires->setVisible(m_memoires->isHidden());
  m_memoires->raise();
}

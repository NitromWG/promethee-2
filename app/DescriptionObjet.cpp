// Prométhée : description d'un objet du schéma (lot L2, tâche 2.30). Licence GPL-3.0-only.
#include "DescriptionObjet.hpp"

#include <cctype>
#include <cmath>

#include <QLocale>

namespace description {
namespace {
// Le multiple d'un caractère (k, M, m, u, µ, n, p, R) et son exposant ; faux si ce n'en est pas un
bool multiple(const std::string& s, std::size_t i, int& exposant, std::size_t& longueur) {
  longueur = 1;
  switch (s[i]) {
    case 'G': exposant = 9; return true;
    case 'M': exposant = 6; return true;
    case 'k':
    case 'K': exposant = 3; return true;
    case 'R':
    case 'r': exposant = 0; return true;
    case 'm': exposant = -3; return true;
    case 'u': exposant = -6; return true;
    case 'n': exposant = -9; return true;
    case 'p': exposant = -12; return true;
    default: break;
  }
  if (s.compare(i, 2, "\xC2\xB5") == 0) {  // µ
    exposant = -6;
    longueur = 2;
    return true;
  }
  return false;
}
}  // namespace

QString valeurLisible(const std::string& valeur, const std::string& prefixe) {
  const char* unite = prefixe == "R" ? "Ω" : prefixe == "C" ? "F" : prefixe == "L" ? "H" : nullptr;
  if (!unite || valeur.empty()) return QString::fromStdString(valeur);
  // Entier, puis un multiple qui sert de virgule (4k7) ou de suffixe (4,7k, 10k), puis l'unité ou rien
  std::string s = valeur;
  std::size_t i = 0;
  std::string entier, decimales;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) entier += s[i++];
  if (i < s.size() && (s[i] == '.' || s[i] == ',')) {
    ++i;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) decimales += s[i++];
  }
  int exposant = 0;
  std::size_t l = 0;
  if (i < s.size() && multiple(s, i, exposant, l)) {
    i += l;
    if (decimales.empty())
      while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) decimales += s[i++];  // 4k7
  }
  if (entier.empty() && decimales.empty()) return QString::fromStdString(valeur);
  // Ce qui suit doit être l'unité (Ω, ohm, F, H) ou rien
  const std::string reste = s.substr(i);
  if (!reste.empty() && reste != unite && reste != "ohm" && reste != "Ohm" && reste != "F" && reste != "H") return QString::fromStdString(valeur);
  double v = std::stod((entier.empty() ? std::string("0") : entier) + (decimales.empty() ? std::string() : "." + decimales));
  // Le préfixe d'écriture : celui donné, ramené à un nombre entre 1 et 1000
  int e = exposant;
  while (v >= 1000 && e < 9) {
    v /= 1000;
    e += 3;
  }
  while (v > 0 && v < 1 && e > -12) {
    v *= 1000;
    e -= 3;
  }
  const char* prefixes = nullptr;
  switch (e) {
    case 9: prefixes = "G"; break;
    case 6: prefixes = "M"; break;
    case 3: prefixes = "k"; break;
    case 0: prefixes = ""; break;
    case -3: prefixes = "m"; break;
    case -6: prefixes = "µ"; break;
    case -9: prefixes = "n"; break;
    case -12: prefixes = "p"; break;
    default: return QString::fromStdString(valeur);
  }
  const double arrondi = std::round(v * 1000.0) / 1000.0;
  QString nombre = QLocale(QLocale::French).toString(arrondi, 'f', 3);
  while (nombre.contains(QLatin1Char(',')) && (nombre.endsWith(QLatin1Char('0')) || nombre.endsWith(QLatin1Char(','))))
    nombre.chop(1);
  return QStringLiteral("%1 %2%3").arg(nombre, QString::fromUtf8(prefixes), QString::fromUtf8(unite));
}

QString liste(const std::vector<std::string>& noms) {
  QString t;
  for (std::size_t i = 0; i < noms.size(); ++i) {
    if (i > 0) t += i + 1 == noms.size() ? QStringLiteral(" et ") : QStringLiteral(", ");
    t += QString::fromStdString(noms[i]);
  }
  return t;
}
}  // namespace description

// ---------- Description des objets du schéma, et passage d'un objet relié au suivant ----------
#include <algorithm>
#include <set>

#include "AccessibiliteCanevas.hpp"
#include "SelectionElectronique.hpp"
#include "VueSchema.hpp"

using namespace prom::ecad;

QString VueSchema::descriptionObjet(const std::string& id) const {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f || id.empty()) return {};
  for (const auto& s : f->symboles) {
    if (s.uuid != id) continue;
    const DefSymbole* d = c->symbole(s.symbole);
    if (!d) return {};
    if (d->alimentation) return QStringLiteral("Alimentation %1").arg(QString::fromStdString(s.nomAlim.empty() ? d->nom : s.nomAlim));
    const ComposantElec* k = c->composant(s.composant);
    if (!k) return QString::fromStdString(d->nom);
    // Repère (avec l'unité), valeur lisible, réseaux de ses broches dans leur ordre
    QString t = QString::fromStdString(k->reference + (d->unites > 1 ? lettreUnite(s.unite) : std::string()));
    if (!k->valeur.empty()) t += QStringLiteral(", ") + description::valeurLisible(k->valeur, d->prefixe);
    std::vector<std::string> reseaux;
    if (m_k)
      for (const auto& b : brochesPlacees(s, *d))
        if (const std::string r = m_k->nom(k->uuid, b.numero); !r.empty() && std::find(reseaux.begin(), reseaux.end(), r) == reseaux.end()) reseaux.push_back(r);
    if (!reseaux.empty()) t += (reseaux.size() == 1 ? QStringLiteral(", réseau ") : QStringLiteral(", réseaux ")) + description::liste(reseaux);
    // Les unités d'un composant à plusieurs unités, et leurs feuilles
    if (d->unites > 1) {
      std::vector<std::string> unites;
      for (std::size_t i = 0; i < c->feuilles.size(); ++i)
        for (const auto& x : c->feuilles[i].symboles)
          if (x.composant == s.composant && x.uuid != s.uuid) unites.push_back(k->reference + lettreUnite(x.unite) + " sur " + c->feuilles[i].nom);
      if (!unites.empty()) t += QStringLiteral(" ; autres unités : ") + description::liste(unites);
    }
    return t;
  }
  const std::string reseau = reseauDe(id);
  // Les feuilles où passe un réseau
  auto feuilles = [&]() {
    std::vector<std::string> noms;
    if (!m_k) return noms;
    for (const auto& fe : c->feuilles) {
      bool ici = false;
      for (const auto& w : fe.fils)
        if (const auto it = m_k->parFil.find(w.uuid); it != m_k->parFil.end() && m_k->reseaux[static_cast<std::size_t>(it->second)].nom == reseau) ici = true;
      if (ici) noms.push_back(fe.nom);
    }
    return noms;
  };
  for (const auto& w : f->fils)
    if (w.uuid == id) {
      const auto fs = feuilles();
      return QStringLiteral("Fil, réseau %1").arg(QString::fromStdString(reseau.empty() ? std::string("sans nom") : reseau)) +
             (fs.size() > 1 ? QStringLiteral(", sur les feuilles ") + description::liste(fs) : QString());
    }
  for (const auto& e : f->etiquettes)
    if (e.uuid == id) {
      const char* genre = e.type == TypeEtiquette::Locale ? "Étiquette" : e.type == TypeEtiquette::Globale ? "Étiquette globale" : "Connecteur hors feuille";
      return QStringLiteral("%1 %2").arg(QString::fromUtf8(genre), QString::fromStdString(e.texte)) + (reseau.empty() || reseau == e.texte ? QString() : QStringLiteral(", réseau ") + QString::fromStdString(reseau));
    }
  for (const auto& t : f->textes)
    if (t.uuid == id) return QStringLiteral("Texte « %1 »").arg(QString::fromStdString(t.texte));
  return libelleObjet(id);
}

QString VueSchema::descriptionTenue() const {
  if (!m_sel || m_sel->vide()) return QStringLiteral("Rien de choisi");
  const SelectionElectronique::Ref* r = m_sel->tenu();
  QString t = descriptionObjet(r->id);
  if (t.isEmpty() && !r->composant.empty())
    if (const ConceptionElec* c = conception())
      if (const ComposantElec* k = c->composant(r->composant)) t = QString::fromStdString(k->reference);
  if (m_sel->taille() > 1) t += QStringLiteral(" (et %1 autre(s) objet(s) choisi(s))").arg(m_sel->taille() - 1);
  return t;
}

QString VueSchema::nomAccessible() const {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f) return QStringLiteral("Schéma");
  return QStringLiteral("Schéma de %1, feuille %2").arg(QString::fromStdString(c->nom), QString::fromStdString(f->nom));
}

// L'objet relié suivant (2.30, ERG-34) : depuis l'objet tenu, les objets de la feuille qui partagent un de ses
// réseaux, de gauche à droite et de haut en bas ; le suivant après lui, en boucle
bool VueSchema::objetRelieSuivant() {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f || !m_k || !m_sel || m_sel->vide()) return false;
  const std::string tenu = m_sel->tenu()->id;
  // Les réseaux de l'objet tenu
  std::set<int> reseaux;
  auto reseauxDe = [&](const std::string& id) {
    std::set<int> r;
    for (const auto& s : f->symboles)
      if (s.uuid == id && !s.composant.empty())
        if (const DefSymbole* d = c->symbole(s.symbole))
          for (const auto& b : brochesPlacees(s, *d))
            if (const int n = m_k->reseau(s.composant, b.numero); n >= 0) r.insert(n);
    for (const auto* m : {&m_k->parFil, &m_k->parEtiquette, &m_k->parSymbole})
      if (const auto it = m->find(id); it != m->end()) r.insert(it->second);
    return r;
  };
  reseaux = reseauxDe(tenu);
  if (reseaux.empty()) return false;
  struct Candidat {
    std::string id;
    Pt p;
  };
  std::vector<Candidat> v;
  auto relie = [&](const std::string& id) {
    for (const int n : reseauxDe(id))
      if (reseaux.count(n)) return true;
    return false;
  };
  for (const auto& s : f->symboles)
    if (relie(s.uuid)) v.push_back({s.uuid, s.position});
  for (const auto& e : f->etiquettes)
    if (relie(e.uuid)) v.push_back({e.uuid, e.p});
  std::sort(v.begin(), v.end(), [](const Candidat& a, const Candidat& b) { return a.p.y != b.p.y ? a.p.y < b.p.y : a.p.x < b.p.x; });
  if (v.size() < 2) return false;
  std::size_t i = 0;
  while (i < v.size() && v[i].id != tenu) ++i;
  const std::string suivant = v[(i + 1) % v.size()].id;
  selectionner({suivant});
  for (const auto& x : v)
    if (x.id == suivant) centrerSur(x.p);
  emit message(descriptionObjet(suivant).toHtmlEscaped());
  return true;
}

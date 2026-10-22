// Prométhée : une seule hauteur par composant (lot L3, tâche 3.4). Voir hauteurs.hpp.
// Licence GPL-3.0-only.
#include "promethee/hauteurs.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <set>

#include "promethee/electronique.hpp"

namespace prom::ecad {

namespace {
std::string millimetres(double v) {
  char t[32];
  std::snprintf(t, sizeof t, "%.2f", v);
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  return s + " mm";
}
}  // namespace

const char* nomProvenance(ProvenanceHauteur p) {
  switch (p) {
    case ProvenanceHauteur::Generique: return "générique";
    case ProvenanceHauteur::Saisie: return "saisie";
    case ProvenanceHauteur::Fiche: return "fiche";
    case ProvenanceHauteur::ModeleExact: return "modèle exact";
  }
  return "générique";
}

const char* codeProvenance(ProvenanceHauteur p) {
  switch (p) {
    case ProvenanceHauteur::Generique: return "generique";
    case ProvenanceHauteur::Saisie: return "saisie";
    case ProvenanceHauteur::Fiche: return "fiche";
    case ProvenanceHauteur::ModeleExact: return "modele";
  }
  return "generique";
}

ProvenanceHauteur provenanceDepuisCode(const std::string& code) {
  if (code == "saisie") return ProvenanceHauteur::Saisie;
  if (code == "fiche") return ProvenanceHauteur::Fiche;
  if (code == "modele") return ProvenanceHauteur::ModeleExact;
  return ProvenanceHauteur::Generique;
}

std::string HauteurComposant::texte() const {
  if (mm <= 0) return "pas de corps";
  const char* p = provenance == ProvenanceHauteur::ModeleExact ? "modèle" : nomProvenance(provenance);
  return millimetres(mm) + " · " + p;
}

double hauteurGenerique(const DefEmpreinte& d) { return d.corps.hauteur > 0 ? mm(d.corps.hauteur) : 0.0; }

bool empreinteCms(const DefEmpreinte& d) {
  return std::none_of(d.pastilles.begin(), d.pastilles.end(), [](const Pastille& p) { return p.percage > 0; });
}

HauteurComposant hauteurComposant(const ComposantElec* x, const DefEmpreinte* d) {
  HauteurComposant h;
  if (!d) return h;
  const double generique = hauteurGenerique(*d);
  if (x && x->hauteur > 0) {
    h.mm = x->hauteur;
    h.provenance = x->provenanceHauteur == ProvenanceHauteur::Generique ? ProvenanceHauteur::Saisie : x->provenanceHauteur;
    h.source = !x->sourceHauteur.empty() ? x->sourceHauteur : h.provenance == ProvenanceHauteur::Fiche ? "confirmée sur la fiche technique" : "tapée dans l'inspecteur";
    return h;
  }
  h.mm = generique > 0 ? generique : 0.1;
  h.provenance = ProvenanceHauteur::Generique;
  h.source = generique > 0 ? "corps de " + d->id : "pastille plate, sans corps";
  // Hauteur générique confirmée sur la fiche (case cochée sans hauteur tapée)
  if (x && x->provenanceHauteur == ProvenanceHauteur::Fiche && generique > 0) {
    h.provenance = ProvenanceHauteur::Fiche;
    h.source = !x->sourceHauteur.empty() ? x->sourceHauteur : "corps de " + d->id + ", confirmé sur la fiche technique";
  }
  return h;
}

HauteurComposant hauteurComposant(const ConceptionElec& c, const ComposantElec& x) { return hauteurComposant(&x, c.empreinte(x.empreinte)); }

std::string hauteurAConfirmer(double v, const DefEmpreinte* d) {
  if (v <= 0) return {};
  char t[200];
  if (d && empreinteCms(*d) && v > 25) {
    std::snprintf(t, sizeof t, "%s pour un boîtier CMS : c'est beaucoup. Confirmer ?", millimetres(v).c_str());
    return t;
  }
  const double g = d ? hauteurGenerique(*d) : 0;
  if (g > 0 && v > 3 * g) {
    std::snprintf(t, sizeof t, "%s, plus de trois fois la hauteur générique (%s). Confirmer ?", millimetres(v).c_str(), millimetres(g).c_str());
    return t;
  }
  return {};
}

Pt centreCorps(const DefEmpreinte& d) { return d.corps.decalage; }

std::optional<CorpsPose> corpsPose(const ConceptionElec& c, const EmpreintePlacee& e) {
  const DefEmpreinte* d = c.empreinte(e.empreinte);
  if (!d) return std::nullopt;
  CorpsPose r;
  r.hauteur = hauteurComposant(c.composant(e.composant), d);
  if (r.hauteur.mm <= 0) return std::nullopt;
  const Boite emprise = empriseCorps(*d);
  r.centre = versCarte(e, emprise.centre());
  r.largeur = mm(emprise.largeur());
  r.profondeur = mm(emprise.hauteur());
  r.rotation = e.rotation;
  r.rond = d->corps.rond && d->corps.largeur > 0;
  r.dessous = e.dessous;
  r.couleur = d->corps.couleur;
  return r;
}

std::optional<double> trouDeFixation(const DefEmpreinte& d) {
  int grands = 0;
  double percage = 0;
  std::set<std::string> numeros;
  for (const auto& p : d.pastilles) {
    if (!p.numero.empty()) numeros.insert(p.numero);
    if (p.percage >= 2'000'000) {
      ++grands;
      percage = mm(p.percage);
    }
  }
  if (grands != 1 || numeros.size() > 1) return std::nullopt;
  return percage;
}

std::string visDuPercage(double v) {
  static const std::pair<double, const char*> t[] = {{2.2, "M2"}, {2.7, "M2.5"}, {3.2, "M3"}, {4.3, "M4"}, {5.3, "M5"}, {6.4, "M6"}};
  const char* r = nullptr;
  double ecart = 0.3 + 1e-9;
  for (const auto& [d, nom] : t)
    if (std::abs(d - v) < ecart) {
      ecart = std::abs(d - v);
      r = nom;
    }
  return r ? r : std::string();
}

bool reseauDeFixation(const std::string& reseau) {
  if (reseau.empty() || reseau.rfind("N-", 0) == 0) return true;
  std::string m;
  for (char x : reseau) m += static_cast<char>(std::toupper(static_cast<unsigned char>(x)));
  for (const char* masse : {"GND", "MASSE", "EARTH", "CHASSIS", "CHÂSSIS", "SHIELD"})
    if (m.find(masse) != std::string::npos) return true;
  return m == "PE" || m == "VSS" || m == "0V";
}

Pt decalageParDefaut(const DefEmpreinte& d) {
  if (d.encombrement.vide()) return {};
  const Pt c = d.encombrement.centre();
  auto arrondi = [](Nm v) { return static_cast<Nm>(std::llround(static_cast<double>(v) / 1000.0) * 1000); };
  return {arrondi(c.x), arrondi(c.y)};
}

}  // namespace prom::ecad

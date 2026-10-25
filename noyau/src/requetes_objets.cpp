// Prométhée : requêtes sur les objets de la carte. Voir requetes_objets.hpp. Licence GPL-3.0-only.
#include "promethee/requetes_objets.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "promethee/expressions.hpp"
#include "promethee/regles_carte.hpp"

namespace prom::ecad {

std::string ObjetsVises::texte() const {
  std::string t;
  auto ajouter = [&](int n, const char* un, const char* plusieurs) {
    if (n) t += (t.empty() ? "" : ", ") + std::to_string(n) + " " + (n > 1 ? plusieurs : un);
  };
  ajouter(pistes, "piste", "pistes");
  ajouter(vias, "via", "vias");
  ajouter(pastilles, "pastille", "pastilles");
  ajouter(zones, "zone", "zones");
  ajouter(composants, "composant", "composants");
  return t.empty() ? std::string("aucun objet") : t;
}

ObjetsVises objetsVises(const ConceptionElec& c, const std::string& requete, std::string* erreur) {
  ObjetsVises r;
  const Expression e = Expression::compiler(requete);
  if (!e.valide()) {
    if (erreur) *erreur = e.erreur();
    return r;
  }
  // Une requête qui ne nomme pas le genre vise tous les objets ; « genre == 'piste' » les seules pistes
  for (const auto& p : c.circuit.pistes)
    if (e.evaluer(proprietesPiste(c, p))) ++r.pistes, r.uuids.push_back(p.uuid);
  for (const auto& v : c.circuit.vias)
    if (e.evaluer(proprietesVia(c, v))) ++r.vias, r.uuids.push_back(v.uuid);
  for (const auto& z : c.circuit.zones) {
    const Proprietes base = proprietesReseau(c, z.reseau);
    const Proprietes pz = [&](const std::string& nom) -> std::optional<ValeurExpression> {
      if (nom == "genre" || nom == "type") return ValeurExpression{std::string("zone")};
      if (nom == "couche" || nom == "layer") return ValeurExpression{std::string(codeCouche(z.couche))};
      if (nom == "interne") return ValeurExpression{estCuivreInterne(z.couche) ? 1.0 : 0.0};
      if (nom == "externe") return ValeurExpression{estCuivreInterne(z.couche) ? 0.0 : 1.0};
      if (nom == "priorite") return ValeurExpression{static_cast<double>(z.priorite)};
      return base(nom);
    };
    if (e.evaluer(pz)) ++r.zones, r.uuids.push_back(z.uuid);
  }
  const Connectivite k = connectivite(c);
  for (const auto& ep : c.circuit.empreintes) {
    const DefEmpreinte* d = c.empreinte(ep.empreinte);
    const ComposantElec* x = c.composant(ep.composant);
    if (!d || !x) continue;
    for (const auto& pa : d->pastilles) {
      const std::string reseau = k.nom(x->uuid, pa.numero);
      const Proprietes base = proprietesReseau(c, reseau);
      const Couches cc = couchesPastille(pa, ep.dessous, c.circuit.empilage.couchesCuivre);
      const Proprietes pp = [&](const std::string& nom) -> std::optional<ValeurExpression> {
        if (nom == "genre" || nom == "type") return ValeurExpression{std::string("pastille")};
        if (nom == "reference") return ValeurExpression{x->reference};
        if (nom == "numero" || nom == "broche") return ValeurExpression{nom == "broche" ? x->reference + "." + pa.numero : pa.numero};
        if (nom == "couche" || nom == "layer") {
          if (cc & bit(Couche::CuivreDessus)) return ValeurExpression{std::string(codeCouche(Couche::CuivreDessus))};
          if (cc & bit(Couche::CuivreDessous)) return ValeurExpression{std::string(codeCouche(Couche::CuivreDessous))};
          return std::nullopt;
        }
        if (nom == "externe") return ValeurExpression{(cc & (bit(Couche::CuivreDessus) | bit(Couche::CuivreDessous))) ? 1.0 : 0.0};
        if (nom == "interne") return ValeurExpression{(cc & (bit(Couche::CuivreDessus) | bit(Couche::CuivreDessous))) ? 0.0 : 1.0};
        if (nom == "largeur") return ValeurExpression{static_cast<double>(pa.largeur)};
        if (nom == "hauteur") return ValeurExpression{static_cast<double>(pa.hauteur)};
        return base(nom);
      };
      if (!reseau.empty() && e.evaluer(pp)) ++r.pastilles;
    }
    if (e.evaluer(proprietesComposant(c, *x))) ++r.composants, r.uuids.push_back(x->uuid);
  }
  return r;
}

std::uint64_t couchesExternes() { return bit(Couche::CuivreDessus) | bit(Couche::CuivreDessous); }

Contrainte regleDepuisRequetes(const std::string& nom, GenreContrainte genre, const std::string& requeteA, const std::string& requeteB, Nm minimum, std::uint64_t couches) {
  Contrainte k;
  k.nom = nom;
  k.genre = genre;
  k.portee = requeteA;
  k.porteeB = requeteB;
  k.valeur.min = minimum;
  k.couches = couches;
  k.source = "requête de l'éditeur";
  return k;
}

void HistoriqueRequetes::noter(const std::string& q, std::size_t garder) {
  if (q.empty()) return;
  recentes.erase(std::remove(recentes.begin(), recentes.end(), q), recentes.end());
  recentes.insert(recentes.begin(), q);
  if (recentes.size() > garder) recentes.resize(garder);
}

void HistoriqueRequetes::basculerFavori(const std::string& q) {
  const auto it = std::find(favoris.begin(), favoris.end(), q);
  if (it == favoris.end()) favoris.push_back(q);
  else favoris.erase(it);
}

std::string HistoriqueRequetes::texte() const { return nlohmann::ordered_json{{"recentes", recentes}, {"favoris", favoris}}.dump(); }

void HistoriqueRequetes::lire(const std::string& t) {
  const auto j = nlohmann::ordered_json::parse(t, nullptr, false);
  if (!j.is_object()) return;
  recentes.clear();
  favoris.clear();
  for (const char* k : {"recentes", "favoris"})
    if (j.contains(k) && j[k].is_array())
      for (const auto& x : j[k])
        if (x.is_string()) (std::string(k) == "recentes" ? recentes : favoris).push_back(x.get<std::string>());
}

const std::vector<std::pair<std::string, std::string>>& exemplesRequetes() {
  static const std::vector<std::pair<std::string, std::string>> v = {
      {"classe == 'Secteur' && externe == 1", "cuivres de la classe Secteur sur les deux faces"},
      {"genre == 'piste' && largeur < 0.2mm", "pistes plus fines que 0,2 mm"},
      {"genre == 'via' && reseau == 'GND'", "vias de la masse"},
      {"dans(reseau, 'SDA', 'SCL')", "bus I2C"},
      {"genre == 'pastille' && commence(reference, 'U')", "pastilles des circuits intégrés"},
      {"genre == 'piste' && couche ~ 'In*'", "pistes des couches internes"},
      {"prefixe == 'C' && boitier == '0402'", "condensateurs en 0402"},
  };
  return v;
}

}  // namespace prom::ecad

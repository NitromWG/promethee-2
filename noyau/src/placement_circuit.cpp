// Prométhée : placement au circuit, « À placer » et disposition en cercle (lot L5). Licence GPL-3.0-only.
#include "promethee/placement_circuit.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numbers>
#include <set>

#include "promethee/synchronisation.hpp"

namespace prom::ecad {

namespace {
int numeroRepere(const std::string& r, const std::string& prefixe) {
  if (r.size() <= prefixe.size() || r.compare(0, prefixe.size(), prefixe) != 0) return -1;
  for (std::size_t i = prefixe.size(); i < r.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(r[i]))) return -1;
  return std::stoi(r.substr(prefixe.size()));
}
}  // namespace

std::vector<GroupeAPlacer> groupesAPlacer(const ConceptionElec& c) {
  std::map<std::string, std::size_t> feuilleDe;
  for (std::size_t f = 0; f < c.feuilles.size(); ++f)
    for (const auto& s : c.feuilles[f].symboles)
      if (!s.composant.empty() && !feuilleDe.count(s.composant)) feuilleDe[s.composant] = f;
  std::vector<GroupeAPlacer> r(c.feuilles.size());
  for (std::size_t f = 0; f < c.feuilles.size(); ++f) r[f].nom = c.feuilles[f].nom;
  GroupeAPlacer sansFeuille{"Sans schéma", {}};
  std::vector<std::pair<std::string, std::string>> tri;  // repère, uuid
  for (const auto& e : c.circuit.empreintes)
    if (aPlacer(c, e))
      if (const ComposantElec* k = c.composant(e.composant)) tri.push_back({k->reference, k->uuid});
  std::sort(tri.begin(), tri.end(), [](const auto& a, const auto& b) {
    // R2 avant R10
    std::string pa, pb;
    for (char ch : a.first)
      if (!std::isdigit(static_cast<unsigned char>(ch))) pa += ch;
    for (char ch : b.first)
      if (!std::isdigit(static_cast<unsigned char>(ch))) pb += ch;
    if (pa != pb) return pa < pb;
    return numeroRepere(a.first, pa) < numeroRepere(b.first, pb);
  });
  for (const auto& [ref, id] : tri) {
    if (const auto it = feuilleDe.find(id); it != feuilleDe.end()) r[it->second].composants.push_back(id);
    else sansFeuille.composants.push_back(id);
  }
  if (!sansFeuille.composants.empty()) r.push_back(sansFeuille);
  std::erase_if(r, [](const GroupeAPlacer& g) { return g.composants.empty(); });
  return r;
}

int placerGroupe(ConceptionElec& c, const std::vector<std::string>& composants, Pt coin, Nm ecart) {
  exigerModifiable(c);
  const Boite carte = boite(c.circuit.contour.polygone());
  const Nm droite = carte.vide() ? coin.x + 100 * NM_PAR_MM : carte.x1 - c.circuit.regles.bord;
  // Encombrements déjà sur la carte : un composant posé ne recouvre personne
  std::vector<Boite> pris;
  std::set<std::string> aPoser(composants.begin(), composants.end());
  for (const auto& e : c.circuit.empreintes)
    if (!aPoser.count(e.composant) && !aPlacer(c, e))
      if (const DefEmpreinte* d = c.empreinte(e.empreinte)) pris.push_back(boite(encombrementSurCarte(e, *d)));
  Nm x = coin.x, y = coin.y, hauteurLigne = 0;
  int n = 0;
  for (const auto& id : composants)
    for (auto& e : c.circuit.empreintes) {
      if (e.composant != id || e.verrou) continue;
      const DefEmpreinte* d = c.empreinte(e.empreinte);
      if (!d) continue;
      e.rotation = 0;
      e.dessous = false;
      const Boite enc = d->encombrement.vide() ? Boite{-2'500'000, -2'500'000, 2'500'000, 2'500'000} : d->encombrement;
      for (int essai = 0; essai < 400; ++essai) {
        if (x + enc.largeur() > droite && x > coin.x) {
          x = coin.x;
          y += hauteurLigne + ecart;
          hauteurLigne = 0;
        }
        const Pt pos = surGrille({x - enc.x0, y - enc.y0}, 50'000);
        Boite ici{pos.x + enc.x0, pos.y + enc.y0, pos.x + enc.x1, pos.y + enc.y1};
        const bool recouvre = std::any_of(pris.begin(), pris.end(), [&](const Boite& b) { return b.gonflee(ecart / 2).touche(ici); });
        if (!recouvre) {
          e.position = pos;
          pris.push_back(ici);
          x = ici.x1 + ecart;
          hauteurLigne = std::max(hauteurLigne, enc.hauteur());
          ++n;
          break;
        }
        x += ecart;
      }
    }
  return n;
}

Pt placeSurCercle(const OptionsCercle& o, std::size_t i, std::size_t n) {
  const bool plein = std::abs(o.balayage) >= 359.999;
  const double pasAngle = n <= 1 ? 0 : plein ? o.balayage / static_cast<double>(n) : o.balayage / static_cast<double>(n - 1);
  const double t = (o.angleDepart + pasAngle * static_cast<double>(i)) * std::numbers::pi / 180;
  return {o.centre.x + static_cast<Nm>(std::llround(static_cast<double>(o.rayon) * std::cos(t))), o.centre.y + static_cast<Nm>(std::llround(static_cast<double>(o.rayon) * std::sin(t)))};
}

double rotationSurCercle(const OptionsCercle& o, std::size_t i, std::size_t n) {
  const bool plein = std::abs(o.balayage) >= 359.999;
  const double pasAngle = n <= 1 ? 0 : plein ? o.balayage / static_cast<double>(n) : o.balayage / static_cast<double>(n - 1);
  const double theta = o.angleDepart + pasAngle * static_cast<double>(i);
  // Le haut de l'empreinte (−y local) tourné vers le centre : rotation 90° − θ (repère à l'écran)
  double r = (o.versCentre ? 90 - theta : 0) + o.rotationPropre;
  r = std::fmod(r, 360.0);
  if (r < 0) r += 360;
  if (std::abs(r - 360) < 1e-9) r = 0;
  return r;
}

int disposerEnCercle(ConceptionElec& c, const std::vector<std::string>& composants, const OptionsCercle& o) {
  exigerModifiable(c);
  const std::size_t n = composants.size();
  int faits = 0;
  for (std::size_t i = 0; i < n; ++i)
    for (auto& e : c.circuit.empreintes)
      if (e.composant == composants[i] && !e.verrou) {
        e.position = placeSurCercle(o, i, n);
        e.rotation = rotationSurCercle(o, i, n);
        ++faits;
      }
  if (!o.renumeroter || n == 0) return faits;
  std::string prefixe = o.prefixe;
  if (prefixe.empty())
    if (const ComposantElec* k = c.composant(composants.front()))
      for (char ch : k->reference) {
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '?') break;
        prefixe += ch;
      }
  std::set<int> pris;
  const std::set<std::string> dans(composants.begin(), composants.end());
  for (const auto& k : c.composants)
    if (!dans.count(k.uuid))
      if (const int x = numeroRepere(k.reference, prefixe); x > 0) pris.insert(x);
  int suivant = 1;
  for (const auto& id : composants)
    if (ComposantElec* k = c.composant(id)) {
      while (pris.count(suivant)) ++suivant;
      k->reference = prefixe + std::to_string(suivant++);
    }
  return faits;
}

}  // namespace prom::ecad

// Prométhée : bus, feuilles de canal, déplacement vers une feuille (lot L7). Licence GPL-3.0-only.
#include "promethee/structure_schema.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>

#include "promethee/synchronisation.hpp"

namespace prom::ecad {

std::optional<NomBus> lireNomBus(const std::string& texte) {
  if (texte.find('[') == std::string::npos) return std::nullopt;
  static const std::regex re(R"(^([A-Za-z_][A-Za-z0-9_]*)\[(\d+)\.\.(\d+)\]$)");
  std::smatch m;
  if (!std::regex_match(texte, m, re)) return std::nullopt;
  NomBus b;
  b.prefixe = m[1].str();
  b.debut = std::stoi(m[2].str());
  b.fin = std::stoi(m[3].str());
  if (b.debut > b.fin) std::swap(b.debut, b.fin);
  return b;
}

std::vector<BusFeuille> busDeFeuille(const Feuille& f) {
  std::vector<BusFeuille> r;
  auto toucheBout = [](const Fil& w, Pt p) { return w.a == p || w.b == p; };
  auto touche = [](const Fil& w, Pt p) { return w.a == p || w.b == p || surSegment(p, w.a, w.b); };
  // Points où une étiquette non-bus est posée : un fil qui en porte une est une entrée, pas le bus
  std::vector<Pt> membres;
  for (const auto& e : f.etiquettes)
    if (!lireNomBus(e.texte)) membres.push_back(e.p);
  for (const auto& e : f.etiquettes) {
    const auto nom = lireNomBus(e.texte);
    if (!nom) continue;
    BusFeuille b;
    b.nom = *nom;
    b.etiquette = e.uuid;
    std::vector<const Fil*> pile;
    for (const auto& w : f.fils)
      if (touche(w, e.p)) pile.push_back(&w);
    while (!pile.empty()) {
      const Fil* w = pile.back();
      pile.pop_back();
      if (b.fils.count(w->uuid)) continue;
      if (std::any_of(membres.begin(), membres.end(), [&](Pt p) { return toucheBout(*w, p); })) continue;
      b.fils.insert(w->uuid);
      for (const auto& x : f.fils)
        if (!b.fils.count(x.uuid) && (toucheBout(x, w->a) || toucheBout(x, w->b))) pile.push_back(&x);
    }
    // Entrées : les fils hors du bus dont un bout tombe sur lui ; le nom qu'elles portent
    for (const auto& w : f.fils) {
      if (b.fils.count(w.uuid)) continue;
      bool surBus = false;
      for (const auto& x : f.fils)
        if (b.fils.count(x.uuid) && (touche(x, w.a) || touche(x, w.b))) surBus = true;
      if (!surBus) continue;
      std::string porte;
      for (const auto& et : f.etiquettes)
        if (!lireNomBus(et.texte) && touche(w, et.p)) porte = et.texte;
      b.entrees.push_back({w.uuid, porte});
    }
    r.push_back(std::move(b));
  }
  return r;
}

namespace {
std::string prefixeDe(const std::string& ref) {
  std::string p;
  for (char ch : ref) {
    if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '?') break;
    p += ch;
  }
  return p;
}
int numeroDe(const std::string& ref) {
  std::string n;
  for (char ch : ref)
    if (std::isdigit(static_cast<unsigned char>(ch))) n += ch;
  return n.empty() ? 0 : std::stoi(n.substr(0, 9));
}
}  // namespace

int repeterFeuille(ConceptionElec& c, std::size_t feuille, int exemplaires) {
  exigerModifiable(c);
  if (feuille >= c.feuilles.size() || exemplaires < 2) return 0;
  // Repères du canal source d'abord : sans « ? »
  if (std::any_of(c.composants.begin(), c.composants.end(), [](const ComposantElec& k) { return k.reference.find('?') != std::string::npos; })) annoter(c, false);
  Feuille& source = c.feuilles[feuille];
  source.repetitions = exemplaires;
  const Feuille modele = source;
  int faites = 0;
  for (int n = 2; n <= exemplaires; ++n) {
    Feuille copie = modele;
    copie.uuid = nouvelUuid();
    copie.nom = modele.nom + " (canal " + std::to_string(n) + ")";
    copie.repetitions = 0;
    copie.modele = modele.uuid;
    copie.instance = n;
    std::map<std::string, std::string> composants;  // source → copie
    for (auto& s : copie.symboles) {
      s.uuid = nouvelUuid();
      if (s.composant.empty()) continue;
      auto it = composants.find(s.composant);
      if (it == composants.end()) {
        const ComposantElec* k = c.composant(s.composant);
        if (!k) continue;
        ComposantElec x = *k;
        x.uuid = nouvelUuid();
        x.modeleComposant = k->uuid;
        x.reference = prefixeDe(k->reference) + std::to_string(n * 100 + numeroDe(k->reference) % 100);
        it = composants.emplace(s.composant, x.uuid).first;
        c.composants.push_back(x);
      }
      s.composant = it->second;
    }
    for (auto& w : copie.fils) w.uuid = nouvelUuid();
    for (auto& j : copie.jonctions) j.uuid = nouvelUuid();
    for (auto& x : copie.nonConnexions) x.uuid = nouvelUuid();
    for (auto& e : copie.etiquettes) e.uuid = nouvelUuid();
    for (auto& t : copie.textes) t.uuid = nouvelUuid();
    c.feuilles.push_back(copie);
    ++faites;
  }
  return faites;
}

int reproduireCanal(ConceptionElec& c, std::size_t feuilleSource, Pt ecart) {
  exigerModifiable(c);
  if (feuilleSource >= c.feuilles.size()) return 0;
  const std::string uuidSource = c.feuilles[feuilleSource].uuid;
  const Connectivite k = connectivite(c);
  // Composants du canal source et leurs empreintes
  std::set<std::string> source;
  for (const auto& s : c.feuilles[feuilleSource].symboles)
    if (!s.composant.empty()) source.insert(s.composant);
  std::map<std::string, const EmpreintePlacee*> empreinte;
  for (const auto& e : c.circuit.empreintes) empreinte[e.composant] = &e;
  // Réseaux propres au canal : toutes leurs broches sur des composants du canal source
  std::map<std::string, std::pair<std::string, std::string>> brocheDuReseau;  // nom → (composant, broche)
  for (const auto& r : k.reseaux) {
    if (r.broches.empty()) continue;
    const bool propre = std::all_of(r.broches.begin(), r.broches.end(), [&](const NoeudBroche& b) { return source.count(b.composant) > 0; });
    if (propre && r.alimentations.empty()) brocheDuReseau[r.nom] = {r.broches.front().composant, r.broches.front().broche};
  }
  std::vector<Piste> pistesSource;
  std::vector<Via> viasSource;
  for (const auto& p : c.circuit.pistes)
    if (brocheDuReseau.count(k.canonique(p.reseau))) pistesSource.push_back(p);
  for (const auto& v : c.circuit.vias)
    if (brocheDuReseau.count(k.canonique(v.reseau))) viasSource.push_back(v);
  int placees = 0;
  for (const auto& f : c.feuilles) {
    if (f.modele != uuidSource || f.instance < 2) continue;
    const Pt d{ecart.x * (f.instance - 1), ecart.y * (f.instance - 1)};
    std::map<std::string, std::string> copieDe;  // composant source → composant de cette copie
    for (const auto& s : f.symboles)
      if (const ComposantElec* x = c.composant(s.composant); x && !x->modeleComposant.empty()) copieDe[x->modeleComposant] = x->uuid;
    for (auto& e : c.circuit.empreintes) {
      const ComposantElec* x = c.composant(e.composant);
      if (!x || x->modeleComposant.empty() || !copieDe.count(x->modeleComposant) || copieDe[x->modeleComposant] != x->uuid) continue;
      const auto it = empreinte.find(x->modeleComposant);
      if (it == empreinte.end() || e.verrou) continue;
      e.position = it->second->position + d;
      e.rotation = it->second->rotation;
      e.dessous = it->second->dessous;
      e.repere = it->second->repere;
      ++placees;
    }
    // Pistes et vias des réseaux propres, sur le réseau correspondant de la copie
    auto reseauCopie = [&](const std::string& nom) -> std::string {
      const auto it = brocheDuReseau.find(k.canonique(nom));
      if (it == brocheDuReseau.end() || !copieDe.count(it->second.first)) return {};
      return k.nom(copieDe[it->second.first], it->second.second);
    };
    for (const auto& p : pistesSource) {
      const std::string n = reseauCopie(p.reseau);
      if (n.empty()) continue;
      c.circuit.pistes.push_back({nouvelUuid(), n, p.couche, p.a + d, p.b + d, p.largeur});
    }
    for (const auto& v : viasSource) {
      const std::string n = reseauCopie(v.reseau);
      if (n.empty()) continue;
      Via x = v;
      x.uuid = nouvelUuid();
      x.reseau = n;
      x.p = v.p + d;
      c.circuit.vias.push_back(x);
    }
  }
  return placees;
}

BilanDeplacement deplacerVersFeuille(ConceptionElec& c, std::size_t source, const std::set<std::string>& symboles, std::size_t cible) {
  exigerModifiable(c);
  BilanDeplacement b;
  if (source >= c.feuilles.size() || cible >= c.feuilles.size() || source == cible || symboles.empty()) return b;
  const Connectivite k = connectivite(c);
  Feuille& S = c.feuilles[source];
  Feuille& T = c.feuilles[cible];
  // Broches des symboles déplacés
  std::vector<std::pair<Pt, std::string>> broches;  // point, nom du réseau
  std::set<Pt> pointsDeplaces;
  for (const auto& s : S.symboles) {
    if (!symboles.count(s.uuid)) continue;
    const DefSymbole* d = c.symbole(s.symbole);
    if (!d) continue;
    for (const auto& bp : brochesPlacees(s, *d)) {
      const std::string n = s.composant.empty() ? std::string() : k.nom(s.composant, bp.numero);
      broches.push_back({bp.p, n});
      pointsDeplaces.insert(bp.p);
    }
  }
  // Fils qui ne relient que des broches déplacées (et d'autres fils de ce groupe) : ils partent avec elles
  std::set<std::string> filsPartants;
  {
    std::set<Pt> autres;  // points d'éléments qui restent : broches, étiquettes
    for (const auto& s : S.symboles) {
      if (symboles.count(s.uuid)) continue;
      if (const DefSymbole* d = c.symbole(s.symbole))
        for (const auto& bp : brochesPlacees(s, *d)) autres.insert(bp.p);
    }
    for (const auto& e : S.etiquettes) autres.insert(e.p);
    for (bool encore = true; encore;) {
      encore = false;
      for (const auto& w : S.fils) {
        if (filsPartants.count(w.uuid)) continue;
        auto lie = [&](Pt p) {
          if (pointsDeplaces.count(p)) return true;
          for (const auto& x : S.fils)
            if (filsPartants.count(x.uuid) && (x.a == p || x.b == p)) return true;
          return false;
        };
        auto reste = [&](Pt p) {
          if (autres.count(p)) return true;
          for (const auto& x : S.fils)
            if (!filsPartants.count(x.uuid) && x.uuid != w.uuid && (x.a == p || x.b == p || surSegment(p, x.a, x.b))) return true;
          return false;
        };
        if (lie(w.a) && lie(w.b) && !reste(w.a) && !reste(w.b)) {
          filsPartants.insert(w.uuid);
          encore = true;
        }
      }
    }
  }
  // Points où une liaison est coupée : broche déplacée reliée à quelque chose qui reste
  std::vector<std::pair<Pt, std::string>> coupures;
  for (const auto& [p, n] : broches) {
    if (n.empty()) continue;
    const int r = k.indice(n);
    if (r < 0) continue;
    // Le réseau touche-t-il autre chose que le groupe déplacé ?
    bool ailleurs = false;
    for (const auto& nb : k.reseaux[static_cast<std::size_t>(r)].broches) {
      bool dansGroupe = false;
      for (const auto& s : S.symboles)
        if (symboles.count(s.uuid) && s.composant == nb.composant) dansGroupe = true;
      if (!dansGroupe) ailleurs = true;
    }
    if (!k.reseaux[static_cast<std::size_t>(r)].noms.empty()) ailleurs = true;
    if (ailleurs) coupures.push_back({p, n});
  }
  std::set<std::string> nomsCoupes;
  for (const auto& [p, n] : coupures) {
    // Côté cible : l'étiquette sur la broche déplacée
    Etiquette e;
    e.uuid = nouvelUuid();
    e.texte = n;
    e.p = p;
    e.type = TypeEtiquette::Globale;
    T.etiquettes.push_back(e);
    ++b.etiquettes;
    // Côté source : une étiquette au même point, sur le fil resté (une par réseau suffit à le garder relié)
    bool filReste = false;
    for (const auto& w : S.fils)
      if (!filsPartants.count(w.uuid) && (w.a == p || w.b == p || surSegment(p, w.a, w.b))) filReste = true;
    if (filReste) {
      Etiquette s = e;
      s.uuid = nouvelUuid();
      S.etiquettes.push_back(s);
      ++b.etiquettes;
    }
    nomsCoupes.insert(n);
  }
  // Le déplacement : symboles et fils partants, mêmes UUID, mêmes positions
  for (auto it = S.symboles.begin(); it != S.symboles.end();) {
    if (symboles.count(it->uuid)) {
      T.symboles.push_back(*it);
      it = S.symboles.erase(it);
      ++b.symboles;
    } else
      ++it;
  }
  for (auto it = S.fils.begin(); it != S.fils.end();) {
    if (filsPartants.count(it->uuid)) {
      T.fils.push_back(*it);
      it = S.fils.erase(it);
      ++b.fils;
    } else
      ++it;
  }
  return b;
}

std::optional<double> tensionReseau(const Reseau& r) {
  std::optional<double> v;
  for (const auto& a : r.alimentations) {
    std::string n;
    for (char ch : a) n += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (n == "GND" || n == "GNDA" || n == "GNDD" || n == "VSS" || n == "0V" || n == "AGND" || n == "DGND" || n == "GNDPWR" || n == "EARTH") return 0.0;
    static const std::regex re(R"(^([+-]?)(\d+)(?:[V.,](\d*))?V?$)");
    std::smatch m;
    if (std::regex_match(n, m, re)) {
      double x = std::stod(m[2].str() + (m[3].matched && !m[3].str().empty() ? "." + m[3].str() : std::string()));
      if (m[1].str() == "-") x = -x;
      v = x;
    }
  }
  return v;
}

}  // namespace prom::ecad

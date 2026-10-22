// Prométhée : équipe, cycle de vie et exigences (lot L19). Licence GPL-3.0-only.
#include "promethee/equipe.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "promethee/modele.hpp"
#include "promethee/variantes.hpp"

namespace prom {

namespace {
using J = nlohmann::ordered_json;
const std::string LETTRES = "ABCDEFGHJKLMNPRTUVWY";  // sans I, O, Q, S, X, Z
std::string chiffre(double v, int d = 2) {
  char s[40];
  std::snprintf(s, sizeof s, "%.*f", d, v);
  std::string r = s;
  std::replace(r.begin(), r.end(), '.', ',');
  return r;
}
// « 40 € », « 3 W », « 25 cm² », « 120 » → nombre et unité
std::pair<double, std::string> seuilDe(const std::string& t0) {
  std::string t;
  for (char ch : t0)
    if (ch != ' ') t += ch == ',' ? '.' : ch;
  char* fin = nullptr;
  const double v = std::strtod(t.c_str(), &fin);
  return {v, fin ? std::string(fin) : std::string()};
}
}  // namespace

const char* nomRole(Role r) {
  switch (r) {
    case Role::Responsable: return "responsable";
    case Role::Concepteur: return "concepteur";
    case Role::Relecteur: return "relecteur";
    case Role::Observateur: return "observateur";
  }
  return "observateur";
}
std::optional<Role> roleDepuis(const std::string& t) {
  for (Role r : {Role::Responsable, Role::Concepteur, Role::Relecteur, Role::Observateur})
    if (t == nomRole(r)) return r;
  return std::nullopt;
}
const Personne* Equipe::personne(const std::string& n) const {
  for (const auto& p : personnes)
    if (p.nom == n) return &p;
  return nullptr;
}

Equipe lireEquipe(const std::string& texte) {
  const J o = J::parse(texte, nullptr, false);
  if (!o.is_object()) throw std::runtime_error("fichier d'équipe illisible");
  Equipe e;
  e.nom = o.value("nom", std::string());
  if (o.contains("personnes") && o["personnes"].is_array())
    for (const auto& x : o["personnes"]) {
      if (!x.is_object() || x.value("nom", std::string()).empty()) continue;
      Personne p;
      p.nom = x.value("nom", std::string());
      p.courriel = x.value("courriel", std::string());
      p.cle = x.value("cle", std::string());
      p.role = roleDepuis(x.value("role", std::string("observateur"))).value_or(Role::Observateur);
      e.personnes.push_back(p);
    }
  return e;
}

std::string ecrireEquipe(const Equipe& e) {
  J o = {{"format", "promethee-equipe"}, {"nom", e.nom}, {"personnes", J::array()}};
  for (const auto& p : e.personnes) {
    J x = {{"nom", p.nom}, {"role", nomRole(p.role)}};
    if (!p.courriel.empty()) x["courriel"] = p.courriel;
    if (!p.cle.empty()) x["cle"] = p.cle;
    o["personnes"].push_back(x);
  }
  return o.dump(2) + "\n";
}

bool peut(Role r, const std::string& action) {
  if (action == "lire" || action == "commenter") return true;
  switch (r) {
    case Role::Responsable: return true;
    case Role::Concepteur: return action == "modifier" || action == "proposer";
    case Role::Relecteur: return action == "approuver";
    case Role::Observateur: return false;
  }
  return false;
}

const std::vector<std::string>& etatsCycleVie() {
  static const std::vector<std::string> e = {"brouillon", "pré-série", "production", "fin de série", "obsolète"};
  return e;
}

bool passagePermis(Role r, const std::string& depuis, const std::string& vers) {
  const auto& e = etatsCycleVie();
  const auto i = std::find(e.begin(), e.end(), depuis.empty() ? std::string("brouillon") : depuis), j = std::find(e.begin(), e.end(), vers);
  if (i == e.end() || j == e.end() || i == j || depuis == "obsolète") return false;
  const long d = std::distance(i, j);
  if (r == Role::Responsable) return d == 1 || d == -1;
  if (r == Role::Concepteur) return (depuis.empty() || depuis == "brouillon") ? vers == "pré-série" : (depuis == "pré-série" && vers == "brouillon");
  return false;
}

std::string changerEtatArticle(ecad::ConceptionElec& c, const std::string& article, const std::string& vers, Role r) {
  const auto it = c.articles.find(article);
  if (it == c.articles.end()) return "article « " + article + " » introuvable";
  if (!passagePermis(r, it->second.etatCycle, vers))
    return "passage de « " + (it->second.etatCycle.empty() ? std::string("brouillon") : it->second.etatCycle) + " » à « " + vers + " » refusé au rôle « " + nomRole(r) + " »";
  it->second.etatCycle = vers;
  return {};
}

bool revisionValide(const std::string& r, SchemaRevision s) {
  if (r.empty()) return false;
  switch (s) {
    case SchemaRevision::Lettres: return std::all_of(r.begin(), r.end(), [](char ch) { return LETTRES.find(ch) != std::string::npos; });
    case SchemaRevision::Nombres: return std::all_of(r.begin(), r.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); }) && r[0] != '0';
    case SchemaRevision::LettreChiffre:
      return r.size() >= 2 && LETTRES.find(r[0]) != std::string::npos && std::all_of(r.begin() + 1, r.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
  }
  return false;
}

std::string revisionSuivante(const std::string& courante, SchemaRevision s, bool majeure) {
  switch (s) {
    case SchemaRevision::Nombres: return courante.empty() || !revisionValide(courante, s) ? "1" : std::to_string(std::stoll(courante) + 1);
    case SchemaRevision::LettreChiffre: {
      if (!revisionValide(courante, s)) return "A1";
      if (!majeure) return courante.substr(0, 1) + std::to_string(std::stoll(courante.substr(1)) + 1);
      const auto i = LETTRES.find(courante[0]);
      return i + 1 < LETTRES.size() ? std::string(1, LETTRES[i + 1]) + "1" : std::string("A1");
    }
    case SchemaRevision::Lettres: {
      if (!revisionValide(courante, s)) return "A";
      // Numération bijective en base 20 : … Y, AA, AB …
      std::string r = courante;
      int i = static_cast<int>(r.size()) - 1;
      while (i >= 0) {
        const auto k = LETTRES.find(r[static_cast<std::size_t>(i)]);
        if (k + 1 < LETTRES.size()) {
          r[static_cast<std::size_t>(i)] = LETTRES[k + 1];
          return r;
        }
        r[static_cast<std::size_t>(i)] = LETTRES[0];
        --i;
      }
      return std::string(1, LETTRES[0]) + r;
    }
  }
  return "A";
}

std::string numeroInterneSuivant(const ecad::ConceptionElec& c, const std::string& prefixe, int chiffres) {
  long long plusGrand = 0;
  for (const auto& [id, a] : c.articles)
    if (a.numeroInterne.rfind(prefixe, 0) == 0) {
      const std::string reste = a.numeroInterne.substr(prefixe.size());
      if (!reste.empty() && std::all_of(reste.begin(), reste.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); }))
        plusGrand = std::max(plusGrand, std::stoll(reste));
    }
  std::string n = std::to_string(plusGrand + 1);
  while (static_cast<int>(n.size()) < chiffres) n = "0" + n;
  return prefixe + n;
}

const ecad::Article* articleParNumero(const ecad::ConceptionElec& c, const std::string& t) {
  for (const auto& [id, a] : c.articles)
    if (a.numeroInterne == t || a.mpn == t || id == t) return &a;
  for (const auto& [id, a] : c.articles)
    for (const auto& s : a.secondes)
      if (s.mpn == t) return &a;
  return nullptr;
}

std::string approuver(OrdreModification& o, const Equipe& e, const std::string& qui, bool accord, const std::string& date, const std::string& commentaire) {
  if (o.etat != "ouvert") return "l'ordre « " + o.id + " » n'est plus ouvert (" + o.etat + ")";
  const Personne* p = e.personne(qui);
  if (!p) return qui + " ne fait pas partie de l'équipe";
  if (!peut(p->role, "approuver")) return qui + " (" + nomRole(p->role) + ") ne peut pas approuver";
  for (const auto& a : o.approbations)
    if (a.personne == qui) return qui + " a déjà donné son avis";
  o.approbations.push_back({qui, nomRole(p->role), date, accord ? "approuvé" : "refusé", commentaire});
  if (!accord) {
    o.etat = "refusé";
    return {};
  }
  bool relecteur = false, responsable = false;
  for (const auto& a : o.approbations)
    if (a.decision == "approuvé") {
      relecteur |= a.role == nomRole(Role::Relecteur);
      responsable |= a.role == nomRole(Role::Responsable);
    }
  if (relecteur && responsable) o.etat = "approuvé";
  return {};
}

std::string appliquerOrdre(OrdreModification& o, ecad::ConceptionElec& c) {
  if (o.etat != "approuvé") return "l'ordre « " + o.id + " » n'est pas approuvé (" + o.etat + ")";
  if (!o.revisionDepart.empty() && o.revisionDepart != c.cartouche.revision)
    return "l'ordre part de la révision " + o.revisionDepart + ", la carte est en " + c.cartouche.revision;
  c.cartouche.revision = o.revisionArrivee.empty() ? revisionSuivante(c.cartouche.revision, SchemaRevision::Lettres) : o.revisionArrivee;
  c.verrouillee = false;  // la nouvelle révision s'ouvre aux modifications
  o.revisionArrivee = c.cartouche.revision;
  o.etat = "appliqué";
  return {};
}

std::vector<VerificationExigence> verifierExigences(const Projet& p) {
  std::vector<VerificationExigence> r;
  std::set<std::string> objets;  // UUID, repères et réseaux connus, pour les liens
  for (const auto& c : p.electronique) {
    objets.insert(c.uuid);
    for (const auto& k : c.composants) {
      objets.insert(k.uuid);
      objets.insert(k.reference);
    }
    for (const auto& cl : c.circuit.regles.classes) objets.insert(cl.nom);
  }
  for (const auto& e : p.exigences) {
    VerificationExigence v;
    v.id = e.id;
    for (const auto& l : e.liens)
      if (!objets.count(l)) v.liensManquants.push_back(l);
    if (!e.grandeur.empty()) {
      const auto [seuil, unite] = seuilDe(e.seuil);
      std::optional<double> mesure;
      std::string texte;
      if (e.grandeur == "cout") {
        double total = 0;
        bool complet = true;
        for (const auto& c : p.electronique) {
          const auto cn = ecad::coutNomenclature(c, e.quantite, c.varianteActive);
          total += cn.parCarte;
          complet &= cn.lignesSansPrix == 0;
        }
        if (complet) mesure = total;
        texte = chiffre(total) + " € par système pour " + std::to_string(e.quantite) + (complet ? "" : " (prix incomplets)");
      } else if (e.grandeur == "composants") {
        int n = 0;
        for (const auto& c : p.electronique)
          for (const auto& l : ecad::nomenclature(c)) n += l.quantite;
        mesure = n;
        texte = std::to_string(n) + " composants";
      } else if (e.grandeur == "surface") {
        double s = 0;
        for (const auto& c : p.electronique) s += ecad::aireMm2(c.circuit.contour.polygone());
        mesure = unite.find("cm") != std::string::npos ? s / 100 : s;
        texte = chiffre(*mesure) + (unite.empty() ? std::string(" mm²") : " " + unite);
      } else if (e.grandeur == "consommation" || e.grandeur == "masse") {
        const std::string champ = e.grandeur == "consommation" ? "Consommation" : "Masse";
        double s = 0;
        int renseignes = 0;
        for (const auto& c : p.electronique)
          for (const auto& k : c.composants)
            if (const auto it = k.champs.find(champ); it != k.champs.end()) {
              auto [v0, u0] = seuilDe(it->second);
              if (u0.rfind("m", 0) == 0 && u0.size() > 1) v0 /= 1000;  // mW, mg
              s += v0;
              ++renseignes;
            }
        if (renseignes) mesure = s;
        texte = renseignes ? chiffre(s, 3) + " " + unite + " (" + std::to_string(renseignes) + " composants renseignés)" : "aucun composant renseigné";
      }
      v.mesure = texte;
      if (!mesure) v.etat = "non vérifiable";
      else {
        const bool ok = e.comparateur == ">=" ? *mesure >= seuil - 1e-9 : e.comparateur == "==" ? std::abs(*mesure - seuil) <= 1e-9 * std::max(1.0, std::abs(seuil)) : *mesure <= seuil + 1e-9;
        v.etat = ok ? "vérifiée" : "en défaut";
      }
    } else
      v.etat = v.liensManquants.empty() ? "à vérifier" : "en défaut";
    if (!v.liensManquants.empty() && v.etat == "vérifiée") v.etat = "en défaut";
    r.push_back(v);
  }
  return r;
}

}  // namespace prom

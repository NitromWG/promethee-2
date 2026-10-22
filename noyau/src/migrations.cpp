// Prométhée : migrations du fichier de projet (lot L1, tâche 1.22). Voir migrations.hpp.
// Licence GPL-3.0-only.
#include "promethee/migrations.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"

namespace prom {

namespace {
const std::string VERSION_COURANTE = "0.31";

std::vector<int> morceaux(const std::string& v) {
  std::vector<int> r;
  std::stringstream s(v);
  std::string m;
  while (std::getline(s, m, '.')) r.push_back(m.empty() ? 0 : std::atoi(m.c_str()));
  return r;
}

std::string pluriel(int n, const std::string& un, const std::string& plusieurs) { return std::to_string(n) + " " + (n > 1 ? plusieurs : un); }

// Accès qui ne lève jamais : un objet abîmé n'est simplement pas touché (la lecture tolérante s'en charge)
Json* membre(Json& o, const char* k) { return o.is_object() && o.contains(k) ? &o[k] : nullptr; }
template <class F>
void pourChaque(Json& o, const char* k, F f) {
  if (Json* t = membre(o, k); t && t->is_array())
    for (Json& x : *t)
      if (x.is_object()) f(x);
}
template <class F>
void textesDeCarte(Json& o, F f) {
  if (Json* ci = membre(o, "circuit"); ci && ci->is_object()) pourChaque(*ci, "textes", f);
}
// Angle des textes de carte : la 0.28 les tournait dans le sens inverse des empreintes (X3)
int retournerTextes(Json& o) {
  int n = 0;
  textesDeCarte(o, [&n](Json& t) {
    if (Json* r = membre(t, "rot"); r && r->is_number() && r->get<double>() != 0) {
      *r = r->is_number_integer() ? Json(-r->get<long long>()) : Json(-r->get<double>());
      ++n;
    }
  });
  return n;
}
int retirerDesComposants(Json& o, const char* cle) {
  int n = 0;
  pourChaque(o, "composants", [&](Json& k) { n += static_cast<int>(k.erase(cle)); });
  return n;
}

std::vector<EtapeMigration> construire() {
  std::vector<EtapeMigration> e;
  // ---------- 0.28 → 0.29 (lot L0) ----------
  e.push_back(
      {"0.28", "0.29", "conception",
       "clé « formatElectronique » ; textes de carte tournés dans le sens des empreintes (X3) ; profil de fabricant personnalisé, brochage vérifié, "
       "exclusions de la nomenclature et du placement, couches explicites des pastilles",
       [](Json& o, std::vector<std::string>& notes) {
         if (const int n = retournerTextes(o))
           notes.push_back(pluriel(n, "texte de carte tourné repris", "textes de carte tournés repris") + " dans le sens de rotation des empreintes, sans changer leur dessin.");
         o["formatElectronique"] = 2;
       },
       [](Json& o, std::vector<std::string>& pertes) {
         retournerTextes(o);
         o.erase("formatElectronique");
         if (Json* ci = membre(o, "circuit"))
           if (Json* r = membre(*ci, "regles"); r && r->is_object()) {
             if (r->erase("capacitesPerso")) pertes.push_back("profil de fabricant personnalisé (capacités saisies) retiré : la 0.28 ne le connaît pas.");
             // La 0.28 essaie les affectations de classes dans l'ordre alphabétique des motifs
             if (Json* a = membre(*r, "affectations"); a && a->is_object() && a->size() > 1) {
               std::vector<std::string> cles;
               for (auto it = a->begin(); it != a->end(); ++it) cles.push_back(it.key());
               if (!std::is_sorted(cles.begin(), cles.end()))
                 pertes.push_back("l’ordre des " + std::to_string(cles.size()) + " affectations de classes devient alphabétique : la 0.28 les essaie dans cet ordre.");
             }
           }
         if (const int n = retirerDesComposants(o, "brochageVerifie")) pertes.push_back("brochage vérifié oublié pour " + pluriel(n, "composant.", "composants."));
         if (const int n = retirerDesComposants(o, "horsNomenclature")) pertes.push_back(pluriel(n, "composant exclu", "composants exclus") + " de la nomenclature y revien" + (n > 1 ? "nent." : "t."));
         if (const int n = retirerDesComposants(o, "horsPlacement")) pertes.push_back(pluriel(n, "composant exclu", "composants exclus") + " du fichier de placement y revien" + (n > 1 ? "nent." : "t."));
         int couches = 0;
         pourChaque(o, "bibliothequeEmpreintes", [&couches](Json& d) { pourChaque(d, "pastilles", [&couches](Json& p) { couches += static_cast<int>(p.erase("couches")); }); });
         if (couches) pertes.push_back("couches explicites de " + pluriel(couches, "pastille", "pastilles") + " d’empreinte retirées : la 0.28 les déduit du type de pastille.");
       }});
  // ---------- 0.29 → 0.30 (lot L1) ----------
  e.push_back({"0.29", "0.30", "conception",
               "clé « format » (« formatElectronique » gardée pour la 0.29) ; schéma seul ; identifiant des jonctions et des marques de non-connexion ; "
               "clés inconnues gardées objet par objet ; ordre stable des listes",
               [](Json& o, std::vector<std::string>&) { o["format"] = 2; },
               [](Json& o, std::vector<std::string>& pertes) {
                 o.erase("format");
                 o["formatElectronique"] = 2;
                 if (Json* s = membre(o, "schemaSeul")) {
                   if (s->is_boolean() && s->get<bool>()) pertes.push_back("schéma seul : la 0.29 lui montrera un circuit imprimé vide.");
                   o.erase("schemaSeul");
                 }
                 int ids = 0;
                 pourChaque(o, "feuilles", [&ids](Json& f) {
                   for (const char* k : {"jonctions", "nonConnexions"})
                     if (Json* t = membre(f, k); t && t->is_array())
                       for (Json& p : *t)
                         if (p.is_array() && p.size() > 2) {
                           p.erase(p.begin() + 2, p.end());
                           ++ids;
                         }
                 });
                 if (ids == 1) pertes.push_back("identifiant d’une jonction ou marque de non-connexion retiré : la 0.29 n’en a pas.");
                 else if (ids > 1) pertes.push_back("identifiants de " + std::to_string(ids) + " jonctions et marques de non-connexion retirés : la 0.29 n’en a pas.");
               }});
  e.push_back({"0.29", "0.30", "projet", "dispositions des panneaux de l’atelier électronique enregistrées avec le projet", [](Json&, std::vector<std::string>&) {},
               [](Json& o, std::vector<std::string>& pertes) {
                 if (Json* d = membre(o, "dispositions")) {
                   const int n = d->is_object() ? static_cast<int>(d->size()) : 0;
                   o.erase("dispositions");
                   if (n)
                     pertes.push_back(pluriel(n, "disposition des panneaux retirée : elle reste", "dispositions des panneaux retirées : elles restent") + " dans le projet de la 0.30.");
                 }
               }});
  // ---------- 0.30 → 0.31 (lot L2) ----------
  // La 0.30 garde objet par objet les clés qu'elle ne connaît pas : rien ne se perd dans le fichier, mais ce qu'elle
  // n'applique pas est dit
  e.push_back({"0.30", "0.31", "conception",
               "format 3 ; réglages de l’ERC du projet (gravités, exclusions, matrice) ; champs de symbole placés et bloqués ; fils arrondis ; connecteurs hors feuille "
               "et sens des signaux ; symboles traversants, liaisons de réseaux et noms des unités",
               [](Json& o, std::vector<std::string>&) {
                 o["format"] = ecad::FORMAT_ELECTRONIQUE;
                 o["formatElectronique"] = ecad::FORMAT_ELECTRONIQUE;
               },
               [](Json& o, std::vector<std::string>& pertes) {
                 o["format"] = 2;
                 o["formatElectronique"] = 2;
                 if (Json* e = membre(o, "erc"); e && e->is_object()) {
                   if (Json* g = membre(*e, "gravites"); g && g->is_object() && !g->empty())
                     pertes.push_back("gravités réglées de " + pluriel(static_cast<int>(g->size()), "contrôle", "contrôles") + " : la 0.30 ne les applique pas (gardées dans le fichier).");
                   if (Json* x = membre(*e, "exclusions"); x && x->is_array() && !x->empty())
                     pertes.push_back(pluriel(static_cast<int>(x->size()), "alerte exclue", "alertes exclues") + " de l’ERC : la 0.30 les montre de nouveau.");
                   if (Json* m = membre(*e, "matrice"); m && m->is_object() && !m->empty()) pertes.push_back("matrice des liaisons de l’ERC réglée : la 0.30 garde la sienne.");
                 }
                 int champs = 0, arrondis = 0, horsFeuille = 0, sens = 0;
                 pourChaque(o, "feuilles", [&](Json& f) {
                   pourChaque(f, "symboles", [&](Json& s) { champs += static_cast<int>(s.contains("champRef")) + static_cast<int>(s.contains("champVal")); });
                   pourChaque(f, "fils", [&](Json& x) { arrondis += x.contains("rayon"); });
                   pourChaque(f, "etiquettes", [&](Json& x) {
                     horsFeuille += x.contains("horsFeuille");
                     sens += x.contains("sens");
                   });
                 });
                 if (champs) pertes.push_back(pluriel(champs, "champ de symbole placé ou bloqué", "champs de symbole placés ou bloqués") + " : la 0.30 les montre à leur place par défaut.");
                 if (arrondis) pertes.push_back(pluriel(arrondis, "fil arrondi dessiné", "fils arrondis dessinés") + " en angle droit par la 0.30.");
                 if (horsFeuille) pertes.push_back(pluriel(horsFeuille, "connecteur hors feuille lu", "connecteurs hors feuille lus") + " comme étiquette globale par la 0.30 (mêmes réseaux).");
                 if (sens) pertes.push_back("sens des signaux de " + pluriel(sens, "étiquette", "étiquettes") + " : la 0.30 ne les montre pas.");
                 int liaisons = 0, nommes = 0;
                 pourChaque(o, "symboles", [&](Json& d) {
                   liaisons += d.contains("liaisonReseaux");
                   nommes += d.contains("nomsUnites");
                 });
                 if (liaisons) pertes.push_back(pluriel(liaisons, "liaison de réseaux vérifiée", "liaisons de réseaux vérifiées") + " comme un composant ordinaire par la 0.30.");
                 if (nommes) pertes.push_back("noms des unités de " + pluriel(nommes, "symbole", "symboles") + " : la 0.30 montre leurs lettres.");
               }});
  return e;
}
}  // namespace

const std::vector<EtapeMigration>& etapesMigration() {
  static const std::vector<EtapeMigration> e = construire();
  return e;
}

std::string versionFichierCourante() { return VERSION_COURANTE; }

std::vector<std::string> versionsAnterieures() {
  std::vector<std::string> r;
  for (auto it = etapesMigration().rbegin(); it != etapesMigration().rend(); ++it)
    if (std::find(r.begin(), r.end(), it->depuis) == r.end()) r.push_back(it->depuis);
  return r;
}

int comparerVersions(const std::string& a, const std::string& b) {
  const std::vector<int> x = morceaux(a), y = morceaux(b);
  for (std::size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
    const int u = i < x.size() ? x[i] : 0, v = i < y.size() ? y[i] : 0;
    if (u != v) return u < v ? -1 : 1;
  }
  return 0;
}

std::string versionConception(const Json& o) {
  // Format 2 : la 0.30 ; 3 : la 0.31 ; au-delà, une version plus récente, lue comme la courante (ce qu'elle ne
  // connaît pas est gardé)
  if (o.is_object() && o.contains("format") && o["format"].is_number()) return o["format"].get<double>() < 3 ? "0.30" : VERSION_COURANTE;
  if (o.is_object() && o.contains("formatElectronique")) return "0.29";
  return "0.28";
}

void monterConception(Json& o, std::vector<std::string>* notes) {
  if (!o.is_object()) return;
  const std::string v = versionConception(o);
  std::vector<std::string> n;
  for (const EtapeMigration& e : etapesMigration())
    if (e.objet == "conception" && comparerVersions(e.depuis, v) >= 0) e.monter(o, n);
  if (notes) notes->insert(notes->end(), n.begin(), n.end());
}

FichierAnterieur pourVersion(const Projet& p, const std::string& version) {
  const std::vector<std::string> possibles = versionsAnterieures();
  if (std::find(possibles.begin(), possibles.end(), version) == possibles.end())
    throw std::invalid_argument("Prométhée ne sait pas écrire de fichier pour la version " + version + ".");
  FichierAnterieur f;
  f.version = version;
  f.projet = versJson(p);
  const auto& etapes = etapesMigration();
  for (auto it = etapes.rbegin(); it != etapes.rend(); ++it) {
    if (comparerVersions(it->vers, version) <= 0) continue;
    if (it->objet == "projet") it->descendre(f.projet, f.pertes);
    else if (it->objet == "conception")
      pourChaque(f.projet, "electronique", [&](Json& c) {
        std::vector<std::string> pertes;
        it->descendre(c, pertes);
        const std::string nom = c.contains("nom") && c["nom"].is_string() ? c["nom"].get<std::string>() : std::string("sans nom");
        for (const auto& x : pertes) f.pertes.push_back("Carte « " + nom + " » : " + x);
      });
  }
  // Clés d'une version plus récente que la 0.30 garde objet par objet : celles d'avant ne les gardent pas
  if (comparerVersions(version, "0.30") < 0)
    for (const auto& c : p.electronique)
      if (!c.clesInconnues.empty())
        f.pertes.push_back("Carte « " + c.nom + " » : " + pluriel(static_cast<int>(c.clesInconnues.size()), "objet garde", "objets gardent") +
                           " des clés d’une version plus récente, que la " + version + " oubliera à son prochain enregistrement.");
  return f;
}

void ecrirePourVersion(const FichierAnterieur& f, const std::string& chemin) { ecrireFichierAtomique(cheminFichier(chemin), f.projet.dump(2) + "\n"); }

std::string nomPourVersion(const std::string& chemin, const std::string& version) {
  std::string v = version;
  std::replace(v.begin(), v.end(), '.', '-');
  for (const char* ext : {".prom.json", ".prom", ".json"}) {
    const std::string e(ext);
    if (chemin.size() > e.size() && chemin.compare(chemin.size() - e.size(), e.size(), e) == 0) return chemin.substr(0, chemin.size() - e.size()) + ".pour-" + v + e;
  }
  return chemin + ".pour-" + v + ".prom";
}

}  // namespace prom

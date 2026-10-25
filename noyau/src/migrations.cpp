// Prométhée : migrations du fichier de projet (lot L1, tâche 1.22). Voir migrations.hpp.
// Licence GPL-3.0-only.
#include "promethee/migrations.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>

#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/valeurs.hpp"

namespace prom {

namespace {
const std::string VERSION_COURANTE = "0.34";

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
int taille(const Json* t) { return t && t->is_array() ? static_cast<int>(t->size()) : t && t->is_object() ? static_cast<int>(t->size()) : 0; }
bool texteEgal(const Json& o, const char* k, const char* v) { return o.contains(k) && o[k].is_string() && o[k].get<std::string>() == v; }
int entier(const Json& o, const char* k, int defaut) { return o.contains(k) && o[k].is_number() ? o[k].get<int>() : defaut; }

// Données des ateliers de la 0.34 rangées dans « extensions » : la 0.33 garde la clé sans la lire
std::string nomExtension(const std::string& cle) {
  static const std::map<std::string, std::string> noms = {
      {"cablage", "faisceaux de câblage"},
      {"colle", "points de colle"},
      {"consignes", "consignes de l'enseignant"},
      {"defautsCarte", "réglages par défaut de la carte"},
      {"directivesSpice", "directives SPICE importées"},
      {"enseignant", "couche de l'enseignant (parties verrouillées, actions masquées, pannes cachées)"},
      {"historiqueIdx", "historique des échanges IDX"},
      {"importCouches", "tableau des couches importées"},
      {"isolementSurface", "réglages des lignes de fuite"},
      {"jeuxDxf", "jeux de couches DXF"},
      {"lignes", "schéma de lignes"},
      {"longueursBoitier", "longueurs internes des boîtiers"},
      {"numeroSerie", "numéro de série en sérigraphie"},
      {"organigrammes", "organigrammes"},
      {"panneau", "panneau de copies liées"},
      {"pdfA", "réglages PDF/A"},
      {"plans", "dossier de plans"},
      {"platine", "platine d'essai"},
      {"programmes", "programmes embarqués"},
      {"provenanceRegles", "provenance des règles"},
      {"retouches", "calque des retouches"},
      {"retouchesRecues", "retouches reçues"},
      {"revues", "revues formelles signées"},
      {"series", "séries d'exercices"},
      {"simulation", "classeur de simulation"},
      {"strategiesRoutage", "stratégies du routeur"},
  };
  const auto it = noms.find(cle);
  return it == noms.end() ? "« " + cle + " »" : it->second;
}

// Accord en nombre des participes des pertes (« ignoré », « ignorés »)
const char* s_(int n) { return n > 1 ? "s" : ""; }

void descendreConceptionVers033(Json& o, std::vector<std::string>& pertes) {
  o["format"] = 5;
  o["formatElectronique"] = 5;
  // ----- Schéma -----
  int blocs = 0, hierarchiques = 0, directives = 0, dessins = 0, reglesSchema = 0;
  pourChaque(o, "feuilles", [&](Json& f) {
    blocs += taille(membre(f, "blocs"));
    directives += taille(membre(f, "directives"));
    dessins += taille(membre(f, "formes")) + taille(membre(f, "tableaux")) + taille(membre(f, "images"));
    reglesSchema += taille(membre(f, "zonesRegles")) + taille(membre(f, "groupes"));
    pourChaque(f, "etiquettes", [&](Json& x) { hierarchiques += x.contains("hierarchique"); });
  });
  if (blocs || hierarchiques)
    pertes.push_back((blocs ? pluriel(blocs, "bloc hiérarchique", "blocs hiérarchiques") + " : la 0.33 ne relie pas " + (blocs > 1 ? "leurs" : "ses") + " ports aux feuilles filles" : std::string("hiérarchie")) +
                     (hierarchiques ? std::string(blocs ? " et lit " : " : la 0.33 lit ") + pluriel(hierarchiques, "étiquette hiérarchique en locale", "étiquettes hiérarchiques en locales") : "") +
                     " ; mettre à jour le circuit depuis ce schéma dans la 0.33 couperait les réseaux qui traversent les feuilles.");
  if (directives) pertes.push_back(pluriel(directives, "directive posée sur les fils", "directives posées sur les fils") + " (classes, règles) : gardée" + s_(directives) + " sans être lue" + s_(directives) + " par la 0.33.");
  if (dessins) pertes.push_back(pluriel(dessins, "forme, tableau ou image", "formes, tableaux et images") + " du schéma : absent" + s_(dessins) + " des vues et du PDF de la 0.33 (gardé" + s_(dessins) + ").");
  if (reglesSchema) pertes.push_back(pluriel(reglesSchema, "zone de règles ou groupe du schéma", "zones de règles et groupes du schéma") + " : ignoré" + s_(reglesSchema) + " par la 0.33 (gardé" + s_(reglesSchema) + ").");
  int alternatives = 0, derives = 0, cachees = 0, vuesPosees = 0;
  pourChaque(o, "symboles", [&](Json& d) {
    derives += d.contains("parent");
    pourChaque(d, "broches", [&](Json& b) { cachees += b.contains("cachee") && b["cachee"].is_boolean() && b["cachee"].get<bool>(); });
    // Seconde vue (De Morgan) : la 0.33 dessinerait les deux corps l'un sur l'autre, et ses broches en double
    if (entier(d, "vues", 1) > 1) {
      ++alternatives;
      for (const char* k : {"broches", "traces"})
        if (Json* t = membre(d, k); t && t->is_array())
          t->erase(std::remove_if(t->begin(), t->end(), [](const Json& x) { return x.is_object() && entier(x, "vue", 0) > 1; }), t->end());
      d.erase("vues");
    }
  });
  pourChaque(o, "feuilles", [&](Json& f) { pourChaque(f, "symboles", [&](Json& s) { vuesPosees += entier(s, "vue", 1) > 1; }); });
  if (alternatives)
    pertes.push_back("seconde vue (De Morgan) de " + pluriel(alternatives, "symbole retirée", "symboles retirée") + " : la 0.33 dessine la première" +
                     (vuesPosees ? ", y compris pour " + pluriel(vuesPosees, "composant posé", "composants posés") + " dans la seconde." : "."));
  if (derives) pertes.push_back(pluriel(derives, "symbole dérivé", "symboles dérivés") + (derives > 1 ? " de leur parent : la 0.33 les garde comme des symboles ordinaires" : " de son parent : la 0.33 le garde comme un symbole ordinaire") + ", sans l'héritage.");
  if (cachees) pertes.push_back(pluriel(cachees, "broche cachée reliée", "broches cachées reliées") + " par " + (cachees > 1 ? "leur" : "son") + " nom : la 0.33 " + (cachees > 1 ? "les montre sans les relier" : "la montre sans la relier") + " ; une mise à jour du circuit depuis la 0.33 couperait " + (cachees > 1 ? "ces liaisons." : "cette liaison."));
  // ----- Bibliothèque du projet -----
  int articles = 0;
  pourChaque(o, "articles", [&](Json& a) { articles += a.contains("modeleSpice") || a.contains("licence"); });
  if (articles) pertes.push_back("modèle SPICE ou licence de " + pluriel(articles, "article", "articles") + " : oubliés par la 0.33 à son prochain enregistrement.");
  int formes = 0, percages = 0;
  pourChaque(o, "bibliothequeEmpreintes", [&](Json& d) {
    pourChaque(d, "pastilles", [&](Json& p) {
      formes += p.contains("forme034");
      percages += p.contains("trous") || p.contains("fenteL") || p.contains("lamageDessus") || p.contains("lamageDessous") || p.contains("decPercage");
    });
  });
  if (formes) pertes.push_back(pluriel(formes, "pastille chanfreinée, trapézoïdale ou libre dessinée", "pastilles chanfreinées, trapézoïdales ou libres dessinées") + " en rectangle par la 0.33.");
  if (percages) pertes.push_back("perçages de " + pluriel(percages, "pastille", "pastilles") + " (fentes, trous multiples, décalés, lamés) : la 0.33 perce un trou rond centré.");
  // ----- Circuit -----
  if (Json* ci = membre(o, "circuit"); ci && ci->is_object()) {
    Json& k = *ci;
    if (Json* em = membre(k, "empilage"); em && em->is_object()) {
      const int n = entier(*em, "couches", 2);
      if (n != 1 && n != 2 && n != 4)
        pertes.push_back(pluriel(n, "couche de cuivre", "couches de cuivre") + " : la 0.33 n'en lit que 1, 2 ou 4 ; la carte y passe à " + (n > 4 ? "4" : "2") +
                         " couches et perd le cuivre des autres.");
      else if (em->contains("pile")) pertes.push_back("empilage détaillé (diélectriques, préimprégnés, usages des cuivres) : la 0.33 garde le nombre de couches et l'épaisseur.");
    }
    int typees = 0, contre = 0;
    pourChaque(k, "vias", [&](Json& v) {
      typees += v.contains("typeVia");
      contre += v.contains("contrePercage");
    });
    if (typees) pertes.push_back(pluriel(typees, "via borgne, enterrée ou micro percée", "vias borgnes, enterrées ou micro percées") + " de part en part par la 0.33 : à revoir avant fabrication.");
    if (contre) pertes.push_back("contre-perçage de " + pluriel(contre, "via ignoré", "vias ignoré") + " par la 0.33.");
    int arcs = 0, effilees = 0;
    pourChaque(k, "pistes", [&](Json& p) {
      arcs += p.contains("milieu");
      effilees += p.contains("largeurFin");
    });
    if (arcs) pertes.push_back(pluriel(arcs, "piste en arc tracée", "pistes en arc tracées") + " en droite par la 0.33.");
    if (effilees) pertes.push_back(pluriel(effilees, "piste effilée tracée", "pistes effilées tracées") + " à largeur constante par la 0.33.");
    int hachurees = 0, figees = 0;
    pourChaque(k, "zones", [&](Json& z) {
      hachurees += z.contains("hachure");
      figees += z.contains("fige");
    });
    if (hachurees) pertes.push_back(pluriel(hachurees, "zone hachurée remplie", "zones hachurées remplies") + " pleine par la 0.33.");
    if (figees) pertes.push_back("remplissage figé de " + pluriel(figees, "zone recalculé", "zones recalculé") + " par la 0.33.");
    if (const int n = taille(membre(k, "regions"))) pertes.push_back(pluriel(n, "région de cuivre plein", "régions de cuivre plein") + " (gouttes, cuivre propre, logos) : absente" + s_(n) + " du cuivre de la 0.33 (gardée" + s_(n) + ").");
    if (const int n = taille(membre(k, "zonesRegles"))) pertes.push_back(pluriel(n, "zone de règles ou d'interdiction", "zones de règles et d'interdiction") + " : la DRC de la 0.33 ne " + (n > 1 ? "les" : "la") + " contrôle pas (gardée" + s_(n) + ").");
    if (const int n = taille(membre(k, "motifs"))) pertes.push_back(pluriel(n, "motif d'accord de longueur gardé", "motifs d'accord de longueur gardés") + " en pistes ordinaires par la 0.33.");
    if (const int n = taille(membre(k, "cotes"))) pertes.push_back(pluriel(n, "cote", "cotes") + " de la carte : absente" + s_(n) + " des plans de la 0.33 (gardée" + s_(n) + ").");
    if (Json* r = membre(k, "regles"); r && r->is_object() && r->contains("avancees"))
      pertes.push_back("contraintes avancées (paires, longueurs, impédances, règles par classe) : la 0.33 vérifie ses règles simples.");
  }
  // ----- Mécanique et vues -----
  if (const int n = taille(membre(o, "piecesMontees"))) pertes.push_back(pluriel(n, "pièce montée", "pièces montées") + " sur la carte : la 0.33 ne " + (n > 1 ? "les montre pas et ne les compte" : "la montre pas et ne la compte") + " pas (gardée" + s_(n) + ").");
  if (o.contains("boitierImporte")) pertes.push_back("boîtier du commerce importé en STEP : la 0.33 dessine et contrôle le boîtier généré.");
  if (const int n = taille(membre(o, "pointsDeVue"))) pertes.push_back(pluriel(n, "point de vue 3D nommé", "points de vue 3D nommés") + " : ignoré" + s_(n) + " par la 0.33 (gardé" + s_(n) + ").");
  if (o.contains("reglagesSchema")) pertes.push_back("réglages du schéma (styles des fils, textes, grille) : la 0.33 garde les siens.");
  if (const int n = taille(membre(o, "aliasBus"))) pertes.push_back(pluriel(n, "alias de bus", "alias de bus") + " : la 0.33 ne les développe pas.");
  if (Json* x = membre(o, "extensions"); x && x->is_object() && !x->empty()) {
    std::string liste;
    for (auto it = x->begin(); it != x->end(); ++it) liste += (liste.empty() ? "" : ", ") + nomExtension(it.key());
    pertes.push_back("données des ateliers de la 0.34 gardées sans être lues par la 0.33 : " + liste + ".");
  }
}

void descendreProjetVers033(Json& o, std::vector<std::string>& pertes) {
  if (o.contains("licence")) pertes.push_back("licence du projet : ignorée par la 0.33 (gardée).");
  if (const int n = taille(membre(o, "liensDisciplines")))
    pertes.push_back(pluriel(n, "lien entre disciplines", "liens entre disciplines") + " (« à revoir » jusqu'à l'accusé de lecture) : ignoré" + s_(n) + " par la 0.33 (gardé" + s_(n) + ").");
  // Projets électriques : leur schéma est une conception, qui redescend au format 5 ; un folio de câblage de faisceau
  // y devient un folio libre, sans son faisceau
  int folios = 0, faisceaux = 0;
  pourChaque(o, "electrique", [&](Json& pe) {
    if (Json* s = membre(pe, "schema"); s && s->is_object()) {
      if (s->contains("format")) (*s)["format"] = 5;
      if (s->contains("formatElectronique")) (*s)["formatElectronique"] = 5;
      if (Json* x = membre(*s, "extensions"); x && x->is_object())
        if (const Json* c = membre(*x, "cablage")) faisceaux += taille(c);
    }
    if (Json* f = membre(pe, "folios"); f && f->is_object())
      for (auto it = f->begin(); it != f->end(); ++it)
        if (it->is_object() && texteEgal(*it, "type", "cablage")) {
          ++folios;
          (*it)["type"] = "libre";
          it->erase("faisceau");
        }
  });
  if (folios) pertes.push_back(pluriel(folios, "folio de câblage de faisceau devenu", "folios de câblage de faisceau devenus") + " libre" + (folios > 1 ? "s" : "") + " : la 0.33 ne les dessine pas.");
  if (faisceaux) pertes.push_back(pluriel(faisceaux, "faisceau", "faisceaux") + " (connecteurs, câbles, fils, planche, topologie) : gardé" + s_(faisceaux) + " dans le fichier sans être lu" + s_(faisceaux) + " par la 0.33.");
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
  // ---------- 0.31 → 0.32 (lot L3) ----------
  e.push_back({"0.31", "0.32", "conception",
               "format 4 ; hauteur et sa provenance (« hauteurVerifiee » devient « saisie », listée) ; centre des corps ; valeur lue, formules, paramètres typés, "
               "variables ; genre des composants ; propriété de fabrication des pastilles ; volumes libres ; mode de synchronisation",
               [](Json& o, std::vector<std::string>& notes) {
                 o["format"] = ecad::FORMAT_ELECTRONIQUE;
                 o["formatElectronique"] = ecad::FORMAT_ELECTRONIQUE;
                 // Une hauteur cochée « vérifiée » en 0.31 l'était d'office dès qu'on la tapait (P5) : elle devient
                 // « saisie », et la liste dit lesquelles reconfirmer sur la fiche
                 std::vector<std::string> cochees;
                 pourChaque(o, "composants", [&](Json& k) {
                   if (Json* v = membre(k, "hauteurVerifiee")) {
                     if (v->is_boolean() && v->get<bool>()) cochees.push_back(k.contains("ref") && k["ref"].is_string() ? k["ref"].get<std::string>() : std::string("?"));
                     k.erase("hauteurVerifiee");
                   }
                 });
                 if (!cochees.empty()) {
                   std::sort(cochees.begin(), cochees.end(), ecad::ordreNaturel);
                   std::string liste;
                   for (const auto& r : cochees) liste += (liste.empty() ? "" : ", ") + r;
                   notes.push_back(pluriel(static_cast<int>(cochees.size()), "hauteur cochée « vérifiée » reprise", "hauteurs cochées « vérifiées » reprises") +
                                   " en « saisie », à confirmer sur la fiche technique : " + liste + ".");
                 }
               },
               [](Json& o, std::vector<std::string>& pertes) {
                 o["format"] = 3;
                 o["formatElectronique"] = 3;
                 int formules = 0, genres = 0, simulation = 0, parametres = 0;
                 pourChaque(o, "composants", [&](Json& k) {
                   if (Json* p = membre(k, "provenanceHauteur")) {
                     if (p->is_string() && p->get<std::string>() == "fiche") k["hauteurVerifiee"] = true;
                     k.erase("provenanceHauteur");
                   }
                   k.erase("sourceHauteur");
                   k.erase("valeurLue");
                   if (k.contains("formule")) {
                     ++formules;
                     k.erase("formule");
                   }
                   if (k.contains("genre")) {
                     ++genres;
                     k.erase("genre");
                   }
                   if (k.contains("horsSimulation")) {
                     ++simulation;
                     k.erase("horsSimulation");
                   }
                   // Paramètres typés : repris en champs libres, que la 0.31 montre
                   if (Json* pa = membre(k, "parametres"); pa && pa->is_object()) {
                     Json& ch = k["champs"];
                     if (!ch.is_object()) ch = Json::object();
                     for (auto it = pa->begin(); it != pa->end(); ++it) {
                       std::string libelle = it.key();
                       for (auto f : {ecad::FamilleValeur::Resistance, ecad::FamilleValeur::Condensateur, ecad::FamilleValeur::Inductance, ecad::FamilleValeur::Ferrite,
                                      ecad::FamilleValeur::Fusible, ecad::FamilleValeur::Quartz, ecad::FamilleValeur::Zener, ecad::FamilleValeur::Pile, ecad::FamilleValeur::Aucune})
                         if (const ecad::DefParametre* d = ecad::parametre(f, it.key())) {
                           libelle = d->libelle;
                           break;
                         }
                       if (!ch.contains(libelle)) ch[libelle] = it.value();
                       ++parametres;
                     }
                     k.erase("parametres");
                   }
                 });
                 if (formules) pertes.push_back(pluriel(formules, "valeur calculée gardée", "valeurs calculées gardées") + " en texte, sans sa formule : la 0.31 ne la recalcule pas.");
                 if (parametres) pertes.push_back(pluriel(parametres, "paramètre typé repris", "paramètres typés repris") + " en champ libre.");
                 if (genres) pertes.push_back("genre de " + pluriel(genres, "composant", "composants") + " (mécanique, graphique…) : la 0.31 les traite en composants ordinaires.");
                 if (simulation) pertes.push_back(pluriel(simulation, "composant exclu", "composants exclus") + " de la simulation : la 0.31 l'ignore.");
                 if (Json* v = membre(o, "variables"); v && v->is_object() && !v->empty()) pertes.push_back(pluriel(static_cast<int>(v->size()), "variable", "variables") + " de la conception : la 0.31 les garde sans les lire.");
                 if (Json* s = membre(o, "synchroSurConfirmation"); s && s->is_boolean() && s->get<bool>()) pertes.push_back("synchronisation sur confirmation : la 0.31 met à jour le circuit par F8.");
                 int decales = 0, fab = 0, volumes = 0;
                 pourChaque(o, "bibliothequeEmpreintes", [&](Json& d) {
                   if (Json* k = membre(d, "corps"); k && k->is_object()) {
                     const bool decale = (k->contains("dx") && (*k)["dx"].is_number() && (*k)["dx"].get<double>() != 0) || (k->contains("dy") && (*k)["dy"].is_number() && (*k)["dy"].get<double>() != 0);
                     decales += decale;
                     k->erase("dx");
                     k->erase("dy");
                   }
                   pourChaque(d, "pastilles", [&](Json& p) { fab += static_cast<int>(p.erase("fab")); });
                   if (d.contains("volumes")) {
                     ++volumes;
                     d.erase("volumes");
                   }
                 });
                 pourChaque(o, "symboles", [](Json& d) { d.erase("genre"); });
                 if (Json* ci = membre(o, "circuit"); ci && ci->is_object() && ci->contains("volumes")) {
                   ++volumes;
                   ci->erase("volumes");
                 }
                 if (decales) pertes.push_back(pluriel(decales, "corps d'empreinte recentré", "corps d'empreintes recentrés") + " sur l'origine par la 0.31.");
                 if (fab) pertes.push_back("propriété de fabrication de " + pluriel(fab, "pastille", "pastilles") + " (BGA, fiducial…) : la 0.31 écrit des pastilles ordinaires.");
                 if (volumes) pertes.push_back("volumes libres retirés : la 0.31 ne les dessine pas.");
               }});
  e.push_back({"0.31", "0.32", "projet", "piliers posés par le dessous avec lamage, nervures, vis M6", [](Json&, std::vector<std::string>&) {},
               [](Json& o, std::vector<std::string>& pertes) {
                 int dessous = 0, nervures = 0, m6 = 0;
                 pourChaque(o, "trous", [&](Json& t) {
                   dessous += static_cast<int>(t.erase("pose"));
                   nervures += static_cast<int>(t.erase("nervures"));
                   if (Json* v = membre(t, "vis"); v && v->is_string() && v->get<std::string>() == "M6") {
                     ++m6;
                     t.erase("vis");
                   }
                 });
                 if (Json* b = membre(o, "boitier"); b && b->is_object())
                   if (Json* v = membre(*b, "vis"); v && v->is_string() && v->get<std::string>() == "M6") {
                     ++m6;
                     (*b)["vis"] = "M5";
                   }
                 if (dessous) pertes.push_back(pluriel(dessous, "pilier posé", "piliers posés") + " par le dessous : la 0.31 les visse par le dessus.");
                 if (nervures) pertes.push_back("nervures de " + pluriel(nervures, "pilier", "piliers") + " retirées.");
                 if (m6) pertes.push_back("vis M6 inconnue de la 0.31 : M5 ou la vis du projet à sa place.");
               }});
  // ---------- 0.32 → 0.33 (lots L4 à L19) ----------
  // La 0.32 garde ce qu'elle ne connaît pas, objet par objet, et le réécrit : la descente ne retire rien, elle dit
  // ce que la 0.32 ignorera tant que le fichier n'est pas rouvert par la 0.33
  e.push_back({"0.32", "0.33", "conception",
               "format 5 ; articles du projet, modèles 3D exacts, façades, contour tiré d'une esquisse, dessins de carte, canaux répétés, variantes de "
               "montage, notes attachées aux objets, jeux de sorties",
               [](Json& o, std::vector<std::string>&) {
                 o["format"] = ecad::FORMAT_ELECTRONIQUE;
                 o["formatElectronique"] = ecad::FORMAT_ELECTRONIQUE;
               },
               [](Json& o, std::vector<std::string>& pertes) {
                 o["format"] = 4;
                 o["formatElectronique"] = 4;
                 auto taille = [&](const char* k) {
                   const Json* t = membre(o, k);
                   return t && t->is_array() ? static_cast<int>(t->size()) : 0;
                 };
                 if (const int n = taille("articles")) pertes.push_back(pluriel(n, "article du projet", "articles du projet") + " : la 0.32 ne les montre pas (gardés dans le fichier).");
                 if (const int n = taille("modeles3D")) pertes.push_back(pluriel(n, "modèle 3D exact", "modèles 3D exacts") + " : la 0.32 dessine la boîte générique (gardés).");
                 if (const int n = taille("facades")) pertes.push_back(pluriel(n, "façade", "façades") + " du boîtier ignorées par la 0.32 (gardées).");
                 if (o.contains("esquisseContour")) pertes.push_back("contour tiré d'une esquisse : la 0.32 garde le contour dessiné, sans son esquisse pilote.");
                 if (const int n = taille("variantesMontage")) pertes.push_back(pluriel(n, "variante de montage", "variantes de montage") + " : la 0.32 ne connaît que la carte de base.");
                 if (const int n = taille("notes")) pertes.push_back(pluriel(n, "note attachée", "notes attachées") + " aux objets : invisibles dans la 0.32 (gardées).");
                 if (const int n = taille("jeuxSorties")) pertes.push_back(pluriel(n, "jeu de sorties", "jeux de sorties") + " : la 0.32 ne les lance pas (gardés).");
                 int dessins = 0, repetees = 0;
                 if (Json* ci = membre(o, "circuit"); ci && ci->is_object())
                   if (Json* d = membre(*ci, "dessins"); d && d->is_array()) dessins = static_cast<int>(d->size());
                 pourChaque(o, "feuilles", [&](Json& f) { repetees += f.contains("repetitions") || f.contains("modele") ? 1 : 0; });
                 if (dessins) pertes.push_back(pluriel(dessins, "dessin de carte", "dessins de carte") + " (traits, arcs, polygones) absents des sorties de la 0.32.");
                 if (repetees) pertes.push_back(pluriel(repetees, "feuille de canal répété", "feuilles de canaux répétés") + " : la 0.32 les montre comme des feuilles ordinaires.");
               }});
  e.push_back({"0.32", "0.33", "projet", "exigences, ordres de modification, schéma de système, impression 3D du boîtier", [](Json&, std::vector<std::string>&) {},
               [](Json& o, std::vector<std::string>& pertes) {
                 if (const Json* x = membre(o, "exigences"); x && x->is_array() && !x->empty())
                   pertes.push_back(pluriel(static_cast<int>(x->size()), "exigence", "exigences") + " du projet : ignorées par la 0.32 (gardées).");
                 if (const Json* x = membre(o, "ordresModification"); x && x->is_array() && !x->empty())
                   pertes.push_back(pluriel(static_cast<int>(x->size()), "ordre de modification", "ordres de modification") + " : ignorés par la 0.32 (gardés).");
                 if (const Json* x = membre(o, "liaisons"); x && x->is_array() && !x->empty())
                   pertes.push_back(pluriel(static_cast<int>(x->size()), "liaison du schéma de système", "liaisons du schéma de système") + " : ignorées par la 0.32 (gardées).");
                 if (Json* b = membre(o, "boitier"); b && b->is_object() && b->erase("impression"))
                   pertes.push_back("réglages d'impression 3D du boîtier (matière, coques, prix) retirés : la 0.32 ne les connaît pas.");
               }});
  // ---------- 0.33 → 0.34 (fin des lots L4 à L19) ----------
  // La 0.33 garde ce qu'elle ne connaît pas dans les objets du schéma, des bibliothèques et du circuit, et le réécrit :
  // la descente n'en retire que ce qui la tromperait (seconde vue des symboles), et dit ce qu'elle ignorera ou oubliera
  e.push_back({"0.33", "0.34", "conception",
               "format 6 ; hiérarchie par blocs, directives, formes, tableaux et images du schéma ; symboles dérivés, à deux vues, broches cachées ; "
               "pastilles et vias complètes, pistes en arc, zones hachurées ou figées, cuivre plein, zones de règles, cotes, empilage jusqu'à 32 cuivres, "
               "contraintes avancées ; pièces montées, boîtier importé, points de vue ; réglages du schéma, alias de bus ; données des ateliers",
               [](Json& o, std::vector<std::string>&) {
                 o["format"] = ecad::FORMAT_ELECTRONIQUE;
                 o["formatElectronique"] = ecad::FORMAT_ELECTRONIQUE;
               },
               descendreConceptionVers033});
  e.push_back({"0.33", "0.34", "projet", "licence du projet, liens entre disciplines, folios de câblage de faisceau et faisceaux des projets électriques",
               [](Json&, std::vector<std::string>&) {}, descendreProjetVers033});
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
  // Format 2 : la 0.30 ; 3 : la 0.31 ; 4 : la 0.32 ; 5 : la 0.33 ; 6 : la 0.34 ; au-delà, une version plus récente,
  // lue comme la courante (ce qu'elle ne connaît pas est gardé)
  if (o.is_object() && o.contains("format") && o["format"].is_number()) {
    const double f = o["format"].get<double>();
    return f < 3 ? "0.30" : f < 4 ? "0.31" : f < 5 ? "0.32" : f < 6 ? "0.33" : VERSION_COURANTE;
  }
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

// Prométhée : changements d'une conception électronique et commandes annulables (lot L1). Licence GPL-3.0-only.
#include "promethee/commandes.hpp"

#include <unordered_map>

namespace prom::ecad {

using N = ChangementConception;

bool ChangementConception::vide() const {
  return !entiere && objets.empty() && proprietes.empty() && !schema && !connectivite && !circuit && !zones && !regles && !definitions;
}

void ChangementConception::ajouter(const std::string& uuid, std::uint8_t natures) {
  if (!uuid.empty()) objets[uuid] |= natures;
}

void ChangementConception::fusionner(const ChangementConception& o) {
  if (conception.empty()) conception = o.conception;
  for (const auto& [u, n] : o.objets) objets[u] |= n;
  proprietes.insert(o.proprietes.begin(), o.proprietes.end());
  schema |= o.schema;
  connectivite |= o.connectivite;
  circuit |= o.circuit;
  zones |= o.zones;
  regles |= o.regles;
  definitions |= o.definitions;
  entiere |= o.entiere;
}

bool ChangementConception::demandeErc() const {
  if (demandeConnectivite()) return true;
  for (const char* p : {"brochageVerifie", "empreinte", "horsCarte", "symbole"})
    if (proprietes.count(p)) return true;
  return false;
}

namespace {
// Compare deux listes d'objets identifiés : ajoutés, retirés, changés (« nature » pour un objet changé)
template <class T, class Cle, class Comparer>
void comparer(const std::vector<T>& avant, const std::vector<T>& apres, Cle cle, Comparer changement, ChangementConception& ch, std::uint8_t enPlus) {
  std::unordered_map<std::string, const T*> anciens;
  anciens.reserve(avant.size());
  for (const auto& x : avant) anciens.emplace(cle(x), &x);
  for (const auto& x : apres) {
    const auto it = anciens.find(cle(x));
    if (it == anciens.end()) {
      ch.ajouter(cle(x), N::Ajout | enPlus);
      continue;
    }
    if (!(*it->second == x)) ch.ajouter(cle(x), changement(*it->second, x));
    anciens.erase(it);
  }
  for (const auto& [k, x] : anciens) ch.ajouter(k, N::Retrait | enPlus);
}
}  // namespace

ChangementConception differencier(const ConceptionElec& a, const ConceptionElec& b) {
  ChangementConception ch;
  ch.conception = b.uuid;
  if (a.uuid != b.uuid) {
    ch.entiere = true;
    return ch;
  }
  auto propriete = [&](bool differe, const char* nom) {
    if (differe) ch.proprietes.insert(nom);
  };
  propriete(a.nom != b.nom, "nom");
  propriete(!(a.cartouche == b.cartouche), "cartouche");
  propriete(a.pilotageBoitier != b.pilotageBoitier, "pilotageBoitier");
  propriete(a.schemaSeul != b.schemaSeul, "schemaSeul");
  propriete(a.verrouillee != b.verrouillee, "verrouillee");
  propriete(!(a.publications == b.publications), "publications");
  if (a.nomsReseaux != b.nomsReseaux) {
    ch.proprietes.insert("nomsReseaux");
    ch.connectivite = true;
  }
  if (a.symboles != b.symboles || a.empreintes != b.empreintes) ch.definitions = true;
  // Composants : chaque propriété changée est nommée ; celles qui nomment les réseaux ou changent le brochage
  // touchent la connectivité, celles qui changent l'empreinte touchent le circuit
  comparer(
      a.composants, b.composants, [](const ComposantElec& x) { return x.uuid; },
      [&](const ComposantElec& x, const ComposantElec& y) {
        std::uint8_t n = N::Propriete;
        const std::pair<bool, const char*> champs[] = {{x.reference != y.reference, "reference"},
                                                       {x.valeur != y.valeur, "valeur"},
                                                       {x.symbole != y.symbole, "symbole"},
                                                       {x.empreinte != y.empreinte, "empreinte"},
                                                       {x.fabricant != y.fabricant, "fabricant"},
                                                       {x.mpn != y.mpn, "mpn"},
                                                       {x.fournisseur != y.fournisseur, "fournisseur"},
                                                       {x.description != y.description, "description"},
                                                       {x.fiche != y.fiche, "fiche"},
                                                       {x.nePasMonter != y.nePasMonter, "nePasMonter"},
                                                       {x.horsCarte != y.horsCarte, "horsCarte"},
                                                       {x.champs != y.champs, "champs"},
                                                       {x.hauteur != y.hauteur, "hauteur"},
                                                       {x.provenanceHauteur != y.provenanceHauteur || x.sourceHauteur != y.sourceHauteur, "provenanceHauteur"},
                                                       {x.genre != y.genre, "genre"},
                                                       {x.horsSimulation != y.horsSimulation, "horsSimulation"},
                                                       {x.parametres != y.parametres, "parametres"},
                                                       {x.formule != y.formule, "formule"},
                                                       {x.brochageVerifie != y.brochageVerifie, "brochageVerifie"},
                                                       {x.horsNomenclature != y.horsNomenclature, "horsNomenclature"},
                                                       {x.horsPlacement != y.horsPlacement, "horsPlacement"}};
        for (const auto& [differe, nom] : champs)
          if (differe) ch.proprietes.insert(nom);
        if (x.reference != y.reference || x.symbole != y.symbole) {
          n |= N::Connectivite;
          ch.connectivite = true;
          ch.schema = true;
        }
        if (x.empreinte != y.empreinte || x.horsCarte != y.horsCarte) ch.circuit = true;
        if (x.valeur != y.valeur) ch.schema = true;
        return n;
      },
      ch, 0);
  for (const auto& [u, n] : ch.objets)
    if (n & (N::Ajout | N::Retrait)) {
      ch.connectivite = ch.schema = ch.circuit = true;
      break;
    }
  // Feuilles et leur contenu
  {
    std::unordered_map<std::string, const Feuille*> anciennes;
    for (const auto& f : a.feuilles) anciennes.emplace(f.uuid, &f);
    std::vector<std::string> ordreA, ordreB;
    for (const auto& f : a.feuilles) ordreA.push_back(f.uuid);
    for (const auto& f : b.feuilles) ordreB.push_back(f.uuid);
    if (ordreA != ordreB) {
      ch.schema = true;
      ch.proprietes.insert("feuilles");
    }
    for (const auto& f : b.feuilles) {
      const auto it = anciennes.find(f.uuid);
      if (it == anciennes.end()) {
        ch.ajouter(f.uuid, N::Ajout | N::Connectivite);
        ch.schema = ch.connectivite = true;
        for (const auto& s : f.symboles) ch.ajouter(s.uuid, N::Ajout | N::Connectivite);
        continue;
      }
      const Feuille& g = *it->second;
      anciennes.erase(it);
      if (g == f) continue;
      ch.schema = true;
      if (g.nom != f.nom || g.format != f.format) ch.ajouter(f.uuid, N::Propriete);
      const std::size_t avantObjets = ch.objets.size();
      bool reseaux = false;
      comparer(
          g.symboles, f.symboles, [](const SymbolePlace& x) { return x.uuid; },
          [&](const SymbolePlace& x, const SymbolePlace& y) {
            reseaux = true;
            std::uint8_t n = N::Geometrie | N::Connectivite;
            if (x.nomAlim != y.nomAlim || x.composant != y.composant || x.symbole != y.symbole || x.unite != y.unite || x.numeros != y.numeros) n |= N::Propriete;
            return n;
          },
          ch, N::Connectivite);
      comparer(g.fils, f.fils, [](const Fil& x) { return x.uuid; }, [&](const Fil&, const Fil&) { return static_cast<std::uint8_t>(N::Geometrie | N::Connectivite); }, ch, N::Connectivite);
      comparer(
          g.etiquettes, f.etiquettes, [](const Etiquette& x) { return x.uuid; },
          [&](const Etiquette& x, const Etiquette& y) {
            return static_cast<std::uint8_t>((x.texte != y.texte || x.type != y.type ? N::Propriete : 0) | (x.p != y.p || x.rotation != y.rotation ? N::Geometrie : 0) | N::Connectivite);
          },
          ch, N::Connectivite);
      if (ch.objets.size() != avantObjets) reseaux = true;
      comparer(
          g.textes, f.textes, [](const TexteSchema& x) { return x.uuid; },
          [](const TexteSchema& x, const TexteSchema& y) { return static_cast<std::uint8_t>((x.texte != y.texte || x.taille != y.taille ? N::Propriete : 0) | (x.p != y.p || x.rotation != y.rotation ? N::Geometrie : 0)); },
          ch, 0);
      comparer(g.jonctions, f.jonctions, [](const Jonction& x) { return idJonction(x); }, [](const Jonction&, const Jonction&) { return static_cast<std::uint8_t>(N::Geometrie); }, ch,
               N::Connectivite);
      comparer(g.nonConnexions, f.nonConnexions, [](const NonConnexion& x) { return idNonConnexion(x); },
               [](const NonConnexion&, const NonConnexion&) { return static_cast<std::uint8_t>(N::Geometrie); }, ch, N::Connectivite);
      if (!(g.jonctions == f.jonctions) || !(g.nonConnexions == f.nonConnexions)) reseaux = true;
      if (reseaux) ch.connectivite = true;
    }
    for (const auto& [u, f] : anciennes) {
      ch.ajouter(u, N::Retrait | N::Connectivite);
      for (const auto& s : f->symboles) ch.ajouter(s.uuid, N::Retrait | N::Connectivite);
      ch.schema = ch.connectivite = true;
    }
  }
  // Circuit
  const Circuit& x = a.circuit;
  const Circuit& y = b.circuit;
  if (!(x == y)) {
    if (!(x.contour == y.contour) || !(x.empilage == y.empilage) || !(x.regles == y.regles) || !(x.serigraphie == y.serigraphie) || !(x.fabrication == y.fabrication)) {
      ch.regles = true;
      ch.circuit = true;
    }
    if (!(x.contour == y.contour)) ch.proprietes.insert("contour");
    if (!(x.empilage == y.empilage)) ch.proprietes.insert("empilage");
    if (!(x.regles == y.regles)) ch.proprietes.insert("regles");
    if (!(x.affichage == y.affichage)) ch.proprietes.insert("affichage");
    const std::size_t avantObjets = ch.objets.size();
    comparer(
        x.empreintes, y.empreintes, [](const EmpreintePlacee& e) { return e.composant; },
        [](const EmpreintePlacee& e, const EmpreintePlacee& f) {
          return static_cast<std::uint8_t>((e.position != f.position || e.rotation != f.rotation || e.dessous != f.dessous || e.empreinte != f.empreinte ? N::Geometrie : 0) |
                                           (e.verrou != f.verrou ? N::Propriete : 0));
        },
        ch, N::Geometrie);
    comparer(
        x.pistes, y.pistes, [](const Piste& p) { return p.uuid; },
        [](const Piste& p, const Piste& q) { return static_cast<std::uint8_t>(N::Geometrie | (p.reseau != q.reseau ? N::Propriete | N::Connectivite : 0)); }, ch, N::Geometrie);
    comparer(
        x.vias, y.vias, [](const Via& v) { return v.uuid; },
        [](const Via& v, const Via& w) { return static_cast<std::uint8_t>(N::Geometrie | (v.reseau != w.reseau ? N::Propriete | N::Connectivite : 0)); }, ch, N::Geometrie);
    comparer(
        x.textes, y.textes, [](const TexteCircuit& t) { return t.uuid; },
        [](const TexteCircuit& t, const TexteCircuit& u) { return static_cast<std::uint8_t>((t.texte != u.texte ? N::Propriete : 0) | N::Geometrie); }, ch, N::Geometrie);
    if (ch.objets.size() != avantObjets || !(x.empreintes == y.empreintes) || !(x.pistes == y.pistes) || !(x.vias == y.vias) || !(x.textes == y.textes)) ch.circuit = true;
    const std::size_t avantZones = ch.objets.size();
    comparer(
        x.zones, y.zones, [](const Zone& z) { return z.uuid; },
        [](const Zone& z, const Zone& w) { return static_cast<std::uint8_t>((z.contour != w.contour || z.couche != w.couche ? N::Geometrie : 0) | (z.reseau != w.reseau ? N::Connectivite : 0) | N::Propriete); },
        ch, N::Geometrie);
    std::vector<std::string> ordreA, ordreB;
    for (const auto& z : x.zones) ordreA.push_back(z.uuid);
    for (const auto& z : y.zones) ordreB.push_back(z.uuid);
    if (ch.objets.size() != avantZones || !(x.zones == y.zones) || ordreA != ordreB) ch.zones = ch.circuit = true;
  }
  return ch;
}

// ---------- Propriété d'un composant ----------
namespace {
std::string ComposantElec::* champTexte(const std::string& p) {
  if (p == "reference") return &ComposantElec::reference;
  if (p == "valeur") return &ComposantElec::valeur;
  if (p == "fabricant") return &ComposantElec::fabricant;
  if (p == "mpn") return &ComposantElec::mpn;
  if (p == "fournisseur") return &ComposantElec::fournisseur;
  if (p == "description") return &ComposantElec::description;
  if (p == "fiche") return &ComposantElec::fiche;
  return nullptr;
}
const char* libelle(const std::string& p) {
  if (p == "reference") return "Repère";
  if (p == "valeur") return "Valeur";
  if (p == "fabricant") return "Fabricant";
  if (p == "mpn") return "Référence fabricant";
  if (p == "fournisseur") return "Fournisseur";
  if (p == "description") return "Description";
  if (p == "fiche") return "Fiche technique";
  return "Propriété";
}
std::string court(const std::string& t) {
  if (t.empty()) return "(vide)";
  return t.size() > 24 ? t.substr(0, 22) + "…" : t;
}
}  // namespace

CommandeProprieteComposant::CommandeProprieteComposant(std::string composant, std::string propriete, std::string avant, std::string apres, std::string repere, std::uint64_t saisie)
    : m_composant(std::move(composant)), m_propriete(std::move(propriete)), m_avant(std::move(avant)), m_apres(std::move(apres)), m_repere(std::move(repere)), m_saisie(saisie) {}

bool CommandeProprieteComposant::connue(const std::string& propriete) { return champTexte(propriete) != nullptr; }

std::string CommandeProprieteComposant::nom() const {
  return std::string(libelle(m_propriete)) + " de " + (m_repere.empty() ? std::string("composant") : m_repere) + " : " + court(m_avant) + " → " + court(m_apres);
}

void CommandeProprieteComposant::ecrire(ConceptionElec& c, const std::string& v) const {
  exigerModifiable(c);
  if (ComposantElec* k = c.composant(m_composant))
    if (auto champ = champTexte(m_propriete)) k->*champ = v;
}

void CommandeProprieteComposant::appliquer(ConceptionElec& c) { ecrire(c, m_apres); }
void CommandeProprieteComposant::defaire(ConceptionElec& c) { ecrire(c, m_avant); }

bool CommandeProprieteComposant::fusionner(const Commande& suivante) {
  const auto* s = dynamic_cast<const CommandeProprieteComposant*>(&suivante);
  if (!s || m_saisie == 0 || s->m_saisie != m_saisie || s->m_composant != m_composant || s->m_propriete != m_propriete) return false;
  m_apres = s->m_apres;
  return true;
}

ChangementConception CommandeProprieteComposant::changement() const {
  ChangementConception ch;
  ch.proprietes.insert(m_propriete);
  std::uint8_t n = N::Propriete;
  if (m_propriete == "reference") {
    n |= N::Connectivite;
    ch.connectivite = true;
  }
  ch.schema = true;  // le repère et la valeur sont écrits sur la feuille
  ch.ajouter(m_composant, n);
  return ch;
}

// ---------- Instantané ----------
CommandeInstantane::CommandeInstantane(std::string nom, const ConceptionElec& avant, const ConceptionElec& apres)
    : m_nom(std::move(nom)), m_avant(versJson(avant).dump()), m_apres(versJson(apres).dump()), m_changement(differencier(avant, apres)) {}

namespace {
void remplacer(ConceptionElec& c, const std::string& texte) {
  // L'affichage (grille, unité, opacité) reste celui du moment : ce n'est pas de l'historique
  const AffichageCircuit affichage = c.circuit.affichage;
  c = conceptionDepuisJson(nlohmann::ordered_json::parse(texte));
  c.circuit.affichage = affichage;
}
}  // namespace

void CommandeInstantane::appliquer(ConceptionElec& c) { remplacer(c, m_apres); }
void CommandeInstantane::defaire(ConceptionElec& c) { remplacer(c, m_avant); }

void CommandeInstantane::prolonger(const CommandeInstantane& suivante) {
  m_apres = suivante.m_apres;
  m_changement.fusionner(suivante.m_changement);
}

// ---------- Commande et ses suites ----------
CommandeAvecSuites::CommandeAvecSuites(std::unique_ptr<Commande> principale, std::unique_ptr<CommandeInstantane> suites)
    : m_principale(std::move(principale)), m_suites(std::move(suites)) {}

void CommandeAvecSuites::appliquer(ConceptionElec& c) {
  m_principale->appliquer(c);
  if (m_suites) m_suites->appliquer(c);
}

void CommandeAvecSuites::defaire(ConceptionElec& c) {
  if (m_suites) m_suites->defaire(c);
  m_principale->defaire(c);
}

ChangementConception CommandeAvecSuites::changement() const {
  ChangementConception ch = m_principale->changement();
  if (m_suites) ch.fusionner(m_suites->changement());
  return ch;
}

void CommandeAvecSuites::prolongerSuites(std::unique_ptr<CommandeInstantane> s) {
  if (!s) return;
  if (m_suites) m_suites->prolonger(*s);
  else m_suites = std::move(s);
}

// ---------- Noms ----------
std::string nommerChangement(const ConceptionElec& c, const ChangementConception& ch) {
  if (ch.entiere) return "Conception remplacée";
  // Un seul objet : son genre et son repère
  if (ch.objets.size() == 1) {
    const auto& [u, n] = *ch.objets.begin();
    std::string quoi = "objet";
    if (const ComposantElec* k = c.composant(u)) quoi = k->reference.empty() ? std::string("composant") : k->reference;
    for (const auto& f : c.feuilles)
      for (const auto& s : f.symboles)
        if (s.uuid == u) {
          const ComposantElec* k = c.composant(s.composant);
          quoi = k && !k->reference.empty() ? k->reference : s.nomAlim.empty() ? std::string("symbole") : s.nomAlim;
        }
    for (const auto& p : c.circuit.pistes)
      if (p.uuid == u) quoi = "piste " + (p.reseau.empty() ? std::string("sans réseau") : p.reseau);
    for (const auto& v : c.circuit.vias)
      if (v.uuid == u) quoi = "via " + (v.reseau.empty() ? std::string("sans réseau") : v.reseau);
    for (const auto& z : c.circuit.zones)
      if (z.uuid == u) quoi = "zone " + (z.reseau.empty() ? std::string("sans réseau") : z.reseau);
    if (n & N::Ajout) return "Ajout : " + quoi;
    if (n & N::Retrait) return "Suppression : " + quoi;
    if (n & N::Geometrie) return "Déplacement : " + quoi;
    if (!ch.proprietes.empty()) return "Modification de " + *ch.proprietes.begin() + " : " + quoi;
    return "Modification : " + quoi;
  }
  std::size_t ajouts = 0, retraits = 0;
  for (const auto& [u, n] : ch.objets) {
    ajouts += (n & N::Ajout) != 0;
    retraits += (n & N::Retrait) != 0;
  }
  if (ajouts && !retraits && ajouts == ch.objets.size()) return std::to_string(ajouts) + " objets ajoutés";
  if (retraits && !ajouts && retraits == ch.objets.size()) return std::to_string(retraits) + " objets supprimés";
  if (!ch.objets.empty()) return std::to_string(ch.objets.size()) + " objets modifiés";
  if (ch.proprietes.count("regles")) return "Règles de conception";
  if (ch.proprietes.count("contour")) return "Contour de la carte";
  if (ch.proprietes.count("empilage")) return "Empilage";
  if (ch.proprietes.count("cartouche")) return "Cartouche";
  if (!ch.proprietes.empty()) return "Modification de " + *ch.proprietes.begin();
  return "Modification";
}

}  // namespace prom::ecad

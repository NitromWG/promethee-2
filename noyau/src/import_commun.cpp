// Prométhée : chaîne d'import commune (10.12). Voir import_commun.hpp. Licence GPL-3.0-only.
#include "promethee/import_commun.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>

#include "promethee/circuit_edition.hpp"
#include "promethee/formats_anciens.hpp"
#include "promethee/formats_cao.hpp"
#include "promethee/import_altium.hpp"
#include "promethee/import_eagle.hpp"
#include "promethee/import_schemas.hpp"
#include "promethee/kicad_electronique.hpp"
#include "promethee/kicad_schema.hpp"

namespace prom::ecad {

// ---------- Lecture d'après le fichier ----------
namespace {
std::string extensionDe(const std::string& n) {
  const auto p = n.find_last_of('.');
  std::string e = p == std::string::npos ? std::string() : n.substr(p + 1);
  for (char& ch : e) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return e;
}
std::string baseDe(const std::string& n) {
  const auto b = n.find_last_of("/\\");
  const std::string f = b == std::string::npos ? n : n.substr(b + 1);
  const auto p = f.find_last_of('.');
  return p == std::string::npos ? f : f.substr(0, p);
}
bool estXmlEagle(const std::string& t) { return t.find("<eagle") != std::string::npos && t.find("<drawing") != std::string::npos; }
// Symboles d'un schéma gschem : à côté du fichier, puis dans les dossiers que nomme son « gafrc » (lignes
// « (component-library "dossier") »), enfin la bibliothèque standard incorporée
LecteurSymboles lecteurGschem(const LecteurVoisin& lire) {
  std::vector<std::string> dossiers{""};
  for (const char* rc : {"gafrc", "gschemrc", "lepton.conf"})
    if (const auto t = lire ? lire(rc) : std::nullopt)
      for (std::size_t p = t->find("component-library"); p != std::string::npos; p = t->find("component-library", p + 1)) {
        const auto a = t->find('"', p), b = a == std::string::npos ? a : t->find('"', a + 1);
        if (b != std::string::npos) dossiers.push_back(t->substr(a + 1, b - a - 1) + "/");
      }
  return [lire, dossiers](const std::string& n) -> std::optional<std::string> {
    if (!lire) return std::nullopt;
    for (const auto& d : dossiers)
      if (auto t = lire(d + n)) return t;
    return std::nullopt;
  };
}
void ajouterLiaison(ImportTiers& r, ConceptionElec& carte) {
  const RapportLiaison l = relierCarteAuSchema(r.conception, carte);
  for (const auto& x : l.carteSeule) r.alertes.push_back(x + " : posé sur la carte sans composant au schéma");
  for (const auto& x : l.schemaSeul) r.alertes.push_back(x + " : au schéma, absent de la carte");
  for (const auto& x : l.ecarts) r.alertes.push_back(x);
  r.conception.schemaSeul = false;
}
}  // namespace

std::string filtreConceptionsTierces() {
  return "*.kicad_pcb *.kicad_sch *.kicad_pro *.sch *.brd *.asc *.txt *.pcb *.PCB *.vsdx *.cpa *.csa *.PrjPcb *.SchDoc *.PcbDoc *.CSPcbDoc *.CSSchDoc *.CMPcbDoc *.CMSchDoc *.SWPcbDoc *.prjpcb *.schdoc *.pcbdoc *.lay6 *.lay";
}

std::optional<ImportTiers> importerConceptionTierce(const std::string& nomFichier, const std::string& contenu, const LecteurVoisin& lire, std::string* erreur) {
  const std::string ext = extensionDe(nomFichier), base = baseDe(nomFichier);
  auto voisin = [&](const std::string& e) -> std::optional<std::string> { return lire ? lire(base + "." + e) : std::nullopt; };
  ImportTiers r;
  try {
    if (ext == "kicad_pcb" || ext == "kicad_sch" || ext == "kicad_pro") {
      r.logiciel = "KiCad";
      const std::optional<std::string> pcb = ext == "kicad_pcb" ? std::optional<std::string>(contenu) : voisin("kicad_pcb");
      const std::optional<std::string> sch = ext == "kicad_sch" ? std::optional<std::string>(contenu) : voisin("kicad_sch");
      const std::optional<std::string> pro = ext == "kicad_pro" ? std::optional<std::string>(contenu) : voisin("kicad_pro");
      if (!pcb && !sch) {
        if (erreur) *erreur = "Ni schéma (.kicad_sch) ni carte (.kicad_pcb) à côté du projet « " + base + " ».";
        return std::nullopt;
      }
      std::optional<ConceptionElec> carte;
      if (pcb) {
        RapportImportKicad rk;
        carte = conceptionDepuisKicad(*pcb, base, &rk, pro.value_or(std::string()));
        r.fichiers.push_back(base + ".kicad_pcb");
        r.alertes.insert(r.alertes.end(), rk.alertes.begin(), rk.alertes.end());
      }
      if (sch) {
        RapportImportSchemaKicad rs;
        r.conception = conceptionDepuisSchemaKicad(*sch, base, [&](const std::string& f) { return lire ? lire(f) : std::nullopt; }, &rs);
        r.fichiers.push_back(base + ".kicad_sch");
        r.alertes.insert(r.alertes.end(), rs.alertes.begin(), rs.alertes.end());
        if (carte) {
          const RapportLiaison l = relierCarteAuSchema(r.conception, *carte);
          for (const auto& x : l.carteSeule) r.alertes.push_back(x + " : posé sur la carte sans composant au schéma");
          for (const auto& x : l.schemaSeul) r.alertes.push_back(x + " : au schéma, absent de la carte");
          for (const auto& x : l.ecarts) r.alertes.push_back(x);
        } else
          r.conception.schemaSeul = true;
      } else
        r.conception = std::move(*carte);
      if (pro) r.fichiers.push_back(base + ".kicad_pro");
    } else if ((ext == "sch" || ext == "brd") && estXmlEagle(contenu)) {
      r.logiciel = "EAGLE";
      const std::optional<std::string> sch = ext == "sch" ? std::optional<std::string>(contenu) : voisin("sch");
      const std::optional<std::string> brd = ext == "brd" ? std::optional<std::string>(contenu) : voisin("brd");
      RapportImportEagle re;
      r.conception = conceptionDepuisEagle(sch && estXmlEagle(*sch) ? sch : std::nullopt, brd && estXmlEagle(*brd) ? brd : std::nullopt, base, &re);
      if (sch) r.fichiers.push_back(base + ".sch");
      if (brd) r.fichiers.push_back(base + ".brd");
      r.alertes = re.alertes;
      if (!brd) r.conception.schemaSeul = true;
    } else if (ext == "prjpcb" || ext == "schdoc" || ext == "csschdoc" || ext == "cmschdoc" || ext == "pcbdoc" || ext == "cspcbdoc" || ext == "cmpcbdoc" || ext == "swpcbdoc") {
      // Altium : le projet s'il est à côté (schémas et carte reliés), sinon le document seul
      r.logiciel = ext.rfind("cs", 0) == 0 ? "CircuitStudio" : ext.rfind("cm", 0) == 0 ? "CircuitMaker" : ext == "swpcbdoc" ? "SolidWorks PCB" : "Altium";
      RapportImportAltium ra;
      auto lecteur = [&](const std::string& f) { return lire ? lire(f) : std::nullopt; };
      const std::optional<std::string> projet = ext == "prjpcb" ? std::optional<std::string>(contenu) : voisin("PrjPcb");
      if (projet)
        r.conception = conceptionDepuisProjetAltium(*projet, base, lecteur, &ra);
      else if (ext.find("sch") != std::string::npos)
        // Schéma et carte de même nom, sans projet : reliés comme dans un projet
        r.conception = conceptionDepuisAltium({{nomFichier, contenu}}, voisin("PcbDoc"), base, &ra);
      else {
        std::vector<std::pair<std::string, std::string>> schemas;
        if (const auto s = voisin("SchDoc")) schemas.push_back({base + ".SchDoc", *s});
        r.conception = conceptionDepuisAltium(schemas, contenu, base, &ra);
      }
      r.fichiers = ra.fichiers.empty() ? std::vector<std::string>{nomFichier} : ra.fichiers;
      r.alertes = ra.alertes;
    } else if (ext == "sch" && estSchemaKicad5(contenu)) {
      // KiCad 5 : schéma au format d'avant la 6, symboles tirés du cache ; la carte voisine (.kicad_pcb) est reliée
      r.logiciel = "KiCad 5";
      RapportImportSchema rs;
      r.conception = conceptionDepuisKicad5(contenu, base, [&](const std::string& f) { return lire ? lire(f) : std::nullopt; }, &rs);
      r.fichiers.push_back(base + ".sch");
      r.alertes = rs.alertes;
      if (const auto pcb = voisin("kicad_pcb")) {
        RapportImportKicad rk;
        ConceptionElec carte = conceptionDepuisKicad(*pcb, base, &rk, voisin("pro").value_or(std::string()));
        r.fichiers.push_back(base + ".kicad_pcb");
        r.alertes.insert(r.alertes.end(), rk.alertes.begin(), rk.alertes.end());
        const RapportLiaison l = relierCarteAuSchema(r.conception, carte);
        for (const auto& x : l.carteSeule) r.alertes.push_back(x + " : posé sur la carte sans composant au schéma");
        for (const auto& x : l.schemaSeul) r.alertes.push_back(x + " : au schéma, absent de la carte");
        for (const auto& x : l.ecarts) r.alertes.push_back(x);
        r.conception.schemaSeul = false;
      }
    } else if (ext == "sch" && estSchemaGschem(contenu)) {
      // gschem ou lepton-schematic : la carte gEDA voisine (.pcb) est reliée
      r.logiciel = contenu.find("lepton") != std::string::npos ? "Lepton" : "gEDA";
      RapportLectureCao rl;
      r.conception = conceptionDepuisGschem(contenu, base, lecteurGschem(lire), &rl);
      r.fichiers.push_back(nomFichier);
      r.alertes = rl.alertes;
      if (const auto pcb = voisin("pcb"); pcb && estGedaPcb(*pcb)) {
        RapportLectureCao rc;
        ConceptionElec carte = conceptionDepuisGedaPcb(*pcb, base, &rc);
        r.fichiers.push_back(base + ".pcb");
        r.alertes.insert(r.alertes.end(), rc.alertes.begin(), rc.alertes.end());
        ajouterLiaison(r, carte);
      }
    } else if (estSchemaLtspice(contenu)) {
      r.logiciel = "LTspice";
      RapportImportSchema rs;
      r.conception = conceptionDepuisLtspice(contenu, base, [&](const std::string& f) { return lire ? lire(f) : std::nullopt; }, &rs);
      r.fichiers.push_back(nomFichier);
      r.alertes = rs.alertes;
      if (!rs.directives.empty()) r.alertes.push_back(std::to_string(rs.directives.size()) + " directive(s) SPICE gardée(s) pour la simulation");
    } else if (ext == "vsdx") {
      r.logiciel = "Visio";
      RapportImportSchema rs;
      r.conception = conceptionDepuisVisio(contenu, base, &rs);
      r.fichiers.push_back(nomFichier);
      r.alertes = rs.alertes;
    } else if (estPadsAscii(contenu)) {
      r.logiciel = "PADS";
      const bool logique = contenu.find("*PADS-LOGIC") != std::string::npos || contenu.find("POWERLOGIC") != std::string::npos;
      RapportLectureCao rl;
      std::optional<std::string> autre;
      for (const char* e : {"asc", "txt"})
        if (!autre)
          if (auto v = voisin(e); v && *v != contenu && estPadsAscii(*v)) autre = v;
      const std::optional<std::string> logic = logique ? std::optional<std::string>(contenu) : (autre && autre->find("LOGIC") != std::string::npos ? autre : std::nullopt);
      const std::optional<std::string> layout = !logique ? std::optional<std::string>(contenu) : (autre && autre->find("LOGIC") == std::string::npos ? autre : std::nullopt);
      r.conception = conceptionDepuisPads(logic, layout, base, &rl);
      r.fichiers.push_back(nomFichier);
      r.alertes = rl.alertes;
    } else if (estArchiveCadstar(contenu)) {
      r.logiciel = "CADSTAR";
      RapportLectureCao rl;
      r.conception = conceptionDepuisCadstar(contenu, base, &rl);
      r.fichiers.push_back(nomFichier);
      r.alertes = rl.alertes;
    } else if (estPcadAscii(contenu)) {
      r.logiciel = "P-CAD";
      RapportLectureCao rl;
      r.conception = conceptionDepuisPcad(contenu, base, &rl);
      r.fichiers.push_back(nomFichier);
      r.alertes = rl.alertes;
    } else if (estSprintLayout(contenu)) {
      r.logiciel = "Sprint-Layout";
      RapportLectureCao rl;
      r.conception = conceptionDepuisSprintLayout(contenu, base, &rl);
      r.fichiers.push_back(nomFichier);
      r.alertes = rl.alertes;
    } else if (estAutotrax(contenu)) {
      r.logiciel = contenu.find("PCB FILE 5") != std::string::npos ? "Easytrax" : "Protel Autotrax";
      RapportLectureCao rl;
      r.conception = conceptionDepuisAutotrax(contenu, base, &rl);
      r.fichiers.push_back(nomFichier);
      r.alertes = rl.alertes;
    } else if (estGedaPcb(contenu)) {
      // gEDA PCB : le schéma gschem voisin (.sch), s'il y est, donne le schéma ; la carte s'y relie
      r.logiciel = "gEDA";
      RapportLectureCao rl;
      ConceptionElec carte = conceptionDepuisGedaPcb(contenu, base, &rl);
      r.fichiers.push_back(nomFichier);
      r.alertes = rl.alertes;
      if (const auto sch = voisin("sch"); sch && estSchemaGschem(*sch)) {
        RapportLectureCao rs;
        r.conception = conceptionDepuisGschem(*sch, base, lecteurGschem(lire), &rs);
        r.fichiers.push_back(base + ".sch");
        r.alertes.insert(r.alertes.end(), rs.alertes.begin(), rs.alertes.end());
        ajouterLiaison(r, carte);
      } else
        r.conception = std::move(carte);
    } else {
      if (erreur) *erreur = "Format de « " + nomFichier + " » non reconnu.";
      return std::nullopt;
    }
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return std::nullopt;
  }
  return r;
}

// ---------- Couches ----------
std::string fonctionCouche(Couche c) {
  if (estCuivre(c)) return "cuivre";
  switch (c) {
    case Couche::SerigraphieDessus: case Couche::SerigraphieDessous: return "sérigraphie";
    case Couche::VernisDessus: case Couche::VernisDessous: return "vernis épargne";
    case Couche::PateDessus: case Couche::PateDessous: return "pâte";
    case Couche::ColleDessus: case Couche::ColleDessous: return "colle";
    case Couche::ProtectionDessus: case Couche::ProtectionDessous: return "vernis de protection";
    case Couche::Contour: return "contour";
    case Couche::EncombrementDessus: case Couche::EncombrementDessous: return "encombrement";
    case Couche::FabricationDessus: case Couche::FabricationDessous: return "fabrication";
    case Couche::Dessins: return "dessin";
    case Couche::Commentaires: return "commentaire";
    default: return "personnelle";
  }
}

namespace {
// Chaque couche portée par un objet de la carte (hors empreintes, dont les couches suivent la face)
void parcourirCouches(ConceptionElec& c, const std::function<void(Couche&)>& f) {
  Circuit& k = c.circuit;
  for (auto& p : k.pistes) f(p.couche);
  for (auto& v : k.vias) {
    f(v.de);
    f(v.vers);
  }
  for (auto& z : k.zones) f(z.couche);
  for (auto& t : k.textes) f(t.couche);
  for (auto& d : k.dessins) f(d.couche);
  for (auto& x : k.cotes) f(x.couche);
  for (auto& r : k.regions) f(r.couche);
  for (auto& m : k.motifs) f(m.couche);
}
std::string nomOrigine(const ConceptionElec& c, Couche k) {
  const std::string code = codeCouche(k);
  if (const auto it = c.extensions.find("importCouches"); it != c.extensions.end() && it->second.is_object() && it->second.contains(code) && it->second.at(code).is_string())
    return it->second.at(code).get<std::string>();
  return code;
}
int cuivresPour(Couche k) {
  if (!estCuivreInterne(k)) return 2;
  for (int i = 1; i <= MAX_CUIVRES - 2; ++i)
    if (cuivreInterne(i) == k) return i + 2;
  return 2;
}
}  // namespace

AssociationCouches proposerAssociation(const std::string& logiciel, const ConceptionElec& importee, int cuivresDestination, const std::map<std::string, std::string>& gardee) {
  AssociationCouches a;
  a.logiciel = logiciel;
  std::map<Couche, int> objets;
  ConceptionElec copie = importee;  // parcours en écriture : sur une copie
  parcourirCouches(copie, [&](Couche& k) { ++objets[k]; });
  for (const auto& z : importee.circuit.zonesRegles)
    for (int i = 0; i < NB_COUCHES; ++i)
      if (z.couches & bit(static_cast<Couche>(i))) ++objets[static_cast<Couche>(i)];
  // Tous les cuivres de l'empilage d'origine, même vides : le tableau montre la carte entière
  for (Couche k : couchesCuivre(importee.circuit.empilage.couchesCuivre)) objets.try_emplace(k, 0);
  a.cuivresNecessaires = std::max(cuivresDestination, importee.circuit.empilage.couchesCuivre);
  for (const auto& [k, n] : objets) {
    LigneAssociation l;
    l.lue = k;
    l.origine = nomOrigine(importee, k);
    l.fonction = fonctionCouche(k);
    l.cible = k;  // les cuivres internes gardent leur rang : la destination reçoit les cuivres qui manquent
    l.objets = n;
    if (const auto g = gardee.find(l.origine); g != gardee.end()) {
      if (g->second.empty()) l.cible.reset();
      else if (const auto x = coucheDepuisCode(g->second)) l.cible = *x;
    }
    a.lignes.push_back(l);
  }
  int besoin = 2;
  for (const auto& l : a.lignes)
    if (l.cible && estCuivre(*l.cible)) besoin = std::max(besoin, cuivresPour(*l.cible));
  a.cuivresNecessaires = std::max(cuivresDestination, besoin);
  return a;
}

std::map<std::string, std::string> associationAGarder(const AssociationCouches& a) {
  std::map<std::string, std::string> r;
  for (const auto& l : a.lignes) r[l.origine] = l.cible ? codeCouche(*l.cible) : std::string();
  return r;
}

int appliquerAssociation(ConceptionElec& c, const AssociationCouches& a) {
  std::map<Couche, std::optional<Couche>> table;
  for (const auto& l : a.lignes) table[l.lue] = l.cible;
  auto cible = [&](Couche k) -> std::optional<Couche> {
    const auto it = table.find(k);
    return it == table.end() ? std::optional<Couche>(k) : it->second;
  };
  int n = 0;
  Circuit& k = c.circuit;
  auto filtrer = [&](auto& liste, auto&& couche) {
    const auto avant = liste.size();
    liste.erase(std::remove_if(liste.begin(), liste.end(), [&](auto& x) { return !cible(couche(x)); }), liste.end());
    n += static_cast<int>(avant - liste.size());
  };
  filtrer(k.pistes, [](const Piste& x) { return x.couche; });
  filtrer(k.zones, [](const Zone& x) { return x.couche; });
  filtrer(k.textes, [](const TexteCircuit& x) { return x.couche; });
  filtrer(k.dessins, [](const DessinCircuit& x) { return x.couche; });
  filtrer(k.cotes, [](const Cote& x) { return x.couche; });
  filtrer(k.regions, [](const RegionCuivre& x) { return x.couche; });
  filtrer(k.motifs, [](const MotifAccord& x) { return x.couche; });
  const auto avantVias = k.vias.size();
  k.vias.erase(std::remove_if(k.vias.begin(), k.vias.end(), [&](const Via& v) { return !cible(v.de) || !cible(v.vers); }), k.vias.end());
  n += static_cast<int>(avantVias - k.vias.size());
  parcourirCouches(c, [&](Couche& x) {
    const auto y = cible(x);
    if (y && *y != x) {
      x = *y;
      ++n;
    }
  });
  for (auto& z : k.zonesRegles) {
    if (!z.couches) continue;
    Couches m = 0;
    for (int i = 0; i < NB_COUCHES; ++i)
      if (z.couches & bit(static_cast<Couche>(i)))
        if (const auto y = cible(static_cast<Couche>(i))) m |= bit(*y);
    if (m != z.couches) ++n;
    z.couches = m;
  }
  // Via dont les deux bouts tombent sur la même couche : plus rien à relier, retirée
  const auto avant = k.vias.size();
  k.vias.erase(std::remove_if(k.vias.begin(), k.vias.end(), [](const Via& v) { return v.de == v.vers; }), k.vias.end());
  n += static_cast<int>(avant - k.vias.size());
  int besoin = 2;
  for (const auto& l : a.lignes)
    if (l.cible && estCuivre(*l.cible) && l.objets > 0) besoin = std::max(besoin, cuivresPour(*l.cible));
  k.empilage.couchesCuivre = std::max(besoin, std::min(k.empilage.couchesCuivre, a.cuivresNecessaires));
  return n;
}

// ---------- Carte et schéma lus séparément ----------
RapportLiaison relierCarteAuSchema(ConceptionElec& schema, const ConceptionElec& carte) {
  RapportLiaison r;
  const Connectivite kc = connectivite(carte);
  Circuit circuit = carte.circuit;
  circuit.empreintes.clear();
  std::map<std::string, std::string> uuidSchema;  // UUID de la carte → UUID du schéma
  std::set<std::string> poses;
  for (const auto& em : carte.circuit.empreintes) {
    const ComposantElec* x = carte.composant(em.composant);
    if (!x) continue;
    EmpreintePlacee e = em;
    ComposantElec* s = nullptr;
    for (auto& y : schema.composants)
      if (y.reference == x->reference && !y.carteSeule) s = &y;
    if (s) {
      e.composant = s->uuid;
      s->empreinte = em.empreinte;
      uuidSchema[x->uuid] = s->uuid;
      poses.insert(s->uuid);
      ++r.relies;
    } else {
      ComposantElec n = *x;
      n.carteSeule = true;
      n.symbole.clear();
      schema.composants.push_back(n);
      uuidSchema[x->uuid] = n.uuid;
      r.carteSeule.push_back(x->reference);
      // Ses réseaux viennent de la carte : noms figés des pastilles
      if (const DefEmpreinte* d = carte.empreinte(em.empreinte))
        for (const auto& p : d->pastilles)
          if (const std::string nom = kc.nom(x->uuid, p.numero); !nom.empty()) schema.nomsReseaux[n.uuid + ":" + p.numero] = nom;
    }
    if (const DefEmpreinte* d = carte.empreinte(em.empreinte)) schema.empreintes[em.empreinte] = *d;
    circuit.empreintes.push_back(e);
  }
  for (const auto& y : schema.composants)
    if (!y.carteSeule && !y.horsCarte && !poses.count(y.uuid) && !y.symbole.empty()) {
      const DefSymbole* d = schema.symbole(y.symbole);
      if (!d || !d->alimentation) r.schemaSeul.push_back(y.reference);
    }
  schema.circuit = circuit;
  schema.schemaSeul = false;
  synchroniserReseaux(schema);
  // Cuivre : chaque réseau de la carte prend le nom que le schéma donne à ses pastilles
  const Connectivite ks = connectivite(schema);
  std::map<std::string, std::set<std::string>> vers;
  for (const auto& em : carte.circuit.empreintes) {
    const auto u = uuidSchema.find(em.composant);
    const DefEmpreinte* d = carte.empreinte(em.empreinte);
    if (u == uuidSchema.end() || !d) continue;
    const ComposantElec* x = carte.composant(em.composant);
    for (const auto& p : d->pastilles) {
      if (p.numero.empty()) continue;
      const std::string a = kc.nom(em.composant, p.numero), b = ks.nom(u->second, p.numero);
      if (a.empty() || b.empty()) continue;
      vers[a].insert(b);
      if (a != b && carte.circuit.pistes.end() != std::find_if(carte.circuit.pistes.begin(), carte.circuit.pistes.end(), [&](const Piste& t) { return t.reseau == a; }) && vers[a].size() > 1)
        r.ecarts.push_back((x ? x->reference : em.composant) + "." + p.numero + " : « " + a + " » sur la carte, « " + b + " » au schéma");
    }
  }
  std::map<std::string, std::string> renommer;
  for (const auto& [a, bs] : vers)
    if (bs.size() == 1 && *bs.begin() != a) renommer[a] = *bs.begin();
  auto suivre = [&](std::string& reseau) {
    if (const auto it = renommer.find(reseau); it != renommer.end()) reseau = it->second;
  };
  for (auto& p : schema.circuit.pistes) suivre(p.reseau);
  for (auto& v : schema.circuit.vias) suivre(v.reseau);
  for (auto& z : schema.circuit.zones) suivre(z.reseau);
  for (auto& g : schema.circuit.regions) suivre(g.reseau);
  return r;
}

// ---------- Import dans la carte ouverte ----------
namespace {
std::pair<std::string, long> prefixeNumero(const std::string& ref) {
  std::size_t i = ref.size();
  while (i > 0 && std::isdigit(static_cast<unsigned char>(ref[i - 1]))) --i;
  if (i == ref.size() || ref.size() - i > 9) return {ref, -1};
  return {ref.substr(0, i), std::stol(ref.substr(i))};
}
Boite boiteCarte(const ConceptionElec& c) {
  Boite b;
  const ContourCarte& k = c.circuit.contour;
  if (!k.libre.empty())
    for (const Pt& p : k.libre) b.etendre(p);
  else {
    b.etendre(k.origine);
    b.etendre(k.origine + Pt{k.largeur, k.hauteur});
  }
  for (const auto& em : c.circuit.empreintes) b.etendre(em.position);
  return b;
}
std::set<std::string> nomsReseauxDe(const ConceptionElec& c) {
  std::set<std::string> r;
  const Connectivite k = connectivite(c);
  for (const auto& x : k.reseaux)
    if (x.nomme) r.insert(x.nom);
  for (const auto& p : c.circuit.pistes) r.insert(p.reseau);
  for (const auto& z : c.circuit.zones) r.insert(z.reseau);
  r.erase(std::string());
  return r;
}
}  // namespace

RapportFusion apercuFusion(const ConceptionElec& ouverte, const ConceptionElec& ajoutee, const OptionsFusion& o) {
  RapportFusion r;
  std::map<std::string, long> plusGrand;
  std::set<std::string> pris;
  for (const auto& x : ouverte.composants) {
    pris.insert(x.reference);
    const auto [p, n] = prefixeNumero(x.reference);
    if (n >= 0) plusGrand[p] = std::max(plusGrand[p], n);
  }
  for (const auto& x : ajoutee.composants) {
    const auto [p, n] = prefixeNumero(x.reference);
    if (n >= 0) plusGrand[p] = std::max(plusGrand[p], ouverte.parReference(x.reference) ? plusGrand[p] : plusGrand[p]);
  }
  for (const auto& x : ajoutee.composants) {
    if (!pris.count(x.reference)) {
      pris.insert(x.reference);
      continue;
    }
    const auto [p, n] = prefixeNumero(x.reference);
    std::string nouveau;
    long k = std::max(plusGrand[p], n);
    do nouveau = (n >= 0 ? p : x.reference + "_") + std::to_string(++k);
    while (pris.count(nouveau));
    plusGrand[p] = k;
    pris.insert(nouveau);
    r.renumerotes.push_back({x.reference, nouveau});
  }
  const auto a = nomsReseauxDe(ouverte), b = nomsReseauxDe(ajoutee);
  std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(r.homonymes));
  r.cuivresAvant = ouverte.circuit.empilage.couchesCuivre;
  r.cuivresApres = std::max(ouverte.circuit.empilage.couchesCuivre, ajoutee.circuit.empilage.couchesCuivre);
  if (o.decaler) {
    const Boite bo = boiteCarte(ouverte), ba = boiteCarte(ajoutee);
    if (!bo.vide() && !ba.vide()) r.decalage = Pt{bo.x1 + 5 * NM_PAR_MM - ba.x0, bo.y0 - ba.y0};
  } else
    r.decalage = o.decalage;
  r.composants = static_cast<int>(ajoutee.composants.size());
  r.pistes = static_cast<int>(ajoutee.circuit.pistes.size());
  r.vias = static_cast<int>(ajoutee.circuit.vias.size());
  r.zones = static_cast<int>(ajoutee.circuit.zones.size());
  r.feuilles = o.feuilles ? static_cast<int>(ajoutee.feuilles.size()) : 0;
  return r;
}

RapportFusion fusionnerConception(ConceptionElec& ouverte, ConceptionElec ajoutee, const OptionsFusion& o) {
  if (ouverte.verrouillee) throw ConceptionVerrouillee(ouverte);
  RapportFusion r = apercuFusion(ouverte, ajoutee, o);
  // Repères en double
  std::map<std::string, std::string> renum(r.renumerotes.begin(), r.renumerotes.end());
  std::set<std::string> dejaRenumerote;
  for (auto& x : ajoutee.composants)
    if (const auto it = renum.find(x.reference); it != renum.end() && !dejaRenumerote.count(x.reference)) {
      dejaRenumerote.insert(x.reference);
      x.reference = it->second;
    }
  // Identifiants déjà pris dans la conception ouverte : renouvelés
  std::set<std::string> uuids;
  for (const auto& x : ouverte.composants) uuids.insert(x.uuid);
  std::map<std::string, std::string> nouveaux;
  for (auto& x : ajoutee.composants)
    if (uuids.count(x.uuid)) {
      const std::string n = nouvelUuid();
      nouveaux[x.uuid] = n;
      x.uuid = n;
    }
  if (!nouveaux.empty()) {
    auto suivre = [&](std::string& u) {
      if (const auto it = nouveaux.find(u); it != nouveaux.end()) u = it->second;
    };
    for (auto& em : ajoutee.circuit.empreintes) suivre(em.composant);
    for (auto& f : ajoutee.feuilles)
      for (auto& s : f.symboles) suivre(s.composant);
    std::map<std::string, std::string> noms;
    for (const auto& [cle, nom] : ajoutee.nomsReseaux) {
      std::string u = cle.substr(0, cle.find(':'));
      suivre(u);
      noms[u + cle.substr(cle.find(':'))] = nom;
    }
    ajoutee.nomsReseaux = noms;
  }
  std::set<std::string> objets;
  for (const auto& p : ouverte.circuit.pistes) objets.insert(p.uuid);
  for (const auto& v : ouverte.circuit.vias) objets.insert(v.uuid);
  for (const auto& z : ouverte.circuit.zones) objets.insert(z.uuid);
  for (auto& p : ajoutee.circuit.pistes)
    if (objets.count(p.uuid)) p.uuid = nouvelUuid();
  for (auto& v : ajoutee.circuit.vias)
    if (objets.count(v.uuid)) v.uuid = nouvelUuid();
  for (auto& z : ajoutee.circuit.zones)
    if (objets.count(z.uuid)) z.uuid = nouvelUuid();
  // Réseaux homonymes : réunis (étiquettes rendues globales des deux côtés) ou séparés (suffixés)
  const std::set<std::string> homonymes(r.homonymes.begin(), r.homonymes.end());
  if (o.reunirHomonymes) {
    for (auto* c : {&ouverte, &ajoutee})
      for (auto& f : c->feuilles)
        for (auto& e : f.etiquettes)
          if (homonymes.count(e.texte) && e.type == TypeEtiquette::Locale) e.type = TypeEtiquette::Globale;
  } else {
    auto renommer = [&](std::string& n) {
      if (homonymes.count(n)) n += "_2";
    };
    for (auto& f : ajoutee.feuilles) {
      for (auto& e : f.etiquettes) renommer(e.texte);
      for (auto& s : f.symboles) renommer(s.nomAlim);
    }
    for (auto& [cle, nom] : ajoutee.nomsReseaux) renommer(nom);
    for (auto& p : ajoutee.circuit.pistes) renommer(p.reseau);
    for (auto& v : ajoutee.circuit.vias) renommer(v.reseau);
    for (auto& z : ajoutee.circuit.zones) renommer(z.reseau);
    for (auto& g : ajoutee.circuit.regions) renommer(g.reseau);
  }
  // Décalage de tout ce que porte la carte ajoutée ; son contour devient un dessin à côté de la carte ouverte
  const Pt d = r.decalage;
  Circuit& k = ajoutee.circuit;
  for (auto& em : k.empreintes) {
    em.position = em.position + d;  // le repère déplacé à la main est dans le repère de l'empreinte : il suit
  }
  for (auto& p : k.pistes) {
    p.a = p.a + d;
    p.b = p.b + d;
    if (p.milieu) *p.milieu = *p.milieu + d;
  }
  for (auto& v : k.vias) v.p = v.p + d;
  for (auto& z : k.zones)
    for (Pt& q : z.contour) q = q + d;
  for (auto& t : k.textes) t.p = t.p + d;
  for (auto& x : k.dessins)
    for (Pt& q : x.points) q = q + d;
  for (auto& x : k.cotes)
    for (Pt& q : x.points) q = q + d;
  for (auto& x : k.zonesRegles)
    for (Pt& q : x.contour) q = q + d;
  for (auto& x : k.regions)
    for (Pt& q : x.forme) q = q + d;
  for (auto& x : k.motifs) {
    x.a = x.a + d;
    x.b = x.b + d;
  }
  {
    DessinCircuit contour;
    contour.uuid = nouvelUuid();
    contour.couche = Couche::Dessins;
    contour.forme = DessinCircuit::Forme::Polygone;
    if (!k.contour.libre.empty())
      for (const Pt& q : k.contour.libre) contour.points.push_back(q + d);
    else {
      const Pt o0 = k.contour.origine + d;
      contour.points = {o0, o0 + Pt{k.contour.largeur, 0}, o0 + Pt{k.contour.largeur, k.contour.hauteur}, o0 + Pt{0, k.contour.hauteur}};
    }
    k.dessins.push_back(contour);
  }
  // Définitions : celles de la conception ouverte l'emportent (même identifiant)
  for (auto& [id, s] : ajoutee.symboles) ouverte.symboles.try_emplace(id, s);
  for (auto& [id, e] : ajoutee.empreintes) ouverte.empreintes.try_emplace(id, e);
  for (auto& [id, a] : ajoutee.articles) ouverte.articles.try_emplace(id, a);
  for (auto& [id, m] : ajoutee.modeles3D) ouverte.modeles3D.try_emplace(id, m);
  for (auto& x : ajoutee.composants) ouverte.composants.push_back(std::move(x));
  for (auto& [cle, nom] : ajoutee.nomsReseaux) ouverte.nomsReseaux.try_emplace(cle, nom);
  Circuit& c = ouverte.circuit;
  auto ajouter = [](auto& dst, auto& src) { dst.insert(dst.end(), std::make_move_iterator(src.begin()), std::make_move_iterator(src.end())); };
  ajouter(c.empreintes, k.empreintes);
  ajouter(c.pistes, k.pistes);
  ajouter(c.vias, k.vias);
  ajouter(c.zones, k.zones);
  ajouter(c.textes, k.textes);
  ajouter(c.dessins, k.dessins);
  ajouter(c.cotes, k.cotes);
  ajouter(c.zonesRegles, k.zonesRegles);
  ajouter(c.regions, k.regions);
  ajouter(c.motifs, k.motifs);
  for (const auto& kl : k.regles.classes)
    if (std::none_of(c.regles.classes.begin(), c.regles.classes.end(), [&](const auto& x) { return x.nom == kl.nom; })) c.regles.classes.push_back(kl);
  for (const auto& [motif, classe] : k.regles.classeReseau)
    if (!c.regles.classeReseau.count(motif)) c.regles.classeReseau[motif] = classe;
  c.empilage.couchesCuivre = r.cuivresApres;
  if (o.feuilles) {
    std::set<std::string> ids;
    for (const auto& f : ouverte.feuilles) ids.insert(f.uuid);
    for (auto& f : ajoutee.feuilles) {
      if (ids.count(f.uuid)) f.uuid = nouvelUuid();
      f.nom = ajoutee.nom + " · " + f.nom;
      ouverte.feuilles.push_back(std::move(f));
    }
  }
  ouverte.schemaSeul = false;
  synchroniserReseaux(ouverte);
  return r;
}

}  // namespace prom::ecad

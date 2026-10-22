// Prométhée : jeux de sorties (lot L13). Licence GPL-3.0-only.
#include "promethee/sorties.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>

#include "promethee/compression.hpp"
#include "promethee/echanges_electronique.hpp"
#include "promethee/fabrication_avancee.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/variantes.hpp"
#include "promethee/verification.hpp"

namespace fs = std::filesystem;

namespace prom::ecad {

namespace {
std::string aujourdhui() {
  const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char s[16];
  std::strftime(s, sizeof s, "%Y-%m-%d", &tm);
  return s;
}
std::string mm(Nm v) {
  char s[32];
  std::snprintf(s, sizeof s, "%.3f", static_cast<double>(v) * 1e-6);
  return s;
}
std::string champCsv(const std::string& v) {
  if (v.find_first_of(",\"\n") == std::string::npos) return v;
  std::string r = "\"";
  for (char ch : v) r += ch == '"' ? std::string("\"\"") : std::string(1, ch);
  return r + "\"";
}
// Différences d'une empreinte figée et de celle de la bibliothèque, pastille par pastille
std::vector<std::string> differencesEmpreinte(const DefEmpreinte& projet, const DefEmpreinte& biblio) {
  std::vector<std::string> r;
  if (projet.pastilles.size() != biblio.pastilles.size())
    r.push_back(std::to_string(projet.pastilles.size()) + " pastilles dans le projet, " + std::to_string(biblio.pastilles.size()) + " dans la bibliothèque");
  for (const auto& p : projet.pastilles) {
    const auto it = std::find_if(biblio.pastilles.begin(), biblio.pastilles.end(), [&](const Pastille& q) { return q.numero == p.numero; });
    if (it == biblio.pastilles.end()) {
      r.push_back("pastille " + p.numero + " absente de la bibliothèque");
      continue;
    }
    if (p.position != it->position) r.push_back("pastille " + p.numero + " déplacée : (" + mm(p.position.x) + " ; " + mm(p.position.y) + ") dans le projet, (" + mm(it->position.x) + " ; " + mm(it->position.y) + ") dans la bibliothèque");
    if (p.largeur != it->largeur || p.hauteur != it->hauteur)
      r.push_back("pastille " + p.numero + " : " + mm(p.largeur) + " × " + mm(p.hauteur) + " mm dans le projet, " + mm(it->largeur) + " × " + mm(it->hauteur) + " mm dans la bibliothèque");
    if (p.percage != it->percage) r.push_back("pastille " + p.numero + " : perçage " + mm(p.percage) + " mm dans le projet, " + mm(it->percage) + " mm dans la bibliothèque");
    if (p.forme != it->forme || p.type != it->type || p.rotation != it->rotation) r.push_back("pastille " + p.numero + " : forme, type ou rotation différents");
  }
  if (r.empty() && !(projet == biblio)) r.push_back("traits, encombrement ou propriétés différents");
  return r;
}
}  // namespace

const std::vector<std::pair<std::string, std::string>>& sortiesConnues() {
  static const std::vector<std::pair<std::string, std::string>> s = {
      {"fabrication", "Dossier de fabrication complet"},
      {"gerber", "Gerber X2 et fichier de travail"},
      {"percages", "Perçages Excellon"},
      {"placement", "Fichier de placement"},
      {"placement-en", "Fichier de placement (en-têtes des assembleurs)"},
      {"nomenclature", "Nomenclature CSV"},
      {"nomenclature-jlcpcb", "Nomenclature au format JLCPCB"},
      {"ipc356", "Liste de réseaux IPC-D-356A"},
      {"ipc2581", "IPC-2581"},
      {"netlist", "Liste de réseaux KiCad"},
      {"boardview", "BoardView (réparation)"},
      {"specctra", "Specctra DSN (routeur externe)"},
      {"dxf", "DXF de la mécanique"},
      {"visionneuse", "Visionneuse HTML pour un relecteur"},
      {"cout", "Coût de la nomenclature"},
      {"variantes", "Tableau des variantes"},
      {"projet", "Conception au format Prométhée"},
  };
  return s;
}

std::vector<std::string> controlerAvantSorties(const ConceptionElec& c, std::string* rapport, std::vector<std::string>* alertes) {
  std::vector<std::string> refus;
  std::ostringstream rap;
  for (const auto& [id, figee] : c.empreintes) {
    const auto biblio = genererEmpreinte(id);
    if (!biblio || *biblio == figee) continue;
    const auto d = differencesEmpreinte(figee, *biblio);
    std::vector<std::string> refs;
    for (const auto& k : c.composants)
      if (k.empreinte == id) refs.push_back(k.reference);
    std::sort(refs.begin(), refs.end(), ordreNaturel);
    std::string liste;
    for (const auto& r : refs) liste += (liste.empty() ? "" : ", ") + r;
    refus.push_back("Empreinte « " + id + " » du projet différente de celle de la bibliothèque" + (liste.empty() ? std::string() : " (" + liste + ")") + ".");
    rap << "Empreinte " << id << (liste.empty() ? "" : " — " + liste) << "\n";
    for (const auto& x : d) rap << "  · " << x << "\n";
  }
  for (const auto& k : c.composants) {
    if (k.nePasMonter || !dansNomenclature(c, k)) continue;
    if (!k.article.empty())
      if (const auto it = c.articles.find(k.article); it != c.articles.end()) {
        const Article& a = it->second;
        if (a.generique) refus.push_back(k.reference + " : article générique « " + a.nom + " » à remplacer par un article réel.");
        if (a.etatCycle == "obsolète" || a.etatCycle == "obsolete") refus.push_back(k.reference + " : article « " + a.nom + " » obsolète.");
        else if (alertes && (a.etatCycle == "fin de série" || a.etatCycle == "brouillon")) alertes->push_back(k.reference + " : article « " + a.nom + " » en état « " + a.etatCycle + " ».");
        continue;
      }
    if (alertes && k.mpn.empty() && k.lcsc.empty()) alertes->push_back(k.reference + " : ni article ni référence fabricant.");
  }
  if (rapport) *rapport = rap.str();
  return refus;
}

std::string dossierDuJeu(const JeuSorties& j, const ConceptionElec& c, const std::string& projet, const std::map<std::string, std::string>& variables) {
  std::map<std::string, std::string> v = {{"projet", projet},
                                         {"carte", nomFichierAscii(c.nom, "carte")},
                                         {"revision", c.cartouche.revision},
                                         {"variante", j.variante.empty() ? std::string("base") : j.variante},
                                         {"date", aujourdhui()}};
  // -D CLE=VALEUR l'emporte, sous son nom et en minuscules (-D REVISION=C donne {revision})
  for (const auto& [cle, valeur] : variables) {
    v[cle] = valeur;
    std::string m = cle;
    for (auto& ch : m) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    v[m] = valeur;
  }
  std::string r = j.dossier, s;
  for (std::size_t i = 0; i < r.size(); ++i) {
    if (r[i] == '{') {
      const std::size_t f = r.find('}', i);
      if (f != std::string::npos)
        if (const auto it = v.find(r.substr(i + 1, f - i - 1)); it != v.end()) {
          s += it->second;
          i = f;
          continue;
        }
    }
    s += r[i];
  }
  return s;
}

ResultatJeu executerJeu(const ConceptionElec& c0, const JeuSorties& j, const std::string& base, const std::string& projet, const std::string& version,
                        const std::map<std::string, std::string>& variables) {
  ResultatJeu res;
  const ConceptionElec c = conceptionPourVariante(c0, j.variante);
  fs::path cible = cheminFichier(dossierDuJeu(j, c, projet, variables));
  if (cible.is_relative()) cible = cheminFichier(base) / cible;
  res.dossier = utf8(cible) + (j.zip ? ".zip" : "");
  res.journal.push_back("Jeu « " + j.nom + " »" + (j.variante.empty() ? "" : ", variante « " + j.variante + " »") + " → " + res.dossier);
  if (j.controler) {
    std::vector<std::string> alertes;
    res.refus = controlerAvantSorties(c, &res.rapport, &alertes);
    for (const auto& a : alertes) res.journal.push_back("alerte : " + a);
    if (!res.refus.empty()) {
      // Rien n'est écrit ; le rapport des différences est joint à côté du dossier visé
      fs::create_directories(cible.parent_path());
      std::string texte = "Publication refusée : " + j.nom + "\n\n";
      for (const auto& r : res.refus) texte += "- " + r + "\n";
      if (!res.rapport.empty()) texte += "\nDifférences d'empreintes :\n" + res.rapport;
      ecrireFichierAtomique(cheminFichier(utf8(cible) + "-refus.txt"), texte);
      res.journal.push_back("refusé : " + std::to_string(res.refus.size()) + " contrôle(s) en échec, rapport « " + utf8(cible.filename()) + "-refus.txt »");
      return res;
    }
  }
  // Préparation dans un dossier temporaire, livrée d'un coup
  const fs::path prep = cheminFichier(utf8(cible) + ".preparation");
  std::error_code ec;
  fs::remove_all(prep, ec);
  fs::create_directories(prep);
  const std::string nom = nomFichierAscii(c.nom, "carte");
  std::vector<std::pair<std::string, std::string>> fichiers;  // relatif → contenu (pour le zip)
  const auto ecrire = [&](const std::string& relatif, const std::string& contenu) {
    const fs::path p = prep / cheminFichier(relatif);
    fs::create_directories(p.parent_path());
    ecrireFichierAtomique(p, contenu);
    fichiers.emplace_back(relatif, contenu);
    res.fichiers.push_back(relatif);
  };
  // Calculs partagés, faits une seule fois et seulement si une sortie les demande
  std::optional<Connectivite> k;
  std::optional<Remplissages> zones;
  std::optional<ModeleFabrication> modele;
  const auto connexe = [&]() -> const Connectivite& {
    if (!k) k = connectivite(c);
    return *k;
  };
  const auto fab = [&]() -> const ModeleFabrication& {
    if (!modele) {
      zones = remplirZones(c, connexe());
      modele = modeleFabrication(c, connexe(), *zones);
    }
    return *modele;
  };
  for (const auto& s : j.sorties) {
    if (s == "fabrication") {
      for (const auto& f : ecrireDossierFabrication(c, utf8(prep / "fabrication"), version)) {
        const std::string rel = "fabrication/" + f;
        fichiers.emplace_back(rel, lireFichier(prep / cheminFichier(rel)));
        res.fichiers.push_back(rel);
      }
    } else if (s == "gerber") {
      std::vector<std::pair<std::string, Couche>> gerbers;
      for (const auto& cf : fab().couches) {
        std::string code = codeCouche(cf.couche);
        std::replace(code.begin(), code.end(), '.', '_');
        ecrire("Gerber/" + nom + "-" + code + ".gbr", gerber(fab(), cf.couche, version));
        gerbers.push_back({nom + "-" + code + ".gbr", cf.couche});
      }
      ecrire("Gerber/" + nom + "-job.gbrjob", gerberJob(c, fab(), gerbers, version));
    } else if (s == "percages") {
      ecrire("Percage/" + nom + "-PTH.drl", excellon(fab(), true, version));
      ecrire("Percage/" + nom + "-NPTH.drl", excellon(fab(), false, version));
    } else if (s == "placement")
      ecrire("Assemblage/" + nom + "-placement.csv", csvPlacement(c));
    else if (s == "placement-en")
      ecrire("Assemblage/" + nom + "-placement-en.csv", csvPlacement(c, true));
    else if (s == "nomenclature")
      ecrire("Nomenclature/" + nom + "-nomenclature.csv", csvNomenclature(c));
    else if (s == "nomenclature-jlcpcb") {
      std::string t = "Comment,Designator,Footprint,LCSC Part #\n";
      for (const auto& l : nomenclature(c)) {
        std::string refs;
        for (const auto& r : l.references) refs += (refs.empty() ? "" : ",") + r;
        t += champCsv(l.valeur) + "," + champCsv(refs) + "," + champCsv(l.empreinte) + "," + champCsv(l.lcsc) + "\n";
      }
      ecrire("Assemblage/" + nom + "-nomenclature-jlcpcb.csv", t);
    } else if (s == "ipc356")
      ecrire("Test/" + nom + "-netlist.ipc", ipcD356(c, connexe()));
    else if (s == "ipc2581")
      ecrire(nom + ".ipc2581.xml", ipc2581(c, version));
    else if (s == "netlist")
      ecrire(nom + ".net", netlistKicad(c, connexe()));
    else if (s == "boardview")
      ecrire(nom + ".brd", boardView(c));
    else if (s == "specctra")
      ecrire(nom + ".dsn", specctraDsn(c));
    else if (s == "dxf")
      ecrire(nom + "-mecanique.dxf", dxfMecaniqueCarte(c));
    else if (s == "visionneuse")
      ecrire(nom + "-visionneuse.html", visionneuseHtml(c));
    else if (s == "cout") {
      int quantite = 10;
      if (const auto it = variables.find("QUANTITE"); it != variables.end()) quantite = std::max(1, std::atoi(it->second.c_str()));
      const CoutNomenclature cn = coutNomenclature(c0, quantite, j.variante);
      std::string t = "Repères;Quantité par carte;Quantité commandée;Article;Référence fabricant;Distributeur;Prix unitaire;Total\n";
      for (const auto& l : cn.lignes) {
        std::string refs;
        for (const auto& r : l.references) refs += (refs.empty() ? "" : ", ") + r;
        char pu[32] = "", tot[32] = "";
        if (l.prixConnu) {
          std::snprintf(pu, sizeof pu, "%.4f", l.prixUnitaire);
          std::snprintf(tot, sizeof tot, "%.2f", l.total);
        }
        t += refs + ";" + std::to_string(l.parCarte) + ";" + std::to_string(l.commandee) + ";" + l.article + ";" + l.mpn + ";" + l.distributeur + ";" + pu + ";" + tot + "\n";
      }
      char total[96];
      std::snprintf(total, sizeof total, "Total pour %d cartes;;;;;;;%.2f\n", cn.cartes, cn.total);
      ecrire(nom + "-cout.csv", t + total);
    } else if (s == "variantes") {
      std::string t = "Repère;Base";
      for (const auto& v : c0.variantesMontage) t += ";" + v.nom;
      t += "\n";
      for (const auto& l : tableauVariantes(c0)) {
        t += l.reference + ";" + l.base;
        for (const auto& x : l.cellules) t += ";" + x;
        t += "\n";
      }
      ecrire(nom + "-variantes.csv", t);
    } else if (s == "projet")
      ecrire(nom + ".conception.json", versJson(c0).dump(2) + "\n");
    else
      res.journal.push_back("sortie inconnue ignorée : « " + s + " »");
  }
  // Livraison : l'archive, ou le dossier préparé à la place de l'ancien (gardé en « .precedent »)
  if (j.zip) {
    fs::create_directories(cible.parent_path());
    ecrireFichierAtomique(cheminFichier(res.dossier), zipComprime(fichiers));
    fs::remove_all(prep, ec);
  } else {
    if (fs::exists(cible)) {
      const fs::path ancien = cheminFichier(utf8(cible) + ".precedent");
      fs::remove_all(ancien, ec);
      fs::rename(cible, ancien, ec);
    }
    fs::create_directories(cible.parent_path());
    fs::rename(prep, cible, ec);
    if (ec) throw std::runtime_error("livraison du jeu impossible : " + ec.message());
  }
  res.publie = true;
  res.journal.push_back(std::to_string(res.fichiers.size()) + " fichier(s) livrés");
  return res;
}

JeuSorties jeuJlcpcb() {
  JeuSorties j;
  j.nom = "Dossier JLCPCB";
  j.dossier = "sorties/{carte}-{revision}-jlcpcb";
  j.sorties = {"gerber", "percages", "placement-en", "nomenclature-jlcpcb"};
  j.zip = true;
  return j;
}

}  // namespace prom::ecad

// Prométhée : assistant de conception. Voir assistant.hpp. Licence GPL-3.0-only.
#include "promethee/assistant.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <random>
#include <regex>
#include <sstream>

#include "promethee/fichiers.hpp"
#include "promethee/pdf_documents.hpp"
#include "promethee/processus.hpp"
#include "promethee/simulation.hpp"
#include "promethee/simulation_projet.hpp"

namespace fs = std::filesystem;
using J = nlohmann::ordered_json;

namespace prom {

using namespace ecad;

namespace {
std::string normal(const std::string& s) {
  std::string t = translitterer(s);
  std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return t;
}
std::string sansBlancs(const std::string& s) {
  const auto d = s.find_first_not_of(" \t\r\n");
  if (d == std::string::npos) return {};
  return s.substr(d, s.find_last_not_of(" \t\r\n") - d + 1);
}
std::vector<std::string> jetons(const std::string& t) {
  std::vector<std::string> v;
  std::string courant;
  for (const char ch : normal(t)) {
    if (std::isalnum(static_cast<unsigned char>(ch))) courant += ch;
    else if (!courant.empty()) v.push_back(courant), courant.clear();
  }
  if (!courant.empty()) v.push_back(courant);
  return v;
}
std::string virgule(double v, int chiffres = 3) {
  char b[48];
  std::snprintf(b, sizeof b, "%.*g", chiffres, v);
  std::string s = b;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
std::string dossierTemporaire() {
  std::random_device r;
  std::error_code ec;
  const fs::path p = fs::temp_directory_path(ec) / ("promethee-assistant-" + std::to_string(r()) + std::to_string(r()));
  fs::create_directories(p, ec);
  return utf8(p);
}
}  // namespace

// ---------- Moteur ----------
J moteurVersJson(const MoteurAssistant& m) {
  return J{{"genre", m.genre}, {"programme", m.programme}, {"modele", m.modele}, {"arguments", m.arguments}, {"jetons", m.jetons}, {"delai", m.delai}};
}

MoteurAssistant moteurDepuisJson(const J& j) {
  MoteurAssistant m;
  if (!j.is_object()) return m;
  m.genre = j.value("genre", std::string("aucun"));
  m.programme = j.value("programme", std::string());
  m.modele = j.value("modele", std::string());
  if (j.contains("arguments") && j["arguments"].is_array())
    for (const auto& x : j["arguments"])
      if (x.is_string()) m.arguments.push_back(x.get<std::string>());
  m.jetons = j.value("jetons", 512);
  m.delai = j.value("delai", 180.0);
  return m;
}

std::vector<std::string> commandeMoteur(const MoteurAssistant& m, const std::string& invite) {
  std::vector<std::string> a;
  if (m.genre == "llama.cpp") {
    a = {m.programme.empty() ? std::string("llama-cli") : m.programme, "-m", m.modele, "-n", std::to_string(m.jetons), "--temp", "0.2", "--no-display-prompt", "-no-cnv", "-p", invite};
  } else if (m.genre == "ollama") {
    a = {m.programme.empty() ? std::string("ollama") : m.programme, "run", m.modele};
  } else {
    a = {m.programme};
  }
  a.insert(a.end(), m.arguments.begin(), m.arguments.end());
  return a;
}

ReponseAssistant demander(const MoteurAssistant& m, const std::string& systeme, const std::string& question) {
  ReponseAssistant r;
  if (!m.configure()) {
    r.erreur = "Aucun moteur d’assistant n’est réglé : choisis un modèle local (llama.cpp, Ollama) ou un programme dans les réglages de l’assistant.";
    return r;
  }
  const std::string invite = (systeme.empty() ? std::string() : systeme + "\n\n") + question;
  r.commande = commandeMoteur(m, invite);
  OptionsProcessus o;
  o.delai = m.delai;
  if (m.genre != "llama.cpp") o.entree = invite + "\n";
  o.environnement["LC_ALL"] = "C.UTF-8";
  const ResultatProcessus p = executerProcessus(r.commande, o);
  if (!p.lance) {
    r.erreur = p.erreurs;
    return r;
  }
  std::string t = p.sortie;
  for (const char* fin : {"[end of text]", "<|im_end|>", "</s>"})
    if (const auto i = t.find(fin); i != std::string::npos) t.erase(i);
  r.texte = sansBlancs(t);
  r.ok = p.reussi() && !r.texte.empty();
  if (!r.ok) r.erreur = p.delaiDepasse ? std::string("L’assistant n’a pas répondu dans le délai.") : sansBlancs(p.erreurs).substr(0, 400);
  return r;
}

std::string MemoireAssistant::texte() const { return J{{"regles", regles}, {"suspendue", suspendue}}.dump(1); }

void MemoireAssistant::lire(const std::string& t) {
  const J j = J::parse(t, nullptr, false);
  if (!j.is_object()) return;
  regles.clear();
  if (j.contains("regles") && j["regles"].is_array())
    for (const auto& x : j["regles"])
      if (x.is_string()) regles.push_back(x.get<std::string>());
  suspendue = j.value("suspendue", false);
}

std::string systemeAssistant() {
  return "Tu es l’assistant de Prométhée, un logiciel de conception électronique. Réponds en français, en phrases simples et courtes. "
         "N’invente aucune valeur : dis ce que tu tires du schéma, et ce qui reste à vérifier sur la fiche technique. "
         "Une correction se propose comme une commande que l’utilisateur acceptera ou refusera.";
}

std::string contexteBloc(const ConceptionElec& c, const std::set<std::string>& composants) {
  const Connectivite k = connectivite(c);
  std::ostringstream o;
  std::map<std::string, std::set<std::string>> dedans, dehors;  // réseau → repères du bloc / hors bloc
  for (const auto& r : k.reseaux)
    for (const auto& b : r.broches) {
      const ComposantElec* x = c.composant(b.composant);
      if (!x) continue;
      (composants.count(x->uuid) ? dedans : dehors)[r.nom].insert(x->reference);
    }
  for (const auto& x : c.composants) {
    if (!composants.count(x.uuid)) continue;
    o << "- " << x.reference << " : " << (x.valeur.empty() ? x.symbole : x.valeur);
    if (!x.mpn.empty()) o << " (référence " << x.mpn << (x.fabricant.empty() ? std::string() : ", " + x.fabricant) << ")";
    if (const DefSymbole* d = c.symbole(x.symbole)) {
      o << " ; broches :";
      std::set<std::string> vus;
      for (const auto& b : d->broches)
        if (vus.insert(b.numero).second) {
          const std::string n = k.nom(x.uuid, b.numero);
          o << " " << b.numero << (b.nom.empty() || b.nom == b.numero || b.nom.rfind("Pin_", 0) == 0 ? std::string() : " " + b.nom) << " → " << (n.empty() ? std::string("libre") : n) << ",";
        }
    }
    o << "\n";
  }
  std::string relies;
  for (const auto& [r, refs] : dedans)
    if (dehors.count(r)) relies += (relies.empty() ? "" : ", ") + r;
  if (!relies.empty()) o << "Réseaux qui relient le bloc au reste du schéma : " << relies << "\n";
  return o.str();
}

std::string inviteExpliquerBloc(const ConceptionElec& c, const std::set<std::string>& composants, const MemoireAssistant& m) {
  std::string t = "Explique à quoi sert ce bloc du schéma de « " + c.nom + " » et comment il fonctionne, puis signale ce qui manque ou paraît faux.\n";
  if (!m.suspendue && !m.regles.empty()) {
    t += "Règles maison à respecter :\n";
    for (const auto& r : m.regles) t += "- " + r + "\n";
  }
  return t + "\nBloc :\n" + contexteBloc(c, composants);
}

std::string inviteViolation(const ConceptionElec& c, const Violation& v, const MemoireAssistant& m) {
  std::string t = "Une vérification de « " + c.nom + " » signale : [" + v.code + "] " + v.message + (v.conseil.empty() ? std::string() : " (conseil : " + v.conseil + ")") +
                  ".\nExplique la cause probable et propose une correction.\n";
  if (!m.suspendue && !m.regles.empty()) {
    t += "Règles maison :\n";
    for (const auto& r : m.regles) t += "- " + r + "\n";
  }
  std::set<std::string> objets(v.objets.begin(), v.objets.end());
  if (!objets.empty()) t += "\nObjets concernés :\n" + contexteBloc(c, objets);
  return t;
}

// ---------- Fiches techniques ----------
namespace {
// Pages d'un PDF à flux en clair (celui de Prométhée) : contenus des objets /Page dans l'ordre de /Kids
std::vector<std::string> pagesSimples(const std::string& pdf) {
  std::map<int, std::string> objets;
  static const std::regex debut(R"((\d+)\s+0\s+obj\b)");
  std::vector<std::pair<int, std::size_t>> positions;
  for (auto it = std::sregex_iterator(pdf.begin(), pdf.end(), debut); it != std::sregex_iterator(); ++it) positions.emplace_back(std::stoi((*it)[1].str()), static_cast<std::size_t>(it->position()));
  for (std::size_t i = 0; i < positions.size(); ++i) {
    const std::size_t a = positions[i].second, b = i + 1 < positions.size() ? positions[i + 1].second : pdf.size();
    objets[positions[i].first] = pdf.substr(a, b - a);
  }
  std::vector<int> ordre;
  for (const auto& [n, corps] : objets)
    if (corps.find("/Type /Pages") != std::string::npos) {
      static const std::regex ref(R"((\d+)\s+0\s+R)");
      const std::size_t k = corps.find("/Kids");
      if (k == std::string::npos) continue;
      const std::string kids = corps.substr(k, corps.find(']', k) - k);
      for (auto it = std::sregex_iterator(kids.begin(), kids.end(), ref); it != std::sregex_iterator(); ++it) ordre.push_back(std::stoi((*it)[1].str()));
      break;
    }
  std::vector<std::string> pages;
  for (const int n : ordre) {
    const std::string& p = objets[n];
    static const std::regex contenu(R"(/Contents\s+(\d+)\s+0\s+R)");
    std::smatch m;
    if (!std::regex_search(p, m, contenu)) {
      pages.emplace_back();
      continue;
    }
    pages.push_back(textePdfSimple(objets[std::stoi(m[1].str())]));
  }
  return pages;
}
// Passages d'une page : paragraphes (lignes vides), ou fenêtres de trois lignes pour un texte sans paragraphes
std::vector<std::string> passages(const std::string& page) {
  std::vector<std::string> lignes, v;
  std::istringstream in(page);
  for (std::string l; std::getline(in, l);) lignes.push_back(sansBlancs(l));
  std::string courant;
  for (const auto& l : lignes) {
    if (l.empty()) {
      if (!courant.empty()) v.push_back(courant), courant.clear();
      continue;
    }
    courant += (courant.empty() ? "" : " ") + l;
    if (courant.size() > 400) v.push_back(courant), courant.clear();
  }
  if (!courant.empty()) v.push_back(courant);
  // Lignes isolées (tableaux) : chaque ligne aussi, pour qu'une caractéristique ne se noie pas dans son tableau
  for (const auto& l : lignes)
    if (l.size() > 8 && std::find(v.begin(), v.end(), l) == v.end()) v.push_back(l);
  return v;
}
// Mots de la question en français, et leurs équivalents dans une fiche en anglais
std::vector<std::vector<std::string>> motsQuestion(const std::string& question) {
  static const std::map<std::string, std::vector<std::string>> equivalents = {
      {"courant", {"current", "courant"}},
      {"sortie", {"output", "sortie", "out"}},
      {"entree", {"input", "entree", "in"}},
      {"tension", {"voltage", "tension"}},
      {"maximal", {"maximum", "max", "excess", "greater", "more", "up", "maximal"}},
      {"maximale", {"maximum", "max", "excess", "greater", "more", "up"}},
      {"maximum", {"maximum", "max", "excess", "greater", "more", "up"}},
      {"max", {"maximum", "max", "excess", "greater", "more"}},
      {"minimal", {"minimum", "min", "minimal"}},
      {"minimale", {"minimum", "min"}},
      {"minimum", {"minimum", "min"}},
      {"frequence", {"frequency", "frequence"}},
      {"temperature", {"temperature", "junction", "operating"}},
      {"puissance", {"power", "dissipation"}},
      {"thermique", {"thermal"}},
      {"resistance", {"resistance"}},
      {"reference", {"reference"}},
      {"regulation", {"regulation"}},
      {"ondulation", {"ripple"}},
      {"rejet", {"rejection"}},
      {"bruit", {"noise"}},
      {"boitier", {"package"}},
      {"repos", {"quiescent", "adjustment", "standby"}},
      {"consommation", {"quiescent", "supply", "consumption"}},
      {"limite", {"limit", "limiting"}},
      {"chute", {"dropout"}},
      {"plage", {"range"}},
      {"reglable", {"adjustable"}},
  };
  static const std::set<std::string> vides = {"de", "du", "la", "le", "les", "des", "est", "quel", "quelle", "quels", "quelles", "a", "au", "en", "et", "un", "une", "sur", "pour", "ce", "cette"};
  std::vector<std::vector<std::string>> v;
  for (const auto& m : jetons(question)) {
    if (vides.count(m)) continue;
    const auto it = equivalents.find(m);
    v.push_back(it == equivalents.end() ? std::vector<std::string>{m} : it->second);
  }
  return v;
}
}  // namespace

std::optional<FicheTechnique> lireFicheTechnique(const std::string& pdf, const std::string& nom, std::string* erreur) {
  FicheTechnique f;
  f.nom = nom;
  if (pdf.rfind("%PDF", 0) != 0) {
    if (erreur) *erreur = "Ce n’est pas un PDF.";
    return std::nullopt;
  }
  std::vector<std::string> pages;
  if (!programmeDuPoste("pdftotext").empty()) {
    const std::string d = dossierTemporaire();
    const fs::path chemin = cheminFichier(d) / "fiche.pdf";
    try {
      ecrireFichierAtomique(chemin, pdf);
      const ResultatProcessus r = executerProcessus({"pdftotext", "-layout", "-enc", "UTF-8", utf8(chemin), "-"});
      if (r.reussi()) {
        std::size_t debut = 0;
        while (debut < r.sortie.size()) {
          const std::size_t fin = r.sortie.find('\f', debut);
          pages.push_back(r.sortie.substr(debut, fin == std::string::npos ? std::string::npos : fin - debut));
          if (fin == std::string::npos) break;
          debut = fin + 1;
        }
        while (!pages.empty() && sansBlancs(pages.back()).empty()) pages.pop_back();
      }
    } catch (...) {
    }
    std::error_code ec;
    fs::remove_all(cheminFichier(d), ec);
  }
  if (pages.empty()) pages = pagesSimples(pdf);
  for (std::size_t i = 0; i < pages.size(); ++i) f.pages.push_back({static_cast<int>(i + 1), pages[i]});
  if (f.pages.empty() || std::all_of(f.pages.begin(), f.pages.end(), [](const PageFiche& p) { return sansBlancs(p.texte).empty(); })) {
    if (erreur) *erreur = "Aucun texte lisible dans ce PDF (image scannée ?) : installe poppler (pdftotext) ou joins un PDF texte.";
    return std::nullopt;
  }
  return f;
}

std::vector<ExtraitFiche> chercherDansFiche(const FicheTechnique& f, const std::string& question, std::size_t n) {
  struct Passage {
    int page;
    std::string texte;
    std::vector<std::string> mots;
  };
  std::vector<Passage> tout;
  for (const auto& p : f.pages)
    for (const auto& x : passages(p.texte)) tout.push_back({p.numero, x, jetons(x)});
  if (tout.empty()) return {};
  double longueurMoyenne = 0;
  for (const auto& p : tout) longueurMoyenne += static_cast<double>(p.mots.size());
  longueurMoyenne /= static_cast<double>(tout.size());
  const auto q = motsQuestion(question);
  // BM25 : chaque mot de la question vaut par le meilleur de ses équivalents ; bonus aux mots voisins dans l'ordre
  std::vector<ExtraitFiche> v;
  for (const auto& p : tout) {
    double score = 0;
    for (const auto& groupe : q) {
      double meilleur = 0;
      for (const auto& m : groupe) {
        int df = 0;
        for (const auto& x : tout) df += std::find(x.mots.begin(), x.mots.end(), m) != x.mots.end() ? 1 : 0;
        const int tf = static_cast<int>(std::count(p.mots.begin(), p.mots.end(), m));
        if (!tf) continue;
        const double idf = std::log(1 + (static_cast<double>(tout.size()) - df + 0.5) / (df + 0.5));
        const double k1 = 1.2, b = 0.75;
        meilleur = std::max(meilleur, idf * tf * (k1 + 1) / (tf + k1 * (1 - b + b * static_cast<double>(p.mots.size()) / longueurMoyenne)));
      }
      score += meilleur;
    }
    // Deux mots consécutifs de la question (« output current ») qui se suivent dans le passage
    for (std::size_t i = 0; i + 1 < q.size(); ++i)
      for (std::size_t k = 0; k + 1 < p.mots.size(); ++k)
        if ((std::find(q[i].begin(), q[i].end(), p.mots[k]) != q[i].end() && std::find(q[i + 1].begin(), q[i + 1].end(), p.mots[k + 1]) != q[i + 1].end()) ||
            (std::find(q[i + 1].begin(), q[i + 1].end(), p.mots[k]) != q[i + 1].end() && std::find(q[i].begin(), q[i].end(), p.mots[k + 1]) != q[i].end()))
          score += 1.5;
    if (score > 0) v.push_back({p.page, p.texte, score});
  }
  std::stable_sort(v.begin(), v.end(), [](const ExtraitFiche& a, const ExtraitFiche& b) { return a.score > b.score; });
  if (v.size() > n) v.resize(n);
  return v;
}

ReponseFiche repondreSurFiche(const FicheTechnique& f, const std::string& question, const MoteurAssistant* moteur) {
  ReponseFiche r;
  const auto extraits = chercherDansFiche(f, question, 3);
  if (extraits.empty()) {
    r.texte = "La fiche « " + f.nom + " » ne dit rien de cela.";
    return r;
  }
  const std::string q = normal(question);
  // Unités attendues d'après la question
  std::vector<std::string> unites;
  if (q.find("courant") != std::string::npos) unites = {"A", "mA", "uA", "µA"};
  else if (q.find("tension") != std::string::npos) unites = {"V", "mV"};
  else if (q.find("frequence") != std::string::npos) unites = {"Hz", "kHz", "MHz"};
  else if (q.find("temperature") != std::string::npos) unites = {"°C", "C"};
  else if (q.find("puissance") != std::string::npos) unites = {"W", "mW"};
  else if (q.find("resistance") != std::string::npos) unites = {"Ω", "ohm", "°C/W"};
  static const std::regex valeur(R"(([0-9]+(?:[.,][0-9]+)?)\s*(µA|uA|mA|A|mV|V|kHz|MHz|Hz|°C/W|°C|mW|W|Ω|ohm)\b)");
  for (const auto& e : extraits) {
    for (auto it = std::sregex_iterator(e.passage.begin(), e.passage.end(), valeur); it != std::sregex_iterator(); ++it) {
      const std::string u = (*it)[2].str();
      if (!unites.empty() && std::find(unites.begin(), unites.end(), u) == unites.end()) continue;
      std::string v = (*it)[1].str();
      std::replace(v.begin(), v.end(), '.', ',');
      r.trouvee = true;
      r.valeur = v;
      r.unite = u;
      std::string point = v;
      std::replace(point.begin(), point.end(), ',', '.');
      r.nombre = std::atof(point.c_str());
      r.extrait = e;
      break;
    }
    if (r.trouvee) break;
  }
  if (!r.trouvee) r.extrait = extraits.front();
  std::string question0 = sansBlancs(question);
  while (!question0.empty() && (question0.back() == '?' || question0.back() == ' ')) question0.pop_back();
  if (!question0.empty()) question0[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(question0[0])));
  r.texte = r.trouvee ? question0 + " : " + r.valeur + " " + r.unite + " — page " + std::to_string(r.extrait.page) + " de « " + f.nom + " » : « " + r.extrait.passage + " »"
                      : "Passage le plus proche, page " + std::to_string(r.extrait.page) + " : « " + r.extrait.passage + " »";
  if (moteur && moteur->configure()) {
    std::string invite = "Réponds à la question à partir de ces seuls extraits de la fiche « " + f.nom + " », en citant la page ; si les extraits ne le disent pas, réponds que la fiche ne le dit pas.\n";
    for (const auto& e : extraits) invite += "[page " + std::to_string(e.page) + "] " + e.passage + "\n";
    invite += "\nQuestion : " + question;
    const ReponseAssistant a = demander(*moteur, systemeAssistant(), invite);
    if (a.ok) r.texte = a.texte + "\n(" + r.texte + ")";
  }
  return r;
}

// ---------- Simulation confiée ----------
namespace {
const ComposantElec* sourceTension(const ConceptionElec& c) {
  for (const auto& x : c.composants)
    if (x.symbole == "Source_tension" || x.symbole == "VSOURCE" || (!x.reference.empty() && x.reference[0] == 'V' && x.symbole.find("Source") != std::string::npos)) return &x;
  return nullptr;
}
}  // namespace

ReponseSimulation repondreSimulation(const ConceptionElec& c0, const std::string& question) {
  ReponseSimulation r;
  const std::string q = normal(question);
  if (!ngspiceDisponible()) {
    r.erreur = "ngspice n’est pas installé sur ce poste : je ne peux pas lancer l’analyse.";
    return r;
  }
  ConceptionElec c = c0;
  const Connectivite k = connectivite(c);
  std::set<std::string> reseaux;
  for (const auto& n : k.reseaux)
    if (!n.nom.empty()) reseaux.insert(n.nom);
  // Réseau visé : nommé dans la question, sinon une sortie nommée, sinon le nœud du condensateur de filtrage
  std::string sortie;
  for (const auto& mot : jetons(question))
    for (const auto& n : reseaux)
      if (normal(n) == mot && normal(n) != "gnd") sortie = n;
  for (const char* nom : {"SORTIE", "OUT", "VOUT", "OUTPUT", "S"})
    if (sortie.empty() && reseaux.count(nom)) sortie = nom;
  const ComposantElec* src = sourceTension(c);
  const std::string entree = src ? k.nom(src->uuid, "1") : std::string();
  if (sortie.empty())
    for (const auto& x : c.composants) {
      if (x.symbole != "C" || !sortie.empty()) continue;
      const std::string a = k.nom(x.uuid, "1"), b = k.nom(x.uuid, "2");
      const bool masseA = normal(a) == "gnd" || a == "0", masseB = normal(b) == "gnd" || b == "0";
      if (masseA != masseB) {
        const std::string n = masseA ? b : a;
        if (n != entree) sortie = n;
      }
    }
  if (sortie.empty()) {
    r.erreur = "Je ne trouve pas la sortie du montage : nomme son réseau dans la question (« … sur SORTIE ? »).";
    return r;
  }
  r.reseau = sortie;
  const bool coupure = q.find("coupure") != std::string::npos || q.find("bande passante") != std::string::npos || q.find("-3 db") != std::string::npos || q.find("3 db") != std::string::npos;
  const bool frequenceOsc = !coupure && (q.find("frequence") != std::string::npos || q.find("periode") != std::string::npos);
  const bool montee = q.find("montee") != std::string::npos;
  AnalyseSpice a;
  MesureAuto m;
  m.courbe = sortie;
  if (coupure || (!frequenceOsc && !montee)) {
    a.type = AnalyseSpice::Type::Alternatif;
    a.fDebut = 1, a.fFin = 100e6, a.pointsParDecade = 100;
    m.genre = GenreMesure::Coupure;
    // La source reçoit une amplitude alternative de 1 V si elle n'en a pas
    if (src && normal(src->valeur).find("ac") == std::string::npos)
      for (auto& x : c.composants)
        if (x.uuid == src->uuid) {
          // Forme « DC v AC 1 » : un nombre seul serait lu comme une tension continue, sans amplitude alternative
          const std::string v = sansBlancs(x.valeur);
          if (v.empty()) x.valeur = "DC 0 AC 1";
          else if (std::isdigit(static_cast<unsigned char>(v[0])) || v[0] == '-' || v[0] == '.') x.valeur = "DC " + v + " AC 1";
          else if (normal(v).rfind("dc", 0) == 0) x.valeur = v + " AC 1";
          else x.valeur = "DC 0 AC 1 " + v;
          r.journal.push_back("amplitude alternative de 1 V donnée à " + x.reference + " pour l’analyse");
        }
    r.analyse = "analyse alternative de 1 Hz à 100 MHz, 100 points par décade";
  } else {
    a.type = AnalyseSpice::Type::Transitoire;
    a.pas = 1e-6, a.duree = 20e-3;
    m.genre = frequenceOsc ? GenreMesure::Frequence : GenreMesure::Montee;
    r.analyse = "analyse transitoire de 20 ms au pas de 1 µs";
  }
  OptionsSimulation o;
  NetlistSpice n = netlistSpice(c, a, o);
  if (!n.valide()) {
    r.erreur = "La netlist ne se fait pas : " + (n.erreurs.empty() ? std::string("?") : n.erreurs.front());
    return r;
  }
  ResultatSimulation s = simuler(n);
  if (!s.reussi()) {
    // Remèdes de convergence de 11.9, essayés seuls
    EssaiRemedes e = simulerAvecRemedes(c, a, o);
    for (const auto& x : e.essayes) r.journal.push_back("remède essayé : " + libelleRemede(x));
    if (!e.resultat.reussi()) {
      r.erreur = "ngspice n’aboutit pas : " + analyserEchec(s, n).cause;
      return r;
    }
    s = e.resultat;
    n = e.netlist;
    r.journal.push_back("calcul abouti avec « " + libelleRemede(e.retenu) + " »");
  }
  const ResultatMesure x = mesurer(m, s, n);
  if (!x.valide) {
    r.erreur = "La mesure demandée ne se fait pas sur « " + sortie + " ».";
    return r;
  }
  r.valeur = x.valeur;
  r.unite = x.unite;
  // Courbe jointe
  const std::string expr = a.type == AnalyseSpice::Type::Alternatif ? (entree.empty() || entree == sortie ? "db(V(" + sortie + "))" : "db(V(" + sortie + ")/V(" + entree + "))") : "V(" + sortie + ")";
  const CourbeCalculee k2 = evaluerCourbe(expr, s, n);
  if (k2.valide()) {
    r.x = k2.x;
    r.y = k2.complexe ? k2.modules() : k2.reels();
    r.axeX = a.type == AnalyseSpice::Type::Alternatif ? "Fréquence (Hz)" : "Temps (s)";
    r.axeY = expr;
  }
  const std::string grandeur = m.genre == GenreMesure::Coupure ? "Fréquence de coupure à −3 dB" : m.genre == GenreMesure::Frequence ? "Fréquence" : "Temps de montée";
  std::string v;
  if (r.unite == "Hz" && r.valeur >= 1e3) v = virgule(r.valeur / 1e3, 4) + " kHz";
  else if (r.unite == "s" && r.valeur < 1e-3) v = virgule(r.valeur * 1e6, 4) + " µs";
  else v = virgule(r.valeur, 4) + " " + r.unite;
  r.texte = grandeur + " sur « " + sortie + " » : " + v + " (" + r.analyse + " par ngspice). La courbe " + expr + " est jointe.";
  r.ok = true;
  return r;
}

}  // namespace prom

// Prométhée : dépôt « Vers le PLM » et retour « Depuis le PLM » (lot L19 de la 0.34 ; 19.14). Voir plm.hpp.
// Licence GPL-3.0-only.
#include "promethee/plm.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "promethee/cablage.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/hachage.hpp"
#include "promethee/modele.hpp"
#include "promethee/projet_partage.hpp"

namespace prom {

using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;

namespace {
std::string xml(const std::string& s) {
  std::string r;
  for (const char c : s) r += c == '<' ? std::string("&lt;") : c == '>' ? std::string("&gt;") : c == '&' ? std::string("&amp;") : c == '"' ? std::string("&quot;") : std::string(1, c);
  return r;
}
std::string sansEspaces(const std::string& s) {
  std::string r;
  for (const unsigned char c : s) r += std::isalnum(c) || c >= 0x80 ? static_cast<char>(c) : '-';
  while (r.find("--") != std::string::npos) r.erase(r.find("--"), 1);
  while (!r.empty() && r.back() == '-') r.pop_back();
  return r.empty() ? std::string("produit") : r;
}
std::string champCsv(std::string v) {
  std::replace(v.begin(), v.end(), ';', ',');
  std::replace(v.begin(), v.end(), '\n', ' ');
  return v;
}
std::string nombre(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.6g", v);
  std::string s = b;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
}  // namespace

const ArticlePlm* DepotPlm::article(const std::string& n) const {
  for (const auto& a : articles)
    if (a.numero == n) return &a;
  return nullptr;
}

DepotPlm preparerDepotPlm(const Projet& p, const std::string& revision, const std::string& date, const std::string& auteur, const std::string& prefixe) {
  DepotPlm d;
  d.produit = p.nom;
  d.numero = prefixe + "P-" + sansEspaces(p.nom);
  d.revision = revision.empty() ? "A" : revision;
  d.date = date;
  d.auteur = auteur;
  ArticlePlm produit;
  produit.numero = d.numero;
  produit.revision = d.revision;
  produit.designation = p.nom;
  produit.genre = "produit";
  d.articles.push_back(produit);
  std::map<std::string, std::size_t> index;  // numéro → place dans d.articles (articles communs à plusieurs parents)
  auto enfant = [&](std::size_t parent, ArticlePlm a, double quantite) {
    auto it = index.find(a.numero);
    if (it == index.end()) {
      index[a.numero] = d.articles.size();
      a.quantite = quantite;
      d.articles.push_back(a);
    }
    // Quantité portée par le lien parent → enfant : gardée dans la liste des enfants « numéro×quantité »
    d.articles[parent].enfants.push_back(a.numero + "×" + nombre(quantite));
  };
  // Cartes et leurs composants
  for (std::size_t i = 0; i < p.electronique.size(); ++i) {
    const ecad::ConceptionElec& c = p.electronique[i];
    ArticlePlm carte;
    const auto it = c.cartouche.champs.find("numero");
    carte.numero = it != c.cartouche.champs.end() && !it->second.empty() ? it->second : prefixe + "C" + std::to_string(i + 1);
    carte.revision = c.cartouche.revision.empty() ? d.revision : c.cartouche.revision;
    carte.designation = c.cartouche.titre.empty() ? c.nom : c.cartouche.titre;
    carte.genre = "carte";
    const std::size_t ic = d.articles.size();
    enfant(0, carte, 1);
    for (const auto& l : ecad::nomenclature(c)) {
      ArticlePlm a;
      a.genre = "composant";
      a.fabricant = l.fabricant;
      a.mpn = l.mpn;
      a.designation = l.valeur + (l.empreinte.empty() ? std::string() : " " + l.empreinte);
      // Numéro interne de l'article, sinon tiré de la référence fabricant
      std::string numero;
      for (const auto& ref : l.references)
        if (const ecad::ComposantElec* k = c.parReference(ref))
          if (const auto ja = c.articles.find(k->article); !k->article.empty() && ja != c.articles.end() && !ja->second.numeroInterne.empty()) numero = ja->second.numeroInterne;
      a.numero = !numero.empty() ? numero : prefixe + "E-" + sansEspaces(!l.mpn.empty() ? l.mpn : a.designation);
      enfant(ic, a, l.quantite);
    }
  }
  // Pièces mécaniques
  for (std::size_t i = 0; i < p.pieces.size(); ++i) {
    ArticlePlm a;
    a.genre = "mécanique";
    a.designation = p.pieces[i].value("nom", std::string("Pièce ") + std::to_string(i + 1));
    a.numero = p.pieces[i].value("numero", prefixe + "M" + std::to_string(i + 1));
    enfant(0, a, 1);
  }
  // Faisceaux des projets électriques, au mètre « selon besoin »
  for (const auto& pe : p.electrique)
    for (const auto& h : elec::cablages(pe)) {
      ArticlePlm f;
      f.genre = "faisceau";
      f.designation = h.nom;
      f.numero = prefixe + "F-" + sansEspaces(h.nom);
      const std::size_t ifa = d.articles.size();
      enfant(0, f, 1);
      for (const auto& l : elec::nomenclatureCablage(h, elec::bibliothequeCablageIntegree())) {
        ArticlePlm a;
        a.genre = l.unite == "m" ? (l.designation.find("câble") != std::string::npos || l.designation.find("Câble") != std::string::npos ? "câble" : "fil") : "contact";
        a.designation = l.designation;
        a.mpn = l.reference;
        a.unite = l.unite == "m" ? "m" : "pièce";
        a.numero = prefixe + "H-" + sansEspaces(l.reference);
        enfant(ifa, a, l.quantite);
      }
    }
  return d;
}

std::string csvNomenclatureCommune(const DepotPlm& d) {
  std::ostringstream o;
  o << "\xEF\xBB\xBFParent;Numéro;Révision;Désignation;Genre;Fabricant;Référence fabricant;Quantité;Unité\r\n";
  o << ";" << d.numero << ";" << d.revision << ";" << champCsv(d.produit) << ";produit;;;1;pièce\r\n";
  for (const auto& parent : d.articles)
    for (const auto& e : parent.enfants) {
      const std::size_t x = e.rfind("×");
      const std::string numero = x == std::string::npos ? e : e.substr(0, x), quantite = x == std::string::npos ? "1" : e.substr(x + std::string("×").size());
      const ArticlePlm* a = d.article(numero);
      if (!a) continue;
      o << parent.numero << ";" << a->numero << ";" << a->revision << ";" << champCsv(a->designation) << ";" << a->genre << ";" << champCsv(a->fabricant) << ";" << champCsv(a->mpn) << ";" << quantite << ";"
        << a->unite << "\r\n";
    }
  return o.str();
}

std::string plmxml(const DepotPlm& d) {
  std::ostringstream o;
  o << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<PLMXML xmlns=\"http://www.plmxml.org/Schemas/PLMXMLSchema\" schemaVersion=\"6\" date=\"" << xml(d.date) << "\" author=\""
    << xml(d.auteur.empty() ? std::string("Prométhée") : d.auteur) << "\">\n";
  std::map<std::string, std::string> idRevision;
  int n = 1;
  for (const auto& a : d.articles) {
    const std::string pid = "id" + std::to_string(n++), rid = "id" + std::to_string(n++);
    idRevision[a.numero] = rid;
    o << "  <Product id=\"" << pid << "\" productId=\"" << xml(a.numero) << "\" name=\"" << xml(a.designation) << "\" subType=\"" << xml(a.genre) << "\"/>\n";
    o << "  <ProductRevision id=\"" << rid << "\" masterRef=\"#" << pid << "\" revision=\"" << xml(a.revision) << "\" name=\"" << xml(a.designation) << "\">\n";
    if (!a.fabricant.empty() || !a.mpn.empty())
      o << "    <UserData><UserValue title=\"fabricant\" value=\"" << xml(a.fabricant) << "\"/><UserValue title=\"mpn\" value=\"" << xml(a.mpn) << "\"/></UserData>\n";
    o << "  </ProductRevision>\n";
  }
  // Vue du produit : occurrences du produit et de ses enfants, quantités en données d'occurrence
  o << "  <ProductView id=\"vue\" rootRefs=\"occ0\" primaryOccurrenceRef=\"occ0\">\n";
  struct Occurrence {
    std::string id, ref, quantite;
    std::vector<std::string> enfants;
  };
  std::vector<Occurrence> occurrences;
  std::function<std::string(const ArticlePlm&, const std::string&, int)> visiter = [&](const ArticlePlm& a, const std::string& quantite, int profondeur) -> std::string {
    const std::size_t i = occurrences.size();
    occurrences.push_back({"occ" + std::to_string(i), idRevision[a.numero], quantite, {}});
    if (profondeur < 16)  // garde contre une structure qui se citerait elle-même
      for (const auto& e : a.enfants) {
        const std::size_t x = e.rfind("×");
        const ArticlePlm* c = d.article(x == std::string::npos ? e : e.substr(0, x));
        if (!c) continue;
        const std::string id = visiter(*c, x == std::string::npos ? std::string("1") : e.substr(x + std::string("×").size()), profondeur + 1);
        occurrences[i].enfants.push_back(id);
      }
    return occurrences[i].id;
  };
  if (!d.articles.empty()) visiter(d.articles.front(), "1", 0);
  for (const auto& oc : occurrences) {
    std::string refs;
    for (const auto& e : oc.enfants) refs += (refs.empty() ? "" : " ") + e;
    o << "    <Occurrence id=\"" << oc.id << "\" instancedRef=\"#" << oc.ref << "\"" << (refs.empty() ? std::string() : " occurrenceRefs=\"" + refs + "\"") << "><UserData><UserValue title=\"quantite\" value=\""
      << xml(oc.quantite) << "\"/></UserData></Occurrence>\n";
  }
  o << "  </ProductView>\n</PLMXML>\n";
  return o.str();
}

std::string manifestePlm(const DepotPlm& d) {
  Json j = {{"format", "promethee-depot-plm"}, {"version", 1}, {"produit", d.produit}, {"numero", d.numero}, {"revision", d.revision}, {"date", d.date}, {"auteur", d.auteur}};
  Json& f = j["fichiers"] = Json::array();
  const std::string csv = csvNomenclatureCommune(d), xmlp = plmxml(d);
  f.push_back({{"nom", "nomenclature.csv"}, {"sha256", sha256Hex(csv)}, {"taille", csv.size()}});
  f.push_back({{"nom", "structure.plmxml"}, {"sha256", sha256Hex(xmlp)}, {"taille", xmlp.size()}});
  for (const auto& [nom, contenu] : d.fichiers) f.push_back({{"nom", nom}, {"sha256", sha256Hex(contenu)}, {"taille", contenu.size()}});
  Json& a = j["articles"] = Json::array();
  for (const auto& x : d.articles) a.push_back({{"numero", x.numero}, {"revision", x.revision}, {"genre", x.genre}, {"designation", x.designation}});
  return j.dump(1) + "\n";
}

std::string deposerPourPlm(const std::string& dossierEquipe, const DepotPlm& d, std::string* erreur) {
  try {
    const fs::path dossier = cheminFichier(dossierEquipe) / kDossierVersPlm / cheminFichier(d.numero + "-" + d.revision);
    fs::create_directories(dossier);
    ecrireFichierAtomique(dossier / "nomenclature.csv", csvNomenclatureCommune(d));
    ecrireFichierAtomique(dossier / "structure.plmxml", plmxml(d));
    for (const auto& [nom, contenu] : d.fichiers) {
      const fs::path f = dossier / cheminFichier(nom);
      fs::create_directories(f.parent_path());
      ecrireFichierAtomique(f, contenu);
    }
    ecrireFichierAtomique(dossier / "manifeste.json", manifestePlm(d));  // en dernier : le dépôt est complet
    return utf8(dossier);
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return {};
  }
}

LectureDepotPlm lireDepotPlm(const std::string& dossier) {
  LectureDepotPlm r;
  try {
    const fs::path d = cheminFichier(dossier);
    const Json j = Json::parse(lireFichier(d / "manifeste.json"));
    if (j.value("format", std::string()) != "promethee-depot-plm") throw std::runtime_error("manifeste inconnu");
    r.produit = j.value("produit", std::string());
    r.numero = j.value("numero", std::string());
    r.revision = j.value("revision", std::string());
    for (const auto& f : j.value("fichiers", Json::array())) {
      const std::string nom = f.value("nom", std::string());
      r.fichiers.push_back(nom);
      const fs::path chemin = d / cheminFichier(nom);
      if (!fs::exists(chemin)) {
        r.manquants.push_back(nom);
        continue;
      }
      if (sha256Hex(lireFichier(chemin)) != f.value("sha256", std::string())) r.sommesFausses.push_back(nom);
    }
    if (fs::exists(d / "structure.plmxml")) {
      const std::string x = lireFichier(d / "structure.plmxml");
      for (std::size_t i = x.find("<ProductRevision "); i != std::string::npos; i = x.find("<ProductRevision ", i + 1)) ++r.articles;
    }
    r.ok = r.manquants.empty() && r.sommesFausses.empty();
    if (!r.ok) r.erreur = "dépôt incomplet ou altéré";
  } catch (const std::exception& e) {
    r.erreur = e.what();
  }
  return r;
}

std::vector<EtatPlm> lireEtatsPlm(const std::string& dossierEquipe) {
  std::vector<EtatPlm> r;
  try {
    const Json j = Json::parse(lireFichier(cheminFichier(dossierEquipe) / kDossierDepuisPlm / "etats.json"));
    for (const auto& x : j.value("etats", Json::array()))
      r.push_back({x.value("numero", std::string()), x.value("revision", std::string()), x.value("etat", std::string()), x.value("reservePar", std::string()), x.value("date", std::string()),
                   x.value("message", std::string())});
  } catch (...) {
  }
  return r;
}

std::string texteEtatsPlm(const std::vector<EtatPlm>& v) {
  Json j = {{"format", "promethee-etats-plm"}, {"version", 1}, {"etats", Json::array()}};
  for (const auto& e : v)
    j["etats"].push_back({{"numero", e.numero}, {"revision", e.revision}, {"etat", e.etat}, {"reservePar", e.reservePar}, {"date", e.date}, {"message", e.message}});
  return j.dump(1) + "\n";
}

bool ecrireEtatsPlm(const std::string& dossierEquipe, const std::vector<EtatPlm>& v) {
  try {
    const fs::path d = cheminFichier(dossierEquipe) / kDossierDepuisPlm;
    fs::create_directories(d);
    ecrireFichierAtomique(d / "etats.json", texteEtatsPlm(v));
    return true;
  } catch (...) {
    return false;
  }
}

std::vector<TicketPlm> lireTickets(const std::string& dossierEquipe, bool depuis) {
  std::vector<TicketPlm> r;
  try {
    const Json j = Json::parse(lireFichier(cheminFichier(dossierEquipe) / (depuis ? kDossierDepuisPlm : kDossierVersPlm) / "tickets.json"));
    for (const auto& x : j.value("tickets", Json::array()))
      r.push_back({x.value("id", std::string()), x.value("distant", std::string()), x.value("titre", std::string()), x.value("texte", std::string()), x.value("etat", std::string("ouvert")),
                   x.value("objet", std::string()), x.value("auteur", std::string()), x.value("date", std::string())});
  } catch (...) {
  }
  return r;
}

bool ecrireTickets(const std::string& dossierEquipe, bool depuis, const std::vector<TicketPlm>& v) {
  try {
    const fs::path d = cheminFichier(dossierEquipe) / (depuis ? kDossierDepuisPlm : kDossierVersPlm);
    fs::create_directories(d);
    Json j = {{"format", "promethee-tickets"}, {"version", 1}, {"tickets", Json::array()}};
    for (const auto& t : v)
      j["tickets"].push_back({{"id", t.id}, {"distant", t.distant}, {"titre", t.titre}, {"texte", t.texte}, {"etat", t.etat}, {"objet", t.objet}, {"auteur", t.auteur}, {"date", t.date}});
    ecrireFichierAtomique(d / "tickets.json", j.dump(1) + "\n");
    return true;
  } catch (...) {
    return false;
  }
}

std::vector<TicketPlm> ticketsDepuisRevues(const ecad::ConceptionElec& c, const std::vector<TicketPlm>& connus) {
  std::vector<TicketPlm> r;
  for (const auto& rv : revuesDe(c))
    for (const auto& k : rv.constats) {
      if (k.etat != "ouvert") continue;
      TicketPlm t;
      t.id = "revue-" + std::to_string(rv.numero) + "-" + k.id;
      t.titre = (k.objet.empty() ? std::string() : k.objet + " : ") + k.texte.substr(0, 80);
      t.texte = k.texte + " (revue " + std::to_string(rv.numero) + " « " + rv.titre + " », révision " + rv.revision + ")";
      t.objet = k.objet;
      t.auteur = k.auteur;
      t.date = k.date;
      for (const auto& x : connus)
        if (x.id == t.id) t.distant = x.distant;
      r.push_back(t);
    }
  return r;
}

int appliquerTickets(ecad::ConceptionElec& c, const std::vector<TicketPlm>& depuis, const std::string& par, const std::string& date) {
  int n = 0;
  for (const auto& t : depuis) {
    if (t.etat != "clos" || t.id.rfind("revue-", 0) != 0) continue;
    const std::size_t a = t.id.find('-'), b = t.id.find('-', a + 1);
    if (b == std::string::npos) continue;
    const int revue = std::atoi(t.id.substr(a + 1, b - a - 1).c_str());
    const std::string constat = t.id.substr(b + 1);
    bool ouvert = false;
    for (const auto& rv : revuesDe(c))
      if (rv.numero == revue)
        for (const auto& k : rv.constats) ouvert = ouvert || (k.id == constat && k.etat == "ouvert");
    if (ouvert && clore(c, revue, constat, "clos dans le gestionnaire de tickets" + (t.distant.empty() ? std::string() : " (" + t.distant + ")"), par, date)) ++n;
  }
  return n;
}

}  // namespace prom

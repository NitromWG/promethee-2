// Prométhée : travail à plusieurs, suite (lot L12 de la 0.34). Voir projet_partage.hpp. Licence GPL-3.0-only.
#include "promethee/projet_partage.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <random>
#include <sstream>

#include "promethee/archive.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/articles.hpp"
#include "promethee/collaboration.hpp"
#include "promethee/compression.hpp"
#include "promethee/csv.hpp"
#include "promethee/fabrication_avancee.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/hachage.hpp"
#include "promethee/pdf_documents.hpp"
#include "promethee/reprise_existant.hpp"
#include "promethee/version.hpp"

namespace fs = std::filesystem;
using J = nlohmann::ordered_json;

namespace prom {

using namespace ecad;

namespace {
std::string nombre(double v, int decimales = 2) {
  std::ostringstream o;
  o.setf(std::ios::fixed);
  o.precision(decimales);
  o << v;
  std::string s = o.str();
  if (s.find('.') != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
bool avantNumerique(const std::string& a, const std::string& b) {
  const int x = std::atoi(a.c_str()), y = std::atoi(b.c_str());
  return x != y ? x < y : a < b;
}
const DefSymbole* definitionElectrique(const elec::ProjetElectrique& pe, const std::string& id) {
  if (const auto it = pe.schema.symboles.find(id); it != pe.schema.symboles.end()) return &it->second;
  return elec::symboleElectrique(id);
}
// Réseau nommé par l'utilisateur (les noms automatiques « N-… » ne disent rien dans une armoire)
bool reseauNomme(const std::string& r) { return !r.empty() && r.rfind("N-", 0) != 0 && r.rfind("Net-", 0) != 0; }
}  // namespace

std::string referenceComparable(const std::string& mpn) {
  std::string r;
  for (const char c : mpn)
    if (!std::isspace(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.' && c != '/') r += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return r;
}

// ======================================================================================================================
// 12.6 · La carte dans le projet électrique
// ======================================================================================================================
CarteAppareil poserCarteDansFolio(elec::ProjetElectrique& pe, const ConceptionElec& carte, const std::string& connecteur, std::size_t folio, Pt position,
                                  const std::map<std::string, std::string>& renvois) {
  CarteAppareil r;
  const ComposantElec* j = carte.parReference(connecteur);
  if (!j) {
    r.erreur = "La carte « " + carte.nom + " » n’a pas de connecteur « " + connecteur + " ».";
    return r;
  }
  const DefSymbole* d = carte.symbole(j->symbole);
  if (!d || d->broches.empty()) {
    r.erreur = "Le symbole du connecteur « " + connecteur + " » n’a pas de broches.";
    return r;
  }
  if (folio >= pe.schema.feuilles.size()) {
    r.erreur = "Folio introuvable.";
    return r;
  }
  std::vector<std::string> nums;
  for (const auto& b : d->broches) nums.push_back(b.numero);
  std::sort(nums.begin(), nums.end(), avantNumerique);
  nums.erase(std::unique(nums.begin(), nums.end()), nums.end());
  elec::SpecAppareil a;
  a.nom = "Carte " + carte.nom + " " + connecteur;
  a.prefixe = "A";
  a.marquage = carte.nom;
  a.description = "Carte électronique « " + carte.nom + " », bornier " + connecteur + " (" + std::to_string(nums.size()) + " bornes)";
  a.fabricant = carte.cartouche.societe;
  a.reference = carte.nom + (carte.cartouche.revision.empty() ? std::string() : " rév. " + carte.cartouche.revision);
  a.bas = nums;
  a.largeur = std::max(10.0, mm(carte.circuit.contour.largeur));
  a.hauteur = std::max(10.0, mm(carte.circuit.contour.hauteur));
  a.profondeur = 25;
  a.montage = elec::MontageAppareil::Plaque;
  if (const auto defauts = elec::defautsAppareil(a); !defauts.empty()) {
    r.erreur = defauts.front();
    return r;
  }
  r.symbole = elec::enregistrerAppareil(pe, a);
  const std::string pose = elec::poserAppareil(pe, folio, r.symbole, position);
  const DefSymbole* ds = definitionElectrique(pe, r.symbole);
  const SymbolePlace* sp = nullptr;
  for (const auto& x : pe.schema.feuilles[folio].symboles)
    if (x.uuid == pose) sp = &x;
  if (!sp || !ds) {
    r.erreur = "La carte n’a pas pu être posée sur le folio.";
    return r;
  }
  r.composant = sp->composant;
  const SymbolePlace place = *sp;  // copie : les fils s'ajoutent à la feuille
  const Connectivite k = connectivite(carte);
  std::map<std::string, std::string> potentielDe;
  for (const auto& b : brochesPlacees(place, *ds)) {
    const std::string reseau = k.nom(j->uuid, b.broche->numero);
    std::string pot = reseauNomme(reseau) ? reseau : std::string();
    if (const auto it = renvois.find(reseau); it != renvois.end()) pot = it->second;
    potentielDe[b.broche->numero] = pot;
    if (pot.empty()) continue;
    // Fil de 10 mm vers l'extérieur de la borne, puis le potentiel au bout : le câblage suit le nom
    const Pt dir = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
    const Pt bout{b.p.x - dir.x * 10'000'000, b.p.y - dir.y * 10'000'000};
    pe.schema.feuilles[folio].fils.push_back({nouvelUuid(), b.p, bout});
    elec::poserPotentiel(pe, folio, pot, bout, b.direction == 1 || b.direction == 3 ? 1 : 0);
  }
  for (const auto& n : nums) {
    r.bornes.push_back(n);
    r.potentiels.push_back(potentielDe.count(n) ? potentielDe[n] : std::string());
  }
  if (ComposantElec* x = pe.schema.composant(r.composant)) {
    x->champs["carte"] = carte.uuid;
    x->champs["connecteur"] = connecteur;
  }
  return r;
}

bool implanterCarte(elec::ProjetElectrique& pe, std::size_t armoire, const std::string& composant, const ConceptionElec& carte, Pt position) {
  if (armoire >= pe.armoires.size() || !pe.schema.composant(composant)) return false;
  elec::Armoire& a = pe.armoires[armoire];
  a.implantations.erase(std::remove_if(a.implantations.begin(), a.implantations.end(), [&](const elec::Implantation& i) { return i.composant == composant; }), a.implantations.end());
  elec::Implantation i;
  i.composant = composant;
  i.p = position;
  i.rail = -1;  // fixée sur la plaque par ses entretoises
  a.implantations.push_back(i);
  (void)carte;
  return true;
}

// ======================================================================================================================
// 12.11 · Matériel libre
// ======================================================================================================================
const std::vector<LicenceLibre>& licencesLibres() {
  static const std::vector<LicenceLibre> v = {
      {"CERN-OHL-S-2.0", "CERN Open Hardware Licence Version 2 — Strongly Reciprocal", "https://ohwr.org/cern_ohl_s_v2.txt", true},
      {"CERN-OHL-W-2.0", "CERN Open Hardware Licence Version 2 — Weakly Reciprocal", "https://ohwr.org/cern_ohl_w_v2.txt", true},
      {"CERN-OHL-P-2.0", "CERN Open Hardware Licence Version 2 — Permissive", "https://ohwr.org/cern_ohl_p_v2.txt", false},
      {"TAPR-OHL-1.0", "TAPR Open Hardware License, version 1.0", "https://tapr.org/the-tapr-open-hardware-license/", true},
      {"CC-BY-SA-4.0", "Creative Commons Attribution-ShareAlike 4.0 International", "https://creativecommons.org/licenses/by-sa/4.0/legalcode", true},
      {"CC-BY-4.0", "Creative Commons Attribution 4.0 International", "https://creativecommons.org/licenses/by/4.0/legalcode", false},
  };
  return v;
}

const LicenceLibre* licenceLibre(const std::string& spdx) {
  for (const auto& l : licencesLibres())
    if (l.spdx == spdx) return &l;
  return nullptr;
}

std::string avisLicence(const std::string& spdx, const std::string& projet, const std::string& detenteur, const std::string& annee) {
  const LicenceLibre* l = licenceLibre(spdx);
  if (!l) return {};
  std::string t = "SPDX-License-Identifier: " + l->spdx + "\n\n";
  t += "Copyright " + (annee.empty() ? std::string() : annee + " ") + (detenteur.empty() ? std::string("les auteurs du projet") : detenteur) + "\n\n";
  if (spdx.rfind("CERN-OHL-", 0) == 0) {
    const std::string nom = spdx == "CERN-OHL-S-2.0" ? "CERN-OHL-S v2" : spdx == "CERN-OHL-W-2.0" ? "CERN-OHL-W v2" : "CERN-OHL-P v2";
    t += "This source describes Open Hardware and is licensed under the " + nom + ".\n\n";
    t += "You may redistribute and modify this source and make products using it under the terms of the " + nom + " (" + l->adresse + ").\n\n";
    t += "This source is distributed WITHOUT ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY AND FITNESS FOR A PARTICULAR "
         "PURPOSE. Please see the " + nom + " for applicable conditions.\n\n";
    if (spdx != "CERN-OHL-P-2.0")
      t += "Source location: the sources/ folder of this package (project « " + projet + " »).\n\n"
           "As per " + nom + " section 4, should You produce hardware based on this source, You must where practicable maintain the Source Location visible on "
           "the external case of the product or in its documentation.\n\n";
  } else {
    t += "Ce projet (« " + projet + " ») est publié sous la licence " + l->nom + " : " + l->adresse + "\n\n";
  }
  t += "Texte complet de la licence : " + l->adresse + "\n";
  return t;
}

namespace {
std::string dossierTemporaire(const std::string& prefixe) {
  std::random_device r;
  std::error_code ec;
  const fs::path p = fs::temp_directory_path(ec) / (prefixe + std::to_string(r()) + std::to_string(r()));
  fs::create_directories(p, ec);
  return utf8(p);
}
}  // namespace

PaquetMaterielLibre publierMaterielLibre(const Projet& p0, const std::string& cheminProjet, const OptionsMaterielLibre& o) {
  PaquetMaterielLibre r;
  const LicenceLibre* licence = licenceLibre(o.licence);
  if (!licence) {
    r.erreur = "Licence « " + o.licence + " » inconnue : choisis-en une de la liste.";
    return r;
  }
  Projet p = p0;
  p.licence = o.licence;
  const std::string nom = nomFichierAscii(p.nom.empty() ? std::string("projet") : p.nom, "projet");
  std::vector<std::pair<std::string, std::string>> fichiers;
  // Sources : l'archive du projet, avec son manifeste d'empreintes
  std::vector<std::string> manquants;
  const std::string projetDansArchive = cheminProjet.empty() ? nom + ".prom" : cheminProjet;
  for (auto& [n, contenu] : lireZip(archiverProjet(p, projetDansArchive, &manquants))) fichiers.emplace_back(nom + "/sources/" + n, std::move(contenu));
  for (const auto& m : manquants) r.alertes.push_back("Fichier cité introuvable, non joint : " + m);
  // Dossier de fabrication et visionneuse de chaque carte
  std::vector<std::string> cartes;
  for (const auto& c : p.electronique) {
    const std::string carte = nomFichierAscii(c.nom, "carte");
    cartes.push_back(c.nom);
    const std::string tmp = dossierTemporaire("promethee-libre-");
    try {
      ecrireDossierFabrication(c, tmp, o.version);
      std::error_code ec;
      for (auto it = fs::recursive_directory_iterator(cheminFichier(tmp), ec); it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) fichiers.emplace_back(nom + "/fabrication/" + carte + "/" + utf8(fs::relative(it->path(), cheminFichier(tmp), ec)), lireFichier(it->path()));
    } catch (const std::exception& e) {
      r.alertes.push_back("Dossier de fabrication de « " + c.nom + " » incomplet : " + e.what());
    }
    std::error_code ec;
    fs::remove_all(cheminFichier(tmp), ec);
    fichiers.emplace_back(nom + "/visionneuse/" + carte + ".html", visionneuseHtml(c));
  }
  // Licences des articles
  std::vector<std::vector<std::string>> lignesArticles;
  int sansLicence = 0;
  for (const auto& c : p.electronique)
    for (const auto& [id, a] : c.articles) {
      lignesArticles.push_back({a.nom.empty() ? id : a.nom, a.mpn, a.licence.empty() ? std::string("non déclarée") : a.licence});
      if (a.licence.empty()) ++sansLicence;
    }
  if (sansLicence) r.alertes.push_back(std::to_string(sansLicence) + " article(s) sans licence déclarée : leurs modèles et dessins sont à vérifier avant diffusion.");
  fichiers.emplace_back(nom + "/LICENSE", avisLicence(o.licence, p.nom, o.detenteur, o.annee));
  std::string lisez = "# " + p.nom + "\n\n" + (o.description.empty() ? std::string() : o.description + "\n\n");
  lisez += "Matériel libre publié sous la licence **" + licence->nom + "** (" + licence->spdx + ") : voir LICENSE.\n\n";
  lisez += "## Contenu\n\n- `sources/` : le projet Prométhée (" + projetDansArchive + ") avec son manifeste d’empreintes SHA-256 ; il s’ouvre tel quel dans Prométhée " +
           versionCourte() + " ou plus récent.\n";
  if (!cartes.empty()) lisez += "- `fabrication/` : dossier de fabrication de chaque carte (Gerber, perçages, placement, nomenclature).\n- `visionneuse/` : une page autonome par carte, à ouvrir dans un navigateur, sans réseau.\n";
  if (!lignesArticles.empty()) {
    lisez += "\n## Articles et leurs licences\n\n| Article | Référence | Licence |\n|---|---|---|\n";
    for (const auto& l : lignesArticles) lisez += "| " + l[0] + " | " + l[1] + " | " + l[2] + " |\n";
  }
  lisez += "\nPublié avec " + ligneVersion() + ".\n";
  fichiers.emplace_back(nom + "/README.md", lisez);
  for (const auto& [n, contenu] : fichiers) r.fichiers.push_back(n);
  r.zip = zipComprime(fichiers);
  return r;
}

std::string ouvrirPaquetMaterielLibre(const std::string& zip, const std::string& dossier, std::string* erreur) {
  std::vector<std::pair<std::string, std::string>> sources;
  for (auto& [n, contenu] : lireZip(zip)) {
    const std::size_t i = n.find("/sources/");
    if (i != std::string::npos) sources.emplace_back(n.substr(i + 9), std::move(contenu));
  }
  if (sources.empty()) {
    if (erreur) *erreur = "Le paquet ne contient pas de dossier sources/.";
    return {};
  }
  std::vector<std::string> defauts;
  const std::string chemin = extraireArchive(zipStocke(sources), dossier, &defauts);
  if (!defauts.empty() && erreur) *erreur = "Empreintes différentes : " + defauts.front();
  return defauts.empty() ? chemin : std::string();
}

// ======================================================================================================================
// 12.13 · Revue formelle
// ======================================================================================================================
bool RevueFormelle::close() const {
  for (const auto& role : rolesAttendus)
    if (std::none_of(signatures.begin(), signatures.end(), [&](const SignatureRevue& s) { return s.role == role; })) return false;
  return true;
}

std::string empreinteRevision(const ConceptionElec& c0) {
  ConceptionElec c = c0;
  c.extensions.erase("revues");
  c.extensions.erase("simulation");
  c.notes.clear();
  return sha256Hex(versJson(c).dump());
}

namespace {
J revueVersJson(const RevueFormelle& r) {
  J o = {{"numero", r.numero}, {"titre", r.titre}, {"revision", r.revision}, {"empreinte", r.empreinte}, {"date", r.date}, {"roles", r.rolesAttendus}};
  J c = J::array();
  for (const auto& x : r.constats)
    c.push_back({{"id", x.id}, {"objet", x.objet}, {"texte", x.texte}, {"auteur", x.auteur}, {"date", x.date}, {"etat", x.etat}, {"cloture", x.cloture}, {"closPar", x.closPar}, {"closLe", x.closLe}});
  o["constats"] = c;
  J p = J::array();
  for (const auto& x : r.points) p.push_back({{"texte", x.texte}, {"source", x.source}, {"objet", x.objet}, {"coche", x.coche}});
  o["points"] = p;
  J s = J::array();
  for (const auto& x : r.signatures) s.push_back({{"role", x.role}, {"nom", x.nom}, {"date", x.date}});
  o["signatures"] = s;
  if (!r.empreintePdf.empty()) o["empreintePdf"] = r.empreintePdf;
  return o;
}
std::string tx(const J& o, const char* k) { return o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); }
RevueFormelle revueDepuisJson(const J& o) {
  RevueFormelle r;
  r.numero = o.contains("numero") && o["numero"].is_number_integer() ? o["numero"].get<int>() : 0;
  r.titre = tx(o, "titre"), r.revision = tx(o, "revision"), r.empreinte = tx(o, "empreinte"), r.date = tx(o, "date"), r.empreintePdf = tx(o, "empreintePdf");
  if (o.contains("roles") && o["roles"].is_array()) {
    r.rolesAttendus.clear();
    for (const auto& x : o["roles"])
      if (x.is_string()) r.rolesAttendus.push_back(x.get<std::string>());
  }
  if (o.contains("constats") && o["constats"].is_array())
    for (const auto& x : o["constats"]) {
      ConstatRevue c;
      c.id = tx(x, "id"), c.objet = tx(x, "objet"), c.texte = tx(x, "texte"), c.auteur = tx(x, "auteur"), c.date = tx(x, "date");
      c.etat = tx(x, "etat").empty() ? std::string("ouvert") : tx(x, "etat");
      c.cloture = tx(x, "cloture"), c.closPar = tx(x, "closPar"), c.closLe = tx(x, "closLe");
      r.constats.push_back(c);
    }
  if (o.contains("points") && o["points"].is_array())
    for (const auto& x : o["points"]) r.points.push_back({tx(x, "texte"), tx(x, "source"), tx(x, "objet"), x.contains("coche") && x["coche"].is_boolean() && x["coche"].get<bool>()});
  if (o.contains("signatures") && o["signatures"].is_array())
    for (const auto& x : o["signatures"]) r.signatures.push_back({tx(x, "role"), tx(x, "nom"), tx(x, "date")});
  return r;
}
RevueFormelle* revue(std::vector<RevueFormelle>& v, int numero) {
  for (auto& r : v)
    if (r.numero == numero) return &r;
  return nullptr;
}
}  // namespace

std::vector<RevueFormelle> revuesDe(const ConceptionElec& c) {
  std::vector<RevueFormelle> v;
  if (const auto it = c.extensions.find("revues"); it != c.extensions.end() && it->second.is_object() && it->second.contains("revues") && it->second["revues"].is_array())
    for (const auto& x : it->second["revues"])
      if (x.is_object()) v.push_back(revueDepuisJson(x));
  return v;
}

void ecrireRevues(ConceptionElec& c, const std::vector<RevueFormelle>& r) {
  if (r.empty()) {
    c.extensions.erase("revues");
    return;
  }
  J o = {{"format", 1}, {"revues", J::array()}};
  for (const auto& x : r) o["revues"].push_back(revueVersJson(x));
  c.extensions["revues"] = o;
}

RevueFormelle ouvrirRevue(ConceptionElec& c, const std::string& titre, const std::string& date, const std::vector<PointControleRevue>& liste) {
  std::vector<RevueFormelle> v = revuesDe(c);
  RevueFormelle r;
  for (const auto& x : v) r.numero = std::max(r.numero, x.numero);
  ++r.numero;
  r.titre = titre.empty() ? "Revue de la révision " + c.cartouche.revision : titre;
  r.revision = c.cartouche.revision;
  r.date = date;
  r.points = liste;
  r.empreinte = empreinteRevision(c);
  v.push_back(r);
  ecrireRevues(c, v);
  return r;
}

std::string ajouterConstat(ConceptionElec& c, int numero, const std::string& objet, const std::string& texte, const std::string& auteur, const std::string& date) {
  std::vector<RevueFormelle> v = revuesDe(c);
  RevueFormelle* r = revue(v, numero);
  if (!r || r->close()) return {};
  ConstatRevue k;
  k.id = "R" + std::to_string(numero) + "-" + std::to_string(r->constats.size() + 1);
  k.objet = objet, k.texte = texte, k.auteur = auteur, k.date = date;
  r->constats.push_back(k);
  ecrireRevues(c, v);
  return k.id;
}

bool clore(ConceptionElec& c, int numero, const std::string& constat, const std::string& reponse, const std::string& par, const std::string& date) {
  std::vector<RevueFormelle> v = revuesDe(c);
  RevueFormelle* r = revue(v, numero);
  if (!r) return false;
  for (auto& k : r->constats)
    if (k.id == constat && k.etat != "clos") {
      k.etat = "clos";
      k.cloture = reponse, k.closPar = par, k.closLe = date;
      ecrireRevues(c, v);
      return true;
    }
  return false;
}

std::string signer(ConceptionElec& c, int numero, const std::string& role, const std::string& nom, const std::string& date) {
  std::vector<RevueFormelle> v = revuesDe(c);
  RevueFormelle* r = revue(v, numero);
  if (!r) return "Revue n° " + std::to_string(numero) + " introuvable.";
  if (std::find(r->rolesAttendus.begin(), r->rolesAttendus.end(), role) == r->rolesAttendus.end()) return "Le rôle « " + role + " » ne signe pas cette revue.";
  const auto ouverts = std::count_if(r->constats.begin(), r->constats.end(), [](const ConstatRevue& k) { return k.etat != "clos"; });
  if (ouverts) return std::to_string(ouverts) + " constat(s) encore ouvert(s) : la revue ne se signe qu’une fois tous clos.";
  if (r->empreinte != empreinteRevision(c))
    return "La conception a changé depuis l’ouverture de la revue (empreinte " + r->empreinte.substr(0, 12) + "… ≠ " + empreinteRevision(c).substr(0, 12) + "…) : ouvre une revue sur la nouvelle révision.";
  if (std::any_of(r->signatures.begin(), r->signatures.end(), [&](const SignatureRevue& s) { return s.role == role; })) return "Le rôle « " + role + " » a déjà signé.";
  r->signatures.push_back({role, nom, date});
  if (r->close()) r->empreintePdf = sha256Hex(pdfRevue(c, *r));
  ecrireRevues(c, v);
  return {};
}

std::string pdfRevue(const ConceptionElec& c, const RevueFormelle& r) {
  DocumentPdf d;
  d.meta = metaDepuisCartouche(c);
  d.meta.titre = "Revue n° " + std::to_string(r.numero) + " — " + c.nom;
  d.meta.sujet = "Revue formelle de la révision " + r.revision;
  d.meta.motsCles = "SHA-256 " + r.empreinte;
  d.meta.revision = r.revision;
  d.pied = c.nom + " — revue n° " + std::to_string(r.numero) + " — révision " + r.revision + " — SHA-256 " + r.empreinte.substr(0, 16) + "…";
  using G = BlocPdf::Genre;
  d.blocs.push_back({G::Titre, "Revue n° " + std::to_string(r.numero) + " : " + r.titre});
  d.blocs.push_back({G::Paragraphe, "Conception : " + c.nom + " — révision " + r.revision + " — ouverte le " + r.date});
  d.blocs.push_back({G::Paragraphe, "Empreinte SHA-256 de la révision revue :", {}, {}, true});
  d.blocs.push_back({G::Paragraphe, r.empreinte});
  d.blocs.push_back({G::SousTitre, "Constats (" + std::to_string(r.constats.size()) + ")"});
  if (r.constats.empty()) d.blocs.push_back({G::Paragraphe, "Aucun constat."});
  else {
    BlocPdf t{G::Tableau};
    t.lignes.push_back({"N°", "Objet", "Constat", "Par", "État", "Clôture"});
    t.largeurs = {0.8, 1.3, 3, 1, 0.7, 3};
    for (const auto& k : r.constats)
      t.lignes.push_back({k.id, k.objet, k.texte, k.auteur + (k.date.empty() ? std::string() : ", " + k.date), k.etat,
                          k.cloture + (k.closPar.empty() ? std::string() : " (" + k.closPar + (k.closLe.empty() ? std::string() : ", " + k.closLe) + ")")});
    d.blocs.push_back(t);
  }
  if (!r.points.empty()) {
    d.blocs.push_back({G::SousTitre, "Liste de contrôle"});
    BlocPdf t{G::Tableau};
    t.lignes.push_back({"Point", "Source", "Coché"});
    t.largeurs = {5, 2, 0.8};
    for (const auto& x : r.points) t.lignes.push_back({x.texte + (x.objet.empty() ? std::string() : " (" + x.objet + ")"), x.source, x.coche ? "oui" : "non"});
    d.blocs.push_back(t);
  }
  d.blocs.push_back({G::SousTitre, "Signatures"});
  BlocPdf s{G::Tableau};
  s.lignes.push_back({"Rôle", "Nom", "Date"});
  for (const auto& role : r.rolesAttendus) {
    const auto it = std::find_if(r.signatures.begin(), r.signatures.end(), [&](const SignatureRevue& x) { return x.role == role; });
    s.lignes.push_back({role, it == r.signatures.end() ? std::string("(à signer)") : it->nom, it == r.signatures.end() ? std::string() : it->date});
  }
  d.blocs.push_back(s);
  d.blocs.push_back({G::Paragraphe, r.close() ? "Revue close : toutes les signatures attendues sont posées. Ce rapport est figé sur la révision ci-dessus."
                                              : "Revue en cours : signatures attendues manquantes."});
  return pdfDocument(d);
}

std::string pdfHistorique(const ConceptionElec& c, const std::string& depuis, const std::string& jusqua) {
  DocumentPdf d;
  d.meta = metaDepuisCartouche(c);
  d.meta.titre = "Historique de " + c.nom;
  d.pied = c.nom + " — historique des révisions";
  using G = BlocPdf::Genre;
  d.blocs.push_back({G::Titre, "Historique des révisions de « " + c.nom + " »"});
  bool dedans = depuis.empty();
  BlocPdf t{G::Tableau};
  t.lignes.push_back({"Révision", "Date", "Auteur", "Ce qui a changé"});
  t.largeurs = {0.8, 1, 1.2, 5};
  for (const auto& h : c.cartouche.historique) {
    if (h.revision == depuis) dedans = true;
    if (dedans) t.lignes.push_back({h.revision, h.date, h.auteur, h.description});
    if (!jusqua.empty() && h.revision == jusqua) break;
  }
  if (t.lignes.size() == 1) d.blocs.push_back({G::Paragraphe, "Aucune révision publiée dans cet intervalle."});
  else d.blocs.push_back(t);
  const auto revues = revuesDe(c);
  if (!revues.empty()) {
    d.blocs.push_back({G::SousTitre, "Revues formelles"});
    BlocPdf rv{G::Tableau};
    rv.lignes.push_back({"N°", "Révision", "Constats", "Signatures", "Empreinte"});
    rv.largeurs = {0.5, 0.8, 0.9, 2.2, 3.4};
    for (const auto& r : revues) {
      std::string sig;
      for (const auto& s : r.signatures) sig += (sig.empty() ? "" : ", ") + s.role + " : " + s.nom;
      rv.lignes.push_back({std::to_string(r.numero), r.revision, std::to_string(r.constats.size()), sig, r.empreinte});
    }
    d.blocs.push_back(rv);
  }
  return pdfDocument(d);
}

// ======================================================================================================================
// 12.15 · Bibliothèque d'équipe hors réseau
// ======================================================================================================================
std::string BilanReversement::texte() const {
  std::string t = std::to_string(ajoutes.size()) + " article(s) ajouté(s), " + std::to_string(rattaches.size()) + " rattaché(s) à un article déjà publié, " +
                  std::to_string(modifies.size()) + " mis à jour";
  if (!conflits.empty()) t += ", " + std::to_string(conflits.size()) + " conflit(s) à régler";
  return t + ".";
}

CopieLocale copierPourHorsReseau(const ContenuBibliotheque& equipe, const std::string& date) {
  CopieLocale c;
  c.base = equipe;
  c.locale = equipe;
  c.depuis = date;
  return c;
}

namespace {
std::string cleReference(const Article& a) {
  const std::string m = referenceComparable(a.mpn);
  return m.empty() ? std::string() : m;
}
template <class T>
const T* parId(const std::vector<T>& v, const std::string& id) {
  for (const auto& x : v)
    if (x.id == id) return &x;
  return nullptr;
}
template <class T>
T* parId(std::vector<T>& v, const std::string& id) {
  for (auto& x : v)
    if (x.id == id) return &x;
  return nullptr;
}
}  // namespace

BilanReversement reverser(ContenuBibliotheque& equipe, CopieLocale& copie) {
  BilanReversement b;
  std::map<std::string, std::string> parReference;  // référence normalisée → article d'équipe
  for (const auto& a : equipe.articles)
    if (const std::string k = cleReference(a); !k.empty()) parReference.emplace(k, a.id);
  // Symboles, empreintes et modèles nouveaux sur le poste : emportés s'ils manquent à l'équipe
  for (const auto& s : copie.locale.symboles)
    if (!parId(copie.base.symboles, s.id) && !parId(equipe.symboles, s.id)) equipe.symboles.push_back(s), ++b.symboles;
  for (const auto& e : copie.locale.empreintes)
    if (!parId(copie.base.empreintes, e.id) && !parId(equipe.empreintes, e.id)) equipe.empreintes.push_back(e), ++b.empreintes;
  for (const auto& [chemin, contenu] : copie.locale.modeles3D)
    if (!copie.base.modeles3D.count(chemin) && !equipe.modeles3D.count(chemin)) equipe.modeles3D[chemin] = contenu, ++b.modeles;
  for (const Article& a : copie.locale.articles) {
    const Article* base = parId(copie.base.articles, a.id);
    Article* e = parId(equipe.articles, a.id);
    if (!base) {
      // Nouveau sur ce poste : rattaché à l'article de même référence fabricant publié entre-temps, sinon ajouté
      if (const std::string k = cleReference(a); !k.empty())
        if (const auto it = parReference.find(k); it != parReference.end()) {
          b.rattaches[a.id] = it->second;
          continue;
        }
      Article n = a;
      if (e) {  // même identifiant pris entre-temps par un autre article
        int i = 2;
        while (parId(equipe.articles, a.id + "-" + std::to_string(i))) ++i;
        n.id = a.id + "-" + std::to_string(i);
        b.rattaches[a.id] = n.id;
      }
      n.version = std::max(1, n.version);
      equipe.articles.push_back(n);
      if (const std::string k = cleReference(n); !k.empty()) parReference.emplace(k, n.id);
      b.ajoutes.push_back(n.id);
      continue;
    }
    if (a == *base) continue;  // inchangé sur le poste
    if (!e) {
      b.conflits.push_back(a.id + " : modifié sur le poste, supprimé de la bibliothèque d’équipe");
      continue;
    }
    if (*e == *base) {
      Article n = a;
      n.version = e->version + 1;  // révision suivante
      *e = n;
      b.modifies.push_back(a.id);
    } else if (!(*e == a)) {
      Article x = a, y = *e;
      x.version = y.version = 0;
      if (!(x == y)) b.conflits.push_back(a.id + " : modifié sur le poste et dans la bibliothèque d’équipe");
    }
  }
  copie.base = equipe;
  copie.locale = equipe;
  return b;
}

std::string texteCopieLocale(const CopieLocale& c) {
  J o = {{"format", "promethee-copie-locale"}, {"version", 1}, {"depuis", c.depuis}};
  o["base"] = J::parse(texteContenuBibliotheque(c.base));
  o["locale"] = J::parse(texteContenuBibliotheque(c.locale));
  return o.dump(1) + "\n";
}

std::optional<CopieLocale> lireCopieLocale(const std::string& texte, std::string* erreur) {
  try {
    const J o = J::parse(texte);
    if (!o.is_object() || o.value("format", std::string()) != "promethee-copie-locale") {
      if (erreur) *erreur = "Ce n’est pas une copie locale de bibliothèque.";
      return std::nullopt;
    }
    CopieLocale c;
    c.depuis = o.value("depuis", std::string());
    std::string e1, e2;
    c.base = lireContenuBibliotheque(o.at("base").dump(), &e1);
    c.locale = lireContenuBibliotheque(o.at("locale").dump(), &e2);
    if ((!e1.empty() || !e2.empty()) && erreur) *erreur = e1.empty() ? e2 : e1;
    return c;
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return std::nullopt;
  }
}

// ======================================================================================================================
// 12.18 · Nomenclature commune et données d'achat
// ======================================================================================================================
std::vector<DonneeAchat> lireDonneesAchat(const std::string& csv, std::string* erreur) {
  std::vector<DonneeAchat> v;
  const auto lignes = lireTableau(csv);
  if (lignes.empty()) {
    if (erreur) *erreur = "Fichier vide.";
    return v;
  }
  std::map<std::string, std::size_t> col;
  for (std::size_t i = 0; i < lignes[0].size(); ++i) {
    std::string n = lignes[0][i];
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    col[n] = i;
  }
  auto cellule = [&](const std::vector<std::string>& l, std::initializer_list<const char*> noms) -> std::string {
    for (const char* n : noms)
      if (const auto it = col.find(n); it != col.end() && it->second < l.size()) return l[it->second];
    return {};
  };
  for (std::size_t i = 1; i < lignes.size(); ++i) {
    const auto& l = lignes[i];
    DonneeAchat d;
    d.mpn = cellule(l, {"mpn", "référence", "reference", "référence fabricant"});
    if (d.mpn.empty()) continue;
    d.distributeur = cellule(l, {"distributeur", "fournisseur", "source"});
    if (const std::string s = cellule(l, {"stock"}); !s.empty()) d.stock = std::atoi(s.c_str());
    if (const std::string s = cellule(l, {"délai", "delai", "délai (jours)", "delai (jours)"}); !s.empty()) d.delaiJours = std::atoi(s.c_str());
    if (std::string s = cellule(l, {"agréé", "agree", "agrée"}); !s.empty()) {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      d.agree = !(s == "non" || s == "0" || s == "faux" || s == "no" || s == "false");
    }
    d.risque = cellule(l, {"risque", "contrefaçon", "contrefacon"});
    if (std::string s = cellule(l, {"prix", "prix unitaire"}); !s.empty()) {
      std::replace(s.begin(), s.end(), ',', '.');
      d.prix = std::atof(s.c_str());
    }
    v.push_back(d);
  }
  return v;
}

double NomenclatureSysteme::cout(int systemes) const {
  double t = 0;
  for (const auto& l : lignes) t += l.total * l.prix;
  return t * systemes;
}

std::vector<std::string> NomenclatureSysteme::sansPrix() const {
  std::vector<std::string> v;
  for (const auto& l : lignes)
    if (l.prix <= 0) v.push_back(l.designation);
  return v;
}

std::string NomenclatureSysteme::csv() const {
  EcrivainCsv w;
  std::vector<std::string> t{"Genre", "Désignation", "Référence", "Fabricant", "Unité"};
  t.insert(t.end(), colonnes.begin(), colonnes.end());
  t.insert(t.end(), {"Par système", "Prix unitaire", "Montant par système"});
  w.ligne(t);
  for (const auto& l : lignes) {
    w.champ(l.genre);
    w.champ(l.designation);
    w.champ(l.mpn);
    w.champ(l.fabricant);
    w.champ(l.unite);
    for (const auto& c : colonnes) {
      const auto it = l.parColonne.find(c);
      if (it == l.parColonne.end()) w.champ({});
      else w.nombre(it->second, 4);
    }
    w.nombre(l.total, 4);
    w.nombre(l.prix, 4);
    w.nombre(l.total * l.prix, 4);
    w.fin();
  }
  return w.texte();
}

NomenclatureSysteme nomenclatureSysteme(const Projet& p, const std::map<std::string, double>& prix, const std::vector<DonneeAchat>& achats, const std::set<std::string>& preferes) {
  NomenclatureSysteme n;
  std::map<std::string, std::size_t> index;
  auto ligne = [&](const std::string& cle) -> LigneNomenclatureSysteme& {
    if (const auto it = index.find(cle); it != index.end()) return n.lignes[it->second];
    index[cle] = n.lignes.size();
    n.lignes.emplace_back();
    n.lignes.back().cle = cle;
    return n.lignes.back();
  };
  auto colonneLibre = [&](std::string c) {
    for (int i = 2; std::find(n.colonnes.begin(), n.colonnes.end(), c) != n.colonnes.end(); ++i) c += " (" + std::to_string(i) + ")";
    n.colonnes.push_back(c);
    return c;
  };
  for (const auto& c : p.electronique) {
    const std::string col = colonneLibre(c.nom);
    for (const auto& x : nomenclature(c)) {
      const std::string m = referenceComparable(x.mpn);
      LigneNomenclatureSysteme& l = ligne(m.empty() ? "val:" + x.valeur + "|" + x.empreinte : "mpn:" + m);
      l.genre = "composant";
      if (l.designation.empty()) l.designation = x.description.empty() ? x.valeur + (x.empreinte.empty() ? std::string() : " " + x.empreinte) : x.description;
      if (l.mpn.empty()) l.mpn = x.mpn;
      if (l.fabricant.empty()) l.fabricant = x.fabricant;
      l.parColonne[col] += x.quantite;
    }
  }
  // Liaisons du système : fils (au mètre) et câble
  for (const auto& li : p.liaisons) {
    if (li.genre == "directe") continue;
    const std::string col = colonneLibre(li.nom.empty() ? std::string("Liaison") : li.nom);
    std::size_t fils = li.broches.size();
    if (fils == 0)
      for (const auto& c : p.electronique)
        if (c.uuid == li.carteA)
          if (const ComposantElec* k = c.parReference(li.connecteurA))
            if (const DefSymbole* d = c.symbole(k->symbole)) {
              std::set<std::string> nums;
              for (const auto& b : d->broches) nums.insert(b.numero);
              fils = nums.size();
            }
    const std::string designation = li.fil.empty() ? std::string("Fil de câblage") : li.fil;
    LigneNomenclatureSysteme& l = ligne("fil:" + designation);
    l.genre = "fil";
    l.designation = designation;
    l.unite = "m";
    l.parColonne[col] += static_cast<double>(fils) * li.longueur / 1000.0;
  }
  // Câbles des projets électriques
  for (const auto& pe : p.electrique) {
    if (pe.cables.empty()) continue;
    const std::string col = colonneLibre(pe.nom);
    for (const auto& c : pe.cables) {
      const std::string designation = c.designation + " " + std::to_string(c.conducteurs) + "G" + nombre(c.section) + (c.blinde ? " blindé" : "");
      LigneNomenclatureSysteme& l = ligne("cable:" + designation);
      l.genre = "câble";
      l.designation = designation;
      l.unite = "m";
      l.parColonne[col] += c.longueur;
    }
  }
  for (auto& l : n.lignes) {
    l.total = 0;
    for (const auto& [c, q] : l.parColonne) l.total += q;
    if (const auto it = prix.find(l.cle); it != prix.end()) l.prix = it->second;
    else if (const auto jt = prix.find(l.mpn); !l.mpn.empty() && jt != prix.end()) l.prix = jt->second;
    else if (const auto kt = prix.find(l.designation); kt != prix.end()) l.prix = kt->second;
    const std::string m = referenceComparable(l.mpn);
    for (const auto& a : achats)
      if (!m.empty() && referenceComparable(a.mpn) == m) {
        l.achats.push_back(a);
        if (l.prix <= 0 && a.prix > 0) l.prix = a.prix;
      }
    l.prefere = (!l.mpn.empty() && preferes.count(l.mpn)) || preferes.count(l.cle);
  }
  return n;
}

// ======================================================================================================================
// 12.20 · Liens entre disciplines
// ======================================================================================================================
namespace {
const ConceptionElec* conceptionDe(const Projet& p, const std::string& objet) {
  if (objet.rfind("carte:", 0) != 0) return nullptr;
  const std::string u = objet.substr(6);
  for (const auto& c : p.electronique)
    if (c.uuid == u) return &c;
  return nullptr;
}
J releveCarte(const ConceptionElec& c, const std::string& quoi) {
  J o = J::object();
  if (quoi == "contour" || quoi == "encombrement") {
    const ContourCarte& k = c.circuit.contour;
    o["largeur"] = std::round(mm(k.largeur) * 1000) / 1000;
    o["hauteur"] = std::round(mm(k.hauteur) * 1000) / 1000;
    o["rayon"] = std::round(mm(k.rayon) * 1000) / 1000;
    if (!k.libre.empty()) {
      J pts = J::array();
      for (const auto& q : k.libre) pts.push_back({std::round(mm(q.x) * 1000) / 1000, std::round(mm(q.y) * 1000) / 1000});
      o["libre"] = pts;
    }
    o["decoupes"] = k.decoupes.size();
  }
  if (quoi == "connecteurs" || quoi == "encombrement") {
    J v = J::object();
    for (const auto& e : c.circuit.empreintes)
      if (const ComposantElec* x = c.composant(e.composant); x && !x->reference.empty() && std::string("JXP").find(x->reference[0]) != std::string::npos)
        v[x->reference] = {{"x", std::round(mm(e.position.x) * 1000) / 1000}, {"y", std::round(mm(e.position.y) * 1000) / 1000}, {"rotation", e.rotation}, {"empreinte", e.empreinte}, {"dessous", e.dessous}};
    o["connecteurs"] = v;
  }
  if (quoi == "trous") {
    J v = J::array();
    for (const auto& e : c.circuit.empreintes)
      if (const ComposantElec* x = c.composant(e.composant); x && x->reference.rfind("H", 0) == 0)
        v.push_back({{"repere", x->reference}, {"x", std::round(mm(e.position.x) * 1000) / 1000}, {"y", std::round(mm(e.position.y) * 1000) / 1000}});
    o["trous"] = v;
  }
  return o;
}
// Valeurs à plat (« contour.largeur » → « 50 ») pour dire ce qui a changé
void aplatir(const J& j, const std::string& prefixe, std::map<std::string, std::string>& sortie) {
  if (j.is_object()) {
    for (auto it = j.begin(); it != j.end(); ++it) aplatir(it.value(), prefixe.empty() ? it.key() : prefixe + "." + it.key(), sortie);
  } else if (j.is_array() && !j.empty() && (j.front().is_object() || j.front().is_array())) {
    for (std::size_t i = 0; i < j.size(); ++i) aplatir(j[i], prefixe + "[" + std::to_string(i + 1) + "]", sortie);
  } else if (j.is_number_float()) {
    sortie[prefixe] = nombre(j.get<double>(), 3);
  } else
    sortie[prefixe] = j.is_string() ? j.get<std::string>() : j.dump();
}
std::vector<std::string> differences(const std::string& avant, const std::string& apres, const std::string& quoi) {
  std::map<std::string, std::string> a, b;
  try {
    if (!avant.empty()) aplatir(J::parse(avant), quoi, a);
    if (!apres.empty()) aplatir(J::parse(apres), quoi, b);
  } catch (...) {
    return {quoi + " : changé"};
  }
  std::vector<std::string> v;
  const bool mmUnite = quoi == "contour" || quoi == "trous" || quoi == "connecteurs" || quoi == "encombrement";
  auto unite = [&](const std::string& k) { return mmUnite && (k.find("rotation") == std::string::npos) && (k.find("empreinte") == std::string::npos) && k.find("dessous") == std::string::npos && k.find("decoupes") == std::string::npos ? std::string(" mm") : std::string(); };
  for (const auto& [k, x] : a) {
    const auto it = b.find(k);
    if (it == b.end()) v.push_back(k + " : " + x + unite(k) + " → (retiré)");
    else if (it->second != x) v.push_back(k + " : " + x + " → " + it->second + unite(k));
  }
  for (const auto& [k, y] : b)
    if (!a.count(k)) v.push_back(k + " : (ajouté) " + y + unite(k));
  return v;
}
}  // namespace

std::string releveLien(const Projet& p, const LienDisciplines& l) {
  if (const ConceptionElec* c = conceptionDe(p, l.de)) return releveCarte(*c, l.quoi).dump();
  if (l.de == "boitier") return J{{"boitier", J::parse(versJson(p).value("boitier", J::object()).dump())}}.dump();
  return {};
}

std::vector<const LienDisciplines*> liensARevoir(const Projet& p, const std::string& vers) {
  std::vector<const LienDisciplines*> v;
  for (const auto& l : p.liensDisciplines) {
    if (!vers.empty() && l.vers != vers) continue;
    const std::string r = releveLien(p, l);
    if (r.empty() || sha256Hex(r) != l.empreinte) v.push_back(&l);
  }
  return v;
}

LienDisciplines lierDisciplines(const Projet& p, const std::string& de, const std::string& vers, const std::string& quoi, const std::string& personne, const std::string& date) {
  LienDisciplines l;
  l.id = nouvelUuid();
  l.de = de, l.vers = vers, l.quoi = quoi;
  l.releve = releveLien(p, l);
  l.empreinte = sha256Hex(l.releve);
  l.accusePar = personne, l.accuseLe = date;
  return l;
}

int verifierLiens(Projet& p) {
  int n = 0;
  for (auto& l : p.liensDisciplines) {
    const std::string r = releveLien(p, l);
    if (r.empty()) {
      if (l.etat != "à revoir") ++n;
      l.etat = "à revoir";
      l.changements = {l.de + " a disparu du projet"};
      continue;
    }
    if (sha256Hex(r) == l.empreinte) {
      l.etat = "à jour";
      l.changements.clear();
      continue;
    }
    if (l.etat != "à revoir") ++n;
    l.etat = "à revoir";
    l.changements = differences(l.releve, r, l.quoi);
  }
  return n;
}

bool accuserLecture(Projet& p, const std::string& lien, const std::string& personne, const std::string& date) {
  for (auto& l : p.liensDisciplines)
    if (l.id == lien) {
      l.releve = releveLien(p, l);
      l.empreinte = sha256Hex(l.releve);
      l.etat = "à jour";
      l.changements.clear();
      l.accusePar = personne, l.accuseLe = date;
      return true;
    }
  return false;
}

FaisceauEntreCartes faisceauDeLiaison(const Projet& p, const LiaisonSysteme& l, double longueur) {
  FaisceauEntreCartes r;
  const ConceptionElec *a = nullptr, *b = nullptr;
  for (const auto& c : p.electronique) {
    if (c.uuid == l.carteA) a = &c;
    if (c.uuid == l.carteB) b = &c;
  }
  if (!a || !b) {
    r.erreur = "Carte de la liaison introuvable.";
    return r;
  }
  const ComposantElec* ka = a->parReference(l.connecteurA);
  const ComposantElec* kb = b->parReference(l.connecteurB);
  if (!ka || !kb) {
    r.erreur = "Connecteur de la liaison introuvable.";
    return r;
  }
  const double L = l.longueur > 0 ? l.longueur : longueur;
  r.faisceau.uuid = nouvelUuid();
  r.faisceau.nom = l.nom.empty() ? "Faisceau " + a->nom + " – " + b->nom : l.nom;
  elec::NoeudFaisceau na, nb;
  na.uuid = nouvelUuid(), na.nom = a->nom + " " + l.connecteurA, na.p = ptMm(0, 0);
  nb.uuid = nouvelUuid(), nb.nom = b->nom + " " + l.connecteurB, nb.p = ptMm(L, 0);
  r.faisceau.noeuds = {na, nb};
  r.faisceau.branches.push_back({na.uuid, nb.uuid, L, {}});
  std::vector<std::pair<std::string, std::string>> paires = l.broches;
  if (paires.empty())
    if (const DefSymbole* d = a->symbole(ka->symbole)) {
      std::vector<std::string> nums;
      for (const auto& x : d->broches) nums.push_back(x.numero);
      std::sort(nums.begin(), nums.end(), avantNumerique);
      nums.erase(std::unique(nums.begin(), nums.end()), nums.end());
      for (const auto& x : nums) paires.emplace_back(x, x);
    }
  const Connectivite k = connectivite(*a);
  int i = 0;
  for (const auto& [x, y] : paires) {
    FilFaisceau f;
    f.numero = std::to_string(++i);
    f.de = l.connecteurA + ":" + x;
    f.vers = l.connecteurB + ":" + y;
    f.reseau = k.nom(ka->uuid, x);
    f.longueur = L + 2 * r.faisceau.surlongueur;
    r.fils.push_back(f);
  }
  return r;
}

}  // namespace prom

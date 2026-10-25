// Prométhée : bibliothèques des autres logiciels (10.4, 10.18). Voir import_bibliotheques.hpp. Licence GPL-3.0-only.
#include "promethee/import_bibliotheques.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include "promethee/archive.hpp"
#include "promethee/formats_anciens.hpp"
#include "promethee/formats_cao.hpp"
#include "promethee/import_altium.hpp"
#include "promethee/import_eagle.hpp"
#include "promethee/import_easyeda.hpp"
#include "promethee/kicad_electronique.hpp"
#include "kicad_interne.hpp"

namespace prom::ecad {

namespace {
std::string minuscules(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}
bool finit(const std::string& n, const std::string& ext) {
  const std::string b = minuscules(n);
  return b.size() >= ext.size() && b.compare(b.size() - ext.size(), ext.size(), ext) == 0;
}
std::string base(const std::string& chemin) {
  const auto p = chemin.find_last_of("/\\");
  return p == std::string::npos ? chemin : chemin.substr(p + 1);
}
std::string sansExtension(const std::string& n) {
  const auto p = n.find_last_of('.');
  return p == std::string::npos ? n : n.substr(0, p);
}
// Bibliothèque d'une empreinte KiCad : le dossier « <nom>.pretty » qui la contient
std::string bibliothequePretty(const std::string& chemin) {
  const auto p = chemin.find_last_of("/\\");
  if (p == std::string::npos) return {};
  const std::string dossier = base(chemin.substr(0, p));
  return finit(dossier, ".pretty") ? dossier.substr(0, dossier.size() - 7) : std::string();
}

// Noms des symboles de premier niveau d'une bibliothèque .kicad_sym, et lesquels sont des alimentations
struct SymboleListe {
  std::string nom;
  bool alimentation = false;
};
std::vector<SymboleListe> listerSymbolesKicad(const std::string& texte) {
  std::vector<SymboleListe> r;
  try {
    kicad_interne::Lecteur l(texte);
    const kicad_interne::Expr racine = l.lire();
    for (const auto& x : racine.elements)
      if (x.nom() == "symbol") r.push_back({x.valeur(1), x.enfant("power") != nullptr});
  } catch (const std::exception&) {
  }
  return r;
}

void importerKicad(const std::vector<FichierBibliotheque>& fichiers, ContenuBibliotheque& dest, bool remplacer, RapportBibliotheque& rap) {
  // Empreintes d'abord, pour que les articles s'y relient ; « Lib:Nom » → identifiant retenu
  std::map<std::string, std::string> empreintes;
  for (const auto& f : fichiers) {
    if (!finit(f.chemin, ".kicad_mod")) continue;
    const std::string nom = sansExtension(base(f.chemin)), lib = bibliothequePretty(f.chemin);
    std::vector<std::string> alertes;
    auto d = empreinteDepuisKicadMod(f.contenu, &alertes);
    if (!d) {
      rap.refuser("empreinte", nom, alertes.empty() ? "aucune pastille" : alertes.front());
      continue;
    }
    d->id = "kicad:" + (lib.empty() ? std::string() : lib + ":") + nom;
    empreintes[lib.empty() ? nom : lib + ":" + nom] = d->id;
    empreintes[nom] = d->id;
    rap.poser(dest.empreintes, std::move(*d), "empreinte", lib.empty() ? nom : lib + ":" + nom, remplacer);
    if (!alertes.empty()) rap.objets.back().detail += (rap.objets.back().detail.empty() ? "" : " ; ") + alertes.front();
  }
  for (const auto& f : fichiers) {
    if (!finit(f.chemin, ".kicad_sym")) continue;
    const std::string lib = sansExtension(base(f.chemin));
    std::vector<std::string> alertes;
    const std::vector<DefSymbole> lus = symbolesDepuisKicadSym(f.contenu, &alertes);
    std::map<std::string, const DefSymbole*> parNom;
    for (const auto& d : lus) parNom[d.nom] = &d;
    const auto liste = listerSymbolesKicad(f.contenu);
    if (liste.empty()) rap.refuser("symbole", base(f.chemin), alertes.empty() ? "fichier illisible" : alertes.front());
    for (const auto& s : liste) {
      const auto it = parNom.find(s.nom);
      if (it == parNom.end()) {
        rap.refuser("symbole", lib + ":" + s.nom, s.alimentation ? "symbole d'alimentation : ceux de Prométhée (GND, +5V…) le remplacent" : "symbole sans broche");
        continue;
      }
      DefSymbole d = *it->second;
      d.id = "kicad:" + lib + ":" + s.nom;
      // Empreinte : celle lue avec la bibliothèque, sinon celle que nommait le symbole (catalogue ou à importer)
      std::string detail;
      if (d.empreinte.rfind("kicad:", 0) == 0) {
        const std::string cible = d.empreinte.substr(6);
        if (const auto e = empreintes.find(cible); e != empreintes.end()) d.empreinte = e->second;
        else if (const auto e2 = empreintes.find(cible.substr(cible.find(':') == std::string::npos ? 0 : cible.find(':') + 1)); e2 != empreintes.end()) d.empreinte = e2->second;
        else if (std::none_of(dest.empreintes.begin(), dest.empreintes.end(), [&](const DefEmpreinte& x) { return x.id == d.empreinte; }))
          detail = "empreinte « " + cible + " » ni lue ni dans la bibliothèque : à importer";
      }
      rap.poser(dest.symboles, d, "symbole", lib + ":" + s.nom, remplacer);
      Article a;
      a.id = lib + ":" + s.nom;
      a.nom = s.nom;
      a.valeur = s.nom;
      a.description = d.description;
      a.categorie = lib;
      a.symbole = d.id;
      a.empreinte = d.empreinte;
      a.fiche = d.fiche;
      rap.poser(dest.articles, std::move(a), "article", lib + ":" + s.nom, remplacer);
      if (!detail.empty()) rap.objets.back().detail = detail;
    }
  }
}
}  // namespace

std::string filtreBibliotheques() { return "*.kicad_sym *.kicad_mod *.lbr *.SchLib *.PcbLib *.IntLib *.schlib *.pcblib *.intlib *.fp *.sym *.lmk *.LMK *.cpa *.csa *.json"; }

bool fichierBibliothequeLu(const std::string& chemin) {
  for (const char* e : {".kicad_sym", ".kicad_mod", ".lbr", ".schlib", ".pcblib", ".intlib", ".fp", ".sym", ".lmk", ".cpa", ".csa", ".json"})
    if (finit(chemin, e)) return true;
  return false;
}

namespace {
// Bibliothèques Altium : symboles (un article par symbole qui nomme son empreinte), empreintes ; une bibliothèque
// intégrée (.IntLib) porte ses bibliothèques de symboles et d'empreintes compressées, chacune lue de même
void importerAltium(const std::string& chemin, const std::string& octets, ContenuBibliotheque& dest, bool remplacer, RapportBibliotheque& rap, int profondeur = 0) {
  const std::string lib = sansExtension(base(chemin));
  if (finit(chemin, ".intlib") && profondeur == 0) {
    int lus = 0;
    for (const auto& [nom, flux] : lireCfb(octets)) {
      if (flux.size() < 3 || flux[0] != '\x02') continue;
      try {
        const std::string sous = inflerZlib(flux.substr(1));
        if (!estCfb(sous)) continue;
        importerAltium(lib + (estBibliothequeAltium(sous) ? ".SchLib" : ".PcbLib"), sous, dest, remplacer, rap, 1);
        ++lus;
      } catch (const std::exception&) {
      }
    }
    if (!lus) rap.refuser("bibliothèque", base(chemin), "aucune bibliothèque lisible dans la bibliothèque intégrée");
    return;
  }
  const BibliothequeAltium b = lireBibliothequeAltium(octets, lib);
  for (const auto& a : b.alertes) rap.refuser("objet", lib, a);
  for (const DefSymbole& d : b.symboles) {
    rap.poser(dest.symboles, d, "symbole", lib + ":" + d.nom, remplacer);
    if (!d.empreinte.empty()) {
      Article a;
      a.id = "altium:" + lib + ":" + d.nom;
      a.nom = d.nom;
      a.description = d.description;
      a.categorie = "Importés d’Altium";
      a.symbole = d.id;
      a.empreinte = d.empreinte;
      rap.poser(dest.articles, std::move(a), "article", lib + ":" + d.nom, remplacer);
    }
  }
  for (const DefEmpreinte& d : b.empreintes) rap.poser(dest.empreintes, d, "empreinte", lib + ":" + d.description, remplacer);
}
}  // namespace

namespace {
// Dossier qui porte un fichier de bibliothèque (« newlib/0805.fp » → « newlib ») : il nomme la bibliothèque
std::string dossierDe(const std::string& chemin) {
  const auto p = chemin.find_last_of("/\\");
  return p == std::string::npos ? std::string() : base(chemin.substr(0, p));
}
// Bibliothèques des formats anciens et des autres outils (10.18) : empreintes gEDA (.fp), symboles gEDA (.sym, un
// article chacun), macros Sprint-Layout (.lmk), archives de bibliothèque CADSTAR (.cpa d'empreintes, .csa de
// symboles), composants EasyEDA ou LCSC enregistrés en JSON (article, symbole, empreinte)
void importerAncien(const FichierBibliotheque& f, ContenuBibliotheque& dest, bool remplacer, RapportBibliotheque& rap) {
  const std::string nom = sansExtension(base(f.chemin)), lib = dossierDe(f.chemin);
  const std::string complet = lib.empty() ? nom : lib + ":" + nom;
  if (finit(f.chemin, ".fp")) {
    auto d = empreinteDepuisGedaFp(f.contenu, nom);
    if (!d || d->pastilles.empty()) return rap.refuser("empreinte", complet, "aucun élément (« Element[…] ») lisible");
    d->id = "geda:" + complet;
    rap.poser(dest.empreintes, std::move(*d), "empreinte", complet, remplacer);
  } else if (finit(f.chemin, ".sym")) {
    std::string raison;
    auto d = symboleDepuisGedaSym(f.contenu, complet, &raison);
    if (!d) return rap.refuser("symbole", complet, raison);
    rap.poser(dest.symboles, *d, "symbole", complet, remplacer);
    Article a;
    a.id = "geda:" + complet;
    a.nom = nom;
    a.valeur = nom;
    a.description = d->description;
    a.categorie = "Importés de gEDA";
    a.symbole = d->id;
    a.fiche = d->fiche;
    rap.poser(dest.articles, std::move(a), "article", complet, remplacer);
  } else if (finit(f.chemin, ".lmk")) {
    std::string alerte;
    auto d = empreinteDepuisMacroSprint(f.contenu, nom, &alerte);
    if (!d) return rap.refuser("empreinte", complet, alerte.empty() ? "macro vide" : alerte);
    d->id = "sprint:" + complet;
    rap.poser(dest.empreintes, std::move(*d), "empreinte", complet, remplacer);
    if (!alerte.empty()) rap.objets.back().detail = alerte;
  } else if (finit(f.chemin, ".cpa") || finit(f.chemin, ".csa")) {
    if (!estArchiveCadstar(f.contenu)) return rap.refuser("bibliothèque", base(f.chemin), "archive CADSTAR illisible");
    RapportLectureCao r;
    const ConceptionElec c = conceptionDepuisCadstar(f.contenu, nom, &r);
    int lus = 0;
    for (const auto& [id, d] : c.empreintes)
      if (id.rfind("cadstar:", 0) == 0) {
        rap.poser(dest.empreintes, d, "empreinte", lib.empty() ? d.description : lib + ":" + d.description, remplacer), ++lus;
      }
    for (const auto& [id, d] : c.symboles)
      if (id.rfind("cadstar:", 0) == 0) {
        rap.poser(dest.symboles, d, "symbole", d.nom, remplacer), ++lus;
      }
    if (!lus) rap.refuser("bibliothèque", base(f.chemin), "aucun symbole ni empreinte dans l'archive");
  } else if (finit(f.chemin, ".json")) {
    const ImportArchive a = importerComposantEasyEda(f.contenu);
    if (a.article.id.empty() && a.symboles.empty() && !a.empreinte) return rap.refuser("composant", base(f.chemin), a.alertes.empty() ? "pas un composant EasyEDA ou LCSC" : a.alertes.front());
    for (const auto& s : a.symboles) rap.poser(dest.symboles, s, "symbole", s.nom, remplacer);
    if (a.empreinte) rap.poser(dest.empreintes, *a.empreinte, "empreinte", a.empreinte->description.empty() ? a.empreinte->id : a.empreinte->description, remplacer);
    if (!a.article.id.empty()) rap.poser(dest.articles, a.article, "article", a.article.nom.empty() ? a.article.id : a.article.nom, remplacer);
  }
}
}  // namespace

RapportBibliotheque importerBibliotheques(const std::vector<FichierBibliotheque>& fichiers, ContenuBibliotheque& destination, bool remplacer) {
  RapportBibliotheque rap;
  importerKicad(fichiers, destination, remplacer, rap);
  for (const auto& f : fichiers) {
    if (finit(f.chemin, ".lbr")) {
      try {
        const RapportBibliotheque r = importerBibliothequeEagle(f.contenu, destination, remplacer);
        rap.objets.insert(rap.objets.end(), r.objets.begin(), r.objets.end());
      } catch (const std::exception& e) {
        rap.refuser("bibliothèque", base(f.chemin), e.what());
      }
    } else if (finit(f.chemin, ".schlib") || finit(f.chemin, ".pcblib") || finit(f.chemin, ".intlib")) {
      try {
        importerAltium(f.chemin, f.contenu, destination, remplacer, rap);
      } catch (const std::exception& e) {
        rap.refuser("bibliothèque", base(f.chemin), e.what());
      }
    } else if (finit(f.chemin, ".fp") || finit(f.chemin, ".sym") || finit(f.chemin, ".lmk") || finit(f.chemin, ".cpa") || finit(f.chemin, ".csa") || finit(f.chemin, ".json")) {
      try {
        importerAncien(f, destination, remplacer, rap);
      } catch (const std::exception& e) {
        rap.refuser("bibliothèque", base(f.chemin), e.what());
      }
    } else if (!fichierBibliothequeLu(f.chemin))
      rap.refuser("fichier", base(f.chemin), "format non lu");
  }
  return rap;
}

}  // namespace prom::ecad

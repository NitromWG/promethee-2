// Prométhée : exclusions « à revoir » et rapport de vérification (lot L5). Licence GPL-3.0-only.
#include "promethee/verification.hpp"

#include <algorithm>
#include <map>
#include <sstream>

#include "promethee/hachage.hpp"

namespace prom::ecad {

namespace {
std::string html(const std::string& s) {
  std::string r;
  for (char ch : s) {
    if (ch == '<') r += "&lt;";
    else if (ch == '>') r += "&gt;";
    else if (ch == '&') r += "&amp;";
    else if (ch == '"') r += "&quot;";
    else r += ch;
  }
  return r;
}
std::string pt(Pt p) {
  char b[64];
  std::snprintf(b, sizeof b, "%.3f ; %.3f mm", mm(p.x), mm(p.y));
  return b;
}
}  // namespace

std::string signatureObjets(const ConceptionElec& c, const std::vector<std::string>& objets) {
  std::ostringstream o;
  for (const auto& id : objets) {
    o << id << ':';
    for (const auto& t : c.circuit.pistes)
      if (t.uuid == id) o << "P" << t.a.x << ',' << t.a.y << ',' << t.b.x << ',' << t.b.y << ',' << t.largeur << ',' << static_cast<int>(t.couche);
    for (const auto& v : c.circuit.vias)
      if (v.uuid == id) o << "V" << v.p.x << ',' << v.p.y << ',' << v.diametre << ',' << v.percage;
    for (const auto& e : c.circuit.empreintes)
      if (e.composant == id) o << "E" << e.empreinte << ',' << e.position.x << ',' << e.position.y << ',' << e.rotation << ',' << e.dessous;
    for (const auto& z : c.circuit.zones)
      if (z.uuid == id) {
        o << "Z" << z.reseau << ',' << static_cast<int>(z.couche);
        for (const Pt q : z.contour) o << ',' << q.x << ',' << q.y;
      }
    o << ';';
  }
  return sha256Hex(o.str()).substr(0, 16);
}

ExclusionErc exclusionPour(const ConceptionElec& c, const Violation& v, const std::string& raison, const std::string& auteur, const std::string& date) {
  ExclusionErc e;
  e.code = v.code;
  e.objets = v.objets;
  e.raison = raison;
  e.auteur = auteur;
  e.date = date;
  e.signature = signatureObjets(c, v.objets);
  return e;
}

BilanVerification bilanDRC(const ConceptionElec& c, const Connectivite& k, const Remplissages& r) {
  BilanVerification b;
  b.actives = verifierDRCReglee(c, k, r);
  for (Violation v : verifierDRC(c, k, r)) {
    if (const auto it = c.erc.gravites.find(v.code); it != c.erc.gravites.end() && it->second == GraviteReglee::Ignorer) continue;
    const ExclusionErc* e = exclusionDe(c, v);
    if (!e) continue;
    if (!e->signature.empty() && e->signature != signatureObjets(c, v.objets)) continue;  // « à revoir » : déjà parmi les actives
    b.exclues.push_back({v, *e});
  }
  return b;
}

std::string rapportVerificationHtml(const ConceptionElec& c, const std::vector<Violation>& erc, const BilanVerification& drc) {
  std::ostringstream o;
  o << "<!doctype html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\"><title>Vérification de " << html(c.nom) << "</title>\n"
    << "<style>body{font-family:sans-serif;margin:16px;color:#1d2330}h1{font-size:20px}h2{font-size:16px;margin-top:22px}"
       "table{border-collapse:collapse;width:100%}td,th{border-bottom:1px solid #d5d9e0;padding:4px 6px;text-align:left;vertical-align:top}"
       ".erreur{color:#b3261e}.avertissement{color:#8a5a00}.revoir{background:#fff4d6}.exclue{color:#5b6372}a{color:#1f5fbf}</style></head><body>\n";
  o << "<h1>Vérification de « " << html(c.nom) << " »</h1>\n";
  std::size_t erreurs = 0, avertissements = 0;
  for (const auto* liste : {&erc, &drc.actives})
    for (const auto& v : *liste) (v.gravite == Gravite::Erreur ? erreurs : avertissements)++;
  o << "<p>" << erreurs << " erreur(s), " << avertissements << " avertissement(s), " << drc.exclues.size() << " exclusion(s).</p>\n";
  std::size_t n = 0;
  auto section = [&](const std::string& titre, const std::vector<Violation>& v) {
    std::map<std::string, std::vector<std::pair<std::size_t, const Violation*>>> parCode;
    for (const auto& x : v) parCode[x.code].push_back({n++, &x});
    if (parCode.empty()) return;
    o << "<h2>" << html(titre) << "</h2>\n";
    for (const auto& [code, liste] : parCode) {
      o << "<h3 id=\"" << html(code) << "\">" << html(code) << " (" << liste.size() << ")</h3>\n<table>\n";
      for (const auto& [i, x] : liste)
        o << "<tr class=\"" << (x->aRevoir ? "revoir " : "") << (x->gravite == Gravite::Erreur ? "erreur" : "avertissement") << "\"><td><a href=\"promethee:violation/" << i << "\">"
          << html(x->message) << "</a></td><td>" << html(x->conseil) << "</td><td>" << pt(x->position) << "</td></tr>\n";
      o << "</table>\n";
    }
  };
  section("Vérification électrique (ERC)", erc);
  section("Règles de dessin (DRC)", drc.actives);
  if (!drc.exclues.empty()) {
    o << "<h2>Exclusions</h2>\n<table>\n";
    for (const auto& [v, e] : drc.exclues)
      o << "<tr class=\"exclue\"><td>" << html(v.code) << "</td><td>" << html(v.message) << "</td><td>" << html(e.raison) << (e.auteur.empty() ? "" : " — " + html(e.auteur))
        << "</td></tr>\n";
    o << "</table>\n";
  }
  o << "</body></html>\n";
  return o.str();
}

std::string rapportVerificationTexte(const ConceptionElec& c, const std::vector<Violation>& erc, const BilanVerification& drc) {
  std::ostringstream o;
  o << "Vérification de « " << c.nom << " »\n";
  auto section = [&](const std::string& titre, const std::vector<Violation>& v) {
    std::map<std::string, std::vector<const Violation*>> parCode;
    for (const auto& x : v) parCode[x.code].push_back(&x);
    if (parCode.empty()) return;
    o << "\n== " << titre << " ==\n";
    for (const auto& [code, liste] : parCode) {
      o << code << " (" << liste.size() << ")\n";
      for (const auto* x : liste) o << "  " << (x->gravite == Gravite::Erreur ? "[erreur] " : "[avertissement] ") << x->message << " (" << pt(x->position) << ")\n";
    }
  };
  section("ERC", erc);
  section("DRC", drc.actives);
  if (!drc.exclues.empty()) {
    o << "\n== Exclusions ==\n";
    for (const auto& [v, e] : drc.exclues) o << "  " << v.code << " : " << v.message << " — " << e.raison << "\n";
  }
  return o.str();
}

}  // namespace prom::ecad

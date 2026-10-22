// Prométhée : exigences et ordres de modification, lecture et écriture (lot L19). Licence GPL-3.0-only.
#include "promethee/exigences.hpp"
#include "promethee/systeme.hpp"

#include <algorithm>

namespace prom {

namespace {
using J = nlohmann::ordered_json;
std::string txt(const J& o, const char* k, const std::string& defaut = {}) { return o.is_object() && o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : defaut; }
std::vector<std::string> textes(const J& o, const char* k) {
  std::vector<std::string> r;
  if (o.is_object() && o.contains(k) && o[k].is_array())
    for (const auto& x : o[k])
      if (x.is_string()) r.push_back(x.get<std::string>());
  return r;
}
}  // namespace

J exigenceVersJson(const Exigence& e) {
  J o = {{"id", e.id}, {"texte", e.texte}};
  if (!e.source.empty()) o["source"] = e.source;
  if (e.methode != "revue") o["methode"] = e.methode;
  if (!e.liens.empty()) o["liens"] = e.liens;
  if (!e.grandeur.empty()) {
    o["grandeur"] = e.grandeur;
    o["comparateur"] = e.comparateur;
    o["seuil"] = e.seuil;
    if (e.quantite != 1) o["quantite"] = e.quantite;
  }
  return o;
}

Exigence exigenceDepuisJson(const J& o) {
  Exigence e;
  e.id = txt(o, "id");
  e.texte = txt(o, "texte");
  e.source = txt(o, "source");
  e.methode = txt(o, "methode", "revue");
  e.liens = textes(o, "liens");
  e.grandeur = txt(o, "grandeur");
  e.comparateur = txt(o, "comparateur", "<=");
  if (e.comparateur != "<=" && e.comparateur != ">=" && e.comparateur != "==") e.comparateur = "<=";
  e.seuil = txt(o, "seuil");
  if (o.is_object() && o.contains("quantite") && o["quantite"].is_number_integer()) e.quantite = std::max(1, o["quantite"].get<int>());
  return e;
}

J ordreVersJson(const OrdreModification& m) {
  J o = {{"id", m.id}, {"titre", m.titre}, {"etat", m.etat}};
  if (!m.motif.empty()) o["motif"] = m.motif;
  if (!m.revisionDepart.empty()) o["revisionDepart"] = m.revisionDepart;
  if (!m.revisionArrivee.empty()) o["revisionArrivee"] = m.revisionArrivee;
  if (!m.dateEffet.empty()) o["dateEffet"] = m.dateEffet;
  if (m.sortStock != "utiliser") o["sortStock"] = m.sortStock;
  if (!m.objets.empty()) o["objets"] = m.objets;
  if (!m.approbations.empty()) {
    J a = J::array();
    for (const auto& x : m.approbations) {
      J y = {{"personne", x.personne}, {"role", x.role}, {"date", x.date}, {"decision", x.decision}};
      if (!x.commentaire.empty()) y["commentaire"] = x.commentaire;
      a.push_back(y);
    }
    o["approbations"] = a;
  }
  return o;
}

OrdreModification ordreDepuisJson(const J& o) {
  OrdreModification m;
  m.id = txt(o, "id");
  m.titre = txt(o, "titre");
  m.etat = txt(o, "etat", "ouvert");
  m.motif = txt(o, "motif");
  m.revisionDepart = txt(o, "revisionDepart");
  m.revisionArrivee = txt(o, "revisionArrivee");
  m.dateEffet = txt(o, "dateEffet");
  m.sortStock = txt(o, "sortStock", "utiliser");
  m.objets = textes(o, "objets");
  if (o.is_object() && o.contains("approbations") && o["approbations"].is_array())
    for (const auto& x : o["approbations"])
      if (x.is_object()) m.approbations.push_back({txt(x, "personne"), txt(x, "role"), txt(x, "date"), txt(x, "decision"), txt(x, "commentaire")});
  return m;
}

J liaisonVersJson(const LiaisonSysteme& l) {
  J o = {{"uuid", l.uuid}, {"nom", l.nom}, {"genre", l.genre}, {"carteA", l.carteA}, {"connecteurA", l.connecteurA}, {"carteB", l.carteB}, {"connecteurB", l.connecteurB}};
  if (!l.broches.empty()) {
    J b = J::array();
    for (const auto& [x, y] : l.broches) b.push_back({x, y});
    o["broches"] = b;
  }
  return o;
}

LiaisonSysteme liaisonDepuisJson(const J& o) {
  LiaisonSysteme l;
  l.uuid = txt(o, "uuid");
  l.nom = txt(o, "nom");
  l.genre = txt(o, "genre", "cable");
  l.carteA = txt(o, "carteA");
  l.connecteurA = txt(o, "connecteurA");
  l.carteB = txt(o, "carteB");
  l.connecteurB = txt(o, "connecteurB");
  if (o.is_object() && o.contains("broches") && o["broches"].is_array())
    for (const auto& x : o["broches"])
      if (x.is_array() && x.size() == 2 && x[0].is_string() && x[1].is_string()) l.broches.emplace_back(x[0].get<std::string>(), x[1].get<std::string>());
  return l;
}

}  // namespace prom

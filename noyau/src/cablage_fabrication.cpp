// Prométhée : fabrication du faisceau (lot L19 de la 0.34 ; 19.13). Contrôles, nomenclature « selon besoin »,
// classeur XLSX, plan de fabrication, fichiers des machines et des testeurs, devis ; disposition du folio de câblage
// (19.10). Voir cablage.hpp. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

#include "cablage_interne.hpp"
#include "electrique_interne.hpp"
#include "pdf_vectoriel.hpp"
#include "promethee/cablage.hpp"
#include "promethee/classeur.hpp"
#include "promethee/pdf_documents.hpp"

namespace prom::elec {

using Json = nlohmann::ordered_json;

namespace {
std::string fr(double v, int decimales = 2) {
  const double p = std::pow(10.0, decimales);
  return detail::nombreFr(std::round(v * p) / p);
}
const FilCablage* filA(const Cablage& h, const std::string& k, const std::string& c) {
  for (const auto& f : h.fils)
    if ((f.de == k && f.deCavite == c) || (f.vers == k && f.versCavite == c)) return &f;
  return nullptr;
}
double longueurProtection(const Cablage& h, const ProtectionPosee& p) {
  for (const auto& t : h.troncons)
    if (t.uuid == p.troncon) {
      const double L = longueurTroncon(h, t);
      const double fin = p.fin > 0 ? std::min(p.fin, L) : L;
      return std::max(0.0, fin - std::max(0.0, p.debut));
    }
  return 0;
}
double diametreDuTroncon(const std::vector<OccupationTroncon>& occ, const std::string& t) {
  for (const auto& o : occ)
    if (o.troncon == t) return o.diametre;
  return 0;
}
}  // namespace

// ======================================================================================================================
// Contrôles
// ======================================================================================================================
std::vector<DefautFaisceau> verifierFaisceau(const Cablage& h, const BibliothequeCablage& b, const std::vector<CircuitContinu>& circuits) {
  std::vector<DefautFaisceau> r;
  for (const auto& f : h.fils) {
    const bool deOk = !f.de.empty() && !f.deCavite.empty(), versOk = (!f.vers.empty() && !f.versCavite.empty()) || (!f.surFil.empty() && h.fil(f.surFil));
    if (f.drain ? (!deOk && !versOk) : (!deOk || !versOk))
      r.push_back({"fil sans extrémité", "fil " + f.numero, "le fil " + f.numero + (deOk ? " n'arrive nulle part" : " ne part de nulle part")});
  }
  for (const auto& k : h.connecteurs) {
    const EntreeConnecteurCablage* e = k.reference.empty() ? nullptr : b.connecteur(k.reference);
    const bool etanche = k.etanche || (e && e->etanche);
    for (const auto& c : k.cavites) {
      const FilCablage* f = filA(h, k.repere, c.numero);
      const std::string ou = k.repere + ":" + c.numero;
      if (f && !c.contact.empty())
        if (const EntreeContact* x = b.contact(c.contact); x && (f->section < x->sectionMin - 1e-9 || f->section > x->sectionMax + 1e-9))
          r.push_back({"contact hors plage", ou,
                       "contact " + c.contact + " (" + fr(x->sectionMin) + " à " + fr(x->sectionMax) + " mm²) pour un fil de " + fr(f->section) + " mm²"});
      if (f && e && !e->contacts.empty() && !c.contact.empty() && std::find(e->contacts.begin(), e->contacts.end(), c.contact) == e->contacts.end())
        r.push_back({"contact hors plage", ou, "contact " + c.contact + " non prévu pour le boîtier " + e->reference});
      if (etanche && f && c.joint.empty()) r.push_back({"étanchéité", ou, "connecteur étanche : fil sans joint"});
      if (etanche && !f && c.bouchon.empty()) r.push_back({"étanchéité", ou, "connecteur étanche : cavité vide sans bouchon"});
    }
  }
  const auto occ = occupationTroncons(h, b);
  for (const auto& p : h.protections) {
    const EntreeProtection* e = b.protection(p.reference);
    if (!e) continue;
    const double d = diametreDuTroncon(occ, p.troncon);
    if (d > 0 && (d < e->diametreMin - 1e-9 || d > e->diametreMax + 1e-9))
      r.push_back({"protection hors plage", p.reference, e->genre + " pour " + fr(e->diametreMin, 1) + " à " + fr(e->diametreMax, 1) + " mm sur un faisceau de " + fr(d, 1) + " mm"});
  }
  for (const auto& c : circuits) {
    const BilanContinu bc = resoudreContinu(h, b, c);
    for (const auto& a : bc.alertes) r.push_back({"chute de tension", c.charge.nom.empty() ? std::string("circuit") : c.charge.nom, a});
  }
  return r;
}

// ======================================================================================================================
// Nomenclature, classeur
// ======================================================================================================================
std::vector<LigneNomenclatureCablage> nomenclatureCablage(const Cablage& h, const BibliothequeCablage& b) {
  std::vector<LigneNomenclatureCablage> r;
  std::map<std::string, std::size_t> index;
  auto ajouter = [&](const std::string& ref, const std::string& designation, const std::string& unite, double q, double prix, bool selonBesoin) {
    const auto it = index.find(ref);
    if (it != index.end()) {
      r[it->second].quantite += q;
      return;
    }
    index[ref] = r.size();
    r.push_back({0, ref, designation, unite, q, prix, selonBesoin});
  };
  // Fils seuls (cavaliers compris), puis câbles, à la longueur totale « selon besoin »
  for (const auto& f : h.fils) {
    if (!f.cable.empty()) continue;
    const EntreeCable* fil = nullptr;
    for (const auto& c : b.cables)
      if (c.nombreConducteurs == 1 && std::abs(c.section - f.section) < 1e-9) {
        fil = &c;
        break;
      }
    const std::string ref = (fil ? fil->designation : "Fil " + fr(f.section) + " mm²") + (f.couleur.empty() ? std::string() : " " + f.couleur);
    ajouter(ref, std::string(f.cavalier ? "cavalier, " : "") + "fil seul " + fr(f.section) + " mm² " + f.couleur, "m", f.longueur, fil ? fil->prixMetre : 0, true);
  }
  for (const auto& w : h.cables) {
    const EntreeCable* e = b.cable(w.designation);
    ajouter(w.designation, e ? e->description : std::string("câble"), "m", w.longueur, e ? e->prixMetre : 0, true);
    if (e)
      for (const auto& p : e->pieces) ajouter(p, "pièce associée à " + w.designation, "pce", 2, 0, false);
  }
  for (const auto& k : h.connecteurs) {
    if (!k.reference.empty())
      if (const EntreeConnecteurCablage* e = b.connecteur(k.reference)) ajouter(e->reference, e->description, "pce", 1, e->prix, false);
    for (const auto& c : k.cavites) {
      const FilCablage* f = filA(h, k.repere, c.numero);
      if (f && !c.contact.empty()) {
        const EntreeContact* e = b.contact(c.contact);
        ajouter(c.contact, e ? (e->genre == "embout" ? "embout de câblage " : "contact à sertir ") + e->fabricant : std::string("contact"), "pce", 1, e ? e->prix : 0, false);
      }
      if (f && !c.joint.empty() && c.joint != "joint intégré") ajouter(c.joint, "joint de fil", "pce", 1, 0, false);
      if (!f && !c.bouchon.empty()) ajouter(c.bouchon, "bouchon de cavité", "pce", 1, 0, false);
    }
  }
  const auto occ = occupationTroncons(h, b);
  for (const auto& p : h.protections) {
    const EntreeProtection* e = b.protection(p.reference);
    if (!e) continue;
    const double l = longueurProtection(h, p);
    if (e->genre == "ruban") ajouter(e->reference, e->description + " (spirale)", "m", longueurRuban(diametreDuTroncon(occ, p.troncon), l, e->largeur, e->recouvrement) / 1000, e->prixMetre, true);
    else if (e->genre == "gaine") ajouter(e->reference, e->description, "m", l / 1000, e->prixMetre, true);
    else ajouter(e->reference, e->description, "pce", 1, e->prix, false);
  }
  if (!h.epissures.empty()) ajouter("Épissure à sertir", "manchon d'épissure", "pce", static_cast<double>(h.epissures.size()), 0.15, false);
  for (std::size_t i = 0; i < r.size(); ++i) r[i].bulle = static_cast<int>(i + 1);
  return r;
}

std::string xlsxFaisceau(const Cablage& h, const BibliothequeCablage& b) {
  FeuilleClasseur nom{"Nomenclature", {"Bulle", "Référence", "Désignation", "Quantité", "Unité", "Selon besoin", "Prix unitaire (€)", "Montant (€)"}, {}};
  for (const auto& l : nomenclatureCablage(h, b))
    nom.lignes.push_back({l.bulle, l.reference, l.designation, std::round(l.quantite * 1000) / 1000, l.unite, l.selonBesoin ? "oui" : "", l.prixUnitaire, std::round(l.quantite * l.prixUnitaire * 100) / 100});
  FeuilleClasseur fils{"Fils", {"Fil", "De", "Vers", "Section (mm²)", "Couleur", "Câble", "Paire", "Réseau", "Longueur (m)", "Provenance", "Contact de", "Contact vers", "Dénudage de (mm)", "Dénudage vers (mm)"}, {}};
  FeuilleClasseur etiquettes{"Étiquettes", {"Fil", "Côté de", "Côté vers"}, {}};
  for (const auto& l : listeFils(h, b)) {
    fils.lignes.push_back({l.numero, l.de, l.vers, l.section, l.couleur, l.cable, l.paire ? CelluleClasseur(l.paire) : CelluleClasseur(""), l.reseau, std::round(l.longueur * 1000) / 1000, l.provenance,
                           l.contactDe, l.contactVers, l.denudageDe, l.denudageVers});
    etiquettes.lignes.push_back({l.numero, l.numero + " " + l.de, l.numero + " " + l.vers});
  }
  FeuilleClasseur gaines{"Gaines", {"Tronçon", "Référence", "Genre", "Début (mm)", "Fin (mm)", "Longueur (mm)", "Diamètre du faisceau (mm)", "Texte"}, {}};
  const auto occ = occupationTroncons(h, b);
  for (const auto& p : h.protections) {
    const EntreeProtection* e = b.protection(p.reference);
    std::string troncon = p.troncon;
    for (const auto& t : h.troncons)
      if (t.uuid == p.troncon) {
        std::string a = t.a, c = t.b;
        for (const auto& n : h.noeuds) {
          if (n.uuid == t.a) a = n.nom;
          if (n.uuid == t.b) c = n.nom;
        }
        troncon = a + "–" + c;
      }
    gaines.lignes.push_back({troncon, p.reference, e ? e->genre : std::string(), p.debut, p.fin, std::round(longueurProtection(h, p)), std::round(diametreDuTroncon(occ, p.troncon) * 10) / 10, p.texte});
    if (e && e->genre == "étiquette") etiquettes.lignes.push_back({std::string("—"), p.texte, troncon});
  }
  return xlsxClasseur({nom, fils, etiquettes, gaines});
}

// ======================================================================================================================
// Plan de fabrication
// ======================================================================================================================
std::string pdfPlanFaisceau(const Cablage& h0, const BibliothequeCablage& b, const std::string& titre, const std::string& revision) {
  Cablage h = h0;
  const PlancheDessinee d = dessinerPlanche(h, b, true);
  pdfv::Document p;
  const double W = 420, H = 297, m = 10, hc = 22;  // A3 à l'italienne, cadre, cartouche
  int page = 0, total = 0;
  const auto fils = listeFils(h, b);
  const auto nom = nomenclatureCablage(h, b);
  // Pages : planche, fils (40 par page), connexions (une table par connecteur), nomenclature
  const int pagesFils = std::max(1, static_cast<int>((fils.size() + 39) / 40));
  int lignesConnexions = 0;
  for (const auto& k : h.connecteurs) lignesConnexions += static_cast<int>(k.cavites.size()) + 3;
  const int pagesConnexions = std::max(1, (lignesConnexions + 44) / 45);
  total = 1 + pagesFils + pagesConnexions + 1;
  auto cadre = [&](const std::string& sousTitre) {
    p.page(W, H);
    ++page;
    p.couleur(0, 0, 0);
    p.epaisseur(0.5);
    p.rectangle(m, m, W - 2 * m, H - 2 * m);
    p.epaisseur(0.35);
    p.rectangle(m, H - m - hc, W - 2 * m, hc);
    p.ligne(W - m - 120, H - m - hc, W - m - 120, H - m);
    p.ligne(W - m - 40, H - m - hc, W - m - 40, H - m);
    p.texte(m + 4, H - m - hc + 9, titre, 6, true);
    p.texte(m + 4, H - m - hc + 17, "Plan de fabrication du faisceau « " + h.nom + " » — " + sousTitre, 3.5);
    p.texte(W - m - 116, H - m - hc + 9, "Longueurs en mm, tolérance ±1 % (au moins ±5 mm)", 2.8);
    p.texte(W - m - 116, H - m - hc + 15, "Fils : longueur de coupe, surlongueurs comprises", 2.8);
    p.texte(W - m - 36, H - m - hc + 8, "Révision " + revision, 3.2, true);
    p.texte(W - m - 36, H - m - hc + 16, "Feuille " + std::to_string(page) + " / " + std::to_string(total), 3.2);
  };
  auto tableau = [&](double x, double& y, const std::vector<std::string>& entetes, const std::vector<double>& largeurs, const std::vector<std::vector<std::string>>& lignes, double haut = 5) {
    auto rangee = [&](const std::vector<std::string>& v, bool gras) {
      double cx = x;
      for (std::size_t c = 0; c < largeurs.size(); ++c) {
        p.epaisseur(0.15);
        p.couleur(0.4, 0.4, 0.4);
        p.rectangle(cx, y, largeurs[c], haut);
        p.couleur(0, 0, 0);
        p.texte(cx + 1.2, y + haut - 1.4, c < v.size() ? v[c] : std::string(), 2.6, gras);
        cx += largeurs[c];
      }
      y += haut;
    };
    rangee(entetes, true);
    for (const auto& l : lignes) rangee(l, false);
  };
  // Feuille 1 : planche cotée, bulles des connecteurs et des protections
  cadre("planche cotée");
  const double zoneW = W - 2 * m - 10, zoneH = H - 2 * m - hc - 10;
  const double k = std::min({1.0, zoneW / std::max(1.0, d.largeur), zoneH / std::max(1.0, d.hauteur)});
  detail::plancheDansPdf(p, h, b, d, k, m + 5, m + 5);
  std::map<std::string, int> bulles;
  for (const auto& l : nom) bulles[l.reference] = l.bulle;
  auto bulle = [&](double x, double y, int n) {
    p.couleur(0, 0, 0);
    p.epaisseur(0.3);
    p.cercle(x, y, 3.2);
    p.texte(x, y + 1.2, std::to_string(n), 3, true, 0, 0);
  };
  for (const auto& n : d.noeuds)
    if (const ConnecteurCablage* c = h.connecteur(n.connecteur); c && bulles.count(c->reference)) bulle(m + 5 + k * (n.x + 12), m + 5 + k * (n.y - 9), bulles[c->reference]);
  for (const auto& pr : h.protections)
    for (const auto& t : d.troncons)
      if (t.uuid == pr.troncon && bulles.count(pr.reference)) bulle(m + 5 + k * ((t.x0 + t.x1) / 2), m + 5 + k * ((t.y0 + t.y1) / 2) - 8, bulles[pr.reference]);
  if (k < 1) p.texte(m + 5, H - m - hc - 3, "Réduite à " + fr(k * 100, 1) + " % : la planche à l'échelle 1 sort en pages assemblées (PDF de la planche)", 2.8);
  // Liste des fils
  for (int pf = 0; pf < pagesFils; ++pf) {
    cadre("liste des fils");
    double y = m + 8;
    std::vector<std::vector<std::string>> lignes;
    for (std::size_t i = static_cast<std::size_t>(pf) * 40; i < fils.size() && i < static_cast<std::size_t>(pf + 1) * 40; ++i) {
      const auto& l = fils[i];
      lignes.push_back({l.numero, l.de, l.vers, fr(l.section), l.couleur, l.cable, l.paire ? std::to_string(l.paire) : std::string(), fr(l.longueur * 1000, 0), l.provenance, l.contactDe, l.contactVers,
                        fr(l.denudageDe, 1) + " / " + fr(l.denudageVers, 1)});
    }
    tableau(m + 5, y, {"Fil", "De", "Vers", "mm²", "Couleur", "Câble", "Paire", "Longueur", "Provenance", "Contact de", "Contact vers", "Dénudage"}, {16, 30, 30, 16, 22, 18, 14, 22, 24, 40, 40, 26}, lignes);
  }
  // Tables de connexion
  {
    cadre("tables de connexion");
    double y = m + 8, x = m + 5;
    int page2 = 1;
    for (const auto& c : h.connecteurs) {
      if (y + 5.0 * static_cast<double>(c.cavites.size() + 2) > H - m - hc - 4) {
        if (x < W / 2) {
          x = W / 2 + 5;
          y = m + 8;
        } else if (page2 < pagesConnexions) {
          cadre("tables de connexion (suite)");
          ++page2;
          x = m + 5;
          y = m + 8;
        }
      }
      p.texte(x, y + 3.5, c.repere + (c.reference.empty() ? std::string() : " — " + c.reference) + (c.carte ? " (connecteur de carte)" : ""), 3.4, true);
      y += 5;
      std::vector<std::vector<std::string>> lignes;
      for (const auto& cv : c.cavites) {
        const FilCablage* f = filA(h, c.repere, cv.numero);
        lignes.push_back({cv.numero, f ? f->numero : std::string("—"), f ? f->couleur : std::string(), f ? fr(f->section) : std::string(), f ? f->reseau : cv.reseau, cv.contact,
                          !cv.joint.empty() ? cv.joint : cv.bouchon});
      }
      tableau(x, y, {"Cavité", "Fil", "Couleur", "mm²", "Réseau", "Contact", "Joint, bouchon"}, {14, 14, 20, 14, 36, 50, 40}, lignes);
      y += 5;
    }
    while (page2 < pagesConnexions) {
      cadre("tables de connexion (suite)");
      ++page2;
    }
  }
  // Nomenclature avec bulles
  {
    cadre("nomenclature");
    double y = m + 8;
    std::vector<std::vector<std::string>> lignes;
    for (const auto& l : nom)
      lignes.push_back({std::to_string(l.bulle), l.reference, l.designation, l.selonBesoin ? "selon besoin (" + fr(l.quantite) + " " + l.unite + ")" : fr(l.quantite, 0) + " " + l.unite});
    tableau(m + 5, y, {"Bulle", "Référence", "Désignation", "Quantité"}, {16, 70, 170, 80}, lignes, 5.5);
  }
  return p.terminer(titre + " — plan du faisceau " + h.nom);
}

// ======================================================================================================================
// Machines et testeurs
// ======================================================================================================================
const std::vector<GabaritMachine>& gabaritsMachines() {
  static const std::vector<GabaritMachine> g = {
      {"Komax (liste de coupe et de sertissage)", "coupe", "csv", "Fil;Longueur (mm);Section (mm²);Couleur;Dénudage A (mm);Dénudage B (mm);Contact A;Contact B;Quantité\n",
       "{numero};{longueur_mm};{section};{couleur};{denudage_de};{denudage_vers};{contact_de};{contact_vers};1\n", ""},
      {"Schleuniger (liste de fils)", "sertissage", "txt", "# Schleuniger — liste de fils\n",
       "WIRE={numero}\tLEN={longueur_mm}\tCSA={section}\tCOL={couleur}\tSTRIP1={denudage_de}\tSTRIP2={denudage_vers}\tTERM1={contact_de}\tTERM2={contact_vers}\n", "# fin\n"},
      {"Étiquettes de fils", "étiquettes", "csv", "Fil;Côté A;Côté B\n", "{numero};{numero} {de};{numero} {vers}\n", ""},
      {"Cirris (programme de continuité)", "testeur", "txt", "; Programme Cirris — essai de continuité\nNETLIST\n", "CONNECT {connecteur_de}-{cavite_de} {connecteur_vers}-{cavite_vers} ; fil {numero}\n", "END\n"},
      {"DIT-MCO (liste de connexions)", "testeur", "txt", "*DIT-MCO WIRE LIST\n", "W {numero} {connecteur_de}.{cavite_de} {connecteur_vers}.{cavite_vers} {section}\n", "*END\n"}};
  return g;
}

std::optional<GabaritMachine> lireGabaritMachine(const std::string& json, std::string* erreur) {
  try {
    const Json j = Json::parse(json);
    GabaritMachine g{j.value("nom", std::string()), j.value("genre", std::string()), j.value("extension", std::string("csv")), j.value("entete", std::string()), j.value("ligne", std::string()),
                     j.value("pied", std::string())};
    if (g.nom.empty() || g.ligne.empty()) throw std::runtime_error("nom ou ligne du gabarit manquant");
    return g;
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return std::nullopt;
  }
}

std::string texteGabaritMachine(const GabaritMachine& g) {
  return Json{{"format", "promethee-gabarit-machine"}, {"nom", g.nom}, {"genre", g.genre}, {"extension", g.extension}, {"entete", g.entete}, {"ligne", g.ligne}, {"pied", g.pied}}.dump(2) + "\n";
}

std::string fichierMachine(const Cablage& h, const BibliothequeCablage& b, const GabaritMachine& g) {
  std::string r = g.entete;
  for (const auto& l : listeFils(h, b)) {
    const FilCablage* f = h.fil(l.numero);
    if (!f) continue;
    if (g.genre == "testeur" && (f->de.empty() || f->vers.empty())) continue;  // un essai de continuité veut deux bouts
    const std::map<std::string, std::string> champs = {{"numero", l.numero},
                                                       {"section", fr(l.section)},
                                                       {"couleur", l.couleur},
                                                       {"longueur_mm", fr(l.longueur * 1000, 0)},
                                                       {"denudage_de", fr(l.denudageDe, 1)},
                                                       {"denudage_vers", fr(l.denudageVers, 1)},
                                                       {"contact_de", l.contactDe},
                                                       {"contact_vers", l.contactVers},
                                                       {"de", l.de},
                                                       {"vers", l.vers},
                                                       {"cavite_de", f->deCavite},
                                                       {"cavite_vers", f->versCavite},
                                                       {"connecteur_de", f->de},
                                                       {"connecteur_vers", f->vers},
                                                       {"cable", l.cable},
                                                       {"reseau", l.reseau}};
    std::string ligne;
    for (std::size_t i = 0; i < g.ligne.size(); ++i) {
      if (g.ligne[i] == '{') {
        const std::size_t j = g.ligne.find('}', i);
        if (j != std::string::npos)
          if (const auto it = champs.find(g.ligne.substr(i + 1, j - i - 1)); it != champs.end()) {
            ligne += it->second;
            i = j;
            continue;
          }
      }
      ligne += g.ligne[i];
    }
    r += ligne;
  }
  return r + g.pied;
}

// ======================================================================================================================
// Devis
// ======================================================================================================================
std::optional<TempsUnitaires> lireTempsUnitaires(const std::string& json, std::string* erreur) {
  try {
    const Json j = Json::parse(json);
    TempsUnitaires t;
    t.coupe = j.value("coupe", t.coupe);
    t.sertissage = j.value("sertissage", t.sertissage);
    t.gaine = j.value("gaine", t.gaine);
    t.controle = j.value("controle", t.controle);
    t.etiquette = j.value("etiquette", t.etiquette);
    t.tauxHoraire = j.value("tauxHoraire", t.tauxHoraire);
    return t;
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return std::nullopt;
  }
}

DevisFaisceau devisFaisceau(const Cablage& h, const BibliothequeCablage& b, const TempsUnitaires& t, const std::map<std::string, double>& prix) {
  DevisFaisceau d;
  d.fils = static_cast<int>(h.fils.size());
  for (const auto& f : h.fils) {
    for (const auto& [k, c] : {std::make_pair(f.de, f.deCavite), std::make_pair(f.vers, f.versCavite)}) {
      if (k.empty() || c.empty()) continue;
      const ConnecteurCablage* x = h.connecteur(k);
      const CaviteCablage* y = x ? x->cavite(c) : nullptr;
      if (y && !y->contact.empty()) ++d.sertissages;
    }
    if (!f.de.empty() && !f.vers.empty()) ++d.connexions;
  }
  for (const auto& p : h.protections) {
    const EntreeProtection* e = b.protection(p.reference);
    if (e && e->genre == "étiquette") ++d.etiquettes;
    else ++d.protections;
  }
  d.detailTemps = {{"coupe et dénudage", d.fils * t.coupe},
                   {"sertissage", d.sertissages * t.sertissage},
                   {"pose des gaines", d.protections * t.gaine},
                   {"étiquettes", d.etiquettes * t.etiquette},
                   {"contrôle", d.connexions * t.controle}};
  for (const auto& [op, s] : d.detailTemps) d.secondes += s;
  d.mainOeuvre = d.secondes / 3600 * t.tauxHoraire;
  d.lignes = nomenclatureCablage(h, b);
  for (auto& l : d.lignes) {
    if (const auto it = prix.find(l.reference); it != prix.end()) l.prixUnitaire = it->second;
    d.materiel += l.quantite * l.prixUnitaire;
  }
  d.total = d.mainOeuvre + d.materiel;
  return d;
}

std::string csvDevis(const DevisFaisceau& d) {
  std::vector<std::vector<std::string>> lignes;
  for (const auto& l : d.lignes) lignes.push_back({"matériel", l.reference, l.designation, fr(l.quantite, 3), l.unite, fr(l.prixUnitaire, 4), fr(l.quantite * l.prixUnitaire)});
  for (const auto& [op, s] : d.detailTemps) lignes.push_back({"main-d'œuvre", op, "", fr(s, 0), "s", "", ""});
  lignes.push_back({"total", "matériel", "", "", "", "", fr(d.materiel)});
  lignes.push_back({"total", "main-d'œuvre", fr(d.secondes / 3600, 3) + " h", "", "", "", fr(d.mainOeuvre)});
  lignes.push_back({"total", "faisceau", "", "", "", "", fr(d.total)});
  return detail::documentCsv({"Poste", "Référence", "Désignation", "Quantité", "Unité", "Prix unitaire (€ HT)", "Montant (€ HT)"}, lignes);
}

std::string pdfDevis(const DevisFaisceau& d, const std::string& titre) {
  ecad::DocumentPdf doc;
  doc.meta.titre = "Devis — " + titre;
  doc.meta.sujet = "Chiffrage du faisceau";
  doc.pied = "Devis du faisceau " + titre;
  using G = ecad::BlocPdf::Genre;
  doc.blocs.push_back({G::Titre, "Devis du faisceau " + titre, {}, {}, false});
  doc.blocs.push_back({G::Paragraphe,
                       std::to_string(d.fils) + " fils, " + std::to_string(d.sertissages) + " sertissages, " + std::to_string(d.protections) + " protections, " + std::to_string(d.etiquettes) +
                           " étiquettes, " + std::to_string(d.connexions) + " connexions contrôlées.",
                       {},
                       {},
                       false});
  ecad::BlocPdf mat{G::Tableau, {}, {{"Bulle", "Référence", "Désignation", "Quantité", "Prix unitaire", "Montant"}}, {0.6, 2, 3, 1.2, 1.1, 1.1}, false};
  for (const auto& l : d.lignes)
    mat.lignes.push_back({std::to_string(l.bulle), l.reference, l.designation, fr(l.quantite, 3) + " " + l.unite, fr(l.prixUnitaire, 4) + " €", fr(l.quantite * l.prixUnitaire) + " €"});
  doc.blocs.push_back({G::SousTitre, "Matériel", {}, {}, false});
  doc.blocs.push_back(mat);
  ecad::BlocPdf mo{G::Tableau, {}, {{"Opération", "Temps (s)"}}, {3, 1}, false};
  for (const auto& [op, s] : d.detailTemps) mo.lignes.push_back({op, fr(s, 0)});
  doc.blocs.push_back({G::SousTitre, "Main-d'œuvre", {}, {}, false});
  doc.blocs.push_back(mo);
  doc.blocs.push_back({G::Paragraphe,
                       "Matériel : " + fr(d.materiel) + " € HT. Main-d'œuvre : " + fr(d.secondes / 3600, 3) + " h, " + fr(d.mainOeuvre) + " € HT. Total : " + fr(d.total) + " € HT.",
                       {},
                       {},
                       true});
  return ecad::pdfDocument(doc);
}

// ======================================================================================================================
// Folio de câblage (19.10)
// ======================================================================================================================
DispositionCablage disposerFolioCablage(const Cablage& h, const BibliothequeCablage& b, double largeur, double hauteur) {
  DispositionCablage r;
  const double pas = 5, entete = 7, boite = 34, marge = 12;
  // Connecteurs de carte à gauche, les autres à droite ; empilés de haut en bas
  std::vector<const ConnecteurCablage*> gauche, droite;
  for (const auto& k : h.connecteurs) (k.carte ? gauche : droite).push_back(&k);
  if (gauche.empty() && droite.size() > 1) gauche.push_back(droite.front()), droite.erase(droite.begin());
  std::map<std::string, std::pair<double, double>> ancre;  // « J3:1 » → point d'attache du fil
  std::map<std::string, bool> aDroite;
  auto poser = [&](const std::vector<const ConnecteurCablage*>& v, bool cote) {
    double y = marge;
    for (const ConnecteurCablage* k : v) {
      DispositionCablage::Boite bx;
      bx.repere = k->repere;
      bx.titre = k->repere + (k->reference.empty() ? std::string() : " " + k->reference);
      bx.largeur = boite;
      bx.hauteur = entete + pas * static_cast<double>(std::max<std::size_t>(1, k->cavites.size())) + 2;
      bx.x = cote ? largeur - marge - boite : marge;
      bx.y = y;
      bx.droite = !cote;
      for (std::size_t i = 0; i < k->cavites.size(); ++i) {
        const double cy = y + entete + pas * (static_cast<double>(i) + 0.5);
        bx.cavites.push_back({k->cavites[i].numero, cy});
        ancre[k->repere + ":" + k->cavites[i].numero] = {cote ? bx.x : bx.x + boite, cy};
      }
      aDroite[k->repere] = cote;
      r.boites.push_back(bx);
      y += bx.hauteur + 10;
    }
  };
  poser(gauche, false);
  poser(droite, true);
  std::map<std::string, std::string> epissureDe;
  for (const auto& e : h.epissures)
    for (const auto& f : e.fils) epissureDe[f] = e.repere;
  std::map<std::string, std::pair<double, double>> lieuEpissure;
  const double milieu = largeur / 2;
  std::map<std::string, std::vector<std::size_t>> traitsDuCable;
  for (const auto& f : h.fils) {
    DispositionCablage::Trait t;
    t.numero = f.numero;
    t.couleur = f.couleur;
    t.paire = f.paire;
    t.drain = f.drain;
    t.texte = f.numero + "  " + fr(f.section) + " mm²" + (f.couleur.empty() ? std::string() : " " + f.couleur) + (f.reseau.empty() ? std::string() : "  " + f.reseau);
    const std::string a = f.de + ":" + f.deCavite, c = f.vers + ":" + f.versCavite;
    const bool aOk = ancre.count(a) > 0, cOk = ancre.count(c) > 0;
    if (aOk && cOk) {
      t.x0 = ancre[a].first, t.y0 = ancre[a].second, t.x1 = ancre[c].first, t.y1 = ancre[c].second;
      if (f.de == f.vers) t.x1 = t.x0 + (aDroite[f.de] ? -12 : 12);  // cavalier : boucle courte
    } else if (aOk || cOk) {
      const auto p = aOk ? ancre[a] : ancre[c];
      t.x0 = p.first, t.y0 = p.second;
      // Bout libre : vers l'épissure (au milieu, à la hauteur du fil porteur), sinon en l'air
      t.x1 = milieu, t.y1 = p.second;
      if (epissureDe.count(f.numero)) {
        const std::string& e = epissureDe[f.numero];
        if (!lieuEpissure.count(e)) lieuEpissure[e] = {milieu, p.second};
        t.x1 = lieuEpissure[e].first, t.y1 = lieuEpissure[e].second;
      } else if (f.drain)
        t.x1 = (p.first + milieu) / 2;
    } else
      continue;
    if (!f.cable.empty()) traitsDuCable[f.cable].push_back(r.traits.size());
    r.traits.push_back(t);
  }
  for (const auto& [e, p] : lieuEpissure) r.epissures.push_back({e, p});
  for (const auto& w : h.cables) {
    const auto it = traitsDuCable.find(w.uuid);
    if (it == traitsDuCable.end()) continue;
    double y0 = 1e9, y1 = -1e9;
    for (const std::size_t i : it->second) {
      const auto& t = r.traits[i];
      const double y = t.y0 + (t.y1 - t.y0) * 0.5;
      y0 = std::min(y0, y), y1 = std::max(y1, y);
    }
    const EntreeCable* e = b.cable(w.designation);
    DispositionCablage::Cadre c;
    c.reference = w.reference;
    c.texte = w.reference + " " + w.designation + (w.longueur > 0 ? " — " + fr(w.longueur) + " m" : std::string(" — à mesurer"));
    c.x = milieu;
    c.y0 = y0 - 3;
    c.y1 = y1 + 3;
    c.blinde = e && e->blinde;
    r.cables.push_back(c);
  }
  (void)hauteur;
  return r;
}

}  // namespace prom::elec

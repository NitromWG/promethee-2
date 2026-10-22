// Prométhée : documents de l'installation calculée — note de calcul (CSV, HTML) et schéma unifilaire tiré du
// calcul (un folio par tableau, protections, canalisations et récepteurs annotés). Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "electrique_interne.hpp"
#include "promethee/electrique_distribution.hpp"

namespace prom::elec {

using detail::nombreFr;

namespace {

const char* ORIGINE = "distribution";

std::string echapper(const std::string& s) {
  std::string r;
  r.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': r += "&amp;"; break;
      case '<': r += "&lt;"; break;
      case '>': r += "&gt;"; break;
      case '"': r += "&quot;"; break;
      default: r += c;
    }
  }
  return r;
}
std::string n2(double v) { return nombreFr(v, 2); }
std::string n1(double v) { return nombreFr(v, 1); }
std::string kA(double v) { return nombreFr(v, v < 10 ? 2 : 1); }
const char* niveau(NiveauAvis n) {
  switch (n) {
    case NiveauAvis::Erreur: return "Erreur";
    case NiveauAvis::Alerte: return "Alerte";
    case NiveauAvis::Info: break;
  }
  return "Info";
}
std::string protectionCourte(const DepartBT& d, const ResultatDepartBT& x) {
  std::string t;
  switch (x.protection) {
    case TypeProtection::Modulaire: t = (d.courbe == "B" || d.courbe == "D" ? d.courbe : std::string("C")) + " " + nombreFr(x.in) + " A"; break;
    case TypeProtection::Reglable: t = nombreFr(x.in) + " A Ir " + nombreFr(x.ir, 0) + " A"; break;
    case TypeProtection::Moteur: t = "Moteur " + nombreFr(x.ir, 2) + " A"; break;
    case TypeProtection::Fusible: t = "gG " + nombreFr(x.in) + " A"; break;
    case TypeProtection::Auto: break;
  }
  t += " " + kA(x.pdc) + " kA";
  if (d.differentiel > 0) t += " " + nombreFr(d.differentiel) + " mA" + (d.differentielSelectif ? " S" : "");
  return t;
}
std::string etat(const ResultatDepartBT& x) {
  if (x.erreurs() > 0) return "à corriger";
  if (x.alertes() > 0) return "à surveiller";
  return "conforme";
}

}  // namespace

// ---------- Note de calcul (CSV) ----------
std::string csvNoteDeCalcul(const ReseauBT& r, const ResultatReseauBT& x) {
  std::map<std::string, std::string> nomParent;
  for (const auto& d : r.departs) nomParent[d.uuid] = d.repere;
  std::vector<std::vector<std::string>> lignes;
  for (const auto& [i, prof] : ordreArbre(r)) {
    const DepartBT& d = r.departs[i];
    const ResultatDepartBT& y = x.departs[i];
    std::string avis;
    for (const auto& a : y.avis) avis += (avis.empty() ? "" : " | ") + std::string(niveau(a.niveau)) + " (" + a.regle + ") : " + a.message;
    lignes.push_back({d.repere,
                      std::string(static_cast<std::size_t>(prof) * 2, ' ') + d.nom,
                      d.parent.empty() ? std::string("Source") : nomParent[d.parent],
                      nomTypeCharge(d.charge),
                      nomPolarite(d.polarite),
                      n2(y.p),
                      n2(y.q),
                      n2(y.cosPhi),
                      n1(y.ib),
                      y.designationProtection,
                      nombreFr(y.in),
                      nombreFr(y.ir, 1),
                      kA(y.pdc),
                      kA(y.pdcRequis),
                      y.designationCable,
                      nombreFr(d.longueur),
                      nomMethodePose(d.pose),
                      nombreFr(y.section),
                      nombreFr(y.sectionNeutre),
                      nombreFr(y.sectionPE),
                      std::to_string(y.parallele),
                      n2(y.k),
                      n1(y.iz),
                      n2(y.chute),
                      n2(y.chuteCumulee),
                      n2(y.chuteLimite),
                      y.chuteDemarrage > 0 ? n2(y.chuteDemarrage) : std::string(),
                      kA(y.ik3max),
                      kA(y.ikMin),
                      kA(y.ief),
                      nombreFr(y.ia, 0),
                      y.longueurMax > 0 ? nombreFr(y.longueurMax, 0) : std::string(),
                      y.protectionPersonnes,
                      y.selectivite,
                      etat(y),
                      avis});
  }
  return detail::documentCsv({"Repère",
                              "Désignation",
                              "Tableau amont",
                              "Type",
                              "Polarité",
                              "P (kW)",
                              "Q (kvar)",
                              "cos φ",
                              "Ib (A)",
                              "Protection",
                              "In (A)",
                              "Ir (A)",
                              "PdC (kA)",
                              "Icc au point de pose (kA)",
                              "Canalisation",
                              "Longueur (m)",
                              "Pose",
                              "Section phase (mm²)",
                              "Section neutre (mm²)",
                              "Section PE (mm²)",
                              "Câbles par phase",
                              "K",
                              "Iz (A)",
                              "ΔU (%)",
                              "ΔU cumulée (%)",
                              "ΔU admise (%)",
                              "ΔU au démarrage (%)",
                              "Ik3 max en bout (kA)",
                              "Ik min en bout (kA)",
                              "Ief (kA)",
                              "Ia (A)",
                              "Longueur maximale (m)",
                              "Protection des personnes",
                              "Sélectivité",
                              "État",
                              "Avis"},
                             lignes);
}

// ---------- Note de calcul (HTML) ----------
std::string htmlNoteDeCalcul(const ReseauBT& r, const ResultatReseauBT& x, const std::string& projet) {
  const SourceBT& s = r.source;
  std::string h;
  h += "<html><head><meta charset=\"utf-8\"><title>Note de calcul</title></head><body style=\"font-family:sans-serif; font-size:9pt; color:#10142A\">";
  h += "<h1 style=\"font-size:16pt\">Note de calcul — " + echapper(r.nom) + "</h1>";
  if (!projet.empty()) h += "<p>Projet : <b>" + echapper(projet) + "</b></p>";
  // Bilan
  h += "<p style=\"font-size:11pt\"><b>" + std::to_string(r.departs.size()) + " départs</b> · <b style=\"color:" + std::string(x.erreurs ? "#C2402E" : "#2E7D32") + "\">" +
       std::to_string(x.erreurs) + " erreur(s)</b> · " + std::to_string(x.alertes) + " alerte(s)</p>";
  // Source
  h += "<h2 style=\"font-size:12pt\">Source</h2><table border=\"1\" cellspacing=\"0\" cellpadding=\"3\">";
  auto ligne = [&h](const std::string& a, const std::string& b) { h += "<tr><td>" + echapper(a) + "</td><td><b>" + echapper(b) + "</b></td></tr>"; };
  ligne("Type", nomTypeSource(s.type));
  switch (s.type) {
    case TypeSourceBT::Transformateur:
      ligne("Puissance", nombreFr(s.puissance) + " kVA, ucc " + nombreFr(s.ucc) + " %" + (s.pertesCuivre > 0 ? ", pertes dues à la charge " + nombreFr(s.pertesCuivre) + " W" : ""));
      ligne("Réseau amont", "Scc " + nombreFr(s.sccAmont) + " MVA");
      break;
    case TypeSourceBT::ReseauPublic:
      ligne("Puissance de raccordement", nombreFr(s.puissance) + " kVA");
      ligne("Réseau amont", "réseau conventionnel de la NF C 14-100 (transformateur 1 000 kVA, ucc 6 %, liaisons L1 et L2 en aluminium)");
      break;
    case TypeSourceBT::BranchementLimite:
      ligne("Puissance souscrite", nombreFr(s.puissance) + " kVA, " + (s.triphase ? "triphasé" : "monophasé"));
      ligne("Icc au point de livraison", nombreFr(s.icc) + " kA (donnée du distributeur)");
      break;
    case TypeSourceBT::Groupe:
      ligne("Puissance", nombreFr(s.puissance) + " kVA, x''d " + nombreFr(s.xdSub) + " %, x'd " + nombreFr(s.xdTrans) + " %, x0 " + nombreFr(s.x0) + " %");
      break;
  }
  ligne("Tension", nombreFr(s.tension) + " V entre phases, " + n1(s.tension / std::sqrt(3.0)) + " V simple");
  ligne("Schéma des liaisons à la terre", std::string(nomRegime(s.regime)) + (s.regime == RegimeNeutre::TT ? ", RA = " + nombreFr(s.resistanceTerre) + " Ω" : ""));
  ligne("Impédance de la source (par phase)", "R " + n2(x.rSource) + " mΩ, X " + n2(x.xSource) + " mΩ");
  ligne("Courant de court-circuit à l'origine", kA(x.ik3Source) + " kA");
  ligne("Appel de puissance", n1(x.p) + " kW, " + n1(x.q) + " kvar, " + n1(x.s) + " kVA, cos φ " + n2(x.cosPhi) + ", " + n1(x.ib) + " A");
  if (s.puissance > 0) ligne("Taux de charge de la source", nombreFr(x.tauxCharge, 0) + " %");
  if (x.compensation > 0) ligne("Compensation conseillée", nombreFr(x.compensation) + " kvar (cos φ visé " + n2(r.cosPhiCible) + ")");
  if (x.transformateurConseille > 0) ligne("Transformateur conseillé (25 % de réserve)", nombreFr(x.transformateurConseille) + " kVA");
  h += "</table>";
  if (!x.avis.empty()) {
    h += "<ul>";
    for (const auto& a : x.avis) h += "<li><b>" + std::string(niveau(a.niveau)) + "</b> (" + echapper(a.regle) + ") : " + echapper(a.message) + "</li>";
    h += "</ul>";
  }
  // Tableau des départs
  h += "<h2 style=\"font-size:12pt\">Départs</h2><table width=\"100%\" border=\"1\" cellspacing=\"0\" cellpadding=\"3\" style=\"font-size:8pt\">";
  h += "<tr style=\"background-color:#E8ECF6\"><th>Repère</th><th>Désignation</th><th>P (kW)</th><th>Ib (A)</th><th>Protection</th><th>Canalisation</th><th>L (m)</th><th>Iz (A)</th>"
       "<th>ΔU cumulée</th><th>Icc max (kA)</th><th>Icc min (kA)</th><th>État</th></tr>";
  for (const auto& [i, prof] : ordreArbre(r)) {
    const DepartBT& d = r.departs[i];
    const ResultatDepartBT& y = x.departs[i];
    const std::string couleur = y.erreurs() ? "#C2402E" : y.alertes() ? "#A8620E" : "#2E7D32";
    std::string retrait;
    for (int k = 0; k < prof; ++k) retrait += "&nbsp;&nbsp;&nbsp;";
    h += "<tr><td>" + echapper(d.repere) + "</td><td>" + retrait + echapper(d.nom) + "</td><td align=\"right\">" + n1(y.p) + "</td><td align=\"right\">" + n1(y.ib) +
         "</td><td>" + echapper(protectionCourte(d, y)) + "</td><td>" + echapper(y.designationCable) + "</td><td align=\"right\">" + nombreFr(d.longueur) +
         "</td><td align=\"right\">" + n1(y.iz) + "</td><td align=\"right\">" + n2(y.chuteCumulee) + " %</td><td align=\"right\">" + kA(y.ik3max) +
         "</td><td align=\"right\">" + kA(y.ikMin) + "</td><td style=\"color:" + couleur + "\"><b>" + etat(y) + "</b></td></tr>";
  }
  h += "</table>";
  // Détail par départ
  h += "<h2 style=\"font-size:12pt\">Justifications</h2>";
  for (const auto& [i, prof] : ordreArbre(r)) {
    (void)prof;
    const DepartBT& d = r.departs[i];
    const ResultatDepartBT& y = x.departs[i];
    h += "<h3 style=\"font-size:10pt\">" + echapper(d.repere) + " — " + echapper(d.nom) + "</h3><p>";
    h += "Charge : " + std::string(nomTypeCharge(d.charge)) + ", " + nomPolarite(d.polarite) + ", P " + n2(y.p) + " kW, Q " + n2(y.q) + " kvar, cos φ " + n2(y.cosPhi) +
         " ; courant d'emploi <b>Ib = " + n1(y.ib) + " A</b>" + (y.iNeutre > 0 ? " (neutre " + n1(y.iNeutre) + " A)" : "") + ".<br>";
    h += "Protection : <b>" + echapper(y.designationProtection) + "</b> ; Ir " + nombreFr(y.ir, 1) + " A ≥ Ib ; pouvoir de coupure " + kA(y.pdc) + " kA pour " +
         kA(y.pdcRequis) + " kA au point de pose.<br>";
    h += "Canalisation : <b>" + echapper(y.designationCable) + "</b>, " + nombreFr(d.longueur) + " m, pose " + echapper(nomMethodePose(d.pose)) + ", " +
         nombreFr(d.temperature) + " °C, " + std::to_string(std::max(1, d.circuitsJointifs)) + " circuit(s) groupé(s) ; K = " + n2(y.k) + ", Iz = " + n1(y.iz) +
         " A ≥ " + n1(y.courantDimensionnement) + " A.<br>";
    h += "Chute de tension : " + n2(y.chute) + " % sur la ligne, " + n2(y.chuteCumulee) + " % depuis l'origine (admise " + n2(y.chuteLimite) + " %)" +
         (y.chuteDemarrage > 0 ? ", " + n2(y.chuteDemarrage) + " % au démarrage" : std::string()) + ".<br>";
    h += "Courts-circuits en bout de ligne : Ik3 max " + kA(y.ik3max) + " kA" + (y.ik1max > 0 ? ", Ik1 max " + kA(y.ik1max) + " kA" : std::string()) + ", Ik min " +
         kA(y.ikMin) + " kA (déclenchement instantané " + nombreFr(y.imHaut, 0) + " A) ; défaut à la masse " + kA(y.ief) + " kA.<br>";
    h += "Protection des personnes : " + echapper(y.protectionPersonnes) + (y.longueurMax > 0 ? " ; longueur maximale " + nombreFr(y.longueurMax, 0) + " m" : std::string()) +
         ".<br>";
    if (y.i2tLaisse > 0)
      h += "Contrainte thermique : I²t " + nombreFr(y.i2tLaisse / 1e6, 3) + " MA²s, k²S² " + nombreFr(y.i2tAdmissible / 1e6, 3) + " MA²s.<br>";
    h += "Sélectivité avec l'amont : " + echapper(y.selectivite) + ".</p>";
    if (!y.avis.empty()) {
      h += "<ul>";
      for (const auto& a : y.avis) h += "<li><b>" + std::string(niveau(a.niveau)) + "</b> (" + echapper(a.regle) + ") : " + echapper(a.message) + "</li>";
      h += "</ul>";
    }
  }
  // Hypothèses
  h += "<h2 style=\"font-size:12pt\">Méthodes et hypothèses</h2><p>"
       "Courants de court-circuit par la méthode des impédances (guide UTE C 15-105) : facteur de tension c = 1,05 (maximal) et 0,95 (minimal), facteur de charge "
       "m = 1,05 ; résistivité du cuivre 18,51 mΩ·mm²/m à 20 °C (aluminium 29,41), multipliée par 1,25 pour les courants minimaux coupés par disjoncteur et 1,5 "
       "par fusible ; réactance 0,08 mΩ/m (0,09 pour des câbles unipolaires). Réseau amont : Ra = 0,1 Xa, Xa = 0,995 Za ; transformateur sans données : "
       "R = 0,31 Z, X = 0,95 Z.<br>"
       "Courants admissibles de la NF C 15-100 (CEI 60364-5-52, annexe B), corrigés de la température, du groupement et des harmoniques ; aluminium : 0,78 du "
       "cuivre. Chutes de tension selon l'annexe G (ρ = 22,5 mΩ·mm²/m en service) ; limites " +
       std::string(r.source.type == TypeSourceBT::Transformateur || r.source.type == TypeSourceBT::Groupe ? "6 % (éclairage) et 8 % (autres usages) depuis un poste privé"
                                                                                                              : "3 % (éclairage) et 5 % (autres usages) sur le réseau public") +
       ".<br>"
       "Protection des personnes : courant de défaut minimal comparé à Ia (seuil instantané haut, 1,2 Isd pour un déclencheur réglable, portes de fusion des "
       "fusibles gG) ; longueurs maximales par la méthode conventionnelle (80 % de U0). Contraintes thermiques : classes de limitation 3 de la NF EN 60898-1 "
       "pour les disjoncteurs modulaires de 32 A au plus, coupure en 10 ms sinon (à confirmer par les courbes du constructeur).<br>"
       "Sélectivité ampèremétrique (rapport des réglages ≥ 1,6, seuils instantanés) ; la sélectivité énergétique et la filiation se lisent dans les tables des "
       "constructeurs.</p>";
  h += "<p style=\"color:#6B7393\">Note établie par Prométhée. Elle aide à concevoir et ne remplace pas la vérification de l'installateur ni le contrôle "
       "de l'installation.</p></body></html>";
  return h;
}

// ---------- Schéma unifilaire ----------
std::size_t genererUnifilaire(ProjetElectrique& p) {
  if (!p.distribution) return 0;
  const ReseauBT& r = *p.distribution;
  const ResultatReseauBT x = calculerReseau(r);
  // Les folios tirés d'un calcul précédent partent (avec leurs appareils), les nouveaux prennent leur place
  const std::size_t position = detail::retirerFoliosCalcules(p, ORIGINE);
  const std::size_t premier = position;
  const auto P = [](double gx, double gy) { return ecad::ptMm(gx * 2.54, gy * 2.54); };
  auto texte = [&](std::size_t f, double gx, double gy, const std::string& s, double taille, int rotation = 0) {
    ecad::TexteSchema t;
    t.uuid = ecad::nouvelUuid();
    t.texte = s;
    t.p = P(gx, gy);
    t.taille = ecad::nm(taille);
    t.rotation = rotation;
    p.schema.feuilles[f].textes.push_back(t);
  };
  // Texte vertical écrit de bas en haut, dont le haut arrive en « haut » (unités de grille)
  auto texteVertical = [&](std::size_t f, double gx, double haut, const std::string& s, double taille) {
    double l = 0;
    for (std::size_t k = 0; k < s.size(); ++k)
      if ((static_cast<unsigned char>(s[k]) & 0xC0) != 0x80) l += 1;
    texte(f, gx, haut + l * taille * 0.58 / 2.54 + 0.4, s, taille, 1);
  };
  auto fil = [&](std::size_t f, ecad::Pt a, ecad::Pt b) {
    if (a == b) return;
    ecad::Fil w;
    w.uuid = ecad::nouvelUuid();
    w.a = a;
    w.b = b;
    p.schema.feuilles[f].fils.push_back(w);
  };
  auto composantDe = [&](std::size_t f, const std::string& pose) -> ecad::ComposantElec* {
    for (const auto& s : p.schema.feuilles[f].symboles)
      if (s.uuid == pose) return p.schema.composant(s.composant);
    return nullptr;
  };
  // Repères : ceux du calcul pour les départs ; les arrivées des tableaux prennent les numéros libres suivants
  std::set<std::string> reperesPris, reserves;
  for (const auto& c : p.schema.composants) reperesPris.insert(c.reference);
  for (const auto& d : r.departs)
    if (d.charge != TypeCharge::Tableau) reserves.insert(d.repere);
  auto nommer = [&](ecad::ComposantElec* k, const std::string& voulu) {
    if (!k) return;
    if (!voulu.empty() && !reperesPris.count(voulu)) {
      k->reference = voulu;
    } else {
      for (int n = 1; n < 100000; ++n) {
        const std::string c = "Q" + std::to_string(n);
        if (!reperesPris.count(c) && !reserves.count(c)) {
          k->reference = c;
          break;
        }
      }
    }
    reperesPris.insert(k->reference);
  };
  std::map<std::string, std::size_t> indice;
  for (std::size_t i = 0; i < r.departs.size(); ++i) indice[r.departs[i].uuid] = i;
  std::map<std::string, std::vector<std::size_t>> enfants;  // tableau → ses départs ; "" → la source
  for (const auto& [i, prof] : ordreArbre(r)) {
    (void)prof;
    const DepartBT& d = r.departs[i];
    const auto it = indice.find(d.parent);
    const bool racine = d.parent.empty() || it == indice.end() || r.departs[it->second].charge != TypeCharge::Tableau;
    enfants[racine ? std::string() : d.parent].push_back(i);
  }
  // Symbole de la protection d'un départ, réglée d'après le calcul
  auto poserProtection = [&](std::size_t f, std::size_t i, double gx, double gy) {
    const DepartBT& d = r.departs[i];
    const ResultatDepartBT& y = x.departs[i];
    const std::string symbole = y.protection == TypeProtection::Fusible ? "iec:u-fusible" : d.differentiel > 0 ? "iec:u-dd" : "iec:u-disj";
    ecad::ComposantElec* k = composantDe(f, poserAppareil(p, f, symbole, P(gx, gy), 0));
    if (!k) return;
    nommer(k, d.charge == TypeCharge::Tableau ? std::string() : d.repere);
    k->description = y.designationProtection;
    k->valeur = y.designationProtection;
    k->champs["fonction"] = d.charge == TypeCharge::Tableau ? "Arrivée " + d.repere + " — " + d.nom : d.nom;
    k->champs["calibre"] = nombreFr(y.protection == TypeProtection::Reglable || y.protection == TypeProtection::Moteur ? y.ir : y.in, y.ir < 10 ? 2 : 0);
    if (y.protection == TypeProtection::Modulaire) k->champs["courbe"] = d.courbe == "B" || d.courbe == "D" ? d.courbe : "C";
    if (d.differentiel > 0) {
      k->champs["sensibilite"] = nombreFr(d.differentiel);
      k->champs["typeDifferentiel"] = d.typeDifferentiel;
    }
    k->champs["pouvoirCoupure"] = nombreFr(y.pdc);
    k->champs["origine"] = ORIGINE;
    k->champs["depart"] = d.uuid;
  };
  // Récepteur au bout d'un départ (ou renvoi vers le tableau alimenté)
  auto poserRecepteur = [&](std::size_t f, std::size_t i, double gx, double gy) {
    const DepartBT& d = r.departs[i];
    std::string symbole;
    switch (d.charge) {
      case TypeCharge::Tableau: poserPotentiel(p, f, d.repere, P(gx, gy)); return;
      case TypeCharge::Eclairage: symbole = "iec:u-lampe"; break;
      case TypeCharge::Prises: symbole = "iec:u-prise"; break;
      case TypeCharge::Chauffage: symbole = "iec:u-chauffage"; break;
      case TypeCharge::Moteur: symbole = "iec:u-moteur"; break;
      case TypeCharge::IRVE: symbole = "iec:u-irve"; break;
      case TypeCharge::Condensateurs: symbole = "iec:u-condensateur"; break;
      case TypeCharge::Autre: symbole = "iec:u-depart"; break;
    }
    poserAppareil(p, f, symbole, P(gx, gy + 2), 0);
  };
  // Constantes de mise en page (pas de 2,54 mm) : source, arrivée, jeu de barres, départs, récepteurs, textes
  constexpr double xArrivee = 10, ySource = 11, yArrivee = 23, yBarres = 33, yDisj = 41, yRecepteur = 49, yTextes = 55;
  constexpr double xPremier = 30, pas = 11, xMax = 154;
  std::size_t folio = position;
  std::function<void(const std::string&, const std::string&)> tableau = [&](const std::string& uuid, const std::string& titre) {
    const auto& liste = enfants[uuid];
    std::size_t f = ajouterFolio(p, TypeFolio::Unifilaire, titre, folio++);
    p.folios[p.schema.feuilles[f].uuid].origine = ORIGINE;
    const auto it = indice.find(uuid);
    const DepartBT* t = it == indice.end() ? nullptr : &r.departs[it->second];
    const ResultatDepartBT* ty = it == indice.end() ? nullptr : &x.departs[it->second];
    // ----- Arrivée -----
    if (!t || t->parent.empty()) {
      const SourceBT& s = r.source;
      std::string symbole = s.type == TypeSourceBT::Transformateur ? "iec:u-transfo" : s.type == TypeSourceBT::Groupe ? "iec:u-groupe" : "iec:u-reseau";
      poserAppareil(p, f, symbole, P(xArrivee, ySource), 0);
      std::string l1 = nomTypeSource(s.type), l2;
      switch (s.type) {
        case TypeSourceBT::Transformateur: l2 = nombreFr(s.puissance) + " kVA, ucc " + nombreFr(s.ucc) + " %"; break;
        case TypeSourceBT::Groupe: l2 = nombreFr(s.puissance) + " kVA, x''d " + nombreFr(s.xdSub) + " %"; break;
        case TypeSourceBT::ReseauPublic: l2 = nombreFr(s.puissance) + " kVA (NF C 14-100)"; break;
        case TypeSourceBT::BranchementLimite: l2 = nombreFr(s.puissance) + " kVA, Icc " + nombreFr(s.icc) + " kA"; break;
      }
      texte(f, xArrivee + 4, ySource - 3.4, l1, 2.6);
      texte(f, xArrivee + 4, ySource - 1.4, l2 + " · " + nombreFr(s.tension) + " V · " + nomRegime(s.regime), 2.0);
      texte(f, xArrivee + 4, ySource + 0.4, "Ik3 à l'origine " + kA(x.ik3Source) + " kA · appel " + n1(x.s) + " kVA, cos φ " + n2(x.cosPhi), 2.0);
      fil(f, P(xArrivee, ySource + 3), P(xArrivee, yArrivee - 3));
      if (t) {
        poserProtection(f, it->second, xArrivee, yArrivee);
        texte(f, xArrivee + 4, yArrivee + 1.8, ty->designationCable + " · " + nombreFr(t->longueur) + " m", 1.8);
      }
      fil(f, P(xArrivee, t ? yArrivee + 3 : ySource + 3), P(xArrivee, yBarres));
    } else {
      poserPotentiel(p, f, t->repere, P(xArrivee, yArrivee - 4));
      fil(f, P(xArrivee, yArrivee - 4), P(xArrivee, yBarres));
      const auto jt = indice.find(t->parent);
      texte(f, xArrivee + 2, yArrivee - 1.4, "Alimenté par " + (jt == indice.end() ? std::string("la source") : r.departs[jt->second].repere), 2.0);
      texte(f, xArrivee + 2, yArrivee + 0.6, ty->designationProtection, 1.8);
      texte(f, xArrivee + 2, yArrivee + 2.4, ty->designationCable + " · " + nombreFr(t->longueur) + " m", 1.8);
    }
    if (t && ty)
      texte(f, xArrivee + 2, yBarres - 1.2,
            t->repere + " — " + t->nom + " · Ik3 " + kA(ty->ik3max) + " kA · ΔU " + n2(ty->chuteCumulee) + " % · " + n1(ty->s) + " kVA, " + n1(ty->ib) + " A", 2.4);
    // ----- Départs -----
    double xDebutBarres = xArrivee, gx = xPremier;
    double xFin = xArrivee;
    for (std::size_t k = 0; k < liste.size(); ++k) {
      if (gx > xMax) {
        // Folio plein : les barres continuent sur le suivant par un renvoi
        const std::string renvoi = (t ? t->repere : std::string("Source")) + " barres";
        fil(f, P(xDebutBarres, yBarres), P(xFin + 4, yBarres));
        poserPotentiel(p, f, renvoi, P(xFin + 4, yBarres));
        f = ajouterFolio(p, TypeFolio::Unifilaire, titre + " (suite)", folio++);
        p.folios[p.schema.feuilles[f].uuid].origine = ORIGINE;
        poserPotentiel(p, f, renvoi, P(8, yBarres));
        xDebutBarres = 8;
        xFin = 8;
        gx = xPremier - 10;
      }
      const std::size_t i = liste[k];
      const DepartBT& d = r.departs[i];
      const ResultatDepartBT& y = x.departs[i];
      fil(f, P(gx, yBarres), P(gx, yDisj - 3));
      poserProtection(f, i, gx, yDisj);
      fil(f, P(gx, yDisj + 3), P(gx, yRecepteur - 2));
      poserRecepteur(f, i, gx, yRecepteur - 2);
      // Textes verticaux sous le récepteur : repère et nom, protection, canalisation, résultats
      texteVertical(f, gx - 2.2, yTextes, d.repere + " " + d.nom, 2.0);
      texteVertical(f, gx - 0.4, yTextes, protectionCourte(d, y), 1.7);
      texteVertical(f, gx + 1.3, yTextes, y.designationCable + " " + nombreFr(d.longueur) + " m", 1.7);
      texteVertical(f, gx + 3.0, yTextes, "ΔU " + n2(y.chuteCumulee) + " % · Ik " + kA(y.ik3max) + " kA", 1.7);
      xFin = gx;
      gx += pas;
    }
    fil(f, P(xDebutBarres, yBarres), P(std::max(xFin, xArrivee), yBarres));
    // Les tableaux alimentés ont leur folio
    for (std::size_t i : liste)
      if (r.departs[i].charge == TypeCharge::Tableau) tableau(r.departs[i].uuid, "Unifilaire " + r.departs[i].repere + " — " + r.departs[i].nom);
  };
  // Chaque tableau racine a son folio ; des départs directs sur la source vont sur un folio « Source »
  bool directs = false;
  for (std::size_t i : enfants[std::string()])
    if (r.departs[i].charge != TypeCharge::Tableau) directs = true;
  if (directs) tableau(std::string(), "Unifilaire — départs de la source");
  for (std::size_t i : enfants[std::string()])
    if (r.departs[i].charge == TypeCharge::Tableau) {
      // Le tableau racine : sa protection est en tête de son propre folio
      const std::string uuid = r.departs[i].uuid;
      tableau(uuid, "Unifilaire " + r.departs[i].repere + " — " + r.departs[i].nom);
    }
  numeroterFils(p, false);
  // Conducteurs : la section et le câble de chaque départ, tels que le calcul les a choisis
  {
    const ecad::Connectivite k = ecad::connectivite(p.schema);
    for (const auto& c : p.schema.composants) {
      if (detail::champ(c, "origine") != ORIGINE) continue;
      const auto it = indice.find(detail::champ(c, "depart"));
      if (it == indice.end()) continue;
      const ResultatDepartBT& y = x.departs[it->second];
      const std::string nom = k.nom(c.uuid, "2");
      if (!nom.empty()) p.conducteurs[nom] = {y.section, {}, y.designationCable};
    }
  }
  return premier;
}

}  // namespace prom::elec

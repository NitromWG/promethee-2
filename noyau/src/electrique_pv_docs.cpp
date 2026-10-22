// Prométhée : documents de l'installation photovoltaïque — note de calcul (HTML) et schéma unifilaire tiré du
// calcul : chaînes, protections et sectionnement du côté continu, parafoudres, onduleurs, protections du côté
// alternatif, branchement. Les onduleurs identiques trop nombreux pour un folio sont dessinés une fois, avec leur
// nombre. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "electrique_interne.hpp"
#include "promethee/electrique_pv.hpp"

namespace prom::elec {

using detail::nombreFr;

namespace {

const char* ORIGINE = "photovoltaique";

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
std::string n0(double v) { return nombreFr(v, 0); }
std::string n1(double v) { return nombreFr(v, 1); }
std::string n2(double v) { return nombreFr(v, 2); }
const char* niveau(NiveauAvis n) {
  switch (n) {
    case NiveauAvis::Erreur: return "Erreur";
    case NiveauAvis::Alerte: return "Alerte";
    case NiveauAvis::Info: break;
  }
  return "Info";
}
std::string cableAC(const OnduleurPV& o, double section) { return std::string("U1000 R2V ") + (o.triphase ? "5G" : "3G") + nombreFr(section); }
// Chaînes sur chaque entrée de l'onduleur k
std::vector<int> entreesDe(const ResultatPV& x, int k, int mppt) {
  std::vector<int> r(static_cast<std::size_t>(mppt), 0);
  for (int e = 0; e < mppt; ++e) {
    const std::size_t i = static_cast<std::size_t>(k * mppt + e);
    if (i < x.repartition.size()) r[static_cast<std::size_t>(e)] = x.repartition[i];
  }
  return r;
}

}  // namespace

// ---------- Note de calcul ----------
std::string htmlNotePV(const InstallationPV& in, const ResultatPV& x, const std::string& projet) {
  const ModulePV& m = in.module;
  const OnduleurPV& o = in.onduleur;
  const int onduleurs = std::max(1, in.onduleurs), mppt = std::max(1, o.mppt);
  std::string h;
  h += "<html><head><meta charset=\"utf-8\"><title>Note de calcul photovoltaïque</title></head><body style=\"font-family:sans-serif; font-size:9pt; color:#10142A\">";
  h += "<h1 style=\"font-size:16pt\">Note de calcul photovoltaïque — " + echapper(in.nom) + "</h1>";
  if (!projet.empty()) h += "<p>Projet : <b>" + echapper(projet) + "</b></p>";
  h += "<p style=\"font-size:11pt\"><b>" + n2(x.puissanceCrete) + " kWc</b> · " + std::to_string(x.modulesPoses) + " modules · " + std::to_string(onduleurs) + " onduleur(s) de " +
       n0(o.puissanceAC) + " VA · production estimée <b>" + n0(x.production) + " kWh/an</b> · <b style=\"color:" + std::string(x.erreurs() ? "#C2402E" : "#2E7D32") + "\">" +
       std::to_string(x.erreurs()) + " erreur(s)</b> · " + std::to_string(x.alertes()) + " alerte(s)</p>";
  auto table = [&h](const std::string& titre) { h += "<h2 style=\"font-size:12pt\">" + titre + "</h2><table border=\"1\" cellspacing=\"0\" cellpadding=\"3\">"; };
  auto ligne = [&h](const std::string& a, const std::string& b) { h += "<tr><td>" + echapper(a) + "</td><td><b>" + echapper(b) + "</b></td></tr>"; };
  auto fin = [&h] { h += "</table>"; };
  // Matériel
  table("Matériel");
  ligne("Module", m.nom + " : " + n0(m.puissance) + " Wc, " + n0(m.largeur) + " × " + n0(m.hauteur) + " mm");
  ligne("Caractéristiques STC du module", "Voc " + n2(m.voc) + " V, Vmp " + n2(m.vmp) + " V, Isc " + n2(m.isc) + " A, Imp " + n2(m.imp) + " A");
  ligne("Coefficient de température de Voc", m.coefVoc != 0 ? n2(m.coefVoc) + " %/°C" : std::string("non donné (tableau selon la température minimale)"));
  ligne("Tension maximale du système, courant inverse admissible", n0(m.tensionMax) + " V, IRM " + n0(m.courantInverse) + " A");
  ligne("Onduleur", o.nom + (onduleurs > 1 ? " (" + std::to_string(onduleurs) + " onduleurs)" : std::string()));
  ligne("Puissances de l'onduleur", n0(o.puissanceAC) + " VA en sortie (" + std::string(o.triphase ? "triphasé" : "monophasé") + "), " + n0(o.puissanceDCMax) + " W en entrée au plus");
  ligne("Entrées de l'onduleur", std::to_string(mppt) + " MPPT, " + std::to_string(std::max(1, o.entreesParMppt)) + " connecteur(s) chacune, " + n0(o.courantMppt) +
                                     " A exploités, Isc " + n0(o.iscMppt) + " A au plus par MPPT");
  ligne("Tensions continues de l'onduleur", "plage MPPT " + n0(o.mpptMin) + " à " + n0(o.mpptMax) + " V, " + n0(o.tensionDCMax) + " V au plus");
  fin();
  // Site
  table("Site et raccordement");
  ligne("Températures", "ambiante minimale " + n0(in.tMin) + " °C, cellules jusqu'à " + n0(in.tMax) + " °C, câbles continus à " + n0(in.temperatureCables) + " °C");
  ligne("Bâtiment", std::string(nomBatimentPV(in.batiment)) + (in.paratonnerre ? ", équipé d'un paratonnerre" : ""));
  ligne("Densité de foudroiement", "Ng = " + n1(in.ng) + " impacts par km² et par an");
  ligne("Longueurs", "des chaînes à l'onduleur " + n0(in.longueurDC) + " m, de l'onduleur au tableau " + n0(in.longueurAC) + " m");
  ligne("Raccordement", in.venteTotale ? "vente totale (branchement propre)" : "autoconsommation avec vente du surplus (circuit dédié du tableau)");
  fin();
  // Calepinage
  table("Calepinage");
  ligne("Coefficient de Voc au froid", m.coefVoc != 0 ? "k = 1 + " + n2(m.coefVoc) + " % × (" + n0(in.tMin) + " − 25) = " + nombreFr(x.kTemperature, 4)
                                                     : "k = " + n2(x.kTemperature) + " (tableau, modules cristallins)");
  ligne("Tension à vide d'un module au froid", "Uoc max = " + n2(m.voc) + " × " + nombreFr(x.kTemperature, 4) + " = " + n2(x.uocModuleMax) + " V");
  ligne("Tension de puissance maximale d'un module", n2(x.vmpModuleMin) + " V à " + n0(in.tMax) + " °C, " + n2(x.vmpModuleMax) + " V au froid");
  ligne("Modules en série admis", std::to_string(x.seriesMin) + " à " + std::to_string(x.seriesMax) + " (plage MPPT au chaud, " + n0(std::min(o.tensionDCMax, m.tensionMax)) +
                                      " V à vide au froid)");
  ligne("Chaînes", std::to_string(x.chaines) + " chaîne(s) de " + std::to_string(x.modulesParChaine) + " modules, " + std::to_string(x.modulesPoses) + " modules posés sur " +
                       std::to_string(std::max(0, in.nombreModules)) + ", " + n2(x.puissanceCrete) + " kWc");
  std::string rep;
  for (int k = 0; k < onduleurs && !x.repartition.empty(); ++k) {
    const std::vector<int> e = entreesDe(x, k, mppt);
    std::string t;
    for (int j = 0; j < mppt; ++j) t += (j ? ", " : "") + std::string("MPPT ") + std::to_string(j + 1) + " : " + std::to_string(e[static_cast<std::size_t>(j)]);
    rep += (k ? " ; " : "") + (onduleurs > 1 ? "onduleur " + std::to_string(k + 1) + " — " : std::string()) + t;
  }
  if (!rep.empty()) ligne("Chaînes par entrée", rep);
  ligne("Tensions d'une chaîne", "Uoc " + n0(x.uocChaine) + " V au froid ; Vmp de " + n0(x.vmpChaineMin) + " V (chaud) à " + n0(x.vmpChaineMax) + " V (froid)");
  ligne("Courants retenus", "1,25 × Isc = " + n2(x.iscChaine) + " A par chaîne, " + n2(x.iscMppt) + " A par entrée");
  ligne("Rapport puissance crête / onduleur", n2(x.ratioDCAC));
  fin();
  // Côté continu
  table("Côté continu");
  ligne("Protection des chaînes", std::to_string(x.chainesMaxSansProtection) + " chaîne(s) en parallèle au plus sans protection (1 + IRM / Isc) ; " +
                                      (x.fusibles ? "fusibles gPV " + n0(x.calibreFusible) + " A sur les deux polarités (1,4 Isc ≤ In ≤ IRM)" : std::string("pas de fusible")));
  ligne("Câbles des chaînes", "H1Z2Z2-K " + nombreFr(x.sectionDC) + " mm², Iz " + n1(x.izDC) + " A à " + n0(in.temperatureCables) + " °C ≥ " + n1(x.izRequis) + " A requis ; ΔU " +
                                  n2(x.chuteDC) + " %");
  if (x.boiteJonction) ligne("Câble principal", "H1Z2Z2-K " + nombreFr(x.sectionPrincipal) + " mm², Iz " + n1(x.izPrincipal) + " A (boîte de jonction près de l'onduleur)");
  ligne("Interrupteur-sectionneur continu", "bipolaire, DC21B au moins, " + n0(x.sectionneurTension) + " V et " + n1(x.sectionneurCourant) + " A au moins par entrée");
  const std::string base = in.batiment == BatimentPV::Habitation ? "115" : in.batiment == BatimentPV::Tertiaire ? "450" : "200";
  ligne("Parafoudre continu", "Lcrit = " + base + " / Ng = " + n0(x.longueurCritique) + " m pour " + n0(in.longueurDC) + " m de câbles : " +
                                  (x.parafoudreDC ? "obligatoire, type 2, Ucpv ≥ " + n0(x.uocChaine) + " V" : std::string("non obligatoire")));
  fin();
  // Côté alternatif
  table("Côté alternatif");
  ligne("Courant de sortie de l'onduleur", n1(x.courantAC) + " A (" + std::string(o.courantACMax > 0 ? "notice" : "1,1 fois le courant nominal") + ")");
  ligne("Protection par onduleur", "disjoncteur " + n0(x.calibreAC) + " A, pouvoir de coupure " + n0(x.pdcAC) + " kA au moins, différentiel " + x.differentielAC);
  const int charges = o.triphase ? 3 : 2;
  ligne("Câble alternatif", cableAC(o, x.sectionAC) + ", " + n0(in.longueurAC) + " m, Iz " + n0(courantAdmissible(x.sectionAC, MethodePose::C, Isolant::PR, charges)) + " A ≥ " +
                                n0(x.calibreAC) + " A, ΔU " + n2(x.chuteAC) + " %");
  if (onduleurs > 1) ligne("Protection générale", n0(x.calibreGeneral) + " A pour " + n1(x.courantACTotal) + " A (" + std::to_string(onduleurs) + " onduleurs)");
  ligne("Parafoudre alternatif", x.parafoudreAC ? x.typeParafoudreAC : std::string("non requis"));
  ligne("Branchement", in.venteTotale ? (x.reglageBranchement > 0 ? "disjoncteur de branchement différentiel 500 mA type S réglé à " + n0(x.reglageBranchement) + " A, compteur de production"
                                                                    : std::string("branchement à puissance surveillée, compteur de production"))
                                      : std::string("circuit dédié du tableau de l'installation, réservé à la production"));
  fin();
  // Production
  table("Production estimée");
  ligne("Productible du site", n0(in.productible) + " kWh par kWc et par an (plein sud, incliné à 30°, pertes comprises)");
  ligne("Orientation et inclinaison", "écart au sud " + n0(in.orientation) + "°, inclinaison " + n0(in.inclinaison) + "° : coefficient " + n2(x.coefOrientation));
  ligne("Production annuelle", n2(x.puissanceCrete) + " kWc × " + n0(in.productible) + " × " + n2(x.coefOrientation) + " = " + n0(x.production) + " kWh");
  fin();
  if (!x.avis.empty()) {
    h += "<h2 style=\"font-size:12pt\">Avis</h2><ul>";
    for (const auto& a : x.avis) h += "<li><b>" + std::string(niveau(a.niveau)) + "</b> (" + echapper(a.regle) + ") : " + echapper(a.message) + "</li>";
    h += "</ul>";
  }
  h += "<h2 style=\"font-size:12pt\">Méthodes et hypothèses</h2><p>"
       "Guide UTE C 15-712-1 et guide SER/ADEME « protection des personnes et des biens » (2012). Tension à vide au froid : Uoc × k, avec k = 1 + α (Tmin − 25) quand "
       "le fabricant donne α, tableau selon la température minimale sinon ; tension de puissance maximale au chaud avec le même coefficient. Composants dimensionnés sur "
       "1,25 Isc STC. Protection des chaînes quand leur nombre en parallèle dépasse 1 + IRM / Isc : fusibles gPV avec 1,4 Isc ≤ In ≤ IRM et câbles Iz ≥ 1,45 In ; sans "
       "fusible, Iz ≥ 1,25 Isc (au-delà de deux chaînes, (Nc − 1) × 1,25 Isc).<br>"
       "Câbles solaires H1Z2Z2-K de l'EN 50618 (annexe A : deux câbles jointifs sur une paroi, 60 °C, facteur 0,91 à 70 °C) ; chute de tension continue à Imp et Vmp STC "
       "avec ρ = 23 mΩ·mm²/m ; alternative au courant de sortie avec ρ = 22,5 mΩ·mm²/m, câble posé en méthode C (PR). Courant de sortie : la notice de l'onduleur, à "
       "défaut 1,1 fois le nominal.<br>"
       "Parafoudres : longueur critique du guide UTE C 61-740-52 (115, 200 ou 450 divisé par Ng) pour le côté continu ; NF C 15-100 (443, 534) pour le côté alternatif.<br>"
       "Production : productible du site multiplié par le coefficient d'orientation et d'inclinaison (tableau usuel en France) ; PVGIS donne une estimation plus fine pour "
       "une adresse.</p>";
  h += "<p style=\"color:#6B7393\">Note établie par Prométhée. Elle aide à concevoir et ne remplace ni la notice de l'onduleur, ni la vérification de l'installateur, "
       "ni l'attestation du Consuel.</p></body></html>";
  return h;
}

// ---------- Schéma unifilaire ----------
std::size_t genererUnifilairePV(ProjetElectrique& p) {
  if (!p.photovoltaique) return 0;
  const InstallationPV& in = *p.photovoltaique;
  const ResultatPV x = calculerPV(in);
  const ModulePV& m = in.module;
  const OnduleurPV& o = in.onduleur;
  // Le folio tiré d'un calcul précédent part (avec ses appareils), le nouveau prend sa place
  const std::size_t position = detail::retirerFoliosCalcules(p, ORIGINE);
  const std::size_t f = ajouterFolio(p, TypeFolio::Unifilaire, "Unifilaire photovoltaïque — " + n2(x.puissanceCrete) + " kWc", position);
  p.folios[p.schema.feuilles[f].uuid].origine = ORIGINE;

  const auto P = [](double gx, double gy) { return ecad::ptMm(gx * 2.54, gy * 2.54); };
  auto texte = [&](double gx, double gy, const std::string& s, double taille) {
    ecad::TexteSchema t;
    t.uuid = ecad::nouvelUuid();
    t.texte = s;
    t.p = P(gx, gy);
    t.taille = ecad::nm(taille);
    p.schema.feuilles[f].textes.push_back(t);
  };
  auto fil = [&](double ax, double ay, double bx, double by) {
    if (std::abs(ax - bx) < 1e-9 && std::abs(ay - by) < 1e-9) return;
    ecad::Fil w;
    w.uuid = ecad::nouvelUuid();
    w.a = P(ax, ay);
    w.b = P(bx, by);
    p.schema.feuilles[f].fils.push_back(w);
  };
  // Pose un appareil réglé d'après le calcul ; rend l'UUID de l'appareil (les pointeurs ne survivent pas aux poses suivantes)
  auto poser = [&](const std::string& symbole, double gx, double gy, const std::string& valeur, const std::string& description,
                   const std::map<std::string, std::string>& champs) -> std::string {
    const std::string pose = poserAppareil(p, f, symbole, P(gx, gy), 0);
    for (const auto& s : p.schema.feuilles[f].symboles)
      if (s.uuid == pose && !s.composant.empty())
        if (ecad::ComposantElec* k = p.schema.composant(s.composant)) {
          k->valeur = valeur;
          k->description = description;
          k->champs["origine"] = ORIGINE;
          for (const auto& [c, v] : champs) k->champs[c] = v;
          return k->uuid;
        }
    return {};
  };
  auto parafoudre = [&](double gx, double yPiquage, const std::string& valeur, const std::string& description) {
    fil(gx, yPiquage, gx + 3, yPiquage);
    poser("iec:parafoudre", gx + 3, yPiquage + 3, valeur, description, {{"affichage", valeur}});
    fil(gx + 3, yPiquage + 6, gx + 3, yPiquage + 7);
    poserAppareil(p, f, "iec:terre", P(gx + 3, yPiquage + 8), 0);
  };
  std::vector<std::tuple<std::string, std::string, ProprietesConducteur>> conducteurs;  // appareil, borne, conducteur qui en part

  // ----- Titre et légende -----
  const int onduleurs = std::max(1, in.onduleurs), mppt = std::max(1, o.mppt);
  texte(6, 7.6, in.nom + " — " + n2(x.puissanceCrete) + " kWc", 2.6);
  texte(6, 9.8,
        std::to_string(x.modulesPoses) + " modules de " + n0(m.puissance) + " Wc en " + std::to_string(x.chaines) + " chaîne(s) de " + std::to_string(x.modulesParChaine) + " · " +
            std::to_string(onduleurs) + " onduleur(s) de " + n0(o.puissanceAC) + " VA · " + (in.venteTotale ? "vente totale" : "autoconsommation"),
        1.8);
  if (x.modulesParChaine <= 0 || x.chaines <= 0 || x.repartition.empty()) {
    std::string raison = "Calepinage impossible";
    for (const auto& a : x.avis)
      if (a.niveau == NiveauAvis::Erreur) {
        raison += " : " + a.message;
        break;
      }
    texte(6, 16, raison, 2.2);
    return f;
  }

  // ----- Colonnes : une par entrée utilisée (au-delà de quatre, une par groupe d'entrées identiques) ; onduleurs
  // identiques réunis quand ils ne tiennent pas tous sur le folio -----
  struct Colonne {
    int chaines = 0, entrees = 1;
    std::string titre;
    double x = 0;
  };
  struct Groupe {
    std::vector<int> config;
    int nombre = 1;
    std::vector<Colonne> colonnes;
    double x = 0;
  };
  auto colonnesDe = [&](const std::vector<int>& c) {
    std::vector<Colonne> r;
    const auto utilisees = std::count_if(c.begin(), c.end(), [](int v) { return v > 0; });
    if (utilisees <= 4) {
      for (int e = 0; e < mppt; ++e)
        if (c[static_cast<std::size_t>(e)] > 0) r.push_back({c[static_cast<std::size_t>(e)], 1, "MPPT " + std::to_string(e + 1), 0});
    } else {
      std::map<int, int> parNombre;  // chaînes par entrée → entrées
      for (int v : c)
        if (v > 0) ++parNombre[v];
      for (auto it = parNombre.rbegin(); it != parNombre.rend(); ++it) r.push_back({it->first, it->second, std::to_string(it->second) + " MPPT identiques", 0});
    }
    return r;
  };
  std::size_t colonnesSeparees = 0;
  for (int k = 0; k < onduleurs; ++k) colonnesSeparees += colonnesDe(entreesDe(x, k, mppt)).size();
  const bool separes = onduleurs <= 6 && colonnesSeparees <= 12;
  std::vector<Groupe> groupes;
  for (int k = 0; k < onduleurs; ++k) {
    const std::vector<int> c = entreesDe(x, k, mppt);
    if (std::all_of(c.begin(), c.end(), [](int v) { return v == 0; })) continue;
    if (!separes) {
      const auto it = std::find_if(groupes.begin(), groupes.end(), [&c](const Groupe& g) { return g.config == c; });
      if (it != groupes.end()) {
        ++it->nombre;
        continue;
      }
    }
    groupes.push_back({c, 1, colonnesDe(c), 0});
  }
  std::size_t nbColonnes = 0;
  for (const auto& g : groupes) nbColonnes += g.colonnes.size();
  constexpr double x0 = 26, xDernier = 148;
  const double pas = nbColonnes <= 1 ? 24 : std::clamp(std::floor((xDernier - x0) / static_cast<double>(nbColonnes - 1)), 11.0, 24.0);
  {
    double cx = x0;
    for (auto& g : groupes) {
      for (auto& c : g.colonnes) {
        c.x = cx;
        cx += pas;
      }
      g.x = std::round((g.colonnes.front().x + g.colonnes.back().x) / 2);
    }
  }

  // ----- Rangées (pas de 2,54 mm) -----
  constexpr double yTitreColonne = 12.8, yPV = 17, yFusible = 25, ySectionneur = 33, yPiquageDC = 37, yOnduleur = 54, yProtection = 63;
  const double yBornePV = yPV + 3, yHautSect = ySectionneur - 3, yBasSect = ySectionneur + 3;
  const double yHautOnd = yOnduleur - 4, yBasOnd = yOnduleur + 4;
  texte(6, yPV, "Chaînes de modules", 1.9);
  if (x.fusibles) texte(6, yFusible, x.boiteJonction ? "Boîte de jonction (gPV)" : "Fusibles gPV", 1.9);
  texte(6, ySectionneur, "Sectionnement continu", 1.9);
  if (x.parafoudreDC) texte(6, yPiquageDC + 3, "Parafoudre continu", 1.9);
  texte(6, yOnduleur, onduleurs > 1 ? "Onduleurs" : "Onduleur", 1.9);
  texte(6, yProtection, "Protection alternative", 1.9);

  const std::string designationDC = "H1Z2Z2-K";
  const ProprietesConducteur conducteurChaine{x.sectionDC, {}, designationDC};
  const ProprietesConducteur conducteurPrincipal{x.boiteJonction ? x.sectionPrincipal : x.sectionDC, {}, designationDC};
  for (auto& g : groupes) {
    const int n = static_cast<int>(g.colonnes.size());
    for (int j = 0; j < n; ++j) {
      const Colonne& c = g.colonnes[static_cast<std::size_t>(j)];
      texte(c.x - 2, yTitreColonne, c.titre, 1.9);
      // Chaîne (ou chaînes en parallèle sur l'entrée)
      const std::string pv = poser("iec:u-pv", c.x, yPV,
                                   (c.chaines > 1 ? std::to_string(c.chaines) + " × " : std::string()) + std::to_string(x.modulesParChaine) + " × " + n0(m.puissance) + " Wc",
                                   std::to_string(c.chaines) + " chaîne(s) de " + std::to_string(x.modulesParChaine) + " modules " + m.nom,
                                   {{"chaines", std::to_string(c.chaines)},
                                    {"modules", std::to_string(x.modulesParChaine)},
                                    {"quantite", std::to_string(c.entrees * g.nombre)},
                                    {"affichage", (c.chaines > 1 ? std::to_string(c.chaines) + " × " : std::string()) + std::to_string(x.modulesParChaine) + " × " + n0(m.puissance) + " Wc"}});
      texte(c.x + 2.4, yPV + 1.9, "Uoc " + n0(x.uocChaine) + " V · Isc " + n1(m.isc * c.chaines) + " A", 1.5);
      conducteurs.emplace_back(pv, "2", conducteurChaine);
      texte(c.x + 0.6, yBornePV + 1.2, nombreFr(x.sectionDC) + " mm²", 1.5);
      // Fusibles des chaînes (dans la boîte de jonction quand il y en a une)
      if (x.fusibles) {
        fil(c.x, yBornePV, c.x, yFusible - 3);
        const std::string fu = poser("iec:u-fusible", c.x, yFusible, "gPV " + n0(x.calibreFusible) + " A", "Fusibles gPV " + n0(x.calibreFusible) + " A sur les deux polarités",
                                     {{"calibre", n0(x.calibreFusible)}, {"quantite", std::to_string(2 * c.chaines * c.entrees * g.nombre)}, {"affichage", "gPV " + n0(x.calibreFusible) + " A"}});
        texte(c.x + 1.4, yFusible + 1.3, "+ et −, par chaîne", 1.5);
        conducteurs.emplace_back(fu, "2", conducteurPrincipal);
        if (x.boiteJonction) texte(c.x + 0.6, yFusible + 4.2, nombreFr(x.sectionPrincipal) + " mm²", 1.5);
        fil(c.x, yFusible + 3, c.x, yHautSect);
      } else {
        fil(c.x, yBornePV, c.x, yHautSect);
      }
      // Interrupteur-sectionneur continu
      const std::string se = poser("iec:u-inter", c.x, ySectionneur, n0(x.sectionneurTension) + " V · " + n1(x.sectionneurCourant) + " A",
                                   "Interrupteur-sectionneur continu bipolaire DC21B, " + n0(x.sectionneurTension) + " V, " + n1(x.sectionneurCourant) + " A au moins",
                                   {{"calibre", n1(x.sectionneurCourant)},
                                    {"tension", n0(x.sectionneurTension)},
                                    {"quantite", std::to_string(c.entrees * g.nombre)},
                                    {"affichage", n0(x.sectionneurTension) + " V · " + n1(x.sectionneurCourant) + " A"}});
      texte(c.x + 1.4, ySectionneur + 1.3, "DC21B, bipolaire", 1.5);
      conducteurs.emplace_back(se, "2", conducteurPrincipal);
      // Descente vers l'entrée de l'onduleur : les colonnes extérieures rejoignent leur borne plus bas que les
      // intérieures, pour que les retours ne se croisent pas
      const double decalage = j - (n - 1) / 2.0;
      const double xBorne = g.x + decalage * 2;
      const double yRetour = std::abs(decalage) >= 1.5 ? yHautOnd - 1 : yHautOnd - 2;
      if (std::abs(xBorne - c.x) < 1e-9) {
        fil(c.x, yBasSect, c.x, yHautOnd);
      } else {
        fil(c.x, yBasSect, c.x, yRetour);
        fil(c.x, yRetour, xBorne, yRetour);
        fil(xBorne, yRetour, xBorne, yHautOnd);
      }
      if (x.parafoudreDC)
        parafoudre(c.x, yPiquageDC, "Type 2 · " + n0(x.uocChaine) + " V", "Parafoudre continu de type 2, Ucpv ≥ " + n0(x.uocChaine) + " V");
    }
    // Onduleur
    const std::string symbole = n == 1 ? "iec:u-onduleur" : "iec:u-onduleur-" + std::to_string(n);
    const std::string affichageOnduleur = (o.puissanceAC >= 10000 ? n0(o.puissanceAC / 1000) + " kVA" : n0(o.puissanceAC) + " VA") + (o.triphase ? " tri" : " mono");
    const std::string ond = poser(symbole, g.x, yOnduleur, affichageOnduleur, o.nom, {{"quantite", std::to_string(g.nombre)}, {"affichage", affichageOnduleur}});
    const double w = std::max(3.0, (n - 1) + 1.0);  // demi-largeur du corps (pas de 2,54 mm)
    if (g.nombre > 1) texte(g.x + w + 0.8, yOnduleur + 2.2, "× " + std::to_string(g.nombre) + " onduleurs identiques", 1.6);
    conducteurs.emplace_back(ond, "AC", ProprietesConducteur{x.sectionAC, {}, cableAC(o, x.sectionAC)});
    texte(g.x + 0.6, yBasOnd + 1.6, nombreFr(x.sectionAC) + " mm²", 1.5);
    // Protection alternative de l'onduleur (au tableau : elle protège le câble contre le réseau)
    fil(g.x, yBasOnd, g.x, yProtection - 3);
    const std::string sens = in.batiment == BatimentPV::Habitation ? "30" : "300";
    poser("iec:u-dd", g.x, yProtection, n0(x.calibreAC) + " A · " + sens + " mA", "Disjoncteur différentiel " + n0(x.calibreAC) + " A, " + x.differentielAC,
          {{"calibre", n0(x.calibreAC)}, {"sensibilite", sens}, {"typeDifferentiel", "A"}, {"pouvoirCoupure", n0(x.pdcAC)}, {"quantite", std::to_string(g.nombre)}});
    texte(g.x + 1.4, yProtection + 2.4, "PdC " + n0(x.pdcAC) + " kA", 1.5);  // sous la marque IΔ du symbole
  }

  // ----- Ensemble des onduleurs, parafoudre et branchement -----
  if (groupes.empty()) return f;
  const double xGen = groupes.size() > 1 ? std::round((groupes.front().x + groupes.back().x) / 2) : groupes.front().x;
  double yc = yProtection + 3;
  if (groupes.size() > 1) {
    constexpr double yBus = 69;
    for (const auto& g : groupes) fil(g.x, yc, g.x, yBus);
    fil(groupes.front().x, yBus, groupes.back().x, yBus);
    yc = yBus;
  }
  if (onduleurs > 1) {
    texte(6, yc + 5, "Protection générale", 1.9);
    fil(xGen, yc, xGen, yc + 2);
    poser("iec:u-disj", xGen, yc + 5, n0(x.calibreGeneral) + " A", "Disjoncteur général de la production, " + n0(x.calibreGeneral) + " A", {{"calibre", n0(x.calibreGeneral)}});
    yc += 8;
  }
  if (x.parafoudreAC) {
    texte(6, yc + 6, "Parafoudre alternatif", 1.9);
    parafoudre(xGen, yc + 3, x.typeParafoudreAC == "type 1" ? "Type 1" : "Type 2", "Parafoudre alternatif de " + x.typeParafoudreAC);
  }
  // Sous le parafoudre (sa terre descend jusqu'à yc + 12), le branchement ou le tableau
  fil(xGen, yc, xGen, yc + 12);
  if (in.venteTotale) {
    texte(6, yc + 15, "Branchement (vente totale)", 1.9);
    poser("iec:u-agcp", xGen, yc + 15, x.reglageBranchement > 0 ? "500 mA S · " + n0(x.reglageBranchement) + " A" : "Puissance surveillée",
          "Disjoncteur de branchement de la production" + (x.reglageBranchement > 0 ? ", différentiel 500 mA type S, réglé à " + n0(x.reglageBranchement) + " A" : std::string()),
          {{"calibre", n0(x.reglageBranchement)}, {"sensibilite", "500"}, {"affichage", x.reglageBranchement > 0 ? n0(x.reglageBranchement) + " A · 500 mA S" : "Puissance surveillée"}});
    texte(6, yc + 20, "Compteur de production", 1.9);
    poserAppareil(p, f, "iec:u-compteur", P(xGen, yc + 20), 0);  // sa borne touche celle du disjoncteur
  } else {
    texte(6, yc + 14, "Tableau de l'installation", 1.9);
    poserAppareil(p, f, "iec:u-tableau", P(xGen, yc + 14), 0);
    texte(xGen + 1.8, yc + 14.4, "Circuit dédié, réservé à la production", 1.6);
  }

  // ----- Caractéristiques : à droite des colonnes quand il y a la place, sinon sous la partie continue -----
  {
    double xFin = 0;
    for (const auto& g : groupes)
      for (const auto& c : g.colonnes) xFin = std::max(xFin, c.x);
    const bool aDroite = xFin + 12 <= 92;
    const double bx = aDroite ? std::max(xFin + 18, 66.0) : std::max(xGen + 16, 96.0), by = aDroite ? 14 : 74;
    std::vector<std::string> l;
    l.push_back("Générateur : " + std::to_string(x.modulesPoses) + " modules de " + n0(m.puissance) + " Wc en " + std::to_string(x.chaines) + " chaîne(s) de " +
                std::to_string(x.modulesParChaine) + ", " + n2(x.puissanceCrete) + " kWc");
    l.push_back("Module : " + m.nom + " — Voc " + n2(m.voc) + " V, Isc " + n2(m.isc) + " A");
    l.push_back("Onduleur : " + (onduleurs > 1 ? std::to_string(onduleurs) + " × " : std::string()) + o.nom + " — MPPT " + n0(o.mpptMin) + " à " + n0(o.mpptMax) + " V");
    l.push_back("Chaîne : Uoc " + n0(x.uocChaine) + " V à " + n0(in.tMin) + " °C, Vmp " + n0(x.vmpChaineMin) + " à " + n0(x.vmpChaineMax) + " V");
    l.push_back("Câbles des chaînes : H1Z2Z2-K " + nombreFr(x.sectionDC) + " mm², " + n0(in.longueurDC) + " m, Iz " + n1(x.izDC) + " A ≥ " + n1(x.izRequis) + " A, ΔU " +
                n2(x.chuteDC) + " %");
    if (x.boiteJonction) l.push_back("Câble principal : H1Z2Z2-K " + nombreFr(x.sectionPrincipal) + " mm², boîte de jonction IP 44 près de l'onduleur");
    l.push_back("Câble alternatif : " + cableAC(o, x.sectionAC) + ", " + n0(in.longueurAC) + " m, ΔU " + n2(x.chuteAC) + " %");
    l.push_back("Protection des chaînes : " + (x.fusibles ? "fusibles gPV " + n0(x.calibreFusible) + " A sur les deux polarités"
                                                           : "aucune (" + std::to_string(x.chainesMaxSansProtection) + " chaînes au plus en parallèle sans protection)"));
    l.push_back("Parafoudre continu : " + std::string(x.parafoudreDC ? "type 2, Ucpv ≥ " + n0(x.uocChaine) + " V" : "non obligatoire") + " (Lcrit " + n0(x.longueurCritique) + " m)");
    l.push_back("Masses et structures : vert-jaune 6 mm² cuivre au moins, reliées à la terre");
    l.push_back("Étiquettes : « Coupure d'urgence entrée onduleur », « Coupure d'urgence sortie onduleur »");
    l.push_back("Production estimée : ≈ " + n0(x.production) + " kWh/an (" + n0(in.productible) + " kWh/kWc × " + n2(x.coefOrientation) + ")");
    texte(bx, by, "Caractéristiques", 2.2);
    for (std::size_t k = 0; k < l.size(); ++k) texte(bx, by + 2.2 + 1.6 * static_cast<double>(k), l[k], 1.7);
  }

  numeroterFils(p, false);
  // Conducteurs : sections et câbles tels que le calcul les a choisis
  const ecad::Connectivite k = ecad::connectivite(p.schema);
  for (const auto& [appareil, borne, pr] : conducteurs) {
    if (appareil.empty()) continue;
    const std::string nom = k.nom(appareil, borne);
    if (!nom.empty()) p.conducteurs[nom] = pr;
  }
  return f;
}

}  // namespace prom::elec

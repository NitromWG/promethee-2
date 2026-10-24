// Prométhée : fabrication détaillée (0.34) — perçage complet par paires de couches (Excellon, EIA, Gerber X2),
// fentes détourées ou en G85, contre-perçages, tableau et plan de perçage, forets du profil, Gerber X3 des
// composants, vias à remplir (IPC-4761) et usinages de la carte nue. Licence GPL-3.0-only.
#include "promethee/csv.hpp"
#include "promethee/fabrication_detaillee.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "promethee/hauteurs.hpp"

namespace prom::ecad {

namespace {
std::string sur(const std::string& s) {
  std::string r;
  for (char ch : asciiFabrication(s)) r += (ch == '*' || ch == '%' || ch == ',' || ch == '\n') ? '_' : ch;
  return r;
}
std::string decimal(Nm v, int chiffres = 6) {
  char t[40];
  std::snprintf(t, sizeof t, "%.*f", chiffres, static_cast<double>(v) * 1e-6);
  return t;
}
// Rang 1 à n d'un cuivre (numérotation des fabricants : 1 dessus)
int numero(Couche c, int n) { return rangCuivre(c, std::max(n, 1)) + 1; }
bool traversant(const TrouFab& t, int n) {
  if (t.contrePercage) return false;
  if (n <= 2) return true;
  const int a = numero(t.de, n), b = numero(t.a, n);
  return std::min(a, b) == 1 && std::max(a, b) == n;
}
bool dansPaire(const TrouFab& t, const PairePercage& p, int n) {
  if (t.contrePercage != p.contrePercage) return false;
  if (!t.contrePercage && t.metallise != p.metallise) return false;
  return numero(t.de, n) == numero(p.de, n) && numero(t.a, n) == numero(p.a, n);
}
// Extrémités d'une fente : le centre ± la demi-longueur utile (longueur − diamètre) le long de son axe
std::pair<Pt, Pt> boutsFente(const TrouFab& t) {
  const Nm demi = (t.longueur - t.diametre) / 2;
  const Pt d = tournerDegres({demi, 0}, t.rotation);
  return {t.p - d, t.p + d};
}
}  // namespace

const std::vector<Nm>& foretsUsuels() {
  static const std::vector<Nm> f = [] {
    std::vector<Nm> v;
    for (int c = 20; c <= 100; c += 5) v.push_back(static_cast<Nm>(c) * 10'000);   // 0,20 à 1,00 mm au pas de 0,05
    for (int c = 110; c <= 300; c += 10) v.push_back(static_cast<Nm>(c) * 10'000);  // 1,1 à 3,0 au pas de 0,1
    for (int c = 320; c <= 630; c += 10) v.push_back(static_cast<Nm>(c) * 10'000);  // 3,2 à 6,3
    return v;
  }();
  return f;
}

std::vector<PairePercage> pairesPercage(const ModeleFabrication& m) {
  const int n = std::max(1, m.couchesCuivre);
  std::map<std::tuple<int, int, int, int>, PairePercage> paires;  // (contre-perçage, métallisé, de, à)
  for (const auto& t : m.trous) {
    PairePercage p;
    p.de = t.de;
    p.a = t.a;
    p.metallise = t.contrePercage ? false : t.metallise;
    p.contrePercage = t.contrePercage;
    if (traversant(t, n)) {
      const auto cu = couchesCuivre(n);
      p.de = cu.front();
      p.a = cu.back();
    }
    auto& x = paires.try_emplace({p.contrePercage ? 1 : 0, p.metallise ? 0 : 1, numero(p.de, n), numero(p.a, n)}, p).first->second;
    ++x.trous;
  }
  std::vector<PairePercage> r;
  for (auto& [cle, p] : paires) {
    const int a = numero(p.de, n), b = numero(p.a, n);
    const bool tout = std::min(a, b) == 1 && std::max(a, b) == n;
    const std::string genre = p.contrePercage ? "Blind" : tout ? (p.metallise ? "PTH" : "NPTH") : (std::min(a, b) == 1 || std::max(a, b) == n) ? "Blind" : "Buried";
    p.fonction = std::string(p.metallise ? "Plated," : "NonPlated,") + std::to_string(a) + "," + std::to_string(b) + "," + genre;
    p.nomFichier = p.contrePercage ? "contre-percage-" + std::to_string(a) + "-" + std::to_string(b)
                   : tout          ? (p.metallise ? "PTH" : "NPTH")
                                   : (p.metallise ? "PTH-" : "NPTH-") + std::to_string(a) + "-" + std::to_string(b);
    r.push_back(p);
  }
  return r;
}

std::string excellonPaire(const ModeleFabrication& m, const PairePercage& paire, const OptionsExcellon& o, const std::string& version) {
  const int n = std::max(1, m.couchesCuivre);
  // Outils : un par diamètre (et par forme : une fente et un trou rond du même diamètre partagent l'outil, comme le
  // veulent les fabricants), dans l'ordre des diamètres
  std::map<Nm, std::vector<const TrouFab*>> parOutil;
  std::map<Nm, std::string> fonctions;
  for (const auto& t : m.trous) {
    if (!dansPaire(t, paire, n) && !(traversant(t, n) && !paire.contrePercage && t.metallise == paire.metallise && numero(paire.de, n) == 1 && numero(paire.a, n) == n)) continue;
    parOutil[t.diametre].push_back(&t);
    auto& f = fonctions[t.diametre];
    if (f.empty()) f = t.fonction;
    else if (f != t.fonction) f = "Other";
  }
  const double facteur = o.pouces ? 1.0 / 25.4e6 : 1e-6;
  const int dec = std::clamp(o.decimales, 2, 6);
  auto nombre = [&](double v) {
    char t[48];
    std::snprintf(t, sizeof t, "%.*f", dec, v);
    return std::string(t);
  };
  auto coord = [&](Pt p) {
    const Pt q = versRepereFabrication(m.repere, p);
    if (o.zerosEntiers) {
      // Entiers à zéros de tête gardés, format 3.3 (mm) ou 2.4 (pouces) : la virgule se lit à sa place
      const double ech = o.pouces ? 1e4 : 1e3;
      char t[64];
      std::snprintf(t, sizeof t, "X%+07lldY%+07lld", static_cast<long long>(std::llround(static_cast<double>(q.x) * facteur * ech)),
                    static_cast<long long>(std::llround(static_cast<double>(q.y) * facteur * ech)));
      std::string s = t;
      s.erase(std::remove(s.begin(), s.end(), '+'), s.end());
      return s;
    }
    return "X" + nombre(static_cast<double>(q.x) * facteur) + "Y" + nombre(static_cast<double>(q.y) * facteur);
  };
  std::ostringstream s;
  if (!o.eia) {
    s << "M48\n";
    if (o.attributs) {
      s << "; Percage " << (paire.contrePercage ? "contre-percage" : paire.metallise ? "metallise" : "non metallise") << " - " << sur(m.nom) << "\n";
      s << "; #@! TF.GenerationSoftware,Promethee,Promethee," << sur(version) << "\n";
      s << "; #@! TF.FileFunction," << paire.fonction << "\n";
    }
    if (!o.xnc) s << "FMAT,2\n";
    s << (o.pouces ? "INCH" : "METRIC") << (o.zerosEntiers ? ",LZ" : "") << "\n";
  }
  int t = 1;
  for (const auto& [diametre, trous] : parOutil) {
    const std::string& f = fonctions[diametre];
    if (o.attributs && !o.eia) s << "; #@! TA.AperFunction," << (paire.metallise ? "Plated," : "NonPlated,") << (f.empty() ? std::string("Other") : f) << "\n";
    s << "T" << t++ << "C" << nombre(static_cast<double>(diametre) * facteur) << "\n";
  }
  s << (o.eia ? "" : "%\n") << "G90\nG05\n";
  t = 1;
  for (const auto& [diametre, trous] : parOutil) {
    s << "T" << t++ << "\n";
    // Trous ronds d'abord, puis les fentes de l'outil
    for (const TrouFab* x : trous)
      if (!x->fente()) s << coord(x->p) << "\n";
    for (const TrouFab* x : trous) {
      if (!x->fente()) continue;
      const auto [a, b] = boutsFente(*x);
      if (o.g85 && !o.xnc) s << coord(a) << "G85" << coord(b) << "\n";
      else s << "G00" << coord(a) << "\nM15\nG01" << coord(b) << "\nM16\nG05\n";  // détourée : plongée, déplacement, remontée
    }
  }
  s << "T0\nM30\n";
  return s.str();
}

std::string gerberPercage(const ModeleFabrication& m, const PairePercage& paire, const std::string& version) {
  const int n = std::max(1, m.couchesCuivre);
  std::ostringstream s;
  s << "G04 Promethee : percage " << sur(m.nom) << "*\n";
  s << "%TF.GenerationSoftware,Promethee,Promethee," << sur(version) << "*%\n";
  if (!m.uuid.empty()) s << "%TF.ProjectId," << sur(m.nom) << "," << sur(m.uuid) << "," << sur(m.revision.empty() ? std::string("rev?") : m.revision) << "*%\n";
  s << "%TF.SameCoordinates,Original*%\n%TF.FileFunction," << paire.fonction << "*%\n%TF.FilePolarity,Positive*%\n%FSLAX46Y46*%\n%MOMM*%\n%LPD*%\nG01*\n";
  std::map<std::pair<Nm, std::string>, std::vector<const TrouFab*>> parOutil;
  for (const auto& t : m.trous)
    if (dansPaire(t, paire, n) || (traversant(t, n) && !paire.contrePercage && t.metallise == paire.metallise && numero(paire.de, n) == 1 && numero(paire.a, n) == n))
      parOutil[{t.diametre, t.fonction}].push_back(&t);
  auto xy = [&m](Pt p) {
    const Pt q = versRepereFabrication(m.repere, p);
    return "X" + std::to_string(q.x) + "Y" + std::to_string(q.y);
  };
  int d = 10;
  for (const auto& [cle, trous] : parOutil) {
    s << "%TA.AperFunction," << (cle.second.empty() ? std::string("ComponentDrill") : cle.second) << "*%\n%ADD" << d << "C," << decimal(cle.first) << "*%\n%TD*%\nD" << d << "*\n";
    for (const TrouFab* t : trous) {
      if (t->fente()) {
        const auto [a, b] = boutsFente(*t);
        s << xy(a) << "D02*\n" << xy(b) << "D01*\n";
      } else s << xy(t->p) << "D03*\n";
    }
    ++d;
  }
  s << "M02*\n";
  return s.str();
}

std::vector<LignePercage> tableauPercages(const ModeleFabrication& m) {
  const int n = std::max(1, m.couchesCuivre);
  std::map<std::tuple<Nm, Nm, bool, std::string, std::string>, LignePercage> lignes;
  for (const auto& t : m.trous) {
    const std::string paire = traversant(t, n) ? "1-" + std::to_string(n) : std::to_string(numero(t.de, n)) + "-" + std::to_string(numero(t.a, n));
    auto& l = lignes[{t.diametre, t.fente() ? t.longueur : 0, t.metallise, paire, t.fonction}];
    l.diametre = t.diametre;
    l.longueur = t.fente() ? t.longueur : 0;
    l.tolerance = std::max(l.tolerance, t.tolerance);
    l.metallise = t.metallise;
    l.paire = paire;
    l.fonction = t.fonction;
    ++l.nombre;
  }
  std::vector<LignePercage> r;
  int outil = 1;
  for (auto& [cle, l] : lignes) {
    l.outil = outil++;
    r.push_back(l);
  }
  return r;
}

std::string tableauPercagesCsv(const ModeleFabrication& m) {
  prom::EcrivainCsv w;  // écrivain du poste (10.20)
  w.ligne({"Outil", "Diamètre (mm)", "Longueur de fente (mm)", "Tolérance (mm)", "Métallisé", "Nombre", "Couches", "Fonction"});
  for (const auto& l : tableauPercages(m)) {
    w.champ("T" + std::to_string(l.outil)).fixe(mm(l.diametre), 3);
    if (l.longueur) w.fixe(mm(l.longueur), 3);
    else w.champ("");
    std::string tol;
    if (l.tolerance) {
      char t[32];
      std::snprintf(t, sizeof t, "±%.3f", mm(l.tolerance));
      tol = t;
      std::replace(tol.begin(), tol.end(), '.', w.reglages().decimale);
    }
    w.champ(tol);
    w.champ(l.metallise ? "oui" : "non").entier(l.nombre).champ(l.paire).champ(l.fonction).fin();
  }
  return w.texte();
}

std::string tableauPercagesJson(const ModeleFabrication& m) {
  nlohmann::ordered_json a = nlohmann::ordered_json::array();
  for (const auto& l : tableauPercages(m)) {
    nlohmann::ordered_json y = {{"outil", l.outil}, {"diametre", mm(l.diametre)}, {"metallise", l.metallise}, {"nombre", l.nombre}, {"couches", l.paire}, {"fonction", l.fonction}};
    if (l.longueur) y["longueurFente"] = mm(l.longueur);
    if (l.tolerance) y["tolerance"] = mm(l.tolerance);
    a.push_back(y);
  }
  return nlohmann::ordered_json{{"carte", m.nom}, {"percages", a}}.dump(2) + "\n";
}

std::string planPercageSvg(const ModeleFabrication& m, const std::string& titre) {
  // Plan de perçage : contour, chaque trou marqué du symbole de son outil (cercle, carré, triangle, croix…) et le
  // tableau des outils sous la carte ; échelle 1, millimètres
  const auto table = tableauPercages(m);
  std::map<std::tuple<Nm, Nm, bool>, int> outilDe;
  for (const auto& l : table) outilDe[{l.diametre, l.longueur, l.metallise}] = l.outil;
  const double L = mm(m.boite.largeur()), H = mm(m.boite.hauteur());
  const double hauteurTable = 8 + 5.0 * static_cast<double>(table.size());
  std::ostringstream s;
  s.setf(std::ios::fixed);
  s.precision(3);
  auto X = [&](Pt p) { return mm(p.x - m.boite.x0) + 5; };
  auto Y = [&](Pt p) { return mm(p.y - m.boite.y0) + 5; };
  s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << L + 10 << "mm\" height=\"" << H + 10 + hauteurTable << "mm\" viewBox=\"0 0 " << L + 10 << " "
    << H + 10 + hauteurTable << "\">\n<title>" << titre << "</title>\n<rect width=\"100%\" height=\"100%\" fill=\"#FFFFFF\"/>\n";
  s << "<path fill=\"none\" stroke=\"#000000\" stroke-width=\"0.15\" d=\"M";
  for (std::size_t i = 0; i < m.contour.size(); ++i) s << (i ? " L" : "") << X(m.contour[i]) << " " << Y(m.contour[i]);
  s << " Z\"/>\n<g fill=\"none\" stroke=\"#1F4FD8\" stroke-width=\"0.1\">\n";
  auto symbole = [&](int outil, double x, double y, double r) {
    switch ((outil - 1) % 5) {
      case 0: s << "<circle cx=\"" << x << "\" cy=\"" << y << "\" r=\"" << r << "\"/>"; break;
      case 1: s << "<rect x=\"" << x - r << "\" y=\"" << y - r << "\" width=\"" << 2 * r << "\" height=\"" << 2 * r << "\"/>"; break;
      case 2: s << "<path d=\"M" << x << " " << y - r << " L" << x + r << " " << y + r << " L" << x - r << " " << y + r << " Z\"/>"; break;
      case 3: s << "<path d=\"M" << x - r << " " << y - r << " L" << x + r << " " << y + r << " M" << x + r << " " << y - r << " L" << x - r << " " << y + r << "\"/>"; break;
      default: s << "<path d=\"M" << x - r << " " << y << " L" << x + r << " " << y << " M" << x << " " << y - r << " L" << x << " " << y + r << "\"/>"; break;
    }
    if (outil > 5) s << "<text x=\"" << x + r << "\" y=\"" << y - r << "\" font-size=\"0.8\" fill=\"#1F4FD8\" stroke=\"none\">" << outil << "</text>";
    s << "\n";
  };
  for (const auto& t : m.trous) {
    const int outil = outilDe[{t.diametre, t.fente() ? t.longueur : 0, t.metallise}];
    if (t.fente()) {
      const auto [a, b] = boutsFente(t);
      s << "<line x1=\"" << X(a) << "\" y1=\"" << Y(a) << "\" x2=\"" << X(b) << "\" y2=\"" << Y(b) << "\" stroke-width=\"" << mm(t.diametre) << "\" stroke-linecap=\"round\" stroke-opacity=\"0.25\"/>\n";
    }
    symbole(outil, X(t.p), Y(t.p), std::max(0.4, mm(t.diametre) / 2));
  }
  s << "</g>\n<g font-family=\"sans-serif\" font-size=\"2.4\" fill=\"#000000\">\n";
  double y = H + 14;
  s << "<text x=\"5\" y=\"" << y << "\">Outil · diamètre · nombre · couches · métallisé</text>\n";
  for (const auto& l : table) {
    y += 5;
    s << "<g fill=\"none\" stroke=\"#1F4FD8\" stroke-width=\"0.15\">";
    symbole(l.outil, 7, y - 0.8, 1.0);
    s << "</g><text x=\"10\" y=\"" << y << "\">T" << l.outil << " · ⌀" << decimal(l.diametre, 2) << (l.longueur ? " × " + decimal(l.longueur, 2) : std::string()) << " mm · " << l.nombre << " · "
      << l.paire << " · " << (l.metallise ? "oui" : "non") << "</text>\n";
  }
  s << "</g>\n</svg>\n";
  return s.str();
}

std::string rapportPairesPercage(const ModeleFabrication& m) {
  std::ostringstream s;
  s << "Paires de perçage — " << m.nom << " (" << m.couchesCuivre << " cuivres)\n";
  for (const auto& p : pairesPercage(m)) {
    const int n = std::max(1, m.couchesCuivre);
    s << "  " << p.nomFichier << " : couches " << numero(p.de, n) << " à " << numero(p.a, n) << " (" << codeCouche(p.de) << " → " << codeCouche(p.a) << "), " << p.trous << " trou(s), "
      << p.fonction << "\n";
  }
  return s.str();
}

int ramenerAuxForets(ModeleFabrication& m, const std::vector<Nm>& forets, Nm tolerance, std::vector<std::string>* sansForet) {
  if (forets.empty()) return 0;
  int changes = 0;
  std::set<Nm> signales;
  for (auto& t : m.trous) {
    const auto it = std::min_element(forets.begin(), forets.end(), [&](Nm a, Nm b) { return std::llabs(a - t.diametre) < std::llabs(b - t.diametre); });
    const Nm ecart = std::llabs(*it - t.diametre);
    if (ecart == 0) continue;
    if (ecart <= tolerance) {
      t.diametre = *it;
      ++changes;
    } else if (sansForet && signales.insert(t.diametre).second)
      sansForet->push_back("diamètre " + decimal(t.diametre, 3) + " mm : aucun foret du profil à moins de " + decimal(tolerance, 3) + " mm");
  }
  return changes;
}

std::string gerberComposants(const ConceptionElec& c, bool dessous, const std::string& version) {
  // Gerber X3 (8.7) : un flash par composant au point de placement, avec ses attributs ; ses broches en flashs
  // .P ; le contour du corps en trait. Repère de fabrication commun (origine au coin bas gauche)
  const Boite carte = boite(c.circuit.contour.polygone());
  std::ostringstream s;
  auto xy = [&](Pt p) {
    const Pt q = versRepereFabrication(repereFichiers(c), p);
    return "X" + std::to_string(q.x) + "Y" + std::to_string(q.y);
  };
  s << "G04 Promethee : composants " << (dessous ? "dessous" : "dessus") << " - " << sur(c.nom) << "*\n";
  s << "%TF.GenerationSoftware,Promethee,Promethee," << sur(version) << "*%\n%TF.SameCoordinates,Original*%\n";
  s << "%TF.FileFunction,Component,L" << (dessous ? std::max(1, c.circuit.empilage.couchesCuivre) : 1) << "," << (dessous ? "Bot" : "Top") << "*%\n";
  s << "%TF.FilePolarity,Positive*%\n%FSLAX46Y46*%\n%MOMM*%\n%LPD*%\nG01*\n";
  s << "%TA.AperFunction,ComponentMain*%\n%ADD10C,0.300000*%\n%TD*%\n";
  s << "%TA.AperFunction,ComponentPin*%\n%ADD11P,0.360000X4X0.0*%\n%TD*%\n";
  s << "%TA.AperFunction,ComponentOutline,Body*%\n%ADD12C,0.100000*%\n%TD*%\n";
  for (const auto& e : c.circuit.empreintes) {
    if (e.dessous != dessous || !dansFichierPlacement(c, e)) continue;
    const ComposantElec* x = c.composant(e.composant);
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    if (!x || !d) continue;
    double rot = std::fmod(e.rotation, 360.0);
    if (rot < 0) rot += 360;
    char r[32];
    std::snprintf(r, sizeof r, "%.1f", rot);
    s << "%TO.C," << sur(x->reference) << "*%\n%TO.CRot," << r << "*%\n";
    if (!x->fabricant.empty()) s << "%TO.CMfr," << sur(x->fabricant) << "*%\n";
    if (!x->mpn.empty()) s << "%TO.CMPN," << sur(x->mpn) << "*%\n";
    if (!x->valeur.empty()) s << "%TO.CVal," << sur(x->valeur) << "*%\n";
    bool traversant = false;
    for (const auto& p : d->pastilles) traversant |= p.type == TypePastille::Traversante;
    s << "%TO.CMnt," << (traversant ? "TH" : "SMD") << "*%\n%TO.CFtp," << sur(e.empreinte) << "*%\n";
    char h[32];
    std::snprintf(h, sizeof h, "%.3f", hauteurComposant(x, d).mm);
    s << "%TO.CHgt," << h << "*%\n";
    s << "D10*\n" << xy(versCarte(e, pointDePlacement(*d))) << "D03*\n";
    s << "D11*\n";
    for (const auto& p : d->pastilles) {
      if (p.type == TypePastille::NonMetallisee || p.numero.empty()) continue;
      s << "%TO.P," << sur(x->reference) << "," << sur(p.numero) << "*%\n" << xy(versCarte(e, p.position)) << "D03*\n%TD.P*%\n";
    }
    // Corps : le rectangle de son emprise
    const Boite b = empriseCorps(*d);
    s << "D12*\n" << xy(versCarte(e, {b.x0, b.y0})) << "D02*\n" << xy(versCarte(e, {b.x1, b.y0})) << "D01*\n" << xy(versCarte(e, {b.x1, b.y1})) << "D01*\n" << xy(versCarte(e, {b.x0, b.y1}))
      << "D01*\n" << xy(versCarte(e, {b.x0, b.y0})) << "D01*\n";
    s << "%TD*%\n";
  }
  s << "M02*\n";
  return s.str();
}

bool aDesViasRemplies(const ConceptionElec& c) {
  return std::any_of(c.circuit.vias.begin(), c.circuit.vias.end(), [](const Via& v) { return !v.remplissage.empty(); });
}

std::string gerberViaFill(const ModeleFabrication& m, const ConceptionElec& c, const std::string& version) {
  std::ostringstream s;
  auto xy = [&m](Pt p) {
    const Pt q = versRepereFabrication(m.repere, p);
    return "X" + std::to_string(q.x) + "Y" + std::to_string(q.y);
  };
  s << "G04 Promethee : vias a remplir (IPC-4761) - " << sur(m.nom) << "*\n";
  s << "%TF.GenerationSoftware,Promethee,Promethee," << sur(version) << "*%\n%TF.SameCoordinates,Original*%\n%TF.FileFunction,Viafill*%\n%TF.FilePolarity,Positive*%\n";
  s << "%FSLAX46Y46*%\n%MOMM*%\n%LPD*%\nG01*\n";
  std::map<std::pair<Nm, std::string>, std::vector<Pt>> parType;
  for (const auto& v : c.circuit.vias)
    if (!v.remplissage.empty()) parType[{v.percage, v.remplissage}].push_back(v.p);
  int d = 10;
  for (const auto& [cle, points] : parType) {
    s << "G04 IPC-4761 type " << sur(cle.second) << "*\n%TA.AperFunction,ViaDrill*%\n%ADD" << d << "C," << decimal(cle.first) << "*%\n%TD*%\nD" << d << "*\n";
    for (const Pt& p : points) s << xy(p) << "D03*\n";
    ++d;
  }
  s << "M02*\n";
  return s.str();
}

UsinagesCarte usinagesCarte(const ConceptionElec& c) {
  UsinagesCarte u;
  for (const auto& e : c.circuit.empreintes)
    if (const DefEmpreinte* d = c.empreinte(e.empreinte))
      for (const auto& p : d->pastilles) {
        if (p.fabrication == "castellee") {
          u.castellee = true;
          ++u.demiTrous;
        }
        if (p.fabrication == "connecteurBord") u.connecteurBord = true;
        if (p.lamageDessus || p.lamageDessous) u.lamages = true;
      }
  for (const auto& t : c.circuit.contour.traitements) {
    switch (t.genre) {
      case ContourCarte::Traitement::Genre::Biseau: u.biseau = u.connecteurBord = true; break;
      case ContourCarte::Traitement::Genre::BordMetallise: u.bordMetallise = true; break;
      case ContourCarte::Traitement::Genre::DemiTrous: u.castellee = true; break;
    }
  }
  return u;
}

}  // namespace prom::ecad

// Prométhée : générateur IPC étendu et séries d'empreintes par nom (lot L4, tâches 4.25 et 4.26).
// Licence GPL-3.0-only.
#include "promethee/gabarits_ipc.hpp"
#include "promethee/synchronisation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <regex>
#include <set>

namespace prom::ecad {

namespace {
char lettre(DensiteIPC d) { return d == DensiteIPC::Plus ? 'M' : d == DensiteIPC::Nominale ? 'N' : 'L'; }
double arrondi(double v, double pas) { return std::round(v / pas) * pas; }
double haut(double v, double pas) { return std::ceil(v / pas - 1e-9) * pas; }
double lire(const std::string& s) { return std::atof(s.c_str()); }
Pastille pastille(const std::string& n, double x, double y, double lx, double ly, FormePastille f = FormePastille::RectArrondi) {
  Pastille q;
  q.numero = n;
  q.type = TypePastille::CMS;
  q.forme = f;
  q.position = ptMm(x, y);
  q.largeur = nm(lx);
  q.hauteur = nm(ly);
  return q;
}
void trait(DefEmpreinte& d, double x0, double y0, double x1, double y1, Couche c = Couche::SerigraphieDessus) {
  TraitEmpreinte t;
  t.couche = c;
  t.a = ptMm(x0, y0);
  t.b = ptMm(x1, y1);
  d.traits.push_back(t);
}
// Encombrement : pastilles et corps, marge de la densité, au 0,05 mm
void encombrer(DefEmpreinte& d, double corpsX, double corpsY, DensiteIPC dens, double margeN = 0.25) {
  double ex = corpsX / 2, ey = corpsY / 2;
  for (const auto& q : d.pastilles) {
    const double rx = mm(q.largeur) / 2, ry = mm(q.hauteur) / 2;
    const double a = q.rotation * 3.14159265358979 / 180;
    const double hx = std::abs(std::cos(a)) * rx + std::abs(std::sin(a)) * ry, hy = std::abs(std::sin(a)) * rx + std::abs(std::cos(a)) * ry;
    ex = std::max(ex, std::abs(mm(q.position.x)) + hx);
    ey = std::max(ey, std::abs(mm(q.position.y)) + hy);
  }
  const double marge = dens == DensiteIPC::Plus ? 2 * margeN : dens == DensiteIPC::Nominale ? margeN : margeN * 0.4;
  ex = haut(ex + marge, 0.05);
  ey = haut(ey + marge, 0.05);
  d.encombrement = Boite{nm(-ex), nm(-ey), nm(ex), nm(ey)};
}
void corps(DefEmpreinte& d, double l, double w, double h, const char* couleur = "#2A2D34") {
  d.corps.largeur = nm(l);
  d.corps.profondeur = nm(w);
  d.corps.hauteur = nm(h);
  d.corps.couleur = couleur;
}
// Contour de sérigraphie du corps et repère de la broche 1 (coin haut gauche)
void serigraphieCorps(DefEmpreinte& d, double bx, double by, bool repere = true) {
  trait(d, -bx, -by, bx, -by);
  trait(d, bx, -by, bx, by);
  trait(d, bx, by, -bx, by);
  trait(d, -bx, by, -bx, -by);
  if (repere) {
    TraitEmpreinte r;
    r.couche = Couche::SerigraphieDessus;
    r.a = r.b = ptMm(-bx - 0.4, -by - 0.4);
    r.rayon = nm(0.1);
    d.traits.push_back(r);
  }
}
// Pastilles écartées du plot central (4.25) : le bout intérieur de chaque pastille reculé jusqu'à garder « ecart »
void ecarterDuPlot(DefEmpreinte& d, const std::string& plot, double ecart) {
  const auto it = std::find_if(d.pastilles.begin(), d.pastilles.end(), [&](const Pastille& p) { return p.numero == plot; });
  if (it == d.pastilles.end()) return;
  const double px = mm(it->largeur) / 2, py = mm(it->hauteur) / 2;
  for (auto& p : d.pastilles) {
    if (p.numero == plot) continue;
    const double x = mm(p.position.x), y = mm(p.position.y), lx = mm(p.largeur), ly = mm(p.hauteur);
    // Pastille d'un côté gauche ou droit : longue selon x ; d'un côté haut ou bas : selon y
    if (lx >= ly && std::abs(x) > px) {
      const double interieur = std::abs(x) - lx / 2, voulu = px + ecart;
      if (interieur < voulu) {
        const double nouvelle = lx - (voulu - interieur);
        if (nouvelle > 0.1) {
          const double ext = std::abs(x) + lx / 2;
          p.largeur = nm(nouvelle);
          p.position.x = nm((x > 0 ? 1 : -1) * (ext - nouvelle / 2));
        }
      }
    } else if (ly > lx && std::abs(y) > py) {
      const double interieur = std::abs(y) - ly / 2, voulu = py + ecart;
      if (interieur < voulu) {
        const double nouvelle = ly - (voulu - interieur);
        if (nouvelle > 0.1) {
          const double ext = std::abs(y) + ly / 2;
          p.hauteur = nm(nouvelle);
          p.position.y = nm((y > 0 ? 1 : -1) * (ext - nouvelle / 2));
        }
      }
    }
  }
}
// Pattes rognées (4.25) : entre deux pastilles voisines d'une rangée, au moins « ecart » de cuivre libre
void rognerRangees(DefEmpreinte& d, double pas, double ecart) {
  for (auto& p : d.pastilles) {
    const double lx = mm(p.largeur), ly = mm(p.hauteur);
    if (lx >= ly && ly > pas - ecart) p.hauteur = nm(pas - ecart);
    else if (ly > lx && lx > pas - ecart) p.largeur = nm(pas - ecart);
  }
}
// Pâte du plot central en motif (4.24) : grille de fenêtres, taux de couverture de 50 à 60 %
void motifPlot(DefEmpreinte& d, const std::string& plot) {
  for (auto& p : d.pastilles)
    if (p.numero == plot) {
      MotifPate m;
      const double c = std::max(mm(p.largeur), mm(p.hauteur));
      m.lignes = m.colonnes = c > 4 ? 4 : c > 2.5 ? 3 : 2;
      m.ecart = 200'000;
      m.couverture = 0.55;
      p.motifPate = m;
    }
}
// Cotes JEDEC des petits boîtiers à deux rangées (aile de mouette)
struct Jedec {
  double pas, lMin, lMax, tMin, tMax, wMin, wMax, corpsW, hauteur;
};
std::optional<Jedec> jedec(const std::string& famille, int broches) {
  if (famille == "SOIC") return Jedec{1.27, 5.80, 6.20, 0.40, 1.27, 0.31, 0.51, 3.9, 1.75};
  if (famille == "SOIC-W") return Jedec{1.27, 10.00, 10.65, 0.40, 1.27, 0.31, 0.51, 7.5, 2.65};
  if (famille == "TSSOP") return Jedec{0.65, 6.20, 6.60, 0.45, 0.75, 0.19, 0.30, 4.4, 1.2};
  if (famille == "SSOP") return Jedec{0.65, 7.40, 8.20, 0.55, 0.95, 0.22, 0.38, 5.3, 2.0};
  if (famille == "MSOP") return broches >= 10 ? Jedec{0.50, 4.75, 5.05, 0.40, 0.70, 0.17, 0.27, 3.0, 1.1} : Jedec{0.65, 4.75, 5.05, 0.40, 0.70, 0.22, 0.38, 3.0, 1.1};
  return std::nullopt;
}
double longueurCorps(const std::string& famille, int broches, double pas) {
  static const std::map<std::string, std::map<int, double>> t = {
      {"SOIC", {{8, 4.9}, {14, 8.65}, {16, 9.9}}},  {"SOIC-W", {{16, 10.3}, {18, 11.55}, {20, 12.8}, {24, 15.4}, {28, 17.9}}},
      {"TSSOP", {{8, 3.0}, {14, 5.0}, {16, 5.0}, {20, 6.5}, {24, 7.8}, {28, 9.7}}}, {"SSOP", {{16, 6.2}, {20, 7.2}, {24, 8.2}, {28, 10.2}}},
      {"MSOP", {{8, 3.0}, {10, 3.0}, {12, 4.0}}}};
  if (const auto f = t.find(famille); f != t.end())
    if (const auto b = f->second.find(broches); b != f->second.end()) return b->second;
  return (broches / 2) * pas + 0.4;
}
// Largeur des pattes d'un boîtier quatre côtés ou sans patte selon le pas
std::pair<double, double> largeurPatte(double pas, bool sansPatte) {
  if (sansPatte) return pas <= 0.41 ? std::pair{0.15, 0.25} : pas <= 0.51 ? std::pair{0.18, 0.30} : std::pair{0.25, 0.35};
  return pas <= 0.41 ? std::pair{0.13, 0.23} : pas <= 0.51 ? std::pair{0.17, 0.27} : pas <= 0.66 ? std::pair{0.22, 0.38} : std::pair{0.30, 0.45};
}
// Puces : longueur, largeur, terminaison (EIA)
std::optional<std::array<double, 3>> puce(const std::string& code) {
  static const std::map<std::string, std::array<double, 3>> t = {{"0201", {0.6, 0.3, 0.15}}, {"0402", {1.0, 0.5, 0.25}}, {"0603", {1.6, 0.8, 0.3}}, {"0805", {2.0, 1.25, 0.4}},
                                                               {"1206", {3.2, 1.6, 0.5}}, {"1210", {3.2, 2.5, 0.5}}, {"1812", {4.5, 3.2, 0.6}}, {"2010", {5.0, 2.5, 0.6}}, {"2512", {6.3, 3.2, 0.6}}};
  const auto it = t.find(code);
  if (it == t.end()) return std::nullopt;
  return it->second;
}
}  // namespace

std::string lettreRangee(int rang) {
  static const std::string a = "ABCDEFGHJKLMNPRTUVWY";
  const int n = static_cast<int>(a.size());
  if (rang < 0) return {};
  if (rang < n) return std::string(1, a[static_cast<std::size_t>(rang)]);
  return std::string(1, a[static_cast<std::size_t>(rang / n - 1)]) + a[static_cast<std::size_t>(rang % n)];
}

std::optional<ParamsGabarit> gabaritDepuisNom(const std::string& brut) {
  std::string nom = brut;
  if (const auto i = nom.find(':'); i != std::string::npos) nom = nom.substr(i + 1);
  std::smatch m;
  ParamsGabarit p;
  // Matrices de billes ou de plots : « BGA-256_17.0x17.0mm_Layout16x16_P1.0mm[_Ball0.5mm][_Pad0.4mm] »
  static const std::regex matrice(R"(^(BGA|LGA|WLCSP)-(\d+)_([\d.]+)x([\d.]+)mm_Layout(\d+)x(\d+)_P([\d.]+)mm(?:_Ball([\d.]+)mm)?(?:_Pad([\d.]+)mm)?.*$)");
  if (std::regex_match(nom, m, matrice)) {
    p.genre = m[1] == "BGA" ? GenreGabarit::BGA : m[1] == "LGA" ? GenreGabarit::LGA : GenreGabarit::WLCSP;
    p.corpsL = lire(m[3]);
    p.corpsW = lire(m[4]);
    p.colonnes = std::stoi(m[5]);
    p.rangees = std::stoi(m[6]);
    p.pas = lire(m[7]);
    if (m[8].matched) p.bille = lire(m[8]);
    if (m[9].matched) p.pastille = lire(m[9]);
    p.hauteur = p.genre == GenreGabarit::BGA ? 1.6 : p.genre == GenreGabarit::LGA ? 1.0 : 0.6;
    // Billes absentes au centre : le compte du nom le dit
    const int total = std::stoi(m[2]);
    if (total < p.rangees * p.colonnes) {
      // Retire un carré central de billes jusqu'au compte annoncé
      const int manque = p.rangees * p.colonnes - total;
      const int cote = static_cast<int>(std::lround(std::sqrt(static_cast<double>(manque))));
      if (cote * cote == manque)
        for (int r = (p.rangees - cote) / 2; r < (p.rangees - cote) / 2 + cote; ++r)
          for (int c = (p.colonnes - cote) / 2; c < (p.colonnes - cote) / 2 + cote; ++c) p.sansBille.push_back(lettreRangee(r) + std::to_string(c + 1));
    }
    return p;
  }
  // Sans patte à deux rangées : « DFN-8-3x3-0.65[-EP1.6x2.4] », « SON-10-3x3-0.5 »
  static const std::regex dfn(R"(^(DFN|SON)-(\d+)-([\d.]+)x([\d.]+)-([\d.]+)(?:-EP([\d.]+)x([\d.]+))?$)");
  if (std::regex_match(nom, m, dfn)) {
    p.genre = m[1] == "DFN" ? GenreGabarit::DFN : GenreGabarit::SON;
    p.broches = std::stoi(m[2]);
    p.corpsW = lire(m[3]);
    p.corpsL = lire(m[4]);
    p.pas = lire(m[5]);
    p.plot = m[6].matched ? lire(m[6]) : std::max(0.0, p.corpsW - 1.4);
    p.plotL = m[7].matched ? lire(m[7]) : std::max(0.0, (p.broches / 2 - 1) * p.pas + 0.2);
    p.hauteur = 0.9;
    return p;
  }
  // Petits boîtiers à deux rangées : « SOIC-8 », « TSSOP-14 », « SSOP-28 », « MSOP-10 », « SOIC-16W »
  static const std::regex deuxRangees(R"(^(SOIC|TSSOP|SSOP|MSOP)-(\d+)(W)?(?:_.*)?$)");
  if (std::regex_match(nom, m, deuxRangees)) {
    const std::string famille = std::string(m[1]) + (m[3].matched ? "-W" : "");
    const int n = std::stoi(m[2]);
    const auto j = jedec(famille, n);
    if (!j || n % 2) return std::nullopt;
    p.cotes.gabarit = GabaritIPC::AileDeMouette;
    p.cotes.broches = n;
    p.cotes.pas = j->pas;
    p.cotes.lMin = j->lMin;
    p.cotes.lMax = j->lMax;
    p.cotes.tMin = j->tMin;
    p.cotes.tMax = j->tMax;
    p.cotes.wMin = j->wMin;
    p.cotes.wMax = j->wMax;
    p.cotes.corpsW = j->corpsW;
    p.cotes.corpsL = longueurCorps(famille, n, j->pas);
    p.cotes.hauteur = j->hauteur;
    return p;
  }
  // Quatre côtés : « QFN-24-4x4-0.5[-EP2.6x2.6] », « TQFP-64-10x10-0.5 », « LQFP-48-7x7-0.5 »
  static const std::regex quatre(R"(^(QFN|TQFP|LQFP|VQFN)-(\d+)-([\d.]+)x([\d.]+)-([\d.]+)(?:-EP([\d.]+)x([\d.]+))?$)");
  if (std::regex_match(nom, m, quatre)) {
    const bool sansPatte = m[1] == "QFN" || m[1] == "VQFN";
    const double b = lire(m[3]), pas = lire(m[5]);
    const auto [wMin, wMax] = largeurPatte(pas, sansPatte);
    p.cotes.gabarit = sansPatte ? GabaritIPC::SansPatte : GabaritIPC::AileDeMouette;
    p.cotes.quatreCotes = true;
    p.cotes.broches = std::stoi(m[2]);
    p.cotes.pas = pas;
    p.cotes.lMin = sansPatte ? b - 0.05 : b + 2 - 0.2;
    p.cotes.lMax = sansPatte ? b + 0.05 : b + 2 + 0.2;
    p.cotes.tMin = sansPatte ? 0.30 : 0.45;
    p.cotes.tMax = sansPatte ? 0.50 : 0.75;
    p.cotes.wMin = wMin;
    p.cotes.wMax = wMax;
    p.cotes.corpsW = b;
    p.cotes.corpsL = lire(m[4]);
    p.cotes.hauteur = sansPatte ? 0.9 : m[1] == "LQFP" ? 1.6 : 1.2;
    if (sansPatte) p.cotes.plotCentral = m[6].matched ? lire(m[6]) : std::max(0.0, b - 1.3);
    return p;
  }
  // SOT-23 à six pattes : aile de mouette au pas de 0,95 ; SOT-23 et SOT-23-5 : la même base, pattes retirées
  static const std::regex sot(R"(^SOT-23(?:-(\d))?$)");
  if (std::regex_match(nom, m, sot)) {
    p.cotes.gabarit = GabaritIPC::AileDeMouette;
    p.cotes.broches = 6;
    p.cotes.pas = 0.95;
    p.cotes.lMin = 2.10;
    p.cotes.lMax = 2.64;
    p.cotes.tMin = 0.30;
    p.cotes.tMax = 0.60;
    p.cotes.wMin = 0.30;
    p.cotes.wMax = 0.50;
    p.cotes.corpsW = 1.6;
    p.cotes.corpsL = 2.9;
    p.cotes.hauteur = 1.1;
    p.variante = m[1].matched ? "SOT-23-" + std::string(m[1]) : "SOT-23";
    return p;
  }
  // Puces : « R_0603 », « C_0805 », « L_1206 », « LED_0603 », « D_0603 », « Fuse_1206 »
  static const std::regex puces(R"(^(R|C|L|LED|D|Fuse)_(\d{4})(?:_.*)?$)");
  if (std::regex_match(nom, m, puces))
    if (const auto t = puce(m[2])) {
      p.cotes.gabarit = GabaritIPC::Puce;
      p.cotes.broches = 2;
      const double tol = (*t)[0] <= 1.0 ? 0.05 : 0.1;
      p.cotes.lMin = (*t)[0] - tol;
      p.cotes.lMax = (*t)[0] + tol;
      p.cotes.wMin = (*t)[1] - tol;
      p.cotes.wMax = (*t)[1] + tol;
      p.cotes.tMin = (*t)[2] - 0.1 > 0.05 ? (*t)[2] - 0.1 : 0.05;
      p.cotes.tMax = (*t)[2] + 0.1;
      p.cotes.corpsL = (*t)[0];
      p.cotes.corpsW = (*t)[1];
      p.cotes.hauteur = m[1] == "C" ? (*t)[1] : std::min(0.55, (*t)[1]);
      p.variante = m[1];
      return p;
    }
  // Diodes et redresseurs : « SOD-123 », « SOD-323 », « SOD-523 », « MiniMELF », « MELF », « SMA », « SMB », « SMC »
  static const std::map<std::string, std::array<double, 5>> deux = {
      // longueur hors tout, largeur du corps, longueur de terminaison, largeur de terminaison, hauteur
      {"SOD-123", {3.7, 1.6, 0.6, 0.6, 1.2}}, {"SOD-323", {2.5, 1.25, 0.4, 0.3, 1.0}}, {"SOD-523", {1.6, 0.8, 0.3, 0.3, 0.6}},
      {"MiniMELF", {3.5, 1.5, 0.45, 1.5, 1.5}}, {"MELF", {5.0, 2.5, 0.5, 2.5, 2.5}},
      {"SMA", {5.2, 2.6, 1.3, 1.45, 2.3}}, {"SMB", {5.4, 3.6, 1.3, 2.1, 2.3}}, {"SMC", {7.9, 5.9, 1.3, 3.0, 2.3}}};
  if (const auto it = deux.find(nom); it != deux.end()) {
    p.genre = nom.rfind("SOD", 0) == 0 ? GenreGabarit::SOD : nom.find("MELF") != std::string::npos ? GenreGabarit::MELF : GenreGabarit::SMx;
    p.variante = nom;
    p.corpsL = it->second[0];
    p.corpsW = it->second[1];
    p.plot = it->second[2];
    p.plotL = it->second[3];
    p.hauteur = it->second[4];
    p.broches = 2;
    return p;
  }
  if (nom == "SOT-89" || nom == "SOT-89-3") {
    p.genre = GenreGabarit::SOT89;
    p.broches = 3;
    p.pas = 1.5;
    p.corpsL = 4.5;
    p.corpsW = 2.5;
    p.hauteur = 1.5;
    return p;
  }
  if (nom == "TO-252" || nom == "DPAK" || nom == "TO-252-2" || nom == "TO-263" || nom == "D2PAK" || nom == "TO-263-2") {
    const bool grand = nom.find("263") != std::string::npos || nom == "D2PAK";
    p.genre = grand ? GenreGabarit::D2PAK : GenreGabarit::DPAK;
    p.broches = 3;
    p.pas = grand ? 2.54 : 2.28;
    p.corpsL = grand ? 10.2 : 6.6;
    p.corpsW = grand ? 9.2 : 6.1;
    p.hauteur = grand ? 4.5 : 2.3;
    return p;
  }
  // Quartz CMS à quatre plots : « Crystal_SMD_3225-4Pin », « Crystal_SMD_2520-4Pin », « Crystal_SMD_5032-4Pin »
  static const std::regex quartz(R"(^Crystal_SMD_(\d{2})(\d{2})-4Pin.*$)");
  if (std::regex_match(nom, m, quartz)) {
    p.genre = GenreGabarit::QuartzCMS;
    p.broches = 4;
    p.corpsL = lire(m[1]) / 10;
    p.corpsW = lire(m[2]) / 10;
    p.hauteur = 0.8;
    p.variante = std::string(m[1]) + std::string(m[2]);
    return p;
  }
  // PLCC : « PLCC-20 », « PLCC-28 », « PLCC-44 », « PLCC-68 », « PLCC-84 » (pattes en J, pas de 1,27)
  static const std::regex plcc(R"(^PLCC-(\d+)$)");
  if (std::regex_match(nom, m, plcc)) {
    p.genre = GenreGabarit::PLCC;
    p.broches = std::stoi(m[1]);
    if (p.broches % 4 || p.broches < 20) return std::nullopt;
    p.pas = 1.27;
    const int parCote = p.broches / 4;
    p.corpsL = p.corpsW = parCote * 1.27 + 2.6;
    p.hauteur = 4.4;
    return p;
  }
  // Connecteur de bord : « Edge-2x10_P2.54 » (doigts des deux faces), « Edge-1x20_P1.27 »
  static const std::regex bord(R"(^Edge-(\d)x(\d+)_P([\d.]+)$)");
  if (std::regex_match(nom, m, bord)) {
    p.genre = GenreGabarit::BordCarte;
    p.rangees = std::stoi(m[1]);
    p.broches = std::stoi(m[2]);
    p.pas = lire(m[3]);
    p.hauteur = 0;
    return p;
  }
  // Noms IPC : « SOIC127P600X175-8N », « QFN50P400X400X90-24N », « RESC1608X55N »
  static const std::regex ipc(R"(^(SOIC|SOP|TSOP|QFP|QFN|SON|DFN)(\d+)P(\d+)X(\d+)(?:X(\d+))?-(\d+)[MNL]?$)");
  if (std::regex_match(nom, m, ipc)) {
    const std::string f = m[1];
    const double pas = lire(m[2]) / 100, a = lire(m[3]) / 100, b = lire(m[4]) / 100;
    const int n = std::stoi(m[6]);
    const bool sansPatte = f == "QFN" || f == "SON" || f == "DFN";
    const bool quatreCotes = f == "QFP" || f == "QFN";
    const auto [wMin, wMax] = largeurPatte(pas, sansPatte);
    p.cotes.gabarit = sansPatte ? GabaritIPC::SansPatte : GabaritIPC::AileDeMouette;
    p.cotes.quatreCotes = quatreCotes;
    p.cotes.broches = n;
    p.cotes.pas = pas;
    p.cotes.lMin = a - (sansPatte ? 0.05 : 0.2);
    p.cotes.lMax = a + (sansPatte ? 0.05 : 0.2);
    p.cotes.tMin = sansPatte ? 0.3 : 0.4;
    p.cotes.tMax = sansPatte ? 0.5 : (pas >= 1.27 ? 1.27 : 0.75);
    p.cotes.wMin = pas >= 1.27 ? 0.31 : wMin;
    p.cotes.wMax = pas >= 1.27 ? 0.51 : wMax;
    p.cotes.corpsW = quatreCotes ? (sansPatte ? a : a - 2) : (sansPatte ? a : a - 2.1);
    p.cotes.corpsL = quatreCotes ? (sansPatte ? b : a - 2) : (n / 2) * pas + 0.4;
    p.cotes.hauteur = m[5].matched ? lire(m[5]) / 100 : b / 100 * 100;
    if (!m[5].matched) p.cotes.hauteur = b;  // SOIC127P600X175 : 1,75 mm
    if (sansPatte && quatreCotes) p.cotes.plotCentral = std::max(0.0, a - 1.3);
    return p;
  }
  static const std::regex ipcPuce(R"(^(RES|CAP|IND|DIO|LED)C(\d{2})(\d{2})X(\d+)[MNL]?$)");
  if (std::regex_match(nom, m, ipcPuce)) {
    p.cotes.gabarit = GabaritIPC::Puce;
    p.cotes.broches = 2;
    const double l = lire(m[2]) / 10, w = lire(m[3]) / 10, tol = l <= 1.0 ? 0.05 : 0.1;
    p.cotes.lMin = l - tol;
    p.cotes.lMax = l + tol;
    p.cotes.wMin = w - tol;
    p.cotes.wMax = w + tol;
    p.cotes.tMin = std::max(0.05, l * 0.15 - 0.1);
    p.cotes.tMax = l * 0.15 + 0.1;
    p.cotes.corpsL = l;
    p.cotes.corpsW = w;
    p.cotes.hauteur = lire(m[4]) / 100;
    return p;
  }
  return std::nullopt;
}

std::string nomGabarit(const ParamsGabarit& p, DensiteIPC d) {
  char t[128];
  auto c = [](double v) { return static_cast<int>(std::lround(v * 100)); };
  switch (p.genre) {
    case GenreGabarit::IPC: {
      if (p.variante.rfind("SOT-23", 0) == 0) return p.variante + "_" + lettre(d);
      if (p.cotes.gabarit == GabaritIPC::Puce) {
        // Noms IPC des puces : RESC, CAPC, INDC, LEDC, DIOC, FUSC ; longueur et largeur en dixièmes, hauteur en centièmes
        static const std::map<std::string, std::string> prefixes = {{"R", "RESC"}, {"C", "CAPC"}, {"L", "INDC"}, {"LED", "LEDC"}, {"D", "DIOC"}, {"Fuse", "FUSC"}};
        const auto it = prefixes.find(p.variante);
        std::snprintf(t, sizeof t, "%s%02d%02dX%d%c", it == prefixes.end() ? "CHIP" : it->second.c_str(), static_cast<int>(std::lround((p.cotes.lMin + p.cotes.lMax) * 5)),
                      static_cast<int>(std::lround((p.cotes.wMin + p.cotes.wMax) * 5)), c(p.cotes.hauteur), lettre(d));
        return t;
      }
      return nomIPC7351(p.cotes, d);
    }
    case GenreGabarit::SOT89: return std::string("SOT89-3") + lettre(d);
    case GenreGabarit::DPAK: return std::string("TO252-3") + lettre(d);
    case GenreGabarit::D2PAK: return std::string("TO263-3") + lettre(d);
    case GenreGabarit::BGA:
    case GenreGabarit::LGA:
    case GenreGabarit::WLCSP:
      std::snprintf(t, sizeof t, "%s%dC%dP%dX%d_%dX%dX%d%c", p.genre == GenreGabarit::BGA ? "BGA" : p.genre == GenreGabarit::LGA ? "LGA" : "WLCSP",
                    p.rangees * p.colonnes - static_cast<int>(p.sansBille.size()), c(p.pas), p.colonnes, p.rangees, c(p.corpsL), c(p.corpsW), c(p.hauteur), lettre(d));
      return t;
    case GenreGabarit::DFN:
    case GenreGabarit::SON:
      std::snprintf(t, sizeof t, "%s%dP%dX%dX%d-%d%c", p.genre == GenreGabarit::DFN ? "DFN" : "SON", c(p.pas), c(p.corpsW), c(p.corpsL), c(p.hauteur), p.broches + (p.plot > 0 ? 1 : 0), lettre(d));
      return t;
    case GenreGabarit::PLCC: std::snprintf(t, sizeof t, "PLCC127P%dX%d-%d%c", c(p.corpsL + 1.5), c(p.corpsW + 1.5), p.broches, lettre(d)); return t;
    case GenreGabarit::BordCarte: std::snprintf(t, sizeof t, "EDGE%dX%dP%d%c", p.rangees, p.broches, c(p.pas), lettre(d)); return t;
    case GenreGabarit::QuartzCMS: return "XTAL" + p.variante + "-4" + lettre(d);
    default: return p.variante.empty() ? std::string("GABARIT") + lettre(d) : p.variante + "_" + lettre(d);
  }
}

DefEmpreinte empreinteGabarit(const ParamsGabarit& p, DensiteIPC d, const ProfilIPC& profil) {
  DefEmpreinte e;
  const double kDens = d == DensiteIPC::Plus ? 1.1 : d == DensiteIPC::Nominale ? 1.0 : 0.9;
  switch (p.genre) {
    case GenreGabarit::IPC: {
      e = empreinteIPC7351(p.cotes, d, profil);
      if (p.cotes.gabarit == GabaritIPC::AileDeMouette) rognerRangees(e, p.cotes.pas, 0.15);  // pattes rognées
      if (p.cotes.gabarit == GabaritIPC::SansPatte && p.cotes.plotCentral > 0) {
        const std::string plot = std::to_string(p.cotes.broches + 1);
        ecarterDuPlot(e, plot, 0.2);
        motifPlot(e, plot);
      }
      if (p.variante == "SOT-23" || p.variante == "SOT-23-5") {
        // Six pattes de base (1 à 3 à gauche, 4 à 6 à droite) : SOT-23 garde 1, 3 et la 5 renumérotées 1, 2, 3 ;
        // SOT-23-5 retire la 5 (milieu de droite) et renumérote 6 → 5
        std::vector<Pastille> garde;
        for (auto q : e.pastilles) {
          if (p.variante == "SOT-23") {
            if (q.numero == "1") garde.push_back(q);
            else if (q.numero == "3") q.numero = "2", garde.push_back(q);
            else if (q.numero == "5") q.numero = "3", garde.push_back(q);
          } else {
            if (q.numero == "5") continue;
            if (q.numero == "6") q.numero = "5";
            garde.push_back(q);
          }
        }
        e.pastilles = garde;
      }
      break;
    }
    case GenreGabarit::BGA:
    case GenreGabarit::LGA:
    case GenreGabarit::WLCSP: {
      const double bille = p.bille > 0 ? p.bille : p.pas * 0.5;
      // Pastille non définie par le vernis (NSMD) : 80 % de la bille à la densité nominale (IPC-7351B, tableau 14-3)
      const double dp = p.pastille > 0 && d == DensiteIPC::Nominale ? p.pastille : (p.genre == GenreGabarit::LGA ? p.pas * 0.6 : bille * 0.8) * (d == DensiteIPC::Plus ? 1.0625 : d == DensiteIPC::Moins ? 0.9375 : 1.0);
      const std::set<std::string> absentes(p.sansBille.begin(), p.sansBille.end());
      for (int r = 0; r < p.rangees; ++r)
        for (int col = 0; col < p.colonnes; ++col) {
          const std::string n = lettreRangee(r) + std::to_string(col + 1);
          if (absentes.count(n)) continue;
          const double x = (col - (p.colonnes - 1) / 2.0) * p.pas, y = (r - (p.rangees - 1) / 2.0) * p.pas;
          e.pastilles.push_back(pastille(n, arrondi(x, 1e-6), arrondi(y, 1e-6), dp, dp, p.genre == GenreGabarit::LGA ? FormePastille::Rect : FormePastille::Rond));
        }
      encombrer(e, p.corpsL, p.corpsW, d, 1.0);
      serigraphieCorps(e, p.corpsL / 2 + 0.1, p.corpsW / 2 + 0.1);
      corps(e, p.corpsL, p.corpsW, p.hauteur, "#2A2D34");
      e.famille = "Circuits intégrés";
      break;
    }
    case GenreGabarit::DFN:
    case GenreGabarit::SON: {
      CotesBoitier k;
      k.gabarit = GabaritIPC::SansPatte;
      k.quatreCotes = false;
      k.broches = p.broches;
      k.pas = p.pas;
      k.lMin = p.corpsW - 0.05;
      k.lMax = p.corpsW + 0.05;
      k.tMin = 0.3;
      k.tMax = 0.5;
      const auto [wMin, wMax] = largeurPatte(p.pas, true);
      k.wMin = wMin;
      k.wMax = wMax;
      k.corpsW = p.corpsW;
      k.corpsL = p.corpsL;
      k.hauteur = p.hauteur;
      e = empreinteIPC7351(k, d, profil);
      if (p.plot > 0) {
        const std::string n = std::to_string(p.broches + 1);
        e.pastilles.push_back(pastille(n, 0, 0, p.plot, p.plotL));
        ecarterDuPlot(e, n, 0.2);
        motifPlot(e, n);
      }
      encombrer(e, p.corpsW, p.corpsL, d);
      break;
    }
    case GenreGabarit::SOD:
    case GenreGabarit::MELF:
    case GenreGabarit::SMx: {
      // Deux pastilles, cathode (1) à gauche ; prolongement de pointe et de talon selon la densité
      const double lt = p.plot, wt = p.plotL;
      const double longueur = (lt + (p.genre == GenreGabarit::MELF ? 0.6 : 0.8)) * kDens, largeur = (wt + 0.4) * kDens;
      const double centre = p.corpsL / 2 - lt / 2 + 0.15 * kDens;
      e.pastilles = {pastille("1", -centre, 0, longueur, largeur), pastille("2", centre, 0, longueur, largeur)};
      encombrer(e, p.corpsL, p.corpsW, d);
      serigraphieCorps(e, p.corpsL / 2 - lt, p.corpsW / 2 + 0.1, false);
      trait(e, -centre - longueur / 2 - 0.3, -largeur / 2, -centre - longueur / 2 - 0.3, largeur / 2);  // repère de cathode
      corps(e, p.corpsL, p.corpsW, p.hauteur, "#1F2126");
      e.famille = "Diodes";
      break;
    }
    case GenreGabarit::SOT89: {
      // Trois pattes au pas de 1,5 en bas, languette (broche 2) qui les prolonge vers le haut
      const double y = 2.0, l = 1.0 * kDens, w = 0.7 * kDens;
      e.pastilles = {pastille("1", -p.pas, y, w, l), pastille("2", 0, y, w, l), pastille("3", p.pas, y, w, l)};
      Pastille tab = pastille("2", 0, -0.4, 1.9 * kDens, 3.0 * kDens);
      e.pastilles.push_back(tab);
      encombrer(e, p.corpsL, p.corpsW + 1.5, d);
      corps(e, p.corpsL, p.corpsW, p.hauteur);
      e.famille = "Transistors";
      break;
    }
    case GenreGabarit::DPAK:
    case GenreGabarit::D2PAK: {
      const bool grand = p.genre == GenreGabarit::D2PAK;
      const double ty = grand ? -2.6 : -1.9;  // languette (broche 2, drain)
      const double py = grand ? 5.6 : 4.5;    // pattes
      const double pl = (grand ? 3.2 : 2.6) * kDens, pw = (grand ? 1.1 : 1.0) * kDens;
      e.pastilles = {pastille("1", -p.pas, py, pw, pl), pastille("3", p.pas, py, pw, pl), pastille("2", 0, ty, (grand ? 10.8 : 6.4) * kDens, (grand ? 9.4 : 5.8) * kDens)};
      encombrer(e, p.corpsL, p.corpsW + (grand ? 6 : 4.5), d);
      corps(e, p.corpsL, p.corpsW, p.hauteur);
      e.famille = "Transistors";
      break;
    }
    case GenreGabarit::QuartzCMS: {
      // Quatre plots aux coins : 1 en bas à gauche, puis sens inverse des aiguilles d'une montre
      const double px = p.corpsL / 2 - 0.35, py = p.corpsW / 2 - 0.3;
      const double l = (p.corpsL >= 5 ? 1.4 : p.corpsL >= 3.2 ? 1.0 : 0.8) * kDens, w = (p.corpsW >= 3.2 ? 1.2 : p.corpsW >= 2.5 ? 1.0 : 0.8) * kDens;
      e.pastilles = {pastille("1", -px, py, l, w), pastille("2", px, py, l, w), pastille("3", px, -py, l, w), pastille("4", -px, -py, l, w)};
      encombrer(e, p.corpsL, p.corpsW, d);
      corps(e, p.corpsL, p.corpsW, p.hauteur, "#B0B4BA");
      e.famille = "Quartz";
      break;
    }
    case GenreGabarit::PLCC: {
      // Pattes en J sur quatre côtés ; la broche 1 au milieu du côté du haut, sens inverse des aiguilles d'une montre
      const int parCote = p.broches / 4;
      const double a0 = -(parCote - 1) * p.pas / 2, centre = p.corpsL / 2 - 0.2, l = 2.0 * kDens, w = 0.6 * kDens;
      // Tour dans le sens inverse des aiguilles d'une montre vu de dessus : le haut de droite à gauche, la gauche de
      // haut en bas, le bas de gauche à droite, la droite de bas en haut
      std::vector<Pastille> tour;
      for (int i = 0; i < parCote; ++i) tour.push_back(pastille("", -a0 - i * p.pas, -centre, w, l));
      for (int i = 0; i < parCote; ++i) tour.push_back(pastille("", -centre, a0 + i * p.pas, l, w));
      for (int i = 0; i < parCote; ++i) tour.push_back(pastille("", a0 + i * p.pas, centre, w, l));
      for (int i = 0; i < parCote; ++i) tour.push_back(pastille("", centre, -a0 - i * p.pas, l, w));
      // Broche 1 au milieu du haut : la numérotation part de la pastille du milieu de la première rangée
      const int depart = parCote / 2;
      for (int i = 0; i < p.broches; ++i) {
        Pastille q = tour[static_cast<std::size_t>((depart + i) % p.broches)];
        q.numero = std::to_string(i + 1);
        e.pastilles.push_back(q);
      }
      encombrer(e, p.corpsL, p.corpsW, d);
      serigraphieCorps(e, p.corpsL / 2 - 1.2, p.corpsW / 2 - 1.2);
      corps(e, p.corpsL, p.corpsW, p.hauteur);
      e.famille = "Circuits intégrés";
      break;
    }
    case GenreGabarit::BordCarte: {
      // Doigts de connecteur de tranche (4.24) : pas de pâte ni de sérigraphie, bout sur le bord (y = 0), les doigts
      // de la face du dessous numérotés à la suite
      const double l = 4.0, w = p.pas * 0.6;
      for (int face = 0; face < p.rangees; ++face)
        for (int i = 0; i < p.broches; ++i) {
          Pastille q = pastille(std::to_string(face * p.broches + i + 1), (i - (p.broches - 1) / 2.0) * p.pas, -l / 2, w, l, FormePastille::Rect);
          q.fabrication = "connecteurBord";
          q.couches = face == 0 ? bit(Couche::CuivreDessus) | bit(Couche::VernisDessus) : bit(Couche::CuivreDessous) | bit(Couche::VernisDessous);
          e.pastilles.push_back(q);
        }
      const double ex = (p.broches - 1) / 2.0 * p.pas + w / 2 + 0.5;
      e.encombrement = Boite{nm(-ex), nm(-l - 0.5), nm(ex), 0};
      e.famille = "Connecteurs";
      break;
    }
  }
  e.id = nomGabarit(p, d);
  if (e.description.empty() || p.genre != GenreGabarit::IPC) e.description = "Généré selon l'IPC-7351B (" + std::string(1, lettre(d)) + ")";
  return e;
}

std::optional<DefEmpreinte> empreinteParNom(const std::string& nom, DensiteIPC d) {
  const auto p = gabaritDepuisNom(nom);
  if (!p) return std::nullopt;
  return empreinteGabarit(*p, d);
}

std::vector<std::string> controlerEmpreinte(const DefEmpreinte& d) {
  std::vector<std::string> r;
  if (d.pastilles.empty()) r.push_back("CE-01 : aucune pastille");
  std::map<std::string, int> numeros;
  for (const auto& p : d.pastilles)
    if (!p.numero.empty()) ++numeros[p.numero];
  // Une même broche sur plusieurs pastilles est permise (languette, plot) ; un doublon de forme et de place ne l'est pas
  for (std::size_t i = 0; i < d.pastilles.size(); ++i)
    for (std::size_t j = i + 1; j < d.pastilles.size(); ++j) {
      const Pastille& a = d.pastilles[i];
      const Pastille& b = d.pastilles[j];
      if (a.numero == b.numero) continue;
      // Pastilles de faces différentes (doigts d'un connecteur de bord des deux côtés) : pas de voisinage
      auto faces = [](const Pastille& q) -> Couches {
        if (q.couches) return q.couches & (bit(Couche::CuivreDessus) | bit(Couche::CuivreDessous));
        return q.type == TypePastille::CMS ? bit(Couche::CuivreDessus) : bit(Couche::CuivreDessus) | bit(Couche::CuivreDessous);
      };
      if (!(faces(a) & faces(b))) continue;
      const Polygone fa = formePastille(a), fb = formePastille(b);  // dans le repère de l'empreinte
      const double ecart = distancePolygones(fa, fb);
      if (ecart <= 0) r.push_back("CE-02 : pastilles " + a.numero + " et " + b.numero + " qui se recouvrent");
      else if (ecart < 100'000 && a.couches == b.couches) r.push_back("CE-07 : pastilles " + a.numero + " et " + b.numero + " à moins de 0,1 mm");
    }
  if (!d.encombrement.vide())
    for (const auto& p : d.pastilles) {
      if (!d.encombrement.gonflee(1'000).contient(boite(formePastille(p)))) r.push_back("CE-03 : pastille " + p.numero + " hors de l'encombrement");
    }
  for (const auto& p : d.pastilles) {
    if (p.percage > 0) {
      const Nm cote = p.forme == FormePastille::Rond ? p.largeur : std::min(p.largeur, p.hauteur);
      if (p.type != TypePastille::NonMetallisee && p.percage >= cote) r.push_back("CE-05 : trou de la pastille " + p.numero + " aussi large qu'elle");
    }
    if (p.margePate && *p.margePate > 0) r.push_back("CE-04 : pâte de la pastille " + p.numero + " plus grande que son cuivre");
  }
  if (d.corps.largeur <= 0 && d.corps.hauteur <= 0 && d.volumes.empty() && d.famille != "Connecteurs" && d.famille != "Mécanique") r.push_back("CE-06 : corps absent (hauteur inconnue)");
  return r;
}

std::vector<DefEmpreinte> serieEmpreintes(const std::vector<std::string>& noms, const std::vector<DensiteIPC>& densites, std::vector<std::string>* refus) {
  std::vector<DefEmpreinte> r;
  std::set<std::string> ids;
  for (const auto& n : noms) {
    const auto p = gabaritDepuisNom(n);
    if (!p) {
      if (refus) refus->push_back(n + " : nom non compris par le générateur");
      continue;
    }
    for (const DensiteIPC d : densites) {
      DefEmpreinte e = empreinteGabarit(*p, d);
      if (!ids.insert(e.id).second) {
        if (refus) refus->push_back(n + " : identifiant « " + e.id + " » déjà produit");
        continue;
      }
      const auto defauts = controlerEmpreinte(e);
      if (!defauts.empty() && refus) refus->push_back(e.id + " : " + defauts.front());
      r.push_back(std::move(e));
    }
  }
  return r;
}

std::string varianteEmpreinte(const std::string& id, char densite, const std::string& procede) {
  // L'identifiant de la carte peut être un nom de gabarit (« SOIC-8 ») ou un nom IPC suffixé (« SOIC127P600X175-8N »)
  std::string base = id;
  const auto p = gabaritDepuisNom(base);
  if (!p) return {};
  char dens = densite;
  if (procede == "vague") dens = 'M';  // brasage à la vague : pastilles plus longues, densité M
  const DensiteIPC d = dens == 'M' ? DensiteIPC::Plus : dens == 'L' ? DensiteIPC::Moins : DensiteIPC::Nominale;
  return nomGabarit(*p, d);
}

int appliquerVariantes(ConceptionElec& c) {
  exigerModifiable(c);
  int n = 0;
  const char dens = c.circuit.fabrication.densite;
  const std::string procede = c.circuit.fabrication.procede;
  std::vector<std::pair<std::string, std::string>> echanges;  // composant, variante
  for (const auto& e : c.circuit.empreintes) {
    // Le gabarit de départ, gardé dans le champ « gabarit » du composant à la première variante : l'identifiant de
    // la variante ne se relit pas toujours en gabarit (BGA, DFN)
    const ComposantElec* x = c.composant(e.composant);
    if (!x) continue;
    const auto g = x->champs.find("gabarit");
    const std::string source = g != x->champs.end() && !g->second.empty() ? g->second : !x->empreinte.empty() ? x->empreinte : e.empreinte;
    const std::string v = varianteEmpreinte(source, dens, procede);
    if (v.empty() || v == e.empreinte) continue;
    if (!c.empreintes.count(v)) {
      const auto p = gabaritDepuisNom(source);
      if (!p) continue;
      const DensiteIPC d = v.back() == 'M' ? DensiteIPC::Plus : v.back() == 'L' ? DensiteIPC::Moins : DensiteIPC::Nominale;
      c.empreintes[v] = empreinteGabarit(*p, d);
    }
    echanges.emplace_back(e.composant, v);
    if (g == x->champs.end()) c.composant(e.composant)->champs["gabarit"] = source;
  }
  // Remplacement en place (3.7) : même point, même angle, même face, pistes raccrochées aux nouvelles pastilles
  for (const auto& [composant, v] : echanges) {
    ComposantElec* x = c.composant(composant);
    if (!remplacerEmpreinteEnPlace(c, composant, v)) continue;
    x->empreinte = v;
    for (auto& e : c.circuit.empreintes)
      if (e.composant == composant) e.copieLocale.clear();
    ++n;
  }
  return n;
}

}  // namespace prom::ecad

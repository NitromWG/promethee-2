// Prométhée : circuit imprimé — couches, règles, profils de fabricant, contour, géométrie des empreintes placées.
// Licence GPL-3.0-only.
#include "promethee/circuit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <tuple>

namespace prom::ecad {

namespace {
const char* const NOMS[NB_COUCHES] = {"Cuivre dessus", "Cuivre intérieur 1", "Cuivre intérieur 2", "Cuivre dessous", "Vernis dessus", "Vernis dessous", "Pâte dessus", "Pâte dessous",
                                      "Sérigraphie dessus", "Sérigraphie dessous", "Contour de carte", "Encombrement dessus", "Encombrement dessous", "Fabrication dessus", "Fabrication dessous",
                                      "Cuivre intérieur 3", "Cuivre intérieur 4", "Cuivre intérieur 5", "Cuivre intérieur 6", "Cuivre intérieur 7", "Cuivre intérieur 8",
                                      "Cuivre intérieur 9", "Cuivre intérieur 10", "Cuivre intérieur 11", "Cuivre intérieur 12", "Cuivre intérieur 13", "Cuivre intérieur 14",
                                      "Cuivre intérieur 15", "Cuivre intérieur 16", "Cuivre intérieur 17", "Cuivre intérieur 18", "Cuivre intérieur 19", "Cuivre intérieur 20",
                                      "Cuivre intérieur 21", "Cuivre intérieur 22", "Cuivre intérieur 23", "Cuivre intérieur 24", "Cuivre intérieur 25", "Cuivre intérieur 26",
                                      "Cuivre intérieur 27", "Cuivre intérieur 28", "Cuivre intérieur 29", "Cuivre intérieur 30",
                                      "Colle dessus", "Colle dessous", "Vernis de protection dessus", "Vernis de protection dessous", "Dessins", "Commentaires",
                                      "Couche personnelle 1", "Couche personnelle 2", "Couche personnelle 3", "Couche personnelle 4", "Couche personnelle 5",
                                      "Couche personnelle 6", "Couche personnelle 7", "Couche personnelle 8", "Couche personnelle 9"};
const char* const CODES[NB_COUCHES] = {"F.Cu", "In1.Cu", "In2.Cu", "B.Cu", "F.Mask", "B.Mask", "F.Paste", "B.Paste", "F.SilkS", "B.SilkS", "Edge.Cuts", "F.CrtYd", "B.CrtYd", "F.Fab", "B.Fab",
                                       "In3.Cu", "In4.Cu", "In5.Cu", "In6.Cu", "In7.Cu", "In8.Cu", "In9.Cu", "In10.Cu", "In11.Cu", "In12.Cu", "In13.Cu", "In14.Cu", "In15.Cu",
                                       "In16.Cu", "In17.Cu", "In18.Cu", "In19.Cu", "In20.Cu", "In21.Cu", "In22.Cu", "In23.Cu", "In24.Cu", "In25.Cu", "In26.Cu", "In27.Cu",
                                       "In28.Cu", "In29.Cu", "In30.Cu", "F.Adhes", "B.Adhes", "F.Coating", "B.Coating", "Dwgs.User", "Cmts.User", "User.1", "User.2",
                                       "User.3", "User.4", "User.5", "User.6", "User.7", "User.8", "User.9"};
}  // namespace

const char* nomCouche(Couche c) { return static_cast<int>(c) < NB_COUCHES ? NOMS[static_cast<int>(c)] : "?"; }
const char* codeCouche(Couche c) { return static_cast<int>(c) < NB_COUCHES ? CODES[static_cast<int>(c)] : "?"; }
std::optional<Couche> coucheDepuisCode(const std::string& code) {
  for (int i = 0; i < NB_COUCHES; ++i)
    if (code == CODES[i]) return static_cast<Couche>(i);
  // Noms longs de KiCad 5 et d'autres outils
  if (code == "F.Silkscreen") return Couche::SerigraphieDessus;
  if (code == "B.Silkscreen") return Couche::SerigraphieDessous;
  if (code == "F.Courtyard") return Couche::EncombrementDessus;
  if (code == "B.Courtyard") return Couche::EncombrementDessous;
  if (code == "F.Adhesive") return Couche::ColleDessus;
  if (code == "B.Adhesive") return Couche::ColleDessous;
  if (code == "User.Drawings") return Couche::Dessins;
  if (code == "User.Comments") return Couche::Commentaires;
  return std::nullopt;
}
Couche cuivreInterne(int k) {
  if (k <= 1) return Couche::CuivreInt1;
  if (k == 2) return Couche::CuivreInt2;
  return static_cast<Couche>(static_cast<int>(Couche::CuivreInt3) + std::min(k, 30) - 3);
}
bool estCuivreInterne(Couche c) {
  return c == Couche::CuivreInt1 || c == Couche::CuivreInt2 || (static_cast<int>(c) >= static_cast<int>(Couche::CuivreInt3) && static_cast<int>(c) <= static_cast<int>(Couche::CuivreInt30));
}
bool estCuivre(Couche c) { return c == Couche::CuivreDessus || c == Couche::CuivreDessous || estCuivreInterne(c); }
bool estCoucheDessin(Couche c) {
  return !estCuivre(c) && c != Couche::VernisDessus && c != Couche::VernisDessous && c != Couche::PateDessus && c != Couche::PateDessous;
}
namespace {
int numeroInterne(Couche c) {
  if (c == Couche::CuivreInt1) return 1;
  if (c == Couche::CuivreInt2) return 2;
  return static_cast<int>(c) - static_cast<int>(Couche::CuivreInt3) + 3;
}
}  // namespace
int rangCuivre(Couche c, int nombre) {
  if (nombre <= 1) return c == Couche::CuivreDessous ? 0 : -1;
  if (c == Couche::CuivreDessus) return 0;
  if (c == Couche::CuivreDessous) return nombre - 1;
  if (!estCuivreInterne(c)) return -1;
  const int k = numeroInterne(c);
  return k <= nombre - 2 ? k : -1;
}
Couche opposee(Couche c) {
  switch (c) {
    case Couche::CuivreDessus: return Couche::CuivreDessous;
    case Couche::CuivreDessous: return Couche::CuivreDessus;
    case Couche::VernisDessus: return Couche::VernisDessous;
    case Couche::VernisDessous: return Couche::VernisDessus;
    case Couche::PateDessus: return Couche::PateDessous;
    case Couche::PateDessous: return Couche::PateDessus;
    case Couche::SerigraphieDessus: return Couche::SerigraphieDessous;
    case Couche::SerigraphieDessous: return Couche::SerigraphieDessus;
    case Couche::EncombrementDessus: return Couche::EncombrementDessous;
    case Couche::EncombrementDessous: return Couche::EncombrementDessus;
    case Couche::FabricationDessus: return Couche::FabricationDessous;
    case Couche::FabricationDessous: return Couche::FabricationDessus;
    case Couche::ColleDessus: return Couche::ColleDessous;
    case Couche::ColleDessous: return Couche::ColleDessus;
    case Couche::ProtectionDessus: return Couche::ProtectionDessous;
    case Couche::ProtectionDessous: return Couche::ProtectionDessus;
    default: return c;
  }
}
std::vector<Couche> couchesCuivre(int nombre) {
  // Simple face : le cuivre sous la carte, les composants traversants posés dessus (gravure maison)
  if (nombre <= 1) return {Couche::CuivreDessous};
  std::vector<Couche> r{Couche::CuivreDessus};
  for (int k = 1; k <= std::min(nombre, MAX_CUIVRES) - 2; ++k) r.push_back(cuivreInterne(k));
  r.push_back(Couche::CuivreDessous);
  return r;
}
Couches tousLesCuivres(int nombre) {
  Couches r = 0;
  for (Couche c : couchesCuivre(nombre)) r |= bit(c);
  return r;
}

bool motifReseau(const std::string& motif) { return motif.find_first_of("*?") != std::string::npos; }

// Motif à la façon de KiCad : « * » pour toute suite de caractères, « ? » pour un caractère
bool correspond(const std::string& motif, const std::string& nom) {
  std::size_t m = 0, n = 0, etoile = std::string::npos, reprise = 0;
  while (n < nom.size()) {
    if (m < motif.size() && (motif[m] == '?' || motif[m] == nom[n])) { ++m; ++n; }
    else if (m < motif.size() && motif[m] == '*') { etoile = m++; reprise = n; }
    else if (etoile != std::string::npos) { m = etoile + 1; n = ++reprise; }
    else return false;
  }
  while (m < motif.size() && motif[m] == '*') ++m;
  return m == motif.size();
}

AffectationsClasses::AffectationsClasses(std::initializer_list<Paire> l) {
  for (const auto& [nom, classe] : l) (*this)[nom] = classe;
}
AffectationsClasses::iterator AffectationsClasses::find(const std::string& nom) {
  return std::find_if(m_liste.begin(), m_liste.end(), [&](const Paire& p) { return p.first == nom; });
}
AffectationsClasses::const_iterator AffectationsClasses::find(const std::string& nom) const {
  return std::find_if(m_liste.begin(), m_liste.end(), [&](const Paire& p) { return p.first == nom; });
}
std::string& AffectationsClasses::operator[](const std::string& nom) {
  const auto it = find(nom);
  if (it != m_liste.end()) return it->second;
  m_liste.emplace_back(nom, std::string());
  return m_liste.back().second;
}
std::size_t AffectationsClasses::erase(const std::string& nom) {
  const auto it = find(nom);
  if (it == m_liste.end()) return 0;
  m_liste.erase(it);
  return 1;
}
void AffectationsClasses::deplacer(const std::string& nom, std::size_t rang) {
  const auto it = find(nom);
  if (it == m_liste.end()) return;
  Paire p = std::move(*it);
  m_liste.erase(it);
  m_liste.insert(m_liste.begin() + static_cast<std::ptrdiff_t>(std::min(rang, m_liste.size())), std::move(p));
}

const ClasseReseau* classeDe(const Regles& r, const std::string& reseau) {
  auto classe = [&r](const std::string& nom) -> const ClasseReseau* {
    for (const auto& c : r.classes)
      if (c.nom == nom) return &c;
    return nullptr;
  };
  // Le nom exact d'abord, puis le premier motif qui correspond, dans l'ordre saisi (EL-39, R5). Une affectation
  // exacte vers une classe supprimée ne rend pas « aucune classe » : les motifs s'essaient (R10).
  if (const auto it = r.classeReseau.find(reseau); it != r.classeReseau.end())
    if (const ClasseReseau* c = classe(it->second)) return c;
  if (reseau.empty()) return nullptr;
  for (const auto& [motif, nom] : r.classeReseau)
    if (motifReseau(motif) && correspond(motif, reseau))
      if (const ClasseReseau* c = classe(nom)) return c;
  return nullptr;
}

const ClasseReseau* plusStricte(const ClasseReseau* a, const ClasseReseau* b) {
  if (!a) return b;
  if (!b) return a;
  return std::tie(a->isolement, a->largeur, a->viaDiametre, a->viaPercage) >= std::tie(b->isolement, b->largeur, b->viaDiametre, b->viaPercage) ? a : b;
}
Nm isolementEntre(const Regles& r, const std::string& a, const std::string& b) {
  Nm v = r.isolement;
  if (const ClasseReseau* c = classeDe(r, a)) v = std::max(v, c->isolement);
  if (const ClasseReseau* c = classeDe(r, b)) v = std::max(v, c->isolement);
  return v;
}
Nm largeurPiste(const Regles& r, const std::string& reseau) {
  const ClasseReseau* c = classeDe(r, reseau);
  return c ? c->largeur : r.largeur;
}
Nm viaDiametre(const Regles& r, const std::string& reseau) {
  const ClasseReseau* c = classeDe(r, reseau);
  return c ? c->viaDiametre : r.viaDiametre;
}
Nm viaPercage(const Regles& r, const std::string& reseau) {
  const ClasseReseau* c = classeDe(r, reseau);
  return c ? c->viaPercage : r.viaPercage;
}

Nm largeurIPC2221(double courant, double echauffement, double cuivreMm, bool coucheExterne) {
  if (courant <= 0 || echauffement <= 0 || cuivreMm <= 0) return 0;
  const double k = coucheExterne ? 0.048 : 0.024;
  const double sectionMil2 = std::pow(courant / (k * std::pow(echauffement, 0.44)), 1.0 / 0.725);
  const double epaisseurMil = cuivreMm / 0.0254;
  const double largeurMm = sectionMil2 / epaisseurMil * 0.0254;
  return static_cast<Nm>(std::ceil(largeurMm * 1e6));
}

std::string hypothesesIPC2221(double courant, double echauffement, double cuivreMm, bool coucheExterne) {
  char t[200];
  std::snprintf(t, sizeof t, "IPC-2221 : %.3g A, échauffement %.3g °C, cuivre %.0f µm, couche %s", courant, echauffement, cuivreMm * 1000,
                coucheExterne ? "externe" : "interne");
  return t;
}

const std::vector<ProfilFabricant>& profilsFabricants() {
  static const std::vector<ProfilFabricant> p = [] {
    std::vector<ProfilFabricant> v;
    Regles s;  // valeurs par défaut : sûres chez la plupart des fabricants en 2 couches
    v.push_back({"Standard", "Pistes et isolements de 0,2 mm, vias 0,8/0,4 mm : accepté partout, prix plancher.", s});
    Regles f = s;
    f.profil = "Fin";
    f.isolement = 127'000;
    f.largeur = 150'000;
    f.largeurMin = 127'000;
    f.viaDiametre = 600'000;
    f.viaPercage = 300'000;
    f.anneauMin = 100'000;
    f.percageMin = 200'000;
    f.bord = 250'000;
    f.ecartTrous = 200'000;
    f.cuivreTrou = 200'000;
    v.push_back({"Fin", "Classe 6 : pistes et isolements de 0,127 mm, vias 0,6/0,3 mm (circuits denses, QFN).", f});
    Regles m = s;
    m.profil = "Gravure maison";
    m.isolement = 400'000;
    m.largeur = 500'000;
    m.largeurMin = 400'000;
    m.viaDiametre = 1'600'000;
    m.viaPercage = 800'000;
    m.anneauMin = 400'000;
    m.percageMin = 800'000;
    m.bord = 1'000'000;
    m.ecartTrous = 500'000;
    m.cuivreTrou = 500'000;
    v.push_back({"Gravure maison", "Insoleuse et perchlorure : pistes de 0,5 mm, isolements de 0,4 mm, trous de 0,8 mm.", m});
    return v;
  }();
  return p;
}

std::optional<ProfilFabricant> profilDeReference(const Regles& r) {
  for (const auto& p : profilsFabricants())
    if (p.nom == r.profil) return p;
  // Profil personnalisé : seulement complet, sinon une règle sous la capacité réelle passerait sans contrôle (R3)
  if (!r.capacitesPerso || !r.capacitesPerso->complet() || r.profil.empty()) return std::nullopt;
  ProfilFabricant p;
  p.nom = r.profil;
  p.description = "Profil personnalisé : capacités écrites dans le projet.";
  p.regles.profil = r.profil;
  p.regles.isolement = r.capacitesPerso->isolement;
  p.regles.largeurMin = r.capacitesPerso->largeurMin;
  p.regles.percageMin = r.capacitesPerso->percageMin;
  p.regles.anneauMin = r.capacitesPerso->anneauMin;
  p.regles.bord = r.capacitesPerso->bord;
  return p;
}

Polygone ContourCarte::polygone() const {
  if (libre.size() >= 3) return orienter(libre);
  return rectangleArrondi({origine.x + largeur / 2, origine.y + hauteur / 2}, largeur, hauteur, rayon, 0, Approche::Interieure);
}

std::vector<std::pair<Pt, Pt>> ContourCarte::aretes() const {
  std::vector<std::pair<Pt, Pt>> r;
  auto boucle = [&r](const Polygone& p) {
    if (p.size() < 2) return;
    for (std::size_t i = 0; i < p.size(); ++i) r.push_back({p[i], p[(i + 1) % p.size()]});
  };
  boucle(polygone());
  for (const auto& d : decoupes)
    if (d.size() >= 3) boucle(d);
  return r;
}

bool ContourCarte::contient(Pt p) const {
  if (!dedans(polygone(), p)) return false;
  for (const auto& d : decoupes)
    if (d.size() >= 3 && dedans(d, p)) {
      // Sur le bord de la découpe : encore dans la matière
      bool surBord = false;
      for (std::size_t i = 0; i < d.size() && !surBord; ++i) surBord = distancePointSegment(p, d[i], d[(i + 1) % d.size()]) < 1.0;
      if (!surBord) return false;
    }
  return true;
}

Pt versCarte(const EmpreintePlacee& e, Pt local) {
  if (e.dessous) local.x = -local.x;
  return tournerDegres(local, e.rotation) + e.position;
}
Pt versEmpreinte(const EmpreintePlacee& e, Pt carte) {
  Pt local = tournerDegres(carte - e.position, -e.rotation);
  if (e.dessous) local.x = -local.x;
  return local;
}
Couche coucheSurCarte(const EmpreintePlacee& e, Couche c) { return e.dessous ? opposee(c) : c; }

Polygone formePastille(const Pastille& p) {
  switch (p.forme) {
    case FormePastille::Rond: return cercle(p.position, p.largeur / 2);
    case FormePastille::Rect: return rectangle(p.position, p.largeur, p.hauteur, p.rotation);
    case FormePastille::Ovale: {
      if (p.largeur == p.hauteur) return cercle(p.position, p.largeur / 2);
      const bool long_x = p.largeur > p.hauteur;
      const Nm demi = (long_x ? p.largeur - p.hauteur : p.hauteur - p.largeur) / 2;
      const Pt d = tournerDegres(long_x ? Pt{demi, 0} : Pt{0, demi}, p.rotation);
      return oblong(p.position - d, p.position + d, long_x ? p.hauteur : p.largeur);
    }
    case FormePastille::RectArrondi:
      return rectangleArrondi(p.position, p.largeur, p.hauteur, static_cast<Nm>(p.arrondi * static_cast<double>(std::min(p.largeur, p.hauteur))), p.rotation);
    case FormePastille::Chanfreine: {
      // Rectangle aux coins chanfreinés choisis (bits 1 haut gauche, 2 haut droit, 4 bas droit, 8 bas gauche)
      const Nm c = static_cast<Nm>(std::clamp(p.chanfrein, 0.0, 0.5) * static_cast<double>(std::min(p.largeur, p.hauteur)));
      const Nm l = p.largeur / 2, h = p.hauteur / 2;
      Polygone q;
      auto coin = [&](int b, Pt sommet, Pt avant, Pt apres) {
        if (c > 0 && (p.coinsChanfrein & b)) {
          q.push_back(avant);
          q.push_back(apres);
        } else q.push_back(sommet);
      };
      coin(1, {-l, -h}, {-l, -h + c}, {-l + c, -h});
      coin(2, {l, -h}, {l - c, -h}, {l, -h + c});
      coin(4, {l, h}, {l, h - c}, {l - c, h});
      coin(8, {-l, h}, {-l + c, h}, {-l, h - c});
      for (Pt& s : q) s = tournerDegres(s, p.rotation) + p.position;
      return orienter(q);
    }
    case FormePastille::Trapeze: {
      // Trapèze de KiCad : le côté du haut plus court de « trapeze » que celui du bas (négatif : l'inverse)
      const Nm l = p.largeur / 2, h = p.hauteur / 2, d = p.trapeze / 2;
      Polygone q{{-l + d, -h}, {l - d, -h}, {l + d, h}, {-l - d, h}};
      for (Pt& s : q) s = tournerDegres(s, p.rotation) + p.position;
      return orienter(q);
    }
    case FormePastille::Libre: {
      if (p.contourLibre.size() < 3) return rectangle(p.position, p.largeur, p.hauteur, p.rotation);
      Polygone q = p.contourLibre;
      for (Pt& s : q) s = tournerDegres(s, p.rotation) + p.position;
      return orienter(q);
    }
  }
  return {};
}

std::string nomIPCGabarit(const GabaritPastille& g) {
  // IPC-7351B, convention des noms de piles de pastilles : RX<l>Y<h>D<d>T (rectangle), RRX… (arrondi), OBX… (oblong),
  // C<diamètre> (rond), puis H<perçage> pour un trou ; centièmes de millimètre
  auto cent = [](Nm v) { return std::to_string(static_cast<long long>(std::llround(static_cast<double>(v) / 10'000.0))); };
  std::string n;
  switch (g.forme) {
    case FormePastille::Rond: n = "C" + cent(g.largeur); break;
    case FormePastille::Rect: n = "RX" + cent(g.largeur) + "Y" + cent(g.hauteur); break;
    case FormePastille::Ovale: n = "OBX" + cent(g.largeur) + "Y" + cent(g.hauteur); break;
    case FormePastille::RectArrondi: n = "RRX" + cent(g.largeur) + "Y" + cent(g.hauteur) + "R" + std::to_string(static_cast<int>(std::lround(g.arrondi * 100))); break;
    case FormePastille::Chanfreine: n = "CRX" + cent(g.largeur) + "Y" + cent(g.hauteur); break;
    case FormePastille::Trapeze: n = "TRX" + cent(g.largeur) + "Y" + cent(g.hauteur); break;
    case FormePastille::Libre: n = "CUSTOM" + cent(g.largeur) + "Y" + cent(g.hauteur); break;
  }
  if (g.percage > 0) n += "H" + cent(g.percage) + (g.longueurPercage > g.percage ? "X" + cent(g.longueurPercage) : std::string());
  if (g.type == TypePastille::NonMetallisee) n += "N";
  return n;
}

void appliquerGabarit(Pastille& p, const GabaritPastille& g) {
  p.forme = g.forme;
  p.type = g.type;
  p.largeur = g.largeur;
  p.hauteur = g.hauteur;
  p.percage = g.type == TypePastille::CMS ? 0 : g.percage;
  p.longueurPercage = g.type == TypePastille::CMS ? 0 : g.longueurPercage;
  p.arrondi = g.arrondi;
  p.chanfrein = g.chanfrein;
  p.connexion = g.connexion;
  p.margeVernis = g.margeVernis;
  if (g.margePate != 0) p.margePate = g.margePate;
  p.gabarit = g.nom;
}

std::vector<CoucheEmpilage> pileEmpilage(const Empilage& e) {
  if (!e.pile.empty()) return e.pile;
  // Déduite des réglages courts : cuivres égaux, diélectriques égaux qui font l'épaisseur totale ; l'âme au milieu,
  // préimprégnés de part et d'autre (comme le gbrjob de la 0.25)
  const std::vector<Couche> cu = couchesCuivre(e.couchesCuivre);
  const std::size_t n = cu.size();
  const std::size_t nd = n > 1 ? n - 1 : 1;
  const double d = std::max(0.0, e.epaisseur - static_cast<double>(n) * e.cuivre) / static_cast<double>(nd);
  std::vector<CoucheEmpilage> r;
  auto dielectrique = [&](bool preimpregne) {
    CoucheEmpilage x;
    x.genre = GenreCoucheEmpilage::Dielectrique;
    x.materiau = e.materiau;
    x.epaisseur = d;
    x.preimpregne = preimpregne;
    x.nom = preimpregne ? "Préimprégné" : "Âme";
    r.push_back(x);
  };
  if (n == 1) dielectrique(false);
  for (std::size_t i = 0; i < n; ++i) {
    if (i > 0) dielectrique(n > 2 && i != n / 2);
    CoucheEmpilage x;
    x.genre = GenreCoucheEmpilage::Cuivre;
    x.nom = codeCouche(cu[i]);
    x.materiau = "Cuivre";
    x.epaisseur = e.cuivre;
    x.usage = (n > 2 && i > 0 && i + 1 < n) ? UsageCuivre::Plan : UsageCuivre::Signal;
    r.push_back(x);
  }
  return r;
}

double epaisseurPile(const std::vector<CoucheEmpilage>& pile) {
  double s = 0;
  for (const auto& c : pile) s += c.epaisseur;
  return s;
}

bool cercleParTroisPoints(Pt a, Pt m, Pt b, double& cx, double& cy, double& r) {
  const double ax = static_cast<double>(a.x), ay = static_cast<double>(a.y), mx = static_cast<double>(m.x), my = static_cast<double>(m.y);
  const double bx = static_cast<double>(b.x), by = static_cast<double>(b.y);
  const double d = 2 * (ax * (my - by) + mx * (by - ay) + bx * (ay - my));
  if (std::abs(d) < 1e-3) return false;
  const double a2 = ax * ax + ay * ay, m2 = mx * mx + my * my, b2 = bx * bx + by * by;
  cx = (a2 * (my - by) + m2 * (by - ay) + b2 * (ay - my)) / d;
  cy = (a2 * (bx - mx) + m2 * (ax - bx) + b2 * (mx - ax)) / d;
  r = std::hypot(ax - cx, ay - cy);
  return std::isfinite(r);
}

namespace {
// Points d'un arc de a à b passant par m, cordes de flèche au plus « fleche »
std::vector<Pt> pointsArc(Pt a, Pt m, Pt b, Nm fleche) {
  double cx, cy, r;
  if (!cercleParTroisPoints(a, m, b, cx, cy, r)) return {a, b};
  const double t0 = std::atan2(static_cast<double>(a.y) - cy, static_cast<double>(a.x) - cx);
  const double tm = std::atan2(static_cast<double>(m.y) - cy, static_cast<double>(m.x) - cx);
  const double t1 = std::atan2(static_cast<double>(b.y) - cy, static_cast<double>(b.x) - cx);
  auto norm = [](double x) {
    while (x < 0) x += 2 * std::numbers::pi;
    while (x >= 2 * std::numbers::pi) x -= 2 * std::numbers::pi;
    return x;
  };
  // Sens : celui qui passe par m
  double balayage = norm(t1 - t0);
  if (norm(tm - t0) > balayage) balayage -= 2 * std::numbers::pi;
  const double f = std::max<double>(static_cast<double>(fleche), 100.0);
  const double pasMax = r > f ? 2 * std::acos(std::max(-1.0, 1 - f / r)) : std::numbers::pi / 4;
  const int n = std::clamp(static_cast<int>(std::ceil(std::abs(balayage) / std::max(pasMax, 1e-4))), 2, 720);
  std::vector<Pt> r2{a};
  for (int i = 1; i < n; ++i) {
    const double t = t0 + balayage * i / n;
    r2.push_back({static_cast<Nm>(std::llround(cx + r * std::cos(t))), static_cast<Nm>(std::llround(cy + r * std::sin(t)))});
  }
  r2.push_back(b);
  return r2;
}
}  // namespace

std::vector<std::pair<Pt, Pt>> segmentsPiste(const Piste& p, Nm fleche) {
  if (!p.milieu) return {{p.a, p.b}};
  const std::vector<Pt> q = pointsArc(p.a, *p.milieu, p.b, fleche);
  std::vector<std::pair<Pt, Pt>> r;
  for (std::size_t i = 1; i < q.size(); ++i) r.push_back({q[i - 1], q[i]});
  return r;
}

double longueurPiste(const Piste& p) {
  if (!p.milieu) return distance(p.a, p.b);
  double cx, cy, r;
  if (!cercleParTroisPoints(p.a, *p.milieu, p.b, cx, cy, r)) return distance(p.a, p.b);
  const double t0 = std::atan2(static_cast<double>(p.a.y) - cy, static_cast<double>(p.a.x) - cx);
  const double tm = std::atan2(static_cast<double>(p.milieu->y) - cy, static_cast<double>(p.milieu->x) - cx);
  const double t1 = std::atan2(static_cast<double>(p.b.y) - cy, static_cast<double>(p.b.x) - cx);
  auto norm = [](double x) {
    while (x < 0) x += 2 * std::numbers::pi;
    while (x >= 2 * std::numbers::pi) x -= 2 * std::numbers::pi;
    return x;
  };
  double balayage = norm(t1 - t0);
  if (norm(tm - t0) > balayage) balayage = 2 * std::numbers::pi - balayage;
  return r * balayage;
}

Polygones formePiste(const Piste& p, Nm marge) {
  if (p.milieu) {
    Polygones r;
    for (const auto& [a, b] : segmentsPiste(p)) r.push_back(oblong(a, b, p.largeur + 2 * marge));
    return unir(r);
  }
  if (p.largeurFin > 0 && p.largeurFin != p.largeur) {
    // Segment effilé : trapèze entre les deux largeurs, bouts ronds (ou carrés) de chaque largeur
    const double dx = static_cast<double>(p.b.x - p.a.x), dy = static_cast<double>(p.b.y - p.a.y), L = std::hypot(dx, dy);
    if (L < 1) return {cercle(p.a, std::max(p.largeur, p.largeurFin) / 2 + marge)};
    const double nx = -dy / L, ny = dx / L;
    const double ha = static_cast<double>(p.largeur) / 2 + static_cast<double>(marge), hb = static_cast<double>(p.largeurFin) / 2 + static_cast<double>(marge);
    auto q = [](double x, double y) { return Pt{static_cast<Nm>(std::llround(x)), static_cast<Nm>(std::llround(y))}; };
    const double ax = static_cast<double>(p.a.x), ay = static_cast<double>(p.a.y), bx = static_cast<double>(p.b.x), by = static_cast<double>(p.b.y);
    Polygone t{q(ax + nx * ha, ay + ny * ha), q(bx + nx * hb, by + ny * hb), q(bx - nx * hb, by - ny * hb), q(ax - nx * ha, ay - ny * ha)};
    Polygones r{orienter(t)};
    if (!p.boutsCarres) {
      r.push_back(cercle(p.a, static_cast<Nm>(std::llround(ha))));
      r.push_back(cercle(p.b, static_cast<Nm>(std::llround(hb))));
    }
    return unir(r);
  }
  if (p.boutsCarres) {
    const double dx = static_cast<double>(p.b.x - p.a.x), dy = static_cast<double>(p.b.y - p.a.y), L = std::hypot(dx, dy);
    if (L < 1) return {rectangle(p.a, p.largeur + 2 * marge, p.largeur + 2 * marge)};
    const double ang = std::atan2(dy, dx) * 180 / std::numbers::pi;
    return {rectangle({(p.a.x + p.b.x) / 2, (p.a.y + p.b.y) / 2}, static_cast<Nm>(std::llround(L)) + 2 * marge, p.largeur + 2 * marge, ang)};
  }
  return {oblong(p.a, p.b, p.largeur + 2 * marge)};
}

Couches couchesVia(const Via& v, int couchesCuivre) {
  const std::vector<Couche> cu = couchesCuivre <= 1 ? std::vector<Couche>{Couche::CuivreDessous} : ::prom::ecad::couchesCuivre(couchesCuivre);
  if (v.type == TypeVia::Traversante || cu.size() <= 2) return tousLesCuivres(couchesCuivre);
  int i0 = rangCuivre(v.de, couchesCuivre), i1 = rangCuivre(v.vers, couchesCuivre);
  if (i0 < 0) i0 = 0;
  if (i1 < 0) i1 = static_cast<int>(cu.size()) - 1;
  if (i0 > i1) std::swap(i0, i1);
  Couches r = 0;
  for (int i = i0; i <= i1; ++i) r |= bit(cu[static_cast<std::size_t>(i)]);
  return r;
}

Nm diametreViaSur(const Via& v, Couche c, int couchesCuivre) {
  const int rang = rangCuivre(c, couchesCuivre);
  if (rang < 0) return v.diametre;
  if (rang == std::max(1, couchesCuivre) - 1 && couchesCuivre > 1 && v.diametreDessous > 0) return v.diametreDessous;
  if (rang > 0 && rang < couchesCuivre - 1 && v.diametreInterne > 0) return v.diametreInterne;
  return v.diametre;
}

double valeurCote(const Cote& c) {
  if (c.points.empty()) return 0;
  const Pt a = c.points.front(), b = c.points.size() > 1 ? c.points[1] : c.points.front();
  switch (c.genre) {
    case GenreCote::Alignee: return mm(static_cast<Nm>(std::llround(distance(a, b))));
    case GenreCote::Horizontale: return std::abs(mm(b.x - a.x));
    case GenreCote::Verticale: return std::abs(mm(b.y - a.y));
    case GenreCote::Rayon: return mm(static_cast<Nm>(std::llround(distance(a, b))));
    case GenreCote::Diametre: return 2 * mm(static_cast<Nm>(std::llround(distance(a, b))));
    case GenreCote::Ordonnee: return c.points.size() > 1 ? mm(b.x - a.x) : 0;
    case GenreCote::Angulaire: {
      if (c.points.size() < 3) return 0;
      const Pt o = c.points[0], p1 = c.points[1], p2 = c.points[2];
      const double t1 = std::atan2(static_cast<double>(p1.y - o.y), static_cast<double>(p1.x - o.x));
      const double t2 = std::atan2(static_cast<double>(p2.y - o.y), static_cast<double>(p2.x - o.x));
      double d = std::abs(t2 - t1) * 180 / std::numbers::pi;
      if (d > 180) d = 360 - d;
      return d;
    }
    case GenreCote::Centre: return 0;
  }
  return 0;
}

std::string texteCote(const Cote& c) {
  if (!c.texte.empty()) return c.prefixe + c.texte + c.suffixe;
  if (c.genre == GenreCote::Centre) return {};
  double v = valeurCote(c);
  std::string unite = c.unite;
  if (c.genre == GenreCote::Angulaire) unite = "°";
  else if (c.unite == "mil") v = v / 0.0254;
  else if (c.unite == "in") v = v / 25.4;
  char t[48];
  std::snprintf(t, sizeof t, "%.*f", std::clamp(c.precision, 0, 6), v);
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  const std::string debut = c.genre == GenreCote::Rayon ? "R" : c.genre == GenreCote::Diametre ? "⌀" : "";
  return c.prefixe + debut + s + (unite == "°" ? "°" : " " + unite) + c.suffixe;
}

std::vector<Pt> contourDessin(const DessinCircuit& d) {
  const auto& p = d.points;
  switch (d.forme) {
    case DessinCircuit::Forme::Trait:
    case DessinCircuit::Forme::Polygone: return p;
    case DessinCircuit::Forme::Arc: return p.size() >= 3 ? pointsArc(p[0], p[1], p[2], 2'000) : p;
    case DessinCircuit::Forme::Cercle: {
      if (p.size() < 2) return p;
      Polygone q = cercle(p[0], static_cast<Nm>(std::llround(distance(p[0], p[1]))));
      q.push_back(q.front());
      return q;
    }
    case DessinCircuit::Forme::Rectangle: {
      if (p.size() < 2) return p;
      const Pt c{(p[0].x + p[1].x) / 2, (p[0].y + p[1].y) / 2};
      const Nm l = std::abs(p[1].x - p[0].x), h = std::abs(p[1].y - p[0].y);
      Polygone q = d.rayon > 0 ? rectangleArrondi(c, l, h, std::min(d.rayon, std::min(l, h) / 2)) : rectangle(c, l, h);
      if (!q.empty()) q.push_back(q.front());
      return q;
    }
    case DessinCircuit::Forme::Bezier: {
      if (p.size() < 4) return p;
      std::vector<Pt> q;
      const int n = 32;
      for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / n, u = 1 - t;
        const double x = u * u * u * static_cast<double>(p[0].x) + 3 * u * u * t * static_cast<double>(p[1].x) + 3 * u * t * t * static_cast<double>(p[2].x) + t * t * t * static_cast<double>(p[3].x);
        const double y = u * u * u * static_cast<double>(p[0].y) + 3 * u * u * t * static_cast<double>(p[1].y) + 3 * u * t * t * static_cast<double>(p[2].y) + t * t * t * static_cast<double>(p[3].y);
        q.push_back({static_cast<Nm>(std::llround(x)), static_cast<Nm>(std::llround(y))});
      }
      return q;
    }
  }
  return p;
}

Pastille pastilleSurCarte(const EmpreintePlacee& e, const Pastille& p) {
  Pastille r = p;
  r.position = versCarte(e, p.position);
  r.rotation = (e.dessous ? -p.rotation : p.rotation) + e.rotation;
  if (e.dessous) {
    // Retournée : les formes propres à la pastille passent en miroir avec elle (repère de la pastille, x → −x)
    auto miroir = [](Polygone& q) {
      for (Pt& s : q) s.x = -s.x;
      std::reverse(q.begin(), q.end());
    };
    miroir(r.contourLibre);
    miroir(r.vernisLibre);
    miroir(r.pateLibre);
    r.decalagePercage.x = -r.decalagePercage.x;
    for (auto& t : r.trousEnPlus) {
      t.decalage.x = -t.decalage.x;
      t.rotation = -t.rotation;
    }
    // Coins chanfreinés : gauche et droite s'échangent (1 ↔ 2, 8 ↔ 4)
    const int k = r.coinsChanfrein;
    r.coinsChanfrein = ((k & 1) << 1) | ((k & 2) >> 1) | ((k & 4) << 1) | ((k & 8) >> 1);
  }
  return r;
}

Pt surPastille(const Pastille& p, Pt local) { return tournerDegres(local, p.rotation) + p.position; }

Couches couchesPastille(const Pastille& p, bool dessous, int couchesCuivreNb) {
  const Couches CUIVRES = tousLesCuivres(MAX_CUIVRES);
  if (p.couches != 0) {
    // Couches explicites (X2), données dans le repère de l'empreinte posée dessus : retournées avec elle
    Couches r = 0;
    for (int i = 0; i < NB_COUCHES; ++i) {
      const Couche c = static_cast<Couche>(i);
      if (p.couches & bit(c)) r |= bit(dessous ? opposee(c) : c);
    }
    // Un trou métallisé traverse tous les cuivres de l'empilage ; un trou non métallisé n'en a aucun
    if (p.type == TypePastille::Traversante) r = (r & ~CUIVRES) | tousLesCuivres(couchesCuivreNb);
    else if (p.type == TypePastille::NonMetallisee) r &= ~CUIVRES;
    return r;
  }
  switch (p.type) {
    case TypePastille::CMS:
      return dessous ? (bit(Couche::CuivreDessous) | bit(Couche::VernisDessous) | bit(Couche::PateDessous)) : (bit(Couche::CuivreDessus) | bit(Couche::VernisDessus) | bit(Couche::PateDessus));
    case TypePastille::Traversante: return tousLesCuivres(couchesCuivreNb) | bit(Couche::VernisDessus) | bit(Couche::VernisDessous);
    case TypePastille::NonMetallisee: return bit(Couche::VernisDessus) | bit(Couche::VernisDessous);
  }
  return 0;
}

Polygone encombrementSurCarte(const EmpreintePlacee& e, const DefEmpreinte& d) {
  const Boite& b = d.encombrement;
  return orienter({versCarte(e, {b.x0, b.y0}), versCarte(e, {b.x1, b.y0}), versCarte(e, {b.x1, b.y1}), versCarte(e, {b.x0, b.y1})});
}

Pt pointDePlacement(const DefEmpreinte& d) {
  // Boîte exacte de chaque pastille, sans passer par son polygone : les facettes d'un cercle décalaient le centre
  // d'un micron
  Boite b;
  for (const auto& p : d.pastilles) {
    if (p.type == TypePastille::NonMetallisee) continue;
    const double a = p.rotation * std::numbers::pi / 180, ca = std::abs(std::cos(a)), sa = std::abs(std::sin(a));
    const double L = static_cast<double>(p.largeur), H = static_cast<double>(p.hauteur);
    double dx = L / 2, dy = L / 2;
    if (p.forme == FormePastille::Ovale && p.largeur != p.hauteur) {
      // Oblong : demi-longueur du segment central le long de son axe, plus le rayon
      const double demi = std::abs(L - H) / 2, r = std::min(L, H) / 2;
      dx = (L > H ? ca : sa) * demi + r;
      dy = (L > H ? sa : ca) * demi + r;
    } else if (p.forme != FormePastille::Rond && p.forme != FormePastille::Ovale) {
      dx = (ca * L + sa * H) / 2;
      dy = (sa * L + ca * H) / 2;
    }
    b.etendre(Boite{p.position.x - static_cast<Nm>(std::llround(dx)), p.position.y - static_cast<Nm>(std::llround(dy)), p.position.x + static_cast<Nm>(std::llround(dx)),
                    p.position.y + static_cast<Nm>(std::llround(dy))});
  }
  return b.vide() ? Pt{} : b.centre();
}

std::string familleBoitier(const std::string& idBrut) {
  std::string id = idBrut;
  for (const std::string prefixe : {"kicad:", "perso:"})
    if (id.rfind(prefixe, 0) == 0) id = id.substr(prefixe.size());
  if (const auto i = id.find(':'); i != std::string::npos) id = id.substr(i + 1);  // bibliothèque KiCad « Package_SO: »
  if (const auto i = id.find('#'); i != std::string::npos) id = id.substr(0, i);  // variante d'import « #2 »
  auto commence = [&id](const char* p) { return id.rfind(p, 0) == 0; };
  auto exact = [&id](const char* p) { return id == p || id.rfind(std::string(p) + "_", 0) == 0; };
  // Du plus précis au plus général : SOT-223 avant SOT-23, SOT-23-5 avant SOT-23
  if (commence("SOT-223")) return "SOT-223";
  if (exact("SOT-23-5") || exact("TSOT-23-5")) return "SOT-23-5";
  if (exact("SOT-23-6") || exact("TSOT-23-6")) return "SOT-23-6";
  if (exact("SOT-23") || exact("SOT-23-3")) return "SOT-23";
  if (commence("TO-220")) return "TO-220";
  if (commence("TO-92")) return "TO-92";
  if (commence("TO-252")) return "TO-252";
  if (commence("TO-263")) return "TO-263";
  for (const char* p : {"SOIC-", "TSSOP-", "DIP-", "SSOP-", "MSOP-", "SO-"})
    if (commence(p)) return "DIL";
  for (const char* p : {"QFN-", "DFN-", "VQFN-", "WQFN-", "UQFN-"})
    if (commence(p)) return "QFN";
  for (const char* p : {"TQFP-", "LQFP-", "QFP-"})
    if (commence(p)) return "QFP";
  for (const char* p : {"R_", "C_", "L_", "LED_", "Fuse_", "D_", "CP_", "Crystal_", "Buzzer_"})
    if (commence(p)) return "2 bornes";
  for (const char* p : {"PinHeader_", "PinSocket_", "TerminalBlock_", "Conn_", "JST_"})
    if (commence(p)) return "barrette";
  if (commence("SW_PUSH")) return "bouton";
  if (commence("SW_Slide")) return "inverseur";
  if (commence("Trimmer_") || commence("Potentiometer_")) return "potentiomètre";
  if (commence("MountingHole")) return "trou";
  if (commence("TestPoint")) return "point de test";
  return {};
}

const std::vector<std::string>& proprietesFabricationPastille() {
  static const std::vector<std::string> p{"", "bga", "fiducialLocal", "fiducialGlobal", "test", "dissipateur", "castellee", "insertion"};
  return p;
}

const char* fonctionGerberPastille(const std::string& f, bool cms) {
  // Attributs .AperFunction de la spécification Gerber X2 (Ucamco, révision 2023.08)
  if (f == "bga") return "BGAPad,CuDef";
  if (f == "fiducialLocal") return "FiducialPad,Local";
  if (f == "fiducialGlobal") return "FiducialPad,Global";
  if (f == "test") return "TestPad";
  if (f == "dissipateur") return "HeatsinkPad";
  if (f == "castellee") return "CastellatedPad";
  return cms ? "SMDPad,CuDef" : "ComponentPad";
}

}  // namespace prom::ecad

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
                                      "Sérigraphie dessus", "Sérigraphie dessous", "Contour de carte", "Encombrement dessus", "Encombrement dessous", "Fabrication dessus", "Fabrication dessous"};
const char* const CODES[NB_COUCHES] = {"F.Cu", "In1.Cu", "In2.Cu", "B.Cu", "F.Mask", "B.Mask", "F.Paste", "B.Paste", "F.SilkS", "B.SilkS", "Edge.Cuts", "F.CrtYd", "B.CrtYd", "F.Fab", "B.Fab"};
}  // namespace

const char* nomCouche(Couche c) { return NOMS[static_cast<int>(c)]; }
const char* codeCouche(Couche c) { return CODES[static_cast<int>(c)]; }
std::optional<Couche> coucheDepuisCode(const std::string& code) {
  for (int i = 0; i < NB_COUCHES; ++i)
    if (code == CODES[i]) return static_cast<Couche>(i);
  return std::nullopt;
}
bool estCuivre(Couche c) { return c == Couche::CuivreDessus || c == Couche::CuivreInt1 || c == Couche::CuivreInt2 || c == Couche::CuivreDessous; }
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
    default: return c;
  }
}
std::vector<Couche> couchesCuivre(int nombre) {
  if (nombre >= 4) return {Couche::CuivreDessus, Couche::CuivreInt1, Couche::CuivreInt2, Couche::CuivreDessous};
  // Simple face : le cuivre sous la carte, les composants traversants posés dessus (gravure maison)
  if (nombre == 1) return {Couche::CuivreDessous};
  return {Couche::CuivreDessus, Couche::CuivreDessous};
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
  }
  return {};
}

Pastille pastilleSurCarte(const EmpreintePlacee& e, const Pastille& p) {
  Pastille r = p;
  r.position = versCarte(e, p.position);
  r.rotation = (e.dessous ? -p.rotation : p.rotation) + e.rotation;
  return r;
}

Couches couchesPastille(const Pastille& p, bool dessous, int couchesCuivreNb) {
  constexpr Couches CUIVRES = bit(Couche::CuivreDessus) | bit(Couche::CuivreInt1) | bit(Couche::CuivreInt2) | bit(Couche::CuivreDessous);
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

}  // namespace prom::ecad

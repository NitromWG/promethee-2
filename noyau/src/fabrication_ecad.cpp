// Prométhée : fabrication du circuit imprimé. Un modèle de fabrication indépendant des formats (couches de
// primitives, perçages), puis les traducteurs : Gerber RS-274X avec attributs X2, Excellon, placement et
// nomenclature en CSV, et le dossier complet prêt à envoyer au fabricant. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <numbers>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "promethee/csv.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/electronique.hpp"
#include "promethee/police.hpp"
#include "promethee/fabrication_detaillee.hpp"
#include "promethee/regles_carte.hpp"

namespace prom::ecad {

namespace {
bool quartDeTour(double degres) { return std::abs(std::remainder(degres, 90.0)) < 1e-6; }
bool tourne90(double degres) { return std::abs(std::remainder(degres, 180.0)) > 1.0; }
std::string sur(const std::string& s) {
  std::string r;
  for (char ch : asciiFabrication(s)) r += (ch == '*' || ch == '%' || ch == ',' || ch == '\n') ? '_' : ch;
  return r;
}
std::string decimal(Nm v) {
  char t[32];
  std::snprintf(t, sizeof t, "%.6f", static_cast<double>(v) * 1e-6);
  return t;
}
double aireSignee(const Polygone& p) {
  double a = 0;
  for (std::size_t i = 0; i < p.size(); ++i) {
    const Pt& u = p[i];
    const Pt& v = p[(i + 1) % p.size()];
    a += static_cast<double>(u.x) * static_cast<double>(v.y) - static_cast<double>(v.x) * static_cast<double>(u.y);
  }
  return a / 2;
}

// Fenêtres de pâte d'une grande pastille CMS (EL-31, F2) : n × n carrés répartis dans le rectangle tourné de la
// pastille, chacun découpé par la forme réelle du cuivre (ronde, ovale, arrondie) ; leur taille est ajustée pour que
// l'aire de pâte soit « taux » de l'aire réelle de la pastille. Aucune fenêtre ne sort du cuivre : de la pâte posée
// sur le vernis donne des billes et des ponts de soudure.
std::vector<PrimitiveFab> fenetresPate(const Pastille& pa, const Polygone& forme, double taux) {
  const double aireCuivre = aireMm2(forme);
  const int n = std::clamp(static_cast<int>(std::ceil(std::sqrt(mm(pa.largeur) * mm(pa.hauteur) / 2.0))), 2, 6);
  const double a = pa.rotation * std::numbers::pi / 180, ca = std::cos(a), sa = std::sin(a);
  // « f » : part de chaque case de la grille occupée par sa fenêtre, de 0 à 1
  auto construire = [&](double f) {
    Polygones r;
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j) {
        const double u = (i + 0.5) / n - 0.5, v = (j + 0.5) / n - 0.5;
        const double dx = u * static_cast<double>(pa.largeur), dy = v * static_cast<double>(pa.hauteur);
        const Pt centre{pa.position.x + static_cast<Nm>(std::llround(dx * ca - dy * sa)), pa.position.y + static_cast<Nm>(std::llround(dx * sa + dy * ca))};
        const Polygone carre = rectangle(centre, static_cast<Nm>(std::llround(f * static_cast<double>(pa.largeur) / n)), static_cast<Nm>(std::llround(f * static_cast<double>(pa.hauteur) / n)), pa.rotation);
        for (auto& p : intersecter({carre}, {forme}))
          if (p.size() >= 3 && aireMm2(p) > 1e-4) r.push_back(std::move(p));
      }
    return r;
  };
  auto aire = [](const Polygones& v) {
    double s = 0;
    for (const auto& p : v) s += aireMm2(p);
    return s;
  };
  // Recherche par dichotomie de la taille qui donne le taux voulu sur l'aire réelle (pas sur le rectangle)
  double bas = 0.05, haut = 1.0;
  for (int k = 0; k < 18 && aireCuivre > 0; ++k) {
    const double milieu = (bas + haut) / 2;
    (aire(construire(milieu)) < taux * aireCuivre ? bas : haut) = milieu;
  }
  std::vector<PrimitiveFab> r;
  for (auto& p : construire(bas)) {
    PrimitiveFab f;
    f.type = PrimitiveFab::Type::Region;
    f.region = std::move(p);
    r.push_back(std::move(f));
  }
  return r;
}

// Fonction X2 d'un fichier Gerber : la même dans son attribut %TF.FileFunction et dans le gbrjob, dont la norme
// reprend les valeurs de l'attribut (« Soldermask,Top », « Paste,Top », « Profile,NP »)
std::string fonctionFichier(Couche couche, int nCuivre) {
  // Cuivres internes (8.20) : leur rang physique dans l'empilage, de 2 à n − 1
  if (estCuivreInterne(couche)) return "Copper,L" + std::to_string(rangCuivre(couche, std::max(nCuivre, 3)) + 1) + ",Inr";
  switch (couche) {
    case Couche::CuivreDessus: return "Copper,L1,Top";
    case Couche::CuivreDessous: return "Copper,L" + std::to_string(std::max(1, nCuivre)) + ",Bot";  // L1 en simple face
    case Couche::ColleDessus: return "Glue,Top";
    case Couche::ColleDessous: return "Glue,Bot";
    case Couche::ProtectionDessus: return "Other,Coating-Top";
    case Couche::ProtectionDessous: return "Other,Coating-Bot";
    case Couche::VernisDessus: return "Soldermask,Top";
    case Couche::VernisDessous: return "Soldermask,Bot";
    case Couche::PateDessus: return "Paste,Top";
    case Couche::PateDessous: return "Paste,Bot";
    case Couche::SerigraphieDessus: return "Legend,Top";
    case Couche::SerigraphieDessous: return "Legend,Bot";
    case Couche::Contour: return "Profile,NP";
    default: return "Other,Comment";
  }
}
bool fichierNegatif(Couche couche) { return couche == Couche::VernisDessus || couche == Couche::VernisDessous; }

// Valeurs de la norme du gbrjob : finition (GeneralSpecs.Finish) et couleur (MaterialStackup.Color)
std::string finitionNormee(const std::string& f) {
  static const std::pair<const char*, const char*> table[] = {
      {"HASL sans plomb", "HAL lead-free"}, {"HASL", "HAL SnPb"}, {"HASL plomb", "HAL SnPb"}, {"ENIG", "Immersion gold (ENIG)"}, {"ENEPIG", "ENEPIG"},
      {"OSP", "OSP"}, {"argent chimique", "Immersion silver"}, {"étain chimique", "Immersion tin"}, {"or dur", "Hard gold"}, {"aucune", "None"}};
  for (const auto& [nom, norme] : table)
    if (f == nom) return norme;
  return f;  // la norme admet une chaîne libre
}
std::string couleurNormee(const std::string& c) {
  static const std::pair<const char*, const char*> table[] = {{"vert", "Green"}, {"bleu", "Blue"},   {"rouge", "Red"},   {"noir", "Black"},
                                                              {"blanc", "White"}, {"jaune", "Yellow"}, {"violet", "R75G44B143"}};
  for (const auto& [nom, norme] : table)
    if (c == nom) return norme;
  return {};
}
}  // namespace

Polygone regionAvecCoupures(const Polygone& exterieur, const Polygones& trous) {
  // Élimination des trous : chaque trou, du plus à droite au plus à gauche, est relié au contour par le rayon
  // horizontal qui part de son sommet le plus à droite ; le contour parcourt alors le trou en sens inverse puis
  // revient par la même coupure, d'épaisseur nulle. Sommets en liste chaînée et arêtes rangées par tranches
  // horizontales : le rayon ne teste que les arêtes de sa tranche (une zone de milliers de trous en quelques ms)
  Polygone ext = exterieur;
  if (aireSignee(ext) < 0) std::reverse(ext.begin(), ext.end());
  std::vector<Polygone> liste;
  for (const auto& t : trous)
    if (t.size() >= 3) {
      Polygone h = t;
      if (aireSignee(h) > 0) std::reverse(h.begin(), h.end());
      liste.push_back(std::move(h));
    }
  if (liste.empty() || ext.size() < 3) return ext;
  auto xmax = [](const Polygone& p) { return std::max_element(p.begin(), p.end(), [](const Pt& a, const Pt& b) { return a.x < b.x; }) - p.begin(); };
  std::sort(liste.begin(), liste.end(), [&](const Polygone& a, const Polygone& b) { return a[static_cast<std::size_t>(xmax(a))].x > b[static_cast<std::size_t>(xmax(b))].x; });
  std::size_t total = ext.size();
  Nm ymin = ext.front().y, ymax = ext.front().y;
  for (const Pt& q : ext) ymin = std::min(ymin, q.y), ymax = std::max(ymax, q.y);
  for (const auto& h : liste) {
    total += h.size() + 3;
    for (const Pt& q : h) ymin = std::min(ymin, q.y), ymax = std::max(ymax, q.y);
  }
  std::vector<Pt> pts;
  std::vector<std::size_t> suiv;
  std::vector<char> retire;
  pts.reserve(total);
  suiv.reserve(total);
  retire.reserve(total);
  for (std::size_t i = 0; i < ext.size(); ++i) {
    pts.push_back(ext[i]);
    suiv.push_back(i + 1 == ext.size() ? 0 : i + 1);
    retire.push_back(0);
  }
  const std::size_t nb = std::clamp<std::size_t>(total / 8, 1, 4096);
  const double hauteur = static_cast<double>(ymax - ymin) + 1;
  auto tranche = [&](Nm y) { return std::min(nb - 1, static_cast<std::size_t>(static_cast<double>(y - ymin) / hauteur * static_cast<double>(nb))); };
  std::vector<std::vector<std::size_t>> tranches(nb);
  auto inscrire = [&](std::size_t i) {
    const Pt a = pts[i], b = pts[suiv[i]];
    if (a.y == b.y) return;  // horizontale (coupure comprise) : jamais touchée par le rayon
    for (std::size_t k = tranche(std::min(a.y, b.y)); k <= tranche(std::max(a.y, b.y)); ++k) tranches[k].push_back(i);
  };
  for (std::size_t i = 0; i < ext.size(); ++i) inscrire(i);
  auto nouveau = [&](Pt p) {
    pts.push_back(p);
    suiv.push_back(0);
    retire.push_back(0);
    return pts.size() - 1;
  };
  for (const auto& h : liste) {
    const std::size_t ih = static_cast<std::size_t>(xmax(h));
    const Pt H = h[ih];
    // Arête du contour courant que le rayon horizontal vers la droite touche en premier
    double meilleur = 1e300;
    std::size_t arete = 0;
    Pt P = H;
    for (const std::size_t i : tranches[tranche(H.y)]) {
      if (retire[i]) continue;
      const Pt a = pts[i], b = pts[suiv[i]];
      if (a.y == b.y || H.y < std::min(a.y, b.y) || H.y > std::max(a.y, b.y)) continue;
      const double x = static_cast<double>(a.x) + static_cast<double>(H.y - a.y) * static_cast<double>(b.x - a.x) / static_cast<double>(b.y - a.y);
      if (x >= static_cast<double>(H.x) && x - static_cast<double>(H.x) < meilleur) {
        meilleur = x - static_cast<double>(H.x);
        arete = i;
        P = {static_cast<Nm>(std::llround(x)), H.y};
      }
    }
    if (meilleur >= 1e300) continue;  // trou hors du contour : ignoré
    std::size_t apres = suiv[arete];
    std::size_t courant = arete;
    if (pts[arete] != P) {
      const std::size_t p1 = nouveau(P);
      suiv[courant] = p1;  // l'arête raccourcie garde ses tranches (elles la couvrent toujours)
      courant = p1;
    }
    for (std::size_t k = 0; k <= h.size(); ++k) {
      const std::size_t n = nouveau(h[(ih + k) % h.size()]);
      suiv[courant] = n;
      if (courant != arete) inscrire(courant);
      courant = n;
    }
    const std::size_t p2 = nouveau(P);
    suiv[courant] = p2;
    inscrire(courant);
    // Le sommet suivant confondu avec la coupure est sauté (sauf le premier sommet, d'où part le parcours)
    if (apres != 0 && pts[apres] == P) {
      retire[apres] = 1;
      apres = suiv[apres];
    }
    suiv[p2] = apres;
    inscrire(p2);
  }
  Polygone r;
  r.reserve(pts.size());
  std::size_t i = 0;
  do {
    r.push_back(pts[i]);
    i = suiv[i];
  } while (i != 0 && r.size() <= pts.size());
  return r;
}

Pt positionRepere(const EmpreintePlacee& e, const DefEmpreinte& d, Nm hauteurTexte) {
  if (e.repere) return versCarte(e, *e.repere);
  const Nm h = std::max<Nm>(hauteurTexte, 500'000), ep = epaisseurTexte(h);
  const Boite b = boite(encombrementSurCarte(e, d));
  return {b.centre().x, b.y0 - ep - h / 2};
}

std::vector<TraitSerigraphie> textesSerigraphie(const ConceptionElec& c) {
  std::vector<TraitSerigraphie> r;
  const OptionsSerigraphie& o = c.circuit.serigraphie;
  const Nm h = std::max<Nm>(o.hauteur, 500'000), ep = epaisseurTexte(h);
  auto ecrire = [&](const std::string& texte, Pt p, Nm hauteur, double rot, Couche couche, const std::string& composant) {
    const bool dessous = couche == Couche::SerigraphieDessous || couche == Couche::CuivreDessous || couche == Couche::VernisDessous;
    for (const auto& t : traitsTexte(texte, p, hauteur, rot, dessous)) r.push_back({couche, t.a, t.b, epaisseurTexte(hauteur), composant, texte});
  };
  if (o.reperes || o.valeurs)
    for (const auto& e : c.circuit.empreintes) {
      const ComposantElec* x = c.composant(e.composant);
      const DefEmpreinte* d = c.empreinte(e.empreinte);
      if (!x || !d) continue;
      const DefSymbole* s = c.symbole(x->symbole);
      if (s && s->mecanique && x->reference.empty()) continue;
      const Boite b = boite(encombrementSurCarte(e, *d));
      const Couche face = e.dessous ? Couche::SerigraphieDessous : Couche::SerigraphieDessus;
      // Repère au-dessus de l'encombrement, valeur au-dessous : hors des pastilles, lisible sans tourner la carte
      if (o.reperes && !x->reference.empty()) ecrire(x->reference, positionRepere(e, *d, o.hauteur), h, 0, face, x->uuid);
      if (o.valeurs && !x->valeur.empty()) ecrire(x->valeur, {b.centre().x, b.y1 + ep + h / 2}, h, 0, face, x->uuid);
    }
  for (const auto& t : c.circuit.textes) {
    if (t.texte.empty()) continue;
    ecrire(t.texte, t.p, std::max<Nm>(t.hauteur, 300'000), t.rotation, t.couche, {});
  }
  (void)ep;
  return r;
}

std::string asciiFabrication(const std::string& s) {
  // Lettres latines accentuées ramenées à leur base ; le reste hors ASCII devient « _ » (une fois par caractère)
  static const char* const latin1[64] = {"A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I", "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "_", "ss",
                                         "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i", "d", "n", "o", "o", "o", "o", "o", "_", "o", "u", "u", "u", "u", "y", "_", "y"};
  std::string r;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      r += (c >= 32 || c == '\t') ? static_cast<char>(c) : '_';
      ++i;
      continue;
    }
    // Longueur de la séquence UTF-8 et point de code
    const std::size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    std::uint32_t cp = n == 2 ? c & 0x1F : n == 3 ? c & 0x0F : c & 0x07;
    for (std::size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += n;
    if (cp >= 0xC0 && cp <= 0xFF) r += latin1[cp - 0xC0];
    else if (cp == 0x152) r += "OE";
    else if (cp == 0x153) r += "oe";
    else if (cp == 0x2019 || cp == 0x2018) r += '\'';
    else if (cp == 0x2013 || cp == 0x2014) r += '-';
    else r += '_';
  }
  return r;
}

Boite repereFichiers(const ConceptionElec& c) {
  Boite b = boite(c.circuit.contour.polygone());
  const auto& a = c.circuit.affichage;
  if (a.origineFabrication && a.origineTravail) {
    // L'origine de travail devient le zéro des fichiers : x0 et y1 (y vers le haut dans les fichiers)
    const Nm dx = a.origineTravail->x - b.x0, dy = a.origineTravail->y - b.y1;
    b.x0 += dx;
    b.x1 += dx;
    b.y0 += dy;
    b.y1 += dy;
  }
  return b;
}

ReglagesColle reglagesColle(const ConceptionElec& c) {
  ReglagesColle r;
  if (const auto it = c.extensions.find("colle"); it != c.extensions.end() && it->second.is_object()) {
    const std::string faces = it->second.value("faces", std::string());
    r.dessus = faces == "dessus" || faces == "deux";
    r.dessous = faces == "dessous" || faces == "deux";
    r.diametre = nm(it->second.value("diametre", 0.6));
  }
  return r;
}

std::set<std::string> composantsSansPate(const ConceptionElec& c) {
  std::set<std::string> r;
  const auto& K = c.circuit.regles.avancees.contraintes;
  if (std::none_of(K.begin(), K.end(), [](const Contrainte& k) { return k.active && k.genre == GenreContrainte::PateNonMontes && k.texte != "non"; })) return r;
  for (const auto& x : c.composants)
    if (x.nePasMonter && resoudre(c, GenreContrainte::PateNonMontes, proprietesComposant(c, x)).trouvee) r.insert(x.uuid);
  return r;
}

ModeleFabrication modeleFabrication(const ConceptionElec& c, const Connectivite& k, const Remplissages& remplis) {
  ModeleFabrication m;
  m.nom = c.nom;
  m.uuid = c.uuid;
  m.revision = c.cartouche.revision;
  m.date = c.cartouche.date;
  m.contour = c.circuit.contour.polygone();
  m.boite = boite(m.contour);
  m.repere = repereFichiers(c);
  const int nc = c.circuit.empilage.couchesCuivre;
  m.couchesCuivre = nc;
  std::vector<Couche> liste = couchesCuivre(nc);
  for (Couche x : {Couche::VernisDessus, Couche::VernisDessous, Couche::PateDessus, Couche::PateDessous, Couche::SerigraphieDessus, Couche::SerigraphieDessous, Couche::Contour}) liste.push_back(x);
  // Couches de colle (8.14) : seulement si une empreinte en porte
  for (Couche x : {Couche::ColleDessus, Couche::ColleDessous})
    for (const auto& e : c.circuit.empreintes)
      if (const DefEmpreinte* d = c.empreinte(e.empreinte);
          d && std::any_of(d->traits.begin(), d->traits.end(), [&](const TraitEmpreinte& t) { return coucheSurCarte(e, t.couche) == x; })) {
        liste.push_back(x);
        break;
      }
  // Points de colle tirés du corps (8.14, FAB-19) : réglage du projet, extensions["colle"] = {faces, diametre}
  const ReglagesColle colle = reglagesColle(c);
  for (Couche x : {Couche::ColleDessus, Couche::ColleDessous})
    if (((x == Couche::ColleDessus && colle.dessus) || (x == Couche::ColleDessous && colle.dessous)) && std::find(liste.begin(), liste.end(), x) == liste.end()) liste.push_back(x);
  // Vernis de protection (8.14, FAB-27) et dessins de la carte sur les couches techniques : la couche existe dès
  // qu'un dessin y est posé
  for (Couche x : {Couche::ColleDessus, Couche::ColleDessous, Couche::ProtectionDessus, Couche::ProtectionDessous})
    if (std::find(liste.begin(), liste.end(), x) == liste.end() &&
        std::any_of(c.circuit.dessins.begin(), c.circuit.dessins.end(), [&](const DessinCircuit& d) { return d.couche == x; }))
      liste.push_back(x);
  for (Couche x : liste) m.couches.push_back({x, {}});
  auto couche = [&](Couche x) -> CoucheFab* {
    for (auto& cf : m.couches)
      if (cf.couche == x) return &cf;
    return nullptr;
  };
  // Zones de cuivre : une région par îlot, ses trous rattachés au contour par des coupures (« cut-ins »), comme
  // le permet la norme Gerber (EL-47) : ni triangulation ni fichiers démesurés
  for (const auto& z : c.circuit.zones) {
    CoucheFab* cf = couche(z.couche);
    const auto it = remplis.find(z.uuid);
    if (!cf || it == remplis.end()) continue;
    for (const Ilot& il : ilots(it->second)) {
      PrimitiveFab p;
      p.type = PrimitiveFab::Type::Region;
      p.region = regionAvecCoupures(il.exterieur, il.trous);
      p.fonction = "Conductor";
      p.reseau = z.reseau;
      cf->primitives.push_back(std::move(p));
    }
  }
  // Nom de chaque broche (F10) : le troisième champ de %TO.P est la fonction de la broche (« SDA », « VCC »), pas son
  // réseau, qui est déjà dans %TO.N
  std::map<std::pair<std::string, std::string>, std::string> nomsBroches;
  for (const auto& x : c.composants)
    if (const DefSymbole* d = c.symbole(x.symbole))
      for (const auto& b : d->broches)
        if (!b.nom.empty() && b.nom != "~") nomsBroches.emplace(std::make_pair(x.uuid, b.numero), b.nom);
  // Simple face : aucun trou n'est métallisé, la carte n'a qu'une couche de cuivre (F9)
  const bool simpleFace = nc == 1;
  // Pastilles : cuivre, vernis (agrandi), pâte (CMS) ; pas de pâte sous un non-monté quand la règle le demande (8.14)
  const std::set<std::string> sansPate = composantsSansPate(c);
  // Marges de vernis et de pâte posées par les règles d'assemblage (8.14), par composant ; la pastille garde la sienne
  std::map<std::string, Nm> margeVernisRegle, margePateRegle;
  {
    const auto& K = c.circuit.regles.avancees.contraintes;
    const bool mv = std::any_of(K.begin(), K.end(), [](const Contrainte& q) { return q.active && q.genre == GenreContrainte::MargeVernis; });
    const bool mp = std::any_of(K.begin(), K.end(), [](const Contrainte& q) { return q.active && q.genre == GenreContrainte::MargePate; });
    if (mv || mp)
      for (const auto& x : c.composants) {
        const Proprietes pr = proprietesComposant(c, x);
        if (mv)
          if (const Resolution r = resoudre(c, GenreContrainte::MargeVernis, pr); r.trouvee) margeVernisRegle[x.uuid] = r.valeur.min ? r.valeur.min : r.valeur.pref;
        if (mp)
          if (const Resolution r = resoudre(c, GenreContrainte::MargePate, pr); r.trouvee) margePateRegle[x.uuid] = r.valeur.min ? r.valeur.min : r.valeur.pref;
      }
  }
  const auto pp = pastilles(c, k);
  for (const auto& x : pp) {
    const Pastille& pa = x.pastille;
    std::string nomBroche;
    if (const auto it = nomsBroches.find({x.composant, x.numero}); it != nomsBroches.end()) nomBroche = it->second;
    auto primitive = [&](Nm marge, const char* fonction) {
      PrimitiveFab p;
      p.a = pa.position;
      p.fonction = fonction;
      p.reseau = x.reseau;
      p.reference = x.reference;
      p.numero = x.numero;
      p.broche = nomBroche;
      if (pa.forme == FormePastille::Rond) { p.type = PrimitiveFab::Type::Disque; p.l = pa.largeur + 2 * marge; }
      else if ((pa.forme == FormePastille::Rect || pa.forme == FormePastille::Ovale) && quartDeTour(pa.rotation)) {
        p.type = pa.forme == FormePastille::Rect ? PrimitiveFab::Type::Rect : PrimitiveFab::Type::Oblong;
        p.l = (tourne90(pa.rotation) ? pa.hauteur : pa.largeur) + 2 * marge;
        p.h = (tourne90(pa.rotation) ? pa.largeur : pa.hauteur) + 2 * marge;
      } else if (pa.forme == FormePastille::RectArrondi && marge >= 0) {
        // Rectangle arrondi (8.4, F12) : une macro d'ouverture, quelle que soit sa rotation, plutôt qu'une région
        p.type = PrimitiveFab::Type::RectArrondi;
        p.l = pa.largeur + 2 * marge;
        p.h = pa.hauteur + 2 * marge;
        p.r = static_cast<Nm>(pa.arrondi * static_cast<double>(std::min(pa.largeur, pa.hauteur))) + marge;
        p.rotation = pa.rotation;
      } else {
        p.type = PrimitiveFab::Type::Region;
        const Polygones g = marge != 0 ? gonfler({x.forme}, marge) : Polygones{x.forme};
        p.region = g.empty() ? x.forme : g.front();
      }
      return p;
    };
    // Forme libre de vernis ou de pâte (4.24) : dans le repère de la pastille
    auto libre = [&](const Polygone& q, const char* fonction) {
      PrimitiveFab p;
      p.type = PrimitiveFab::Type::Region;
      for (const Pt& s : q) p.region.push_back(surPastille(pa, s));
      p.region = orienter(p.region);
      p.fonction = fonction;
      p.reseau = x.reseau;
      p.reference = x.reference;
      p.numero = x.numero;
      p.broche = nomBroche;
      return p;
    };
    // Propriété de fabrication de la pastille (3.20, EMP-18) : BGA, fiducial, point de test, dissipateur…
    const char* fonction = fonctionGerberPastille(pa.fabrication, pa.type == TypePastille::CMS);
    const ReglagesFabrication& RF = c.circuit.fabrication;
    // Grande pastille CMS (pastille centrale) : pâte en fenêtres, pour un taux de couverture réglé (EL-31)
    const double aire = mm(pa.largeur) * mm(pa.hauteur);
    const bool fenetres = pa.type == TypePastille::CMS && aire >= RF.seuilGrandePastille && RF.tauxPateGrandes < 0.999;
    const auto itV = margeVernisRegle.find(x.composant);
    const auto itP = margePateRegle.find(x.composant);
    const Nm margeVernis = pa.margeVernis.value_or(itV != margeVernisRegle.end() ? itV->second : RF.margeVernis);
    const Nm margePate = pa.margePate.value_or(itP != margePateRegle.end() ? itP->second : RF.margePate);
    for (auto& cf : m.couches) {
      if (!(x.couches & bit(cf.couche))) continue;
      if (estCuivre(cf.couche)) cf.primitives.push_back(primitive(0, fonction));
      else if (cf.couche == Couche::VernisDessus || cf.couche == Couche::VernisDessous) cf.primitives.push_back(pa.vernisLibre.size() >= 3 ? libre(pa.vernisLibre, fonction) : primitive(margeVernis, fonction));
      else if (cf.couche == Couche::PateDessus || cf.couche == Couche::PateDessous) {
        if (sansPate.count(x.composant)) continue;
        if (pa.pateLibre.size() >= 3) { cf.primitives.push_back(libre(pa.pateLibre, fonction)); continue; }
        if (pa.motifPate) {
          // Motif de pâte (4.24, EMP-26) : grille de fenêtres de la pastille, au taux de couverture voulu
          const MotifPate& mp = *pa.motifPate;
          const double L = static_cast<double>(pa.largeur), H = static_cast<double>(pa.hauteur), e = static_cast<double>(mp.ecart);
          const double w = std::max(1.0, (L - (mp.colonnes - 1) * e) / mp.colonnes), h = std::max(1.0, (H - (mp.lignes - 1) * e) / mp.lignes);
          const double f = std::min(1.0, std::sqrt(mp.couverture * L * H / (mp.colonnes * mp.lignes * w * h)));
          for (int i = 0; i < mp.colonnes; ++i)
            for (int j = 0; j < mp.lignes; ++j) {
              const Pt centre = surPastille(pa, {static_cast<Nm>(std::llround(-L / 2 + w / 2 + i * (w + e))), static_cast<Nm>(std::llround(-H / 2 + h / 2 + j * (h + e)))});
              const Nm lw = static_cast<Nm>(std::llround(w * f)), lh = static_cast<Nm>(std::llround(h * f));
              const Polygone fen = mp.rayon > 0 ? rectangleArrondi(centre, lw, lh, std::min(mp.rayon, std::min(lw, lh) / 2), pa.rotation) : rectangle(centre, lw, lh, pa.rotation);
              for (auto& q : intersecter({fen}, {x.forme})) {
                PrimitiveFab p;
                p.type = PrimitiveFab::Type::Region;
                p.region = std::move(q);
                p.fonction = fonction;
                p.reseau = x.reseau;
                p.reference = x.reference;
                p.numero = x.numero;
                p.broche = nomBroche;
                cf.primitives.push_back(std::move(p));
              }
            }
          continue;
        }
        if (!fenetres) { cf.primitives.push_back(primitive(margePate, fonction)); continue; }
        for (auto& f : fenetresPate(pa, x.forme, RF.tauxPateGrandes)) {
          f.fonction = fonction;
          f.reseau = x.reseau;
          f.reference = x.reference;
          f.numero = x.numero;
          f.broche = nomBroche;
          cf.primitives.push_back(std::move(f));
        }
      }
    }
    if (pa.percage > 0) {
      const bool metallise = pa.type == TypePastille::Traversante && !simpleFace;
      // Trou décalé, oblong (fente), avec sa tolérance (4.27) ; trous en plus de la même pastille
      TrouFab t{surPastille(pa, pa.decalagePercage), pa.percage, metallise, pa.type == TypePastille::Traversante ? "ComponentDrill" : "MechanicalDrill"};
      t.longueur = pa.longueurPercage > pa.percage ? pa.longueurPercage : 0;
      t.rotation = pa.rotation;
      t.tolerance = pa.tolerancePercage;
      t.reference = x.reference + "." + x.numero;
      if (pa.fabrication == "castellee") t.fonction = "CastellatedDrill";
      m.trous.push_back(t);
      for (const auto& tp : pa.trousEnPlus) {
        TrouFab u{surPastille(pa, tp.decalage), tp.diametre, metallise, t.fonction};
        u.longueur = tp.longueur > tp.diametre ? tp.longueur : 0;
        u.rotation = pa.rotation + tp.rotation;
        u.reference = t.reference;
        m.trous.push_back(u);
      }
    }
  }
  // Vias (couvertes de vernis) et pistes
  for (const auto& v : c.circuit.vias) {
    // Couches que la via relie (8.22), couronnes non fonctionnelles retirées (8.23) : comme pour la connectivité
    Couches couchesV = couchesVia(v, nc);
    if (v.retirerCouronnes) {
      Couches utiles = bit(Couche::CuivreDessus) | bit(Couche::CuivreDessous);
      for (const auto& p : c.circuit.pistes)
        if (p.reseau == v.reseau && (distance(p.a, v.p) <= static_cast<double>(v.diametre) / 2 || distance(p.b, v.p) <= static_cast<double>(v.diametre) / 2)) utiles |= bit(p.couche);
      for (const auto& z : c.circuit.zones)
        if (z.reseau == v.reseau) utiles |= bit(z.couche);
      couchesV &= utiles;
    }
    for (Couche x : couchesCuivre(nc))
      if (CoucheFab* cf = couche(x); cf && (couchesV & bit(x))) {
        PrimitiveFab p;
        p.type = PrimitiveFab::Type::Disque;
        p.a = v.p;
        p.l = diametreViaSur(v, x, nc);
        p.fonction = "ViaPad";
        p.reseau = v.reseau;
        cf->primitives.push_back(p);
      }
    // Vias découvertes au choix (points de mesure, dissipation) : ouverture du vernis des deux faces
    if (!c.circuit.fabrication.viasCouvertes)
      for (Couche x : {Couche::VernisDessus, Couche::VernisDessous})
        if (CoucheFab* cf = couche(x)) {
          PrimitiveFab p;
          p.type = PrimitiveFab::Type::Disque;
          p.a = v.p;
          p.l = v.diametre + 2 * c.circuit.fabrication.margeVernis;
          p.fonction = "ViaPad";
          cf->primitives.push_back(p);
        }
    {
      // Simple face : un trou de strap, non métallisé (F9) ; via borgne ou enterrée : sa paire de couches
      TrouFab t{v.p, v.percage, !simpleFace, "ViaDrill"};
      const std::vector<Couche> cu = couchesCuivre(nc);
      if (v.type != TypeVia::Traversante && cu.size() > 2) {
        int i0 = std::max(0, rangCuivre(v.de, nc)), i1 = rangCuivre(v.vers, nc);
        if (i1 < 0) i1 = static_cast<int>(cu.size()) - 1;
        if (i0 > i1) std::swap(i0, i1);
        t.de = cu[static_cast<std::size_t>(i0)];
        t.a = cu[static_cast<std::size_t>(i1)];
      }
      m.trous.push_back(t);
      // Contre-perçage (8.23) : depuis la face choisie jusqu'à la couche qui suit la dernière couche gardée
      if (v.contrePercage && cu.size() > 2) {
        const ContrePercage& cp = *v.contrePercage;
        const int garde = rangCuivre(cp.jusqua, nc);
        if (garde >= 0) {
          TrouFab b{v.p, cp.diametre > 0 ? cp.diametre : v.percage + 200'000, false, "Backdrill"};
          b.contrePercage = true;
          if (cp.dessous && garde + 1 <= static_cast<int>(cu.size()) - 1) {
            b.de = cu.back();
            b.a = cu[static_cast<std::size_t>(garde + 1)];
            m.trous.push_back(b);
          } else if (!cp.dessous && garde - 1 >= 0) {
            b.de = cu.front();
            b.a = cu[static_cast<std::size_t>(garde - 1)];
            m.trous.push_back(b);
          }
        }
      }
    }
  }
  for (const auto& t : c.circuit.pistes)
    if (CoucheFab* cf = couche(t.couche)) {
      PrimitiveFab p;
      p.fonction = "Conductor";
      p.reseau = t.reseau;
      if (!t.milieu && (t.largeurFin > 0 || t.boutsCarres)) {
        // Segment effilé ou à bouts carrés (5.22) : une région, au contour exact
        for (auto& f : formePiste(t)) {
          PrimitiveFab q = p;
          q.type = PrimitiveFab::Type::Region;
          q.region = std::move(f);
          cf->primitives.push_back(std::move(q));
        }
        continue;
      }
      p.type = t.milieu ? PrimitiveFab::Type::Arc : PrimitiveFab::Type::Trait;
      p.a = t.a;
      p.b = t.b;
      if (t.milieu) p.m = *t.milieu;
      p.l = t.largeur;
      cf->primitives.push_back(p);
    }
  for (const auto& g : c.circuit.regions)
    if (CoucheFab* cf = couche(g.couche); cf && g.forme.size() >= 3) {
      PrimitiveFab p;
      p.type = PrimitiveFab::Type::Region;
      p.region = orienter(g.forme);
      p.fonction = "Conductor";
      p.reseau = g.reseau;
      cf->primitives.push_back(p);
    }
  // Sérigraphie des empreintes
  for (const auto& e : c.circuit.empreintes) {
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    if (!d) continue;
    for (const auto& tr : d->traits) {
      if (tr.couche != Couche::SerigraphieDessus && tr.couche != Couche::SerigraphieDessous && tr.couche != Couche::ColleDessus && tr.couche != Couche::ColleDessous) continue;
      CoucheFab* cf = couche(coucheSurCarte(e, tr.couche));
      if (!cf) continue;
      auto trait = [&](Pt a, Pt b) {
        PrimitiveFab p;
        p.type = PrimitiveFab::Type::Trait;
        p.a = versCarte(e, a);
        p.b = versCarte(e, b);
        p.l = tr.epaisseur;
        p.fonction = "Other,Legend";
        cf->primitives.push_back(p);
      };
      if (tr.rayon > 0) {
        const int n = 36;
        const double r = static_cast<double>(tr.rayon);
        auto surCercle = [&](int i) {
          const double a = 2 * std::numbers::pi * i / n;
          return tr.a + Pt{static_cast<Nm>(std::llround(r * std::cos(a))), static_cast<Nm>(std::llround(r * std::sin(a)))};
        };
        for (int i = 0; i < n; ++i) trait(surCercle(i), surCercle(i + 1));
      } else trait(tr.a, tr.b);
    }
  }
  // Points de colle tirés du corps (8.14) : un disque au centre de l'encombrement de chaque composant CMS monté de la
  // face, sauf s'il porte déjà ses points dans son empreinte
  if (colle.dessus || colle.dessous)
    for (const auto& e : c.circuit.empreintes) {
      const DefEmpreinte* d = c.empreinte(e.empreinte);
      const ComposantElec* x = c.composant(e.composant);
      if (!d || !x || x->nePasMonter || (e.dessous ? !colle.dessous : !colle.dessus)) continue;
      if (d->pastilles.empty() || std::any_of(d->pastilles.begin(), d->pastilles.end(), [](const Pastille& q) { return q.type != TypePastille::CMS; })) continue;
      if (std::any_of(d->traits.begin(), d->traits.end(), [](const TraitEmpreinte& t) { return t.couche == Couche::ColleDessus || t.couche == Couche::ColleDessous; })) continue;
      if (CoucheFab* cf = couche(e.dessous ? Couche::ColleDessous : Couche::ColleDessus)) {
        PrimitiveFab p;
        p.type = PrimitiveFab::Type::Disque;
        p.a = boite(encombrementSurCarte(e, *d)).centre();
        p.l = colle.diametre;
        p.reference = x->reference;
        cf->primitives.push_back(p);
      }
    }
  // Dessins de la carte (5.31) : traits, ou régions s'ils sont pleins ; sur le cuivre, sans fonction électrique
  for (const auto& d : c.circuit.dessins) {
    CoucheFab* cf = couche(d.couche);
    if (!cf || d.couche == Couche::Contour) continue;
    const std::vector<Pt> pts = contourDessin(d);
    if (pts.size() < 2) continue;
    const bool ferme = d.forme == DessinCircuit::Forme::Polygone || d.forme == DessinCircuit::Forme::Rectangle || d.forme == DessinCircuit::Forme::Cercle;
    const char* fonction = estCuivre(d.couche) ? "NonConductor" : d.couche == Couche::SerigraphieDessus || d.couche == Couche::SerigraphieDessous ? "Other,Legend" : "";
    const bool region = (d.plein || d.couche == Couche::ProtectionDessus || d.couche == Couche::ProtectionDessous) && ferme && pts.size() >= 3;
    if (region) {
      PrimitiveFab p;
      p.type = PrimitiveFab::Type::Region;
      p.region = orienter(Polygone(pts.begin(), pts.end()));
      p.fonction = fonction;
      cf->primitives.push_back(p);
      continue;
    }
    for (std::size_t i = 0; i + 1 < pts.size() + (ferme ? 1 : 0); ++i) {
      PrimitiveFab p;
      p.type = PrimitiveFab::Type::Trait;
      p.a = pts[i];
      p.b = pts[(i + 1) % pts.size()];
      p.l = d.epaisseur;
      p.fonction = fonction;
      cf->primitives.push_back(p);
    }
  }
  // Repères, valeurs et textes de carte, dans la police vectorielle commune (EL-06). Un texte posé sur une couche
  // de cuivre est du cuivre sans fonction électrique (C20) : la DRC, les zones et la connectivité le comptent.
  for (const auto& t : textesSerigraphie(c))
    if (CoucheFab* cf = couche(t.couche)) {
      PrimitiveFab p;
      p.type = PrimitiveFab::Type::Trait;
      p.a = t.a;
      p.b = t.b;
      p.l = t.epaisseur;
      p.fonction = estCuivre(t.couche) ? "NonConductor" : "Other,Legend";
      cf->primitives.push_back(p);
    }
  // Contour de carte
  // Contour de carte, puis découpes internes (EL-44) : même couche de fraisage, attribut « Profile » pour
  // le contour, « NonPlated » n'existe pas pour une découpe : KiCad l'écrit aussi en « Profile »
  if (CoucheFab* cf = couche(Couche::Contour))
    for (const auto& [a, b] : c.circuit.contour.aretes()) {
      PrimitiveFab p;
      p.type = PrimitiveFab::Type::Trait;
      p.a = a;
      p.b = b;
      p.l = 100'000;
      p.fonction = "Profile";
      cf->primitives.push_back(p);
    }
  return m;
}

std::string gerber(const ModeleFabrication& m, Couche couche, const std::string& version) {
  const CoucheFab* cf = nullptr;
  int nCuivre = 0;
  for (const auto& x : m.couches) {
    if (x.couche == couche) cf = &x;
    if (estCuivre(x.couche)) ++nCuivre;
  }
  std::ostringstream s;
  const std::string fonction = fonctionFichier(couche, nCuivre);
  const bool negatif = fichierNegatif(couche);
  s << "G04 Promethee : " << sur(m.nom) << " - " << codeCouche(couche) << "*\n";
  s << "%TF.GenerationSoftware,Promethee,Promethee," << sur(version) << "*%\n";
  // Identité du projet et date (8.4, F13) : la date est celle du cartouche, pour que deux écritures d'une même
  // révision donnent les mêmes fichiers
  if (!m.uuid.empty()) s << "%TF.ProjectId," << sur(m.nom) << "," << sur(m.uuid) << "," << sur(m.revision.empty() ? std::string("rev?") : m.revision) << "*%\n";
  if (m.date.size() == 10 && m.date[4] == '-' && m.date[7] == '-') s << "%TF.CreationDate," << m.date << "T00:00:00+00:00*%\n";
  s << "%TF.Part,Single*%\n";
  s << "%TF.SameCoordinates,Original*%\n";
  s << "%TF.FileFunction," << fonction << "*%\n";
  s << "%TF.FilePolarity," << (negatif ? "Negative" : "Positive") << "*%\n";
  s << "%FSLAX46Y46*%\n%MOMM*%\n%LPD*%\nG01*\n";
  auto xy = [&m](Pt p) {
    const Pt q = versRepereFabrication(m.repere, p);
    return "X" + std::to_string(q.x) + "Y" + std::to_string(q.y);
  };
  // Ouvertures
  struct Cle {
    int type;
    Nm l, h, r;
    long long rot;  // millièmes de degré
    std::string fonction;
    bool operator<(const Cle& o) const { return std::tie(type, l, h, r, rot, fonction) < std::tie(o.type, o.l, o.h, o.r, o.rot, o.fonction); }
  };
  std::map<Cle, int> ouvertures;
  auto cleDe = [](const PrimitiveFab& p) {
    const bool rond = p.type == PrimitiveFab::Type::Trait || p.type == PrimitiveFab::Type::Arc;
    const int t = rond ? 0 : static_cast<int>(p.type);
    const bool arrondi = p.type == PrimitiveFab::Type::RectArrondi;
    return Cle{t, p.l, p.type == PrimitiveFab::Type::Disque || rond ? 0 : p.h, arrondi ? p.r : 0, arrondi ? std::llround(std::fmod(std::fmod(p.rotation, 360.0) + 360.0, 360.0) * 1000) : 0, p.fonction};
  };
  bool macroArrondi = false;
  if (cf)
    for (const auto& p : cf->primitives)
      if (p.type != PrimitiveFab::Type::Region) {
        ouvertures.emplace(cleDe(p), 0);
        macroArrondi |= p.type == PrimitiveFab::Type::RectArrondi;
      }
  // Macro du rectangle arrondi (8.4, F12), celle de KiCad : un polygone des quatre centres des arrondis, quatre cercles
  // et quatre traits de la largeur de l'arrondi
  if (macroArrondi)
    s << "%AMRoundRect*\n0 Rectangle aux coins arrondis*\n0 $1 rayon ; $2 a $9 centres des quatre arrondis*\n"
         "4,1,4,$2,$3,$4,$5,$6,$7,$8,$9,$2,$3,0*\n1,1,$1+$1,$2,$3*\n1,1,$1+$1,$4,$5*\n1,1,$1+$1,$6,$7*\n1,1,$1+$1,$8,$9*\n"
         "20,1,$1+$1,$2,$3,$4,$5,0*\n20,1,$1+$1,$4,$5,$6,$7,0*\n20,1,$1+$1,$6,$7,$8,$9,0*\n20,1,$1+$1,$8,$9,$2,$3,0*%\n";
  int d = 10;
  for (auto& [cle, code] : ouvertures) {
    code = d++;
    s << "%TA.AperFunction," << cle.fonction << "*%\n";
    if (cle.type == 0 || cle.type == static_cast<int>(PrimitiveFab::Type::Disque)) s << "%ADD" << code << "C," << decimal(std::max<Nm>(cle.l, 1000)) << "*%\n";
    else if (cle.type == static_cast<int>(PrimitiveFab::Type::Rect)) s << "%ADD" << code << "R," << decimal(cle.l) << "X" << decimal(cle.h) << "*%\n";
    else if (cle.type == static_cast<int>(PrimitiveFab::Type::RectArrondi)) {
      // Centres des arrondis, tournés, dans le repère de fabrication (y vers le haut : l'angle s'inverse)
      const Nm rr = std::clamp<Nm>(cle.r, 0, std::min(cle.l, cle.h) / 2);
      const double a = -static_cast<double>(cle.rot) / 1000.0 * std::numbers::pi / 180, ca = std::cos(a), sa = std::sin(a);
      const double dx = static_cast<double>(cle.l) / 2 - static_cast<double>(rr), dy = static_cast<double>(cle.h) / 2 - static_cast<double>(rr);
      s << "%ADD" << code << "RoundRect," << decimal(rr);
      for (const auto& [ux, uy] : {std::pair{-dx, dy}, std::pair{dx, dy}, std::pair{dx, -dy}, std::pair{-dx, -dy}})
        s << "X" << decimal(static_cast<Nm>(std::llround(ux * ca - uy * sa))) << "X" << decimal(static_cast<Nm>(std::llround(ux * sa + uy * ca)));
      s << "*%\n";
    } else s << "%ADD" << code << "O," << decimal(cle.l) << "X" << decimal(cle.h) << "*%\n";
    s << "%TD*%\n";
  }
  if (cf) {
    // Attributs d'objet X2 (EL-30) : réseau (%TO.N), pastille (%TO.P : repère, numéro, réseau), composant
    // (%TO.C) ; chacun est retiré par son propre %TD, pour que les attributs d'ouverture restent en place
    std::string n, pa, co;
    auto poser = [&](const char* nom, std::string& courant, const std::string& voulu) {
      if (courant == voulu) return;
      if (!courant.empty()) s << "%TD." << nom << "*%\n";
      courant = voulu;
      if (!courant.empty()) s << "%TO." << nom << "," << courant << "*%\n";
    };
    auto attributs = [&](const PrimitiveFab& p) {
      const bool cuivre = estCuivre(couche);
      poser("N", n, cuivre && !p.reseau.empty() ? sur(p.reseau) : std::string());
      // %TO.P : repère, numéro, puis la fonction de la broche quand elle est connue (F10) ; le réseau est dans %TO.N
      poser("P", pa, cuivre && !p.reference.empty() && !p.numero.empty() ? sur(p.reference) + "," + sur(p.numero) + (p.broche.empty() ? std::string() : "," + sur(p.broche)) : std::string());
      poser("C", co, !p.reference.empty() ? sur(p.reference) : std::string());
    };
    for (const auto& p : cf->primitives) {
      if (p.type != PrimitiveFab::Type::Region || p.region.size() < 3) continue;
      attributs(p);
      // La région prend les attributs d'ouverture en vigueur : sa fonction est posée juste avant
      s << "%TA.AperFunction," << p.fonction << "*%\n";
      s << "G36*\n" << xy(p.region.front()) << "D02*\n";
      for (std::size_t i = 1; i < p.region.size(); ++i) s << xy(p.region[i]) << "D01*\n";
      s << xy(p.region.front()) << "D01*\nG37*\n";
      s << "%TD.AperFunction*%\n";
    }
    int courante = -1;
    bool modeArc = false;
    for (const auto& p : cf->primitives) {
      if (p.type == PrimitiveFab::Type::Region) continue;
      attributs(p);
      const int code = ouvertures[cleDe(p)];
      if (code != courante) { s << "D" << code << "*\n"; courante = code; }
      if (p.type == PrimitiveFab::Type::Arc) {
        // Arc de piste (8.4, ROU-13) : G75 (arcs sur plus d'un quadrant), G02 horaire ou G03 antihoraire, centre
        // en I, J relatif au départ, dans le repère de fabrication (y vers le haut)
        const Pt A = versRepereFabrication(m.repere, p.a), M = versRepereFabrication(m.repere, p.m), B = versRepereFabrication(m.repere, p.b);
        double cx, cy, rayon;
        if (!cercleParTroisPoints(A, M, B, cx, cy, rayon)) {
          s << xy(p.a) << "D02*\n" << xy(p.b) << "D01*\n";
          continue;
        }
        if (!modeArc) { s << "G75*\n"; modeArc = true; }
        const double croix = static_cast<double>(M.x - A.x) * static_cast<double>(B.y - A.y) - static_cast<double>(M.y - A.y) * static_cast<double>(B.x - A.x);
        s << xy(p.a) << "D02*\n" << (croix < 0 ? "G02" : "G03") << xy(p.b) << "I" << std::llround(cx - static_cast<double>(A.x)) << "J" << std::llround(cy - static_cast<double>(A.y)) << "D01*\nG01*\n";
      } else if (p.type == PrimitiveFab::Type::Trait) s << xy(p.a) << "D02*\n" << xy(p.b) << "D01*\n";
      else s << xy(p.a) << "D03*\n";
    }
    if (!n.empty() || !pa.empty() || !co.empty()) s << "%TD*%\n";
  }
  s << "M02*\n";
  return s.str();
}

std::string excellon(const ModeleFabrication& m, bool metallises, const std::string& version) {
  // Trous qui traversent toute la carte, ronds ou oblongs (fentes détourées, 4.13) ; les vias borgnes ou enterrées
  // et les contre-perçages ont leurs propres fichiers (excellonPaire)
  PairePercage paire;
  const auto cu = couchesCuivre(std::max(1, m.couchesCuivre));
  paire.de = cu.front();
  paire.a = cu.back();
  paire.metallise = metallises;
  const int n = std::max(1, static_cast<int>(cu.size()));
  paire.fonction = std::string(metallises ? "Plated,1," : "NonPlated,1,") + std::to_string(n) + (metallises ? ",PTH" : ",NPTH");
  return excellonPaire(m, paire, OptionsExcellon{}, version);
}

std::string csvPlacement(const ConceptionElec& c, bool anglais) {
  // Champ CSV : entre guillemets s'il contient une virgule, un guillemet ou un retour à la ligne, guillemets
  // doublés ; aucune longueur maximale (EL-29)
  auto champ = [](std::string t) {
    std::replace(t.begin(), t.end(), '\n', ' ');
    std::replace(t.begin(), t.end(), '\r', ' ');
    std::string r = "\"";
    for (char ch : t) r += ch == '"' ? std::string("\"\"") : std::string(1, ch);
    return r + "\"";
  };
  auto nombre = [](double v, int decimales) {
    char t[48];
    std::snprintf(t, sizeof t, "%.*f", decimales, v);
    return std::string(t);
  };
  // Origine au coin bas gauche de la carte, Y vers le haut : la convention des assembleurs. Position : le point de
  // placement (centre des pastilles), pas l'origine de l'empreinte, qui est la broche 1 d'un traversant de KiCad.
  // Rotation : degrés, sens anti-horaire vu de dessus, de 0 à 360, sans miroir pour le dessous (attribut .CRot du
  // Gerber X3, convention de JLCPCB) (F3).
  std::ostringstream s;
  s << (anglais ? "Designator,Val,Package,Mid X,Mid Y,Rotation,Layer\n" : "Ref,Valeur,Empreinte,PosX,PosY,Rot,Face\n");
  std::vector<const EmpreintePlacee*> v;
  for (const auto& e : c.circuit.empreintes) v.push_back(&e);
  std::sort(v.begin(), v.end(), [&](const EmpreintePlacee* a, const EmpreintePlacee* b) {
    const ComposantElec *x = c.composant(a->composant), *y = c.composant(b->composant);
    return ordreNaturel(x ? x->reference : "", y ? y->reference : "");
  });
  for (const EmpreintePlacee* e : v) {
    if (!dansFichierPlacement(c, *e)) continue;
    const ComposantElec* x = c.composant(e->composant);
    double rot = std::fmod(e->rotation, 360.0);
    if (rot < 0) rot += 360;
    const DefEmpreinte* def = c.empreinte(e->empreinte);
    const Pt q = versRepereFabrication(repereFichiers(c), def ? versCarte(*e, pointDePlacement(*def)) : e->position);
    s << champ(x->reference) << ',' << champ(x->valeur) << ',' << champ(e->empreinte) << ',' << nombre(static_cast<double>(q.x) * 1e-6, 4) << ',' << nombre(static_cast<double>(q.y) * 1e-6, 4)
      << ',' << nombre(rot, 1) << ','
      << (anglais ? (e->dessous ? "bottom" : "top") : (e->dessous ? "dessous" : "dessus")) << '\n';
  }
  return s.str();
}

bool dansFichierPlacement(const ConceptionElec& c, const EmpreintePlacee& e) {
  const ComposantElec* x = c.composant(e.composant);
  return x && dansPlacement(c, *x);  // selon le genre du composant (3.17)
}

std::string csvNomenclature(const ConceptionElec& c) {
  prom::EcrivainCsv w;  // écrivain du poste (10.20)
  w.ligne({"Quantité", "Références", "Valeur", "Empreinte", "Fabricant", "Référence fabricant", "Fournisseur", "Description", "LCSC"});
  for (const auto& l : nomenclature(c)) {
    std::string refs;
    for (const auto& r : l.references) refs += (refs.empty() ? "" : ", ") + r;
    w.entier(l.quantite).champ(refs).champ(l.valeur).champ(l.empreinte).champ(l.fabricant).champ(l.mpn).champ(l.fournisseur).champ(l.description).champ(l.lcsc).fin();
  }
  return w.texte();
}

std::vector<std::string> ecrireDossierFabrication(const ConceptionElec& c, const std::string& dossier, const std::string& version) {
  namespace fs = std::filesystem;
  const Connectivite k = connectivite(c);
  const Remplissages r = remplirZones(c, k);
  const ModeleFabrication m = modeleFabrication(c, k, r);
  const std::string base = nomFichierAscii(c.nom, "carte");  // « récepteur » donne « recepteur » (EL-46)
  const fs::path racine = cheminFichier(dossier);
  std::vector<std::string> ecrits;
  auto ecrire = [&](const fs::path& relatif, const std::string& contenu) {
    const fs::path chemin = racine / relatif;
    fs::create_directories(chemin.parent_path());
    ecrireFichierAtomique(chemin, contenu);
    ecrits.push_back(relatif.generic_string());
  };
  std::vector<std::pair<std::string, Couche>> gerbers;
  for (const auto& cf : m.couches) {
    std::string code = codeCouche(cf.couche);
    std::replace(code.begin(), code.end(), '.', '_');
    ecrire(fs::path("Gerber") / (base + "-" + code + ".gbr"), gerber(m, cf.couche, version));
    gerbers.push_back({base + "-" + code + ".gbr", cf.couche});
  }
  ecrire(fs::path("Gerber") / (base + "-job.gbrjob"), gerberJob(c, m, gerbers, version));
  ecrire(fs::path("Test") / (base + "-netlist.ipc"), ipcD356(c, k));
  ecrire(fs::path("Percage") / (base + "-PTH.drl"), excellon(m, true, version));
  ecrire(fs::path("Percage") / (base + "-NPTH.drl"), excellon(m, false, version));
  // Vias borgnes, enterrées et contre-perçages (8.22, 8.23) : un fichier par paire de couches ; fentes en G85 au
  // choix du profil (4.13)
  {
    OptionsExcellon oe;
    oe.g85 = c.circuit.fabrication.fentesG85;
    for (const auto& paire : pairesPercage(m))
      if (paire.nomFichier != "PTH" && paire.nomFichier != "NPTH") ecrire(fs::path("Percage") / (base + "-" + paire.nomFichier + ".drl"), excellonPaire(m, paire, oe, version));
  }
  // Tableau et plan de perçage (4.27, 8.7), vias à remplir (8.22), couches des composants en Gerber X3 (8.7)
  ecrire(fs::path("Percage") / (base + "-tableau-percage.csv"), tableauPercagesCsv(m));
  ecrire(fs::path("Percage") / (base + "-plan-percage.svg"), planPercageSvg(m, c.nom + " — plan de perçage"));
  if (aDesViasRemplies(c)) ecrire(fs::path("Percage") / (base + "-vias-remplies.gbr"), gerberViaFill(m, c, version));
  ecrire(fs::path("Assemblage") / (base + "-composants-dessus.gbr"), gerberComposants(c, false, version));
  if (std::any_of(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [](const EmpreintePlacee& e) { return e.dessous; }))
    ecrire(fs::path("Assemblage") / (base + "-composants-dessous.gbr"), gerberComposants(c, true, version));
  ecrire(fs::path("Assemblage") / (base + "-placement.csv"), csvPlacement(c));
  ecrire(fs::path("Assemblage") / (base + "-placement-en.csv"), csvPlacement(c, true));  // en-têtes des assembleurs (Designator, Mid X…)
  ecrire(fs::path("Nomenclature") / (base + "-nomenclature.csv"), csvNomenclature(c));
  // Fiche de lecture pour le fabricant
  const auto drc = verifierDRCReglee(c, k, r);
  int erreurs = 0, alertes = 0;
  for (const auto& v : drc) (v.gravite == Gravite::Erreur ? erreurs : alertes)++;
  Nm pisteMin = 0, percageMin = 0;
  for (const auto& p : c.circuit.pistes) pisteMin = pisteMin ? std::min(pisteMin, p.largeur) : p.largeur;
  for (const auto& t : m.trous) percageMin = percageMin ? std::min(percageMin, t.diametre) : t.diametre;
  const Empilage& e = c.circuit.empilage;
  std::ostringstream s;
  char ligne[256];
  s << "Dossier de fabrication - " << c.nom << "\nProduit par Prométhée " << version << "\n\n";
  std::snprintf(ligne, sizeof ligne, "Carte : %.2f x %.2f mm, %d couches de cuivre, épaisseur %.2f mm, cuivre %.0f µm\n", static_cast<double>(m.boite.largeur()) * 1e-6,
                static_cast<double>(m.boite.hauteur()) * 1e-6, e.couchesCuivre, e.epaisseur, e.cuivre * 1000);
  s << ligne << "Matériau : " << e.materiau << " ; finition : " << e.finition << " ; vernis " << e.vernis << ", sérigraphie " << e.serigraphie << "\n";
  std::snprintf(ligne, sizeof ligne, "Piste la plus fine : %.3f mm ; isolement minimal (règle) : %.3f mm ; plus petit perçage : %.3f mm ; %zu trous\n", static_cast<double>(pisteMin) * 1e-6,
                static_cast<double>(c.circuit.regles.isolement) * 1e-6, static_cast<double>(percageMin) * 1e-6, m.trous.size());
  s << ligne << "Coordonnées : millimètres, origine commune à tous les fichiers (Gerber X2, format 4.6).\n";
  s << "Vérification des règles (DRC) : " << erreurs << " erreur(s), " << alertes << " avertissement(s).\n\nFichiers :\n";
  for (const auto& f : ecrits) s << "  " << f << "\n";
  ecrire("LISEZMOI.txt", s.str());
  return ecrits;
}

std::string ipcD356(const ConceptionElec& c, const Connectivite& k) {
  std::ostringstream s;
  auto champ = [](std::string t, std::size_t n) {
    for (char& ch : t)
      if (static_cast<unsigned char>(ch) < 32 || static_cast<unsigned char>(ch) > 126) ch = '_';
    if (t.size() > n) t = t.substr(0, n);
    return t + std::string(n - t.size(), ' ');
  };
  auto coord = [](Nm v) {
    char t[16];
    std::snprintf(t, sizeof t, "%+07lld", static_cast<long long>(std::llround(static_cast<double>(v) / 1000.0)));  // micromètres
    return std::string(t);
  };
  s << "C  Liste de reseaux IPC-D-356A produite par Promethee\n";  // ASCII : la norme ne connaît pas les accents
  s << "C  " << champ(c.nom, 60) << "\n";
  s << "P  JOB   " << champ(c.nom, 60) << "\n";
  s << "P  UNITS CUST 1\n";  // millimètres, au micromètre
  s << "P  DIM   N\n";
  // Noms de réseau longs : alias NNAME (le champ du réseau ne tient que 14 caractères)
  std::map<std::string, std::string> alias;
  int n = 0;
  auto nomReseau = [&](const std::string& r) -> std::string {
    if (r.empty()) return "N/C";
    if (r.size() <= 14) return r;
    auto it = alias.find(r);
    if (it == alias.end()) {
      const std::string a = "NNAME" + std::to_string(++n);
      it = alias.emplace(r, a).first;
      s << "P  " << champ(a, 6) << " " << champ(r, std::min<std::size_t>(r.size(), 70)) << "\n";  // ASCII, comme tout le fichier
    }
    return it->second;
  };
  for (const auto& p : pastilles(c, k)) nomReseau(p.reseau);
  for (const auto& v : c.circuit.vias) nomReseau(v.reseau);
  const int nc = c.circuit.empilage.couchesCuivre;
  // Champ numérique de quatre chiffres en micromètres : au-delà de 9,999 mm, la valeur est plafonnée et la ligne de
  // commentaire qui suit le dit (F11) ; un testeur la lit sans déborder sur le champ voisin
  std::vector<std::string> plafonnes;
  auto quatre = [&](Nm v, const std::string& objet) {
    long long u = std::llround(static_cast<double>(v) / 1000.0);
    if (u > 9999) {
      plafonnes.push_back(objet + " : " + std::to_string(u) + " um, plafonne a 9999 um");
      u = 9999;
    }
    char t[8];
    std::snprintf(t, sizeof t, "%04lld", std::max(0LL, u));
    return std::string(t);
  };
  // Une seule ligne au gabarit de la norme pour les pastilles et les vias (F1) : type (1-3), réseau (4-17), repère
  // (21-26), tiret (27), broche (28-31), milieu (32), perçage D….P (33-38), accès (39-41), X (42-49), Y (50-57),
  // taille X (58-62), taille Y (63-67), rotation (68-71), vernis S (73-74) : 74 caractères
  // Milieu « M » (32) pour les vias, qui ne sont pas des extrémités de réseau ; vernis S (73-74) : les faces où le
  // vernis couvre l'objet, qui ne s'y sonde donc pas (1 dessus, 2 dessous, 3 les deux), d'après ses vraies ouvertures
  // Trou non métallisé (« U ») sur une simple face : tous ses trous le sont (F9)
  const bool metallises = nc > 1;
  auto ligne = [&](bool traversante, const std::string& reseau, const std::string& reference, const std::string& numero, bool milieu, Nm percage, const std::string& acces, Pt position, Nm l,
                   Nm h, double rotation, int vernis) {
    const Pt q = versRepereFabrication(repereFichiers(c), position);
    std::string x = traversante ? "317" : "327";
    x += champ(nomReseau(reseau), 14) + "   ";
    x += champ(reference, 6) + "-" + champ(numero, 4) + (milieu ? "M" : " ");
    x += traversante ? "D" + quatre(percage, reference + "-" + numero + " (percage)") + (metallises ? "P" : "U") : std::string(6, ' ');
    x += acces;
    x += "X" + coord(q.x) + "Y" + coord(q.y);
    char r[8];
    std::snprintf(r, sizeof r, "R%03d", static_cast<int>(std::lround(std::fmod(std::fmod(rotation, 360.0) + 360.0, 360.0))) % 360);
    x += "X" + quatre(l, reference + "-" + numero + " (taille X)") + "Y" + quatre(h, reference + "-" + numero + " (taille Y)") + r;
    x += " S" + std::to_string(vernis);
    return x;
  };
  for (const auto& p : pastilles(c, k)) {
    // Ni trou non métallisé, ni pastille CMS sans cuivre (face sans cuivre d'une simple face : R2) : rien à sonder
    if (p.pastille.type == TypePastille::NonMetallisee || p.inaccessible) continue;
    const bool traversante = p.pastille.type == TypePastille::Traversante;
    // Accès : 00 si la pastille a du cuivre sur les deux faces, sinon le numéro de sa couche de cuivre (1 dessus,
    // n dessous ; 1 pour le cuivre unique d'une simple face), d'après ses couches réelles : une CMS sur B.Cu d'une
    // empreinte du dessus (X2) s'atteint par-dessous
    const std::vector<Couche> cuivres = couchesCuivre(nc);
    int numeroCouche = 0;
    if (cuivres.size() > 1 && (p.couches & bit(cuivres.front())) && (p.couches & bit(cuivres.back()))) numeroCouche = 0;
    else
      for (std::size_t i = 0; i < cuivres.size(); ++i)
        if (p.couches & bit(cuivres[i])) {
          numeroCouche = static_cast<int>(i) + 1;
          break;
        }
    const std::string acces = std::string("A") + static_cast<char>('0' + numeroCouche / 10) + static_cast<char>('0' + numeroCouche % 10);
    int vernis = 3;
    if (p.couches & bit(Couche::VernisDessus)) vernis &= ~1;
    if (p.couches & bit(Couche::VernisDessous)) vernis &= ~2;
    s << ligne(traversante, p.reseau, p.reference, p.numero, false, p.pastille.percage, acces, p.pastille.position, p.pastille.largeur, p.pastille.hauteur, p.pastille.rotation, vernis)
      << "\n";
  }
  // Vias : traversantes, accès des deux faces, milieux de réseau ; vernis selon le réglage réel (S3 couvertes, S0
  // découvertes)
  for (const auto& v : c.circuit.vias)
    s << ligne(true, v.reseau, "VIA", "", true, v.percage, nc > 1 ? "A00" : "A01", v.p, v.diametre, v.diametre, 0, c.circuit.fabrication.viasCouvertes ? 3 : 0) << "\n";
  for (const auto& x : plafonnes) s << "C  " << champ(x, 70) << "\n";
  s << "999\n";
  return s.str();
}

std::string gerberJob(const ConceptionElec& c, const ModeleFabrication& m, const std::vector<std::pair<std::string, Couche>>& fichiers, const std::string& version) {
  // Au vocabulaire de la norme du gbrjob (Ucamco, Gerber Job Format) : fonctions identiques aux %TF.FileFunction des
  // fichiers, finition et couleurs parmi les valeurs prévues, empilage complet de haut en bas. Pas de date de
  // création : le dossier reste identique d'une écriture à l'autre, et la norme ne l'exige pas.
  nlohmann::ordered_json j;
  j["Header"] = {{"GenerationSoftware", {{"Vendor", "Prométhée"}, {"Application", "Prométhée"}, {"Version", version}}}};
  const Empilage& e = c.circuit.empilage;
  int nCuivre = 0;
  for (const auto& cf : m.couches) nCuivre += estCuivre(cf.couche);
  const std::vector<CoucheEmpilage> pileReelle = pileEmpilage(e);
  j["GeneralSpecs"] = {{"ProjectId", {{"Name", c.nom}, {"GUID", c.uuid}, {"Revision", c.cartouche.revision.empty() ? std::string("rev?") : c.cartouche.revision}}},
                       {"Size", {{"X", mm(m.boite.largeur())}, {"Y", mm(m.boite.hauteur())}}},
                       {"LayerNumber", nCuivre},
                       {"BoardThickness", e.pile.empty() ? e.epaisseur : epaisseurPile(pileReelle)},
                       {"Finish", finitionNormee(e.finition)}};
  // Usinages de la carte nue (8.12) : demi-trous, bord métallisé, connecteur de tranche biseauté
  {
    const UsinagesCarte u = usinagesCarte(c);
    if (u.castellee) j["GeneralSpecs"]["Castellated"] = true;
    if (u.bordMetallise) j["GeneralSpecs"]["EdgePlating"] = true;
    if (u.connecteurBord) j["GeneralSpecs"]["EdgeConnector"] = true;
    if (u.biseau) j["GeneralSpecs"]["EdgeConnectorBevelled"] = true;
  }
  const Regles& R = c.circuit.regles;
  auto regles = [&](const char* couches) {
    return nlohmann::ordered_json{{"Layers", couches},
                                  {"PadToPad", mm(R.isolement)},
                                  {"PadToTrack", mm(R.isolement)},
                                  {"PadToRegion", mm(R.isolement)},
                                  {"TrackToTrack", mm(R.isolement)},
                                  {"TrackToRegion", mm(R.isolement)},
                                  {"RegionToRegion", mm(R.isolement)},
                                  {"MinLineWidth", mm(R.largeurMin)},
                                  {"MinClearanceToProfile", mm(R.bord)}};
  };
  j["DesignRules"] = nlohmann::ordered_json::array({regles("Outer")});
  if (nCuivre > 2) j["DesignRules"].push_back(regles("Inner"));  // pas d'héritage : chaque classe porte toutes ses valeurs
  j["FilesAttributes"] = nlohmann::ordered_json::array();
  bool present[NB_COUCHES] = {};
  for (const auto& [nom, x] : fichiers) {
    present[static_cast<int>(x)] = true;
    j["FilesAttributes"].push_back({{"Path", nom}, {"FileFunction", fonctionFichier(x, nCuivre)}, {"FilePolarity", fichierNegatif(x) ? "Negative" : "Positive"}});
  }
  // Empilage de haut en bas, complet comme la norme le demande : légende et vernis des faces qui en ont, cuivres et
  // diélectriques alternés (le cuivre unique d'une simple face est dessous)
  auto& pile = j["MaterialStackup"] = nlohmann::ordered_json::array();
  auto enduit = [&](const char* type, const std::string& couleur, const char* nom) {
    nlohmann::ordered_json x = {{"Type", type}};
    if (const std::string norme = couleurNormee(couleur); !norme.empty()) x["Color"] = norme;
    x["Name"] = nom;
    pile.push_back(std::move(x));
  };
  if (present[static_cast<int>(Couche::SerigraphieDessus)]) enduit("Legend", e.serigraphie, "Top Silk Screen");
  if (present[static_cast<int>(Couche::VernisDessus)]) enduit("SolderMask", e.vernis, "Top Solder Mask");
  // Empilage réel, du dessus vers le dessous (8.20) : cuivres, diélectriques avec leur permittivité, âme métallique
  for (const auto& x : pileReelle) {
    if (x.genre == GenreCoucheEmpilage::Cuivre) pile.push_back({{"Type", "Copper"}, {"Thickness", x.epaisseur}, {"Name", x.nom}});
    else {
      nlohmann::ordered_json d = {{"Type", "Dielectric"}, {"Thickness", x.epaisseur}, {"Material", x.genre == GenreCoucheEmpilage::AmeMetallique ? x.materiau : x.materiau}};
      if (x.genre == GenreCoucheEmpilage::Dielectrique) {
        d["DielectricConstant"] = x.permittivite;
        d["LossTangent"] = x.pertes;
      }
      d["Name"] = x.genre == GenreCoucheEmpilage::AmeMetallique ? std::string("Metal core") : x.preimpregne ? std::string("Prepreg") : std::string("Core");
      pile.push_back(std::move(d));
    }
  }
  if (present[static_cast<int>(Couche::VernisDessous)]) enduit("SolderMask", e.vernis, "Bottom Solder Mask");
  if (present[static_cast<int>(Couche::SerigraphieDessous)]) enduit("Legend", e.serigraphie, "Bottom Silk Screen");
  return j.dump(2) + "\n";
}

std::string netlistKicad(const ConceptionElec& c, const Connectivite& k) {
  auto ch = [](const std::string& t) {
    std::string r = "\"";
    for (char x : t) {
      if (x == '"' || x == '\\') r += '\\';
      r += x;
    }
    return r + "\"";
  };
  std::ostringstream s;
  s << "(export (version \"E\")\n  (design\n    (source " << ch(c.nom) << ")\n    (tool \"Prométhée\"))\n  (components";
  for (const auto& x : c.composants) {
    const DefSymbole* d = c.symbole(x.symbole);
    s << "\n    (comp (ref " << ch(x.reference) << ")\n      (value " << ch(x.valeur) << ")\n      (footprint " << ch(x.empreinte.rfind("kicad:", 0) == 0 ? x.empreinte.substr(6) : "Promethee:" + x.empreinte) << ")";
    if (!x.fiche.empty()) s << "\n      (datasheet " << ch(x.fiche) << ")";
    s << "\n      (libsource (lib \"promethee\") (part " << ch(d ? d->nom : x.symbole) << ") (description " << ch(x.description) << "))";
    for (const auto& [nom, v] : x.champs) s << "\n      (property (name " << ch(nom) << ") (value " << ch(v) << "))";
    if (x.nePasMonter) s << "\n      (property (name \"dnp\"))";
    s << "\n      (tstamps " << ch(x.uuid) << "))";
  }
  s << ")\n  (nets";
  int code = 0;
  for (const auto& r : k.reseaux) {
    if (r.broches.empty()) continue;
    s << "\n    (net (code " << ch(std::to_string(++code)) << ") (name " << ch(r.nom) << ")";
    for (const auto& b : r.broches) {
      const ComposantElec* x = c.composant(b.composant);
      if (!x) continue;
      std::string fonctionBroche = "~", typeBroche = "passive";
      if (const DefSymbole* d = c.symbole(x->symbole))
        for (const auto& br : d->broches)
          if (br.numero == b.broche) {
            fonctionBroche = br.nom.empty() ? "~" : br.nom;
            switch (br.type) {
              case TypeBroche::Entree: typeBroche = "input"; break;
              case TypeBroche::Sortie: typeBroche = "output"; break;
              case TypeBroche::Bidirectionnelle: typeBroche = "bidirectional"; break;
              case TypeBroche::TroisEtats: typeBroche = "tri_state"; break;
              case TypeBroche::AlimEntree: typeBroche = "power_in"; break;
              case TypeBroche::AlimSortie: typeBroche = "power_out"; break;
              case TypeBroche::CollecteurOuvert: typeBroche = "open_collector"; break;
              case TypeBroche::NonConnectee: typeBroche = "no_connect"; break;
              case TypeBroche::NonSpecifiee: typeBroche = "unspecified"; break;
              case TypeBroche::Passive: break;
            }
          }
      s << "\n      (node (ref " << ch(x->reference) << ") (pin " << ch(b.broche) << ") (pinfunction " << ch(fonctionBroche) << ") (pintype " << ch(typeBroche) << "))";
    }
    s << ")";
  }
  s << "))\n";
  return s.str();
}

namespace {
std::string echapperXml(const std::string& t) {
  std::string r;
  for (char ch : t) {
    switch (ch) {
      case '&': r += "&amp;"; break;
      case '<': r += "&lt;"; break;
      case '>': r += "&gt;"; break;
      case '"': r += "&quot;"; break;
      default: r += ch;
    }
  }
  return r;
}
}  // namespace

ModeleTypon modeleTypon(const ConceptionElec& c, const Connectivite& k, const Remplissages& r, const OptionsTypon& o) {
  ModeleTypon m;
  const Couche L = o.couche;
  // Vue côté cuivre : la face dessous se lit en miroir ; le transfert de toner retourne encore une fois
  m.miroir = (L == Couche::CuivreDessous) != o.miroirTransfert;
  const Polygone carte = c.circuit.contour.polygone();
  const Boite b = boite(carte).gonflee(nm(o.marge));
  const double ex = o.echelleX > 0 ? o.echelleX : 1, ey = o.echelleY > 0 ? o.echelleY : 1;
  m.echelleX = ex;
  m.echelleY = ey;
  auto place = [&](Pt p) {
    Pt q = p - Pt{b.x0, b.y0};
    if (m.miroir) q.x = b.largeur() - q.x;
    // Compensation de l'imprimante (8.29)
    q.x = static_cast<Nm>(std::llround(static_cast<double>(q.x) * ex));
    q.y = static_cast<Nm>(std::llround(static_cast<double>(q.y) * ey));
    return q;
  };
  auto placerPoly = [&](const Polygone& poly) {
    Polygone x;
    for (const Pt& p : poly) x.push_back(place(p));
    if (m.miroir) std::reverse(x.begin(), x.end());
    return x;
  };
  m.largeur = std::max(90.0, mm(b.largeur()) * ex);
  m.hauteurCarte = mm(b.hauteur()) * ey;
  m.hauteur = m.hauteurCarte + 14;  // règle de contrôle et légende sous la carte
  m.contour = placerPoly(carte);
  for (const auto& d : c.circuit.contour.decoupes) m.decoupes.push_back(placerPoly(d));
  // Une zone remplie est une région : ses îlots et leurs trous (orientés) forment un seul tracé
  for (const auto& z : c.circuit.zones)
    if (z.couche == L)
      if (const auto it = r.find(z.uuid); it != r.end()) {
        Polygones region;
        for (const auto& poly : it->second) region.push_back(placerPoly(poly));
        m.cuivre.push_back(region);
      }
  for (const auto& t : c.circuit.pistes)
    if (t.couche == L) m.traits.push_back({place(t.a), place(t.b), std::max<Nm>(1'000, t.largeur + nm(o.correctionLargeur))});
  const Nm centrage = nm(std::max(0.0, o.trouCentrage));
  auto trou = [&](Pt p, Nm percage) {
    if (percage <= 0) return;
    m.trous.push_back({place(p), centrage > 0 ? std::min(centrage, percage) : percage});
  };
  for (const auto& p : pastilles(c, k)) {
    if (!(p.couches & bit(L))) {
      if (p.pastille.type == TypePastille::NonMetallisee) trou(p.pastille.position, p.pastille.percage);
      continue;
    }
    m.cuivre.push_back({placerPoly(p.forme)});
    trou(p.pastille.position, p.pastille.percage);
  }
  for (const auto& v : c.circuit.vias) {
    m.cuivre.push_back({placerPoly(cercle(v.p, v.diametre / 2))});
    trou(v.p, v.percage);
  }
  return m;
}

std::string svgTypon(const ModeleTypon& m, const OptionsTypon& o, const std::string& titre) {
  std::ostringstream s;
  s.setf(std::ios::fixed);
  s.precision(4);
  auto X = [](Nm v) { return static_cast<double>(v) * 1e-6; };
  const char* encre = o.negatif ? "#FFFFFF" : "#000000";
  const char* fond = o.negatif ? "#000000" : "#FFFFFF";
  s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  s << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << m.largeur << "mm\" height=\"" << m.hauteur << "mm\" viewBox=\"0 0 " << m.largeur << " " << m.hauteur << "\">\n";
  s << "<title>" << echapperXml(titre) << "</title>\n";
  const double hc = m.hauteurCarte;
  // Fond de la carte (négatif : la carte est noire, le cuivre blanc)
  s << "<rect x=\"0\" y=\"0\" width=\"" << m.largeur << "\" height=\"" << hc << "\" fill=\"" << fond << "\"/>\n";
  // Chaque région en un tracé : les trous d'une zone (orientés à l'envers) restent vides
  s << "<g fill=\"" << encre << "\" fill-rule=\"nonzero\">\n";
  for (const auto& region : m.cuivre) {
    std::string d;
    for (const auto& poly : region) {
      if (poly.size() < 3) continue;
      std::ostringstream x;
      x.setf(std::ios::fixed);
      x.precision(4);
      x << "M";
      for (std::size_t i = 0; i < poly.size(); ++i) x << (i ? " L" : "") << X(poly[i].x) << " " << X(poly[i].y);
      x << " Z ";
      d += x.str();
    }
    if (!d.empty()) s << "<path d=\"" << d << "\"/>\n";
  }
  s << "</g>\n<g stroke=\"" << encre << "\" stroke-linecap=\"round\" fill=\"none\">\n";
  for (const auto& t : m.traits) s << "<line x1=\"" << X(t.a.x) << "\" y1=\"" << X(t.a.y) << "\" x2=\"" << X(t.b.x) << "\" y2=\"" << X(t.b.y) << "\" stroke-width=\"" << X(t.largeur) << "\"/>\n";
  s << "</g>\n<g fill=\"" << fond << "\">\n";
  for (const auto& [p, d] : m.trous) s << "<circle cx=\"" << X(p.x) << "\" cy=\"" << X(p.y) << "\" r=\"" << X(d) / 2 << "\"/>\n";
  s << "</g>\n<g stroke=\"#808080\" stroke-width=\"0.1\" fill=\"none\">\n";
  auto boucle = [&](const Polygone& poly) {
    if (poly.size() < 2) return;
    s << "<path d=\"M";
    for (std::size_t i = 0; i < poly.size(); ++i) s << (i ? " L" : "") << X(poly[i].x) << " " << X(poly[i].y);
    s << " Z\"/>\n";
  };
  boucle(m.contour);
  for (const auto& d : m.decoupes) boucle(d);
  s << "</g>\n";
  // Règle de 50 mm et légende : l'impression doit être à 100 %, sans ajustement à la page
  const double y = hc + 4;
  const double ex = m.echelleX;  // la règle suit la compensation : elle mesure 50 mm une fois imprimée
  s << "<g stroke=\"#000000\" stroke-width=\"0.2\"><line x1=\"2\" y1=\"" << y << "\" x2=\"" << 2 + 50 * ex << "\" y2=\"" << y << "\"/>";
  for (int k = 0; k <= 50; k += 10) s << "<line x1=\"" << 2 + k * ex << "\" y1=\"" << y - 1.5 << "\" x2=\"" << 2 + k * ex << "\" y2=\"" << y + 1.5 << "\"/>";
  s << "</g>\n<text x=\"2\" y=\"" << y + 6 << "\" font-family=\"sans-serif\" font-size=\"2\">" << echapperXml(titre) << " · " << codeCouche(o.couche)
    << (m.miroir ? " · miroir" : "") << (o.negatif ? " · négatif" : "") << " · 50 mm entre les traits : imprime à 100 %</text>\n";
  s << "</svg>\n";
  return s.str();
}

}  // namespace prom::ecad

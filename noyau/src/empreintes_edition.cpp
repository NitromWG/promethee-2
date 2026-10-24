// Prométhée : édition des empreintes de la 0.34 (lot L7, tâches 7.9, 7.21, 7.24). Voir empreintes_edition.hpp.
// Licence GPL-3.0-only.
#include "promethee/empreintes_edition.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <sstream>

#include "promethee/gabarits_ipc.hpp"
#include "promethee/synchronisation.hpp"

namespace prom::ecad {

namespace {
std::string mmTexte(Nm v) {
  char t[32];
  std::snprintf(t, sizeof t, "%.3f", mm(v));
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
EmpreintePlacee* placee(ConceptionElec& c, const std::string& composant) {
  for (auto& e : c.circuit.empreintes)
    if (e.composant == composant) return &e;
  return nullptr;
}
const EmpreintePlacee* placee(const ConceptionElec& c, const std::string& composant) {
  for (const auto& e : c.circuit.empreintes)
    if (e.composant == composant) return &e;
  return nullptr;
}
}  // namespace

std::vector<std::pair<std::string, std::vector<std::size_t>>> doublonsPastilles(const DefEmpreinte& d) {
  std::map<std::string, std::vector<std::size_t>> parNumero;
  for (std::size_t i = 0; i < d.pastilles.size(); ++i)
    if (!d.pastilles[i].numero.empty()) parNumero[d.pastilles[i].numero].push_back(i);
  std::vector<std::pair<std::string, std::vector<std::size_t>>> r;
  for (auto& [n, v] : parNumero) {
    if (v.size() < 2) continue;
    // Plusieurs pastilles d'un même numéro de formes différentes : voulu (languette, plot) ; identiques : un doublon
    bool memeForme = true;
    for (std::size_t j = 1; j < v.size(); ++j) {
      const Pastille& a = d.pastilles[v[0]];
      const Pastille& b = d.pastilles[v[j]];
      memeForme &= a.forme == b.forme && a.largeur == b.largeur && a.hauteur == b.hauteur && a.type == b.type;
    }
    if (memeForme) r.emplace_back(n, v);
  }
  return r;
}

DefEmpreinte empreinteDepuisCoordonnees(const std::string& id, const std::string& csv, const Pastille& modele, std::vector<std::string>* journal) {
  DefEmpreinte d;
  d.id = id;
  std::istringstream in(csv);
  std::string l;
  int ligne = 0;
  while (std::getline(in, l)) {
    ++ligne;
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (l.empty() || l[0] == '#') continue;
    std::vector<std::string> m;
    std::string x;
    for (char ch : l + ";") {
      if (ch == ';' || ch == '\t' || (ch == ',' && l.find(';') == std::string::npos)) {
        m.push_back(x);
        x.clear();
      } else
        x += ch;
    }
    auto nombre = [](std::string s, double& v) {
      std::replace(s.begin(), s.end(), ',', '.');
      char* fin = nullptr;
      v = std::strtod(s.c_str(), &fin);
      return fin != s.c_str();
    };
    double px = 0, py = 0;
    if (m.size() < 3 || !nombre(m[1], px) || !nombre(m[2], py)) {
      if (journal && ligne > 1) journal->push_back("ligne " + std::to_string(ligne) + " ignorée : « " + l + " »");
      continue;  // en-tête ou ligne illisible
    }
    Pastille p = modele;
    p.numero = m[0];
    p.position = ptMm(px, py);
    double v = 0;
    if (m.size() > 3 && nombre(m[3], v) && v > 0) p.largeur = nm(v);
    if (m.size() > 4 && nombre(m[4], v) && v > 0) p.hauteur = nm(v);
    if (m.size() > 5) {
      const std::string f = m[5];
      if (f == "rond" || f == "circle") p.forme = FormePastille::Rond;
      else if (f == "rect" || f == "rectangle") p.forme = FormePastille::Rect;
      else if (f == "ovale" || f == "oval") p.forme = FormePastille::Ovale;
    }
    if (m.size() > 6 && nombre(m[6], v) && v > 0) {
      p.percage = nm(v);
      p.type = TypePastille::Traversante;
    }
    d.pastilles.push_back(p);
  }
  // Encombrement : les pastilles et un quart de millimètre
  Boite b;
  for (const auto& p : d.pastilles) {
    const Boite q = boite(formePastille(p));
    b.etendre(Pt{q.x0, q.y0});
    b.etendre(Pt{q.x1, q.y1});
  }
  if (!b.vide()) d.encombrement = b.gonflee(250'000);
  return d;
}

int renumeroterSerie(DefEmpreinte& d, int premier, const std::string& prefixe) {
  std::vector<std::size_t> ordre(d.pastilles.size());
  for (std::size_t i = 0; i < ordre.size(); ++i) ordre[i] = i;
  // Ordre de lecture : par rangées (y à 0,05 mm près), puis de gauche à droite
  std::stable_sort(ordre.begin(), ordre.end(), [&](std::size_t a, std::size_t b) {
    const Pt pa = d.pastilles[a].position, pb = d.pastilles[b].position;
    if (std::llabs(pa.y - pb.y) > 50'000) return pa.y < pb.y;
    return pa.x < pb.x;
  });
  int n = premier;
  for (std::size_t i : ordre) d.pastilles[i].numero = prefixe + std::to_string(n++);
  return static_cast<int>(ordre.size());
}

int renumeroterMatrice(DefEmpreinte& d) {
  // Rangées et colonnes : les y et les x distincts (à 0,05 mm près)
  std::vector<Nm> ys, xs;
  auto ranger = [](std::vector<Nm>& v, Nm x) {
    for (Nm y : v)
      if (std::llabs(y - x) <= 50'000) return;
    v.push_back(x);
  };
  for (const auto& p : d.pastilles) {
    ranger(ys, p.position.y);
    ranger(xs, p.position.x);
  }
  std::sort(ys.begin(), ys.end());
  std::sort(xs.begin(), xs.end());
  auto rang = [](const std::vector<Nm>& v, Nm x) {
    for (std::size_t i = 0; i < v.size(); ++i)
      if (std::llabs(v[i] - x) <= 50'000) return static_cast<int>(i);
    return 0;
  };
  for (auto& p : d.pastilles) p.numero = lettreRangee(rang(ys, p.position.y)) + std::to_string(rang(xs, p.position.x) + 1);
  return static_cast<int>(d.pastilles.size());
}

int appliquerPastilleModele(DefEmpreinte& d, const Pastille& modele, const std::vector<std::string>& numeros) {
  const std::set<std::string> vises(numeros.begin(), numeros.end());
  int n = 0;
  for (auto& p : d.pastilles) {
    if (!vises.count(p.numero)) continue;
    Pastille q = modele;
    q.numero = p.numero;
    q.position = p.position;
    q.rotation = p.rotation;
    p = q;
    ++n;
  }
  return n;
}

bool reglerCote(DefEmpreinte& d, const std::string& a, const std::string& b, Nm distance) {
  auto ia = std::find_if(d.pastilles.begin(), d.pastilles.end(), [&](const Pastille& p) { return p.numero == a; });
  auto ib = std::find_if(d.pastilles.begin(), d.pastilles.end(), [&](const Pastille& p) { return p.numero == b; });
  if (ia == d.pastilles.end() || ib == d.pastilles.end() || distance <= 0) return false;
  const double dx = static_cast<double>(ib->position.x - ia->position.x), dy = static_cast<double>(ib->position.y - ia->position.y), l = std::hypot(dx, dy);
  if (l < 1) return false;
  ib->position = {ia->position.x + static_cast<Nm>(std::llround(dx / l * static_cast<double>(distance))), ia->position.y + static_cast<Nm>(std::llround(dy / l * static_cast<double>(distance)))};
  return true;
}

std::vector<std::string> composantsDeLEmpreinte(const ConceptionElec& c, const std::string& id) {
  std::vector<std::string> r;
  const auto it = c.empreintes.find(id);
  for (const auto& e : c.circuit.empreintes)
    if (e.empreinte == id || e.copieLocale == id) r.push_back(e.composant);
  (void)it;
  return r;
}

int mettreAJourEmpreinte(ConceptionElec& c, const DefEmpreinte& nouvelle, const std::vector<std::string>& composants) {
  exigerModifiable(c);
  // La définition figée du projet est remplacée ; chaque composant demandé est repris en place (pistes raccrochées)
  const std::string temporaire = nouvelle.id + "~maj";
  DefEmpreinte t = nouvelle;
  t.id = temporaire;
  c.empreintes[temporaire] = t;
  int n = 0;
  for (const auto& id : composants)
    if (remplacerEmpreinteEnPlace(c, id, temporaire)) ++n;
  c.empreintes[nouvelle.id] = nouvelle;
  for (auto& e : c.circuit.empreintes)
    if (e.empreinte == temporaire) e.empreinte = nouvelle.id;
  c.empreintes.erase(temporaire);
  return n;
}

// ---------- Cuivre propre à la carte ----------
std::string copierEmpreinteLocale(ConceptionElec& c, const std::string& composant) {
  exigerModifiable(c);
  EmpreintePlacee* e = placee(c, composant);
  const ComposantElec* x = c.composant(composant);
  if (!e || !x) return {};
  if (!e->copieLocale.empty()) return e->empreinte;  // déjà une copie
  const DefEmpreinte* d = c.empreinte(e->empreinte);
  if (!d) return {};
  DefEmpreinte copie = *d;
  const std::string origine = e->empreinte;
  copie.id = origine + "~local~" + x->reference;
  if (!c.empreintes.count(origine)) c.empreintes[origine] = *d;  // l'origine figée, pour l'écart
  c.empreintes[copie.id] = copie;
  e->empreinte = copie.id;
  e->copieLocale = origine;
  return copie.id;
}

std::vector<std::string> ecartBibliotheque(const ConceptionElec& c, const std::string& composant) {
  std::vector<std::string> r;
  const EmpreintePlacee* e = placee(c, composant);
  if (!e || e->copieLocale.empty()) return r;
  const DefEmpreinte* locale = c.empreinte(e->empreinte);
  const DefEmpreinte* biblio = c.empreinte(e->copieLocale);
  if (!locale || !biblio) return r;
  std::map<std::string, const Pastille*> avant;
  for (const auto& p : biblio->pastilles) avant[p.numero] = &p;
  for (const auto& p : locale->pastilles) {
    const auto it = avant.find(p.numero);
    if (it == avant.end()) {
      r.push_back("pastille " + p.numero + " ajoutée");
      continue;
    }
    const Pastille& a = *it->second;
    if (a.largeur != p.largeur || a.hauteur != p.hauteur)
      r.push_back("pastille " + p.numero + " : " + mmTexte(a.largeur) + " × " + mmTexte(a.hauteur) + " → " + mmTexte(p.largeur) + " × " + mmTexte(p.hauteur) + " mm");
    if (a.position != p.position) r.push_back("pastille " + p.numero + " déplacée");
    if (a.forme != p.forme) r.push_back("pastille " + p.numero + " : forme changée");
    if (a.percage != p.percage) r.push_back("pastille " + p.numero + " : perçage " + mmTexte(a.percage) + " → " + mmTexte(p.percage) + " mm");
    if (a.margeVernis != p.margeVernis || a.margePate != p.margePate) r.push_back("pastille " + p.numero + " : marges de vernis ou de pâte changées");
    avant.erase(it);
  }
  for (const auto& [n, p] : avant) r.push_back("pastille " + n + " retirée");
  if (locale->traits != biblio->traits) r.push_back("traits de sérigraphie ou de fabrication changés");
  return r;
}

bool revenirBibliotheque(ConceptionElec& c, const std::string& composant) {
  exigerModifiable(c);
  EmpreintePlacee* e = placee(c, composant);
  if (!e || e->copieLocale.empty()) return false;
  const std::string copie = e->empreinte, origine = e->copieLocale;
  e->copieLocale.clear();
  if (!remplacerEmpreinteEnPlace(c, composant, origine)) {
    e->copieLocale = origine;
    return false;
  }
  // La copie n'est plus utilisée : retirée du projet
  if (std::none_of(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [&](const EmpreintePlacee& x) { return x.empreinte == copie; })) c.empreintes.erase(copie);
  return true;
}

int eclaterEmpreinte(ConceptionElec& c, const std::string& composant, std::string* raison) {
  exigerModifiable(c);
  const ComposantElec* x = c.composant(composant);
  const EmpreintePlacee* e = placee(c, composant);
  if (!x || !e) {
    if (raison) *raison = "composant introuvable sur la carte";
    return 0;
  }
  if (!x->carteSeule && !x->symbole.empty()) {
    if (raison) *raison = x->reference + " est au schéma : l'éclater romprait le lien (retire-le du schéma d'abord)";
    return 0;
  }
  const DefEmpreinte* d = c.empreinte(e->empreinte);
  if (!d) return 0;
  const Connectivite k = connectivite(c);
  int n = 0;
  for (const auto& p : pastilles(c, k)) {
    if (p.composant != composant) continue;
    if (p.pastille.percage > 0) {
      // Pastille percée : une via de son diamètre et de son trou
      Via v;
      v.uuid = nouvelUuid();
      v.reseau = p.reseau;
      v.p = p.pastille.position;
      v.diametre = std::min(p.pastille.largeur, p.pastille.hauteur);
      v.percage = p.pastille.percage;
      c.circuit.vias.push_back(v);
    } else {
      RegionCuivre r;
      r.uuid = nouvelUuid();
      r.reseau = p.reseau;
      r.genre = "libre";
      r.couche = p.dessous ? Couche::CuivreDessous : Couche::CuivreDessus;
      r.forme = p.forme;
      c.circuit.regions.push_back(r);
    }
    ++n;
  }
  for (const auto& t : d->traits) {
    DessinCircuit dc;
    dc.uuid = nouvelUuid();
    dc.couche = e->dessous ? opposee(t.couche) : t.couche;
    dc.epaisseur = t.epaisseur;
    if (t.rayon > 0) {
      dc.forme = DessinCircuit::Forme::Cercle;
      dc.points = {versCarte(*e, t.a), versCarte(*e, Pt{t.a.x + t.rayon, t.a.y})};  // centre, point du bord
    } else
      dc.points = {versCarte(*e, t.a), versCarte(*e, t.b)};
    c.circuit.dessins.push_back(dc);
    ++n;
  }
  const std::string uuid = x->uuid;
  std::erase_if(c.circuit.empreintes, [&](const EmpreintePlacee& y) { return y.composant == uuid; });
  std::erase_if(c.composants, [&](const ComposantElec& y) { return y.uuid == uuid; });
  return n;
}

// ---------- Générateurs ----------
double diametreInterieur(const Bobine& b) { return std::max(0.0, b.diametreExterieur - 2 * (b.spires * b.largeur + (b.spires - 1) * b.ecart)); }

double inductanceBobine(const Bobine& b) {
  // Coefficients de Mohan et al. (1999) : carrée 2,34 / 2,75 ; hexagonale 2,33 / 3,82 ; octogonale 2,25 / 3,55 ; le
  // cercle est pris comme l'octogone
  const double k1 = b.forme == FormeBobine::Carree ? 2.34 : b.forme == FormeBobine::Hexagonale ? 2.33 : 2.25;
  const double k2 = b.forme == FormeBobine::Carree ? 2.75 : b.forme == FormeBobine::Hexagonale ? 3.82 : 3.55;
  const double dout = b.diametreExterieur * 1e-3, din = diametreInterieur(b) * 1e-3;
  if (dout <= 0 || din <= 0 || b.spires <= 0) return 0;
  const double davg = (dout + din) / 2, rho = (dout - din) / (dout + din), mu0 = 4e-7 * std::numbers::pi;
  return k1 * mu0 * b.spires * b.spires * davg / (1 + k2 * rho);
}

std::vector<std::string> poserBobine(ConceptionElec& c, const Bobine& b, Pt centre, const std::string& reseau) {
  exigerModifiable(c);
  std::vector<std::string> r;
  const int cotes = b.forme == FormeBobine::Carree ? 4 : b.forme == FormeBobine::Hexagonale ? 6 : b.forme == FormeBobine::Octogonale ? 8 : 32;
  const double pas = b.largeur + b.ecart;
  // Spirale polygonale : le rayon décroît d'un pas par tour, réparti sur les côtés
  std::vector<Pt> points;
  const double r0 = (b.diametreExterieur - b.largeur) / 2;
  const double angle0 = b.forme == FormeBobine::Carree ? std::numbers::pi / 4 : 0;
  for (int i = 0; i <= b.spires * cotes; ++i) {
    const double rayon = (r0 - pas * static_cast<double>(i) / cotes) / (b.forme == FormeBobine::Circulaire ? 1.0 : std::cos(std::numbers::pi / cotes));
    if (rayon <= b.largeur) break;
    const double a = angle0 + 2 * std::numbers::pi * static_cast<double>(i) / cotes;
    points.push_back({centre.x + nm(rayon * std::cos(a)), centre.y + nm(rayon * std::sin(a))});
  }
  for (std::size_t i = 1; i < points.size(); ++i) {
    Piste p;
    p.uuid = nouvelUuid();
    p.reseau = reseau;
    p.couche = b.couche;
    p.a = points[i - 1];
    p.b = points[i];
    p.largeur = nm(b.largeur);
    c.circuit.pistes.push_back(p);
    r.push_back(p.uuid);
  }
  return r;
}

double poserPisteResistive(ConceptionElec& c, Pt a, Pt b, double ohms, Nm largeur, const std::string& reseau, Couche couche) {
  exigerModifiable(c);
  // R = ρ L / (e w) : longueur pour la résistance demandée ; cuivre à 1,7e-5 Ω·mm
  const double e = std::max(0.005, c.circuit.empilage.cuivre), w = mm(largeur), rho = 1.72e-5;
  const double L = ohms * e * w / rho;
  const double direct = mm(static_cast<Nm>(std::llround(distance(a, b))));
  const double ux = (mm(b.x) - mm(a.x)) / std::max(1e-9, direct), uy = (mm(b.y) - mm(a.y)) / std::max(1e-9, direct);
  const double nx = -uy, ny = ux;
  std::vector<Pt> pts{a};
  if (L <= direct + 1e-9) pts.push_back(b);
  else {
    // Méandre de pas 3 largeurs : n allers-retours de hauteur h donnent la longueur voulue
    const double pas = 3 * w;
    const int n = std::max(1, static_cast<int>(std::floor(direct / pas)) - 1);
    const double h = (L - direct) / (2.0 * n);
    double t = (direct - n * pas) / 2;
    pts.push_back(ptMm(mm(a.x) + ux * t, mm(a.y) + uy * t));
    for (int i = 0; i < n; ++i) {
      const double s = (i % 2 ? -1 : 1) * h;
      pts.push_back(ptMm(mm(a.x) + ux * t + nx * s, mm(a.y) + uy * t + ny * s));
      t += pas;
      pts.push_back(ptMm(mm(a.x) + ux * t + nx * s, mm(a.y) + uy * t + ny * s));
      pts.push_back(ptMm(mm(a.x) + ux * t, mm(a.y) + uy * t));
    }
    pts.push_back(b);
  }
  double total = 0;
  for (std::size_t i = 1; i < pts.size(); ++i) {
    if (pts[i] == pts[i - 1]) continue;
    Piste p;
    p.uuid = nouvelUuid();
    p.reseau = reseau;
    p.couche = couche;
    p.a = pts[i - 1];
    p.b = pts[i];
    p.largeur = largeur;
    c.circuit.pistes.push_back(p);
    total += distance(p.a, p.b) * 1e-6;
  }
  return total;
}

int poserCapteur(ConceptionElec& c, const Capteur& k, Pt centre) {
  exigerModifiable(c);
  int n = 0;
  Polygone tout;
  auto region = [&](Polygone forme, const std::string& reseau) {
    RegionCuivre r;
    r.uuid = nouvelUuid();
    r.reseau = reseau;
    r.genre = "capteur";
    r.forme = std::move(forme);
    c.circuit.regions.push_back(r);
    ++n;
  };
  const double t = k.taille;
  if (k.genre == GenreCapteur::Bouton) {
    region(cercle(centre, nm(t / 2)), k.prefixeReseau + "1");
    tout = cercle(centre, nm(t / 2 + k.degage));
  } else if (k.genre == GenreCapteur::Curseur) {
    // Segments en chevrons (dents imbriquées) le long de x
    const int s = std::max(2, k.segments);
    const double l = t / s, h = std::max(4.0, t / 5), dent = l / 3;
    for (int i = 0; i < s; ++i) {
      const double x0 = mm(centre.x) - t / 2 + i * l, y0 = mm(centre.y) - h / 2;
      Polygone p{ptMm(x0 + (i ? dent : 0), y0), ptMm(x0 + l - 0.25 + (i + 1 < s ? dent : 0), y0), ptMm(x0 + l - 0.25, y0 + h / 2), ptMm(x0 + l - 0.25 + (i + 1 < s ? dent : 0), y0 + h),
                 ptMm(x0 + (i ? dent : 0), y0 + h), ptMm(x0, y0 + h / 2)};
      region(orienter(p), k.prefixeReseau + std::to_string(i + 1));
    }
    tout = rectangle(centre, nm(t + 2 * k.degage + 2 * dent), nm(h + 2 * k.degage));
  } else {
    // Roue : n secteurs d'anneau
    const int s = std::max(3, k.segments);
    const double re = t / 2, ri = t / 4;
    for (int i = 0; i < s; ++i) {
      Polygone p;
      const double a0 = 2 * std::numbers::pi * i / s + 0.04, a1 = 2 * std::numbers::pi * (i + 1) / s - 0.04;
      for (int j = 0; j <= 12; ++j) {
        const double a = a0 + (a1 - a0) * j / 12;
        p.push_back({centre.x + nm(re * std::cos(a)), centre.y + nm(re * std::sin(a))});
      }
      for (int j = 12; j >= 0; --j) {
        const double a = a0 + (a1 - a0) * j / 12;
        p.push_back({centre.x + nm(ri * std::cos(a)), centre.y + nm(ri * std::sin(a))});
      }
      region(orienter(p), k.prefixeReseau + std::to_string(i + 1));
    }
    tout = cercle(centre, nm(re + k.degage));
  }
  // Zone dégagée : aucun remplissage de zone ni piste d'un autre réseau autour du capteur
  ZoneRegle z;
  z.uuid = nouvelUuid();
  z.nom = "Capteur " + k.prefixeReseau;
  z.contour = orienter(tout);
  z.interditZones = true;
  c.circuit.zonesRegles.push_back(z);
  return n;
}

}  // namespace prom::ecad

// Prométhée : sorties pour le fabricant et l'assembleur (8.5, 8.6, 8.15). Voir sorties_fabricant.hpp.
// Licence GPL-3.0-only.
#include "promethee/sorties_fabricant.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include <nlohmann/json.hpp>

#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
std::string nombre(double v, int decimales) {
  char t[64];
  std::snprintf(t, sizeof t, "%.*f", decimales, v);
  std::string s = t;
  if (s == "-0" || s.rfind("-0.", 0) == 0) {
    bool nul = true;
    for (char ch : s.substr(1))
      if (ch != '0' && ch != '.') nul = false;
    if (nul) s = s.substr(1);
  }
  return s;
}
std::string champ(const std::string& t, const std::string& sep) {
  if (t.find_first_of(sep + "\"\n") == std::string::npos) return t;
  std::string r = "\"";
  for (char ch : t) r += ch == '"' ? std::string("\"\"") : std::string(1, ch);
  return r + "\"";
}
// Motif à jokers « * » et « ? », sans casse
bool joker(const std::string& motif, const std::string& texte) {
  std::size_t m = 0, t = 0, etoile = std::string::npos, reprise = 0;
  auto egal = [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); };
  while (t < texte.size()) {
    if (m < motif.size() && (motif[m] == '?' || egal(motif[m], texte[t]))) {
      ++m, ++t;
    } else if (m < motif.size() && motif[m] == '*') {
      etoile = m++;
      reprise = t;
    } else if (etoile != std::string::npos) {
      m = etoile + 1;
      t = ++reprise;
    } else
      return false;
  }
  while (m < motif.size() && motif[m] == '*') ++m;
  return m == motif.size();
}
bool estCms(const ConceptionElec& c, const std::string& empreinte) {
  const DefEmpreinte* d = c.empreinte(empreinte);
  if (!d) return true;
  return std::none_of(d->pastilles.begin(), d->pastilles.end(), [](const Pastille& p) { return p.type == TypePastille::Traversante; });
}
}  // namespace

// ---------- Nomenclature configurable ----------
OptionsNomenclature optionsNomenclature(const std::string& format) {
  OptionsNomenclature o;
  o.format = format;
  if (format == "jlcpcb") {
    o.colonnes = {"valeur", "reperes", "empreinte", "lcsc"};
    o.entetes = {"Comment", "Designator", "Footprint", "LCSC Part #"};
  } else if (format == "pcbway") {
    o.colonnes = {"numero", "reperes", "quantite", "fabricant", "mpn", "description", "empreinte", "genre", "notes"};
    o.entetes = {"Item #", "*Designator", "*Qty", "Manufacturer", "*Mfg Part #", "Description / Value", "*Package/Footprint", "Type", "Your Instructions / Notes"};
  }
  return o;
}

std::vector<std::vector<std::string>> tableauNomenclature(const ConceptionElec& c, const OptionsNomenclature& o) {
  static const std::map<std::string, std::string> francais = {{"quantite", "Quantité"},  {"reperes", "Repères"},  {"valeur", "Valeur"},       {"empreinte", "Empreinte"},
                                                              {"fabricant", "Fabricant"}, {"mpn", "Référence fabricant"}, {"fournisseur", "Fournisseur"}, {"lcsc", "LCSC"},
                                                              {"description", "Description"}, {"genre", "Genre"}, {"face", "Face"}, {"numero", "N°"}, {"notes", "Notes"}};
  std::vector<std::vector<std::string>> r;
  std::vector<std::string> entete;
  for (std::size_t i = 0; i < o.colonnes.size(); ++i)
    entete.push_back(i < o.entetes.size() ? o.entetes[i] : (francais.count(o.colonnes[i]) ? francais.at(o.colonnes[i]) : o.colonnes[i]));
  r.push_back(entete);
  // Lignes : composants de la nomenclature, regroupés par valeur comprise (ou texte), empreinte et référence
  struct Ligne {
    std::vector<std::string> reperes;
    const ComposantElec* modele = nullptr;
    bool nonMonte = false, dessous = false;
  };
  std::vector<Ligne> lignes;
  std::map<std::string, std::size_t> parCle;
  for (const auto& x : c.composants) {
    if (!dansNomenclature(c, x)) continue;
    if (x.nePasMonter && !o.nonMontes) continue;
    const std::string valeur = o.regrouperValeur ? cleValeur(lireValeurComposant(x.valeur, familleDe(c, x)), x.valeur) : x.valeur;
    const std::string cle = valeur + '\x1f' + x.empreinte + '\x1f' + x.mpn + '\x1f' + (x.nePasMonter ? "nm" : "");
    auto [it, neuf] = parCle.emplace(cle, lignes.size());
    if (neuf) {
      Ligne l;
      l.modele = &x;
      l.nonMonte = x.nePasMonter;
      lignes.push_back(l);
    }
    lignes[it->second].reperes.push_back(x.reference);
    for (const auto& e : c.circuit.empreintes)
      if (e.composant == x.uuid && e.dessous) lignes[it->second].dessous = true;
  }
  for (auto& l : lignes) std::sort(l.reperes.begin(), l.reperes.end(), ordreNaturel);
  std::stable_sort(lignes.begin(), lignes.end(), [](const Ligne& a, const Ligne& b) {
    if (a.nonMonte != b.nonMonte) return !a.nonMonte;
    return ordreNaturel(a.reperes.front(), b.reperes.front());
  });
  int numero = 0;
  for (const auto& l : lignes) {
    const ComposantElec& x = *l.modele;
    std::string reperes;
    for (const auto& rr : l.reperes) reperes += (reperes.empty() ? "" : o.separateurReperes) + rr;
    std::vector<std::string> ligne;
    ++numero;
    for (const auto& col : o.colonnes) {
      if (col == "quantite") ligne.push_back(std::to_string(l.reperes.size()));
      else if (col == "reperes") ligne.push_back(reperes);
      else if (col == "valeur") ligne.push_back(x.valeur);
      else if (col == "empreinte") ligne.push_back(x.empreinte);
      else if (col == "fabricant") ligne.push_back(x.fabricant);
      else if (col == "mpn") ligne.push_back(x.mpn);
      else if (col == "fournisseur") ligne.push_back(x.fournisseur);
      else if (col == "lcsc") ligne.push_back(x.champs.count("LCSC") ? x.champs.at("LCSC") : x.champs.count("lcsc") ? x.champs.at("lcsc") : std::string());
      else if (col == "description") ligne.push_back(x.description.empty() ? x.valeur : x.description);
      else if (col == "genre") ligne.push_back(estCms(c, x.empreinte) ? "SMD" : "THT");
      else if (col == "face") ligne.push_back(l.dessous ? "Bottom" : "Top");
      else if (col == "numero") ligne.push_back(std::to_string(numero));
      else if (col == "notes") ligne.push_back(l.nonMonte ? "DNP" : "");
      else ligne.push_back(x.champs.count(col) ? x.champs.at(col) : std::string());
    }
    r.push_back(ligne);
  }
  return r;
}

std::string csvNomenclatureFormat(const ConceptionElec& c, const OptionsNomenclature& o) {
  std::string s;
  for (const auto& l : tableauNomenclature(c, o)) {
    for (std::size_t i = 0; i < l.size(); ++i) s += (i ? o.separateur : "") + champ(l[i], o.separateur);
    s += "\n";
  }
  return s;
}

// ---------- Corrections de rotation ----------
std::vector<CorrectionRotation> correctionsRotation(const std::string& profil0) {
  std::string profil = profil0;
  for (auto& ch : profil) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  // Zéro de JLCPCB (et de la plupart des assembleurs chinois) : broche 1 en haut à gauche pour les circuits,
  // SOT-23 et SOT-223 tournés d'un demi-tour par rapport à l'IPC-7351 de la bibliothèque
  if (profil == "JLCPCB" || profil == "PCBWAY")
    return {{"SOT-23*", 180}, {"SOT-223*", 180}, {"SOT-89*", 180}, {"SOIC-*", 270}, {"TSSOP-*", 270}, {"SSOP-*", 270}, {"*QFN*", 270}, {"*QFP*", 270}, {"TO-252*", 180}, {"SOD-*", 180}};
  return {};
}

double rotationCorrigee(const std::string& empreinte, double rotation, bool dessous, const std::vector<CorrectionRotation>& corrections) {
  double r = rotation;
  for (const auto& k : corrections)
    if ((!k.dessousSeulement || dessous) && joker(k.motif, empreinte)) {
      r += k.angle;
      break;
    }
  r = std::fmod(r, 360.0);
  if (r < 0) r += 360;
  if (std::abs(r - 360) < 1e-9) r = 0;
  return r;
}

// ---------- Positions ----------
std::pair<double, double> taillePanneau(const ConceptionElec& c, const OptionsPanneau& p) {
  const Boite b = boite(c.circuit.contour.polygone());
  const double W = mm(b.largeur()), H = mm(b.hauteur());
  const int C = std::max(1, p.colonnes), L = std::max(1, p.lignes);
  return {2 * p.rail + 2 * p.ecart + C * W + (C - 1) * p.ecart, 2 * p.rail + 2 * p.ecart + L * H + (L - 1) * p.ecart};
}

std::pair<double, double> origineCopie(const ConceptionElec& c, const OptionsPanneau& p, int ligne, int colonne) {
  // Comme panneauFabrication : copie (i, j) à rail + écart + j (W + écart) de la gauche et rail + écart + i (H + écart)
  // du haut ; repère de fabrication Y vers le haut
  const Boite b = boite(c.circuit.contour.polygone());
  const double W = mm(b.largeur()), H = mm(b.hauteur());
  const auto [PW, PH] = taillePanneau(c, p);
  (void)PW;
  return {p.rail + p.ecart + colonne * (W + p.ecart), PH - (p.rail + p.ecart + ligne * (H + p.ecart)) - H};
}

std::vector<Position> miresPanneau(const ConceptionElec& c, const OptionsPanneau& p) {
  const auto [PW, PH] = taillePanneau(c, p);
  const double m = p.rail / 2;
  std::vector<Position> r;
  int n = 0;
  for (const auto& [x, y] : std::vector<std::pair<double, double>>{{m, m}, {PW - m, m}, {m, PH - m}}) {
    Position q;
    q.repere = "FID" + std::to_string(++n);
    q.genre = "mire";
    q.empreinte = "Fiducial_1mm_Mask2mm";
    q.x = x;
    q.y = y;
    r.push_back(q);
  }
  return r;
}

std::vector<Position> positions(const ConceptionElec& c, const OptionsPositions& o) {
  std::vector<Position> base;
  const Boite carte = boite(c.circuit.contour.polygone());
  const auto corrections = correctionsRotation(o.profil);
  auto versFab = [&](Pt p) {
    if (o.origine) return std::pair{mm(p.x - o.origine->x), mm(o.origine->y - p.y)};
    const Pt q = versRepereFabrication(o.panneau ? carte : repereFichiers(c), p);
    return std::pair{mm(q.x), mm(q.y)};
  };
  std::vector<const EmpreintePlacee*> v;
  for (const auto& e : c.circuit.empreintes) v.push_back(&e);
  std::sort(v.begin(), v.end(), [&](const EmpreintePlacee* a, const EmpreintePlacee* b) {
    const ComposantElec *x = c.composant(a->composant), *y = c.composant(b->composant);
    return ordreNaturel(x ? x->reference : "", y ? y->reference : "");
  });
  for (const EmpreintePlacee* e : v) {
    const ComposantElec* x = c.composant(e->composant);
    const DefEmpreinte* d = c.empreinte(e->empreinte);
    if (!x || (e->dessous ? !o.dessous : !o.dessus)) continue;
    // Mires de la carte : pastilles marquées « fiducial » ; points de test : composants TP ou pastilles « test »
    bool mire = false, test = false;
    if (d)
      for (const auto& p : d->pastilles) {
        mire |= p.fabrication.rfind("fiducial", 0) == 0;
        test |= p.fabrication == "test";
      }
    test |= x->reference.rfind("TP", 0) == 0;
    const std::string genre = mire ? "mire" : test ? "test" : "composant";
    if ((genre == "composant" && (!o.composants || !dansFichierPlacement(c, *e))) || (genre == "mire" && !o.mires) || (genre == "test" && !o.pointsTest)) continue;
    Position q;
    q.repere = x->reference;
    q.valeur = x->valeur;
    q.empreinte = e->copieLocale.empty() ? e->empreinte : e->copieLocale;
    q.genre = genre;
    q.dessous = e->dessous;
    const auto [px, py] = versFab(d ? versCarte(*e, pointDePlacement(*d)) : e->position);
    q.x = px;
    q.y = py;
    q.rotation = rotationCorrigee(q.empreinte, e->rotation, e->dessous, corrections);
    base.push_back(q);
  }
  if (!o.panneau) return base;
  // Panneau : chaque copie décalée de son origine, puis les mires du cadre
  std::vector<Position> r;
  const int C = std::max(1, o.panneau->colonnes), L = std::max(1, o.panneau->lignes);
  for (int i = 0; i < L; ++i)
    for (int j = 0; j < C; ++j) {
      const auto [ox, oy] = origineCopie(c, *o.panneau, i, j);
      for (Position q : base) {
        q.x += ox;
        q.y += oy;
        q.copie = i * C + j + 1;
        r.push_back(q);
      }
    }
  if (o.mires)
    for (const auto& m : miresPanneau(c, *o.panneau)) r.push_back(m);
  return r;
}

const std::vector<GabaritPositions>& gabaritsPositions() {
  static const std::vector<GabaritPositions> g = [] {
    std::vector<GabaritPositions> v;
    v.push_back({"Prométhée", "csv", "Ref,Valeur,Empreinte,PosX,PosY,Rot,Face,Genre,Copie", "{repere},{valeur},{empreinte},{x:4},{y:4},{rotation:1},{face},{genre},{copie}", false, false});
    v.push_back({"JLCPCB", "csv", "Designator,Mid X,Mid Y,Layer,Rotation", "{repere},{x:4}mm,{y:4}mm,{face:Top/Bottom},{rotation:1}", false, false});
    v.push_back({"OpenPnP", "csv", "Designator,Value,Package,X,Y,Rotation,Side", "{repere},{valeur},{empreinte},{x:4},{y:4},{rotation:2},{face:top/bottom}", false, false});
    v.push_back({"Neoden", "csv", "Designator,Footprint,Mid X,Mid Y,Layer,Rotation,Comment", "{repere},{empreinte},{x:2}mm,{y:2}mm,{face:T/B},{rotation:2},{valeur}", true, false});
    v.push_back({"Charmhigh", "csv", "Designator,NozzleNum,StackNum,Mid X,Mid Y,Rotation,Height,Speed,Vision,Pressure,Explanation", "{repere},1,1,{x:2},{y:2},{rotation:2},0.5,0,1,0,{valeur}",
                 false, false});
    v.push_back({"MacroFab", "xyrs", "#Designator\tX-Loc\tY-Loc\tRotation\tSide\tType\tX-Size\tY-Size\tValue\tFootprint\tPopulate\tMPN", "{repere}\t{xmil:0}\t{ymil:0}\t{rotation:0}\t{face:1/2}\t1\t0\t0\t{valeur}\t{empreinte}\t1\t",
                 false, false});
    return v;
  }();
  return g;
}

std::optional<GabaritPositions> gabaritPositions(const std::string& nom) {
  for (const auto& g : gabaritsPositions())
    if (g.nom == nom) return g;
  return std::nullopt;
}

std::optional<GabaritPositions> lireGabaritPositions(const std::string& json, std::string* erreur) {
  try {
    const auto j = nlohmann::json::parse(json);
    GabaritPositions g;
    g.nom = j.value("nom", std::string());
    g.extension = j.value("extension", std::string("csv"));
    g.entete = j.value("entete", std::string());
    g.ligne = j.at("ligne").get<std::string>();
    g.rotationSigneeDessous = j.value("rotationSigneeDessous", false);
    g.miroirDessous = j.value("miroirDessous", false);
    return g;
  } catch (const std::exception& x) {
    if (erreur) *erreur = x.what();
    return std::nullopt;
  }
}

std::string texteGabaritPositions(const GabaritPositions& g) {
  nlohmann::ordered_json j;
  j["format"] = "promethee-gabarit-positions";
  j["nom"] = g.nom;
  j["extension"] = g.extension;
  j["entete"] = g.entete;
  j["ligne"] = g.ligne;
  j["rotationSigneeDessous"] = g.rotationSigneeDessous;
  j["miroirDessous"] = g.miroirDessous;
  return j.dump(1);
}

std::string ecrirePositions(const std::vector<Position>& p, const GabaritPositions& g, double largeurCarteMm) {
  std::string s = g.entete.empty() ? std::string() : g.entete + "\n";
  for (const auto& q : p) {
    std::string l;
    const std::string& m = g.ligne;
    for (std::size_t i = 0; i < m.size(); ++i) {
      if (m[i] != '{') {
        l += m[i];
        continue;
      }
      const std::size_t f = m.find('}', i);
      if (f == std::string::npos) {
        l += m.substr(i);
        break;
      }
      std::string nom = m.substr(i + 1, f - i - 1), arg;
      if (const auto d = nom.find(':'); d != std::string::npos) {
        arg = nom.substr(d + 1);
        nom = nom.substr(0, d);
      }
      const int dec = arg.empty() || !std::isdigit(static_cast<unsigned char>(arg[0])) ? 3 : std::atoi(arg.c_str());
      const double x = g.miroirDessous && q.dessous && largeurCarteMm > 0 ? largeurCarteMm - q.x : q.x;
      double rot = q.rotation;
      if (g.rotationSigneeDessous && q.dessous) rot = rot == 0 ? 0 : rot - 360;
      if (nom == "repere") l += q.repere;
      else if (nom == "valeur") l += q.valeur;
      else if (nom == "empreinte") l += q.empreinte;
      else if (nom == "genre") l += q.genre;
      else if (nom == "x") l += nombre(x, dec);
      else if (nom == "y") l += nombre(q.y, dec);
      else if (nom == "xmil") l += nombre(x / 0.0254, dec);
      else if (nom == "ymil") l += nombre(q.y / 0.0254, dec);
      else if (nom == "rotation") l += nombre(rot, dec);
      else if (nom == "copie") l += std::to_string(q.copie);
      else if (nom == "face") {
        if (arg.empty()) l += q.dessous ? "dessous" : "dessus";
        else {
          const auto barre = arg.find('/');
          l += barre == std::string::npos ? arg : q.dessous ? arg.substr(barre + 1) : arg.substr(0, barre);
        }
      } else
        l += m.substr(i, f - i + 1);
      i = f;
    }
    s += l + "\n";
  }
  return s;
}

// ---------- Statistiques et récapitulatif ----------
StatistiquesFabrication statistiquesFabrication(const ConceptionElec& c) {
  StatistiquesFabrication s;
  const Boite b = boite(c.circuit.contour.polygone());
  s.largeur = mm(b.largeur());
  s.hauteur = mm(b.hauteur());
  s.surface = aireMm2(c.circuit.contour.polygone());
  s.couchesCuivre = c.circuit.empilage.couchesCuivre;
  s.epaisseur = c.circuit.empilage.pile.empty() ? c.circuit.empilage.epaisseur : epaisseurPile(c.circuit.empilage.pile);
  s.finition = c.circuit.empilage.finition;
  s.couleurVernis = c.circuit.empilage.vernis;
  s.couleurSerigraphie = c.circuit.empilage.serigraphie;
  for (const auto& e : c.circuit.empreintes) {
    const ComposantElec* x = c.composant(e.composant);
    if (!x) continue;
    ++s.composants;
    if (x->nePasMonter) ++s.nonMontes;
    (estCms(c, e.empreinte) ? s.cms : s.traversants) += 1;
    (e.dessous ? s.dessous : s.dessus) += 1;
    if (const DefEmpreinte* d = c.empreinte(e.empreinte)) s.pastilles += static_cast<int>(d->pastilles.size());
  }
  s.lignesNomenclature = static_cast<int>(nomenclature(c).size());
  const Connectivite k = connectivite(c);
  const Remplissages zones = remplirZones(c, k);
  const ModeleFabrication m = modeleFabrication(c, k, zones);
  std::set<Nm> diametres;
  for (const auto& t : m.trous) {
    ++s.trous;
    (t.metallise ? s.trousMetallises : s.trousNonMetallises) += 1;
    diametres.insert(t.diametre);
    if (s.plusPetitTrou == 0 || mm(t.diametre) < s.plusPetitTrou) s.plusPetitTrou = mm(t.diametre);
  }
  s.diametresDifferents = static_cast<int>(diametres.size());
  for (const auto& v : c.circuit.vias) {
    ++s.vias;
    if (s.plusPetiteVia == 0 || mm(v.percage) < s.plusPetiteVia) s.plusPetiteVia = mm(v.percage);
  }
  for (const auto& p : c.circuit.pistes) {
    s.longueurPistes += distance(p.a, p.b) * 1e-6;
    if (s.plusFinePiste == 0 || mm(p.largeur) < s.plusFinePiste) s.plusFinePiste = mm(p.largeur);
  }
  s.plusPetitIsolement = mm(c.circuit.regles.isolement);
  for (const auto& cl : c.circuit.regles.classes) s.plusPetitIsolement = std::min(s.plusPetitIsolement, mm(cl.isolement));
  s.reseaux = 0;
  for (const auto& r : k.reseaux)
    if (r.broches.size() >= 2) ++s.reseaux;
  s.reseauxNonRoutes = static_cast<int>(etatRoutage(c, k, zones).chevelu.size());
  return s;
}

std::string statistiquesJson(const StatistiquesFabrication& s) {
  nlohmann::ordered_json j;
  j["carte"] = {{"largeur_mm", s.largeur}, {"hauteur_mm", s.hauteur}, {"surface_mm2", s.surface}, {"couches_cuivre", s.couchesCuivre}, {"epaisseur_mm", s.epaisseur},
                {"finition", s.finition}, {"vernis", s.couleurVernis}, {"serigraphie", s.couleurSerigraphie}};
  j["composants"] = {{"total", s.composants}, {"cms", s.cms}, {"traversants", s.traversants}, {"dessus", s.dessus}, {"dessous", s.dessous}, {"non_montes", s.nonMontes},
                     {"lignes_nomenclature", s.lignesNomenclature}, {"pastilles", s.pastilles}};
  j["percage"] = {{"trous", s.trous}, {"metallises", s.trousMetallises}, {"non_metallises", s.trousNonMetallises}, {"diametres", s.diametresDifferents}, {"vias", s.vias},
                  {"plus_petit_trou_mm", s.plusPetitTrou}, {"plus_petite_via_mm", s.plusPetiteVia}};
  j["routage"] = {{"plus_fine_piste_mm", s.plusFinePiste}, {"plus_petit_isolement_mm", s.plusPetitIsolement}, {"longueur_pistes_mm", s.longueurPistes}, {"reseaux", s.reseaux},
                  {"liaisons_non_routees", s.reseauxNonRoutes}};
  return j.dump(2);
}

std::string recapitulatifFabrication(const ConceptionElec& c, const StatistiquesFabrication& s, const std::string& fabricant, const std::vector<std::string>& fichiers) {
  std::string r;
  auto l = [&](const std::string& x) { r += x + "\n"; };
  l("Récapitulatif de fabrication — " + c.nom + " révision " + c.cartouche.revision);
  l("Fabricant visé : " + fabricant);
  l("");
  l("Carte : " + nombre(s.largeur, 2) + " × " + nombre(s.hauteur, 2) + " mm, " + std::to_string(s.couchesCuivre) + " couches de cuivre, épaisseur " + nombre(s.epaisseur, 2) + " mm");
  if (!s.finition.empty()) l("Finition : " + s.finition);
  if (!s.couleurVernis.empty()) l("Vernis : " + s.couleurVernis + (s.couleurSerigraphie.empty() ? "" : ", sérigraphie " + s.couleurSerigraphie));
  l("Plus fine piste : " + nombre(s.plusFinePiste, 3) + " mm ; plus petit isolement : " + nombre(s.plusPetitIsolement, 3) + " mm");
  l("Perçage : " + std::to_string(s.trous) + " trous (" + std::to_string(s.trousMetallises) + " métallisés), " + std::to_string(s.diametresDifferents) + " diamètres, plus petit " +
    nombre(s.plusPetitTrou, 2) + " mm");
  l("Assemblage : " + std::to_string(s.composants) + " composants (" + std::to_string(s.cms) + " CMS, " + std::to_string(s.traversants) + " traversants ; " + std::to_string(s.dessus) +
    " dessus, " + std::to_string(s.dessous) + " dessous), " + std::to_string(s.lignesNomenclature) + " lignes de nomenclature");
  if (s.reseauxNonRoutes > 0) l("ATTENTION : " + std::to_string(s.reseauxNonRoutes) + " liaison(s) non routée(s)");
  l("");
  l("Fichiers :");
  for (const auto& f : fichiers) l("  " + f);
  return r;
}

JeuSorties jeuArchiveFabricant(const std::string& fabricant) {
  JeuSorties j;
  std::string f = fabricant;
  for (auto& ch : f) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  j.nom = "Archive " + fabricant;
  j.dossier = "sorties/{projet}_rev{revision}_" + (f == "PCBWAY" ? std::string("PCBWay") : f);
  const std::string format = f == "PCBWAY" ? "pcbway" : "jlcpcb";
  j.sorties = {"gerber", "percages", "nomenclature-" + format, "placement-" + format, "recapitulatif", "statistiques"};
  j.zip = true;
  return j;
}

}  // namespace prom::ecad

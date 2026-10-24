// Prométhée : échange IDX avec la mécanique (10.10, 10.24). Voir idx.hpp. Licence GPL-3.0-only.
#include "promethee/idx.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <map>
#include <numbers>
#include <set>
#include <sstream>

#include "promethee/circuit_edition.hpp"
#include "promethee/hauteurs.hpp"
#include "xml_interne.hpp"

namespace prom::ecad {

namespace {
using xml_interne::lireXml;
using xml_interne::NoeudXml;

std::string echapper(const std::string& s) {
  std::string r;
  for (char ch : s) switch (ch) {
      case '&': r += "&amp;"; break;
      case '<': r += "&lt;"; break;
      case '>': r += "&gt;"; break;
      case '"': r += "&quot;"; break;
      default: r += ch;
    }
  return r;
}
std::string nb(double v) {
  char t[40];
  std::snprintf(t, sizeof t, "%.6f", std::abs(v) < 1e-12 ? 0.0 : v);
  std::string s = t;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s == "-0" ? "0" : s;
}
std::string maintenant() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char b[32];
  std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return b;
}
std::string local(const std::string& n) {
  const auto p = n.find(':');
  return p == std::string::npos ? n : n.substr(p + 1);
}

// ---------- Écriture ----------
class Ecrivain {
 public:
  explicit Ecrivain(int version) : m_v(version == 4 ? "4.0" : "3.0") {}
  std::string point(double x, double y) {
    const std::string id = "CP" + std::to_string(++m_n);
    m_corps << "    <foundation:CartesianPoint id=\"" << id << "\" xsi:type=\"d2:EDMDCartesianPoint\"><d2:X><property:Value>" << nb(x) << "</property:Value></d2:X><d2:Y><property:Value>" << nb(y)
            << "</property:Value></d2:Y></foundation:CartesianPoint>\n";
    return id;
  }
  // Contour (mm, y vers le haut) extrudé de zb à zh ; rend l'identifiant de l'élément de forme
  std::string forme(const std::vector<std::pair<double, double>>& pts, double zb, double zh, bool inverse = false) {
    std::vector<std::string> ids;
    for (const auto& [x, y] : pts) ids.push_back(point(x, y));
    if (!ids.empty()) ids.push_back(ids.front());
    const std::string pl = "PL" + std::to_string(++m_n), cs = "CS" + std::to_string(++m_n), se = "SE" + std::to_string(++m_n);
    m_corps << "    <foundation:PolyLine id=\"" << pl << "\" xsi:type=\"d2:EDMDPolyLine\"><d2:Thickness><property:Value>0</property:Value></d2:Thickness>";
    for (const auto& i : ids) m_corps << "<d2:Point>" << i << "</d2:Point>";
    m_corps << "</foundation:PolyLine>\n";
    courbes(cs, pl, zb, zh);
    m_corps << "    <foundation:ShapeElement id=\"" << se << "\" xsi:type=\"pdm:EDMDShapeElement\"><pdm:ShapeElementType>FeatureShapeElement</pdm:ShapeElementType><pdm:Inverted>"
            << (inverse ? "true" : "false") << "</pdm:Inverted><pdm:DefiningShape>" << cs << "</pdm:DefiningShape></foundation:ShapeElement>\n";
    return se;
  }
  std::string cercle(double x, double y, double d, double zb, double zh) {
    const std::string c = point(x, y), cc = "CC" + std::to_string(++m_n), cs = "CS" + std::to_string(++m_n), se = "SE" + std::to_string(++m_n);
    m_corps << "    <foundation:CircleCenter id=\"" << cc << "\" xsi:type=\"d2:EDMDCircleCenter\"><d2:Thickness><property:Value>0</property:Value></d2:Thickness><d2:CenterPoint>" << c
            << "</d2:CenterPoint><d2:Diameter><property:Value>" << nb(d) << "</property:Value></d2:Diameter></foundation:CircleCenter>\n";
    courbes(cs, cc, zb, zh);
    m_corps << "    <foundation:ShapeElement id=\"" << se << "\" xsi:type=\"pdm:EDMDShapeElement\"><pdm:ShapeElementType>FeatureShapeElement</pdm:ShapeElementType><pdm:Inverted>false</pdm:Inverted><pdm:DefiningShape>" << cs
            << "</pdm:DefiningShape></foundation:ShapeElement>\n";
    return se;
  }
  // Objet (pdm:Item, « single ») de géométrie typée
  std::string objet(const std::string& type, const std::string& nom, const std::vector<std::string>& formes, const std::string& numero, const std::map<std::string, std::string>& proprietes = {},
                    const std::string& couche = {}) {
    const std::string id = "IT" + std::to_string(++m_n);
    m_corps << "    <foundation:Item id=\"" << id << "\" xsi:type=\"pdm:EDMDItem\" geometryType=\"" << type << "\"><foundation:Name>" << echapper(nom)
            << "</foundation:Name><pdm:ItemType>single</pdm:ItemType><pdm:Identifier xsi:type=\"foundation:EDMDIdentifier\"><foundation:SystemScope>Promethee</foundation:SystemScope><foundation:Number>"
            << echapper(numero) << "</foundation:Number><foundation:Version>1</foundation:Version><foundation:Revision>0</foundation:Revision><foundation:Sequence>0</foundation:Sequence></pdm:Identifier>";
    if (!couche.empty()) m_corps << "<pdm:AssembleToName>" << echapper(couche) << "</pdm:AssembleToName>";
    for (const auto& f : formes) m_corps << "<pdm:Shape>" << f << "</pdm:Shape>";
    for (const auto& [k, v] : proprietes)
      m_corps << "<foundation:UserProperty xsi:type=\"property:EDMDUserSimpleProperty\"><property:Key><foundation:SystemScope>Promethee</foundation:SystemScope><foundation:ObjectName>" << echapper(k)
              << "</foundation:ObjectName></property:Key><property:Value>" << echapper(v) << "</property:Value></foundation:UserProperty>";
    m_corps << "<pdm:BaseLine><property:Value>true</property:Value></pdm:BaseLine></foundation:Item>\n";
    return id;
  }
  // Pose d'un objet dans l'assemblage de la carte
  void instance(const std::string& item, const std::string& nom, double x, double y, double angle, double z, const std::map<std::string, std::string>& proprietes = {}) {
    const double a = angle * std::numbers::pi / 180;
    m_instances << "      <pdm:ItemInstance id=\"II" << ++m_n << "\"><pdm:Item>" << item << "</pdm:Item><pdm:InstanceName><foundation:SystemScope>Promethee</foundation:SystemScope><foundation:ObjectName>"
                << echapper(nom) << "</foundation:ObjectName></pdm:InstanceName><pdm:Transformation><pdm:TransformationType>d2</pdm:TransformationType><pdm:xx>" << nb(std::cos(a)) << "</pdm:xx><pdm:xy>"
                << nb(-std::sin(a)) << "</pdm:xy><pdm:yx>" << nb(std::sin(a)) << "</pdm:yx><pdm:yy>" << nb(std::cos(a)) << "</pdm:yy><pdm:tx><property:Value>" << nb(x) << "</property:Value></pdm:tx><pdm:ty><property:Value>"
                << nb(y) << "</property:Value></pdm:ty><pdm:zOffset><property:Value>" << nb(z) << "</property:Value></pdm:zOffset></pdm:Transformation>";
    for (const auto& [k, v] : proprietes)
      m_instances << "<foundation:UserProperty xsi:type=\"property:EDMDUserSimpleProperty\"><property:Key><foundation:SystemScope>Promethee</foundation:SystemScope><foundation:ObjectName>" << echapper(k)
                  << "</foundation:ObjectName></property:Key><property:Value>" << echapper(v) << "</property:Value></foundation:UserProperty>";
    m_instances << "</pdm:ItemInstance>\n";
  }
  std::string fichier(const OptionsIdx& o, const std::string& nom, const std::string& instruction) const {
    std::ostringstream s;
    const std::string ns = "http://www.prostep.org/EDMD/" + m_v;
    s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<foundation:EDMDDataSet xmlns:foundation=\"" << ns << "/foundation\" xmlns:pdm=\"" << ns << "/pdm\" xmlns:d2=\"" << ns << "/2D\" xmlns:property=\"" << ns
      << "/property\" xmlns:computational=\"" << ns << "/computational\" xmlns:administration=\"" << ns << "/administration\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n";
    s << "  <foundation:Header xsi:type=\"foundation:EDMDHeader\"><foundation:Description>" << echapper(nom) << "</foundation:Description><foundation:CreatorName>" << echapper(o.auteur)
      << "</foundation:CreatorName><foundation:CreatorCompany>" << echapper(o.societe) << "</foundation:CreatorCompany><foundation:CreatorSystem>Prométhée</foundation:CreatorSystem>"
      << "<foundation:GlobalUnitLength>UNIT_MM</foundation:GlobalUnitLength><foundation:CreationDateTime>" << maintenant() << "</foundation:CreationDateTime><foundation:ModifiedDateTime>" << maintenant()
      << "</foundation:ModifiedDateTime></foundation:Header>\n  <foundation:Body xsi:type=\"foundation:EDMDDataSetBody\">\n"
      << m_corps.str() << "    <foundation:Item id=\"ASSEMBLAGE\" xsi:type=\"pdm:EDMDItem\"><foundation:Name>" << echapper(nom) << "</foundation:Name><pdm:ItemType>assembly</pdm:ItemType>\n"
      << m_instances.str() << "    </foundation:Item>\n  </foundation:Body>\n  <foundation:ProcessInstruction xsi:type=\"computational:" << instruction << "\"/>\n</foundation:EDMDDataSet>\n";
    return s.str();
  }

 private:
  void courbes(const std::string& cs, const std::string& geo, double zb, double zh) {
    m_corps << "    <foundation:CurveSet2d id=\"" << cs << "\" xsi:type=\"d2:EDMDCurveSet2d\"><pdm:ShapeDescriptionType>GeometricModel</pdm:ShapeDescriptionType><d2:LowerBound><property:Value>" << nb(zb)
            << "</property:Value></d2:LowerBound><d2:UpperBound><property:Value>" << nb(zh) << "</property:Value></d2:UpperBound><d2:DetailedGeometricModelElement>" << geo
            << "</d2:DetailedGeometricModelElement></foundation:CurveSet2d>\n";
  }
  std::string m_v;
  int m_n = 0;
  std::ostringstream m_corps, m_instances;
};
std::vector<std::pair<double, double>> enMm(const Polygone& p) {
  std::vector<std::pair<double, double>> r;
  for (const Pt& q : p) r.push_back({mm(q.x), -mm(q.y)});
  return r;
}
Polygone contourCarte(const ContourCarte& k) {
  if (!k.libre.empty()) return k.libre;
  return {k.origine, k.origine + Pt{k.largeur, 0}, k.origine + Pt{k.largeur, k.hauteur}, k.origine + Pt{0, k.hauteur}};
}
std::string natureZone(const ZoneRegle& z) {
  if (z.interditEmpreintes && z.interditPistes && z.interditVias) return "tout";
  if (z.interditEmpreintes || z.hauteurMax > 0) return "placement";
  if (z.interditVias && !z.interditPistes) return "vias";
  return "routage";
}
const char* typeZone(const std::string& nature) {
  if (nature == "placement") return "KEEPOUT_AREA_COMPONENT";
  if (nature == "vias") return "KEEPOUT_AREA_VIA";
  if (nature == "routage") return "KEEPOUT_AREA_ROUTE";
  return "KEEPOUT_AREA_OTHER";
}

// ---------- Lecture ----------
struct Instance {
  std::string id, item, nom;
  double tx = 0, ty = 0, angle = 0, z = 0;
  std::map<std::string, std::string> proprietes;
};
struct Objet {
  std::string id, type, nom, numero;
  std::vector<std::string> formes;
  std::map<std::string, std::string> proprietes;
};
struct Modele {
  std::map<std::string, std::pair<double, double>> points;
  std::map<std::string, std::vector<std::string>> polylignes;
  std::map<std::string, std::pair<std::string, double>> cercles;  // centre, diamètre
  std::map<std::string, std::vector<std::string>> courbes;      // CurveSet2d → géométries
  std::map<std::string, std::pair<double, double>> bornes;       // CurveSet2d → (bas, haut)
  std::map<std::string, std::string> elements;                   // ShapeElement → CurveSet2d
  std::map<std::string, Objet> objets;
  std::vector<Instance> instances;
  std::vector<std::pair<std::string, std::string>> changements;  // (prédécesseur, successeur), vides possibles
  std::string instruction, logiciel, auteur, date;
};
std::string valeur(const NoeudXml& n) {
  for (const auto& e : n.enfants)
    if (local(e.nom) == "Value") return e.texte;
  return n.texte;
}
double reel(const std::string& s) {
  char* fin = nullptr;
  const double v = std::strtod(s.c_str(), &fin);
  return fin == s.c_str() ? 0 : v;
}
const NoeudXml* enfantLocal(const NoeudXml& n, const char* nom) {
  for (const auto& e : n.enfants)
    if (local(e.nom) == nom) return &e;
  return nullptr;
}
std::map<std::string, std::string> proprietes(const NoeudXml& n) {
  std::map<std::string, std::string> r;
  for (const auto& e : n.enfants)
    if (local(e.nom) == "UserProperty") {
      std::string cle;
      if (const NoeudXml* k = enfantLocal(e, "Key")) {
        if (const NoeudXml* o = enfantLocal(*k, "ObjectName")) cle = o->texte;
        else cle = k->texte;
      }
      if (const NoeudXml* v = enfantLocal(e, "Value")) r[cle] = v->texte;
    }
  return r;
}
void parcourir(const NoeudXml& n, Modele& m) {
  const std::string l = local(n.nom);
  const std::string id = n.attr("id");
  if (l == "CartesianPoint") {
    double x = 0, y = 0;
    if (const NoeudXml* e = enfantLocal(n, "X")) x = reel(valeur(*e));
    if (const NoeudXml* e = enfantLocal(n, "Y")) y = reel(valeur(*e));
    m.points[id] = {x, y};
  } else if (l == "PolyLine") {
    for (const auto& e : n.enfants)
      if (local(e.nom) == "Point") m.polylignes[id].push_back(e.texte);
  } else if (l == "CircleCenter") {
    std::string c;
    double d = 0;
    if (const NoeudXml* e = enfantLocal(n, "CenterPoint")) c = e->texte;
    if (const NoeudXml* e = enfantLocal(n, "Diameter")) d = reel(valeur(*e));
    m.cercles[id] = {c, d};
  } else if (l == "CurveSet2d") {
    for (const auto& e : n.enfants)
      if (local(e.nom) == "DetailedGeometricModelElement") m.courbes[id].push_back(e.texte);
    double b = 0, h = 0;
    if (const NoeudXml* e = enfantLocal(n, "LowerBound")) b = reel(valeur(*e));
    if (const NoeudXml* e = enfantLocal(n, "UpperBound")) h = reel(valeur(*e));
    m.bornes[id] = {b, h};
  } else if (l == "ShapeElement") {
    if (const NoeudXml* e = enfantLocal(n, "DefiningShape")) m.elements[id] = e->texte;
  } else if (l == "Item") {
    Objet o;
    o.id = id;
    o.type = n.attr("geometryType");
    if (const NoeudXml* e = enfantLocal(n, "Name")) o.nom = e->texte;
    if (const NoeudXml* e = enfantLocal(n, "Identifier"))
      if (const NoeudXml* x = enfantLocal(*e, "Number")) o.numero = x->texte;
    for (const auto& e : n.enfants)
      if (local(e.nom) == "Shape") o.formes.push_back(e.texte);
    o.proprietes = proprietes(n);
    for (const auto& e : n.enfants)
      if (local(e.nom) == "ItemInstance") {
        Instance i;
        i.id = e.attr("id");
        if (const NoeudXml* x = enfantLocal(e, "Item")) i.item = x->texte;
        if (const NoeudXml* x = enfantLocal(e, "InstanceName")) {
          const NoeudXml* o2 = enfantLocal(*x, "ObjectName");
          i.nom = o2 ? o2->texte : x->texte;
        }
        if (const NoeudXml* t = enfantLocal(e, "Transformation")) {
          double xx = 1, yx = 0;
          if (const NoeudXml* x = enfantLocal(*t, "xx")) xx = reel(valeur(*x));
          if (const NoeudXml* x = enfantLocal(*t, "yx")) yx = reel(valeur(*x));
          if (const NoeudXml* x = enfantLocal(*t, "tx")) i.tx = reel(valeur(*x));
          if (const NoeudXml* x = enfantLocal(*t, "ty")) i.ty = reel(valeur(*x));
          if (const NoeudXml* x = enfantLocal(*t, "zOffset")) i.z = reel(valeur(*x));
          i.angle = std::atan2(yx, xx) * 180 / std::numbers::pi;
          if (std::abs(i.angle) < 1e-9) i.angle = 0;
          if (i.angle < 0) i.angle += 360;
        }
        i.proprietes = proprietes(e);
        if (i.nom.empty() && i.proprietes.count("REFDES")) i.nom = i.proprietes.at("REFDES");
        m.instances.push_back(i);
      }
    m.objets[id] = o;
  } else if (l == "Change") {
    std::string avant, apres;
    for (const auto& e : n.enfants) {
      const std::string le = local(e.nom);
      if (le.rfind("Predecessor", 0) == 0) avant = e.texte;
      if (le.rfind("Successor", 0) == 0 || le == "NewItem" || le == "NewItemInstance") apres = e.texte;
      if (le == "DeletedItem" || le == "DeletedItemInstance") avant = e.texte;
    }
    m.changements.push_back({avant, apres});
  } else if (l == "ProcessInstruction") {
    m.instruction = local(n.attr("xsi:type"));
  } else if (l == "Header") {
    if (const NoeudXml* e = enfantLocal(n, "CreatorSystem")) m.logiciel = e->texte;
    if (const NoeudXml* e = enfantLocal(n, "CreatorName")) m.auteur = e->texte;
    if (const NoeudXml* e = enfantLocal(n, "ModifiedDateTime")) m.date = e->texte;
  }
  for (const auto& e : n.enfants) parcourir(e, m);
}
// Contour (nm, y vers le bas) d'un objet, et ses bornes en hauteur
Polygone contourObjet(const Modele& m, const Objet& o, double* bas = nullptr, double* haut = nullptr) {
  Polygone r;
  for (const auto& f : o.formes) {
    std::string cs = f;
    if (const auto it = m.elements.find(f); it != m.elements.end()) cs = it->second;
    const auto c = m.courbes.find(cs);
    if (c == m.courbes.end()) continue;
    if (const auto b = m.bornes.find(cs); b != m.bornes.end()) {
      if (bas) *bas = b->second.first;
      if (haut) *haut = b->second.second;
    }
    for (const auto& g : c->second) {
      if (const auto pl = m.polylignes.find(g); pl != m.polylignes.end()) {
        for (const auto& p : pl->second)
          if (const auto q = m.points.find(p); q != m.points.end()) {
            const Pt x = ptMm(q->second.first, -q->second.second);
            if (r.empty() || !(r.back() == x)) r.push_back(x);
          }
        while (r.size() > 1 && r.front() == r.back()) r.pop_back();
      } else if (const auto ce = m.cercles.find(g); ce != m.cercles.end())
        if (const auto q = m.points.find(ce->second.first); q != m.points.end())
          for (int k = 0; k < 32; ++k) {
            const double a = 2 * std::numbers::pi * k / 32;
            r.push_back(ptMm(q->second.first + ce->second.second / 2 * std::cos(a), -(q->second.second + ce->second.second / 2 * std::sin(a))));
          }
    }
    if (!r.empty()) break;
  }
  return r;
}
bool estZone(const std::string& t) { return t.rfind("KEEPOUT", 0) == 0 || t.rfind("KEEPIN", 0) == 0; }
std::string natureDeType(const std::string& t) {
  if (t == "KEEPOUT_AREA_COMPONENT") return "placement";
  if (t == "KEEPOUT_AREA_VIA") return "vias";
  if (t == "KEEPOUT_AREA_ROUTE") return "routage";
  return "tout";
}
std::string texteMm(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.2f", v);
  std::string s = b;
  for (char& ch : s)
    if (ch == '.') ch = ',';
  return s + " mm";
}
}  // namespace

std::string idxLigneDeBase(const ConceptionElec& c, const OptionsIdx& o) {
  Ecrivain e(o.version);
  const double ep = c.circuit.empilage.epaisseur;
  // Carte : contour extrudé, découpes en creux
  std::vector<std::string> formesCarte{e.forme(enMm(contourCarte(c.circuit.contour)), 0, ep)};
  for (const auto& d : c.circuit.contour.decoupes) formesCarte.push_back(e.forme(enMm(d), 0, ep, true));
  const std::string carte = e.objet("BOARD_OUTLINE", c.nom, formesCarte, c.uuid.empty() ? "carte" : c.uuid, {{"EPAISSEUR", nb(ep)}});
  e.instance(carte, "CARTE", 0, 0, 0, 0);
  // Trous non métallisés et métallisés des empreintes (fixations, connecteurs)
  for (const auto& em : c.circuit.empreintes) {
    const DefEmpreinte* d = c.empreinte(em.empreinte);
    const ComposantElec* x = c.composant(em.composant);
    if (!d || !x) continue;
    for (const auto& p : d->pastilles) {
      if (p.percage < 800'000) continue;  // les trous des petites broches ne regardent pas la mécanique
      const Pt q = versCarte(em, p.position);
      const std::string f = e.cercle(mm(q.x), -mm(q.y), mm(p.percage), 0, ep);
      const std::string t = e.objet(p.type == TypePastille::NonMetallisee ? "HOLE_NON_PLATED" : "HOLE_PLATED", x->reference + "." + p.numero, {f}, x->uuid + ":" + p.numero);
      e.instance(t, x->reference + "." + p.numero, 0, 0, 0, 0);
    }
  }
  // Composants : un objet par empreinte (corps extrudé à sa hauteur), une pose par composant
  std::map<std::string, std::string> parEmpreinte;
  for (const auto& em : c.circuit.empreintes) {
    const DefEmpreinte* d = c.empreinte(em.empreinte);
    const ComposantElec* x = c.composant(em.composant);
    if (!d || !x) continue;
    const double h = hauteurComposant(c, *x).mm;
    const std::string cle = em.empreinte + "@" + nb(h);
    if (!parEmpreinte.count(cle)) {
      const Boite b = d->encombrement;
      const std::vector<std::pair<double, double>> rect{{mm(b.x0), -mm(b.y0)}, {mm(b.x1), -mm(b.y0)}, {mm(b.x1), -mm(b.y1)}, {mm(b.x0), -mm(b.y1)}};
      parEmpreinte[cle] = e.objet("COMPONENT", em.empreinte, {e.forme(rect, 0, h)}, em.empreinte, {{"HAUTEUR", nb(h)}});
    }
    std::map<std::string, std::string> pr{{"REFDES", x->reference}, {"VALUE", x->valeur}, {"SIDE", em.dessous ? "BOTTOM" : "TOP"}};
    if (em.verrou) pr["LOCKED"] = "true";
    if (!x->mpn.empty()) pr["PART_NUMBER"] = x->mpn;
    pr["UUID"] = x->uuid;
    e.instance(parEmpreinte[cle], x->reference, mm(em.position.x), -mm(em.position.y), em.rotation, em.dessous ? -ep : ep, pr);
  }
  // Zones interdites et de hauteur limitée
  for (const auto& z : c.circuit.zonesRegles) {
    if (!(z.interditEmpreintes || z.interditPistes || z.interditVias || z.hauteurMax > 0) || z.contour.size() < 3) continue;
    const double h = z.hauteurMax > 0 ? mm(z.hauteurMax) : 100;
    const std::string t = e.objet(typeZone(natureZone(z)), z.nom.empty() ? z.uuid : z.nom, {e.forme(enMm(z.contour), 0, h)}, z.uuid, {{"NATURE", natureZone(z)}});
    e.instance(t, z.nom.empty() ? z.uuid : z.nom, 0, 0, 0, 0, {{"UUID", z.uuid}});
  }
  // Pièces montées (dissipateurs, entretoises) : posées comme pièces mécaniques
  for (const auto& p : c.piecesMontees) {
    const std::string t = e.objet("COMPONENT_MECHANICAL", p.nom.empty() ? p.reference : p.nom, {}, p.uuid, {{"MPN", p.mpn}});
    e.instance(t, p.reference, p.x, -p.y, p.rotation, p.z, {{"UUID", p.uuid}, {"SIDE", p.dessous ? "BOTTOM" : "TOP"}});
  }
  // Cuivre : décalques plats sur leur couche, ou volumes à l'épaisseur du cuivre
  if (o.cuivre != OptionsIdx::Cuivre::Aucun) {
    const double cu = c.circuit.empilage.cuivre;
    for (const auto& z : c.circuit.zones) {
      if (z.contour.size() < 3 || !estCuivre(z.couche)) continue;
      const bool dessous = z.couche == Couche::CuivreDessous;
      const double zb = dessous ? -cu : ep, zh = o.cuivre == OptionsIdx::Cuivre::Volumes ? zb + cu : zb;
      const std::string t = e.objet("OTHER_OUTLINE", "Zone " + z.reseau, {e.forme(enMm(z.contour), zb, zh)}, z.uuid, {{"RESEAU", z.reseau}}, codeCouche(z.couche));
      e.instance(t, "Zone " + z.reseau, 0, 0, 0, 0);
    }
    for (const auto& p : c.circuit.pistes) {
      if (p.couche != Couche::CuivreDessus && p.couche != Couche::CuivreDessous) continue;
      Polygones f = formePiste(p);
      if (f.empty()) continue;
      const bool dessous = p.couche == Couche::CuivreDessous;
      const double zb = dessous ? -cu : ep, zh = o.cuivre == OptionsIdx::Cuivre::Volumes ? zb + cu : zb;
      const std::string t = e.objet("OTHER_OUTLINE", "Piste " + p.reseau, {e.forme(enMm(f.front()), zb, zh)}, p.uuid, {{"RESEAU", p.reseau}}, codeCouche(p.couche));
      e.instance(t, "Piste " + p.reseau, 0, 0, 0, 0);
    }
  }
  return e.fichier(o, c.nom, "EDMDProcessInstructionSendInformation");
}

PropositionIdx lirePropositionIdx(const std::string& xml, const ConceptionElec& c) {
  PropositionIdx r;
  Modele m;
  try {
    parcourir(lireXml(xml), m);
  } catch (const std::exception& e) {
    r.alertes.push_back(std::string("Fichier IDX illisible : ") + e.what());
    return r;
  }
  r.logiciel = m.logiciel;
  r.auteur = m.auteur;
  r.date = m.date;
  if (!m.instruction.empty() && m.instruction.find("SendChanges") == std::string::npos && m.instruction.find("SendInformation") == std::string::npos)
    r.alertes.push_back("Instruction « " + m.instruction + " » : le fichier est lu comme une proposition");
  // Ce que le fichier pose, comparé à la carte actuelle
  std::set<std::string> zonesVues, piecesVues;
  for (const auto& i : m.instances) {
    const auto it = m.objets.find(i.item);
    if (it == m.objets.end()) continue;
    const Objet& o = it->second;
    const std::string type = o.type;
    auto prop = [&](const char* k) {
      if (const auto p = i.proprietes.find(k); p != i.proprietes.end()) return p->second;
      if (const auto p = o.proprietes.find(k); p != o.proprietes.end()) return p->second;
      return std::string();
    };
    if (type == "COMPONENT" || type == "COMPONENT_ELECTRICAL" || (type.empty() && !prop("REFDES").empty())) {
      const ComposantElec* x = c.parReference(i.nom);
      if (!x) {
        r.alertes.push_back("Composant « " + i.nom + " » inconnu de la carte : ignoré");
        continue;
      }
      const EmpreintePlacee* em = nullptr;
      for (const auto& e : c.circuit.empreintes)
        if (e.composant == x->uuid) em = &e;
      if (!em) continue;
      const Pt p = ptMm(i.tx, -i.ty);
      const std::string cote = prop("SIDE");
      const bool dessous = cote.empty() ? em->dessous : (cote == "BOTTOM" || cote == "bottom");
      const double d = std::hypot(mm(p.x - em->position.x), mm(p.y - em->position.y));
      const double da = std::abs(std::remainder(i.angle - em->rotation, 360.0));
      if (d > 0.001 || da > 0.01 || dessous != em->dessous) {
        ChangementIdx ch;
        ch.genre = ChangementIdx::Genre::Deplacement;
        ch.identifiant = i.id;
        ch.reference = i.nom;
        ch.position = p;
        ch.rotation = i.angle;
        ch.dessous = dessous;
        ch.description = i.nom + (d > 0.001 ? " déplacé de " + texteMm(d) : std::string()) + (da > 0.01 ? " tourné à " + nb(i.angle) + "°" : std::string()) +
                         (dessous != em->dessous ? (dessous ? " retourné dessous" : " remis dessus") : std::string());
        r.changements.push_back(ch);
      }
      const std::string verrou = prop("LOCKED");
      if ((verrou == "true" || verrou == "TRUE" || verrou == "1") && !em->verrou) {
        ChangementIdx ch;
        ch.genre = ChangementIdx::Genre::Verrou;
        ch.identifiant = i.id;
        ch.reference = i.nom;
        ch.verrouille = true;
        ch.description = i.nom + " verrouillé par la mécanique";
        r.changements.push_back(ch);
      }
    } else if (estZone(type)) {
      const std::string uuid = prop("UUID").empty() ? o.numero : prop("UUID");
      zonesVues.insert(uuid);
      zonesVues.insert(o.nom);
      const bool connue = std::any_of(c.circuit.zonesRegles.begin(), c.circuit.zonesRegles.end(), [&](const ZoneRegle& z) { return z.uuid == uuid || (!z.nom.empty() && z.nom == o.nom); });
      if (connue) continue;
      double bas = 0, haut = 0;
      ChangementIdx ch;
      ch.genre = ChangementIdx::Genre::ZoneInterdite;
      ch.identifiant = o.id;
      ch.reference = o.nom.empty() ? i.nom : o.nom;
      ch.contour = contourObjet(m, o, &bas, &haut);
      for (Pt& q : ch.contour) q = q + ptMm(i.tx, -i.ty);
      ch.nature = prop("NATURE").empty() ? natureDeType(type) : prop("NATURE");
      if (ch.nature == "placement" && haut > 0 && haut < 50) ch.hauteurMax = nm(haut);
      if (ch.contour.size() < 3) {
        r.alertes.push_back("Zone « " + ch.reference + " » sans contour lisible : ignorée");
        continue;
      }
      ch.description = "Zone interdite « " + ch.reference + " » ajoutée (" + ch.nature + (ch.hauteurMax > 0 ? ", hauteur ≤ " + texteMm(haut) : std::string()) + ")";
      r.changements.push_back(ch);
    } else if (type == "COMPONENT_MECHANICAL" || type == "MCAD_PART") {
      const std::string uuid = prop("UUID").empty() ? o.numero : prop("UUID");
      piecesVues.insert(uuid);
      piecesVues.insert(i.nom);
      const bool connue = std::any_of(c.piecesMontees.begin(), c.piecesMontees.end(), [&](const PieceMontee& p) { return p.uuid == uuid || p.reference == i.nom; });
      if (connue) continue;
      ChangementIdx ch;
      ch.genre = ChangementIdx::Genre::PieceMecanique;
      ch.identifiant = i.id;
      ch.reference = i.nom;
      ch.nature = o.nom;
      ch.position = ptMm(i.tx, -i.ty);
      ch.rotation = i.angle;
      ch.dessous = prop("SIDE") == "BOTTOM";
      ch.verrouille = prop("LOCKED") == "true";
      ch.description = "Pièce « " + o.nom + " » posée par la mécanique (" + i.nom + ")";
      r.changements.push_back(ch);
    } else if (type == "BOARD_OUTLINE" || type == "BOARD") {
      double bas = 0, haut = 0;
      const Polygone p = contourObjet(m, o, &bas, &haut);
      const Polygone actuel = contourCarte(c.circuit.contour);
      if (p.size() >= 3 && std::abs(aireMm2(p) - aireMm2(actuel)) > 0.01) {
        ChangementIdx ch;
        ch.genre = ChangementIdx::Genre::Contour;
        ch.identifiant = o.id;
        ch.reference = o.nom;
        ch.contour = p;
        ch.description = "Contour de la carte changé (" + nb(aireMm2(actuel)) + " → " + nb(aireMm2(p)) + " mm²)";
        r.changements.push_back(ch);
      }
    }
  }
  // Retraits annoncés : prédécesseur sans successeur
  for (const auto& [avant, apres] : m.changements) {
    if (avant.empty() || !apres.empty()) continue;
    for (const auto& z : c.circuit.zonesRegles)
      if (z.uuid == avant || z.nom == avant) {
        ChangementIdx ch;
        ch.genre = ChangementIdx::Genre::ZoneRetiree;
        ch.identifiant = avant;
        ch.reference = z.nom.empty() ? z.uuid : z.nom;
        ch.description = "Zone « " + ch.reference + " » retirée par la mécanique";
        r.changements.push_back(ch);
      }
    for (const auto& p : c.piecesMontees)
      if (p.uuid == avant || p.reference == avant) {
        ChangementIdx ch;
        ch.genre = ChangementIdx::Genre::PieceRetiree;
        ch.identifiant = avant;
        ch.reference = p.reference;
        ch.description = "Pièce « " + p.reference + " » retirée par la mécanique";
        r.changements.push_back(ch);
      }
  }
  r.identifiant = m.date.empty() ? maintenant() : m.date;
  return r;
}

int appliquerPropositionIdx(ConceptionElec& c, const PropositionIdx& p, const std::vector<bool>& acceptes, const std::string& fichier) {
  if (c.verrouillee) throw ConceptionVerrouillee(c);
  int n = 0, refuses = 0;
  for (std::size_t i = 0; i < p.changements.size(); ++i) {
    const bool ok = i < acceptes.size() && acceptes[i];
    if (!ok) {
      ++refuses;
      continue;
    }
    const ChangementIdx& ch = p.changements[i];
    switch (ch.genre) {
      case ChangementIdx::Genre::Deplacement:
      case ChangementIdx::Genre::Verrou:
        if (const ComposantElec* x = c.parReference(ch.reference))
          for (auto& em : c.circuit.empreintes)
            if (em.composant == x->uuid) {
              if (ch.genre == ChangementIdx::Genre::Deplacement) {
                em.position = ch.position;
                em.rotation = ch.rotation;
                em.dessous = ch.dessous;
              } else
                em.verrou = true;
              ++n;
            }
        break;
      case ChangementIdx::Genre::ZoneInterdite: {
        ZoneRegle z;
        z.uuid = nouvelUuid();
        z.nom = ch.reference;
        z.contour = ch.contour;
        z.interditEmpreintes = ch.nature == "placement" ? ch.hauteurMax == 0 : ch.nature == "tout";
        z.hauteurMax = ch.hauteurMax;
        z.interditPistes = ch.nature == "routage" || ch.nature == "tout";
        z.interditVias = ch.nature == "vias" || ch.nature == "routage" || ch.nature == "tout";
        z.depuisBoitier = true;
        c.circuit.zonesRegles.push_back(z);
        ++n;
        break;
      }
      case ChangementIdx::Genre::ZoneRetiree: {
        const auto avant = c.circuit.zonesRegles.size();
        c.circuit.zonesRegles.erase(std::remove_if(c.circuit.zonesRegles.begin(), c.circuit.zonesRegles.end(), [&](const ZoneRegle& z) { return z.uuid == ch.identifiant || z.nom == ch.reference; }),
                                    c.circuit.zonesRegles.end());
        n += static_cast<int>(avant - c.circuit.zonesRegles.size());
        break;
      }
      case ChangementIdx::Genre::PieceMecanique: {
        PieceMontee pm;
        pm.uuid = nouvelUuid();
        pm.reference = ch.reference;
        pm.nom = ch.nature;
        pm.x = mm(ch.position.x);
        pm.y = mm(ch.position.y);
        pm.rotation = ch.rotation;
        pm.dessous = ch.dessous;
        c.piecesMontees.push_back(pm);
        ++n;
        break;
      }
      case ChangementIdx::Genre::PieceRetiree: {
        const auto avant = c.piecesMontees.size();
        c.piecesMontees.erase(std::remove_if(c.piecesMontees.begin(), c.piecesMontees.end(), [&](const PieceMontee& x) { return x.reference == ch.reference; }), c.piecesMontees.end());
        n += static_cast<int>(avant - c.piecesMontees.size());
        break;
      }
      case ChangementIdx::Genre::Contour:
        c.circuit.contour.libre = orienter(ch.contour);
        ++n;
        break;
      case ChangementIdx::Genre::Autre: break;
    }
  }
  EchangeIdx e;
  e.date = maintenant();
  e.sens = "reçu";
  e.fichier = fichier;
  e.logiciel = p.logiciel;
  e.acceptes = n;
  e.refuses = refuses;
  std::string resume;
  for (std::size_t i = 0; i < p.changements.size(); ++i)
    resume += (resume.empty() ? "" : " ; ") + p.changements[i].description + (i < acceptes.size() && acceptes[i] ? " (accepté)" : " (refusé)");
  e.resume = resume;
  inscrireEchangeIdx(c, e);
  return n;
}

std::string idxReponse(const PropositionIdx& p, const std::vector<bool>& acceptes) {
  std::ostringstream s;
  s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<foundation:EDMDDataSet xmlns:foundation=\"http://www.prostep.org/EDMD/3.0/foundation\" xmlns:computational=\"http://www.prostep.org/EDMD/3.0/computational\""
       " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n  <foundation:Header xsi:type=\"foundation:EDMDHeader\"><foundation:CreatorSystem>Prométhée</foundation:CreatorSystem><foundation:GlobalUnitLength>UNIT_MM</foundation:GlobalUnitLength><foundation:CreationDateTime>"
    << maintenant() << "</foundation:CreationDateTime></foundation:Header>\n  <foundation:Body xsi:type=\"foundation:EDMDDataSetBody\"/>\n"
    << "  <foundation:ProcessInstruction xsi:type=\"computational:EDMDProcessInstructionSendChanges\">\n    <computational:Changes>\n";
  for (std::size_t i = 0; i < p.changements.size(); ++i) {
    const bool ok = i < acceptes.size() && acceptes[i];
    s << "      <computational:Change><computational:Description>" << echapper(p.changements[i].description) << "</computational:Description><computational:PredecessorItemInstance>"
      << echapper(p.changements[i].identifiant) << "</computational:PredecessorItemInstance><computational:Response><computational:Accept>" << (ok ? "true" : "false")
      << "</computational:Accept></computational:Response></computational:Change>\n";
  }
  s << "    </computational:Changes>\n  </foundation:ProcessInstruction>\n</foundation:EDMDDataSet>\n";
  return s.str();
}

std::vector<EchangeIdx> historiqueIdx(const ConceptionElec& c) {
  std::vector<EchangeIdx> r;
  const auto it = c.extensions.find("historiqueIdx");
  if (it == c.extensions.end() || !it->second.is_array()) return r;
  for (const auto& j : it->second) {
    EchangeIdx e;
    e.date = j.value("date", "");
    e.sens = j.value("sens", "");
    e.fichier = j.value("fichier", "");
    e.logiciel = j.value("logiciel", "");
    e.resume = j.value("resume", "");
    e.acceptes = j.value("acceptes", 0);
    e.refuses = j.value("refuses", 0);
    r.push_back(e);
  }
  return r;
}

void inscrireEchangeIdx(ConceptionElec& c, const EchangeIdx& e) {
  auto& h = c.extensions["historiqueIdx"];
  if (!h.is_array()) h = nlohmann::ordered_json::array();
  h.push_back({{"date", e.date}, {"sens", e.sens}, {"fichier", e.fichier}, {"logiciel", e.logiciel}, {"resume", e.resume}, {"acceptes", e.acceptes}, {"refuses", e.refuses}});
}

}  // namespace prom::ecad

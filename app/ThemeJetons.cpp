// Prométhée : jetons de couleur des canevas, palettes et daltonismes (lot L2, tâche 2.31, ERG-04, ERG-36). Chaque
// encre du schéma, du circuit et des signes a un nom (« circuit.cuivreDessus ») : elle se lit, se règle, part dans
// un fichier de thème et en revient. Les palettes reprennent les couleurs d'autres logiciels, ou celles d'Okabe et
// Ito, lisibles par les daltoniens ; la simulation des daltonismes courants (modèle de Machado, 2009) vérifie que la
// sélection tient 3:1 sur chaque cuivre une fois les couleurs vues par un œil daltonien. Méthodes de Theme.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <functional>

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QWidget>

#include "EncresCircuit.hpp"
#include "Theme.hpp"

namespace {
struct Acces {
  const char* cle;
  const char* libelle;
  std::function<QColor&(Theme&)> couleur;
};
}  // namespace

// Les encres nommées, et où elles vivent dans le thème appliqué
static const std::vector<Acces>& acces() {
  static const std::vector<Acces> a = [] {
    std::vector<Acces> v;
    auto schema = [&v](const char* cle, const char* libelle, QColor EncresSchema::*m) { v.push_back({cle, libelle, [m](Theme& t) -> QColor& { return t.encresSchemaModifiables().*m; }}); };
    auto circuit = [&v](const char* cle, const char* libelle, QColor EncresCircuit::*m) { v.push_back({cle, libelle, [m](Theme& t) -> QColor& { return t.encresCircuitModifiables().*m; }}); };
    auto signes = [&v](const char* cle, const char* libelle, QColor SignesCanevas::*m, bool duCircuit) {
      v.push_back({cle, libelle, [m, duCircuit](Theme& t) -> QColor& { return t.signesModifiables(duCircuit).*m; }});
    };
    schema("schema.papier", "Schéma : papier", &EncresSchema::papier);
    schema("schema.hors", "Schéma : autour de la feuille", &EncresSchema::hors);
    schema("schema.cadre", "Schéma : cadre", &EncresSchema::cadre);
    schema("schema.encre", "Schéma : traits des symboles", &EncresSchema::encre);
    schema("schema.fondCorps", "Schéma : fond des symboles", &EncresSchema::fondCorps);
    schema("schema.fil", "Schéma : fils", &EncresSchema::fil);
    schema("schema.alim", "Schéma : alimentations", &EncresSchema::alim);
    schema("schema.broche", "Schéma : broches", &EncresSchema::broche);
    schema("schema.globale", "Schéma : étiquettes globales", &EncresSchema::globale);
    schema("schema.reseau", "Schéma : réseau surligné", &EncresSchema::reseau);
    schema("schema.choix", "Schéma : choix", &EncresSchema::choix);
    schema("schema.apercu", "Schéma : aperçu d’un tracé", &EncresSchema::apercu);
    schema("schema.grille", "Schéma : grille", &EncresSchema::grille);
    schema("schema.marque", "Schéma : marques", &EncresSchema::marque);
    circuit("circuit.fond", "Circuit : fond", &EncresCircuit::fond);
    circuit("circuit.carte", "Circuit : carte", &EncresCircuit::carte);
    circuit("circuit.bord", "Circuit : bord de la carte", &EncresCircuit::bord);
    circuit("circuit.grille", "Circuit : grille", &EncresCircuit::grille);
    circuit("circuit.cuivreDessus", "Circuit : cuivre du dessus", &EncresCircuit::cuivreDessus);
    circuit("circuit.cuivreDessous", "Circuit : cuivre du dessous", &EncresCircuit::cuivreDessous);
    circuit("circuit.cuivreInt1", "Circuit : cuivre intérieur 1", &EncresCircuit::cuivreInt1);
    circuit("circuit.cuivreInt2", "Circuit : cuivre intérieur 2", &EncresCircuit::cuivreInt2);
    circuit("circuit.traversant", "Circuit : pastilles traversantes", &EncresCircuit::traversant);
    circuit("circuit.via", "Circuit : vias", &EncresCircuit::via);
    circuit("circuit.serigDessus", "Circuit : sérigraphie du dessus", &EncresCircuit::serigDessus);
    circuit("circuit.serigDessous", "Circuit : sérigraphie du dessous", &EncresCircuit::serigDessous);
    circuit("circuit.chevelu", "Circuit : chevelu", &EncresCircuit::chevelu);
    circuit("circuit.choix", "Circuit : choix", &EncresCircuit::choix);
    signes("signes.schema.sel", "Signes du schéma : sélection", &SignesCanevas::sel, false);
    signes("signes.schema.err", "Signes du schéma : erreur", &SignesCanevas::err, false);
    signes("signes.schema.avert", "Signes du schéma : avertissement", &SignesCanevas::avert, false);
    signes("signes.circuit.sel", "Signes du circuit : sélection", &SignesCanevas::sel, true);
    signes("signes.circuit.err", "Signes du circuit : erreur", &SignesCanevas::err, true);
    signes("signes.circuit.avert", "Signes du circuit : avertissement", &SignesCanevas::avert, true);
    return v;
  }();
  return a;
}

std::vector<Theme::Jeton> Theme::jetons() {
  std::vector<Jeton> r;
  for (const auto& a : acces()) r.push_back({QString::fromLatin1(a.cle), QString::fromUtf8(a.libelle)});
  return r;
}

QColor Theme::jeton(const QString& cle) const {
  for (const auto& a : acces())
    if (cle == QLatin1String(a.cle)) return a.couleur(const_cast<Theme&>(*this));
  return {};
}

namespace {
QJsonObject jetonsGardes() { return QJsonDocument::fromJson(QSettings().value(QStringLiteral("apparence/jetons")).toByteArray()).object(); }
}  // namespace

void Theme::setJeton(const QString& cle, const QColor& c) {
  QJsonObject o = jetonsGardes();
  if (c.isValid()) o.insert(cle, c.name(QColor::HexRgb));
  else o.remove(cle);
  QSettings().setValue(QStringLiteral("apparence/jetons"), QJsonDocument(o).toJson(QJsonDocument::Compact));
  appliquer();
}

void Theme::effacerJetons() {
  QSettings().remove(QStringLiteral("apparence/jetons"));
  appliquer();
}

// ---------- Palettes ----------
namespace {
struct Palette {
  const char* nom;
  std::vector<std::pair<const char*, const char*>> couleurs;
};
const std::vector<Palette>& palettesDefinies() {
  static const std::vector<Palette> p = {
      {"Celles du thème", {}},
      {"Inspirées de KiCad",
       {{"schema.papier", "#F5F4EF"}, {"schema.encre", "#840000"}, {"schema.fondCorps", "#FFFFC2"}, {"schema.fil", "#009600"}, {"schema.broche", "#840000"},
        {"circuit.fond", "#001023"}, {"circuit.carte", "#0B1A2E"}, {"circuit.cuivreDessus", "#B82E2E"}, {"circuit.cuivreDessous", "#3E6BB0"}, {"circuit.cuivreInt1", "#3C8C3C"},
        {"circuit.cuivreInt2", "#A6651F"}, {"circuit.serigDessus", "#F2EDA1"}, {"circuit.serigDessous", "#E8D5FF"}, {"circuit.choix", "#FFFFFF"}}},
      {"Inspirées d’Altium",
       {{"schema.papier", "#FFFFF0"}, {"schema.encre", "#800000"}, {"schema.fondCorps", "#FFFFB0"}, {"schema.fil", "#000080"},
        {"circuit.fond", "#000000"}, {"circuit.carte", "#101010"}, {"circuit.cuivreDessus", "#C00000"}, {"circuit.cuivreDessous", "#0000C8"}, {"circuit.cuivreInt1", "#8C6B00"},
        {"circuit.cuivreInt2", "#00808C"}, {"circuit.serigDessus", "#FFFF00"}, {"circuit.serigDessous", "#E0E0FF"}, {"circuit.choix", "#FFFFFF"}}},
      {"Inspirées d’Eagle",
       {{"schema.papier", "#FFFFFF"}, {"schema.encre", "#A50000"}, {"schema.fil", "#007A00"},
        {"circuit.fond", "#000000"}, {"circuit.carte", "#141414"}, {"circuit.cuivreDessus", "#A82828"}, {"circuit.cuivreDessous", "#2828A8"}, {"circuit.cuivreInt1", "#287828"},
        {"circuit.cuivreInt2", "#787828"}, {"circuit.serigDessus", "#C8C8C8"}, {"circuit.serigDessous", "#E6E6FF"}, {"circuit.choix", "#FFFFFF"}}},
      // Okabe et Ito, assombries pour que la sélection blanche tienne 3:1 sur chaque cuivre, même vue par un œil daltonien
      {"Pour daltoniens (Okabe et Ito)",
       {{"schema.fil", "#0072B2"}, {"schema.encre", "#7A3B00"}, {"schema.reseau", "#D55E00"}, {"schema.globale", "#CC79A7"},
        {"circuit.fond", "#10141F"}, {"circuit.carte", "#161B28"}, {"circuit.cuivreDessus", "#B35F00"}, {"circuit.cuivreDessous", "#1F6FB2"}, {"circuit.cuivreInt1", "#007A5A"},
        {"circuit.cuivreInt2", "#9E4E7E"}, {"circuit.serigDessus", "#F0E442"}, {"circuit.serigDessous", "#EDE6FF"}, {"circuit.choix", "#FFFFFF"}, {"signes.circuit.sel", "#FFFFFF"}}},
  };
  return p;
}
}  // namespace

QStringList Theme::palettes() {
  QStringList r;
  for (const auto& p : palettesDefinies()) r << QString::fromUtf8(p.nom);
  return r;
}

int Theme::palette() const { return std::clamp(QSettings().value(QStringLiteral("apparence/palette"), 0).toInt(), 0, static_cast<int>(palettesDefinies().size()) - 1); }

void Theme::choisirPalette(int i) {
  QSettings().setValue(QStringLiteral("apparence/palette"), std::clamp(i, 0, static_cast<int>(palettesDefinies().size()) - 1));
  appliquer();
}

void Theme::rafraichir() { appliquer(); }

double Theme::echelleCanevas() { return instance().m_echelleCanevas; }
bool Theme::anticrenelage() { return instance().m_anticrenelage; }

// Appelé par appliquer(), avant que le circuit prenne ses encres : la palette, puis les jetons de l'utilisateur
void Theme::appliquerPaletteEtJetons() {
  {
    const QSettings s;
    m_echelleCanevas = std::clamp(s.value(QStringLiteral("apparence/echelleCanevas"), 1.0).toDouble(), 0.5, 3.0);
    m_anticrenelage = s.value(QStringLiteral("apparence/anticrenelage"), true).toBool();
  }
  auto poser = [this](const QString& cle, const QColor& c) {
    if (!c.isValid()) return;
    for (const auto& a : acces())
      if (cle == QLatin1String(a.cle)) a.couleur(*this) = c;
  };
  const auto& p = palettesDefinies()[static_cast<std::size_t>(palette())];
  for (const auto& [cle, hex] : p.couleurs) poser(QString::fromLatin1(cle), QColor(QString::fromLatin1(hex)));
  const QJsonObject o = jetonsGardes();
  for (auto it = o.begin(); it != o.end(); ++it) poser(it.key(), QColor(it.value().toString()));
}

// ---------- Fichiers de thème ----------
QJsonObject Theme::exporterTheme() const {
  QJsonObject couleurs;
  for (const auto& a : acces()) couleurs.insert(QString::fromLatin1(a.cle), a.couleur(const_cast<Theme&>(*this)).name(QColor::HexRgb));
  return QJsonObject{{QStringLiteral("format"), QStringLiteral("promethee-theme")}, {QStringLiteral("version"), 1}, {QStringLiteral("base"), cle(m_choix)},
                     {QStringLiteral("palette"), palette()}, {QStringLiteral("couleurs"), couleurs}};
}

bool Theme::importerTheme(const QJsonObject& o, QString* erreur) {
  if (o.value(QStringLiteral("format")).toString() != QLatin1String("promethee-theme")) {
    if (erreur) *erreur = QStringLiteral("ce fichier n’est pas un thème de Prométhée");
    return false;
  }
  QSettings s;
  s.setValue(QStringLiteral("apparence/palette"), o.value(QStringLiteral("palette")).toInt(0));
  // Les couleurs du fichier deviennent les jetons de l'utilisateur, sur le thème de base qu'il nomme
  s.setValue(QStringLiteral("apparence/jetons"), QJsonDocument(o.value(QStringLiteral("couleurs")).toObject()).toJson(QJsonDocument::Compact));
  choisir(depuisCle(o.value(QStringLiteral("base")).toString(), m_choix));
  appliquer();
  return true;
}

// ---------- Daltonismes ----------
QColor Theme::simuler(const QColor& c, Daltonisme d) {
  // Machado, Oliveira et Fernandes (2009), sévérité 1, sur le RVB linéaire
  static const double M[3][3][3] = {{{0.152286, 1.052583, -0.204868}, {0.114503, 0.786281, 0.099216}, {-0.003882, -0.048116, 1.051998}},
                                    {{0.367322, 0.860646, -0.227968}, {0.280085, 0.672501, 0.047413}, {-0.011820, 0.042940, 0.968881}},
                                    {{1.255528, -0.076749, -0.178779}, {-0.078411, 0.930809, 0.147602}, {0.004733, 0.691367, 0.303900}}};
  auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
  auto srgb = [](double v) {
    v = std::clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055;
  };
  const double r = lin(c.redF()), g = lin(c.greenF()), b = lin(c.blueF());
  const auto& m = M[static_cast<int>(d)];
  QColor x;
  x.setRgbF(static_cast<float>(srgb(m[0][0] * r + m[0][1] * g + m[0][2] * b)), static_cast<float>(srgb(m[1][0] * r + m[1][1] * g + m[1][2] * b)),
            static_cast<float>(srgb(m[2][0] * r + m[2][1] * g + m[2][2] * b)));
  return x;
}

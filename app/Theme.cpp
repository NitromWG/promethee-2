// Prométhée : thèmes de couleur de l'interface. Licence GPL-3.0-only.
#include "Theme.hpp"

#include "EncresCircuit.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleHints>
#include <QWidget>

#include "Journal.hpp"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

// ---------- Couleurs perceptuelles (OKLab) : la clarté se règle sans changer la teinte ----------
struct Lab {
  double L = 0, a = 0, b = 0;
};
double versLineaire(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double versSrgb(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055; }
Lab lab(const QColor& c) {
  const double r = versLineaire(c.redF()), g = versLineaire(c.greenF()), b = versLineaire(c.blueF());
  const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
  const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
  const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
          0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}
bool lineaire(const Lab& x, double& r, double& g, double& b) {
  const double l = x.L + 0.3963377774 * x.a + 0.2158037573 * x.b;
  const double m = x.L - 0.1055613458 * x.a - 0.0638541728 * x.b;
  const double s = x.L - 0.0894841775 * x.a - 1.2914855480 * x.b;
  const double l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
  r = 4.0767416621 * l3 - 3.3077115913 * m3 + 0.2309699292 * s3;
  g = -1.2684380046 * l3 + 2.6097574011 * m3 - 0.3413193965 * s3;
  b = -0.0041960863 * l3 - 0.7034186147 * m3 + 1.7076147010 * s3;
  constexpr double e = 1e-4;
  return r >= -e && r <= 1 + e && g >= -e && g <= 1 + e && b >= -e && b <= 1 + e;
}
// Hors du gamut sRGB, la chroma se réduit jusqu'à y rentrer (clarté et teinte gardées)
QColor couleur(Lab x, int alpha) {
  x.L = std::clamp(x.L, 0.0, 1.0);
  double r = 0, g = 0, b = 0;
  if (!lineaire(x, r, g, b)) {
    double bas = 0, haut = 1;
    for (int i = 0; i < 24; ++i) {
      const double k = (bas + haut) / 2;
      if (lineaire({x.L, x.a * k, x.b * k}, r, g, b)) bas = k;
      else haut = k;
    }
    lineaire({x.L, x.a * bas, x.b * bas}, r, g, b);
  }
  auto canal = [](double v) { return static_cast<int>(std::lround(std::clamp(versSrgb(std::clamp(v, 0.0, 1.0)), 0.0, 1.0) * 255)); };
  return QColor(canal(r), canal(g), canal(b), alpha);
}
double chroma(const Lab& x) { return std::hypot(x.a, x.b); }
constexpr double GRIS = 0.035;  // en dessous : couleur neutre, qui prend la teinte du thème

// ---------- Règles de traduction des thèmes sombres ----------
struct Regles {
  double texteHaut;   // clarté de l'encre principale traduite
  double texteBas;    // clarté du texte le plus discret (désactivé)
  double couleurMin;  // clarté minimale d'un texte coloré
  double fondBlanc;   // clarté d'un fond blanc traduit
  double fondPente;   // clarté gagnée par unité de clarté perdue
  double bordure;     // clarté du filet ordinaire
  double teinteA, teinteB;
  bool contraste;
};
Regles regles(IdTheme id) {
  switch (id) {
    case IdTheme::Promethee: return {0.94, 0.53, 0.70, 0.235, 1.25, 0.36, 0.004, -0.050, false};
    case IdTheme::Sombre: return {0.94, 0.53, 0.70, 0.255, 1.20, 0.37, 0.0, -0.008, false};
    case IdTheme::ContrasteEleve: return {1.0, 0.72, 0.84, 0.0, 0.70, 0.85, 0.0, 0.0, true};
    default: return {0.19, 0.70, 0.0, 1.0, 0.0, 0.88, 0.0, 0.0, false};
  }
}

// Couleurs du jeu clair d'origine et leur rôle : traduites exactement vers le rôle du thème
enum class Role { Fenetre, Surface, Champ, Survol, Choisi, Bordure, Texte, TexteSecondaire, TexteDiscret, TexteDesactive, Accent, Marque, Succes, Alerte, Erreur };
const std::unordered_map<QRgb, Role>& rolesTexte() {
  static const std::unordered_map<QRgb, Role> t = {
      {0x10142A, Role::Texte},           {0x232A45, Role::Texte},           {0x1A203C, Role::Texte},           {0x18211D, Role::Texte},
      {0x363D5C, Role::TexteSecondaire}, {0x4A5270, Role::TexteSecondaire}, {0x565E7D, Role::TexteSecondaire}, {0x5A6380, Role::TexteSecondaire},
      {0x6B7393, Role::TexteDiscret},    {0x8A91AC, Role::TexteDiscret},    {0x9AA0B8, Role::TexteDesactive},  {0xC2402E, Role::Erreur},
      {0x2E8B57, Role::Succes},          {0x2F7D53, Role::Succes},          {0x267A4C, Role::Succes},          {0xA8620E, Role::Alerte},
      {0xA87410, Role::Alerte},          {0x2F6BF5, Role::Accent},          {0x5B4BF5, Role::Marque}};
  return t;
}
const std::unordered_map<QRgb, Role>& rolesFond() {
  static const std::unordered_map<QRgb, Role> t = {
      {0xEEF1F8, Role::Fenetre}, {0xF4F6FB, Role::Fenetre}, {0xECEFF8, Role::Fenetre}, {0xE9ECF5, Role::Fenetre}, {0xFAFBFE, Role::Surface},
      {0xFBFCFE, Role::Surface}, {0xFFFFFF, Role::Champ},   {0xEEF1F9, Role::Survol},  {0xEFF3FF, Role::Survol},  {0xE9EEFF, Role::Survol},
      {0xF1F3FA, Role::Survol},  {0xE3E6F1, Role::Survol},  {0xDDE5FF, Role::Choisi},  {0xE7E4FF, Role::Choisi}};
  return t;
}
const std::unordered_map<QRgb, Role>& rolesBordure() {
  static const std::unordered_map<QRgb, Role> t = {
      {0xD2D7E6, Role::Bordure}, {0xCDD3E4, Role::Bordure}, {0xD7DCEA, Role::Bordure}, {0xDEE2EE, Role::Bordure}, {0xE1E5F0, Role::Bordure}};
  return t;
}
QColor selonRole(const CouleursInterface& c, Role r) {
  switch (r) {
    case Role::Fenetre: return c.fenetre;
    case Role::Surface: return c.surface;
    case Role::Champ: return c.champ;
    case Role::Survol: return c.survol;
    case Role::Choisi: return c.choisi;
    case Role::Bordure: return c.bordure;
    case Role::Texte: return c.texte;
    case Role::TexteSecondaire: return c.texteSecondaire;
    case Role::TexteDiscret: return c.texteDiscret;
    case Role::TexteDesactive: return c.texteDesactive;
    case Role::Accent: return c.accentTexte;
    case Role::Marque: return c.marque;
    case Role::Succes: return c.succes;
    case Role::Alerte: return c.alerte;
    case Role::Erreur: return c.erreur;
  }
  return {};
}
std::optional<QColor> exacte(const std::unordered_map<QRgb, Role>& table, const CouleursInterface& c, const QColor& x) {
  const auto it = table.find(x.rgb() & 0xFFFFFF);
  if (it == table.end()) return std::nullopt;
  QColor r = selonRole(c, it->second);
  r.setAlpha(x.alpha());
  return r;
}

// ---------- Jeux de couleurs des thèmes ----------
QColor h(QRgb v, int alpha = 255) { return QColor((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, alpha); }

CouleursInterface interfaceDe(IdTheme id) {
  CouleursInterface c;
  switch (id) {
    case IdTheme::Promethee:
      c = {h(0x0C1028), h(0x131938), h(0x181F45), h(0x202957), h(0x2B3577), h(0x2A3262), h(0xE8ECFF), h(0xB8C1E6), h(0x8D97C3), h(0x5B648F), h(0x3563E9), h(0x8DAEFF),
           h(0x9B84FF), h(0xFFFFFF), h(0x4FD493), h(0xFFB547), h(0xFF7262), h(0xE8ECFF), h(0x0C1028), h(0x070A1A), h(0x0C1028), h(0xDCE2FA), h(0x1C2244),
           h(0x262F63), h(0x080B1E), h(0xFFFFFF), true, false};
      break;
    case IdTheme::Sombre:
      c = {h(0x17181C), h(0x1E2025), h(0x25272D), h(0x2D3037), h(0x2E3D63), h(0x373A43), h(0xE9EAEE), h(0xB9BDC7), h(0x8E939E), h(0x5D626C), h(0x3A68E0), h(0x8AB0FF),
           h(0x9C8CFF), h(0xFFFFFF), h(0x4CC38A), h(0xF0A847), h(0xFF6B5B), h(0xE9EAEE), h(0x17181C), h(0x101114), h(0x17181C), h(0xD9DCE3), h(0x2A2C32),
           h(0x3B3F48), h(0x141518), h(0xFFFFFF), true, true};
      break;
    case IdTheme::ContrasteEleve:
      c = {h(0x000000), h(0x000000), h(0x000000), h(0x1F1F1F), h(0x0B4FD6), h(0xBDBDBD), h(0xFFFFFF), h(0xFFFFFF), h(0xE6E6E6), h(0x9E9E9E), h(0x0B4FD6), h(0x6FC3FF),
           h(0xB59BFF), h(0xFFFFFF), h(0x3DF07A), h(0xFFD400), h(0xFF6B6B), h(0xFFFFFF), h(0x000000), h(0x000000), h(0x000000), h(0xFFFFFF), h(0xFFFFFF),
           h(0x141414), h(0x000000), h(0xFFFFFF), true, true};
      break;
    default:  // Clair : le jeu d'origine
      c = {h(0xEEF1F8), h(0xFAFBFE), h(0xFFFFFF), h(0xEEF1F9), h(0xDDE5FF), h(0xD2D7E6), h(0x10142A), h(0x4A5270), h(0x6B7393), h(0x9AA0B8), h(0x2F6BF5), h(0x2F6BF5),
           h(0x5B4BF5), h(0xFFFFFF), h(0x2E8B57), h(0xA8620E), h(0xC2402E), h(0x10142A), h(0xFBFCFE), h(0x0A0D20), h(0x0E1230), h(0xDCE2FA), h(0x1C2244),
           h(0xF6F8FD), h(0xD3D9E8), h(0x000000), false, false};
      break;
  }
  return c;
}

EncresSchema schemaClair() {
  return {h(0xFFFFFF), h(0xEEF1F8), h(0x6B7393), h(0x232A45), h(0xF1F3FF), h(0x0E94B8), h(0xC2402E), h(0x2F6BF5), h(0x8868F8), h(0x6A4CF5),
          h(0xB35900), h(0xC9CFDF), h(0xFAFBFE), h(0x5B4BF5), h(0x6B7393), h(0xF1EEFF)};  // aperçu à 4,8:1 sur le papier (G3)
}
EncresSchema schemaDe(IdTheme id) {
  switch (id) {
    case IdTheme::Promethee: {
      EncresSchema s = schemaClair();
      s.hors = h(0x0C1028);  // la feuille posée sur le bureau bleu nuit
      return s;
    }
    case IdTheme::Sombre:
      return {h(0x1C1E24), h(0x131418), h(0x8D93A3), h(0xE2E5EC), h(0x262A35), h(0x3CC8EC), h(0xFF7A66), h(0x6E9BFF), h(0xB49CFF), h(0xA58CFF),
              h(0xFFA552), h(0x3A3E4A), h(0x22252C), h(0xA58CFF), h(0x9AA0AE), h(0x2D2840)};
    case IdTheme::ContrasteEleve:
      return {h(0x000000), h(0x1A1A1A), h(0xFFFFFF), h(0xFFFFFF), h(0x000000), h(0x00E5FF), h(0xFF6B6B), h(0xFFD400), h(0xFF66FF), h(0xFF66FF),
              h(0xFFB000), h(0x4D4D4D), h(0x000000), h(0xFFFFFF), h(0xE6E6E6), h(0x000000)};
    default: return schemaClair();
  }
}
// Signes des canevas (Spécifications, charte) : jeu clair, jeu sombre, contraste élevé (4,5:1 sur le noir)
SignesCanevas signesClairs() { return {h(0x2F6FEB), h(0x8B3FD9), h(0x2F6FEB), h(0xD92D20), h(0xB54708), h(0x475467), h(0x7A8494)}; }
SignesCanevas signesSombres() { return {h(0x6EA0FF), h(0xC79BFF), h(0x6EA0FF), h(0xFF6B5E), h(0xFDB022), h(0x98A2B3), h(0x8A94A6)}; }
SignesCanevas signesContraste() { return {h(0x4DA6FF), h(0xFF66FF), h(0x4DA6FF), h(0xFF6B6B), h(0xFFD400), h(0xE6E6E6), h(0xA6A6A6)}; }

EncresEsquisse esquisseDe(IdTheme id) {
  switch (id) {
    case IdTheme::Sombre:
      return {h(0x1B1D22), QColor(220, 225, 235, 18), QColor(220, 225, 235, 38), h(0xFF7A66, 150), h(0x5FD08F, 150), h(0xD5DAD7), h(0x5FD08F), h(0x7FB4F5),
              h(0x5B9BFF), h(0x7FB4F5), h(0xFFA552), h(0xFF6B5B), h(0xE6EAE8), h(0x8FB0FF), h(0x9AA6A0), QColor(0x2A, 0x2D, 0x34, 235), h(0x3D414B), h(0xE9EAEE)};
    case IdTheme::ContrasteEleve:
      return {h(0x000000), QColor(255, 255, 255, 30), QColor(255, 255, 255, 64), h(0xFF6B6B, 200), h(0x3DF07A, 200), h(0xFFFFFF), h(0x3DF07A), h(0x6FC3FF),
              h(0xFFD400), h(0x6FC3FF), h(0xFFB000), h(0xFF6B6B), h(0xFFFFFF), h(0x6FC3FF), h(0xCCCCCC), QColor(0, 0, 0, 240), h(0xFFFFFF), h(0xFFFFFF)};
    default:  // Clair et Prométhée : papier clair
      return {h(0xFBFCFB), QColor(24, 33, 29, 16), QColor(24, 33, 29, 34), h(0xC2402E, 140), h(0x2F7D53, 140), h(0x3A4640), h(0x2E8B57), h(0x6FA8EC),
              h(0x1E7BE0), h(0x6FA8EC), h(0xE07B1E), h(0xC2402E), h(0x18211D), h(0x1D3F9A), h(0x7D8A84), QColor(255, 255, 255, 235), h(0xCDD3E4), h(0x232A45)};
  }
}
EncresCarte carteDe(IdTheme id) {
  switch (id) {
    case IdTheme::Sombre:
      return {h(0x1A1D24), QColor(220, 225, 240, 14), QColor(220, 225, 240, 30), h(0x4A5060), h(0x2A2E38), h(0x9AA2B8), h(0xE6E9F0), h(0xAEB4C4), QColor(30, 33, 40, 225)};
    case IdTheme::ContrasteEleve:
      return {h(0x000000), QColor(255, 255, 255, 28), QColor(255, 255, 255, 56), h(0x808080), h(0x333333), h(0xFFFFFF), h(0xFFFFFF), h(0xE6E6E6), QColor(0, 0, 0, 230)};
    default:
      return {h(0xE6EAF4), QColor(16, 20, 42, 14), QColor(16, 20, 42, 28), h(0xC9CEDD), h(0xDCE1EE), h(0x7F879F), h(0x10142A), h(0x565E7A), QColor(255, 255, 255, 220)};
  }
}

// Circuit : un canevas sombre dans tous les thèmes, noir au contraste élevé. Cuivres assez sombres pour que la
// sélection blanche y tienne 3:1, assez clairs pour tenir sur la carte (G3)
EncresCircuit circuitDe(IdTheme id) {
  EncresCircuit e{h(0x0B0F24), h(0x262F5A), h(0x12193D), h(0xF2C14E), h(0xD9B64A), h(0xC9CED9), h(0xF2F2F2), h(0xECE6FF), QColor(0xE8, 0xEC, 0xFF, 170),
                  h(0xFFFFFF), h(0xFF4D4D), h(0xE8613C), h(0x4F7BFF), h(0x1F9A68), h(0xA9801C)};
  if (id == IdTheme::ContrasteEleve) {
    e.fond = h(0x000000);
    e.grille = h(0x4D4D4D);
    e.carte = h(0x101010);
    e.bord = h(0xFFD400);
    e.chevelu = QColor(255, 255, 255, 220);
    e.cuivreDessus = h(0xF05A2A);
    e.cuivreInt1 = h(0x00A060);
    e.cuivreInt2 = h(0xB88A00);
  }
  return e;
}

QPalette paletteDe(const CouleursInterface& c, IdTheme id) {
  QPalette p;
  p.setColor(QPalette::Window, c.fenetre);
  p.setColor(QPalette::WindowText, c.texte);
  p.setColor(QPalette::Base, c.champ);
  p.setColor(QPalette::Text, c.texte);
  p.setColor(QPalette::Button, id == IdTheme::Clair ? h(0xFBFCFE) : c.surface);
  p.setColor(QPalette::ButtonText, c.texte);
  p.setColor(QPalette::Highlight, c.accent);
  p.setColor(QPalette::HighlightedText, c.surAccent);
  p.setColor(QPalette::ToolTipBase, c.infobulle);
  p.setColor(QPalette::ToolTipText, c.texteInfobulle);
  p.setColor(QPalette::Mid, c.bordure);
  p.setColor(QPalette::Disabled, QPalette::Text, c.texteDesactive);
  p.setColor(QPalette::Disabled, QPalette::ButtonText, c.texteDesactive);
  p.setColor(QPalette::Disabled, QPalette::WindowText, c.texteDesactive);
  if (id == IdTheme::Clair) {
    p.setColor(QPalette::AlternateBase, h(0xF1F3FA));
    p.setColor(QPalette::PlaceholderText, h(0x7C84A0));
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Dark, h(0xA9B0C8));
    return p;
  }
  p.setColor(QPalette::AlternateBase, c.surface);
  p.setColor(QPalette::PlaceholderText, c.texteDiscret);
  p.setColor(QPalette::Light, c.survol.lighter(130));
  p.setColor(QPalette::Midlight, c.survol);
  p.setColor(QPalette::Dark, c.fenetre.darker(160));
  p.setColor(QPalette::Shadow, Qt::black);
  p.setColor(QPalette::Link, c.accentTexte);
  p.setColor(QPalette::LinkVisited, c.marque);
  p.setColor(QPalette::BrightText, c.erreur);
  p.setColor(QPalette::Disabled, QPalette::Highlight, c.survol);
  return p;
}

QString nomCss(const QColor& c) {
  if (c.alpha() == 255) return c.name(QColor::HexRgb).toUpper();
  return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

// Feuille de style de l'application : celle d'origine, ses couleurs prises au thème
QString feuilleGlobale(const CouleursInterface& c, IdTheme id) {
  const bool clair = id == IdTheme::Clair;
  QString s = QStringLiteral(R"(
    QMenuBar { background: {surface}; border-bottom: 1px solid {bordure}; padding: 2px; }
    QMenuBar::item { padding: 5px 10px; border-radius: 5px; }
    QMenuBar::item:selected { background: {survol}; }
    QMenu { background: {surface}; border: 1px solid {bordure}; padding: 4px; }
    QMenu::item { padding: 6px 26px 6px 14px; border-radius: 4px; }
    QMenu::item:selected { background: {survol}; color: {texte}; }
    QMenu::item:disabled { color: {desactive}; }
    QMenu::separator { height: 1px; background: {bordure}; margin: 4px 6px; }
    QToolBar { background: {surface}; border: none; border-bottom: 1px solid {bordure}; spacing: 4px; padding: 3px 6px; }
    QToolButton { padding: 4px 10px; border-radius: 5px; color: {texte}; }
    QToolButton:hover { background: {survol}; }
    QToolButton:checked { background: {coche}; color: {texte}; }
    QToolButton:disabled { color: {desactive}; }
    QToolButton[popupMode="1"] { padding-right: 14px; }
    QToolButton::menu-button { border: none; background: transparent; width: 13px; subcontrol-origin: padding; subcontrol-position: bottom right; }
    QToolButton::menu-button:hover { background: {survolFort}; border-radius: 4px; }
    QToolButton::menu-arrow { width: 8px; height: 8px; }
    QDockWidget::title { padding: 7px 10px; background: {surface}; border-bottom: 1px solid {bordure}; font-weight: 600; }
    QTabWidget::pane { border: none; border-top: 1px solid {bordure}; background: {surface}; }
    QTabBar::tab { padding: 8px 12px; border: none; border-bottom: 2px solid transparent; color: {onglet}; font-weight: 600; background: transparent; }
    QTabBar::tab:selected { color: {texte}; border-bottom: 2px solid {souligne}; }
    QScrollArea, QListWidget, QTableWidget { background: {surface}; border: none; }
    QGroupBox { font-weight: 600; border: none; border-top: 1px solid {bordure}; margin-top: 16px; padding-top: 10px; }
    QGroupBox::title { subcontrol-origin: margin; left: 0px; padding: 0 6px 0 0; color: {texte}; }
    QPushButton { padding: 6px 14px; border: 1px solid {bordure}; border-radius: 6px; background: {surface}; }
    QPushButton:hover { background: {survolBouton}; }
    QPushButton#principal { background: {principal}; color: white; border-color: {marque}; }
    QLineEdit { padding: 4px 6px; border: 1px solid {bordure}; border-radius: 5px; background: {champ}; selection-background-color: {marque}; }
    QLineEdit:focus { border-color: {focus}; }
    QStatusBar { background: {surface}; border-top: 1px solid {bordure}; }
    QSplitter::handle { background: {bordure}; }
    QHeaderView::section { background: {entete}; border: none; border-bottom: 1px solid {bordure}; padding: 5px; font-weight: 600; }
  )");
  if (!clair) {
    s += QStringLiteral(R"(
    QToolTip { color: {texteInfobulle}; background: {infobulle}; border: 1px solid {bordure}; padding: 4px 6px; }
    QPushButton { color: {texte}; }
    QPushButton:disabled { color: {desactive}; }
    QComboBox, QSpinBox, QDoubleSpinBox { border: 1px solid {bordure}; border-radius: 5px; padding: 3px 6px; background: {champ}; color: {texte}; }
    QComboBox QAbstractItemView { background: {surface}; border: 1px solid {bordure}; selection-background-color: {choisi}; selection-color: {texte}; }
    )");
  }
  if (id == IdTheme::Promethee)
    s += QStringLiteral(R"(
    QPushButton#principal { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #3563E9, stop:1 #8868F8); border: none; }
    QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #38E8F8, stop:0.5 #3563E9, stop:1 #8868F8); }
    )");
  if (id == IdTheme::ContrasteEleve)
    s += QStringLiteral(R"(
    QPushButton:focus, QToolButton:focus, QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QTreeView:focus, QListView:focus { border: 2px solid #FFD400; }
    QTabBar::tab:selected { color: #FFD400; }
    QMenu::item:selected, QMenuBar::item:selected { background: #FFD400; color: #000000; }
    )");
  const std::pair<const char*, QColor> jetons[] = {
      {"{surface}", c.surface},
      {"{bordure}", c.bordure},
      {"{survol}", c.survol},
      {"{texte}", c.texte},
      {"{desactive}", c.texteDesactive},
      {"{coche}", clair ? h(0xE7E4FF) : c.choisi},
      {"{survolFort}", clair ? h(0xE3E6F1) : c.survol.lighter(118)},
      {"{onglet}", clair ? h(0x565E7D) : c.texteSecondaire},
      {"{souligne}", clair ? h(0x5B4BF5) : c.accentTexte},
      {"{survolBouton}", clair ? h(0xF1F3FA) : c.survol},
      {"{principal}", clair ? h(0x5B4BF5) : c.accent},
      {"{marque}", clair ? h(0x5B4BF5) : c.accent},
      {"{champ}", c.champ},
      {"{focus}", clair ? h(0x5B4BF5) : c.accentTexte},
      {"{entete}", clair ? h(0xF1F3FA) : c.survol},
      {"{infobulle}", c.infobulle},
      {"{texteInfobulle}", c.texteInfobulle},
      {"{choisi}", c.choisi},
  };
  for (const auto& [jeton, couleur] : jetons) s.replace(QLatin1String(jeton), nomCss(couleur));
  return s;
}

#ifdef Q_OS_WIN
// Barre de titre native sombre (fenêtres et boîtes de dialogue), Windows 10 20H1 et plus récents
void titreSombre(QWidget* w, bool sombre) {
  using Fonction = long(__stdcall*)(HWND, DWORD, const void*, DWORD);
  static const Fonction f = [] {
    const HMODULE m = LoadLibraryW(L"dwmapi.dll");
    return m ? reinterpret_cast<Fonction>(reinterpret_cast<void*>(GetProcAddress(m, "DwmSetWindowAttribute"))) : nullptr;
  }();
  if (!f) return;
  const BOOL v = sombre ? TRUE : FALSE;
  const HWND fenetre = reinterpret_cast<HWND>(w->winId());
  if (f(fenetre, 20, &v, sizeof v) != 0) f(fenetre, 19, &v, sizeof v);
}
#endif

const char* const ORIGINE_CSS = "_prometheeCssOrigine";
const char* const TRADUIT_CSS = "_prometheeCss";
const char* const ORIGINE_TEXTE = "_prometheeTexteOrigine";
const char* const TRADUIT_TEXTE = "_prometheeTexte";

bool porteCouleur(const QString& t) {
  return t.contains(QLatin1Char('#')) || t.contains(QLatin1String("rgb")) || t.contains(QLatin1String("white")) || t.contains(QLatin1String("black"));
}
}  // namespace

// Filtre d'événements de l'application : traduit les feuilles de style des widgets et les textes riches des
// étiquettes au moment où ils changent, en gardant l'original pour le prochain changement de thème.
class FiltreTheme : public QObject {
public:
  using QObject::QObject;
  void traduireFeuille(QWidget* w) const {
    const QString actuelle = w->styleSheet();
    if (actuelle.isEmpty() || w->property("themeFixe").toBool()) return;
    if (w->property(TRADUIT_CSS).toString() == actuelle) return;  // c'est notre traduction
    const QString t = Theme::instance().css(actuelle);
    w->setProperty(ORIGINE_CSS, actuelle);
    w->setProperty(TRADUIT_CSS, t);
    if (t != actuelle) w->setStyleSheet(t);
  }
  void traduireTexte(QLabel* l) const {
    const QString actuel = l->text();
    // Un texte écrit avec les couleurs du thème (inspecteur électronique, P15) n'est pas retraduit
    if (!porteCouleur(actuel) || l->property("themeFixe").toBool()) return;
    if (l->property(TRADUIT_TEXTE).toString() == actuel) return;
    const QString t = Theme::instance().html(actuel);
    l->setProperty(ORIGINE_TEXTE, actuel);
    l->setProperty(TRADUIT_TEXTE, t);
    if (t != actuel) l->setText(t);
  }

protected:
  bool eventFilter(QObject* o, QEvent* e) override {
    if (!o->isWidgetType()) return false;
    const QEvent::Type type = e->type();
    if (type != QEvent::Paint && type != QEvent::Polish && type != QEvent::StyleChange && type != QEvent::Show) return false;
    auto* w = static_cast<QWidget*>(o);
    const Theme& th = Theme::instance();
    if (type == QEvent::Polish) {
      if (auto* v = qobject_cast<QAbstractItemView*>(w)) {
        QAbstractItemDelegate* d = v->itemDelegate();
        if (d && d->metaObject() == &QStyledItemDelegate::staticMetaObject) {
          v->setItemDelegate(new DelegueTheme(v));
          if (d->parent() == v) d->deleteLater();
        }
      }
    }
#ifdef Q_OS_WIN
    if (type == QEvent::Show && w->isWindow()) {
      const Qt::WindowType genre = w->windowType();
      if (genre == Qt::Window || genre == Qt::Dialog) titreSombre(w, th.c().sombre);
    }
#endif
    if (!th.traduit()) return false;
    if (type == QEvent::Paint) {
      if (auto* l = qobject_cast<QLabel*>(w)) traduireTexte(l);
    } else if (type == QEvent::Polish || type == QEvent::StyleChange) {
      traduireFeuille(w);
      if (auto* l = qobject_cast<QLabel*>(w)) traduireTexte(l);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------------------------------------
Theme& Theme::instance() {
  static Theme t;
  return t;
}

QString Theme::nom(IdTheme id) {
  switch (id) {
    case IdTheme::Promethee: return QStringLiteral("Prométhée (recommandé)");
    case IdTheme::Clair: return QStringLiteral("Clair");
    case IdTheme::Sombre: return QStringLiteral("Sombre");
    case IdTheme::ContrasteEleve: return QStringLiteral("Contraste élevé");
    case IdTheme::Systeme: return QStringLiteral("Selon le système");
  }
  return {};
}

QString Theme::description(IdTheme id) {
  switch (id) {
    case IdTheme::Promethee:
      return QStringLiteral("Interface bleu nuit aux couleurs du logo, schémas, esquisses et plans sur papier clair, comme imprimés : moins d’éblouissement "
                            "autour de la zone de travail pendant les longues séances.");
    case IdTheme::Clair: return QStringLiteral("Fonds clairs bleutés, encre marine.");
    case IdTheme::Sombre: return QStringLiteral("Tout en sombre, documents compris : pour travailler dans la pénombre.");
    case IdTheme::ContrasteEleve: return QStringLiteral("Noir et blanc, couleurs vives, repères épais : pour une vue fatiguée ou un écran en plein soleil.");
    case IdTheme::Systeme: return QStringLiteral("Clair ou sombre, comme le système, et suit ses changements.");
  }
  return {};
}

QString Theme::cle(IdTheme id) {
  switch (id) {
    case IdTheme::Promethee: return QStringLiteral("promethee");
    case IdTheme::Clair: return QStringLiteral("clair");
    case IdTheme::Sombre: return QStringLiteral("sombre");
    case IdTheme::ContrasteEleve: return QStringLiteral("contraste");
    case IdTheme::Systeme: return QStringLiteral("systeme");
  }
  return {};
}

IdTheme Theme::depuisCle(const QString& c, IdTheme parDefaut) {
  for (IdTheme id : TOUS)
    if (cle(id) == c.trimmed().toLower()) return id;
  return parDefaut;
}

void Theme::demarrer(QApplication& app) {
  m_app = &app;
  m_paletteSysteme = QGuiApplication::palette();
  const QByteArray impose = qgetenv("PROMETHEE_THEME");
  if (!impose.isEmpty()) {
    m_choix = depuisCle(QString::fromLatin1(impose));
    m_impose = true;
  } else {
    m_choix = depuisCle(QSettings().value(QStringLiteral("apparence/theme")).toString());
  }
  app.installEventFilter(new FiltreTheme(&app));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
    if (m_choix == IdTheme::Systeme) appliquer();
  });
#endif
  appliquer();
}

void Theme::choisir(IdTheme id) {
  m_choix = id;
  if (!m_impose) QSettings().setValue(QStringLiteral("apparence/theme"), cle(id));
  appliquer();
}

bool Theme::systemeSombre() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  const Qt::ColorScheme s = QGuiApplication::styleHints()->colorScheme();
  if (s != Qt::ColorScheme::Unknown) return s == Qt::ColorScheme::Dark;
#endif
#ifdef Q_OS_WIN
  const QSettings r(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"), QSettings::NativeFormat);
  return r.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
#else
  return m_paletteSysteme.color(QPalette::Window).lightness() < 128;
#endif
}

void Theme::appliquer() {
  m_effectif = m_choix == IdTheme::Systeme ? (systemeSombre() ? IdTheme::Sombre : IdTheme::Clair) : m_choix;
  m_c = interfaceDe(m_effectif);
  m_schema = schemaDe(m_effectif);
  m_schemaClair = schemaClair();
  m_signesClair = signesClairs();
  m_signesSchema = m_effectif == IdTheme::ContrasteEleve ? signesContraste() : m_effectif == IdTheme::Sombre ? signesSombres() : signesClairs();
  m_signesCircuit = m_effectif == IdTheme::ContrasteEleve ? signesContraste() : signesSombres();
  m_esquisse = esquisseDe(m_effectif);
  m_esquisseSur3D = esquisseDe(m_effectif == IdTheme::Clair || m_effectif == IdTheme::ContrasteEleve ? m_effectif : IdTheme::Sombre);
  m_carte = carteDe(m_effectif);
  m_circuit = circuitDe(m_effectif);
  appliquerPaletteEtJetons();  // 2.31 : palette choisie, puis jetons de l'utilisateur
  encres_circuit::prendreEncres(m_circuit);
  ++m_generation;
  if (m_app) {
    m_app->setPalette(paletteDe(m_c, m_effectif));
    m_app->setStyleSheet(feuilleGlobale(m_c, m_effectif));
    retraduireWidgets();
#ifdef Q_OS_WIN
    for (QWidget* w : QApplication::topLevelWidgets())
      if (w->isVisible()) titreSombre(w, m_c.sombre);
#endif
  }
  journal("thème : " + cle(m_effectif).toStdString() + (m_choix == IdTheme::Systeme ? " (selon le système)" : ""));
  emit change();
  if (m_app)
    for (QWidget* w : QApplication::allWidgets()) w->update();
}

// Original d'une valeur : celle que nous avons traduite, sauf si le programme l'a remplacée depuis
static QString originale(const QWidget* w, const QString& actuelle, const char* origine, const char* traduite) {
  const QVariant t = w->property(traduite);
  return t.isValid() && t.toString() == actuelle ? w->property(origine).toString() : actuelle;
}

void Theme::retraduireWidgets() {
  for (QWidget* w : QApplication::allWidgets()) {
    const QString origine = originale(w, w->styleSheet(), ORIGINE_CSS, TRADUIT_CSS);
    if (!origine.isEmpty() && !w->property("themeFixe").toBool()) {
      const QString t = this->css(origine);
      w->setProperty(ORIGINE_CSS, origine);
      w->setProperty(TRADUIT_CSS, t);
      if (w->styleSheet() != t) w->setStyleSheet(t);
    }
    if (auto* l = qobject_cast<QLabel*>(w)) {
      const QString o = originale(l, l->text(), ORIGINE_TEXTE, TRADUIT_TEXTE);
      if (porteCouleur(o)) {
        const QString t = html(o);
        l->setProperty(ORIGINE_TEXTE, o);
        l->setProperty(TRADUIT_TEXTE, t);
        if (l->text() != t) l->setText(t);
      }
    }
  }
}

const EncresSchema& Theme::schema(bool impression) const { return impression ? m_schemaClair : m_schema; }
const SignesCanevas& Theme::signesSchema(bool impression) const { return impression ? m_signesClair : m_signesSchema; }
const SignesCanevas& Theme::signesCircuit() const { return m_signesCircuit; }

QColor Theme::texte(const QColor& clair) const {
  if (!traduit() || !clair.isValid()) return clair;
  if (const auto r = exacte(rolesTexte(), m_c, clair)) return *r;
  const Regles R = regles(m_effectif);
  Lab x = lab(clair);
  const bool gris = chroma(x) < GRIS;
  if (x.L >= 0.82) return clair;  // texte clair : posé sur un fond sombre ou d'accent, il le reste
  const double t = std::clamp((x.L - 0.20) / 0.62, 0.0, 1.0);
  double L = R.texteHaut - t * (R.texteHaut - R.texteBas);
  if (gris) {
    x.a = R.teinteA;
    x.b = R.teinteB;
    if (R.contraste) L = t < 0.75 ? 1.0 : R.texteBas;
  } else {
    L = std::max(L, R.couleurMin);
  }
  x.L = L;
  return couleur(x, clair.alpha());
}

QColor Theme::fond(const QColor& clair) const {
  if (!traduit() || !clair.isValid()) return clair;
  if (const auto r = exacte(rolesFond(), m_c, clair)) return *r;
  const Regles R = regles(m_effectif);
  Lab x = lab(clair);
  if (x.L < 0.80) return clair;  // fonds déjà sombres, couleurs franches (boutons d'accent) : inchangés
  if (chroma(x) < GRIS) {
    x.a = R.teinteA;
    x.b = R.teinteB;
  } else {
    x.a *= 0.85;
    x.b *= 0.85;
  }
  x.L = R.fondBlanc + (1.0 - x.L) * R.fondPente;
  return couleur(x, clair.alpha());
}

QColor Theme::bordure(const QColor& clair) const {
  if (!traduit() || !clair.isValid()) return clair;
  if (const auto r = exacte(rolesBordure(), m_c, clair)) return *r;
  const Regles R = regles(m_effectif);
  Lab x = lab(clair);
  if (x.L < 0.70) return clair;  // filets d'accent ou déjà sombres
  const bool gris = chroma(x) < GRIS;
  double L = R.bordure + (0.88 - x.L) * 0.6;
  if (R.contraste) L = gris ? 0.80 : 0.78;
  if (gris) {
    x.a = R.teinteA;
    x.b = R.teinteB;
  } else {
    x.a *= 0.9;
    x.b *= 0.9;
  }
  x.L = L;
  return couleur(x, clair.alpha());
}

QColor Theme::pourFondSombre(const QColor& c) {
  if (!c.isValid()) return c;
  Lab x = lab(c);
  if (x.L >= 0.62) return c;
  x.L = std::max(0.70, 1.02 - x.L * 0.55);
  return couleur(x, c.alpha());
}

QColor Theme::encreDocument(const QColor& c) const { return m_c.documentsSombres ? pourFondSombre(c) : c; }

QRgb Theme::pixel(QRgb p) const {
  const int alpha = qAlpha(p);
  if (!traduit() || alpha == 0) return p;
  const Regles R = regles(m_effectif);
  Lab x = lab(QColor(qRed(p), qGreen(p), qBlue(p)));
  if (chroma(x) < 0.04) {
    // Gris : la clarté s'inverse dans la plage du thème (traits d'encre clairs, remplissages sombres)
    const double t = std::clamp((x.L - 0.20) / 0.80, 0.0, 1.0);
    x.L = R.contraste ? (t < 0.55 ? 1.0 : 0.10) : R.texteHaut + t * (R.fondBlanc + 0.10 - R.texteHaut);
    x.a = R.teinteA;
    x.b = R.teinteB;
  } else if (x.L > 0.85) {
    x.L = R.fondBlanc + 0.10 + (1.0 - x.L) * 1.2;  // pastel (fond bleuté) : remplissage sombre teinté
    x.a *= 0.8;
    x.b *= 0.8;
  } else {
    x.L = std::max(x.L, R.contraste ? 0.80 : 0.64);  // couleur franche : même teinte, assez claire
  }
  return couleur(x, alpha).rgba();
}

QString Theme::css(const QString& feuille) const {
  if (!traduit() || feuille.isEmpty()) return feuille;
  static const QRegularExpression declaration(QStringLiteral(R"(([A-Za-z-]+)(\s*:\s*)([^;{}"'<>]*))"));
  static const QRegularExpression valeurCouleur(QStringLiteral(R"(#[0-9A-Fa-f]{6}\b|#[0-9A-Fa-f]{3}\b|rgba?\(\s*\d+\s*,\s*\d+\s*,\s*\d+\s*(?:,\s*\d+(?:\.\d+)?\s*)?\)|\bwhite\b|\bblack\b)"));
  enum class Usage { Aucun, Texte, Fond, Bordure };
  QString r;
  r.reserve(feuille.size());
  qsizetype fin = 0;
  auto it = declaration.globalMatch(feuille);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    const QString propriete = m.captured(1).toLower();
    Usage u = Usage::Aucun;
    if (propriete == QLatin1String("color") || propriete == QLatin1String("selection-color")) u = Usage::Texte;
    else if (propriete.contains(QLatin1String("background"))) u = Usage::Fond;
    else if (propriete.startsWith(QLatin1String("border")) || propriete.startsWith(QLatin1String("outline")) || propriete == QLatin1String("gridline-color")) u = Usage::Bordure;
    if (u == Usage::Aucun) continue;
    const QString valeur = m.captured(3);
    QString nouvelle;
    qsizetype k = 0;
    auto jt = valeurCouleur.globalMatch(valeur);
    while (jt.hasNext()) {
      const QRegularExpressionMatch c = jt.next();
      QString jeton = c.captured(0);
      QColor q;
      if (jeton.startsWith(QLatin1String("rgb"))) {
        const QStringList n = jeton.mid(jeton.indexOf(QLatin1Char('(')) + 1).chopped(1).split(QLatin1Char(','));
        q = QColor(n.value(0).trimmed().toInt(), n.value(1).trimmed().toInt(), n.value(2).trimmed().toInt());
        if (n.size() == 4) {
          const double a = n.value(3).trimmed().toDouble();
          q.setAlpha(n.value(3).contains(QLatin1Char('.')) ? static_cast<int>(std::lround(a * 255)) : static_cast<int>(a));
        }
      } else {
        q = QColor(jeton);
      }
      if (!q.isValid()) continue;
      const QColor t = u == Usage::Texte ? texte(q) : u == Usage::Fond ? fond(q) : bordure(q);
      nouvelle += valeur.mid(k, c.capturedStart(0) - k) + nomCss(t);
      k = c.capturedEnd(0);
    }
    if (k == 0) continue;
    nouvelle += valeur.mid(k);
    r += feuille.mid(fin, m.capturedStart(3) - fin) + nouvelle;
    fin = m.capturedEnd(3);
  }
  r += feuille.mid(fin);
  return r;
}

QString Theme::html(const QString& texte) const {
  if (!traduit() || texte.isEmpty()) return texte;
  QString r = css(texte);  // attributs style='…'
  static const QRegularExpression attribut(QStringLiteral(R"(\b(color|bgcolor)\s*=\s*(['"]?)(#[0-9A-Fa-f]{6}|[A-Za-z]+)\2)"));
  QString sortie;
  qsizetype fin = 0;
  auto it = attribut.globalMatch(r);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    const QColor q(m.captured(3));
    if (!q.isValid()) continue;
    const QColor t = m.captured(1).toLower() == QLatin1String("bgcolor") ? fond(q) : this->texte(q);
    sortie += r.mid(fin, m.capturedStart(3) - fin) + nomCss(t);
    fin = m.capturedEnd(3);
  }
  sortie += r.mid(fin);
  return sortie;
}

QMenu* Theme::menu(QWidget* parent) {
  auto* m = new QMenu(QStringLiteral("&Thème"), parent);
  m->setToolTipsVisible(true);
  auto* groupe = new QActionGroup(m);
  for (IdTheme id : TOUS) {
    QAction* a = m->addAction(nom(id));
    a->setCheckable(true);
    a->setChecked(id == m_choix);
    a->setToolTip(description(id));
    a->setStatusTip(description(id));
    a->setData(static_cast<int>(id));
    a->setObjectName(QStringLiteral("theme-") + cle(id));
    groupe->addAction(a);
    connect(a, &QAction::triggered, this, [id] { Theme::instance().choisir(id); });
  }
  connect(this, &Theme::change, m, [m] {
    for (QAction* a : m->actions()) a->setChecked(a->data().toInt() == static_cast<int>(Theme::instance().choix()));
  });
  return m;
}

void DelegueTheme::initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const {
  QStyledItemDelegate::initStyleOption(option, index);
  const Theme& th = Theme::instance();
  if (!th.traduit()) return;
  const QVariant avant = index.data(Qt::ForegroundRole);
  if (avant.isValid()) {
    const QColor t = th.texte(avant.value<QBrush>().color());
    option->palette.setColor(QPalette::Text, t);
    option->palette.setColor(QPalette::WindowText, t);
  }
  const QVariant arriere = index.data(Qt::BackgroundRole);
  if (arriere.isValid()) option->backgroundBrush = QBrush(th.fond(arriere.value<QBrush>().color()));
}

double contraste(const QColor& a, const QColor& b) {
  auto luminance = [](const QColor& c) {
    auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
  };
  const double la = luminance(a), lb = luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

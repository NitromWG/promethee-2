// Prométhée : lien électronique-mécanique. Le circuit imprimé devient la carte du projet (contour, épaisseur,
// trous de fixation, corps des composants) : le boîtier dérivé suit la carte conçue. Ce qui vient de la carte
// (position, encombrement, hauteur) est mis à jour ; ce qui se règle côté boîtier (fixation d'un trou, vis,
// perçage de couvercle, découpe de paroi) est gardé par identifiant de composant (EL-10). Licence GPL-3.0-only.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numbers>

#include "promethee/modele.hpp"

namespace prom {

namespace {

std::string minuscules(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}
bool contient(const std::string& n, std::initializer_list<const char*> mots) {
  return std::any_of(mots.begin(), mots.end(), [&](const char* m) { return n.find(m) != std::string::npos; });
}

// Rôle d'une empreinte pour le boîtier, d'après son identifiant (mêmes familles que le pont KiCad)
struct Role {
  std::string type = "generique";
  bool bordPossible = false;  // connecteur qui perce la paroi s'il est au bord
  double couvercle = 0;       // perçage du couvercle (0 : aucun)
};
Role role(const std::string& empreinte) {
  const std::string n = minuscules(empreinte);
  Role r;
  if (contient(n, {"usb_c", "usb-c", "type-c", "typec"})) { r.type = "usbc"; r.bordPossible = true; }
  else if (contient(n, {"barreljack", "barrel_jack", "dc_jack"})) { r.type = "jack"; r.bordPossible = true; }
  else if (contient(n, {"pinheader", "pin_header", "terminalblock", "terminal_block", "usb_micro", "usb_mini", "usb_a", "usb_b", "rj45", "rj11", "jack_3.5", "audio_jack", "hdmi",
                        "dsub", "sd_card", "microsd", "sma_", "bnc", "jst"})) {
    r.type = contient(n, {"jst"}) ? "jst" : "connecteur";
    r.bordPossible = true;
  } else if (n.rfind("led_", 0) == 0) {
    r.type = "led";
    // LED traversante : perçage à son diamètre ; CMS : fenêtre de 2 mm pour un guide de lumière
    r.couvercle = contient(n, {"led_d5"}) ? 5.4 : contient(n, {"led_d3"}) ? 3.4 : 2.0;
  } else if (contient(n, {"sw_push", "tactile"})) {
    r.type = "bouton";
    r.couvercle = 4.2;
  } else if (contient(n, {"cp_radial", "c_radial", "cp_elec"})) r.type = "condo";
  else if (contient(n, {"esp32", "rf_module"})) r.type = "module";
  return r;
}

}  // namespace

std::vector<std::string> appliquerCarteElectronique(Projet& p, std::size_t indice) {
  std::vector<std::string> alertes;
  if (indice >= p.electronique.size()) return alertes;
  const ecad::ConceptionElec& c = p.electronique[indice];
  const ecad::Circuit& k = c.circuit;
  // Repère mécanique : y vers le haut ; x0, y0 : centre de la carte
  // Contour libre, ou découpes internes (EL-44) : la carte du boîtier reçoit le contour et ses découpes (le
  // premier contour est l'extérieur, les suivants sont fraisés de part en part)
  if (k.contour.libre.size() >= 3 || !k.contour.decoupes.empty()) {
    auto versContour = [](const ecad::Polygone& L) {
      Contour ct;
      ct.ferme = true;
      for (std::size_t j = 0; j < L.size(); ++j) {
        const auto& a = L[j];
        const auto& b = L[(j + 1) % L.size()];
        ct.elements.push_back({{ecad::mm(a.x), -ecad::mm(a.y)}, {ecad::mm(b.x), -ecad::mm(b.y)}, std::nullopt});
      }
      return ct;
    };
    p.carte.contours = {versContour(k.contour.polygone())};
    for (const auto& d : k.contour.decoupes)
      if (d.size() >= 3) p.carte.contours.push_back(versContour(d));
    orienterContours(p.carte.contours);
    recalerEncombrement(p.carte);
  } else {
    p.carte.contours.clear();
    p.carte.L = ecad::mm(k.contour.largeur);
    p.carte.W = ecad::mm(k.contour.hauteur);
    p.carte.r = ecad::mm(k.contour.rayon);
    p.carte.x0 = ecad::mm(k.contour.origine.x) + p.carte.L / 2;
    p.carte.y0 = -(ecad::mm(k.contour.origine.y) + p.carte.W / 2);
  }
  p.carte.t = k.empilage.epaisseur;
  // Réglages faits côté boîtier, gardés par identifiant
  std::map<std::string, Trou> anciensTrous;
  for (const auto& t : p.trous) anciensTrous[t.id] = t;
  std::map<std::string, Composant> anciens;
  for (const auto& m : p.composants) anciens[m.id] = m;
  p.trous.clear();
  p.composants.clear();
  for (const auto& e : k.empreintes) {
    const ecad::ComposantElec* x = c.composant(e.composant);
    if (x && x->horsCarte) continue;
    const ecad::DefEmpreinte* d = c.empreinte(e.empreinte);
    const double X = ecad::mm(e.position.x), Y = -ecad::mm(e.position.y);
    if (e.empreinte.rfind("MountingHole", 0) == 0) {
      Trou t;
      if (const auto it = anciensTrous.find(e.composant); it != anciensTrous.end()) t = it->second;  // fixation, vis, dimensions
      t.id = e.composant;
      t.ref = x ? x->reference : std::string();
      t.ancrage = "libre";
      t.coin.reset();
      t.x = X;
      t.y = Y;
      if (d && !d->pastilles.empty()) t.diamTrou = ecad::mm(d->pastilles.front().percage);
      p.trous.push_back(t);
      continue;
    }
    // Un corps dès qu'une hauteur est connue, saisie ou générique (T2) : une empreinte sans corps prend son encombrement
    if (!d || ecad::hauteurCorpsMm(x, *d) <= 0) continue;
    const Role r = role(e.empreinte);
    Composant m;
    const auto ancien = anciens.find(e.composant);
    if (ancien != anciens.end()) m = ancien->second;  // perçage de couvercle, découpe, ancrage réglés côté boîtier
    m.id = e.composant;
    m.ref = x ? x->reference : std::string();
    m.valeur = x ? x->valeur : std::string();
    if (ancien == anciens.end() || ancien->second.type == "generique") m.type = typeComposant(r.type) ? r.type : "generique";
    // Encombrement du corps réellement tourné : le modèle mécanique ne connaît que les quarts de tour
    const ecad::Boite emprise = ecad::empriseCorps(*d);
    const double w0 = ecad::mm(emprise.largeur()), d0 = ecad::mm(emprise.hauteur());
    const double rotation = std::fmod(std::fmod(e.rotation, 360.0) + 360.0, 360.0);
    const bool quart = std::abs(std::remainder(rotation, 90.0)) < 0.5;
    if (quart) {
      m.w = w0;
      m.d = d0;
      m.rot = (static_cast<int>(std::lround(rotation / 90.0)) * 90) % 360;
    } else {
      const double a = rotation * std::numbers::pi / 180;
      m.w = std::abs(w0 * std::cos(a)) + std::abs(d0 * std::sin(a));
      m.d = std::abs(w0 * std::sin(a)) + std::abs(d0 * std::cos(a));
      m.rot = 0;
      alertes.push_back((m.ref.empty() ? std::string("Un composant") : m.ref) + " est tourné de " + std::to_string(static_cast<int>(std::lround(rotation))) +
                        "° : le boîtier réserve l'encombrement de son corps tourné (" + std::to_string(static_cast<int>(std::lround(m.w))) + " × " +
                        std::to_string(static_cast<int>(std::lround(m.d))) + " mm).");
    }
    m.h = ecad::hauteurCorpsMm(x, *d);  // la même hauteur que la 3D de l'atelier (P4)
    m.sourceHauteur = x && x->hauteurVerifiee ? "verifiee" : "nominale";
    m.dessous = e.dessous;
    m.origine = "pcb";
    m.verrou = true;
    m.x = X;
    m.y = Y;
    m.bord.reset();
    // Connecteur au bord de la carte : il perce la paroi
    if (r.bordPossible && !e.dessous) {
      const double dE = p.carte.x0 + p.carte.L / 2 - (X + m.w / 2), dW = (X - m.w / 2) - (p.carte.x0 - p.carte.L / 2);
      const double dN = p.carte.y0 + p.carte.W / 2 - (Y + m.d / 2), dS = (Y - m.d / 2) - (p.carte.y0 - p.carte.W / 2);
      if (std::min({dE, dW, dN, dS}) < 1.5) {
        const double wCorps = m.w, dCorps = m.d;
        placerSurBord(p, m, X, Y);
        const bool hz = horizontal(*m.bord);
        m.w = hz ? wCorps : dCorps;  // largeur le long du bord
        m.d = hz ? dCorps : wCorps;
        if (ancien == anciens.end() || !ancien->second.bord || m.decoupe.w <= 0) {
          const TypeComposant* T = typeComposant(m.type);
          m.decoupe = T && T->decoupe.w > 0 && m.type != "connecteur" ? T->decoupe : Decoupe{std::max(3.0, m.w + 1), m.h + 1, 1};
          m.zc = T && T->zc > 0 && m.type != "connecteur" ? T->zc : m.h / 2;
        }
      }
    }
    if (r.couvercle > 0 && !e.dessous && !m.trou_couvercle) m.trou_couvercle = r.couvercle;
    p.composants.push_back(m);
  }
  return alertes;
}

}  // namespace prom

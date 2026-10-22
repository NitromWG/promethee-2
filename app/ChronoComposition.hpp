// Prométhée : part du temps passée à poser les images à l'écran (essais de temps, ERG-10, 9.1). Quand il est actif,
// le gestionnaire d'événements de l'application chronomètre chaque demande d'image (QEvent::UpdateRequest, que
// QWidget::repaint envoie aussi) et les peintures qu'elle contient : la différence est la pose de l'image par la
// pile graphique (copie du tampon, composition OpenGL de la fenêtre), qui sous un rendu logiciel (Mesa llvmpipe,
// sans carte graphique) dépasse à elle seule les objectifs. Les essais comparent les objectifs au travail de
// Prométhée (événements traités, widgets peints), comme le lot L9 le fait pour la vue du circuit.
// Licence GPL-3.0-only.
#pragma once

#include <chrono>

namespace composition {

struct Etat {
  bool actif = false;
  int profondeur = 0;          // demandes d'image en cours (imbriquées)
  double requetes = 0;         // ms dans les demandes d'image, peinture comprise
  double peintures = 0;        // ms dans les peintures faites pendant ces demandes
};

inline Etat& etat() {
  static Etat e;
  return e;
}

inline void activer(bool oui) { etat().actif = oui; }
inline bool actif() { return etat().actif; }
// ms passées à poser les images depuis l'activation (demandes d'image moins leurs peintures)
inline double cumul() { return etat().requetes - etat().peintures; }

// Chronomètre d'un événement d'image : demande (requete) ou peinture
class Mesure {
 public:
  explicit Mesure(bool requete) : m_requete(requete), m_debut(std::chrono::steady_clock::now()) {
    if (m_requete) ++etat().profondeur;
  }
  ~Mesure() {
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_debut).count();
    Etat& e = etat();
    if (m_requete) {
      --e.profondeur;
      if (e.profondeur == 0) e.requetes += ms;
    } else if (e.profondeur > 0) {
      e.peintures += ms;
    }
  }
  Mesure(const Mesure&) = delete;
  Mesure& operator=(const Mesure&) = delete;

 private:
  bool m_requete;
  std::chrono::steady_clock::time_point m_debut;
};

}  // namespace composition

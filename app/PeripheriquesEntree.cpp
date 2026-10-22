// Prométhée : souris 3D (lot L2, tâche 2.29, ERG-31). Licence GPL-3.0-only.
#include "PeripheriquesEntree.hpp"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QLibrary>
#include <QPointF>
#include <QSocketNotifier>
#include <QWidget>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
#if defined(Q_OS_LINUX)
// libspnav : l'événement tel que la bibliothèque le rend (spnav.h), déclaré ici pour ne pas en dépendre à la compilation
struct EvenementSpnav {
  int type;  // 1 : mouvement, 2 : bouton
  union {
    struct {
      int x, y, z, rx, ry, rz;
      unsigned int period;
      int* data;
    } mouvement;
    struct {
      int press, bnum;
    } bouton;
  };
};
using FnOuvrir = int (*)();
using FnFd = int (*)();
using FnLire = int (*)(EvenementSpnav*);
struct Spnav {
  QLibrary lib{QStringLiteral("spnav")};
  FnOuvrir ouvrir = nullptr;
  FnFd fd = nullptr;
  FnLire lire = nullptr;
};
#endif
constexpr double PLEINE_ECHELLE = 350.0;  // valeur brute d'un axe poussé à fond
}  // namespace

#ifdef Q_OS_WIN
// Entrée brute de Windows : rapports HID de l'appareil multi-axes (1 : translation, 2 : rotation, 3 : boutons)
class FiltreEntreeBrute : public QAbstractNativeEventFilter {
 public:
  bool nativeEventFilter(const QByteArray& type, void* message, qintptr*) override {
    if (type != "windows_generic_MSG") return false;
    const MSG* m = static_cast<const MSG*>(message);
    if (m->message != WM_INPUT) return false;
    UINT taille = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(m->lParam), RID_INPUT, nullptr, &taille, sizeof(RAWINPUTHEADER));
    if (taille == 0 || taille > 1024) return false;
    alignas(RAWINPUT) BYTE tampon[1024];
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(m->lParam), RID_INPUT, tampon, &taille, sizeof(RAWINPUTHEADER)) != taille) return false;
    const RAWINPUT* r = reinterpret_cast<const RAWINPUT*>(tampon);
    if (r->header.dwType != RIM_TYPEHID || r->data.hid.dwSizeHid < 7) return false;
    const BYTE* d = r->data.hid.bRawData;
    auto axe = [d](int i) { return static_cast<double>(static_cast<short>(d[1 + 2 * i] | (d[2 + 2 * i] << 8))) / PLEINE_ECHELLE; };
    SourisEspace& s = SourisEspace::instance();
    if (d[0] == 1) {
      m_t[0] = axe(0);
      m_t[1] = axe(1);
      m_t[2] = axe(2);
      if (r->data.hid.dwSizeHid >= 13) {  // translation et rotation dans le même rapport
        m_r[0] = static_cast<double>(static_cast<short>(d[7] | (d[8] << 8))) / PLEINE_ECHELLE;
        m_r[1] = static_cast<double>(static_cast<short>(d[9] | (d[10] << 8))) / PLEINE_ECHELLE;
        m_r[2] = static_cast<double>(static_cast<short>(d[11] | (d[12] << 8))) / PLEINE_ECHELLE;
      }
      emit s.mouvement(m_t[0], -m_t[1], -m_t[2], m_r[0], m_r[1], m_r[2]);
    } else if (d[0] == 2) {
      m_r[0] = axe(0);
      m_r[1] = axe(1);
      m_r[2] = axe(2);
      emit s.mouvement(m_t[0], -m_t[1], -m_t[2], m_r[0], m_r[1], m_r[2]);
    } else if (d[0] == 3) {
      const unsigned masque = d[1] | (d[2] << 8);
      for (int b = 0; b < 16; ++b)
        if (((masque ^ m_boutons) >> b) & 1u) emit s.bouton(b + 1, (masque >> b) & 1u);
      m_boutons = masque;
    }
    return false;
  }

 private:
  double m_t[3]{}, m_r[3]{};
  unsigned m_boutons = 0;
};
#endif

SourisEspace& SourisEspace::instance() {
  static SourisEspace* s = new SourisEspace;
  return *s;
}

void SourisEspace::demarrer(QWidget* fenetre) {
  if (m_demarre) return;
  m_demarre = true;
  Q_UNUSED(fenetre);
#if defined(Q_OS_LINUX)
  auto* sp = new Spnav;
  if (!sp->lib.load()) {
    m_description = QStringLiteral("libspnav absente : installe spacenavd et libspnav pour la souris 3D");
    delete sp;
    return;
  }
  sp->ouvrir = reinterpret_cast<FnOuvrir>(sp->lib.resolve("spnav_open"));
  sp->fd = reinterpret_cast<FnFd>(sp->lib.resolve("spnav_fd"));
  sp->lire = reinterpret_cast<FnLire>(sp->lib.resolve("spnav_poll_event"));
  if (!sp->ouvrir || !sp->fd || !sp->lire || sp->ouvrir() == -1) {
    m_description = QStringLiteral("libspnav présente, mais le service spacenavd ne répond pas");
    delete sp;
    return;
  }
  m_bibliotheque = sp;
  m_disponible = true;
  m_description = QStringLiteral("souris 3D par libspnav");
  auto* n = new QSocketNotifier(sp->fd(), QSocketNotifier::Read, this);
  connect(n, &QSocketNotifier::activated, this, [this] { lireLinux(); });
#elif defined(Q_OS_WIN)
  // L'appareil multi-axes (page HID générique, usage 8), dont les rapports arrivent à la fenêtre principale
  RAWINPUTDEVICE d{};
  d.usUsagePage = 0x01;
  d.usUsage = 0x08;
  d.dwFlags = 0;
  d.hwndTarget = fenetre ? reinterpret_cast<HWND>(fenetre->winId()) : nullptr;
  if (!RegisterRawInputDevices(&d, 1, sizeof(d))) {
    m_description = QStringLiteral("entrée brute de Windows refusée pour la souris 3D");
    return;
  }
  QCoreApplication::instance()->installNativeEventFilter(new FiltreEntreeBrute);
  m_disponible = true;
  m_description = QStringLiteral("souris 3D par l’entrée brute de Windows");
#elif defined(Q_OS_MACOS)
  // Le cadre du pilote 3Dconnexion, s'il est installé ; ses rappels demandent l'enregistrement d'un client
  auto* lib = new QLibrary(QStringLiteral("/Library/Frameworks/3DconnexionClient.framework/3DconnexionClient"));
  if (!lib->load()) {
    m_description = QStringLiteral("pilote 3Dconnexion absent : installe 3DxWare pour la souris 3D");
    delete lib;
    return;
  }
  m_bibliotheque = lib;
  m_disponible = lib->resolve("SetConnexionHandlers") != nullptr && lib->resolve("RegisterConnexionClient") != nullptr;
  m_description = m_disponible ? QStringLiteral("pilote 3Dconnexion trouvé") : QStringLiteral("pilote 3Dconnexion trop ancien");
#else
  m_description = QStringLiteral("souris 3D non prise en charge sur ce système");
#endif
}

void SourisEspace::lireLinux() {
#if defined(Q_OS_LINUX)
  auto* sp = static_cast<Spnav*>(m_bibliotheque);
  if (!sp) return;
  EvenementSpnav e{};
  while (sp->lire(&e) > 0) {
    if (e.type == 1) {
      auto n = [](int v) { return std::clamp(v / PLEINE_ECHELLE, -1.0, 1.0); };
      emit mouvement(n(e.mouvement.x), n(e.mouvement.z), n(e.mouvement.y), n(e.mouvement.rx), n(e.mouvement.rz), n(e.mouvement.ry));
    } else if (e.type == 2) {
      emit bouton(e.bouton.bnum + 1, e.bouton.press != 0);
    }
  }
#endif
}

void SourisEspace::appliquer2D(QWidget* vue, QPointF& origine, double& echelle, double tx, double ty, double tz, double min, double max) {
  // Une petite poussée reste sans effet : la main posée sur l'appareil ne fait pas dériver la vue
  auto zone = [](double v) { return std::abs(v) < 0.04 ? 0.0 : v; };
  tx = zone(tx);
  ty = zone(ty);
  tz = zone(tz);
  const double vitesse = 18.0;  // pixels par événement à fond
  origine -= QPointF(tx * vitesse, -ty * vitesse);
  if (tz != 0) {
    const double nouvelle = std::clamp(echelle * std::pow(1.04, tz), min, max);
    const QPointF c(vue->width() / 2.0, vue->height() / 2.0);
    origine = c - (c - origine) * (nouvelle / echelle);
    echelle = nouvelle;
  }
}

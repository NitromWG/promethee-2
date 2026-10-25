// Prométhée : instruments numériques (lot L14 de la 0.34 ; 14.6). Voir analyseurs_numeriques.hpp.
// Licence GPL-3.0-only.
#include "promethee/analyseurs_numeriques.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

namespace prom::ecad {

// ======================================================================================================================
// Voies logiques
// ======================================================================================================================
VoieLogique numeriser(const Releve& s, double bas, double haut) {
  VoieLogique v;
  if (s.empty()) return v;
  auto niveau = [&](double x, Niveau avant) { return x >= haut ? Niveau::Haut : x <= bas ? Niveau::Bas : avant; };
  Niveau n = s.front().v >= haut ? Niveau::Haut : s.front().v <= bas ? Niveau::Bas : Niveau::X;
  v.push_back({s.front().t, n});
  for (std::size_t i = 1; i < s.size(); ++i) {
    const Niveau m = niveau(s[i].v, n);
    if (m == n) continue;
    // Instant interpolé au seuil franchi
    const double seuil = m == Niveau::Haut ? haut : bas;
    const double a = s[i - 1].v, b = s[i].v;
    const double t = b == a ? s[i].t : s[i - 1].t + (s[i].t - s[i - 1].t) * std::clamp((seuil - a) / (b - a), 0.0, 1.0);
    v.push_back({t, m});
    n = m;
  }
  return v;
}

VoieLogique voieDepuisEvenements(const std::vector<std::pair<double, std::string>>& e) {
  VoieLogique v;
  for (const auto& [t, x] : e) {
    const char c = x.empty() ? 'U' : x[0];
    const Niveau n = c == '1' ? Niveau::Haut : c == '0' ? Niveau::Bas : (c == 'Z' || c == 'z') ? Niveau::Z : Niveau::X;
    if (v.empty() || v.back().niveau != n) v.push_back({t, n});
  }
  return v;
}

Niveau niveauA(const VoieLogique& v, double t) {
  if (v.empty()) return Niveau::X;
  const auto it = std::upper_bound(v.begin(), v.end(), t, [](double x, const Front& f) { return x < f.t; });
  return it == v.begin() ? v.front().niveau : (it - 1)->niveau;
}


std::uint64_t valeurBus(const std::vector<VoieLogique>& voies, double t) {
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < voies.size() && i < 64; ++i)
    if (niveauA(voies[i], t) == Niveau::Haut) v |= std::uint64_t{1} << i;
  return v;
}

CaptureLogique capturerA(const std::vector<std::pair<std::string, VoieLogique>>& voies, double instant, double avant, double apres) {
  CaptureLogique c;
  c.instant = instant;
  c.debut = instant - avant;
  c.fin = instant + apres;
  c.declenche = true;
  for (std::size_t i = 0; i < voies.size() && i < 40; ++i) {
    c.noms.push_back(voies[i].first);
    VoieLogique v{{c.debut, niveauA(voies[i].second, c.debut)}};
    for (const auto& f : voies[i].second)
      if (f.t > c.debut && f.t <= c.fin) v.push_back(f);
    c.voies.push_back(std::move(v));
  }
  return c;
}

CaptureLogique capturer(const std::vector<std::pair<std::string, VoieLogique>>& voies, const DeclenchementLogique& d, double avant, double apres) {
  double instant = std::numeric_limits<double>::quiet_NaN();
  if (d.genre == DeclenchementLogique::Genre::Front) {
    if (d.voie >= 0 && d.voie < static_cast<int>(voies.size())) {
      const VoieLogique& v = voies[static_cast<std::size_t>(d.voie)].second;
      for (std::size_t i = 1; i < v.size(); ++i)
        if ((d.montant && v[i].niveau == Niveau::Haut && v[i - 1].niveau != Niveau::Haut) || (!d.montant && v[i].niveau == Niveau::Bas && v[i - 1].niveau != Niveau::Bas)) {
          instant = v[i].t;
          break;
        }
    }
  } else {
    std::set<double> temps;
    std::vector<VoieLogique> vv;
    for (const auto& [n, v] : voies) {
      vv.push_back(v);
      for (const auto& f : v) temps.insert(f.t);
    }
    for (const double t : temps)
      if ((valeurBus(vv, t) & d.masque) == (d.valeur & d.masque)) {
        instant = t;
        break;
      }
  }
  if (std::isnan(instant)) {
    CaptureLogique c;
    for (const auto& [n, v] : voies) c.noms.push_back(n);
    return c;
  }
  return capturerA(voies, instant, avant, apres);
}

// ======================================================================================================================
// I2C
// ======================================================================================================================
namespace {
std::string hexa(unsigned v, int chiffres = 2) {
  char t[16];
  std::snprintf(t, sizeof t, "0x%0*X", chiffres, v);
  return t;
}
// Événements fusionnés des deux voies, dans l'ordre des temps
struct Evenement {
  double t;
  int voie;  // 0 : première, 1 : seconde
  Niveau niveau;
};
std::vector<Evenement> fusionner(const VoieLogique& a, const VoieLogique& b) {
  std::vector<Evenement> e;
  for (std::size_t i = 1; i < a.size(); ++i) e.push_back({a[i].t, 0, a[i].niveau});
  for (std::size_t i = 1; i < b.size(); ++i) e.push_back({b[i].t, 1, b[i].niveau});
  std::stable_sort(e.begin(), e.end(), [](const Evenement& x, const Evenement& y) { return x.t < y.t; });
  return e;
}
}  // namespace

std::string TrameI2C::texte() const {
  std::string t = adresse < 0 ? std::string("?") : std::string(lecture ? "L " : "É ") + hexa(static_cast<unsigned>(adresse));
  for (std::size_t i = 1; i < octets.size(); ++i) t += " · " + hexa(octets[i].valeur) + (octets[i].accuse ? " ✓" : " ✗");
  if (!complete) t += " (sans arrêt)";
  return t;
}

std::vector<TrameI2C> decoderI2C(const VoieLogique& sda, const VoieLogique& scl) {
  std::vector<TrameI2C> r;
  if (sda.empty() || scl.empty()) return r;
  Niveau nSda = sda.front().niveau, nScl = scl.front().niveau;
  TrameI2C* tr = nullptr;
  int bitsOctet = 0;
  std::uint8_t octet = 0;
  double debutOctet = 0;
  // Bit lu au front montant de SCL, gardé jusqu'au front descendant : un départ ou un arrêt pendant que SCL est haut
  // l'annule (le front montant qui précède un arrêt n'est pas un bit)
  bool enAttente = false;
  int bitAttente = 0;
  double tAttente = 0;
  auto ouvrir = [&](double t, bool redepart) {
    if (tr && !tr->complete) tr->fin = t;
    r.emplace_back();
    tr = &r.back();
    tr->debut = t;
    tr->bits.push_back({t, 0, redepart ? "redépart" : "départ"});
    bitsOctet = 0;
    octet = 0;
  };
  auto valider = [&](double t, int b) {
    if (!tr) return;
    if (bitsOctet < 8) {
      if (bitsOctet == 0) debutOctet = t;
      octet = static_cast<std::uint8_t>((octet << 1) | b);
      const bool adresse = tr->octets.empty();
      tr->bits.push_back({t, b, adresse ? (bitsOctet == 7 ? "lecture/écriture" : "adresse") : "donnée"});
      ++bitsOctet;
      return;
    }
    // Neuvième bit : l'accusé
    OctetI2C o;
    o.debut = debutOctet;
    o.fin = t;
    o.valeur = octet;
    o.accuse = b == 0;
    o.adresse = tr->octets.empty();
    if (o.adresse) {
      tr->adresse = octet >> 1;
      tr->lecture = (octet & 1) != 0;
    }
    tr->octets.push_back(o);
    tr->bits.push_back({t, b, b == 0 ? "accusé" : "non-accusé"});
    bitsOctet = 0;
    octet = 0;
  };
  for (const auto& e : fusionner(sda, scl)) {
    if (e.voie == 0) {
      // SDA change pendant que SCL est haut : départ ou arrêt
      if (nScl == Niveau::Haut && tr && e.niveau == Niveau::Haut && nSda == Niveau::Bas) {
        enAttente = false;
        tr->bits.push_back({e.t, 1, "arrêt"});
        tr->fin = e.t;
        if (bitsOctet != 0) tr->erreurs.push_back("arrêt au milieu d'un octet");
        tr->complete = true;
        tr = nullptr;
      } else if (nScl == Niveau::Haut && e.niveau == Niveau::Bas && nSda == Niveau::Haut) {
        enAttente = false;
        ouvrir(e.t, tr != nullptr);
      }
      nSda = e.niveau;
    } else {
      if (tr && e.niveau == Niveau::Haut && nScl != Niveau::Haut) {
        if (nSda == Niveau::X || nSda == Niveau::Z) tr->erreurs.push_back("SDA indéfini à " + std::to_string(e.t) + " s");
        enAttente = true;
        bitAttente = nSda == Niveau::Haut ? 1 : 0;
        tAttente = e.t;
      } else if (e.niveau == Niveau::Bas && nScl == Niveau::Haut && enAttente) {
        enAttente = false;
        valider(tAttente, bitAttente);
      }
      nScl = e.niveau;
    }
  }
  return r;
}

double instantOctetI2C(const std::vector<TrameI2C>& t, std::uint8_t valeur) {
  for (const auto& x : t)
    for (std::size_t i = 1; i < x.octets.size(); ++i)
      if (x.octets[i].valeur == valeur) return x.octets[i].debut;
  return -1;
}

std::string MotifPwl::texte() const {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o << "PWL(";
  for (std::size_t i = 0; i < points.size(); ++i) o << (i ? " " : "") << points[i].first << " " << points[i].second;
  o << ")";
  return o.str();
}

std::pair<MotifPwl, MotifPwl> motifEcritureI2C(std::uint8_t adresse, const std::vector<std::uint8_t>& donnees, double frequence, double vHaut, double depart) {
  const double T = 1 / frequence, q = T / 4, tr = T / 100;
  MotifPwl sda, scl;
  double niveauSda = vHaut, niveauScl = vHaut;
  sda.points.push_back({0, vHaut});
  scl.points.push_back({0, vHaut});
  auto changer = [&](MotifPwl& m, double& niveau, double t, double v) {
    if (v == niveau) return;
    m.points.push_back({t, niveau});
    m.points.push_back({t + tr, v});
    niveau = v;
  };
  double t = depart;
  // Départ : SDA tombe pendant que SCL est haut
  changer(sda, niveauSda, t, 0);
  t += q;
  changer(scl, niveauScl, t, 0);
  auto bit = [&](int b) {
    // SDA change au milieu du niveau bas de SCL, SCL haut une demi-période
    changer(sda, niveauSda, t + q, b ? vHaut : 0);
    changer(scl, niveauScl, t + 2 * q, vHaut);
    changer(scl, niveauScl, t + 4 * q, 0);
    t += T;
  };
  auto octet = [&](std::uint8_t o) {
    for (int i = 7; i >= 0; --i) bit((o >> i) & 1);
    bit(0);  // accusé de l'esclave
  };
  octet(static_cast<std::uint8_t>(adresse << 1));
  for (const auto o : donnees) octet(o);
  // Arrêt : SDA bas, SCL monte, puis SDA monte
  changer(sda, niveauSda, t + q, 0);
  changer(scl, niveauScl, t + 2 * q, vHaut);
  changer(sda, niveauSda, t + 3 * q, vHaut);
  sda.points.push_back({t + 6 * q, vHaut});
  scl.points.push_back({t + 6 * q, vHaut});
  return {sda, scl};
}

// ======================================================================================================================
// SPI
// ======================================================================================================================
std::vector<TrameSPI> decoderSPI(const VoieLogique& sck, const VoieLogique& mosi, const VoieLogique& miso, const VoieLogique& cs, const ReglagesSPI& r) {
  std::vector<TrameSPI> v;
  const bool cpol = r.mode >= 2, cpha = r.mode % 2 == 1;
  const bool surMontant = cpol == cpha;  // modes 0 et 3 : lecture sur le front montant
  const Niveau actif = r.csActifBas ? Niveau::Bas : Niveau::Haut;
  Niveau nCs = cs.empty() ? actif : cs.front().niveau, nSck = sck.empty() ? Niveau::Bas : sck.front().niveau;
  TrameSPI* t = nCs == actif ? &v.emplace_back() : nullptr;
  int n = 0;
  std::uint32_t mo = 0, mi = 0;
  for (const auto& e : fusionner(cs, sck)) {
    if (e.voie == 0) {
      if (e.niveau == actif && nCs != actif) {
        t = &v.emplace_back();
        t->debut = e.t;
        n = 0;
        mo = mi = 0;
      } else if (e.niveau != actif && nCs == actif && t) {
        t->fin = e.t;
        t = nullptr;
      }
      nCs = e.niveau;
    } else {
      const bool montant = e.niveau == Niveau::Haut && nSck != Niveau::Haut, descendant = e.niveau == Niveau::Bas && nSck == Niveau::Haut;
      if (t && ((surMontant && montant) || (!surMontant && descendant))) {
        const int bo = niveauA(mosi, e.t) == Niveau::Haut ? 1 : 0, bi = niveauA(miso, e.t) == Niveau::Haut ? 1 : 0;
        if (r.poidsFortDabord) {
          mo = (mo << 1) | static_cast<std::uint32_t>(bo);
          mi = (mi << 1) | static_cast<std::uint32_t>(bi);
        } else {
          mo |= static_cast<std::uint32_t>(bo) << n;
          mi |= static_cast<std::uint32_t>(bi) << n;
        }
        if (++n == r.bits) {
          t->mosi.push_back(mo);
          t->miso.push_back(mi);
          n = 0;
          mo = mi = 0;
        }
      }
      nSck = e.niveau;
    }
  }
  return v;
}

// ======================================================================================================================
// CAN
// ======================================================================================================================
std::uint16_t crcCAN(const std::vector<int>& bits) {
  std::uint16_t crc = 0;
  for (const int b : bits) {
    const int suivant = b ^ ((crc >> 14) & 1);
    crc = static_cast<std::uint16_t>((crc << 1) & 0x7FFF);
    if (suivant) crc ^= 0x4599;
  }
  return crc;
}

namespace {
void ajouterBits(std::vector<int>& v, std::uint32_t x, int n) {
  for (int i = n - 1; i >= 0; --i) v.push_back(static_cast<int>((x >> i) & 1));
}
}  // namespace

std::vector<int> bitsTrameCAN(std::uint32_t id, const std::vector<std::uint8_t>& donnees, bool etendu, bool accusee) {
  std::vector<int> b{0};  // départ de trame
  if (etendu) {
    ajouterBits(b, (id >> 18) & 0x7FF, 11);
    b.push_back(1);  // SRR
    b.push_back(1);  // IDE
    ajouterBits(b, id & 0x3FFFF, 18);
    b.push_back(0);  // RTR
    b.push_back(0);  // r1
    b.push_back(0);  // r0
  } else {
    ajouterBits(b, id & 0x7FF, 11);
    b.push_back(0);  // RTR
    b.push_back(0);  // IDE
    b.push_back(0);  // r0
  }
  const std::size_t n = std::min<std::size_t>(donnees.size(), 8);
  ajouterBits(b, static_cast<std::uint32_t>(n), 4);
  for (std::size_t i = 0; i < n; ++i) ajouterBits(b, donnees[i], 8);
  ajouterBits(b, crcCAN(b), 15);
  // Bourrage : après cinq bits égaux, un bit contraire
  std::vector<int> s;
  int pareil = 0, dernier = -1;
  for (const int x : b) {
    s.push_back(x);
    pareil = x == dernier ? pareil + 1 : 1;
    dernier = x;
    if (pareil == 5) {
      s.push_back(1 - x);
      dernier = 1 - x;
      pareil = 1;
    }
  }
  s.push_back(1);                 // délimiteur du CRC
  s.push_back(accusee ? 0 : 1);   // accusé (dominant si un nœud a reçu)
  s.push_back(1);                 // délimiteur de l'accusé
  for (int i = 0; i < 7; ++i) s.push_back(1);  // fin de trame
  for (int i = 0; i < 3; ++i) s.push_back(1);  // intertrame
  return s;
}

VoieLogique voieDepuisBits(const std::vector<int>& bits, double debit, double debut) {
  VoieLogique v{{debut, Niveau::Haut}};
  const double tb = 1 / debit;
  // Bus au repos (récessif) avant la trame
  double t = debut + 11 * tb;
  for (const int b : bits) {
    const Niveau n = b ? Niveau::Haut : Niveau::Bas;
    if (v.back().niveau != n) v.push_back({t, n});
    t += tb;
  }
  if (v.back().niveau != Niveau::Haut) v.push_back({t, Niveau::Haut});
  return v;
}

std::vector<TrameCAN> decoderCAN(const VoieLogique& rx, double debit) {
  std::vector<TrameCAN> r;
  if (rx.size() < 2) return r;
  const double tb = 1 / debit;
  std::size_t i = 1;
  double finPrecedente = rx.front().t;
  while (i < rx.size()) {
    // Départ : front récessif → dominant après au moins 11 bits récessifs
    // (après une trame, l'intertrame de trois bits suffit)
    while (i < rx.size() && !(rx[i].niveau == Niveau::Bas && rx[i - 1].niveau == Niveau::Haut && rx[i].t >= finPrecedente &&
                              rx[i].t - rx[i - 1].t >= (r.empty() ? 10.5 : 2.5) * tb))
      ++i;
    if (i >= rx.size()) break;
    const double t0 = rx[i].t;
    TrameCAN tr;
    tr.debut = t0;
    std::vector<int> brut;   // bits lus, bourrage retiré, jusqu'au CRC compris
    int pareil = 0, dernier = -1;
    std::size_t k = 0;
    bool fini = false;
    auto lireBit = [&](std::size_t rang) { return niveauA(rx, t0 + (static_cast<double>(rang) + 0.5) * tb) == Niveau::Haut ? 1 : 0; };
    // Longueur attendue : connue après l'IDE et le DLC
    std::size_t attendus = 19;  // SOF + 11 + RTR/SRR + IDE + r0 + DLC (4)
    bool etendu = false;
    int dlc = -1;
    while (!fini && k < 200) {
      const int b = lireBit(k++);
      if (pareil == 5) {
        if (b == dernier) {
          tr.erreurs.push_back("bourrage");
          break;
        }
        pareil = 1;
        dernier = b;
        continue;
      }
      brut.push_back(b);
      pareil = b == dernier ? pareil + 1 : 1;
      dernier = b;
      if (brut.size() == 14 && brut[13] == 1 && !etendu) {
        etendu = true;
        attendus = 39;  // SOF + 11 + SRR + IDE + 18 + RTR + r1 + r0 + DLC
      }
      if (brut.size() == attendus && dlc < 0) {
        dlc = 0;
        for (std::size_t j = attendus - 4; j < attendus; ++j) dlc = dlc * 2 + brut[j];
        attendus += static_cast<std::size_t>(std::min(dlc, 8)) * 8 + 15;
      }
      if (dlc >= 0 && brut.size() == attendus) fini = true;
    }
    if (fini && pareil == 5) ++k;  // bit de bourrage après les cinq derniers bits égaux du CRC
    if (fini) {
      auto champ = [&](std::size_t a, std::size_t n) {
        std::uint32_t v = 0;
        for (std::size_t j = a; j < a + n; ++j) v = (v << 1) | static_cast<std::uint32_t>(brut[j]);
        return v;
      };
      tr.etendu = etendu;
      if (etendu) {
        tr.identifiant = (champ(1, 11) << 18) | champ(14, 18);
        tr.distante = brut[32] == 1;
      } else {
        tr.identifiant = champ(1, 11);
        tr.distante = brut[12] == 1;
      }
      const std::size_t debutDonnees = etendu ? 39 : 19;
      for (int j = 0; j < std::min(dlc, 8); ++j) tr.donnees.push_back(static_cast<std::uint8_t>(champ(debutDonnees + static_cast<std::size_t>(j) * 8, 8)));
      const std::size_t debutCrc = brut.size() - 15;
      tr.crc = static_cast<std::uint16_t>(champ(debutCrc, 15));
      tr.crcCalcule = crcCAN(std::vector<int>(brut.begin(), brut.begin() + static_cast<long>(debutCrc)));
      tr.crcJuste = tr.crc == tr.crcCalcule;
      if (!tr.crcJuste) tr.erreurs.push_back("CRC");
      // Délimiteur, accusé, délimiteur, fin de trame
      if (lireBit(k) != 1) tr.erreurs.push_back("forme");
      tr.acquittee = lireBit(k + 1) == 0;
      if (!tr.acquittee) tr.erreurs.push_back("accusé");
      if (lireBit(k + 2) != 1) tr.erreurs.push_back("forme");
      for (std::size_t j = 0; j < 7; ++j)
        if (lireBit(k + 3 + j) != 1) {
          tr.erreurs.push_back("forme");
          break;
        }
      k += 10;
    }
    tr.fin = t0 + static_cast<double>(k) * tb;
    finPrecedente = tr.fin;
    r.push_back(tr);
    while (i < rx.size() && rx[i].t < tr.fin) ++i;
  }
  return r;
}

CompteursCAN compterErreurs(const std::vector<TrameCAN>& t) {
  CompteursCAN c;
  for (const auto& x : t) {
    ++c.trames;
    for (const auto& e : x.erreurs) {
      if (e == "CRC") ++c.crc;
      else if (e == "bourrage") ++c.bourrage;
      else if (e == "forme") ++c.forme;
      else if (e == "accusé") ++c.accuse;
    }
  }
  return c;
}

// ---------- DBC ----------
const MessageDBC* BaseDBC::message(std::uint32_t identifiant) const {
  for (const auto& m : messages)
    if ((m.identifiant & 0x1FFFFFFF) == (identifiant & 0x1FFFFFFF)) return &m;
  return nullptr;
}

std::optional<BaseDBC> lireDBC(const std::string& texte, std::string* erreur) {
  BaseDBC b;
  std::istringstream in(texte);
  MessageDBC* courant = nullptr;
  int n = 0;
  for (std::string l; std::getline(in, l);) {
    ++n;
    std::size_t p = l.find_first_not_of(" \t");
    if (p == std::string::npos) continue;
    const std::string s = l.substr(p);
    if (s.rfind("BO_ ", 0) == 0) {
      MessageDBC m;
      std::istringstream mots(s.substr(4));
      unsigned long id = 0;
      std::string nom;
      mots >> id >> nom;
      if (!nom.empty() && nom.back() == ':') nom.pop_back();
      int dlc = 8;
      std::string emetteur;
      mots >> dlc >> emetteur;
      m.identifiant = static_cast<std::uint32_t>(id & 0x9FFFFFFFul);
      m.nom = nom;
      m.longueur = dlc;
      m.emetteur = emetteur;
      b.messages.push_back(m);
      courant = &b.messages.back();
    } else if (s.rfind("SG_ ", 0) == 0) {
      if (!courant) {
        if (erreur) *erreur = "ligne " + std::to_string(n) + " : signal hors d'un message";
        return std::nullopt;
      }
      // SG_ nom : début|longueur@ordre signe (facteur,décalage) [min|max] "unité" récepteurs
      SignalDBC g;
      const auto dp = s.find(':');
      if (dp == std::string::npos) continue;
      std::istringstream nomFlux(s.substr(4, dp - 4));
      nomFlux >> g.nom;
      int debut = 0, longueur = 0;
      char ordre = '1', signe = '+';
      double facteur = 1, decalage = 0, mn = 0, mx = 0;
      if (std::sscanf(s.c_str() + dp + 1, " %d|%d@%c%c (%lf,%lf) [%lf|%lf]", &debut, &longueur, &ordre, &signe, &facteur, &decalage, &mn, &mx) < 4) {
        if (erreur) *erreur = "ligne " + std::to_string(n) + " : signal illisible";
        return std::nullopt;
      }
      g.debut = debut;
      g.longueur = longueur;
      g.intel = ordre == '1';
      g.signe = signe == '-';
      g.facteur = facteur;
      g.decalage = decalage;
      g.min = mn;
      g.max = mx;
      if (const auto q1 = s.find('"', dp); q1 != std::string::npos)
        if (const auto q2 = s.find('"', q1 + 1); q2 != std::string::npos) g.unite = s.substr(q1 + 1, q2 - q1 - 1);
      courant->signaux.push_back(g);
    }
  }
  if (b.messages.empty()) {
    if (erreur) *erreur = "aucun message (BO_)";
    return std::nullopt;
  }
  return b;
}

std::map<std::string, double> signauxCAN(const BaseDBC& b, const TrameCAN& t) {
  std::map<std::string, double> r;
  const MessageDBC* m = b.message(t.identifiant);
  if (!m) return r;
  auto bit = [&](int k) { return k >= 0 && k / 8 < static_cast<int>(t.donnees.size()) ? (t.donnees[static_cast<std::size_t>(k / 8)] >> (k % 8)) & 1 : 0; };
  for (const auto& g : m->signaux) {
    std::uint64_t brut = 0;
    if (g.intel)
      for (int i = 0; i < g.longueur; ++i) brut |= static_cast<std::uint64_t>(bit(g.debut + i)) << i;
    else {
      // Motorola : le bit de début est le poids fort, la numérotation descend dans l'octet puis passe au suivant
      int k = g.debut;
      for (int i = 0; i < g.longueur; ++i) {
        brut = (brut << 1) | static_cast<std::uint64_t>(bit(k));
        k = (k % 8 == 0) ? k + 15 : k - 1;
      }
    }
    double v = static_cast<double>(brut);
    if (g.signe && g.longueur < 64 && (brut >> (g.longueur - 1)) & 1) v -= std::ldexp(1.0, g.longueur);
    r[g.nom] = v * g.facteur + g.decalage;
  }
  return r;
}

}  // namespace prom::ecad

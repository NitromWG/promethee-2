// Prométhée : tableau électrique d'un logement selon la NF C 15-100 (règles courantes des guides des fabricants,
// édition de 2024 applicable depuis le 1er septembre 2025) : prises par pièce, circuits de prises et d'éclairage,
// circuits spécialisés, interrupteurs différentiels 30 mA (nombre, type, calibre, huit circuits au plus), réserve
// de 20 %. Rend aussi le projet électrique complet (unifilaire, coffret implanté, nomenclature).
// Ces règles aident à concevoir ; elles ne remplacent ni le texte de la norme ni le contrôle du Consuel.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "electrique_interne.hpp"
#include "promethee/electrique.hpp"

namespace prom::elec {

namespace {

// Nombre minimal de socles de prise de courant d'une pièce
int prisesRequises(const PieceLogement& p) {
  switch (p.type) {
    case Piece::Sejour: return std::clamp(static_cast<int>(std::ceil(p.surface / 4.0 - 1e-9)), 5, 7);  // 1 par 4 m², 5 au moins, 7 suffisent
    case Piece::Chambre:
    case Piece::Bureau: return 3;
    case Piece::Cuisine: return p.surface <= 4.0 ? 3 : 6;
    case Piece::SalleDeBain: return p.surface > 4.0 ? 1 : 0;
    case Piece::WC: return 0;
    case Piece::Entree:
    case Piece::Couloir: return p.surface > 4.0 ? 1 : 0;
    case Piece::Garage:
    case Piece::Buanderie: return 1;
    case Piece::Exterieur: return 0;
    case Piece::Autre: return 1;
  }
  return 1;
}

int prisesPrevues(const PieceLogement& p) { return p.prises > 0 ? p.prises : prisesRequises(p); }

std::string nomDe(const PieceLogement& p) { return p.nom.empty() ? std::string(nomPiece(p.type)) : p.nom; }

std::string minuscule(std::string s) {
  if (!s.empty() && static_cast<unsigned char>(s[0]) < 128) s[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
  return s;
}

CircuitLogement circuit(const std::string& nom, const std::string& usage, double calibre, double section, int points, double puissance, bool specialise, bool typeA = false) {
  CircuitLogement c;
  c.nom = nom;
  c.usage = usage;
  c.calibre = calibre;
  c.section = section;
  c.points = points;
  c.puissance = puissance;
  c.specialise = specialise;
  c.differentielTypeA = typeA;
  return c;
}

// Différentiel exigé par l'usage : « A » (cuisson, lavage, recharge), « F » (variation de vitesse monophasée :
// climatisation, pompe à chaleur ; conseillé pour le congélateur), vide sinon
std::string typeExige(const CircuitLogement& c) {
  if (c.usage == "climatisation" || c.usage == "congelateur") return "F";
  if (c.differentielTypeA) return "A";
  return {};
}

bool chargeContinue(const CircuitLogement& c) { return c.usage == "chauffage" || c.usage == "chauffe-eau" || c.usage == "recharge"; }

int modulesDe(const CircuitLogement& c, bool triphase) { return (triphase && c.usage == "cuisson") ? 4 : 1; }

}  // namespace

TableauLogement tableauLogement(const Logement& l) {
  TableauLogement t;
  std::vector<CircuitLogement> circuits;
  // ----- Prises : la cuisine sur ses propres circuits (6 au plus), les autres pièces groupées par 12 au plus
  // sur des circuits 20 A en 2,5 mm² (8 au plus en 1,5 mm² sur 16 A)
  struct Lot {
    std::string piece;
    int prises = 0;
  };
  std::vector<Lot> lots;
  int prisesCuisine = 0;
  for (const auto& p : l.pieces) {
    const int n = prisesPrevues(p);
    if (n <= 0) continue;
    if (p.type == Piece::Cuisine) prisesCuisine += n;
    else lots.push_back({nomDe(p), n});
  }
  for (const auto& p : l.pieces)
    if (p.type == Piece::Cuisine) {
      lots.push_back({"hotte", 1});  // prise de la hotte, à 1,80 m
      break;
    }
  for (int reste = prisesCuisine, k = 1; reste > 0; ++k) {
    const int n = std::min(6, reste);
    circuits.push_back(circuit(prisesCuisine > 6 ? "Prises cuisine " + std::to_string(k) : "Prises cuisine", "prises", 20, 2.5, n, 0, false));
    reste -= n;
  }
  {
    std::vector<CircuitLogement> prises;
    CircuitLogement courant;
    std::vector<std::string> pieces;
    auto clore = [&] {
      if (courant.points == 0) return;
      std::string nom = "Prises ";
      for (std::size_t i = 0; i < pieces.size(); ++i) nom += (i ? ", " : "") + minuscule(pieces[i]);
      courant.nom = nom;
      prises.push_back(courant);
      courant = circuit("", "prises", 20, 2.5, 0, 0, false);
      pieces.clear();
    };
    courant = circuit("", "prises", 20, 2.5, 0, 0, false);
    for (const auto& lot : lots) {
      int reste = lot.prises;
      // Une pièce reste sur un seul circuit quand c'est possible
      if (courant.points > 0 && courant.points + reste > 12 && reste <= 12) clore();
      while (reste > 0) {
        if (courant.points >= 12) clore();
        const int n = std::min(12 - courant.points, reste);
        courant.points += n;
        if (std::find(pieces.begin(), pieces.end(), lot.piece) == pieces.end()) pieces.push_back(lot.piece);
        reste -= n;
      }
    }
    clore();
    for (auto& c : prises) circuits.push_back(c);
  }
  // ----- Éclairage : 8 points au plus par circuit, 1,5 mm², 16 A ; zone jour et zone nuit sur des circuits distincts
  {
    int jour = 0, nuit = 0;
    for (const auto& p : l.pieces) {
      const bool zoneNuit = p.type == Piece::Chambre || p.type == Piece::SalleDeBain || p.type == Piece::Bureau;
      (zoneNuit ? nuit : jour) += std::max(0, p.pointsLumineux);
    }
    auto ajouterEclairage = [&](const std::string& zone, int points) {
      const int n = static_cast<int>(std::ceil(points / 8.0 - 1e-9));
      for (int k = 0, reste = points; k < n; ++k) {
        const int x = std::min(8, reste);
        circuits.push_back(circuit("Éclairage " + zone + (n > 1 ? " " + std::to_string(k + 1) : std::string()), "eclairage", 16, 1.5, x, 0, false));
        reste -= x;
      }
    };
    if (l.surface > 35 && jour > 0 && nuit > 0) {
      ajouterEclairage("jour", jour);
      ajouterEclairage("nuit", nuit);
    } else if (jour + nuit > 0) {
      ajouterEclairage("", jour + nuit);
      for (auto& c : circuits)
        if (c.usage == "eclairage" && c.nom.size() > 1 && c.nom.back() == ' ') c.nom.pop_back();
    }
  }
  // ----- Volets roulants
  {
    int volets = 0;
    for (const auto& p : l.pieces) volets += p.voletRoulant ? 1 : 0;
    if (volets > 0) circuits.push_back(circuit("Volets roulants", "volets", 16, 1.5, volets, 0, false));
  }
  // ----- Circuits spécialisés
  if (l.plaqueCuisson)
    circuits.push_back(l.triphase ? circuit("Plaque de cuisson (triphasée)", "cuisson", 20, 2.5, 1, 7200, true, true) : circuit("Plaque de cuisson", "cuisson", 32, 6, 1, 7200, true, true));
  if (l.four) circuits.push_back(circuit("Four", "four", 20, 2.5, 1, 3000, true));
  if (l.laveVaisselle) circuits.push_back(circuit("Lave-vaisselle", "lave-vaisselle", 20, 2.5, 1, 2500, true));
  if (l.laveLinge) circuits.push_back(circuit("Lave-linge", "lave-linge", 20, 2.5, 1, 2500, true, true));
  if (l.secheLinge) circuits.push_back(circuit("Sèche-linge", "seche-linge", 20, 2.5, 1, 2500, true, true));
  if (l.congelateur) circuits.push_back(circuit("Congélateur", "congelateur", 20, 2.5, 1, 300, true));
  if (l.chauffeEau) circuits.push_back(circuit("Chauffe-eau", "chauffe-eau", 20, 2.5, 1, l.puissanceChauffeEau, true));
  if (l.climatisation) circuits.push_back(circuit("Climatisation", "climatisation", 20, 2.5, 1, l.puissanceClim, true));
  if (l.vmc) circuits.push_back(circuit("VMC", "vmc", 2, 1.5, 1, 50, true));
  circuits.push_back(circuit("Tableau de communication", "communication", 16, 1.5, 1, 100, true));
  if (l.borneRecharge) {
    const double w = l.puissanceBorne;
    if (w <= 3700) circuits.push_back(circuit("Recharge du véhicule (prise renforcée)", "recharge", 20, 2.5, 1, w, true, true));
    else if (w <= 7400 || !l.triphase) circuits.push_back(circuit("Borne de recharge du véhicule", "recharge", 40, 10, 1, std::min(w, 7400.0), true, true));
    else circuits.push_back(circuit("Borne de recharge du véhicule (triphasée)", "recharge", 20, 6, 1, w, true, true));
  }
  // ----- Chauffage électrique : 4 500 W au plus par circuit en 2,5 mm² (20 A)
  {
    double cumul = 0;
    std::vector<std::string> pieces;
    int k = 0;
    auto clore = [&] {
      if (cumul <= 0) return;
      std::string nom = "Chauffage ";
      for (std::size_t i = 0; i < pieces.size(); ++i) nom += (i ? ", " : "") + minuscule(pieces[i]);
      circuits.push_back(circuit(nom, "chauffage", 20, 2.5, static_cast<int>(pieces.size()), cumul, true));
      cumul = 0;
      pieces.clear();
      ++k;
    };
    for (const auto& p : l.pieces) {
      if (p.chauffage <= 0) continue;
      if (cumul + p.chauffage > 4500) clore();
      cumul += p.chauffage;
      pieces.push_back(nomDe(p));
    }
    clore();
  }

  // ----- Différentiels 30 mA : 2 au moins, 3 de 35 à 100 m², 4 au-delà ; 8 circuits au plus chacun ; la
  // recharge du véhicule a le sien (édition 2024)
  std::vector<std::size_t> recharge, typeF, typeA, eclairage, prises, autres;
  for (std::size_t i = 0; i < circuits.size(); ++i) {
    const CircuitLogement& c = circuits[i];
    if (c.usage == "recharge") recharge.push_back(i);
    else if (typeExige(c) == "F") typeF.push_back(i);
    else if (typeExige(c) == "A") typeA.push_back(i);
    else if (c.usage == "eclairage") eclairage.push_back(i);
    else if (c.usage == "prises") prises.push_back(i);
    else autres.push_back(i);
  }
  const std::size_t partages = circuits.size() - recharge.size();
  int nombre = l.surface <= 35 ? 2 : l.surface <= 100 ? 3 : 4;
  nombre = std::max({nombre, 2, static_cast<int>((partages + 7) / 8)});
  if (!typeF.empty()) nombre = std::max(nombre, 3);  // A, F et au moins un autre
  std::vector<std::vector<std::size_t>> groupes(static_cast<std::size_t>(nombre));
  std::vector<std::string> types(static_cast<std::size_t>(nombre), "A");
  // Différentiel 0 : circuits exigeant le type A ; différentiel 1 : type F s'il en faut un
  const std::size_t indiceF = typeF.empty() ? static_cast<std::size_t>(-1) : 1;
  if (indiceF != static_cast<std::size_t>(-1)) types[indiceF] = "F";
  auto ajouterDans = [&](std::size_t g, std::size_t c) {
    while (groupes[g].size() >= 8) {
      // Plein : un différentiel de plus, du même type
      groupes.emplace_back();
      types.push_back(types[g]);
      g = groupes.size() - 1;
    }
    groupes[g].push_back(c);
  };
  for (std::size_t c : typeA) ajouterDans(0, c);
  for (std::size_t c : typeF) ajouterDans(indiceF, c);
  // Éclairage et prises répartis sur au moins deux différentiels : le moins chargé d'abord, en alternant
  auto moinsCharge = [&](std::size_t sauf) {
    std::size_t m = static_cast<std::size_t>(-1);
    for (std::size_t g = 0; g < groupes.size(); ++g) {
      if (g == sauf || groupes[g].size() >= 8) continue;
      if (m == static_cast<std::size_t>(-1) || groupes[g].size() < groupes[m].size()) m = g;
    }
    if (m == static_cast<std::size_t>(-1)) {
      groupes.emplace_back();
      types.push_back("A");
      m = groupes.size() - 1;
    }
    return m;
  };
  for (const auto* famille : {&eclairage, &prises, &autres}) {
    std::size_t precedent = static_cast<std::size_t>(-1);
    for (std::size_t c : *famille) {
      const std::size_t g = moinsCharge(precedent);
      groupes[g].push_back(c);
      precedent = g;
    }
  }
  for (std::size_t c : recharge) {
    groupes.push_back({c});
    types.push_back("A");
  }
  // Calibre : au moins 1 × (chauffage, chauffe-eau, recharge) + 0,5 × (autres circuits), puis 25, 40 ou 63 A
  for (std::size_t g = 0; g < groupes.size(); ++g) {
    if (groupes[g].empty()) continue;
    double somme = 0;
    for (std::size_t c : groupes[g]) somme += circuits[c].calibre * (chargeContinue(circuits[c]) ? 1.0 : 0.5);
    TableauLogement::Differentiel d;
    d.type = types[g];
    d.calibre = somme <= 25 ? 25 : somme <= 40 ? 40 : 63;
    if (somme > 63)
      t.remarques.push_back("Le différentiel " + std::to_string(t.differentiels.size() + 1) + " protège " + detail::nombreFr(somme) +
                            " A selon la règle de calcul : répartis ses circuits ou prends un appareil de plus.");
    for (std::size_t c : groupes[g]) circuits[c].differentiel = static_cast<int>(t.differentiels.size());
    t.differentiels.push_back(d);
  }
  // Ordre du tableau : différentiel par différentiel ; sous chacun, éclairage, prises, volets, puis spécialisés
  auto rang = [](const CircuitLogement& c) {
    if (c.usage == "eclairage") return 0;
    if (c.usage == "prises") return 1;
    if (c.usage == "volets") return 2;
    return 3;
  };
  std::stable_sort(circuits.begin(), circuits.end(), [&](const CircuitLogement& a, const CircuitLogement& b) {
    if (a.differentiel != b.differentiel) return a.differentiel < b.differentiel;
    return rang(a) < rang(b);
  });
  t.circuits = circuits;

  // ----- Encombrement : 2 modules par différentiel (4 en triphasé), 1 par disjoncteur, contacteur heures
  // creuses, parafoudre ; réserve de 20 % au moins
  int modules = 0;
  for (const auto& d : t.differentiels) {
    (void)d;
    modules += l.triphase ? 4 : 2;
  }
  for (const auto& c : t.circuits) modules += modulesDe(c, l.triphase);
  if (l.chauffeEau) modules += 1;  // contacteur heures creuses
  modules += 2;                    // parafoudre
  t.modulesOccupes = modules;
  constexpr int parRangee = 13;
  t.rangees = std::max(1, static_cast<int>(std::ceil(modules / (parRangee * 0.8) - 1e-9)));
  t.modulesTotal = t.rangees * parRangee;
  t.remarques.push_back("Coffret de " + std::to_string(t.rangees) + " rangée(s) de 13 modules : " + std::to_string(t.modulesOccupes) + " modules occupés, " +
                        std::to_string(t.modulesTotal - t.modulesOccupes) + " de réserve (20 % au moins).");
  if (l.chauffeEau) t.remarques.push_back("Chauffe-eau : contacteur heures creuses (1 module) piloté par le compteur.");
  t.remarques.push_back("Parafoudre de type 2 (In ≥ 5 kA) compté dans le coffret : obligatoire selon la zone et le bâtiment, conseillé partout ; liaison de 6 mm² au moins.");
  t.remarques.push_back("Différentiels en type A (le type AC ne voit pas les défauts à composante continue des appareils électroniques)" +
                        std::string(typeF.empty() ? "." : " ; type F pour la climatisation et le congélateur."));
  if (l.borneRecharge) t.remarques.push_back("La recharge du véhicule a son circuit et son différentiel 30 mA, que rien d'autre ne partage.");
  return t;
}

std::vector<AvisNorme> verifierLogement(const Logement& l, const TableauLogement& t) {
  std::vector<AvisNorme> v;
  auto avis = [&](bool exigence, const std::string& regle, const std::string& message) { v.push_back({exigence, regle, message}); };
  for (const auto& p : l.pieces) {
    const int requises = prisesRequises(p);
    if (p.prises > 0 && p.prises < requises)
      avis(true, "Prises par pièce",
           nomDe(p) + " (" + detail::nombreFr(p.surface) + " m²) : " + std::to_string(requises) + " prises au minimum, " + std::to_string(p.prises) + " prévues");
    if (p.pointsLumineux < 1 && p.type != Piece::Exterieur) avis(true, "Éclairage", nomDe(p) + " : un point d'éclairage au moins (plafond ou applique commandée)");
  }
  int specialises = 0, eclairage = 0, prises = 0;
  std::set<int> diffEclairage, diffPrises;
  std::map<int, int> parDifferentiel;
  bool cuisson = false;
  for (const auto& c : t.circuits) {
    ++parDifferentiel[c.differentiel];
    if (c.usage == "prises") {
      ++prises;
      diffPrises.insert(c.differentiel);
      const int max = c.section >= 2.5 ? 12 : 8;
      if (c.points > max)
        avis(true, "Circuits de prises", c.nom + " : " + std::to_string(c.points) + " prises, " + std::to_string(max) + " au plus en " + detail::nombreFr(c.section) + " mm²");
      if (c.nom.find("cuisine") != std::string::npos && c.points > 6)
        avis(false, "Circuits de prises", c.nom + " : 6 prises au plus sur le circuit de la cuisine");
    }
    if (c.usage == "eclairage") {
      ++eclairage;
      diffEclairage.insert(c.differentiel);
      if (c.points > 8) avis(true, "Éclairage", c.nom + " : " + std::to_string(c.points) + " points, 8 au plus par circuit");
    }
    if (c.usage == "cuisson") cuisson = true;
    if (c.usage == "four" || c.usage == "lave-vaisselle" || c.usage == "lave-linge" || c.usage == "seche-linge" || c.usage == "congelateur") ++specialises;
    if (c.differentiel >= 0 && c.differentiel < static_cast<int>(t.differentiels.size())) {
      const std::string type = t.differentiels[static_cast<std::size_t>(c.differentiel)].type;
      const std::string exige = typeExige(c);
      if (exige == "A" && type == "AC") avis(true, "Différentiels", c.nom + " : sur un différentiel de type A au moins");
      if (c.usage == "climatisation" && type != "F") avis(true, "Différentiels", c.nom + " : variation de vitesse monophasée, différentiel de type F");
      if (c.usage == "congelateur" && type != "F") avis(false, "Différentiels", c.nom + " : type F conseillé (moins de déclenchements intempestifs)");
    }
    if (c.usage == "recharge" && parDifferentiel.count(c.differentiel)) {
      int autres = 0;
      for (const auto& x : t.circuits) autres += (x.differentiel == c.differentiel && &x != &c) ? 1 : 0;
      if (autres > 0) avis(true, "Recharge du véhicule", "Le point de recharge doit avoir son propre différentiel 30 mA, non partagé");
    }
  }
  if (t.differentiels.size() < 2) avis(true, "Différentiels", "Deux interrupteurs différentiels 30 mA au moins");
  const std::size_t conseille = l.surface <= 35 ? 2 : l.surface <= 100 ? 3 : 4;
  if (t.differentiels.size() < conseille)
    avis(false, "Différentiels", std::to_string(conseille) + " différentiels conseillés pour " + detail::nombreFr(l.surface) + " m² (" + std::to_string(t.differentiels.size()) + " prévus)");
  for (const auto& [d, n] : parDifferentiel)
    if (n > 8) avis(true, "Différentiels", "Le différentiel " + std::to_string(d + 1) + " protège " + std::to_string(n) + " circuits : 8 au plus");
  if (eclairage >= 2 && diffEclairage.size() < 2) avis(true, "Différentiels", "Les circuits d'éclairage doivent être répartis sur deux différentiels au moins");
  if (prises >= 2 && diffPrises.size() < 2) avis(true, "Différentiels", "Les circuits de prises doivent être répartis sur deux différentiels au moins");
  if (specialises < 3) avis(true, "Circuits spécialisés", "Trois circuits spécialisés au moins (lave-linge, lave-vaisselle, four, sèche-linge, congélateur), " + std::to_string(specialises) + " prévus");
  if (!cuisson) avis(true, "Circuits spécialisés", "Un circuit de cuisson (32 A en 6 mm², ou 20 A en 2,5 mm² en triphasé) même sans plaque prévue : boîte de connexion en attente");
  if (t.modulesTotal > 0 && (t.modulesTotal - t.modulesOccupes) < 0.2 * t.modulesTotal - 1e-9)
    avis(true, "Réserve", "Réserve du tableau inférieure à 20 % (" + std::to_string(t.modulesTotal - t.modulesOccupes) + " modules libres sur " + std::to_string(t.modulesTotal) + ")");
  // Puissance appelée et abonnement : éclairage et prises (2 kVA), puis chaque usage avec sa simultanéité usuelle ;
  // chauffe-eau et recharge programmés en heures creuses
  double appelee = 2.0;
  for (const auto& c : t.circuits) {
    double k = 0;
    if (c.usage == "cuisson") k = 0.4;
    else if (c.usage == "four") k = 0.5;
    else if (c.usage == "lave-vaisselle" || c.usage == "lave-linge" || c.usage == "seche-linge") k = 0.4;
    else if (c.usage == "congelateur" || c.usage == "vmc" || c.usage == "communication" || c.usage == "chauffage") k = 1.0;
    else if (c.usage == "climatisation") k = 0.8;
    appelee += k * c.puissance / 1000.0;
  }
  if (appelee > l.abonnementKVA * 1.0 + 1e-9)
    avis(false, "Abonnement", "Puissance appelée estimée " + detail::nombreFr(appelee, 1) + " kVA pour un abonnement de " + detail::nombreFr(l.abonnementKVA) +
                                  " kVA : délestage ou abonnement supérieur à envisager");
  return v;
}

Logement logementType(double surface, int chambres) {
  Logement l;
  l.surface = std::clamp(surface, 15.0, 1000.0);
  chambres = std::clamp(chambres, 0, 12);
  l.nom = "Logement " + detail::nombreFr(l.surface) + " m²";
  l.abonnementKVA = l.surface <= 50 ? 6 : l.surface <= 120 ? 9 : 12;
  const double chambre = 11.0, sdb = l.surface < 60 ? 4.5 : 6.0, wc = 1.5, entree = l.surface < 50 ? 3.0 : 5.0;
  double reste = l.surface - chambres * chambre - sdb - wc - entree;
  const double cuisine = std::max(4.0, std::min(14.0, reste * 0.3));
  reste -= cuisine;
  const double sejour = std::max(12.0, reste);
  auto piece = [&](Piece type, const std::string& nom, double s, int points, bool volet) {
    PieceLogement p;
    p.type = type;
    p.nom = nom;
    p.surface = std::round(s * 10.0) / 10.0;
    p.pointsLumineux = points;
    p.voletRoulant = volet;
    l.pieces.push_back(p);
  };
  piece(Piece::Entree, "Entrée", entree, 1, false);
  piece(Piece::Sejour, "Séjour", sejour, 2, true);
  piece(Piece::Cuisine, "Cuisine", cuisine, 2, true);
  for (int i = 1; i <= chambres; ++i) piece(Piece::Chambre, chambres > 1 ? "Chambre " + std::to_string(i) : std::string("Chambre"), chambre, 1, true);
  piece(Piece::SalleDeBain, "Salle d'eau", sdb, 2, false);
  piece(Piece::WC, "WC", wc, 1, false);
  return l;
}

ProjetElectrique projetLogement(const Logement& l) {
  const TableauLogement t = tableauLogement(l);
  ProjetElectrique p = nouveauProjetElectrique(l.nom, false);
  p.logement = l;
  p.reglages.tension = l.triphase ? 400 : 230;
  p.reglages.tensionSimple = 230;
  p.reglages.tensionCommande = 230;
  p.reglages.commandeContinue = false;
  p.reglages.regime = RegimeNeutre::TT;
  p.reglages.installation = l.nom;
  p.schema.cartouche.titre = l.nom;
  // Folios : page de garde, puis le tableau en unifilaire
  p.schema.feuilles.clear();
  p.folios.clear();
  const std::size_t garde = ajouterFolio(p, TypeFolio::Garde, "Page de garde");
  const auto P = [](double x, double y) { return ecad::ptMm(x * 2.54, y * 2.54); };
  auto texte = [&](std::size_t f, double x, double y, const std::string& s, double taille = 2.5, int rotation = 0) {
    ecad::TexteSchema tx;
    tx.uuid = ecad::nouvelUuid();
    tx.texte = s;
    tx.p = P(x, y);
    tx.taille = ecad::nm(taille);
    tx.rotation = rotation;
    p.schema.feuilles[f].textes.push_back(tx);
  };
  // Page de garde : titre, renseignements et sommaire sont dessinés d'office ; le logement s'y décrit dessous
  // (textes écrits à partir de leur point)
  texte(garde, 18, 66, "Logement de " + detail::nombreFr(l.surface) + " m², " + std::to_string(l.pieces.size()) + " pièces, " + (l.triphase ? "triphasé" : "monophasé") +
                           ", abonnement " + detail::nombreFr(l.abonnementKVA) + " kVA",
        3.5);
  texte(garde, 18, 71, std::to_string(t.circuits.size()) + " circuits, " + std::to_string(t.differentiels.size()) + " interrupteurs différentiels 30 mA, coffret de " +
                           std::to_string(t.rangees) + " rangée(s) de 13 modules",
        3);
  texte(garde, 18, 75, "Conçu selon les règles de la NF C 15-100 (édition 2024) ; à faire valider par l'installateur et le Consuel.", 3);
  {
    double y = 80;
    for (const auto& r : t.remarques) {
      texte(garde, 18, y, "• " + r, 2.5);
      y += 3.5;
    }
  }

  // ----- Unifilaire : arrivée, disjoncteur de branchement, différentiels, départs -----
  std::size_t f = ajouterFolio(p, TypeFolio::Unifilaire, "Tableau de répartition");
  auto fil = [&](std::size_t feuille, ecad::Pt a, ecad::Pt b) {
    if (a == b) return;
    ecad::Fil w;
    w.uuid = ecad::nouvelUuid();
    w.a = a;
    w.b = b;
    p.schema.feuilles[feuille].fils.push_back(w);
  };
  auto composantDe = [&](std::size_t feuille, const std::string& symbole) -> ecad::ComposantElec* {
    for (const auto& s : p.schema.feuilles[feuille].symboles)
      if (s.uuid == symbole) return p.schema.composant(s.composant);
    return nullptr;
  };
  auto calibreAgcp = [&] {
    const double ka = l.abonnementKVA;
    const double a = l.triphase ? ka * 1000 / (std::sqrt(3.0) * 400) : ka * 1000 / 230;
    for (double c : {15.0, 30.0, 45.0, 60.0})
      if (c + 1e-9 >= a) return c;
    return 60.0;
  };
  // Grille en pas de 2,54 mm : AGCP, ligne de répartition, différentiels, rampes, disjoncteurs, départs
  constexpr double yBus = 18, yDiff = 23, yRampe = 29, yDisj = 34, yDepart = 41, xMax = 152;
  if (ecad::ComposantElec* k = composantDe(f, poserAppareil(p, f, "iec:u-agcp", P(10, 12), 0, "agcp-" + detail::nombreFr(calibreAgcp()))))
    k->champs["fonction"] = "Disjoncteur de branchement (AGCP)";
  {
    ecad::Etiquette e;
    e.uuid = ecad::nouvelUuid();
    e.texte = "Arrivée réseau";
    e.p = P(10, 9);
    e.type = ecad::TypeEtiquette::Locale;
    p.schema.feuilles[f].etiquettes.push_back(e);
  }
  fil(f, P(10, 15), P(10, yBus));
  // Parafoudre en tête, relié à la terre
  if (ecad::ComposantElec* k = composantDe(f, poserAppareil(p, f, "iec:parafoudre", P(14, 24), 0, "pf-2p"))) k->champs["fonction"] = "Parafoudre de type 2";
  fil(f, P(14, yBus), P(14, 21));
  {
    ecad::SymbolePlace terre;
    terre.uuid = ecad::nouvelUuid();
    terre.symbole = "iec:terre";
    terre.nomAlim = "PE";
    terre.position = P(14, 28);
    if (const ecad::DefSymbole* d = symboleElectrique("iec:terre")) p.schema.symboles["iec:terre"] = *d;
    p.schema.feuilles[f].symboles.push_back(terre);
  }
  double x = 22, xFin = 14, xDebutBus = 10;
  for (std::size_t d = 0; d < t.differentiels.size(); ++d) {
    std::vector<const CircuitLogement*> liste;
    for (const auto& c : t.circuits)
      if (c.differentiel == static_cast<int>(d)) liste.push_back(&c);
    if (liste.empty()) continue;
    const double largeur = 6.0 * static_cast<double>(liste.size() - 1);
    if (x + largeur > xMax && x > 22) {
      // Le folio est plein : la ligne de répartition continue sur le suivant par un renvoi
      fil(f, P(xDebutBus, yBus), P(xFin + 4, yBus));
      poserPotentiel(p, f, "Répartition", P(xFin + 4, yBus));
      f = ajouterFolio(p, TypeFolio::Unifilaire, "Tableau de répartition (suite)");
      poserPotentiel(p, f, "Répartition", P(8, yBus));
      xDebutBus = 8;
      x = 22;
      xFin = 8;
    }
    const auto& diff = t.differentiels[d];
    std::string ref = "id-2p-" + detail::nombreFr(diff.calibre) + "-" + diff.type;
    if (!entreeCatalogue(ref)) ref = "id-2p-40-" + diff.type;
    if (!entreeCatalogue(ref)) ref = "id-2p-40-A";
    if (ecad::ComposantElec* k = composantDe(f, poserAppareil(p, f, "iec:u-diff", P(x, yDiff), 0, ref))) {
      k->champs["fonction"] = "Différentiel 30 mA type " + diff.type;
      k->champs["calibre"] = detail::nombreFr(diff.calibre);
    }
    fil(f, P(x, yBus), P(x, yDiff - 3));
    fil(f, P(x, yDiff + 3), P(x, yRampe));
    if (liste.size() > 1) fil(f, P(x, yRampe), P(x + largeur, yRampe));
    for (std::size_t j = 0; j < liste.size(); ++j) {
      const CircuitLogement& c = *liste[j];
      const double bx = x + 6.0 * static_cast<double>(j);
      std::string reference = "dj-1pn-c" + detail::nombreFr(c.calibre);
      if (l.triphase && c.usage == "cuisson") reference = "dj-4p-c25";
      if (!entreeCatalogue(reference)) reference = "dj-1pn-c16";
      if (ecad::ComposantElec* k = composantDe(f, poserAppareil(p, f, "iec:u-disj", P(bx, yDisj), 0, reference))) {
        k->champs["fonction"] = c.nom;
        k->champs["section"] = detail::nombreFr(c.section);
        k->champs["calibre"] = detail::nombreFr(c.calibre);
        if (c.puissance > 0) k->champs["puissanceCircuit"] = detail::nombreFr(c.puissance);
      }
      fil(f, P(bx, yRampe), P(bx, yDisj - 3));
      poserAppareil(p, f, "iec:u-depart", P(bx, yDepart), 0);
      fil(f, P(bx, yDisj + 3), P(bx, yDepart - 2));
      // Désignation du départ, verticale sous la flèche : le texte part de son point (en bas) et monte jusqu'à elle
      const double l1 = static_cast<double>(c.nom.size()) * 0.95 / 2.54, l2 = 6.0;
      texte(f, bx - 0.7, yDepart + 1.2 + l1, c.nom, 1.7, 1);
      texte(f, bx + 1.0, yDepart + 1.2 + l2, detail::nombreFr(c.section) + " mm² " + detail::nombreFr(c.calibre) + " A", 1.5, 1);
    }
    xFin = x;
    x += largeur + 10;
  }
  fil(f, P(xDebutBus, yBus), P(std::max(xFin, xDebutBus), yBus));
  annoterElectrique(p, false);
  numeroterFils(p, true);
  // Liaison du disjoncteur de branchement à la tête du tableau : section selon son calibre
  {
    const ecad::Connectivite k = ecad::connectivite(p.schema);
    for (const auto& c : p.schema.composants)
      if (c.symbole == "iec:u-agcp") {
        const std::string nom = k.nom(c.uuid, "2");
        if (!nom.empty()) p.conducteurs[nom] = {calibreAgcp() <= 50 ? 10.0 : 16.0, {}, "H07V-R"};
      }
  }

  // ----- Coffret modulaire implanté -----
  Armoire a;
  a.uuid = ecad::nouvelUuid();
  a.nom = "Tableau de répartition";
  a.tableauModulaire = true;
  a.modulesParRangee = 13;
  a.marge = 25;
  a.largeur = 13 * 18 + 2 * 35;
  a.hauteur = t.rangees * 150 + 2 * 40;
  a.profondeur = 110;
  p.armoires.push_back(a);
  implanterArmoire(p, 0);
  // Folios dessinés d'office : le coffret à l'échelle et la liste du matériel, pour le dossier de l'installateur
  ajouterFolio(p, TypeFolio::Implantation, "Coffret");
  ajouterFolio(p, TypeFolio::Nomenclature, "Matériel");
  return p;
}

}  // namespace prom::elec

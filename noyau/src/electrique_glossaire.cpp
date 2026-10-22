// Prométhée : glossaire de l'électricien. Licence GPL-3.0-only.
#include "promethee/electrique_glossaire.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace prom::elec {

namespace {

// Minuscules sans accents (lettres du français), pour chercher sans se soucier de l'orthographe exacte
std::string simplifier(const std::string& s) {
  static const std::pair<const char*, char> accents[] = {{"à", 'a'}, {"â", 'a'}, {"ä", 'a'}, {"é", 'e'}, {"è", 'e'}, {"ê", 'e'}, {"ë", 'e'}, {"î", 'i'}, {"ï", 'i'},
                                                         {"ô", 'o'}, {"ö", 'o'}, {"ù", 'u'}, {"û", 'u'}, {"ü", 'u'}, {"ç", 'c'}, {"À", 'a'}, {"Â", 'a'}, {"É", 'e'},
                                                         {"È", 'e'}, {"Ê", 'e'}, {"Î", 'i'}, {"Ô", 'o'}, {"Û", 'u'}, {"Ç", 'c'}, {"œ", 'o'}};
  std::string r;
  for (std::size_t i = 0; i < s.size();) {
    bool remplace = false;
    for (const auto& [a, c] : accents) {
      const std::string x = a;
      if (s.compare(i, x.size(), x) == 0) {
        r += c;
        i += x.size();
        remplace = true;
        break;
      }
    }
    if (!remplace) {
      r += static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
      ++i;
    }
  }
  return r;
}

}  // namespace

const std::vector<TermeGlossaire>& glossaireElectrique() {
  static const std::vector<TermeGlossaire> g = {
      {"ib", "Courant d'emploi Ib",
       "Le courant que le circuit transporte en service normal, calculé à partir de la puissance des récepteurs (et de leurs facteurs d'utilisation et de "
       "simultanéité).",
       "Un radiateur de 2 000 W en 230 V appelle environ 8,7 A ; un moteur de 7,5 kW en 400 V, environ 15 A.", "NF C 15-100, 433.1 : Ib ≤ In ≤ Iz"},
      {"in", "Calibre In",
       "Le courant assigné de la protection (disjoncteur ou fusible) : au-delà, elle finit par couper. On le choisit juste au-dessus du courant d'emploi.",
       "Calibres courants : 10, 16, 20, 25, 32, 40, 63 A en modulaire ; 100 à 6 300 A en disjoncteur de puissance.", "NF C 15-100, 433.1"},
      {"ir", "Réglage Ir",
       "Sur un disjoncteur réglable (ou un disjoncteur moteur), le seuil de surcharge réglé à la main, entre 0,4 et 1 fois le calibre. Il remplace In dans les "
       "calculs.",
       "Un disjoncteur de 250 A réglé à 0,7 coupe les surcharges au-delà de 175 A.", "NF C 15-100, 433.1 : Ib ≤ Ir ≤ Iz"},
      {"iz", "Courant admissible Iz",
       "Le courant qu'un câble supporte en permanence sans chauffer au-delà de la limite de son isolant. Il dépend de la section, de l'isolant (PVC ou PR), "
       "de la façon dont le câble est posé, de la température ambiante et du nombre de circuits voisins.",
       "2,5 mm² cuivre sous conduit en plinthe : environ 24 A ; 16 mm² sur chemin de câbles : environ 100 A.",
       "NF C 15-100, partie 5-52 (CEI 60364-5-52, annexe B)"},
      {"k", "Facteur de correction K",
       "Le produit des coefficients qui réduisent le courant admissible d'un câble : température ambiante, câbles groupés, harmoniques, câbles en parallèle.",
       "Trois circuits jointifs : 0,70 ; 40 °C ambiants pour un câble PVC : 0,87.", "CEI 60364-5-52, tableaux B.52.14 à B.52.17"},
      {"section", "Section du conducteur",
       "L'aire de cuivre (ou d'aluminium) d'un conducteur, en mm². Plus elle est grande, plus le câble porte de courant et moins la tension chute.",
       "1,5 mm² pour l'éclairage, 2,5 mm² pour les prises 20 A, 6 mm² pour une plaque de cuisson 32 A.", "NF C 15-100, 524"},
      {"neutre", "Conducteur neutre",
       "Le conducteur qui ramène le courant des circuits monophasés vers la source. En triphasé équilibré il ne porte presque rien, sauf quand les "
       "récepteurs produisent des harmoniques de rang 3 (informatique, éclairage à LED) qui s'y additionnent.",
       "Avec 40 % d'harmonique 3, le neutre porte 1,2 fois le courant d'une phase.", "NF C 15-100, 524.2 et CEI 60364-5-52, annexe E"},
      {"pe", "Conducteur de protection PE",
       "Le conducteur vert-jaune qui relie les masses métalliques à la terre. En cas de défaut d'isolement, c'est lui qui écoule le courant et permet à la "
       "protection de couper.",
       "Même section que les phases jusqu'à 16 mm², 16 mm² jusqu'à 35 mm², la moitié au-delà.", "NF C 15-100, tableau 54F"},
      {"chute", "Chute de tension ΔU",
       "La tension perdue dans les câbles entre la source et le récepteur. Trop forte, elle fait mal fonctionner les appareils (lampes qui faiblissent, moteurs "
       "qui peinent).",
       "Limites usuelles : 3 % pour l'éclairage et 5 % pour le reste sur le réseau public ; 6 et 8 % depuis un poste de transformation privé.",
       "NF C 15-100, 525 et annexe G"},
      {"demarrage", "Démarrage d'un moteur",
       "À la mise sous tension, un moteur asynchrone appelle 6 à 8 fois son courant nominal pendant quelques secondes : la tension chute plus fort, et une "
       "protection trop sensible déclenche.",
       "Démarrage direct : Id ≈ 7 In ; étoile-triangle : ≈ 2,5 In ; démarreur progressif ou variateur : 2 à 3 In.", "UTE C 15-105 (chute ≤ 15 % au démarrage)"},
      {"icc", "Courant de court-circuit Icc",
       "Le courant qui circule quand deux conducteurs se touchent. Il est d'autant plus fort qu'on est près de la source. Le maximal sert à choisir le pouvoir "
       "de coupure ; le minimal, en bout de ligne, doit encore faire déclencher la protection.",
       "Aux bornes d'un transformateur de 630 kVA : environ 22 kA ; au bout de 50 m de 2,5 mm² : quelques centaines d'ampères.",
       "UTE C 15-105 (méthode des impédances)"},
      {"ik3", "Courant de court-circuit maximal Ik3",
       "Le court-circuit le plus fort, entre les trois phases, calculé avec les résistances des câbles froids. Il fixe le pouvoir de coupure des appareils.",
       "En monophasé, c'est le court-circuit entre phase et neutre qui est le plus fort.", "UTE C 15-105"},
      {"ikmin", "Courant de court-circuit minimal",
       "Le court-circuit le plus faible, au bout de la ligne, câbles chauds : entre phase et neutre (ou entre deux phases sans neutre). La protection doit "
       "encore couper instantanément pour ce courant.",
       "Un disjoncteur courbe C 16 A coupe instantanément à partir de 160 A.", "NF C 15-100, 434.2"},
      {"ief", "Courant de défaut à la masse Ief",
       "Le courant qui circule quand une phase touche une masse métallique reliée au PE. En schéma TN, il doit faire déclencher la protection assez vite "
       "pour que personne ne soit électrocuté en touchant l'appareil.",
       "Plus le câble est long et fin, plus ce courant est faible.", "NF C 15-100, 411.4"},
      {"pdc", "Pouvoir de coupure",
       "Le plus fort courant de court-circuit qu'un appareil sait couper sans être détruit. Il doit dépasser l'Icc maximal là où l'appareil est posé.",
       "Modulaires : 3 à 50 kA ; disjoncteurs de puissance : 25 à 150 kA ; fusibles gG : 100 kA. Dans un logement, 3 kA suffisent.",
       "NF C 15-100, 434.5.1"},
      {"ia", "Courant de fonctionnement Ia",
       "Le courant qui fait sûrement déclencher la protection dans le temps prescrit : le haut de la plage instantanée d'un disjoncteur (5, 10 ou 20 fois In "
       "selon la courbe), ou le courant de fusion d'un fusible.",
       "Disjoncteur courbe C 20 A : Ia = 200 A.", "UTE C 15-105"},
      {"lmax", "Longueur maximale protégée",
       "La plus grande longueur de câble pour laquelle un défaut à la masse fait encore déclencher la protection (méthode conventionnelle). Au-delà, il faut "
       "une section plus forte ou un différentiel.",
       "2,5 mm² protégé par un C 16 A en TN : environ 60 m.", "UTE C 15-105, méthode conventionnelle"},
      {"personnes", "Protection des personnes (contacts indirects)",
       "Le dispositif qui coupe le courant quand une masse métallique est mise sous tension par un défaut d'isolement, avant qu'une personne qui la touche "
       "ne soit en danger : différentiel en schéma TT, disjoncteur (ou différentiel) en TN et IT.",
       "En TT, RA × IΔn ≤ 50 V : avec une prise de terre de 100 Ω, un différentiel de 500 mA suffit.", "NF C 15-100, 411"},
      {"ddr", "Dispositif différentiel (DDR)",
       "Il compare le courant qui part et celui qui revient : s'il en manque (fuite vers la terre, ou à travers une personne), il coupe. 30 mA protège les "
       "personnes d'un contact direct ; 300 à 500 mA protège contre l'incendie et assure les contacts indirects.",
       "Type AC : courants alternatifs ; type A : aussi les courants redressés (plaques, lave-linge, bornes) ; type F : fréquences variables ; type B : "
       "courants continus lisses.",
       "NF C 15-100, 411.3.3 (prises : 30 mA)"},
      {"tt", "Schéma TT",
       "Le neutre de la source est relié à la terre, et les masses de l'installation à une autre prise de terre. Un défaut d'isolement crée un courant "
       "faible que seul un différentiel détecte. C'est le schéma des installations alimentées par le réseau public en France.",
       "Prise de terre de 30 à 100 Ω, différentiel en tête (500 mA) et 30 mA sur les circuits de prises.", "NF C 15-100, 411.5"},
      {"tn", "Schéma TN",
       "Le neutre de la source est relié à la terre et les masses au neutre par le PE : un défaut d'isolement devient un court-circuit, coupé par le "
       "disjoncteur. TN-S : neutre et PE séparés ; TN-C : un seul conducteur PEN (10 mm² cuivre au moins).",
       "Courant dans les postes privés, l'industrie et le tertiaire.", "NF C 15-100, 411.4"},
      {"it", "Schéma IT",
       "Le neutre de la source est isolé de la terre (ou relié par une forte impédance) : le premier défaut ne coupe rien, il est seulement signalé par un "
       "contrôleur permanent d'isolement. Un second défaut doit être coupé comme en TN. On l'emploie là où la continuité de service compte (hôpitaux, "
       "process).",
       "Contrôleur permanent d'isolement obligatoire.", "NF C 15-100, 411.6"},
      {"selectivite", "Sélectivité",
       "Quand un défaut survient sur un départ, seule sa protection doit couper, pas celle du tableau qui l'alimente. Elle s'obtient en écartant les seuils "
       "(ampèremétrique), en retardant l'amont (chronométrique) ou par les tables des constructeurs (énergétique).",
       "Réglages dans un rapport d'au moins 1,6 ; différentiels : l'amont retardé (S) et au moins deux fois moins sensible.", "NF C 15-100, 535"},
      {"i2t", "Contrainte thermique I²t",
       "L'énergie que laisse passer la protection pendant un court-circuit. Le câble doit la supporter sans que son isolant ne se dégrade : I²t ≤ k² S².",
       "k vaut 115 pour le cuivre isolé PVC, 143 pour le cuivre isolé PR.", "NF C 15-100, 434.5.2"},
      {"cosphi", "Facteur de puissance cos φ",
       "La part utile de la puissance appelée. Un moteur ou un transformateur consomme aussi de l'énergie réactive qui charge les câbles sans travailler ; "
       "un cos φ bas coûte en pertes et en pénalités.",
       "Résistances : 1 ; moteurs : 0,8 à 0,85 ; LED : 0,9 à 0,95.", "Compensation : relever le cos φ à 0,93 environ"},
      {"ku", "Facteur d'utilisation ku",
       "La part de sa puissance nominale qu'un récepteur appelle vraiment en service.", "Un moteur tourne rarement à pleine charge : ku ≈ 0,75.",
       "Bilan de puissance"},
      {"ks", "Facteur de simultanéité ks",
       "Tous les départs d'un tableau ne fonctionnent pas en même temps : on réduit la somme des puissances.",
       "2 à 3 départs : 0,9 ; 4 à 5 : 0,8 ; 6 à 9 : 0,7 ; 10 et plus : 0,6. Bornes de recharge : 1 sans gestion de la charge.", "NF C 15-100, 722.311"},
      {"h3", "Harmonique de rang 3",
       "Un courant à 150 Hz produit par l'électronique (alimentations, LED, informatique). Les harmoniques 3 des trois phases s'additionnent dans le neutre.",
       "Bureaux très équipés : 20 à 40 %.", "CEI 60364-5-52, annexe E"},
      {"pose", "Mode de pose",
       "La façon dont un câble est installé, qui décide de sa capacité à évacuer sa chaleur : sous conduit dans un mur isolé (A1), en goulotte (B), fixé au mur "
       "(C), sur chemin de câbles (E), enterré (D1).",
       "À section égale, un câble à l'air libre porte 30 à 40 % de plus qu'un câble encastré dans un isolant.", "CEI 60364-5-52, tableau B.52.1"},
      {"isolant", "Isolant PVC ou PR",
       "Le PVC supporte 70 °C en service ; le PR (polyéthylène réticulé, câbles U1000 R2V) 90 °C : il porte plus de courant à section égale.",
       "2,5 mm² sur paroi : 27 A en PVC, 33 A en PR (deux conducteurs chargés).", "CEI 60364-5-52"},
      {"source", "Source d'alimentation",
       "Ce qui fournit l'énergie : le réseau public (branchement), un transformateur privé, un groupe électrogène. Son impédance fixe le courant de "
       "court-circuit maximal de toute l'installation.",
       "Réseau public > 36 kVA : réseau conventionnel de la NF C 14-100 (transformateur de 1 000 kVA).", "NF C 14-100, UTE C 15-105"},
      {"ucc", "Tension de court-circuit ucc",
       "La caractéristique d'un transformateur qui fixe son impédance : plus elle est faible, plus le courant de court-circuit à ses bornes est fort.",
       "4 % jusqu'à 630 kVA, 6 % au-delà (transformateurs HTA/BT courants).", "NF EN 60076"},
      {"tgbt", "Tableau général basse tension (TGBT)",
       "Le premier tableau après la source : il reçoit l'arrivée et alimente les tableaux divisionnaires (TD) et les gros départs.",
       "Dans un logement, c'est le tableau de répartition.", "NF C 15-100"},
      {"compensation", "Compensation de l'énergie réactive",
       "Des condensateurs fournissent sur place l'énergie réactive des moteurs : le courant appelé baisse, le cos φ remonte.",
       "Qc = P × (tan φ − tan φ visé) ; batterie fixe si elle reste sous 15 % de la puissance du transformateur, automatique sinon.", "Compensation"},
      {"irve", "Infrastructure de recharge (IRVE)",
       "Les bornes de recharge de véhicules électriques. Chaque point de recharge a son circuit, son différentiel 30 mA de type A (avec détection des défauts "
       "continus dans la borne) ou B, et compte sans foisonnement.",
       "7,4 kW en monophasé 32 A ; 22 kW en triphasé 32 A.", "NF C 15-100, 722"},
      {"repere", "Repère d'appareil",
       "Le nom court qui identifie un appareil sur tous les documents : une lettre de fonction et un numéro (-Q1 disjoncteur, -KM1 contacteur, -M1 moteur, "
       "-X1 bornier).",
       "Selon la CEI 81346, on peut aussi employer des codes à deux lettres (QA, KF, MA…).", "CEI 81346-2"},
      {"folio", "Folio",
       "Une page du dossier de schémas, avec son cadre et son cartouche. Les renvois indiquent « folio.colonne » pour retrouver un contact ou un fil.",
       "3.4 : folio 3, colonne 4.", "CEI 61082"},
      {"renvoi", "Renvoi",
       "L'indication, à côté d'une bobine, des folios et colonnes où sont dessinés ses contacts (et inversement) ; ou l'étiquette qui fait passer un fil d'un "
       "folio à l'autre.",
       "Sous -KM1 : « 13-14 → 4.2 ».", "CEI 61082"},
      {"bornier", "Bornier",
       "La rangée de bornes où se raccordent les câbles qui sortent de l'armoire. Chaque borne a un numéro (-X1:3) ; un côté pour l'intérieur, un côté pour "
       "l'extérieur.",
       "Bornes de passage grises, bornes de terre vert-jaune.", "CEI 60947-7-1"},
      {"potentiel", "Potentiel",
       "Un nom donné à un fil qui le relie, sans le dessiner, à tous les fils du même nom (L1, N, PE, 24V, 0V…).", "Le même nom sur deux folios fait le renvoi.",
       "Règle de l'art"},
      {"unifilaire", "Schéma unifilaire",
       "La représentation d'un tableau où un seul trait figure toutes les phases d'un circuit : on y lit l'arborescence, les protections et les câbles.",
       "Le dossier d'un tableau de logement est un unifilaire.", "CEI 61082"},
      {"agcp", "Disjoncteur de branchement (AGCP)",
       "Le disjoncteur posé en tête d'une installation alimentée par le réseau public : il limite la puissance souscrite et porte le différentiel de "
       "500 mA retardé.",
       "Calibres 15/45 A ou 30/60 A en monophasé.", "NF C 14-100"},
      {"photovoltaique", "Installation photovoltaïque",
       "Des modules produisent du courant continu ; l'onduleur le transforme en alternatif. Les modules sont mis en série (chaînes) pour atteindre la tension "
       "de l'onduleur, puis les chaînes en parallèle.",
       "Un module de 425 Wc donne environ 31 V et 13,7 A au point de puissance maximale.", "UTE C 15-712-1, NF C 15-100 partie 7-712"},
      {"uoc", "Tension à vide Uoc",
       "La tension d'un module photovoltaïque à vide ; elle augmente par grand froid. La tension maximale d'une chaîne (Uoc × nombre de modules × coefficient "
       "de température) ne doit jamais dépasser celle que supportent l'onduleur et les modules.",
       "Coefficient de 1,14 à −10 °C, 1,2 par défaut.", "UTE C 15-712-1"},
      {"isc", "Courant de court-circuit Isc d'un module",
       "Le courant d'un module photovoltaïque court-circuité : à peine plus que son courant de fonctionnement. Les câbles et appareils du côté continu se "
       "dimensionnent sur 1,25 Isc.",
       "13 à 14 A pour un module courant.", "UTE C 15-712-1"},
      {"lcrit", "Longueur critique Lcrit",
       "La longueur de câbles continus au-delà de laquelle un parafoudre devient obligatoire du côté continu d'une installation photovoltaïque. Elle dépend de "
       "la densité de foudroiement Ng du lieu.",
       "Maison : Lcrit = 115 / Ng ; centrale au sol : 200 / Ng ; bâtiment tertiaire, industriel ou agricole : 450 / Ng.", "Guide UTE C 61-740-52"},
      {"kwc", "Puissance crête (kWc)",
       "La puissance que donnent les modules dans les conditions d'essai normalisées (1 000 W/m², cellules à 25 °C). Elle sert à comparer les installations ; la "
       "production réelle dépend du lieu, de l'orientation et de la chaleur.",
       "10 modules de 425 Wc font 4,25 kWc.", "CEI 61215"},
      {"chaine", "Chaîne (string)",
       "Des modules photovoltaïques branchés en série : leurs tensions s'additionnent, le courant reste celui d'un module. Le nombre de modules d'une chaîne est "
       "borné en haut par la tension à vide par grand froid, en bas par la plage MPPT de l'onduleur par forte chaleur.",
       "Une chaîne de 10 modules de 38,5 V à vide donne 421 V à −10 °C.", "UTE C 15-712-1"},
      {"mppt", "Entrée MPPT",
       "Une entrée de l'onduleur qui cherche sans cesse le point de puissance maximale de ce qui lui est branché. Deux pans de toit orientés différemment "
       "vont sur deux entrées distinctes ; les chaînes d'une même entrée doivent être identiques.",
       "Plage MPPT de 90 à 520 V, 16 A par entrée.", "Notice de l'onduleur"},
      {"onduleur", "Onduleur photovoltaïque",
       "Il transforme le courant continu des modules en courant alternatif synchronisé sur le réseau, et se déconnecte de lui-même quand le réseau disparaît "
       "(protection de découplage intégrée).",
       "Onduleur monophasé de 3,6 kVA à deux entrées MPPT.", "DIN VDE V 0126-1-1, EN 50549-1"},
      {"gpv", "Fusible gPV et courant inverse",
       "Quand plusieurs chaînes sont en parallèle, une chaîne en défaut reçoit le courant des autres. Au-delà de 1 + IRM / Isc chaînes (IRM : courant inverse "
       "que supporte le module), chaque chaîne reçoit des fusibles gPV sur ses deux polarités, entre 1,4 Isc et IRM.",
       "Isc 13,9 A et IRM 25 A : protection dès 3 chaînes, fusibles de 20 A.", "UTE C 15-712-1, CEI 60269-6"},
      {"productible", "Productible",
       "L'énergie produite par an pour 1 kWc installé, pertes comprises : elle dépend surtout de l'ensoleillement du lieu, puis de l'orientation et de "
       "l'inclinaison des modules (un coefficient la corrige).",
       "Environ 950 kWh/kWc dans le Nord, 1 400 sur la Méditerranée, plein sud à 30°.", "PVGIS (Commission européenne)"},
      {"ng", "Densité de foudroiement Ng",
       "Le nombre moyen d'impacts de foudre par km² et par an au lieu de l'installation. Au-delà de 2,5 (zone AQ2), la NF C 15-100 impose un parafoudre en "
       "tête de l'installation, comme lorsque le bâtiment porte un paratonnerre.",
       "1 à 2 sur la plus grande partie de la France, 3 et plus dans le Sud-Est et en montagne.", "NF C 15-100 443"},
      // ----- Grafcet -----
      {"grafcet", "Grafcet",
       "Le langage graphique qui décrit le comportement d'un automatisme : une suite d'étapes (ce que fait la machine) séparées par des transitions (ce "
       "qu'elle attend pour continuer). Il se lit de haut en bas et se programme ensuite dans l'automate.",
       "Chariot : repos (0), avance (1) quand on appuie sur départ cycle, retour (2) quand il touche le capteur de droite.", "NF EN CEI 60848"},
      {"etape", "Étape",
       "Une situation stable de la machine, dessinée par un carré numéroté ; elle est active ou inactive. L'étape initiale, au double carré, est active au "
       "démarrage. Une étape active porte un jeton.",
       "Étape 1 « avance » : tant qu'elle est active, le contacteur KM1 est alimenté.", "NF EN CEI 60848"},
      {"transition", "Transition",
       "Le passage d'une étape à la suivante, dessiné par un trait horizontal sur la liaison. Elle est franchie quand toutes les étapes d'avant sont actives "
       "et que sa réceptivité est vraie : les étapes d'avant se désactivent, celles d'après s'activent.",
       "Entre l'étape 0 et l'étape 1, la transition « dcy · a ».", "NF EN CEI 60848 (règles 2 à 5)"},
      {"receptivite", "Réceptivité",
       "La condition écrite à droite d'une transition : une expression logique des entrées (capteurs, boutons), avec ET (·), OU (+), NON (/), les fronts "
       "(↑ montant, ↓ descendant) et les temporisations (5s/X3 : 5 secondes après l'activation de l'étape 3).",
       "« dcy · a » : départ cycle appuyé ET chariot à gauche. « =1 » : toujours vraie.", "NF EN CEI 60848"},
      {"action", "Action",
       "Ce que fait une étape active, écrit dans un rectangle à sa droite. Continue : vraie tant que l'étape est active (KM1). Conditionnelle : seulement si "
       "une condition est vraie (KM1 si a). Mémorisée : une sortie mise à 1 ou à 0 à l'activation de l'étape, qui le reste ensuite (H1 := 1).",
       "Étape 2 « retour » : action continue KM2 ; étape 5 : H1 := 1 allume le voyant jusqu'à ce qu'une autre étape l'éteigne.", "NF EN CEI 60848"},
      {"divergence", "Divergence en OU, en ET",
       "En OU (trait simple) : une étape suivie de plusieurs transitions, une seule branche est prise selon la réceptivité vraie ; leurs réceptivités "
       "doivent s'exclure. En ET (double trait) : une transition qui active plusieurs étapes à la fois, des séquences qui se déroulent en parallèle et se "
       "rejoignent sur une convergence en ET.",
       "Tri : pièce grande → poussoir gauche, sinon poussoir droit (OU). Poste : perçage et taraudage en même temps (ET).", "NF EN CEI 60848"},
      {"st", "Texte structuré (ST)",
       "Le langage textuel des automates programmables défini par la CEI 61131-3, proche du Pascal. Un grafcet s'y traduit par un bit par étape, les "
       "conditions de franchissement et les équations d'activation.",
       "X1 := (X1 AND NOT Tr2) OR Tr1; KM1 := X1;", "CEI 61131-3"},
  };
  return g;
}

const TermeGlossaire* termeGlossaire(const std::string& cle) {
  for (const auto& t : glossaireElectrique())
    if (t.cle == cle) return &t;
  return nullptr;
}

std::vector<const TermeGlossaire*> chercherGlossaire(const std::string& texte) {
  std::vector<std::string> mots;
  {
    std::istringstream in(simplifier(texte));
    std::string m;
    while (in >> m) mots.push_back(m);
  }
  std::vector<const TermeGlossaire*> r;
  for (const auto& t : glossaireElectrique()) {
    const std::string tout = simplifier(t.cle + " " + t.terme + " " + t.definition + " " + t.exemple);
    if (std::all_of(mots.begin(), mots.end(), [&](const std::string& m) { return tout.find(m) != std::string::npos; })) r.push_back(&t);
  }
  return r;
}

}  // namespace prom::elec

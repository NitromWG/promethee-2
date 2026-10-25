// Prométhée : aide de l'atelier électronique (lot L2, tâche 2.17). Licence GPL-3.0-only.
#include "AideElectronique.hpp"

#include <map>

namespace aide {
namespace {
struct Regle {
  const char* code;
  const char* titre;
  const char* explication;
};
// Une ligne par code que le noyau peut rendre ; l'essai aide_regles le vérifie à chaque construction
constexpr Regle REGLES[] = {
    {"ERC-01", "Symbole inconnu", "Le schéma cite un symbole qui n’est ni dans le projet ni dans la bibliothèque. Remplace-le par un symbole de la bibliothèque."},
    {"ERC-02", "Composant non annoté", "Un repère contient encore « ? » : il n’a pas de numéro. « Annoter » donne à chaque composant le premier numéro libre."},
    {"ERC-03", "Composant sans empreinte", "Sans empreinte, le composant serait dans la nomenclature mais pas sur la carte. Choisis son empreinte, ou coche « hors carte »."},
    {"ERC-04", "Broche sans pastille", "Une broche du symbole n’a pas de pastille de même numéro dans l’empreinte : elle ne serait reliée à rien sur la carte."},
    {"ERC-05", "Empreinte inconnue", "L’empreinte nommée n’existe ni dans le projet ni dans le catalogue. Choisis-en une du catalogue."},
    {"ERC-06", "Repère en double", "Deux composants portent le même repère : la nomenclature et le montage les confondraient. Renumérote."},
    {"ERC-07", "Pastille sans broche", "Une pastille numérotée de l’empreinte ne correspond à aucune broche du symbole : elle ne sera reliée à rien. Ajoute la broche au symbole, ou choisis une empreinte dont la numérotation correspond (pastille centrale d’un QFN : broche « EP » ou « 0 »)."},
    {"ERC-08", "Empreinte d’une autre sorte", "Le composant passif est posé sur l’empreinte d’une autre sorte de composant (une inductance sur une empreinte de fusible, une résistance sur celle d’un condensateur) : les cotes et la sérigraphie ne conviennent pas. Choisis une empreinte de la même sorte dans la liste filtrée."},
    {"ERC-09", "Brochage à vérifier", "L’empreinte est d’une autre famille de boîtier que celle du symbole : une broche peut arriver sur la mauvaise pastille. Vérifie sur la fiche technique, puis coche « Brochage vérifié »."},
    {"ERC-10", "Broche non connectée", "Une broche ne touche aucun fil. Relie-la, ou pose une marque de non-connexion si elle doit rester libre. Une entrée ou une alimentation libre est une erreur."},
    {"ERC-11", "Marque sur une broche reliée", "Une marque de non-connexion est posée sur une broche pourtant reliée : l’une des deux est de trop."},
    {"ERC-12", "Broche « non connectée » reliée", "Le fabricant déclare cette broche sans liaison interne, mais elle est reliée : vérifie le brochage."},
    {"ERC-13", "Marque dans le vide", "Une marque de non-connexion ne touche ni broche ni bout de fil."},
    {"ERC-14", "Broche reliée au milieu d’un fil", "Un fil passe sur le bout d’une broche sans s’y arrêter : la liaison existe mais se voit mal. Arrête le fil sur la broche."},
    {"ERC-15", "Hors du cadre", "Un composant dépasse du cadre de la feuille : il ne serait pas imprimé."},
    {"ERC-16", "Étiquette dans le vide", "Une étiquette ne touche ni fil ni broche : elle ne nomme rien."},
    {"ERC-17", "Alimentation dans le vide", "Un symbole d’alimentation ne touche rien : il n’alimente aucun réseau."},
    {"ERC-18", "Broches au même point", "Les broches de deux symboles se touchent sans fil : la liaison est réelle mais invisible. Écarte-les et pose un fil."},
    {"ERC-19", "Hors de la grille", "Un point n’est pas sur la grille de 1,27 mm : il risque de ne pas rejoindre les broches. Déplace l’objet d’un pas."},
    {"ERC-20", "Conflit de broches", "Deux broches reliées se contredisent (deux sorties, une sortie et un collecteur ouvert…), selon la matrice des conflits du projet."},
    {"ERC-21", "Entrée non pilotée", "Une entrée ne reçoit rien : ni sortie, ni résistance de tirage, ni alimentation. Son niveau serait indéterminé."},
    {"ERC-22", "Réseau non alimenté", "Une broche d’alimentation est reliée à un réseau que rien n’alimente. Relie un connecteur, un régulateur, ou pose un drapeau d’alimentation."},
    {"ERC-23", "Court-circuit d’alimentations", "Deux alimentations différentes (GND et +5V…) sont sur le même réseau. Pour une masse en étoile voulue, pose une liaison de réseaux."},
    {"ERC-24", "Réseau à plusieurs noms", "Un réseau porte plusieurs noms : c’est voulu (alias) ou une étiquette relie deux réseaux par erreur."},
    {"ERC-25", "Mêmes noms, portées différentes", "Une étiquette locale et une étiquette globale portent le même nom sans être reliées : une locale ne rejoint jamais une globale."},
    {"ERC-26", "Valeur vide", "Un composant à monter n’a pas de valeur : la nomenclature en a besoin."},
    {"ERC-30", "Fil qui ne mène nulle part", "Un bout de fil ne touche rien. Prolonge-le jusqu’à une broche, ou supprime-le."},
    {"ERC-31", "Étiquette seule", "Une étiquette relie les fils qui portent le même nom ; celle-ci n’a pas de jumelle dans sa portée."},
    {"ERC-33", "Port de bloc sans étiquette", "Un port d’un bloc hiérarchique n’a pas d’étiquette hiérarchique de même nom dans la feuille qu’il désigne, ou l’inverse."},
    {"ERC-34", "Texte marqué au schéma", "Un texte du schéma porte une marque « à revoir » : règle ce qu’il signale, puis retire la marque."},
    {"ERC-35", "Variable sans valeur", "Un texte du schéma cite une variable ${…} qui n’a pas de valeur dans la feuille ni dans la conception."},
    {"ERC-32", "Entrée de bus hors du bus", "Un fil d’entrée relié au bus porte un nom qui n’en fait pas partie (D8 sur D[0..7]) : il ne rejoint aucun membre du bus."},
    {"ERC-40", "Unité non posée", "Une unité d’un symbole à plusieurs unités (une porte, un ampli) n’est posée nulle part : ses broches resteraient en l’air."},
    {"ERC-41", "Posé deux fois", "Une unité, ou un composant à une seule unité, est posé deux fois : ses broches seraient reliées en silence."},
    {"ERC-42", "Broche commune sur deux réseaux", "Une broche commune à plusieurs unités (l’alimentation d’un double ampli) est un seul point du boîtier ; reliée à deux réseaux différents selon l’unité, elle les court-circuiterait. Les deux réseaux sont nommés, aucun n’est réuni en silence."},
    {"ERC-43", "Une pièce, deux boîtiers", "Une même référence fabricant est posée avec deux empreintes différentes : à l’achat, une seule des deux conviendra."},
    {"ERC-45", "Électrolytique à l’envers", "Le « + » du condensateur polarisé est à un potentiel plus bas que son « − », d’après les alimentations de ses réseaux : il chaufferait ou éclaterait. Retourne-le."},
    {"ERC-44", "Composant générique", "Le composant est provisoire : décrit par ses exigences (« LDO 3,3 V 500 mA »), il n’a ni référence fabricant ni empreinte réelle. Lance « Standardiser » pour le remplacer par un article de la bibliothèque, ou choisis l’article à la main."},
    {"DRC-01", "Isolement", "Deux cuivres de réseaux différents sont plus proches que l’isolement de leur classe."},
    {"DRC-02", "Piste trop fine", "Une piste est plus fine que la largeur minimale."},
    {"DRC-03", "Perçage de via trop petit", "Une via est percée sous le perçage minimal du fabricant."},
    {"DRC-04", "Couronne de via", "L’anneau de cuivre autour du trou de la via est trop fin."},
    {"DRC-05", "Perçage de pastille", "Une pastille est percée sous le perçage minimal."},
    {"DRC-06", "Cuivre hors de la carte", "Du cuivre sort du contour ou entre dans une découpe : il serait coupé à la fabrication."},
    {"DRC-07", "Trop près du bord", "Du cuivre est plus près du bord que la distance permise."},
    {"DRC-08", "Empreinte inconnue", "Une empreinte posée sur la carte n’existe ni dans le projet ni dans le catalogue."},
    {"DRC-09", "Composant hors de la carte", "Un composant dépasse du contour de la carte."},
    {"DRC-10", "Encombrements qui se recouvrent", "Les corps de deux composants se recouvrent : ils ne pourraient pas être montés."},
    {"DRC-11", "Couronne de pastille", "L’anneau de cuivre autour du trou d’une pastille est trop fin."},
    {"DRC-12", "Perçages trop proches", "Deux trous sont trop près l’un de l’autre."},
    {"DRC-13", "Sérigraphie sur une pastille", "Un trait ou un texte de sérigraphie couvre une pastille : il gênerait la soudure."},
    {"DRC-14", "Perçages qui se recoupent", "Deux trous se chevauchent."},
    {"DRC-15", "Cuivre près d’un trou non métallisé", "Du cuivre est trop près d’un trou de fixation non métallisé."},
    {"DRC-16", "Via sans réseau", "Une via n’appartient à aucun réseau."},
    {"DRC-17", "Couche absente", "Un objet est sur une couche que l’empilage n’a pas."},
    {"DRC-20", "Liaisons non routées", "Des broches d’un même réseau ne sont pas encore reliées par du cuivre (le chevelu)."},
    {"DRC-21", "Court-circuit", "Deux réseaux différents se touchent par le cuivre."},
    {"DRC-22", "Bout de piste", "Une piste s’arrête dans le vide."},
    {"DRC-23", "Zone vide", "Une zone de cuivre ne contient plus rien après remplissage."},
    {"DRC-24", "Cuivre rempli", "Contrôle indépendant du cuivre des zones tel qu’il est rempli : isolement, trous, découpes."},
    {"DRC-25", "Via reliée à rien", "Une via ne touche aucune piste ni zone."},
    {"DRC-26", "Trop peu de rayons", "Une pastille est reliée à sa zone par trop peu de rayons thermiques."},
    {"DRC-27", "Rayons trop fins", "Les rayons thermiques sont plus fins que le minimum de la zone."},
    {"DRC-30", "Absent du circuit", "Un composant du schéma n’a pas d’empreinte sur la carte : mets à jour le circuit (F8)."},
    {"DRC-31", "Piste trop étroite pour son courant", "La largeur de la piste ne tient pas le courant de sa classe (IPC-2221)."},
    {"DRC-32", "Classe trop étroite", "La largeur d’une classe est sous sa recommandation."},
    {"DRC-33", "Piste plus étroite que sa classe", "Une piste est plus fine que la largeur de sa classe de réseau."},
    {"DRC-34", "Via plus petite que sa classe", "Une via est plus petite que celle de sa classe de réseau."},
    {"DRC-35", "Affectation de classe orpheline", "Une affectation vise un réseau, un motif ou une classe qui n’existe pas, ou elle est masquée."},
    {"DRC-37", "Via sur une simple face", "Une via sur une carte simple face devient un pont de fil à poser."},
    {"DRC-38", "CMS sur la face sans cuivre", "Des pastilles CMS sont sur la face sans cuivre d’une carte simple face : rien ne peut s’y souder."},
    {"DRC-39", "Absent du schéma", "Une empreinte du circuit n’a plus de composant au schéma."},
    {"DRC-40", "Empreinte différente", "L’empreinte posée sur la carte n’est pas celle que le schéma demande."},
    {"DRC-41", "Hors carte mais posé", "Un composant marqué « hors carte » a pourtant une empreinte sur la carte."},
    {"DRC-42", "Réseau disparu", "Du cuivre porte le nom d’un réseau qui n’existe plus au schéma."},
    {"DRC-43", "Liaison de réseaux sans pont", "Une liaison de réseaux est le seul endroit où deux réseaux se touchent (une masse en étoile) : sans piste d’une pastille à l’autre, ils ne se rejoignent pas sur la carte."},
    {"DRC-45", "Trop haut pour le boîtier", "Le composant touche le couvercle (ou sa lèvre) du boîtier que la carte pilote : abaisse-le, déplace-le, ou monte la hauteur du boîtier."},
    {"DRC-46", "Collision dans le boîtier", "Le composant heurte un pilier, le fond ou la tête d’une vis du boîtier que la carte pilote."},
    {"DRC-44", "Zone interdite", "Une piste, une via ou une zone entre dans une zone de règles qui l’interdit."},
    {"DRC-53", "Écart de paire hors bornes", "Les deux pistes d’une paire différentielle s’écartent ou se rapprochent au-delà des bornes de la paire."},
    {"DRC-54", "Accord hors tolérance", "Un réseau d’un groupe d’accord est trop court ou trop long (ou en retard) par rapport à la cible du groupe."},
    {"DRC-55", "Ligne de fuite trop courte", "Le chemin le long de la surface entre deux cuivres de tensions différentes est plus court que ce que demande la CEI 60664-1 ; une fente plus large que X l’allonge."},
    {"DRC-56", "Cuivre sans réseau", "Du cuivre n’appartient à aucun réseau."},
    {"DRC-57", "Couche non permise", "Une piste est sur une couche que la contrainte de son réseau ne permet pas."},
    {"DRC-58", "Trop de vias", "Un réseau a plus de vias que sa contrainte n’en permet."},
    {"DRC-59", "Objet interdit", "Une contrainte d’interdiction refuse ce genre d’objet à cet endroit."},
    {"DRC-60", "Via d’une seule couche", "Une via ne relie qu’une couche : elle ne sert à rien."},
    {"DRC-61", "Zones de même priorité", "Deux zones de réseaux différents se chevauchent avec la même priorité : le remplissage dépendrait de l’ordre du fichier."},
    {"DRC-62", "Plan coupé", "Un plan de masse ou d’alimentation est coupé en plusieurs morceaux : le courant de retour fait le tour."},
    {"DRC-63", "Cuivre mort", "Un morceau de cuivre d’un réseau n’est relié à aucune pastille."},
    {"DRC-64", "Rayons thermiques trop étroits", "Les rayons thermiques d’une zone sont plus fins que la piste la plus fine permise."},
    {"DRC-65", "Repères superposés", "Deux repères de sérigraphie se recouvrent."},
    {"DRC-66", "Texte dans le mauvais sens", "Un texte du dessous se lit en miroir vu de dessus, un texte du dessus à l’endroit : celui-ci ne suit pas sa face."},
    {"DRC-67", "Dessin sur le contour", "Un dessin ou un texte est sur la couche du contour de la carte : la fraise le suivrait."},
    {"DRC-68", "Reste de tracé", "Un segment de piste minuscule reste d’une modification : « Nettoyer les pistes » le fond dans son voisin."},
    {"DRC-69", "Longueur de réseau hors bornes", "Un réseau est plus long (ou plus court) que sa contrainte de longueur."},
    {"DRC-70", "Piste excentrée sur une via", "Une piste s’arrête sur une via loin de son centre : le raccord se casse au perçage."},
    {"DRC-71", "Pastille trop petite pour son trou", "La pastille est plus petite que son perçage : le foret ne laisserait pas de cuivre."},
    {"DRC-72", "Trou hors de sa pastille", "Le décalage du trou le fait sortir de sa pastille."},
    {"DRC-73", "Pastilles superposées", "Deux pastilles différentes d’une même empreinte se recouvrent : deux broches seraient en court-circuit."},
    {"DRC-74", "Fente trop courte", "Une fente plus courte que son diamètre n’est qu’un trou rond."},
    {"DRC-76", "Composants trop éloignés", "Deux objets que relie une contrainte de proximité sont plus loin l’un de l’autre qu’elle ne le permet."},
    {"DRC-77", "Perçages trop proches, même réseau", "Deux perçages, même d’un même réseau, sont trop près bord à bord : le foret casse la paroi."},
    {"DRC-78", "Isolement entre couches", "Deux cuivres de couches voisines, l’un au-dessus de l’autre, sont séparés par moins de diélectrique que la contrainte ne l’exige."},
    {"DRC-79", "Courant d’une branche", "Un tronçon du trajet d’une branche déclarée en courant est plus étroit que ce que demande l’IPC-2221, ou le trajet est introuvable."},
    {"DRC-80", "Sortie de pastille trop longue", "La piste qui mène une pastille CMS à sa via est plus longue que la contrainte ne le permet."},
    {"DRC-81", "Longueur parallèle", "Deux pistes courent côte à côte, à moins de l’écart donné, plus longtemps que la contrainte ne le permet (diaphonie)."},
    {"DRC-82", "Contre-perçage", "Un contre-perçage est impossible tel qu’il est déclaré, ou une piste reste reliée à une couche qu’il coupe."},
    {"DRC-83", "Texte marqué au circuit", "Un texte du circuit porte une marque « à revoir » (ou une erreur de ses variables) : règle ce qu’il signale, puis retire la marque."},
    {"DRC-84", "Variable sans valeur au circuit", "Un texte du circuit cite une variable ${…} qui n’a pas de valeur."},
    {"DRC-88", "Écart d’assemblage", "Deux composants sont plus proches que la règle d’assemblage ne le demande pour la machine de pose ou la retouche ; la valeur la plus prudente l’emporte."},
    {"DRC-89", "Orientation non permise", "Le composant est tourné d’un angle que la règle d’assemblage ne permet pas (soudure à la vague, chargeur)."},
    {"DRC-90", "Pont de vernis trop fin", "Le vernis restant entre deux pastilles voisines est plus fin que le pont permis : il se décollerait."},
    {"DRC-86", "Tronçon hors bornes", "Un tronçon d’un signal, de broche à broche (From-To), est plus long ou plus court que ses bornes."},
    {"DRC-87", "Ordre de raccordement", "Le cuivre d’un signal en chaîne ou en fly-by ne passe pas par ses broches dans l’ordre imposé."},
    {"DFM-01", "Contour invalide", "Le contour de la carte n’est pas fermé ou se recoupe."},
    {"DFM-10", "Isolement du fabricant", "L’isolement des règles est sous la capacité du fabricant choisi."},
    {"DFM-11", "Largeur du fabricant", "La largeur minimale des règles est sous la capacité du fabricant."},
    {"DFM-12", "Perçage du fabricant", "Le perçage minimal des règles est sous la capacité du fabricant."},
    {"DFM-13", "Couronne du fabricant", "La couronne minimale des règles est sous la capacité du fabricant."},
    {"DFM-14", "Bord du fabricant", "La distance au bord des règles est sous la capacité du fabricant."},
    {"DFM-15", "Pistes trop fines pour le fabricant", "Des pistes sont plus fines que ce que le fabricant sait graver."},
    {"DFM-16", "Vias trop petites pour le fabricant", "Des vias sont percées sous ce que le fabricant sait percer."},
    {"DFM-20", "Profil de fabricant", "Le profil de fabricant est inconnu, ou un profil personnalisé ne déclare pas toutes ses capacités."},
    {"DFM-21", "Piège à acide", "Deux cuivres se rejoignent en angle aigu : l’acide de gravure y stagne et ronge la jonction."},
    {"DFM-22", "Copeau de vernis", "Un ruban de vernis plus fin que ce que le fabricant sait tenir se détacherait."},
    {"DFM-23", "Écart pour la machine", "Deux composants sont trop proches pour la buse de la machine de pose."},
    {"DFM-24", "Écart pour la retouche", "Un composant est trop près d’un autre pour être dessoudé et retouché à la main."},
    {"DFM-25", "Soulèvement d’un petit passif", "Les pastilles d’un 0402 ou plus petit sont si différentes que le composant se dresserait à la refusion (effet tombstone)."},
    {"DFM-26", "Pâte sur une via", "Une ouverture de pâte recouvre une via non bouchée : la soudure s’y écoulerait."},
    {"DFM-27", "Ligne de nomenclature incomplète", "Une ligne de la nomenclature n’a pas ce que l’assembleur demande (référence fabricant, empreinte, quantité)."},
    {"DFM-28", "Profil de refusion", "Les articles demandent des profils de refusion incompatibles ; le plus contraignant est retenu."},
    {"DFM-29", "Pièce à étuver", "Un article de niveau de sensibilité à l’humidité MSL 3 ou plus doit être étuvé avant la refusion (J-STD-033)."},
    {"INT-01", "Symbole sans composant", "Un symbole du schéma ne désigne aucun composant : repose-le depuis la bibliothèque."},
    {"INT-02", "Composant sans symbole", "Un composant n’a plus de symbole dans le schéma."},
    {"INT-03", "Empreinte sans composant", "Une empreinte du circuit ne désigne aucun composant."},
    {"INT-04", "Conception vide", "La conception ne contient aucun composant."},
    {"PCB-01", "Circuit imprimé vide", "Le schéma a des composants mais le circuit n’en a encore aucun : « Mettre à jour depuis le schéma » (F8) les y fait venir."},
    {"BOM-01", "Valeur manquante", "Un composant de la nomenclature n’a pas de valeur."},
    {"BOM-02", "Sans référence fabricant", "Des composants n’ont ni fabricant ni référence fabricant : l’achat devra les choisir."},
    {"DOC-01", "Cartouche incomplet", "Un champ du cartouche manque (titre, auteur, date…)."},
    {"VER-01", "Conception publiée", "Une révision publiée est verrouillée : passe à une nouvelle révision pour la modifier."},
};

constexpr Mot MOTS[] = {
    {"réseau", "Ensemble des broches reliées électriquement, par des fils, des étiquettes ou des alimentations du même nom."},
    {"étiquette", "Nom posé sur un fil : deux fils qui portent le même nom sont reliés sans être tracés l’un vers l’autre."},
    {"symbole", "Dessin d’un composant au schéma : un corps et des broches numérotées."},
    {"empreinte", "Dessin d’un composant sur la carte : ses pastilles de cuivre, son contour, sa sérigraphie."},
    {"pastille", "Surface de cuivre où se soude une broche du composant ; percée pour un composant traversant."},
    {"piste", "Ruban de cuivre qui relie des pastilles sur une couche de la carte."},
    {"via", "Trou métallisé qui fait passer une piste d’une couche à l’autre."},
    {"plan de masse", "Grande surface de cuivre reliée à la masse, qui remplit la carte autour des pistes."},
    {"chevelu", "Lignes droites qui montrent les liaisons encore à router entre les pastilles d’un même réseau."},
    {"ERC", "Vérification électrique du schéma : broches libres, sorties en conflit, alimentation manquante…"},
    {"DRC", "Vérification des règles de dessin de la carte : isolements, largeurs, perçages, bords…"},
    {"annotation", "Attribution d’un repère numéroté (R1, C2…) à chaque composant."},
    {"nomenclature", "Liste des composants à acheter et à monter, avec leurs valeurs et leurs références."},
    {"sérigraphie", "Inscriptions peintes sur la carte : repères, contours des composants."},
    {"Gerber", "Format des fichiers que le fabricant de cartes utilise, une couche par fichier."},
    {"jonction", "Point qui relie deux fils qui se croisent ou se rejoignent en T."},
    {"drapeau d’alimentation", "Marque qui déclare qu’un réseau est alimenté par l’extérieur, pour la vérification électrique."},
    {"unité", "Partie d’un composant posée à part : une porte d’un 74HC00, un ampli d’un LM358."},
    {"article", "Composant réel, achetable : symbole, empreinte, brochage, valeur, fabricant et référence fabricant, fournisseurs."},
    {"référence fabricant", "Numéro de la pièce chez son fabricant (MPN, « RC0603FR-0710KL ») : il relie un composant importé à l’article de la bibliothèque."},
    {"IPC/DAC-2552", "Norme d’échange des propriétés d’un composant en XML (identification, boîtier, cotes, masse, environnement, fournisseurs)."},
    {"JEP30", "Modèle de pièce de la JEDEC, en XML : ici, la section des boîtiers (corps, broches, pas)."},
    {"netlist", "Liste des réseaux et des broches qu’ils relient : ce que s’échangent un schéma et une carte d’outils différents."},
    {"IDX", "Format d’échange entre la carte et la CAO mécanique : ligne de base, puis propositions acceptées ou refusées une à une."},
};
}  // namespace

QString titreRegle(const std::string& code) {
  for (const Regle& r : REGLES)
    if (code == r.code) return QString::fromUtf8(r.titre);
  return {};
}

QString explicationRegle(const std::string& code) {
  for (const Regle& r : REGLES)
    if (code == r.code) return QString::fromUtf8(r.explication);
  return {};
}

std::vector<std::string> codesExpliques() {
  std::vector<std::string> v;
  for (const Regle& r : REGLES) v.push_back(r.code);
  return v;
}

const std::vector<Mot>& lexique() {
  static const std::vector<Mot> v(std::begin(MOTS), std::end(MOTS));
  return v;
}

QString definition(const QString& mot) {
  for (const Mot& m : lexique())
    if (mot.compare(QString::fromUtf8(m.mot), Qt::CaseInsensitive) == 0) return QString::fromUtf8(m.definition);
  return {};
}

}  // namespace aide

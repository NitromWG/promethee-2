// Prométhée : reprise de l'existant (lot L10 de la 0.34 ; 10.19 ; ECH-25, ECH-26, ECH-28).
// Les composants des conceptions importées se rattachent aux articles de la bibliothèque par la référence du fabricant
// (MPN), sinon un article se crée, une seule fois par référence, avec son symbole et son empreinte. Les propriétés d'un
// article s'échangent en XML selon la structure de l'IPC/DAC-2552 (fiche de composant pour la définition par le
// modèle), son boîtier selon celle du JEP30 de la JEDEC (modèle de pièce, section des boîtiers). Une conception de
// référence de fabricant s'importe avec ses articles ; un dossier entier de projets se convertit en série, sans
// s'arrêter au premier échec, avec un rapport par projet et les articles communs rangés une seule fois dans la
// bibliothèque d'équipe ; un guide dit les termes et les gestes de l'ancien outil. Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"
#include "promethee/gestion_bibliotheque.hpp"
#include "promethee/import_commun.hpp"

namespace prom::ecad {

// ---------- Rattachement des articles ----------
// Référence du fabricant comparable : majuscules, sans espaces ; « 0 » et « O » restent distincts
std::string mpnNormalise(const std::string& mpn);
// Référence, fabricant et fournisseur lus dans les champs que les autres logiciels écrivent (« Manufacturer Part
// Number », « MPN », « MFR_PN », « PartNumber », « Manufacturer », « MF », « Supplier 1 »…), sans écraser ce que le
// composant porte déjà ; rend le nombre de composants complétés
int completerReferencesFabricant(ConceptionElec& c);
struct LigneRattachement {
  std::string reference, mpn, fabricant, article;
  std::string etat;  // « rattaché », « créé », « sans référence »
  std::string detail;
};
struct RattachementArticles {
  int rattaches = 0, crees = 0, sansReference = 0;
  std::vector<LigneRattachement> lignes;
  std::vector<std::string> articlesUtilises;  // identifiants, sans doublon, dans l'ordre de la conception
};
// Chaque composant (hors alimentations) à référence fabricant se rattache à l'article de « bibliotheque » qui porte
// cette référence, en propre ou en seconde source (même fabricant quand les deux le disent) ; sinon un article est
// créé, une seule fois par référence, avec la valeur, la description, les paramètres, la hauteur, le symbole et
// l'empreinte du composant (copiés dans la bibliothèque s'ils ne sont pas ceux de Prométhée ; renommés « ~2 » si un
// objet différent y porte déjà l'identifiant). Le composant prend l'article et sa version, l'article est copié dans la
// conception. « origine » nomme la catégorie des articles créés (« Repris de KiCad »).
RattachementArticles rattacherArticles(ConceptionElec& c, ContenuBibliotheque& bibliotheque, const std::string& origine = {});

// ---------- IPC/DAC-2552 : propriétés d'un article en XML ----------
// Fiche du composant : identification (fabricant, référence, numéro interne, cycle de vie, fiche technique),
// composition physique (boîtier, montage, nombre de broches, cotes, masse, modèle 3D), propriétés électriques,
// environnement (températures, sensibilité à l'humidité, RoHS), conditionnement, fournisseurs et prix par quantité,
// secondes sources ; les données propres à Prométhée (symbole, brochage) dans une extension nommée. Somme de contrôle
// SHA-256 du composant à la fin. « date » : horodatage écrit dans l'en-tête (vide : omis).
std::string xmlIpc2552(const Article& a, const DefEmpreinte* empreinte = nullptr, const std::string& date = {});
struct FicheIpc2552 {
  Article article;
  bool sommeJuste = true;          // somme de contrôle absente ou juste
  std::vector<std::string> alertes;
};
std::optional<FicheIpc2552> articleDepuisIpc2552(const std::string& xml, std::string* erreur = nullptr);
// Propriétés des fiches portées dans les articles de même référence : champs vides complétés, paramètres ajoutés,
// fournisseurs et secondes sources joints ; rend le nombre d'articles enrichis
int enrichirArticles(std::vector<Article>& articles, const std::vector<Article>& fiches);

// ---------- JEP30 : boîtier ----------
struct TerminalJep30 {
  std::string numero;
  double x = 0, y = 0, largeur = 0, longueur = 0, percage = 0, rotation = 0;  // mm, repère du boîtier, y vers le haut
  std::string forme = "rectangulaire";  // « rectangulaire », « rond », « oblong »
};
struct BoitierJep30 {
  std::string nom, type, montage = "CMS";  // type : famille JEDEC (« SO », « QFN », « BGA »…) ; montage : « CMS », « traversant »
  std::string fabricant, mpn;
  double longueur = 0, largeur = 0, hauteur = 0, pas = 0;  // corps (mm) ; pas : entre broches voisines, 0 si inconnu
  std::vector<TerminalJep30> terminaux;
};
BoitierJep30 boitierDepuisEmpreinte(const DefEmpreinte& e, const Article* a = nullptr);
DefEmpreinte empreinteDepuisBoitier(const BoitierJep30& b);  // identifiant « jep30:<nom> »
std::string xmlJep30(const BoitierJep30& b);
std::optional<BoitierJep30> lireJep30(const std::string& xml, std::string* erreur = nullptr);

// ---------- Conception de référence ----------
// Projet d'un fabricant (carte d'évaluation) : importé comme tout autre, ses composants rattachés ou créés en articles
// dans « bibliotheque », enrichis des fiches IPC/DAC-2552 jointes (textes XML) ; les boîtiers JEP30 joints entrent
// dans la bibliothèque comme empreintes
struct ImportReference {
  ImportTiers import;
  RattachementArticles articles;
  int fichesLues = 0, articlesEnrichis = 0, boitiers = 0;
  std::vector<std::string> alertes;
};
std::optional<ImportReference> importerConceptionReference(const std::string& nomFichier, const std::string& contenu, const LecteurVoisin& lire, ContenuBibliotheque& bibliotheque,
                                                           const std::vector<std::string>& fichesXml = {}, std::string* erreur = nullptr);

// ---------- Conversion d'un dossier en série ----------
struct ProjetAConvertir {
  std::string nom;      // nom du projet (nom de base du fichier principal)
  std::string fichier;  // chemin relatif au dossier, « / » pour séparateur
};
// Lecture d'un fichier du dossier (chemin relatif) ; rien s'il manque
using LecteurDossier = std::function<std::optional<std::string>(const std::string&)>;
// Projets d'un dossier d'après la liste de ses fichiers (chemins relatifs) : un par nom de base et par sous-dossier,
// le fichier principal pris dans l'ordre .kicad_pro, .PrjPcb, .kicad_sch, .sch, .kicad_pcb, .brd, .SchDoc, .PcbDoc,
// .pcb, puis les autres formats lus (.asc, .lay6, .lay, .cpa, .csa, .vsdx). Ne comptent pas à part : les documents
// d'un projet Altium (.PrjPcb), les feuilles d'un projet KiCad (.kicad_pro), les dossiers de sauvegarde ; et, si
// « lire » est donné, les feuilles filles qu'un schéma du même dossier appelle (KiCad 5 et suivants, gschem)
std::vector<ProjetAConvertir> projetsDuDossier(const std::vector<std::string>& fichiers, const LecteurDossier& lire = {});
struct RapportProjet {
  std::string projet, fichier, logiciel, erreur;
  bool reussi = false;
  int composants = 0, reseaux = 0, empreintes = 0, feuilles = 0;
  RattachementArticles articles;
  std::vector<std::string> fichiersLus, alertes;
  std::string texte() const;  // rapport lisible (Markdown), un par projet
};
struct BilanSerie {
  std::vector<RapportProjet> projets;
  int reussis = 0, echecs = 0;
  int articlesCrees = 0;    // articles ajoutés à la bibliothèque d'équipe
  int articlesCommuns = 0;  // articles utilisés par au moins deux projets, rangés une seule fois
  bool interrompu = false;
  std::string texte() const;
};
// Chaque projet converti est rendu à « ecrire » (enregistrement) avec son rapport ; une exception levée par
// l'import, le rattachement ou l'écriture marque le projet en échec et la série continue. « arret » levé : la série
// s'arrête avant le projet suivant. « progression » (fait, total) est appelée après chaque projet.
BilanSerie convertirEnSerie(const std::vector<ProjetAConvertir>& projets, ContenuBibliotheque& equipe, const LecteurDossier& lire,
                            const std::function<void(const RapportProjet&, const ConceptionElec&)>& ecrire = {},
                            const std::function<void(std::size_t, std::size_t)>& progression = {}, const std::atomic<bool>* arret = nullptr);

// ---------- Guide de l'ancien outil ----------
struct TermeGuide {
  std::string ancien, promethee, geste;  // terme de l'ancien outil, terme de Prométhée, geste ou touche qui le remplace
};
// Logiciels décrits : KiCad, EAGLE, Altium, PADS, OrCAD, EasyEDA, gEDA, Sprint-Layout, Protel Autotrax, P-CAD,
// CADSTAR, LTspice ; un nom inconnu rend les termes communs
const std::vector<TermeGuide>& guideAncienOutil(const std::string& logiciel);
std::vector<std::string> logicielsDuGuide();
std::string guideHtml(const std::string& logiciel);  // page HTML autonome : tableau des termes et des gestes

}  // namespace prom::ecad

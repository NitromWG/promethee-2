// Prométhée : travail à plusieurs, suite (lot L12 de la 0.34 ; 12.6, 12.11, 12.13, 12.15, 12.18, 12.20 ; DOC-10,
// STR-12, ECH-14, MEC-18, PRJ-22, PRJ-25 à PRJ-29, BIB-09, BIB-12, BIB-18, BIB-35, BIB-36, FAB-17).
// — La carte comme appareil d'un projet électrique : son connecteur devient un bornier dont chaque borne porte le
//   réseau de la carte et se câble au potentiel du même nom sur un folio ; la carte s'implante dans l'armoire.
// — « Publier en matériel libre » : sources, dossier de fabrication, visionneuse, licence (CERN-OHL…) et description
//   dans un paquet qui se rouvre tel quel sur un poste neuf ; licence déclarée par projet et par article.
// — Revue formelle numérotée sur une révision désignée par son empreinte SHA-256 : constats suivis jusqu'à leur
//   clôture, signatures datées des rôles, rapport PDF figé qui porte l'empreinte.
// — Bibliothèque d'équipe hors réseau : copie locale de travail, articles nouveaux reversés au retour avec leur
//   numéro de révision, sans doublon (référence fabricant normalisée), fusion à trois voies des articles modifiés.
// — Nomenclature commune du projet : une colonne par carte et par liaison du système, fils et câbles en lignes,
//   coût de N systèmes ; données d'achat par ligne lues d'un fichier, articles préférés des achats.
// — Liens entre disciplines (carte et boîtier, carte et armoire, pièce et carte) qui gardent l'empreinte de ce
//   qu'ils ont vérifié : un changement fait passer l'autre discipline « à revoir », avec ce qui a changé, jusqu'à
//   l'accusé de lecture ; faisceau entre cartes tiré de leurs connecteurs et de leurs réseaux.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "promethee/electrique.hpp"
#include "promethee/gestion_bibliotheque.hpp"
#include "promethee/modele.hpp"

namespace prom {

// Référence fabricant comparable : sans espaces, tirets, points, barres ni soulignés, en capitales (« lm-317 t » et
// « LM317T » sont la même pièce) ; sert aux doublons des bibliothèques et aux lignes de nomenclature
std::string referenceComparable(const std::string& mpn);

// ---------- La carte dans le projet électrique (12.6) ----------
struct CarteAppareil {
  std::string composant;                // UUID de l'appareil posé dans le projet électrique
  std::string symbole;                  // identifiant de sa définition (appareil personnel)
  std::vector<std::string> bornes;      // numéros des broches du connecteur, dans l'ordre
  std::vector<std::string> potentiels;  // potentiel câblé à chaque borne (vide : broche non reliée sur la carte)
  std::string erreur;
};
// « connecteur » : repère du connecteur de la carte (« J1 ») ; « renvois » : réseau de la carte → potentiel du
// projet électrique (« GND » → « 0V ») ; les autres réseaux gardent leur nom
CarteAppareil poserCarteDansFolio(elec::ProjetElectrique& pe, const ecad::ConceptionElec& carte, const std::string& connecteur, std::size_t folio, ecad::Pt position,
                                  const std::map<std::string, std::string>& renvois = {});
// La carte posée dans l'armoire à sa taille (contour de la carte), sur la plaque
bool implanterCarte(elec::ProjetElectrique& pe, std::size_t armoire, const std::string& composant, const ecad::ConceptionElec& carte, ecad::Pt position);

// ---------- Matériel libre (12.11) ----------
struct LicenceLibre {
  std::string spdx, nom, adresse;  // « CERN-OHL-S-2.0 », « CERN Open Hardware Licence Version 2 — Strongly Reciprocal »
  bool reciproque = false;
};
const std::vector<LicenceLibre>& licencesLibres();
const LicenceLibre* licenceLibre(const std::string& spdx);
// Avis de licence à mettre en tête des sources (texte recommandé par la licence, avec son adresse)
std::string avisLicence(const std::string& spdx, const std::string& projet, const std::string& detenteur, const std::string& annee);
struct OptionsMaterielLibre {
  std::string licence = "CERN-OHL-S-2.0";
  std::string detenteur, annee, description, version = "0.34";
};
struct PaquetMaterielLibre {
  std::string zip;                     // octets de l'archive
  std::vector<std::string> fichiers;   // chemins dans l'archive
  std::vector<std::string> alertes;    // ce qui manque (licence d'article inconnue, carte non fabricable…)
  std::string erreur;
};
// Sources (archive du projet), dossier de fabrication et visionneuse de chaque carte, LICENSE, README
PaquetMaterielLibre publierMaterielLibre(const Projet& p, const std::string& cheminProjet, const OptionsMaterielLibre& o);
// Paquet extrait dans « dossier » : chemin du projet rouvert, ou vide et la raison
std::string ouvrirPaquetMaterielLibre(const std::string& zip, const std::string& dossier, std::string* erreur = nullptr);

// ---------- Revue formelle (12.13) ----------
struct ConstatRevue {
  std::string id, objet, texte, auteur, date;
  std::string etat = "ouvert";  // « ouvert », « clos »
  std::string cloture, closPar, closLe;
  friend bool operator==(const ConstatRevue&, const ConstatRevue&) = default;
};
struct SignatureRevue {
  std::string role, nom, date;
  friend bool operator==(const SignatureRevue&, const SignatureRevue&) = default;
};
struct PointControleRevue {
  std::string texte, source, objet;  // source : « exigence EX-3 », « règle maison », « liste »
  bool coche = false;
  friend bool operator==(const PointControleRevue&, const PointControleRevue&) = default;
};
struct RevueFormelle {
  int numero = 0;
  std::string titre, revision, empreinte, date;  // empreinte : SHA-256 de la conception revue
  std::vector<ConstatRevue> constats;
  std::vector<PointControleRevue> points;
  std::vector<SignatureRevue> signatures;
  std::vector<std::string> rolesAttendus{"Concepteur", "Relecteur"};
  std::string empreintePdf;  // SHA-256 du rapport figé à la dernière signature
  bool close() const;        // toutes les signatures attendues posées
  friend bool operator==(const RevueFormelle&, const RevueFormelle&) = default;
};
// Empreinte d'une révision : SHA-256 de la conception écrite en JSON, sans ce qui ne la change pas (revues, notes)
std::string empreinteRevision(const ecad::ConceptionElec& c);
std::vector<RevueFormelle> revuesDe(const ecad::ConceptionElec& c);
void ecrireRevues(ecad::ConceptionElec& c, const std::vector<RevueFormelle>& r);
// Nouvelle revue numérotée sur la conception telle qu'elle est ; ses points de contrôle tirés des exigences du projet
// et de la liste maison
RevueFormelle ouvrirRevue(ecad::ConceptionElec& c, const std::string& titre, const std::string& date, const std::vector<PointControleRevue>& liste = {});
std::string ajouterConstat(ecad::ConceptionElec& c, int revue, const std::string& objet, const std::string& texte, const std::string& auteur, const std::string& date);
bool clore(ecad::ConceptionElec& c, int revue, const std::string& constat, const std::string& reponse, const std::string& par, const std::string& date);
// Signature d'un rôle ; refusée (raison rendue) tant qu'un constat est ouvert, ou si la conception a changé depuis
// l'ouverture de la revue (empreinte différente)
std::string signer(ecad::ConceptionElec& c, int revue, const std::string& role, const std::string& nom, const std::string& date);
// Rapport PDF de la revue : en-tête avec l'empreinte SHA-256, constats et clôtures, points cochés, signatures
std::string pdfRevue(const ecad::ConceptionElec& c, const RevueFormelle& r);
// Historique des révisions (cartouche) et des revues, sur toute la vie ou entre deux révisions
std::string pdfHistorique(const ecad::ConceptionElec& c, const std::string& depuis = {}, const std::string& jusqua = {});

// ---------- Bibliothèque d'équipe hors réseau (12.15) ----------
struct BilanReversement {
  std::vector<std::string> ajoutes;     // identifiants ajoutés à la bibliothèque d'équipe
  std::map<std::string, std::string> rattaches;  // article local → article d'équipe de même référence (pas de doublon)
  std::vector<std::string> modifies;    // articles mis à jour (version suivante)
  std::vector<std::string> conflits;    // modifiés des deux côtés : gardés tels quels dans l'équipe, signalés
  int symboles = 0, empreintes = 0, modeles = 0;
  std::string texte() const;
};
// Copie locale : l'état de la bibliothèque d'équipe au départ (« base ») et la copie de travail (« locale »)
struct CopieLocale {
  ecad::ContenuBibliotheque base, locale;
  std::string depuis;  // date de la copie
};
CopieLocale copierPourHorsReseau(const ecad::ContenuBibliotheque& equipe, const std::string& date);
// Reverse la copie locale dans la bibliothèque d'équipe : articles nouveaux ajoutés (ou rattachés à celui de même
// référence fabricant déjà publié par un autre poste), articles modifiés d'un seul côté repris à la version suivante,
// symboles, empreintes et modèles 3D emportés ; « locale » reçoit les identifiants retenus
BilanReversement reverser(ecad::ContenuBibliotheque& equipe, CopieLocale& copie);
std::string texteCopieLocale(const CopieLocale& c);
std::optional<CopieLocale> lireCopieLocale(const std::string& texte, std::string* erreur = nullptr);

// ---------- Nomenclature commune et données d'achat (12.18) ----------
struct DonneeAchat {
  std::string mpn, distributeur;
  int stock = -1, delaiJours = -1;   // −1 : inconnu
  bool agree = true;                  // distributeur agréé par le fabricant
  std::string risque;                 // « faible », « moyen », « élevé » (contrefaçon)
  double prix = 0;
};
// CSV (séparateur deviné) : mpn, distributeur, stock, délai, agréé, risque, prix
std::vector<DonneeAchat> lireDonneesAchat(const std::string& csv, std::string* erreur = nullptr);
struct LigneNomenclatureSysteme {
  std::string cle, genre, designation, mpn, fabricant, unite = "pièce";  // genre : « composant », « fil », « câble », « contact »
  std::map<std::string, double> parColonne;  // colonne → quantité par système
  double total = 0, prix = 0;                 // par système ; prix unitaire
  std::vector<DonneeAchat> achats;
  bool prefere = false;
};
struct NomenclatureSysteme {
  std::vector<std::string> colonnes;  // « Carte A », « Carte B », « W1 »
  std::vector<LigneNomenclatureSysteme> lignes;
  double cout(int systemes) const;    // aux prix connus
  std::vector<std::string> sansPrix() const;
  std::string csv() const;
};
// « prix » : clé de ligne ou référence fabricant → prix unitaire ; « achats » : données d'achat à rattacher
NomenclatureSysteme nomenclatureSysteme(const Projet& p, const std::map<std::string, double>& prix = {}, const std::vector<DonneeAchat>& achats = {},
                                        const std::set<std::string>& preferes = {});

// ---------- Liens entre disciplines (12.20) ----------
// LienDisciplines : voir modele.hpp (gardé dans le projet)
// Relevé de ce que le lien surveille (JSON) ; vide si l'objet a disparu
std::string releveLien(const Projet& p, const LienDisciplines& l);
// Liens vers « vers » (tous si vide) dont ce qu'ils surveillent a changé depuis l'accusé de lecture, sans rien
// modifier : l'arbre du projet marque ainsi l'autre discipline « à revoir »
std::vector<const LienDisciplines*> liensARevoir(const Projet& p, const std::string& vers = {});
// Nouveau lien, relevé et accusé tout de suite (« à jour »)
LienDisciplines lierDisciplines(const Projet& p, const std::string& de, const std::string& vers, const std::string& quoi, const std::string& personne, const std::string& date);
// Relève chaque lien : empreinte changée → « à revoir », avec les changements (« contour.largeur : 50 → 52 mm ») ;
// rend le nombre de liens passés « à revoir »
int verifierLiens(Projet& p);
// L'autre discipline a lu : relevé et empreinte repris, « à jour »
bool accuserLecture(Projet& p, const std::string& lien, const std::string& personne, const std::string& date);
// Faisceau d'une liaison du système (12.20) : un nœud par connecteur, une branche, un fil par broche reliée, au réseau
// de la carte A ; dessiné sur la planche de l'atelier électrique
struct FilFaisceau {
  std::string numero, de, vers, reseau;  // de « J3:1 » vers « J1:1 »
  double longueur = 0;                    // mm
};
struct FaisceauEntreCartes {
  elec::Faisceau faisceau;
  std::vector<FilFaisceau> fils;
  std::string erreur;
};
FaisceauEntreCartes faisceauDeLiaison(const Projet& p, const LiaisonSysteme& l, double longueur = 500);

}  // namespace prom

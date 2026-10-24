// Prométhée : documents PDF du module électronique (lot L8 de la 0.34 ; 8.11, 8.29 ; DOC-01, DOC-02, DOC-06, DOC-14,
// FAB-14). Un écrivain PDF du noyau (objets, flux, xref) sort le circuit par couches — une page par couche ou des
// couches superposées, couleurs, miroir du dessous, contour, filigrane, repères cherchables par le lecteur, empreintes
// cliquables (infobulle : repère, valeur, empreinte), signets, métadonnées XMP tirées du cartouche. Une mise à jour
// incrémentale ajoute à un PDF existant (celui du schéma, écrit par QPdfWriter) un signet par feuille et des liens
// entre étiquettes de même nom, sans réécrire le fichier ; un lecteur relit pages, signets, liens et métadonnées.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- Signets, liens, métadonnées ----------
struct SignetPdf {
  std::string titre;
  int page = 0;            // de 0
  double x = 0, y = 0;     // mm depuis le coin haut gauche de la page
  int niveau = 0;          // 0 : premier niveau ; 1 : sous le signet précédent de niveau 0
};
struct LienPdf {
  int page = 0;
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // mm, rectangle cliquable (coin haut gauche, coin bas droit)
  int pageCible = 0;
  double xCible = 0, yCible = 0;          // mm
  std::string infobulle;
};
struct MetaPdf {
  std::string titre, auteur, sujet, motsCles, revision, date, createur = "Prométhée";
};
std::string xmpPdf(const MetaPdf& m);
MetaPdf metaDepuisCartouche(const ConceptionElec& c);

// Mise à jour incrémentale d'un PDF à table xref classique : signets (dictionnaire /Outlines), liens (annotations
// /Link vers une destination), métadonnées XMP ; rend le PDF complété, ou le PDF intact si sa structure ne se lit pas
std::string completerPdf(const std::string& pdf, const std::vector<SignetPdf>& signets, const std::vector<LienPdf>& liens, const std::optional<MetaPdf>& meta = std::nullopt);

// Lecture (dernière révision, mises à jour incrémentales suivies)
struct LecturePdf {
  bool ok = false;
  std::string erreur;
  int pages = 0;
  std::vector<std::pair<double, double>> taillesPages;  // mm
  std::vector<std::string> signets;                       // titres, dans l'ordre
  int liens = 0, infobulles = 0;
  std::string xmp;
  int revisions = 0;
};
LecturePdf lirePdf(const std::string& pdf);

// ---------- Schéma ----------
// Un signet par feuille (et par bloc de hiérarchie) ; liens entre étiquettes globales et hiérarchiques de même nom,
// d'une feuille à l'autre (positions en mm sur le cadre de chaque feuille)
std::vector<SignetPdf> signetsSchema(const ConceptionElec& c);
std::vector<LienPdf> liensSchema(const ConceptionElec& c);

// ---------- Circuit par couches ----------
struct CouchePdf {
  Couche couche = Couche::CuivreDessus;
  std::string couleur = "#C83232";  // #RRGGBB
};
struct OptionsPdfCircuit {
  std::vector<CouchePdf> couches;  // vide : chaque cuivre, puis sérigraphies, vernis, contour
  bool superposer = false;         // toutes les couches sur une page, plutôt qu'une page par couche
  bool miroirDessous = true;       // une couche du dessous vue par transparence
  bool contour = true;
  bool texteCherchable = true;     // repères en texte invisible à leur place : le lecteur les trouve
  bool empreintesCliquables = true;
  std::string filigrane;           // vide : aucun
  double marge = 10;               // mm
  bool metaDonnees = true;
};
std::string pdfCircuit(const ConceptionElec& c, const OptionsPdfCircuit& o = {});

}  // namespace prom::ecad

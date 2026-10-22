// Prométhée : outil en ligne de commande (mode sans écran prévu par le CDC). Licence GPL-3.0-only.
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <fstream>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "promethee/derive.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/kicad.hpp"
#include "promethee/kicad_electronique.hpp"
#include "promethee/migrations.hpp"
#include "promethee/modele.hpp"
#include "promethee/verifs.hpp"
#include "promethee/version.hpp"

namespace fs = std::filesystem;
using namespace prom;

namespace {

void aide() {
  std::cout << ligneVersion() << ", outil en ligne de commande\n\n"
      "Utilisation :\n"
      "  promethee verifier <projet.prom.json>             vérifie la carte et le boîtier\n"
      "  promethee corriger <projet.prom.json> [sortie]    applique les corrections automatiques\n"
      "  promethee exporter <projet.prom.json> <dossier>   produit les pièces STEP et STL\n"
      "  promethee exemple <fichier.prom.json>             écrit le projet d’exemple\n"
      "  promethee kicad <carte.kicad_pcb>                 résume une carte KiCad (contour, trous, empreintes)\n"
      "  promethee importer-kicad <carte.kicad_pcb> <projet.prom.json>   crée le projet Prométhée de cette carte\n"
      "  promethee exporter-kicad <projet.prom.json> <carte.kicad_pcb>   écrit la carte pour KiCad (mise à jour de l’originale si elle existe)\n"
      "  promethee fabrication <projet.prom> <dossier> [n]   dossier de fabrication (Gerber, perçages, placement, nomenclature)\n"
      "                                                     de la conception électronique n (1 par défaut), contrôlé après écriture\n"
      "  promethee fabrication-exemple <dossier>           même chose pour la carte d’exemple (clignotant 555)\n"
      "  promethee exemple-electronique <projet.prom>      écrit le projet de la carte d’exemple (clignotant 555)\n"
      "  promethee importer-kicad-electronique <carte.kicad_pcb> <projet.prom>   crée la conception électronique de cette carte\n"
      "  promethee carte-mesure <composants> <projet.prom> [sans-zone]   écrit une carte de mesure (300, 1000, 2000…)\n"
      "  promethee pour-version <projet.prom> <sortie> [version]   écrit le projet pour une version précédente\n"
      "                                                     (la plus récente par défaut) et dit ce qui s’y perd\n\n"
      "Les fichiers .prom.json sont ceux du prototype web : un projet passe de l’un à l’autre.\n";
}

int afficherProblemes(const Projet& p, const std::vector<Probleme>& pb) {
  int erreurs = 0;
  const ControleBoitier controle = controleBoitier(p);
  if (!controle.complet) std::cout << controle.resume() << '\n';
  if (pb.empty()) {
    std::cout << (controle.complet ? "Tout est cohérent.\n" : "Aucun problème détecté, sous réserve des hauteurs à vérifier.\n");
    return 0;
  }
  for (const auto& q : pb) {
    if (q.sev == Severite::Erreur) ++erreurs;
    std::cout << (q.sev == Severite::Erreur ? "  erreur  " : "  alerte  ") << q.msg;
    if (q.fix) std::cout << "  [correction : " << q.fix->libelle << "]";
    std::cout << '\n';
  }
  std::cout << erreurs << (erreurs > 1 ? " erreurs, " : " erreur, ") << (pb.size() - erreurs)
            << (pb.size() - erreurs > 1 ? " alertes.\n" : " alerte.\n");
  return erreurs;
}

void resume(const Projet& p, const Derive& d) {
  std::cout << p.nom << " : carte de " << fmt(p.carte.L) << " × " << fmt(p.carte.W) << " mm, boîtier de "
            << fmt(d.Lo) << " × " << fmt(d.Wo) << " × " << fmt(d.ztop) << " mm couvercle compris, "
            << p.composants.size() << " composants, " << p.trous.size() << " trous.\n";
}

int verifierCmd(const std::string& chemin) {
  const Projet p = lireProjet(chemin);
  const Derive d = deriver(p);
  resume(p, d);
  return afficherProblemes(p, verifier(p, d)) > 0 ? 1 : 0;
}

int corrigerCmd(const std::string& chemin, const std::string& sortie) {
  Projet p = lireProjet(chemin);
  const auto faites = corrigerTout(p);
  for (const auto& f : faites) std::cout << "  " << f << '\n';
  std::cout << faites.size() << (faites.size() > 1 ? " corrections appliquées.\n" : " correction appliquée.\n");
  ecrireProjet(p, sortie);
  std::cout << "Écrit : " << sortie << '\n';
  return afficherProblemes(p, verifier(p, deriver(p))) > 0 ? 1 : 0;
}

int exporterCmd(const std::string& chemin, const std::string& dossier) {
  const Projet p = lireProjet(chemin);
  const Derive d = deriver(p);
  resume(p, d);
  const int erreurs = afficherProblemes(p, verifier(p, d));
  fs::create_directories(dossier);
  const TopoDS_Shape corps = construireCorps(p, d), couvercle = construireCouvercle(p, d);
  for (const auto& [nom, forme] : {std::pair<const char*, const TopoDS_Shape&>{"Boîtier", corps}, {"Couvercle", couvercle}}) {
    if (!estValide(forme) || nombreSolides(forme) != 1) {
      std::cerr << nom << " : la pièce générée n’est pas un solide sain.\n";
      return 2;
    }
  }
  const fs::path base(dossier);
  exporterStep({{"Boîtier", corps, 0.83, 0.85, 0.83}, {"Couvercle", couvercle, 0.90, 0.91, 0.90}}, (base / "boitier.step").string());
  exporterStl(pourImpression(corps, false, d.cx, d.cy), (base / "boitier.stl").string());
  exporterStl(pourImpression(couvercle, true, d.cx, d.cy), (base / "couvercle.stl").string());
  ecrireProjet(p, (base / "projet.prom.json").string());
  std::cout << "Boîtier : " << fmt(volume(corps) / 1000, 2) << " cm³, couvercle : " << fmt(volume(couvercle) / 1000, 2) << " cm³.\n"
            << "Écrit dans " << dossier << " : boitier.step (assemblage, deux pièces), boitier.stl, couvercle.stl, projet.prom.json.\n";
  return erreurs > 0 ? 1 : 0;
}

int kicadCmd(const std::string& chemin) {
  const CarteKicad c = lireFichierKicad(chemin);
  std::cout << (c.titre.empty() ? std::string("Carte KiCad") : c.titre) << " : " << fmt(c.xmax - c.xmin, 2) << " × " << fmt(c.ymax - c.ymin, 2)
            << " mm, épaisseur " << fmt(c.epaisseur, 2) << " mm.\n";
  if (c.contours.empty()) std::cout << "Aucun contour Edge.Cuts.\n";
  for (size_t i = 0; i < c.contours.size(); ++i) {
    const auto& k = c.contours[i];
    const auto arcs = std::count_if(k.elements.begin(), k.elements.end(), [](const ElementContour& e) { return e.milieu.has_value(); });
    std::cout << (i == 0 ? "  contour extérieur : " : "  découpe intérieure : ") << k.elements.size() << " éléments dont " << arcs << " arcs, "
              << (k.ferme ? "fermé" : "OUVERT") << ", " << fmt(std::abs(k.aire()), 1) << " mm².\n";
  }
  int fixations = 0;
  for (const auto& f : c.empreintes)
    if (f.fixation) {
      ++fixations;
      std::cout << "  trou de fixation " << f.ref << " : Ø " << fmt(f.percage, 2) << " mm en (" << fmt(f.position.x - c.xmin, 2) << " ; " << fmt(f.position.y - c.ymin, 2) << ")\n";
    }
  std::cout << c.empreintes.size() << " empreintes, dont " << fixations << " trous de fixation.\n";
  return 0;
}

// Écrit le dossier de fabrication, le relit et le contrôle. 0 : prêt ; 1 : écrit mais la carte a des erreurs de
// règles (DRC) ; 2 : le dossier écrit ne passe pas son contrôle.
int fabricationCmd(const ecad::ConceptionElec& c, const std::string& dossier) {
  const ecad::Connectivite k = ecad::connectivite(c);
  int erreurs = 0;
  for (const auto& v : ecad::verifierDRC(c, k, ecad::remplirZones(c, k))) erreurs += v.gravite == ecad::Gravite::Erreur ? 1 : 0;
  fs::create_directories(cheminFichier(dossier));
  const std::vector<std::string> fichiers = ecad::ecrireDossierFabrication(c, dossier, infosVersion().identifiant);
  std::cout << "Dossier de fabrication de « " << c.nom << " » : " << fichiers.size() << " fichiers dans " << dossier << '\n';
  for (const auto& f : fichiers) std::cout << "  " << f << '\n';
  const std::vector<std::string> defauts = ecad::controlerDossierFabrication(c, dossier);
  for (const auto& d : defauts) std::cerr << "  contrôle : " << d << '\n';
  if (!defauts.empty()) return 2;
  std::cout << "Contrôle du dossier relu : conforme.\n";
  if (erreurs > 0) std::cout << erreurs << (erreurs > 1 ? " erreurs" : " erreur") << " de règles (DRC) : à corriger avant d’envoyer au fabricant.\n";
  return erreurs > 0 ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  const std::vector<std::string> a(argv + 1, argv + argc);
  try {
    if (a.size() == 1 && (a[0] == "--version" || a[0] == "version")) {
      std::cout << ligneVersion() << "\nidentifiant " << infosVersion().identifiant << '\n';
      return 0;
    }
    if (a.size() == 2 && a[0] == "verifier") return verifierCmd(a[1]);
    if ((a.size() == 2 || a.size() == 3) && a[0] == "corriger") return corrigerCmd(a[1], a.size() == 3 ? a[2] : a[1]);
    if (a.size() == 3 && a[0] == "exporter") return exporterCmd(a[1], a[2]);
    if (a.size() == 2 && a[0] == "kicad") return kicadCmd(a[1]);
    if (a.size() == 3 && a[0] == "exporter-kicad") {
      const Projet p = lireProjet(a[1]);
      const Derive d = deriver(p);
      std::string texte;
      bool miseAJour = false;
      if (p.kicad && !p.kicad->fichier.empty()) {
        try {
          texte = kicadMisAJour(lireFichier(cheminFichier(p.kicad->fichier)), p, d);
          miseAJour = true;
        } catch (const ErreurFichier&) {
          miseAJour = false;  // carte d'origine introuvable : on en écrit une nouvelle
        }
      }
      if (!miseAJour) texte = kicadDepuisProjet(p, d);
      ecrireFichierAtomique(cheminFichier(a[2]), texte);
      std::cout << (miseAJour ? "Carte KiCad d’origine mise à jour (contour et trous) : " : "Nouvelle carte KiCad écrite : ") << a[2] << "\n";
      return 0;
    }
    if (a.size() == 3 && a[0] == "importer-kicad") {
      RapportImport r;
      Projet p = projetDepuisKicad(lireFichierKicad(a[1]), "", &r);
      if (p.kicad) p.kicad->fichier = utf8(std::filesystem::absolute(cheminFichier(a[1])));  // pour l'aller-retour
      ecrireProjet(p, a[2]);
      std::cout << "Projet écrit : contour réel, " << r.trous << " trous de fixation, " << r.composants << " composants (dont " << r.connecteursBord
                << " connecteurs de bord et " << r.dessous << " sur la face arrière).\n";
      return 0;
    }
    // Carte de mesure (AUT-12) : la taille voulue, toujours la même ; « sans-zone » retire le plan de masse
    if ((a.size() == 3 || a.size() == 4) && a[0] == "carte-mesure") {
      const int n = std::stoi(a[1]);
      if (n < 1 || n > 20000) {
        std::cerr << "Nombre de composants hors des bornes (1 à 20 000).\n";
        return 64;
      }
      Projet p = projetVide();
      p.sansCarte = true;
      p.electronique.push_back(ecad::carteDeMesure(n, !(a.size() == 4 && a[3] == "sans-zone")));
      p.nom = p.electronique.front().nom;
      ecrireProjet(p, a[2]);
      std::cout << "Écrit : " << a[2] << " (" << p.electronique.front().composants.size() << " composants, " << p.electronique.front().feuilles.size() << " feuille(s))\n";
      return 0;
    }
    if (a.size() == 2 && a[0] == "exemple-electronique") {
      Projet p = projetVide();
      p.sansCarte = true;
      p.nom = "Clignotant 555";
      p.electronique.push_back(ecad::exempleElectronique());
      ecrireProjet(p, a[1]);
      std::cout << "Écrit : " << a[1] << '\n';
      return 0;
    }
    if (a.size() == 3 && a[0] == "importer-kicad-electronique") {
      ecad::RapportImportKicad r;
      Projet p = projetVide();
      p.sansCarte = true;
      const std::string nom = utf8(cheminFichier(a[1]).stem());
      p.nom = nom;
      p.electronique.push_back(ecad::conceptionDepuisKicad(lireFichier(cheminFichier(a[1])), nom, &r));
      ecrireProjet(p, a[2]);
      std::cout << "Projet écrit : " << p.electronique.front().composants.size() << " composants, " << p.electronique.front().circuit.pistes.size() << " pistes.\n";
      return 0;
    }
    // Enregistrer pour une version précédente (1.22, ECH-11) : chaque perte sur une ligne « perte : … »
    if ((a.size() == 3 || a.size() == 4) && a[0] == "pour-version") {
      const Projet p = lireProjet(a[1]);
      const std::string version = a.size() == 4 ? a[3] : versionsAnterieures().front();
      const FichierAnterieur f = pourVersion(p, version);
      ecrirePourVersion(f, a[2]);
      std::cout << "Projet écrit pour la " << f.version << " : " << a[2] << '\n';
      if (f.pertes.empty()) std::cout << "Rien ne se perd.\n";
      for (const auto& x : f.pertes) std::cout << "perte : " << x << '\n';
      return 0;
    }
    if (a.size() == 2 && a[0] == "exemple") { ecrireProjet(exemple(), a[1]); std::cout << "Écrit : " << a[1] << '\n'; return 0; }
    if (a.size() == 2 && a[0] == "fabrication-exemple") return fabricationCmd(ecad::exempleElectronique(), a[1]);
    if ((a.size() == 3 || a.size() == 4) && a[0] == "fabrication") {
      const Projet p = lireProjet(a[1]);
      const std::size_t n = a.size() == 4 ? static_cast<std::size_t>(std::stoul(a[3])) : 1;
      if (n == 0 || n > p.electronique.size()) {
        std::cerr << "Le projet contient " << p.electronique.size() << " conception(s) électronique(s) : pas de numéro " << n << ".\n";
        return 64;
      }
      return fabricationCmd(p.electronique[n - 1], a[2]);
    }
    aide();
    return a.empty() || a[0] == "aide" || a[0] == "--help" ? 0 : 64;
  } catch (const std::exception& e) {
    std::cerr << "Erreur : " << e.what() << '\n';
    return 2;
  }
}

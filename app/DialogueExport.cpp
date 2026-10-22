// Prométhée : export du dossier de fabrication. Licence GPL-3.0-only.
#include <QDir>
#include "promethee/fichiers.hpp"
#include "DialogueExport.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>

#include "Document.hpp"
#include "promethee/echanges.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/kicad.hpp"
#include "promethee/nomenclature.hpp"

using namespace prom;

namespace {
struct Format { const char* cle; const char* groupe; const char* libelle; bool parDefaut; };
const Format FORMATS[] = {
    {"step", "Pièces exactes (CAO)", "STEP AP214 : assemblage nommé et coloré (Inventor, SolidWorks, Fusion, FreeCAD, KiCad)", true},
    {"iges", "Pièces exactes (CAO)", "IGES : anciens logiciels de CAO et d’usinage", false},
    {"brep", "Pièces exactes (CAO)", "BREP : format natif d’OpenCascade (FreeCAD, CadQuery)", false},
    {"stl", "Impression 3D", "STL binaire : une pièce par fichier, posée sur le plateau", true},
    {"stl_texte", "Impression 3D", "STL texte : même contenu, lisible dans un éditeur", false},
    {"3mf", "Impression 3D", "3MF : boîtier et couvercle côte à côte, prêts pour le trancheur", true},
    {"obj", "Rendu et visualisation", "OBJ avec ses couleurs (.mtl) : Blender, rendus, sites web", false},
    {"ply", "Rendu et visualisation", "PLY binaire avec couleurs par sommet", false},
    {"dxf", "Carte électronique", "DXF du contour et des perçages : à importer dans KiCad (Edge.Cuts)", true},
    {"svg", "Carte électronique", "SVG : plan coté de la carte à l’échelle 1", false},
    {"kicad", "Carte électronique", "KiCad (.kicad_pcb) : copie de la carte d’origine mise à jour, ou nouvelle carte avec contour et trous", true},
    {"csv", "Documents", "Nomenclature CSV (Excel, LibreOffice)", true},
    {"json", "Documents", "Projet Prométhée (.prom.json)", true},
};

// Noms de fichiers en ASCII ; le dossier peut contenir des accents (UTF-8 sous Windows grâce au manifeste).
std::string chemin(const QString& dossier, const char* nom) { return QDir(dossier).filePath(QString::fromLatin1(nom)).toStdString(); }
void ecrire(const std::string& f, const std::string& contenu) { prom::ecrireFichierAtomique(prom::cheminFichier(f), contenu); }
}  // namespace

DialogueExport::DialogueExport(Document* doc, QWidget* parent) : QDialog(parent), m_doc(doc) {
  setWindowTitle(QStringLiteral("Dossier de fabrication"));
  auto* colonne = new QVBoxLayout(this);
  auto* intro = new QLabel(QStringLiteral("Choisis les formats à produire. Tous sont tirés du même modèle : ils restent cohérents entre eux."));
  intro->setWordWrap(true);
  colonne->addWidget(intro);
  QSettings reglages;
  std::map<QString, QVBoxLayout*> groupes;
  for (const auto& f : FORMATS) {
    const QString g = QString::fromUtf8(f.groupe);
    if (!groupes.count(g)) {
      auto* boite = new QGroupBox(g);
      groupes[g] = new QVBoxLayout(boite);
      colonne->addWidget(boite);
    }
    auto* c = new QCheckBox(QString::fromUtf8(f.libelle));
    c->setChecked(reglages.value(QStringLiteral("export/") + f.cle, f.parDefaut).toBool());
    groupes[g]->addWidget(c);
    m_formats[QString::fromLatin1(f.cle)] = c;
  }
  auto* ligne = new QHBoxLayout;
  m_dossier = new QLineEdit(reglages.value(QStringLiteral("export/dossier"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString());
  auto* parcourir = new QPushButton(QStringLiteral("Parcourir…"));
  connect(parcourir, &QPushButton::clicked, this, [this] {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier de fabrication"), m_dossier->text());
    if (!d.isEmpty()) m_dossier->setText(d);
  });
  ligne->addWidget(new QLabel(QStringLiteral("Dossier")));
  ligne->addWidget(m_dossier, 1);
  ligne->addWidget(parcourir);
  colonne->addLayout(ligne);
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  boutons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Exporter"));
  boutons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("principal"));
  boutons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annuler"));
  connect(boutons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  colonne->addWidget(boutons);
}

QStringList DialogueExport::exporter() {
  QSettings reglages;
  for (const auto& [cle, c] : m_formats) reglages.setValue(QStringLiteral("export/") + cle, c->isChecked());
  const QString dossier = m_dossier->text();
  reglages.setValue(QStringLiteral("export/dossier"), dossier);
  std::filesystem::create_directories(prom::cheminFichier(dossier.toStdString()));
  auto choisi = [this](const char* cle) { return m_formats.at(QString::fromLatin1(cle))->isChecked(); };
  const Projet& p = m_doc->projet();
  const Derive& d = m_doc->derive();
  const TopoDS_Shape corps = construireCorps(p, d), couvercle = construireCouvercle(p, d), carte = construireCarte(p, d);
  const std::vector<PieceNommee> assemblage = {{"Boîtier", corps, 0.83, 0.85, 0.83}, {"Couvercle", couvercle, 0.90, 0.91, 0.90}, {"Carte", carte, 0.18, 0.42, 0.31}};
  const TopoDS_Shape corpsPlateau = pourImpression(corps, false, d.cx, d.cy), couverclePlateau = pourImpression(couvercle, true, d.cx, d.cy);
  gp_Trsf aCote;
  aCote.SetTranslation(gp_Vec(d.Lo + 10, 0, 0));
  const TopoDS_Shape couvercleACote = BRepBuilderAPI_Transform(couverclePlateau, aCote, true).Shape();
  const std::vector<PieceNommee> plateau = {{"Boîtier", corpsPlateau, 0.83, 0.85, 0.83}, {"Couvercle", couvercleACote, 0.90, 0.91, 0.90}};
  QStringList ecrits;
  auto note = [&ecrits](const char* nom) { ecrits << QString::fromUtf8(nom); };
  if (choisi("step")) { exporterStep(assemblage, chemin(dossier, "boitier.step")); note("boitier.step"); }
  if (choisi("iges")) { exporterIges(assemblage, chemin(dossier, "boitier.igs")); note("boitier.igs"); }
  if (choisi("brep")) { exporterBrep(assemblage, chemin(dossier, "boitier.brep")); note("boitier.brep"); }
  if (choisi("stl")) {
    exporterStl(corpsPlateau, chemin(dossier, "boitier.stl"));
    exporterStl(couverclePlateau, chemin(dossier, "couvercle.stl"));
    note("boitier.stl"); note("couvercle.stl");
  }
  if (choisi("stl_texte")) {
    exporterStlTexte(corpsPlateau, chemin(dossier, "boitier_texte.stl"));
    exporterStlTexte(couverclePlateau, chemin(dossier, "couvercle_texte.stl"));
    note("boitier_texte.stl"); note("couvercle_texte.stl");
  }
  if (choisi("3mf")) { exporter3mf(plateau, chemin(dossier, "impression.3mf")); note("impression.3mf"); }
  if (choisi("obj")) { exporterObj(assemblage, chemin(dossier, "boitier.obj")); note("boitier.obj et boitier.mtl"); }
  if (choisi("ply")) { exporterPly(assemblage, chemin(dossier, "boitier.ply")); note("boitier.ply"); }
  if (choisi("dxf")) { ecrire(chemin(dossier, "carte_contour.dxf"), dxfCarte(p, d)); note("carte_contour.dxf"); }
  if (choisi("svg")) { ecrire(chemin(dossier, "plan_carte.svg"), svgCarte(p, d)); note("plan_carte.svg"); }
  if (choisi("kicad")) {
    std::string texte;
    if (p.kicad && !p.kicad->fichier.empty()) {
      try {
        texte = kicadMisAJour(prom::lireFichier(prom::cheminFichier(p.kicad->fichier)), p, d);
      } catch (const prom::ErreurFichier&) {
        texte.clear();  // carte d'origine introuvable : nouvelle carte
      }
    }
    if (texte.empty()) texte = kicadDepuisProjet(p, d);
    ecrire(chemin(dossier, "carte.kicad_pcb"), texte);
    note("carte.kicad_pcb");
  }
  if (choisi("csv")) { ecrire(chemin(dossier, "nomenclature.csv"), versCsv(nomenclature(p, d))); note("nomenclature.csv"); }
  if (choisi("json")) { ecrireProjet(p, chemin(dossier, "projet.prom.json")); note("projet.prom.json"); }
  return ecrits;
}

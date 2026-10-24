// Prométhée : fenêtres mécatroniques de la 0.34 (lot L6). Voir FenetresMeca034.hpp.
// Licence GPL-3.0-only.
#include "FenetresMeca034.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "InspecteurElectronique.hpp"
#include "VuePiece.hpp"
#include "promethee/carte3d.hpp"
#include "promethee/distances3d.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QLineEdit* e) { return e->text().trimmed().toStdString(); }
QDoubleSpinBox* nombre(double min, double max, double v, int dec = 2, const QString& suffixe = QStringLiteral(" mm")) {
  auto* b = new QDoubleSpinBox;
  b->setRange(min, max);
  b->setDecimals(dec);
  b->setValue(v);
  b->setSuffix(suffixe);
  return b;
}
QString rouge(const QString& t) { return QStringLiteral("<span style='color:%1'>%2</span>").arg(teintes::erreur(), t.toHtmlEscaped()); }
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// 6.7 · Boîtier du commerce importé
DialogueBoitierImporte::DialogueBoitierImporte(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setObjectName(QStringLiteral("dialogueBoitierImporte"));
  setWindowTitle(QStringLiteral("Boîtier du commerce"));
  auto* col = new QVBoxLayout(this);
  auto* ligne = new QHBoxLayout;
  m_fichier = new QLabel(QStringLiteral("Aucun fichier"));
  auto* choisir = new QPushButton(QStringLiteral("Fichier STEP…"));
  ligne->addWidget(m_fichier, 1);
  ligne->addWidget(choisir);
  col->addLayout(ligne);
  auto* f = new QFormLayout;
  m_nom = new QLineEdit;
  m_fabricant = new QLineEdit;
  m_mpn = new QLineEdit;
  m_x = nombre(-2000, 2000, 0);
  m_y = nombre(-2000, 2000, 0);
  m_z = nombre(-2000, 2000, 0);
  m_rotation = nombre(-360, 360, 0, 1, QStringLiteral(" °"));
  m_jeu = nombre(0, 50, 0.5);
  f->addRow(QStringLiteral("Nom"), m_nom);
  f->addRow(QStringLiteral("Fabricant"), m_fabricant);
  f->addRow(QStringLiteral("Référence"), m_mpn);
  f->addRow(QStringLiteral("Place X"), m_x);
  f->addRow(QStringLiteral("Place Y"), m_y);
  f->addRow(QStringLiteral("Place Z"), m_z);
  f->addRow(QStringLiteral("Rotation"), m_rotation);
  f->addRow(QStringLiteral("Jeu minimal"), m_jeu);
  col->addLayout(f);
  m_bilan = inspecteur::texte();
  m_bilan->setObjectName(QStringLiteral("bilanBoitierImporte"));
  col->addWidget(m_bilan);
  auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  b->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Importer"));
  b->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annuler"));
  connect(choisir, &QPushButton::clicked, this, [this] {
    const QString c = QFileDialog::getOpenFileName(this, QStringLiteral("Boîtier du commerce"), {}, QStringLiteral("Modèles 3D (*.step *.stp *.iges *.igs *.brep)"));
    if (!c.isEmpty()) choisirFichier(c);
  });
  connect(b, &QDialogButtonBox::accepted, this, [this] {
    if (m_modele && appliquer() >= 0) accept();
  });
  connect(b, &QDialogButtonBox::rejected, this, &QDialog::reject);
  col->addWidget(b);
}

bool DialogueBoitierImporte::choisirFichier(const QString& chemin) {
  std::string erreur;
  m_modele = prom::lireModele3D(chemin.toStdString(), &erreur);
  if (!m_modele) {
    m_bilan->setText(rouge(QStringLiteral("« %1 » illisible : %2").arg(QFileInfo(chemin).fileName(), q(erreur))));
    return false;
  }
  m_fichier->setText(QFileInfo(chemin).fileName());
  if (m_nom->text().isEmpty()) m_nom->setText(QFileInfo(chemin).completeBaseName());
  // Place proposée : le modèle centré sur la carte, son fond 3 mm sous la face du dessous
  if (const ConceptionElec* c = m_atelier.conceptionCourante()) {
    const Boite b = boite(c->circuit.contour.polygone());
    const double cx = mm(b.x0 + b.x1) / 2, cy = mm(b.y0 + b.y1) / 2;
    m_x->setValue(cx - (m_modele->x0 + m_modele->x1) / 2);
    m_y->setValue(cy + (m_modele->y0 + m_modele->y1) / 2);
    m_z->setValue(-c->circuit.empilage.epaisseur - 3 - m_modele->z0);
  }
  m_bilan->setText(QStringLiteral("%1 × %2 × %3 mm.").arg(m_modele->x1 - m_modele->x0, 0, 'f', 1).arg(m_modele->y1 - m_modele->y0, 0, 'f', 1).arg(m_modele->z1 - m_modele->z0, 0, 'f', 1));
  return true;
}

void DialogueBoitierImporte::reglerPlace(double x, double y, double z, double rotation, double jeu) {
  m_x->setValue(x);
  m_y->setValue(y);
  m_z->setValue(z);
  m_rotation->setValue(rotation);
  m_jeu->setValue(jeu);
}

int DialogueBoitierImporte::appliquer() {
  if (!m_modele) return -1;
  BoitierImporte b;
  b.modele = "boitier:" + (s(m_nom).empty() ? std::string("importe") : s(m_nom));
  b.nom = s(m_nom);
  b.fabricant = s(m_fabricant);
  b.mpn = s(m_mpn);
  b.x = m_x->value();
  b.y = m_y->value();
  b.z = m_z->value();
  b.rotation = m_rotation->value();
  b.jeu = m_jeu->value();
  const prom::ecad::Modele3D m = *m_modele;
  m_atelier.modifierConception([&](ConceptionElec& c) {
    c.modeles3D[b.modele] = m;
    c.boitierImporte = b;
  }, QStringLiteral("Boîtier du commerce importé"));
  int n = 0;
  if (const ConceptionElec* c = m_atelier.conceptionCourante())
    for (const auto& v : prom::verifierDistances3D(*c)) n += v.code == "DRC-47" ? 1 : 0;
  m_atelier.afficherMessage(QStringLiteral("Boîtier <b>%1</b> importé pour les contrôles : %2 collision(s) avec les corps de la carte.").arg(q(b.nom).toHtmlEscaped()).arg(n),
                            n ? NiveauMessage::Erreur : NiveauMessage::Succes);
  return n;
}

// ---------------------------------------------------------------------------------------------------------------
// 6.8 · Pièce montée
DialoguePieceMontee::DialoguePieceMontee(AtelierElectronique& a, const std::string& composant, QWidget* parent) : QDialog(parent), m_atelier(a), m_composant(composant) {
  setObjectName(QStringLiteral("dialoguePieceMontee"));
  setWindowTitle(QStringLiteral("Pièce montée sur la carte"));
  auto* col = new QVBoxLayout(this);
  auto* ligne = new QHBoxLayout;
  m_fichier = new QLabel(QStringLiteral("Aucun fichier"));
  auto* choisir = new QPushButton(QStringLiteral("Fichier STEP…"));
  ligne->addWidget(m_fichier, 1);
  ligne->addWidget(choisir);
  col->addLayout(ligne);
  auto* f = new QFormLayout;
  m_reference = new QLineEdit(QStringLiteral("DS1"));
  m_nom = new QLineEdit;
  m_fabricant = new QLineEdit;
  m_mpn = new QLineEdit;
  m_fournisseur = new QLineEdit;
  m_z = nombre(-100, 100, 0);
  m_quantite = new QSpinBox;
  m_quantite->setRange(1, 1000);
  f->addRow(QStringLiteral("Repère"), m_reference);
  f->addRow(QStringLiteral("Désignation"), m_nom);
  f->addRow(QStringLiteral("Fabricant"), m_fabricant);
  f->addRow(QStringLiteral("Référence"), m_mpn);
  f->addRow(QStringLiteral("Fournisseur"), m_fournisseur);
  f->addRow(QStringLiteral("Au-dessus de la face"), m_z);
  f->addRow(QStringLiteral("Quantité"), m_quantite);
  col->addLayout(f);
  m_bilan = inspecteur::texte();
  col->addWidget(m_bilan);
  auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  b->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Poser"));
  b->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annuler"));
  connect(choisir, &QPushButton::clicked, this, [this] {
    const QString c = QFileDialog::getOpenFileName(this, QStringLiteral("Modèle de la pièce"), {}, QStringLiteral("Modèles 3D (*.step *.stp *.iges *.igs *.brep)"));
    if (!c.isEmpty()) choisirFichier(c);
  });
  connect(b, &QDialogButtonBox::accepted, this, [this] {
    if (!appliquer().empty()) accept();
  });
  connect(b, &QDialogButtonBox::rejected, this, &QDialog::reject);
  col->addWidget(b);
}

bool DialoguePieceMontee::choisirFichier(const QString& chemin) {
  std::string erreur;
  m_modele = prom::lireModele3D(chemin.toStdString(), &erreur);
  if (!m_modele) {
    m_bilan->setText(rouge(QStringLiteral("« %1 » illisible : %2").arg(QFileInfo(chemin).fileName(), q(erreur))));
    return false;
  }
  m_fichier->setText(QFileInfo(chemin).fileName());
  if (m_nom->text().isEmpty()) m_nom->setText(QFileInfo(chemin).completeBaseName());
  return true;
}

void DialoguePieceMontee::reglerArticle(const QString& reference, const QString& nom, const QString& fabricant, const QString& mpn) {
  m_reference->setText(reference);
  m_nom->setText(nom);
  m_fabricant->setText(fabricant);
  m_mpn->setText(mpn);
}

std::string DialoguePieceMontee::appliquer() {
  if (!m_modele || s(m_nom).empty()) {
    m_bilan->setText(rouge(m_modele ? QStringLiteral("Donne sa désignation.") : QStringLiteral("Choisis le fichier de la pièce.")));
    return {};
  }
  PieceMontee p;
  p.uuid = nouvelUuid();
  p.reference = s(m_reference);
  p.nom = s(m_nom);
  p.fabricant = s(m_fabricant);
  p.mpn = s(m_mpn);
  p.fournisseur = s(m_fournisseur);
  p.quantite = m_quantite->value();
  p.composant = m_composant;
  p.modele = "piece:" + p.uuid;
  p.z = m_z->value();
  const ConceptionElec* c0 = m_atelier.conceptionCourante();
  if (!c0) return {};
  // Sur le composant visé (son point de placement), sinon au centre de la carte
  const Boite b = boite(c0->circuit.contour.polygone());
  p.x = mm(b.x0 + b.x1) / 2;
  p.y = mm(b.y0 + b.y1) / 2;
  for (const auto& e : c0->circuit.empreintes)
    if (e.composant == m_composant) {
      p.x = mm(e.position.x);
      p.y = mm(e.position.y);
      p.dessous = e.dessous;
    }
  const Modele3D m = *m_modele;
  m_atelier.modifierConception([&](ConceptionElec& c) {
    c.modeles3D[p.modele] = m;
    c.piecesMontees.push_back(p);
  }, QStringLiteral("Pièce montée %1").arg(q(p.reference)));
  m_atelier.afficherMessage(QStringLiteral("<b>%1</b> (%2) posée sur la carte : elle va dans la nomenclature de la carte et dans la nomenclature commune.").arg(q(p.reference).toHtmlEscaped(), q(p.nom).toHtmlEscaped()),
                            NiveauMessage::Succes);
  return p.uuid;
}

// ---------------------------------------------------------------------------------------------------------------
// 6.11 · Apparence
DialogueApparence3D::DialogueApparence3D(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setObjectName(QStringLiteral("dialogueApparence3D"));
  setWindowTitle(QStringLiteral("Apparence de la 3D"));
  const ApparenceCarte3D& ap = a.apparence3D();
  auto* col = new QVBoxLayout(this);
  auto* f = new QFormLayout;
  m_vernis = new QComboBox;
  m_vernis->addItem(QStringLiteral("Celui de l’empilage"), QString());
  for (const char* v : {"vert", "noir", "bleu", "rouge", "blanc", "violet"}) m_vernis->addItem(QString::fromUtf8(v), QString::fromUtf8(v));
  m_vernis->setCurrentIndex(std::max(0, m_vernis->findData(q(ap.vernis))));
  f->addRow(QStringLiteral("Vernis"), m_vernis);
  col->addLayout(f);
  col->addWidget(new QLabel(QStringLiteral("Familles montrées :")));
  m_familles = new QListWidget;
  std::set<std::string> familles;
  if (const ConceptionElec* c = a.conceptionCourante())
    for (const auto& e : c->circuit.empreintes)
      if (const DefEmpreinte* d = c->empreinte(e.empreinte)) familles.insert(d->famille.empty() ? std::string("Autres") : d->famille);
  for (const auto& fam : familles) {
    auto* it = new QListWidgetItem(q(fam), m_familles);
    it->setCheckState(ap.famillesMasquees.count(fam) ? Qt::Unchecked : Qt::Checked);
  }
  col->addWidget(m_familles, 1);
  m_boites = new QCheckBox(QStringLiteral("Boîtes à la place des modèles exacts"));
  m_boites->setChecked(ap.boites);
  m_pieces = new QCheckBox(QStringLiteral("Pièces montées"));
  m_pieces->setChecked(ap.pieces);
  m_boitier = new QCheckBox(QStringLiteral("Boîtier importé"));
  m_boitier->setChecked(ap.boitierImporte);
  for (QCheckBox* k : {m_boites, m_pieces, m_boitier}) col->addWidget(k);
  auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(b, &QDialogButtonBox::accepted, this, [this] {
    appliquer();
    accept();
  });
  connect(b, &QDialogButtonBox::rejected, this, &QDialog::reject);
  col->addWidget(b);
}

void DialogueApparence3D::appliquer() {
  ApparenceCarte3D ap;
  ap.vernis = m_vernis->currentData().toString().toStdString();
  for (int i = 0; i < m_familles->count(); ++i)
    if (m_familles->item(i)->checkState() != Qt::Checked) ap.famillesMasquees.insert(m_familles->item(i)->text().toStdString());
  ap.boites = m_boites->isChecked();
  ap.pieces = m_pieces->isChecked();
  ap.boitierImporte = m_boitier->isChecked();
  m_atelier.reglerApparence3D(ap);
}

// ---------------------------------------------------------------------------------------------------------------
// 6.18 · Rendu
FenetreRendu::FenetreRendu(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreRendu"));
  setWindowTitle(QStringLiteral("Rendu de la carte"));
  setModal(false);
  auto* col = new QVBoxLayout(this);
  auto* f = new QFormLayout;
  auto* taille = new QHBoxLayout;
  m_largeur = new QSpinBox;
  m_largeur->setRange(16, 8192);
  m_largeur->setValue(1920);
  m_hauteur = new QSpinBox;
  m_hauteur->setRange(16, 8192);
  m_hauteur->setValue(1080);
  taille->addWidget(m_largeur);
  taille->addWidget(new QLabel(QStringLiteral("×")));
  taille->addWidget(m_hauteur);
  f->addRow(QStringLiteral("Taille"), taille);
  m_echantillons = new QSpinBox;
  m_echantillons->setRange(1, 4);
  m_echantillons->setValue(1);
  m_echantillons->setToolTip(QStringLiteral("Rayons par côté de pixel : 2 lisse les bords (quatre fois plus long)"));
  f->addRow(QStringLiteral("Lissage"), m_echantillons);
  col->addLayout(f);
  m_transparent = new QCheckBox(QStringLiteral("Fond transparent"));
  m_transparent->setChecked(true);
  m_ombres = new QCheckBox(QStringLiteral("Ombres"));
  m_ombres->setChecked(true);
  m_reflets = new QCheckBox(QStringLiteral("Reflets"));
  m_reflets->setChecked(true);
  auto* cases = new QHBoxLayout;
  for (QCheckBox* k : {m_transparent, m_ombres, m_reflets}) cases->addWidget(k);
  col->addLayout(cases);
  m_barre = new QProgressBar;
  m_barre->setRange(0, 1000);
  col->addWidget(m_barre);
  m_apercu = new QLabel;
  m_apercu->setMinimumSize(320, 180);
  m_apercu->setAlignment(Qt::AlignCenter);
  col->addWidget(m_apercu, 1);
  m_bilan = inspecteur::texte();
  m_bilan->setObjectName(QStringLiteral("bilanRendu"));
  col->addWidget(m_bilan);
  // Images clés
  m_listeCles = new QListWidget;
  m_listeCles->setMaximumHeight(90);
  col->addWidget(m_listeCles);
  auto* bas = new QHBoxLayout;
  auto* cle = new QPushButton(QStringLiteral("Image clé ici"));
  auto* video = new QPushButton(QStringLiteral("Animation…"));
  m_lancer = new QPushButton(QStringLiteral("Rendre…"));
  m_lancer->setObjectName(QStringLiteral("lancerRendu"));
  m_arreter = new QPushButton(QStringLiteral("Arrêter"));
  m_arreter->setEnabled(false);
  bas->addWidget(cle);
  bas->addWidget(video);
  bas->addStretch(1);
  bas->addWidget(m_lancer);
  bas->addWidget(m_arreter);
  col->addLayout(bas);
  connect(cle, &QPushButton::clicked, this, [this] { ajouterCle(m_cles.empty() ? 0 : m_cles.back().t + 2); });
  connect(video, &QPushButton::clicked, this, [this] {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier des images de l’animation"));
    if (!d.isEmpty()) enregistrerAnimation(d);
  });
  connect(m_lancer, &QPushButton::clicked, this, [this] {
    const QString fichier = QFileDialog::getSaveFileName(this, QStringLiteral("Image de la carte"), QStringLiteral("carte.png"), QStringLiteral("PNG (*.png)"));
    if (!fichier.isEmpty()) lancer(fichier);
  });
  connect(m_arreter, &QPushButton::clicked, this, &FenetreRendu::arreter);
  resize(560, 520);
}

FenetreRendu::~FenetreRendu() {
  if (m_arret) m_arret->store(true);
  if (m_fil.joinable()) m_fil.join();
}

void FenetreRendu::reglerTaille(int largeur, int hauteur) {
  m_largeur->setValue(largeur);
  m_hauteur->setValue(hauteur);
}
void FenetreRendu::reglerFondTransparent(bool oui) { m_transparent->setChecked(oui); }

void FenetreRendu::lancer(const QString& fichier) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c || m_enCours) return;
  if (m_fil.joinable()) m_fil.join();
  prom::OptionsRendu o;
  o.largeur = m_largeur->value();
  o.hauteur = m_hauteur->value();
  o.fondTransparent = m_transparent->isChecked();
  o.ombres = m_ombres->isChecked();
  o.reflets = m_reflets->isChecked();
  o.echantillons = m_echantillons->value();
  o.vue = m_atelier.pointDeVue3D("rendu");
  if (o.vue.distance <= 0) o.vue = prom::vueDeTroisQuarts(*c);
  m_fichier = fichier;
  m_arret = std::make_shared<std::atomic<bool>>(false);
  m_enCours = true;
  m_progression = 0;
  m_barre->setValue(0);
  m_lancer->setEnabled(false);
  m_arreter->setEnabled(true);
  m_bilan->setText(QStringLiteral("Rendu de %1 × %2 en cours, sur son propre fil : l’atelier reste utilisable.").arg(o.largeur).arg(o.hauteur));
  QPointer<FenetreRendu> moi(this);
  auto arret = m_arret;
  m_fil = std::thread([copie = *c, o, arret, moi] {
    auto r = std::make_shared<prom::ImageRendu>(prom::rendreCarte(copie, o, arret.get(), [moi](double p) {
      QMetaObject::invokeMethod(qApp, [moi, p] {
        if (!moi) return;
        moi->m_progression = p;
        moi->m_barre->setValue(static_cast<int>(p * 1000));
      }, Qt::QueuedConnection);
    }));
    QMetaObject::invokeMethod(qApp, [moi, r] {
      if (moi) moi->fin(r);
    }, Qt::QueuedConnection);
  });
}

void FenetreRendu::arreter() {
  if (m_arret) m_arret->store(true);
}

void FenetreRendu::fin(const std::shared_ptr<prom::ImageRendu>& r) {
  m_enCours = false;
  m_lancer->setEnabled(true);
  m_arreter->setEnabled(false);
  if (m_fil.joinable()) m_fil.join();
  m_image = QImage(r->rgba.data(), r->largeur, r->hauteur, r->largeur * 4, QImage::Format_RGBA8888).copy();
  m_apercu->setPixmap(QPixmap::fromImage(m_image.scaled(m_apercu->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
  if (r->interrompu) m_bilan->setText(QStringLiteral("Rendu arrêté : l’image est partielle."));
  else {
    QString enregistre;
    if (!m_fichier.isEmpty()) enregistre = m_image.save(m_fichier, "PNG") ? QStringLiteral(" Enregistré dans « %1 ».").arg(QFileInfo(m_fichier).fileName()) : QStringLiteral(" Enregistrement impossible.");
    m_bilan->setText(QStringLiteral("%1 × %2 rendu en %3 s (%4 triangles, %5).%6")
                         .arg(r->largeur)
                         .arg(r->hauteur)
                         .arg(r->secondes, 0, 'f', 1)
                         .arg(r->triangles)
                         .arg(r->alphaPresent() ? QStringLiteral("fond transparent") : QStringLiteral("fond opaque"))
                         .arg(enregistre));
  }
  emit termine(r->interrompu);
}

void FenetreRendu::ajouterCle(double t, double leveeBoitier) {
  prom::ImageCle k;
  k.t = t;
  k.vue = m_atelier.pointDeVue3D("clé");
  k.leveeBoitier = leveeBoitier;
  m_cles.push_back(k);
  m_listeCles->addItem(QStringLiteral("%1 s · azimut %2°, élévation %3°").arg(t, 0, 'f', 1).arg(k.vue.azimut, 0, 'f', 1).arg(k.vue.elevation, 0, 'f', 1));
}

int FenetreRendu::enregistrerAnimation(const QString& dossier, int imagesParSeconde) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c || m_cles.size() < 2) {
    m_bilan->setText(rouge(QStringLiteral("Il faut au moins deux images clés.")));
    return 0;
  }
  // Qt Multimedia n'est pas livré avec Prométhée : l'animation s'enregistre en PNG numérotés, qu'un outil de montage
  // assemble ensuite (ffmpeg -i image-%04d.png)
  QDir().mkpath(dossier);
  const double duree = prom::dureeAnimation(m_cles);
  const int n = std::max(2, static_cast<int>(std::lround(duree * imagesParSeconde)) + 1);
  prom::OptionsRendu o;
  o.largeur = m_largeur->value();
  o.hauteur = m_hauteur->value();
  o.fondTransparent = m_transparent->isChecked();
  o.ombres = m_ombres->isChecked();
  o.reflets = m_reflets->isChecked();
  int ecrites = 0;
  for (int i = 0; i < n; ++i) {
    const prom::EtatAnimation e = prom::etatAnimation(m_cles, duree * i / (n - 1));
    o.vue = e.vue;
    o.leveeBoitier = e.leveeBoitier;
    o.leveePieces = e.leveePieces;
    const prom::ImageRendu r = prom::rendreCarte(*c, o);
    const QImage img(r.rgba.data(), r.largeur, r.hauteur, r.largeur * 4, QImage::Format_RGBA8888);
    if (img.save(QDir(dossier).filePath(QStringLiteral("image-%1.png").arg(i, 4, 10, QLatin1Char('0'))), "PNG")) ++ecrites;
    m_barre->setValue(static_cast<int>(1000.0 * (i + 1) / n));
    QCoreApplication::processEvents();
  }
  m_bilan->setText(QStringLiteral("%1 image(s) de l’animation écrites dans « %2 » (PNG numérotés).").arg(ecrites).arg(QDir(dossier).dirName()));
  return ecrites;
}

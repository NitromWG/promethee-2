// Prométhée : commandes du cuivre et de la carte (lot L5, 0.34). Chaque commande appelle une fonction de
// circuit_edition.hpp dans un seul pas d'annulation et dit ce qu'elle a fait (le journal des changements en masse,
// 5.19). Placement (5.17, 5.19, 5.24), pistes (5.9, 5.10, 5.22, 5.23), zones (5.11, 5.25, 5.26), dessin de la carte
// (5.15, 5.31), réglages repris (5.32).
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "PanneauReseaux.hpp"
#include "VueCircuit.hpp"
#include "promethee/circuit_edition.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QString mmTexte(double nm) { return QLocale().toString(nm * 1e-6, 'f', 2) + QStringLiteral(" mm"); }

// Liste à cocher (nettoyage prévu, renumérotation prévue) : rend les lignes gardées, vide si l'on renonce
std::optional<std::vector<bool>> cocherListe(QWidget* parent, const QString& titre, const QString& texte, const QStringList& lignes) {
  QDialog d(parent);
  d.setObjectName(QStringLiteral("listeACocher"));
  d.setWindowTitle(titre);
  auto* v = new QVBoxLayout(&d);
  v->addWidget(new QLabel(texte));
  auto* l = new QListWidget;
  l->setObjectName(QStringLiteral("lignes"));
  for (const QString& x : lignes) {
    auto* it = new QListWidgetItem(x, l);
    it->setCheckState(Qt::Checked);
  }
  v->addWidget(l);
  auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  QObject::connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  v->addWidget(b);
  d.resize(560, 420);
  if (d.exec() != QDialog::Accepted) return std::nullopt;
  std::vector<bool> r;
  for (int i = 0; i < l->count(); ++i) r.push_back(l->item(i)->checkState() == Qt::Checked);
  return r;
}

QStringList nomsCouchesCuivre(const ConceptionElec& c) {
  QStringList r;
  for (const Couche k : couchesCuivre(c.circuit.empilage.couchesCuivre)) r << QString::fromUtf8(codeCouche(k));
  return r;
}
std::optional<Couche> demanderCouche(QWidget* parent, const ConceptionElec& c, const QString& titre, const QString& texte, Couche defaut, bool toutes = false) {
  QStringList noms = nomsCouchesCuivre(c);
  if (toutes)
    for (const Couche k : {Couche::SerigraphieDessus, Couche::SerigraphieDessous, Couche::VernisDessus, Couche::VernisDessous, Couche::FabricationDessus, Couche::FabricationDessous, Couche::Dessins})
      noms << QString::fromUtf8(codeCouche(k));
  bool ok = false;
  const int i = static_cast<int>(std::max<qsizetype>(0, noms.indexOf(QString::fromUtf8(codeCouche(defaut)))));
  const QString choix = QInputDialog::getItem(parent, titre, texte, noms, i, false, &ok);
  if (!ok) return std::nullopt;
  return coucheDepuisCode(choix.toStdString());
}
}  // namespace

void AtelierElectronique::enregistrerCommandesCuivre() {
  auto& c = m_commandes;
  // Objets choisis dans la vue du circuit, d'un genre ou de tous
  auto choisis = [this](std::optional<VueCircuit::Genre> g = std::nullopt) {
    std::set<std::string> r;
    for (const auto& o : m_circuit->selection())
      if (!g || o.genre == *g) r.insert(o.id);
    return r;
  };
  // Une commande, un pas : la fonction rend son bilan, montré en message (vide : rien n'a changé)
  auto faire = [this](const QString& nom, const std::function<QString(ConceptionElec&)>& f) {
    QString bilan;
    if (modifierConception([&](ConceptionElec& x) { bilan = f(x); }, nom)) {
      if (!bilan.isEmpty()) afficherMessage(bilan, NiveauMessage::Succes);
    } else if (!bilan.isEmpty())
      afficherMessage(bilan, NiveauMessage::Information);
  };
  auto reseauChoisi = [this, choisis]() -> std::string {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return {};
    const auto ids = choisis();
    for (const auto& p : x->circuit.pistes)
      if (ids.count(p.uuid)) return p.reseau;
    for (const auto& v : x->circuit.vias)
      if (ids.count(v.uuid)) return v.reseau;
    for (const auto& z : x->circuit.zones)
      if (ids.count(z.uuid)) return z.reseau;
    return {};
  };
  auto demanderReseau = [this, reseauChoisi](const QString& titre) -> std::optional<std::string> {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return std::nullopt;
    QStringList noms;
    for (const auto& r : m_calculs.k.reseaux) noms << q(r.nom);
    std::set<std::string> deja;
    for (const auto& r : m_calculs.k.reseaux) deja.insert(r.nom);
    for (const auto& [cle, r] : x->nomsReseaux)
      if (!r.empty() && deja.insert(r).second) noms << q(r);
    const std::string choisi = reseauChoisi();
    bool ok = false;
    const QString r = QInputDialog::getItem(this, titre, QStringLiteral("Réseau :"), noms, std::max<int>(0, static_cast<int>(noms.indexOf(q(choisi)))), true, &ok);
    if (!ok || r.trimmed().isEmpty()) return std::nullopt;
    return r.trimmed().toStdString();
  };
  auto centreCarte = [this]() -> Pt {
    const ConceptionElec* x = conceptionCourante();
    return x ? boite(x->circuit.contour.libre.size() >= 3 ? x->circuit.contour.libre : x->circuit.contour.polygone()).centre() : Pt{};
  };

  c["vue.reseaux"] = [this] {
    if (QDockWidget* d = panneau(Panneau::Reseaux)) {
      d->show();
      d->raise();
      m_reseaux->refaire();
    }
  };

  // ---------- Placement (5.17, 5.19, 5.24) ----------
  c["circuit.poserEmpreinte"] = [this, faire] {
    bool ok = false;
    const QString id = QInputDialog::getText(this, QStringLiteral("Poser une empreinte seule"),
                                             QStringLiteral("Identifiant de l’empreinte (« R_0603 », « SOIC-8 », « MountingHole_3.2 ») :"), QLineEdit::Normal,
                                             QStringLiteral("R_0603"), &ok);
    if (!ok || id.trimmed().isEmpty()) return;
    setMode(Circuit);
    const Pt p = m_circuit->pointSouris();
    std::string cree;
    faire(QStringLiteral("Poser une empreinte"), [&](ConceptionElec& x) {
      cree = poserEmpreinteSeule(x, id.trimmed().toStdString(), p);
      if (cree.empty()) return QStringLiteral("Empreinte « %1 » inconnue : rien n’est posé.").arg(id.toHtmlEscaped());
      return QStringLiteral("<b>%1</b> posé sans schéma : ses pastilles prennent leur réseau des pistes tirées entre elles.").arg(q(x.composant(cree)->reference));
    });
    if (!cree.empty()) m_circuit->choisir(VueCircuit::Genre::Empreinte, cree);
  };
  c["circuit.renumeroter"] = [this, faire, choisis] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    QDialog d(this);
    d.setObjectName(QStringLiteral("dialogueRenumeroter"));
    d.setWindowTitle(QStringLiteral("Renuméroter selon la carte"));
    auto* f = new QFormLayout(&d);
    auto* parcours = new QComboBox;
    parcours->setObjectName(QStringLiteral("parcours"));
    parcours->addItems({QStringLiteral("De gauche à droite, puis de haut en bas"), QStringLiteral("De gauche à droite, puis de bas en haut"), QStringLiteral("De droite à gauche, puis de haut en bas"),
                        QStringLiteral("De droite à gauche, puis de bas en haut"), QStringLiteral("De haut en bas, puis de gauche à droite"), QStringLiteral("De haut en bas, puis de droite à gauche"),
                        QStringLiteral("De bas en haut, puis de gauche à droite"), QStringLiteral("De bas en haut, puis de droite à gauche")});
    f->addRow(QStringLiteral("Parcours :"), parcours);
    auto* dessous = new QLineEdit;
    dessous->setObjectName(QStringLiteral("prefixeDessous"));
    dessous->setPlaceholderText(QStringLiteral("aucun"));
    f->addRow(QStringLiteral("Préfixe du dessous :"), dessous);
    auto* debut = new QSpinBox;
    debut->setRange(0, 9999);
    debut->setValue(1);
    f->addRow(QStringLiteral("Premier numéro :"), debut);
    auto* selection = new QCheckBox(QStringLiteral("Seulement les empreintes choisies"));
    selection->setChecked(!choisis(VueCircuit::Genre::Empreinte).empty());
    f->addRow(selection);
    auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    f->addRow(b);
    if (d.exec() != QDialog::Accepted) return;
    OptionsRenumerotation o;
    o.parcours = static_cast<Parcours>(parcours->currentIndex());
    o.prefixeDessous = dessous->text().trimmed().toStdString();
    o.debut = debut->value();
    if (selection->isChecked()) o.portee = choisis(VueCircuit::Genre::Empreinte);
    faire(QStringLiteral("Renuméroter selon la carte"), [&](ConceptionElec& y) {
      std::vector<std::string> journal;
      const int n = renumeroter(y, o, &journal);
      if (!n) return QStringLiteral("Les repères suivent déjà ce parcours.");
      QStringList l;
      for (const auto& j : journal) l << q(j);
      return QStringLiteral("<b>%1 repère(s) renuméroté(s)</b> : %2. Le schéma suit.").arg(n).arg(l.mid(0, 12).join(QStringLiteral(", ")) + (l.size() > 12 ? QStringLiteral("…") : QString()));
    });
  };
  c["circuit.importerPlacement"] = [this, faire] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Importer un placement"), {},
                                                   QStringLiteral("Placements (*.csv *.txt *.pos *.xlsx *.tsv);;Tous les fichiers (*)"));
    if (f.isEmpty()) return;
    QFile fi(f);
    if (!fi.open(QIODevice::ReadOnly)) {
      afficherMessage(QStringLiteral("Fichier illisible : %1").arg(f.toHtmlEscaped()), NiveauMessage::Erreur);
      return;
    }
    const std::string contenu = fi.readAll().toStdString();
    faire(QStringLiteral("Importer un placement"), [&](ConceptionElec& x) {
      const BilanImportPlacement b = importerPlacement(x, contenu);
      QStringList l;
      for (const auto& j : b.journal) l << q(j).toHtmlEscaped();
      return QStringLiteral("<b>Placement importé</b><br>") + l.mid(0, 15).join(QStringLiteral("<br>"));
    });
  };
  c["circuit.placerReperes"] = [this, faire, choisis] {
    bool ok = false;
    const QStringList positions{QStringLiteral("0 · place libre la plus proche"), QStringLiteral("1 · en bas à gauche"), QStringLiteral("2 · en bas"), QStringLiteral("3 · en bas à droite"),
                                QStringLiteral("4 · à gauche"), QStringLiteral("5 · au centre"), QStringLiteral("6 · à droite"), QStringLiteral("7 · en haut à gauche"),
                                QStringLiteral("8 · en haut"), QStringLiteral("9 · en haut à droite")};
    const QString p = QInputDialog::getItem(this, QStringLiteral("Placer les repères"), QStringLiteral("Place du repère autour du corps (pavé numérique) :"), positions, 8, false, &ok);
    if (!ok) return;
    const int pos = static_cast<int>(positions.indexOf(p));
    const auto sel = choisis(VueCircuit::Genre::Empreinte);
    faire(QStringLiteral("Placer les repères"), [&](ConceptionElec& x) {
      return QStringLiteral("<b>%1 repère(s) placé(s)</b>.").arg(placerReperes(x, sel, pos));
    });
  };
  c["circuit.decouplages"] = [this, faire, choisis] {
    const auto sel = choisis(VueCircuit::Genre::Empreinte);
    if (sel.empty()) {
      afficherMessage(QStringLiteral("Choisis d’abord le circuit intégré dont les condensateurs de découplage sont à placer."));
      return;
    }
    faire(QStringLiteral("Placer les découplages"), [&](ConceptionElec& x) {
      int n = 0;
      for (const auto& id : sel) n += placerDecouplages(x, id);
      if (!n) return QStringLiteral("Aucun condensateur à rapprocher : ils sont déjà près de leurs broches, ou aucun n’est relié à une alimentation de ce circuit.");
      return QStringLiteral("<b>%1 condensateur(s) de découplage placé(s)</b> au plus près de leur broche, le plus petit d’abord, puis reliés.").arg(n);
    });
  };
  c["circuit.serrer"] = [this, faire] {
    std::vector<std::string> ids;
    for (const auto& o : m_circuit->selection())
      if (o.genre == VueCircuit::Genre::Empreinte) ids.push_back(o.id);
    if (ids.size() < 2) {
      afficherMessage(QStringLiteral("Choisis au moins deux empreintes à serrer."));
      return;
    }
    faire(QStringLiteral("Serrer"), [&](ConceptionElec& x) { return QStringLiteral("<b>%1 empreinte(s) rapprochée(s)</b> de leur centre commun, sans se recouvrir.").arg(serrerEmpreintes(x, ids)); });
  };
  c["circuit.echangerEmpreintes"] = [this, faire] {
    std::vector<std::string> ids;
    for (const auto& o : m_circuit->selection())
      if (o.genre == VueCircuit::Genre::Empreinte) ids.push_back(o.id);
    if (ids.size() != 2) {
      afficherMessage(QStringLiteral("Choisis exactement deux empreintes à échanger."));
      return;
    }
    faire(QStringLiteral("Échanger deux empreintes"), [&](ConceptionElec& x) {
      return echangerEmpreintes(x, ids[0], ids[1]) ? QStringLiteral("Places échangées.") : QStringLiteral("Une des deux empreintes est verrouillée : rien n’a bougé.");
    });
  };
  c["circuit.ecarterVoisines"] = [this, faire] {
    if (m_circuit->genreChoisi() != VueCircuit::Genre::Empreinte) {
      afficherMessage(QStringLiteral("Choisis l’empreinte qui doit écarter ses voisines."));
      return;
    }
    const std::string id = m_circuit->choisi();
    faire(QStringLiteral("Écarter les voisines"), [&](ConceptionElec& x) {
      const auto it = std::find_if(x.circuit.empreintes.begin(), x.circuit.empreintes.end(), [&](const EmpreintePlacee& e) { return e.composant == id; });
      if (it == x.circuit.empreintes.end()) return QString();
      return pousserEmpreintes(x, id, it->position) ? QStringLiteral("Les voisines se sont écartées.") : QStringLiteral("Une voisine verrouillée empêche de pousser : rien n’a bougé.");
    });
  };

  // ---------- Pistes (5.9, 5.10, 5.22, 5.23) ----------
  c["circuit.nettoyer"] = [this, faire, choisis] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const auto sel = choisis();
    const auto prevu = nettoyagePrevu(*x, sel.empty() ? nullptr : &sel);
    if (prevu.empty()) {
      afficherMessage(QStringLiteral("Rien à nettoyer : ni segment nul, ni doublon, ni segments alignés à réunir, ni via orpheline."), NiveauMessage::Succes);
      return;
    }
    QStringList lignes;
    for (const auto& ch : prevu) lignes << q(ch.description);
    const auto garde = cocherListe(this, QStringLiteral("Nettoyer les pistes"), QStringLiteral("Changements prévus ; décoche ceux à ne pas faire :"), lignes);
    if (!garde) return;
    std::vector<ChangementNettoyage> choisisAFaire;
    for (std::size_t i = 0; i < prevu.size(); ++i)
      if ((*garde)[i]) choisisAFaire.push_back(prevu[i]);
    faire(QStringLiteral("Nettoyer les pistes"), [&](ConceptionElec& y) { return QStringLiteral("<b>%1 objet(s) nettoyé(s)</b>.").arg(nettoyer(y, choisisAFaire)); });
  };
  c["circuit.lisser"] = [this, faire, choisis] {
    const auto sel = choisis(VueCircuit::Genre::Piste);
    if (sel.empty()) {
      afficherMessage(QStringLiteral("Choisis les pistes à lisser."));
      return;
    }
    faire(QStringLiteral("Lisser les pistes"), [&](ConceptionElec& x) { return QStringLiteral("<b>%1 coin(s) coupé(s) à 45°</b>, isolement tenu.").arg(lisser(x, sel)); });
  };
  c["circuit.derouter"] = [this, faire, reseauChoisi] {
    const QStringList portees{QStringLiteral("Le réseau de la piste choisie"), QStringLiteral("La connexion de la piste choisie"), QStringLiteral("Les réseaux du composant choisi"),
                              QStringLiteral("Toute la carte")};
    bool ok = false;
    const QString p = QInputDialog::getItem(this, QStringLiteral("Dérouter"), QStringLiteral("Retirer le cuivre (le verrouillé reste) :"), portees, 0, false, &ok);
    if (!ok) return;
    std::string portee;
    switch (portees.indexOf(p)) {
      case 0: portee = "reseau:" + reseauChoisi(); break;
      case 1: portee = m_circuit->genreChoisi() == VueCircuit::Genre::Piste ? "connexion:" + m_circuit->choisi() : std::string(); break;
      case 2: portee = m_circuit->genreChoisi() == VueCircuit::Genre::Empreinte ? "composant:" + m_circuit->choisi() : std::string(); break;
      default: portee = "carte";
    }
    if (portee.empty() || portee == "reseau:") {
      afficherMessage(QStringLiteral("Choisis d’abord l’objet que la portée désigne."));
      return;
    }
    faire(QStringLiteral("Dérouter"), [&](ConceptionElec& x) { return QStringLiteral("<b>%1 objet(s) retiré(s)</b> ; le cuivre verrouillé reste.").arg(derouter(x, portee)); });
  };
  c["circuit.pinceauReseau"] = [this, faire, choisis, demanderReseau] {
    const auto sel = choisis();
    if (sel.empty()) {
      afficherMessage(QStringLiteral("Choisis les pistes, vias ou zones qui prennent le réseau."));
      return;
    }
    const auto r = demanderReseau(QStringLiteral("Donner un réseau"));
    if (!r) return;
    faire(QStringLiteral("Donner un réseau"), [&](ConceptionElec& x) { return QStringLiteral("<b>%1 objet(s)</b> passés sur « %2 ».").arg(appliquerReseau(x, sel, *r)).arg(q(*r).toHtmlEscaped()); });
  };
  c["circuit.couperPiste"] = [this, faire, choisis] {
    const auto sel = choisis(VueCircuit::Genre::Piste);
    if (sel.empty()) {
      afficherMessage(QStringLiteral("Choisis les pistes à couper : chacune l’est en son milieu, les deux morceaux restent reliés."));
      return;
    }
    faire(QStringLiteral("Couper les pistes"), [&](ConceptionElec& x) {
      int n = 0;
      for (const auto& id : sel) {
        const auto it = std::find_if(x.circuit.pistes.begin(), x.circuit.pistes.end(), [&](const Piste& p) { return p.uuid == id; });
        if (it != x.circuit.pistes.end()) n += couperPiste(x, id, {(it->a.x + it->b.x) / 2, (it->a.y + it->b.y) / 2}) ? 1 : 0;
      }
      return QStringLiteral("<b>%1 piste(s) coupée(s)</b> en leur milieu.").arg(n);
    });
  };
  c["circuit.routerChoisies"] = [this, faire, choisis] {
    const auto sel = choisis();
    std::vector<Liaison> l;
    std::set<std::string> reseaux;
    for (const auto& p : m_calculs.pastilles)
      if (sel.count(p.composant) && !p.reseau.empty()) reseaux.insert(p.reseau);
    for (const auto& x : m_calculs.etat.chevelu)
      if (sel.empty() || reseaux.count(x.reseau)) l.push_back(x);
    if (l.empty()) {
      afficherMessage(QStringLiteral("Aucune liaison à router pour ce choix."), NiveauMessage::Succes);
      return;
    }
    faire(QStringLiteral("Router les liaisons"), [&](ConceptionElec& x) {
      std::vector<std::string> raisons;
      const int n = routerPlusieurs(x, l, &raisons);
      QString r = QStringLiteral("<b>%1 liaison(s) routée(s)</b> sur %2.").arg(n).arg(l.size());
      for (std::size_t i = 0; i < std::min<std::size_t>(raisons.size(), 6); ++i) r += QStringLiteral("<br>") + q(raisons[i]).toHtmlEscaped();
      return r;
    });
  };
  c["circuit.routerPaire"] = [this, faire] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const auto& paires = x->circuit.regles.avancees.paires;
    if (paires.empty()) {
      afficherMessage(QStringLiteral("Aucune paire différentielle : déclare-la dans le gestionnaire de contraintes (Paires)."));
      return;
    }
    QStringList noms;
    for (const auto& p : paires) noms << QStringLiteral("%1 (%2 / %3)").arg(q(p.nom), q(p.positif), q(p.negatif));
    bool ok = false;
    const QString choix = QInputDialog::getItem(this, QStringLiteral("Router une paire"), QStringLiteral("Paire :"), noms, 0, false, &ok);
    if (!ok) return;
    const PaireDifferentielle paire = paires[static_cast<std::size_t>(noms.indexOf(choix))];
    const Couche couche = m_circuit->coucheActive();
    faire(QStringLiteral("Router la paire"), [&](ConceptionElec& y) {
      const BilanPaire b = routerPaire(y, paire, couche);
      if (!b.ok) return QStringLiteral("Paire « %1 » non routée : %2").arg(q(paire.nom), q(b.raison).toHtmlEscaped());
      return QStringLiteral("<b>Paire « %1 » routée</b> à %2 d’écart%3").arg(q(paire.nom), mmTexte(static_cast<double>(b.ecart)),
                                                                             b.raison.empty() ? QStringLiteral(".") : QStringLiteral(" ; ") + q(b.raison).toHtmlEscaped());
    });
  };
  c["circuit.accorder"] = [this, faire, demanderReseau] {
    const auto r = demanderReseau(QStringLiteral("Accorder la longueur"));
    if (!r) return;
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const double L = longueurReseau(*x, *r) * 1e-6;
    bool ok = false;
    const double cible = QInputDialog::getDouble(this, QStringLiteral("Accorder la longueur"), QStringLiteral("« %1 » mesure %2 mm. Longueur visée (mm) :").arg(q(*r)).arg(L, 0, 'f', 2), L + 5, 0, 10000, 2, &ok);
    if (!ok) return;
    const QStringList genres{QStringLiteral("Accordéon"), QStringLiteral("Trombone"), QStringLiteral("Dents de scie")};
    const QString g = QInputDialog::getItem(this, QStringLiteral("Accorder la longueur"), QStringLiteral("Motif :"), genres, 0, false, &ok);
    if (!ok) return;
    const auto genre = static_cast<GenreMotifAccord>(genres.indexOf(g));
    faire(QStringLiteral("Accorder la longueur"), [&](ConceptionElec& y) {
      const std::string id = accorderReseau(y, *r, nm(cible), genre);
      if (id.empty()) return QStringLiteral("« %1 » : pas de segment droit assez libre pour le motif, ou longueur déjà atteinte.").arg(q(*r));
      return QStringLiteral("<b>« %1 » accordé</b> à %2 mm ; le motif se refait si une règle change.").arg(q(*r)).arg(longueurReseau(y, *r) * 1e-6, 0, 'f', 2);
    });
  };
  c["circuit.accorderGroupe"] = [this, faire] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const auto& groupes = x->circuit.regles.avancees.accords;
    if (groupes.empty()) {
      afficherMessage(QStringLiteral("Aucun groupe d’accord : déclare-le dans le gestionnaire de contraintes (Accords)."));
      return;
    }
    QStringList noms;
    for (const auto& g : groupes) noms << q(g.nom);
    bool ok = false;
    const QString choix = QInputDialog::getItem(this, QStringLiteral("Accorder un groupe"), QStringLiteral("Groupe :"), noms, 0, false, &ok);
    if (!ok) return;
    const GroupeAccord g = groupes[static_cast<std::size_t>(noms.indexOf(choix))];
    faire(QStringLiteral("Accorder un groupe"), [&](ConceptionElec& y) {
      std::vector<std::string> nonTenus;
      const int n = accorderGroupe(y, g, &nonTenus);
      QString r = QStringLiteral("<b>Groupe « %1 »</b> : %2 réseau(x) accordé(s).").arg(q(g.nom)).arg(n);
      if (!nonTenus.empty()) {
        QStringList l;
        for (const auto& s : nonTenus) l << q(s);
        r += QStringLiteral(" Sans la place : %1.").arg(l.join(QStringLiteral(", ")));
      }
      return r;
    });
  };
  c["circuit.regenererMotifs"] = [faire] { faire(QStringLiteral("Refaire les motifs d’accord"), [](ConceptionElec& x) { return QStringLiteral("<b>%1 motif(s) refait(s)</b> d’après les règles.").arg(regenererMotifs(x)); }); };
  c["circuit.gouttes"] = [faire] {
    faire(QStringLiteral("Poser les gouttes"), [](ConceptionElec& x) { return QStringLiteral("<b>%1 goutte(s) posée(s)</b> sur les pastilles et les vias.").arg(poserGouttes(x)); });
  };
  c["circuit.retirerGouttes"] = [faire] { faire(QStringLiteral("Retirer les gouttes"), [](ConceptionElec& x) { return QStringLiteral("<b>%1 goutte(s) retirée(s)</b>.").arg(retirerGouttes(x)); }); };

  // ---------- Zones (5.11, 5.25, 5.26) ----------
  auto zoneChoisie = [this]() -> std::string { return m_circuit->genreChoisi() == VueCircuit::Genre::Zone ? m_circuit->choisi() : std::string(); };
  c["circuit.zoneCarte"] = [this, faire, demanderReseau] {
    const auto r = demanderReseau(QStringLiteral("Zone sur toute la carte"));
    if (!r) return;
    const Couche couche = m_circuit->coucheActive();
    faire(QStringLiteral("Zone sur toute la carte"), [&](ConceptionElec& x) {
      zoneSurToutLaCarte(x, *r, couche);
      return QStringLiteral("<b>Zone « %1 »</b> sur toute la carte, couche %2.").arg(q(x.circuit.zones.back().nom), QString::fromUtf8(codeCouche(couche)));
    });
  };
  c["circuit.planReseau"] = [this, faire, demanderReseau] {
    const auto r = demanderReseau(QStringLiteral("Plan d’un réseau"));
    if (!r) return;
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const auto couches = couchesCuivre(x->circuit.empilage.couchesCuivre);
    faire(QStringLiteral("Plan d’un réseau"), [&](ConceptionElec& y) { return QStringLiteral("<b>%1 zone(s) de « %2 »</b> posée(s).").arg(planDuReseau(y, *r, couches).size()).arg(q(*r)); });
  };
  c["circuit.coudre"] = [this, faire, zoneChoisie] {
    const std::string z = zoneChoisie();
    if (z.empty()) {
      afficherMessage(QStringLiteral("Choisis la zone à coudre."));
      return;
    }
    bool ok = false;
    const double pas = QInputDialog::getDouble(this, QStringLiteral("Coudre la zone"), QStringLiteral("Pas de la grille de vias (mm) :"), 5, 1, 50, 2, &ok);
    if (!ok) return;
    faire(QStringLiteral("Coudre la zone"), [&](ConceptionElec& x) { return QStringLiteral("<b>%1 via(s) de couture</b> posée(s) ; refaite à chaque nouvel appel.").arg(coudreZone(x, z, nm(pas))); });
  };
  c["circuit.relierIlots"] = [this, faire, zoneChoisie] {
    const std::string z = zoneChoisie();
    if (z.empty()) {
      afficherMessage(QStringLiteral("Choisis la zone dont les îlots sont à relier."));
      return;
    }
    faire(QStringLiteral("Relier les îlots"), [&](ConceptionElec& x) { return QStringLiteral("<b>%1 îlot(s) relié(s)</b> par une via au plan du même réseau.").arg(relierIlotsParVias(x, z)); });
  };
  c["circuit.dupliquerZone"] = [this, faire, zoneChoisie] {
    const std::string z = zoneChoisie();
    const ConceptionElec* x = conceptionCourante();
    if (z.empty() || !x) {
      afficherMessage(QStringLiteral("Choisis la zone à dupliquer."));
      return;
    }
    const auto k = demanderCouche(this, *x, QStringLiteral("Dupliquer la zone"), QStringLiteral("Sur la couche :"), Couche::CuivreDessous);
    if (!k) return;
    faire(QStringLiteral("Dupliquer la zone"), [&](ConceptionElec& y) {
      dupliquerZone(y, z, *k);
      return QStringLiteral("<b>Zone dupliquée</b> : « %1 ».").arg(q(y.circuit.zones.back().nom));
    });
  };
  c["circuit.fusionnerZones"] = [this, faire, choisis] {
    const auto s = choisis(VueCircuit::Genre::Zone);
    if (s.size() < 2) {
      afficherMessage(QStringLiteral("Choisis au moins deux zones du même réseau et de la même couche."));
      return;
    }
    const std::vector<std::string> ids(s.begin(), s.end());
    faire(QStringLiteral("Fusionner les zones"), [&](ConceptionElec& x) {
      return fusionnerZones(x, ids).empty() ? QStringLiteral("Ces zones ne se fusionnent pas : réseaux ou couches différents, ou elles ne se touchent pas.") : QStringLiteral("<b>Zones fusionnées</b>.");
    });
  };
  c["circuit.soustraireZone"] = [this, faire] {
    std::vector<std::string> ids;
    for (const auto& o : m_circuit->selection())
      if (o.genre == VueCircuit::Genre::Zone) ids.push_back(o.id);
    if (ids.size() != 2) {
      afficherMessage(QStringLiteral("Choisis deux zones : la première garde ce que la seconde ne couvre pas."));
      return;
    }
    faire(QStringLiteral("Soustraire une zone"), [&](ConceptionElec& x) { return soustraireZone(x, ids[0], ids[1]) ? QStringLiteral("<b>Zone soustraite</b>.") : QStringLiteral("Rien ne reste : la zone n’a pas changé."); });
  };
  c["circuit.couperZone"] = [this, faire, zoneChoisie] {
    const std::string z = zoneChoisie();
    const ConceptionElec* x = conceptionCourante();
    if (z.empty() || !x) {
      afficherMessage(QStringLiteral("Choisis la zone à couper."));
      return;
    }
    const auto it = std::find_if(x->circuit.zones.begin(), x->circuit.zones.end(), [&](const Zone& y) { return y.uuid == z; });
    const Pt m = boite(it->contour).centre();
    const QStringList sens{QStringLiteral("Verticale, par le centre"), QStringLiteral("Horizontale, par le centre")};
    bool ok = false;
    const QString s = QInputDialog::getItem(this, QStringLiteral("Couper la zone"), QStringLiteral("Droite de coupe :"), sens, 0, false, &ok);
    if (!ok) return;
    const bool vert = sens.indexOf(s) == 0;
    faire(QStringLiteral("Couper la zone"), [&](ConceptionElec& y) {
      const auto r = couperZone(y, z, m, vert ? Pt{m.x, m.y + NM_PAR_MM} : Pt{m.x + NM_PAR_MM, m.y});
      return r.empty() ? QStringLiteral("La droite ne coupe pas la zone.") : QStringLiteral("<b>Zone coupée</b> en %1 morceaux.").arg(r.size());
    });
  };
  c["circuit.zoneDepuisPistes"] = [this, faire, choisis] {
    const auto s = choisis(VueCircuit::Genre::Piste);
    if (s.empty()) {
      afficherMessage(QStringLiteral("Choisis les pistes dont la zone épouse la forme."));
      return;
    }
    bool ok = false;
    const double marge = QInputDialog::getDouble(this, QStringLiteral("Zone depuis les pistes"), QStringLiteral("Marge autour des pistes (mm) :"), 1, 0, 50, 2, &ok);
    if (!ok) return;
    faire(QStringLiteral("Zone depuis les pistes"), [&](ConceptionElec& x) { return zoneDepuisPistes(x, s, nm(marge)).empty() ? QString() : QStringLiteral("<b>Zone posée</b> autour des pistes."); });
  };
  c["circuit.arrondirZone"] = [this, faire, zoneChoisie] {
    const std::string z = zoneChoisie();
    if (z.empty()) {
      afficherMessage(QStringLiteral("Choisis la zone à arrondir."));
      return;
    }
    bool ok = false;
    const double r = QInputDialog::getDouble(this, QStringLiteral("Arrondir la zone"), QStringLiteral("Rayon, ou côté du chanfrein (mm) :"), 1, 0.05, 50, 2, &ok);
    if (!ok) return;
    const QStringList formes{QStringLiteral("Arrondis"), QStringLiteral("Chanfreins")};
    const QString f = QInputDialog::getItem(this, QStringLiteral("Arrondir la zone"), QStringLiteral("Coins :"), formes, 0, false, &ok);
    if (!ok) return;
    faire(QStringLiteral("Arrondir la zone"), [&](ConceptionElec& x) { return arrondirZone(x, z, nm(r), formes.indexOf(f) == 1) ? QStringLiteral("<b>Coins de la zone repris</b>.") : QStringLiteral("La zone est trop fine pour ce rayon."); });
  };
  auto etat = [faire, choisis](EtatZone e, const QString& nom, const QString& fait) {
    return [faire, choisis, e, nom, fait] {
      const auto s = choisis(VueCircuit::Genre::Zone);
      faire(nom, [&](ConceptionElec& x) {
        std::set<std::string> ids = s;
        if (ids.empty())
          for (const auto& z : x.circuit.zones) ids.insert(z.uuid);
        return fait.arg(etatZones(x, ids, e));
      });
    };
  };
  c["circuit.figerZones"] = etat(EtatZone::Figee, QStringLiteral("Figer les zones"), QStringLiteral("<b>%1 zone(s) figée(s)</b> : leur cuivre ne bouge plus ; une vérification signale celles devenues périmées."));
  c["circuit.mettreDeCote"] = etat(EtatZone::MiseDeCote, QStringLiteral("Mettre les zones de côté"), QStringLiteral("<b>%1 zone(s) mise(s) de côté</b> : contour gardé, sans cuivre."));
  c["circuit.zonesNormales"] = etat(EtatZone::Normale, QStringLiteral("Rendre les zones à la normale"), QStringLiteral("<b>%1 zone(s)</b> de nouveau remplie(s) à chaque calcul."));

  // ---------- Presse-papiers (5.16, 5.30) ----------
  c["circuit.collageSpecial"] = [this] {
    setMode(Circuit);
    if (!m_circuit->peutColler()) {
      afficherMessage(QStringLiteral("Le presse-papiers ne contient pas de circuit : copie d’abord des empreintes ou du cuivre."));
      return;
    }
    QDialog d(this);
    d.setObjectName(QStringLiteral("dialogueCollageSpecial"));
    d.setWindowTitle(QStringLiteral("Collage spécial"));
    auto* f = new QFormLayout(&d);
    auto* reseaux = new QCheckBox(QStringLiteral("Garder les réseaux du cuivre"));
    reseaux->setObjectName(QStringLiteral("garderReseaux"));
    reseaux->setChecked(true);
    f->addRow(reseaux);
    auto* neufs = new QCheckBox(QStringLiteral("Composants neufs, de la carte seule (repères suivants)"));
    neufs->setObjectName(QStringLiteral("composantsNeufs"));
    neufs->setChecked(true);
    f->addRow(neufs);
    auto* classe = new QLineEdit;
    classe->setObjectName(QStringLiteral("classe"));
    classe->setPlaceholderText(QStringLiteral("aucune"));
    f->addRow(QStringLiteral("Ranger les réseaux collés dans la classe :"), classe);
    auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    f->addRow(b);
    if (d.exec() != QDialog::Accepted) return;
    OptionsCollageCircuit o;
    o.garderReseaux = reseaux->isChecked();
    o.composantsNeufs = neufs->isChecked();
    o.classe = classe->text().trimmed().toStdString();
    m_circuit->collerAvec(o);
  };

  // ---------- Dessin de la carte (5.15, 5.31), réglages (5.19, 5.32) ----------
  auto poserMotif = [this, faire](const QString& nom, const Polygones& motif, Couche couche) {
    faire(nom, [&](ConceptionElec& x) {
      const auto ids = poserMotifGraphique(x, motif, couche);
      return ids.empty() ? QString() : QStringLiteral("<b>%1 posé</b> sur %2 (%3 contour(s)).").arg(nom, QString::fromUtf8(codeCouche(couche))).arg(ids.size());
    });
  };
  c["circuit.codeQR"] = [this, poserMotif] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    bool ok = false;
    const QString texte = QInputDialog::getText(this, QStringLiteral("Code QR"), QStringLiteral("Texte ou adresse à coder :"), QLineEdit::Normal, q(x->nom), &ok);
    if (!ok || texte.isEmpty()) return;
    const double module = QInputDialog::getDouble(this, QStringLiteral("Code QR"), QStringLiteral("Taille d’un module (mm) :"), 0.4, 0.1, 5, 2, &ok);
    if (!ok) return;
    const QStringList genres{QStringLiteral("QR code"), QStringLiteral("Micro QR")};
    const QString g = QInputDialog::getItem(this, QStringLiteral("Code QR"), QStringLiteral("Sorte :"), genres, 0, false, &ok);
    if (!ok) return;
    const auto couche = demanderCouche(this, *x, QStringLiteral("Code QR"), QStringLiteral("Couche :"), Couche::SerigraphieDessus, true);
    if (!couche) return;
    const std::string octets = texte.toUtf8().toStdString();
    const auto modules = genres.indexOf(g) == 0 ? codeQR(octets) : codeMicroQR(octets);
    if (modules.empty()) {
      afficherMessage(QStringLiteral("Texte trop long pour ce code."), NiveauMessage::Avertissement);
      return;
    }
    const Pt o = m_circuit->pointSouris();
    const Nm pas = nm(module);
    const Nm cote = static_cast<Nm>(modules.size()) * pas;
    poserMotif(QStringLiteral("Code QR"), modulesEnPolygones(modules, {o.x - cote / 2, o.y - cote / 2}, pas), *couche);
  };
  c["circuit.code128"] = [this, poserMotif] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    bool ok = false;
    const QString texte = QInputDialog::getText(this, QStringLiteral("Code-barres"), QStringLiteral("Texte à coder (Code 128, ASCII) :"), QLineEdit::Normal, q(x->nom), &ok);
    if (!ok || texte.isEmpty()) return;
    const auto barres = code128(texte.toStdString());
    if (barres.empty()) {
      afficherMessage(QStringLiteral("Le Code 128 ne code que les caractères ASCII imprimables."), NiveauMessage::Avertissement);
      return;
    }
    const auto couche = demanderCouche(this, *x, QStringLiteral("Code-barres"), QStringLiteral("Couche :"), Couche::SerigraphieDessus, true);
    if (!couche) return;
    const Pt o = m_circuit->pointSouris();
    poserMotif(QStringLiteral("Code-barres"), barresEnPolygones(barres, o, 250'000, nm(5)), *couche);
  };
  c["circuit.logo"] = [this, poserMotif] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Logo depuis une image"), {}, QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif)"));
    if (f.isEmpty()) return;
    QImage img(f);
    if (img.isNull()) {
      afficherMessage(QStringLiteral("Image illisible."), NiveauMessage::Erreur);
      return;
    }
    bool ok = false;
    const double largeur = QInputDialog::getDouble(this, QStringLiteral("Logo"), QStringLiteral("Largeur du logo sur la carte (mm) :"), 10, 1, 500, 2, &ok);
    if (!ok) return;
    const auto couche = demanderCouche(this, *x, QStringLiteral("Logo"), QStringLiteral("Couche :"), Couche::SerigraphieDessus, true);
    if (!couche) return;
    img = img.convertToFormat(QImage::Format_Grayscale8);
    if (img.width() > 600) img = img.scaledToWidth(600, Qt::SmoothTransformation);
    std::vector<std::uint8_t> gris(static_cast<std::size_t>(img.width()) * static_cast<std::size_t>(img.height()));
    for (int y = 0; y < img.height(); ++y) std::copy_n(img.constScanLine(y), img.width(), gris.begin() + static_cast<std::ptrdiff_t>(y) * img.width());
    const double mmParPixel = largeur / img.width();
    const Pt o = m_circuit->pointSouris();
    poserMotif(QStringLiteral("Logo"), logoDepuisImage(gris, img.width(), img.height(), 128, mmParPixel, {o.x - nm(largeur / 2), o.y - nm(mmParPixel * img.height() / 2)}), *couche);
  };
  c["circuit.gabarit"] = [this, faire] {
    QStringList noms;
    for (const auto& g : gabaritsCarte()) noms << q(g.nom) + QStringLiteral(" — ") + q(g.description);
    bool ok = false;
    const QString choix = QInputDialog::getItem(this, QStringLiteral("Gabarit de carte"), QStringLiteral("Contour et trous de fixation :"), noms, 0, false, &ok);
    if (!ok) return;
    const GabaritCarte g = gabaritsCarte()[static_cast<std::size_t>(noms.indexOf(choix))];
    faire(QStringLiteral("Gabarit de carte"), [&](ConceptionElec& x) {
      appliquerGabaritCarte(x, g);
      return QStringLiteral("<b>Gabarit « %1 »</b> appliqué : contour et %2 trou(s) de fixation.").arg(q(g.nom)).arg(g.trous.size());
    });
  };
  c["circuit.reprendreReglages"] = [this, faire] {
    const auto& projet = m_doc->projet();
    QStringList noms;
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < projet.electronique.size(); ++i)
      if (static_cast<int>(i) != m_conception) {
        noms << q(projet.electronique[i].nom);
        indices.push_back(i);
      }
    if (noms.isEmpty()) {
      afficherMessage(QStringLiteral("Aucune autre carte dans ce projet : ouvre ou importe d’abord la carte de référence."));
      return;
    }
    bool ok = false;
    const QString choix = QInputDialog::getItem(this, QStringLiteral("Reprendre les réglages"), QStringLiteral("Depuis la carte :"), noms, 0, false, &ok);
    if (!ok) return;
    const ConceptionElec source = projet.electronique[indices[static_cast<std::size_t>(noms.indexOf(choix))]];
    const QStringList sections{QStringLiteral("Règles"), QStringLiteral("Classes de réseaux"), QStringLiteral("Contraintes"), QStringLiteral("Empilage"), QStringLiteral("Couches visibles"),
                               QStringLiteral("Vues et couleurs"), QStringLiteral("Fabrication"), QStringLiteral("Sérigraphie")};
    const auto garde = cocherListe(this, QStringLiteral("Reprendre les réglages"), QStringLiteral("Sections reprises de « %1 » :").arg(choix), sections);
    if (!garde) return;
    SectionsReglages s;
    s.regles = (*garde)[0];
    s.classes = (*garde)[1];
    s.contraintes = (*garde)[2];
    s.empilage = (*garde)[3];
    s.couches = (*garde)[4];
    s.vues = (*garde)[5];
    s.fabrication = (*garde)[6];
    s.serigraphie = (*garde)[7];
    faire(QStringLiteral("Reprendre les réglages"), [&](ConceptionElec& x) {
      reprendreReglages(x, source, s);
      return QStringLiteral("<b>Réglages repris</b> de « %1 ».").arg(choix);
    });
  };
  c["circuit.echangerCouches"] = [this, faire] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const auto a = demanderCouche(this, *x, QStringLiteral("Échanger deux couches"), QStringLiteral("Première couche :"), Couche::CuivreDessus, true);
    if (!a) return;
    const auto b = demanderCouche(this, *x, QStringLiteral("Échanger deux couches"), QStringLiteral("Seconde couche :"), Couche::CuivreDessous, true);
    if (!b || *a == *b) return;
    faire(QStringLiteral("Échanger deux couches"), [&](ConceptionElec& y) { return QStringLiteral("<b>%1 objet(s)</b> passés d’une couche à l’autre.").arg(echangerCouches(y, {{*a, *b}, {*b, *a}})); });
  };
  c["circuit.textesMasse"] = [this, faire] {
    bool ok = false;
    const double h = QInputDialog::getDouble(this, QStringLiteral("Hauteur des textes"), QStringLiteral("Hauteur des textes et des repères (mm) :"), 1, 0.3, 20, 2, &ok);
    if (!ok) return;
    faire(QStringLiteral("Hauteur des textes"), [&](ConceptionElec& x) { return QStringLiteral("<b>%1 texte(s)</b> et les repères à %2 mm.").arg(modifierTextes(x, {}, nm(h), std::nullopt)).arg(h); });
  };
  c["circuit.origineTravail"] = [this] {
    const Pt p = m_circuit->pointSouris();
    m_doc->reglerSansHistorique([&](prom::Projet& projet) {
      if (m_conception < 0 || m_conception >= static_cast<int>(projet.electronique.size())) return;
      AffichageCircuit& a = projet.electronique[static_cast<std::size_t>(m_conception)].circuit.affichage;
      a.origineTravail = p;
      a.yVersLeHaut = true;
    });
    afficherMessage(QStringLiteral("Origine de travail posée sous la souris, y vers le haut : les coordonnées s’y rapportent."));
    majPanneau();
  };
  c["circuit.origineFichiers"] = [this] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    if (!x->circuit.affichage.origineTravail) {
      afficherMessage(QStringLiteral("Pose d’abord une origine de travail (clic droit sur un trou de fixation › Origine de travail ici)."));
      return;
    }
    const bool oui = !x->circuit.affichage.origineFabrication;
    m_doc->reglerSansHistorique([&](prom::Projet& projet) {
      if (m_conception >= 0 && m_conception < static_cast<int>(projet.electronique.size())) projet.electronique[static_cast<std::size_t>(m_conception)].circuit.affichage.origineFabrication = oui;
    });
    afficherMessage(oui ? QStringLiteral("Fichiers de fabrication et de placement dans le repère de travail.") : QStringLiteral("Fichiers de fabrication et de placement depuis le coin bas gauche de la carte."));
  };
  c["circuit.contourPoints"] = [this, faire] {
    QDialog d(this);
    d.setObjectName(QStringLiteral("dialogueContourPoints"));
    d.setWindowTitle(QStringLiteral("Contour par ses sommets"));
    auto* v = new QVBoxLayout(&d);
    v->addWidget(new QLabel(QStringLiteral("Un sommet par ligne, « x ; y » en millimètres, dans le repère de travail :")));
    auto* t = new QPlainTextEdit;
    t->setObjectName(QStringLiteral("sommets"));
    t->setPlainText(QStringLiteral("0 ; 0\n50 ; 0\n50 ; 40\n0 ; 40"));
    v->addWidget(t);
    auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(b);
    if (d.exec() != QDialog::Accepted) return;
    std::vector<Pt> pts;
    for (const auto& ligne : lireTableau(t->toPlainText().toStdString()))
      if (ligne.size() >= 2) {
        bool ox = false, oy = false;
        const double xx = QLocale().toDouble(q(ligne[0]), &ox), yy = QLocale().toDouble(q(ligne[1]), &oy);
        const double xx2 = ox ? xx : QString::fromStdString(ligne[0]).replace(',', '.').toDouble(&ox), yy2 = oy ? yy : QString::fromStdString(ligne[1]).replace(',', '.').toDouble(&oy);
        if (ox && oy) pts.push_back(ptMm(xx2, yy2));
      }
    faire(QStringLiteral("Contour par ses sommets"), [&](ConceptionElec& x) { return contourDepuisPoints(x, pts) ? QStringLiteral("<b>Contour de %1 sommets</b>.").arg(pts.size()) : QStringLiteral("Il faut au moins trois sommets qui ne soient pas alignés."); });
  };
}

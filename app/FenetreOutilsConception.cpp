// Prométhée : outils de conception (lots L12 à L19). Licence GPL-3.0-only.
#include "FenetreOutilsConception.hpp"

#include <QComboBox>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QLineEdit>
#include <QLocale>
#include <QSignalBlocker>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "Recuperation.hpp"
#include "promethee/collaboration.hpp"
#include "promethee/comparaison.hpp"
#include "promethee/equipe.hpp"
#include "promethee/essais_production.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/historique_local.hpp"
#include "promethee/integrite.hpp"
#include "promethee/logique.hpp"
#include "promethee/modele.hpp"
#include "promethee/revue_conception.hpp"
#include "promethee/sorties.hpp"
#include "promethee/systeme_erc.hpp"
#include "promethee/usinage.hpp"
#include "promethee/variantes.hpp"

using namespace prom::ecad;

namespace {
QString h(const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); }
QString nombre(double v, int d = 2) { return QLocale().toString(v, 'f', d); }
const char* STYLE = "<style>table{border-collapse:collapse}td,th{padding:2px 8px;border-bottom:1px solid #ccc;text-align:left}.err{color:#c53030}.av{color:#b7791f}.ok{color:#2f855a}</style>";
QString tableau(const QStringList& entetes, const std::vector<QStringList>& lignes) {
  QString t = QStringLiteral("<table><tr>");
  for (const auto& e : entetes) t += QStringLiteral("<th>") + e + QStringLiteral("</th>");
  t += QStringLiteral("</tr>");
  for (const auto& l : lignes) {
    t += QStringLiteral("<tr>");
    for (const auto& c : l) t += QStringLiteral("<td>") + c + QStringLiteral("</td>");
    t += QStringLiteral("</tr>");
  }
  return t + QStringLiteral("</table>");
}
QString dossierHistorique(const Document* d) { return d->dossierHistorique(); }
}  // namespace

FenetreOutilsConception::FenetreOutilsConception(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setWindowTitle(QStringLiteral("Outils de conception"));
  setObjectName(QStringLiteral("outilsConception"));
  resize(860, 620);
  auto* l = new QVBoxLayout(this);
  m_onglets = new QTabWidget(this);
  l->addWidget(m_onglets);
  Document* doc = a.document();
  const auto conception = [&a]() { return a.conceptionCourante(); };
  const auto indice = [&a]() { return a.conception(); };

  // ---------- Variantes et coût ----------
  {
    auto* page = onglet(QStringLiteral("variantes"), QStringLiteral("Variantes et coût"), [this, conception]() {
      const ConceptionElec* c = conception();
      if (!c) return QString();
      QString t = QStringLiteral("<h3>Variantes de montage</h3>");
      if (c->variantesMontage.empty()) t += QStringLiteral("<p>Aucune variante : la carte de base seule.</p>");
      else {
        QStringList entetes{QStringLiteral("Repère"), QStringLiteral("Base")};
        for (const auto& v : c->variantesMontage) entetes << h(v.nom);
        std::vector<QStringList> lignes;
        for (const auto& x : tableauVariantes(*c)) {
          QStringList r{h(x.reference), h(x.base)};
          for (const auto& y : x.cellules) r << h(y);
          lignes.push_back(r);
        }
        t += tableau(entetes, lignes);
      }
      for (const auto& e : verifierVariantes(*c)) t += QStringLiteral("<p class='err'>") + h(e) + QStringLiteral("</p>");
      const int n = m_cartes ? m_cartes->value() : 10;
      const CoutNomenclature cn = coutNomenclature(*c, n, c->varianteActive);
      t += QStringLiteral("<h3>Coût pour %1 cartes%2</h3>").arg(n).arg(c->varianteActive.empty() ? QString() : QStringLiteral(" (variante « %1 »)").arg(h(c->varianteActive)));
      std::vector<QStringList> lignes;
      for (const auto& x : cn.lignes) {
        QString refs;
        for (const auto& r : x.references) refs += (refs.isEmpty() ? QString() : QStringLiteral(", ")) + h(r);
        lignes.push_back({refs, h(x.mpn.empty() ? x.valeur : x.mpn), QString::number(x.commandee), x.prixConnu ? nombre(x.prixUnitaire, 4) : QStringLiteral("—"),
                          x.prixConnu ? nombre(x.total) : QStringLiteral("<span class='av'>prix inconnu</span>")});
      }
      t += tableau({QStringLiteral("Repères"), QStringLiteral("Article"), QStringLiteral("Commandés"), QStringLiteral("Prix unitaire"), QStringLiteral("Total")}, lignes);
      t += QStringLiteral("<p><b>Total : %1 %2</b>, soit %3 par carte ; %4 ligne(s) sans prix.</p>").arg(nombre(cn.total), h(cn.devise), nombre(cn.parCarte)).arg(cn.lignesSansPrix);
      return t;
    }, {{QStringLiteral("Nouvelle variante…"), [this, doc, indice, conception] {
          bool ok = false;
          const QString nom = QInputDialog::getText(this, QStringLiteral("Nouvelle variante"), QStringLiteral("Nom de la variante :"), QLineEdit::Normal, {}, &ok);
          if (!ok || nom.trimmed().isEmpty() || !conception()) return;
          doc->modifierConception(indice(), [&](ConceptionElec& c) { ajouterVariante(c, nom.trimmed().toStdString()); }, QStringLiteral("Variante « %1 »").arg(nom.trimmed()));
          refaire();
        }}});
    auto* ligne = new QHBoxLayout;
    ligne->addWidget(new QLabel(QStringLiteral("Variante active :")));
    m_variante = new QComboBox(page);
    m_variante->setObjectName(QStringLiteral("varianteActive"));
    ligne->addWidget(m_variante);
    ligne->addWidget(new QLabel(QStringLiteral("Cartes :")));
    m_cartes = new QSpinBox(page);
    m_cartes->setRange(1, 100000);
    m_cartes->setValue(10);
    ligne->addWidget(m_cartes);
    ligne->addStretch();
    static_cast<QVBoxLayout*>(page->layout())->insertLayout(0, ligne);
    connect(m_variante, &QComboBox::activated, this, [this, doc, indice, conception](int i) {
      const ConceptionElec* c = conception();
      if (!c) return;
      const std::string v = i <= 0 ? std::string() : m_variante->itemText(i).toStdString();
      if (v == c->varianteActive) return;
      doc->modifierConception(indice(), [&](ConceptionElec& x) { x.varianteActive = v; }, QStringLiteral("Variante active : %1").arg(v.empty() ? QStringLiteral("base") : QString::fromStdString(v)));
      refaire();
    });
    connect(m_cartes, qOverload<int>(&QSpinBox::valueChanged), this, [this] { refaire(); });
  }

  // ---------- Revue ----------
  onglet(QStringLiteral("revue"), QStringLiteral("Revue"), [conception]() {
    const ConceptionElec* c = conception();
    if (!c) return QString();
    const auto points = revueConception(*c);
    if (points.empty()) return QStringLiteral("<p class='ok'>Aucun point : LED, régulateurs, I2C, découplage, entrées et tensions des condensateurs sont en ordre.</p>");
    std::vector<QStringList> l;
    for (const auto& p : points)
      l.push_back({QStringLiteral("<span class='%1'>%2</span>").arg(p.gravite == "erreur" ? "err" : "av", h(p.gravite)), h(p.regle), h(p.message), h(p.correction)});
    return tableau({QStringLiteral("Gravité"), QStringLiteral("Règle"), QStringLiteral("Constat"), QStringLiteral("Correction")}, l);
  });

  // ---------- Comparer ----------
  onglet(QStringLiteral("comparer"), QStringLiteral("Comparer"), [this]() { return m_comparaison.isEmpty() ? QStringLiteral("<p>Choisir un fichier à comparer à la conception ouverte.</p>") : m_comparaison; },
         {{QStringLiteral("Comparer avec un fichier…"), [this, conception] {
            const ConceptionElec* c = conception();
            if (!c) return;
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Comparer avec"), {}, QStringLiteral("Projets Prométhée (*.prom *.prom.json)"));
            if (f.isEmpty()) return;
            try {
              const prom::Projet p = prom::lireProjet(f.toStdString());
              const ConceptionElec* autre = nullptr;
              for (const auto& x : p.electronique)
                if (x.uuid == c->uuid) autre = &x;
              if (!autre && !p.electronique.empty()) autre = &p.electronique.front();
              if (!autre) {
                m_comparaison = QStringLiteral("<p class='err'>Ce fichier n'a pas de conception électronique.</p>");
              } else {
                const auto d = comparerConceptions(*c, autre->uuid == c->uuid ? *autre : aligner(*c, *autre));
                QString t = QStringLiteral("<h3>%1 différence(s)</h3>").arg(d.differences.size());
                std::vector<QStringList> l;
                for (const auto& x : d.differences) l.push_back({h(x.categorie), h(x.genre), h(x.repere), h(x.detail)});
                t += tableau({QStringLiteral("Catégorie"), QStringLiteral("Changement"), QStringLiteral("Objet"), QStringLiteral("Détail")}, l);
                m_comparaison = t;
              }
            } catch (const std::exception& e) {
              m_comparaison = QStringLiteral("<p class='err'>") + h(e.what()) + QStringLiteral("</p>");
            }
            refaire();
          }}});

  // ---------- Système ----------
  onglet(QStringLiteral("systeme"), QStringLiteral("Système"), [doc]() {
    const prom::Projet& p = doc->projet();
    if (p.liaisons.empty()) return QStringLiteral("<p>Aucune liaison entre cartes dans ce projet.</p>");
    const prom::NetlistSysteme n = prom::netlistSysteme(p);
    QString t = QStringLiteral("<h3>Réseaux qui traversent les liaisons</h3>");
    std::vector<QStringList> l;
    for (const auto& r : n.reseaux) {
      QString m;
      for (const auto& [carte, reseau] : r.membres) m += (m.isEmpty() ? QString() : QStringLiteral(" = ")) + h(carte + ":" + reseau);
      QString c;
      for (const auto& x : r.liaisons) c += (c.isEmpty() ? QString() : QStringLiteral(", ")) + h(x);
      l.push_back({h(r.nom), m, c});
    }
    t += tableau({QStringLiteral("Réseau"), QStringLiteral("Membres"), QStringLiteral("Liaisons")}, l);
    for (const auto& v : prom::verifierSysteme(p)) t += QStringLiteral("<p class='%1'>").arg(v.erreur ? "err" : "av") + h(v.message) + QStringLiteral("</p>");
    return t;
  });

  // ---------- Notes ----------
  onglet(QStringLiteral("notes"), QStringLiteral("Notes"), [conception]() {
    const ConceptionElec* c = conception();
    if (!c) return QString();
    if (c->notes.empty()) return QStringLiteral("<p>Aucune note.</p>");
    std::vector<QStringList> l;
    for (const auto& n : c->notes)
      l.push_back({h(prom::objetLisible(*c, n)), n.genre == NoteObjet::Genre::Tache ? QStringLiteral("tâche") : n.genre == NoteObjet::Genre::Journal ? QStringLiteral("journal") : QStringLiteral("commentaire"),
                   h(n.auteur), h(n.texte), n.close ? QStringLiteral("close") : QStringLiteral("<b>ouverte</b>")});
    QString t = tableau({QStringLiteral("Objet"), QStringLiteral("Genre"), QStringLiteral("Auteur"), QStringLiteral("Texte"), QStringLiteral("État")}, l);
    const auto attente = prom::enAttente(*c, Document::personne().toStdString());
    if (!attente.empty()) t = QStringLiteral("<p class='av'>%1 note(s) vous attendent.</p>").arg(attente.size()) + t;
    return t;
  }, {{QStringLiteral("Note sur la carte…"), [this, doc, indice, conception] {
         bool ok = false;
         const QString texte = QInputDialog::getMultiLineText(this, QStringLiteral("Note"), QStringLiteral("Texte (@nom pour mentionner) :"), {}, &ok);
         if (!ok || texte.trimmed().isEmpty() || !conception()) return;
         doc->modifierConception(indice(), [&](ConceptionElec& c) {
           NoteObjet n;
           n.auteur = Document::personne().toStdString();
           n.texte = texte.trimmed().toStdString();
           prom::ajouterNote(c, n);
         }, QStringLiteral("Note ajoutée"));
         refaire();
       }},
      {QStringLiteral("Retour de revue…"), [this, doc, indice, conception] {
         const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Retour de revue de la visionneuse"), {}, QStringLiteral("Retours (*.json)"));
         if (f.isEmpty() || !conception()) return;
         std::string texte;
         try {
           texte = prom::lireFichier(prom::cheminFichier(f.toStdString()));
         } catch (const std::exception&) {
           return;
         }
         doc->modifierConception(indice(), [&](ConceptionElec& c) { prom::importerRetourRevue(c, texte); }, QStringLiteral("Retour de revue"));
         refaire();
       }}});

  // ---------- Historique local ----------
  onglet(QStringLiteral("historique"), QStringLiteral("Historique"), [doc]() {
    if (doc->chemin().isEmpty()) return QStringLiteral("<p>Le projet n'est pas encore enregistré : l'historique commence au premier enregistrement.</p>");
    const prom::HistoriqueLocal hl(dossierHistorique(doc).toStdString());
    const auto c = hl.chronologie();
    std::vector<QStringList> l;
    for (auto it = c.rbegin(); it != c.rend(); ++it) l.push_back({h(it->date), h(it->nom), QLocale().formattedDataSize(static_cast<qint64>(it->taille)), h(it->id)});
    return QStringLiteral("<p>%1 instantané(s), %2 sur le disque.</p>").arg(c.size()).arg(QLocale().formattedDataSize(static_cast<qint64>(hl.occupation()))) +
           tableau({QStringLiteral("Date"), QStringLiteral("Nom"), QStringLiteral("Taille"), QStringLiteral("Identifiant")}, l);
  }, {{QStringLiteral("Restaurer en projet séparé…"), [this, doc] {
         if (doc->chemin().isEmpty()) return;
         const prom::HistoriqueLocal hl(dossierHistorique(doc).toStdString());
         QStringList ids;
         for (const auto& x : hl.chronologie()) ids.prepend(QString::fromStdString(x.id + (x.nom.empty() ? "" : " — " + x.nom)));
         if (ids.isEmpty()) return;
         bool ok = false;
         const QString choix = QInputDialog::getItem(this, QStringLiteral("Restaurer"), QStringLiteral("Instantané :"), ids, 0, false, &ok);
         if (!ok) return;
         const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Restaurer sous"), {}, QStringLiteral("Projets Prométhée (*.prom)"));
         if (f.isEmpty()) return;
         try {
           prom::ecrireFichierAtomique(prom::cheminFichier(f.toStdString()), hl.restaurer(choix.section(QStringLiteral(" — "), 0, 0).toStdString()));
         } catch (const std::exception&) {
         }
       }}});

  // ---------- Mise en service, essais, fiabilité ----------
  onglet(QStringLiteral("essais"), QStringLiteral("Mise en service"), [conception]() {
    const ConceptionElec* c = conception();
    if (!c) return QString();
    QString t = QStringLiteral("<h3>Plan de mise sous tension</h3><ol>");
    const PlanMiseSousTension p = planMiseSousTension(*c);
    for (const auto& e : p.etapes) t += QStringLiteral("<li>") + h(e.action) + (e.attendu.empty() ? QString() : QStringLiteral(" — <i>") + h(e.attendu) + QStringLiteral("</i>")) + QStringLiteral("</li>");
    t += QStringLiteral("</ol>");
    for (const auto& a : p.alertes) t += QStringLiteral("<p class='av'>") + h(a) + QStringLiteral("</p>");
    const CouvertureTest cv = couvertureTest(*c);
    t += QStringLiteral("<h3>Accès de test</h3><p>%1 réseau(x) sur %2 accessibles (%3 %).</p>").arg(cv.accessibles).arg(cv.reseaux).arg(nombre(100 * cv.taux, 0));
    std::vector<QStringList> l;
    for (const auto& x : pointsDeTest(*c)) l.push_back({h(x.reference), h(x.reseau), x.chiffree ? nombre(x.attendue, 3) + QStringLiteral(" ± ") + nombre(100 * x.tolerance, 0) + QStringLiteral(" %") : QStringLiteral("—")});
    if (!l.empty()) t += tableau({QStringLiteral("Point"), QStringLiteral("Réseau"), QStringLiteral("Attendu")}, l);
    t += QStringLiteral("<h3>Contraintes d'emploi</h3>");
    for (const auto& x : contraintesEmploi(*c)) t += QStringLiteral("<p class='%1'>").arg(x.depasse ? "err" : "ok") + h(x.message) + QStringLiteral("</p>");
    const Fiabilite f = fiabilite(*c, 40);
    t += QStringLiteral("<h3>Fiabilité à 40 °C</h3><p>%1 FIT, MTBF %2 h.</p>").arg(nombre(f.fitTotal, 1), nombre(f.mtbfHeures, 0));
    const Conformite co = conformite(*c);
    t += QStringLiteral("<h3>Conformité RoHS et REACH</h3><p>%1 composant(s) conformes, %2 non conformes, %3 état(s) non renseignés.</p>").arg(co.conformes).arg(co.nonConformes.size()).arg(co.inconnus.size());
    return t;
  });

  // ---------- Intégrité ----------
  onglet(QStringLiteral("integrite"), QStringLiteral("Intégrité"), [conception]() {
    const ConceptionElec* c = conception();
    if (!c) return QString();
    QString t = QStringLiteral("<h3>Plans de référence</h3>");
    const RapportImpedance r = analyserImpedance(*c);
    if (r.alertes.empty()) t += QStringLiteral("<p class='ok'>Chaque piste routée a un plan sous elle.</p>");
    for (const auto& a : r.alertes) t += QStringLiteral("<p class='av'>") + h(a) + QStringLiteral("</p>");
    t += QStringLiteral("<h3>Diaphonie</h3>");
    std::vector<QStringList> l;
    for (const auto& x : analyserDiaphonie(*c))
      l.push_back({h(x.agresseur + " ↔ " + x.victime), QString::fromLatin1(codeCouche(x.couche)), nombre(x.longueurCouplee, 1) + QStringLiteral(" mm"), nombre(x.ecart, 2) + QStringLiteral(" mm"), nombre(100 * x.coefficient, 0) + QStringLiteral(" %")});
    t += l.empty() ? QStringLiteral("<p class='ok'>Aucun couplage notable.</p>") : tableau({QStringLiteral("Réseaux"), QStringLiteral("Couche"), QStringLiteral("Couplés"), QStringLiteral("Écart"), QStringLiteral("Couplage")}, l);
    t += QStringLiteral("<h3>Découplage</h3>");
    std::vector<QStringList> d;
    for (const auto& x : analyserDecouplage(*c)) d.push_back({h(x.condensateur), h(x.reseau), nombre(x.longueur, 1) + QStringLiteral(" mm"), QString::number(x.vias), nombre(x.inductance, 1) + QStringLiteral(" nH")});
    t += d.empty() ? QStringLiteral("<p>Aucun condensateur entre une alimentation et la masse.</p>") : tableau({QStringLiteral("Condensateur"), QStringLiteral("Rail"), QStringLiteral("Piste"), QStringLiteral("Vias"), QStringLiteral("Inductance")}, d);
    return t;
  });

  // ---------- Logique ----------
  onglet(QStringLiteral("logique"), QStringLiteral("Logique"), [conception]() {
    const ConceptionElec* c = conception();
    if (!c) return QString();
    const CircuitLogique cl = circuitLogique(*c);
    if (cl.portes.empty()) return QStringLiteral("<p>Aucune porte logique (bibliothèque « Logique », 74HC00 par porte).</p>");
    std::vector<std::string> sorties;
    for (const auto& p : cl.portes)
      for (int s : p.sorties)
        if (s >= 0 && cl.signaux[static_cast<std::size_t>(s)].find('(') == std::string::npos && cl.signaux[static_cast<std::size_t>(s)].rfind("Net-", 0) != 0)
          sorties.push_back(cl.signaux[static_cast<std::size_t>(s)]);
    QString t = QStringLiteral("<p>%1 porte(s), %2 entrée(s).</p>").arg(cl.portes.size()).arg(cl.entrees.size());
    if (cl.entrees.size() <= 8) {
      QStringList entetes;
      for (const auto& e : cl.entrees) entetes << h(e);
      for (const auto& s : sorties) entetes << QStringLiteral("<b>") + h(s) + QStringLiteral("</b>");
      std::vector<QStringList> l;
      for (const auto& [e, s] : tableVerite(cl, cl.entrees, sorties)) {
        QStringList r;
        for (char ch : e) r << QString(QChar(ch));
        for (char ch : s) r << QStringLiteral("<b>") + QString(QChar(ch)) + QStringLiteral("</b>");
        l.push_back(r);
      }
      t += tableau(entetes, l);
    }
    for (const auto& a : cl.alertes) t += QStringLiteral("<p class='av'>") + h(a) + QStringLiteral("</p>");
    return t;
  }, {{QStringLiteral("Exporter en Verilog…"), [this, conception] {
         const ConceptionElec* c = conception();
         if (!c) return;
         const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Verilog"), QString::fromStdString(prom::nomFichierAscii(c->nom, "carte")) + QStringLiteral(".v"), QStringLiteral("Verilog (*.v)"));
         if (!f.isEmpty()) prom::ecrireFichierAtomique(prom::cheminFichier(f.toStdString()), verilogStructurel(circuitLogique(*c), prom::nomFichierAscii(c->nom, "carte")));
       }}});

  // ---------- Atelier : gravure à l'anglaise, pochoir, jeux de sorties ----------
  onglet(QStringLiteral("atelier"), QStringLiteral("Atelier"), [conception]() {
    const ConceptionElec* c = conception();
    if (!c) return QString();
    QString t = QStringLiteral("<h3>Gravure à l'anglaise (outil de 0,2 mm, deux passes)</h3>");
    for (const auto couche : {Couche::CuivreDessus, Couche::CuivreDessous}) {
      const ResultatIsolement r = sillonsIsolement(*c, couche, {});
      t += QStringLiteral("<p>%1 : %2 sillons, %3 mm.</p>").arg(QString::fromLatin1(codeCouche(couche))).arg(r.sillons.size()).arg(nombre(r.longueur, 0));
      for (const auto& a : r.alertes) t += QStringLiteral("<p class='av'>") + h(a) + QStringLiteral("</p>");
    }
    t += QStringLiteral("<h3>Jeux de sorties</h3>");
    if (c->jeuxSorties.empty()) t += QStringLiteral("<p>Aucun jeu enregistré ; « Dossier JLCPCB » est toujours proposé.</p>");
    for (const auto& j : c->jeuxSorties) t += QStringLiteral("<p>") + h(j.nom) + QStringLiteral(" → ") + h(j.dossier) + QStringLiteral("</p>");
    return t;
  }, {{QStringLiteral("Exporter le G-code…"), [this, conception] {
         const ConceptionElec* c = conception();
         if (!c) return;
         const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier du G-code"));
         if (d.isEmpty()) return;
         const auto ecrire = [&](const char* nom, const std::string& texte) { prom::ecrireFichierAtomique(prom::cheminFichier(QDir(d).filePath(QString::fromLatin1(nom)).toStdString()), texte); };
         OptionsUsinage o;
         ecrire("isolement-dessus.nc", gcodeIsolement(*c, sillonsIsolement(*c, Couche::CuivreDessus, o), o));
         o.miroir = true;
         ecrire("isolement-dessous.nc", gcodeIsolement(*c, sillonsIsolement(*c, Couche::CuivreDessous, o), o));
         o.miroir = false;
         ecrire("percages.nc", gcodePercages(*c, o));
         ecrire("decoupe.nc", gcodeDecoupe(*c, o));
         ecrire("pochoir-dessus.svg", pochoirSvg(*c, false));
         ecrire("pochoir-dessus.dxf", pochoirDxf(*c, false));
         ecrire("nettoyage-dessous.nc", gcodeIsolement(*c, nettoyageCuivre(*c, Couche::CuivreDessous, o), o));
         // Palpage de la plaque (16.8) : grille de 11 × 9 points sur la carte, relevé écrit par la machine
         const Boite b = boite(c->circuit.contour.polygone());
         OptionsPalpage p;
         p.x1 = mm(b.largeur()), p.y1 = mm(b.hauteur());
         ecrire("palpage-linuxcnc.nc", gcodePalpage(p));
         p.machine = "grbl";
         ecrire("palpage-grbl.nc", gcodePalpage(p));
       }},
      {QStringLiteral("Corriger d'après un relevé…"), [this] {
         // Relevé de palpage (LinuxCNC, GRBL, Marlin) puis les G-code à corriger : « …-corrige.nc » à côté de chacun
         const QString releve = QFileDialog::getOpenFileName(this, QStringLiteral("Relevé de palpage"), {}, QStringLiteral("Relevés (*.txt *.log *.csv);;Tous (*)"));
         if (releve.isEmpty()) return;
         std::string erreur;
         const auto g = lireRelevePalpage(prom::lireFichier(prom::cheminFichier(releve.toStdString())), &erreur);
         if (!g) {
           m_textes[QStringLiteral("atelier")]->append(QStringLiteral("<p class='err'>") + h(erreur) + QStringLiteral("</p>"));
           return;
         }
         const QStringList fichiers = QFileDialog::getOpenFileNames(this, QStringLiteral("G-code à corriger"), QFileInfo(releve).absolutePath(), QStringLiteral("G-code (*.nc *.ngc *.gcode)"));
         for (const QString& f : fichiers) {
           const QString sortie = QFileInfo(f).absoluteDir().filePath(QFileInfo(f).completeBaseName() + QStringLiteral("-corrige.nc"));
           prom::ecrireFichierAtomique(prom::cheminFichier(sortie.toStdString()), corrigerProfondeur(prom::lireFichier(prom::cheminFichier(f.toStdString())), *g));
           m_textes[QStringLiteral("atelier")]->append(QStringLiteral("<p>Profondeur corrigée (%1 × %2 points) : %3</p>").arg(g->nx).arg(g->ny).arg(h(sortie.toStdString())));
         }
       }},
      {QStringLiteral("Dossier JLCPCB…"), [this, doc, conception] {
         const ConceptionElec* c = conception();
         if (!c) return;
         const QString base = doc->chemin().isEmpty() ? QDir::homePath() : QFileInfo(doc->chemin()).absolutePath();
         const ResultatJeu r = executerJeu(*c, jeuJlcpcb(), base.toStdString(), prom::nomFichierAscii(doc->projet().nom, "projet"), "Prométhée");
         m_textes[QStringLiteral("atelier")]->append(QStringLiteral("<p>") + h(r.journal.empty() ? std::string() : r.journal.front()) + QStringLiteral("</p>"));
         for (const auto& x : r.refus) m_textes[QStringLiteral("atelier")]->append(QStringLiteral("<p class='err'>") + h(x) + QStringLiteral("</p>"));
       }}});

  // ---------- Équipe et exigences ----------
  onglet(QStringLiteral("exigences"), QStringLiteral("Exigences"), [doc]() {
    const prom::Projet& p = doc->projet();
    if (p.exigences.empty()) return QStringLiteral("<p>Aucune exigence dans ce projet.</p>");
    std::vector<QStringList> l;
    for (const auto& v : prom::verifierExigences(p)) {
      const QString classe = v.etat == "vérifiée" ? QStringLiteral("ok") : v.etat == "en défaut" ? QStringLiteral("err") : QStringLiteral("av");
      QString manquants;
      for (const auto& m : v.liensManquants) manquants += (manquants.isEmpty() ? QString() : QStringLiteral(", ")) + h(m);
      l.push_back({h(v.id), QStringLiteral("<span class='%1'>%2</span>").arg(classe, h(v.etat)), h(v.mesure), manquants});
    }
    QString t = tableau({QStringLiteral("Exigence"), QStringLiteral("État"), QStringLiteral("Mesure"), QStringLiteral("Liens manquants")}, l);
    if (!p.ordresModification.empty()) {
      std::vector<QStringList> o;
      for (const auto& x : p.ordresModification) o.push_back({h(x.id), h(x.titre), h(x.etat), h(x.revisionDepart + " → " + x.revisionArrivee)});
      t += QStringLiteral("<h3>Ordres de modification</h3>") + tableau({QStringLiteral("Ordre"), QStringLiteral("Titre"), QStringLiteral("État"), QStringLiteral("Révisions")}, o);
    }
    return t;
  });
  ajouterOnglets034();     // lots L12 à L19 de la 0.34 (OutilsConception034.cpp)
  ajouterOnglets034L19();  // achats et PLM (OutilsConception034L19.cpp)
  refaire();
}

QWidget* FenetreOutilsConception::onglet(const QString& id, const QString& titre, const std::function<QString()>& html, const std::vector<std::pair<QString, std::function<void()>>>& boutons) {
  auto* page = new QWidget(m_onglets);
  page->setObjectName(QStringLiteral("onglet-") + id);
  auto* l = new QVBoxLayout(page);
  auto* texte = new QTextBrowser(page);
  texte->setObjectName(QStringLiteral("rapport-") + id);
  texte->setOpenLinks(false);
  l->addWidget(texte);
  auto* barre = new QHBoxLayout;
  auto* refaireBouton = new QPushButton(QStringLiteral("Relire"), page);
  connect(refaireBouton, &QPushButton::clicked, this, [this] { refaire(); });
  barre->addWidget(refaireBouton);
  for (const auto& [libelle, action] : boutons) {
    auto* b = new QPushButton(libelle, page);
    connect(b, &QPushButton::clicked, this, action);
    barre->addWidget(b);
  }
  barre->addStretch();
  l->addLayout(barre);
  m_indices[id] = m_onglets->addTab(page, titre);
  m_textes[id] = texte;
  m_rapports[id] = html;
  return page;
}

void FenetreOutilsConception::refaire() {
  majTableauBord();  // suivi des commandes suspendu fenêtre fermée
  if (m_variante) {
    const ConceptionElec* c = m_atelier.conceptionCourante();
    const QSignalBlocker b(m_variante);
    m_variante->clear();
    m_variante->addItem(QStringLiteral("Carte de base"));
    if (c)
      for (const auto& v : c->variantesMontage) m_variante->addItem(QString::fromStdString(v.nom));
    m_variante->setCurrentIndex(c && !c->varianteActive.empty() ? std::max(0, m_variante->findText(QString::fromStdString(c->varianteActive))) : 0);
  }
  for (const auto& [id, f] : m_rapports) {
    QString t;
    try {
      t = f();
    } catch (const std::exception& e) {
      t = QStringLiteral("<p class='err'>") + h(e.what()) + QStringLiteral("</p>");
    }
    m_textes[id]->setHtml(QString::fromLatin1(STYLE) + t);
  }
}

bool FenetreOutilsConception::montrer(const QString& id) {
  const auto it = m_indices.find(id);
  if (it == m_indices.end()) return false;
  m_onglets->setCurrentIndex(it->second);
  return true;
}

QString FenetreOutilsConception::rapport(const QString& id) const {
  const auto it = m_textes.find(id);
  return it == m_textes.end() ? QString() : it->second->toPlainText();
}

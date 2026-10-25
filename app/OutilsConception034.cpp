// Prométhée : onglets de la 0.34 dans la fenêtre des outils de conception. Lot L12 : partage et matériel libre
// (12.11), revue formelle (12.13), bibliothèque hors réseau (12.15), Git (12.16), nomenclature du système (12.18),
// liens entre disciplines et faisceaux entre cartes (12.6, 12.20). Chaque action qui modifie passe par le document,
// en un pas d'annulation. Licence GPL-3.0-only.
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QInputDialog>
#include <QLocale>
#include <QMessageBox>
#include <QSettings>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "FenetreOutilsConception.hpp"
#include "SelectionElectronique.hpp"
#include "promethee/collaboration.hpp"
#include "promethee/electrique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/git_projet.hpp"
#include "promethee/gestion_bibliotheque.hpp"
#include "promethee/modele.hpp"
#include "promethee/projet_partage.hpp"

using namespace prom::ecad;

namespace {
QString h(const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); }
QString q(const std::string& s) { return QString::fromStdString(s); }
QString tableau034(const QStringList& entetes, const std::vector<QStringList>& lignes) {
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
std::string aujourdhui() { return QDate::currentDate().toString(Qt::ISODate).toStdString(); }
bool ecrire(QWidget* w, const QString& chemin, const std::string& contenu) {
  try {
    prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), contenu);
    return true;
  } catch (const std::exception& e) {
    QMessageBox::warning(w, QStringLiteral("Écriture"), QString::fromUtf8(e.what()));
    return false;
  }
}
// Dépôt Git du projet : le dossier du fichier ouvert ; le réseau est un réglage du poste, par dossier
prom::DepotGit depotDuProjet(const Document* doc) {
  prom::DepotGit d;
  if (!doc->chemin().isEmpty()) d.dossier = QFileInfo(doc->chemin()).absolutePath().toStdString();
  QSettings s;
  const QString cle = QStringLiteral("git/") + QString::fromUtf8(QCryptographicHash::hash(QByteArray::fromStdString(d.dossier), QCryptographicHash::Sha1).toHex());
  d.reseauPermis = s.value(cle + QStringLiteral("/reseau"), false).toBool();
  d.coupe = s.value(cle + QStringLiteral("/coupe"), false).toBool();
  d.auteur = s.value(QStringLiteral("git/auteur"), Document::personne()).toString().toStdString();
  d.courriel = s.value(QStringLiteral("git/courriel")).toString().toStdString();
  d.cleSsh = s.value(cle + QStringLiteral("/cle")).toString().toStdString();
  return d;
}
void reglerDepot(const Document* doc, const QString& reglage, const QVariant& v) {
  QSettings s;
  const std::string dossier = doc->chemin().isEmpty() ? std::string() : QFileInfo(doc->chemin()).absolutePath().toStdString();
  s.setValue(QStringLiteral("git/") + QString::fromUtf8(QCryptographicHash::hash(QByteArray::fromStdString(dossier), QCryptographicHash::Sha1).toHex()) + QLatin1Char('/') + reglage, v);
}
}  // namespace

void FenetreOutilsConception::ajouterOnglets034() {
  AtelierElectronique& a = m_atelier;
  Document* doc = a.document();
  const auto conception = [&a]() { return a.conceptionCourante(); };
  const auto indice = [&a]() { return a.conception(); };

  // ---------- 12.11 · Partager et publier ----------
  onglet(QStringLiteral("partage"), QStringLiteral("Partager"), [doc]() {
    const prom::Projet& p = doc->projet();
    QString t = QStringLiteral("<p>Trois sorties : la <b>visionneuse</b> pour un relecteur (une page qui s’ouvre sans Prométhée), l’<b>archive complète</b> pour un collègue (projet et fichiers cités, empreintes vérifiées à l’ouverture), le <b>dossier publié</b> pour le fabricant (révision verrouillée).</p>");
    const prom::LicenceLibre* l = prom::licenceLibre(p.licence);
    t += QStringLiteral("<p>Licence du projet : <b>%1</b></p>").arg(l ? h(l->nom + " (" + l->spdx + ")") : QStringLiteral("non déclarée"));
    std::vector<QStringList> lignes;
    for (const auto& c : p.electronique)
      for (const auto& [id, art] : c.articles) lignes.push_back({h(c.nom), h(art.nom.empty() ? id : art.nom), h(art.mpn), art.licence.empty() ? QStringLiteral("<span class='av'>non déclarée</span>") : h(art.licence)});
    if (!lignes.empty()) t += QStringLiteral("<h3>Licences des articles</h3>") + tableau034({QStringLiteral("Carte"), QStringLiteral("Article"), QStringLiteral("Référence"), QStringLiteral("Licence")}, lignes);
    return t;
  },
         {{QStringLiteral("Visionneuse…"), [this, &a] {
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Visionneuse pour un relecteur"), QStringLiteral("visionneuse.html"), QStringLiteral("Page HTML (*.html)"));
            if (!f.isEmpty()) a.exporterVisionneuse(f);
          }},
          {QStringLiteral("Archive complète…"), [this, doc] {
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Archive pour un collègue"), QStringLiteral("projet.zip"), QStringLiteral("Archive (*.zip)"));
            if (f.isEmpty()) return;
            std::vector<std::string> manquants;
            if (ecrire(this, f, prom::archiverProjet(doc->projet(), doc->chemin().toStdString(), &manquants)) && !manquants.empty())
              QMessageBox::information(this, QStringLiteral("Archive"), QStringLiteral("%1 fichier(s) cité(s) introuvable(s) : non joints.").arg(manquants.size()));
          }},
          {QStringLiteral("Dossier publié…"), [&a] { a.executerAction("fabrication.publier"); }},
          {QStringLiteral("Licence du projet…"), [this, doc] {
            QStringList noms;
            for (const auto& l : prom::licencesLibres()) noms << q(l.spdx + " — " + l.nom);
            bool ok = false;
            const QString choix = QInputDialog::getItem(this, QStringLiteral("Licence"), QStringLiteral("Licence du projet"), noms, 0, false, &ok);
            if (!ok) return;
            const std::string spdx = choix.section(QStringLiteral(" — "), 0, 0).toStdString();
            doc->modifier([&](prom::Projet& p) { p.licence = spdx; }, QStringLiteral("Licence du projet : %1").arg(q(spdx)));
            refaire();
          }},
          {QStringLiteral("Publier en matériel libre…"), [this, doc] {
            prom::OptionsMaterielLibre o;
            o.licence = doc->projet().licence.empty() ? std::string("CERN-OHL-S-2.0") : doc->projet().licence;
            bool ok = false;
            o.detenteur = QInputDialog::getText(this, QStringLiteral("Matériel libre"), QStringLiteral("Détenteur des droits"), QLineEdit::Normal, Document::personne(), &ok).toStdString();
            if (!ok) return;
            o.description = QInputDialog::getMultiLineText(this, QStringLiteral("Matériel libre"), QStringLiteral("Description du projet"), {}, &ok).toStdString();
            if (!ok) return;
            o.annee = std::to_string(QDate::currentDate().year());
            const prom::PaquetMaterielLibre pk = prom::publierMaterielLibre(doc->projet(), doc->chemin().toStdString(), o);
            if (!pk.erreur.empty()) {
              QMessageBox::warning(this, QStringLiteral("Matériel libre"), q(pk.erreur));
              return;
            }
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Paquet de matériel libre"), q(prom::nomFichierAscii(doc->projet().nom, "projet")) + QStringLiteral("-libre.zip"), QStringLiteral("Archive (*.zip)"));
            if (f.isEmpty() || !ecrire(this, f, pk.zip)) return;
            QString m = QStringLiteral("%1 fichier(s) dans le paquet.").arg(pk.fichiers.size());
            for (const auto& x : pk.alertes) m += QStringLiteral("\n") + q(x);
            QMessageBox::information(this, QStringLiteral("Matériel libre"), m);
          }}});

  // ---------- 12.13 · Revue formelle ----------
  onglet(QStringLiteral("revueFormelle"), QStringLiteral("Revue formelle"), [conception]() {
    const ConceptionElec* c = conception();
    if (!c) return QString();
    const auto v = prom::revuesDe(*c);
    QString t = QStringLiteral("<p>Révision en cours : <b>%1</b> — empreinte SHA-256 <code>%2</code></p>").arg(h(c->cartouche.revision), h(prom::empreinteRevision(*c)));
    if (v.empty()) return t + QStringLiteral("<p>Aucune revue formelle.</p>");
    for (const auto& r : v) {
      t += QStringLiteral("<h3>Revue n° %1 : %2 %3</h3><p>Révision %4, empreinte <code>%5</code></p>")
               .arg(r.numero)
               .arg(h(r.titre), r.close() ? QStringLiteral("<span class='ok'>(close)</span>") : QStringLiteral("<span class='av'>(en cours)</span>"), h(r.revision), h(r.empreinte));
      std::vector<QStringList> l;
      for (const auto& k : r.constats)
        l.push_back({h(k.id), h(k.objet), h(k.texte), h(k.auteur), k.etat == "clos" ? QStringLiteral("<span class='ok'>clos</span>") : QStringLiteral("<span class='err'>ouvert</span>"), h(k.cloture)});
      if (!l.empty()) t += tableau034({QStringLiteral("N°"), QStringLiteral("Objet"), QStringLiteral("Constat"), QStringLiteral("Par"), QStringLiteral("État"), QStringLiteral("Clôture")}, l);
      QString sig;
      for (const auto& role : r.rolesAttendus) {
        QString qui = QStringLiteral("à signer");
        for (const auto& s : r.signatures)
          if (s.role == role) qui = h(s.nom + ", " + s.date);
        sig += QStringLiteral("%1 : %2 ; ").arg(h(role), qui);
      }
      t += QStringLiteral("<p>Signatures : %1</p>").arg(sig);
    }
    return t;
  },
         {{QStringLiteral("Ouvrir une revue…"), [this, &a, indice] {
            bool ok = false;
            const QString titre = QInputDialog::getText(this, QStringLiteral("Revue formelle"), QStringLiteral("Titre"), QLineEdit::Normal, {}, &ok);
            if (!ok) return;
            a.document()->modifierConception(indice(), [&](ConceptionElec& c) { prom::ouvrirRevue(c, titre.toStdString(), aujourdhui()); }, QStringLiteral("Revue formelle ouverte"));
            refaire();
          }},
          {QStringLiteral("Constat…"), [this, &a, indice, conception] {
            const ConceptionElec* c = conception();
            if (!c || prom::revuesDe(*c).empty()) return;
            const int n = prom::revuesDe(*c).back().numero;
            std::string objet;
            if (a.selection() && !a.selection()->composants().empty())
              if (const ComposantElec* k = c->composant(*a.selection()->composants().begin())) objet = k->reference;
            bool ok = false;
            const QString o = QInputDialog::getText(this, QStringLiteral("Constat"), QStringLiteral("Objet (repère, réseau, feuille)"), QLineEdit::Normal, q(objet), &ok);
            if (!ok) return;
            const QString texte = QInputDialog::getMultiLineText(this, QStringLiteral("Constat"), QStringLiteral("Constat"), {}, &ok);
            if (!ok || texte.trimmed().isEmpty()) return;
            a.document()->modifierConception(indice(), [&](ConceptionElec& x) { prom::ajouterConstat(x, n, o.toStdString(), texte.toStdString(), Document::personne().toStdString(), aujourdhui()); },
                                             QStringLiteral("Constat de revue"));
            refaire();
          }},
          {QStringLiteral("Clore un constat…"), [this, &a, indice, conception] {
            const ConceptionElec* c = conception();
            if (!c || prom::revuesDe(*c).empty()) return;
            const auto r = prom::revuesDe(*c).back();
            QStringList ouverts;
            for (const auto& k : r.constats)
              if (k.etat != "clos") ouverts << q(k.id + " — " + k.texte);
            if (ouverts.isEmpty()) return;
            bool ok = false;
            const QString choix = QInputDialog::getItem(this, QStringLiteral("Clore"), QStringLiteral("Constat"), ouverts, 0, false, &ok);
            if (!ok) return;
            const QString reponse = QInputDialog::getMultiLineText(this, QStringLiteral("Clore"), QStringLiteral("Ce qui a été fait"), {}, &ok);
            if (!ok) return;
            const std::string id = choix.section(QStringLiteral(" — "), 0, 0).toStdString();
            a.document()->modifierConception(indice(), [&](ConceptionElec& x) { prom::clore(x, r.numero, id, reponse.toStdString(), Document::personne().toStdString(), aujourdhui()); },
                                             QStringLiteral("Constat %1 clos").arg(q(id)));
            refaire();
          }},
          {QStringLiteral("Signer…"), [this, &a, indice, conception] {
            const ConceptionElec* c = conception();
            if (!c || prom::revuesDe(*c).empty()) return;
            const auto r = prom::revuesDe(*c).back();
            QStringList roles;
            for (const auto& x : r.rolesAttendus) roles << q(x);
            bool ok = false;
            const QString role = QInputDialog::getItem(this, QStringLiteral("Signer"), QStringLiteral("Rôle"), roles, 0, false, &ok);
            if (!ok) return;
            std::string refus;
            a.document()->modifierConception(indice(), [&](ConceptionElec& x) { refus = prom::signer(x, r.numero, role.toStdString(), Document::personne().toStdString(), aujourdhui()); },
                                             QStringLiteral("Revue signée (%1)").arg(role));
            if (!refus.empty()) QMessageBox::warning(this, QStringLiteral("Signature refusée"), q(refus));
            refaire();
          }},
          {QStringLiteral("Rapport PDF…"), [this, conception] {
            const ConceptionElec* c = conception();
            if (!c || prom::revuesDe(*c).empty()) return;
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Rapport de revue"), QStringLiteral("revue.pdf"), QStringLiteral("PDF (*.pdf)"));
            if (!f.isEmpty()) ecrire(this, f, prom::pdfRevue(*c, prom::revuesDe(*c).back()));
          }},
          {QStringLiteral("Historique PDF…"), [this, conception] {
            const ConceptionElec* c = conception();
            if (!c) return;
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Historique des révisions"), QStringLiteral("historique.pdf"), QStringLiteral("PDF (*.pdf)"));
            if (!f.isEmpty()) ecrire(this, f, prom::pdfHistorique(*c));
          }}});

  // ---------- 12.15 · Bibliothèque d'équipe hors réseau ----------
  onglet(QStringLiteral("horsReseau"), QStringLiteral("Hors réseau"), [this]() {
    QString t = QStringLiteral("<p>Avant de partir : <b>prendre une copie locale</b> de la bibliothèque d’équipe. Au retour : <b>reverser</b> la copie. Les articles nouveaux rejoignent l’équipe avec leur numéro de révision, rattachés à l’article de même référence fabricant s’il a été publié entre-temps, sans doublon.</p>");
    if (!m_bilanReversement.isEmpty()) t += QStringLiteral("<h3>Dernier reversement</h3>") + m_bilanReversement;
    return t;
  },
         {{QStringLiteral("Prendre une copie locale…"), [this] {
            const QString equipe = QFileDialog::getOpenFileName(this, QStringLiteral("Bibliothèque d’équipe"), {}, QStringLiteral("Bibliothèque (*.json *.prombib)"));
            if (equipe.isEmpty()) return;
            std::string erreur;
            const ContenuBibliotheque b = lireContenuBibliotheque(prom::lireFichier(prom::cheminFichier(equipe.toStdString())), &erreur);
            if (!erreur.empty()) {
              QMessageBox::warning(this, QStringLiteral("Copie locale"), q(erreur));
              return;
            }
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Copie locale"), QStringLiteral("copie-locale.json"), QStringLiteral("Copie locale (*.json)"));
            if (!f.isEmpty() && ecrire(this, f, prom::texteCopieLocale(prom::copierPourHorsReseau(b, aujourdhui())))) {
              QSettings().setValue(QStringLiteral("bibliotheque/copieLocale"), f);
              QSettings().setValue(QStringLiteral("bibliotheque/equipe"), equipe);
            }
          }},
          {QStringLiteral("Reverser la copie locale"), [this] {
            QSettings s;
            QString copie = s.value(QStringLiteral("bibliotheque/copieLocale")).toString(), equipe = s.value(QStringLiteral("bibliotheque/equipe")).toString();
            if (copie.isEmpty()) copie = QFileDialog::getOpenFileName(this, QStringLiteral("Copie locale"), {}, QStringLiteral("Copie locale (*.json)"));
            if (equipe.isEmpty()) equipe = QFileDialog::getOpenFileName(this, QStringLiteral("Bibliothèque d’équipe"), {}, QStringLiteral("Bibliothèque (*.json *.prombib)"));
            if (copie.isEmpty() || equipe.isEmpty()) return;
            try {
              std::string erreur;
              auto c = prom::lireCopieLocale(prom::lireFichier(prom::cheminFichier(copie.toStdString())), &erreur);
              ContenuBibliotheque b = lireContenuBibliotheque(prom::lireFichier(prom::cheminFichier(equipe.toStdString())), &erreur);
              if (!c) throw std::runtime_error(erreur);
              const prom::BilanReversement r = prom::reverser(b, *c);
              prom::ecrireFichierAtomique(prom::cheminFichier(equipe.toStdString()), texteContenuBibliotheque(b));
              prom::ecrireFichierAtomique(prom::cheminFichier(copie.toStdString()), prom::texteCopieLocale(*c));
              m_bilanReversement = QStringLiteral("<p>") + h(r.texte()) + QStringLiteral("</p>");
              for (const auto& x : r.conflits) m_bilanReversement += QStringLiteral("<p class='err'>") + h(x) + QStringLiteral("</p>");
              for (const auto& [l, e] : r.rattaches) m_bilanReversement += QStringLiteral("<p>« %1 » rattaché à « %2 »</p>").arg(h(l), h(e));
            } catch (const std::exception& e) {
              QMessageBox::warning(this, QStringLiteral("Reversement"), QString::fromUtf8(e.what()));
            }
            refaire();
          }}});

  // ---------- 12.16 · Git ----------
  onglet(QStringLiteral("git"), QStringLiteral("Git"), [doc]() {
    std::string version;
    if (!prom::gitDisponible(&version)) return QStringLiteral("<p class='av'>git n’est pas installé sur ce poste : installe-le pour suivre le projet par Git.</p>");
    if (doc->chemin().isEmpty()) return QStringLiteral("<p>Enregistre d’abord le projet : son dossier devient le dépôt.</p>");
    const prom::DepotGit d = depotDuProjet(doc);
    QString t = QStringLiteral("<p>%1 — dépôt : <code>%2</code></p>").arg(h(version), h(d.dossier));
    if (d.coupe) return t + QStringLiteral("<p class='av'>Git est coupé pour ce projet.</p>");
    const std::string branche = prom::gitBrancheCourante(d);
    if (branche.empty()) return t + QStringLiteral("<p>Pas encore de dépôt dans ce dossier.</p>");
    t += QStringLiteral("<p>Branche : <b>%1</b> ; réseau : %2</p>").arg(h(branche), d.reseauPermis ? QStringLiteral("<span class='av'>activé pour ce dépôt</span>") : QStringLiteral("<span class='ok'>coupé (rien ne part ni n’arrive)</span>"));
    QString b;
    for (const auto& x : prom::gitBranches(d)) b += h(x) + QStringLiteral(" ");
    t += QStringLiteral("<p>Branches : %1</p>").arg(b);
    if (const std::string u = prom::gitAdresseDistant(d, "origin"); !u.empty()) t += QStringLiteral("<p>Dépôt distant « origin » : <code>%1</code>%2</p>").arg(h(u), prom::adresseReseau(u) ? QStringLiteral(" (par le réseau)") : QString());
    return t;
  },
         {{QStringLiteral("Créer le dépôt"), [this, doc] {
            const prom::ResultatGit r = prom::gitInitialiser(depotDuProjet(doc));
            QMessageBox::information(this, QStringLiteral("Git"), q(r.message));
            refaire();
          }},
          {QStringLiteral("Valider…"), [this, doc] {
            bool ok = false;
            const QString m = QInputDialog::getText(this, QStringLiteral("Valider"), QStringLiteral("Ce qui a changé"), QLineEdit::Normal, {}, &ok);
            if (!ok || m.trimmed().isEmpty()) return;
            QMessageBox::information(this, QStringLiteral("Git"), q(prom::gitValider(depotDuProjet(doc), m.toStdString()).message));
            refaire();
          }},
          {QStringLiteral("Branche…"), [this, doc] {
            bool ok = false;
            const QString b = QInputDialog::getText(this, QStringLiteral("Branche"), QStringLiteral("Branche (nouvelle ou existante)"), QLineEdit::Normal, {}, &ok);
            if (!ok || b.trimmed().isEmpty()) return;
            const prom::DepotGit d = depotDuProjet(doc);
            const auto existantes = prom::gitBranches(d);
            const bool creer = std::find(existantes.begin(), existantes.end(), b.toStdString()) == existantes.end();
            QMessageBox::information(this, QStringLiteral("Git"), q(prom::gitBrancher(d, b.toStdString(), creer).message));
            refaire();
          }},
          {QStringLiteral("Dépôt distant…"), [this, doc] {
            bool ok = false;
            const QString u = QInputDialog::getText(this, QStringLiteral("Dépôt distant"), QStringLiteral("Adresse (dossier partagé, ou ssh/https si le réseau est activé)"), QLineEdit::Normal, {}, &ok);
            if (!ok || u.trimmed().isEmpty()) return;
            if (prom::adresseReseau(u.toStdString())) {
              const QString cle = QFileDialog::getOpenFileName(this, QStringLiteral("Clé SSH (facultative)"), QDir::homePath() + QStringLiteral("/.ssh"));
              if (!cle.isEmpty()) reglerDepot(doc, QStringLiteral("cle"), cle);
            }
            QMessageBox::information(this, QStringLiteral("Git"), q(prom::gitRattacherDistant(depotDuProjet(doc), "origin", u.trimmed().toStdString()).message));
            refaire();
          }},
          {QStringLiteral("Réseau pour ce dépôt"), [this, doc] {
            const bool actif = depotDuProjet(doc).reseauPermis;
            const auto r = QMessageBox::question(this, QStringLiteral("Réseau"), actif ? QStringLiteral("Couper le réseau pour ce dépôt ?") : QStringLiteral("Permettre à Git d’échanger avec le dépôt distant par le réseau, pour ce dépôt seulement ?"));
            if (r == QMessageBox::Yes) reglerDepot(doc, QStringLiteral("reseau"), !actif);
            refaire();
          }},
          {QStringLiteral("Pousser"), [this, doc] {
            const prom::DepotGit d = depotDuProjet(doc);
            QMessageBox::information(this, QStringLiteral("Git"), q(prom::gitPousser(d, "origin", prom::gitBrancheCourante(d)).message));
          }},
          {QStringLiteral("Tirer"), [this, doc] {
            const prom::DepotGit d = depotDuProjet(doc);
            QMessageBox::information(this, QStringLiteral("Git"), q(prom::gitTirer(d, "origin", prom::gitBrancheCourante(d)).message));
            refaire();
          }},
          {QStringLiteral("Contribuer l’article…"), [this, &a, conception] {
            const ConceptionElec* c = conception();
            if (!c || !a.selection() || a.selection()->composants().empty()) {
              QMessageBox::information(this, QStringLiteral("Contribution"), QStringLiteral("Choisis d’abord un composant posé d’après un article."));
              return;
            }
            const ComposantElec* k = c->composant(*a.selection()->composants().begin());
            if (!k || !c->articles.count(k->article)) {
              QMessageBox::information(this, QStringLiteral("Contribution"), QStringLiteral("Ce composant n’a pas d’article."));
              return;
            }
            const QString biblio = QFileDialog::getExistingDirectory(this, QStringLiteral("Dépôt Git de la bibliothèque"));
            if (biblio.isEmpty()) return;
            prom::DepotGit d;
            d.dossier = biblio.toStdString();
            d.auteur = Document::personne().toStdString();
            const Article& art = c->articles.at(k->article);
            const prom::ContributionArticle r = prom::contribuerArticle(d, art, c->symbole(art.symbole.empty() ? k->symbole : art.symbole), c->empreinte(art.empreinte.empty() ? k->empreinte : art.empreinte), art.modele3D);
            QMessageBox::information(this, QStringLiteral("Contribution"), r.ok ? QStringLiteral("Branche « %1 » : %2 fichier(s).").arg(q(r.branche)).arg(r.fichiers.size()) : q(r.erreur));
          }}});

  // ---------- 12.18 · Nomenclature du système ----------
  onglet(QStringLiteral("nomenclatureSysteme"), QStringLiteral("Nomenclature du système"), [this, doc]() {
    const prom::NomenclatureSysteme n = prom::nomenclatureSysteme(doc->projet(), {}, m_donneesAchat);
    if (n.lignes.empty()) return QStringLiteral("<p>Aucune carte ni liaison dans ce projet.</p>");
    QStringList entetes{QStringLiteral("Désignation"), QStringLiteral("Référence")};
    for (const auto& c : n.colonnes) entetes << h(c);
    entetes << QStringLiteral("Par système") << QStringLiteral("Achats");
    std::vector<QStringList> l;
    for (const auto& x : n.lignes) {
      QStringList r{h(x.designation) + (x.prefere ? QStringLiteral(" ★") : QString()), h(x.mpn)};
      for (const auto& c : n.colonnes) r << (x.parColonne.count(c) ? QLocale().toString(x.parColonne.at(c), 'g', 6) : QString());
      r << QLocale().toString(x.total, 'g', 6) + QStringLiteral(" ") + h(x.unite);
      QString achats;
      for (const auto& d : x.achats)
        achats += h(d.distributeur) + QStringLiteral(" (") + (d.stock >= 0 ? QString::number(d.stock) : QStringLiteral("?")) + QStringLiteral(", ") +
                  (d.delaiJours >= 0 ? QStringLiteral("%1 j").arg(d.delaiJours) : QStringLiteral("?")) + (d.agree ? QString() : QStringLiteral(", <span class='err'>non agréé</span>")) + QStringLiteral(") ");
      r << achats;
      l.push_back(r);
    }
    QString t = tableau034(entetes, l);
    if (const auto sp = n.sansPrix(); !sp.empty()) t += QStringLiteral("<p class='av'>%1 ligne(s) sans prix connu.</p>").arg(sp.size());
    return t;
  },
         {{QStringLiteral("Données d’achat (CSV)…"), [this] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Données d’achat"), {}, QStringLiteral("CSV (*.csv *.txt)"));
            if (f.isEmpty()) return;
            std::string erreur;
            m_donneesAchat = prom::lireDonneesAchat(prom::lireFichier(prom::cheminFichier(f.toStdString())), &erreur);
            refaire();
          }},
          {QStringLiteral("Exporter (CSV)…"), [this, doc] {
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Nomenclature du système"), QStringLiteral("nomenclature-systeme.csv"), QStringLiteral("CSV (*.csv)"));
            if (!f.isEmpty()) ecrire(this, f, prom::nomenclatureSysteme(doc->projet(), {}, m_donneesAchat).csv());
          }}});

  // ---------- 12.6, 12.20 · Liens entre disciplines ----------
  onglet(QStringLiteral("liens"), QStringLiteral("Liens entre disciplines"), [doc]() {
    prom::Projet p = doc->projet();  // relevé sur une copie : le projet ne change qu'à l'accusé de lecture
    prom::verifierLiens(p);
    if (p.liensDisciplines.empty()) return QStringLiteral("<p>Aucun lien : « Lier la carte au boîtier » garde l’empreinte du contour vérifié par la mécanique.</p>");
    std::vector<QStringList> l;
    for (const auto& x : p.liensDisciplines) {
      QString ch;
      for (const auto& c : x.changements) ch += h(c) + QStringLiteral("<br>");
      l.push_back({h(x.de), h(x.vers), h(x.quoi), x.etat == "à jour" ? QStringLiteral("<span class='ok'>à jour</span>") : QStringLiteral("<span class='err'>à revoir</span>"), ch, h(x.accusePar + " " + x.accuseLe)});
    }
    return tableau034({QStringLiteral("De"), QStringLiteral("Vers"), QStringLiteral("Vérifié"), QStringLiteral("État"), QStringLiteral("Changements"), QStringLiteral("Dernier accusé")}, l);
  },
         {{QStringLiteral("Lier la carte au boîtier"), [this, doc, conception] {
            const ConceptionElec* c = conception();
            if (!c) return;
            const std::string uuid = c->uuid;
            doc->modifier([&](prom::Projet& p) { p.liensDisciplines.push_back(prom::lierDisciplines(p, "carte:" + uuid, "boitier", "contour", Document::personne().toStdString(), aujourdhui())); },
                          QStringLiteral("Carte liée au boîtier"));
            refaire();
          }},
          {QStringLiteral("Accuser la lecture"), [this, doc] {
            prom::Projet copie = doc->projet();
            prom::verifierLiens(copie);
            std::vector<std::string> aRevoir;
            for (const auto& x : copie.liensDisciplines)
              if (x.etat != "à jour") aRevoir.push_back(x.id);
            if (aRevoir.empty()) return;
            doc->modifier([&](prom::Projet& p) {
              for (const auto& id : aRevoir) prom::accuserLecture(p, id, Document::personne().toStdString(), aujourdhui());
            }, QStringLiteral("Changements lus (%1 lien(s))").arg(aRevoir.size()));
            refaire();
          }},
          {QStringLiteral("Carte dans un folio électrique…"), [this, doc, conception] {
            const ConceptionElec* c = conception();
            if (!c || doc->projet().electrique.empty()) {
              QMessageBox::information(this, QStringLiteral("Folio"), QStringLiteral("Le projet n’a pas de projet électrique."));
              return;
            }
            bool ok = false;
            const QString j = QInputDialog::getText(this, QStringLiteral("Carte dans un folio"), QStringLiteral("Connecteur de la carte (bornier)"), QLineEdit::Normal, QStringLiteral("J1"), &ok);
            if (!ok) return;
            const prom::elec::ProjetElectrique& pe0 = doc->projet().electrique.front();
            QStringList folios;
            for (std::size_t i = 0; i < pe0.schema.feuilles.size(); ++i) folios << QStringLiteral("%1 — %2").arg(i + 1).arg(q(pe0.schema.feuilles[i].nom));
            const QString f = QInputDialog::getItem(this, QStringLiteral("Carte dans un folio"), QStringLiteral("Folio"), folios, 0, false, &ok);
            if (!ok) return;
            const std::size_t folio = static_cast<std::size_t>(folios.indexOf(f));
            const ConceptionElec carte = *c;
            std::string erreur;
            doc->modifier([&](prom::Projet& p) { erreur = prom::poserCarteDansFolio(p.electrique.front(), carte, j.toStdString(), folio, ptMm(100, 80)).erreur; }, QStringLiteral("Carte posée dans le folio"));
            if (!erreur.empty()) QMessageBox::warning(this, QStringLiteral("Folio"), q(erreur));
          }},
          {QStringLiteral("Faisceaux des liaisons"), [this, doc] {
            if (doc->projet().electrique.empty() || doc->projet().liaisons.empty()) return;
            int n = 0;
            doc->modifier([&](prom::Projet& p) {
              for (const auto& l : p.liaisons)
                if (const prom::FaisceauEntreCartes f = prom::faisceauDeLiaison(p, l); f.erreur.empty()) {
                  p.electrique.front().faisceaux.push_back(f.faisceau);
                  ++n;
                }
            }, QStringLiteral("Faisceaux tirés des liaisons du système"));
            QMessageBox::information(this, QStringLiteral("Faisceaux"), QStringLiteral("%1 faisceau(x) posé(s) sur la planche de l’atelier électrique.").arg(n));
          }}});
}

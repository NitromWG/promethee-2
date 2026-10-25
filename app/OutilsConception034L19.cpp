// Prométhée : onglets du lot L19 de la 0.34 dans la fenêtre des outils de conception. Achats (19.7) : tableau de bord
// de la nomenclature recalculé à chaque commande sur les seules lignes touchées, liste d'achat pour N cartes de
// toutes les cartes du projet, panier par distributeur, page autonome pour les acheteurs. PLM (19.14) : dépôt « Vers
// le PLM » dans le dossier d'équipe, retour « Depuis le PLM », réglages et lancement du programme séparé d'accès en
// ligne (décision D13), qui seul ouvre le réseau. Licence GPL-3.0-only.
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBrowser>

#include "AtelierElectronique.hpp"
#include "ChienDeGarde.hpp"
#include "Document.hpp"
#include "FenetreOutilsConception.hpp"
#include "promethee/carte3d.hpp"
#include "promethee/equipe.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/plm.hpp"

using namespace prom::ecad;

namespace {
QString h(const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); }
QString q(const std::string& s) { return QString::fromStdString(s); }
const char* STYLE19 = "<style>table{border-collapse:collapse}td,th{padding:2px 8px;border-bottom:1px solid #ccc;text-align:left}.err{color:#c53030}.av{color:#b7791f}.ok{color:#2f855a}</style>";
QString euros(double v) { return QLocale(QLocale::French).toString(v, 'f', 2) + QStringLiteral(" €"); }
bool ecrire(QWidget* w, const QString& chemin, const std::string& contenu) {
  try {
    prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), contenu);
    return true;
  } catch (const std::exception& e) {
    if (w) QMessageBox::warning(w, QStringLiteral("Écriture"), QString::fromUtf8(e.what()));
    return false;
  }
}
// Réglages de l'accès en ligne : même fichier que le programme séparé (données portables, sinon configuration)
QString fichierAccesEnLigne() {
  if (const QByteArray force = qgetenv("PROMETHEE_ACCES_EN_LIGNE"); !force.isEmpty()) return QString::fromUtf8(force);
  if (portable::actif()) return QDir(portable::dossier()).filePath(QStringLiteral("acces-en-ligne.json"));
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(QStringLiteral("acces-en-ligne.json"));
}
QJsonObject lireAcces() {
  QFile f(fichierAccesEnLigne());
  return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}
QString programmeEnLigne() {
  const QString base = QStringLiteral("promethee-en-ligne");
  const QDir ici(QCoreApplication::applicationDirPath());
  for (const QString& c : {ici.filePath(base), ici.filePath(base + QStringLiteral(".exe")), ici.filePath(QStringLiteral("../../../bin/") + base)})
    if (QFileInfo(c).isExecutable()) return QFileInfo(c).absoluteFilePath();
  return QStandardPaths::findExecutable(base);
}
QString dossierEquipe(QWidget* w) {
  QSettings r;
  QString d = r.value(QStringLiteral("equipe/dossier")).toString();
  if (d.isEmpty() && w) {
    d = QFileDialog::getExistingDirectory(w, QStringLiteral("Dossier d’équipe"));
    if (!d.isEmpty()) r.setValue(QStringLiteral("equipe/dossier"), d);
  }
  return d;
}
}  // namespace

void FenetreOutilsConception::majTableauBord() {
  if (const ConceptionElec* c = m_atelier.conceptionCourante()) m_tableauBord = tableauBordNomenclature(*c, &m_tableauBord);
}

const ListeAchat& FenetreOutilsConception::calculerAchats(int cartes, const QString& fichierFrais) {
  m_cartesAchat = cartes;
  std::vector<std::pair<const ConceptionElec*, int>> projets;
  for (const auto& c : m_atelier.document()->projet().electronique) projets.push_back({&c, cartes});
  FraisAchat frais;
  if (!fichierFrais.isEmpty()) {
    std::string erreur;
    try {
      if (const auto f = lireFraisAchat(prom::lireFichier(prom::cheminFichier(fichierFrais.toStdString())), &erreur)) frais = *f;
    } catch (const std::exception&) {
    }
  }
  OptionsAchat o;
  o.cartes = 1;  // les lignes portent déjà le besoin de toutes les cartes
  o.donnees = frais;
  m_listeAchat = calculerListeAchat(lignesAchat(projets, 0.02, &frais), o);
  if (m_textes.count(QStringLiteral("achats"))) m_textes[QStringLiteral("achats")]->setHtml(QString::fromLatin1(STYLE19) + m_rapports[QStringLiteral("achats")]());
  return m_listeAchat;
}

bool FenetreOutilsConception::exporterPanier(const QString& distributeur, const QString& chemin) {
  for (const auto& g : gabaritsPanier())
    if (QString::fromStdString(g.distributeur).compare(distributeur, Qt::CaseInsensitive) == 0) return ecrire(this, chemin, panierDistributeur(m_listeAchat, g));
  // Gabarit modifié par l'utilisateur
  try {
    if (const auto g = lireGabaritPanier(prom::lireFichier(prom::cheminFichier(distributeur.toStdString())))) return ecrire(this, chemin, panierDistributeur(m_listeAchat, *g));
  } catch (const std::exception&) {
  }
  return false;
}

bool FenetreOutilsConception::exporterPageAcheteurs(const QString& chemin) {
  majTableauBord();
  const ConceptionElec* c = m_atelier.conceptionCourante();
  return c && ecrire(this, chemin, htmlTableauBord(m_tableauBord, c->cartouche.titre.empty() ? c->nom : c->cartouche.titre));
}

QString FenetreOutilsConception::deposerPlm(const QString& equipe, const QString& revision, bool modeles) {
  Document* doc = m_atelier.document();
  prom::DepotPlm d = prom::preparerDepotPlm(doc->projet(), revision.toStdString(), QDate::currentDate().toString(Qt::ISODate).toStdString(), Document::personne().toStdString());
  // Modèles des cartes en STEP AP242
  if (modeles) {
    QTemporaryDir tmp;
    for (std::size_t i = 0; i < doc->projet().electronique.size() && tmp.isValid(); ++i) {
      const QString f = tmp.filePath(QStringLiteral("carte%1.step").arg(i + 1));
      try {
        prom::OptionsStep o;
        o.schema = prom::SchemaStep::AP242;
        o.auteur = Document::personne().toStdString();
        prom::exporterStepCarte(doc->projet().electronique[i], f.toStdString(), o);
        d.fichiers.push_back({"modeles/carte" + std::to_string(i + 1) + ".step", prom::lireFichier(prom::cheminFichier(f.toStdString()))});
      } catch (const std::exception&) {
      }
    }
  }
  std::string erreur;
  const std::string dossier = prom::deposerPourPlm(equipe.toStdString(), d, &erreur);
  if (dossier.empty()) {
    m_atelier.afficherMessage(QStringLiteral("Dépôt vers le PLM impossible : ") + h(erreur), NiveauMessage::Erreur);
    return {};
  }
  m_dernierDepot = q(dossier);
  m_dossierEquipePlm = equipe;
  m_atelier.afficherMessage(QStringLiteral("Déposé sous « Vers le PLM » : %1 (révision %2, %3 article(s))").arg(h(d.numero), h(d.revision)).arg(d.articles.size()), NiveauMessage::Succes);
  if (m_textes.count(QStringLiteral("plm"))) m_textes[QStringLiteral("plm")]->setHtml(QString::fromLatin1(STYLE19) + m_rapports[QStringLiteral("plm")]());
  return m_dernierDepot;
}

int FenetreOutilsConception::lancerEnLigne(const QStringList& commande, QString* sortie) {
  const QString programme = programmeEnLigne();
  if (programme.isEmpty()) {
    m_sortiePlm = QStringLiteral("Le programme d’accès en ligne (promethee-en-ligne) n’est pas installé à côté de Prométhée : les fichiers restent dans le dossier d’équipe.");
    if (sortie) *sortie = m_sortiePlm;
    return -1;
  }
  QStringList args = {QStringLiteral("--reglages"), fichierAccesEnLigne()};
  if (!m_dossierEquipePlm.isEmpty()) args << QStringLiteral("--equipe") << m_dossierEquipePlm;
  args << QStringLiteral("--utilisateur") << Document::personne() << commande;
  QProcess p;
  p.start(programme, args);
  const bool fini = p.waitForFinished(90000);
  m_sortiePlm = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError()).trimmed();
  if (sortie) *sortie = m_sortiePlm;
  const int code = fini ? p.exitCode() : -1;
  m_atelier.afficherMessage(h(m_sortiePlm.toStdString()), code == 0 ? NiveauMessage::Succes : code == 3 ? NiveauMessage::Information : NiveauMessage::Avertissement);
  if (m_textes.count(QStringLiteral("plm"))) m_textes[QStringLiteral("plm")]->setHtml(QString::fromLatin1(STYLE19) + m_rapports[QStringLiteral("plm")]());
  return code;
}

int FenetreOutilsConception::synchroniserPlm(const QString& operation, QString* sortie) {
  QStringList commande = {QStringLiteral("plm"), operation};
  if (operation == QStringLiteral("deposer") || operation == QStringLiteral("restituer")) commande << m_dernierDepot;
  else commande << q(prom::lireDepotPlm(m_dernierDepot.toStdString()).numero);
  return lancerEnLigne(commande, sortie);
}

int FenetreOutilsConception::synchroniserTickets(QString* sortie) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (m_dossierEquipePlm.isEmpty()) m_dossierEquipePlm = dossierEquipe(this);
  if (!c || m_dossierEquipePlm.isEmpty()) return -1;
  const std::string equipe = m_dossierEquipePlm.toStdString();
  prom::ecrireTickets(equipe, false, prom::ticketsDepuisRevues(*c, prom::lireTickets(equipe, false)));
  if (const int code = lancerEnLigne({QStringLiteral("tickets"), QStringLiteral("synchroniser")}, sortie); code != 0) return code == 3 ? 0 : -1;
  const auto depuis = prom::lireTickets(equipe, true);
  ConceptionElec essai = *c;
  const std::string date = QDate::currentDate().toString(Qt::ISODate).toStdString(), par = Document::personne().toStdString();
  int clos = prom::appliquerTickets(essai, depuis, par, date);
  if (clos > 0)
    m_atelier.document()->modifierConception(m_atelier.conception(), [&](ConceptionElec& x) { clos = prom::appliquerTickets(x, depuis, par, date); }, QStringLiteral("Constats clos par le gestionnaire de tickets"));
  m_atelier.afficherMessage(QStringLiteral("Tickets : %1 constat(s) clos d’après le gestionnaire").arg(clos), NiveauMessage::Information);
  return clos;
}

void FenetreOutilsConception::ajouterOnglets034L19() {
  AtelierElectronique& a = m_atelier;
  Document* doc = a.document();
  majTableauBord();
  // Chaque commande : seules les lignes touchées de la nomenclature se recalculent
  const auto suivre = [this] {
    majTableauBord();
    if (isVisible() && m_textes.count(QStringLiteral("achats"))) m_textes[QStringLiteral("achats")]->setHtml(QString::fromLatin1(STYLE19) + m_rapports[QStringLiteral("achats")]());
  };
  connect(doc, &Document::change, this, suivre);
  connect(doc, &Document::conceptionChangee, this, [suivre](const prom::ecad::ChangementConception&) { suivre(); });

  // ---------- 19.7 · Achats ----------
  onglet(QStringLiteral("achats"), QStringLiteral("Achats"), [this]() {
    const TableauBordNomenclature& t = m_tableauBord;
    QString r = QStringLiteral("<h3>Nomenclature : %1 / 10</h3><p>%2 ligne(s), %3 complète(s), coût connu %4 ; %5 ligne(s) recalculée(s) à la dernière commande.</p>")
                    .arg(QLocale(QLocale::French).toString(t.note, 'f', 1))
                    .arg(t.lignes.size())
                    .arg(t.lignesCompletes)
                    .arg(euros(t.coutTotal))
                    .arg(t.recalculees);
    r += QStringLiteral("<table><tr><th>Ligne</th><th>Qté</th><th>Complétude</th><th>Coût</th><th>Délai</th><th>Risque</th><th>Remarques</th></tr>");
    for (const auto& l : t.lignes) {
      QStringList rq;
      for (const auto& x : l.remarques) rq << h(x);
      r += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td><td>%7</td></tr>")
               .arg(h(l.cle))
               .arg(l.quantite)
               .arg(l.completude)
               .arg(l.cout)
               .arg(l.delai)
               .arg(l.risque)
               .arg(rq.join(QStringLiteral(" ; ")));
    }
    r += QStringLiteral("</table>");
    if (m_cartesAchat > 0) {
      const ListeAchat& la = m_listeAchat;
      r += QStringLiteral("<h3>Liste d’achat pour %1 carte(s) de chaque conception</h3><p>Méthode : %2 ; fournisseurs : %3.</p>")
               .arg(m_cartesAchat)
               .arg(h(la.methode))
               .arg(h([&] {
                 std::string s;
                 for (const auto& f : la.fournisseurs) s += (s.empty() ? "" : ", ") + f;
                 return s;
               }()));
      r += QStringLiteral("<table><tr><th>Article</th><th>Fournisseur</th><th>Conditionnement</th><th>Besoin</th><th>Achat</th><th>Prix unitaire</th><th>Montant</th></tr>");
      for (const auto& c : la.lignes)
        r += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td><td>%7</td></tr>")
                 .arg(h(c.cle), h(c.fournisseur), h(c.conditionnement))
                 .arg(c.besoin)
                 .arg(c.quantite)
                 .arg(QLocale(QLocale::French).toString(c.prixUnitaire, 'g', 4) + QStringLiteral(" €"))
                 .arg(euros(c.cout));
      r += QStringLiteral("</table><p>Sous-total %1, port %2, douane %3 : <b>total %4</b>.</p>").arg(euros(la.sousTotal), euros(la.port), euros(la.douane), euros(la.total));
      for (const auto& x : la.alertes) r += QStringLiteral("<p class='av'>⚠ ") + h(x) + QStringLiteral("</p>");
    } else
      r += QStringLiteral("<p>« Liste d’achat… » : pour N cartes de chaque conception du projet, conditionnements (bobine, bande coupée), minimum et multiple de commande, pertes, port et douane selon le pays du fournisseur.</p>");
    return r;
  },
         {{QStringLiteral("Liste d’achat…"), [this] {
            bool ok = false;
            const int n = QInputDialog::getInt(this, QStringLiteral("Liste d’achat"), QStringLiteral("Nombre de cartes de chaque conception :"), m_cartesAchat > 0 ? m_cartesAchat : 100, 1, 1000000, 1, &ok);
            if (!ok) return;
            const QString frais = QFileDialog::getOpenFileName(this, QStringLiteral("Frais de port et de douane (facultatif)"), {}, QStringLiteral("Données d’achat (*.json)"));
            calculerAchats(n, frais);
          }},
          {QStringLiteral("Panier…"), [this] {
            QStringList d;
            for (const auto& g : gabaritsPanier()) d << QString::fromStdString(g.distributeur);
            bool ok = false;
            const QString choix = QInputDialog::getItem(this, QStringLiteral("Panier"), QStringLiteral("Distributeur :"), d, 0, false, &ok);
            if (!ok) return;
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Panier ") + choix, QStringLiteral("panier-") + choix.toLower() + QStringLiteral(".csv"), QStringLiteral("CSV (*.csv)"));
            if (!f.isEmpty()) exporterPanier(choix, f);
          }},
          {QStringLiteral("Liste en CSV…"), [this] {
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Liste d’achat"), QStringLiteral("liste-achat.csv"), QStringLiteral("CSV (*.csv)"));
            if (!f.isEmpty()) ecrire(this, f, csvListeAchat(m_listeAchat));
          }},
          {QStringLiteral("Page pour les acheteurs…"), [this] {
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Tableau de bord de la nomenclature"), QStringLiteral("nomenclature.html"), QStringLiteral("Page HTML (*.html)"));
            if (!f.isEmpty()) exporterPageAcheteurs(f);
          }}});

  // ---------- 19.14 · PLM ----------
  onglet(QStringLiteral("plm"), QStringLiteral("PLM"), [this]() {
    QString r = QStringLiteral("<p>La publication vers le PLM dépose dans le dossier d’équipe, sous « Vers le PLM », la nomenclature commune (cartes, mécanique, faisceaux), la structure du "
                               "produit en PLMXML et les modèles en STEP AP242. Le programme séparé d’accès en ligne, seul à ouvrir le réseau et seulement si tu l’actives, l’envoie au PLM "
                               "et rapporte les états sous « Depuis le PLM ».</p>");
    const QJsonObject acces = lireAcces();
    const QJsonObject plm = acces.value(QStringLiteral("fonctions")).toObject().value(QStringLiteral("plm")).toObject();
    const bool actif = acces.value(QStringLiteral("actif")).toBool(false) && plm.value(QStringLiteral("actif")).toBool(false);
    r += QStringLiteral("<p>Accès en ligne : <b class='%1'>%2</b>%3</p>")
             .arg(actif ? QStringLiteral("ok") : QStringLiteral("av"), actif ? QStringLiteral("activé pour le PLM") : QStringLiteral("désactivé (rien ne part)"),
                  actif ? QStringLiteral(" — ") + plm.value(QStringLiteral("adaptateur")).toString().toHtmlEscaped() + QStringLiteral(", ") + plm.value(QStringLiteral("adresse")).toString().toHtmlEscaped()
                        : QString());
    if (!m_dernierDepot.isEmpty()) {
      const prom::LectureDepotPlm l = prom::lireDepotPlm(m_dernierDepot.toStdString());
      r += QStringLiteral("<h3>Dernier dépôt</h3><p>%1 — %2 révision %3 : %4, %5 article(s), %6 fichier(s).</p>")
               .arg(m_dernierDepot.toHtmlEscaped(), h(l.numero), h(l.revision), l.ok ? QStringLiteral("<span class='ok'>complet</span>") : QStringLiteral("<span class='err'>incomplet ou altéré</span>"))
               .arg(l.articles)
               .arg(l.fichiers.size());
    }
    const QString equipe = m_dossierEquipePlm.isEmpty() ? QSettings().value(QStringLiteral("equipe/dossier")).toString() : m_dossierEquipePlm;
    const auto etats = equipe.isEmpty() ? std::vector<prom::EtatPlm>{} : prom::lireEtatsPlm(equipe.toStdString());
    if (!etats.empty()) {
      r += QStringLiteral("<h3>Depuis le PLM</h3><table><tr><th>Numéro</th><th>Révision</th><th>État</th><th>Réservé par</th><th>Date</th><th>Message</th></tr>");
      for (const auto& e : etats)
        r += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td></tr>").arg(h(e.numero), h(e.revision), h(e.etat), h(e.reservePar), h(e.date), h(e.message));
      r += QStringLiteral("</table>");
    }
    const auto tickets = equipe.isEmpty() ? std::vector<prom::TicketPlm>{} : prom::lireTickets(equipe.toStdString(), true);
    if (!tickets.empty()) {
      r += QStringLiteral("<h3>Tickets</h3><table><tr><th>Constat</th><th>Ticket</th><th>Titre</th><th>État</th></tr>");
      for (const auto& t : tickets) r += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>").arg(h(t.id), h(t.distant), h(t.titre), h(t.etat));
      r += QStringLiteral("</table>");
    }
    if (!m_sortiePlm.isEmpty()) r += QStringLiteral("<p><i>%1</i></p>").arg(m_sortiePlm.toHtmlEscaped());
    return r;
  },
         {{QStringLiteral("Déposer vers le PLM…"), [this] {
            const QString equipe = dossierEquipe(this);
            if (equipe.isEmpty()) return;
            const ConceptionElec* c = m_atelier.conceptionCourante();
            bool ok = false;
            const QString revision =
                QInputDialog::getText(this, QStringLiteral("Vers le PLM"), QStringLiteral("Révision déposée :"), QLineEdit::Normal, c && !c->cartouche.revision.empty() ? q(c->cartouche.revision) : QStringLiteral("A"), &ok);
            if (ok) deposerPlm(equipe, revision.trimmed().isEmpty() ? QStringLiteral("A") : revision.trimmed());
          }},
          {QStringLiteral("Accès en ligne…"), [this] {
            QDialog d(this);
            d.setWindowTitle(QStringLiteral("Accès en ligne (décision D13)"));
            auto* f = new QFormLayout(&d);
            QJsonObject o = lireAcces();
            QJsonObject fonctions = o.value(QStringLiteral("fonctions")).toObject();
            QJsonObject plm = fonctions.value(QStringLiteral("plm")).toObject();
            auto* general = new QCheckBox(QStringLiteral("Autoriser l’accès en ligne"));
            general->setChecked(o.value(QStringLiteral("actif")).toBool(false));
            auto* fplm = new QCheckBox(QStringLiteral("Pour le PLM"));
            fplm->setChecked(plm.value(QStringLiteral("actif")).toBool(false));
            auto* adaptateur = new QComboBox;
            adaptateur->addItems({QStringLiteral("simule"), QStringLiteral("windchill"), QStringLiteral("teamcenter"), QStringLiteral("3dexperience"), QStringLiteral("agile")});
            adaptateur->setCurrentText(plm.value(QStringLiteral("adaptateur")).toString(QStringLiteral("simule")));
            auto* adresse = new QLineEdit(plm.value(QStringLiteral("adresse")).toString());
            auto* utilisateur = new QLineEdit(plm.value(QStringLiteral("utilisateur")).toString());
            auto* jeton = new QLineEdit(plm.value(QStringLiteral("jeton")).toString());
            jeton->setEchoMode(QLineEdit::Password);
            f->addRow(new QLabel(QStringLiteral("Seul le programme séparé « promethee-en-ligne » ouvre le réseau, et seulement si ces cases sont cochées ; chaque connexion s’inscrit à son journal.")));
            f->addRow(general);
            f->addRow(fplm);
            f->addRow(QStringLiteral("Adaptateur"), adaptateur);
            f->addRow(QStringLiteral("Adresse"), adresse);
            f->addRow(QStringLiteral("Utilisateur"), utilisateur);
            f->addRow(QStringLiteral("Jeton ou mot de passe"), jeton);
            auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
            connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
            f->addRow(b);
            if (d.exec() != QDialog::Accepted) return;
            o[QStringLiteral("format")] = QStringLiteral("promethee-acces-en-ligne");
            o[QStringLiteral("version")] = 1;
            o[QStringLiteral("actif")] = general->isChecked();
            plm[QStringLiteral("actif")] = fplm->isChecked();
            plm[QStringLiteral("adaptateur")] = adaptateur->currentText();
            plm[QStringLiteral("adresse")] = adresse->text().trimmed();
            plm[QStringLiteral("utilisateur")] = utilisateur->text().trimmed();
            plm[QStringLiteral("jeton")] = jeton->text();
            fonctions[QStringLiteral("plm")] = plm;
            o[QStringLiteral("fonctions")] = fonctions;
            QDir().mkpath(QFileInfo(fichierAccesEnLigne()).absolutePath());
            ecrire(this, fichierAccesEnLigne(), QJsonDocument(o).toJson(QJsonDocument::Indented).toStdString());
            refaire();
          }},
          {QStringLiteral("Envoyer le dépôt"), [this] { synchroniserPlm(QStringLiteral("deposer")); }},
          {QStringLiteral("Réserver"), [this] { synchroniserPlm(QStringLiteral("reserver")); }},
          {QStringLiteral("Restituer…"), [this] {
            // La révision suivante se dépose, puis se restitue
            const prom::LectureDepotPlm l = prom::lireDepotPlm(m_dernierDepot.toStdString());
            if (!l.ok || m_dossierEquipePlm.isEmpty()) return;
            const QString suivante = q(prom::revisionSuivante(l.revision, prom::SchemaRevision::Lettres));
            if (!deposerPlm(m_dossierEquipePlm, suivante).isEmpty()) synchroniserPlm(QStringLiteral("restituer"));
          }},
          {QStringLiteral("Consulter"), [this] { synchroniserPlm(QStringLiteral("consulter")); }},
          {QStringLiteral("Tickets des revues"), [this] { synchroniserTickets(); }}});
}

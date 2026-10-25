// Prométhée : programme séparé d'accès en ligne (décision D13 ; 4.22, 19.14). Ligne de commande :
//
//   promethee-en-ligne [--reglages fichier] [--equipe dossier] [--utilisateur nom] commande
//     reglages [--initialiser]           montre les réglages (et crée le fichier, tout désactivé)
//     plm deposer <dossier du dépôt>     dépôt « Vers le PLM/<numéro>-<révision> » envoyé au PLM
//     plm reserver <numéro>              réservation (check-out)
//     plm restituer <dossier du dépôt>   restitution en révision suivante (check-in)
//     plm consulter <numéro>
//     distributeurs offres <mpn>…        stock, prix et fin de vie chez les distributeurs (gabarit)
//     erp lire                           base de l'ERP lue par Qt SQL
//     tickets synchroniser               tickets des revues créés chez le gestionnaire, leurs états relus
//     journal                            connexions faites, une par ligne
//
// Rien ne part si l'accès en ligne n'est pas activé, en général et pour la fonction : le programme le dit et rend 3.
// Après chaque opération, « Depuis le PLM/etats.json » du dossier d'équipe reçoit l'état et la révision.
// Licence GPL-3.0-only.
#include <cstdio>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#ifdef PROMETHEE_EN_LIGNE_SQL
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#endif

#include "AccesEnLigne.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/hachage.hpp"
#include "promethee/plm.hpp"

using namespace prom::enligne;

namespace {
void dire(const QString& s) { std::fprintf(stdout, "%s\n", s.toUtf8().constData()); }
void erreur(const QString& s) { std::fprintf(stderr, "%s\n", s.toUtf8().constData()); }
QByteArray lire(const QString& chemin) {
  QFile f(chemin);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
// Manifeste et fichiers du dépôt, en JSON pour le corps des requêtes (contenus en base64)
std::map<QString, QString> valeursDepot(const QString& dossier, QString* probleme) {
  std::map<QString, QString> v;
  const prom::LectureDepotPlm l = prom::lireDepotPlm(dossier.toStdString());
  if (!l.ok) {
    *probleme = QStringLiteral("dépôt incomplet ou altéré : ") + QString::fromStdString(l.erreur);
    return v;
  }
  const QByteArray manifeste = lire(QDir(dossier).filePath(QStringLiteral("manifeste.json")));
  const QJsonObject m = QJsonDocument::fromJson(manifeste).object();
  v[QStringLiteral("numero")] = QString::fromStdString(l.numero);
  v[QStringLiteral("revision")] = QString::fromStdString(l.revision);
  v[QStringLiteral("designation")] = QString::fromStdString(l.produit);
  v[QStringLiteral("manifeste")] = QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact));
  QJsonArray fichiers;
  for (const auto& nom : l.fichiers) {
    const QByteArray contenu = lire(QDir(dossier).filePath(QString::fromStdString(nom)));
    fichiers.append(QJsonObject{{QStringLiteral("nom"), QString::fromStdString(nom)},
                                {QStringLiteral("sha256"), QString::fromStdString(prom::sha256Hex(std::string_view(contenu.constData(), static_cast<std::size_t>(contenu.size()))))},
                                {QStringLiteral("contenu"), QString::fromLatin1(contenu.toBase64())}});
  }
  v[QStringLiteral("fichiers")] = QString::fromUtf8(QJsonDocument(fichiers).toJson(QJsonDocument::Compact));
  return v;
}
// Dossier d'équipe d'un dépôt : <équipe>/Vers le PLM/<numéro>-<révision>
QString equipeDuDepot(const QString& dossier) {
  QDir d(dossier);
  d.cdUp();
  if (d.dirName() != QString::fromUtf8(prom::kDossierVersPlm)) return {};
  d.cdUp();
  return d.absolutePath();
}
void noterEtat(const QString& equipe, const QString& numero, const ReponsePlm& r, const QString& etatParDefaut, const QString& message) {
  if (equipe.isEmpty()) return;
  std::vector<prom::EtatPlm> etats = prom::lireEtatsPlm(equipe.toStdString());
  prom::EtatPlm* e = nullptr;
  for (auto& x : etats)
    if (x.numero == numero.toStdString()) e = &x;
  if (!e) {
    etats.push_back({numero.toStdString(), {}, {}, {}, {}, {}});
    e = &etats.back();
  }
  if (!r.revision.isEmpty()) e->revision = r.revision.toStdString();
  e->etat = (r.etat.isEmpty() ? etatParDefaut : r.etat).toStdString();
  e->reservePar = r.reservePar.toStdString();
  e->date = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
  e->message = message.toStdString();
  prom::ecrireEtatsPlm(equipe.toStdString(), etats);
}
// Données d'achat (format d'import de 12.18 : mpn, distributeur, stock, délai, agréé, risque, prix) sous « Depuis le PLM »
bool ecrireDonneesAchat(const QString& equipe, const QStringList& lignes) {
  if (equipe.isEmpty()) return false;
  QDir().mkpath(QDir(equipe).filePath(QString::fromUtf8(prom::kDossierDepuisPlm)));
  QFile f(QDir(QDir(equipe).filePath(QString::fromUtf8(prom::kDossierDepuisPlm))).filePath(QStringLiteral("donnees-achat.csv")));
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
  f.write("mpn;distributeur;stock;delai;agree;risque;prix\n");
  for (const QString& l : lignes) f.write((l + QLatin1Char('\n')).toUtf8());
  return true;
}
QString cellule(const QJsonValue& v) {
  if (v.isDouble()) return QString::number(v.toDouble(), 'g', 10);
  if (v.isBool()) return v.toBool() ? QStringLiteral("oui") : QStringLiteral("non");
  return v.toString().replace(QLatin1Char(';'), QLatin1Char(','));
}
// Stock, prix et fin de vie chez les distributeurs, par le gabarit de la fonction « distributeurs »
int offresDistributeurs(const Reglages& r, const QString& cheminReglages, const QString& equipe, const QStringList& mpns) {
  if (const QString non = refus(r, QStringLiteral("distributeurs")); !non.isEmpty()) {
    erreur(QStringLiteral("Rien n'est parti : ") + non);
    return 3;
  }
  QString probleme;
  const ReglageFonction& rf = r.fonctions.at(QStringLiteral("distributeurs"));
  const auto g = gabarit(rf.adaptateur.isEmpty() ? QStringLiteral("distributeur-simule") : rf.adaptateur, QFileInfo(cheminReglages).absolutePath(), &probleme);
  if (!g) {
    erreur(probleme);
    return 2;
  }
  QStringList lignes;
  for (const QString& mpn : mpns) {
    const ReponsePlm rep = executer(r, QStringLiteral("distributeurs"), *g, QStringLiteral("offres"), {{QStringLiteral("mpn"), mpn}});
    if (!rep.ok) {
      erreur(mpn + QStringLiteral(" : ") + rep.erreur);
      continue;
    }
    for (const QJsonValue& o : rep.corps.value(QStringLiteral("offres")).toArray()) {
      const QJsonObject x = o.toObject();
      lignes << QStringList{mpn, cellule(x.value(QStringLiteral("distributeur"))), cellule(x.value(QStringLiteral("stock"))), cellule(x.value(QStringLiteral("delai"))),
                            cellule(x.value(QStringLiteral("agree"))), cellule(x.value(QStringLiteral("risque"))), cellule(x.value(QStringLiteral("prix")))}
                    .join(QLatin1Char(';'));
    }
  }
  if (!ecrireDonneesAchat(equipe, lignes)) {
    erreur(QStringLiteral("dossier d'équipe manquant (--equipe)"));
    return 2;
  }
  dire(QStringLiteral("%1 offre(s) écrite(s) sous « Depuis le PLM »").arg(lignes.size()));
  return 0;
}
// Tickets dans les deux sens : ceux de « Vers le PLM/tickets.json » qui n'ont pas de numéro chez le gestionnaire y
// sont créés ; la liste du gestionnaire revient dans « Depuis le PLM/tickets.json », états « clos » reconnus
int synchroniserTickets(const Reglages& r, const QString& cheminReglages, const QString& equipe) {
  if (const QString non = refus(r, QStringLiteral("tickets")); !non.isEmpty()) {
    erreur(QStringLiteral("Rien n'est parti : ") + non);
    return 3;
  }
  if (equipe.isEmpty()) {
    erreur(QStringLiteral("dossier d'équipe manquant (--equipe)"));
    return 2;
  }
  QString probleme;
  const ReglageFonction& rf = r.fonctions.at(QStringLiteral("tickets"));
  const auto g = gabarit(rf.adaptateur.isEmpty() ? QStringLiteral("tickets-simule") : rf.adaptateur, QFileInfo(cheminReglages).absolutePath(), &probleme);
  if (!g) {
    erreur(probleme);
    return 2;
  }
  std::vector<prom::TicketPlm> locaux = prom::lireTickets(equipe.toStdString(), false);
  int crees = 0;
  const QString projet = QString::fromUtf8(qgetenv("PROMETHEE_TICKETS_PROJET"));
  for (auto& t : locaux) {
    if (!t.distant.empty()) continue;
    const ReponsePlm rep = executer(r, QStringLiteral("tickets"), *g, QStringLiteral("creer"),
                                    {{QStringLiteral("id"), QString::fromStdString(t.id)}, {QStringLiteral("titre"), QString::fromStdString(t.titre)}, {QStringLiteral("texte"), QString::fromStdString(t.texte)},
                                     {QStringLiteral("objet"), QString::fromStdString(t.objet)}, {QStringLiteral("auteur"), QString::fromStdString(t.auteur)}, {QStringLiteral("projet"), projet},
                                     {QStringLiteral("jeton"), rf.jeton}});
    if (!rep.ok) {
      erreur(QStringLiteral("ticket ") + QString::fromStdString(t.id) + QStringLiteral(" : ") + rep.erreur);
      continue;
    }
    const auto& op = g->operations.at(QStringLiteral("creer"));
    t.distant = valeurJson(rep.corps, op.champs.count(QStringLiteral("id")) ? op.champs.at(QStringLiteral("id")) : QStringLiteral("id")).toStdString();
    ++crees;
  }
  prom::ecrireTickets(equipe.toStdString(), false, locaux);
  const ReponsePlm rep = executer(r, QStringLiteral("tickets"), *g, QStringLiteral("lister"), {{QStringLiteral("projet"), projet}, {QStringLiteral("jeton"), rf.jeton}});
  if (!rep.ok) {
    erreur(QStringLiteral("tickets : ") + rep.erreur);
    return 4;
  }
  const auto& op = g->operations.at(QStringLiteral("lister"));
  auto champ = [&](const QJsonValue& x, const char* k) { return valeurJson(x, op.champs.count(QString::fromLatin1(k)) ? op.champs.at(QString::fromLatin1(k)) : QString::fromLatin1(k)); };
  const QJsonArray liste = op.liste.isEmpty() ? rep.tableau : rep.corps.value(op.liste).toArray();
  std::vector<prom::TicketPlm> depuis;
  for (const QJsonValue& x : liste) {
    prom::TicketPlm t;
    t.distant = champ(x, "id").toStdString();
    t.id = champ(x, "reference").toStdString();
    t.titre = champ(x, "titre").toStdString();
    const QString etat = champ(x, "etat");
    t.etat = op.clos.contains(etat) ? "clos" : etat.isEmpty() ? "ouvert" : etat.toStdString();
    depuis.push_back(t);
  }
  prom::ecrireTickets(equipe.toStdString(), true, depuis);
  dire(QStringLiteral("%1 ticket(s) créé(s), %2 relu(s) du gestionnaire").arg(crees).arg(depuis.size()));
  return 0;
}
// Base de l'ERP lue par Qt SQL (fonction « erp » : pilote, base, requête rendant mpn, distributeur, stock, délai,
// agréé, risque, prix)
int lireErp(const Reglages& r, const QString& equipe) {
  if (const QString non = refus(r, QStringLiteral("erp")); !non.isEmpty() && !non.contains(QStringLiteral("adresse"))) {
    erreur(QStringLiteral("Rien n'est parti : ") + non);
    return 3;
  }
#ifdef PROMETHEE_EN_LIGNE_SQL
  QFile f(r.chemin);
  if (!f.open(QIODevice::ReadOnly)) return 2;
  const QJsonObject erp = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("fonctions")).toObject().value(QStringLiteral("erp")).toObject();
  const QString pilote = erp.value(QStringLiteral("pilote")).toString(QStringLiteral("QSQLITE")), base = erp.value(QStringLiteral("base")).toString();
  const QString requete = erp.value(QStringLiteral("requete")).toString(QStringLiteral("SELECT mpn, distributeur, stock, delai, agree, risque, prix FROM achats"));
  QStringList lignes;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(pilote, QStringLiteral("erp"));
    db.setDatabaseName(base);
    db.setHostName(erp.value(QStringLiteral("hote")).toString());
    db.setUserName(erp.value(QStringLiteral("utilisateur")).toString());
    db.setPassword(erp.value(QStringLiteral("jeton")).toString());
    const bool ouverte = db.open();
    noter(r, QStringLiteral("erp"), QStringLiteral("SQL"), pilote + QLatin1Char(':') + base, ouverte ? 200 : 0, requete.size(), ouverte ? QStringLiteral("lecture") : db.lastError().text());
    if (!ouverte) {
      erreur(QStringLiteral("ERP : ") + db.lastError().text());
      return 4;
    }
    QSqlQuery q(db);
    if (!q.exec(requete)) {
      erreur(QStringLiteral("ERP : ") + q.lastError().text());
      return 4;
    }
    while (q.next()) {
      QStringList c;
      for (int i = 0; i < 7; ++i) c << q.value(i).toString().replace(QLatin1Char(';'), QLatin1Char(','));
      lignes << c.join(QLatin1Char(';'));
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(QStringLiteral("erp"));
  if (!ecrireDonneesAchat(equipe, lignes)) {
    erreur(QStringLiteral("dossier d'équipe manquant (--equipe)"));
    return 2;
  }
  dire(QStringLiteral("%1 ligne(s) de l'ERP écrite(s) sous « Depuis le PLM »").arg(lignes.size()));
  return 0;
#else
  (void)equipe;
  erreur(QStringLiteral("ce programme a été compilé sans Qt SQL"));
  return 2;
#endif
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("Prométhée"));
  QCoreApplication::setOrganizationName(QStringLiteral("Prométhée"));
  QStringList args = app.arguments();
  args.removeFirst();
  QString cheminReglages = cheminReglagesParDefaut(), equipe, utilisateur;
  for (int i = 0; i + 1 < args.size();) {
    if (args[i] == QStringLiteral("--reglages")) cheminReglages = args[i + 1];
    else if (args[i] == QStringLiteral("--equipe")) equipe = args[i + 1];
    else if (args[i] == QStringLiteral("--utilisateur")) utilisateur = args[i + 1];
    else {
      ++i;
      continue;
    }
    args.remove(i, 2);
  }
  if (args.isEmpty() || args[0] == QStringLiteral("--aide") || args[0] == QStringLiteral("-h")) {
    dire(QStringLiteral("promethee-en-ligne [--reglages fichier] [--equipe dossier] [--utilisateur nom] reglages|plm deposer|reserver|restituer|consulter …|journal"));
    return args.isEmpty() ? 2 : 0;
  }
  QString probleme;
  const Reglages r = lireReglages(cheminReglages, &probleme);
  if (!probleme.isEmpty()) erreur(probleme);
  if (args[0] == QStringLiteral("reglages")) {
    if (args.contains(QStringLiteral("--initialiser")) && !QFileInfo::exists(cheminReglages)) {
      QDir().mkpath(QFileInfo(cheminReglages).absolutePath());
      QFile f(cheminReglages);
      if (f.open(QIODevice::WriteOnly)) f.write(texteReglagesParDefaut());
    }
    dire(QStringLiteral("Réglages : ") + cheminReglages);
    dire(QStringLiteral("Accès en ligne : ") + (r.actif ? QStringLiteral("activé") : QStringLiteral("désactivé")));
    for (const auto& [nom, f] : r.fonctions)
      dire(QStringLiteral("  %1 : %2%3").arg(nom, f.actif ? QStringLiteral("activée") : QStringLiteral("désactivée"), f.adresse.isEmpty() ? QString() : QStringLiteral(" (") + f.adresse + QLatin1Char(')')));
    dire(QStringLiteral("Journal des connexions : ") + r.journal);
    return 0;
  }
  if (args[0] == QStringLiteral("journal")) {
    const QByteArray j = lire(r.journal);
    std::fwrite(j.constData(), 1, static_cast<std::size_t>(j.size()), stdout);
    return 0;
  }
  if (args[0] == QStringLiteral("distributeurs") && args.size() >= 3 && args[1] == QStringLiteral("offres")) return offresDistributeurs(r, cheminReglages, equipe, args.mid(2));
  if (args[0] == QStringLiteral("erp") && args.size() >= 2 && args[1] == QStringLiteral("lire")) return lireErp(r, equipe);
  if (args[0] == QStringLiteral("tickets") && args.size() >= 2 && args[1] == QStringLiteral("synchroniser")) return synchroniserTickets(r, cheminReglages, equipe);
  if (args[0] != QStringLiteral("plm") || args.size() < 3) {
    erreur(QStringLiteral("commande inconnue ; --aide pour la liste"));
    return 2;
  }
  // ---------- PLM ----------
  if (const QString non = refus(r, QStringLiteral("plm")); !non.isEmpty()) {
    erreur(QStringLiteral("Rien n'est parti : ") + non);
    return 3;
  }
  const ReglageFonction& rf = r.fonctions.at(QStringLiteral("plm"));
  const auto g = gabarit(rf.adaptateur.isEmpty() ? QStringLiteral("simule") : rf.adaptateur, QFileInfo(cheminReglages).absolutePath(), &probleme);
  if (!g) {
    erreur(probleme);
    return 2;
  }
  const QString operation = args[1], cible = args[2];
  std::map<QString, QString> valeurs = {{QStringLiteral("utilisateur"), utilisateur.isEmpty() ? rf.utilisateur : utilisateur}};
  QString numero = cible;
  if (operation == QStringLiteral("deposer") || operation == QStringLiteral("restituer")) {
    const auto v = valeursDepot(cible, &probleme);
    if (!probleme.isEmpty()) {
      erreur(probleme);
      return 5;
    }
    valeurs.insert(v.begin(), v.end());
    numero = valeurs[QStringLiteral("numero")];
    if (equipe.isEmpty()) equipe = equipeDuDepot(cible);
  } else
    valeurs[QStringLiteral("numero")] = numero;
  // Identifiant donné par le serveur au dépôt (Windchill, Teamcenter, 3DEXPERIENCE), gardé dans les états
  valeurs[QStringLiteral("identifiant")] = numero;
  valeurs[QStringLiteral("csrf")] = QString();
  const ReponsePlm rep = executer(r, QStringLiteral("plm"), *g, operation, valeurs);
  if (!rep.ok) {
    erreur(QStringLiteral("PLM : ") + rep.erreur);
    noterEtat(equipe, numero, rep, QStringLiteral("erreur"), rep.erreur);
    return 4;
  }
  const QString parDefaut = operation == QStringLiteral("deposer")    ? QStringLiteral("déposé")
                            : operation == QStringLiteral("reserver")  ? QStringLiteral("réservé")
                            : operation == QStringLiteral("restituer") ? QStringLiteral("restitué")
                                                                        : QString();
  noterEtat(equipe, numero, rep, parDefaut, operation + QStringLiteral(" par ") + g->nom);
  dire(QStringLiteral("%1 %2 : %3, révision %4").arg(operation, numero, rep.etat.isEmpty() ? parDefaut : rep.etat, rep.revision));
  return 0;
}

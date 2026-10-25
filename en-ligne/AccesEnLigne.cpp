// Prométhée : programme séparé d'accès en ligne (décision D13). Voir AccesEnLigne.hpp. Licence GPL-3.0-only.
#include "AccesEnLigne.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace prom::enligne {

QString cheminReglagesParDefaut() {
  if (const QByteArray force = qgetenv("PROMETHEE_ACCES_EN_LIGNE"); !force.isEmpty()) return QString::fromUtf8(force);
  // Mode portable : à côté du programme, comme le reste des données
  QString programme = QString::fromUtf8(qgetenv("PROMETHEE_DOSSIER_PROGRAMME"));
  if (programme.isEmpty()) programme = QCoreApplication::applicationDirPath();
  if (QFileInfo::exists(QDir(programme).filePath(QStringLiteral("promethee-portable.txt"))))
    return QDir(QDir(programme).filePath(QStringLiteral("promethee-donnees"))).filePath(QStringLiteral("acces-en-ligne.json"));
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(QStringLiteral("acces-en-ligne.json"));
}

QByteArray texteReglagesParDefaut() {
  QJsonObject fonctions;
  for (const QString& f : {QStringLiteral("plm"), QStringLiteral("distributeurs"), QStringLiteral("tickets"), QStringLiteral("erp")})
    fonctions[f] = QJsonObject{{QStringLiteral("actif"), false}, {QStringLiteral("adaptateur"), f == QStringLiteral("plm") ? QStringLiteral("simule") : QString()},
                               {QStringLiteral("adresse"), QString()}, {QStringLiteral("utilisateur"), QString()}, {QStringLiteral("jeton"), QString()}};
  QJsonArray gabarits;
  for (const QString& g : gabaritsIntegres()) gabarits.append(g);
  const QJsonObject o{{QStringLiteral("format"), QStringLiteral("promethee-acces-en-ligne")},
                      {QStringLiteral("version"), 1},
                      {QStringLiteral("actif"), false},
                      {QStringLiteral("commentaire"), QStringLiteral("Rien ne part tant que « actif » et la fonction ne sont pas à true (décision D13).")},
                      {QStringLiteral("gabaritsDisponibles"), gabarits},
                      {QStringLiteral("journal"), QStringLiteral("journal-connexions.csv")},
                      {QStringLiteral("fonctions"), fonctions}};
  return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

Reglages lireReglages(const QString& chemin, QString* erreur) {
  Reglages r;
  r.chemin = chemin;
  r.journal = QDir(QFileInfo(chemin).absolutePath()).filePath(QStringLiteral("journal-connexions.csv"));
  QFile f(chemin);
  if (!f.exists()) return r;  // aucun fichier : tout est désactivé
  if (!f.open(QIODevice::ReadOnly)) {
    if (erreur) *erreur = QStringLiteral("réglages illisibles : ") + chemin;
    return r;
  }
  QJsonParseError e{};
  const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &e);
  if (!d.isObject()) {
    if (erreur) *erreur = QStringLiteral("réglages invalides : ") + e.errorString();
    return r;
  }
  const QJsonObject o = d.object();
  r.actif = o.value(QStringLiteral("actif")).toBool(false);
  if (const QString j = o.value(QStringLiteral("journal")).toString(); !j.isEmpty()) r.journal = QDir::isAbsolutePath(j) ? j : QDir(QFileInfo(chemin).absolutePath()).filePath(j);
  const QJsonObject fs = o.value(QStringLiteral("fonctions")).toObject();
  for (auto it = fs.begin(); it != fs.end(); ++it) {
    const QJsonObject x = it.value().toObject();
    ReglageFonction rf;
    rf.actif = x.value(QStringLiteral("actif")).toBool(false);
    rf.adaptateur = x.value(QStringLiteral("adaptateur")).toString();
    rf.adresse = x.value(QStringLiteral("adresse")).toString();
    rf.utilisateur = x.value(QStringLiteral("utilisateur")).toString();
    rf.jeton = x.value(QStringLiteral("jeton")).toString();
    rf.delai = x.value(QStringLiteral("delai")).toInt(20);
    r.fonctions[it.key()] = rf;
  }
  return r;
}

QString refus(const Reglages& r, const QString& fonction) {
  if (!r.actif) return QStringLiteral("accès en ligne désactivé (« actif » n'est pas à true dans %1) : rien n'est parti").arg(r.chemin);
  const auto it = r.fonctions.find(fonction);
  if (it == r.fonctions.end() || !it->second.actif) return QStringLiteral("la fonction « %1 » n'est pas activée : rien n'est parti").arg(fonction);
  if (it->second.adresse.isEmpty()) return QStringLiteral("la fonction « %1 » n'a pas d'adresse").arg(fonction);
  return {};
}

std::optional<Gabarit> lireGabarit(const QByteArray& json, QString* erreur) {
  QJsonParseError e{};
  const QJsonDocument d = QJsonDocument::fromJson(json, &e);
  if (!d.isObject()) {
    if (erreur) *erreur = e.errorString();
    return std::nullopt;
  }
  const QJsonObject o = d.object();
  Gabarit g;
  g.nom = o.value(QStringLiteral("nom")).toString();
  g.description = o.value(QStringLiteral("description")).toString();
  g.authentification = o.value(QStringLiteral("authentification")).toString(QStringLiteral("bearer"));
  const QJsonObject ops = o.value(QStringLiteral("operations")).toObject();
  for (auto it = ops.begin(); it != ops.end(); ++it) {
    const QJsonObject x = it.value().toObject();
    Operation op;
    op.methode = x.value(QStringLiteral("methode")).toString(QStringLiteral("GET"));
    op.chemin = x.value(QStringLiteral("chemin")).toString();
    op.corps = x.value(QStringLiteral("corps")).toString();
    op.champRevision = x.value(QStringLiteral("revision")).toString(QStringLiteral("revision"));
    op.champEtat = x.value(QStringLiteral("etat")).toString(QStringLiteral("etat"));
    const QJsonObject h = x.value(QStringLiteral("entetes")).toObject();
    for (auto k = h.begin(); k != h.end(); ++k) op.entetes[k.key()] = k.value().toString();
    op.liste = x.value(QStringLiteral("liste")).toString();
    const QJsonObject ch = x.value(QStringLiteral("champs")).toObject();
    for (auto k = ch.begin(); k != ch.end(); ++k) op.champs[k.key()] = k.value().toString();
    for (const QJsonValue& c : x.value(QStringLiteral("clos")).toArray()) op.clos << c.toString();
    g.operations[it.key()] = op;
  }
  if (g.nom.isEmpty() || g.operations.empty()) {
    if (erreur) *erreur = QStringLiteral("gabarit sans nom ou sans opération");
    return std::nullopt;
  }
  return g;
}

QByteArray texteGabaritIntegre(const QString& nom);  // Gabarits.cpp

std::optional<Gabarit> gabarit(const QString& nom, const QString& dossierReglages, QString* erreur) {
  // Un gabarit modifié par l'utilisateur (« gabarits/<nom>.json » à côté des réglages) l'emporte sur l'intégré
  QFile f(QDir(dossierReglages).filePath(QStringLiteral("gabarits/") + nom + QStringLiteral(".json")));
  if (f.open(QIODevice::ReadOnly)) return lireGabarit(f.readAll(), erreur);
  const QByteArray integre = texteGabaritIntegre(nom);
  if (integre.isEmpty()) {
    if (erreur) *erreur = QStringLiteral("gabarit « %1 » inconnu").arg(nom);
    return std::nullopt;
  }
  return lireGabarit(integre, erreur);
}

void noter(const Reglages& r, const QString& fonction, const QString& methode, const QString& adresse, int statut, qint64 envoyes, const QString& detail) {
  QFile f(r.journal);
  const bool nouveau = !f.exists();
  QDir().mkpath(QFileInfo(r.journal).absolutePath());
  if (!f.open(QIODevice::Append | QIODevice::Text)) return;
  auto champ = [](QString s) { return s.replace(QLatin1Char(';'), QLatin1Char(',')).replace(QLatin1Char('\n'), QLatin1Char(' ')); };
  QByteArray l;
  if (nouveau) l += "Date;Fonction;Méthode;Adresse;Statut;Octets envoyés;Détail\n";
  l += (QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + QLatin1Char(';') + champ(fonction) + QLatin1Char(';') + methode + QLatin1Char(';') + champ(adresse) + QLatin1Char(';') +
        QString::number(statut) + QLatin1Char(';') + QString::number(envoyes) + QLatin1Char(';') + champ(detail) + QLatin1Char('\n'))
           .toUtf8();
  f.write(l);
}

namespace {
QString remplir(QString modele, const std::map<QString, QString>& valeurs, bool json) {
  for (const auto& [k, v] : valeurs) {
    QString x = v;
    // Dans un corps JSON, une valeur texte s'échappe (sauf les valeurs déjà JSON : manifeste, fichiers)
    if (json && k != QStringLiteral("manifeste") && k != QStringLiteral("fichiers")) {
      const QByteArray e = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
      x = QString::fromUtf8(e.mid(2, e.size() - 4));
    }
    modele.replace(QLatin1Char('{') + k + QLatin1Char('}'), x);
  }
  return modele;
}
QString champ(const QJsonObject& o, const QString& chemin) { return valeurJson(o, chemin); }
}  // namespace

QString valeurJson(const QJsonValue& racine, const QString& chemin) {
  QJsonValue v = racine;
  for (const QString& p : chemin.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
    bool nombre = false;
    const int i = p.toInt(&nombre);
    v = nombre && v.isArray() ? v.toArray().at(i) : v.toObject().value(p);
  }
  return v.isString() ? v.toString() : v.isDouble() ? QString::number(v.toDouble(), 'g', 15) : QString();
}

ReponsePlm executer(const Reglages& r, const QString& fonction, const Gabarit& g, const QString& operation, const std::map<QString, QString>& valeurs) {
  ReponsePlm rep;
  if (const QString non = refus(r, fonction); !non.isEmpty()) {
    rep.erreur = non;  // aucune connexion : on ne crée même pas le gestionnaire de réseau
    return rep;
  }
  const auto it = g.operations.find(operation);
  if (it == g.operations.end()) {
    rep.erreur = QStringLiteral("le gabarit « %1 » ne sait pas « %2 »").arg(g.nom, operation);
    return rep;
  }
  const ReglageFonction& rf = r.fonctions.at(fonction);
  const Operation& op = it->second;
  const QUrl url(rf.adresse + remplir(op.chemin, valeurs, false));
  QNetworkRequest q(url);
  q.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  q.setRawHeader("Accept", "application/json");
  q.setRawHeader("User-Agent", "promethee-en-ligne");
  if (g.authentification == QStringLiteral("bearer") && !rf.jeton.isEmpty()) q.setRawHeader("Authorization", "Bearer " + rf.jeton.toUtf8());
  else if (g.authentification == QStringLiteral("basic") && !rf.utilisateur.isEmpty())
    q.setRawHeader("Authorization", "Basic " + (rf.utilisateur + QLatin1Char(':') + rf.jeton).toUtf8().toBase64());
  for (const auto& [k, v] : op.entetes) q.setRawHeader(k.toUtf8(), remplir(v, valeurs, false).toUtf8());
  const QByteArray corps = op.corps.isEmpty() ? QByteArray() : remplir(op.corps, valeurs, true).toUtf8();
  QNetworkAccessManager reseau;
  QNetworkReply* reponse = reseau.sendCustomRequest(q, op.methode.toUtf8(), corps);
  QEventLoop boucle;
  QTimer minuterie;
  minuterie.setSingleShot(true);
  QObject::connect(&minuterie, &QTimer::timeout, reponse, &QNetworkReply::abort);
  QObject::connect(reponse, &QNetworkReply::finished, &boucle, &QEventLoop::quit);
  minuterie.start(rf.delai * 1000);
  boucle.exec();
  rep.statut = reponse->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QByteArray lu = reponse->readAll();
  if (reponse->error() != QNetworkReply::NoError && rep.statut == 0) rep.erreur = reponse->errorString();
  reponse->deleteLater();
  const QJsonDocument d = QJsonDocument::fromJson(lu);
  if (d.isObject()) rep.corps = d.object();
  else if (d.isArray()) rep.tableau = d.array();
  rep.ok = rep.statut >= 200 && rep.statut < 300;
  if (!rep.ok && rep.erreur.isEmpty()) rep.erreur = QStringLiteral("le serveur a répondu %1 %2").arg(rep.statut).arg(rep.corps.value(QStringLiteral("erreur")).toString());
  rep.revision = champ(rep.corps, op.champRevision);
  rep.etat = champ(rep.corps, op.champEtat);
  rep.reservePar = rep.corps.value(QStringLiteral("reservePar")).toString();
  noter(r, fonction, op.methode, url.toString(QUrl::RemoveUserInfo), rep.statut, corps.size(), operation + (rep.ok ? QString() : QStringLiteral(" : ") + rep.erreur));
  return rep;
}

}  // namespace prom::enligne

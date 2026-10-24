// Prométhée : réglages déclarés une fois (lot L2, tâche 2.32). Licence GPL-3.0-only.
#include "promethee/csv.hpp"
#include "RegistreReglages.hpp"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <memory>

#include <QSettings>

#include <QApplication>
#include <QWidget>

#include "DescriptionRuban.hpp"
#include "NavigationVue.hpp"
#include "VueSchema.hpp"
#include "RegistreActions.hpp"
#include "Theme.hpp"

namespace {
// Sans accents ni casse : « reticule » trouve « Réticule »
QString plat(const QString& t) {
  QString r = t.normalized(QString::NormalizationForm_D).toLower();
  r.remove(QRegularExpression(QStringLiteral("[\\x{0300}-\\x{036f}]")));
  return r;
}
// Les réglages de la souris, relus d'un coup : la navigation les tient en mémoire
void relireNavigation() {
  const QSettings s;
  ReglagesNavigation r;
  r.zoomCran = std::clamp(s.value(QStringLiteral("navigation/zoomCran"), r.zoomCran).toDouble(), 1.1, 2.0);
  r.moletteInverse = s.value(QStringLiteral("navigation/moletteInverse"), r.moletteInverse).toBool();
  r.defilementCroise = s.value(QStringLiteral("navigation/defilementCroise"), r.defilementCroise).toBool();
  r.zoomSurCurseur = s.value(QStringLiteral("navigation/zoomSurCurseur"), r.zoomSurCurseur).toBool();
  r.defilementBord = s.value(QStringLiteral("navigation/defilementBord"), r.defilementBord).toBool();
  r.deuxDoigts = static_cast<ReglagesNavigation::DeuxDoigts>(std::clamp(s.value(QStringLiteral("navigation/deuxDoigts"), 0).toInt(), 0, 1));
  r.gestes = static_cast<ReglagesNavigation::BoutonGestes>(std::clamp(s.value(QStringLiteral("navigation/boutonGestes"), 0).toInt(), 0, 2));
  Navigation::instance().setReglages(r);
}
// Clés propres à un poste ou à un lancement, jamais exportées : géométrie des fenêtres, essais, synchronisation
bool exportable(const QString& cle) {
  static const QStringList exclues{QStringLiteral("fenetre/"), QStringLiteral("geometrie"), QStringLiteral("etat/"), QStringLiteral("reglages/dossierSynchronise"),
                                   QStringLiteral("reglages/version"), QStringLiteral("session/")};
  return std::none_of(exclues.begin(), exclues.end(), [&](const QString& e) { return cle.startsWith(e) || cle.contains(e); });
}
// Clés renommées d'une version à l'autre (ancienne → nouvelle) : la reprise les déplace
const std::vector<std::pair<const char*, const char*>>& renommees() {
  static const std::vector<std::pair<const char*, const char*>> r{{"theme", "apparence/theme"}, {"schema/annotation", "schema/annotationALaPose"}};
  return r;
}
}  // namespace

RegistreReglages& RegistreReglages::instance() {
  static RegistreReglages r;
  return r;
}

RegistreReglages::RegistreReglages() {
  using G = Reglage::Genre;
  // ---------- Apparence (2.31, ERG-04, ERG-36) ----------
  {
    Reglage r{QStringLiteral("apparence/theme"), QStringLiteral("Thème des couleurs"), QStringLiteral("couleurs sombre clair contraste nuit"), QStringLiteral("Apparence"), G::Choix,
              Theme::cle(IdTheme::Promethee)};
    for (const IdTheme t : Theme::TOUS) {
      r.choix << Theme::nom(t);
      r.valeurs << Theme::cle(t);
    }
    r.appliquer = [](const QVariant& v) { Theme::instance().choisir(Theme::depuisCle(v.toString())); };
    declarer(r);
  }
  {
    Reglage r{QStringLiteral("apparence/palette"), QStringLiteral("Couleurs des canevas"), QStringLiteral("palette kicad altium eagle daltonien daltonisme cuivre schéma"),
              QStringLiteral("Apparence"), G::Choix, 0};
    r.choix = Theme::palettes();
    r.bulle = QStringLiteral("Les couleurs du schéma et du circuit : celles du thème, celles d’un autre logiciel, ou une palette lisible par les daltoniens");
    r.appliquer = [](const QVariant& v) { Theme::instance().choisirPalette(v.toInt()); };
    declarer(r);
  }
  {
    Reglage r{QStringLiteral("apparence/echelleInterface"), QStringLiteral("Échelle de l’interface"), QStringLiteral("taille grossir agrandir zoom écran dpi"), QStringLiteral("Apparence"),
              G::Choix, 0};
    r.choix = {QStringLiteral("100 %"), QStringLiteral("125 %"), QStringLiteral("150 %"), QStringLiteral("175 %"), QStringLiteral("200 %")};
    r.redemarrer = true;
    r.bulle = QStringLiteral("Appliquée au prochain lancement (QT_SCALE_FACTOR)");
    declarer(r);
  }
  {
    Reglage r{QStringLiteral("apparence/tailleIcones"), QStringLiteral("Taille des icônes"), QStringLiteral("pictogrammes ruban boutons"), QStringLiteral("Apparence"), G::Choix, 1};
    r.choix = {QStringLiteral("Petites (16 px)"), QStringLiteral("Moyennes (20 px)"), QStringLiteral("Grandes (24 px)"), QStringLiteral("Très grandes (32 px)")};
    r.redemarrer = true;
    declarer(r);
  }
  {
    Reglage r{QStringLiteral("apparence/echelleCanevas"), QStringLiteral("Échelle des signes du canevas"), QStringLiteral("poignées réticule épaisseur traits"), QStringLiteral("Apparence"),
              G::Reel, 1.0};
    r.min = 0.75;
    r.max = 2.0;
    r.pas = 0.25;
    r.suffixe = QStringLiteral(" ×");
    r.appliquer = [](const QVariant&) { Theme::instance().rafraichir(); };
    declarer(r);
  }
  {
    Reglage r{QStringLiteral("apparence/anticrenelage"), QStringLiteral("Lissage des traits (anticrénelage)"), QStringLiteral("antialiasing lisser dents"), QStringLiteral("Apparence"),
              G::Booleen, true};
    r.appliquer = [](const QVariant&) { Theme::instance().rafraichir(); };
    declarer(r);
  }
  // ---------- Schéma ----------
  declarer({QStringLiteral("schema/annotationALaPose"), QStringLiteral("Repère donné à la pose"), QStringLiteral("annotation numérotation référence"), QStringLiteral("Schéma"), G::Booleen, true});
  {
    Reglage r{QStringLiteral("schema/sautsCroisements"), QStringLiteral("Saut aux croisements de fils"), QStringLiteral("enjambement pont croisement"), QStringLiteral("Schéma"), G::Booleen, false};
    r.appliquer = [](const QVariant&) {
      for (QWidget* w : QApplication::allWidgets())
        if (auto* v = qobject_cast<VueSchema*>(w)) v->invaliderCache();
    };
    declarer(r);
  }
  declarer({QStringLiteral("schema/etiquetteBoutOuvert"), QStringLiteral("Étiquette au bout d’un fil laissé ouvert"), QStringLiteral("nom réseau extrémité"), QStringLiteral("Schéma"),
            G::Booleen, false});
  declarer({QStringLiteral("schema/coordonneesPolaires"), QStringLiteral("Coordonnées relatives en polaire"), QStringLiteral("barre d’état longueur angle cartésien"), QStringLiteral("Schéma"),
            G::Booleen, true});
  {
    Reglage r{QStringLiteral("schema/reticule"), QStringLiteral("Réticule"), QStringLiteral("curseur croix viseur"), QStringLiteral("Schéma"), G::Choix, 0};
    r.choix = {QStringLiteral("Petit"), QStringLiteral("Sur toute la vue"), QStringLiteral("Aucun")};
    declarer(r);
  }
  // Valeurs et exclusions sur la feuille (3.2, 3.17)
  auto redessiner = [](const QVariant&) {
    for (QWidget* w : QApplication::allWidgets())
      if (auto* v = qobject_cast<VueSchema*>(w)) {
        v->invaliderCache();
        v->update();
      }
  };
  {
    Reglage r{QStringLiteral("schema/affichageValeurs"), QStringLiteral("Affichage des valeurs"), QStringLiteral("4k7 kΩ unité CEI 60062 compacte système international"),
              QStringLiteral("Schéma"), G::Choix, 0};
    r.choix = {QStringLiteral("Comme tapées"), QStringLiteral("CEI 60062 compacte (4k7)"), QStringLiteral("Système international (4,7 kΩ)")};
    r.bulle = QStringLiteral("Une valeur que Prométhée ne comprend pas reste comme tapée.");
    r.appliquer = redessiner;
    declarer(r);
  }
  {
    Reglage r{QStringLiteral("schema/marquerNonMontes"), QStringLiteral("Barrer les composants non montés"), QStringLiteral("ne pas monter DNP croix variante"), QStringLiteral("Schéma"), G::Booleen, true};
    r.appliquer = redessiner;
    declarer(r);
  }
  {
    Reglage r{QStringLiteral("schema/couleurNonMontes"), QStringLiteral("Couleur de la croix des non-montés"), QStringLiteral("ne pas monter couleur rouge"), QStringLiteral("Schéma"), G::Texte,
              QStringLiteral("#D63B3B")};
    r.appliquer = redessiner;
    declarer(r);
  }
  {
    Reglage r{QStringLiteral("schema/marquerHorsSimulation"), QStringLiteral("Griser les composants exclus de la simulation"), QStringLiteral("simulation spice exclu gris"), QStringLiteral("Schéma"),
              G::Booleen, true};
    r.appliquer = redessiner;
    declarer(r);
  }
  {
    Reglage r{QStringLiteral("schema/couleurHorsSimulation"), QStringLiteral("Couleur du signe « hors simulation »"), QStringLiteral("simulation couleur"), QStringLiteral("Schéma"), G::Texte,
              QStringLiteral("#8A8F99")};
    r.appliquer = redessiner;
    declarer(r);
  }
  // ---------- Souris et pavé tactile (1.19) ----------
  auto navigation = [this](const char* cle, const char* libelle, const char* mots, G g, QVariant defaut) {
    Reglage r{QStringLiteral("navigation/") + QLatin1String(cle), QString::fromUtf8(libelle), QString::fromUtf8(mots), QStringLiteral("Souris et pavé tactile"), g, defaut};
    r.appliquer = [](const QVariant&) { relireNavigation(); };
    return r;
  };
  {
    Reglage r = navigation("zoomCran", "Zoom par cran de molette", "molette facteur grossissement", G::Reel, 1.25);
    r.min = 1.1;
    r.max = 2.0;
    r.pas = 0.05;
    r.suffixe = QStringLiteral(" ×");
    declarer(r);
  }
  declarer(navigation("moletteInverse", "Molette inversée", "sens défilement zoom", G::Booleen, false));
  declarer(navigation("defilementCroise", "Défilement croisé (Maj + molette horizontal)", "maj ctrl molette horizontal vertical", G::Booleen, false));
  declarer(navigation("zoomSurCurseur", "Zoom centré sur le curseur", "centre zoom pointeur", G::Booleen, true));
  declarer(navigation("defilementBord", "Défilement au bord pendant un glisser", "bord glisser auto", G::Booleen, true));
  {
    Reglage r = navigation("deuxDoigts", "Deux doigts sur le pavé tactile", "pavé tactile trackpad doigts", G::Choix, 0);
    r.choix = {QStringLiteral("Zoomer"), QStringLiteral("Faire défiler")};
    declarer(r);
  }
  {
    Reglage r = navigation("boutonGestes", "Bouton du menu à marques", "gestes menu circulaire marking", G::Choix, 0);
    r.choix = {QStringLiteral("Droit avec Ctrl"), QStringLiteral("Bouton latéral"), QStringLiteral("Bouton du milieu")};
    declarer(r);
  }
  // ---------- Clavier ----------
  {
    Reglage r{QStringLiteral("clavier/premierAppui"), QStringLiteral("Premier appui d’une touche d’outil"), QStringLiteral("touche outil armer agir curseur"), QStringLiteral("Clavier"),
              G::Choix, 0};
    r.choix = {QStringLiteral("Arme l’outil"), QStringLiteral("Agit au curseur")};
    declarer(r);
  }
  // ---------- Session (ERG-35) ----------
  declarer({QStringLiteral("session/rouvrir"), QStringLiteral("Rouvrir la dernière session au lancement"), QStringLiteral("projet atelier feuille zoom reprendre"), QStringLiteral("Session"),
            G::Booleen, true});
  {
    Reglage r{QStringLiteral("recents/longueur"), QStringLiteral("Projets récents gardés"), QStringLiteral("historique fichiers liste accueil"), QStringLiteral("Session"), G::Entier, 10};
    r.min = 3;
    r.max = 40;
    declarer(r);
  }
  // ---------- Échanges (0.34, 10.20) : l'écrivain CSV unique du noyau, réglé une fois ----------
  {
    Reglage r{QStringLiteral("echanges/csv"), QStringLiteral("Fichiers CSV écrits pour un tableur"), QStringLiteral("csv séparateur décimale point-virgule virgule excel libreoffice tableur marque utf-8 bom"),
              QStringLiteral("Échanges"), G::Choix, QStringLiteral("; , bom crlf")};
    r.choix = {QStringLiteral("Tableur en français (point-virgule, virgule décimale, marque UTF-8)"), QStringLiteral("International (virgule, point décimal, sans marque)"),
               QStringLiteral("Tabulations (virgule décimale, marque UTF-8)"), QStringLiteral("Point-virgule et point décimal (marque UTF-8)")};
    r.valeurs = {QStringLiteral("; , bom crlf"), QStringLiteral(", . sansbom lf"), QStringLiteral("tab , bom crlf"), QStringLiteral("; . bom crlf")};
    r.bulle = QStringLiteral("Nomenclatures, rapports de changements et listes : séparateur, marque décimale et marque d’ordre. Les fichiers imposés par un fabricant gardent leur format.");
    r.appliquer = [](const QVariant& v) { prom::reglerCsv(prom::reglagesCsvDepuisTexte(v.toString().toStdString())); };
    declarer(r);
  }
  {
    Reglage r{QStringLiteral("reglages/dossierSynchronise"), QStringLiteral("Dossier synchronisé des réglages"), QStringLiteral("nuage partage poste sauvegarde synchroniser"),
              QStringLiteral("Session"), G::Texte, QString()};
    r.bulle = QStringLiteral("Un dossier partagé (nuage, réseau) : les réglages y sont écrits à chaque changement, et relus au lancement s’ils y sont plus récents");
    declarer(r);
  }
}

const Reglage* RegistreReglages::reglage(const QString& cle) const {
  for (const auto& r : m_reglages)
    if (r.cle == cle) return &r;
  return nullptr;
}

QStringList RegistreReglages::pages() const {
  QStringList p;
  for (const auto& r : m_reglages)
    if (!p.contains(r.page)) p << r.page;
  return p;
}

std::vector<const Reglage*> RegistreReglages::chercher(const QString& texte) const {
  std::vector<const Reglage*> r;
  const QStringList mots = plat(texte).split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (mots.isEmpty()) return r;
  for (const auto& x : m_reglages) {
    const QString foin = plat(x.libelle + QLatin1Char(' ') + x.motsVoisins + QLatin1Char(' ') + x.page + QLatin1Char(' ') + x.choix.join(QLatin1Char(' ')));
    if (std::all_of(mots.begin(), mots.end(), [&](const QString& m) { return foin.contains(m); })) r.push_back(&x);
  }
  return r;
}

QVariant RegistreReglages::valeur(const QString& cle) const {
  const Reglage* r = reglage(cle);
  // Un QSettings gardé par fil : la vue en lit des réglages à chaque image, et en construire un par lecture coûte.
  // Les QSettings d'un même processus partagent leurs valeurs : ce qu'un autre écrit se lit aussitôt. Il est refait
  // si le format par défaut change (mode portable).
  thread_local std::unique_ptr<QSettings> s;
  thread_local QSettings::Format format = QSettings::NativeFormat;
  if (!s || format != QSettings::defaultFormat()) {
    s = std::make_unique<QSettings>();
    format = QSettings::defaultFormat();
  }
  return s->value(cle, r ? r->defaut : QVariant());
}

void RegistreReglages::ecrire(const QString& cle, const QVariant& v) {
  QSettings().setValue(cle, v);
  if (const Reglage* r = reglage(cle); r && r->appliquer) r->appliquer(v);
  if (cle != QLatin1String("reglages/dossierSynchronise")) synchroniser();
}

QJsonObject RegistreReglages::exporter() const {
  QSettings s;
  QJsonObject valeurs;
  for (const QString& cle : s.allKeys())
    if (exportable(cle)) {
      const QVariant v = s.value(cle);
      // Le ruban est gardé en octets (JSON en UTF-8) : écrit tel quel, lisible
      valeurs.insert(cle, v.typeId() == QMetaType::QByteArray ? QJsonValue(QString::fromUtf8(v.toByteArray())) : QJsonValue::fromVariant(v));
    }
  return QJsonObject{{QStringLiteral("format"), QStringLiteral("promethee-reglages")},
                     {QStringLiteral("version"), 1},
                     {QStringLiteral("ecrit"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                     {QStringLiteral("reglages"), valeurs}};
}

bool RegistreReglages::relire(const QJsonObject& o, QString* erreur) {
  if (o.value(QStringLiteral("format")).toString() != QLatin1String("promethee-reglages")) {
    if (erreur) *erreur = QStringLiteral("ce fichier ne contient pas de réglages de Prométhée");
    return false;
  }
  QSettings s;
  const QJsonObject v = o.value(QStringLiteral("reglages")).toObject();
  for (auto it = v.begin(); it != v.end(); ++it)
    if (exportable(it.key())) s.setValue(it.key(), it.value().toVariant());
  s.sync();
  // Ce qui est tenu en mémoire suit : thème, navigation, réglages déclarés
  for (const auto& r : m_reglages)
    if (r.appliquer && v.contains(r.cle)) r.appliquer(s.value(r.cle, r.defaut));
  RegistreActions::atelierElectronique().relireRaccourcis();
  RubanEnCours::instance().relire();
  Theme::instance().rafraichir();
  return true;
}

void RegistreReglages::remettreAZero() {
  QSettings s;
  const QString dossier = s.value(QStringLiteral("reglages/dossierSynchronise")).toString();
  for (const QString& cle : s.allKeys())
    if (exportable(cle)) s.remove(cle);
  s.sync();
  for (const auto& r : m_reglages)
    if (r.appliquer) r.appliquer(r.defaut);
  RegistreActions::atelierElectronique().relireRaccourcis();
  RubanEnCours::instance().relire();
  Theme::instance().rafraichir();
  Q_UNUSED(dossier);
}

bool RegistreReglages::exporterFichier(const QString& chemin, QString* erreur) const {
  QFile f(chemin);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (erreur) *erreur = f.errorString();
    return false;
  }
  f.write(QJsonDocument(exporter()).toJson(QJsonDocument::Indented));
  return true;
}

bool RegistreReglages::relireFichier(const QString& chemin, QString* erreur) {
  QFile f(chemin);
  if (!f.open(QIODevice::ReadOnly)) {
    if (erreur) *erreur = f.errorString();
    return false;
  }
  QJsonParseError e{};
  const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &e);
  if (e.error != QJsonParseError::NoError) {
    if (erreur) *erreur = e.errorString();
    return false;
  }
  return relire(d.object(), erreur);
}

QString RegistreReglages::dossierSynchronise() const { return QSettings().value(QStringLiteral("reglages/dossierSynchronise")).toString(); }

void RegistreReglages::setDossierSynchronise(const QString& dossier) {
  QSettings().setValue(QStringLiteral("reglages/dossierSynchronise"), dossier);
  if (!dossier.isEmpty()) synchroniser();
}

void RegistreReglages::synchroniser() {
  const QString d = dossierSynchronise();
  if (d.isEmpty() || !QFileInfo(d).isDir()) return;
  exporterFichier(QDir(d).filePath(QStringLiteral("promethee-reglages.json")));
  QSettings().setValue(QStringLiteral("reglages/ecritLocal"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
}

bool RegistreReglages::relireSynchronise() {
  const QString d = dossierSynchronise();
  if (d.isEmpty()) return false;
  const QString chemin = QDir(d).filePath(QStringLiteral("promethee-reglages.json"));
  const QFileInfo fi(chemin);
  if (!fi.exists()) return false;
  const QDateTime local = QDateTime::fromString(QSettings().value(QStringLiteral("reglages/ecritLocal")).toString(), Qt::ISODate);
  if (local.isValid() && fi.lastModified().toUTC() <= local) return false;
  return relireFichier(chemin);
}

QString RegistreReglages::reprendreVersionPrecedente(const QString& versionCourante) {
  QSettings s;
  const QString avant = s.value(QStringLiteral("reglages/version")).toString();
  s.setValue(QStringLiteral("reglages/version"), versionCourante);
  // Premier lancement de tous, ou même version : rien à reprendre
  if (avant.isEmpty() || avant == versionCourante) return {};
  int deplacees = 0;
  for (const auto& [ancienne, nouvelle] : renommees())
    if (s.contains(QString::fromLatin1(ancienne)) && !s.contains(QString::fromLatin1(nouvelle))) {
      s.setValue(QString::fromLatin1(nouvelle), s.value(QString::fromLatin1(ancienne)));
      s.remove(QString::fromLatin1(ancienne));
      ++deplacees;
    }
  Q_UNUSED(deplacees);
  return avant;
}

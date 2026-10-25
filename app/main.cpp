// Prométhée : application de bureau. Licence GPL-3.0-only.
#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QLibraryInfo>
#include <QTranslator>
#include <QLinearGradient>
#include <QPainter>
#include <QSplashScreen>
#include <QLocale>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QMouseEvent>
#include <QTimer>
#include <QMessageBox>
#include <Standard_Failure.hxx>
#include <exception>
#include <cstdio>

#include "promethee/csv.hpp"
#include "promethee/micro_simulation.hpp"
#include "Enseignement034.hpp"
#include "Accueil.hpp"
#include "ChienDeGarde.hpp"
#include "Document.hpp"
#include "EssaisInterface.hpp"
#include "Journal.hpp"
#include "AtelierPiece.hpp"
#include "Esquisseur.hpp"
#include <QAction>
#include <QPushButton>
#include <QToolBar>
#include "AtelierElectronique.hpp"
#include "RegistreReglages.hpp"
#include "DialogueComposant.hpp"
#include "FenetrePrincipale.hpp"
#include "Coquille.hpp"
#include "Recuperation.hpp"
#include "Icones.hpp"
#include "Theme.hpp"
#include "promethee/esquisse.hpp"
#include "promethee/piece.hpp"
#include "Vue3D.hpp"
#include "VueCarte.hpp"
#include "promethee/placement.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <cstring>
#include "promethee/version.hpp"
#include "ChronoComposition.hpp"

namespace {
#ifdef _WIN32
// Rendu OpenGL logiciel de secours : une copie d'opengl32sw.dll (Mesa) nommée opengl32.dll à côté du
// programme est déjà celle d'OpenCascade (Windows cherche d'abord dans le dossier du programme). Qt, qui
// prend sinon la bibliothèque du système, doit utiliser la même, sans quoi les deux ne partagent pas le
// contexte OpenGL.
bool renduLogicielLocal() {
  wchar_t chemin[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, chemin, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return false;
  std::wstring dossier(chemin, n);
  dossier = dossier.substr(0, dossier.find_last_of(L"\\/") + 1);
  return GetFileAttributesW((dossier + L"opengl32.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
}
#endif

// Garde commune de toutes les actions (EX-03) : une exception échappée d'un gestionnaire d'événement (écriture
// refusée, support retiré, échec d'OpenCascade) est consignée au journal et signalée, au lieu d'arrêter le
// logiciel par std::terminate. Le message est affiché après le retour à la boucle d'événements.
class ApplicationProtegee : public QApplication {
public:
  using QApplication::QApplication;
  bool notify(QObject* cible, QEvent* evenement) override {
    // Essais de temps : la pose des images est chronométrée à part (ChronoComposition.hpp)
    if (composition::actif() && (evenement->type() == QEvent::UpdateRequest || evenement->type() == QEvent::Paint)) {
      const composition::Mesure m(evenement->type() == QEvent::UpdateRequest);
      return notifierProtege(cible, evenement);
    }
    return notifierProtege(cible, evenement);
  }

private:
  bool notifierProtege(QObject* cible, QEvent* evenement) {
    try {
      return QApplication::notify(cible, evenement);
    } catch (const std::exception& e) {
      signaler(QString::fromUtf8(e.what()));
    } catch (const Standard_Failure& e) {
      signaler(QString::fromUtf8(e.GetMessageString()));
    } catch (...) {
      signaler(QStringLiteral("erreur inconnue"));
    }
    return false;
  }

  void signaler(const QString& cause) {
    journal("action interrompue par une exception : " + cause.toStdString());
    if (m_enCours) return;  // un seul message à la fois
    m_enCours = true;
    QTimer::singleShot(0, this, [this, cause] {
      QMessageBox::warning(activeWindow(), QStringLiteral("Action interrompue"),
                           QStringLiteral("L’action n’a pas pu aboutir : %1\n\nLe logiciel continue ; vérifiez l’état du projet avant d’enregistrer.").arg(cause));
      m_enCours = false;
    });
  }
  bool m_enCours = false;
};
}  // namespace

int main(int argc, char** argv) {
  // --version : version et identifiant de build, sans ouvrir de fenêtre (scripts, intégration continue).
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--version") == 0) {
      std::printf("%s\nidentifiant %s\n", prom::ligneVersion().c_str(), prom::infosVersion().identifiant);
      std::fflush(stdout);
      return 0;
    }
  // --robot programme.elf [--piste p.pgm] [--attendre texte] [--duree s] [--frequence Hz] (15.8) : robot simulé sans
  // écran ; 0 quand le texte attendu arrive sur la liaison série
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--robot") == 0) {
      std::vector<std::string> a(argv + 1, argv + argc);
      std::string sortie;
      const int code = prom::ecad::commandeRobot(a, &sortie);
      std::fputs(sortie.c_str(), stdout);
      std::fflush(stdout);
      return code;
    }
  // Mode portable (9.11) : avant le journal, pour que rien ne s'écrive dans le profil
  portable::initialiser(argc > 0 ? argv[0] : nullptr);
  installerJournal();
  // Échelle de l'interface réglée dans Prométhée (2.31, ERG-36) : appliquée avant la création de l'application, par
  // QT_SCALE_FACTOR, sauf si l'environnement en impose une ou pendant les essais
  {
    bool essais = false;
    for (int i = 1; i < argc; ++i) essais |= std::strncmp(argv[i], "--essais", 8) == 0;
    if (!essais && !qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) {
      const auto s = portable::actif() ? std::make_unique<QSettings>(portable::fichierReglages(), QSettings::IniFormat)
                                       : std::make_unique<QSettings>(QStringLiteral("Prométhée"), QStringLiteral("Prométhée"));
      static const char* facteurs[] = {"1", "1.25", "1.5", "1.75", "2"};
      const int i = std::clamp(s->value(QStringLiteral("apparence/echelleInterface"), 0).toInt(), 0, 4);
      if (i > 0) qputenv("QT_SCALE_FACTOR", facteurs[i]);
    }
  }
#if defined(_WIN32)
  if (!qEnvironmentVariableIsSet("QT_OPENGL_DLL") && renduLogicielLocal()) {
    qputenv("QT_OPENGL_DLL", "opengl32");
    journal("rendu OpenGL logiciel : opengl32.dll à côté du programme, partagée par Qt et OpenCascade");
  }
  QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
#elif !defined(__APPLE__)
  // La vue 3D d'OpenCascade passe par X11 : on évite Wayland natif tant qu'il n'est pas pris en charge.
  if (!std::getenv("QT_QPA_PLATFORM")) setenv("QT_QPA_PLATFORM", "xcb", 0);
#endif
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QSurfaceFormat format;
  format.setDepthBufferSize(24);
  format.setStencilBufferSize(8);
#ifdef __APPLE__
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setVersion(4, 1);
#else
  format.setProfile(QSurfaceFormat::CompatibilityProfile);
#endif
  QSurfaceFormat::setDefaultFormat(format);

  ApplicationProtegee app(argc, argv);
  {
    // Boîtes de dialogue de Qt en français : traductions livrées avec Qt, ou copiées à côté du programme
    auto* tr = new QTranslator(&app);
    const QStringList dossiers = {QCoreApplication::applicationDirPath() + QStringLiteral("/translations"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)};
    for (const QString& d : dossiers)
      if (tr->load(QLocale(QLocale::French), QStringLiteral("qtbase"), QStringLiteral("_"), d)) { app.installTranslator(tr); break; }
  }
  journal("application Qt créée");
  completerJournal();
  // Qt adopte la langue du système (virgule décimale) : on garde le point pour lire et écrire les fichiers.
  std::setlocale(LC_NUMERIC, "C");
  QCoreApplication::setApplicationName(QStringLiteral("Prométhée"));
  QCoreApplication::setOrganizationName(QStringLiteral("Prométhée"));
  QCoreApplication::setApplicationVersion(QString::fromLatin1(prom::infosVersion().version));
  portable::appliquerReglages();
  if (portable::actif()) journal("mode portable : données dans " + portable::dossier().toStdString());
  // Chien de garde (9.10) : un blocage de plus de dix secondes laisse un rapport dans le dossier des rapports
  chien::demarrer(QDir(Recuperation::dossierDonnees()).filePath(QStringLiteral("rapports")));
  struct ArretChien {
    ~ArretChien() { chien::arreter(); }
  } arretChien;
  // Essai du mode portable : réglage, copie de récupération et bibliothèque personnelle écrits, chemins affichés
  if (app.arguments().contains(QStringLiteral("--essais-portable"))) {
    QSettings().setValue(QStringLiteral("essai/portable"), true);
    QSettings().sync();
    QDir().mkpath(portable::dossierDonneesAppli());
    QFile b(QDir(portable::dossierDonneesAppli()).filePath(QStringLiteral("bibliotheque-electronique.json")));
    if (b.open(QIODevice::WriteOnly)) b.write("{}");
    b.close();
    QDir().mkpath(QDir(Recuperation::dossierDonnees()).filePath(QStringLiteral("recuperation")));
    std::printf("portable %d\nreglages %s\ndonnees %s\nbibliotheques %s\n", portable::actif() ? 1 : 0, QSettings().fileName().toUtf8().constData(),
                Recuperation::dossierDonnees().toUtf8().constData(), portable::dossierDonneesAppli().toUtf8().constData());
    std::fflush(stdout);
    return 0;
  }
  // Essais : des réglages à part, neufs à chaque lancement (dernier atelier par fichier, récents, thème), pour qu'un
  // essai ne dépende jamais de ce qu'un lancement précédent a retenu
  for (const QString& a : app.arguments())
    if (a.startsWith(QLatin1String("--essais-"))) {
      const QString dossier = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-reglages-%1").arg(QCoreApplication::applicationPid()));
      QDir(dossier).removeRecursively();
      QSettings::setDefaultFormat(QSettings::IniFormat);
      QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dossier);
      break;
    }
  QLocale::setDefault(QLocale(QLocale::French, QLocale::France));
  // Réglages (2.32) : ceux d'une version précédente repris au premier lancement de celle-ci, puis ceux d'un dossier
  // synchronisé s'ils y sont plus récents
  if (const QString avant = RegistreReglages::instance().reprendreVersionPrecedente(QCoreApplication::applicationVersion()); !avant.isEmpty())
    journal("réglages repris de la version " + avant.toStdString());
  RegistreReglages::instance().relireSynchronise();
  // Réglages de l'établissement (16.10) : un « etablissement.json » posé à côté du paquet s'impose aux réglages du poste
  if (const auto& e = etablissement::actifs()) journal("réglages de l'établissement appliqués : " + e->nom);
  // --verifier-poste : essais intégrés d'un poste de salle de TP, sans fenêtre ; 0 quand tout est vert
  if (app.arguments().contains(QStringLiteral("--verifier-poste"))) {
    const QStringList l = etablissement::verifierPoste();
    bool vert = true;
    for (const QString& x : l) {
      std::printf("%s\n", x.toUtf8().constData());
      vert &= !x.startsWith(QStringLiteral("ÉCHEC"));
    }
    std::fflush(stdout);
    return vert ? 0 : 1;
  }
  // Écrivain CSV du noyau réglé une fois pour le poste (10.20)
  prom::reglerCsv(prom::reglagesCsvDepuisTexte(RegistreReglages::instance().valeur(QStringLiteral("echanges/csv")).toString().toStdString()));

  // Thème de couleur choisi (Prométhée par défaut), appliqué à toute l'application : voir Theme.hpp
  // La taille des icônes réglée (2.31) : un style qui grandit les icônes des barres, boutons et menus
  {
    class StyleIcones : public QProxyStyle {
     public:
      StyleIcones(QStyle* base, double facteur) : QProxyStyle(base), m_facteur(facteur) {}
      int pixelMetric(PixelMetric m, const QStyleOption* o = nullptr, const QWidget* w = nullptr) const override {
        const int v = QProxyStyle::pixelMetric(m, o, w);
        if (m == PM_SmallIconSize || m == PM_ToolBarIconSize || m == PM_ButtonIconSize || m == PM_LargeIconSize || m == PM_TabBarIconSize) return static_cast<int>(v * m_facteur + 0.5);
        return v;
      }

     private:
      double m_facteur;
    };
    static const double facteurs[] = {0.8, 1.0, 1.2, 1.6};
    const int t = std::clamp(QSettings().value(QStringLiteral("apparence/tailleIcones"), 1).toInt(), 0, 3);
    QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"));
    if (t == 1) QApplication::setStyle(fusion);
    else QApplication::setStyle(new StyleIcones(fusion, facteurs[t]));
  }
  Theme::instance().demarrer(app);

  // Écran d'accueil pendant le chargement (OpenCascade et la vue 3D prennent quelques secondes au premier lancement).
  app.setWindowIcon(iconeMarque());
  QPixmap fondAccueil(640, 340);
  {
    fondAccueil.fill(Qt::transparent);
    QPainter g(&fondAccueil);
    g.setRenderHint(QPainter::Antialiasing);
    g.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath arrondi;
    arrondi.addRoundedRect(QRectF(0, 0, 640, 340), 16, 16);
    g.setClipPath(arrondi);
    QRadialGradient halo(QPointF(170, 150), 330);
    halo.setColorAt(0, QColor(0x10, 0x18, 0x3C));
    halo.setColorAt(1, QColor(0x06, 0x07, 0x10));
    g.fillRect(QRectF(0, 0, 640, 340), halo);
    const QPixmap marque(QStringLiteral(":/marque/marque.png"));
    if (!marque.isNull()) g.drawPixmap(QRectF(28, 40, 250, 250), marque, marque.rect());
    QFont nom = app.font();
    nom.setPixelSize(50);
    nom.setBold(true);
    g.setFont(nom);
    QLinearGradient degrade(300, 0, 610, 0);
    degrade.setColorAt(0, QColor(0x38, 0xE8, 0xF8));
    degrade.setColorAt(0.5, QColor(0x2F, 0x6B, 0xF5));
    degrade.setColorAt(1, QColor(0x88, 0x68, 0xF8));
    g.setPen(QPen(QBrush(degrade), 1));
    g.drawText(QRectF(296, 104, 330, 70), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Prométhée"));
    QFont sous = app.font();
    sous.setPixelSize(15);
    g.setFont(sous);
    g.setPen(QColor(0xC9, 0xD0, 0xEE));
    g.drawText(QRectF(300, 172, 330, 24), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Plateforme libre d’ingénierie intégrée"));
    sous.setPixelSize(12);
    g.setFont(sous);
    g.setPen(QColor(0x7C, 0x86, 0xB4));
    g.drawText(QRectF(300, 198, 330, 20), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Mécanique · Électronique · Électrique — version ") + QCoreApplication::applicationVersion());
    g.setPen(QColor(0x56, 0x5E, 0x8C));
    g.drawText(QRectF(300, 218, 330, 20), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("build ") + QString::fromLatin1(prom::infosVersion().identifiant));
  }
  if (const QByteArray chemin = qgetenv("PROMETHEE_CAPTURE_DEMARRAGE"); !chemin.isEmpty()) fondAccueil.save(QString::fromLocal8Bit(chemin));
  QSplashScreen accueil(fondAccueil);
  accueil.show();
  accueil.showMessage(QStringLiteral("      Chargement du moteur géométrique et de la vue 3D…"), Qt::AlignBottom | Qt::AlignLeft, QColor(0x7C, 0x86, 0xB4));
  app.processEvents();
  {
    // Options de démonstration, de capture et d'essai : elles pilotent une fenêtre de projet seule. Sans elles, la
    // fenêtre à onglets (un fichier passé en argument s'y ouvre).
    static const QStringList options = {"--capture", "--essai", "--essais-interface", "--essai-piece", "--piece-demo", "--esquisse-demo", "--gestionnaire-demo",
                                        "--extrusion-demo", "--piece-vide", "--regions-demo", "--construction-demo", "--accueil-metier", "--essai-rotation", "--kicad",
                                        "--dessous", "--transparent", "--masque", "--vue-dessous", "--selection", "--essais-onglets",
                                        "--essais-electronique", "--electronique-demo", "--essais-audit", "--essais-electrique", "--onglets"};
    bool demo = false;
    for (const QString& a : app.arguments()) if (options.contains(a)) demo = true;
    if (!demo) {
      Coquille coquille;
      accueil.finish(&coquille);
      coquille.show();
      coquille.demarrerRecuperation(true);
      const QStringList a = app.arguments();
      if (a.size() > 1) coquille.ouvrirFichier(a.at(1));
      else if (RegistreReglages::instance().valeur(QStringLiteral("session/rouvrir")).toBool())
        if (FenetrePrincipale* f = coquille.active()) f->rouvrirSession();  // la session d'avant (2.32)
      const int code = app.exec();
      journal("fermeture normale");
      return code;
    }
    // --onglets avec un parcours d'essai (--essais-electronique, --essais-electrique, --essais-audit) : le parcours
    // tourne dans la fenêtre à onglets, comme chez l'utilisateur (lot L1, tâche 1.1, Q1)
    if (app.arguments().contains(QStringLiteral("--onglets"))) {
      const QStringList a = app.arguments();
      using Lanceur = void (*)(FenetrePrincipale&);
      const Lanceur lanceur = a.contains(QStringLiteral("--essais-electronique")) ? &lancerEssaisElectronique
                              : a.contains(QStringLiteral("--essais-electrique")) ? &lancerEssaisElectrique
                              : a.contains(QStringLiteral("--essais-audit"))      ? &lancerEssaisAudit
                                                                                 : nullptr;
      if (lanceur) {
        Coquille coquille;
        accueil.finish(&coquille);
        coquille.show();
        QTimer::singleShot(400, &coquille, [&coquille, lanceur] {
          if (FenetrePrincipale* f = coquille.active()) lanceur(*f);
        });
        const int code = app.exec();
        journal("fermeture normale");
        return code;
      }
    }
    if (app.arguments().contains(QStringLiteral("--essais-onglets"))) {
      // Essais des onglets et des scénarios de documents (créer, enregistrer, rouvrir, fermer, assemblage…),
      // avec des données locales à part (copies de récupération, verrous) pour ne rien mélanger.
      if (qEnvironmentVariableIsEmpty("PROMETHEE_DOSSIER_DONNEES"))
        qputenv("PROMETHEE_DOSSIER_DONNEES", QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-essais-%1").arg(QCoreApplication::applicationPid())).toLocal8Bit());
      Coquille coquille;
      accueil.finish(&coquille);
      coquille.show();
      QTimer::singleShot(400, &coquille, [&coquille] { lancerEssaisOnglets(coquille); });
      const int code = app.exec();
      journal("fermeture normale");
      return code;
    }
  }
  journal("création de la fenêtre");
  FenetrePrincipale fenetre;
  journal("fenêtre créée");
  accueil.showMessage(QStringLiteral("Ouverture de la fenêtre…"), Qt::AlignBottom | Qt::AlignLeft, QColor(0xC9, 0xD2, 0xCD));
  app.processEvents();
  QStringList args = app.arguments();
  // --capture image.png : enregistre une capture de la fenêtre puis quitte (essais automatiques, documentation).
  const int capture = static_cast<int>(args.indexOf(QStringLiteral("--capture")));
  QString image;
  if (capture > 0 && capture + 1 < args.size()) {
    image = args.at(capture + 1);
    args.remove(capture, 2);
  }
  // --essai : déroule un scénario d'utilisation (déplacer un trou, agrandir la carte, ajouter une LED,
  // grandir un condensateur, annuler, rétablir) pour vérifier que toutes les vues suivent sans planter.
  const bool essai = args.removeAll(QStringLiteral("--essai")) > 0;
  const bool essaisInterface = args.removeAll(QStringLiteral("--essais-interface")) > 0;
  // --essai-piece : parcours complet à la souris : nouvelle pièce, rectangle tracé en deux clics, cote, extrusion.
  const bool essaiPiece = args.removeAll(QStringLiteral("--essai-piece")) > 0;
  const bool pieceDemo = args.removeAll(QStringLiteral("--piece-demo")) > 0;
  const bool esquisseDemo = args.removeAll(QStringLiteral("--esquisse-demo")) > 0;
  const bool gestionDemo = args.removeAll(QStringLiteral("--gestionnaire-demo")) > 0;
  const bool extrusionDemo = args.removeAll(QStringLiteral("--extrusion-demo")) > 0;
  const bool pieceVide = args.removeAll(QStringLiteral("--piece-vide")) > 0;
  const bool regionsDemo = args.removeAll(QStringLiteral("--regions-demo")) > 0;
  const bool constructionDemo = args.removeAll(QStringLiteral("--construction-demo")) > 0;
  const int iMetier = static_cast<int>(args.indexOf(QStringLiteral("--accueil-metier")));
  QString metierAccueil;
  if (iMetier > 0 && iMetier + 1 < args.size()) { metierAccueil = args.at(iMetier + 1); args.remove(iMetier, 2); }
  // --essais-electronique : parcours complet de l'atelier électronique ; --electronique-demo MODE : l'exemple livré,
  // ouvert sur schema, circuit, 3d, fabrication, bibliotheque ou composant (captures).
  const bool essaisElectronique = args.removeAll(QStringLiteral("--essais-electronique")) > 0;
  const bool essaisAudit = args.removeAll(QStringLiteral("--essais-audit")) > 0;
  // --essais-electrique : parcours complet de l'atelier électrique (accueil, schéma, armoire, logement, import)
  const bool essaisElectrique = args.removeAll(QStringLiteral("--essais-electrique")) > 0;
  const int iElec = static_cast<int>(args.indexOf(QStringLiteral("--electronique-demo")));
  QString modeElec;
  if (iElec > 0 && iElec + 1 < args.size()) { modeElec = args.at(iElec + 1); args.remove(iElec, 2); }
  // --essai-rotation : vérifie qu'on peut faire un tour complet de la caméra sans butée.
  const bool essaiRotation = args.removeAll(QStringLiteral("--essai-rotation")) > 0;
  // --kicad carte.kicad_pcb : importe une carte KiCad au démarrage.
  const int kicad = static_cast<int>(args.indexOf(QStringLiteral("--kicad")));
  QString carteKicad;
  if (kicad > 0 && kicad + 1 < args.size()) {
    carteKicad = args.at(kicad + 1);
    args.remove(kicad, 2);
  }
  // --dessous : montre la face arrière ; --transparent : boîtier transparent ; --selection REF : sélectionne un composant.
  const bool dessous = args.removeAll(QStringLiteral("--dessous")) > 0;
  const bool transparent = args.removeAll(QStringLiteral("--transparent")) > 0;
  const bool masque = args.removeAll(QStringLiteral("--masque")) > 0;
  const bool vueDessous = args.removeAll(QStringLiteral("--vue-dessous")) > 0;
  const int iSel = static_cast<int>(args.indexOf(QStringLiteral("--selection")));
  QString refSelection;
  if (iSel > 0 && iSel + 1 < args.size()) {
    refSelection = args.at(iSel + 1);
    args.remove(iSel, 2);
  }
  if ((essai || essaisInterface || essaiPiece || pieceDemo || esquisseDemo || gestionDemo || extrusionDemo || pieceVide || regionsDemo || constructionDemo || essaiRotation ||
       !carteKicad.isEmpty() || dessous || transparent || masque || vueDessous || !refSelection.isEmpty() || !image.isEmpty()) &&
      !essaisElectronique && !essaisAudit && !essaisElectrique && modeElec.isEmpty())
    fenetre.document()->exemple();
  // Accueil au lancement ; un projet passé en argument, une carte KiCad ou une démonstration ouvrent le projet.
  if (args.size() > 1) {
    fenetre.ouvrirFichier(args.at(1));
    fenetre.montrerProjet();
  } else if (!carteKicad.isEmpty() || dessous || transparent || masque || vueDessous || !refSelection.isEmpty() || essai || essaiRotation) {
    fenetre.montrerProjet();
    fenetre.montrerAtelierPiece(false);
  } else {
    fenetre.montrerAccueil();
    if (!metierAccueil.isEmpty()) fenetre.findChild<Accueil*>()->choisirMetier(metierAccueil);
  }
  fenetre.show();
  accueil.finish(&fenetre);
  if (dessous || transparent || masque || vueDessous || !refSelection.isEmpty())
    QTimer::singleShot(900, &fenetre, [&fenetre, dessous, transparent, masque, vueDessous, refSelection] {
      if (dessous) fenetre.vueCarte()->setFaceArriere(true);
      if (transparent) fenetre.vue3D()->setBoitierTransparent(true);
      if (masque) fenetre.vue3D()->setModeBoitier(Vue3D::Boitier::Masque);
      if (vueDessous) fenetre.vue3D()->vueStandard(V3d_XnegYnegZneg);
      for (const auto& k : fenetre.document()->projet().composants)
        if (QString::fromStdString(k.ref) == refSelection) fenetre.document()->selectionner({Cible::Composant, k.id});
    });
  if (!carteKicad.isEmpty()) QTimer::singleShot(300, &fenetre, [&fenetre, carteKicad] { fenetre.importerFichierKicad(carteKicad, false); });
  if (essai)
    QTimer::singleShot(800, &fenetre, [&fenetre] {
      Document* doc = fenetre.document();
      doc->modifier([](prom::Projet& p) { for (auto& t : p.trous) if (t.ref == "T1") prom::ancrerTrou(p, t, -20.5, -15.5); });
      doc->debutGeste();
      for (int i = 1; i <= 12; ++i) doc->pendantGeste([i](prom::Projet& p) { p.carte.L = 60 + i; p.carte.x0 = i / 2.0; });
      doc->finGeste();
      std::string led;
      doc->modifier([&led](prom::Projet& p) { led = prom::ajouterComposant(p, "led"); });
      doc->selectionner({Cible::Composant, led});
      doc->modifier([](prom::Projet& p) { for (auto& k : p.composants) if (k.ref == "C1") k.h = 16; });
      doc->annuler();
      doc->retablir();
      // Version 0.3 : insert laiton sur T1, U1 suit le coin haut droit, SW1 verrouillé, la carte s'élargit encore.
      doc->modifier([](prom::Projet& p) {
        for (auto& t : p.trous) if (t.ref == "T1") { t.fixation = "insert"; t.vis = "M2.5"; }
        for (auto& k : p.composants) {
          if (k.ref == "U1") prom::changerAncrage(p, k, "NE");
          if (k.ref == "SW1") k.verrou = true;
        }
        p.carte.L += 8;
        p.carte.x0 += 4;
      });
      for (const auto& k : doc->projet().composants) if (k.ref == "U1") doc->selectionner({Cible::Composant, k.id});
      fenetre.vue3D()->setCouvercle(Vue3D::Couvercle::Souleve);
    });
  // --piece-demo : ouvre l'atelier Pièces sur une plaque percée et arrondie ; --esquisse-demo : sur son esquisse.
  if (pieceDemo || esquisseDemo || gestionDemo || extrusionDemo)
    QTimer::singleShot(700, &fenetre, [&fenetre, esquisseDemo, gestionDemo, extrusionDemo] {
      using namespace prom;
      Piece pc;
      pc.id = "piece1";
      pc.nom = "Support";
      FonctionPiece esq;
      esq.type = TypeFonction::Esquisse;
      Esquisse& e = esq.esquisse;
      const auto l = e.ajouterRectangle(0, 0, 60, 40);
      e.contraindre(TypeContrainte::Coincidence, {e.element(l[0])->p1, 0});
      e.contraindre(TypeContrainte::Longueur, {l[0]}, 70);
      e.contraindre(TypeContrainte::Longueur, {l[1]}, 45);
      const int c = e.ajouterPoint(30, 20);
      const int trou = e.ajouterCercle(c, 6);
      e.contraindre(TypeContrainte::Diametre, {trou}, 16);
      e.contraindre(TypeContrainte::DistanceX, {c}, 35);
      e.contraindre(TypeContrainte::DistanceY, {c}, 22.5);
      // Cotes posées à la souris dans la vraie vie : on les place pour une démonstration lisible.
      for (auto& k : e.contraintes) {
        if (k.type == TypeContrainte::Longueur && k.valeur == 70) { k.place = true; k.lx = 35; k.ly = -9; }
        if (k.type == TypeContrainte::Longueur && k.valeur == 45) { k.place = true; k.lx = 79; k.ly = 22.5; }
        if (k.type == TypeContrainte::Diametre) { k.place = true; k.lx = 48; k.ly = 34; }
        if (k.type == TypeContrainte::DistanceX) { k.place = true; k.lx = 17; k.ly = 12; }
        if (k.type == TypeContrainte::DistanceY) { k.place = true; k.lx = 26; k.ly = 8; }
      }
      resoudre(e);
      const int idEsq = pc.ajouter(esq);
      FonctionPiece ext;
      ext.type = TypeFonction::Extrusion;
      ext.esquisseId = idEsq;
      ext.longueur = 8;
      const int idExt = pc.ajouter(ext);
      FonctionPiece conge;
      conge.type = TypeFonction::Conge;
      conge.rayon = 6;
      for (size_t i = 0; i < 4; ++i) {
        const std::string a = std::to_string(idExt) + ":cote:" + std::to_string(l[i]), b = std::to_string(idExt) + ":cote:" + std::to_string(l[(i + 1) % 4]);
        conge.references.push_back(a < b ? a + "|" + b : b + "|" + a);
      }
      const int idConge = pc.ajouter(conge);
      FonctionPiece ch;
      ch.type = TypeFonction::Chanfrein;
      ch.distance = 1;
      ch.references = {std::to_string(idExt) + ":cote:" + std::to_string(trou) + "|" + std::to_string(idExt) + ":sommet"};
      pc.ajouter(ch);
      const Json j = pieceVersJson(pc);
      fenetre.document()->modifier([&](Projet& p) { p.pieces = {j}; });
      fenetre.montrerAtelierPiece(true);
      if (esquisseDemo) fenetre.atelierPiece()->editerEsquisse(idEsq);
      if (gestionDemo) QTimer::singleShot(500, &fenetre, [&fenetre, idConge] { fenetre.atelierPiece()->editerFonction(idConge); });
      if (extrusionDemo) QTimer::singleShot(500, &fenetre, [&fenetre, idExt] { fenetre.atelierPiece()->editerFonction(idExt); });
    });
  if (essaiPiece)
    QTimer::singleShot(700, &fenetre, [&fenetre] {
      fenetre.montrerAtelierPiece(true);
      fenetre.atelierPiece()->nouvellePiece();
      QTimer::singleShot(400, &fenetre, [&fenetre] {
        auto* e = fenetre.findChild<Esquisseur*>();
        auto clic = [e](QPointF p) {
          QMouseEvent a(QEvent::MouseButtonPress, p, e->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
          QCoreApplication::sendEvent(e, &a);
          QMouseEvent b(QEvent::MouseButtonRelease, p, e->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
          QCoreApplication::sendEvent(e, &b);
        };
        e->setOutil(Esquisseur::Outil::Rectangle);
        const QPointF c(e->width() / 2.0, e->height() / 2.0);
        clic(c + QPointF(-180, 110));
        clic(c + QPointF(160, -90));
        e->setOutil(Esquisseur::Outil::Cercle);
        clic(c);
        clic(c + QPointF(40, 0));
        std::printf("esquisse : %zu éléments, %d degrés de liberté\n", e->esquisse().elements.size(), e->etat().degresLiberte);
        for (auto* barre : fenetre.findChildren<QToolBar*>())
          for (QAction* act : barre->actions())
            if (act->text().contains(QStringLiteral("Terminer"))) act->trigger();
        for (auto* barre : fenetre.findChildren<QToolBar*>())
          for (QAction* act : barre->actions())
            if (act->text() == QStringLiteral("Extrusion")) act->trigger();
        for (auto* b : fenetre.findChildren<QPushButton*>())
          if (b->text() == QStringLiteral("✔ OK") && b->isVisible()) { b->click(); break; }
        std::printf("pièce : %zu fonctions\n", fenetre.document()->projet().pieces.empty() ? 0 : fenetre.document()->projet().pieces[0]["fonctions"].size());
        std::fflush(stdout);
      });
    });
  if (essaisInterface) QTimer::singleShot(300, &fenetre, [&fenetre] { lancerEssaisInterface(fenetre); });
  if (essaisElectronique) QTimer::singleShot(400, &fenetre, [&fenetre] { lancerEssaisElectronique(fenetre); });
  if (essaisAudit) QTimer::singleShot(400, &fenetre, [&fenetre] { lancerEssaisAudit(fenetre); });
  if (essaisElectrique) QTimer::singleShot(400, &fenetre, [&fenetre] { lancerEssaisElectrique(fenetre); });
  if (!modeElec.isEmpty())
    QTimer::singleShot(500, &fenetre, [&fenetre, modeElec] {
      fenetre.choixAccueilPublic(QStringLiteral("exemple-elec"));
      AtelierElectronique* a = fenetre.atelierElectronique();
      if (!a) return;
      const int mode = modeElec == QLatin1String("circuit") ? AtelierElectronique::Circuit : modeElec == QLatin1String("3d") ? AtelierElectronique::Vue3D
                       : modeElec == QLatin1String("fabrication") ? AtelierElectronique::Fabrication : AtelierElectronique::Schema;
      a->ouvrir(0, mode);
      if (modeElec == QLatin1String("bibliotheque")) a->montrerBibliotheque();
      // PROMETHEE_EXPORT_DESSINS=dossier : schéma et plan de montage de l'exemple en PDF (documentation)
      if (const QByteArray d = qgetenv("PROMETHEE_EXPORT_DESSINS"); !d.isEmpty()) {
        const QString dossier = QString::fromLocal8Bit(d);
        a->exporterDessin(QDir(dossier).filePath(QStringLiteral("exemple-schema.pdf")));
        a->exporterPlanMontage(QDir(dossier).filePath(QStringLiteral("exemple-plan-de-montage.pdf")));
      }
      if (modeElec == QLatin1String("composant"))
        if (const auto* c = fenetre.document()->projet().electronique.empty() ? nullptr : &fenetre.document()->projet().electronique[0])
          if (const auto* u = c->parReference("U1")) a->montrerComposant(u->uuid);
      if (modeElec == QLatin1String("creer")) {
        // Fenêtre de création d'un composant, préremplie (capture : PROMETHEE_CAPTURE_DIALOGUE=image.png)
        auto* d = new DialogueComposant(&fenetre);
        prom::ecad::SpecSymbole sp;
        sp.nom = "Capteur I2C";
        sp.description = "Capteur de température et d’humidité, bus I2C";
        sp.empreinte = "SOIC-8";
        sp.fabricant = "Exemple";
        sp.mpn = "CAPT-8";
        d->setSpec(sp);
        d->collerBroches(QStringLiteral("1 VDD\n2 GND\n3 SDA io\n4 SCL in\n5 ALERTE oc\n6 ADR in\n7 NC\n8 RESET in"));
        d->show();
        if (const QByteArray chemin = qgetenv("PROMETHEE_CAPTURE_DIALOGUE"); !chemin.isEmpty())
          QTimer::singleShot(1500, d, [d, chemin] { d->grab().save(QString::fromLocal8Bit(chemin)); });
      }
    });
  if (pieceVide) QTimer::singleShot(600, &fenetre, [&fenetre] { fenetre.montrerAtelierPiece(true); fenetre.atelierPiece()->nouvellePiece(); });
  if (constructionDemo)
    QTimer::singleShot(600, &fenetre, [&fenetre] {
      using namespace prom;
      Piece pc;
      pc.id = "piece1";
      pc.nom = "Construction";
      FonctionPiece esq;
      esq.type = TypeFonction::Esquisse;
      esq.esquisse.ajouterRectangleCentre(0, 0, 30, 30);
      resoudre(esq.esquisse);
      pc.ajouter(esq);
      fenetre.document()->modifier([&](Projet& p) { p.pieces = {pieceVersJson(pc)}; });
      fenetre.montrerAtelierPiece(true);
    });
  if (regionsDemo)
    QTimer::singleShot(600, &fenetre, [&fenetre] {
      using namespace prom;
      Piece pc;
      pc.id = "piece1";
      pc.nom = "Régions";
      FonctionPiece esq;
      esq.type = TypeFonction::Esquisse;
      esq.esquisse.ajouterRectangle(0, 0, 40, 30);
      const int c = esq.esquisse.ajouterPoint(40, 30);
      esq.esquisse.ajouterCercle(c, 12);
      resoudre(esq.esquisse);
      pc.ajouter(esq);
      fenetre.document()->modifier([&](Projet& p) { p.pieces = {pieceVersJson(pc)}; });
      fenetre.montrerAtelierPiece(true);
      QTimer::singleShot(500, &fenetre, [&fenetre] {
        for (auto* barre : fenetre.findChildren<QToolBar*>())
          for (QAction* act : barre->actions())
            if (act->text() == QStringLiteral("Extrusion")) act->trigger();
      });
    });
  if (essaiRotation)
    QTimer::singleShot(1500, &fenetre, [&fenetre] {
      Vue3D* v = fenetre.vue3D();
      const QPointF c(v->width() / 2.0, v->height() / 2.0);
      auto envoyer = [v](QEvent::Type t, QPointF p, Qt::MouseButton b, Qt::MouseButtons bs) {
        QMouseEvent ev(t, p, v->mapToGlobal(p), b, bs, Qt::NoModifier);
        QCoreApplication::sendEvent(v, &ev);
      };
      const gp_Dir avant = v->directionCamera();
      envoyer(QEvent::MouseButtonPress, c, Qt::LeftButton, Qt::LeftButton);
      double pireAlignement = 1.0;
      const int pas = 2, total = 838;  // 838 pixels × 0,0075 rad ≈ un tour complet
      for (int i = pas; i <= total; i += pas) {
        envoyer(QEvent::MouseMove, c + QPointF(0, i), Qt::NoButton, Qt::LeftButton);
        pireAlignement = std::min(pireAlignement, v->directionCamera().Dot(avant));
      }
      envoyer(QEvent::MouseButtonRelease, c + QPointF(0, total), Qt::LeftButton, Qt::NoButton);
      std::printf("rotation verticale : alignement final %.4f, le plus opposé %.4f\n", v->directionCamera().Dot(avant), pireAlignement);
      std::fflush(stdout);
    });
  if (!image.isEmpty())
    QTimer::singleShot(3000, &fenetre, [&fenetre, image] {
      fenetre.grab().save(image);
      QCoreApplication::exit(0);  // sans passer par la question « enregistrer les modifications ? »
    });
  journal("boucle principale");
  const int code = app.exec();
  journal("fermeture normale");
  return code;
}

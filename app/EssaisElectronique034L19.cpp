// Prométhée : essais d'interface de la 0.34, lot L19 — bibliothèque d'équipe (demandes, santé, doublons fusionnés et
// annulés, 19.4), achats et tableau de bord de la nomenclature (19.7), faisceau : câble blindé de J3 au bornier X1,
// planche à l'échelle 1, longueur venue de la 3D et chute de tension, faisceau d'essai chiffré et classeur relu par
// LibreOffice (19.10 à 19.13), dépôt vers le PLM et programme séparé d'accès en ligne face au serveur simulé : aucune
// connexion tant qu'il n'est pas activé, puis dépôt, réservation et restitution en révision B (19.14). Appelés seuls
// avec PROMETHEE_ESSAIS_LOTS=034L19. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <functional>
#include <regex>
#include <set>
#include <string>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>

#include <BRepPrimAPI_MakeBox.hxx>

#include "AtelierElectrique.hpp"
#include "AtelierElectronique.hpp"
#include "Bibliotheque034.hpp"
#include "Cablage034.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetreOutilsConception.hpp"
#include "FenetrePrincipale.hpp"
#include "VueSchema.hpp"
#include "promethee/articles.hpp"
#include "promethee/cablage.hpp"
#include "promethee/electrique.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/gestion_bibliotheque.hpp"
#include "promethee/modele.hpp"
#include "promethee/pdf_documents.hpp"
#include "promethee/plm.hpp"
#include "promethee/projet_partage.hpp"
#include "promethee/sante_bibliotheque.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
bool attendreQue(const std::function<bool()>& condition, int ms) {
  QElapsedTimer t;
  t.start();
  while (!condition() && t.elapsed() < ms) attendre(50);
  return condition();
}
std::string lire(const QString& f) {
  try {
    return prom::lireFichier(prom::cheminFichier(f.toStdString()));
  } catch (...) {
    return {};
  }
}
// Bibliothèque de « n » résistances de valeurs différentes, trois doublons semés (référence sans son tiret)
ContenuBibliotheque bibliothequeSemee(int n) {
  ContenuBibliotheque b;
  b.nom = "Équipe";
  for (int i = 0; i < n - 3; ++i) {
    Article a;
    a.id = "r-" + std::to_string(i);
    a.nom = "Résistance " + std::to_string(1000 + 7 * i) + " Ω";
    a.symbole = "R";
    a.empreinte = "R_0603";
    a.valeur = std::to_string(1000 + 7 * i);
    a.fabricant = "Yageo";
    a.mpn = "RC0603FR-07" + std::to_string(1000 + 7 * i) + "RL";
    b.articles.push_back(a);
  }
  for (const int k : {3, 50, 120}) {
    Article d = b.articles[static_cast<std::size_t>(k)];
    d.id = "doublon-" + std::to_string(k);
    d.mpn.erase(d.mpn.find('-'), 1);
    b.articles.push_back(d);
  }
  return b;
}
ComposantElec* parRef(ConceptionElec& c, const std::string& r) {
  for (auto& k : c.composants)
    if (k.reference == r) return &k;
  return nullptr;
}
// Fil court et étiquette locale au bout d'une broche
void etiqueter(ConceptionElec& c, const std::string& symbole, const std::string& numero, const std::string& nom) {
  for (auto& f : c.feuilles)
    for (auto& s : f.symboles) {
      if (s.uuid != symbole) continue;
      for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
        if (b.broche->numero == numero) {
          const Pt a = b.p, e{b.p.x - 5'080'000, b.p.y};
          f.fils.push_back({nouvelUuid(), a, e});
          f.etiquettes.push_back({nouvelUuid(), nom, e, 0, TypeEtiquette::Locale});
        }
    }
}
ConceptionElec carteCapteur() {
  ConceptionElec c = nouvelleConception("Carte capteur");
  const std::string s = poserSymbole(c, 0, "Conn_01x04", ptMm(80, 60));
  for (const auto& f : c.feuilles)
    for (const auto& x : f.symboles)
      if (x.uuid == s)
        if (ComposantElec* k = c.composant(x.composant)) k->reference = "J3";
  const char* reseaux[] = {"CAN_H", "CAN_L", "RS485_A", "RS485_B"};
  for (int i = 0; i < 4; ++i) etiqueter(c, s, std::to_string(i + 1), reseaux[i]);
  return c;
}
prom::elec::ProjetElectrique machineX1() {
  prom::elec::ProjetElectrique pe = prom::elec::nouveauProjetElectrique("Machine");
  const std::size_t f = prom::elec::ajouterFolio(pe, prom::elec::TypeFolio::Commande, "Bornier");
  const std::string s1 = prom::elec::poserBorne(pe, f, ptMm(100, 50), 0, {});
  std::string bornier;
  for (const auto& x : pe.schema.feuilles[f].symboles)
    if (x.uuid == s1) bornier = x.composant;
  for (int i = 1; i < 5; ++i) prom::elec::poserBorne(pe, f, ptMm(100 + 10.16 * i, 50), 0, bornier);
  return pe;
}
// Serveur PLM simulé (tests/plm_simule.py) : son journal relève chaque connexion reçue
struct ServeurPlm {
  QProcess processus;
  QString journal, adresse;
  bool lance = false;
  QString raison, m_python;
  explicit ServeurPlm(const QString& dossier) {
    // Données d'essai : la variable PROMETHEE_DONNEES, sinon le dépôt vu depuis l'exécutable (build/app) ou le dossier
    // courant ; le programme d'accès en ligne doit être à côté de Prométhée (il n'est pas dans le paquet Windows)
    QString donnees = qEnvironmentVariable("PROMETHEE_DONNEES");
    for (const QString& essai : {QCoreApplication::applicationDirPath() + QStringLiteral("/../../tests/donnees"), QStringLiteral("tests/donnees")})
      if (donnees.isEmpty() && QFileInfo::exists(essai + QStringLiteral("/../plm_simule.py"))) donnees = essai;
    const QString script = QDir(donnees).filePath(QStringLiteral("../plm_simule.py"));
    QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python"));
    const bool programme = !QStandardPaths::findExecutable(QStringLiteral("promethee-en-ligne"), {QCoreApplication::applicationDirPath()}).isEmpty();
    if (python.isEmpty() || !QFile::exists(script) || !programme) {
      raison = python.isEmpty() ? QStringLiteral("Python absent") : !programme ? QStringLiteral("programme d'accès en ligne absent à côté de Prométhée") : QStringLiteral("tests/plm_simule.py absent");
      return;
    }
    m_python = python;
    journal = QDir(dossier).filePath(QStringLiteral("connexions.log"));
    const QString port = QDir(dossier).filePath(QStringLiteral("port.txt"));
    processus.start(python, {script, QStringLiteral("--journal"), journal, QStringLiteral("--fichier-port"), port, QStringLiteral("--jeton"), QStringLiteral("secret")});
    lance = attendreQue([&] { return !lire(port).empty(); }, 10000);
    if (lance) adresse = QStringLiteral("http://127.0.0.1:") + QString::fromStdString(lire(port)).trimmed();
    else raison = QStringLiteral("le serveur simulé ne démarre pas");
  }
  ~ServeurPlm() {
    if (processus.state() != QProcess::NotRunning) {
      processus.terminate();
      if (!processus.waitForFinished(5000)) processus.kill();
    }
  }
  int connexions() const {
    const std::string j = lire(journal);
    return static_cast<int>(std::count(j.begin(), j.end(), '\n'));
  }
};
void reglagesAcces(const QString& fichier, const QString& adresse, bool actif) {
  const QJsonObject plm{{QStringLiteral("actif"), actif}, {QStringLiteral("adaptateur"), QStringLiteral("simule")}, {QStringLiteral("adresse"), adresse},
                        {QStringLiteral("utilisateur"), QStringLiteral("evan")}, {QStringLiteral("jeton"), QStringLiteral("secret")}};
  const QJsonObject tickets{{QStringLiteral("actif"), actif}, {QStringLiteral("adaptateur"), QStringLiteral("tickets-simule")}, {QStringLiteral("adresse"), adresse},
                            {QStringLiteral("jeton"), QStringLiteral("secret")}};
  const QJsonObject o{{QStringLiteral("format"), QStringLiteral("promethee-acces-en-ligne")}, {QStringLiteral("version"), 1}, {QStringLiteral("actif"), actif},
                      {QStringLiteral("journal"), QStringLiteral("journal-connexions.csv")},
                      {QStringLiteral("fonctions"), QJsonObject{{QStringLiteral("plm"), plm}, {QStringLiteral("tickets"), tickets}}}};
  QFile f(fichier);
  if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(o).toJson());
}
}  // namespace

void essaisElectronique034L19(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  QTemporaryDir tmp;
  const QString racine = tmp.path();
  // Le serveur PLM simulé tourne pendant tout le lot ; l'accès en ligne reste désactivé jusqu'au dernier essai
  ServeurPlm serveur(racine);
  const QString reglages = QDir(racine).filePath(QStringLiteral("acces-en-ligne.json"));
  qputenv("PROMETHEE_ACCES_EN_LIGNE", reglages.toUtf8());
  reglagesAcces(reglages, serveur.adresse, false);

  // ======================================================================================================================
  // 19.4 · Bibliothèque d'équipe
  // ======================================================================================================================
  {
    const QString equipe = QDir(racine).filePath(QStringLiteral("equipe"));
    QDir().mkpath(equipe);
    const QString fichier = QDir(equipe).filePath(QStringLiteral("equipe.prombib"));
    const ContenuBibliotheque bib = bibliothequeSemee(200);
    prom::ecrireFichierAtomique(prom::cheminFichier(fichier.toStdString()), texteContenuBibliotheque(bib));
    // Deux projets de l'équipe citent le doublon ; le projet ouvert, l'exemple, non
    QStringList projets;
    for (int i = 0; i < 2; ++i) {
      prom::Projet p = prom::projetVide();
      p.nom = "Carte " + std::to_string(i + 1);
      p.electronique.push_back(exempleElectronique());
      ComposantElec* r1 = parRef(p.electronique[0], "R1");
      r1->article = "doublon-3";
      p.electronique[0].articles["doublon-3"] = bib.articles[197];
      projets << QDir(equipe).filePath(QStringLiteral("carte%1.prom").arg(i + 1));
      prom::ecrireProjet(p, projets.back().toStdString());
    }
    prom::Projet ouvert = prom::projetVide();
    ouvert.sansCarte = true;
    ouvert.nom = "Bibliothèque d'équipe";
    ouvert.electronique = {exempleElectronique()};
    doc->creer(ouvert);
    f.montrerElectronique(0, AtelierElectronique::Schema);
    attendre(400);
    AtelierElectronique* a = f.atelierElectronique();
    if (!a) {
      verifier(false, "034 L19 : l'atelier électronique s'ouvre", {});
      return;
    }
    a->executerAction("bibliotheque.equipe");
    attendre(200);
    FenetreBibliothequeEquipe* w = a->bibliothequeEquipe();
    w->choisir(equipe, fichier);
    // Demandes : champ imposé, dépôt, suivi, notification quand l'article publié apparaît
    imposerChamps(equipe.toStdString(), {"boitier"});
    w->choisir(equipe, fichier);
    DemandeArticle d;
    d.reference = "TPS62130RGTR";
    d.fabricant = "Texas Instruments";
    QString erreur;
    const bool refusee = !w->deposer(d, &erreur);
    d.champs["boitier"] = "VQFN-16";
    const bool deposee = w->deposer(d);
    const auto liste = w->demandes();
    const std::string uuid = liste.empty() ? std::string() : liste.back().uuid;
    w->suivre(QString::fromStdString(uuid), EtatDemande::Prise);
    w->suivre(QString::fromStdString(uuid), EtatDemande::Publiee, QStringLiteral("ti-tps62130"));
    // Le bibliothécaire publie l'article : la surveillance du dossier le voit
    ContenuBibliotheque publiee = bib;
    Article tps;
    tps.id = "ti-tps62130";
    tps.mpn = "TPS62130RGTR";
    tps.symbole = "R";
    publiee.articles.push_back(tps);
    prom::ecrireFichierAtomique(prom::cheminFichier(fichier.toStdString()), texteContenuBibliotheque(publiee));
    const bool notifiee = attendreQue([&] { return !w->notifications().isEmpty(); }, 5000);
    verifier(refusee && erreur.contains(QStringLiteral("boitier")) && deposee && liste.size() == 1 && notifiee && w->notifications().front().contains(QStringLiteral("TPS62130RGTR")) &&
                 QFile::exists(QDir(equipe).filePath(QStringLiteral("demandes/") + QString::fromStdString(uuid) + QStringLiteral(".json"))),
             "034 L19 · 19.4 : demande d'article : refusée sans le champ imposé, déposée en fichier, suivie puis publiée ; le demandeur est notifié dès que l'article apparaît",
             erreur.toStdString() + ", " + std::to_string(w->notifications().size()) + " notification(s)");
    // Santé : sur son fil, trois doublons repérés malgré le tiret
    prom::ecrireFichierAtomique(prom::cheminFichier(fichier.toStdString()), texteContenuBibliotheque(bib));
    attendre(200);
    w->lancerSante();
    const bool surFil = w->santeEnCours();
    const bool fini = attendreQue([&] { return !w->santeEnCours(); }, 60000);
    const auto& s = w->sante();
    std::set<std::string> paires;
    for (const auto& x : s.doublons) paires.insert(x.articles[0] < x.articles[1] ? x.articles[0] + "|" + x.articles[1] : x.articles[1] + "|" + x.articles[0]);
    verifier(surFil && fini && s.articles == 200 && s.doublons.size() == 3 && paires.count("doublon-3|r-3") && paires.count("doublon-120|r-120") && paires.count("doublon-50|r-50"),
             "034 L19 · 19.4 : santé de la bibliothèque de 200 articles, sur son fil : les trois doublons semés repérés, référence écrite avec et sans tiret",
             std::to_string(s.doublons.size()) + " doublon(s), " + std::to_string(s.problemes.size()) + " problème(s)");
    // Interruptible : une grande bibliothèque arrêtée en route
    const QString grande = QDir(equipe).filePath(QStringLiteral("grande.prombib"));
    prom::ecrireFichierAtomique(prom::cheminFichier(grande.toStdString()), texteContenuBibliotheque(bibliothequeSemee(6000)));
    w->choisir(equipe, grande);
    w->lancerSante();
    // Aussitôt : sur une machine rapide, 6 000 articles s'analysent en moins de 30 ms ; l'arrêt est lu avant chaque
    // article, sur le fil de l'analyse
    w->interrompreSante();
    attendreQue([&] { return !w->santeEnCours(); }, 60000);
    verifier(w->sante().interrompu, "034 L19 · 19.4 : l'analyse de santé s'interrompt", std::to_string(w->sante().articles) + " article(s)");
    w->choisir(equipe, fichier);
    // Comparer, fusionner (les deux projets repointés), annuler, refaire
    w->comparer(QStringLiteral("r-3"), QStringLiteral("doublon-3"));
    QTableWidget* t = w->tableComparaison();
    const int lignesComparees = t->rowCount();
    bool referenceDiffere = false, mpnDiffere = false;
    for (int r = 0; r < t->rowCount(); ++r) {
      if (t->item(r, 0)->text() == QStringLiteral("Référence normalisée")) referenceDiffere = t->item(r, 1)->text() != t->item(r, 2)->text();
      if (t->item(r, 0)->text() == QStringLiteral("Référence fabricant")) mpnDiffere = t->item(r, 1)->text() != t->item(r, 2)->text();
    }
    const bool fusion = w->fusionner(QStringLiteral("r-3"), QStringLiteral("doublon-3"));
    auto articleDeR1 = [&](int i) {
      const prom::Projet p = prom::lireProjet(projets[i].toStdString());
      for (const auto& k : p.electronique[0].composants)
        if (k.reference == "R1") return k.article;
      return std::string();
    };
    const bool repointes = articleDeR1(0) == "r-3" && articleDeR1(1) == "r-3" && lireFichierBibliotheque(fichier.toStdString()).articles.size() == 199;
    w->pile()->undo();
    const bool annulee = articleDeR1(0) == "doublon-3" && articleDeR1(1) == "doublon-3" && lireFichierBibliotheque(fichier.toStdString()).articles.size() == 200;
    w->pile()->redo();
    const bool refaite = articleDeR1(0) == "r-3" && articleDeR1(1) == "r-3";
    const std::string journal = lire(QDir(equipe).filePath(QStringLiteral("journal-bibliotheque.csv")));
    verifier(lignesComparees > 8 && mpnDiffere && !referenceDiffere && fusion && repointes && annulee && refaite && journal.find(";fusion;") != std::string::npos && journal.find(";annulation;") != std::string::npos,
             "034 L19 · 19.4 : doublons comparés côte à côte ; la fusion repointe les deux projets vers l'article gardé, s'inscrit au journal, s'annule et se refait",
             std::to_string(lignesComparees) + " ligne(s) comparées" + (referenceDiffere ? ", références normalisées différentes" : "") + (fusion ? "" : ", fusion refusée") +
                 (repointes ? "" : ", non repointés") + (annulee ? "" : ", annulation ratée") + (refaite ? "" : ", refaire raté") +
                 (journal.find(";fusion;") != std::string::npos ? "" : ", journal sans fusion") + (journal.find(";annulation;") != std::string::npos ? "" : ", journal sans annulation"));
    // Correction annulable
    const bool corrige = w->corriger(QStringLiteral("r-50"), QStringLiteral("modele3D"), QStringLiteral("modeles/R_0603.step"));
    auto modele = [&] {
      for (const auto& x : lireFichierBibliotheque(fichier.toStdString()).articles)
        if (x.id == "r-50") return x.modele3D;
      return std::string("?");
    };
    const bool applique = modele() == "modeles/R_0603.step";
    w->pile()->undo();
    verifier(corrige && applique && modele().empty(), "034 L19 · 19.4 : un champ corrigé depuis la comparaison, annulable", modele());
    w->close();
  }

  // ======================================================================================================================
  // 19.7 · Achats et tableau de bord de la nomenclature
  // ======================================================================================================================
  {
    ConceptionElec c = nouvelleConception("Quatre résistances");
    Article art;
    art.id = "yageo-10k";
    art.nom = "10 kΩ 0603";
    art.symbole = "R";
    art.empreinte = "R_0603";
    art.valeur = "10k";
    art.fabricant = "Yageo";
    art.mpn = "RC0603FR-0710KL";
    OffreArticle bobine;
    bobine.distributeur = "LCSC";
    bobine.reference = "C25804";
    bobine.conditionnement = 5000;
    bobine.prix = {{1, 0.002}};
    OffreArticle bande;
    bande.distributeur = "Mouser";
    bande.reference = "603-RC0603FR-0710KL";
    bande.prix = {{1, 0.01}};
    art.offres = {bande, bobine};
    c.articles[art.id] = art;
    for (int i = 0; i < 4; ++i) {
      poserSymbole(c, 0, "R", ptMm(40 + 15 * i, 40));
      ComposantElec& k = c.composants.back();
      k.reference = "R" + std::to_string(i + 1);
      k.valeur = "10k";
      k.empreinte = "R_0603";
      k.article = art.id;
      k.fabricant = art.fabricant;
      k.mpn = art.mpn;
    }
    prom::Projet p = prom::projetVide();
    p.sansCarte = true;
    p.nom = "Achats";
    p.electronique = {c};
    doc->creer(p);
    f.montrerElectronique(0, AtelierElectronique::Schema);
    attendre(300);
    AtelierElectronique* a = f.atelierElectronique();
    a->executerAction("fabrication.achats");
    attendre(300);
    FenetreOutilsConception* o = a->outilsConception();
    const ListeAchat& l = o->calculerAchats(250);
    const auto it = std::find_if(l.lignes.begin(), l.lignes.end(), [](const ChoixLigne& x) { return x.cle == "RC0603FR-0710KL"; });
    const bool bobineChoisie = it != l.lignes.end() && it->besoin == 1020 && it->quantite == 5000 && it->conditionnement == "bobine" && std::abs(it->cout - 10.0) < 1e-9;
    verifier(bobineChoisie && o->rapport(QStringLiteral("achats")).contains(QStringLiteral("bobine")),
             "034 L19 · 19.7 : 250 cartes à quatre résistances identiques, 2 % de pertes : la bobine de 5 000 à 0,002 € (10 €) préférée à 1 020 pièces en bande coupée à 0,01 € (10,20 €)",
             it == l.lignes.end() ? std::string("ligne absente") : std::to_string(it->besoin) + " pièces, " + std::to_string(it->quantite) + " achetées en " + it->conditionnement);
    // Tableau de bord : une commande ne recalcule que la ligne touchée
    const int lignes = static_cast<int>(o->tableauBord().lignes.size());
    doc->modifierConception(0, [](ConceptionElec& x) {
      for (auto& k : x.composants)
        if (k.reference == "R4") k.parametres["rohs"] = "oui";
    });
    attendre(150);
    const int recalculees = o->tableauBord().recalculees;
    const QString page = QDir(racine).filePath(QStringLiteral("nomenclature.html"));
    const QString panier = QDir(racine).filePath(QStringLiteral("panier-lcsc.csv"));
    o->exporterPageAcheteurs(page);
    o->exporterPanier(QStringLiteral("LCSC"), panier);
    const std::string html = lire(page), csv = lire(panier);
    verifier(recalculees >= 1 && recalculees <= 2 && recalculees < std::max(2, lignes + 1) && html.find("<svg") != std::string::npos && html.find(" / 10</p>") != std::string::npos &&
                 csv.rfind("LCSC Part Number,Manufacture Part Number,Quantity", 0) == 0 && csv.find("C25804,RC0603FR-0710KL,5000") != std::string::npos,
             "034 L19 · 19.7 : tableau de bord recalculé sur les seules lignes touchées, page autonome pour les acheteurs (graphiques en SVG), panier LCSC au gabarit de colonnes",
             std::to_string(recalculees) + " ligne(s) recalculée(s) sur " + std::to_string(o->tableauBord().lignes.size()));
    o->close();
  }

  // ======================================================================================================================
  // 19.10 à 19.13 · Faisceau
  // ======================================================================================================================
  {
    prom::Projet p = prom::projetVide();
    p.sansCarte = true;
    p.nom = "Machine et carte capteur";
    p.electronique = {carteCapteur()};
    p.electrique = {machineX1()};
    doc->creer(p);
    f.montrerElectrique(0, AtelierElectrique::Faisceau);
    attendre(400);
    AtelierElectrique* e = f.atelierElectrique();
    if (!e) {
      verifier(false, "034 L19 : l'atelier électrique s'ouvre", {});
      return;
    }
    FenetreCablage* w = e->cablage();
    // Bibliothèque de l'atelier complétée par un fichier de données : fil de 1,6 mm et câble d'essai de 25 mΩ/m
    const QString fichierCables = QDir(racine).filePath(QStringLiteral("cables.json"));
    prom::ecrireFichierAtomique(prom::cheminFichier(fichierCables.toStdString()),
                                R"({"format":"promethee-cables","version":2,"cables":[{"designation":"FLRY-A 0,75","section":0.75,"diametreConducteur":1.6,"conducteurs":[{"repere":"1","couleur":"BU"}]},
                                    {"designation":"Essai 2x0,75","section":0.75,"resistanceLineique":25,"conducteurs":[{"repere":"1","couleur":"BN"},{"repere":"2","couleur":"BU"}]}]})");
    w->chargerBibliotheque(fichierCables, false);
    // ----- 19.10 : câble blindé à deux paires de J3 au bornier X1 -----
    w->nouveauFaisceau(QStringLiteral("Liaison capteur"));
    const int pas0 = doc->positionHistorique();
    w->ajouterConnecteurCarte(0, QStringLiteral("J3"), QStringLiteral("43025-0400"));
    w->ajouterConnecteurSchema(QStringLiteral("X1"));
    const QString w1 = w->poserCable(QStringLiteral("LiYCY-TP 2x2x0,25"), QStringLiteral("J3"), QStringLiteral("X1"));
    const int servies = w->affecterContacts();
    const auto h = w->courant();
    const auto liste = h ? prom::elec::listeFils(*h, w->bibliotheque()) : std::vector<prom::elec::LigneFil>{};
    const char* reseaux[] = {"CAN_H", "CAN_L", "RS485_A", "RS485_B"};
    int justes = 0, drains = 0;
    for (const auto& x : liste) {
      if (x.drain) {
        drains += x.vers == "X1:5" && x.de.empty();
        continue;
      }
      const int i = std::atoi(x.numero.c_str()) - 1;
      if (i >= 0 && i < 4 && x.de == "J3:" + std::to_string(i + 1) && x.reseau == reseaux[i] && x.contactDe == "43030-0007" && x.contactVers == "Embout 0,25 mm²") ++justes;
    }
    verifier(w1 == QStringLiteral("W1") && servies == 9 && liste.size() == 5 && justes == 4 && drains == 1 && doc->positionHistorique() == pas0 + 4 && w->tableFils()->rowCount() == 5,
             "034 L19 · 19.10 : câble blindé à deux paires de J3 au bornier X1 : quatre fils et un drain dans la liste, les quatre aux réseaux de leurs broches de J3, chacun serti du contact qui "
             "accepte sa section",
             std::to_string(justes) + " fil(s) justes, " + std::to_string(drains) + " drain, " + std::to_string(servies) + " cavité(s) servie(s)");
    doc->annuler();
    const bool sansContacts = w->courant() && w->courant()->connecteurs[0].cavites[0].contact.empty();
    doc->retablir();
    const int folio = w->ajouterFolio();
    const prom::elec::ProjetElectrique* pe = e->projetCourant();
    const bool folioCablage = pe && folio >= 0 && pe->folio(pe->schema.feuilles[static_cast<std::size_t>(folio)].uuid).type == prom::elec::TypeFolio::Cablage;
    attendre(200);
    const QImage image = e->vueSchema() ? e->vueSchema()->grab().toImage() : QImage();
    verifier(sansContacts && folioCablage && !image.isNull(), "034 L19 · 19.10 : contacts annulés puis rétablis ; folio « Câblage de faisceau » dessiné d'office depuis le faisceau", {});
    // ----- 19.11 : sept fils de 1,6 mm ; planche à l'échelle 1 -----
    w->nouveauFaisceau(QStringLiteral("Sept fils"));
    w->ajouterConnecteurVolant(QStringLiteral("A"), {}, 7);
    w->ajouterConnecteurVolant(QStringLiteral("B"), {}, 7);
    for (int i = 1; i <= 7; ++i) w->poserFil(0.75, QStringLiteral("BU"), QStringLiteral("A"), QString::number(i), QStringLiteral("B"), QString::number(i));
    w->regler(1.0, 50);
    w->dessinerPlanche();
    w->reglerTroncon(0, 500, false);
    const auto h7 = w->courant();
    const auto occ = h7 ? prom::elec::occupationTroncons(*h7, w->bibliotheque()) : std::vector<prom::elec::OccupationTroncon>{};
    const QString svg = QDir(racine).filePath(QStringLiteral("planche.svg"));
    w->exporterPlanche(svg, 1);
    const std::string texte = lire(svg);
    std::smatch m;
    double cote = 0, largeurMm = 0, largeurVue = 0;
    if (std::regex_search(texte, m, std::regex("width=\"([0-9.]+)mm\" height=\"[0-9.]+mm\" viewBox=\"0 0 ([0-9.]+) "))) largeurMm = std::stod(m[1]), largeurVue = std::stod(m[2]);
    if (std::regex_search(texte, m, std::regex("class=\"cote\" data-longueur=\"500\" x1=\"([-0-9.]+)\" y1=\"([-0-9.]+)\" x2=\"([-0-9.]+)\" y2=\"([-0-9.]+)\"")))
      cote = std::hypot(std::stod(m[3]) - std::stod(m[1]), std::stod(m[4]) - std::stod(m[2]));
    const int pages = w->exporterPages(QDir(racine).filePath(QStringLiteral("planche.pdf")), QStringLiteral("A4"));
    const LecturePdf lp = lirePdf(lire(QDir(racine).filePath(QStringLiteral("planche.pdf"))));
    // La planche de l'atelier : un nœud rangé à la souris revient au faisceau
    pe = e->projetCourant();
    bool planche = false;
    for (const auto& fx : pe->faisceaux) planche = planche || (h7 && fx.uuid == h7->uuid && fx.noeuds.size() == 2);
    doc->modifier([&](prom::Projet& x) {
      for (auto& fx : x.electrique[0].faisceaux)
        if (h7 && fx.uuid == h7->uuid) fx.noeuds[1].p = ptMm(fx.noeuds[1].p.x / 1e6 + 40, fx.noeuds[1].p.y / 1e6 + 30);
    });
    const int repris = w->reprendrePlanche();
    verifier(!occ.empty() && std::abs(occ[0].diametre - 4.8) < 1e-9 && std::abs(cote - 500) < 1e-6 && largeurMm > 0 && std::abs(largeurMm - largeurVue) < 1e-6 && lp.ok && lp.pages == pages && pages >= 1 &&
                 planche && repris >= 1,
             "034 L19 · 19.11 : sept fils de 1,6 mm, foisonnement à 1 : tronçon de 4,8 mm ; planche à l'échelle 1 : la cote de 500 mm mesure 500 mm ; pages assemblées ; nœud rangé à la souris repris",
             "Ø " + std::to_string(occ.empty() ? 0 : occ[0].diametre) + " mm, cote " + std::to_string(cote) + " mm, " + std::to_string(pages) + " page(s)");
    // Le connecteur A dessiné d'après son modèle 3D : projection en lignes cachées, tournée et répétée
    {
      const QString step = QDir(racine).filePath(QStringLiteral("connecteur.step"));
      prom::exporterStep({prom::PieceNommee{"connecteur", BRepPrimAPI_MakeBox(20, 10, 8).Shape()}}, step.toStdString());
      const bool dessine = w->dessinerConnecteur(QStringLiteral("A"), step, QStringLiteral("face"), 90, 2);
      const auto hd = w->courant();
      const prom::elec::ConnecteurCablage* ka = hd ? hd->connecteur("A") : nullptr;
      w->exporterPlanche(svg, 1);
      const std::string avecDessin = lire(svg);
      verifier(dessine && ka && ka->dessin && ka->dessin->vus.size() >= 4 && std::abs(ka->dessin->largeur - 20) < 1e-6 && ka->dessin->cavites.size() == 7 && ka->dessin->repetitions == 2 &&
                   avecDessin.find("class=\"dessinConnecteur\"") != std::string::npos,
               "034 L19 · 19.11 : le symbole du connecteur remplacé par la projection en lignes cachées de son modèle 3D, tournée et répétée, numéros de cavités compris",
               ka && ka->dessin ? std::to_string(ka->dessin->vus.size()) + " trait(s) vus, " + std::to_string(ka->dessin->largeur) + " mm de large" : std::string("aucun dessin"));
    }
    // ----- 19.12 : câble de deux fils de 0,75 mm² routé en 3D sur 4 m, 6 A sous 12 V -----
    w->nouveauFaisceau(QStringLiteral("Alimentation"));
    w->ajouterConnecteurVolant(QStringLiteral("S"), {}, 2);
    w->ajouterConnecteurVolant(QStringLiteral("L"), {}, 2);
    w->poserCable(QStringLiteral("Essai 2x0,75"), QStringLiteral("S"), QStringLiteral("L"));
    const double route = w->routerEn3D(QStringLiteral("W1"), {{0, 0, 0}, {1500, 0, 0}, {4000, 0, 0}}, 50);
    prom::elec::CircuitContinu k;
    k.tension = 12;
    k.aller = {"1"};
    k.retour = {"2"};
    k.charge = {"moteur", 6, 0, 0};
    k.chuteMax = 5;
    const prom::elec::BilanContinu b = w->circuit(k);
    const auto ha = w->courant();
    const auto fils = ha ? prom::elec::listeFils(*ha, w->bibliotheque()) : std::vector<prom::elec::LigneFil>{};
    const bool quatre = fils.size() == 2 && std::abs(fils[0].longueur - 4.0) < 1e-9 && fils[0].provenance == "3D";
    verifier(std::abs(route - 4000) < 1e-6 && quatre && std::abs(b.chute - 1.2) < 1e-9 && std::abs(b.pourcent - 10) < 1e-9 && b.depasse && !b.alertes.empty(),
             "034 L19 · 19.12 : câble à deux fils de 0,75 mm² (25 mΩ/m) routé en 3D sur 4 m, charge de 6 A sous 12 V : 4 m dans la liste de fils, chute de 1,2 V (10 %) signalée au-delà de 5 %",
             b.alertes.empty() ? std::string() : b.alertes[0]);
    // Faisceau tracé en 3D, mis à plat en planche : topologie et longueurs gardées
    {
      const QString fichier = QDir(racine).filePath(QStringLiteral("branches.json"));
      prom::ecrireFichierAtomique(prom::cheminFichier(fichier.toStdString()),
                                  R"({"branches":[{"a":"S","b":"N1","points":[[0,0,0],[1000,0,0]]},{"a":"N1","b":"L","rayon":30,"points":[[1000,0,0],[1000,1500,0],[1000,1500,1500]]}]})");
      const int n = w->mettreAPlat(fichier);
      const auto hp = w->courant();
      const auto lp = hp ? prom::elec::listeFils(*hp, w->bibliotheque()) : std::vector<prom::elec::LigneFil>{};
      const double attendu = (1000 + 3000 - 60 + 30 * 3.14159265358979 / 2 + 2 * 50) / 1000;
      verifier(n == 2 && hp && hp->troncons.size() == 2 && lp.size() == 2 && std::abs(lp[0].longueur - attendu) < 1e-9 && lp[0].provenance == "3D" && pe && !e->projetCourant()->faisceaux.empty(),
               "034 L19 · 19.12 : faisceau tracé en 3D mis à plat en planche, topologie et longueurs gardées", std::to_string(lp.empty() ? 0 : lp[0].longueur) + " m");
    }
    // ----- 19.13 : faisceau d'essai de 24 fils et 48 sertissages -----
    w->nouveauFaisceau(QStringLiteral("Essai 24 fils"));
    for (const char* x : {"A1", "A2", "A3", "B1", "B2", "B3"}) w->ajouterConnecteurVolant(QString::fromLatin1(x), QStringLiteral("43025-0800"), 8);
    for (int c = 1; c <= 3; ++c)
      for (int i = 1; i <= 8; ++i)
        w->poserFil(0.5, i % 2 ? QStringLiteral("BU") : QStringLiteral("WH"), QStringLiteral("A%1").arg(c), QString::number(i), QStringLiteral("B%1").arg(c), QString::number(i));
    const int sertis = w->affecterContacts();
    w->dessinerPlanche();
    w->poserProtection(0, QStringLiteral("Gaine tressée 3-7"), 0, 0);
    w->poserProtection(1, QStringLiteral("Gaine tressée 3-7"), 0, 0);
    w->poserProtection(2, QStringLiteral("Étiquette 25 × 10"), 50, 75, QStringLiteral("ESSAI"));
    const QString temps = QDir(racine).filePath(QStringLiteral("temps.json"));
    prom::ecrireFichierAtomique(prom::cheminFichier(temps.toStdString()), R"({"coupe":12,"sertissage":20,"gaine":45,"controle":6,"etiquette":15,"tauxHoraire":55})");
    const prom::elec::DevisFaisceau dv = w->devis(temps, QDir(racine).filePath(QStringLiteral("devis.pdf")), QDir(racine).filePath(QStringLiteral("devis.csv")));
    const double main = 24 * 12 + 48 * 20 + 2 * 45 + 1 * 15 + 24 * 6;
    const QString xlsx = QDir(racine).filePath(QStringLiteral("faisceau.xlsx"));
    w->exporterClasseur(xlsx);
    // Classeur relu par LibreOffice Calc : chaque feuille convertie en CSV
    QString relu = QStringLiteral("LibreOffice absent : relecture ignorée");
    bool calc = true;
    const QString soffice = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    if (!soffice.isEmpty()) {
      QProcess lo;
      QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
      env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));  // noms de feuilles accentués dans les noms de fichiers
      lo.setProcessEnvironment(env);
      const QString sortie = QDir(racine).filePath(QStringLiteral("calc"));
      QDir().mkpath(sortie);
      lo.start(soffice, {QStringLiteral("--headless"), QStringLiteral("-env:UserInstallation=file://") + QDir(racine).filePath(QStringLiteral("profil-lo")), QStringLiteral("--convert-to"),
                         QStringLiteral("csv:Text - txt - csv (StarCalc):59,34,76,1,,0,false,true,false,false,false,-1"), QStringLiteral("--outdir"), sortie, xlsx});
      lo.waitForFinished(180000);
      const QStringList csvs = QDir(sortie).entryList({QStringLiteral("*.csv")}, QDir::Files);
      std::string tout;
      for (const QString& x : csvs) tout += lire(QDir(sortie).filePath(x));
      calc = csvs.size() >= 4 && tout.find("Bulle;Référence") != std::string::npos && tout.find("43030-0007;contact à sertir Molex;48") != std::string::npos;
      relu = QStringLiteral("%1 feuille(s) relue(s) par LibreOffice (%2)").arg(csvs.size()).arg(csvs.join(QStringLiteral(", ")));
    }
    const LecturePdf plan = lirePdf(lire(QDir(racine).filePath(QStringLiteral("devis.pdf"))));
    w->exporterPlan(QDir(racine).filePath(QStringLiteral("plan.pdf")), QStringLiteral("B"));
    const LecturePdf planFab = lirePdf(lire(QDir(racine).filePath(QStringLiteral("plan.pdf"))));
    w->exporterMachine(0, QDir(racine).filePath(QStringLiteral("komax.csv")));
    const std::string komax = lire(QDir(racine).filePath(QStringLiteral("komax.csv")));
    verifier(sertis == 48 && dv.fils == 24 && dv.sertissages == 48 && std::abs(dv.secondes - main) < 1e-9 && std::abs(dv.mainOeuvre - main / 3600 * 55) < 1e-9 && calc && plan.ok && planFab.ok &&
                 planFab.pages >= 4 && std::count(komax.begin(), komax.end(), '\n') == 25 && w->verifier().empty(),
             "034 L19 · 19.13 : faisceau d'essai de 24 fils et 48 sertissages : main-d'œuvre du devis égale à la somme à la main des temps unitaires, classeur relu par LibreOffice Calc",
             std::to_string(dv.secondes) + " s pour " + std::to_string(main) + " s ; " + relu.toStdString());
    w->close();
  }

  // ======================================================================================================================
  // 19.14 · Vers le PLM
  // ======================================================================================================================
  {
    prom::Projet p = prom::projetVide();
    p.sansCarte = true;
    p.nom = "Capteur de pression";
    p.electronique = {exempleElectronique()};
    doc->creer(p);
    f.montrerElectronique(0, AtelierElectronique::Schema);
    attendre(300);
    AtelierElectronique* a = f.atelierElectronique();
    a->executerAction("fabrication.plm");
    attendre(300);
    FenetreOutilsConception* o = a->outilsConception();
    const QString equipe = QDir(racine).filePath(QStringLiteral("equipe-plm"));
    QDir().mkpath(equipe);
    const QString depot = o->deposerPlm(equipe, QStringLiteral("A"));
    const prom::LectureDepotPlm l = prom::lireDepotPlm(depot.toStdString());
    const bool step = std::find(l.fichiers.begin(), l.fichiers.end(), "modeles/carte1.step") != l.fichiers.end() && lire(depot + QStringLiteral("/modeles/carte1.step")).find("AP242") != std::string::npos;
    verifier(!depot.isEmpty() && l.ok && l.revision == "A" && l.articles > 3 && step,
             "034 L19 · 19.14 : publication vers le PLM : nomenclature commune, structure PLMXML et modèle STEP AP242 déposés sous « Vers le PLM » du dossier d'équipe, relus",
             depot.toStdString());
    if (!serveur.lance) {
      verifier(true, "034 L19 · 19.14 : serveur PLM simulé", "ignoré : " + serveur.raison.toStdString());
    } else {
      // Sans activation : le programme d'accès en ligne refuse, et rien n'a touché le serveur de tout le lot
      QString sortie;
      const int refus = o->synchroniserPlm(QStringLiteral("deposer"), &sortie);
      const int aucune = serveur.connexions();
      verifier(refus == 3 && sortie.contains(QStringLiteral("Rien n'est parti")) && aucune == 0,
               "034 L19 · 19.14 : accès en ligne désactivé : aucune connexion relevée par le serveur PLM simulé pendant tous les essais du lot", std::to_string(aucune) + " connexion(s)");
      // Activé : dépôt, réservation, restitution en révision B
      reglagesAcces(reglages, serveur.adresse, true);
      const int depose = o->synchroniserPlm(QStringLiteral("deposer"));
      const int reserve = o->synchroniserPlm(QStringLiteral("reserver"));
      const QString suivante = o->deposerPlm(equipe, QStringLiteral("B"), false);
      const int restitue = o->synchroniserPlm(QStringLiteral("restituer"), &sortie);
      const auto etats = prom::lireEtatsPlm(equipe.toStdString());
      const bool b = !etats.empty() && etats.back().revision == "B" && etats.back().etat == "restitué";
      verifier(depose == 0 && reserve == 0 && !suivante.isEmpty() && restitue == 0 && b && serveur.connexions() == 3,
               "034 L19 · 19.14 : serveur PLM simulé : dossier déposé, réservé puis restitué en révision B quand l'accès est activé", sortie.toStdString());
      // Tickets dans les deux sens : un constat de revue devient un ticket ; clos chez le gestionnaire, il se clôt ici
      std::string constat;
      doc->modifierConception(0, [&](ConceptionElec& x) {
        const prom::RevueFormelle rv = prom::ouvrirRevue(x, "Revue de la révision B", "2026-10-25");
        constat = prom::ajouterConstat(x, rv.numero, "R3", "valeur à confirmer par la mesure", "Evan", "2026-10-25");
      });
      const int avant = o->synchroniserTickets();
      const auto locaux = prom::lireTickets(equipe.toStdString(), false);
      const QString distant = locaux.empty() ? QString() : QString::fromStdString(locaux.front().distant);
      QProcess clore;
      clore.start(serveur.m_python,
                  {QStringLiteral("-c"), QStringLiteral("import sys,urllib.request as u; u.urlopen(u.Request(sys.argv[1], data=b'{}', method='POST', headers={'Authorization': 'Bearer secret'})).read()"),
                   serveur.adresse + QStringLiteral("/api/tickets/") + distant + QStringLiteral("/clore")});
      clore.waitForFinished(20000);
      const int pas = doc->positionHistorique();
      const int clos = o->synchroniserTickets();
      bool closIci = false;
      for (const auto& rv : prom::revuesDe(doc->projet().electronique[0]))
        for (const auto& k : rv.constats) closIci = closIci || (k.id == constat && k.etat == "clos");
      verifier(avant == 0 && !distant.isEmpty() && clos == 1 && closIci && doc->positionHistorique() == pas + 1,
               "034 L19 · 19.14 : tickets dans les deux sens : le constat ouvert part au gestionnaire, clos là-bas il se clôt dans la revue, en un pas", distant.toStdString());
      reglagesAcces(reglages, serveur.adresse, false);
    }
    o->close();
  }
  qunsetenv("PROMETHEE_ACCES_EN_LIGNE");
}

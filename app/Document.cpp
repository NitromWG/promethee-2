// Prométhée : le document ouvert. Licence GPL-3.0-only.
#include "Document.hpp"
#include "ChienDeGarde.hpp"
#include "promethee/synchronisation.hpp"
#include "promethee/electronique.hpp"
#include "promethee/structure.hpp"

#include <algorithm>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QUuid>

#include "Journal.hpp"
#include "Recuperation.hpp"

namespace {
std::string nomFichier(const QString& chemin) { return QFileInfo(chemin).fileName().toStdString(); }
// Clé d'un fichier, la même quel que soit le chemin qui y mène : dossier réel + nom (casse ignorée sous Windows).
QString cleFichier(const QString& chemin) {
  const QFileInfo f(chemin);
  QString dossier = QFileInfo(f.absolutePath()).canonicalFilePath();
  if (dossier.isEmpty()) dossier = QDir::cleanPath(f.absolutePath());
  QString cle = dossier + QLatin1Char('/') + f.fileName();
#ifdef _WIN32
  cle = cle.toLower();
#endif
  return cle;
}
}  // namespace

// Pas d'historique : instantané du projet entier (mécanique, ajout ou retrait de conceptions, import), ou commande
// sur une seule conception électronique (A2) ; chacun porte un nom lisible et un identifiant d'état
struct Document::Pas {
  QString nom;
  std::uint64_t id = 0;
  // Projet entier : l'état à rendre (avant le pas dans la pile d'annulation, après lui dans celle de rétablissement)
  std::string etat;
  // Conception : son identité, la commande qui sait s'appliquer et se défaire, la clé et l'heure de fusion
  std::string conception;
  std::unique_ptr<prom::ecad::Commande> commande;
  QString fusion;
  qint64 quand = 0;
  std::vector<std::string> selection;  // sélection de l'atelier avant le pas (conception seulement)
  std::size_t octets() const { return commande ? commande->octets() : etat.size() + 64; }
};

Document::~Document() = default;

Document::Document(QObject* parent) : QObject(parent), m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
  // Au lancement : un projet vierge (sans carte), non modifié. L'exemple s'ouvre à la demande.
  m_projet = prom::projetVide();
  m_instantane.reset();
  m_projet.sansCarte = true;
  m_base = m_prochainPas++;
  m_enregistre = etatCourant();
  recalculer();
}

void Document::lacherFichier() {
  m_verrou.reset();
  m_disque = {};
  m_avertissements.clear();
}

std::unique_ptr<QLockFile> Document::verrouPour(const QString& chemin, bool ignorer) const {
  const QString dossier = QDir(Recuperation::dossierDonnees()).filePath(QStringLiteral("verrous"));
  if (!QDir().mkpath(dossier)) return nullptr;
  const QByteArray cle = QCryptographicHash::hash(cleFichier(chemin).toUtf8(), QCryptographicHash::Sha256).toHex().left(32);
  auto v = std::make_unique<QLockFile>(QDir(dossier).filePath(QString::fromLatin1(cle) + QStringLiteral(".verrou")));
  v->setStaleLockTime(0);  // périmé seulement si le processus qui le tient n'existe plus
  if (v->tryLock(0)) return v;
  if (v->error() == QLockFile::LockFailedError && !ignorer) {
    qint64 pid = 0;
    QString hote, appli;
    v->getLockInfo(&pid, &hote, &appli);
    throw FichierDejaOuvert("« " + nomFichier(chemin) + " » est déjà ouvert dans une autre fenêtre de Prométhée (processus " + std::to_string(pid) + ").", pid);
  }
  return nullptr;
}

std::string Document::contenu() const { return prom::versJson(m_projet).dump(2) + "\n"; }

void Document::charger(const prom::Projet& p) {
  lacherFichier();
  m_projet = p;
  m_instantane.reset();
  prom::assurerIdentifiantsPieces(m_projet);
  prom::assurerReperes(m_projet);
  m_chemin.clear();
  oublierHistorique();
  m_selection = {};
  emit projetRemplace();
  emit projetCharge();
  recalculer();
  m_enregistre = etatCourant();
  emit change();
  emit etatChange();
}

// Identifiant de l'état du projet : le dernier pas fait, ou la base posée quand l'historique a été vidé
std::uint64_t Document::etatCourant() const { return m_annuler.empty() ? m_base : m_annuler.back()->id; }

void Document::oublierHistorique() {
  m_annuler.clear();
  m_retablir.clear();
  m_base = m_prochainPas++;
  m_horsHistorique = false;
  m_gesteConception = -1;
  m_conceptionAvantGeste.reset();
}

int Document::indiceConception(const std::string& uuid) const {
  for (std::size_t i = 0; i < m_projet.electronique.size(); ++i)
    if (m_projet.electronique[i].uuid == uuid) return static_cast<int>(i);
  return -1;
}

bool Document::estVierge() const {
  return m_chemin.isEmpty() && !modifie() && m_projet.pieces.empty() && m_projet.assemblages.empty() && m_projet.schemasElectriques.empty() && m_projet.electronique.empty() && m_projet.electrique.empty() &&
         m_projet.sansCarte;
}

// Conceptions électroniques publiées : leur état avant une modification, pour la refuser si elle les touche.
namespace {
std::string cleVerrou(const prom::ecad::ConceptionElec& c) {
  nlohmann::ordered_json j = prom::ecad::versJson(c);
  if (j.contains("circuit") && j["circuit"].is_object()) j["circuit"].erase("affichage");
  return j.dump();
}
}  // namespace

std::vector<Document::Verrou> Document::verrous() const {
  std::vector<Verrou> v;
  for (const auto& c : m_projet.electronique)
    if (c.verrouillee) v.push_back({c.uuid, prom::ecad::versJson(c).dump(), cleVerrou(c)});
  return v;
}

void Document::proteger(const std::vector<Verrou>& avant) {
  for (const auto& x : avant)
    for (auto& c : m_projet.electronique)
      if (c.uuid == x.uuid && c.verrouillee && cleVerrou(c) != x.cle) {
        const auto affichage = c.circuit.affichage;
        c = prom::ecad::conceptionDepuisJson(nlohmann::ordered_json::parse(x.json));
        c.circuit.affichage = affichage;
        m_instantane.reset();
        emit refus(QStringLiteral("« %1 » est publiée (%2) et verrouillée : passe à une nouvelle révision, dans l’onglet Fabrication, pour la modifier.")
                       .arg(QString::fromStdString(c.nom), QString::fromStdString(c.publications.empty() ? std::string("verrouillée") : c.publications.back().id)));
      }
}

void Document::reappliquerVerrous(const std::vector<Verrou>& avant) {
  for (const auto& x : avant) {
    bool trouve = false;
    for (auto& c : m_projet.electronique)
      if (c.uuid == x.uuid) {
        trouve = true;
        if (cleVerrou(c) != x.cle || !c.verrouillee) c = prom::ecad::conceptionDepuisJson(nlohmann::ordered_json::parse(x.json));
      }
    if (!trouve) m_projet.electronique.push_back(prom::ecad::conceptionDepuisJson(nlohmann::ordered_json::parse(x.json)));
  }
  m_instantane.reset();
}

void Document::reglerSansHistorique(const std::function<void(prom::Projet&)>& f) {
  try {
    f(m_projet);
  } catch (const prom::ecad::ConceptionVerrouillee& e) {
    emit refus(QString::fromStdString(e.what()));
  }
  m_instantane.reset();
  m_horsHistorique = true;
  emit change();
  emit etatChange();
}

int Document::nombreErreurs() const {
  return static_cast<int>(std::count_if(m_problemes.begin(), m_problemes.end(), [](const prom::Probleme& q) { return q.sev == prom::Severite::Erreur; }));
}

// Instantané du projet (texte JSON compact), gardé jusqu'à la prochaine modification : modifie(), estVierge() et
// les pas d'annulation le relisent sans re-sérialiser tout le projet à chaque appel (EX-06).
const std::string& Document::instantane() const {
  if (!m_instantane) m_instantane = prom::versJson(m_projet, false).dump();  // références des géométries importées seulement (EX-06)
  return *m_instantane;
}

void Document::empilerAnnulation(std::string etat, const QString& nom) {
  auto pas = std::make_unique<Pas>();
  pas->nom = nom.isEmpty() ? QStringLiteral("Modification") : nom;
  pas->etat = std::move(etat);
  empiler(std::move(pas));
}

void Document::empiler(std::unique_ptr<Pas> pas) {
  // Pile bornée en nombre de pas et en mémoire : un projet qui porte de gros imports ne remplit plus la mémoire ;
  // les dix derniers pas restent toujours annulables. Le pas le plus ancien oublié devient la base de l'état.
  constexpr std::size_t PAS_MAX = 200, PAS_GARANTIS = 10, OCTETS_MAX = std::size_t{256} << 20;  // 200 pas ou 256 Mo (Spécifications)
  pas->id = m_prochainPas++;
  chien::geste(pas->nom);  // derniers gestes du rapport de blocage (9.10)
  if (!pas->conception.empty() && m_suiviSelection) pas->selection = m_suiviSelection(pas->conception);
  m_annuler.push_back(std::move(pas));
  std::size_t total = 0;
  for (const auto& e : m_annuler) total += e->octets();
  while (m_annuler.size() > PAS_GARANTIS && (m_annuler.size() > PAS_MAX || total > OCTETS_MAX)) {
    total -= m_annuler.front()->octets();
    m_base = m_annuler.front()->id;
    m_annuler.erase(m_annuler.begin());
  }
}

void Document::depiler() {
  if (!m_annuler.empty()) m_annuler.pop_back();
}

void Document::restaurer(const std::string& s) {
  m_projet = prom::normaliser(prom::Json::parse(s));
  m_instantane.reset();
}

void Document::recalculer() {
  m_instantane.reset();  // le lien carte-boîtier peut modifier le projet
  // Le boîtier suit la carte électronique qui le pilote (contour, épaisseur, trous, corps des composants)
  std::vector<std::string> remarques;
  for (std::size_t i = 0; i < m_projet.electronique.size(); ++i)
    if (m_projet.electronique[i].pilotageBoitier) {
      remarques = prom::appliquerCarteElectronique(m_projet, i);
      break;
    }
  m_derive = prom::deriver(m_projet);
  m_problemes = prom::verifier(m_projet, m_derive);
  // Ce que le lien carte-boîtier a dû approcher (corps tourné hors quart de tour…) est dit avec les autres problèmes
  for (const auto& r : remarques) m_problemes.push_back({prom::Severite::Alerte, "lien_carte", r, {}, std::nullopt});
  for (const auto& [source, liste] : m_problemesExternes) m_problemes.insert(m_problemes.end(), liste.begin(), liste.end());
  validerSelection();
  emit change();
}

void Document::definirProblemesExternes(const std::string& source, std::vector<prom::Probleme> problemes) {
  auto meme = [](const std::vector<prom::Probleme>& a, const std::vector<prom::Probleme>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
      if (a[i].code != b[i].code || a[i].msg != b[i].msg || a[i].sev != b[i].sev) return false;
    return true;
  };
  const auto it = m_problemesExternes.find(source);
  if (problemes.empty() ? it == m_problemesExternes.end() : (it != m_problemesExternes.end() && meme(it->second, problemes))) return;
  if (problemes.empty()) m_problemesExternes.erase(source);
  else m_problemesExternes[source] = std::move(problemes);
  recalculer();
}

void Document::validerSelection() {
  if ((m_selection.type == Cible::Composant && !composantSelectionne()) || (m_selection.type == Cible::Trou && !trouSelectionne())) {
    m_selection = {};
    emit selectionChangee();
  }
}

void Document::setConsultation(bool oui) { m_consultation = oui; }

void Document::modifier(const std::function<void(prom::Projet&)>& f, const QString& nom) {
  if (m_consultation) {
    emit refus(QStringLiteral("Mode consultation : rien ne se modifie. Affichage › Consultation le quitte."));
    return;
  }
  std::string avant = instantane();
  const auto proteges = verrous();
  // Le noyau refuse lui-même de modifier une révision publiée (F4) : le refus remonte ici, la conception est rendue
  // telle qu'elle était publiée, et le reste de la modification est gardé
  try {
    f(m_projet);
  } catch (const prom::ecad::ConceptionVerrouillee& e) {
    emit refus(QString::fromStdString(e.what()));
  }
  m_instantane.reset();
  prom::assurerIdentifiantsPieces(m_projet);
  prom::assurerReperes(m_projet);  // invariant : chaque pièce garde un identifiant unique
  proteger(proteges);
  if (instantane() == avant) return;
  empilerAnnulation(std::move(avant), nom);
  m_retablir.clear();
  recalculer();
  emit etatChange();
}

namespace {
QString texte(const std::string& t) { return QString::fromStdString(t); }
QString messageVerrou(const prom::ecad::ConceptionElec& c) {
  return QStringLiteral("« %1 » est publiée (%2) et verrouillée : passe à une nouvelle révision, dans l’onglet Fabrication, pour la modifier.")
      .arg(texte(c.nom), texte(c.publications.empty() ? std::string("verrouillée") : c.publications.back().id));
}
}  // namespace

bool Document::modifierConception(int indice, const std::function<void(prom::ecad::ConceptionElec&)>& f, const QString& nom, const QString& fusion) {
  if (indice < 0 || indice >= static_cast<int>(m_projet.electronique.size())) return false;
  if (m_consultation) {
    emit refus(QStringLiteral("Mode consultation : rien ne se modifie. Affichage › Consultation le quitte."));
    return false;
  }
  prom::ecad::ConceptionElec& c = m_projet.electronique[static_cast<std::size_t>(indice)];
  prom::ecad::ConceptionElec avant = c;
  try {
    f(c);
  } catch (const prom::ecad::ConceptionVerrouillee& e) {
    c = std::move(avant);
    emit refus(QString::fromStdString(e.what()));
    return false;
  }
  // Le verrou tient aussi pour ce qui n'appelle pas le noyau (un champ écrit directement) ; l'affichage reste libre,
  // et une nouvelle révision (qui déverrouille) passe
  if (avant.verrouillee && c.verrouillee && cleVerrou(avant) != cleVerrou(c)) {
    c = std::move(avant);
    emit refus(messageVerrou(c));
    return false;
  }
  prom::ecad::ChangementConception ch = prom::ecad::differencier(avant, c);
  if (ch.vide()) return false;
  // Suites de la modification, dans le même pas (3.6, 3.14) : valeurs calculées, écarts sans risque du circuit
  if (prom::ecad::appliquerSuites(c, ch)) ch = prom::ecad::differencier(avant, c);
  auto pas = std::make_unique<Pas>();
  pas->nom = nom.isEmpty() ? texte(prom::ecad::nommerChangement(c, ch)) : nom;
  pas->conception = c.uuid;
  pas->commande = std::make_unique<prom::ecad::CommandeInstantane>(pas->nom.toStdString(), avant, c);
  pas->fusion = fusion;
  pas->quand = QDateTime::currentMSecsSinceEpoch();
  // Appuis rapprochés de même clé (flèches) : un seul pas, qui garde l'état de départ du premier ; les écritures d'un
  // même passage dans un champ (« saisie:N ») se fondent quel que soit le temps entre deux frappes (P2)
  if (!fusion.isEmpty() && !m_annuler.empty() && m_retablir.empty()) {
    Pas& dernier = *m_annuler.back();
    const bool saisie = fusion.startsWith(QLatin1String("saisie:"));
    if (dernier.conception == pas->conception && dernier.fusion == fusion && (saisie || pas->quand - dernier.quand < 1000))
      if (auto* inst = dynamic_cast<prom::ecad::CommandeInstantane*>(dernier.commande.get())) {
        inst->prolonger(static_cast<const prom::ecad::CommandeInstantane&>(*pas->commande));
        dernier.quand = pas->quand;
        dernier.id = m_prochainPas++;  // nouvel état : un enregistrement fait entre deux appuis ne le couvre pas
        // Revenu à son état de départ (Échap dans un champ) : le pas disparaît
        if (inst->neutre()) depiler();
        m_instantane.reset();
        apresConception(indice, std::move(ch));
        return true;
      }
  }
  empiler(std::move(pas));
  m_retablir.clear();
  apresConception(indice, std::move(ch));
  return true;
}

QString Document::fusionOuverte() const {
  if (m_annuler.empty() || !m_retablir.empty()) return {};
  const Pas& dernier = *m_annuler.back();
  return QDateTime::currentMSecsSinceEpoch() - dernier.quand < 1000 ? dernier.fusion : QString();
}

bool Document::executer(int indice, std::unique_ptr<prom::ecad::Commande> commande) {
  if (!commande || indice < 0 || indice >= static_cast<int>(m_projet.electronique.size())) return false;
  if (m_consultation) {
    emit refus(QStringLiteral("Mode consultation : rien ne se modifie. Affichage › Consultation le quitte."));
    return false;
  }
  prom::ecad::ConceptionElec& c = m_projet.electronique[static_cast<std::size_t>(indice)];
  if (c.verrouillee) {
    emit refus(messageVerrou(c));
    return false;
  }
  try {
    commande->appliquer(c);
  } catch (const prom::ecad::ConceptionVerrouillee& e) {
    emit refus(QString::fromStdString(e.what()));
    return false;
  }
  prom::ecad::ChangementConception ch = commande->changement();
  ch.conception = c.uuid;
  // Suites de la commande (3.6, 3.14) : valeurs calculées recalculées, écarts sans risque appliqués au circuit ; elles
  // font partie du même pas d'annulation
  const prom::ecad::ConceptionElec apresCommande = c;
  std::unique_ptr<prom::ecad::CommandeInstantane> suites;
  if (prom::ecad::appliquerSuites(c, ch)) {
    suites = std::make_unique<prom::ecad::CommandeInstantane>("suites", apresCommande, c);
    ch.fusionner(suites->changement());
  }
  // Frappes d'un même champ : la commande se fond dans la précédente, un seul pas d'annulation (P2) ; ses suites
  // prolongent celles de la précédente
  auto* dernier = !m_annuler.empty() && m_retablir.empty() && m_annuler.back()->conception == c.uuid ? m_annuler.back()->commande.get() : nullptr;
  auto* avecSuites = dynamic_cast<prom::ecad::CommandeAvecSuites*>(dernier);
  prom::ecad::Commande* cible = avecSuites ? &avecSuites->principale() : dernier;
  if (cible && cible->fusionner(*commande)) {
    if (avecSuites) {
      if (suites) avecSuites->prolongerSuites(std::move(suites));
      else if (avecSuites->aDesSuites()) avecSuites->prolongerSuites(std::make_unique<prom::ecad::CommandeInstantane>("suites", c, c));
    } else if (suites) {
      m_annuler.back()->commande = std::make_unique<prom::ecad::CommandeAvecSuites>(std::move(m_annuler.back()->commande), std::move(suites));
    }
    m_annuler.back()->nom = texte(m_annuler.back()->commande->nom());
    m_annuler.back()->id = m_prochainPas++;
    // Une saisie revenue à sa valeur de départ (Échap dans le champ) ne laisse pas de pas
    if (m_annuler.back()->commande->neutre()) depiler();
    apresConception(indice, std::move(ch));
    return true;
  }
  auto pas = std::make_unique<Pas>();
  pas->nom = texte(commande->nom());
  pas->conception = c.uuid;
  pas->commande = suites ? std::make_unique<prom::ecad::CommandeAvecSuites>(std::move(commande), std::move(suites)) : std::move(commande);
  pas->quand = QDateTime::currentMSecsSinceEpoch();
  empiler(std::move(pas));
  m_retablir.clear();
  apresConception(indice, std::move(ch));
  return true;
}

// Après un changement d'une conception : la mécanique n'est recalculée que si cette carte pilote le boîtier (A2)
void Document::apresConception(int indice, prom::ecad::ChangementConception ch) {
  m_instantane.reset();
  if (indice >= 0 && indice < static_cast<int>(m_projet.electronique.size())) {
    ch.conception = m_projet.electronique[static_cast<std::size_t>(indice)].uuid;
    if (m_projet.electronique[static_cast<std::size_t>(indice)].pilotageBoitier) recalculer();
  }
  emit conceptionChangee(ch);
  emit etatChange();
}

void Document::remplacer(const prom::Projet& p) {
  modifier([&p](prom::Projet& q) { q = p; });
}

void Document::debutGeste() {
  m_avantGeste = instantane();
  m_geste = true;
  m_gesteConception = -1;
}

void Document::debutGesteConception(int indice) {
  if (indice < 0 || indice >= static_cast<int>(m_projet.electronique.size())) return;
  m_geste = true;
  m_gesteConception = indice;
  m_conceptionAvantGeste = m_projet.electronique[static_cast<std::size_t>(indice)];
  m_avantGeste.clear();
}

// Geste en cours quand Annuler ou Rétablir arrive : il devient un pas, et la vue qui le menait lâche l'objet
void Document::terminerGesteInterrompu() {
  if (!m_geste) return;
  finGeste();
  emit gesteInterrompu();
}

void Document::annulerGeste() {
  if (!m_geste) return;
  m_geste = false;
  emit gesteInterrompu();
  if (m_gesteConception >= 0 && m_conceptionAvantGeste && m_gesteConception < static_cast<int>(m_projet.electronique.size())) {
    // Ctrl+Z pendant un glisser : l'objet revient à sa place, l'historique ne bouge pas (A15)
    prom::ecad::ConceptionElec& c = m_projet.electronique[static_cast<std::size_t>(m_gesteConception)];
    prom::ecad::ChangementConception ch = prom::ecad::differencier(c, *m_conceptionAvantGeste);
    c = std::move(*m_conceptionAvantGeste);
    const int i = m_gesteConception;
    m_gesteConception = -1;
    m_conceptionAvantGeste.reset();
    apresConception(i, std::move(ch));
    return;
  }
  if (!m_avantGeste.empty()) {
    restaurer(m_avantGeste);
    m_avantGeste.clear();
    recalculer();
    emit etatChange();
  }
}

void Document::pendantGeste(const std::function<void(prom::Projet&)>& f, bool electronique) {
  if (m_consultation) return;  // le refus est dit à la fin du geste
  // Geste sur une conception : rien d'autre ne bouge, seule elle est tenue ; le verrou est vérifié à la fin
  if (m_gesteConception >= 0 && m_gesteConception < static_cast<int>(m_projet.electronique.size())) {
    prom::ecad::ConceptionElec& c = m_projet.electronique[static_cast<std::size_t>(m_gesteConception)];
    if (c.verrouillee) {
      emit refus(messageVerrou(c));
      return;
    }
    try {
      f(m_projet);
    } catch (const prom::ecad::ConceptionVerrouillee& e) {
      emit refus(QString::fromStdString(e.what()));
    }
    m_instantane.reset();
    prom::ecad::ChangementConception ch;
    ch.conception = c.uuid;
    ch.schema = ch.circuit = true;  // en cours de geste : la vue et le chevelu suivent, le reste attend la fin
    emit conceptionChangee(ch);
    return;
  }
  const auto proteges = verrous();
  try {
    f(m_projet);
  } catch (const prom::ecad::ConceptionVerrouillee& e) {
    emit refus(QString::fromStdString(e.what()));
  }
  m_instantane.reset();
  prom::assurerIdentifiantsPieces(m_projet);
  prom::assurerReperes(m_projet);
  proteger(proteges);
  if (electronique) {
    emit change();
    return;
  }
  recalculer();
}

void Document::modifierDefinitivement(const std::function<void(prom::Projet&)>& f) {
  try {
    f(m_projet);
  } catch (const prom::ecad::ConceptionVerrouillee& e) {
    emit refus(QString::fromStdString(e.what()));
  }
  m_instantane.reset();
  prom::assurerIdentifiantsPieces(m_projet);
  prom::assurerReperes(m_projet);
  // L'historique repart d'un état neuf, qui n'est pas encore enregistré
  const std::uint64_t enregistre = m_enregistre;
  oublierHistorique();
  m_enregistre = enregistre;
  recalculer();
  emit etatChange();
}

void Document::finGeste(const QString& nom) {
  if (!m_geste) return;
  m_geste = false;
  if (m_gesteConception >= 0) {
    // Geste sur une conception : un pas qui ne garde qu'elle, nommé d'après ce qui a bougé
    const int i = m_gesteConception;
    m_gesteConception = -1;
    if (!m_conceptionAvantGeste || i >= static_cast<int>(m_projet.electronique.size())) return;
    prom::ecad::ConceptionElec avant = std::move(*m_conceptionAvantGeste);
    m_conceptionAvantGeste.reset();
    prom::ecad::ConceptionElec& c = m_projet.electronique[static_cast<std::size_t>(i)];
    if (avant.verrouillee && c.verrouillee && cleVerrou(avant) != cleVerrou(c)) {
      c = std::move(avant);
      emit refus(messageVerrou(c));
      prom::ecad::ChangementConception tout;
      tout.entiere = true;
      apresConception(i, std::move(tout));
      return;
    }
    prom::ecad::ChangementConception ch = prom::ecad::differencier(avant, c);
    if (ch.vide()) {
      apresConception(i, std::move(ch));
      return;
    }
    // Suites du geste (3.6) : un symbole glissé sur un fil change des réseaux, le circuit suit dans le même pas
    if (prom::ecad::appliquerSuites(c, ch)) ch = prom::ecad::differencier(avant, c);
    auto pas = std::make_unique<Pas>();
    pas->nom = nom.isEmpty() ? texte(prom::ecad::nommerChangement(c, ch)) : nom;
    pas->conception = c.uuid;
    pas->commande = std::make_unique<prom::ecad::CommandeInstantane>(pas->nom.toStdString(), avant, c);
    pas->quand = QDateTime::currentMSecsSinceEpoch();
    empiler(std::move(pas));
    m_retablir.clear();
    apresConception(i, std::move(ch));
    return;
  }
  if (instantane() != m_avantGeste) {
    empilerAnnulation(std::move(m_avantGeste), nom);
    m_avantGeste.clear();
    m_retablir.clear();
    emit etatChange();
  }
  recalculer();
}

void Document::annuler() {
  // Pendant un glisser sur une conception, Ctrl+Z n'annule que le geste (A15) ; un autre geste en cours (session
  // d'esquisse…) devient d'abord un pas, puis il s'annule comme les autres
  if (m_geste && m_gesteConception >= 0) {
    annulerGeste();
    return;
  }
  terminerGesteInterrompu();
  if (m_annuler.empty()) return;
  const auto publiees = verrous();
  bool sansEffet = false, projetEntier = false;
  prom::ecad::ChangementConception changement;
  int indice = -1;
  std::vector<std::string> selectionAvant;
  while (!m_annuler.empty()) {
    std::unique_ptr<Pas> pas = std::move(m_annuler.back());
    m_annuler.pop_back();
    if (pas->commande) {
      // Commande sur une conception : défaite sur elle seule ; une conception publiée depuis ne se défait pas
      const int i = indiceConception(pas->conception);
      if (i < 0 || m_projet.electronique[static_cast<std::size_t>(i)].verrouillee) {
        sansEffet = true;
        continue;
      }
      try {
        pas->commande->defaire(m_projet.electronique[static_cast<std::size_t>(i)]);
      } catch (const std::exception&) {
        sansEffet = true;
        continue;
      }
      changement = pas->commande->changement();
      indice = i;
      selectionAvant = pas->selection;
      m_retablir.push_back(std::move(pas));
      sansEffet = false;
      break;
    }
    const std::string actuel = instantane();
    restaurer(pas->etat);
    reappliquerVerrous(publiees);
    if (instantane() != actuel) {
      pas->etat = actuel;
      m_retablir.push_back(std::move(pas));
      sansEffet = false;
      projetEntier = true;
      break;
    }
    sansEffet = true;  // pas qui ne touchait que la conception publiée : il est passé
  }
  if (sansEffet && !publiees.empty())
    emit refus(QStringLiteral("La publication ne s’annule pas : la révision publiée reste verrouillée ; le reste de l’historique s’annule normalement."));
  if (indice >= 0) {
    const std::string uuid = m_projet.electronique[static_cast<std::size_t>(indice)].uuid;
    apresConception(indice, std::move(changement));
    emit selectionRendue(uuid, selectionAvant);
    return;
  }
  if (projetEntier || sansEffet) {
    emit projetRemplace();
    recalculer();
  }
  emit etatChange();
}

void Document::retablir() {
  terminerGesteInterrompu();
  if (m_retablir.empty()) return;
  const auto publiees = verrous();
  while (!m_retablir.empty()) {
    std::unique_ptr<Pas> pas = std::move(m_retablir.back());
    m_retablir.pop_back();
    if (pas->commande) {
      const int i = indiceConception(pas->conception);
      if (i < 0 || m_projet.electronique[static_cast<std::size_t>(i)].verrouillee) continue;
      try {
        pas->commande->appliquer(m_projet.electronique[static_cast<std::size_t>(i)]);
      } catch (const std::exception&) {
        continue;
      }
      prom::ecad::ChangementConception ch = pas->commande->changement();
      m_annuler.push_back(std::move(pas));
      apresConception(i, std::move(ch));
      return;
    }
    const std::string actuel = instantane();
    restaurer(pas->etat);
    reappliquerVerrous(publiees);
    if (instantane() != actuel) {
      pas->etat = actuel;
      m_annuler.push_back(std::move(pas));
      break;
    }
  }
  emit projetRemplace();
  recalculer();
  emit etatChange();
}

QString Document::nomAnnuler() const { return m_annuler.empty() ? QString() : m_annuler.back()->nom; }
QString Document::nomRetablir() const { return m_retablir.empty() ? QString() : m_retablir.back()->nom; }

QStringList Document::historique() const {
  QStringList r;
  for (const auto& p : m_annuler) r << p->nom;
  for (auto it = m_retablir.rbegin(); it != m_retablir.rend(); ++it) r << (*it)->nom;
  return r;
}

int Document::positionHistorique() const { return static_cast<int>(m_annuler.size()); }

void Document::allerA(int position) {
  position = std::clamp(position, 0, static_cast<int>(m_annuler.size() + m_retablir.size()));
  // Un pas qui ne peut pas se défaire (publication) arrête la marche : on ne boucle jamais
  for (int garde = 0; static_cast<int>(m_annuler.size()) > position && garde < 1000; ++garde) {
    const std::size_t avant = m_annuler.size();
    annuler();
    if (m_annuler.size() >= avant) break;
  }
  for (int garde = 0; static_cast<int>(m_annuler.size()) < position && !m_retablir.empty() && garde < 1000; ++garde) {
    const std::size_t avant = m_retablir.size();
    retablir();
    if (m_retablir.size() >= avant) break;
  }
}

void Document::selectionner(const Cible& c) {
  if (c == m_selection) return;
  m_selection = c;
  emit selectionChangee();
}

const prom::Composant* Document::composantSelectionne() const {
  if (m_selection.type != Cible::Composant) return nullptr;
  for (const auto& k : m_projet.composants) if (k.id == m_selection.id) return &k;
  return nullptr;
}

const prom::Trou* Document::trouSelectionne() const {
  if (m_selection.type != Cible::Trou) return nullptr;
  for (const auto& t : m_projet.trous) if (t.id == m_selection.id) return &t;
  return nullptr;
}

// Sans re-sérialiser le projet (A2) : l'état courant est-il celui de l'enregistrement, sans réglage changé depuis ?
bool Document::modifie() const { return m_horsHistorique || etatCourant() != m_enregistre; }

void Document::ouvrir(const QString& chemin, bool ignorerVerrou) {
  auto verrou = verrouPour(chemin, ignorerVerrou);  // avant la lecture : on demande d'abord s'il faut l'ouvrir
  prom::RapportLecture rapport;
  prom::Projet p = prom::lireProjet(chemin.toStdString(), &rapport);
  m_projet = std::move(p);
  m_instantane.reset();
  m_verrou = std::move(verrou);
  m_avertissements = rapport.avertissements;
  m_disque = prom::etatDisque(prom::cheminFichier(chemin.toStdString()));
  m_chemin = chemin;
  oublierHistorique();
  m_selection = {};
  emit projetRemplace();
  emit projetCharge();
  emit selectionChangee();
  recalculer();
  m_enregistre = etatCourant();  // après le recalcul : le boîtier piloté par une carte est déjà à jour
  journal("ouverture de « " + nomFichier(chemin) + " » (format " + std::to_string(rapport.versionFormat) + ", " + std::to_string(rapport.avertissements.size()) +
          " avertissement(s) de lecture)");
  emit etatChange();
}

void Document::enregistrer(const QString& chemin, bool forcer) {
  const bool memeFichier = !m_chemin.isEmpty() && cleFichier(chemin) == cleFichier(m_chemin);
  const std::filesystem::path cible = prom::cheminFichier(chemin.toStdString());
  if (memeFichier && m_disque.existe && !forcer) {
    const prom::EtatDisque maintenant = prom::etatDisque(cible);
    if (maintenant.existe && maintenant != m_disque)
      throw ConflitEnregistrement("« " + nomFichier(chemin) + " » a été modifié ailleurs depuis son ouverture (autre fenêtre, autre programme ou synchronisation). "
                                  "L’enregistrer maintenant effacerait ces changements.");
  }
  std::unique_ptr<QLockFile> nouveau;
  if (!memeFichier) nouveau = verrouPour(chemin, forcer);
  // Premier enregistrement d'un projet sans nom : il prend celui du fichier, que montrent aussi l'onglet et le
  // titre (AP-12)
  if (m_projet.nom.empty() || m_projet.nom == "Projet sans nom") {
    m_projet.nom = QFileInfo(chemin).completeBaseName().toStdString();
    m_instantane.reset();
  }
  prom::ecrireProjet(m_projet, chemin.toStdString());
  if (!memeFichier) m_verrou = std::move(nouveau);
  m_chemin = chemin;
  m_disque = prom::etatDisque(cible);
  m_enregistre = etatCourant();
  m_horsHistorique = false;
  journal("enregistrement de « " + nomFichier(chemin) + " »");
  emit etatChange();
}

void Document::restaurerCopie(const std::string& texte, const QString& cheminOrigine) {
  lacherFichier();
  m_projet = prom::normaliser(prom::Json::parse(texte));
  m_instantane.reset();
  m_chemin = cheminOrigine;
  oublierHistorique();
  m_selection = {};
  // Modifiée tant qu'elle diffère du fichier d'origine sur le disque (ou s'il n'y en a pas)
  std::string disque;
  if (!cheminOrigine.isEmpty()) {
    try {
      m_verrou = verrouPour(cheminOrigine, true);
      const std::filesystem::path f = prom::cheminFichier(cheminOrigine.toStdString());
      m_disque = prom::etatDisque(f);
      if (m_disque.existe) disque = prom::versJson(prom::lireProjet(cheminOrigine.toStdString())).dump();
    } catch (const std::exception&) {
      m_disque = {};
    }
  }
  emit projetRemplace();
  emit projetCharge();
  emit selectionChangee();
  recalculer();
  m_enregistre = etatCourant();
  m_horsHistorique = disque.empty() || disque != instantane();
  journal("copie de récupération reprise pour « " + nomFichier(cheminOrigine.isEmpty() ? QStringLiteral("sans nom") : cheminOrigine) + " »");
  emit etatChange();
}

void Document::nouveau() {
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  charger(p);
}

void Document::exemple() { charger(prom::exemple()); }

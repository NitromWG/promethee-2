// Prométhée : équipe, cycle de vie et exigences (lot L19 ; 19.2, 19.3, 19.5, 19.6, 19.8, 19.9 ; ENT-01 à ENT-12).
// L'équipe se déclare dans un fichier du dossier d'équipe : personnes, rôles, clés. Les états du cycle de vie et les
// passages permis à chaque rôle ; les schémas de révision (lettres sans I, O, Q, S, X, Z ; nombres ; lettre et
// chiffre). Numéros d'articles de l'entreprise reliés aux références fabricant. Ordres de modification approuvés par
// leur circuit. Exigences vérifiées à chaque enregistrement, chiffrées ou reliées. Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"
#include "promethee/exigences.hpp"

namespace prom {
struct Projet;

// ---------- Équipe et rôles (19.2) ----------
enum class Role : std::uint8_t { Responsable, Concepteur, Relecteur, Observateur };
const char* nomRole(Role r);
std::optional<Role> roleDepuis(const std::string& texte);
struct Personne {
  std::string nom, courriel, cle;  // clé publique (signature des approbations)
  Role role = Role::Concepteur;
};
struct Equipe {
  std::string nom;
  std::vector<Personne> personnes;
  const Personne* personne(const std::string& nom) const;
};
// Fichier « equipe.json » du dossier d'équipe ; lève std::runtime_error s'il est illisible
Equipe lireEquipe(const std::string& json);
std::string ecrireEquipe(const Equipe& e);
// Ce qu'un rôle peut faire : « modifier », « publier », « approuver », « valider-article », « lire »
bool peut(Role r, const std::string& action);

// ---------- Cycle de vie (19.3) ----------
const std::vector<std::string>& etatsCycleVie();  // brouillon, pré-série, production, fin de série, obsolète
// Passage permis : un concepteur propose (brouillon → pré-série), le responsable met en production ou en fin de
// série, personne ne revient d'« obsolète »
bool passagePermis(Role r, const std::string& depuis, const std::string& vers);
// Change l'état d'un article de la conception si le rôle le permet ; rend la raison d'un refus, vide sinon
std::string changerEtatArticle(ecad::ConceptionElec& c, const std::string& article, const std::string& vers, Role r);

// ---------- Révisions (19.3) ----------
enum class SchemaRevision : std::uint8_t { Lettres, Nombres, LettreChiffre };
// Lettres : A, B… Y sans I, O, Q, S, X, Z, puis AA, AB… ; nombres : 1, 2… ; lettre et chiffre : A1, A2… puis B1 au
// changement majeur
std::string revisionSuivante(const std::string& courante, SchemaRevision s, bool majeure = true);
bool revisionValide(const std::string& r, SchemaRevision s);

// ---------- Numéros internes (19.5) ----------
// « PRM-000042 » : préfixe et numéro sur « chiffres » chiffres, au premier libre dans la conception
std::string numeroInterneSuivant(const ecad::ConceptionElec& c, const std::string& prefixe = "PRM-", int chiffres = 6);
// Article trouvé par son numéro interne, sa référence fabricant ou celles de ses secondes sources
const ecad::Article* articleParNumero(const ecad::ConceptionElec& c, const std::string& texte);

// ---------- Ordres de modification (19.6) ----------
// Circuit d'approbation : un relecteur et le responsable ; un refus clôt l'ordre. Rend la raison d'un refus d'agir.
std::string approuver(OrdreModification& o, const Equipe& e, const std::string& personne, bool accord, const std::string& date, const std::string& commentaire = {});
std::string appliquerOrdre(OrdreModification& o, ecad::ConceptionElec& c);  // l'ordre approuvé fait passer la révision

// ---------- Exigences (19.8, 19.9) ----------
struct VerificationExigence {
  std::string id, etat;  // « vérifiée », « en défaut », « non vérifiable », « à vérifier » (méthode manuelle)
  std::string mesure;    // « 32,40 € pour 100 cartes »
  std::vector<std::string> liensManquants;
};
std::vector<VerificationExigence> verifierExigences(const Projet& p);

}  // namespace prom

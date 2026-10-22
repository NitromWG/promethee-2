// Prométhée : chien de garde du fil principal et mode portable (lot L9, tâches 9.10 et 9.11 ; ERG-45, ERG-46, ERG-11).
//
// Le chien de garde tourne sur un fil à part : le fil principal bat toutes les 500 ms ; plus de dix secondes sans
// battement (Prométhée figé), un rapport est préparé dans le dossier des rapports : version, système, pile du fil
// principal (ou vidage du processus sous Windows), liste des fils et vingt derniers gestes tirés de l'historique des
// commandes. Rien n'est envoyé : le rapport reste dans le dossier, à joindre à la main si on le souhaite.
//
// Mode portable : un fichier témoin « promethee-portable.txt » à côté du programme place réglages (QSettings au
// format INI), bibliothèques personnelles, copies de récupération, journal et caches dans « promethee-donnees », à
// côté du programme : rien n'est écrit dans le profil de l'utilisateur.
// Licence GPL-3.0-only.
#pragma once

#include <QString>

namespace chien {
// Depuis le fil principal, l'application créée. « secondes » : durée sans battement qui déclenche le rapport.
void demarrer(const QString& dossierRapports, int secondes = 10);
void arreter();
void geste(const QString& nom);  // un geste de plus dans l'anneau des vingt derniers
QString dernierRapport();         // chemin du dernier rapport écrit ; vide s'il n'y en a pas
}  // namespace chien

namespace portable {
// Tout premier appel de main(), avant le journal : cherche le témoin à côté du programme (ou dans le dossier donné
// par PROMETHEE_DOSSIER_PROGRAMME, pour les essais) ; actif, il oriente les dossiers de données vers lui
void initialiser(const char* argv0);
bool actif();
QString dossier();              // « …/promethee-donnees » ; vide hors mode portable
void appliquerReglages();       // après la création de l'application : QSettings au format INI dans le dossier
QString dossierDonneesAppli();  // à la place de QStandardPaths::AppDataLocation (bibliothèques personnelles)
QString fichierReglages();      // fichier INI des réglages en mode portable
}  // namespace portable

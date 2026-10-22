// Prométhée : journal de démarrage et rapport de plantage. Licence GPL-3.0-only.
// Chaque lancement écrit promethee-journal.txt dans le dossier temporaire de l'utilisateur ; celui du
// lancement précédent est gardé (promethee-journal-precedent.txt), car c'est lui qui décrit un plantage.
// En tête : version et identifiant de build, système, Qt, OpenCascade. Les chemins y sont abrégés (le
// dossier personnel devient « ~ »). Un plantage écrit le code, le module fautif et la pile d'appels dans le
// journal (et, sous Windows, un fichier promethee-plantage.dmp à joindre au signalement).
#pragma once

#include <string>

void installerJournal();  // tout premier appel de main()
void completerJournal();  // après la création de QApplication : système, Qt, écrans
void journal(const char* texte);
void journal(const std::string& texte);
const char* cheminJournal();

// Comment s'est terminé le lancement précédent (lu dans son journal) : sans « fermeture normale », il a été
// interrompu (plantage, arrêt forcé, coupure).
enum class FinPrecedente { Inconnue, Normale, Interrompue };
FinPrecedente finSessionPrecedente();
std::string cheminJournalPrecedent();

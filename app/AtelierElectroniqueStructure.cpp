// Prométhée : structure du schéma dans l'atelier (lot L7) : feuilles de canal, reproduction d'un canal, déplacement
// vers une autre feuille. Licence GPL-3.0-only.
#include <QInputDialog>

#include "AtelierElectronique.hpp"
#include "VueSchema.hpp"
#include "promethee/structure_schema.hpp"

using namespace prom::ecad;

int AtelierElectronique::repeterFeuille(int feuille, int exemplaires) {
  const ConceptionElec* c = conceptionCourante();
  if (!c || feuille < 0 || feuille >= static_cast<int>(c->feuilles.size())) return 0;
  int n = 0;
  modifierConception([&](ConceptionElec& cc) { n = prom::ecad::repeterFeuille(cc, static_cast<std::size_t>(feuille), exemplaires); },
                     QStringLiteral("Feuille de canal : %1 exemplaires").arg(exemplaires));
  afficherMessage(QStringLiteral("<b>%1 × %2</b> : %3 copie(s) liée(s), repères par canal (R5, R205, R305…).")
                      .arg(exemplaires)
                      .arg(QString::fromStdString(c->feuilles[static_cast<std::size_t>(feuille)].nom).toHtmlEscaped())
                      .arg(n),
                  NiveauMessage::Succes);
  return n;
}

int AtelierElectronique::reproduireCanal(int feuilleSource) {
  const ConceptionElec* c = conceptionCourante();
  if (!c || feuilleSource < 0 || feuilleSource >= static_cast<int>(c->feuilles.size())) return 0;
  // Écart : la largeur du canal placé, plus 5 mm
  Boite b;
  for (const auto& s : c->feuilles[static_cast<std::size_t>(feuilleSource)].symboles)
    for (const auto& e : c->circuit.empreintes)
      if (e.composant == s.composant)
        if (const DefEmpreinte* d = c->empreinte(e.empreinte)) b.etendre(boite(encombrementSurCarte(e, *d)));
  if (b.vide()) {
    afficherMessage(QStringLiteral("Place d’abord le canal source sur la carte."));
    return 0;
  }
  const Pt ecart{b.largeur() + 5 * NM_PAR_MM, 0};
  int n = 0;
  modifierConception([&](ConceptionElec& cc) { n = prom::ecad::reproduireCanal(cc, static_cast<std::size_t>(feuilleSource), ecart); }, QStringLiteral("Reproduire le canal"));
  afficherMessage(QStringLiteral("Canal reproduit : %1 empreinte(s) placée(s), pistes recopiées sur leurs réseaux.").arg(n), NiveauMessage::Succes);
  return n;
}

int AtelierElectronique::deplacerVersFeuille(const std::set<std::string>& symboles, int cible) {
  const ConceptionElec* c = conceptionCourante();
  const int source = vueSchema() ? vueSchema()->feuille() : -1;
  if (!c || source < 0 || cible < 0 || cible >= static_cast<int>(c->feuilles.size()) || cible == source || symboles.empty()) return 0;
  BilanDeplacement b;
  modifierConception([&](ConceptionElec& cc) { b = prom::ecad::deplacerVersFeuille(cc, static_cast<std::size_t>(source), symboles, static_cast<std::size_t>(cible)); },
                     QStringLiteral("Déplacer vers « %1 »").arg(QString::fromStdString(c->feuilles[static_cast<std::size_t>(cible)].nom)));
  afficherMessage(QStringLiteral("%1 symbole(s) déplacé(s), %2 fil(s) avec eux, %3 étiquette(s) posée(s) pour les liaisons coupées.").arg(b.symboles).arg(b.fils).arg(b.etiquettes),
                  NiveauMessage::Succes);
  return b.symboles;
}

// Prométhée : outils communs aux fichiers du faisceau (non publics). Licence GPL-3.0-only.
#pragma once

#include "pdf_vectoriel.hpp"
#include "promethee/cablage.hpp"

namespace prom::elec::detail {

// La planche dessinée dans la page courante d'un PDF, réduite de « k » et posée en (dx, dy) mm
void plancheDansPdf(pdfv::Document& p, const Cablage& h, const BibliothequeCablage& b, const PlancheDessinee& d, double k, double dx, double dy);

}  // namespace prom::elec::detail

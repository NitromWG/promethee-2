add_library(promethee_noyau
  src/modele.cpp
  src/migrations.cpp
  src/cartes_mesure.cpp src/schema_gestes.cpp src/electronique_parcours.cpp
  src/archive.cpp
  src/nomenclature_meca.cpp
  src/derive.cpp
  src/verifs.cpp
  src/placement.cpp
  src/nomenclature.cpp
  src/kicad.cpp
  src/forme.cpp
  src/esquisse.cpp
  src/piece.cpp
  src/unites.cpp src/valeurs.cpp src/formules.cpp src/hauteurs.cpp src/synchronisation.cpp src/remplacement.cpp src/objets.cpp src/articles.cpp
  src/ecad.cpp
  src/schema.cpp
  src/circuit.cpp
  src/empreintes.cpp
  src/electronique.cpp
  src/electronique_cuivre.cpp
  src/fabrication_ecad.cpp
  src/electronique_json.cpp
  src/commandes.cpp
  src/lien_electronique.cpp src/publication.cpp src/routage.cpp src/routeur.cpp src/verification.cpp src/placement_circuit.cpp src/facade.cpp src/contour_esquisse.cpp src/modeles3d.cpp src/structure_schema.cpp src/dessins_circuit.cpp src/fabrication_avancee.cpp src/robustesse.cpp src/echanges_electronique.cpp src/simulation.cpp src/moteur_ngspice.cpp src/exemple_electronique.cpp src/exigences.cpp src/compression.cpp src/variantes.cpp src/comparaison.cpp src/historique_local.cpp src/collaboration.cpp src/systeme_erc.cpp src/sorties.cpp src/revue_conception.cpp src/placement_auto.cpp src/api_commandes.cpp src/simulation_avancee.cpp src/logique.cpp src/usinage.cpp src/essais_production.cpp src/integrite.cpp src/equipe.cpp
  src/contraintes.cpp src/fabrication_detaillee.cpp src/piles_pastilles.cpp src/expressions.cpp src/regles_carte.cpp src/drc_avancee.cpp src/circuit_edition.cpp src/cuivre_outils.cpp src/approvisionnement.cpp src/gabarits_ipc.cpp src/gestion_bibliotheque.cpp src/idf.cpp src/schema_avance.cpp src/empreintes_edition.cpp src/reutilisation.cpp src/lecture_svg.cpp src/formes_schema.cpp src/sorties_fabricant.cpp src/mise_en_panneau.cpp src/controles_fabrication.cpp src/drc_familles.cpp src/serigraphie_preparee.cpp src/lignes_transmission.cpp src/signaux.cpp src/routage_auto.cpp src/pdf_documents.cpp src/plans_carte.cpp src/kicad_schema.cpp src/import_eagle.cpp src/import_bibliotheques.cpp src/import_easyeda.cpp src/nomenclature_entree.cpp src/import_commun.cpp src/idx.cpp src/csv.cpp src/netlists_tierces.cpp src/dessins_carte.cpp src/formats_cao.cpp src/import_schemas.cpp src/import_altium.cpp src/pads_ascii.cpp src/cadstar.cpp src/formats_anciens.cpp src/symboles_geda.cpp src/reprise_existant.cpp src/simulation_projet.cpp src/modeles_simulation.cpp src/calculateurs_electronique.cpp src/processus.cpp src/git_projet.cpp src/projet_partage.cpp src/scripts_python.cpp src/assistant_mcp.cpp src/brochages.cpp src/commandes_texte.cpp src/requetes_objets.cpp src/rendu_png.cpp src/assistant.cpp src/conception_guidee.cpp src/simulation_continue.cpp src/analyseurs_numeriques.cpp src/analyses_natives.cpp src/moteurs_tiers.cpp src/parametres_s.cpp src/simulation_nourrie.cpp src/alimentations.cpp src/embarque.cpp src/micro_simulation.cpp src/hdl.cpp src/platine.cpp src/enseignement.cpp
  src/fichiers.cpp src/hachage.cpp src/structure.cpp src/bibliotheque.cpp src/police.cpp src/moindres_carres.cpp src/dxf.cpp src/dxf_dessin.cpp src/depot.cpp src/kicad_electronique.cpp
  src/electrique.cpp src/electrique_symboles.cpp src/electrique_catalogue.cpp src/electrique_rapports.cpp src/electrique_verif.cpp
  src/electrique_calculs.cpp src/electrique_json.cpp src/electrique_logement.cpp src/electrique_armoire.cpp src/electrique_qet.cpp src/exemple_electrique.cpp src/electrique_circuits.cpp
  src/electrique_distribution.cpp src/electrique_distribution_docs.cpp src/electrique_glossaire.cpp src/electrique_calculateurs.cpp
  src/electrique_pv.cpp
  src/electrique_pv_docs.cpp
  src/electrique_aide_symboles.cpp
  src/electrique_fabrication.cpp
  src/electrique_grafcet.cpp
  src/electrique_appareils.cpp
  src/electrique_parcours.cpp
  ${PROMETHEE_GENERE}/version.cpp
  externe/clipper2/src/clipper.engine.cpp
  externe/clipper2/src/clipper.offset.cpp
  externe/clipper2/src/clipper.rectclip.cpp
  externe/clipper2/src/clipper.triangulation.cpp)
add_library(Promethee::noyau ALIAS promethee_noyau)
add_dependencies(promethee_noyau promethee_version)
target_include_directories(promethee_noyau PUBLIC include PRIVATE externe/clipper2/include)
target_link_libraries(promethee_noyau PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(promethee_noyau PUBLIC cxx_std_20)
target_link_libraries(promethee_noyau PRIVATE ${CMAKE_DL_LIBS})

# simavr (15.4) : l'émulateur libre de l'AVR, lié en statique quand il est installé (paquet libsimavr-dev) ; sans
# lui, la co-simulation du microcontrôleur est absente et ses essais sont passés
find_path(PROMETHEE_SIMAVR_INCLUDE simavr/sim_avr.h)
find_library(PROMETHEE_SIMAVR_STATIQUE NAMES libsimavr.a)
# libelf (lecture des programmes ELF par simavr) : la bibliothèque partagée, présente sur tous les Linux
find_library(PROMETHEE_ELF NAMES elf)
if(PROMETHEE_SIMAVR_INCLUDE AND PROMETHEE_SIMAVR_STATIQUE AND PROMETHEE_ELF)
  message(STATUS "simavr : ${PROMETHEE_SIMAVR_STATIQUE} (co-simulation du microcontrôleur)")
  target_compile_definitions(promethee_noyau PRIVATE PROMETHEE_SIMAVR=1)
  target_link_libraries(promethee_noyau PRIVATE ${PROMETHEE_SIMAVR_STATIQUE} ${PROMETHEE_ELF})
else()
  message(STATUS "simavr absent : co-simulation du microcontrôleur non construite")
endif()

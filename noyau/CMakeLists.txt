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
  src/lien_electronique.cpp src/publication.cpp src/routage.cpp src/routeur.cpp src/verification.cpp src/placement_circuit.cpp src/facade.cpp src/contour_esquisse.cpp src/modeles3d.cpp src/structure_schema.cpp src/dessins_circuit.cpp src/fabrication_avancee.cpp src/robustesse.cpp src/echanges_electronique.cpp src/simulation.cpp src/moteur_ngspice.cpp src/exemple_electronique.cpp
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

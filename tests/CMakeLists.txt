find_package(Catch2 3 REQUIRED)
add_executable(promethee_tests test_noyau.cpp test_reference.cpp test_geometrie.cpp test_fixations_ancrages.cpp test_kicad.cpp test_pieces.cpp test_electronique.cpp test_fichiers.cpp test_mecanique.cpp test_electronique_audit.cpp test_electronique_l0.cpp test_electronique_l1.cpp test_electronique_l2.cpp test_electronique_l3.cpp test_electronique_l4.cpp test_electronique_l5.cpp test_electronique_l6.cpp test_electronique_l7.cpp test_electronique_l8.cpp test_electronique_l9.cpp test_electronique_l10.cpp test_electronique_l11.cpp test_electronique_l12.cpp test_electronique_l13.cpp test_electronique_l14.cpp test_electronique_l15.cpp test_electronique_l16.cpp test_electronique_l17.cpp test_electronique_l18.cpp test_electronique_l19.cpp test_mesures.cpp test_structure.cpp test_moindres_carres.cpp test_dessin.cpp test_exports.cpp test_fonctions_meca.cpp test_electrique.cpp test_distribution.cpp test_calculateurs.cpp test_pv.cpp test_fabrication.cpp test_grafcet.cpp test_appareils.cpp test_034_modele.cpp test_034_l5.cpp test_034_regles.cpp test_034_l4.cpp test_034_l6.cpp test_034_l7.cpp test_034_l8.cpp test_034_lignes.cpp test_034_pdf.cpp test_034_l9.cpp test_034_l10.cpp test_034_l10b.cpp test_034_l10c.cpp test_034_l10d.cpp)
target_link_libraries(promethee_tests PRIVATE Promethee::noyau Promethee::geometrie Catch2::Catch2WithMain)
target_compile_definitions(promethee_tests PRIVATE PROMETHEE_DONNEES="${CMAKE_CURRENT_SOURCE_DIR}/donnees")
if(WIN32)
  target_sources(promethee_tests PRIVATE ${PROJECT_SOURCE_DIR}/cmake/utf8.manifest)
endif()
include(Catch)
# Un cas sauté (SKIP, par exemple sans ngspice sur le poste) rend 4 : CTest le dit sauté, pas en échec
catch_discover_tests(promethee_tests DISCOVERY_MODE PRE_TEST PROPERTIES SKIP_RETURN_CODE 4)
add_test(NAME outil_exemple_puis_export
  COMMAND ${CMAKE_COMMAND} -DOUTIL=$<TARGET_FILE:promethee> -DDOSSIER=${CMAKE_CURRENT_BINARY_DIR}/sortie_outil
          -P ${CMAKE_CURRENT_SOURCE_DIR}/outil.cmake)
add_test(NAME outil_conception_electronique
  COMMAND ${CMAKE_COMMAND} -DOUTIL=$<TARGET_FILE:promethee> -DDOSSIER=${CMAKE_CURRENT_BINARY_DIR}/sortie_outil_atelier
          -P ${CMAKE_CURRENT_SOURCE_DIR}/outil_atelier.cmake)
# Lecture du source (ERG-17, ERG-18) : pointeur jamais déplacé, touches de l'atelier électronique par le registre
add_test(NAME source_souris_et_pointeur COMMAND ${CMAKE_COMMAND} -DAPP=${PROJECT_SOURCE_DIR}/app -P ${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)
# Aide de l'atelier électronique (2.11, 2.17, ERG-13) : chaque code de règle que le noyau rend a son explication
add_test(NAME source_aucun_acces_reseau COMMAND ${CMAKE_COMMAND} "-DDOSSIERS=${PROJECT_SOURCE_DIR}/app;${PROJECT_SOURCE_DIR}/noyau;${PROJECT_SOURCE_DIR}/geometrie;${PROJECT_SOURCE_DIR}/outils"
         -P ${CMAKE_CURRENT_SOURCE_DIR}/reseau.cmake)
add_test(NAME aide_regles COMMAND ${CMAKE_COMMAND} -DNOYAU=${PROJECT_SOURCE_DIR}/noyau -DAPP=${PROJECT_SOURCE_DIR}/app -P ${CMAKE_CURRENT_SOURCE_DIR}/aide.cmake)
find_package(Python3 COMPONENTS Interpreter QUIET)
find_program(GERBV gerbv)
# Corpus de cartes d'essai relu par les lecteurs indépendants (audit Q4) : simple face, quatre couches, composants
# dessous, zones et découpe, cartes KiCad importées
add_executable(corpus_fabrication audit_electronique/corpus_fabrication.cpp)
target_link_libraries(corpus_fabrication PRIVATE Promethee::noyau)
# gerbonara, troisième lecteur, s'il est installé (pip install gerbonara)
set(GERBONARA "")
if(Python3_Interpreter_FOUND)
  # Cherché comme il sera lancé, en mode isolé (-I) : un paquet installé pour l'utilisateur seul n'y est pas vu
  execute_process(COMMAND ${Python3_EXECUTABLE} -I -c "import gerbonara" RESULT_VARIABLE sans_gerbonara OUTPUT_QUIET ERROR_QUIET)
  if(sans_gerbonara EQUAL 0)
    set(GERBONARA ${CMAKE_CURRENT_SOURCE_DIR}/audit_electronique/verif_gerbonara.py)
  endif()
endif()
add_test(NAME fabrication_relue_independamment
  COMMAND ${CMAKE_COMMAND} -DOUTIL=$<TARGET_FILE:promethee> -DCORPUS=$<TARGET_FILE:corpus_fabrication>
          -DDONNEES=${CMAKE_CURRENT_SOURCE_DIR}/donnees -DDOSSIER=${CMAKE_CURRENT_BINARY_DIR}/sortie_fabrication
          -DPYTHON=$<$<BOOL:${Python3_Interpreter_FOUND}>:${Python3_EXECUTABLE}> -DGERBV=${GERBV} -DGERBONARA=${GERBONARA}
          -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/verifier_fabrication.py
          -P ${CMAKE_CURRENT_SOURCE_DIR}/fabrication.cmake)
# Formats de fichier (1.22, ECH-10, ECH-11) : les projets d'essai, et ce qu'ils deviennent écrits pour une version
# précédente, validés par un lecteur JSON Schema indépendant (jsonschema, sauté s'il manque) contre docs/schemas
if(Python3_Interpreter_FOUND)
  add_test(NAME formats_schemas_json
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/outils/compatibilite.py schemas $<TARGET_FILE:promethee> ${PROJECT_SOURCE_DIR}/docs
            ${CMAKE_CURRENT_SOURCE_DIR}/donnees ${CMAKE_CURRENT_BINARY_DIR}/sortie_formats)
  set_tests_properties(formats_schemas_json PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Essais des audits du 10 octobre 2026 (mécanique : Catch2 ; électronique : programme qui écrit « DÉFAUT
# CONFIRMÉ » ou « conforme » par cas). Un défaut corrigé fait passer son essai ; un défaut qui revient le fait
# échouer.
add_subdirectory(audit)
add_executable(essais_audit_electronique audit_electronique/essais_noyau.cpp)
target_link_libraries(essais_audit_electronique PRIVATE Promethee::noyau)
add_test(NAME audit_electronique COMMAND essais_audit_electronique WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(audit_electronique PROPERTIES FAIL_REGULAR_EXPRESSION "DÉFAUT CONFIRMÉ")
# Banc de mesure des cartes de 300, 1 000 et 2 000 composants (1.1, Q5) : la plus petite à chaque essai, les trois
# en intégration continue (résumé de la construction)
add_executable(mesures_electronique audit_electronique/mesures.cpp)
target_link_libraries(mesures_electronique PRIVATE Promethee::noyau)
add_test(NAME mesures_carte_300 COMMAND mesures_electronique 300)
add_executable(fuzz_electronique audit_electronique/fuzz_json.cpp)
target_link_libraries(fuzz_electronique PRIVATE Promethee::noyau)
add_test(NAME audit_electronique_fichiers_abimes COMMAND fuzz_electronique 300)
set_tests_properties(audit_electronique_fichiers_abimes PROPERTIES FAIL_REGULAR_EXPRESSION "[1-9][0-9]* exception")

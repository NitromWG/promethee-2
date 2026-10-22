# Essais de l'audit de Prométhée 0.25 (Catch2 3, liés au noyau et à la géométrie).
# Chaque CHECK décrit le comportement CORRECT attendu : un essai en échec confirme le défaut du rapport
# qui porte le même numéro (ES-01, FO-02, AS-05…). Les essais « OK … » vérifient ce qui fonctionne.
#
# Dans le dépôt (recommandé) : copier ce dossier en tests/audit/ et ajouter « add_subdirectory(audit) »
# à la fin de tests/CMakeLists.txt. Les cibles Promethee::noyau et Promethee::geometrie sont alors reprises.
#
# Seul, contre un dépôt déjà compilé (Linux, bibliothèques statiques) :
#   cmake -S . -B b -G Ninja -DPROMETHEE_SRC=<racine du dépôt> -DPROMETHEE_BUILD=<dossier de compilation>
#   cmake --build b
cmake_minimum_required(VERSION 3.22)
if(NOT TARGET Promethee::geometrie)
  project(essais_audit_promethee CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
  endif()
  find_package(OpenCASCADE REQUIRED)
  find_package(nlohmann_json REQUIRED)
  set(PROMETHEE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Racine du dépôt Prométhée")
  set(PROMETHEE_BUILD "${PROMETHEE_SRC}/build" CACHE PATH "Dossier de compilation de Prométhée (contient noyau/ et geometrie/)")
  # Mêmes boîtes à outils que geometrie/CMakeLists.txt (OpenCascade 7.6 comme 7.8 et plus).
  set(OCCT TKHLR TKOffset TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO TKBool
      TKShHealing TKMesh TKXSBase TKLCAF TKCAF TKCDF TKXCAF TKFillet TKFeat)
  if(TARGET TKDESTEP)
    list(APPEND OCCT TKDE TKDESTEP TKDESTL TKDEIGES)
  else()
    list(APPEND OCCT TKSTEPBase TKSTEPAttr TKSTEP209 TKSTEP TKSTL TKXDESTEP TKIGES TKXDEIGES)
  endif()
  add_library(audit_promethee INTERFACE)
  target_include_directories(audit_promethee INTERFACE ${PROMETHEE_SRC}/noyau/include ${PROMETHEE_SRC}/geometrie/include)
  target_include_directories(audit_promethee SYSTEM INTERFACE ${OpenCASCADE_INCLUDE_DIR})
  target_link_libraries(audit_promethee INTERFACE ${PROMETHEE_BUILD}/geometrie/libpromethee_geometrie.a
                        ${PROMETHEE_BUILD}/noyau/libpromethee_noyau.a nlohmann_json::nlohmann_json ${OCCT})
  set(LIENS_AUDIT audit_promethee)
  enable_testing()
else()
  set(LIENS_AUDIT Promethee::noyau Promethee::geometrie)
endif()
find_package(Catch2 3 REQUIRED)
add_executable(essais_audit essais_esquisse.cpp essais_fonctions3d.cpp essais_assemblage.cpp essais_plan_exports.cpp
               essais_performances.cpp)
target_include_directories(essais_audit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(essais_audit PRIVATE ${LIENS_AUDIT} Catch2::Catch2WithMain)
if(WIN32 AND EXISTS ${PROJECT_SOURCE_DIR}/cmake/utf8.manifest)
  target_sources(essais_audit PRIVATE ${PROJECT_SOURCE_DIR}/cmake/utf8.manifest)
endif()
include(Catch)
# [gel] : l'essai ES-26b ne rend jamais la main tant que ES-26 n'est pas corrigé ; on l'écarte de CTest.
catch_discover_tests(essais_audit TEST_SPEC "~[gel]" TEST_PREFIX "audit:" DISCOVERY_MODE PRE_TEST)

cmake_minimum_required(VERSION 3.22)
project(Promethee VERSION 0.33.0 LANGUAGES CXX
        DESCRIPTION "Plateforme libre d'ingénierie intégrée : carte et boîtier dans un seul modèle")

# Windows : évite le plantage connu des programmes compilés avec MSVC 17.10+ face à une ancienne msvcp140.dll.
if(MSVC)
  add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de compilation" FORCE)
endif()

option(PROMETHEE_TESTS "Compiler les tests" ON)
option(PROMETHEE_APP "Compiler l'application de bureau (Qt 6)" ON)

if(MSVC)
  add_compile_options(/W4 /utf-8 /permissive-)
else()
  # Les agrégats du modèle ont des membres à valeur par défaut, pensés pour une initialisation partielle
  add_compile_options(-Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

# Identifiant de build : version et révision des sources (commit), régénérés à chaque compilation par
# cmake/version.cmake ; seul ce qui en dépend est recompilé quand la révision change.
set(PROMETHEE_GENERE ${PROJECT_BINARY_DIR}/genere)
set(PROMETHEE_ARGS_VERSION -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DSORTIE=${PROMETHEE_GENERE} -DVERSION=${PROJECT_VERSION})
execute_process(COMMAND ${CMAKE_COMMAND} ${PROMETHEE_ARGS_VERSION} -P ${PROJECT_SOURCE_DIR}/cmake/version.cmake)
add_custom_target(promethee_version
  COMMAND ${CMAKE_COMMAND} ${PROMETHEE_ARGS_VERSION} -P ${PROJECT_SOURCE_DIR}/cmake/version.cmake
  BYPRODUCTS ${PROMETHEE_GENERE}/version.cpp ${PROMETHEE_GENERE}/promethee.rc ${PROMETHEE_GENERE}/version.txt
  COMMENT "Identifiant de build" VERBATIM)

find_package(nlohmann_json 3.10 REQUIRED)
find_package(OpenCASCADE REQUIRED)

add_subdirectory(noyau)
add_subdirectory(geometrie)
add_subdirectory(outils)
if(PROMETHEE_APP)
  add_subdirectory(app)
endif()

if(PROMETHEE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

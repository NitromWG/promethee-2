# Prométhée : identifiant de build (version, révision des sources, numéro de build). Licence GPL-3.0-only.
# Lancé à chaque compilation (cmake -P) : les fichiers générés ne sont réécrits que si leur contenu change,
# si bien qu'une compilation sans nouveau commit ne recompile rien.
#
# Entrées : SOURCE_DIR, SORTIE, VERSION (x.y.z).
# Révision, par ordre de préférence : variable d'environnement PROMETHEE_REVISION, GITHUB_SHA (intégration
# continue), « git rev-parse » si les sources sont un dépôt git, sinon « hors-depot ».
# Numéro de build : GITHUB_RUN_NUMBER, sinon 0. Date de la révision : celle du commit, ou SOURCE_DATE_EPOCH.

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED SORTIE OR NOT DEFINED VERSION)
  message(FATAL_ERROR "version.cmake : SOURCE_DIR, SORTIE et VERSION sont requis")
endif()

set(REVISION "")
set(DATE_REVISION "")
set(MODIFIEE FALSE)

if(DEFINED ENV{PROMETHEE_REVISION} AND NOT "$ENV{PROMETHEE_REVISION}" STREQUAL "")
  set(REVISION "$ENV{PROMETHEE_REVISION}")
endif()

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${SOURCE_DIR}/.git")
  if(REVISION STREQUAL "")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --short=12 HEAD
                    OUTPUT_VARIABLE REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE r)
    if(NOT r EQUAL 0)
      set(REVISION "")
    endif()
  endif()
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" log -1 --format=%cI
                  OUTPUT_VARIABLE DATE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  # Sources modifiées depuis le commit (fichiers suivis seulement) : le binaire ne correspond à aucune révision publiée.
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" status --porcelain --untracked-files=no
                  OUTPUT_VARIABLE etat OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE r)
  if(r EQUAL 0 AND NOT etat STREQUAL "")
    set(MODIFIEE TRUE)
  endif()
endif()

if(REVISION STREQUAL "" AND DEFINED ENV{GITHUB_SHA} AND NOT "$ENV{GITHUB_SHA}" STREQUAL "")
  string(SUBSTRING "$ENV{GITHUB_SHA}" 0 12 REVISION)
endif()
if(REVISION STREQUAL "")
  set(REVISION "hors-depot")
endif()
if(MODIFIEE)
  string(APPEND REVISION "-modifiee")
endif()
if(DATE_REVISION STREQUAL "" AND DEFINED ENV{SOURCE_DATE_EPOCH})
  string(TIMESTAMP DATE_REVISION "%Y-%m-%dT%H:%M:%SZ" UTC)
endif()

set(NUMERO_BUILD 0)
if(DEFINED ENV{GITHUB_RUN_NUMBER} AND "$ENV{GITHUB_RUN_NUMBER}" MATCHES "^[0-9]+$")
  set(NUMERO_BUILD "$ENV{GITHUB_RUN_NUMBER}")
  # Champ de version Windows limité à 16 bits.
  if(NUMERO_BUILD GREATER 65535)
    math(EXPR NUMERO_BUILD "${NUMERO_BUILD} % 65536")
  endif()
endif()

string(REPLACE "." ";" morceaux "${VERSION}")
list(GET morceaux 0 VERSION_MAJEURE)
list(GET morceaux 1 VERSION_MINEURE)
list(GET morceaux 2 VERSION_CORRECTIVE)
set(IDENTIFIANT "${VERSION}+${REVISION}")
if(NOT NUMERO_BUILD EQUAL 0)
  set(IDENTIFIANT "${VERSION}+${REVISION}.${NUMERO_BUILD}")
endif()

file(MAKE_DIRECTORY "${SORTIE}")
configure_file("${SOURCE_DIR}/cmake/version.cpp.in" "${SORTIE}/version.cpp" @ONLY)
configure_file("${SOURCE_DIR}/app/promethee.rc.in" "${SORTIE}/promethee.rc" @ONLY)
# Pour les scripts d'intégration continue (nom du paquet, LISEZMOI) : une ligne « clé=valeur » par champ.
file(WRITE "${SORTIE}/version.txt.tmp" "version=${VERSION}\nrevision=${REVISION}\ndate=${DATE_REVISION}\nbuild=${NUMERO_BUILD}\nidentifiant=${IDENTIFIANT}\n")
configure_file("${SORTIE}/version.txt.tmp" "${SORTIE}/version.txt" COPYONLY)
file(REMOVE "${SORTIE}/version.txt.tmp")

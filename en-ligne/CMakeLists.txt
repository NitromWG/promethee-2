# Programme séparé d'accès en ligne (décision D13, lot L4 ; 4.22, 19.14). Seul programme de Prométhée qui ouvre le
# réseau, et seulement si l'utilisateur l'active, fonction par fonction : il lit et remplit les fichiers du dossier
# d'équipe (« Vers le PLM », « Depuis le PLM »), Prométhée ne voit que ces fichiers. Il vit hors des dossiers que
# l'essai « source_aucun_acces_reseau » parcourt (app, noyau, geometrie, outils). Facultatif : sans Qt Network, il
# n'est pas compilé et tout continue de marcher par fichiers.
find_package(Qt6 6.2 QUIET COMPONENTS Core Network)
if(NOT Qt6Network_FOUND)
  message(STATUS "Qt Network introuvable : le programme d'accès en ligne n'est pas compilé.")
  return()
endif()
find_package(Qt6 6.2 QUIET COMPONENTS Sql)
add_executable(promethee-en-ligne main.cpp AccesEnLigne.cpp AccesEnLigne.hpp Gabarits.cpp)
target_link_libraries(promethee-en-ligne PRIVATE Promethee::noyau Qt6::Core Qt6::Network)
if(Qt6Sql_FOUND)
  target_link_libraries(promethee-en-ligne PRIVATE Qt6::Sql)
  target_compile_definitions(promethee-en-ligne PRIVATE PROMETHEE_EN_LIGNE_SQL=1)
endif()
# À côté du programme principal, dans l'arbre de compilation comme à l'installation
set_target_properties(promethee-en-ligne PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/app)
install(TARGETS promethee-en-ligne RUNTIME DESTINATION bin)

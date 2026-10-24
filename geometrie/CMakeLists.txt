add_library(promethee_geometrie src/geometrie.cpp src/echanges.cpp src/construction.cpp src/dessin.cpp src/assemblage.cpp src/fabrication.cpp src/carte3d.cpp src/mince.cpp src/tolerie.cpp src/import_cao.cpp src/mesure.cpp src/cotes_modele.cpp src/distances3d.cpp src/rendu_carte.cpp)
add_library(Promethee::geometrie ALIAS promethee_geometrie)
target_include_directories(promethee_geometrie PUBLIC include)
target_include_directories(promethee_geometrie SYSTEM PUBLIC ${OpenCASCADE_INCLUDE_DIR})

# OpenCascade 7.8 a regroupé les boîtes à outils d'échange de données (TKDESTEP, TKDESTL).
# On prend celles qui existent : 7.6 (Ubuntu 24.04) comme 8.0 (vcpkg, Homebrew).
set(PROMETHEE_OCCT TKHLR TKOffset TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO TKBool
    TKShHealing TKMesh TKXSBase TKLCAF TKCAF TKCDF TKXCAF)
if(TARGET TKDESTEP)
  list(APPEND PROMETHEE_OCCT TKDE TKDESTEP TKDESTL TKDEIGES)
else()
  list(APPEND PROMETHEE_OCCT TKSTEPBase TKSTEPAttr TKSTEP209 TKSTEP TKSTL TKXDESTEP TKIGES TKXDEIGES)
endif()
target_link_libraries(promethee_geometrie PUBLIC Promethee::noyau ${PROMETHEE_OCCT})
message(STATUS "OpenCascade ${OpenCASCADE_VERSION} : ${PROMETHEE_OCCT}")

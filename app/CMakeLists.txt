# Application de bureau. Facultative : sans Qt 6, le reste du projet se compile quand même.
find_package(Qt6 6.2 QUIET COMPONENTS Widgets OpenGLWidgets)
if(NOT Qt6_FOUND)
  message(STATUS "Qt 6 introuvable : l'application de bureau n'est pas compilée.")
  return()
endif()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(OpenGL REQUIRED)

set(PROMETHEE_OCCT_VISU TKService TKV3d TKOpenGl)
add_executable(promethee_app WIN32 MACOSX_BUNDLE
  main.cpp Document.cpp VueCarte.cpp Vue3D.cpp OcctGlTools.cpp Panneau.cpp FenetrePrincipale.cpp DialogueExport.cpp
  VuePiece.cpp Esquisseur.cpp AtelierPiece.cpp ChampValeur.hpp Journal.cpp EssaisInterface.cpp Icones.cpp Accueil.cpp Navigateur.cpp MiseEnPlan.cpp Qet.cpp AtelierAssemblage.cpp Nomenclature.cpp BarreTitre.cpp Coquille.cpp FenetreMenusAtelier.cpp EtiquetteEtat.hpp RemplacerPage.hpp VueSchema.cpp VueSchemaElectrique.cpp VueCircuit.cpp SelectionElectronique.cpp FiltreSelection.cpp NavigationVue.cpp FenetreReglages.cpp EditeurRuban.cpp EvenementsReels.cpp OutilsSchema.cpp MenuSchema.cpp CollageSchema.cpp SelectionAvancee.cpp MemoiresSelection.cpp CoordonneesSchema.cpp PeripheriquesEntree.cpp DescriptionObjet.cpp AccessibiliteCanevas.cpp Vignettes.cpp ThemeJetons.cpp RegistreReglages.cpp PanneauVerifications.cpp PanneauEcarts.cpp PanneauRecherche.cpp CadreVue.cpp AideElectronique.cpp ParcoursElectronique.cpp OutilsCircuit.cpp BandeauOutil.cpp RegistreActions.cpp ListeRaccourcis.cpp InspecteurElectronique.cpp InspecteurSchema.cpp InspecteurCircuit.cpp PanneauBibliotheque.cpp GlisserArticle.cpp ApercuArticle.cpp DispositionsAtelier.cpp ZoneMessages.cpp AtelierElectronique.cpp RubanElectronique.cpp DescriptionRuban.cpp ExportsElectronique.cpp PublicationElectronique.cpp DialogueComposant.cpp DialogueRemplacer.cpp DialogueAppariement.cpp DialogueVersionPrecedente.cpp TableauChamps.cpp TableauObjets.cpp MosaiqueVues.cpp DialogueEmpreinte.cpp DialogueImportDxf.cpp DialogueNomenclature.cpp EssaisElectronique.cpp EssaisElectroniqueL1.cpp EssaisElectroniqueL2.cpp EssaisElectroniqueL3.cpp FenetreChoixComposant.cpp FenetreChoixEmpreinte.cpp DialoguesArticles.cpp AtelierElectroniqueArticles.cpp CartoucheRoutage.cpp PanneauAPlacer.cpp FenetreRapport.cpp DialogueCercle.cpp EssaisElectroniqueL5.cpp EssaisElectroniqueL4.cpp EssaisAudit.cpp EssaisElectrique.cpp Recuperation.cpp Theme.cpp PanneauLateral.cpp AtelierElectrique.cpp AtelierElectriquePages.cpp AtelierElectriqueDistribution.cpp AtelierElectriquePV.cpp AtelierElectriqueParcours.cpp AtelierElectriqueGrafcet.cpp AtelierElectriqueAppareils.cpp FormulaireAppareil.cpp FormulaireLie.cpp PeintreDxf.cpp VueGrafcet.cpp VueArmoire.cpp VueFaisceau.cpp ressources.qrc)
set_target_properties(promethee_app PROPERTIES OUTPUT_NAME Promethee)
target_link_libraries(promethee_app PRIVATE Promethee::noyau Promethee::geometrie Qt6::Widgets Qt6::OpenGLWidgets OpenGL::GL ${PROMETHEE_OCCT_VISU})
if(WIN32)
  target_sources(promethee_app PRIVATE ${PROJECT_SOURCE_DIR}/cmake/utf8.manifest ${PROMETHEE_GENERE}/promethee.rc)
  add_dependencies(promethee_app promethee_version)
endif()
if(MSVC)
  # Version de l'image dans l'en-tête PE, en plus de la ressource VERSIONINFO
  target_link_options(promethee_app PRIVATE /VERSION:${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
endif()
install(TARGETS promethee_app RUNTIME DESTINATION bin BUNDLE DESTINATION .)

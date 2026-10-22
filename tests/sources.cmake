# Lecture du source (lot L1, tâche 1.16). ERG-18 : aucune vue ne déplace le pointeur, nulle part dans
# l'application. ERG-17 : dans l'atelier électronique, aucune touche n'est comparée hors du registre des actions
# (RegistreActions) ; une ligne qui le fait pour un champ de saisie le dit par le commentaire « champ de saisie ».
# L'échec nomme le fichier et la ligne.
file(GLOB sources ${APP}/*.cpp ${APP}/*.hpp)
set(erreurs "")
foreach(f ${sources})
  file(STRINGS ${f} lignes REGEX "(QCursor::setPos|XWarpPointer|SetCursorPos|CGWarpMouseCursorPosition)[ \t]*\\(")
  foreach(l ${lignes})
    string(STRIP "${l}" l)
    if(NOT l MATCHES "^//")
      get_filename_component(nom ${f} NAME)
      string(APPEND erreurs "ERG-18 : ${nom} déplace le pointeur : ${l}\n")
    endif()
  endforeach()
endforeach()
set(atelier VueSchema.cpp VueCircuit.cpp OutilsSchema.cpp OutilsCircuit.cpp AtelierElectronique.cpp RubanElectronique.cpp
    ExportsElectronique.cpp PublicationElectronique.cpp InspecteurElectronique.cpp InspecteurSchema.cpp InspecteurCircuit.cpp
    PanneauBibliotheque.cpp ApercuArticle.cpp DispositionsAtelier.cpp BandeauOutil.cpp ZoneMessages.cpp FenetreMenusAtelier.cpp ListeRaccourcis.cpp SelectionElectronique.cpp
    MenuSchema.cpp CollageSchema.cpp SelectionAvancee.cpp MemoiresSelection.cpp CoordonneesSchema.cpp PeripheriquesEntree.cpp DescriptionObjet.cpp AccessibiliteCanevas.cpp Vignettes.cpp ThemeJetons.cpp RegistreReglages.cpp GlisserArticle.cpp PanneauVerifications.cpp PanneauRecherche.cpp CadreVue.cpp AideElectronique.cpp ParcoursElectronique.cpp)
foreach(nom ${atelier})
  file(STRINGS ${APP}/${nom} lignes REGEX "(==|!=)[ \t]*Qt::Key_|Qt::Key_[A-Za-z0-9_]+[ \t]*(==|!=)|case[ \t]+Qt::Key_")
  foreach(l ${lignes})
    if(NOT l MATCHES "champ de saisie")
      string(STRIP "${l}" l)
      string(APPEND erreurs "ERG-17 : ${nom} compare une touche hors du registre des actions : ${l}\n")
    endif()
  endforeach()
endforeach()
if(erreurs)
  message(FATAL_ERROR "${erreurs}")
endif()
message(STATUS "Source conforme : le pointeur n'est jamais déplacé, les touches de l'atelier électronique passent par le registre")

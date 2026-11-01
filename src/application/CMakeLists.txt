# compile the icons and the user manual into the executable
COPPERSPICE_RESOURCES(${CMAKE_CURRENT_SOURCE_DIR}/icons/icons.qrc ${CMAKE_CURRENT_SOURCE_DIR}/help/help.qrc)

# the macro only depends on the .qrc file itself; also rerun rcc when one of the files it lists changes
file(GLOB DV_ICON_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/icons/*.png)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/qrc_icons.cpp APPEND DEPENDS ${DV_ICON_FILES})
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/qrc_help.cpp APPEND DEPENDS ${PROJECT_SOURCE_DIR}/docs/readme.html)

add_executable(debugvision WIN32 MACOSX_BUNDLE
  main.cpp
  FilterDialog.cpp
  Help.cpp
  LogViewWidget.cpp
  MainWindow.cpp
  RunDialog.cpp
  SourcesDialog.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/qrc_icons.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/qrc_help.cpp
)

if (WIN32)
  target_sources(debugvision PRIVATE icons/debugvision.rc)
endif()

target_link_libraries(debugvision PRIVATE
  dvcore
  project::warnings_options
  project::warning_suppressions
  project::project_options
  CopperSpice::CsCore
  CopperSpice::CsGui
)

if (WIN32 AND DV_SELF_CONTAINED)
  # src/launcher embeds the application and its libraries into the installed debugvision.exe
  return()
endif()

install(TARGETS debugvision
  RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}
  BUNDLE DESTINATION ${CMAKE_INSTALL_PREFIX}
)

# Copy the libraries and plugins necessary for our application to our install path.
cs_copy_library(CsCore)
cs_copy_library(CsGui)
cs_copy_plugins(CsGui)

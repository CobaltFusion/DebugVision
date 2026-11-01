# compile the icons into the executable
COPPERSPICE_RESOURCES(${CMAKE_CURRENT_SOURCE_DIR}/icons/icons.qrc)

add_executable(debugvision WIN32 MACOSX_BUNDLE
  main.cpp
  FilterDialog.cpp
  LogViewWidget.cpp
  MainWindow.cpp
  RunDialog.cpp
  SourcesDialog.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/qrc_icons.cpp
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

install(TARGETS debugvision
  RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}
  BUNDLE DESTINATION ${CMAKE_INSTALL_PREFIX}
)

# Copy the libraries and plugins necessary for our application to our install path.
cs_copy_library(CsCore)
cs_copy_library(CsGui)
cs_copy_plugins(CsGui)

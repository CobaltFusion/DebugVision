add_executable(debugvision WIN32 MACOSX_BUNDLE
  main.cpp
  FilterDialog.cpp
  LogViewWidget.cpp
  MainWindow.cpp
  RunDialog.cpp
  SourcesDialog.cpp
)

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

# the user manual, shown by --webhelp
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/manual.cpp
  COMMAND ${CMAKE_COMMAND} -DINPUT=${PROJECT_SOURCE_DIR}/docs/readme.html -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual.cpp -DNAME=userManual -P ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
  DEPENDS ${PROJECT_SOURCE_DIR}/docs/readme.html ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
)

add_executable(dvcli
  main.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/manual.cpp
)

target_link_libraries(dvcli PRIVATE
  dvcore
  project::warnings_options
  project::warning_suppressions
)

install(TARGETS dvcli DESTINATION ${CMAKE_INSTALL_PREFIX})

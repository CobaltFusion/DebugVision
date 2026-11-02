# the user manual, shown by --webhelp
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/manual.cpp
  COMMAND ${CMAKE_COMMAND} -DINPUT=${DV_MANUAL_HTML} -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/manual.cpp -DNAME=userManual -P ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
  DEPENDS ${DV_MANUAL_HTML} ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
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

add_dependencies(dvcli dv_manual)

install(TARGETS dvcli DESTINATION ${CMAKE_INSTALL_PREFIX})

add_test(NAME dvcli_stdin_end COMMAND ${CMAKE_COMMAND} -DDVCLI=$<TARGET_FILE:dvcli> -P ${CMAKE_CURRENT_SOURCE_DIR}/test_stdin_end.cmake)

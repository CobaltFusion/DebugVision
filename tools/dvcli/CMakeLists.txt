add_executable(dvcli
  main.cpp
)

target_link_libraries(dvcli PRIVATE
  dvcore
  project::warnings_options
  project::warning_suppressions
)

install(TARGETS dvcli DESTINATION ${CMAKE_INSTALL_PREFIX})

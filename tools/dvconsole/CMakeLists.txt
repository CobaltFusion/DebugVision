add_executable(dvconsole
  main.cpp
)

target_link_libraries(dvconsole PRIVATE
  dvcore
  project::warnings_options
  project::warning_suppressions
)

install(TARGETS dvconsole DESTINATION ${CMAKE_INSTALL_PREFIX})

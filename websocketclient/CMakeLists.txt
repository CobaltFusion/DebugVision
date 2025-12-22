
add_executable(cpp_example
  main.cpp
)

target_link_libraries(cpp_example PRIVATE
  project::warnings_options
  project::warning_suppressions
  project::project_options
)

install(TARGETS cpp_example DESTINATION ${CMAKE_INSTALL_PREFIX})


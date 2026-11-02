add_executable(dvtest
  main.cpp
)

target_link_libraries(dvtest PRIVATE
  dvcore
  project::warnings_options
  project::warning_suppressions
)

install(TARGETS dvtest DESTINATION ${CMAKE_INSTALL_PREFIX})

add_test(NAME dvtest_sequence COMMAND dvtest --count 50 --events 200 --tick 5 --seed 7 --speed 0)

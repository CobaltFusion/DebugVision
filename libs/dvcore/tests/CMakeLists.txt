add_executable(dvcore_tests
  main.cpp
  test_encoding.cpp
  test_linestore.cpp
  test_fileformat.cpp
  test_filters.cpp
  test_matcher.cpp
  test_timeline.cpp
  test_sourceconfig.cpp
  test_eventloop.cpp
  test_sources.cpp
)

target_link_libraries(dvcore_tests PRIVATE
  dvcore
  doctest::doctest
  project::warning_suppressions
)

target_compile_definitions(dvcore_tests PRIVATE DV_TESTFILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testfiles")

add_test(NAME dvcore_tests COMMAND dvcore_tests)

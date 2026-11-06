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
  test_http.cpp
)

target_link_libraries(dvcore_tests PRIVATE
  dvcore
  doctest::doctest
  project::warning_suppressions
)

# the HTTP test uses parseContentRange from the private sources header
target_include_directories(dvcore_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_compile_definitions(dvcore_tests PRIVATE DV_TESTFILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testfiles")

add_test(NAME dvcore_tests COMMAND dvcore_tests)

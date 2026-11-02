add_library(dvcore STATIC
  src/Clock.cpp
  src/Encoding.cpp
  src/FileFormat.cpp
  src/Filter.cpp
  src/FilterEngine.cpp
  src/Ingest.cpp
  src/LineStore.cpp
  src/LogView.cpp
  src/Matcher.cpp
  src/Settings.cpp
  src/Source.cpp
  src/SourceConfig.cpp
  src/sources/FileSource.cpp
  src/sources/NetworkSources.cpp
  src/sources/ProcessSources.cpp
)

if (WIN32)
  target_sources(dvcore PRIVATE
    src/platform/win32/EventLoopWin32.cpp
    src/platform/win32/FileWin32.cpp
    src/platform/win32/MiscWin32.cpp
    src/platform/win32/SocketWin32.cpp
    src/sources/OutputDebugStringSource.cpp
  )
  target_link_libraries(dvcore PRIVATE ws2_32 psapi shell32 ole32)
else()
  target_sources(dvcore PRIVATE
    src/platform/posix/FilePosix.cpp
    src/platform/posix/LegacyCodecPosix.cpp
    src/platform/posix/SocketPosix.cpp
  )
  if (APPLE)
    target_sources(dvcore PRIVATE src/platform/macos/EventLoopMacos.cpp)
    target_link_libraries(dvcore PRIVATE iconv)
  else()
    target_sources(dvcore PRIVATE src/platform/linux/EventLoopLinux.cpp)
  endif()
  find_package(Threads REQUIRED)
  target_link_libraries(dvcore PUBLIC Threads::Threads)
endif()

target_include_directories(dvcore
  PUBLIC include
  PRIVATE src
)

target_compile_definitions(dvcore PRIVATE PCRE2_CODE_UNIT_WIDTH=8 PCRE2_STATIC)

target_link_libraries(dvcore
  PUBLIC
    nlohmann_json::nlohmann_json
    project::project_options
  PRIVATE
    pcre2-8-static
    project::warnings_options
    project::warning_suppressions
)

if (DV_BUILD_TESTS)
  add_subdirectory(tests)
endif()

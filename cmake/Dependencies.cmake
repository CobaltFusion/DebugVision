# Third party dependencies, pinned to fixed versions

include(FetchContent)

# CMake 4 rejects the old cmake_minimum_required() versions used by the pinned doctest and PCRE2 releases
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL
)

set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_JIT ON CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_UNICODE ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PCRE2_STATIC_PIC ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
FetchContent_Declare(pcre2
  URL https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.44/pcre2-10.44.tar.bz2
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL
)

FetchContent_Declare(doctest
  URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL
)

# only the sources: docs/CMakeLists.txt builds md2html from them, md4c's own CMakeLists.txt changes
# global settings (BUILD_SHARED_LIBS, the MSVC runtime); SOURCE_SUBDIR without a CMakeLists.txt skips it
FetchContent_Declare(md4c
  URL https://github.com/mity/md4c/archive/refs/tags/release-0.5.2.tar.gz
  URL_HASH SHA256=55d0111d48fb11883aaee91465e642b8b640775a4d6993c2d0e7a8092758ef21
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR none
)

FetchContent_MakeAvailable(nlohmann_json pcre2 md4c)

include(${CMAKE_CURRENT_LIST_DIR}/Curl.cmake)

if (DV_BUILD_TESTS)
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(doctest)
endif()

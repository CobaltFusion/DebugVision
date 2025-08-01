include(FetchContent)

message("--- MSVC Zero Configuration Script")

if(NOT CMAKE_BUILD_TYPE)
  message(FATAL_ERROR "No build type specified, ie. specify -DCMAKE_BUILD_TYPE=Release to build in release configuration.")
endif()

Set(CS_URL "https://github.com/janwilmans/copperspice-gettingstarted/raw/develop/example_project_cmake/nuget/copperspice.2.0.0.nupkg")
Set(NUGET_PACKAGE_PATH ${CMAKE_SOURCE_DIR}/packages)

FetchContent_Declare(copperspice_nuget
   DOWNLOAD_EXTRACT_TIMESTAMP TRUE
   SOURCE_DIR ${NUGET_PACKAGE_PATH}/copperspice
   URL ${CS_URL}
   EXCLUDE_FROM_ALL
)

message("--- NUGET restore: ${CS_URL}")
FetchContent_MakeAvailable(copperspice_nuget)
message("--- NUGET done")

if(CMAKE_PREFIX_PATH)
  message("Getting CMAKE_PREFIX_PATH as ${CMAKE_PREFIX_PATH}")
else()
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_PREFIX_PATH "${NUGET_PACKAGE_PATH}/copperspice/build/native/debug/cmake")
  else()
    set(CMAKE_PREFIX_PATH "${NUGET_PACKAGE_PATH}/copperspice/build/native/release/cmake")
  endif()
  message("Setting CMAKE_PREFIX_PATH to ${CMAKE_PREFIX_PATH}")
endif()

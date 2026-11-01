cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

project(debugvision LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT "${CMAKE_GENERATOR}" MATCHES "Visual Studio")
    message("CMAKE_GENERATOR: ${CMAKE_GENERATOR}")
    message(FATAL_ERROR "No build type specified, ie. use -DCMAKE_BUILD_TYPE=<config> typical values are Debug, Release, RelWithDebInfo and MinSizeRel")
endif()

option(DV_BUILD_GUI "Build the CopperSpice GUI application (requires CopperSpice)" ON)
option(DV_BUILD_TESTS "Build the dvcore unit tests" ON)
option(DV_BUILD_EXAMPLES "Build the websocket example" OFF)

if (MSVC)
  # only fall back to the copperspice nuget package when no CMAKE_PREFIX_PATH was given
  if (DV_BUILD_GUI AND NOT CMAKE_PREFIX_PATH)
    include(cmake/CMakeLists.msvc.zero.config.cmake)
  endif()
  add_compile_options(/MP)
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "installation path" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (DV_BUILD_GUI)
  find_package(CopperSpice REQUIRED)
endif()

include(cmake/CMakeLists.warnings.cmake)
include(cmake/Dependencies.cmake)

add_library(define_project_options INTERFACE)

if(BUILD_WITH_ASAN)
    target_compile_options(define_project_options INTERFACE -fsanitize=address)
    target_link_options(define_project_options INTERFACE -fsanitize=address)
    message("-- build with ASAN enabled")
endif()

if(BUILD_WITH_UBSAN)
    target_compile_options(define_project_options INTERFACE -fsanitize=undefined)
    target_link_options(define_project_options INTERFACE -fsanitize=undefined)
    message("-- build with UBSAN enabled")
endif()

# Make the application look for the CopperSpice libraries and plugins in its own directory first.
if (APPLE)
  set(CMAKE_INSTALL_RPATH "@loader_path;@loader_path/../Frameworks")
else()
  set(CMAKE_INSTALL_RPATH "\$ORIGIN")
endif()

if (MSVC)
  target_compile_options(define_project_options INTERFACE /utf-8 /permissive- /Zc:__cplusplus)
  target_compile_definitions(define_project_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

# here the 'project::' scoped targets are available, all project should only depend on these as needed through its target_link_libraries
# doing so will automatically have them add their compiler and linker option as defined above.

add_library(project::warnings_options ALIAS define_project_warning_options)
add_library(project::warning_suppressions ALIAS define_project_warning_suppressions)
add_library(project::project_options ALIAS define_project_options)

enable_testing()

add_subdirectory(libs/dvcore)
add_subdirectory(tools/dvconsole)

if (DV_BUILD_GUI)
  add_subdirectory(src)
endif()

if (DV_BUILD_EXAMPLES)
  add_subdirectory(websocketclient)
endif()

if(${CMAKE_SIZEOF_VOID_P} EQUAL 4)
   set(TARGETBITS 32)
else()
   set(TARGETBITS 64)
endif()

message("")
message("${PROJECT_NAME} configured to run on:  ${CMAKE_SYSTEM_NAME} ${TARGETBITS} bit, ${CMAKE_BUILD_TYPE} Mode")
message("${PROJECT_NAME} will be built in:      '${CMAKE_BINARY_DIR}'")
message("CMAKE_C_COMPILER:                  '${CMAKE_C_COMPILER}'")
message("CMAKE_CXX_COMPILER:                '${CMAKE_CXX_COMPILER}'")
message("${PROJECT_NAME} will be installed in:  '${CMAKE_INSTALL_PREFIX}'")
message("\n")

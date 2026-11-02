cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

project(debugvision LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT "${CMAKE_GENERATOR}" MATCHES "Visual Studio")
    message("CMAKE_GENERATOR: ${CMAKE_GENERATOR}")
    message(FATAL_ERROR "No build type specified, ie. use -DCMAKE_BUILD_TYPE=<config> typical values are Debug, Release, RelWithDebInfo and MinSizeRel")
endif()

# COPPERSPICE_DIR is the CopperSpice install prefix, taken from -DCOPPERSPICE_DIR=..., the COPPERSPICE_DIR environment variable or /opt/copperspice
set(DV_DEFAULT_COPPERSPICE_DIR "")
if (DEFINED ENV{COPPERSPICE_DIR})
  set(DV_DEFAULT_COPPERSPICE_DIR "$ENV{COPPERSPICE_DIR}")
elseif (EXISTS "/opt/copperspice")
  set(DV_DEFAULT_COPPERSPICE_DIR "/opt/copperspice")
endif()
set(COPPERSPICE_DIR "${DV_DEFAULT_COPPERSPICE_DIR}" CACHE PATH "CopperSpice install prefix")

# build the GUI by default only when a CopperSpice install was specified or found (MSVC can fall back to the nuget package)
if (COPPERSPICE_DIR OR CopperSpice_DIR OR CMAKE_PREFIX_PATH OR MSVC)
  set(DV_BUILD_GUI_DEFAULT ON)
else()
  set(DV_BUILD_GUI_DEFAULT OFF)
endif()

option(DV_BUILD_GUI "Build the GUI application (requires CopperSpice)" ${DV_BUILD_GUI_DEFAULT})
option(DV_BUILD_TESTS "Build the dvcore unit tests" ON)
option(DV_BUILD_EXAMPLES "Build the websocket example" OFF)
option(DV_SELF_CONTAINED "Windows: install the GUI as one debugvision.exe that unpacks its DLLs to a temporary directory" ON)

if (MSVC)
  # only fall back to the copperspice nuget package when no CopperSpice install was given
  if (DV_BUILD_GUI AND NOT CMAKE_PREFIX_PATH AND NOT COPPERSPICE_DIR AND NOT CopperSpice_DIR)
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
  find_package(CopperSpice REQUIRED PATHS ${COPPERSPICE_DIR} PATH_SUFFIXES cmake/CopperSpice lib/cmake/CopperSpice lib64/cmake/CopperSpice)
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

# the user manual, generated from docs/manual.md (target dv_manual)
set(DV_MANUAL_HTML ${PROJECT_BINARY_DIR}/docs/manual.html)
add_subdirectory(docs)

add_subdirectory(libs/dvcore)
add_subdirectory(tools/dvcli)
add_subdirectory(tools/dvtest)

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

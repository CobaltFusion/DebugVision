cmake_minimum_required(VERSION 3.31 FATAL_ERROR)

project(debugvision)

if(NOT CMAKE_BUILD_TYPE)
  message(FATAL_ERROR "No build type specified, ie. use -DCMAKE_BUILD_TYPE=<config> typical values are Debug, Release, RelWithDebInfo and MinSizeRel")
endif()

if (MSVC)
  include(cmake/CMakeLists.msvc.zero.config.cmake)
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "installation path" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CopperSpice REQUIRED)

include(cmake/CMakeLists.warnings.cmake)

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

# Setting the rpath to '$ORIGIN' ensures our application looks for the
# CopperSpice libraries and plugins in its own directory first.
set(CMAKE_INSTALL_RPATH "\$ORIGIN")

target_compile_options(define_project_options INTERFACE /utf-8)

# here the 'project::' scoped targets are available, all project should only depend on these as needed through its target_link_libraries
# doing so will automatically have them add their compiler and linker option as defined above.

add_library(project::warnings_options ALIAS define_project_warning_options)
add_library(project::warning_suppressions ALIAS define_project_warning_suppressions)
add_library(project::project_options ALIAS define_project_options)

add_subdirectory(src)

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

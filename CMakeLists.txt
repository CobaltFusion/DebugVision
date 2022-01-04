cmake_minimum_required(VERSION 3.12)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# may be enable later once we have a way to install them.
#set(Boost_USE_STATIC_LIBS ON)
#find_package(Boost REQUIRED COMPONENTS system thread filesystem regex program_options)
#find_package(GTest REQUIRED)
#find_package(GMock REQUIRED)
#find_package(fmt REQUIRED)

option(BUILD_WITH_ASAN "Build with address sanitizer" ON)
option(BUILD_WITH_UBSAN "Build with undefined behaviour sanitizer" ON)
option(BUILD_WITH_TSAN "Build with address sanitizer" OFF)

add_compile_options(-Wall -Wextra -pedantic -Wmissing-include-dirs -Wformat=2 -Wunused -Wno-error=unused-variable -Wcast-align -Wno-vla -Wnull-dereference -Wmaybe-uninitialized)
add_compile_options(-Werror -fdiagnostics-color=auto)

if(BUILD_WITH_ASAN)
    add_compile_options(-fsanitize=address)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif(BUILD_WITH_ASAN)

if(BUILD_WITH_UBSAN)
    add_compile_options(-fsanitize=undefined)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
endif(BUILD_WITH_UBSAN)

if(BUILD_WITH_TSAN)
    add_compile_options(-fsanitize=thread)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif(BUILD_WITH_TSAN)

add_subdirectory(tools)

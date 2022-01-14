cmake_minimum_required(VERSION 3.20)
project(DebugVision)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# may be enable later once we have a way to install them.
#set(Boost_USE_STATIC_LIBS ON)
#find_package(Boost REQUIRED COMPONENTS system thread filesystem regex program_options)
#find_package(GTest REQUIRED)
#find_package(GMock REQUIRED)
#find_package(fmt REQUIRED)

option(BUILD_WITH_ASAN "Build with address sanitizer" OFF)
option(BUILD_WITH_UBSAN "Build with undefined behaviour sanitizer" OFF)
option(BUILD_WITH_TSAN "Build with address sanitizer" OFF)

# Qt
find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets Concurrent REQUIRED)
message("QT_VERSION=${QT_VERSION_MAJOR}.${QT_VERSION_MINOR}")

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/W4 /external:W3 /WX /permissive-)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /GL /Ox")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG")
    if (QT_VERSION_MAJOR EQUAL 5)
        # with Qt5, setting /external:W3 is not enough to get rid of all warnings
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd5054")
    endif()
else()
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
endif()

add_subdirectory(tools)

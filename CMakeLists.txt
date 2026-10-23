cmake_minimum_required(VERSION 3.28)
project(ie3ogre-recomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# SDL3 for the game and the launcher: window, input, timing. MinGW cross builds use the official
# SDL3-devel-*-mingw package (SDL3.dll must ship next to boej.exe).
set(NTR_SDL3_VERSION 3.4.18)
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND AND MINGW)
    include(FetchContent)
    FetchContent_Declare(sdl3_mingw
        URL https://github.com/libsdl-org/SDL/releases/download/release-${NTR_SDL3_VERSION}/SDL3-devel-${NTR_SDL3_VERSION}-mingw.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP ON)
    FetchContent_MakeAvailable(sdl3_mingw)
    find_package(SDL3 CONFIG REQUIRED PATHS "${sdl3_mingw_SOURCE_DIR}/x86_64-w64-mingw32"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
endif()
if(NOT SDL3_FOUND)
    find_package(SDL3 CONFIG REQUIRED)
endif()
# ntr-recomp is a build-machine tool; a cross build (e.g. MinGW) uses a native one.
option(NTR_BUILD_RECOMPILER "Build the ntr-recomp code generator" ON)
if(NTR_BUILD_RECOMPILER)
    add_subdirectory(recompiler)
endif()
add_subdirectory(runtime)

option(NTR_BUILD_LAUNCHER "Build the launcher (ie3-launcher)" ON)
if(NTR_BUILD_LAUNCHER)
    add_subdirectory(launcher)
endif()

# Output of ntr-recomp (see tools/analyze.py manifest and README). The game target is
# only available once code has been generated.
set(NTR_GENERATED_DIR "${CMAKE_SOURCE_DIR}/build/recomp/gen" CACHE PATH "ntr-recomp output directory")
if(EXISTS "${NTR_GENERATED_DIR}/funcs.h")
    add_subdirectory(game)
else()
    message(STATUS "No generated code in ${NTR_GENERATED_DIR}; skipping the game target")
endif()

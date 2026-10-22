cmake_minimum_required(VERSION 3.28)
project(ie3ogre-recomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ntr-recomp is a build-machine tool; a cross build (e.g. MinGW) uses a native one.
option(NTR_BUILD_RECOMPILER "Build the ntr-recomp code generator" ON)
if(NTR_BUILD_RECOMPILER)
    add_subdirectory(recompiler)
endif()
add_subdirectory(runtime)

# Output of ntr-recomp (see tools/analyze.py manifest and README). The game target is
# only available once code has been generated.
set(NTR_GENERATED_DIR "${CMAKE_SOURCE_DIR}/build/recomp/gen" CACHE PATH "ntr-recomp output directory")
if(EXISTS "${NTR_GENERATED_DIR}/funcs.h")
    add_subdirectory(game)
else()
    message(STATUS "No generated code in ${NTR_GENERATED_DIR}; skipping the game target")
endif()

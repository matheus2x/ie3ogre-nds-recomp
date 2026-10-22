add_library(ntr-runtime STATIC
    src/arm7_card.cpp
    src/arm7_input.cpp
    src/arm7_sound.cpp
    src/boot.cpp
    src/cpu.cpp
    src/fiber.cpp
    src/gpu2d.cpp
    src/hle.cpp
    src/host.cpp
    src/io.cpp
    src/irq.cpp
    src/memory.cpp
    src/pxi.cpp
    src/rom.cpp
    src/task.cpp
    src/thread.cpp
    src/timing.cpp
    src/trace.cpp
    src/vram.cpp
)
target_include_directories(ntr-runtime PUBLIC include)
target_compile_options(ntr-runtime PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>)

# SDL3 for the window, input and timing. MinGW cross builds use the official
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
target_link_libraries(ntr-runtime PUBLIC SDL3::SDL3)

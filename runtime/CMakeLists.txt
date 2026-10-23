# Shared with the launcher: files, settings, hashes, dumps and the game data package.
add_library(ntr-common STATIC
    src/config.cpp
    src/files.cpp
    src/hash.cpp
    src/setup.cpp
)
target_include_directories(ntr-common PUBLIC include)
target_compile_options(ntr-common PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>)

add_library(ntr-runtime STATIC
    src/arm7_card.cpp
    src/arm7_input.cpp
    src/arm7_rtc.cpp
    src/arm7_sound.cpp
    src/boot.cpp
    src/cpu.cpp
    src/diag.cpp
    src/fiber.cpp
    src/gpu2d.cpp
    src/gpu3d.cpp
    src/hle.cpp
    src/host.cpp
    src/interp.cpp
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

target_link_libraries(ntr-runtime PUBLIC ntr-common SDL3::SDL3)

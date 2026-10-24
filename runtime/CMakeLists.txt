# Shared with the launcher: files, settings, hashes, dumps and the game data package.
add_library(ntr-common STATIC
    src/config.cpp
    src/files.cpp
    src/hash.cpp
    src/nitro.cpp
    src/package.cpp
    src/setup.cpp
)
target_include_directories(ntr-common PUBLIC include)
target_compile_options(ntr-common PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>)

# Dear ImGui (pinned release) and the screens shared by the game's pause menu and the
# launcher.
include(FetchContent)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(imgui)
set(NTR_IMGUI_DIR ${imgui_SOURCE_DIR} CACHE INTERNAL "Dear ImGui sources (tools/release.py copies them into the SDK)")
add_library(ntr-ui STATIC
    ui/ui.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(ntr-ui PUBLIC include ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(ntr-ui PUBLIC ntr-common SDL3::SDL3)

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
    src/mods.cpp
    src/pool.cpp
    src/pxi.cpp
    src/rom.cpp
    src/savestate.cpp
    src/task.cpp
    src/thread.cpp
    src/timing.cpp
    src/trace.cpp
    src/vram.cpp
)
target_include_directories(ntr-runtime PUBLIC include)
target_compile_options(ntr-runtime PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>)

target_link_libraries(ntr-runtime PUBLIC ntr-common ntr-ui SDL3::SDL3)

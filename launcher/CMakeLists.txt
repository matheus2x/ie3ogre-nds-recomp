# The launcher (see launcher.h). Dear ImGui is fetched at a pinned release.
include(FetchContent)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" NTR_VERSION)
string(STRIP "${NTR_VERSION}" NTR_VERSION)

add_executable(ie3-launcher WIN32
    main.cpp
    prepare.cpp
    process.cpp
    settings.cpp
    update.cpp
)
target_compile_definitions(ie3-launcher PRIVATE LAUNCHER_VERSION="${NTR_VERSION}")
target_compile_options(ie3-launcher PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>)
target_link_libraries(ie3-launcher PRIVATE ntr-common imgui SDL3::SDL3)

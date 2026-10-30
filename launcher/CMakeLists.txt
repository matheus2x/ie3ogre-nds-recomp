# The launcher (see launcher.h). Its screens share ntr-ui (Dear ImGui) with the game.
file(READ "${CMAKE_SOURCE_DIR}/VERSION" NTR_VERSION)
string(STRIP "${NTR_VERSION}" NTR_VERSION)

add_executable(ie3-launcher WIN32
    achievements.cpp
    gallery.cpp
    main.cpp
    mods.cpp
    prepare.cpp
    process.cpp
    saves.cpp
    settings.cpp
    update.cpp
)
target_compile_definitions(ie3-launcher PRIVATE LAUNCHER_VERSION="${NTR_VERSION}")
target_compile_options(ie3-launcher PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>)
target_link_libraries(ie3-launcher PRIVATE ntr-common ntr-ui SDL3::SDL3)
if(WIN32)
    target_sources(ie3-launcher PRIVATE "${CMAKE_SOURCE_DIR}/game/icon.rc")  # the game's icon
endif()

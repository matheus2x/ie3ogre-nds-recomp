file(GLOB NTR_GENERATED_SOURCES CONFIGURE_DEPENDS "${NTR_GENERATED_DIR}/*.cpp")

# Generated code is large and repetitive; -O1 keeps build times reasonable.
add_library(boej-generated OBJECT ${NTR_GENERATED_SOURCES})
target_link_libraries(boej-generated PRIVATE ntr-runtime)
target_include_directories(boej-generated PRIVATE "${NTR_GENERATED_DIR}")
target_compile_options(boej-generated PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-O1 -w -fno-strict-aliasing>
    $<$<CXX_COMPILER_ID:MSVC>:/O1 /w /bigobj>)
if(MINGW)
    # Generated translation units exceed the default COFF section limit.
    target_compile_options(boej-generated PRIVATE -Wa,-mbig-obj)
endif()

option(NTR_TRACE "Record guest function entries (debugging)" OFF)
if(NTR_TRACE)
    target_compile_definitions(boej-generated PRIVATE NTR_TRACE)
endif()

add_executable(boej main.cpp)
target_link_libraries(boej PRIVATE ntr-runtime boej-generated)
# Savestates put the fiber stacks back at their addresses, return addresses into this
# executable included (runtime/src/savestate.cpp): it must load at the same address in
# every run. Keep in sync with launcher/prepare.cpp.
if(MINGW)
    # GNU ld only takes --large-address-aware for 32-bit images (64-bit ones have it anyway).
    target_link_options(boej PRIVATE -Wl,--disable-dynamicbase $<$<CXX_COMPILER_ID:Clang>:-Wl,--large-address-aware>)
elseif(UNIX AND NOT APPLE)
    target_link_options(boej PRIVATE -no-pie)
endif()
if(WIN32)
    target_link_libraries(boej PRIVATE shell32)  # CommandLineToArgvW
    target_sources(boej PRIVATE icon.rc)  # launcher/prepare.cpp does the same
endif()

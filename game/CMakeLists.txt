file(GLOB NTR_GENERATED_SOURCES CONFIGURE_DEPENDS "${NTR_GENERATED_DIR}/*.cpp")

# Generated code is large and repetitive; -O1 keeps build times reasonable.
add_library(boej-generated OBJECT ${NTR_GENERATED_SOURCES})
target_link_libraries(boej-generated PRIVATE ntr-runtime)
target_include_directories(boej-generated PRIVATE "${NTR_GENERATED_DIR}")
target_compile_options(boej-generated PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-O1 -w -fno-strict-aliasing>
    $<$<CXX_COMPILER_ID:MSVC>:/O1 /w /bigobj>)

option(NTR_TRACE "Record guest function entries (debugging)" OFF)
if(NTR_TRACE)
    target_compile_definitions(boej-generated PRIVATE NTR_TRACE)
endif()

add_executable(boej main.cpp)
target_link_libraries(boej PRIVATE ntr-runtime boej-generated)

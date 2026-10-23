add_executable(ntr-recomp
    src/main.cpp
    src/program.cpp
    src/translator.cpp
)
target_compile_options(ntr-recomp PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>)
if(WIN32)
    target_link_libraries(ntr-recomp PRIVATE shell32)  # CommandLineToArgvW
endif()

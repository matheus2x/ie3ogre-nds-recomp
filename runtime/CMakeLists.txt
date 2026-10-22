add_library(ntr-runtime STATIC
    src/arm7_card.cpp
    src/arm7_input.cpp
    src/arm7_sound.cpp
    src/boot.cpp
    src/cpu.cpp
    src/fiber.cpp
    src/hle.cpp
    src/io.cpp
    src/irq.cpp
    src/memory.cpp
    src/pxi.cpp
    src/thread.cpp
    src/timing.cpp
    src/trace.cpp
)
target_include_directories(ntr-runtime PUBLIC include)
target_compile_options(ntr-runtime PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>)

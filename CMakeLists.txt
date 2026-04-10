cmake_minimum_required(VERSION 3.16)
project(amos-reborn C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0)
else()
    add_compile_options(-O2)
endif()

# Find SDL2
if(NOT EMSCRIPTEN)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    link_directories(${SDL2_LIBRARY_DIRS})
endif()

# Find OpenGL
if(NOT EMSCRIPTEN)
    find_package(OpenGL REQUIRED)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

if(NOT EMSCRIPTEN)
    include_directories(${SDL2_INCLUDE_DIRS})
endif()

# Source files
set(INTERPRETER_SOURCES
    src/interpreter/tokenizer.c
    src/interpreter/parser.c
    src/interpreter/executor.c
    src/interpreter/expressions.c
    src/interpreter/variables.c
    src/interpreter/commands.c
)

set(DISPLAY_SOURCES
    src/display/screen.c
    src/display/drawing.c
    src/display/compositor.c
    src/display/crt_shader.c
    src/display/font_topaz.c
    src/display/sprites.c
)

set(AUDIO_SOURCES
    src/audio/paula.c
    src/audio/effects.c
    src/audio/mixer.c
)

set(HAL_SOURCES
    src/hal/hal_classic.c
    src/hal/hal_modern.c
    src/hal/hal.c
)

set(PLATFORM_SOURCES
    src/platform/platform_sdl2.c
)

set(ALL_SOURCES
    src/main.c
    ${INTERPRETER_SOURCES}
    ${DISPLAY_SOURCES}
    ${AUDIO_SOURCES}
    ${HAL_SOURCES}
    ${PLATFORM_SOURCES}
)

# Main executable
add_executable(amos-reborn ${ALL_SOURCES})

if(EMSCRIPTEN)
    set_target_properties(amos-reborn PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "-s USE_SDL=2 -s FULL_ES3=1 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1"
    )
else()
    target_link_libraries(amos-reborn
        ${SDL2_LIBRARIES}
        OpenGL::GL
    )
    target_compile_options(amos-reborn PRIVATE ${SDL2_CFLAGS_OTHER})
endif()

# macOS framework linking
if(APPLE)
    target_link_libraries(amos-reborn
        "-framework AudioToolbox"
        "-framework CoreAudio"
    )
endif()

# Test executable
add_executable(amos-test
    tests/test_main.c
    tests/test_tokenizer.c
    tests/test_parser.c
    tests/test_executor.c
    tests/test_expressions.c
    ${INTERPRETER_SOURCES}
    src/display/screen.c
    src/display/font_topaz.c
    src/display/compositor.c
    src/display/sprites.c
    src/audio/paula.c
    src/audio/effects.c
)
target_compile_definitions(amos-test PRIVATE AMOS_TESTING=1)

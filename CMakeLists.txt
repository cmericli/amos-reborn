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
    src/interpreter/amal.c
    src/interpreter/amos_loader.c
    src/interpreter/file_io.c
)

set(DISPLAY_SOURCES
    src/display/screen.c
    src/display/drawing.c
    src/display/compositor.c
    src/display/crt_shader.c
    src/display/font_topaz.c
    src/display/sprites.c
    src/display/iff_loader.c
    src/display/iff_commands.c
    src/display/bank_loader.c
    src/display/bank_commands.c
)

set(AUDIO_SOURCES
    src/audio/paula.c
    src/audio/effects.c
    src/audio/mixer.c
    src/audio/tracker.c
)

# Optional: libxmp for MOD tracker playback
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBXMP libxmp)
    if(LIBXMP_FOUND)
        add_compile_definitions(HAVE_LIBXMP=1)
        include_directories(${LIBXMP_INCLUDE_DIRS})
        link_directories(${LIBXMP_LIBRARY_DIRS})
        message(STATUS "libxmp found — MOD tracker playback enabled")
    else()
        message(STATUS "libxmp not found — MOD tracker playback stubbed")
    endif()
endif()

set(HAL_SOURCES
    src/hal/hal_classic.c
    src/hal/hal_modern.c
    src/hal/hal.c
)

set(PLATFORM_SOURCES
    src/platform/platform_sdl2.c
    src/platform/input.c
)

set(EDITOR_SOURCES
    src/editor/editor.c
)

set(ALL_SOURCES
    src/main.c
    ${INTERPRETER_SOURCES}
    ${DISPLAY_SOURCES}
    ${AUDIO_SOURCES}
    ${HAL_SOURCES}
    ${PLATFORM_SOURCES}
    ${EDITOR_SOURCES}
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
    tests/test_iff.c
    tests/test_bank_loader.c
    tests/vv_framework.c
    tests/vv_example_tests.c
    tests/vv_interpreter_tests.c
    tests/vv_display_tests.c
    tests/vv_audio_tests.c
    tests/vv_amal_tests.c
    tests/vv_format_tests.c
    ${INTERPRETER_SOURCES}
    src/display/screen.c
    src/display/font_topaz.c
    src/display/compositor.c
    src/display/sprites.c
    src/display/iff_loader.c
    src/display/iff_commands.c
    src/display/bank_loader.c
    src/display/bank_commands.c
    src/audio/paula.c
    src/audio/effects.c
    src/audio/tracker.c
    src/platform/input.c
)
target_compile_definitions(amos-test PRIVATE AMOS_TESTING=1)
target_include_directories(amos-test PRIVATE ${CMAKE_SOURCE_DIR}/tests)

# Headless .AMOS file tester
add_executable(test-load
    tools/test_load.c
    ${INTERPRETER_SOURCES}
    src/display/screen.c
    src/display/font_topaz.c
    src/display/compositor.c
    src/display/sprites.c
    src/display/iff_loader.c
    src/display/iff_commands.c
    src/display/bank_loader.c
    src/display/bank_commands.c
    src/audio/paula.c
    src/audio/effects.c
    src/audio/tracker.c
    src/platform/input.c
)
target_compile_definitions(test-load PRIVATE AMOS_TESTING=1)

# Headless screenshot capture tool (oracle for visual regression)
add_executable(oracle-capture
    tools/oracle_capture.c
    ${INTERPRETER_SOURCES}
    src/display/screen.c
    src/display/font_topaz.c
    src/display/compositor.c
    src/display/sprites.c
    src/display/iff_loader.c
    src/display/iff_commands.c
    src/display/bank_loader.c
    src/display/bank_commands.c
    src/audio/paula.c
    src/audio/effects.c
    src/audio/tracker.c
    src/platform/input.c
)
target_compile_definitions(oracle-capture PRIVATE AMOS_TESTING=1)
target_include_directories(oracle-capture PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

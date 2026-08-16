cmake_minimum_required(VERSION 3.20)

project(dkc1_port
    VERSION 0.0.1
    DESCRIPTION "Native PC recompilation of Donkey Kong Country"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT DEFINED SNESRECOMP_ROOT)
    set(SNESRECOMP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp)
endif()
set(DKC1_SNESRECOMP_GEN_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/snesrecomp" CACHE PATH
    "Directory containing private generated snesrecomp C sources")
if(NOT EXISTS "${SNESRECOMP_ROOT}/runner/runner.cmake")
    message(FATAL_ERROR
        "snesrecomp is not initialized; run git submodule update --init --recursive")
endif()
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

file(GLOB DKC1_SNESRECOMP_GEN_SOURCES CONFIGURE_DEPENDS
    "${DKC1_SNESRECOMP_GEN_DIR}/*.c")
if(NOT DKC1_SNESRECOMP_GEN_SOURCES)
    message(FATAL_ERROR
        "No private generated sources found; run scripts/generate_snesrecomp.py first")
endif()

# The headless host has no SDL dependency; provide small host adapters for
# the keybind/report surfaces instead of the desktop implementations.
list(REMOVE_ITEM SNESRECOMP_RUNNER_SOURCES
    "${SNESRECOMP_ROOT}/runner/src/keybinds.c"
    "${SNESRECOMP_ROOT}/runner/src/host_report.c")

add_executable(dkc1_snesrecomp_headless
    ${SNESRECOMP_RUNNER_SOURCES}
    ${DKC1_SNESRECOMP_GEN_SOURCES}
    runner/dkc1_game.c
    runner/dkc1_video.c
    runner/dkc1_margin_proxy.c
    runner/dkc1_ws_trace.c
    runner/headless_host.c
    runner/input_playback.c
    runner/wram_dump.c
    runner/dkc1_script.c
    runner/dkc1_debug_dump.c
    runner/verified_rom.c
    runner/headless_main.c)
target_include_directories(dkc1_snesrecomp_headless PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/runner
    ${CMAKE_CURRENT_SOURCE_DIR}/recomp
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
target_compile_definitions(dkc1_snesrecomp_headless PRIVATE
    SNESRECOMP_TRACE=0
    SNESRECOMP_REVERSE_DEBUG=0
    SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1
    SYSTEM_VOLUME_MIXER_AVAILABLE=0)
if(MSVC)
    # Do not use /MP here. This generated-source workload has reproduced an
    # MSVC race where cl.exe returns before every bank object is available to
    # the immediately following link. A serial compile is slower but reliable.
    target_compile_options(dkc1_snesrecomp_headless PRIVATE /W0)
    target_compile_definitions(dkc1_snesrecomp_headless PRIVATE
        _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(dkc1_snesrecomp_headless PRIVATE
    ${SNESRECOMP_RUNNER_LIBRARIES})

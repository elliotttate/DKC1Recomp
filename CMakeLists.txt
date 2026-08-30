cmake_minimum_required(VERSION 3.20)

project(dkc1_port
    VERSION 0.0.2
    DESCRIPTION "Native PC recompilation of Donkey Kong Country"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE DKC1_BUILD_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT DKC1_BUILD_COMMIT)
    set(DKC1_BUILD_COMMIT "nogit")
endif()

if(NOT DEFINED SNESRECOMP_ROOT)
    set(SNESRECOMP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp)
endif()
if(APPLE AND NOT DEFINED SNESRECOMP_SDL_BACKEND)
    # The DKC1 macOS frontend currently uses SDL2's queued-audio API. Keep
    # this project on the engine's explicit compatibility backend until its
    # SDL3 audio-stream equivalent is implemented and validated.
    set(SNESRECOMP_SDL_BACKEND "SDL2" CACHE STRING
        "Desktop SDL backend (SDL3 or SDL2)")
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

set(DKC1_RUNNER_SOURCES
    runner/dkc1_game.c
    runner/dkc1_video.c
    runner/dkc1_margin_proxy.c
    runner/dkc1_ws_trace.c
    runner/input_playback.c
    runner/wram_dump.c
    runner/dkc1_script.c
    runner/dkc1_debug_dump.c
    runner/dkc1_flight_recorder.c
    runner/dkc1_blank_scan.c
    runner/dkc1_invariant_monitor.c
    runner/verified_rom.c)

# DKC1 supplies its own lightweight keybind/report host adapters. The shared
# desktop implementations depend on the larger launcher/config UI used by
# other games and are not part of either DKC1 frontend.
set(DKC1_ENGINE_SOURCES ${SNESRECOMP_RUNNER_SOURCES})
list(REMOVE_ITEM DKC1_ENGINE_SOURCES
    "${SNESRECOMP_ROOT}/runner/src/keybinds.c"
    "${SNESRECOMP_ROOT}/runner/src/host_report.c")

add_executable(dkc1_snesrecomp_headless
    ${DKC1_ENGINE_SOURCES}
    ${DKC1_SNESRECOMP_GEN_SOURCES}
    ${DKC1_RUNNER_SOURCES}
    runner/headless_host.c
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

if(APPLE)
    enable_language(OBJC)
    if(NOT SNESRECOMP_SDL_BACKEND STREQUAL "SDL2")
        message(FATAL_ERROR
            "The DKC1 macOS frontend currently requires "
            "-DSNESRECOMP_SDL_BACKEND=SDL2")
    endif()

    add_executable(dkc1_macos MACOSX_BUNDLE
        ${DKC1_ENGINE_SOURCES}
        ${DKC1_SNESRECOMP_GEN_SOURCES}
        ${DKC1_RUNNER_SOURCES}
        assets/macos/DKC1Recomp.icns
        runner/headless_host.c
        runner/sdl_host.c
        runner/macos_file_picker.m)
    set_source_files_properties(assets/macos/DKC1Recomp.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_include_directories(dkc1_macos PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
        ${SNESRECOMP_ROOT}/runner/src/desktop)
    target_compile_definitions(dkc1_macos PRIVATE
        SNESRECOMP_TRACE=0
        SNESRECOMP_REVERSE_DEBUG=0
        SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1
        SYSTEM_VOLUME_MIXER_AVAILABLE=0
        DKC1_BUILD_COMMIT="${DKC1_BUILD_COMMIT}"
        DKC1_BUILD_CONFIG="macos")
    target_compile_options(dkc1_macos PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wno-unused-function>)
    target_link_libraries(dkc1_macos PRIVATE
        ${SNESRECOMP_RUNNER_LIBRARIES}
        "-framework AppKit"
        "-framework QuartzCore"
        "-framework UniformTypeIdentifiers")
    snesrecomp_target_sdl(dkc1_macos)
    set_target_properties(dkc1_macos PROPERTIES
        OUTPUT_NAME "DKC1Recomp"
        MACOSX_BUNDLE_BUNDLE_NAME "DKC1Recomp"
        MACOSX_BUNDLE_ICON_FILE "DKC1Recomp.icns"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.flat2vr.dkc1recomp"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
endif()

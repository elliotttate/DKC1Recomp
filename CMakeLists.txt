cmake_minimum_required(VERSION 3.20)

project(dkc1_port
    VERSION 0.0.17
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

if(WIN32)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    include(FetchContent)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG c98c4fbff6d8f3016a3ce6685bf8f43433c3efcc)
    FetchContent_MakeAvailable(SDL2)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(miniz GIT_REPOSITORY https://github.com/richgel999/miniz.git
        GIT_TAG 293d4db1b7d0ffee9756d035b9ac6f7431ef8492)
    # The pinned miniz snapshot declares cmake_minimum_required(2.8), which
    # CMake 4 rejects outright; let it configure under the 3.5 policy set.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(miniz)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/windows_shaders.h
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_windows_shaders.py
            ${CMAKE_CURRENT_SOURCE_DIR}/runner/macos_graphics.metal
            ${CMAKE_CURRENT_BINARY_DIR}/windows_shaders.h
        DEPENDS scripts/generate_windows_shaders.py runner/macos_graphics.metal VERBATIM)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/windows_hlsl.h
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_windows_hlsl.py
            ${CMAKE_CURRENT_SOURCE_DIR}/runner/macos_graphics.metal
            ${CMAKE_CURRENT_BINARY_DIR}/windows_hlsl.h
        DEPENDS scripts/generate_windows_hlsl.py runner/macos_graphics.metal VERBATIM)
endif()

if(NOT DEFINED SNESRECOMP_ROOT)
    set(SNESRECOMP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp)
endif()
if((APPLE OR WIN32) AND NOT DEFINED SNESRECOMP_SDL_BACKEND)
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
    runner/desktop_sram.c
    runner/dkc1_game.c
    runner/dkc1_video.c
    runner/dkc1_hd_scene.c
    runner/dkc1_hd_sprites.c
    runner/dkc1_framegen.c
    runner/dkc1_dixie_mod.c
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

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    include(cmake/ios.cmake)
    return()
endif()

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
    target_sources(dkc1_snesrecomp_headless PRIVATE runner/macos_hd_scene.m)
    target_link_libraries(dkc1_snesrecomp_headless PRIVATE
        "-framework Foundation" "-framework CoreFoundation" "-framework Metal")
    add_executable(test_macos_graphics tests/test_macos_graphics.m
        runner/macos_graphics.m runner/desktop_graphics.c runner/desktop_crt.c)
    target_include_directories(test_macos_graphics PRIVATE runner)
    target_link_libraries(test_macos_graphics PRIVATE "-framework Foundation" "-framework Metal")

    if(NOT SNESRECOMP_SDL_BACKEND STREQUAL "SDL2")
        message(FATAL_ERROR
            "The DKC1 macOS frontend currently requires "
            "-DSNESRECOMP_SDL_BACKEND=SDL2")
    endif()

    set(DKC1_MACOS_FRONTEND_SOURCES
        ${DKC1_ENGINE_SOURCES}
        ${DKC1_RUNNER_SOURCES}
        runner/headless_host.c
        runner/dkc1_haptics.c
        runner/dkc1_msu1.c
        runner/desktop_audio_rate.c
        runner/desktop_rewind.c
        runner/desktop_input.c
        runner/desktop_refresh.c
        runner/desktop_crt.c
        runner/desktop_filter.c
        runner/desktop_graphics.c
        runner/macos_graphics.m
        runner/macos_graphics.metal
        runner/macos_hd_scene.m
        runner/macos_hd_scene.metal
        runner/dkc1_hd_gpu_types.h
        runner/macos_pause_menu.m
        runner/macos_controls.m
        runner/sdl_host.c
        runner/macos_metal_presenter.m
        runner/macos_file_picker.m)
    add_executable(dkc1_macos MACOSX_BUNDLE
        ${DKC1_SNESRECOMP_GEN_SOURCES}
        ${DKC1_MACOS_FRONTEND_SOURCES}
        assets/macos/DKC1Recomp.icns)
    set_source_files_properties(runner/macos_graphics.metal runner/macos_hd_scene.metal runner/dkc1_hd_gpu_types.h PROPERTIES
        HEADER_FILE_ONLY TRUE MACOSX_PACKAGE_LOCATION "Resources")
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
        "-framework CoreFoundation"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework UniformTypeIdentifiers")
    snesrecomp_target_sdl(dkc1_macos)
    set_target_properties(dkc1_macos PROPERTIES
        OUTPUT_NAME "DKC1Recomp-HD"
        MACOSX_BUNDLE_BUNDLE_NAME "DKC1Recomp HD Experiment"
        MACOSX_BUNDLE_ICON_FILE "DKC1Recomp.icns"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.flat2vr.dkc1recomp.hdexperiment"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")

    file(GLOB DKC1_DIXIE_GEN_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/snesrecomp_dixie/*.c")
    if(NOT DKC1_DIXIE_GEN_SOURCES)
        message(FATAL_ERROR
            "No private Dixie sources found; run scripts/generate_macos_dixie.py")
    endif()

    add_executable(dkc1_dixie_headless
        ${DKC1_ENGINE_SOURCES}
        ${DKC1_DIXIE_GEN_SOURCES}
        ${DKC1_RUNNER_SOURCES}
        runner/headless_host.c
        runner/headless_main.c
        runner/macos_hd_scene.m)
    target_include_directories(dkc1_dixie_headless PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp/dixie
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    target_compile_definitions(dkc1_dixie_headless PRIVATE
        DKC1_DIXIE_VARIANT=1
        SNESRECOMP_TRACE=0
        SNESRECOMP_REVERSE_DEBUG=0
        SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1
        SYSTEM_VOLUME_MIXER_AVAILABLE=0)
    target_link_libraries(dkc1_dixie_headless PRIVATE
        ${SNESRECOMP_RUNNER_LIBRARIES}
        "-framework Foundation" "-framework CoreFoundation" "-framework Metal")

    add_executable(dkc1_macos_dixie
        ${DKC1_DIXIE_GEN_SOURCES}
        ${DKC1_MACOS_FRONTEND_SOURCES})
    target_include_directories(dkc1_macos_dixie PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp/dixie
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
        ${SNESRECOMP_ROOT}/runner/src/desktop)
    target_compile_definitions(dkc1_macos_dixie PRIVATE
        DKC1_DIXIE_VARIANT=1
        SNESRECOMP_TRACE=0
        SNESRECOMP_REVERSE_DEBUG=0
        SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1
        SYSTEM_VOLUME_MIXER_AVAILABLE=0
        DKC1_BUILD_COMMIT="${DKC1_BUILD_COMMIT}"
        DKC1_BUILD_CONFIG="macos-dixie")
    target_compile_options(dkc1_macos_dixie PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wno-unused-function>)
    target_link_libraries(dkc1_macos_dixie PRIVATE
        ${SNESRECOMP_RUNNER_LIBRARIES}
        "-framework AppKit"
        "-framework CoreFoundation"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework UniformTypeIdentifiers")
    snesrecomp_target_sdl(dkc1_macos_dixie)
    set_target_properties(dkc1_macos_dixie PROPERTIES
        OUTPUT_NAME "DKC1Recomp-HD-Dixie")
endif()

if(WIN32)
    set(DKC1_WINDOWS_HOST_SOURCES runner/headless_host.c runner/sdl_host.c
        runner/dkc1_haptics.c runner/dkc1_msu1.c runner/desktop_audio_rate.c
        runner/desktop_rewind.c runner/desktop_input.c runner/desktop_refresh.c
        runner/desktop_crt.c runner/desktop_filter.c runner/desktop_graphics.c
        runner/windows_platform.c runner/windows_graphics.c runner/windows_music_pack.c
        runner/windows_present.c
        ${CMAKE_CURRENT_BINARY_DIR}/windows_shaders.h ${CMAKE_CURRENT_BINARY_DIR}/windows_hlsl.h)
    add_executable(dkc1_windows ${DKC1_ENGINE_SOURCES} ${DKC1_SNESRECOMP_GEN_SOURCES}
        ${DKC1_RUNNER_SOURCES} ${DKC1_WINDOWS_HOST_SOURCES})
    target_include_directories(dkc1_windows PRIVATE runner recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_definitions(dkc1_windows PRIVATE SDL_MAIN_HANDLED
        SNESRECOMP_TRACE=0 SNESRECOMP_REVERSE_DEBUG=0
        SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1 SYSTEM_VOLUME_MIXER_AVAILABLE=0
        _CRT_SECURE_NO_WARNINGS DKC1_BUILD_COMMIT="${DKC1_BUILD_COMMIT}"
        DKC1_BUILD_CONFIG="windows-${PROJECT_VERSION}")
    target_link_libraries(dkc1_windows PRIVATE ${SNESRECOMP_RUNNER_LIBRARIES}
        SDL2::SDL2 miniz opengl32 dwmapi winmm comctl32 comdlg32 shell32 ole32 uuid gdi32)
    set_target_properties(dkc1_windows PROPERTIES OUTPUT_NAME DKC1Recomp WIN32_EXECUTABLE TRUE)
    target_link_options(dkc1_windows PRIVATE /ENTRY:mainCRTStartup)
    add_custom_command(TARGET dkc1_windows POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2>
            $<TARGET_FILE_DIR:dkc1_windows>)
    option(DKC1_BUILD_DIXIE_VARIANT "Build the optional Dixie SDL sibling" OFF)
    if(DKC1_BUILD_DIXIE_VARIANT)
        file(GLOB DKC1_DIXIE_GEN_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/generated/snesrecomp_dixie/*.c")
        if(NOT DKC1_DIXIE_GEN_SOURCES)
            message(FATAL_ERROR "Generate the private Dixie sources first; see docs/DIXIE_MOD.md")
        endif()
        add_executable(dkc1_windows_dixie ${DKC1_ENGINE_SOURCES}
            ${DKC1_DIXIE_GEN_SOURCES} ${DKC1_RUNNER_SOURCES} ${DKC1_WINDOWS_HOST_SOURCES})
        target_include_directories(dkc1_windows_dixie PRIVATE runner recomp/dixie recomp
            ${SNESRECOMP_RUNNER_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
        target_compile_definitions(dkc1_windows_dixie PRIVATE SDL_MAIN_HANDLED
            DKC1_DIXIE_VARIANT=1 SNESRECOMP_TRACE=0 SNESRECOMP_REVERSE_DEBUG=0
            SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1 SYSTEM_VOLUME_MIXER_AVAILABLE=0
            _CRT_SECURE_NO_WARNINGS DKC1_BUILD_COMMIT="${DKC1_BUILD_COMMIT}"
            DKC1_BUILD_CONFIG="windows-dixie-${PROJECT_VERSION}")
        target_link_libraries(dkc1_windows_dixie PRIVATE ${SNESRECOMP_RUNNER_LIBRARIES}
            SDL2::SDL2 miniz opengl32 dwmapi winmm comctl32 comdlg32 shell32 ole32 uuid gdi32)
        set_target_properties(dkc1_windows_dixie PROPERTIES
            OUTPUT_NAME dkc1_dixie_desktop WIN32_EXECUTABLE TRUE)
        target_link_options(dkc1_windows_dixie PRIVATE /ENTRY:mainCRTStartup)
        add_custom_command(TARGET dkc1_windows_dixie POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2>
                $<TARGET_FILE_DIR:dkc1_windows_dixie>)
    endif()
    enable_testing()
    add_test(NAME windows_graphics COMMAND dkc1_windows --graphics-test)
    add_test(NAME windows_haptics COMMAND dkc1_windows --haptics-test)
    if(DKC1_BUILD_DIXIE_VARIANT)
        add_test(NAME windows_dixie_haptics COMMAND dkc1_windows_dixie --haptics-test)
    endif()
    set_tests_properties(windows_graphics PROPERTIES TIMEOUT 60)
    add_executable(test_windows_music tests/test_windows_music.c
        runner/windows_music_pack.c runner/dkc1_msu1.c)
    target_include_directories(test_windows_music PRIVATE runner)
    target_compile_definitions(test_windows_music PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(test_windows_music PRIVATE miniz ole32)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/music-test)
    add_test(NAME windows_music COMMAND test_windows_music ${CMAKE_CURRENT_BINARY_DIR}/music-test)
    add_test(NAME windows_platform COMMAND dkc1_windows --platform-test ${CMAKE_CURRENT_BINARY_DIR}/music-test)
    set_source_files_properties(runner/windows_platform.c runner/windows_graphics.c
        runner/windows_music_pack.c runner/windows_present.c PROPERTIES COMPILE_OPTIONS "/W4;/WX")
endif()

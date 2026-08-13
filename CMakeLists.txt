cmake_minimum_required(VERSION 3.22)

# Universal macOS binary, same as the rest of the workspace: some hosts here run
# under Rosetta.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

project(Orcha VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE lives outside this tree - the same ~/JUCE checkout (JUCE 9) every plugin
# in this workspace uses. The Windows source bundle clones a pinned JUCE into
# ./JUCE and passes ORCHA_JUCE_DIR_OVERRIDE so that pin beats any stray
# %USERPROFILE%\JUCE from another project.
if(DEFINED ORCHA_JUCE_DIR_OVERRIDE AND EXISTS "${ORCHA_JUCE_DIR_OVERRIDE}/CMakeLists.txt")
    set(ORCHA_JUCE_DIR "${ORCHA_JUCE_DIR_OVERRIDE}")
elseif(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/JUCE/CMakeLists.txt")
    set(ORCHA_JUCE_DIR "$ENV{HOME}/JUCE")
elseif(DEFINED ENV{USERPROFILE} AND EXISTS "$ENV{USERPROFILE}/JUCE/CMakeLists.txt")
    set(ORCHA_JUCE_DIR "$ENV{USERPROFILE}/JUCE")
else()
    set(ORCHA_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
endif()
add_subdirectory(${ORCHA_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

# --- build identity ------------------------------------------------------------
set(ORCHA_VERSION "${PROJECT_VERSION}")

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE ORCHA_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if(NOT ORCHA_GIT_DESCRIBE)
    set(ORCHA_GIT_DESCRIBE "no-git")
endif()
string(TIMESTAMP ORCHA_BUILD_DATE "%Y-%m-%d" UTC)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/OrchaVersion.h.in"
    "${CMAKE_BINARY_DIR}/generated/OrchaVersion.h"
    @ONLY)

message(STATUS "ORCHA ${ORCHA_VERSION} (${ORCHA_GIT_DESCRIBE}, ${ORCHA_BUILD_DATE})")

if(APPLE)
    set(ORCHA_FORMATS VST3 AU Standalone)
else()
    set(ORCHA_FORMATS VST3 Standalone)
endif()

option(ORCHA_COPY_AFTER_BUILD "Copy built plug-ins into the user plug-in folders" ON)
option(ORCHA_WERROR "Treat warnings in ORCHA's own sources as errors" OFF)

# Warning flags are attached to ORCHA's own translation units only: JUCE's
# module sources compile into our targets and we do not own their warnings.
if(MSVC)
    # /utf-8: without it MSVC reads the source in the machine's ANSI codepage.
    set(ORCHA_WARNING_FLAGS /W4 /permissive- /utf-8)
    if(ORCHA_WERROR)
        list(APPEND ORCHA_WARNING_FLAGS /WX)
    endif()
else()
    set(ORCHA_WARNING_FLAGS
        -Wall -Wextra -Wpedantic -Wshadow -Wcast-align -Wunused
        -Woverloaded-virtual -Wnon-virtual-dtor -Wimplicit-fallthrough
        -Wfloat-conversion)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND ORCHA_WARNING_FLAGS -Wshorten-64-to-32)
    endif()
    if(ORCHA_WERROR)
        list(APPEND ORCHA_WARNING_FLAGS -Werror)
    endif()
endif()

set(ORCHA_ENGINE_SOURCES
    Source/Engine/SampleAnalyzer.cpp
    Source/Engine/SampleLoader.cpp
    Source/Engine/RhythmStyle.cpp
    Source/Engine/LoopGenerator.cpp
    Source/Engine/PatternValidator.cpp
    Source/Engine/LoopRenderer.cpp
    Source/Engine/RenderCache.cpp
    Source/Engine/MidiExporter.cpp
    Source/Engine/SampleTransform.cpp
    Source/Engine/PhrasePlanner.cpp
    Source/Engine/Motif.cpp
    Source/Engine/MusicalScorer.cpp)

set(ORCHA_PLUGIN_SOURCES
    ${ORCHA_ENGINE_SOURCES}
    Source/Playback/PreviewPlayer.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Ui/Theme.cpp
    Source/Ui/SampleCard.cpp
    Source/Ui/GenerationStrip.cpp
    Source/Ui/OptionCard.cpp
    Source/Ui/PatternEditPanel.cpp
    Source/Ui/SampleEditPanel.cpp)

set(ORCHA_OWN_SOURCES ${ORCHA_PLUGIN_SOURCES} Source/Tools/test_engine.cpp
    Source/Tools/screenshot_tool.cpp Source/Tools/audition_tool.cpp)
set_source_files_properties(${ORCHA_OWN_SOURCES}
    PROPERTIES COMPILE_OPTIONS "${ORCHA_WARNING_FLAGS}")

set(ORCHA_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_FLAC=1)

# --- the plug-in ---------------------------------------------------------------
juce_add_plugin(Orcha
    COMPANY_NAME "Naaman"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${ORCHA_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Naam
    PLUGIN_CODE Orca
    FORMATS ${ORCHA_FORMATS}
    VST3_CATEGORIES "Instrument" "Drum"
    # Generator, not MusicDevice: ORCHA produces audio without MIDI input, and
    # auval fails a MusicDevice that ignores MIDI.
    AU_MAIN_TYPE kAudioUnitType_Generator
    VERSION "${ORCHA_VERSION}"
    PRODUCT_NAME "ORCHA")

target_sources(Orcha PRIVATE ${ORCHA_PLUGIN_SOURCES})
target_include_directories(Orcha PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(Orcha PUBLIC ${ORCHA_COMMON_DEFS})
target_link_libraries(Orcha
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- engine test suite ---------------------------------------------------------
#     build/OrchaTests_artefacts/<config>/OrchaTests
#
# Links only the engine: no editor, no plug-in wrapper, so a UI mistake cannot
# stop the generation checks from running.
juce_add_console_app(OrchaTests PRODUCT_NAME "OrchaTests")
juce_generate_juce_header(OrchaTests)
target_sources(OrchaTests PRIVATE
    Source/Tools/test_engine.cpp
    Source/Playback/PreviewPlayer.cpp
    ${ORCHA_ENGINE_SOURCES})
target_include_directories(OrchaTests PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(OrchaTests PUBLIC
    ${ORCHA_COMMON_DEFS}
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(OrchaTests
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)

# --- audition renderer ---------------------------------------------------------
#     build/OrchaAudition_artefacts/<config>/OrchaAudition out-dir bpm <samples>
juce_add_console_app(OrchaAudition PRODUCT_NAME "OrchaAudition")
juce_generate_juce_header(OrchaAudition)
target_sources(OrchaAudition PRIVATE
    Source/Tools/audition_tool.cpp
    ${ORCHA_ENGINE_SOURCES})
target_include_directories(OrchaAudition PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(OrchaAudition PUBLIC
    ${ORCHA_COMMON_DEFS}
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(OrchaAudition
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)

# --- deterministic UI renderer + host smoke test -------------------------------
#     build/OrchaShot_artefacts/<config>/OrchaShot ui-shots <samples...>
juce_add_console_app(OrchaShot PRODUCT_NAME "OrchaShot")
juce_generate_juce_header(OrchaShot)
target_sources(OrchaShot PRIVATE
    Source/Tools/screenshot_tool.cpp
    ${ORCHA_PLUGIN_SOURCES})
target_include_directories(OrchaShot PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(OrchaShot PUBLIC
    ${ORCHA_COMMON_DEFS}
    JUCE_STANDALONE_APPLICATION=1
    # The shot tool pumps the message loop so worker results actually land.
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(OrchaShot
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
            juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags)

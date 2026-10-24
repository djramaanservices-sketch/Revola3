cmake_minimum_required(VERSION 3.22)

# macOS: universal binary (Apple Silicon + Intel), runs on macOS 10.13+
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

project(Revola VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows: link the C++ runtime statically so users don't need the VC++ redistributable
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(REVOLA_COPY_AFTER_BUILD "Install the plugin into the system plug-in folders after building" OFF)
option(REVOLA_BUILD_TESTS "Build the standalone DSP test (no JUCE needed)" OFF)

# --- DSP test (plain C++) ---------------------------------------------------
if(REVOLA_BUILD_TESTS)
    add_executable(revola_dsp_test tests/dsp_test.cpp)
    enable_testing()
    add_test(NAME dsp COMMAND revola_dsp_test)
endif()

# --- JUCE ---------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(REVOLA_FORMATS VST3 AU Standalone)
else()
    set(REVOLA_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Revola
    COMPANY_NAME                "Revola Audio"        # <- change me
    PLUGIN_MANUFACTURER_CODE    Rvla                  # 4 chars, at least one uppercase
    PLUGIN_CODE                 Rv01                  # 4 chars, unique per plugin
    BUNDLE_ID                   com.revolaaudio.revola
    PRODUCT_NAME                "Revola"
    FORMATS                     ${REVOLA_FORMATS}
    VST3_CATEGORIES             Fx Reverb
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${REVOLA_COPY_AFTER_BUILD})

target_sources(Revola PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Revola PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Revola
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

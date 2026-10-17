cmake_minimum_required(VERSION 3.22)

project(BigMod VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # Link the C++ runtime statically so the plug-in works without installing the VC++ redistributable
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version" FORCE)
endif()

option(BIGMOD_COPY_AFTER_BUILD "Copy the built plug-ins into the system plug-in folders" OFF)
option(BIGMOD_BUILD_TESTS "Build the test plug-in and the automated test runner" OFF)

#------------------------------------------------------------------------------
# JUCE: use ./JUCE if it is there, otherwise download it
#------------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(BIGMOD_SOURCES
    Source/Curve.h
    Source/ModEngine.h
    Source/ModEngine.cpp
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/Theme.h
    Source/Widgets.h
    Source/Widgets.cpp
    Source/ModulePanels.h
    Source/ModulePanels.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp)

set(BIGMOD_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND BIGMOD_FORMATS AU)
endif()

set(BIGMOD_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_REPORT_APP_USAGE=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
if(APPLE)
    list(APPEND BIGMOD_DEFINITIONS JUCE_PLUGINHOST_AU=1)
endif()

function(bigmod_add_plugin target product code is_synth vst3_cat au_type)
    juce_add_plugin(${target}
        COMPANY_NAME                "BigMod"
        BUNDLE_ID                   "com.bigmod.${target}"
        PLUGIN_MANUFACTURER_CODE    Bgmd
        PLUGIN_CODE                 ${code}
        PRODUCT_NAME                "${product}"
        FORMATS                     ${BIGMOD_FORMATS}
        IS_SYNTH                    ${is_synth}
        NEEDS_MIDI_INPUT            TRUE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES             ${vst3_cat}
        AU_MAIN_TYPE                ${au_type}
        COPY_PLUGIN_AFTER_BUILD     ${BIGMOD_COPY_AFTER_BUILD})

    target_sources(${target} PRIVATE ${BIGMOD_SOURCES})
    target_compile_definitions(${target} PUBLIC ${BIGMOD_DEFINITIONS})
    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

# Effect version: hosts effects (EQ, compressors, distortion...)
bigmod_add_plugin(BigModFx   "BigMod"            Bgfx FALSE "Fx"         kAudioUnitType_MusicEffect)
# Instrument version: hosts synths and samplers
bigmod_add_plugin(BigModInst "BigMod Instrument" Bgin TRUE  "Instrument" kAudioUnitType_MusicDevice)

if(BIGMOD_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

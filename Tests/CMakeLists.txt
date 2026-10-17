# Test plug-ins + automated test runners (cmake -DBIGMOD_BUILD_TESTS=ON)
foreach(kind Fx Synth)
    if(kind STREQUAL "Synth")
        set(is_synth TRUE)
        set(code Tsy1)
        set(synth_def 1)
    else()
        set(is_synth FALSE)
        set(code Tfx1)
        set(synth_def 0)
    endif()
    juce_add_plugin(BigModTest${kind}
        COMPANY_NAME "BigModTest"
        PLUGIN_MANUFACTURER_CODE Bmts
        PLUGIN_CODE ${code}
        PRODUCT_NAME "BigMod Test${kind}"
        FORMATS VST3
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT ${is_synth})
    target_sources(BigModTest${kind} PRIVATE TestPlugins.cpp)
    target_compile_definitions(BigModTest${kind} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
                               JUCE_VST3_CAN_REPLACE_VST2=0 TEST_SYNTH=${synth_def})
    target_link_libraries(BigModTest${kind} PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
endforeach()

set(test_sources ${BIGMOD_SOURCES})
list(TRANSFORM test_sources PREPEND "${PROJECT_SOURCE_DIR}/")

foreach(mode Fx Inst)
    if(mode STREQUAL "Inst")
        set(synth 1)
    else()
        set(synth 0)
    endif()
    juce_add_console_app(BigModTests${mode} PRODUCT_NAME "BigModTests${mode}")
    target_sources(BigModTests${mode} PRIVATE RunTests.cpp ${test_sources})
    target_compile_definitions(BigModTests${mode} PRIVATE ${BIGMOD_DEFINITIONS}
                               JUCE_MODAL_LOOPS_PERMITTED=1 BIGMOD_IS_SYNTH=${synth})
    target_link_libraries(BigModTests${mode} PRIVATE juce::juce_audio_utils juce::juce_audio_processors
                          PUBLIC juce::juce_recommended_config_flags)
endforeach()

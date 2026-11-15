cmake_minimum_required(VERSION 3.22)

project(SimpleSampler VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(SIMPLESAMPLER_BUILD_TESTS "Build the unit test executable" ON)

# ---------------------------------------------------------------------------
# Dependencies (downloaded automatically)
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)

FetchContent_Declare(
    signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(JUCE signalsmith-stretch)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(SimpleSampler
    PRODUCT_NAME                "SimpleSampler"
    COMPANY_NAME                "HomeMade"
    BUNDLE_ID                   "com.homemade.simplesampler"
    PLUGIN_MANUFACTURER_CODE    Hmde
    PLUGIN_CODE                 Ssmp
    FORMATS                     VST3 Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD     FALSE)

set(SIMPLESAMPLER_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Voice.cpp
    Source/PadSlot.cpp
    Source/SampleLoader.cpp
    Source/PastelLookAndFeel.cpp
    Source/NumberField.cpp
    Source/EnvelopeEditor.cpp
    Source/ControlCards.cpp
    Source/WaveformView.cpp
    Source/PadGrid.cpp
    Source/PresetManager.cpp
    Source/PresetBar.cpp)

set(SIMPLESAMPLER_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_sources(SimpleSampler PRIVATE ${SIMPLESAMPLER_SOURCES})
target_compile_definitions(SimpleSampler PUBLIC ${SIMPLESAMPLER_DEFINITIONS})

target_link_libraries(SimpleSampler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        signalsmith-stretch
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

if(MSVC)
    target_compile_options(SimpleSampler PRIVATE /W4 /utf-8)
endif()

# ---------------------------------------------------------------------------
# Tests
#   The test executable compiles the plugin sources itself instead of linking
#   the plugin library: linking both the library and the JUCE modules would put
#   two differently configured copies of JUCE into one program.
# ---------------------------------------------------------------------------
if(SIMPLESAMPLER_BUILD_TESTS)
    juce_add_console_app(SimpleSamplerTests PRODUCT_NAME "SimpleSamplerTests")
    target_sources(SimpleSamplerTests PRIVATE Tests/Tests.cpp ${SIMPLESAMPLER_SOURCES})
    target_include_directories(SimpleSamplerTests PRIVATE Source)
    target_compile_definitions(SimpleSamplerTests PRIVATE ${SIMPLESAMPLER_DEFINITIONS} SS_HAS_PRESETS=1)
    target_link_libraries(SimpleSamplerTests PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        signalsmith-stretch
        juce::juce_recommended_config_flags)
    if(MSVC)
        target_compile_options(SimpleSamplerTests PRIVATE /utf-8)
    endif()
    enable_testing()
    add_test(NAME SimpleSamplerTests COMMAND SimpleSamplerTests)
endif()

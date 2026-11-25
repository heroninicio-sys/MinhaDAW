cmake_minimum_required(VERSION 3.22)
project(MinhaDAW VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(MinhaDAW
    PRODUCT_NAME "MinhaDAW Studio"
    COMPANY_NAME "AudioDev"
    VERSION 1.0.0
)

juce_generate_juce_header(MinhaDAW)

target_sources(MinhaDAW PRIVATE
    Source/Main.cpp
    Source/AudioClip.h
    Source/MidiClip.h
    Source/PianoRollWindow.h
    Source/NativeDSPPlugins.h
    Source/TrackRackWindow.h
    Source/AudioTrack.h
    Source/AudioExporter.h
    Source/ProjectSerializer.h
    Source/TimelineComponent.h
    Source/MainComponent.h
)

target_compile_definitions(MinhaDAW PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(MinhaDAW PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
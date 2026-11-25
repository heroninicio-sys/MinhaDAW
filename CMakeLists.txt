cmake_minimum_required(VERSION 3.22)
project(MinhaDAW VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# 1. JUCE 8.0.4
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
)
FetchContent_MakeAvailable(JUCE)

# 2. Encoder MP3 LAME embutido nativamente no executável (elimina dependência de PATH/ffmpeg)
FetchContent_Declare(
    lame_src
    GIT_REPOSITORY https://github.com/gypified/libmp3lame.git
    GIT_TAG        master
)
FetchContent_GetProperties(lame_src)
if(NOT lame_src_POPULATED)
    FetchContent_Populate(lame_src)
endif()

file(GLOB LAME_C_FILES "${lame_src_SOURCE_DIR}/libmp3lame/*.c")
list(FILTER LAME_C_FILES EXCLUDE REGEX ".*(mpglib|xmm_quantize_sub).*")

add_library(embedded_lame STATIC ${LAME_C_FILES})
target_include_directories(embedded_lame PUBLIC
    "${lame_src_SOURCE_DIR}/include"
    "${lame_src_SOURCE_DIR}/libmp3lame"
)
target_compile_definitions(embedded_lame PRIVATE HAVE_CONFIG_H=0 TAKEHIRO_IEEE754_HACK=1)
if(MSVC)
    target_compile_options(embedded_lame PRIVATE /W0 /wd4244 /wd4267 /wd4996)
endif()

juce_add_gui_app(MinhaDAW
    PRODUCT_NAME "MinhaDAW Studio"
    COMPANY_NAME "AudioDev"
    VERSION 1.0.0
)

juce_generate_juce_header(MinhaDAW)

target_sources(MinhaDAW PRIVATE
    Source/Main.cpp
    Source/StudioLookAndFeel.h
    Source/Metronome.h
    Source/PluginScanner.h
    Source/AudioClip.h
    Source/MidiClip.h
    Source/PianoRollWindow.h
    Source/NativeDSPPlugins.h
    Source/TrackRackWindow.h
    Source/AudioTrack.h
    Source/AudioExporter.h
    Source/ProjectSerializer.h
    Source/TimelineComponent.h
    Source/MixerComponent.h
    Source/WelcomeAndActions.h
    Source/MainComponent.h
)

target_compile_definitions(MinhaDAW PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_USE_MP3AUDIOFORMAT=1
)

target_link_libraries(MinhaDAW PRIVATE
    embedded_lame
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)

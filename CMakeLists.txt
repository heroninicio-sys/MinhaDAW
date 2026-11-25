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

# 2. Encoder MP3 LAME embutido nativamente com config.h gerado para MSVC x64
FetchContent_Declare(
    lame_src
    GIT_REPOSITORY https://github.com/gypified/libmp3lame.git
    GIT_TAG        master
)
FetchContent_GetProperties(lame_src)
if(NOT lame_src_POPULATED)
    FetchContent_Populate(lame_src)
endif()

set(LAME_GEN_INC "${CMAKE_CURRENT_BINARY_DIR}/lame_generated_inc")
file(MAKE_DIRECTORY "${LAME_GEN_INC}")

file(WRITE "${LAME_GEN_INC}/config.h" [=[
#pragma once
#define STDC_HEADERS 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_LIMITS_H 1
#define HAVE_FCNTL_H 1
#define HAVE_ERRNO_H 1
#define HAVE_TIME_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_VSNPRINTF 1
#define HAVE_SNPRINTF 1
#define HAVE_ALLOCA 1
#define LAME_LIBRARY_BUILD 1
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
]=])

file(GLOB LAME_C_FILES "${lame_src_SOURCE_DIR}/libmp3lame/*.c")
list(FILTER LAME_C_FILES EXCLUDE REGEX ".*(mpglib|xmm_quantize_sub|i386|vector|fft3dn|tas).*")

add_library(embedded_lame STATIC ${LAME_C_FILES})
target_include_directories(embedded_lame PUBLIC
    "${LAME_GEN_INC}"
    "${lame_src_SOURCE_DIR}/include"
    "${lame_src_SOURCE_DIR}/libmp3lame"
)
target_compile_definitions(embedded_lame PRIVATE HAVE_CONFIG_H=1 _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(embedded_lame PRIVATE /W0 /wd4244 /wd4267 /wd4305 /wd4996 /FI"${LAME_GEN_INC}/config.h")
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

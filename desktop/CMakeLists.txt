# Desktop player: plays the TinyInstruments with a (USB) midi keyboard on the
# sound card of the computer. Dependencies are downloaded with FetchContent:
#  - RtMidi: midi input (ALSA on Linux, CoreMIDI on macOS, WinMM on Windows)
#  - miniaudio: audio output
#  - arduino-midi: midi parser (the sibling directory is used if available)
include(FetchContent)

FetchContent_Declare(rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG 6.0.0
    GIT_SHALLOW TRUE)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTMIDI_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rtmidi)

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()

if(NOT TARGET arduino-midi)
    if(EXISTS "${PROJECT_SOURCE_DIR}/../arduino-midi/CMakeLists.txt")
        set(MIDI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${PROJECT_SOURCE_DIR}/../arduino-midi" arduino-midi)
    else()
        FetchContent_Declare(arduino-midi
            GIT_REPOSITORY https://github.com/pschatzmann/arduino-midi.git
            GIT_TAG main
            GIT_SHALLOW TRUE)
        set(MIDI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(arduino-midi)
    endif()
endif()

find_package(Threads REQUIRED)

add_executable(tinyinstruments-player player.cpp serial_baud_linux.cpp)
target_include_directories(tinyinstruments-player PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(tinyinstruments-player PRIVATE TinyInstruments arduino-midi rtmidi
    Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(tinyinstruments-player PRIVATE m)
endif()
if(WIN32)
    target_link_libraries(tinyinstruments-player PRIVATE ws2_32)
endif()
# piano and drums together need more voices
target_compile_definitions(tinyinstruments-player PRIVATE TI_MAX_VOICES=32
    TI_DATA_DIR="${PROJECT_SOURCE_DIR}/data")

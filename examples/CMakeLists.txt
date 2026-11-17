# Builds the example sketches for the desktop with the Arduino-Emulator: the
# sketches are compiled unchanged, the platform specific parts are provided
# by the libraries:
#  - arduino-audio-tools: audio, the I2SStream (see TinyInstrumentsAudioTools.h)
#    and the I2SCodecStream are the sound card (miniaudio)
#  - Arduino-Emulator: Arduino API, Serial/Serial2 (stdout/stdin), WiFi
#    (sockets of the computer), LittleFS (a directory, by default the data
#    folder of this library) and ESP
#  - arduino-midi: midi
#  - TinyGPU/TinyMaterialDesign: display and touch in an SDL window (only for
#    material-gui, if SDL2 is installed)
# and by desktop/emulator for the hardware of material-gui: the display board
# (DesktopBoards.h) and the audio board (AudioBoards/*.h, arduino-audio-driver
# is not used on the desktop).
#
# The libraries are taken from the sibling directories (Arduino libraries
# folder) if available, otherwise they are downloaded. Use
# -DFETCHCONTENT_SOURCE_DIR_ARDUINO_EMULATOR=<dir> for a local
# Arduino-Emulator checkout.
include(FetchContent)

# --- Arduino-Emulator
if(NOT TARGET arduino_emulator)
    FetchContent_Declare(arduino_emulator
        GIT_REPOSITORY https://github.com/pschatzmann/Arduino-Emulator.git
        GIT_TAG main
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(arduino_emulator)
endif()

# --- a library from the sibling directory or from GitHub
function(ti_library name repository)
    if(EXISTS "${PROJECT_SOURCE_DIR}/../${name}/CMakeLists.txt")
        add_subdirectory("${PROJECT_SOURCE_DIR}/../${name}" ${CMAKE_BINARY_DIR}/libs/${name})
    else()
        FetchContent_Declare(${name}
            GIT_REPOSITORY ${repository}
            GIT_TAG main
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(${name})
    endif()
endfunction()

if(NOT TARGET arduino-audio-tools)
    set(ADD_ARDUINO_EMULATOR OFF CACHE BOOL "" FORCE)
    set(ADD_PORTAUDIO OFF CACHE BOOL "" FORCE)
    ti_library(arduino-audio-tools https://github.com/pschatzmann/arduino-audio-tools.git)
endif()
if(NOT TARGET arduino-midi)
    set(MIDI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    ti_library(arduino-midi https://github.com/pschatzmann/arduino-midi.git)
endif()

# --- miniaudio: the sound card (MiniAudioStream of arduino-audio-tools)
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()

find_package(Threads REQUIRED)

# --- one executable per sketch: examples/<name>/<name>.ino
function(ti_example name)
    set(ino "${CMAKE_CURRENT_SOURCE_DIR}/${name}/${name}.ino")
    set_source_files_properties(${ino} PROPERTIES LANGUAGE CXX)
    add_executable(${name} ${ino})
    # like the Arduino IDE: Arduino.h is included automatically
    target_compile_options(${name} PRIVATE -x c++ "SHELL:-include Arduino.h" -Wno-unused-parameter)
    target_compile_features(${name} PRIVATE cxx_std_17)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${name}
        ${miniaudio_SOURCE_DIR})
    # IS_DESKTOP: desktop platform of arduino-audio-tools
    # LITTLEFS_ROOT: directory of the LittleFS files of the Arduino-Emulator
    target_compile_definitions(${name} PRIVATE IS_DESKTOP
        LITTLEFS_ROOT="${PROJECT_SOURCE_DIR}/data")
    target_link_libraries(${name} PRIVATE TinyInstruments arduino-audio-tools arduino-midi
        arduino_emulator Threads::Threads ${CMAKE_DL_LIBS} ${ARGN})
    if(UNIX AND NOT APPLE)
        target_link_libraries(${name} PRIVATE m)
    endif()
endfunction()

foreach(example
        piano-demo drum-demo benchmark
        piano-serial-midi sax-serial-midi guitar-serial-midi bass-serial-midi
        clarinet-serial-midi organ-serial-midi piano-drums-serial-midi piano-ble-midi
        file-psram-serial-midi all-instruments)
    ti_example(${example})
endforeach()

# --- material-gui: display and touch in an SDL window (TinyGPU), the codec of
# the board is replaced by the sound card (I2SCodecStream of
# arduino-audio-tools, without arduino-audio-driver)
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    if(NOT TARGET TinyMaterialDesign)
        set(TMD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(TMD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(TGPU_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        if(NOT FETCHCONTENT_SOURCE_DIR_TINYGPU AND EXISTS "${PROJECT_SOURCE_DIR}/../TinyGPU/CMakeLists.txt")
            set(FETCHCONTENT_SOURCE_DIR_TINYGPU "${PROJECT_SOURCE_DIR}/../TinyGPU")
        endif()
        ti_library(TinyMaterialDesign https://github.com/pschatzmann/TinyMaterialDesign.git)
    endif()
    ti_example(material-gui TinyMaterialDesign SDL2::SDL2)
    # like in the Arduino IDE: the namespaces of TinyGPU and TinyMaterialDesign
    # are active
    target_compile_definitions(material-gui PRIVATE TINYGPU_AUTO_NAMESPACE TINYMD_AUTO_NAMESPACE)
    # the display board of the sketch as SDL window
    target_compile_options(material-gui PRIVATE
        "SHELL:-include ${PROJECT_SOURCE_DIR}/desktop/emulator/DesktopBoards.h")
    # arduino-audio-driver is not used: desktop/emulator replaces the board
    # definition (AudioBoards/*.h), the codec is the sound card
    target_include_directories(material-gui BEFORE PRIVATE ${PROJECT_SOURCE_DIR}/desktop/emulator)
else()
    message(STATUS "SDL2 not found: material-gui is not built")
endif()

# --- run the sketches: a sketch passes if it is still running after 3 seconds
# (no crash or exit); "ctest -L examples"
if(UNIX)
    enable_testing()
    set(TI_EXAMPLE_TESTS
        piano-demo drum-demo benchmark
        piano-serial-midi sax-serial-midi guitar-serial-midi bass-serial-midi
        clarinet-serial-midi organ-serial-midi piano-drums-serial-midi piano-ble-midi
        file-psram-serial-midi all-instruments)
    if(TARGET material-gui)
        list(APPEND TI_EXAMPLE_TESTS material-gui)
    endif()
    foreach(example ${TI_EXAMPLE_TESTS})
        add_test(NAME run-${example}
            COMMAND sh -c "timeout 3 \"$<TARGET_FILE:${example}>\" < /dev/null > /dev/null; test $? -eq 124")
        # no window: SDL without video device
        set_tests_properties(run-${example} PROPERTIES LABELS examples TIMEOUT 20
            ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    endforeach()
endif()

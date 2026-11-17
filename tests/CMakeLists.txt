# Unit tests: run with "ctest" in the build directory
foreach(test_name test_adpcm test_voice test_synthesizer test_piano test_drums test_sax test_loader test_guitar test_bass test_clarinet test_psola test_small test_octave)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE TinyInstruments)
    target_compile_options(${test_name} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_definitions(${test_name} PRIVATE TI_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# Renders a demo of all instruments to demo.wav for listening
add_executable(render_demo render_demo.cpp)
target_link_libraries(render_demo PRIVATE TinyInstruments)
# piano and drums together need more voices
target_compile_definitions(render_demo PRIVATE TI_MAX_VOICES=16)

# Renders the example recordings of the README (docs/audio): one wav file per
# instrument and version, loaded from the data folder
add_executable(render_examples render_examples.cpp)
target_link_libraries(render_examples PRIVATE TinyInstruments)
target_compile_definitions(render_examples PRIVATE TI_DATA_DIR="${PROJECT_SOURCE_DIR}/data")

# midi forwarders of the desktop player (POSIX only)
if(UNIX)
    add_executable(test_forwarders test_forwarders.cpp ${PROJECT_SOURCE_DIR}/desktop/serial_baud_linux.cpp)
    target_link_libraries(test_forwarders PRIVATE TinyInstruments util)
    target_compile_options(test_forwarders PRIVATE -Wall -Wextra -Wno-unused-parameter)
    add_test(NAME test_forwarders COMMAND test_forwarders)
endif()

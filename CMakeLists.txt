cmake_minimum_required(VERSION 3.16)

project(TinyInstruments LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(TI_IS_TOP_LEVEL ON)
else()
    set(TI_IS_TOP_LEVEL OFF)
endif()

option(TI_BUILD_TESTS "Build the TinyInstruments unit tests" ${TI_IS_TOP_LEVEL})
option(TI_BUILD_DESKTOP "Build the desktop player (downloads RtMidi and miniaudio)" OFF)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# Header only library: the core does not depend on Arduino
add_library(TinyInstruments INTERFACE)
target_include_directories(TinyInstruments INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_library(TinyInstruments::TinyInstruments ALIAS TinyInstruments)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/"
    DESTINATION "include/TinyInstruments"
    FILES_MATCHING PATTERN "*.h")

if(TI_BUILD_DESKTOP)
    add_subdirectory(desktop)
endif()

if(TI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

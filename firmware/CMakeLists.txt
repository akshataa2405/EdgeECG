# Top-level CMakeLists.txt for ESP-IDF
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake OPTIONAL RESULT_VARIABLE IDF_PROJECT_INCLUDED)

if(NOT IDF_PROJECT_INCLUDED)
    # Host testing build mode (Native Linux / macOS)
    project(edgeecg C)
    set(CMAKE_C_STANDARD 99)
    add_subdirectory(main)
    enable_testing()
    add_subdirectory(test)
else()
    # ESP-IDF Target build mode
    project(edgeecg)
endif()

cmake_minimum_required(VERSION 3.5)

project(synth C)

set(C_STANDARD C17)

include_directories(synth src/)
add_library(synth SHARED
    src/main.c
    # TODO: make this configurable via platform
    src/gui/gui_x11.c
)

link_libraries(synth PRIVATE m)

include_directories(synth deps/clap)

set_target_properties(synth PROPERTIES PREFIX "")
set_target_properties(synth PROPERTIES OUTPUT_NAME "Sampler")
set_target_properties(synth PROPERTIES SUFFIX ".clap")

# Compiler flags
if (CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    # WHAT
    set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} /Z7 /W4 /wd4146 /wd4245 /RTCcsu")
else()
    set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g3 -Wnull-dereference -Wwrite-strings -Wformat=2 -Wcast-align -Wconversion -Wdouble-promotion -Wno-unused-parameter -Wno-unused-function -Wno-sign-conversion -fsanitize=undefined -fsanitize-trap")
endif()
cmake_minimum_required(VERSION 3.10)
project(ats_flight_mode)

set(CMAKE_CXX_STANDARD 17)

# Add source files
set(SOURCES
    src/flight_mode.cpp
    src/flight_mode.h
)

# Create the DLL
add_library(ats_flight_mode SHARED ${SOURCES})

# Link libraries
target_link_libraries(ats_flight_mode user32 kernel32)

# Set output directory
set_target_properties(ats_flight_mode PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/win_x64/plugins"
)

# Copy to mod directory
add_custom_command(TARGET ats_flight_mode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/bin
    ${CMAKE_SOURCE_DIR}/bin
)

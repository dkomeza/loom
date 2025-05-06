# Minimum CMake version required
cmake_minimum_required(VERSION 3.10)

# Project name and language (optional, but good practice)
project(ebike-display C) # Or C if you are using C

# Set C++ standard if needed (e.g., C++17)
# set(CMAKE_CXX_STANDARD 17)
# set(CMAKE_CXX_STANDARD_REQUIRED True)

# Define the name of your executable
set(EXECUTABLE_NAME "ebike-display")

# --- Source Files ---
# Automatically find all .cpp files in the src directory
# For more complex projects, explicitly listing files is often preferred for clarity
# and to avoid issues if non-source files are added to src/.
file(GLOB_RECURSE SOURCE_FILES "src/*.c") # Add "src/*.c" if you have C files

# --- Dependencies ---
# Include directories for headers
# This tells the compiler where to find #include <header.h> files
# from your dependencies. Adjust paths if your libs have different structures.
include_directories(dependencies/minifb/include)
# Add more include_directories for each dependency

add_subdirectory(dependencies/minifb)

# --- Executable ---
# Add the executable target and specify its source files
add_executable(${EXECUTABLE_NAME} ${SOURCE_FILES})

target_link_libraries(${PROJECT_NAME}
    loom
    minifb
)

# --- Optional: Installation ---
# If you want to install your program (e.g., with 'make install')
# install(TARGETS ${EXECUTABLE_NAME} DESTINATION bin)
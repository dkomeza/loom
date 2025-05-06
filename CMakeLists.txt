cmake_minimum_required(VERSION 3.14)
project(loom)

# Make your library
add_subdirectory(src)

# Add the include directory to the project
include_directories(${PROJECT_SOURCE_DIR}/include)

# Optionally build examples
option(BUILD_EXAMPLES "Build examples" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

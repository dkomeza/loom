#!/bin/bash

# Create a build directory if it doesn't exist
mkdir -p build

# Navigate into the build directory
cd build

# Run CMake to configure the project
# You can specify a generator, e.g., "Unix Makefiles" or "Ninja"
# CMake usually picks a sensible default.
cmake ..

# Build the project using the generated build system (e.g., make or ninja)
# The '--parallel' flag will try to build using multiple cores.
cmake --build . --parallel

# Optional: Go back to the project root directory
# cd ..
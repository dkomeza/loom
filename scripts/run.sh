#!/bin/bash

set -e  # Exit on error

EXAMPLE_TYPE="desktop"
EXAMPLE_NAME="ebike-display"  # Change this to the example you want to build/run
BUILD_DIR="build/$EXAMPLE_TYPE/$EXAMPLE_NAME"

# Step 1: Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Step 2: Configure with CMake, only enabling the desired example
cmake -DBUILD_EXAMPLES=ON ../../..

# Step 3: Build the specific example
cmake --build . --target "$EXAMPLE_NAME"

# Step 4: Run the example
echo "Running $EXAMPLE_NAME..."
"./examples/$EXAMPLE_TYPE/$EXAMPLE_NAME/$EXAMPLE_NAME" 

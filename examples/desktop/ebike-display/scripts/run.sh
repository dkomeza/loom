#!/bin/bash


./scripts/build.sh
# Check if the build was successful
if [ $? -ne 0 ]; then
    echo "Build failed. Please check the output for errors."
    exit 1
fi

echo -e "Build successful. Running the application... \n"

# Run the application
./build/ebike-display
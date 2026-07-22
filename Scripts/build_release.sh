#!/usr/bin/env bash

# Create and navigate to the build directory (default release build)
echo ""
build_dir="build"

# Wipe if the existing build dir was configured with a different
# generator (e.g. leftover Unix Makefiles) — CMake won't silently
# re-generate across generators.
if [ -f "$build_dir/CMakeCache.txt" ] && ! grep -q "CMAKE_GENERATOR:INTERNAL=Ninja" "$build_dir/CMakeCache.txt"; then
    echo "Existing build dir uses a different generator — wiping for a clean Ninja config..."
    rm -rf "$build_dir"
fi

mkdir -p "$build_dir"
cd "$build_dir"

# Run CMake and Ninja for release build
echo "Building Release Version..."
cmake -GNinja -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

echo "Release build completed in '$build_dir' directory!"

# Return to project root
cd ..
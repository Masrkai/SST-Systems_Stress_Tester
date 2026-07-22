#!/usr/bin/env bash

# build profiling version automatically
echo ""
profiling_dir="profiling"

if [ -f "$profiling_dir/CMakeCache.txt" ] && ! grep -q "CMAKE_GENERATOR:INTERNAL=Ninja" "$profiling_dir/CMakeCache.txt"; then
    echo "Existing profiling dir uses a different generator — wiping for a clean Ninja config..."
    rm -rf "$profiling_dir"
fi

mkdir -p "$profiling_dir"
cd "$profiling_dir"

echo "Building Profiling version for performance analysis..."
cmake -GNinja -DCMAKE_BUILD_TYPE=Profiling ..
cmake --build .

echo "Profiling build completed in '$profiling_dir' directory!"

# Return to project root
cd ..

echo ""
echo "=== Build Summary ==="
echo "✓ Release build available in: ./build/"
echo "✓ Profiling build available in: ./profiling/"
echo ""
echo "Available commands:"
echo "  make build-release    # Builds release in build/"
echo "  make build-debug      # Builds debug in debug/"
echo "  make build-profiling  # Builds profiling in profiling/"
echo ""
echo "To run performance analysis:"
echo "  ./profile.sh [program_arguments]"

# Don't exit automatically - let user stay in the shell
# exit
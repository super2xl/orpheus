#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Check for cmake
if ! command -v cmake &>/dev/null; then
    echo "Error: cmake not found in PATH"
    exit 1
fi

# Step 1: Build C++ core DLL
echo "=== Building C++ core ==="
mkdir -p build
cmake -S . -B build 2>&1
cmake --build build --config Release --target orpheus_core 2>&1
if [ $? -ne 0 ]; then
    echo "Error: C++ build failed"
    exit 1
fi
echo "C++ core built successfully"

# Step 2: Copy DLL to Tauri target directory
echo "=== Copying DLL ==="
mkdir -p ui/src-tauri/target/release
cp build/bin/Release/orpheus_core.dll ui/src-tauri/target/release/
echo "DLL copied to ui/src-tauri/target/release/"

# Step 3: Build frontend
echo "=== Building frontend ==="
cd ui
npm run build 2>&1
if [ $? -ne 0 ]; then
    echo "Error: Frontend build failed"
    exit 1
fi
echo "Frontend built successfully"

# Step 4: Build Tauri app
echo "=== Building Tauri app ==="
npx tauri build 2>&1
if [ $? -ne 0 ]; then
    echo "Error: Tauri build failed"
    exit 1
fi
echo "Tauri app built successfully"

cd "$SCRIPT_DIR"
echo "=== Build complete ==="

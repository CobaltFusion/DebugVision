#!/bin/sh
# Builds DebugVision on Linux and macOS.
# COPPERSPICE_DIR points to a GUI-enabled CopperSpice install prefix; without it only dvcore and dvconsole are built.
set -e
if [ -n "$COPPERSPICE_DIR" ]; then
    CS_CONFIG=$(dirname "$(find "$COPPERSPICE_DIR" -name CopperSpiceConfig.cmake | head -1)")
    GUI="-DCMAKE_PREFIX_PATH=$COPPERSPICE_DIR -DCopperSpice_DIR=$CS_CONFIG"
else
    GUI="-DDV_BUILD_GUI=OFF"
fi
cmake -G Ninja -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=build/release/install $GUI
cmake --build build/release -t install
ctest --test-dir build/release --output-on-failure

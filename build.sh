#!/bin/sh
# Builds DebugVision on Linux and macOS.
# The GUI is built when COPPERSPICE_DIR points to a GUI-enabled CopperSpice install prefix or /opt/copperspice exists (see README.md);
# otherwise only dvcore and dvconsole are built.
set -e
cmake -G Ninja -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=build/release/install "$@"
cmake --build build/release -t install
ctest --test-dir build/release --output-on-failure

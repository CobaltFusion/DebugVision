#/bin/bash
set -eu

rm -rf build/release
cmake --preset="release"
cmake --build --preset="release"
cmake --build --preset="release" --target install

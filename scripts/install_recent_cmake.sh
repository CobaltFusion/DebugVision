#!/bin/bash
# script to install a recent cmake version in CI build env

set -eu

CMAKEVER=3.22.1
curl -sL https://github.com/Kitware/CMake/releases/download/v${CMAKEVER}/cmake-${CMAKEVER}-linux-x86_64.tar.gz -o cmake.tar.gz
tar xzf cmake.tar.gz
ls -l
mv cmake-${CMAKEVER}-linux-x86_64 /opt/cmake
ln -s /opt/cmake/bin/cmake /usr/bin/cmake
ln -s /opt/cmake/bin/ctest /usr/bin/ctest
ln -s /opt/cmake/bin/cpack /usr/bin/cpack
rm cmake.tar.gz

cmake -version

:: Builds DebugVision with Ninja; run from a "x64 Native Tools Command Prompt for VS 2022".
:: COPPERSPICE_DIR points to a GUI-enabled CopperSpice install (see C:\project\copperspice\build-gui.bat)
:: generate a build configuration: cmake -G <generator> -B <dir>
:: build a generated configuration: cmake --build [-t <target>]

if "%COPPERSPICE_DIR%"=="" set COPPERSPICE_DIR=C:/project/copperspice/install-gui

cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=%COPPERSPICE_DIR% -DCopperSpice_DIR=%COPPERSPICE_DIR%/cmake/CopperSpice -B build/debug
cmake --build build/debug -t install

cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=%COPPERSPICE_DIR% -DCopperSpice_DIR=%COPPERSPICE_DIR%/cmake/CopperSpice -B build/release
cmake --build build/release -t install
ctest --test-dir build/release --output-on-failure

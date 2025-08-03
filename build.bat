:: generate a build configuration: cmake -G <generator> -B <dir> 
:: build a generated configuration: cmake --build [-t <target>]

cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -B build/debug
cmake --build build/debug -t install

cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -B build/release
cmake --build build/release -t install

:: cmake -G "Visual Studio 17 2022" -A x64 -T v142,host=x64 %SRC%

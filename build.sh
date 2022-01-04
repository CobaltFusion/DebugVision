rm -rf build/release
mkdir -p build/release
cd build/release
cmake -G Ninja ../.. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=install
ninja 


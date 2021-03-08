#!/bin/bash

mkdir -p bin/script

# copy readme
sed 's/\r$//' README.md | sed 's/$/\r/' > bin/bridge.txt

# update version string
VERSION='v0.6'
GITHASH=`git rev-parse --short HEAD`
echo -n "$VERSION ( $GITHASH )" > "VERSION"
cat << EOS | sed 's/\r$//' | sed 's/$/\r/' > 'src/ver.h'
#pragma once
#define VERSION "$VERSION ( $GITHASH )"
EOS

# build
# using packages:
#   pacman -S mingw-w64-i686-ninja
#   pacman -S mingw-w64-i686-clang
#   pacman -S mingw-w64-i686-cmake
#   pacman -S mingw-w64-i686-lua51
# mkdir build
# cd build
# CC=clang cmake -GNinja -DCMAKE_BUILD_TYPE=Release ..
# ninja

mkdir -p build
rm -rf build/*
cd build
CC=clang cmake.exe -GNinja -DCMAKE_BUILD_TYPE=Release ..
ninja
cd ..

rm -rf bin/bridge.auf bin/script/bridge.dll
cp build/src/bridge.auf bin/bridge.auf
cp build/src/bridge.dll bin/script/bridge.dll

cd bin
zip aviutl_bridge_${VERSION}.zip bridge.auf bridge.txt script/bridge.dll
cd ..

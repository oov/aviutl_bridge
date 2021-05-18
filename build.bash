#!/bin/bash

mkdir -p bin

# copy readme
sed 's/\r$//' README.md | sed 's/$/\r/' > bin/bridge.txt

# update version string
VERSION='v0.12'
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
cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang ..
ninja
cd ..

rm -rf bin/bridge.dll
cp build/src/bridge.dll bin/bridge.dll

cd bin
zip aviutl_bridge_wip.zip bridge.txt bridge.dll
cd ..

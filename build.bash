#!/bin/bash

mkdir -p bin/script

# copy readme
sed 's/\r$//' README.md | sed 's/$/\r/' > bin/bridge.txt

# update version string
VERSION='v0.5'
GITHASH=`git rev-parse --short HEAD`
echo -n "$VERSION ( $GITHASH )" > "VERSION"
cat << EOS | sed 's/\r$//' | sed 's/$/\r/' > 'src/ver.h'
#pragma once
#define VERSION "$VERSION ( $GITHASH )"
EOS

# build
# pacman -S mingw-w64-i686-gcc
# pacman -S mingw-w64-i686-clang
# pacman -S mingw-w64-i686-make
# pacman -S mingw-w64-i686-lua51
PATH=$(wslpath "C:\msys64\mingw32\bin") WSLENV=PATH/lw mingw32-make.exe


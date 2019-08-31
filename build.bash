#!/bin/bash

mkdir -p bin/script

# copy readme
sed 's/\r$//' README.md | sed 's/$/\r/' > bin/bridge.txt

# update version string
VERSION='v0.3'
GITHASH=`git rev-parse --short HEAD`
echo -n "$VERSION ( $GITHASH )" > "VERSION"
cat << EOS | sed 's/\r$//' | sed 's/$/\r/' > 'src/ver.h'
#pragma once
#define VERSION "$VERSION ( $GITHASH )"
EOS

# build
PATH=$(wslpath "C:\Program Files (x86)\mingw-w64\i686-8.1.0-win32-dwarf-rt_v6-rev0\mingw32\bin") WSLENV=PATH/lw mingw32-make.exe

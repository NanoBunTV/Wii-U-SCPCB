#!/usr/bin/env bash
# Builds bb2cpp.exe (Windows) with MinGW-w64 g++ (C:\msys64\mingw64). See Docs/BUILDING.md.
set -e
cd "$(dirname "$0")"
GXX="${GXX:-/c/msys64/mingw64/bin/g++.exe}"
PATH="$(dirname "$GXX"):$PATH" "$GXX" -std=gnu++17 -O1 -g0 -w -static -o bb2cpp.exe *.cpp ../../runtime/stdutil/stdutil.cpp
echo "built $(pwd)/bb2cpp.exe"

#!/usr/bin/env bash
# Builds texconv.exe (Windows) with MinGW-w64 g++ (C:\msys64\mingw64). See Docs/BUILDING.md.
set -e
cd "$(dirname "$0")"
GXX="${GXX:-/c/msys64/mingw64/bin/g++.exe}"
PATH="$(dirname "$GXX"):$PATH" "$GXX" -std=gnu++17 -O2 -w -static -o texconv.exe texconv.cpp
echo "built $(pwd)/texconv.exe"

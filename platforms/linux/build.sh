#!/bin/sh
# Builds the Fcitx5 addon and the settings windows into build/ with CMake and
# Ninja, and runs the engine and controller tests. Needs the packages listed in
# README.md. The McBopomofo engine and dictionary
# (MIT) are fetched at a pinned commit into build/. Install with
#   sudo cmake --install build
set -eu
cd "$(dirname "$0")"
BUILD=build
MCB=$BUILD/mcbopomofo
MCB_COMMIT=$(tr -d '[:space:]' < ../../MCBOPOMOFO_COMMIT)

if [ ! -d "$MCB/.git" ]; then
  git init -q "$MCB"
  git -C "$MCB" remote add origin https://github.com/openvanilla/McBopomofo
  git -C "$MCB" fetch -q --depth 1 origin "$MCB_COMMIT"
  git -C "$MCB" checkout -q FETCH_HEAD
fi
[ -f "$MCB/Source/Data/data.txt" ] || make -C "$MCB/Source/Data" data.txt

cmake -S . -B $BUILD -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DMCBOPOMOFO="$PWD/$MCB" >/dev/null
cmake --build $BUILD
for test in ComposerTest ControllerTest; do
  "$BUILD/$test" "$MCB/Source/Data/data.txt"
done
echo "built $BUILD/projectkey.so"

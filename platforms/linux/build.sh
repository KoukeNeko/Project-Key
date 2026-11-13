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

cxxlib() {
  case $(readelf -d "$1" 2>/dev/null | grep NEEDED) in
    *libc++.so*) echo libc++ ;;
    *libstdc++.so*) echo libstdc++ ;;
  esac
}
FCITX_CORE=$(pkg-config --variable=libdir Fcitx5Core 2>/dev/null)/libFcitx5Core.so
FCITX_CXXLIB=$(cxxlib "$FCITX_CORE")
if [ "$FCITX_CXXLIB" = libc++ ]; then
  if [ -z "${CXX:-}" ]; then
    command -v clang++ >/dev/null || { echo "Fcitx5 is built with libc++: install clang, or set CXX to a compiler with libc++" >&2; exit 1; }
    CXX=clang++
  fi
  export CXX CXXFLAGS="-stdlib=libc++ ${CXXFLAGS:-}" LDFLAGS="-stdlib=libc++ ${LDFLAGS:-}"
fi

../../tools/fetch-mcbopomofo.sh "$MCB"

cmake -S . -B $BUILD -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DMCBOPOMOFO="$PWD/$MCB" >/dev/null
cmake --build $BUILD
for test in ComposerTest ControllerTest; do
  "$BUILD/$test" "$MCB/Source/Data/data.txt"
done
"$BUILD/PoolStressTest" 5
sh ./test-profile.sh
ADDON_CXXLIB=$(cxxlib $BUILD/projectkey.so)
if [ -n "$FCITX_CXXLIB" ] && [ "$ADDON_CXXLIB" != "$FCITX_CXXLIB" ]; then
  echo "warning: Fcitx5 uses $FCITX_CXXLIB but projectkey.so uses $ADDON_CXXLIB: Fcitx5 won't be able to load it" >&2
fi
echo "built $BUILD/projectkey.so"

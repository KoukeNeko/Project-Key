#!/bin/sh
# Builds .build/ProjectKey.app and runs the composer test. Needs Xcode command
# line tools and CMake. The McBopomofo engine and dictionary (MIT) are fetched
# at a pinned commit into .build/.
set -eu
cd "$(dirname "$0")"
BUILD=.build
MCB_COMMIT=257da536ddaf590aae0e74e6adb9b415988344f6
MCB=$BUILD/mcbopomofo
APP=$BUILD/ProjectKey.app
CXXFLAGS="-std=c++20 -O2 -Wall -mmacosx-version-min=13.0 -I$MCB/Source/Engine"
LIBS="$MCB/build/libMcBopomofoLMLib.a $MCB/build/Mandarin/libMandarinLib.a $MCB/build/gramambular2/libgramambular2_lib.a"

if [ ! -d "$MCB/.git" ]; then
  git init -q "$MCB"
  git -C "$MCB" remote add origin https://github.com/openvanilla/McBopomofo
  git -C "$MCB" fetch -q --depth 1 origin "$MCB_COMMIT"
  git -C "$MCB" checkout -q FETCH_HEAD
fi
[ -f "$MCB/Source/Data/data.txt" ] || make -C "$MCB/Source/Data" data.txt
cmake -S "$MCB/Source/Engine" -B "$MCB/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 >/dev/null
cmake --build "$MCB/build" --target McBopomofoLMLib MandarinLib gramambular2_lib >/dev/null

# shellcheck disable=SC2086
c++ $CXXFLAGS Sources/Composer.cpp Tests/ComposerTest.cpp $LIBS -o $BUILD/ComposerTest
$BUILD/ComposerTest "$MCB/Source/Data/data.txt"
# ControllerTest finds data.txt next to itself, as the app finds it in Resources.
ln -sf "$PWD/$MCB/Source/Data/data.txt" $BUILD/data.txt
# shellcheck disable=SC2086
c++ $CXXFLAGS -fobjc-arc Sources/Composer.cpp Sources/CandidatePanel.mm Sources/InputController.mm Tests/ControllerTest.mm $LIBS \
  -framework Cocoa -framework InputMethodKit -framework Carbon -o $BUILD/ControllerTest
if ! out=$($BUILD/ControllerTest 2>&1); then echo "$out" >&2; exit 1; fi
echo "$out" | grep -v 'Keyboard Layouts\|TISFileInterrogator' || true  # system noise from loading input sources

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
# shellcheck disable=SC2086
c++ $CXXFLAGS -fobjc-arc Sources/*.cpp Sources/*.mm $LIBS \
  -framework Cocoa -framework InputMethodKit -framework Carbon \
  -o "$APP/Contents/MacOS/ProjectKey"
cp Resources/Info.plist "$APP/Contents/"
cp Resources/icon.tiff "$MCB/Source/Data/data.txt" "$APP/Contents/Resources/"
cp -R Resources/*.lproj "$APP/Contents/Resources/"
cp "$MCB/LICENSE.txt" "$APP/Contents/Resources/McBopomofo-LICENSE.txt"
codesign --force --sign - "$APP"
echo "built $APP"

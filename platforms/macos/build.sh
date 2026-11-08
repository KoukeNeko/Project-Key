#!/bin/sh
# Builds build/ProjectKey.app and runs the engine and controller tests. Needs Xcode command
# line tools and CMake. The McBopomofo engine and dictionary (MIT) are fetched
# at a pinned commit into build/. With --require-models it stops unless the trained models
# (tools/bigram-trainer/fetch-models.sh) are there, as releases need them.
set -eu
cd "$(dirname "$0")"
TRAINED=../../tools/bigram-trainer/.cache
if [ "${1:-}" = --require-models ]; then
  for file in data.txt bigram.bin chars.bin models-version.txt; do
    [ -f "$TRAINED/$file" ] || { echo "no $TRAINED/$file: run tools/bigram-trainer/fetch-models.sh" >&2; exit 1; }
  done
fi
BUILD=build
MCB_COMMIT=$(cat ../../MCBOPOMOFO_COMMIT)
CORE=../../core/engine  # the engine shared with Windows
ENGINE="$CORE/Composer.cpp $CORE/Bigram.cpp $CORE/CharModel.cpp $CORE/PersonalModel.cpp $CORE/Controller.cpp"
MCB=$BUILD/mcbopomofo
APP=$BUILD/ProjectKey.app
CXXFLAGS="-std=c++20 -O2 -Wall -mmacosx-version-min=13.0 -I$MCB/Source/Engine -I$CORE -I$BUILD"
LIBS="$MCB/build/libMcBopomofoLMLib.a $MCB/build/Mandarin/libMandarinLib.a $MCB/build/gramambular2/libgramambular2_lib.a"

if [ ! -d "$MCB/.git" ]; then
  git init -q "$MCB"
  git -C "$MCB" remote add origin https://github.com/openvanilla/McBopomofo
fi
# Again whenever MCBOPOMOFO_COMMIT moves, with the dictionary built from it.
if [ "$(git -C "$MCB" rev-parse -q --verify HEAD)" != "$MCB_COMMIT" ]; then
  git -C "$MCB" fetch -q --depth 1 origin "$MCB_COMMIT"
  git -C "$MCB" checkout -q FETCH_HEAD
  rm -f "$MCB/Source/Data/data.txt"
fi
[ -f "$MCB/Source/Data/data.txt" ] || make -C "$MCB/Source/Data" data.txt
cmake -S "$MCB/Source/Engine" -B "$MCB/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 >/dev/null
cmake --build "$MCB/build" --target McBopomofoLMLib MandarinLib gramambular2_lib >/dev/null

# The settings and welcome forms are SwiftUI; they expose ProjectKey-Swift.h to Objective-C.
xcrun swiftc -c -O -wmo -parse-as-library -module-name ProjectKey -target "$(uname -m)-apple-macos13.0" \
  -import-objc-header Sources/SettingsWindow.h -emit-objc-header-path $BUILD/ProjectKey-Swift.h \
  Sources/SettingsForm.swift Sources/WelcomeForm.swift -o $BUILD/SettingsForm.o
SWIFT_LINK="$BUILD/SettingsForm.o -L$(xcrun --show-sdk-path)/usr/lib/swift -L$(dirname "$(xcrun -f swiftc)")/../lib/swift/macosx -Wl,-rpath,/usr/lib/swift"

# shellcheck disable=SC2086
for test in ComposerTest ControllerTest; do
  c++ $CXXFLAGS $ENGINE "$CORE/Tests/$test.cpp" $LIBS -o "$BUILD/$test"
  "$BUILD/$test" "$MCB/Source/Data/data.txt"
done
# The IMK ControllerTest finds data.txt next to itself, as the app finds it in Resources.
ln -sf "$PWD/$MCB/Source/Data/data.txt" $BUILD/data.txt
# shellcheck disable=SC2086
c++ $CXXFLAGS -fobjc-arc $ENGINE Sources/CandidatePanel.mm Sources/SettingsWindow.mm Sources/InputController.mm Tests/IMKControllerTest.mm $LIBS $SWIFT_LINK \
  -framework Cocoa -framework QuartzCore -framework InputMethodKit -framework Carbon -o $BUILD/IMKControllerTest
if ! out=$($BUILD/IMKControllerTest 2>&1); then echo "$out" >&2; exit 1; fi
echo "$out" | grep -v 'Keyboard Layouts\|TISFileInterrogator' || true  # system noise from loading input sources

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
# shellcheck disable=SC2086
c++ $CXXFLAGS -fobjc-arc $ENGINE Sources/*.mm $LIBS $SWIFT_LINK \
  -framework Cocoa -framework QuartzCore -framework InputMethodKit -framework Carbon \
  -o "$APP/Contents/MacOS/ProjectKey"
cp Resources/Info.plist "$APP/Contents/"
cp Resources/icon.tiff "$MCB/Source/Data/data.txt" "$APP/Contents/Resources/"
cp ../linux/fcitx-projectkey.svg "$APP/Contents/Resources/"  # the icon, drawn by the welcome window
cp -R Resources/*.lproj "$APP/Contents/Resources/"
cp "$MCB/LICENSE.txt" "$APP/Contents/Resources/McBopomofo-LICENSE.txt"
# From tools/bigram-trainer: the dictionary with the words McBopomofo lacks, replacing
# McBopomofo's, and the context ranking model; the app falls back to unigrams without it.
if [ -f "$TRAINED/data.txt" ]; then cp "$TRAINED/data.txt" "$APP/Contents/Resources/"; fi
if [ -f "$TRAINED/bigram.bin" ]; then cp "$TRAINED/bigram.bin" "$APP/Contents/Resources/"; else echo "no $TRAINED/bigram.bin: building without context ranking"; fi
if [ -f "$TRAINED/chars.bin" ]; then cp "$TRAINED/chars.bin" "$APP/Contents/Resources/"; fi
if [ -f "$TRAINED/models-version.txt" ]; then cp "$TRAINED/models-version.txt" "$APP/Contents/Resources/"; fi
# Their licenses and sources go with them.
if [ -f "$TRAINED/data.txt" ] || [ -f "$TRAINED/bigram.bin" ]; then
  cp ../../tools/bigram-trainer/MODELS-NOTICE.txt ../../tools/bigram-trainer/LICENSE-LGPL-2.1.txt "$APP/Contents/Resources/"
fi
codesign --force --sign - "$APP"
echo "built $APP"

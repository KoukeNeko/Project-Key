#!/bin/sh
# Builds .build/ProjectKey.app and runs the engine and controller tests. Needs Xcode command
# line tools and CMake. The McBopomofo engine and dictionary (MIT) are fetched
# at a pinned commit into .build/.
set -eu
cd "$(dirname "$0")"
BUILD=.build
MCB_COMMIT=$(cat ../../MCBOPOMOFO_COMMIT)
CORE=../../core/engine  # the engine shared with Windows
ENGINE="$CORE/Composer.cpp $CORE/Bigram.cpp $CORE/Controller.cpp"
MCB=$BUILD/mcbopomofo
APP=$BUILD/ProjectKey.app
CXXFLAGS="-std=c++20 -O2 -Wall -mmacosx-version-min=13.0 -I$MCB/Source/Engine -I$CORE -I$BUILD"
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

# The settings form is SwiftUI; it exposes ProjectKey-Swift.h to Objective-C.
xcrun swiftc -c -O -parse-as-library -module-name ProjectKey -target "$(uname -m)-apple-macos13.0" \
  -import-objc-header Sources/SettingsWindow.h -emit-objc-header-path $BUILD/ProjectKey-Swift.h \
  Sources/SettingsForm.swift -o $BUILD/SettingsForm.o
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
cp -R Resources/*.lproj "$APP/Contents/Resources/"
cp "$MCB/LICENSE.txt" "$APP/Contents/Resources/McBopomofo-LICENSE.txt"
# From tools/bigram-trainer: the dictionary with the words McBopomofo lacks, replacing
# McBopomofo's, and the context ranking model; the app falls back to unigrams without it.
TRAINED=../../tools/bigram-trainer/.cache
if [ -f "$TRAINED/data.txt" ]; then cp "$TRAINED/data.txt" "$APP/Contents/Resources/"; fi
if [ -f "$TRAINED/bigram.bin" ]; then cp "$TRAINED/bigram.bin" "$APP/Contents/Resources/"; else echo "no $TRAINED/bigram.bin: building without context ranking"; fi
codesign --force --sign - "$APP"
echo "built $APP"

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
CORE=../../core/engine  # the engine shared with Windows
ENGINE="$CORE/Composer.cpp $CORE/Bigram.cpp $CORE/CharModel.cpp $CORE/SentenceModel.cpp $CORE/PersonalModel.cpp $CORE/Controller.cpp $CORE/InputMacro.cpp"
MCB=$BUILD/mcbopomofo
APP=$BUILD/ProjectKey.app
CXXFLAGS="-std=c++20 -O2 -Wall -mmacosx-version-min=13.0 -I$MCB/Source/Engine -I$CORE -I$BUILD"
LIBS="$MCB/build/libMcBopomofoLMLib.a $MCB/build/Mandarin/libMandarinLib.a $MCB/build/gramambular2/libgramambular2_lib.a"

../../tools/fetch-mcbopomofo.sh "$MCB"
cmake -S "$MCB/Source/Engine" -B "$MCB/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 >/dev/null
cmake --build "$MCB/build" --target McBopomofoLMLib MandarinLib gramambular2_lib >/dev/null

# PROJECTKEY_LLAMA, a llama.cpp checkout (MIT), builds in the advanced model (core/engine/AdvancedModel.h):
# one static CPU backend, as on Linux and Windows, for any Mac rather than the build machine's own.
if [ -n "${PROJECTKEY_LLAMA:-}" ]; then
  X86=""
  [ "$(uname -m)" = x86_64 ] && X86="-DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON"
  # shellcheck disable=SC2086
  cmake -S "$PROJECTKEY_LLAMA" -B "$BUILD/llama" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DBUILD_SHARED_LIBS=OFF \
    -DGGML_NATIVE=OFF -DGGML_METAL=OFF -DGGML_BLAS=OFF -DGGML_ACCELERATE=OFF -DGGML_OPENMP=OFF $X86 \
    -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_TOOLS=OFF -DLLAMA_BUILD_SERVER=OFF -DLLAMA_CURL=OFF >/dev/null
  cmake --build "$BUILD/llama" --target llama >/dev/null
  ENGINE="$ENGINE $CORE/AdvancedModel.cpp"
  CXXFLAGS="$CXXFLAGS -DPROJECTKEY_ADVANCED_MODEL -I$PROJECTKEY_LLAMA/include -I$PROJECTKEY_LLAMA/ggml/include"
  LIBS="$LIBS $BUILD/llama/src/libllama.a $BUILD/llama/ggml/src/libggml.a $BUILD/llama/ggml/src/libggml-cpu.a $BUILD/llama/ggml/src/libggml-base.a"
fi

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
cp Resources/icon.tiff Resources/AppIcon.icns "$MCB/Source/Data/data.txt" "$MCB/Source/Data/associated-phrases-v2.txt" "$APP/Contents/Resources/"
cp ../linux/fcitx-projectkey.svg "$APP/Contents/Resources/"  # the icon, drawn by the welcome window
cp -R Resources/*.lproj "$APP/Contents/Resources/"
cp "$MCB/LICENSE.txt" "$APP/Contents/Resources/McBopomofo-LICENSE.txt"
cp ../../core/data/emoji.txt ../../core/data/EMOJI-NOTICE.txt "$APP/Contents/Resources/"  # the emoji, and the license of their annotations
# From tools/bigram-trainer: the dictionary with the words McBopomofo lacks, replacing
# McBopomofo's, and the context ranking model; the app falls back to unigrams without it.
if [ -f "$TRAINED/data.txt" ]; then cp "$TRAINED/data.txt" "$APP/Contents/Resources/"; fi
if [ -f "$TRAINED/bigram.bin" ]; then cp "$TRAINED/bigram.bin" "$APP/Contents/Resources/"; else echo "no $TRAINED/bigram.bin: building without context ranking"; fi
if [ -f "$TRAINED/chars.bin" ]; then cp "$TRAINED/chars.bin" "$APP/Contents/Resources/"; fi
if [ -f "$TRAINED/sentence.bin" ]; then cp "$TRAINED/sentence.bin" "$APP/Contents/Resources/"; fi  # optional: tools/lm-trainer
if [ -f "$TRAINED/models-version.txt" ]; then cp "$TRAINED/models-version.txt" "$APP/Contents/Resources/"; fi
# Their licenses and sources go with them.
if [ -f "$TRAINED/data.txt" ] || [ -f "$TRAINED/bigram.bin" ]; then
  cp ../../tools/bigram-trainer/MODELS-NOTICE.txt ../../tools/bigram-trainer/LICENSE-LGPL-2.1.txt "$APP/Contents/Resources/"
fi
codesign --force --sign - "$APP"
echo "built $APP"

# The settings app (Sources/SettingsApp): the same settings window, opened without the input menu.
SETTINGS="$BUILD/Project Key 設定.app"
rm -rf "$SETTINGS"
mkdir -p "$SETTINGS/Contents/MacOS" "$SETTINGS/Contents/Resources"
# shellcheck disable=SC2086
c++ $CXXFLAGS -fobjc-arc -ISources Sources/SettingsApp/main.mm Sources/SettingsWindow.mm Sources/CandidatePanel.mm $SWIFT_LINK \
  -framework Cocoa -framework QuartzCore -framework Carbon -o "$SETTINGS/Contents/MacOS/ProjectKeySettings"
cp Resources/SettingsApp/Info.plist "$SETTINGS/Contents/"
for key in CFBundleShortVersionString CFBundleVersion; do  # the input method's, which it updates with
  plutil -replace $key -string "$(plutil -extract $key raw Resources/Info.plist)" "$SETTINGS/Contents/Info.plist"
done
cp Resources/AppIcon.icns ../linux/fcitx-projectkey.svg "$SETTINGS/Contents/Resources/"
codesign --force --sign - "$SETTINGS"
echo "built $SETTINGS"

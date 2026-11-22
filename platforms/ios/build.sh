#!/bin/sh
# Generates ProjectKey.xcodeproj (XcodeGen) and builds it for the iOS
# Simulator into build/derived. The McBopomofo engine and dictionary (MIT)
# are fetched at the pinned commit into build/, as on macOS. The trained
# dictionary and models from tools/bigram-trainer go in when they exist; the
# models are memory-mapped, so they fit a keyboard extension's memory.
set -eu
cd "$(dirname "$0")"
BUILD=build
MCB=$BUILD/mcbopomofo
TRAINED=../../tools/bigram-trainer/.cache  # data.txt: McBopomofo's dictionary with the words it lacks added

command -v xcodegen >/dev/null || brew install xcodegen
../../tools/fetch-mcbopomofo.sh "$MCB"
if [ -f "$TRAINED/data.txt" ]; then cp "$TRAINED/data.txt" $BUILD/data.txt; else cp "$MCB/Source/Data/data.txt" $BUILD/data.txt; fi
cp "$MCB/Source/Data/associated-phrases-v2.txt" $BUILD/
# XcodeGen lists what is in build/models when it generates the project.
rm -rf $BUILD/models
mkdir -p $BUILD/models
for model in bigram.bin chars.bin; do
  if [ -f "$TRAINED/$model" ]; then cp "$TRAINED/$model" $BUILD/models/; fi
done

xcodegen generate --quiet
# Release: an unoptimized engine takes ~8s to load the dictionary, and the
# system gives up on the keyboard and shows its own. Signed to run locally, so
# the simulator honors the app group that carries the settings.
xcodebuild -project ProjectKey.xcodeproj -scheme ProjectKey -sdk iphonesimulator -configuration Release \
  -derivedDataPath $BUILD/derived CODE_SIGN_STYLE=Manual CODE_SIGN_IDENTITY=- -quiet build
echo "built $BUILD/derived/Build/Products/Release-iphonesimulator/ProjectKey.app"

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
MCB_COMMIT=$(cat ../../MCBOPOMOFO_COMMIT)
TRAINED=../../tools/bigram-trainer/.cache  # data.txt: McBopomofo's dictionary with the words it lacks added

command -v xcodegen >/dev/null || brew install xcodegen
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
if [ -f "$TRAINED/data.txt" ]; then cp "$TRAINED/data.txt" $BUILD/data.txt; else cp "$MCB/Source/Data/data.txt" $BUILD/data.txt; fi
# XcodeGen lists what is in build/models when it generates the project.
rm -rf $BUILD/models
mkdir -p $BUILD/models
for model in bigram.bin chars.bin; do
  if [ -f "$TRAINED/$model" ]; then cp "$TRAINED/$model" $BUILD/models/; fi
done

xcodegen generate --quiet
# Release: an unoptimized engine takes ~8s to load the dictionary, and the
# system gives up on the keyboard and shows its own.
xcodebuild -project ProjectKey.xcodeproj -scheme ProjectKey -sdk iphonesimulator -configuration Release \
  -derivedDataPath $BUILD/derived CODE_SIGNING_ALLOWED=NO -quiet build
echo "built $BUILD/derived/Build/Products/Release-iphonesimulator/ProjectKey.app"

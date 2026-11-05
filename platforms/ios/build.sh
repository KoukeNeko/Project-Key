#!/bin/sh
# Generates ProjectKey.xcodeproj (XcodeGen) and builds it for the iOS
# Simulator into .build/derived. The McBopomofo engine and dictionary (MIT)
# are fetched at the pinned commit into .build/, as on macOS. On iOS the
# keyboard ships only the dictionary: the 190MB context model would not fit
# a keyboard extension's memory.
set -eu
cd "$(dirname "$0")"
BUILD=.build
MCB=$BUILD/mcbopomofo
MCB_COMMIT=$(cat ../../MCBOPOMOFO_COMMIT)
TRAINED=../../tools/bigram-trainer/.cache/data.txt  # McBopomofo's dictionary with the words it lacks added

command -v xcodegen >/dev/null || brew install xcodegen
if [ ! -d "$MCB/.git" ]; then
  git init -q "$MCB"
  git -C "$MCB" remote add origin https://github.com/openvanilla/McBopomofo
  git -C "$MCB" fetch -q --depth 1 origin "$MCB_COMMIT"
  git -C "$MCB" checkout -q FETCH_HEAD
fi
[ -f "$MCB/Source/Data/data.txt" ] || make -C "$MCB/Source/Data" data.txt
if [ -f "$TRAINED" ]; then cp "$TRAINED" $BUILD/data.txt; else cp "$MCB/Source/Data/data.txt" $BUILD/data.txt; fi

xcodegen generate --quiet
# Release: an unoptimized engine takes ~8s to load the dictionary, and the
# system gives up on the keyboard and shows its own.
xcodebuild -project ProjectKey.xcodeproj -scheme ProjectKey -sdk iphonesimulator -configuration Release \
  -derivedDataPath $BUILD/derived CODE_SIGNING_ALLOWED=NO -quiet build
echo "built $BUILD/derived/Build/Products/Release-iphonesimulator/ProjectKey.app"

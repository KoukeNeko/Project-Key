#!/bin/sh
# Downloads the published dictionary and context models (MODELS-NOTICE.txt) into .cache/,
# where the platform builds pick them up, and checks them against models.sha256.
# They are published once per training run as the GitHub release $TAG (README.md).
set -eu
cd "$(dirname "$0")"
TAG=models-2026-11-06
URL=https://github.com/KoukeNeko/Project-Key/releases/download/$TAG
mkdir -p .cache
for file in data.txt bigram.bin chars.bin; do
  curl -fsSL --retry 3 -o ".cache/$file" "$URL/$file"
done
cd .cache
shasum -a 256 -c ../models.sha256

#!/bin/sh
# Downloads the published dictionary and context models (MODELS-NOTICE.txt) into .cache/,
# where the platform builds pick them up, checks them against models.sha256, and records
# the tag's date in .cache/models-version.txt.
# They are published once per training run as the GitHub release $TAG. To ship newly trained ones:
# 1. Run train.sh.
# 2. Publish them as a pre-release under a new date tag, so the settings window's update check
#    (GitHub's latest release) never finds it:
#    gh release create models-YYYY-MM-DD --prerelease --latest=false --title "Models YYYY-MM-DD" \
#      .cache/data.txt .cache/bigram.bin .cache/chars.bin MODELS-NOTICE.txt LICENSE-LGPL-2.1.txt
#    From then on the settings windows offer it to anyone with older models: they take the newest
#    models-* release with data.txt, bigram.bin and chars.bin, check each file against the SHA-256
#    GitHub lists, and the engine switches to it (core/engine/Models.h). The models' file formats
#    must stay readable by the releases already installed; a format change needs a new tag prefix.
# 3. Set TAG below to the new tag, so new installs bundle it, update the checksums with
#    (cd .cache && shasum -a 256 data.txt bigram.bin chars.bin) > models.sha256
#    and commit both.
set -eu
cd "$(dirname "$0")"
TAG=models-2026-11-07
URL=https://github.com/KoukeNeko/Project-Key/releases/download/$TAG
mkdir -p .cache
for file in data.txt bigram.bin chars.bin; do
  curl -fsSL --retry 3 -o ".cache/$file" "$URL/$file"
done
cd .cache
# Git Bash on Windows has sha256sum but no shasum; macOS has shasum.
if command -v sha256sum >/dev/null; then sha256sum -c ../models.sha256; else shasum -a 256 -c ../models.sha256; fi
# The builds install it with the models; the settings window shows it.
echo "${TAG#models-}" > models-version.txt

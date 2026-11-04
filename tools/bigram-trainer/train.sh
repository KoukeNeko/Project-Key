#!/bin/sh
# Trains .cache/bigram.bin from one Chinese Wikipedia shard (CC BY-SA 4.0),
# converted to Taiwan Traditional with OpenCC s2twp. OpenCC writes 佈 臺 賬
# where everyday Taiwanese text and McBopomofo's dictionary use 布 台 帳, so
# those are mapped back. Local prototype data:
# the shipping training source is not decided yet.
# Usage: train.sh [max characters, default 200000000] [min pair count, default 3]
set -eu
cd "$(dirname "$0")"
LIMIT=${1:-200000000}
MIN_COUNT=${2:-3}
CACHE=.cache
SHARD_URL=https://huggingface.co/api/datasets/wikimedia/wikipedia/parquet/20231101.zh/train/0.parquet
DATA=../../platforms/macos/.build/mcbopomofo/Source/Data/data.txt  # built by platforms/macos/build.sh

[ -f "$DATA" ] || { echo "run platforms/macos/build.sh first" >&2; exit 1; }
command -v opencc >/dev/null || brew install opencc
mkdir -p "$CACHE/wiki"
[ -f "$CACHE/wiki/0.parquet" ] || curl -fsSL -o "$CACHE/wiki/0.parquet" "$SHARD_URL"
[ -x "$CACHE/venv/bin/python" ] || { python3 -m venv "$CACHE/venv"; "$CACHE/venv/bin/pip" install -q pyarrow; }

c++ -std=c++20 -O2 -I../../platforms/macos/Sources train.cpp ../../platforms/macos/Sources/Bigram.cpp -o "$CACHE/train"
"$CACHE/venv/bin/python" -I extract.py "$CACHE/wiki/0.parquet" "$LIMIT" \
  | opencc -c s2twp.json \
  | sed 's/佈/布/g; s/臺/台/g; s/賬/帳/g' \
  | "$CACHE/train" "$DATA" "$MIN_COUNT" "$CACHE/bigram.bin"
ls -l "$CACHE/bigram.bin"

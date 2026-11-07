#!/bin/sh
# Builds .cache/data.txt, McBopomofo's dictionary with the words it lacks added
# from libchewing-data (LGPL-2.1-or-later), from the training text
# (supplement.py, mine.cpp) and from the national road-name list, and trains .cache/bigram.bin on it from one
# Chinese Wikipedia shard (CC BY-SA 4.0),
# converted to Taiwan Traditional with OpenCC s2twp. OpenCC writes 佈 臺 賬
# where everyday Taiwanese text and McBopomofo's dictionary use 布 台 帳, so
# those are mapped back. Gazette texts from fetch_gazette.py (Legislative Yuan,
# Open Government Data License v1) add conversational Taiwanese Mandarin when
# .cache/gazette exists; they are already Taiwan Traditional. Local prototype data:
# the shipping training source is not decided yet.
# Usage: train.sh [max characters, default 200000000] [min pair count, default 3]
set -eu
cd "$(dirname "$0")"
LIMIT=${1:-200000000}
MIN_COUNT=${2:-3}
CHAR_MIN_COUNT=3  # character pairs and triples seen less often back off to shorter ones
CACHE=.cache
SHARD_URL=https://huggingface.co/api/datasets/wikimedia/wikipedia/parquet/20231101.zh/train/0.parquet
CHEWING_URL=https://raw.githubusercontent.com/chewing/libchewing-data/c44e81aef24b06f1509f19e1be54c99812d0c43f/dict/chewing/tsi.csv
# 全國路名資料, year 115 edition (內政部戶政司, Open Government Data License v1): https://data.gov.tw/dataset/35321
ROADS_URL=https://opdadm.moi.gov.tw/api/v1/no-auth/resource/api/dataset/E2EDC47D-2D3F-4EB1-878A-4DEB6160FD4C/resource/EB1B8C49-5C2A-42E0-8CC3-6B69B37DA66F/download
TEXT=$CACHE/text-$LIMIT.txt  # delete it after fetching gazette texts
DATA=../../platforms/macos/build/mcbopomofo/Source/Data/data.txt  # built by platforms/macos/build.sh

[ -f "$DATA" ] || { echo "run platforms/macos/build.sh first" >&2; exit 1; }
command -v opencc >/dev/null || brew install opencc
mkdir -p "$CACHE/wiki"
[ -f "$CACHE/wiki/0.parquet" ] || curl -fsSL -o "$CACHE/wiki/0.parquet" "$SHARD_URL"
[ -f "$CACHE/tsi.csv" ] || curl -fsSL -o "$CACHE/tsi.csv" "$CHEWING_URL"
[ -f "$CACHE/roads.csv" ] || curl -fsSL -o "$CACHE/roads.csv" "$ROADS_URL"
[ -x "$CACHE/venv/bin/python" ] || { python3 -m venv "$CACHE/venv"; "$CACHE/venv/bin/pip" install -q pyarrow; }

MCB_ENGINE=../../platforms/macos/build/mcbopomofo/Source/Engine  # the models map their files with its MemoryMappedFile
c++ -std=c++20 -O2 -I../../core/engine -I$MCB_ENGINE train.cpp ../../core/engine/Bigram.cpp $MCB_ENGINE/MemoryMappedFile.cpp -o "$CACHE/train"
c++ -std=c++20 -O2 -I../../core/engine -I$MCB_ENGINE chars.cpp ../../core/engine/CharModel.cpp $MCB_ENGINE/MemoryMappedFile.cpp -o "$CACHE/chars"
c++ -std=c++20 -O2 -I../../core/engine mine.cpp -o "$CACHE/mine"
if [ ! -f "$TEXT" ]; then
  {
    "$CACHE/venv/bin/python" -I extract.py "$CACHE/wiki/0.parquet" "$LIMIT" | opencc -c s2twp.json
    [ -d "$CACHE/gazette" ] && find "$CACHE/gazette" -name '*.txt' -exec cat {} +
  } | sed 's/佈/布/g; s/臺/台/g; s/賬/帳/g' > "$TEXT.part"
  mv "$TEXT.part" "$TEXT"
fi

python3 -I supplement.py chewing "$DATA" "$CACHE/tsi.csv" > "$CACHE/chewing.txt"
python3 -I supplement.py merge "$DATA" "$CACHE/chewing.txt" > "$CACHE/data-chewing.txt"
"$CACHE/mine" "$CACHE/data-chewing.txt" 20 < "$TEXT" > "$CACHE/candidates.tsv"
python3 -I supplement.py mined "$CACHE/data-chewing.txt" "$CACHE/candidates.tsv" > "$CACHE/mined.txt"
python3 -I supplement.py merge "$CACHE/data-chewing.txt" "$CACHE/mined.txt" > "$CACHE/data-mined.txt"
python3 -I supplement.py places "$CACHE/data-mined.txt" "$CACHE/roads.csv" > "$CACHE/places.txt"
python3 -I supplement.py merge "$CACHE/data-mined.txt" "$CACHE/places.txt" > "$CACHE/data.txt"
wc -l "$CACHE/chewing.txt" "$CACHE/mined.txt" "$CACHE/places.txt"
"$CACHE/train" "$CACHE/data.txt" "$MIN_COUNT" "$CACHE/bigram.bin" < "$TEXT"
"$CACHE/chars" "$CHAR_MIN_COUNT" "$CACHE/chars.bin" < "$TEXT"
ls -l "$CACHE/data.txt" "$CACHE/bigram.bin" "$CACHE/chars.bin"

#!/bin/sh
# Builds .cache/data.txt, McBopomofo's dictionary with the words it lacks added
# from libchewing-data (LGPL-2.1-or-later), from the training text
# (supplement.py, mine.cpp) and from the national road-name list, and trains .cache/bigram.bin on it from one
# Chinese Wikipedia (CC BY-SA 4.0),
# converted to Taiwan Traditional with OpenCC s2twp. OpenCC writes 佈 臺 賬
# where everyday Taiwanese text and McBopomofo's dictionary use 布 台 帳, so
# those are mapped back. Gazette texts from fetch_gazette.py (Legislative Yuan,
# Open Government Data License v1) add conversational Taiwanese Mandarin when
# .cache/gazette exists; they are already Taiwan Traditional. Local prototype data:
# the shipping training source is not decided yet.
# Usage: train.sh [max characters, default 2000000000] [min pair count, default 3]
set -eu
cd "$(dirname "$0")"
LIMIT=${1:-2000000000}
MIN_COUNT=${2:-3}
CHAR_MIN_COUNT=3  # character pairs and triples seen less often back off to shorter ones
CACHE=.cache
SHARD_URL=https://huggingface.co/api/datasets/wikimedia/wikipedia/parquet/20231101.zh/train  # 0.parquet to 5.parquet
CHEWING_URL=https://raw.githubusercontent.com/chewing/libchewing-data/c44e81aef24b06f1509f19e1be54c99812d0c43f/dict/chewing/tsi.csv
# 全國路名資料, year 115 edition (內政部戶政司, Open Government Data License v1): https://data.gov.tw/dataset/35321
ROADS_URL=https://opdadm.moi.gov.tw/api/v1/no-auth/resource/api/dataset/E2EDC47D-2D3F-4EB1-878A-4DEB6160FD4C/resource/EB1B8C49-5C2A-42E0-8CC3-6B69B37DA66F/download
TEXT=$CACHE/text-$LIMIT.txt  # delete it after fetching gazette texts
DATA=../../platforms/macos/build/mcbopomofo/Source/Data/data.txt  # built by platforms/macos/build.sh

[ -f "$DATA" ] || { echo "run platforms/macos/build.sh first" >&2; exit 1; }
command -v opencc >/dev/null || brew install opencc
mkdir -p "$CACHE/wiki"
for shard in 0 1 2 3 4 5; do
  [ -f "$CACHE/wiki/$shard.parquet" ] || curl -fsSL -o "$CACHE/wiki/$shard.parquet" "$SHARD_URL/$shard.parquet"
done
[ -f "$CACHE/tsi.csv" ] || curl -fsSL -o "$CACHE/tsi.csv" "$CHEWING_URL"
[ -f "$CACHE/roads.csv" ] || curl -fsSL -o "$CACHE/roads.csv" "$ROADS_URL"
# The open government name lists in MODELS-NOTICE.txt, by the file name supplement.py reads.
MOI=https://opdadm.moi.gov.tw/api/v1/no-auth/resource/api/dataset
MOE=https://stats.moe.gov.tw/files
mkdir -p "$CACHE/names"
while read -r name url; do
  [ -s "$CACHE/names/$name.csv" ] || curl -fsSL --retry 3 -o "$CACHE/names/$name.csv" "$url"
done <<LISTS
place-settlement $MOI/4A5AB0C9-0395-4B04-AE50-02624075516F/resource/9E43B451-3177-4CEC-8765-C601660A88E4/download
place-landmark $MOI/ADE629B4-0A35-418B-A623-E54281140BCB/resource/D31502F8-FC87-4B4D-AFDF-4AF22A326510/download
place-admin $MOI/C8DB1578-7554-4DBA-82AD-AD0105992C68/resource/167ADA8B-A97E-4261-ACD0-329968B2F1FE/download
place-natural $MOI/50ACF0B4-D215-463F-8813-5164027D59D3/resource/A67A55E9-8E9D-42E1-B73E-E302A32BA8C0/download
place-street $MOI/076663CB-6E77-4AA4-AD4C-FCFA357CE257/resource/C272CAC0-F868-4AD7-933F-7044649BD75D/download
place-romanized $MOI/4FECC3B1-4E35-49A5-BB46-C49A19B5B1D3/resource/82C14CCA-2531-402D-B7AC-66DF8355E154/download
school-university $MOE/opendata/u1_new.csv
school-junior $MOE/opendata/j1_new.csv
school-elementary $MOE/school/115/e1_new.csv
departments $MOE/opendata/sdata.csv
agency-addressbook https://www.good.nat.gov.tw/regcenter/pub/addressbook/file/all_active_utf8.csv
agency-oid https://oid.nat.gov.tw/OIDWeb/GDS.csv
company-listed https://mopsfin.twse.com.tw/opendata/t187ap03_L.csv
company-otc https://mopsfin.twse.com.tw/opendata/t187ap03_O.csv
company-emerging https://mopsfin.twse.com.tw/opendata/t187ap03_R.csv
shopping-areas https://data.gcis.nat.gov.tw/od/file?oid=188FEC5E-491A-4B06-A903-A5BD72735552
LISTS
[ -x "$CACHE/venv/bin/python" ] || { python3 -m venv "$CACHE/venv"; "$CACHE/venv/bin/pip" install -q pyarrow; }

MCB_ENGINE=../../platforms/macos/build/mcbopomofo/Source/Engine  # the models map their files with its MemoryMappedFile
c++ -std=c++20 -O2 -I../../core/engine -I$MCB_ENGINE train.cpp ../../core/engine/Bigram.cpp $MCB_ENGINE/MemoryMappedFile.cpp -o "$CACHE/train"
c++ -std=c++20 -O2 -I../../core/engine -I$MCB_ENGINE chars.cpp ../../core/engine/CharModel.cpp $MCB_ENGINE/MemoryMappedFile.cpp -o "$CACHE/chars"
c++ -std=c++20 -O2 -I../../core/engine mine.cpp -o "$CACHE/mine"
c++ -std=c++20 -O2 -I../../core/engine -I$MCB_ENGINE lexicon.cpp ../../core/engine/LexiconFile.cpp ../../core/engine/CharModel.cpp $MCB_ENGINE/MemoryMappedFile.cpp -o "$CACHE/lexicon"
if [ ! -f "$TEXT" ]; then
  {
    "$CACHE/venv/bin/python" -I extract.py "$LIMIT" "$CACHE"/wiki/*.parquet | opencc -c s2twp.json
    [ -d "$CACHE/gazette" ] && find "$CACHE/gazette" -name '*.txt' -exec cat {} +
  } | sed 's/佈/布/g; s/臺/台/g; s/賬/帳/g' > "$TEXT.part"
  mv "$TEXT.part" "$TEXT"
fi

python3 -I supplement.py chewing "$DATA" "$CACHE/tsi.csv" > "$CACHE/chewing.txt"
python3 -I supplement.py merge "$DATA" "$CACHE/chewing.txt" > "$CACHE/data-chewing.txt"
"$CACHE/mine" "$CACHE/data-chewing.txt" 20 < "$TEXT" > "$CACHE/candidates.tsv"
python3 -I supplement.py mined "$CACHE/data-chewing.txt" "$CACHE/candidates.tsv" > "$CACHE/mined.txt"
python3 -I supplement.py merge "$CACHE/data-chewing.txt" "$CACHE/mined.txt" > "$CACHE/data-mined.txt"
python3 -I supplement.py places "$CACHE/data-mined.txt" "$CACHE/roads.csv" "$CACHE/names" > "$CACHE/places.txt"
python3 -I supplement.py merge "$CACHE/data-mined.txt" "$CACHE/places.txt" > "$CACHE/data.txt"
wc -l "$CACHE/chewing.txt" "$CACHE/mined.txt" "$CACHE/places.txt"
"$CACHE/lexicon" "$CACHE/data.txt" "$CACHE/lexicon.bin"
"$CACHE/train" "$CACHE/data.txt" "$MIN_COUNT" "$CACHE/bigram.bin" < "$TEXT"
"$CACHE/chars" "$CHAR_MIN_COUNT" "$CACHE/chars.bin" < "$TEXT"
date +%F > "$CACHE/models-version.txt"  # what the settings window shows, as fetch-models.sh records a release
ls -l "$CACHE/data.txt" "$CACHE/bigram.bin" "$CACHE/chars.bin"

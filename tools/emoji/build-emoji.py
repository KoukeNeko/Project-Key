#!/usr/bin/env python3
"""Builds core/data/emoji.txt from Unicode CLDR's Traditional Chinese emoji annotations.

  tools/emoji/build-emoji.py --data data.txt --annotations annotations/zh_Hant.xml \
      --emoji-test emoji-test.txt --output core/data/emoji.txt

The inputs are CLDR's common/annotations/zh_Hant.xml, Unicode's emoji-test.txt (the order and the groups)
and the dictionary the engine uses (data.txt, to read a keyword in Zhuyin). The output has two kinds of
lines, fields separated by a tab:
  G  group name  emoji separated by spaces       the groups of the table, in CLDR order
  K  reading     emoji separated by spaces       the emoji a word typed in that reading offers
The data is under the Unicode License v3 (core/data/EMOJI-NOTICE.txt).
"""
import argparse
import re
import sys
import xml.etree.ElementTree as ET

GROUPS = {  # emoji-test.txt's group names, as the table names them
    "Smileys & Emotion": "表情",
    "People & Body": "人物",
    "Animals & Nature": "動物與自然",
    "Food & Drink": "飲食",
    "Travel & Places": "旅遊與地點",
    "Activities": "活動",
    "Objects": "物品",
    "Symbols": "符號",
    "Flags": "旗幟",
}
SKIN_TONES = re.compile("[\U0001F3FB-\U0001F3FF]")
# A keyword that many emoji share (臉, 手) says too little to offer all of them.
MAX_SHARED = 20
# The emoji one reading offers at most.
MAX_PER_READING = 12


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", required=True)
    parser.add_argument("--annotations", required=True)
    parser.add_argument("--emoji-test", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    groups, order = {}, []
    group = None
    for line in open(args.emoji_test, encoding="utf-8"):
        if line.startswith("# group:"):
            group = line.split(":", 1)[1].strip()
        elif line and not line.startswith("#"):
            fields = line.split(";", 1)
            if len(fields) == 2 and fields[1].split("#")[0].strip() == "fully-qualified" and group in GROUPS:
                emoji = "".join(chr(int(code, 16)) for code in fields[0].split())
                if not SKIN_TONES.search(emoji):
                    groups.setdefault(group, []).append(emoji)
                    order.append(emoji)
    rank = {emoji: i for i, emoji in enumerate(order)}
    # The annotations spell an emoji with its variation selector or without; the table keeps the first.
    by_plain = {e.replace("️", ""): e for e in order}

    keywords = {}  # keyword -> emoji
    for note in ET.parse(args.annotations).getroot().iter("annotation"):
        if note.get("type") == "tts" or not note.text:
            continue
        emoji = by_plain.get(note.get("cp").replace("️", ""))
        if emoji is None:
            continue
        for word in note.text.split("|"):
            keywords.setdefault(word.strip(), []).append(emoji)

    readings = {}  # a word's value -> its best reading in data.txt
    for line in open(args.data, encoding="utf-8"):
        if line.startswith("_") or line.startswith("#"):
            continue
        parts = line.split(" ")
        if len(parts) == 3:
            reading, value, score = parts[0], parts[1], float(parts[2])
            if value in keywords and (value not in readings or score > readings[value][1]):
                readings[value] = (reading, score)

    by_reading = {}
    skipped = 0
    for word, emoji in keywords.items():
        if word not in readings or len(emoji) > MAX_SHARED:
            skipped += 1
            continue
        by_reading.setdefault(readings[word][0], set()).update(emoji)

    with open(args.output, "w", encoding="utf-8") as out:
        for name, emoji in groups.items():
            out.write("G\t%s\t%s\n" % (GROUPS[name], " ".join(emoji)))
        for reading in sorted(by_reading):
            offered = sorted(by_reading[reading], key=rank.__getitem__)[:MAX_PER_READING]
            out.write("K\t%s\t%s\n" % (reading, " ".join(offered)))
    print("%d emoji in %d groups, %d readings, %d of %d keywords skipped"
          % (len(order), len(groups), len(by_reading), skipped, len(keywords)), file=sys.stderr)


main()

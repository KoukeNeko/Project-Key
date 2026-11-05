"""Turns training text into character ids for train.py.

Usage: prepare.py <out dir> < text

Text is cut into sentences of Han characters with their punctuation removed, the
form the evaluation corpus and an input method's composing buffer take. A
sentence ends at 。！？； or a line end, and at any character that is neither Han
nor punctuation (digits, Latin), since dropping those would join words that
never stand together. Each sentence is written as [CLS] characters [SEP].
Writes vocab.txt (BertTokenizerFast's format), train.bin and valid.bin
(uint16 ids, every 100th sentence held out).
"""
import collections
import re
import sys

import numpy as np

SPECIALS = ["[PAD]", "[UNK]", "[CLS]", "[SEP]", "[MASK]"]
UNK, CLS, SEP = 1, 2, 3
MIN_COUNT = 10  # rarer characters become [UNK]
HAN = r"㐀-鿿豈-﫿\U00020000-\U0002ffff"
SOFT = "，、：「」『』（）〈〉《》“”‘’—…·－ 　"
BREAK = re.compile(f"[^{HAN}{re.escape(SOFT)}]+")
DROP = re.compile(f"[{re.escape(SOFT)}]")


class Ids(dict):
    def __missing__(self, char):
        return chr(UNK)


def sentences(lines):
    for line in lines:
        for piece in BREAK.split(line):
            piece = DROP.sub("", piece)
            if len(piece) >= 2:
                yield piece


def main():
    out = sys.argv[1]
    texts = list(sentences(sys.stdin))
    counts = collections.Counter()
    for text in texts:
        counts.update(text)
    chars = sorted(c for c, n in counts.items() if n >= MIN_COUNT)
    with open(f"{out}/vocab.txt", "w", encoding="utf-8") as f:
        f.write("".join(token + "\n" for token in SPECIALS + chars))
    # Ids stay below the surrogate range, so each one is a single UTF-16 unit.
    assert len(SPECIALS) + len(chars) < 0xD800
    ids = Ids({ord(c): chr(len(SPECIALS) + i) for i, c in enumerate(chars)})
    for name, part in (("train", [t for i, t in enumerate(texts) if i % 100]), ("valid", texts[::100])):
        stream = "".join(chr(CLS) + t.translate(ids) + chr(SEP) for t in part)
        np.frombuffer(stream.encode("utf-16-le"), dtype=np.uint16).tofile(f"{out}/{name}.bin")
        print(f"{name}: {len(part)} sentences, {len(stream)} ids")
    unknown = sum(n for c, n in counts.items() if n < MIN_COUNT)
    print(f"vocabulary {len(SPECIALS) + len(chars)}, {unknown} of {sum(counts.values())} characters unknown")


if __name__ == "__main__":
    main()

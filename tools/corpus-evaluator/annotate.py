#!/usr/bin/env python3
"""Annotate tests/corpus/sentences.tsv with zhuyin into tests/corpus/corpus.tsv.

Readings come from McBopomofo's MIT-licensed data (pinned commit). Sentences are
segmented by unigram phrase frequency; a segment whose reading cannot be decided
is marked `review` so a person checks it. Rows already in corpus.tsv are kept
as-is, so manual corrections survive re-runs.

The corpus records what people type, not dictionary pronunciation: citation
tones without 一/不 tone sandhi, and TYPED_FORM for characters whose dictionary
reading differs from the usual keyed one.
"""
import csv
import math
import sys
import urllib.request
from pathlib import Path

MCB_COMMIT = "257da536ddaf590aae0e74e6adb9b415988344f6"
HETEROPHONY = ["heterophony1.list", "heterophony2.list", "heterophony3.list"]
MCB_FILES = ["BPMFMappings.txt", "BPMFBase.txt", "phrase.occ", *HETEROPHONY]
MAX_WORD = 8
TYPED_FORM = {
    "不": "ㄅㄨˋ", "一": "ㄧ", "個": "ㄍㄜˋ", "們": "ㄇㄣ˙", "這": "ㄓㄜˋ",
    "和": "ㄏㄜˊ", "署": "ㄕㄨˋ", "吧": "ㄅㄚ˙", "放": "ㄈㄤˋ",
}

ROOT = Path(__file__).resolve().parents[2]
CACHE = Path(__file__).resolve().parent / ".cache" / MCB_COMMIT
SENTENCES = ROOT / "tests/corpus/sentences.tsv"
CORPUS = ROOT / "tests/corpus/corpus.tsv"
FIELDS = ["domain", "sentence", "zhuyin", "status", "note"]


def fetch_data():
    CACHE.mkdir(parents=True, exist_ok=True)
    for name in MCB_FILES:
        path = CACHE / name
        if not path.exists():
            url = f"https://raw.githubusercontent.com/openvanilla/McBopomofo/{MCB_COMMIT}/Source/Data/{name}"
            urllib.request.urlretrieve(url, path)
    return CACHE


def load_lexicon(data):
    readings = {}  # phrase or char -> ordered unique readings
    for line in (data / "BPMFMappings.txt").read_text().splitlines():
        phrase, *syllables = line.split()
        readings.setdefault(phrase, [])
        if " ".join(syllables) not in readings[phrase]:
            readings[phrase].append(" ".join(syllables))
    preferred = {}  # char -> readings in McBopomofo's heterophony order (most common first)
    for name in HETEROPHONY:
        for line in (data / name).read_text().splitlines():
            parts = line.split()
            if len(parts) == 2 and not line.startswith("#"):
                preferred.setdefault(parts[0], []).append(parts[1])
    for line in (data / "BPMFBase.txt").read_text().splitlines():
        char, reading = line.split()[:2]
        if len(char) == 1:
            readings.setdefault(char, [])
            if reading not in readings[char]:
                readings[char].append(reading)
    counts = {}
    for line in (data / "phrase.occ").read_text().splitlines():
        parts = line.split()
        if len(parts) == 2:
            counts[parts[0]] = int(parts[1])
    return readings, preferred, counts


def segment(sentence, readings, counts):
    total = sum(counts.values())
    best = [0.0] + [-math.inf] * len(sentence)
    back = [0] * (len(sentence) + 1)
    for end in range(1, len(sentence) + 1):
        for start in range(max(0, end - MAX_WORD), end):
            word = sentence[start:end]
            if word not in readings or best[start] == -math.inf:
                continue
            score = best[start] + math.log((counts.get(word, 0) + 0.5) / total)
            if score > best[end]:
                best[end], back[end] = score, start
    if best[-1] == -math.inf:
        raise ValueError(f"no reading for some character in {sentence!r}")
    words, end = [], len(sentence)
    while end:
        words.append(sentence[back[end]:end])
        end = back[end]
    return words[::-1]


def char_rank(char, syllable, readings, preferred):
    order = preferred.get(char, []) + [r for r in readings.get(char, []) if r not in preferred.get(char, [])]
    return order.index(syllable) if syllable in order else len(order)


def annotate(sentence, readings, preferred, counts):
    """Pick each segment's reading by how common its per-character readings are.

    A segment is flagged only when the choice has no signal: a lone character
    without a heterophony entry, or a phrase whose best readings tie.
    """
    syllables, ambiguous = [], []
    for word in segment(sentence, readings, counts):
        options = readings[word]
        if len(word) == 1:
            ranked = sorted(options, key=lambda r: char_rank(word, r, readings, preferred))
            unsure = len(options) > 1 and word not in preferred
        else:
            scores = {r: sum(char_rank(c, s, readings, preferred) for c, s in zip(word, r.split())) for r in options}
            ranked = sorted(options, key=scores.get)
            unsure = len(ranked) > 1 and scores[ranked[0]] == scores[ranked[1]]
        typed = [TYPED_FORM.get(c, s) for c, s in zip(word, ranked[0].split())]
        syllables.extend(typed)
        unsure = unsure and not all(c in TYPED_FORM for c in word)
        if unsure:
            ambiguous.append(f"{word}:{'/'.join(ranked)}")
    return " ".join(syllables), ambiguous


def main():
    readings, preferred, counts = load_lexicon(fetch_data())
    existing = {}
    if CORPUS.exists():
        with CORPUS.open() as f:
            existing = {row["sentence"]: row for row in csv.DictReader(f, delimiter="\t")}
    rows = []
    with SENTENCES.open() as f:
        for domain, sentence in csv.reader(f, delimiter="\t"):
            if sentence in existing:
                rows.append({**existing[sentence], "domain": domain})
                continue
            zhuyin, ambiguous = annotate(sentence, readings, preferred, counts)
            rows.append({
                "domain": domain,
                "sentence": sentence,
                "zhuyin": zhuyin,
                "status": "review" if ambiguous else "auto",
                "note": " ".join(ambiguous),
            })
    with CORPUS.open("w", newline="") as f:
        writer = csv.DictWriter(f, FIELDS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    pending = sum(row["status"] == "review" for row in rows)
    print(f"{len(rows)} sentences, {pending} need review -> {CORPUS.relative_to(ROOT)}", file=sys.stderr)


if __name__ == "__main__":
    main()

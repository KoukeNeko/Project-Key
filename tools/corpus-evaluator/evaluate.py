#!/usr/bin/env python3
"""Type every corpus sentence into each engine adapter and score the results.

Usage: evaluate.py [--engine NAME ...] [--out report.md] [--slip row|column] [--corpus corpus.tsv]

Each sentence is typed as one continuous Daqian key sequence with no candidate
selection, so the score is what the user gets without correcting anything.
"""
import argparse
import csv
import difflib
import random
import statistics
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CACHE = HERE / ".cache"
TRAINED = ROOT / "tools/bigram-trainer/.cache"  # from tools/bigram-trainer/train.sh
BIGRAM = TRAINED / "bigram.bin"
CORPUS = ROOT / "tests/corpus/corpus.tsv"

# Daqian layout, as in RIME's bopomofo.schema.yaml; tone 1 is the space bar.
ZHUYIN = "ㄅㄆㄇㄈㄉㄊㄋㄌㄍㄎㄏㄐㄑㄒㄓㄔㄕㄖㄗㄘㄙㄧㄨㄩㄚㄛㄜㄝㄞㄟㄠㄡㄢㄣㄤㄥㄦˊˇˋ˙"
KEYS = "1qaz2wsxedcrfv5tgbyhnujm8ik,9ol.0p;/-6347"
TONES = "ˊˇˋ˙"
KEY_OF = dict(zip(ZHUYIN, KEYS))


def engines():
    return {
        "rime": [CACHE / "bin/rime", CACHE / "rime", "bopomofo_tw"],
        "chewing": [CACHE / "bin/chewing", "/opt/homebrew/opt/libchewing/share/libchewing", CACHE / "chewing-user.dat"],
        "mcbopomofo": [CACHE / "bin/mcbopomofo", CACHE / "mcbopomofo/Source/Data/data.txt"],
        # The dictionary the model was trained with, McBopomofo's plus the words it lacks.
        "projectkey": [CACHE / "bin/projectkey", TRAINED / "data.txt" if (TRAINED / "data.txt").exists() else CACHE / "mcbopomofo/Source/Data/data.txt", BIGRAM],
    }


def to_keys(zhuyin):
    keys = []
    for syllable in zhuyin.split():
        keys.extend(KEY_OF[symbol] for symbol in syllable)
        if syllable[-1] not in TONES:
            keys.append(" ")
    return "".join(keys)


# Physical rows; each row sits half a key right of the one above, so key c
# touches c and c+1 above and c-1 and c below.
ROWS = ["1234567890-", "qwertyuiop", "asdfghjkl;", "zxcvbnm,./"]


def neighbors(key, direction):
    for r, row in enumerate(ROWS):
        c = row.find(key)
        if c < 0:
            continue
        spots = [(r, c - 1), (r, c + 1)] if direction == "row" else [(r - 1, c), (r - 1, c + 1), (r + 1, c - 1), (r + 1, c)]
        return [ROWS[i][j] for i, j in spots if 0 <= i < len(ROWS) and 0 <= j < len(ROWS[i])]
    return []


def slip(keys, direction, rng):
    """One Zhuyin key replaced by a neighbor in `direction` that is also a Zhuyin key."""
    spots = [(i, n) for i, k in enumerate(keys) if k in KEYS and k not in "3467"
             for n in neighbors(k, direction) if n in KEYS and n not in "3467"]
    i, n = rng.choice(spots)
    return keys[:i] + n + keys[i + 1:]


def run_engine(command, rows):
    if command[0].name == "chewing":
        command[2].unlink(missing_ok=True)  # start every run from an empty user dictionary
    keys = "".join(row.get("keys", to_keys(row["zhuyin"])) + "\n" for row in rows)
    result = subprocess.run([str(part) for part in command], input=keys, capture_output=True, text=True, check=True)
    outputs = []
    for line in result.stdout.splitlines():
        latencies, _, text = line.partition("\t")
        outputs.append((text, [float(ms) for ms in latencies.split(",")]))
    if len(outputs) != len(rows):
        raise RuntimeError(f"{command[0].name}: {len(outputs)} outputs for {len(rows)} sentences\n{result.stderr}")
    return outputs


def error_spans(expected, actual):
    """Contiguous wrong stretches; each one costs the user roughly one correction."""
    return sum(tag != "equal" for tag, *_ in difflib.SequenceMatcher(None, expected, actual, autojunk=False).get_opcodes())


def char_errors(expected, actual):
    matcher = difflib.SequenceMatcher(None, expected, actual, autojunk=False)
    return sum(max(i2 - i1, j2 - j1) for tag, i1, i2, j1, j2 in matcher.get_opcodes() if tag != "equal")


def percentile(values, p):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * p))]


def summarize(rows, outputs):
    chars = sum(len(row["sentence"]) for row in rows)
    return {
        "sentences": len(rows),
        "exact": sum(row["sentence"] == text for row, (text, _) in zip(rows, outputs)),
        "char_acc": 1 - sum(char_errors(row["sentence"], text) for row, (text, _) in zip(rows, outputs)) / chars,
        "corrections": sum(error_spans(row["sentence"], text) for row, (text, _) in zip(rows, outputs)),
    }


def report(rows, results, slip=None):
    typed = {"row": "每句有一個注音鍵打成同一列的相鄰鍵", "column": "每句有一個注音鍵打成上一列或下一列的相鄰鍵"}.get(slip, "連續輸入整句注音")
    lines = ["# 選字評測結果", "", f"語料：{len(rows)} 句，{sum(len(r['sentence']) for r in rows)} 字。{typed}、不手動選字。", ""]
    lines += ["| 引擎 | 整句正確 | 字正確率 | 需修正處 | 每鍵 P50 | 每鍵 P95 | 每鍵最大 |", "|---|---|---|---|---|---|---|"]
    for name, outputs in results.items():
        s = summarize(rows, outputs)
        latencies = [ms for _, keys in outputs for ms in keys[1:]]  # first key includes lazy loading
        lines.append(
            f"| {name} | {s['exact']}/{s['sentences']} ({s['exact'] / s['sentences']:.1%}) | {s['char_acc']:.2%} | {s['corrections']} "
            f"| {statistics.median(latencies):.2f}ms | {percentile(latencies, 0.95):.2f}ms | {max(latencies):.2f}ms |"
        )
    domains = sorted({row["domain"] for row in rows}, key=[row["domain"] for row in rows].index)
    lines += ["", "## 各領域整句正確率", "", "| 領域 | " + " | ".join(results) + " |", "|---|" + "---|" * len(results)]
    for domain in domains:
        picked = [i for i, row in enumerate(rows) if row["domain"] == domain]
        cells = []
        for outputs in results.values():
            s = summarize([rows[i] for i in picked], [outputs[i] for i in picked])
            cells.append(f"{s['exact']}/{s['sentences']}")
        lines.append(f"| {domain} | " + " | ".join(cells) + " |")
    lines += ["", "## 錯誤句", ""]
    for i, row in enumerate(rows):
        wrong = {name: outputs[i][0] for name, outputs in results.items() if outputs[i][0] != row["sentence"]}
        if wrong:
            lines.append(f"- {row['sentence']}（{row['domain']}）")
            lines += [f"  - {name}：{text}" for name, text in wrong.items()]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", action="append", choices=sorted(engines()))
    parser.add_argument("--out", type=Path)
    parser.add_argument("--slip", choices=["row", "column"], help="mistype one key per sentence as a neighbor in the same row, or the row above or below")
    parser.add_argument("--corpus", type=Path, default=CORPUS)
    args = parser.parse_args()
    with args.corpus.open() as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    if args.slip:
        rng = random.Random(0)
        for row in rows:
            row["keys"] = slip(to_keys(row["zhuyin"]), args.slip, rng)
    unchecked = [row["sentence"] for row in rows if row["status"] == "review"]
    if unchecked:
        print(f"warning: {len(unchecked)} sentences still need zhuyin review", file=sys.stderr)
    results = {name: run_engine(engines()[name], rows) for name in args.engine or engines()}
    text = report(rows, results, args.slip)
    if args.out:
        args.out.write_text(text)
    sys.stdout.write(text)


if __name__ == "__main__":
    main()

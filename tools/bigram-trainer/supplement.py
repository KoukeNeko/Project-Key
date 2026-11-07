"""Words McBopomofo's data.txt lacks, as data.txt lines, and merging them in.

Usage:
  supplement.py chewing <data.txt> <tsi.csv>      libchewing words data.txt lacks
  supplement.py mined <data.txt> <candidates.tsv> words mine.cpp found
  supplement.py places <data.txt> <roads.csv>     road and district names data.txt lacks
  supplement.py merge <data.txt> <lines>...        data.txt with the lines added

Scores are put on data.txt's scale by fitting its scores of the words both
sources have against their counts, separately for 2, 3 and 4 or more characters.
Place names have no counts, so they take the lower quartile of data.txt's scores
for the place names it already has.
"""
import csv
import math
import re
import statistics
import sys

HEADER = "# format org.openvanilla.mcbopomofo.sorted\n"
MIN_COUNT = 50            # mined words seen fewer times are too uncertain
MIN_ASSOCIATION = 2.0     # log10 PMI: the parts occur together 100 times more than by chance
CONTAINED = 0.8           # a candidate seen mostly inside a longer one is a fragment of it
MAX_READING_VARIANTS = 4
FLOOR = -6.8              # data.txt's score for a character's rare readings (葉 ㄕㄜˋ)
ROAD_SUFFIX = re.compile(r"(大道|路|街|巷|道)$")


def is_han(text):
    return all(0x3400 <= ord(c) <= 0x9FFF or 0xF900 <= ord(c) <= 0xFAFF or 0x20000 <= ord(c) <= 0x2FFFF for c in text)


def read_data(path):
    """{word: {reading: score}} for the words of data.txt."""
    words = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line[0] in "#_":
                continue
            reading, word, score = line.split()
            words.setdefault(word, {})[reading] = float(score)
    return words


def bucket(word):
    return min(len(word), 4)


def calibrate(points):
    """{bucket: (intercept, slope)} fitting score on log10 count."""
    fits = {}
    for b in (2, 3, 4):
        xs = [x for k, x, _ in points if k == b]
        ys = [y for k, _, y in points if k == b]
        slope, intercept = statistics.linear_regression(xs, ys)
        fits[b] = (intercept, slope)
    return fits


def line(reading, word, score):
    return f"{reading} {word} {score:.6f}"


def chewing(data_path, tsi_path):
    known = read_data(data_path)
    rows = []
    with open(tsi_path, encoding="utf-8") as f:
        for row in csv.reader(f):
            if len(row) < 3 or row[0].startswith("#"):
                continue
            word, count, reading = row[0], int(row[1]), "-".join(row[2].split())
            if len(word) >= 2 and is_han(word) and reading.count("-") + 1 == len(word):
                rows.append((word, math.log10(count + 1), reading))
    fits = calibrate([(bucket(w), x, known[w][r]) for w, x, r in rows if r in known.get(w, {})])
    for word, x, reading in rows:
        if word not in known:  # a word data.txt has under another reading keeps data.txt's readings
            intercept, slope = fits[bucket(word)]
            print(line(reading, word, intercept + slope * x))


def mined(data_path, candidates_path):
    known = read_data(data_path)
    points, candidates = [], {}
    with open(candidates_path, encoding="utf-8") as f:
        for row in f:
            fields = row.rstrip("\n").split("\t")
            if fields[0] == "known":
                _, word, reading, count, score = fields
                if int(count) > 0:
                    points.append((bucket(word), math.log10(int(count)), float(score)))
            elif int(fields[2]) >= MIN_COUNT and float(fields[3]) >= MIN_ASSOCIATION:
                _, word, count, _, parts = fields
                candidates[word] = (int(count), parts.split("|"))
    fits = calibrate(points)
    inside = {}  # the most a word is seen inside a longer candidate
    for other, (count, _) in candidates.items():
        for i in range(len(other)):
            for j in range(i + 2, len(other) + (i > 0)):
                inside[other[i:j]] = max(inside.get(other[i:j], 0), count)
    for word, (count, parts) in sorted(candidates.items()):
        if inside.get(word, 0) >= CONTAINED * count:
            continue
        # Every reading of a one-character part: data.txt's scores for single
        # characters do not say which reading is common (長 ㄓㄤˇ is at its floor).
        readings = [""]
        for part in parts:
            options = list(known[part]) if len(part) == 1 else [max(known[part], key=known[part].get)]
            readings = [f"{r}-{o}" if r else o for r in readings for o in options]
        if len(readings) > MAX_READING_VARIANTS:
            continue
        intercept, slope = fits[bucket(word)]
        for reading in readings:
            print(line(reading, word, intercept + slope * math.log10(count)))


def place_readings(known, name):
    """Readings of a name from its longest dictionary parts; none when a part is unknown or too ambiguous."""
    readings, i = [""], 0
    while i < len(name):
        j = next(j for j in range(min(len(name), i + 4), i, -1) if name[i:j] in known or j == i + 1)
        part = name[i:j]
        if part not in known:
            return []
        if len(part) == 1:  # a character's rare readings sit at the floor; place names use the real ones
            top = max(known[part].values())
            options = [r for r, s in known[part].items() if s > FLOOR or top <= FLOOR]
        else:
            options = [max(known[part], key=known[part].get)]
        readings = [f"{r}-{o}" if r else o for r in readings for o in options]
        if len(readings) > MAX_READING_VARIANTS:
            return []
        i = j
    return readings


def places(data_path, roads_path):
    """Road and district names from 全國路名資料, with and without 路/街/鄉/區, so 福興路口 and 仁和大智路口 both type."""
    known = read_data(data_path)
    with open(roads_path, encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    roads = {r["road"].strip("　 ") for r in rows}
    roads = {r for r in roads if len(r) >= 3 and is_han(r) and ROAD_SUFFIX.search(r)}
    districts = {r["site_id"][3:] for r in rows}  # site_id is the county followed by the district
    names = roads | {d for d in districts if is_han(d)}
    names |= {ROAD_SUFFIX.sub("", r) for r in roads} | {d[:-1] for d in districts if len(d) >= 3}
    names = {n for n in names if len(n) >= 2 and is_han(n)}
    quartiles = {b: statistics.quantiles([max(known[n].values()) for n in names if n in known and bucket(n) == b], n=4)[0]
                 for b in (2, 3, 4)}
    for name in sorted(names - known.keys()):
        for reading in place_readings(known, name):
            print(line(reading, name, quartiles[bucket(name)]))


def merge(data_path, extra_paths):
    lines = []
    for path in [data_path, *extra_paths]:
        with open(path, encoding="utf-8") as f:
            lines += [l.rstrip("\n") for l in f if not l.startswith("#") and l.strip()]
    # ParselessPhraseDB binary-searches lines sorted by the bytes of their key.
    lines.sort(key=lambda l: l.split(" ", 1)[0].encode())
    sys.stdout.write(HEADER + "\n".join(lines) + "\n")


if __name__ == "__main__":
    command, data, *rest = sys.argv[1:]
    if command == "chewing":
        chewing(data, *rest)
    elif command == "mined":
        mined(data, *rest)
    elif command == "places":
        places(data, *rest)
    elif command == "merge":
        merge(data, rest)
    else:
        sys.exit(__doc__)

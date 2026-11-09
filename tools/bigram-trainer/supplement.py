"""Words McBopomofo's data.txt lacks, as data.txt lines, and merging them in.

Usage:
  supplement.py chewing <data.txt> <tsi.csv>      libchewing words data.txt lacks
  supplement.py mined <data.txt> <candidates.tsv> words mine.cpp found
  supplement.py places <data.txt> <roads.csv> <names dir>  place, school, agency and company names data.txt lacks
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
import unicodedata
from pathlib import Path

HEADER = "# format org.openvanilla.mcbopomofo.sorted\n"
MIN_COUNT = 50            # mined words seen fewer times are too uncertain
MIN_ASSOCIATION = 2.0     # log10 PMI: the parts occur together 100 times more than by chance
CONTAINED = 0.8           # a candidate seen mostly inside a longer one is a fragment of it
MAX_READING_VARIANTS = 4
FLOOR = -6.8              # data.txt's score for a character's rare readings (葉 ㄕㄜˋ)
ROAD_SUFFIX = re.compile(r"(大道|路|街|巷|道)$")
MAX_NAME = 8              # ReadingGrid::kMaximumSpanLength: longer words can never be typed as one
SCHOOL_OWNER = re.compile(r"^(?:[一-鿿]{2}[縣市])?(?:國立|市立|縣立|私立)")
COMPANY_FORM = re.compile(r"(?:股份)?有限公司$")
DEPARTMENT = re.compile(r"(?:系|所|學程|科)$")
AGENCY = re.compile(r"(?:部|署|局|處|院|廳|委員會|公所|法院|檢察署|研究所|戶政事務所|地政事務所)$")
NOT_AGENCY = re.compile(r"(?:診所|醫院|藥局|律師事務所)$")
NOT_PLACE = re.compile(r"(?:公墓|納骨|活動中心|行刑場)")


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


def clean(name):
    # Some government lists use compatibility ideographs that look the same but do not compare equal.
    return unicodedata.normalize("NFC", name or "").strip("　 ")


def rows_of(path):
    with open(path, encoding="utf-8-sig", errors="replace", newline="") as f:
        return list(csv.DictReader(f))


def official_names(folder):
    """Names from the open government lists in tools/bigram-trainer/MODELS-NOTICE.txt; never a person's name field."""
    names = set()
    for kind in ("admin", "natural", "street", "landmark", "settlement", "romanized"):
        for row in rows_of(folder / f"place-{kind}.csv"):
            for name in (clean(row["PlaceName"]), clean(row.get("AnotherName"))):
                if NOT_PLACE.search(name):
                    continue
                # Two-character hamlet names would mostly shadow ordinary words of the same reading.
                if len(name) >= 3 or kind in ("admin", "natural"):
                    names.add(name)
                if kind == "admin" and len(name) >= 3 and name[-1] in "里村":
                    names.add(name[:-1])
    for kind in ("university", "junior", "elementary"):
        for row in rows_of(folder / f"school-{kind}.csv"):
            name = clean(row["學校名稱"])
            names |= {name, SCHOOL_OWNER.sub("", name)}
    for row in rows_of(folder / "departments.csv"):
        name = re.split(r"[(（]", clean(row["科系名稱"]))[0]
        if DEPARTMENT.search(name):
            names.add(name)
    for path, field in (("agency-addressbook.csv", "ORGNAME"), ("agency-oid.csv", "OrgName")):
        for row in rows_of(folder / path):
            name = clean(row[field])
            if AGENCY.search(name) and not NOT_AGENCY.search(name):
                names.add(name)
    for kind in ("listed", "otc", "emerging"):
        for row in rows_of(folder / f"company-{kind}.csv"):
            names |= {COMPANY_FORM.sub("", clean(row["公司名稱"])), clean(row["公司簡稱"])}
    for row in rows_of(folder / "shopping-areas.csv"):
        name = clean(row["商圈名稱"])
        names |= {name, name.removesuffix("形象商圈"), name.removesuffix("商圈")}
    return names


def places(data_path, roads_path, names_folder):
    """Road and district names from 全國路名資料, with and without 路/街/鄉/區, so 福興路口 and 仁和大智路口 both type."""
    known = read_data(data_path)
    rows = rows_of(roads_path)
    roads = {clean(r["road"]) for r in rows}
    roads = {r for r in roads if len(r) >= 2 and is_han(r)}  # 半山雅 and 前街 are roads too
    districts = {clean(r["site_id"])[3:] for r in rows}  # site_id is the county followed by the district
    names = roads | {d for d in districts if is_han(d)}
    names |= {ROAD_SUFFIX.sub("", r) for r in roads if ROAD_SUFFIX.search(r)} | {d[:-1] for d in districts if len(d) >= 3}
    names |= {f"{n}{unit}" for n in "一二三四五六七八九十" for unit in "段巷弄"}  # the road list has no sections
    names |= official_names(Path(names_folder))
    # People type 台 for most names written 臺 in the lists, and the training text does too.
    names |= {n.replace("臺", "台") for n in names}
    names = {n for n in names if 2 <= len(n) <= MAX_NAME and is_han(n)}
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

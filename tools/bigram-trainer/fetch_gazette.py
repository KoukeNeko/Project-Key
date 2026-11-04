"""Downloads Legislative Yuan Gazette agenda texts (Open Government Data License v1)
through the g0v LY API into <dir>/<doc id>.txt, skipping files already there.
Usage: fetch_gazette.py <dir> <term> ...
"""
import concurrent.futures
import json
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

API = "https://v2.ly.govapi.tw"
WORKERS = 3  # a shared volunteer-run service


def get(url, attempts=4):
    for attempt in range(attempts):
        try:
            with urllib.request.urlopen(url, timeout=120) as response:
                return response.read()
        except OSError as error:
            if attempt == attempts - 1:
                raise
            print(f"retry {url}: {error}", file=sys.stderr)
            time.sleep(5 * (attempt + 1))


def text_urls(term):
    urls, page = {}, 1
    while True:
        query = urllib.parse.urlencode({"屆": term, "limit": 100, "page": page})
        listing = json.loads(get(f"{API}/gazette_agendas?{query}"))
        for agenda in listing["gazetteagendas"]:
            for doc in agenda.get("處理後公報網址") or []:
                if doc["type"] == "txt":
                    urls[doc["url"].split("/")[-2]] = doc["url"]
        if page >= listing["total_page"]:
            return urls
        page += 1


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    urls = {}
    for term in sys.argv[2:]:
        urls.update(text_urls(term))
    todo = {doc: url for doc, url in urls.items() if not (out / f"{doc}.txt").exists()}
    print(f"{len(urls)} documents, {len(todo)} to download", file=sys.stderr)

    def fetch(item):
        doc, url = item
        try:
            body = get(url)
        except OSError as error:
            return f"skipped {doc}: {error}"
        (out / f"{doc}.part").write_bytes(body)
        (out / f"{doc}.part").rename(out / f"{doc}.txt")
        return None

    with concurrent.futures.ThreadPoolExecutor(WORKERS) as pool:
        for i, problem in enumerate(pool.map(fetch, todo.items()), 1):
            if problem:
                print(problem, file=sys.stderr)
            if i % 200 == 0:
                print(f"{i}/{len(todo)}", file=sys.stderr)


if __name__ == "__main__":
    main()

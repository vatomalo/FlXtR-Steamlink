#!/usr/bin/env python3
"""Cache the site's public movie/TV listings; never fetch video or server links.

The upstream API caps each popular list at 500 pages, regardless of total_pages.
One worker per list, at most one request/second/worker. Cached pages allow resume.
"""
import argparse
import concurrent.futures
import json
import pathlib
import time
import urllib.error
import urllib.request

BASE = "https://plsdontscrapemelove.flixer.gd/api/tmdb"

def clean(value, size):
    return str(value or "").replace("\t", " ").replace("\n", " ").replace("\r", " ").encode("ascii", "replace").decode()[:size]

def crawl(kind, cache, max_pages):
    rows, seen = [], set()
    pages = max_pages
    for page in range(1, max_pages + 1):
        target = cache / f"{kind}-{page}.json"
        if target.exists():
            data = json.loads(target.read_text())
        else:
            for attempt in range(4):
                start = time.monotonic()
                try:
                    req = urllib.request.Request(f"{BASE}/{kind}/popular?page={page}", headers={"User-Agent": "FlXtR-Steamlink/0.2 catalog"})
                    with urllib.request.urlopen(req, timeout=25) as response:
                        raw = response.read(2 * 1024 * 1024 + 1)
                    if len(raw) > 2 * 1024 * 1024:
                        raise ValueError("Catalog response too large")
                    data = json.loads(raw)
                    if not isinstance(data.get("results"), list):
                        raise ValueError("Unexpected catalog response")
                    temp = target.with_suffix(".tmp")
                    temp.write_bytes(raw)
                    temp.replace(target)
                    break
                except urllib.error.HTTPError as exc:
                    if exc.code not in (429, 500, 502, 503, 504) or attempt == 3:
                        raise
                    time.sleep(min(60, 5 * 2**attempt))
                except (OSError, ValueError):
                    if attempt == 3:
                        raise
                    time.sleep(3 * 2**attempt)
                finally:
                    time.sleep(max(0, 1 - (time.monotonic() - start)))
        pages = min(max_pages, 500, int(data.get("total_pages", 1)))
        for item in data["results"]:
            ident = item.get("id")
            if not isinstance(ident, int) or ident <= 0 or ident in seen:
                continue
            seen.add(ident)
            title = clean(item.get("title") or item.get("name"), 79)
            date = item.get("release_date") or item.get("first_air_date") or ""
            meta = clean(f"{date[:4]} / {'SERIES' if kind == 'tv' else 'MOVIE'}", 95)
            poster = clean(item.get("poster_path"), 100)
            if not poster.startswith("/") or ".." in poster:
                poster = ""
            rows.append(f"{title}\t{meta}\t{poster}\t\t{kind}\t{ident}\t0\t0\n")
        if page % 25 == 0 or page == pages:
            print(f"{kind}: {page}/{pages} pages; {len(rows)} unique titles", flush=True)
        if page >= pages:
            break
    return rows

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--pages", type=int, default=500)
    args = parser.parse_args()
    if not 1 <= args.pages <= 500:
        parser.error("--pages must be 1..500")
    args.cache.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(crawl, kind, args.cache, args.pages) for kind in ("movie", "tv")]
        results = [f.result() for f in futures]
    temp = args.output.with_suffix(".tmp")
    with temp.open("w", encoding="utf-8", newline="\n") as out:
        out.write("# Public metadata snapshot. Popular-list limit: 500 pages per type. No playback links.\n")
        for rows in results:
            out.writelines(rows)
    temp.replace(args.output)
    print(f"Saved {sum(map(len, results))} titles to {args.output}", flush=True)

if __name__ == "__main__":
    main()

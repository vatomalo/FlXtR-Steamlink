#!/usr/bin/env python3
"""Import phone-uploaded videos into FlXtR's LOCAL catalog (standard library only).

Run from /home/apps/greenlink or pass --root. The resulting catalog.tsv is
read by the native LOCAL screen. Never relocates, deletes, or rewrites uploads.
"""
import argparse
import os
from pathlib import Path
import tempfile

VIDEO_SUFFIXES = {".mp4", ".mkv", ".webm", ".avi", ".mov"}
MARKER = "# FLXTR-PHONE-LOCAL-AUTO"
MAX_NAME = 100

def valid_name(name):
    return (0 < len(name) <= MAX_NAME and not name.startswith(".") and
            ".." not in name and all(c.isascii() and
            (c.isalnum() or c in "._-") for c in name))

def rows(root):
    directory = root / "phone-files"
    if not directory.is_dir():
        return []
    records = []
    for file in sorted(directory.iterdir(), key=lambda p: p.name.casefold()):
        if not valid_name(file.name) or file.suffix.lower() not in VIDEO_SUFFIXES:
            continue
        if file.is_symlink() or not file.is_file():
            continue
        title = file.stem.replace("_", " ").replace("-", " ").strip()
        if not title:
            continue
        # 8 columns: title, metadata, artwork, URL, kind, id, season, episode.
        records.append("\t".join((title[:79], "PHONE UPLOAD / LOCAL", "",
                                   "phone-files/" + file.name, "offline", "0", "0", "0")))
    return records

def update(root):
    root = Path(root).resolve()
    catalog = root / "catalog.tsv"
    existing = catalog.read_text(encoding="utf-8") if catalog.exists() else ""
    begin = MARKER + " BEGIN"
    end = MARKER + " END"
    if begin in existing:
        if end not in existing:
            raise ValueError("incomplete existing managed section; refusing to change catalog")
        first = existing.index(begin)
        last = existing.index(end, first) + len(end)
        existing = existing[:first] + existing[last:]
    data = rows(root)
    managed = "\n".join([begin, *data, end])
    output = existing.rstrip("\r\n") + ("\n" if existing.strip() else "") + managed + "\n"
    if catalog.exists() and catalog.is_symlink():
        raise ValueError("refusing to replace symlink catalog")
    if catalog.exists() and catalog.read_text(encoding="utf-8") == output:
        return len(data), False
    # Atomic replacement in the same filesystem; no partial catalog on errors.
    fd, temporary = tempfile.mkstemp(prefix=".catalog-phone-", dir=root)
    try:
        if catalog.exists():
            os.chmod(temporary, catalog.stat().st_mode & 0o777)
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(output)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, catalog)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return len(data), True

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    count, changed = update(args.root)
    print(f"LOCAL: {count} phone videos indexed; catalog {'updated' if changed else 'unchanged'}")

if __name__ == "__main__":
    main()

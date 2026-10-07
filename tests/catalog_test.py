"""Exercise bounded pages and movie/episode source matching through the CLI."""
import json
import time
import os
import pathlib
import subprocess
import tempfile

worker = pathlib.Path("build/greenlink-catalog").resolve()
with tempfile.TemporaryDirectory() as folder:
    root = pathlib.Path(folder)
    rows = [f"Movie {i}\t2000 / MOVIE\t\t\tmovie\t{i}\t0\t0\n" for i in range(1, 8)]
    rows += ["#A Show\tSERIES\t\t\ttv\t100\t0\t0\n"]
    (root / "library.local.tsv").write_text("".join(rows))
    (root / "sources.local.tsv").write_text(
        "1\t0\t0\tmovie\tServer A\t720P\thttps://example.org/movie.m3u8\n"
        "100\t1\t2\ttv\tServer A\t720P\thttps://example.org/episode.m3u8\n"
        "100\t1\t2\ttv\tServer B\t1080P\thttps://example.org/backup.m3u8\n"
        "100\t1\t3\ttv\tOther episode\t720P\thttps://example.org/wrong.m3u8\n"
        "100\t1\t2\ttv\tInvalid\t720P\tfile:///etc/passwd\n"
    )
    def run(kind, page=1, ident=0, season=0, episode=0):
        result = subprocess.run([str(worker), kind, str(page), "", str(ident), str(season), str(episode)], cwd=root, env={**os.environ, "FLXTR_NO_ART": "1"}, capture_output=True, text=True, check=True)
        return result.stdout.splitlines()
    first = run("movie")
    assert len(first) == 7
    cached = root / "catalog-cache/page-1.json"
    stamp = cached.stat().st_mtime_ns
    assert len(json.loads(cached.read_text())["entries"]) == 6
    assert run("movie") == first and cached.stat().st_mtime_ns == stamp
    cached.write_text("{truncated")
    assert run("movie") == first  # corrupt cache rebuilds
    os.utime(cached, (time.time()-90000, time.time()-90000))
    assert run("movie") == first and cached.stat().st_mtime > time.time()-10
    # A cache from another library cannot masquerade as this page.
    assert run("tv")[1].startswith("#A Show\t")
    assert run("movie") == first
    for page in range(1, 7): run("movie", page)
    assert len(list((root / "catalog-cache").glob("page-*.json"))) == 3
    assert run("movie", 2)[0] == "# pages=2 total=7"
    assert run("movie", 2)[1].startswith("Movie 7\t")
    assert run("tv")[1].startswith("#A Show\t")
    episode = run("source", ident=100, season=1, episode=2)
    assert len(episode) == 3 and "Server A" in episode[1] and "Server B" in episode[2]
    assert "wrong.m3u8" not in "".join(episode) and "file:" not in "".join(episode)
    assert len(run("source", ident=100, season=1, episode=9)) == 1
    assert "movie.m3u8" in run("source", ident=1)[1]
print("PASS: catalog paging, series filter, multiple sources, episode identity and URL restriction")

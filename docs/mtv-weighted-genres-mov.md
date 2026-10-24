# MTV weighted genres and MOV support (experimental)

## Music video genre weights

In FlXtR settings select **CUSTOM / MIX** for the MTV genre. If a nonempty `mtv-query.txt` is present, that custom search still wins.

Without a custom query, create `mtv-weights.tsv` in FlXtR's working directory, for example:

```text
1 0
2 2
3 8
4 0
5 0
6 0
7 5
8 0
9 0
10 0
```

Entries are `genre_id weight` (0..100), one per line. Zero means disabled. Above, Rock gets 2 parts, Hip Hop 8, and R&B / Soul 5; all other genres are disabled. To play **Hip Hop only**, set ID 3 to 100 and all other IDs to zero. Alternatively choose HIP HOP directly in existing MTV settings.

IDs: 1 Pop; 2 Rock; 3 Hip Hop; 4 Electronic; 5 Metal; 6 Jazz; 7 Country; 8 R&B / Soul; 9 K-Pop; 10 Anime music.

If the file is missing, all ten genres have equal weights. Genre choice advances when a new MTV query is requested, not on every rendered frame. Weights affect *which genre is searched*, not individual search result ranking. TV's existing permanent play ledger still governs no-repeat behavior.

**MOV:** Archive listings now admit `.mov` alongside `.mp4` when Archive reports H.264 or MPEG4. MOV is only a container: support for ProRes, HEVC, unusual audio codecs, and seeking depends on the actual player/demuxer and device decoder. This change does not add codec support, nor does it promise compatibility with every MOV.

## Test before deploying

```sh
cc -std=c11 -Wall -Wextra -Werror -Isrc tests/mtv_weights_test.c -o /tmp/mtv-weights-test
/tmp/mtv-weights-test
```

Do not deploy to a Steam Link whose `/home/apps` storage is full. Never create recursive full-install backups on the full filesystem.

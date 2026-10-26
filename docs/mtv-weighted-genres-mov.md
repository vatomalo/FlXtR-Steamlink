# MTV weighted genres and MOV support (experimental)

## Music video genre weights

Open **Settings → MUSIC → MTV GENRE**, then use Left/Right to cycle the choices. Select **CUSTOM / MIX** for weighted selection. In that mode, a nonempty `mtv-query.txt` takes priority over the weights.

Open **MTV GENRE WEIGHTS** in the same MUSIC tab to edit every genre with the controller. Up/Down selects the genre, Left/Right adjusts by 1, shoulders adjust by 10, A toggles mute, X resets equal weights, and B saves and returns. Weights range from 0 to 100; zero disables a genre. The editor shows the current MTV and fusion modes. Selecting a single MTV genre still overrides weighted mixing.

**FUSION GENRE** chooses the style paired with VGA or Karaoke: a specific genre, **WEIGHTED MIX**, or **CUSTOM STYLE FILE**. A manual genre or Weighted Mix takes priority over an existing `mtv-fusion-style.txt`. On an older installation with that file and no saved fusion setting, Custom Style File is initially selected to preserve the previous behavior. An absent or empty custom style falls back to the weights. The style weights exclude VGA/Karaoke to avoid recursive combinations.

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
11 0
12 0
13 0
14 0
15 0
```

Entries are `genre_id weight` (0..100), one per line. Zero means disabled; omitted IDs retain weight 1. Above, Rock gets 2 parts, Hip Hop 8, and Country 5; all other genres are disabled. To play **Hip Hop only**, set ID 3 to 100 and all other IDs to zero. Alternatively choose HIP HOP directly in MTV settings.

IDs: 1 Pop; 2 Rock; 3 Hip Hop; 4 Electronic; 5 Metal; 6 Jazz; 7 Country; 8 R&B / Soul; 9 K-Pop; 10 Anime music; 11 VGA + GENRE; 12 Karaoke + GENRE; 13 Enka; 14 City Pop; 15 Latin.

If the file is missing, all fifteen genres have equal weights. Genre choice advances when a new MTV query is requested, not on every rendered frame. Weights affect *which genre is searched*, not individual search result ranking. TV's existing permanent play ledger still governs no-repeat behavior.

VGA and Karaoke choose their second genre from the same weights, excluding IDs 11 and 12. `mtv-vga-subject.txt` can set a subject and `mtv-fusion-style.txt` can override the second genre. With no eligible positive style weight, the fallback style is City Pop. Karaoke queries request instrumental backing tracks; actual search results depend on YouTube.

**MOV:** Archive listings now admit `.mov` alongside `.mp4` when Archive reports H.264 or MPEG4. MOV is only a container: support for ProRes, HEVC, unusual audio codecs, and seeking depends on the actual player/demuxer and device decoder. This change does not add codec support, nor does it promise compatibility with every MOV.

## Test before deploying

```sh
make test-mtv
python3 tests/deploy_steamlink_test.py
```

Do not deploy to a Steam Link whose `/home/apps` storage is full. Never create recursive full-install backups on the full filesystem.

## Verified manual deployment

Compile the feature branch with `bash scripts/autocompile --steamlink --no-update` in the SDK container. The archive root is `greenlink/`, and the executable inside it is `greenlink/greenlink`. Extracting that archive inside `/home/apps/greenlink` uses the wrong directory depth. The `build/greenlink-arm` filename is only the compiler output; the launcher runs `./greenlink`.

Run `python3 scripts/deploy-steamlink.py dist/greenlink.tgz --verify-only` to validate the archive. With SSH access, omit `--verify-only` to stage, back up and atomically replace the shell, catalog, player, launcher, activity helper and USB setup helpers. An existing SSH control socket can be supplied with `--control-path`. This targeted deployment requires all three executables to report the same committed version, preserves user data, and disables automatic updates while testing the feature branch. It does not replace the music runtime or install optional add-ons.

The script prints the backup directory and checks private configuration hashes. Relaunch the app afterward. Verify `/home/apps/greenlink/greenlink --version` and `--mtv-genres`, then compare the running process's `/proc/PID/exe` hash with `build-sha256.txt`. A disk file update alone does not replace an already running process. Re-enable automatic updates in Settings after branch testing is finished.

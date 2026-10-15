# Catalogue and phone performance

Public TMDB metadata and RomsGames listing responses are now reused across
six-item UI pages. A RomsGames website page holds 36 games, so one fetch now
feeds six screens. Movie lists and season/episode pages also reuse their
original response. The existing final-page JSON and three-screen RAM caches
remain in place.

The new disk cache uses 16 slots, full-URL identity checks, a six-hour lifetime,
a 512 KiB input limit and a 1 MiB serialized limit per slot. Writes are locked
and atomic. Broken, expired, oversized or colliding records are cache misses.
This cache never stores media links, credentials or protected download replies.

Catalogue requests reuse HTTPS connections within each worker, reducing repeated
DNS/TLS setup for covers. Connections are closed before background workers fork.

The phone screen polls its local listener every 10 ms while idle (previously
up to 250 ms) and refreshes its display at 10 fps during uploads instead of
redrawing for every transfer batch. Controller events still redraw immediately.
Upload size limits, bounded chunks and atomic file installation are unchanged.

WSL measurement with artwork disabled, live SNES listing, fresh temporary cache:
first page 0.694 seconds; following five pages 0.024–0.027 seconds each.
These are catalogue-worker timings on the development PC, not Steam Link frame
rates or measured Wi-Fi transfer speeds. Hardware performance is not yet timed.

Checks: response cache reuse/expiry/corruption/collision/size bounds, RomsGames
parser and phone handoff, catalogue regressions and phone HTTP/upload tests.

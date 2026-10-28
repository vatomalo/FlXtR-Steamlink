# Favorite covers

Opening Favorites automatically checks missing or corrupt movie, series, anime
and Internet Archive covers. A separate catalog worker recovers them without
blocking navigation. Completed covers appear while the library stays open.
Valid cached images are reused, and a missing index can be rebuilt from them.

Images stay under `favorites-art/`, keyed by the full saved identity (kind,
title, URL, ID, season and episode). Page-scoped catalog thumbnails are not used
as a fallback, so unrelated titles and same-named movies/series cannot inherit
one another's covers. Downloads are decoded and size-checked before the stable
file is replaced. Failed fetches leave existing artwork intact.

Favorites themselves remain in `favorites.tsv` and are never rewritten by the
repair. Offline failures retry after five minutes. The worker processes at most
32 missing covers per batch. Run `make test-favorites-art` for cache repair,
identity isolation, background refresh, index rebuild and retry tests.

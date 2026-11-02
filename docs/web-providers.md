# Native movie/TV fallback providers

The order is **Flixer mirrors → FMovies → SFlix**. Saved mirror preferences
apply within Flixer and cannot move a later provider ahead of it. FMovies and
SFlix remain discoverable if the Flixer lookup fails. KissAnime is unchanged.

The addresses inspected on 2026-11-02 are `https://www.f-moviess.org` and
`https://ssflix.pro`. Both use TMDB identities, so the resolver preserves the
movie/series ID and season/episode rather than guessing from title strings.

- FMovies server 3 uses VidAPI. Its XPS route opens VIDEM. The adapter reads the
  public embed's temporary source references and requests its playback API.
- SFlix server 1 uses MoviesAPI, currently redirected to VidSpark. The adapter
  reads its current public application bundle/header, then requests the same
  movie/episode API used by the website. No fixed player key is stored.

Only supported native HLS results are returned, never a browser iframe. The
other embedded players on these websites are not currently supported. An
unavailable backend advances to the next provider. The resolver uses bounded
HTTPS requests, validates returned media URLs/playlists, and omits tokens and
media URLs from its diagnostics. Expiring references are fetched on each lookup.

SFlix's current CDN requires its player referrer, and uses nonstandard playlist
and segment suffixes. Its fixed provider profile supplies the referrer and
relaxes HLS extension checking; the existing remote-only protocol whitelist
continues to exclude local-file access. The profile follows automatic/manual
quality selections and offline downloads.

Validation: `make test-providers` covers parsing, provider order despite a saved
SFlix preference, failed-provider advancement, playback environment and fixed
HTTP profiles. A live TMDB movie 10331 lookup returned H.264/AAC renditions and
passed an FFmpeg media probe. BoJack S1E1 was unavailable on SFlix's tested
backend. VIDEM returned `unavailable` for the tested FMovies titles; its failure
path was exercised, but successful FMovies video playback remains unverified.

The pre-existing `auto_server_test.c:90` next-episode assertion also fails on
unchanged main; the dedicated provider fallback test passes. This integration
does not change the next-episode behavior.

# Flixer subtitle discovery

Flixer currently loads its subtitle catalogue separately from video sources, using https://sub.vdrk.site/v1 and /v2 with /movie/TMDB_ID or /tv/TMDB_ID/SEASON/EPISODE. Its public VideoPlayer bundle (52585954, inspected 2026-10-14) combines both catalogues. These captions are often external WebVTT files, rather than tracks embedded in the media stream.

The native resolver now queries both catalogues for movies and episodes. Version 2 is preferred, then version 1 fills missing languages. URLs containing spaces or non-ASCII bytes are percent-encoded. The existing video/playlist captions and optional SubDL fallback remain available. Every quality row receives the captions, including movies.

The current subtitle menu still offers Off, Auto, English, Norwegian and Spanish. The website advertises additional languages and versions that this menu does not yet expose. No SubDL key is required for these provider captions. Synchronisation can differ by release; the player subtitle delay adjustment remains available.

Validation: resolver tests cover label/file catalogue entries, space escaping, three-language handoff and unsafe URL rejection. Live lookup returned all three supported languages for movie 550 and TV 1396 S1E1. The installed Steam Link player downloaded and parsed 1861 movie subtitle packets and 587 episode packets in probe mode, without interrupting hardware playback.

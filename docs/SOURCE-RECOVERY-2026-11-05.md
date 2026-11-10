# Movie and series source recovery — November 5, 2026

The main movie/episode provider changed its public client's `x-fingerprint-lite`
request marker. FlXtR's previous marker was rejected with HTTP 403, preventing
server discovery even though titles and streams remained available. KissAnime
and Internet Archive use separate paths and were unaffected by this failure.

The Android and shared native resolvers now send the marker used by the current
provider application. The decoder module's SHA-256 is unchanged; this fix does
not replace the decoder or change signature generation. HTTPS verification and
bounded network requests remain enabled.

Live checks reproduced HTTP 403 before the fix, then discovered the current
server list and obtained fresh movie and episode HLS playlists after it. These
are network/resolution checks, separate from physical playback on each device.
Individual third-party servers can still be unavailable. Signed stream URLs are
not logged or stored as permanent download links.

Android release: **0.10.26**. The same native correction is included in refreshed
Steam Link, Windows, Linux and Steam Deck packages.

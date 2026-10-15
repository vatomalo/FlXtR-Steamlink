# KissAnime resolution fallback

One Piece episode 1 (KissAnime post 1365), SUB, was checked on the Steam Link.
Resolution and source lookup succeeded, but the 720p player probe exited 45.
The same URL passed the 1080p probe: H.264 1440x1080 and stereo AAC at 48 kHz.
This is a compatible stream with no rendition within the default quality cap.

Normal automatic playback now tries every server at the configured quality
first. If at least one reports no compatible track, it retries the server list
at the next quality: 480 → 720 → 1080. It stops after 1080 and resets for each
new title. Resolution is never raised just for a network/source lookup failure.
The saved quality preference stays unchanged. TV keeps its existing retry policy.
The player continues to reject unsupported codecs, bit depth and dimensions.

PR7 subtitle confirmation is retained. Tests cover exhaustion order and bounded
retry rounds. The live check inspects media without taking over the display;
it does not claim every KissAnime host or episode has been tested.

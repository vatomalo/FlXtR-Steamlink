# Phone remote and Pocket Quiz beta

Open Settings → API Accounts, scan the large QR and follow **Phone remote,
uploads and party quiz**. Keep this TV screen open; it also stays connected
during video playback launched from the phone. B closes the room when back
on the setup screen. A generates a new room and clears quiz players/scores.

The small QR below it joins the Steam Link's active Wi-Fi using its saved
ConnMan credentials. It is shown only for an active supported Wi-Fi service
with readable credentials. This QR contains the Wi-Fi password: share the TV
screen only with people you want on your network. It is never served over
HTTP, written to a file, or included in logs. Enterprise Wi-Fi and other
network managers are not supported by this credential reader.

Uploads stream to disk with a 16 KB receive buffer and a 512 MB file limit.
Names are sanitized on the phone, with a 100-character basename limit; paths,
executables, overwriting existing files and transfers without sufficient free
space are rejected. Interrupted transfers are removed. Only one HTTP client
is served at a time, so controls/polls wait during an upload. Videos and text
subtitles go into phone-files; ROMs go into roms/phone and can be picked up by
Import Games. The phone file picker lists up to 32 uploads.

Select an uploaded SRT/VTT before playing a video to use it as the external
subtitle. Local files and direct HTTP/HTTPS video links use the existing
player and its format/codec support. Stop one video before starting another.
Controls include pause/resume, stop, ten-second seeks, picture size and menu
navigation for subtitle/options changes. This is direct playback, not AirPlay,
Chromecast, protected-app casting or screen mirroring.

Pocket Quiz is an original small quiz prototype: up to eight phones, five
questions per game, twenty seconds per question, five-second answer reveals,
100 points per correct answer, scores on TV. Join before starting; each phone
has a random player token and can answer once per round. No account or cloud
service is involved. The browser polls every two seconds; an open remote keeps
the room alive. Without valid requests or an active upload it expires after
ten minutes. Use a trusted home LAN: this setup connection uses HTTP.

The API form now uses Referrer-Policy: same-origin. The previous no-referrer
policy could cause browsers to submit Origin: null, which correctly failed
the origin check. The origin check is retained; changing the page policy fixes
normal same-origin form submissions. Rescan/reload after updating.

The player control pipe is explicitly retained across exec, while graphics
handles remain close-on-exec. This repairs the launch regression introduced
by broad descriptor isolation.

Pocket Quiz is also available directly from About > Party Games / Pocket Quiz and Settings > Party Games. Its QR opens the quiz section directly; API keys are not required.

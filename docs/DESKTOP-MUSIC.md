# Music and phone remote

Windows includes Python, yt-dlp, EJS and QuickJS. Linux and Steam Deck include yt-dlp,
EJS and QuickJS and require host Python 3.10 or newer. All three desktop packages
include the libmpv audio worker. No background server is installed.

A selected playlist or search plays its remaining tracks first. At the end,
FlXtR requests the public YouTube radio mix for the last resolved song and plays
the first recommendation outside the last 50 resolved tracks. It repeats this
after each completed song. Lookup failures or unavailable recommendations stop
with an error instead of looping. Pausing does not advance the queue. Phone Stop
cancels the current worker and prevents further autoplay. Leaving the music
browser keeps music playing.

Open phone setup and keep the QR screen open. The phone uses a local HTTP remote,
with a random session URL; it does not use WebSockets. Sending a music search or
public link plays the first matching track. Direct video URLs must point to
playable media, rather than a YouTube or streaming-service webpage.

ROM uploads are saved under the writable data directory's roms/phone folder.
Existing files are never replaced, interrupted uploads are removed, and a
completed upload invalidates the game index. Close the QR screen to import games.
Install a matching core through Games > DOWNLOAD LIBRETRO CORES and install
RetroArch separately. Upload all referenced BIN/CUE files together; the importer
uses the CUE. Generic BIN files alone do not identify a console. ZIP archives can
be browsed in Games. The maximum upload size is 512 MB.

YouTube playback depends on publicly accessible media and YouTube's current
restrictions. Age-restricted, private, protected or bot-blocked media may fail.
No accounts, browser cookies, ROMs or BIOS files are bundled. Steam Link keeps its
existing ARM runtime; the shared queue/phone fixes do not deploy to any device.

Sources: YouTube extraction uses [yt-dlp](https://github.com/yt-dlp/yt-dlp).
The JavaScript engine is [QuickJS](https://bellard.org/quickjs/).

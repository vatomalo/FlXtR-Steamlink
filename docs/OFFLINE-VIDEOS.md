# Offline videos

On a movie or episode, press **L1** (keyboard **D**) to **Download to local**.
FlXtR resolves a source using its normal server selection and downloads it
without starting playback. On a resolved quality/source row, **Y** also starts
the download. Direct Internet Archive video files use the same controls.

Browse other libraries while it runs. The footer shows progress and stored MB.
Open **Downloads** using **X / F2** to switch libraries. **A** plays a completed
video without looking up its server or requiring an Internet connection.
**Y** deletes the selected local copy after confirmation. During a download,
**Y in Downloads** or keyboard **D** cancels it. Exiting FlXtR cancels unfinished
transfers; failed and cancelled transfers remove the incomplete file.

Videos are stored under `phone-files/offline-<key>.mkv`, with their local library
records under `video-downloads/`. These use the filesystem hosting FlXtR, including
USB-backed app storage when configured. There is a 64 MB free-space reserve.
Existing completed files are kept. The UI does not list partial files.

The native downloader copies H.264 video and one audio track into Matroska,
without re-encoding. It accepts finite HTTP(S) files and VOD HLS playlists using
the same TLS verification and provider referer as the player. Live/unknown-length
streams, YouTube's separately resolved audio/video, external subtitles and
resuming interrupted downloads are outside this first version.

Steam Link packaging includes `greenlink-download`; Linux packaging builds it
against the system FFmpeg libraries. The Windows UI reports that the native
downloader is unavailable. Run `make test-video-downloads` with FFmpeg CLI and
development libraries installed. Tests cover direct files, HLS, offline decode,
low storage, cancellation, duplicate protection, source resolution without
autoplay, literal URL arguments, offline listing and confirmed deletion.

The Steam Link worker uses the same checksum-pinned FFmpeg 4.4.5 source as the
player (`scripts/build-player.sh`), adding the Matroska muxer. FFmpeg is built
under its LGPL configuration. Source archive:
https://ffmpeg.org/releases/ffmpeg-4.4.5.tar.xz

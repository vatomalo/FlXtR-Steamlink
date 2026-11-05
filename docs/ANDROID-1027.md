# Android 0.10.27: Tailscale pairing

The Continue on TV pairing dialog can open the official Tailscale app, or its
Play Store page if it is not installed. Connect the phone and Steam Link to your
tailnet, choose the remote QR on the TV, then pair FlXtR normally.

The Steam Link companion update adds managed userspace Tailscale, QR sign-in,
private forwarding, connection status, disconnect/sign-out and persistent remote
pairing. No separate cloud server is required. LAN handoff remains available.

Retains the movie/series source fix from 0.10.26, Turkish subtitles and embedded
cores. Exact game handoff is still initially Game Boy/Game Boy Color with the
matching ROM already on TV. No normal-save fallback is used.

Setup and test scope:
https://github.com/vatomalo/FlXtR-Steamlink/blob/main/docs/TAILSCALE.md

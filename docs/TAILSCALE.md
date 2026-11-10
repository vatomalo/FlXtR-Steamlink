# Tailscale for Continue on TV

No additional cloud server is required. GitHub hosts FlXtR's source and packages;
the Steam Link runs the receiver. Tailscale supplies encrypted private connectivity
between devices that are signed in to the same tailnet and permitted by its policy.

## Steam Link

1. Open **Settings > Services > Tailscale**.
2. Press **A** to install/connect. The first connection downloads the official
   ARM 1.104.5 archive and verifies the pinned SHA-256 before installing the two
   binaries. Leave at least 256 MB of free space. It is downloaded on demand to
   keep the full FlXtR package below the existing updater's size limit.
3. Scan the displayed QR and sign in to your own Tailscale account. If your
   tailnet requires device approval, approve the Steam Link in its admin console.
4. Wait for **CONNECTED … REMOTE CONTINUE ON TV READY**.
5. Open **Continue on TV**, then press **Y** to choose the remote address/QR.

**X** disconnects while retaining the private sign-in for reconnection. **Y**
signs out. The daemon is private to FlXtR, starts automatically on later FlXtR
launches once enabled, and stops when FlXtR's launcher exits. It does not create
a system boot service or alter system DNS, default routes or the firmware VPN.

The daemon uses userspace networking and Tailscale Serve to forward private
TCP port 43927 to FlXtR's local receiver. This is not an exit node, subnet router,
public Funnel or a system-wide VPN for other Steam Link applications. Moonlight
UDP streaming is not enabled by this TCP-only integration.

## Android

Install/open the official Tailscale app and sign in to the same account/tailnet.
FlXtR's pairing dialog includes **Open Tailscale**, with an official Play Store
link if it is not installed. Connect there, then scan the TV's remote QR and
confirm FlXtR pairing. The remote address is a private 100.64.0.0/10 address.

Once paired remotely, the receiver token and private Tailscale identity survive
FlXtR restarts, so the remote pairing address stays stable. Remote handoffs are
accepted over the private Tailscale forward while FlXtR is running and Tailscale
is connected. Busy TV playback is rejected rather than queued for later surprise
playback. Disconnecting or signing out closes remote reception.

On the same LAN, use the existing LAN QR without requiring Tailscale. LAN
reception still requires the receiver screen's ten-minute receive window.
The movie/episode and exact-state compatibility limits in
[Continue on TV](CONTINUE-ON-TV.md) remain unchanged.

## Private state and verification

The local `tailscale/` directory and pairing token are private app data, excluded
from source control and release packages. Identity files and login links are
stored with restricted permissions. Login URLs are displayed on the TV, never
served from the handoff website or included in normal status/log output.
The app does not need a shared auth key, OAuth application registration or a
GitHub-hosted database.

Verified: official archive checksum, ARM CLI/daemon version, ARM daemon startup
in userspace mode, its local status API and production of an official QR login
URL. Automated checks cover sign-in parsing, forwarding setup, disconnect cleanup,
host/origin checks, remote pairing links, busy reception and existing handoffs.
An authenticated two-device transfer still requires signing in with your account;
the headless checks do not represent a physical Steam Link tailnet session.

Primary references:
- https://tailscale.com/docs/concepts/userspace-networking
- https://tailscale.com/docs/reference/tailscale-cli/serve
- https://pkgs.tailscale.com/stable/

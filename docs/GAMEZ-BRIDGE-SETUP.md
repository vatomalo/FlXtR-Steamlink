# Gamez bridge: one Sunshine entry

The Steam Link package has an **opt-in single-entry bridge**. FlXtR sends the
selected catalog ID to the Windows companion; the companion resolves that ID
against its explicit allowlist and starts the configured executable. Moonlight
then opens one dedicated Sunshine application. This does not create a Sunshine
application for every title. The feature remains opt-in until both host and
Steam Link are configured.

## Windows (CafeLeBlanc)

Install Python 3 and configure ignored/private `tools/gamez.local.json`:

```json
{
  "bind": "0.0.0.0",
  "port": 47777,
  "token": "REPLACE_WITH_64_RANDOM_HEX_CHARACTERS",
  "games": {
    "1": {"exe": "D:\\Gamez\\FFVII\\ff7remake.exe"},
    "12": {"exe": "E:\\Gamez\\TEKKEN 8\\Polaris\\Binaries\\Win64\\Polaris-Win64-Shipping.exe"}
  }
}
```

The paths above are **examples only**, not discovered or verified install paths.
Generate a token with `python -c "import secrets; print(secrets.token_hex(32))"`.
Find the correct executable and working directory for each game before adding it.
Only allow TCP 47777 from the Steam Link IP in Windows Firewall. HTTP carries
the bearer token in plaintext on the LAN; do not expose this service to the
internet or untrusted networks. The bridge accepts numeric IDs only; it cannot
execute network-supplied paths or shell strings.

From PowerShell in the checkout, install the current-user startup task:

```powershell
powershell -ExecutionPolicy Bypass -File tools/install_gamez_companion.ps1
```

This starts the companion at sign-in, without elevation, and restarts it after
an unexpected exit. Its log is written beside the script as
`tools/gamez-companion.log`.

## Steam Link

After building/installing the updated package, create
`/home/apps/greenlink/gamez-bridge.conf`:

```sh
GAMEZ_HOST=192.168.10.187:47777
GAMEZ_TOKEN_FILE=/home/apps/greenlink/gamez-token
GAMEZ_SUNSHINE_APP='FlXtR Gamez'
```

Put the **same secret** in `/home/apps/greenlink/gamez-token`, then
`chmod 600 /home/apps/greenlink/gamez-token`. Neither file belongs in Git.

In Sunshine, create a **separate** application called `FlXtR Gamez` which
starts the stream without starting FFVII. Reuse the FFVII app's known-working
controller, display, VitaBar and Explorer setup, but keep FFVII untouched.
The companion starts the game **before** Moonlight opens the `FlXtR Gamez`
session. Sunshine still needs a suitable session lifecycle/cleanup configuration;
that integration is not automated by this prototype.

Only game IDs explicitly entered in `gamez.local.json` can launch. In bridge
mode FlXtR's numeric catalog ID is sent directly, so adding titles to FlXtR no
longer requires extending the old 69-title Moonlight-name switch. An unknown
ID, missing game, invalid token or an already-running game causes an error; the
Steam Link will **not** open the dummy Sunshine session. For now the error is
written to `moonlight-gamez.log`, not rendered in the Gamez UI.

The companion exposes an authenticated `GET /v1/status` endpoint for checking
whether its tracked game process is still running. The bearer token remains
restricted to the trusted LAN; HTTP is not encrypted, so do not expose the
listener beyond that network.

### Disable/rollback

Move or delete `gamez-bridge.conf` and restart FlXtR to restore its original
Sunshine application-per-game behavior. The original FFVII entry was not edited.

### Limitations

The companion does not auto-discover executables or track child-process trees,
and it does not manage crashes/Explorer restoration or confirm controller
operation. Games that launch a child process may require configuring the
long-lived game executable. This code still needs an end-to-end run on
CafeLeBlanc and the Steam Link before treating the bridge as deployed.

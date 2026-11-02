# Gamez bridge: experimental FFVII reuse

This branch includes an **opt-in prototype**, not an installed or verified
Windows/Steam Link deployment. By default, the previous per-game Moonlight
mapping is unchanged. Do not overwrite your working FFVII Sunshine registration.

## Windows (CafeLeBlanc)

Install Python 3 and run `python tools/gamez_companion.py` from the checkout.
Next to it, create ignored/private `tools/gamez.local.json`:

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

Only game IDs explicitly entered in `gamez.local.json` can launch. An unknown
ID, missing game, invalid token or an already-running game causes an error; the
Steam Link will **not** open the dummy Sunshine session. For now the error is
written to `moonlight-gamez.log`, not rendered in the Gamez UI.

### Disable/rollback

Move or delete `gamez-bridge.conf` and restart FlXtR to restore its original
Sunshine application-per-game behavior. The original FFVII entry was not edited.

### Limitations

The initial companion does not auto-discover executables, track process trees,
provide a secure encrypted channel, manage crashes/Explorer restoration, or
confirm controller operation. Games that launch a child process may require a
different executable. This code has **not** been tested on CafeLeBlanc or the
Steam Link. Do not treat it as a completed 69-game deployment.

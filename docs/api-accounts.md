# Phone setup for API accounts

Open Settings, scroll to API Accounts and press A. Scan the code with a phone
on the same home network. Follow the provider links to sign up or log in, get
an API key, return to the setup tab and paste it into the corresponding form.

- SubDL: https://subdl.com/api-doc — obtain a key from the account panel.
- TheGamesDB: https://api.thegamesdb.net/key.php — log in and follow its API
  access process; use the public API key. Access is controlled by the provider.

This is API-key setup, not an OAuth device login. FlXtR never asks for provider
passwords and cannot automatically create an account or approve API access.
The TV reports key saved, not verified; normal provider requests determine
whether the key works and has remaining quota.

The small HTTP listener binds the device's private IPv4 address on an ephemeral
port, only while this screen is open. A random 128-bit URL token protects the
form, with Host/Origin checks and no external scripts or assets. Leaving the
screen closes it; it expires after ten minutes. Press A for a fresh code.
Use a trusted home network: phone-to-device setup uses HTTP, not TLS. Guest
Wi-Fi/client isolation may prevent reaching the device. The URL is also shown
on the TV. A PC is not needed for Steam Link setup.

Keys are atomically saved with mode 0600 in the app's writable working directory:
subdl-api-key.txt and thegamesdb.key. Existing integrations read these files;
SUBDL_API_KEY, if explicitly set in the environment, takes precedence. No key
is put in a QR code, response page, diagnostic log, package, or Git. To remove a
key, delete its local file. Reopening a library page triggers new game metadata
lookups; subtitle fallback uses the key on the next lookup.

Test: `python3 tests/api_setup_test.py`. Preview:
`SDL_VIDEODRIVER=dummy ./build/greenlink --api-accounts --screenshot build/api-accounts.bmp`.
The preview exits immediately, so its setup link is intentionally no longer live.

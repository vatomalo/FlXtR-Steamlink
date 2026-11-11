# Additional Android cores

Genesis Plus GX supports Mega Drive/Genesis (`.md`, `.gen`, `.smd`), Master
System (`.sms`) and Game Gear (`.gg`). Beetle PCE Fast supports PC Engine
cartridge games (`.pce`). Existing embedded engines and TV handoff are retained.

Both cores have SHA-256 pins for arm64-v8a and armeabi-v7a in
`android/console-cores.lock.json`. Run `scripts/prepare-console-cores.py` before
building. Generic `.bin` files remain ambiguous. Sega CD and PC Engine CD
are not added. Device audio, controls, saves and performance need validation.

The buildbot URLs are mutable: changed downloads must fail checksum verification.
Mirror verified archives with licenses and corresponding source before publishing.

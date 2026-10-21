# Desktop libretro core downloader

Windows, Linux x86-64 and Steam Deck packages include a downloader under
**Games → DOWNLOAD LIBRETRO CORES**. Select a core and press A/Enter.
Downloads go into FlXtR's writable data/cores directory. Existing cores
are preserved; the action installs missing cores rather than replacing them.

The menu includes FCEUmm, Snes9x 2005, Snes9x, Gambatte, mGBA, PicoDrive,
Genesis Plus GX, Beetle PSX, Stella, FinalBurn Neo, MAME 2003 Plus,
PPSSPP and 2048. Some cores need additional BIOS/data files supplied by the user.

## RetroArch is required

Cores are emulator libraries. Install RetroArch separately to run them:

- Windows: native RetroArch on PATH, in the usual Program Files installation,
  or the Steam RetroArch installation. A portable copy can be selected with
  FLXTR_RETROARCH set to its full retroarch.exe path.
- Linux: native retroarch on PATH, or Flatpak org.libretro.RetroArch.
- Steam Deck: the RetroArch Flatpak is supported. FlXtR grants that launch
  access to its own data folder and read-only access to the selected ROM folder.
  It does not change permanent Flatpak permissions.
- FLXTR_RETROARCH overrides automatic selection. FLXTR_RETROARCH_HOME can also
  point to a native installation directory containing retroarch/retroarch.exe.

The desktop launcher passes explicit arguments and returns to the Games library
after RetroArch exits. Settings are appended for the game without saving over
RetroArch's global configuration. FlXtR saves/states remain in its data folder.

Put ROMs in FlXtR's data/roms folder, or list additional absolute directories
(one per line) in data/games-paths.txt. Installed managed cores are used by
the importer and the per-ROM core picker. A successful core install invalidates
the cached game index so newly supported ROMs are discovered.

## Platform selection and installation checks

The helper selects the official buildbot's windows/x86_64 DLL or linux/x86_64 SO
archives. It enforces HTTPS and official-host redirects, bounded downloads,
one exact ZIP member, no traversal/symlinks, x86-64 PE/ELF headers and a separate
bounded load/API probe. Failed installs are not published. Per-core metadata
records the URL, SHA-256 and installation time. Concurrent installs are locked.

Errors appear in the UI and core-error.txt/catalog.log. If a new upstream core
cannot load on the host, the existing installation is kept. A physical Steam Deck
session and specific game/controller combinations still need user testing.

## Steam Link

The desktop downloader deliberately rejects ARM/Steam Link. The official Steam
Link documentation says its build is outside the automated build service:
https://docs.libretro.com/guides/install-steamlink/

Keep the Steam Link SDK-targeted cores and existing RetroArch installation.
scripts/build-cores.sh builds the project's pinned Steam Link SNES and arcade
cores using Valve's SDK. Generic Linux ARM or desktop x86-64 files must not be
installed over those cores. This update does not deploy changes to a Steam Link.

Official desktop core directories:
https://buildbot.libretro.com/nightly/windows/x86_64/latest/
https://buildbot.libretro.com/nightly/linux/x86_64/latest/

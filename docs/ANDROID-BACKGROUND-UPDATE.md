# Android background library update

The saved ROM library opens immediately. Folder refreshes run on the library
worker without disabling navigation and are limited to once per hour across
activity restarts. SCAN still requests a refresh; importing games also refreshes
the index. Folder grants and existing ROM files remain unchanged.

Automatic update checks no longer open download or installation dialogs.
Use the app update menu to download and install a release manually.

ROM cards download matching Libretro box artwork in the background with a
bounded disk cache and decoded image size. Artwork requires the ROM filename
(without its extension) to match the thumbnail collection, including region
suffixes. Missing artwork keeps the console placeholder; failed lookups are
retried after a day. These images are provided by the Libretro thumbnail
collection; no ROMs are downloaded or distributed by this feature.

An ARCHIVE shortcut in Games opens the existing Internet Archive media browser.
This is not a ROM download service.

This Java-only update reuses the signed previous release's native libraries and
runtime assets without modifying their binaries. Android 8 remains the minimum.

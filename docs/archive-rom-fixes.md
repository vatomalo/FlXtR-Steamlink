# Archive ROM downloads and Neo Geo

Based on the updated `6f99b71` branch, preserving its subtitle and playback changes.

Archive requests retain the complete URL instead of squeezing item ID plus filename into 128 bytes. ROMs are saved under short hashed directories grouped by source folder and system. Filenames below 192 bytes remain unchanged (important for arcade driver identification); longer names retain their extension and a hash suffix. The importer keeps the original title separately. CUE/BIN siblings from one source folder share a destination folder.

Select a ZIP with A to browse its members. The first row downloads the complete ZIP, which is appropriate for individual arcade sets. Other rows download only the selected supported member through Archive's ZIP service. No complete collection is downloaded merely to browse. Listings are bounded to 8 MiB; source URLs are restricted to Archive downloads, and decoded traversal/control-character paths are rejected. This is Archive ZIP browsing, not a general local archive manager.

Modern Neo Geo sets containing `.p1` and `.c1` ROM members are identified even inside a MAME folder. Prefer an installed FBNeo core, then the installed FBA 2012 Neo Geo core. The launcher links the user's existing `system/neogeo.zip` beside the ROM when absent, without replacing an existing BIOS file. Launch logs now include RetroArch diagnostics.

Hardware diagnosis: Last Blade failed in the installed MAME 2003 Plus core with missing legacy ROM member names. FBA 2012 Neo Geo initially could not locate the shared BIOS. After providing a neighboring link to the existing BIOS, the same game ran 120 frames and captured a screenshot. No new BIOS or commercial ROM was downloaded.

Validation: complete host suite; long filename, collision, arcade basename, disc sibling, malicious path and ZIP parsing tests; importer/core/BIOS-link tests; live Archive ZIP browsing and a 131088-byte Super Bat Puncher demo member download with a verified iNES header.

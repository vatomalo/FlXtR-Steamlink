# USB storage setup

The setup wizard and helpers are tracked together with the app and included in Steam Link builds, updates, and verified manual deployment. Run `make test-usb` for the backend and controller-navigation tests. USB execution also refreshes firmware power activity until the operation finishes.

Open **Settings → Services → USB Storage Setup** on Steam Link.

The default action is **Use / Repair Existing USB**. It discovers the selected drive's existing FAT32 and Linux partitions, restores the mounts and symbolic links, and installs an idempotent boot helper. It does not format the drive. If several USBs are attached, choose by model, capacity, serial and USB port. Do not choose by `/dev/block/sda` alone: device letters can change.

Mounts and links are the central feature:

- FAT data is visible at `/mnt/disk`.
- The Linux volume is anchored at `/mnt/flxtr-system` and bound at `/mnt/disk/.system`.
- Linux `apps` and `local` directories are bound at `/home/apps` and `/usr/local`.
- Stable aliases are bound at `/mnt/flxtr-usb/<UUID>/data` and `/mnt/flxtr-usb/<UUID>/system`.
- `/mnt/flxtr-usb/by-uuid/<UUID>` links point to those aliases. When the app has a ROM directory, `roms/USB-<FAT-UUID>` links to the FAT ROM folder.
- `/mnt/config/flxtr-usb/mounts.sh` and the new `S19zz_flxtr_usb.sh` boot hook restore this arrangement. Existing stock and recovery scripts are preserved. Firmware that resets internal configuration can still remove the hook; rerun recovery after reinstalling FlXtR.

Conflicting files, links and unrelated mounts are refused instead of overwritten. Mounted filesystem checks account for records hidden underneath later parent mounts. Duplicate filesystem UUIDs are refused. Recovery requires one FAT32 and one ext3/ext4 partition with real `apps` and `local` directories on the Linux filesystem. The current Steam Link formatter creates ext3; ext4 recovery depends on firmware support.

## Erase and prepare

**Erase and Prepare USB** destroys every partition and file on the selected drive. Back up its contents elsewhere first. It does not physically destroy the hardware, and it is not a secure erase.

The review shows the exact selected drive and resulting sizes. Cancel is selected by default. Continuing requires a randomized four-direction code and a final erase action. The backend independently revalidates the hardware identity, connection generation, partition table, plan age and active mounts. A drive containing running apps, libraries or the helper cannot be erased. Busy mounts cause a refusal; no force or lazy unmount is used. Disconnecting or replacing a reviewed drive invalidates the plan. Do not remove the drive or cut power during preparation; interrupted formatting may require starting over.

Supported preparation: USB disks with 512-byte logical sectors, at least 4 GiB and less than 2 TiB. It creates an MBR, FAT32 data partition and ext3 apps partition. The default apps allocation is one quarter of capacity, capped at 8 GiB; the CLI can select a different size. FAT32 limits individual files to less than 4 GiB.

Preparation copies the current `/home/apps` and `/usr/local` trees to the new Linux filesystem, preserving source files and symbolic links. It creates FAT folders for ROMs, videos and backups. Existing FAT-side ROMs/media are not automatically migrated. If another USB supplies the active installation, the new disk is prepared as a spare and the primary mounts remain unchanged. Launch FlXtR from internal storage with no other USB installation active to prepare and activate a new primary. A successful activation requires exiting to the Steam menu and reopening FlXtR.

## CLI

Run from the FlXtR app directory as root on Steam Link. Scanning is read-only:

```sh
sh scripts/music-runtime.sh tools/usb_setup.py scan
sh scripts/music-runtime.sh tools/usb_setup.py plan --device /dev/block/sdX --action restore --out /tmp/usb-plan.json
```

Inspect the plan before execution. `execute --plan /tmp/usb-plan.json --confirm RESTORE-XXXXXXXX` requires the exact confirmation printed by that plan. Replace `restore` with `prepare` only when you intend to erase that selected disk; it produces an `ERASE-XXXXXXXX` confirmation. Plans expire after five minutes, cannot be reused, and are invalid after reboot. `prepare --apps-gib N` is accepted on the `plan` command.

## Validation

20 backend tests cover selection among multiple disks, active-disk refusal, stale and changed identities, confirmation failures, layout geometry, failed unmounts, short writes, conflicting links, hidden mounts, startup apply failure and a complete mocked formatter flow redirected to a sparse regular-file image. ARM compilation and existing preview-service tests pass. On the live Steam Link, scanning and recovery planning pass and active-drive erase planning is refused. The picker was rendered on-device with SDL's dummy renderer; the warning screen used a separate noninteractive screenshot fixture that cannot dispatch actions. The physical destructive path has not been tested on a spare USB. No live USB was formatted during development.

# SoMUltimate

A source-controlled ROM-hacking workspace for **Secret of Mana Turbo**, integrated with the FlXtR AI proxy.

## Strategy

**Patch and annotate first; do not attempt a complete Turbo ROM reassembly as the entry point.** Preserve the known-working Turbo ROM and use narrowly scoped 65816 hooks assembled with Asar. Investigate the original weapon ring in a debugger before changing any byte. A full matching disassembly can be a later, independent milestone.

The first goal is **instant primary/secondary weapon switching with native graphics initialization**, without opening the equipment ring and without copying monster sprite graphics into weapon graphics slots.

## Layout

- `roms/`: local, user-supplied source ROMs; **gitignored**, never commit ROMs.
- `patches/`: reviewed Asar patches; do not assume unverified SNES addresses.
- `symbols/`: discovered WRAM symbols, code labels, evidence and provenance.
- `traces/`: local debugger traces, frame captures and comparison artifacts; ignored.
- `tools/`: ROM identification, patch assembly and reproducibility scripts.
- `build/`: derived ROMs, BPS patches and test outputs; ignored.
- `docs/`: workflow and research notes.

## First steps

1. Copy your **own** Secret of Mana Turbo ROM into `roms/`.
2. Run `python3 tools/identify_rom.py roms/your-turbo.smc`; record SHA-256, ROM size, copier-header status, and mapper candidate.
3. Run the *same tool* on the US v1.0 reference ROM if available. **Never assume Turbo matches vanilla addresses.**
4. Use MesenCE/Mesen2's 65816 debugger to break on writes to `7E:E1E4` during a native sword-to-boomerang equip. Record PC, return addresses, call stack, CPU M/X flags, DBR, WRAM side effects, DMA and sprite state.
5. Confirm candidate ROM routines against the exact Turbo binary. Translate addresses to the verified mapper.
6. Design a minimal Asar patch with explicit original-byte assertions, occupied-space checks, reentrancy/state guards, and a proxy-to-ROM mailbox only after its WRAM location is verified unused.
7. Test original native equip, patched B/Y switch, menus, save/load, cutscenes, and attack animations on emulator and Steam Link.

## Existing observed WRAM fields

These were observed in an actual native sword-to-boomerang transition, **not yet a full specification**:

| Address (bank 7E) | Observation |
|---|---|
| `E1E4` | Equipped weapon ID changed `01` to `06` |
| `CC55` | Sword ownership/equipment flag changed `40` to `00` |
| `CC5A` | Boomerang ownership/equipment flag changed `00` to `40` |
| `E1E3`, `E1E8` | Related weapon state changed `09` to `36` |

Do not blindly write these fields to emulate equipping: the old instant-switch attempt produced sprite corruption.

## Existing reference project

- `https://github.com/cidwel/som-decomp`: US v1.0 reconstruction with matching assembly, readable C, and documentation. **Reference material, not a Turbo-compatible drop-in build.**
- Our FlXtR proxy: `src/som_ai_proxy.c`, which contains diagnostic native-ring traces and the safe fallback that currently requires native equipment selection.

## Boundaries

Never commit original or patched game ROMs, raw copyrighted ROM data, save files, or game assets. Distribute source and patches (BPS) separately. Keep each patch reversible and anchored to verified binary hashes.

# Toolchain and workflow

## Recommended

1. **MesenCE (Mesen2 continuation)** for the 65816 debugger, code/data logging, execution/read/write breakpoints, trace logs, call stacks, memory and event viewer. For actual code discovery, this is the primary instrument.
   - https://github.com/nesdev-org/MesenCE
2. **Asar** to assemble small 65C816 patches directly against an existing ROM, with original-byte assertions.
   - https://github.com/RPGHacker/asar
3. **Python 3** for hashing ROMs, copier-header detection, binary comparison and scripting tests.
4. **Floating IPS (FLIPS)** or another trusted BPS-capable patcher, to export binary diffs rather than distribute a patched ROM.
   - https://github.com/Alcaro/Flips
5. **ca65 / ld65 (cc65)** for working with `som-decomp` and possible future code reconstruction.
   - https://github.com/cc65/cc65
   - https://github.com/cidwel/som-decomp
6. **A modern hex editor** (e.g. ImHex), and optionally Ghidra with a verified 65C816 processor extension for static analysis. The debugger is more reliable for 65816 M/X-dependent decoding.

## Why not disassemble everything?

An unannotated complete ROM disassembly also disassembles tables, compressed assets, graphics and text as if they were executable instructions. Correctly identifying code versus data and tracking 65816 accumulator/index widths can be difficult. It does not recover the original source-level identifiers or comments. Build **your own symbol map** based on observed addresses and the existing Secret of Mana reconstruction.

## Non-negotiable before any Asar patch

- Exact SHA-256 of baseline Turbo ROM; copier-header length and internal mapping verified.
- CPU address and PC-offset conversion verified against the selected ROM.
- Hook entry-point, instruction lengths, expected pre-patch opcode bytes and calling convention proven.
- Free space or expanded ROM banks independently checked against Turbo's patch occupancy.
- Full register and flag preservation, including DBR, direct page and M/X expectations; IRQ/NMI/frame safety.
- Do not overwrite an instruction midstream or run menu-only routines outside their required state.
- Track each patch's base hash, output hash, original bytes, and verified test cases.

## First debugger experiment

Start gameplay with sword equipped. Add a **write breakpoint at `7E:E1E4`** and trace original ring-based selection of boomerang. Record the instruction that actually writes the field, the preceding call stack, and its parent procedure. Repeat with another weapon to separate generic equipment initialization from specific weapon graphics loading.

The goal is **not** to make the proxy force the CPU to a guessed address. Instead, identify and safely hook the native equipment machinery in a patched ROM, with the proxy passing a requested weapon through a verified-unused, documented mailbox (or an in-ROM button handler).

#!/usr/bin/env python3
"""EXPERIMENT: put a second copy of an on-screen monster into an empty slot.

Stand near a Rabite (or any monster) so it is loaded, then:

    python3 tools/som_spawn.py clone     # copy it 32 px to the right
    python3 tools/som_spawn.py undo      # restore the snapshot taken first
    python3 tools/som_spawn.py show      # list the three monster slots

The proxy copies the 0x200-byte monster object (slots 7EE600/E800/EA00, per
cidwel/som-decomp) without calling the game's own spawner ($C0:DE3B), so
engine bookkeeping outside the slot is not updated: the copy may not draw,
move, or die cleanly. A full emulator snapshot is taken right before the
write; `undo` restores it (and loses everything since). Save normally only
after you are happy with the result. The proxy logs [som-ai] SPAWN / UNDO.
"""
import argparse
import sys
import time

from som_memory import WRAM
from som_qwen_bridge import RemoteLink

MONSTER_NAMES = {0x00: "Rabite"}


def show(link):
    raw = link.fetch_wram(False)
    if raw is None:
        raise SystemExit("no WRAM snapshot; is Turbo running with som_ai_proxy?")
    wram = WRAM(raw)
    for enemy in (wram.enemy(i) for i in range(3)):
        name = MONSTER_NAMES.get(enemy["identity"], f"monster id {enemy['identity']:#04x}")
        state = "live" if enemy["active"] else "empty/inactive"
        print(f"  slot {enemy['slot']} (7E{0xE600 + (enemy['slot'] - 1) * 0x200:04X}): "
              f"{state}, {name}, HP {enemy['hp']}/{enemy['max_hp']}, "
              f"x={enemy['x_px']} y={enemy['y_px']}")
    return wram


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("clone", "undo", "show"))
    parser.add_argument("--host", default="root@100.124.74.13")
    args = parser.parse_args()
    link = RemoteLink(args.host)
    try:
        print("before:")
        show(link)
        if args.command != "show":
            link.send_control("clone_enemy" if args.command == "clone" else "undo_spawn")
            time.sleep(0.5)  # proxy reads commands every 2 frames, WRAM every 6
            print("after:")
            show(link)
            print("Check the game log for the [som-ai] SPAWN/UNDO line.")
    finally:
        link.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

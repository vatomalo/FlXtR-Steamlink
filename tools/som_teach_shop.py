#!/usr/bin/env python3
"""Show the AI the Candy shop and how you buy one Candy.

Run on SkyLoft while Turbo is running with som_ai_proxy. You walk to the
shopkeeper and buy ONE Candy by hand. This tool:

- learns the walk (map crossings and walkable tiles), like som_teach_inn.py;
- records the buttons you press while buying (the proxy logs them; it never
  presses anything during teaching);
- finds where gold is stored by comparing the amounts you type in before and
  after the purchase.

    systemctl --user stop flxtr-som-ai.service
    python3 tools/som_teach_shop.py
    systemctl --user start flxtr-som-ai.service
"""
import argparse
import subprocess
import sys
from pathlib import Path

from som_memory import WRAM, ObservationHistory
from som_menu import (MAX_ITEM_COUNT, REPLAYABLE, candy_count, find_gold_candidates,
                      narrow_gold)
from som_qwen_bridge import SSH_BASE, RemoteLink
from som_teach_inn import STATE, ask_face, record_walk

RECORD_ON = "rm -f /tmp/flxtr-som-ai-recorded; touch /tmp/flxtr-som-ai-record"
RECORD_OFF = ("rm -f /tmp/flxtr-som-ai-record; cat /tmp/flxtr-som-ai-recorded 2>/dev/null; "
              "rm -f /tmp/flxtr-som-ai-recorded")


def remote(host, command):
    """Run one of the fixed recording commands above on the Steam Link."""
    return subprocess.run([*SSH_BASE, host, command], capture_output=True, text=True,
                          check=True, timeout=15).stdout


def ask_gold(text):
    while True:
        answer = input(text).strip().replace(",", "")
        if answer.isdigit():
            return int(answer)
        print("  please type the number of GP shown in the game")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--history-db", type=Path,
                        default=STATE / "flxtr" / "som-ai.sqlite")
    parser.add_argument("--interval", type=float, default=0.2)
    args = parser.parse_args()
    history = ObservationHistory(args.history_db)
    link = RemoteLink(args.host)
    try:
        start = WRAM(link.fetch_wram(False))
        if candy_count(start) >= MAX_ITEM_COUNT:
            raise SystemExit("you already carry four Candy; use one first so you can buy one")
        gold_before = ask_gold("How much GP do you have right now? ")
        candidates = find_gold_candidates(start, gold_before)
        print(f"  {len(candidates)} possible gold locations")

        print("\nWalk to the Candy SHOPKEEPER the normal way. Stop on the tile in front\n"
              "of them (do not talk yet), then press Enter here.\n", flush=True)
        tiles, maps = record_walk(link, history, args.interval)
        at_shop = WRAM(link.fetch_wram(False)).observe()
        context = at_shop.get("tile_context") or {}
        if not context.get("runtime_verified") or context.get("game_mode") != "field":
            raise SystemExit("cannot verify the tile under the hero here; step once and retry")
        face = ask_face("Which side is the shopkeeper on?")
        candy_before = candy_count(WRAM(link.fetch_wram(False)))

        remote(args.host, RECORD_ON)
        input("\nNow buy exactly ONE Candy by hand: talk, buy, and close every dialog\n"
              "until you can walk again. Then press Enter here... ")
        buttons = remote(args.host, RECORD_OFF).split()
        print(f"  recorded {len(buttons)} presses: {' '.join(buttons)}")
        if not buttons or buttons[0] != "confirm":
            raise SystemExit("the first press should be Confirm (talk); please retry")
        unsupported = sorted(set(buttons) - REPLAYABLE)
        if unsupported:
            raise SystemExit(f"cannot replay {unsupported}; buy without Start/Select")

        after = WRAM(link.fetch_wram(False))
        if after.game_mode() != "field":
            raise SystemExit("still in a dialog; close it and rerun")
        if candy_count(after) != candy_before + 1:
            raise SystemExit(f"Candy went {candy_before} -> {candy_count(after)}, expected +1")
        gold_after = ask_gold("How much GP do you have now? ")
        if gold_after >= gold_before:
            raise SystemExit("gold did not go down")
        found = narrow_gold(candidates, after, gold_after)
        addresses = {address for address, _ in found}
        if len(addresses) != 1:
            raise SystemExit(f"gold location still ambiguous ({len(found)} matches); "
                             "rerun after earning or spending some GP")
        address = addresses.pop()
        width = 3 if (address, 3) in found else 2
        gold = {"address": address, "width": width}
        shop = history.teach_shop(at_shop, face, buttons, gold_before - gold_after, gold)
        print(f"\nSaved shop: map {shop['map_id']} tile {shop['target']} facing "
              f"{face.removeprefix('step_')}, Candy costs {shop['price']} GP, "
              f"gold at {address:#08x}. Learned {tiles} tile observations on "
              f"{len(maps)} map(s).")
        print("Restart the AI: systemctl --user start flxtr-som-ai.service")
    finally:
        try:
            remote(args.host, "rm -f /tmp/flxtr-som-ai-record")
        except (OSError, subprocess.SubprocessError):
            pass
        link.close()
        history.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

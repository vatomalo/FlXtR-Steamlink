#!/usr/bin/env python3
"""Learn the Turbo item-ring cursor byte once, interactively, on SkyLoft.

Stop the AI service first. You press the controller buttons; this tool only
reads WRAM over SSH and, in the last step, presses Open Ring and Cancel to
check which buttons work. Result: ~/.config/flxtr/som-ring.json.

    systemctl --user stop flxtr-som-ai.service
    python3 tools/som_ring_probe.py
"""
import argparse
import json
import sys
import time
from pathlib import Path

from som_memory import WRAM
from som_menu import (DEFAULT_CALIBRATION, RING_KIND, Calibration, candy_slot,
                      find_cursor_candidates, ring_open)
from som_qwen_bridge import RemoteLink


def snapshot(link):
    raw = link.fetch_wram(False)
    if raw is None:
        raise SystemExit("no WRAM snapshot; is Turbo running with som_ai_proxy?")
    return WRAM(raw)


def prompt(text):
    input(f"\n{text}\n  then press Enter here... ")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--rotations", type=int, default=4)
    parser.add_argument("--output", type=Path, default=DEFAULT_CALIBRATION)
    args = parser.parse_args()
    link = RemoteLink(args.host)
    try:
        prompt("1. In the field, with all menus closed and Candy in your items,")
        field = snapshot(link)
        if field.game_mode() != "field":
            raise SystemExit(f"game mode is {field.game_mode()}, expected field")
        slot = candy_slot(field)
        if slot is None:
            raise SystemExit("no verified Candy in the inventory")
        items = len([item for item in field.inventory() if item["verified"]])
        print(f"   Candy is inventory slot {slot}; {items} item types in the ring.")

        prompt("2. Open the ring menu on the ITEM ring (the one with Candy)")
        opened = snapshot(link)
        if not ring_open(opened):
            raise SystemExit("ring-open byte 7E007D is not 1; is the ring open?")
        kind = opened.u8(RING_KIND)
        snaps = [opened]
        for turn in range(args.rotations):
            prompt(f"3.{turn + 1} Rotate the ring ONE item to the RIGHT")
            snaps.append(snapshot(link))
        candidates = find_cursor_candidates(snaps, items)
        print(f"   cursor candidates: {[(hex(a), s) for a, s in candidates][:8]}")
        if len(candidates) != 1:
            raise SystemExit("need exactly one candidate; rerun with --rotations 6")
        address, step = candidates[0]

        prompt("4. Rotate until Candy is selected (do not press confirm)")
        candy_view = snapshot(link)
        candy_cursor = candy_view.u8(address)
        print(f"   Candy cursor value {candy_cursor} at {address:#08x}")

        prompt("5. Close the ring menu and stand still. The tool will now press "
               "Open Ring and Cancel itself")
        open_action = None
        for action in ("open_ring", "menu_x"):
            link.send_action(action)
            time.sleep(0.5)
            if ring_open(snapshot(link)):
                open_action = action
                break
        if open_action is None:
            raise SystemExit("neither Y (open_ring) nor X (menu_x) opened the ring")
        cancel_action = None
        for action in ("cancel", "open_ring", "menu_x", "confirm"):
            link.send_action(action)
            time.sleep(0.5)
            if not ring_open(snapshot(link)):
                cancel_action = action
                break
        if cancel_action is None:
            raise SystemExit("could not close the ring; close it by hand")
        calibration = Calibration({
            "open_action": open_action, "cancel_action": cancel_action,
            "confirm_action": "confirm", "item_ring_kind": kind,
            "cursor_address": address, "cursor_step": step,
            "rotate_action": "step_right", "candy_cursor": candy_cursor,
            "candy_slot": slot})
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(calibration.to_json(), indent=2) + "\n")
        print(f"\nSaved {args.output}: open={open_action} cancel={cancel_action} "
              f"item ring kind={kind}")
        print("Restart the AI: systemctl --user start flxtr-som-ai.service")
    finally:
        link.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Show the AI the way to the inn by walking there yourself.

Run on SkyLoft while Turbo is running with som_ai_proxy. This tool only
reads WRAM: it never presses buttons. While you walk, every map crossing and
the walkable tiles around you go into the same memory the AI plans with.
When you stand on the tile in front of the innkeeper, press Enter here and
say which side the innkeeper is on.

    systemctl --user stop flxtr-som-ai.service
    python3 tools/som_teach_inn.py
    systemctl --user start flxtr-som-ai.service
"""
import argparse
import os
import select
import sys
import time
from pathlib import Path

from som_memory import WRAM, ObservationHistory
from som_qwen_bridge import RemoteLink

STATE = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local" / "state"))
FACES = {"u": "step_up", "d": "step_down", "l": "step_left", "r": "step_right"}


def walked(previous, current):
    """The direction the hero moved between two verified observations."""
    if previous is None or current is None:
        return None
    dx = current[0] - previous[0]
    dy = current[1] - previous[1]
    if not dx and not dy:
        return None
    if abs(dx) >= abs(dy):
        return "step_right" if dx > 0 else "step_left"
    return "step_down" if dy > 0 else "step_up"


def record_walk(link, history, interval):
    """Learn the player's walk until Enter is pressed; returns (tiles, maps)."""
    last_pixels, last_map, last_action, tiles, maps = None, None, None, 0, set()
    while True:
        raw = link.fetch_wram(False)
        if raw is None:
            raise SystemExit("no WRAM snapshot; is Turbo running with som_ai_proxy?")
        state = WRAM(raw).observe()
        context = state.get("tile_context") or {}
        # Every tile stood on counts, even where the tile-ID cross-check
        # fails (bridges): that is exactly what the AI cannot learn alone.
        if history.remember_walk(state):
            tiles += 1
        if context.get("runtime_verified"):
            actor = context["actor"]
            pixels = (actor["x_px"], actor["y_px"])
            pixels_before = last_pixels if context["map_id"] == last_map else None
            action = walked(pixels_before, pixels) or last_action
            # Store the direction walked to reach this sample; at a map
            # crossing that is the direction that led through the exit.
            history.record(state, action)
            history.remember_position(state)
            tiles += history.remember_world(state)
            if context["map_id"] != last_map:
                maps.add(context["map_id"])
                print(f"  on map {context['map_id']} at tile {context['actor_tile']}",
                      flush=True)
            last_pixels, last_map, last_action = pixels, context["map_id"], action
        if select.select([sys.stdin], [], [], interval)[0]:
            sys.stdin.readline()
            return tiles, maps


def ask_face(prompt="Which side is the innkeeper on?"):
    face = None
    while face is None:
        answer = input(f"{prompt} [u]p/[d]own/[l]eft/[r]ight: ")
        face = FACES.get(answer.strip().lower()[:1])
    return face


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--history-db", type=Path,
                        default=STATE / "flxtr" / "som-ai.sqlite")
    parser.add_argument("--interval", type=float, default=0.2)
    args = parser.parse_args()
    args.history_db.parent.mkdir(parents=True, exist_ok=True)
    history = ObservationHistory(args.history_db)
    link = RemoteLink(args.host)
    print("Walk from where you are to the INNKEEPER, taking the normal way.\n"
          "Stop on the tile right in front of them (do not talk yet),\n"
          "then press Enter here.\n", flush=True)
    try:
        tiles, maps = record_walk(link, history, args.interval)
        final = WRAM(link.fetch_wram(False)).observe()
        context = final.get("tile_context") or {}
        if not context.get("runtime_verified") or context.get("game_mode") != "field":
            raise SystemExit("cannot verify the tile under the hero here; step once and retry")
        print(f"\nYou are on map {context['map_id']} tile {context['actor_tile']}.")
        face = ask_face()
        anchor = history.teach_inn(final, face)
        print(f"Saved inn: map {anchor['map_id']} tile {anchor['target']} facing "
              f"{face.removeprefix('step_')}. Learned {tiles} tile observations on "
              f"{len(maps)} map(s).")
        print("Restart the AI: systemctl --user start flxtr-som-ai.service")
    finally:
        link.close()
        history.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

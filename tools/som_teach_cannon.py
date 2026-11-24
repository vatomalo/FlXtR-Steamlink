#!/usr/bin/env python3
"""Show the AI a cannon, then let it ride once to learn where it lands.

Walk to the cannon crew yourself (the walk is learned like som_teach_inn.py),
stop on the tile in front of the NPC, press Enter here and say which side
the NPC is on. With --ride the tool then talks to them and presses Confirm
(B) until the hero lands, as on the inn, and stores the landing map.
Confirm takes each box's default: Yes to 50 GP and the first destination.

    systemctl --user stop flxtr-som-ai.service
    python3 tools/som_teach_cannon.py --ride
    systemctl --user start flxtr-som-ai.service
"""
import argparse
import sys
from pathlib import Path

from som_memory import WRAM, ObservationHistory
from som_menu import CannonMacro, MacroFailed
from som_qwen_bridge import RemoteLink
from som_teach_inn import STATE, ask_face, record_walk


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--history-db", type=Path,
                        default=STATE / "flxtr" / "som-ai.sqlite")
    parser.add_argument("--interval", type=float, default=0.2)
    parser.add_argument("--ride", action="store_true",
                        help="ride once after teaching and record the landing map")
    args = parser.parse_args()
    args.history_db.parent.mkdir(parents=True, exist_ok=True)
    history = ObservationHistory(args.history_db)
    link = RemoteLink(args.host)
    print("Walk to the CANNON crew, stop on the tile right in front of them\n"
          "(do not talk yet), then press Enter here.\n", flush=True)
    try:
        record_walk(link, history, args.interval)
        final = WRAM(link.fetch_wram(False)).observe()
        context = final.get("tile_context") or {}
        if not context.get("runtime_verified") or context.get("game_mode") != "field":
            raise SystemExit("cannot verify the tile under the hero here; step once and retry")
        print(f"\nYou are on map {context['map_id']} tile {context['actor_tile']}.")
        anchor = history.teach_cannon(final, ask_face("Which side is the cannon NPC on?"))
        print(f"Saved cannon: map {anchor['map_id']} tile {anchor['target']} facing "
              f"{anchor['face'].removeprefix('step_')}.")
        if args.ride:
            macro = CannonMacro(link, anchor)
            try:
                flight = macro.run()
            except MacroFailed as error:
                raise SystemExit(f"ride failed: {error} "
                                 f"(heard {len(getattr(macro, 'transcript', []))} line(s))")
            history.record_cannon_flight(flight)
            print(f"Landed on map {flight['to_map']} at {flight['tile']}.")
    finally:
        link.close()
        history.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

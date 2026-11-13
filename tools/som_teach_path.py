#!/usr/bin/env python3
"""Show the AI a path it will not take on its own (bridges, ramps, doors).

The AI only steps on tiles it can verify as plainly open. Bridges switch
layers and read as "conditional", so it never crosses them by itself. Walk
across once while this runs; it only reads WRAM. Afterwards:

- every tile you stood on is walkable for the AI, whatever the grid says;
- the collision signature of each conditional tile you crossed is learned,
  so other bridges built from the same tiles become walkable on any map.

    systemctl --user stop flxtr-som-ai.service
    python3 tools/som_teach_path.py
    systemctl --user start flxtr-som-ai.service
"""
import argparse
import sys
from pathlib import Path

from som_memory import ObservationHistory
from som_qwen_bridge import RemoteLink
from som_teach_inn import STATE, record_walk


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--history-db", type=Path,
                        default=STATE / "flxtr" / "som-ai.sqlite")
    parser.add_argument("--interval", type=float, default=0.15)
    args = parser.parse_args()
    args.history_db.parent.mkdir(parents=True, exist_ok=True)
    history = ObservationHistory(args.history_db)
    link = RemoteLink(args.host)
    before = history.passable_signatures()
    before_cuts = history.cuttable_signatures()
    print("Walk the path (e.g. across the bridge and a few steps beyond).\n"
          "To teach grass cutting, stand next to grass, face it and attack it.\n"
          "Press Enter here when done.\n", flush=True)
    try:
        tiles, maps = record_walk(link, history, args.interval)
        learned = history.passable_signatures() - before
        cuts = history.cuttable_signatures() - before_cuts
        print(f"Learned {len(cuts)} new cuttable tile type(s): {sorted(cuts)}")
        print(f"Learned {tiles} tile observations on {len(maps)} map(s) and "
              f"{len(learned)} new walkable tile type(s): {sorted(learned)}")
        if not learned:
            print("No new conditional tile types; the walked tiles themselves are "
                  "still remembered as walkable.")
        print("Restart the AI: systemctl --user start flxtr-som-ai.service")
    finally:
        link.close()
        history.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

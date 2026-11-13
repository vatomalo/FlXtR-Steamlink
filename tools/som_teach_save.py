#!/usr/bin/env python3
"""Show the AI where and how you save the game.

Run on SkyLoft while Turbo is running with som_ai_proxy. You walk to the place
where you save (for example the innkeeper) and save once by hand, into the
slot you want the AI to use. This tool:

- learns the walk (map crossings and walkable tiles), like som_teach_inn.py;
- records the buttons you press while saving (the proxy only logs them; it
  never presses anything during teaching);
- records the dialogue pages shown, so the AI can check that a replayed save
  reached the same final page you did.

The AI replays exactly your presses from the same tile. If the game puts the
cursor on a different slot later, the final-page check still passes, so save
into the slot the game offers by default (normally the one you loaded).

    systemctl --user stop flxtr-som-ai.service
    python3 tools/som_teach_save.py
    systemctl --user start flxtr-som-ai.service
"""
import argparse
import sys
from pathlib import Path

from som_memory import WRAM, ObservationHistory
from som_menu import REPLAYABLE
from som_play_loop import PageRecorder
from som_qwen_bridge import RemoteLink
from som_teach_inn import STATE, ask_face, record_walk
from som_teach_shop import RECORD_OFF, RECORD_ON, remote


def teach_save(history, state, face, buttons, pages):
    """Validate and store the taught save; returns the stored dict."""
    if not buttons or buttons[0] != "confirm":
        raise ValueError("the first press should be Confirm (talk)")
    unsupported = sorted(set(buttons) - REPLAYABLE)
    if unsupported:
        raise ValueError(f"cannot replay {unsupported}; save without Start/Select")
    if not pages:
        raise ValueError("no dialogue pages were seen while saving")
    position = history._verified_position(state, None)
    if position is None:
        raise ValueError("the tile under the hero is not runtime-verified")
    save = {"map_id": position["map_id"], "layer": position["layer"],
            "target": position["target"], "face": face, "buttons": list(buttons),
            "pages": list(pages), "final_page": pages[-1], "taught": True}
    history.set_agent_state("save", save)
    history.remember_place(save["map_id"], save["layer"], *save["target"],
                           "save", "taught save point", "player_demonstration")
    return save


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--history-db", type=Path, default=STATE / "flxtr" / "som-ai.sqlite")
    parser.add_argument("--interval", type=float, default=0.2)
    args = parser.parse_args()
    history = ObservationHistory(args.history_db)
    link = RemoteLink(args.host)
    try:
        print("\nWalk to where you SAVE (e.g. the innkeeper) the normal way. Stop on the\n"
              "tile in front of them (do not talk yet), then press Enter here.\n", flush=True)
        record_walk(link, history, args.interval)
        at_save = WRAM(link.fetch_wram(False)).observe()
        context = at_save.get("tile_context") or {}
        if not context.get("runtime_verified") or context.get("game_mode") != "field":
            raise SystemExit("cannot verify the tile under the hero here; step once and retry")
        face = ask_face("Which side is the person you save with on?")

        remote(args.host, RECORD_ON)
        with PageRecorder(lambda: WRAM(link.fetch_wram(False))) as recorder:
            input("\nNow SAVE by hand: talk, save, and close every dialog until you can\n"
                  "walk again. Then press Enter here... ")
        buttons = remote(args.host, RECORD_OFF).split()
        print(f"  recorded {len(buttons)} presses: {' '.join(buttons)}")
        print(f"  saw {len(recorder.pages)} pages; last: {recorder.pages[-1:]}")
        if WRAM(link.fetch_wram(False)).game_mode() != "field":
            raise SystemExit("still in a dialog; close it and rerun")
        try:
            save = teach_save(history, at_save, face, buttons, recorder.pages)
        except ValueError as exc:
            raise SystemExit(f"not saved: {exc}; please retry")
        print(f"\nSaved save point: map {save['map_id']} tile {save['target']} facing "
              f"{face.removeprefix('step_')}, final page {save['final_page']!r}.")
        print("Restart the AI: systemctl --user start flxtr-som-ai.service")
    finally:
        try:
            remote(args.host, "rm -f /tmp/flxtr-som-ai-record")
        except Exception:
            pass
        link.close()
        history.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

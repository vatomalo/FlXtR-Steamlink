"""Deterministic play loop: hunt -> shop -> inn -> save -> hunt.

Used to soak-test game edits with a hero that actually plays. Qwen does not
pick goals while the loop runs; each phase maps onto goals the planner and
the existing closed-loop macros already carry out:

- hunt: travel to the hunting map (best gold per HP lost in the reward
  ledger, or --hunt-map) and fight there. The trip ends when it has earned
  `trip_gold`, HP drops below 60 %, or after `hunt_decisions` decisions
  (travel included). Shop, inn and save each give up after
  ERRAND_DECISIONS so a route that never arrives cannot stall the loop.
- shop: buy Candy (ShopMacro) while the shop errand is available.
- inn: rest (InnMacro); skipped when HP is already full, because the inn
  macro rejects a full-HP stay and would disable the inn for the run.
- save: replay the save the player taught with tools/som_teach_save.py and
  verify that the taught final page appeared; skipped (logged once) when no
  save is taught, so a save slot is never written by guesswork.

Survival (HP at or below half) stays with the planner, which retreats to the
inn by itself; a successful inn stay from any cause advances the loop to
saving. The phase is persisted in agent_state "play_loop".
"""
import threading
import time

from som_dialogue import GLYPHS
from som_game_over import deaths_by_map
from som_menu import REPLAYABLE, InnMacro, MacroFailed

PHASES = ("hunt", "shop", "inn", "save")
TRIP_GOLD = 100
HUNT_DECISIONS = 3000
ERRAND_DECISIONS = 1500   # shop/inn/save travel that never arrives moves on
NEXT = {"hunt": "shop", "shop": "inn", "inn": "save", "save": "hunt"}
LEAVE_HP_TENTHS = 6
# HP a game over on a map counts as when ranking hunting maps.
DEATH_HP_COST = 500


def page_text(wram):
    """Decoded visible dialogue text, or None."""
    if wram.game_mode() != "event":
        return None
    raw = bytes(wram.u8(0x7EA1A4 + i) for i in range(33)).split(b"\0", 1)[0]
    if not raw or any(c not in GLYPHS for c in raw):
        return None
    text = " ".join("".join(GLYPHS[c] for c in raw).split())
    return text if any(c.isalpha() for c in text) else None


def best_hunt_map(history, reachable, exclude=()):
    """Map with the most gold per HP lost that ever paid out, or None."""
    if history is None:
        return None
    best = None
    deaths = deaths_by_map(history.connection)
    for map_id, gold, hp_lost in history.connection.execute(
            "SELECT map_id,SUM(gold_gained),SUM(hp_lost) FROM reward_ledger GROUP BY map_id"):
        if not gold or gold <= 0 or map_id in exclude or not reachable(map_id):
            continue
        rate = gold / (1 + (hp_lost or 0) + DEATH_HP_COST * deaths.get(map_id, 0))
        if best is None or rate > best[0]:
            best = (rate, map_id)
    return best[1] if best else None


class PlayLoop:
    def __init__(self, history, hunt_map=None, trip_gold=TRIP_GOLD,
                 hunt_decisions=HUNT_DECISIONS, log=print):
        self.history, self.fixed_hunt_map = history, hunt_map
        self.trip_gold, self.hunt_decisions, self.log = trip_gold, hunt_decisions, log
        state = history.get_agent_state("play_loop") if history is not None else None
        self.state = state if isinstance(state, dict) and state.get("phase") in PHASES else \
            {"phase": "hunt", "trip_gold": None, "steps": 0, "cycles": 0}
        self.warned = set()

    @property
    def phase(self):
        return self.state["phase"]

    def _save_state(self):
        if self.history is not None:
            self.history.set_agent_state("play_loop", self.state)

    def _enter(self, phase, why):
        if phase == "hunt":
            self.state["trip_gold"] = None
            self.state["cycles"] = self.state.get("cycles", 0) + 1
        self.state["phase"], self.state["steps"] = phase, 0
        self._save_state()
        self.log(f"[loop] -> {phase}: {why}")

    def _warn_once(self, key, text):
        if key not in self.warned:
            self.warned.add(key)
            self.log(text)

    def goal(self, summary, shop_ready, inn_ready, save_ready, reachable):
        """Goal for this decision: hunt_map_N, hunt_here, go_to_shop,
        go_to_inn or go_to_save. Advances the phase when its work is done."""
        hp, max_hp = summary.get("hp") or 0, summary.get("max_hp") or 0
        gold, here = summary.get("gold"), summary.get("map_id")
        if self.cornered(summary, reachable):
            # No way out to the inn/shop from here (boss arena, story pit):
            # fight where we are; the phase resumes once a route exists.
            self._warn_once(("cornered", here), f"[loop] no learned route from map {here} "
                            "to the inn; fighting here")
            return "hunt_here"
        for _ in PHASES:                       # fall through skipped phases
            phase = self.phase
            self.state["steps"] = self.state.get("steps", 0) + 1
            if self.state["steps"] % 100 == 0:
                self._save_state()
            if phase != "hunt" and self.state["steps"] > ERRAND_DECISIONS:
                self._enter(NEXT[phase], f"{phase} errand did not finish in "
                            f"{ERRAND_DECISIONS} decisions")
                continue
            if phase == "hunt":
                if self.state.get("trip_gold") is None and isinstance(gold, int):
                    self.state["trip_gold"] = gold
                    self._save_state()
                earned = (gold - self.state["trip_gold"]
                          if isinstance(gold, int) and self.state.get("trip_gold") is not None
                          else 0)
                target = self.fixed_hunt_map or best_hunt_map(self.history, reachable)
                if max_hp and hp * 10 < max_hp * LEAVE_HP_TENTHS:
                    self._enter("shop", f"HP {hp}/{max_hp}, earned {earned} GP")
                    continue
                if earned >= self.trip_gold:
                    self._enter("shop", f"earned {earned} GP this trip")
                    continue
                if self.state["steps"] >= self.hunt_decisions:
                    self._enter("shop", f"hunted {self.state['steps']} decisions, "
                                f"earned {earned} GP")
                    continue
                if target is None:
                    self._warn_once("no_hunt_map", "[loop] no hunting map with gold in "
                                    "memory yet; hunting where the hero is")
                    return "hunt_here"
                return "hunt_here" if here == target else f"hunt_map_{target}"
            if phase == "shop":
                if shop_ready:
                    return "go_to_shop"
                self._enter("inn", "nothing (more) to buy")
                continue
            if phase == "inn":
                if not inn_ready:
                    self._warn_once("no_inn", "[loop] inn not taught or disabled; skipping rest")
                    self._enter("save", "no usable inn")
                    continue
                if max_hp and hp >= max_hp:
                    self._enter("save", "HP already full; no rest needed")
                    continue
                return "go_to_inn"
            if phase == "save":
                if not save_ready:
                    self._warn_once("no_save", "[loop] no save taught (run "
                                    "tools/som_teach_save.py); not saving")
                    self._enter("hunt", "save skipped")
                    continue
                return "go_to_save"
        return "hunt_here"

    @staticmethod
    def cornered(summary, reachable):
        inn_map, here = summary.get("inn_map"), summary.get("map_id")
        return (inn_map is not None and here is not None and here != inn_map and
                not reachable(inn_map))

    # Macro outcomes ------------------------------------------------------
    def inn_done(self):
        self._enter("save", "rested at the inn")

    def inn_failed(self, why):
        self._enter("save", f"inn failed: {why}")

    def save_done(self):
        self._enter("hunt", "game saved")

    def save_failed(self, why):
        self._enter("hunt", f"save failed: {why}")


class SaveMacro(InnMacro):
    """Replay the player's taught save and verify its final page appeared."""

    GAP = 0.5

    def ready(self, wram):
        save = self.anchor if isinstance(self.anchor, dict) else {}
        buttons = save.get("buttons")
        if not buttons or any(b not in REPLAYABLE for b in buttons) or not save.get("final_page"):
            return "no verified save taught; run tools/som_teach_save.py"
        if wram.game_mode() != "field":
            return "not in field mode"
        if self._tile(wram) != (save["map_id"], save["layer"], *save["target"]):
            return "not on the taught save tile"
        return None

    def run(self):
        start = self._read()
        why = self.ready(start)
        if why:
            raise MacroFailed(why)
        before = self._tile(start)
        self.link.send_action(self.anchor["face"])
        self.sleep(0.3)
        if self._tile(self._read()) != before:
            raise MacroFailed("facing tap moved the hero; save point is not on that side")
        seen = []
        try:
            for index, button in enumerate(self.anchor["buttons"]):
                self.link.send_action(button)
                self.sleep(self.GAP)
                wram = self._read()
                if index == 0 and wram.game_mode() == "field":
                    raise MacroFailed("save dialog did not start")
                text = page_text(wram)
                if text and (not seen or seen[-1] != text):
                    seen.append(text)
            deadline = self.clock() + 3.0
            while self._read().game_mode() != "field":
                if self.clock() >= deadline:
                    raise MacroFailed("still in a dialog after the taught save")
                self.sleep(self.POLL)
        except MacroFailed:
            self._leave_dialog()
            raise
        if self.anchor["final_page"] not in seen:
            raise MacroFailed(f"taught final page {self.anchor['final_page']!r} not seen "
                              f"(saw {seen[-3:]}); reteach the save")
        return seen


class PageRecorder:
    """Collect distinct dialogue pages from live WRAM while the player acts."""

    def __init__(self, fetch, interval=0.15):
        self.fetch, self.interval = fetch, interval
        self.pages, self._stop = [], threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        while not self._stop.is_set():
            try:
                text = page_text(self.fetch())
            except Exception:              # transient SSH hiccup: keep polling
                text = None
            if text and (not self.pages or self.pages[-1] != text):
                self.pages.append(text)
            time.sleep(self.interval)

    def __enter__(self):
        self._thread.start()
        return self

    def __exit__(self, *exc):
        self._stop.set()
        self._thread.join(5)

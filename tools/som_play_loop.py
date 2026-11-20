"""Deterministic play loop with durable return-to-inn-and-save commitment.

Used to soak-test game edits with a hero that actually plays. Qwen does not
pick goals while the loop runs; each phase maps onto goals the planner and
the existing closed-loop macros already carry out:

- hunt: travel to the hunting map (best gold per HP lost in the reward
  ledger, or --hunt-map) and fight there. The trip ends when it has earned
  `trip_gold`, HP drops below 60 %, or after `hunt_decisions` decisions
  (travel included). Once the return trip starts, it remains committed until
  a verified save succeeds; fighting may interrupt movement but not erase it.
- shop: buy Candy (ShopMacro) while the shop errand is available.
- inn: rest (InnMacro); skipped when HP is already full, because the inn
  macro rejects a full-HP stay and would disable the inn for the run.
- save: replay the save the player taught with tools/som_teach_save.py and
  verify that the taught final page appeared. An untaught/failed save is
  blocked rather than being silently counted as complete, unless the inn
  saves (inn_saves): then a verified inn stay is the save, and with no taught
  save the phase hands back to hunting.

Survival (HP at or below half) stays with the planner, which retreats to the
inn by itself; a successful inn stay from any cause advances the loop to
saving. The phase is persisted in agent_state "play_loop".
"""
import threading
import time

from som_dialogue import GLYPHS
from som_frontier import Frontier
from som_game_over import deaths_by_map
from som_menu import REPLAYABLE, InnMacro, MacroFailed

PHASES = ("hunt", "shop", "inn", "save")
TRIP_GOLD = 100
HUNT_DECISIONS = 3000
STORY_LEAVE = 0.5                # a story trip turns back below this HP fraction
STORY_BUDGET = 3                 # a story trip may take this many hunting budgets
ERRAND_DECISIONS = 1500   # shop alone may time out; inn/save never expire
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
                 hunt_decisions=HUNT_DECISIONS, log=print, inn_saves=False, quest=None):
        self.history, self.fixed_hunt_map = history, hunt_map
        # The inn's own "Save?" (InnMacro save_slot) saves on every stay, so
        # without a taught save the save phase must not wait for one forever.
        self.inn_saves = inn_saves
        self.trip_gold, self.hunt_decisions, self.log = trip_gold, hunt_decisions, log
        # HP fraction that ends a hunting trip; the gambit weights move it.
        self.leave_fraction = LEAVE_HP_TENTHS / 10
        state = history.get_agent_state("play_loop") if history is not None else None
        self.state = state if isinstance(state, dict) and state.get("phase") in PHASES else \
            {"phase": "hunt", "trip_gold": None, "steps": 0, "cycles": 0}
        self.warned = set()
        self.frontier = Frontier(history, log) if history is not None else None
        self.quest = quest            # som_quest.Quest: story / level-up trips first
        # The durable objective survives game resets and tactical interruptions.
        # Phase is persisted; the short-term tactical decision is not.

    @property
    def phase(self):
        return self.state["phase"]

    def _save_state(self):
        if self.history is not None:
            self.history.set_agent_state("play_loop", self.state)

    def _enter(self, phase, why):
        if phase == "hunt":
            self.state["trip_gold"] = None
            self.state["target"] = None     # chosen afresh for each trip
            self.state["target_checked"] = False
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
            if phase == "shop" and self.state["steps"] > ERRAND_DECISIONS:
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
                target = self.fixed_hunt_map or self._trip_target(summary, reachable)
                # A story trip is not over when the purse is full (it turned
                # back to the Potos inn with 108 GP, 2026-11-20); only HP or
                # a longer decision budget end it. It turns back at half HP:
                # leaving at a quarter, he died on the walk back every trip.
                story = bool(self.state.get("target_story")) and not self.fixed_hunt_map
                leave = max(self.leave_fraction, STORY_LEAVE) if story else self.leave_fraction
                if max_hp and hp < max_hp * leave:
                    self._enter("shop", f"HP {hp}/{max_hp}, earned {earned} GP")
                    continue
                if earned >= self.trip_gold and not story:
                    self._enter("shop", f"earned {earned} GP this trip")
                    continue
                budget = self.hunt_decisions * (STORY_BUDGET if story else 1)
                if self.state["steps"] >= budget:
                    if (target is not None and target != here and
                            not self.state.get("arrived") and self.frontier is not None):
                        self.frontier.failed(target, f"{self.state['steps']} decisions")
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
                # Full HP first: an inn stay that healed him but then failed
                # at the save prompt left him waiting for the inn at 146/146
                # HP, wandering the Potos exit (2026-11-20).
                if max_hp and hp >= max_hp:
                    self._enter("save", "HP already full; no rest needed")
                    continue
                if not inn_ready:
                    self._warn_once("no_inn", "[loop] inn unavailable; committed "
                                    "return objective remains active")
                    return "await_inn_setup"
                return "go_to_inn"
            if phase == "save":
                if not save_ready and self.inn_saves:
                    self._enter("hunt", "no taught save; the inn saves on each stay")
                    continue
                if not save_ready:
                    self._warn_once("no_save", "[loop] save unavailable: teach/repair "
                                    "save (tools/som_teach_save.py); commitment held")
                    return "await_save_setup"
                return "go_to_save"
        return "hunt_here"

    def _trip_target(self, summary, reachable):
        """This trip's hunting map: the quest's story or level-up map
        (som_quest), else a new or still-useful place (som_frontier), else
        the best gold per HP lost. Kept for the whole trip."""
        here = summary.get("map_id")
        if "target" not in self.state or (self.state["target"] is None and
                                          not self.state.get("target_checked")):
            pick = self.quest.choose(summary, reachable) if self.quest is not None else None
            if pick is None and self.frontier is not None:
                pick = self.frontier.choose(here, summary.get("max_hp"), reachable)
            self.state["target"] = (pick[0] if pick else
                                    best_hunt_map(self.history, reachable))
            self.state["target_story"] = bool(pick and str(pick[1]).startswith("story"))
            self.state["target_checked"] = True
            self.state["arrived"] = False
            self.state["story_checked_map"] = here
            self._save_state()
        elif (self.quest is not None and not self.state.get("target_story") and
              here is not None and self.state.get("story_checked_map") != here):
            # A story goal outranks a hunting trip already under way; asked
            # once per map entered (the route search is not free).
            self.state["story_checked_map"] = here
            pick = self.quest.choose(summary, reachable)
            if pick is not None and str(pick[1]).startswith("story") and \
                    pick[0] != self.state.get("target"):
                self.log(f"[loop] story first: map {pick[0]} replaces trip target "
                         f"{self.state.get('target')}")
                self.state.update(target=pick[0], target_story=True, arrived=False)
            self._save_state()
        target = self.state["target"]
        if target is not None and here == target and not self.state.get("arrived"):
            self.state["arrived"] = True
            self._save_state()
        return target

    @staticmethod
    def cornered(summary, reachable):
        inn_map, here = summary.get("inn_map"), summary.get("map_id")
        return (inn_map is not None and here is not None and here != inn_map and
                not reachable(inn_map))

    # Macro outcomes ------------------------------------------------------
    def inn_done(self):
        self._enter("save", "rested at the inn")

    def inn_failed(self, why):
        self._warn_once(("inn_failed", why),
                        f"[loop] inn attempt failed ({why}); retain return objective")
        self._save_state()

    def save_done(self):
        self._enter("hunt", "game saved")

    def save_failed(self, why):
        self._warn_once(("save_failed", why),
                        f"[loop] save attempt failed ({why}); do not resume hunting")
        self._save_state()


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

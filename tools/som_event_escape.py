"""Get the AI out of dialogue and event screens it would otherwise wait in forever.

The planner only acts in field mode, so any text box, shop or scripted event it
did not start itself (walking into a trigger, a talk started by an attack
press, a cutscene) used to leave it observing indefinitely.

Only inputs whose effect is already verified on live Turbo are used:
- `confirm` advances dialogue pages (InnMacro, NpcDialogue, KO respawn);
- `cancel`, repeated, backs out of shop/inn dialogs (InnMacro._leave_dialog).

Policy, per event episode (from leaving field mode until back in it):
- nothing is pressed until the screen (event state, text pointer, visible
  text) has been unchanged for SETTLE seconds, so animations and text
  printing run untouched, and presses are at least GAP seconds apart;
- a visible text page (D0 0x81/0x83/0x84) without a choice: `confirm`; if
  the page does not change after two confirms, alternate with `cancel`;
- a yes/no choice ("Save?", "Stay at the inn?", "Buy?"): only `cancel`,
  never `confirm`, so nothing is bought, saved or accepted;
- any other event state (shop menus, scripts waiting): only `cancel`;
- at most MAX_PRESSES per episode, then it stops and leaves the screen to the
  player (logged once).
"""
import time

TEXT_STATES = (0x81, 0x83, 0x84)
SETTLE = 1.0
GAP = 0.6
MAX_PRESSES = 40
CONFIRMS_BEFORE_CANCEL = 2


def screen_state(wram):
    """(fingerprint, kind) of the current event screen; kind is text/choice/other."""
    d0 = wram.u8(0x7E00D0)
    pointer = wram.u16(0x7E1D01) | wram.u8(0x7E1D03) << 16
    text = bytes(wram.u8(0x7EA1A4 + i) for i in range(33)).split(b"\0", 1)[0]
    choice = wram.u8(0x7EA170) in (0x5A, 0x5F) or bool(wram.u8(0x7E1D00) & 0x80)
    if choice:
        kind = "choice"
    elif d0 in TEXT_STATES and text:
        kind = "text"
    else:
        kind = "other"
    return (d0, pointer, text, choice), kind


class EventEscape:
    def __init__(self, clock=time.monotonic, log=print):
        self.clock, self.log = clock, log
        self.reset()

    def reset(self):
        self.fingerprint = None
        self.since = None
        self.last_press = None
        self.presses = 0
        self.same_page = 0
        self.gave_up = False

    def observe(self, wram):
        """(action, reason) to press now, or None."""
        if wram.game_mode() != "event":
            if self.presses and not self.gave_up:
                self.log(f"[escape] back in {wram.game_mode()} after {self.presses} presses")
            self.reset()
            return None
        now = self.clock()
        fingerprint, kind = screen_state(wram)
        if fingerprint != self.fingerprint:
            self.fingerprint, self.since, self.same_page = fingerprint, now, 0
            return None
        if self.gave_up:
            return None
        if self.presses >= MAX_PRESSES:
            self.gave_up = True
            self.log(f"[escape] still in an event after {MAX_PRESSES} presses; "
                     "leaving it to the player")
            return None
        if now - self.since < SETTLE:
            return None
        if self.last_press is not None and now - self.last_press < GAP:
            return None
        if kind == "text":
            action = "confirm" if self.same_page < CONFIRMS_BEFORE_CANCEL or \
                self.same_page % 2 == 0 else "cancel"
            reason = "advance dialogue page"
        elif kind == "choice":
            action, reason = "cancel", "decline choice"
        else:
            action, reason = "cancel", "back out of event screen"
        self.same_page += 1
        self.presses += 1
        self.last_press = now
        return action, f"{reason} ({self.presses}/{MAX_PRESSES})"

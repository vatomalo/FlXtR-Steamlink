"""Fighting strategy from the hero's build (its base stats).

Stats are read from the character record (+0x188..+0x18C: strength,
agility, constitution, intelligence, wisdom; verified live 2026-11-14 on
Turbo: boy 19/19/18/8/9, girl template 11/11/11/7/15, sprite 8/13/9/15/5).

A build is the stat that clearly leads (by LEAD or more); otherwise
"balanced". Each build has a profile the planner and tactic learner use:

- preferred: the tactic to use per enemy unless learned data shows it doing
  clearly worse than another (som_tactics.TacticLearner);
- anticipate_risk: predicted hit chance at which the hero reacts before a
  swing (lower = more careful);
- dodge: react to a predicted hit by stepping away instead of guarding;
- tactics: the som_tactics styles this build tries at all.
"""

LEAD = 3
STATS = ("strength", "agility", "constitution", "intelligence", "wisdom")

PROFILES = {
    # Head-on: trade blows, rarely guard.
    "brawler": {"preferred": "press", "anticipate_risk": 0.55, "dodge": False,
                "tactics": ("press", "close")},
    # Hit and run: step out after swings, sidestep predicted hits.
    "skirmisher": {"preferred": "kite", "anticipate_risk": 0.35, "dodge": True,
                   "tactics": ("kite", "press")},
    # Hold the line: guard early, then counter.
    "guardian": {"preferred": "press", "anticipate_risk": 0.25, "dodge": False,
                 "tactics": ("press", "close")},
    # Fragile: keep distance, never trade.
    "caster": {"preferred": "kite", "anticipate_risk": 0.25, "dodge": True,
               "tactics": ("kite", "press")},
    # No preference: learned per enemy.
    # Kiting (stepping back) looks like walking back and forth and scored
    # worst live, so only agile and fragile builds use it.
    "balanced": {"preferred": None, "anticipate_risk": 0.35, "dodge": False,
                 "tactics": ("press", "close")},
}


def read_stats(wram, base):
    return {name: wram.u8(base + 0x188 + offset) for offset, name in enumerate(STATS)}


def classify(stats):
    """Build name for a stats dict (missing or zero stats: balanced)."""
    if not isinstance(stats, dict) or not all(isinstance(stats.get(s), int) for s in STATS):
        return "balanced"
    if not any(stats[s] for s in STATS):
        return "balanced"
    mind = max(stats["intelligence"], stats["wisdom"])
    body = {"brawler": stats["strength"], "skirmisher": stats["agility"],
            "guardian": stats["constitution"], "caster": mind}
    leader = max(body, key=body.get)
    others = [value for name, value in body.items() if name != leader]
    return leader if body[leader] >= max(others) + LEAD else "balanced"


def profile(build):
    return PROFILES.get(build, PROFILES["balanced"])

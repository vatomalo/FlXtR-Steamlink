"""Test link to the critic: a second model coaches the hero (SOM_AI_CRITIC=1).

The critic (a strategy master and anime / martial-arts director model) watches
the bridge journal and writes its latest verdict to CRITIC_FILE:
{"time", "coach", "tags", "verdict"}. The bridge uses it two ways:
- thinking: `coach` is one sentence added to the Qwen goal prompt;
- action: `tags` from a fixed list nudge the combat gambit weights.
Free model text never becomes a command: tags are matched against TAG_NUDGES
and the coach sentence is stripped to plain words.
"""
import json
import os
import re
import time
from pathlib import Path

CRITIC_FILE = Path.home() / ".cache" / "flxtr" / "som-critic.json"
CRITIC_MAX_AGE_S = 240     # a verdict on older play no longer applies
WEIGHT_MAX = 5             # combat gambit weights are 0-5

# tag: combat gambits raised by one while the verdict is fresh.
TAG_NUDGES = {
    "no_evade": ("evade",),            # evade > guard: blocks become cartwheel/Kawarimi
    "mashing": ("guard", "charge"),    # block then counter, charged strikes
    "reckless": ("distance", "guard"),
    "timid": ("press",),
    "idle": ("press",),
}
# no_finisher: the monk's flying kick comes back this much sooner.
FINISHER_COOLDOWN_SCALE = 0.5


def read_critic(path=None, now=None):
    if os.environ.get("SOM_AI_CRITIC") != "1":
        return None
    try:
        data = json.loads(Path(path or CRITIC_FILE).read_text())
    except (OSError, ValueError):
        return None
    if (not isinstance(data, dict) or not isinstance(data.get("time"), (int, float)) or
            (now if now is not None else time.time()) - data["time"] > CRITIC_MAX_AGE_S):
        return None
    return data


def critic_coach(data):
    """The critic's advice as one clean sentence, or None."""
    text = re.sub(r"[^A-Za-z0-9 ,.;:'()/-]", " ", str((data or {}).get("coach") or ""))
    text = re.sub(r"\s+", " ", text).strip()[:200]
    return text or None


def critic_tags(data):
    tags = (data or {}).get("tags")
    return {t for t in tags if isinstance(t, str)} if isinstance(tags, list) else set()


def nudge_style(style, tags):
    """(new style, changed gambit names). A gambit the player set to 0 stays off."""
    style = dict(style)
    changed = []
    for tag in sorted(tags):
        for name in TAG_NUDGES.get(tag, ()):
            if 0 < style.get(name, 0) < WEIGHT_MAX and name not in changed:
                style[name] += 1
                changed.append(name)
    return style, changed

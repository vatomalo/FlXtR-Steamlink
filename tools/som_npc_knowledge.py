"""Evidence-grounded NPC knowledge for the Secret of Mana AI.

The original dialogue recorder persists native, verified WRAM text. This module
reads that archive and supplies bounded hearsay to the goal director. An NPC
hint is never an executable command, a verified map edge, or proof of a quest.
"""
import re

MAX_SCAN = 80
MAX_HINTS = 4
KEYWORDS = re.compile(
    r"\b(?:go|head|travel|follow|find|seek|look|north|south|east|west|"
    r"road|path|cave|castle|temple|forest|town|village|waterfall|mana|"
    r"witch|sage|king|girl|boy|sprite|monster|weapon|sword|shop|inn|"
    r"danger|beware|rescue|save|need|must|bridge|passage|secret)\b", re.I)
DIRECTION = re.compile(r"\b(north|south|east|west)\b", re.I)
GUIDANCE = re.compile(r"\b(?:go|head|travel|follow|path|road|route|toward|"
                      r"leads?|entrance|way)\b", re.I)
NEGATION = re.compile(r"\b(?:do not|don't|never|avoid|beware|danger|"
                      r"dangerous|blocked|can't|cannot|no way)\b", re.I)


def hear_npc_clues(history, map_id=None, limit=MAX_HINTS):
    """Return recent distinct quotes with precise source information.

    No schema changes: draws on NpcDialogue's existing versioned SQLite table.
    Same-map dialogue is preferred, but older conversations remain useful after
    leaving the village. Partial pages are explicitly presented as quotes.
    """
    if history is None or not getattr(history, "connection", None):
        return []
    db = history.connection
    if not db.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='npc_dialogue_lines'"
            ).fetchone():
        return []
    rows = db.execute(
        "SELECT map_id,npc_identity,event_id,text,MAX(ts) AS seen "
        "FROM npc_dialogue_lines "
        "WHERE length(text)>0 GROUP BY map_id,npc_identity,event_id,text "
        "ORDER BY seen DESC LIMIT ?", (MAX_SCAN,)).fetchall()
    candidates = []
    for map_seen, identity, event, quote, timestamp in rows:
        if not isinstance(quote, str):
            continue
        # The native font parser limits each page to 33 characters. Keep it
        # short and do not try to reconstruct text that was never observed.
        quote = " ".join(quote.split())[:33]
        if len(quote) < 6 or not KEYWORDS.search(quote):
            continue
        directions = []
        if GUIDANCE.search(quote) and not NEGATION.search(quote):
            directions = sorted(set(m.lower() for m in DIRECTION.findall(quote)))
        candidates.append({
            "map_id": map_seen, "npc_id": identity, "event": event,
            "quote": quote, "directions": directions,
            "source": "observed NPC dialogue (unverified hint)",
        })
    # Newest within each category; local rumors first.
    same = [item for item in candidates if item["map_id"] == map_id]
    elsewhere = [item for item in candidates if item["map_id"] != map_id]
    return (same + elsewhere)[:max(0, min(int(limit), MAX_HINTS))]


def heard_directions(clues, map_id):
    """Positive headings reported locally. The planner still verifies steps."""
    directions = set()
    for item in clues or ():
        if item.get("map_id") == map_id:
            directions.update(d for d in item.get("directions", ())
                              if d in ("north", "south", "east", "west"))
    return sorted(directions)


def loop_hint_directive(summary, goal, step, previous=None, until=-1, window=120):
    """Use a recently-heard local direction briefly during the default loop.

    Does not change the play-loop's goals. Rest, shopping, saving, fights,
    and explicit map visits keep priority. A bounded trial avoids following
    an old, mistaken hint forever. Output is an allowed exploration directive.
    """
    if goal != "hunt_here" or summary.get("enemies_near"):
        return None, previous, until
    here = summary.get("map_id")
    for item in summary.get("npc_clues") or ():
        if item.get("map_id") != here or not item.get("directions"):
            continue
        direction = item["directions"][0]
        if direction not in ("north", "south", "east", "west"):
            continue
        key = (here, item.get("npc_id"), item.get("event"),
               item.get("quote"), direction)
        if key != previous:
            return "explore_" + direction, key, step + window
        if step < until:
            return "explore_" + direction, key, until
        return None, key, until
    return None, previous, until

"""Evidence-grounded NPC knowledge for the Secret of Mana AI.

The original dialogue recorder persists native, verified WRAM text, one 33
character page line at a time. This module stitches every line heard from one
villager back together in ROM order (the text pointer), reads whole sentences,
and supplies bounded hearsay to the goal director. An NPC hint is never an
executable command, a verified map edge, or proof of a quest.
"""
import re

MAX_SCAN = 400
MAX_HINTS = 4
QUOTE_CHARS = 120
RETALK_S = 1800          # a villager who taught nothing new is asked again after this
KEYWORDS = re.compile(
    r"\b(?:go|head|travel|follow|find|seek|look|north|south|east|west|"
    r"road|path|cave|castle|temple|forest|town|village|waterfall|mana|"
    r"witch|sage|king|girl|boy|sprite|monster|monsters|weapon|sword|shop|inn|"
    r"danger|beware|rescue|save|need|must|bridge|passage|secret|navel|"
    r"kingdom|commander|legend|health)\b", re.I)
DIRECTION = re.compile(r"\b(north|south|east|west)\b", re.I)
GUIDANCE = re.compile(r"\b(?:go|head|travel|follow|path|road|route|toward|towards|"
                      r"leads?|entrance|way|keep|onward|lies)\b", re.I)
NEGATION = re.compile(r"\b(?:do not|don't|never|avoid|beware|danger|"
                      r"dangerous|blocked|can't|cannot|no way)\b", re.I)
WARNING = re.compile(r"\b(?:monsters?|danger|dangerous|beware|scary|dreadful|"
                     r"run wild|earthquake|disaster|misfortune|serious)\b", re.I)
SAVE = re.compile(r"\bsave for thee\b|\bsave your\b|\bsave (?:the|thy) game\b", re.I)
REST = re.compile(r"\b(?:restore|recover)\w*\b.*\bhealth\b|\bhealth\b", re.I)
SPEAKER = re.compile(r"\b[A-Z][a-z]+:")
# Capitalised words that start sentences or are the hero, not names.
NOT_NAMES = {
    "The", "This", "That", "That's", "It", "It's", "What", "What'd", "Hey", "Oh",
    "Please", "Everyone", "Now", "Something", "Monsters", "Come", "You", "You've",
    "I", "I'm", "Shall", "Have", "Took", "Just", "Maybe", "Geez", "So",
    "There's", "Whoa", "Cut", "Shut", "Get", "Idiots", "Didn't", "An", "A", "Ooooh",
    "Kuwabara", "Thy", "Thou", "Hello", "Go", "Find", "Do", "Don't", "Danger",
    "Elder", "Timothy", "Elliott", "Ask", "Where", "Yes", "No", "We", "He", "She", "They", "My",
}
NAME = re.compile(r"\b[A-Z][a-z]+(?:'s)?(?:\s+(?:of\s+)?[A-Z][a-z]+(?:'s)?)*")
HERO = {"Mana"}          # the hero's own name in this save, not news
TITLE = re.compile(r"^(?:Major|Sir|King|Queen|Sage|Witch|Elder|Lord|Lady|Dr)\s")


def sentences(text):
    return [s.strip() for s in re.split(r"(?<=[.!?])\s+", text) if s.strip()]


def understand(text):
    """What a villager's whole speech tells: headings, names, warnings, services."""
    text = " ".join((text or "").split())
    directions, names = set(), []
    for sentence in sentences(text):
        if GUIDANCE.search(sentence) and not NEGATION.search(sentence):
            directions.update(d.lower() for d in DIRECTION.findall(sentence))
    plain = SPEAKER.sub(" ", text)
    for match in NAME.finditer(plain):
        words = match.group(0).split()
        while words and words[0] in NOT_NAMES:
            words = words[1:]
        name = " ".join(words)
        if not name or name in NOT_NAMES or name in HERO or name in names:
            continue
        before = plain[:match.start()].rstrip()
        sentence_start = not before or before[-1] in ".!?"
        # One capitalised word at a sentence start is usually just a word.
        if len(words) == 1 and sentence_start and not TITLE.match(name):
            continue
        # "Sword" after "Mana Sword" is the same thing, not a second name.
        if any(re.search(rf"\b{re.escape(name)}\b", known) for known in names):
            continue
        names = [known for known in names
                 if not re.search(rf"\b{re.escape(known)}\b", name)]
        names.append(name)
    return {"directions": sorted(directions), "names": names[:4],
            "warning": bool(WARNING.search(text)),
            "save": bool(SAVE.search(text)), "rest": bool(REST.search(text))}


def lesson_score(lesson):
    return (3 * bool(lesson.get("directions")) + 2 * bool(lesson.get("names")) +
            bool(lesson.get("warning")) + bool(lesson.get("save")))


def _useful_quote(text):
    """The sentences that carry a clue, bounded for small-model prompts."""
    picked = [s for s in sentences(text) if KEYWORDS.search(s) or DIRECTION.search(s)]
    quote = " ".join(picked) or text
    return quote[:QUOTE_CHARS].rstrip()


def _has_table(db):
    return bool(db.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='npc_dialogue_lines'"
        ).fetchone())


def villager_speech(db, map_id, identity, event):
    """Every distinct line heard from one villager, in ROM text order."""
    rows = db.execute(
        "SELECT text, MIN(pointer) AS p, MIN(ts) FROM npc_dialogue_lines "
        "WHERE map_id IS ? AND npc_identity IS ? AND event_id IS ? AND length(text)>0 "
        "GROUP BY text ORDER BY p, 3", (map_id, identity, event)).fetchall()
    return " ".join(" ".join(r[0].split()) for r in rows if isinstance(r[0], str))


def session_lines(db, session):
    rows = db.execute("SELECT text FROM npc_dialogue_lines WHERE session=? "
                      "ORDER BY pointer, ts", (session,)).fetchall()
    return [" ".join(r[0].split()) for r in rows if isinstance(r[0], str)]


def learned_from_session(db, session):
    """What one finished conversation added. None if the session left no lines."""
    head = db.execute("SELECT map_id,npc_identity,event_id,MIN(ts) FROM npc_dialogue_lines "
                      "WHERE session=?", (session,)).fetchone()
    if not head or head[3] is None:
        return None
    map_id, identity, event, started = head
    said = session_lines(db, session)
    before = {r[0] for r in db.execute(
        "SELECT text FROM npc_dialogue_lines WHERE map_id IS ? AND npc_identity IS ? "
        "AND event_id IS ? AND session<>? AND ts<?",
        (map_id, identity, event, session, started))}
    new = [line for line in said if line not in before]
    speech = villager_speech(db, map_id, identity, event)
    lesson = understand(speech)
    return {"map_id": map_id, "npc_id": identity, "event": event,
            "said": " ".join(said), "new_lines": len(new), "first_time": not before,
            "quote": _useful_quote(speech), **lesson}


def heard_enough(db, map_id, identity, event, now):
    """True while a villager's last talk added nothing and is recent.

    Lines are already persistent, so this survives bridge restarts. After
    RETALK_S the villager is worth asking again: the story may have moved on.
    """
    if not _has_table(db):
        return False
    sessions = db.execute(
        "SELECT session, MIN(ts), MAX(ts) FROM npc_dialogue_lines WHERE map_id IS ? AND "
        "npc_identity IS ? AND event_id IS ? GROUP BY session ORDER BY 2",
        (map_id, identity, event)).fetchall()
    if len(sessions) < 2 or now - sessions[-1][2] > RETALK_S:
        return False
    last = sessions[-1][0]
    learned = learned_from_session(db, last)
    return learned is not None and learned["new_lines"] == 0


def hear_npc_clues(history, map_id=None, limit=MAX_HINTS):
    """Return each villager's useful speech with precise source information.

    No schema changes: draws on NpcDialogue's existing versioned SQLite table.
    Lines heard over several talks are stitched together per villager. Local
    rumors come first, then the most informative, then the newest.
    """
    if history is None or not getattr(history, "connection", None):
        return []
    db = history.connection
    if not _has_table(db):
        return []
    villagers = db.execute(
        "SELECT map_id,npc_identity,event_id,MAX(ts) AS seen FROM npc_dialogue_lines "
        "WHERE length(text)>0 GROUP BY map_id,npc_identity,event_id "
        "ORDER BY seen DESC LIMIT ?", (MAX_SCAN,)).fetchall()
    candidates = []
    for map_seen, identity, event, seen in villagers:
        speech = villager_speech(db, map_seen, identity, event)
        if len(speech) < 6 or not (KEYWORDS.search(speech) or DIRECTION.search(speech)):
            continue
        lesson = understand(speech)
        candidates.append({
            "map_id": map_seen, "npc_id": identity, "event": event,
            "quote": _useful_quote(speech), "directions": lesson["directions"],
            "names": lesson["names"], "warning": lesson["warning"],
            "score": lesson_score(lesson), "seen": seen,
            "source": "observed NPC dialogue (unverified hint)",
        })
    candidates.sort(key=lambda c: (c["map_id"] != map_id, -c["score"], -c["seen"]))
    return candidates[:max(0, min(int(limit), MAX_HINTS))]


def heard_directions(clues, map_id):
    """Positive headings reported locally. The planner still verifies steps."""
    directions = set()
    for item in clues or ():
        if item.get("map_id") == map_id:
            directions.update(d for d in item.get("directions", ())
                              if d in ("north", "south", "east", "west"))
    return sorted(directions)


def hero_comment(learned):
    """A short first-person line about what a talk taught; None if nothing new."""
    if not learned or not learned.get("new_lines"):
        return None
    names = learned.get("names") or []
    heading = (learned.get("directions") or [None])[0]
    if heading and names:
        return f"{names[0]}, to the {heading}. I'll go see."
    if heading:
        return f"Head {heading}, they say. Worth a look."
    if learned.get("save"):
        return "Good, somewhere to save my journey."
    if learned.get("warning") and names:
        return f"Trouble around {names[0]}. Blade ready."
    if learned.get("warning"):
        return "Trouble is coming. I'll stay sharp."
    if names:
        return f"{names[0]}... I'll remember that name."
    return "Village talk. Nothing to act on yet."


def follow_comment(clue):
    """Spoken when the hero sets off on a villager's directions."""
    heading = (clue.get("directions") or ["on"])[0]
    names = clue.get("names") or []
    return (f"Going {heading}, toward {names[0]}." if names else
            f"Going {heading}, like the villager said.")


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

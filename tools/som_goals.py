"""Qwen goal chooser: a compact memory summary and a small goal allowlist.

Qwen never moves the hero. It picks one goal from `offered_goals`, which only
lists goals the planner can carry out from verified state; the deterministic
planner turns the goal into a heading or a travel errand. Survival below half
HP, fighting enemies met on the way and the closed-loop inn/shop macros keep
overriding whatever was chosen. An answer that is not offered keeps the goal.
"""
import re

MAP_NAMES = {128: "Waterfall", 129: "Waterfall path", 130: "Potos outskirts",
             131: "Potos Village", 257: "House", 259: "House"}
POTOS = 131
HEADINGS = ("north", "south", "east", "west")
# Travel goals stay chosen until arrival or this many decisions, so a small
# model cannot undo a journey halfway by answering differently.
TRAVEL_TIMEOUT = 600
HUNT_MAPS_OFFERED = 2
_GOAL_PATTERN = re.compile(
    r"(?<![a-z0-9_])(hunt_here|hunt_map_\d{1,3}|explore_(?:north|south|east|west|unknown)|"
    r"go_to_inn|go_to_shop|return_to_potos|engage_enemy|avoid_enemy)(?![a-z0-9_])")


def is_travel(goal):
    return goal in ("go_to_inn", "go_to_shop", "return_to_potos") or \
        bool(goal and goal.startswith("hunt_map_"))


def memory_summary(history, observation, gold=None, shop_errand=None):
    """Few facts the goal choice depends on, all from verified memory."""
    context = (observation or {}).get("tile_context") or {}
    hero = ((observation or {}).get("party") or [{}])[0]
    summary = {"map_id": context.get("map_id"), "hp": hero.get("hp"),
               "max_hp": hero.get("max_hp"), "level": hero.get("level"),
               "gold": gold, "candy": (observation or {}).get("candy_count"),
               "enemies_near": sum(1 for enemy in (observation or {}).get("enemy_slots", [])
                                   if isinstance(enemy, dict) and enemy.get("active")),
               "inn_map": None, "shop_map": None, "shop_ready": bool(shop_errand),
               "known_maps": [], "reward_maps": [], "avoided_enemies": []}
    if history is None:
        return summary
    anchor = history.safe_anchor()
    if isinstance(anchor, dict):
        summary["inn_map"] = anchor.get("map_id")
    shop = history.get_agent_state("shop")
    if isinstance(shop, dict):
        summary["shop_map"] = shop.get("map_id")
    summary["known_maps"] = [row[0] for row in history.connection.execute(
        "SELECT map_id FROM world_cells GROUP BY map_id HAVING COUNT(*)>=50 "
        "ORDER BY map_id")]
    summary["reward_maps"] = [
        {"map_id": map_id, "score": round(score), "gold": gold_sum, "hp_lost": hp}
        for map_id, gold_sum, hp, score in history.connection.execute(
            "SELECT map_id,SUM(gold_gained),SUM(hp_lost),SUM(score) FROM reward_ledger "
            "GROUP BY map_id ORDER BY SUM(score) DESC")]
    summary["avoided_enemies"] = sorted(history.unprofitable_enemies())
    return summary


def offered_goals(summary, hunt_direction="west", reachable=lambda map_id: True):
    """Goals the planner can carry out now; the first is the safe default."""
    goals = ["hunt_here"]
    here = summary.get("map_id")
    if summary.get("enemies_near"):
        goals += ["engage_enemy", "avoid_enemy"]
    if hunt_direction == "none":
        goals += [f"explore_{heading}" for heading in HEADINGS]
    hp, max_hp = summary.get("hp"), summary.get("max_hp")
    if (summary.get("inn_map") is not None and isinstance(hp, int) and max_hp and
            hp * 10 < max_hp * 9 and reachable(summary["inn_map"])):
        goals.append("go_to_inn")
    if summary.get("shop_ready") and summary.get("shop_map") is not None and \
            reachable(summary["shop_map"]):
        goals.append("go_to_shop")
    if here != POTOS and reachable(POTOS):
        goals.append("return_to_potos")
    offered = 0
    for item in summary.get("reward_maps") or []:
        if offered >= HUNT_MAPS_OFFERED or item["score"] <= 0:
            break
        if item["map_id"] != here and reachable(item["map_id"]):
            goals.append(f"hunt_map_{item['map_id']}")
            offered += 1
    return goals


def build_goal_prompt(objective, summary, offered, current):
    """Short prompt: CPU prompt processing dominates small-model latency."""
    here = summary.get("map_id")
    rewards = ", ".join(f"map {item['map_id']} score {item['score']}"
                        for item in (summary.get("reward_maps") or [])[:3]) or "none yet"
    return (
        "You choose the next goal for a Secret of Mana hero. Score: +1 per gold "
        "gained, -2 per percent of max HP lost. "
        f"Player objective: {objective}. "
        f"Now: {MAP_NAMES.get(here, 'map')} (map {here}), HP {summary.get('hp')}/"
        f"{summary.get('max_hp')}, level {summary.get('level')}, gold {summary.get('gold')}, "
        f"candy {summary.get('candy')}, enemies near {summary.get('enemies_near')}. "
        f"Inn on map {summary.get('inn_map')}, shop on map {summary.get('shop_map')}. "
        f"Known maps {summary.get('known_maps')}. Score per map: {rewards}. "
        f"Current goal: {current}. Goals: {', '.join(offered)}. "
        "Keep the current goal unless another earns a better score. "
        "Reply with exactly one goal from the list."
    )


def parse_goal(raw, offered):
    """One offered goal from a free-form reply, else None."""
    text = re.sub(r"<think>.*?(</think>|$)", " ", raw or "", flags=re.S | re.I)
    text = text.lower().replace("-", "_")
    text = re.sub(r"\bhunt\s+map\s+(\d+)", r"hunt_map_\1", text)
    text = re.sub(r"\b(go)\s+(to)\s+(inn|shop)\b", r"\1_\2_\3", text)
    for match in _GOAL_PATTERN.finditer(text):
        if match.group(1) in offered:
            return match.group(1)
    return None


def goal_plan(goal, summary, hunt_direction="west", inn_anchor=None, shop_errand=None):
    """(directive, errand, done) for the planner; errand None = no travel."""
    heading = f"explore_{hunt_direction}" if hunt_direction != "none" else "explore_unknown"
    here = summary.get("map_id")
    if goal in ("engage_enemy", "avoid_enemy"):
        return goal, None, False
    if goal and goal.startswith("explore_"):
        return goal, None, False
    if goal == "go_to_inn" and isinstance(inn_anchor, dict):
        return heading, {"place": "inn", "anchor": inn_anchor,
                         "arrive": "recover_at_inn"}, False
    if goal == "go_to_shop" and shop_errand:
        return heading, shop_errand, False
    target = POTOS if goal == "return_to_potos" else (
        int(goal.rsplit("_", 1)[1]) if goal and goal.startswith("hunt_map_") else None)
    if target is not None:
        if target == here:
            return heading, None, True
        return heading, {"place": f"map {target}", "anchor": {"map_id": target}}, False
    return heading, None, goal != "hunt_here"

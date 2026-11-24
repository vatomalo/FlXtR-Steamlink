"""Opt-in Potos lesson grounded in the guide and som-decomp.

The guide (printed pp. 7-9) describes circling the Mantis Ant and following
Jema's lead after the fight. The map, monster and flag identities below
were cross-checked against the local Turbo event/transition tables using
som-decomp's map_tables and event_vm decoders.
"""

POTOS_MAP = 131
MANTIS_MAP = 260
MANTIS_ID = 0x57
ROAD_MAP = 16


def story_stage(wram, observation):
    """Read checkpoints only from verified field observations."""
    context = (observation or {}).get("tile_context") or {}
    if not (wram and context.get("runtime_verified") and
            context.get("game_mode") == "field"):
        return None
    here = context.get("map_id")
    story, pit = wram.u8(0x7ECF10), wram.u8(0x7ECF0B)
    if here == MANTIS_MAP and (story, pit) == (6, 1):
        if any(is_mantis(enemy) for enemy in observation.get("enemy_slots", ())):
            return "fight"
    if (story, pit) == (9, 2):
        if here == POTOS_MAP:
            return "leave"
        if here == ROAD_MAP:
            return "road_reached"
    return None


def is_mantis(enemy):
    return (enemy.get("slot") == 1 and enemy.get("identity") == MANTIS_ID and
            enemy.get("active") and enemy.get("hp_plausible") and
            enemy.get("hp", 0) > 0)

"""Learn native return doors from read-only tiles and observed crossings.

som-decomp docs/maps.md section 7: attribute byte 1 bit 4 invokes the saved
return location. This is not a plain ROM trigger. Separate rooms can share
one map id, so a doorway is always identified by map, layer and coordinates.
Candidates are recorded separately from crossings observed on Turbo.
"""


class DoorLearner:
    def __init__(self, history):
        self.history = history
        self.cached_key = None
        self.doors = []
        self.previous = None

    def observe(self, wram, observation):
        context = observation.get("tile_context") or {}
        if (not context.get("runtime_verified") or context.get("game_mode") != "field" or
                context.get("map_loading")):
            self.cached_key = None
            return
        actor = context["actor"]
        size = context["map_size_tiles"]
        key = (context["map_id"], actor["layer"], *size)
        if key != self.cached_key:
            self.cached_key = key
            self.doors = []
            for y in range(size[1]):
                for x in range(size[0]):
                    tile, attributes = wram.tile(x, y, actor["layer"])
                    if attributes[1] & 0x10:
                        self.doors.append({"target": [x, y], "tile_id": tile,
                                           "attributes": list(attributes)})
                        # Preserve a confirmed lesson on subsequent visits.
                        known = self.history.connection.execute(
                            "SELECT source FROM places WHERE map_id=? AND layer=? "
                            "AND x=? AND y=? AND kind='return_door'",
                            (key[0], key[1], x, y)).fetchone()
                        if not known:
                            self.history.remember_place(key[0], key[1], x, y,
                                "return_door", "Native return door (candidate)",
                                "attribute_b1_bit4")
            self.history.set_agent_state(f"door_map_size_{key[0]}", list(size))
        context["return_doors"] = list(self.doors)
        current = {"map": key[0], "layer": key[1],
                   "tile": list(context["actor_tile"]), "doors": list(self.doors)}
        before = self.previous
        if before is not None and before["map"] != current["map"]:
            nearby = [(abs(d["target"][0] - before["tile"][0]) +
                       abs(d["target"][1] - before["tile"][1]), d)
                      for d in before["doors"]]
            if nearby:
                distance, door = min(nearby, key=lambda item: item[0])
                if distance <= 2:
                    x, y = door["target"]
                    self.history.remember_place(before["map"], before["layer"], x, y,
                        "return_door", f"Return door to map {current['map']}",
                        "observed_map_transition")
        self.previous = current

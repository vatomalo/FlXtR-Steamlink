#!/usr/bin/env python3
"""Inherited perk database -> validated C registry, CSV and docs.

    python3 tools/generate_perk_registry.py            # validate + write
    python3 tools/generate_perk_registry.py --check    # fail if outputs are stale
    python3 tools/generate_perk_registry.py --format   # rewrite perks.json canonically

Inputs (canonical, hand edited):
  data/perks/perks.json         the perks, disciplines, capacity, builds
  data/perks/capabilities.json  what the engine can do per trigger/condition/effect
  data/perks/verified.json      perks whose behaviour a test has checked

Outputs (generated, never edit):
  src/generated/som_perk_registry.h   static C tables (header only: the proxy
                                      is one translation unit)
  data/perks/perks.csv
  docs/SOM_PERK_DATABASE.md, docs/SOM_PERK_CAPABILITY_MATRIX.md,
  docs/SOM_HYBRID_BUILDS.md

No third-party modules: the schema rules are checked here directly
(data/perks/perk_schema.json documents the same shape for editors)."""
import argparse
import collections
import csv
import io
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data", "perks")
OUT_H = os.path.join(ROOT, "src", "generated", "som_perk_registry.h")
OUT_CSV = os.path.join(DATA, "perks.csv")
OUT_DB = os.path.join(ROOT, "docs", "SOM_PERK_DATABASE.md")
OUT_MATRIX = os.path.join(ROOT, "docs", "SOM_PERK_CAPABILITY_MATRIX.md")
OUT_BUILDS = os.path.join(ROOT, "docs", "SOM_HYBRID_BUILDS.md")

CATEGORIES = ["attribute", "technique", "trigger", "transformative", "keystone"]
TIERS = ["novice", "apprentice", "adept", "expert", "master", "grandmaster"]
ROLES = ["combat", "trigger", "defense", "resource", "party", "explore", "economy"]
POLICIES = ["refresh", "add", "ignore"]
SOURCES = ["discipline_mastery", "equipment", "temporary_buff", "story_unlock", "universal_progression"]
COST_RANGE = {"attribute": (1, 2), "technique": (2, 3), "trigger": (2, 3), "transformative": (4, 4), "keystone": (5, 8)}
# Worst first: the first status a perk's capabilities hit decides it.
CAP_ORDER = ["entity", "hook", "primitive", "ui", "engine", "runtime"]
STATUS_OF_CAP = {"entity": "REQUIRES_ENTITY_SUPPORT", "hook": "REQUIRES_ENGINE_HOOK",
                 "primitive": "REQUIRES_NEW_EFFECT_PRIMITIVE", "ui": "REQUIRES_UI_SUPPORT",
                 "engine": "REQUIRES_ENGINE_HOOK"}
STATUSES = ["IMPLEMENTED", "SUPPORTED_PENDING_TEST", "REQUIRES_ENGINE_HOOK", "REQUIRES_NEW_EFFECT_PRIMITIVE",
            "REQUIRES_ENTITY_SUPPORT", "REQUIRES_UI_SUPPORT", "DESIGN_ONLY"]
TARGETS = ["self", "same", "nearest_other", "attacker", "behind_in_line", "passed_through", "owner", "ally",
           "nearest_ally", "healed", "area"]
MODIFIERS = ["next_hit_damage", "damage", "crit_chance", "crit_next", "counter_damage", "dodge_frames",
             "dodge_distance"]
ONLY = ["first_hit_on_target", "later_hits", "behind_target", "front", "ranged", "melee", "low_hp", "above_half",
        "after_switch", "same_weapon_twice", "undead_or_boss", "other", "familiar_on_marked", "mp_empty"]
CLEAR = ["DAMAGE_POST", "same_kind", "chain_break", "same_element"]
# Effect parameters -> the C slots. a: the magnitude, base: what a % is of.
A_KEYS = [("pct", "hit"), ("pct_of_hit", "hit"), ("pct_of_last_hit", "last_hit"), ("pct_max_hp", "max_hp"),
          ("pct_owner_max_hp", "owner_max_hp"), ("pct_of_heal", "heal"), ("pct_per_stack", "hit"),
          ("pct_per_combo_hit", "hit"), ("pct_per_8px", "hit"), ("scale_pct", "flat"), ("strength_pct", "flat"),
          ("duration_pct", "flat"), ("rare_pct", "flat"), ("pct_of_drop", "flat"), ("weak_pct", "flat"),
          ("spell_pct_each", "flat"), ("damage_pct", "hit"), ("party_damage_pct", "hit"), ("max_pct", "flat"),
          ("amount", "flat"), ("per_level", "flat"), ("px", "flat"), ("frames", "flat"), ("extend_frames", "flat"),
          ("pips", "flat"), ("gain", "flat"), ("swings", "flat"), ("hp_per_mp", "flat"), ("count", "flat")]
B_KEYS = ["radius", "px"]
C_KEYS = ["max_pct", "max", "max_total_frames", "max_extra_frames", "ticks", "charges", "boss_frames", "max_extra",
          "extra", "hp_per_mp", "max_depth", "swings"]
F_KEYS = ["duration_frames", "frames"]
D_KEYS = ["delay_frames", "interval_frames"]
BASES = ["hit", "last_hit", "max_hp", "owner_max_hp", "heal", "flat"]


def load(name):
    with open(os.path.join(DATA, name), encoding="utf-8") as f:
        return json.load(f)


def cname(s):
    return re.sub(r"[^A-Za-z0-9]+", "_", s).strip("_").upper()


def cstr(s):
    return '"' + (s or "").replace("\\", "\\\\").replace('"', '\\"') + '"'


def cond_split(c):
    name, _, arg = c.partition(":")
    return name, [a for a in arg.split(",") if a] if arg else []


class Db:
    def __init__(self, perks, caps, verified):
        self.doc, self.caps, self.verified = perks, caps, verified
        self.perks = perks["perks"]
        self.disc = {d["key"]: d for d in perks["disciplines"]}
        self.by_key = {p["key"]: p for p in self.perks}
        self.errors = []
        self.tokens = []

    def err(self, where, msg):
        self.errors.append(f"{where}: {msg}")

    def token(self, s):
        if s not in self.tokens:
            self.tokens.append(s)
        return self.tokens.index(s)

    # ---------------------------------------------------------- validation
    def validate(self):
        ids, keys = set(), set()
        if len(self.perks) < 200:
            self.err("perks", f"{len(self.perks)} perks, at least 200 are required")
        for p in self.perks:
            w = p.get("key", f"id {p.get('id')}")
            for f in ("id", "key", "name", "discipline", "category", "tier", "role", "mastery_required",
                      "capacity_cost", "description", "triggers", "conditions", "effects", "cooldown_frames",
                      "proc_chance", "stacking_policy", "max_stacks", "charges", "prerequisites", "boss_ok",
                      "generated_can_trigger", "ai_tactic", "tags", "sources"):
                if f not in p:
                    self.err(w, f"missing field {f}")
            if self.errors and self.errors[-1].startswith(w + ": missing"):
                continue
            if not isinstance(p["id"], int) or not 1 <= p["id"] <= 0xFFFF:
                self.err(w, "id must be 1..65535")
            if p["id"] in ids:
                self.err(w, f"duplicate id {p['id']}")
            if p["key"] in keys:
                self.err(w, "duplicate key")
            ids.add(p["id"])
            keys.add(p["key"])
            d = self.disc.get(p["discipline"])
            if not d:
                self.err(w, f"unknown discipline {p['discipline']}")
            elif not p["key"].startswith(p["discipline"] + "."):
                self.err(w, "key must start with its discipline")
            elif not d["id_base"] < p["id"] < d["id_base"] + 100:
                self.err(w, f"id outside its discipline's range {d['id_base']}..{d['id_base'] + 99}")
            if not re.fullmatch(r"[a-z_]+\.[a-z0-9_]+", p["key"]):
                self.err(w, "key must be discipline.snake_case")
            if not p["name"].strip() or len(p["name"]) > 24:
                self.err(w, "name must be 1..24 characters")
            if not p["description"].strip() or not p["description"].endswith("."):
                self.err(w, "description must be a sentence")
            if len(p["description"]) > 160:
                self.err(w, "description over 160 characters (menu space)")
            for f, allowed in (("category", CATEGORIES), ("tier", TIERS), ("role", ROLES),
                               ("stacking_policy", POLICIES)):
                if p[f] not in allowed:
                    self.err(w, f"{f} {p[f]!r} not in {allowed}")
            lo, hi = COST_RANGE.get(p["category"], (1, 8))
            if not lo <= p["capacity_cost"] <= hi:
                self.err(w, f"capacity_cost {p['capacity_cost']} outside {lo}..{hi} for a {p['category']}")
            if not 1 <= p["mastery_required"] <= 99:
                self.err(w, "mastery_required must be 1..99")
            if p["discipline"] != "universal" and p["mastery_required"] != self.doc["tiers"].get(p["tier"]):
                self.err(w, "mastery_required must match its tier")
            if not 0 <= p["cooldown_frames"] <= 36000:
                self.err(w, "cooldown_frames must be 0..36000")
            if p["proc_chance"] != 1.0 and not 0 < p["proc_chance"] < 1:
                self.err(w, "proc_chance must be in (0, 1]")
            if not 1 <= p["max_stacks"] <= 10 or not 0 <= p["charges"] <= 10:
                self.err(w, "max_stacks 1..10, charges 0..10")
            if p["stacking_policy"] == "add" and p["max_stacks"] < 2:
                self.err(w, "an adding perk needs max_stacks >= 2")
            if not p["triggers"] or not p["effects"]:
                self.err(w, "needs a trigger and an effect")
            if len(p["triggers"]) > 3 or len(p["conditions"]) > 3 or len(p["effects"]) > 3:
                self.err(w, "at most 3 triggers, 3 conditions and 3 effects")
            for t in p["triggers"]:
                if t not in self.caps["events"]:
                    self.err(w, f"unknown trigger {t}")
            for c in p["conditions"]:
                name, args = cond_split(c)
                if name not in self.caps["conditions"]:
                    self.err(w, f"unknown condition {name}")
                for a in args:
                    if a.isdigit() and not 0 < int(a) <= 32767:
                        self.err(w, f"condition argument {a} out of range")
            for e in p["effects"]:
                self.check_effect(w, e)
            for s in p["sources"]:
                if s not in SOURCES:
                    self.err(w, f"unknown source {s}")
            if p["ai_tactic"] is None or not re.fullmatch(r"[a-z_]+", p["ai_tactic"]):
                self.err(w, "ai_tactic must be snake_case")
            if p.get("feedback") is not None and len(p["feedback"]) > 12:
                self.err(w, "feedback text over 12 characters")
        for p in self.perks:
            for r in p.get("prerequisites", []):
                if r not in self.by_key:
                    self.err(p["key"], f"unknown prerequisite {r}")
                elif r == p["key"]:
                    self.err(p["key"], "prerequisite on itself")
        self.check_cycles()
        groups = collections.defaultdict(list)
        for p in self.perks:
            if p.get("exclusive_group"):
                groups[p["exclusive_group"]].append(p["key"])
        for g, members in groups.items():
            if len(members) < 2:
                self.err(g, "an exclusive group needs at least two perks")
        for k in self.verified.get("perks", {}):
            if k not in self.by_key:
                self.err("verified.json", f"unknown perk {k}")
        cap = self.doc["capacity"]
        if not 1 <= cap["base"] <= cap["max"] <= 60 or cap["per_hero_levels"] < 1:
            self.err("capacity", "base/max/per_hero_levels out of range")
        if not 1 <= self.doc["max_equipped"] <= 16:
            self.err("max_equipped", "1..16")
        names = set()
        for b in self.doc["builds"]:
            if b["name"] in names:
                self.err("builds", f"duplicate build {b['name']}")
            names.add(b["name"])
            if b["primary"] not in self.disc or b["primary"] == "universal":
                self.err(b["name"], f"primary {b['primary']} is not a discipline")
            for k in b["perks"]:
                if k not in self.by_key:
                    self.err(b["name"], f"unknown perk {k}")
            if len(set(b["perks"])) != len(b["perks"]):
                self.err(b["name"], "a perk listed twice")
        if len(self.doc["builds"]) < 25:
            self.err("builds", "at least 25 hybrid builds")
        return not self.errors

    def check_effect(self, w, e):
        t = e.get("type")
        if t not in self.caps["effects"]:
            self.err(w, f"unknown effect {t}")
            return
        for k, v in e.items():
            if k == "type":
                continue
            if isinstance(v, bool) or isinstance(v, str):
                continue
            if not isinstance(v, (int, float)):
                self.err(w, f"effect {t}.{k} must be a number, text or flag")
            elif not -10000 <= v <= 36000:
                self.err(w, f"effect {t}.{k}={v} out of range")
        if e.get("target") and e["target"] not in TARGETS:
            self.err(w, f"effect {t}: unknown target {e['target']}")
        if e.get("modifier") and e["modifier"] not in MODIFIERS:
            self.err(w, f"effect {t}: unknown modifier {e['modifier']}")
        if e.get("only") and e["only"] not in ONLY:
            self.err(w, f"effect {t}: unknown 'only' {e['only']}")
        if e.get("clear_on") and e["clear_on"] not in CLEAR:
            self.err(w, f"effect {t}: unknown clear_on {e['clear_on']}")

    def check_cycles(self):
        state = {}

        def visit(k, path):
            if state.get(k) == 1:
                self.err(k, "circular prerequisites: " + " -> ".join(path + [k]))
                return
            if state.get(k) == 2:
                return
            state[k] = 1
            for r in self.by_key[k].get("prerequisites", []):
                if r in self.by_key:
                    visit(r, path + [k])
            state[k] = 2
        for k in self.by_key:
            visit(k, [])

    # ---------------------------------------------------------- status
    def needs(self, p):
        out = [("events", t) for t in p["triggers"] if t != "PASSIVE"]
        out += [("conditions", cond_split(c)[0]) for c in p["conditions"]]
        out += [("effects", e["type"]) for e in p["effects"]]
        return out

    def status(self, p):
        """(status, blockers): the worst capability decides."""
        worst, blockers = "runtime", []
        for kind, name in self.needs(p):
            s = self.caps[kind][name]["status"]
            if s != "runtime":
                blockers.append(f"{kind[:-1]} {name}: {s} ({self.caps[kind][name]['note']})")
            if CAP_ORDER.index(s) < CAP_ORDER.index(worst):
                worst = s
        if worst == "runtime":
            return ("IMPLEMENTED" if p["key"] in self.verified.get("perks", {}) else "SUPPORTED_PENDING_TEST"), []
        return STATUS_OF_CAP[worst], blockers

    # ---------------------------------------------------------- C
    def effect_slots(self, e):
        a, base = 0, "flat"
        for k, b in A_KEYS:
            if k in e and not isinstance(e[k], (str, bool)):
                a, base = e[k], b
                break
        b = next((e[k] for k in B_KEYS if k in e and k != next((x for x, _ in A_KEYS if x in e), None)), 0)
        c = next((e[k] for k in C_KEYS if k in e and not isinstance(e[k], bool)), 0)
        f = next((e[k] for k in F_KEYS if k in e), 0)
        d = next((e[k] for k in D_KEYS if k in e), 0)
        sub = next((e[k] for k in ("ability", "status", "stat", "kind", "what", "species", "mode", "item", "release",
                                    "scope", "which", "to", "on", "follow", "per", "refresh_on", "visual")
                    if isinstance(e.get(k), str)), "")
        flags = []
        if e.get("exclude_target"):
            flags.append("SOM_PFXF_EXCLUDE_TARGET")
        if e.get("ignore_defence"):
            flags.append("SOM_PFXF_IGNORE_DEFENCE")
        if e.get("both_ways") or e.get("pin") or e.get("refresh") or e.get("show_warning"):
            flags.append("SOM_PFXF_OPTION")
        target = e.get("target") or ("area" if "radius" in e else "same")
        return dict(type="SOM_PFX_" + cname(e["type"]), a=int(a), base="SOM_PBASE_" + cname(base), b=int(b),
                    c=int(c), frames=int(f), delay=int(d), target="SOM_PTGT_" + cname(target),
                    modifier="SOM_PMOD_" + cname(e.get("modifier", "none")),
                    only="SOM_PONLY_" + cname(e.get("only", "none")),
                    clear="SOM_PCLEAR_" + cname(e.get("clear_on", "none")),
                    sub=self.token(sub) if sub else 0, flags="|".join(flags) or "0")

    def cond_slots(self, c):
        name, args = cond_split(c)
        vals = [int(a) if a.isdigit() else self.token(a) for a in args] + [0, 0]
        return name, vals[0], vals[1]

    def header(self):
        self.tokens = [""]
        perks = sorted(self.perks, key=lambda p: p["id"])
        ev, co, fx = list(self.caps["events"]), list(self.caps["conditions"]), list(self.caps["effects"])
        groups = sorted({p["exclusive_group"] for p in perks if p.get("exclusive_group")})
        tactics = sorted({p["ai_tactic"] for p in perks})
        L = []
        L.append("/* GENERATED by tools/generate_perk_registry.py from data/perks/perks.json")
        L.append(" * and data/perks/capabilities.json. Do not edit: change the JSON and run the")
        L.append(" * generator. Static tables only: no allocation, no parsing at run time. */")
        L.append("#ifndef FLXTR_SOM_PERK_REGISTRY_H\n#define FLXTR_SOM_PERK_REGISTRY_H\n#include <stdint.h>\n")
        L.append(f"#define SOM_PERK_DB_VERSION {self.doc['version']}u")
        L.append(f"#define SOM_PERK_DEF_COUNT {len(perks)}u")
        L.append(f"#define SOM_PERK_MAX_EQUIPPED {self.doc['max_equipped']}u")
        cap = self.doc["capacity"]
        L.append(f"#define SOM_PERK_CAPACITY_BASE {cap['base']}u")
        L.append(f"#define SOM_PERK_CAPACITY_PER_LEVELS {cap['per_hero_levels']}u")
        L.append(f"#define SOM_PERK_CAPACITY_MAX {cap['max']}u")
        L.append("#define SOM_PERK_UNIVERSAL 255u\n")

        def enum(name, prefix, items, first_none=False):
            L.append(f"enum {name} {{")
            vals = (["NONE"] if first_none else []) + [cname(i) for i in items]
            L.append("    " + ",\n    ".join(f"{prefix}{v}" + (" = 0" if j == 0 else "") for j, v in enumerate(vals)) + ",")
            L.append(f"    {prefix}COUNT\n}};")
        enum("SomPerkEvent", "SOM_PEV_", ev, True)
        enum("SomPerkCond", "SOM_PCOND_", co, True)
        enum("SomPerkFxType", "SOM_PFX_", fx, True)
        enum("SomPerkTarget", "SOM_PTGT_", TARGETS)
        enum("SomPerkModifier", "SOM_PMOD_", MODIFIERS, True)
        enum("SomPerkOnly", "SOM_PONLY_", ONLY, True)
        enum("SomPerkClear", "SOM_PCLEAR_", CLEAR, True)
        enum("SomPerkBase", "SOM_PBASE_", BASES)
        enum("SomPerkCategory", "SOM_PCAT_", CATEGORIES)
        enum("SomPerkTier", "SOM_PTIER_", TIERS)
        enum("SomPerkRole", "SOM_PROLE_", ROLES)
        enum("SomPerkPolicy", "SOM_PPOL_", POLICIES)
        enum("SomPerkStatus", "SOM_PSTAT_", STATUSES)
        enum("SomPerkGroup", "SOM_PGRP_", groups, True)
        enum("SomPerkTactic", "SOM_PAI_", [t for t in tactics if t != "none"], True)
        L.append("enum { SOM_PFXF_EXCLUDE_TARGET = 1, SOM_PFXF_IGNORE_DEFENCE = 2, SOM_PFXF_OPTION = 4 };")
        L.append("enum { SOM_PDF_BOSS_OK = 1, SOM_PDF_GENERATED_OK = 2, SOM_PDF_UNIVERSAL = 4 };\n")
        L.append("typedef struct { uint8_t type, base, target, modifier, only, clear, sub, flags;")
        L.append("                 int16_t a, b, c; uint16_t frames, delay; } SomPerkFx;")
        L.append("typedef struct { uint8_t type; int16_t arg, arg2; } SomPerkCondDef;")
        L.append("typedef struct {")
        L.append("    uint16_t id; uint8_t discipline, category, tier, role, mastery, cost, status, policy,")
        L.append("             max_stacks, charges, group, tactic, flags, n_trig, n_cond, n_fx;")
        L.append("    uint8_t trig[3]; SomPerkCondDef cond[3]; SomPerkFx fx[3];")
        L.append("    uint16_t cooldown; uint16_t prereq[2];")
        L.append("    const char *key, *name, *description, *feedback;")
        L.append("} SomPerkDef;\n")
        rows = []
        for p in perks:
            d = self.disc[p["discipline"]]
            st, _ = self.status(p)
            flags = []
            if p["boss_ok"]:
                flags.append("SOM_PDF_BOSS_OK")
            if p["generated_can_trigger"]:
                flags.append("SOM_PDF_GENERATED_OK")
            if p["discipline"] == "universal":
                flags.append("SOM_PDF_UNIVERSAL")
            trig = [t for t in p["triggers"]]
            tr = ", ".join("SOM_PEV_" + cname(t) for t in trig) or "0"
            cs = [self.cond_slots(c) for c in p["conditions"]]
            cond = ", ".join(f"{{SOM_PCOND_{cname(n)}, {a}, {b}}}" for n, a, b in cs) or "{0, 0, 0}"
            fxs = []
            for e in p["effects"]:
                s = self.effect_slots(e)
                fxs.append("{%(type)s, %(base)s, %(target)s, %(modifier)s, %(only)s, %(clear)s, %(sub)d, %(flags)s, "
                           "%(a)d, %(b)d, %(c)d, %(frames)d, %(delay)d}" % s)
            pre = [self.by_key[k]["id"] for k in p["prerequisites"]][:2] + [0, 0]
            rows.append(
                f"    {{{p['id']}, {d['som_index']}, SOM_PCAT_{cname(p['category'])}, SOM_PTIER_{cname(p['tier'])}, "
                f"SOM_PROLE_{cname(p['role'])}, {p['mastery_required']}, {p['capacity_cost']}, SOM_PSTAT_{st}, "
                f"SOM_PPOL_{cname(p['stacking_policy'])}, {p['max_stacks']}, {p['charges']}, "
                f"SOM_PGRP_{cname(p['exclusive_group'] or 'none')}, SOM_PAI_{cname(p['ai_tactic'])}, "
                f"{'|'.join(flags) or '0'}, {len(trig)}, {len(cs)}, {len(fxs)},\n"
                f"     {{{tr}}}, {{{cond}}},\n     {{{', '.join(fxs)}}},\n"
                f"     {p['cooldown_frames']}, {{{pre[0]}, {pre[1]}}}, {cstr(p['key'])}, {cstr(p['name'])},\n"
                f"     {cstr(p['description'])}, {cstr(p.get('feedback'))}}},")
        L.append("/* String parameters (ability, status, mark kind ...) as small numbers. */")
        L.append("static const char *const som_perk_tokens[] = {" + ", ".join(cstr(t) for t in self.tokens) + "};")
        L.append(f"#define SOM_PERK_TOKEN_COUNT {len(self.tokens)}u\n")
        L.append("/* Sorted by id: som_perk_def_by_id is a binary search. */")
        L.append("static const SomPerkDef som_perk_defs[SOM_PERK_DEF_COUNT] = {")
        L.extend(rows)
        L.append("};\n")
        L.append("static const char *const som_perk_event_names[SOM_PEV_COUNT] = {\"\", " +
                 ", ".join(cstr(e) for e in ev) + "};")
        L.append("static const char *const som_perk_status_names[SOM_PSTAT_COUNT] = {" +
                 ", ".join(cstr(s) for s in STATUSES) + "};")
        L.append("static const char *const som_perk_tier_names[SOM_PTIER_COUNT] = {" +
                 ", ".join(cstr(t.capitalize()) for t in TIERS) + "};")
        L.append("static const char *const som_perk_category_names[SOM_PCAT_COUNT] = {" +
                 ", ".join(cstr(c.capitalize()) for c in CATEGORIES) + "};")
        L.append("static const uint8_t som_perk_tier_level[SOM_PTIER_COUNT] = {" +
                 ", ".join(str(self.doc["tiers"][t]) for t in TIERS) + "};\n")
        L.append("static inline const SomPerkDef *som_perk_def_by_id(unsigned id){")
        L.append("    unsigned lo=0,hi=SOM_PERK_DEF_COUNT;")
        L.append("    while(lo<hi){unsigned mid=(lo+hi)/2u;")
        L.append("        if(som_perk_defs[mid].id==id)return &som_perk_defs[mid];")
        L.append("        if(som_perk_defs[mid].id<id)lo=mid+1u;else hi=mid;}")
        L.append("    return 0;\n}")
        L.append("static inline int som_perk_def_index(unsigned id){")
        L.append("    const SomPerkDef *d=som_perk_def_by_id(id);return d?(int)(d-som_perk_defs):-1;\n}")
        L.append("#endif")
        return "\n".join(L) + "\n"

    # ---------------------------------------------------------- CSV / docs
    def csv(self):
        b = io.StringIO()
        w = csv.writer(b, lineterminator="\n")
        w.writerow(["id", "key", "name", "discipline", "branch", "category", "tier", "mastery_required",
                    "capacity_cost", "triggers", "conditions", "effects", "description", "cooldown_frames",
                    "implementation_status", "required_capabilities", "tags"])
        for p in sorted(self.perks, key=lambda p: p["id"]):
            st, _ = self.status(p)
            w.writerow([p["id"], p["key"], p["name"], p["discipline"], p.get("branch") or "", p["category"],
                        p["tier"], p["mastery_required"], p["capacity_cost"], " ".join(p["triggers"]),
                        " ".join(p["conditions"]), " ".join(e["type"] for e in p["effects"]), p["description"],
                        p["cooldown_frames"], st,
                        " ".join(f"{k[:-1]}.{n}" for k, n in self.needs(p)), " ".join(p["tags"])])
        return b.getvalue()

    def counts(self):
        st = collections.Counter(self.status(p)[0] for p in self.perks)
        return st

    def doc_database(self):
        L = ["# SoM inherited perk database", "",
             "Generated by `tools/generate_perk_registry.py` from `data/perks/perks.json`. Do not edit by hand.", ""]
        st = self.counts()
        L.append(f"**{len(self.perks)} perks.** Status: " +
                 ", ".join(f"{s} {st[s]}" for s in STATUSES if st[s]) + ".")
        L.append("")
        L.append("Only `IMPLEMENTED` perks can be equipped in ordinary play; `SUPPORTED_PENDING_TEST` ones need the "
                 "`/tmp/flxtr-som-perks-pending` test switch. Everything else is designed and waits for the engine "
                 "work named in `docs/SOM_PERK_CAPABILITY_MATRIX.md`.")
        L.append("")
        L.append("| Discipline | Class | Perks | Note |")
        L.append("|---|---|---:|---|")
        per = collections.Counter(p["discipline"] for p in self.perks)
        for d in self.doc["disciplines"]:
            L.append(f"| {d['key']} | {d['title']} | {per[d['key']]} | {d['note']} |")
        L.append("")
        roles = collections.Counter(p["role"] for p in self.perks)
        cats = collections.Counter(p["category"] for p in self.perks)
        L.append("Roles: " + ", ".join(f"{r} {roles[r]} ({100 * roles[r] // len(self.perks)}%)" for r in ROLES) + ".")
        L.append("")
        L.append("Categories: " + ", ".join(f"{c} {cats[c]}" for c in CATEGORIES) + ".")
        L.append("")
        for d in self.doc["disciplines"]:
            ps = [p for p in self.perks if p["discipline"] == d["key"]]
            L.append(f"## {d['title']} ({d['key']})")
            L.append("")
            L.append("| Id | Perk | Tier | Lv | Cost | Kind | What it does | Status |")
            L.append("|---:|---|---|---:|---:|---|---|---|")
            for p in sorted(ps, key=lambda p: p["id"]):
                br = f" *({p['branch']})*" if p.get("branch") else ""
                L.append(f"| {p['id']} | **{p['name']}**{br} | {p['tier']} | {p['mastery_required']} | "
                         f"{p['capacity_cost']} | {p['category']} | {p['description']} | {self.status(p)[0]} |")
            L.append("")
        return "\n".join(L)

    def doc_matrix(self):
        L = ["# SoM perk capability matrix", "",
             "Generated by `tools/generate_perk_registry.py`. For each engine capability that is not wired into the "
             "perk runtime yet: how many perks it blocks, and how many it would finish on its own (the perk's only "
             "missing piece). Build from the top.", ""]
        st = self.counts()
        L.append("| Status | Perks |")
        L.append("|---|---:|")
        for s in STATUSES:
            L.append(f"| {s} | {st[s]} |")
        L.append("")
        block = collections.defaultdict(set)
        alone = collections.Counter()
        for p in self.perks:
            miss = {(k, n) for k, n in self.needs(p) if self.caps[k][n]["status"] != "runtime"}
            for m in miss:
                block[m].add(p["key"])
            if len(miss) == 1:
                alone[next(iter(miss))] += 1
        L.append("| Capability | Status today | Perks blocked | Finishes alone | Note |")
        L.append("|---|---|---:|---:|---|")
        for (k, n), ps in sorted(block.items(), key=lambda kv: (-alone[kv[0]], -len(kv[1]), kv[0])):
            c = self.caps[k][n]
            L.append(f"| {k[:-1]} `{n}` | {c['status']} | {len(ps)} | {alone[(k, n)]} | {c['note']} |")
        L.append("")
        L.append("## Per perk")
        L.append("")
        L.append("| Perk | Status | Blockers |")
        L.append("|---|---|---|")
        for p in sorted(self.perks, key=lambda p: p["id"]):
            s, b = self.status(p)
            L.append(f"| {p['key']} | {s} | {'; '.join(b) or '-'} |")
        return "\n".join(L) + "\n"

    def doc_builds(self):
        L = ["# SoM hybrid builds", "",
             "Generated from the `builds` list in `data/perks/perks.json`. No build is programmed as a class: "
             "each is a primary discipline plus inherited perks. The capacity needed is the sum of the costs; a "
             "build is playable once every perk in it is `IMPLEMENTED`.", ""]
        for b in self.doc["builds"]:
            ps = [self.by_key[k] for k in b["perks"]]
            cost = sum(p["capacity_cost"] for p in ps)
            ready = sum(1 for p in ps if self.status(p)[0] == "IMPLEMENTED")
            L.append(f"## {b['name']}")
            L.append("")
            L.append(f"Primary: **{self.disc[b['primary']]['title']}** ({b['kind'].replace('_', ' ')}). "
                     f"Capacity {cost}. Playable perks {ready}/{len(ps)}.")
            L.append("")
            L.append(b["idea"])
            L.append("")
            for p in ps:
                L.append(f"- **{p['name']}** ({self.disc[p['discipline']]['title']}, {p['capacity_cost']}): "
                         f"{p['description']} *{self.status(p)[0]}*")
            L.append("")
        return "\n".join(L)


def canonical_json(doc):
    """One perk per block, short lists on one line: easy to diff and edit."""
    def dump(v):
        return json.dumps(v, ensure_ascii=False)
    out = ["{"]
    keys = [k for k in doc if k not in ("perks", "builds")]
    for k in keys:
        out.append(f' {dump(k)}: {json.dumps(doc[k], ensure_ascii=False)},')
    out.append(' "perks": [')
    for i, p in enumerate(doc["perks"]):
        out.append("  {")
        items = list(p.items())
        for j, (k, v) in enumerate(items):
            if k == "effects":
                inner = ",\n".join("    " + dump(e) for e in v)
                txt = f'   "effects": [\n{inner}\n   ]'
            else:
                txt = f"   {dump(k)}: {dump(v)}"
            out.append(txt + ("," if j < len(items) - 1 else ""))
        out.append("  }" + ("," if i < len(doc["perks"]) - 1 else ""))
    out.append(" ],")
    out.append(' "builds": [')
    for i, b in enumerate(doc["builds"]):
        out.append("  " + dump(b) + ("," if i < len(doc["builds"]) - 1 else ""))
    out.append(" ]")
    out.append("}")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="fail if a generated file is out of date")
    ap.add_argument("--format", action="store_true", help="rewrite perks.json in canonical form")
    a = ap.parse_args()
    perks = load("perks.json")
    db = Db(perks, load("capabilities.json"), load("verified.json"))
    if not db.validate():
        for e in db.errors:
            print("error:", e, file=sys.stderr)
        print(f"{len(db.errors)} error(s)", file=sys.stderr)
        return 1
    if a.format:
        with open(os.path.join(DATA, "perks.json"), "w", encoding="utf-8") as f:
            f.write(canonical_json(perks))
    outs = {OUT_H: db.header(), OUT_CSV: db.csv(), OUT_DB: db.doc_database(), OUT_MATRIX: db.doc_matrix(),
            OUT_BUILDS: db.doc_builds()}
    stale = []
    for path, text in outs.items():
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        if old != text:
            stale.append(os.path.relpath(path, ROOT))
            if not a.check:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text)
    st = db.counts()
    print(f"{len(db.perks)} perks: " + ", ".join(f"{s} {st[s]}" for s in STATUSES if st[s]))
    if a.check and stale:
        print("stale (run tools/generate_perk_registry.py): " + ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

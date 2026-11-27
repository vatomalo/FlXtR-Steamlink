#!/usr/bin/env python3
"""Perk database validation (no ROM): python3 -m unittest tests/test_perk_database.py"""
import copy
import json
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import generate_perk_registry as gen  # noqa: E402


def load(name):
    with open(os.path.join(ROOT, "data", "perks", name), encoding="utf-8") as f:
        return json.load(f)


class PerkDatabase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.doc, cls.caps, cls.verified = load("perks.json"), load("capabilities.json"), load("verified.json")

    def db(self, doc=None):
        return gen.Db(doc or copy.deepcopy(self.doc), self.caps, self.verified)

    def broken(self, mutate):
        doc = copy.deepcopy(self.doc)
        mutate(doc)
        db = self.db(doc)
        self.assertFalse(db.validate(), "the broken database validated")
        return db.errors

    def test_valid_and_large_enough(self):
        db = self.db()
        self.assertTrue(db.validate(), "\n".join(db.errors))
        self.assertGreaterEqual(len(self.doc["perks"]), 200)

    def test_unique_ids_and_keys(self):
        ids = [p["id"] for p in self.doc["perks"]]
        keys = [p["key"] for p in self.doc["perks"]]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(len(keys), len(set(keys)))

    def test_every_discipline_has_perks(self):
        per = {d["key"]: 0 for d in self.doc["disciplines"]}
        for p in self.doc["perks"]:
            per[p["discipline"]] += 1
        for k, n in per.items():
            self.assertGreaterEqual(n, 6 if k == "universal" else 12, k)
        chrono = [p for p in self.doc["perks"] if p.get("branch") == "chronomancy"]
        self.assertGreaterEqual(len(chrono), 15)
        for name in ("Temporal Distortion", "Chronostasis", "Psychic Acceleration", "Temporal Field", "Frozen Arsenal",
                     "Temporal Echo", "Borrowed Time", "Time Fracture", "Temporal Pursuit", "Chrono Counter", "Paradox",
                     "Temporal Reversal", "Time Prison", "Time Collapse", "Chrono Sovereignty"):
            self.assertIn(name, {p["name"] for p in chrono})

    def test_disciplines_match_the_proxy(self):
        # som_discipline_names order in src/som_disciplines.h
        with open(os.path.join(ROOT, "src", "som_disciplines.h"), encoding="utf-8") as f:
            src = f.read()
        start = src.index("som_discipline_names[SOM_DISCIPLINE_COUNT]={")
        names = src[start:src.index("};", start)]
        count = names.count('"') // 2
        idx = sorted(d["som_index"] for d in self.doc["disciplines"] if d["som_index"] != 255)
        self.assertEqual(idx, list(range(count)))

    def test_rejects_bad_rows(self):
        def dup_id(d):
            d["perks"][1]["id"] = d["perks"][0]["id"]
        self.assertTrue(any("duplicate id" in e for e in self.broken(dup_id)))

        def bad_disc(d):
            d["perks"][0]["discipline"] = "gunslinger"
        self.assertTrue(any("unknown discipline" in e for e in self.broken(bad_disc)))

        def no_desc(d):
            d["perks"][0]["description"] = ""
        self.assertTrue(any("description" in e for e in self.broken(no_desc)))

        def cost(d):
            d["perks"][0]["capacity_cost"] = 9
        self.assertTrue(any("capacity_cost" in e for e in self.broken(cost)))

        def mastery(d):
            d["perks"][0]["mastery_required"] = 33
        self.assertTrue(any("tier" in e for e in self.broken(mastery)))

        def trig(d):
            d["perks"][0]["triggers"] = ["NOT_AN_EVENT"]
        self.assertTrue(any("unknown trigger" in e for e in self.broken(trig)))

        def fx(d):
            d["perks"][0]["effects"] = [{"type": "teleport_everything"}]
        self.assertTrue(any("unknown effect" in e for e in self.broken(fx)))

        def param(d):
            d["perks"][0]["effects"] = [{"type": "damage_bonus", "pct": 99999}]
        self.assertTrue(any("out of range" in e for e in self.broken(param)))

        def prereq(d):
            d["perks"][0]["prerequisites"] = ["nobody.nothing"]
        self.assertTrue(any("unknown prerequisite" in e for e in self.broken(prereq)))

        def too_few(d):
            d["perks"] = d["perks"][:150]
        self.assertTrue(any("at least 200" in e for e in self.broken(too_few)))

    def test_rejects_circular_prerequisites(self):
        def cycle(d):
            a, b = d["perks"][0], d["perks"][1]
            a["prerequisites"], b["prerequisites"] = [b["key"]], [a["key"]]
        self.assertTrue(any("circular" in e for e in self.broken(cycle)))

    def test_status_never_claims_more_than_the_engine(self):
        db = self.db()
        db.validate()
        for p in self.doc["perks"]:
            st, blockers = db.status(p)
            if st in ("IMPLEMENTED", "SUPPORTED_PENDING_TEST"):
                self.assertEqual(blockers, [], p["key"])
                for kind, name in db.needs(p):
                    self.assertEqual(self.caps[kind][name]["status"], "runtime", (p["key"], name))
            if st == "IMPLEMENTED":
                self.assertIn(p["key"], self.verified["perks"])

    def test_builds_use_real_perks(self):
        self.assertGreaterEqual(len(self.doc["builds"]), 25)
        keys = {p["key"] for p in self.doc["perks"]}
        for b in self.doc["builds"]:
            for k in b["perks"]:
                self.assertIn(k, keys, b["name"])

    def test_generated_files_are_current(self):
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "generate_perk_registry.py"), "--check"],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_canonical_format_round_trips(self):
        self.assertEqual(json.loads(gen.canonical_json(self.doc)), self.doc)


if __name__ == "__main__":
    unittest.main()

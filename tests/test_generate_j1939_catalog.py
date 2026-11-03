"""
Phase 1 generator tests.

    python -m unittest tests/test_generate_j1939_catalog.py

Customer lists are checked against the specification table in
analyze_spn_data.md section 12.6, independently of the generator's
config/j1939_customer_profiles.json input.
"""

import copy
import dataclasses
import re
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_j1939_catalog as gen  # noqa: E402


def spec_customer_matrix():
    """{name: (count, pgn_count, [keys])} from analyze_spn_data.md 12.6."""
    text = (REPO_ROOT / "analyze_spn_data.md").read_text(encoding="utf-8")
    section = text.split("### 12.6 Customer profile matrix", 1)[1].split("###", 1)[0]
    rows = {}
    for line in section.splitlines():
        m = re.match(r"^\|\s*([A-Z]+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|[^|]*\|\s*([\d,\s]+)\|\s*$",
                     line)
        if m:
            keys = [int(k) for k in m.group(4).split(",")]
            rows[m.group(1)] = (int(m.group(2)), int(m.group(3)), keys)
    return rows


class GeneratorTestBase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = gen.load_master(gen.DEFAULT_MASTER)
        cls.policy = gen.load_json(gen.DEFAULT_POLICY)
        cls.network = gen.load_json(gen.DEFAULT_NETWORK)
        cls.profiles = gen.load_json(gen.DEFAULT_PROFILES)
        cls.model = gen.build_catalog(cls.rows, cls.policy, cls.network, cls.profiles)

    def build(self, rows=None, policy=None, network=None, profiles=None):
        return gen.build_catalog(rows if rows is not None else list(self.rows),
                                 policy or copy.deepcopy(self.policy),
                                 network or copy.deepcopy(self.network),
                                 profiles or copy.deepcopy(self.profiles))

    def assert_fails(self, pattern, **kwargs):
        with self.assertRaises(gen.CatalogError) as ctx:
            self.build(**kwargs)
        joined = "\n".join(ctx.exception.errors)
        self.assertRegex(joined, pattern)

    def replace_rows(self, predicate, **changes):
        return [dataclasses.replace(r, **changes) if predicate(r) else r for r in self.rows]


class CatalogContentTests(GeneratorTestBase):
    def test_exactly_179_records(self):
        self.assertEqual(len(self.model["signals"]), 179)
        self.assertEqual([s["key"] for s in self.model["signals_by_key"]], list(range(1, 180)))

    def test_exactly_177_unique_spns(self):
        self.assertEqual(self.model["unique_spn_count"], 177)
        self.assertEqual(self.model["duplicate_spns"],
                         {928: [0xF00B, 0xFEEA], 1807: [0xF009, 0xF01D]})

    def test_exactly_60_pgns(self):
        self.assertEqual(len(self.model["pgns"]), 60)

    def test_transmission_modes(self):
        counts = {}
        for p in self.model["pgns"]:
            counts[p["mode"]] = counts.get(p["mode"], 0) + 1
        self.assertEqual(counts, {"PERIODIC": 45, "ON_REQUEST": 10,
                                  "ON_CHANGE": 4, "ENGINE_SPEED_DEPENDENT": 1})
        # 330 ms contributes 3.03 frame/s, hence 697.03.
        self.assertAlmostEqual(gen.bus_load(self.model["pgns"])["fixed_fps"], 697.0, delta=0.05)

    def test_on_request_demo_broadcast(self):
        # Customer demo decision 2026-11-02: every On-Request PGN is broadcast
        # (EBS21 100 ms, VI 5000 ms like FEE3, others 1000 ms) and keeps its
        # ON_REQUEST mode.
        on_request = {p["pgn"]: p for p in self.model["pgns"] if p["mode"] == "ON_REQUEST"}
        self.assertEqual(on_request[0xFEC6]["period_ms"], 100)
        self.assertEqual(on_request[0xFEC6]["min_interval_ms"], 100)
        self.assertEqual(on_request[0xFEEC]["period_ms"], 5000)
        others = [p for pgn, p in on_request.items() if pgn not in (0xFEC6, 0xFEEC)]
        self.assertEqual(len(others), 8)
        self.assertTrue(all(p["period_ms"] == 1000 for p in others))
        # 697 fixed + EEC1 0.2 + VI 0.2 (5000 ms demo) + 18 demo frame/s.
        self.assertAlmostEqual(gen.bus_load(self.model["pgns"])["fixed_plus_fallback_fps"],
                               715.4, delta=0.05)

    def test_vin_demo_frame(self):
        vin = self.policy["vin"]
        self.assertEqual(vin["demo_transmit_bytes"], 21)
        self.assertTrue(vin["automatic_terminator"])

    def test_on_request_broadcast_without_period_fails(self):
        policy = copy.deepcopy(self.policy)
        del policy["pgn_policies"]["0xFEC6"]["unsolicited_period_ms"]
        self.assert_fails(r"0xFEC6.*unsolicited_period_ms", policy=policy)

    def test_three_long_pgns_are_tp(self):
        tp = {p["pgn"]: p for p in self.model["pgns"] if p["requires_tp"]}
        self.assertEqual(sorted(tp), [0xFEE1, 0xFEE3, 0xFEEC])
        self.assertEqual(tp[0xFEE1]["payload_length"], 18)
        self.assertEqual(tp[0xFEE3]["payload_length"], 21)
        self.assertEqual(tp[0xFEEC]["payload_length"], 200)
        self.assertTrue(tp[0xFEEC]["variable_length"])
        self.assertEqual(sorted(s["key"] for p in tp.values() for s in p["members"]),
                         [3, 36, 37, 107])
        short = [p for p in self.model["pgns"] if not p["requires_tp"]]
        self.assertEqual(len(short), 57)
        self.assertEqual(sum(p["signal_count"] for p in short), 175)
        self.assertTrue(all(p["payload_length"] == 8 for p in short))

    def test_customer_profiles_match_specification(self):
        spec = spec_customer_matrix()
        self.assertEqual(sorted(spec), sorted(["C", "K", "E", "F", "M", "H", "A", "D", "I", "POC"]))
        generated = {e["name"]: e for e in self.model["profiles"]}
        for name, (count, pgn_count, keys) in spec.items():
            with self.subTest(profile=name):
                e = generated[name]
                self.assertEqual(len(keys), count)
                self.assertEqual(e["keys"], sorted(keys))
                self.assertEqual(len(e["keys"]), count)
                self.assertEqual(len(e["pgns"]), pgn_count)
        self.assertEqual(generated["M"]["tp_keys"], [3])
        self.assertEqual(generated["D"]["tp_keys"], [36, 37, 107])
        self.assertEqual(len(generated["ALL"]["keys"]), 179)

    def test_raw_bounds_inside_bit_width(self):
        for s in self.model["signals"]:
            with self.subTest(key=s["key"]):
                self.assertLessEqual(0, s["raw_min"])
                self.assertLessEqual(s["raw_min"], s["raw_max"])
                self.assertLessEqual(s["raw_max"], s["raw_capacity"])
                if s["kind"] == "continuous":
                    self.assertLessEqual(s["raw_max"], gen.operational_raw_max(s["bit_length"]))

    def test_rounded_source_bounds_keep_full_range(self):
        by_key = {s["key"]: s for s in self.model["signals"]}
        self.assertEqual(by_key[144]["raw_max"], 64255)   # 250.996 km/h
        self.assertEqual(by_key[103]["raw_max"], 64255)   # 8031.875 rpm
        self.assertEqual(by_key[13]["raw_max"], 64255)    # yaw rate 3.92 -> 0xFAFF

    def test_dash_repaired_to_ascii(self):
        self.assertEqual(self.model["signals_by_key"][104]["name"],
                         "Engine Demand - Percent Torque")


class FailureTests(GeneratorTestBase):
    def test_unknown_rate_label_fails(self):
        rows = self.replace_rows(lambda r: r.pgn_hex == "0xFEF7", rate="every so often")
        self.assert_fails(r"0xFEF7.*unresolved transmission rule", rows=rows)

    def test_inconsistent_rate_inside_pgn_fails(self):
        rows = list(self.rows)
        i = next(i for i, r in enumerate(rows) if r.pgn_hex == "0xFEF7")
        rows[i] = dataclasses.replace(rows[i], rate="100 ms")
        self.assert_fails(r"0xFEF7: inconsistent rate", rows=rows)

    def test_engine_speed_dependent_without_policy_fails(self):
        policy = copy.deepcopy(self.policy)
        del policy["pgn_policies"]["0xF004"]
        self.assert_fails(r"0xF004.*ENGINE_SPEED_DEPENDENT requires", policy=policy)

    def test_on_request_100ms_without_policy_fails(self):
        policy = copy.deepcopy(self.policy)
        del policy["pgn_policies"]["0xFEC6"]
        self.assert_fails(r"0xFEC6.*unresolved transmission rule 'On request \(100ms\)'",
                          policy=policy)

    def test_on_change_without_policy_fails(self):
        policy = copy.deepcopy(self.policy)
        del policy["pgn_policies"]["0xFEE1"]
        self.assert_fails(r"0xFEE1.*unresolved", policy=policy)

    def test_alias_policy_conflict_fails(self):
        policy = copy.deepcopy(self.policy)
        policy["pgn_policies"]["0xFEF7"] = {"mode": "ON_REQUEST"}
        self.assert_fails(r"0xFEF7.*conflicts", policy=policy)

    def test_overlap_fails(self):
        rows = self.replace_rows(lambda r: r.pgn_hex == "0xFEF7" and r.spn == 115, byte=1)
        self.assert_fails(r"0xFEF7: signal_key \d+ .* overlaps", rows=rows)

    def test_non_integer_byte_fails(self):
        rows = self.replace_rows(lambda r: r.spn == 190, byte=4.5)
        self.assert_fails(r"Byte must be an integer", rows=rows)

    def test_bit_outside_byte_fails(self):
        rows = self.replace_rows(lambda r: r.spn == 190, bit=9)
        self.assert_fails(r"Bit=9 outside 1..8", rows=rows)

    def test_duplicate_pgn_spn_fails(self):
        rows = self.replace_rows(lambda r: r.spn == 115, spn=114)
        self.assert_fails(r"duplicate \(PGN 0xFEF7, SPN 114\)", rows=rows)

    def test_raw_range_exceeding_bit_width_fails(self):
        rows = self.replace_rows(lambda r: r.spn == 5113, physical_range="[0.0, 7.0]")
        self.assert_fails(r"exceeds 2-bit capacity", rows=rows)

    def test_missing_record_fails(self):
        self.assert_fails(r"record count 178 != policy 179", rows=list(self.rows[:-1]))

    def test_customer_count_mismatch_fails(self):
        profiles = copy.deepcopy(self.profiles)
        profiles["profiles"][0]["keys"] = profiles["profiles"][0]["keys"][:-1]
        self.assert_fails(r"profile C: 56 keys != expected 57", profiles=profiles)

    def test_customer_key_out_of_range_fails(self):
        profiles = copy.deepcopy(self.profiles)
        profiles["profiles"][1]["keys"][0] = 180
        self.assert_fails(r"keys outside 1..179", profiles=profiles)

    def test_network_missing_pgn_fails(self):
        network = copy.deepcopy(self.network)
        del network["pgns"]["0xFEF7"]
        self.assert_fails(r"0xFEF7: missing from network config", network=network)

    def test_non_ascii_name_fails(self):
        rows = self.replace_rows(lambda r: r.spn == 190, name="Engine Speed °")
        self.assert_fails(r"non-ASCII name", rows=rows)


class DeterminismTests(GeneratorTestBase):
    def test_render_is_deterministic(self):
        sha = gen.file_sha256(gen.DEFAULT_MASTER)
        first = gen.render_all(self.model, sha)
        second = gen.render_all(self.build(), sha)
        self.assertEqual(first, second)

    def test_crc_tracks_catalog_content(self):
        network = copy.deepcopy(self.network)
        network["pgns"]["0xFEF7"]["priority"] = 5
        changed = self.build(network=network)
        self.assertNotEqual(changed["catalog_crc32"], self.model["catalog_crc32"])

    def test_committed_artifacts_are_up_to_date(self):
        self.assertEqual(gen.main(["--check"]), 0)


if __name__ == "__main__":
    unittest.main()

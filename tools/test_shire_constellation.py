"""Constellation normalization: validation, seed derivation, and link rules."""
import copy
import unittest

from shire_constellation_config import MAX_VEHICLES, derive_seed, normalize_constellation

SPACECRAFT = {"sat-1", "sat-2"}
COMPONENTS = {"adcs", "eps", "radio"}


def base():
    return {
        "constellation_name": "demo",
        "vehicles": [
            {"id": "leaf-1", "spacecraft": "sat-1", "node_id": 101},
            {"id": "relay", "spacecraft": "sat-1", "role": "relay", "node_id": 100},
            {"id": "leaf-2", "spacecraft": "sat-2", "node_id": 102},
        ],
        "links": [
            {"endpoints": ["leaf-1", "relay"]},
            {"endpoints": ["relay", "leaf-2"]},
        ],
    }


def normalize(config):
    return normalize_constellation(config, spacecraft_names=SPACECRAFT, component_names=COMPONENTS)


def broken(mutate):
    config = base()
    mutate(config)
    return config


class ValidConstellationTests(unittest.TestCase):
    def test_defaults_are_applied_and_vehicle_indices_follow_list_order(self):
        result = normalize(base())
        self.assertEqual([(v["id"], v["spacecraft_id"]) for v in result["vehicles"]],
                         [("leaf-1", 0), ("relay", 1), ("leaf-2", 2)])
        self.assertEqual(result["vehicles"][0]["role"], "leaf")
        link = result["links"][0]
        self.assertEqual((link["name"], link["type"], link["delay"], link["drop_ppm"], link["max_range_km"]),
                         ("leaf-1-relay", "crosslink", {"mode": "range"}, 0, 5000.0))

    def test_input_is_not_mutated(self):
        config = base()
        before = copy.deepcopy(config)
        normalize(config)
        self.assertEqual(config, before)

    def test_a_single_vehicle_needs_no_links(self):
        config = {"constellation_name": "solo", "vehicles": [{"id": "a", "spacecraft": "sat-1", "node_id": 1}]}
        self.assertEqual(normalize(config)["links"], [])

    def test_fixed_delay_and_disruptions_normalize_and_sort(self):
        def mutate(config):
            config["links"][0]["delay"] = {"mode": "fixed", "ms": 40}
            config["links"][0]["disruptions"] = [
                {"type": "drop", "at_s": 50, "until_s": 60, "drop_ppm": 5000},
                {"type": "outage", "at_s": 10},
            ]
        link = normalize(broken(mutate))["links"][0]
        self.assertEqual(link["delay"], {"mode": "fixed", "ms": 40})
        self.assertEqual([(d["type"], d["at_s"], d["until_s"]) for d in link["disruptions"]],
                         [("outage", 10.0, None), ("drop", 50.0, 60.0)])
        self.assertEqual(link["disruptions"][1]["drop_ppm"], 5000)

    def test_overrides_are_kept_per_vehicle(self):
        config = broken(lambda c: c["vehicles"][0].update(overrides={"radio": {"uplink_delay_ms": 5}}))
        self.assertEqual(normalize(config)["vehicles"][0]["overrides"], {"radio": {"uplink_delay_ms": 5}})


class SeedTests(unittest.TestCase):
    def test_seeds_are_stable_nonzero_and_independent_per_direction(self):
        links = [normalize(base())["links"][0] for _ in range(2)]
        self.assertEqual(links[0]["seeds"], links[1]["seeds"])
        forward, backward = links[0]["seeds"].values()
        self.assertNotEqual(forward, backward)
        self.assertTrue(all(0 < seed < 2**32 for seed in (forward, backward)))

    def test_master_seed_changes_every_stream(self):
        first = normalize(base())
        second = normalize(broken(lambda c: c.update(master_seed=2)))
        for one, other in zip(first["links"], second["links"]):
            self.assertTrue(all(one["seeds"][k] != other["seeds"][k] for k in one["seeds"]))

    def test_a_link_seed_overrides_the_master_seed(self):
        pinned = broken(lambda c: c["links"][0].update(seed=7))
        self.assertEqual(normalize(pinned)["links"][0]["seeds"],
                         {"leaf-1_to_relay": derive_seed(7, "leaf-1-relay", "leaf-1", "relay"),
                          "relay_to_leaf-1": derive_seed(7, "leaf-1-relay", "relay", "leaf-1")})


class RejectionTests(unittest.TestCase):
    def assertRejects(self, mutate, pattern):
        with self.assertRaisesRegex(ValueError, pattern):
            normalize(broken(mutate))

    def test_unknown_keys_are_rejected_at_every_level(self):
        self.assertRejects(lambda c: c.update(vehicels=[]), "unknown key.*vehicels")
        self.assertRejects(lambda c: c["vehicles"][0].update(nodeid=1), "unknown key.*nodeid")
        self.assertRejects(lambda c: c["links"][0].update(max_rnage_km=1), "unknown key.*max_rnage_km")
        self.assertRejects(lambda c: c["links"][0].update(disruptions=[{"type": "outage", "at_s": 0, "x": 1}]),
                           "unknown key.*'x'")

    def test_vehicle_rules(self):
        self.assertRejects(lambda c: c["vehicles"][0].update(id="Leaf_1"), "id must be lowercase")
        self.assertRejects(lambda c: c["vehicles"][0].update(spacecraft="sat-9"), "not a registered spacecraft")
        self.assertRejects(lambda c: c["vehicles"][0].update(role="boss"), "role must be one of")
        self.assertRejects(lambda c: c["vehicles"][0].pop("node_id"), "node_id must be an integer")
        self.assertRejects(lambda c: c["vehicles"][0].update(node_id=0), "node_id must be an integer")
        self.assertRejects(lambda c: c["vehicles"][1].update(id="leaf-1"), "id 'leaf-1' is used more than once")
        self.assertRejects(lambda c: c["vehicles"][1].update(node_id=101), "node_id 101 is used more than once")
        self.assertRejects(lambda c: c["vehicles"][0].update(overrides={"warp": {}}), "unknown component 'warp'")
        self.assertRejects(lambda c: c["vehicles"][0].update(overrides={"radio": 3}), "overrides.radio must be a mapping")

    def test_vehicle_count_limits(self):
        self.assertRejects(lambda c: c.update(vehicles=[]), "nonempty list")
        many = [{"id": f"v{i}", "spacecraft": "sat-1", "node_id": i + 1} for i in range(MAX_VEHICLES + 1)]
        self.assertRejects(lambda c: c.update(vehicles=many, links=[]), "at most 16")

    def test_link_rules(self):
        self.assertRejects(lambda c: c["links"][0].update(endpoints=["leaf-1"]), "list of two vehicle ids")
        self.assertRejects(lambda c: c["links"][0].update(endpoints=["leaf-1", "ghost"]), "'ghost' is not a vehicle")
        self.assertRejects(lambda c: c["links"][0].update(endpoints=["relay", "relay"]), "two different vehicles")
        self.assertRejects(lambda c: c["links"][0].update(type="laser"), "type must be one of")
        self.assertRejects(lambda c: c["links"][0].update(drop_ppm=2_000_000), "drop_ppm must be an integer")
        self.assertRejects(lambda c: c["links"][0].update(max_range_km=0), "max_range_km must be finite")
        self.assertRejects(lambda c: c["links"][1].update(endpoints=["relay", "leaf-1"], name="other"),
                           "more than one link")
        self.assertRejects(lambda c: c["links"][1].update(name="leaf-1-relay"), "name 'leaf-1-relay' is used more than once")

    def test_delay_rules(self):
        self.assertRejects(lambda c: c["links"][0].update(delay="fast"), "'range' or a mapping")
        self.assertRejects(lambda c: c["links"][0].update(delay={"mode": "range", "ms": 5}), "range delay does not take ms")
        self.assertRejects(lambda c: c["links"][0].update(delay={"mode": "warp"}), "delay mode must be one of")

    def test_disruption_rules(self):
        def with_disruptions(*items):
            return lambda c: c["links"][0].update(disruptions=list(items))
        self.assertRejects(with_disruptions({"type": "flood", "at_s": 0}), "type must be one of")
        self.assertRejects(with_disruptions({"type": "outage", "at_s": -1}), "at_s must be finite")
        self.assertRejects(with_disruptions({"type": "outage", "at_s": 5, "until_s": 5}), "until_s must be after at_s")
        self.assertRejects(with_disruptions({"type": "drop", "at_s": 0}), "drop_ppm must be an integer")
        self.assertRejects(with_disruptions({"type": "outage", "at_s": 0, "drop_ppm": 1}), "does not take drop_ppm")
        self.assertRejects(with_disruptions({"type": "delay", "at_s": 0, "delay_ms": 1, "drop_ppm": 1}),
                           "does not take drop_ppm")

    def test_overlapping_windows_of_one_type_are_rejected_but_different_types_may_overlap(self):
        overlap = [{"type": "outage", "at_s": 0, "until_s": 10}, {"type": "outage", "at_s": 5}]
        self.assertRejects(lambda c: c["links"][0].update(disruptions=overlap), "overlapping outage")
        mixed = [{"type": "outage", "at_s": 0, "until_s": 10}, {"type": "delay", "at_s": 5, "delay_ms": 3}]
        normalize(broken(lambda c: c["links"][0].update(disruptions=mixed)))
        touching = [{"type": "outage", "at_s": 0, "until_s": 10}, {"type": "outage", "at_s": 10, "until_s": 20}]
        normalize(broken(lambda c: c["links"][0].update(disruptions=touching)))

    def test_every_vehicle_must_be_reachable_over_links(self):
        self.assertRejects(lambda c: c.update(links=[c["links"][0]]), "'leaf-2'.*no chain of links")
        self.assertRejects(lambda c: c.update(links=[]), "no chain of links")

    def test_top_level_rules(self):
        self.assertRejects(lambda c: c.pop("constellation_name"), "constellation_name must be")
        self.assertRejects(lambda c: c.update(master_seed=-1), "master_seed must be an integer")
        with self.assertRaisesRegex(ValueError, "must be a mapping"):
            normalize([])


if __name__ == "__main__":
    unittest.main()

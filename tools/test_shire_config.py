"""Shared component configuration cascade used by the orchestrator and the campaign runner."""
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

import yaml

from shire_config import (CFG_DIR, ConfigError, apply_vehicle, cascade_component, deep_merge, load_mission,
                          resolve_component_configs, select_components, select_vehicle, vehicle_initial_conditions)

COMPONENTS = [{"name": "demo"}, {"name": "adcs"}]


def cascade(name="demo", fallback=None, **layers):
    base = dict(global_cfg={}, mission_cfg={}, spacecraft_cfg={}, ic_cfg={}, scenario_cfg={}, ground_stations=[])
    base.update(layers)
    return cascade_component(name, COMPONENTS, fallback or {}, **base)


class SelectComponentsTests(unittest.TestCase):
    def test_spacecraft_components_win_over_mission_components(self):
        self.assertEqual(select_components({"components": [{"name": "a"}]}, {"components": [{"name": "b"}]}),
                         [{"name": "b"}])

    def test_mission_components_are_the_fallback_without_a_spacecraft(self):
        self.assertEqual(select_components({"components": [{"name": "a"}]}, {}), [{"name": "a"}])
        self.assertEqual(select_components({"components": [{"name": "a"}]}, None), [{"name": "a"}])

    def test_spacecraft_without_components_selects_none(self):
        self.assertEqual(select_components({"components": [{"name": "a"}]}, {"spacecraft_name": "x"}), [])


class CascadeTests(unittest.TestCase):
    def test_each_layer_overrides_the_one_below_it(self):
        layers = [("global_cfg", "global"), ("mission_cfg", "mission"), ("spacecraft_cfg", "spacecraft"),
                  ("ic_cfg", "ic"), ("scenario_cfg", "scenario")]
        values = {"global_cfg": {"demo": {"x": "global"}}, "mission_cfg": {"demo": {"x": "mission"}},
                  "spacecraft_cfg": {"demo": {"x": "spacecraft"}},
                  "ic_cfg": {"component_overrides": {"demo": {"x": "ic"}}},
                  "scenario_cfg": {"demo": {"x": "scenario"}}}
        expected = ["fallback"] + [name for _, name in layers]
        for count in range(len(layers) + 1):
            with self.subTest(layers=count):
                active = {key: values[key] for key, _ in layers[:count]}
                self.assertEqual(cascade(fallback={"x": "fallback"}, **active)["x"], expected[count])

    def test_scenario_overrides_beat_the_scenario_component_block(self):
        scenario = {"demo": {"x": "block"}, "overrides": {"x": "override"}}
        self.assertEqual(cascade(scenario_cfg=scenario)["x"], "override")

    def test_null_layers_are_ignored(self):
        result = cascade(fallback={"x": 1}, global_cfg={"demo": None}, scenario_cfg={"overrides": None})
        self.assertEqual(result, {"x": 1})

    def test_cli_debug_applies_only_to_the_cli_component(self):
        self.assertTrue(cascade("demo", cli_debug=True, cli_component="demo")["debug"])
        self.assertNotIn("debug", cascade("adcs", cli_debug=True, cli_component="demo"))
        self.assertNotIn("debug", cascade("demo", cli_debug=False, cli_component="demo"))

    def test_cascade_does_not_mutate_the_fallback(self):
        fallback = {"x": 1}
        cascade(fallback=fallback, scenario_cfg={"demo": {"x": 2}})
        self.assertEqual(fallback, {"x": 1})

    def test_normalizer_errors_surface_as_value_error(self):
        with self.assertRaises(ValueError):
            cascade_component("radio", [{"name": "radio"}], {"link_mode": "bogus"}, global_cfg={}, mission_cfg={},
                              spacecraft_cfg={}, ic_cfg={}, scenario_cfg={}, ground_stations=[])


class ResolveComponentConfigsTests(unittest.TestCase):
    def test_retain_all_events_adds_the_fsw_pseudo_component(self):
        resolved = resolve_component_configs({}, {"components": []}, {}, {"retain_all_events": True}, {}, ground_stations=[])
        self.assertEqual(resolved["__fsw"], {"event_burst_max": 1000000, "event_refill_per_sec": 1000000})

    def test_no_pseudo_components_without_events_or_limits(self):
        self.assertEqual(resolve_component_configs({}, {"components": []}, {}, {}, {}, ground_stations=[]), {})

    def test_unnamed_components_are_skipped(self):
        self.assertEqual(resolve_component_configs({}, {"components": [{}]}, {}, {}, {}, ground_stations=[]), {})


class MissionRegistryTests(unittest.TestCase):
    def test_every_registered_mission_resolves_with_its_own_directory_and_catalog(self):
        from shire_config import load_global_config
        names = [m["name"] for m in load_global_config()["build"]["missions"]]
        self.assertEqual(names, ["drm", "lunar-relay"])
        for name in names:
            with self.subTest(mission=name):
                mission = load_mission(name)
                self.assertEqual(mission.directory, CFG_DIR / name)
                self.assertTrue(mission.ground_stations())
                self.assertTrue(mission.scenario_names())
                for scenario in mission.scenario_names():
                    mission.load_scenario(scenario)

    def test_missions_do_not_share_ground_stations_or_scenarios(self):
        drm, lunar = load_mission("drm"), load_mission("lunar-relay")
        self.assertFalse({s["name"] for s in drm.ground_stations()} & {s["name"] for s in lunar.ground_stations()})
        self.assertEqual(lunar.scenario_names(), ["smoke"])
        self.assertNotIn("smoke", drm.scenario_names())
        with self.assertRaisesRegex(ConfigError, "Scenario 'smoke'"):
            drm.load_scenario("smoke")
        with self.assertRaisesRegex(ConfigError, "Scenario 'nominal'"):
            lunar.load_scenario("nominal")

    def test_the_lunar_mission_orbits_the_moon_and_the_drm_orbits_the_earth(self):
        lunar_ic = load_mission("lunar-relay").load_initial_conditions("lunar-baseline")[2]
        drm_ic = load_mission("drm").load_initial_conditions("nominal-baseline")[2]
        self.assertEqual(lunar_ic["orbit"]["center"], "LUNA")
        self.assertNotIn("center", drm_ic["orbit"])

    def test_the_lunar_catalog_has_earth_stations_and_a_lunar_surface_site(self):
        worlds = {s["world"] for s in load_mission("lunar-relay").ground_stations()}
        self.assertEqual(worlds, {"EARTH", "LUNA"})


class RepositoryMissionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.drm = load_mission("drm")

    def test_directory_is_the_missions_own_directory(self):
        self.assertEqual(self.drm.directory, CFG_DIR / "drm")
        self.assertTrue(self.drm.initial_conditions_dir.is_dir())
        self.assertTrue(self.drm.campaigns_dir.is_dir())
        self.assertTrue(self.drm.ground_stations_path.is_file())

    def test_registered_names_resolve(self):
        self.assertIn("nominal", self.drm.scenario_names())
        self.assertIn("sat-1", self.drm.spacecraft_names())
        self.assertEqual(self.drm.load_scenario("nominal")["scenario_name"], "nominal")
        self.assertIn("components", self.drm.load_spacecraft("sat-1"))
        self.assertTrue(self.drm.ground_stations())

    def test_every_registered_scenario_and_spacecraft_loads(self):
        for name in self.drm.scenario_names():
            with self.subTest(scenario=name):
                self.assertIsInstance(self.drm.load_scenario(name), dict)
        for name in self.drm.spacecraft_names():
            with self.subTest(spacecraft=name):
                self.assertIsInstance(self.drm.load_spacecraft(name), dict)

    def test_every_scenario_names_an_initial_condition_that_exists(self):
        for name in self.drm.scenario_names():
            with self.subTest(scenario=name):
                scenario = self.drm.load_scenario(name)
                self.drm.load_initial_conditions(scenario.get("initial_conditions"))

    def test_no_spacecraft_selection_is_an_empty_config(self):
        self.assertEqual(self.drm.load_spacecraft(None), {})

    def test_errors_list_what_is_available(self):
        lunar = load_mission("lunar-relay")
        with self.assertRaisesRegex(ConfigError, r"Spacecraft 'sat-1' not found in mission 'lunar-relay' config\. "
                                                 r"Available: leaf, relay\."):
            lunar.load_spacecraft("sat-1")
        with self.assertRaisesRegex(ConfigError, r"Scenario 'nominal' not found.*Available: smoke\."):
            lunar.load_scenario("nominal")
        with self.assertRaisesRegex(ConfigError, r"Mission 'moon' not found in global config\. "
                                                 r"Available: drm, lunar-relay\."):
            load_mission("moon")

    def test_an_unselected_scenario_says_so_instead_of_naming_none(self):
        lunar = load_mission("lunar-relay")
        for blank in (None, ""):
            with self.subTest(blank=blank):
                with self.assertRaisesRegex(ConfigError, r"No scenario is selected\. Scenarios in mission "
                                                         r"'lunar-relay': smoke\."):
                    lunar.load_scenario(blank)

    def test_unknown_names_raise_config_error(self):
        with self.assertRaisesRegex(ConfigError, "Mission 'nope'"):
            load_mission("nope")
        with self.assertRaisesRegex(ConfigError, "Spacecraft 'nope'"):
            self.drm.load_spacecraft("nope")
        with self.assertRaisesRegex(ConfigError, "Scenario 'nope'"):
            self.drm.load_scenario("nope")

    def test_initial_conditions_default_and_explicit_file(self):
        label, path, config = self.drm.load_initial_conditions(None)
        self.assertEqual((label, path.name), ("nominal-baseline", "nominal-baseline.yaml"))
        self.assertIn("orbit", config)
        _, explicit_path, explicit = self.drm.load_initial_conditions("eclipse-entry", path)
        self.assertEqual(explicit_path, path)
        self.assertEqual(explicit, config)

    def test_missing_initial_condition_raises_config_error(self):
        with self.assertRaisesRegex(ConfigError, "Initial condition bin 'nope'"):
            self.drm.load_initial_conditions("nope")
        with self.assertRaisesRegex(ConfigError, "Initial condition file"):
            self.drm.load_initial_conditions(None, "/nonexistent/ic.yaml")


class RelocatedMissionTests(unittest.TestCase):
    def test_lookups_follow_the_missions_directory_not_cfg_drm(self):
        with tempfile.TemporaryDirectory() as temporary:
            cfg = Path(temporary)
            (cfg / "moon/initial_conditions").mkdir(parents=True)
            (cfg / "moon/campaigns").mkdir()
            (cfg / "shire-config.yaml").write_text(yaml.safe_dump(
                {"build": {"missions": [{"name": "moon", "config_file": "moon/moon.yaml"}]}}))
            (cfg / "moon/moon.yaml").write_text(yaml.safe_dump({
                "spacecraft": [{"name": "relay", "config_file": "moon/relay.yaml"}],
                "scenarios": [{"name": "pass", "config_file": "moon/pass.yaml"}]}))
            (cfg / "moon/relay.yaml").write_text(yaml.safe_dump({"spacecraft_name": "relay"}))
            (cfg / "moon/pass.yaml").write_text(yaml.safe_dump({"scenario_name": "pass", "initial_conditions": "lunar"}))
            (cfg / "moon/initial_conditions/lunar.yaml").write_text(yaml.safe_dump({"orbit": {"inclination_deg": 90}}))

            moon = load_mission("moon", cfg_dir=cfg)
            self.assertEqual(moon.directory, cfg / "moon")
            self.assertEqual(moon.campaigns_dir, cfg / "moon/campaigns")
            self.assertEqual(moon.load_spacecraft("relay"), {"spacecraft_name": "relay"})
            scenario = moon.load_scenario("pass")
            _, _, ic = moon.load_initial_conditions(scenario["initial_conditions"])
            self.assertEqual(ic["orbit"]["inclination_deg"], 90)
            self.assertEqual(moon.ground_stations(), [])

    def test_a_registered_scenario_with_a_missing_file_raises_config_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            cfg = Path(temporary)
            (cfg / "m").mkdir()
            (cfg / "shire-config.yaml").write_text(yaml.safe_dump(
                {"build": {"missions": [{"name": "m", "config_file": "m/m.yaml"}]}}))
            (cfg / "m/m.yaml").write_text(yaml.safe_dump({"scenarios": [{"name": "s", "config_file": "m/s.yaml"}]}))
            with self.assertRaisesRegex(ConfigError, "Could not load scenario config for 's'"):
                load_mission("m", cfg_dir=cfg).load_scenario("s")


class DeepMergeTests(unittest.TestCase):
    def test_nested_mappings_merge_and_scalars_and_lists_replace(self):
        base = {"a": {"x": 1, "y": {"z": 1}}, "list": [1, 2], "keep": True}
        merged = deep_merge(base, {"a": {"y": {"w": 2}, "x": 9}, "list": [3]})
        self.assertEqual(merged, {"a": {"x": 9, "y": {"z": 1, "w": 2}}, "list": [3], "keep": True})

    def test_replace_marker_replaces_the_mapping_and_is_removed(self):
        merged = deep_merge({"a": {"x": 1, "y": 2}}, {"a": {"_replace": True, "z": 3}})
        self.assertEqual(merged, {"a": {"z": 3}})

    def test_replace_marker_inside_a_new_key_is_removed_too(self):
        self.assertEqual(deep_merge({}, {"a": {"_replace": True, "z": 3}}), {"a": {"z": 3}})

    def test_inputs_are_not_mutated_and_results_do_not_alias_them(self):
        base, override = {"a": {"x": [1]}}, {"b": {"y": [2]}}
        merged = deep_merge(base, override)
        merged["a"]["x"].append(9)
        merged["b"]["y"].append(9)
        self.assertEqual((base, override), ({"a": {"x": [1]}}, {"b": {"y": [2]}}))


class VehicleInitialConditionTests(unittest.TestCase):
    IC = {"epoch": {"year": 2023}, "orbit": {"inclination_deg": 52.0, "true_anomaly_deg": 200.0},
          "attitude": {"quaternion": [0, 0, 0, 1]}, "component_overrides": {"eps": {"soc": 0.9}},
          "vehicles": {"relay": {"orbit": {"true_anomaly_deg": 100.0}, "component_overrides": {"eps": {"soc": 0.5}}}}}

    def test_a_vehicle_entry_overlays_only_the_fields_it_names(self):
        ic = vehicle_initial_conditions(self.IC, "relay")
        self.assertEqual(ic["orbit"], {"inclination_deg": 52.0, "true_anomaly_deg": 100.0})
        self.assertEqual(ic["component_overrides"], {"eps": {"soc": 0.5}})
        self.assertEqual(ic["epoch"], {"year": 2023})
        self.assertNotIn("vehicles", ic)

    def test_a_vehicle_without_an_entry_gets_the_top_level_values(self):
        ic = vehicle_initial_conditions(self.IC, "leaf-1")
        self.assertEqual(ic["orbit"]["true_anomaly_deg"], 200.0)
        self.assertEqual(ic["component_overrides"], {"eps": {"soc": 0.9}})

    def test_a_single_vehicle_ic_comes_back_unchanged(self):
        single = {k: v for k, v in self.IC.items() if k != "vehicles"}
        self.assertEqual(vehicle_initial_conditions(single, "anything"), single)

    def test_the_shipped_formation_gives_each_vehicle_its_own_orbit_about_the_moon(self):
        _, _, formation = load_mission("lunar-relay").load_initial_conditions("lunar-formation")
        orbits = {v: vehicle_initial_conditions(formation, v)["orbit"] for v in ("leaf-1", "relay", "leaf-2")}
        self.assertEqual({o["center"] for o in orbits.values()}, {"LUNA"})
        self.assertEqual(len({repr(sorted(o.items())) for o in orbits.values()}), 3)
        self.assertEqual(orbits["relay"]["apoapsis_alt_km"], 8000.0)
        self.assertEqual(orbits["leaf-1"]["apoapsis_alt_km"], 100.0)


class ExtendsTests(unittest.TestCase):
    def mission(self, temporary, spacecraft):
        cfg = Path(temporary)
        (cfg / "m").mkdir()
        (cfg / "shire-config.yaml").write_text(yaml.safe_dump(
            {"build": {"missions": [{"name": "m", "config_file": "m/m.yaml"}]}}))
        (cfg / "m/m.yaml").write_text(yaml.safe_dump(
            {"spacecraft": [{"name": n, "config_file": f"m/{n}.yaml"} for n in spacecraft]}))
        for name, body in spacecraft.items():
            (cfg / f"m/{name}.yaml").write_text(yaml.safe_dump(body))
        return load_mission("m", cfg_dir=cfg)

    def test_a_child_deep_merges_over_its_base_and_replaces_lists(self):
        with tempfile.TemporaryDirectory() as temporary:
            mission = self.mission(temporary, {
                "base": {"spacecraft_name": "base", "spacecraft_id": 1,
                         "components": [{"name": "eps"}, {"name": "radio"}],
                         "radio": {"spi_bus": 1, "spi_cs": 0, "gpio_power_pin": 10},
                         "eps": {"i2c_device_addr": 30}},
                "relay": {"extends": "base", "spacecraft_name": "relay", "spacecraft_id": 7,
                          "components": [{"name": "eps"}, {"name": "radio"}, {"name": "crosslink"}],
                          "radio": {"spi_cs": 2}}})
            relay = mission.load_spacecraft("relay")
            self.assertNotIn("extends", relay)
            self.assertEqual((relay["spacecraft_name"], relay["spacecraft_id"]), ("relay", 7))
            self.assertEqual(relay["radio"], {"spi_bus": 1, "spi_cs": 2, "gpio_power_pin": 10})
            self.assertEqual(relay["eps"], {"i2c_device_addr": 30})
            self.assertEqual([c["name"] for c in relay["components"]], ["eps", "radio", "crosslink"])
            self.assertEqual(mission.load_spacecraft("base")["components"], [{"name": "eps"}, {"name": "radio"}])

    def test_extends_chains_resolve_through_every_level(self):
        with tempfile.TemporaryDirectory() as temporary:
            mission = self.mission(temporary, {"a": {"x": 1, "y": 1}, "b": {"extends": "a", "y": 2},
                                               "c": {"extends": "b", "z": 3}})
            self.assertEqual(mission.load_spacecraft("c"), {"x": 1, "y": 2, "z": 3})

    def test_cycles_and_unknown_bases_raise_config_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            mission = self.mission(temporary, {"a": {"extends": "b"}, "b": {"extends": "a"}, "c": {"extends": "ghost"}})
            with self.assertRaisesRegex(ConfigError, "cycle: a -> b -> a"):
                mission.load_spacecraft("a")
            with self.assertRaisesRegex(ConfigError, "Spacecraft 'ghost'"):
                mission.load_spacecraft("c")

    def test_drm_spacecraft_do_not_extend_anything_so_resolution_is_unchanged(self):
        drm = load_mission("drm")
        for name in drm.spacecraft_names():
            self.assertNotIn("extends", drm.load_spacecraft(name))

    def test_the_lunar_relay_extends_the_leaf_and_changes_only_what_differs(self):
        lunar = load_mission("lunar-relay")
        leaf, relay = lunar.load_spacecraft("leaf"), lunar.load_spacecraft("relay")
        self.assertNotIn("extends", relay)
        self.assertEqual((relay["spacecraft_name"], relay["spacecraft_id"]), ("relay", 2))
        self.assertEqual(relay["components"], leaf["components"])
        self.assertEqual(relay["adcs"], leaf["adcs"])
        self.assertEqual(relay["visualization"], leaf["visualization"])
        self.assertEqual((relay["eps"]["i2c_device_addr"], leaf["eps"]["i2c_device_addr"]), (0x1F, 0x1E))
        self.assertEqual(relay["eps"]["i2c_speed"], leaf["eps"]["i2c_speed"])
        self.assertEqual(relay["radio"]["spi_bus"], leaf["radio"]["spi_bus"])
        self.assertEqual((relay["radio"]["spi_cs"], leaf["radio"]["spi_cs"]), (1, 0))
        self.assertEqual(relay["radio"]["uplink_delay_ms"], 1282)
        self.assertNotIn("uplink_delay_ms", leaf["radio"])


class ConstellationLoadingTests(unittest.TestCase):
    def test_the_shipped_constellation_loads_through_the_lunar_mission_and_not_the_drm(self):
        lunar = load_mission("lunar-relay")
        self.assertIn("crosslink-demo", lunar.constellation_names())
        demo = lunar.load_constellation("crosslink-demo")
        self.assertEqual([(v["id"], v["spacecraft"], v["role"]) for v in demo["vehicles"]],
                         [("leaf-1", "leaf", "leaf"), ("relay", "relay", "relay"), ("leaf-2", "leaf", "leaf")])
        self.assertEqual(demo["initial_conditions"], "lunar-formation")
        self.assertEqual(load_mission("drm").constellation_names(), [])

    def test_validation_failures_surface_as_config_error_naming_the_constellation(self):
        lunar = load_mission("lunar-relay")
        without_spacecraft = replace(lunar, mission_cfg={**lunar.mission_cfg, "spacecraft": []})
        with self.assertRaisesRegex(ConfigError, "Constellation 'crosslink-demo': .*not a registered spacecraft"):
            without_spacecraft.load_constellation("crosslink-demo")
        with self.assertRaisesRegex(ConfigError, "Constellation 'nope'"):
            lunar.load_constellation("nope")

class VehicleSelectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lunar = load_mission("lunar-relay")
        cls.constellation = cls.lunar.load_constellation("crosslink-demo")
        _, _, cls.formation = cls.lunar.load_initial_conditions("lunar-formation")

    def test_a_vehicle_is_found_by_id(self):
        self.assertEqual(select_vehicle(self.constellation, "relay")["node_id"], 100)

    def test_a_missing_or_unknown_vehicle_lists_the_choices(self):
        with self.assertRaisesRegex(ConfigError, "No vehicle is selected. Vehicles in constellation 'crosslink-demo': "
                                                 "leaf-1, relay, leaf-2"):
            select_vehicle(self.constellation, None)
        with self.assertRaisesRegex(ConfigError, "Vehicle 'moon' not found in constellation 'crosslink-demo'. "
                                                 "Available: leaf-1, relay, leaf-2"):
            select_vehicle(self.constellation, "moon")

    def test_two_vehicles_of_one_spacecraft_type_get_their_own_node_numbers(self):
        leaf = self.lunar.load_spacecraft("leaf")
        first, _ = apply_vehicle(leaf, self.formation, select_vehicle(self.constellation, "leaf-1"))
        second, _ = apply_vehicle(leaf, self.formation, select_vehicle(self.constellation, "leaf-2"))
        self.assertEqual((first["dtn"]["node_id"], second["dtn"]["node_id"]), (101, 102))
        self.assertEqual(leaf["dtn"]["node_id"], 101)  # the type is untouched
        self.assertEqual(first["radio"], second["radio"])

    def test_each_vehicle_starts_in_its_own_orbit(self):
        relay = self.lunar.load_spacecraft("relay")
        orbits = {}
        for vid in ("leaf-1", "relay", "leaf-2"):
            _, ic = apply_vehicle(relay, self.formation, select_vehicle(self.constellation, vid))
            orbits[vid] = ic["orbit"]
            self.assertNotIn("vehicles", ic)
            self.assertEqual(ic["orbit"]["center"], "LUNA")
        self.assertEqual(orbits["relay"]["apoapsis_alt_km"], 8000.0)
        self.assertEqual((orbits["leaf-1"]["true_anomaly_deg"], orbits["leaf-2"]["true_anomaly_deg"]), (0.0, 180.0))

    def test_overrides_merge_over_the_spacecraft_settings_key_by_key(self):
        vehicle = {"id": "x", "spacecraft": "leaf", "node_id": 7, "overrides": {"radio": {"spi_cs": 3}, "eps": {"soc": 1}}}
        spacecraft = {"radio": {"spi_bus": 1, "spi_cs": 0}, "dtn": {"node_id": 1}}
        specialized, _ = apply_vehicle(spacecraft, {"orbit": {}}, vehicle)
        self.assertEqual(specialized["radio"], {"spi_bus": 1, "spi_cs": 3})
        self.assertEqual(specialized["eps"], {"soc": 1})
        self.assertEqual(specialized["dtn"], {"node_id": 7})
        self.assertEqual(spacecraft["radio"]["spi_cs"], 0)

    def test_a_spacecraft_without_dtn_is_not_given_one(self):
        specialized, _ = apply_vehicle({"radio": {}}, {"orbit": {}}, {"id": "x", "node_id": 7, "overrides": {}})
        self.assertNotIn("dtn", specialized)


if __name__ == "__main__":
    unittest.main()

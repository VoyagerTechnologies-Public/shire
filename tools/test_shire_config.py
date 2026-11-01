"""Shared component configuration cascade used by the orchestrator and the campaign runner."""
import tempfile
import unittest
from pathlib import Path

import yaml

from shire_config import (CFG_DIR, ConfigError, cascade_component, load_mission, resolve_component_configs,
                          select_components)

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
        resolved = resolve_component_configs({}, {"components": []}, {}, {"retain_all_events": True}, {})
        self.assertEqual(resolved["__fsw"], {"event_burst_max": 1000000, "event_refill_per_sec": 1000000})

    def test_no_pseudo_components_without_events_or_limits(self):
        self.assertEqual(resolve_component_configs({}, {"components": []}, {}, {}, {}), {})

    def test_unnamed_components_are_skipped(self):
        self.assertEqual(resolve_component_configs({}, {"components": [{}]}, {}, {}, {}), {})


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


if __name__ == "__main__":
    unittest.main()

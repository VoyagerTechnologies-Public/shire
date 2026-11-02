"""cfg-check: the real tree is clean, and each class of mistake is reported."""
from pathlib import Path
import shutil
import tempfile
import unittest

import yaml

from shire_config_check import check_repository, wiring_resources
from shire_provenance import ROOT


class CheckRepositoryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.cfg = Path(temporary.name) / "cfg"
        shutil.copytree(ROOT / "cfg", self.cfg, ignore=shutil.ignore_patterns("__pycache__"))

    def edit(self, relative, mutate):
        path = self.cfg / relative
        data = yaml.safe_load(path.read_text())
        mutate(data)
        path.write_text(yaml.safe_dump(data, sort_keys=False))

    def problems(self):
        return check_repository(self.cfg, ROOT)

    def assertReported(self, pattern):
        found = self.problems()
        self.assertTrue(any(pattern in line for line in found), f"{pattern!r} not in {found}")
        return found

    def test_the_shipped_configuration_is_clean(self):
        self.assertEqual(check_repository(), [])
        self.assertEqual(self.problems(), [])

    def test_a_misspelled_scenario_key_is_reported(self):
        self.edit("drm/scenarios/drm-nominal.yaml", lambda d: d.update(run_duraton_s=d.pop("run_duration_s")))
        self.assertReported("scenario 'nominal': unknown key(s) run_duraton_s")

    def test_component_blocks_are_valid_scenario_keys(self):
        self.edit("drm/scenarios/drm-nominal.yaml", lambda d: d.update(radio={"link_mode": "always"}))
        self.assertEqual(self.problems(), [])

    def test_unknown_spacecraft_and_mission_keys_are_reported(self):
        self.edit("drm/spacecraft/sat-1.yaml", lambda d: d.update(spacecraft_idd=1))
        self.edit("drm/drm.yaml", lambda d: d.update(scenarioz=[]))
        self.assertReported("spacecraft 'sat-1': unknown key(s) spacecraft_idd")
        self.assertReported("mission 'drm': unknown key(s) scenarioz")

    def test_duplicate_spacecraft_ids_are_reported(self):
        self.edit("drm/spacecraft/sat-2.yaml", lambda d: d.update(spacecraft_id=1))
        self.assertReported("spacecraft 'sat-2': spacecraft_id 1 is already used by 'sat-1'")

    def test_a_spacecraft_name_that_differs_from_its_registration_is_reported(self):
        self.edit("drm/spacecraft/sat-2.yaml", lambda d: d.update(spacecraft_name="sat-two"))
        self.assertReported("spacecraft 'sat-2': spacecraft_name is 'sat-two'")

    def test_two_components_on_the_same_hardware_are_reported(self):
        self.edit("drm/spacecraft/sat-1.yaml", lambda d: d["demo"].update(handle=d["adcs"]["handle"]))
        self.assertReported("spacecraft 'sat-1': demo and adcs both use uart (2,)")
        self.edit("drm/spacecraft/sat-2.yaml", lambda d: d["radio"].update(gpio_interrupt_pin=d["radio"]["gpio_power_pin"]))
        self.assertReported("spacecraft 'sat-2': radio and radio both use gpio (12,)")

    def test_sharing_hardware_across_different_spacecraft_is_allowed(self):
        # sat-1 and flatsat deliberately use the same I2C address; wiring is per vehicle.
        self.assertEqual(self.problems(), [])

    def test_components_must_exist_and_be_listed_once_with_matching_blocks(self):
        self.edit("drm/spacecraft/sat-2.yaml", lambda d: d["components"].append({"name": "warpdrive"}))
        self.assertReported("spacecraft 'sat-2': component 'warpdrive' is not under comp/")
        self.edit("drm/spacecraft/sat-2.yaml", lambda d: d["components"].append({"name": "eps"}))
        self.assertReported("spacecraft 'sat-2': component 'eps' is listed more than once")
        self.edit("drm/spacecraft/sat-2.yaml", lambda d: d.update(demo={"handle": 9}))
        self.assertReported("spacecraft 'sat-2': has a 'demo' settings block but does not list the component")

    def test_a_scenario_with_a_missing_initial_condition_is_reported(self):
        self.edit("drm/scenarios/drm-nominal.yaml", lambda d: d.update(initial_conditions="no-such-bin"))
        self.assertReported("scenario 'nominal': Initial condition bin 'no-such-bin'")

    def test_bad_scenario_values_are_reported(self):
        self.edit("drm/scenarios/drm-nominal.yaml", lambda d: d.update(
            run_duration_s=-5, simulith_speed="warp", verify_stacks=[{"stack": "nope.ycs", "at_s": 0}, {"stack": "x"}],
            log_assertions=[{"container": "toaster", "pattern": "x"}]))
        found = self.problems()
        for pattern in ("run_duration_s must be a positive number", 'simulith_speed must be a positive number or "max"',
                        "stack 'nope.ycs' does not exist", "verify_stacks entries need a numeric at_s",
                        "log_assertions container 'toaster'"):
            self.assertTrue(any(pattern in line for line in found), f"{pattern!r} not in {found}")

    def test_documented_speed_forms_are_accepted(self):
        self.edit("drm/scenarios/drm-nominal.yaml", lambda d: d.update(simulith_speed="max", post_verification_speed=0))
        self.assertEqual(self.problems(), [])
        self.edit("drm/scenarios/drm-nominal.yaml", lambda d: d.update(simulith_speed=0))
        self.assertReported("simulith_speed must be a positive number")

    def test_registry_problems_are_reported(self):
        self.edit("drm/drm.yaml", lambda d: d["scenarios"].append(dict(d["scenarios"][0])))
        self.edit("drm/drm.yaml", lambda d: d["spacecraft"].append({"name": "ghost", "config_file": "drm/spacecraft/ghost.yaml"}))
        self.assertReported(f"scenarios entry '{yaml.safe_load((self.cfg / 'drm/drm.yaml').read_text())['scenarios'][0]['name']}' is registered more than once")
        self.assertReported("spacecraft entry 'ghost' config_file 'drm/spacecraft/ghost.yaml' does not exist")

    def test_an_initial_condition_missing_orbit_or_with_a_stray_vehicle_key_is_reported(self):
        self.edit("drm/initial_conditions/eclipse-entry.yaml", lambda d: d.pop("orbit"))
        self.edit("lunar-relay/initial_conditions/lunar-formation.yaml", lambda d: d["vehicles"]["relay"].update(orbt={}))
        self.assertReported("initial condition 'eclipse-entry': missing 'orbit'")
        self.assertReported("initial condition 'lunar-formation' vehicles.relay: unknown key(s) orbt")

    def test_campaign_references_are_checked(self):
        name = next((self.cfg / "drm/campaigns").glob("*.yaml")).name
        self.edit(f"drm/campaigns/{name}", lambda d: d.update(scenario="ghost", base_initial_conditions="nope", trails=3))
        stem = name[:-5]
        self.assertReported(f"campaign '{stem}': scenario 'ghost' is not registered")
        self.assertReported(f"campaign '{stem}': base_initial_conditions 'nope' not found")
        self.assertReported(f"campaign '{stem}': unknown key(s) trails")

    def test_constellation_problems_are_reported(self):
        path = "lunar-relay/constellations/crosslink-demo.yaml"
        self.edit(path, lambda d: d["vehicles"][0].update(spacecraft="sat-9"))
        self.assertReported("Constellation 'crosslink-demo': constellation vehicle 'leaf-1' spacecraft 'sat-9'")
        self.assertTrue(any(line.startswith("lunar-relay: ") for line in self.problems()))

    def test_constellation_initial_conditions_must_cover_every_vehicle_with_a_distinct_orbit(self):
        ic = "lunar-relay/initial_conditions/lunar-formation.yaml"
        self.edit(ic, lambda d: d["vehicles"].pop("leaf-2"))
        self.assertReported("has no vehicles entry for 'leaf-2'")
        self.edit(ic, lambda d: d["vehicles"].update({"leaf-2": {"orbit": {"true_anomaly_deg": 0.0}}}))
        self.assertReported("vehicles 'leaf-1' and 'leaf-2' start in the same orbit")
        self.edit(ic, lambda d: d["vehicles"].update(ghost={"orbit": {}}))
        self.assertReported("has a vehicles entry 'ghost' that is not a vehicle")

    def test_a_mission_missing_the_shared_ground_definitions_is_reported(self):
        for name in ("ccsds.xtce", "shire_server.xtce"):
            (self.cfg / "lunar-relay/gsw" / name).unlink()
        self.assertReported("lunar-relay: ground software: lunar-relay/gsw/ccsds.xtce is missing")
        self.assertReported("lunar-relay: ground software: lunar-relay/gsw/shire_server.xtce is missing")
        self.assertFalse(any("drm: ground software" in line for line in self.problems()))

    def test_the_required_ground_definitions_are_read_from_the_yamcs_configuration(self):
        from shire_config_check import required_mission_xtce
        self.assertEqual(required_mission_xtce(ROOT), ["ccsds.xtce", "shire_ground.xtce", "shire_server.xtce",
                                                       "shire_visual.xtce", "sim_42_truth.xtce"])
        self.assertEqual(required_mission_xtce(Path("/nonexistent")), [])

    def test_orbit_centers_must_be_earth_or_the_moon(self):
        self.edit("lunar-relay/initial_conditions/lunar-baseline.yaml", lambda d: d["orbit"].update(center="MARS"))
        self.assertReported("initial condition 'lunar-baseline': orbit.center 'MARS' must be one of EARTH, LUNA")
        self.edit("lunar-relay/initial_conditions/lunar-formation.yaml",
                  lambda d: d["vehicles"]["relay"]["orbit"].update(center="JUPITER"))
        self.assertReported("'lunar-formation' vehicles.relay: orbit.center 'JUPITER'")

    def test_ground_station_problems_are_reported(self):
        def mutate(d):
            d["ground_stations"][0]["world"] = "MARS"
            d["ground_stations"][1]["lat_deg"] = 123
            d["ground_stations"].append(dict(d["ground_stations"][2]))
        self.edit("lunar-relay/ground_stations.yaml", mutate)
        self.assertReported("ground station 'Goldstone, CA': world 'MARS' must be one of EARTH, LUNA")
        self.assertReported("ground station 'Madrid, Spain': lat_deg must be a number within +-90")
        self.assertReported("ground station 'Canberra, Australia' is listed more than once")

    def test_a_mission_without_a_ground_station_catalog_is_reported(self):
        (self.cfg / "lunar-relay/ground_stations.yaml").unlink()
        self.assertReported("lunar-relay: ground stations: lunar-relay/ground_stations.yaml is missing")

    def test_a_relay_that_extends_a_missing_spacecraft_is_reported(self):
        self.edit("lunar-relay/spacecraft/relay.yaml", lambda d: d.update(extends="ghost"))
        self.assertReported("spacecraft 'relay': Spacecraft 'ghost' not found in mission 'lunar-relay' config.")

    def test_dtn_blocks_are_validated_and_inherited_problems_reach_the_child(self):
        self.edit("lunar-relay/spacecraft/leaf.yaml", lambda d: d["dtn"].update(node_id=0))
        self.assertReported("spacecraft 'leaf': dtn node_id must be an integer")
        # relay sets its own node_id, so a bad leaf value does not reach it.
        self.assertFalse(any("spacecraft 'relay': dtn" in line for line in self.problems()))
        # Merging is key by key, so an unknown key in the base is inherited by the child.
        self.edit("lunar-relay/spacecraft/leaf.yaml", lambda d: d.update(dtn={"node_id": 5, "nodeid": 6}))
        self.assertReported("spacecraft 'leaf': dtn has unknown key(s) 'nodeid'")
        self.assertReported("spacecraft 'relay': dtn has unknown key(s) 'nodeid'")

    def test_a_constellation_vehicle_must_fly_a_spacecraft_that_runs_bundle_protocol(self):
        self.edit("lunar-relay/spacecraft/relay.yaml", lambda d: d.update(dtn=None))
        self.assertReported("vehicle 'relay' flies spacecraft 'relay', which has no dtn: block")
        self.assertFalse(any("vehicle 'leaf-1'" in line for line in self.problems()))

    def test_every_problem_is_collected_in_one_pass(self):
        self.edit("drm/spacecraft/sat-2.yaml", lambda d: d.update(spacecraft_id=1))
        self.edit("drm/scenarios/drm-nominal.yaml", lambda d: d.update(initial_conditions="no-such-bin"))
        self.assertGreaterEqual(len(self.problems()), 2)

    def test_a_missing_global_registry_is_reported(self):
        (self.cfg / "shire-config.yaml").write_text("build: {missions: []}\n")
        self.assertEqual(self.problems(), ["global config registers no missions"])


class WiringResourceTests(unittest.TestCase):
    def test_each_bus_kind_is_recognized(self):
        self.assertEqual(wiring_resources({"handle": 2, "string": "/dev/usart_2"}), [("uart", 2)])
        self.assertEqual(wiring_resources({"i2c_bus_id": 0, "i2c_device_addr": 30}), [("i2c", 0, 30)])
        self.assertEqual(wiring_resources({"spi_bus": 1, "spi_cs": 0, "gpio_power_pin": 10, "gpio_interrupt_pin": 11}),
                         [("spi", 1, 0), ("gpio", 11), ("gpio", 10)])
        self.assertEqual(wiring_resources({"baudrate_hz": 115200}), [])


if __name__ == "__main__":
    unittest.main()

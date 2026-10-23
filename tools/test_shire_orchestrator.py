"""Verify configuration discovery is independent of the caller's directory."""
from pathlib import Path
import shutil
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml


ROOT = Path(__file__).resolve().parent.parent


def prepare_repository(temporary, scenario=None):
    root = Path(temporary) / "repository"
    shutil.copytree(ROOT / "cfg", root / "cfg")
    (root / "tools").mkdir()
    shutil.copytree(ROOT / "comp/eps/support", root / "comp/eps/support")
    shutil.copytree(ROOT / "comp/eps/gsw", root / "comp/eps/gsw")
    for name in ("shire-orchestrator.py", "shire_provenance.py", "shire_eps_config.py", "shire_radio_config.py", "shire_eps_stacks.py",
                 "shire_board_config.py"):
        shutil.copy2(ROOT / "tools" / name, root / "tools" / name)
    if scenario:
        (root / "build").mkdir()
        (root / "build/active.yaml").write_text(yaml.safe_dump({
            "mission": "drm", "spacecraft": "sat-1", "scenario": scenario,
            "cli": "demo", "fsw_dir": "cfs", "gsw_dir": "yamcs", "log_mode": "none",
        }))
    return root


class ConfigurationPathsTests(unittest.TestCase):
    def test_cli_switch_after_cleaned_mission_output_selects_only_demo_client(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = prepare_repository(temporary, 'nominal')
            for name in ('adcs', 'demo', 'radio'):
                shutil.copytree(ROOT / f'comp/{name}/support', root / f'comp/{name}/support')
            active_path = root / 'build/active.yaml'
            selection = yaml.safe_load(active_path.read_text())
            for component, scenario in (('eps', 'nominal'), ('demo', 'nominal'), ('demo', 'debug')):
                selection['cli'] = component
                selection['scenario'] = scenario
                active_path.write_text(yaml.safe_dump(selection))
                mission_output = root / 'build/drm'
                if mission_output.exists():
                    shutil.rmtree(mission_output)
                result = subprocess.run([sys.executable, str(root / 'tools/shire-orchestrator.py'), '--cli-debug'],
                                        cwd=root, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                config = yaml.safe_load((root / 'build/build.yaml').read_text())
                self.assertEqual(config['global']['build']['cli'], [{'name': component}])
                compose = yaml.safe_load((mission_output / 'cli-compose.yaml').read_text())
                clients = {name: service for name, service in compose['services'].items() if name.endswith('-cli')}
                self.assertEqual(set(clients), {f'shire-comp-{component}-cli'})
                self.assertEqual(clients[f'shire-comp-{component}-cli']['image'], f'shire-comp-{component}-cli:0.0.0')
                snapshot = yaml.safe_load((mission_output / f'scenario/{scenario}.snapshot.yaml').read_text())
                resolved = snapshot['resolved_components']
                for name in ('adcs', 'demo', 'eps', 'radio'):
                    self.assertEqual(resolved[name]['debug'], name == component or scenario == 'debug')
                for name in ('eps', 'demo'):
                    header = (root / f'comp/{name}/shared/device_cfg.h').read_text()
                    enabled = re.search(rf'^#define {name.upper()}_CFG_DEBUG\b', header, re.MULTILINE) is not None
                    self.assertEqual(enabled, name == component or scenario == 'debug')
                with patch.object(sys, 'path', [str(ROOT / 'tools'), *sys.path]):
                    from shire_runner_lib import resolve_component_configs
                self.assertEqual(resolve_component_configs(config['global'], config['mission_cfg'],
                    config['spacecraft_cfg'], config['scenario_cfg'], snapshot['resolved_ic'],
                    cli_debug=True, cli_component=component), resolved)
                # Changing clients must retain the mission's power topology.
                self.assertIn({'name': 'eps'}, config['spacecraft_cfg']['components'])

    def test_radio_link_scenario_renders_stations_and_link_settings_into_the_header(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = prepare_repository(temporary, 'radio-link-testing')
            for name in ('adcs', 'demo', 'radio'):
                shutil.copytree(ROOT / f'comp/{name}/support', root / f'comp/{name}/support')
            scenario = root / 'cfg/drm/scenarios/radio-link-testing.yaml'
            settings = yaml.safe_load(scenario.read_text())
            settings['radio'].update(link_mode='auto', uplink_delay_ms=1500, downlink_drop_ppm=20000,
                                     ground_stations=['VOYG Denver, CO'])
            scenario.write_text(yaml.safe_dump(settings))
            result = subprocess.run([sys.executable, str(root / 'tools/shire-orchestrator.py')],
                                    cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            header = (root / 'comp/radio/shared/device_cfg.h').read_text()
            self.assertIn('RADIO_LINK_MODE_AUTO', header)
            self.assertRegex(header, r'RADIO_CFG_LINK_UPLINK_DELAY_MS\s+1500\b')
            self.assertRegex(header, r'RADIO_CFG_LINK_DOWNLINK_DROP_PPM\s+20000\b')
            self.assertRegex(header, r'RADIO_CFG_NUM_GROUND_STATIONS\s+1\b')
            self.assertIn('{ 39.751000000, -104.995000000, 5.000, 1 }', header)
            self.assertNotIn('San Diego', header)
            snapshot = yaml.safe_load((root / 'build/drm/scenario/radio-link-testing.snapshot.yaml').read_text())
            self.assertEqual([s['name'] for s in snapshot['resolved_components']['radio']['ground_stations']],
                             ['VOYG Denver, CO'])

    def test_unknown_radio_ground_station_fails_configuration(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = prepare_repository(temporary, 'radio-link-testing')
            for name in ('adcs', 'demo', 'radio'):
                shutil.copytree(ROOT / f'comp/{name}/support', root / f'comp/{name}/support')
            scenario = root / 'cfg/drm/scenarios/radio-link-testing.yaml'
            settings = yaml.safe_load(scenario.read_text())
            settings['radio']['ground_stations'] = ['Atlantis']
            scenario.write_text(yaml.safe_dump(settings))
            result = subprocess.run([sys.executable, str(root / 'tools/shire-orchestrator.py')],
                                    cwd=root, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Atlantis', result.stdout + result.stderr)

    def test_initial_configuration_keeps_a_previously_selected_board(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = prepare_repository(temporary)
            active = root / 'build/active.yaml'
            active.parent.mkdir()
            active.write_text('board: rpi5\n')
            result = subprocess.run([sys.executable, str(root / 'tools/shire-orchestrator.py')],
                                    cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            selection = yaml.safe_load(active.read_text())
            self.assertEqual(selection['board'], 'rpi5')
            self.assertEqual(selection['mission'], 'drm')
            self.assertEqual(selection['spacecraft'], 'sat-1')

    def test_configuration_generation_from_root_tools_and_outside(self):
        for location in ("root", "tools", "outside"):
            with self.subTest(location=location), tempfile.TemporaryDirectory() as temporary:
                root = prepare_repository(temporary)
                cwd = {"root": root, "tools": root / "tools",
                       "outside": Path(temporary)}[location]
                result = subprocess.run(
                    [sys.executable, str(root / "tools/shire-orchestrator.py")],
                    cwd=cwd, capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(yaml.safe_load((root / 'build/active.yaml').read_text())['board'], 'qemu-zynq')
                config = yaml.safe_load((root / "build/build.yaml").read_text())
                self.assertEqual(config["global"], yaml.safe_load(
                    (root / "cfg/shire-config.yaml").read_text()))
                mission = config["mission"]
                self.assertTrue((root / "build" / mission / "shire-compose.yaml").is_file())
                self.assertTrue((root / "build" / mission / "42_config/Inp_Sim.txt").is_file())
                definitions = root / "build" / mission / config["spacecraft"] / "shire_defs"
                self.assertTrue((definitions / "coverage-tests/CMakeLists.txt").is_file())


class EventLimitTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("gcc"), "C preprocessor required")
    def test_runtime_retention_preserves_unit_test_squelch_limits(self):
        names = ("CFE_PLATFORM_EVS_MAX_APP_EVENT_BURST", "CFE_PLATFORM_EVS_APP_EVENTS_PER_SEC")
        # Include an ordinary scenario to verify that retention stays opt-in.
        for scenario in ("nominal", "eps-functional", "eps-backdoor-testing"):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as temporary:
                root = prepare_repository(temporary, scenario)
                result = subprocess.run([sys.executable, str(root / "tools/shire-orchestrator.py")],
                                        cwd=root, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                config = yaml.safe_load((root / "build/build.yaml").read_text())
                definitions = root / "build" / config["mission"] / config["spacecraft"] / "shire_defs"
                for cpu in (1, 2):
                    header = f"cpu{cpu}_platform_cfg.h"
                    baseline = (ROOT / "cfg/shire_defs" / header).read_text()
                    baseline_limits = {name: int(re.search(rf"#define {name}\s+(\d+)", baseline)[1])
                                       for name in names}
                    for unit_test in (False, True):
                        command = ["gcc", "-E", "-dM", "-x", "c", "-I", str(definitions),
                                   "-include", header]
                        if unit_test:
                            command.append("-D_UNIT_TEST_")
                        command.append("/dev/null")
                        cpp = subprocess.run(command, capture_output=True, text=True)
                        self.assertEqual(cpp.returncode, 0, cpp.stderr)
                        actual = {name: int(re.search(rf"#define {name}\s+(\d+)", cpp.stdout)[1])
                                  for name in names}
                        expected = baseline_limits if unit_test or scenario == "nominal" else dict.fromkeys(names, 1000000)
                        self.assertEqual(actual, expected, (scenario, cpu, unit_test))
                if scenario != "nominal":
                    snapshot = yaml.safe_load((root / "build/drm/scenario" / f"{scenario}.snapshot.yaml").read_text())
                    self.assertEqual(snapshot["resolved_components"]["__fsw"],
                                     {"event_burst_max": 1000000, "event_refill_per_sec": 1000000})


if __name__ == "__main__":
    unittest.main()

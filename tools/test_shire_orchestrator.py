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
    for name in ("shire-orchestrator.py", "shire_provenance.py", "shire_eps_config.py", "shire_radio_config.py", "shire_runner_lib.py", "shire_config.py", "shire_constellation_config.py", "shire_dtn_config.py", "shire_eps_stacks.py",
                 "shire_board_config.py", "shire_limits_config.py"):
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
                    cli_debug=True, cli_component=component,
                    ground_stations=yaml.safe_load((root / 'cfg/drm/ground_stations.yaml').read_text())['ground_stations']),
                    resolved)
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
                                     ground_stations=['VOYG Denver, CO'], uplink_encryption=False)
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
            # The flight app (compiled header) and the ground CryptoLib (compose environment) must agree.
            self.assertRegex(header, r'RADIO_CFG_UPLINK_ENCRYPTION\s+0\b')
            self.assertRegex(header, r'RADIO_CFG_DOWNLINK_ENCRYPTION\s+1\b')
            compose = yaml.safe_load((root / 'build/drm/shire-compose.yaml').read_text())
            self.assertEqual(sorted(compose['services']['shire-cryptolib']['environment']),
                             ['SHIRE_DOWNLINK_ENCRYPTION=1', 'SHIRE_UPLINK_ENCRYPTION=0'])
            snapshot = yaml.safe_load((root / 'build/drm/scenario/radio-link-testing.snapshot.yaml').read_text())
            self.assertEqual([s['name'] for s in snapshot['resolved_components']['radio']['ground_stations']],
                             ['VOYG Denver, CO'])

    def test_compose_only_still_passes_link_security_to_the_ground_cryptolib(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = prepare_repository(temporary, 'radio-link-testing')
            for name in ('adcs', 'demo', 'radio'):
                shutil.copytree(ROOT / f'comp/{name}/support', root / f'comp/{name}/support')
            scenario = root / 'cfg/drm/scenarios/radio-link-testing.yaml'
            settings = yaml.safe_load(scenario.read_text())
            settings['radio'].update(downlink_encryption=False)
            scenario.write_text(yaml.safe_dump(settings))
            result = subprocess.run([sys.executable, str(root / 'tools/shire-orchestrator.py'), '--compose-only'],
                                    cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            compose = yaml.safe_load((root / 'build/drm/shire-compose.yaml').read_text())
            self.assertEqual(sorted(compose['services']['shire-cryptolib']['environment']),
                             ['SHIRE_DOWNLINK_ENCRYPTION=0', 'SHIRE_UPLINK_ENCRYPTION=1'])
            # Compose-only must not render the device header an already-built image was baked from.
            self.assertFalse((root / 'comp/radio/shared/device_cfg.h').exists())

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
            # The mission lists its scenarios alphabetically, so the default is named and not the first entry.
            self.assertEqual(selection['scenario'], 'nominal')

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


class TelemetryLimitsTests(unittest.TestCase):
    def build(self, scenario_limits=None, *extra):
        """Runs the orchestrator on a copy of the repository, returning (root, completed process)."""
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = prepare_repository(self.temporary.name, 'nominal')
        for name in ('adcs', 'demo', 'radio'):
            shutil.copytree(ROOT / f'comp/{name}/support', root / f'comp/{name}/support')
        if scenario_limits is not None:
            scenario = root / 'cfg/drm/scenarios/drm-nominal.yaml'
            settings = yaml.safe_load(scenario.read_text()) or {}
            settings['limits'] = scenario_limits
            scenario.write_text(yaml.safe_dump(settings))
        result = subprocess.run([sys.executable, str(root / 'tools/shire-orchestrator.py'), *extra],
                                cwd=root, capture_output=True, text=True)
        return root, result

    def ranges(self, root):
        text = (root / 'build/drm/scenario/mdb/components/eps/eps.xtce').read_text()
        types = re.search(r'<xtce:FloatParameterType name="BATTERY_VOLTAGE_Type_BATTERY_VOLTAGE_Limits".*?</xtce:FloatParameterType>',
                          text, re.DOTALL).group(0)
        return dict(re.findall(r'<xtce:(WarningRange|CriticalRange) ([^/]*)/>', types))

    def test_mission_limits_become_alarm_ranges_in_the_resolved_ground_definition(self):
        root, result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.ranges(root), {'WarningRange': 'minInclusive="20.0" ', 'CriticalRange': 'minInclusive="18.0" '})
        # Only definitions that carry a limited parameter are overlaid.
        mdb = root / 'build/drm/scenario/mdb'
        self.assertEqual(sorted(p.relative_to(mdb).as_posix() for p in mdb.rglob('*.xtce')), ['components/eps/eps.xtce'])
        # The resolved EPS wiring is preserved, since limits are applied to the generated definition.
        self.assertIn('Switch 0', (mdb / 'components/eps/eps.xtce').read_text())

    def test_limits_are_part_of_the_snapshot_and_match_the_build_key_resolution(self):
        root, result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        config = yaml.safe_load((root / 'build/build.yaml').read_text())
        snapshot = yaml.safe_load((root / 'build/drm/scenario/nominal.snapshot.yaml').read_text())
        resolved = snapshot['resolved_components']
        self.assertEqual(resolved['__limits']['/EPS/BATTERY_VOLTAGE'], {'yellow': {'min': 20.0}, 'red': {'min': 18.0}})
        with patch.object(sys, 'path', [str(ROOT / 'tools'), *sys.path]):
            from shire_runner_lib import resolve_component_configs
        self.assertEqual(resolve_component_configs(config['global'], config['mission_cfg'], config['spacecraft_cfg'],
                                                   config['scenario_cfg'], snapshot['resolved_ic'],
                                                   ground_stations=yaml.safe_load(
                                                       (root / 'cfg/drm/ground_stations.yaml').read_text())['ground_stations']),
                         resolved)

    def render(self, root, mission, spacecraft, scenario):
        for name in ('adcs', 'demo', 'radio'):
            if not (root / f'comp/{name}/support').exists():
                shutil.copytree(ROOT / f'comp/{name}/support', root / f'comp/{name}/support')
        (root / 'build').mkdir(exist_ok=True)
        (root / 'build/active.yaml').write_text(yaml.safe_dump({
            'mission': mission, 'spacecraft': spacecraft, 'scenario': scenario,
            'cli': 'demo', 'fsw_dir': 'cfs', 'gsw_dir': 'yamcs', 'log_mode': 'none'}))
        result = subprocess.run([sys.executable, str(root / 'tools/shire-orchestrator.py')],
                                cwd=root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_the_lunar_relay_mission_renders_a_lunar_orbit_its_own_stations_and_the_light_time_delay(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = prepare_repository(temporary)
            self.render(root, 'lunar-relay', 'relay', 'smoke')
            config = yaml.safe_load((root / 'build/build.yaml').read_text())
            self.assertEqual((config['mission'], config['spacecraft'], config['scenario']),
                             ('lunar-relay', 'relay', 'smoke'))
            self.assertEqual(config['spacecraft_cfg']['spacecraft_name'], 'relay')
            self.assertNotIn('extends', config['spacecraft_cfg'])
            self.assertIn('visualization', config['spacecraft_cfg'])  # inherited from leaf

            output = root / 'build/lunar-relay'
            orbit = (output / '42_config/Orb_SHIRE.txt').read_text()
            self.assertRegex(orbit, r'(?m)^LUNA\s+!  Orbit Center$')
            self.assertRegex(orbit, r'(?m)^100\.0\s+100\.0\s+!  Periapsis & Apoapsis Altitude')
            self.assertRegex(orbit, r'(?m)^90\.0\s+!  Inclination')
            sim = (output / '42_config/Inp_Sim.txt').read_text()
            self.assertIn('4                               ! Number of Ground Stations', sim)
            self.assertRegex(sim, r'TRUE\s+LUNA\s+0\.0\s+-89\.9\s+"Shackleton rim"')
            self.assertRegex(sim, r'TRUE\s+EARTH\s+-116\.89\s+35\.4267\s+"Goldstone, CA"')
            self.assertNotIn('VOYG', sim)
            self.assertRegex(sim, r'(?m)^11\s+1\s+2026\s+!  Date')

            snapshot = yaml.safe_load((output / 'scenario/smoke.snapshot.yaml').read_text())
            self.assertEqual(snapshot['resolved_ic']['orbit']['center'], 'LUNA')
            resolved = snapshot['resolved_components']
            self.assertEqual(set(resolved) - {'__limits'}, {'adcs', 'eps', 'radio'})
            self.assertEqual(resolved['eps']['i2c_device_addr'], 0x1F)
            self.assertEqual((resolved['radio']['uplink_delay_ms'], resolved['radio']['downlink_delay_ms']), (1282, 1282))
            self.assertEqual(resolved['__limits']['/EPS/BATTERY_VOLTAGE'], {'yellow': {'min': 21.0}, 'red': {'min': 19.0}})
            self.assertEqual({s['name'] for s in resolved['radio']['ground_stations']},
                             {'Goldstone, CA', 'Madrid, Spain', 'Canberra, Australia'})

            header = (root / 'comp/radio/shared/device_cfg.h').read_text()
            self.assertRegex(header, r'RADIO_CFG_LINK_UPLINK_DELAY_MS\s+1282\b')
            self.assertRegex(header, r'RADIO_CFG_LINK_DOWNLINK_DELAY_MS\s+1282\b')
            compose = yaml.safe_load((output / 'shire-compose.yaml').read_text())
            self.assertIn('shire-fsw-relay', {s.get('container_name') for s in compose['services'].values()})

    def test_bundle_protocol_runs_only_on_spacecraft_with_a_dtn_block(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = prepare_repository(temporary)
            self.render(root, 'lunar-relay', 'relay', 'smoke')
            defs = root / 'build/lunar-relay/relay/shire_defs'
            self.assertIn('BPLIB_LOCAL_EID_NODE_NUM=100', (defs / 'dtn.cmake').read_text())
            startup = (defs / 'cpu1_cfe_es_startup.scr').read_text()
            self.assertRegex(startup, r'(?m)^CFE_LIB, bplib,')
            self.assertRegex(startup, r'(?m)^CFE_APP, bpnode,')
            self.assertNotRegex(startup, r'(?m)^CFE_APP, demo,')  # the lunar spacecraft do not list demo
            self.assertIn('include("${MISSION_DEFS}/dtn.cmake" OPTIONAL)', (defs / 'arch_build_custom.cmake').read_text())

            self.render(root, 'lunar-relay', 'leaf', 'smoke')
            leaf = root / 'build/lunar-relay/leaf/shire_defs'
            self.assertIn('BPLIB_LOCAL_EID_NODE_NUM=101', (leaf / 'dtn.cmake').read_text())

            self.render(root, 'drm', 'sat-1', 'nominal')
            drm = root / 'build/drm/sat-1/shire_defs'
            self.assertFalse((drm / 'dtn.cmake').exists())
            startup = (drm / 'cpu1_cfe_es_startup.scr').read_text()
            self.assertNotIn('bpnode', startup)
            self.assertNotIn('bplib', startup)
            self.assertRegex(startup, r'(?m)^CFE_APP, demo,')

    def render_vehicle(self, root, vehicle, scenario='smoke'):
        for name in ('adcs', 'demo', 'radio'):
            if not (root / f'comp/{name}/support').exists():
                shutil.copytree(ROOT / f'comp/{name}/support', root / f'comp/{name}/support')
        (root / 'build').mkdir(exist_ok=True)
        (root / 'build/active.yaml').write_text(yaml.safe_dump({
            'mission': 'lunar-relay', 'constellation': 'crosslink-demo', 'vehicle': vehicle, 'scenario': scenario,
            'cli': 'demo', 'fsw_dir': 'cfs', 'gsw_dir': 'yamcs', 'log_mode': 'none'}))
        result = subprocess.run([sys.executable, str(root / 'tools/shire-orchestrator.py')],
                                cwd=root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return yaml.safe_load((root / 'build/build.yaml').read_text())

    def test_each_constellation_vehicle_builds_with_its_own_identity_node_number_orbit_and_image_tag(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = prepare_repository(temporary)
            seen = {}
            for vehicle, node, spacecraft in (('leaf-1', 101, 'leaf'), ('leaf-2', 102, 'leaf'), ('relay', 100, 'relay')):
                config = self.render_vehicle(root, vehicle)
                self.assertEqual((config['spacecraft'], config['image_tag'], config['vehicle']['id']),
                                 (spacecraft, vehicle, vehicle))
                self.assertEqual(config['constellation'], 'crosslink-demo')
                defs = root / f'build/lunar-relay/{spacecraft}/shire_defs'
                self.assertIn(f'BPLIB_LOCAL_EID_NODE_NUM={node}', (defs / 'dtn.cmake').read_text())
                orbit = (root / 'build/lunar-relay/42_config/Orb_SHIRE.txt').read_text()
                self.assertRegex(orbit, r'(?m)^LUNA\s+!  Orbit Center$')
                seen[vehicle] = re.search(r'(?m)^(\S+)\s+(\S+)\s+!  Periapsis & Apoapsis', orbit).groups(), \
                    re.search(r'(?m)^(\S+)\s+!  True Anomaly', orbit).group(1)
                compose = yaml.safe_load((root / 'build/lunar-relay/shire-compose.yaml').read_text())
                self.assertIn(f'shire-fsw-lunar-relay:{vehicle}', {s.get('image') for s in compose['services'].values()})
            self.assertEqual(seen['relay'], (('500.0', '8000.0'), '180.0'))
            self.assertEqual(seen['leaf-1'], (('100.0', '100.0'), '0.0'))
            self.assertEqual(seen['leaf-2'], (('100.0', '100.0'), '180.0'))

    def test_a_vehicle_that_is_not_in_the_constellation_stops_the_build_with_the_choices(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = prepare_repository(temporary)
            for name in ('adcs', 'demo', 'radio'):
                shutil.copytree(ROOT / f'comp/{name}/support', root / f'comp/{name}/support')
            (root / 'build').mkdir(exist_ok=True)
            (root / 'build/active.yaml').write_text(yaml.safe_dump({
                'mission': 'lunar-relay', 'constellation': 'crosslink-demo', 'vehicle': 'ghost', 'scenario': 'smoke',
                'cli': 'demo', 'fsw_dir': 'cfs', 'gsw_dir': 'yamcs', 'log_mode': 'none'}))
            result = subprocess.run([sys.executable, str(root / 'tools/shire-orchestrator.py')],
                                    cwd=root, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Vehicle 'ghost' not found in constellation 'crosslink-demo'. Available: leaf-1, relay, leaf-2",
                          result.stderr)

    def test_a_dtn_file_left_by_an_earlier_build_is_removed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = prepare_repository(temporary)
            stale = root / 'build/drm/sat-1/shire_defs/dtn.cmake'
            stale.parent.mkdir(parents=True)
            stale.write_text('add_compile_definitions(BPLIB_LOCAL_EID_NODE_NUM=999)\n')
            self.render(root, 'drm', 'sat-1', 'nominal')
            self.assertFalse(stale.exists())

    def test_an_invalid_dtn_block_stops_the_build_with_a_clear_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = prepare_repository(temporary)
            path = root / 'cfg/lunar-relay/spacecraft/leaf.yaml'
            data = yaml.safe_load(path.read_text())
            data['dtn'] = {'node_id': 0}
            path.write_text(yaml.safe_dump(data))
            for name in ('adcs', 'demo', 'radio'):
                shutil.copytree(ROOT / f'comp/{name}/support', root / f'comp/{name}/support')
            (root / 'build').mkdir(exist_ok=True)
            (root / 'build/active.yaml').write_text(yaml.safe_dump({
                'mission': 'lunar-relay', 'spacecraft': 'leaf', 'scenario': 'smoke', 'cli': 'demo',
                'fsw_dir': 'cfs', 'gsw_dir': 'yamcs', 'log_mode': 'none'}))
            result = subprocess.run([sys.executable, str(root / 'tools/shire-orchestrator.py')],
                                    cwd=root, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('dtn node_id must be an integer', result.stderr)

    def test_a_leaf_in_the_same_mission_gets_no_relay_delay_and_its_own_wiring(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = prepare_repository(temporary)
            self.render(root, 'lunar-relay', 'leaf', 'smoke')
            snapshot = yaml.safe_load((root / 'build/lunar-relay/scenario/smoke.snapshot.yaml').read_text())
            radio = snapshot['resolved_components']['radio']
            self.assertEqual((radio['uplink_delay_ms'], radio['spi_cs']), (0, 0))
            self.assertEqual(snapshot['resolved_components']['eps']['i2c_device_addr'], 0x1E)

    def test_the_drm_still_renders_an_earth_orbit_and_its_own_stations(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = prepare_repository(temporary)
            self.render(root, 'drm', 'sat-1', 'nominal')
            orbit = (root / 'build/drm/42_config/Orb_SHIRE.txt').read_text()
            self.assertRegex(orbit, r'(?m)^EARTH\s+!  Orbit Center$')
            self.assertRegex(orbit, r'(?m)^400\.0\s+400\.0\s+!  Periapsis')
            sim = (root / 'build/drm/42_config/Inp_Sim.txt').read_text()
            self.assertIn('VOYG San Diego, CA', sim)
            self.assertNotIn('Shackleton', sim)

    def test_a_scenario_overrides_one_level_and_keeps_the_other(self):
        root, result = self.build({'/EPS/BATTERY_VOLTAGE': {'yellow': {'min': 21.0}}})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.ranges(root), {'WarningRange': 'minInclusive="21.0" ', 'CriticalRange': 'minInclusive="18.0" '})

    def test_a_scenario_can_remove_every_limit(self):
        root, result = self.build({'/EPS/BATTERY_VOLTAGE': None, '/EPS/BATTERY_TEMPERATURE': None})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((root / 'build/drm/scenario/mdb').exists())
        snapshot = yaml.safe_load((root / 'build/drm/scenario/nominal.snapshot.yaml').read_text())
        self.assertNotIn('__limits', snapshot['resolved_components'])

    def test_a_limit_on_an_unknown_parameter_fails_configuration(self):
        root, result = self.build({'/EPS/NO_SUCH_PARAMETER': {'red': {'min': 1}}})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('/EPS/NO_SUCH_PARAMETER', result.stdout + result.stderr)

    def test_an_inconsistent_limit_fails_configuration(self):
        root, result = self.build({'/EPS/BATTERY_VOLTAGE': {'red': {'min': 25.0}}})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('red min 25 must not be above yellow min 20', result.stdout + result.stderr)

    def test_compose_only_resolves_limits_but_leaves_the_built_definitions_alone(self):
        root, result = self.build(None, '--compose-only')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((root / 'build/drm/scenario/mdb').exists())


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

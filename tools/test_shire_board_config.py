"""Board profile and mixed-architecture deployment regression checks."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

import yaml

spec = importlib.util.spec_from_file_location('board_config', Path(__file__).with_name('shire_board_config.py'))
config = importlib.util.module_from_spec(spec)
spec.loader.exec_module(config)


def elf(bits, machine, flags=0):
    data = bytearray(64)
    data[:6] = b'\x7fELF' + bytes([1 if bits == 32 else 2, 1])
    struct.pack_into('<H', data, 18, machine)
    if bits == 32:
        struct.pack_into('<I', data, 36, flags)
    return data


class ProfileTests(unittest.TestCase):
    def test_board_selection_precedence_and_saved_yaml_are_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'active.yaml'
            self.assertEqual(config.select_profile(active_path=path), 'qemu-zynq')
            for active, expected in (({'mission': 'drm'}, 'qemu-zynq'),
                                     ({'board': 'zybo7020'}, 'zybo7020'),
                                     ({'board': 'qemu-zynq'}, 'qemu-zynq'),
                                     ({'board': 'cfg/boards/my-board.yaml'}, 'cfg/boards/my-board.yaml')):
                path.write_text(yaml.safe_dump(active))
                saved = path.read_bytes()
                self.assertEqual(config.select_profile(active_path=path), expected)
                self.assertEqual(config.select_profile('rpi5', active_path=path), 'rpi5')
                self.assertEqual(path.read_bytes(), saved)

    def test_invalid_saved_board_does_not_silently_fall_back(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'active.yaml'
            for active in ('[]', '', 'board: [', 'board: null', 'board: false',
                           'board: 42', 'board: []', 'board: ""'):
                path.write_text(active)
                with self.subTest(active=active), self.assertRaises(RuntimeError):
                    config.select_profile(active_path=path)
                self.assertEqual(config.select_profile('qemu-zynq', active_path=path), 'qemu-zynq')

    def test_qemu_schema_is_explicit_and_does_not_change_legacy_hardware_digest(self):
        profile = config.load_profile('qemu-zynq')
        self.assertEqual(profile['backend'], 'qemu')
        self.assertEqual(profile['platform'], 'zynq-zc702')
        self.assertNotIn('ssh', profile)
        hardware = config.load_profile('zybo7020')
        original = yaml.safe_load(Path(hardware['_path']).read_text())
        self.assertEqual(config.profile_digest(hardware),
                         hashlib.sha256(json.dumps(original, sort_keys=True).encode()).hexdigest())
        self.assertNotIn('backend', hardware)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'profile.yaml'
            for key, value in (('backend', 'invalid'), ('platform', 'rpi5'), ('ssh', {}),
                               ('work_dir', ''), ('schema_version', True)):
                changed = {k: v for k, v in profile.items() if not k.startswith('_')}
                changed[key] = value
                path.write_text(yaml.safe_dump(changed))
                with self.subTest(key=key), self.assertRaises(RuntimeError):
                    config.load_profile(str(path))

    def test_cfs_selectors_keep_native_and_unit_defaults_and_arm64_sync_hooks(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            script = directory / 'check.cmake'
            for system, unit, expected in (('', False, 'amd64-shire'),
                    ('armv7l-shire', False, 'armv7l-shire'),
                    ('aarch64-shire', False, 'aarch64-shire'),
                    ('aarch64-shire', True, 'amd64-linux')):
                script.write_text(f'set(ENABLE_UNIT_TESTS {"TRUE" if unit else "FALSE"})\n'
                    f'include("{config.ROOT}/cfg/shire_defs/targets.cmake")\n'
                    f'if(NOT cpu1_SYSTEM STREQUAL "{expected}")\n'
                    'message(FATAL_ERROR "Wrong cFS system")\nendif()\n')
                subprocess.run(['cmake', '-P', str(script)],
                    env={**os.environ, 'SHIRE_FSW_SYSTEM': system}, check=True, capture_output=True)
            sdk = directory / 'sdk/share/buildroot'
            sdk.mkdir(parents=True)
            (sdk / 'toolchainfile.cmake').write_text('set(CMAKE_SYSTEM_PROCESSOR aarch64)\n')
            script.write_text(f'include("{config.ROOT}/cfg/shire_defs/toolchain-aarch64-shire.cmake")\n'
                'if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR NOT CFE_SYSTEM_PSPNAME STREQUAL "shire" '
                'OR NOT OSAL_SYSTEM_OSTYPE STREQUAL "shire" OR NOT DEFINED SCH_CUSTOM_PLATFORM_SOURCE '
                'OR NOT DEFINED CFE_SB_OBSERVER_SOURCE)\nmessage(FATAL_ERROR "Missing synchronized runtime")\nendif()\n')
            subprocess.run(['cmake', '-P', str(script)],
                env={**os.environ, 'SHIRE_ARM_SDK': str(directory / 'sdk')}, check=True, capture_output=True)

    def test_shipped_profiles_and_artifact_templates(self):
        zybo, pi = (config.load_profile(name) for name in ('zybo7020', 'rpi5'))
        self.assertEqual(zybo['runtime']['architecture'], 'armv7hf')
        self.assertEqual(pi['runtime']['architecture'], 'aarch64')
        path = config.artifact_path(pi, 'bundle', {'mission': 'mission', 'spacecraft': 'vehicle'})
        self.assertEqual(path, config.ROOT / 'build/board-targets/rpi5/mission/vehicle/bundle')
        self.assertNotEqual(config.profile_digest(zybo), config.profile_digest(pi))
        other = copy.deepcopy(zybo)
        other['network']['tunnel'] += 1
        self.assertNotEqual(config.profile_digest(zybo), config.profile_digest(other))

    def test_invalid_profile_fields_and_port_collisions_are_rejected(self):
        original = config.load_profile('zybo7020')
        original.pop('_path')
        changes = [('ssh', 'password', 'never-store-this'), ('network', 'command', 0),
            ('network', 'tunnel', 50000), ('network', 'telemetry', 1234),
            ('network', 'device_ports', [50001]), ('runtime', 'loader', '/../../loader'),
            ('runtime', 'architecture', 'unknown'), ('runtime', 'executable', '../core'),
            ('ssh', 'target', '-oProxyCommand=bad'), ('ssh', 'connect_timeout', 0),
            ('runtime', 'kernel_machines', []),
            ('runtime', 'serial_console', '/tmp/output'),
            ('runtime', 'serial_console', '/dev/../etc/passwd'),
            ('runtime', 'serial_console', True),
            ('runtime', 'cfs_system', '../another-target')]
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'profile.yaml'
            for section, key, value in changes:
                data = copy.deepcopy(original)
                data[section][key] = value
                path.write_text(yaml.safe_dump(data))
                with self.subTest(key=key), self.assertRaises(RuntimeError):
                    config.load_profile(str(path))
            for version in (True, 1.0, '1', 2):
                data = copy.deepcopy(original)
                data['schema_version'] = version
                path.write_text(yaml.safe_dump(data))
                with self.subTest(version=version), self.assertRaises(RuntimeError):
                    config.load_profile(str(path))

    def test_optional_console_profile_setting_is_validated_without_changing_defaults(self):
        profile = config.load_profile('zybo7020')
        self.assertNotIn('serial_console', profile['runtime'])
        profile.pop('_path')
        profile['runtime']['serial_console'] = '/dev/ttyPS0'
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'profile.yaml'
            path.write_text(yaml.safe_dump(profile))
            self.assertEqual(config.load_profile(str(path))['runtime']['serial_console'], '/dev/ttyPS0')

    def test_cpu_and_float_abi_mismatches_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'core'
            path.write_bytes(elf(32, 40, 0x400))
            config.check_elf(path, 'armv7hf')
            with self.assertRaisesRegex(RuntimeError, 'architecture mismatch'):
                config.check_elf(path, 'aarch64')
            path.write_bytes(elf(32, 40))
            with self.assertRaisesRegex(RuntimeError, 'hard-float'):
                config.check_elf(path, 'armv7hf')
            path.write_bytes(elf(64, 183))
            config.check_elf(path, 'aarch64')
            with self.assertRaisesRegex(RuntimeError, 'architecture mismatch'):
                config.check_elf(path, 'armv7hf')
            path.write_bytes(b'short')
            with self.assertRaisesRegex(RuntimeError, 'Not an ELF'):
                config.check_elf(path, 'aarch64')

    def test_mixed_modules_and_stale_bundle_files_are_rejected(self):
        profile = config.load_profile('rpi5')
        active = {'mission': 'drm', 'spacecraft': 'sat-1'}
        with tempfile.TemporaryDirectory() as temporary:
            bundle = Path(temporary)
            app = bundle / 'app'
            app.mkdir()
            (app / 'core-cpu1').write_bytes(elf(64, 183))
            (app / 'module.so').write_bytes(elf(32, 40, 0x400))
            manifest = {'system': 'aarch64-shire', **active, 'files': {}}

            def save():
                manifest['files'] = {str(p.relative_to(bundle)): hashlib.sha256(p.read_bytes()).hexdigest()
                                     for p in app.iterdir()}
                (bundle / 'manifest.json').write_text(json.dumps(manifest))

            save()
            with self.assertRaisesRegex(RuntimeError, 'architecture mismatch'):
                config.validate_bundle(profile, bundle, active)
            (app / 'module.so').write_bytes(elf(64, 183))
            save()
            config.validate_bundle(profile, bundle, active)
            (app / 'stale.so').write_bytes(elf(64, 183))
            with self.assertRaisesRegex(RuntimeError, 'unmanifested'):
                config.validate_bundle(profile, bundle, active)
            save()
            (app / 'stale.so').write_bytes(b'changed')
            with self.assertRaisesRegex(RuntimeError, 'hash mismatch'):
                config.validate_bundle(profile, bundle, active)


if __name__ == '__main__':
    unittest.main()

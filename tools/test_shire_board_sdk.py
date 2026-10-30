"""Verify SDK/userspace preparation gates without downloading or flashing."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


sdk_builder = module('shire_board_sdk', 'shire_board_sdk.py')
config = module('shire_board_config', 'shire_board_config.py')


def elf(machine=183, alignment=65536):
    header = bytearray(64)
    header[:6] = b'\x7fELF\x02\x01'
    struct.pack_into('<H', header, 18, machine)
    struct.pack_into('<Q', header, 32, 64)
    struct.pack_into('<HH', header, 54, 56, 1)
    entry = bytearray(56)
    struct.pack_into('<I', entry, 0, 1)
    struct.pack_into('<Q', entry, 48, alignment)
    header.extend(entry)
    return header


class SdkTests(unittest.TestCase):
    def fixture(self, root, *, resolved='BR2_aarch64=y\n', compiler_machine=183, loader_machine=183,
                loader_alignment=65536):
        profile = copy.deepcopy(config.load_profile('rpi5'))
        profile['artifacts'].update(sdk=str(root / 'sdk'), compiler=str(root / 'sdk/bin/aarch64-linux-gcc'),
            rootfs=str(root / 'rootfs.tar'), buildroot_config=str(root / 'aarch64_defconfig'))
        (root / 'aarch64_defconfig').write_text('BR2_aarch64=y\nBR2_TARGET_ROOTFS_TAR=y\n')
        archive = root / 'buildroot.tar.xz'
        archive.write_bytes(b'pinned Buildroot source')
        backend = SimpleNamespace(VERSION='2025.02.6', SOURCE_SHA256='source-hash', SOURCE=root / 'source',
                                  prepare_source=Mock(return_value=archive), run=Mock())
        output = root / 'sdk-buildroot'

        def container(command, **kwargs):
            if command[0] != 'make':
                Path(command[-1]).write_bytes(elf(compiler_machine))
            elif 'defconfig' in command:
                (output / '.config').write_text(resolved)
            elif 'legal-info' in command:
                (output / 'legal-info').mkdir(exist_ok=True)
            else:
                (output / 'target/lib').mkdir(parents=True, exist_ok=True)
                (output / 'target/lib/ld-linux-aarch64.so.1').write_bytes(elf(loader_machine, loader_alignment))
                (output / 'images').mkdir(exist_ok=True)
                (output / 'images/rootfs.tar').write_bytes(b'matching userspace')
                (root / 'sdk/opt/ext-toolchain').mkdir(parents=True, exist_ok=True)
                (root / 'sdk/bin').mkdir(exist_ok=True)
                (root / 'sdk/bin/toolchain-wrapper').touch()
                compiler = root / 'sdk/bin/aarch64-linux-gcc'
                if not compiler.exists():
                    compiler.symlink_to('toolchain-wrapper')
                for name in ('README.txt', 'summary.csv', 'buildroot.config'):
                    (root / 'sdk/opt/ext-toolchain' / name).write_text(name)
        backend.container = Mock(side_effect=container)
        return profile, backend, output

    def test_prepares_matching_sdk_userspace_and_license_receipt_without_building_a_kernel(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            profile, backend, output = self.fixture(root)
            with patch.object(config, 'ROOT', root):
                sdk_builder.prepare(profile, {'mission': 'drm', 'spacecraft': 'sat-1'}, 2, backend=backend, board_config=config)
            receipt = json.loads((output / 'preparation.json').read_text())
            self.assertFalse(receipt['hardware_validated'])
            self.assertEqual(receipt['architecture'], 'aarch64')
            self.assertEqual((root / 'rootfs.tar').read_bytes(), b'matching userspace')
            self.assertIn(f'BR2_HOST_DIR="{root / "sdk"}"', (output / 'shire_defconfig').read_text())
            self.assertTrue((output / 'legal-info/shire-supplement/buildroot.tar.xz').is_file())
            commands = [call.args[0] for call in backend.container.call_args_list]
            self.assertEqual(sum('legal-info' in cmd for cmd in commands), 1)
            self.assertTrue(all('linux' not in cmd and 'uboot' not in cmd for cmd in commands))

    def test_cached_preparation_preserves_compiler_wrapper_dispatch_name(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            profile, backend, _ = self.fixture(root)
            with patch.object(config, 'ROOT', root):
                for _ in range(2):
                    sdk_builder.prepare(profile, {'mission': 'drm', 'spacecraft': 'sat-1'}, 2,
                                        backend=backend, board_config=config)
            compilers = [call.args[0][0] for call in backend.container.call_args_list
                         if call.args[0][0] != 'make']
            self.assertEqual(compilers, [root / 'sdk/bin/aarch64-linux-gcc'] * 2)

    def test_wrong_architecture_or_boot_image_configuration_fails_before_target_build(self):
        for resolved in ('BR2_arm=y\n', 'BR2_aarch64=y\nBR2_LINUX_KERNEL=y\n',
                         'BR2_aarch64=y\nBR2_TARGET_UBOOT=y\n'):
            with self.subTest(resolved=resolved), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                profile, backend, _ = self.fixture(root, resolved=resolved)
                with patch.object(config, 'ROOT', root), self.assertRaises(RuntimeError):
                    sdk_builder.prepare(profile, {'mission': 'drm', 'spacecraft': 'sat-1'}, 2, backend=backend, board_config=config)
                self.assertEqual(backend.container.call_count, 1)
                self.assertFalse((root / 'rootfs.tar').exists())

    def test_probe_and_loader_elf_must_both_match_the_profile_before_rootfs_is_published(self):
        for compiler_machine, loader_machine in ((62, 183), (183, 62)):
            with self.subTest(compiler=compiler_machine, loader=loader_machine), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                profile, backend, _ = self.fixture(root, compiler_machine=compiler_machine,
                                                   loader_machine=loader_machine)
                with patch.object(config, 'ROOT', root), self.assertRaisesRegex(RuntimeError, 'architecture mismatch'):
                    sdk_builder.prepare(profile, {'mission': 'drm', 'spacecraft': 'sat-1'}, 2, backend=backend, board_config=config)
                self.assertFalse((root / 'rootfs.tar').exists())

    def test_prebuilt_4k_loader_is_rejected_for_a_16k_userspace(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            profile, backend, _ = self.fixture(root, resolved='BR2_aarch64=y\nBR2_ARM64_PAGE_SIZE_16K=y\n',
                                               loader_alignment=4096)
            with patch.object(config, 'ROOT', root), self.assertRaisesRegex(RuntimeError, 'page-size mismatch'):
                sdk_builder.prepare(profile, {'mission': 'drm', 'spacecraft': 'sat-1'}, 2,
                                    backend=backend, board_config=config)
            self.assertFalse((root / 'rootfs.tar').exists())

    def test_changed_sdk_inputs_are_rejected_before_modifying_the_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            profile, backend, output = self.fixture(root)
            output.mkdir()
            (output / '.config').write_text('original configuration')
            (output / 'shire-input.sha256').write_text('different inputs')
            with patch.object(config, 'ROOT', root), self.assertRaisesRegex(RuntimeError, 'fresh artifacts.sdk'):
                sdk_builder.prepare(profile, {'mission': 'drm', 'spacecraft': 'sat-1'}, 2, backend=backend, board_config=config)
            backend.prepare_source.assert_not_called()
            backend.run.assert_not_called()
            self.assertEqual((output / '.config').read_text(), 'original configuration')

    def test_existing_supplied_sdk_is_not_overwritten_by_preparation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            profile, backend, _ = self.fixture(root)
            (root / 'sdk').mkdir()
            sentinel = root / 'sdk/keep-this'
            sentinel.write_text('supplied SDK')
            with patch.object(config, 'ROOT', root), self.assertRaisesRegex(RuntimeError, 'already contains files'):
                sdk_builder.prepare(profile, {'mission': 'drm', 'spacecraft': 'sat-1'}, 2,
                                    backend=backend, board_config=config)
            backend.prepare_source.assert_not_called()
            backend.run.assert_not_called()
            self.assertEqual(sentinel.read_text(), 'supplied SDK')


"""Exercise generated network topology and the actual initramfs archive format."""
import importlib.util
import gzip
from pathlib import Path
import tempfile
import unittest
import subprocess
from unittest.mock import patch

import yaml


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


qemu = module("shire_qemu", "shire-qemu.py")
entry = module("shire_qemu_entry", "shire-qemu-entrypoint.py")


class QemuTests(unittest.TestCase):
    def test_work_directory_keeps_sdk_and_source_together_and_rejects_unmounted_paths(self):
        with patch.object(qemu, 'WORK'), patch.object(qemu, 'SOURCE'), patch.object(qemu, 'OUTPUT'):
            work = qemu.ROOT / 'build/boards/custom-qemu'
            qemu.configure_work_directory(work)
            self.assertEqual(qemu.WORK, work)
            self.assertEqual(qemu.SOURCE, work / f'buildroot-{qemu.VERSION}')
            self.assertEqual(qemu.OUTPUT, work / 'output')
            with self.assertRaisesRegex(RuntimeError, 'inside the checkout'):
                qemu.configure_work_directory(Path('/tmp/external-shire-qemu'))

    def test_deploy_rejects_invalid_bundle_before_packaging_or_building_image(self):
        with patch.object(qemu, 'validate_bundle', side_effect=RuntimeError('ELF architecture mismatch')), \
                patch.object(qemu, 'package') as package, patch.object(qemu, 'run') as run:
            with self.assertRaisesRegex(RuntimeError, 'architecture mismatch'):
                qemu.deploy()
            package.assert_not_called()
            run.assert_not_called()

    def test_deploy_uses_selected_work_directory_without_cross_compiling(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            images = work / 'output/images'
            images.mkdir(parents=True)
            for name in ('uImage', 'zynq-zc702.dtb'):
                (images / name).write_text(name)
            (work / 'drm/sat-1').mkdir(parents=True)
            with patch.object(qemu, 'WORK', work), patch.object(qemu, 'OUTPUT', work / 'output'), \
                    patch.object(qemu, 'active_config', return_value={'mission': 'drm', 'spacecraft': 'sat-1'}), \
                    patch.object(qemu, 'validate_bundle') as validate, \
                    patch.object(qemu, 'package') as package, patch.object(qemu, 'run') as run, \
                    patch.object(qemu, 'container') as cross_compile, patch.object(qemu, 'render_compose') as compose:
                qemu.deploy()
                validate.assert_called_once()
                package.assert_called_once_with()
                cross_compile.assert_not_called()
                self.assertEqual(run.call_args.args[0][-1], work / 'drm/sat-1/runtime')
                compose.assert_called_once_with({'mission': 'drm', 'spacecraft': 'sat-1'}, 'shire-fsw-drm-qemu:sat-1')
                self.assertEqual((work / 'drm/sat-1/runtime/uImage').read_text(), 'uImage')

    def test_failed_or_incomplete_baseline_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / "report.json"
            for content in ('{"passed":false,"trials":[{}]}',
                            '{"passed":true,"trials":[]}', '[]', 'broken json'):
                report.write_text(content)
                with self.assertRaises(RuntimeError):
                    qemu.load_baseline(report)
            report.write_text('{"passed":true,"trials":[{"ticks":3000}]}')
            self.assertEqual(qemu.load_baseline(report)["trials"][0]["ticks"], 3000)
            self.assertIsNone(qemu.load_baseline(None))

    def test_missing_image_stops_start_before_archive_allocation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            compose = root / "build/drm/shire-qemu-compose.yaml"
            compose.parent.mkdir(parents=True)
            compose.touch()
            resolved = '{"services":{"shire-42":{"image":"shire-42-drm:sat-1"}}}'
            missing = subprocess.CompletedProcess([], 1, stderr="No such image: shire-42-drm:sat-1")
            with patch.object(qemu, "ROOT", root), \
                    patch.object(qemu, "active_config", return_value={"mission": "drm", "spacecraft": "sat-1"}), \
                    patch.object(qemu.subprocess, "check_output", return_value=resolved), \
                    patch.object(qemu.subprocess, "run", return_value=missing), \
                    patch.object(qemu, "run") as launch:
                with self.assertRaisesRegex(RuntimeError, "shire-42-drm:sat-1: make 42"):
                    qemu.compose_command("start")
                launch.assert_not_called()

    def test_daemon_errors_are_not_reported_as_missing_images(self):
        resolved = '{"services":{"shire-42":{"image":"shire-42-drm:sat-1"}}}'
        failed = subprocess.CompletedProcess([], 1, stderr="permission denied on Docker socket")
        with patch.object(qemu.subprocess, "check_output", return_value=resolved), \
                patch.object(qemu.subprocess, "run", return_value=failed):
            with self.assertRaisesRegex(RuntimeError, "permission denied on Docker socket"):
                qemu.require_runtime_images(Path("compose.yaml"))

    def test_packaging_restarts_from_base_and_removes_stale_bundle_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "mission/spacecraft"
            app = directory / "bundle/app"
            app.mkdir(parents=True)
            (app / "core-cpu1").write_bytes(b"target executable")
            (app / "obsolete.so").write_bytes(b"old app")
            (directory / "runtime").mkdir()
            output = root / "output"
            (output / "images").mkdir(parents=True)
            base = gzip.compress(b"base archive", mtime=0)
            (output / "images/rootfs.cpio.gz").write_bytes(base)
            (root / "id_ed25519").touch()
            (root / "id_ed25519.pub").write_text("ssh-ed25519 public-test-key\n")
            with patch.object(qemu, "WORK", root), patch.object(qemu, "OUTPUT", output), \
                    patch.object(qemu, "active_config", return_value={}), \
                    patch.object(qemu, "fsw_directory", return_value=directory / "fsw"):
                qemu.package()
                packaged = directory / "runtime/rootfs.cpio.gz"
                first = packaged.read_bytes()
                qemu.package()
                self.assertEqual(packaged.read_bytes(), first)
                self.assertTrue(first.startswith(base))
                (app / "obsolete.so").unlink()
                qemu.package()
                archive = gzip.decompress(packaged.read_bytes()[len(base):])
                self.assertNotIn(b"obsolete.so", archive)
                self.assertIn(b"target executable", archive)
                self.assertIn(b"ssh-ed25519 public-test-key", archive)

    def test_overlay_preserves_paths_contents_modes_and_root_ownership(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "etc/init.d").mkdir(parents=True)
            script = root / "etc/init.d/S99shire"
            script.write_text("#!/bin/sh\necho boot\n")
            script.chmod(0o755)
            archive = entry.overlay_archive(root)
            cursor = 0
            files = {}
            while True:
                self.assertEqual(archive[cursor:cursor + 6], b"070701")
                fields = [int(archive[cursor + 6 + i * 8:cursor + 14 + i * 8], 16) for i in range(13)]
                name_size = fields[11]
                name = archive[cursor + 110:cursor + 110 + name_size - 1].decode()
                self.assertEqual(fields[2:4], [0, 0])
                cursor = (cursor + 110 + name_size + 3) & ~3
                data = archive[cursor:cursor + fields[6]]
                cursor = (cursor + fields[6] + 3) & ~3
                if name == "TRAILER!!!":
                    break
                files[name] = (fields[1], data)
            self.assertEqual(files["etc/init.d/S99shire"], (0o100755, script.read_bytes()))
            self.assertIn("etc/init.d", files)

    def test_qemu_profile_changes_only_required_services(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "build/drm"
            directory.mkdir(parents=True)
            original = {"services": {
                "shire-server": {"environment": []},
                "shire-director": {"environment": []},
                "shire-gsw": {"image": "gsw"},
                "shire-fsw": {"image": "native", "environment": [], "container_name": "shire-fsw-sat-1",
                              "volumes": ["ipc:/tmp"], "sysctls": {"fs.mqueue.msg_max": 10000}}}}
            source = directory / "shire-compose.yaml"
            source.write_text(yaml.safe_dump(original))
            with patch.object(qemu, "ROOT", root):
                qemu.render_compose({"mission": "drm"}, "arm32-image")
            actual = yaml.safe_load((directory / "shire-qemu-compose.yaml").read_text())
            self.assertEqual(yaml.safe_load(source.read_text()), original)
            self.assertEqual(actual["services"]["shire-gsw"],
                             {**original["services"]["shire-gsw"], "pull_policy": "never"})
            self.assertTrue(all(service["pull_policy"] == "never"
                                for service in actual["services"].values()))
            fsw = actual["services"]["shire-fsw"]
            self.assertEqual(fsw["image"], "arm32-image")
            self.assertEqual(fsw["container_name"], "shire-fsw-sat-1")
            self.assertNotIn("volumes", fsw)
            self.assertNotIn("sysctls", fsw)
            self.assertIn("SIMULITH_DEVICE_HOST=shire-director", fsw["environment"])
            self.assertIn("SHIRE_SYNCHRONIZE_GROUND_OUTPUT=1", fsw["environment"])
            self.assertIn("SIMULITH_SYNC_TRANSPORT=zmq", actual["services"]["shire-director"]["environment"])


if __name__ == "__main__":
    unittest.main()

"""Safe bulk archive cleanup tests; Docker is never called by these tests.

Run directly: python3 tools/test_shire_archives.py
"""
from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).with_name("shire-archives.py")
SPEC = importlib.util.spec_from_file_location("shire_archives", SOURCE)
assert SPEC and SPEC.loader
archives = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(archives)


def result(args: tuple[str, ...], output: str = "") -> subprocess.CompletedProcess[str]:
    return subprocess.CompletedProcess(args, 0, output, "")


class PurgeArchivesTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runs = ["a" * 32, "b" * 32]
        self.volumes = [f"shire-yamcs-archive-{run}" for run in self.runs]
        self.rows = list(zip(self.volumes, ({"shire.run": run} for run in self.runs)))

    def test_purge_removes_stopped_mounts_volumes_and_only_run_images(self) -> None:
        calls: list[tuple[str, ...]] = []

        def fake_docker(*args: str) -> subprocess.CompletedProcess[str]:
            calls.append(args)
            if args[:2] == ("ps", "-aq") and args[-1] == f"volume={self.volumes[0]}":
                return result(args, "stopped-replay\n")
            return result(args)

        with (patch.object(archives, "records", return_value=self.rows),
              patch.object(archives, "docker", side_effect=fake_docker),
              patch.object(archives, "image_exists",
                           side_effect=lambda image: image.endswith(self.runs[0])),
              redirect_stdout(io.StringIO()) as output):
            archives.purge_archives()

        self.assertEqual(calls[:2], [
            ("ps", "-q", "--filter", f"volume={self.volumes[0]}"),
            ("ps", "-q", "--filter", f"volume={self.volumes[1]}"),
        ])
        self.assertIn(("rm", "stopped-replay"), calls)
        self.assertIn(("volume", "rm", self.volumes[0]), calls)
        self.assertIn(("volume", "rm", self.volumes[1]), calls)
        self.assertIn(("image", "rm", f"shire-gsw-archive:{self.runs[0]}"), calls)
        self.assertNotIn(("image", "rm", f"shire-gsw-archive:{self.runs[1]}"), calls)
        self.assertIn("Removed 2 retained SHIRE run archive(s)", output.getvalue())

    def test_image_removal_failure_occurs_after_all_volumes_are_removed(self) -> None:
        calls: list[tuple[str, ...]] = []

        def fake_docker(*args: str) -> subprocess.CompletedProcess[str]:
            calls.append(args)
            if args == ("image", "rm", f"shire-gsw-archive:{self.runs[0]}"):
                raise ValueError("image still in use")
            return result(args)

        with (patch.object(archives, "records", return_value=self.rows),
              patch.object(archives, "docker", side_effect=fake_docker),
              patch.object(archives, "image_exists", return_value=True),
              redirect_stdout(io.StringIO())):
            with self.assertRaisesRegex(ValueError, "image still in use"):
                archives.purge_archives()

        self.assertLess(calls.index(("volume", "rm", self.volumes[1])),
                        calls.index(("image", "rm", f"shire-gsw-archive:{self.runs[0]}")))

    def test_running_archive_aborts_before_any_deletion(self) -> None:
        calls: list[tuple[str, ...]] = []

        def fake_docker(*args: str) -> subprocess.CompletedProcess[str]:
            calls.append(args)
            if args == ("ps", "-q", "--filter", f"volume={self.volumes[1]}"):
                return result(args, "live-yamcs\n")
            return result(args)

        with (patch.object(archives, "records", return_value=self.rows),
              patch.object(archives, "docker", side_effect=fake_docker),
              patch.object(archives, "image_exists") as image_exists):
            with self.assertRaisesRegex(ValueError, "stop those runs"):
                archives.purge_archives()

        self.assertEqual(calls, [
            ("ps", "-q", "--filter", f"volume={self.volumes[0]}"),
            ("ps", "-q", "--filter", f"volume={self.volumes[1]}"),
        ])
        image_exists.assert_not_called()

    def test_records_excludes_legacy_and_other_project_volumes(self) -> None:
        names = (self.volumes[0] + "\ngsw-data\n"
                 "gtosat-yamcs-archive-" + self.runs[1] + "\n")

        def fake_docker(*args: str) -> subprocess.CompletedProcess[str]:
            self.assertEqual(args, ("volume", "ls", "-q",
                                    "--filter", "label=shire.archive=true"))
            return result(args, names)

        with (patch.object(archives, "docker", side_effect=fake_docker),
              patch.object(archives, "get_archive",
                           return_value=self.rows[0]) as get_archive):
            self.assertEqual(archives.records(), [self.rows[0]])
        get_archive.assert_called_once_with(self.runs[0])


if __name__ == "__main__":
    unittest.main()

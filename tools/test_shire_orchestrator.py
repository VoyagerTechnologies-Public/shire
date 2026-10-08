"""Verify configuration discovery is independent of the caller's directory."""
from pathlib import Path
import shutil
import re
import subprocess
import sys
import tempfile
import unittest

import yaml


ROOT = Path(__file__).resolve().parent.parent


def prepare_repository(temporary, scenario=None):
    root = Path(temporary) / "repository"
    shutil.copytree(ROOT / "cfg", root / "cfg")
    (root / "tools").mkdir()
    shutil.copytree(ROOT / "comp/eps/support", root / "comp/eps/support")
    shutil.copytree(ROOT / "comp/eps/gsw", root / "comp/eps/gsw")
    for name in ("shire-orchestrator.py", "shire_provenance.py", "shire_eps_config.py", "shire_eps_stacks.py"):
        shutil.copy2(ROOT / "tools" / name, root / "tools" / name)
    if scenario:
        (root / "build").mkdir()
        (root / "build/active.yaml").write_text(yaml.safe_dump({
            "mission": "drm", "spacecraft": "sat-1", "scenario": scenario,
            "cli": "demo", "fsw_dir": "cfs", "gsw_dir": "yamcs", "log_mode": "none",
        }))
    return root


class ConfigurationPathsTests(unittest.TestCase):
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

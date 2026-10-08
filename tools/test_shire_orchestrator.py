"""Verify configuration discovery is independent of the caller's directory."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import yaml


ROOT = Path(__file__).resolve().parent.parent


class ConfigurationPathsTests(unittest.TestCase):
    def test_configuration_generation_from_root_tools_and_outside(self):
        for location in ("root", "tools", "outside"):
            with self.subTest(location=location), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "repository"
                shutil.copytree(ROOT / "cfg", root / "cfg")
                (root / "tools").mkdir()
                shutil.copytree(ROOT / "comp/eps/support", root / "comp/eps/support")
                shutil.copytree(ROOT / "comp/eps/gsw", root / "comp/eps/gsw")
                for name in ("shire-orchestrator.py", "shire_provenance.py", "shire_eps_config.py", "shire_eps_stacks.py"):
                    shutil.copy2(ROOT / "tools" / name, root / "tools" / name)
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


if __name__ == "__main__":
    unittest.main()

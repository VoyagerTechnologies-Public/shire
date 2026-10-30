"""Component mold: name handling, the generated component, and the files its closing instructions send you to."""
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent

_spec = importlib.util.spec_from_file_location("shire_comp_mold", ROOT / "tools/shire-comp-mold.py")
mold = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mold)


class NameTests(unittest.TestCase):
    def test_names_are_lowercased_and_must_be_identifiers(self):
        self.assertEqual(mold.validate_component_name("Thermal_2"), "thermal_2")
        for bad in ("", "2thermal", "ther-mal", "ther mal", "_thermal"):
            with self.subTest(bad), self.assertRaises(ValueError):
                mold.validate_component_name(bad)

    def test_variants_cover_the_three_cases_the_template_uses(self):
        self.assertEqual(mold.get_name_variants("Thermal"), {"lower": "thermal", "first": "Thermal", "upper": "THERMAL"})


class GenerationTests(unittest.TestCase):
    """Runs the real script on a copy of the demo component, as `make mold` does."""

    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.workspace = Path(cls.temporary.name)
        (cls.workspace / "tools").mkdir()
        shutil.copy2(ROOT / "tools/shire-comp-mold.py", cls.workspace / "tools/shire-comp-mold.py")
        shutil.copytree(ROOT / "comp/demo", cls.workspace / "comp/demo", ignore=shutil.ignore_patterns("build"))
        cls.result = cls.run_mold("thermal")

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @classmethod
    def run_mold(cls, *arguments, answer=""):
        return subprocess.run([sys.executable, str(cls.workspace / "tools/shire-comp-mold.py"), *arguments],
                              input=answer, capture_output=True, text=True, timeout=60)

    def test_the_component_is_created_with_renamed_files_and_symbols(self):
        self.assertEqual(self.result.returncode, 0, self.result.stdout + self.result.stderr)
        component = self.workspace / "comp/thermal"
        self.assertTrue((component / "src/thermal_app.c").is_file())
        self.assertFalse(list(component.rglob("*demo*")), "no file or folder keeps the template's name")
        source = (component / "src/thermal_app.c").read_text()
        self.assertIn("THERMAL_AppMain", source)
        self.assertNotIn("DEMO_AppMain", source)

    def test_the_template_is_left_alone(self):
        self.assertTrue((self.workspace / "comp/demo/src/demo_app.c").is_file())

    def test_every_file_the_instructions_name_exists(self):
        """The instructions are the only guide to the integration, so a stale path misleads every new component."""
        paths = re.findall(r"^ {7}(\./\S+)$", self.result.stdout, re.MULTILINE)
        self.assertGreaterEqual(len(paths), 6, self.result.stdout)
        for path in paths:
            with self.subTest(path):
                if path.startswith("./yamcs/") and not (ROOT / "yamcs/src").exists():
                    self.skipTest("the yamcs submodule is not checked out")
                self.assertTrue((ROOT / path).is_file(), f"{path} is named in the mold's instructions but does not exist")

    def test_an_existing_component_is_kept_unless_the_prompt_is_answered_yes(self):
        marker = self.workspace / "comp/thermal/my_work.txt"
        marker.write_text("edited by hand")
        declined = self.run_mold("thermal", answer="n\n")
        self.assertEqual(declined.returncode, 0)
        self.assertIn("Aborting component creation", declined.stdout)
        self.assertTrue(marker.exists(), "declining leaves the component as it was")
        # Nothing answers the prompt when input ends, which must not replace the component either.
        ended = self.run_mold("thermal")
        self.assertNotEqual(ended.returncode, 0)
        self.assertTrue(marker.exists())
        accepted = self.run_mold("thermal", answer="y\n")
        self.assertEqual(accepted.returncode, 0, accepted.stdout + accepted.stderr)
        self.assertFalse(marker.exists(), "accepting regenerates the component from the template")
        self.assertTrue((self.workspace / "comp/thermal/src/thermal_app.c").is_file())

    def test_an_invalid_name_or_missing_source_is_an_error(self):
        self.assertNotEqual(self.run_mold("9lives").returncode, 0)
        result = self.run_mold("another", "--source", "missing")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Source component 'missing' not found", result.stderr)


if __name__ == "__main__":
    unittest.main()

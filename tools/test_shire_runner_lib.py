"""Scenario log assertions: counting matching container log lines against min/max bounds."""
import unittest

from shire_runner_lib import evaluate_log_assertions

LOGS = {
    "cryptolib": "boot\nCryptoLib link security: uplink (TC) encrypted\nTM failed status = -21\nTM failed status = -23\n",
    "fsw": "",
}


class LogAssertionTests(unittest.TestCase):
    def test_no_assertions_means_no_results(self):
        self.assertEqual(evaluate_log_assertions(None, LOGS), [])
        self.assertEqual(evaluate_log_assertions([], LOGS), [])

    def test_min_defaults_to_one_and_counts_matching_lines(self):
        result, = evaluate_log_assertions([{"container": "cryptolib", "pattern": r"status = -2\d"}], LOGS)
        self.assertEqual((result["count"], result["min"], result["max"], result["passed"]), (2, 1, None, True))

    def test_bounds_are_inclusive(self):
        cases = [({"min": 2, "max": 2}, True), ({"min": 3}, False), ({"max": 1}, False), ({"min": 0, "max": 0}, False)]
        for bounds, expected in cases:
            with self.subTest(bounds=bounds):
                result, = evaluate_log_assertions(
                    [{"container": "cryptolib", "pattern": "status = -2", **bounds}], LOGS)
                self.assertIs(result["passed"], expected)

    def test_absence_can_be_required(self):
        result, = evaluate_log_assertions([{"container": "fsw", "pattern": "ERROR", "min": 0, "max": 0}], LOGS)
        self.assertTrue(result["passed"])
        result, = evaluate_log_assertions([{"container": "fsw", "pattern": "ERROR"}], LOGS)
        self.assertFalse(result["passed"])

    def test_description_is_carried_into_the_report(self):
        result, = evaluate_log_assertions(
            [{"container": "cryptolib", "pattern": "boot", "description": "starts"}], LOGS)
        self.assertEqual(result["description"], "starts")

    def test_rejects_malformed_entries(self):
        for entry in ({"container": "nope", "pattern": "x"}, {"container": "fsw"}, {"container": "fsw", "pattern": ""},
                      {"container": "fsw", "pattern": "x", "min": -1}, {"container": "fsw", "pattern": "x", "max": "2"},
                      {"container": "fsw", "pattern": "x", "min": True}):
            with self.subTest(entry=entry):
                with self.assertRaises(ValueError):
                    evaluate_log_assertions([entry], LOGS)


if __name__ == "__main__":
    unittest.main()

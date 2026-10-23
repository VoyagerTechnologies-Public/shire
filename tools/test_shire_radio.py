"""Radio link configuration: normalization, station selection, and the rendered C header."""
from pathlib import Path
import re
import unittest

from jinja2 import Environment, FileSystemLoader
import yaml

from shire_radio_config import MAX_GROUND_STATIONS, normalize_radio

ROOT = Path(__file__).resolve().parent.parent
CATALOG = [
    {"name": "A", "world": "EARTH", "lon_deg": -117.0, "lat_deg": 33.0},
    {"name": "B", "world": "EARTH", "lon_deg": -105.0, "lat_deg": 39.7, "min_elev_deg": 10, "enabled": False},
    {"name": "Moon base", "world": "LUNA", "lon_deg": 0.0, "lat_deg": 0.0},
]


def defaults():
    return dict(yaml.safe_load((ROOT / "comp/radio/support/device_config.yaml").read_text())["radio"])


def render(config):
    env = Environment(loader=FileSystemLoader(str(ROOT / "comp/radio/support")))
    return env.get_template("device_config.j2").render(config=config)


class NormalizeRadioTests(unittest.TestCase):
    def test_shipped_defaults_describe_an_ideal_link_with_every_earth_station(self):
        cfg = normalize_radio(defaults(), CATALOG)
        self.assertEqual(cfg["link_mode"], "always")
        self.assertEqual((cfg["uplink_delay_ms"], cfg["downlink_delay_ms"]), (0, 0))
        self.assertEqual((cfg["uplink_drop_ppm"], cfg["downlink_drop_ppm"]), (0, 0))
        self.assertEqual([s["name"] for s in cfg["ground_stations"]], ["A", "B"])  # LUNA excluded

    def test_station_defaults_and_overrides(self):
        a, b = normalize_radio(defaults(), CATALOG)["ground_stations"]
        self.assertEqual((a["min_elev_deg"], a["enabled"]), (5.0, True))
        self.assertEqual((b["min_elev_deg"], b["enabled"]), (10.0, False))

    def test_selection_orders_stations_and_defines_backdoor_indices(self):
        cfg = defaults()
        cfg["ground_stations"] = ["B", "A"]
        self.assertEqual([s["name"] for s in normalize_radio(cfg, CATALOG)["ground_stations"]], ["B", "A"])

    def test_empty_selection_means_no_stations(self):
        cfg = defaults()
        cfg["ground_stations"] = []
        self.assertEqual(normalize_radio(cfg, CATALOG)["ground_stations"], [])

    def test_link_mode_is_case_insensitive(self):
        cfg = defaults()
        cfg["link_mode"] = "AUTO"
        self.assertEqual(normalize_radio(cfg, CATALOG)["link_mode"], "auto")

    def test_link_security_defaults_to_encrypted_in_both_directions(self):
        cfg = normalize_radio(defaults(), CATALOG)
        self.assertIs(cfg["uplink_encryption"], True)
        self.assertIs(cfg["downlink_encryption"], True)
        partial = defaults()
        del partial["uplink_encryption"]
        del partial["downlink_encryption"]
        self.assertIs(normalize_radio(partial, CATALOG)["uplink_encryption"], True)

    def test_link_security_must_be_boolean(self):
        for key in ("uplink_encryption", "downlink_encryption"):
            for value in ("yes", 1, 0, None, "false"):
                with self.subTest(key=key, value=value):
                    cfg = defaults()
                    cfg[key] = value
                    with self.assertRaises(ValueError):
                        normalize_radio(cfg, CATALOG)

    def test_input_is_not_mutated(self):
        cfg = defaults()
        before = dict(cfg)
        normalize_radio(cfg, CATALOG)
        self.assertEqual(cfg, before)

    def test_rejects_invalid_values(self):
        cases = {
            "link_mode": ["sometimes", 1, None],
            "uplink_delay_ms": [-1, 1.5, "10", True, 2**32],
            "downlink_drop_ppm": [-1, 1_000_001, 0.5, None],
            "link_seed": [-1, 2**32],
            "link_queue_depth": [0, 5000],
            "ground_stations": ["A", ["nowhere"], ["A", "A"], ["Moon base"], [1]],
        }
        for key, values in cases.items():
            for value in values:
                with self.subTest(key=key, value=value):
                    cfg = defaults()
                    cfg[key] = value
                    with self.assertRaises(ValueError):
                        normalize_radio(cfg, CATALOG)

    def test_rejects_malformed_catalog_entries(self):
        for entry in ({"name": "X", "lat_deg": 91, "lon_deg": 0},
                      {"name": "X", "lat_deg": 0, "lon_deg": 181},
                      {"name": "X", "lat_deg": 0},
                      {"name": "X", "lat_deg": 0, "lon_deg": 0, "min_elev_deg": 95},
                      {"name": "X", "lat_deg": 0, "lon_deg": 0, "enabled": "yes"}):
            with self.subTest(entry=entry):
                with self.assertRaises(ValueError):
                    normalize_radio(defaults(), [entry])

    def test_rejects_more_stations_than_the_sim_supports(self):
        catalog = [{"name": f"S{i}", "lat_deg": 0, "lon_deg": 0} for i in range(MAX_GROUND_STATIONS + 1)]
        with self.assertRaises(ValueError):
            normalize_radio(defaults(), catalog)

    def test_repository_catalog_is_valid_for_the_radio(self):
        catalog = yaml.safe_load((ROOT / "cfg/drm/ground_stations.yaml").read_text())["ground_stations"]
        self.assertGreaterEqual(len(normalize_radio(defaults(), catalog)["ground_stations"]), 1)


class RadioHeaderTests(unittest.TestCase):
    def test_header_carries_link_settings_and_station_table(self):
        cfg = defaults()
        cfg.update(link_mode="auto", uplink_delay_ms=250, downlink_drop_ppm=5000, link_seed=99)
        header = render(normalize_radio(cfg, CATALOG))
        self.assertIn("#define RADIO_CFG_LINK_MODE                 RADIO_LINK_MODE_AUTO", header)
        for name, value in (("UPLINK_DELAY_MS", "250"), ("DOWNLINK_DROP_PPM", "5000"), ("LINK_SEED", "99U"),
                            ("NUM_GROUND_STATIONS", "2")):
            self.assertRegex(header, rf"#define RADIO_CFG_(LINK_)?{name}\s+{value}\b")
        rows = re.findall(r"\{ (-?[\d.]+), (-?[\d.]+), ([\d.]+), ([01]) \}", header)
        self.assertEqual(rows, [("33.000000000", "-117.000000000", "5.000", "1"),
                                ("39.700000000", "-105.000000000", "10.000", "0")])

    def test_header_carries_link_security_settings(self):
        header = render(normalize_radio(defaults(), CATALOG))
        self.assertRegex(header, r"RADIO_CFG_UPLINK_ENCRYPTION\s+1\b")
        self.assertRegex(header, r"RADIO_CFG_DOWNLINK_ENCRYPTION\s+1\b")
        cfg = defaults()
        cfg.update(uplink_encryption=False, downlink_encryption=True)
        header = render(normalize_radio(cfg, CATALOG))
        self.assertRegex(header, r"RADIO_CFG_UPLINK_ENCRYPTION\s+0\b")
        self.assertRegex(header, r"RADIO_CFG_DOWNLINK_ENCRYPTION\s+1\b")

    def test_header_without_stations_keeps_a_placeholder_row(self):
        cfg = defaults()
        cfg["ground_stations"] = []
        header = render(normalize_radio(cfg, CATALOG))
        self.assertRegex(header, r"RADIO_CFG_NUM_GROUND_STATIONS\s+0\b")
        self.assertIn("{ 0.0, 0.0, 0.0, 0 }", header)

    def test_station_names_cannot_break_out_of_their_comment(self):
        catalog = [{"name": "evil */ #error x", "lat_deg": 0, "lon_deg": 0}]
        header = render(normalize_radio(defaults(), catalog))
        self.assertNotIn("*/ #error", header)


if __name__ == "__main__":
    unittest.main()

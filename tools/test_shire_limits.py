"""Telemetry limits: normalization, cascade merging, and application to XTCE alarm ranges."""
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

from shire_limits_config import (LimitError, apply_limits, mdb_sources, merge_limits, normalize_limits, resolve_limits,
                                 root_space_system, write_limited_mdb)

ROOT = Path(__file__).resolve().parent.parent
NS = "http://www.omg.org/spec/XTCE/20180204"

SAMPLE = f"""<?xml version='1.0' encoding='UTF-8'?>
<xtce:SpaceSystem xmlns:xtce="{NS}" name="DEV">
  <xtce:TelemetryMetaData>
    <xtce:ParameterTypeSet>
      <xtce:FloatParameterType name="VOLTS_Type">
        <xtce:IntegerDataEncoding sizeInBits="8" encoding="unsigned" />
        <xtce:UnitSet><xtce:Unit>V</xtce:Unit></xtce:UnitSet>
      </xtce:FloatParameterType>
      <xtce:IntegerParameterType name="COUNT_Type" signed="false">
        <xtce:IntegerDataEncoding sizeInBits="16" encoding="unsigned" />
        <xtce:DefaultAlarm><xtce:StaticAlarmRanges><xtce:WatchRange maxInclusive="1" /></xtce:StaticAlarmRanges></xtce:DefaultAlarm>
      </xtce:IntegerParameterType>
      <xtce:EnumeratedParameterType name="STATE_Type">
        <xtce:IntegerDataEncoding sizeInBits="8" encoding="unsigned" />
        <xtce:EnumerationList><xtce:Enumeration value="0" label="OFF" /></xtce:EnumerationList>
      </xtce:EnumeratedParameterType>
    </xtce:ParameterTypeSet>
    <xtce:ParameterSet>
      <xtce:Parameter name="BUS" parameterTypeRef="VOLTS_Type" />
      <xtce:Parameter name="AUX" parameterTypeRef="VOLTS_Type" />
      <xtce:Parameter name="COUNT" parameterTypeRef="COUNT_Type" />
      <xtce:Parameter name="STATE" parameterTypeRef="STATE_Type" />
    </xtce:ParameterSet>
  </xtce:TelemetryMetaData>
  <xtce:SpaceSystem name="PACKET">
    <xtce:TelemetryMetaData>
      <xtce:ParameterTypeSet>
        <xtce:FloatParameterType name="TEMP_Type"><xtce:IntegerDataEncoding sizeInBits="8" /></xtce:FloatParameterType>
      </xtce:ParameterTypeSet>
      <xtce:ParameterSet><xtce:Parameter name="TEMP" parameterTypeRef="TEMP_Type" /></xtce:ParameterSet>
    </xtce:TelemetryMetaData>
  </xtce:SpaceSystem>
</xtce:SpaceSystem>
"""

BUS = {"/DEV/BUS": {"yellow": {"min": 20}, "red": {"min": 18, "max": 30}}}


def tag(name):
    return f"{{{NS}}}{name}"


def types_of(text):
    root = ET.fromstring(text)
    return {t.get("name"): t for t in root.find(f"{tag('TelemetryMetaData')}/{tag('ParameterTypeSet')}")}, root


def parameter_ref(root, name):
    for p in root.iter(tag("Parameter")):
        if p.get("name") == name:
            return p.get("parameterTypeRef")


class NormalizeTests(unittest.TestCase):
    def test_numbers_become_floats_and_empty_means_no_limits(self):
        self.assertEqual(normalize_limits(BUS), {"/DEV/BUS": {"yellow": {"min": 20.0}, "red": {"min": 18.0, "max": 30.0}}})
        self.assertEqual(normalize_limits(None), {})
        self.assertEqual(normalize_limits({"/DEV/BUS": None}), {})

    def test_a_parameter_whose_levels_are_all_null_has_no_limits(self):
        self.assertEqual(normalize_limits({"/DEV/BUS": {"yellow": None, "red": None}}), {})

    def test_rejects_malformed_limits(self):
        cases = {
            "qualified name": {"BUS": {"red": {"min": 1}}},
            "qualified name ": {"/BUS": {"red": {"min": 1}}},
            "unknown level": {"/DEV/BUS": {"orange": {"min": 1}}},
            "min, max, or both": {"/DEV/BUS": {"red": {}}},
            "unknown key": {"/DEV/BUS": {"red": {"lo": 1}}},
            "must be a number": {"/DEV/BUS": {"red": {"min": "low"}}},
            "must be a number ": {"/DEV/BUS": {"red": {"min": True}}},
            "finite": {"/DEV/BUS": {"red": {"min": float("inf")}}},
            "below max": {"/DEV/BUS": {"red": {"min": 5, "max": 5}}},
            "red min": {"/DEV/BUS": {"yellow": {"min": 10}, "red": {"min": 12}}},
            "red max": {"/DEV/BUS": {"yellow": {"max": 10}, "red": {"max": 8}}},
            "yellow, red, or both": {"/DEV/BUS": 5},
            "mapping": [1, 2],
        }
        for fragment, raw in cases.items():
            with self.subTest(fragment):
                with self.assertRaisesRegex(LimitError, fragment.strip()):
                    normalize_limits(raw)

    def test_one_sided_levels_need_no_counterpart(self):
        self.assertEqual(normalize_limits({"/DEV/BUS": {"red": {"max": 9}}}), {"/DEV/BUS": {"red": {"max": 9.0}}})


class MergeTests(unittest.TestCase):
    def test_later_layers_win_per_level(self):
        base = {"/DEV/BUS": {"yellow": {"min": 20}, "red": {"min": 18}}, "/DEV/AUX": {"red": {"min": 1}}}
        scenario = {"/DEV/BUS": {"yellow": {"min": 21}}}
        merged = merge_limits(base, None, scenario)
        self.assertEqual(merged["/DEV/BUS"], {"yellow": {"min": 21}, "red": {"min": 18}})
        self.assertEqual(merged["/DEV/AUX"], {"red": {"min": 1}})

    def test_null_removes_a_parameter_or_a_level(self):
        base = {"/DEV/BUS": {"yellow": {"min": 20}, "red": {"min": 18}}, "/DEV/AUX": {"red": {"min": 1}}}
        merged = merge_limits(base, {"/DEV/BUS": {"yellow": None}, "/DEV/AUX": None})
        self.assertEqual(merged, {"/DEV/BUS": {"red": {"min": 18}}})
        self.assertEqual(merge_limits(base, {"/DEV/BUS": {"yellow": None, "red": None}}).get("/DEV/BUS"), None)

    def test_a_layer_or_a_parameter_that_is_not_a_mapping_is_rejected(self):
        with self.assertRaisesRegex(LimitError, "mapping of parameter name"):
            merge_limits(["/DEV/BUS"])
        with self.assertRaisesRegex(LimitError, "must give yellow, red, or both"):
            merge_limits({"/DEV/BUS": 5})

    def test_inputs_are_not_modified(self):
        base = {"/DEV/BUS": {"red": {"min": 18}}}
        merge_limits(base, {"/DEV/BUS": {"red": None}})
        self.assertEqual(base, {"/DEV/BUS": {"red": {"min": 18}}})

    def test_resolve_validates_the_merged_result(self):
        with self.assertRaises(LimitError):
            resolve_limits({"/DEV/BUS": {"yellow": {"min": 20}}}, {"/DEV/BUS": {"red": {"min": 25}}})
        self.assertEqual(resolve_limits(None, None), {})


class ApplyTests(unittest.TestCase):
    def test_limited_parameter_gets_its_own_type_with_ranges(self):
        text, applied = apply_limits(SAMPLE, normalize_limits(BUS))
        self.assertEqual(applied, {"/DEV/BUS"})
        types, root = types_of(text)
        self.assertEqual(parameter_ref(root, "BUS"), "VOLTS_Type_BUS_Limits")
        limited = types["VOLTS_Type_BUS_Limits"]
        ranges = limited.find(f"{tag('DefaultAlarm')}/{tag('StaticAlarmRanges')}")
        self.assertEqual([r.tag.split("}")[1] for r in ranges], ["WarningRange", "CriticalRange"])
        self.assertEqual(dict(ranges[0].attrib), {"minInclusive": "20.0"})
        self.assertEqual(dict(ranges[1].attrib), {"minInclusive": "18.0", "maxInclusive": "30.0"})

    def test_other_parameters_sharing_the_type_keep_it_unlimited(self):
        text, _ = apply_limits(SAMPLE, normalize_limits(BUS))
        types, root = types_of(text)
        self.assertEqual(parameter_ref(root, "AUX"), "VOLTS_Type")
        self.assertIsNone(types["VOLTS_Type"].find(tag("DefaultAlarm")))

    def test_alarm_follows_encoding_and_units(self):
        text, _ = apply_limits(SAMPLE, normalize_limits(BUS))
        types, _ = types_of(text)
        self.assertEqual([c.tag.split("}")[1] for c in types["VOLTS_Type_BUS_Limits"]],
                         ["IntegerDataEncoding", "UnitSet", "DefaultAlarm"])

    def test_existing_default_alarm_is_replaced_not_duplicated(self):
        text, applied = apply_limits(SAMPLE, normalize_limits({"/DEV/COUNT": {"red": {"max": 5}}}))
        types, _ = types_of(text)
        alarms = types["COUNT_Type_COUNT_Limits"].findall(tag("DefaultAlarm"))
        self.assertEqual(len(alarms), 1)
        self.assertEqual(alarms[0].find(f"{tag('StaticAlarmRanges')}/{tag('CriticalRange')}").get("maxInclusive"), "5.0")
        self.assertIsNone(alarms[0].find(f"{tag('StaticAlarmRanges')}/{tag('WatchRange')}"))
        # the original keeps its own alarm
        self.assertIsNotNone(types["COUNT_Type"].find(tag("DefaultAlarm")))

    def test_nested_space_system_parameters_are_found_by_path(self):
        text, applied = apply_limits(SAMPLE, normalize_limits({"/DEV/PACKET/TEMP": {"yellow": {"max": 40}}}))
        self.assertEqual(applied, {"/DEV/PACKET/TEMP"})
        self.assertIn("TEMP_Type_TEMP_Limits", text)

    def test_only_numeric_parameters_can_be_limited(self):
        with self.assertRaisesRegex(LimitError, "only integer and float"):
            apply_limits(SAMPLE, normalize_limits({"/DEV/STATE": {"red": {"min": 1}}}))

    def test_a_document_that_owns_none_of_the_limits_is_returned_unchanged(self):
        text, applied = apply_limits(SAMPLE, normalize_limits({"/OTHER/BUS": {"red": {"min": 1}}}))
        self.assertEqual((text, applied), (SAMPLE, set()))
        self.assertEqual(apply_limits(SAMPLE, {}), (SAMPLE, set()))

    def test_a_path_through_a_missing_system_or_a_system_without_parameters_is_not_applied(self):
        text, applied = apply_limits(SAMPLE, {"/DEV/NOPE/TEMP": {"red": {"min": 1.0}}})
        self.assertEqual((text, applied), (SAMPLE, set()))
        bare = SAMPLE.replace('<xtce:SpaceSystem name="PACKET">',
                              '<xtce:SpaceSystem name="EMPTY"><xtce:TelemetryMetaData /></xtce:SpaceSystem>'
                              '<xtce:SpaceSystem name="PACKET">')
        text, applied = apply_limits(bare, {"/DEV/EMPTY/TEMP": {"red": {"min": 1.0}}})
        self.assertEqual(applied, set())

    def test_a_parameter_whose_type_is_not_in_the_same_file_is_an_error(self):
        orphan = SAMPLE.replace('<xtce:Parameter name="AUX" parameterTypeRef="VOLTS_Type" />',
                                '<xtce:Parameter name="AUX" parameterTypeRef="OTHER_FILE_Type" />')
        with self.assertRaisesRegex(LimitError, "OTHER_FILE_Type.*not defined in the same XTCE file"):
            apply_limits(orphan, {"/DEV/AUX": {"red": {"min": 1.0}}})

    def test_the_alarm_is_placed_before_any_context_alarms(self):
        contextual = SAMPLE.replace(
            '<xtce:UnitSet><xtce:Unit>V</xtce:Unit></xtce:UnitSet>',
            '<xtce:UnitSet><xtce:Unit>V</xtce:Unit></xtce:UnitSet>'
            '<xtce:ContextAlarmList />')
        text, applied = apply_limits(contextual, {"/DEV/BUS": {"red": {"min": 1.0}}})
        self.assertEqual(applied, {"/DEV/BUS"})
        types, _ = types_of(text)
        children = [child.tag.split("}")[-1] for child in types["VOLTS_Type_BUS_Limits"]]
        self.assertEqual(children.index("DefaultAlarm") + 1, children.index("ContextAlarmList"))

    def test_unknown_parameter_in_a_known_system_is_not_applied(self):
        _, applied = apply_limits(SAMPLE, normalize_limits({"/DEV/NOPE": {"red": {"min": 1}}}))
        self.assertEqual(applied, set())

    def test_output_is_valid_xtce_with_the_xtce_prefix(self):
        text, _ = apply_limits(SAMPLE, normalize_limits(BUS))
        self.assertTrue(text.startswith('<?xml version="1.0" encoding="UTF-8"?>'))
        self.assertIn("<xtce:DefaultAlarm>", text)
        self.assertEqual(root_space_system(text), "DEV")

    def test_real_eps_battery_voltage_limits(self):
        source = (ROOT / "comp/eps/gsw/eps.xtce").read_text(encoding="utf-8")
        text, applied = apply_limits(source, normalize_limits(
            {"/EPS/BATTERY_VOLTAGE": {"yellow": {"min": 20}, "red": {"min": 18}}}))
        self.assertEqual(applied, {"/EPS/BATTERY_VOLTAGE"})
        types, root = types_of(text)
        self.assertEqual(parameter_ref(root, "BATTERY_VOLTAGE"), "BATTERY_VOLTAGE_Type_BATTERY_VOLTAGE_Limits")
        self.assertEqual(parameter_ref(root, "BATTERY_TEMPERATURE"), "BATTERY_TEMPERATURE_Type")
        # the calibration (volts from counts) must survive the copy
        self.assertIsNotNone(types["BATTERY_VOLTAGE_Type_BATTERY_VOLTAGE_Limits"].find(f".//{tag('PolynomialCalibrator')}"))


class WriteTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.source = self.dir / "dev.xtce"
        self.source.write_text(SAMPLE, encoding="utf-8")
        self.other = self.dir / "other.xtce"
        self.other.write_text(SAMPLE.replace('name="DEV"', 'name="OTHER"', 1), encoding="utf-8")
        self.out = self.dir / "scenario" / "mdb"

    def test_writes_only_files_that_carry_a_limited_parameter(self):
        applied = write_limited_mdb(normalize_limits(BUS), [(self.source, "components/dev/dev.xtce"),
                                                             (self.other, "components/other/other.xtce")], self.out)
        self.assertEqual(applied, {"/DEV/BUS"})
        self.assertTrue((self.out / "components/dev/dev.xtce").exists())
        self.assertFalse((self.out / "components/other/other.xtce").exists())

    def test_stale_output_is_removed_when_limits_are_dropped(self):
        write_limited_mdb(normalize_limits(BUS), [(self.source, "components/dev/dev.xtce")], self.out)
        self.assertTrue((self.out / "components/dev/dev.xtce").exists())
        write_limited_mdb({}, [(self.source, "components/dev/dev.xtce")], self.out)
        self.assertFalse(self.out.exists())

    def test_a_limit_that_matches_nothing_is_an_error(self):
        with self.assertRaisesRegex(LimitError, r"no component or mission XTCE defines parameter\(s\): /DEV/NOPE"):
            write_limited_mdb(normalize_limits({"/DEV/NOPE": {"red": {"min": 1}}}),
                              [(self.source, "components/dev/dev.xtce")], self.out)

    def test_a_missing_source_file_is_skipped(self):
        with self.assertRaises(LimitError):
            write_limited_mdb(normalize_limits(BUS), [(self.dir / "absent.xtce", "x.xtce")], self.out)


if __name__ == "__main__":
    unittest.main()


class SourceTests(unittest.TestCase):
    def test_sources_are_every_component_and_mission_definition_with_the_generated_eps_substituted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in ("comp/eps/gsw/eps.xtce", "comp/demo/gsw/demo.xtce", "comp/demo/gsw/other.xtce",
                             "cfg/drm/gsw/ground.xtce", "comp/radio/gsw/readme.txt"):
                (root / relative).parent.mkdir(parents=True, exist_ok=True)
                (root / relative).write_text("<x />")
            generated = root / "generated-eps.xtce"
            generated.write_text("<x />")
            sources = mdb_sources(root, "drm", generated)
            self.assertEqual([(path.relative_to(root).as_posix(), name) for path, name in sources], [
                ("comp/demo/gsw/demo.xtce", "components/demo/demo.xtce"),
                ("comp/demo/gsw/other.xtce", "components/demo/other.xtce"),
                ("generated-eps.xtce", "components/eps/eps.xtce"),
                ("cfg/drm/gsw/ground.xtce", "ground.xtce"),
            ])
            # With no generated EPS definition the checked in one is used.
            self.assertIn(("comp/eps/gsw/eps.xtce", "components/eps/eps.xtce"),
                          [(path.relative_to(root).as_posix(), name) for path, name in mdb_sources(root, "drm")])

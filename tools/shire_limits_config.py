"""Configurable yellow and red telemetry limits, applied to the ground MDB as XTCE alarm ranges.

Limits are written per parameter, in engineering units, under a `limits:` key that cascades like component
configuration (mission, spacecraft, then scenario):

    limits:
      /EPS/BATTERY_VOLTAGE:
        yellow: {min: 20.0}        # outside this range the parameter is WARNING (yellow)
        red: {min: 18.0}           # outside this range the parameter is CRITICAL (red)
      /EPS/BATTERY_TEMPERATURE: null   # removes a limit set by an earlier layer

YAMCS evaluates the alarms, so limits show in the web UI, the alarm list, and each parameter's monitoring result.
A parameter's type is shared with other parameters, so a limited parameter gets its own copy of the type and the
other parameters keep theirs. Only component and mission XTCE files are limited. The cFS application definitions
belong to the YAMCS submodule and are left as they are.
"""

from __future__ import annotations

import copy
import re
from pathlib import Path
from typing import Any
import xml.etree.ElementTree as ET

LEVELS = ("yellow", "red")
BOUNDS = ("min", "max")
# XTCE names the ranges by severity. Yellow maps to WARNING and red to CRITICAL in the YAMCS web UI.
XTCE_RANGE = {"yellow": "WarningRange", "red": "CriticalRange"}
NUMERIC_TYPES = ("IntegerParameterType", "FloatParameterType")
PARAMETER_PATH = re.compile(r"/[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)+")
ROOT_NAME = re.compile(r"<(?:\w+:)?SpaceSystem\b[^>]*?\sname=\"([^\"]+)\"")


class LimitError(ValueError):
    """The limits are malformed or name a parameter that cannot be limited."""


def number(value: Any, what: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise LimitError(f"{what} must be a number, got {value!r}")
    if value != value or value in (float("inf"), float("-inf")):
        raise LimitError(f"{what} must be finite")
    return float(value)


def normalize_level(parameter: str, level: str, raw: Any) -> dict[str, float]:
    where = f"limit {parameter} {level}"
    if not isinstance(raw, dict) or not raw:
        raise LimitError(f"{where} must give min, max, or both")
    unknown = set(raw) - set(BOUNDS)
    if unknown:
        raise LimitError(f"{where} has unknown key(s): {', '.join(sorted(unknown))}")
    bounds = {key: number(raw[key], f"{where} {key}") for key in BOUNDS if key in raw}
    if "min" in bounds and "max" in bounds and bounds["min"] >= bounds["max"]:
        raise LimitError(f"{where} min must be below max")
    return bounds


def normalize_limits(raw: Any) -> dict[str, dict[str, dict[str, float]]]:
    """Validates a merged `limits:` mapping and returns it with numeric bounds. A red range must enclose the
    yellow range on each side where both are given, or the red alarm could never be the more severe one."""
    if raw in (None, {}):
        return {}
    if not isinstance(raw, dict):
        raise LimitError("limits must be a mapping of parameter name to limits")
    result: dict[str, dict[str, dict[str, float]]] = {}
    for parameter, spec in raw.items():
        if not isinstance(parameter, str) or not PARAMETER_PATH.fullmatch(parameter):
            raise LimitError(f"limit parameter {parameter!r} must be a qualified name such as /EPS/BATTERY_VOLTAGE")
        if spec is None:
            continue
        if not isinstance(spec, dict) or not spec:
            raise LimitError(f"limit {parameter} must give yellow, red, or both")
        unknown = set(spec) - set(LEVELS)
        if unknown:
            raise LimitError(f"limit {parameter} has unknown level(s): {', '.join(sorted(unknown))}")
        levels = {level: normalize_level(parameter, level, spec[level]) for level in LEVELS
                  if spec.get(level) is not None}
        if not levels:
            continue
        yellow, red = levels.get("yellow"), levels.get("red")
        if yellow and red:
            if "min" in yellow and "min" in red and red["min"] > yellow["min"]:
                raise LimitError(f"limit {parameter}: red min {red['min']:g} must not be above yellow min {yellow['min']:g}")
            if "max" in yellow and "max" in red and red["max"] < yellow["max"]:
                raise LimitError(f"limit {parameter}: red max {red['max']:g} must not be below yellow max {yellow['max']:g}")
        result[parameter] = levels
    return result


def merge_limits(*layers: Any) -> dict[str, dict[str, Any]]:
    """Combines the cascade's layers, later layers winning. A layer replaces a parameter's yellow or red range as
    a whole, and `null` removes a parameter's limits or one of its two levels."""
    merged: dict[str, dict[str, Any]] = {}
    for layer in layers:
        if not layer:
            continue
        if not isinstance(layer, dict):
            raise LimitError("limits must be a mapping of parameter name to limits")
        for parameter, spec in layer.items():
            if spec is None:
                merged.pop(parameter, None)
                continue
            if not isinstance(spec, dict):
                raise LimitError(f"limit {parameter} must give yellow, red, or both")
            current = merged.setdefault(parameter, {})
            for level, bounds in spec.items():
                if bounds is None:
                    current.pop(level, None)
                else:
                    current[level] = bounds
            if not current:
                del merged[parameter]
    return merged


def resolve_limits(*layers: Any) -> dict[str, dict[str, dict[str, float]]]:
    return normalize_limits(merge_limits(*layers))


# ----------------------------------------------------------------------------------------------------------
# XTCE


def root_space_system(text: str) -> str | None:
    match = ROOT_NAME.search(text)
    return match.group(1) if match else None


def find_child(element: ET.Element, ns: str, name: str) -> ET.Element | None:
    return element.find(f"{{{ns}}}{name}")


def locate_parameter(root: ET.Element, ns: str, path: str) -> tuple[ET.Element, ET.Element] | None:
    """The (space system, Parameter) for a qualified name, or None. The first segment names the root system,
    any middle segments are nested systems, and the last is the parameter."""
    segments = path.strip("/").split("/")
    system = root
    if system.get("name") != segments[0]:
        return None
    for name in segments[1:-1]:
        system = next((child for child in system.findall(f"{{{ns}}}SpaceSystem") if child.get("name") == name), None)
        if system is None:
            return None
    parameters = system.find(f"{{{ns}}}TelemetryMetaData/{{{ns}}}ParameterSet")
    if parameters is None:
        return None
    parameter = next((p for p in parameters.findall(f"{{{ns}}}Parameter") if p.get("name") == segments[-1]), None)
    return (system, parameter) if parameter is not None else None


def alarm_element(ns: str, levels: dict[str, dict[str, float]]) -> ET.Element:
    alarm = ET.Element(f"{{{ns}}}DefaultAlarm")
    ranges = ET.SubElement(alarm, f"{{{ns}}}StaticAlarmRanges")
    for level in LEVELS:
        if level in levels:
            attributes = {("minInclusive" if key == "min" else "maxInclusive"): repr(value)
                          for key, value in levels[level].items()}
            ET.SubElement(ranges, f"{{{ns}}}{XTCE_RANGE[level]}", attributes)
    return alarm


def limit_parameter(root: ET.Element, ns: str, path: str, levels: dict[str, dict[str, float]]) -> bool:
    """Gives one parameter its own copy of its type, carrying the alarm ranges. False when the parameter is
    not in this file."""
    found = locate_parameter(root, ns, path)
    if found is None:
        return False
    system, parameter = found
    type_name = parameter.get("parameterTypeRef")
    types = system.find(f"{{{ns}}}TelemetryMetaData/{{{ns}}}ParameterTypeSet")
    original = None if types is None else next((t for t in types if t.get("name") == type_name), None)
    if original is None:
        raise LimitError(f"{path}: parameter type {type_name!r} is not defined in the same XTCE file")
    if original.tag.split("}")[-1] not in NUMERIC_TYPES:
        raise LimitError(f"{path}: only integer and float parameters can have limits, not {original.tag.split('}')[-1]}")
    limited = copy.deepcopy(original)
    limited.set("name", f"{type_name}_{parameter.get('name')}_Limits")
    for existing in limited.findall(f"{{{ns}}}DefaultAlarm"):
        limited.remove(existing)
    # DefaultAlarm follows the encoding and range elements, and precedes any context alarms.
    context = find_child(limited, ns, "ContextAlarmList")
    alarm = alarm_element(ns, levels)
    if context is None:
        limited.append(alarm)
    else:
        limited.insert(list(limited).index(context), alarm)
    types.insert(list(types).index(original) + 1, limited)
    parameter.set("parameterTypeRef", limited.get("name"))
    return True


def apply_limits(text: str, limits: dict[str, dict[str, dict[str, float]]]) -> tuple[str, set[str]]:
    """Applies every limit that belongs to this XTCE document. Returns the new document and the parameter names
    that were limited, which is empty (and the text unchanged) when none belong here."""
    if not limits:
        return text, set()
    root = ET.fromstring(text)
    ns = root.tag.split("}")[0][1:] if root.tag.startswith("{") else ""
    applied = {path for path, levels in limits.items() if limit_parameter(root, ns, path, levels)}
    if not applied:
        return text, set()
    ET.register_namespace("xtce", ns)
    ET.register_namespace("xsi", "http://www.w3.org/2001/XMLSchema-instance")
    ET.indent(root)
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding="unicode") + "\n", applied


def mdb_sources(root: Path, mission: str, eps_xtce: Path | None = None) -> list[tuple[Path, str]]:
    """The XTCE files that limits can apply to, with the path each takes under the ground MDB: every component's
    definition (the resolved EPS definition replacing the checked in one, when it was generated) and the mission's
    own. These are the files the ground image build copies, so a limit on a component the spacecraft omits is
    harmless."""
    sources: list[tuple[Path, str]] = []
    for gsw in sorted((root / "comp").glob("*/gsw")):
        component = gsw.parent.name
        for xtce in sorted(gsw.glob("*.xtce")):
            if component == "eps" and eps_xtce is not None and eps_xtce.exists():
                sources.append((eps_xtce, f"components/eps/{xtce.name}"))
            else:
                sources.append((xtce, f"components/{component}/{xtce.name}"))
    for xtce in sorted((root / "cfg" / mission / "gsw").glob("*.xtce")):
        sources.append((xtce, xtce.name))
    return sources


def write_limited_mdb(limits: dict[str, dict[str, dict[str, float]]], sources: list[tuple[Path, str]],
                      destination: Path) -> set[str]:
    """Writes a limited copy of each XTCE source that carries a limited parameter, at its relative output path
    under `destination`, for the ground image build to overlay on the checked in MDB. Stale output is removed
    first so dropping a limit restores the checked in definition. A limit that matches no parameter in any
    source is an error, since a misspelled name would otherwise silently do nothing."""
    import shutil
    shutil.rmtree(destination, ignore_errors=True)
    applied: set[str] = set()
    roots = {path.split("/")[1] for path in limits}
    for source, relative in sources:
        if not source.exists():
            continue
        text = source.read_text(encoding="utf-8")
        if root_space_system(text) not in roots:
            continue
        new_text, done = apply_limits(text, limits)
        if done:
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(new_text, encoding="utf-8")
            applied |= done
    missing = sorted(set(limits) - applied)
    if missing:
        raise LimitError("no component or mission XTCE defines parameter(s): " + ", ".join(missing))
    return applied

"""Constellation configuration normalization shared by the orchestrator, runners, and cfg-check.

A constellation file describes a multi-vehicle run: which spacecraft types fly as which vehicles,
and the crosslinks between them, including scripted disruptions. It deliberately holds nothing
that a single-vehicle run needs, so existing missions, spacecraft, and scenarios are unaffected.
"""
from copy import deepcopy
import hashlib
import math
import re

ROLES = ("leaf", "relay")
LINK_TYPES = ("crosslink",)
DELAY_MODES = ("range", "fixed")
DISRUPTION_TYPES = ("outage", "drop", "delay")
# Simulith's server and barrier hold 32 clients, and each vehicle needs two (Director and FSW).
MAX_VEHICLES = 16
MAX_NODE_ID = 2**32 - 1
VEHICLE_ID = re.compile(r"^[a-z0-9][a-z0-9-]{0,15}$")

TOP_KEYS = {"constellation_name", "description", "initial_conditions", "master_seed", "vehicles", "links"}
VEHICLE_KEYS = {"id", "spacecraft", "role", "node_id", "overrides"}
LINK_KEYS = {"name", "endpoints", "type", "max_range_km", "delay", "drop_ppm", "bandwidth_kbps", "seed",
             "disruptions"}
DISRUPTION_KEYS = {"type", "at_s", "until_s", "drop_ppm", "delay_ms"}


def _unknown(keys, allowed, where):
    extra = sorted(set(keys) - allowed)
    if extra:
        raise ValueError(f"{where} has unknown key(s) {', '.join(map(repr, extra))}; "
                         f"allowed: {', '.join(sorted(allowed))}")


def integer(value, name, low, high):
    if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
        raise ValueError(f"constellation {name} must be an integer in [{low}, {high}]")
    return value


def number(value, name, low, high):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"constellation {name} must be numeric")
    result = float(value)
    if not math.isfinite(result) or not low <= result <= high:
        raise ValueError(f"constellation {name} must be finite and in [{low}, {high}]")
    return result


def derive_seed(master_seed: int, link: str, source: str, destination: str) -> int:
    """A stable nonzero 32-bit seed per link direction, so every impairment stream is independent
    of the others yet repeats exactly for the same master seed. Zero is excluded because the link
    model's xorshift generator never leaves zero."""
    digest = hashlib.sha256(f"{master_seed}:{link}:{source}->{destination}".encode()).digest()
    return int.from_bytes(digest[:4], "big") or 1


def normalize_vehicle(entry, index, spacecraft_names, component_names):
    if not isinstance(entry, dict):
        raise ValueError(f"constellation vehicle #{index} must be a mapping")
    _unknown(entry, VEHICLE_KEYS, f"constellation vehicle #{index}")
    vid = entry.get("id")
    if not isinstance(vid, str) or not VEHICLE_ID.match(vid):
        raise ValueError(f"constellation vehicle #{index} id must be lowercase letters, digits, and hyphens "
                         f"(at most 16 characters, used in container names)")
    spacecraft = entry.get("spacecraft")
    if spacecraft not in spacecraft_names:
        raise ValueError(f"constellation vehicle {vid!r} spacecraft {spacecraft!r} is not a registered spacecraft")
    role = entry.get("role", "leaf")
    if role not in ROLES:
        raise ValueError(f"constellation vehicle {vid!r} role must be one of {', '.join(ROLES)}")
    overrides = entry.get("overrides") or {}
    if not isinstance(overrides, dict):
        raise ValueError(f"constellation vehicle {vid!r} overrides must be a mapping of component to settings")
    for component, settings in overrides.items():
        if component not in component_names:
            raise ValueError(f"constellation vehicle {vid!r} overrides unknown component {component!r}")
        if not isinstance(settings, dict):
            raise ValueError(f"constellation vehicle {vid!r} overrides.{component} must be a mapping")
    return {
        "id": vid,
        "spacecraft": spacecraft,
        "role": role,
        "node_id": integer(entry.get("node_id"), f"vehicle {vid!r} node_id", 1, MAX_NODE_ID),
        # The index doubles as the 42 spacecraft index, so it is assigned and never user-set.
        "spacecraft_id": index,
        "overrides": deepcopy(overrides),
    }


def normalize_disruption(entry, link_name, index):
    where = f"link {link_name!r} disruption #{index}"
    if not isinstance(entry, dict):
        raise ValueError(f"constellation {where} must be a mapping")
    _unknown(entry, DISRUPTION_KEYS, f"constellation {where}")
    kind = entry.get("type")
    if kind not in DISRUPTION_TYPES:
        raise ValueError(f"constellation {where} type must be one of {', '.join(DISRUPTION_TYPES)}")
    start = number(entry.get("at_s"), f"{where} at_s", 0.0, 1e9)
    result = {"type": kind, "at_s": start, "until_s": None}
    if entry.get("until_s") is not None:
        result["until_s"] = number(entry["until_s"], f"{where} until_s", 0.0, 1e9)
        if result["until_s"] <= start:
            raise ValueError(f"constellation {where} until_s must be after at_s")
    needs = {"drop": "drop_ppm", "delay": "delay_ms"}.get(kind)
    for key in ("drop_ppm", "delay_ms"):
        if key == needs:
            high = 1_000_000 if key == "drop_ppm" else 2**32 - 1
            result[key] = integer(entry.get(key), f"{where} {key}", 0, high)
        elif key in entry:
            raise ValueError(f"constellation {where} of type {kind!r} does not take {key}")
    return result


def _windows_overlap(a, b):
    a_end = math.inf if a["until_s"] is None else a["until_s"]
    b_end = math.inf if b["until_s"] is None else b["until_s"]
    return a["at_s"] < b_end and b["at_s"] < a_end


def normalize_link(entry, index, vehicle_ids, master_seed):
    if not isinstance(entry, dict):
        raise ValueError(f"constellation link #{index} must be a mapping")
    _unknown(entry, LINK_KEYS, f"constellation link #{index}")
    endpoints = entry.get("endpoints")
    if (not isinstance(endpoints, list) or len(endpoints) != 2 or not all(isinstance(e, str) for e in endpoints)):
        raise ValueError(f"constellation link #{index} endpoints must be a list of two vehicle ids")
    first, second = endpoints
    for vehicle in endpoints:
        if vehicle not in vehicle_ids:
            raise ValueError(f"constellation link #{index} endpoint {vehicle!r} is not a vehicle")
    if first == second:
        raise ValueError(f"constellation link #{index} must connect two different vehicles")
    name = entry.get("name", f"{first}-{second}")
    if not isinstance(name, str) or not name:
        raise ValueError(f"constellation link #{index} name must be a nonempty string")
    kind = entry.get("type", "crosslink")
    if kind not in LINK_TYPES:
        raise ValueError(f"constellation link {name!r} type must be one of {', '.join(LINK_TYPES)}")

    delay = entry.get("delay", "range")
    if delay == "range":
        delay = {"mode": "range"}
    elif isinstance(delay, dict):
        _unknown(delay, {"mode", "ms"}, f"constellation link {name!r} delay")
        mode = delay.get("mode", "fixed")
        if mode not in DELAY_MODES:
            raise ValueError(f"constellation link {name!r} delay mode must be one of {', '.join(DELAY_MODES)}")
        delay = {"mode": mode}
        if mode == "fixed":
            delay["ms"] = integer(entry["delay"].get("ms", 0), f"link {name!r} delay ms", 0, 2**32 - 1)
        elif "ms" in entry["delay"]:
            raise ValueError(f"constellation link {name!r} range delay does not take ms")
    else:
        raise ValueError(f"constellation link {name!r} delay must be 'range' or a mapping")

    link_seed = integer(entry.get("seed", master_seed), f"link {name!r} seed", 0, 2**32 - 1)
    disruptions = [normalize_disruption(d, name, i) for i, d in enumerate(entry.get("disruptions") or [])]
    disruptions.sort(key=lambda d: (d["at_s"], d["type"]))
    for i, one in enumerate(disruptions):
        for other in disruptions[i + 1:]:
            if one["type"] == other["type"] and _windows_overlap(one, other):
                raise ValueError(f"constellation link {name!r} has overlapping {one['type']} disruptions")
    return {
        "name": name,
        "endpoints": [first, second],
        "type": kind,
        "max_range_km": number(entry.get("max_range_km", 5000.0), f"link {name!r} max_range_km", 0.001, 1e6),
        "delay": delay,
        "drop_ppm": integer(entry.get("drop_ppm", 0), f"link {name!r} drop_ppm", 0, 1_000_000),
        "bandwidth_kbps": number(entry.get("bandwidth_kbps", 0), f"link {name!r} bandwidth_kbps", 0, 1e9),
        "seeds": {f"{a}_to_{b}": derive_seed(link_seed, name, a, b) for a, b in ((first, second), (second, first))},
        "disruptions": disruptions,
    }


def unreachable_vehicles(vehicle_ids, links):
    """Vehicles not connected to the first vehicle by any chain of links."""
    adjacent = {v: set() for v in vehicle_ids}
    for link in links:
        a, b = link["endpoints"]
        adjacent[a].add(b)
        adjacent[b].add(a)
    seen, pending = set(), [vehicle_ids[0]]
    while pending:
        current = pending.pop()
        if current not in seen:
            seen.add(current)
            pending.extend(adjacent[current] - seen)
    return [v for v in vehicle_ids if v not in seen]


def normalize_constellation(config, *, spacecraft_names, component_names):
    """Validates a constellation and returns it with defaults applied, vehicle indices assigned, and
    per-link seeds derived. Raises ValueError for anything a run could not honor."""
    if not isinstance(config, dict):
        raise ValueError("constellation config must be a mapping")
    _unknown(config, TOP_KEYS, "constellation")
    name = config.get("constellation_name")
    if not isinstance(name, str) or not name:
        raise ValueError("constellation constellation_name must be a nonempty string")
    master_seed = integer(config.get("master_seed", 1), "master_seed", 0, 2**32 - 1)

    raw = config.get("vehicles")
    if not isinstance(raw, list) or not raw:
        raise ValueError("constellation vehicles must be a nonempty list")
    if len(raw) > MAX_VEHICLES:
        raise ValueError(f"constellation supports at most {MAX_VEHICLES} vehicles")
    vehicles = [normalize_vehicle(entry, i, set(spacecraft_names), set(component_names)) for i, entry in enumerate(raw)]
    for key in ("id", "node_id"):
        seen = set()
        for vehicle in vehicles:
            if vehicle[key] in seen:
                raise ValueError(f"constellation vehicle {key} {vehicle[key]!r} is used more than once")
            seen.add(vehicle[key])
    ids = [v["id"] for v in vehicles]

    links = [normalize_link(entry, i, set(ids), master_seed) for i, entry in enumerate(config.get("links") or [])]
    names, pairs = set(), set()
    for link in links:
        pair = frozenset(link["endpoints"])
        if link["name"] in names:
            raise ValueError(f"constellation link name {link['name']!r} is used more than once")
        if pair in pairs:
            raise ValueError(f"constellation has more than one link between {' and '.join(sorted(pair))}")
        names.add(link["name"])
        pairs.add(pair)
    if len(ids) > 1:
        stranded = unreachable_vehicles(ids, links)
        if stranded:
            raise ValueError(f"constellation vehicle(s) {', '.join(map(repr, stranded))} have no chain of links "
                             f"to {ids[0]!r}")

    normalized = {
        "constellation_name": name,
        "description": config.get("description", ""),
        "master_seed": master_seed,
        "vehicles": vehicles,
        "links": links,
    }
    if config.get("initial_conditions") is not None:
        normalized["initial_conditions"] = config["initial_conditions"]
    return normalized

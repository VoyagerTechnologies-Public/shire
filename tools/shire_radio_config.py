"""Radio link configuration normalization shared by image generation and hashing."""
from copy import deepcopy
import math

LINK_MODES = ("auto", "always", "never")
MAX_GROUND_STATIONS = 16
MAX_QUEUE_DEPTH = 4096
DEFAULT_MIN_ELEVATION_DEG = 5.0


def integer(value, name, low, high):
    if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
        raise ValueError(f"radio {name} must be an integer in [{low}, {high}]")
    return value


def number(value, name, low, high):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"radio {name} must be numeric")
    result = float(value)
    if not math.isfinite(result) or not low <= result <= high:
        raise ValueError(f"radio {name} must be finite and in [{low}, {high}]")
    return result


def normalize_station(entry):
    if not isinstance(entry, dict) or not isinstance(entry.get("name"), str) or not entry["name"].strip():
        raise ValueError("radio ground station must be a mapping with a nonempty name")
    name = entry["name"]
    enabled = entry.get("enabled", True)
    if not isinstance(enabled, bool):
        raise ValueError(f"radio ground station {name!r} enabled must be boolean")
    return {
        "name": name,
        "lat_deg": number(entry.get("lat_deg"), f"ground station {name!r} lat_deg", -90.0, 90.0),
        "lon_deg": number(entry.get("lon_deg"), f"ground station {name!r} lon_deg", -180.0, 180.0),
        "min_elev_deg": number(entry.get("min_elev_deg", DEFAULT_MIN_ELEVATION_DEG),
                               f"ground station {name!r} min_elev_deg", 0.0, 90.0),
        "enabled": enabled,
    }


def select_stations(selection, catalog):
    """Resolve the radio's station list from the shared catalog.

    selection None means every Earth station in the catalog; otherwise it is an
    ordered list of catalog names (station index == position in that list, which
    is what the SET_STATION_ENABLED backdoor addresses)."""
    by_name = {}
    for entry in catalog or []:
        if isinstance(entry, dict) and isinstance(entry.get("name"), str):
            by_name[entry["name"]] = entry
    if selection is None:
        names = [n for n, e in by_name.items() if str(e.get("world", "EARTH")).upper() == "EARTH"]
    else:
        if not isinstance(selection, list) or not all(isinstance(n, str) for n in selection):
            raise ValueError("radio ground_stations must be a list of ground station names")
        names = selection
        if len(set(names)) != len(names):
            raise ValueError("radio ground_stations must not repeat a station")
        for name in names:
            if name not in by_name:
                raise ValueError(f"radio ground station {name!r} is not in the ground station catalog")
            if str(by_name[name].get("world", "EARTH")).upper() != "EARTH":
                raise ValueError(f"radio ground station {name!r} must be on EARTH")
    if len(names) > MAX_GROUND_STATIONS:
        raise ValueError(f"radio supports at most {MAX_GROUND_STATIONS} ground stations")
    return [normalize_station(by_name[n]) for n in names]


def normalize_radio(config, catalog):
    cfg = deepcopy(config)
    mode = cfg.get("link_mode", "always")
    if not isinstance(mode, str) or mode.lower() not in LINK_MODES:
        raise ValueError(f"radio link_mode must be one of {', '.join(LINK_MODES)}")
    cfg["link_mode"] = mode.lower()
    for key in ("uplink_delay_ms", "downlink_delay_ms"):
        cfg[key] = integer(cfg.get(key, 0), key, 0, 2**32 - 1)
    for key in ("uplink_drop_ppm", "downlink_drop_ppm"):
        cfg[key] = integer(cfg.get(key, 0), key, 0, 1_000_000)
    cfg["link_seed"] = integer(cfg.get("link_seed", 1), "link_seed", 0, 2**32 - 1)
    cfg["link_queue_depth"] = integer(cfg.get("link_queue_depth", 64), "link_queue_depth", 1, MAX_QUEUE_DEPTH)
    cfg["ground_stations"] = select_stations(cfg.get("ground_stations"), catalog)
    return cfg

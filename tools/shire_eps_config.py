"""Canonical EPS wiring normalization shared by image generation and hashing."""
from copy import deepcopy
import math

COMPONENTS = ("demo", "adcs", "radio")


def number(value, name, low=0.0, high=float("inf")):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"EPS {name} must be numeric")
    result = float(value)
    if not math.isfinite(result) or not low <= result <= high:
        raise ValueError(f"EPS {name} must be finite and in [{low}, {high}]")
    return result


def normalize_eps(config, available):
    cfg = deepcopy(config)
    for key, low, high in (("battery_capacity_wh", 1e-9, float("inf")),
                          ("battery_initial_soc", 0, 1),
                          ("battery_voltage_min", 0, 32),
                          ("battery_voltage_max", 0, 32),
                          ("max_solar_power_w", 0, float("inf"))):
        cfg[key] = number(cfg[key], key, low, high)
    if cfg["battery_voltage_max"] < cfg["battery_voltage_min"]:
        raise ValueError("EPS battery voltage maximum is below minimum")
    cfg["base_load_w"] = number(cfg.get("base_load_w", 0), "base_load_w")
    passive = cfg.get("switch_load_power_w", [0.] * 8)
    if not isinstance(passive, list) or len(passive) != 8:
        raise ValueError("EPS switch_load_power_w must contain eight watt values")
    cfg["switch_load_power_w"] = [number(w, "switch load power") for w in passive]
    switches = cfg.get("switches", [
        {"label": "Load bank" if passive[i] else f"Switch {i}", "voltage_v": v, "startup_on": False}
        for i, v in enumerate([3.3, 3.3, 5, 5, 12, 12, 24, 24])])
    if not isinstance(switches, list) or len(switches) != 8:
        raise ValueError("EPS switches must contain exactly eight definitions")
    for i, switch in enumerate(switches):
        if not isinstance(switch, dict):
            raise ValueError(f"EPS switch {i} must be a mapping")
        switch["load_power_w"] = number(switch.get("load_power_w", passive[i]), f"switch {i} load power")
        switch["voltage_v"] = number(switch["voltage_v"], f"switch {i} voltage", 1e-9, 32)
        if not isinstance(switch.get("startup_on", False), bool):
            raise ValueError(f"EPS switch {i} startup_on must be boolean")
        switch.setdefault("startup_on", False)
        switch.setdefault("label", f"Switch {i}")
        if not isinstance(switch["label"], str) or not switch["label"].strip():
            raise ValueError(f"EPS switch {i} label must be nonempty text")
    loads = cfg.get("loads", {})
    if not isinstance(loads, dict):
        raise ValueError("EPS loads must be a component-name mapping")
    defaults = cfg.get("default_loads", {})
    if not isinstance(defaults, dict):
        raise ValueError("EPS default_loads must be a component-name mapping")
    for name, load in defaults.items():
        if name not in COMPONENTS or not isinstance(load, dict):
            raise ValueError(f"EPS default load {name!r} must describe a supported component")
    selected = {name: deepcopy(load) for name, load in defaults.items()
                if cfg.get("wire_default_loads") and name in available}
    for name, load in loads.items():
        if not isinstance(load, dict):
            raise ValueError(f"EPS load {name!r} must be a mapping")
        selected[name] = {**defaults.get(name, {}), **load}
    loads = selected
    if "switches" not in cfg:
        for load in loads.values():
            if not isinstance(load, dict):
                raise ValueError("EPS each load must be a mapping")
            idx = load.get("switch")
            if isinstance(idx, int) and not isinstance(idx, bool) and 0 <= idx < 8:
                switches[idx]["startup_on"] = True
    rendered = []
    for name in loads:
        if name not in COMPONENTS or name not in available:
            raise ValueError(f"EPS load {name!r} is unavailable or has no power-control support")
    for name in COMPONENTS:
        if name not in loads:
            rendered.append({"component": "", "switch": 0, "mode_power_w": [0.] * 6,
                             "boot_power_w": 0., "boot_delay_s": 0., "power_scale": 1.})
            continue
        load = loads[name]
        if not isinstance(load, dict):
            raise ValueError(f"EPS load {name} must be a mapping")
        idx = load.get("switch")
        if isinstance(idx, bool) or not isinstance(idx, int) or not 0 <= idx < 8:
            raise ValueError(f"EPS {name} switch must be an integer from 0 to 7")
        powers = load.get("mode_power_w")
        if not isinstance(powers, list) or len(powers) != 6:
            raise ValueError(f"EPS {name} mode_power_w must contain six mode values")
        load["mode_power_w"] = [number(p, f"{name} mode power") for p in powers]
        load["boot_power_w"] = number(load.get("boot_power_w", max(powers)), f"{name} boot power")
        load["boot_delay_s"] = number(load.get("boot_delay_s", 0), f"{name} boot delay", 0, 3600)
        load["power_scale"] = number(load.get("power_scale", 1), f"{name} scale", 0, 1000)
        rendered.append({**load, "component": name + "_sim"})
    for i, switch in enumerate(switches):
        peak = switch["load_power_w"] + sum(max(max(l["mode_power_w"]), l["boot_power_w"]) * l["power_scale"]
                   for l in loads.values() if l["switch"] == i)
        if peak / switch["voltage_v"] > 10:
            raise ValueError(f"EPS switch {i} peak current exceeds 10 A")
    cfg["switches"] = switches
    cfg["loads"] = loads
    cfg["rendered_loads"] = rendered
    return cfg


def merge_component_config(name, base, override):
    """Keep layer precedence while allowing partial EPS load-field overrides."""
    merged = {}
    for key in ('loads', 'default_loads') if name == 'eps' else ():
        if not isinstance(override.get(key), dict) or not override[key]:
            continue
        inherited = deepcopy(base.get(key, {}))
        for component, settings in override[key].items():
            if isinstance(settings, dict) and isinstance(inherited.get(component), dict):
                inherited[component].update(settings)
            else:
                inherited[component] = deepcopy(settings)
        merged[key] = inherited
    base.update(override)
    base.update(merged)

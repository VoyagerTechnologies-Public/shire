"""Canonical EPS wiring normalization shared by image generation and hashing."""
from copy import deepcopy
import math

COMPONENTS = ("demo", "adcs", "radio")
MODES = {"demo": [0.8] * 6, "adcs": [0.5] + [2.5] * 5,
         "radio": [0.2, 4.0, 1.5, 4.5, 0.2, 0.2]}


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
    switches = cfg.get("switches", [
        {"label": f"Switch {i}", "voltage_v": v, "startup_on": False}
        for i, v in enumerate([3.3, 3.3, 5, 5, 12, 12, 24, 24])])
    if not isinstance(switches, list) or len(switches) != 8:
        raise ValueError("EPS switches must contain exactly eight definitions")
    for i, switch in enumerate(switches):
        if not isinstance(switch, dict):
            raise ValueError(f"EPS switch {i} must be a mapping")
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
    if cfg.get("wire_default_loads"):
        defaults = {name: {"switch": idx} for name, idx in zip(COMPONENTS, (0,4,6)) if name in available}
        for name,load in loads.items():
            if not isinstance(load,dict): raise ValueError(f"EPS load {name} must be a mapping")
            defaults[name] = {**defaults.get(name,{}), **load}
        loads=defaults
    if not isinstance(loads, dict):
        raise ValueError("EPS loads must be a component-name mapping")
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
        powers = load.get("mode_power_w", MODES[name])
        if not isinstance(powers, list) or len(powers) != 6:
            raise ValueError(f"EPS {name} mode_power_w must contain six mode values")
        load["mode_power_w"] = [number(p, f"{name} mode power") for p in powers]
        load["boot_power_w"] = number(load.get("boot_power_w", max(powers)), f"{name} boot power")
        load["boot_delay_s"] = number(load.get("boot_delay_s", 0), f"{name} boot delay", 0, 3600)
        load["power_scale"] = number(load.get("power_scale", 1), f"{name} scale", 0, 1000)
        rendered.append({**load, "component": name + "_sim"})
    for i, switch in enumerate(switches):
        peak = sum(max(max(l["mode_power_w"]), l["boot_power_w"]) * l["power_scale"]
                   for l in loads.values() if l["switch"] == i)
        if peak / switch["voltage_v"] > 10:
            raise ValueError(f"EPS switch {i} peak current exceeds 10 A")
    cfg["switches"] = switches
    cfg["loads"] = loads
    cfg["rendered_loads"] = rendered
    return cfg


def merge_component_config(name, base, override):
    """Keep layer precedence while allowing partial EPS load-field overrides."""
    if name != 'eps' or not isinstance(override.get('loads'),dict) or not override['loads']:
        base.update(override)
        return
    inherited=deepcopy(base.get('loads',{}))
    for component,settings in override['loads'].items():
        if isinstance(settings,dict) and isinstance(inherited.get(component),dict):
            inherited[component].update(settings)
        else: inherited[component]=deepcopy(settings)
    base.update(override)
    base['loads']=inherited

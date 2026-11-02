#!/usr/bin/env python3
"""Shared configuration resolution for SHIRE's orchestrator and runners.

The per-component cascade (fallback -> global -> mission -> spacecraft -> IC
component_overrides -> scenario -> scenario overrides -> CLI debug) used to
exist twice: inline in shire-orchestrator.py, which renders the build, and in
shire_runner_lib.py, which reproduced it for campaign build keys. Both now call
cascade_component() here, so a change to the layering happens in one place.
"""

from __future__ import annotations

import os
from copy import deepcopy
from dataclasses import dataclass
from pathlib import Path

import yaml

from shire_provenance import ROOT
from shire_eps_config import normalize_eps, merge_component_config
from shire_radio_config import normalize_radio
from shire_limits_config import resolve_limits
from shire_constellation_config import normalize_constellation

CFG_DIR = ROOT / "cfg"
GLOBAL_CONFIG = CFG_DIR / "shire-config.yaml"
DEFAULT_INITIAL_CONDITIONS = "nominal-baseline"


class ConfigError(RuntimeError):
    """A selection (mission, spacecraft, scenario, initial condition) that does not resolve."""


REPLACE_KEY = "_replace"


def deep_merge(base: dict, override: dict) -> dict:
    """Returns a new dict where nested mappings merge key by key and anything else in `override`
    (a scalar or a list) replaces the base value. A mapping in `override` that carries
    `_replace: true` replaces the base value wholesale; the marker is dropped from the result."""
    merged = deepcopy(base)
    for key, value in override.items():
        if isinstance(value, dict):
            replace = bool(value.get(REPLACE_KEY))
            value = {k: v for k, v in value.items() if k != REPLACE_KEY}
            if not replace and isinstance(merged.get(key), dict):
                merged[key] = deep_merge(merged[key], value)
                continue
            merged[key] = deepcopy(value)
        else:
            merged[key] = deepcopy(value)
    return merged


def vehicle_initial_conditions(ic_cfg: dict, vehicle_id: str) -> dict:
    """The IC a single vehicle starts from. An IC bin may carry a `vehicles:` section keyed by
    vehicle id that overlays the top-level orbit, attitude, and component_overrides; a vehicle with no
    entry (and every single-vehicle IC) gets the top-level values unchanged."""
    shared = {k: v for k, v in ic_cfg.items() if k != "vehicles"}
    return deep_merge(shared, (ic_cfg.get("vehicles") or {}).get(vehicle_id) or {})


def select_vehicle(constellation: dict, vehicle_id: str | None) -> dict:
    """The named vehicle of a normalized constellation."""
    vehicles = {v["id"]: v for v in constellation["vehicles"]}
    if not vehicle_id:
        raise ConfigError(f"No vehicle is selected. Vehicles in constellation '{constellation['constellation_name']}': "
                          f"{', '.join(vehicles)}.")
    if vehicle_id not in vehicles:
        raise ConfigError(f"Vehicle '{vehicle_id}' not found in constellation '{constellation['constellation_name']}'. "
                          f"Available: {', '.join(vehicles)}.")
    return vehicles[vehicle_id]


def apply_vehicle(spacecraft_cfg: dict, ic_cfg: dict, vehicle: dict) -> tuple[dict, dict]:
    """Specializes a spacecraft type and an initial condition bin for one constellation vehicle.

    The vehicle's node number replaces the spacecraft's `dtn.node_id`, and its per component overrides merge
    over the spacecraft's settings (above the spacecraft file, below the IC bin and the scenario). The IC becomes
    the vehicle's own orbit and attitude. Neither input is modified."""
    specialized = deepcopy(spacecraft_cfg)
    if "dtn" in specialized:
        specialized["dtn"] = deep_merge(specialized["dtn"] or {}, {"node_id": vehicle["node_id"]})
    for component, settings in vehicle["overrides"].items():
        specialized[component] = deep_merge(specialized.get(component) or {}, settings)
    return specialized, vehicle_initial_conditions(ic_cfg, vehicle["id"])


def load_yaml(path) -> dict | None:
    """Returns the parsed YAML at `path`, or None when the file does not exist."""
    if not os.path.exists(path):
        return None
    with open(path, "r") as f:
        return yaml.safe_load(f)


def load_ground_station_catalog(path) -> list[dict]:
    return (load_yaml(path) or {}).get("ground_stations", [])


def installed_components(root: Path = ROOT) -> set[str]:
    """Component names present in the checkout (the directories under comp/)."""
    return {p.name for p in (root / "comp").iterdir() if p.is_dir()} if (root / "comp").is_dir() else set()


@dataclass(frozen=True)
class MissionConfig:
    """A mission resolved from the global registry, with lookups relative to the
    directory its YAML lives in (cfg/drm/ for the DRM) instead of a hardcoded path.

    Every lookup raises ConfigError when the name is not registered or its file is
    missing, so the orchestrator and the runners fail the same way on the same input."""
    name: str
    global_cfg: dict
    config_file: str
    mission_cfg: dict
    cfg_dir: Path = CFG_DIR

    @property
    def directory(self) -> Path:
        return self.cfg_dir / Path(self.config_file).parent

    @property
    def initial_conditions_dir(self) -> Path:
        return self.directory / "initial_conditions"

    @property
    def campaigns_dir(self) -> Path:
        return self.directory / "campaigns"

    @property
    def ground_stations_path(self) -> Path:
        return self.directory / "ground_stations.yaml"

    def ground_stations(self) -> list[dict]:
        return load_ground_station_catalog(self.ground_stations_path)

    def scenario_names(self) -> list[str]:
        return [s["name"] for s in self.mission_cfg.get("scenarios", [])]

    def spacecraft_names(self) -> list[str]:
        return [s["name"] for s in self.mission_cfg.get("spacecraft", [])]

    def _available(self, key: str) -> str:
        return ", ".join(e["name"] for e in self.mission_cfg.get(key, [])) or "none"

    def _load_entry(self, kind: str, key: str, name: str) -> dict:
        entry = next((e for e in self.mission_cfg.get(key, []) if e["name"] == name), None)
        if not entry:
            raise ConfigError(f"{kind} '{name}' not found in mission '{self.name}' config. "
                              f"Available: {self._available(key)}.")
        return load_yaml(self.cfg_dir / entry["config_file"]) or {}

    def load_spacecraft(self, name: str | None) -> dict:
        """The spacecraft's config, or {} when no spacecraft is selected. A spacecraft may name another
        registered spacecraft in `extends:`; the child deep-merges over its base (lists replace), so a
        relay or leaf vehicle can be a small delta over a base vehicle type."""
        return self._load_spacecraft(name, ()) if name else {}

    def _load_spacecraft(self, name: str, chain: tuple[str, ...]) -> dict:
        if name in chain:
            raise ConfigError(f"Spacecraft extends cycle: {' -> '.join((*chain, name))}.")
        config = self._load_entry("Spacecraft", "spacecraft", name)
        base_name = config.get("extends")
        if not base_name:
            return config
        own = {k: v for k, v in config.items() if k != "extends"}
        return deep_merge(self._load_spacecraft(base_name, (*chain, name)), own)

    def constellation_names(self) -> list[str]:
        return [c["name"] for c in self.mission_cfg.get("constellations", [])]

    def load_constellation(self, name: str, component_names: set[str] | None = None) -> dict:
        """The normalized constellation: validated, with vehicle indices and link seeds assigned."""
        raw = self._load_entry("Constellation", "constellations", name)
        try:
            return normalize_constellation(
                raw, spacecraft_names=self.spacecraft_names(),
                component_names=component_names if component_names is not None else installed_components())
        except ValueError as exc:
            raise ConfigError(f"Constellation '{name}': {exc}") from exc

    def load_scenario(self, name: str | None) -> dict:
        if not name:
            raise ConfigError(f"No scenario is selected. Scenarios in mission '{self.name}': "
                              f"{self._available('scenarios')}.")
        entry = next((e for e in self.mission_cfg.get("scenarios", []) if e["name"] == name), None)
        if not entry:
            raise ConfigError(f"Scenario '{name}' not found in mission '{self.name}' config. "
                              f"Available: {self._available('scenarios')}.")
        scenario_cfg = load_yaml(self.cfg_dir / entry["config_file"])
        if scenario_cfg is None:
            raise ConfigError(f"Could not load scenario config for '{name}'.")
        return scenario_cfg

    def load_initial_conditions(self, name: str | None = None, file: str | os.PathLike | None = None
                                ) -> tuple[str, Path, dict]:
        """Returns (label, path, config). An explicit `file`, such as a campaign trial's
        perturbed IC, takes priority over the named bin."""
        if file:
            path, label = Path(file), f"file:{file}"
            what = f"Initial condition file '{file}'"
        else:
            label = name or DEFAULT_INITIAL_CONDITIONS
            path = self.initial_conditions_dir / f"{label}.yaml"
            what = f"Initial condition bin '{label}' at {path}"
        config = load_yaml(path)
        if config is None:
            raise ConfigError(f"{what} not found.")
        return label, path, config


def load_global_config(path=GLOBAL_CONFIG) -> dict:
    return load_yaml(path) or {}


def load_mission(name: str, *, global_cfg: dict | None = None, cfg_dir: Path = CFG_DIR) -> MissionConfig:
    """Resolves `name` through the global mission registry."""
    if global_cfg is None:
        global_cfg = load_global_config(cfg_dir / "shire-config.yaml")
    entry = next((m for m in global_cfg.get("build", {}).get("missions", []) if m["name"] == name), None)
    if not entry:
        available = ", ".join(m["name"] for m in global_cfg.get("build", {}).get("missions", [])) or "none"
        raise ConfigError(f"Mission '{name}' not found in global config. Available: {available}.")
    mission_cfg = load_yaml(cfg_dir / entry["config_file"]) or {}
    return MissionConfig(name, global_cfg, entry["config_file"], mission_cfg, cfg_dir)


def load_component_fallback(comp_name: str, root=ROOT) -> dict:
    """The component's own defaults from comp/<name>/support/device_config.yaml."""
    data = load_yaml(os.path.join(root, "comp", comp_name, "support", "device_config.yaml"))
    return dict(data[comp_name]) if data and data.get(comp_name) else {}


def select_components(mission_cfg: dict, spacecraft_cfg: dict | None) -> list[dict]:
    """The component list a build uses: the spacecraft's when a spacecraft
    config is selected, else the mission-level list kept for backward
    compatibility. A spacecraft config that omits `components` selects none."""
    if spacecraft_cfg:
        return spacecraft_cfg.get("components", [])
    return mission_cfg.get("components", [])


def cascade_component(comp_name: str, components: list[dict], fallback: dict, *,
                      global_cfg: dict, mission_cfg: dict, spacecraft_cfg: dict | None,
                      ic_cfg: dict, scenario_cfg: dict, ground_stations: list[dict],
                      cli_debug: bool = False, cli_component: str = "demo") -> dict:
    """Resolves one component's configuration through every layer, lowest
    precedence first, then normalizes the components that have a validator.
    Raises ValueError when a normalizer rejects the merged result."""
    comp_cfg = dict(fallback)
    merge_component_config(comp_name, comp_cfg, global_cfg.get(comp_name, {}) or {})
    merge_component_config(comp_name, comp_cfg, mission_cfg.get(comp_name, {}) or {})
    if spacecraft_cfg:
        merge_component_config(comp_name, comp_cfg, spacecraft_cfg.get(comp_name, {}) or {})
    # An IC bin's per-component state (EPS SOC, ADCS starting mode, fault flags) is applied
    # before the scenario's own component config, so a scenario can still override it.
    merge_component_config(comp_name, comp_cfg, (ic_cfg.get("component_overrides") or {}).get(comp_name, {}) or {})
    merge_component_config(comp_name, comp_cfg, scenario_cfg.get(comp_name, {}) or {})
    merge_component_config(comp_name, comp_cfg, scenario_cfg.get("overrides", {}) or {})
    if cli_debug and comp_name == cli_component:
        comp_cfg["debug"] = True

    if comp_name == "eps":
        comp_cfg = normalize_eps(comp_cfg, {c["name"] for c in components})
    if comp_name == "radio":
        comp_cfg = normalize_radio(comp_cfg, ground_stations)
    return comp_cfg


def resolve_component_configs(global_cfg: dict[str, object], mission_cfg: dict[str, object],
                              spacecraft_cfg: dict[str, object], scenario_cfg: dict[str, object],
                              ic_cfg: dict[str, object], *, cli_debug: bool = False,
                              cli_component: str = 'demo',
                              ground_stations: list[dict]) -> dict[str, dict]:
    """Resolves every component of the selected build without Jinja or Docker, so a
    campaign can hash everything that will be baked into comp/<name>/shared/device_cfg.h
    without rendering or building anything. Also carries the `__fsw` and `__limits`
    pseudo-components that the scenario snapshot records. `ground_stations` is the selected
    mission's catalog (MissionConfig.ground_stations()); it has no default, so one mission's
    stations can never silently stand in for another's."""
    components = select_components(mission_cfg, spacecraft_cfg)

    resolved: dict[str, dict] = {}
    for comp in components:
        comp_name = comp.get("name")
        if not comp_name:
            continue
        resolved[comp_name] = cascade_component(
            comp_name, components, load_component_fallback(comp_name),
            global_cfg=global_cfg, mission_cfg=mission_cfg, spacecraft_cfg=spacecraft_cfg,
            ic_cfg=ic_cfg, scenario_cfg=scenario_cfg, ground_stations=ground_stations,
            cli_debug=cli_debug, cli_component=cli_component)
    if scenario_cfg.get('retain_all_events'):
        resolved['__fsw'] = {'event_burst_max': 1000000, 'event_refill_per_sec': 1000000}
    limits = resolve_limits(global_cfg.get("limits"), mission_cfg.get("limits"),
                            (spacecraft_cfg or {}).get("limits"), scenario_cfg.get("limits"))
    if limits:
        resolved['__limits'] = limits
    return resolved

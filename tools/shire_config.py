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
from dataclasses import dataclass
from pathlib import Path

import yaml

from shire_provenance import ROOT
from shire_eps_config import normalize_eps, merge_component_config
from shire_radio_config import normalize_radio
from shire_limits_config import resolve_limits

CFG_DIR = ROOT / "cfg"
GLOBAL_CONFIG = CFG_DIR / "shire-config.yaml"
GROUND_STATIONS_PATH = CFG_DIR / "drm" / "ground_stations.yaml"
DEFAULT_INITIAL_CONDITIONS = "nominal-baseline"


class ConfigError(RuntimeError):
    """A selection (mission, spacecraft, scenario, initial condition) that does not resolve."""


def load_yaml(path) -> dict | None:
    """Returns the parsed YAML at `path`, or None when the file does not exist."""
    if not os.path.exists(path):
        return None
    with open(path, "r") as f:
        return yaml.safe_load(f)


def load_ground_station_catalog(path=GROUND_STATIONS_PATH) -> list[dict]:
    return (load_yaml(path) or {}).get("ground_stations", [])


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

    def _load_entry(self, kind: str, key: str, name: str) -> dict:
        entry = next((e for e in self.mission_cfg.get(key, []) if e["name"] == name), None)
        if not entry:
            raise ConfigError(f"{kind} '{name}' not found in mission '{self.name}' config.")
        return load_yaml(self.cfg_dir / entry["config_file"]) or {}

    def load_spacecraft(self, name: str | None) -> dict:
        """The spacecraft's config, or {} when no spacecraft is selected."""
        return self._load_entry("Spacecraft", "spacecraft", name) if name else {}

    def load_scenario(self, name: str) -> dict:
        entry = next((e for e in self.mission_cfg.get("scenarios", []) if e["name"] == name), None)
        if not entry:
            raise ConfigError(f"Scenario '{name}' not found in mission '{self.name}' config.")
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
        raise ConfigError(f"Mission '{name}' not found in global config.")
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
                              ground_stations: list[dict] | None = None) -> dict[str, dict]:
    """Resolves every component of the selected build without Jinja or Docker, so a
    campaign can hash everything that will be baked into comp/<name>/shared/device_cfg.h
    without rendering or building anything. Also carries the `__fsw` and `__limits`
    pseudo-components that the scenario snapshot records."""
    components = select_components(mission_cfg, spacecraft_cfg)
    if ground_stations is None:
        ground_stations = load_ground_station_catalog()

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

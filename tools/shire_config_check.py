"""Static validation of SHIRE's configuration tree, with no Docker and no build.

check_repository() walks every mission in the global registry and returns a list of problems
instead of stopping at the first, so a single `make cfg-check` shows everything a typo or a bad
reference broke. It rejects unknown keys in each file kind, which the loaders otherwise accept
silently (a misspelled `run_duraton_s` would be ignored and the default used).
"""
from __future__ import annotations

from pathlib import Path

from shire_config import (CFG_DIR, ConfigError, MissionConfig, installed_components, load_global_config,
                          load_mission, load_yaml, vehicle_initial_conditions)
from shire_dtn_config import normalize_dtn
from shire_provenance import ROOT
from shire_runner_lib import container_names

MISSION_KEYS = {"mission_name", "eps", "limits", "spacecraft", "components", "scenarios", "constellations",
                "fsw", "gsw"}
SPACECRAFT_KEYS = {"spacecraft_name", "spacecraft_id", "extends", "visualization", "components", "limits", "dtn"}
SCENARIO_KEYS = {"scenario_name", "description", "initial_conditions", "overrides", "run_duration_s",
                 "verify_stacks", "simulith_speed", "accelerate_after_verification", "post_verification_speed",
                 "synchronize_ground_output", "log_assertions", "verification_clock_parameter", "eps_validation",
                 "retain_all_events", "limits", "director_command_scenario", "ground_output_hz"}
INITIAL_CONDITION_KEYS = {"name", "description", "epoch", "orbit", "attitude", "component_overrides", "vehicles"}
VEHICLE_IC_KEYS = {"orbit", "attitude", "component_overrides"}
CAMPAIGN_KEYS = {"name", "description", "scenario", "base_initial_conditions", "trial_count", "seed",
                 "max_parallel", "parameters", "metrics"}
REQUIRED_INITIAL_CONDITION_KEYS = ("epoch", "orbit", "attitude")
# SHIRE's 42 input enables only Earth and the Moon (Inp_Sim.j2), so those are the central bodies and station worlds a run can use.
WORLDS = ("EARTH", "LUNA")
YAMCS_CONFIG = Path("yamcs/src/main/yamcs/etc/yamcs.shire.yaml")


def _unknown(where: str, keys, allowed, errors: list[str]) -> None:
    extra = sorted(set(keys) - allowed)
    if extra:
        errors.append(f"{where}: unknown key(s) {', '.join(extra)}")


def _duplicates(values) -> list:
    seen, dupes = set(), []
    for value in values:
        if value in seen and value not in dupes:
            dupes.append(value)
        seen.add(value)
    return dupes


def wiring_resources(block: dict) -> list[tuple]:
    """The hardware a component block claims: a UART handle, an I2C bus and address, an SPI bus and chip
    select, and any GPIO pin."""
    claims = []
    if "handle" in block:
        claims.append(("uart", block["handle"]))
    if "i2c_bus_id" in block and "i2c_device_addr" in block:
        claims.append(("i2c", block["i2c_bus_id"], block["i2c_device_addr"]))
    if "spi_bus" in block and "spi_cs" in block:
        claims.append(("spi", block["spi_bus"], block["spi_cs"]))
    claims.extend(("gpio", block[k]) for k in sorted(block) if k.startswith("gpio_") and k.endswith("_pin"))
    return claims


def _check_center(where: str, orbit, errors: list[str]) -> None:
    center = (orbit or {}).get("center", "EARTH")
    if center not in WORLDS:
        errors.append(f"{where}: orbit.center '{center}' must be one of {', '.join(WORLDS)}")


def required_mission_xtce(root: Path) -> list[str]:
    """Top-level XTCE files YAMCS loads straight from the mission's gsw/ directory, read from the YAMCS
    configuration. Empty when the yamcs submodule is not checked out."""
    config = load_yaml(root / YAMCS_CONFIG) or {}
    names = [m.get("args", {}).get("file", "") for m in config.get("mdb", []) if m.get("type") == "xtce"]
    return sorted(Path(n).name for n in names if n.startswith("mdb/") and n.count("/") == 1)


def check_ground_software(mission: MissionConfig, root: Path, errors: list[str]) -> None:
    """A mission's gsw/ directory is copied into YAMCS whole, so it must carry the definitions every mission
    needs; without them YAMCS fails to start."""
    for name in required_mission_xtce(root):
        if not (mission.directory / "gsw" / name).is_file():
            errors.append(f"ground software: {mission.directory.relative_to(mission.cfg_dir)}/gsw/{name} is missing "
                          f"(YAMCS loads mdb/{name} from the mission)")


def check_ground_stations(mission: MissionConfig, errors: list[str]) -> None:
    catalog = load_yaml(mission.ground_stations_path)
    if catalog is None:
        errors.append(f"ground stations: {mission.ground_stations_path.relative_to(mission.cfg_dir)} is missing")
        return
    names = []
    for entry in catalog.get("ground_stations", []):
        name = entry.get("name")
        where = f"ground station '{name}'"
        names.append(name)
        if entry.get("world", "EARTH") not in WORLDS:
            errors.append(f"{where}: world '{entry.get('world')}' must be one of {', '.join(WORLDS)}")
        for key, limit in (("lat_deg", 90), ("lon_deg", 180)):
            value = entry.get(key)
            if isinstance(value, bool) or not isinstance(value, (int, float)) or abs(value) > limit:
                errors.append(f"{where}: {key} must be a number within +-{limit}")
    for name in _duplicates(names):
        errors.append(f"ground station '{name}' is listed more than once")


def check_registry(mission: MissionConfig, errors: list[str]) -> None:
    where = f"mission '{mission.name}'"
    _unknown(where, mission.mission_cfg, MISSION_KEYS | installed_components(), errors)
    for key in ("spacecraft", "scenarios", "constellations"):
        entries = mission.mission_cfg.get(key, [])
        for name in _duplicates(e.get("name") for e in entries):
            errors.append(f"{where}: {key} entry '{name}' is registered more than once")
        for entry in entries:
            if not (mission.cfg_dir / entry.get("config_file", "")).is_file():
                errors.append(f"{where}: {key} entry '{entry.get('name')}' config_file "
                              f"'{entry.get('config_file')}' does not exist")


def check_spacecraft(mission: MissionConfig, components: set[str], errors: list[str]) -> None:
    ids: dict[object, str] = {}
    for name in mission.spacecraft_names():
        where = f"spacecraft '{name}'"
        try:
            resolved = mission.load_spacecraft(name)
        except ConfigError as exc:
            errors.append(f"{where}: {exc}")
            continue
        _unknown(where, resolved, SPACECRAFT_KEYS | components, errors)
        if resolved.get("spacecraft_name") != name:
            errors.append(f"{where}: spacecraft_name is {resolved.get('spacecraft_name')!r}, "
                          f"expected the registered name")
        try:
            normalize_dtn(resolved.get("dtn"))
        except ValueError as exc:
            errors.append(f"{where}: {exc}")
        sid = resolved.get("spacecraft_id")
        if sid in ids:
            errors.append(f"{where}: spacecraft_id {sid} is already used by '{ids[sid]}'")
        ids[sid] = name

        listed = [c.get("name") for c in resolved.get("components", [])]
        for comp in listed:
            if comp not in components:
                errors.append(f"{where}: component '{comp}' is not under comp/")
        for comp in _duplicates(listed):
            errors.append(f"{where}: component '{comp}' is listed more than once")
        for comp in sorted(components - set(listed)):
            if comp in resolved:
                errors.append(f"{where}: has a '{comp}' settings block but does not list the component")

        claimed: dict[tuple, str] = {}
        for comp in listed:
            for claim in wiring_resources(resolved.get(comp) or {}):
                if claim in claimed:
                    errors.append(f"{where}: {comp} and {claimed[claim]} both use {claim[0]} {claim[1:]}")
                claimed[claim] = comp


def check_scenarios(mission: MissionConfig, components: set[str], root: Path, errors: list[str]) -> None:
    roles = set(container_names("m", "s"))
    for name in mission.scenario_names():
        where = f"scenario '{name}'"
        try:
            scenario = mission.load_scenario(name)
        except ConfigError as exc:
            errors.append(f"{where}: {exc}")
            continue
        _unknown(where, scenario, SCENARIO_KEYS | components, errors)
        if scenario.get("scenario_name") != name:
            errors.append(f"{where}: scenario_name is {scenario.get('scenario_name')!r}, expected the registered name")
        try:
            mission.load_initial_conditions(scenario.get("initial_conditions"))
        except ConfigError as exc:
            errors.append(f"{where}: {exc}")
        for key, minimum, allows_max in (("run_duration_s", 0, False), ("simulith_speed", 0, True),
                                         ("post_verification_speed", -1, True)):
            if key not in scenario:
                continue
            value = scenario[key]
            if allows_max and value == "max":
                continue
            # A speed is a multiplier above zero, "max" for as fast as possible, and (after verification only) 0 for unbounded.
            if (isinstance(value, bool) or not isinstance(value, (int, float))
                    or (value < 0 if minimum < 0 else value <= 0)):
                errors.append(f"{where}: {key} must be a positive number" + (' or "max"' if allows_max else "")
                              + (" (post_verification_speed may also be 0)" if key == "post_verification_speed" else ""))
        for step in scenario.get("verify_stacks") or []:
            stack = step.get("stack") if isinstance(step, dict) else None
            if not stack or not (root / stack).is_file():
                errors.append(f"{where}: verify_stacks stack '{stack}' does not exist")
            if not isinstance(step, dict) or not isinstance(step.get("at_s"), (int, float)):
                errors.append(f"{where}: verify_stacks entries need a numeric at_s")
        for entry in scenario.get("log_assertions") or []:
            if entry.get("container") not in roles:
                errors.append(f"{where}: log_assertions container '{entry.get('container')}' "
                              f"is not one of {', '.join(sorted(roles))}")


def check_initial_conditions(mission: MissionConfig, errors: list[str]) -> None:
    for path in sorted(mission.initial_conditions_dir.glob("*.yaml")):
        where = f"initial condition '{path.stem}'"
        ic = load_yaml(path) or {}
        _unknown(where, ic, INITIAL_CONDITION_KEYS, errors)
        for key in REQUIRED_INITIAL_CONDITION_KEYS:
            if key not in ic:
                errors.append(f"{where}: missing '{key}'")
        _check_center(where, ic.get("orbit"), errors)
        for vehicle, section in (ic.get("vehicles") or {}).items():
            _unknown(f"{where} vehicles.{vehicle}", section or {}, VEHICLE_IC_KEYS, errors)
            _check_center(f"{where} vehicles.{vehicle}", (section or {}).get("orbit"), errors)


def check_campaigns(mission: MissionConfig, errors: list[str]) -> None:
    for path in sorted(mission.campaigns_dir.glob("*.yaml")):
        where = f"campaign '{path.stem}'"
        campaign = load_yaml(path) or {}
        _unknown(where, campaign, CAMPAIGN_KEYS, errors)
        if campaign.get("scenario") not in mission.scenario_names():
            errors.append(f"{where}: scenario '{campaign.get('scenario')}' is not registered")
        if not (mission.initial_conditions_dir / f"{campaign.get('base_initial_conditions')}.yaml").is_file():
            errors.append(f"{where}: base_initial_conditions '{campaign.get('base_initial_conditions')}' not found")


def check_constellations(mission: MissionConfig, components: set[str], errors: list[str]) -> None:
    for name in mission.constellation_names():
        where = f"constellation '{name}'"
        try:
            constellation = mission.load_constellation(name, components)
        except ConfigError as exc:
            errors.append(str(exc))
            continue
        for vehicle in constellation["vehicles"]:
            try:
                runs_bundle_protocol = bool(mission.load_spacecraft(vehicle["spacecraft"]).get("dtn"))
            except ConfigError:
                continue  # the broken spacecraft is reported with its own entry
            if not runs_bundle_protocol:
                errors.append(f"{where}: vehicle '{vehicle['id']}' flies spacecraft '{vehicle['spacecraft']}', "
                              f"which has no dtn: block, so it would not run Bundle Protocol")
        ic_name = constellation.get("initial_conditions")
        if ic_name is None:
            continue
        try:
            _, _, ic = mission.load_initial_conditions(ic_name)
        except ConfigError as exc:
            errors.append(f"{where}: {exc}")
            continue
        sections = ic.get("vehicles") or {}
        known = {v["id"] for v in constellation["vehicles"]}
        for stray in sorted(set(sections) - known):
            errors.append(f"{where}: initial condition '{ic_name}' has a vehicles entry '{stray}' "
                          f"that is not a vehicle")
        orbits = {}
        for vehicle in constellation["vehicles"]:
            if len(known) > 1 and vehicle["id"] not in sections:
                errors.append(f"{where}: initial condition '{ic_name}' has no vehicles entry for '{vehicle['id']}'")
            orbit = vehicle_initial_conditions(ic, vehicle["id"]).get("orbit")
            key = repr(sorted((orbit or {}).items()))
            if key in orbits:
                errors.append(f"{where}: vehicles '{orbits[key]}' and '{vehicle['id']}' start in the same orbit")
            orbits[key] = vehicle["id"]


def check_repository(cfg_dir: Path = CFG_DIR, root: Path = ROOT) -> list[str]:
    """Every problem found across all registered missions, as `mission: where: message` lines."""
    errors: list[str] = []
    global_cfg = load_global_config(cfg_dir / "shire-config.yaml")
    missions = global_cfg.get("build", {}).get("missions", [])
    if not missions:
        return ["global config registers no missions"]
    components = installed_components(root)
    for entry in missions:
        found: list[str] = []
        try:
            mission = load_mission(entry["name"], global_cfg=global_cfg, cfg_dir=cfg_dir)
        except (ConfigError, OSError) as exc:
            errors.append(f"mission '{entry.get('name')}': {exc}")
            continue
        check_registry(mission, found)
        for listed in mission.mission_cfg.get("components", []):
            if listed.get("name") not in components:
                found.append(f"fallback component '{listed.get('name')}' is not under comp/")
        check_spacecraft(mission, components, found)
        check_scenarios(mission, components, root, found)
        check_initial_conditions(mission, found)
        check_campaigns(mission, found)
        check_constellations(mission, components, found)
        check_ground_software(mission, root, found)
        check_ground_stations(mission, found)
        errors.extend(f"{mission.name}: {message}" for message in found)
    return errors

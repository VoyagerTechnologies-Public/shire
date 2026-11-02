"""Planning and compose rendering for a multi-vehicle constellation run.

Kept apart from shire-constellation.py (which builds images and drives Docker) so the plan and the compose file
can be tested without either.
"""
from __future__ import annotations

from pathlib import Path

from jinja2 import Environment, FileSystemLoader

from shire_config import (CFG_DIR, MissionConfig, apply_vehicle, resolve_component_configs, select_vehicle)

TEMPLATE = "constellation-compose.j2"
GSW_BASE_PORT = 8090
GSW_PORT_STRIDE = 10


def vehicle_plan(mission: MissionConfig, constellation_name: str, scenario_name: str, trace_root: Path,
                 *, base_port: int = GSW_BASE_PORT, stride: int = GSW_PORT_STRIDE) -> tuple[dict, list[dict]]:
    """The constellation and, for each vehicle in order, what the compose file needs: its spacecraft type, host port,
    trace directory, and the radio link security its ground CryptoLib must match."""
    constellation = mission.load_constellation(constellation_name)
    scenario_cfg = mission.load_scenario(scenario_name)
    ic_request = constellation.get("initial_conditions") or scenario_cfg.get("initial_conditions")
    _, _, ic_cfg = mission.load_initial_conditions(ic_request)
    ground_stations = mission.ground_stations()
    plan = []
    for index, entry in enumerate(constellation["vehicles"]):
        vehicle = select_vehicle(constellation, entry["id"])
        spacecraft_cfg, vehicle_ic = apply_vehicle(mission.load_spacecraft(vehicle["spacecraft"]), ic_cfg, vehicle)
        resolved = resolve_component_configs(mission.global_cfg, mission.mission_cfg, spacecraft_cfg, scenario_cfg,
                                             vehicle_ic, ground_stations=ground_stations)
        radio = resolved.get("radio", {})
        plan.append({
            "id": vehicle["id"],
            "index": index,
            "spacecraft": vehicle["spacecraft"],
            "role": vehicle["role"],
            "node_id": vehicle["node_id"],
            "gsw_port": base_port + stride * index,
            "trace_dir": str(trace_root / vehicle["id"]),
            "uplink_encryption": radio.get("uplink_encryption", True) is not False,
            "downlink_encryption": radio.get("downlink_encryption", True) is not False,
        })
    return constellation, plan


def render_compose(mission_name: str, constellation_name: str, vehicles: list[dict], *, log_mode: str = "stdout",
                   template_dir: Path = CFG_DIR) -> str:
    environment = Environment(loader=FileSystemLoader(str(template_dir)), keep_trailing_newline=True)
    return environment.get_template(TEMPLATE).render(
        mission=mission_name, constellation=constellation_name, vehicles=vehicles, log_mode=log_mode)

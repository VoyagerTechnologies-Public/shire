#!/usr/bin/env python3
"""Shared full-stack Docker Compose helpers for SHIRE's noninteractive runners.

Extracted from shire-perf.py so shire-scenario.py (the headless
"confirm a pass" runner) can drive the same compose stack and read the same
completion markers without re-deriving container names or duplicating the
terminal-marker parsing logic. Extended for Monte Carlo campaigns (issue
#23) with instance-scoped naming and build-key hashing, so many trials'
compose stacks can run concurrently without colliding.
"""

from __future__ import annotations

import hashlib
import json
import os
import socket
import subprocess
import threading
import time

import yaml

from shire_provenance import ROOT


def container_names(mission: str, spacecraft: str, instance: str | None = None) -> dict[str, str]:
    """Returns this run's container names. `instance` is None for a plain
    `make scenario` run (byte-identical to the pre-campaign names); a
    Monte Carlo trial passes its own instance token so its stack's names
    never collide with another concurrently-running trial's."""
    suffix = f"-{instance}" if instance else ""
    return {
        "server": f"shire-server-{mission}{suffix}",
        "director": f"shire-director-{spacecraft}{suffix}",
        "fsw": f"shire-fsw-{spacecraft}{suffix}",
        "42": f"shire-42-{spacecraft}{suffix}",
        "gsw": f"shire-gsw-{mission}{suffix}",
        "cryptolib": f"shire-cryptolib-{spacecraft}{suffix}",
    }


def run_streaming(cmd: list[str], *, cwd: object = None, env: dict[str, str] | None = None,
                  timeout: float | None = None) -> tuple[int, str]:
    """Runs `cmd`, printing its combined stdout/stderr live (line by line,
    flushed immediately) to this process's stdout, while also capturing it
    to return for writing to a log file afterward -- so a long-running
    child (a full `make build`, or a `.ycs` stack with a 100+-second CFDP
    step) shows visible progress instead of going silent until it exits.
    Shared by shire-scenario.py and shire-campaign.py.

    Reads the child's output on a background thread so `timeout` is
    enforced even if the child produces no output at all (a plain
    line-blocking read loop wouldn't notice a hang between lines)."""
    proc = subprocess.Popen(cmd, cwd=cwd or ROOT, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=1)
    lines: list[str] = []

    def _pump() -> None:
        assert proc.stdout is not None
        for line in proc.stdout:
            print(line, end="", flush=True)
            lines.append(line)

    pump = threading.Thread(target=_pump, daemon=True)
    pump.start()

    started = time.monotonic()
    timed_out = False
    while proc.poll() is None:
        if timeout is not None and time.monotonic() - started > timeout:
            proc.terminate()
            timed_out = True
            break
        time.sleep(0.1)
    pump.join(timeout=5)
    if timed_out:
        raise subprocess.TimeoutExpired(cmd, timeout, output="".join(lines))
    return proc.returncode, "".join(lines)


def parse_marker(log: str, marker: str) -> dict[str, object]:
    for line in reversed(log.splitlines()):
        if marker in line:
            start = line.find("{", line.find(marker))
            if start >= 0:
                return json.loads(line[start:])
    raise RuntimeError(f"process exited without {marker}")


def try_parse_marker(log: str, marker: str) -> dict[str, object] | None:
    """Non-raising variant of parse_marker(), for callers that treat a
    missing marker as one signal among several rather than a hard error."""
    try:
        return parse_marker(log, marker)
    except RuntimeError:
        return None


def resolve_component_configs(global_cfg: dict[str, object], mission_cfg: dict[str, object],
                              spacecraft_cfg: dict[str, object], scenario_cfg: dict[str, object],
                              ic_cfg: dict[str, object], *, cli_debug: bool = False) -> dict[str, dict]:
    """Reproduces shire-orchestrator.py's per-component cascading merge
    (fallback -> global -> mission -> spacecraft -> IC component_overrides
    -> scenario -> scenario overrides) standalone, without Jinja/Docker, so
    a Monte Carlo campaign runner can hash "everything that will end up
    baked into comp/<name>/shared/device_cfg.h" for a trial without
    actually rendering or building anything. Keep this in lockstep with
    shire-orchestrator.py's main() cascading-merge loop -- it is a
    deliberate duplicate (that loop is inline in a large main(), not
    exported), not a shared import, so a change to one must be mirrored in
    the other."""
    if spacecraft_cfg and spacecraft_cfg.get("components"):
        components = spacecraft_cfg.get("components", [])
    else:
        components = mission_cfg.get("components", [])

    resolved: dict[str, dict] = {}
    for comp in components:
        comp_name = comp.get("name")
        if not comp_name:
            continue

        fallback_path = os.path.abspath(
            os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        f"../comp/{comp_name}/support/device_config.yaml"))
        comp_cfg: dict[str, object] = {}
        if os.path.exists(fallback_path):
            with open(fallback_path, "r") as f:
                fallback_data = yaml.safe_load(f)
            if fallback_data and fallback_data.get(comp_name):
                comp_cfg = dict(fallback_data[comp_name])

        comp_cfg.update(global_cfg.get(comp_name, {}) or {})
        comp_cfg.update(mission_cfg.get(comp_name, {}) or {})
        if spacecraft_cfg:
            comp_cfg.update(spacecraft_cfg.get(comp_name, {}) or {})
        comp_cfg.update((ic_cfg.get("component_overrides") or {}).get(comp_name, {}) or {})
        comp_cfg.update(scenario_cfg.get(comp_name, {}) or {})
        comp_cfg.update(scenario_cfg.get("overrides", {}) or {})
        if cli_debug:
            comp_cfg["debug"] = True

        resolved[comp_name] = comp_cfg
    return resolved


def compute_build_key(ic_cfg: dict[str, object], resolved_components: dict[str, dict]) -> str:
    """Hashes exactly what shire-orchestrator.py bakes into images at build
    time (the resolved IC -- orbit/epoch/attitude feed 42's InOut txt files,
    component_overrides feed device_cfg.h -- plus the fully-cascaded
    per-component config dicts) into a short, stable, content-addressed
    key. Two trials whose IC and resolved component configs are identical
    get the same key and can safely share one built image; any difference
    (continuous orbit/attitude sweep values almost always differ trial to
    trial) produces a different key, forcing its own rebuild."""
    canonical = json.dumps({"ic": ic_cfg, "components": resolved_components},
                           sort_keys=True, default=str)
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()[:10]


def _port_is_free(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind(("0.0.0.0", port))
            return True
        except OSError:
            return False


def find_free_port_offset(base_ports: list[int], candidate_offset: int = 0,
                          stride: int = 10, max_tries: int = 200) -> int:
    """Finds a port offset such that every `base_ports[i] + offset` is free
    on the host right now, starting from `candidate_offset` (typically
    `trial_index * stride`) and stepping by `stride` past it if occupied --
    a leftover container from a crashed prior campaign, or an unrelated
    host process, can occupy the naive guess. A plain socket bind probe,
    not a Docker query, since the collision could be either."""
    offset = candidate_offset
    for _ in range(max_tries):
        if all(_port_is_free(port + offset) for port in base_ports):
            return offset
        offset += stride
    raise RuntimeError(
        f"could not find a free port offset starting from {candidate_offset} "
        f"(stride {stride}) after {max_tries} tries for base ports {base_ports}")

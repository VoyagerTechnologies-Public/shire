#!/usr/bin/env python3
"""Autonomous, no-GUI scenario runner: confirm a scenario completes cleanly.

Usage:
    python3 cfg/shire-scenario.py --scenario <name> [--mission drm] [--spacecraft sat-1]

Brings up the full stack (director, 42, FSW, GSW, cryptolib, server) exactly
as `make start` would -- the 42 container still starts its VNC/xterm/noVNC
internals unchanged -- but nothing opens a GUI and nothing waits for a
human. This script waits for the run to finish, checks for a clean
completion (FSW + Director terminal markers present, no watchdog timeout),
tears everything down, and exits 0 (pass) / 1 (fail). This is the path
`make scenario SCENARIO=<name>` and CI use to confirm a scenario passes;
`make start` remains the manual path for a developer who wants the 42 GUI +
YAMCS open to investigate.

Pass/fail is "clean completion only" (see issue #21 decision): this script
does not assert anything about telemetry values, only that every
participant reached its terminal marker without crashing or timing out.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

import yaml

from shire_provenance import ROOT, run, git_head_sha
from shire_runner_lib import container_names, try_parse_marker

CFG_DIR = ROOT / "cfg"
ACTIVE_PATH = ROOT / "build" / "active.yaml"
GLOBAL_CONFIG = CFG_DIR / "shire-config.yaml"


def load_yaml(path: pathlib.Path) -> dict[str, object] | None:
    if not path.exists():
        return None
    return yaml.safe_load(path.read_text(encoding="utf-8"))


def load_scenario_cfg(mission: str, scenario_name: str) -> dict[str, object]:
    """Mirrors shire-orchestrator.py's mission/scenario config_file lookup,
    so this script finds the same scenario YAML the orchestrator will."""
    global_cfg = load_yaml(GLOBAL_CONFIG) or {}
    mission_entry = next(
        (m for m in global_cfg.get("build", {}).get("missions", [])
         if m["name"] == mission), None)
    if not mission_entry:
        raise RuntimeError(f"mission '{mission}' not found in {GLOBAL_CONFIG}")
    mission_cfg = load_yaml(CFG_DIR / mission_entry["config_file"]) or {}
    scenario_entry = next(
        (s for s in mission_cfg.get("scenarios", []) if s["name"] == scenario_name), None)
    if not scenario_entry:
        raise RuntimeError(f"scenario '{scenario_name}' not found for mission '{mission}'")
    scenario_cfg = load_yaml(CFG_DIR / scenario_entry["config_file"])
    if scenario_cfg is None:
        raise RuntimeError(f"could not load scenario config for '{scenario_name}'")
    return scenario_cfg


def update_active(mission: str | None, spacecraft: str | None, scenario: str) -> dict[str, object]:
    active = load_yaml(ACTIVE_PATH) or {}
    active["scenario"] = scenario
    if mission:
        active["mission"] = mission
    if spacecraft:
        active["spacecraft"] = spacecraft
    ACTIVE_PATH.parent.mkdir(parents=True, exist_ok=True)
    ACTIVE_PATH.write_text(yaml.safe_dump(active, sort_keys=False), encoding="utf-8")
    return active


def wait_for_completion(server_container: str, deadline_s: float) -> bool:
    """Blocks until `server_container` exits, or returns False if it hasn't
    by `deadline_s` seconds (a hung participant never lets Simulith's
    server container exit on its own)."""
    waiter = subprocess.Popen(["docker", "wait", server_container], cwd=ROOT,
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    started = time.monotonic()
    try:
        while waiter.poll() is None:
            if time.monotonic() - started > deadline_s:
                waiter.terminate()
                return False
            time.sleep(0.2)
    finally:
        if waiter.poll() is None:
            waiter.terminate()
    return True


def finish(result: dict[str, object], passed: bool, reason: str,
          report_dir: pathlib.Path) -> int:
    snapshot_src = (ROOT / "build" / str(result["mission"]) / "scenario" /
                    f"{result['scenario']}.snapshot.yaml")
    if snapshot_src.exists():
        shutil.copy2(snapshot_src, report_dir / snapshot_src.name)
        result["initial_conditions_snapshot"] = snapshot_src.name

    result["pass"] = passed
    result["reason"] = reason
    result_path = report_dir / "result.json"
    result_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"[scenario] {'PASS' if passed else 'FAIL'}: {reason}")
    print(f"[scenario] Report written to {result_path}")
    return 0 if passed else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", required=True,
                        help="Name of a scenario in cfg/drm/scenarios/*.yaml (scenario_name field)")
    parser.add_argument("--mission", help="Override build/active.yaml's mission")
    parser.add_argument("--spacecraft", help="Override build/active.yaml's spacecraft")
    parser.add_argument("--report-dir",
                        help="Where to write result.json + logs (default: "
                             "build/scenario-runs/<scenario>-<UTC timestamp>/)")
    args = parser.parse_args()

    timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    report_dir = pathlib.Path(args.report_dir).expanduser().resolve() if args.report_dir \
        else ROOT / "build" / "scenario-runs" / f"{args.scenario}-{timestamp}"
    report_dir.mkdir(parents=True, exist_ok=True)

    active = update_active(args.mission, args.spacecraft, args.scenario)
    mission = str(active.get("mission", "drm"))
    spacecraft = str(active.get("spacecraft", "sat-1"))

    result: dict[str, object] = {
        "scenario": args.scenario,
        "mission": mission,
        "spacecraft": spacecraft,
        "started_utc": timestamp,
        "git_sha": git_head_sha(),
    }

    print(f"[scenario] Configuring build for scenario={args.scenario} "
          f"mission={mission} spacecraft={spacecraft}")
    build = run(["make", "build"], check=False)
    (report_dir / "build.log").write_text(build.stdout, encoding="utf-8")
    if build.returncode != 0:
        return finish(result, False, "make build failed; see build.log", report_dir)

    scenario_cfg = load_scenario_cfg(mission, args.scenario)
    run_duration_s = float(scenario_cfg.get("run_duration_s", 900))
    director_command_scenario = scenario_cfg.get("director_command_scenario")
    result["run_duration_s"] = run_duration_s

    names = container_names(mission, spacecraft)
    compose = ROOT / "build" / mission / "shire-compose.yaml"
    compose_cmd = ["docker", "compose", "-f", str(compose)]

    trial_env = os.environ.copy()
    trial_env["SIMULITH_DURATION"] = str(run_duration_s)
    if director_command_scenario:
        trial_env["SIMULITH_SCENARIO_ENABLED"] = "1"
        trial_env["SIMULITH_SCENARIO_FILE"] = str(ROOT / director_command_scenario)

    run(compose_cmd + ["down", "--remove-orphans", "--timeout", "2"], check=False)

    passed = False
    reason = "unknown"
    try:
        up = run(compose_cmd + ["up", "-d"], env=trial_env, check=False)
        (report_dir / "compose-up.log").write_text(up.stdout, encoding="utf-8")
        if up.returncode != 0:
            reason = "docker compose up failed; see compose-up.log"
            return finish(result, passed, reason, report_dir)

        deadline_s = max(60.0, run_duration_s * 3.0 + 60.0)
        completed = wait_for_completion(names["server"], deadline_s)

        logs: dict[str, str] = {}
        for role, container in names.items():
            value = run(["docker", "logs", container], check=False).stdout
            logs[role] = value
            (report_dir / f"{container}.log").write_text(value, encoding="utf-8")

        if not completed:
            result["watchdog"] = {"deadline_s": deadline_s, "timed_out": True}
            reason = f"server container did not exit within {deadline_s:.0f}s watchdog deadline"
            return finish(result, passed, reason, report_dir)

        director_terminal = try_parse_marker(logs["director"], "SIMULITH_DIRECTOR_TERMINAL")
        fsw_terminal = try_parse_marker(logs["fsw"], "SIMULITH_FSW_TERMINAL")
        result["markers"] = {
            "director_terminal_present": director_terminal is not None,
            "fsw_terminal_present": fsw_terminal is not None,
        }

        fault_lines = [line for log in logs.values() for line in log.splitlines()
                       if " ERROR" in line or " CRITICAL" in line]
        result["fault_scan"] = {
            "heuristic": True,
            "note": "best-effort EVS ERROR/CRITICAL grep across container logs; "
                    "not a substitute for a dedicated fault-telemetry channel",
            "matches": fault_lines[:50],
            "match_count": len(fault_lines),
        }

        if director_terminal is None or fsw_terminal is None:
            missing = [name for name, present in
                       (("director", director_terminal is not None),
                        ("fsw", fsw_terminal is not None)) if not present]
            reason = "missing terminal marker(s): " + ", ".join(missing)
        else:
            passed = True
            reason = "clean completion (director + fsw terminal markers present)"
    finally:
        run(compose_cmd + ["down", "--remove-orphans", "--timeout", "2"], check=False)

    return finish(result, passed, reason, report_dir)


if __name__ == "__main__":
    sys.exit(main())

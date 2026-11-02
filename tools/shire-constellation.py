#!/usr/bin/env python3
"""Builds and runs a multi-vehicle constellation on one shared Simulith clock.

    python3 tools/shire-constellation.py --mission lunar-relay --constellation crosslink-demo [--scenario smoke]
        [--duration 60] [--speed max] [--no-build] [--report-dir DIR]

Each vehicle is built as a single vehicle run (build/active.yaml names the constellation and the vehicle, so
the orchestrator gives it its own node number, orbit and image tag), and then every vehicle's stack starts in one compose
project with a single Simulith server, so all vehicles tick together. build/active.yaml is restored afterwards.
"""
from __future__ import annotations

import argparse
import datetime as dt
import fcntl
import hashlib
import json
import pathlib
import subprocess
import sys

import yaml

from shire_config import ConfigError, load_mission
from shire_constellation_run import render_compose, vehicle_plan
from shire_provenance import ROOT, run
from shire_runner_lib import run_streaming, try_parse_marker

ACTIVE_PATH = ROOT / "build" / "active.yaml"
ACTIVE_LOCK_PATH = ROOT / "build" / ".shire-active.lock"
DEFAULT_TIMEOUT_S = 240
CAMPAIGN_FIELDS = ("instance", "port_offset", "image_tag", "initial_conditions_file")


def fail(message: str) -> int:
    print(f"[constellation] ERROR: {message}", file=sys.stderr)
    return 1


def select_for_build(active: dict, mission: str, constellation: str, vehicle: dict, scenario: str) -> dict:
    """The active selection that makes `make build` build one vehicle of the constellation."""
    selection = {k: v for k, v in active.items() if k not in CAMPAIGN_FIELDS}
    selection.update(mission=mission, constellation=constellation, vehicle=vehicle["id"],
                     spacecraft=vehicle["spacecraft"], scenario=scenario)
    return selection


def build_vehicles(mission: str, constellation: str, vehicles: list[dict], scenario: str, report_dir: pathlib.Path) -> str | None:
    """Builds every vehicle in turn. They share build directories, so builds are serialized and the previous
    active.yaml is put back whatever happens. Returns an error message, or None."""
    ACTIVE_LOCK_PATH.parent.mkdir(parents=True, exist_ok=True)
    original = ACTIVE_PATH.read_text(encoding="utf-8") if ACTIVE_PATH.exists() else None
    with open(ACTIVE_LOCK_PATH, "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            for vehicle in vehicles:
                print(f"[constellation] Building {vehicle['id']} ({vehicle['spacecraft']}, node {vehicle['node_id']})", flush=True)
                active = yaml.safe_load(original) if original else {}
                ACTIVE_PATH.write_text(yaml.safe_dump(
                    select_for_build(active or {}, mission, constellation, vehicle, scenario), sort_keys=True),
                    encoding="utf-8")
                code, output = run_streaming(["make", "build"])
                (report_dir / f"build-{vehicle['id']}.log").write_text(output, encoding="utf-8")
                if code != 0:
                    return f"build of vehicle '{vehicle['id']}' failed; see build-{vehicle['id']}.log"
        finally:
            if original is None:
                ACTIVE_PATH.unlink(missing_ok=True)
            else:
                ACTIVE_PATH.write_text(original, encoding="utf-8")
            fcntl.flock(lock, fcntl.LOCK_UN)
    return None


def container_log(name: str) -> str:
    return run(["docker", "logs", name], check=False).stdout


def run_stack(compose: pathlib.Path, vehicles: list[dict], constellation: str, env: dict, timeout_s: float) -> tuple[bool, int | None]:
    """Starts the stack, waits for the server to finish, and returns (finished in time, server exit code)."""
    server = f"shire-server-{constellation}"
    up = run(["docker", "compose", "-f", str(compose), "up", "-d"], env=env, check=False)
    if up.returncode != 0:
        print(up.stdout)
        return False, None
    try:
        waited = subprocess.run(["docker", "wait", server], capture_output=True, text=True, timeout=timeout_s)
        return True, int(waited.stdout.strip()) if waited.stdout.strip().lstrip("-").isdigit() else None
    except subprocess.TimeoutExpired:
        return False, None


def collect(report_dir: pathlib.Path, vehicles: list[dict], constellation: str) -> dict[str, str]:
    logs = {f"shire-server-{constellation}": container_log(f"shire-server-{constellation}")}
    for v in vehicles:
        for role in ("42", "gsw", "cryptolib", "director", "fsw"):
            logs[f"shire-{role}-{v['id']}"] = container_log(f"shire-{role}-{v['id']}")
    for name, text in logs.items():
        (report_dir / f"{name}.log").write_text(text, encoding="utf-8")
    return logs


def evaluate(vehicles: list[dict], constellation: str, logs: dict[str, str], finished: bool, exit_code: int | None,
             trace_root: pathlib.Path) -> dict:
    """Checks the evidence that every vehicle ran, and that they ran on one clock."""
    server_log = logs[f"shire-server-{constellation}"]
    report = {"vehicles": {}, "problems": []}
    if not finished:
        report["problems"].append(f"the server did not finish within the deadline")
    elif exit_code != 0:
        report["problems"].append(f"the server exited with status {exit_code}")
    ticks = set()
    for v in vehicles:
        director = try_parse_marker(logs[f"shire-director-{v['id']}"], "SIMULITH_DIRECTOR_TERMINAL")
        fsw = try_parse_marker(logs[f"shire-fsw-{v['id']}"], "SIMULITH_FSW_TERMINAL")
        traces = sorted(pathlib.Path(trace_root, v["id"]).rglob("control-trace.v1"))
        entry = {
            "node_id": v["node_id"],
            "client_ids_seen_by_server": all(f"shire-{role}-{v['id']}" in server_log for role in ("fsw", "director")),
            "director_terminal": director is not None,
            "fsw_terminal": fsw is not None,
            "prepare_count": (director or {}).get("prepare_count"),
            "commit_count": (director or {}).get("commit_count"),
            "stalls": logs[f"shire-fsw-{v['id']}"].count("stalled"),
            "control_trace_sha256": hashlib.sha256(traces[-1].read_bytes()).hexdigest() if traces else None,
        }
        report["vehicles"][v["id"]] = entry
        ticks.add(entry["prepare_count"])
        for key, text in (("director_terminal", "no director terminal marker"), ("fsw_terminal", "no flight software terminal marker"),
                          ("client_ids_seen_by_server", "the server never saw both of its clients")):
            if not entry[key]:
                report["problems"].append(f"{v['id']}: {text}")
        if entry["stalls"]:
            report["problems"].append(f"{v['id']}: {entry['stalls']} PSP stall message(s)")
    if len(ticks) > 1:
        report["problems"].append(f"vehicles ran different numbers of ticks: {sorted(map(str, ticks))}")
    report["shared_clock_ticks"] = ticks.pop() if len(ticks) == 1 else None
    report["pass"] = not report["problems"]
    return report


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mission", default="lunar-relay")
    parser.add_argument("--constellation", required=True)
    parser.add_argument("--scenario", default="smoke", help="scenario whose component settings apply to every vehicle")
    parser.add_argument("--duration", type=float, default=60.0, help="simulated seconds to run (default 60)")
    parser.add_argument("--speed", default="max", help="'max' or a multiplier (default max)")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_S, help="seconds to wait for the run to finish")
    parser.add_argument("--no-build", action="store_true", help="reuse images already built for each vehicle")
    parser.add_argument("--report-dir", help="where to write logs and result.json")
    args = parser.parse_args(argv)

    try:
        mission = load_mission(args.mission)
        stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        report_dir = pathlib.Path(args.report_dir).resolve() if args.report_dir else (
            ROOT / "build" / "constellation-runs" / f"{args.constellation}-{stamp}")
        trace_root = report_dir / "traces"
        constellation, vehicles = vehicle_plan(mission, args.constellation, args.scenario, trace_root)
    except ConfigError as exc:
        return fail(str(exc))
    report_dir.mkdir(parents=True, exist_ok=True)
    for v in vehicles:
        pathlib.Path(v["trace_dir"]).mkdir(parents=True, exist_ok=True)

    compose = report_dir / "shire-compose.yaml"
    compose.write_text(render_compose(args.mission, args.constellation, vehicles), encoding="utf-8")
    print(f"[constellation] {args.constellation}: {len(vehicles)} vehicle(s) {[v['id'] for v in vehicles]}; compose at {compose}", flush=True)

    if not args.no_build:
        problem = build_vehicles(args.mission, args.constellation, vehicles, args.scenario, report_dir)
        if problem:
            (report_dir / "result.json").write_text(json.dumps({"pass": False, "problems": [problem]}, indent=2))
            return fail(problem)

    env = {**__import__("os").environ, "SIMULITH_SPEED": "max" if str(args.speed) in ("max", "0", "0.0") else str(args.speed),
           "SIMULITH_DURATION": str(args.duration)}
    finished, exit_code = False, None
    try:
        finished, exit_code = run_stack(compose, vehicles, args.constellation, env, args.timeout)
        logs = collect(report_dir, vehicles, args.constellation)
    finally:
        run(["docker", "compose", "-f", str(compose), "down", "-v", "--remove-orphans"], env=env, check=False)

    report = evaluate(vehicles, args.constellation, logs, finished, exit_code, trace_root)
    report.update(mission=args.mission, constellation=args.constellation, scenario=args.scenario,
                  duration_s=args.duration, speed=args.speed, git_sha=run(["git", "rev-parse", "HEAD"], check=False).stdout.strip())
    (report_dir / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for problem in report["problems"]:
        print(f"[constellation] FAIL: {problem}", file=sys.stderr)
    print(f"[constellation] {'PASS' if report['pass'] else 'FAIL'}: {len(vehicles)} vehicle(s) on one clock"
          f" ({report['shared_clock_ticks']} ticks each). Report: {report_dir / 'result.json'}")
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())

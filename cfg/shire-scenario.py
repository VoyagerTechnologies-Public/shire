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

Pass/fail is "clean completion only". This script
does not assert anything about telemetry values, only that every
participant reached its terminal marker without crashing or timing out.
"""

from __future__ import annotations

import argparse
import datetime as dt
import fcntl
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

import yaml

from shire_provenance import ROOT, run, git_head_sha
from shire_runner_lib import container_names, run_streaming, try_parse_marker

SIMULATED_TIME_RE = re.compile(r"Simulation time:\s*([0-9.]+)\s*seconds")

# How often (real seconds) the wait/streaming helpers below print a
# heartbeat. Long silent gaps are exactly what prompted these: a run
# taking several minutes with no output looks indistinguishable from a
# hang, so progress -- including simulated time, since that's the
# clearest "how far along is this" signal available -- gets printed
# periodically instead of only at the very end.
HEARTBEAT_INTERVAL_S = 10.0

CFG_DIR = ROOT / "cfg"
ACTIVE_PATH = ROOT / "build" / "active.yaml"
ACTIVE_LOCK_PATH = ROOT / "build" / ".shire-active.lock"
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


def list_scenario_names(mission: str) -> list[str]:
    """Every scenario name registered for `mission` in its mission YAML
    (e.g. cfg/drm/drm.yaml's `scenarios:` list) -- the same list
    load_scenario_cfg() looks a name up against, so this is always exactly
    what --scenario will accept."""
    global_cfg = load_yaml(GLOBAL_CONFIG) or {}
    mission_entry = next(
        (m for m in global_cfg.get("build", {}).get("missions", [])
         if m["name"] == mission), None)
    if not mission_entry:
        return []
    mission_cfg = load_yaml(CFG_DIR / mission_entry["config_file"]) or {}
    return [s["name"] for s in mission_cfg.get("scenarios", [])]


INSTANCE_RE = re.compile(r"^[a-z0-9]{1,8}$")


def update_active(mission: str | None, spacecraft: str | None, scenario: str, *,
                  instance: str | None = None, port_offset: int | None = None,
                  image_tag: str | None = None,
                  initial_conditions_file: str | None = None) -> dict[str, object]:
    active = load_yaml(ACTIVE_PATH) or {}
    active["scenario"] = scenario
    if mission:
        active["mission"] = mission
    if spacecraft:
        active["spacecraft"] = spacecraft
    # Monte Carlo campaign instance-scoping fields. These must
    # always be set explicitly -- including clearing -- rather than only
    # added when given: active.yaml persists between invocations, so a
    # plain `make scenario` call with none of these flags must not inherit
    # a stale instance/port_offset/image_tag/initial_conditions_file left
    # behind by an earlier campaign trial run on the same checkout.
    for key, value in (("instance", instance), ("port_offset", port_offset),
                       ("image_tag", image_tag),
                       ("initial_conditions_file", initial_conditions_file)):
        if value is None:
            active.pop(key, None)
        else:
            active[key] = value
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
    last_report_s = 0.0
    try:
        while waiter.poll() is None:
            elapsed_s = time.monotonic() - started
            if elapsed_s > deadline_s:
                waiter.terminate()
                return False
            if elapsed_s - last_report_s >= HEARTBEAT_INTERVAL_S:
                current = read_latest_simulated_time(server_container)
                current_str = f"{current:.1f}s" if current is not None else "unknown"
                print(f"[scenario] waiting for the run to finish: "
                      f"simulated time {current_str}, {elapsed_s:.0f}s elapsed "
                      f"(watchdog at {deadline_s:.0f}s)...", flush=True)
                last_report_s = elapsed_s
            time.sleep(0.2)
    finally:
        if waiter.poll() is None:
            waiter.terminate()
    return True


def read_latest_simulated_time(server_container: str) -> float | None:
    """Reads the most recent simulated-time value the server container has
    logged. simulith_server.c prints "  Simulation time: %.3f seconds |
    ..." every 10 simulated seconds (tied to simulated-time progress, not
    wall clock) -- polling this avoids adding a YAMCS/telemetry dependency
    just to answer "how far into the run are we."""
    log = run(["docker", "logs", "--tail", "20", server_container], check=False).stdout
    matches = SIMULATED_TIME_RE.findall(log)
    return float(matches[-1]) if matches else None


# FSW app boot/subscribe time is a real-world constant (cFE apps need a
# fixed number of real seconds to start and subscribe to their command
# MIDs), not tied to simulated ticks at all. Confirmed live at
# simulith_speed=5: an early CheckoutTest.ycs verify step failed
# (CMDCOUNTER read back nonzero) because waiting for one simulated-time
# log sample -- simulith_server.c logs every 10 *simulated* seconds --
# only bought ~2 real seconds of settle time at 5x, versus ~10 at 1x.
# This floor is real seconds, deliberately not scaled by speed, so it
# keeps protecting the same boot race regardless of how fast a scenario
# runs.
FSW_BOOT_SETTLE_S = 15.0


def wait_for_simulated_time(server_container: str, target_s: float, deadline_s: float,
                            min_real_settle_s: float = 0.0) -> bool:
    """Blocks until the run has reached `target_s` simulated seconds AND
    at least `min_real_settle_s` real seconds have elapsed, or returns
    False if `deadline_s` wall-clock seconds elapse first (the run ended,
    or is far behind, before reaching that point).

    Deliberately does NOT special-case target_s <= 0 to return instantly:
    a stack fired the moment containers come up can race FSW apps that
    haven't finished subscribing to their command MIDs yet (confirmed live
    -- a command sent too early is silently dropped by the software bus,
    no error of any kind). Waiting for the *first* logged simulated-time
    sample already gives every participant a full boot/subscribe window at
    1x speed, and target_s <= 0 is satisfied by that first sample
    regardless of its value -- but at higher speed that same sample
    arrives after fewer real seconds, so `min_real_settle_s` (see
    FSW_BOOT_SETTLE_S) adds a speed-independent floor on top."""
    started = time.monotonic()
    last_report_s = 0.0
    while time.monotonic() - started < deadline_s:
        current = read_latest_simulated_time(server_container)
        elapsed_real_s = time.monotonic() - started
        if current is not None and current >= target_s and elapsed_real_s >= min_real_settle_s:
            return True
        if elapsed_real_s - last_report_s >= HEARTBEAT_INTERVAL_S:
            current_str = f"{current:.1f}s" if current is not None else "no sample yet"
            settle_note = (f", {min_real_settle_s - elapsed_real_s:.0f}s of boot settle left"
                          if min_real_settle_s > elapsed_real_s else "")
            print(f"[scenario] waiting for scheduled simulated time {target_s:.0f}s "
                  f"(currently {current_str}{settle_note})...", flush=True)
            last_report_s = elapsed_real_s
        time.sleep(1.0)
    return False


def run_scheduled_verify_stacks(verify_stacks: list[dict[str, object]], server_container: str,
                                deadline_s: float, report_dir: pathlib.Path,
                                port_offset: int = 0) -> dict[str, object]:
    """Runs each {stack, at_s} entry in ascending at_s order, waiting for
    the run to reach each one's simulated time before firing it. Every
    entry is attempted regardless of earlier failures (same "run to
    completion, record everything" policy yamcs_commander.py's stack
    runner already uses within a single stack).

    `deadline_s` bounds the *whole* schedule (all entries combined), not
    each one separately: it's tracked from a single start time so a slow
    or stuck stack can't push later entries -- or the overall run -- past
    it. Without this, a stack's own step timeouts have no outer bound:
    e.g. running well past the point the simulated clock stops (telemetry
    freezes at SIMULITH_DURATION, see wait_for_simulated_time's docstring)
    would otherwise make every remaining verify step burn its full
    timeout against stale data, one after another, for a very long time."""
    started = time.monotonic()
    ordered = sorted(verify_stacks, key=lambda entry: float(entry.get("at_s", 0)))
    stack_results: list[dict[str, object]] = []
    for index, entry in enumerate(ordered):
        stack_path = entry["stack"]
        at_s = float(entry.get("at_s", 0))
        stem = pathlib.Path(stack_path).stem
        remaining_s = deadline_s - (time.monotonic() - started)
        min_real_settle_s = FSW_BOOT_SETTLE_S if index == 0 else 0.0
        reached = remaining_s > 0 and wait_for_simulated_time(
            server_container, at_s, remaining_s, min_real_settle_s)
        if not reached:
            stack_results.append({
                "stack": stack_path, "at_s": at_s, "passed": False,
                "step_count": 0, "steps": [],
                "failures": [{"index": -1, "type": "schedule", "name": None, "status": "failed",
                              "detail": f"never reached scheduled at_s={at_s} "
                                        f"within the {deadline_s:.0f}s verification budget"}],
            })
            continue
        report_path = report_dir / f"verify-{stem}.json"
        remaining_s = deadline_s - (time.monotonic() - started)
        yamcs_url = f"http://localhost:{8090 + port_offset}"
        print(f"[scenario] running verify stack: {stack_path} "
              f"(scheduled at simulated t={at_s:.0f}s)...", flush=True)
        try:
            # --yamcs-url must reflect this run's port_offset -- without
            # it, every campaign trial's verification would silently hit
            # port 8090 regardless of offset (either verifying nothing, or
            # worse, verifying a *different* trial's YAMCS instance).
            returncode, stdout = run_streaming(
                [sys.executable, str(ROOT / "yamcs" / "yamcs_commander.py"),
                 "--stack", str(ROOT / stack_path), "--report", str(report_path),
                 "--yamcs-url", yamcs_url],
                timeout=max(1.0, remaining_s))
        except subprocess.TimeoutExpired as e:
            stdout, returncode = (e.output or ""), None
        (report_dir / f"verify-{stem}.log").write_text(stdout, encoding="utf-8")
        if report_path.exists():
            stack_result = json.loads(report_path.read_text(encoding="utf-8"))
        elif returncode is None:
            stack_result = {
                "stack": stack_path, "step_count": 0, "steps": [], "passed": False,
                "failures": [{"index": -1, "type": "runner", "name": None, "status": "failed",
                              "detail": f"yamcs_commander.py --stack exceeded the "
                                        f"{deadline_s:.0f}s verification budget and was killed; "
                                        f"see verify-{stem}.log"}],
            }
        else:
            stack_result = {
                "stack": stack_path, "step_count": 0, "steps": [], "passed": False,
                "failures": [{"index": -1, "type": "runner", "name": None, "status": "failed",
                              "detail": f"yamcs_commander.py --stack exited {returncode} "
                                        f"before writing a report; see verify-{stem}.log"}],
            }
        stack_result["at_s"] = at_s
        stack_results.append(stack_result)
    failures = [dict(f, stack=r["stack"]) for r in stack_results for f in r["failures"]]
    return {"passed": all(r["passed"] for r in stack_results),
            "stacks": stack_results, "failures": failures}


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
    parser.add_argument("--scenario",
                        help="Name of a scenario in cfg/drm/scenarios/*.yaml (scenario_name field). "
                             "Required unless --list-scenarios is given.")
    parser.add_argument("--list-scenarios", action="store_true",
                        help="Print every scenario name registered for --mission "
                             "(default: build/active.yaml's current mission) and exit.")
    parser.add_argument("--mission", help="Override build/active.yaml's mission")
    parser.add_argument("--spacecraft", help="Override build/active.yaml's spacecraft")
    parser.add_argument("--report-dir",
                        help="Where to write result.json + logs (default: "
                             "build/scenario-runs/<scenario>-<UTC timestamp>/)")
    parser.add_argument("--instance-id",
                        help="Monte Carlo campaign trial token, e.g. a zero-padded "
                             "trial index. Namespaces this run's container/network/volume names "
                             "so it can run concurrently with other instances. Must match "
                             "^[a-z0-9]{1,8}$ (also used as a DNS hostname on the bridge network).")
    parser.add_argument("--port-offset", type=int, default=None,
                        help="Added to this run's published host ports (8090 YAMCS, 5801 42 VNC) "
                             "so concurrent instances don't collide. Default: 0.")
    parser.add_argument("--image-tag", help="Explicit image tag to run (e.g. a campaign build-key "
                                            "tag) instead of the bare spacecraft tag.")
    parser.add_argument("--initial-conditions-file",
                        help="Absolute path to a generated IC bin yaml to use instead of the "
                             "scenario's named initial_conditions bin.")
    parser.add_argument("--no-build", action="store_true",
                        help="Skip `make build`; run `make cfg-compose-only` instead. Assumes an "
                             "image matching --image-tag was already built (see cfg/shire-campaign.py).")
    args = parser.parse_args()

    if args.list_scenarios:
        mission = args.mission or str((load_yaml(ACTIVE_PATH) or {}).get("mission", "drm"))
        names = list_scenario_names(mission)
        if not names:
            print(f"\t[scenario] no scenarios registered for mission '{mission}'", file=sys.stderr)
            return 1
        for name in names:
            print("\t" + name)
        return 0

    if not args.scenario:
        print("[scenario] ERROR: --scenario is required (or use --list-scenarios)", file=sys.stderr)
        return 2

    if args.instance_id and not INSTANCE_RE.match(args.instance_id):
        print(f"[scenario] ERROR: --instance-id {args.instance_id!r} must match "
              f"^[a-z0-9]{{1,8}}$ (it's also used as a container-name/DNS-hostname suffix)",
              file=sys.stderr)
        return 2

    timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    report_dir = pathlib.Path(args.report_dir).expanduser().resolve() if args.report_dir \
        else ROOT / "build" / "scenario-runs" / f"{args.scenario}-{timestamp}"
    report_dir.mkdir(parents=True, exist_ok=True)

    port_offset = args.port_offset or 0

    result: dict[str, object] = {
        "scenario": args.scenario,
        "mission": None,
        "spacecraft": None,
        "started_utc": timestamp,
        "git_sha": git_head_sha(),
    }

    # build/active.yaml is shared, global, mutable state: update_active()'s
    # write and the orchestrator's read of it (inside `make build` /
    # `make cfg-compose-only`) are two separate steps with no atomicity
    # between them. Without a lock, two shire-scenario.py invocations
    # running concurrently (e.g. two Monte Carlo campaign trials) can
    # interleave -- one process's write, then the other's -- so the first
    # process's `make` call ends up reading the *second* process's
    # instance/port_offset/image_tag and renders the wrong compose file.
    # Held only across the write-then-render step; nothing after this
    # block touches active.yaml, so the lock is not held for the run's
    # actual (multi-minute) duration.
    ACTIVE_LOCK_PATH.parent.mkdir(parents=True, exist_ok=True)
    with open(ACTIVE_LOCK_PATH, "w") as lock_file:
        fcntl.flock(lock_file, fcntl.LOCK_EX)
        try:
            active = update_active(args.mission, args.spacecraft, args.scenario,
                                   instance=args.instance_id, port_offset=args.port_offset,
                                   image_tag=args.image_tag,
                                   initial_conditions_file=args.initial_conditions_file)
            mission = str(active.get("mission", "drm"))
            spacecraft = str(active.get("spacecraft", "sat-1"))
            result["mission"] = mission
            result["spacecraft"] = spacecraft

            print(f"[scenario] Configuring build for scenario={args.scenario} "
                  f"mission={mission} spacecraft={spacecraft}"
                  + (f" instance={args.instance_id} port_offset={port_offset}" if args.instance_id else ""))
            # --no-build assumes a Monte Carlo campaign trial
            # (cfg/shire-campaign.py) already ran a full `make build` for
            # this trial's --image-tag; only the per-instance compose file
            # needs (re-)rendering here.
            build_target = "cfg-compose-only" if args.no_build else "build"
            build_returncode, build_stdout = run_streaming(["make", build_target])
        finally:
            fcntl.flock(lock_file, fcntl.LOCK_UN)

    (report_dir / "build.log").write_text(build_stdout, encoding="utf-8")
    if build_returncode != 0:
        return finish(result, False, f"make {build_target} failed; see build.log", report_dir)

    scenario_cfg = load_scenario_cfg(mission, args.scenario)
    run_duration_s = float(scenario_cfg.get("run_duration_s", 900))
    simulith_speed = scenario_cfg.get("simulith_speed")
    director_command_scenario = scenario_cfg.get("director_command_scenario")
    result["run_duration_s"] = run_duration_s

    names = container_names(mission, spacecraft, instance=args.instance_id)
    compose_dir = ROOT / "build" / mission / args.instance_id if args.instance_id \
        else ROOT / "build" / mission
    compose = compose_dir / "shire-compose.yaml"
    compose_cmd = ["docker", "compose", "-f", str(compose)]

    trial_env = os.environ.copy()
    trial_env["SIMULITH_DURATION"] = str(run_duration_s)
    if simulith_speed is not None:
        # Only safe to raise unconditionally for scenarios with no
        # verify_stacks: for those, run_duration_s just bounds how much
        # simulated-time coverage this run exercises, so running it
        # faster in wall-clock time changes nothing else. A verify_stacks
        # scenario would need run_duration_s scaled up to match (so the
        # simulated clock doesn't outrun verification and freeze
        # telemetry mid-stack, see wait_for_simulated_time's docstring) --
        # confirmed empirically, not something to set blindly.
        trial_env["SIMULITH_SPEED"] = str(simulith_speed)
    if director_command_scenario:
        trial_env["SIMULITH_SCENARIO_ENABLED"] = "1"
        trial_env["SIMULITH_SCENARIO_FILE"] = str(ROOT / director_command_scenario)

    # Instance-scoped volumes (simulith_ipc_*/gsw-data_* with an -<instance>
    # suffix) are 100% ephemeral per trial, unlike the plain single-run
    # path where they may deliberately persist across successive manual
    # `make scenario` calls -- so campaign trials also drop them with -v,
    # or a long campaign leaks one set of named volumes per trial.
    down_cmd = compose_cmd + ["down", "--remove-orphans", "--timeout", "2"]
    if args.instance_id:
        down_cmd = down_cmd + ["-v"]
    run(down_cmd, check=False)

    passed = False
    reason = "unknown"
    try:
        up = run(compose_cmd + ["up", "-d"], env=trial_env, check=False)
        (report_dir / "compose-up.log").write_text(up.stdout, encoding="utf-8")
        if up.returncode != 0:
            reason = "docker compose up failed; see compose-up.log"
            return finish(result, passed, reason, report_dir)

        deadline_s = max(60.0, run_duration_s * 3.0 + 60.0)

        verify_stacks = scenario_cfg.get("verify_stacks", [])
        if verify_stacks:
            result["verification"] = run_scheduled_verify_stacks(
                verify_stacks, names["server"], deadline_s, report_dir, port_offset=port_offset)

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
            verification = result.get("verification")
            if verification is not None and not verification["passed"]:
                passed = False
                first = verification["failures"][0]
                reason = (f"clean completion, but stack verification failed "
                          f"({len(verification['failures'])} failing step(s)): "
                          f"stack {first['stack']!r} step {first['index']} "
                          f"({first['type']} {first.get('name')}): "
                          f"{first.get('detail') or first.get('actual')}")
    finally:
        run(down_cmd, check=False)

    return finish(result, passed, reason, report_dir)


if __name__ == "__main__":
    sys.exit(main())

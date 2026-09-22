#!/usr/bin/env python3
"""Monte Carlo campaign runner.

Usage:
    python3 cfg/shire-campaign.py --campaign <name> [--max-parallel N] [--report-dir DIR]
    python3 cfg/shire-campaign.py --from-report <campaign_report.json> [--jsonl-out <path>]

Samples a campaign's swept parameters into N perturbed Initial Condition
(IC) bins (see cfg/drm/initial_conditions/*.yaml for the schema), builds
whatever distinct image content those bins require, runs every trial's
scenario via cfg/shire-scenario.py (in parallel, bounded by
--max-parallel), and aggregates the results into campaign_report.json and
campaign_dataset.jsonl under the run's report directory.

Sampling is deterministic (one random.Random(seed), every trial sampled
up front, single-threaded, before any trial runs) and reuses
cfg/shire-scenario.py's tested single-run pipeline unmodified for the
actual scenario execution -- this script only adds parameter sampling,
per-trial image-build deduplication, bounded-concurrency dispatch, and
result aggregation on top of it.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import copy
import datetime as dt
import fcntl
import json
import pathlib
import random
import signal
import statistics
import subprocess
import sys
import threading

import yaml

from shire_provenance import ROOT, run, git_metadata, host_metadata
from shire_runner_lib import compute_build_key, find_free_port_offset, resolve_component_configs, run_streaming

CFG_DIR = ROOT / "cfg"
GLOBAL_CONFIG = CFG_DIR / "shire-config.yaml"
CAMPAIGNS_DIR = CFG_DIR / "drm" / "campaigns"
IC_DIR = CFG_DIR / "drm" / "initial_conditions"
ACTIVE_PATH = ROOT / "build" / "active.yaml"

# Same lock cfg/shire-scenario.py holds around its own active.yaml write +
# make build/cfg-compose-only step. The two scripts can't share a literal
# Python constant (shire-scenario.py's hyphenated filename isn't
# importable as a module), so this path must be kept identical by hand --
# it protects the exact same shared active.yaml / build-time file races
# either script's active.yaml write + `make` call could cause.
ACTIVE_LOCK_PATH = ROOT / "build" / ".shire-active.lock"

BASE_PORTS = [8090, 5801]
PORT_STRIDE = 10
DEFAULT_MAX_PARALLEL = 2
# Rough per-trial compose-stack footprint (server 4 + director 4 +
# cryptolib 1 + fsw 4 + 42 2 + gsw 2 cpu-units; ~2+0.5+2+2+1+1GB mem),
# printed at startup so raising --max-parallel is a conscious choice, not
# a surprise once the host starts thrashing.
PER_TRIAL_CPU_UNITS = 17.0
PER_TRIAL_MEM_GB = 8.5

DEFAULT_MODE_BY_DISTRIBUTION = {"uniform": "additive", "normal": "additive", "choice": "absolute"}

INSTANCE_LOCK = threading.Lock()
ASSIGNED_INSTANCES: set[str] = set()
# Set by _signal_cleanup on SIGINT/SIGTERM. A worker thread checks this
# before launching its trial's subprocess -- without it, a SIGINT only
# cleans up whatever was already running at that instant; the thread pool
# keeps pulling *queued* trials off its work queue regardless (confirmed
# live: two more trials started, each spinning up a fresh, never-cleaned
# set of containers, after the "interrupted" message had already
# printed), so new work kept starting well after the user asked to stop.
ABORT_EVENT = threading.Event()


def fail(msg: str) -> None:
    print(f"[campaign] ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def load_yaml(path: pathlib.Path) -> dict | None:
    if not path.exists():
        return None
    return yaml.safe_load(path.read_text(encoding="utf-8"))


def reset_active_campaign_fields() -> None:
    """Clears instance/port_offset/image_tag/initial_conditions_file from
    active.yaml, leaving mission/spacecraft/scenario as they last were.

    Every shire-scenario.py invocation explicitly sets-or-clears these
    same four fields on its own (see its update_active()), so a plain
    `make scenario` never inherits stale campaign state. But nothing
    equivalent runs after a whole campaign finishes: shire-campaign.py's
    build phase writes them directly (not through shire-scenario.py), and
    the very last trial's own shire-scenario.py invocation is the last
    thing to touch active.yaml, leaving its instance/port_offset/
    image_tag/initial_conditions_file sitting there afterward. Without
    this, the next plain `make build`/`make start` a developer runs
    -- with no campaign flags at all -- would silently tag images with a
    stale campaign build key, override the IC with a stale generated
    file, and (if `instance` was still set) render shire-compose.yaml
    into a numbered subdirectory `make start` never looks at, leaving it
    pointed at a stale, unrelated compose file. Called on every exit path
    (normal completion, an unhandled exception, and SIGINT/SIGTERM), not
    just the happy path."""
    active = load_yaml(ACTIVE_PATH)
    if active is None:
        return
    changed = False
    for key in ("instance", "port_offset", "image_tag", "initial_conditions_file"):
        if key in active:
            del active[key]
            changed = True
    if changed:
        ACTIVE_PATH.write_text(yaml.safe_dump(active, sort_keys=False), encoding="utf-8")


# --------------------------------------------------------------------------
# Config loading -- mirrors shire-orchestrator.py's / shire-scenario.py's
# mission/scenario/spacecraft config_file lookup, standalone, so build-key
# hashing can run without Docker or the orchestrator's own Jinja/file-write
# side effects.
# --------------------------------------------------------------------------

def load_configs(mission: str, spacecraft: str, scenario_name: str) -> tuple[dict, dict, dict, dict]:
    global_cfg = load_yaml(GLOBAL_CONFIG) or {}
    mission_entry = next((m for m in global_cfg.get("build", {}).get("missions", [])
                          if m["name"] == mission), None)
    if not mission_entry:
        fail(f"mission '{mission}' not found in {GLOBAL_CONFIG}")
    mission_cfg = load_yaml(CFG_DIR / mission_entry["config_file"]) or {}

    spacecraft_cfg: dict = {}
    spacecraft_entry = next((sc for sc in mission_cfg.get("spacecraft", [])
                             if sc["name"] == spacecraft), None)
    if spacecraft_entry:
        spacecraft_cfg = load_yaml(CFG_DIR / spacecraft_entry["config_file"]) or {}

    scenario_entry = next((s for s in mission_cfg.get("scenarios", [])
                           if s["name"] == scenario_name), None)
    if not scenario_entry:
        fail(f"scenario '{scenario_name}' not found for mission '{mission}'")
    scenario_cfg = load_yaml(CFG_DIR / scenario_entry["config_file"])
    if scenario_cfg is None:
        fail(f"could not load scenario config for '{scenario_name}'")

    return global_cfg, mission_cfg, spacecraft_cfg, scenario_cfg


# --------------------------------------------------------------------------
# Dotted-path get/set. A numeric segment indexes into a list (never
# auto-extended -- the IC schema's vectors are fixed-size, so an
# out-of-range index is a campaign-authoring error); a non-numeric segment
# indexes into a dict, creating missing intermediate dicts as needed (so
# e.g. "component_overrides.eps.battery_initial_soc" can create
# component_overrides["eps"] = {} the first time, since every shipped IC
# bin's component_overrides is {}).
# --------------------------------------------------------------------------

def get_path(root: dict, path: str) -> object:
    node = root
    for seg in path.split("."):
        if node is None:
            # A missing intermediate key (e.g. every shipped IC bin's
            # component_overrides: {} has no "eps" key yet) is a normal,
            # valid "not set" result, not an error -- short-circuit rather
            # than falling into the scalar-descent branch below.
            return None
        if isinstance(node, list):
            try:
                node = node[int(seg)]
            except (ValueError, IndexError):
                raise ValueError(f"path {path!r}: list index {seg!r} out of range")
        elif isinstance(node, dict):
            node = node.get(seg)
        else:
            raise ValueError(f"path {path!r}: cannot descend into a scalar at segment {seg!r}")
    return node


def set_path(root: dict, path: str, value: object) -> None:
    segs = path.split(".")
    node = root
    for seg in segs[:-1]:
        if isinstance(node, list):
            try:
                node = node[int(seg)]
            except (ValueError, IndexError):
                raise ValueError(f"path {path!r}: list index {seg!r} out of range")
        else:
            if seg not in node or node[seg] is None:
                node[seg] = {}
            node = node[seg]
    last = segs[-1]
    if isinstance(node, list):
        try:
            node[int(last)] = value
        except (ValueError, IndexError):
            raise ValueError(f"path {path!r}: list index {last!r} out of range")
    else:
        node[last] = value


# --------------------------------------------------------------------------
# Sampling. All trials are sampled up front, single-threaded, in trial
# order 0..N-1, parameters within a trial sampled in the YAML's listed
# order, before any trial executes -- required for reproducibility: if
# sampling were interleaved with concurrent execution, draw order (and
# therefore the values a given seed produces) would depend on
# scheduler/Docker timing instead of the campaign's own seed.
# --------------------------------------------------------------------------

def resolve_parameter_mode(parameter: dict) -> str:
    mode = parameter.get("mode")
    if mode:
        return mode
    return DEFAULT_MODE_BY_DISTRIBUTION.get(parameter["distribution"]["type"], "absolute")


def validate_campaign_parameters(campaign_cfg: dict, base_ic: dict) -> None:
    """Fails fast, before any sampling/building, if a parameter's mode or
    path can't work against the base IC -- e.g. `additive` against a path
    that isn't already a number in base_ic."""
    for parameter in campaign_cfg.get("parameters", []):
        path = parameter["path"]
        mode = resolve_parameter_mode(parameter)
        try:
            base_value = get_path(base_ic, path)
        except ValueError as exc:
            fail(f"campaign parameter path {path!r}: {exc}")
            return
        if mode == "additive" and not isinstance(base_value, (int, float)):
            fail(f"campaign parameter path {path!r} has mode 'additive' but the base "
                 f"initial_conditions bin's value at that path is {base_value!r}, not a number")


RESERVED_OUTCOME_FIELDS = {"campaign", "git_sha", "trial_index", "pass", "reason",
                          "run_duration_s", "verification_passed", "fault_scan_match_count"}


def validate_campaign_metrics(campaign_cfg: dict) -> None:
    """Fails fast if a metrics: entry is malformed, or its name collides
    with a fixed outcome field or a swept parameter's own dotted path --
    both flatten into the same campaign_dataset.jsonl row, so a collision
    would silently clobber one column with the other."""
    parameter_paths = {p["path"] for p in campaign_cfg.get("parameters", [])}
    seen_names: set[str] = set()
    for metric in campaign_cfg.get("metrics", []):
        for required in ("name", "stack", "field"):
            if required not in metric:
                fail(f"campaign metric {metric!r} is missing required field {required!r}")
        name = metric["name"]
        if name in RESERVED_OUTCOME_FIELDS:
            fail(f"campaign metric name {name!r} collides with a fixed outcome field")
        if name in parameter_paths:
            fail(f"campaign metric name {name!r} collides with a swept parameter's path")
        if name in seen_names:
            fail(f"campaign metric name {name!r} is used more than once")
        seen_names.add(name)
        has_index = "step_index" in metric
        has_name = "step_name" in metric
        if has_index == has_name:
            fail(f"campaign metric {name!r} must set exactly one of step_index or step_name")


def _draw(rng: random.Random, distribution: dict) -> object:
    kind = distribution["type"]
    if kind == "uniform":
        return rng.uniform(distribution["low"], distribution["high"])
    if kind == "normal":
        value = rng.gauss(distribution["mean"], distribution["stdev"])
        if "min" in distribution:
            value = max(value, distribution["min"])
        if "max" in distribution:
            value = min(value, distribution["max"])
        return value
    if kind == "choice":
        values = distribution["values"]
        weights = distribution.get("weights")
        return rng.choices(values, weights=weights, k=1)[0] if weights else rng.choice(values)
    raise ValueError(f"unknown distribution type {kind!r}")


def sample_trials(campaign_cfg: dict, base_ic: dict) -> list[tuple[dict, list[dict]]]:
    rng = random.Random(campaign_cfg["seed"])
    trials: list[tuple[dict, list[dict]]] = []
    for _ in range(campaign_cfg["trial_count"]):
        resolved_ic = copy.deepcopy(base_ic)
        detail: list[dict] = []
        for parameter in campaign_cfg.get("parameters", []):
            path = parameter["path"]
            distribution = parameter["distribution"]
            mode = resolve_parameter_mode(parameter)
            sample = _draw(rng, distribution)
            base_value = get_path(base_ic, path)
            value = (base_value + sample) if mode == "additive" else sample
            set_path(resolved_ic, path, value)
            detail.append({"path": path, "distribution": distribution, "mode": mode,
                           "base_value": base_value, "sample": sample, "resolved_value": value})
        trials.append((resolved_ic, detail))
    return trials


def trial_token(index: int, width: int) -> str:
    return str(index).zfill(width)


def write_trial_files(report_dir: pathlib.Path, token: str, campaign_name: str, seed: int,
                      base_ic_name: str, resolved_ic: dict, detail: list[dict],
                      index: int) -> tuple[pathlib.Path, pathlib.Path]:
    ic_out = dict(resolved_ic)
    ic_out["name"] = f"{campaign_name}-trial-{token}"
    ic_out["description"] = (
        f"Generated by cfg/shire-campaign.py for campaign {campaign_name!r} trial {index}, "
        f"perturbed from initial_conditions bin {base_ic_name!r}. Not hand-authored -- "
        f"edit the campaign YAML instead of this file.")
    ic_path = report_dir / "ic" / f"trial-{token}.yaml"
    ic_path.write_text(yaml.safe_dump(ic_out, sort_keys=False), encoding="utf-8")

    params = {"trial_index": index, "campaign": campaign_name, "seed": seed,
             "base_initial_conditions": base_ic_name, "parameters": detail}
    params_path = report_dir / "ic" / f"trial-{token}.params.json"
    params_path.write_text(json.dumps(params, indent=2, default=str) + "\n", encoding="utf-8")
    return ic_path, params_path


def compute_trial_build_key(global_cfg: dict, mission_cfg: dict, spacecraft_cfg: dict,
                            scenario_cfg: dict, resolved_ic: dict) -> str:
    resolved_components = resolve_component_configs(global_cfg, mission_cfg, spacecraft_cfg,
                                                     scenario_cfg, resolved_ic)
    return compute_build_key(resolved_ic, resolved_components)


# --------------------------------------------------------------------------
# Build phase: dedup by content-addressed key, lock-serialized (the same
# lock cfg/shire-scenario.py holds -- comp/*/shared/device_cfg.h and
# build/<mission>/42_config/*.txt are fixed, non-instance-scoped paths
# that two differing concurrent builds would race on).
# --------------------------------------------------------------------------

def build_unique_keys(mission: str, spacecraft: str, scenario_name: str,
                      trials_meta: list[dict]) -> dict[str, bool]:
    seen: dict[str, pathlib.Path] = {}
    order: list[str] = []
    for trial in trials_meta:
        key = trial["build_key"]
        if key not in seen:
            seen[key] = trial["ic_path"]
            order.append(key)

    print(f"[campaign] {len(order)} unique build key(s) for {len(trials_meta)} trial(s)", flush=True)
    results: dict[str, bool] = {}
    for key in order:
        image_tag = f"{spacecraft}-mc-{key}"
        ic_path = seen[key]
        print(f"[campaign] building image_tag={image_tag} (from {ic_path.name})...", flush=True)
        ACTIVE_LOCK_PATH.parent.mkdir(parents=True, exist_ok=True)
        with open(ACTIVE_LOCK_PATH, "w") as lock_file:
            fcntl.flock(lock_file, fcntl.LOCK_EX)
            try:
                active = load_yaml(ACTIVE_PATH) or {}
                active["mission"] = mission
                active["spacecraft"] = spacecraft
                active["scenario"] = scenario_name
                active["image_tag"] = image_tag
                active["initial_conditions_file"] = str(ic_path)
                active.pop("instance", None)
                active.pop("port_offset", None)
                ACTIVE_PATH.write_text(yaml.safe_dump(active, sort_keys=False), encoding="utf-8")
                returncode, _ = run_streaming(["make", "build"])
            finally:
                fcntl.flock(lock_file, fcntl.LOCK_UN)
        results[key] = (returncode == 0)
        if returncode != 0:
            print(f"[campaign] WARNING: build failed for image_tag={image_tag}; every trial "
                 f"sharing this build key will be recorded as errored, not run", file=sys.stderr)
    return results


def extract_metrics(trial_dir: pathlib.Path, metrics_cfg: list[dict]) -> dict[str, object]:
    """Pulls a named value out of a trial's own verify-<stack>.json (the
    per-step results cfg/shire-scenario.py's verify_stacks already writes,
    via yamcs_commander.py's run_stack()) into the campaign's aggregated
    output -- e.g. a verify step's "elapsed_s" as a convergence-time
    metric, such as time to reach a Sun-pointed attitude after commanding
    SUNSAFE. Resolves every configured metric to None, rather than
    raising, when the trial has no verification at all (no verify_stacks,
    a build failure, a crashed trial) -- a metric is exploratory data on
    top of pass/fail, not a second pass/fail gate."""
    values: dict[str, object] = {}
    cache: dict[str, dict | None] = {}
    for metric in metrics_cfg:
        name = metric["name"]
        stem = metric["stack"]
        if stem not in cache:
            stack_path = trial_dir / f"verify-{stem}.json"
            cache[stem] = (json.loads(stack_path.read_text(encoding="utf-8"))
                           if stack_path.exists() else None)
        stack_result = cache[stem]
        if stack_result is None:
            values[name] = None
            continue
        steps = stack_result.get("steps", [])
        step = None
        if "step_index" in metric:
            index = metric["step_index"]
            if 0 <= index < len(steps):
                step = steps[index]
        else:
            step = next((s for s in steps if s.get("name") == metric["step_name"]), None)
        values[name] = step.get(metric["field"]) if step is not None else None
    return values


# --------------------------------------------------------------------------
# Run phase: bounded concurrency, one subprocess per trial via
# cfg/shire-scenario.py's own --instance-id/--port-offset/--image-tag/
# --no-build/--initial-conditions-file flags (Part B), reusing its tested
# wait/marker/verify_stacks/fault-scan/result.json/teardown logic
# unmodified rather than duplicating it here.
# --------------------------------------------------------------------------

def cleanup_by_label(label_value: str | None) -> None:
    """Removes every container/network/volume carrying a shire.instance
    label. `label_value=None` matches ANY value -- used for the
    unconditional startup sweep that clears a prior crashed run's
    orphans; a specific token is used for this run's own SIGINT/atexit
    cleanup."""
    label_filter = "label=shire.instance" + (f"={label_value}" if label_value else "")
    ids = run(["docker", "ps", "-aq", "--filter", label_filter], check=False).stdout.split()
    if ids:
        run(["docker", "rm", "-f", *ids], check=False)
    net_ids = run(["docker", "network", "ls", "-q", "--filter", label_filter], check=False).stdout.split()
    if net_ids:
        run(["docker", "network", "rm", *net_ids], check=False)
    vol_names = run(["docker", "volume", "ls", "-q", "--filter", label_filter], check=False).stdout.split()
    if vol_names:
        run(["docker", "volume", "rm", "-f", *vol_names], check=False)


def _signal_cleanup(signum: int, frame: object) -> None:
    # Set first, before anything else: every worker thread checks this at
    # the top of run_trial(), so setting it immediately stops any
    # not-yet-started queued trial from launching a new subprocess/
    # containers, no matter how long the cleanup sweep below takes.
    ABORT_EVENT.set()
    with INSTANCE_LOCK:
        tokens = list(ASSIGNED_INSTANCES)
    print(f"\n[campaign] interrupted (signal {signum}); cleaning up {len(tokens)} "
         f"in-flight instance(s)... no further trials will start", flush=True)
    for token in tokens:
        cleanup_by_label(token)
    reset_active_campaign_fields()
    sys.exit(130)


def run_trial(index: int, token: str, port_offset: int, mission: str, spacecraft: str,
             scenario_name: str, image_tag: str, ic_path: pathlib.Path,
             report_dir: pathlib.Path, metrics_cfg: list[dict]) -> dict:
    if ABORT_EVENT.is_set():
        return {"trial_index": index, "pass": None, "reason": None, "run_duration_s": None,
               "verification_passed": None, "fault_scan_match_count": None,
               "metrics": {m["name"]: None for m in metrics_cfg},
               "error": "aborted before starting (campaign was interrupted)"}

    trial_dir = report_dir / "trials" / f"trial-{token}"
    trial_dir.mkdir(parents=True, exist_ok=True)
    with INSTANCE_LOCK:
        ASSIGNED_INSTANCES.add(token)

    print(f"[campaign] trial {index} (instance={token}, port_offset={port_offset}) starting...", flush=True)
    cmd = [sys.executable, str(CFG_DIR / "shire-scenario.py"),
          "--scenario", scenario_name, "--mission", mission, "--spacecraft", spacecraft,
          "--instance-id", token, "--port-offset", str(port_offset),
          "--image-tag", image_tag, "--no-build",
          "--initial-conditions-file", str(ic_path), "--report-dir", str(trial_dir)]
    log_path = trial_dir / "campaign-trial.log"
    try:
        proc = subprocess.run(cmd, cwd=ROOT, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log_path.write_text(proc.stdout, encoding="utf-8")
    except Exception as exc:
        log_path.write_text(f"trial subprocess failed to launch: {exc}\n", encoding="utf-8")
        print(f"[campaign] trial {index} (instance={token}) errored: {exc}", flush=True)
        return {"trial_index": index, "pass": None, "reason": None, "run_duration_s": None,
               "verification_passed": None, "fault_scan_match_count": None,
               "metrics": {m["name"]: None for m in metrics_cfg}, "error": str(exc)}
    finally:
        with INSTANCE_LOCK:
            ASSIGNED_INSTANCES.discard(token)

    result_path = trial_dir / "result.json"
    if not result_path.exists():
        detail = (f"trial subprocess exited {proc.returncode} without writing result.json; "
                  f"see {log_path}")
        print(f"[campaign] trial {index} (instance={token}) errored: {detail}", flush=True)
        return {"trial_index": index, "pass": None, "reason": None, "run_duration_s": None,
               "verification_passed": None, "fault_scan_match_count": None,
               "metrics": {m["name"]: None for m in metrics_cfg}, "error": detail}

    result = json.loads(result_path.read_text(encoding="utf-8"))
    outcome = {
        "trial_index": index, "pass": result.get("pass"), "reason": result.get("reason"),
        "run_duration_s": result.get("run_duration_s"),
        "verification_passed": (result.get("verification") or {}).get("passed"),
        "fault_scan_match_count": (result.get("fault_scan") or {}).get("match_count"),
        "metrics": extract_metrics(trial_dir, metrics_cfg),
        "error": None,
    }
    print(f"[campaign] trial {index} (instance={token}) finished: pass={outcome['pass']}, "
         f"metrics={outcome['metrics']}", flush=True)
    return outcome


# --------------------------------------------------------------------------
# Aggregation and JSONL export.
# --------------------------------------------------------------------------

def _numeric_stats(values: list[object]) -> dict | None:
    numeric = [v for v in values if isinstance(v, (int, float))]
    if not numeric:
        return None
    return {"min": min(numeric), "max": max(numeric),
           "mean": statistics.mean(numeric),
           "stdev": statistics.stdev(numeric) if len(numeric) > 1 else 0.0}


def summarize(trials: list[dict], campaign_cfg: dict) -> dict:
    completed = [t for t in trials if t.get("error") is None]
    errored = [t for t in trials if t.get("error") is not None]
    passed = [t for t in completed if t.get("pass") is True]
    failed = [t for t in completed if t.get("pass") is False]

    reason_hist: dict[str, int] = {}
    for t in failed:
        reason = t.get("reason") or "unknown"
        reason_hist[reason] = reason_hist.get(reason, 0) + 1

    summary = {
        "trial_count": len(trials),
        "completed_count": len(completed),
        "errored_count": len(errored),
        "pass_count": len(passed),
        "fail_count": len(failed),
        "pass_rate": (len(passed) / len(completed)) if completed else None,
        "failure_reason_histogram": reason_hist,
    }
    duration_stats = _numeric_stats([t.get("run_duration_s") for t in completed])
    if duration_stats:
        summary["run_duration_s"] = duration_stats

    param_stats = {}
    for parameter in campaign_cfg.get("parameters", []):
        path = parameter["path"]
        values = [t["parameters"].get(path) for t in trials if "parameters" in t]
        stats = _numeric_stats(values)
        if stats:
            param_stats[path] = stats
    if param_stats:
        summary["parameters"] = param_stats

    metric_stats = {}
    for metric in campaign_cfg.get("metrics", []):
        name = metric["name"]
        values = [t["metrics"].get(name) for t in trials if "metrics" in t]
        stats = _numeric_stats(values)
        if stats:
            metric_stats[name] = stats
    if metric_stats:
        summary["metrics"] = metric_stats
    return summary


def export_jsonl(trials: list[dict], campaign_name: str, git_sha: str, out_path: pathlib.Path) -> None:
    lines = []
    for t in trials:
        row = {"campaign": campaign_name, "git_sha": git_sha, "trial_index": t["trial_index"]}
        row.update(t.get("parameters", {}))
        for key in ("pass", "reason", "run_duration_s", "verification_passed", "fault_scan_match_count"):
            row[key] = t.get(key)
        row.update(t.get("metrics", {}))
        lines.append(json.dumps(row, default=str))
    out_path.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--campaign", help="Name of a campaign in cfg/drm/campaigns/*.yaml (name field)")
    parser.add_argument("--list-campaigns", action="store_true",
                        help="Print every campaign name available under cfg/drm/campaigns/ and exit.")
    parser.add_argument("--mission", default="drm", help="Mission to run under (default: drm)")
    parser.add_argument("--spacecraft", default="sat-1", help="Spacecraft to run under (default: sat-1)")
    parser.add_argument("--max-parallel", type=int, default=None,
                        help="Max trials to run concurrently. Overrides the campaign YAML's "
                             f"max_parallel; default {DEFAULT_MAX_PARALLEL} if neither is set.")
    parser.add_argument("--seed", type=int, help="Override the campaign YAML's seed (ad hoc use; "
                                                 "breaks the YAML-is-the-source-of-truth story)")
    parser.add_argument("--report-dir", help="Where to write campaign_report.json/"
                                             "campaign_dataset.jsonl and every trial's artifacts "
                                             "(default: build/monte-carlo-runs/<campaign>-<UTC timestamp>/)")
    parser.add_argument("--from-report", help="Re-export campaign_dataset.jsonl from an existing "
                                              "campaign_report.json, without running anything")
    parser.add_argument("--jsonl-out", help="Output path for --from-report (default: alongside the report)")
    args = parser.parse_args()

    if args.list_campaigns:
        names = sorted(p.stem for p in CAMPAIGNS_DIR.glob("*.yaml"))
        if not names:
            print(f"\t[campaign] no campaigns found under {CAMPAIGNS_DIR}", file=sys.stderr)
            return 1
        for name in names:
            print("\t" + name)
        return 0

    if args.from_report:
        report_path = pathlib.Path(args.from_report).expanduser().resolve()
        report = json.loads(report_path.read_text(encoding="utf-8"))
        out_path = pathlib.Path(args.jsonl_out).expanduser().resolve() if args.jsonl_out \
            else report_path.parent / "campaign_dataset.jsonl"
        export_jsonl(report["trials"], report["campaign"]["name"], report["git"]["root"], out_path)
        print(f"[campaign] Re-exported {out_path}")
        return 0

    if not args.campaign:
        fail("--campaign is required (or use --from-report)")
        return 2

    campaign_cfg = load_yaml(CAMPAIGNS_DIR / f"{args.campaign}.yaml")
    if campaign_cfg is None:
        fail(f"campaign '{args.campaign}' not found at {CAMPAIGNS_DIR}")
        return 2
    if args.seed is not None:
        campaign_cfg["seed"] = args.seed

    max_parallel = args.max_parallel or campaign_cfg.get("max_parallel") or DEFAULT_MAX_PARALLEL

    timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    report_dir = pathlib.Path(args.report_dir).expanduser().resolve() if args.report_dir \
        else ROOT / "build" / "monte-carlo-runs" / f"{args.campaign}-{timestamp}"
    (report_dir / "ic").mkdir(parents=True, exist_ok=True)
    (report_dir / "trials").mkdir(parents=True, exist_ok=True)

    scenario_name = campaign_cfg["scenario"]
    global_cfg, mission_cfg, spacecraft_cfg, scenario_cfg = load_configs(
        args.mission, args.spacecraft, scenario_name)
    base_ic_name = campaign_cfg["base_initial_conditions"]
    base_ic = load_yaml(IC_DIR / f"{base_ic_name}.yaml")
    if base_ic is None:
        fail(f"base_initial_conditions '{base_ic_name}' not found at {IC_DIR}")
        return 2
    validate_campaign_parameters(campaign_cfg, base_ic)
    validate_campaign_metrics(campaign_cfg)
    metrics_cfg = campaign_cfg.get("metrics", [])

    trial_count = campaign_cfg["trial_count"]
    print(f"[campaign] {args.campaign}: {trial_count} trial(s), seed={campaign_cfg['seed']}, "
         f"max_parallel={max_parallel} (~{max_parallel * PER_TRIAL_CPU_UNITS:.0f} cpu-units / "
         f"~{max_parallel * PER_TRIAL_MEM_GB:.0f}GB reserved concurrently)", flush=True)

    cleanup_by_label(None)

    print("[campaign] Phase 1/4: sampling parameters...", flush=True)
    sampled = sample_trials(campaign_cfg, base_ic)
    width = max(4, len(str(trial_count - 1)))

    trials_meta = []
    for index, (resolved_ic, detail) in enumerate(sampled):
        token = trial_token(index, width)
        ic_path, _ = write_trial_files(report_dir, token, args.campaign, campaign_cfg["seed"],
                                       base_ic_name, resolved_ic, detail, index)
        build_key = compute_trial_build_key(global_cfg, mission_cfg, spacecraft_cfg,
                                            scenario_cfg, resolved_ic)
        trials_meta.append({
            "trial_index": index, "token": token, "ic_path": ic_path, "build_key": build_key,
            "parameters": {d["path"]: d["resolved_value"] for d in detail},
        })

    # Wrapped in try/finally from here on: build_unique_keys() and every
    # trial subprocess write campaign-scoped fields (instance/port_offset/
    # image_tag/initial_conditions_file) into the shared active.yaml, and
    # nothing else ever clears them afterward. Without this, the *next*
    # plain `make build`/`make start` a developer runs -- no campaign
    # flags at all -- silently inherits this run's leftover build tag and
    # IC override. Runs on normal completion and on any unhandled
    # exception; SIGINT/SIGTERM is handled separately in
    # _signal_cleanup(), since that path exits before this function
    # returns at all.
    try:
        print("[campaign] Phase 2/4: building images...", flush=True)
        build_results = build_unique_keys(args.mission, args.spacecraft, scenario_name, trials_meta)

        print(f"[campaign] Phase 3/4: running trials (max_parallel={max_parallel})...", flush=True)
        signal.signal(signal.SIGINT, _signal_cleanup)
        signal.signal(signal.SIGTERM, _signal_cleanup)

        trial_results: list[dict | None] = [None] * trial_count
        with concurrent.futures.ThreadPoolExecutor(max_workers=max_parallel) as pool:
            futures = {}
            for trial in trials_meta:
                index = trial["trial_index"]
                build_key = trial["build_key"]
                common = {
                    "parameters": trial["parameters"],
                    "ic_file": str(trial["ic_path"].relative_to(report_dir)),
                    "report_dir": f"trials/trial-{trial['token']}",
                }
                if not build_results.get(build_key, False):
                    trial_results[index] = {
                        "trial_index": index, "pass": None, "reason": None, "run_duration_s": None,
                        "verification_passed": None, "fault_scan_match_count": None,
                        "metrics": {m["name"]: None for m in metrics_cfg},
                        "error": f"image build for build_key={build_key} failed; trial not run",
                        **common,
                    }
                    continue
                image_tag = f"{args.spacecraft}-mc-{build_key}"
                port_offset = find_free_port_offset(BASE_PORTS, index * PORT_STRIDE)
                future = pool.submit(run_trial, index, trial["token"], port_offset,
                                     args.mission, args.spacecraft, scenario_name, image_tag,
                                     trial["ic_path"], report_dir, metrics_cfg)
                futures[future] = (trial, common)

            done, _ = concurrent.futures.wait(list(futures.keys()),
                                              return_when=concurrent.futures.ALL_COMPLETED)
            for future in done:
                trial, common = futures[future]
                index = trial["trial_index"]
                try:
                    outcome = future.result()
                except Exception as exc:
                    outcome = {"trial_index": index, "pass": None, "reason": None,
                              "run_duration_s": None, "verification_passed": None,
                              "fault_scan_match_count": None,
                              "metrics": {m["name"]: None for m in metrics_cfg}, "error": str(exc)}
                outcome.update(common)
                trial_results[index] = outcome

        print("[campaign] Phase 4/4: aggregating...", flush=True)
        trials_final = [t if t is not None else
                        {"trial_index": i, "pass": None, "reason": None, "run_duration_s": None,
                         "verification_passed": None, "fault_scan_match_count": None,
                         "metrics": {m["name"]: None for m in metrics_cfg},
                         "error": "not scheduled", "parameters": {}}
                        for i, t in enumerate(trial_results)]

        summary = summarize(trials_final, campaign_cfg)
        git = git_metadata()
        campaign_report = {
            "schema_version": 1,
            "created_utc": timestamp,
            "git": git,
            "host": host_metadata(),
            "mission": args.mission,
            "spacecraft": args.spacecraft,
            "campaign": {
                "name": campaign_cfg["name"], "scenario": scenario_name,
                "base_initial_conditions": base_ic_name,
                "trial_count": trial_count, "seed": campaign_cfg["seed"],
                "parameters": campaign_cfg.get("parameters", []),
                "metrics": metrics_cfg,
            },
            "trials": trials_final,
            "summary": summary,
        }
        report_path = report_dir / "campaign_report.json"
        report_path.write_text(json.dumps(campaign_report, indent=2, sort_keys=True, default=str) + "\n",
                               encoding="utf-8")
        jsonl_path = report_dir / "campaign_dataset.jsonl"
        export_jsonl(trials_final, campaign_cfg["name"], git["root"], jsonl_path)

        print(f"[campaign] {summary['pass_count']}/{summary['completed_count']} trial(s) passed "
             f"({summary['errored_count']} errored)", flush=True)
        print(f"[campaign] Report written to {report_path}")
        print(f"[campaign] Dataset written to {jsonl_path}")
        return 0 if summary["errored_count"] == 0 and summary["fail_count"] == 0 else 1
    finally:
        reset_active_campaign_fields()


if __name__ == "__main__":
    sys.exit(main())

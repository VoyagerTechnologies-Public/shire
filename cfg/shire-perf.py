#!/usr/bin/env python3
"""Repeatable, noninteractive full-stack Simulith performance runner."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import os
import pathlib
import re
import statistics
import subprocess
import threading
import time
from collections.abc import Callable

import yaml

from shire_provenance import ROOT, run, git_metadata, host_metadata
from shire_runner_lib import container_names, parse_marker


def active_value(key: str) -> str:
    for line in (ROOT / "build" / "active.yaml").read_text(encoding="utf-8").splitlines():
        if line.startswith(f"{key}:"):
            return line.split(":", 1)[1].strip()
    raise RuntimeError(f"missing {key} in build/active.yaml")


def parse_stats(output: str, containers: tuple[str, ...]) -> list[dict[str, str]]:
    samples: list[dict[str, str]] = []
    for line in output.splitlines():
        start = line.find("{")
        end = line.rfind("}")
        if start < 0 or end < start:
            continue
        try:
            item = json.loads(line[start:end + 1])
        except json.JSONDecodeError:
            continue
        if item.get("Name") in containers:
            samples.append(item)
    return samples


def read_continuous_stats(process: subprocess.Popen[str],
                          containers: tuple[str, ...], started: float,
                          samples: list[dict[str, object]],
                          raw_lines: list[str]) -> None:
    """Timestamp each Docker sample as it arrives from the streaming command."""
    if process.stdout is None:
        return
    for line in process.stdout:
        raw_lines.append(line)
        for item in parse_stats(line, containers):
            samples.append({
                "scope": "runtime",
                "wall_offset_s": time.monotonic() - started,
                "captured_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
                "container": item,
            })


def collect_startup_stats(containers: tuple[str, ...]) -> list[dict[str, str]]:
    """Take one resource snapshot during excluded startup/warmup time."""
    result = run(["docker", "stats", "--no-stream", "--format", "{{json .}}",
                  *containers], check=False)
    return parse_stats(result.stdout, containers)


def scheduler_metadata(containers: tuple[str, ...]) -> dict[str, object]:
    """Capture effective Linux scheduling policy/priority for every container thread."""
    result: dict[str, object] = {}
    for container in containers:
        inspected = run(["docker", "inspect", "--format", "{{.State.Pid}}", container],
                        check=False).stdout.strip()
        if not inspected.isdigit() or inspected == "0":
            result[container] = {"error": "container pid unavailable"}
            continue
        process_dir = pathlib.Path("/proc") / inspected / "task"
        threads: list[dict[str, object]] = []
        try:
            task_dirs = sorted(process_dir.iterdir(), key=lambda path: int(path.name))
        except OSError as error:
            result[container] = {"pid": int(inspected), "error": str(error)}
            continue
        for task_dir in task_dirs:
            try:
                stat = (task_dir / "stat").read_text(encoding="utf-8").split()
                name = (task_dir / "comm").read_text(encoding="utf-8").strip()
                schedstat = (task_dir / "schedstat").read_text(
                    encoding="utf-8").split()
                status_fields: dict[str, str] = {}
                for line in (task_dir / "status").read_text(
                        encoding="utf-8").splitlines():
                    if ":" in line:
                        key, value = line.split(":", 1)
                        status_fields[key] = value.strip()
                sched_fields: dict[str, str] = {}
                for line in (task_dir / "sched").read_text(
                        encoding="utf-8").splitlines():
                    if ":" in line:
                        key, value = line.split(":", 1)
                        sched_fields[key.strip()] = value.strip()
                try:
                    wchan = (task_dir / "wchan").read_text(
                        encoding="utf-8").strip()
                except OSError:
                    wchan = "unavailable"
                threads.append({
                    "tid": int(task_dir.name), "name": name,
                    "policy": int(stat[40]), "nice": int(stat[18]),
                    "rt_priority": int(stat[39]),
                    "runtime_ns": int(schedstat[0]),
                    "runqueue_wait_ns": int(schedstat[1]),
                    "timeslices": int(schedstat[2]),
                    "voluntary_context_switches": int(
                        status_fields.get("voluntary_ctxt_switches", "0")),
                    "nonvoluntary_context_switches": int(
                        status_fields.get("nonvoluntary_ctxt_switches", "0")),
                    "migrations": int(float(
                        sched_fields.get("se.nr_migrations", "0"))),
                    "wchan": wchan,
                })
            except (OSError, ValueError, IndexError):
                continue
        result[container] = {"pid": int(inspected), "threads": threads}
    return result


def sha256_file(path: pathlib.Path) -> str | None:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError:
        return None


def cmake_cache_metadata(path: pathlib.Path) -> dict[str, object]:
    values: dict[str, str] = {}
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as error:
        return {"path": str(path), "error": str(error)}
    wanted = {
        "CMAKE_BUILD_TYPE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER",
        "CMAKE_C_FLAGS", "CMAKE_C_FLAGS_DEBUG", "CMAKE_C_FLAGS_RELEASE",
        "CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_DEBUG", "CMAKE_CXX_FLAGS_RELEASE",
    }
    for line in lines:
        if line.startswith("//") or line.startswith("#") or "=" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        key = key_and_type.split(":", 1)[0]
        if key in wanted:
            values[key] = value
    return {"path": str(path.relative_to(ROOT)), "sha256": sha256_file(path),
            "values": values}


def compile_commands_metadata(path: pathlib.Path) -> dict[str, object]:
    try:
        commands = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return {"path": str(path), "error": str(error)}
    flag_sets: set[tuple[str, ...]] = set()
    compilers: set[str] = set()
    for entry in commands:
        arguments = entry.get("arguments")
        if not isinstance(arguments, list):
            import shlex
            arguments = shlex.split(str(entry.get("command", "")))
        if arguments:
            compilers.add(str(arguments[0]))
        flags = tuple(sorted(str(value) for value in arguments[1:]
                             if str(value).startswith(("-O", "-g", "-D", "-f", "-m"))))
        flag_sets.add(flags)
    return {"path": str(path.relative_to(ROOT)), "sha256": sha256_file(path),
            "translation_units": len(commands), "compilers": sorted(compilers),
            "compiler_flag_sets": [list(flags) for flags in sorted(flag_sets)]}


def generated_flags_metadata(build_dir: pathlib.Path) -> dict[str, object]:
    values: dict[str, set[str]] = {}
    files = sorted(build_dir.rglob("flags.make")) if build_dir.exists() else []
    for path in files:
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
        for line in lines:
            if " = " not in line:
                continue
            key, value = line.split(" = ", 1)
            if key in {"C_FLAGS", "C_DEFINES", "CXX_FLAGS", "CXX_DEFINES",
                       "ASM_FLAGS", "ASM_DEFINES"}:
                values.setdefault(key, set()).add(value)
    return {"path": str(build_dir.relative_to(ROOT)), "files": len(files),
            "values": {key: sorted(items) for key, items in sorted(values.items())}}


def image_metadata(compose: pathlib.Path) -> list[dict[str, object]]:
    references = run(["docker", "compose", "-f", str(compose), "config", "--images"],
                     check=False).stdout.splitlines()
    images: list[dict[str, object]] = []
    for reference in sorted(set(value.strip() for value in references if value.strip())):
        inspected = run(["docker", "image", "inspect", reference], check=False)
        if inspected.returncode != 0:
            images.append({"reference": reference, "error": inspected.stdout.strip()})
            continue
        value = json.loads(inspected.stdout)[0]
        images.append({"reference": reference, "id": value.get("Id"),
                       "repo_digests": value.get("RepoDigests", []),
                       "created": value.get("Created")})
    return images


def build_metadata(mission: str, spacecraft: str,
                   compose: pathlib.Path) -> dict[str, object]:
    base = ROOT / "build" / mission / spacecraft
    caches = [path for path in (base / "sim" / "CMakeCache.txt",
                                base / "fsw" / "CMakeCache.txt") if path.exists()]
    compile_databases = [path for path in (
        base / "sim" / "compile_commands.json",
        base / "fsw" / "compile_commands.json") if path.exists()]
    recipe_paths = (ROOT / "cfg" / "shire-build.py",
                    ROOT / "cfg" / "Dockerfile.42", ROOT / "42" / "Makefile")
    return {
        "cmake_caches": [cmake_cache_metadata(path) for path in caches],
        "compile_commands": [compile_commands_metadata(path)
                             for path in compile_databases],
        "generated_target_flags": [generated_flags_metadata(path)
                                   for path in (base / "sim", base / "fsw")],
        "images": image_metadata(compose),
        "build_recipes": [{"path": str(path.relative_to(ROOT)),
                           "sha256": sha256_file(path)} for path in recipe_paths],
    }


def scenario_delivery_counts(scenario: dict[str, object],
                             fsw: dict[str, object]) -> tuple[list[dict[str, int]],
                                                              list[dict[str, int]]]:
    expected_by_mid: dict[int, int] = {}
    for command in scenario.get("commands", []):
        packet_hex = str(command.get("packet_hex", ""))
        if len(packet_hex) >= 4:
            mid = int(packet_hex[:4], 16)
            expected_by_mid[mid] = expected_by_mid.get(mid, 0) + int(
                command.get("expected_deliveries", 1))
    observed_by_mid = {int(item["mid"]): int(item["count"])
                       for item in fsw.get("command_deliveries", [])}
    expected = [{"mid": mid, "count": count}
                for mid, count in sorted(expected_by_mid.items())]
    observed = [{"mid": mid, "count": observed_by_mid.get(mid, 0)}
                for mid in sorted(expected_by_mid)]
    return expected, observed


def scenario_acceptance_counts(scenario: dict[str, object],
                               fsw_log: str) -> tuple[list[dict[str, object]],
                                                      list[dict[str, object]]]:
    """Count scenario-declared application success events in the cFE EVS log.

    Software Bus delivery proves that a command reached an application pipe,
    but not that the application accepted it.  Each performance command names
    the existing success event emitted after its application/device work has
    completed, keeping this assertion external to flight application code.
    """
    expected_by_event: dict[tuple[str, int], int] = {}
    for command in scenario.get("commands", []):
        if "acceptance_app" not in command and \
           "acceptance_event_id" not in command:
            continue
        app = str(command.get("acceptance_app", ""))
        event_id = int(command.get("acceptance_event_id", -1))
        count = int(command.get("acceptance_count", 1))
        if not app or event_id < 0 or count <= 0:
            raise RuntimeError("invalid scenario acceptance event fields")
        key = (app, event_id)
        expected_by_event[key] = expected_by_event.get(key, 0) + count

    expected: list[dict[str, object]] = []
    observed: list[dict[str, object]] = []
    for (app, event_id), count in sorted(expected_by_event.items()):
        # A cFE EVS console record contains processor/spacecraft/app followed by
        # the numeric event ID.  Anchor all separators so an app or event with a
        # shared prefix cannot satisfy the assertion.
        pattern = re.compile(
            rf"(?:^|\s)\d+/\d+/{re.escape(app)}\s+{event_id}:", re.MULTILINE)
        expected.append({"app": app, "event_id": event_id, "count": count})
        observed.append({"app": app, "event_id": event_id,
                         "count": len(pattern.findall(fsw_log))})
    return expected, observed


def classify_command_deliveries(fsw: dict[str, object]) -> None:
    """Separate deterministic command work from wall-clock output wakeups."""
    ground_mid = int(fsw.get("ground_output_mid", 0))
    deliveries = list(fsw.get("command_deliveries", []))
    fsw["deterministic_command_deliveries"] = [
        item for item in deliveries if int(item.get("mid", -1)) != ground_mid]
    fsw["wall_clock_command_deliveries"] = [
        item for item in deliveries if int(item.get("mid", -1)) == ground_mid]


def initial_conditions_snapshot(mission: str, scenario_name: str) -> dict[str, object] | None:
    """Read the scenario+IC snapshot shire-orchestrator.py wrote for the
    currently-configured build, so a determinism (or any other) report
    stays traceable to the exact starting state a run used."""
    path = ROOT / "build" / mission / "scenario" / f"{scenario_name}.snapshot.yaml"
    if not path.exists():
        return None
    return yaml.safe_load(path.read_text(encoding="utf-8"))


def resolved_scenario(artifact_dir: pathlib.Path, spacecraft: str) -> pathlib.Path:
    source = json.loads((ROOT / "cfg" / "perf-scenario.json").read_text(encoding="utf-8"))
    for command in source.get("commands", []):
        command["host"] = f"shire-fsw-{spacecraft}"
    target = artifact_dir / "scenario.json"
    target.write_text(json.dumps(source, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return target


def one_trial(compose: pathlib.Path, artifact_dir: pathlib.Path,
              speed: str, duration: float, warmup: float,
              trial_number: int, resource_snapshot: bool,
              names: dict[str, str], scenario: pathlib.Path) -> dict[str, object]:
    label = f"{speed.replace('.', '_')}x-trial-{trial_number}"
    trial_dir = artifact_dir / label
    trial_dir.mkdir(parents=True)
    compose_cmd = ["docker", "compose", "-f", str(compose)]
    run(compose_cmd + ["down", "--remove-orphans", "--timeout", "2"], check=False)

    trial_env = os.environ.copy()
    trial_env["SIMULITH_SPEED"] = speed
    trial_env["SIMULITH_DURATION"] = str(duration)
    trial_env["SIMULITH_WARMUP"] = str(warmup)
    trial_env["SIMULITH_WATCHDOG_SECONDS"] = "1"
    trial_env["SIMULITH_SCENARIO_ENABLED"] = "1"
    trial_env["SIMULITH_SCENARIO_FILE"] = str(scenario)
    started = time.monotonic()
    output = run(compose_cmd + ["up", "-d"], env=trial_env, check=False)
    (trial_dir / "compose-up.log").write_text(output.stdout, encoding="utf-8")
    if output.returncode != 0:
        raise RuntimeError(f"compose up failed; see {trial_dir / 'compose-up.log'}")

    waiter: subprocess.Popen[str] | None = None
    stats_process: subprocess.Popen[str] | None = None
    stats_thread: threading.Thread | None = None
    stats_raw_lines: list[str] = []
    resource_samples: list[dict[str, object]] = []
    logs: dict[str, str] = {}
    try:
        containers = tuple(names.values())
        stats_process = subprocess.Popen(
            ["docker", "stats", "--format", "{{json .}}", *containers], cwd=ROOT,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        stats_thread = threading.Thread(
            target=read_continuous_stats,
            args=(stats_process, containers, started, resource_samples,
                  stats_raw_lines), daemon=True)
        stats_thread.start()
        waiter = subprocess.Popen(["docker", "wait", names["server"]], cwd=ROOT,
                                  text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
        if resource_snapshot:
            resource_samples.append(
                {"scope": "startup", "wall_offset_s": time.monotonic() - started,
                 "containers": collect_startup_stats(containers)})
        deadline = time.monotonic() + max(60.0, duration * 3.0 + 30.0)
        scheduler_samples: list[dict[str, object]] = []
        next_scheduler_sample_s = 5.0
        while waiter.poll() is None:
            if time.monotonic() > deadline:
                waiter.terminate()
                raise RuntimeError(f"trial {label} exceeded its watchdog deadline")
            wall_offset_s = time.monotonic() - started
            if wall_offset_s >= next_scheduler_sample_s:
                scheduler_samples.append({
                    "wall_offset_s": wall_offset_s,
                    "containers": scheduler_metadata(containers),
                })
                next_scheduler_sample_s += 5.0
            time.sleep(0.1)
        # Give the synchronized clients a moment to flush terminal logs.
        time.sleep(0.2)

        if stats_process is not None:
            stats_process.terminate()
            stats_process.wait(timeout=5)
            if stats_thread is not None:
                stats_thread.join(timeout=5)
            (trial_dir / "docker-stats.raw").write_text(
                "".join(stats_raw_lines), encoding="utf-8")
        resource_samples.sort(key=lambda sample: float(sample["wall_offset_s"]))
        (trial_dir / "docker-stats.json").write_text(
            json.dumps(resource_samples, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if not scheduler_samples:
            scheduler_samples.append({
                "wall_offset_s": time.monotonic() - started,
                "containers": scheduler_metadata(containers),
            })
        (trial_dir / "scheduler.json").write_text(
            json.dumps(scheduler_samples, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")

        for container in containers:
            value = run(["docker", "logs", container], check=False).stdout
            logs[container] = value
            (trial_dir / f"{container}.log").write_text(value, encoding="utf-8")
        metrics = parse_marker(logs[names["server"]], "SIMULITH_METRICS")
        metrics["terminal"] = parse_marker(
            logs[names["director"]], "SIMULITH_DIRECTOR_TERMINAL")
        metrics["fsw"] = parse_marker(
            logs[names["fsw"]], "SIMULITH_FSW_TERMINAL")
        classify_command_deliveries(metrics["fsw"])
        metrics["scheduling_policy"] = scheduler_samples
        metrics["resource_samples"] = resource_samples
        metrics["scenario"] = metrics.get("terminal", {}).get("scenario", {})
        scenario_source = json.loads(scenario.read_text(encoding="utf-8"))
        delivery_expected, delivery_observed = scenario_delivery_counts(
            scenario_source, metrics["fsw"])
        acceptance_expected, acceptance_observed = scenario_acceptance_counts(
            scenario_source, logs[names["fsw"]])
        metrics["scenario"]["delivery_expected"] = delivery_expected
        metrics["scenario"]["delivery_observed"] = delivery_observed
        metrics["scenario"]["acceptance_expected"] = acceptance_expected
        metrics["scenario"]["acceptance_observed"] = acceptance_observed
        metrics["scenario"]["acceptance_source"] = \
            "scenario-declared cFE EVS application success events"
        metrics["scenario"]["accepted"] = sum(
            int(item["count"]) for item in acceptance_observed)
        graphics_result = run(
            ["docker", "exec", names["42"], "od", "-An", "-tu8",
             "-N24", "/tmp/fortytwo-output-metrics.bin"], check=False)
        graphics_values = graphics_result.stdout.split()
        if graphics_result.returncode == 0 and len(graphics_values) == 3:
            metrics["graphics"] = dict(zip(
                ("graphics_due", "graphics_sent", "graphics_throttled"),
                (int(value) for value in graphics_values)))
    finally:
        if stats_process is not None and stats_process.poll() is None:
            stats_process.terminate()
            try:
                stats_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                stats_process.kill()
        if stats_thread is not None:
            stats_thread.join(timeout=5)
        if waiter is not None and waiter.poll() is None:
            waiter.terminate()
        # Preserve diagnostics even when startup or a synchronized tick trips
        # the watchdog. These logs are the primary evidence for the exact
        # participant holding the barrier.
        for container in names.values():
            if container not in logs:
                value = run(["docker", "logs", container], check=False).stdout
                (trial_dir / f"{container}.log").write_text(value, encoding="utf-8")
        run(compose_cmd + ["down", "--remove-orphans", "--timeout", "2"], check=False)
    metrics["speed"] = speed
    metrics["trial"] = trial_number
    metrics["startup_and_run_wall_s"] = time.monotonic() - started
    return metrics


def fidelity(left: dict[str, object], right: dict[str, object]) -> dict[str, object]:
    exact_fields = ("ticks", "simulated_ns", "completions", "protocol_errors",
                    "duplicate_completions", "stale_completions", "future_completions")
    differences = {field: [left.get(field), right.get(field)]
                   for field in exact_fields if left.get(field) != right.get(field)}
    for field in ("prepare_count", "commit_count", "telemetry_count", "digest",
                  "component_phase_errors", "component_service_errors",
                  "telemetry_errors", "fortytwo_errors", "queue"):
        left_value = left.get("terminal", {}).get(field)
        right_value = right.get("terminal", {}).get(field)
        if left_value != right_value:
            differences[f"terminal.{field}"] = [left_value, right_value]
    for field in ("sch_ticks", "participants_registered",
                  "participants_completed", "participants_canceled",
                  "ground_output_due", "command_delivery_overflows"):
        left_value = left.get("fsw", {}).get(field)
        right_value = right.get("fsw", {}).get(field)
        if left_value != right_value:
            differences[f"fsw.{field}"] = [left_value, right_value]
    for field in ("digest", "expected", "injected", "accepted", "errors",
                  "acceptance_source", "delivery_expected", "delivery_observed",
                  "acceptance_expected", "acceptance_observed"):
        left_value = left.get("scenario", {}).get(field)
        right_value = right.get("scenario", {}).get(field)
        if left_value != right_value:
            differences[f"scenario.{field}"] = [left_value, right_value]
    for collection, key_fields, value_fields in (
            ("device_transactions", ("name",), ("count", "errors")),
            ("participant_latency", ("schedule_entry", "mid"), ("count",)),
            ("deterministic_command_deliveries", ("mid",), ("count",))):
        left_items = sorted(
            tuple(item.get(key) for key in key_fields) +
            tuple(item.get(value) for value in value_fields)
            for item in left.get("fsw", {}).get(collection, []))
        right_items = sorted(
            tuple(item.get(key) for key in key_fields) +
            tuple(item.get(value) for value in value_fields)
            for item in right.get("fsw", {}).get(collection, []))
        if left_items != right_items:
            differences[f"fsw.{collection}"] = [left_items, right_items]
    for collection in ("participants",):
        left_counts = sorted((item.get("id"), item.get("phase"), item.get("count"))
                             for item in left.get(collection, []))
        right_counts = sorted((item.get("id"), item.get("phase"), item.get("count"))
                              for item in right.get(collection, []))
        if left_counts != right_counts:
            differences[collection] = [left_counts, right_counts]
    left_state = left.get("terminal", {}).get("state", {})
    right_state = right.get("terminal", {}).get("state", {})
    for field in ("valid", "sim_time", "dyn_time", "qn", "wn", "pos_n", "vel_n"):
        left_value = left_state.get(field)
        right_value = right_state.get(field)
        left_values = left_value if isinstance(left_value, list) else [left_value]
        right_values = right_value if isinstance(right_value, list) else [right_value]
        if len(left_values) != len(right_values) or any(
                not math.isclose(float(a), float(b), rel_tol=1e-12, abs_tol=1e-12)
                for a, b in zip(left_values, right_values)):
            differences[f"terminal.state.{field}"] = [left_value, right_value]
    return {"passed": not differences, "differences": differences,
            "exact_fields": list(exact_fields), "dynamics_tolerance": 1e-12,
            "command_delivery_scope":
                "all command deliveries except explicitly wall-clock-paced ground output"}


def evaluate(report: dict[str, object], baseline: dict[str, object] | None,
             enforce_performance: bool) -> list[str]:
    failures: list[str] = []
    trials = report["trials"]
    assert isinstance(trials, list)
    max_speeds = [float(t["achieved_speed"]) for t in trials if t["speed"] == "max"]
    for index, achieved in enumerate(max_speeds, 1):
        if enforce_performance and achieved < 25.0:
            failures.append(f"unbounded trial {index} achieved {achieved:.3f}x (<25x)")
    for trial in trials:
        achieved = float(trial["achieved_speed"])
        if enforce_performance and trial["speed"] == "1" and not 0.99 <= achieved <= 1.01:
            failures.append(f"1x trial achieved {achieved:.3f}x")
        if enforce_performance and trial["speed"] == "25" and not 24.75 <= achieved <= 25.25:
            failures.append(f"25x trial achieved {achieved:.3f}x")
        if int(trial["protocol_errors"]) != 0:
            failures.append(f"trial {trial['speed']} had protocol errors")
        for field in ("duplicate_completions", "stale_completions",
                      "future_completions"):
            if int(trial.get(field, -1)) != 0:
                failures.append(f"trial {trial['speed']} had {field.replace('_', ' ')}")
        ticks = int(trial["ticks"])
        participants = trial.get("participants", [])
        expected_completions = len(participants) * ticks
        if int(trial["completions"]) != expected_completions or any(
                int(participant.get("count", -1)) != ticks
                for participant in participants):
            failures.append(
                f"trial {trial['speed']} did not complete every registered phase")
        terminal = trial.get("terminal", {})
        queue = terminal.get("queue", {})
        if int(queue.get("overflows", -1)) != 0:
            failures.append(f"trial {trial['speed']} had command queue overflows")
        if int(terminal.get("component_service_errors", -1)) != 0:
            failures.append(f"trial {trial['speed']} had component service errors")
        if int(terminal.get("component_phase_errors", -1)) != 0:
            failures.append(f"trial {trial['speed']} had component phase errors")
        if int(terminal.get("telemetry_errors", -1)) != 0:
            failures.append(f"trial {trial['speed']} had director telemetry errors")
        if int(terminal.get("fortytwo_errors", -1)) != 0:
            failures.append(f"trial {trial['speed']} had 42 transport errors")
        if int(terminal.get("state", {}).get("valid", 0)) != 1:
            failures.append(f"trial {trial['speed']} has no valid terminal 42 state")
        scenario = trial.get("scenario", {})
        if int(scenario.get("errors", -1)) != 0 or \
           int(scenario.get("injected", -1)) != int(scenario.get("expected", -2)):
            failures.append(f"trial {trial['speed']} did not inject every scenario command")
        if int(scenario.get("accepted", -1)) != int(scenario.get("expected", -2)):
            failures.append(f"trial {trial['speed']} did not accept every scenario command")
        if scenario.get("delivery_expected") != scenario.get("delivery_observed"):
            failures.append(
                f"trial {trial['speed']} software-bus command deliveries differ from scenario")
        if scenario.get("acceptance_expected") != scenario.get("acceptance_observed"):
            failures.append(
                f"trial {trial['speed']} application command acceptances differ from scenario")
        if enforce_performance and (int(queue.get("enqueued", 0)) <= 0 or
                                    int(queue.get("dequeued", 0)) <= 0):
            failures.append(f"trial {trial['speed']} did not exercise the 42 actuator queue")
        if enforce_performance and int(
                queue.get("nonzero_actuator_commands", 0)) <= 0:
            failures.append(
                f"trial {trial['speed']} had no nonzero ADCS actuator commands")
        if int(terminal.get("prepare_count", -1)) != ticks or \
           int(terminal.get("commit_count", -1)) != ticks:
            failures.append(f"trial {trial['speed']} director phase counts differ from ticks")
        fsw = trial.get("fsw", {})
        if int(fsw.get("sch_ticks", -1)) != ticks:
            failures.append(f"trial {trial['speed']} SCH slot count differs from ticks")
        if int(fsw.get("participants_registered", -1)) != \
           int(fsw.get("participants_completed", -2)):
            failures.append(f"trial {trial['speed']} has incomplete FSW participants")
        if int(fsw.get("participants_canceled", -1)) != 0:
            failures.append(f"trial {trial['speed']} canceled an FSW participant")
        if int(fsw.get("command_delivery_overflows", -1)) != 0:
            failures.append(f"trial {trial['speed']} overflowed command-delivery metrics")
        device_transactions = {
            str(item.get("name")): item
            for item in fsw.get("device_transactions", [])}
        for item in device_transactions.values():
            if int(item.get("latency_us", {}).get(
                    "histogram_overflows", -1)) != 0:
                failures.append(
                    f"trial {trial['speed']} overflowed the {item.get('name')} latency histogram")
        for item in fsw.get("participant_latency", []):
            for latency_name in ("dispatch_us", "execution_us", "execution_cpu_us"):
                if int(item.get(latency_name, {}).get(
                        "histogram_overflows", -1)) != 0:
                    failures.append(
                        f"trial {trial['speed']} overflowed participant "
                        f"{item.get('schedule_entry')}/{item.get('mid')} {latency_name}")
        for device in ("/dev/usart_2", "I2C0_0x1E", "SPI1_CS0"):
            item = device_transactions.get(device, {})
            if int(item.get("count", 0)) <= 0:
                failures.append(f"trial {trial['speed']} did not exercise {device}")
            if int(item.get("errors", -1)) != 0:
                failures.append(f"trial {trial['speed']} had {device} transaction errors")
        output_due = int(fsw.get("ground_output_due", -1))
        output_accounted = int(fsw.get("ground_output_sent", -2)) + \
            int(fsw.get("ground_output_throttled", -3))
        if output_due < 0 or output_due != output_accounted:
            failures.append(f"trial {trial['speed']} ground-output accounting mismatch")
        ground_mid = int(fsw.get("ground_output_mid", 0))
        wall_clock_deliveries = fsw.get("wall_clock_command_deliveries", [])
        wall_clock_sent = sum(int(item.get("count", 0))
                              for item in wall_clock_deliveries)
        if ground_mid <= 0 or wall_clock_sent != int(fsw.get("ground_output_sent", -1)):
            failures.append(
                f"trial {trial['speed']} ground-output delivery count mismatch")
        graphics = trial.get("graphics", {})
        graphics_due = int(graphics.get("graphics_due", -1))
        graphics_accounted = int(graphics.get("graphics_sent", -2)) + \
            int(graphics.get("graphics_throttled", -3))
        if graphics_due < 0 or graphics_due != graphics_accounted:
            failures.append(f"trial {trial['speed']} graphics-output accounting mismatch")
    if enforce_performance and not report["fidelity"]["passed"]:
        failures.append("1x/25x exact-count fidelity failed")
    # Repeatability is a correctness check, not a performance threshold --
    # unlike the speed/queue-activity checks above, it must not be skipped
    # in smoke/determinism modes (enforce_performance=False), or a genuine
    # non-determinism would print FAIL but still exit 0.
    if not report["repeatability"]["passed"]:
        failures.append("cross-trial terminal/count repeatability failed")
    if enforce_performance and baseline and max_speeds:
        if baseline.get("schema_version") != report.get("schema_version"):
            failures.append("baseline report schema does not match candidate schema")
        for field in ("mission", "spacecraft", "workload", "simulated_duration_s",
                      "warmup_s", "output_configuration", "latency_histograms",
                      "scenario", "fidelity_policy"):
            if baseline.get(field) != report.get(field):
                failures.append(f"baseline {field.replace('_', ' ')} does not match candidate")
        for field in ("machine", "logical_cpus"):
            if baseline.get("host", {}).get(field) != report.get("host", {}).get(field):
                failures.append(f"baseline host {field.replace('_', ' ')} does not match candidate")
        prior = [float(t["achieved_speed"]) for t in baseline.get("trials", [])
                 if t.get("speed") == "max"]
        if prior and statistics.median(max_speeds) < 0.9 * statistics.median(prior):
            failures.append("median unbounded throughput regressed by more than 10%")
    return failures


def common_trial_value(
        trials: list[dict[str, object]],
        getter: Callable[[dict[str, object]], object]) -> int | None:
    """Return one integer when every trial reports the same value."""
    values = [int(getter(trial)) for trial in trials]
    if not values or any(value != values[0] for value in values[1:]):
        return None
    return values[0]


def print_summary(report: dict[str, object], report_path: pathlib.Path,
                  mode: str, baseline: dict[str, object] | None) -> None:
    """Print a compact human-readable view of the JSON acceptance report."""
    trials = report.get("trials", [])
    assert isinstance(trials, list)
    failures = report.get("failures", [])
    assert isinstance(failures, list)

    result = "PASS" if not failures else "FAIL"
    if mode == "smoke":
        result += " (diagnostic only)"

    print("\nPerformance summary")
    print("===================")
    print(f"Result: {result}")
    print(f"Workload: {report.get('mission')} / {report.get('spacecraft')}, "
          f"{float(report.get('simulated_duration_s', 0.0)):.1f} simulated seconds, "
          f"{float(report.get('warmup_s', 0.0)):.1f}-second warmup")

    if trials:
        print("\nTrial       Achieved   Tick p50   Tick p95    Ticks")
        for trial in trials:
            speed = str(trial.get("speed"))
            requested = "max" if speed == "max" else f"{speed}x"
            label = f"{requested} #{int(trial.get('trial', 0))}"
            latency = trial.get("tick_latency_us", {})
            assert isinstance(latency, dict)
            print(f"{label:<10} {float(trial.get('achieved_speed', 0.0)):>8.3f}x "
                  f"{float(latency.get('p50', 0.0)):>9.1f} us "
                  f"{float(latency.get('p95', 0.0)):>9.1f} us "
                  f"{int(trial.get('ticks', 0)):>8}")

    max_speeds = [float(trial["achieved_speed"]) for trial in trials
                  if trial.get("speed") == "max"]
    if max_speeds:
        trial_word = "trial" if len(max_speeds) == 1 else "trials"
        print(f"Unbounded throughput: median {statistics.median(max_speeds):.3f}x, "
              f"minimum {min(max_speeds):.3f}x across "
              f"{len(max_speeds)} {trial_word}")

    ic_snapshot = report.get("initial_conditions")
    if ic_snapshot:
        assert isinstance(ic_snapshot, dict)
        print(f"Scenario / IC: {ic_snapshot.get('scenario_name')} / "
              f"{ic_snapshot.get('initial_conditions')}")

    fidelity_result = report.get("fidelity", {})
    assert isinstance(fidelity_result, dict)
    if fidelity_result.get("not_run"):
        print(f"1x/25x fidelity: NOT RUN ({mode} mode)")
    else:
        print(f"1x/25x fidelity: {'PASS' if fidelity_result.get('passed') else 'FAIL'}")

    repeatability = report.get("repeatability", {})
    assert isinstance(repeatability, dict)
    comparisons = repeatability.get("comparisons", [])
    if not comparisons:
        print("Cross-trial repeatability: NOT RUN (one trial)")
    else:
        print(f"Cross-trial repeatability: "
              f"{'PASS' if repeatability.get('passed') else 'FAIL'}")

    scenario_ok = all(
        int(trial.get("scenario", {}).get("errors", -1)) == 0 and
        int(trial.get("scenario", {}).get("injected", -1)) ==
        int(trial.get("scenario", {}).get("expected", -2)) and
        int(trial.get("scenario", {}).get("accepted", -1)) ==
        int(trial.get("scenario", {}).get("expected", -2))
        for trial in trials)
    expected_commands = common_trial_value(
        trials, lambda trial: trial.get("scenario", {}).get("expected", -1))
    scenario_detail = (f"{expected_commands} injected and accepted per trial"
                       if expected_commands is not None else "counts vary by trial")
    print(f"Scenario commands: {'PASS' if scenario_ok else 'CHECK REPORT'} "
          f"({scenario_detail})")

    synchronization_ok = all(
        int(trial.get("fsw", {}).get("sch_ticks", -1)) == int(trial.get("ticks", -2)) and
        int(trial.get("fsw", {}).get("participants_registered", -1)) ==
        int(trial.get("fsw", {}).get("participants_completed", -2)) and
        int(trial.get("fsw", {}).get("participants_canceled", -1)) == 0
        for trial in trials)
    tick_count = common_trial_value(trials, lambda trial: trial.get("ticks", -1))
    registered_count = common_trial_value(
        trials, lambda trial: trial.get("fsw", {}).get("participants_registered", -1))
    completed_count = common_trial_value(
        trials, lambda trial: trial.get("fsw", {}).get("participants_completed", -1))
    if tick_count is not None and registered_count is not None and completed_count is not None:
        synchronization_detail = (
            f"{tick_count} ticks and {completed_count}/{registered_count} "
            "FSW participant completions per trial")
    else:
        synchronization_detail = "counts vary by trial"
    print(f"Synchronization: {'PASS' if synchronization_ok else 'CHECK REPORT'} "
          f"({synchronization_detail})")

    actuation_ok = all(
        int(trial.get("terminal", {}).get("queue", {}).get("enqueued", -1)) ==
        int(trial.get("terminal", {}).get("queue", {}).get("dequeued", -2)) and
        int(trial.get("terminal", {}).get("queue", {}).get("overflows", -1)) == 0 and
        int(trial.get("terminal", {}).get("queue", {}).get(
            "nonzero_actuator_commands", 0)) > 0
        for trial in trials)
    actuator_commands = common_trial_value(
        trials, lambda trial: trial.get("terminal", {}).get("queue", {}).get("enqueued", -1))
    nonzero_commands = common_trial_value(
        trials, lambda trial: trial.get("terminal", {}).get("queue", {}).get(
            "nonzero_actuator_commands", -1))
    if actuator_commands is not None and nonzero_commands is not None:
        actuation_detail = (
            f"{actuator_commands} commands, {nonzero_commands} nonzero, "
            "zero overflows per trial")
    else:
        actuation_detail = "counts vary by trial"
    print(f"ADCS actuation: {'ACTIVE' if actuation_ok else 'CHECK REPORT'} "
          f"({actuation_detail})")

    if baseline is not None:
        baseline_speeds = [float(trial["achieved_speed"])
                           for trial in baseline.get("trials", [])
                           if trial.get("speed") == "max"]
        if max_speeds and baseline_speeds:
            candidate_median = statistics.median(max_speeds)
            baseline_median = statistics.median(baseline_speeds)
            change = ((candidate_median / baseline_median) - 1.0) * 100.0 \
                if baseline_median else 0.0
            print(f"Baseline comparison: {'PASS' if not failures else 'FAIL'} "
                  f"({candidate_median:.3f}x vs {baseline_median:.3f}x, "
                  f"{change:+.1f}%)")
        else:
            print("Baseline comparison: CHECK REPORT (no comparable unbounded trials)")

    if failures:
        print("Failures:")
        for failure in failures:
            print(f"  - {failure}")
    print(f"Report: {report_path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("smoke", "perf", "compare", "determinism"),
                        default="smoke")
    parser.add_argument("--baseline")
    parser.add_argument(
        "--artifact-dir",
        help="report directory (default: build/performance/shire-perf-<UTC timestamp>)",
    )
    parser.add_argument("--duration", type=float)
    parser.add_argument("--warmup", type=float, default=5.0)
    parser.add_argument("--resource-snapshot", action="store_true",
                        help="collect a Docker stats snapshot during excluded startup")
    args = parser.parse_args()
    if args.mode == "compare" and not args.baseline:
        parser.error("--baseline is required for compare mode")

    mission = active_value("mission")
    spacecraft = active_value("spacecraft")
    names = container_names(mission, spacecraft)
    compose = ROOT / "build" / mission / "shire-compose.yaml"
    timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    if args.artifact_dir:
        artifact_dir = pathlib.Path(args.artifact_dir).expanduser().resolve()
    else:
        artifact_dir = ROOT / "build" / "performance" / f"shire-perf-{timestamp}"
    artifact_dir.mkdir(parents=True, exist_ok=True)
    duration = args.duration if args.duration is not None else 75.0
    if not math.isfinite(duration) or duration <= 0.0:
        parser.error("--duration must be a positive finite number")
    if not math.isfinite(args.warmup) or args.warmup < 0.0 or args.warmup >= duration:
        parser.error("--warmup must be non-negative and shorter than --duration")
    if args.mode == "smoke":
        plan = [("max", 1)]
    elif args.mode == "determinism":
        # Two identically-configured trials (same speed, same scenario, same
        # build/active.yaml IC selection) — reuses the existing repeatability
        # comparison in full, including fidelity()'s exact terminal.state
        # (qn/wn/pos_n/vel_n/dyn_time) check, to confirm a scenario's
        # Initial Condition bin produces the same run every time.
        plan = [("max", 1), ("max", 2)]
    else:
        plan = [("1", 1), ("25", 1), ("max", 1), ("max", 2), ("max", 3)]

    report: dict[str, object] = {
        "schema_version": 4,
        "created_utc": timestamp,
        "git": git_metadata(),
        "build": build_metadata(mission, spacecraft, compose),
        "latency_histograms": {
            "participant_resolution_us": 5,
            "device_transaction_resolution_us": 5,
            "histogram_max_us": 10240,
        },
        "output_configuration": {"graphics_hz": os.environ.get("FORTYTWO_GRAPHICS_HZ", "1"),
                                 "ground_output_hz": os.environ.get("SHIRE_GROUND_OUTPUT_HZ", "20")},
        "fidelity_policy": {
            "deterministic_commands":
                "all observed command deliveries except the reported periodic-ground-output MID",
            "scenario_commands": "exact Software Bus delivery and application acceptance",
            "periodic_ground_output":
                "exact due=sent+throttled accounting; sent count is wall-clock dependent",
        },
        "workload": "automatic mission startup plus sequence-numbered CI_LAB ADCS checkout",
        "host": host_metadata(),
        "mission": mission,
        "spacecraft": spacecraft,
        "simulated_duration_s": duration,
        "warmup_s": args.warmup,
        "trials": [],
    }
    scenario = resolved_scenario(artifact_dir, spacecraft)
    report["scenario"] = json.loads(scenario.read_text(encoding="utf-8"))
    report["initial_conditions"] = initial_conditions_snapshot(
        mission, active_value("scenario"))
    for speed, number in plan:
        print(f"running synchronized {speed}x trial {number}", flush=True)
        report["trials"].append(
            one_trial(compose, artifact_dir, speed, duration, args.warmup, number,
                      args.resource_snapshot, names, scenario))

    one_x = next((t for t in report["trials"] if t["speed"] == "1"), None)
    twenty_five_x = next((t for t in report["trials"] if t["speed"] == "25"), None)
    report["fidelity"] = fidelity(one_x, twenty_five_x) if one_x and twenty_five_x else {
        "passed": True, "not_run": True}
    reference = one_x or report["trials"][0]
    repeatability = []
    for trial in report["trials"]:
        if trial is reference:
            continue
        comparison = fidelity(reference, trial)
        repeatability.append({"speed": trial["speed"], "trial": trial["trial"],
                              **comparison})
    report["repeatability"] = {
        "passed": all(item["passed"] for item in repeatability),
        "reference": {"speed": reference["speed"], "trial": reference["trial"]},
        "comparisons": repeatability,
    }
    baseline = json.loads(pathlib.Path(args.baseline).read_text(encoding="utf-8")) \
        if args.baseline else None
    report["failures"] = evaluate(
        report, baseline,
        enforce_performance=args.mode not in ("smoke", "determinism"))
    report_path = artifact_dir / "report.json"
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print_summary(report, report_path, args.mode, baseline)
    return 1 if report["failures"] else 0


if __name__ == "__main__":
    raise SystemExit(main())

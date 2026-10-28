# Verify synchronized simulation performance

`make perf` measures the complete SHIRE simulation while checking that each
simulated tick closes the control loop.
Run it from the repository root after selecting a mission and spacecraft in
`build/active.yaml`:

```sh
make perf
```

The command builds the active stack and runs a 75-second simulated workload.
It runs one full-output reference at 1x, then control-mode trials at 1x, 25x,
50x, and three unbounded trials.
The gate requires accurate 1x, 25x, and 50x pacing and more than 50x in each
unbounded trial.
Performance depends on the host, so a failure can indicate either a speed
shortfall or a correctness problem.
The summary and JSON report identify which check failed.

## What the gate checks

Every trial completes all 7,500 synchronized 10 ms ticks and their scheduled
flight work before advancing to the next tick.
The workload sends ADCS ENABLE and SUNSAFE through the normal ground-command
path and checks application acceptance and actuator activity.
The gate compares terminal dynamics, command deliveries, device transactions,
participant completions, and output accounting across trials.
It also compares the complete control traces byte for byte, including the
full-output reference.

The Director sends one 42 truth packet per simulated second to Yamcs.
The gate requires all 75 sends to succeed and verifies that Yamcs decodes at
least two increasing `/SIM_42_TRUTH/DYN_TIME` values that match the trace.
Visualization truth is also published at 20 Hz by default.
Each trial reports
native Yamcs archive sequence coverage separately from the simulation result.

`make perf` writes the report and trial logs under
`build/performance/shire-perf-<UTC timestamp>/`.
The report records the workload, CPU placement, throughput, trace format,
output mode, and reasons for any failure.
Use `make perf-smoke` for one short diagnostic trial.
A smoke result is not a performance acceptance result.

The workload commands ADCS through `cfg/perf-scenario.json`, so the active spacecraft must include ADCS.
`sat-1` and `sat-2` do, while `flatsat` does not and fails the ADCS actuator check.

### Performance options

Call `tools/shire-perf.py` directly for options that `make` does not pass.

| Option | Meaning |
| --- | --- |
| `--mode smoke` | One unbounded trial for a quick diagnostic, which is the default and what `make perf-smoke` runs. |
| `--mode perf` | The full gate that `make perf` runs, with the 50x target. |
| `--mode perf-200` | The full gate plus a 200x trial, with a 200x target. |
| `--mode compare` | Repeat the gate against `--baseline`, which `make perf-compare` runs. |
| `--mode determinism` | Two identical unbounded trials, which `make scenario-smoke` uses. |
| `--duration <seconds>` | Simulated seconds per trial, default 75. |
| `--warmup <seconds>` | Excluded startup seconds, default 5, which must be shorter than the duration. |
| `--artifact-dir <path>` | Report directory, default `build/performance/shire-perf-<UTC timestamp>`. |
| `--resource-snapshot` | Collect a Docker stats snapshot during the excluded startup. |
| `SHIRE_PERF_CPU_PLACEMENT` | Environment variable that is `auto` by default, `off` to disable CPU pinning, or `service=cpuset,...` for a manual placement. |

Each trial writes its logs and trace under `<artifact dir>/<speed>x-trial-<n>/`.
For example, `1x-trial-1/traces/control-trace.v1` holds the 1x trace, unbounded trials use `maxx-trial-<n>`, and the full-output reference uses `1x-trial-0-full-reference`.

## Build and startup cost

The 42 compiler stage copies source and headers separately from runtime model data, so an imagery or model change does not trigger C recompilation.
The Simulith build stages only its executables, shared library, and selected component modules instead of copying 42 model data into the Server image.
Runtime images omit compiler and Maven toolchains, reducing image transfer and unpacking work during startup.
These packaging changes do not remove physics or control-loop work from a simulation tick.
Use the full `make perf` gate to measure steady-state throughput and verify fidelity after changing runtime packaging.

## 42 output modes

SHIRE runs 42 in control mode by default.
Control mode retains physics, state and command IPC, and Yamcs truth
packets while omitting synchronous legacy 42 report files.
Set `SHIRE_42_REPORT_MODE=full` when a run needs those files:

```sh
SHIRE_42_REPORT_MODE=full make start
```

Both modes record every published 42 state and acknowledged actuator batch in
an ordered, versioned control trace.
Completed traces are stored in run-specific directories under `build/`.
For `make start` and `make scenario`, `make cfg` creates a random run directory at `build/<mission>/traces/<spacecraft>/<random id>/`, for example `build/drm/traces/sat-1/<random id>/control-trace.v1`.
The directory name is a random identifier created each time the Compose file is rendered, so list the directory to find it.
A Monte Carlo trial uses `build/<mission>/<instance>/traces/` instead.
`make perf` writes each trace under its own trial directory, as described in [Performance options](#performance-options).
The trace writer blocks if its bounded queue fills, and the final timed tick
includes the trace drain.
A failed run retains its partial trace for diagnosis.
To inspect a trace, run:

```sh
python3 tools/shire-control-trace.py build/drm/traces/sat-1/<random id>/control-trace.v1
```

Replace `<random id>` with the directory for the run.
For a `make perf` trial, use `build/performance/shire-perf-<UTC timestamp>/<trial>/traces/control-trace.v1`.
Add `--jsonl` to stream the decoded ticks.
The trace records control inputs and outputs, not the legacy report-file format.

## Compare runs

After reviewing and retaining an accepted `report.json`, compare a new run on
the same host and workload:

```sh
make perf-compare BASELINE=/path/to/accepted-report.json
```

The comparison repeats the 50x gate and checks for a throughput regression.
It rejects reports with different workload, output configuration, CPU
placement, or performance target.
Historical full-output reports and reports targeting a different speed are
not interchangeable with the current control-mode baseline.

***
Last reviewed: 20261027

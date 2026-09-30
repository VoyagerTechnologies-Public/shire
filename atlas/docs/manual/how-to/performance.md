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
UDP delivery is not required for every packet.

`make perf` writes the report and trial logs under
`build/performance/shire-perf-<UTC timestamp>/`.
The report records the workload, CPU placement, throughput, trace format,
output mode, and reasons for any failure.
Use `make perf-smoke` for one short diagnostic trial.
A smoke result is not a performance acceptance result.

## 42 output modes

SHIRE runs 42 in control mode by default.
Control mode retains physics, graphics, state and command IPC, and Yamcs
truth packets while omitting synchronous legacy 42 report files.
Set `SHIRE_42_REPORT_MODE=full` when a run needs those files:

```sh
SHIRE_42_REPORT_MODE=full make start
```

Both modes record every published 42 state and acknowledged actuator batch in
an ordered, versioned control trace.
Completed traces are stored in run-specific directories under `build/`.
The trace writer blocks if its bounded queue fills, and the final timed tick
includes the trace drain.
A failed run retains its partial trace for diagnosis.
To inspect a trace, run:

```sh
python3 cfg/shire-control-trace.py build/drm/traces/sat-1/RUN_ID/control-trace.v1
```

Replace `RUN_ID` with the directory for the run.
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

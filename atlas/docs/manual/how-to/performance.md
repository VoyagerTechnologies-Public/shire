# Truthfully synchronized simulation performance

Date: 2026-09-13  
Host: Linux/WSL2, 22 logical CPUs, Docker memory limit 15.35 GiB  
Default artifact location: `build/performance/shire-perf-<UTC timestamp>/report.json`

This document records the diagnose-first performance work and the first valid
active-workload result.
All measurements retain separate server, director, FSW, 42, crypto, component,
and ground containers, the 10 ms simulated timestep, and the PREPARE -> EXECUTE
-> COMMIT barrier.
Flight applications retain one
synchronous device-transaction path and contain no simulation-only branches.
Profiling captures and generated reports are intentionally outside the tracked
source tree.

## Valid workload

The performance workload runs for 75 simulated seconds.
The director loads the
versioned `cfg/perf-scenario.json` scenario and, during COMMIT, sends two normal
CCSDS/UDP packets to CI_LAB:

* ADCS ENABLE at sequence 2000.
* ADCS SUNSAFE at sequence 2200.

The packets do not use a component backdoor.
The performance report schema records the
scenario identity and digest, injected commands, explicit cFE Software Bus
delivery counts, and application acceptance counts from the success events
declared by each scenario command.
It also records device transaction counts, nonzero actuator commands,
scheduling policies, timestamped resource samples, captured build provenance,
5-us participant/device latency distributions with explicit out-of-range counts,
phase latency, and terminal-state data.
Every accepted trial recorded two injections, two deliveries, two application
success events, nonzero
ADCS actuator activity, and zero scenario errors.

Each command entry keeps the Director-facing transport fields flat
(`sequence`, `host`, `port`, and `packet_hex`).
The optional flat
`acceptance_app`, `acceptance_event_id`, and `acceptance_count` fields tell the
performance harness which existing cFE EVS success event proves application
acceptance.
They do not add a simulation branch or callback to flight code.

Command fidelity compares every deterministic Software Bus command delivery.
The only excluded MID is the report-declared periodic ground-output wakeup
(`TO_LAB_WAKEUP_MID`), whose raw delivery count remains recorded and must equal
`ground_output_sent`.
Its deterministic assertion is
`ground_output_due = ground_output_sent + ground_output_throttled`, because the
sent/throttled split is intentionally paced by wall clock.

## Diagnose-first findings

The original unchanged-tree comparison showed overlapping performance between
the current and pre-submodule-update revisions.
The apparent 0.75x-0.98x spread
was primarily scheduler sensitivity, not one identifiable submodule regression.
It also exposed that the old FSW acknowledgement represented receipt rather than
completed scheduled work, making the old throughput number unsuitable as a
closed-loop baseline.

The first truthful post-component-migration smoke achieved only 1.53x.
The
measured critical path was then reduced one category at a time:

1. Director synchronization: retain one service worker per component for
   concurrent FSW requests, but run `on_tick` and `actuate` sequentially.
   Remove
   the PREPARE worker and EXECUTE-readiness barriers, block on transport
   readiness plus an interrupt descriptor, and prevent COMMIT until active
   services return.
2. Device transport: share one ZeroMQ context per process and provide blocking,
   monotonic-deadline exact receives for simulated UART, SPI, I2C, and GPIO.
3. OSAL timeout clock: POSIX absolute waits were incorrectly derived from
   simulated elapsed time.
   Constructing those deadlines from `CLOCK_REALTIME`
   increased active-workload throughput to about 14.1x.
4. Radio transactions: a zero-length receive now terminates an empty drain, and
   target-neutral deadline-based SPI reads replace blind delay/retry loops.
   This
   raised the workload to roughly 25x without changing application behavior or
   physical-hardware semantics.
5. 42 IPC: versioned framed command messages send only populated commands while
   preserving one exchange per tick and an explicit text compatibility mode.
   This raised the repeatable median above 26x.

No tick is batched, skipped, coalesced, or speculated.
Graphics and periodic
ground output remain enabled and wall-clock paced independently of the internal
barrier.
Lossless outputs retain their existing accounting.

## Truthful transitive FSW completion

Strict repeatability testing found one remaining barrier gap: CI_LAB could
complete after publishing a downstream ADCS command while ADCS processing was
still queued.
The Software Bus/PSP integration now registers a child completion
for every command subscriber when an active scheduled participant publishes a
command.
Child tokens are allocated atomically, tied to the current sequence,
and canceled on transmit failure.
Telemetry and ordinary asynchronous sends do
not create completion tokens.

This makes the barrier transitive: tick N+1 cannot begin until scheduled work,
commands produced by that work, all resulting device transactions, component
services, ACTUATE, and the 42 command commit for tick N have completed.
The
participant watchdog identifies the schedule entry, MID, and claimed/running
state holding a stalled tick.

## Candidate acceptance result

The following results are from one warm 1x run, one requested-25x run, and three
unbounded runs of the same 7500-tick active scenario:

| Mode | Achieved | Tick p50 | Tick p95 | Tick p99 |
|---|---:|---:|---:|---:|
| 1x | 0.999999x | 482 us | 791 us | 1,084 us |
| requested 25x | 24.999969x | 283 us | 428 us | 560 us |
| max trial 1 | 29.298461x | 319 us | 460 us | 573 us |
| max trial 2 | 28.150260x | 328 us | 490 us | 618 us |
| max trial 3 | 29.822094x | 313 us | 461 us | 592 us |

Median unbounded throughput is 29.298461x.
Every unbounded trial exceeds 25x,
requested 25x is within one percent, and warm 1x is within 0.99x-1.01x.

All five trials produced:

* Terminal dynamics digest `6538e83164bbf0c9`, with terminal floating-point
  state also compared at `1e-12` absolute/relative tolerance.
* Exactly 7500 ticks, SCH slots, and PREPARE/EXECUTE/COMMIT phase sets.
* Exactly 911 registered and 911 completed FSW participants, with no
  cancellations.
* 524 enqueued and dequeued 42 commands, queue high-watermark 2, 363 nonzero
  ADCS actuator commands, and zero overflows.
* 8 I2C, 1 GPIO, 104 SPI, and 55 UART transactions, all successful.
* Zero protocol, stale, future, duplicate, component, device, and queue errors.
* Scenario digest `a26a1ed90197c58c` and exact deterministic command/count
  fidelity.
  The wall-clock periodic-output split remains fully accounted in
  every trial.

Both the explicit 1x/25x fidelity comparison and the cross-trial terminal/count
repeatability check pass.
This report is a local baseline candidate for review.
The harness does not accept or install it automatically.
No baseline artifact
or source change has been committed.

The report records continuous Docker resource samples and each cFE thread's
effective `/proc` scheduling policy.
`perf`, `pidstat`, and `strace` were not
installed on this host, so their report fields explicitly record them as
unavailable rather than implying those captures were collected.

## CPU placement

Despite the architectural work above, repeated unbounded `make perf` runs on
the same workload, image, and host were observed landing anywhere from the
mid-teens to the high-20s x, hours apart -- a repeatability problem, not a
uniform slowdown. Before changing anything, this repo's own already-collected
`make perf` instrumentation (`scheduler.json` migrations, `docker-stats.json`
CPU%) was checked for a cause:

* Across a slow run and two faster runs of the same unbounded workload,
  `shire-director`'s CPU-migration count tracked the slowdown closely (far
  higher in the slow run than in either faster one), and `shire-gsw` (outside
  the Simulith tick barrier) ran the hottest CPU% of any container in every
  run -- a plausible noisy neighbor for the barrier participants even though
  it isn't itself barrier-gated.
* A fresh unplaced baseline landed comfortably above the 25x floor on all
  three unbounded trials, but *failed* on the requested-25x trial by landing
  just outside the required tolerance band -- a narrow, specific gap, not a
  broad slowdown.

**Implementation**: `cfg/shire_perf_topology.py` (new) partitions this
host's physical cores (read from `/sys/devices/system/cpu/*/topology/`,
Linux-only) among `shire-server`/`shire-director`/`shire-fsw`/`shire-42`/
`shire-gsw`/`shire-cryptolib`, proportional to each service's `cpus:` quota
in `cfg/shire-compose.j2`, and writes a docker-compose override (`cpuset:`
per service) that `cfg/shire-perf.py` passes as an extra `-f` alongside the
normal generated compose file -- `cfg/shire-compose.j2` itself, and every
ordinary `make scenario`/`make start`/campaign run, are unchanged. Default
is automatic detection with a safe unplaced fallback (non-Linux host,
undetectable topology, or fewer than 8 physical cores); `SHIRE_PERF_CPU_PLACEMENT=off`
opts out explicitly, and a manual `service=cpuset,...` override string is
honored verbatim for restricted/unusual hosts. The effective placement (or
the reason none was applied) is recorded in `report.json`'s `placement`
field and now required to match between a baseline and a candidate before
`make perf-compare` accepts a throughput comparison.

**Result**, on an 11-physical-core/22-logical-CPU host, across several
independent full `make perf` runs with placement enabled (default):

| Run | 25x trial | Unbounded (min/median/max) | Result |
|---|---:|---:|---|
| Unplaced baseline | 24.669x (FAIL, <24.75x) | 25.53x / 26.14x / 26.34x | FAIL |
| Placed, run 1 | 25.000x | 31.25x / 32.48x / 33.69x | PASS |
| Placed, run 2 | 25.000x | 31.66x / 32.31x / 32.84x | PASS |
| Placed, run 3 | 24.999x | 28.10x / 32.43x / 32.92x | PASS |
| Placed, run 4 | 24.999x | 29.43x / 31.97x / 32.87x | PASS |

Every placed run hit the requested-25x trial at or within a thousandth of a
percent of exact, and raised median unbounded throughput by roughly a
quarter over the unplaced baseline, with a visibly tighter spread. Fidelity
and repeatability passed in every run, placed or not -- placement changed
throughput and its consistency, nothing about correctness.
`MAX_CATCHUP_LAG_NS`/Simulith's pacing loop was not touched; this is purely
a docker-compose-level change scoped to `make perf`.

`shire-gsw` and `shire-cryptolib` are not gated by the Simulith tick
barrier, but are isolated onto their own cores here anyway, because the
measured evidence (GSW's high CPU%) supported it independent of whether
they participate in the barrier.

## Regression commands

```sh
make perf-smoke
make perf
make perf-compare BASELINE=/path/to/accepted-report.json
```

`make perf` runs the complete active scenario at 1x and 25x plus three
unbounded trials.
Comparison fails if fidelity or repeatability changes, any
unbounded trial falls below 25x, requested pacing is outside tolerance, or
median throughput regresses more than ten percent from the supplied accepted
baseline.
Reports and raw logs default to timestamped, untracked directories under
`build/performance/`.
At completion, each performance command prints a concise pass/fail summary,
trial throughput and latency, fidelity and repeatability status, synchronized
workload counts, and the full report path.
`make perf-compare` also prints the candidate and baseline median unbounded
throughput and percentage change.

## Verification

* Simulith/director: 15/15 tests passed, with 83.6% line, 67.4% branch, and 66.7%
  MC/DC coverage.
* FSW/PSP/SCH/cFE: 160/160 tests passed, with 96.2% line, 94.1% branch, and 93.8%
  MC/DC coverage.
* ADCS, demo, EPS, and radio component suites all passed.
  Combined coverage was
  95.2% line, 81.3% branch, and 79.8% MC/DC.
* Strict production builds, Python syntax checks, and recursive whitespace
  checks passed.

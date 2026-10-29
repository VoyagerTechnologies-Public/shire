# Scenarios and initial conditions

SHIRE has one scenario concept: a description of how a mission run starts
and what happens during it.
There are two ways to exercise a scenario today.

An Atlas doc scenario (`atlas/docs/scenarios/*.md`) is a scenario walked through
manually: a human reads the steps and drives 42, YAMCS, and the vehicle by
hand.
Commissioning, nominal operations, and FDIR are manual only.
They have no DRM scenario behind them, and the DRM scenario named `nominal` is a separate, shorter configuration run rather than the Nominal Operations page.
Other doc pages pair with a DRM scenario that automates their checks, such as Cmd/Tlm Validation, Radio link testing, Radio security testing, ADCS truth comparison, and EPS load switching.
A DRM scenario (`cfg/drm/scenarios/*.yaml`, selected via `build/active.yaml`'s
`scenario:`) is the same concept exercised through configuration instead: it
is built, and can be run automatically, without a human walking through
steps.
`drm` is simply the default mission this repository ships with, and its
scenarios are the out-of-the-box example of the configured form.
There is nothing DRM-specific about the mechanism.
Any mission can define its own scenarios the same way.

The two forms are not tied to separate content forever.
Commissioning, nominal operations, and FDIR have no machine-readable configuration behind them today,
but a future one could be rebuilt on the same DRM scenario and Initial
Condition (IC) bin mechanism this page describes, gaining the reproducible
starting state and the autonomous/manual run paths below for free.

A DRM scenario can also reference a Director command scenario (for example
`cfg/perf-scenario.json`, see [Performance](performance.md) and
[Simulations](../core-concepts/simulations.md#performance-regression-runs))
via `director_command_scenario:` to inject a versioned, sequence-numbered
CCSDS command playlist during the run.
That is a supporting mechanism for in-run behavior, not a third kind of
scenario: a DRM scenario uses it the way it uses an IC bin, to fill in one
part of "what starts, and what happens."

## Initial Condition (IC) bins

Orbit, epoch, attitude, and (optionally) per-component state used to be
hardcoded in `cfg/42_shire_config/Orb_SHIRE.txt`, `SC_SHIRE.txt`, and
`Inp_Sim.txt`.
They are now templates (`.j2`), and their values come from a named IC bin
under `cfg/drm/initial_conditions/*.yaml`, referenced by a scenario's
`initial_conditions:` field:

```yaml
# cfg/drm/scenarios/eclipse-entry-adcs.yaml
scenario_name: "eclipse-entry-adcs"
description: "..."
initial_conditions: "eclipse-entry"   # cfg/drm/initial_conditions/eclipse-entry.yaml
overrides:
  debug: false
run_duration_s: 900                    # used by `make scenario` (see below)
director_command_scenario: "cfg/drm/scenarios/commands/some-scenario.json"  # optional, illustrative -- none ship today
```

If a scenario omits `initial_conditions:`, it gets `nominal-baseline`, which
reproduces the original hardcoded values exactly.
`drm-nominal.yaml` and `drm-debug.yaml` now set `initial_conditions:
"nominal-baseline"` explicitly for clarity, but that was never a required
change: any scenario without the field already gets the same result.

### Why IC bins are separate from scenarios

A scenario is "what happens in this run" (component overrides, optionally an
in-run command playlist).
An IC bin is "what state the run starts from" (orbit, epoch, attitude,
per-component state like EPS charge or a fault flag).
Keeping them separate, reusable, and independently swappable is deliberate.
It is the axis a future Monte Carlo campaign needs: running one
scenario across many varied IC bins, or one IC bin across many scenario
variants, without either side changing when the other does.

### IC bin schema

```yaml
name: "eclipse-entry"
description: "..."
epoch:
  month: 9
  day: 16
  year: 2023
  hour: 23
  minute: 21
  second: 41.94
  leap_seconds: 37.0
orbit:
  periapsis_alt_km: 400.0
  apoapsis_alt_km: 400.0
  inclination_deg: 52.0
  raan_deg: 0.0
  arg_periapsis_deg: 0.0
  true_anomaly_deg: 284.395
attitude:
  angular_velocity_deg_s: [0.0, 0.0, 0.0]
  quaternion: [0.0, 0.0, 0.0, 1.0]
component_overrides: {}   # optional, see below
```

`component_overrides` is a dict keyed by component name (for example `eps:`
or `adcs:`).
Each value is merged into that component's config the same way a scenario's
own per-component config and `overrides:` dict already are
(`tools/shire-orchestrator.py`'s cascading merge: fallback, global, mission,
spacecraft, **IC bin**, scenario, scenario `overrides`, then CLI
`--cli-debug`).
It is not a second override mechanism.
It is one more layer in the existing one, which is how an IC bin can carry
EPS state-of-charge, an ADCS starting mode, or a fault-injection flag without
new plumbing.

Ground stations referenced by name (for a pass-acquisition/loss bin) come
from the single catalog `cfg/drm/ground_stations.yaml`, which also renders
`Inp_Sim.txt`'s "Ground Stations" block, so the two never drift apart.
The Radio simulator uses the same catalog (optionally narrowed by the scenario's
`radio.ground_stations` list) to decide when the spacecraft is in view, using each
station's optional `min_elev_deg` and `enabled` fields.

### Reproducibility

Every `make cfg` (and therefore every `make build`, `make start`, or `make
scenario`) writes
`build/<mission>/scenario/<scenario_name>.snapshot.yaml`: the scenario name,
the IC bin name, its fully resolved values, and the current git SHA.
That snapshot is the record of exactly what a given run started from.
`tools/shire-scenario.py` copies it into its own report directory, and
`tools/shire-perf.py --mode determinism` references it too.

### Authoring a new IC bin

There is no live solver that computes an event-based starting point (eclipse
entry/exit, ground-pass acquisition/loss) for you.
You will need to determine it yourself by doing the following:

1. Copy `cfg/drm/initial_conditions/nominal-baseline.yaml` to a new name.
2. Run the reference orbit for at least one period with 42's legacy report files enabled,
   using `SHIRE_42_REPORT_MODE=full make start`.
   The compose default is control mode, which does not write these files
   (see [42 output modes](performance.md#42-output-modes)).
   Then read 42's own real output in the `shire-42-<spacecraft>` container's `/42/InOut/`:
   `svn.42` (sun vector) and `PosN.42`/`VelN.42` (position/velocity), both in the
   inertial frame, one row per simulated timestep.
   Use `make start` rather than `python3 tools/shire-scenario.py`, because the scenario runner removes its containers when the run ends.
3. Find the crossing you want by applying 42's own test for it to that real
   data.
   For eclipse, that is the cylindrical shadow test in
   `42/Source/42ephem.c`'s "Eclipse Flag" block:
   `sun_dot_earthward = dot(svn, -pos/|pos|)`, and eclipse is true when
   `sun_dot_earthward > 0` and `(earth_radius/|pos|)^2 > 1 -
   sun_dot_earthward^2`.
4. Convert the crossing's elapsed time into orbital elements.
   For a circular orbit (periapsis altitude equal to apoapsis altitude, so
   eccentricity is exactly 0) true anomaly advances linearly with time:
   `true_anomaly(t) = true_anomaly(0) + (t / period) * 360deg`.
   Cross-check that directly against the real position vector at that row
   by rotating it into the orbital plane using the orbit's fixed, known
   inclination and RAAN.
   The resulting angle should match.
   `eclipse-entry.yaml` and `eclipse-exit.yaml` were derived exactly this
   way, and the derivation notes in `eclipse-entry.yaml` are the worked
   example.
5. Set the new bin's `epoch` to the original epoch plus the crossing's
   elapsed time, and `orbit.true_anomaly_deg` to the converted value.
   Record how you derived it in a `notes:` field.
6. Reference the new bin from a scenario's `initial_conditions:` field,
   `make cfg`, and inspect the written snapshot, or `make start` to inspect the run in the Yamcs visualization viewer.

Ground-pass acquisition/loss bins follow the same pattern, using
`cfg/drm/ground_stations.yaml`'s station position and an elevation-mask test
against the real position data instead of the eclipse test.
None are shipped yet.
Deriving one is a good follow-up.

## Running a scenario

`cfg/drm/drm.yaml` registers 17 scenarios.
List them with `make scenario` and no argument, or with `python3 tools/shire-scenario.py --list-scenarios`.
Durations are simulated seconds from `run_duration_s`, and every scenario starts from `nominal-baseline` unless the table says otherwise.

| Scenario | What it runs |
| --- | --- |
| `nominal` | A 900 second run at maximum speed with no stacks and `debug: false`. |
| `debug` | The same 900 second run as `nominal` with `debug: true` on every selected component. |
| `eclipse-entry-adcs` | A 900 second run at maximum speed from the `eclipse-entry` bin with no stacks. |
| `eclipse-exit-adcs` | A 900 second run at maximum speed from the `eclipse-exit` bin with no stacks. |
| `checkout` | The `CheckoutTest` and `ServerPauseResume` stacks for 240 seconds at 1x. |
| `cmd-tlm-validation` | The `CmdTlmLimits`, `CmdTlmStoredCommands`, and `CmdTlmPatches` stacks for 480 seconds. |
| `adcs-boot-config` | The `AdcsComponent` stack for 300 seconds. |
| `adcs-gps-time-sync` | The `AdcsGpsTimeSync` stack for 120 seconds. |
| `adcs-modes-sweep` | The `AdcsModesSweep` stack for 360 seconds. |
| `adcs-sunpoint-rotisserie` | The `AdcsSunpointRotisserie` stack for 180 seconds. |
| `adcs-target-track` | The `AdcsTargetTrack` stack for 180 seconds. |
| `adcs-time-file-fallback` | The `AdcsTimeFileFallback` stack for 180 seconds. |
| `adcs-truth-verification` | The `AdcsTruthComparison` stack for 180 seconds at 5x with a Yamcs clock parameter. |
| `eps-functional` | The `EpsPowerFunctional` stack for 1200 seconds at 2x with EPS validation. |
| `eps-backdoor-testing` | The `EpsBackdoorFunctional` and `EpsFaultInjection` stacks for 1200 seconds at 2x with EPS validation. |
| `radio-link-testing` | The `RadioLinkBackdoor` stack for 600 seconds at 2x with two ground stations and an always in view link. |
| `radio-security-testing` | The `RadioSecurity` stack for 600 seconds at 2x plus `log_assertions` on the CryptoLib and FSW logs. |

Use `<name>` below to try any of them.

### Manually, with Yamcs visualization

Set `scenario: <name>` in `build/active.yaml`, `make`
(renders the IC into the 42 config files), then `make start` (brings up the
headless 42 + Yamcs stack with that IC already baked in).
This is the path for investigating an issue or exploring the system by hand.

### Autonomously, to confirm a pass (no GUI, CI/Monte-Carlo friendly)

```bash
make scenario SCENARIO=<name>
# for example: make scenario SCENARIO=eclipse-entry-adcs
```

This selects the scenario, builds, and brings up the exact same compose
stack `make start` would.
42 runs headlessly, and nothing waits for a human.
It waits for the run to finish, bounded by a watchdog, checks that both the
Director and FSW reached their terminal marker with no crash or timeout,
tears everything down, and exits 0 (pass) or 1 (fail).
Results, container logs, and a copy of the IC snapshot land in
`build/scenario-runs/<scenario>-<UTC timestamp>/result.json`.
To run the same scenario with cFS on a physical board or QEMU, use
`make board-scenario SCENARIO=<name> BOARD=<profile>`.
That command builds and deploys target cFS while retaining host simulators, Yamcs, and the same scenario verification.
See [Board processor PIL](board-pil.md#run-a-named-scenario) for prerequisites and examples.
This is not wired into `.github/workflows/ci.yml` yet, by choice: running
the full Docker Compose stack in CI is new territory for this pipeline
(no existing job does it), and the resource and timing behavior of a
hosted runner needs validating on a draft PR before gating merges on it.
Run it on demand until that validation happens.

On its own, pass/fail here is clean completion only, with no
telemetry-value assertions.
A scenario can layer real telemetry-value checks on top via
`verify_stacks` (next section).
Without it, clean completion is all this script confirms.

### Layering telemetry verification (`verify_stacks`)

A scenario can run one or more existing YAMCS Stack (`.ycs`) files
headlessly, the same command/verify/check sequence a human would
otherwise run by hand in Procedures / Stacks in the YAMCS web UI (see
[Ground Software](../core-concepts/ground-software.md)'s "Checkout stack"
section), and fold their pass/fail into the scenario's own result:

```yaml
# cfg/drm/scenarios/checkout.yaml
verify_stacks:
  - stack: "cfg/drm/gsw/procedures/CheckoutTest.ycs"
    at_s: 0
```

`stack` paths point into `cfg/<mission>/gsw/`, not the `yamcs/` submodule:
DRM-level GSW content (this stack, plus the MDB and display files it
depends on) is authored in the outer repo and staged into the submodule
at build time by `yamcs/Makefile`'s `copy-gsw-files` target, the same
pattern `comp/<name>/gsw/` already uses for per-component content.
This keeps every editable file in one repo's history instead of needing
a submodule commit plus an outer-repo pointer bump for something that's
fundamentally DRM-mission content.

`stack` is a repo-relative `.ycs` path.
`at_s` schedules it at that many simulated seconds into the run, and
multiple entries at different times are allowed in one scenario.
Interpreting the `.ycs` file itself, `command`/`verify`/`check`/`text`
steps against YAMCS's REST API, lives in `yamcs/yamcs_commander.py
--stack <path>`, not a separate tool, so there's one implementation to
maintain for both the CLI (`--command`, `--interactive`) and
headless-stack use cases.

Each entry runs before `tools/shire-scenario.py` starts waiting for the run
to finish, and always waits for at least the first simulated-time sample
before firing.
Firing at the literal instant containers start can race FSW apps that
haven't finished subscribing to their command MIDs yet, confirmed live: a
command sent too early is silently dropped, with no error at all.
Every scheduled entry, and the run's own completion, share one overall
time budget derived from `run_duration_s`, so a stuck stack can't hang
the run forever.

Results land in `result["verification"]` in `result.json`, plus a
per-stack `verify-<stem>.json`/`.log` in the run's artifact directory.
A verification failure is worded distinctly from a clean-completion
failure, `"clean completion, but stack verification failed..."` versus
`"missing terminal marker(s)..."`, so the two are never ambiguous.

A `verify` step's `condition` entries normally compare a parameter against
a static literal `value`. `yamcs_commander.py` also supports a headless-
commander-only `"operator": "approx"` condition (`reference_parameter` +
`tolerance` instead of `value`) for asserting numeric agreement between two
*live* parameters, e.g. confirming ADCS telemetry matches `SIM_42_TRUTH`
within tolerance (`comp/adcs/gsw/procedures/AdcsTruthComparison.ycs`).
This is not part of the official YAMCS stack schema, so a `.ycs` using it still
opens fine in the YAMCS web UI, but a human running it there would see that
condition simply never resolve -- reserve "approx" for stacks driven
exclusively through `verify_stacks`, never ones also meant for manual GUI
use.
See `APPROX_OPERATOR`'s comment in `yamcs_commander.py` for the detail.

The commander also supports two headless-only conditions that compare a parameter with its own earlier value.
`unchanged` holds the step for the number of milliseconds in `value` and fails if the parameter changes, and `advances` passes once the parameter is greater than it was when the step began.
A step that uses either may not mix them with other operators, and they are reserved for stacks driven through `verify_stacks` like the others.
`ServerPauseResume.ycs` uses them to prove that the simulation clock stood still during a pause and then resumed.

Every scenario in the table above except `nominal`, `debug`, `eclipse-entry-adcs`, and `eclipse-exit-adcs` uses `verify_stacks` today.
The `adcs-*` scenarios run `comp/adcs/gsw/procedures/*.ycs` stacks, the EPS and Radio scenarios run `comp/eps` and `comp/radio` stacks, and `checkout` and `cmd-tlm-validation` run stacks from `cfg/drm/gsw/procedures/`.
`make scenario` is a developer tool.
No CI job runs `make scenario` at all yet (see the note above), so this
isn't gating anything by extension either.
Using it needs `requests` and PyYAML installed on whatever host runs
`make scenario`
(`python3 -m pip install --requirement tools/requirements-host.txt`),
for every scenario and not only those that set `verify_stacks`.

### Asserting on container logs (`log_assertions`)

Some evidence never reaches a YAMCS parameter, such as the ground CryptoLib container rejecting a tampered telemetry frame.
A scenario can assert on the captured container logs instead:

```yaml
# cfg/drm/scenarios/radio-security-testing.yaml
log_assertions:
  - container: cryptolib
    pattern: 'Crypto_TM_ProcessSecurity Failed with status = -21$'
    min: 1
    max: 1
    description: the tampered telemetry frame failed authentication, once
```

`container` is a role (`cryptolib`, `fsw`, `director`, `gsw`, `server`, or `42`), `pattern` is a regular expression matched per log line, and `min` (default 1) and `max` bound how many lines may match.
A `max` of 0 asserts absence.
Each result is recorded in `result["log_assertions"]` in `result.json`, and a failed assertion fails the run with a distinct reason.
These checks run once, after the run completes, so use them for totals rather than for ordering inside a stack.

### Tuning wall-clock speed

`run_duration_s` bounds the run in *simulated* seconds.
How long that takes in real wall-clock time depends on `SIMULITH_SPEED`,
which a scenario can set via `simulith_speed: "max"` (or a specific
multiplier).
This only affects the autonomous `make scenario` path (`shire-scenario.py`
sets it as an env var before `docker compose up`), never `make start`'s
manual GUI path, which keeps defaulting to real-time (1x) for watching.
For accelerated verification, set `verification_clock_parameter` to a Yamcs
simulation clock such as `/SIM_42_TRUTH/DYN_TIME` to record `sim_elapsed_s`
for each verify step alongside its wall-clock `elapsed_s`.

Whether raising it is safe depends on whether the scenario has
`verify_stacks`.

**No `verify_stacks`**: always safe.
The scenario exists to exercise `run_duration_s` simulated seconds of
behavior, and running that faster in wall-clock time doesn't change what's
exercised.
`nominal`, `debug`, `eclipse-entry-adcs`, and `eclipse-exit-adcs` all set
`simulith_speed: "max"` for exactly this reason.
Confirmed live: a 900-simulated-second scenario that used to take several
minutes at 1x now finishes in about a minute.

**With `verify_stacks`**: raising it is safe (see `FSW_BOOT_SETTLE_S`
below), but not automatically a net win, and needs `run_duration_s`
re-checked either way.
Some `.ycs` steps have real-wall-clock-bound timing.
Confirmed live: `CheckoutTest.ycs`'s CFDP upload/downlink steps are a
YAMCS-side service, not a Simulith tick participant, so they take the same
real seconds regardless of sim speed.
If the simulated clock outruns them, telemetry freezes
(`SIMULITH_PHASE_STOP`) before verification can observe completion, so
`run_duration_s` has to scale up with speed to preserve the same
real-time margin.
That scaling has a real cost, and it can outweigh the speed gain.
Confirmed live on `checkout`: raising `simulith_speed` to 5 (with
`run_duration_s` scaled from 240 to 600 to stay safe) did shrink
verification itself, from about 76 seconds to about 30, exactly the
tick-bound ADCS convergence step getting faster.
But the achieved simulated/real ratio wasn't linear with the requested
speed, so the larger `run_duration_s`'s "wait for the clock to run out"
phase cost about 212 seconds, more than the roughly 46 seconds saved.
`checkout` stays at the default speed because 1x measured faster
end to end, not because raising it was unsafe.
Treat this as a per-scenario question to measure, with real timestamps
from an artifact directory, not a rule to apply blindly either way.

One boot-race fix matters for any `verify_stacks` scenario at a raised
speed.
cFE apps need a fixed number of *real* seconds to start and subscribe to
their command MIDs, a real-world constant, not tied to simulated ticks.
Waiting for one simulated-time log sample (which `wait_for_simulated_time`
already does before firing the first entry) grants a fixed number of
*simulated* seconds of settle time, which is fewer real seconds at higher
speed.
Confirmed live: this caused a real failure at `simulith_speed=5` (an
early `CheckoutTest.ycs` verify step read back a nonzero counter that
should have been freshly reset), fixed by `FSW_BOOT_SETTLE_S` in
`tools/shire-scenario.py`, a real-second floor applied once, before the
first scheduled entry, independent of speed.

### Scenario keys and runner options

A scenario file accepts the keys below.
Only `scenario_name` is needed to name it, and every other key is optional.

| Key | Meaning |
| --- | --- |
| `scenario_name`, `description` | The registered name and a human description. |
| `initial_conditions` | The IC bin name, defaulting to `nominal-baseline`. |
| `overrides` | A mapping applied to every selected component. |
| `<component>:` such as `radio:` | Values merged into only that component's configuration. |
| `run_duration_s` | Simulated seconds the run lasts, default 900. |
| `simulith_speed` | `max` or a multiplier, where `--simulith-speed` overrides it. |
| `director_command_scenario` | A Director command playlist injected during the run. |
| `verify_stacks` | Stacks to run headlessly, each with a repo-relative `stack` path and an `at_s` time. |
| `verification_clock_parameter` | A Yamcs clock parameter used to record `sim_elapsed_s` for each verify step. |
| `log_assertions` | Checks on captured container logs, evaluated after the run. |
| `limits` | Telemetry limit overrides for this scenario, described in [Telemetry limits](telemetry-limits.md#where-limits-are-set). |
| `eps_validation` | Runs the EPS stacks regenerated from the resolved EPS configuration, requires fresh FSW results, and adds an event gate, an archive completeness check, and infrastructure error checks that can fail the run. |
| `retain_all_events` | Raises the cFE event burst and refill limits for the build so that the run retains every flight event. |
| `synchronize_ground_output` | Sets `SHIRE_SYNCHRONIZE_GROUND_OUTPUT` for the run unless that variable is already set in your environment. |
| `ground_output_hz` | Sets `SHIRE_GROUND_OUTPUT_HZ`, the periodic FSW downlink rate limit, which is 20 Hz when unset. |
| `accelerate_after_verification` | After the stacks finish, sends the Server backdoor speed command and then waits for the run to complete. |
| `post_verification_speed` | The speed that command requests, default 0 for unbounded. |

The EPS and Radio acceptance scenarios set `synchronize_ground_output`, `accelerate_after_verification`, and `post_verification_speed`.
The two EPS scenarios also set `eps_validation` and `retain_all_events`.
See [EPS validation](eps-validation.md) for why.

`result.json` also records a `fault_scan` entry.
It is a heuristic grep for ` ERROR` and ` CRITICAL` lines across the container logs, with a `match_count` and the first 50 matches.
It never changes the pass or fail result, so read it as a hint for diagnosis only.

`make scenario` passes only `SCENARIO`.
Call `tools/shire-scenario.py` directly for the other options.

| Option | Meaning |
| --- | --- |
| `--scenario <name>` | The scenario to run, required unless `--list-scenarios` is given. |
| `--list-scenarios` | Print every scenario registered for the mission and exit. |
| `--mission <name>` | Override the mission in `build/active.yaml`. |
| `--spacecraft <name>` | Override the spacecraft in `build/active.yaml`. |
| `--report-dir <path>` | Where to write `result.json` and logs, default `build/scenario-runs/<scenario>-<UTC timestamp>/`. |
| `--simulith-speed <n>` | Override the runtime speed, where 0 is maximum and 1 is real time. |
| `--no-build` | Skip `make build` and run `make cfg-compose-only` instead, assuming an image matching `--image-tag` already exists. |
| `--instance-id`, `--port-offset`, `--image-tag`, `--initial-conditions-file` | Isolation and image options that Monte Carlo campaigns set for each trial. |

### Confirming a scenario is deterministic

```bash
make scenario-smoke
```

This runs the currently-configured scenario twice back to back at the same
speed and reuses `tools/shire-perf.py`'s existing repeatability/fidelity
comparison, including its exact (1e-12 tolerance) terminal dynamics-state
check on `qn`, `wn`, `pos_n`, `vel_n`, and `dyn_time`, to confirm the two
runs match.
That answers "does this IC reproduce run after run," which is a different
question from "did this run complete cleanly" above.

## Monte Carlo Campaigns

The IC-bin/scenario split above is the axis a Monte Carlo campaign needs:
one fixed scenario, run across many varied (or generated and perturbed) IC
bins, reusing this page's autonomous run path unmodified for every trial.
See [Monte Carlo campaigns](monte-carlo-campaigns.md) for the campaign
YAML schema, parameter sampling, running one with `make campaign`, and its
aggregated report and dataset export.

***
Last reviewed: 20261027

# Scenarios and initial conditions

SHIRE has one scenario concept: a description of how a mission run starts
and what happens during it.
There are two ways to exercise a scenario today.

An Atlas doc scenario (`atlas/docs/scenarios/*.md`, for example
commissioning, nominal operations, or FDIR) is a scenario walked through
manually: a human reads the steps and drives 42, YAMCS, and the vehicle by
hand.
A DRM scenario (`cfg/drm/scenarios/*.yaml`, selected via `build/active.yaml`'s
`scenario:`) is the same concept exercised through configuration instead: it
is built, and can be run automatically, without a human walking through
steps.
`drm` is simply the default mission this repository ships with, and its
scenarios are the out-of-the-box example of the configured form.
There is nothing DRM-specific about the mechanism.
Any mission can define its own scenarios the same way.

The two forms are not tied to separate content forever.
An Atlas doc scenario today has no machine-readable configuration behind it,
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
(`cfg/shire-orchestrator.py`'s cascading merge: fallback, global, mission,
spacecraft, **IC bin**, scenario, scenario `overrides`, then CLI
`--cli-debug`).
It is not a second override mechanism.
It is one more layer in the existing one, which is how an IC bin can carry
EPS state-of-charge, an ADCS starting mode, or a fault-injection flag without
new plumbing.

Ground stations referenced by name (for a pass-acquisition/loss bin) come
from the single catalog `cfg/drm/ground_stations.yaml`, which also renders
`Inp_Sim.txt`'s "Ground Stations" block, so the two never drift apart.

### Reproducibility

Every `make cfg` (and therefore every `make build`, `make start`, or `make
scenario`) writes
`build/<mission>/scenario/<scenario_name>.snapshot.yaml`: the scenario name,
the IC bin name, its fully resolved values, and the current git SHA.
That snapshot is the record of exactly what a given run started from.
`cfg/shire-scenario.py` copies it into its own report directory, and
`cfg/shire-perf.py --mode determinism` references it too.

### Authoring a new IC bin

There is no live solver that computes an event-based starting point (eclipse
entry/exit, ground-pass acquisition/loss) for you.
You will need to determine it yourself by doing the following:

1. Copy `cfg/drm/initial_conditions/nominal-baseline.yaml` to a new name.
2. Run the reference orbit for at least one period (`make start`, or
   `cfg/shire-scenario.py --scenario nominal`) and read 42's own real
   output in the `shire-42-<spacecraft>` container's `/42/InOut/`: `svn.42`
   (sun vector) and `PosN.42`/`VelN.42` (position/velocity), both in the
   inertial frame, one row per simulated timestep.
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
   `make cfg`, and inspect the written snapshot, or `make start` to visually
   confirm in the 42 GUI that the run begins where you expect.

Ground-pass acquisition/loss bins follow the same pattern, using
`cfg/drm/ground_stations.yaml`'s station position and an elevation-mask test
against the real position data instead of the eclipse test.
None are shipped yet.
Deriving one is a good follow-up.

## Running a scenario

Five scenarios ship today: `nominal` and `debug` (both use
`nominal-baseline`, the default launch state), `eclipse-entry-adcs` and
`eclipse-exit-adcs` (both use the eclipse IC bins above), and `checkout`
(also `nominal-baseline`, plus a `verify_stacks` entry covered later on
this page).
Use `<name>` below to try any of them.

### Manually, with the GUI and YAMCS

Unchanged from before: set `scenario: <name>` in `build/active.yaml`, `make`
(renders the IC into the 42 config files), then `make start` (brings up the
full VNC/noVNC + YAMCS stack with that IC already baked in).
This is the path for investigating an issue or exploring the system by hand.

### Autonomously, to confirm a pass (no GUI, CI/Monte-Carlo friendly)

```bash
make scenario SCENARIO=<name>
# for example: make scenario SCENARIO=eclipse-entry-adcs
```

This selects the scenario, builds, and brings up the exact same compose
stack `make start` would.
The 42 container still starts its VNC/xterm/noVNC internals internally.
This script just never opens them, and nothing waits for a human.
It waits for the run to finish, bounded by a watchdog, checks that both the
Director and FSW reached their terminal marker with no crash or timeout,
tears everything down, and exits 0 (pass) or 1 (fail).
Results, container logs, and a copy of the IC snapshot land in
`build/scenario-runs/<scenario>-<UTC timestamp>/result.json`.
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

Each entry runs before `cfg/shire-scenario.py` starts waiting for the run
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
within tolerance (`comp/adcs/gsw/procedures/AdcsTruthComparison.ycs`). This
is not part of the official YAMCS stack schema, so a `.ycs` using it still
opens fine in the YAMCS web UI, but a human running it there would see that
condition simply never resolve -- reserve "approx" for stacks driven
exclusively through `verify_stacks`, never ones also meant for manual GUI
use. See `APPROX_OPERATOR`'s comment in `yamcs_commander.py` for the detail.

`checkout` and several `adcs-*` scenarios (`adcs-gps-time-sync`,
`adcs-boot-config`, `adcs-sunpoint-rotisserie`, `adcs-truth-verification`,
`adcs-target-track`) use `verify_stacks` today, the latter running
`comp/adcs/gsw/procedures/*.ycs` stacks.
It is a developer tool.
No CI job runs `make scenario` at all yet (see the note above), so this
isn't gating anything by extension either.
Using it needs `requests`/`yamcs-client` installed on whatever host runs
`make scenario`
(`python3 -m pip install --requirement yamcs/requirements-commander.txt`),
a prerequisite only for scenarios that set `verify_stacks`, not for
`make scenario` in general.

### Tuning wall-clock speed

`run_duration_s` bounds the run in *simulated* seconds.
How long that takes in real wall-clock time depends on `SIMULITH_SPEED`,
which a scenario can set via `simulith_speed: "max"` (or a specific
multiplier).
This only affects the autonomous `make scenario` path (`shire-scenario.py`
sets it as an env var before `docker compose up`), never `make start`'s
manual GUI path, which keeps defaulting to real-time (1x) for watching.

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
`cfg/shire-scenario.py`, a real-second floor applied once, before the
first scheduled entry, independent of speed.

### Confirming a scenario is deterministic

```bash
make scenario-smoke
```

This runs the currently-configured scenario twice back to back at the same
speed and reuses `cfg/shire-perf.py`'s existing repeatability/fidelity
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

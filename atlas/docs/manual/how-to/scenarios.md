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
It is the axis a future Monte Carlo campaign (issue #23) needs: running one
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
Issue #21 deliberately left that out.
Determine it yourself:

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

Four scenarios ship today: `nominal` and `debug` (both use
`nominal-baseline`, the default launch state), and `eclipse-entry-adcs` and
`eclipse-exit-adcs` (both use the eclipse IC bins above).
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
CI runs exactly this, on `eclipse-entry-adcs`, in the `scenario-confirm-pass`
job (`.github/workflows/ci.yml`).

Pass/fail here is clean completion only, with no telemetry-value
assertions.
That was an explicit scope decision for issue #21.
Layering tolerance-based telemetry checks on top of this, as issue #8's
ADCS audit tooling does separately for the fields it cares about, is future
work, not something this script does today.

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

## Looking ahead to Monte Carlo (issue #23)

This design's main hook for a future batch campaign is the IC-bin/scenario
split above.
A batch driver would enumerate `initial_conditions` bin names, or generate
perturbed ones matching the same schema, against one fixed scenario, reusing
everything in this page unmodified.
`cfg/shire-perf.py`'s report schema, with its provenance and per-trial
structure, is the intended pattern to extend for aggregating many trials'
outcomes, the way it already does for performance trials.

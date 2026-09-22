# Monte Carlo campaigns

A Monte Carlo campaign (issue #23) runs many trials of one fixed scenario, each against its own
perturbed Initial Condition (IC) bin, and aggregates the results.
It builds on the scenario/IC-bin split described in
[Scenarios and initial conditions](scenarios.md), reusing `cfg/shire-scenario.py`'s tested
single-run pipeline unmodified for every trial.
This page assumes you have already read that page, especially the IC bin schema.

## Campaign YAML schema

A campaign is defined in `cfg/drm/campaigns/<name>.yaml`:

```yaml
name: "eclipse-entry-soc-sweep"
description: >
  Perturbs EPS state-of-charge and orbit inclination around eclipse-entry.
scenario: "eclipse-entry-adcs"           # fixed scenario every trial runs unmodified
base_initial_conditions: "eclipse-entry" # cfg/drm/initial_conditions/<name>.yaml to perturb from
trial_count: 200
seed: 20260921
max_parallel: 2                          # optional, CLI --max-parallel overrides this

parameters:
  - path: "orbit.inclination_deg"
    distribution: {type: "normal", mean: 0.0, stdev: 2.0}
    mode: "additive"                     # base_value + sample
  - path: "attitude.angular_velocity_deg_s.0"
    distribution: {type: "uniform", low: -1.0, high: 1.0}
  - path: "component_overrides.eps.battery_initial_soc"
    distribution: {type: "choice", values: [40.0, 60.0, 80.0, 95.0]}
```

`scenario` and `base_initial_conditions` are fixed for the whole campaign.
Only the swept `parameters` vary from trial to trial.

Each parameter names a dotted `path` into the IC bin schema.
A numeric path segment indexes into a list, for example `attitude.angular_velocity_deg_s.0`.
A non-numeric segment indexes into a dict, creating missing intermediate dicts as needed.
Every shipped IC bin's `component_overrides` is `{}`, so a path like
`component_overrides.eps.battery_initial_soc` creates the `eps` dict the first time it is used.
A list index is never auto-extended.
An out-of-range index is a campaign-authoring error, reported clearly at load time.

## Distributions and modes

Three distribution types are supported, drawn from Python's standard `random` module.

* `uniform` needs `low` and `high`.
* `normal` needs `mean` and `stdev`, and optionally `min`/`max` to clamp the result.
* `choice` needs `values`, and optionally `weights`.

Each parameter also has a `mode`, controlling how the drawn sample becomes the trial's value.

* `additive` sets the value to `base_value + sample`.
  This is the default for `uniform` and `normal`, since a continuous distribution is almost
  always used to disperse an existing physical quantity around whatever the base IC bin already
  specifies.
  Campaign loading fails fast if the path doesn't already resolve to a number in the base bin.
* `absolute` sets the value to `sample`, replacing (or filling in) the base value entirely.
  This is the default for `choice`, since a discrete pick is usually a complete value, often into
  a field the base bin leaves empty.

## Determinism

A campaign's `seed` produces the same trials every time.
One `random.Random(seed)` instance samples every trial up front, in trial-index order, before any
trial runs, with parameters within a trial sampled in the YAML's listed order.
This holds regardless of `max_parallel`, since sampling happens entirely before the concurrent run
phase begins.
If sampling were interleaved with concurrent execution, draw order would depend on
scheduler/Docker timing instead of the seed.

Each trial's resolved IC bin and sampled parameter values are written under the run's report
directory, so a campaign's inputs stay inspectable and reproducible independently of its outputs.

## Metrics

By default a trial's outcome is limited to a handful of fixed fields (`pass`, `reason`,
`run_duration_s`, and so on).
A campaign can also pull a specific named value out of a trial's own `verify_stacks` results into
`campaign_report.json` and `campaign_dataset.jsonl` as a first-class column, using an optional
`metrics:` list:

```yaml
metrics:
  - name: "time_to_sun_pointed_s"
    stack: "CheckoutTest"    # matches verify-CheckoutTest.json, the .ycs file's stem
    step_index: 119          # the verify step to read, 0-based
    field: "elapsed_s"       # which field of that step's result to extract
```

`stack` names the `.ycs` file's stem, the same name `verify-<stem>.json` already uses in a
trial's report directory (see
[Layering telemetry verification](scenarios.md#layering-telemetry-verification-verify_stacks)).
`step_index` (0-based) or `step_name` selects one step, exactly one of the two required.
`field` names the key to read off that step's own result dict, most usefully `elapsed_s`, the
time (in seconds) a `verify` step spent polling before its condition passed or its timeout
elapsed.

A `verify` step's `elapsed_s` is a real convergence-time measurement whenever the step
immediately follows the command that starts whatever it's waiting for.
`cfg/drm/gsw/procedures/CheckoutTest.ycs`'s step 118 commands ADCS into `SUNSAFE`, and step 119
verifies the reported Sun vector converges, timing out at 120 seconds.
That step's `elapsed_s` is genuinely time-to-Sun-pointed, not a synthetic stand-in, which is
exactly what `cfg/drm/campaigns/test-adcs-sunpointing.yaml` measures against a swept initial
tumble rate.

A metric resolves to `null` rather than raising whenever a trial has no matching verification to
read, a build failure, a crashed trial, or a scenario without `verify_stacks` at all.
It is exploratory data layered on top of pass/fail, not a second pass/fail gate.
A metric's name must not collide with a fixed outcome field or a swept parameter's own dotted
path, since both flatten into the same dataset row.
Campaign loading fails fast on a collision, a missing required field, or setting both
`step_index` and `step_name`.

`summary.metrics` in `campaign_report.json` carries the same min/mean/max/standard-deviation
statistics the summary already computes for swept parameters and `run_duration_s`.

## Running a campaign

```bash
make campaign CAMPAIGN=<name>
make campaign CAMPAIGN=<name> MAX_PARALLEL=4
```

This is a developer tool, not wired into any CI job.
A full campaign of many trials is too slow for CI gating, matching the same reasoning that keeps
`make scenario`'s `verify_stacks` scenarios out of CI today.

A campaign run has four phases, printed as it goes.

1. **Sample.** Every trial's IC bin and parameter values are generated and written to disk.
2. **Build.** Trials are grouped by a content hash of everything that actually gets baked into an
   image (the resolved IC bin plus every enabled component's fully-cascaded config).
   Trials sharing an identical hash share one image build.
   A continuous parameter sweep (for example orbit inclination) almost never produces two
   identical trials, so expect close to one build per trial in that case.
   A `choice` sweep over a small set of values collapses naturally, since repeated values hash
   identically.
   Either way, each build is a real `docker build`, not a free reuse, though an IC-only
   difference only forces a fast, layer-cache-hit rebuild of the affected images, not a full
   recompile.
3. **Run.** Trials run concurrently, bounded by `max_parallel`.
   Each trial's compose stack reserves roughly 17 CPU-units and 8.5GB across its six services, so
   raising `max_parallel` is a real resource commitment, not a free lunch.
   The campaign prints its estimated concurrent footprint at startup.
   Every trial reuses `cfg/shire-scenario.py`'s own `--instance-id`/`--port-offset`/`--image-tag`/
   `--no-build`/`--initial-conditions-file` flags to run with unique container names, networks,
   volumes, and published ports, so concurrent trials never collide.
4. **Aggregate.** Once every trial has finished (or errored), results are read back in
   trial-index order, never completion order, so the report's shape is stable across runs
   regardless of which trial happens to finish first.

A trial that errors (a failed build, a crashed subprocess, a missing result) is recorded with
`"pass": null` and a non-null `"error"`, and never aborts the rest of the campaign.

### Interrupting a campaign

A `SIGINT`/`SIGTERM` (`Ctrl-C`) stops a running campaign cleanly.
No trial not yet started launches after the signal.
Every already-running trial's containers, networks, and volumes are removed immediately, tagged
by a `shire.instance` Docker label every trial's compose stack carries.
A campaign also sweeps for that same label unconditionally at startup, clearing any orphaned
resources left behind by a prior crashed run before it begins.

## `campaign_report.json` and `campaign_dataset.jsonl`

A campaign run writes its report directory under `build/monte-carlo-runs/<campaign>-<UTC
timestamp>/` by default, or `--report-dir`.
It contains `ic/` (each trial's generated IC bin and resolved parameters), `trials/` (each
trial's full `cfg/shire-scenario.py` artifacts, identical in shape to a plain `make scenario`
run's report directory), `campaign_report.json`, and `campaign_dataset.jsonl`.

`campaign_report.json` extends `cfg/shire-perf.py`'s provenance-plus-trials report pattern.

```json
{
  "schema_version": 1,
  "created_utc": "...",
  "git": { "...": "cfg/shire_provenance.py's git_metadata()" },
  "host": { "...": "cfg/shire_provenance.py's host_metadata()" },
  "campaign": {"name": "...", "scenario": "...", "base_initial_conditions": "...",
               "trial_count": 200, "seed": 20260921, "parameters": []},
  "trials": [
    {"trial_index": 7, "ic_file": "ic/trial-0007.yaml", "report_dir": "trials/trial-0007",
     "parameters": {"orbit.inclination_deg": 51.8}, "pass": true, "reason": "...",
     "run_duration_s": 903.4, "verification_passed": null, "fault_scan_match_count": 0,
     "error": null}
  ],
  "summary": {"trial_count": 200, "pass_count": 190, "pass_rate": 0.95,
             "failure_reason_histogram": {}, "parameters": {}}
}
```

Every trial appears in `trials`, including errored ones, so the trial count always matches the
campaign's configured `trial_count`.
A downstream tool computing failure-versus-parameter correlation never sees a silently biased
sample from dropped rows.
`summary` stays descriptive: pass rate, a failure-reason histogram, and min/mean/max/standard
deviation for run duration and every swept parameter.
It deliberately stops short of correlation or regression, since that's what the exported dataset
is for.

`campaign_dataset.jsonl` flattens `trials` into one JSON object per line, suitable for
`pandas.read_json(path, lines=True)` or a plain per-line `json.loads` loop.
Each swept parameter appears as its own key, using its literal dotted path, alongside `campaign`,
`git_sha`, `trial_index`, and the same outcome fields as the report.
Every trial gets exactly one line, including errored ones.

To regenerate the dataset from an existing report without running anything:

```bash
python3 cfg/shire-campaign.py --from-report <path to campaign_report.json> --jsonl-out <path>
```

## Looking ahead to constellations

Today's compose stack models exactly one spacecraft's director/FSW/42/cryptolib set per trial.
A future constellation campaign, simulating multiple vehicles together in one trial, is a
different compose-topology change, not an extension of this mechanism.
The per-trial instance token this design uses is deliberately independent of spacecraft identity,
so a future vehicle-id axis could be added alongside it without renaming or restructuring
anything this page describes.

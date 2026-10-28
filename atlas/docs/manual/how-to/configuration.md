# Configuration

SHIRE configuration selects a mission, spacecraft, scenario, CLI component and logging mode.
The checked in YAML files are inputs.
The orchestrator writes derived state under `build/` and renders component headers in place.

## Configuration and tooling directories

`cfg/` holds the user configuration inputs and editable templates.
This includes mission and spacecraft YAML, scenarios, campaigns, initial conditions, ground software definitions, Compose templates, and 42 defaults.
Flight tables, headers, startup files, toolchain settings, and mission build hooks stay in `cfg/shire_defs/`.
The cFS coverage tests remain in `cfg/shire_defs/coverage-tests/` because they are registered through the mission build.

`tools/` holds shared executable utilities, their Python helpers and tests, shell scripts, Python requirements, and Docker build recipes.
Component-specific and Yamcs utilities stay with their owning subsystems.
Use the existing `make` commands for normal configuration, builds, scenarios, campaigns, performance checks, and replay.
Direct utility invocations use `python3 tools/<script>.py` from the repository root.

## Configuration hierarchy

Configuration performs two related jobs.
The active selection chooses which mission, spacecraft, and scenario files apply.
The orchestrator then resolves the device settings for every selected component.

### Select the configuration inputs

| Source | Current location | Purpose |
| --- | --- | --- |
| Repository catalog | `cfg/shire-config.yaml` | Lists missions and supplies repository defaults for spacecraft, FSW, GSW, and the fallback component set. |
| Active selection | `build/active.yaml` | Chooses the mission, spacecraft, scenario, CLI component, logging mode, and FSW and GSW directories for the next generated configuration. |
| Selected mission | `cfg/drm/drm.yaml` | Identifies the spacecraft and scenario files available to the DRM. |
| Selected spacecraft | `cfg/drm/spacecraft/*.yaml` | Selects the component set and supplies spacecraft specific device values. |
| Selected scenario | `cfg/drm/scenarios/*.yaml` | Supplies scenario specific component values and values applied to every selected component. |

`build/active.yaml` selects these inputs but does not directly supply component device values.
The orchestrator stores the selected inputs in `build/build.yaml` so later build steps can use the same configuration snapshot.

### Resolve component device settings

For each selected component, the orchestrator starts with that component's fallback settings and applies the following sources in order.
Each source replaces a value only when it defines the same key.
Values for keys that are not mentioned carry forward unchanged.

| Step | Source | Effect |
| --- | --- | --- |
| 1 | `comp/<name>/support/device_config.yaml` | Establishes the component defaults. |
| 2 | Component values in `cfg/shire-config.yaml` | Replaces matching defaults when repository values are present. |
| 3 | Component values in the selected mission file | Replaces matching values when mission values are present. |
| 4 | Component values in the selected spacecraft file | Applies the device values for that spacecraft. |
| 5 | The Initial Condition bin's `component_overrides` | Applies component state that the scenario's selected bin carries, such as EPS charge. |
| 6 | Component values in the selected scenario file | Applies values specific to that component and scenario. |
| 7 | The scenario `overrides` mapping | Applies the same named values to every selected component. |
| 8 | `make cfg-cli` | Forces `debug: true` for the active `cli:` component before rendering its device header. |

The current `sat-2` nominal Radio configuration provides a concrete example:

* The Radio fallback supplies the UDP ports, buffer limits, timeout, and initial SPI and GPIO values.
* `sat-2.yaml` supplies the spacecraft SPI and GPIO values, including chip select 1 and GPIO pins 12 and 13.
* The nominal scenario replaces `debug: true` with `debug: false` for every selected component.
* With `cli: radio`, `make cfg-cli` changes Radio's `debug` back to `true` after the scenario is applied.

The resolved Radio settings therefore combine values from several files rather than taking one complete configuration block from a single file.

The Radio RF link model follows the same layering.
A scenario can set the keys below under its `radio:` block.
`tools/shire_radio_config.py` validates them with the ranges shown, `tools/shire-orchestrator.py` resolves the station list, and the result is rendered into the Radio simulator's `device_cfg.h`, so re-run `make cfg` after changing them.
The backdoor can change the same settings while a simulation runs.

| Key | Valid values and default |
| --- | --- |
| `link_mode` | `auto` is in view when a listed ground station sees the spacecraft, `always` is always in view, and `never` is never in view, with `always` as the default. |
| `uplink_delay_ms`, `downlink_delay_ms` | Integer milliseconds from 0 to 4294967295, default 0. |
| `uplink_drop_ppm`, `downlink_drop_ppm` | Integer packet drop in parts per million from 0 to 1000000, default 0. |
| `link_seed` | Integer from 0 to 4294967295 that seeds the drop generator, default 1. |
| `link_queue_depth` | Integer from 1 to 4096 for packets in flight per direction, default 64. |
| `ground_stations` | A list of up to 16 distinct names from `cfg/drm/ground_stations.yaml`, defaulting to every Earth station in the catalog. |

The Radio also resolves `uplink_encryption` and `downlink_encryption`, both `true` by default.
The orchestrator renders them into the Radio's `device_cfg.h` for the flight app and into the `shire-cryptolib` service environment in `shire-compose.yaml` for the ground, so both ends of the link are always configured from the same resolved values.
Changing either one needs `make cfg` and a rebuild of the flight software.

## Generate configuration

From the repository root:

```bash
make cfg
```

This runs `tools/shire-orchestrator.py` inside the configured SHIRE build image.
The current default image reference uses the `0.0.0` tag rather than an immutable digest.
On the first run, the orchestrator creates `build/active.yaml` with DRM, `sat-1`, the nominal scenario, the Demo CLI, logging disabled, the default FSW and GSW directories, and the default board profile.

The orchestrator currently produces:

* `build/active.yaml`: editable active selection
* `build/build.yaml`: merged configuration snapshot consumed by the build script
* `build/<mission>/shire-compose.yaml`: DRM Compose file
* `build/<mission>/cli-compose.yaml`: component CLI compose file
* `build/<mission>/42_config/Inp_Sim.txt`: rendered 42 simulation input
* `build/<mission>/<spacecraft>/shire_defs/`: copied cFS mission definitions, with the CPU1 startup script pruned for the selected spacecraft
* `comp/<component>/shared/device_cfg.h`: rendered device header for each selected component that provides a template
* `build/<mission>/scenario/<scenario>.snapshot.yaml`: the scenario name, the Initial Condition bin name, its fully resolved values, and the current git SHA, written on every `make cfg`

The generated `build/` tree is intentionally ignored by Git.
The rendered `device_cfg.h` files are also ignored.
Change their source YAML or Jinja template rather than treating generated output as source.

## Change the active target

Run `make cfg` once, then edit `build/active.yaml`.
For example:

```yaml
mission: drm
spacecraft: sat-1
scenario: nominal
cli: demo
log_mode: none
fsw_dir: cfs
gsw_dir: yamcs
board: qemu-zynq
```

`log_mode` sets the `SIMULITH_LOG_MODE` of the Director and FSW containers.
Valid values are `stdout`, `file`, `both`, and `none`, and the generated default is `none`.
With `none`, the Simulith log lines from the Director and FSW are silenced.
The Server container is always forced to `stdout`.
Set `log_mode: stdout` and run `make cfg` before diagnosing Director or FSW startup (see [Inspect the DRM](docker.md#inspect-the-drm)).
`board` selects the profile in `cfg/boards/` that board commands use, and `qemu-zynq` is the default.

Valid DRM spacecraft in the current checkout are `flatsat`, `sat-1`, and `sat-2`.
The DRM registers 17 scenarios in `cfg/drm/drm.yaml`, and `make scenario` with no argument lists them.
See [Scenarios and initial conditions](scenarios.md#running-a-scenario) for what each one runs.
The current spacecraft component sets are:

| Spacecraft | Components |
| --- | --- |
| `flatsat` | demo, EPS, radio |
| `sat-1` | ADCS, demo, EPS, radio |
| `sat-2` | ADCS, EPS, radio |

`build/active.yaml` can also carry four additional fields a human should not
hand-edit: `instance`, `port_offset`, `image_tag`, and
`initial_conditions_file`.
`tools/shire-scenario.py` and `tools/shire-campaign.py` set and clear these
themselves to namespace a Monte Carlo campaign trial's containers, ports,
image tag, and generated Initial Condition bin, clearing them again on
normal completion or an interrupt.
See [Monte Carlo campaigns](monte-carlo-campaigns.md) for what they do.
A plain `make cfg`/`make build`/`make start` run never sets or needs them.
If a prior campaign process was killed hard enough to skip its own
cleanup (a `kill -9` or a host crash, not a plain `Ctrl-C`), they can be
left behind, in which case clear them from `build/active.yaml` by hand
before the next plain run.

After any edit, regenerate and inspect:

```bash
make cfg
make list
```

`make list` reports the resolved mission, spacecraft, scenario, and component build features from `build/build.yaml`.

## Scenario overrides

The nominal scenario currently sets `debug: false`.
The debug scenario sets `debug: true`.
The `overrides` dictionary is applied to every selected component, so reserve it for keys that every component template understands.
Put values for only one component under that component's name in the scenario file.

These configuration scenarios select component settings before the environment
is built.
They are distinct from the versioned Director command scenario used by the
[performance regression workload](../core-concepts/simulations.md#performance-regression-runs).
See [Scenarios and initial conditions](scenarios.md) for a full walkthrough
of both, plus a third, unrelated use of the word "scenario" in the Atlas docs.

## Scenario initial conditions

A scenario (`cfg/drm/scenarios/*.yaml`) can also set `initial_conditions:
<name>` to select a starting orbit, epoch, attitude, and optionally
per-component state from a reusable Initial Condition (IC) bin under
`cfg/drm/initial_conditions/*.yaml`, instead of the fixed values that used to
be hardcoded in `Orb_SHIRE.txt`, `SC_SHIRE.txt`, and `Inp_Sim.txt`.
A scenario that omits `initial_conditions` gets `nominal-baseline`, which
reproduces those original hardcoded values exactly, so existing scenarios
needed no changes.
See [Scenarios and initial conditions](scenarios.md) for the full IC bin
schema, how to author a new one, and how to run one autonomously (no GUI) to
confirm it completes cleanly or check that it reproduces run after run.

## Compose selection

`make start` reads `build/active.yaml` to derive the mission and starts `build/<mission>/shire-compose.yaml`.
`make cli-start` uses `build/<mission>/cli-compose.yaml`.
If either path is missing or stale, run `make cfg` again before diagnosing Docker.
A Monte Carlo campaign trial's compose file lives at `build/<mission>/<instance>/shire-compose.yaml` instead, one numbered subdirectory per trial, so concurrent trials' compose files can coexist on disk.
`make start`/`make cli-start` never read that path, since `instance` is always cleared outside a campaign trial.

## Verification checklist

Before a long build, verify:

1. `build/active.yaml` contains the intended target.
2. `build/build.yaml` points to the expected mission, spacecraft, and scenario input files.
3. The selected spacecraft contains the intended component list.
4. The generated CPU1 startup script contains only the intended component applications.
5. Each rendered `device_cfg.h` contains the expected bus, handle, timeout, and debug values.
6. The generated compose files reference the intended mission and image tag, ordinarily the spacecraft name unless a Monte Carlo campaign trial set an explicit build-key tag.

***
Last reviewed: 20261027

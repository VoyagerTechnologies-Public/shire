# Configuration

SHIRE configuration selects a mission, spacecraft, scenario, CLI component, logging mode, and graphics setting.
The checked in YAML files are inputs.
The orchestrator writes derived state under `build/` and renders component headers in place.

## Configuration hierarchy

Configuration performs two related jobs.
The active selection chooses which mission, spacecraft, and scenario files apply.
The orchestrator then resolves the device settings for every selected component.

### Select the configuration inputs

| Source | Current location | Purpose |
| --- | --- | --- |
| Repository catalog | `cfg/shire-config.yaml` | Lists missions and supplies repository defaults for spacecraft, FSW, GSW, and the fallback component set. |
| Active selection | `build/active.yaml` | Chooses the mission, spacecraft, scenario, CLI component, logging mode, graphics setting, and FSW and GSW directories for the next generated configuration. |
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
| 5 | Component values in the selected scenario file | Applies values specific to that component and scenario. |
| 6 | The scenario `overrides` mapping | Applies the same named values to every selected component. |
| 7 | `make cfg-cli` | Forces `debug: true` for every selected component before rendering its device header. |

The current `sat-2` nominal Radio configuration provides a concrete example:

* The Radio fallback supplies the UDP ports, buffer limits, timeout, and initial SPI and GPIO values.
* `sat-2.yaml` supplies the spacecraft SPI and GPIO values, including chip select 1 and GPIO pins 12 and 13.
* The nominal scenario replaces `debug: true` with `debug: false` for every selected component.
* `make cfg-cli` changes only `debug` back to `true` after the scenario is applied.

The resolved Radio settings therefore combine values from several files rather than taking one complete configuration block from a single file.

## Generate configuration

From the repository root:

```bash
make cfg
```

This runs `cfg/shire-orchestrator.py` inside the configured SHIRE build image.
The current default image reference uses the `latest` tag rather than an immutable digest.
On the first run, the orchestrator creates `build/active.yaml` with DRM, `sat-1`, the nominal scenario, the Demo CLI, logging disabled, and graphics enabled.

The orchestrator currently produces:

* `build/active.yaml`: editable active selection
* `build/build.yaml`: merged configuration snapshot consumed by the build script
* `build/<mission>/shire-compose.yaml`: DRM Compose file
* `build/<mission>/cli-compose.yaml`: component CLI compose file
* `build/<mission>/42_config/Inp_Sim.txt`: rendered 42 simulation input
* `build/<mission>/<spacecraft>/shire_defs/`: copied cFS mission definitions, with the CPU1 startup script pruned for the selected spacecraft
* `comp/<component>/shared/device_cfg.h`: rendered device header for each selected component that provides a template

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
graphics: true
fsw_dir: cfs
gsw_dir: yamcs
```

Valid DRM spacecraft in the current checkout are `flatsat`, `sat-1`, and `sat-2`.
Valid DRM scenarios are `nominal` and `debug`.
The current spacecraft component sets are:

| Spacecraft | Components |
| --- | --- |
| `flatsat` | demo, EPS, radio |
| `sat-1` | ADCS, demo, EPS, radio |
| `sat-2` | ADCS, EPS, radio |

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

## Compose selection

`make start` reads `build/active.yaml` to derive the mission and starts `build/<mission>/shire-compose.yaml`.
`make cli-start` uses `build/<mission>/cli-compose.yaml`.
If either path is missing or stale, run `make cfg` again before diagnosing Docker.

## Verification checklist

Before a long build, verify:

1. `build/active.yaml` contains the intended target.
2. `build/build.yaml` points to the expected mission, spacecraft, and scenario input files.
3. The selected spacecraft contains the intended component list.
4. The generated CPU1 startup script contains only the intended component applications.
5. Each rendered `device_cfg.h` contains the expected bus, handle, timeout, and debug values.
6. The generated compose files reference the intended mission and spacecraft image tags.

***
Last reviewed: 20260817

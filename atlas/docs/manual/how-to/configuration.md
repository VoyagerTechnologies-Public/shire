# Configuration

SHIRE configuration selects a mission, spacecraft, scenario, CLI component, logging mode, and graphics setting.
The checked in YAML files are inputs.
The orchestrator writes derived state under `build/` and renders component headers in place.

## Configuration hierarchy

| Layer | Current location | Purpose |
| --- | --- | --- |
| Repository | `cfg/shire-config.yaml` | Lists available missions, the default spacecraft, the default CLI, FSW and GSW directories, and global component entries. |
| Mission | `cfg/drm/drm.yaml` | Lists the DRM spacecraft and scenarios, plus mission level component fallbacks. |
| Spacecraft | `cfg/drm/spacecraft/*.yaml` | Selects components and supplies spacecraft specific device settings. |
| Scenario | `cfg/drm/scenarios/*.yaml` | Supplies scenario values and optional global component overrides. |
| Component fallback | `comp/<name>/support/device_config.yaml` | Supplies default device settings for one component. |
| Active selection | `build/active.yaml` | Selects the mission, spacecraft, scenario, CLI component, logging, graphics, and optional FSW or GSW directories and is created automatically when absent. |

For component configuration, later layers win:

```text
component fallback
  -> repository
  -> mission
  -> spacecraft
  -> scenario component values
  -> scenario overrides
  -> CLI debug override, when make cfg-cli is used
```

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
* `build/<mission>/shire-compose.yaml`: full lab compose file
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

# Component Mold

The component mold copies an existing reference component and applies mechanical name and identifier substitutions.
It creates a starting point, not a fully integrated or flight ready component.

From the repository root:

```bash
make mold COMP=new_sensor
```

Component names must start with a letter and contain only letters, numbers, and underscores.
The script normalizes the name to lowercase.
If the target already exists, the script asks before deleting and replacing it.

## What the mold changes

By default, `cfg/shire-comp-mold.py`:

* copies `comp/demo/` to `comp/<name>/`
* omits Git metadata, build directories, generated `device_cfg.h`, and common temporary files
* replaces `demo`, `Demo`, and `DEMO` in supported text files
* renames files and directories containing those name forms
* changes the Demo UART path and handle from 5 to 9
* remaps Demo's four cFS message IDs from `0x18FA`, `0x18FB`, `0x08FA`, and `0x08FB` to `0x18FC`, `0x18FD`, `0x08FC`, and `0x08FD`

The alternate source option is:

```bash
python3 cfg/shire-comp-mold.py new_sensor --source other_component
```

The mechanical substitutions are tailored to Demo.
Review every change when another source component is used.

## Required integration review

After generation, review and update at least:

1. **Device model and protocol**
   * `comp/<name>/shared/`, `src/`, `cli/`, and `sim/`
   * `support/device_config.yaml` and `support/device_config.j2`
   * unique UART/I2C/SPI/GPIO endpoint selection
2. **cFS identity and build**
   * unique command, request, telemetry, and performance IDs
   * `cfg/shire_defs/targets.cmake`
   * `cfg/shire_defs/cpu1_cfe_es_startup.scr` and any target specific startup scripts
3. **Mission tables**
   * applicable scheduler, TO_LAB, DS, LC, SC, Radio, or CF tables under `cfg/shire_defs/tables/`
4. **Mission selection**
   * add the component to the intended spacecraft file under `cfg/drm/spacecraft/`, or to the mission fallback list when appropriate
5. **Ground definitions**
   * update `comp/<name>/gsw/` XTCE, displays, and procedures
   * add the component XTCE entry to `yamcs/src/main/yamcs/etc/yamcs.shire.yaml`
6. **Tests and evidence**
   * replace copied Demo expectations in `test-fsw/` and `test-sim/`
   * run focused CLI, simulator tests, FSW tests, and full lab checks

The generated UART handle and message IDs are merely less likely to collide with Demo.
They are not allocated from a registry and do not prove uniqueness.
Search the entire repository before accepting any identifier.

## Validate the result

```bash
make cfg
make list
make cli
make test-sim
make test-fsw
make
```

Inspect `build/build.yaml`, the rendered `device_cfg.h`, the generated CPU1 startup script, and the YAMCS mission database before treating the component as integrated.
Continue with [Component Hardware Development](../../scenarios/component-hardware-development.md) to move through CLI simulation, cFS simulation, focused hardware checkout, simulator reconciliation, and board cFS integration.

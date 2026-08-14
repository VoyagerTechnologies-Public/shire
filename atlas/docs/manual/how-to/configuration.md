# Configuration
This page documents the SHIRE configuration and the orchestrator used to generate mission and spacecraft configuration artifacts (compose files, component device configs and FSW startup scripts).

Goals of the orchestrator:

* Provide one place (`cfg/active.yaml`) to pick the active mission/spacecraft/scenario.
* Produce a merged, human-readable snapshot (`cfg/build.yaml`) for inspection and CI.
* Render component device configuration from Jinja2 templates using a cascading merge of defaults and overrides.
* Generate `cli-compose.yaml` and `shire-compose.yaml` from Jinja2 templates tuned for the selected spacecraft and log mode.
* Copy and adapt FSW baseline config files (from `cfg/shire_defs`) into a per-build directory `build/<mission>/<spacecraft>/shire_defs` and prune startup scripts to only include enabled components.

Where the important files live:

* `cfg/shire-config.yaml` - global repository-level configuration (lists missions, global defaults, and available spacecraft). See `cfg/shire-config.yaml` for the concrete structure used by your repo.
* `cfg/<mission>/<mission>.yaml` - mission-level configuration; defines scenarios and mission-wide component lists and defaults (example: `cfg/drm/drm.yaml`).
* `cfg/<mission>/scenarios/<scenario>.yaml` - scenario config providing scenario-specific component settings and optional `overrides`.
* `cfg/active.yaml` - the single source of truth for what the lab/build should target (mission, spacecraft, scenario). The orchestrator will create sensible defaults here if missing.
* `cfg/build.yaml` - the merged snapshot produced by the orchestrator. Always inspect this file after a run to verify what was merged.

Primary concepts and flow:

* The orchestrator loads `cfg/shire-config.yaml` (global), the selected mission config, the selected scenario, and an optional spacecraft config.
* It writes a merged snapshot to `cfg/build.yaml`.
* For each component listed in the active spacecraft (or mission fallback), it builds a final component config via cascading merges.
	* Precedence (later wins): fallback defaults from `comp/<component>/support/device_config.yaml` -> global entries -> mission entries -> spacecraft entries -> scenario entries -> scenario `overrides`.
* If `comp/<component>/support/device_config.j2` exists and the merged component config is non-empty, the orchestrator renders it with Jinja2 and writes the result to `comp/<component>/shared/device_cfg.h`.
* The orchestrator also renders `cfg/cli-compose.yaml` and `cfg/shire-compose.yaml` from `cli-compose.j2` and `shire-compose.j2` using the selected `cli_component`, `spacecraft`, `mission`, and `log_mode`.
* A copy of baseline FSW configuration files (`cfg/shire_defs/*`) is made into `build/<mission>/<spacecraft>/shire_defs` and the `cpu1_cfe_es_startup.scr` is pruned to remove apps not enabled for the selected spacecraft.

Running the orchestrator:

```bash
make cfg
```

After a successful run you should see:

* `cfg/build.yaml` - merged snapshot of the active configuration
* `cfg/cli-compose.yaml` - CLI compose generated for the `cli` component
* `cfg/shire-compose.yaml` - lab compose tuned to the selected spacecraft and log mode
* `comp/<component>/shared/device_cfg.h` - generated component configuration header(s) (when templates are present)
* `build/<mission>/<spacecraft>/shire_defs/` - copied FSW config and an adjusted `cpu1_cfe_es_startup.scr`

Changing the active target

Edit `cfg/active.yaml` to pick which mission/spacecraft/scenario the lab should use. The orchestrator will create `active.yaml` with sensible defaults if the file is missing.

Minimal example of `cfg/active.yaml`:

```yaml
mission: drm
spacecraft: sat-1
scenario: nominal
cli: demo        # optional: which CLI container/component to enable
log_mode: none   # optional: 'none'|'basic'|'verbose' used when rendering compose
```

After editing `cfg/active.yaml` run `make cfg` to regenerate the derived files.

Component template and device config details:

* Fallback defaults: a component may include a `comp/<component>/support/device_config.yaml` which provides baseline settings used when no other overrides are present.
* Per-component template: if `comp/<component>/support/device_config.j2` exists the orchestrator will render it with the final `config` dict and write the result to `comp/<component>/shared/device_cfg.h`.
* Use the Jinja-rendered `config` object inside the template (example inside a template: `{{ config.port }}` or loop through `{{ config.parameters }}`).

Spacecraft-specific startup script pruning:

When a spacecraft is selected the orchestrator copies `cfg/shire_defs/*` into `build/<mission>/<spacecraft>/shire_defs`.
It then edits `cpu1_cfe_es_startup.scr` to remove the `CFE_APP` lines for components that are not enabled for the spacecraft (adcs, demo, eps, radio are examples).
This lets a single repository support multiple spacecraft variants without changing source FSW files.

Compose generation:

`cli-compose.j2` and `shire-compose.j2` live in `cfg/` and are used to generate `cfg/cli-compose.yaml` and `cfg/shire-compose.yaml`.
These are the compose files your `make start` target will use to build the lab (ensure `make` uses those outputs).
The templates accept variables: `mission`, `spacecraft`, `cli_component`, and `log_mode`.

Debugging and validation tips:

* If rendered files are missing, check the orchestrator output as it prints which templates were found/used and where outputs were written.
* Inspect `cfg/build.yaml` to see exactly what the orchestrator merged.
	* It is the single authoritative snapshot used to render templates.
* Ensure YAML keys are correctly nested under the component name (templates receive a flattened `config` dict for the component).
* If a generated `cpu1_cfe_es_startup.scr` is incorrect, inspect `build/<mission>/<spacecraft>/shire_defs/cpu1_cfe_es_startup.scr` to see which apps were pruned and why.

Automation and CI

* Use `make cfg` as a CI step to produce `cfg/build.yaml` and generated artifacts; fail the build if artifacts are missing or malformed.
* Because the orchestrator writes reproducible files, CI can run system-level tests against the generated `shire-compose.yaml` and `cli-compose.yaml` to validate a particular mission/spacecraft/scenario combination.

## Configuration Summary

The orchestrator is intentionally small and deterministic: it makes your repo configurable at mission/spacecraft/scenario level, renders per-component headers from Jinja2 templates, and writes the compose files and per-spacecraft FSW configs a lab run needs.
Inspect `cfg/build.yaml` after each run — it's the quickest way to verify your changes.

----
Last updated: 20251203

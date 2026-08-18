# Ground Software

SHIRE uses YAMCS as its ground software.
The included instance supplies a browser UI, command and telemetry links, XTCE mission definitions, archives, procedures, displays, timelines, and CFDP services for the DRM.

## Current YAMCS configuration

The instance configuration and mission database are under `yamcs/src/main/yamcs/`:

* `etc/yamcs.yaml` exposes the HTTP service on port 8090 and defines persistent data buckets.
* `etc/yamcs.shire.yaml` defines the SHIRE instance, data links, recorders, parameter archive, replay service, CFDP service, streams, and mission database files.
* `mdb/cfs/` contains reusable cFS XTCE definitions.
* `mdb/sim_42_truth.xtce` defines the Director's 42 truth packet.
* `displays/` and `procedures/` contain operator artifacts shared across components.

Supporting tools and runtime defaults are under `yamcs/`:

* `timelines/` contains the default DRM timeline view and its parameter bands.
* `yamcs_commander.py` and `yamcs_timeline.py` provide command line access to the YAMCS API.

Files owned by each component remain under `comp/<name>/gsw/`.
During `make gsw`, the YAMCS Makefile copies component XTCE, displays, and procedures into the YAMCS build context before creating the runtime image.

## Links

The instance defines three operator facing paths:

* **Debug:** commands go directly to CI_LAB on UDP 1234, and TO_LAB telemetry returns on UDP 1235.
* **Radio:** commands pass through CryptoLib and the Radio simulator, and telemetry returns through the reciprocal pipeline.
* **Simulation:** YAMCS can send component backdoor commands to the Director and receive periodic 42 truth telemetry.

See [System Architecture](architecture.md#ground-links) for the complete endpoint table.

## Displays and procedure stacks

The reference components currently provide these YAMCS procedures:

* `AdcsComponent.ycs`
* `DemoComponent.ycs` and `DemoFDIR.ycs`
* `EpsComponent.ycs`, `EpsAllOn.ycs`, and `EpaAllOff.ycs`
* `RadioComponent.ycs`

A `CheckoutTest.ycs` stack is also provided to allow for confirmation that the DRM is operating nominally as changes are made.
On first run with empty buckets, the YAMCS image entrypoint populates the stacks bucket with packaged procedures and the displays bucket with packaged displays.
Existing persistent buckets are not overwritten automatically.
Rebuilding the image does not replace a stack or display in a populated bucket.
Treat procedure results as evidence only when the procedure, configuration, SHIRE revision, and outcome are recorded.

### Checkout stack

`CheckoutTest.ycs` is the broadest automated smoke check currently packaged with YAMCS.
It resets counters, sends NOOP commands, and checks command counters for selected cFE services and applications.
The current sequence covers ES, EVS, SB, TBL, TIME, DS, FM, LC, SC, SCH, Radio, EPS, Demo, and ADCS.
It also enables ADCS, presents the reported and 42 Sun vectors in one operator check, commands `SUNSAFE`, and verifies the resulting ADCS vector limits.

The checkout stack changes spacecraft state.
It resets command counters, enables ADCS, and leaves ADCS in `SUNSAFE` when it completes.
It does not check every loaded application, exercise every component behavior, transfer a file through CFDP, or implement the full [Commissioning](../../scenarios/commissioning.md) walkthrough.

Open **Procedures / Stacks / CheckoutTest.ycs** in YAMCS and review the steps before running it.
Use a newly started lab and enable only the intended outbound command link.
Both `debug-out` and `radio-out` consume the same realtime command stream, so leaving both enabled can deliver a command twice and invalidate the stack's absolute counter checks.
The stack advances after YAMCS reports `Acknowledge_Queued` and then relies on its telemetry verification steps to establish the result.

## Timelines

The `shire` instance enables the YAMCS timeline service.
The checked in `yamcs/timelines/complete_timeline.json` defines one shared view named **Design Reference Mission** with these bands:

| Band | Current source |
| --- | --- |
| Enabled: ADCS | `/ADCS/DEVICE_ENABLED` |
| Enabled: Demo Payload | `/DEMO/DEVICE_ENABLED` |
| Radio Mode | `/RADIO/RADIO_DEVICE_Mode` |
| Sun Vector | `/ADCS/SUN_X`, `/ADCS/SUN_Y`, and `/ADCS/SUN_Z` |

The current file contains no scheduled timeline items.
The default view therefore shows state history and the ADCS Sun vector rather than a planned command sequence.

At container startup, the entrypoint waits for the YAMCS API and loads `complete_timeline.json`.
It falls back to `default.json` only when the complete file is absent.
Loading deletes and recreates bands and views that have the same names as the checked in defaults.
Differently named views and bands are left in place.
Export changes that must survive a restart instead of relying on edits to the packaged view.
A timeline load failure produces a warning without stopping YAMCS.

The timeline manager defaults to `http://localhost:8090` and the `shire` instance.
Run these commands from the repository root after YAMCS is healthy:

```bash
python3 yamcs/yamcs_timeline.py list
python3 yamcs/yamcs_timeline.py save timeline_backup.json
python3 yamcs/yamcs_timeline.py load timeline_backup.json
```

The manager also supports `export` and `import` for directory based copies.
Its `delete-all --confirm` action deletes timeline views, so save any required state first.
Use `python3 yamcs/yamcs_timeline.py --help` for the complete interface and the alternate URL and instance options.

## YAMCS Commander

`yamcs/yamcs_commander.py` is a developer utility for exploring the active mission database and issuing commands through the YAMCS realtime processor.
It does not start YAMCS or select an outbound link.
Inspect the YAMCS link state before using it because every enabled telecommand link attached to `tc_realtime` can receive the issued command.

Install its host dependencies when they are not already available:

```bash
python3 -m pip install --requirement yamcs/requirements-commander.txt
```

The utility defaults to `http://localhost:8090`, instance `shire`, and processor `realtime`.
Use fully qualified command names returned by the active mission database:

```bash
python3 yamcs/yamcs_commander.py --list
python3 yamcs/yamcs_commander.py --interactive
python3 yamcs/yamcs_commander.py --command /CFE_ES/CFE_ES_COMMANDS/CFE_ES_NOOP
python3 yamcs/yamcs_commander.py --command /SC/SC_COMMANDS/SC_START_RTS --args "RTSID=6"
```

Interactive mode supports `list`, filtered `list <text>`, `info`, and `send` commands.
One shot arguments use comma separated `name=value` pairs.
The script uses `yamcs-client` when that package can initialize and otherwise sends through the REST API.
An issued command ID or queued acknowledgment proves that YAMCS accepted the request, not that flight software completed it.
Confirm execution with command history, events, and fresh telemetry.

## Start and inspect

After the full build:

```bash
make start
```

Open [http://localhost:8090](http://localhost:8090), choose the SHIRE instance, and inspect **Links** before commanding.
Both `debug-in` and the appropriate command path should be available for the workflow being run.
Open **Timeline** to inspect the **Design Reference Mission** view.
Open **Procedures / Stacks** to inspect the shared checkout stack and the component stacks copied into the image.

YAMCS data is stored in the generated compose volume named for the mission.
`make stop` stops the stack without intentionally deleting that volume.
Cleanup targets can remove persisted state, so inspect the top level Makefile before using them.

## Security boundary

The included configuration is a local development baseline: it exposes HTTP, contains a placeholder secret, and is not hardened as an operational mission control deployment.
Add authentication, authorization, TLS, network controls, secret management, backup, and operational monitoring before using a derived deployment outside an isolated development environment.

***
Last reviewed: 20260817

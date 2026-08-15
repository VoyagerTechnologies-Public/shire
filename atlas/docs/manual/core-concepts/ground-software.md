# Ground Software

SHIRE uses YAMCS as its ground software.
The included instance supplies a browser UI, command and telemetry links, XTCE mission definitions, archives, procedures, displays, timelines, and CFDP services for the DRM.

## Current YAMCS configuration

The main configuration is under `yamcs/src/main/yamcs/`:

* `etc/yamcs.yaml` exposes the HTTP service on port 8090 and defines persistent data buckets.
* `etc/yamcs.shire.yaml` defines the SHIRE instance, data links, recorders, parameter archive, replay service, CFDP service, streams, and mission database files.
* `mdb/cfs/` contains reusable cFS XTCE definitions.
* `mdb/sim_42_truth.xtce` defines the Director's 42 truth packet.
* `displays/` and `procedures/` contain operator artifacts shared across components.

Files owned by each component remain under `comp/<name>/gsw/`.
During `make gsw`, the YAMCS Makefile copies component XTCE, displays, and procedures into the YAMCS build context before creating the runtime image.

## Links

The instance defines three operator facing paths:

* **Debug:** commands go directly to CI_LAB on UDP 1234, and TO_LAB telemetry returns on UDP 1235.
* **Radio:** commands pass through CryptoLib and the Radio simulator, and telemetry returns through the reciprocal pipeline.
* **Simulation:** YAMCS can send component backdoor commands to the Director and receive periodic 42 truth telemetry.

See [System Architecture](architecture.md#ground-links) for the complete endpoint table.

## Procedures and displays

The reference components currently provide these YAMCS procedures:

* `AdcsComponent.ycs`
* `DemoComponent.ycs` and `DemoFDIR.ycs`
* `EpsComponent.ycs`, `EpsAllOn.ycs`, and `EpaAllOff.ycs`
* `RadioComponent.ycs`

On first run with empty buckets, the YAMCS image entrypoint populates the stacks bucket with packaged procedures and the displays bucket with packaged displays.
Existing persistent buckets are not overwritten automatically.
Treat procedure results as evidence only when the procedure, configuration, SHIRE revision, and outcome are recorded.

## Start and inspect

After the full build:

```bash
make start
```

Open [http://localhost:8090](http://localhost:8090), choose the SHIRE instance, and inspect **Links** before commanding.
Both `debug-in` and the appropriate command path should be available for the workflow being run.

YAMCS data is stored in the generated compose volume named for the mission.
`make stop` stops the stack without intentionally deleting that volume.
Cleanup targets can remove persisted state, so inspect the top level Makefile before using them.

## Security boundary

The included configuration is a local development baseline: it exposes HTTP, contains a placeholder secret, and is not hardened as an operational mission control deployment.
Add authentication, authorization, TLS, network controls, secret management, backup, and operational monitoring before using a derived deployment outside an isolated development environment.

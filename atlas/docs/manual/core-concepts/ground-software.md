# Ground Software (GSW)

The Ground Software (GSW) in SHIRE provides the mission control, telemetry visualization, command uplink, archives, and automation needed to operate a design reference mission from the ground. 
SHIRE uses YAMCS (Yet Another Mission Control System) as the primary GSW component.
YAMCS is an open-source, production-capable framework for telemetry, commanding, and mission operations: https://yamcs.org/.

## Why YAMCS

* Full-featured: telemetry display, parameter archives, command queues, procedures/stacks, and scripting.
* Flexible data model: supports CCSDS space packets and custom mappings used by cFS payloads.
* Extensible: plugin points for custom processors, protocol adapters, and archives.
* Web-based UI: operators and developers can access the GSW from a browser.
* Light weight: minimal overhead to run on older developer machines.

## How GSW fits into SHIRE

In SHIRE the GSW (YAMCS) connects to the rest of the lab through a small set of well-defined interfaces:

* Telemetry ingest and command uplink: YAMCS receives telemetry frames and injects telecommands via the simulated network pipeline handled by CryptoLib and the Director.
* Archive and database: YAMCS stores parameter archives and telemetry for post-processing, analysis, and replay.
* Integration: YAMCS talks to the Director and the FSW via UDP transports (see [Architecture](./architecture.md) for port mapping) and expects the simulated radio/transport frames to be delivered by CryptoLib/Director.

Typical network ports used in SHIRE (from [Architecture](./architecture.md)):

* Debug or Lab Interface
    * A lab interface enabling quick commanding and continuous telemetry monitoring.
* Radio Interface
    * Standard interface to the space vehicle for command and control.
* Simulation Director Backdoor
    * Backdoor to inject faults into the simulators for testing.

## Quick start (run & access)

1. Follow the instructions in [Getting Started](../handbook/getting-started.md) to build and start the lab.
2. After `make start` completes, open YAMCS in your browser at: http://localhost:8090
3. Verify telemetry is flowing by viewing the debug telemetry page and checking that parameters update in real time.
4. Use the Procedures / Stacks UI to run pre-defined commissioning or checkout procedures (see [Commissioning](../../scenarios/commissioning.md)).

## Common operator workflows

* Inspect telemetry and parameter trends: create dashboards and charts to monitor mission state.
* Send commands: use the command queue UI or scripts to inject telecommands; observe acknowledgements via C&DH HK packets.
* Run procedures: automate sequences (e.g., commissioning, instrument checkout) with Stacks/Procedures and watch for RTS/ACKs from the FSW.
* Archive and replay: archive telemetry for post-mortem analysis and replay archived streams into the system for repeatable tests.
* File transfers: use the cFS CF application to schedule and monitor file downlinks (see Commissioning guide for an example).

## Configuration and customizations

* Parameter / TM mapping: YAMCS requires parameter definitions that map CCSDS packets and fields to named parameters.
    * SHIRE ships with a set of definitions for the design reference mission; extend these as needed in your mission profile.
* Procedures and stacks: author YS (YAMCS script) stacks for common operational sequences and add them to the mission repository under `docs/scenarios`.
* Authentication and users: for local labs the default YAMCS setup is permissive; for production-like setups configure users, roles, and HTTPS in the YAMCS configuration.

## Troubleshooting

* No telemetry in YAMCS
	* Ensure the main lab containers are running: `docker ps` or `docker stats`.
	* Check the Server and Director logs: `docker logs shire-server-sat-1` and `docker logs shire-director` (container names may vary).
	* Confirm the Director and CryptoLib are bound to the expected UDP ports (see `cfg` directory and `architecture.md`).
* Commands not received by FSW
	* Verify the radio is enabled (RTS flow) and that the radio mode is correct (e.g., DUPLEX for file transfers).
	* Inspect the FSW console (`docker attach shire-server-sat-1`) for incoming command logs and error messages.
* Archive or DB problems
	* Check YAMCS server logs for database connectivity errors and ensure the configured DB is reachable and initialized.

----
Last updated: 20251202

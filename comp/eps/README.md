# SHIRE Component - Electrical Power System
This repository is an example Electrical Power System (EPS) component for the SHIRE environment.

## Overview
Command line interface (CLI), flight software (FSW), ground software (GSW), and simulation (SIM) directories are included in this repository.

The EPS component is an I2C device with a fixed ID of 0x1E and a maximum speed of 400kHz.
The EPS is a system as it includes a solar array, batteries, and power rails / switches to the space vehicle.
Eight switches are available for routing to specific components.
Each command sent to the device requires a CRC-8-CCITT polynomial 0x07 that is calculated over the I2C address through the end of the payload.
The specific command format is as follows:
* uint8, I2C address (7-bit), 0x1E
* uint8, command
  * (0) No operation
  * (1) Get housekeeping
  * (2) Set switch OFF
  * (3) Set switch ON
* uint8, payload
  * Unused except for set switch commands
* uint8, CRC-8-CCITT polynomial 0x07

EPS housekeeping is published once per second.
Energy integrates at every simulated tick, independently of housekeeping.
The housekeeping format is as follows:
* uint8, battery voltage (Vmin + count × (Vmax − Vmin) / 255, default 16–24 V)
* uint8, battery temperature (250C / 255 = 0.9803921568627451C per count)
* uint8, solar array voltage (32V / 255 =0.12549019607843137V per count)
* uint8, solar array temperature (250C / 255 = 0.9803921568627451C per count)
* uint8, switch 0 state (0 off, 1 on, else fault)
* uint8, switch 0 voltage (32V / 255 =0.12549019607843137V per count)
* uint8, switch 0 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 1 state (0 off, 1 on, else fault)
* uint8, switch 1 voltage (32V / 255 =0.12549019607843137V per count)
* uint8, switch 1 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 2 state (0 off, 1 on, else fault)
* uint8, switch 2 voltage (32V / 255 =0.12549019607843137V per count)
* uint8, switch 2 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 3 state (0 off, 1 on, else fault)
* uint8, switch 3 voltage (32V / 255 =0.12549019607843137V per count)
* uint8, switch 3 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 4 state (0 off, 1 on, else fault)
* uint8, switch 4 voltage (32V / 255 =0.12549019607843137V per count)
* uint8, switch 4 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 5 state (0 off, 1 on, else fault)
* uint8, switch 5 voltage (32V / 255 =0.12549019607843137V per count)
* uint8, switch 5 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 6 state (0 off, 1 on, else fault)
* uint8, switch 6 voltage (32V / 255 =0.12549019607843137V per count)
* uint8, switch 6 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 7 state (0 off, 1 on, else fault)
* uint8, switch 7 voltage (32V / 255 =0.12549019607843137V per count)
* uint8, switch 7 current (10A / 255 = 0.0392156862745098A per count)
* uint8, CRC-8-CCITT polynomial 0x07

### Command Line Interface
The CLI can be configured to connect to either the hardware or simulation.
The simulated CLI builds the configured mission simulators along with the selected EPS CLI client.
This includes DEMO, ADCS, and radio when wired to EPS in DRM, so switch commands control their simulated supplies.
After changing the configuration or build code, run `make cli` before `make cli-start` to rebuild the images.
This enables direct checkouts these without interference.
Note that `make cfg` (tools/shire-orchestrator.py) must be run at the top level SHIRE to produce the required `./shared/device_cfg.h`.

### Flight Software
The core Flight System (cFS) flight software application receives commands from the software bus.
Two message IDs exist for commands:
* 0x18D0 - Commands
  * (0) No operation
  * (1) Reset counters
  * (3) Set switch OFF
  * (4) Set switch ON
* 0x18D1 - Requests
  * (0) Request telemetry

Two message IDs exist for telemetry:
* 0x08D0 - Application Housekeeping
* 0x08D1 - Component Telemetry

### Ground Software
The XTCE file provided details the CCSDS Space Packet Protocol format used for commanding and telemetry.

### Simulation
The simulation available is built as a library that is loaded by the shire-director for use.
This maintains the state of the simulation and enables communication to the FSW via simulith.
Similar to the CLI, the `make cfg` call at the top level shire-lab is required prior to building.


## Simulator wiring

DRM maps DEMO to switch 0 at 3.3 V, ADCS to switch 4 at 12 V, and radio to switch 6 at 24 V.
Mapped rails start on and unused rails start off.
These are illustrative simulation defaults.
Rail current is summed load watts divided by configured rail voltage.
Switches without device or passive demand draw no power.
`base_load_w` models always-on platform demand in addition to switched loads.
It is currently configured as 2 W for illustrative platform demand, remains active with all rails off, and contributes only to total battery consumption, not rail current.
In AUTO solar mode, eclipse sets solar generation to zero.
Battery energy integrates `(solar watts - base watts - switched watts) * elapsed seconds / 3600` each tick.
The default 40 Wh battery maps SOC linearly onto 16–24 V, and FSW battery voltage now uses all 256 byte values across that configured range.
Battery voltage resolution is `(battery_voltage_max - battery_voltage_min) / 255`, about 0.03137 V with the default 16–24 V range.
The one-byte field and CRC placement are unchanged.
Rail and solar voltages retain their 32/255 V scaling.
Resolved Yamcs calibration follows the configured battery range.
With the configured 2 W base load and 1.5 W idle device demand, one full voltage count spans about 2.7 simulated minutes of eclipse discharge.
The default passive 20 W load bank on switch 5 reduces this to about 24 simulated seconds when enabled.
The load bank starts off, has no component simulator, and contributes to switch current and battery consumption.
Default solar capacity is 12 W, scaled by positive body-X solar incidence and set to zero in eclipse.
With idle devices and peak illumination, the 8.5 W charging surplus produces a full voltage count in about 1.1 simulated minutes.
The 20 W bank exceeds solar capacity, so enabling it demonstrates discharge even in sunlight.
A solar override can keep charging in eclipse until AUTO mode is restored.

Configure `eps.switches` and `eps.loads` in the mission, spacecraft, initial condition, or scenario YAML using the existing configuration precedence.
The default DEMO, ADCS, and radio assignments and mode power values live under `eps.default_loads` in `comp/eps/support/device_config.yaml`.
DRM enables these defaults with `eps.wire_default_loads: true`; only available components are mapped.
Override `eps.default_loads` to change the defaults, or use `eps.loads` to override a selected device's settings.
Both mappings merge individual device fields across configuration layers.
With `wire_default_loads: false`, only devices explicitly listed in `eps.loads` are mapped; their omitted fields still inherit from `default_loads`.
The resolver contains no device wattage or default switch assignment values.
A switch list contains exactly eight entries with `label`, `voltage_v`, and `startup_on`, plus optional `load_power_w` for a passive switched load.
`switch_load_power_w` supplies eight default passive watt values, independently of device mappings.
Explicit switch `load_power_w` values override that list, including zero to remove a load.
Passive and mapped demand share the rail current limit of 10 A.
Several loads can share one switch.
Each component has one assignment.

```yaml
eps:
  wire_default_loads: true
  battery_initial_soc: 0.8
  max_solar_power_w: 12.0
  base_load_w: 4.0  # Always-on platform watts, independent of the eight rails
  switch_load_power_w: [0, 0, 0, 0, 0, 20, 0, 0]
  loads:
    demo:
      switch: 0
      mode_power_w: [0.8, 0.8, 0.8, 0.8, 0.8, 0.8]
      boot_power_w: 0.8
      boot_delay_s: 0
      power_scale: 1
    adcs:
      switch: 4
      mode_power_w: [0.5, 2.5, 2.5, 2.5, 2.5, 2.5]
    radio:
      switch: 6
      mode_power_w: [0.2, 4, 1.5, 4.5, 0.2, 0.2]
```

ADCS mode slots are disabled, BDOT, SUNSAFE, NADIR, TARGET, and INERTIAL.
Radio slots 0 through 3 are sleep, transmit, receive, and duplex.
Boot power defaults to the highest configured mode power and boot delay defaults to zero.
Voltages must fit the 32 V encoding and peak rail current must fit 10 A.
SOC is a fraction from 0 to 1.
Resolved configuration is archived with scenario results and contributes to campaign build keys.

Transitions requested by flight commands are queued during EXECUTE.
After workers quiesce at COMMIT, the director dispatches received backdoors into the queue after completed flight transactions, then applies the queue before actuation.
An OFF then ON within one tick still power cycles the load.
An ON applied to an already powered load preserves its state and boot deadline.
Endpoints stay open while powered off or booting, and requests complete as modeled failures without payloads.
Power cycles clear private data and configuration.
ADCS returns disabled and clears queued actuation before committing zero wheel torque and MTB dipole.
Radio requires both EPS supply and GPIO enable and clears its RF buffers on power loss.

## Simulator backdoors

All commands use the existing director UDP service on port 50060, targeting `eps_sim`.
The frame is `BACKDOOR`, one target-length byte, the target bytes, big endian command ID and payload length as uint16 values, and the exact payload.
Fixed width integers are big endian.
Selectors are byte sized. Yamcs generates component choices from the resolved topology; their wire IDs remain DEMO=0, ADCS=1 and RADIO=2.
Counts are uint16 values.
Yamcs names are `/EPS/BACKDOOR_EPS_` followed by the suffix below.

| ID | Suffix | Payload |
| --- | --- | --- |
| 1 | `SET_SOC` | `SOC_PPM` as uint32, 0 to 1000000 |
| 2 | `SET_SOLAR` | `MODE` as uint8, AUTO=0 or OVERRIDE=1, followed by `POWER_MW` as uint32 within configured solar capacity |
| 3 | `SET_SWITCH` | `SWITCH` and `STATE` as uint8, indices 0 to 7, OFF=0 or ON=1 |
| 4 | `SET_LOAD_SCALE` | `COMPONENT` as uint8, DEMO=0, ADCS=1, RADIO=2, followed by `SCALE_PPM` as uint32, 0 to 1000000000, subject to peak current limits |
| 5 | `SET_SWITCH_FAULT` | `SWITCH` and `FAULT` as uint8, NORMAL=0, STUCK_OFF=1 or STUCK_ON=2 |
| 6 | `CORRUPT_HK_CRC` | `COUNT` as uint16, zero cancels |
| 7 | `FAIL_REQUESTS` | `SELECTOR` as uint8, ANY=0, HK=1, ON=2 or OFF=3, followed by `COUNT` as uint16, zero cancels |
| 8 | `CLEAR_FAULTS` | Empty |
| 9 | `RESET` | Empty |

SOC and scale use millionths and solar power uses milliwatts.
Unknown selectors, invalid values, incorrect lengths and malformed framing are rejected.
Stuck switch faults retain the requested state separately from the effective state.
Clearing a fault applies the latest requested state.
Request failures do not change physical state and do not consume pending CRC corruption.
CRC corruption is consumed only when a housekeeping response is emitted.
The FSW retains its last valid housekeeping after failed reads or CRC checks.

`CLEAR_FAULTS` retains energy, solar override and load scales.
`RESET` restores startup SOC, rails, scales, AUTO solar, device configuration and the deterministic noise seed.
It power cycles mapped loads without resetting simulation time, 42, transport resources or FSW application counters.
Diagnostic counters and application history remain monotonic.
Commands received while paused apply after resuming at the next COMMIT.
Backdoor commands are sent once. Stacks confirm their observable effects in fresh FSW telemetry. Generation, command ID, result and simulation time remain optional console diagnostics.

## Demonstration and recovery

Use the direct debug command and telemetry links for EPS testing.
Simulator commands use the existing `sim-backdoor` link and `tc_backdoor` stream.
EPS simulator state and application diagnostics appear as `EPS_SIM_STATE` and `EPS_BACKDOOR` records in the director console; they are not Yamcs telemetry or MDB parameters.
`EPS_SIM_STATE` is emitted only with EPS debug enabled. `SIMULITH_EPS_DEBUG=1` explicitly enables snapshots at runtime, and `SIMULITH_EPS_DEBUG=0` suppresses them. Manual and automated stacks do not require snapshots or console observation. Backdoor application logs remain available with snapshots disabled.
Use `python3 tools/shire_eps_console.py --container shire-director-sat-1 --follow` to view named diagnostics from console records (use the instance-specific director name for isolated runs).
The EPS display and stack conditions show flight telemetry only.
All EPS stacks first issue `/SHIRE_GROUND/USE_DEBUG_UPLINK` to enable `debug-out` and disable `radio-out` for normal commanding, including radio outages. This removes dependence on the initial command-link states.
In `EpsPowerFunctional.ycs`, the normal EPS flight NOOP exercises RF through the existing `radio-out` link.
The nominal stack automatically enables `radio-out` and disables `debug-out` around each marked EPS flight NOOP, then restores their previous states before requesting housekeeping. Its ground-only `/SHIRE_GROUND/USE_RADIO_UPLINK` and `/SHIRE_GROUND/RESTORE_UPLINK` commands work in both the Yamcs browser and headless runner. No manual link changes are needed during execution. If a run stops between those commands, Yamcs restores the saved states after 30 wall-clock seconds; `RESTORE_UPLINK` also allows immediate recovery. Radio housekeeping reports RF byte totals; disabling `radio-out` blocks ground uplink, while radio downlink can continue.
After updating, restart Yamcs to load the ground-control service and commands, and re-import the EPS procedures. Existing uploaded stacks are retained and are not overwritten by startup.
`EpsBackdoorFunctional.ycs` and `EpsFaultInjection.ycs` keep flight commands on debug throughout, including recovery, with `radio-out` disabled.

```sh
make scenario SCENARIO=eps-functional
make scenario SCENARIO=eps-backdoor-testing
make campaign CAMPAIGN=eps-wiring-smoke
make campaign CAMPAIGN=eps-nominal MAX_PARALLEL=2
make campaign CAMPAIGN=eps-backdoor-faults MAX_PARALLEL=2
```

The three generated stacks are `EpsPowerFunctional.ycs`, `EpsBackdoorFunctional.ycs`, and `EpsFaultInjection.ycs`.
The fault stack can run independently after a fresh start. Before running it in Yamcs, enable `debug-in` and `sim-backdoor` and keep the simulation playing. The stack selects the debug command path automatically. Its initial preflight proves EPS commands work with the radio supply off, and it initializes the mapped FSW devices. Command advancement waits one second instead of five; the fault stack keeps commands on debug throughout recovery.
Manual stacks use standard Yamcs conditions on EPS and consumer FSW telemetry.
Generated stacks need no console companion files and the runner reads FSW results only.
Stacks request application housekeeping explicitly before checking FSW results.
Fresh successful or failed transactions remain required.
It requires new FSW acquisition timestamps, advancing successful device transaction counters when powered, and advancing device error counters during outages.
Backdoor effects are checked through FSW rail states, battery/solar voltage, configured current readings and new device error counts, without retries.
Every run retains resolved settings, phase times, FSW verification samples, flight events, logs and a native Yamcs archive.
Unexpected errors fail acceptance.
Device I/O errors are permitted only within identified outage, fault or recovery phases.
EPS-specific binary traces and independent per-tick audits have been removed. Acceptance requires passing fresh FSW checks and startup restoration. FSW telemetry cannot establish exact simulator energy or zero actuation/traffic at every tick.
Automated EPS scenarios explicitly enable synchronized ground output because fresh runs without it produced FSW telemetry and wakeup queue-limit errors. The option synchronizes FSW downlink draining and does not require console observation. Ordinary manual starts retain the default asynchronous 20 Hz downlink behavior.

If interrupted during an RF probe, issue `/SHIRE_GROUND/RESTORE_UPLINK` or wait 30 wall-clock seconds for automatic link restoration. Then resume the simulation if paused and issue `BACKDOOR_EPS_RESET` once.
Optional console diagnostics expose reset generation, requested/effective rails, cleared fault counts, AUTO solar and startup scales.
Confirm the effective rails and recovery directly in fresh EPS/consumer FSW housekeeping.
Wait for mapped loads to finish booting and verify fresh device transactions.
Keep control on debug until radio traffic is confirmed.
If application cannot be observed, restart the isolated simulation and retain the failed run's evidence.

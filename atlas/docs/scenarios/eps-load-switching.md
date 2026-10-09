# EPS Load Switching

## Objective

Demonstrate EPS flight command switching, mapped device shutdown and recovery, mode dependent electrical demand, simulator backdoor application, and injected fault recovery.
The executable scenarios use the configured Simulith wiring and preserve the closed simulation loop.

## Configuration

DRM maps DEMO to switch 0 at 3.3 V, ADCS to switch 4 at 12 V, and radio to switch 6 at 24 V.
Mapped rails start on and unused rails start off.
These are configurable illustrative defaults.
Read `comp/eps/README.md` in the checkout for the wiring schema and backdoor wire format.

Several loads can share one switch.
Each load belongs to one switch.
Power demand is specified in watts per operating mode, with configurable boot demand, boot delay and power scale.
Energy integrates using simulated elapsed time at every tick.
Housekeeping publication does not control integration.
`eps.base_load_w` adds always-on platform demand to the switched loads.
It is currently configured as 2 W for illustrative platform demand, must be finite and nonnegative, and remains active when every rail is off.
It consumes battery energy without contributing to any switch current.
Set it in the mission or spacecraft YAML, for example by setting `eps.base_load_w` to `4.0`, then regenerate configuration and rebuild the simulators.

The default battery stores 40 Wh and maps SOC linearly onto 16 to 24 V.
The FSW battery voltage has a resolution of `(battery_voltage_max - battery_voltage_min) / 255`, about 0.03137 V per count with these defaults.
It uses the full eight-bit field across the configured battery range, with resolved Yamcs calibration and unchanged packet layout.
Rail and solar voltage scaling remain 32/255 V.
At the configured 3.5 W demand, including the 2 W base load with DEMO powered, ADCS disabled, and radio asleep, a full voltage count takes about 2.7 simulated minutes in eclipse.
Switch 5 defaults to a passive 20 W load bank at 12 V, labeled **Load bank**, with no attached component simulator.
It starts off and can be enabled or disabled through normal EPS flight switch commands.
When enabled alongside idle devices, total demand becomes 23.5 W and one voltage count takes about 24 simulated seconds in eclipse.
Its current appears in FSW switch 5 housekeeping and its label appears in the EPS visualization.
Configure the eight default passive loads through `eps.switch_load_power_w`, or override an individual switch with `load_power_w`.
Mapped device demand is additional to these passive loads.
Default assignments and mode power values are configured under `eps.default_loads` in `comp/eps/support/device_config.yaml`: DEMO on switch 0, ADCS on switch 4, and radio on switch 6.
DRM's `eps.wire_default_loads: true` selects defaults for available devices.
Mapped rails start on unless explicit switch startup states override them.
Mission, spacecraft, and scenario YAML can override individual `default_loads` fields or provide higher-priority `eps.loads` settings.
Setting `wire_default_loads: false` maps only explicitly listed `eps.loads` devices, while retaining their omitted settings from `default_loads`.
Passive loads can share rails with devices and are included in the 10 A peak rail limit.
Default solar capacity is 12 W, with output scaled by positive body-X solar incidence.
The model assumes a single array surface normal along body +X, matching ADCS SUNSAFE's +X Sun-pointing target.
Generation is `12 * max(0, cos(angle))` watts outside eclipse.
At 0, 30, 60, and 90 degrees off-pointing this gives 12, about 10.4, 6, and 0 W.
SUNSAFE increases ADCS demand from 0.5 to 2.5 W, giving about 5.5 W total nominal consumption with the load bank off and radio asleep.
Direct Sun-pointing then leaves 6.5 W for charging, producing a full battery voltage count about every 1.4 simulated minutes.
At 60 degrees, 6 W generation leaves just 0.5 W for charging.
Greater off-pointing can cause discharge outside eclipse.
Solar voltage housekeeping currently indicates generation present or absent and does not measure generated watts or pointing efficiency.
At peak illumination with idle devices, the 8.5 W charging surplus produces a full voltage count in about 1.1 simulated minutes.
Enabling the load bank exceeds solar generation even at peak illumination.
Energy still decreases every tick while the reported voltage remains on one count.
Solar generation is zero in eclipse under AUTO solar mode.
A solar backdoor override continues to supply its configured power until AUTO is restored.
The graph shows these FSW voltage readings, so a flat segment does not by itself indicate stalled energy integration.

## Executable demonstrations

```sh
make scenario SCENARIO=eps-functional
make scenario SCENARIO=eps-backdoor-testing
```

`eps-functional` generates and runs `EpsPowerFunctional.ycs` from resolved wiring.
It exercises all eight switches through real flight commands, combinations, repeated ON commands, shutdown, boot, recovery and mode demand.
`eps-backdoor-testing` runs `EpsBackdoorFunctional.ycs` and `EpsFaultInjection.ycs`.
These exercise EPS state controls and finite fault injections, confirm their FSW-observable effects, and restore startup settings.
For a manual Yamcs demonstration, start a fresh simulator and run either stack independently.
Before pressing Run, enable `debug-in` and `sim-backdoor` in Yamcs Links.
Every EPS stack first issues `/SHIRE_GROUND/USE_DEBUG_UPLINK` to enable `debug-out` and disable `radio-out`.
This prevents normal flight commands from following the radio path after its EPS supply is cut.
Keep the simulation playing throughout the stack.
`EpsFaultInjection.ycs` selects the debug path, clears injections, proves flight commanding and housekeeping work with the radio rail off, and initializes the mapped FSW devices.
Each preflight check has a five-second timeout and identifies an unavailable debug path.
Both backdoor and fault stacks keep command traffic on debug throughout, including recovery.
Their startup command disables `radio-out` automatically.
The nominal stack exercises RF traffic separately.
The one-second command advancement delay replaces the previous five-second delay in manual Yamcs runs.
Their standard Yamcs conditions inspect FSW telemetry only.
EPS simulator commands use the existing `sim-backdoor` interface.
Simulator diagnostics and backdoor application records are read from the director console, outside the Yamcs MDB.
`EPS_SIM_STATE` snapshots are printed only when EPS `debug` is enabled.
Set `SIMULITH_EPS_DEBUG=1` before starting a stack to enable snapshots explicitly, or `SIMULITH_EPS_DEBUG=0` to suppress them.
Manual stacks and automated scenarios do not require snapshots or console observation.
Use `python3 tools/shire_eps_console.py --container shire-director-sat-1 --follow` for optional troubleshooting with debug enabled.
Generated procedures contain standard FSW checks and need no `.console.json` companions.
The director does not collect EPS-specific audit files.

Control and observation use debug during radio outages.
In `EpsPowerFunctional.ycs`, the standard EPS flight NOOP uses the existing `radio-out` link to exercise the encrypted radio path.
The stack surrounds each probe with ground-only `/SHIRE_GROUND/USE_RADIO_UPLINK` and `/SHIRE_GROUND/RESTORE_UPLINK` commands.
Yamcs saves the current link states, disables `debug-out`, enables `radio-out`, and restores the saved states before housekeeping requests.
Both the browser and headless runner execute the same commands.
No operator intervention is needed at RF probes.
After updating, restart Yamcs and re-import the EPS procedures because existing uploaded stacks are retained at startup.
Each ground command acknowledges success only after checking the link states.
If execution stops between selection and restoration, Yamcs restores the saved states after 30 wall-clock seconds.
To recover immediately, issue `/SHIRE_GROUND/RESTORE_UPLINK` from Yamcs, then restart the stack from a fresh simulation.
An expired selection causes a later restoration command to fail, so the stack cannot silently pass an interrupted probe.
These commands remain inside Yamcs and send no simulator or flight packets.
Radio housekeeping reports RF receive and transmit totals.
Downlink transmission can continue while `radio-out` is disabled because that link controls ground uplink only.
Powered radio probes must reach the EPS flight application and radio probes during an outage must be rejected.
The stacks require successful device transactions after startup and recovery.
The headless runner requires fresh FSW acquisition timestamps and advancing successful transaction counters while powered, or device error counters during deliberate outages.
Reports record FSW values and acquisition timestamps.
Stacks request application housekeeping explicitly before checking FSW results.
Fresh successful or failed transactions remain required.
Flight housekeeping rail voltage and current are checked against configured demand within one encoding count.

Automated EPS scenarios explicitly set `synchronize_ground_output: true`.
This option controls whether FSW telemetry draining participates in the simulation completion barrier.
It does not control console printing or wait for an operator to read output.
Fresh runs without this barrier produced FSW telemetry and wakeup queue-limit errors, including during unbounded completion after the stacks.
The barrier keeps scheduled downlink drains complete before time advances.
Ordinary manual starts retain the default asynchronous 20 Hz downlink behavior.
An explicit `SHIRE_SYNCHRONIZE_GROUND_OUTPUT` environment value overrides the scenario setting.
Validation builds retain all flight events and reject unexpected errors.
`retain_all_events: true` raises the runtime EVS burst and refill limits.
Unit-test builds keep the baseline EVS limits so the cFE squelching tests can exercise counter saturation without overflowing their test arithmetic.
The 1200-second simulated run budget accommodates fresh FSW housekeeping at normal and accelerated timing.
Verification measures freshness from before each command, followed by accelerated completion.
Use `--simulith-speed 1` or `--simulith-speed 2` with `tools/shire-scenario.py` to repeat a scenario from a fresh start.
Recovery durations measure wall time from `RECOVERY_BEGIN` to confirmed startup restoration using recorded stack timestamps.
This measures the recovery phase, including commanding and FSW readback delays.

## Fault semantics

Stuck switches retain requested and effective state separately.
A flight switch command can succeed while the physical switch remains stuck.
Clearing the fault applies the latest requested state.
Failed requests leave device state unchanged.
CRC corruption affects only emitted housekeeping responses and never replaces valid FSW housekeeping.

`CLEAR_FAULTS` clears injections while retaining energy and environmental or load overrides.
`RESET` restores startup energy, switch states, power scales, AUTO solar, device configuration and noise seed.
Simulation time, 42 state, transports, FSW counters and diagnostic history continue.
Backdoors received while paused apply at the first COMMIT after resuming.
Commands are not automatically retried.
FSW-observable effects must pass their stack conditions.
Backdoor application IDs, generations and internal fault counts remain optional console diagnostics.
Settings without distinct FSW telemetry, such as solar AUTO selection or cleared internal counters, are not independently confirmed by these stacks.

## Campaigns

```sh
make campaign CAMPAIGN=eps-wiring-smoke MAX_PARALLEL=2
make campaign CAMPAIGN=eps-nominal MAX_PARALLEL=2
make campaign CAMPAIGN=eps-backdoor-faults MAX_PARALLEL=2
```

The wiring smoke campaign has five trials.
The nominal campaign has 100 trials with seed `20261006`.
The fault campaign has 20 trials with seed `20261007`.
Both sweep SOC, battery capacity, solar capacity, load scales, boot delays and component assignments across eight switches.

Acceptance requires every required trial to complete and pass, complete named FSW metrics, successful device recovery and rail restoration, no unexpected events, and a complete Yamcs archive.
Electrical readings must agree with configured rail demand within one encoding count.
Fault stacks observe CRC and request failures through new EPS device error counts and switch state readback.
The nominal stack observes radio shutdown and recovery through bounded real RF command probes.
FSW housekeeping cannot prove exact simulator energy, internal readiness or cycle counts, zero actuation at every tick, or zero traffic throughout an outage.
Those independent simulator audit checks are outside the current acceptance criteria.

## Evidence

Each run archives sampled inputs, resolved settings, generated stacks, every step and phase boundary, flight events, logs and FSW telemetry.
Backdoor console records remain in retained logs for troubleshooting.
The generic control trace remains available for shared Simulith performance and fidelity workflows.
Native Yamcs archives support replay.
`events.json` retains events and identifies bounded expected error windows.
Campaign reports expose FSW rail readings, device transaction counters, recovery phase time, topology and commands exercised in passing fault stacks.
Older reports retain their historical console metrics and simulator audit criteria.
Run new campaigns before generating a report under the current FSW-only criteria.
Plots do not report simulator energy residuals or confirmed internal backdoor applications.
The report requires Matplotlib 3.9 or later, NumPy and PyYAML in its Python environment.
Generate standalone PNG/PDF plots and the acceptance report with:

```sh
python3 tools/shire_eps_replay.py --campaign-report build/eps-validation/nominal \
  --trials 0,50,99 --report-dir build/eps-validation/replay-nominal
python3 tools/shire_eps_replay.py --campaign-report build/eps-validation/faults \
  --trials 0,19 --report-dir build/eps-validation/replay-faults
python3 tools/shire_eps_report.py --smoke build/eps-validation/smoke-final \
  --nominal build/eps-validation/nominal --faults build/eps-validation/faults \
  --nominal-replay build/eps-validation/replay-nominal \
  --fault-replay build/eps-validation/replay-faults \
  --output build/eps-validation/analysis
```

If plotting dependencies are unavailable in the host Python, use the tested SHIRE base image environment.
Install the plotting packages once, then run the same report command inside the container:

```sh
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$PWD:$PWD" -w "$PWD" \
  ghcr.io/voyagertechnologies-public/shire-base:0.0.0 \
  python3 -m pip install --target build/eps-validation/analysis-python \
  matplotlib==3.11.2 numpy==2.5.3
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -e PYTHONPATH="$PWD/build/eps-validation/analysis-python" \
  -e MPLCONFIGDIR=/tmp/eps-matplotlib -v "$PWD:$PWD" -w "$PWD" \
  ghcr.io/voyagertechnologies-public/shire-base:0.0.0 \
  python3 tools/shire_eps_report.py --smoke build/eps-validation/smoke-final \
  --nominal build/eps-validation/nominal --faults build/eps-validation/faults \
  --nominal-replay build/eps-validation/replay-nominal \
  --fault-replay build/eps-validation/replay-faults \
  --output build/eps-validation/analysis
```

Selected replays use the archived inputs and recorded runtime image tags.
They require the same resolved wiring, equivalent FSW rail readings within encoding tolerance, ordered backdoor commands and successful functional acceptance.
Live command admission can vary with wall-clock scheduling, so transaction counts and battery voltage can differ between runs.
Internal transition tests additionally require deterministic results at simulation tick resolution.

## Interrupted run recovery

If interrupted during an RF probe, issue `/SHIRE_GROUND/RESTORE_UPLINK` or wait 30 wall-clock seconds for automatic link restoration.
Then restore EPS and its consumers using the steps below.

Resume the simulation if paused and issue `BACKDOOR_EPS_RESET` once.
Verify the reset effect through fresh EPS and consumer FSW telemetry.
Optional `EPS_BACKDOOR` and debug `EPS_SIM_STATE` records provide internal application details.
Verify restored rails and device transactions in fresh EPS/consumer FSW telemetry.
Confirm startup rail states and electrical readings in FSW.
Internal fault counts, AUTO selection and load scales require optional console diagnostics if those details are needed.
Wait for boot completion and verify new device transactions.
Keep control on debug until actual radio traffic is confirmed.
If reset application cannot be observed, restart the isolated simulation and retain the interrupted run evidence.

***
Last reviewed: 8 October 2026

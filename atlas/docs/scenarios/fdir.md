# Fault Detection, Isolation, and Recovery

The current repository includes a focused FDIR path for the Demo component that links simulator fault injection, cFS Limit Checker (LC), Stored Command (SC), and a YAMCS procedure.

## Implemented path

1. `DemoFDIR.ycs` enables Demo and confirms nominal command and data telemetry.
2. YAMCS sends `BACKDOOR_DEMO_RAND_DATA` on the simulation backdoor stream.
3. The Demo simulator changes its data generation so channel values fall below the configured valid range.
4. LC watchpoint 10 checks that Demo is enabled.
   Watchpoint 11 checks whether Demo channel 1 is below 256.
5. LC actionpoint 11 combines those watchpoints and starts RTS 11 after one failure.
6. RTS 11 disables Demo, waits ten scheduler wakeups, and enables it again.
7. The procedure disables fault injection and checks that the component and data return to the expected state.

The relevant sources are:

* `comp/demo/gsw/procedures/DemoFDIR.ycs`
* `comp/demo/sim/demo_sim.c`
* `cfg/shire_defs/tables/lc_def_wdt.c`
* `cfg/shire_defs/tables/lc_def_adt.c`
* `cfg/shire_defs/tables/sc_rts011.c`

## Run the exercise

1. Build and start the default `sat-1` DRM.
2. Confirm YAMCS `debug-in`, `debug-out`, `sim-backdoor`, and 42 truth links are available.
3. Open **Procedures / Stacks / DemoFDIR.ycs**.
4. Review every command and assertion against the source files above.
5. Run from the first step while watching Demo telemetry, LC events, SC execution status, and FSW logs.
6. Save the YAMCS result and relevant logs with the configuration and repository revision.

## Scope

This exercise demonstrates one injected data range fault and one automated restart response.
It does not establish general fault coverage, isolation accuracy, safe mode entry, or recovery for ADCS, EPS, Radio, CryptoLib, 42, containers, networks, or physical hardware.
Add one controlled procedure and explicit acceptance criteria for each additional failure mode.

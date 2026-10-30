# Radio security validation

This page is the reference for the radio link security configuration and the scenario that attacks it.
The [Space Link Security](../../scenarios/space-link-security.md) lesson is the place to start if you are new to it.

## Purpose

The scenario shows that the radio path is protected in both directions and that tampering and replay are caught.
Radio commands and telemetry are AES-256-GCM encrypted and authenticated by CryptoLib, which runs in the ground `shire-cryptolib` container and in the flight radio app.

## Configuration

Link security is on by default.
`uplink_encryption` (telecommands, the radio's RX) and `downlink_encryption` (telemetry, the radio's TX) in the Radio's `radio:` block each select encrypted or clear mode.
The orchestrator renders the same values into the flight app's `device_cfg.h` and into the `shire-cryptolib` service environment, so the two ends cannot disagree.

| Direction | Virtual channel | Encrypted SA | Clear SA |
| --- | --- | --- | --- |
| Uplink | TC VCID 0 | SPI 4 | SPI 1 |
| Downlink | TM VCID 1 | SPI 6 | SPI 5 |

The keys are CryptoLib's built-in in-memory test keys, identical on both ends.
This protects a simulation, not a real link.
The anti-replay window is 65535 frames, so an ordinary loss of signal or a high drop rate does not desynchronize the receiver.
A very long outage can pass the window, as the Limitations of the [Space Link Security](../../scenarios/space-link-security.md#limitations) lesson explain.
Restart the ground CryptoLib and the flight software together, because each end tracks its own IV.

## What the scenario checks

`RadioSecurity.ycs` sends commands over `radio-out` (using `USE_RADIO_UPLINK` and `RESTORE_UPLINK`) and reads Radio housekeeping back over the debug path.
It uses the Radio simulator backdoor to attack the link while the simulation runs.

| Step | Expected result |
| --- | --- |
| Encrypted command over radio-out | Accepted, `RADIO_HK_CommandCount` increments |
| `CORRUPT_PACKETS` uplink, then a command | Rejected, `RADIO_HK_DeviceErrorCount` increments and `CommandCount` does not |
| A genuine command afterwards | Accepted, so the link recovers from the lost sequence number |
| `REPLAY_PACKET` uplink | Rejected as a replay |
| A genuine command afterwards | Accepted |
| `REPLAY_PACKET` downlink, then `CORRUPT_PACKETS` downlink with `COUNT` 1 and `OFFSET` 30 | Both rejected by the ground CryptoLib |

Rejections of downlink frames happen in the ground container and are not visible to Yamcs, so the scenario also asserts on the CryptoLib container log through the scenario `log_assertions` option.
The stack's own final check on the downlink step only confirms that the flight side counted no new errors, and it cannot show that the ground rejected the frames.
Each assertion names a container, a regular expression, and optional `min` and `max` counts of matching lines.

## Running

```bash
make scenario SCENARIO=radio-security-testing
```

To run `RadioSecurity.ycs` by hand in the Yamcs web UI, first enable `debug-in` and `sim-backdoor` on the Links page and keep the simulation playing.
The stack selects its own command path with `USE_DEBUG_UPLINK`, `USE_RADIO_UPLINK`, and `RESTORE_UPLINK`.
Run by hand, the stack checks the uplink rejections through `RADIO_HK_DeviceErrorCount` and `RADIO_HK_CommandCount`.
It cannot verify the downlink rejections, because those appear only in the ground CryptoLib container log and the scenario's `log_assertions` check them.
After a manual run, inspect the `cryptolib` container log for the authentication failure (status -21) and replay rejection (status -23) lines, or use `make scenario` for the pass or fail result.

To compare with an unprotected link, set `uplink_encryption` or `downlink_encryption` to `false` for that direction in a scenario copy, run `make cfg`, and rebuild.
The clear-mode frames carry the command bytes in the open, which the CryptoLib unit tests in `comp/cryptolib/test/unit/ut_shire_security.c` also check directly.

***
Last reviewed: 20261029

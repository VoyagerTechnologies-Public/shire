# Radio Security Testing

## Objective

Show that the radio path is protected in both directions and that tampering and replay are caught.
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
The anti-replay window is 65535 frames, so a long loss of signal or a high drop rate does not desynchronize the receiver.
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
| `CORRUPT_PACKETS` downlink, then `REPLAY_PACKET` downlink | Rejected by the ground CryptoLib |

Rejections of downlink frames happen in the ground container and are not visible to Yamcs, so the scenario also asserts on the CryptoLib container log through the scenario `log_assertions` option.
Each assertion names a container, a regular expression, and optional `min` and `max` counts of matching lines.

## Running

```bash
make scenario SCENARIO=radio-security-testing
```

To compare with an unprotected link, set `uplink_encryption` or `downlink_encryption` to `false` for that direction in a scenario copy, run `make cfg`, and rebuild.
The clear-mode frames carry the command bytes in the open, which the CryptoLib unit tests in `comp/cryptolib/test/unit/ut_shire_security.c` also check directly.

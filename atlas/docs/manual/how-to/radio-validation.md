# Radio validation

This page is the reference for the Radio simulator's link model, its backdoor commands, and the scenario that exercises them.
The [Radio Link and Command Paths](../../scenarios/radio-link-command-paths.md) lesson is the place to start if you are new to the radio path.

## Purpose

The scenario exercises the Radio simulator's RF link model: ground station visibility, one-way transmission delay and random packet loss, all controllable through simulator backdoor commands.
It also sends the same command over the debug path and over the radio path, and checks the flight counter after each.
The default link is ideal (always in view, no delay, no loss), so other scenarios are unaffected.

## Configuration

The scenario `cfg/drm/scenarios/radio-link-testing.yaml` selects the two catalog ground stations for the Radio and starts with `link_mode: always`.
Stations come from `cfg/drm/ground_stations.yaml`.
Each can add `min_elev_deg` (default 5) and `enabled` (default true).
Delay, drop rate, the loss seed and the in-flight queue depth are the `uplink_*`, `downlink_*`, `link_seed` and `link_queue_depth` keys in the Radio's `radio:` block.
Run `make cfg` after changing any of them.

## Link behaviour

* **Always / never.** Forced visibility ignores geometry.
  Packets sent while out of view are discarded and counted.
* **Auto.** The link is up when any enabled station sees the spacecraft above its elevation mask, computed from 42's Earth-fixed spacecraft position.
* **Delay.** Applies per direction in simulated time and preserves packet order.
* **Loss.** Per-direction probability in parts per million from a seeded generator, so a run is repeatable.
* **Ground lock.** The Radio HK `GroundLock` field reports the current link state.

All stations share one downlink, so they differ only in visibility.
A packet the link loses is invisible to the flight software.

## Backdoor commands

| Command | Effect |
| --- | --- |
| `/RADIO/BACKDOOR_RADIO_SET_LINK_MODE` | `AUTO`, `ALWAYS_IN_VIEW` or `NEVER_IN_VIEW` |
| `/RADIO/BACKDOOR_RADIO_SET_DELAY` | Direction (`UPLINK`, `DOWNLINK`, `BOTH`) and delay in ms |
| `/RADIO/BACKDOOR_RADIO_SET_DROP_RATE` | Direction and drop probability in ppm |
| `/RADIO/BACKDOOR_RADIO_SET_IMPAIRMENTS_ENABLED` | Bypass or apply the configured delay and loss |
| `/RADIO/BACKDOOR_RADIO_SET_STATION_ENABLED` | Enable or disable a station by index (255 for all) |
| `/RADIO/BACKDOOR_RADIO_SET_SEED` | Reseed the loss generator |
| `/RADIO/BACKDOOR_RADIO_RESET_LINK` | Restore configured defaults and discard packets in flight |
| `/RADIO/BACKDOOR_RADIO_CORRUPT_PACKETS` | Flip one bit at a byte offset in the next N packets (see [Radio security validation](radio-security-validation.md)) |
| `/RADIO/BACKDOOR_RADIO_REPLAY_PACKET` | Re-inject the last packet that crossed the link |

Each accepted or rejected command prints a `RADIO_BACKDOOR {...}` line with the resulting settings and drop counters.
AOS and LOS transitions print `RADIO_LINK {...}`.

## Running

```bash
make scenario SCENARIO=radio-link-testing
```

The scenario runs two stacks.
`RadioLinkBackdoor.ycs` selects the debug uplink so its commands do not depend on the link being impaired, forces the link out of and into view, and checks the HK ground lock flag after each change.
The stack ends with `BACKDOOR_RADIO_RESET_LINK`, which discards the delay and drop rate it applied, so its end state does not show impaired traffic.
`RadioCommandPaths.ycs` then sends the ES NOOP over each path and reads the ES command counter, which is a flight side observation, after each one.
It sends the NOOP once on the debug path and once on the radio path, once on the radio path while the link is out of view, once on the radio path with a 6 second uplink delay, and ten times on the radio path with half of the uplink packets dropped by seed 1.
It checks that each command that should arrive is counted and that the one sent out of view is not.
The scenario's `log_assertions` check the director log for the Radio's own counters of packets discarded out of view and packets dropped by the loss model, because no YAMCS parameter reports them.
`RadioCommandPaths.ycs` restores the debug path and the ideal link when it finishes.

To observe delay and loss on the radio path yourself, send `BACKDOOR_RADIO_SET_DELAY` or `BACKDOOR_RADIO_SET_DROP_RATE` by hand, with impairments enabled, and then send commands with `/SHIRE_GROUND/USE_RADIO_UPLINK` or switch to `radio-out` and `radio-in` on the Yamcs **Links** page, as described in [Choosing a command path](../core-concepts/ground-software.md#choosing-a-command-path).
After the observation, send `BACKDOOR_RADIO_RESET_LINK` to restore the default ideal link.
The lesson does this step by step.

***
Last reviewed: 20261029

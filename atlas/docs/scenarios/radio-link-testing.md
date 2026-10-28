# Radio Link Testing

## Objective

Exercise the Radio simulator's RF link model: ground station visibility, one-way transmission delay and random packet loss, all controllable through simulator backdoor commands.
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
| `/RADIO/BACKDOOR_RADIO_CORRUPT_PACKETS` | Flip one bit at a byte offset in the next N packets (see [Radio Security Testing](radio-security-testing.md)) |
| `/RADIO/BACKDOOR_RADIO_REPLAY_PACKET` | Re-inject the last packet that crossed the link |

Each accepted or rejected command prints a `RADIO_BACKDOOR {...}` line with the resulting settings and drop counters.
AOS and LOS transitions print `RADIO_LINK {...}`.

## Running

```bash
make scenario SCENARIO=radio-link-testing
```

`RadioLinkBackdoor.ycs` selects the debug uplink so its commands do not depend on the link being impaired, forces the link out of and into view, and checks the HK ground lock flag after each change.
The stack ends with `BACKDOOR_RADIO_RESET_LINK`, which discards the delay and drop rate it applied, so its end state does not show impaired traffic.
To observe delay and loss on the radio path itself, send `BACKDOOR_RADIO_SET_DELAY` or `BACKDOOR_RADIO_SET_DROP_RATE` by hand, with impairments enabled, and then switch to `radio-out` and `radio-in` before you watch the traffic.
Use `/SHIRE_GROUND/USE_RADIO_UPLINK` or the Yamcs **Links** page to switch, as described in [Choosing a command path](../manual/core-concepts/ground-software.md#choosing-a-command-path).
After the observation, send `BACKDOOR_RADIO_RESET_LINK` to restore the default ideal link.

***
Last reviewed: 20261027

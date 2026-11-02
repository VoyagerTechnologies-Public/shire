# Missions and constellations

A mission is a self contained set of spacecraft types, scenarios, initial conditions and ground definitions.
SHIRE ships two.
The Design Reference Mission (`drm`) is the Earth orbit reference used by the lessons.
The lunar relay (`lunar-relay`) is a second mission about the Moon, and the home of the constellation work.
Having two shows which parts of the configuration belong to a mission and which are shared.

## What a mission owns

A mission is a directory under `cfg/` that `cfg/shire-config.yaml` registers.
Every path inside a mission file is relative to `cfg/`.

| Path | Purpose |
| --- | --- |
| `cfg/<mission>/<mission>.yaml` | Registers the spacecraft, scenarios and constellations, and sets mission level limits and component values. |
| `cfg/<mission>/spacecraft/` | Spacecraft types, with their component lists and bus wiring. |
| `cfg/<mission>/scenarios/` | Scenarios that run under this mission. |
| `cfg/<mission>/initial_conditions/` | Starting orbit, epoch and attitude bins that scenarios reference. |
| `cfg/<mission>/constellations/` | Multi vehicle runs, with their crosslinks and scripted disruptions. |
| `cfg/<mission>/ground_stations.yaml` | The ground station catalog that 42 and the radio use. |
| `cfg/<mission>/gsw/` | Ground software definitions that are copied into Yamcs. |

Scenario names are unique within a mission, so `smoke` exists only under `lunar-relay` and `nominal` only under `drm`.
A scenario in one mission cannot be run under the other.

## Create a mission

1. Create `cfg/<mission>/` with the layout above, starting from a copy of an existing mission.
2. Register it under `missions:` in `cfg/shire-config.yaml`.
3. Copy the shared ground definitions described in the next section.
4. Run `make cfg-check` and fix what it reports.
5. Run a scenario under the new mission with `python3 tools/shire-scenario.py --scenario <name> --mission <mission> --spacecraft <type>`.

### Ground definitions every mission needs

Yamcs loads five definition files straight from the mission's `gsw/` directory.
They are `ccsds.xtce`, `shire_ground.xtce`, `shire_server.xtce`, `shire_visual.xtce` and `sim_42_truth.xtce`.
They describe the platform and not the mission, but they currently live inside each mission.
A mission without them builds, and then Yamcs fails to start.
`make cfg-check` reads the list from the Yamcs configuration and reports any that are missing.

The `lunar-relay` mission carries copies of the DRM's files.
Keeping them identical is a manual job for now.
A shared platform directory that every mission overlays would remove the copies, and it needs a change in the `yamcs` submodule.

## Reuse a spacecraft type with extends

A spacecraft file can name another spacecraft in `extends:`.
The child merges over its base key by key, and a list in the child replaces the base list.
The `relay` type in the lunar mission extends `leaf`, so it states only its identity, its power bus address, its radio wiring and its radio delay.

```yaml
extends: "leaf"
spacecraft_name: "relay"
spacecraft_id: 2

eps:
  i2c_device_addr: 0x1F
```

A child that sets `_replace: true` inside a mapping replaces that mapping instead of merging into it.
A cycle or an unknown base is reported by `make cfg-check`.

## Run Bundle Protocol on a spacecraft

A spacecraft runs NASA's BPNode, the cFS Bundle Protocol application, when its file has a `dtn:` block.
The only setting is the vehicle's node number, which is built into the flight software.
Spacecraft without the block do not start BPNode or its library.

```yaml
dtn:
  node_id: 100
```

The orchestrator writes the node number into `dtn.cmake` for the flight software build and keeps the startup script in step.
A constellation vehicle must fly a spacecraft with this block, and `make cfg-check` reports one that does not.
BPNode needs more OSAL queue depth and a longer cFE table name limit than the cFS defaults, and SHIRE sets both.

BPNode starts, runs in lock step with the simulation, and answers ground commands such as NOOP.
Exchanging bundles between vehicles over a crosslink is still being built.
The scheduler wakes BPNode at 10 Hz and requests its housekeeping every 10 seconds.
SHIRE's PSP holds simulated time until each scheduled message is received, and stock BPNode only reads its command pipe during a wakeup, which deadlocks the first scheduled command.
The Voyager fork of BPNode receives commands on the same pipe as the wakeup, so scheduled housekeeping and ground commands are handled as they arrive.

## Orbit about another world

An initial condition sets the central body with `orbit.center`.
It defaults to `EARTH`, so existing bins are unchanged.
The lunar bins set `center: LUNA` and 42 then propagates the orbit about the Moon.
SHIRE's 42 input enables only Earth and the Moon, so those are the two valid values.
Ground stations take the same `world` values, which lets the lunar catalog hold Earth stations and a lunar surface site.

The lunar mission also shows how distance becomes configuration.
The relay's radio carries 1282 ms of delay in each direction, which is the light time over the mean Earth to Moon distance.
The radio component applies it with its existing delay setting, so no new simulation code is involved.

### What is verified for the lunar mission

The `smoke` scenario boots the full stack for one simulated minute under the `relay` spacecraft.
The control trace of that run shows a circular orbit about the Moon, with the vehicle 1838 km from the Moon's center and moving at 1.63 km/s.
The same trace for the DRM would show 6778 km and 7.7 km/s.
The relay's radio delay reaches the flight configuration as 1282 ms in each direction.
No ground traffic is sent in `smoke`, so the delay itself is not exercised yet.

### Limits of the lunar mission today

The Moon has no global magnetic field, so ADCS modes that rely on the magnetometer, such as B-dot detumble, are not meaningful in lunar orbit.
The Cesium visualization and the radio's `auto` visibility mode are built around the Earth and have not been checked for lunar orbits.
Treat the lunar mission as proof that a second mission configures, builds and boots, and not yet as a validated lunar simulation.

## Constellations

A constellation file lists vehicles and the links between them.
It lives under `cfg/<mission>/constellations/` and is registered in the mission file.
The `crosslink-demo` constellation in `lunar-relay` has two low lunar orbiters and a relay in a high elliptical orbit.

* Each vehicle names a registered spacecraft type, a role of `leaf` or `relay`, and a Bundle Protocol node number.
* Each link joins two vehicles and sets a maximum range, a delay mode of `range` or a fixed delay, a loss rate and scripted disruptions.
* A disruption is an outage, extra loss or extra delay over a window of simulated time.
* Vehicles start from one initial condition bin whose `vehicles:` section gives each vehicle its own orbit.
* Link seeds derive from one `master_seed`, so every run repeats exactly.

### Run a constellation

`make constellation CONSTELLATION=crosslink-demo` builds and runs every vehicle of the constellation on one simulated clock.
Add `SCENARIO=<name>` to choose the scenario whose component settings apply to every vehicle, and `DURATION=<seconds>` to set how much simulated time to run.

Each vehicle is built as its own single vehicle run, so it gets its own spacecraft type, node number, orbit and image tag.
`tools/shire-constellation.py` selects each vehicle in `build/active.yaml` for its build and puts your selection back afterwards.
All vehicles then start in one compose project.
Each vehicle has its own network, its own 42, Director, flight software, CryptoLib and Yamcs, and the usual host names still work inside it.
One Simulith server joins every vehicle network, so the vehicles tick together.
The server waits for two clients per vehicle, and every client has a unique name such as `shire-fsw-leaf-1`.

| Vehicle | Yamcs address |
| --- | --- |
| The first vehicle | `localhost:8090` |
| Each later vehicle | The port rises by 10 for each vehicle, so `8100`, then `8110`. |

The run reports each vehicle's tick count, its control trace hash and whether the server saw both of its clients.
It writes the logs and `result.json` under `build/constellation-runs/`.
Running the same constellation twice gives identical control traces for every vehicle.

The constellation still has no crosslink data path.
The vehicles share a clock and each runs BPNode, and the crosslink component and the bundle contacts between vehicles are still being built.
Constellation runs keep no replay archive yet.

## Validate the configuration

`make cfg-check` validates every registered mission without Docker or a build.
It reports unknown keys, missing files, duplicate identifiers, wiring conflicts within one spacecraft, bad references, and constellation errors.
It collects every problem in one pass.

***
Last reviewed: 20261101

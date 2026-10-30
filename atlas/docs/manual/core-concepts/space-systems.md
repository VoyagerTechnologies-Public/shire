# Space System Crash Course

A space system can be divided into a space segment and a ground segment.
The space segment contains the spacecraft and relays.
The ground segment contains the mission operations center, science operations center, and ground station.
The space link connects the two segments through radio frequency or optical communication.

SHIRE focuses on the software visible parts of this system.
It models spacecraft dynamics, flight software, component interfaces, a radio path, and ground software.
It does not model a qualified antenna, modem, ground station, or complete physical communication channel.
The minimum level of fidelity needed to close requirements or perform the desired testing is recommended to enable maximum performance.

```mermaid
graph LR
    subgraph Ground Segment
        A[SOC] <--> B[MOC]
        B <--> C[Ground Station]
    end
    subgraph Space Segment
        C <--> |Space Link| D[Relay]
        D <--> E[Spacecraft]
    end
```

## What crosses the space link

Physical space links use antennas, radio hardware, and modems on the spacecraft and ground.
SHIRE replaces those physical elements with a software radio model and UDP transport.

Telemetry and commands commonly use packet and frame formats such as those defined by the Consultative Committee for Space Data Systems (CCSDS) Space Packet standard.
The current SHIRE ground and flight configuration includes CCSDS packet handling and CCSDS File Delivery Protocol (CFDP) services.
CFDP Class 1 transfers files without acknowledgements, while Class 2 adds acknowledgements and retransmission.
The DRM scenarios send files as Class 2 by setting the `CFDP_CLASS` command argument, and the polling directory in `cf_def_config.c` is also set to Class 2 though it is disabled.

The Radio component provides Sleep, Transmit, Receive, and Duplex modes.
Its simulator also models the link itself.
The default `link_mode` is `always`, which keeps the link up regardless of geometry.
With `link_mode: auto`, the simulator computes visibility from 42 truth and the ground stations in `cfg/drm/ground_stations.yaml`, and each station has a `min_elev_deg` elevation mask.
Each direction can also have a one-way delay (`*_delay_ms`) and a packet drop rate (`*_drop_ppm`).
See `comp/radio/README.md` for the full link model.

The DRM uses RTS 6 to create a pass window by selecting Duplex mode for a fixed 480 scheduler wakeups before returning to Receive mode.
That window does not follow the Radio link model or ground station visibility.

## Spacecraft bus and payload

The spacecraft bus contains the services needed to operate the vehicle.
Common bus functions include command and data handling, power, communication, attitude control, and thermal control.

The payload performs the mission specific work.
SHIRE uses the Demo or demonstration component as its reference payload.
The Demo application and simulator provide commands, housekeeping, and three representative data channels mimicking an instrument grade magnetometer.

## The Spacecraft Bus

The current DRM represents these bus functions:

* **Attitude Determination and Control:** the ADCS component consumes 42 state and can send actuator commands back to 42.
* **Command and Data Handling:** cFS, reusable cFS applications, and component applications run in the FSW service.
* **Communication:** the Radio component models command uplink, telemetry downlink, buffering, and operating modes.
* **Electrical Power System:** the EPS component models solar generation, battery state, and switched loads.

The current DRM does not include a separate thermal control component.
Thermal gradients, heaters, coolers, and temperature sensor dynamics are outside the implemented baseline.

***
Last reviewed: 20261027

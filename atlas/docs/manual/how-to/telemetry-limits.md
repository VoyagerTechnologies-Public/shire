# Telemetry limits

SHIRE can give any numeric telemetry parameter yellow and red limits.
YAMCS evaluates them, so they appear in the Parameters page, the Alarms page, and each parameter's monitoring result.
The limits are configured in YAML, so a mission or a scenario can set its own without editing any XTCE file.

## Configure limits

Limits are written per parameter, in engineering units, under a `limits` key.

```yaml
limits:
  /EPS/BATTERY_VOLTAGE:
    yellow: {min: 20.0}
    red: {min: 18.0}
  /EPS/BATTERY_TEMPERATURE:
    yellow: {min: 0.0, max: 40.0}
    red: {min: -10.0, max: 50.0}
```

Each range is the acceptable region.
A value outside the `yellow` range is yellow, which YAMCS reports as `WARNING`.
A value outside the `red` range is red, which YAMCS reports as `CRITICAL`.
Give `min`, `max`, or both.

| Rule | Detail |
| --- | --- |
| Parameter name | Use the qualified YAMCS name, such as `/EPS/BATTERY_VOLTAGE`. |
| Nesting | The red range must not be inside the yellow range on a side where both are given. |
| Types | Only integer and float parameters can have limits. |
| Scope | Component and mission XTCE files are limited, and the cFS application definitions are not. |

## Where limits are set

Limits cascade like component configuration, and a later layer wins.
The layers are the repository catalog `cfg/shire-config.yaml`, the mission file, the spacecraft file, and the scenario file.
The catalog layer ships with no limits, so a limit placed there applies to every mission.
The shipped DRM limits are in `cfg/drm/drm.yaml`.

A layer replaces a parameter's yellow range or red range as a whole, so a scenario can change one and keep the other.
Setting a level or a whole parameter to `null` removes it.

```yaml
# In a scenario file: tighten yellow only, and remove the temperature limits.
limits:
  /EPS/BATTERY_VOLTAGE:
    yellow: {min: 21.0}
  /EPS/BATTERY_TEMPERATURE: null
```

## How limits reach YAMCS

`make cfg` and `make` resolve the layers and check them.
A limit that is malformed, or that names a parameter no component or mission definition contains, stops the build with a message that names it.
For each limited parameter the build writes a copy of its parameter type with the alarm ranges, under `build/<mission>/scenario/mdb`.
The ground software build overlays those files on the checked in definitions.
Other parameters that share the original type are not limited.

The build log reports how many limits were applied.
The resolved limits are also part of the scenario snapshot, so a run records the limits it used.
Run `make` after changing a limit so the ground software image includes it.

## See limits in YAMCS

Open **Telemetry**, then **Parameters**, and choose a system.
A parameter outside its limits shows its value in red with an arrow that points toward the violated side.
Open **Alarms** to see each active alarm, its severity, and its trigger value.
YAMCS draws `WARNING` in amber and `CRITICAL` in red on the alarm count.
Alarms stay active until someone acknowledges them.

The REST API reports the same state.
A parameter's value includes `monitoringResult`, which is `IN_LIMITS`, `WARNING`, or `CRITICAL`, and `alarmRange`, which lists its limits.

## Check limits in a procedure

The headless stack runner adds a `monitoring` condition for verify steps.
It compares the parameter's monitoring result with a level.

```json
{
  "type": "verify",
  "condition": [
    {"parameter": "/EPS/BATTERY_VOLTAGE", "operator": "monitoring", "value": "WARNING"},
    {"parameter": "/EPS/BATTERY_VOLTAGE", "operator": "lt", "value": "20"}
  ],
  "timeout": 60000
}
```

The levels are `IN_LIMITS`, `WATCH`, `WARNING`, `DISTRESS`, `CRITICAL`, and `SEVERE`.
A parameter with no limits has no monitoring result, so the step fails at once and says so.
The YAMCS web interface cannot evaluate this condition, so use it only in stacks that `make scenario` runs.
`CmdTlmLimits.ycs` does not use it.
That stack drives the battery state of charge through the EPS backdoor into yellow and then red, and checks the voltage numerically, so it runs the same way in the web interface and in `make scenario`.
The [Cmd/Tlm Validation](../../scenarios/cmd-tlm-validation.md) scenario walks through the alarms that those limits raise in YAMCS, from the first warning to the acknowledged and resolved alarm.

***
Last reviewed: 20261027

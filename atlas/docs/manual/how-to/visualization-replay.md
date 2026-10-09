# Visualization and replay

The SHIRE 42 container runs headlessly.
Yamcs records visualization truth in its native packet and parameter archives, and the offline Cesium viewer renders frames from those data at 30 FPS by default.
Rendering FPS is independent of the 20 Hz default recording cadence and the Simulith tick rate.
The viewer keeps a bounded sample window in memory and does not write images or video.
It reads atomic pose windows from Yamcs's native packet archive so position, attitude, and Earth transform come from one recorded sample.
Yamcs also indexes the fields in its Parameter Archive for historical diagnostic queries and archive verification.
Packet replay remains available if indexing is incomplete.

Build and start the DRM, then choose **Visualization** in Yamcs's left sidebar to open the viewer in a new tab or window.
The top-right **Links → Go to Visualization** menu and [direct viewer URL](http://localhost:8090/visualization/) also remain available.
The sidebar link is a small Yamcs Web header extension, so the Yamcs Web application is not rebuilt.
In Yamcs-only replay mode, the link opens the archive timeline automatically.
Use **Live** to follow incoming packets.
Use **Replay** to browse recorded data.
**Pause** in Live mode freezes only the viewer.
SHIRE and Yamcs continue running and recording.
**Play** then replays from the paused timestamp at the selected playback speed.
When playback catches the archive head on a live run, the viewer automatically rejoins incoming telemetry at SHIRE's current simulation pace.
Select a playback speed above the simulation pace to catch up, or choose **Live** to jump immediately.
Drag the timeline, enter a positive playback-speed multiplier and rendering FPS, and choose attitude inspection (the default), spacecraft follow, or Earth overview.
Camera orbit and zoom stay relative to the spacecraft when telemetry advances or the viewer switches between Live and Replay.
The top-right ground-track map shows the recorded path in cyan, a one-orbit two-body expected path in dashed orange, eclipse portions in outlined violet, and the current position in yellow.
After a timeline scrub, recorded history loads backward from the displayed time, bounded to 30 minutes or 12,000 raw packets, whichever is shorter.
At 20 Hz this covers about 10 minutes.
At 100 Hz it covers about 2 minutes.
Solid violet marks archived eclipse truth.
Dotted violet is an approximate Earth-shadow forecast.
The forecast is a diagnostic overlay and does not replace 42 truth.
The **EPS** box below the ground-track map shows the eight switch states and their configured labels.
Labels come from the active Yamcs mission database and show the connected device names.
The default DRM shows DEMO on switch 0, ADCS on switch 4, RADIO on switch 6, and the passive load bank on switch 5.
Shared rails list every mapped device, and custom rail labels are retained alongside device names.
Hover over a switch to see its load mapping, rail voltage, and startup state.
The battery graph plots calibrated `/EPS/BATTERY_VOLTAGE` from FSW housekeeping in volts.
Battery reporting uses the configured voltage span divided by 255, about 0.03137 V per count for the default 16 to 24 V range.
Rebuild the simulator and Yamcs together after changing this range so raw counts and calibration agree.
Use the corner minimize button to collapse the EPS box, and the plus button to expand it.
Telemetry collection continues while collapsed.
Set **Past** to a whole number from 1 to 120 simulated minutes.
The default is ten minutes.
The viewer backfills available Yamcs battery history when opened or when the window changes.
History is bounded to 7,200 samples and twelve archive pages, so a longer window can contain less coverage at higher telemetry rates.
The horizontal axis spans the selected window even when only a few samples are available.
Live mode subscribes to the existing EPS FSW parameters, and Replay reads their Yamcs Parameter Archive history at or before the displayed UTC timestamp.
Missing, invalid, expired, or reports older than 15 simulated seconds at the replay cursor show an unavailable marker rather than an inferred switch state.
The graph leaves gaps between readings more than 15 seconds apart.
Its timestamp identifies the latest battery report, so cached FSW housekeeping should not be used as proof of device responsiveness.
These readings do not expose EPS simulator state or estimate battery state of charge.

The scene includes Cesium's offline star map and Moon, a dashed grey orbit trail, solid body axes, a translucent Sun vector, eclipse state, and angular rates.
The body axes and Sun vector start at the same spacecraft truth position used by the orbit trail.
The credits under the controls show the configured spacecraft model filename.
Previously saved bookmarks remain available.
The button for creating new bookmarks is hidden.

`SHIRE_VISUAL_HZ=20 make start` selects the visualization truth rate in samples per simulated second.
It must divide the 100 Hz simulation tick rate and can range from 1 to 100.
The legacy truth stream remains at 1 Hz.
The viewer's **Render FPS** control starts at 30 and changes only the rate at which the browser draws frames.

The selected spacecraft's `visualization` settings in `cfg/<mission>/spacecraft/<spacecraft>.yaml` choose `earth_imagery` and `model` source paths.
Paths are relative to the SHIRE checkout or absolute.
The default DRM `sat-1` settings point to `42/World/BlueMarbleNG4096.ppm` and `42/Model/stf1_red.obj`.
The model manifest sets Cesium's up and forward axes and a row-major `model_to_body` 4×4 matrix.
For the 42 STF1 OBJ, the converter preserves body axes after Cesium's Y-up correction, so its forward axis is X rather than Cesium's default Z.
The configured matrix also places the mesh origin relative to 42's center of mass at `[0.12, -0.25, 0.2]` meters.
Custom glTF models can set `model_up_axis`, `model_forward_axis`, and `model_to_body` in the spacecraft visualization settings.
Build-time staging converts the 4096×2048 PPM texture to an 8-bit PNG without copying the 42 source into Git.
After selecting the mission and spacecraft with `make cfg`, `make gsw` reads their source configuration, stages the assets under ignored `build/visualization-assets/`, and embeds them into the GSW image at build time.
Neither source asset is vendored in the repository.
The model can be a `.glb` file or an `.obj` file.
OBJ conversion preserves geometry, normals, vertex colors, and diffuse material colors.
Use GLB for textured or more complex materials.
The viewer uses Cesium's packaged star and Moon assets, so no external imagery service is required.

Every new DRM scenario, campaign trial, performance trial, and manual start allocates a separate Docker volume labeled `shire.archive=true` and `shire.run=<run-id>`.
This prevents repeated epochs from colliding in Yamcs's packet archive.
The Yamcs instance remains named `shire` inside each volume.
Volume labels include mission, spacecraft, scenario, image ID, and start time.
A dedicated Docker image tag keeps each new run's compatible Yamcs runtime available through ordinary cleanup.
The database's `shireRuns` bucket contains `run.json`, the Yamcs mission database and configuration snapshot, `archive-status.json` when a check completes, and optional `bookmarks.json`.

After a run ends:

```sh
make replay-list
make replay RUN=<run-id>
# When port 8090 is occupied:
make replay RUN=<run-id> PORT=8092
```

`make replay` starts only Yamcs against that run's retained database and prints the viewer URL.
It refuses an archive still mounted by another container.
If an older run's pinned image has already been removed, it checks the replacement image's visualization mission database against the snapshot stored in Yamcs before opening replay.
Replay configuration starts no UDP links, command processor, CFDP service, or startup automation.
The web UI hides commanding.
Stop the replay container with the command printed by `make replay`.
Ordinary scenario cleanup and `make clean` retain these volumes.
`make replay-delete RUN=<run-id>` deletes one run.
`make clean-cache` deletes every run-ID-labeled SHIRE archive, its stored telemetry and bookmarks, and its pinned image tag.
It refuses to delete any run while a labeled archive is mounted by a running container, so stop live and replay containers first.
Older unlabeled `gsw-data` volumes remain untouched.
Because `make uninstall` invokes `clean-cache`, it also deletes labeled replay archives.

### Inspect telemetry at a replay timestamp

Keep the visualization and Yamcs open in separate tabs.
Pause or scrub the visualization, then read the UTC timestamp shown below **SHIRE Visualization**.
The visualization timeline does not automatically move the Yamcs telemetry pages to that time, so use this UTC timestamp when selecting a historical range.
Use the port printed by `make replay` for the Yamcs web UI, normally [http://localhost:8090](http://localhost:8090).

1. In Yamcs, select the `shire` instance and open **Telemetry → Parameters**.
2. Search for a parameter such as `/SHIRE_VISUAL/ECLIPSE`, open it, and choose **Historical Data**.
3. Set a narrow UTC range around the visualization timestamp to see the archived samples and their generation times.
4. Use the **Chart** tab for a numeric trend, or create a **Telemetry → Parameter Lists** list to inspect several parameters together.

`/SHIRE_VISUAL/SIM_TIME` gives elapsed simulation seconds, `/SHIRE_VISUAL/POSITION_N_1` gives the first inertial position component in meters, and `/SHIRE_VISUAL/SUN_BODY_1` gives the first body-frame Sun-vector component.
`/SHIRE_VISUAL/ECLIPSE` is 0 for sunlight and 1 for eclipse.
The separate `/SIM_42_TRUTH` parameters provide the legacy 1 Hz truth stream.
Use **Historical Data** for a past instant.
**Summary** or **Realtime** follows the selected Yamcs processor rather than the visualization scrub position.
For packet coverage, open **Archive Browser**, find `/SHIRE_VISUAL/SHIRE_VISUAL_DATA`, and use **Jump to...** or select an indexed time range.

For an exact sample window, the Yamcs native parameter-history API accepts UTC bounds.
Replace the example time with the UTC shown in the viewer and replace port 8090 if `make replay` used `PORT=`:

```sh
curl -fsSG 'http://localhost:8090/api/archive/shire/parameters/SHIRE_VISUAL/ECLIPSE' \
  --data-urlencode 'start=2023-09-16T23:00:00Z' \
  --data-urlencode 'stop=2023-09-16T23:00:01Z' \
  --data-urlencode 'order=asc' \
  --data-urlencode 'limit=25' \
  --data-urlencode 'norealtime=true' \
  --data-urlencode 'source=ParameterArchive'
```

That one-second query returned 20 archived values in a run recorded at 20 Hz.
Change the parameter path to inspect another field.
If parameter indexing is incomplete for a requested interval, repeat the request with `source=replay` to extract values from Yamcs's native packet archive.
See Yamcs's [Telemetry](https://docs.yamcs.org/yamcs-server-manual/web-interface/telemetry/), [Archive Browser](https://docs.yamcs.org/yamcs-server-manual/web-interface/archive/), and [parameter-history API](https://docs.yamcs.org/yamcs-http-api/parameter-values/list-parameter-history/) documentation.

Scenario and performance reports distinguish simulation success from archive completeness.
The archive check compares Director publication counts with Yamcs packet sequences and generation timestamps, reports missing samples, and probes the first, middle, and final positions through Yamcs parameter history.
If indexing is incomplete, it probes Yamcs native packet replay instead.
`replay_ready: false` means the run is still useful for diagnosis, but its visualization history is incomplete.
No secondary telemetry database or frame directory is created.

The packet contains 42 simulation time, UTC civil seconds since J2000, inertial position and velocity, vector-first/scalar-last attitude quaternion, Earth-fixed transformation, body Sun vector, angular rates, eclipse state, run ID, spacecraft ID, and tick sequence.
The viewer transforms inertial state to Earth-fixed coordinates using 42's matrix.
It interpolates only adjacent valid samples.
Telemetry gaps are shown rather than filled with invented state.

Legacy Yamcs volumes are not assigned a run ID.
Their historical run boundaries may be ambiguous, especially if repeated epochs were written into one database.
Preserve them and inspect their packet archive before making replay claims.

# Scenario screenshots

Scenario pages show screenshots of one completed run.
SHIRE captures them with a script, so any contributor can refresh every image after a change without taking screenshots by hand.
The script starts the DRM, replays the operator steps in a headless browser, and writes the images the page references.

## Set up once

```bash
make screenshots-setup
```

This creates a Python environment under `build/screenshots-venv` and installs Playwright with headless Chromium.
Nothing is installed system wide.
If the host lacks shared libraries that Chromium needs, the setup unpacks them under `build/screenshots-libs` without requiring root.
When that is not possible, it prints the `playwright install-deps` command to run with `sudo`.

## Capture a scenario

```bash
make
make screenshots SCENARIO=<name>
```

The capture starts the lab the same way `make start` does, so port 8090 must be free.
It waits for YAMCS, runs the manifest steps in order, writes the images, and stops the lab.
Run `make` first so the images match the current source.

| Option | Effect |
| --- | --- |
| `ONLY="<shot> <shot>"` | Write only the named shots, while every step still runs so later state is correct. |
| `ATTACH=1` | Capture from a lab that is already running instead of starting and stopping one. |

Images are written to `atlas/docs/assets/scenarios/<name>/`.
Review each one before committing it, as described in [Review the images](#review-the-images).

## Manifests

A manifest lists the steps for one scenario in the order the scenario page presents them.
It lives at `atlas/screenshots/<name>.yaml`.

```yaml
scenario: commissioning
doc: scenarios/commissioning.md
steps:
  - goto: /links
  - wait: {text: debug-in}
  - shot: {name: 04-yamcs-startup-links}
  - command:
      name: /SC/SC_COMMANDS/SC_START_RTS
      args: {RTSID: 6, PADDING: 0}
  - shot: {name: 06-start-pass-rts-command}
```

The top level keys are:

| Key | Meaning |
| --- | --- |
| `scenario` | The scenario name, which names the image folder. |
| `doc` | The scenario page, relative to `atlas/docs`, used to check image references. |
| `viewport` | Browser size, which defaults to 1280 by 800. |
| `base_url` | The YAMCS address, which defaults to `http://localhost:8090`. |
| `redact` | Extra `pattern` and `replace` rules applied to terminal text. |

Each step has exactly one action:

| Action | Effect |
| --- | --- |
| `goto: /links` | Open a YAMCS page, with the realtime processor selected, or the viewer at `/visualization/`. |
| `click: <text>` | Click the first element showing that text, or use `{selector: <css>}`. |
| `fill: {selector: <css>, value: <text>}` | Type a value into a field. |
| `press: {selector: <css>, key: <key>}` | Press a key in a field, such as Enter to apply a search. |
| `command: {name, args, send}` | Open a command form, fill its arguments, and send it when `send` is true. |
| `wait: {text, selector, or ms}` | Wait for page content or a fixed time, with an optional `timeout_s`. |
| `wait_param: {name, equals, fresh}` | Wait until telemetry holds a value, and with `fresh` until a new packet arrives. |
| `wait_log: {pattern, source}` | Wait until a log line appears, so timing follows the run and not a guess. |
| `shot: {name, ...}` | Write `<name>.png`. |

Prefer `wait_param` and `wait_log` over `wait: {ms: ...}`.
Simulation speed and host load change how long a step takes, but they do not change what the run reports.

## Web shots

A web shot captures the browser viewport.
Add `clip: <css>` to capture one element, `full_page: true` to capture the whole page, and `settle_ms` to wait before capturing.
Add `hide: [<css>]` to blank elements, such as a host name, for that image only.

## Terminal shots

Services such as the flight software write to a terminal, which a browser cannot capture.
A terminal shot renders log text into a styled frame instead, with each service's prefix colored consistently.

```yaml
- shot:
    name: 08-cfe-es-noop-event
    kind: terminal
    source: compose_logs
    service: shire-fsw
    after: "CFE_ES 3: No-op command"
    tail: 4
```

The `source` is `compose_logs`, which reads the current output of the lab, or `start_log`, which reads the output of the `make start` that the capture launched.
Use `start_log` for the container start lines, which Compose logs do not retain.
The line selectors are applied in this order:

| Selector | Effect |
| --- | --- |
| `after` | Start at the newest line matching the pattern. |
| `include` | Keep only lines matching a pattern or any pattern in a list. |
| `exclude` | Drop lines matching a pattern. |
| `head` and `tail` | Keep the first or last lines that remain. |

A terminal shot that selects no lines fails the run, so a wrong pattern never produces an empty image.

## Redaction

Terminal text is redacted before it is drawn.
Build banners that name the account and container that built an image are always replaced with `shire@builder`.
Add `redact` rules to the manifest for anything else that should not be published.
Web shots show only the browser viewport, so window titles and address bars are not captured.

## Check agreement with the page

```bash
make screenshots-check SCENARIO=<name>
```

The check reports a shot that has no generated image, a shot the scenario page does not reference, and an image the page references that has no shot.
Run it, together with `make docs-check`, before committing screenshot changes.

## Review the images

A successful capture shows that every step ran, not that every image is right.
Open each changed image and confirm that it shows what the page text says it shows.
Check that nothing private appears, such as host names, keys, or unrelated mission data.
Counters, times, and file names differ between runs, so the page text describes values to look for and not the exact numbers.

***
Last reviewed: 20261026

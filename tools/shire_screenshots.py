#!/usr/bin/env python3
"""Scripted capture of scenario documentation screenshots.

A scenario's screenshots are described by a manifest (atlas/screenshots/<scenario>.yaml): an ordered list
of steps that drive the YAMCS web UI, the 42 viewer, or a rendered terminal frame, in the same order the
scenario page walks an operator through them. Replaying the steps against a freshly started stack
regenerates every image, so docs stay current without anyone taking screenshots by hand.

This module holds the parts that do not need a browser or a running stack (manifest validation, log
selection and redaction, terminal rendering, the YAMCS REST client, and the step engine, which takes its
page and log sources as arguments so tests can substitute stand-ins). tools/shire-screenshots.py is the
command line that wires them to Playwright and Docker.
"""

from __future__ import annotations

import dataclasses
import html
import pathlib
import re
import time
from typing import Any, Callable

import requests
import yaml

from shire_provenance import ROOT

ATLAS = ROOT / "atlas"
DOCS = ATLAS / "docs"
MANIFEST_DIR = ATLAS / "screenshots"
VENV_DIR = ROOT / "build" / "screenshots-venv"
LIBS_DIR = ROOT / "build" / "screenshots-libs"

ACTIONS = ("goto", "click", "fill", "wait", "wait_param", "wait_log", "command", "shot")
SHOT_NAME = re.compile(r"[a-z0-9][a-z0-9-]*")
SHOT_KINDS = ("web", "terminal")
LOG_SOURCES = ("start_log", "compose_logs")

# Build banners print the user and container id of whoever built the image. Screenshots are published, so
# those are replaced with fixed text rather than leaking a developer's account and host.
DEFAULT_REDACTIONS = [
    {"pattern": r"\b[\w.-]+@[0-9a-f]{12}\b", "replace": "shire@builder"},
]

# Terminal palette (a dark theme close to the one the existing commissioning images use).
TERMINAL_BACKGROUND = "#0c0c0c"
TERMINAL_FOREGROUND = "#e6e6e6"
TERMINAL_SERVICE_COLORS = ["#7bd88f", "#c586c0", "#4fc1ff", "#e5c07b", "#e06c75", "#56b6c2"]

ANSI_PATTERN = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
SERVICE_PREFIX = re.compile(r"^(?P<service>[\w.-]+?)\s+\|(?P<rest>.*)$")


class ManifestError(ValueError):
    """The manifest is malformed. The message names the offending step."""


@dataclasses.dataclass
class Manifest:
    scenario: str
    doc: str | None
    output_dir: pathlib.Path
    base_url: str
    instance: str
    processor: str
    viewport: dict[str, int]
    redactions: list[dict[str, str]]
    steps: list[dict[str, Any]]

    @property
    def shot_names(self) -> list[str]:
        return [step["shot"]["name"] for step in self.steps if "shot" in step]


def display_path(path: pathlib.Path) -> str:
    """The repository relative form of a path for messages, or the path itself when it is elsewhere."""
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def manifest_path(scenario: str) -> pathlib.Path:
    return MANIFEST_DIR / f"{scenario}.yaml"


def load_manifest(path: pathlib.Path) -> Manifest:
    """Reads and validates a manifest. Every problem is a ManifestError, so a typo fails before any
    container starts rather than halfway through a ten minute run."""
    data = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    if not isinstance(data, dict):
        raise ManifestError(f"{path.name}: top level must be a mapping")
    scenario = data.get("scenario")
    if not isinstance(scenario, str) or not scenario:
        raise ManifestError(f"{path.name}: 'scenario' is required")
    steps = data.get("steps")
    if not isinstance(steps, list) or not steps:
        raise ManifestError(f"{path.name}: 'steps' must be a non-empty list")

    seen: set[str] = set()
    for index, step in enumerate(steps, 1):
        validate_step(index, step, seen)

    yamcs = data.get("yamcs") or {}
    viewport = {"width": 1280, "height": 800, **(data.get("viewport") or {})}
    redactions = DEFAULT_REDACTIONS + list(data.get("redact") or [])
    for rule in redactions:
        try:
            re.compile(rule["pattern"])
        except (KeyError, TypeError, re.error) as error:
            raise ManifestError(f"{path.name}: invalid redact rule {rule!r}: {error}") from error
    return Manifest(
        scenario=scenario,
        doc=data.get("doc"),
        output_dir=DOCS / data.get("output_dir", f"assets/scenarios/{scenario}"),
        base_url=str(data.get("base_url", "http://localhost:8090")).rstrip("/"),
        instance=yamcs.get("instance", "shire"),
        processor=yamcs.get("processor", "realtime"),
        viewport=viewport,
        redactions=redactions,
        steps=steps,
    )


def validate_step(index: int, step: object, seen: set[str]) -> None:
    where = f"step {index}"
    if not isinstance(step, dict) or len(step) != 1:
        raise ManifestError(f"{where}: each step is a mapping with exactly one action key")
    action, arg = next(iter(step.items()))
    if action not in ACTIONS:
        raise ManifestError(f"{where}: unknown action {action!r} (expected one of {', '.join(ACTIONS)})")
    needs_mapping = {"fill", "wait", "wait_param", "wait_log", "command", "shot"}
    if action in needs_mapping and not isinstance(arg, dict):
        raise ManifestError(f"{where}: {action} takes a mapping")
    if action in {"goto", "click"} and not isinstance(arg, (str, dict)):
        raise ManifestError(f"{where}: {action} takes a string")
    if action == "shot":
        validate_shot(where, arg, seen)
    elif action == "command" and not str(arg.get("name", "")).startswith("/"):
        raise ManifestError(f"{where}: command needs a qualified 'name' such as /SC/SC_COMMANDS/SC_START_RTS")
    elif action == "wait_param" and "name" not in arg:
        raise ManifestError(f"{where}: wait_param needs 'name'")
    elif action == "wait_log" and "pattern" not in arg:
        raise ManifestError(f"{where}: wait_log needs 'pattern'")


def validate_shot(where: str, arg: dict, seen: set[str]) -> None:
    name = arg.get("name")
    if not isinstance(name, str) or not SHOT_NAME.fullmatch(name):
        raise ManifestError(f"{where}: shot 'name' must be lowercase words separated by hyphens, got {name!r}")
    if name in seen:
        raise ManifestError(f"{where}: duplicate shot name {name!r}")
    seen.add(name)
    kind = arg.get("kind", "web")
    if kind not in SHOT_KINDS:
        raise ManifestError(f"{where}: shot kind must be one of {SHOT_KINDS}, got {kind!r}")
    if kind == "terminal" and arg.get("source", "compose_logs") not in LOG_SOURCES:
        raise ManifestError(f"{where}: terminal source must be one of {LOG_SOURCES}")


def check_against_docs(manifest: Manifest) -> list[str]:
    """Problems that tie the manifest to the docs: shots without a page reference, page images without a
    shot, and shots whose image was never generated. Empty means the two agree."""
    problems: list[str] = []
    names = set(manifest.shot_names)
    for name in sorted(names):
        if not (manifest.output_dir / f"{name}.png").exists():
            problems.append(f"{name}: image has not been generated")
    if manifest.doc:
        page = DOCS / manifest.doc
        if not page.exists():
            return problems + [f"doc page does not exist: {manifest.doc}"]
        text = page.read_text(encoding="utf-8")
        folder = manifest.output_dir.relative_to(DOCS).as_posix()
        referenced = set(re.findall(rf"{re.escape(folder)}/([a-z0-9-]+)\.png", text))
        for name in sorted(names - referenced):
            problems.append(f"{name}: not referenced by {manifest.doc}")
        for name in sorted(referenced - names):
            problems.append(f"{name}: referenced by {manifest.doc} but has no shot in the manifest")
    return problems


# ----------------------------------------------------------------------------------------------------------
# Terminal frames


def strip_ansi(text: str) -> str:
    return ANSI_PATTERN.sub("", text)


def apply_redactions(text: str, rules: list[dict[str, str]]) -> str:
    for rule in rules:
        text = re.sub(rule["pattern"], rule.get("replace", ""), text)
    return text


def select_lines(text: str, spec: dict[str, Any]) -> list[str]:
    """Chooses the lines a terminal shot shows. `after` starts at the last line matching a pattern (so a
    repeated event shows its newest occurrence), `include` and `exclude` filter, then `head` or `tail` trim."""
    lines = strip_ansi(text).splitlines()
    after = spec.get("after")
    if after:
        starts = [i for i, line in enumerate(lines) if re.search(after, line)]
        if not starts:
            raise LookupError(f"no log line matches after: {after!r}")
        lines = lines[starts[-1]:]
    include = spec.get("include")
    if include:
        patterns = [include] if isinstance(include, str) else include
        lines = [line for line in lines if any(re.search(pattern, line) for pattern in patterns)]
    exclude = spec.get("exclude")
    if exclude:
        patterns = [exclude] if isinstance(exclude, str) else exclude
        lines = [line for line in lines if not any(re.search(pattern, line) for pattern in patterns)]
    if "head" in spec:
        lines = lines[: int(spec["head"])]
    if "tail" in spec:
        lines = lines[-int(spec["tail"]):]
    if not lines:
        raise LookupError(f"terminal shot selected no log lines from spec {spec!r}")
    return lines


def render_terminal_html(lines: list[str], title: str, max_width_px: int = 1100) -> str:
    """A self contained page holding a terminal frame. The prefix Compose puts before each line is colored
    per service, which the live terminal does and plain `docker compose logs` output does not."""
    colors: dict[str, str] = {}
    rows = []
    for line in lines:
        match = SERVICE_PREFIX.match(line)
        if match:
            service = match["service"]
            color = colors.setdefault(service, TERMINAL_SERVICE_COLORS[len(colors) % len(TERMINAL_SERVICE_COLORS)])
            rows.append(f'<span style="color:{color}">{html.escape(service)}</span>'
                        f'<span class="bar"> |</span>{html.escape(match["rest"])}')
        else:
            rows.append(html.escape(line))
    body = "\n".join(rows)
    return f"""<!doctype html>
<html><head><meta charset="utf-8"><style>
html, body {{ margin: 0; padding: 0; background: #ffffff; }}
#frame {{ display: inline-block; max-width: {max_width_px}px; margin: 0; background: {TERMINAL_BACKGROUND};
  border: 1px solid #3a3a3a; border-radius: 6px; overflow: hidden;
  font-family: "DejaVu Sans Mono", "Ubuntu Mono", monospace; }}
#titlebar {{ background: #2b2b2b; color: #bdbdbd; font-size: 12px; padding: 6px 12px;
  font-family: "DejaVu Sans", sans-serif; }}
#term {{ margin: 0; padding: 10px 12px; color: {TERMINAL_FOREGROUND}; font-size: 13px; line-height: 1.35;
  white-space: pre-wrap; overflow-wrap: anywhere; }}
.bar {{ color: #8a8a8a; }}
</style></head><body>
<div id="frame"><div id="titlebar">{html.escape(title)}</div><pre id="term">{body}</pre></div>
</body></html>
"""


# ----------------------------------------------------------------------------------------------------------
# YAMCS REST


class YamcsClient:
    """The few REST calls the capture needs: readiness, and reading a parameter to wait on a state change."""

    def __init__(self, base_url: str, instance: str, processor: str, session: requests.Session | None = None):
        self.base_url = base_url
        self.instance = instance
        self.processor = processor
        self.session = session or requests.Session()

    def ready(self) -> bool:
        try:
            return self.session.get(f"{self.base_url}/api/", timeout=3).ok
        except requests.RequestException:
            return False

    def parameter(self, name: str) -> dict[str, Any] | None:
        url = f"{self.base_url}/api/processors/{self.instance}/{self.processor}/parameters{name}"
        try:
            response = self.session.get(url, timeout=5)
        except requests.RequestException:
            return None
        if not response.ok:
            return None
        return response.json()


def engineering_value(parameter: dict[str, Any]) -> Any:
    """The python value of a parameter's engineering value (enumerations come back as their label)."""
    value = parameter.get("engValue") or {}
    for key, item in value.items():
        if key != "type" and key.endswith("Value"):
            return item
    return None


def values_equal(actual: Any, expected: Any) -> bool:
    if isinstance(actual, bool) or isinstance(expected, bool):
        return actual == expected
    try:
        return float(actual) == float(expected)
    except (TypeError, ValueError):
        return str(actual) == str(expected)


# ----------------------------------------------------------------------------------------------------------
# Step engine


@dataclasses.dataclass
class Environment:
    """What the engine needs from the outside world. Tests replace each member."""
    page: Any
    yamcs: YamcsClient
    logs: Callable[[str, str | None], str]          # (source, service) -> log text so far
    terminal_page: Any = None
    sleep: Callable[[float], None] = time.sleep
    now: Callable[[], float] = time.monotonic


class CaptureError(RuntimeError):
    """A step could not be completed. The message says which step and why."""


class Runner:
    """Replays a manifest's steps in order. Actions always execute, because later shots depend on the state
    earlier steps created, but only shots selected by `only` (all when empty) are written to disk."""

    def __init__(self, manifest: Manifest, env: Environment, only: set[str] | None = None, dry_run: bool = False):
        self.manifest = manifest
        self.env = env
        self.only = only or set()
        self.dry_run = dry_run
        self.written: list[pathlib.Path] = []

    def run(self) -> list[pathlib.Path]:
        unknown = self.only - set(self.manifest.shot_names)
        if unknown:
            raise CaptureError(f"unknown shot name(s): {', '.join(sorted(unknown))}")
        if not self.dry_run:
            self.manifest.output_dir.mkdir(parents=True, exist_ok=True)
        for index, step in enumerate(self.manifest.steps, 1):
            action, arg = next(iter(step.items()))
            try:
                getattr(self, f"do_{action}")(arg)
            except CaptureError:
                raise
            except Exception as error:
                raise CaptureError(f"step {index} ({action} {arg!r}) failed: {error}") from error
        return self.written

    # -- navigation -------------------------------------------------------------------------------------

    def url(self, path: str) -> str:
        if path.startswith("http"):
            return path
        url = self.manifest.base_url + path
        if path.startswith("/visualization"):
            return url
        separator = "&" if "?" in url else "?"
        return f"{url}{separator}c={self.manifest.instance}__{self.manifest.processor}"

    def do_goto(self, path: str) -> None:
        self.env.page.goto(self.url(path), wait_until="networkidle")

    def do_click(self, arg: str | dict) -> None:
        target = {"text": arg} if isinstance(arg, str) else arg
        self.locator(target).click()

    def do_fill(self, arg: dict) -> None:
        self.locator(arg).fill(str(arg["value"]))

    def locator(self, target: dict):
        page = self.env.page
        if "selector" in target:
            return page.locator(target["selector"]).first
        if "text" in target:
            return page.get_by_text(target["text"], exact=target.get("exact", False)).first
        raise CaptureError(f"locator needs 'selector' or 'text', got {target!r}")

    # -- waiting ----------------------------------------------------------------------------------------

    def do_wait(self, arg: dict) -> None:
        timeout_ms = int(float(arg.get("timeout_s", 30)) * 1000)
        if "ms" in arg:
            self.env.page.wait_for_timeout(int(arg["ms"]))
        elif "selector" in arg:
            self.env.page.wait_for_selector(arg["selector"], timeout=timeout_ms)
        elif "text" in arg:
            self.env.page.get_by_text(arg["text"]).first.wait_for(timeout=timeout_ms)
        else:
            raise CaptureError(f"wait needs one of ms, selector, text, got {arg!r}")

    def poll(self, description: str, timeout_s: float, check: Callable[[], bool], interval_s: float = 1.0) -> None:
        deadline = self.env.now() + timeout_s
        while not check():
            if self.env.now() >= deadline:
                raise CaptureError(f"timed out after {timeout_s:g}s waiting for {description}")
            self.env.sleep(interval_s)

    def do_wait_param(self, arg: dict) -> None:
        """Waits until a parameter holds a value. `fresh: true` additionally requires a new telemetry packet,
        so a screenshot is never taken of the packet that predates the command it is meant to show."""
        name, timeout = arg["name"], float(arg.get("timeout_s", 60))
        baseline = (self.env.yamcs.parameter(name) or {}).get("generationTime") if arg.get("fresh") else None

        def satisfied() -> bool:
            parameter = self.env.yamcs.parameter(name)
            if parameter is None:
                return False
            if baseline is not None and parameter.get("generationTime") == baseline:
                return False
            return "equals" not in arg or values_equal(engineering_value(parameter), arg["equals"])

        self.poll(f"{name} {'to equal ' + repr(arg['equals']) if 'equals' in arg else 'to update'}", timeout, satisfied)

    def do_wait_log(self, arg: dict) -> None:
        source, service = arg.get("source", "compose_logs"), arg.get("service")
        pattern = re.compile(arg["pattern"])
        self.poll(f"log line {arg['pattern']!r}", float(arg.get("timeout_s", 120)),
                  lambda: pattern.search(strip_ansi(self.env.logs(source, service))) is not None)

    # -- commands ---------------------------------------------------------------------------------------

    def do_command(self, arg: dict) -> None:
        """Opens a command's form in the YAMCS UI, fills its arguments, and optionally sends it, so the form
        shown in a screenshot is the same one that produced the command history entry."""
        page = self.env.page
        page.goto(self.url(f"/commanding/send{arg['name']}"), wait_until="networkidle")
        expand = page.get_by_text("with defaults")
        if expand.count():
            expand.first.click()
        for key, value in (arg.get("args") or {}).items():
            self.fill_argument(key, value)
        if arg.get("send", False):
            page.get_by_role("button", name="Send", exact=False).first.click()
            page.get_by_text("View report").first.wait_for()

    def fill_argument(self, key: str, value: Any) -> None:
        page = self.env.page
        label = page.get_by_text(key, exact=True).first
        field = label.locator("xpath=following::*[self::input or self::button][1]")
        tag = field.evaluate("element => element.tagName.toLowerCase()")
        if tag == "input":
            field.fill(str(value))
            return
        field.click()
        # Enumerations open a Material menu. The entry that is already selected is prefixed by a check icon.
        label_pattern = re.compile(rf"^\s*(check\s+)?{re.escape(str(value))}\s*$")
        page.get_by_role("menuitem", name=label_pattern).first.click()

    # -- screenshots ------------------------------------------------------------------------------------

    def selected(self, name: str) -> bool:
        return not self.only or name in self.only

    def do_shot(self, arg: dict) -> None:
        name = arg["name"]
        if not self.selected(name):
            return
        target = self.manifest.output_dir / f"{name}.png"
        if arg.get("kind", "web") == "terminal":
            self.capture_terminal(arg, target)
        else:
            self.capture_web(arg, target)
        self.written.append(target)
        print(f"captured {display_path(target)}", flush=True)

    def capture_web(self, arg: dict, target: pathlib.Path) -> None:
        page = self.env.page
        if arg.get("settle_ms"):
            page.wait_for_timeout(int(arg["settle_ms"]))
        if self.dry_run:
            return
        options: dict[str, Any] = {"path": str(target)}
        hidden = arg.get("hide") or []
        if hidden:
            # `style` applies only while the image is taken, so the page itself is left untouched.
            options["style"] = "".join(f"{selector} {{ visibility: hidden !important; }}" for selector in hidden)
        if "clip" in arg:
            page.locator(arg["clip"]).first.screenshot(**options)
        else:
            page.screenshot(full_page=bool(arg.get("full_page", False)), **options)

    def capture_terminal(self, arg: dict, target: pathlib.Path) -> None:
        text = self.env.logs(arg.get("source", "compose_logs"), arg.get("service"))
        lines = select_lines(apply_redactions(text, self.manifest.redactions), arg)
        if self.dry_run:
            return
        # Rendered on its own page so the YAMCS page the walkthrough is on keeps its state.
        page = self.env.terminal_page or self.env.page
        page.set_content(render_terminal_html(lines, arg.get("title", "SHIRE")))
        page.locator("#frame").screenshot(path=str(target))

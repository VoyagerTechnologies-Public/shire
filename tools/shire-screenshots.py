#!/usr/bin/env python3
"""Capture a scenario's documentation screenshots without a human at the keyboard.

Usage:
    python3 tools/shire-screenshots.py setup
    python3 tools/shire-screenshots.py check --scenario <name>
    build/screenshots-venv/bin/python tools/shire-screenshots.py run --scenario <name> [--only <shot> ...]

`setup` creates a virtual environment under build/ with Playwright and headless Chromium (nothing is
installed system wide). `check` validates a manifest and confirms it agrees with the scenario page.
`run` starts the DRM the way `make start` does, replays the manifest's steps in a headless browser, writes the
images, and stops the stack. Use --attach to capture from a stack that is already running instead.

Run `make` first so the images are current. Manifests live in atlas/screenshots/<scenario>.yaml and the
how-to guide "Scenario screenshots" describes their steps.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import time

import yaml

from shire_provenance import ROOT
from shire_screenshots import (CaptureError, Environment, LIBS_DIR, ManifestError, Runner, VENV_DIR,
                               YamcsClient, check_against_docs, display_path, load_manifest, manifest_path)

REQUIREMENTS = ROOT / "tools" / "requirements-screenshots.txt"
RUN_DIR = ROOT / "build" / "screenshots-run"
READY_TIMEOUT_S = 300

# Chromium's headless shell needs these shared libraries. Where the host lacks them they are unpacked from
# the distribution's packages into build/ (no root needed), and a hint is printed for installing them properly.
LIBRARY_PACKAGES = {
    "libnspr4.so": ["libnspr4"],
    "libnss3.so": ["libnss3"],
    "libnssutil3.so": ["libnss3"],
    "libasound.so.2": ["libasound2t64", "libasound2"],
}

# Software rendering so the 42 viewer's WebGL canvas draws on a machine without a GPU.
BROWSER_ARGS = ["--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"]


def venv_python() -> pathlib.Path:
    return VENV_DIR / "bin" / "python"


def library_path() -> str:
    lib = LIBS_DIR / "usr" / "lib" / "x86_64-linux-gnu"
    parts = [str(lib)] if lib.is_dir() else []
    if os.environ.get("LD_LIBRARY_PATH"):
        parts.append(os.environ["LD_LIBRARY_PATH"])
    return ":".join(parts)


def sh(cmd: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True, **kwargs)


# ----------------------------------------------------------------------------------------------------------
# setup


def setup(_: argparse.Namespace) -> int:
    if not venv_python().exists():
        print(f"creating {VENV_DIR.relative_to(ROOT)}")
        result = sh([sys.executable, "-m", "venv", str(VENV_DIR)])
        if result.returncode != 0:
            # Debian and Ubuntu ship `venv` without `ensurepip`. Create the environment bare and install
            # into it with the system pip instead of asking for `apt install python3-venv`.
            shutil.rmtree(VENV_DIR, ignore_errors=True)
            subprocess.run([sys.executable, "-m", "venv", "--without-pip", str(VENV_DIR)], check=True)
    pip = [str(venv_python()), "-m", "pip"]
    if sh(pip + ["--version"]).returncode == 0:
        install = pip + ["install", "-r", str(REQUIREMENTS)]
    else:
        install = ["pip3", "--python", str(venv_python()), "install", "-r", str(REQUIREMENTS)]
    subprocess.run(install, check=True)
    subprocess.run([str(venv_python()), "-m", "playwright", "install", "chromium"], check=True)
    return ensure_libraries()


def chromium_binary() -> pathlib.Path | None:
    found = sorted((pathlib.Path.home() / ".cache" / "ms-playwright").glob("chromium_headless_shell-*/*/chrome-headless-shell"))
    return found[-1] if found else None


def missing_libraries(binary: pathlib.Path) -> list[str]:
    env = {**os.environ, "LD_LIBRARY_PATH": library_path()}
    output = sh(["ldd", str(binary)], env=env).stdout
    return [line.split()[0] for line in output.splitlines() if "not found" in line]


def ensure_libraries() -> int:
    binary = chromium_binary()
    if binary is None:
        print("Chromium was not found after installation", file=sys.stderr)
        return 1
    missing = missing_libraries(binary)
    if not missing:
        print("browser dependencies are present")
        return 0
    print(f"missing shared libraries: {', '.join(missing)}")
    packages: list[str] = []
    for library in missing:
        candidates = next((v for k, v in LIBRARY_PACKAGES.items() if library.startswith(k)), [])
        package = next((c for c in candidates if sh(["apt-cache", "show", c]).returncode == 0), None)
        if package and package not in packages:
            packages.append(package)
    staging = LIBS_DIR / "debs"
    staging.mkdir(parents=True, exist_ok=True)
    if packages and sh(["apt-get", "download", *packages], cwd=staging).returncode == 0:
        for deb in staging.glob("*.deb"):
            subprocess.run(["dpkg-deb", "-x", str(deb), str(LIBS_DIR)], check=True)
    still_missing = missing_libraries(binary)
    if still_missing:
        print(f"could not provide: {', '.join(still_missing)}\n"
              f"Install them with: sudo {venv_python()} -m playwright install-deps chromium", file=sys.stderr)
        return 1
    print(f"unpacked browser dependencies into {LIBS_DIR.relative_to(ROOT)}")
    return 0


# ----------------------------------------------------------------------------------------------------------
# check


def check(args: argparse.Namespace) -> int:
    try:
        manifest = load_manifest(args.manifest or manifest_path(args.scenario))
    except (OSError, ManifestError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    problems = check_against_docs(manifest)
    for problem in problems:
        print(f"{args.scenario}: {problem}", file=sys.stderr)
    if not problems:
        print(f"{args.scenario}: {len(manifest.shot_names)} screenshots match {manifest.doc or 'the manifest'}")
    return 1 if problems else 0


# ----------------------------------------------------------------------------------------------------------
# run


def active_mission() -> str:
    return yaml.safe_load((ROOT / "build" / "active.yaml").read_text(encoding="utf-8"))["mission"]


def start_stack(log_path: pathlib.Path) -> subprocess.Popen:
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    log = log_path.open("w", encoding="utf-8")
    # A new session lets the whole `make` tree be signalled together when the capture ends.
    return subprocess.Popen(["make", "start"], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, text=True,
                            start_new_session=True)


def stop_stack(process: subprocess.Popen | None) -> None:
    if process is not None and process.poll() is None:
        os.killpg(os.getpgid(process.pid), signal.SIGINT)
        try:
            process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            os.killpg(os.getpgid(process.pid), signal.SIGKILL)
    subprocess.run(["make", "stop"], cwd=ROOT, check=False)


def wait_ready(yamcs: YamcsClient, process: subprocess.Popen | None) -> None:
    deadline = time.monotonic() + READY_TIMEOUT_S
    while not yamcs.ready():
        if process is not None and process.poll() is not None:
            raise CaptureError(f"`make start` exited with code {process.returncode} before YAMCS was ready; "
                               f"see {(RUN_DIR / 'start.log').relative_to(ROOT)}")
        if time.monotonic() > deadline:
            raise CaptureError(f"YAMCS was not ready after {READY_TIMEOUT_S}s")
        time.sleep(2)


def log_reader(start_log: pathlib.Path | None):
    compose_file = ROOT / "build" / active_mission() / "shire-compose.yaml"

    def read(source: str, service: str | None) -> str:
        if source == "start_log":
            if start_log is None or not start_log.exists():
                raise CaptureError("a start_log shot needs the stack started by this tool (omit --attach) "
                                   "or --start-log pointing at a saved `make start` output")
            return start_log.read_text(encoding="utf-8", errors="replace")
        command = ["docker", "compose", "-f", str(compose_file), "logs", "--no-color"]
        if service:
            command.append(service)
        result = sh(command)
        if result.returncode != 0:
            raise CaptureError(f"docker compose logs failed: {result.stderr.strip()}")
        return result.stdout

    return read


def run(args: argparse.Namespace) -> int:
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        print("Playwright is not installed here. Run `make screenshots-setup` first.", file=sys.stderr)
        return 2
    try:
        manifest = load_manifest(args.manifest or manifest_path(args.scenario))
    except (OSError, ManifestError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    if args.base_url:
        manifest.base_url = args.base_url.rstrip("/")

    yamcs = YamcsClient(manifest.base_url, manifest.instance, manifest.processor)
    process = None
    start_log = args.start_log
    if not args.attach:
        if yamcs.ready():
            print("A stack is already answering at "
                  f"{manifest.base_url}. Use --attach to capture from it, or `make stop` first.", file=sys.stderr)
            return 2
        start_log = RUN_DIR / "start.log"
        process = start_stack(start_log)
    try:
        wait_ready(yamcs, process)
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(args=BROWSER_ARGS, env={**os.environ, "LD_LIBRARY_PATH": library_path()})
            context = browser.new_context(viewport=manifest.viewport, device_scale_factor=1)
            env = Environment(page=context.new_page(), terminal_page=context.new_page(), yamcs=yamcs,
                              logs=log_reader(start_log))
            written = Runner(manifest, env, only=set(args.only or []), dry_run=args.dry_run).run()
            browser.close()
        print(f"{len(written)} screenshot(s) written to {display_path(manifest.output_dir)}")
        return 0
    except CaptureError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    finally:
        if not args.attach and not args.keep_running:
            stop_stack(process)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("setup", help="install Playwright and headless Chromium under build/").set_defaults(func=setup)
    checking = commands.add_parser("check", help="validate a manifest against its scenario page")
    checking.add_argument("--scenario", required=True)
    checking.add_argument("--manifest", type=pathlib.Path, help="read this file instead of atlas/screenshots/<scenario>.yaml")
    checking.set_defaults(func=check)
    running = commands.add_parser("run", help="capture a scenario's screenshots")
    running.add_argument("--scenario", required=True)
    running.add_argument("--manifest", type=pathlib.Path, help="read this file instead of atlas/screenshots/<scenario>.yaml")
    running.add_argument("--only", nargs="+", metavar="SHOT", help="write only these shots (steps still all run)")
    running.add_argument("--attach", action="store_true", help="use the stack that is already running")
    running.add_argument("--keep-running", action="store_true", help="leave the stack up when finished")
    running.add_argument("--start-log", type=pathlib.Path, help="saved `make start` output for start_log shots with --attach")
    running.add_argument("--base-url", help="override the manifest's YAMCS address")
    running.add_argument("--dry-run", action="store_true", help="run every step but write no images")
    running.set_defaults(func=run)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())

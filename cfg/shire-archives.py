#!/usr/bin/env python3
"""Allocate and reopen per-run native Yamcs archives; no exported telemetry copy."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import io
import json
import os
import pathlib
import re
import signal
import subprocess
import sys
import tarfile
import time
import urllib.error
import urllib.request
import uuid

ROOT = pathlib.Path(__file__).resolve().parent.parent
RUN_RE = re.compile(r"^[0-9a-f]{32}$")
PREFIX = "shire-yamcs-archive-"
LABEL = "shire.archive=true"


def docker(*args: str) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(["docker", *args], check=False, text=True,
                            capture_output=True)
    if result.returncode:
        raise ValueError(f"docker {args[0]} failed: "
                         f"{(result.stderr or result.stdout).strip()}")
    return result


def get_archive(run_id: str) -> tuple[str, dict[str, str]]:
    if not RUN_RE.fullmatch(run_id):
        raise ValueError("RUN must be a 32-character hexadecimal run ID")
    name = PREFIX + run_id
    details = json.loads(docker("volume", "inspect", name).stdout)[0]
    labels = details.get("Labels") or {}
    if labels.get("shire.archive") != "true" or labels.get("shire.run") != run_id:
        raise ValueError(f"{name} is not a SHIRE run archive")
    return name, labels


def attached_containers(volume: str, *, running_only: bool = False) -> list[str]:
    flag = "-q" if running_only else "-aq"
    return docker("ps", flag, "--filter", f"volume={volume}").stdout.split()


def in_use(volume: str) -> bool:
    return bool(attached_containers(volume))


def image_exists(reference: str) -> bool:
    return subprocess.run(["docker", "image", "inspect", reference],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def verify_fallback_snapshot(container: str, port: int) -> None:
    """Reject a replacement image if its visualization MDB differs from the run."""
    url = (f"http://localhost:{port}/api/storage/buckets/shireRuns/objects/"
           "mission-database-config.tar.gz")
    with urllib.request.urlopen(url, timeout=10) as response:
        snapshot = response.read(10_000_001)
    if len(snapshot) > 10_000_000:
        raise ValueError("run mission-database snapshot is unexpectedly large")
    try:
        with tarfile.open(fileobj=io.BytesIO(snapshot), mode="r:gz") as archive:
            for name in ("shire_visual.xtce", "sim_42_truth.xtce"):
                path = f"mdb/{name}"
                member = archive.extractfile(path)
                if member is None:
                    raise ValueError(f"run snapshot lacks {path}")
                original = member.read()
                current = subprocess.run(["docker", "exec", container, "cat",
                                          f"/app/{path}"], capture_output=True,
                                         check=True).stdout
                if hashlib.sha256(original).digest() != hashlib.sha256(current).digest():
                    raise ValueError(f"fallback GSW image has incompatible {path}")
    except (tarfile.TarError, KeyError) as exc:
        raise ValueError(f"run snapshot is unreadable: {exc}") from exc


def allocate(args: argparse.Namespace) -> tuple[str, str]:
    run_id = uuid.uuid4().hex
    volume = PREFIX + run_id
    image_id = docker("image", "inspect", "--format", "{{.Id}}", args.image).stdout.strip()
    archive_image = f"shire-gsw-archive:{run_id}"
    docker("tag", image_id, archive_image)
    labels = {
        "shire.archive": "true", "shire.run": run_id,
        "shire.mission": args.mission, "shire.spacecraft": args.spacecraft,
        "shire.scenario": args.scenario or "manual",
        "shire.started": dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ"),
        "shire.image": args.image, "shire.image_id": image_id,
        "shire.archive_image": archive_image,
    }
    command = ["volume", "create", *[item for key, value in labels.items()
                                    for item in ("--label", f"{key}={value}")], volume]
    docker(*command)
    return run_id, volume


def records() -> list[tuple[str, dict[str, str]]]:
    names = docker("volume", "ls", "-q", "--filter", f"label={LABEL}").stdout.split()
    return [get_archive(name[len(PREFIX):]) for name in names if name.startswith(PREFIX)]


def purge_archives() -> None:
    """Delete only run-ID-labeled SHIRE archives after an all-runs active-use check."""
    archives = records()
    active = [(volume, attached_containers(volume, running_only=True))
              for volume, _ in archives]
    busy = [(volume, containers) for volume, containers in active if containers]
    if busy:
        details = ", ".join(f"{volume} ({', '.join(containers)})"
                            for volume, containers in busy)
        raise ValueError(f"cannot purge archives mounted by running containers: "
                         f"{details}; stop those runs before make clean-cache")
    for volume, _ in archives:
        # Stopped replay and Compose containers still hold Docker volume references.
        for container in attached_containers(volume):
            docker("rm", container)
        docker("volume", "rm", volume)
        print(f"Deleted {volume}")
    # Remove image tags only after every database has been deleted. An image
    # still used by an unrelated container must not leave later archives behind.
    for _, labels in archives:
        image = f"shire-gsw-archive:{labels['shire.run']}"
        if image_exists(image):
            docker("image", "rm", image)
    print(f"Removed {len(archives)} retained SHIRE run archive(s)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("create")
    for p in (create, commands.add_parser("start")):
        p.add_argument("--mission", required=True)
        p.add_argument("--spacecraft", required=True)
        p.add_argument("--scenario", default="manual")
        p.add_argument("--image", required=True)
    start = commands.choices["start"]
    start.add_argument("--compose", required=True)
    commands.add_parser("list")
    commands.add_parser("purge")
    for name in ("replay", "delete"):
        command = commands.add_parser(name)
        command.add_argument("run")
    commands.choices["replay"].add_argument("--port", type=int, default=8090)
    args = parser.parse_args()
    try:
        if args.command == "create":
            run_id, volume = allocate(args)
            print(json.dumps({"run_id": run_id, "volume": volume}))
        elif args.command == "list":
            print("RUN_ID\tSTARTED_UTC\tMISSION\tSPACECRAFT\tSCENARIO")
            for volume, labels in sorted(records(), key=lambda row: row[1].get("shire.started", "")):
                print("\t".join(labels.get(key, "") for key in
                                ("shire.run", "shire.started", "shire.mission",
                                 "shire.spacecraft", "shire.scenario")))
            legacy = [name for name in docker("volume", "ls", "-q").stdout.split()
                      if "gsw-data" in name and not name.startswith(PREFIX)]
            if legacy:
                print("Legacy Yamcs volume(s) with ambiguous run boundaries (preserved):")
                for name in legacy:
                    print(f"  {name}")
        elif args.command == "purge":
            purge_archives()
        elif args.command == "start":
            run_id, volume = allocate(args)
            env = {**os.environ, "SHIRE_YAMCS_VOLUME": volume, "SHIRE_RUN_ID": run_id}
            print(f"[archive] run={run_id} volume={volume}", flush=True)
            process = subprocess.Popen(["docker", "compose", "-f", args.compose, "up"], env=env)
            try:
                return process.wait()
            except KeyboardInterrupt:
                process.send_signal(signal.SIGINT)
                return process.wait()
        elif args.command == "replay":
            volume, labels = get_archive(args.run)
            if in_use(volume):
                raise ValueError(f"{volume} is already mounted by a container")
            if not 1 <= args.port <= 65535:
                raise ValueError("port must be 1 through 65535")
            references = [labels.get("shire.archive_image"),
                          labels.get("shire.image_id"), labels.get("shire.image")]
            image = next((ref for ref in references if ref and image_exists(ref)), None)
            if not image:
                raise ValueError("archive has no available GSW image; rebuild its compatible runtime")
            fallback = (image == labels.get("shire.image") and
                        image != labels.get("shire.image_id"))
            if fallback:
                print("[archive] pinned image unavailable; checking current GSW "
                      "tag against the run mission database", file=sys.stderr)
            container = f"shire-replay-{args.run[:12]}"
            replay_command = ("cp /app/etc/yamcs-replay.yaml /app/etc/yamcs.yaml && "
                              "cp /app/etc/yamcs.shire-replay.yaml /app/etc/yamcs.shire.yaml && "
                              "exec /app/bin/yamcsd")
            docker("run", "-d", "--name", container,
                   "-p", f"{args.port}:8090", "-e", "YAMCS_REPLAY_ONLY=1",
                   "-v", f"{volume}:/app/yamcs-data",
                   "--entrypoint", "/bin/sh", image, "-c", replay_command)
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                running = docker("inspect", "--format", "{{.State.Running}}", container).stdout.strip()
                if running != "true":
                    output = docker("logs", "--tail", "30", container)
                    logs = output.stdout + output.stderr
                    docker("rm", container)
                    raise ValueError(f"replay Yamcs exited during startup:\n{logs}")
                try:
                    with urllib.request.urlopen(f"http://localhost:{args.port}/api/", timeout=1):
                        break
                except (OSError, urllib.error.URLError):
                    time.sleep(.5)
            else:
                raise ValueError(f"replay Yamcs did not become ready; inspect docker logs {container}")
            if fallback:
                try:
                    verify_fallback_snapshot(container, args.port)
                except (OSError, ValueError, subprocess.CalledProcessError) as exc:
                    docker("stop", "-t", "20", container)
                    docker("rm", container)
                    raise ValueError(f"incompatible replay fallback: {exc}") from exc
            print(f"Replay: http://localhost:{args.port}/visualization/?mode=replay&run={args.run}")
            print(f"Stop: docker stop {container} && docker rm {container}")
        else:
            volume, labels = get_archive(args.run)
            if in_use(volume):
                raise ValueError(f"{volume} is still mounted by a container")
            docker("volume", "rm", volume)
            archive_image = labels.get("shire.archive_image")
            if archive_image and image_exists(archive_image):
                docker("image", "rm", archive_image)
            print(f"Deleted {volume}")
    except (subprocess.CalledProcessError, ValueError) as exc:
        print(f"[archive] {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

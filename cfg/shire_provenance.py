#!/usr/bin/env python3
"""Shared git/host provenance helpers for SHIRE's noninteractive runners.

Extracted from shire-perf.py so shire-orchestrator.py, shire-perf.py, and
shire-scenario.py can all stamp their outputs with the same git SHA /
dirty-state / host metadata instead of each tracking it separately.
"""

from __future__ import annotations

import hashlib
import os
import pathlib
import platform
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(command: list[str], *, env: dict[str, str] | None = None,
        check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=ROOT, env=env, check=check,
                          text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)


def untracked_file_hashes(repository: pathlib.Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for value in run(["git", "-C", str(repository), "ls-files", "--others",
                      "--exclude-standard"], check=False).stdout.splitlines():
        path = repository / value
        if path.is_file():
            result[value] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def git_metadata() -> dict[str, object]:
    root_sha = run(["git", "rev-parse", "HEAD"]).stdout.strip()
    submodules: dict[str, dict[str, object]] = {}
    for line in run(["git", "submodule", "status", "--recursive"]).stdout.splitlines():
        fields = line.strip().split()
        if len(fields) >= 2:
            path = fields[1]
            status = run(["git", "-C", path, "status", "--porcelain"], check=False)
            diff = run(["git", "-C", path, "diff", "--binary", "HEAD"], check=False)
            submodules[path] = {
                "sha": fields[0].lstrip("-+"),
                "dirty": bool(status.stdout.strip()),
                "tracked_diff_sha256": hashlib.sha256(
                    diff.stdout.encode("utf-8")).hexdigest(),
                "untracked_files": untracked_file_hashes(ROOT / path),
            }
    root_status = run(["git", "status", "--porcelain", "--ignore-submodules=all"])
    root_diff = run(["git", "diff", "--binary", "HEAD"], check=False).stdout
    return {"root": root_sha, "dirty": bool(root_status.stdout.strip()),
            "tracked_diff_sha256": hashlib.sha256(
                root_diff.encode("utf-8")).hexdigest(),
            "untracked_files": untracked_file_hashes(ROOT),
            "submodules": submodules}


def host_metadata() -> dict[str, object]:
    docker = run(["docker", "version", "--format", "{{.Server.Version}}"], check=False)
    paranoid_path = pathlib.Path("/proc/sys/kernel/perf_event_paranoid")
    try:
        perf_paranoid = int(paranoid_path.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        perf_paranoid = None
    return {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "logical_cpus": os.cpu_count(),
        "docker_server": docker.stdout.strip(),
        "profiling_tools": {tool: shutil.which(tool)
                            for tool in ("perf", "strace", "pidstat")},
        "perf_event_paranoid": perf_paranoid,
    }


def git_head_sha(short: bool = False) -> str:
    """Lightweight helper for callers (e.g. the orchestrator) that just want
    the current commit, without the full submodule/dirty-diff walk that
    git_metadata() does — cheap enough to call on every config render."""
    cmd = ["git", "rev-parse"] + (["--short"] if short else []) + ["HEAD"]
    return run(cmd, check=False).stdout.strip()

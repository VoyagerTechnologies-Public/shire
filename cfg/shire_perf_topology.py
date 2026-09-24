#!/usr/bin/env python3
"""Optional CPU placement for `make perf`'s docker-compose services.

Motivation: a sister port of SHIRE found that pinning its per-tick
participants to separate physical cores raised its unbounded `make perf`
throughput and cut run-to-run variance dramatically. This repo's own
`scheduler.json`/`docker-stats.json` captures (already collected by every
`make perf` trial -- see `cfg/shire-perf.py`) show the same symptom here:
`shire-director`'s CPU-migration count varies by more than 10x between an
otherwise-identical fast run and a slow one, and `shire-gsw` (not itself a
Simulith tick-barrier participant, but a heavy, independently-scheduled
neighbor) runs the hottest of any container. This module turns "isolate the
busiest containers onto their own physical cores" into a portable, opt-in,
measurable default for perf runs specifically -- never for ordinary
`make scenario`/campaign runs, which don't use this module at all.

Every function here that makes a placement *decision* (`plan_placement`,
`_partition_cores`) is pure: topology and requests are passed in as plain
data, so they're fully unit-testable with synthetic topologies and never
touch `/sys`, docker, or the network themselves. Only `detect_physical_topology`
touches the filesystem, and it does nothing but read and return data.

`SHIRE_PERF_CPU_PLACEMENT` controls the mode (read by `cfg/shire-perf.py`):
  - unset or "auto" (default): detect this host's physical-core topology and
    place automatically; falls back to unplaced (no cpuset at all) on any
    non-Linux host, unreadable topology, or too few physical cores to
    usefully isolate every service. Never errors the run -- worst case is
    identical to today's unplaced behavior.
  - "off": explicit opt-out, always unplaced.
  - anything else: a manual override, "service=cpuset,service=cpuset,...",
    e.g. "shire-fsw=4-7,shire-42=8-9" -- used verbatim, letting a restricted
    or unusual host (or a human who's already profiled it) specify exactly
    what they want without relying on auto-detection.
"""

from __future__ import annotations

import dataclasses
import pathlib
import platform
import re

# Compose service names -> requested `cpus:` quota, from cfg/shire-compose.j2.
# Keys match the compose file's service names exactly (not container names).
SERVICE_CPU_REQUESTS: dict[str, float] = {
    "shire-server": 4.0,
    "shire-director": 4.0,
    "shire-fsw": 4.0,
    "shire-42": 2.0,
    "shire-gsw": 2.0,
    "shire-cryptolib": 1.0,
}

# Isolated from each other first, in this order, when physical cores are
# scarce: server/director/fsw/42 are the Simulith tick-barrier participants;
# gsw and cryptolib aren't barrier-gated but are measured neighbors (gsw in
# particular runs the hottest of any container) worth isolating too when
# there's room.
PRIORITY_ORDER = [
    "shire-server", "shire-director", "shire-fsw", "shire-42", "shire-gsw", "shire-cryptolib",
]

# Below this many physical cores, don't attempt separation at all -- with 6
# services needing at least one dedicated physical core each, this leaves a
# minimum 2-core margin for the host/Docker daemon rather than claiming every
# last core.
MIN_PHYSICAL_CORES = 8

_CPUSET_RE = re.compile(r"[0-9]+(-[0-9]+)?(,[0-9]+(-[0-9]+)?)*")


@dataclasses.dataclass(frozen=True)
class PlacementResult:
    mode: str  # "auto" | "off" | "manual"
    assignments: dict[str, str]  # service -> cpuset string ("4-5"); only placed services appear
    unplaced_reason: str | None  # None iff assignments is non-empty
    topology_summary: dict[str, object] | None  # physical_cores/logical_cpus, when topology was read

    def to_report(self) -> dict[str, object]:
        return {
            "mode": self.mode,
            "assignments": self.assignments or None,
            "unplaced_reason": self.unplaced_reason,
            "topology_summary": self.topology_summary,
        }


def detect_physical_topology(cpu_root: pathlib.Path = pathlib.Path("/sys/devices/system/cpu"),
                             ) -> dict[int, list[int]] | None:
    """Maps physical core id -> sorted logical CPU (thread) ids, on Linux,
    from /sys/devices/system/cpu/cpu*/topology/core_id. Returns None on any
    non-Linux host or if the topology can't be read cleanly -- callers must
    treat None as "can't safely place," not retry or error."""
    if platform.system() != "Linux":
        return None
    cores: dict[int, list[int]] = {}
    try:
        cpu_dirs = sorted(
            (p for p in cpu_root.glob("cpu[0-9]*") if p.is_dir()),
            key=lambda p: int(p.name[3:]),
        )
        if not cpu_dirs:
            return None
        for cpu_dir in cpu_dirs:
            logical_id = int(cpu_dir.name[3:])
            core_id_path = cpu_dir / "topology" / "core_id"
            if not core_id_path.exists():
                return None
            core_id = int(core_id_path.read_text().strip())
            cores.setdefault(core_id, []).append(logical_id)
    except (OSError, ValueError):
        return None
    for logical_ids in cores.values():
        logical_ids.sort()
    return cores


def format_cpuset(logical_ids: list[int]) -> str:
    """Formats a sorted list of logical CPU ids as a compact cpuset range
    string, e.g. [0, 1, 2, 4] -> "0-2,4"."""
    if not logical_ids:
        return ""
    ranges: list[tuple[int, int]] = []
    start = prev = logical_ids[0]
    for cur in logical_ids[1:]:
        if cur == prev + 1:
            prev = cur
            continue
        ranges.append((start, prev))
        start = prev = cur
    ranges.append((start, prev))
    return ",".join(f"{a}-{b}" if a != b else str(a) for a, b in ranges)


def _partition_cores(core_ids: list[int], requests: dict[str, float],
                     order: list[str]) -> dict[str, list[int]]:
    """Partitions whole physical cores among `order`'s services,
    proportional to each service's requested `cpus:` quota, never splitting
    one physical core across two services. Every requested service gets at
    least one core as long as enough remain for the services still to come;
    a service that would starve a later one gets capped, not the reverse,
    so no single big quota can crowd out everything after it."""
    total_request = sum(requests[s] for s in order if s in requests)
    remaining = list(core_ids)
    assignment: dict[str, list[int]] = {}
    for i, service in enumerate(order):
        if service not in requests or not remaining:
            continue
        share = requests[service] / total_request
        wanted = max(1, round(share * len(core_ids) / 2))
        later_services = [s for s in order[i:] if s in requests]
        wanted = min(wanted, len(remaining) - (len(later_services) - 1))
        wanted = max(1, wanted)
        take, remaining = remaining[:wanted], remaining[wanted:]
        if take:
            assignment[service] = take
    return assignment


def plan_placement(topology: dict[int, list[int]] | None,
                    service_cpu_requests: dict[str, float],
                    mode: str) -> PlacementResult:
    """Pure placement decision -- no filesystem/docker access. `mode` is
    "off", "auto", or a manual "service=cpuset,..." override string."""
    if mode == "off":
        return PlacementResult("off", {}, "placement disabled (SHIRE_PERF_CPU_PLACEMENT=off)", None)

    if mode != "auto":
        assignments: dict[str, str] = {}
        for entry in mode.split(","):
            entry = entry.strip()
            if not entry:
                continue
            if "=" not in entry:
                return PlacementResult(
                    "manual", {}, f"malformed SHIRE_PERF_CPU_PLACEMENT entry {entry!r} (expected service=cpuset)",
                    None)
            service, cpuset = (part.strip() for part in entry.split("=", 1))
            if not service or not _CPUSET_RE.fullmatch(cpuset):
                return PlacementResult(
                    "manual", {}, f"malformed SHIRE_PERF_CPU_PLACEMENT cpuset {cpuset!r} for {service!r}", None)
            assignments[service] = cpuset
        if not assignments:
            return PlacementResult("manual", {}, "empty SHIRE_PERF_CPU_PLACEMENT override", None)
        return PlacementResult("manual", assignments, None, None)

    if not topology:
        return PlacementResult("auto", {}, "no Linux physical-core topology detected", None)

    core_ids = sorted(topology.keys())
    topology_summary = {
        "physical_cores": len(core_ids),
        "logical_cpus": sum(len(v) for v in topology.values()),
    }
    if len(core_ids) < MIN_PHYSICAL_CORES:
        return PlacementResult(
            "auto", {},
            f"only {len(core_ids)} physical cores detected (need >= {MIN_PHYSICAL_CORES} to isolate every service)",
            topology_summary)

    order = [s for s in PRIORITY_ORDER if s in service_cpu_requests]
    per_service_cores = _partition_cores(core_ids, service_cpu_requests, order)
    if len(per_service_cores) < len(order):
        return PlacementResult("auto", {}, "not enough physical cores to isolate every service", topology_summary)

    assignments = {
        service: format_cpuset(sorted(lid for core in cores for lid in topology[core]))
        for service, cores in per_service_cores.items()
    }
    return PlacementResult("auto", assignments, None, topology_summary)

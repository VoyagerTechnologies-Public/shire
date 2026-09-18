#!/usr/bin/env python3
"""Shared full-stack Docker Compose helpers for SHIRE's noninteractive runners.

Extracted from shire-perf.py so shire-scenario.py (the headless
"confirm a pass" runner) can drive the same compose stack and read the same
completion markers without re-deriving container names or duplicating the
terminal-marker parsing logic.
"""

from __future__ import annotations

import json


def container_names(mission: str, spacecraft: str) -> dict[str, str]:
    return {
        "server": f"shire-server-{mission}",
        "director": f"shire-director-{spacecraft}",
        "fsw": f"shire-fsw-{spacecraft}",
        "42": f"shire-42-{spacecraft}",
        "gsw": f"shire-gsw-{mission}",
        "cryptolib": f"shire-cryptolib-{spacecraft}",
    }


def parse_marker(log: str, marker: str) -> dict[str, object]:
    for line in reversed(log.splitlines()):
        if marker in line:
            start = line.find("{", line.find(marker))
            if start >= 0:
                return json.loads(line[start:])
    raise RuntimeError(f"process exited without {marker}")


def try_parse_marker(log: str, marker: str) -> dict[str, object] | None:
    """Non-raising variant of parse_marker(), for callers that treat a
    missing marker as one signal among several rather than a hard error."""
    try:
        return parse_marker(log, marker)
    except RuntimeError:
        return None

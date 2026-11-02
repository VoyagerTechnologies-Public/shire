#!/usr/bin/env python3
"""Validates SHIRE's configuration tree without Docker or a build. Run with `make cfg-check`."""
import sys

from shire_config_check import check_repository


def main() -> int:
    errors = check_repository()
    for message in errors:
        print(f"[cfg-check] ERROR: {message}", file=sys.stderr)
    if errors:
        print(f"[cfg-check] {len(errors)} problem(s) found", file=sys.stderr)
        return 1
    print("[cfg-check] configuration is consistent")
    return 0


if __name__ == "__main__":
    sys.exit(main())

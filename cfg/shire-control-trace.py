#!/usr/bin/env python3
"""Read SHIRE's ordered, lossless version 1 control trace."""

from __future__ import annotations

import argparse
import json
import pathlib
import struct
from collections.abc import Iterator


class RecordReader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def take(self, fmt: str):
        result = struct.unpack_from("<" + fmt, self.data, self.offset)
        self.offset += struct.calcsize("<" + fmt)
        return result[0] if len(result) == 1 else list(result)


def read_state(reader: RecordReader) -> dict[str, object]:
    state: dict[str, object] = {
        "sim_time": reader.take("d"),
        "dyn_time": reader.take("d"),
    }
    for name, count in (
        ("qn", 4), ("wn", 3), ("pos_n", 3), ("vel_n", 3),
        ("pos_r", 3), ("vel_r", 3), ("sun_vector_body", 3),
        ("mag_field_body", 3), ("sun_vector_inertial", 3),
        ("mag_field_inertial", 3), ("hvb", 3),
    ):
        state[name] = reader.take(f"{count}d")
    state["mass"] = reader.take("d")
    state["cm"] = reader.take("3d")
    inertia = reader.take("9d")
    state["inertia"] = [inertia[i:i + 3] for i in (0, 3, 6)]
    state["eclipse"] = reader.take("I")
    state["atmo_density"] = reader.take("d")
    state["spacecraft_id"] = reader.take("I")
    state["exists"] = reader.take("I")
    label = reader.take("40s")
    state["label"] = label.split(b"\0", 1)[0].decode("utf-8", "replace")
    state["valid"] = reader.take("I")
    return state


def read_command(reader: RecordReader) -> dict[str, object]:
    command: dict[str, object] = {
        "type": reader.take("I"),
        "spacecraft_id": reader.take("I"),
        "valid": reader.take("I"),
    }
    kind = command["type"]
    if kind == 1:
        command.update(enable_mask=reader.take("I"), dipole=reader.take("3d"))
    elif kind == 2:
        command.update(enable_mask=reader.take("I"), torque=reader.take("4d"))
    elif kind == 3:
        command.update(enable_mask=reader.take("I"), thrust=reader.take("3d"),
                       torque=reader.take("3d"))
    elif kind == 4:
        command.update(mode=reader.take("I"), parm=reader.take("I"),
                       frame=reader.take("I"), qrn=reader.take("4d"),
                       pri_w=reader.take("3d"), sec_w=reader.take("3d"),
                       have_pri=reader.take("I"), have_sec=reader.take("I"),
                       have_qrn=reader.take("I"))
    elif kind not in (0, 5):
        raise ValueError(f"unknown actuator command type {kind}")
    return command


def records(path: pathlib.Path) -> Iterator[dict[str, object]]:
    with path.open("rb") as trace:
        if trace.read(8) != b"SHCT\x01\x00\x00\x00":
            raise ValueError("not a SHCT version 1 control trace")
        while (length_bytes := trace.read(4)):
            if len(length_bytes) != 4:
                raise ValueError("truncated record length")
            length = struct.unpack("<I", length_bytes)[0]
            if length < 476 or length > 16384:
                raise ValueError(f"invalid record length {length}")
            data = trace.read(length)
            if len(data) != length:
                raise ValueError("truncated record")
            reader = RecordReader(data)
            sequence = reader.take("Q")
            tick_time_ns = reader.take("Q")
            state = read_state(reader)
            command_count = reader.take("I")
            if command_count > 64:
                raise ValueError("invalid actuator command count")
            commands = [read_command(reader) for _ in range(command_count)]
            if reader.offset != length:
                raise ValueError(f"unexpected bytes in tick {sequence}")
            yield {"sequence": sequence, "tick_time_ns": tick_time_ns,
                   "state": state, "commands": commands}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=pathlib.Path)
    parser.add_argument("--jsonl", action="store_true",
                        help="write one decoded JSON object per tick")
    args = parser.parse_args()
    ticks = 0
    commands = 0
    first_dyn = last_dyn = None
    for record in records(args.trace):
        if record["sequence"] != ticks:
            raise ValueError(f"out-of-order tick {record['sequence']} after {ticks}")
        dyn = record["state"]["dyn_time"]
        if first_dyn is None:
            first_dyn = dyn
        last_dyn = dyn
        ticks += 1
        commands += len(record["commands"])
        if args.jsonl:
            print(json.dumps(record, separators=(",", ":")))
    if not args.jsonl:
        print(json.dumps({"format_version": 1, "ticks": ticks,
                          "actuator_commands": commands,
                          "first_dyn_time": first_dyn,
                          "last_dyn_time": last_dyn}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

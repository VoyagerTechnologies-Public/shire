"""Check native Yamcs packet/parameter archive coverage for one simulation run."""
from __future__ import annotations

import base64
import datetime as dt
import json
import struct
import time
import urllib.parse
import urllib.request

PACKET = "/SHIRE_VISUAL/SHIRE_VISUAL_DATA"
PARAMETER = "/SHIRE_VISUAL/POSITION_N_1"
J2000_MS = 946728000000


def get_json(url: str, query: dict[str, str]) -> dict:
    with urllib.request.urlopen(url + "?" + urllib.parse.urlencode(query), timeout=10) as response:
        return json.load(response)


def coverage(base_url: str, run_id: str, published: int, send_errors: int,
             interval: int, settle_s: float = 3.0) -> dict:
    """Keep packet reads paged; never export telemetry into a second store."""
    archive = base_url.rstrip("/") + "/api/archive/shire"
    deadline = time.monotonic() + settle_s
    entries: list[tuple[int, str, float]] = []
    while True:
        entries.clear()
        query = {"name": PACKET, "order": "asc", "limit": "500"}
        while True:
            page = get_json(archive + "/packets", query)
            for record in page.get("packets", []):
                raw = base64.b64decode(record["packet"])
                if len(raw) != 256 or raw[8:24].hex() != run_id:
                    raise ValueError("visual packet size or run ID mismatch")
                entries.append((struct.unpack_from("<Q", raw, 28)[0],
                                record["generationTime"],
                                struct.unpack_from("<d", raw, 44)[0]))
            token = page.get("continuationToken")
            if not token:
                break
            query["next"] = token
        if len(entries) >= published or time.monotonic() >= deadline:
            break
        time.sleep(.25)
    sequences = [row[0] for row in entries]
    duplicate = len(sequences) - len(set(sequences))
    seen = set(sequences)
    missing = [seq for seq in range(sequences[0], sequences[-1] + 1, interval)
               if seq not in seen] if sequences else []
    time_errors = []
    for seq, stamp, utc in entries:
        actual = dt.datetime.fromisoformat(stamp.replace("Z", "+00:00"))
        expected = dt.datetime.fromtimestamp((J2000_MS + round(utc * 1000)) / 1000,
                                             tz=dt.timezone.utc)
        if abs((actual - expected).total_seconds()) > .001:
            time_errors.append(seq)
    probes = []
    for index in sorted(set((0, len(entries)//2, len(entries)-1))) if entries else []:
        seq, stamp, _ = entries[index]
        start = dt.datetime.fromisoformat(stamp.replace("Z", "+00:00"))
        stop = start + dt.timedelta(milliseconds=1)
        common = {"start": (start - dt.timedelta(milliseconds=1)).isoformat(),
                  "stop": stop.isoformat(),
                  "norealtime": "true", "noreplay": "true", "limit": "5"}
        values = get_json(archive + "/parameters" + PARAMETER,
                          {**common, "source": "ParameterArchive"}).get("parameter", [])
        source = "ParameterArchive"
        if not values:
            values = get_json(archive + "/parameters" + PARAMETER,
                              {**common, "source": "replay"}).get("parameter", [])
            source = "replay" if values else "missing"
        probes.append({"sequence": seq, "generation_time": stamp, "source": source})
    missing_total = max(0, published - len(entries))
    report = {"run_id": run_id, "published": published, "send_errors": send_errors,
              "archived": len(entries), "first_sequence": sequences[0] if sequences else None,
              "last_sequence": sequences[-1] if sequences else None,
              "missing_count": missing_total, "missing_sequences": missing,
              "unlocated_missing_count": max(0, missing_total - len(missing)),
              "duplicate_sequences": duplicate, "timestamp_mismatches": time_errors,
              "parameter_probes": probes}
    report["replay_ready"] = (published > 0 and missing_total == 0 and send_errors == 0 and
                              duplicate == 0 and not time_errors and
                              all(p["source"] != "missing" for p in probes))
    data = json.dumps(report, sort_keys=True).encode()
    request = urllib.request.Request(base_url.rstrip("/") +
             "/api/storage/buckets/shireRuns/objects/archive-status.json", data=data,
             headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(request, timeout=10):
        pass
    return report

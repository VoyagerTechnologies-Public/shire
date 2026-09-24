"""Unit tests for shire_perf_topology.py's placement decisions.

All of plan_placement()/detect_physical_topology()'s decision logic is pure
or takes its filesystem root as an argument, so these tests never touch the
real host's /sys or docker -- synthetic topologies stand in for both a
real 11-physical-core/22-thread host and hosts too small to place on.

Run directly: python3 cfg/test_shire_perf_topology.py
"""
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import shire_perf_topology as topo  # noqa: E402


def make_topology(physical_cores: int, threads_per_core: int = 2) -> dict[int, list[int]]:
    """A synthetic topology matching how detect_physical_topology() shapes
    its real return value: core_id -> sorted logical CPU ids, interleaved
    the way Linux typically numbers hyperthread siblings (0,11 / 1,12 / ...
    for an 11-core/22-thread host) is not assumed -- callers only rely on
    the mapping, not any particular numbering scheme."""
    topology: dict[int, list[int]] = {}
    logical = 0
    for core in range(physical_cores):
        ids = []
        for _ in range(threads_per_core):
            ids.append(logical)
            logical += 1
        topology[core] = ids
    return topology


class FormatCpusetTests(unittest.TestCase):
    def test_empty(self):
        self.assertEqual(topo.format_cpuset([]), "")

    def test_single(self):
        self.assertEqual(topo.format_cpuset([4]), "4")

    def test_contiguous_range(self):
        self.assertEqual(topo.format_cpuset([0, 1, 2]), "0-2")

    def test_mixed_ranges_and_singletons(self):
        self.assertEqual(topo.format_cpuset([0, 1, 2, 4, 6, 7]), "0-2,4,6-7")


class DetectPhysicalTopologyTests(unittest.TestCase):
    def test_non_linux_returns_none(self):
        with patch.object(topo.platform, "system", return_value="Darwin"):
            self.assertIsNone(topo.detect_physical_topology())

    def test_reads_synthetic_sysfs_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            # 2 physical cores x 2 threads: cpu0/cpu2 share core_id 0, cpu1/cpu3 share core_id 1.
            core_id_by_cpu = {0: 0, 1: 1, 2: 0, 3: 1}
            for cpu, core_id in core_id_by_cpu.items():
                topo_dir = root / f"cpu{cpu}" / "topology"
                topo_dir.mkdir(parents=True)
                (topo_dir / "core_id").write_text(str(core_id))
            with patch.object(topo.platform, "system", return_value="Linux"):
                result = topo.detect_physical_topology(cpu_root=root)
        self.assertEqual(result, {0: [0, 2], 1: [1, 3]})

    def test_missing_core_id_file_returns_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "cpu0" / "topology").mkdir(parents=True)
            # No core_id file written.
            with patch.object(topo.platform, "system", return_value="Linux"):
                result = topo.detect_physical_topology(cpu_root=root)
        self.assertIsNone(result)

    def test_no_cpu_dirs_returns_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            with patch.object(topo.platform, "system", return_value="Linux"):
                result = topo.detect_physical_topology(cpu_root=pathlib.Path(tmp))
        self.assertIsNone(result)


class PlanPlacementTests(unittest.TestCase):
    def test_mode_off_is_always_unplaced(self):
        result = topo.plan_placement(make_topology(11), topo.SERVICE_CPU_REQUESTS, "off")
        self.assertEqual(result.mode, "off")
        self.assertEqual(result.assignments, {})
        self.assertIsNotNone(result.unplaced_reason)

    def test_none_topology_falls_back_unplaced(self):
        result = topo.plan_placement(None, topo.SERVICE_CPU_REQUESTS, "auto")
        self.assertEqual(result.mode, "auto")
        self.assertEqual(result.assignments, {})
        self.assertIn("no Linux physical-core topology", result.unplaced_reason)

    def test_too_few_physical_cores_falls_back_unplaced(self):
        result = topo.plan_placement(make_topology(4), topo.SERVICE_CPU_REQUESTS, "auto")
        self.assertEqual(result.assignments, {})
        self.assertIn("physical cores detected", result.unplaced_reason)
        self.assertEqual(result.topology_summary["physical_cores"], 4)

    def test_normal_placement_isolates_every_service_on_11_cores(self):
        topology = make_topology(11)
        result = topo.plan_placement(topology, topo.SERVICE_CPU_REQUESTS, "auto")
        self.assertEqual(result.mode, "auto")
        self.assertIsNone(result.unplaced_reason)
        self.assertEqual(set(result.assignments), set(topo.SERVICE_CPU_REQUESTS))
        # No two services may share a physical core (i.e. a logical CPU id).
        seen: set[int] = set()
        for cpuset in result.assignments.values():
            ids = _expand_cpuset(cpuset)
            self.assertTrue(ids, f"empty cpuset for a placed service: {cpuset!r}")
            self.assertFalse(seen & ids, f"overlapping cpuset assignment: {cpuset!r} vs already-seen {seen}")
            seen |= ids
        self.assertEqual(result.topology_summary["physical_cores"], 11)
        self.assertEqual(result.topology_summary["logical_cpus"], 22)

    def test_heavier_quota_gets_at_least_as_many_cores_as_lighter_quota(self):
        topology = make_topology(11)
        result = topo.plan_placement(topology, topo.SERVICE_CPU_REQUESTS, "auto")
        fsw_cores = len(_expand_cpuset(result.assignments["shire-fsw"])) // 2  # threads -> cores
        cryptolib_cores = len(_expand_cpuset(result.assignments["shire-cryptolib"])) // 2
        self.assertGreaterEqual(fsw_cores, cryptolib_cores)

    def test_manual_override_used_verbatim(self):
        result = topo.plan_placement(
            make_topology(11), topo.SERVICE_CPU_REQUESTS, "shire-fsw=4-7,shire-42=8-9")
        self.assertEqual(result.mode, "manual")
        self.assertEqual(result.assignments, {"shire-fsw": "4-7", "shire-42": "8-9"})
        self.assertIsNone(result.unplaced_reason)

    def test_manual_override_ignores_topology_entirely(self):
        # A manual override must work even when auto-detection would have
        # refused to place (e.g. a tiny or undetectable host) -- that's the
        # whole point of "preserve explicit CPU-set overrides."
        result = topo.plan_placement(None, topo.SERVICE_CPU_REQUESTS, "shire-fsw=0-1")
        self.assertEqual(result.assignments, {"shire-fsw": "0-1"})

    def test_manual_override_rejects_malformed_cpuset(self):
        result = topo.plan_placement(make_topology(11), topo.SERVICE_CPU_REQUESTS, "shire-fsw=not-a-cpuset")
        self.assertEqual(result.assignments, {})
        self.assertIsNotNone(result.unplaced_reason)

    def test_manual_override_rejects_missing_equals(self):
        result = topo.plan_placement(make_topology(11), topo.SERVICE_CPU_REQUESTS, "shire-fsw")
        self.assertEqual(result.assignments, {})
        self.assertIsNotNone(result.unplaced_reason)

    def test_manual_override_rejects_empty_string(self):
        result = topo.plan_placement(make_topology(11), topo.SERVICE_CPU_REQUESTS, "")
        self.assertEqual(result.assignments, {})
        self.assertIsNotNone(result.unplaced_reason)


def _expand_cpuset(cpuset: str) -> set[int]:
    ids: set[int] = set()
    for part in cpuset.split(","):
        if "-" in part:
            lo, hi = part.split("-")
            ids.update(range(int(lo), int(hi) + 1))
        else:
            ids.add(int(part))
    return ids


if __name__ == "__main__":
    unittest.main()

"""Check early runtime failure propagation and retained trial diagnostics."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
spec = importlib.util.spec_from_file_location('shire_perf', Path(__file__).with_name('shire-perf.py'))
perf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(perf)


class RuntimeTests(unittest.TestCase):
    def test_failed_participant_stops_waiting_and_retains_logs_and_cleanup(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            names = {key: key for key in ('server', 'director', 'fsw', 'gsw', '42')}
            stats, waiter = Mock(), Mock()
            stats.poll.return_value = waiter.poll.return_value = None
            archive = subprocess.CompletedProcess([], 0, stdout='{"run_id":"retained","volume":"archive"}')
            output = subprocess.CompletedProcess([], 0, stdout='healthy\nretained diagnostics\n')

            def run(command, **kwargs):
                if command[:2] == ['docker', 'inspect']:
                    return subprocess.CompletedProcess([], 0, stdout='healthy')
                return output

            health = Mock(side_effect=RuntimeError('Board runtime exited with status 2'))
            with patch.object(perf, 'active_value', side_effect={'mission': 'drm', 'spacecraft': 'sat-1'}.__getitem__), \
                    patch.object(perf, 'run', side_effect=run) as runner, \
                    patch.object(perf.subprocess, 'run', return_value=archive), \
                    patch.object(perf.subprocess, 'Popen', side_effect=[stats, waiter]), \
                    patch.object(perf.threading, 'Thread'), patch.object(perf.time, 'sleep'):
                with self.assertRaisesRegex(RuntimeError, 'Board runtime exited with status 2'):
                    perf.one_trial(directory / 'compose.yaml', directory, 'max', 30, 0, 1, False,
                                   names, directory / 'scenario.json', check_running=health)
            health.assert_called_once()
            waiter.terminate.assert_called_once()
            stats.terminate.assert_called_once()
            trial = directory / 'maxx-trial-1'
            self.assertIn('retained diagnostics', (trial / 'fsw.log').read_text())
            self.assertTrue(any(call.args[0][-5:] == ['down', '--remove-orphans', '--timeout', '2', '-v']
                                for call in runner.call_args_list))

"""Named scenarios share verification and reporting across native and board cFS."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, Mock, patch


TOOLS = Path(__file__).resolve().parent
with patch.object(sys, 'path', [str(TOOLS), *sys.path]):
    spec = importlib.util.spec_from_file_location('scenario_runner', TOOLS / 'shire-scenario.py')
    scenario = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(scenario)
    import shire_runner_lib as runner_lib


class ScenarioTests(unittest.TestCase):
    def args(self):
        return SimpleNamespace(scenario='checkout', port_offset=None, instance_id=None,
                               image_tag=None, simulith_speed=None)

    def execute(self, directory, *, runtime=None, config=None, completed=True, verification=None):
        def command_result(command, **kwargs):
            output = ''
            if command[:2] == ['docker', 'logs']:
                if 'shire-director-sat-1' in command:
                    output = 'SIMULITH_DIRECTOR_TERMINAL {"visual_count": 1}\n'
                elif 'shire-fsw-sat-1' in command:
                    output = 'SIMULITH_FSW_TERMINAL {}\n'
            return subprocess.CompletedProcess(command, 0, output)

        with patch.object(scenario, 'load_scenario_cfg', return_value=config or {'run_duration_s': 30}), \
                patch.object(scenario.subprocess, 'run', return_value=SimpleNamespace(
                    stdout='{"run_id":"retained","volume":"archive-volume"}')), \
                patch.object(scenario, 'run', side_effect=command_result) as run, \
                patch.object(scenario, 'run_scheduled_verify_stacks', return_value=verification) as stacks, \
                patch.object(scenario, 'wait_for_completion', return_value=completed) as wait, \
                patch.object(scenario, 'archive_coverage', return_value={'replay_ready': True}):
            result = {}
            code = scenario.execute_scenario(self.args(), 'drm', 'sat-1', result, directory, runtime=runtime)
        return code, result, run, stacks, wait

    def test_board_compose_verification_archive_and_health_callback_are_shared(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            record, health = Mock(), Mock()
            runtime = {'compose': directory / 'board-compose.yaml', 'target': {'hardware': True},
                       'record_archive': record, 'check_running': health}
            config = {'run_duration_s': 30, 'verify_stacks': [{'stack': 'example.ycs', 'at_s': 0}],
                      'director_command_scenario': 'cfg/perf-scenario.json'}
            code, result, run, stacks, wait = self.execute(directory, runtime=runtime, config=config,
                verification={'passed': True, 'failures': []})
            self.assertEqual(code, 0)
            self.assertTrue(result['pass'])
            self.assertTrue(result['archive']['replay_ready'])
            record.assert_called_once_with({'run_id': 'retained', 'volume': 'archive-volume'})
            self.assertIs(stacks.call_args.kwargs['check_running'], health)
            self.assertIs(wait.call_args.kwargs['check_running'], health)
            compose_calls = [call for call in run.call_args_list if call.args[0][:2] == ['docker', 'compose']]
            for call in compose_calls:
                self.assertEqual(call.args[0][3], str(runtime['compose']))
                self.assertNotIn('-v', call.args[0])
            up = next(call for call in compose_calls if 'up' in call.args[0])
            self.assertEqual(up.kwargs['env']['SHIRE_YAMCS_VOLUME'], 'archive-volume')
            self.assertEqual(up.kwargs['env']['SIMULITH_SCENARIO_ENABLED'], '1')
            self.assertTrue((directory / 'target.json').exists())
            self.assertTrue((directory / 'shire-fsw-sat-1.log').exists())

    def test_verification_failure_remains_a_failure_on_a_board(self):
        with tempfile.TemporaryDirectory() as temporary:
            code, result, *_ = self.execute(Path(temporary), runtime={'compose': 'board.yaml'},
                config={'run_duration_s': 30, 'verify_stacks': [{'stack': 'bad.ycs'}]},
                verification={'passed': False, 'failures': [
                    {'stack': 'bad.ycs', 'index': 2, 'type': 'verify', 'detail': 'Wrong telemetry'}]})
            self.assertEqual(code, 1)
            self.assertFalse(result['pass'])
            self.assertIn('Wrong telemetry', result['reason'])

    def test_native_compose_and_max_speed_scenarios_keep_working(self):
        with tempfile.TemporaryDirectory() as temporary:
            code, result, run, *_ = self.execute(Path(temporary), config={
                'run_duration_s': 30, 'simulith_speed': 'max'})
            self.assertEqual(code, 0)
            self.assertEqual(result['simulith_speed'], 0)
            up = next(call for call in run.call_args_list if 'up' in call.args[0])
            self.assertTrue(up.args[0][3].endswith('/build/drm/shire-compose.yaml'))
            self.assertEqual(up.kwargs['env']['SIMULITH_SCENARIO_ENABLED'], '0')
            self.assertIn('-v', run.call_args.args[0])

    def test_transport_error_still_retains_logs_and_stops_compose(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with patch.object(scenario, 'load_scenario_cfg', return_value={'run_duration_s': 30}), \
                    patch.object(scenario.subprocess, 'run', return_value=SimpleNamespace(
                        stdout='{"run_id":"kept","volume":"kept-volume"}')), \
                    patch.object(scenario, 'run', return_value=SimpleNamespace(returncode=0, stdout='logs')) as run, \
                    patch.object(scenario, 'wait_for_completion', side_effect=RuntimeError('Board exited')):
                with self.assertRaisesRegex(RuntimeError, 'Board exited'):
                    scenario.execute_scenario(self.args(), 'drm', 'sat-1', {}, directory,
                                              runtime={'compose': 'board.yaml'})
            self.assertIn('down', run.call_args.args[0])
            self.assertTrue(run.call_args.kwargs['check'])
            self.assertTrue((directory / 'shire-fsw-sat-1.log').exists())

    def test_main_selects_backend_build_and_runtime_without_native_fsw_build(self):
        for use_board, instance in ((False, None), (True, None), (False, 't1')):
            with self.subTest(board=use_board, instance=instance), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                active = root / 'active.yaml'
                active.write_text('mission: drm\nspacecraft: sat-1\nboard: rpi5\n')
                backend = Mock() if use_board else None
                if backend:
                    backend.build_command.return_value = ['python3', 'shire-board.py', 'build']
                    backend.session.return_value = MagicMock()
                    backend.session.return_value.__enter__.return_value = {'compose': 'board.yaml'}
                with patch.object(scenario, 'ACTIVE_PATH', active), \
                        patch.object(scenario, 'ACTIVE_LOCK_PATH', root / 'lock'), \
                        patch.object(scenario, 'git_head_sha', return_value='source-sha'), \
                        patch.object(scenario, 'run_streaming', return_value=(0, 'build log')) as build, \
                        patch.object(scenario, 'execute_scenario', return_value=0) as execute:
                    argv = ['--scenario', 'checkout', '--report-dir', str(root / 'report')]
                    if instance:
                        argv += ['--instance-id', instance, '--port-offset', '2']
                    code = scenario.main(argv, backend=backend)
                self.assertEqual(code, 0)
                self.assertEqual(build.call_args.args[0], backend.build_command() if backend else ['make', 'build'])
                self.assertEqual(execute.call_args.kwargs['runtime'], {'compose': 'board.yaml'} if backend else None)
                if backend:
                    backend.preflight.assert_called_once()

    def test_board_exit_terminates_completion_waiter(self):
        waiter = Mock()
        waiter.poll.return_value = None
        with patch.object(scenario.subprocess, 'Popen', return_value=waiter):
            with self.assertRaisesRegex(RuntimeError, 'lost target'):
                scenario.wait_for_completion('server', 30,
                    check_running=Mock(side_effect=RuntimeError('lost target')))
        waiter.terminate.assert_called_once()
        waiter.wait.assert_called_once_with(timeout=5)

    def test_board_exit_interrupts_a_scheduled_stack_child(self):
        with tempfile.TemporaryDirectory() as temporary:
            pid = Path(temporary) / 'pid'
            def health():
                if pid.exists():
                    raise RuntimeError('target failed during verification')
            with self.assertRaisesRegex(RuntimeError, 'target failed during verification'):
                runner_lib.run_streaming([sys.executable, '-c',
                    'import os, pathlib, time; pathlib.Path(os.environ["PROBE_PID"]).write_text(str(os.getpid())); time.sleep(60)'],
                    env={**os.environ, 'PROBE_PID': str(pid)}, check_running=health)
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pid.read_text()), 0)


if __name__ == '__main__':
    unittest.main()

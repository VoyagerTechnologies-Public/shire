"""Check port mapping and real UDP packet boundaries through the C tunnel."""
import importlib.util
import io
import json
import os
from pathlib import Path
import pty
import select
import socket
import struct
import subprocess
import sys
import tempfile
import termios
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import yaml


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


board = module('shire_board', 'shire-board.py')
bridge = module('shire_board_bridge', 'shire-board-bridge.py')


class BoardTests(unittest.TestCase):
    def test_saved_profile_drives_every_board_command_and_explicit_override_is_temporary(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'build').mkdir()
            active = root / 'build/active.yaml'
            active.write_text('mission: drm\nboard: qemu-zynq\n')
            saved = active.read_bytes()
            for command in ('prepare', 'connect', 'inspect', 'build', 'deploy', 'start', 'stop', 'test'):
                with self.subTest(command=command), patch.object(board, 'ROOT', root), \
                        patch.object(sys, 'argv', ['shire-board.py', command]), \
                        patch.object(board, 'qemu_command') as dispatch, \
                        patch.object(board, 'connect') as ssh:
                    board.main()
                    args = dispatch.call_args.args[0]
                    self.assertEqual(args.profile, 'qemu-zynq')
                    self.assertEqual(args.settings['name'], 'qemu-zynq')
                    ssh.assert_not_called()
            with patch.object(board, 'ROOT', root), \
                    patch.object(sys, 'argv', ['shire-board.py', 'prepare', '--profile', 'rpi5']), \
                    patch.object(board, 'prepare_platform') as prepare:
                board.main()
                self.assertEqual(prepare.call_args.args[0].profile, 'rpi5')
            self.assertEqual(active.read_bytes(), saved)

    def test_make_defers_saved_selection_and_forwards_explicit_board_for_all_commands(self):
        commands = [f'board-{name}' for name in
                    ('prepare', 'connect', 'inspect', 'build', 'deploy', 'start', 'stop', 'test')]
        environment = {key: value for key, value in os.environ.items()
                       if key not in ('BOARD', 'BOARD_ARGS', 'MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES')}
        for arguments, expected in (([], None), (['BOARD=qemu-zynq'], 'qemu-zynq'),
                                    (['BOARD=rpi5'], 'rpi5')):
            result = subprocess.run(['make', '--no-print-directory', '-n', *commands, *arguments],
                                    cwd=board.ROOT, env=environment, capture_output=True, text=True, check=True)
            lines = result.stdout.splitlines()
            self.assertEqual(len(lines), len(commands))
            for line in lines:
                if expected:
                    self.assertIn(f'--profile "{expected}"', line)
                else:
                    self.assertNotIn('--profile', line)

    def test_qemu_frontend_dispatch_never_requires_a_physical_ssh_profile(self):
        for command in ('prepare', 'connect', 'inspect', 'build', 'deploy', 'start', 'stop', 'test'):
            with self.subTest(command=command), \
                    patch.object(sys, 'argv', ['shire-board.py', command, '--profile', 'qemu-zynq']), \
                    patch.object(board, 'qemu_command') as dispatch, \
                    patch.object(board, 'connect') as physical_ssh:
                board.main()
                args = dispatch.call_args.args[0]
                self.assertEqual(args.settings['backend'], 'qemu')
                self.assertEqual(args.work, board.ROOT / 'build/qemu')
                self.assertEqual(args.command, command)
                physical_ssh.assert_not_called()

    def test_qemu_rejects_hardware_overrides_before_any_launch(self):
        for option in (['--target', 'user@host'], ['--ssh-control', '/tmp/socket'],
                       ['--password-auth'], ['--remote-root', '/tmp/root'],
                       ['--serial-console', '/dev/ttyPS0'], ['--no-serial-console']):
            with self.subTest(option=option), \
                    patch.object(sys, 'argv', ['shire-board.py', 'start', '--profile', 'qemu-zynq', *option]), \
                    patch.object(sys, 'stderr', io.StringIO()) as errors, \
                    patch.object(board.qemu, 'compose_command') as launch:
                with self.assertRaises(SystemExit) as result:
                    board.main()
                self.assertEqual(result.exception.code, 1)
                self.assertIn('only supported by SSH', errors.getvalue())
                launch.assert_not_called()

    def test_qemu_build_from_clean_configuration_builds_host_runtime_before_arm_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            sdk = work / 'output/host/share/buildroot'
            sdk.mkdir(parents=True)
            (sdk / 'toolchainfile.cmake').touch()
            args = SimpleNamespace(command='build', work=work, jobs=2)
            operations = []
            with patch.object(board.qemu, 'configure_work_directory'), \
                    patch.object(board.qemu, 'OUTPUT', work / 'output'), \
                    patch.object(board.subprocess, 'run', side_effect=lambda cmd, **kw: operations.append(cmd)), \
                    patch.object(board.qemu, 'build', side_effect=lambda *a, **kw: operations.append((a, kw))):
                board.qemu_command(args)
            self.assertEqual(operations[0], ['make', '-C', str(board.ROOT), 'cfg'])
            self.assertEqual([cmd[-1] for cmd in operations[1:3]], ['sim', 'gsw'])
            self.assertEqual(operations[3], ((2,), {'deploy_runtime': False}))

    def test_qemu_start_requires_idle_lab_and_passes_acceptance_parameters(self):
        profile = board.board_config.load_profile('qemu-zynq')
        args = SimpleNamespace(command='start', settings=profile, work=board.ROOT / 'build/qemu',
                               duration=12, trials=2, baseline=Path('baseline.json'))
        with patch.object(board.qemu, 'configure_work_directory'), \
                patch.object(board.qemu, 'active_config', return_value={}), \
                patch.object(board, 'require_idle', side_effect=RuntimeError('lab running')), \
                patch.object(board.qemu, 'compose_command') as launch:
            with self.assertRaisesRegex(RuntimeError, 'lab running'):
                board.qemu_command(args)
            launch.assert_not_called()
        args.command = 'test'
        with patch.object(board.qemu, 'configure_work_directory'), \
                patch.object(board.qemu, 'validate_bundle') as validate, \
                patch.object(board.qemu, 'test') as acceptance:
            board.qemu_command(args)
            validate.assert_called_once()
            self.assertEqual(acceptance.call_args.args, (12, 2, Path('baseline.json')))
            self.assertEqual(acceptance.call_args.kwargs['profile']['board_profile'], profile)
            self.assertNotIn('physical_target', acceptance.call_args.kwargs['profile'])

    def test_physical_platform_prepare_checks_local_inputs_without_connecting(self):
        with patch.object(sys, 'argv', ['shire-board.py', 'prepare', '--profile', 'rpi5']), \
                patch.object(board, 'prepare_platform') as prepare, \
                patch.object(board, 'connect') as ssh:
            board.main()
            prepare.assert_called_once()
            ssh.assert_not_called()

    def test_qemu_inspect_can_read_prepared_platform_before_any_mission_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            (work / 'platform.json').write_text('{"machine":"xilinx-zynq-a9"}')
            args = SimpleNamespace(command='inspect', work=work,
                                   settings=board.board_config.load_profile('qemu-zynq'))
            with patch.object(board, 'ROOT', work), \
                    patch.object(board.qemu, 'configure_work_directory'), \
                    patch.object(board.qemu, 'WORK', work), \
                    patch.object(board.qemu, 'active_config') as mission, \
                    patch.object(board.qemu, 'validate_bundle') as validate, \
                    patch('sys.stdout', io.StringIO()):
                board.qemu_command(args)
            mission.assert_not_called()
            validate.assert_not_called()
            report = json.loads((work / 'inspection.json').read_text())
            self.assertIsNone(report['bundle'])
            self.assertFalse(report['hardware_validated'])
            self.assertEqual(report['platform']['machine'], 'xilinx-zynq-a9')

    def test_console_mirror_preserves_output_tty_settings_and_child_failure_status(self):
        master, slave = pty.openpty()
        try:
            original = termios.tcgetattr(slave)
            child = ['sh', '-c', '[ -t 1 ] || exit 99; '
                     'printf "SHIRE_DEMO_START\\n"; '
                     'printf "SHIRE_DEMO_COMMAND_ACCEPTED\\n" >&2; '
                     'printf "SHIRE_DEMO_END\\n"; exit 7']
            result = subprocess.run(board.mirror_console_command(child, os.ttyname(slave)),
                stdin=subprocess.DEVNULL, capture_output=True, timeout=5)
            serial = bytearray()
            while select.select([master], [], [], .1)[0]:
                serial.extend(os.read(master, 65536))
            self.assertEqual(result.returncode, 7, result.stderr)
            self.assertEqual(termios.tcgetattr(slave), original)
            expected = [b'SHIRE_DEMO_START', b'SHIRE_DEMO_COMMAND_ACCEPTED', b'SHIRE_DEMO_END']
            for output in (result.stdout, bytes(serial)):
                lines = output.replace(b'\r', b'').splitlines()
                self.assertEqual([line for line in lines if line.startswith(b'SHIRE_DEMO_')], expected)
        finally:
            os.close(master)
            os.close(slave)

    def test_console_mirror_rejects_files_before_launching_or_truncating_them(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'ordinary-file'
            path.write_text('keep this')
            started = Path(temporary) / 'started'
            result = subprocess.run(board.mirror_console_command(['touch', str(started)], str(path)),
                stdin=subprocess.DEVNULL, capture_output=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b'not a character device', result.stderr)
            self.assertEqual(path.read_text(), 'keep this')
            self.assertFalse(started.exists())

    def test_serial_console_override_does_not_change_the_deployment_profile(self):
        profile = board.board_config.load_profile('zybo7020')
        profile['runtime']['serial_console'] = '/dev/ttyPS0'
        digest = board.board_config.profile_digest(profile)
        args = SimpleNamespace(settings=profile, serial_console=None)
        self.assertEqual(board.selected_serial_console(args), '/dev/ttyPS0')
        args.serial_console = '/dev/ttyAMA0'
        self.assertEqual(board.selected_serial_console(args), '/dev/ttyAMA0')
        args.serial_console = ''
        self.assertIsNone(board.selected_serial_console(args))
        self.assertEqual(board.board_config.profile_digest(profile), digest)

    def test_build_recovers_host_runtime_from_empty_configuration_before_packaging_arm(self):
        profile = board.board_config.load_profile('zybo7020')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk = root / 'sdk'
            (sdk / 'share/buildroot').mkdir(parents=True)
            (sdk / 'share/buildroot/toolchainfile.cmake').touch()
            bundle = root / 'bundle'
            profile['artifacts'].update(sdk=str(sdk), bundle=str(bundle))
            args = SimpleNamespace(settings=profile, work=root / 'board', jobs=2)
            config = {'mission': 'mission', 'spacecraft': 'vehicle', 'global': {'build': {'fsw': 'cfs'}}}
            configuration = root / 'build/build.yaml'
            operations = []

            def host_run(command, **kwargs):
                operations.append(command)
                if command[-1] == 'cfg':
                    configuration.parent.mkdir(parents=True)
                    configuration.write_text(yaml.safe_dump(config))
                else:
                    self.assertTrue(configuration.is_file())

            def cross_run(selected_profile, command, **kwargs):
                operations.append('cross-build')
                self.assertEqual([op[-1] for op in operations[:-1]], ['cfg', 'sim', 'gsw'])
                self.assertEqual(kwargs['environment']['SHIRE_FSW_SYSTEM'], 'armv7l-shire')
                directory = args.work / 'mission/vehicle/fsw'
                app = directory / 'exe/cpu1'
                app.mkdir(parents=True)
                header = bytearray(64)
                header[:6] = b'\x7fELF\x01\x01'
                struct.pack_into('<H', header, 18, 40)
                struct.pack_into('<I', header, 36, 0x400)
                (app / 'core-cpu1').write_bytes(header)
                library = directory / 'armv7l-shire/default_cpu1/simulith/libsimulith.so'
                library.parent.mkdir(parents=True)
                library.write_bytes(header)

            with patch.object(board, 'ROOT', root), \
                    patch.object(board.subprocess, 'run', side_effect=host_run), \
                    patch.object(board.qemu, 'active_config',
                        side_effect=lambda: yaml.safe_load(configuration.read_text())), \
                    patch.object(board, 'container_run', side_effect=cross_run):
                board.build(args)
            manifest = board.board_config.validate_bundle(profile, bundle, config)
            self.assertEqual(manifest['system'], 'armv7l-shire')
            self.assertIn('app/libsimulith.so', manifest['files'])

    def test_host_build_failure_stops_before_cross_build_and_bundle_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            profile = board.board_config.load_profile('zybo7020')
            (root / 'share/buildroot').mkdir(parents=True)
            (root / 'share/buildroot/toolchainfile.cmake').touch()
            profile['artifacts']['sdk'] = str(root)
            failure = subprocess.CalledProcessError(1, ['python3', 'shire-build.py', 'sim'])
            with patch.object(board.subprocess, 'run', side_effect=[None, failure]), \
                    patch.object(board.qemu, 'active_config', return_value={'mission': 'drm', 'spacecraft': 'sat-1'}), \
                    patch.object(board, 'container_run') as cross_build:
                with self.assertRaises(subprocess.CalledProcessError):
                    board.build(SimpleNamespace(settings=profile))
                cross_build.assert_not_called()

    def test_profile_ports_reach_both_compose_and_board_environment(self):
        profile = board.board_config.load_profile('rpi5')
        profile['network'].update(sync_pub=31000, sync_rep=31001, tunnel=31198,
                                  command=31234, telemetry=31235, device_ports=[32001])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / 'build/drm'
            directory.mkdir(parents=True)
            original = {'services': {name: {'image': 'local', 'environment': [], 'container_name': name}
                        for name in ('shire-server', 'shire-director', 'shire-fsw', 'shire-gsw')}}
            original['services']['shire-fsw']['sysctls'] = {'fs.mqueue.msg_max': 10000}
            source = directory / 'shire-compose.yaml'
            source.write_text(yaml.safe_dump(original))
            with patch.object(board, 'ROOT', root):
                path = board.render_compose({'mission': 'drm'}, root / 'state', profile)
            actual = yaml.safe_load(path.read_text())
            self.assertEqual(yaml.safe_load(source.read_text()), original)
            services = actual['services']
            self.assertEqual(services['shire-director']['ports'], ['127.0.0.1:32001:32001/tcp'])
            self.assertIn('SIMULITH_PUB_BIND=tcp://0.0.0.0:31000', services['shire-server']['environment'])
            self.assertIn('SIMULITH_REP_ADDR=tcp://shire-server:31001', services['shire-director']['environment'])
            self.assertIn('SHIRE_BOARD_COMMAND=31234', services['shire-fsw']['environment'])
            self.assertNotIn('sysctls', services['shire-fsw'])
            self.assertEqual(services['shire-fsw']['ports'], ['127.0.0.1:31198:31198/tcp'])
            environment = board.runtime_environment(profile)
            self.assertIn('SHIRE_BOARD_TELEMETRY=31235', environment)
            self.assertIn('SHIRE_BOARD_SYSTEM=aarch64-shire', environment)

    def test_start_deployment_checks_explain_missing_runtime_and_wrong_spacecraft(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            args = SimpleNamespace(target='petalinux@board')
            config = {'mission': 'drm', 'spacecraft': 'sat-1'}
            with patch.object(board, 'WORK', directory):
                with self.assertRaisesRegex(RuntimeError, 'board-deploy'):
                    board.deployment_for(args, config)
                manifest = {'target': args.target, 'root': '/tmp/shire-pil-test',
                            'profile_sha256': board.board_config.profile_digest(board.settings()),
                            'bundle': {'mission': 'drm', 'spacecraft': 'sat-2'}}
                (directory / 'deployment.json').write_text(json.dumps(manifest))
                with self.assertRaisesRegex(RuntimeError, 'active mission/spacecraft'):
                    board.deployment_for(args, config)
                manifest['bundle']['spacecraft'] = 'sat-1'
                (directory / 'deployment.json').write_text(json.dumps(manifest))
                with patch.object(board, 'remote', side_effect=subprocess.CalledProcessError(1, ['test'])):
                    with self.assertRaisesRegex(RuntimeError, 'after a reboot.*board-deploy'):
                        board.deployment_for(args, config)

    def test_stop_without_active_lab_needs_no_ssh_connection(self):
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(board, 'WORK', Path(temporary)), \
                patch.object(sys, 'argv', ['shire-board.py', 'stop', '--profile', 'zybo7020', '--work-dir', temporary]), \
                patch.object(board, 'connect') as connect:
            board.main()
            connect.assert_not_called()

    def test_stop_recovers_stale_controller_without_deleting_archive(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            state = directory / 'session'
            state.mkdir()
            (state / 'request.json').write_text('{"run_id":"test-run"}')
            (directory / 'active.json').write_text(json.dumps({'state': str(state),
                'compose': str(directory / 'compose.yaml'), 'archive': {'run_id': 'retained-archive'}}))
            with patch.object(board, 'WORK', directory), \
                    patch.object(board.time, 'monotonic', side_effect=[0, 46]), \
                    patch.object(board.subprocess, 'run') as run:
                board.stop(SimpleNamespace())
            self.assertTrue((state / 'stop').exists())
            self.assertTrue((state / 'test-run.stop').exists())
            self.assertFalse((directory / 'active.json').exists())
            command = run.call_args.args[0]
            self.assertIn('down', command)
            self.assertNotIn('-v', command)
            self.assertNotIn('--volumes', command)

    def test_start_refuses_to_replace_a_recorded_session(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / 'active.json').write_text('{}')
            with patch.object(board, 'WORK', directory), patch.object(board, 'board_session') as session:
                with self.assertRaisesRegex(RuntimeError, 'board-stop'):
                    board.start(SimpleNamespace())
                session.assert_not_called()

    def test_start_failure_stops_host_services_and_preserves_session_archive(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            state = directory / 'session'
            state.mkdir()
            session = {'config': {'mission': 'drm', 'spacecraft': 'sat-1'}, 'state': state,
                       'compose': directory / 'compose.yaml', 'deployment': {}, 'errors': []}
            with patch.object(board, 'WORK', directory), \
                    patch.object(board, 'board_session') as context, \
                    patch.object(board.subprocess, 'check_output',
                        return_value='{"run_id":"kept","volume":"retained-volume"}'), \
                    patch.object(board.subprocess, 'Popen', side_effect=OSError('cannot launch')), \
                    patch.object(board.subprocess, 'run') as run:
                context.return_value.__enter__.return_value = session
                with self.assertRaisesRegex(OSError, 'cannot launch'):
                    board.start(SimpleNamespace(target='petalinux@board'))
            metadata = json.loads((state / 'session.json').read_text())
            self.assertEqual(metadata['archive']['volume'], 'retained-volume')
            command = run.call_args.args[0]
            self.assertIn('down', command)
            self.assertNotIn('-v', command)

    def test_live_ssh_connection_does_not_prompt_again(self):
        args = SimpleNamespace(target='petalinux@board', ssh_control=Path('/unused/control'),
                               password_auth=False)
        with patch.object(board.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)) as run:
            board.connect(args)
        self.assertEqual(run.call_count, 1)
        self.assertEqual(run.call_args.args[0][-3:], ['-O', 'check', args.target])

    def test_expired_ssh_connection_reauthenticates_before_inspection(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = SimpleNamespace(target='petalinux@board', ssh_control=Path(temporary) / 'ssh/control',
                                   password_auth=True)
            missing = subprocess.CompletedProcess([], 255, stderr='Control socket does not exist')
            success = subprocess.CompletedProcess([], 0, stderr='')
            with patch.object(board.subprocess, 'run', side_effect=[missing, success, success]) as run:
                board.connect(args)
            command = run.call_args_list[1].args[0]
            self.assertIn('ControlMaster=auto', command)
            self.assertIn('ControlPersist=15m', command)
            self.assertIn('PubkeyAuthentication=no', command)
            self.assertNotIn('BatchMode=yes', command)
            self.assertEqual(command[-1], args.target)
            self.assertTrue(args.ssh_control.parent.is_dir())

    def test_profile_ssh_port_and_timeout_apply_to_authentication_and_remote_calls(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = board.board_config.load_profile('rpi5')
            profile['ssh'].update(port=2223, connect_timeout=47)
            args = SimpleNamespace(target='user@pi', settings=profile,
                                   ssh_control=Path(temporary) / 'control', password_auth=False)
            missing = subprocess.CompletedProcess([], 255, stderr='missing socket')
            success = subprocess.CompletedProcess([], 0)
            with patch.object(board.subprocess, 'run', side_effect=[missing, success, success]) as run:
                board.connect(args)
            for command in (board.ssh_base(args), run.call_args_list[1].args[0]):
                self.assertEqual(command[command.index('-p') + 1], '2223')
                self.assertIn('ConnectTimeout=47', command)

    def test_failed_connection_is_not_treated_as_a_ready_master(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = SimpleNamespace(target='petalinux@board', ssh_control=Path(temporary) / 'control',
                                   password_auth=False)
            missing = subprocess.CompletedProcess([], 255, stderr='missing socket')
            success = subprocess.CompletedProcess([], 0)
            with patch.object(board.subprocess, 'run', side_effect=[missing, success, missing]):
                with self.assertRaisesRegex(RuntimeError, 'not established: missing socket'):
                    board.connect(args)

    def test_captured_ssh_diagnostic_is_reported_for_text_and_binary_calls(self):
        args = SimpleNamespace(target='petalinux@board', ssh_control=Path('/unused/control'))
        for diagnostic in ('Permission denied (publickey,password).', b'Connection timed out'):
            failure = subprocess.CalledProcessError(255, ['ssh'], stderr=diagnostic)
            with patch.object(board.subprocess, 'run', side_effect=failure):
                with self.assertRaisesRegex(RuntimeError, 'Permission denied|Connection timed out'):
                    board.remote(args, ['id'], capture_output=True)

    def test_missing_or_invalid_ground_telemetry_fails_the_physical_gate(self):
        for link in ({}, {'status': 'OK', 'dataInCount': '0'},
                     {'status': 'FAILED', 'dataInCount': '3'},
                     {'status': 'OK', 'dataInCount': '3', 'extra': {'Invalid datagrams': '1'}}):
            response = io.BytesIO(json.dumps({'links': [{'name': 'debug-in', **link}]}).encode())
            with patch.object(board.urllib.request, 'urlopen', return_value=response):
                with self.assertRaises(RuntimeError):
                    board.ground_links()
        response = io.BytesIO(b'{"links":[{"name":"debug-in","status":"OK","dataInCount":"3"}]}')
        with patch.object(board.urllib.request, 'urlopen', return_value=response):
            self.assertIn('debug-in', board.ground_links()['physical_ground_links'])

    def test_all_device_bus_ports_and_duplicates(self):
        config = {'spacecraft_cfg': {'name': 'sat-1', 'adcs': {'handle': 2},
            'demo': {'handle': 5}, 'eps': {'i2c_bus_id': 0, 'i2c_device_addr': 30},
            'radio': {'spi_bus': 1, 'spi_cs': 0, 'gpio_power_pin': 10, 'gpio_interrupt_pin': 11},
            'duplicate': {'handle': 2}}}
        self.assertEqual(board.device_ports(config), [51002, 51005, 52030, 53008, 54010, 54011])
        with self.assertRaises(RuntimeError):
            board.device_ports({'spacecraft_cfg': {'bad': {'handle': 65535}}})

    def test_eof_during_tunnel_frame_is_an_error(self):
        reader, writer = socket.socketpair()
        with reader, writer:
            writer.sendall(b'partial')
            writer.shutdown(socket.SHUT_WR)
            with self.assertRaisesRegex(RuntimeError, 'tunnel closed'):
                bridge.receive(reader, 20)

    def test_real_c_tunnel_preserves_packets_and_fragmented_frames(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / 'tunnel'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror',
                str(Path(__file__).with_name('shire-board-udp.c')), '-o', str(binary)], check=True)
            with socket.socket() as listener, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                listener.bind(('127.0.0.1', 31198))
                listener.listen(1)
                listener.settimeout(3)
                udp.bind(('127.0.0.1', 31234))
                udp.settimeout(3)
                process = subprocess.Popen([str(binary), '31198', '31234', '31235'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                try:
                    stream, _ = listener.accept()
                    with stream:
                        stream.settimeout(3)
                        self.assertEqual(process.stdout.readline().strip(), b'SHIRE_BOARD_UDP_READY')
                        # A telemetry datagram must survive as exactly one frame.
                        udp.sendto(b'\x00telemetry\xff', ('127.0.0.1', 31235))
                        size, = struct.unpack('!I', bridge.receive(stream, 4))
                        self.assertEqual(bridge.receive(stream, size), b'\x00telemetry\xff')
                        # Partial TCP headers/bodies and consecutive frames must
                        # produce separate command datagrams, including empty ones.
                        packets = [b'\x00command\xff', b'next', b'']
                        frames = b''.join(struct.pack('!I', len(p)) + p for p in packets)
                        for value in frames:
                            stream.sendall(bytes([value]))
                        self.assertEqual([udp.recvfrom(65507)[0] for _ in packets], packets)
                        stream.shutdown(socket.SHUT_WR)
                        self.assertEqual(process.wait(timeout=3), 1)
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait()
                    process.stdout.close()
                    process.stderr.close()


if __name__ == '__main__':
    unittest.main()

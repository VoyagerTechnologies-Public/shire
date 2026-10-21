#!/usr/bin/env python3
"""Run a configured Linux processor PIL target with host SHIRE models."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import getpass
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import pty
import select
import shutil
import shlex
import signal
import subprocess
import tarfile
import threading
import time
import uuid
import urllib.request

import yaml

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'build/boards'
config_spec = importlib.util.spec_from_file_location('shire_board_config', ROOT / 'tools/shire_board_config.py')
board_config = importlib.util.module_from_spec(config_spec)
config_spec.loader.exec_module(board_config)
spec = importlib.util.spec_from_file_location('shire_qemu', ROOT / 'tools/shire-qemu.py')
qemu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qemu)


def settings(args=None):
    return getattr(args, 'settings', None) or board_config.load_profile('zybo7020')


def work_directory(args):
    return getattr(args, 'work', WORK)


def device_ports(config, profile=None):
    if profile and profile['network']['device_ports'] != 'auto':
        return sorted(set(profile['network']['device_ports']))
    ports = set()
    for value in config['spacecraft_cfg'].values():
        if not isinstance(value, dict):
            continue
        if 'handle' in value:
            ports.add(51000 + int(value['handle']))
        if 'i2c_bus_id' in value:
            ports.add(52000 + int(value['i2c_bus_id']) * 100 + int(value['i2c_device_addr']))
        if 'spi_bus' in value:
            ports.add(53000 + int(value['spi_bus']) * 8 + int(value['spi_cs']))
        for key in ('gpio_power_pin', 'gpio_interrupt_pin'):
            if key in value:
                ports.add(54000 + int(value[key]))
    if any(port > 65535 or port < 1 for port in ports):
        raise RuntimeError('Invalid simulated-device port')
    if profile and ports & {profile['network'][key] for key in ('sync_pub', 'sync_rep', 'tunnel')}:
        raise RuntimeError('Simulated-device ports overlap synchronization or tunnel ports')
    return sorted(ports)


def ssh_base(args):
    return ['ssh', '-S', str(args.ssh_control), '-o', 'BatchMode=yes',
            '-p', str(settings(args)['ssh']['port']),
            '-o', f"ConnectTimeout={settings(args)['ssh']['connect_timeout']}",
            '-o', f'UserKnownHostsFile={args.ssh_control.parent / "known_hosts"}', args.target]


def ssh_check(args):
    return subprocess.run([*ssh_base(args)[:-1], '-O', 'check', args.target],
                          capture_output=True, text=True, timeout=15)


def connect(args):
    """Reuse a live master or authenticate interactively to establish one."""
    if ssh_check(args).returncode == 0:
        return
    args.ssh_control.parent.mkdir(parents=True, exist_ok=True)
    print(f'[board] Establishing SSH connection to {args.target}', flush=True)
    command = ['ssh', '-S', str(args.ssh_control), '-o', 'ControlMaster=auto',
               '-p', str(settings(args)['ssh']['port']),
               '-o', 'ControlPersist=15m', '-o', f"ConnectTimeout={settings(args)['ssh']['connect_timeout']}",
               '-o', 'ServerAliveInterval=15', '-o', 'ServerAliveCountMax=3',
               '-o', f'UserKnownHostsFile={args.ssh_control.parent / "known_hosts"}',
               '-o', 'StrictHostKeyChecking=accept-new', '-N', '-f']
    if args.password_auth:
        command += ['-o', 'PubkeyAuthentication=no',
                    '-o', 'PreferredAuthentications=password,keyboard-interactive']
    subprocess.run([*command, args.target], check=True)
    result = ssh_check(args)
    if result.returncode:
        raise RuntimeError('SSH control connection was not established: ' + result.stderr.strip())


def remote(args, command, **kwargs):
    try:
        return subprocess.run([*ssh_base(args), shlex.join(command)], check=True, **kwargs)
    except subprocess.CalledProcessError as error:
        diagnostic = error.stderr or error.stdout
        if diagnostic:
            if isinstance(diagnostic, bytes):
                diagnostic = diagnostic.decode(errors='replace')
            raise RuntimeError(f'Remote command failed on {args.target} (exit {error.returncode}):\n'
                               + diagnostic.strip()) from error
        raise


def inspect(args, *, quiet=False):
    work = work_directory(args)
    work.mkdir(parents=True, exist_ok=True)
    machine = remote(args, ['uname', '-m'], capture_output=True, text=True).stdout.strip()
    page_size = int(remote(args, ['getconf', 'PAGE_SIZE'], capture_output=True, text=True).stdout.strip())
    expected = settings(args)['runtime']['kernel_machines']
    if machine not in expected:
        raise RuntimeError(f'Board kernel architecture mismatch: got {machine}, expected {expected}')
    result = remote(args, ['sh', '-c',
        'id; uname -a; cat /etc/os-release; getconf GNU_LIBC_VERSION; '
        'cat /proc/cpuinfo; cat /proc/sys/fs/mqueue/msg_max; '
        'cat /proc/sys/fs/mqueue/msgsize_max; date -u; ip -4 addr; ip route'],
        capture_output=True, text=True)
    path = work / 'inspection.txt'
    path.write_text(result.stdout)
    if not quiet:
        print(result.stdout)
    return {'target': args.target, 'kernel_machine': machine, 'page_size': page_size, 'inspection': result.stdout,
            'transport': 'SSH TCP forwards and framed UDP tunnel',
            'physical_buses_tested': False}


def prepare(args):
    config = qemu.active_config()
    profile = settings(args)
    work = work_directory(args)
    bundle = board_config.artifact_path(profile, 'bundle', config)
    manifest = board_config.validate_bundle(profile, bundle, config)
    rootfs = board_config.artifact_path(profile, 'rootfs', config)
    compiler = board_config.artifact_path(profile, 'compiler', config)
    for path in (rootfs, compiler):
        if not path.is_file():
            raise RuntimeError(f'Missing profile artifact: {path}. Build the matching SDK/userspace first.')
    identity = inspect(args, quiet=True)
    board_config.validate_bundle(profile, bundle, config, page_size=identity['page_size'])
    directory = work / 'package'
    directory.mkdir(parents=True, exist_ok=True)
    binary = directory / 'shire-board-udp'
    container_run(profile, [compiler, *profile['artifacts']['compiler_flags'],
                  '-Wall', '-Wextra', '-Werror', ROOT / 'tools/shire-board-udp.c', '-o', binary])
    board_config.check_elf(binary, profile['runtime']['architecture'])
    archive = directory / 'overlay.tar'
    with tarfile.open(archive, 'w') as output:
        output.add(bundle / 'app', arcname='opt/shire/seed')
        output.add(binary, arcname='usr/bin/shire-board-udp')
        output.add(ROOT / 'cfg/boards/run-inner.sh', arcname='run-shire-inner.sh')
    environment = runtime_environment(profile)
    (directory / 'shire-board-env').write_text(environment)
    # Deploy only to a new temporary directory. Nothing touches the boot image.
    remote(args, ['mkdir', '-m', '700', args.remote_root])
    for path in (rootfs, archive):
        with path.open('rb') as source:
            remote(args, ['tar', '-x', '-f', '-', '-C', args.remote_root], stdin=source)
    with (ROOT / 'cfg/boards/run-root.sh').open('rb') as source:
        remote(args, ['sh', '-c', 'cat > "$1/run-shire-root.sh"', 'sh', args.remote_root], stdin=source)
    remote(args, ['sh', '-c', 'printf "127.0.0.1 localhost %s\\n" "$2" > "$1/etc/hosts"',
                  'sh', args.remote_root, profile['network']['gsw_host']])
    remote(args, ['sh', '-c', 'cat > "$1/etc/shire-board-env"', 'sh', args.remote_root],
           input=environment, text=True)
    runtime = profile['runtime']
    remote(args, [args.remote_root + runtime['loader'], '--library-path',
                  f'{args.remote_root}/usr/lib:{args.remote_root}/lib:{args.remote_root}/opt/shire/seed',
                  '--list', f"{args.remote_root}/opt/shire/seed/{runtime['executable']}"])
    checksums = ''.join(f'{digest}  opt/shire/seed/{name.removeprefix("app/")}\n'
                        for name, digest in manifest['files'].items())
    remote(args, ['sh', '-c', 'cd "$1" && sha256sum -c -', 'sh', args.remote_root],
           input=checksums, capture_output=True, text=True)
    (work / 'deployment.json').write_text(json.dumps({'target': args.target, 'root': args.remote_root,
        'profile': profile, 'profile_sha256': board_config.profile_digest(profile),
        'bundle': manifest,
        'rootfs_tar_sha256': hashlib.sha256(rootfs.read_bytes()).hexdigest(),
        'runtime_sources': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
            for name in ('tools/shire-board-udp.c', 'cfg/boards/run-root.sh', 'cfg/boards/run-inner.sh')}},
        indent=2) + '\n')
    print(f'[board] isolated runtime: {args.remote_root}')


def fsw_build_directory(args, config, sdk):
    # A changed SDK must not inherit a CPU's cached compiler/sysroot or object
    # files. Keep prior builds for diagnosis rather than deleting their output.
    profile = settings(args)
    identity = json.dumps({'sdk': str(sdk.resolve()), 'system': profile['runtime']['cfs_system'],
                           'flags': profile['artifacts']['compiler_flags']}, sort_keys=True).encode()
    identity += (sdk / 'share/buildroot/toolchainfile.cmake').read_bytes()
    digest = hashlib.sha256(identity).hexdigest()[:16]
    return work_directory(args) / config['mission'] / config['spacecraft'] / f'fsw-{digest}'


def build(args):
    profile = settings(args)
    # Resolve configuration through the normal host build path, including its
    # base images, before reading SDK paths from build.yaml.
    subprocess.run(['make', '-C', str(ROOT), 'cfg'], check=True)
    config = qemu.active_config()
    sdk = board_config.artifact_path(profile, 'sdk', config)
    if not (sdk / 'share/buildroot/toolchainfile.cmake').is_file():
        selection = shlex.quote(getattr(args, 'profile', profile['name']))
        raise RuntimeError(f'Missing {profile["runtime"]["architecture"]} Buildroot SDK: {sdk}\n'
                           f'Run make board-prepare BOARD={selection} first. '
                           'The SDK and userspace must match the selected architecture.')
    # A physical processor replaces native FSW only. Its lab still needs 42,
    # Simulith, the configured device simulators, CryptoLib, and Yamcs.
    build_host_runtime()
    runtime = profile['runtime']
    directory = fsw_build_directory(args, config, sdk)
    container_run(profile, ['make', '-C', ROOT / config['global']['build'].get('fsw', 'cfs'),
        'build-fsw', f'BUILDDIR={directory}', f"MISSION={config['mission']}",
        f"SPACECRAFT={config['spacecraft']}", 'BUILDTYPE=release', f'-j{args.jobs}'],
        environment={'SHIRE_FSW_SYSTEM': runtime['cfs_system'], 'SHIRE_ARM_SDK': sdk})
    bundle = board_config.artifact_path(profile, 'bundle', config)
    bundle.mkdir(parents=True, exist_ok=True)
    if (bundle / 'app').exists():
        shutil.rmtree(bundle / 'app')
    shutil.copytree(directory / 'exe' / runtime['cpu'], bundle / 'app')
    shutil.copy2(directory / runtime['cfs_system'] / f"default_{runtime['cpu']}" / 'simulith/libsimulith.so',
                 bundle / 'app')
    for name in ('cf/data', 'cf/tmp', 'cf/fail', 'cf/poll_dir', 'd'):
        (bundle / 'app' / name).mkdir(parents=True, exist_ok=True)
    manifest = {'system': runtime['cfs_system'], 'cpu': runtime['cpu'],
        'mission': config['mission'], 'spacecraft': config['spacecraft'],
        'files': {str(path.relative_to(bundle)): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in (bundle / 'app').rglob('*') if path.is_file()}}
    (bundle / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    board_config.validate_bundle(profile, bundle, config)
    print(f'[board] Built {runtime["architecture"]} bundle: {bundle}')


def build_host_runtime():
    for target in ('sim', 'gsw'):
        subprocess.run(['python3', str(ROOT / 'tools/shire-build.py'), target], check=True)


def prepare_platform(args):
    """Physical boards supply a matching SDK/userspace; do not flash their OS."""
    subprocess.run(['make', '-C', str(ROOT), 'cfg'], check=True)
    config = qemu.active_config()
    profile = settings(args)
    if 'buildroot_config' in profile['artifacts']:
        spec = importlib.util.spec_from_file_location('shire_board_sdk', ROOT / 'tools/shire_board_sdk.py')
        sdk_builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(sdk_builder)
        sdk_builder.prepare(profile, config, args.jobs, backend=qemu, board_config=board_config)
    sdk = board_config.artifact_path(profile, 'sdk', config)
    paths = [sdk / 'share/buildroot/toolchainfile.cmake',
             *(board_config.artifact_path(profile, key, config) for key in ('compiler', 'rootfs'))]
    missing = [str(path) for path in paths if not path.is_file()]
    if missing:
        raise RuntimeError('Missing matching board SDK/userspace:\n  ' + '\n  '.join(missing)
            + '\nFor the shipped Zybo profile, prepare its shared ARM32 SDK with '
              'make board-prepare BOARD=qemu-zynq. Other profiles require their own matching SDK/rootfs.')
    print(f'[board] Configured SDK/userspace inputs present for {profile["name"]}; boot image unchanged.')


def qemu_command(args):
    """Adapt the shared public lifecycle to the Zynq emulator backend."""
    for key in ('target', 'ssh_control', 'password_auth', 'remote_root', 'serial_console'):
        if getattr(args, key, None) not in (None, False):
            raise RuntimeError(f'--{key.replace("_", "-")} is only supported by SSH board profiles')
    qemu.configure_work_directory(work_directory(args))
    if args.command == 'prepare':
        qemu.prepare(args.jobs)
    elif args.command == 'build':
        subprocess.run(['make', '-C', str(ROOT), 'cfg'], check=True)
        if not (qemu.OUTPUT / 'host/share/buildroot/toolchainfile.cmake').is_file():
            raise RuntimeError('Run make board-prepare BOARD=qemu-zynq first')
        build_host_runtime()
        qemu.build(args.jobs, deploy_runtime=False)
    elif args.command == 'deploy':
        require_idle(qemu.active_config())
        qemu.deploy()
    elif args.command == 'connect':
        qemu.shell()
    elif args.command == 'inspect':
        platform = json.loads((qemu.WORK / 'platform.json').read_text())
        bundle = (qemu.fsw_directory(qemu.active_config()).parent / 'bundle'
                  if (ROOT / 'build/build.yaml').is_file() else None)
        report = {'profile': settings(args), 'backend': 'qemu', 'hardware_validated': False,
                  'inspection_scope': 'local platform and bundle artifacts; use board-connect for a live guest shell',
                  'platform': platform, 'bundle': qemu.validate_bundle() if bundle and bundle.exists() else None}
        (qemu.WORK / 'inspection.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
    elif args.command == 'test':
        qemu.validate_bundle()
        qemu.test(args.duration, args.trials, args.baseline,
                  profile={'label': 'board', 'board_profile': settings(args), 'work': qemu.WORK})
    else:
        if args.command == 'start':
            require_idle(qemu.active_config())
        qemu.compose_command(args.command)


def container_run(profile, command, *, environment=None):
    image = profile['artifacts']['tools_image']
    sdk = board_config.artifact_path(profile, 'sdk', qemu.active_config()).resolve()
    mounts = [] if sdk.is_relative_to(ROOT) else ['-v', f'{sdk}:{sdk}:ro']
    variables = [item for key, value in (environment or {}).items() for item in ('-e', f'{key}={value}')]
    return subprocess.run(list(map(str, ['docker', 'run', '--rm', '--network', 'none',
        '--user', f'{os.getuid()}:{os.getgid()}', '-e', 'HOME=/tmp',
        '-v', f'{ROOT}:{ROOT}', *mounts, *variables, '-w', ROOT, image, *command])), check=True)


def runtime_environment(profile):
    runtime, network = profile['runtime'], profile['network']
    values = {'NAME': profile['name'], 'SYSTEM': runtime['cfs_system'],
              'EXECUTABLE': runtime['executable'], 'MQUEUE_MAX': runtime['mqueue_max'],
              'RTPRIO': runtime['rtprio']}
    values.update({key.upper(): network[key] for key in ('sync_pub', 'sync_rep', 'tunnel', 'command', 'telemetry')})
    return ''.join(f'export SHIRE_BOARD_{name}={shlex.quote(str(value))}\n' for name, value in values.items())


def render_compose(config, state, profile=None):
    profile = profile or settings()
    network = profile['network']
    source = ROOT / 'build' / config['mission'] / 'shire-compose.yaml'
    compose = yaml.safe_load(source.read_text())
    services = compose['services']
    for service in services.values():
        service['pull_policy'] = 'never'

    def environment(name, values):
        service = services[name]
        current = service.get('environment', [])
        if isinstance(current, dict):
            current = [f'{key}={value}' for key, value in current.items()]
        service['environment'] = [item for item in current if item.split('=', 1)[0] not in values]
        service['environment'] += [f'{key}={value}' for key, value in values.items()]

    environment('shire-server', {'SIMULITH_PUB_BIND': f"tcp://0.0.0.0:{network['sync_pub']}",
        'SIMULITH_REP_BIND': f"tcp://0.0.0.0:{network['sync_rep']}", 'SIMULITH_SYNC_TRANSPORT': 'zmq',
        'SIMULITH_START_PAUSED': '${SIMULITH_START_PAUSED:-0}'})
    environment('shire-director', {'SIMULITH_PUB_ADDR': f"tcp://shire-server:{network['sync_pub']}",
        'SIMULITH_REP_ADDR': f"tcp://shire-server:{network['sync_rep']}", 'SIMULITH_SYNC_TRANSPORT': 'zmq',
        'SIMULITH_DEVICE_TRANSPORT': 'tcp'})
    for name, ports in (('shire-server', [network['sync_pub'], network['sync_rep']]),
                        ('shire-director', device_ports(config, profile))):
        services[name]['ports'] = [f'127.0.0.1:{port}:{port}/tcp' for port in ports]
    fsw = services['shire-fsw']
    fsw['image'] = profile['artifacts']['tools_image']
    fsw['entrypoint'] = ['python3', '-u', '/run/shire-board-bridge.py']
    fsw['ports'] = [f"127.0.0.1:{network['tunnel']}:{network['tunnel']}/tcp"]
    fsw['volumes'] = [f'{ROOT}/tools/shire-board-bridge.py:/run/shire-board-bridge.py:ro',
                      f'{state}:/run/shire-board']
    fsw['cpus'] = '1.0'
    fsw['mem_limit'] = '128m'
    for key in ('sysctls', 'ulimits', 'cap_add', 'tmpfs'):
        fsw.pop(key, None)
    fsw['privileged'] = False
    environment('shire-fsw', {f'SHIRE_BOARD_{key.upper()}': network[key]
                             for key in ('tunnel', 'command', 'telemetry', 'gsw_host')})
    path = ROOT / 'build' / config['mission'] / f"shire-board-{profile['name']}-compose.yaml"
    path.write_text(yaml.safe_dump(compose, sort_keys=False))
    return path


def ground_links():
    with urllib.request.urlopen('http://localhost:8090/api/links/shire', timeout=5) as response:
        links = {item['name']: item for item in json.load(response)['links']}
    debug = links.get('debug-in', {})
    if (debug.get('status') != 'OK' or int(debug.get('dataInCount', 0)) <= 0 or
            int(debug.get('extra', {}).get('Invalid datagrams', 0))):
        raise RuntimeError('Physical-board telemetry did not reach the Yamcs debug-in link cleanly. '
                           'Allow enough simulated time for the startup RTS to enable debug output.')
    return {'physical_ground_links': links}


def selected_serial_console(args):
    value = getattr(args, 'serial_console', None)
    if value is None:
        value = settings(args)['runtime'].get('serial_console')
    board_config.validate_serial_console(value)
    return value or None


def mirror_console_command(command, console):
    """Keep a PTY and child exit status while duplicating output on the board."""
    return ['sh', '-c',
        'test -c "$1" || { printf "Serial console is not a character device: %s\\n" "$1" >&2; exit 1; }; '
        'command -v script >/dev/null || { echo "Serial mirroring requires util-linux script" >&2; exit 1; }; '
        'exec script --quiet --return --flush --echo never --force --log-out "$1" --command "$2"',
        'shire-board-console', console, shlex.join(command)]


def run_board(args, request, state, password, stop):
    run_id = request['run_id']
    profile = request['profile']
    if len(run_id) != 32 or any(char not in '0123456789abcdef' for char in run_id) or profile not in ('0', '1'):
        raise RuntimeError('Invalid board launch request')
    command = ['unshare', '--mount', '--ipc', '--pid',
               '--fork', '--kill-child=TERM', '/bin/sh', f'{args.remote_root}/run-shire-root.sh',
               args.remote_root, run_id, profile]
    console = selected_serial_console(args)
    if console:
        command = mirror_console_command(command, console)
    command = ['sudo', '-p', 'SHIRE_SUDO_PASSWORD:', *command]
    base = ssh_base(args)
    base.insert(1, '-tt')
    master, slave = pty.openpty()
    process = subprocess.Popen([*base, shlex.join(command)], stdin=slave, stdout=slave, stderr=slave)
    os.close(slave)
    pending = b''
    authenticated = False
    try:
        with (state / f'{run_id}.log').open('wb', buffering=0) as log:
            while process.poll() is None:
                if stop.is_set() or (state / f'{run_id}.stop').exists():
                    process.terminate()
                    break
                if not select.select([master], [], [], .1)[0]:
                    continue
                try:
                    block = os.read(master, 65536)
                except OSError:
                    break
                if not block:
                    break
                pending = (pending + block)[-4096:]
                if b'SHIRE_SUDO_PASSWORD:' in pending:
                    if authenticated:
                        raise RuntimeError('Board sudo authentication failed')
                    os.write(master, password.encode() + b'\n')
                    authenticated = True
                    pending = b''
                log.write(block)
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
            # Drain the PTY after exit so terminal metrics are retained.
            while select.select([master], [], [], .05)[0]:
                try:
                    block = os.read(master, 65536)
                except OSError:
                    break
                if not block:
                    break
                log.write(block)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        (state / f'{run_id}.exit').write_text(str(process.returncode))


def deployment_for(args, config):
    profile = settings(args)
    manifest = work_directory(args) / 'deployment.json'
    if not manifest.exists():
        raise RuntimeError('No Board deployment recorded. Run make board-deploy first.')
    deployment = json.loads(manifest.read_text())
    if deployment.get('profile_sha256') != board_config.profile_digest(profile):
        raise RuntimeError('Board profile changed or deployment predates profiles. Run make board-deploy BOARD=' +
                           shlex.quote(getattr(args, 'profile', profile['name'])))
    if deployment['target'] != args.target:
        raise RuntimeError('Deploy the bundle to the selected board first')
    if any(deployment['bundle'][key] != config[key] for key in ('mission', 'spacecraft')):
        raise RuntimeError('Board deployment does not match the active mission/spacecraft. Rebuild and redeploy it.')
    for name, digest in deployment.get('runtime_sources', {}).items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Board runtime source changed: {name}. Run board-deploy again.')
    args.remote_root = deployment['root']
    try:
        remote(args, ['test', '-f', f"{args.remote_root}/opt/shire/seed/{profile['runtime']['executable']}"])
    except subprocess.CalledProcessError as error:
        if error.returncode == 1:
            raise RuntimeError('Deployed Board runtime is missing, possibly after a reboot. Run make board-deploy.') from error
        raise
    return deployment


def require_idle(config):
    source = ROOT / 'build' / config['mission'] / 'shire-compose.yaml'
    services = yaml.safe_load(source.read_text())['services']
    names = {service['container_name'] for service in services.values() if 'container_name' in service}
    running = set(subprocess.check_output(['docker', 'ps', '--format', '{{.Names}}'], text=True).splitlines())
    if names & running:
        raise RuntimeError('Stop the existing SHIRE lab before starting a Board session')


@contextmanager
def board_session(args):
    profile = settings(args)
    work = work_directory(args)
    config = qemu.active_config()
    require_idle(config)
    deployment = deployment_for(args, config)
    board = inspect(args, quiet=True)
    console = selected_serial_console(args)
    board['serial_console'] = console
    state = work / 'sessions' / uuid.uuid4().hex
    state.mkdir(parents=True)
    compose = render_compose(config, state, profile)
    qemu.require_runtime_images(compose, fsw_repair='build/load the tools image configured in the board profile')
    password = getpass.getpass('Board sudo password (kept in memory for this session): ')
    ports = [profile['network'][key] for key in ('sync_pub', 'sync_rep', 'tunnel')]
    ports += device_ports(config, profile)
    forwards = []
    stop = threading.Event()
    errors = []

    def worker():
        seen = set()
        while not stop.wait(.05):
            request = state / 'request.json'
            if not request.exists():
                continue
            value = json.loads(request.read_text())
            if value['run_id'] in seen:
                continue
            seen.add(value['run_id'])
            try:
                run_board(args, value, state, password, stop)
            except Exception as error:
                errors.append(str(error))
                (state / f"{value['run_id']}.log").open('ab').write(str(error).encode())
                (state / f"{value['run_id']}.exit").write_text('1')

    thread = threading.Thread(target=worker, daemon=True)
    try:
        for port in ports:
            forwarding = f'127.0.0.1:{port}:127.0.0.1:{port}'
            subprocess.run([*ssh_base(args)[:-1], '-O', 'forward', '-o', 'ExitOnForwardFailure=yes',
                            '-R', forwarding, args.target], check=True)
            forwards.append(forwarding)
        thread.start()
        yield {'compose': compose, 'state': state, 'config': config, 'board': board,
               'deployment': deployment, 'errors': errors, 'profile': profile, 'work': work,
               'serial_console': console}
    finally:
        stop.set()
        if thread.is_alive():
            thread.join(timeout=15)
        for forwarding in forwards:
            subprocess.run([*ssh_base(args)[:-1], '-O', 'cancel', '-R', forwarding, args.target],
                           check=False)
        active = work_directory(args) / 'active.json'
        if ((state / 'host-stopped').exists() and active.exists()
                and json.loads(active.read_text()).get('state') == str(state)):
            active.unlink(missing_ok=True)


def check_board_running(session):
    if session['errors']:
        raise RuntimeError('; '.join(session['errors']))
    request = session['state'] / 'request.json'
    if not request.exists():
        return
    run_id = json.loads(request.read_text())['run_id']
    status = session['state'] / f'{run_id}.exit'
    code = status.read_text().strip() if status.exists() else ''
    if code and int(code) != 0:
        raise RuntimeError(f'Board runtime exited with status {code}; '
                           f'see {session["state"] / (run_id + ".log")}')


def test(args):
    with board_session(args) as session:
        deployment = session['deployment']
        profile = session['profile']
        qemu.test(args.duration, args.trials, args.baseline, profile={
            'compose': session['compose'], 'work': session['work'],
            'runtime': profile['name'] + '-' + profile['runtime']['architecture'],
            'image': profile['artifacts']['tools_image'], 'physical_target': True,
            'command_port': profile['network']['command'], 'board': session['board'], 'label': 'board',
            'post_run': ground_links,
            'check_running': lambda: check_board_running(session),
            'before_trial': lambda: (session['state'] / 'request.json').unlink(missing_ok=True),
            'platform': {'board': profile['name'], 'kernel': 'Existing board Linux kernel, see inspection',
                         'userspace': 'Public Buildroot root filesystem in an isolated chroot',
                         'deployment': deployment},
            'target': deployment['bundle'],
            'validation_scope': 'Physical Linux processor PIL with host simulated devices. No physical bus qualification.'})
        if session['errors']:
            raise RuntimeError('; '.join(session['errors']))


def save_logs(compose, state, environment=None):
    with (state / 'compose.log').open('w') as log:
        subprocess.run(['docker', 'compose', '-f', str(compose), 'logs', '--no-color'],
                       stdout=log, stderr=subprocess.STDOUT, env=environment, check=False)


def stop_host_services(compose, state, environment=None):
    save_logs(compose, state, environment)
    # A failed teardown must remain retryable; never discard its session record.
    subprocess.run(['docker', 'compose', '-f', str(compose), 'down', '--timeout', '2'],
                   env=environment, check=True)
    (state / 'host-stopped').touch()


def recover_host_services(args):
    """Recover a lost controller record without stopping another configuration."""
    if not (ROOT / 'build/build.yaml').exists():
        return
    compose = ROOT / 'build' / qemu.active_config()['mission'] / f"shire-board-{settings(args)['name']}-compose.yaml"
    if not compose.exists():
        return
    owned = set(subprocess.check_output(['docker', 'ps', '-aq', '--no-trunc', '--filter',
        f'label=com.docker.compose.project.config_files={compose}'], text=True).split())
    if not owned:
        return
    project = set(subprocess.check_output(
        ['docker', 'compose', '-f', str(compose), 'ps', '-a', '-q'], text=True).split())
    if project != owned:
        raise RuntimeError('Another SHIRE configuration shares this Compose project. '
                           'Stop that lab with its own stop command before retrying board-stop.')
    state = work_directory(args) / 'sessions' / ('cleanup-' + uuid.uuid4().hex)
    state.mkdir(parents=True)
    stop_host_services(compose, state)
    print(f'[board] Recovered host services for {settings(args)["name"]}. Logs: {state}. Archives retained.')
    return True


def start(args):
    active = work_directory(args) / 'active.json'
    if active.exists():
        raise RuntimeError('A Board session is already recorded. Run make board-stop before starting again.')
    with board_session(args) as session:
        config, state, compose = (session[key] for key in ('config', 'state', 'compose'))
        archive = json.loads(subprocess.check_output([
            'python3', str(ROOT / 'tools/shire-archives.py'), 'create',
            '--mission', config['mission'], '--spacecraft', config['spacecraft'],
            '--scenario', settings(args)['name'] + '-pil', '--image', f"shire-gsw-{config['mission']}:{config['spacecraft']}"],
            text=True))
        environment = {**os.environ, 'SHIRE_YAMCS_VOLUME': archive['volume'],
                       'SHIRE_RUN_ID': archive['run_id'], 'SIMULITH_SCENARIO_ENABLED': '0'}
        metadata = {'target': args.target, 'state': str(state), 'compose': str(compose),
                    'archive': archive, 'deployment': session['deployment'],
                    'serial_console': session.get('serial_console')}
        (state / 'session.json').write_text(json.dumps(metadata, indent=2) + '\n')
        with active.open('x') as output:
            json.dump(metadata, output, indent=2)
        print(f"[board] Yamcs: http://localhost:8090  archive: {archive['run_id']}", flush=True)
        if session.get('serial_console'):
            print(f"[board] Mirroring board output to {session['serial_console']} for the serial console.", flush=True)
        selection = shlex.quote(getattr(args, 'profile', settings(args)['name']))
        print(f'[board] Stop with Ctrl-C or make board-stop BOARD={selection} in another terminal.', flush=True)
        command = ['docker', 'compose', '-f', str(compose), 'up', '--abort-on-container-exit',
                   '--exit-code-from', 'shire-fsw']
        process = None
        stopped = False
        previous_handler = signal.getsignal(signal.SIGTERM)

        def terminate(_signum, _frame):
            raise KeyboardInterrupt

        signal.signal(signal.SIGTERM, terminate)
        try:
            process = subprocess.Popen(command, env=environment)
            while process.poll() is None:
                if (state / 'stop').exists():
                    stopped = True
                    break
                time.sleep(.1)
        except KeyboardInterrupt:
            stopped = True
        finally:
            signal.signal(signal.SIGTERM, previous_handler)
            if process is not None and process.poll() is None:
                process.send_signal(signal.SIGINT)
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            # Keep the per-run Yamcs archive. Only transient lab resources stop.
            stop_host_services(compose, state, environment)
            print(f'[board] Logs retained in {state}', flush=True)
        if not (stopped or (state / 'stop').exists()) and (process.returncode or session['errors']):
            raise RuntimeError('Board lab exited unexpectedly. See retained session logs.')


def stop(args):
    # Do not require working SSH to stop the host side after a link failure.
    active = work_directory(args) / 'active.json'
    if not active.exists():
        if not recover_host_services(args):
            print('[board] No interactive board lab is active.')
        return
    metadata = json.loads(active.read_text())
    state, compose = Path(metadata['state']), Path(metadata['compose'])
    (state / 'stop').touch()
    request = state / 'request.json'
    if request.exists():
        (state / f"{json.loads(request.read_text())['run_id']}.stop").touch()
    deadline = time.monotonic() + 45
    while active.exists() and time.monotonic() < deadline:
        time.sleep(.1)
    if active.exists():
        environment = {**os.environ, 'SHIRE_YAMCS_VOLUME': metadata['archive']['volume'],
                       'SHIRE_RUN_ID': metadata['archive']['run_id']}
        stop_host_services(compose, state, environment)
        active.unlink(missing_ok=True)
    print(f'[board] Lab stopped. Logs: {state}. Archive retained: {metadata["archive"]["run_id"]}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('prepare', 'connect', 'inspect', 'build', 'deploy', 'test', 'start', 'stop'))
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument('--profile', help='Select and save the active board profile name or YAML path')
    parser.add_argument('--target', help='Override profile SSH account and host')
    parser.add_argument('--ssh-control', type=Path)
    parser.add_argument('--work-dir', type=Path, help='Override per-board generated artifacts directory')
    parser.add_argument('--password-auth', action='store_true',
                        help='Use account password authentication when creating the SSH connection')
    parser.add_argument('--remote-root')
    parser.add_argument('--duration', type=float, default=30)
    parser.add_argument('--trials', type=int, default=3)
    parser.add_argument('--baseline', type=Path)
    console_options = parser.add_mutually_exclusive_group()
    console_options.add_argument('--serial-console', metavar='DEVICE',
                                 help='Mirror board output to a board serial device, e.g. /dev/ttyPS0')
    console_options.add_argument('--no-serial-console', dest='serial_console', action='store_const', const='',
                                 help='Disable the serial mirror configured in the board profile')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if not math.isfinite(args.duration) or args.duration < 1 or args.trials < 1:
        parser.error('--duration must be finite and at least 1 second; --trials must be positive')
    try:
        explicit_profile = args.profile is not None
        args.profile = board_config.select_profile(args.profile, active_path=ROOT / 'build/active.yaml')
        args.settings = board_config.load_profile(args.profile)
        if explicit_profile:
            board_config.save_active_profile(args.profile, active_path=ROOT / 'build/active.yaml')
        path = args.work_dir or Path(args.settings['work_dir'])
        args.work = path if path.is_absolute() else ROOT / path
        if args.settings.get('backend', 'ssh') == 'qemu':
            qemu_command(args)
            return
        if args.command == 'prepare':
            prepare_platform(args)
            return
        selected_serial_console(args)
        args.target = args.target or args.settings['ssh']['target']
        board_config.validate_target(args.target)
        args.settings['ssh']['target'] = args.target
        args.ssh_control = args.ssh_control or args.work / 'ssh-control'
        prefix = args.settings['runtime']['staging_prefix']
        args.remote_root = args.remote_root or prefix + uuid.uuid4().hex
        from pathlib import PurePosixPath
        if not args.remote_root.startswith(prefix) or '..' in PurePosixPath(args.remote_root).parts:
            raise RuntimeError('Runtime directory must be under the configured staging prefix')
        args.work.mkdir(parents=True, exist_ok=True)
        if 'YOUR_' in args.target and args.command not in ('stop', 'build'):
            raise RuntimeError('Set the SSH account/address in the selected board profile or pass --target')
        if args.command not in ('stop', 'build'):
            connect(args)
        if args.command == 'connect':
            print(f'[board] SSH connection ready: {args.target}')
        else:
            {'inspect': inspect, 'build': build, 'deploy': prepare, 'test': test,
             'start': start, 'stop': stop}[args.command](args)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired) as error:
        parser.exit(1, f'[board] {error}\n')


if __name__ == '__main__':
    main()

"""Validated processor PIL profiles and architecture checks."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct

import yaml

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROFILE = 'qemu-zynq'
ARCHITECTURES = {'armv7hf': (32, 40), 'aarch64': (64, 183), 'x86_64': (64, 62)}


def select_profile(selection=None, *, active_path=None):
    """Explicit invocation overrides the saved selection without rewriting it."""
    if selection is None:
        path = Path(active_path) if active_path is not None else ROOT / 'build/active.yaml'
        try:
            active = yaml.safe_load(path.read_text())
        except FileNotFoundError:
            active = {}
        except (OSError, yaml.YAMLError) as error:
            raise RuntimeError(f'Cannot read active board selection from {path}: {error}') from error
        if not isinstance(active, dict):
            raise RuntimeError(f'{path} must contain a YAML mapping')
        selection = active.get('board', DEFAULT_PROFILE)
    if not isinstance(selection, str) or not selection.strip() or '\n' in selection or '\x00' in selection:
        raise RuntimeError('Board selection must be a nonempty profile name or YAML path')
    return selection


def integer(value, minimum, maximum, name):
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise RuntimeError(f'{name} must be an integer in [{minimum}, {maximum}]')


def load_profile(selection):
    path = Path(selection)
    if path.suffix not in ('.yaml', '.yml'):
        if not re.fullmatch(r'[a-z0-9_-]+', str(selection)):
            raise RuntimeError('Board profile must be a name or YAML path')
        path = ROOT / 'cfg/boards' / f'{selection}.yaml'
    elif not path.is_absolute():
        path = ROOT / path
    try:
        data = yaml.safe_load(path.read_text())
    except (OSError, yaml.YAMLError) as error:
        raise RuntimeError(f'Cannot load board profile {path}: {error}') from error
    backend = data.get('backend', 'ssh') if isinstance(data, dict) else None
    if backend not in ('ssh', 'qemu'):
        raise RuntimeError('Board profile backend must be ssh or qemu')
    if backend == 'qemu':
        expected = {'schema_version', 'name', 'description', 'backend', 'work_dir', 'platform'}
        if set(data) != expected:
            raise RuntimeError('QEMU profile must contain exactly: ' + ', '.join(sorted(expected)))
        if type(data['schema_version']) is not int or data['schema_version'] != 1:
            raise RuntimeError('Unsupported board profile schema_version')
        if not isinstance(data['name'], str) or not re.fullmatch(r'[a-z0-9_-]+', data['name']):
            raise RuntimeError('Invalid board profile name')
        for key in ('description', 'work_dir'):
            if not isinstance(data[key], str) or not data[key] or '\n' in data[key] or '\x00' in data[key]:
                raise RuntimeError(f'{key} must be a nonempty single-line string')
        if data['platform'] != 'zynq-zc702':
            raise RuntimeError('Only the zynq-zc702 QEMU platform is supported')
        data['_path'] = str(path)
        return data
    fields = {
        'root': {'schema_version', 'name', 'description', 'work_dir', 'ssh', 'runtime', 'artifacts', 'network'},
        'ssh': {'target', 'port', 'connect_timeout'},
        'runtime': {'architecture', 'kernel_machines', 'cfs_system', 'cpu', 'executable', 'loader',
                    'staging_prefix', 'mqueue_max', 'rtprio'},
        'artifacts': {'bundle', 'rootfs', 'sdk', 'compiler', 'compiler_flags', 'tools_image'},
        'network': {'sync_pub', 'sync_rep', 'tunnel', 'command', 'telemetry', 'gsw_host', 'device_ports'},
    }
    for section, expected in fields.items():
        value = data if section == 'root' else data.get(section) if isinstance(data, dict) else None
        optional = {'serial_console'} if section == 'runtime' else {'backend'} if section == 'root' else set()
        if not isinstance(value, dict) or expected - set(value) or set(value) - expected - optional:
            raise RuntimeError(f'Board profile {section} must contain exactly: {", ".join(sorted(expected))}')
    if type(data['schema_version']) is not int or data['schema_version'] != 1:
        raise RuntimeError('Unsupported board profile schema_version')
    if not isinstance(data['name'], str) or not re.fullmatch(r'[a-z0-9_-]+', data['name']):
        raise RuntimeError('Invalid board profile name')
    for section, keys in (('root', ('description', 'work_dir')), ('ssh', ('target',)),
                          ('runtime', ('cfs_system', 'cpu', 'executable', 'loader', 'staging_prefix')),
                          ('artifacts', ('bundle', 'rootfs', 'sdk', 'compiler', 'tools_image')),
                          ('network', ('gsw_host',))):
        values = data if section == 'root' else data[section]
        for key in keys:
            if not isinstance(values[key], str) or not values[key] or '\n' in values[key] or '\x00' in values[key]:
                raise RuntimeError(f'{section}.{key} must be a nonempty single-line string')
    validate_target(data['ssh']['target'])
    integer(data['ssh']['port'], 1, 65535, 'ssh.port')
    integer(data['ssh']['connect_timeout'], 1, 300, 'ssh.connect_timeout')
    runtime = data['runtime']
    if not isinstance(runtime['architecture'], str) or runtime['architecture'] not in ARCHITECTURES:
        raise RuntimeError(f'Unsupported architecture: {runtime["architecture"]}')
    if not re.fullmatch(r'[A-Za-z0-9_-]+', runtime['cpu']):
        raise RuntimeError('Invalid runtime.cpu target name')
    if not re.fullmatch(r'[A-Za-z0-9_-]+', runtime['cfs_system']):
        raise RuntimeError('Invalid runtime.cfs_system target name')
    if not isinstance(runtime['kernel_machines'], list) or not runtime['kernel_machines'] or any(
            not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9_-]+', value)
            for value in runtime['kernel_machines']):
        raise RuntimeError('runtime.kernel_machines must list Linux uname -m values')
    executable = PurePosixPath(runtime['executable'])
    if executable.is_absolute() or '..' in executable.parts or not executable.parts:
        raise RuntimeError('runtime.executable must be relative to the app directory')
    for key in ('loader', 'staging_prefix'):
        value = PurePosixPath(runtime[key])
        if not value.is_absolute() or '..' in value.parts or str(value) == '/':
            raise RuntimeError(f'runtime.{key} must be an absolute path without traversal')
    integer(runtime['mqueue_max'], 1, 65536, 'runtime.mqueue_max')
    integer(runtime['rtprio'], 1, 99, 'runtime.rtprio')
    validate_serial_console(runtime.get('serial_console'))
    flags = data['artifacts']['compiler_flags']
    if not isinstance(flags, list) or any(not isinstance(flag, str) or '\x00' in flag for flag in flags):
        raise RuntimeError('artifacts.compiler_flags must be a list of compiler arguments')
    network = data['network']
    for name in ('sync_pub', 'sync_rep', 'tunnel', 'command', 'telemetry'):
        integer(network[name], 1, 65535, f'network.{name}')
    if len({network[key] for key in ('sync_pub', 'sync_rep', 'tunnel')}) != 3:
        raise RuntimeError('Synchronization and tunnel TCP ports must be distinct')
    if network['command'] == network['telemetry']:
        raise RuntimeError('Command and telemetry UDP ports must be distinct')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+', network['gsw_host']):
        raise RuntimeError('Invalid network.gsw_host')
    devices = network['device_ports']
    if devices != 'auto':
        if not isinstance(devices, list):
            raise RuntimeError('network.device_ports must be auto or a port list')
        for port in devices:
            integer(port, 1, 65535, 'network.device_ports')
        if set(devices) & {network[key] for key in ('sync_pub', 'sync_rep', 'tunnel')}:
            raise RuntimeError('Device ports overlap synchronization or tunnel ports')
    data['_path'] = str(path)
    return data


def validate_target(target):
    if not isinstance(target, str) or not re.fullmatch(r'(?:[A-Za-z0-9_.-]+@)?[A-Za-z0-9_.:-]+', target) or target.startswith('-'):
        raise RuntimeError('Invalid SSH target')


def validate_serial_console(value):
    if value in (None, ''):
        return
    if (not isinstance(value, str) or not re.fullmatch(r'/dev/[A-Za-z0-9_./-]+', value)
            or '..' in PurePosixPath(value).parts):
        raise RuntimeError('Serial console must be a board device path under /dev without traversal')


def profile_digest(profile):
    return hashlib.sha256(json.dumps({k: v for k, v in profile.items() if not k.startswith('_')},
                                    sort_keys=True).encode()).hexdigest()


def artifact_path(profile, key, config):
    try:
        value = profile['artifacts'][key].format(mission=config['mission'], spacecraft=config['spacecraft'])
    except (KeyError, ValueError) as error:
        raise RuntimeError(f'Invalid artifacts.{key} path template') from error
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


def check_elf(path, architecture, *, required=True):
    with Path(path).open('rb') as source:
        header = source.read(52)
    if not header.startswith(b'\x7fELF'):
        if required:
            raise RuntimeError(f'Not an ELF executable: {path}')
        return
    if len(header) < 52 or header[4] not in (1, 2) or header[5] != 1:
        raise RuntimeError(f'Invalid or unsupported ELF header: {path}')
    bits, machine = ARCHITECTURES[architecture]
    actual = (32 if header[4] == 1 else 64, struct.unpack_from('<H', header, 18)[0])
    if actual != (bits, machine):
        raise RuntimeError(f'ELF architecture mismatch for {path}: expected {architecture}, got {actual}')
    if architecture == 'armv7hf' and not (struct.unpack_from('<I', header, 36)[0] & 0x400):
        raise RuntimeError(f'ARM hard-float ABI required: {path}')


def validate_bundle(profile, bundle, config):
    manifest = json.loads((bundle / 'manifest.json').read_text())
    if manifest.get('system') != profile['runtime']['cfs_system']:
        raise RuntimeError('Bundle cFS system does not match the board profile')
    if any(manifest.get(key) != config[key] for key in ('mission', 'spacecraft')):
        raise RuntimeError('Bundle does not match the active mission/spacecraft')
    executable = 'app/' + profile['runtime']['executable']
    files = manifest.get('files')
    if not isinstance(files, dict) or executable not in files:
        raise RuntimeError('Bundle manifest must include the configured executable and all app files')
    actual = {str(path.relative_to(bundle)) for path in (bundle / 'app').rglob('*') if path.is_file()}
    if set(files) != actual:
        raise RuntimeError('Bundle contains missing or unmanifested files. Rebuild and repackage it.')
    for name, digest in files.items():
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or not name.startswith('app/'):
            raise RuntimeError(f'Invalid bundle path: {name}')
        source = bundle / name
        if not source.resolve().is_relative_to(bundle.resolve()):
            raise RuntimeError(f'Bundle file escapes the bundle directory: {name}')
        if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Bundle hash mismatch: {name}. Rebuild and repackage it.')
        check_elf(source, profile['runtime']['architecture'], required=name == executable)
    return manifest

"""Build a matching public SDK/userspace for an existing Linux PIL board."""
import hashlib
import json
import shutil


def prepare(profile, config, jobs, *, backend, board_config):
    root = board_config.ROOT
    sdk = board_config.artifact_path(profile, 'sdk', config).resolve()
    rootfs = board_config.artifact_path(profile, 'rootfs', config).resolve()
    # Buildroot compiler names may symlink to a wrapper that dispatches using
    # argv[0]. Preserve that name when invoking it, including on cached builds.
    compiler = board_config.artifact_path(profile, 'compiler', config)
    definition = board_config.artifact_path(profile, 'buildroot_config', config).resolve()
    output = sdk.parent / (sdk.name + '-buildroot')
    for path in (sdk, rootfs, compiler, definition, output):
        if not path.resolve().is_relative_to(root):
            raise RuntimeError('Buildroot preparation paths must be inside the checkout mounted into build containers. '
                               'External prebuilt SDKs remain supported without artifacts.buildroot_config.')
    contents = definition.read_text()
    identity = hashlib.sha256((backend.VERSION + '\n' + str(sdk) + '\n' + contents).encode()).hexdigest()
    stamp = output / 'shire-input.sha256'
    if not (output / '.config').exists() and sdk.exists() and any(sdk.iterdir()):
        raise RuntimeError(f'SDK directory already contains files: {sdk}. Select a fresh artifacts.sdk path, '
                           'or omit artifacts.buildroot_config to use a supplied SDK without modifying it.')
    if (output / '.config').exists() and (not stamp.exists() or stamp.read_text().strip() != identity):
        raise RuntimeError(f'Buildroot inputs changed for {output}. Select a fresh artifacts.sdk path '
                           'to avoid mixing incompatible SDK builds.')
    archive = backend.prepare_source()
    image = profile['artifacts']['tools_image']
    backend.run(['docker', 'build', '-t', image, '-f', root / 'tools/Dockerfile.qemu', root / 'tools'])
    output.mkdir(parents=True, exist_ok=True)
    generated = output / 'shire_defconfig'
    # Build wrappers and the CMake sysroot at their final location instead of
    # copying an SDK whose embedded paths have not been relocated.
    generated.write_text(contents + '\nBR2_HOST_DIR=' + json.dumps(str(sdk)) + '\n')

    def make(*targets, network=False):
        backend.container(['make', '-C', backend.SOURCE, f'O={output}', f'-j{jobs}', *targets],
                          network=network, image=image)

    make(f'BR2_DEFCONFIG={generated}', 'defconfig')
    stamp.write_text(identity + '\n')
    resolved = (output / '.config').read_text()
    arch_setting = {'aarch64': 'BR2_aarch64=y', 'armv7hf': 'BR2_ARM_EABIHF=y', 'x86_64': 'BR2_x86_64=y'}
    if arch_setting[profile['runtime']['architecture']] not in resolved.splitlines():
        raise RuntimeError('Buildroot configuration does not match runtime.architecture')
    if 'BR2_LINUX_KERNEL=y' in resolved.splitlines() or 'BR2_TARGET_UBOOT=y' in resolved.splitlines():
        raise RuntimeError('Board SDK preparation is userspace-only; disable kernel and bootloader image builds')
    make(network=True)
    probe = output / 'shire-abi-check'
    source = probe.with_suffix('.c')
    source.write_text('int main(void) { return 0; }\n')
    backend.container([compiler, *profile['artifacts']['compiler_flags'], source, '-o', probe], image=image)
    page_size = next((size for name, size in (('4K', 4096), ('16K', 16384), ('64K', 65536))
                      if f'BR2_ARM64_PAGE_SIZE_{name}=y' in resolved.splitlines()), None)
    board_config.check_elf(probe, profile['runtime']['architecture'], page_size=page_size)
    board_config.check_elf(output / 'target' / profile['runtime']['loader'].lstrip('/'),
                           profile['runtime']['architecture'], page_size=page_size)
    make('legal-info', network=True)
    supplement = output / 'legal-info/shire-supplement'
    supplement.mkdir(exist_ok=True)
    shutil.copy2(archive, supplement)
    toolchain = sdk / 'opt/ext-toolchain'
    notes = [toolchain / name for name in ('README.txt', 'summary.csv', 'buildroot.config', 'license.txt')]
    notes.extend(toolchain.glob('*manifest*'))
    for source in notes:
        if source.is_file():
            shutil.copy2(source, supplement / f'toolchain-{source.name}')
    rootfs.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(output / 'images/rootfs.tar', rootfs)
    receipt = {'buildroot': backend.VERSION, 'source_sha256': backend.SOURCE_SHA256,
               'architecture': profile['runtime']['architecture'], 'page_size': page_size, 'hardware_validated': False,
               'scope': 'Cross SDK and matching userspace; existing board kernel and boot image retained',
               'configuration_sha256': hashlib.sha256((output / '.config').read_bytes()).hexdigest(),
               'input_sha256': identity, 'sdk': str(sdk), 'rootfs': str(rootfs),
               'rootfs_sha256': hashlib.sha256(rootfs.read_bytes()).hexdigest(),
               'legal_info': str(output / 'legal-info')}
    (output / 'preparation.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'[board] Prepared {receipt["architecture"]} SDK: {sdk}', flush=True)
    print(f'[board] Matching userspace: {rootfs}; provenance: {output / "preparation.json"}', flush=True)

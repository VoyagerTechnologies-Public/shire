#!/usr/bin/env python3
"""Build and run an open-source ARM32 Zynq guest for SHIRE PIL."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tarfile
import urllib.request

import yaml

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build/qemu"
VERSION = "2025.02.6"
SOURCE_SHA256 = "a49881ff3042f458e8dbff8173bf0e5c53a84e426ec58f7343eda5e6fec76d89"
TOOLS_IMAGE = "shire-qemu-tools:0.0.0"
SOURCE = WORK / f"buildroot-{VERSION}"
OUTPUT = WORK / "output"

# The emulated Zynq platform has one supported cFS ABI. Reuse the same bundle
# checks as physical targets instead of accepting a native executable here.
BUNDLE_PROFILE = {"runtime": {"cfs_system": "armv7l-shire", "executable": "core-cpu1",
                               "architecture": "armv7hf"}}


def configure_work_directory(path):
    global WORK, SOURCE, OUTPUT
    path = Path(path).resolve()
    if not path.is_relative_to(ROOT):
        raise RuntimeError("QEMU work directory must be inside the checkout mounted into its build containers")
    WORK = path
    SOURCE = WORK / f"buildroot-{VERSION}"
    OUTPUT = WORK / "output"


def validate_bundle():
    spec = importlib.util.spec_from_file_location("shire_board_config", ROOT / "tools/shire_board_config.py")
    config = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(config)
    active = active_config()
    return config.validate_bundle(BUNDLE_PROFILE, fsw_directory(active).parent / "bundle", active)


def run(command, **kwargs):
    print("[qemu]", " ".join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def container(command, *, network=False, cwd=ROOT, environment=None):
    variables = [item for key, value in (environment or {}).items()
                 for item in ("-e", f"{key}={value}")]
    return run(["docker", "run", "--rm", "--network", "bridge" if network else "none",
                "--user", f"{os.getuid()}:{os.getgid()}",
                "-e", "HOME=/tmp", "-v", f"{ROOT}:{ROOT}", "-w", cwd,
                *variables, TOOLS_IMAGE, *command])


def active_config():
    return yaml.safe_load((ROOT / "build/build.yaml").read_text())


def fsw_directory(config):
    return WORK / config["mission"] / config["spacecraft"] / "fsw"


def build(jobs, *, deploy_runtime=True):
    if not (OUTPUT / "host/share/buildroot/toolchainfile.cmake").exists():
        raise RuntimeError("Run make board-prepare BOARD=qemu-zynq first")
    container(["python3", ROOT / "tools/shire-orchestrator.py"])
    config = active_config()
    directory = fsw_directory(config)
    container(["make", "-C", ROOT / config["global"]["build"].get("fsw", "cfs"),
               "build-fsw", f"BUILDDIR={directory}", f"MISSION={config['mission']}",
               f"SPACECRAFT={config['spacecraft']}", "BUILDTYPE=release", f"-j{jobs}"],
              environment={"SHIRE_FSW_SYSTEM": "armv7l-shire", "SHIRE_ARM_SDK": OUTPUT / "host"})
    bundle = directory.parent / "bundle"
    bundle.mkdir(exist_ok=True)
    if (bundle / "app").exists():
        shutil.rmtree(bundle / "app")
    shutil.copytree(directory / "exe/cpu1", bundle / "app")
    shutil.copy2(directory / "armv7l-shire/default_cpu1/simulith/libsimulith.so", bundle / "app")
    for name in ("cf/data", "cf/tmp", "cf/fail", "cf/poll_dir", "d"):
        (bundle / "app" / name).mkdir(parents=True, exist_ok=True)
    manifest = {"system": "armv7l-shire", "mission": config["mission"],
                "spacecraft": config["spacecraft"],
                "files": {str(path.relative_to(bundle)): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in (bundle / "app").rglob("*") if path.is_file()}}
    (bundle / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    validate_bundle()
    if deploy_runtime:
        deploy()


def deploy():
    """Package an already built ARM bundle; do not compile native or ARM FSW."""
    validate_bundle()
    config = active_config()
    directory = fsw_directory(config)
    runtime = directory.parent / "runtime"
    runtime.mkdir(exist_ok=True)
    for name in ("uImage", "zynq-zc702.dtb"):
        shutil.copy2(OUTPUT / "images" / name, runtime / name)
    # Add the exact target bundle to the guest without rebuilding its kernel.
    package()
    shutil.copy2(ROOT / "tools/shire-qemu-entrypoint.py", runtime)
    image = f"shire-fsw-{config['mission']}-qemu:{config['spacecraft']}"
    run(["docker", "build", "--network=none", "-t", image,
         "-f", ROOT / "tools/Dockerfile.qemu-runtime", runtime])
    render_compose(config, image)


def package():
    # Reuse the unprivileged newc writer used by the runtime entry point.
    import gzip
    spec = importlib.util.spec_from_file_location("qemu_entry", ROOT / "tools/shire-qemu-entrypoint.py")
    entry = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(entry)
    directory = fsw_directory(active_config()).parent
    overlay = directory / "overlay"
    (overlay / "opt/shire").mkdir(parents=True, exist_ok=True)
    if (overlay / "opt/shire/app").exists():
        shutil.rmtree(overlay / "opt/shire/app")
    shutil.copytree(directory / "bundle/app", overlay / "opt/shire/app")
    key = WORK / "id_ed25519"
    if not key.exists():
        run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", key])
    ssh = overlay / "root/.ssh"
    ssh.mkdir(parents=True, exist_ok=True)
    ssh.chmod(0o700)
    shutil.copy2(key.with_suffix(".pub"), ssh / "authorized_keys")
    (ssh / "authorized_keys").chmod(0o600)
    with (directory / "runtime/rootfs.cpio.gz").open("wb") as target:
        target.write((OUTPUT / "images/rootfs.cpio.gz").read_bytes())
        target.write(gzip.compress(entry.overlay_archive(overlay), mtime=0))


def render_compose(config, image):
    base = ROOT / "build" / config["mission"]
    compose = yaml.safe_load((base / "shire-compose.yaml").read_text())
    services = compose["services"]
    # These mission-specific runtime images are built locally, not published
    # under the unqualified image names in the generated configuration.
    for service in services.values():
        service["pull_policy"] = "never"
    services["shire-server"]["environment"] += [
        "SIMULITH_PUB_BIND=tcp://0.0.0.0:50000", "SIMULITH_REP_BIND=tcp://0.0.0.0:50001",
        "SIMULITH_SYNC_TRANSPORT=zmq", "SIMULITH_START_PAUSED=${SIMULITH_START_PAUSED:-0}"]
    services["shire-director"]["environment"] += [
        "SIMULITH_PUB_ADDR=tcp://shire-server:50000", "SIMULITH_REP_ADDR=tcp://shire-server:50001",
        "SIMULITH_SYNC_TRANSPORT=zmq", "SIMULITH_DEVICE_TRANSPORT=tcp"]
    fsw = services["shire-fsw"]
    fsw["image"] = image
    fsw["environment"] = [item for item in fsw["environment"]
                          if not item.startswith("SHIRE_SYNCHRONIZE_GROUND_OUTPUT=")]
    fsw["environment"] += [
        "SIMULITH_PUB_ADDR=tcp://shire-server:50000", "SIMULITH_REP_ADDR=tcp://shire-server:50001",
        "SIMULITH_SYNC_TRANSPORT=zmq", "SIMULITH_DEVICE_TRANSPORT=tcp",
        "SIMULITH_DEVICE_HOST=shire-director", "SHIRE_SYNCHRONIZE_GROUND_OUTPUT=1"]
    fsw["ports"] = ["127.0.0.1:${SHIRE_QEMU_SSH_PORT:-2222}:2222"]
    for key in ("sysctls", "ulimits", "cap_add", "volumes", "tmpfs"):
        fsw.pop(key, None)
    path = base / "shire-qemu-compose.yaml"
    path.write_text(yaml.safe_dump(compose, sort_keys=False))
    print(f"[qemu] Compose configuration: {path}")


def require_runtime_images(path, *, fsw_repair=None):
    """Fail before allocating an archive or starting a partial stack."""
    compose = json.loads(subprocess.check_output(
        ["docker", "compose", "-f", str(path), "config", "--format", "json"], text=True))
    build_commands = {"shire-42": "make 42", "shire-server": "make sim",
                      "shire-director": "make sim", "shire-gsw": "make gsw",
                      "shire-cryptolib": "make gsw",
                      "shire-fsw": "make board-build BOARD=qemu-zynq then make board-deploy BOARD=qemu-zynq"}
    if fsw_repair:
        build_commands['shire-fsw'] = fsw_repair
    missing = []
    for name, service in compose["services"].items():
        image = service["image"]
        result = subprocess.run(["docker", "image", "inspect", image],
                                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if result.returncode:
            # Preserve daemon/permission failures instead of misreporting them
            # as a missing build artifact.
            if "No such image:" not in result.stderr and "No such object:" not in result.stderr:
                raise RuntimeError(result.stderr.strip())
            repair = build_commands.get(name, "build or load this configured image")
            missing.append(f"  {image}: {repair}")
    if missing:
        raise RuntimeError("Missing local runtime images:\n" + "\n".join(missing) +
                           "\nBuild the missing images, then retry. No run archive was allocated.")


def compose_command(command):
    config = active_config()
    path = ROOT / "build" / config["mission"] / "shire-qemu-compose.yaml"
    if not path.exists():
        raise RuntimeError("Run make board-deploy BOARD=qemu-zynq first")
    if command == "start":
        require_runtime_images(path)
        run(["python3", ROOT / "tools/shire-archives.py", "start", "--mission", config["mission"],
             "--spacecraft", config["spacecraft"], "--scenario", "qemu-pil", "--image",
             f"shire-gsw-{config['mission']}:{config['spacecraft']}", "--compose", path])
    else:
        logs = WORK / "runs" / dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        logs.mkdir(parents=True, exist_ok=True)
        with (logs / "compose.log").open("w") as stream:
            subprocess.run(["docker", "compose", "-f", str(path), "logs", "--no-color"],
                           stdout=stream, stderr=subprocess.STDOUT, check=False)
        run(["docker", "compose", "-f", path, "down"])
        print(f"[qemu] Logs retained in {logs}")


def shell():
    config = active_config()
    path = ROOT / "build" / config["mission"] / "shire-qemu-compose.yaml"
    compose = ["docker", "compose", "-f", str(path)]
    identifier = subprocess.check_output([*compose, "ps", "-q", "shire-fsw"], text=True).strip()
    if not identifier:
        raise RuntimeError("Start the QEMU lab before opening a guest shell")
    state = json.loads(subprocess.check_output(["docker", "inspect", identifier], text=True))[0]
    if not state["State"]["Running"]:
        raise RuntimeError("The QEMU guest container is not running")
    # Dropbear creates fresh host keys in each ephemeral guest. Trust them on
    # first connection within this container launch, retaining checks thereafter.
    launch = hashlib.sha256((identifier + state["State"]["StartedAt"]).encode()).hexdigest()[:16]
    endpoint = subprocess.check_output([*compose, "port", "shire-fsw", "2222"], text=True).strip()
    host, port = endpoint.rsplit(":", 1)
    run(["ssh", "-i", WORK / "id_ed25519", "-o", "IdentitiesOnly=yes",
         "-o", f"UserKnownHostsFile={WORK / ('known_hosts-' + launch)}",
         "-o", "StrictHostKeyChecking=accept-new", "-p", port, f"root@{host}"])


def load_baseline(path):
    if path is None:
        return None
    try:
        report = json.loads(path.read_text())
    except (OSError, ValueError) as error:
        raise RuntimeError(f"Cannot read QEMU baseline {path}: {error}") from error
    if not isinstance(report, dict) or report.get("passed") is not True or not report.get("trials"):
        raise RuntimeError("QEMU baseline must be a passing acceptance report with completed trials")
    return report


def test(duration, trials, baseline_path=None, *, profile=None):
    """Reuse the existing full-stack fidelity, command, trace, and archive runner."""
    spec = importlib.util.spec_from_file_location("shire_perf", ROOT / "tools/shire-perf.py")
    perf = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(perf)
    baseline = load_baseline(baseline_path)
    profile = profile or {}
    config = active_config()
    names = perf.container_names(config["mission"], config["spacecraft"])
    compose = profile.get("compose", ROOT / "build" / config["mission"] / "shire-qemu-compose.yaml")
    require_runtime_images(compose)
    running = subprocess.check_output(["docker", "ps", "--format", "{{.Names}}"], text=True).splitlines()
    if set(names.values()) & set(running):
        raise RuntimeError("Stop the existing SHIRE lab before running board-test")
    directory = profile.get("work", WORK) / "reports" / dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    directory.mkdir(parents=True)
    scenario = perf.resolved_scenario(directory, config["spacecraft"])
    commands = json.loads(scenario.read_text())
    if 'command_port' in profile:
        for item in commands['commands']:
            item['port'] = profile['command_port']
    if any(item["sequence"] >= duration * 100 for item in commands["commands"]):
        for index, item in enumerate(commands["commands"]):
            item["sequence"] = int((index + 1) * duration * 100 / (len(commands["commands"]) + 1))
    scenario.write_text(json.dumps(commands, indent=2) + "\n")
    report = {"runtime": profile.get("runtime", "qemu-zynq-arm32"), "hardware_validated": False,
              "validation_scope": profile.get("validation_scope", "QEMU PIL with host simulated devices"),
              "duration_s": duration, "trials": [], "failures": [],
              "baseline": {"report": str(baseline_path.resolve()),
                           "sha256": hashlib.sha256(baseline_path.read_bytes()).hexdigest()}
                          if baseline else None,
              "git": perf.git_metadata(), "host": perf.host_metadata(),
              "platform": profile["platform"] if "platform" in profile else json.loads((WORK / "platform.json").read_text()),
              "target": profile["target"] if "target" in profile else json.loads((fsw_directory(config).parent / "bundle/manifest.json").read_text()),
              "runtime_image_id": subprocess.check_output(
                  ["docker", "image", "inspect", "--format", "{{.Id}}",
                   profile.get("image", f"shire-fsw-{config['mission']}-qemu:{config['spacecraft']}")], text=True).strip()}
    if "board" in profile:
        report["board"] = profile["board"]
    if "board_profile" in profile:
        report["board_profile"] = profile["board_profile"]
    target = directory / "report.json"
    label = profile.get('label', 'qemu')
    print(f"[{label}] Running {trials} trial(s), {duration:g} simulated seconds each. "
          "Flight software starts fresh for each trial.", flush=True)
    print(f"[{label}] Logs and report directory: {directory}", flush=True)
    try:
        for number in range(1, trials + 1):
            failures_before = len(report["failures"])
            def progress(message):
                print(f"[{label}] Trial {number}/{trials}: {message}", flush=True)

            result = perf.one_trial(compose, directory, "max", duration, 0, number, False, names, scenario,
                                    post_run=profile.get("post_run"), progress=progress)
            report["trials"].append(result)
            expected_ticks = math.ceil(round(duration * 1e9) / 10_000_000)
            if result["ticks"] != expected_ticks or result["fsw"]["sch_ticks"] != expected_ticks:
                report["failures"].append(f"trial {number}: incomplete synchronized tick count")
            participants = result["participants"]
            if result["completions"] != len(participants) * expected_ticks or any(
                    item["count"] != expected_ticks for item in participants):
                report["failures"].append(f"trial {number}: incomplete participant phases")
            for field in ("protocol_errors", "duplicate_completions", "stale_completions", "future_completions"):
                if result[field]:
                    report["failures"].append(f"trial {number}: {field}")
            terminal = result["terminal"]
            for field in ("component_phase_errors", "component_service_errors", "fortytwo_errors", "telemetry_errors", "visual_errors"):
                if terminal[field]:
                    report["failures"].append(f"trial {number}: {field}")
            if terminal["prepare_count"] != expected_ticks or terminal["commit_count"] != expected_ticks:
                report["failures"].append(f"trial {number}: missing dynamics phases")
            queue = terminal["queue"]
            if queue["overflows"] or queue["enqueued"] != queue["dequeued"]:
                report["failures"].append(f"trial {number}: actuator queue overflow or incomplete drain")
            if not terminal["state"]["valid"] or terminal["telemetry_count"] != expected_ticks // 100:
                report["failures"].append(f"trial {number}: invalid dynamics or incomplete truth cadence")
            fsw = result["fsw"]
            if (fsw["participants_registered"] != fsw["participants_completed"] or
                    fsw["participants_canceled"] or fsw["command_delivery_overflows"]):
                report["failures"].append(f"trial {number}: incomplete FSW work or command accounting")
            # The shared acceptance scenario targets DRM, whose TO_LAB wakeup
            # MID is 0x1887 and runs at 10 Hz in the source schedule table.
            ground = [item for item in fsw["deterministic_command_deliveries"] if item["mid"] == 0x1887]
            if sum(item["count"] for item in ground) != expected_ticks // 10:
                report["failures"].append(f"trial {number}: incomplete synchronized downlink wakeups")
            transactions = result["fsw"]["device_transactions"]
            if not transactions or any(item["errors"] for item in transactions):
                report["failures"].append(f"trial {number}: missing or failed device transactions")
            if result["scenario"]["accepted"] != len(commands["commands"]):
                report["failures"].append(f"trial {number}: command acceptance mismatch")
            scenario_result = result["scenario"]
            if (scenario_result["errors"] or scenario_result["injected"] != scenario_result["expected"] or
                    scenario_result["delivery_expected"] != scenario_result["delivery_observed"] or
                    scenario_result["acceptance_expected"] != scenario_result["acceptance_observed"]):
                report["failures"].append(f"trial {number}: scenario injection, delivery, or acceptance mismatch")
            if result["control_trace"]["ticks"] != expected_ticks:
                report["failures"].append(f"trial {number}: incomplete control trace")
            if not all(result["yamcs_truth"]["matched_to_trace"]) or result["yamcs_truth"]["increasing_pairs"] < 1:
                report["failures"].append(f"trial {number}: missing advancing Yamcs truth")
            archive = result["archive"]
            if not archive.get("replay_ready"):
                report["failures"].append(f"trial {number}: incomplete Yamcs archive")
            if number > 1:
                comparison = perf.fidelity(report["trials"][0], result)
                result["repeatability"] = comparison
                if not comparison["passed"]:
                    report["failures"].append(f"trial {number}: repeatability mismatch")
            if baseline:
                comparison = perf.fidelity(baseline["trials"][0], result)
                result["baseline_fidelity"] = comparison
                if not comparison["passed"]:
                    report["failures"].append(f"trial {number}: baseline fidelity mismatch")
            target.write_text(json.dumps(report, indent=2) + "\n")
            status = 'PASSED' if len(report['failures']) == failures_before else 'FAILED'
            progress(f"{status}: {result['ticks']} ticks, "
                     f"{scenario_result['accepted']}/{len(commands['commands'])} accepted commands, "
                     f"{result['achieved_speed']:.3f}x real time.")
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        report["failures"].append(str(error))
    report["passed"] = not report["failures"] and len(report["trials"]) == trials
    report["hardware_validated"] = report["passed"] and profile.get("physical_target", False)
    if report["trials"]:
        median = statistics.median(item["achieved_speed"] for item in report["trials"])
        report["performance"] = {"median_simulation_speed": median}
        if baseline:
            prior = statistics.median(item["achieved_speed"] for item in baseline["trials"])
            report["performance"].update(baseline_median_simulation_speed=prior,
                                         speedup=median / prior)
        print(f"[{profile.get('label', 'qemu')}] median simulation speed: {median:.3f}x real time")
    target.write_text(json.dumps(report, indent=2) + "\n")
    print(f"[{label}] Acceptance {'PASSED' if report['passed'] else 'FAILED'}: "
          f"{len(report['trials'])}/{trials} trials completed.", flush=True)
    print(f"[{profile.get('label', 'qemu')}] report: {target}")
    if not report["passed"]:
        raise RuntimeError(profile.get('label', 'QEMU') + " acceptance failed: " + "; ".join(report["failures"]))


def prepare(jobs):
    WORK.mkdir(parents=True, exist_ok=True)
    archive = WORK / "downloads" / f"buildroot-{VERSION}.tar.xz"
    archive.parent.mkdir(exist_ok=True)
    if not archive.exists():
        temporary = archive.with_suffix(".partial")
        with urllib.request.urlopen(f"https://buildroot.org/downloads/{archive.name}", timeout=60) as response:
            with temporary.open("wb") as target:
                while block := response.read(1024 * 1024):
                    target.write(block)
        temporary.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise RuntimeError(f"Buildroot checksum mismatch: {archive}")
    if not SOURCE.exists():
        with tarfile.open(archive) as source:
            source.extractall(WORK, filter="data")
    run(["docker", "build", "-t", TOOLS_IMAGE, "-f", ROOT / "tools/Dockerfile.qemu", ROOT / "tools"])
    OUTPUT.mkdir(exist_ok=True)
    fragment = OUTPUT / "shire.fragment"
    fragment.write_text((ROOT / "cfg/qemu/zynq.fragment").read_text().replace("@SHIRE_ROOT@", str(ROOT)))
    container(["make", "-C", SOURCE, f"O={OUTPUT}", "zynq_zc702_defconfig"])
    container(["bash", SOURCE / "support/kconfig/merge_config.sh", "-m",
               OUTPUT / ".config", fragment], cwd=OUTPUT)
    container(["make", "-C", SOURCE, f"O={OUTPUT}", "olddefconfig"])
    kernel_hash = hashlib.sha256((ROOT / "cfg/qemu/linux.fragment").read_bytes()).hexdigest()
    kernel_stamp = WORK / "kernel-fragment.sha256"
    if (OUTPUT / "build/linux-custom/.stamp_configured").exists() and (
            not kernel_stamp.exists() or kernel_stamp.read_text().strip() != kernel_hash):
        container(["make", "-C", SOURCE, f"O={OUTPUT}", f"-j{jobs}", "linux-reconfigure"], network=True)
    container(["make", "-C", SOURCE, f"O={OUTPUT}", f"-j{jobs}"], network=True)
    kernel_stamp.write_text(kernel_hash + "\n")
    container(["make", "-C", SOURCE, f"O={OUTPUT}", "legal-info"], network=True)
    supplement = OUTPUT / "legal-info/shire-supplement"
    supplement.mkdir(exist_ok=True)
    shutil.copy2(archive, supplement)
    kernel_licenses = supplement / "linux"
    kernel_licenses.mkdir(exist_ok=True)
    for name in ("COPYING", "LICENSES/preferred/GPL-2.0", "LICENSES/exceptions/Linux-syscall-note"):
        shutil.copy2(OUTPUT / "build/linux-custom" / name, kernel_licenses / Path(name).name)
    toolchain = OUTPUT / "host/opt/ext-toolchain"
    for name in ("README.txt", "summary.csv", "buildroot.config"):
        shutil.copy2(toolchain / name, supplement / f"toolchain-{name}")
    manifest = {"buildroot": VERSION, "source_sha256": SOURCE_SHA256,
                "machine": "xilinx-zynq-a9", "board_reference": "zc702",
                "hardware_validated": False,
                "configuration_sha256": hashlib.sha256((OUTPUT / ".config").read_bytes()).hexdigest(),
                "tools_image_id": subprocess.check_output(
                    ["docker", "image", "inspect", "--format", "{{.Id}}", TOOLS_IMAGE], text=True).strip(),
                "license_manifest": str((OUTPUT / "legal-info/manifest.csv").relative_to(ROOT)),
                "images": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                           for path in (OUTPUT / "images").iterdir() if path.is_file()}}
    (WORK / "platform.json").write_text(json.dumps(manifest, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["prepare", "build", "package", "start", "stop", "shell", "test"])
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--duration", type=float, default=30)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--baseline", type=Path,
                        help="Compare test fidelity and median speed with a passing QEMU report")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if not math.isfinite(args.duration) or args.duration < 1 or args.trials < 1:
        parser.error("--duration must be finite and at least 1 second; --trials must be positive")
    try:
        if args.command in ("prepare", "build"):
            {"prepare": prepare, "build": build}[args.command](args.jobs)
        elif args.command == "package":
            package()
        elif args.command == "test":
            test(args.duration, args.trials, args.baseline)
        elif args.command == "shell":
            shell()
        else:
            compose_command(args.command)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"[qemu] {error}\n")


if __name__ == "__main__":
    main()

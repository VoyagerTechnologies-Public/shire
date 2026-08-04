#!/usr/bin/env python3
"""
SHIRE Build Script - Builds only configured components based on build.yaml
Integrates with shire-orchestrator.py and Makefile
"""
import argparse
import os
import sys
import yaml
import subprocess

CFG_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.abspath(os.path.join(CFG_DIR, "../build"))
BUILD_YAML = os.path.join(BUILD_DIR, "build.yaml")
ROOT_DIR = os.path.abspath(os.path.join(CFG_DIR, ".."))
BUILD_IMAGE = "ghcr.io/voyagertechnologies-public/shire-base:latest"
FSW_DIR = os.environ.get("FSW_DIR", "cfs")
GSW_DIR = os.environ.get("GSW_DIR", "yamcs")


def fail(msg):
    print(f"[build] ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def load_yaml(path):
    if not os.path.exists(path):
        return None
    with open(path, "r") as f:
        return yaml.safe_load(f)


def get_build_config():
    """Load and validate build configuration"""
    if not os.path.exists(BUILD_YAML):
        fail(f"build.yaml not found at {BUILD_YAML}. Run 'make cfg' first.")
    
    config = load_yaml(BUILD_YAML)
    if not config:
        fail("build.yaml is empty or invalid")
    
    return config


def get_enabled_components(config):
    """Get list of enabled components from spacecraft or mission config"""
    spacecraft_cfg = config.get("spacecraft_cfg", {})
    mission_cfg = config.get("mission_cfg", {})
    
    # Prefer spacecraft-level components, fallback to mission-level
    if spacecraft_cfg and "components" in spacecraft_cfg:
        components = spacecraft_cfg.get("components", [])
    else:
        components = mission_cfg.get("components", [])
    
    return [comp["name"] for comp in components if "name" in comp]


def get_build_dirs(config):
    """Get build directory paths from config"""
    mission = config.get("mission")
    spacecraft = config.get("spacecraft")
    
    if not mission or not spacecraft:
        fail("Mission and spacecraft must be defined in build.yaml")
    
    builddir_mission = os.path.join(ROOT_DIR, "build", mission)
    builddir_base = os.path.join(builddir_mission, spacecraft)
    
    return {
        "mission": builddir_mission,
        "base": builddir_base,
        "sim": os.path.join(builddir_base, "sim"),
        "fsw": os.path.join(builddir_base, "fsw"),
        "gsw": os.path.join(builddir_base, "gsw"),
        "comp": os.path.join(builddir_base, "comp"),
    }


def run_make(target, cwd=None, env_vars=None, make_vars=None, capture=False, jobs=None, in_docker=True):
    """Run make command, optionally inside Docker container
    
    Args:
        env_vars: Environment variables to set
        make_vars: Variables to pass as make arguments (VAR=value)
    """
    if jobs is None:
        # Determine number of parallel jobs
        try:
            nproc = int(subprocess.check_output(["nproc"], text=True).strip())
            jobs = max(1, nproc - 1)
        except:
            jobs = 1
    
    cwd = cwd or ROOT_DIR
    env = os.environ.copy()
    if env_vars:
        env.update(env_vars)
    
    if in_docker:
        # Run make inside Docker container
        cmd = [
            "docker", "run", "--rm",
            "-v", f"{ROOT_DIR}:{ROOT_DIR}",
            "-w", cwd,
            "--user", f"{os.getuid()}:{os.getgid()}",
        ]
        
        # Add environment variables to Docker command
        if env_vars:
            for key, value in env_vars.items():
                cmd.extend(["-e", f"{key}={value}"])
        
        # Add image and make command
        cmd.extend([BUILD_IMAGE, "make", f"-j{jobs}"])
        
        # Add make variables as arguments
        if make_vars:
            for key, value in make_vars.items():
                cmd.append(f"{key}={value}")
        
        cmd.append(target)
    else:
        # Run make directly on host
        cmd = ["make", f"-j{jobs}"]
        
        # Add make variables as arguments
        if make_vars:
            for key, value in make_vars.items():
                cmd.append(f"{key}={value}")
        
        cmd.append(target)
    
    print(f"[build] Running: {' '.join(cmd[-3:])} in {cwd}")
    
    result = subprocess.run(
        cmd, 
        cwd=cwd if not in_docker else None,
        env=env if not in_docker else None,
        capture_output=capture,
        text=True
    )
    
    if result.returncode != 0:
        fail(f"Command failed: {' '.join(cmd[-3:])}")
    
    return result


def build_simulith(config, builddirs):
    """Build Simulith simulator core"""
    print(f"\n[build] Building Simulith...")
    
    env_vars = {
        "BUILDDIR": builddirs["sim"],
    }
    
    simulith_dir = os.path.join(ROOT_DIR, "simulith")
    # Use build-sim target which is the internal target (doesn't invoke docker)
    run_make("build-sim", cwd=simulith_dir, env_vars=env_vars, in_docker=True)


def build_42(config, builddirs):
    """Build 42 dynamics simulator using SHIRE's custom Dockerfile"""
    print(f"\n[build] Building 42 dynamics simulator...")
    
    fortytwo_dir = os.path.join(ROOT_DIR, "42")
    
    if not os.path.exists(fortytwo_dir):
        print(f"[build] ERROR: 42 directory not found at {fortytwo_dir}")
        fail("42 must be at top-level directory")
    
    mission = config["mission"]
    spacecraft = config["spacecraft"]
    image_name = f"shire-42-{mission}:{spacecraft}"
    
    # Use SHIRE's custom Dockerfile for better control over build and configuration
    cfg_dir = os.path.join(ROOT_DIR, "cfg")
    dockerfile = os.path.join(cfg_dir, "Dockerfile.42")
    
    if not os.path.exists(dockerfile):
        print(f"[build] ERROR: Dockerfile.42 not found at {dockerfile}")
        fail("SHIRE's 42 Dockerfile is required")
    
    print(f"[build] Building 42 container with SHIRE configuration...")
    
    # TODO: Copy spacecraft-specific configuration to staging area
    # For now, we'll use 42's defaults and override via IPC config volume mount
    
    # Build 42 container with our custom Dockerfile
    # Use ROOT_DIR as context so we can COPY the 42/ directory
    cmd = [
        "docker", "build",
        "-f", dockerfile,
        "-t", image_name,
        "--build-arg", f"MISSION={mission}",
        ROOT_DIR
    ]

    print(f"[build] Running: docker build -f Dockerfile.42 -t {image_name}")
    result = subprocess.run(cmd, cwd=ROOT_DIR)
    
    if result.returncode != 0:
        fail(f"Failed to build 42 container")
    
    print(f"[build] 42 container built: {image_name}")


def build_component_sim(comp_name, builddirs):
    """Build simulation for a specific component"""
    comp_sim_dir = os.path.join(ROOT_DIR, "comp", comp_name, "sim")
    
    if not os.path.exists(comp_sim_dir):
        print(f"[build] Skipping {comp_name}/sim (directory not found)")
        return
    
    if not os.path.exists(os.path.join(comp_sim_dir, "Makefile")):
        print(f"[build] Skipping {comp_name}/sim (no Makefile)")
        return
    
    print(f"[build] Building {comp_name}/sim...")
    comp_builddir = os.path.join(builddirs["comp"], comp_name, "sim")
    env_vars = {"BUILDDIR": comp_builddir}
    
    # Use build-sim target which is the internal target (doesn't invoke docker)
    run_make("build-sim", cwd=comp_sim_dir, env_vars=env_vars, in_docker=True)


def build_simulith_director_and_server(config, builddirs):
    """Build Simulith director and server Docker images"""
    print(f"\n[build] Building Simulith director and server images...")
    mission = config["mission"]
    spacecraft = config["spacecraft"]
    
    env_vars = {
        "BUILDDIR": builddirs["sim"],
        "BUILDDIR_COMP": builddirs["comp"],
        "SPACECRAFT": spacecraft,
        "MISSION": mission,
    }
    
    simulith_dir = os.path.join(ROOT_DIR, "simulith")
    
    # Copy component sims to build directory
    print(f"[build] Copying component simulators...")
    run_make("copy-sims", cwd=simulith_dir, env_vars=env_vars, in_docker=True)
    
    # Build Docker images for director and server (runs on host)
    print(f"[build] Building director image...")
    run_make("director", cwd=simulith_dir, env_vars=env_vars, in_docker=False)
    
    print(f"[build] Building server image...")
    run_make("server", cwd=simulith_dir, env_vars=env_vars, in_docker=False)


def build_sim(config):
    """Build all simulation components"""
    print(f"\n{'='*60}")
    print(f"[build] Building Simulation Components")
    print(f"{'='*60}")
    
    builddirs = get_build_dirs(config)
    components = get_enabled_components(config)
    
    print(f"[build] Enabled components: {', '.join(components)}")
    
    # Build 42 first (it's now a separate top-level component)
    build_42(config, builddirs)
    
    # Build Simulith core
    build_simulith(config, builddirs)
    
    # Build each enabled component's simulator
    for comp_name in components:
        build_component_sim(comp_name, builddirs)
    
    # Build director and server
    build_simulith_director_and_server(config, builddirs)
    
    print(f"[build] Simulation build complete")


def build_fsw(config):
    """Build flight software"""
    print(f"\n{'='*60}")
    print(f"[build] Building Flight Software")
    print(f"{'='*60}")
    
    builddirs = get_build_dirs(config)
    mission = config["mission"]
    spacecraft = config["spacecraft"]
    
    # Set up environment for FSW build
    fsw_dir = os.path.join(ROOT_DIR, FSW_DIR)
    
    env_vars = {
        "BUILDDIR": builddirs["fsw"],
        "SPACECRAFT": spacecraft,
        "MISSION": mission,
    }
    
    # Clean first
    print(f"[build] Cleaning FSW build directory...")
    run_make("clean", cwd=fsw_dir, env_vars=env_vars, in_docker=True)
    
    # Build FSW using internal target
    print(f"[build] Building FSW binaries...")
    run_make("build-fsw", cwd=fsw_dir, env_vars=env_vars, in_docker=True)
    
    # Copy libsimulith.so if available
    lib_dir = os.path.join(ROOT_DIR, "build", mission, spacecraft, "lib")
    os.makedirs(lib_dir, exist_ok=True)
    
    sim_lib_path = os.path.join(builddirs["sim"], "libsimulith.so")
    fsw_lib_path = os.path.join(builddirs["fsw"], "amd64-shire", "default_cpu1", "simulith", "libsimulith.so")
    dest_lib_path = os.path.join(lib_dir, "libsimulith.so")
    
    if os.path.exists(sim_lib_path):
        subprocess.run(["cp", sim_lib_path, dest_lib_path])
    elif os.path.exists(fsw_lib_path):
        subprocess.run(["cp", fsw_lib_path, dest_lib_path])
    
    # Build runtime Docker image
    print(f"[build] Building FSW runtime image...")
    runtime_image = f"shire-fsw-{mission}:{spacecraft}"
    cmd = [
        "docker", "build",
        "-t", runtime_image,
        "-f", f"{FSW_DIR}/tools/Dockerfile.fsw",
        "--build-arg", f"SPACECRAFT={spacecraft}",
        "--build-arg", f"MISSION={mission}",
        "."
    ]
    result = subprocess.run(cmd, cwd=ROOT_DIR)
    if result.returncode != 0:
        print(f"[build] WARNING: Failed to build FSW runtime image")
    
    print(f"[build] Flight software build complete")


def build_gsw(config):
    """Build ground software"""
    print(f"\n{'='*60}")
    print(f"[build] Building Ground Software")
    print(f"{'='*60}")
    
    mission = config["mission"]
    spacecraft = config["spacecraft"]
    
    # Build cryptolib first
    # Note: cryptolib Makefile invokes docker, so we run it on host (in_docker=False)
    print(f"[build] Building cryptolib...")
    cryptolib_dir = os.path.join(ROOT_DIR, "comp", "cryptolib")
    env_vars = {
        "SPACECRAFT": spacecraft,
        "MISSION": mission,
    }
    # Check if cryptolib Makefile exists and has shire target
    if os.path.exists(os.path.join(cryptolib_dir, "Makefile")):
        run_make("shire", cwd=cryptolib_dir, env_vars=env_vars, in_docker=False)
    else:
        print(f"[build] Skipping cryptolib (no Makefile found)")
    
    # Build GSW
    # Note: GSW is a YAMCS Java project - the runtime target builds the Docker image directly
    print(f"[build] Building GSW...")
    env_vars = {
        "SPACECRAFT": spacecraft,
        "MISSION": mission,
    }
    gsw_dir = os.path.join(ROOT_DIR, GSW_DIR)
    
    # Clean first
    run_make("clean", cwd=gsw_dir, env_vars=env_vars, in_docker=False)
    
    # Build runtime image (this is the only build step for GSW)
    print(f"[build] Building GSW runtime image...")
    run_make("runtime", cwd=gsw_dir, env_vars=env_vars, in_docker=False)
    
    print(f"[build] Ground software build complete")


def build_component_cli(comp_name, builddirs=None):
    """Build CLI for a specific component"""
    comp_cli_dir = os.path.join(ROOT_DIR, "comp", comp_name, "cli")
    
    if not os.path.exists(comp_cli_dir):
        print(f"[build] Skipping {comp_name}/cli (directory not found)")
        return
    
    if not os.path.exists(os.path.join(comp_cli_dir, "Makefile")):
        print(f"[build] Skipping {comp_name}/cli (no Makefile)")
        return
    
    # Use centralized build directory if provided
    env_vars = {}
    if builddirs:
        cli_builddir = os.path.join(builddirs["comp"], comp_name, "cli")
        env_vars["BUILDDIR"] = cli_builddir
        print(f"[build] Building {comp_name}/cli in {cli_builddir}...")
    else:
        print(f"[build] Building {comp_name}/cli...")
    
    # Runtime target builds and creates Docker image (runs on host)
    run_make("runtime", cwd=comp_cli_dir, env_vars=env_vars, in_docker=False)


def build_cli(config):
    """Build CLI components (includes simulith-director and component sims)"""
    print(f"\n{'='*60}")
    print(f"[build] Building CLI Components")
    print(f"{'='*60}")
    
    # Get CLI components from global.build.cli (not all enabled components)
    cli_components = []
    if "global" in config and "build" in config["global"] and "cli" in config["global"]["build"]:
        cli_list = config["global"]["build"]["cli"]
        if cli_list:
            cli_components = [item["name"] for item in cli_list if isinstance(item, dict) and "name" in item]
    
    if not cli_components:
        print(f"[build] No CLI components configured")
        return
    
    print(f"[build] CLI components: {', '.join(cli_components)}")
    
    # Get build directories
    builddirs = get_build_dirs(config)
    
    # Build 42 (shire-director connects to it at startup)
    print(f"[build] Building 42 simulator...")
    build_42(config, builddirs)

    # Build simulith core (includes simulith-director)
    print(f"[build] Building Simulith core...")
    build_simulith(config, builddirs)
    
    # Build component simulations for each CLI component
    for comp_name in cli_components:
        build_component_sim(comp_name, builddirs)
    
    # Build simulith-director Docker image
    print(f"[build] Building simulith-director image...")
    build_simulith_director_and_server(config, builddirs)
    
    # Build CLI Docker images
    for comp_name in cli_components:
        build_component_cli(comp_name, builddirs)
    
    print(f"[build] CLI build complete")


def build_all(config):
    """Build everything: sim, fsw, gsw"""
    build_sim(config)
    build_fsw(config)
    build_gsw(config)


def clean_component_sim(comp_name, build_dir=None):
    """Clean simulation for a specific component"""
    comp_sim_dir = os.path.join(ROOT_DIR, "comp", comp_name, "sim")

    if not os.path.exists(comp_sim_dir):
        return

    if not os.path.exists(os.path.join(comp_sim_dir, "Makefile")):
        return

    print(f"[build] Cleaning {comp_name}/sim...")
    env_vars = {"BUILDDIR": build_dir} if build_dir else None
    # Run on host: component clean targets invoke docker themselves
    run_make("clean", cwd=comp_sim_dir, env_vars=env_vars, in_docker=False)


def clean_component_cli(comp_name, builddirs=None):
    """Clean CLI for a specific component"""
    comp_cli_dir = os.path.join(ROOT_DIR, "comp", comp_name, "cli")
    
    if not os.path.exists(comp_cli_dir):
        return
    
    if not os.path.exists(os.path.join(comp_cli_dir, "Makefile")):
        return
    
    # Use centralized build directory if provided
    env_vars = {}
    if builddirs:
        cli_builddir = os.path.join(builddirs["comp"], comp_name, "cli")
        env_vars["BUILDDIR"] = cli_builddir
    
    print(f"[build] Cleaning {comp_name}/cli...")
    run_make("clean", cwd=comp_cli_dir, env_vars=env_vars, in_docker=True)


def clean_sim(config):
    """Clean simulation components"""
    print(f"\n[build] Cleaning simulation components...")

    builddirs = get_build_dirs(config)
    components = get_enabled_components(config)

    for comp_name in components:
        comp_builddir = os.path.join(builddirs["comp"], comp_name, "sim")
        clean_component_sim(comp_name, build_dir=comp_builddir)

    simulith_dir = os.path.join(ROOT_DIR, "simulith")
    if os.path.exists(os.path.join(simulith_dir, "Makefile")):
        run_make("clean", cwd=simulith_dir, env_vars={"BUILDDIR": builddirs["sim"]}, in_docker=True)


def clean_cli(config):
    """Clean CLI components"""
    print(f"\n[build] Cleaning CLI components...")
    
    # Get build directories
    builddirs = get_build_dirs(config)
    
    # Get CLI components from global.build.cli (not all enabled components)
    cli_components = []
    if "global" in config and "build" in config["global"] and "cli" in config["global"]["build"]:
        cli_list = config["global"]["build"]["cli"]
        if cli_list:
            cli_components = [item["name"] for item in cli_list if isinstance(item, dict) and "name" in item]
    
    for comp_name in cli_components:
        clean_component_cli(comp_name, builddirs)


def list_components(config):
    """List enabled components"""
    print(f"\n{'='*60}")
    print(f"[build] Configuration Summary")
    print(f"{'='*60}")
    print(f"Mission:    {config.get('mission')}")
    print(f"Spacecraft: {config.get('spacecraft')}")
    print(f"Scenario:   {config.get('scenario')}")
    print(f"\nEnabled Components:")
    
    components = get_enabled_components(config)
    for comp_name in components:
        comp_sim = os.path.join(ROOT_DIR, "comp", comp_name, "sim")
        comp_cli = os.path.join(ROOT_DIR, "comp", comp_name, "cli")
        
        has_sim = os.path.exists(os.path.join(comp_sim, "Makefile"))
        has_cli = os.path.exists(os.path.join(comp_cli, "Makefile"))
        
        features = []
        if has_sim:
            features.append("sim")
        if has_cli:
            features.append("cli")
        
        print(f"  - {comp_name:15} [{', '.join(features) if features else 'no build targets'}]")
    
    print(f"{'='*60}\n")


def main():
    parser = argparse.ArgumentParser(
        description="SHIRE Build Script - Build configured components",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s build     # Build sim, fsw, and gsw
  %(prog)s 42        # Build only 42 simulator container
  %(prog)s sim       # Build only simulation components
  %(prog)s fsw       # Build only flight software
  %(prog)s gsw       # Build only ground software
  %(prog)s cli       # Build only CLI components
  %(prog)s list      # List enabled components
  %(prog)s clean-sim # Clean simulation components
  %(prog)s clean-cli # Clean CLI components
  %(prog)s clean-42  # Clean 42 simulator container
        """
    )
    
    parser.add_argument(
        "target",
        choices=["build", "42", "sim", "fsw", "gsw", "cli", "list", "clean-sim", "clean-cli", "clean-42"],
        help="Build target"
    )
    
    args = parser.parse_args()
    
    # Load build configuration
    config = get_build_config()
    
    # Execute requested target
    if args.target == "build":
        build_all(config)
    elif args.target == "42":
        builddirs = get_build_dirs(config)
        build_42(config, builddirs)
    elif args.target == "sim":
        build_sim(config)
    elif args.target == "fsw":
        build_fsw(config)
    elif args.target == "gsw":
        build_gsw(config)
    elif args.target == "cli":
        build_cli(config)
    elif args.target == "list":
        list_components(config)
    elif args.target == "clean-sim":
        clean_sim(config)
    elif args.target == "clean-cli":
        clean_cli(config)
    elif args.target == "clean-42":
        print("[build] Cleaning 42 container...")
        mission = config["mission"]
        spacecraft = config["spacecraft"]
        image_name = f"shire-42-{mission}:{spacecraft}"
        
        # Check if image exists
        result = subprocess.run(
            ["docker", "images", "-q", image_name],
            capture_output=True,
            text=True
        )
        
        if result.stdout.strip():
            print(f"[build] Removing image: {image_name}")
            subprocess.run(["docker", "rmi", "-f", image_name], stderr=subprocess.DEVNULL)
        else:
            print(f"[build] Image {image_name} not found (already clean)")
        
        # Also clean up any dangling images from previous builds
        subprocess.run(["docker", "image", "prune", "-f"], stdout=subprocess.DEVNULL)
    
    print(f"\n[build] Target '{args.target}' completed successfully")


if __name__ == "__main__":
    main()

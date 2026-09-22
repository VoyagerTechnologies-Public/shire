#!/usr/bin/env python3
"""
Orchestrator for SHIRE configuration.
Loads global, mission, and scenario YAMLs, merges them, and writes to active.yaml.
"""
import argparse
import sys
import os
from jinja2 import Environment, FileSystemLoader
import yaml

from shire_provenance import git_head_sha

CFG_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.abspath(os.path.join(CFG_DIR, "../build"))
ACTIVE_PATH = os.path.join(BUILD_DIR, "active.yaml")
BUILD_PATH = os.path.join(BUILD_DIR, "build.yaml")
GLOBAL_CONFIG = os.path.join(CFG_DIR, "shire-config.yaml")
DEFAULT_FSW_DIR = "cfs"
DEFAULT_GSW_DIR = "yamcs"


def fail(msg):
    print(f"[orchestrator] ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def load_yaml(path):
    if not os.path.exists(path):
        return None
    with open(path, "r") as f:
        return yaml.safe_load(f)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli-debug", action="store_true", help="Force debug=True for all components (CLI builds)")
    parser.add_argument("--compose-only", action="store_true",
                        help="Only (re-)render cli-compose.yaml/shire-compose.yaml; skip device_cfg.h, "
                             "42_config, the scenario snapshot, and the FSW startup script edit. Used by "
                             "Monte Carlo campaign trials (issue #23) that already built their image and "
                             "only need a per-instance compose file rendered.")
    args = parser.parse_args()

    # Ensure build directory exists
    os.makedirs(BUILD_DIR, exist_ok=True)
    
    # If active.yaml does not exist, create with defaults from global config
    active = load_yaml(ACTIVE_PATH)
    global_cfg = load_yaml(GLOBAL_CONFIG)
    if active is None:
        # Use first mission and scenario as defaults
        missions = global_cfg["build"]["missions"]
        if not missions:
            fail("No missions defined in global config.")
        mission = missions[0]["name"]
        mission_cfg_path = os.path.join(CFG_DIR, os.path.relpath(missions[0]["config_file"], CFG_DIR))
        mission_cfg = load_yaml(mission_cfg_path)
        scenarios = mission_cfg.get("scenarios", [])
        scenario = scenarios[0]["name"] if scenarios else "nominal"
        
        # Default spacecraft selection from global or mission config
        if "spacecraft" in global_cfg["build"] and global_cfg["build"]["spacecraft"]:
            spacecraft_list = global_cfg["build"]["spacecraft"]
        else:
            spacecraft_list = mission_cfg.get("spacecraft", [])
        spacecraft = spacecraft_list[0]["name"] if spacecraft_list else None

        # Get the fsw directory
        if "fsw" in global_cfg["build"] and global_cfg["build"]["fsw"]:
            fsw_dir = global_cfg["build"]["fsw"]
        else:
            fsw_dir = mission_cfg.get("fsw", DEFAULT_FSW_DIR)

        # Get the gsw directory
        if "gsw" in global_cfg["build"] and global_cfg["build"]["gsw"]:
            gsw_dir = global_cfg["build"]["gsw"]
        else:
            gsw_dir = mission_cfg.get("gsw", DEFAULT_GSW_DIR)
        
        active = {"mission": mission, "spacecraft": spacecraft, "scenario": scenario, "cli": "demo", "log_mode": "none", "graphics": True, "fsw_dir": fsw_dir, "gsw_dir": gsw_dir}
        with open(ACTIVE_PATH, "w") as f:
            yaml.safe_dump(active, f)
        print(f"[orchestrator] Created {ACTIVE_PATH} with defaults: mission={mission}, spacecraft={spacecraft}, scenario={scenario}, graphics=True")

    mission = active.get("mission", "drm")
    spacecraft = active.get("spacecraft", "sat-1")
    scenario = active.get("scenario", "nominal")
    cli_component = active.get("cli", "demo")
    log_mode = active.get("log_mode", active.get("log", "none"))
    graphics = active.get("graphics", True)  # Default to graphics enabled
    fsw_dir = active.get("fsw_dir", DEFAULT_FSW_DIR)
    gsw_dir = active.get("gsw_dir", DEFAULT_GSW_DIR)

    # Monte Carlo campaign (issue #23) instance-scoping fields. All optional
    # and unset for a plain `make scenario`/`make start` run, in which case
    # every use below defaults away to today's behavior exactly.
    instance = active.get("instance")
    port_offset = active.get("port_offset", 0)
    image_tag = active.get("image_tag")
    initial_conditions_file = active.get("initial_conditions_file")

    # Find mission config file
    mission_entry = next((m for m in global_cfg["build"]["missions"] if m["name"] == mission), None)
    if not mission_entry:
        fail(f"Mission '{mission}' not found in global config.")
    mission_cfg_path = os.path.join(CFG_DIR, os.path.relpath(mission_entry["config_file"], CFG_DIR))
    mission_cfg = load_yaml(mission_cfg_path)

    # Find scenario config file
    scenario_entry = next((s for s in mission_cfg["scenarios"] if s["name"] == scenario), None)
    if not scenario_entry:
        fail(f"Scenario '{scenario}' not found in mission config.")
    scenario_cfg_path = os.path.join(CFG_DIR, os.path.relpath(scenario_entry["config_file"], CFG_DIR))
    scenario_cfg = load_yaml(scenario_cfg_path)

    # Load the Initial Condition (IC) bin this scenario references (orbit,
    # epoch, attitude, and optional per-component state). Scenarios that
    # don't set `initial_conditions:` get "nominal-baseline", which
    # reproduces today's hardcoded values exactly, so existing scenarios
    # (drm-nominal, drm-debug) render unchanged.
    # A campaign trial's generated, perturbed IC (initial_conditions_file,
    # an absolute path written by cfg/shire-campaign.py) takes priority
    # over the scenario's named IC bin -- this is the only hook a per-trial
    # IC needs into the orchestrator.
    if initial_conditions_file:
        ic_name = f"file:{initial_conditions_file}"
        ic_cfg = load_yaml(initial_conditions_file)
        if ic_cfg is None:
            fail(f"Initial condition file '{initial_conditions_file}' not found.")
    else:
        ic_name = scenario_cfg.get("initial_conditions", "nominal-baseline")
        ic_cfg_path = os.path.join(CFG_DIR, "drm", "initial_conditions", f"{ic_name}.yaml")
        ic_cfg = load_yaml(ic_cfg_path)
        if ic_cfg is None:
            fail(f"Initial condition bin '{ic_name}' not found at {ic_cfg_path}.")

    ground_stations_cfg = load_yaml(os.path.join(CFG_DIR, "drm", "ground_stations.yaml"))
    ground_stations = (ground_stations_cfg or {}).get("ground_stations", [])

    # Load spacecraft config if specified
    spacecraft_cfg = {}
    if spacecraft:
        spacecraft_list = mission_cfg.get("spacecraft", [])
        spacecraft_entry = next((sc for sc in spacecraft_list if sc["name"] == spacecraft), None)
        if not spacecraft_entry:
            fail(f"Spacecraft '{spacecraft}' not found in mission config.")
        spacecraft_cfg_path = os.path.join(CFG_DIR, os.path.relpath(spacecraft_entry["config_file"], CFG_DIR))
        spacecraft_cfg = load_yaml(spacecraft_cfg_path)
        if not spacecraft_cfg:
            spacecraft_cfg = {}

    # Merge configs (minimal: just collect for now)
    merged = {
        "mission": mission,
        "spacecraft": spacecraft,
        "scenario": scenario,
        "global": global_cfg,
        "mission_cfg": mission_cfg,
        "spacecraft_cfg": spacecraft_cfg,
        "scenario_cfg": scenario_cfg,
        "image_tag": image_tag or spacecraft,
    }
    
    # Override global.build.cli with the active CLI component from active.yaml
    if "global" in merged and "build" in merged["global"]:
        merged["global"]["build"]["cli"] = [{"name": cli_component}]

    # Write merged config to build.yaml
    with open(BUILD_PATH, "w") as f:
        yaml.safe_dump(merged, f)
    print(f"[orchestrator] Merged config written to {BUILD_PATH}")

    # Render config files for components. Skipped under --compose-only:
    # this writes comp/<name>/shared/device_cfg.h, which a Monte Carlo
    # campaign trial reusing an already-built image must NOT touch (that
    # file is what got baked into the image; re-rendering it here would
    # race a concurrently-building trial with different content).
    if not args.compose_only:
        # Determine which components to process based on spacecraft or fallback to mission components
        if spacecraft and merged["spacecraft_cfg"]:
            components = merged["spacecraft_cfg"].get("components", [])
        else:
            # Fallback to mission-level components for backward compatibility
            components = merged["mission_cfg"].get("components", [])

        for comp in components:
            comp_name = comp.get("name")
            if not comp_name:
                continue

            # Full cascading merge: fallback -> global -> mission -> spacecraft -> IC bin -> scenario -> scenario overrides
            # 1. Fallback config
            fallback_path = os.path.abspath(os.path.join(CFG_DIR, f'../comp/{comp_name}/support/device_config.yaml'))
            fallback_data = load_yaml(fallback_path)
            comp_cfg = dict(fallback_data.get(comp_name, {})) if fallback_data and fallback_data.get(comp_name) else {}

            # 2. Global config
            global_cfg_comp = merged["global"].get(comp_name, {})
            comp_cfg.update(global_cfg_comp)

            # 3. Mission config
            mission_cfg_comp = merged["mission_cfg"].get(comp_name, {})
            comp_cfg.update(mission_cfg_comp)

            # 4. Spacecraft config (NEW LAYER)
            if merged["spacecraft_cfg"]:
                spacecraft_cfg_comp = merged["spacecraft_cfg"].get(comp_name, {})
                comp_cfg.update(spacecraft_cfg_comp)

            # 5. Initial condition bin's per-component state (NEW LAYER: EPS SOC,
            #    ADCS starting mode, fault-injection flags, etc.) — applied
            #    before the scenario's own component config so a scenario can
            #    still override an IC's component state if it needs to.
            ic_comp_overrides = (ic_cfg.get("component_overrides") or {}).get(comp_name, {})
            comp_cfg.update(ic_comp_overrides)

            # 6. Scenario config
            scenario_cfg_comp = merged["scenario_cfg"].get(comp_name, {})
            comp_cfg.update(scenario_cfg_comp)

            # 7. Scenario-level 'overrides' dict
            overrides = merged["scenario_cfg"].get("overrides", {})
            if overrides is None:
                overrides = {}
            comp_cfg.update(overrides)

            # 8. CLI debug override (highest priority)
            if args.cli_debug:
                comp_cfg["debug"] = True

            # Try to find a template for this component
            template_path = os.path.abspath(os.path.join(CFG_DIR, f'../comp/{comp_name}/support'))
            template_file = f'device_config.j2'
            template_full_path = os.path.join(template_path, template_file)
            if os.path.exists(template_full_path) and comp_cfg:
                env = Environment(loader=FileSystemLoader(template_path))
                template = env.get_template(template_file)
                output = template.render(config=comp_cfg)

                # Output path for the generated config (e.g., comp/<name>/shared/<name>_cfg.h)
                output_path = os.path.abspath(os.path.join(CFG_DIR, f'../comp/{comp_name}/shared/device_cfg.h'))
                os.makedirs(os.path.dirname(output_path), exist_ok=True)
                with open(output_path, 'w') as f:
                    f.write(output)
                print(f"[orchestrator] device_cfg.h written to {output_path}")
            else:
                print(f"[orchestrator] No config or template found for component '{comp_name}', skipping.")

    # Render cli-compose.yaml from Jinja2 template using cli_component
    cli_template_path = os.path.abspath(os.path.join(CFG_DIR))
    cli_template_file = "cli-compose.j2"
    cli_template_full_path = os.path.join(cli_template_path, cli_template_file)
    build_mission_dir = os.path.abspath(os.path.join(CFG_DIR, f'../build/{mission}'))
    os.makedirs(build_mission_dir, exist_ok=True)
    cli_compose_output_path = os.path.join(build_mission_dir, "cli-compose.yaml")
    if os.path.exists(cli_template_full_path):
        env = Environment(loader=FileSystemLoader(cli_template_path))
        template = env.get_template(cli_template_file)
        output = template.render(cli_component=cli_component, spacecraft=spacecraft, mission=mission, fsw_dir=fsw_dir, gsw_dir=gsw_dir)
        with open(cli_compose_output_path, "w") as f:
            f.write(output)
        print(f"[orchestrator] cli-compose.yaml written to {cli_compose_output_path} (cli_component={cli_component})")
    else:
        print(f"[orchestrator] cli-compose.j2 template not found, skipping cli-compose.yaml generation.")

    # Render shire-compose.yaml from Jinja2 template using log_mode and
    # spacecraft. A campaign trial's compose file lives under its own
    # build/<mission>/<instance>/ subdirectory rather than the shared
    # build/<mission>/ path, since multiple trials' compose files must
    # coexist on disk while their stacks run concurrently; a plain
    # `make scenario`/`make start` run (instance unset) is unaffected.
    lab_template_path = os.path.abspath(os.path.join(CFG_DIR))
    lab_template_file = "shire-compose.j2"
    lab_template_full_path = os.path.join(lab_template_path, lab_template_file)
    lab_compose_dir = os.path.join(build_mission_dir, instance) if instance else build_mission_dir
    os.makedirs(lab_compose_dir, exist_ok=True)
    lab_compose_output_path = os.path.join(lab_compose_dir, "shire-compose.yaml")
    if os.path.exists(lab_template_full_path):
        env = Environment(loader=FileSystemLoader(lab_template_path))
        template = env.get_template(lab_template_file)
        output = template.render(log_mode=log_mode, spacecraft=spacecraft, mission=mission, fsw_dir=fsw_dir,
                                 gsw_dir=gsw_dir, instance=instance, port_offset=port_offset,
                                 image_tag=merged["image_tag"])
        with open(lab_compose_output_path, "w") as f:
            f.write(output)
        print(f"[orchestrator] shire-compose.yaml written to {lab_compose_output_path} "
              f"(log_mode={log_mode}, spacecraft={spacecraft}, instance={instance}, port_offset={port_offset})")
    else:
        print(f"[orchestrator] shire-compose.j2 template not found, skipping shire-compose.yaml generation.")

    # Render 42's Inp_Sim.txt / Orb_SHIRE.txt / SC_SHIRE.txt, snapshot the
    # scenario, and adjust the FSW startup script. All of these write to
    # fixed, non-instance-scoped paths that get baked into images at build
    # time -- skipped under --compose-only, since a campaign trial reusing
    # an already-built image must not race a concurrently-building trial
    # with different content over these same paths.
    if not args.compose_only:
        # Render 42's Inp_Sim.txt / Orb_SHIRE.txt / SC_SHIRE.txt from Jinja2
        # templates using the graphics setting and the scenario's resolved IC.
        sim_template_path = os.path.abspath(os.path.join(CFG_DIR, '42_shire_config'))
        build_42_config_dir = os.path.abspath(os.path.join(CFG_DIR, f'../build/{mission}/42_config'))
        os.makedirs(build_42_config_dir, exist_ok=True)

        for template_file, output_name, extra_context in (
            ("Inp_Sim.j2", "Inp_Sim.txt", {"graphics": graphics, "ground_stations": ground_stations}),
            ("Orb_SHIRE.j2", "Orb_SHIRE.txt", {}),
            ("SC_SHIRE.j2", "SC_SHIRE.txt", {}),
        ):
            template_full_path = os.path.join(sim_template_path, template_file)
            output_path = os.path.join(build_42_config_dir, output_name)
            if os.path.exists(template_full_path):
                env = Environment(loader=FileSystemLoader(sim_template_path))
                template = env.get_template(template_file)
                output = template.render(ic=ic_cfg, **extra_context)
                with open(output_path, "w") as f:
                    f.write(output)
                print(f"[orchestrator] {output_name} written to {output_path} (initial_conditions={ic_name})")
            else:
                print(f"[orchestrator] {template_file} template not found, skipping {output_name} generation.")

        # Snapshot the scenario + resolved IC selection for this build, so a
        # specific run's exact starting state stays traceable even after
        # build/active.yaml later points at something else. Cheap git SHA only
        # (not the full submodule/dirty walk shire-perf.py's git_metadata() does)
        # since this runs on every `make cfg`, including trivial edits.
        scenario_build_dir = os.path.abspath(os.path.join(CFG_DIR, f'../build/{mission}/scenario'))
        os.makedirs(scenario_build_dir, exist_ok=True)
        snapshot_path = os.path.join(scenario_build_dir, f"{scenario}.snapshot.yaml")
        snapshot = {
            "scenario_name": scenario,
            "initial_conditions": ic_name,
            "resolved_ic": ic_cfg,
            "git_sha": git_head_sha(),
        }
        with open(snapshot_path, "w") as f:
            yaml.safe_dump(snapshot, f, sort_keys=False)
        print(f"[orchestrator] Scenario snapshot written to {snapshot_path}")

    # Copy and manipulate spacecraft-specific FSW config files
    if spacecraft and not args.compose_only:
        build_cfg_dir = os.path.abspath(os.path.join(CFG_DIR, f'../build/{mission}/{spacecraft}/shire_defs'))
        baseline_cfg_dir = os.path.abspath(os.path.join(CFG_DIR, 'shire_defs'))
        os.makedirs(build_cfg_dir, exist_ok=True)

        # Copy all baseline config files to build/<mission>/<spacecraft>/cfg
        import shutil
        for item in os.listdir(baseline_cfg_dir):
            s = os.path.join(baseline_cfg_dir, item)
            d = os.path.join(build_cfg_dir, item)
            if os.path.isdir(s):
                if os.path.exists(d):
                    shutil.rmtree(d)
                shutil.copytree(s, d)
            else:
                shutil.copy2(s, d)
        print(f"[orchestrator] Baseline FSW config files copied to {build_cfg_dir}")

        # Manipulate cpu1_cfe_es_startup.scr to remove lines for components not enabled for the spacecraft
        startup_scr_path = os.path.join(build_cfg_dir, "cpu1_cfe_es_startup.scr")
        enabled_components = set()
        # Get enabled components from spacecraft_cfg (preferred) or mission_cfg
        if merged["spacecraft_cfg"] and "components" in merged["spacecraft_cfg"]:
            enabled_components = set(comp["name"] for comp in merged["spacecraft_cfg"]["components"] if "name" in comp)
        elif "components" in merged["mission_cfg"]:
            enabled_components = set(comp["name"] for comp in merged["mission_cfg"]["components"] if "name" in comp)

        if os.path.exists(startup_scr_path):
            with open(startup_scr_path, "r") as f:
                lines = f.readlines()
            new_lines = []
            # Only modify lines for spacecraft-specific components (adcs, demo, eps, radio)
            spacecraft_apps = {"adcs", "demo", "eps", "radio"}
            for line in lines:
                # Only process non-comment, non-blank lines before '!'
                if line.strip().startswith("!") or not line.strip():
                    new_lines.append(line)
                    continue
                # Check for CFE_APP lines for spacecraft-specific components
                if line.startswith("CFE_APP"):
                    parts = [p.strip() for p in line.split(",")]
                    if len(parts) > 1:
                        comp_name = parts[1]
                        if comp_name in spacecraft_apps:
                            # Only keep if enabled for this spacecraft
                            if comp_name in enabled_components:
                                new_lines.append(line)
                            else:
                                print(f"[orchestrator] Removing {comp_name} from startup script (not enabled for {spacecraft})")
                        else:
                            new_lines.append(line)
                    else:
                        new_lines.append(line)
                else:
                    new_lines.append(line)
            with open(startup_scr_path, "w") as f:
                f.writelines(new_lines)
            print(f"[orchestrator] Updated {startup_scr_path} to only include enabled spacecraft components: {sorted(enabled_components)}")
        else:
            print(f"[orchestrator] {startup_scr_path} not found, skipping manipulation.")

if __name__ == "__main__":
    main()

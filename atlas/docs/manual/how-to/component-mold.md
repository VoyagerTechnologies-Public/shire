# Component Mold

The component mold is the repo-supported shortcut for creating a new component based on the demo component. 
Use it to scaffold a new `comp/<name>` tree that follows the repository conventions for CLI, FSW, simulation, tests and ground artifacts described in [Components](./components.md).

At the top-level run:
```bash
make mold COMP=new_sensor
```

If a target component directory already exists the script will ask before overwriting it.

This runs `cfg/shire-comp-mold.py` which performs the following high-level steps:

* Copies the `comp/demo` directory to `comp/<component>` (skips `.git`, `build/` and common temporary files).
* Replaces strings in text files to map `demo` to `<component>` (various case styles: `demo`, `Demo`, `DEMO`).
* Applies safe defaults and small protocol changes to avoid conflicts (device path and handle, and message IDs — see details below).
* Rename files and directories whose names contain `demo` to use the new component name.

What the mold changes for you (details from `cfg/shire-comp-mold.py`):

* Name normalization: the supplied component name is validated and normalized; replacements include three casing variants:
	* `{{lower}}` to lower-case replacement (e.g., `demo` to `my_sensor`)
	* `{{first}}` to capitalized (e.g., `Demo` to `My_sensor`)
	* `{{upper}}` to upper-case (e.g., `DEMO` to `MY_SENSOR`)
* Text replacements: the mold updates file contents for many common tokens. Notable automated replacements in the default mold:
	* `/dev/usart_5` to `/dev/usart_9`
	* `handle: 5` to `handle: 9`
	* Message ID remapping to reduce clashes:
		* `0x18FA` to `0x18FC`
		* `0x18FB` to `0x18FD`
		* `0x08FA` to `0x08FC`
		* `0x08FB` to `0x08FD`

These are safe defaults to make new components avoid conflict with the demo IDs so you can run and verify them immediately along side the demo component.
You **MUST** review and choose new message IDs before creating other components to avoid clashes.

Post-creation steps (what the script prints and what you should do next):

1. Review the generated component in ./comp/{component_name}
2. Add the component to your active mission, default is ./cfg/drm/drm.yaml
3. Add the component to the FSW definitions:
    * ./fsw/shire_defs/cpu1_cfe_es_startup.scr
    * ./fsw/shire_defs/tables/sch_def_msgtbl.c
    * ./fsw/shire_defs/tables/sch_def_schtbl.c
    * ./fsw/shire_defs/tables/to_lab_sub.c
    * ./fsw/shire_defs/targets.cmake
4. Add the component to the GSW definitions:
    * ./gsw/src/main/yamcs/etc/yamcs.shire.yaml
5. Build like you would normally and confirm new component runs
6. Customize the component for your specific needs

## Advanced Options

The mold script accepts a `--source` flag to copy from a different source component instead of `demo`. 

Example:
```bash
python3 cfg/shire-comp-mold.py my_sensor --source=some_other_component
```

----
Last updated: 20251203

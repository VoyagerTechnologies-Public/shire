# Uploading tables and patches

SHIRE builds some flight software files for the ground to uplink instead of for the spacecraft to boot with.
They let you practice the operations that change a flying spacecraft: loading a table, loading a stored command sequence, and replacing an application.
The [Cmd/Tlm Validation](../../scenarios/cmd-tlm-validation.md) scenario uses all of them.

## How files reach the spacecraft

Files that are built for uplink have names that start with `up_`.
The flight software build produces them next to the boot files, and the ground software build copies them into the YAMCS image.
When the image starts, they are placed in the `cfdpUp` bucket, which the File transfer page and the upload command read.
The spacecraft ignores them at boot because their file names are not the ones that the flight software loads.

| File | What it is |
| --- | --- |
| `up_sc_rts012.tbl` | A replacement for the empty RTS 12 table that sends a NOOP to ES, TBL, and SB. |
| `up_sc_rts13bad.tbl` | A deliberately corrupt RTS 13 table that fails validation. |
| `up_lc_wdt.tbl` | The boot LC watchpoint table plus watchpoint 12, which watches the EPS battery voltage. |
| `up_eps.so` | A build of the EPS application that refuses to switch on switch 5. |

There are two ways to send a file:

* Open **File transfer** in YAMCS and start a new transfer from the `cfdpUp` bucket
* Send `/CCSDS/CFDP_UPLOAD` with an `OBJECT_NAME` and a `REMOTE_PATH`, which completes when the transfer does and is how procedures upload

`/CCSDS/CFDP_UPLOAD` accepts only a plain file name that is in the bucket and a path under `/d`.
It refuses anything else, such as a path under `/cf`, so it cannot overwrite the boot files.
A refusal appears as a failed command acknowledgement with the reason.

## Load a table

A table is replaced in four steps.
Use the table name that is inside the file, such as `SC.RTS_TBL012` or `LC.LC_WDT`.

1. Upload the file to `/d` as above.
2. Send `/CFE_TBL/CFE_TBL_COMMANDS/CFE_TBL_LOAD` with `LOADFILENAME` set to the uploaded path, which puts the file in the table's inactive buffer.
3. Send `CFE_TBL_VALIDATE` with `ACTIVETBLFLAG` set to `0` and the table name.
The application that owns the table checks the contents.
Read `SUCCESSVALCTR`, `FAILEDVALCTR`, and `LASTVALSTATUS` in the cFE TBL housekeeping.
4. Send `CFE_TBL_ACTIVATE` with the table name.

If validation fails, send `CFE_TBL_ABORT_LOAD` and the table keeps its current contents.

Activating an RTS table disables that RTS.
Send `/SC/SC_COMMANDS/SC_ENABLE_RTS` before you start it.

To put a table back, load the boot file from `/cf` the same way.

## Replace an application

Send `/CFE_ES/CFE_ES_COMMANDS/CFE_ES_RELOAD_APP` with the application's name, such as `EPS_APP`, and the path of the new module.
ES stops the application and starts it again from that file.
The application restarts in its boot state, so its counters start from zero and any device it had enabled is disabled again.

To check which module is running, send `CFE_ES_QUERY_ONE` for the application and read `APPINFO_FILENAME` in the `CFE_ES_ONEAPPTLM` parameters.
To put the original back, reload from the boot module under `/cf`.

Be careful with applications that other operations depend on.
Reloading the Radio application restarts it disabled, which drops the Radio uplink until it is enabled again.
The [simulation notes](../core-concepts/simulations.md#stopping-reloading-and-restarting-applications) explain how application restarts work in the synchronized simulation.

## Build your own files

A table that is uplinked is a second table source with its own file name.
The name must be at most 19 characters, which is why these files use the short `up_` prefix.

1. Add a `.c` file for the table under `cfg/shire_defs/tables`.
Give it the same table name as the table it replaces, and a different file name that starts with `up_`.
2. Register it in `cfg/shire_defs/cpu1/install_custom.cmake` with `add_cfe_tables`.
3. Run `make`.
The build copies every `up_*` file into the ground software image.

An application patch is a second build of the same sources.
In the application's `CMakeLists.txt`, add another `add_cfe_app` with a name that starts with `up_`, and set a compile definition that your source tests.
The patched module exports the same entry point as the original, which is what `CFE_ES_RELOAD_APP` loads.
`comp/eps/CMakeLists.txt` and `comp/eps/src/eps_app.c` show the pattern.

Test every patch in both directions.
Show the behavior before, apply the patch, show the behavior after, and restore the original.
`CmdTlmPatches.ycs` is a worked example.

***
Last reviewed: 20261026

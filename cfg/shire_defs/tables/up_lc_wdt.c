/*
 * LC watchpoint table with one more watchpoint, for the ground to upload.
 *
 * It is the boot table (lc_def_wdt.c) plus watchpoint 12, which watches the EPS battery voltage against the
 * 20 V yellow limit. The file name is not the one LC loads at boot, so it takes no effect until the ground
 * uploads it and loads, validates, and activates it as table LC.LC_WDT.
 */
#define LC_WDT_UPLINK_PATCH
#define SHIRE_LC_WDT_FILE "up_lc_wdt.tbl"
#include "lc_def_wdt.c"

/*
 * RTS 12, an uploadable replacement for the empty RTS 12 table.
 *
 * It is built with the flight software but its file name does not match what SC loads at boot, so it takes no
 * effect until the ground uploads it and loads, validates, and activates it as table SC.RTS_TBL012. Starting
 * RTS 12 then sends one NOOP each to cFE ES, TBL, and SB, one second apart. Each NOOP raises a different
 * application's command counter by one, which gives the ground an independent check for every entry.
 */

#include "cfe.h"
#include "cfe_tbl_filedef.h"

#include "sc_tbldefs.h"      /* defines SC table headers */
#include "sc_platform_cfg.h" /* defines table buffer size */
#include "sc_msgdefs.h"      /* defines SC command code values */
#include "sc_msgids.h"       /* defines SC packet msg ID's */
#include "sc_msg.h"          /* defines SC message structures */

#include "cfe_es_msgstruct.h"
#include "cfe_es_msgids.h"
#include "cfe_tbl_msgstruct.h"
#include "cfe_tbl_msgids.h"
#include "cfe_sb_msgstruct.h"
#include "cfe_sb_msgids.h"

/* Note: Assumes SC_PLATFORM_ENABLE_HEADER_UPDATE is true, so SC fills in the packet checksum when it sends. */

typedef struct
{
    /* 1 - ES NOOP */
    SC_RtsEntryHeader_t hdr1;
    CFE_ES_NoopCmd_t    cmd1;

    /* 2 - TBL NOOP */
    SC_RtsEntryHeader_t hdr2;
    CFE_TBL_NoopCmd_t   cmd2;

    /* 3 - SB NOOP */
    SC_RtsEntryHeader_t hdr3;
    CFE_SB_NoopCmd_t    cmd3;
} SC_RtsStruct012Up_t;

/* Define the union to size the table correctly */
typedef union
{
    SC_RtsStruct012Up_t rts;
    uint16              buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable012Up_t;

/* Helper macro to get size of structure elements */
#define SC_MEMBER_SIZE(member) (sizeof(((SC_RtsStruct012Up_t *)0)->member))

SC_RtsTable012Up_t SC_Rts012Up =
{
    /* 1 - ES NOOP, one second after the RTS starts */
    .rts.hdr1.WakeupCount = 1,
    .rts.cmd1.CommandHeader = CFE_MSG_CMD_HDR_INIT(CFE_ES_CMD_MID, SC_MEMBER_SIZE(cmd1), 0x00, 0x00),

    /* 2 - TBL NOOP, one second later */
    .rts.hdr2.WakeupCount = 1,
    .rts.cmd2.CommandHeader = CFE_MSG_CMD_HDR_INIT(CFE_TBL_CMD_MID, SC_MEMBER_SIZE(cmd2), 0x00, 0x00),

    /* 3 - SB NOOP, one second later */
    .rts.hdr3.WakeupCount = 1,
    .rts.cmd3.CommandHeader = CFE_MSG_CMD_HDR_INIT(CFE_SB_CMD_MID, SC_MEMBER_SIZE(cmd3), 0x00, 0x00),
};

/* The table name is the one SC loads for RTS 12. The file name is not, so SC ignores this file at boot. */
CFE_TBL_FILEDEF(SC_Rts012Up, SC.RTS_TBL012, Uploadable RTS_TBL012, up_sc_rts012.tbl)

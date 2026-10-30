/*
 * A deliberately corrupt RTS 13 table, for the ground to upload to show that a bad table is caught.
 *
 * The first command claims a length of one byte, which is shorter than any command, so SC rejects the table when
 * it validates it. The ground then aborts the load and RTS 13 keeps its boot table.
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

typedef struct
{
    SC_RtsEntryHeader_t hdr1;
    CFE_ES_NoopCmd_t    cmd1;
} SC_RtsStruct013Bad_t;

typedef union
{
    SC_RtsStruct013Bad_t rts;
    uint16               buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable013Bad_t;

SC_RtsTable013Bad_t SC_Rts013Bad =
{
    .rts.hdr1.WakeupCount = 1,
    /* A length of 1 is invalid. A real NOOP is sizeof(CFE_ES_NoopCmd_t). */
    .rts.cmd1.CommandHeader = CFE_MSG_CMD_HDR_INIT(CFE_ES_CMD_MID, 1, 0x00, 0x00),
};

CFE_TBL_FILEDEF(SC_Rts013Bad, SC.RTS_TBL013, Corrupt RTS_TBL013, up_sc_rts13bad.tbl)

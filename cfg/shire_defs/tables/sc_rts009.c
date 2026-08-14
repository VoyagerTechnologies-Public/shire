#include "cfe.h"
#include "cfe_tbl_filedef.h"
#include "sc_tbldefs.h"
#include "sc_platform_cfg.h"

typedef union
{
    uint16 buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable009_t;

SC_RtsTable009_t SC_Rts009 = {{ 0 }};

CFE_TBL_FILEDEF(SC_Rts009, SC.RTS_TBL009, SC RTS_TBL009, sc_rts009.tbl)

#include "cfe.h"
#include "cfe_tbl_filedef.h"
#include "sc_tbldefs.h"
#include "sc_platform_cfg.h"

typedef union
{
    uint16 buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable008_t;

SC_RtsTable008_t SC_Rts008 = {{ 0 }};

CFE_TBL_FILEDEF(SC_Rts008, SC.RTS_TBL008, SC RTS_TBL008, sc_rts008.tbl)

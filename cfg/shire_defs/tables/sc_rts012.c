#include "cfe.h"
#include "cfe_tbl_filedef.h"
#include "sc_tbldefs.h"
#include "sc_platform_cfg.h"

typedef union
{
    uint16 buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable012_t;

SC_RtsTable012_t SC_Rts012 = {{ 0 }};

CFE_TBL_FILEDEF(SC_Rts012, SC.RTS_TBL012, SC RTS_TBL012, sc_rts012.tbl)

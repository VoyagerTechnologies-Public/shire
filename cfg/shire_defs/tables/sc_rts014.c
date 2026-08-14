#include "cfe.h"
#include "cfe_tbl_filedef.h"
#include "sc_tbldefs.h"
#include "sc_platform_cfg.h"

typedef union
{
    uint16 buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable014_t;

SC_RtsTable014_t SC_Rts014 = {{ 0 }};

CFE_TBL_FILEDEF(SC_Rts014, SC.RTS_TBL014, SC RTS_TBL014, sc_rts014.tbl)

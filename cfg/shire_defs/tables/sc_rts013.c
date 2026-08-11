#include "cfe.h"
#include "cfe_tbl_filedef.h"
#include "sc_tbldefs.h"
#include "sc_platform_cfg.h"

typedef union
{
    uint16 buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable013_t;

SC_RtsTable013_t SC_Rts013 = {{ 0 }};

CFE_TBL_FILEDEF(SC_Rts013, SC.RTS_TBL013, SC RTS_TBL013, sc_rts013.tbl)

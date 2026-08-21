#include "cfe.h"
#include "cfe_tbl_filedef.h"
#include "sc_tbldefs.h"
#include "sc_platform_cfg.h"

typedef union
{
    uint16 buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable015_t;

SC_RtsTable015_t SC_Rts015 = {{ 0 }};

CFE_TBL_FILEDEF(SC_Rts015, SC.RTS_TBL015, SC RTS_TBL015, sc_rts015.tbl)

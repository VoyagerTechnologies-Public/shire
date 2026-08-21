#include "cfe.h"
#include "cfe_tbl_filedef.h"
#include "sc_tbldefs.h"
#include "sc_platform_cfg.h"

typedef union
{
    uint16 buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable010_t;

SC_RtsTable010_t SC_Rts010 = {{ 0 }};

CFE_TBL_FILEDEF(SC_Rts010, SC.RTS_TBL010, SC RTS_TBL010, sc_rts010.tbl)

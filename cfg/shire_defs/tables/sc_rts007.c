#include "cfe.h"
#include "cfe_tbl_filedef.h"
#include "sc_tbldefs.h"
#include "sc_platform_cfg.h"

typedef union
{
    uint16 buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable007_t;

SC_RtsTable007_t SC_Rts007 = {{ 0 }};

CFE_TBL_FILEDEF(SC_Rts007, SC.RTS_TBL007, SC RTS_TBL007, sc_rts007.tbl)

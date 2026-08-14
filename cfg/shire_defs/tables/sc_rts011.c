/************************************************************************
 * NASA Docket No. GSC-18,924-1, and identified as “Core Flight
 * System (cFS) Stored Command Application version 3.1.1”
 *
 * Copyright (c) 2021 United States Government as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ************************************************************************/

/**
 * @file
 *   CFS Stored Command (SC) RTS Table 11
 */

#include "cfe.h"
#include "cfe_tbl_filedef.h"

#include "sc_tbldefs.h"      /* defines SC table headers */
#include "sc_platform_cfg.h" /* defines table buffer size */
#include "sc_msgdefs.h"      /* defines SC command code values */
#include "sc_msgids.h"       /* defines SC packet msg ID's */
#include "sc_msg.h"          /* defines SC message structures */

/* Specific includes */
#include "demo_msg.h"
#include "demo_msgids.h"
#include "lc_msg.h"
#include "lc_msgdefs.h"
#include "lc_msgids.h"

/* Note: Assumes SC_PLATFORM_ENABLE_HEADER_UPDATE is true */

/* Custom table structure, modify as needed to add desired commands */
typedef struct
{
    /* 1 - Disable Demo Component */
    SC_RtsEntryHeader_t hdr1;
    DEMO_NoArgs_cmd_t  cmd1;

    /* 2 - Enable Demo Component */
    SC_RtsEntryHeader_t hdr2;
    DEMO_NoArgs_cmd_t cmd2;

} SC_RtsStruct011_t;

/* Define the union to size the table correctly */
typedef union
{
    SC_RtsStruct011_t rts;
    uint16            buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable011_t;

/* Helper macro to get size of structure elements */
#define SC_MEMBER_SIZE(member) (sizeof(((SC_RtsStruct011_t *)0)->member))

/* Used designated initializers to be verbose, modify as needed/desired */
SC_RtsTable011_t SC_Rts011 = 
{
    /* 1 - Disable Demo Component */
    .rts.hdr1.WakeupCount = 1,
    .rts.cmd1.CmdHeader = CFE_MSG_CMD_HDR_INIT(DEMO_CMD_MID, SC_MEMBER_SIZE(cmd1), DEMO_DISABLE_CC, 0x00),

    /* 2 - Enable Demo Component */
    .rts.hdr2.WakeupCount = 10, /* 10 seconds */
    .rts.cmd2.CmdHeader = CFE_MSG_CMD_HDR_INIT(DEMO_CMD_MID, SC_MEMBER_SIZE(cmd2), DEMO_ENABLE_CC, 0x00),
};

/* Macro for table structure */
CFE_TBL_FILEDEF(SC_Rts011, SC.RTS_TBL011, Demo FDIR RTS_TBL011, sc_rts011.tbl)

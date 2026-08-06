/**
 * @file
 *   Define Radio specific subscription table
 */
#ifndef RADIO_SUB_TABLE_H
#define RADIO_SUB_TABLE_H

#include "cfe_msgids.h"
#include "cfe_platform_cfg.h"
#include "cfe_sb.h"

typedef struct
{
    CFE_SB_MsgId_t Stream;
    CFE_SB_Qos_t   Flags;
    uint16         BufLimit;
} RADIO_Sub_t;

typedef struct
{
    RADIO_Sub_t Subs[CFE_PLATFORM_SB_MAX_MSG_IDS];
} RADIO_Subs_t;

#endif

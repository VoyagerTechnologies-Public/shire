/************************************************************************
 * Radio Downlink Subscription Table
 ************************************************************************/

#include "cfe_tbl_filedef.h" /* Required to obtain the CFE_TBL_FILEDEF macro definition */
#include "cfe_sb_api_typedefs.h"
#include "cfe_msgids.h"

/*
** Add the proper include file for the message IDs below
*/
#include "radio_msgids.h"
#include "radio_sub_tbl.h"

RADIO_Subs_t RADIO_Subs = {.Subs = {
    /* Example subscriptions, replace/add as needed */
    {CFE_SB_MSGID_WRAP_VALUE(RADIO_HK_TLM_MID), {0, 0}, 4},
    {CFE_SB_MSGID_WRAP_VALUE(CFE_ES_HK_TLM_MID), {0, 0}, 4},
    {CFE_SB_MSGID_WRAP_VALUE(CFE_EVS_HK_TLM_MID), {0, 0}, 4},
    /* Add more as needed */
    {CFE_SB_MSGID_RESERVED, {0, 0}, 0}
}};

CFE_TBL_FILEDEF(RADIO_Subs, RADIO_APP.RADIO_Subs, Radio Downlink Sub Tbl, radio_sub.tbl)

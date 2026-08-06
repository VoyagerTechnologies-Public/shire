/*
** CryptoLib Stub Functions for Unit Testing
*/

#include "radio_unit_test_types.h"
#include <string.h>

/*
** Global stub variables for test control
*/
int32 UT_CRYPTO_TC_ProcessSecurity_ReturnValue = CRYPTO_LIB_SUCCESS;
int32 UT_CRYPTO_TM_ApplySecurity_ReturnValue = CRYPTO_LIB_SUCCESS;
bool  UT_CRYPTO_Enable_Stubs = true;

/*
** Stub for Crypto_TC_ProcessSecurity
*/
int32 Crypto_TC_ProcessSecurity(uint8* tc_frame, int32* frame_size, TC_t* tc_struct)
{
    if (!UT_CRYPTO_Enable_Stubs)
        return CRYPTO_LIB_ERROR;
        
    if (tc_struct != NULL && tc_frame != NULL && frame_size != NULL)
    {
        /* Simple stub - just copy input frame to output structure */
        tc_struct->tc_pdu_len = (uint16)*frame_size;
        if (*frame_size > 0 && *frame_size <= 1024)
        {
            memcpy(tc_struct->tc_pdu, tc_frame, *frame_size);
        }
    }
    
    return UT_CRYPTO_TC_ProcessSecurity_ReturnValue;
}

/*
** Stub for Crypto_TM_ApplySecurity
*/
int32 Crypto_TM_ApplySecurity(uint8* tm_frame, int32 frame_len)
{
    if (!UT_CRYPTO_Enable_Stubs)
        return CRYPTO_LIB_ERROR;
        
    /* Simple stub - no actual crypto processing */
    return UT_CRYPTO_TM_ApplySecurity_ReturnValue;
}

/*
** Stub for get_sa_interface_inmemory
*/
SaInterface get_sa_interface_inmemory(void)
{
    if (!UT_CRYPTO_Enable_Stubs)
        return NULL;
        
    /* Return a simple stub interface */
    static struct SaInterfaceStruct stub_interface = {0};
    return &stub_interface;
}

/*
 * Test helper: set the SA lookup hook in the in-memory interface
 * Tests may call this to provide a function that returns an operational SA.
 */
void UT_CRYPTO_Set_SaHook(int32_t (*hook)(uint8_t, uint8_t, uint8_t, uint8_t, SecurityAssociation_t **))
{
    SaInterface iface = get_sa_interface_inmemory();
    if (iface)
    {
        iface->sa_get_operational_sa_from_gvcid = hook;
    }
}

/* Default SA hook for tests: returns success and supplies a static SA */
int32_t UT_CRYPTO_Sa_Default(uint8_t gvcid, uint8_t scid, uint8_t vcid, uint8_t mapid, SecurityAssociation_t **sa)
{
    static SecurityAssociation_t default_sa = {0};
    if (sa)
    {
        *sa = &default_sa;
        return 0;
    }
    return -1;
}

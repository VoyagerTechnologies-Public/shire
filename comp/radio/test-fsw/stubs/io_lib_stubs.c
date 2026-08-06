/*
** IO_lib Stub Functions for Unit Testing
*/

#include "radio_unit_test_types.h"
#include <string.h>
#include <stdlib.h>

/*
** Global stub variables for test control
*/
int32 UT_TM_SYNC_LibInit_ReturnValue = 0;
int32 UT_TM_SDLP_InitChannel_ReturnValue = 0;
int32 UT_TM_SDLP_StartFrame_ReturnValue = 0;
int32 UT_TM_SDLP_AddPacket_ReturnValue = 0;
bool  UT_TM_SDLP_FrameHasData_ReturnValue = true;
int32 UT_TM_SYNC_Synchronize_ReturnValue = 1024;
bool  UT_IO_LIB_Enable_Stubs = true;

/*
** Stub for TM_SYNC_LibInit
*/
int32 TM_SYNC_LibInit(void)
{
    if (!UT_IO_LIB_Enable_Stubs)
        return -1;
        
    return UT_TM_SYNC_LibInit_ReturnValue;
}

/*
** Stub for TM_SDLP_InitChannel
*/
int32 TM_SDLP_InitChannel(uint8 channel_id)
{
    if (!UT_IO_LIB_Enable_Stubs)
        return -1;
        
    return UT_TM_SDLP_InitChannel_ReturnValue;
}

/*
** Stub for TM_SDLP_StartFrame
*/
int32 TM_SDLP_StartFrame(uint8 channel_id)
{
    if (!UT_IO_LIB_Enable_Stubs)
        return -1;
        
    return UT_TM_SDLP_StartFrame_ReturnValue;
}

/*
** Stub for TM_SDLP_AddPacket
*/
int32 TM_SDLP_AddPacket(uint8 channel_id, uint8* packet, uint16 packet_len)
{
    if (!UT_IO_LIB_Enable_Stubs)
        return -1;
        
    /* Simple stub - just validate parameters */
    if (packet == NULL || packet_len == 0)
        return -1;
        
    return UT_TM_SDLP_AddPacket_ReturnValue;
}

/*
** Stub for TM_SDLP_FrameHasData
*/
bool TM_SDLP_FrameHasData(uint8 channel_id)
{
    if (!UT_IO_LIB_Enable_Stubs)
        return false;
        
    return UT_TM_SDLP_FrameHasData_ReturnValue;
}

/*
** Stub for TM_SDLP_CompleteFrame
*/
uint8* TM_SDLP_CompleteFrame(uint8 channel_id, uint16* frame_len)
{
    /* Static buffer for frame data */
    static uint8_t stub_frame_buffer[2048];
    
    if (!UT_IO_LIB_Enable_Stubs)
        return NULL;
        
    if (frame_len != NULL)
    {
        *frame_len = 1024; /* Stub frame length */
        
        /* Fill with some test data */
        memset(stub_frame_buffer, 0xAA, sizeof(stub_frame_buffer));
    }
    
    return stub_frame_buffer;
}

/*
** Stub for TM_SYNC_Synchronize
*/
int32 TM_SYNC_Synchronize(uint8* cadu_buffer, char* asm_pattern, uint8 asm_size, uint16 frame_len, bool randomize)
{
    if (!UT_IO_LIB_Enable_Stubs)
        return -1;
        
    /* Simple stub - validate parameters and return frame length */
    if (cadu_buffer == NULL || asm_pattern == NULL)
        return -1;
        
    return UT_TM_SYNC_Synchronize_ReturnValue;
}

/* Generate a deterministic pseudo-random sequence into buffer for tests.
 * Keep it simple and deterministic so tests are repeatable.
 */
void IO_LIB_UTIL_GenPseudoRandomSeq(uint8_t *buf, uint8_t seed, uint8_t len)
{
    if (!buf) return;
    for (uint32_t i = 0; i < (uint32_t)len; ++i)
    {
        buf[i] = (uint8_t)(seed + (uint8_t)i);
    }
}

/* Idle packet helpers used by radio_app. In unit tests we don't need
 * full SDLP behavior, so provide no-op implementations that preserve
 * the expected symbol names for linking.
 */
void TM_SDLP_InitIdlePacket(void *IdlePacket, uint8_t *pattern, uint32_t frameSize, uint8_t repeat)
{
    (void)IdlePacket;
    (void)pattern;
    (void)frameSize;
    (void)repeat;
}

void TM_SDLP_AddIdlePacket(void *frame_info, void *IdlePacket)
{
    (void)frame_info;
    (void)IdlePacket;
}


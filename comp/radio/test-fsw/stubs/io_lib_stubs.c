/* IO_LIB generated-style stubs for radio unit testing. */

#include "utgenstub.h"

#include "io_lib_utils.h"
#include "tm_sdlp.h"
#include "tm_sync.h"

int32 TM_SYNC_LibInit(void)
{
    UT_GenStub_SetupReturnBuffer(TM_SYNC_LibInit, int32);
    UT_GenStub_Execute(TM_SYNC_LibInit, Basic, NULL);
    return UT_GenStub_GetReturnValue(TM_SYNC_LibInit, int32);
}

int32 TM_SDLP_InitChannel(TM_SDLP_FrameInfo_t *frame_info, uint8 *tf_buffer, uint8 *overflow_buffer,
                          TM_SDLP_GlobalConfig_t *global_config, TM_SDLP_ChannelConfig_t *channel_config)
{
    UT_GenStub_SetupReturnBuffer(TM_SDLP_InitChannel, int32);
    UT_GenStub_AddParam(TM_SDLP_InitChannel, TM_SDLP_FrameInfo_t *, frame_info);
    UT_GenStub_AddParam(TM_SDLP_InitChannel, uint8 *, tf_buffer);
    UT_GenStub_AddParam(TM_SDLP_InitChannel, uint8 *, overflow_buffer);
    UT_GenStub_AddParam(TM_SDLP_InitChannel, TM_SDLP_GlobalConfig_t *, global_config);
    UT_GenStub_AddParam(TM_SDLP_InitChannel, TM_SDLP_ChannelConfig_t *, channel_config);
    UT_GenStub_Execute(TM_SDLP_InitChannel, Basic, NULL);
    return UT_GenStub_GetReturnValue(TM_SDLP_InitChannel, int32);
}

int32 TM_SDLP_StartFrame(TM_SDLP_FrameInfo_t *frame_info)
{
    UT_GenStub_SetupReturnBuffer(TM_SDLP_StartFrame, int32);
    UT_GenStub_AddParam(TM_SDLP_StartFrame, TM_SDLP_FrameInfo_t *, frame_info);
    UT_GenStub_Execute(TM_SDLP_StartFrame, Basic, NULL);
    return UT_GenStub_GetReturnValue(TM_SDLP_StartFrame, int32);
}

int32 TM_SDLP_AddPacket(TM_SDLP_FrameInfo_t *frame_info, CFE_MSG_Message_t *packet)
{
    UT_GenStub_SetupReturnBuffer(TM_SDLP_AddPacket, int32);
    UT_GenStub_AddParam(TM_SDLP_AddPacket, TM_SDLP_FrameInfo_t *, frame_info);
    UT_GenStub_AddParam(TM_SDLP_AddPacket, CFE_MSG_Message_t *, packet);
    UT_GenStub_Execute(TM_SDLP_AddPacket, Basic, NULL);
    return UT_GenStub_GetReturnValue(TM_SDLP_AddPacket, int32);
}

int32 TM_SDLP_FrameHasData(TM_SDLP_FrameInfo_t *frame_info)
{
    UT_GenStub_SetupReturnBuffer(TM_SDLP_FrameHasData, int32);
    UT_GenStub_AddParam(TM_SDLP_FrameHasData, TM_SDLP_FrameInfo_t *, frame_info);
    UT_GenStub_Execute(TM_SDLP_FrameHasData, Basic, NULL);
    return UT_GenStub_GetReturnValue(TM_SDLP_FrameHasData, int32);
}

int32 TM_SDLP_CompleteFrame(TM_SDLP_FrameInfo_t *frame_info, uint8 *master_channel_count, uint8 *ocf)
{
    UT_GenStub_SetupReturnBuffer(TM_SDLP_CompleteFrame, int32);
    UT_GenStub_AddParam(TM_SDLP_CompleteFrame, TM_SDLP_FrameInfo_t *, frame_info);
    UT_GenStub_AddParam(TM_SDLP_CompleteFrame, uint8 *, master_channel_count);
    UT_GenStub_AddParam(TM_SDLP_CompleteFrame, uint8 *, ocf);
    UT_GenStub_Execute(TM_SDLP_CompleteFrame, Basic, NULL);
    return UT_GenStub_GetReturnValue(TM_SDLP_CompleteFrame, int32);
}

int32 TM_SYNC_Synchronize(uint8 *buffer, char *asm_pattern, uint8 asm_size, uint16 frame_length, bool randomize)
{
    UT_GenStub_SetupReturnBuffer(TM_SYNC_Synchronize, int32);
    UT_GenStub_AddParam(TM_SYNC_Synchronize, uint8 *, buffer);
    UT_GenStub_AddParam(TM_SYNC_Synchronize, char *, asm_pattern);
    UT_GenStub_AddParam(TM_SYNC_Synchronize, uint8, asm_size);
    UT_GenStub_AddParam(TM_SYNC_Synchronize, uint16, frame_length);
    UT_GenStub_AddParam(TM_SYNC_Synchronize, bool, randomize);
    UT_GenStub_Execute(TM_SYNC_Synchronize, Basic, NULL);
    return UT_GenStub_GetReturnValue(TM_SYNC_Synchronize, int32);
}

int32 IO_LIB_UTIL_GenPseudoRandomSeq(uint8 *sequence, uint8 polynomial, uint8 seed)
{
    UT_GenStub_SetupReturnBuffer(IO_LIB_UTIL_GenPseudoRandomSeq, int32);
    UT_GenStub_AddParam(IO_LIB_UTIL_GenPseudoRandomSeq, uint8 *, sequence);
    UT_GenStub_AddParam(IO_LIB_UTIL_GenPseudoRandomSeq, uint8, polynomial);
    UT_GenStub_AddParam(IO_LIB_UTIL_GenPseudoRandomSeq, uint8, seed);
    UT_GenStub_Execute(IO_LIB_UTIL_GenPseudoRandomSeq, Basic, NULL);
    return UT_GenStub_GetReturnValue(IO_LIB_UTIL_GenPseudoRandomSeq, int32);
}

int32 TM_SDLP_InitIdlePacket(CFE_MSG_Message_t *idle_packet, uint8 *pattern, uint16 buffer_length,
                             uint32 pattern_bit_length)
{
    UT_GenStub_SetupReturnBuffer(TM_SDLP_InitIdlePacket, int32);
    UT_GenStub_AddParam(TM_SDLP_InitIdlePacket, CFE_MSG_Message_t *, idle_packet);
    UT_GenStub_AddParam(TM_SDLP_InitIdlePacket, uint8 *, pattern);
    UT_GenStub_AddParam(TM_SDLP_InitIdlePacket, uint16, buffer_length);
    UT_GenStub_AddParam(TM_SDLP_InitIdlePacket, uint32, pattern_bit_length);
    UT_GenStub_Execute(TM_SDLP_InitIdlePacket, Basic, NULL);
    return UT_GenStub_GetReturnValue(TM_SDLP_InitIdlePacket, int32);
}

int32 TM_SDLP_AddIdlePacket(TM_SDLP_FrameInfo_t *frame_info, CFE_MSG_Message_t *idle_packet)
{
    UT_GenStub_SetupReturnBuffer(TM_SDLP_AddIdlePacket, int32);
    UT_GenStub_AddParam(TM_SDLP_AddIdlePacket, TM_SDLP_FrameInfo_t *, frame_info);
    UT_GenStub_AddParam(TM_SDLP_AddIdlePacket, CFE_MSG_Message_t *, idle_packet);
    UT_GenStub_Execute(TM_SDLP_AddIdlePacket, Basic, NULL);
    return UT_GenStub_GetReturnValue(TM_SDLP_AddIdlePacket, int32);
}

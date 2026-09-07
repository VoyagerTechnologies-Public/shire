/* SHIRE-owned behavioral coverage for IO_LIB's deployed, public helpers. */

#include "io_lib_utils.h"
#include "tc_sync.h"
#include "tctf.h"
#include "tm_sync.h"
#include "tmtf.h"
#include "trans_select.h"

#include "utassert.h"
#include "utstubs.h"
#include "uttest.h"

#include <string.h>

static void Test_Setup(void)
{
    UT_ResetState(0);
}

static void Test_UtilitiesAndSync(void)
{
    uint8  sequence[32] = {0};
    uint8  original[40];
    uint8  buffer[40];
    uint8  cltu[18] = {0xeb, 0x90, 1, 2, 3, 4, 5, 6, 7, 0, 0xc5, 0xc5, 0xc5, 0xc5, 0xc5, 0xc5, 0xc5, 0x79};
    char   asm_string[] = TM_SYNC_ASM_STR;
    uint16 tf_offset;
    uint16 cltu_offset;

    memset(original, 0x5a, sizeof(original));
    memcpy(buffer, original, sizeof(buffer));
    UtAssert_INT32_EQ(IO_LIB_UTIL_GenPseudoRandomSeq(sequence, 0xa9, 0xff), 0);
    UtAssert_INT32_EQ(IO_LIB_UTIL_PseudoRandomize(buffer, sizeof(buffer), sequence), 0);
    UtAssert_True(memcmp(buffer, original, sizeof(buffer)) != 0, "randomizer changes data");
    IO_LIB_UTIL_PseudoRandomize(buffer, sizeof(buffer), sequence);
    UtAssert_MemCmp(buffer, original, sizeof(buffer), "randomizer is symmetric");

    UtAssert_INT32_EQ(TM_SYNC_LibInit(), TM_SYNC_SUCCESS);
    UtAssert_INT32_EQ(TM_SYNC_Synchronize(NULL, asm_string, 4, 4, false), TM_SYNC_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SYNC_Synchronize(buffer, NULL, 4, 4, false), TM_SYNC_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SYNC_Synchronize(buffer, asm_string, 3, 4, false), TM_SYNC_INVALID_ASM_SIZE);
    memset(buffer, 0, sizeof(buffer));
    UtAssert_INT32_EQ(TM_SYNC_Synchronize(buffer, asm_string, 4, 4, false), 8);
    UtAssert_UINT8_EQ(buffer[0], 0x1a);
    UtAssert_INT32_EQ(TM_SYNC_Synchronize(buffer, asm_string, 4, 4, true), 8);
    UtAssert_INT32_EQ(TM_SYNC_PseudoRandomize(buffer, 4), TM_SYNC_SUCCESS);

    UtAssert_INT32_EQ(TC_SYNC_LibInit(), TC_SYNC_SUCCESS);
    tf_offset = 0;
    cltu_offset = 0;
    UtAssert_INT32_EQ(TC_SYNC_CheckStartSeq(NULL, &cltu_offset), TC_SYNC_INVALID_POINTER);
    UtAssert_INT32_EQ(TC_SYNC_CheckStartSeq(cltu, NULL), TC_SYNC_INVALID_POINTER);
    cltu[0] = 0;
    UtAssert_INT32_EQ(TC_SYNC_CheckStartSeq(cltu, &cltu_offset), TC_SYNC_INVALID_CLTU);
    cltu[0] = 0xeb;
    UtAssert_INT32_EQ(TC_SYNC_CheckStartSeq(cltu, &cltu_offset), TC_SYNC_SUCCESS);
    UtAssert_UINT16_EQ(cltu_offset, 2);

    UtAssert_INT32_EQ(TC_SYNC_GetCodeBlockData(NULL, cltu + 2, &tf_offset, &cltu_offset, 20, 18),
                      TC_SYNC_INVALID_POINTER);
    UtAssert_INT32_EQ(TC_SYNC_GetCodeBlockData(buffer, cltu + 2, NULL, &cltu_offset, 20, 18),
                      TC_SYNC_INVALID_POINTER);
    cltu_offset = 11;
    UtAssert_INT32_EQ(TC_SYNC_GetCodeBlockData(buffer, cltu + 2, &tf_offset, &cltu_offset, 20, 18),
                      TC_SYNC_INVALID_CLTU);
    cltu_offset = 2;
    tf_offset = 0;
    UtAssert_INT32_EQ(TC_SYNC_GetCodeBlockData(buffer, cltu + 2, &tf_offset, &cltu_offset, 6, 18),
                      TC_SYNC_INVALID_LENGTH);
    UtAssert_INT32_EQ(TC_SYNC_GetCodeBlockData(buffer, cltu + 10, &tf_offset, &cltu_offset, 20, 18),
                      TC_SYNC_FOUND_TAIL_SEQ);
    tf_offset = 0;
    cltu_offset = 2;
    UtAssert_INT32_EQ(TC_SYNC_GetCodeBlockData(buffer, cltu + 2, &tf_offset, &cltu_offset, 20, 18), TC_SYNC_SUCCESS);
    UtAssert_UINT16_EQ(tf_offset, 7);

    UtAssert_INT32_EQ(TC_SYNC_GetTransferFrame(NULL, cltu, 20, sizeof(cltu), false), TC_SYNC_INVALID_POINTER);
    cltu[0] = 0;
    UtAssert_INT32_EQ(TC_SYNC_GetTransferFrame(buffer, cltu, 20, sizeof(cltu), false), TC_SYNC_INVALID_CLTU);
    cltu[0] = 0xeb;
    UtAssert_INT32_EQ(TC_SYNC_GetTransferFrame(buffer, cltu, 20, sizeof(cltu), false), 7);
    UtAssert_INT32_EQ(TC_SYNC_GetTransferFrame(buffer, cltu, 20, sizeof(cltu), true), 7);
    UtAssert_INT32_EQ(TC_SYNC_DeRandomizeFrame(buffer, 7), TC_SYNC_SUCCESS);
}

static void Test_Tctf(void)
{
    uint8                 raw[32] = {0};
    uint8                 output[32] = {0};
    TCTF_Hdr_t           *tf = (TCTF_Hdr_t *)raw;
    TCTF_ChannelService_t svc = {0};

    tf->Octet[0] = (1U << 6) | (0x155U >> 8);
    tf->Octet[1] = 0x55;
    tf->Octet[2] = (3U << 2);
    tf->Octet[3] = 11; /* 12-byte frame */
    tf->Sequence = 9;
    tf->SegHdr = (TCTF_FIRST_SEGMENT << 6) | 7;
    raw[6] = 0xaa;

    UtAssert_UINT16_EQ(TCTF_GetVersion(NULL), 0);
    UtAssert_UINT16_EQ(TCTF_GetBypassFlag(NULL), 0);
    UtAssert_UINT16_EQ(TCTF_GetCtlCmdFlag(NULL), 0);
    UtAssert_UINT16_EQ(TCTF_GetScId(NULL), 0);
    UtAssert_UINT16_EQ(TCTF_GetVcId(NULL), 0);
    UtAssert_UINT16_EQ(TCTF_GetLength(NULL), 0);
    UtAssert_UINT8_EQ(TCTF_GetSeqNum(NULL), 0);
    UtAssert_UINT16_EQ(TCTF_GetVersion(tf), 1);
    UtAssert_UINT16_EQ(TCTF_GetScId(tf), 0x155);
    UtAssert_UINT16_EQ(TCTF_GetVcId(tf), 3);
    UtAssert_UINT16_EQ(TCTF_GetLength(tf), 12);
    UtAssert_UINT8_EQ(TCTF_GetSeqNum(tf), 9);
    UtAssert_UINT16_EQ(TCTF_GetSegHdrSeqFlags(tf), TCTF_FIRST_SEGMENT);
    UtAssert_UINT16_EQ(TCTF_GetSegHdrMapId(tf), 7);
    UtAssert_UINT16_EQ(TCTF_GetSegHdrSeqFlags(NULL), TCTF_NO_SEGHDR);

    svc.PacketVersionNumber = 1;
    svc.SpacecraftId = 0x155;
    svc.VirtualChannelId = 3;
    svc.MapId = 7;
    svc.Service = TCTF_SERVICE_MAPP;
    svc.HasSegHdr = true;
    svc.HasFrameErrCtl = true;
    UtAssert_UINT16_EQ(TCTF_GetPayloadLength(tf, &svc), 4);
    UtAssert_UINT16_EQ(TCTF_GetPayloadLength(NULL, &svc), 0);
    UtAssert_UINT16_EQ(TCTF_CopyData(output, tf, &svc), 4);
    UtAssert_UINT16_EQ(TCTF_CopyData(NULL, tf, &svc), 0);
    UtAssert_True(TCTF_IsValidTf(tf, &svc), "matching frame is valid");
    svc.MapId++;
    UtAssert_True(!TCTF_IsValidTf(tf, &svc), "bad map is rejected");
    svc.Service = TCTF_SERVICE_VCP;
    svc.VirtualChannelId++;
    UtAssert_True(!TCTF_IsValidTf(tf, &svc), "bad VC is rejected");
    svc.Service = TCTF_SERVICE_MCF;
    svc.SpacecraftId++;
    UtAssert_True(!TCTF_IsValidTf(tf, &svc), "bad master channel is rejected");
    UtAssert_True(!TCTF_IsValidTf(NULL, &svc), "null frame is rejected");
    tf->Octet[0] = 0x10; /* control command without bypass */
    UtAssert_True(!TCTF_IsValidTf(tf, &svc), "invalid control flags are rejected");
    UtAssert_UINT16_EQ(TCTF_GetSegHdrMapId(tf), TCTF_NO_SEGHDR);
}

static void Test_Tmtf(void)
{
    uint8          raw[128] = {0};
    uint8          data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TMTF_PriHdr_t *tf = (TMTF_PriHdr_t *)raw;

    UtAssert_INT32_EQ(TMTF_LibInit(), TMTF_SUCCESS);
    UtAssert_INT32_EQ(TMTF_SetVersion(NULL, 1), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_SetScId(NULL, 1), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_SetVcId(NULL, 1), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_SetOcfFlag(NULL, true), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_GetMcId(NULL), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_GetGlobalVcId(NULL), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_SetMcFrameCount(NULL, 1), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_SetVcFrameCount(NULL, 1), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_IncrVcFrameCount(NULL), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_SetSecHdrFlag(NULL, true), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_SetSyncFlag(NULL, true), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_SetPacketOrderFlag(NULL, true), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_SetSegLengthId(NULL, 1), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_SetFirstHdrPtr(NULL, 1), TMTF_INVALID_POINTER);

    UtAssert_INT32_EQ(TMTF_SetVersion(tf, 1), TMTF_SUCCESS);
    UtAssert_INT32_EQ(TMTF_SetScId(tf, 0x155), TMTF_SUCCESS);
    UtAssert_INT32_EQ(TMTF_SetVcId(tf, 3), TMTF_SUCCESS);
    UtAssert_INT32_EQ(TMTF_SetOcfFlag(tf, true), TMTF_SUCCESS);
    UtAssert_True(TMTF_GetMcId(tf) >= 0, "master channel id is readable");
    UtAssert_True(TMTF_GetGlobalVcId(tf) >= 0, "global VC id is readable");
    UtAssert_INT32_EQ(TMTF_SetMcFrameCount(tf, 255), TMTF_SUCCESS);
    UtAssert_INT32_EQ(TMTF_SetVcFrameCount(tf, 255), TMTF_SUCCESS);
    UtAssert_INT32_EQ(TMTF_IncrVcFrameCount(tf), TMTF_SUCCESS);
    UtAssert_UINT8_EQ(tf->VcFrameCount, 0);
    UtAssert_INT32_EQ(TMTF_SetSecHdrFlag(tf, true), TMTF_SUCCESS);
    UtAssert_INT32_EQ(TMTF_SetSyncFlag(tf, false), TMTF_SUCCESS);
    UtAssert_INT32_EQ(TMTF_SetSyncFlag(tf, true), TMTF_SUCCESS);
    UtAssert_INT32_EQ(TMTF_SetPacketOrderFlag(tf, true), TMTF_SUCCESS);
    UtAssert_INT32_EQ(TMTF_SetSegLengthId(tf, 2), TMTF_SUCCESS);
    UtAssert_INT32_EQ(TMTF_SetFirstHdrPtr(tf, 0x123), TMTF_SUCCESS);

    UtAssert_INT32_EQ(TMTF_SetSecHdrLength(NULL, 1), TMTF_INVALID_POINTER);
    TMTF_SetSecHdrFlag(tf, false);
    UtAssert_INT32_EQ(TMTF_SetSecHdrLength(tf, 1), TMTF_INVALID_SECHDR);
    TMTF_SetSecHdrFlag(tf, true);
    UtAssert_INT32_EQ(TMTF_SetSecHdrLength(tf, 0), TMTF_INVALID_LENGTH);
    UtAssert_INT32_EQ(TMTF_SetSecHdrLength(tf, TMTF_SECHDR_MAX_LENGTH + 1), TMTF_INVALID_LENGTH);
    UtAssert_INT32_EQ(TMTF_SetSecHdrLength(tf, 4), TMTF_SUCCESS);
    UtAssert_INT32_EQ(TMTF_SetSecHdrData(NULL, data, 4), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_SetSecHdrData(tf, NULL, 4), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_SetSecHdrData(tf, data, 5), TMTF_INVALID_LENGTH);
    UtAssert_INT32_EQ(TMTF_SetSecHdrData(tf, data, 4), TMTF_SUCCESS);

    TMTF_SetOcfFlag(tf, false);
    UtAssert_INT32_EQ(TMTF_SetOcf(tf, data, 32), TMTF_ERROR);
    TMTF_SetOcfFlag(tf, true);
    UtAssert_INT32_EQ(TMTF_SetOcf(NULL, data, 32), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_SetOcf(tf, data, 32), TMTF_SUCCESS);
    UtAssert_INT32_EQ(TMTF_UpdateErrCtrlField(NULL, 32), TMTF_INVALID_POINTER);
    UtAssert_INT32_EQ(TMTF_UpdateErrCtrlField(tf, 2), TMTF_INVALID_LENGTH);
    UtAssert_INT32_EQ(TMTF_UpdateErrCtrlField(tf, 32), TMTF_SUCCESS);
}

static void Test_Select(void)
{
    IO_TransSelect_t set;

    UtAssert_INT32_EQ(IO_TransSelectClear(NULL), IO_TRANS_SELECT_NULL_SET_ERR);
    UtAssert_INT32_EQ(IO_TransSelectAddFd(NULL, 0), IO_TRANS_SELECT_NULL_SET_ERR);
    UtAssert_INT32_EQ(IO_TransSelectClear(&set), IO_TRANS_SELECT_NO_ERROR);
    UtAssert_INT32_EQ(IO_TransSelectAddFd(&set, -1), IO_TRANS_SELECT_BAD_FD_ERR);
    UtAssert_INT32_EQ(IO_TransSelectInput(&set, 0), IO_TRANS_SELECT_EMPTY_SET_ERR);
    UtAssert_INT32_EQ(IO_TransSelectAddFd(&set, 0), IO_TRANS_SELECT_NO_ERROR);
    UtAssert_INT32_EQ(IO_TransSelectAddFd(&set, 2), IO_TRANS_SELECT_NO_ERROR);
    UtAssert_INT32_EQ(IO_TransSelectFdInFull(&set, 2), 1);
    UtAssert_INT32_EQ(IO_TransSelectFdInFull(NULL, 2), IO_TRANS_SELECT_NULL_SET_ERR);
    UtAssert_INT32_EQ(IO_TransSelectFdInActive(&set, -1), IO_TRANS_SELECT_BAD_FD_ERR);
    UtAssert_INT32_EQ(IO_TransSelectInput(&set, -2), IO_TRANS_SELECT_INVALID_TIMEOUT_ERR);
    UtAssert_True(IO_TransSelectInput(&set, 0) >= 0, "nonblocking input select");
    UtAssert_True(IO_TransSelectOutput(&set, 0) >= 0, "nonblocking output select");
    UtAssert_INT32_EQ(IO_TransSelectRemoveFd(&set, 2), IO_TRANS_SELECT_NO_ERROR);
    UtAssert_INT32_EQ(IO_TransSelectRemoveFd(&set, 0), IO_TRANS_SELECT_NO_ERROR);
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_UtilitiesAndSync, Test_Setup, NULL, "IO utility and synchronization services");
    UtTest_Add(Test_Tctf, Test_Setup, NULL, "telecommand transfer frames");
    UtTest_Add(Test_Tmtf, Test_Setup, NULL, "telemetry transfer frames");
    UtTest_Add(Test_Select, Test_Setup, NULL, "transport descriptor selection");
}

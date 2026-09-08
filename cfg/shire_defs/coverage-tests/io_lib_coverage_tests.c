/* SHIRE-owned behavioral coverage for IO_LIB's deployed, public helpers. */

#include "io_lib_utils.h"
#include "cop1.h"
#include "crypto.h"
#include "tc_sync.h"
#include "tctf.h"
#include "tm_sdlp.h"
#include "tm_sync.h"
#include "tmtf.h"
#include "trans_rs422.h"
#include "trans_select.h"
#include "trans_udp.h"

#include "utassert.h"
#include "utstubs.h"
#include "uttest.h"

#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

speed_t IO_TransRS422GetBaudRateMacro(int32 bps);

static SaInterfaceStruct     TestSaInterface;
static SecurityAssociation_t TestSa;
static int32                 TestSaStatus;
static int32                 TestSecurityHeaderLength;
static int32                 TestSecurityTrailerLength;
static bool                  TestReturnNullSa;

SaInterface sa_if;

static int32 Test_GetOperationalSa(uint8 tfvn, uint16 scid, uint16 vcid, uint8 mapid,
                                   SecurityAssociation_t **saPtr)
{
    UtAssert_UINT32_EQ(tfvn, 0);
    UtAssert_UINT32_EQ(scid, 3);
    UtAssert_UINT32_EQ(vcid, 1);
    UtAssert_UINT32_EQ(mapid, 0);

    if (TestSaStatus == CRYPTO_LIB_SUCCESS && !TestReturnNullSa)
    {
        *saPtr = &TestSa;
    }
    return TestSaStatus;
}

int32_t Crypto_Get_Security_Header_Length(SecurityAssociation_t *saPtr)
{
    UtAssert_ADDRESS_EQ(saPtr, &TestSa);
    return TestSecurityHeaderLength;
}

int32_t Crypto_Get_Security_Trailer_Length(SecurityAssociation_t *saPtr)
{
    UtAssert_ADDRESS_EQ(saPtr, &TestSa);
    return TestSecurityTrailerLength;
}

static void Test_Setup(void)
{
    UT_ResetState(0);
    memset(&TestSaInterface, 0, sizeof(TestSaInterface));
    memset(&TestSa, 0, sizeof(TestSa));
    TestSaStatus              = CRYPTO_LIB_SUCCESS;
    TestSecurityHeaderLength = 0;
    TestSecurityTrailerLength = 0;
    TestReturnNullSa          = false;
    TestSaInterface.sa_get_operational_sa_from_gvcid = Test_GetOperationalSa;
    sa_if = &TestSaInterface;
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

static void InitCop1Frame(uint8 *raw, uint8 flags, uint8 sequence)
{
    memset(raw, 0, 32);
    raw[0] = flags;
    raw[1] = 3;         /* spacecraft ID */
    raw[2] = (1U << 2); /* virtual channel ID */
    raw[3] = 11;        /* encoded length: 12 bytes minus one */
    raw[4] = sequence;
}

static TCTF_ChannelService_t GetCop1Service(void)
{
    TCTF_ChannelService_t service = {0};

    service.Service             = TCTF_SERVICE_VCP;
    service.PacketVersionNumber = 0;
    service.SpacecraftId        = 3;
    service.VirtualChannelId    = 1;
    service.HasSegHdr           = false;
    service.HasFrameErrCtl      = false;
    return service;
}

static void Test_Cop1Clcw(void)
{
    COP1_Clcw_t clcw = {0};

    UtAssert_INT32_EQ(COP1_InitClcw(NULL, 1), COP1_BADINPUT_ERR);
    UtAssert_INT32_EQ(COP1_InitClcw(&clcw, 5), COP1_SUCCESS);
    UtAssert_UINT16_EQ(COP1_GetClcwCtrlWordType(&clcw), 0);
    UtAssert_UINT16_EQ(COP1_GetClcwVersion(&clcw), 0);
    UtAssert_UINT16_EQ(COP1_GetClcwCopEffect(&clcw), 1);
    UtAssert_UINT16_EQ(COP1_GetClcwVcId(&clcw), 5);

    COP1_SetClcwStatus(&clcw, 6);
    UtAssert_UINT16_EQ(COP1_GetClcwStatus(&clcw), 6);
    COP1_SetClcwNoRf(&clcw, true);
    COP1_SetClcwNoBitlock(&clcw, true);
    UtAssert_True(COP1_GetClcwNoRf(&clcw), "no-RF flag is set");
    UtAssert_True(COP1_GetClcwNoBitlock(&clcw), "no-bitlock flag is set");
    COP1_SetClcwNoRf(&clcw, false);
    COP1_SetClcwNoBitlock(&clcw, false);
    UtAssert_True(!COP1_GetClcwNoRf(&clcw), "no-RF flag is cleared");
    UtAssert_True(!COP1_GetClcwNoBitlock(&clcw), "no-bitlock flag is cleared");

    clcw.Flags  = 0x3e;
    clcw.Report = 42;
    UtAssert_True(COP1_GetClcwLockout(&clcw), "lockout flag is readable");
    UtAssert_True(COP1_GetClcwWait(&clcw), "wait flag is readable");
    UtAssert_True(COP1_GetClcwRetransmit(&clcw), "retransmit flag is readable");
    UtAssert_UINT16_EQ(COP1_GetClcwFarmbCtr(&clcw), 3);
    UtAssert_UINT8_EQ(COP1_GetClcwReport(&clcw), 42);

    COP1_SetClcwStatus(NULL, 1);
    COP1_SetClcwNoRf(NULL, true);
    COP1_SetClcwNoBitlock(NULL, true);
    UtAssert_UINT16_EQ(COP1_GetClcwCtrlWordType(NULL), 0);
    UtAssert_UINT16_EQ(COP1_GetClcwVersion(NULL), 0);
    UtAssert_UINT16_EQ(COP1_GetClcwStatus(NULL), 0);
    UtAssert_UINT16_EQ(COP1_GetClcwCopEffect(NULL), 0);
    UtAssert_UINT16_EQ(COP1_GetClcwVcId(NULL), 0);
    UtAssert_True(!COP1_GetClcwNoRf(NULL), "null CLCW has no flags");
    UtAssert_True(!COP1_GetClcwNoBitlock(NULL), "null CLCW has no flags");
    UtAssert_True(!COP1_GetClcwLockout(NULL), "null CLCW has no flags");
    UtAssert_True(!COP1_GetClcwWait(NULL), "null CLCW has no flags");
    UtAssert_True(!COP1_GetClcwRetransmit(NULL), "null CLCW has no flags");
    UtAssert_UINT16_EQ(COP1_GetClcwFarmbCtr(NULL), 0);
    UtAssert_UINT8_EQ(COP1_GetClcwReport(NULL), 0);
}

static void Test_Cop1Frames(void)
{
    uint8                 raw[32];
    uint8                 output[32] = {0};
    TCTF_Hdr_t           *tf = (TCTF_Hdr_t *)raw;
    TCTF_ChannelService_t service = GetCop1Service();
    COP1_Clcw_t           clcw = {0};

    UtAssert_INT32_EQ(COP1_ProcessFrame(output, NULL, tf, &service), COP1_BADINPUT_ERR);
    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, NULL, &service), COP1_BADINPUT_ERR);

    InitCop1Frame(raw, 0, 10);
    clcw.Report = 10;
    raw[1] = 4;
    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, tf, &service), COP1_INVALID_TF_ERR);
    raw[1] = 3;

    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, tf, &service), 7);
    UtAssert_UINT8_EQ(clcw.Report, 11);
    UtAssert_True(!COP1_GetClcwWait(&clcw), "accepted frame clears wait");
    UtAssert_True(!COP1_GetClcwRetransmit(&clcw), "accepted frame clears retransmit");

    InitCop1Frame(raw, 0, 11);
    UtAssert_INT32_EQ(COP1_ProcessFrame(NULL, &clcw, tf, &service), COP1_BADINPUT_ERR);
    UtAssert_True(COP1_GetClcwWait(&clcw), "missing destination enters wait");
    UtAssert_True(COP1_GetClcwRetransmit(&clcw), "missing destination requests retransmit");

    InitCop1Frame(raw, 0, 12);
    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, tf, &service), COP1_FARM1_ERR);
    UtAssert_True(COP1_GetClcwRetransmit(&clcw), "positive-window frame requests retransmit");

    InitCop1Frame(raw, 0, 10);
    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, tf, &service), COP1_FARM1_ERR);

    clcw.Report = 100;
    InitCop1Frame(raw, 0, 200);
    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, tf, &service), COP1_FARM1_ERR);
    UtAssert_True(COP1_GetClcwLockout(&clcw), "out-of-window frame enters lockout");
    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, tf, &service), COP1_FARM1_ERR);

    /* Unlock bypass-control frame. */
    InitCop1Frame(raw, 0x30, 0);
    raw[5] = 0x00;
    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, tf, &service), COP1_SUCCESS);
    UtAssert_True(!COP1_GetClcwLockout(&clcw), "UNLOCK clears lockout");

    /* SET-V(R) bypass-control frame, both accepted and rejected while locked. */
    raw[5] = 0x82;
    raw[6] = 0x00;
    raw[7] = 250;
    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, tf, &service), COP1_SUCCESS);
    UtAssert_UINT8_EQ(clcw.Report, 250);
    clcw.Flags |= 0x20;
    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, tf, &service), COP1_FARM1_ERR);
    clcw.Flags &= (uint8)~0x20;

    raw[5] = 0x55;
    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, tf, &service), COP1_FARM1_ERR);

    /* Bypass data is copied and advances only FARM-B. */
    InitCop1Frame(raw, 0x20, 0);
    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, tf, &service), 7);

    /* Exercise both halves of sequence windows that wrap at 255. */
    clcw.Report = 250;
    InitCop1Frame(raw, 0, 5);
    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, tf, &service), COP1_FARM1_ERR);
    clcw.Report = 5;
    InitCop1Frame(raw, 0, 250);
    UtAssert_INT32_EQ(COP1_ProcessFrame(output, &clcw, tf, &service), COP1_FARM1_ERR);
}

static int32 InitSdlp(TM_SDLP_FrameInfo_t *info, TM_SDLP_GlobalConfig_t *global,
                      TM_SDLP_ChannelConfig_t *channel, uint8 *frame, uint16 frameLength,
                      uint8 *overflow, uint16 overflowLength)
{
    memset(info, 0, sizeof(*info));
    memset(global, 0, sizeof(*global));
    memset(channel, 0, sizeof(*channel));
    memset(frame, 0, frameLength);
    memset(overflow, 0, overflowLength);
    global->scId          = 3;
    global->frameLength   = frameLength;
    channel->vcId         = 1;
    channel->overflowSize = overflowLength;
    return TM_SDLP_InitChannel(info, frame, overflow, global, channel);
}

static void Test_TmSdlpInitialization(void)
{
    TM_SDLP_FrameInfo_t     info = {0};
    TM_SDLP_GlobalConfig_t  global = {0};
    TM_SDLP_ChannelConfig_t channel = {0};
    uint8                   frame[96] = {0};
    uint8                   overflow[96] = {0};
    uint8                   idle[32] = {0};
    uint8                   pattern[2] = {0xa5, 0x80};

    UtAssert_INT32_EQ(TM_SDLP_InitIdlePacket(NULL, pattern, sizeof(idle), 9), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_InitIdlePacket((CFE_MSG_Message_t *)idle, NULL, sizeof(idle), 9),
                      TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_InitIdlePacket((CFE_MSG_Message_t *)idle, pattern, sizeof(idle), 0),
                      TM_SDLP_INVALID_LENGTH);
    UT_SetDefaultReturnValue(UT_KEY(CFE_SB_GetUserDataLength), 8);
    UtAssert_INT32_EQ(TM_SDLP_InitIdlePacket((CFE_MSG_Message_t *)idle, pattern, sizeof(idle), 9),
                      TM_SDLP_SUCCESS);

    global.scId = 3;
    global.frameLength = sizeof(frame);
    channel.vcId = 1;
    channel.overflowSize = sizeof(overflow);
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(NULL, frame, overflow, &global, &channel), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, NULL, overflow, &global, &channel), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, NULL, &global, &channel), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, NULL, &channel), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, NULL), TM_SDLP_INVALID_POINTER);

    channel.secHdrLength = 1;
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, &channel), TM_SDLP_INVALID_LENGTH);
    channel.fshFlag = true;
    channel.secHdrLength = 0;
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, &channel), TM_SDLP_INVALID_LENGTH);
    channel.secHdrLength = TMTF_SECHDR_MAX_LENGTH + 1;
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, &channel), TM_SDLP_INVALID_LENGTH);
    channel.fshFlag = false;
    channel.secHdrLength = 0;

    sa_if = NULL;
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, &channel), CRYPTO_LIB_ERR_NO_INIT);
    sa_if = &TestSaInterface;
    TestSaInterface.sa_get_operational_sa_from_gvcid = NULL;
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, &channel), CRYPTO_LIB_ERR_NO_INIT);
    TestSaInterface.sa_get_operational_sa_from_gvcid = Test_GetOperationalSa;
    TestSaStatus = -77;
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, &channel), -77);
    TestSaStatus = CRYPTO_LIB_SUCCESS;
    TestReturnNullSa = true;
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, &channel), CRYPTO_LIB_ERR_NULL_SA);
    TestReturnNullSa = false;

    TestSecurityHeaderLength = -1;
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, &channel), TM_SDLP_INVALID_LENGTH);
    TestSecurityHeaderLength = 0;
    TestSecurityTrailerLength = -1;
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, &channel), TM_SDLP_INVALID_LENGTH);
    TestSecurityTrailerLength = 0;
    TestSecurityHeaderLength = UINT16_MAX;
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, &channel), TM_SDLP_INVALID_LENGTH);
    TestSecurityHeaderLength = 0;
    global.frameLength = 5;
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, &channel), TM_SDLP_INVALID_LENGTH);

    global.frameLength = sizeof(frame);
    global.hasErrCtrl = true;
    channel.fshFlag = true;
    channel.secHdrLength = 4;
    channel.ocfFlag = true;
    TestSecurityHeaderLength = 3;
    TestSecurityTrailerLength = 5;
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, &channel), TM_SDLP_SUCCESS);
    UtAssert_UINT16_EQ(info.dataFieldOffset, TMTF_PRIHDR_LENGTH + 5 + 3);
    UtAssert_UINT16_EQ(info.dataFieldLength, sizeof(frame) - info.dataFieldOffset - 5 - TMTF_OCF_LENGTH -
                                                TMTF_ERR_CTRL_FIELD_LENGTH);
    UtAssert_True(info.isInitialized, "VCP channel is initialized");

    channel.dataType = 1;
    channel.fshFlag = false;
    channel.secHdrLength = 0;
    channel.ocfFlag = false;
    global.hasErrCtrl = false;
    TestSecurityHeaderLength = 0;
    TestSecurityTrailerLength = 0;
    UtAssert_INT32_EQ(TM_SDLP_InitChannel(&info, frame, overflow, &global, &channel), TM_SDLP_SUCCESS);
}

static void Test_TmSdlpInputsAndFrames(void)
{
    TM_SDLP_FrameInfo_t     info = {0};
    TM_SDLP_GlobalConfig_t  global;
    TM_SDLP_ChannelConfig_t channel;
    uint8                   frame[64];
    uint8                   overflow[64];
    uint8                   data[96] = {0};
    uint8                   mcCount = 0;
    uint8                   ocf[TMTF_OCF_LENGTH] = {1, 2, 3, 4};
    CFE_MSG_Size_t          packetLength = 10;

    UtAssert_INT32_EQ(TM_SDLP_FrameHasData(NULL), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_AddPacket(NULL, (CFE_MSG_Message_t *)data), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_AddPacket(&info, NULL), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_AddPacket(&info, (CFE_MSG_Message_t *)data), TM_SDLP_FRAME_NOT_INIT);
    UtAssert_INT32_EQ(TM_SDLP_AddIdlePacket(NULL, (CFE_MSG_Message_t *)data), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_AddIdlePacket(&info, NULL), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_AddIdlePacket(&info, (CFE_MSG_Message_t *)data), TM_SDLP_FRAME_NOT_INIT);
    UtAssert_INT32_EQ(TM_SDLP_AddVcaData(NULL, data, 1), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_AddVcaData(&info, NULL, 1), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_AddVcaData(&info, data, 1), TM_SDLP_FRAME_NOT_INIT);
    UtAssert_INT32_EQ(TM_SDLP_StartFrame(NULL), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_StartFrame(&info), TM_SDLP_FRAME_NOT_INIT);
    UtAssert_INT32_EQ(TM_SDLP_SetOidFrame(NULL, (CFE_MSG_Message_t *)data), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_SetOidFrame(&info, NULL), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_SetOidFrame(&info, (CFE_MSG_Message_t *)data), TM_SDLP_FRAME_NOT_INIT);
    UtAssert_INT32_EQ(TM_SDLP_CompleteFrame(NULL, &mcCount, NULL), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_CompleteFrame(&info, NULL, NULL), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_CompleteFrame(&info, &mcCount, NULL), TM_SDLP_FRAME_NOT_INIT);

    UtAssert_INT32_EQ(InitSdlp(&info, &global, &channel, frame, sizeof(frame), overflow, sizeof(overflow)),
                      TM_SDLP_SUCCESS);
    UtAssert_INT32_EQ(TM_SDLP_FrameHasData(&info), 0);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &packetLength, sizeof(packetLength), false);
    UtAssert_INT32_EQ(TM_SDLP_AddPacket(&info, (CFE_MSG_Message_t *)data), TM_SDLP_FRAME_NOT_READY);
    UtAssert_INT32_EQ(TM_SDLP_AddVcaData(&info, data, 1), TM_SDLP_FRAME_NOT_READY);
    UtAssert_INT32_EQ(TM_SDLP_StartFrame(&info), TM_SDLP_SUCCESS);
    UtAssert_INT32_EQ(TM_SDLP_StartFrame(&info), TM_SDLP_SUCCESS);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &packetLength, sizeof(packetLength), false);
    UtAssert_INT32_EQ(TM_SDLP_AddPacket(&info, (CFE_MSG_Message_t *)data),
                      (int32)info.dataFieldLength - (int32)packetLength);
    UtAssert_INT32_EQ(TM_SDLP_FrameHasData(&info), 1);
    UtAssert_INT32_EQ(TM_SDLP_SetOidFrame(&info, (CFE_MSG_Message_t *)data), TM_SDLP_ERROR);

    channel.ocfFlag = true;
    global.hasErrCtrl = true;
    UtAssert_INT32_EQ(TM_SDLP_CompleteFrame(&info, &mcCount, NULL), TM_SDLP_INVALID_POINTER);
    UtAssert_INT32_EQ(TM_SDLP_CompleteFrame(&info, &mcCount, ocf), TM_SDLP_SUCCESS);
    UtAssert_UINT8_EQ(mcCount, 1);
    UtAssert_True(!info.isReady, "completed frame is reset");
    channel.isMaster = true;
    UtAssert_INT32_EQ(TM_SDLP_CompleteFrame(&info, &mcCount, ocf), TM_SDLP_SUCCESS);
}

static void Test_TmSdlpIdleAndOverflow(void)
{
    TM_SDLP_FrameInfo_t     info;
    TM_SDLP_GlobalConfig_t  global;
    TM_SDLP_ChannelConfig_t channel;
    uint8                   frame[32];
    uint8                   overflow[64];
    uint8                   data[128] = {0};
    uint8                   mcCount = 0;
    CFE_SB_MsgId_t          idleId = CFE_SB_ValueToMsgId(0x7ffU);
    CFE_SB_MsgId_t          badId = CFE_SB_ValueToMsgId(1U);
    CFE_MSG_Size_t          packetLength;

    UtAssert_INT32_EQ(InitSdlp(&info, &global, &channel, frame, sizeof(frame), overflow, sizeof(overflow)),
                      TM_SDLP_SUCCESS);
    UtAssert_INT32_EQ(TM_SDLP_StartFrame(&info), TM_SDLP_SUCCESS);
    info.freeOctets = 0;
    UtAssert_INT32_EQ(TM_SDLP_AddIdlePacket(&info, (CFE_MSG_Message_t *)data), TM_SDLP_SUCCESS);

    info.freeOctets = 5;
    info.currentDataOffset = info.dataFieldOffset;
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &badId, sizeof(badId), false);
    UtAssert_INT32_EQ(TM_SDLP_AddIdlePacket(&info, (CFE_MSG_Message_t *)data), TM_SDLP_ERROR);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &idleId, sizeof(idleId), false);
    UtAssert_INT32_EQ(TM_SDLP_AddIdlePacket(&info, (CFE_MSG_Message_t *)data), 0);
    UtAssert_UINT16_EQ(info.overflowInfo.partialOctets, 2);

    UtAssert_INT32_EQ(TM_SDLP_CompleteFrame(&info, &mcCount, NULL), TM_SDLP_SUCCESS);
    UtAssert_INT32_EQ(TM_SDLP_StartFrame(&info), TM_SDLP_SUCCESS);
    UtAssert_UINT16_EQ(info.overflowInfo.partialOctets, 0);

    /* Queue a whole packet, then recover it at the next frame boundary. */
    info.freeOctets = 0;
    info.currentDataOffset = info.dataFieldOffset + info.dataFieldLength;
    packetLength = 10;
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &packetLength, sizeof(packetLength), false);
    UtAssert_INT32_EQ(TM_SDLP_AddPacket(&info, (CFE_MSG_Message_t *)data), 0);
    UtAssert_INT32_EQ(TM_SDLP_CompleteFrame(&info, &mcCount, NULL), TM_SDLP_SUCCESS);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &packetLength, sizeof(packetLength), false);
    UtAssert_INT32_EQ(TM_SDLP_StartFrame(&info), TM_SDLP_SUCCESS);

    /* Put both write and read cursors near the end to exercise ring wrapping. */
    UtAssert_INT32_EQ(TM_SDLP_CompleteFrame(&info, &mcCount, NULL), TM_SDLP_SUCCESS);
    info.overflowInfo.dataStart = overflow + 60;
    info.overflowInfo.dataEnd = overflow + 60;
    info.overflowInfo.freeOctets = sizeof(overflow);
    UtAssert_INT32_EQ(TM_SDLP_StartFrame(&info), TM_SDLP_SUCCESS);
    info.freeOctets = 0;
    info.currentDataOffset = info.dataFieldOffset + info.dataFieldLength;
    UtAssert_INT32_EQ(TM_SDLP_AddVcaData(&info, data, 10), 0);
    UtAssert_UINT16_EQ(info.overflowInfo.partialOctets, 0);
    info.overflowInfo.partialOctets = 10;
    UtAssert_INT32_EQ(TM_SDLP_CompleteFrame(&info, &mcCount, NULL), TM_SDLP_SUCCESS);
    UtAssert_INT32_EQ(TM_SDLP_StartFrame(&info), TM_SDLP_SUCCESS);

    /* Distinguish an item too large for the queue from a queue lacking space. */
    info.freeOctets = 0;
    info.overflowInfo.freeOctets = sizeof(overflow);
    UtAssert_INT32_EQ(TM_SDLP_AddVcaData(&info, data, sizeof(data)), TM_SDLP_INVALID_LENGTH);
    info.overflowInfo.freeOctets = 1;
    UtAssert_INT32_EQ(TM_SDLP_AddVcaData(&info, data, 2), TM_SDLP_OVERFLOW_FULL);

    UtAssert_INT32_EQ(InitSdlp(&info, &global, &channel, frame, sizeof(frame), overflow, sizeof(overflow)),
                      TM_SDLP_SUCCESS);
    UtAssert_INT32_EQ(TM_SDLP_StartFrame(&info), TM_SDLP_SUCCESS);
    UtAssert_INT32_EQ(TM_SDLP_SetOidFrame(&info, (CFE_MSG_Message_t *)data), 0);
}

static void Test_UdpTransport(void)
{
    IO_TransUdpConfig_t config = {0};
    IO_TransUdpConfig_t badConfig = {0};
    IO_TransUdp_t       udp = {.sockId = -1};
    IO_TransUdp_t       second = {.sockId = -1};
    struct sockaddr_in  boundAddr;
    socklen_t           boundLength = sizeof(boundAddr);
    uint8               sent[] = {0x11, 0x22, 0x33, 0x44};
    uint8               received[8] = {0};
    char                destination[16] = IO_TRANS_UDP_INADDR_LOOPBACK;
    char                badDestination[16] = "999.999.999.999";

    UtAssert_INT32_EQ(IO_TransUdpCreateSocket(NULL), IO_TRANS_UDP_BAD_INPUT_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpInit(NULL, NULL), IO_TRANS_UDP_SOCKETCREATE_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpConfigSocket(NULL, &udp), IO_TRANS_UDP_BAD_INPUT_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpConfigSocket(&config, NULL), IO_TRANS_UDP_BAD_INPUT_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpConfigSocket(&config, &udp), IO_TRANS_UDP_BAD_INPUT_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpBindSocket(NULL), IO_TRANS_UDP_BAD_INPUT_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpCloseSocket(NULL), IO_TRANS_UDP_BAD_INPUT_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpSetDestAddr(NULL, destination, 1), IO_TRANS_UDP_BAD_INPUT_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpSetDestAddr(&udp, NULL, 1), IO_TRANS_UDP_BAD_INPUT_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpRcvTimeout(NULL, received, sizeof(received), 0),
                      IO_TRANS_UDP_BAD_INPUT_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpRcvTimeout(&udp, NULL, sizeof(received), 0),
                      IO_TRANS_UDP_BAD_INPUT_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpRcv(NULL, received, sizeof(received)), IO_TRANS_UDP_BAD_INPUT_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpRcv(&udp, NULL, sizeof(received)), IO_TRANS_UDP_BAD_INPUT_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpSnd(NULL, sent, sizeof(sent)), IO_TRANS_UDP_BAD_INPUT_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpSnd(&udp, NULL, sizeof(sent)), IO_TRANS_UDP_BAD_INPUT_ERROR);

    strcpy(badConfig.cAddr, IO_TRANS_UDP_INADDR_ANY);
    badConfig.timeoutRcv = -2;
    UtAssert_True(IO_TransUdpCreateSocket(&udp) >= 0, "UDP socket created for bad-timeout test");
    UtAssert_INT32_EQ(IO_TransUdpConfigSocket(&badConfig, &udp), IO_TRANS_UDP_BAD_INPUT_ERROR);
    IO_TransUdpCloseSocket(&udp);
    badConfig.timeoutRcv = 0;
    badConfig.timeoutSnd = -2;
    UtAssert_True(IO_TransUdpCreateSocket(&udp) >= 0, "UDP socket created for send-timeout test");
    UtAssert_INT32_EQ(IO_TransUdpConfigSocket(&badConfig, &udp), IO_TRANS_UDP_BAD_INPUT_ERROR);
    IO_TransUdpCloseSocket(&udp);

    udp.sockId = STDIN_FILENO;
    badConfig.timeoutSnd = 0;
    badConfig.timeoutRcv = 1;
    UtAssert_INT32_EQ(IO_TransUdpConfigSocket(&badConfig, &udp), IO_TRANS_UDP_SOCKETOPT_ERROR);
    badConfig.timeoutRcv = 0;
    badConfig.timeoutSnd = 1;
    UtAssert_INT32_EQ(IO_TransUdpConfigSocket(&badConfig, &udp), IO_TRANS_UDP_SOCKETOPT_ERROR);

    strcpy(config.cAddr, "localhost");
    config.usPort = 0;
    config.timeoutRcv = 10;
    config.timeoutSnd = 10;
    UtAssert_True(IO_TransUdpInit(&config, &udp) >= 0, "UDP loopback receiver initialized");
    memset(&boundAddr, 0, sizeof(boundAddr));
    UtAssert_INT32_EQ(getsockname(udp.sockId, (struct sockaddr *)&boundAddr, &boundLength), 0);
    UtAssert_INT32_EQ(IO_TransUdpSetDestAddr(&udp, destination, ntohs(boundAddr.sin_port)),
                      IO_TRANS_UDP_NO_ERROR);
    UtAssert_INT32_EQ(IO_TransUdpSetDestAddr(&udp, badDestination, 1), IO_TRANS_UDP_BAD_INPUT_ERROR);

    UtAssert_INT32_EQ(IO_TransUdpSnd(&udp, sent, sizeof(sent)), sizeof(sent));
    UtAssert_INT32_EQ(IO_TransUdpRcvTimeout(&udp, received, sizeof(received), IO_TRANS_PEND_FOREVER), sizeof(sent));
    UtAssert_MemCmp(received, sent, sizeof(sent), "blocking UDP loopback preserves payload");
    UtAssert_INT32_EQ(IO_TransUdpSnd(&udp, sent, sizeof(sent)), sizeof(sent));
    UtAssert_INT32_EQ(IO_TransUdpRcvTimeout(&udp, received, sizeof(received), 0), sizeof(sent));
    UtAssert_INT32_EQ(IO_TransUdpRcvTimeout(&udp, received, sizeof(received), 0), 0);
    UtAssert_INT32_EQ(IO_TransUdpRcv(&udp, received, sizeof(received)), 0);

    /* A second bind to the live receiver's port must fail. */
    strcpy(config.cAddr, IO_TRANS_UDP_INADDR_LOOPBACK);
    config.usPort = ntohs(boundAddr.sin_port);
    config.timeoutRcv = 0;
    config.timeoutSnd = 0;
    UtAssert_INT32_EQ(IO_TransUdpInit(&config, &second), IO_TRANS_UDP_SOCKETBIND_ERROR);
    IO_TransUdpCloseSocket(&second);

    UtAssert_INT32_EQ(IO_TransUdpCloseSocket(&udp), 0);
    UtAssert_INT32_EQ(IO_TransUdpCloseSocket(&udp), -1);
    UtAssert_INT32_EQ(IO_TransUdpSnd(&udp, sent, sizeof(sent)), -1);
    UtAssert_INT32_EQ(IO_TransUdpRcv(&udp, received, sizeof(received)), -1);
}

static void Test_Rs422Transport(void)
{
    IO_TransRS422Config_t config = {0};
    uint8                 input[] = {1, 2, 3, 4};
    uint8                 output[8] = {0};
    int                   pipeFd[2];
    int                   masterFd;
    int32                 serialFd;
    char                 *slaveName;
    void                (*oldSigpipe)(int);

    UtAssert_INT32_EQ(IO_TransRS422Init(NULL), IO_TRANS_RS422_BADINPUT_ERR);
    config.baudRate = 123;
    strcpy(config.device, "/dev/null");
    UtAssert_INT32_EQ(IO_TransRS422Init(&config), IO_TRANS_RS422_BAUDRATE_ERR);
    config.baudRate = 115200;
    config.device[0] = '\0';
    UtAssert_INT32_EQ(IO_TransRS422Init(&config), IO_TRANS_RS422_BADDEVICE_ERR);
    strcpy(config.device, "/not/a/serial/port");
    UtAssert_INT32_EQ(IO_TransRS422Init(&config), IO_TRANS_RS422_OPEN_ERR);

    UtAssert_True(IO_TransRS422GetBaudRateMacro(19200) != (speed_t)-1, "19200 baud supported");
    UtAssert_True(IO_TransRS422GetBaudRateMacro(38400) != (speed_t)-1, "38400 baud supported");
    UtAssert_True(IO_TransRS422GetBaudRateMacro(57600) != (speed_t)-1, "57600 baud supported");
    UtAssert_True(IO_TransRS422GetBaudRateMacro(115200) != (speed_t)-1, "115200 baud supported");
    UtAssert_True(IO_TransRS422GetBaudRateMacro(230400) != (speed_t)-1, "230400 baud supported");
    UtAssert_True(IO_TransRS422GetBaudRateMacro(460800) != (speed_t)-1, "460800 baud supported");
    UtAssert_True(IO_TransRS422GetBaudRateMacro(921600) != (speed_t)-1, "921600 baud supported");
    UtAssert_INT32_EQ(IO_TransRS422GetBaudRateMacro(1), -1);

    masterFd = posix_openpt(O_RDWR | O_NOCTTY);
    UtAssert_True(masterFd >= 0, "pseudo-terminal master opened");
    UtAssert_INT32_EQ(grantpt(masterFd), 0);
    UtAssert_INT32_EQ(unlockpt(masterFd), 0);
    slaveName = ptsname(masterFd);
    UtAssert_NOT_NULL(slaveName);
    snprintf(config.device, sizeof(config.device), "%s", slaveName);
    config.baudRate = 115200;
    config.timeout = 100;
    config.minBytes = 1;
    serialFd = IO_TransRS422Init(&config);
    UtAssert_True(serialFd >= 0, "pseudo-terminal serial endpoint initialized");
    if (serialFd >= 0)
    {
        UtAssert_INT32_EQ(IO_TransRS422Close(serialFd), 0);
    }
    close(masterFd);

    UtAssert_INT32_EQ(pipe(pipeFd), 0);
    UtAssert_INT32_EQ(IO_TransRS422Write(pipeFd[1], input, sizeof(input)), sizeof(input));
    UtAssert_INT32_EQ(IO_TransRS422ReadTimeout(pipeFd[0], output, sizeof(input), IO_TRANS_PEND_FOREVER),
                      sizeof(input));
    UtAssert_MemCmp(output, input, sizeof(input), "serial read/write helpers preserve payload");
    UtAssert_INT32_EQ(IO_TransRS422ReadTimeout(pipeFd[0], output, 1, 0), 0);
    UtAssert_INT32_EQ(IO_TransRS422Read(pipeFd[0], output, 0), 0);
    close(pipeFd[1]);
    UtAssert_INT32_EQ(IO_TransRS422Read(pipeFd[0], output, 1), 0);
    UtAssert_INT32_EQ(IO_TransRS422Close(pipeFd[0]), 0);
    UtAssert_INT32_EQ(IO_TransRS422Close(pipeFd[0]), -1);
    UtAssert_INT32_EQ(IO_TransRS422Write(-1, input, sizeof(input)), IO_TRANS_RS422_BADDEVICE_ERR);

    UtAssert_INT32_EQ(pipe(pipeFd), 0);
    close(pipeFd[0]);
    oldSigpipe = signal(SIGPIPE, SIG_IGN);
    UtAssert_INT32_EQ(IO_TransRS422Write(pipeFd[1], input, sizeof(input)), IO_TRANS_RS422_ERROR);
    signal(SIGPIPE, oldSigpipe);
    close(pipeFd[1]);
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_UtilitiesAndSync, Test_Setup, NULL, "IO utility and synchronization services");
    UtTest_Add(Test_Tctf, Test_Setup, NULL, "telecommand transfer frames");
    UtTest_Add(Test_Tmtf, Test_Setup, NULL, "telemetry transfer frames");
    UtTest_Add(Test_Select, Test_Setup, NULL, "transport descriptor selection");
    UtTest_Add(Test_Cop1Clcw, Test_Setup, NULL, "COP-1 CLCW fields");
    UtTest_Add(Test_Cop1Frames, Test_Setup, NULL, "COP-1 FARM-1 state machine");
    UtTest_Add(Test_TmSdlpInitialization, Test_Setup, NULL, "TM SDLP initialization and configuration");
    UtTest_Add(Test_TmSdlpInputsAndFrames, Test_Setup, NULL, "TM SDLP inputs and frame lifecycle");
    UtTest_Add(Test_TmSdlpIdleAndOverflow, Test_Setup, NULL, "TM SDLP idle data and overflow queue");
    UtTest_Add(Test_UdpTransport, Test_Setup, NULL, "UDP transport over loopback");
    UtTest_Add(Test_Rs422Transport, Test_Setup, NULL, "RS-422 transport and pseudo-terminal");
}

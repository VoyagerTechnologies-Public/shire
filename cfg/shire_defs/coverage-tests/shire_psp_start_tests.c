/* Isolated coverage of SHIRE PSP startup and reset selection. */
#include "cfe_psp.h"
#include "cfe_psp_config.h"
#include "cfe_psp_memory.h"
#include "bsp-impl.h"
#include "target_config.h"
#include "utassert.h"
#include "utstubs.h"
#include "uttest.h"

#include <getopt.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void UT_OS_Application_Startup(void);
extern void UT_OS_Application_Run(void);
extern uint32 CFE_PSP_SpacecraftId;
extern uint32 CFE_PSP_CpuId;
extern char CFE_PSP_CpuName[32];
extern CFE_PSP_IdleTaskState_t CFE_PSP_IdleTaskState;
extern int32 CFE_PSP_OS_EventHandler(OS_Event_t event, osal_id_t object_id, void *data);

static CFE_PSP_ReservedMemoryBootRecord_t BootRecord;
static uint32 LastStartType;
static uint32 LastStartSubtype;
static uint32 LastReservedReset;
static unsigned int MainCalls;
static unsigned int MemorySetupCalls;
static unsigned int ModuleInitCalls;
static int32 ReservedStatus;
static jmp_buf UsageExit;
static int UsageStatus;

void ShireTest_Exit(int status) __attribute__((noreturn));
void ShireTest_Exit(int status)
{
    UsageStatus = status;
    longjmp(UsageExit, 1);
}

static void FakeSystemMain(uint32 start_type, uint32 start_subtype, uint32 mode,
                           const char *startup_file)
{
    LastStartType = start_type;
    LastStartSubtype = start_subtype;
    MainCalls++;
    UtAssert_UINT32_EQ(mode, 1);
    UtAssert_NOT_NULL(startup_file);
}

Target_CfeConfigData GLOBAL_CFE_CONFIGDATA = {
    .SystemMain = FakeSystemMain,
    .NonvolStartupFile = "/cf/cfe_es_startup.scr"
};
Target_ConfigData GLOBAL_CONFIGDATA = {
    .Default_CpuName = "cpu1",
    .Default_CpuId = 1,
    .Default_SpacecraftId = 42,
    .CfeConfig = &GLOBAL_CFE_CONFIGDATA
};
CFE_PSP_ReservedMemoryMap_t CFE_PSP_ReservedMemoryMap = {.BootPtr = &BootRecord};

unsigned int sleep(unsigned int seconds) { (void)seconds; return 0; }
void CFE_PSP_SetupReservedMemoryMap(void) { MemorySetupCalls++; }
void CFE_PSP_ModuleInit(void) { ModuleInitCalls++; }
int32 CFE_PSP_InitProcessorReservedMemory(uint32 reset_type)
{
    LastReservedReset = reset_type;
    return ReservedStatus;
}

static void ResetStartupState(void)
{
    static char name[] = "shire-fsw";
    static char *default_args[] = {name, NULL};
    memset(&BootRecord, 0, sizeof(BootRecord));
    OS_BSP_Global.ArgC = 1;
    OS_BSP_Global.ArgV = default_args;
    optind = 0;
    opterr = 0;
    LastStartType = LastStartSubtype = LastReservedReset = 0;
    MainCalls = MemorySetupCalls = ModuleInitCalls = 0;
    ReservedStatus = CFE_PSP_SUCCESS;
}

static void Test_BootRecordAndDefaults(void)
{
    ResetStartupState();
    BootRecord.ValidityFlag = CFE_PSP_BOOTRECORD_VALID;
    BootRecord.NextResetType = CFE_PSP_RST_TYPE_PROCESSOR;
    UT_OS_Application_Startup();
    UtAssert_UINT32_EQ(MainCalls, 1);
    UtAssert_UINT32_EQ(LastStartType, CFE_PSP_RST_TYPE_PROCESSOR);
    UtAssert_UINT32_EQ(LastReservedReset, CFE_PSP_RST_TYPE_PROCESSOR);
    UtAssert_UINT32_EQ(LastStartSubtype, 1);
    UtAssert_UINT32_EQ(CFE_PSP_CpuId, 1);
    UtAssert_UINT32_EQ(CFE_PSP_SpacecraftId, 42);
    UtAssert_StrCmp(CFE_PSP_CpuName, "cpu1", "default CPU name is retained");
    UtAssert_UINT32_EQ(MemorySetupCalls, 1);
    UtAssert_UINT32_EQ(ModuleInitCalls, 1);
}

static void Test_ExplicitArgumentsAndStartupErrors(void)
{
    static char storage[][16] = {
        "shire-fsw", "-R", "PO", "-S", "5", "-C", "3", "-I", "7", "-N", "sat"
    };
    static char *args[] = {
        storage[0], storage[1], storage[2], storage[3], storage[4], storage[5],
        storage[6], storage[7], storage[8], storage[9], storage[10], NULL
    };
    ResetStartupState();
    OS_BSP_Global.ArgC = 11;
    OS_BSP_Global.ArgV = args;
    BootRecord.ValidityFlag = CFE_PSP_BOOTRECORD_INVALID;
    BootRecord.NextResetType = CFE_PSP_RST_TYPE_PROCESSOR;
    UT_SetDefaultReturnValue(UT_KEY(OS_API_Init), OS_ERROR);
    UT_SetDefaultReturnValue(UT_KEY(OS_FileSysAddFixedMap), OS_ERROR);
    ReservedStatus = -1;
    UT_OS_Application_Startup();
    UT_ClearDefaultReturnValue(UT_KEY(OS_API_Init));
    UT_ClearDefaultReturnValue(UT_KEY(OS_FileSysAddFixedMap));
    UtAssert_UINT32_EQ(MainCalls, 1);
    UtAssert_UINT32_EQ(LastStartType, CFE_PSP_RST_TYPE_POWERON);
    UtAssert_UINT32_EQ(LastStartSubtype, 5);
    UtAssert_UINT32_EQ(LastReservedReset, CFE_PSP_RST_TYPE_POWERON);
    UtAssert_UINT32_EQ(CFE_PSP_CpuId, 3);
    UtAssert_UINT32_EQ(CFE_PSP_SpacecraftId, 7);
    UtAssert_StrCmp(CFE_PSP_CpuName, "sat", "explicit CPU name is retained");
    UtAssert_STUB_COUNT(CFE_PSP_Panic, 2);
}

static void Test_ExplicitProcessorResetAndShutdown(void)
{
    static char storage[][16] = {"shire-fsw", "--reset", "PR"};
    static char *args[] = {storage[0], storage[1], storage[2], NULL};
    ResetStartupState();
    OS_BSP_Global.ArgC = 3;
    OS_BSP_Global.ArgV = args;
    UT_OS_Application_Startup();
    UtAssert_UINT32_EQ(LastStartType, CFE_PSP_RST_TYPE_PROCESSOR);
    UtAssert_UINT32_EQ(LastReservedReset, CFE_PSP_RST_TYPE_PROCESSOR);
    CFE_PSP_IdleTaskState.ShutdownReq = true;
    UT_OS_Application_Run();
    UtAssert_STUB_COUNT(OS_TaskDelay, 1);
    UtAssert_STUB_COUNT(OS_DeleteAllObjects, 1);
}

static void ExpectUsageExit(char **args, uint32 count)
{
    ResetStartupState();
    OS_BSP_Global.ArgC = count;
    OS_BSP_Global.ArgV = args;
    UsageStatus = -1;
    if (setjmp(UsageExit) == 0)
    {
        UT_OS_Application_Startup();
        UtAssert_True(false, "invalid startup argument must exit after usage");
    }
    UtAssert_INT32_EQ(UsageStatus, EXIT_FAILURE);
    UtAssert_UINT32_EQ(MainCalls, 0);
}

static void Test_InvalidArgumentsShowUsage(void)
{
    static char reset_storage[][16] = {"shire-fsw", "-R", "BAD"};
    static char *reset_args[] = {reset_storage[0], reset_storage[1], reset_storage[2], NULL};
    static char low_storage[][16] = {"shire-fsw", "-S", "0"};
    static char *low_args[] = {low_storage[0], low_storage[1], low_storage[2], NULL};
    static char high_storage[][16] = {"shire-fsw", "-S", "6"};
    static char *high_args[] = {high_storage[0], high_storage[1], high_storage[2], NULL};
    static char help_storage[][16] = {"shire-fsw", "--help"};
    static char *help_args[] = {help_storage[0], help_storage[1], NULL};

    ExpectUsageExit(reset_args, 3);
    ExpectUsageExit(low_args, 3);
    ExpectUsageExit(high_args, 3);
    ExpectUsageExit(help_args, 2);
}

static void Test_TaskStartEventNames(void)
{
    osal_id_t task_id = OS_ObjectIdFromInteger(OS_OBJECT_TYPE_OS_TASK << OS_OBJECT_TYPE_SHIFT);
    char long_name[] = "ordinary-task-name-long";
    char cfe_name[] = "CFE_TestTask";

    UtAssert_INT32_EQ(CFE_PSP_OS_EventHandler(OS_EVENT_RESOURCE_ALLOCATED, task_id, NULL), OS_SUCCESS);
    UtAssert_INT32_EQ(CFE_PSP_OS_EventHandler(OS_EVENT_RESOURCE_CREATED, task_id, NULL), OS_SUCCESS);
    UtAssert_INT32_EQ(CFE_PSP_OS_EventHandler(OS_EVENT_RESOURCE_DELETED, task_id, NULL), OS_SUCCESS);
    UtAssert_INT32_EQ(CFE_PSP_OS_EventHandler((OS_Event_t)-1, task_id, NULL), OS_SUCCESS);

    UT_SetDefaultReturnValue(UT_KEY(OS_GetResourceName), OS_ERROR);
    UtAssert_INT32_EQ(CFE_PSP_OS_EventHandler(OS_EVENT_TASK_STARTUP, task_id, NULL), OS_SUCCESS);
    UT_ClearDefaultReturnValue(UT_KEY(OS_GetResourceName));
    UT_SetDataBuffer(UT_KEY(OS_GetResourceName), long_name, sizeof(long_name), false);
    UtAssert_INT32_EQ(CFE_PSP_OS_EventHandler(OS_EVENT_TASK_STARTUP, task_id, NULL), OS_SUCCESS);
    UT_SetDataBuffer(UT_KEY(OS_GetResourceName), cfe_name, sizeof(cfe_name), false);
    UtAssert_INT32_EQ(CFE_PSP_OS_EventHandler(OS_EVENT_TASK_STARTUP, task_id, NULL), OS_SUCCESS);
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_BootRecordAndDefaults, NULL, NULL, "SHIRE PSP boot record and defaults");
    UtTest_Add(Test_ExplicitArgumentsAndStartupErrors, NULL, NULL,
               "SHIRE PSP argument overrides and startup errors");
    UtTest_Add(Test_ExplicitProcessorResetAndShutdown, NULL, NULL,
               "SHIRE PSP processor reset and shutdown");
    UtTest_Add(Test_InvalidArgumentsShowUsage, NULL, NULL,
               "SHIRE PSP invalid arguments show usage");
    UtTest_Add(Test_TaskStartEventNames, NULL, NULL,
               "SHIRE PSP task-start event names");
}

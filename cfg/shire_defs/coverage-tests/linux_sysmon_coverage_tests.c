/* Behavioral coverage for the Linux system-monitor PSP module deployed by SHIRE. */

#include "cfe_psp.h"
#include "cfe_psp_module.h"
#include "iodriver_analog_io.h"
#include "iodriver_impl.h"

#include "utassert.h"
#include "utstubs.h"
#include "uttest.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_MAX_CPUS 128

typedef struct
{
    CFE_PSP_IODriver_AdcCode_t avg_load;
    unsigned long              last_run_time;
} TestCpuCore_t;

typedef struct
{
    volatile bool is_running;
    volatile bool should_run;
    uint8_t       num_cpus;
    pthread_t     task_id;
    int           dev_fd;
    uint32_t      num_samples;
    uint64_t      last_sample_time;
    TestCpuCore_t per_core[TEST_MAX_CPUS];
} TestCpuState_t;

extern CFE_PSP_ModuleApi_t CFE_PSP_linux_sysmon_API;

void    linux_sysmon_read_cpuuse_line(const char *line_data, unsigned int *cpu_num, unsigned long *run_time);
void    linux_sysmon_update_schedstat(TestCpuState_t *state, int elapsed_ms);
int32_t linux_sysmon_calc_aggregate_cpu(TestCpuState_t *state, CFE_PSP_IODriver_AdcCode_t *value);
int32_t linux_sysmon_aggregate_dispatch(uint32_t command, uint16_t subchannel, CFE_PSP_IODriver_Arg_t arg);
int32_t linux_sysmon_cpu_load_dispatch(uint32_t command, uint16_t subchannel, CFE_PSP_IODriver_Arg_t arg);

static void Test_Setup(void)
{
    UT_ResetState(0);
    CFE_PSP_linux_sysmon_API.Init(7);
}

static int MakeSchedstatFile(const char *content)
{
    char path[] = "/tmp/shire-schedstat-XXXXXX";
    int  fd = mkstemp(path);

    if (fd >= 0)
    {
        unlink(path);
        if (write(fd, content, strlen(content)) != (ssize_t)strlen(content))
        {
            close(fd);
            return -1;
        }
        lseek(fd, 0, SEEK_SET);
    }
    return fd;
}

static void Test_ParseAndAggregate(void)
{
    TestCpuState_t state = {0};
    unsigned int  cpu = 99;
    unsigned long runtime = 99;
    CFE_PSP_IODriver_AdcCode_t value = -1;

    linux_sysmon_read_cpuuse_line(" 2 0 0 0 0 0 0 123456789 0 0", &cpu, &runtime);
    UtAssert_UINT32_EQ(cpu, 2);
    UtAssert_True(runtime == 123456789UL, "scheduler runtime field parsed");
    cpu = 7;
    runtime = 8;
    linux_sysmon_read_cpuuse_line("not-a-number", &cpu, &runtime);
    UtAssert_UINT32_EQ(cpu, 7);
    UtAssert_True(runtime == 8UL, "malformed line leaves outputs unchanged");

    UtAssert_INT32_EQ(linux_sysmon_calc_aggregate_cpu(&state, &value), CFE_PSP_ERROR);
    UtAssert_INT32_EQ(value, 0);
    state.num_cpus = 2;
    state.per_core[0].avg_load = 0x100;
    state.per_core[1].avg_load = 0x300;
    UtAssert_INT32_EQ(linux_sysmon_calc_aggregate_cpu(&state, &value), CFE_PSP_SUCCESS);
    UtAssert_INT32_EQ(value, 0x200);
}

static void Test_SchedstatUpdates(void)
{
    TestCpuState_t state = {0};
    char           malformed[257];
    const char     updatedLine[] = "cpu0 0 0 0 0 0 0 6000000 0 0\n";

    state.dev_fd = MakeSchedstatFile(
        "version 15\n"
        "cpu0 0 0 0 0 0 0 1000000 0 0\n"
        "cpu1 0 0 0 0 0 0 2000000 0 0\n"
        "cpu200 0 0 0 0 0 0 3000000 0 0\n");
    UtAssert_True(state.dev_fd >= 0, "scheduler fixture created");
    linux_sysmon_update_schedstat(&state, 0);
    UtAssert_UINT8_EQ(state.num_cpus, 2);
    UtAssert_INT32_EQ(state.per_core[0].avg_load, 0xffffff);

    ftruncate(state.dev_fd, 0);
    lseek(state.dev_fd, 0, SEEK_SET);
    UtAssert_True(write(state.dev_fd, updatedLine, sizeof(updatedLine) - 1) == (ssize_t)(sizeof(updatedLine) - 1),
                  "updated scheduler fixture written");
    linux_sysmon_update_schedstat(&state, 10);
    UtAssert_True(state.per_core[0].avg_load > 0 && state.per_core[0].avg_load < 0xffffff,
                  "ordinary CPU load is scaled");
    lseek(state.dev_fd, 0, SEEK_SET);
    linux_sysmon_update_schedstat(&state, 1);
    UtAssert_INT32_EQ(state.per_core[0].avg_load, 0);

    close(state.dev_fd);
    linux_sysmon_update_schedstat(&state, 10);

    memset(malformed, 'x', sizeof(malformed));
    malformed[sizeof(malformed) - 1] = '\0';
    state.dev_fd = MakeSchedstatFile(malformed);
    UtAssert_True(state.dev_fd >= 0, "malformed scheduler fixture created");
    linux_sysmon_update_schedstat(&state, 10);
    close(state.dev_fd);
}

static void Test_Dispatch(void)
{
    const CFE_PSP_IODriver_API_t      *api = CFE_PSP_linux_sysmon_API.ExtendedApi;
    CFE_PSP_IODriver_Direction_t       direction = CFE_PSP_IODriver_Direction_DISABLED;
    CFE_PSP_IODriver_AdcCode_t         samples[TEST_MAX_CPUS] = {0};
    CFE_PSP_IODriver_AnalogRdWr_t      io = {.NumChannels = 1, .Samples = samples};
    CFE_PSP_IODriver_Arg_t             arg = {0};
    int32_t                             status;

    UtAssert_NOT_NULL(api);
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_NOOP, 0, 0, arg), CFE_PSP_SUCCESS);
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_ANALOG_IO_NOOP, 0, 0, arg), CFE_PSP_SUCCESS);
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_GET_RUNNING, 0, 0, arg), 0);
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_SET_CONFIGURATION, 0, 0, arg),
                      CFE_PSP_ERROR_NOT_IMPLEMENTED);
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_GET_CONFIGURATION, 0, 0, arg),
                      CFE_PSP_ERROR_NOT_IMPLEMENTED);
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_LOOKUP_SUBSYSTEM, 0, 0,
                                         CFE_PSP_IODriver_CONST_STR("aggregate")), 0);
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_LOOKUP_SUBSYSTEM, 0, 0,
                                         CFE_PSP_IODriver_CONST_STR("per-cpu")), 1);
    UtAssert_True(api->DeviceCommand(CFE_PSP_IODriver_LOOKUP_SUBSYSTEM, 0, 0,
                                     CFE_PSP_IODriver_CONST_STR("missing")) < 0,
                  "unknown subsystem rejected");
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_LOOKUP_SUBCHANNEL, 0, 0,
                                         CFE_PSP_IODriver_CONST_STR("cpu-load")), 0);
    UtAssert_True(api->DeviceCommand(CFE_PSP_IODriver_LOOKUP_SUBCHANNEL, 0, 0,
                                     CFE_PSP_IODriver_CONST_STR("missing")) < 0,
                  "unknown subchannel rejected");
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_QUERY_DIRECTION, 0, 0, arg),
                      CFE_PSP_ERROR_NOT_IMPLEMENTED);
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_QUERY_DIRECTION, 0, 0,
                                         CFE_PSP_IODriver_VPARG(&direction)), CFE_PSP_SUCCESS);
    UtAssert_INT32_EQ(direction, CFE_PSP_IODriver_Direction_INPUT_ONLY);
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_ANALOG_IO_READ_CHANNELS, 0, 0,
                                         CFE_PSP_IODriver_VPARG(&io)), CFE_PSP_ERROR);
    UtAssert_INT32_EQ(api->DeviceCommand(0x7fffffff, 0, 0, arg), CFE_PSP_ERROR_NOT_IMPLEMENTED);
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_NOOP, 99, 0, arg), CFE_PSP_ERROR_NOT_IMPLEMENTED);
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_NOOP, 1, 0, arg), CFE_PSP_SUCCESS);
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_ANALOG_IO_NOOP, 1, 0, arg), CFE_PSP_SUCCESS);
    UtAssert_INT32_EQ(api->DeviceCommand(0x7fffffff, 1, 0, arg), CFE_PSP_ERROR_NOT_IMPLEMENTED);

    status = api->DeviceCommand(CFE_PSP_IODriver_SET_RUNNING, 0, 0, CFE_PSP_IODriver_U32ARG(1));
    UtAssert_INT32_EQ(status, CFE_PSP_SUCCESS);
    if (status == CFE_PSP_SUCCESS)
    {
        UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_GET_RUNNING, 0, 0, arg), 1);
        UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_SET_RUNNING, 0, 0,
                                             CFE_PSP_IODriver_U32ARG(1)), CFE_PSP_SUCCESS);
        io.NumChannels = 1;
        api->DeviceCommand(CFE_PSP_IODriver_ANALOG_IO_READ_CHANNELS, 0, 0, CFE_PSP_IODriver_VPARG(&io));
        api->DeviceCommand(CFE_PSP_IODriver_ANALOG_IO_READ_CHANNELS, 1, 0, CFE_PSP_IODriver_VPARG(&io));
        io.NumChannels = TEST_MAX_CPUS;
        api->DeviceCommand(CFE_PSP_IODriver_ANALOG_IO_READ_CHANNELS, 1, TEST_MAX_CPUS,
                           CFE_PSP_IODriver_VPARG(&io));
        UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_SET_RUNNING, 0, 0,
                                             CFE_PSP_IODriver_U32ARG(0)), CFE_PSP_SUCCESS);
    }
    UtAssert_INT32_EQ(api->DeviceCommand(CFE_PSP_IODriver_SET_RUNNING, 0, 0,
                                         CFE_PSP_IODriver_U32ARG(0)), CFE_PSP_SUCCESS);
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_ParseAndAggregate, Test_Setup, NULL, "Linux sysmon parsing and aggregation");
    UtTest_Add(Test_SchedstatUpdates, Test_Setup, NULL, "Linux sysmon scheduler-stat updates");
    UtTest_Add(Test_Dispatch, Test_Setup, NULL, "Linux sysmon device dispatch and lifecycle");
}

#include "simulith.h"
#include "simulith_shared_barrier.h"
#include "unity.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

typedef enum
{
    RECV_ACK,
    RECV_EAGAIN,
    RECV_ERROR,
    RECV_DUPLICATE,
    RECV_UNEXPECTED,
    RECV_TICK_SEND_FAILURE,
    RECV_TICK_ACK_FAILURE,
    RECV_RUN_NULL_CALLBACK,
    RECV_RUN_COMPLETION_FAILURE,
    RECV_PHASE_CALLBACK_FAILURE,
    RECV_PHASE_VARIANTS,
    RECV_TIME_VALIDATION,
    RECV_INVALID_TICK,
    RECV_DUPLICATE_TICK,
    RECV_STOP_TICK,
    RECV_EAGAIN_THEN_STOP,
    RECV_OVERSIZED,
    RECV_SHARED_PHASE_RACE,
    RECV_SCRIPTED
} receive_mode_t;

static int socket_calls;
static int socket_failure_call;
static int connect_calls;
static int connect_failure_call;
static int send_failure;
static int send_calls;
static int receive_calls;
static receive_mode_t receive_mode;
static int shared_phase_receives;
static int shared_phase_completions;
static int shared_complete_failure;
static int shared_connect_failure, shared_receive_status;
static const char *scripted_reply;
static simulith_tick_message_t scripted_tick;
static simulith_shared_barrier_t *connected_barrier;


void simulith_log(const char *format, ...)
{
    (void)format;
}

void *__wrap_zmq_ctx_new(void)
{
    return socket_failure_call == -1 ? NULL : (void *)(uintptr_t)1;
}

int __wrap_zmq_ctx_term(void *context)
{
    (void)context;
    return 0;
}

void *__wrap_zmq_socket(void *context, int type)
{
    (void)context;
    (void)type;
    socket_calls++;
    return socket_calls == socket_failure_call ? NULL : (void *)(uintptr_t)(socket_calls + 1);
}

int __wrap_zmq_connect(void *socket, const char *endpoint)
{
    (void)socket;
    (void)endpoint;
    connect_calls++;
    return connect_calls == connect_failure_call ? -1 : 0;
}

int __wrap_zmq_setsockopt(void *socket, int option, const void *value, size_t length)
{
    (void)socket;
    (void)option;
    (void)value;
    (void)length;
    return 0;
}

int __wrap_zmq_close(void *socket)
{
    (void)socket;
    return 0;
}

int __wrap_zmq_send(void *socket, const void *buffer, size_t length, int flags)
{
    (void)socket;
    (void)buffer;
    (void)flags;
    send_calls++;
    return send_failure ? -1 : (int)length;
}

static int copy_reply(void *buffer, size_t length, const char *reply)
{
    size_t reply_length = strlen(reply);
    if (reply_length > length)
        reply_length = length;
    memcpy(buffer, reply, reply_length);
    return (int)reply_length;
}

static int copy_tick(void *buffer, size_t length, uint64_t sequence, uint64_t time_ns,
                     simulith_phase_t phase)
{
    simulith_tick_message_t tick = {
        .magic = SIMULITH_PROTOCOL_MAGIC,
        .version = SIMULITH_PROTOCOL_VERSION,
        .phase = phase,
        .sequence = sequence,
        .time_ns = time_ns
    };
    if (length < sizeof(tick))
        return -1;
    memcpy(buffer, &tick, sizeof(tick));
    return (int)sizeof(tick);
}

int __wrap_zmq_recv(void *socket, void *buffer, size_t length, int flags)
{
    (void)socket;
    (void)flags;
    receive_calls++;
    switch (receive_mode) {
        case RECV_SCRIPTED:
            if (scripted_reply) return copy_reply(buffer, length, scripted_reply);
            if (length < sizeof(scripted_tick)) return -1;
            memcpy(buffer, &scripted_tick, sizeof(scripted_tick));
            return sizeof(scripted_tick);
        case RECV_EAGAIN:
            errno = EAGAIN;
            return -1;
        case RECV_ERROR:
            errno = EIO;
            return -1;
        case RECV_DUPLICATE:
            return copy_reply(buffer, length, "DUP_ID");
        case RECV_UNEXPECTED:
            return copy_reply(buffer, length, "WHAT");
        case RECV_OVERSIZED:
            memset(buffer, 'A', length);
            return (int)length + 1;
        case RECV_SHARED_PHASE_RACE:
            return copy_reply(buffer, length, "ACK 1");
        case RECV_TICK_SEND_FAILURE:
            if (receive_calls == 1)
                return copy_tick(buffer, length, 0, 42, SIMULITH_PHASE_EXECUTE);
            return copy_reply(buffer, length, "ACK");
        case RECV_TICK_ACK_FAILURE:
            if (receive_calls == 1)
                return copy_tick(buffer, length, 0, 42, SIMULITH_PHASE_EXECUTE);
            errno = EIO;
            return -1;
        case RECV_RUN_NULL_CALLBACK:
            if (receive_calls == 1)
                return copy_tick(buffer, length, 2, 44, SIMULITH_PHASE_COMMIT);
            simulith_client_request_stop();
            errno = EAGAIN;
            return -1;
        case RECV_RUN_COMPLETION_FAILURE:
            if (receive_calls == 1)
                return copy_tick(buffer, length, 0, 100,
                                 SIMULITH_PHASE_PREPARE);
            if (receive_calls == 2)
                return copy_tick(buffer, length, 0, 100,
                                 SIMULITH_PHASE_COMMIT);
            if (receive_calls == 3)
                return copy_reply(buffer, length, "WHAT");
            simulith_client_request_stop();
            errno = EAGAIN;
            return -1;
        case RECV_PHASE_CALLBACK_FAILURE:
            return copy_tick(buffer, length, 4, 400,
                             SIMULITH_PHASE_PREPARE);
        case RECV_PHASE_VARIANTS:
            if (receive_calls == 1)
                return copy_tick(buffer, length, 0, 100,
                                 SIMULITH_PHASE_EXECUTE);
            if (receive_calls == 2)
                return copy_reply(buffer, length, "ACK");
            if (receive_calls == 3)
                return copy_tick(buffer, length, 0, 100,
                                 SIMULITH_PHASE_COMMIT);
            if (receive_calls == 4)
                return copy_reply(buffer, length, "ACK");
            simulith_client_request_stop();
            errno = EAGAIN;
            return -1;
        case RECV_TIME_VALIDATION:
            if (receive_calls == 1)
                return copy_tick(buffer, length, 0, 100, SIMULITH_PHASE_PREPARE);
            if (receive_calls == 2)
                return copy_tick(buffer, length, 0, 99, SIMULITH_PHASE_EXECUTE);
            if (receive_calls == 3)
                return copy_tick(buffer, length, 1, 100, SIMULITH_PHASE_PREPARE);
            return copy_tick(buffer, length, 1, 101, SIMULITH_PHASE_PREPARE);
        case RECV_INVALID_TICK: {
            int size = copy_tick(buffer, length, 1, 100,
                                 SIMULITH_PHASE_PREPARE);
            ((simulith_tick_message_t *)buffer)->magic = 0;
            return size;
        }
        case RECV_DUPLICATE_TICK:
            return copy_tick(buffer, length, 1, 100,
                             SIMULITH_PHASE_PREPARE);
        case RECV_STOP_TICK:
            return copy_tick(buffer, length, 2, 200, SIMULITH_PHASE_STOP);
        case RECV_EAGAIN_THEN_STOP:
            if (receive_calls > 1)
                simulith_client_request_stop();
            errno = EAGAIN;
            return -1;
        case RECV_ACK:
        default:
            return copy_reply(buffer, length, "ACK");
    }
}

int __wrap_simulith_shared_barrier_connect(simulith_shared_barrier_t *barrier, int slot)
{
    if (shared_connect_failure || slot < 0 || slot >= SIMULITH_SHARED_BARRIER_SLOTS) return -1;
    barrier->slot = slot;
    connected_barrier = barrier;
    return 0;
}

int __wrap_simulith_shared_barrier_receive(simulith_shared_barrier_t *barrier,
                                           uint64_t *sequence, uint64_t *time_ns,
                                           uint32_t *phase)
{
    (void)barrier;
    shared_phase_receives++;
    if (shared_receive_status) return shared_receive_status;
    *sequence = (uint64_t)shared_phase_receives;
    *time_ns = 100 + (uint64_t)shared_phase_receives;
    *phase = SIMULITH_PHASE_EXECUTE;
    return 0;
}

int __wrap_simulith_shared_barrier_complete(simulith_shared_barrier_t *barrier,
                                            uint64_t sequence, uint32_t phase)
{
    (void)barrier;
    (void)phase;
    shared_phase_completions++;
    if (shared_complete_failure)
        return -1;
    if (sequence == 1)
    {
        /* The server publishes the next phase before the old barrier call
         * returns to its caller. This used to overwrite the new phase's
         * completion state after receive had cleared it. */
        uint64_t time_ns = 0, next_sequence = 0;
        simulith_phase_t next_phase = 0;
        TEST_ASSERT_EQUAL_INT(0, simulith_client_receive_phase(
            &time_ns, &next_sequence, &next_phase));
        TEST_ASSERT_EQUAL_UINT64(2, next_sequence);
    }
    return 0;
}

void setUp(void)
{
    simulith_client_shutdown();
    socket_calls = 0;
    socket_failure_call = 0;
    connect_calls = 0;
    connect_failure_call = 0;
    send_failure = 0;
    send_calls = 0;
    receive_calls = 0;
    receive_mode = RECV_ACK;
    shared_phase_receives = 0;
    shared_phase_completions = 0;
    shared_complete_failure = 0;
    shared_connect_failure = shared_receive_status = 0;
    scripted_reply = NULL; memset(&scripted_tick, 0, sizeof(scripted_tick));
    connected_barrier = NULL;
}

void tearDown(void)
{
    simulith_client_shutdown();
    unsetenv("SIMULITH_SYNC_TRANSPORT");
}

static void test_client_initialization_resource_failures(void)
{
    socket_failure_call = -1;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_init("pub", "rep", "id", 1));

    socket_failure_call = 1;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_init("pub", "rep", "id", 1));

    socket_calls = 0;
    socket_failure_call = 2;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_init("pub", "rep", "id", 1));

    socket_calls = 0;
    socket_failure_call = 0;
    connect_calls = 0;
    connect_failure_call = 1;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_init("pub", "rep", "id", 1));

    socket_calls = 0;
    connect_calls = 0;
    connect_failure_call = 2;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_init("pub", "rep", "id", 1));
}

static void test_client_identity_and_shared_handshake_guards(void)
{
    TEST_ASSERT_EQUAL_INT(-1,
                          simulith_client_init("pub", "rep", "bad id", 1));

    /* An IPC client requesting the shared barrier must receive an assigned
     * slot.  A legacy slotless ACK is not sufficient for that transport. */
    unsetenv("SIMULITH_SYNC_TRANSPORT");
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init(
                                  "pub", "ipc:///tmp/shire-client-test.sock",
                                  "shared-client", 1));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_configure_phases(0));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_configure_phases(UINT32_C(1) << 31));
    receive_mode = RECV_ACK;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
}

static void test_client_handshake_failures(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 1));
    send_failure = 1;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
    send_failure = 0;

    receive_mode = RECV_EAGAIN;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
    receive_mode = RECV_ERROR;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
    receive_mode = RECV_DUPLICATE;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
    receive_mode = RECV_UNEXPECTED;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
    receive_mode = RECV_OVERSIZED;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
}

static void test_client_tick_failures_and_null_callback(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 1));
    uint64_t tick = 0;

    receive_mode = RECV_TICK_SEND_FAILURE;
    send_failure = 1;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_wait_for_tick(&tick));
    TEST_ASSERT_EQUAL_UINT64(42, tick);

    receive_calls = 0;
    receive_mode = RECV_TICK_ACK_FAILURE;
    send_failure = 0;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_wait_for_tick(&tick));

    receive_calls = 0;
    receive_mode = RECV_RUN_NULL_CALLBACK;
    simulith_client_run_loop(NULL);

    simulith_client_shutdown();
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 100));
    receive_calls = 0;
    receive_mode = RECV_RUN_COMPLETION_FAILURE;
    simulith_client_run_loop(NULL);
}

static void test_client_rejects_duplicate_completion(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 1));
    TEST_ASSERT_EQUAL_INT(0, simulith_client_configure_phases(SIMULITH_PHASE_MASK_EXECUTE));
    receive_mode = RECV_TICK_SEND_FAILURE;

    uint64_t tick = 0;
    TEST_ASSERT_EQUAL_INT(0, simulith_client_wait_for_tick(&tick));
    TEST_ASSERT_EQUAL_UINT64(42, tick);
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_complete_tick(0, SIMULITH_PHASE_EXECUTE));
}

static void test_shared_completion_does_not_clobber_next_phase(void)
{
    receive_mode = RECV_SHARED_PHASE_RACE;
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init(
        "pub", "ipc:///tmp/shire-client-test.sock", "shire-fsw", 1));
    TEST_ASSERT_EQUAL_INT(0, simulith_client_handshake());
    uint64_t time_ns = 0, sequence = 0;
    simulith_phase_t phase = 0;
    TEST_ASSERT_EQUAL_INT(0, simulith_client_receive_phase(&time_ns, &sequence, &phase));
    TEST_ASSERT_EQUAL_UINT64(1, sequence);
    TEST_ASSERT_EQUAL_INT(0, simulith_client_complete_tick(sequence, phase));
    TEST_ASSERT_EQUAL_INT(0, simulith_client_complete_tick(2, SIMULITH_PHASE_EXECUTE));
    TEST_ASSERT_EQUAL_INT(2, shared_phase_completions);
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_complete_tick(2, SIMULITH_PHASE_EXECUTE));
}

static void test_shared_completion_failure_allows_retry(void)
{
    receive_mode = RECV_SHARED_PHASE_RACE;
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init(
        "pub", "ipc:///tmp/shire-client-test.sock", "shire-fsw", 1));
    TEST_ASSERT_EQUAL_INT(0, simulith_client_handshake());
    uint64_t time_ns = 0, sequence = 0;
    simulith_phase_t phase = 0;
    TEST_ASSERT_EQUAL_INT(0, simulith_client_receive_phase(&time_ns, &sequence, &phase));
    shared_complete_failure = 1;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_complete_tick(sequence, phase));
    shared_complete_failure = 0;
    TEST_ASSERT_EQUAL_INT(0, simulith_client_complete_tick(sequence, phase));
}

static int fail_phase_callback(uint64_t sequence, uint64_t time_ns)
{
    TEST_ASSERT_EQUAL_UINT64(4, sequence);
    TEST_ASSERT_EQUAL_UINT64(400, time_ns);
    return -1;
}

static void test_phase_callback_failure_withholds_completion(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 1));
    TEST_ASSERT_EQUAL_INT(0, simulith_client_configure_phases(
        SIMULITH_PHASE_MASK_PREPARE));
    receive_mode = RECV_PHASE_CALLBACK_FAILURE;

    TEST_ASSERT_EQUAL_INT(-1, simulith_client_run_phased_loop(
        fail_phase_callback, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(0, send_calls);
}

static int successful_phase_callback(uint64_t sequence, uint64_t time_ns)
{
    TEST_ASSERT_EQUAL_UINT64(0, sequence);
    TEST_ASSERT_EQUAL_UINT64(100, time_ns);
    return 0;
}

static void test_phased_loop_dispatches_execute_and_commit(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 100));
    TEST_ASSERT_EQUAL_INT(0, simulith_client_configure_phases(
        SIMULITH_PHASE_MASK_EXECUTE | SIMULITH_PHASE_MASK_COMMIT));
    receive_mode = RECV_PHASE_VARIANTS;

    TEST_ASSERT_EQUAL_INT(0, simulith_client_run_phased_loop(
        NULL, successful_phase_callback, successful_phase_callback));
    TEST_ASSERT_EQUAL_INT(2, send_calls);
}

static void test_client_rejects_invalid_duplicate_and_stop_ticks(void)
{
    uint64_t time_ns = 0;
    uint64_t sequence = 0;
    simulith_phase_t phase = 0;

    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 100));
    receive_mode = RECV_INVALID_TICK;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_receive_phase(
        &time_ns, &sequence, &phase));

    receive_mode = RECV_DUPLICATE_TICK;
    TEST_ASSERT_EQUAL_INT(0, simulith_client_receive_phase(
        &time_ns, &sequence, &phase));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_receive_phase(
        &time_ns, &sequence, &phase));

    simulith_client_shutdown();
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 100));
    receive_calls = 0;
    receive_mode = RECV_STOP_TICK;
    TEST_ASSERT_EQUAL_INT(1, simulith_client_receive_phase(
        &time_ns, &sequence, &phase));
    TEST_ASSERT_EQUAL_INT(SIMULITH_PHASE_STOP, phase);

    simulith_client_shutdown();
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 100));
    receive_calls = 0;
    receive_mode = RECV_EAGAIN_THEN_STOP;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_receive_phase(
        &time_ns, &sequence, &phase));
}

static void test_client_rejects_non_monotonic_simulation_time(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 1));
    receive_mode = RECV_TIME_VALIDATION;

    uint64_t time_ns = 0;
    uint64_t sequence = 0;
    simulith_phase_t phase = 0;

    TEST_ASSERT_EQUAL_INT(0, simulith_client_receive_phase(
        &time_ns, &sequence, &phase));
    TEST_ASSERT_EQUAL_UINT64(100, time_ns);

    /* A later phase of one sequence must retain exactly the same time. */
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_receive_phase(
        &time_ns, &sequence, &phase));

    /* A new sequence must advance time rather than repeat it. */
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_receive_phase(
        &time_ns, &sequence, &phase));

    TEST_ASSERT_EQUAL_INT(0, simulith_client_receive_phase(
        &time_ns, &sequence, &phase));
    TEST_ASSERT_EQUAL_UINT64(1, sequence);
    TEST_ASSERT_EQUAL_UINT64(101, time_ns);
}


static void test_identity_bounds_and_explicit_zmq_override(void)
{
    char long_id[65]; memset(long_id, 'a', sizeof(long_id) - 1); long_id[64] = 0;
    const char *bad[] = {"", "a/b", long_id};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        TEST_ASSERT_EQUAL_INT(-1, simulith_client_init("pub", "rep", bad[i], 1));
    TEST_ASSERT_EQUAL_INT(0, socket_calls);
    long_id[63] = 0;
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", long_id, 1));
    simulith_client_shutdown();
    setenv("SIMULITH_SYNC_TRANSPORT", "zmq", 1);
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "ipc:///tmp/unused", "a-_.9", 1));
    TEST_ASSERT_EQUAL_INT(0, simulith_client_handshake()); /* slotless ACK is valid for ZMQ */
    TEST_ASSERT_NULL(connected_barrier);
    simulith_client_shutdown();
    setenv("SIMULITH_SYNC_TRANSPORT", "shared", 1);
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "ipc:///tmp/unused", "id", 1));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake()); /* shared requires a slot */
}
static void test_shared_handshake_slots_and_connect_failure(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "ipc:///tmp/unused", "id", 1));
    receive_mode = RECV_SCRIPTED;
    const char *bad[] = {"ACK 1 trailing", "ACK -1", "ACK 32"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        scripted_reply = bad[i];
        TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
    }
    scripted_reply = "ACK 31"; shared_connect_failure = 1;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
    shared_connect_failure = 0;
    TEST_ASSERT_EQUAL_INT(0, simulith_client_configure_phases(
        SIMULITH_PHASE_MASK_PREPARE | SIMULITH_PHASE_MASK_EXECUTE | SIMULITH_PHASE_MASK_COMMIT));
    TEST_ASSERT_EQUAL_INT(0, simulith_client_handshake());
    TEST_ASSERT_NOT_NULL(connected_barrier);
    TEST_ASSERT_EQUAL_INT(31, connected_barrier->slot);
    TEST_ASSERT_TRUE(connected_barrier->fast_receive_safe);
}
static void test_receive_outputs_and_completion_identity_guards(void)
{
    uint64_t time_ns, sequence; simulith_phase_t phase;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_configure_phases(SIMULITH_PHASE_MASK_COMMIT));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_complete_tick(0, SIMULITH_PHASE_EXECUTE));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_receive_phase(&time_ns, &sequence, &phase));
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 10));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_wait_for_tick(NULL));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_receive_phase(NULL, &sequence, &phase));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_receive_phase(&time_ns, NULL, &phase));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_receive_phase(&time_ns, &sequence, NULL));
    TEST_ASSERT_EQUAL_INT(0, receive_calls);
    receive_mode = RECV_SCRIPTED;
    scripted_tick = (simulith_tick_message_t){.magic = SIMULITH_PROTOCOL_MAGIC,
        .version = SIMULITH_PROTOCOL_VERSION, .sequence = 1, .time_ns = 100,
        .phase = SIMULITH_PHASE_PREPARE};
    TEST_ASSERT_EQUAL_INT(0, simulith_client_receive_phase(&time_ns, &sequence, &phase));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_complete_tick(sequence, 0));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_complete_tick(sequence, SIMULITH_PHASE_STOP));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_complete_tick(sequence + 1, phase));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_complete_tick(sequence, SIMULITH_PHASE_EXECUTE));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_complete_tick(sequence, phase)); /* not subscribed */
    TEST_ASSERT_EQUAL_INT(0, send_calls);
    TEST_ASSERT_EQUAL_INT(0, simulith_client_configure_phases(SIMULITH_PHASE_MASK_PREPARE));
    receive_mode = RECV_ACK;
    TEST_ASSERT_EQUAL_INT(0, simulith_client_complete_tick(sequence, phase));
    TEST_ASSERT_EQUAL_INT(1, send_calls);
}
static void test_bad_header_sequence_and_rate_do_not_replace_last_valid_tick(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 10));
    receive_mode = RECV_SCRIPTED;
    const simulith_tick_message_t valid = {.magic = SIMULITH_PROTOCOL_MAGIC,
        .version = SIMULITH_PROTOCOL_VERSION, .sequence = 10, .time_ns = 100,
        .phase = SIMULITH_PHASE_PREPARE};
    uint64_t time_ns, sequence; simulith_phase_t phase;
    for (int bad = 0; bad < 3; bad++) {
        scripted_tick = valid;
        if (bad == 0) scripted_tick.version++;
        if (bad == 1) scripted_tick.phase = 0;
        if (bad == 2) scripted_tick.phase = SIMULITH_PHASE_STOP + 1;
        TEST_ASSERT_EQUAL_INT(-1, simulith_client_receive_phase(&time_ns, &sequence, &phase));
    }
    scripted_tick = valid;
    TEST_ASSERT_EQUAL_INT(0, simulith_client_receive_phase(&time_ns, &sequence, &phase));
    for (int bad = 0; bad < 3; bad++) {
        scripted_tick = valid;
        if (bad == 0) scripted_tick.sequence = 9;
        if (bad == 1) { scripted_tick.sequence = 12; scripted_tick.time_ns = 120; }
        if (bad == 2) { scripted_tick.sequence = 11; scripted_tick.time_ns = 119; }
        TEST_ASSERT_EQUAL_INT(-1, simulith_client_receive_phase(&time_ns, &sequence, &phase));
    }
    scripted_tick = valid; scripted_tick.sequence = 11; scripted_tick.time_ns = 110;
    TEST_ASSERT_EQUAL_INT(0, simulith_client_receive_phase(&time_ns, &sequence, &phase));
    TEST_ASSERT_EQUAL_UINT64(11, sequence); TEST_ASSERT_EQUAL_UINT64(110, time_ns);
}
static void test_shared_receive_stop_and_rejection(void)
{
    receive_mode = RECV_SHARED_PHASE_RACE;
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "ipc:///tmp/unused", "shire-fsw", 10));
    TEST_ASSERT_EQUAL_INT(0, simulith_client_handshake());
    uint64_t time_ns, sequence; simulith_phase_t phase = 0;
    shared_receive_status = -1;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_receive_phase(&time_ns, &sequence, &phase));
    shared_receive_status = 1;
    TEST_ASSERT_EQUAL_INT(1, simulith_client_receive_phase(&time_ns, &sequence, &phase));
    TEST_ASSERT_EQUAL_INT(SIMULITH_PHASE_STOP, phase);
    TEST_ASSERT_EQUAL_INT(0, simulith_client_run_phased_loop(NULL, NULL, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_client_initialization_resource_failures);
    RUN_TEST(test_client_identity_and_shared_handshake_guards);
    RUN_TEST(test_client_handshake_failures);
    RUN_TEST(test_client_tick_failures_and_null_callback);
    RUN_TEST(test_client_rejects_duplicate_completion);
    RUN_TEST(test_shared_completion_does_not_clobber_next_phase);
    RUN_TEST(test_shared_completion_failure_allows_retry);
    RUN_TEST(test_phase_callback_failure_withholds_completion);
    RUN_TEST(test_phased_loop_dispatches_execute_and_commit);
    RUN_TEST(test_client_rejects_invalid_duplicate_and_stop_ticks);
    RUN_TEST(test_client_rejects_non_monotonic_simulation_time);
    RUN_TEST(test_identity_bounds_and_explicit_zmq_override);
    RUN_TEST(test_shared_handshake_slots_and_connect_failure);
    RUN_TEST(test_receive_outputs_and_completion_identity_guards);
    RUN_TEST(test_bad_header_sequence_and_rate_do_not_replace_last_valid_tick);
    RUN_TEST(test_shared_receive_stop_and_rejection);
    return UNITY_END();
}

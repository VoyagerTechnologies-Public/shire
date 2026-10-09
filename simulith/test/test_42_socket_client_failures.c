/* Public-API tests with real loopback sockets and a private shared IPC file.
 * Wrapping only the syscall boundary makes rare failures reproducible. */
#include "simulith_42_socket_client.h"
#include "shire_ipc_protocol.h"
#include "unity.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

enum { IO_OK, IO_SOCKET, IO_OPEN, IO_STAT, IO_SHORT_FILE, IO_MAP };
static int failure, interrupt_read, interrupt_write, fail_read, fail_write, fragment_io;
static int futex_mode, futex_waits, shared_opens;
static char shared_path[128];
static int owner_fd = -1, peer_fd = -1;
static shire_ipc_shared_t *shared;

int __real_socket(int, int, int);
int __wrap_socket(int domain, int type, int protocol)
{
    if (failure == IO_SOCKET) { errno = EMFILE; return -1; }
    return __real_socket(domain, type, protocol);
}
int __real_open(const char *, int, ...);
int __wrap_open(const char *path, int flags, ...)
{
    if (strcmp(path, SHIRE_IPC_SHARED_PATH) == 0) {
        shared_opens++;
        if (failure == IO_OPEN) { errno = ENOENT; return -1; }
        /* Never touch the production /tmp/42_ipc_shared.v1 inode. */
        return __real_open(shared_path, flags);
    }
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args; va_start(args, flags); mode = (mode_t)va_arg(args, int); va_end(args);
    }
    return __real_open(path, flags, mode);
}
int __real_fstat(int, struct stat *);
int __wrap_fstat(int fd, struct stat *details)
{
    if (failure == IO_STAT) { errno = EIO; return -1; }
    int result = __real_fstat(fd, details);
    if (!result && failure == IO_SHORT_FILE) details->st_size = sizeof(shire_ipc_shared_t) - 1;
    return result;
}
void *__real_mmap(void *, size_t, int, int, int, off_t);
void *__wrap_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    if (failure == IO_MAP) { errno = ENOMEM; return MAP_FAILED; }
    return __real_mmap(addr, length, prot, flags, fd, offset);
}
ssize_t __real_recv(int, void *, size_t, int);
ssize_t __wrap_recv(int fd, void *buffer, size_t length, int flags)
{
    if (interrupt_read) { interrupt_read = 0; errno = EINTR; return -1; }
    if (fail_read) { fail_read = 0; errno = ECONNRESET; return -1; }
    if (fragment_io && length > 3) length = 3;
    return __real_recv(fd, buffer, length, flags);
}
ssize_t __real_send(int, const void *, size_t, int);
ssize_t __wrap_send(int fd, const void *buffer, size_t length, int flags)
{
    if (interrupt_write) { interrupt_write = 0; errno = EINTR; return -1; }
    if (fail_write) { fail_write = 0; errno = EPIPE; return -1; }
    if (fragment_io && length > 3) length = 3;
    return __real_send(fd, buffer, length, flags);
}
long __real_syscall(long, ...);
long __wrap_syscall(long number, ...)
{
    TEST_ASSERT_EQUAL_INT(SYS_futex, number);
    va_list args; va_start(args, number);
    uint32_t *word = va_arg(args, uint32_t *);
    int op = va_arg(args, int);
    unsigned value = va_arg(args, unsigned);
    const struct timespec *timeout = va_arg(args, const struct timespec *);
    void *word2 = va_arg(args, void *);
    int value3 = va_arg(args, int);
    va_end(args);
    if (op == FUTEX_WAIT) {
        futex_waits++;
        if (futex_mode == 3)
            return __real_syscall(number, word, op, value, timeout, word2, value3);
        TEST_ASSERT_NOT_NULL(timeout);
        TEST_ASSERT_EQUAL_INT(5, timeout->tv_sec);
        if (futex_mode == 1 || (futex_mode == 2 && futex_waits > 1)) {
            /* Producer publishes after the client enters its futex wait. */
            __atomic_store_n(word, value + 1, __ATOMIC_RELEASE);
            errno = EAGAIN;
        } else errno = futex_mode == 2 ? EINTR : ETIMEDOUT;
        return -1;
    }
    TEST_ASSERT_EQUAL_INT(FUTEX_WAKE, op);
    return __real_syscall(number, word, op, value, timeout, word2, value3);
}

static shire_ipc_state_t valid_state(void)
{
    shire_ipc_state_t state = {0};
    state.header = (shire_ipc_header_t){.magic = SHIRE_IPC_MAGIC,
        .version = SHIRE_IPC_VERSION, .type = SHIRE_IPC_STATE,
        .payload_size = sizeof(state) - sizeof(state.header)};
    state.sim_time = 12.5; state.utc_civil_time = 748177200.0; state.mass = 42.25;
    return state;
}
static shire_ipc_ack_t valid_ack(void)
{
    shire_ipc_ack_t ack = {0};
    ack.header = (shire_ipc_header_t){.magic = SHIRE_IPC_MAGIC,
        .version = SHIRE_IPC_VERSION, .type = SHIRE_IPC_ACK,
        .payload_size = sizeof(ack) - sizeof(ack.header)};
    return ack;
}
static simulith_42_command_t wheel_command(void)
{
    return (simulith_42_command_t){.valid = 1, .type = SIMULITH_42_CMD_WHEEL_TORQUE,
        .spacecraft_id = 3, .cmd.wheel = {.enable_mask = 5, .torque = {1, 2, 3, 4}}};
}
static void connect_tcp(void)
{
    int listener = __real_socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, listener);
    struct sockaddr_in addr = {.sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    TEST_ASSERT_EQUAL_INT(0, bind(listener, (struct sockaddr *)&addr, sizeof(addr)));
    TEST_ASSERT_EQUAL_INT(0, listen(listener, 1));
    socklen_t length = sizeof(addr);
    TEST_ASSERT_EQUAL_INT(0, getsockname(listener, (struct sockaddr *)&addr, &length));
    TEST_ASSERT_EQUAL_INT(0, simulith_42_init("127.0.0.1", ntohs(addr.sin_port)));
    peer_fd = accept(listener, NULL, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, peer_fd);
    close(listener);
}
static void put_peer(const void *buffer, size_t length)
{
    TEST_ASSERT_EQUAL_INT(length, __real_send(peer_fd, buffer, length, MSG_NOSIGNAL));
}
static void create_shared_owner(void)
{
    strcpy(shared_path, "/tmp/shire-42-private-XXXXXX");
    owner_fd = mkstemp(shared_path);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, owner_fd);
    TEST_ASSERT_EQUAL_INT(0, ftruncate(owner_fd, sizeof(*shared)));
    shared = __real_mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED, owner_fd, 0);
    TEST_ASSERT_TRUE(shared != MAP_FAILED);
    memset(shared, 0, sizeof(*shared));
    shared->magic = SHIRE_IPC_SHARED_MAGIC;
    shared->version = SHIRE_IPC_SHARED_VERSION;
    shared->state = valid_state();
    TEST_ASSERT_EQUAL_INT(0, flock(owner_fd, LOCK_EX | LOCK_NB));
    setenv("FORTYTWO_IPC_MODE", "shared", 1);
}
void setUp(void)
{
    failure = IO_OK; interrupt_read = interrupt_write = fail_read = fail_write = fragment_io = 0;
    futex_mode = futex_waits = shared_opens = 0;
    peer_fd = owner_fd = -1; shared = NULL; shared_path[0] = 0;
    simulith_42_cleanup();
    setenv("FORTYTWO_IPC_MODE", "binary", 1);
    setenv("SIMULITH_42_RECONNECT_ATTEMPTS", "1", 1);
    setenv("SIMULITH_42_RECONNECT_DELAY_MS", "0", 1);
}
void tearDown(void)
{
    simulith_42_cleanup();
    if (peer_fd >= 0) close(peer_fd);
    if (shared) munmap(shared, sizeof(*shared));
    if (owner_fd >= 0) close(owner_fd);
    if (shared_path[0]) unlink(shared_path);
    unsetenv("FORTYTWO_IPC_MODE");
    unsetenv("SIMULITH_42_RECONNECT_ATTEMPTS");
    unsetenv("SIMULITH_42_RECONNECT_DELAY_MS");
}

static void test_socket_creation_failure(void)
{
    failure = IO_SOCKET;
    TEST_ASSERT_EQUAL_INT(-1, simulith_42_init("127.0.0.1", 1));
    TEST_ASSERT_EQUAL_INT(-1, simulith_42_init("/tmp/shire-unused-test.sock", 0));
    TEST_ASSERT_EQUAL_INT(0, simulith_42_is_connected());
}
static void test_shared_requires_live_owner_and_valid_header(void)
{
    create_shared_owner();
    for (int bad = IO_OPEN; bad <= IO_MAP; bad++) {
        failure = bad;
        TEST_ASSERT_EQUAL_INT(-1, simulith_42_init(NULL, 0));
        TEST_ASSERT_EQUAL_INT(0, simulith_42_is_connected());
    }
    failure = IO_OK;
    shared->magic = 0;
    TEST_ASSERT_EQUAL_INT(-1, simulith_42_init(NULL, 0));
    shared->magic = SHIRE_IPC_SHARED_MAGIC;
    shared->version++;
    TEST_ASSERT_EQUAL_INT(-1, simulith_42_init(NULL, 0));
    shared->version = SHIRE_IPC_SHARED_VERSION;
    TEST_ASSERT_EQUAL_INT(0, flock(owner_fd, LOCK_UN));
    TEST_ASSERT_EQUAL_INT(-1, simulith_42_init(NULL, 0)); /* valid but stale inode */
    TEST_ASSERT_EQUAL_INT(0, flock(owner_fd, LOCK_EX | LOCK_NB)); /* probe released its lock */
    failure = IO_OPEN;
    int previous_opens = shared_opens;
    setenv("SIMULITH_42_RECONNECT_ATTEMPTS", "2", 1);
    setenv("SIMULITH_42_RECONNECT_DELAY_MS", "1", 1);
    TEST_ASSERT_EQUAL_INT(-1, simulith_42_init(NULL, 0));
    TEST_ASSERT_EQUAL_INT(2, shared_opens - previous_opens);
    failure = IO_OK;
    TEST_ASSERT_EQUAL_INT(0, simulith_42_init(NULL, 0));
    TEST_ASSERT_EQUAL_INT(1, simulith_42_is_connected());
}
/* The producer publishes acknowledgements only after observing command_seq.
 * Its finite deadline also bounds failures when the client does not publish. */
static void *shared_producer(void *argument)
{
    int *result = argument;
    *result = -1;
    for (unsigned seq = 1; seq <= 2; seq++) {
        struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);
        while (__atomic_load_n(&shared->command_seq, __ATOMIC_ACQUIRE) < seq) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            if ((now.tv_sec - start.tv_sec) * 1000000000LL +
                now.tv_nsec - start.tv_nsec > 2000000000LL) return NULL;
            nanosleep(&(struct timespec){.tv_nsec = 100000}, NULL);
        }
        if (__atomic_load_n(&shared->command_seq, __ATOMIC_ACQUIRE) != seq ||
            shared->commands.count != (seq == 1 ? 1U : 0U)) return NULL;
        __atomic_store_n(&shared->ack_seq, seq, __ATOMIC_RELEASE);
        __real_syscall(SYS_futex, &shared->ack_seq, FUTEX_WAKE, 1, NULL, NULL, 0);
        if (seq == 1) {
            shared->state.sim_time = 0.5;
            __atomic_store_n(&shared->state_seq, 2, __ATOMIC_RELEASE);
            __real_syscall(SYS_futex, &shared->state_seq, FUTEX_WAKE, 1, NULL, NULL, 0);
        }
    }
    *result = 0;
    return NULL;
}

static void test_shared_round_trip_and_reconnect_sequence(void)
{
    create_shared_owner();
    TEST_ASSERT_EQUAL_INT(0, simulith_42_init(NULL, 0));
    simulith_42_context_t context;
    simulith_42_command_t command = wheel_command();
    shared->state.sim_time = 0.25;
    __atomic_store_n(&shared->state_seq, 1, __ATOMIC_RELEASE);
    futex_mode = 3;
    int producer_result = -1;
    pthread_t producer;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&producer, NULL, shared_producer, &producer_result));
    for (unsigned seq = 1; seq <= 2; seq++) {
        TEST_ASSERT_EQUAL_INT(0, simulith_42_request_state(&context));
        TEST_ASSERT_FLOAT_WITHIN(1e-6, seq * 0.25, context.sim_time);
        if (seq == 1) TEST_ASSERT_EQUAL_INT(0, simulith_42_send_command_batch(&command, 1));
        else TEST_ASSERT_EQUAL_INT(0, simulith_42_send_empty_commands());
        TEST_ASSERT_EQUAL_UINT32(seq, __atomic_load_n(&shared->command_seq, __ATOMIC_ACQUIRE));
        TEST_ASSERT_EQUAL_UINT32(seq == 1 ? 1 : 0, shared->commands.count);
        TEST_ASSERT_EQUAL_UINT32(SHIRE_IPC_COMMANDS_PAYLOAD_SIZE(seq == 1 ? 1 : 0),
                                 shared->commands.header.payload_size);
        if (seq == 1) {
            TEST_ASSERT_EQUAL_INT(3, shared->commands.commands[0].spacecraft_id);
            TEST_ASSERT_EQUAL_UINT32(5, shared->commands.commands[0].enable_mask);
            TEST_ASSERT_EQUAL_MEMORY(command.cmd.wheel.torque,
                                     shared->commands.commands[0].values, sizeof(command.cmd.wheel.torque));
        }
    }
    TEST_ASSERT_EQUAL_INT(0, pthread_join(producer, NULL));
    TEST_ASSERT_EQUAL_INT(0, producer_result);
    simulith_42_cleanup();
    TEST_ASSERT_EQUAL_INT(0, simulith_42_is_connected());
    shared->state_seq = shared->ack_seq = 1;
    TEST_ASSERT_EQUAL_INT(0, simulith_42_init(NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, simulith_42_request_state(&context));
    TEST_ASSERT_EQUAL_INT(0, simulith_42_send_empty_commands());
    TEST_ASSERT_EQUAL_UINT32(1, shared->command_seq); /* fresh connection starts at one */
}
static void test_shared_wait_retries_interrupts_and_detects_sequence_loss(void)
{
    create_shared_owner();
    simulith_42_context_t context;
    for (int mode = 1; mode <= 2; mode++) {
        shared->state_seq = 0; futex_mode = mode; futex_waits = 0;
        TEST_ASSERT_EQUAL_INT(0, simulith_42_init(NULL, 0));
        TEST_ASSERT_EQUAL_INT(0, simulith_42_request_state(&context));
        TEST_ASSERT_EQUAL_INT(mode, futex_waits);
        TEST_ASSERT_FLOAT_WITHIN(1e-6, 42.25, context.mass);
        simulith_42_cleanup();
    }
    for (int future = 0; future <= 1; future++) {
        shared->state_seq = future ? 2 : 0; futex_mode = 0; futex_waits = 0;
        TEST_ASSERT_EQUAL_INT(0, simulith_42_init(NULL, 0));
        TEST_ASSERT_EQUAL_INT(-1, simulith_42_request_state(&context));
        TEST_ASSERT_EQUAL_INT(0, simulith_42_is_connected());
        TEST_ASSERT_EQUAL_INT(future ? 0 : 1, futex_waits);
        simulith_42_cleanup();
    }
}
static void test_shared_ack_faults_disconnect_and_recovery(void)
{
    create_shared_owner();
    simulith_42_command_t command = wheel_command();
    for (int empty = 0; empty <= 1; empty++) {
        for (int bad = 0; bad < 3; bad++) {
            shared->ack_seq = bad == 0 ? 0 : bad == 1 ? 2 : 1;
            shared->ack_status = bad == 2 ? -1 : 0;
            TEST_ASSERT_EQUAL_INT(0, simulith_42_init(NULL, 0));
            TEST_ASSERT_EQUAL_INT(-1, empty ? simulith_42_send_empty_commands()
                                            : simulith_42_send_command_batch(&command, 1));
            TEST_ASSERT_EQUAL_INT(0, simulith_42_is_connected());
            TEST_ASSERT_EQUAL_UINT32(1, shared->command_seq);
            simulith_42_cleanup();
        }
    }
    shared->ack_seq = 1; shared->ack_status = 0;
    TEST_ASSERT_EQUAL_INT(0, simulith_42_init(NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, simulith_42_send_command_batch(&command, 1));
}
static void test_binary_header_fields_are_independently_validated(void)
{
    for (int bad = 0; bad < 4; bad++) {
        connect_tcp();
        shire_ipc_state_t state = valid_state();
        if (bad == 0) state.header.magic = 0;
        if (bad == 1) state.header.version++;
        if (bad == 2) state.header.type = SHIRE_IPC_ACK;
        if (bad == 3) state.header.payload_size--;
        put_peer(&state, sizeof(state));
        simulith_42_context_t context;
        TEST_ASSERT_EQUAL_INT(-1, simulith_42_request_state(&context));
        TEST_ASSERT_EQUAL_INT(0, simulith_42_is_connected());
        simulith_42_cleanup(); close(peer_fd); peer_fd = -1;
    }
    for (int empty = 0; empty <= 1; empty++) {
        for (int bad = 0; bad < 5; bad++) {
            connect_tcp();
            shire_ipc_ack_t ack = valid_ack();
            if (bad == 0) ack.header.magic = 0;
            if (bad == 1) ack.header.version++;
            if (bad == 2) ack.header.type = SHIRE_IPC_STATE;
            if (bad == 3) ack.header.payload_size--;
            if (bad == 4) ack.status = -1;
            put_peer(&ack, sizeof(ack));
            simulith_42_command_t command = wheel_command();
            TEST_ASSERT_EQUAL_INT(-1, empty ? simulith_42_send_empty_commands()
                                            : simulith_42_send_command_batch(&command, 1));
            TEST_ASSERT_EQUAL_INT(0, simulith_42_is_connected());
            simulith_42_cleanup(); close(peer_fd); peer_fd = -1;
        }
    }
}
static void test_fragmented_io_and_eintr_preserve_frames(void)
{
    connect_tcp(); fragment_io = 1; interrupt_read = 1;
    shire_ipc_state_t state = valid_state(); put_peer(&state, sizeof(state));
    simulith_42_context_t context;
    TEST_ASSERT_EQUAL_INT(0, simulith_42_request_state(&context));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 42.25, context.mass);
    interrupt_write = 1; interrupt_read = 1;
    shire_ipc_ack_t ack = valid_ack(); put_peer(&ack, sizeof(ack));
    simulith_42_command_t command = wheel_command();
    TEST_ASSERT_EQUAL_INT(0, simulith_42_send_command_batch(&command, 1));
    shire_ipc_commands_t batch = {0};
    size_t expected = SHIRE_IPC_COMMANDS_FRAME_SIZE(1);
    TEST_ASSERT_EQUAL_INT(expected, __real_recv(peer_fd, &batch, expected, MSG_WAITALL));
    TEST_ASSERT_EQUAL_UINT32(1, batch.count);
    TEST_ASSERT_EQUAL_MEMORY(command.cmd.wheel.torque, batch.commands[0].values,
                             sizeof(command.cmd.wheel.torque));
    TEST_ASSERT_EQUAL_INT(1, simulith_42_is_connected());
}
static void test_send_and_receive_errors_disconnect(void)
{
    for (int text = 0; text <= 1; text++) {
        setenv("FORTYTWO_IPC_MODE", text ? "text" : "binary", 1);
        for (int operation = 0; operation < 3; operation++) {
            connect_tcp();
            if (operation == 0) {
                fail_read = 1;
                simulith_42_context_t context;
                TEST_ASSERT_EQUAL_INT(-1, simulith_42_request_state(&context));
            } else {
                fail_write = 1;
                simulith_42_command_t command = wheel_command();
                TEST_ASSERT_EQUAL_INT(-1, operation == 1
                    ? simulith_42_send_command_batch(&command, 1) : simulith_42_send_empty_commands());
            }
            TEST_ASSERT_EQUAL_INT(0, simulith_42_is_connected());
            simulith_42_cleanup(); close(peer_fd); peer_fd = -1;
        }
    }
}
static void test_text_frame_bounds_parse_and_ack_failures(void)
{
    setenv("FORTYTWO_IPC_MODE", "text", 1);
    const char text[] = "TIME 2026-001-00:00:00.25\nSC[0].svb = [1 0 0]\n[ENDMSG]\n";
    for (int operation = 0; operation < 4; operation++) {
        connect_tcp();
        simulith_42_context_t context;
        if (operation == 0) {
            char oversized[16384]; memset(oversized, 'x', sizeof(oversized));
            put_peer(oversized, sizeof(oversized));
            TEST_ASSERT_EQUAL_INT(-1, simulith_42_request_state(&context));
        } else if (operation == 1) {
            put_peer(text, sizeof(text) - 1); fail_write = 1;
            TEST_ASSERT_EQUAL_INT(-1, simulith_42_request_state(&context));
        } else {
            put_peer("Bad!", 4);
            simulith_42_command_t command = wheel_command();
            TEST_ASSERT_EQUAL_INT(-1, operation == 2
                ? simulith_42_send_command_batch(&command, 1) : simulith_42_send_empty_commands());
        }
        TEST_ASSERT_EQUAL_INT(0, simulith_42_is_connected());
        simulith_42_cleanup(); close(peer_fd); peer_fd = -1;
    }
    connect_tcp(); interrupt_read = 1; interrupt_write = 1; fragment_io = 1;
    put_peer(text, sizeof(text) - 1);
    simulith_42_context_t context;
    TEST_ASSERT_EQUAL_INT(0, simulith_42_request_state(&context));
    TEST_ASSERT_EQUAL_INT(0, context.eclipse);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.25, context.sim_time);
    char ack[4];
    TEST_ASSERT_EQUAL_INT(4, __real_recv(peer_fd, ack, sizeof(ack), MSG_WAITALL));
    TEST_ASSERT_EQUAL_MEMORY("Ack", ack, 4);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_socket_creation_failure);
    RUN_TEST(test_shared_requires_live_owner_and_valid_header);
    RUN_TEST(test_shared_round_trip_and_reconnect_sequence);
    RUN_TEST(test_shared_wait_retries_interrupts_and_detects_sequence_loss);
    RUN_TEST(test_shared_ack_faults_disconnect_and_recovery);
    RUN_TEST(test_binary_header_fields_are_independently_validated);
    RUN_TEST(test_fragmented_io_and_eintr_preserve_frames);
    RUN_TEST(test_send_and_receive_errors_disconnect);
    RUN_TEST(test_text_frame_bounds_parse_and_ack_failures);
    return UNITY_END();
}

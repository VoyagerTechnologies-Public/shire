#include "simulith.h"
#include "simulith_shared_barrier.h"
#include "unity.h"
#include "test_sleep.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zmq.h>

typedef enum
{
    FAIL_NONE,
    FAIL_CONTEXT,
    FAIL_PUBLISHER,
    FAIL_RESPONDER,
    SCRIPT_HANDSHAKES,
    FAIL_BACKDOOR_SOCKET,
    FAIL_STATUS_SOCKET,
    FAIL_BACKDOOR_BIND,
    FAIL_BACKDOOR_FCNTL_GETFL,
    FAIL_STATUS_HOST_EMPTY_ADDR_LIST
} failure_t;

static failure_t failure;
static int socket_calls;
static int receive_calls;
static int close_calls;
static int term_calls;
static int send_calls;
static int raw_socket_calls;
static int raw_sendto_calls;

extern int __real_socket(int domain, int type, int protocol);
extern int __real_bind(int fd, const struct sockaddr *addr, socklen_t len);
extern int __real_fcntl(int fd, int cmd, ...);
extern struct hostent *__real_gethostbyname(const char *name);

int __wrap_socket(int domain, int type, int protocol)
{
    raw_socket_calls++;
    if (failure == FAIL_BACKDOOR_SOCKET && raw_socket_calls == 1)
    {
        errno = EMFILE;
        return -1;
    }
    if (failure == FAIL_STATUS_SOCKET && raw_socket_calls == 2)
    {
        errno = EMFILE;
        return -1;
    }
    return __real_socket(domain, type, protocol);
}

int __wrap_bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    if (failure == FAIL_BACKDOOR_BIND)
    {
        errno = EADDRINUSE;
        return -1;
    }
    return __real_bind(fd, addr, len);
}

int __wrap_fcntl(int fd, int cmd, ...)
{
    va_list args;
    va_start(args, cmd);
    int arg = va_arg(args, int);
    va_end(args);
    if (failure == FAIL_BACKDOOR_FCNTL_GETFL && cmd == F_GETFL)
        return -1;
    return __real_fcntl(fd, cmd, arg);
}

struct hostent *__wrap_gethostbyname(const char *name)
{
    if (failure == FAIL_STATUS_HOST_EMPTY_ADDR_LIST)
    {
        static struct hostent empty_addr_host;
        static char *empty_addr_list[1] = { NULL };
        memset(&empty_addr_host, 0, sizeof(empty_addr_host));
        empty_addr_host.h_addr_list = empty_addr_list;
        return &empty_addr_host;
    }
    return __real_gethostbyname(name);
}

ssize_t __wrap_sendto(int fd, const void *buffer, size_t length, int flags,
                      const struct sockaddr *dest_addr, socklen_t addrlen)
{
    (void)fd;
    (void)buffer;
    (void)flags;
    (void)dest_addr;
    (void)addrlen;
    raw_sendto_calls++;
    return (ssize_t)length;
}

void simulith_log(const char *format, ...)
{
    (void)format;
}

void *__wrap_zmq_ctx_new(void)
{
    return failure == FAIL_CONTEXT ? NULL : (void *)(uintptr_t)1;
}

int __wrap_zmq_ctx_term(void *context)
{
    (void)context;
    term_calls++;
    return 0;
}

void *__wrap_zmq_socket(void *context, int type)
{
    (void)context;
    (void)type;
    socket_calls++;
    if (failure == FAIL_PUBLISHER && socket_calls == 1)
        return NULL;
    if (failure == FAIL_RESPONDER && socket_calls == 2)
        return NULL;
    return (void *)(uintptr_t)(socket_calls + 1);
}

int __wrap_zmq_bind(void *socket, const char *endpoint)
{
    (void)socket;
    (void)endpoint;
    return 0;
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
    close_calls++;
    return 0;
}

int __wrap_zmq_send(void *socket, const void *buffer, size_t length, int flags)
{
    (void)socket;
    (void)buffer;
    (void)flags;
    send_calls++;
    return (int)length;
}

int __wrap_zmq_recv(void *socket, void *buffer, size_t length, int flags)
{
    (void)socket;
    (void)flags;
    static const char *const messages[] = {
        NULL,
        NULL,
        "BAD READY",
        "READY ",
        "READY triple 1 zmq trailing",
        "READY single trailing",
        "READY first",
        "READY second",
    };

    if (failure != SCRIPT_HANDSHAKES)
        return -1;

    int call = receive_calls++;
    if (call == 0) {
        errno = EAGAIN;
        return -1;
    }
    if (call == 1) {
        errno = EIO;
        return -1;
    }

    const char *message = messages[call];
    size_t message_length = strlen(message);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(length, message_length);
    memcpy(buffer, message, message_length);
    if (call == 7)
        simulith_server_request_stop();
    return (int)message_length;
}

void setUp(void)
{
    failure = FAIL_NONE;
    socket_calls = 0;
    receive_calls = 0;
    close_calls = 0;
    term_calls = 0;
    send_calls = 0;
    raw_socket_calls = 0;
    raw_sendto_calls = 0;
    unsetenv("SIMULITH_GSW_HOST");
    TEST_ASSERT_EQUAL_INT(0, simulith_server_configure(1.0, 0, NULL));
}

void tearDown(void)
{
    simulith_server_shutdown();

    TEST_ASSERT_EQUAL_INT(0, simulith_server_configure(0.0, 0, NULL));
    simulith_server_shutdown();
}

static void test_server_reports_shared_barrier_creation_failure(void)
{
    TEST_ASSERT_EQUAL_INT(0, mkdir(SIMULITH_SHARED_BARRIER_PATH, 0700));
    TEST_ASSERT_EQUAL_INT(-1,
                          simulith_server_init("pub", "ipc://rep", 1, 1));
    TEST_ASSERT_EQUAL_INT(0, rmdir(SIMULITH_SHARED_BARRIER_PATH));
}

static void test_server_context_creation_failure(void)
{
    failure = FAIL_CONTEXT;
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_init("pub", "rep", 1, 1));
    TEST_ASSERT_EQUAL_INT(0, close_calls);
    TEST_ASSERT_EQUAL_INT(0, term_calls);
}

static void test_server_rejects_invalid_endpoints_and_metrics_path(void)
{
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_init(NULL, "rep", 1, 1));
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_init("pub", NULL, 1, 1));

    char oversized[513];
    memset(oversized, 'x', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_configure(1.0, 0, oversized));

    TEST_ASSERT_EQUAL_INT(0, simulith_server_configure(
                                 1.0, 0,
                                 "/tmp/shire-missing-directory/metrics.json"));
    simulith_server_shutdown();
}

static void test_server_publisher_creation_failure_cleans_context(void)
{
    failure = FAIL_PUBLISHER;
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_init("pub", "rep", 1, 1));
    TEST_ASSERT_EQUAL_INT(0, close_calls);
    TEST_ASSERT_EQUAL_INT(1, term_calls);
}

static void test_server_responder_creation_failure_cleans_publisher(void)
{
    failure = FAIL_RESPONDER;
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_init("pub", "rep", 1, 1));
    TEST_ASSERT_EQUAL_INT(1, close_calls);
    TEST_ASSERT_EQUAL_INT(1, term_calls);
}

static void test_server_rejects_malformed_handshakes_before_two_clients_join(void)
{
    failure = SCRIPT_HANDSHAKES;
    TEST_ASSERT_EQUAL_INT(0, simulith_server_init("pub", "rep", 2, 1));
    simulith_server_run();
    TEST_ASSERT_EQUAL_INT(8, receive_calls);
    /* Four malformed handshakes, two READY acknowledgements, and STOP. */
    TEST_ASSERT_EQUAL_INT(7, send_calls);
}

static void test_server_backdoor_socket_reports_creation_failure(void)
{
    failure = FAIL_BACKDOOR_SOCKET;
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_ensure_backdoor_socket_for_test());
}

static void test_server_backdoor_socket_reports_bind_failure(void)
{
    failure = FAIL_BACKDOOR_BIND;
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_ensure_backdoor_socket_for_test());
}

static void test_server_backdoor_socket_survives_fcntl_failure(void)
{
    /* A fcntl(F_GETFL) failure only skips O_NONBLOCK; the socket is still
     * created and reported as usable. */
    failure = FAIL_BACKDOOR_FCNTL_GETFL;
    TEST_ASSERT_EQUAL_INT(0, simulith_server_ensure_backdoor_socket_for_test());
}

static void test_server_status_socket_reports_creation_failure(void)
{
    /* The first raw socket() call belongs to the backdoor socket; let it
     * succeed and fail only the second (status) call. */
    TEST_ASSERT_EQUAL_INT(0, simulith_server_ensure_backdoor_socket_for_test());
    failure = FAIL_STATUS_SOCKET;
    simulith_server_ensure_status_socket_for_test();

    /* A missing status link must disable outbound telemetry rather than
     * crash: send_status_update() becomes a silent no-op. */
    simulith_server_send_status_update_for_test(1, 2.0);
    TEST_ASSERT_EQUAL_INT(0, raw_sendto_calls);
}

static void test_server_status_socket_resolves_host_with_empty_address_list(void)
{
    /* A resolver that returns a non-NULL hostent with no addresses must
     * still fall back to sending, rather than leaving the destination
     * unset: send_status_update() reaches sendto() either way. */
    setenv("SIMULITH_GSW_HOST", "shire-gsw-empty", 1);
    failure = FAIL_STATUS_HOST_EMPTY_ADDR_LIST;
    simulith_server_ensure_status_socket_for_test();
    failure = FAIL_NONE;

    simulith_server_send_status_update_for_test(0, 1.0);
    TEST_ASSERT_EQUAL_INT(1, raw_sendto_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_server_context_creation_failure);
    RUN_TEST(test_server_rejects_invalid_endpoints_and_metrics_path);
    RUN_TEST(test_server_reports_shared_barrier_creation_failure);
    RUN_TEST(test_server_publisher_creation_failure_cleans_context);
    RUN_TEST(test_server_responder_creation_failure_cleans_publisher);
    RUN_TEST(test_server_rejects_malformed_handshakes_before_two_clients_join);
    RUN_TEST(test_server_backdoor_socket_reports_creation_failure);
    RUN_TEST(test_server_backdoor_socket_reports_bind_failure);
    RUN_TEST(test_server_backdoor_socket_survives_fcntl_failure);
    RUN_TEST(test_server_status_socket_reports_creation_failure);
    RUN_TEST(test_server_status_socket_resolves_host_with_empty_address_list);
    return UNITY_END();
}

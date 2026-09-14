#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include "simulith_transport.h"
#include "test_sleep.h"
#include "unity.h"

static transport_port_t transport_a_ports[8];
static transport_port_t transport_b_ports[8];

typedef struct
{
    transport_port_t *port;
    uint8_t expected[8];
    size_t expected_len;
    uint8_t response[8];
    size_t response_len;
    int receive_result;
    int complete_result;
} request_server_args_t;

typedef struct
{
    transport_port_t *port;
    uint8_t first[8];
    size_t first_len;
    uint8_t second[8];
    size_t second_len;
    int send_stale_ack;
} exact_sender_args_t;

static void *send_exact_parts_thread(void *argument)
{
    exact_sender_args_t *sender = argument;
    test_sleep_us(2000);
    if (sender->send_stale_ack)
        (void)simulith_transport_complete_request(
            sender->port, 99, SIMULITH_TRANSPORT_SUCCESS);
    (void)simulith_transport_send(sender->port, sender->first, sender->first_len);
    test_sleep_us(2000);
    (void)simulith_transport_send(sender->port, sender->second, sender->second_len);
    return NULL;
}

static void *complete_request_thread(void *arg)
{
    request_server_args_t *server = arg;
    uint8_t received[8];
    uint64_t transaction_id = 0;

    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        server->receive_result = simulith_transport_receive_request(
            server->port, received, sizeof(received), &transaction_id);
        if (server->receive_result != 0)
            break;
        test_sleep_us(1000);
    }

    if (server->receive_result == (int)server->expected_len &&
        memcmp(received, server->expected, server->expected_len) == 0)
    {
        if (server->response_len > 0)
            (void)simulith_transport_send(server->port, server->response,
                                          server->response_len);
        server->complete_result = simulith_transport_complete_request(
            server->port, transaction_id, SIMULITH_TRANSPORT_SUCCESS);
    }
    return NULL;
}

void setUp(void)
{
    memset(transport_a_ports, 0, sizeof(transport_a_ports));
    memset(transport_b_ports, 0, sizeof(transport_b_ports));
}

void tearDown(void)
{
    for (int i = 0; i < 8; i++) {
        simulith_transport_close(&transport_a_ports[i]);
        simulith_transport_close(&transport_b_ports[i]);
    }
}

static void test_transport_init(void)
{
    int result;

    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR, simulith_transport_init(NULL));

    /* Example: port 0, A (server) and B (client) */
    strcpy(transport_a_ports[0].name, "tp0_a");
    strcpy(transport_a_ports[0].address, LOCAL_PUB_ADDR);
    transport_a_ports[0].is_server = 1;
    result = simulith_transport_init(&transport_a_ports[0]);
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, result);
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, simulith_transport_init(&transport_a_ports[0]));

    strcpy(transport_b_ports[0].name, "tp0_b");
    strcpy(transport_b_ports[0].address, LOCAL_PUB_ADDR);
    transport_b_ports[0].is_server = 0;
    result = simulith_transport_init(&transport_b_ports[0]);
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, result);

    /* Example: port 1, A (server) and B (client) */
    strcpy(transport_a_ports[1].name, "tp1_a");
    strcpy(transport_a_ports[1].address, LOCAL_REP_ADDR);
    transport_a_ports[1].is_server = 1;
    result = simulith_transport_init(&transport_a_ports[1]);
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, result);

    strcpy(transport_b_ports[1].name, "tp1_b");
    strcpy(transport_b_ports[1].address, LOCAL_REP_ADDR);
    transport_b_ports[1].is_server = 0;
    result = simulith_transport_init(&transport_b_ports[1]);
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, result);
    TEST_ASSERT_EQUAL_PTR(transport_a_ports[0].zmq_ctx,
                          transport_b_ports[0].zmq_ctx);
    TEST_ASSERT_EQUAL_PTR(transport_a_ports[0].zmq_ctx,
                          transport_a_ports[1].zmq_ctx);

    /* Last port pair */
    int last = 8 - 1;
    sprintf(transport_a_ports[last].name, "tp%d_a", last);
    sprintf(transport_a_ports[last].address, "ipc:///tmp/simulith_pub:%d", 7000 + last);
    transport_a_ports[last].is_server = 1;
    result = simulith_transport_init(&transport_a_ports[last]);
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, result);

    sprintf(transport_b_ports[last].name, "tp%d_b", last);
    sprintf(transport_b_ports[last].address, "ipc:///tmp/simulith_pub:%d", 7000 + last);
    transport_b_ports[last].is_server = 0;
    result = simulith_transport_init(&transport_b_ports[last]);
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, result);
}

static void test_transport_receive_exact_partial_timeout_and_stale_ack(void)
{
    transport_port_t server = {0};
    transport_port_t client = {0};
    strcpy(server.name, "exact_server");
    strcpy(server.address, "ipc:///tmp/simulith_pub:7011");
    server.is_server = 1;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          simulith_transport_init(&server));
    strcpy(client.name, "exact_client");
    strcpy(client.address, server.address);
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          simulith_transport_init(&client));
    TEST_ASSERT_EQUAL_PTR(server.zmq_ctx, client.zmq_ctx);

    exact_sender_args_t sender = {
        .port = &server,
        .first = {1, 2}, .first_len = 2,
        .second = {3, 4, 5}, .second_len = 3,
        .send_stale_ack = 1,
    };
    pthread_t thread;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&thread, NULL,
                                            send_exact_parts_thread, &sender));
    uint8_t received[5] = {0};
    TEST_ASSERT_EQUAL_INT(5, simulith_transport_receive_exact(
                                 &client, received, sizeof(received), 1000));
    static const uint8_t expected[] = {1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, received, sizeof(expected));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(thread, NULL));

    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_receive_exact(
                              &client, received, 1, 1));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_receive_exact(
                              &client, received,
                              SIMULITH_TRANSPORT_BUFFER_SIZE + 1U, 1));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          simulith_transport_close(&server));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          simulith_transport_close(&client));
}

static void test_transport_wait_for_request_and_interrupt(void)
{
    transport_port_t server = {0};
    transport_port_t client = {0};
    strcpy(server.name, "wait_server");
    strcpy(server.address, "ipc:///tmp/simulith_pub:7012");
    server.is_server = 1;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          simulith_transport_init(&server));
    strcpy(client.name, "wait_client");
    strcpy(client.address, server.address);
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          simulith_transport_init(&client));
    int interrupt_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, interrupt_fd);
    transport_port_t *ports[] = {&server};

    test_sleep_us(1000);
    static const uint8_t request[] = {0x31, 0x32};
    TEST_ASSERT_EQUAL_INT((int)sizeof(request), simulith_transport_send(
                                                    &client, request,
                                                    sizeof(request)));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_READY,
                          simulith_transport_wait_for_request(
                              ports, 1U, interrupt_fd));
    uint8_t received[4] = {0};
    uint64_t transaction_id = UINT64_MAX;
    TEST_ASSERT_EQUAL_INT((int)sizeof(request),
                          simulith_transport_receive_request(
                              &server, received, sizeof(received),
                              &transaction_id));
    TEST_ASSERT_EQUAL_UINT64(0, transaction_id);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(request, received, sizeof(request));

    uint64_t wake = 1;
    TEST_ASSERT_EQUAL_INT((int)sizeof(wake),
                          (int)write(interrupt_fd, &wake, sizeof(wake)));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_INTERRUPTED,
                          simulith_transport_wait_for_request(
                              ports, 1U, interrupt_fd));

    /* If COMMIT and a request become ready together, interruption wins but
     * the transport frame must remain queued for a later EXECUTE. */
    TEST_ASSERT_EQUAL_INT((int)sizeof(request), simulith_transport_send(
                                                    &client, request,
                                                    sizeof(request)));
    TEST_ASSERT_EQUAL_INT((int)sizeof(wake),
                          (int)write(interrupt_fd, &wake, sizeof(wake)));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_INTERRUPTED,
                          simulith_transport_wait_for_request(
                              ports, 1U, interrupt_fd));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_READY,
                          simulith_transport_wait_for_request(
                              ports, 1U, interrupt_fd));
    TEST_ASSERT_EQUAL_INT((int)sizeof(request),
                          simulith_transport_receive_request(
                              &server, received, sizeof(received),
                              &transaction_id));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_wait_for_request(
                              NULL, 1U, interrupt_fd));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_wait_for_request(
                              ports, 0U, interrupt_fd));

    close(interrupt_fd);
    simulith_transport_close(&server);
    simulith_transport_close(&client);
}

static void test_transport_invalid_addresses(void)
{
    transport_port_t bind_port = {0};
    strcpy(bind_port.name, "bad_bind");
    strcpy(bind_port.address, "invalid://bind");
    bind_port.is_server = 1;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR, simulith_transport_init(&bind_port));

    transport_port_t connect_port = {0};
    strcpy(connect_port.address, "invalid://connect");
    connect_port.is_server = 0;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR, simulith_transport_init(&connect_port));
}

static void test_transport_send_receive(void)
{
    int result;
    uint8_t test_data[] = {0xAA, 0xBB, 0xCC};
    uint8_t rx_data[sizeof(test_data)];

    /* Initialize port 0 pair */
    strcpy(transport_a_ports[0].name, "tp0_a");
    strcpy(transport_a_ports[0].address, LOCAL_PUB_ADDR);
    transport_a_ports[0].is_server = 1;
    result = simulith_transport_init(&transport_a_ports[0]);
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, result);

    strcpy(transport_b_ports[0].name, "tp0_b");
    strcpy(transport_b_ports[0].address, LOCAL_PUB_ADDR);
    transport_b_ports[0].is_server = 0;
    result = simulith_transport_init(&transport_b_ports[0]);
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, result);

    /* Allow ZMQ to establish connection */
    test_sleep_us(1000);

    /* Send from A to B */
    result = simulith_transport_send(&transport_a_ports[0], test_data, sizeof(test_data));
    TEST_ASSERT_EQUAL(sizeof(test_data), result);
    test_sleep_us(1000);

    /* Confirm available on B */
    result = simulith_transport_available(&transport_b_ports[0]);
    TEST_ASSERT_TRUE(result == 1);

    /* Receive on B */
    result = simulith_transport_receive(&transport_b_ports[0], rx_data, sizeof(rx_data));
    TEST_ASSERT_EQUAL(sizeof(test_data), result);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(test_data, rx_data, sizeof(test_data));

    /* Send back from B to A */
    result = simulith_transport_send(&transport_b_ports[0], rx_data, sizeof(rx_data));
    TEST_ASSERT_EQUAL(sizeof(rx_data), result);
    test_sleep_us(1000);

    result = simulith_transport_available(&transport_a_ports[0]);
    TEST_ASSERT_TRUE(result == 1);

    result = simulith_transport_receive(&transport_a_ports[0], rx_data, sizeof(rx_data));
    TEST_ASSERT_EQUAL(sizeof(test_data), result);
}

static void test_transport_request_completion(void)
{
    static const uint8_t request[] = {0x10, 0x20, 0x30, 0x40};
    static const uint8_t response[] = {0x50, 0x60, 0x70};
    request_server_args_t server = {0};
    pthread_t thread;

    strcpy(transport_a_ports[1].name, "request_server");
    strcpy(transport_a_ports[1].address, "ipc:///tmp/simulith_pub:7001");
    transport_a_ports[1].is_server = 1;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS,
                      simulith_transport_init(&transport_a_ports[1]));

    strcpy(transport_b_ports[1].name, "request_client");
    strcpy(transport_b_ports[1].address, transport_a_ports[1].address);
    transport_b_ports[1].is_server = 0;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS,
                      simulith_transport_init(&transport_b_ports[1]));

    server.port = &transport_a_ports[1];
    memcpy(server.expected, request, sizeof(request));
    server.expected_len = sizeof(request);
    memcpy(server.response, response, sizeof(response));
    server.response_len = sizeof(response);
    server.receive_result = SIMULITH_TRANSPORT_ERROR;
    server.complete_result = SIMULITH_TRANSPORT_ERROR;

    TEST_ASSERT_EQUAL_INT(0, pthread_create(&thread, NULL, complete_request_thread, &server));
    test_sleep_us(1000);
    TEST_ASSERT_EQUAL_INT((int)sizeof(request),
                          simulith_transport_request(&transport_b_ports[1], request,
                                                     sizeof(request), 1000));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(thread, NULL));
    TEST_ASSERT_EQUAL_INT((int)sizeof(request), server.receive_result);
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, server.complete_result);
    TEST_ASSERT_EQUAL_INT(1, simulith_transport_available(&transport_b_ports[1]));
    uint8_t received_response[sizeof(response)] = {0};
    TEST_ASSERT_EQUAL_INT((int)sizeof(response),
                          simulith_transport_receive(&transport_b_ports[1], received_response,
                                                     sizeof(received_response)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(response, received_response, sizeof(response));
}

static void test_transport_request_timeout(void)
{
    static const uint8_t request[] = {0x55};

    strcpy(transport_a_ports[5].name, "timeout_server");
    strcpy(transport_a_ports[5].address, "ipc:///tmp/simulith_pub:7005");
    transport_a_ports[5].is_server = 1;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS,
                      simulith_transport_init(&transport_a_ports[5]));

    strcpy(transport_b_ports[5].name, "timeout_client");
    strcpy(transport_b_ports[5].address, transport_a_ports[5].address);
    transport_b_ports[5].is_server = 0;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS,
                      simulith_transport_init(&transport_b_ports[5]));
    test_sleep_us(1000);

    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_request(&transport_b_ports[5], request,
                                                     sizeof(request), 1));
}

static void test_transport_buffer_overflow(void)
{
    /* Initialize a pair */
    strcpy(transport_a_ports[2].name, "tp2_a");
    strcpy(transport_a_ports[2].address, "ipc:///tmp/simulith_pub:7002");
    transport_a_ports[2].is_server = 1;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, simulith_transport_init(&transport_a_ports[2]));

    strcpy(transport_b_ports[2].name, "tp2_b");
    strcpy(transport_b_ports[2].address, "ipc:///tmp/simulith_pub:7002");
    transport_b_ports[2].is_server = 0;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, simulith_transport_init(&transport_b_ports[2]));

    /* Big payload larger than rx buffer */
    size_t big = SIMULITH_TRANSPORT_BUFFER_SIZE + 100;
    uint8_t *bigbuf = malloc(big);
    TEST_ASSERT_NOT_NULL(bigbuf);
    memset(bigbuf, 0xFF, big);

    test_sleep_us(1000);
    int sent = simulith_transport_send(&transport_b_ports[2], bigbuf, big);
    /* send may succeed or fail depending on ZMQ state; we just ensure server won't buffer it */
    (void)sent;

    int available = 0;
    for (int i = 0; i < 200; ++i) {
        available = simulith_transport_available(&transport_a_ports[2]);
        if (available) break;
        test_sleep_us(1000);
    }

    /* The implementation will drop oversized messages; expect no buffered data */
    TEST_ASSERT_EQUAL(0, available);
    free(bigbuf);
}

static void test_transport_uninitialized_send(void)
{
    transport_port_t uninit = {0};
    uint8_t buf[4] = {1,2,3,4};
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR,
                      simulith_transport_send(NULL, buf, sizeof(buf)));
    int rc = simulith_transport_send(&uninit, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR, rc);

    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR,
                      simulith_transport_receive(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR,
                      simulith_transport_receive(&uninit, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR,
                      simulith_transport_available(NULL));
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR,
                      simulith_transport_available(&uninit));
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR,
                      simulith_transport_flush(NULL));
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR,
                      simulith_transport_flush(&uninit));
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR,
                      simulith_transport_close(NULL));

    transport_port_t initialized_empty = {.init = SIMULITH_TRANSPORT_INITIALIZED};
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_receive(&initialized_empty, buf, sizeof(buf)));
}

static void test_transport_multiple_messages(void)
{
    /* Initialize a pair */
    strcpy(transport_a_ports[3].name, "tp3_a");
    strcpy(transport_a_ports[3].address, "ipc:///tmp/simulith_pub:7003");
    transport_a_ports[3].is_server = 1;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, simulith_transport_init(&transport_a_ports[3]));

    strcpy(transport_b_ports[3].name, "tp3_b");
    strcpy(transport_b_ports[3].address, "ipc:///tmp/simulith_pub:7003");
    transport_b_ports[3].is_server = 0;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, simulith_transport_init(&transport_b_ports[3]));

    const char *msgs[] = {"one","two","three"};
    for (int i = 0; i < 3; ++i) {
        int sent = simulith_transport_send(&transport_b_ports[3], (const uint8_t*)msgs[i], strlen(msgs[i]));
        TEST_ASSERT_EQUAL((int)strlen(msgs[i]), sent);
        test_sleep_us(1000);
    }

    /* Receive sequentially */
    char buf[32];
    for (int i = 0; i < 3; ++i) {
        int available = 0;
        for (int j = 0; j < 200; ++j) {
            available = simulith_transport_available(&transport_a_ports[3]);
            if (available) break;
            test_sleep_us(1000);
        }
        TEST_ASSERT_TRUE(available == 1);
        int r = simulith_transport_receive(&transport_a_ports[3], (uint8_t*)buf, sizeof(buf));
        TEST_ASSERT_EQUAL((int)strlen(msgs[i]), r);
        buf[r] = '\0';
        TEST_ASSERT_EQUAL_STRING(msgs[i], buf);
    }
}

static void test_transport_partial_receive(void)
{
    /* Initialize a pair */
    strcpy(transport_a_ports[4].name, "tp4_a");
    strcpy(transport_a_ports[4].address, "ipc:///tmp/simulith_pub:7004");
    transport_a_ports[4].is_server = 1;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, simulith_transport_init(&transport_a_ports[4]));

    strcpy(transport_b_ports[4].name, "tp4_b");
    strcpy(transport_b_ports[4].address, "ipc:///tmp/simulith_pub:7004");
    transport_b_ports[4].is_server = 0;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, simulith_transport_init(&transport_b_ports[4]));

    const size_t total = 100;
    uint8_t *big = malloc(total);
    TEST_ASSERT_NOT_NULL(big);
    for (size_t i = 0; i < total; ++i) big[i] = (uint8_t)(i & 0xFF);

    test_sleep_us(1000);
    int sent = simulith_transport_send(&transport_b_ports[4], big, total);
    TEST_ASSERT_EQUAL((int)total, sent);

    /* read a portion */
    uint8_t part[40];
    int avail = 0;
    for (int j = 0; j < 200; ++j) {
        avail = simulith_transport_available(&transport_a_ports[4]);
        if (avail) break;
        test_sleep_us(1000);
    }
    TEST_ASSERT_TRUE(avail == 1);
    int r1 = simulith_transport_receive(&transport_a_ports[4], part, sizeof(part));
    TEST_ASSERT_EQUAL((int)sizeof(part), r1);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(big, part, sizeof(part));

    /* remaining should be total - sizeof(part) */
    int avail2 = simulith_transport_available(&transport_a_ports[4]);
    TEST_ASSERT_TRUE(avail2 == 1);
    uint8_t rest[128];
    int r2 = simulith_transport_receive(&transport_a_ports[4], rest, sizeof(rest));
    TEST_ASSERT_EQUAL((int)(total - sizeof(part)), r2);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(big + sizeof(part), rest, r2);

    free(big);
}

static void test_transport_flush(void)
{
    transport_port_t a = {0};
    transport_port_t b = {0};
    strcpy(a.name, "flush_a");
    strcpy(a.address, "ipc:///tmp/simulith_pub:7010");
    a.is_server = 1;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, simulith_transport_init(&a));

    strcpy(b.name, "flush_b");
    strcpy(b.address, a.address);
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, simulith_transport_init(&b));
    test_sleep_us(1000);
    static const uint8_t message[] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT((int)sizeof(message),
                          simulith_transport_send(&b, message, sizeof(message)));
    test_sleep_us(1000);
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, simulith_transport_flush(&a));
    simulith_transport_close(&a);
    simulith_transport_close(&b);
}

static void test_transport_close_uninitialized(void)
{
    transport_port_t p = {0};
    int rc = simulith_transport_close(&p);
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR, rc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_transport_init);
    RUN_TEST(test_transport_receive_exact_partial_timeout_and_stale_ack);
    RUN_TEST(test_transport_wait_for_request_and_interrupt);
    RUN_TEST(test_transport_invalid_addresses);
    RUN_TEST(test_transport_send_receive);
    RUN_TEST(test_transport_request_completion);
    RUN_TEST(test_transport_request_timeout);
    RUN_TEST(test_transport_buffer_overflow);
    RUN_TEST(test_transport_uninitialized_send);
    RUN_TEST(test_transport_multiple_messages);
    RUN_TEST(test_transport_partial_receive);
    RUN_TEST(test_transport_flush);
    RUN_TEST(test_transport_close_uninitialized);
    return UNITY_END();
}

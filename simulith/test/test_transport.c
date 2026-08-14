#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "simulith_transport.h"
#include "unity.h"

static transport_port_t transport_a_ports[8];
static transport_port_t transport_b_ports[8];

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

    /* Example: port 0, A (server) and B (client) */
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
    usleep(1000);

    /* Send from A to B */
    result = simulith_transport_send(&transport_a_ports[0], test_data, sizeof(test_data));
    TEST_ASSERT_EQUAL(sizeof(test_data), result);
    usleep(1000);

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
    usleep(1000);

    result = simulith_transport_available(&transport_a_ports[0]);
    TEST_ASSERT_TRUE(result == 1);

    result = simulith_transport_receive(&transport_a_ports[0], rx_data, sizeof(rx_data));
    TEST_ASSERT_EQUAL(sizeof(test_data), result);
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

    usleep(1000);
    int sent = simulith_transport_send(&transport_b_ports[2], bigbuf, big);
    /* send may succeed or fail depending on ZMQ state; we just ensure server won't buffer it */
    (void)sent;

    int available = 0;
    for (int i = 0; i < 200; ++i) {
        available = simulith_transport_available(&transport_a_ports[2]);
        if (available) break;
        usleep(1000);
    }

    /* The implementation will drop oversized messages; expect no buffered data */
    TEST_ASSERT_EQUAL(0, available);
    free(bigbuf);
}

static void test_transport_uninitialized_send(void)
{
    transport_port_t uninit = {0};
    uint8_t buf[4] = {1,2,3,4};
    int rc = simulith_transport_send(&uninit, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_ERROR, rc);
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
        usleep(1000);
    }

    /* Receive sequentially */
    char buf[32];
    for (int i = 0; i < 3; ++i) {
        int available = 0;
        for (int j = 0; j < 200; ++j) {
            available = simulith_transport_available(&transport_a_ports[3]);
            if (available) break;
            usleep(1000);
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

    usleep(1000);
    int sent = simulith_transport_send(&transport_b_ports[4], big, total);
    TEST_ASSERT_EQUAL((int)total, sent);

    /* read a portion */
    uint8_t part[40];
    int avail = 0;
    for (int j = 0; j < 200; ++j) {
        avail = simulith_transport_available(&transport_a_ports[4]);
        if (avail) break;
        usleep(1000);
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
    strcpy(a.name, "flush_a");
    strcpy(a.address, "ipc:///tmp/simulith_pub:7010");
    a.is_server = 1;
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, simulith_transport_init(&a));
    TEST_ASSERT_EQUAL(SIMULITH_TRANSPORT_SUCCESS, simulith_transport_flush(&a));
    simulith_transport_close(&a);
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
    RUN_TEST(test_transport_send_receive);
    RUN_TEST(test_transport_buffer_overflow);
    RUN_TEST(test_transport_uninitialized_send);
    RUN_TEST(test_transport_multiple_messages);
    RUN_TEST(test_transport_partial_receive);
    RUN_TEST(test_transport_flush);
    RUN_TEST(test_transport_close_uninitialized);
    return UNITY_END();
}

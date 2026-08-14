#include "unity.h"
#include <zmq.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "simulith_time.h"
#include "simulith.h"

void setUp(void) { }
void tearDown(void) { }

static void test_time_init_get_wait_cleanup(void)
{
    // Create a PUB socket to send a tick to LOCAL_PUB_ADDR
    void* ctx = zmq_ctx_new();
    TEST_ASSERT_NOT_NULL(ctx);

    void* pub = zmq_socket(ctx, ZMQ_PUB);
    TEST_ASSERT_NOT_NULL(pub);

    int rc = zmq_bind(pub, LOCAL_PUB_ADDR);
    TEST_ASSERT_EQUAL_INT(0, rc);

    // Initialize the time provider which connects to LOCAL_PUB_ADDR
    void* handle = simulith_time_init();
    TEST_ASSERT_NOT_NULL(handle);

    // Send a tick (uint64_t) through the PUB socket
    uint64_t tick = 1;
    // Sleep briefly to allow subscriber to connect
    usleep(1000);

    int s = zmq_send(pub, &tick, sizeof(tick), 0);
    TEST_ASSERT_EQUAL_INT(sizeof(tick), s);

    // Wait for next tick via provider
    rc = simulith_time_wait_for_next_tick(handle);
    TEST_ASSERT_EQUAL_INT(0, rc);

    double t = simulith_time_get(handle);
    TEST_ASSERT_TRUE(t >= 0.0);

    simulith_time_cleanup(handle);

    zmq_close(pub);
    zmq_ctx_destroy(ctx);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_time_init_get_wait_cleanup);
    return UNITY_END();
}

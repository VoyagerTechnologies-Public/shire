#include "unity.h"
#include <zmq.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "simulith_time.h"
#include "simulith.h"
#include "test_sleep.h"

void setUp(void) { }
void tearDown(void) { }

static void test_time_init_get_wait_cleanup(void)
{
    TEST_ASSERT_TRUE(simulith_time_get(NULL) == 0.0);
    TEST_ASSERT_EQUAL_INT(-1, simulith_time_wait_for_next_tick(NULL));
    simulith_time_cleanup(NULL);

    // Create a PUB socket to send a tick to LOCAL_PUB_ADDR
    void* ctx = zmq_ctx_new();
    TEST_ASSERT_NOT_NULL(ctx);

    /* XPUB reports each subscription it receives, which is the only reliable signal that a PUB message
     * will not be dropped (a fixed sleep loses the "slow joiner" race under load). */
    void* pub = zmq_socket(ctx, ZMQ_XPUB);
    TEST_ASSERT_NOT_NULL(pub);
    int subscription_timeout_ms = 10000;
    TEST_ASSERT_EQUAL_INT(0, zmq_setsockopt(pub, ZMQ_RCVTIMEO, &subscription_timeout_ms,
                                            sizeof(subscription_timeout_ms)));

    int rc = zmq_bind(pub, LOCAL_PUB_ADDR);
    TEST_ASSERT_EQUAL_INT(0, rc);

    // Initialize the time provider which connects to LOCAL_PUB_ADDR
    void* handle = simulith_time_init();
    TEST_ASSERT_NOT_NULL(handle);

    // Send a tick (uint64_t) through the PUB socket once the provider has subscribed
    uint64_t tick = 1;
    unsigned char subscription[8];
    TEST_ASSERT_EQUAL_INT(1, zmq_recv(pub, subscription, sizeof(subscription), 0));
    TEST_ASSERT_EQUAL_UINT8(1, subscription[0]); /* 1 = subscribe */

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

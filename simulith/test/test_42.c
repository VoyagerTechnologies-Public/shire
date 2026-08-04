#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "simulith_transport.h"
#include "unity.h"
#include "simulith_42_commands.h"

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

static void test_enqueue_dequeue_basic(void)
{
    simulith_42_command_t cmd_in = {0};
    simulith_42_command_t cmd_out = {0};
    int rc;

    cmd_in.type = SIMULITH_42_CMD_MTB_TORQUE;
    cmd_in.spacecraft_id = 3;
    cmd_in.valid = 1;
    cmd_in.cmd.mtb.dipole[0] = 1.1;
    cmd_in.cmd.mtb.dipole[1] = 2.2;
    cmd_in.cmd.mtb.dipole[2] = 3.3;
    cmd_in.cmd.mtb.enable_mask = 0x5;

    rc = enqueue_command(&cmd_in);
    TEST_ASSERT_EQUAL_INT(0, rc);

    rc = dequeue_command(&cmd_out);
    TEST_ASSERT_EQUAL_INT(0, rc);

    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_MTB_TORQUE, cmd_out.type);
    TEST_ASSERT_EQUAL_INT(3, cmd_out.spacecraft_id);
    TEST_ASSERT_EQUAL_INT(1, cmd_out.valid);
    TEST_ASSERT_EQUAL_INT(0x5, cmd_out.cmd.mtb.enable_mask);
    TEST_ASSERT_EQUAL_FLOAT((float)1.1, (float)cmd_out.cmd.mtb.dipole[0]);
    TEST_ASSERT_EQUAL_FLOAT((float)2.2, (float)cmd_out.cmd.mtb.dipole[1]);
    TEST_ASSERT_EQUAL_FLOAT((float)3.3, (float)cmd_out.cmd.mtb.dipole[2]);
}

static void test_queue_overflow(void)
{
    // Fill the queue completely, then one more should fail
    simulith_42_command_t tmp = {0};
    int i;
    int rc;

    tmp.type = SIMULITH_42_CMD_NONE;
    tmp.valid = 0;

    for (i = 0; i < SIMULITH_42_CMD_QUEUE_SIZE; ++i) {
        rc = enqueue_command(&tmp);
        TEST_ASSERT_EQUAL_INT(0, rc);
    }
    // queue now full
    rc = enqueue_command(&tmp);
    TEST_ASSERT_EQUAL_INT(-1, rc);

    // drain queue
    for (i = 0; i < SIMULITH_42_CMD_QUEUE_SIZE; ++i) {
        rc = dequeue_command(&tmp);
        TEST_ASSERT_EQUAL_INT(0, rc);
    }
    // now empty
    rc = dequeue_command(&tmp);
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

static void test_helper_wrappers(void)
{
    double dipole[3] = {0.1, 0.2, 0.3};
    double torque4[4] = {1.0, 2.0, 3.0, 4.0};
    double thrust[3] = {0.4, 0.5, 0.6};
    double ttorque[3] = {0.7, 0.8, 0.9};
    int rc;
    simulith_42_command_t out = {0};

    rc = simulith_42_send_mtb_command(1, dipole, 0x3);
    TEST_ASSERT_EQUAL_INT(0, rc);

    rc = dequeue_command(&out);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_MTB_TORQUE, out.type);
    TEST_ASSERT_EQUAL_INT(1, out.spacecraft_id);
    TEST_ASSERT_EQUAL_FLOAT((float)dipole[0], (float)out.cmd.mtb.dipole[0]);

    rc = simulith_42_send_wheel_command(2, torque4, 0xF);
    TEST_ASSERT_EQUAL_INT(0, rc);
    rc = dequeue_command(&out);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_WHEEL_TORQUE, out.type);
    TEST_ASSERT_EQUAL_INT(2, out.spacecraft_id);
    TEST_ASSERT_EQUAL_FLOAT((float)torque4[3], (float)out.cmd.wheel.torque[3]);

    rc = simulith_42_send_thruster_command(4, thrust, ttorque, 0x1);
    TEST_ASSERT_EQUAL_INT(0, rc);
    rc = dequeue_command(&out);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_THRUSTER, out.type);
    TEST_ASSERT_EQUAL_INT(4, out.spacecraft_id);
    TEST_ASSERT_EQUAL_FLOAT((float)ttorque[2], (float)out.cmd.thruster.torque[2]);
}

static void test_set_mode_extra_and_defaults(void)
{
    // Test sending set_mode with NULL extra (defaults)
    int rc;
    simulith_42_command_t out = {0};

    rc = simulith_42_send_set_mode(7, 42, NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);
    rc = dequeue_command(&out);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_SET_MODE, out.type);
    TEST_ASSERT_EQUAL_INT(7, out.spacecraft_id);
    TEST_ASSERT_EQUAL_INT(42, out.cmd.setmode.mode);
    TEST_ASSERT_EQUAL_INT(0, out.cmd.setmode.have_pri);
    TEST_ASSERT_EQUAL_INT(0, out.cmd.setmode.have_sec);
    TEST_ASSERT_EQUAL_INT(0, out.cmd.setmode.have_qrn);

    // Now craft an extra that sets pri and qrn flags and values
    simulith_42_command_t extra = {0};
    extra.cmd.setmode.mode = 99; // should be overridden by explicit mode argument
    extra.cmd.setmode.have_pri = 1;
    extra.cmd.setmode.have_qrn = 1;
    extra.cmd.setmode.priW[0] = 9.9;
    extra.cmd.setmode.qrn[0] = 1.0;

    rc = simulith_42_send_set_mode(8, 24, &extra.cmd.setmode);
    TEST_ASSERT_EQUAL_INT(0, rc);
    rc = dequeue_command(&out);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_SET_MODE, out.type);
    TEST_ASSERT_EQUAL_INT(8, out.spacecraft_id);
    TEST_ASSERT_EQUAL_INT(24, out.cmd.setmode.mode); // mode should be set to argument
    TEST_ASSERT_EQUAL_INT(1, out.cmd.setmode.have_pri);
    TEST_ASSERT_EQUAL_INT(1, out.cmd.setmode.have_qrn);
    TEST_ASSERT_EQUAL_FLOAT((float)9.9, (float)out.cmd.setmode.priW[0]);
    TEST_ASSERT_EQUAL_FLOAT((float)1.0, (float)out.cmd.setmode.qrn[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_enqueue_dequeue_basic);
    RUN_TEST(test_queue_overflow);
    RUN_TEST(test_helper_wrappers);
    RUN_TEST(test_set_mode_extra_and_defaults);
    return UNITY_END();
}

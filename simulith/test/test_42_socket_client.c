#include "simulith_42_socket_client.h"
#include "unity.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct
{
    int listen_fd;
    char batch[2048];
    char empty[64];
} fake_42_t;

static void *fake_42_server(void *arg)
{
    fake_42_t *server = arg;
    int client = accept(server->listen_fd, NULL, NULL);
    if (client < 0)
        return NULL;

    static const char state[] =
        "TIME 2026-001-01:02:03.500\n"
        "SC[0].qn = [1 0 0 0]\n"
        "SC[0].wn = [1 2 3]\n"
        "Orb[0].PosN = [4 5 6]\n"
        "Orb[0].VelN = [7 8 9]\n"
        "SC[0].svb = [0 0 1]\n"
        "SC[0].bvb = [10 11 12]\n"
        "SC[0].Hvb = [13 14 15]\n";
    send(client, state, sizeof(state) - 1, 0);

    char ack[8] = {0};
    recv(client, ack, sizeof(ack), 0);
    recv(client, server->batch, sizeof(server->batch) - 1, 0);
    send(client, "Ack", 4, 0);
    recv(client, server->empty, sizeof(server->empty) - 1, 0);
    send(client, "Ack", 4, 0);
    close(client);
    return NULL;
}

void setUp(void)
{
    simulith_42_cleanup();
    setenv("SIMULITH_42_RECONNECT_ATTEMPTS", "1", 1);
    setenv("SIMULITH_42_RECONNECT_DELAY_MS", "0", 1);
}

void tearDown(void)
{
    simulith_42_cleanup();
}

static void test_parse_state_fields_and_eclipse(void)
{
    simulith_42_context_t context;
    TEST_ASSERT_EQUAL_INT(0, simulith_42_parse_state_for_test(
        "TIME 2026-001-02:03:04.25\nSC[0].svb = [0 0 0]\n", &context));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7384.25f, (float)context.sim_time);
    TEST_ASSERT_EQUAL_INT(1, context.valid);
    TEST_ASSERT_EQUAL_INT(1, context.eclipse);
    TEST_ASSERT_EQUAL_INT(-1, simulith_42_parse_state_for_test(NULL, &context));
    TEST_ASSERT_EQUAL_INT(-1, simulith_42_parse_state_for_test("", NULL));
}

static void test_unconnected_and_invalid_connections(void)
{
    simulith_42_context_t context;
    simulith_42_command_t command = {0};
    char long_path[256];
    memset(long_path, 'x', sizeof(long_path));
    long_path[0] = '/';
    long_path[sizeof(long_path) - 1] = '\0';

    TEST_ASSERT_EQUAL_INT(-1, simulith_42_request_state(&context));
    TEST_ASSERT_EQUAL_INT(-1, simulith_42_send_empty_commands());
    TEST_ASSERT_EQUAL_INT(-1, simulith_42_send_command_batch(&command, 1));
    TEST_ASSERT_EQUAL_INT(-1, simulith_42_init(long_path, 0));
    TEST_ASSERT_EQUAL_INT(-1, simulith_42_init("/tmp/shire-missing-42.sock", 0));
    TEST_ASSERT_EQUAL_INT(0, simulith_42_is_connected());
}

static void test_unix_connection_state_and_commands(void)
{
    char directory[] = "/tmp/shire-42-test-XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(directory));

    char path[108];
    snprintf(path, sizeof(path), "%s/socket", directory);
    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, listen_fd);

    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);
    TEST_ASSERT_EQUAL_INT(0, bind(listen_fd, (struct sockaddr *)&address, sizeof(address)));
    TEST_ASSERT_EQUAL_INT(0, listen(listen_fd, 1));

    fake_42_t server = {.listen_fd = listen_fd};
    pthread_t thread;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&thread, NULL, fake_42_server, &server));
    TEST_ASSERT_EQUAL_INT(0, simulith_42_init(path, 0));
    TEST_ASSERT_EQUAL_INT(1, simulith_42_is_connected());

    simulith_42_context_t context;
    TEST_ASSERT_EQUAL_INT(0, simulith_42_request_state(&context));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3723.5f, (float)context.sim_time);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.0f, (float)context.pos_n[2]);
    TEST_ASSERT_EQUAL_INT(0, context.eclipse);

    simulith_42_command_t commands[4] = {0};
    commands[0].valid = 1;
    commands[0].type = SIMULITH_42_CMD_WHEEL_TORQUE;
    commands[0].spacecraft_id = 2;
    commands[0].cmd.wheel.enable_mask = 0x3;
    commands[0].cmd.wheel.torque[0] = 1.25;
    commands[1].valid = 1;
    commands[1].type = SIMULITH_42_CMD_MTB_TORQUE;
    commands[1].cmd.mtb.enable_mask = 0x1;
    commands[1].cmd.mtb.dipole[0] = 2.5;
    commands[2].valid = 0;
    commands[3].valid = 1;
    commands[3].type = SIMULITH_42_CMD_SET_MODE;
    TEST_ASSERT_EQUAL_INT(0, simulith_42_send_command_batch(commands, 4));
    TEST_ASSERT_EQUAL_INT(0, simulith_42_send_empty_commands());

    pthread_join(thread, NULL);
    TEST_ASSERT_NOT_NULL(strstr(server.batch, "Whl[0].Tcmd"));
    TEST_ASSERT_NOT_NULL(strstr(server.batch, "MTB[0].Mcmd"));
    TEST_ASSERT_NOT_NULL(strstr(server.batch, "[ENDMSG]"));
    TEST_ASSERT_NOT_NULL(strstr(server.empty, "[ENDMSG]"));

    close(listen_fd);
    unlink(path);
    rmdir(directory);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_state_fields_and_eclipse);
    RUN_TEST(test_unconnected_and_invalid_connections);
    RUN_TEST(test_unix_connection_state_and_commands);
    return UNITY_END();
}

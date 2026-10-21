/* Carry cFS UDP packet boundaries over an SSH-forwarded TCP connection.
 * Both endpoints use a four-byte network-order payload length. */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

static int transfer(int fd, void *data, size_t length, int writing)
{
    char *cursor = data;
    while (length)
    {
        ssize_t count = writing ? send(fd, cursor, length, MSG_NOSIGNAL) : recv(fd, cursor, length, 0);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        cursor += count;
        length -= (size_t)count;
    }
    return 0;
}

static int port_argument(const char *value)
{
    char *end;
    errno = 0;
    long port = strtol(value, &end, 10);
    return errno || !value[0] || *end || port < 1 || port > 65535 ? -1 : (int)port;
}

int main(int argc, char **argv)
{
    int tunnel_port = 50198, command_port = 1234, telemetry_port = 1235;
    if (argc == 4)
    {
        tunnel_port = port_argument(argv[1]);
        command_port = port_argument(argv[2]);
        telemetry_port = port_argument(argv[3]);
    }
    if ((argc != 1 && argc != 4) || tunnel_port < 0 || command_port < 0 || telemetry_port < 0)
    {
        fputs("usage: shire-board-udp [tunnel-port command-port telemetry-port]\n", stderr);
        return 1;
    }
    int stream = socket(AF_INET, SOCK_STREAM, 0);
    int udp = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
                                  .sin_port = htons((uint16_t)tunnel_port)};
    int enabled = 1;
    if (stream < 0 || udp < 0 || setsockopt(stream, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) ||
        connect(stream, (struct sockaddr *)&address, sizeof(address)))
    {
        perror("Board UDP tunnel connect");
        return 1;
    }
    address.sin_port = htons((uint16_t)telemetry_port);
    if (bind(udp, (struct sockaddr *)&address, sizeof(address)))
    {
        perror("Board telemetry bind");
        return 1;
    }
    address.sin_port = htons((uint16_t)command_port);
    puts("SHIRE_BOARD_UDP_READY");
    fflush(stdout);
    struct pollfd sockets[] = {{udp, POLLIN, 0}, {stream, POLLIN, 0}};
    unsigned char packet[65507];
    for (;;)
    {
        int status = poll(sockets, 2, -1);
        if (status < 0 && errno == EINTR)
            continue;
        if (status < 0)
            break;
        if (sockets[0].revents & POLLIN)
        {
            ssize_t count = recv(udp, packet, sizeof(packet), 0);
            if (count < 0)
                break;
            uint32_t length = htonl((uint32_t)count);
            if (transfer(stream, &length, sizeof(length), 1) || transfer(stream, packet, (size_t)count, 1))
                break;
        }
        if (sockets[1].revents & POLLIN)
        {
            uint32_t length;
            if (transfer(stream, &length, sizeof(length), 0))
                break;
            length = ntohl(length);
            if (length > sizeof(packet) || transfer(stream, packet, length, 0) ||
                sendto(udp, packet, length, 0, (struct sockaddr *)&address, sizeof(address)) != (ssize_t)length)
                break;
        }
        if (sockets[1].revents & (POLLHUP | POLLERR | POLLNVAL))
            break;
    }
    close(stream);
    close(udp);
    return 1;
}

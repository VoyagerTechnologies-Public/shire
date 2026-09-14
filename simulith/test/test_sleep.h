#ifndef SIMULITH_TEST_SLEEP_H
#define SIMULITH_TEST_SLEEP_H

#include <errno.h>
#include <time.h>

static inline void test_sleep_us(long microseconds)
{
    struct timespec delay = {
        .tv_sec = microseconds / 1000000L,
        .tv_nsec = (microseconds % 1000000L) * 1000L,
    };

    while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
    {
    }
}

#endif

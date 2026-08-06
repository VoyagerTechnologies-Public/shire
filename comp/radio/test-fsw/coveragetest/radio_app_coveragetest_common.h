#ifndef RADIO_APP_COVERAGETEST_COMMON_H
#define RADIO_APP_COVERAGETEST_COMMON_H

/*
 * Includes
 */

#include "utassert.h"
#include "uttest.h"
#include "utstubs.h"

#include "cfe.h"
#include "radio_events.h"
#include "radio_app.h"

/*
 * Device state constants for unit testing
 */
#define SPI_DEVICE_OPEN    1
#define SPI_DEVICE_CLOSED  0
#define GPIO_OPEN          1
#define GPIO_CLOSED        0

/*
 * Macro to call a function and check its int32 return code
 */
#define UT_TEST_FUNCTION_RC(func, exp)                                                                \
    {                                                                                                 \
        int32 rcexp = exp;                                                                            \
        int32 rcact = func;                                                                           \
        UtAssert_True(rcact == rcexp, "%s (%ld) == %s (%ld)", #func, (long)rcact, #exp, (long)rcexp); \
    }

/*
 * Macro to add a test case to the list of tests to execute
 */
#define ADD_TEST(test) UtTest_Add((Test_##test), Radio_UT_Setup, Radio_UT_TearDown, #test)

/*
 * Setup function prior to every test
 */
void Radio_UT_Setup(void);

/*
 * Teardown function after every test
 */
void Radio_UT_TearDown(void);

#endif /* RADIO_APP_COVERAGETEST_COMMON_H */

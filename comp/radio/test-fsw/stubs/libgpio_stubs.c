#include "libgpio.h"
#include "utgenstub.h"

int32_t gpio_init(gpio_info_t* device)
{
    UT_GenStub_SetupReturnBuffer(gpio_init, int32_t);

    UT_GenStub_AddParam(gpio_init, gpio_info_t*, device);

    UT_GenStub_Execute(gpio_init, Basic, NULL);

    return UT_GenStub_GetReturnValue(gpio_init, int32_t);
}

int32_t gpio_read(gpio_info_t* device, uint8_t* value)
{
    UT_GenStub_SetupReturnBuffer(gpio_read, int32_t);

    UT_GenStub_AddParam(gpio_read, gpio_info_t*, device);
    UT_GenStub_AddParam(gpio_read, uint8_t*, value);

    UT_GenStub_Execute(gpio_read, Basic, NULL);

    return UT_GenStub_GetReturnValue(gpio_read, int32_t);
}

int32_t gpio_write(gpio_info_t* device, uint8_t value)
{
    UT_GenStub_SetupReturnBuffer(gpio_write, int32_t);

    UT_GenStub_AddParam(gpio_write, gpio_info_t*, device);
    UT_GenStub_AddParam(gpio_write, uint8_t, value);

    UT_GenStub_Execute(gpio_write, Basic, NULL);

    return UT_GenStub_GetReturnValue(gpio_write, int32_t);
}

int32_t gpio_close(gpio_info_t* device)
{
    UT_GenStub_SetupReturnBuffer(gpio_close, int32_t);

    UT_GenStub_AddParam(gpio_close, gpio_info_t*, device);

    UT_GenStub_Execute(gpio_close, Basic, NULL);

    return UT_GenStub_GetReturnValue(gpio_close, int32_t);
}

void gpio_dummy(void) { }

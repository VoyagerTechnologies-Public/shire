#include "libuart.h"
#include "utgenstub.h"
#include <string.h>

/*
 * ----------------------------------------------------
 * Custom handler for uart_read_port() to copy data
 * ----------------------------------------------------
 */
void UT_DefaultHandler_uart_read_port(void *UserObj, UT_EntryKey_t FuncKey, const UT_StubContext_t *Context)
{
    uint8_t *data = UT_Hook_GetArgValueByName(Context, "data", uint8_t *);
    uint32_t numBytes = UT_Hook_GetArgValueByName(Context, "numBytes", uint32_t);
    
    /* Use UT_Stub_CopyToLocal to automatically handle buffer position */
    if (data != NULL && numBytes > 0)
    {
        UT_Stub_CopyToLocal(UT_KEY(uart_read_port), data, numBytes);
    }
}

/*
 * ----------------------------------------------------
 * Generated stub function for uart_bytes_available()
 * ----------------------------------------------------
 */
int32_t uart_bytes_available(uart_info_t *device)
{
    UT_GenStub_SetupReturnBuffer(uart_bytes_available, int32_t);

    UT_GenStub_AddParam(uart_bytes_available, uart_info_t *, device);

    UT_GenStub_Execute(uart_bytes_available, Basic, NULL);

    return UT_GenStub_GetReturnValue(uart_bytes_available, int32_t);
}

/*
 * ----------------------------------------------------
 * Generated stub function for uart_close_port()
 * ----------------------------------------------------
 */
int32_t uart_close_port(uart_info_t *device)
{
    UT_GenStub_SetupReturnBuffer(uart_close_port, int32_t);

    UT_GenStub_AddParam(uart_close_port, uart_info_t *, device);

    UT_GenStub_Execute(uart_close_port, Basic, NULL);

    return UT_GenStub_GetReturnValue(uart_close_port, int32_t);
}

/*
 * ----------------------------------------------------
 * Generated stub function for uart_flush()
 * ----------------------------------------------------
 */
int32_t uart_flush(uart_info_t *device)
{
    UT_GenStub_SetupReturnBuffer(uart_flush, int32_t);

    UT_GenStub_AddParam(uart_flush, uart_info_t *, device);

    UT_GenStub_Execute(uart_flush, Basic, NULL);

    return UT_GenStub_GetReturnValue(uart_flush, int32_t);
}

/*
 * ----------------------------------------------------
 * Generated stub function for uart_init_port()
 * ----------------------------------------------------
 */
int32_t uart_init_port(uart_info_t *device)
{
    UT_GenStub_SetupReturnBuffer(uart_init_port, int32_t);

    UT_GenStub_AddParam(uart_init_port, uart_info_t *, device);

    UT_GenStub_Execute(uart_init_port, Basic, NULL);

    return UT_GenStub_GetReturnValue(uart_init_port, int32_t);
}

/*
 * ----------------------------------------------------
 * Generated stub function for uart_read_port()
 * ----------------------------------------------------
 */
int32_t uart_read_port(uart_info_t *device, uint8_t data[], const uint32_t numBytes)
{
    UT_GenStub_SetupReturnBuffer(uart_read_port, int32_t);

    UT_GenStub_AddParam(uart_read_port, uart_info_t *, device);
    UT_GenStub_AddParam(uart_read_port, uint8_t *, data);
    UT_GenStub_AddParam(uart_read_port, const uint32_t, numBytes);

    UT_GenStub_Execute(uart_read_port, Basic, UT_DefaultHandler_uart_read_port);

    return UT_GenStub_GetReturnValue(uart_read_port, int32_t);
}

/*
 * ----------------------------------------------------
 * Generated stub function for uart_write_port()
 * ----------------------------------------------------
 */
int32_t uart_write_port(uart_info_t *device, uint8_t data[], const uint32_t numBytes)
{
    UT_GenStub_SetupReturnBuffer(uart_write_port, int32_t);

    UT_GenStub_AddParam(uart_write_port, uart_info_t *, device);
    UT_GenStub_AddParam(uart_write_port, const uint32_t, numBytes);

    UT_GenStub_Execute(uart_write_port, Basic, NULL);

    return UT_GenStub_GetReturnValue(uart_write_port, int32_t);
}

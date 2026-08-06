#include "libspi.h"
#include "utgenstub.h"

int32_t spi_init_dev(spi_info_t* device)
{
    UT_GenStub_SetupReturnBuffer(spi_init_dev, int32_t);

    UT_GenStub_AddParam(spi_init_dev, spi_info_t*, device);

    UT_GenStub_Execute(spi_init_dev, Basic, NULL);

    return UT_GenStub_GetReturnValue(spi_init_dev, int32_t);
}

int32_t spi_set_mode(spi_info_t* device)
{
    UT_GenStub_SetupReturnBuffer(spi_set_mode, int32_t);

    UT_GenStub_AddParam(spi_set_mode, spi_info_t*, device);

    UT_GenStub_Execute(spi_set_mode, Basic, NULL);

    return UT_GenStub_GetReturnValue(spi_set_mode, int32_t);
}

int32_t spi_get_mode(spi_info_t* device)
{
    UT_GenStub_SetupReturnBuffer(spi_get_mode, int32_t);

    UT_GenStub_AddParam(spi_get_mode, spi_info_t*, device);

    UT_GenStub_Execute(spi_get_mode, Basic, NULL);

    return UT_GenStub_GetReturnValue(spi_get_mode, int32_t);
}

int32_t spi_write(spi_info_t* device, uint8_t data[], const uint32_t numBytes)
{
    UT_GenStub_SetupReturnBuffer(spi_write, int32_t);

    UT_GenStub_AddParam(spi_write, spi_info_t*, device);
    UT_GenStub_AddParam(spi_write, uint8_t*, data);
    UT_GenStub_AddParam(spi_write, const uint32_t, numBytes);

    UT_GenStub_Execute(spi_write, Basic, NULL);

    return UT_GenStub_GetReturnValue(spi_write, int32_t);
}

int32_t spi_read(spi_info_t* device, uint8_t data[], const uint32_t numBytes)
{
    UT_GenStub_SetupReturnBuffer(spi_read, int32_t);

    UT_GenStub_AddParam(spi_read, spi_info_t*, device);
    UT_GenStub_AddParam(spi_read, uint8_t*, data);
    UT_GenStub_AddParam(spi_read, const uint32_t, numBytes);

    UT_GenStub_Execute(spi_read, Basic, NULL);

    return UT_GenStub_GetReturnValue(spi_read, int32_t);
}

int32_t spi_transaction(spi_info_t* device, uint8_t *txBuff, uint8_t * rxBuffer, uint32_t length, uint16_t delay, uint8_t bits, uint8_t deselect)
{
    UT_GenStub_SetupReturnBuffer(spi_transaction, int32_t);

    UT_GenStub_AddParam(spi_transaction, spi_info_t*, device);
    UT_GenStub_AddParam(spi_transaction, uint8_t*, txBuff);
    UT_GenStub_AddParam(spi_transaction, uint8_t*, rxBuffer);
    UT_GenStub_AddParam(spi_transaction, uint32_t, length);
    UT_GenStub_AddParam(spi_transaction, uint16_t, delay);
    UT_GenStub_AddParam(spi_transaction, uint8_t, bits);
    UT_GenStub_AddParam(spi_transaction, uint8_t, deselect);

    UT_GenStub_Execute(spi_transaction, Basic, NULL);

    return UT_GenStub_GetReturnValue(spi_transaction, int32_t);
}

int32_t spi_select_chip(spi_info_t* device)
{
    UT_GenStub_SetupReturnBuffer(spi_select_chip, int32_t);

    UT_GenStub_AddParam(spi_select_chip, spi_info_t*, device);

    UT_GenStub_Execute(spi_select_chip, Basic, NULL);

    return UT_GenStub_GetReturnValue(spi_select_chip, int32_t);
}

int32_t spi_unselect_chip(spi_info_t* device)
{
    UT_GenStub_SetupReturnBuffer(spi_unselect_chip, int32_t);

    UT_GenStub_AddParam(spi_unselect_chip, spi_info_t*, device);

    UT_GenStub_Execute(spi_unselect_chip, Basic, NULL);

    return UT_GenStub_GetReturnValue(spi_unselect_chip, int32_t);
}

int32_t spi_close_device(spi_info_t* device)
{
    UT_GenStub_SetupReturnBuffer(spi_close_device, int32_t);

    UT_GenStub_AddParam(spi_close_device, spi_info_t*, device);

    UT_GenStub_Execute(spi_close_device, Basic, NULL);

    return UT_GenStub_GetReturnValue(spi_close_device, int32_t);
}
